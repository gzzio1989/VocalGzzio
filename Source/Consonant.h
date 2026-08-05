#pragma once
// =============================================================================
//  VocalGzzio v2.6.0 —「ことば」子音エンハンサー
//
//  歌が上手いのに歌詞が聞き取れない、という相談はとても多いです。原因はたいてい
//  母音ではなく**子音**。「か・た・ぱ・さ」の出だしは 2〜4kHz あたりに短く出る
//  ぶんが本体で、ここが弱いと言葉が輪郭を失います。しかも子音は母音より小さい
//  ので、コンプをかけるほど埋もれていきます。
//
//  ● やっていること: 「いま母音か、子音か」を音の重心で見分ける
//    母音(あ・い・う…)は 500Hz あたりの低い側が圧倒的に強く、
//    子音(か・た・ぱ…)は 2〜4kHz の高い側が強い。これは声の作られ方から来る
//    はっきりした違いで、大きさや音程には左右されません。
//    そこで2つの帯域の力関係だけを見て、**高い側が優勢な瞬間＝子音のとき
//    だけ** 2.6kHz を最大 +6dB 持ち上げます。
//    母音に切り替わった瞬間に効果は消えるので、EQのように全体が明るくなって
//    うるさくなることがありません。言葉の輪郭だけが前に出ます。
//
//  ● ゼロ遅延でできる理由
//    Resonance.h と同じ Regalia–Mitra 構造です。2次オールパス1本から
//        bandpass = 0.5*(x − A(x))
//        peak     = x + (g − 1)·bandpass      ← f0 のゲインが g になる
//    が取れます。g は係数ではなく単なる掛け算なので、サンプル単位で動かしても
//    係数の作り直しが発生しません。先読みも不要なので遅延ゼロです。
//
//  ● サ行(歯擦音)は持ち上げない
//    「さ・し」は 6kHz 以上が主役で、ここを足すと耳に刺さるだけです(しかも
//    ディエッサーと引っ張り合いになる)。高い側が優勢なときは効果を切ります。
//
//  JUCE に依存しません。tools/dsp_consonant.cpp から同じコードを直接テストします.
// =============================================================================

#include <cmath>
#include <algorithm>

namespace gz::cons
{

// Resonance.h と同じ 2次オールパス (Regalia–Mitra)
struct Allpass2
{
    float c = 0.0f, e = 0.0f;
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
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }
};

class Enhancer
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        apDet.set (2600.0, 2400.0, sr);             // 言葉の輪郭が住んでいるところ
        for (int ch = 0; ch < 2; ++ch) apA[ch].set (2600.0, 2400.0, sr);
        // 歯擦音(サ行)を見るための 6.5kHz ハイパス(1次×2)
        sibZ = (float) std::exp (-2.0 * 3.14159265358979323846 * 6500.0 / sr);
        apLo.set (500.0, 600.0, sr);                // 母音がいるところ
        aFast = tc (3.0);  rFast = tc (18.0);       // 子音帯の包絡
        aLo   = tc (3.0);  rLo   = tc (35.0);       // 母音帯の包絡
        aSib  = tc (3.0);  rSib  = tc (40.0);
        gUp   = tc (2.0);  gDn   = tc (35.0);       // 効かせ方(速く出て、すっと戻る)
        reset();
    }

    void reset() noexcept
    {
        apDet.reset(); apLo.reset();
        for (int ch = 0; ch < 2; ++ch) { apA[ch].reset(); hp[ch][0] = hp[ch][1] = 0.0f; }
        envF = envLo = envSib = 0.0f; gNow = 1.0f; peakDb = 0.0f;
    }

    void setAmount (float a) noexcept { amount = std::min (1.0f, std::max (0.0f, a)); }

    void process (float* L, float* R, int numSamples) noexcept
    {
        if (amount <= 0.0005f) return;
        float peak = 1.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            const float xm = (R != nullptr) ? 0.5f * (L[n] + R[n]) : L[n];

            // --- 判定は左右の平均で。左右で効き方が変わって定位が動くのを防ぐ ---
            const float bpHi = 0.5f * (xm - apDet.process (xm));   // 2.6kHz まわり(子音)
            const float bpLo = 0.5f * (xm - apLo .process (xm));   // 500Hz まわり(母音)
            const float mh = std::fabs (bpHi), ml = std::fabs (bpLo);
            envF  += ((mh > envF ) ? aFast : rFast) * (mh - envF);
            envLo += ((ml > envLo) ? aLo   : rLo  ) * (ml - envLo);

            // 歯擦音(6.5kHz以上)
            float v = xm;
            for (int i = 0; i < 2; ++i) { hp[0][i] += (1.0f - sibZ) * (v - hp[0][i]); v -= hp[0][i]; }
            const float ms = std::fabs (v);
            envSib += ((ms > envSib) ? aSib : rSib) * (ms - envSib);

            // --- 目標ゲイン ---
            float g = 1.0f;
            // 静かすぎる場所ではノイズを持ち上げるだけなので手を出さない
            if (envF + envLo > 2.0e-3f)
            {
                // 重心 = 高い側 ÷ (高い側 + 低い側)。母音で小さく、子音で大きい。
                const float tilt = envF / (envF + envLo + 1.0e-9f);
                if (tilt > kTiltLo)
                {
                    float amt = std::min (1.0f, (tilt - kTiltLo) / (kTiltHi - kTiltLo));
                    // サ行のときは効かせない(6.5kHz以上が優勢ならそれは「さ・し」)
                    const float sibRatio = envSib / (envF + 1.0e-9f);
                    if (sibRatio > kSibHi) amt = 0.0f;
                    else if (sibRatio > kSibLo)
                        amt *= (kSibHi - sibRatio) / (kSibHi - kSibLo);
                    g = 1.0f + (kMaxGain - 1.0f) * amt * amount;
                }
            }
            gNow += ((g > gNow) ? gUp : gDn) * (g - gNow);
            peak = std::max (peak, gNow);

            // --- 適用: ピークフィルタ(中心 2.6kHz、ゲイン gNow) ---
            // 判定に使った bpD は L/R 平均なので、適用は各chで取り直す
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = (ch == 0) ? L : R;
                if (p == nullptr) break;
                const float bp = 0.5f * (p[n] - apA[ch].process (p[n]));
                p[n] += (gNow - 1.0f) * bp;
            }
        }

        peakDb = 20.0f * std::log10 (std::max (1.0f, peak));
    }

    float lastBoostDb() const noexcept { return peakDb; }

private:
    float tc (double ms) const
    { return (float) (1.0 - std::exp (-1.0 / (sr * ms * 0.001))); }

    static constexpr float kTiltLo   = 0.45f;   // 重心がこれを超えたら子音とみなし始める
    static constexpr float kTiltHi   = 0.70f;   // ここで効果が最大になる
    static constexpr float kMaxGain  = 2.0f;    // +6dB
    static constexpr float kSibLo    = 0.55f;   // 高域比がこれを超えたら弱め
    static constexpr float kSibHi    = 1.00f;   //           これを超えたら切る

    double sr = 48000.0;
    Allpass2 apDet, apLo;  // 判定用(L/R平均): 子音帯 / 母音帯
    Allpass2 apA[2];       // 適用用(各ch)
    float hp[2][2] {}, sibZ = 0.0f;
    float envF = 0.0f, envLo = 0.0f, envSib = 0.0f;
    float aFast = 0, rFast = 0, aLo = 0, rLo = 0, aSib = 0, rSib = 0, gUp = 0, gDn = 0;
    float gNow = 1.0f, amount = 0.0f, peakDb = 0.0f;
};

} // namespace gz::cons
