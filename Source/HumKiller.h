#pragma once
// =============================================================================
//  VocalGzzio v2.6.0 —「ジー音」電源ハムノイズの自動除去
//
//  安いケーブル、アース不良、PCの電源、シールドの弱いギター。原因は色々ですが、
//  出てくる音はいつも同じ「ジー…」です。正体は電源周波数(日本は東50Hz /
//  西60Hz、北米60Hz)とその倍音が、ずっと同じ大きさで鳴り続けるもの。
//
//  ● なぜ「ノッチ(その周波数を削るEQ)」ではダメなのか
//    100Hz・150Hz・200Hz は、男性の声の基音がまさに乗っている場所です。そこを
//    EQで削ると、ジーと一緒に**声の芯まで消えます**。低い声ほど痩せます。
//
//  ● ここでやっていること: 「同じ音を作って引き算する」
//    50Hz(または60Hz)ちょうどの正弦波を内部で作り、その**大きさと位相**を
//    入力に合うよう少しずつ調整して、引き算します(LMS適応フィルタ)。
//
//      誤差 e = 入力 − (a·cos + b·sin)
//      a += μ·e·cos ,  b += μ·e·sin
//
//    捕まえられるのは「その周波数ちょうどで、ずっと同じ大きさ・同じ位相で
//    鳴り続けているもの」だけ。歌は音程も大きさも常に動くので捕まりません。
//    つまり **声には触れずにジーだけを引き算で消せます**。フィルタを通さない
//    ので当然ゼロ遅延、位相も変わりません。
//
//  ● 誤爆しないための3つの仕掛け
//    1) 検出は基本波(50 / 60Hz)だけを見る。歌がこの高さを長く伸ばすことは無い。
//    2) **一貫性チェック**: 本物のハムは a,b がほぼ止まって見える。周波数が
//       少しでもズレた音(例: B1=61.7Hz のベース)だと a,b が毎秒くるくる回る。
//       「1.5秒平均した長さ ÷ 今の長さ」がほぼ1のときだけ本物と認める。
//    3) 学習の速さを入力の大きさで割る(パワー正規化)。大きい音が入っていても
//       推定値がふらつかないので、ハムが無いのに何かを消すことが起きない。
//
//  ● 歌っている間は学習をうんと遅くする
//    静かな場所では素早く合わせ、歌が入っている間はほぼ固定。それでも電源の
//    ゆっくりした揺れ(±0.1Hz)には追従します。
//
//  JUCE に依存しません。tools/dsp_hum.cpp から同じコードを直接テストします。
// =============================================================================

#include <cmath>
#include <algorithm>

namespace gz::hum
{

// ---- 回転による正弦波発振器 (毎サンプル sin/cos を呼ばない) ------------------
struct Osc
{
    float c = 1.0f, s = 0.0f;      // 現在の cos / sin
    float dc = 1.0f, ds = 0.0f;    // 1サンプルぶんの回転
    int   renorm = 0;

    void set (double freq, double sr) noexcept
    {
        const double w = 2.0 * 3.14159265358979323846 * freq / sr;
        dc = (float) std::cos (w); ds = (float) std::sin (w);
        c = 1.0f; s = 0.0f; renorm = 0;
    }
    inline void tick() noexcept
    {
        const float nc = c * dc - s * ds;
        const float ns = s * dc + c * ds;
        c = nc; s = ns;
        // 回転を繰り返すと半径が少しずつずれるので、たまに1へ戻す
        if (++renorm >= 4096)
        {
            renorm = 0;
            const float g = 1.0f / std::sqrt (c * c + s * s + 1.0e-20f);
            c *= g; s *= g;
        }
    }
};

class Killer
{
public:
    static constexpr int kHarm = 8;        // 基本波＋倍音(50Hzなら400Hzまで)

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // 学習の速さ。時定数 τ[秒] に対して μ = 2/(τ·fs)
        muQuiet = (float) (2.0 / (0.30 * sr));   // 静か: 0.3秒で合わせる
        muVoice = (float) (2.0 / (2.00 * sr));   // 歌中: 2秒。歌には追従しない遅さ
        // 「歌っているか」の判定用: 600Hz 3次ハイパス → 包絡
        //  (ハムの倍音は500Hz以下なので、ここには実質入ってこない)
        //  離れを速く(60ms)しているのは、歌の切れ目・息継ぎの一瞬を
        //  「静かな時間」として使い切って、そこで一気に合わせ込むため。
        vdHp = (float) std::exp (-2.0 * 3.14159265358979323846 * 600.0 / sr);
        vdA  = (float) (1.0 - std::exp (-1.0 / (sr * 0.010)));   // 10ms
        vdR  = (float) (1.0 - std::exp (-1.0 / (sr * 0.060)));   // 60ms
        pwA  = (float) (1.0 - std::exp (-1.0 / (sr * 0.030)));   // 入力パワー 30ms
        fastA= (float) (1.0 - std::exp (-1.0 / (sr * 0.150)));   // |w| 0.15秒
        slowA= (float) (1.0 - std::exp (-1.0 / (sr * 1.500)));   // w   1.5秒平均
        appA = (float) (1.0 - std::exp (-1.0 / (sr * 0.050)));   // 効かせ始めの渡り 50ms
        setFreqs();
        reset();
    }

    void reset() noexcept
    {
        for (int sIdx = 0; sIdx < 2; ++sIdx)
        {
            for (int h = 0; h < kHarm; ++h)
            {
                osc[sIdx][h].set (base[sIdx] * (h + 1), sr);
                for (int ch = 0; ch < 2; ++ch) { wa[sIdx][h][ch] = 0.0f; wb[sIdx][h][ch] = 0.0f; }
            }
            mFast[sIdx] = 0.0f; msA[sIdx] = 0.0f; msB[sIdx] = 0.0f;
            score[sIdx] = 0.0f; applyG[sIdx] = 0.0f;
            hCap2[sIdx][0] = hCap2[sIdx][1] = 1.0e-12f;
        }
        hpZ[0] = hpZ[1] = hpZ[2] = 0.0f; vdEnv = 0.0f; inPow = 0.0f;
        sel = -1; ctrl = 0; humDb = -120.0f;
    }

    // amount 0..1
    void setAmount (float a) noexcept { amount = std::min (1.0f, std::max (0.0f, a)); }

    void process (float* L, float* R, int numSamples) noexcept
    {
        if (amount <= 0.0005f) return;
        const int nCh = (R != nullptr) ? 2 : 1;
        float peak = 0.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            float* p[2] = { L, R };
            const float x = (nCh > 1) ? 0.5f * (L[n] + R[n]) : L[n];

            // --- 歌っているか(600Hz以上のエネルギー) ---
            {
                float v = x;
                for (int i = 0; i < 3; ++i)                    // 1次ハイパス×3
                { hpZ[i] += (1.0f - vdHp) * (v - hpZ[i]); v -= hpZ[i]; }
                const float m = std::fabs (v);
                vdEnv += ((m > vdEnv) ? vdA : vdR) * (m - vdEnv);
            }
            inPow += pwA * (x * x - inPow);

            // -45dBFS(0.0056)を境に、なめらかに学習速度を切り替える
            const float voiced = std::min (1.0f, vdEnv * 178.0f);
            // パワー正規化: 大きい音が入っているときほど慎重に動かす。
            // これがないと、ハムが無いのに推定値がふらついて「何かを消して」しまう。
            const float mu = (muQuiet + (muVoice - muQuiet) * voiced)
                             / (1.0f + inPow * 400.0f);

            for (int sIdx = 0; sIdx < 2; ++sIdx)
            {
                // 効かせる量。採用/解除を 50ms かけて渡すので、切り替えで
                // 「プツッ」と鳴らない。採用されていない系列は 0 に落ちる。
                applyG[sIdx] += appA * (((sIdx == sel) ? 1.0f : 0.0f) - applyG[sIdx]);
                const float g = amount * applyG[sIdx];

                // 倍音は両系列とも常に学習しておく。こうしておくと、50/60 が
                // 決まった瞬間からすぐ効く(決まってから覚え始めると数秒遅れる)。
                for (int h = 0; h < kHarm; ++h)
                {
                    if (! active[sIdx][h]) continue;
                    Osc& o = osc[sIdx][h];
                    const float c = o.c, s = o.s;
                    o.tick();
                    // 上の倍音ほど慎重に。ハムの倍音は基本波より小さいのに、
                    // 200Hz あたりは男性の声の基音がまさに通る場所。ゆっくり
                    // 覚えるようにしておくと、たまたま近くを通った歌の音を
                    // 掴んでしまう量が同じだけ減る。ハムは動かないので遅くて困らない。
                    const float muH = mu / (float) (h + 1);

                    for (int ch = 0; ch < nCh; ++ch)
                    {
                        float& a = wa[sIdx][h][ch];
                        float& b = wb[sIdx][h][ch];
                        const float est = a * c + b * s;           // 今わかっているハム
                        const float e   = p[ch][n] - est;          // 残り(＝歌＋その他)
                        a += muH * e * c;
                        b += muH * e * s;
                        // 暴走防止。-16dBFSを超えるハムは機材側の異常なのでそこで頭打ち
                        a = std::min (0.15f, std::max (-0.15f, a));
                        b = std::min (0.15f, std::max (-0.15f, b));
                        // 倍音は基本波の2倍までしか認めない。
                        // 男性が 100Hz(=50Hzの2倍)を長く伸ばすと、そこだけ声を
                        // 掴んでしまいかねない。本物のハムの倍音が基本波より
                        // 大きく出ることはまず無いので、この上限が効くのは
                        // 「声を掴みかけたとき」だけになる。
                        if (h > 0)
                        {
                            const float m2 = a * a + b * b;
                            if (m2 > hCap2[sIdx][ch])
                            {
                                const float g2 = std::sqrt (hCap2[sIdx][ch] / m2);
                                a *= g2; b *= g2;
                            }
                        }
                        if (g > 1.0e-5f) p[ch][n] -= g * est;
                    }
                }
            }

            // --- 検出用のならし(基本波・左chのみ) ---
            for (int sIdx = 0; sIdx < 2; ++sIdx)
            {
                const float a = wa[sIdx][0][0], b = wb[sIdx][0][0];
                mFast[sIdx] += fastA * (std::sqrt (a * a + b * b) - mFast[sIdx]);
                msA  [sIdx] += slowA * (a - msA[sIdx]);
                msB  [sIdx] += slowA * (b - msB[sIdx]);
            }
            if (--ctrl <= 0) { ctrl = (int) (sr * 0.10); updateDetect(); }

            if (sel >= 0)
                for (int h = 0; h < kHarm; ++h)
                    peak = std::max (peak, std::sqrt (wa[sel][h][0] * wa[sel][h][0]
                                                    + wb[sel][h][0] * wb[sel][h][0]));
        }

        humDb = (peak > 1.0e-6f) ? 20.0f * std::log10 (peak) : -120.0f;
    }

    // 0 = 検出なし / 50 / 60
    int   detectedHz() const noexcept { return (sel < 0) ? 0 : (int) base[sel]; }
    // 見つかっているハムの大きさ(dBFS)。UI表示用
    float humLevelDb() const noexcept { return humDb; }

    // 検証用(tools/dsp_hum.cpp から中身を覗くため。製品動作には影響しない)
    float dbgSlow (int sIdx) const noexcept
    { return std::sqrt (msA[sIdx] * msA[sIdx] + msB[sIdx] * msB[sIdx]); }
    float dbgCoh (int sIdx) const noexcept
    { return dbgSlow (sIdx) / (mFast[sIdx] + 1.0e-9f); }
    // 検証用: 採用系列の倍音 h の重みの大きさ。長時間テストで
    // 「声を掴んで溜め込んでいないか」を本来のハム振幅と照合するために使う。
    float dbgHarmMag (int h) const noexcept
    {
        if (sel < 0 || h < 0 || h >= kHarm) return 0.0f;
        const float a = wa[sel][h][0], b = wb[sel][h][0];
        return std::sqrt (a * a + b * b);
    }

private:
    void setFreqs()
    {
        base[0] = 50.0; base[1] = 60.0;
        for (int sIdx = 0; sIdx < 2; ++sIdx)
            for (int h = 0; h < kHarm; ++h)
            {
                const double f = base[sIdx] * (h + 1);
                active[sIdx][h] = (f < sr * 0.45) && (f <= 520.0);
            }
    }

    // 50 と 60 のどちらを採用するか。0.1秒ごとに呼ばれる。
    //
    // ここがこの機能のいちばん大事なところ。「ハムが無いのに何かを消す」のは
    // 絶対に避けたいので、3つの条件を全部満たしたときだけ本物と認める。
    //   (1) 一貫性: 1.5秒平均した長さが、今の長さとほぼ同じ
    //       → 本物のハムは a,b が止まって見える。周波数がズレた持続音
    //         (例 B1=61.7Hz)は a,b が毎秒くるくる回るので平均すると消える。
    //   (2) 相手より十分大きい: 50 と 60 の「地の推定ゆらぎ」はほぼ同じ大きさ。
    //       片方だけが 2.2 倍以上大きければ、それは本物のハム。
    //       この比較のおかげで、入力の大きさに関係なく判定できる。
    //   (3) 絶対値の下限: 聞こえないほど小さいものは触らない。
    void updateDetect() noexcept
    {
        constexpr float kFloor = 2.0e-4f;    // ≒ -74dBFS。これ未満は相手にしない
        constexpr float kCoh   = 0.70f;      // 一貫性
        constexpr float kDom   = 2.2f;       // 相手に対する優位

        // v2.8.0 保険: 状態のどこかが非有限になっていたら全部やり直す。
        // 「長時間でジーが走る」報告を受けて長時間シミュレーション
        // (tools/dsp_longrun.cpp・20分〜6時間ぶん)を回した結果、この部品は
        // シロだったが、引き算する側の状態が一度壊れると**壊れたまま音に
        // 混ざり続ける**構造ではあるので、0.1秒ごとのここで自己回復させる。
        // (64個のfloatを見るだけ。0.1秒に1回なので負荷はゼロに等しい)
        {
            bool bad = false;
            for (int sIdx = 0; sIdx < 2 && ! bad; ++sIdx)
            {
                for (int h = 0; h < kHarm && ! bad; ++h)
                    if (! std::isfinite (wa[sIdx][h][0]) || ! std::isfinite (wb[sIdx][h][0])
                     || ! std::isfinite (wa[sIdx][h][1]) || ! std::isfinite (wb[sIdx][h][1])
                     || ! std::isfinite (osc[sIdx][h].c) || ! std::isfinite (osc[sIdx][h].s))
                        bad = true;
                if (! std::isfinite (mFast[sIdx]) || ! std::isfinite (msA[sIdx])
                 || ! std::isfinite (msB[sIdx])   || ! std::isfinite (applyG[sIdx]))
                    bad = true;
            }
            if (! std::isfinite (vdEnv) || ! std::isfinite (inPow)) bad = true;
            if (bad) { reset(); return; }
        }

        float slow[2];
        for (int sIdx = 0; sIdx < 2; ++sIdx)
        {
            slow[sIdx] = std::sqrt (msA[sIdx] * msA[sIdx] + msB[sIdx] * msB[sIdx]);
            // 倍音の上限(基本波の2倍)を更新。毎サンプルではなくここで作る。
            for (int ch = 0; ch < 2; ++ch)
            {
                const float m2 = wa[sIdx][0][ch] * wa[sIdx][0][ch]
                               + wb[sIdx][0][ch] * wb[sIdx][0][ch];
                hCap2[sIdx][ch] = 4.0f * m2 + 1.0e-12f;      // (2倍)^2
            }
        }

        for (int sIdx = 0; sIdx < 2; ++sIdx)
        {
            const float coh = slow[sIdx] / (mFast[sIdx] + 1.0e-9f);
            // 「今このとき鳴っているか」は 1.5秒平均ではなく現在値で見る。
            // 平均は過去を引きずるので、ケーブルを直してハムが消えても
            // 数秒間ずっと効きっぱなしに見えてしまう。
            const bool live = (mFast[sIdx] > kFloor * 0.5f);
            const bool ok   = live
                           && (slow[sIdx] > kFloor)
                           && (coh > kCoh)
                           && (slow[sIdx] > kDom * slow[1 - sIdx]);
            // 1秒続いたら採用、消えたら1秒で解除(0.1秒ごとに±0.1ずつ)。
            // 鳴ってはいるが判定が一瞬揺れただけのときは、その場に留まる。
            score[sIdx] += ok ? 0.1f : (live ? 0.0f : -0.1f);
            score[sIdx] = std::min (1.0f, std::max (0.0f, score[sIdx]));
        }

        const bool ok0 = (score[0] >= 1.0f), ok1 = (score[1] >= 1.0f);
        if (! ok0 && ! ok1) { sel = -1; return; }
        if (ok0 != ok1)     { sel = ok0 ? 0 : 1; return; }
        sel = (slow[0] >= slow[1]) ? 0 : 1;      // 両立はしない想定だが念のため
    }

    double sr = 48000.0;
    double base[2] = { 50.0, 60.0 };
    bool   active[2][kHarm] {};
    Osc    osc[2][kHarm];
    float  wa[2][kHarm][2] {}, wb[2][kHarm][2] {};
    float  mFast[2] {}, msA[2] {}, msB[2] {}, score[2] {}, applyG[2] {};
    float  hCap2[2][2] {};                 // 倍音に許す大きさの2乗(基本波×2)
    float  appA = 0.0f;
    float  muQuiet = 0.0f, muVoice = 0.0f;
    float  vdA = 0.0f, vdR = 0.0f, vdHp = 0.0f, vdEnv = 0.0f, hpZ[3] {};
    float  pwA = 0.0f, inPow = 0.0f, fastA = 0.0f, slowA = 0.0f;
    float  amount = 0.0f, humDb = -120.0f;
    int    sel = -1, ctrl = 0;
};

} // namespace gz::hum
