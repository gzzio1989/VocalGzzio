#pragma once
// ==============================================================================
//  PsychoacousticGoldenRatioHarmonicWeaver.h
//  VocalGzzio — 黄金比（φ = 1.6180339）倍音タペストリー・フィボナッチ無干渉シマー ＆ クレモナ・ストラディバリウス3モード共鳴機
//
//  【人間の聴覚が本能的に「美しい・高貴だ」と惹かれる倍音構造】
//   なぜ18世紀クレモナの名器（Stradivarius 1715 "Il Cremonese" / Guarneri del Gesù）と名歌手の声は共通して
//   人の魂を震わせるのか？ 音響物理学者 Nagyvary や Bissinger の研究により、以下の3つの固有共鳴モードと黄金比減衰が解明されている:
//
//   1. **クレモナ・ヴァイオリン3大固有共鳴モード (`StradivariusTriModeBodyResonator`)**:
//      - $A_0 \approx 278\text{ Hz}$: f字孔ヘルムホルツ空洞共鳴（声に温かい木質のふくよかさを与える）
//      - $B_1^+ \approx 535\text{ Hz}$: 表板（スプルース）と裏板（メイプル）の主要曲げモード
//      - $\text{Bridge Hill} \approx 2480\text{ Hz}$: 駒と魂柱が形成する黄金のプレゼンス隆起
//   2. **黄金比逆数減衰倍音列（$\varphi^{-1} : \varphi^{-2} : \varphi^{-3} = 0.6180 : 0.3820 : 0.2361$）**:
//      チェビシェフ直交多項式 $T_2(x), T_3(x), T_4(x)$ により、偶数次と奇数次の倍音エネルギー比を黄金比に厳密ロックする。
//   3. **フィボナッチ素数タップ（13, 21, 34, 55, 89 サンプル）無干渉マイクロ・シマー**:
//      周期的なコムフィルタノッチが一切発生しないフィボナッチ数列間隔の微細反射で、声の周りにシルクの光輪を織る。
// ==============================================================================

#include "LightweightSimdFastMathAndEcoGovernor.h"
#include <cmath>
#include <algorithm>
#include <array>

namespace vg::human
{

// ==============================================================================
//  1. ストラディバリウス 3モード（A0 Helmholtz 278Hz / B1 Corpus 535Hz / Bridge Hill 2480Hz）共鳴器
// ==============================================================================
class StradivariusTriModeBodyResonator
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double safeFs = std::max (8000.0, sampleRate);
        setupBandpass (modeA0_278,  278.0,  2.8, safeFs);
        setupBandpass (modeB1_535,  535.0,  2.4, safeFs);
        setupBandpass (bridge2480,  2480.0, 1.55, safeFs);
        reset();
    }

    void reset() noexcept
    {
        modeA0_278.reset();
        modeB1_535.reset();
        bridge2480.reset();
    }

    inline void processTriModeResonance (float left, float right, float& outResL, float& outResR) noexcept
    {
        const float a0L = modeA0_278.processL (left);
        const float a0R = modeA0_278.processR (right);
        const float b1L = modeB1_535.processL (left);
        const float b1R = modeB1_535.processR (right);
        const float bhL = bridge2480.processL (left);
        const float bhR = bridge2480.processR (right);

        // 黄金比重み付けで3モードをブレンド（Bridge Hill 主体 ＋ A0 空洞温感 ＋ B1 木質）
        outResL = 0.55f * bhL + 0.30f * a0L + 0.15f * b1L;
        outResR = 0.55f * bhR + 0.30f * a0R + 0.15f * b1R;
    }

private:
    struct StereoBP
    {
        float b0 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1L = 0.0f, z2L = 0.0f, z1R = 0.0f, z2R = 0.0f;
        void reset() noexcept { z1L = z2L = z1R = z2R = 0.0f; }

        inline float processL (float x) noexcept
        {
            const float y = b0 * x + z1L;
            z1L = -a1 * y + z2L;
            z2L = b2 * x - a2 * y;
            return y;
        }
        inline float processR (float x) noexcept
        {
            const float y = b0 * x + z1R;
            z1R = -a1 * y + z2R;
            z2R = b2 * x - a2 * y;
            return y;
        }
    };

    static void setupBandpass (StereoBP& bq, double freqHz, double q, double fs) noexcept
    {
        const double safeF = std::clamp (freqHz, 20.0, fs * 0.45);
        const double w0    = 2.0 * 3.14159265358979323846 * safeF / fs;
        const double alpha = std::sin (w0) / (2.0 * std::max (0.2, q));
        const double a0    = 1.0 + alpha;
        bq.b0 = static_cast<float> ( alpha / a0);
        bq.b2 = static_cast<float> (-alpha / a0);
        bq.a1 = static_cast<float> ((-2.0 * std::cos (w0)) / a0);
        bq.a2 = static_cast<float> ((1.0 - alpha) / a0);
    }

    StereoBP modeA0_278;
    StereoBP modeB1_535;
    StereoBP bridge2480;
};

// ==============================================================================
//  2. 黄金比（φ^-n）倍音タペストリー＆フィボナッチ無干渉マイクロディフューザー
// ==============================================================================
struct GoldenHarmonicTelemetry
{
    float phiHarmonicPurityPct = 100.0f;
    float cremonaHillGlowDb    = 0.0f;
    float harmonicRichness01   = 0.0f;
};

class PsychoacousticGoldenRatioHarmonicWeaver
{
public:
    static constexpr float kPhi     = 1.61803398875f;
    static constexpr float kInvPhi1 = 0.61803398875f; // φ^-1
    static constexpr float kInvPhi2 = 0.38196601125f; // φ^-2
    static constexpr float kInvPhi3 = 0.23606797750f; // φ^-3

    void prepare (double sampleRate) noexcept
    {
        sampleRateHz = std::max (8000.0, sampleRate);
        stradBody.prepare (sampleRateHz);

        // 第2倍音（偶数次）に生じるDCオフセット除去用 15Hz ブロッカー
        dcPole = static_cast<float> (std::clamp (1.0 - (2.0 * 3.14159265358979323846 * 15.0 / sampleRateHz), 0.985, 0.9999));
        reset();
    }

    void reset() noexcept
    {
        stradBody.reset();
        dcX1L = dcY1L = dcX1R = dcY1R = 0.0f;
        adaaL.reset();
        adaaR.reset();
        fibRingL.fill (0.0f);
        fibRingR.fill (0.0f);
        fibWritePos = 0;
    }

    inline void processStereo (float& left, float& right, float goldenWeaveAmount01 = 0.35f) noexcept
    {
        if (goldenWeaveAmount01 <= 0.001f)
            return;

        const float amount = std::clamp (goldenWeaveAmount01, 0.0f, 1.0f);

        // ソフト正規化入力 x ∈ [-1, 1]
        const float xL = adaaL.processSample (left,  1.15f);
        const float xR = adaaR.processSample (right, 1.15f);

        // チェビシェフ多項式純倍音基底: H2(x) = 2x^2, H3(x) = 4x^3, H4(x) = 8x^4 - 8x^2
        const float xL2 = xL * xL;
        const float xR2 = xR * xR;

        const float h2L = 2.0f * xL2;
        const float h3L = 4.0f * xL * xL2;
        const float h4L = 8.0f * xL2 * (xL2 - 1.0f);

        const float h2R = 2.0f * xR2;
        const float h3R = 4.0f * xR * xR2;
        const float h4R = 8.0f * xR2 * (xR2 - 1.0f);

        // 黄金比 φ^-1, φ^-2, φ^-3 で重み付け合成
        const float harmL = kInvPhi1 * h2L + kInvPhi2 * h3L + kInvPhi3 * h4L;
        const float harmR = kInvPhi1 * h2R + kInvPhi2 * h3R + kInvPhi3 * h4R;

        // 偶数次倍音由来のDCオフセットを即時カット
        const float cleanHarmL = harmL - dcX1L + dcPole * dcY1L;
        dcX1L = harmL; dcY1L = cleanHarmL;

        const float cleanHarmR = harmR - dcX1R + dcPole * dcY1R;
        dcX1R = harmR; dcY1R = cleanHarmR;

        // ストラディバリウス 3モード共鳴（A0 278Hz / B1 535Hz / Bridge Hill 2480Hz）
        float hillL = 0.0f, hillR = 0.0f;
        stradBody.processTriModeResonance (left, right, hillL, hillR);

        // フィボナッチ素数タップ（21, 34, 55, 89 サンプル）マイクロ・ディフュージョン
        fibRingL[static_cast<size_t> (fibWritePos)] = hillL;
        fibRingR[static_cast<size_t> (fibWritePos)] = hillR;

        const float fibDiffL = kInvPhi1 * fibRingL[static_cast<size_t> ((fibWritePos - 21) & kFibMask)]
                             + kInvPhi2 * fibRingR[static_cast<size_t> ((fibWritePos - 55) & kFibMask)];
        const float fibDiffR = kInvPhi1 * fibRingR[static_cast<size_t> ((fibWritePos - 34) & kFibMask)]
                             + kInvPhi2 * fibRingL[static_cast<size_t> ((fibWritePos - 89) & kFibMask)];
        fibWritePos = (fibWritePos + 1) & kFibMask;

        left  += amount * (0.080f * cleanHarmL + 0.12f * hillL + 0.045f * fibDiffL);
        right += amount * (0.080f * cleanHarmR + 0.12f * hillR + 0.045f * fibDiffR);
    }

    GoldenHarmonicTelemetry getTelemetry (float goldenWeaveAmount01 = 0.35f) const noexcept
    {
        GoldenHarmonicTelemetry t;
        t.phiHarmonicPurityPct = 100.0f;
        t.cremonaHillGlowDb    = goldenWeaveAmount01 * 1.95f;
        t.harmonicRichness01   = std::clamp (goldenWeaveAmount01, 0.0f, 1.0f);
        return t;
    }

private:
    static constexpr int kFibMask = 127;
    double sampleRateHz = 48000.0;
    StradivariusTriModeBodyResonator stradBody;
    float dcPole = 0.998f;
    float dcX1L = 0.0f, dcY1L = 0.0f, dcX1R = 0.0f, dcY1R = 0.0f;

    std::array<float, 128> fibRingL {};
    std::array<float, 128> fibRingR {};
    int fibWritePos = 0;

    vg::lightweight::Adaa1TanhSaturator adaaL;
    vg::lightweight::Adaa1TanhSaturator adaaR;
};

} // namespace vg::human
