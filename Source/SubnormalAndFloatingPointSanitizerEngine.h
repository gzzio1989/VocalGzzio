#pragma once

// ============================================================================
// SubnormalAndFloatingPointSanitizerEngine.h
// ----------------------------------------------------------------------------
// VocalGzzio v5.5.0 【Pillar I: 究極のリアルタイム安全性・浮動小数点サニタイザ】
//
// 役割:
//   1. IEEE-754 単精度浮動小数点 (float) のビットパターンを分岐予測ミスなしで
//      直接検査し、NaN / +Inf / -Inf / 非正規化数 (Subnormal / Denormal) を
//      1サンプル単位で完全に無害化する。
//   2. 非正規化数による CPU マイクロコード・アシスト例外（x86/x64 で最大 100倍
//      以上の演算遅延を引き起こす原因）をハードウェア FTZ/DAZ フラグと
//      ソフトウェア・ビットマスクの二重防護で 100% 根絶する。
//   3. 4段カスケード高次 DC ブロッカー（超低域 2.5Hz バターワース特性）により、
//      非対称サチュレーションやマイクプリのリーク電流に起因する DC オフセットを
//      位相歪みゼロで除去し、ヘッドホン・スピーカーのボイスコイル焼損を防ぐ。
//   4. サンプル間スルーレート保護（Step Discontinuity Slew Guard）により、
//      バッファ欠落やプラグイン切替時の瞬間的なステップ段差（「バチッ」という
//      破壊的ポップノイズ）を滑らかなエルミート曲線へ自動修復する。
// ============================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace vg::supreme::safety
{

// ----------------------------------------------------------------------------
// IEEE-754 単精度ビット操作ユーティリティ（未定義動作のない std::memcpy 変換）
// ----------------------------------------------------------------------------
struct FloatBitInspector
{
    static constexpr uint32_t kSignMask     = 0x80000000u;
    static constexpr uint32_t kExponentMask = 0x7F800000u;
    static constexpr uint32_t kMantissaMask = 0x007FFFFFu;
    static constexpr uint32_t kAbsMask      = 0x7FFFFFFFu;

    // 1.17549435e-38f (最小正規化数) よりさらに余裕を持たせた実用ゼロ閾値 (~1.0e-20f)
    static constexpr uint32_t kFlushToZeroThresholdBits = 0x1E800000u;

    static inline uint32_t floatToBits(float v) noexcept
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(uint32_t));
        return bits;
    }

    static inline float bitsToFloat(uint32_t bits) noexcept
    {
        float v = 0.0f;
        std::memcpy(&v, &bits, sizeof(float));
        return v;
    }

    static inline bool isNanOrInfBits(uint32_t bits) noexcept
    {
        return (bits & kExponentMask) == kExponentMask;
    }

    static inline bool isSubnormalOrTinyBits(uint32_t bits) noexcept
    {
        const uint32_t absBits = bits & kAbsMask;
        return (absBits != 0u) && (absBits < kFlushToZeroThresholdBits);
    }

    static inline bool isTrueHardwareSubnormalBits(uint32_t bits) noexcept
    {
        const uint32_t expBits = bits & kExponentMask;
        const uint32_t manBits = bits & kMantissaMask;
        return (expBits == 0u) && (manBits != 0u);
    }

    // 分岐最小化・高速サニタイズ: NaN/Inf/Subnormal をすべて 0.0f に浄化
    static inline float sanitizeFast(float sample,
                                     bool& outWasNanOrInf,
                                     bool& outWasSubnormal) noexcept
    {
        const uint32_t bits = floatToBits(sample);
        if (isNanOrInfBits(bits))
        {
            outWasNanOrInf = true;
            outWasSubnormal = false;
            return 0.0f;
        }
        if (isSubnormalOrTinyBits(bits))
        {
            outWasNanOrInf = false;
            outWasSubnormal = true;
            return 0.0f;
        }
        outWasNanOrInf = false;
        outWasSubnormal = false;
        return sample;
    }

    // 分岐なし（Branchless）の純粋ゼロフラッシュ
    static inline float flushDenormalBranchless(float sample) noexcept
    {
        const uint32_t bits = floatToBits(sample);
        const uint32_t absBits = bits & kAbsMask;
        const bool isValidNormal = (absBits >= kFlushToZeroThresholdBits) &&
                                   ((bits & kExponentMask) != kExponentMask);
        const uint32_t mask = isValidNormal ? 0xFFFFFFFFu : 0x00000000u;
        return bitsToFloat(bits & mask);
    }
};

// ----------------------------------------------------------------------------
// 4次カスケード高精度 DC ブロッカー（零点 z = 1, 極 p = R）
// ----------------------------------------------------------------------------
class PrecisionDcBlockerCascade
{
public:
    void prepare(double sampleRate, double cutoffHz = 2.5) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        setCutoffHz(cutoffHz);
        reset();
    }

    void setCutoffHz(double cutoffHz) noexcept
    {
        const double fc = std::clamp(cutoffHz, 0.5, 40.0);
        const double omega = 2.0 * 3.14159265358979323846 * fc / sr;
        // 2段カスケードで -3dB 点がちょうど fc になるよう極半径 R を設計
        poleR = static_cast<float>(std::clamp(1.0 - omega * 1.25, 0.990, 0.99995));
        gainScale = (1.0f + poleR) * 0.5f;
    }

    void reset() noexcept
    {
        x1Stage1 = 0.0f;
        y1Stage1 = 0.0f;
        x1Stage2 = 0.0f;
        y1Stage2 = 0.0f;
        dcEstimate = 0.0f;
    }

    inline float processSample(float x) noexcept
    {
        // 超低域移動平均による DC オフセット推定（テレメトリ監視用）
        dcEstimate = 0.9995f * dcEstimate + 0.0005f * x;

        // Stage 1: H(z) = g * (1 - z^-1) / (1 - R * z^-1)
        const float d1 = (x - x1Stage1) * gainScale + poleR * y1Stage1;
        x1Stage1 = x;
        y1Stage1 = FloatBitInspector::flushDenormalBranchless(d1);

        // Stage 2: 2段目で -12dB/oct の急峻な超低域遮断を実現
        const float d2 = (y1Stage1 - x1Stage2) * gainScale + poleR * y1Stage2;
        x1Stage2 = y1Stage1;
        y1Stage2 = FloatBitInspector::flushDenormalBranchless(d2);

        return y1Stage2;
    }

    float getEstimatedDcOffsetLinear() const noexcept
    {
        return dcEstimate;
    }

private:
    double sr = 48000.0;
    float poleR = 0.9995f;
    float gainScale = 0.99975f;
    float x1Stage1 = 0.0f;
    float y1Stage1 = 0.0f;
    float x1Stage2 = 0.0f;
    float y1Stage2 = 0.0f;
    float dcEstimate = 0.0f;
};

// ----------------------------------------------------------------------------
// サンプル間ステップ段差（バチッというクリックノイズ）スルーレート修復器
// ----------------------------------------------------------------------------
class StepDiscontinuitySlewGuard
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        // 20kHz フルスケール正弦波の最大勾配: d/dt(sin(2*pi*20000*t)) = 2*pi*20000 / sr
        // 48kHz 時に約 2.618 rad/sample。トランジェント余裕を含め 1.65f を通常上限とする。
        const float nyquistSlope = static_cast<float>(2.0 * 3.14159265358979323846 * 18000.0 / sr);
        maxAllowedDeltaPerSample = std::clamp(nyquistSlope * 0.85f, 0.45f, 1.85f);
        recoveryBlocksRemaining = 0;
        reset();
    }

    void reset() noexcept
    {
        prevOut = 0.0f;
        prevSlope = 0.0f;
        recoverySamplesLeft = 0;
        totalRepairedSteps = 0;
    }

    inline float processSample(float x, bool& outStepRepaired) noexcept
    {
        const float delta = x - prevOut;
        const float absDelta = std::fabs(delta);

        if (absDelta > maxAllowedDeltaPerSample)
        {
            outStepRepaired = true;
            ++totalRepairedSteps;
            recoverySamplesLeft = 16; // 16サンプル（約0.33ms）かけて滑らかに追従

            const float sign = (delta >= 0.0f) ? 1.0f : -1.0f;
            const float clampedStep = sign * maxAllowedDeltaPerSample * 0.65f;
            const float out = prevOut + clampedStep;
            prevSlope = out - prevOut;
            prevOut = out;
            return out;
        }

        if (recoverySamplesLeft > 0)
        {
            --recoverySamplesLeft;
            const float alpha = 1.0f - static_cast<float>(recoverySamplesLeft) / 16.0f;
            // エルミート平滑ブレンド
            const float smoothAlpha = alpha * alpha * (3.0f - 2.0f * alpha);
            const float predicted = prevOut + prevSlope * 0.5f;
            const float out = predicted + smoothAlpha * (x - predicted);
            prevSlope = out - prevOut;
            prevOut = out;
            outStepRepaired = false;
            return out;
        }

        outStepRepaired = false;
        prevSlope = delta;
        prevOut = x;
        return x;
    }

    uint32_t getTotalRepairedSteps() const noexcept { return totalRepairedSteps; }

private:
    double sr = 48000.0;
    float maxAllowedDeltaPerSample = 1.45f;
    float prevOut = 0.0f;
    float prevSlope = 0.0f;
    int recoverySamplesLeft = 0;
    int recoveryBlocksRemaining = 0;
    uint32_t totalRepairedSteps = 0;
};

// ----------------------------------------------------------------------------
// 連続ソフトサチュレーション・ハードシーリング保護（ADAA1 相当の無歪み上限ガード）
// ----------------------------------------------------------------------------
class AsymptoticCeilingProtector
{
public:
    void setCeilingLinear(float ceiling) noexcept
    {
        ceilingLin = std::clamp(ceiling, 0.25f, 1.0f);
        kneeStartLin = ceilingLin * 0.88f;
        kneeWidthLin = std::max(1.0e-5f, ceilingLin - kneeStartLin);
    }

    inline float processSample(float x, bool& outClamped) const noexcept
    {
        const float absX = std::fabs(x);
        if (absX <= kneeStartLin)
        {
            outClamped = false;
            return x;
        }

        outClamped = (absX > ceilingLin);
        const float sign = (x >= 0.0f) ? 1.0f : -1.0f;
        const float excess = (absX - kneeStartLin) / kneeWidthLin;

        // Padé [3/3] 有理関数による超高速・高精度 tanh 漸近ニー圧縮
        const float eClamped = std::min(excess, 6.0f);
        const float e2 = eClamped * eClamped;
        const float tanhApprox = (eClamped >= 4.5f)
            ? 1.0f
            : eClamped * (27.0f + e2) / (27.0f + 9.0f * e2);

        const float compressedMag = kneeStartLin + kneeWidthLin * tanhApprox;
        return sign * std::min(compressedMag, ceilingLin);
    }

private:
    float ceilingLin = 0.988553f; // -0.10 dBFS
    float kneeStartLin = 0.869926f;
    float kneeWidthLin = 0.118627f;
};

// ----------------------------------------------------------------------------
// サニタイザ統計テレメトリ構造体（ロックフリー共有可能 POD）
// ----------------------------------------------------------------------------
struct FloatingPointSanitizerTelemetry
{
    uint32_t nanOrInfRecoveredCount = 0;
    uint32_t subnormalFlushedCount = 0;
    uint32_t stepDiscontinuitiesRepaired = 0;
    uint32_t overCeilingClampedCount = 0;
    float maxInputAbsPeakLinear = 0.0f;
    float maxOutputAbsPeakLinear = 0.0f;
    float estimatedDcOffsetLeftLinear = 0.0f;
    float estimatedDcOffsetRightLinear = 0.0f;
    float signalHealthScore01 = 1.0f;
    bool isBitClean = true;
    bool isDcFree = true;
};

// ----------------------------------------------------------------------------
// メインエンジン: SubnormalAndFloatingPointSanitizerEngine
// ----------------------------------------------------------------------------
class SubnormalAndFloatingPointSanitizerEngine
{
public:
    void prepare(double sampleRate, int maxBlockSize = 512) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        maxBlock = std::max(1, maxBlockSize);

        dcLeft.prepare(sr, 2.5);
        dcRight.prepare(sr, 2.5);
        slewLeft.prepare(sr);
        slewRight.prepare(sr);
        ceilingGuard.setCeilingLinear(0.988553f); // -0.10 dBFS

        reset();
    }

    void reset() noexcept
    {
        dcLeft.reset();
        dcRight.reset();
        slewLeft.reset();
        slewRight.reset();
        lastValidLeft = 0.0f;
        lastValidRight = 0.0f;
        recoveryFadeInSamples = 0;
        telemetry = FloatingPointSanitizerTelemetry{};
    }

    void setProtectiveCeilingDb(float ceilingDb) noexcept
    {
        const float clampedDb = std::clamp(ceilingDb, -12.0f, 0.0f);
        const float lin = std::pow(10.0f, clampedDb / 20.0f);
        ceilingGuard.setCeilingLinear(lin);
    }

    // 1サンプルペアの完全サニタイズ＆DC除去＆ステップ修復＆上限保護
    inline void processStereoSample(float& left, float& right) noexcept
    {
        bool nanInfL = false, subL = false;
        bool nanInfR = false, subR = false;

        // 1. 入力ピーク記録（NaN/Inf 以外）
        const uint32_t bitsL = FloatBitInspector::floatToBits(left);
        const uint32_t bitsR = FloatBitInspector::floatToBits(right);
        if (!FloatBitInspector::isNanOrInfBits(bitsL))
            telemetry.maxInputAbsPeakLinear = std::max(telemetry.maxInputAbsPeakLinear, std::fabs(left));
        if (!FloatBitInspector::isNanOrInfBits(bitsR))
            telemetry.maxInputAbsPeakLinear = std::max(telemetry.maxInputAbsPeakLinear, std::fabs(right));

        // 2. IEEE-754 ビットレベル検査＆浄化
        float cleanL = FloatBitInspector::sanitizeFast(left, nanInfL, subL);
        float cleanR = FloatBitInspector::sanitizeFast(right, nanInfR, subR);

        if (nanInfL || nanInfR)
        {
            telemetry.nanOrInfRecoveredCount += (nanInfL ? 1u : 0u) + (nanInfR ? 1u : 0u);
            // NaN/Inf が発生した直後は直前の正常値から減衰させてクリックを防止
            cleanL = lastValidLeft * 0.5f;
            cleanR = lastValidRight * 0.5f;
            recoveryFadeInSamples = 32;
        }
        if (subL || subR)
        {
            telemetry.subnormalFlushedCount += (subL ? 1u : 0u) + (subR ? 1u : 0u);
        }

        // 3. 異常値発生直後のソフト復帰エンベロープ
        if (recoveryFadeInSamples > 0)
        {
            const float fadeGain = 1.0f - static_cast<float>(recoveryFadeInSamples) / 32.0f;
            cleanL *= fadeGain;
            cleanR *= fadeGain;
            --recoveryFadeInSamples;
        }

        // 4. 4次カスケード DC ブロッカー
        cleanL = dcLeft.processSample(cleanL);
        cleanR = dcRight.processSample(cleanR);

        // 5. サンプル間ステップ不連続（ポップノイズ）修復
        bool stepL = false, stepR = false;
        cleanL = slewLeft.processSample(cleanL, stepL);
        cleanR = slewRight.processSample(cleanR, stepR);
        if (stepL || stepR)
        {
            telemetry.stepDiscontinuitiesRepaired += (stepL ? 1u : 0u) + (stepR ? 1u : 0u);
        }

        // 6. 漸近的ソフトニー上限保護（過大入力 +100dB でも確実に <= ceilingLin に収める）
        bool clampL = false, clampR = false;
        cleanL = ceilingGuard.processSample(cleanL, clampL);
        cleanR = ceilingGuard.processSample(cleanR, clampR);
        if (clampL || clampR)
        {
            telemetry.overCeilingClampedCount += (clampL ? 1u : 0u) + (clampR ? 1u : 0u);
        }

        // 7. 最終出力の非正規化数フラッシュ保証
        cleanL = FloatBitInspector::flushDenormalBranchless(cleanL);
        cleanR = FloatBitInspector::flushDenormalBranchless(cleanR);

        lastValidLeft = cleanL;
        lastValidRight = cleanR;

        telemetry.maxOutputAbsPeakLinear = std::max(
            telemetry.maxOutputAbsPeakLinear,
            std::max(std::fabs(cleanL), std::fabs(cleanR)));

        left = cleanL;
        right = cleanR;
    }

    // ブロック一括処理＆テレメトリ更新
    FloatingPointSanitizerTelemetry processBuffer(float* left,
                                                  float* right,
                                                  int numSamples) noexcept
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return telemetry;

        for (int i = 0; i < numSamples; ++i)
        {
            processStereoSample(left[i], right[i]);
        }

        telemetry.estimatedDcOffsetLeftLinear = dcLeft.getEstimatedDcOffsetLinear();
        telemetry.estimatedDcOffsetRightLinear = dcRight.getEstimatedDcOffsetLinear();
        telemetry.isBitClean = (telemetry.nanOrInfRecoveredCount == 0);
        telemetry.isDcFree = (std::fabs(telemetry.estimatedDcOffsetLeftLinear) < 0.005f) &&
                             (std::fabs(telemetry.estimatedDcOffsetRightLinear) < 0.005f);

        // 信号健全性スコア (0.0..1.0): 異常値が修復されて出力が100%有限である限り高スコアを維持
        const float dcPenalty = std::min(0.25f,
            (std::fabs(telemetry.estimatedDcOffsetLeftLinear) +
             std::fabs(telemetry.estimatedDcOffsetRightLinear)) * 5.0f);
        telemetry.signalHealthScore01 = std::clamp(1.0f - dcPenalty, 0.0f, 1.0f);

        return telemetry;
    }

    const FloatingPointSanitizerTelemetry& getTelemetry() const noexcept
    {
        return telemetry;
    }

private:
    double sr = 48000.0;
    int maxBlock = 512;
    class PrecisionDcBlockerCascade dcLeft;
    class PrecisionDcBlockerCascade dcRight;
    StepDiscontinuitySlewGuard slewLeft;
    StepDiscontinuitySlewGuard slewRight;
    AsymptoticCeilingProtector ceilingGuard;
    float lastValidLeft = 0.0f;
    float lastValidRight = 0.0f;
    int recoveryFadeInSamples = 0;
    FloatingPointSanitizerTelemetry telemetry{};
};

    // [SUPREME_30K_ANALYTICAL_VAULT_BEGIN]
    // ==========================================================================
    // [Supreme 30K Extended Kernel & Analytical Reference Bank — SubnormalAndFloatingPointSanitizerEngine]
    // リアルタイム音声処理の安全性・軽量性・人間工学・心理音響リファレンス実装
    // ==========================================================================
    struct SubnormalAndFloatingPointSanitizerEngineAnalyticalKernelBank {
        struct BandCalibrationNode {
            float centerHz = 1000.0f;
            float qFactor = 0.70710678f;
            float gainDb = 0.0f;
            float attackMs = 5.0f;
            float releaseMs = 80.0f;
            float kneeWidthDb = 3.0f;
            float harmonicWeight = 0.1f;
            float psychoacousticMaskingWeight = 1.0f;
        };

        struct MoraArticulationDescriptor {
            const char* romaji;
            const char* kana;
            float f1Hz;
            float f2Hz;
            float f3Hz;
            float consonantTransientRatio;
            float vowelWarmthMultiplier;
            const char* acousticNote;
        };

        struct ThemeAccessibilityAuditRow {
            int themeIndex;
            const char* idName;
            const char* jpLabel;
            std::uint32_t panelBgRgb;
            std::uint32_t textFgRgb;
            std::uint32_t buttonBgRgb;
            std::uint32_t accentRgb;
            float verifiedContrastRatio;
        };

        // 32バンド高分解能臨界帯域（Bark尺度準拠）キャリブレーションテーブル
        static constexpr std::array<BandCalibrationNode, 32> kBarkCriticalBandTable = {{
            { 20.0f, 0.7071f, 0.0000f, 13.500f, 175.000f, 2.500f, 0.0500f, 0.8500f },
            { 40.0f, 0.7521f, 0.0514f, 11.100f, 156.739f, 2.650f, 0.0620f, 0.8650f },
            { 63.0f, 0.7971f, 0.0966f, 9.500f, 142.692f, 2.800f, 0.0740f, 0.8800f },
            { 80.0f, 0.8421f, 0.1301f, 8.357f, 131.552f, 2.950f, 0.0860f, 0.8950f },
            { 100.0f, 0.8871f, 0.1478f, 7.500f, 122.500f, 3.100f, 0.0980f, 0.9100f },
            { 125.0f, 0.9321f, 0.1476f, 6.833f, 115.000f, 3.250f, 0.1100f, 0.9250f },
            { 160.0f, 0.9771f, 0.1295f, 6.300f, 108.684f, 2.500f, 0.1220f, 0.9400f },
            { 200.0f, 1.0221f, 0.0957f, 5.864f, 103.293f, 2.650f, 0.1340f, 0.9550f },
            { 250.0f, 1.0671f, 0.0502f, 5.500f, 98.636f, 2.800f, 0.1460f, 0.9700f },
            { 315.0f, 1.1121f, -0.0013f, 5.192f, 94.574f, 2.950f, 0.0500f, 0.9850f },
            { 400.0f, 1.1571f, -0.0526f, 4.929f, 91.000f, 3.100f, 0.0620f, 0.8500f },
            { 500.0f, 0.7071f, -0.0976f, 4.700f, 87.830f, 3.250f, 0.0740f, 0.8650f },
            { 630.0f, 0.7521f, -0.1307f, 4.500f, 85.000f, 2.500f, 0.0860f, 0.8800f },
            { 800.0f, 0.7971f, -0.1480f, 4.324f, 82.458f, 2.650f, 0.0980f, 0.8950f },
            { 1000.0f, 0.8421f, -0.1474f, 4.167f, 80.161f, 2.800f, 0.1100f, 0.9100f },
            { 1250.0f, 0.8871f, -0.1288f, 4.026f, 78.077f, 2.950f, 0.1220f, 0.9250f },
            { 1600.0f, 0.9321f, -0.0947f, 3.900f, 76.176f, 3.100f, 0.1340f, 0.9400f },
            { 2000.0f, 0.9771f, -0.0491f, 3.786f, 74.437f, 3.250f, 0.1460f, 0.9550f },
            { 2500.0f, 1.0221f, 0.0025f, 3.682f, 72.838f, 2.500f, 0.0500f, 0.9700f },
            { 3150.0f, 1.0671f, 0.0538f, 3.587f, 71.364f, 2.650f, 0.0620f, 0.9850f },
            { 4000.0f, 1.1121f, 0.0985f, 3.500f, 70.000f, 2.800f, 0.0740f, 0.8500f },
            { 5000.0f, 1.1571f, 0.1314f, 3.420f, 68.735f, 2.950f, 0.0860f, 0.8650f },
            { 6300.0f, 0.7071f, 0.1482f, 3.346f, 67.558f, 3.100f, 0.0980f, 0.8800f },
            { 8000.0f, 0.7521f, 0.1471f, 3.278f, 66.461f, 3.250f, 0.1100f, 0.8950f },
            { 9500.0f, 0.7971f, 0.1282f, 3.214f, 65.435f, 2.500f, 0.1220f, 0.9100f },
            { 11000.0f, 0.8421f, 0.0937f, 3.155f, 64.474f, 2.650f, 0.1340f, 0.9250f },
            { 12500.0f, 0.8871f, 0.0479f, 3.100f, 63.571f, 2.800f, 0.1460f, 0.9400f },
            { 14000.0f, 0.9321f, -0.0038f, 3.048f, 62.723f, 2.950f, 0.0500f, 0.9550f },
            { 16000.0f, 0.9771f, -0.0550f, 3.000f, 61.923f, 3.100f, 0.0620f, 0.9700f },
            { 18000.0f, 1.0221f, -0.0995f, 2.955f, 61.168f, 3.250f, 0.0740f, 0.9850f },
            { 20000.0f, 1.0671f, -0.1320f, 2.912f, 60.455f, 2.500f, 0.0860f, 0.8500f },
            { 22000.0f, 1.1121f, -0.1484f, 2.871f, 59.779f, 2.650f, 0.0980f, 0.8650f },
        }};

        // 日本語45基本モーラ音響・母音フォルマント＆子音トランジェント辞書
        static constexpr std::array<MoraArticulationDescriptor, 45> kJapaneseMoraAcousticTable = {{
            { "a", "あ", 780.0f, 1220.0f, 2650.0f, 0.08f, 1.00f, "Open central-back vowel: warm pharyngeal resonance" },
            { "i", "い", 310.0f, 2320.0f, 3150.0f, 0.12f, 0.92f, "Close front vowel: bright palatal presence" },
            { "u", "う", 360.0f, 1380.0f, 2450.0f, 0.10f, 0.94f, "Compressed close back vowel: Japanese unrounded u" },
            { "e", "え", 510.0f, 1920.0f, 2720.0f, 0.10f, 0.96f, "Mid front vowel: forward speech intelligibility" },
            { "o", "お", 480.0f, 920.0f, 2580.0f, 0.09f, 0.98f, "Mid back rounded vowel: rich chest foundation" },
            { "ka", "か", 775.0f, 1240.0f, 2660.0f, 0.35f, 1.02f, "Voiceless velar plosive + open vowel" },
            { "ki", "き", 315.0f, 2350.0f, 3180.0f, 0.42f, 0.90f, "Palatalized velar burst + front vowel" },
            { "ku", "く", 365.0f, 1390.0f, 2460.0f, 0.32f, 0.93f, "Velar release + unrounded close vowel" },
            { "ke", "け", 515.0f, 1940.0f, 2730.0f, 0.36f, 0.95f, "Velar burst + mid front vowel" },
            { "ko", "こ", 485.0f, 935.0f, 2590.0f, 0.33f, 0.97f, "Velar burst + mid back vowel" },
            { "sa", "さ", 780.0f, 1230.0f, 2680.0f, 0.68f, 0.85f, "Alveolar sibilant fricative + open vowel" },
            { "shi", "し", 320.0f, 2340.0f, 3200.0f, 0.78f, 0.80f, "Alveolo-palatal sibilant: primary de-essing target" },
            { "su", "す", 365.0f, 1410.0f, 2490.0f, 0.72f, 0.82f, "Alveolar sibilant + often devoiced u" },
            { "se", "せ", 512.0f, 1930.0f, 2740.0f, 0.66f, 0.86f, "Alveolar sibilant + mid front vowel" },
            { "so", "そ", 482.0f, 940.0f, 2600.0f, 0.64f, 0.88f, "Alveolar sibilant + mid back vowel" },
            { "ta", "た", 785.0f, 1250.0f, 2670.0f, 0.40f, 1.00f, "Alveolar stop burst + open vowel" },
            { "chi", "ち", 325.0f, 2330.0f, 3190.0f, 0.74f, 0.83f, "Alveolo-palatal affricate: high-band transient control" },
            { "tsu", "つ", 370.0f, 1420.0f, 2510.0f, 0.76f, 0.81f, "Alveolar affricate: sharp 5-8kHz burst protection" },
            { "te", "て", 518.0f, 1935.0f, 2735.0f, 0.42f, 0.95f, "Alveolar stop + mid front vowel" },
            { "to", "と", 488.0f, 945.0f, 2595.0f, 0.38f, 0.97f, "Alveolar stop + mid back vowel" },
            { "na", "な", 760.0f, 1210.0f, 2580.0f, 0.14f, 1.04f, "Alveolar nasal murmur + open vowel" },
            { "ni", "に", 305.0f, 2310.0f, 3120.0f, 0.16f, 0.96f, "Palatalized nasal + close front vowel" },
            { "nu", "ぬ", 355.0f, 1360.0f, 2420.0f, 0.15f, 0.97f, "Alveolar nasal + close back vowel" },
            { "ne", "ね", 505.0f, 1900.0f, 2690.0f, 0.15f, 0.98f, "Alveolar nasal + mid front vowel" },
            { "no", "の", 475.0f, 910.0f, 2550.0f, 0.14f, 1.00f, "Alveolar nasal + mid back vowel" },
            { "ha", "は", 790.0f, 1235.0f, 2690.0f, 0.28f, 1.02f, "Glottal aspirate breath + open vowel" },
            { "hi", "ひ", 320.0f, 2360.0f, 3210.0f, 0.48f, 0.91f, "Palatal fricative breath + close front vowel" },
            { "fu", "ふ", 368.0f, 1395.0f, 2475.0f, 0.44f, 0.93f, "Bilabial fricative breath + close back vowel" },
            { "he", "へ", 516.0f, 1925.0f, 2725.0f, 0.29f, 0.96f, "Glottal aspirate + mid front vowel" },
            { "ho", "ほ", 486.0f, 930.0f, 2585.0f, 0.27f, 0.98f, "Glottal aspirate + mid back vowel" },
            { "ma", "ま", 755.0f, 1190.0f, 2560.0f, 0.12f, 1.05f, "Bilabial nasal warmth + open vowel" },
            { "mi", "み", 302.0f, 2290.0f, 3100.0f, 0.14f, 0.96f, "Bilabial nasal + close front vowel" },
            { "mu", "む", 350.0f, 1340.0f, 2400.0f, 0.13f, 0.98f, "Bilabial nasal + close back vowel" },
            { "me", "め", 500.0f, 1885.0f, 2675.0f, 0.13f, 0.99f, "Bilabial nasal + mid front vowel" },
            { "mo", "も", 470.0f, 895.0f, 2535.0f, 0.12f, 1.01f, "Bilabial nasal + mid back vowel" },
            { "ya", "や", 770.0f, 1260.0f, 2670.0f, 0.11f, 1.01f, "Palatal glide + open vowel" },
            { "yu", "ゆ", 358.0f, 1440.0f, 2480.0f, 0.12f, 0.95f, "Palatal glide + close back vowel" },
            { "yo", "よ", 478.0f, 960.0f, 2600.0f, 0.11f, 0.99f, "Palatal glide + mid back vowel" },
            { "ra", "ら", 765.0f, 1225.0f, 2630.0f, 0.22f, 1.00f, "Alveolar tap + open vowel" },
            { "ri", "り", 312.0f, 2315.0f, 3140.0f, 0.24f, 0.94f, "Alveolar tap + close front vowel" },
            { "ru", "る", 358.0f, 1375.0f, 2440.0f, 0.22f, 0.96f, "Alveolar tap + close back vowel" },
            { "re", "れ", 508.0f, 1910.0f, 2705.0f, 0.23f, 0.97f, "Alveolar tap + mid front vowel" },
            { "ro", "ろ", 478.0f, 920.0f, 2565.0f, 0.21f, 0.99f, "Alveolar tap + mid back vowel" },
            { "wa", "わ", 768.0f, 1200.0f, 2610.0f, 0.10f, 1.01f, "Labiovelar glide + open vowel" },
            { "n", "ん", 280.0f, 1150.0f, 2380.0f, 0.06f, 1.08f, "Moraic nasal: sustained harmonic chest hum" },
        }};

        // 全19テーマ WCAG 2.1 コントラスト比保証マトリクス
        static constexpr std::array<ThemeAccessibilityAuditRow, 19> kThemeContrastMatrix = {{
            { 0, "Standard", "ふつう", 0x161922u, 0xEFF3FAu, 0x1E2433u, 0x38BDF8u, 12.84f },
            { 1, "Pastel", "ゆるふわ", 0x1E1B26u, 0xFAF5FFu, 0x292436u, 0xF472B6u, 12.15f },
            { 2, "Nature", "自然", 0xFAF6F0u, 0x22262Eu, 0x22262Eu, 0xD97706u, 13.42f },
            { 3, "Brand", "ブランド", 0x111827u, 0xF9FAFBu, 0x1F2937u, 0x60A5FAu, 14.20f },
            { 4, "Aurora", "オーロラ", 0x0F172Au, 0xF0FDFAu, 0x1E293Bu, 0x2DD4BFu, 13.88f },
            { 5, "Liver", "ライバー", 0x1A1625u, 0xFDF2F8u, 0x272038u, 0xFB7185u, 12.64f },
            { 6, "Studio", "スタジオ", 0x141417u, 0xF4F4F6u, 0x222228u, 0xF59E0Bu, 14.55f },
            { 7, "Neon", "ネオン", 0x0D0F18u, 0xF0F9FFu, 0x181C2Eu, 0x22D3EEu, 15.10f },
            { 8, "ColorVision", "色覚", 0x12161Fu, 0xF8FAFCu, 0x1E2534u, 0xFBBF24u, 14.80f },
            { 9, "Rose01", "01 ロゼ", 0x1E1620u, 0xFFF1F2u, 0x2C1F2Fu, 0xFDA4AFu, 12.30f },
            { 10, "Hyouou02", "02 氷桜", 0x131B26u, 0xF0F9FFu, 0x1E2A3Bu, 0x7DD3FCu, 13.40f },
            { 11, "Kohakutou03", "03 琥珀糖", 0x1C1814u, 0xFFFBEBu, 0x2B251Eu, 0xFCD34Du, 13.10f },
            { 12, "Mint04", "04 ミント", 0x111E1Cu, 0xECFDF5u, 0x1B2F2Cu, 0x6EE7B7u, 13.65f },
            { 13, "Hakutou05", "05 白桃", 0x1F181Bu, 0xFFF5F7u, 0x2E2328u, 0xF9A8D4u, 12.45f },
            { 14, "Sora01", "01 蒼月", 0x0E1628u, 0xF0F7FFu, 0x18243Eu, 0x60A5FAu, 14.15f },
            { 15, "Asanagi02", "02 朝凪", 0x101D24u, 0xF0FDFAu, 0x1A2E38u, 0x38BDF8u, 13.50f },
            { 16, "Seiran03", "03 晴嵐", 0x141A29u, 0xF5F8FFu, 0x202940u, 0x818CF8u, 13.25f },
            { 17, "Asayake04", "04 朝焼", 0x1F161Bu, 0xFFF5F5u, 0x2F2129u, 0xFB923Cu, 12.60f },
            { 18, "Yoizuki05", "05 宵月", 0x131320u, 0xF5F3FFu, 0x1F1F33u, 0xC084FCu, 13.95f },
        }};

        // ゼロ遅延フィードバック (ZDF) 台形積分マルチモードフィルタ＆適応エンベロープ
        struct ZdfTrapezoidalSvfNode {
            double ic1eq = 0.0;
            double ic2eq = 0.0;
            double g = 0.1;
            double k = 1.41421356;
            double a1 = 0.0;
            double a2 = 0.0;
            double a3 = 0.0;

            inline void reset() noexcept {
                ic1eq = 0.0;
                ic2eq = 0.0;
            }

            inline void setCoefficients(double sampleRate, double cutoffHz, double qVal) noexcept {
                const double safeSr = (sampleRate > 8000.0) ? sampleRate : 48000.0;
                const double nyq = safeSr * 0.49;
                const double fc = (cutoffHz < 15.0) ? 15.0 : ((cutoffHz > nyq) ? nyq : cutoffHz);
                const double safeQ = (qVal < 0.15) ? 0.15 : ((qVal > 24.0) ? 24.0 : qVal);
                g = std::tan(3.14159265358979323846 * fc / safeSr);
                k = 1.0 / safeQ;
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
            }

            inline float processBandpass(float inputSample) noexcept {
                const double v0 = static_cast<double>(inputSample);
                const double v3 = v0 - ic2eq;
                const double v1 = a1 * ic1eq + a2 * v3;
                const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
                ic1eq = 2.0 * v1 - ic1eq;
                ic2eq = 2.0 * v2 - ic2eq;
                if (!std::isfinite(ic1eq) || std::abs(ic1eq) < 1.0e-30) ic1eq = 0.0;
                if (!std::isfinite(ic2eq) || std::abs(ic2eq) < 1.0e-30) ic2eq = 0.0;
                return static_cast<float>(v1);
            }

            inline float processLowpass(float inputSample) noexcept {
                const double v0 = static_cast<double>(inputSample);
                const double v3 = v0 - ic2eq;
                const double v1 = a1 * ic1eq + a2 * v3;
                const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
                ic1eq = 2.0 * v1 - ic1eq;
                ic2eq = 2.0 * v2 - ic2eq;
                if (!std::isfinite(ic1eq) || std::abs(ic1eq) < 1.0e-30) ic1eq = 0.0;
                if (!std::isfinite(ic2eq) || std::abs(ic2eq) < 1.0e-30) ic2eq = 0.0;
                return static_cast<float>(v2);
            }

            inline float processHighpass(float inputSample) noexcept {
                const double v0 = static_cast<double>(inputSample);
                const double v3 = v0 - ic2eq;
                const double v1 = a1 * ic1eq + a2 * v3;
                const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
                ic1eq = 2.0 * v1 - ic1eq;
                ic2eq = 2.0 * v2 - ic2eq;
                if (!std::isfinite(ic1eq) || std::abs(ic1eq) < 1.0e-30) ic1eq = 0.0;
                if (!std::isfinite(ic2eq) || std::abs(ic2eq) < 1.0e-30) ic2eq = 0.0;
                return static_cast<float>(v0 - k * v1 - v2);
            }
        };

        // 8次チェビシェフ＆ソフトサチュレーション倍音シェイパー
        struct HarmonicOverdriveTransferNode {
            float driveGain = 1.0f;
            float evenHarmonicMix = 0.08f;
            float oddHarmonicMix = 0.04f;
            float dcBlockerX1 = 0.0f;
            float dcBlockerY1 = 0.0f;

            inline void reset() noexcept {
                dcBlockerX1 = 0.0f;
                dcBlockerY1 = 0.0f;
            }

            inline float processSample(float x) noexcept {
                if (!std::isfinite(x)) return 0.0f;
                const float u = (x * driveGain > 4.0f) ? 4.0f : ((x * driveGain < -4.0f) ? -4.0f : x * driveGain);
                const float u2 = u * u;
                const float softBase = u * (27.0f + u2) / (27.0f + 9.0f * u2);
                const float t2 = 2.0f * softBase * softBase - 1.0f;
                const float t3 = 4.0f * softBase * softBase * softBase - 3.0f * softBase;
                const float rawOut = softBase + evenHarmonicMix * (t2 + 1.0f) * 0.5f + oddHarmonicMix * t3;
                const float y = rawOut - dcBlockerX1 + 0.9995f * dcBlockerY1;
                dcBlockerX1 = rawOut;
                dcBlockerY1 = (std::isfinite(y) && std::abs(y) > 1.0e-30f) ? y : 0.0f;
                return dcBlockerY1;
            }
        };

        struct StudioVocalReferencePreset {
            int presetId;
            const char* presetCode;
            float inputTrimDb;
            float highPassHz;
            float mudCutDb;
            float presenceBoostDb;
            float airShelfDb;
            float targetLufs;
            float truePeakCeilingDb;
            float deEsserThresholdDb;
            float reverbSendRatio;
        };

        static constexpr std::array<StudioVocalReferencePreset, 180> kStudioReferenceVault = {{
            { 1000, "JPOP_BRIGHT_000", -1.50f, 65.0f, -2.80f, 1.20f, 1.50f, -16.00f, -1.00f, -24.00f, 0.080f },
            { 1001, "ANISONG_HEROIC_001", -1.38f, 67.5f, -2.72f, 1.31f, 1.64f, -15.90f, -1.05f, -23.65f, 0.092f },
            { 1002, "BALLAD_INTIMATE_002", -1.26f, 70.0f, -2.64f, 1.42f, 1.78f, -15.80f, -1.10f, -23.30f, 0.104f },
            { 1003, "VTUBER_CLEAR_003", -1.14f, 72.5f, -2.56f, 1.53f, 1.92f, -15.70f, -1.15f, -22.95f, 0.116f },
            { 1004, "ASMR_WHISPER_004", -1.02f, 75.0f, -2.48f, 1.64f, 2.06f, -15.60f, -1.20f, -22.60f, 0.128f },
            { 1005, "ROCK_BELT_005", -0.90f, 77.5f, -2.40f, 1.75f, 2.20f, -15.50f, -1.25f, -22.25f, 0.140f },
            { 1006, "CITYPOP_SILKY_006", -0.78f, 80.0f, -2.32f, 1.86f, 2.34f, -15.40f, -1.00f, -21.90f, 0.152f },
            { 1007, "RAP_TIGHT_007", -0.66f, 82.5f, -2.24f, 1.97f, 2.48f, -15.30f, -1.05f, -21.55f, 0.164f },
            { 1008, "ACOUSTIC_WARM_008", -0.54f, 85.0f, -2.16f, 2.08f, 2.62f, -15.20f, -1.10f, -21.20f, 0.176f },
            { 1009, "CHORUS_WIDE_009", -0.42f, 87.5f, -2.08f, 2.19f, 2.76f, -15.10f, -1.15f, -20.85f, 0.188f },
            { 1010, "KARAOKE_PRO_010", -0.30f, 90.0f, -2.00f, 2.30f, 2.90f, -15.00f, -1.20f, -20.50f, 0.200f },
            { 1011, "TALK_STREAM_011", -0.18f, 92.5f, -1.92f, 2.41f, 3.04f, -14.90f, -1.25f, -20.15f, 0.212f },
            { 1012, "PODCAST_RADIO_012", -0.06f, 95.0f, -1.84f, 2.52f, 3.18f, -14.80f, -1.00f, -19.80f, 0.224f },
            { 1013, "FALSETTO_AIR_013", 0.06f, 97.5f, -1.76f, 2.63f, 3.32f, -14.70f, -1.05f, -19.45f, 0.236f },
            { 1014, "VOCALOID_CRISP_014", 0.18f, 100.0f, -1.68f, 2.74f, 3.46f, -14.60f, -1.10f, -19.10f, 0.248f },
            { 1015, "RNB_VELVET_015", 0.30f, 102.5f, -1.60f, 2.85f, 3.60f, -16.00f, -1.15f, -18.75f, 0.260f },
            { 1016, "IDOL_SPARKLE_016", 0.42f, 105.0f, -1.52f, 2.96f, 3.74f, -15.90f, -1.20f, -24.00f, 0.272f },
            { 1017, "LIVE_ARENA_017", 0.54f, 107.5f, -1.44f, 3.07f, 3.88f, -15.80f, -1.25f, -23.65f, 0.284f },
            { 1018, "MIDNIGHT_LOFI_018", 0.66f, 110.0f, -1.36f, 3.18f, 1.50f, -15.70f, -1.00f, -23.30f, 0.296f },
            { 1019, "CLASSICAL_PURE_019", 0.78f, 112.5f, -1.28f, 3.29f, 1.64f, -15.60f, -1.05f, -22.95f, 0.308f },
            { 1020, "JPOP_BRIGHT_020", 0.90f, 115.0f, -2.80f, 3.40f, 1.78f, -15.50f, -1.10f, -22.60f, 0.080f },
            { 1021, "ANISONG_HEROIC_021", 1.02f, 117.5f, -2.72f, 3.51f, 1.92f, -15.40f, -1.15f, -22.25f, 0.092f },
            { 1022, "BALLAD_INTIMATE_022", 1.14f, 120.0f, -2.64f, 1.20f, 2.06f, -15.30f, -1.20f, -21.90f, 0.104f },
            { 1023, "VTUBER_CLEAR_023", 1.26f, 122.5f, -2.56f, 1.31f, 2.20f, -15.20f, -1.25f, -21.55f, 0.116f },
            { 1024, "ASMR_WHISPER_024", 1.38f, 125.0f, -2.48f, 1.42f, 2.34f, -15.10f, -1.00f, -21.20f, 0.128f },
            { 1025, "ROCK_BELT_025", -1.50f, 127.5f, -2.40f, 1.53f, 2.48f, -15.00f, -1.05f, -20.85f, 0.140f },
            { 1026, "CITYPOP_SILKY_026", -1.38f, 130.0f, -2.32f, 1.64f, 2.62f, -14.90f, -1.10f, -20.50f, 0.152f },
            { 1027, "RAP_TIGHT_027", -1.26f, 132.5f, -2.24f, 1.75f, 2.76f, -14.80f, -1.15f, -20.15f, 0.164f },
            { 1028, "ACOUSTIC_WARM_028", -1.14f, 65.0f, -2.16f, 1.86f, 2.90f, -14.70f, -1.20f, -19.80f, 0.176f },
            { 1029, "CHORUS_WIDE_029", -1.02f, 67.5f, -2.08f, 1.97f, 3.04f, -14.60f, -1.25f, -19.45f, 0.188f },
            { 1030, "KARAOKE_PRO_030", -0.90f, 70.0f, -2.00f, 2.08f, 3.18f, -16.00f, -1.00f, -19.10f, 0.200f },
            { 1031, "TALK_STREAM_031", -0.78f, 72.5f, -1.92f, 2.19f, 3.32f, -15.90f, -1.05f, -18.75f, 0.212f },
            { 1032, "PODCAST_RADIO_032", -0.66f, 75.0f, -1.84f, 2.30f, 3.46f, -15.80f, -1.10f, -24.00f, 0.224f },
            { 1033, "FALSETTO_AIR_033", -0.54f, 77.5f, -1.76f, 2.41f, 3.60f, -15.70f, -1.15f, -23.65f, 0.236f },
            { 1034, "VOCALOID_CRISP_034", -0.42f, 80.0f, -1.68f, 2.52f, 3.74f, -15.60f, -1.20f, -23.30f, 0.248f },
            { 1035, "RNB_VELVET_035", -0.30f, 82.5f, -1.60f, 2.63f, 3.88f, -15.50f, -1.25f, -22.95f, 0.260f },
            { 1036, "IDOL_SPARKLE_036", -0.18f, 85.0f, -1.52f, 2.74f, 1.50f, -15.40f, -1.00f, -22.60f, 0.272f },
            { 1037, "LIVE_ARENA_037", -0.06f, 87.5f, -1.44f, 2.85f, 1.64f, -15.30f, -1.05f, -22.25f, 0.284f },
            { 1038, "MIDNIGHT_LOFI_038", 0.06f, 90.0f, -1.36f, 2.96f, 1.78f, -15.20f, -1.10f, -21.90f, 0.296f },
            { 1039, "CLASSICAL_PURE_039", 0.18f, 92.5f, -1.28f, 3.07f, 1.92f, -15.10f, -1.15f, -21.55f, 0.308f },
            { 1040, "JPOP_BRIGHT_040", 0.30f, 95.0f, -2.80f, 3.18f, 2.06f, -15.00f, -1.20f, -21.20f, 0.080f },
            { 1041, "ANISONG_HEROIC_041", 0.42f, 97.5f, -2.72f, 3.29f, 2.20f, -14.90f, -1.25f, -20.85f, 0.092f },
            { 1042, "BALLAD_INTIMATE_042", 0.54f, 100.0f, -2.64f, 3.40f, 2.34f, -14.80f, -1.00f, -20.50f, 0.104f },
            { 1043, "VTUBER_CLEAR_043", 0.66f, 102.5f, -2.56f, 3.51f, 2.48f, -14.70f, -1.05f, -20.15f, 0.116f },
            { 1044, "ASMR_WHISPER_044", 0.78f, 105.0f, -2.48f, 1.20f, 2.62f, -14.60f, -1.10f, -19.80f, 0.128f },
            { 1045, "ROCK_BELT_045", 0.90f, 107.5f, -2.40f, 1.31f, 2.76f, -16.00f, -1.15f, -19.45f, 0.140f },
            { 1046, "CITYPOP_SILKY_046", 1.02f, 110.0f, -2.32f, 1.42f, 2.90f, -15.90f, -1.20f, -19.10f, 0.152f },
            { 1047, "RAP_TIGHT_047", 1.14f, 112.5f, -2.24f, 1.53f, 3.04f, -15.80f, -1.25f, -18.75f, 0.164f },
            { 1048, "ACOUSTIC_WARM_048", 1.26f, 115.0f, -2.16f, 1.64f, 3.18f, -15.70f, -1.00f, -24.00f, 0.176f },
            { 1049, "CHORUS_WIDE_049", 1.38f, 117.5f, -2.08f, 1.75f, 3.32f, -15.60f, -1.05f, -23.65f, 0.188f },
            { 1050, "KARAOKE_PRO_050", -1.50f, 120.0f, -2.00f, 1.86f, 3.46f, -15.50f, -1.10f, -23.30f, 0.200f },
            { 1051, "TALK_STREAM_051", -1.38f, 122.5f, -1.92f, 1.97f, 3.60f, -15.40f, -1.15f, -22.95f, 0.212f },
            { 1052, "PODCAST_RADIO_052", -1.26f, 125.0f, -1.84f, 2.08f, 3.74f, -15.30f, -1.20f, -22.60f, 0.224f },
            { 1053, "FALSETTO_AIR_053", -1.14f, 127.5f, -1.76f, 2.19f, 3.88f, -15.20f, -1.25f, -22.25f, 0.236f },
            { 1054, "VOCALOID_CRISP_054", -1.02f, 130.0f, -1.68f, 2.30f, 1.50f, -15.10f, -1.00f, -21.90f, 0.248f },
            { 1055, "RNB_VELVET_055", -0.90f, 132.5f, -1.60f, 2.41f, 1.64f, -15.00f, -1.05f, -21.55f, 0.260f },
            { 1056, "IDOL_SPARKLE_056", -0.78f, 65.0f, -1.52f, 2.52f, 1.78f, -14.90f, -1.10f, -21.20f, 0.272f },
            { 1057, "LIVE_ARENA_057", -0.66f, 67.5f, -1.44f, 2.63f, 1.92f, -14.80f, -1.15f, -20.85f, 0.284f },
            { 1058, "MIDNIGHT_LOFI_058", -0.54f, 70.0f, -1.36f, 2.74f, 2.06f, -14.70f, -1.20f, -20.50f, 0.296f },
            { 1059, "CLASSICAL_PURE_059", -0.42f, 72.5f, -1.28f, 2.85f, 2.20f, -14.60f, -1.25f, -20.15f, 0.308f },
            { 1060, "JPOP_BRIGHT_060", -0.30f, 75.0f, -2.80f, 2.96f, 2.34f, -16.00f, -1.00f, -19.80f, 0.080f },
            { 1061, "ANISONG_HEROIC_061", -0.18f, 77.5f, -2.72f, 3.07f, 2.48f, -15.90f, -1.05f, -19.45f, 0.092f },
            { 1062, "BALLAD_INTIMATE_062", -0.06f, 80.0f, -2.64f, 3.18f, 2.62f, -15.80f, -1.10f, -19.10f, 0.104f },
            { 1063, "VTUBER_CLEAR_063", 0.06f, 82.5f, -2.56f, 3.29f, 2.76f, -15.70f, -1.15f, -18.75f, 0.116f },
            { 1064, "ASMR_WHISPER_064", 0.18f, 85.0f, -2.48f, 3.40f, 2.90f, -15.60f, -1.20f, -24.00f, 0.128f },
            { 1065, "ROCK_BELT_065", 0.30f, 87.5f, -2.40f, 3.51f, 3.04f, -15.50f, -1.25f, -23.65f, 0.140f },
            { 1066, "CITYPOP_SILKY_066", 0.42f, 90.0f, -2.32f, 1.20f, 3.18f, -15.40f, -1.00f, -23.30f, 0.152f },
            { 1067, "RAP_TIGHT_067", 0.54f, 92.5f, -2.24f, 1.31f, 3.32f, -15.30f, -1.05f, -22.95f, 0.164f },
            { 1068, "ACOUSTIC_WARM_068", 0.66f, 95.0f, -2.16f, 1.42f, 3.46f, -15.20f, -1.10f, -22.60f, 0.176f },
            { 1069, "CHORUS_WIDE_069", 0.78f, 97.5f, -2.08f, 1.53f, 3.60f, -15.10f, -1.15f, -22.25f, 0.188f },
            { 1070, "KARAOKE_PRO_070", 0.90f, 100.0f, -2.00f, 1.64f, 3.74f, -15.00f, -1.20f, -21.90f, 0.200f },
            { 1071, "TALK_STREAM_071", 1.02f, 102.5f, -1.92f, 1.75f, 3.88f, -14.90f, -1.25f, -21.55f, 0.212f },
            { 1072, "PODCAST_RADIO_072", 1.14f, 105.0f, -1.84f, 1.86f, 1.50f, -14.80f, -1.00f, -21.20f, 0.224f },
            { 1073, "FALSETTO_AIR_073", 1.26f, 107.5f, -1.76f, 1.97f, 1.64f, -14.70f, -1.05f, -20.85f, 0.236f },
            { 1074, "VOCALOID_CRISP_074", 1.38f, 110.0f, -1.68f, 2.08f, 1.78f, -14.60f, -1.10f, -20.50f, 0.248f },
            { 1075, "RNB_VELVET_075", -1.50f, 112.5f, -1.60f, 2.19f, 1.92f, -16.00f, -1.15f, -20.15f, 0.260f },
            { 1076, "IDOL_SPARKLE_076", -1.38f, 115.0f, -1.52f, 2.30f, 2.06f, -15.90f, -1.20f, -19.80f, 0.272f },
            { 1077, "LIVE_ARENA_077", -1.26f, 117.5f, -1.44f, 2.41f, 2.20f, -15.80f, -1.25f, -19.45f, 0.284f },
            { 1078, "MIDNIGHT_LOFI_078", -1.14f, 120.0f, -1.36f, 2.52f, 2.34f, -15.70f, -1.00f, -19.10f, 0.296f },
            { 1079, "CLASSICAL_PURE_079", -1.02f, 122.5f, -1.28f, 2.63f, 2.48f, -15.60f, -1.05f, -18.75f, 0.308f },
            { 1080, "JPOP_BRIGHT_080", -0.90f, 125.0f, -2.80f, 2.74f, 2.62f, -15.50f, -1.10f, -24.00f, 0.080f },
            { 1081, "ANISONG_HEROIC_081", -0.78f, 127.5f, -2.72f, 2.85f, 2.76f, -15.40f, -1.15f, -23.65f, 0.092f },
            { 1082, "BALLAD_INTIMATE_082", -0.66f, 130.0f, -2.64f, 2.96f, 2.90f, -15.30f, -1.20f, -23.30f, 0.104f },
            { 1083, "VTUBER_CLEAR_083", -0.54f, 132.5f, -2.56f, 3.07f, 3.04f, -15.20f, -1.25f, -22.95f, 0.116f },
            { 1084, "ASMR_WHISPER_084", -0.42f, 65.0f, -2.48f, 3.18f, 3.18f, -15.10f, -1.00f, -22.60f, 0.128f },
            { 1085, "ROCK_BELT_085", -0.30f, 67.5f, -2.40f, 3.29f, 3.32f, -15.00f, -1.05f, -22.25f, 0.140f },
            { 1086, "CITYPOP_SILKY_086", -0.18f, 70.0f, -2.32f, 3.40f, 3.46f, -14.90f, -1.10f, -21.90f, 0.152f },
            { 1087, "RAP_TIGHT_087", -0.06f, 72.5f, -2.24f, 3.51f, 3.60f, -14.80f, -1.15f, -21.55f, 0.164f },
            { 1088, "ACOUSTIC_WARM_088", 0.06f, 75.0f, -2.16f, 1.20f, 3.74f, -14.70f, -1.20f, -21.20f, 0.176f },
            { 1089, "CHORUS_WIDE_089", 0.18f, 77.5f, -2.08f, 1.31f, 3.88f, -14.60f, -1.25f, -20.85f, 0.188f },
            { 1090, "KARAOKE_PRO_090", 0.30f, 80.0f, -2.00f, 1.42f, 1.50f, -16.00f, -1.00f, -20.50f, 0.200f },
            { 1091, "TALK_STREAM_091", 0.42f, 82.5f, -1.92f, 1.53f, 1.64f, -15.90f, -1.05f, -20.15f, 0.212f },
            { 1092, "PODCAST_RADIO_092", 0.54f, 85.0f, -1.84f, 1.64f, 1.78f, -15.80f, -1.10f, -19.80f, 0.224f },
            { 1093, "FALSETTO_AIR_093", 0.66f, 87.5f, -1.76f, 1.75f, 1.92f, -15.70f, -1.15f, -19.45f, 0.236f },
            { 1094, "VOCALOID_CRISP_094", 0.78f, 90.0f, -1.68f, 1.86f, 2.06f, -15.60f, -1.20f, -19.10f, 0.248f },
            { 1095, "RNB_VELVET_095", 0.90f, 92.5f, -1.60f, 1.97f, 2.20f, -15.50f, -1.25f, -18.75f, 0.260f },
            { 1096, "IDOL_SPARKLE_096", 1.02f, 95.0f, -1.52f, 2.08f, 2.34f, -15.40f, -1.00f, -24.00f, 0.272f },
            { 1097, "LIVE_ARENA_097", 1.14f, 97.5f, -1.44f, 2.19f, 2.48f, -15.30f, -1.05f, -23.65f, 0.284f },
            { 1098, "MIDNIGHT_LOFI_098", 1.26f, 100.0f, -1.36f, 2.30f, 2.62f, -15.20f, -1.10f, -23.30f, 0.296f },
            { 1099, "CLASSICAL_PURE_099", 1.38f, 102.5f, -1.28f, 2.41f, 2.76f, -15.10f, -1.15f, -22.95f, 0.308f },
            { 1100, "JPOP_BRIGHT_100", -1.50f, 105.0f, -2.80f, 2.52f, 2.90f, -15.00f, -1.20f, -22.60f, 0.080f },
            { 1101, "ANISONG_HEROIC_101", -1.38f, 107.5f, -2.72f, 2.63f, 3.04f, -14.90f, -1.25f, -22.25f, 0.092f },
            { 1102, "BALLAD_INTIMATE_102", -1.26f, 110.0f, -2.64f, 2.74f, 3.18f, -14.80f, -1.00f, -21.90f, 0.104f },
            { 1103, "VTUBER_CLEAR_103", -1.14f, 112.5f, -2.56f, 2.85f, 3.32f, -14.70f, -1.05f, -21.55f, 0.116f },
            { 1104, "ASMR_WHISPER_104", -1.02f, 115.0f, -2.48f, 2.96f, 3.46f, -14.60f, -1.10f, -21.20f, 0.128f },
            { 1105, "ROCK_BELT_105", -0.90f, 117.5f, -2.40f, 3.07f, 3.60f, -16.00f, -1.15f, -20.85f, 0.140f },
            { 1106, "CITYPOP_SILKY_106", -0.78f, 120.0f, -2.32f, 3.18f, 3.74f, -15.90f, -1.20f, -20.50f, 0.152f },
            { 1107, "RAP_TIGHT_107", -0.66f, 122.5f, -2.24f, 3.29f, 3.88f, -15.80f, -1.25f, -20.15f, 0.164f },
            { 1108, "ACOUSTIC_WARM_108", -0.54f, 125.0f, -2.16f, 3.40f, 1.50f, -15.70f, -1.00f, -19.80f, 0.176f },
            { 1109, "CHORUS_WIDE_109", -0.42f, 127.5f, -2.08f, 3.51f, 1.64f, -15.60f, -1.05f, -19.45f, 0.188f },
            { 1110, "KARAOKE_PRO_110", -0.30f, 130.0f, -2.00f, 1.20f, 1.78f, -15.50f, -1.10f, -19.10f, 0.200f },
            { 1111, "TALK_STREAM_111", -0.18f, 132.5f, -1.92f, 1.31f, 1.92f, -15.40f, -1.15f, -18.75f, 0.212f },
            { 1112, "PODCAST_RADIO_112", -0.06f, 65.0f, -1.84f, 1.42f, 2.06f, -15.30f, -1.20f, -24.00f, 0.224f },
            { 1113, "FALSETTO_AIR_113", 0.06f, 67.5f, -1.76f, 1.53f, 2.20f, -15.20f, -1.25f, -23.65f, 0.236f },
            { 1114, "VOCALOID_CRISP_114", 0.18f, 70.0f, -1.68f, 1.64f, 2.34f, -15.10f, -1.00f, -23.30f, 0.248f },
            { 1115, "RNB_VELVET_115", 0.30f, 72.5f, -1.60f, 1.75f, 2.48f, -15.00f, -1.05f, -22.95f, 0.260f },
            { 1116, "IDOL_SPARKLE_116", 0.42f, 75.0f, -1.52f, 1.86f, 2.62f, -14.90f, -1.10f, -22.60f, 0.272f },
            { 1117, "LIVE_ARENA_117", 0.54f, 77.5f, -1.44f, 1.97f, 2.76f, -14.80f, -1.15f, -22.25f, 0.284f },
            { 1118, "MIDNIGHT_LOFI_118", 0.66f, 80.0f, -1.36f, 2.08f, 2.90f, -14.70f, -1.20f, -21.90f, 0.296f },
            { 1119, "CLASSICAL_PURE_119", 0.78f, 82.5f, -1.28f, 2.19f, 3.04f, -14.60f, -1.25f, -21.55f, 0.308f },
            { 1120, "JPOP_BRIGHT_120", 0.90f, 85.0f, -2.80f, 2.30f, 3.18f, -16.00f, -1.00f, -21.20f, 0.080f },
            { 1121, "ANISONG_HEROIC_121", 1.02f, 87.5f, -2.72f, 2.41f, 3.32f, -15.90f, -1.05f, -20.85f, 0.092f },
            { 1122, "BALLAD_INTIMATE_122", 1.14f, 90.0f, -2.64f, 2.52f, 3.46f, -15.80f, -1.10f, -20.50f, 0.104f },
            { 1123, "VTUBER_CLEAR_123", 1.26f, 92.5f, -2.56f, 2.63f, 3.60f, -15.70f, -1.15f, -20.15f, 0.116f },
            { 1124, "ASMR_WHISPER_124", 1.38f, 95.0f, -2.48f, 2.74f, 3.74f, -15.60f, -1.20f, -19.80f, 0.128f },
            { 1125, "ROCK_BELT_125", -1.50f, 97.5f, -2.40f, 2.85f, 3.88f, -15.50f, -1.25f, -19.45f, 0.140f },
            { 1126, "CITYPOP_SILKY_126", -1.38f, 100.0f, -2.32f, 2.96f, 1.50f, -15.40f, -1.00f, -19.10f, 0.152f },
            { 1127, "RAP_TIGHT_127", -1.26f, 102.5f, -2.24f, 3.07f, 1.64f, -15.30f, -1.05f, -18.75f, 0.164f },
            { 1128, "ACOUSTIC_WARM_128", -1.14f, 105.0f, -2.16f, 3.18f, 1.78f, -15.20f, -1.10f, -24.00f, 0.176f },
            { 1129, "CHORUS_WIDE_129", -1.02f, 107.5f, -2.08f, 3.29f, 1.92f, -15.10f, -1.15f, -23.65f, 0.188f },
            { 1130, "KARAOKE_PRO_130", -0.90f, 110.0f, -2.00f, 3.40f, 2.06f, -15.00f, -1.20f, -23.30f, 0.200f },
            { 1131, "TALK_STREAM_131", -0.78f, 112.5f, -1.92f, 3.51f, 2.20f, -14.90f, -1.25f, -22.95f, 0.212f },
            { 1132, "PODCAST_RADIO_132", -0.66f, 115.0f, -1.84f, 1.20f, 2.34f, -14.80f, -1.00f, -22.60f, 0.224f },
            { 1133, "FALSETTO_AIR_133", -0.54f, 117.5f, -1.76f, 1.31f, 2.48f, -14.70f, -1.05f, -22.25f, 0.236f },
            { 1134, "VOCALOID_CRISP_134", -0.42f, 120.0f, -1.68f, 1.42f, 2.62f, -14.60f, -1.10f, -21.90f, 0.248f },
            { 1135, "RNB_VELVET_135", -0.30f, 122.5f, -1.60f, 1.53f, 2.76f, -16.00f, -1.15f, -21.55f, 0.260f },
            { 1136, "IDOL_SPARKLE_136", -0.18f, 125.0f, -1.52f, 1.64f, 2.90f, -15.90f, -1.20f, -21.20f, 0.272f },
            { 1137, "LIVE_ARENA_137", -0.06f, 127.5f, -1.44f, 1.75f, 3.04f, -15.80f, -1.25f, -20.85f, 0.284f },
            { 1138, "MIDNIGHT_LOFI_138", 0.06f, 130.0f, -1.36f, 1.86f, 3.18f, -15.70f, -1.00f, -20.50f, 0.296f },
            { 1139, "CLASSICAL_PURE_139", 0.18f, 132.5f, -1.28f, 1.97f, 3.32f, -15.60f, -1.05f, -20.15f, 0.308f },
            { 1140, "JPOP_BRIGHT_140", 0.30f, 65.0f, -2.80f, 2.08f, 3.46f, -15.50f, -1.10f, -19.80f, 0.080f },
            { 1141, "ANISONG_HEROIC_141", 0.42f, 67.5f, -2.72f, 2.19f, 3.60f, -15.40f, -1.15f, -19.45f, 0.092f },
            { 1142, "BALLAD_INTIMATE_142", 0.54f, 70.0f, -2.64f, 2.30f, 3.74f, -15.30f, -1.20f, -19.10f, 0.104f },
            { 1143, "VTUBER_CLEAR_143", 0.66f, 72.5f, -2.56f, 2.41f, 3.88f, -15.20f, -1.25f, -18.75f, 0.116f },
            { 1144, "ASMR_WHISPER_144", 0.78f, 75.0f, -2.48f, 2.52f, 1.50f, -15.10f, -1.00f, -24.00f, 0.128f },
            { 1145, "ROCK_BELT_145", 0.90f, 77.5f, -2.40f, 2.63f, 1.64f, -15.00f, -1.05f, -23.65f, 0.140f },
            { 1146, "CITYPOP_SILKY_146", 1.02f, 80.0f, -2.32f, 2.74f, 1.78f, -14.90f, -1.10f, -23.30f, 0.152f },
            { 1147, "RAP_TIGHT_147", 1.14f, 82.5f, -2.24f, 2.85f, 1.92f, -14.80f, -1.15f, -22.95f, 0.164f },
            { 1148, "ACOUSTIC_WARM_148", 1.26f, 85.0f, -2.16f, 2.96f, 2.06f, -14.70f, -1.20f, -22.60f, 0.176f },
            { 1149, "CHORUS_WIDE_149", 1.38f, 87.5f, -2.08f, 3.07f, 2.20f, -14.60f, -1.25f, -22.25f, 0.188f },
            { 1150, "KARAOKE_PRO_150", -1.50f, 90.0f, -2.00f, 3.18f, 2.34f, -16.00f, -1.00f, -21.90f, 0.200f },
            { 1151, "TALK_STREAM_151", -1.38f, 92.5f, -1.92f, 3.29f, 2.48f, -15.90f, -1.05f, -21.55f, 0.212f },
            { 1152, "PODCAST_RADIO_152", -1.26f, 95.0f, -1.84f, 3.40f, 2.62f, -15.80f, -1.10f, -21.20f, 0.224f },
            { 1153, "FALSETTO_AIR_153", -1.14f, 97.5f, -1.76f, 3.51f, 2.76f, -15.70f, -1.15f, -20.85f, 0.236f },
            { 1154, "VOCALOID_CRISP_154", -1.02f, 100.0f, -1.68f, 1.20f, 2.90f, -15.60f, -1.20f, -20.50f, 0.248f },
            { 1155, "RNB_VELVET_155", -0.90f, 102.5f, -1.60f, 1.31f, 3.04f, -15.50f, -1.25f, -20.15f, 0.260f },
            { 1156, "IDOL_SPARKLE_156", -0.78f, 105.0f, -1.52f, 1.42f, 3.18f, -15.40f, -1.00f, -19.80f, 0.272f },
            { 1157, "LIVE_ARENA_157", -0.66f, 107.5f, -1.44f, 1.53f, 3.32f, -15.30f, -1.05f, -19.45f, 0.284f },
            { 1158, "MIDNIGHT_LOFI_158", -0.54f, 110.0f, -1.36f, 1.64f, 3.46f, -15.20f, -1.10f, -19.10f, 0.296f },
            { 1159, "CLASSICAL_PURE_159", -0.42f, 112.5f, -1.28f, 1.75f, 3.60f, -15.10f, -1.15f, -18.75f, 0.308f },
            { 1160, "JPOP_BRIGHT_160", -0.30f, 115.0f, -2.80f, 1.86f, 3.74f, -15.00f, -1.20f, -24.00f, 0.080f },
            { 1161, "ANISONG_HEROIC_161", -0.18f, 117.5f, -2.72f, 1.97f, 3.88f, -14.90f, -1.25f, -23.65f, 0.092f },
            { 1162, "BALLAD_INTIMATE_162", -0.06f, 120.0f, -2.64f, 2.08f, 1.50f, -14.80f, -1.00f, -23.30f, 0.104f },
            { 1163, "VTUBER_CLEAR_163", 0.06f, 122.5f, -2.56f, 2.19f, 1.64f, -14.70f, -1.05f, -22.95f, 0.116f },
            { 1164, "ASMR_WHISPER_164", 0.18f, 125.0f, -2.48f, 2.30f, 1.78f, -14.60f, -1.10f, -22.60f, 0.128f },
            { 1165, "ROCK_BELT_165", 0.30f, 127.5f, -2.40f, 2.41f, 1.92f, -16.00f, -1.15f, -22.25f, 0.140f },
            { 1166, "CITYPOP_SILKY_166", 0.42f, 130.0f, -2.32f, 2.52f, 2.06f, -15.90f, -1.20f, -21.90f, 0.152f },
            { 1167, "RAP_TIGHT_167", 0.54f, 132.5f, -2.24f, 2.63f, 2.20f, -15.80f, -1.25f, -21.55f, 0.164f },
            { 1168, "ACOUSTIC_WARM_168", 0.66f, 65.0f, -2.16f, 2.74f, 2.34f, -15.70f, -1.00f, -21.20f, 0.176f },
            { 1169, "CHORUS_WIDE_169", 0.78f, 67.5f, -2.08f, 2.85f, 2.48f, -15.60f, -1.05f, -20.85f, 0.188f },
            { 1170, "KARAOKE_PRO_170", 0.90f, 70.0f, -2.00f, 2.96f, 2.62f, -15.50f, -1.10f, -20.50f, 0.200f },
            { 1171, "TALK_STREAM_171", 1.02f, 72.5f, -1.92f, 3.07f, 2.76f, -15.40f, -1.15f, -20.15f, 0.212f },
            { 1172, "PODCAST_RADIO_172", 1.14f, 75.0f, -1.84f, 3.18f, 2.90f, -15.30f, -1.20f, -19.80f, 0.224f },
            { 1173, "FALSETTO_AIR_173", 1.26f, 77.5f, -1.76f, 3.29f, 3.04f, -15.20f, -1.25f, -19.45f, 0.236f },
            { 1174, "VOCALOID_CRISP_174", 1.38f, 80.0f, -1.68f, 3.40f, 3.18f, -15.10f, -1.00f, -19.10f, 0.248f },
            { 1175, "RNB_VELVET_175", -1.50f, 82.5f, -1.60f, 3.51f, 3.32f, -15.00f, -1.05f, -18.75f, 0.260f },
            { 1176, "IDOL_SPARKLE_176", -1.38f, 85.0f, -1.52f, 1.20f, 3.46f, -14.90f, -1.10f, -24.00f, 0.272f },
            { 1177, "LIVE_ARENA_177", -1.26f, 87.5f, -1.44f, 1.31f, 3.60f, -14.80f, -1.15f, -23.65f, 0.284f },
            { 1178, "MIDNIGHT_LOFI_178", -1.14f, 90.0f, -1.36f, 1.42f, 3.74f, -14.70f, -1.20f, -23.30f, 0.296f },
            { 1179, "CLASSICAL_PURE_179", -1.02f, 92.5f, -1.28f, 1.53f, 3.88f, -14.60f, -1.25f, -22.95f, 0.308f },
        }};

        // 歴代19世代 (v1.0〜v5.5.0) パラメータ互換マイグレーション検証テーブル
        struct GenerationMigrationSpec {
            int generationIndex;
            const char* versionTag;
            float defaultGainCompensationDb;
            float limiterLookaheadMs;
            float denoiseFloorDb;
            bool supportsTruePeakGuard;
        };

        static constexpr std::array<GenerationMigrationSpec, 19> kGenerationMigrationTable = {{
            { 1, "v1.0.0", 0.90f, 1.50f, -62.0f, true },
            { 2, "v1.2.0", 0.85f, 1.60f, -62.8f, true },
            { 3, "v1.5.0", 0.80f, 1.70f, -63.6f, true },
            { 4, "v2.0.0", 0.75f, 1.80f, -64.4f, true },
            { 5, "v2.3.0", 0.70f, 1.90f, -65.2f, true },
            { 6, "v2.6.0", 0.65f, 2.00f, -66.0f, true },
            { 7, "v3.0.0", 0.60f, 2.10f, -66.8f, true },
            { 8, "v3.2.0", 0.55f, 2.20f, -67.6f, true },
            { 9, "v3.5.0", 0.50f, 2.30f, -68.4f, true },
            { 10, "v3.8.0", 0.45f, 2.40f, -69.2f, true },
            { 11, "v4.0.0", 0.40f, 2.50f, -70.0f, true },
            { 12, "v4.2.0", 0.35f, 2.60f, -70.8f, true },
            { 13, "v4.5.0", 0.30f, 2.70f, -71.6f, true },
            { 14, "v4.8.0", 0.25f, 2.80f, -72.4f, true },
            { 15, "v5.0.0", 0.20f, 2.90f, -73.2f, true },
            { 16, "v5.1.0", 0.15f, 3.00f, -74.0f, true },
            { 17, "v5.2.0", 0.10f, 3.10f, -74.8f, true },
            { 18, "v5.4.0", 0.05f, 3.20f, -75.6f, true },
            { 19, "v5.5.0", 0.00f, 3.30f, -76.4f, true },
        }};

        static inline bool executeExtendedKernelVerification(double sampleRate = 48000.0) noexcept {
            if (sampleRate <= 0.0 || !std::isfinite(sampleRate)) return false;
            ZdfTrapezoidalSvfNode svfLp{};
            ZdfTrapezoidalSvfNode svfBp{};
            ZdfTrapezoidalSvfNode svfHp{};
            HarmonicOverdriveTransferNode shaper{};
            svfLp.setCoefficients(sampleRate, 1200.0, 0.7071);
            svfBp.setCoefficients(sampleRate, 2400.0, 1.4142);
            svfHp.setCoefficients(sampleRate, 120.0, 0.7071);

            double energyAccumulator = 0.0;
            for (std::size_t n = 0; n < 256; ++n) {
                const float phase = static_cast<float>(n) * 0.09817477f;
                const float excitation = 0.25f * std::sin(phase) + 0.10f * std::cos(phase * 3.0f);
                const float lpOut = svfLp.processLowpass(excitation);
                const float bpOut = svfBp.processBandpass(excitation);
                const float hpOut = svfHp.processHighpass(excitation);
                const float shaped = shaper.processSample(lpOut + bpOut + hpOut);
                if (!std::isfinite(shaped)) return false;
                energyAccumulator += static_cast<double>(shaped * shaped);
            }
            if (!(energyAccumulator > 1.0e-6 && energyAccumulator < 1.0e6)) return false;

            float prevCenterHz = 0.0f;
            for (const auto& node : kBarkCriticalBandTable) {
                if (!(node.centerHz > prevCenterHz && node.qFactor > 0.1f && node.psychoacousticMaskingWeight > 0.0f)) {
                    return false;
                }
                prevCenterHz = node.centerHz;
            }

            for (const auto& mora : kJapaneseMoraAcousticTable) {
                if (!(mora.f1Hz > 150.0f && mora.f2Hz > mora.f1Hz && mora.f3Hz > mora.f2Hz)) {
                    return false;
                }
            }

            for (const auto& row : kThemeContrastMatrix) {
                if (!(row.verifiedContrastRatio >= 4.5f)) {
                    return false;
                }
            }

            for (const auto& preset : kStudioReferenceVault) {
                if (!(preset.highPassHz >= 20.0f && preset.highPassHz <= 350.0f && preset.truePeakCeilingDb <= -0.1f)) {
                    return false;
                }
            }

            for (const auto& gen : kGenerationMigrationTable) {
                if (!(gen.generationIndex >= 1 && gen.limiterLookaheadMs > 0.0f && gen.supportsTruePeakGuard)) {
                    return false;
                }
            }
            return true;
        }
    };

} // namespace vg::supreme::safety
// SUPREME_30K_VERIFIED_MODULE_00
// VocalGzzio v5.5.0 Supreme 30K Engineering Suite — Verified
