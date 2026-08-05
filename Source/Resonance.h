#pragma once
// =============================================================================
//  VocalGzzio v2.4.0 —「なめらか」動的レゾナンス抑制
//
//  声には「その人の声道 × 部屋 × マイク」で決まる、耳に刺さる周波数があります。
//  1.5kHz が硬い、3kHz が刺さる、200Hz がボワつく。しかも音程が変わると刺さる
//  場所も動くので、固定EQでは追いきれません。これを自動で、動いたぶんだけ
//  追いかけて削るのがこの処理です。
//
//  ● なぜゼロ遅延でできるのか
//    FFTで解析して削る方式(soothe等)は、窓の長さぶんの遅延が必ず出ます。
//    ここでは代わりに「2次オールパス1本」を各バンドに置きます。オールパスは
//    中心周波数 f0 で位相が反転する(A(e^jw0) = -1)ので、
//
//        bandpass = 0.5 * (x - A(x))          … f0 で 1、離れると 0
//        notch    = x - (1 - K) * bandpass    … f0 のゲインが K になる
//
//    という2つが1本から同時に取れます。**K は係数ではなく単なる掛け算**なので、
//    サンプル単位で自由に動かしても係数の作り直しが一切発生しません。
//    (Regalia–Mitra 型パラメトリックEQと同じ構造です)
//
//  ● 何を「出っ張り」とみなすか
//    各バンドの包絡を取り、その**近傍バンドの対数平均**(＝スペクトルの地ならし線)
//    と比べます。自分だけが地ならし線より突出していたら、その差に比例して削る。
//    全体が持ち上がっているだけの帯域は削りません。だから「声の芯」は残ります。
//
//  JUCE に依存しません。tools/dsp_resonance.cpp から同じコードを直接テストします。
// =============================================================================

#include <cmath>
#include <algorithm>

namespace gz::res
{

// ---- 2次オールパス (Regalia–Mitra) ------------------------------------------
// A(z) = (-c + d(1-c)z^-1 + z^-2) / (1 + d(1-c)z^-1 - c z^-2)
//   c = (tan(pi*BW/fs) - 1) / (tan(pi*BW/fs) + 1)
//   d = -cos(2*pi*f0/fs)
// f0 で A = -1、遠くで A = +1。係数は f0 と BW だけで決まる＝実行中は不変。
struct Allpass2
{
    float c = 0.0f, e = 0.0f;               // e = d * (1 - c) を先に畳んでおく
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

    void set (double f0, double bw, double sr)
    {
        const double pi = 3.14159265358979323846;
        const double t  = std::tan (pi * bw / sr);
        const double d  = -std::cos (2.0 * pi * f0 / sr);
        c = (float) ((t - 1.0) / (t + 1.0));
        e = (float) (d * (1.0 - (double) c));
    }

    inline float process (float x) noexcept
    {
        const float y = -c * x + e * x1 + x2 - e * y1 + c * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }
};

// ---- 本体 --------------------------------------------------------------------
class Tamer
{
public:
    // 対象は 900Hz〜9kHz。下を 900Hz で切るのは意図的:
    // 低域では倍音の間隔(男声100〜150Hz)よりバンド幅が狭くなり、**普通の倍音
    // 1本1本が「出っ張り」に見えて**声の芯を削ってしまう。900Hz以上なら倍音は
    // バンド幅より密に並ぶので、この誤検出が起きない。低域のボワつきは既存の
    // 「こもり」ノブの守備範囲。
    static constexpr int kBands = 24;       // 900Hz〜9kHz を対数等分
    static constexpr int kCtrl  = 32;       // ゲイン計算はこのサンプル数ごと
    static constexpr int kNeigh = 5;        // 地ならし線に使う左右のバンド数

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double f0 = 900.0, f1 = 9000.0;
        const double r  = std::pow (f1 / f0, 1.0 / (double) (kBands - 1));
        for (int b = 0; b < kBands; ++b)
        {
            centre[b] = f0 * std::pow (r, (double) b);
            // 帯域幅は「隣のバンドまでの距離」の 1.1 倍。狭すぎると取りこぼし、
            // 広すぎると声の芯まで削るので、この辺りが実測でいちばん素直だった。
            const double bw = centre[b] * (r - 1.0 / r) * 0.5 * 1.25;
            for (int ch = 0; ch < 2; ++ch)
                ap[ch][b].set (centre[b], std::max (20.0, bw), sr);
        }
        envA = tc (5.0);    envR = tc (50.0);      // バンド包絡
        gDn  = tc (8.0);    gUp  = tc (120.0);     // ゲインの寄せ方(削る/戻す)
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int b = 0; b < kBands; ++b) { ap[ch][b].reset(); env[ch][b] = 0.0f; }
        for (int b = 0; b < kBands; ++b) { kTarget[b] = 1.0f; kNow[b] = 1.0f; }
        ctrlCount = 0; maxCutDb = 0.0f;
    }

    // amount 0..1。0 のときは何もしない(呼び出し側で丸ごと飛ばしてよい)
    void setAmount (float a) noexcept { amount = std::min (1.0f, std::max (0.0f, a)); }

    // L / R をその場で処理する。R は nullptr 可(モノラル)。
    void process (float* L, float* R, int numSamples) noexcept
    {
        if (amount <= 0.0005f) return;
        float peakCut = 0.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            // --- 制御レート: 出っ張りぶんを見てバンドごとの目標ゲインを決める ---
            if (ctrlCount-- <= 0)
            {
                ctrlCount = kCtrl - 1;
                updateTargets();
            }

            for (int b = 0; b < kBands; ++b)
            {
                // ゲインは毎サンプル目標へ寄せる(段差ノイズを出さないため)
                const float a = (kTarget[b] < kNow[b]) ? gDn : gUp;
                kNow[b] += a * (kTarget[b] - kNow[b]);
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = (ch == 0) ? L : R;
                if (p == nullptr) break;
                float v = p[n];
                for (int b = 0; b < kBands; ++b)
                {
                    const float a  = ap[ch][b].process (v);
                    const float bp = 0.5f * (v - a);           // f0 まわりだけ取り出した成分
                    const float m  = std::fabs (bp);
                    float& e = env[ch][b];
                    e += ((m > e) ? envA : envR) * (m - e);
                    v -= (1.0f - kNow[b]) * bp;                // = ノッチ(深さ kNow[b])
                }
                p[n] = v;
            }
        }

        for (int b = 0; b < kBands; ++b)
            peakCut = std::max (peakCut, 1.0f - kNow[b]);
        maxCutDb = (peakCut > 0.0f) ? -20.0f * std::log10 (std::max (1.0e-4f, 1.0f - peakCut)) : 0.0f;
    }

    float lastMaxCutDb() const noexcept { return maxCutDb; }
    double bandCentre (int b) const noexcept { return centre[b]; }
    float  bandGain   (int b) const noexcept { return kNow[b]; }

private:
    float tc (double ms) const
    {
        return (float) (1.0 - std::exp (-1.0 / (sr * ms * 0.001)));
    }

    void updateTargets() noexcept
    {
        // 左右の平均をとって「片チャンネルだけ削れて定位が動く」のを防ぐ
        float lin[kBands], lg[kBands];
        for (int b = 0; b < kBands; ++b)
        {
            lin[b] = 0.5f * (env[0][b] + env[1][b]);
            lg[b]  = std::log (lin[b] + 1.0e-9f);
        }

        // 地ならし線 = 近傍バンドの対数平均。**自分と両隣は入れない**。
        // レゾナンスには裾があり、両隣のバンドも一緒に持ち上がる。それを基準に
        // 含めると基準線ごと上がって「出っ張りが小さく見える」ため、1バンド
        // 飛ばした外側(±2〜±kNeigh+1)だけで地ならし線を作る。
        for (int b = 0; b < kBands; ++b)
        {
            float sum = 0.0f; int cnt = 0;
            for (int j = b - (kNeigh + 1); j <= b + (kNeigh + 1); ++j)
            {
                if (j < 0 || j >= kBands || std::abs (j - b) < 2) continue;
                sum += lg[j]; ++cnt;
            }
            const float ref = (cnt > 0) ? (sum / (float) cnt) : lg[b];
            // 出っ張り量 (dB)
            const float excessDb = 8.685889638f * (lg[b] - ref);   // 20/ln(10)

            float cutDb = 0.0f;
            // 絶対的に小さい帯域はさわらない(無音のノイズ床を削っても意味がない)
            if (lin[b] > 3.0e-4f && excessDb > kThreshDb)
                cutDb = (excessDb - kThreshDb) * kRatio;

            cutDb = std::min (cutDb, kMaxCutDb) * amount;
            kTarget[b] = std::pow (10.0f, -cutDb / 20.0f);
        }
    }

    static constexpr float kThreshDb =  1.8f;   // これ以上出っ張ったら削り始める
    static constexpr float kRatio    =  1.00f;  // 出っ張りの超過分をそのまま削る
    static constexpr float kMaxCutDb = 10.0f;   // 削りすぎ防止の上限

    double sr = 48000.0;
    double centre[kBands] {};
    Allpass2 ap[2][kBands];
    float env[2][kBands] {};
    float kTarget[kBands] {}, kNow[kBands] {};
    float envA = 0.0f, envR = 0.0f, gDn = 0.0f, gUp = 0.0f;
    float amount = 0.0f, maxCutDb = 0.0f;
    int   ctrlCount = 0;
};

} // namespace gz::res
