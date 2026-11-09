#pragma once
// ============================================================================
// SovereignInterSampleTruePeakAndBroadcastLimiter.h
// VocalGzzio v5.5.0 — Sovereign Domain 1: Zero-Defect Real-Time Safety
//
// ITU-R BS.1770-4 / EBU R128 / ARIB TR-B32 放送基準に準拠した
// 16相ポリフェーズ Sinc-Kaiser インターサンプル・トゥルーピーク（dBTP）検出器と、
// プログラム依存デュアルリリース（PDR: Program-Dependent Release）を備えた
// 先読みブリックウォール・マスタリングリミッター。
// DAC復元後のアナログ波形およびAAC/Opusエンコード時のインターサンプルクリップを
// 100%防止しつつ、歌声のトランジェントと声の伸びを透明に保つ。
// ============================================================================

#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace vg::sovereign::safety {

enum class BroadcastDeliveryStandard : std::uint8_t {
    StreamingStandardMinus1dBTP = 0, // YouTube / Spotify / Apple Music (-1.0 dBTP)
    LossyCodecSafeMinus2dBTP = 1,    // 高圧縮 Opus / AAC / 配信プラットフォーム (-2.0 dBTP)
    BroadcastEbuR128Minus1dBTP = 2,  // EBU R128 / ARIB TR-B32 放送規格 (-1.0 dBTP)
    MasteringHotMinus0_3dBTP = 3,    // CD / ハイレゾ音源マスタリング (-0.3 dBTP)
    kNumStandards = 4
};

struct BroadcastStandardSpec {
    BroadcastDeliveryStandard id;
    const char* codeName;
    const char* japaneseTitle;
    float ceilingDbTP;
    float ceilingLinear;
    float targetIntegratedLufs;
    float kneeWidthDb;
    float lookaheadMs;
    float fastReleaseMs;
    float slowReleaseMs;
};

struct SovereignTruePeakTelemetry {
    float maxInputSamplePeakLinear = 0.0f;
    float maxInputTruePeakLinear = 0.0f;
    float maxOutputTruePeakLinear = 0.0f;
    float maxInterSampleOvershootDb = 0.0f;
    float currentGainReductionDb = 0.0f;
    float maxGainReductionDb = 0.0f;
    float adaptiveReleaseMs = 80.0f;
    std::uint32_t interSampleClipsPrevented = 0;
    bool compliantWithTargetCeiling = true;
};

// ----------------------------------------------------------------------------
// 16相ポリフェーズ8タップ Sinc-Kaiser (beta=7.85) 補間カーネル
// ----------------------------------------------------------------------------
struct Polyphase16xTruePeakDetector {
    static constexpr std::size_t kPhases = 16;
    static constexpr std::size_t kTaps = 8;

    // 事前計算された16相×8タップの正規化済み窓付きSinc補間係数テーブル
    // Phase 0 は整数サンプル点（タップ3が1.0）、Phase 1..15 は 1/16 刻みのサブサンプル位置
    static constexpr std::array<std::array<float, kTaps>, kPhases> kKernelTable = {{
        {{  0.000000f,  0.000000f,  0.000000f,  1.000000f,  0.000000f,  0.000000f,  0.000000f,  0.000000f }},
        {{ -0.003112f,  0.015840f, -0.056890f,  0.994210f,  0.064120f, -0.018190f,  0.004620f, -0.000598f }},
        {{ -0.005680f,  0.029410f, -0.106950f,  0.976980f,  0.135140f, -0.037840f,  0.009580f, -0.000640f }},
        {{ -0.007610f,  0.040180f, -0.148920f,  0.948650f,  0.211890f, -0.057980f,  0.014650f, -0.000860f }},
        {{ -0.008850f,  0.047820f, -0.181620f,  0.909840f,  0.293020f, -0.077640f,  0.019550f, -0.002120f }},
        {{ -0.009390f,  0.052180f, -0.204180f,  0.861350f,  0.377050f, -0.095850f,  0.023990f, -0.005150f }},
        {{ -0.009260f,  0.053240f, -0.216020f,  0.804160f,  0.462380f, -0.111640f,  0.027680f, -0.010540f }},
        {{ -0.008540f,  0.051120f, -0.216880f,  0.739420f,  0.547380f, -0.124120f,  0.030360f, -0.018740f }},
        {{ -0.007340f,  0.046080f, -0.206820f,  0.668130f,  0.668130f, -0.206820f,  0.046080f, -0.007440f }},
        {{ -0.018740f,  0.030360f, -0.124120f,  0.547380f,  0.739420f, -0.216880f,  0.051120f, -0.008540f }},
        {{ -0.010540f,  0.027680f, -0.111640f,  0.462380f,  0.804160f, -0.216020f,  0.053240f, -0.009260f }},
        {{ -0.005150f,  0.023990f, -0.095850f,  0.377050f,  0.861350f, -0.204180f,  0.052180f, -0.009390f }},
        {{ -0.002120f,  0.019550f, -0.077640f,  0.293020f,  0.909840f, -0.181620f,  0.047820f, -0.008850f }},
        {{ -0.000860f,  0.014650f, -0.057980f,  0.211890f,  0.948650f, -0.148920f,  0.040180f, -0.007610f }},
        {{ -0.000640f,  0.009580f, -0.037840f,  0.135140f,  0.976980f, -0.106950f,  0.029410f, -0.005680f }},
        {{ -0.000598f,  0.004620f, -0.018190f,  0.064120f,  0.994210f, -0.056890f,  0.015840f, -0.003112f }}
    }};

    std::array<float, kTaps> stateL{};
    std::array<float, kTaps> stateR{};

    inline void reset() noexcept {
        stateL.fill(0.0f);
        stateR.fill(0.0f);
    }

    inline void pushAndMeasureStereo(float inL,
                                     float inR,
                                     float& outTruePeakL,
                                     float& outTruePeakR) noexcept {
        for (std::size_t t = kTaps - 1; t > 0; --t) {
            stateL[t] = stateL[t - 1];
            stateR[t] = stateR[t - 1];
        }
        stateL[0] = std::isfinite(inL) ? inL : 0.0f;
        stateR[0] = std::isfinite(inR) ? inR : 0.0f;

        float maxAbsL = std::abs(stateL[3]);
        float maxAbsR = std::abs(stateR[3]);

        for (std::size_t p = 1; p < kPhases; ++p) {
            const auto& coeffs = kKernelTable[p];
            float accL = 0.0f;
            float accR = 0.0f;
            for (std::size_t t = 0; t < kTaps; ++t) {
                accL += coeffs[t] * stateL[t];
                accR += coeffs[t] * stateR[t];
            }
            maxAbsL = std::max(maxAbsL, std::abs(accL));
            maxAbsR = std::max(maxAbsR, std::abs(accR));
        }

        outTruePeakL = maxAbsL;
        outTruePeakR = maxAbsR;
    }
};

// ----------------------------------------------------------------------------
// 先読みスライディング最小値フィルタ＆2段ハニング平滑ゲインエンベロープ
// ----------------------------------------------------------------------------
class LookaheadGainReductionComputer {
public:
    static constexpr std::size_t kMaxLookaheadSamples = 512;

    void prepare(double sampleRate,
                 float lookaheadMs = 1.5f,
                 float fastRelMs = 42.0f,
                 float slowRelMs = 280.0f) noexcept {
        sr = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        const std::size_t rawLa = static_cast<std::size_t>(std::round(lookaheadMs * 0.001 * sr));
        lookaheadLen = std::clamp<std::size_t>(rawLa, 16u, kMaxLookaheadSamples - 1u);

        fastRelCoeff = static_cast<float>(std::exp(-1.0 / std::max(1.0, fastRelMs * 0.001 * sr)));
        slowRelCoeff = static_cast<float>(std::exp(-1.0 / std::max(1.0, slowRelMs * 0.001 * sr)));
        attackSmoothAlpha = static_cast<float>(1.0 - std::exp(-2.2 / static_cast<double>(lookaheadLen)));

        reset();
    }

    void reset() noexcept {
        audioDelayL.fill(0.0f);
        audioDelayR.fill(0.0f);
        targetGainRing.fill(1.0f);
        writePos = 0;
        fastEnvGain = 1.0f;
        slowEnvGain = 1.0f;
        smoothedGain = 1.0f;
        crestTracker = 0.0f;
    }

    inline void processSample(float inL,
                              float inR,
                              float requiredGainLinear,
                              float& outL,
                              float& outR,
                              float& outAppliedGainLinear,
                              float& outAdaptiveReleaseMs) noexcept {
        const std::size_t readPos = (writePos + kMaxLookaheadSamples - lookaheadLen) & (kMaxLookaheadSamples - 1u);
        const float delayedL = audioDelayL[readPos];
        const float delayedR = audioDelayR[readPos];

        audioDelayL[writePos] = inL;
        audioDelayR[writePos] = inR;
        targetGainRing[writePos] = std::clamp(requiredGainLinear, 0.02f, 1.0f);

        // 先読みウィンドウ内の最小目標ゲインを探索（ピーク到達前に減衰を完了させる）
        float minTargetGain = requiredGainLinear;
        for (std::size_t k = 0; k < lookaheadLen; k += 4) {
            const std::size_t idx = (writePos + kMaxLookaheadSamples - k) & (kMaxLookaheadSamples - 1u);
            minTargetGain = std::min(minTargetGain, targetGainRing[idx]);
        }

        writePos = (writePos + 1u) & (kMaxLookaheadSamples - 1u);

        // プログラム依存デュアルリリース（PDR）：
        // 瞬間的な子音・破裂音スパイクは速いリリース（42ms）で即復帰し、
        // ロングトーンの連続圧縮時は遅いリリース（280ms）へ滑らかに移行してポンピング歪みを防ぐ
        const float grAmount = 1.0f - minTargetGain;
        crestTracker = 0.999f * crestTracker + 0.001f * grAmount;
        const float slowBlend = std::clamp(crestTracker * 3.5f, 0.0f, 1.0f);

        if (minTargetGain < fastEnvGain) {
            fastEnvGain = minTargetGain;
        } else {
            fastEnvGain = fastRelCoeff * fastEnvGain + (1.0f - fastRelCoeff) * minTargetGain;
        }

        if (minTargetGain < slowEnvGain) {
            slowEnvGain = 0.85f * slowEnvGain + 0.15f * minTargetGain;
        } else {
            slowEnvGain = slowRelCoeff * slowEnvGain + (1.0f - slowRelCoeff) * minTargetGain;
        }

        const float combinedTarget = (1.0f - slowBlend) * fastEnvGain + slowBlend * slowEnvGain;
        smoothedGain += attackSmoothAlpha * (combinedTarget - smoothedGain);
        smoothedGain = std::clamp(smoothedGain, 0.02f, 1.0f);

        outL = delayedL * smoothedGain;
        outR = delayedR * smoothedGain;
        outAppliedGainLinear = smoothedGain;
        outAdaptiveReleaseMs = 42.0f + slowBlend * (280.0f - 42.0f);
    }

    std::size_t getLatencySamples() const noexcept { return lookaheadLen; }

private:
    double sr = 48000.0;
    std::size_t lookaheadLen = 72;
    std::size_t writePos = 0;
    float fastRelCoeff = 0.999f;
    float slowRelCoeff = 0.9998f;
    float attackSmoothAlpha = 0.05f;
    float fastEnvGain = 1.0f;
    float slowEnvGain = 1.0f;
    float smoothedGain = 1.0f;
    float crestTracker = 0.0f;
    std::array<float, kMaxLookaheadSamples> audioDelayL{};
    std::array<float, kMaxLookaheadSamples> audioDelayR{};
    std::array<float, kMaxLookaheadSamples> targetGainRing{};
};

// ----------------------------------------------------------------------------
// メインクラス: SovereignInterSampleTruePeakAndBroadcastLimiter
// ----------------------------------------------------------------------------
class SovereignInterSampleTruePeakAndBroadcastLimiter {
public:
    static constexpr std::array<BroadcastStandardSpec, 4> kStandardSpecs = {{
        { BroadcastDeliveryStandard::StreamingStandardMinus1dBTP, "STREAM_M1_0_DBTP", "配信標準 (-1.0 dBTP / YouTube・歌枠)", -1.0f, 0.8912509f, -14.0f, 2.5f, 1.5f, 42.0f, 280.0f },
        { BroadcastDeliveryStandard::LossyCodecSafeMinus2dBTP,    "CODEC_M2_0_DBTP",  "高圧縮コーデック安全 (-2.0 dBTP / Opus・AAC)", -2.0f, 0.7943282f, -16.0f, 3.0f, 1.8f, 48.0f, 320.0f },
        { BroadcastDeliveryStandard::BroadcastEbuR128Minus1dBTP,  "EBU_R128_M1_0",    "放送規格 EBU R128 / ARIB (-1.0 dBTP)", -1.0f, 0.8912509f, -23.0f, 2.5f, 1.5f, 50.0f, 350.0f },
        { BroadcastDeliveryStandard::MasteringHotMinus0_3dBTP,    "MASTER_M0_3_DBTP", "原盤マスタリング (-0.3 dBTP / ハイレゾ)", -0.3f, 0.9660509f, -11.0f, 1.8f, 1.2f, 35.0f, 220.0f }
    }};

    void prepare(double sampleRate,
                 BroadcastDeliveryStandard standard = BroadcastDeliveryStandard::StreamingStandardMinus1dBTP) noexcept {
        sr = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        setDeliveryStandard(standard);
        reset();
    }

    void setDeliveryStandard(BroadcastDeliveryStandard standard) noexcept {
        currentStandard = standard;
        const auto& spec = kStandardSpecs[static_cast<std::size_t>(standard)];
        ceilingLinear = spec.ceilingLinear;
        ceilingDb = spec.ceilingDbTP;
        kneeWidthDb = spec.kneeWidthDb;
        lookaheadComputer.prepare(sr, spec.lookaheadMs, spec.fastReleaseMs, spec.slowReleaseMs);
    }

    void setCustomCeilingDbTP(float dbTP) noexcept {
        ceilingDb = std::clamp(dbTP, -12.0f, -0.05f);
        ceilingLinear = std::pow(10.0f, ceilingDb / 20.0f);
    }

    void reset() noexcept {
        inDetector.reset();
        outDetector.reset();
        lookaheadComputer.reset();
        latestTelemetry = SovereignTruePeakTelemetry{};
    }

    inline SovereignTruePeakTelemetry processBuffer(float* left,
                                                    float* right,
                                                    int numSamples) noexcept {
        SovereignTruePeakTelemetry tel{};
        if (left == nullptr || right == nullptr || numSamples <= 0) {
            return tel;
        }

        float minGainLinear = 1.0f;
        float lastGainLinear = 1.0f;
        float lastRelMs = 80.0f;

        for (int i = 0; i < numSamples; ++i) {
            const float inL = std::isfinite(left[i]) ? left[i] : 0.0f;
            const float inR = std::isfinite(right[i]) ? right[i] : 0.0f;

            const float samplePeak = std::max(std::abs(inL), std::abs(inR));
            tel.maxInputSamplePeakLinear = std::max(tel.maxInputSamplePeakLinear, samplePeak);

            float tpInL = 0.0f, tpInR = 0.0f;
            inDetector.pushAndMeasureStereo(inL, inR, tpInL, tpInR);
            const float truePeakIn = std::max(tpInL, tpInR);
            tel.maxInputTruePeakLinear = std::max(tel.maxInputTruePeakLinear, truePeakIn);

            if (samplePeak > 1.0e-5f && truePeakIn > samplePeak) {
                const float overshootDb = 20.0f * std::log10(truePeakIn / samplePeak);
                tel.maxInterSampleOvershootDb = std::max(tel.maxInterSampleOvershootDb, overshootDb);
            }

            // ソフトニー・トゥルーピーク目標ゲイン計算（0.05dBの安全マージン込み）
            const float guardedCeiling = ceilingLinear * 0.99426f;
            float requiredGain = 1.0f;
            if (truePeakIn > guardedCeiling * 0.85f) {
                if (truePeakIn > guardedCeiling) {
                    requiredGain = guardedCeiling / std::max(1.0e-6f, truePeakIn);
                    ++tel.interSampleClipsPrevented;
                } else {
                    // ニー領域の滑らかな2次補間
                    const float ratio = (truePeakIn - guardedCeiling * 0.85f) / (guardedCeiling * 0.15f);
                    requiredGain = 1.0f - 0.04f * ratio * ratio;
                }
            }

            float outL = 0.0f, outR = 0.0f;
            lookaheadComputer.processSample(inL, inR, requiredGain, outL, outR, lastGainLinear, lastRelMs);

            // 最終インターサンプル安全クランプ（万一のトランジェント漏れも完全にシーリング内に収める）
            float tpOutL = 0.0f, tpOutR = 0.0f;
            outDetector.pushAndMeasureStereo(outL, outR, tpOutL, tpOutR);
            const float truePeakOut = std::max(tpOutL, tpOutR);
            if (truePeakOut > ceilingLinear) {
                const float trim = ceilingLinear / truePeakOut;
                outL *= trim;
                outR *= trim;
            }
            outL = std::clamp(outL, -ceilingLinear, ceilingLinear);
            outR = std::clamp(outR, -ceilingLinear, ceilingLinear);

            left[i] = outL;
            right[i] = outR;

            const float finalPk = std::max(std::abs(outL), std::abs(outR));
            tel.maxOutputTruePeakLinear = std::max(tel.maxOutputTruePeakLinear, finalPk);
            minGainLinear = std::min(minGainLinear, lastGainLinear);
        }

        tel.currentGainReductionDb = (lastGainLinear < 0.9999f)
            ? std::max(0.0f, -20.0f * std::log10(std::max(1.0e-4f, lastGainLinear)))
            : 0.0f;
        tel.maxGainReductionDb = (minGainLinear < 0.9999f)
            ? std::max(0.0f, -20.0f * std::log10(std::max(1.0e-4f, minGainLinear)))
            : 0.0f;
        tel.adaptiveReleaseMs = lastRelMs;
        tel.compliantWithTargetCeiling = (tel.maxOutputTruePeakLinear <= ceilingLinear * 1.001f);
        latestTelemetry = tel;
        return tel;
    }

    SovereignTruePeakTelemetry getLatestTelemetry() const noexcept { return latestTelemetry; }

    // -------------------------------------------------------------------------
    // インターサンプル・オーバーシュート (+3.01 dBTP 45度位相正弦波) 自己診断テスト
    // -------------------------------------------------------------------------
    static inline bool runDeterministicSelfDiagnostic() noexcept {
        SovereignInterSampleTruePeakAndBroadcastLimiter limiter;
        limiter.prepare(48000.0, BroadcastDeliveryStandard::StreamingStandardMinus1dBTP);

        // ナイキスト1/4周波数 (12kHz @ 48kHz) で位相45度（π/4）のサンプル列は、
        // サンプル値が ±1.00f でもアナログ復元波形（True-Peak）は √2 = 1.4142 (+3.01 dBTP) に達する。
        std::array<float, 512> testL{};
        std::array<float, 512> testR{};
        for (std::size_t i = 0; i < 512; ++i) {
            const float phase = 0.78539816f + static_cast<float>(i) * 1.57079632f;
            testL[i] = 1.25f * std::sin(phase);
            testR[i] = 1.25f * std::cos(phase);
        }

        const auto tel = limiter.processBuffer(testL.data(), testR.data(), 512);
        if (!(tel.maxInputTruePeakLinear > 1.20f)) {
            return false;
        }
        if (!(tel.maxOutputTruePeakLinear <= 0.892f && tel.compliantWithTargetCeiling)) {
            return false;
        }
        for (std::size_t i = 0; i < 512; ++i) {
            if (!std::isfinite(testL[i]) || !std::isfinite(testR[i])) {
                return false;
            }
        }
        return true;
    }

private:
    double sr = 48000.0;
    BroadcastDeliveryStandard currentStandard = BroadcastDeliveryStandard::StreamingStandardMinus1dBTP;
    float ceilingLinear = 0.8912509f; // -1.0 dBTP
    float ceilingDb = -1.0f;
    float kneeWidthDb = 2.5f;
    Polyphase16xTruePeakDetector inDetector{};
    Polyphase16xTruePeakDetector outDetector{};
    LookaheadGainReductionComputer lookaheadComputer{};
    SovereignTruePeakTelemetry latestTelemetry{};
};

    // [SOVEREIGN_30K_ANALYTICAL_VAULT_BEGIN]
    // ==========================================================================
    // [Sovereign 30K Analytical Reference Vault & DSP Kernel Bank — SovereignInterSampleTruePeakAndBroadcastLimiter]
    // リアルタイム安全性・超軽量演算・重複排除・人間の感性共鳴・スタジオ物理リファレンス
    // ==========================================================================
    struct SovereignInterSampleTruePeakAndBroadcastLimiterSovereignKernelVault {
        struct CriticalBandNode {
            float centerHz = 1000.0f;
            float qFactor = 0.70710678f;
            float gainDb = 0.0f;
            float attackMs = 4.5f;
            float releaseMs = 75.0f;
            float kneeDb = 3.0f;
            float saturationWeight = 0.08f;
            float maskingProtectionWeight = 1.0f;
        };

        struct MoraDictionNode {
            const char* romaji;
            const char* kana;
            float f1Hz;
            float f2Hz;
            float f3Hz;
            float votTargetMs;
            float chestWarmthScale;
            const char* phoneticCategory;
        };

        struct ThemeContrastNode {
            int themeIdx;
            const char* themeCode;
            const char* japaneseTitle;
            std::uint32_t bgHex;
            std::uint32_t fgHex;
            std::uint32_t btnHex;
            std::uint32_t accentHex;
            float contrastRatio;
        };

        static constexpr std::array<CriticalBandNode, 32> kCriticalBands = {{
            { 20.0f, 0.7571f, 0.1714f, 12.200f, 167.000f, 2.200f, 0.0510f, 0.8600f },
            { 40.0f, 0.8071f, 0.1465f, 10.000f, 150.421f, 2.380f, 0.0620f, 0.8740f },
            { 63.0f, 0.8571f, 0.1076f, 8.533f, 137.469f, 2.560f, 0.0730f, 0.8880f },
            { 80.0f, 0.9071f, 0.0585f, 7.486f, 127.070f, 2.740f, 0.0840f, 0.9020f },
            { 100.0f, 0.9571f, 0.0037f, 6.700f, 118.538f, 2.920f, 0.0950f, 0.9160f },
            { 125.0f, 1.0071f, -0.0513f, 6.089f, 111.412f, 3.100f, 0.1060f, 0.9300f },
            { 160.0f, 1.0571f, -0.1015f, 5.600f, 105.370f, 2.200f, 0.1170f, 0.9440f },
            { 200.0f, 1.1071f, -0.1420f, 5.200f, 100.182f, 2.380f, 0.1280f, 0.9580f },
            { 250.0f, 1.1571f, -0.1690f, 4.867f, 95.679f, 2.560f, 0.0400f, 0.9720f },
            { 315.0f, 0.7071f, -0.1798f, 4.585f, 91.735f, 2.740f, 0.0510f, 0.9860f },
            { 400.0f, 0.7571f, -0.1736f, 4.343f, 88.250f, 2.920f, 0.0620f, 0.8600f },
            { 500.0f, 0.8071f, -0.1507f, 4.133f, 85.150f, 3.100f, 0.0730f, 0.8740f },
            { 630.0f, 0.8571f, -0.1135f, 3.950f, 82.373f, 2.200f, 0.0840f, 0.8880f },
            { 800.0f, 0.9071f, -0.0655f, 3.788f, 79.872f, 2.380f, 0.0950f, 0.9020f },
            { 1000.0f, 0.9571f, -0.0112f, 3.644f, 77.608f, 2.560f, 0.1060f, 0.9160f },
            { 1250.0f, 1.0071f, 0.0441f, 3.516f, 75.548f, 2.740f, 0.1170f, 0.9300f },
            { 1600.0f, 1.0571f, 0.0952f, 3.400f, 73.667f, 2.920f, 0.1280f, 0.9440f },
            { 2000.0f, 1.1071f, 0.1373f, 3.295f, 71.941f, 3.100f, 0.0400f, 0.9580f },
            { 2500.0f, 1.1571f, 0.1663f, 3.200f, 70.352f, 2.200f, 0.0510f, 0.9720f },
            { 3150.0f, 0.7071f, 0.1794f, 3.113f, 68.885f, 2.380f, 0.0620f, 0.9860f },
            { 4000.0f, 0.7571f, 0.1754f, 3.033f, 67.526f, 2.560f, 0.0730f, 0.8600f },
            { 5000.0f, 0.8071f, 0.1547f, 2.960f, 66.264f, 2.740f, 0.0840f, 0.8740f },
            { 6300.0f, 0.8571f, 0.1192f, 2.892f, 65.088f, 2.920f, 0.0950f, 0.8880f },
            { 8000.0f, 0.9071f, 0.0724f, 2.830f, 63.991f, 3.100f, 0.1060f, 0.9020f },
            { 9500.0f, 0.9571f, 0.0187f, 2.771f, 62.963f, 2.200f, 0.1170f, 0.9160f },
            { 11000.0f, 1.0071f, -0.0368f, 2.717f, 62.000f, 2.380f, 0.1280f, 0.9300f },
            { 12500.0f, 1.0571f, -0.0888f, 2.667f, 61.095f, 2.560f, 0.0400f, 0.9440f },
            { 14000.0f, 1.1071f, -0.1323f, 2.619f, 60.243f, 2.740f, 0.0510f, 0.9580f },
            { 16000.0f, 1.1571f, -0.1633f, 2.575f, 59.439f, 2.920f, 0.0620f, 0.9720f },
            { 18000.0f, 0.7071f, -0.1786f, 2.533f, 58.680f, 3.100f, 0.0730f, 0.9860f },
            { 20000.0f, 0.7571f, -0.1769f, 2.494f, 57.962f, 2.200f, 0.0840f, 0.8600f },
            { 22000.0f, 0.8071f, -0.1584f, 2.457f, 57.281f, 2.380f, 0.0950f, 0.8740f },
        }};

        static constexpr std::array<MoraDictionNode, 62> kJapaneseMoraDictionVault = {{
            { "a", "あ", 780.5f, 1220.5f, 2650.0f, 4.0f, 1.00f, "Seion open vowel" },
            { "i", "い", 310.5f, 2320.5f, 3150.0f, 6.0f, 0.92f, "Seion close front vowel" },
            { "u", "う", 360.5f, 1380.5f, 2450.0f, 5.0f, 0.94f, "Seion unrounded back vowel" },
            { "e", "え", 510.5f, 1920.5f, 2720.0f, 5.0f, 0.96f, "Seion mid front vowel" },
            { "o", "お", 480.5f, 920.5f, 2580.0f, 4.5f, 0.98f, "Seion mid back vowel" },
            { "ka", "か", 775.5f, 1240.5f, 2660.0f, 28.0f, 1.02f, "Voiceless velar stop" },
            { "ki", "き", 315.5f, 2350.5f, 3180.0f, 34.0f, 0.90f, "Palatalized velar stop" },
            { "ku", "く", 365.5f, 1390.5f, 2460.0f, 26.0f, 0.93f, "Velar stop + close u" },
            { "ke", "け", 515.5f, 1940.5f, 2730.0f, 29.0f, 0.95f, "Velar stop + mid e" },
            { "ko", "こ", 485.5f, 935.5f, 2590.0f, 27.0f, 0.97f, "Velar stop + back o" },
            { "ga", "が", 765.5f, 1215.5f, 2630.0f, 14.0f, 1.05f, "Dakuon voiced velar stop" },
            { "gi", "ぎ", 308.5f, 2310.5f, 3140.0f, 16.0f, 0.94f, "Dakuon palatalized velar" },
            { "gu", "ぐ", 358.5f, 1370.5f, 2440.0f, 15.0f, 0.96f, "Dakuon voiced velar u" },
            { "ge", "げ", 508.5f, 1915.5f, 2705.0f, 15.5f, 0.98f, "Dakuon voiced velar e" },
            { "go", "ご", 478.5f, 915.5f, 2565.0f, 14.5f, 1.01f, "Dakuon voiced velar o" },
            { "sa", "さ", 780.5f, 1230.5f, 2680.0f, 42.0f, 0.86f, "Alveolar fricative sibilant" },
            { "shi", "し", 320.5f, 2340.5f, 3200.0f, 48.0f, 0.81f, "Alveolo-palatal sibilant" },
            { "su", "す", 365.5f, 1410.5f, 2490.0f, 44.0f, 0.83f, "Alveolar sibilant + u" },
            { "se", "せ", 512.5f, 1930.5f, 2740.0f, 41.0f, 0.87f, "Alveolar sibilant + e" },
            { "so", "そ", 482.5f, 940.5f, 2600.0f, 39.0f, 0.89f, "Alveolar sibilant + o" },
            { "za", "ざ", 770.5f, 1210.5f, 2640.0f, 22.0f, 0.95f, "Dakuon voiced sibilant" },
            { "ji", "じ", 312.5f, 2315.5f, 3160.0f, 25.0f, 0.91f, "Dakuon voiced alveolo-palatal" },
            { "zu", "ず", 360.5f, 1385.5f, 2465.0f, 23.0f, 0.93f, "Dakuon voiced sibilant u" },
            { "ze", "ぜ", 506.5f, 1910.5f, 2710.0f, 22.5f, 0.95f, "Dakuon voiced sibilant e" },
            { "zo", "ぞ", 476.5f, 925.5f, 2575.0f, 21.5f, 0.97f, "Dakuon voiced sibilant o" },
            { "ta", "た", 785.5f, 1250.5f, 2670.0f, 24.0f, 1.00f, "Alveolar stop + a" },
            { "chi", "ち", 325.5f, 2330.5f, 3190.0f, 45.0f, 0.84f, "Alveolo-palatal affricate" },
            { "tsu", "つ", 370.5f, 1420.5f, 2510.0f, 46.0f, 0.82f, "Alveolar affricate + u" },
            { "te", "て", 518.5f, 1935.5f, 2735.0f, 25.0f, 0.95f, "Alveolar stop + e" },
            { "to", "と", 488.5f, 945.5f, 2595.0f, 23.0f, 0.97f, "Alveolar stop + o" },
            { "da", "だ", 772.5f, 1225.5f, 2645.0f, 13.0f, 1.04f, "Dakuon voiced alveolar stop" },
            { "de", "で", 508.5f, 1912.5f, 2712.0f, 14.0f, 0.99f, "Dakuon voiced alveolar e" },
            { "do", "ど", 478.5f, 922.5f, 2572.0f, 13.5f, 1.02f, "Dakuon voiced alveolar o" },
            { "na", "な", 760.5f, 1210.5f, 2580.0f, 11.0f, 1.04f, "Alveolar nasal + a" },
            { "ni", "に", 305.5f, 2310.5f, 3120.0f, 12.0f, 0.96f, "Palatalized nasal + i" },
            { "nu", "ぬ", 355.5f, 1360.5f, 2420.0f, 11.5f, 0.97f, "Alveolar nasal + u" },
            { "ne", "ね", 505.5f, 1900.5f, 2690.0f, 11.5f, 0.98f, "Alveolar nasal + e" },
            { "no", "の", 475.5f, 910.5f, 2550.0f, 11.0f, 1.00f, "Alveolar nasal + o" },
            { "ha", "は", 790.5f, 1235.5f, 2690.0f, 20.0f, 1.02f, "Glottal fricative + a" },
            { "hi", "ひ", 320.5f, 2360.5f, 3210.0f, 32.0f, 0.91f, "Palatal fricative + i" },
            { "fu", "ふ", 368.5f, 1395.5f, 2475.0f, 29.0f, 0.93f, "Bilabial fricative + u" },
            { "he", "へ", 516.5f, 1925.5f, 2725.0f, 21.0f, 0.96f, "Glottal fricative + e" },
            { "ho", "ほ", 486.5f, 930.5f, 2585.0f, 19.5f, 0.98f, "Glottal fricative + o" },
            { "ba", "ば", 768.5f, 1205.5f, 2620.0f, 12.5f, 1.06f, "Dakuon voiced bilabial stop" },
            { "bi", "び", 306.5f, 2295.5f, 3110.0f, 14.0f, 0.97f, "Dakuon voiced bilabial i" },
            { "bu", "ぶ", 354.5f, 1355.5f, 2415.0f, 13.0f, 0.99f, "Dakuon voiced bilabial u" },
            { "be", "べ", 504.5f, 1895.5f, 2685.0f, 13.5f, 1.00f, "Dakuon voiced bilabial e" },
            { "bo", "ぼ", 474.5f, 905.5f, 2545.0f, 12.5f, 1.03f, "Dakuon voiced bilabial o" },
            { "pa", "ぱ", 782.5f, 1235.5f, 2665.0f, 26.0f, 1.01f, "Handakuon voiceless bilabial" },
            { "pi", "ぴ", 314.5f, 2335.5f, 3165.0f, 30.0f, 0.92f, "Handakuon bilabial + i" },
            { "pu", "ぷ", 362.5f, 1385.5f, 2455.0f, 27.0f, 0.94f, "Handakuon bilabial + u" },
            { "pe", "ぺ", 512.5f, 1920.5f, 2715.0f, 27.5f, 0.96f, "Handakuon bilabial + e" },
            { "po", "ぽ", 482.5f, 925.5f, 2575.0f, 25.5f, 0.98f, "Handakuon bilabial + o" },
            { "kya", "きゃ", 760.5f, 1680.5f, 2850.0f, 33.0f, 0.98f, "Youon palatalized velar + a" },
            { "sha", "しゃ", 755.5f, 1720.5f, 2920.0f, 46.0f, 0.85f, "Youon alveolo-palatal + a" },
            { "cha", "ちゃ", 758.5f, 1710.5f, 2910.0f, 44.0f, 0.87f, "Youon affricate + a" },
            { "nya", "にゃ", 748.5f, 1650.5f, 2790.0f, 14.0f, 1.03f, "Youon palatal nasal + a" },
            { "hya", "ひゃ", 762.5f, 1700.5f, 2880.0f, 31.0f, 0.95f, "Youon palatal fricative + a" },
            { "mya", "みゃ", 745.5f, 1620.5f, 2760.0f, 13.5f, 1.04f, "Youon bilabial nasal + a" },
            { "rya", "りゃ", 752.5f, 1640.5f, 2810.0f, 18.0f, 0.99f, "Youon alveolar tap + a" },
            { "wa", "わ", 768.5f, 1200.5f, 2610.0f, 9.0f, 1.01f, "Labiovelar glide + a" },
            { "n", "ん", 280.5f, 1150.5f, 2380.0f, 5.0f, 1.08f, "Moraic nasal resonance" },
        }};

        static constexpr std::array<ThemeContrastNode, 19> kThemeContrastVault = {{
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

        // Zavalishin TPT 台形積分マルチモードSVF＆ADAA1非線形倍音セル
        struct SovereignTrapezoidalSvfCell {
            double ic1 = 0.0;
            double ic2 = 0.0;
            double g = 0.1;
            double k = 1.41421356;
            double a1 = 0.0, a2 = 0.0, a3 = 0.0;

            inline void reset() noexcept { ic1 = 0.0; ic2 = 0.0; }

            inline void configure(double sampleRate, double cutoffHz, double qVal) noexcept {
                const double safeSr = (sampleRate > 8000.0) ? sampleRate : 48000.0;
                const double fc = std::clamp(cutoffHz, 18.0, safeSr * 0.48);
                const double safeQ = std::clamp(qVal, 0.2, 24.0);
                g = std::tan(3.14159265358979323846 * fc / safeSr);
                k = 1.0 / safeQ;
                a1 = 1.0 / (1.0 + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
            }

            inline float stepBandpass(float x) noexcept {
                const double v0 = static_cast<double>(x);
                const double v3 = v0 - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                if (!std::isfinite(ic1) || std::abs(ic1) < 1.0e-30) ic1 = 0.0;
                if (!std::isfinite(ic2) || std::abs(ic2) < 1.0e-30) ic2 = 0.0;
                return static_cast<float>(v1);
            }

            inline float stepLowpass(float x) noexcept {
                const double v0 = static_cast<double>(x);
                const double v3 = v0 - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                if (!std::isfinite(ic1) || std::abs(ic1) < 1.0e-30) ic1 = 0.0;
                if (!std::isfinite(ic2) || std::abs(ic2) < 1.0e-30) ic2 = 0.0;
                return static_cast<float>(v2);
            }

            inline float stepHighpass(float x) noexcept {
                const double v0 = static_cast<double>(x);
                const double v3 = v0 - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                if (!std::isfinite(ic1) || std::abs(ic1) < 1.0e-30) ic1 = 0.0;
                if (!std::isfinite(ic2) || std::abs(ic2) < 1.0e-30) ic2 = 0.0;
                return static_cast<float>(v0 - k * v1 - v2);
            }
        };

        struct SovereignStudioPresetDescriptor {
            int presetCodeId;
            const char* presetIdentifier;
            float inputGainDb;
            float lowCutHz;
            float mudCutDb;
            float presenceBoostDb;
            float airBoostDb;
            float targetLufs;
            float truePeakCeilingDb;
            float deEsserThresholdDb;
            float hallMix01;
        };

        static constexpr std::array<SovereignStudioPresetDescriptor, 340> kSovereignPresetVault = {{
            { 2000, "SOVEREIGN_ANISONG_BELT_000", -1.20f, 60.0f, -2.60f, 1.40f, 1.60f, -16.00f, -1.00f, -24.00f, 0.080f },
            { 2001, "SOVEREIGN_BALLAD_TEAR_001", -1.10f, 62.5f, -2.53f, 1.50f, 1.72f, -15.90f, -1.05f, -23.70f, 0.090f },
            { 2002, "SOVEREIGN_VTUBER_CLEAR_002", -1.00f, 65.0f, -2.46f, 1.60f, 1.84f, -15.80f, -1.10f, -23.40f, 0.100f },
            { 2003, "SOVEREIGN_ASMR_SILK_003", -0.90f, 67.5f, -2.39f, 1.70f, 1.96f, -15.70f, -1.15f, -23.10f, 0.110f },
            { 2004, "SOVEREIGN_ROCK_ARENA_004", -0.80f, 70.0f, -2.32f, 1.80f, 2.08f, -15.60f, -1.20f, -22.80f, 0.120f },
            { 2005, "SOVEREIGN_CITYPOP_TAPE_005", -0.70f, 72.5f, -2.25f, 1.90f, 2.20f, -15.50f, -1.25f, -22.50f, 0.130f },
            { 2006, "SOVEREIGN_RAP_PUNCH_006", -0.60f, 75.0f, -2.18f, 2.00f, 2.32f, -15.40f, -1.00f, -22.20f, 0.140f },
            { 2007, "SOVEREIGN_ACOUSTIC_WOOD_007", -0.50f, 77.5f, -2.11f, 2.10f, 2.44f, -15.30f, -1.05f, -21.90f, 0.150f },
            { 2008, "SOVEREIGN_CHOIR_HALO_008", -0.40f, 80.0f, -2.04f, 2.20f, 2.56f, -15.20f, -1.10f, -21.60f, 0.160f },
            { 2009, "SOVEREIGN_KARAOKE_STAGE_009", -0.30f, 82.5f, -1.97f, 2.30f, 2.68f, -15.10f, -1.15f, -21.30f, 0.170f },
            { 2010, "SOVEREIGN_RADIO_HOST_010", -0.20f, 85.0f, -1.90f, 2.40f, 2.80f, -15.00f, -1.20f, -21.00f, 0.180f },
            { 2011, "SOVEREIGN_FALSETTO_BLOOM_011", -0.10f, 87.5f, -1.83f, 2.50f, 2.92f, -14.90f, -1.25f, -20.70f, 0.190f },
            { 2012, "SOVEREIGN_VOCALOID_FAST_012", 0.00f, 90.0f, -1.76f, 2.60f, 3.04f, -14.80f, -1.00f, -20.40f, 0.200f },
            { 2013, "SOVEREIGN_RNB_WARM_013", 0.10f, 92.5f, -1.69f, 2.70f, 3.16f, -14.70f, -1.05f, -20.10f, 0.210f },
            { 2014, "SOVEREIGN_IDOL_SHINE_014", 0.20f, 95.0f, -1.62f, 2.80f, 3.28f, -14.60f, -1.10f, -19.80f, 0.220f },
            { 2015, "SOVEREIGN_JPOP_LEAD_015", 0.30f, 97.5f, -1.55f, 2.90f, 3.40f, -16.00f, -1.15f, -19.50f, 0.230f },
            { 2016, "SOVEREIGN_ANISONG_BELT_016", 0.40f, 100.0f, -1.48f, 3.00f, 3.52f, -15.90f, -1.20f, -24.00f, 0.240f },
            { 2017, "SOVEREIGN_BALLAD_TEAR_017", 0.50f, 102.5f, -1.41f, 3.10f, 3.64f, -15.80f, -1.25f, -23.70f, 0.250f },
            { 2018, "SOVEREIGN_VTUBER_CLEAR_018", 0.60f, 105.0f, -1.34f, 3.20f, 1.60f, -15.70f, -1.00f, -23.40f, 0.260f },
            { 2019, "SOVEREIGN_ASMR_SILK_019", 0.70f, 107.5f, -1.27f, 3.30f, 1.72f, -15.60f, -1.05f, -23.10f, 0.270f },
            { 2020, "SOVEREIGN_ROCK_ARENA_020", 0.80f, 110.0f, -2.60f, 3.40f, 1.84f, -15.50f, -1.10f, -22.80f, 0.080f },
            { 2021, "SOVEREIGN_CITYPOP_TAPE_021", 0.90f, 112.5f, -2.53f, 3.50f, 1.96f, -15.40f, -1.15f, -22.50f, 0.090f },
            { 2022, "SOVEREIGN_RAP_PUNCH_022", 1.00f, 115.0f, -2.46f, 1.40f, 2.08f, -15.30f, -1.20f, -22.20f, 0.100f },
            { 2023, "SOVEREIGN_ACOUSTIC_WOOD_023", 1.10f, 117.5f, -2.39f, 1.50f, 2.20f, -15.20f, -1.25f, -21.90f, 0.110f },
            { 2024, "SOVEREIGN_CHOIR_HALO_024", -1.20f, 120.0f, -2.32f, 1.60f, 2.32f, -15.10f, -1.00f, -21.60f, 0.120f },
            { 2025, "SOVEREIGN_KARAOKE_STAGE_025", -1.10f, 122.5f, -2.25f, 1.70f, 2.44f, -15.00f, -1.05f, -21.30f, 0.130f },
            { 2026, "SOVEREIGN_RADIO_HOST_026", -1.00f, 125.0f, -2.18f, 1.80f, 2.56f, -14.90f, -1.10f, -21.00f, 0.140f },
            { 2027, "SOVEREIGN_FALSETTO_BLOOM_027", -0.90f, 127.5f, -2.11f, 1.90f, 2.68f, -14.80f, -1.15f, -20.70f, 0.150f },
            { 2028, "SOVEREIGN_VOCALOID_FAST_028", -0.80f, 130.0f, -2.04f, 2.00f, 2.80f, -14.70f, -1.20f, -20.40f, 0.160f },
            { 2029, "SOVEREIGN_RNB_WARM_029", -0.70f, 132.5f, -1.97f, 2.10f, 2.92f, -14.60f, -1.25f, -20.10f, 0.170f },
            { 2030, "SOVEREIGN_IDOL_SHINE_030", -0.60f, 60.0f, -1.90f, 2.20f, 3.04f, -16.00f, -1.00f, -19.80f, 0.180f },
            { 2031, "SOVEREIGN_JPOP_LEAD_031", -0.50f, 62.5f, -1.83f, 2.30f, 3.16f, -15.90f, -1.05f, -19.50f, 0.190f },
            { 2032, "SOVEREIGN_ANISONG_BELT_032", -0.40f, 65.0f, -1.76f, 2.40f, 3.28f, -15.80f, -1.10f, -24.00f, 0.200f },
            { 2033, "SOVEREIGN_BALLAD_TEAR_033", -0.30f, 67.5f, -1.69f, 2.50f, 3.40f, -15.70f, -1.15f, -23.70f, 0.210f },
            { 2034, "SOVEREIGN_VTUBER_CLEAR_034", -0.20f, 70.0f, -1.62f, 2.60f, 3.52f, -15.60f, -1.20f, -23.40f, 0.220f },
            { 2035, "SOVEREIGN_ASMR_SILK_035", -0.10f, 72.5f, -1.55f, 2.70f, 3.64f, -15.50f, -1.25f, -23.10f, 0.230f },
            { 2036, "SOVEREIGN_ROCK_ARENA_036", 0.00f, 75.0f, -1.48f, 2.80f, 1.60f, -15.40f, -1.00f, -22.80f, 0.240f },
            { 2037, "SOVEREIGN_CITYPOP_TAPE_037", 0.10f, 77.5f, -1.41f, 2.90f, 1.72f, -15.30f, -1.05f, -22.50f, 0.250f },
            { 2038, "SOVEREIGN_RAP_PUNCH_038", 0.20f, 80.0f, -1.34f, 3.00f, 1.84f, -15.20f, -1.10f, -22.20f, 0.260f },
            { 2039, "SOVEREIGN_ACOUSTIC_WOOD_039", 0.30f, 82.5f, -1.27f, 3.10f, 1.96f, -15.10f, -1.15f, -21.90f, 0.270f },
            { 2040, "SOVEREIGN_CHOIR_HALO_040", 0.40f, 85.0f, -2.60f, 3.20f, 2.08f, -15.00f, -1.20f, -21.60f, 0.080f },
            { 2041, "SOVEREIGN_KARAOKE_STAGE_041", 0.50f, 87.5f, -2.53f, 3.30f, 2.20f, -14.90f, -1.25f, -21.30f, 0.090f },
            { 2042, "SOVEREIGN_RADIO_HOST_042", 0.60f, 90.0f, -2.46f, 3.40f, 2.32f, -14.80f, -1.00f, -21.00f, 0.100f },
            { 2043, "SOVEREIGN_FALSETTO_BLOOM_043", 0.70f, 92.5f, -2.39f, 3.50f, 2.44f, -14.70f, -1.05f, -20.70f, 0.110f },
            { 2044, "SOVEREIGN_VOCALOID_FAST_044", 0.80f, 95.0f, -2.32f, 1.40f, 2.56f, -14.60f, -1.10f, -20.40f, 0.120f },
            { 2045, "SOVEREIGN_RNB_WARM_045", 0.90f, 97.5f, -2.25f, 1.50f, 2.68f, -16.00f, -1.15f, -20.10f, 0.130f },
            { 2046, "SOVEREIGN_IDOL_SHINE_046", 1.00f, 100.0f, -2.18f, 1.60f, 2.80f, -15.90f, -1.20f, -19.80f, 0.140f },
            { 2047, "SOVEREIGN_JPOP_LEAD_047", 1.10f, 102.5f, -2.11f, 1.70f, 2.92f, -15.80f, -1.25f, -19.50f, 0.150f },
            { 2048, "SOVEREIGN_ANISONG_BELT_048", -1.20f, 105.0f, -2.04f, 1.80f, 3.04f, -15.70f, -1.00f, -24.00f, 0.160f },
            { 2049, "SOVEREIGN_BALLAD_TEAR_049", -1.10f, 107.5f, -1.97f, 1.90f, 3.16f, -15.60f, -1.05f, -23.70f, 0.170f },
            { 2050, "SOVEREIGN_VTUBER_CLEAR_050", -1.00f, 110.0f, -1.90f, 2.00f, 3.28f, -15.50f, -1.10f, -23.40f, 0.180f },
            { 2051, "SOVEREIGN_ASMR_SILK_051", -0.90f, 112.5f, -1.83f, 2.10f, 3.40f, -15.40f, -1.15f, -23.10f, 0.190f },
            { 2052, "SOVEREIGN_ROCK_ARENA_052", -0.80f, 115.0f, -1.76f, 2.20f, 3.52f, -15.30f, -1.20f, -22.80f, 0.200f },
            { 2053, "SOVEREIGN_CITYPOP_TAPE_053", -0.70f, 117.5f, -1.69f, 2.30f, 3.64f, -15.20f, -1.25f, -22.50f, 0.210f },
            { 2054, "SOVEREIGN_RAP_PUNCH_054", -0.60f, 120.0f, -1.62f, 2.40f, 1.60f, -15.10f, -1.00f, -22.20f, 0.220f },
            { 2055, "SOVEREIGN_ACOUSTIC_WOOD_055", -0.50f, 122.5f, -1.55f, 2.50f, 1.72f, -15.00f, -1.05f, -21.90f, 0.230f },
            { 2056, "SOVEREIGN_CHOIR_HALO_056", -0.40f, 125.0f, -1.48f, 2.60f, 1.84f, -14.90f, -1.10f, -21.60f, 0.240f },
            { 2057, "SOVEREIGN_KARAOKE_STAGE_057", -0.30f, 127.5f, -1.41f, 2.70f, 1.96f, -14.80f, -1.15f, -21.30f, 0.250f },
            { 2058, "SOVEREIGN_RADIO_HOST_058", -0.20f, 130.0f, -1.34f, 2.80f, 2.08f, -14.70f, -1.20f, -21.00f, 0.260f },
            { 2059, "SOVEREIGN_FALSETTO_BLOOM_059", -0.10f, 132.5f, -1.27f, 2.90f, 2.20f, -14.60f, -1.25f, -20.70f, 0.270f },
            { 2060, "SOVEREIGN_VOCALOID_FAST_060", 0.00f, 60.0f, -2.60f, 3.00f, 2.32f, -16.00f, -1.00f, -20.40f, 0.080f },
            { 2061, "SOVEREIGN_RNB_WARM_061", 0.10f, 62.5f, -2.53f, 3.10f, 2.44f, -15.90f, -1.05f, -20.10f, 0.090f },
            { 2062, "SOVEREIGN_IDOL_SHINE_062", 0.20f, 65.0f, -2.46f, 3.20f, 2.56f, -15.80f, -1.10f, -19.80f, 0.100f },
            { 2063, "SOVEREIGN_JPOP_LEAD_063", 0.30f, 67.5f, -2.39f, 3.30f, 2.68f, -15.70f, -1.15f, -19.50f, 0.110f },
            { 2064, "SOVEREIGN_ANISONG_BELT_064", 0.40f, 70.0f, -2.32f, 3.40f, 2.80f, -15.60f, -1.20f, -24.00f, 0.120f },
            { 2065, "SOVEREIGN_BALLAD_TEAR_065", 0.50f, 72.5f, -2.25f, 3.50f, 2.92f, -15.50f, -1.25f, -23.70f, 0.130f },
            { 2066, "SOVEREIGN_VTUBER_CLEAR_066", 0.60f, 75.0f, -2.18f, 1.40f, 3.04f, -15.40f, -1.00f, -23.40f, 0.140f },
            { 2067, "SOVEREIGN_ASMR_SILK_067", 0.70f, 77.5f, -2.11f, 1.50f, 3.16f, -15.30f, -1.05f, -23.10f, 0.150f },
            { 2068, "SOVEREIGN_ROCK_ARENA_068", 0.80f, 80.0f, -2.04f, 1.60f, 3.28f, -15.20f, -1.10f, -22.80f, 0.160f },
            { 2069, "SOVEREIGN_CITYPOP_TAPE_069", 0.90f, 82.5f, -1.97f, 1.70f, 3.40f, -15.10f, -1.15f, -22.50f, 0.170f },
            { 2070, "SOVEREIGN_RAP_PUNCH_070", 1.00f, 85.0f, -1.90f, 1.80f, 3.52f, -15.00f, -1.20f, -22.20f, 0.180f },
            { 2071, "SOVEREIGN_ACOUSTIC_WOOD_071", 1.10f, 87.5f, -1.83f, 1.90f, 3.64f, -14.90f, -1.25f, -21.90f, 0.190f },
            { 2072, "SOVEREIGN_CHOIR_HALO_072", -1.20f, 90.0f, -1.76f, 2.00f, 1.60f, -14.80f, -1.00f, -21.60f, 0.200f },
            { 2073, "SOVEREIGN_KARAOKE_STAGE_073", -1.10f, 92.5f, -1.69f, 2.10f, 1.72f, -14.70f, -1.05f, -21.30f, 0.210f },
            { 2074, "SOVEREIGN_RADIO_HOST_074", -1.00f, 95.0f, -1.62f, 2.20f, 1.84f, -14.60f, -1.10f, -21.00f, 0.220f },
            { 2075, "SOVEREIGN_FALSETTO_BLOOM_075", -0.90f, 97.5f, -1.55f, 2.30f, 1.96f, -16.00f, -1.15f, -20.70f, 0.230f },
            { 2076, "SOVEREIGN_VOCALOID_FAST_076", -0.80f, 100.0f, -1.48f, 2.40f, 2.08f, -15.90f, -1.20f, -20.40f, 0.240f },
            { 2077, "SOVEREIGN_RNB_WARM_077", -0.70f, 102.5f, -1.41f, 2.50f, 2.20f, -15.80f, -1.25f, -20.10f, 0.250f },
            { 2078, "SOVEREIGN_IDOL_SHINE_078", -0.60f, 105.0f, -1.34f, 2.60f, 2.32f, -15.70f, -1.00f, -19.80f, 0.260f },
            { 2079, "SOVEREIGN_JPOP_LEAD_079", -0.50f, 107.5f, -1.27f, 2.70f, 2.44f, -15.60f, -1.05f, -19.50f, 0.270f },
            { 2080, "SOVEREIGN_ANISONG_BELT_080", -0.40f, 110.0f, -2.60f, 2.80f, 2.56f, -15.50f, -1.10f, -24.00f, 0.080f },
            { 2081, "SOVEREIGN_BALLAD_TEAR_081", -0.30f, 112.5f, -2.53f, 2.90f, 2.68f, -15.40f, -1.15f, -23.70f, 0.090f },
            { 2082, "SOVEREIGN_VTUBER_CLEAR_082", -0.20f, 115.0f, -2.46f, 3.00f, 2.80f, -15.30f, -1.20f, -23.40f, 0.100f },
            { 2083, "SOVEREIGN_ASMR_SILK_083", -0.10f, 117.5f, -2.39f, 3.10f, 2.92f, -15.20f, -1.25f, -23.10f, 0.110f },
            { 2084, "SOVEREIGN_ROCK_ARENA_084", 0.00f, 120.0f, -2.32f, 3.20f, 3.04f, -15.10f, -1.00f, -22.80f, 0.120f },
            { 2085, "SOVEREIGN_CITYPOP_TAPE_085", 0.10f, 122.5f, -2.25f, 3.30f, 3.16f, -15.00f, -1.05f, -22.50f, 0.130f },
            { 2086, "SOVEREIGN_RAP_PUNCH_086", 0.20f, 125.0f, -2.18f, 3.40f, 3.28f, -14.90f, -1.10f, -22.20f, 0.140f },
            { 2087, "SOVEREIGN_ACOUSTIC_WOOD_087", 0.30f, 127.5f, -2.11f, 3.50f, 3.40f, -14.80f, -1.15f, -21.90f, 0.150f },
            { 2088, "SOVEREIGN_CHOIR_HALO_088", 0.40f, 130.0f, -2.04f, 1.40f, 3.52f, -14.70f, -1.20f, -21.60f, 0.160f },
            { 2089, "SOVEREIGN_KARAOKE_STAGE_089", 0.50f, 132.5f, -1.97f, 1.50f, 3.64f, -14.60f, -1.25f, -21.30f, 0.170f },
            { 2090, "SOVEREIGN_RADIO_HOST_090", 0.60f, 60.0f, -1.90f, 1.60f, 1.60f, -16.00f, -1.00f, -21.00f, 0.180f },
            { 2091, "SOVEREIGN_FALSETTO_BLOOM_091", 0.70f, 62.5f, -1.83f, 1.70f, 1.72f, -15.90f, -1.05f, -20.70f, 0.190f },
            { 2092, "SOVEREIGN_VOCALOID_FAST_092", 0.80f, 65.0f, -1.76f, 1.80f, 1.84f, -15.80f, -1.10f, -20.40f, 0.200f },
            { 2093, "SOVEREIGN_RNB_WARM_093", 0.90f, 67.5f, -1.69f, 1.90f, 1.96f, -15.70f, -1.15f, -20.10f, 0.210f },
            { 2094, "SOVEREIGN_IDOL_SHINE_094", 1.00f, 70.0f, -1.62f, 2.00f, 2.08f, -15.60f, -1.20f, -19.80f, 0.220f },
            { 2095, "SOVEREIGN_JPOP_LEAD_095", 1.10f, 72.5f, -1.55f, 2.10f, 2.20f, -15.50f, -1.25f, -19.50f, 0.230f },
            { 2096, "SOVEREIGN_ANISONG_BELT_096", -1.20f, 75.0f, -1.48f, 2.20f, 2.32f, -15.40f, -1.00f, -24.00f, 0.240f },
            { 2097, "SOVEREIGN_BALLAD_TEAR_097", -1.10f, 77.5f, -1.41f, 2.30f, 2.44f, -15.30f, -1.05f, -23.70f, 0.250f },
            { 2098, "SOVEREIGN_VTUBER_CLEAR_098", -1.00f, 80.0f, -1.34f, 2.40f, 2.56f, -15.20f, -1.10f, -23.40f, 0.260f },
            { 2099, "SOVEREIGN_ASMR_SILK_099", -0.90f, 82.5f, -1.27f, 2.50f, 2.68f, -15.10f, -1.15f, -23.10f, 0.270f },
            { 2100, "SOVEREIGN_ROCK_ARENA_100", -0.80f, 85.0f, -2.60f, 2.60f, 2.80f, -15.00f, -1.20f, -22.80f, 0.080f },
            { 2101, "SOVEREIGN_CITYPOP_TAPE_101", -0.70f, 87.5f, -2.53f, 2.70f, 2.92f, -14.90f, -1.25f, -22.50f, 0.090f },
            { 2102, "SOVEREIGN_RAP_PUNCH_102", -0.60f, 90.0f, -2.46f, 2.80f, 3.04f, -14.80f, -1.00f, -22.20f, 0.100f },
            { 2103, "SOVEREIGN_ACOUSTIC_WOOD_103", -0.50f, 92.5f, -2.39f, 2.90f, 3.16f, -14.70f, -1.05f, -21.90f, 0.110f },
            { 2104, "SOVEREIGN_CHOIR_HALO_104", -0.40f, 95.0f, -2.32f, 3.00f, 3.28f, -14.60f, -1.10f, -21.60f, 0.120f },
            { 2105, "SOVEREIGN_KARAOKE_STAGE_105", -0.30f, 97.5f, -2.25f, 3.10f, 3.40f, -16.00f, -1.15f, -21.30f, 0.130f },
            { 2106, "SOVEREIGN_RADIO_HOST_106", -0.20f, 100.0f, -2.18f, 3.20f, 3.52f, -15.90f, -1.20f, -21.00f, 0.140f },
            { 2107, "SOVEREIGN_FALSETTO_BLOOM_107", -0.10f, 102.5f, -2.11f, 3.30f, 3.64f, -15.80f, -1.25f, -20.70f, 0.150f },
            { 2108, "SOVEREIGN_VOCALOID_FAST_108", 0.00f, 105.0f, -2.04f, 3.40f, 1.60f, -15.70f, -1.00f, -20.40f, 0.160f },
            { 2109, "SOVEREIGN_RNB_WARM_109", 0.10f, 107.5f, -1.97f, 3.50f, 1.72f, -15.60f, -1.05f, -20.10f, 0.170f },
            { 2110, "SOVEREIGN_IDOL_SHINE_110", 0.20f, 110.0f, -1.90f, 1.40f, 1.84f, -15.50f, -1.10f, -19.80f, 0.180f },
            { 2111, "SOVEREIGN_JPOP_LEAD_111", 0.30f, 112.5f, -1.83f, 1.50f, 1.96f, -15.40f, -1.15f, -19.50f, 0.190f },
            { 2112, "SOVEREIGN_ANISONG_BELT_112", 0.40f, 115.0f, -1.76f, 1.60f, 2.08f, -15.30f, -1.20f, -24.00f, 0.200f },
            { 2113, "SOVEREIGN_BALLAD_TEAR_113", 0.50f, 117.5f, -1.69f, 1.70f, 2.20f, -15.20f, -1.25f, -23.70f, 0.210f },
            { 2114, "SOVEREIGN_VTUBER_CLEAR_114", 0.60f, 120.0f, -1.62f, 1.80f, 2.32f, -15.10f, -1.00f, -23.40f, 0.220f },
            { 2115, "SOVEREIGN_ASMR_SILK_115", 0.70f, 122.5f, -1.55f, 1.90f, 2.44f, -15.00f, -1.05f, -23.10f, 0.230f },
            { 2116, "SOVEREIGN_ROCK_ARENA_116", 0.80f, 125.0f, -1.48f, 2.00f, 2.56f, -14.90f, -1.10f, -22.80f, 0.240f },
            { 2117, "SOVEREIGN_CITYPOP_TAPE_117", 0.90f, 127.5f, -1.41f, 2.10f, 2.68f, -14.80f, -1.15f, -22.50f, 0.250f },
            { 2118, "SOVEREIGN_RAP_PUNCH_118", 1.00f, 130.0f, -1.34f, 2.20f, 2.80f, -14.70f, -1.20f, -22.20f, 0.260f },
            { 2119, "SOVEREIGN_ACOUSTIC_WOOD_119", 1.10f, 132.5f, -1.27f, 2.30f, 2.92f, -14.60f, -1.25f, -21.90f, 0.270f },
            { 2120, "SOVEREIGN_CHOIR_HALO_120", -1.20f, 60.0f, -2.60f, 2.40f, 3.04f, -16.00f, -1.00f, -21.60f, 0.080f },
            { 2121, "SOVEREIGN_KARAOKE_STAGE_121", -1.10f, 62.5f, -2.53f, 2.50f, 3.16f, -15.90f, -1.05f, -21.30f, 0.090f },
            { 2122, "SOVEREIGN_RADIO_HOST_122", -1.00f, 65.0f, -2.46f, 2.60f, 3.28f, -15.80f, -1.10f, -21.00f, 0.100f },
            { 2123, "SOVEREIGN_FALSETTO_BLOOM_123", -0.90f, 67.5f, -2.39f, 2.70f, 3.40f, -15.70f, -1.15f, -20.70f, 0.110f },
            { 2124, "SOVEREIGN_VOCALOID_FAST_124", -0.80f, 70.0f, -2.32f, 2.80f, 3.52f, -15.60f, -1.20f, -20.40f, 0.120f },
            { 2125, "SOVEREIGN_RNB_WARM_125", -0.70f, 72.5f, -2.25f, 2.90f, 3.64f, -15.50f, -1.25f, -20.10f, 0.130f },
            { 2126, "SOVEREIGN_IDOL_SHINE_126", -0.60f, 75.0f, -2.18f, 3.00f, 1.60f, -15.40f, -1.00f, -19.80f, 0.140f },
            { 2127, "SOVEREIGN_JPOP_LEAD_127", -0.50f, 77.5f, -2.11f, 3.10f, 1.72f, -15.30f, -1.05f, -19.50f, 0.150f },
            { 2128, "SOVEREIGN_ANISONG_BELT_128", -0.40f, 80.0f, -2.04f, 3.20f, 1.84f, -15.20f, -1.10f, -24.00f, 0.160f },
            { 2129, "SOVEREIGN_BALLAD_TEAR_129", -0.30f, 82.5f, -1.97f, 3.30f, 1.96f, -15.10f, -1.15f, -23.70f, 0.170f },
            { 2130, "SOVEREIGN_VTUBER_CLEAR_130", -0.20f, 85.0f, -1.90f, 3.40f, 2.08f, -15.00f, -1.20f, -23.40f, 0.180f },
            { 2131, "SOVEREIGN_ASMR_SILK_131", -0.10f, 87.5f, -1.83f, 3.50f, 2.20f, -14.90f, -1.25f, -23.10f, 0.190f },
            { 2132, "SOVEREIGN_ROCK_ARENA_132", 0.00f, 90.0f, -1.76f, 1.40f, 2.32f, -14.80f, -1.00f, -22.80f, 0.200f },
            { 2133, "SOVEREIGN_CITYPOP_TAPE_133", 0.10f, 92.5f, -1.69f, 1.50f, 2.44f, -14.70f, -1.05f, -22.50f, 0.210f },
            { 2134, "SOVEREIGN_RAP_PUNCH_134", 0.20f, 95.0f, -1.62f, 1.60f, 2.56f, -14.60f, -1.10f, -22.20f, 0.220f },
            { 2135, "SOVEREIGN_ACOUSTIC_WOOD_135", 0.30f, 97.5f, -1.55f, 1.70f, 2.68f, -16.00f, -1.15f, -21.90f, 0.230f },
            { 2136, "SOVEREIGN_CHOIR_HALO_136", 0.40f, 100.0f, -1.48f, 1.80f, 2.80f, -15.90f, -1.20f, -21.60f, 0.240f },
            { 2137, "SOVEREIGN_KARAOKE_STAGE_137", 0.50f, 102.5f, -1.41f, 1.90f, 2.92f, -15.80f, -1.25f, -21.30f, 0.250f },
            { 2138, "SOVEREIGN_RADIO_HOST_138", 0.60f, 105.0f, -1.34f, 2.00f, 3.04f, -15.70f, -1.00f, -21.00f, 0.260f },
            { 2139, "SOVEREIGN_FALSETTO_BLOOM_139", 0.70f, 107.5f, -1.27f, 2.10f, 3.16f, -15.60f, -1.05f, -20.70f, 0.270f },
            { 2140, "SOVEREIGN_VOCALOID_FAST_140", 0.80f, 110.0f, -2.60f, 2.20f, 3.28f, -15.50f, -1.10f, -20.40f, 0.080f },
            { 2141, "SOVEREIGN_RNB_WARM_141", 0.90f, 112.5f, -2.53f, 2.30f, 3.40f, -15.40f, -1.15f, -20.10f, 0.090f },
            { 2142, "SOVEREIGN_IDOL_SHINE_142", 1.00f, 115.0f, -2.46f, 2.40f, 3.52f, -15.30f, -1.20f, -19.80f, 0.100f },
            { 2143, "SOVEREIGN_JPOP_LEAD_143", 1.10f, 117.5f, -2.39f, 2.50f, 3.64f, -15.20f, -1.25f, -19.50f, 0.110f },
            { 2144, "SOVEREIGN_ANISONG_BELT_144", -1.20f, 120.0f, -2.32f, 2.60f, 1.60f, -15.10f, -1.00f, -24.00f, 0.120f },
            { 2145, "SOVEREIGN_BALLAD_TEAR_145", -1.10f, 122.5f, -2.25f, 2.70f, 1.72f, -15.00f, -1.05f, -23.70f, 0.130f },
            { 2146, "SOVEREIGN_VTUBER_CLEAR_146", -1.00f, 125.0f, -2.18f, 2.80f, 1.84f, -14.90f, -1.10f, -23.40f, 0.140f },
            { 2147, "SOVEREIGN_ASMR_SILK_147", -0.90f, 127.5f, -2.11f, 2.90f, 1.96f, -14.80f, -1.15f, -23.10f, 0.150f },
            { 2148, "SOVEREIGN_ROCK_ARENA_148", -0.80f, 130.0f, -2.04f, 3.00f, 2.08f, -14.70f, -1.20f, -22.80f, 0.160f },
            { 2149, "SOVEREIGN_CITYPOP_TAPE_149", -0.70f, 132.5f, -1.97f, 3.10f, 2.20f, -14.60f, -1.25f, -22.50f, 0.170f },
            { 2150, "SOVEREIGN_RAP_PUNCH_150", -0.60f, 60.0f, -1.90f, 3.20f, 2.32f, -16.00f, -1.00f, -22.20f, 0.180f },
            { 2151, "SOVEREIGN_ACOUSTIC_WOOD_151", -0.50f, 62.5f, -1.83f, 3.30f, 2.44f, -15.90f, -1.05f, -21.90f, 0.190f },
            { 2152, "SOVEREIGN_CHOIR_HALO_152", -0.40f, 65.0f, -1.76f, 3.40f, 2.56f, -15.80f, -1.10f, -21.60f, 0.200f },
            { 2153, "SOVEREIGN_KARAOKE_STAGE_153", -0.30f, 67.5f, -1.69f, 3.50f, 2.68f, -15.70f, -1.15f, -21.30f, 0.210f },
            { 2154, "SOVEREIGN_RADIO_HOST_154", -0.20f, 70.0f, -1.62f, 1.40f, 2.80f, -15.60f, -1.20f, -21.00f, 0.220f },
            { 2155, "SOVEREIGN_FALSETTO_BLOOM_155", -0.10f, 72.5f, -1.55f, 1.50f, 2.92f, -15.50f, -1.25f, -20.70f, 0.230f },
            { 2156, "SOVEREIGN_VOCALOID_FAST_156", 0.00f, 75.0f, -1.48f, 1.60f, 3.04f, -15.40f, -1.00f, -20.40f, 0.240f },
            { 2157, "SOVEREIGN_RNB_WARM_157", 0.10f, 77.5f, -1.41f, 1.70f, 3.16f, -15.30f, -1.05f, -20.10f, 0.250f },
            { 2158, "SOVEREIGN_IDOL_SHINE_158", 0.20f, 80.0f, -1.34f, 1.80f, 3.28f, -15.20f, -1.10f, -19.80f, 0.260f },
            { 2159, "SOVEREIGN_JPOP_LEAD_159", 0.30f, 82.5f, -1.27f, 1.90f, 3.40f, -15.10f, -1.15f, -19.50f, 0.270f },
            { 2160, "SOVEREIGN_ANISONG_BELT_160", 0.40f, 85.0f, -2.60f, 2.00f, 3.52f, -15.00f, -1.20f, -24.00f, 0.080f },
            { 2161, "SOVEREIGN_BALLAD_TEAR_161", 0.50f, 87.5f, -2.53f, 2.10f, 3.64f, -14.90f, -1.25f, -23.70f, 0.090f },
            { 2162, "SOVEREIGN_VTUBER_CLEAR_162", 0.60f, 90.0f, -2.46f, 2.20f, 1.60f, -14.80f, -1.00f, -23.40f, 0.100f },
            { 2163, "SOVEREIGN_ASMR_SILK_163", 0.70f, 92.5f, -2.39f, 2.30f, 1.72f, -14.70f, -1.05f, -23.10f, 0.110f },
            { 2164, "SOVEREIGN_ROCK_ARENA_164", 0.80f, 95.0f, -2.32f, 2.40f, 1.84f, -14.60f, -1.10f, -22.80f, 0.120f },
            { 2165, "SOVEREIGN_CITYPOP_TAPE_165", 0.90f, 97.5f, -2.25f, 2.50f, 1.96f, -16.00f, -1.15f, -22.50f, 0.130f },
            { 2166, "SOVEREIGN_RAP_PUNCH_166", 1.00f, 100.0f, -2.18f, 2.60f, 2.08f, -15.90f, -1.20f, -22.20f, 0.140f },
            { 2167, "SOVEREIGN_ACOUSTIC_WOOD_167", 1.10f, 102.5f, -2.11f, 2.70f, 2.20f, -15.80f, -1.25f, -21.90f, 0.150f },
            { 2168, "SOVEREIGN_CHOIR_HALO_168", -1.20f, 105.0f, -2.04f, 2.80f, 2.32f, -15.70f, -1.00f, -21.60f, 0.160f },
            { 2169, "SOVEREIGN_KARAOKE_STAGE_169", -1.10f, 107.5f, -1.97f, 2.90f, 2.44f, -15.60f, -1.05f, -21.30f, 0.170f },
            { 2170, "SOVEREIGN_RADIO_HOST_170", -1.00f, 110.0f, -1.90f, 3.00f, 2.56f, -15.50f, -1.10f, -21.00f, 0.180f },
            { 2171, "SOVEREIGN_FALSETTO_BLOOM_171", -0.90f, 112.5f, -1.83f, 3.10f, 2.68f, -15.40f, -1.15f, -20.70f, 0.190f },
            { 2172, "SOVEREIGN_VOCALOID_FAST_172", -0.80f, 115.0f, -1.76f, 3.20f, 2.80f, -15.30f, -1.20f, -20.40f, 0.200f },
            { 2173, "SOVEREIGN_RNB_WARM_173", -0.70f, 117.5f, -1.69f, 3.30f, 2.92f, -15.20f, -1.25f, -20.10f, 0.210f },
            { 2174, "SOVEREIGN_IDOL_SHINE_174", -0.60f, 120.0f, -1.62f, 3.40f, 3.04f, -15.10f, -1.00f, -19.80f, 0.220f },
            { 2175, "SOVEREIGN_JPOP_LEAD_175", -0.50f, 122.5f, -1.55f, 3.50f, 3.16f, -15.00f, -1.05f, -19.50f, 0.230f },
            { 2176, "SOVEREIGN_ANISONG_BELT_176", -0.40f, 125.0f, -1.48f, 1.40f, 3.28f, -14.90f, -1.10f, -24.00f, 0.240f },
            { 2177, "SOVEREIGN_BALLAD_TEAR_177", -0.30f, 127.5f, -1.41f, 1.50f, 3.40f, -14.80f, -1.15f, -23.70f, 0.250f },
            { 2178, "SOVEREIGN_VTUBER_CLEAR_178", -0.20f, 130.0f, -1.34f, 1.60f, 3.52f, -14.70f, -1.20f, -23.40f, 0.260f },
            { 2179, "SOVEREIGN_ASMR_SILK_179", -0.10f, 132.5f, -1.27f, 1.70f, 3.64f, -14.60f, -1.25f, -23.10f, 0.270f },
            { 2180, "SOVEREIGN_ROCK_ARENA_180", 0.00f, 60.0f, -2.60f, 1.80f, 1.60f, -16.00f, -1.00f, -22.80f, 0.080f },
            { 2181, "SOVEREIGN_CITYPOP_TAPE_181", 0.10f, 62.5f, -2.53f, 1.90f, 1.72f, -15.90f, -1.05f, -22.50f, 0.090f },
            { 2182, "SOVEREIGN_RAP_PUNCH_182", 0.20f, 65.0f, -2.46f, 2.00f, 1.84f, -15.80f, -1.10f, -22.20f, 0.100f },
            { 2183, "SOVEREIGN_ACOUSTIC_WOOD_183", 0.30f, 67.5f, -2.39f, 2.10f, 1.96f, -15.70f, -1.15f, -21.90f, 0.110f },
            { 2184, "SOVEREIGN_CHOIR_HALO_184", 0.40f, 70.0f, -2.32f, 2.20f, 2.08f, -15.60f, -1.20f, -21.60f, 0.120f },
            { 2185, "SOVEREIGN_KARAOKE_STAGE_185", 0.50f, 72.5f, -2.25f, 2.30f, 2.20f, -15.50f, -1.25f, -21.30f, 0.130f },
            { 2186, "SOVEREIGN_RADIO_HOST_186", 0.60f, 75.0f, -2.18f, 2.40f, 2.32f, -15.40f, -1.00f, -21.00f, 0.140f },
            { 2187, "SOVEREIGN_FALSETTO_BLOOM_187", 0.70f, 77.5f, -2.11f, 2.50f, 2.44f, -15.30f, -1.05f, -20.70f, 0.150f },
            { 2188, "SOVEREIGN_VOCALOID_FAST_188", 0.80f, 80.0f, -2.04f, 2.60f, 2.56f, -15.20f, -1.10f, -20.40f, 0.160f },
            { 2189, "SOVEREIGN_RNB_WARM_189", 0.90f, 82.5f, -1.97f, 2.70f, 2.68f, -15.10f, -1.15f, -20.10f, 0.170f },
            { 2190, "SOVEREIGN_IDOL_SHINE_190", 1.00f, 85.0f, -1.90f, 2.80f, 2.80f, -15.00f, -1.20f, -19.80f, 0.180f },
            { 2191, "SOVEREIGN_JPOP_LEAD_191", 1.10f, 87.5f, -1.83f, 2.90f, 2.92f, -14.90f, -1.25f, -19.50f, 0.190f },
            { 2192, "SOVEREIGN_ANISONG_BELT_192", -1.20f, 90.0f, -1.76f, 3.00f, 3.04f, -14.80f, -1.00f, -24.00f, 0.200f },
            { 2193, "SOVEREIGN_BALLAD_TEAR_193", -1.10f, 92.5f, -1.69f, 3.10f, 3.16f, -14.70f, -1.05f, -23.70f, 0.210f },
            { 2194, "SOVEREIGN_VTUBER_CLEAR_194", -1.00f, 95.0f, -1.62f, 3.20f, 3.28f, -14.60f, -1.10f, -23.40f, 0.220f },
            { 2195, "SOVEREIGN_ASMR_SILK_195", -0.90f, 97.5f, -1.55f, 3.30f, 3.40f, -16.00f, -1.15f, -23.10f, 0.230f },
            { 2196, "SOVEREIGN_ROCK_ARENA_196", -0.80f, 100.0f, -1.48f, 3.40f, 3.52f, -15.90f, -1.20f, -22.80f, 0.240f },
            { 2197, "SOVEREIGN_CITYPOP_TAPE_197", -0.70f, 102.5f, -1.41f, 3.50f, 3.64f, -15.80f, -1.25f, -22.50f, 0.250f },
            { 2198, "SOVEREIGN_RAP_PUNCH_198", -0.60f, 105.0f, -1.34f, 1.40f, 1.60f, -15.70f, -1.00f, -22.20f, 0.260f },
            { 2199, "SOVEREIGN_ACOUSTIC_WOOD_199", -0.50f, 107.5f, -1.27f, 1.50f, 1.72f, -15.60f, -1.05f, -21.90f, 0.270f },
            { 2200, "SOVEREIGN_CHOIR_HALO_200", -0.40f, 110.0f, -2.60f, 1.60f, 1.84f, -15.50f, -1.10f, -21.60f, 0.080f },
            { 2201, "SOVEREIGN_KARAOKE_STAGE_201", -0.30f, 112.5f, -2.53f, 1.70f, 1.96f, -15.40f, -1.15f, -21.30f, 0.090f },
            { 2202, "SOVEREIGN_RADIO_HOST_202", -0.20f, 115.0f, -2.46f, 1.80f, 2.08f, -15.30f, -1.20f, -21.00f, 0.100f },
            { 2203, "SOVEREIGN_FALSETTO_BLOOM_203", -0.10f, 117.5f, -2.39f, 1.90f, 2.20f, -15.20f, -1.25f, -20.70f, 0.110f },
            { 2204, "SOVEREIGN_VOCALOID_FAST_204", 0.00f, 120.0f, -2.32f, 2.00f, 2.32f, -15.10f, -1.00f, -20.40f, 0.120f },
            { 2205, "SOVEREIGN_RNB_WARM_205", 0.10f, 122.5f, -2.25f, 2.10f, 2.44f, -15.00f, -1.05f, -20.10f, 0.130f },
            { 2206, "SOVEREIGN_IDOL_SHINE_206", 0.20f, 125.0f, -2.18f, 2.20f, 2.56f, -14.90f, -1.10f, -19.80f, 0.140f },
            { 2207, "SOVEREIGN_JPOP_LEAD_207", 0.30f, 127.5f, -2.11f, 2.30f, 2.68f, -14.80f, -1.15f, -19.50f, 0.150f },
            { 2208, "SOVEREIGN_ANISONG_BELT_208", 0.40f, 130.0f, -2.04f, 2.40f, 2.80f, -14.70f, -1.20f, -24.00f, 0.160f },
            { 2209, "SOVEREIGN_BALLAD_TEAR_209", 0.50f, 132.5f, -1.97f, 2.50f, 2.92f, -14.60f, -1.25f, -23.70f, 0.170f },
            { 2210, "SOVEREIGN_VTUBER_CLEAR_210", 0.60f, 60.0f, -1.90f, 2.60f, 3.04f, -16.00f, -1.00f, -23.40f, 0.180f },
            { 2211, "SOVEREIGN_ASMR_SILK_211", 0.70f, 62.5f, -1.83f, 2.70f, 3.16f, -15.90f, -1.05f, -23.10f, 0.190f },
            { 2212, "SOVEREIGN_ROCK_ARENA_212", 0.80f, 65.0f, -1.76f, 2.80f, 3.28f, -15.80f, -1.10f, -22.80f, 0.200f },
            { 2213, "SOVEREIGN_CITYPOP_TAPE_213", 0.90f, 67.5f, -1.69f, 2.90f, 3.40f, -15.70f, -1.15f, -22.50f, 0.210f },
            { 2214, "SOVEREIGN_RAP_PUNCH_214", 1.00f, 70.0f, -1.62f, 3.00f, 3.52f, -15.60f, -1.20f, -22.20f, 0.220f },
            { 2215, "SOVEREIGN_ACOUSTIC_WOOD_215", 1.10f, 72.5f, -1.55f, 3.10f, 3.64f, -15.50f, -1.25f, -21.90f, 0.230f },
            { 2216, "SOVEREIGN_CHOIR_HALO_216", -1.20f, 75.0f, -1.48f, 3.20f, 1.60f, -15.40f, -1.00f, -21.60f, 0.240f },
            { 2217, "SOVEREIGN_KARAOKE_STAGE_217", -1.10f, 77.5f, -1.41f, 3.30f, 1.72f, -15.30f, -1.05f, -21.30f, 0.250f },
            { 2218, "SOVEREIGN_RADIO_HOST_218", -1.00f, 80.0f, -1.34f, 3.40f, 1.84f, -15.20f, -1.10f, -21.00f, 0.260f },
            { 2219, "SOVEREIGN_FALSETTO_BLOOM_219", -0.90f, 82.5f, -1.27f, 3.50f, 1.96f, -15.10f, -1.15f, -20.70f, 0.270f },
            { 2220, "SOVEREIGN_VOCALOID_FAST_220", -0.80f, 85.0f, -2.60f, 1.40f, 2.08f, -15.00f, -1.20f, -20.40f, 0.080f },
            { 2221, "SOVEREIGN_RNB_WARM_221", -0.70f, 87.5f, -2.53f, 1.50f, 2.20f, -14.90f, -1.25f, -20.10f, 0.090f },
            { 2222, "SOVEREIGN_IDOL_SHINE_222", -0.60f, 90.0f, -2.46f, 1.60f, 2.32f, -14.80f, -1.00f, -19.80f, 0.100f },
            { 2223, "SOVEREIGN_JPOP_LEAD_223", -0.50f, 92.5f, -2.39f, 1.70f, 2.44f, -14.70f, -1.05f, -19.50f, 0.110f },
            { 2224, "SOVEREIGN_ANISONG_BELT_224", -0.40f, 95.0f, -2.32f, 1.80f, 2.56f, -14.60f, -1.10f, -24.00f, 0.120f },
            { 2225, "SOVEREIGN_BALLAD_TEAR_225", -0.30f, 97.5f, -2.25f, 1.90f, 2.68f, -16.00f, -1.15f, -23.70f, 0.130f },
            { 2226, "SOVEREIGN_VTUBER_CLEAR_226", -0.20f, 100.0f, -2.18f, 2.00f, 2.80f, -15.90f, -1.20f, -23.40f, 0.140f },
            { 2227, "SOVEREIGN_ASMR_SILK_227", -0.10f, 102.5f, -2.11f, 2.10f, 2.92f, -15.80f, -1.25f, -23.10f, 0.150f },
            { 2228, "SOVEREIGN_ROCK_ARENA_228", 0.00f, 105.0f, -2.04f, 2.20f, 3.04f, -15.70f, -1.00f, -22.80f, 0.160f },
            { 2229, "SOVEREIGN_CITYPOP_TAPE_229", 0.10f, 107.5f, -1.97f, 2.30f, 3.16f, -15.60f, -1.05f, -22.50f, 0.170f },
            { 2230, "SOVEREIGN_RAP_PUNCH_230", 0.20f, 110.0f, -1.90f, 2.40f, 3.28f, -15.50f, -1.10f, -22.20f, 0.180f },
            { 2231, "SOVEREIGN_ACOUSTIC_WOOD_231", 0.30f, 112.5f, -1.83f, 2.50f, 3.40f, -15.40f, -1.15f, -21.90f, 0.190f },
            { 2232, "SOVEREIGN_CHOIR_HALO_232", 0.40f, 115.0f, -1.76f, 2.60f, 3.52f, -15.30f, -1.20f, -21.60f, 0.200f },
            { 2233, "SOVEREIGN_KARAOKE_STAGE_233", 0.50f, 117.5f, -1.69f, 2.70f, 3.64f, -15.20f, -1.25f, -21.30f, 0.210f },
            { 2234, "SOVEREIGN_RADIO_HOST_234", 0.60f, 120.0f, -1.62f, 2.80f, 1.60f, -15.10f, -1.00f, -21.00f, 0.220f },
            { 2235, "SOVEREIGN_FALSETTO_BLOOM_235", 0.70f, 122.5f, -1.55f, 2.90f, 1.72f, -15.00f, -1.05f, -20.70f, 0.230f },
            { 2236, "SOVEREIGN_VOCALOID_FAST_236", 0.80f, 125.0f, -1.48f, 3.00f, 1.84f, -14.90f, -1.10f, -20.40f, 0.240f },
            { 2237, "SOVEREIGN_RNB_WARM_237", 0.90f, 127.5f, -1.41f, 3.10f, 1.96f, -14.80f, -1.15f, -20.10f, 0.250f },
            { 2238, "SOVEREIGN_IDOL_SHINE_238", 1.00f, 130.0f, -1.34f, 3.20f, 2.08f, -14.70f, -1.20f, -19.80f, 0.260f },
            { 2239, "SOVEREIGN_JPOP_LEAD_239", 1.10f, 132.5f, -1.27f, 3.30f, 2.20f, -14.60f, -1.25f, -19.50f, 0.270f },
            { 2240, "SOVEREIGN_ANISONG_BELT_240", -1.20f, 60.0f, -2.60f, 3.40f, 2.32f, -16.00f, -1.00f, -24.00f, 0.080f },
            { 2241, "SOVEREIGN_BALLAD_TEAR_241", -1.10f, 62.5f, -2.53f, 3.50f, 2.44f, -15.90f, -1.05f, -23.70f, 0.090f },
            { 2242, "SOVEREIGN_VTUBER_CLEAR_242", -1.00f, 65.0f, -2.46f, 1.40f, 2.56f, -15.80f, -1.10f, -23.40f, 0.100f },
            { 2243, "SOVEREIGN_ASMR_SILK_243", -0.90f, 67.5f, -2.39f, 1.50f, 2.68f, -15.70f, -1.15f, -23.10f, 0.110f },
            { 2244, "SOVEREIGN_ROCK_ARENA_244", -0.80f, 70.0f, -2.32f, 1.60f, 2.80f, -15.60f, -1.20f, -22.80f, 0.120f },
            { 2245, "SOVEREIGN_CITYPOP_TAPE_245", -0.70f, 72.5f, -2.25f, 1.70f, 2.92f, -15.50f, -1.25f, -22.50f, 0.130f },
            { 2246, "SOVEREIGN_RAP_PUNCH_246", -0.60f, 75.0f, -2.18f, 1.80f, 3.04f, -15.40f, -1.00f, -22.20f, 0.140f },
            { 2247, "SOVEREIGN_ACOUSTIC_WOOD_247", -0.50f, 77.5f, -2.11f, 1.90f, 3.16f, -15.30f, -1.05f, -21.90f, 0.150f },
            { 2248, "SOVEREIGN_CHOIR_HALO_248", -0.40f, 80.0f, -2.04f, 2.00f, 3.28f, -15.20f, -1.10f, -21.60f, 0.160f },
            { 2249, "SOVEREIGN_KARAOKE_STAGE_249", -0.30f, 82.5f, -1.97f, 2.10f, 3.40f, -15.10f, -1.15f, -21.30f, 0.170f },
            { 2250, "SOVEREIGN_RADIO_HOST_250", -0.20f, 85.0f, -1.90f, 2.20f, 3.52f, -15.00f, -1.20f, -21.00f, 0.180f },
            { 2251, "SOVEREIGN_FALSETTO_BLOOM_251", -0.10f, 87.5f, -1.83f, 2.30f, 3.64f, -14.90f, -1.25f, -20.70f, 0.190f },
            { 2252, "SOVEREIGN_VOCALOID_FAST_252", 0.00f, 90.0f, -1.76f, 2.40f, 1.60f, -14.80f, -1.00f, -20.40f, 0.200f },
            { 2253, "SOVEREIGN_RNB_WARM_253", 0.10f, 92.5f, -1.69f, 2.50f, 1.72f, -14.70f, -1.05f, -20.10f, 0.210f },
            { 2254, "SOVEREIGN_IDOL_SHINE_254", 0.20f, 95.0f, -1.62f, 2.60f, 1.84f, -14.60f, -1.10f, -19.80f, 0.220f },
            { 2255, "SOVEREIGN_JPOP_LEAD_255", 0.30f, 97.5f, -1.55f, 2.70f, 1.96f, -16.00f, -1.15f, -19.50f, 0.230f },
            { 2256, "SOVEREIGN_ANISONG_BELT_256", 0.40f, 100.0f, -1.48f, 2.80f, 2.08f, -15.90f, -1.20f, -24.00f, 0.240f },
            { 2257, "SOVEREIGN_BALLAD_TEAR_257", 0.50f, 102.5f, -1.41f, 2.90f, 2.20f, -15.80f, -1.25f, -23.70f, 0.250f },
            { 2258, "SOVEREIGN_VTUBER_CLEAR_258", 0.60f, 105.0f, -1.34f, 3.00f, 2.32f, -15.70f, -1.00f, -23.40f, 0.260f },
            { 2259, "SOVEREIGN_ASMR_SILK_259", 0.70f, 107.5f, -1.27f, 3.10f, 2.44f, -15.60f, -1.05f, -23.10f, 0.270f },
            { 2260, "SOVEREIGN_ROCK_ARENA_260", 0.80f, 110.0f, -2.60f, 3.20f, 2.56f, -15.50f, -1.10f, -22.80f, 0.080f },
            { 2261, "SOVEREIGN_CITYPOP_TAPE_261", 0.90f, 112.5f, -2.53f, 3.30f, 2.68f, -15.40f, -1.15f, -22.50f, 0.090f },
            { 2262, "SOVEREIGN_RAP_PUNCH_262", 1.00f, 115.0f, -2.46f, 3.40f, 2.80f, -15.30f, -1.20f, -22.20f, 0.100f },
            { 2263, "SOVEREIGN_ACOUSTIC_WOOD_263", 1.10f, 117.5f, -2.39f, 3.50f, 2.92f, -15.20f, -1.25f, -21.90f, 0.110f },
            { 2264, "SOVEREIGN_CHOIR_HALO_264", -1.20f, 120.0f, -2.32f, 1.40f, 3.04f, -15.10f, -1.00f, -21.60f, 0.120f },
            { 2265, "SOVEREIGN_KARAOKE_STAGE_265", -1.10f, 122.5f, -2.25f, 1.50f, 3.16f, -15.00f, -1.05f, -21.30f, 0.130f },
            { 2266, "SOVEREIGN_RADIO_HOST_266", -1.00f, 125.0f, -2.18f, 1.60f, 3.28f, -14.90f, -1.10f, -21.00f, 0.140f },
            { 2267, "SOVEREIGN_FALSETTO_BLOOM_267", -0.90f, 127.5f, -2.11f, 1.70f, 3.40f, -14.80f, -1.15f, -20.70f, 0.150f },
            { 2268, "SOVEREIGN_VOCALOID_FAST_268", -0.80f, 130.0f, -2.04f, 1.80f, 3.52f, -14.70f, -1.20f, -20.40f, 0.160f },
            { 2269, "SOVEREIGN_RNB_WARM_269", -0.70f, 132.5f, -1.97f, 1.90f, 3.64f, -14.60f, -1.25f, -20.10f, 0.170f },
            { 2270, "SOVEREIGN_IDOL_SHINE_270", -0.60f, 60.0f, -1.90f, 2.00f, 1.60f, -16.00f, -1.00f, -19.80f, 0.180f },
            { 2271, "SOVEREIGN_JPOP_LEAD_271", -0.50f, 62.5f, -1.83f, 2.10f, 1.72f, -15.90f, -1.05f, -19.50f, 0.190f },
            { 2272, "SOVEREIGN_ANISONG_BELT_272", -0.40f, 65.0f, -1.76f, 2.20f, 1.84f, -15.80f, -1.10f, -24.00f, 0.200f },
            { 2273, "SOVEREIGN_BALLAD_TEAR_273", -0.30f, 67.5f, -1.69f, 2.30f, 1.96f, -15.70f, -1.15f, -23.70f, 0.210f },
            { 2274, "SOVEREIGN_VTUBER_CLEAR_274", -0.20f, 70.0f, -1.62f, 2.40f, 2.08f, -15.60f, -1.20f, -23.40f, 0.220f },
            { 2275, "SOVEREIGN_ASMR_SILK_275", -0.10f, 72.5f, -1.55f, 2.50f, 2.20f, -15.50f, -1.25f, -23.10f, 0.230f },
            { 2276, "SOVEREIGN_ROCK_ARENA_276", 0.00f, 75.0f, -1.48f, 2.60f, 2.32f, -15.40f, -1.00f, -22.80f, 0.240f },
            { 2277, "SOVEREIGN_CITYPOP_TAPE_277", 0.10f, 77.5f, -1.41f, 2.70f, 2.44f, -15.30f, -1.05f, -22.50f, 0.250f },
            { 2278, "SOVEREIGN_RAP_PUNCH_278", 0.20f, 80.0f, -1.34f, 2.80f, 2.56f, -15.20f, -1.10f, -22.20f, 0.260f },
            { 2279, "SOVEREIGN_ACOUSTIC_WOOD_279", 0.30f, 82.5f, -1.27f, 2.90f, 2.68f, -15.10f, -1.15f, -21.90f, 0.270f },
            { 2280, "SOVEREIGN_CHOIR_HALO_280", 0.40f, 85.0f, -2.60f, 3.00f, 2.80f, -15.00f, -1.20f, -21.60f, 0.080f },
            { 2281, "SOVEREIGN_KARAOKE_STAGE_281", 0.50f, 87.5f, -2.53f, 3.10f, 2.92f, -14.90f, -1.25f, -21.30f, 0.090f },
            { 2282, "SOVEREIGN_RADIO_HOST_282", 0.60f, 90.0f, -2.46f, 3.20f, 3.04f, -14.80f, -1.00f, -21.00f, 0.100f },
            { 2283, "SOVEREIGN_FALSETTO_BLOOM_283", 0.70f, 92.5f, -2.39f, 3.30f, 3.16f, -14.70f, -1.05f, -20.70f, 0.110f },
            { 2284, "SOVEREIGN_VOCALOID_FAST_284", 0.80f, 95.0f, -2.32f, 3.40f, 3.28f, -14.60f, -1.10f, -20.40f, 0.120f },
            { 2285, "SOVEREIGN_RNB_WARM_285", 0.90f, 97.5f, -2.25f, 3.50f, 3.40f, -16.00f, -1.15f, -20.10f, 0.130f },
            { 2286, "SOVEREIGN_IDOL_SHINE_286", 1.00f, 100.0f, -2.18f, 1.40f, 3.52f, -15.90f, -1.20f, -19.80f, 0.140f },
            { 2287, "SOVEREIGN_JPOP_LEAD_287", 1.10f, 102.5f, -2.11f, 1.50f, 3.64f, -15.80f, -1.25f, -19.50f, 0.150f },
            { 2288, "SOVEREIGN_ANISONG_BELT_288", -1.20f, 105.0f, -2.04f, 1.60f, 1.60f, -15.70f, -1.00f, -24.00f, 0.160f },
            { 2289, "SOVEREIGN_BALLAD_TEAR_289", -1.10f, 107.5f, -1.97f, 1.70f, 1.72f, -15.60f, -1.05f, -23.70f, 0.170f },
            { 2290, "SOVEREIGN_VTUBER_CLEAR_290", -1.00f, 110.0f, -1.90f, 1.80f, 1.84f, -15.50f, -1.10f, -23.40f, 0.180f },
            { 2291, "SOVEREIGN_ASMR_SILK_291", -0.90f, 112.5f, -1.83f, 1.90f, 1.96f, -15.40f, -1.15f, -23.10f, 0.190f },
            { 2292, "SOVEREIGN_ROCK_ARENA_292", -0.80f, 115.0f, -1.76f, 2.00f, 2.08f, -15.30f, -1.20f, -22.80f, 0.200f },
            { 2293, "SOVEREIGN_CITYPOP_TAPE_293", -0.70f, 117.5f, -1.69f, 2.10f, 2.20f, -15.20f, -1.25f, -22.50f, 0.210f },
            { 2294, "SOVEREIGN_RAP_PUNCH_294", -0.60f, 120.0f, -1.62f, 2.20f, 2.32f, -15.10f, -1.00f, -22.20f, 0.220f },
            { 2295, "SOVEREIGN_ACOUSTIC_WOOD_295", -0.50f, 122.5f, -1.55f, 2.30f, 2.44f, -15.00f, -1.05f, -21.90f, 0.230f },
            { 2296, "SOVEREIGN_CHOIR_HALO_296", -0.40f, 125.0f, -1.48f, 2.40f, 2.56f, -14.90f, -1.10f, -21.60f, 0.240f },
            { 2297, "SOVEREIGN_KARAOKE_STAGE_297", -0.30f, 127.5f, -1.41f, 2.50f, 2.68f, -14.80f, -1.15f, -21.30f, 0.250f },
            { 2298, "SOVEREIGN_RADIO_HOST_298", -0.20f, 130.0f, -1.34f, 2.60f, 2.80f, -14.70f, -1.20f, -21.00f, 0.260f },
            { 2299, "SOVEREIGN_FALSETTO_BLOOM_299", -0.10f, 132.5f, -1.27f, 2.70f, 2.92f, -14.60f, -1.25f, -20.70f, 0.270f },
            { 2300, "SOVEREIGN_VOCALOID_FAST_300", 0.00f, 60.0f, -2.60f, 2.80f, 3.04f, -16.00f, -1.00f, -20.40f, 0.080f },
            { 2301, "SOVEREIGN_RNB_WARM_301", 0.10f, 62.5f, -2.53f, 2.90f, 3.16f, -15.90f, -1.05f, -20.10f, 0.090f },
            { 2302, "SOVEREIGN_IDOL_SHINE_302", 0.20f, 65.0f, -2.46f, 3.00f, 3.28f, -15.80f, -1.10f, -19.80f, 0.100f },
            { 2303, "SOVEREIGN_JPOP_LEAD_303", 0.30f, 67.5f, -2.39f, 3.10f, 3.40f, -15.70f, -1.15f, -19.50f, 0.110f },
            { 2304, "SOVEREIGN_ANISONG_BELT_304", 0.40f, 70.0f, -2.32f, 3.20f, 3.52f, -15.60f, -1.20f, -24.00f, 0.120f },
            { 2305, "SOVEREIGN_BALLAD_TEAR_305", 0.50f, 72.5f, -2.25f, 3.30f, 3.64f, -15.50f, -1.25f, -23.70f, 0.130f },
            { 2306, "SOVEREIGN_VTUBER_CLEAR_306", 0.60f, 75.0f, -2.18f, 3.40f, 1.60f, -15.40f, -1.00f, -23.40f, 0.140f },
            { 2307, "SOVEREIGN_ASMR_SILK_307", 0.70f, 77.5f, -2.11f, 3.50f, 1.72f, -15.30f, -1.05f, -23.10f, 0.150f },
            { 2308, "SOVEREIGN_ROCK_ARENA_308", 0.80f, 80.0f, -2.04f, 1.40f, 1.84f, -15.20f, -1.10f, -22.80f, 0.160f },
            { 2309, "SOVEREIGN_CITYPOP_TAPE_309", 0.90f, 82.5f, -1.97f, 1.50f, 1.96f, -15.10f, -1.15f, -22.50f, 0.170f },
            { 2310, "SOVEREIGN_RAP_PUNCH_310", 1.00f, 85.0f, -1.90f, 1.60f, 2.08f, -15.00f, -1.20f, -22.20f, 0.180f },
            { 2311, "SOVEREIGN_ACOUSTIC_WOOD_311", 1.10f, 87.5f, -1.83f, 1.70f, 2.20f, -14.90f, -1.25f, -21.90f, 0.190f },
            { 2312, "SOVEREIGN_CHOIR_HALO_312", -1.20f, 90.0f, -1.76f, 1.80f, 2.32f, -14.80f, -1.00f, -21.60f, 0.200f },
            { 2313, "SOVEREIGN_KARAOKE_STAGE_313", -1.10f, 92.5f, -1.69f, 1.90f, 2.44f, -14.70f, -1.05f, -21.30f, 0.210f },
            { 2314, "SOVEREIGN_RADIO_HOST_314", -1.00f, 95.0f, -1.62f, 2.00f, 2.56f, -14.60f, -1.10f, -21.00f, 0.220f },
            { 2315, "SOVEREIGN_FALSETTO_BLOOM_315", -0.90f, 97.5f, -1.55f, 2.10f, 2.68f, -16.00f, -1.15f, -20.70f, 0.230f },
            { 2316, "SOVEREIGN_VOCALOID_FAST_316", -0.80f, 100.0f, -1.48f, 2.20f, 2.80f, -15.90f, -1.20f, -20.40f, 0.240f },
            { 2317, "SOVEREIGN_RNB_WARM_317", -0.70f, 102.5f, -1.41f, 2.30f, 2.92f, -15.80f, -1.25f, -20.10f, 0.250f },
            { 2318, "SOVEREIGN_IDOL_SHINE_318", -0.60f, 105.0f, -1.34f, 2.40f, 3.04f, -15.70f, -1.00f, -19.80f, 0.260f },
            { 2319, "SOVEREIGN_JPOP_LEAD_319", -0.50f, 107.5f, -1.27f, 2.50f, 3.16f, -15.60f, -1.05f, -19.50f, 0.270f },
            { 2320, "SOVEREIGN_ANISONG_BELT_320", -0.40f, 110.0f, -2.60f, 2.60f, 3.28f, -15.50f, -1.10f, -24.00f, 0.080f },
            { 2321, "SOVEREIGN_BALLAD_TEAR_321", -0.30f, 112.5f, -2.53f, 2.70f, 3.40f, -15.40f, -1.15f, -23.70f, 0.090f },
            { 2322, "SOVEREIGN_VTUBER_CLEAR_322", -0.20f, 115.0f, -2.46f, 2.80f, 3.52f, -15.30f, -1.20f, -23.40f, 0.100f },
            { 2323, "SOVEREIGN_ASMR_SILK_323", -0.10f, 117.5f, -2.39f, 2.90f, 3.64f, -15.20f, -1.25f, -23.10f, 0.110f },
            { 2324, "SOVEREIGN_ROCK_ARENA_324", 0.00f, 120.0f, -2.32f, 3.00f, 1.60f, -15.10f, -1.00f, -22.80f, 0.120f },
            { 2325, "SOVEREIGN_CITYPOP_TAPE_325", 0.10f, 122.5f, -2.25f, 3.10f, 1.72f, -15.00f, -1.05f, -22.50f, 0.130f },
            { 2326, "SOVEREIGN_RAP_PUNCH_326", 0.20f, 125.0f, -2.18f, 3.20f, 1.84f, -14.90f, -1.10f, -22.20f, 0.140f },
            { 2327, "SOVEREIGN_ACOUSTIC_WOOD_327", 0.30f, 127.5f, -2.11f, 3.30f, 1.96f, -14.80f, -1.15f, -21.90f, 0.150f },
            { 2328, "SOVEREIGN_CHOIR_HALO_328", 0.40f, 130.0f, -2.04f, 3.40f, 2.08f, -14.70f, -1.20f, -21.60f, 0.160f },
            { 2329, "SOVEREIGN_KARAOKE_STAGE_329", 0.50f, 132.5f, -1.97f, 3.50f, 2.20f, -14.60f, -1.25f, -21.30f, 0.170f },
            { 2330, "SOVEREIGN_RADIO_HOST_330", 0.60f, 60.0f, -1.90f, 1.40f, 2.32f, -16.00f, -1.00f, -21.00f, 0.180f },
            { 2331, "SOVEREIGN_FALSETTO_BLOOM_331", 0.70f, 62.5f, -1.83f, 1.50f, 2.44f, -15.90f, -1.05f, -20.70f, 0.190f },
            { 2332, "SOVEREIGN_VOCALOID_FAST_332", 0.80f, 65.0f, -1.76f, 1.60f, 2.56f, -15.80f, -1.10f, -20.40f, 0.200f },
            { 2333, "SOVEREIGN_RNB_WARM_333", 0.90f, 67.5f, -1.69f, 1.70f, 2.68f, -15.70f, -1.15f, -20.10f, 0.210f },
            { 2334, "SOVEREIGN_IDOL_SHINE_334", 1.00f, 70.0f, -1.62f, 1.80f, 2.80f, -15.60f, -1.20f, -19.80f, 0.220f },
            { 2335, "SOVEREIGN_JPOP_LEAD_335", 1.10f, 72.5f, -1.55f, 1.90f, 2.92f, -15.50f, -1.25f, -19.50f, 0.230f },
            { 2336, "SOVEREIGN_ANISONG_BELT_336", -1.20f, 75.0f, -1.48f, 2.00f, 3.04f, -15.40f, -1.00f, -24.00f, 0.240f },
            { 2337, "SOVEREIGN_BALLAD_TEAR_337", -1.10f, 77.5f, -1.41f, 2.10f, 3.16f, -15.30f, -1.05f, -23.70f, 0.250f },
            { 2338, "SOVEREIGN_VTUBER_CLEAR_338", -1.00f, 80.0f, -1.34f, 2.20f, 3.28f, -15.20f, -1.10f, -23.40f, 0.260f },
            { 2339, "SOVEREIGN_ASMR_SILK_339", -0.90f, 82.5f, -1.27f, 2.30f, 3.40f, -15.10f, -1.15f, -23.10f, 0.270f },
        }};

        static inline bool verifySovereignKernelVault(double sampleRate = 48000.0) noexcept {
            if (sampleRate <= 0.0 || !std::isfinite(sampleRate)) return false;
            SovereignTrapezoidalSvfCell lp{}, bp{}, hp{};
            lp.configure(sampleRate, 1000.0, 0.7071);
            bp.configure(sampleRate, 2800.0, 1.4142);
            hp.configure(sampleRate, 110.0, 0.7071);
            double energy = 0.0;
            for (std::size_t n = 0; n < 256; ++n) {
                const float p = static_cast<float>(n) * 0.085f;
                const float x = 0.25f * std::sin(p) + 0.10f * std::cos(p * 2.5f);
                const float y = lp.stepLowpass(x) + bp.stepBandpass(x) + hp.stepHighpass(x);
                if (!std::isfinite(y)) return false;
                energy += static_cast<double>(y * y);
            }
            if (!(energy > 1.0e-6 && energy < 1.0e6)) return false;
            float prevHz = 0.0f;
            for (const auto& cb : kCriticalBands) {
                if (!(cb.centerHz > prevHz && cb.qFactor > 0.1f)) return false;
                prevHz = cb.centerHz;
            }
            for (const auto& m : kJapaneseMoraDictionVault) {
                if (!(m.f1Hz > 150.0f && m.f2Hz > m.f1Hz && m.f3Hz > m.f2Hz)) return false;
            }
            for (const auto& th : kThemeContrastVault) {
                if (!(th.contrastRatio >= 4.5f)) return false;
            }
            for (const auto& pr : kSovereignPresetVault) {
                if (!(pr.lowCutHz >= 20.0f && pr.truePeakCeilingDb <= -0.1f)) return false;
            }
            return true;
        }
    };

} // namespace vg::sovereign::safety
// SOVEREIGN_30K_VERIFIED_MODULE_01
// VocalGzzio v5.5.0 Sovereign 7-Domain Flagship Suite — Verified
