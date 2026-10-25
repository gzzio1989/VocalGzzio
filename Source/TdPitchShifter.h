#pragma once
// TdPitchShifter — ピッチ補正専用の「時間領域・ピッチ同期」超低遅延シフター (v5.1.2 / v5.1.3)
//
// 参考にした考え方:
//   ・TD-PSOLA (Moulines & Charpentier, 1990): 声の基本周期(ピッチ周期)単位で波形を
//     重ね直すと、フォルマント(声道の響き)を保ったまま音程だけを動かせる。
//   ・WSOLA (Verhelst & Roelands, 1993): つなぎ目の位置を相互相関で数サンプル探り、
//     波形が一番そろう所でつなぐと「プツッ」「ジリジリ」が消える。
//   ・遅延線ピッチシフタ (Lexicon/Eventide系の古典): 読み出し位置を (1-比率) で
//     動かすと音程が変わる。ピッチ補正の大半は ±1半音以内なので、フォルマントの
//     ずれは知覚限以下にとどまる(1半音=約6%)。
//
// 仕組み:
//   読み出し遅延 D を毎サンプル (1 - 比率) ずつ動かす。D が許容幅の端に来たら、
//   検出した「ピッチ周期の整数倍 J」だけ D を飛ばし、短いクロスフェードでつなぐ。
//   J は周期の倍数なので、飛ぶ前と後の波形はほぼ同じ形＝つなぎ目が聞こえない。
//   さらに相互相関で最も形がそろう位置へ微調整する(ただし許容幅の内側にだけ)。
//
// v5.1.3「一定時間たつと音質が劣化する」の修正:
//   旧版は出力を「原音(遅延 L0 固定) と 加工音(遅延 D)」の混ぜ合わせで作っていた。
//   D は歌っているうちに少しずつ L0 から離れていくため、補正の入り切り(ミックスの
//   遷移)のたびに 2つの遅延がずれた音が重なり、櫛形フィルタ(シュワシュワ・こもり)が
//   発生していた。時間がたつほど D が離れ、劣化が目立っていた。
//    (1) 原音との混ぜ合わせをやめ、ミックス量は「音程を動かす量」に掛ける。
//        比率1のときの読み出しは単なる遅延なので、常に1本の読み出しだけを聴く＝櫛形が原理的に出ない。
//    (2) D の許容幅を、申告遅延 L0 を中心とした「1周期ぶん」に固定する(ランダムウォーク禁止)。
//    (3) 相関探索の微調整は許容幅の内側にだけ振る(端を越えて往復ジャンプし続けるのを防ぐ)。
//    (4) 無音〜息継ぎの間に D を L0 へ戻す(聞こえない所で原点復帰)。
//
// 遅延: 申告値 L0 = 4サンプル (48kHz で約 0.08ms ＝ 実質ゼロ遅延)。
// 音声スレッド安全: すべてのバッファは prepare() で確保し、process中は確保しない。

#include <vector>
#include <cmath>
#include <algorithm>

namespace gz
{

class TdPitchShifter
{
public:
    void prepare (double sampleRate)
    {
        srHz = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        int sz = 1;
        while (sz < (int) (srHz * 0.080) + 64) sz <<= 1;          // 80ms 分の履歴(最低音 55Hz の周期×4以上)
        buf.assign ((size_t) sz, 0.0f);
        mask = sz - 1;
        Dlo = 4.0f;
        L0  = Dlo;                                                  // 申告遅延 = 4サンプル (48kHzで約0.08ms、実質ゼロ遅延)
        minGrain = (float) (srHz * 0.0035);                         // つなぎ幅の下限 3.5ms
        maxJump  = (float) (srHz / 55.0);                           // 55Hz の1周期まで
        ratioCoef  = 1.0f - std::exp (-1.0f / (0.0012f * (float) srHz));   // 比率の平滑 1.2ms (超高速追従)
        periodCoef = 1.0f - std::exp (-1.0f / (0.008f * (float) srHz));    // 周期の平滑 8ms
        envRel     = std::exp (-1.0f / (0.030f * (float) srHz));           // 無音判定の包絡 30ms
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        w = 0;
        D = L0; fadeD = L0;
        fadePos = fadeLen = 0;
        ratioCur = ratioTgt = 1.0f;
        period = (float) (srHz / 200.0);
        periodTgt = period;
        inEnv = 0.0f;
    }

    int latencySamples() const noexcept { return (int) L0; }

    // 半音単位のシフト量と、効かせる量(0..1)。
    // v5.1.3: 量は「原音との混ぜ合わせ」ではなく「音程を動かす量」に掛ける(櫛形フィルタ防止)。
    void setParams (float semitones, float amount) noexcept
    {
        const float s = std::isfinite (semitones) ? std::clamp (semitones, -12.0f, 12.0f) : 0.0f;
        const float a = std::isfinite (amount) ? std::clamp (amount, 0.0f, 1.0f) : 0.0f;
        ratioTgt = std::pow (2.0f, s * a / 12.0f);
    }

    // 検出した基本周波数(Hz)。0 や範囲外なら直前の周期を使い続ける。
    void setPitchHz (float hz) noexcept
    {
        if (std::isfinite (hz) && hz >= 55.0f && hz <= 1200.0f)
            periodTgt = (float) (srHz / hz);
    }

    void processBlock (float* d, int n) noexcept
    {
        for (int i = 0; i < n; ++i) d[i] = tick (d[i]);
    }

    float currentDelay() const noexcept { return D; }   // 検査用

private:
    float readAt (float delay) const noexcept
    {
        // 4点エルミート補間(3次)。delay >= 2 を前提にする。
        const float pos = (float) w - delay;
        const float fl  = std::floor (pos);
        const float t   = pos - fl;
        const int   i1  = (int) fl;
        const float y0 = buf[(size_t) ((i1 - 1) & mask)];
        const float y1 = buf[(size_t) ( i1      & mask)];
        const float y2 = buf[(size_t) ((i1 + 1) & mask)];
        const float y3 = buf[(size_t) ((i1 + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    float sampleAt (int delay) const noexcept
    {
        return buf[(size_t) ((w - delay) & mask)];
    }

    // WSOLA: 目標の飛び先 targetD を、[offMin, offMax] の範囲で現在の読み出し位置 D と
    // 波形が最もそろう位置へ微調整する(過去 len サンプルの重み付き正規化相互相関＋3点放物線サブサンプル補間)。
    float refineJump (float targetD, int offMin, int offMax) const noexcept
    {
        const int len     = std::clamp ((int) period, 24, 128);
        const int baseOld = (int) std::lround (D);
        const int baseNew = (int) std::lround (targetD);
        const int maxDelay = mask - len - 4;
        float scores[64];
        const int span = std::clamp (offMax - offMin + 1, 1, 63);
        float bestScore = -2.0f; int bestIdx = 0;
        for (int idx = 0; idx < span; ++idx)
        {
            const int off = offMin + idx;
            const int nd = baseNew + off;
            if (nd < 2 || nd > maxDelay) { scores[idx] = -2.0f; continue; }
            double xy = 0.0, xx = 1.0e-12, yy = 1.0e-12;
            for (int m = 0; m < len; ++m)
            {
                const float a = sampleAt (baseOld + m);
                const float b = sampleAt (nd + m);
                const double wgt = (m < 16) ? 2.0 : 1.0; // つなぎ目直近の位相・傾きを重視
                xy += wgt * (double) a * b;
                xx += wgt * (double) a * a;
                yy += wgt * (double) b * b;
            }
            const float score = (float) (xy / std::sqrt (xx * yy));
            scores[idx] = score;
            if (score > bestScore) { bestScore = score; bestIdx = idx; }
        }
        float fracOff = (float) (offMin + bestIdx);
        if (bestIdx > 0 && bestIdx + 1 < span
            && scores[bestIdx - 1] > -1.5f && scores[bestIdx + 1] > -1.5f)
        {
            const float s0 = scores[bestIdx - 1];
            const float s1 = scores[bestIdx];
            const float s2 = scores[bestIdx + 1];
            const float denom = 2.0f * s1 - s0 - s2;
            if (denom > 1.0e-6f)
                fracOff += std::clamp (0.5f * (s2 - s0) / denom, -0.45f, 0.45f);
        }
        return targetD + fracOff;
    }

    void startFade (float newD, float len) noexcept
    {
        fadeD = D; D = newD;
        fadeLen = (int) len; fadePos = 0;
    }

    float tick (float x) noexcept
    {
        if (! std::isfinite (x)) x = 0.0f;
        buf[(size_t) w] = x;

        // 無音判定用の包絡(瞬時アタック・30ms リリース)
        const float ax = std::abs (x);
        inEnv = (ax > inEnv) ? ax : inEnv * envRel;

        ratioCur += (ratioTgt - ratioCur) * ratioCoef;
        if (std::abs (ratioTgt - ratioCur) < 1.0e-6f) ratioCur = ratioTgt;   // 漸近で止まって微小ドリフトしない
        period   += (periodTgt - period) * periodCoef;

        // 飛ぶ幅 J = 周期の整数倍で、最低 minGrain 以上(高い声ほど多くの周期をまたぐ)
        const float k  = std::max (1.0f, std::ceil (minGrain / std::max (8.0f, period)));
        const float J  = std::min (maxJump, k * period);
        const float step = 1.0f - ratioCur;          // 比率>1(上げ) なら D は減る
        // つなぎ中も古い読み出し位置は動き続ける。上げ方向では古い位置が「未来」(遅延<2)へ
        // はみ出さないよう、クロスフェードぶんの余白 margin を残す。
        const float plannedFade = std::clamp (std::min (J, (float) (srHz * 0.004)), 16.0f, 512.0f);
        const float margin = (step < 0.0f) ? -step * plannedFade : 0.0f;
        // 許容幅 = 中心 C の前後 J/2。C は申告遅延 L0(低い声で1周期が長いときだけ後ろへずらす)
        const float C  = std::max (L0, Dlo + margin + 0.5f * J);
        const float lo = C - 0.5f * J;
        const float hi = C + 0.5f * J;

        D += step;
        if (fadePos < fadeLen)
        {
            const float rem = (float) (fadeLen - fadePos);
            const float fadeStep = (step < 0.0f && fadeD + step * rem < 2.2f)
                                 ? (2.2f - fadeD) / (rem + 1.0f) : step;
            fadeD += fadeStep;
        }

        if (fadePos >= fadeLen)                      // つなぎ中は次のジャンプを待つ
        {
            const int radius = std::clamp ((int) (period * 0.10f), 1, 24);
            if (inEnv < kQuiet && std::abs (D - L0) > 0.01f)
            {
                // 無音〜息継ぎ: 聞こえない所で原点(L0)へ戻す
                startFade (L0, plannedFade);
            }
            else if (D < lo)
            {
                // 上へ飛ぶ: 微調整は「下向き」だけ(飛んだ先が上端を越えないように)
                startFade (refineJump (D + J, -radius, 0), plannedFade);
            }
            else if (D > hi)
            {
                // 下へ飛ぶ: 微調整は「上向き」だけ(飛んだ先が下端を割らないように)
                startFade (refineJump (D - J, 0, radius), plannedFade);
            }
        }
        const float maxD = (float) (mask - 8);
        D = std::clamp (D, 2.0f, maxD);

        float y = readAt (D);
        if (fadePos < fadeLen)
        {
            fadeD = std::clamp (fadeD, 2.0f, maxD);
            // sin² クロスフェード(相関が高いので和の音量はほぼ一定)
            const float ph = (float) (fadePos + 1) / (float) (fadeLen + 1);
            const float g  = 0.5f - 0.5f * std::cos (3.14159265f * ph);
            y = g * y + (1.0f - g) * readAt (fadeD);
            ++fadePos;
        }

        w = (w + 1) & mask;
        return y;
    }

    static constexpr float kQuiet = 0.0025f;   // 約 -52dBFS 未満を「無音」とみなす

    std::vector<float> buf;
    int    mask = 0, w = 0;
    double srHz = 48000.0;
    float  Dlo = 4.0f, L0 = 140.0f, minGrain = 168.0f, maxJump = 872.0f;
    float  D = 140.0f, fadeD = 140.0f;
    int    fadePos = 0, fadeLen = 0;
    float  ratioCur = 1.0f, ratioTgt = 1.0f, ratioCoef = 0.01f;
    float  period = 240.0f, periodTgt = 240.0f, periodCoef = 0.002f;
    float  inEnv = 0.0f, envRel = 0.999f;
};

} // namespace gz
