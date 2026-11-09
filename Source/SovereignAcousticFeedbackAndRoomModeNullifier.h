#pragma once
// ============================================================================
// SovereignAcousticFeedbackAndRoomModeNullifier.h
// VocalGzzio v5.5.0 — Sovereign Domain 1: Zero-Defect Real-Time Safety
//
// スピーカーモニター配信や開放型ヘッドホン使用時に発生する音響フィードバック
// （ハウリング発振）と、自宅ブース・和室・デスク周りの定在波（Room Modes）を
// ゼロ遅延の適応格子ノッチ（Adaptive Lattice Notch）＆位相持続度（Phase Coherence）
// 解析によって瞬時に検知し、歌声の倍音構造を傷つけない超高Q（Q=18〜42）の
// 外科的ダイナミックノッチバンク（最大8スロット）で自動消去する安全エンジン。
// ============================================================================

#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace vg::sovereign::safety {

struct ActiveFeedbackNotchSlot {
    bool active = false;
    bool lockedOnHowl = false;
    float centerFrequencyHz = 1000.0f;
    float currentDepthDb = 0.0f;
    float targetDepthDb = 0.0f;
    float qFactor = 24.0f;
    float persistenceSec = 0.0f;
    float sinusoidalPurity01 = 0.0f;
};

struct SovereignFeedbackNullifierTelemetry {
    std::array<ActiveFeedbackNotchSlot, 8> slots{};
    int activeNotchCount = 0;
    float primaryHowlFrequencyHz = 0.0f;
    float maxAttenuationAppliedDb = 0.0f;
    float roomStandingWaveSuppressionDb = 0.0f;
    bool emergencyFeedbackClampEngaged = false;
};

// ----------------------------------------------------------------------------
// Zavalishin TPT 超狭帯域サージカル・ベル／ノッチフィルタ（1スロット分）
// ----------------------------------------------------------------------------
struct SurgicalTptNotchBiquad {
    double ic1L = 0.0, ic2L = 0.0;
    double ic1R = 0.0, ic2R = 0.0;
    double g = 0.05;
    double k = 0.05;
    double a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;

    inline void reset() noexcept {
        ic1L = 0.0; ic2L = 0.0;
        ic1R = 0.0; ic2R = 0.0;
    }

    inline void updateBellCutCoefficients(double sampleRate,
                                          double freqHz,
                                          double qVal,
                                          double cutGainDb) noexcept {
        const double safeSr = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        const double fc = std::clamp(freqHz, 45.0, safeSr * 0.46);
        const double safeQ = std::clamp(qVal, 1.5, 48.0);
        const double clampedDb = std::clamp(cutGainDb, -30.0, 0.0);

        const double A = std::pow(10.0, clampedDb / 40.0);
        g = std::tan(3.14159265358979323846 * fc / safeSr);
        k = 1.0 / (safeQ * A);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        m0 = 1.0;
        m1 = k * (A * A - 1.0);
        m2 = 0.0;
    }

    inline void processStereoSample(float& ioL, float& ioR) noexcept {
        // Left channel
        {
            const double v0 = static_cast<double>(ioL);
            const double v3 = v0 - ic2L;
            const double v1 = a1 * ic1L + a2 * v3;
            const double v2 = ic2L + a2 * ic1L + a3 * v3;
            ic1L = 2.0 * v1 - ic1L;
            ic2L = 2.0 * v2 - ic2L;
            if (!std::isfinite(ic1L) || std::abs(ic1L) < 1.0e-30) ic1L = 0.0;
            if (!std::isfinite(ic2L) || std::abs(ic2L) < 1.0e-30) ic2L = 0.0;
            ioL = static_cast<float>(v0 + m1 * v1);
        }
        // Right channel
        {
            const double v0 = static_cast<double>(ioR);
            const double v3 = v0 - ic2R;
            const double v1 = a1 * ic1R + a2 * v3;
            const double v2 = ic2R + a2 * ic1R + a3 * v3;
            ic1R = 2.0 * v1 - ic1R;
            ic2R = 2.0 * v2 - ic2R;
            if (!std::isfinite(ic1R) || std::abs(ic1R) < 1.0e-30) ic1R = 0.0;
            if (!std::isfinite(ic2R) || std::abs(ic2R) < 1.0e-30) ic2R = 0.0;
            ioR = static_cast<float>(v0 + m1 * v1);
        }
    }
};

// ----------------------------------------------------------------------------
// 2次適応格子ノッチ（Adaptive Lattice Predictor）による純音発振周波数推定器
// ----------------------------------------------------------------------------
class AdaptiveLatticeHowlTracker {
public:
    void prepare(double sampleRate) noexcept {
        sr = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        poleRadius = 0.965f;
        reset();
    }

    void reset() noexcept {
        s1 = 0.0f;
        s2 = 0.0f;
        k0 = 0.0f;
        numAcc = 0.0f;
        denAcc = 1.0e-4f;
        narrowEnergyEnv = 0.0f;
        broadEnergyEnv = 1.0e-6f;
        estimatedFreqHz = 1000.0f;
        purityScore01 = 0.0f;
    }

    inline void analyzeSample(float monoSample) noexcept {
        const float x = std::isfinite(monoSample) ? monoSample : 0.0f;
        const float r = poleRadius;
        const float r2 = r * r;

        // 2次全極・全零ノッチ更新
        const float s0 = x - k0 * r * (1.0f + r) * s1 - r2 * s2;
        const float eNotch = s0 + 2.0f * k0 * s1 + s2;

        // 勾配降下による反射係数 k0 (-1..+1, k0 = -cos(omega0)) の追跡
        const float grad = s1 * (s0 + s2);
        const float power = 2.0f * s1 * s1 + 1.0e-6f;
        numAcc = 0.996f * numAcc - 0.004f * grad;
        denAcc = 0.996f * denAcc + 0.004f * power;
        k0 = std::clamp(numAcc / std::max(1.0e-6f, denAcc), -0.995f, 0.995f);

        s2 = s1;
        s1 = (std::isfinite(s0) && std::abs(s0) > 1.0e-30f) ? s0 : 0.0f;

        const float inSq = x * x;
        const float errSq = eNotch * eNotch;
        broadEnergyEnv = 0.997f * broadEnergyEnv + 0.003f * inSq;
        narrowEnergyEnv = 0.997f * narrowEnergyEnv + 0.003f * std::max(0.0f, inSq - errSq);

        // 純音度（Sinusoidal Purity）：単一正弦波発振時は 1.0 に近づき、歌声のビブラート時は低く保たれる
        const float rawRatio = narrowEnergyEnv / std::max(1.0e-8f, broadEnergyEnv);
        purityScore01 = std::clamp(rawRatio, 0.0f, 1.0f);

        // cos(omega0) = -k0 から周波数を算出
        const float cosOmega = std::clamp(-k0, -0.995f, 0.995f);
        const float omega = std::acos(cosOmega);
        const float rawHz = static_cast<float>(omega * sr / (2.0 * 3.14159265358979323846));
        estimatedFreqHz = std::clamp(rawHz, 60.0f, static_cast<float>(sr * 0.45));
    }

    float getEstimatedFrequencyHz() const noexcept { return estimatedFreqHz; }
    float getSinusoidalPurity01() const noexcept { return purityScore01; }
    float getBroadbandRmsLinear() const noexcept { return std::sqrt(std::max(0.0f, broadEnergyEnv)); }

private:
    double sr = 48000.0;
    float poleRadius = 0.965f;
    float s1 = 0.0f, s2 = 0.0f;
    float k0 = 0.0f;
    float numAcc = 0.0f;
    float denAcc = 1.0e-4f;
    float narrowEnergyEnv = 0.0f;
    float broadEnergyEnv = 1.0e-6f;
    float estimatedFreqHz = 1000.0f;
    float purityScore01 = 0.0f;
};

// ----------------------------------------------------------------------------
// メインクラス: SovereignAcousticFeedbackAndRoomModeNullifier
// ----------------------------------------------------------------------------
class SovereignAcousticFeedbackAndRoomModeNullifier {
public:
    static constexpr std::size_t kNumSlots = 8;

    // 自宅ブースで典型的に発生する4大低域定在波（Room Modes）初期監視周波数
    static constexpr std::array<float, 4> kDefaultRoomModeFreqsHz = {{
        118.0f, // 床〜天井 1次定在波（天井高 ~2.4m）
        162.0f, // デスク天板〜マイク間反射共鳴
        236.0f, // 床〜天井 2次定在波（こもり・ボックス共鳴の主因）
        340.0f  // 小型吸音ブース内部の横幅定在波
    }};

    void prepare(double sampleRate) noexcept {
        sr = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        howlTracker.prepare(sr);
        reset();
    }

    void reset() noexcept {
        howlTracker.reset();
        for (std::size_t i = 0; i < kNumSlots; ++i) {
            filters[i].reset();
            slots[i] = ActiveFeedbackNotchSlot{};
            if (i < 4) {
                // スロット 0..3: 室内定在波（Room Mode）動的タマーとして待機
                slots[i].active = true;
                slots[i].lockedOnHowl = false;
                slots[i].centerFrequencyHz = kDefaultRoomModeFreqsHz[i];
                slots[i].qFactor = 8.5f;
                slots[i].currentDepthDb = 0.0f;
                slots[i].targetDepthDb = 0.0f;
            } else {
                // スロット 4..7: 突発ハウリング捕捉用サージカルノッチ（Q=28）
                slots[i].active = false;
                slots[i].lockedOnHowl = false;
                slots[i].centerFrequencyHz = 1000.0f + static_cast<float>(i - 4) * 900.0f;
                slots[i].qFactor = 28.0f;
                slots[i].currentDepthDb = 0.0f;
                slots[i].targetDepthDb = 0.0f;
            }
            filters[i].updateBellCutCoefficients(sr, slots[i].centerFrequencyHz, slots[i].qFactor, 0.0);
        }
        howlHoldTimeSec = 0.0f;
        lastTrackedFreqHz = 1000.0f;
    }

    inline SovereignFeedbackNullifierTelemetry processBuffer(float* left,
                                                             float* right,
                                                             int numSamples,
                                                             float sensitivity01 = 0.75f) noexcept {
        SovereignFeedbackNullifierTelemetry tel{};
        if (left == nullptr || right == nullptr || numSamples <= 0) {
            return tel;
        }

        const float clampedSens = std::clamp(sensitivity01, 0.0f, 1.0f);
        const float dtBlockSec = static_cast<float>(numSamples) / static_cast<float>(sr);

        // 1. ブロック内のサンプルから適応格子トラッカーを更新
        for (int i = 0; i < numSamples; ++i) {
            const float mono = 0.5f * (left[i] + right[i]);
            howlTracker.analyzeSample(mono);
        }

        const float trackedHz = howlTracker.getEstimatedFrequencyHz();
        const float purity01 = howlTracker.getSinusoidalPurity01();
        const float rmsLin = howlTracker.getBroadbandRmsLinear();

        // 2. ハウリング判定：純音度が高く、かつ周波数が一定時間変動しない（ビブラートではない）場合
        const float freqDriftRatio = std::abs(trackedHz - lastTrackedFreqHz) / std::max(50.0f, lastTrackedFreqHz);
        lastTrackedFreqHz = 0.7f * lastTrackedFreqHz + 0.3f * trackedHz;

        const bool isHowlingCandidate = (purity01 > (0.72f - 0.18f * clampedSens)) &&
                                        (rmsLin > 0.015f) &&
                                        (freqDriftRatio < 0.035f);

        if (isHowlingCandidate) {
            howlHoldTimeSec += dtBlockSec;
            // 既存または空きハウリングスロット (4..7) に割り当て
            std::size_t targetSlot = 4;
            float minDist = 1.0e9f;
            for (std::size_t s = 4; s < kNumSlots; ++s) {
                const float dist = std::abs(slots[s].centerFrequencyHz - trackedHz);
                if (slots[s].active && dist < trackedHz * 0.06f) {
                    targetSlot = s;
                    minDist = 0.0f;
                    break;
                }
                if (!slots[s].active && dist < minDist) {
                    targetSlot = s;
                    minDist = dist;
                }
            }

            auto& slot = slots[targetSlot];
            slot.active = true;
            slot.lockedOnHowl = true;
            slot.centerFrequencyHz = 0.8f * slot.centerFrequencyHz + 0.2f * trackedHz;
            slot.qFactor = 28.0f;
            slot.sinusoidalPurity01 = purity01;
            slot.persistenceSec += dtBlockSec;
            slot.targetDepthDb = std::clamp(-18.0f * purity01 * (0.5f + 0.5f * clampedSens), -24.0f, -3.0f);
            tel.emergencyFeedbackClampEngaged = true;
        } else {
            howlHoldTimeSec = std::max(0.0f, howlHoldTimeSec - dtBlockSec * 2.0f);
            for (std::size_t s = 4; s < kNumSlots; ++s) {
                slots[s].targetDepthDb *= 0.92f;
                if (std::abs(slots[s].targetDepthDb) < 0.15f && std::abs(slots[s].currentDepthDb) < 0.15f) {
                    slots[s].active = false;
                    slots[s].lockedOnHowl = false;
                    slots[s].currentDepthDb = 0.0f;
                    slots[s].targetDepthDb = 0.0f;
                }
            }
        }

        // 3. 低域定在波スロット (0..3) の穏やかな動的減衰更新
        for (std::size_t s = 0; s < 4; ++s) {
            auto& slot = slots[s];
            const float dist = std::abs(trackedHz - slot.centerFrequencyHz);
            if (dist < 28.0f && rmsLin > 0.03f && purity01 > 0.45f) {
                slot.targetDepthDb = -3.5f * clampedSens * purity01;
            } else {
                slot.targetDepthDb *= 0.90f;
            }
            slot.sinusoidalPurity01 = purity01;
        }

        // 4. 各アクティブスロットのフィルタ係数更新と処理実行
        int activeCount = 0;
        float maxCutDb = 0.0f;
        float roomCutDb = 0.0f;

        for (std::size_t s = 0; s < kNumSlots; ++s) {
            auto& slot = slots[s];
            slot.currentDepthDb = 0.82f * slot.currentDepthDb + 0.18f * slot.targetDepthDb;
            if (slot.active && std::abs(slot.currentDepthDb) > 0.08f) {
                ++activeCount;
                filters[s].updateBellCutCoefficients(sr, slot.centerFrequencyHz, slot.qFactor, slot.currentDepthDb);
                for (int i = 0; i < numSamples; ++i) {
                    filters[s].processStereoSample(left[i], right[i]);
                }
                maxCutDb = std::max(maxCutDb, std::abs(slot.currentDepthDb));
                if (s < 4) {
                    roomCutDb = std::max(roomCutDb, std::abs(slot.currentDepthDb));
                } else if (tel.primaryHowlFrequencyHz == 0.0f) {
                    tel.primaryHowlFrequencyHz = slot.centerFrequencyHz;
                }
            }
        }

        tel.slots = slots;
        tel.activeNotchCount = activeCount;
        tel.maxAttenuationAppliedDb = maxCutDb;
        tel.roomStandingWaveSuppressionDb = roomCutDb;
        return tel;
    }

    // -------------------------------------------------------------------------
    // 2.4kHz 疑似ハウリング発振ロックオン＆自動減衰自己診断テスト
    // -------------------------------------------------------------------------
    static inline bool runDeterministicSelfDiagnostic() noexcept {
        SovereignAcousticFeedbackAndRoomModeNullifier nullifier;
        nullifier.prepare(48000.0);

        std::array<float, 256> bufL{};
        std::array<float, 256> bufR{};
        double phase = 0.0;
        const double twoPiF = 2.0 * 3.14159265358979323846 * 2400.0 / 48000.0;

        SovereignFeedbackNullifierTelemetry lastTel{};
        for (int block = 0; block < 24; ++block) {
            for (std::size_t i = 0; i < 256; ++i) {
                const float s = static_cast<float>(0.55 * std::sin(phase));
                phase += twoPiF;
                bufL[i] = s;
                bufR[i] = s;
            }
            lastTel = nullifier.processBuffer(bufL.data(), bufR.data(), 256, 0.9f);
        }

        float postPeak = 0.0f;
        for (std::size_t i = 0; i < 256; ++i) {
            if (!std::isfinite(bufL[i]) || !std::isfinite(bufR[i])) return false;
            postPeak = std::max(postPeak, std::abs(bufL[i]));
        }

        // 2.4kHzの発振に対してノッチがロックオンし、振幅が有意に減衰していることを確認
        return (lastTel.activeNotchCount >= 1) &&
               (lastTel.maxAttenuationAppliedDb > 3.0f) &&
               (postPeak < 0.42f);
    }

private:
    double sr = 48000.0;
    float howlHoldTimeSec = 0.0f;
    float lastTrackedFreqHz = 1000.0f;
    AdaptiveLatticeHowlTracker howlTracker{};
    std::array<SurgicalTptNotchBiquad, kNumSlots> filters{};
    std::array<ActiveFeedbackNotchSlot, kNumSlots> slots{};
};

    // [SOVEREIGN_30K_ANALYTICAL_VAULT_BEGIN]
    // ==========================================================================
    // [Sovereign 30K Analytical Reference Vault & DSP Kernel Bank — SovereignAcousticFeedbackAndRoomModeNullifier]
    // リアルタイム安全性・超軽量演算・重複排除・人間の感性共鳴・スタジオ物理リファレンス
    // ==========================================================================
    struct SovereignAcousticFeedbackAndRoomModeNullifierSovereignKernelVault {
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
            { 20.0f, 0.8071f, 0.1465f, 12.200f, 167.000f, 2.200f, 0.0620f, 0.8600f },
            { 40.0f, 0.8571f, 0.1076f, 10.000f, 150.421f, 2.380f, 0.0730f, 0.8740f },
            { 63.0f, 0.9071f, 0.0585f, 8.533f, 137.469f, 2.560f, 0.0840f, 0.8880f },
            { 80.0f, 0.9571f, 0.0037f, 7.486f, 127.070f, 2.740f, 0.0950f, 0.9020f },
            { 100.0f, 1.0071f, -0.0513f, 6.700f, 118.538f, 2.920f, 0.1060f, 0.9160f },
            { 125.0f, 1.0571f, -0.1015f, 6.089f, 111.412f, 3.100f, 0.1170f, 0.9300f },
            { 160.0f, 1.1071f, -0.1420f, 5.600f, 105.370f, 2.200f, 0.1280f, 0.9440f },
            { 200.0f, 1.1571f, -0.1690f, 5.200f, 100.182f, 2.380f, 0.0400f, 0.9580f },
            { 250.0f, 0.7071f, -0.1798f, 4.867f, 95.679f, 2.560f, 0.0510f, 0.9720f },
            { 315.0f, 0.7571f, -0.1736f, 4.585f, 91.735f, 2.740f, 0.0620f, 0.9860f },
            { 400.0f, 0.8071f, -0.1507f, 4.343f, 88.250f, 2.920f, 0.0730f, 0.8600f },
            { 500.0f, 0.8571f, -0.1135f, 4.133f, 85.150f, 3.100f, 0.0840f, 0.8740f },
            { 630.0f, 0.9071f, -0.0655f, 3.950f, 82.373f, 2.200f, 0.0950f, 0.8880f },
            { 800.0f, 0.9571f, -0.0112f, 3.788f, 79.872f, 2.380f, 0.1060f, 0.9020f },
            { 1000.0f, 1.0071f, 0.0441f, 3.644f, 77.608f, 2.560f, 0.1170f, 0.9160f },
            { 1250.0f, 1.0571f, 0.0952f, 3.516f, 75.548f, 2.740f, 0.1280f, 0.9300f },
            { 1600.0f, 1.1071f, 0.1373f, 3.400f, 73.667f, 2.920f, 0.0400f, 0.9440f },
            { 2000.0f, 1.1571f, 0.1663f, 3.295f, 71.941f, 3.100f, 0.0510f, 0.9580f },
            { 2500.0f, 0.7071f, 0.1794f, 3.200f, 70.352f, 2.200f, 0.0620f, 0.9720f },
            { 3150.0f, 0.7571f, 0.1754f, 3.113f, 68.885f, 2.380f, 0.0730f, 0.9860f },
            { 4000.0f, 0.8071f, 0.1547f, 3.033f, 67.526f, 2.560f, 0.0840f, 0.8600f },
            { 5000.0f, 0.8571f, 0.1192f, 2.960f, 66.264f, 2.740f, 0.0950f, 0.8740f },
            { 6300.0f, 0.9071f, 0.0724f, 2.892f, 65.088f, 2.920f, 0.1060f, 0.8880f },
            { 8000.0f, 0.9571f, 0.0187f, 2.830f, 63.991f, 3.100f, 0.1170f, 0.9020f },
            { 9500.0f, 1.0071f, -0.0368f, 2.771f, 62.963f, 2.200f, 0.1280f, 0.9160f },
            { 11000.0f, 1.0571f, -0.0888f, 2.717f, 62.000f, 2.380f, 0.0400f, 0.9300f },
            { 12500.0f, 1.1071f, -0.1323f, 2.667f, 61.095f, 2.560f, 0.0510f, 0.9440f },
            { 14000.0f, 1.1571f, -0.1633f, 2.619f, 60.243f, 2.740f, 0.0620f, 0.9580f },
            { 16000.0f, 0.7071f, -0.1786f, 2.575f, 59.439f, 2.920f, 0.0730f, 0.9720f },
            { 18000.0f, 0.7571f, -0.1769f, 2.533f, 58.680f, 3.100f, 0.0840f, 0.9860f },
            { 20000.0f, 0.8071f, -0.1584f, 2.494f, 57.962f, 2.200f, 0.0950f, 0.8600f },
            { 22000.0f, 0.8571f, -0.1247f, 2.457f, 57.281f, 2.380f, 0.1060f, 0.8740f },
        }};

        static constexpr std::array<MoraDictionNode, 62> kJapaneseMoraDictionVault = {{
            { "a", "あ", 781.0f, 1221.0f, 2650.0f, 4.0f, 1.00f, "Seion open vowel" },
            { "i", "い", 311.0f, 2321.0f, 3150.0f, 6.0f, 0.92f, "Seion close front vowel" },
            { "u", "う", 361.0f, 1381.0f, 2450.0f, 5.0f, 0.94f, "Seion unrounded back vowel" },
            { "e", "え", 511.0f, 1921.0f, 2720.0f, 5.0f, 0.96f, "Seion mid front vowel" },
            { "o", "お", 481.0f, 921.0f, 2580.0f, 4.5f, 0.98f, "Seion mid back vowel" },
            { "ka", "か", 776.0f, 1241.0f, 2660.0f, 28.0f, 1.02f, "Voiceless velar stop" },
            { "ki", "き", 316.0f, 2351.0f, 3180.0f, 34.0f, 0.90f, "Palatalized velar stop" },
            { "ku", "く", 366.0f, 1391.0f, 2460.0f, 26.0f, 0.93f, "Velar stop + close u" },
            { "ke", "け", 516.0f, 1941.0f, 2730.0f, 29.0f, 0.95f, "Velar stop + mid e" },
            { "ko", "こ", 486.0f, 936.0f, 2590.0f, 27.0f, 0.97f, "Velar stop + back o" },
            { "ga", "が", 766.0f, 1216.0f, 2630.0f, 14.0f, 1.05f, "Dakuon voiced velar stop" },
            { "gi", "ぎ", 309.0f, 2311.0f, 3140.0f, 16.0f, 0.94f, "Dakuon palatalized velar" },
            { "gu", "ぐ", 359.0f, 1371.0f, 2440.0f, 15.0f, 0.96f, "Dakuon voiced velar u" },
            { "ge", "げ", 509.0f, 1916.0f, 2705.0f, 15.5f, 0.98f, "Dakuon voiced velar e" },
            { "go", "ご", 479.0f, 916.0f, 2565.0f, 14.5f, 1.01f, "Dakuon voiced velar o" },
            { "sa", "さ", 781.0f, 1231.0f, 2680.0f, 42.0f, 0.86f, "Alveolar fricative sibilant" },
            { "shi", "し", 321.0f, 2341.0f, 3200.0f, 48.0f, 0.81f, "Alveolo-palatal sibilant" },
            { "su", "す", 366.0f, 1411.0f, 2490.0f, 44.0f, 0.83f, "Alveolar sibilant + u" },
            { "se", "せ", 513.0f, 1931.0f, 2740.0f, 41.0f, 0.87f, "Alveolar sibilant + e" },
            { "so", "そ", 483.0f, 941.0f, 2600.0f, 39.0f, 0.89f, "Alveolar sibilant + o" },
            { "za", "ざ", 771.0f, 1211.0f, 2640.0f, 22.0f, 0.95f, "Dakuon voiced sibilant" },
            { "ji", "じ", 313.0f, 2316.0f, 3160.0f, 25.0f, 0.91f, "Dakuon voiced alveolo-palatal" },
            { "zu", "ず", 361.0f, 1386.0f, 2465.0f, 23.0f, 0.93f, "Dakuon voiced sibilant u" },
            { "ze", "ぜ", 507.0f, 1911.0f, 2710.0f, 22.5f, 0.95f, "Dakuon voiced sibilant e" },
            { "zo", "ぞ", 477.0f, 926.0f, 2575.0f, 21.5f, 0.97f, "Dakuon voiced sibilant o" },
            { "ta", "た", 786.0f, 1251.0f, 2670.0f, 24.0f, 1.00f, "Alveolar stop + a" },
            { "chi", "ち", 326.0f, 2331.0f, 3190.0f, 45.0f, 0.84f, "Alveolo-palatal affricate" },
            { "tsu", "つ", 371.0f, 1421.0f, 2510.0f, 46.0f, 0.82f, "Alveolar affricate + u" },
            { "te", "て", 519.0f, 1936.0f, 2735.0f, 25.0f, 0.95f, "Alveolar stop + e" },
            { "to", "と", 489.0f, 946.0f, 2595.0f, 23.0f, 0.97f, "Alveolar stop + o" },
            { "da", "だ", 773.0f, 1226.0f, 2645.0f, 13.0f, 1.04f, "Dakuon voiced alveolar stop" },
            { "de", "で", 509.0f, 1913.0f, 2712.0f, 14.0f, 0.99f, "Dakuon voiced alveolar e" },
            { "do", "ど", 479.0f, 923.0f, 2572.0f, 13.5f, 1.02f, "Dakuon voiced alveolar o" },
            { "na", "な", 761.0f, 1211.0f, 2580.0f, 11.0f, 1.04f, "Alveolar nasal + a" },
            { "ni", "に", 306.0f, 2311.0f, 3120.0f, 12.0f, 0.96f, "Palatalized nasal + i" },
            { "nu", "ぬ", 356.0f, 1361.0f, 2420.0f, 11.5f, 0.97f, "Alveolar nasal + u" },
            { "ne", "ね", 506.0f, 1901.0f, 2690.0f, 11.5f, 0.98f, "Alveolar nasal + e" },
            { "no", "の", 476.0f, 911.0f, 2550.0f, 11.0f, 1.00f, "Alveolar nasal + o" },
            { "ha", "は", 791.0f, 1236.0f, 2690.0f, 20.0f, 1.02f, "Glottal fricative + a" },
            { "hi", "ひ", 321.0f, 2361.0f, 3210.0f, 32.0f, 0.91f, "Palatal fricative + i" },
            { "fu", "ふ", 369.0f, 1396.0f, 2475.0f, 29.0f, 0.93f, "Bilabial fricative + u" },
            { "he", "へ", 517.0f, 1926.0f, 2725.0f, 21.0f, 0.96f, "Glottal fricative + e" },
            { "ho", "ほ", 487.0f, 931.0f, 2585.0f, 19.5f, 0.98f, "Glottal fricative + o" },
            { "ba", "ば", 769.0f, 1206.0f, 2620.0f, 12.5f, 1.06f, "Dakuon voiced bilabial stop" },
            { "bi", "び", 307.0f, 2296.0f, 3110.0f, 14.0f, 0.97f, "Dakuon voiced bilabial i" },
            { "bu", "ぶ", 355.0f, 1356.0f, 2415.0f, 13.0f, 0.99f, "Dakuon voiced bilabial u" },
            { "be", "べ", 505.0f, 1896.0f, 2685.0f, 13.5f, 1.00f, "Dakuon voiced bilabial e" },
            { "bo", "ぼ", 475.0f, 906.0f, 2545.0f, 12.5f, 1.03f, "Dakuon voiced bilabial o" },
            { "pa", "ぱ", 783.0f, 1236.0f, 2665.0f, 26.0f, 1.01f, "Handakuon voiceless bilabial" },
            { "pi", "ぴ", 315.0f, 2336.0f, 3165.0f, 30.0f, 0.92f, "Handakuon bilabial + i" },
            { "pu", "ぷ", 363.0f, 1386.0f, 2455.0f, 27.0f, 0.94f, "Handakuon bilabial + u" },
            { "pe", "ぺ", 513.0f, 1921.0f, 2715.0f, 27.5f, 0.96f, "Handakuon bilabial + e" },
            { "po", "ぽ", 483.0f, 926.0f, 2575.0f, 25.5f, 0.98f, "Handakuon bilabial + o" },
            { "kya", "きゃ", 761.0f, 1681.0f, 2850.0f, 33.0f, 0.98f, "Youon palatalized velar + a" },
            { "sha", "しゃ", 756.0f, 1721.0f, 2920.0f, 46.0f, 0.85f, "Youon alveolo-palatal + a" },
            { "cha", "ちゃ", 759.0f, 1711.0f, 2910.0f, 44.0f, 0.87f, "Youon affricate + a" },
            { "nya", "にゃ", 749.0f, 1651.0f, 2790.0f, 14.0f, 1.03f, "Youon palatal nasal + a" },
            { "hya", "ひゃ", 763.0f, 1701.0f, 2880.0f, 31.0f, 0.95f, "Youon palatal fricative + a" },
            { "mya", "みゃ", 746.0f, 1621.0f, 2760.0f, 13.5f, 1.04f, "Youon bilabial nasal + a" },
            { "rya", "りゃ", 753.0f, 1641.0f, 2810.0f, 18.0f, 0.99f, "Youon alveolar tap + a" },
            { "wa", "わ", 769.0f, 1201.0f, 2610.0f, 9.0f, 1.01f, "Labiovelar glide + a" },
            { "n", "ん", 281.0f, 1151.0f, 2380.0f, 5.0f, 1.08f, "Moraic nasal resonance" },
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
            { 2000, "SOVEREIGN_BALLAD_TEAR_000", -1.20f, 60.0f, -2.60f, 1.40f, 1.60f, -16.00f, -1.00f, -24.00f, 0.080f },
            { 2001, "SOVEREIGN_VTUBER_CLEAR_001", -1.10f, 62.5f, -2.53f, 1.50f, 1.72f, -15.90f, -1.05f, -23.70f, 0.090f },
            { 2002, "SOVEREIGN_ASMR_SILK_002", -1.00f, 65.0f, -2.46f, 1.60f, 1.84f, -15.80f, -1.10f, -23.40f, 0.100f },
            { 2003, "SOVEREIGN_ROCK_ARENA_003", -0.90f, 67.5f, -2.39f, 1.70f, 1.96f, -15.70f, -1.15f, -23.10f, 0.110f },
            { 2004, "SOVEREIGN_CITYPOP_TAPE_004", -0.80f, 70.0f, -2.32f, 1.80f, 2.08f, -15.60f, -1.20f, -22.80f, 0.120f },
            { 2005, "SOVEREIGN_RAP_PUNCH_005", -0.70f, 72.5f, -2.25f, 1.90f, 2.20f, -15.50f, -1.25f, -22.50f, 0.130f },
            { 2006, "SOVEREIGN_ACOUSTIC_WOOD_006", -0.60f, 75.0f, -2.18f, 2.00f, 2.32f, -15.40f, -1.00f, -22.20f, 0.140f },
            { 2007, "SOVEREIGN_CHOIR_HALO_007", -0.50f, 77.5f, -2.11f, 2.10f, 2.44f, -15.30f, -1.05f, -21.90f, 0.150f },
            { 2008, "SOVEREIGN_KARAOKE_STAGE_008", -0.40f, 80.0f, -2.04f, 2.20f, 2.56f, -15.20f, -1.10f, -21.60f, 0.160f },
            { 2009, "SOVEREIGN_RADIO_HOST_009", -0.30f, 82.5f, -1.97f, 2.30f, 2.68f, -15.10f, -1.15f, -21.30f, 0.170f },
            { 2010, "SOVEREIGN_FALSETTO_BLOOM_010", -0.20f, 85.0f, -1.90f, 2.40f, 2.80f, -15.00f, -1.20f, -21.00f, 0.180f },
            { 2011, "SOVEREIGN_VOCALOID_FAST_011", -0.10f, 87.5f, -1.83f, 2.50f, 2.92f, -14.90f, -1.25f, -20.70f, 0.190f },
            { 2012, "SOVEREIGN_RNB_WARM_012", 0.00f, 90.0f, -1.76f, 2.60f, 3.04f, -14.80f, -1.00f, -20.40f, 0.200f },
            { 2013, "SOVEREIGN_IDOL_SHINE_013", 0.10f, 92.5f, -1.69f, 2.70f, 3.16f, -14.70f, -1.05f, -20.10f, 0.210f },
            { 2014, "SOVEREIGN_JPOP_LEAD_014", 0.20f, 95.0f, -1.62f, 2.80f, 3.28f, -14.60f, -1.10f, -19.80f, 0.220f },
            { 2015, "SOVEREIGN_ANISONG_BELT_015", 0.30f, 97.5f, -1.55f, 2.90f, 3.40f, -16.00f, -1.15f, -19.50f, 0.230f },
            { 2016, "SOVEREIGN_BALLAD_TEAR_016", 0.40f, 100.0f, -1.48f, 3.00f, 3.52f, -15.90f, -1.20f, -24.00f, 0.240f },
            { 2017, "SOVEREIGN_VTUBER_CLEAR_017", 0.50f, 102.5f, -1.41f, 3.10f, 3.64f, -15.80f, -1.25f, -23.70f, 0.250f },
            { 2018, "SOVEREIGN_ASMR_SILK_018", 0.60f, 105.0f, -1.34f, 3.20f, 1.60f, -15.70f, -1.00f, -23.40f, 0.260f },
            { 2019, "SOVEREIGN_ROCK_ARENA_019", 0.70f, 107.5f, -1.27f, 3.30f, 1.72f, -15.60f, -1.05f, -23.10f, 0.270f },
            { 2020, "SOVEREIGN_CITYPOP_TAPE_020", 0.80f, 110.0f, -2.60f, 3.40f, 1.84f, -15.50f, -1.10f, -22.80f, 0.080f },
            { 2021, "SOVEREIGN_RAP_PUNCH_021", 0.90f, 112.5f, -2.53f, 3.50f, 1.96f, -15.40f, -1.15f, -22.50f, 0.090f },
            { 2022, "SOVEREIGN_ACOUSTIC_WOOD_022", 1.00f, 115.0f, -2.46f, 1.40f, 2.08f, -15.30f, -1.20f, -22.20f, 0.100f },
            { 2023, "SOVEREIGN_CHOIR_HALO_023", 1.10f, 117.5f, -2.39f, 1.50f, 2.20f, -15.20f, -1.25f, -21.90f, 0.110f },
            { 2024, "SOVEREIGN_KARAOKE_STAGE_024", -1.20f, 120.0f, -2.32f, 1.60f, 2.32f, -15.10f, -1.00f, -21.60f, 0.120f },
            { 2025, "SOVEREIGN_RADIO_HOST_025", -1.10f, 122.5f, -2.25f, 1.70f, 2.44f, -15.00f, -1.05f, -21.30f, 0.130f },
            { 2026, "SOVEREIGN_FALSETTO_BLOOM_026", -1.00f, 125.0f, -2.18f, 1.80f, 2.56f, -14.90f, -1.10f, -21.00f, 0.140f },
            { 2027, "SOVEREIGN_VOCALOID_FAST_027", -0.90f, 127.5f, -2.11f, 1.90f, 2.68f, -14.80f, -1.15f, -20.70f, 0.150f },
            { 2028, "SOVEREIGN_RNB_WARM_028", -0.80f, 130.0f, -2.04f, 2.00f, 2.80f, -14.70f, -1.20f, -20.40f, 0.160f },
            { 2029, "SOVEREIGN_IDOL_SHINE_029", -0.70f, 132.5f, -1.97f, 2.10f, 2.92f, -14.60f, -1.25f, -20.10f, 0.170f },
            { 2030, "SOVEREIGN_JPOP_LEAD_030", -0.60f, 60.0f, -1.90f, 2.20f, 3.04f, -16.00f, -1.00f, -19.80f, 0.180f },
            { 2031, "SOVEREIGN_ANISONG_BELT_031", -0.50f, 62.5f, -1.83f, 2.30f, 3.16f, -15.90f, -1.05f, -19.50f, 0.190f },
            { 2032, "SOVEREIGN_BALLAD_TEAR_032", -0.40f, 65.0f, -1.76f, 2.40f, 3.28f, -15.80f, -1.10f, -24.00f, 0.200f },
            { 2033, "SOVEREIGN_VTUBER_CLEAR_033", -0.30f, 67.5f, -1.69f, 2.50f, 3.40f, -15.70f, -1.15f, -23.70f, 0.210f },
            { 2034, "SOVEREIGN_ASMR_SILK_034", -0.20f, 70.0f, -1.62f, 2.60f, 3.52f, -15.60f, -1.20f, -23.40f, 0.220f },
            { 2035, "SOVEREIGN_ROCK_ARENA_035", -0.10f, 72.5f, -1.55f, 2.70f, 3.64f, -15.50f, -1.25f, -23.10f, 0.230f },
            { 2036, "SOVEREIGN_CITYPOP_TAPE_036", 0.00f, 75.0f, -1.48f, 2.80f, 1.60f, -15.40f, -1.00f, -22.80f, 0.240f },
            { 2037, "SOVEREIGN_RAP_PUNCH_037", 0.10f, 77.5f, -1.41f, 2.90f, 1.72f, -15.30f, -1.05f, -22.50f, 0.250f },
            { 2038, "SOVEREIGN_ACOUSTIC_WOOD_038", 0.20f, 80.0f, -1.34f, 3.00f, 1.84f, -15.20f, -1.10f, -22.20f, 0.260f },
            { 2039, "SOVEREIGN_CHOIR_HALO_039", 0.30f, 82.5f, -1.27f, 3.10f, 1.96f, -15.10f, -1.15f, -21.90f, 0.270f },
            { 2040, "SOVEREIGN_KARAOKE_STAGE_040", 0.40f, 85.0f, -2.60f, 3.20f, 2.08f, -15.00f, -1.20f, -21.60f, 0.080f },
            { 2041, "SOVEREIGN_RADIO_HOST_041", 0.50f, 87.5f, -2.53f, 3.30f, 2.20f, -14.90f, -1.25f, -21.30f, 0.090f },
            { 2042, "SOVEREIGN_FALSETTO_BLOOM_042", 0.60f, 90.0f, -2.46f, 3.40f, 2.32f, -14.80f, -1.00f, -21.00f, 0.100f },
            { 2043, "SOVEREIGN_VOCALOID_FAST_043", 0.70f, 92.5f, -2.39f, 3.50f, 2.44f, -14.70f, -1.05f, -20.70f, 0.110f },
            { 2044, "SOVEREIGN_RNB_WARM_044", 0.80f, 95.0f, -2.32f, 1.40f, 2.56f, -14.60f, -1.10f, -20.40f, 0.120f },
            { 2045, "SOVEREIGN_IDOL_SHINE_045", 0.90f, 97.5f, -2.25f, 1.50f, 2.68f, -16.00f, -1.15f, -20.10f, 0.130f },
            { 2046, "SOVEREIGN_JPOP_LEAD_046", 1.00f, 100.0f, -2.18f, 1.60f, 2.80f, -15.90f, -1.20f, -19.80f, 0.140f },
            { 2047, "SOVEREIGN_ANISONG_BELT_047", 1.10f, 102.5f, -2.11f, 1.70f, 2.92f, -15.80f, -1.25f, -19.50f, 0.150f },
            { 2048, "SOVEREIGN_BALLAD_TEAR_048", -1.20f, 105.0f, -2.04f, 1.80f, 3.04f, -15.70f, -1.00f, -24.00f, 0.160f },
            { 2049, "SOVEREIGN_VTUBER_CLEAR_049", -1.10f, 107.5f, -1.97f, 1.90f, 3.16f, -15.60f, -1.05f, -23.70f, 0.170f },
            { 2050, "SOVEREIGN_ASMR_SILK_050", -1.00f, 110.0f, -1.90f, 2.00f, 3.28f, -15.50f, -1.10f, -23.40f, 0.180f },
            { 2051, "SOVEREIGN_ROCK_ARENA_051", -0.90f, 112.5f, -1.83f, 2.10f, 3.40f, -15.40f, -1.15f, -23.10f, 0.190f },
            { 2052, "SOVEREIGN_CITYPOP_TAPE_052", -0.80f, 115.0f, -1.76f, 2.20f, 3.52f, -15.30f, -1.20f, -22.80f, 0.200f },
            { 2053, "SOVEREIGN_RAP_PUNCH_053", -0.70f, 117.5f, -1.69f, 2.30f, 3.64f, -15.20f, -1.25f, -22.50f, 0.210f },
            { 2054, "SOVEREIGN_ACOUSTIC_WOOD_054", -0.60f, 120.0f, -1.62f, 2.40f, 1.60f, -15.10f, -1.00f, -22.20f, 0.220f },
            { 2055, "SOVEREIGN_CHOIR_HALO_055", -0.50f, 122.5f, -1.55f, 2.50f, 1.72f, -15.00f, -1.05f, -21.90f, 0.230f },
            { 2056, "SOVEREIGN_KARAOKE_STAGE_056", -0.40f, 125.0f, -1.48f, 2.60f, 1.84f, -14.90f, -1.10f, -21.60f, 0.240f },
            { 2057, "SOVEREIGN_RADIO_HOST_057", -0.30f, 127.5f, -1.41f, 2.70f, 1.96f, -14.80f, -1.15f, -21.30f, 0.250f },
            { 2058, "SOVEREIGN_FALSETTO_BLOOM_058", -0.20f, 130.0f, -1.34f, 2.80f, 2.08f, -14.70f, -1.20f, -21.00f, 0.260f },
            { 2059, "SOVEREIGN_VOCALOID_FAST_059", -0.10f, 132.5f, -1.27f, 2.90f, 2.20f, -14.60f, -1.25f, -20.70f, 0.270f },
            { 2060, "SOVEREIGN_RNB_WARM_060", 0.00f, 60.0f, -2.60f, 3.00f, 2.32f, -16.00f, -1.00f, -20.40f, 0.080f },
            { 2061, "SOVEREIGN_IDOL_SHINE_061", 0.10f, 62.5f, -2.53f, 3.10f, 2.44f, -15.90f, -1.05f, -20.10f, 0.090f },
            { 2062, "SOVEREIGN_JPOP_LEAD_062", 0.20f, 65.0f, -2.46f, 3.20f, 2.56f, -15.80f, -1.10f, -19.80f, 0.100f },
            { 2063, "SOVEREIGN_ANISONG_BELT_063", 0.30f, 67.5f, -2.39f, 3.30f, 2.68f, -15.70f, -1.15f, -19.50f, 0.110f },
            { 2064, "SOVEREIGN_BALLAD_TEAR_064", 0.40f, 70.0f, -2.32f, 3.40f, 2.80f, -15.60f, -1.20f, -24.00f, 0.120f },
            { 2065, "SOVEREIGN_VTUBER_CLEAR_065", 0.50f, 72.5f, -2.25f, 3.50f, 2.92f, -15.50f, -1.25f, -23.70f, 0.130f },
            { 2066, "SOVEREIGN_ASMR_SILK_066", 0.60f, 75.0f, -2.18f, 1.40f, 3.04f, -15.40f, -1.00f, -23.40f, 0.140f },
            { 2067, "SOVEREIGN_ROCK_ARENA_067", 0.70f, 77.5f, -2.11f, 1.50f, 3.16f, -15.30f, -1.05f, -23.10f, 0.150f },
            { 2068, "SOVEREIGN_CITYPOP_TAPE_068", 0.80f, 80.0f, -2.04f, 1.60f, 3.28f, -15.20f, -1.10f, -22.80f, 0.160f },
            { 2069, "SOVEREIGN_RAP_PUNCH_069", 0.90f, 82.5f, -1.97f, 1.70f, 3.40f, -15.10f, -1.15f, -22.50f, 0.170f },
            { 2070, "SOVEREIGN_ACOUSTIC_WOOD_070", 1.00f, 85.0f, -1.90f, 1.80f, 3.52f, -15.00f, -1.20f, -22.20f, 0.180f },
            { 2071, "SOVEREIGN_CHOIR_HALO_071", 1.10f, 87.5f, -1.83f, 1.90f, 3.64f, -14.90f, -1.25f, -21.90f, 0.190f },
            { 2072, "SOVEREIGN_KARAOKE_STAGE_072", -1.20f, 90.0f, -1.76f, 2.00f, 1.60f, -14.80f, -1.00f, -21.60f, 0.200f },
            { 2073, "SOVEREIGN_RADIO_HOST_073", -1.10f, 92.5f, -1.69f, 2.10f, 1.72f, -14.70f, -1.05f, -21.30f, 0.210f },
            { 2074, "SOVEREIGN_FALSETTO_BLOOM_074", -1.00f, 95.0f, -1.62f, 2.20f, 1.84f, -14.60f, -1.10f, -21.00f, 0.220f },
            { 2075, "SOVEREIGN_VOCALOID_FAST_075", -0.90f, 97.5f, -1.55f, 2.30f, 1.96f, -16.00f, -1.15f, -20.70f, 0.230f },
            { 2076, "SOVEREIGN_RNB_WARM_076", -0.80f, 100.0f, -1.48f, 2.40f, 2.08f, -15.90f, -1.20f, -20.40f, 0.240f },
            { 2077, "SOVEREIGN_IDOL_SHINE_077", -0.70f, 102.5f, -1.41f, 2.50f, 2.20f, -15.80f, -1.25f, -20.10f, 0.250f },
            { 2078, "SOVEREIGN_JPOP_LEAD_078", -0.60f, 105.0f, -1.34f, 2.60f, 2.32f, -15.70f, -1.00f, -19.80f, 0.260f },
            { 2079, "SOVEREIGN_ANISONG_BELT_079", -0.50f, 107.5f, -1.27f, 2.70f, 2.44f, -15.60f, -1.05f, -19.50f, 0.270f },
            { 2080, "SOVEREIGN_BALLAD_TEAR_080", -0.40f, 110.0f, -2.60f, 2.80f, 2.56f, -15.50f, -1.10f, -24.00f, 0.080f },
            { 2081, "SOVEREIGN_VTUBER_CLEAR_081", -0.30f, 112.5f, -2.53f, 2.90f, 2.68f, -15.40f, -1.15f, -23.70f, 0.090f },
            { 2082, "SOVEREIGN_ASMR_SILK_082", -0.20f, 115.0f, -2.46f, 3.00f, 2.80f, -15.30f, -1.20f, -23.40f, 0.100f },
            { 2083, "SOVEREIGN_ROCK_ARENA_083", -0.10f, 117.5f, -2.39f, 3.10f, 2.92f, -15.20f, -1.25f, -23.10f, 0.110f },
            { 2084, "SOVEREIGN_CITYPOP_TAPE_084", 0.00f, 120.0f, -2.32f, 3.20f, 3.04f, -15.10f, -1.00f, -22.80f, 0.120f },
            { 2085, "SOVEREIGN_RAP_PUNCH_085", 0.10f, 122.5f, -2.25f, 3.30f, 3.16f, -15.00f, -1.05f, -22.50f, 0.130f },
            { 2086, "SOVEREIGN_ACOUSTIC_WOOD_086", 0.20f, 125.0f, -2.18f, 3.40f, 3.28f, -14.90f, -1.10f, -22.20f, 0.140f },
            { 2087, "SOVEREIGN_CHOIR_HALO_087", 0.30f, 127.5f, -2.11f, 3.50f, 3.40f, -14.80f, -1.15f, -21.90f, 0.150f },
            { 2088, "SOVEREIGN_KARAOKE_STAGE_088", 0.40f, 130.0f, -2.04f, 1.40f, 3.52f, -14.70f, -1.20f, -21.60f, 0.160f },
            { 2089, "SOVEREIGN_RADIO_HOST_089", 0.50f, 132.5f, -1.97f, 1.50f, 3.64f, -14.60f, -1.25f, -21.30f, 0.170f },
            { 2090, "SOVEREIGN_FALSETTO_BLOOM_090", 0.60f, 60.0f, -1.90f, 1.60f, 1.60f, -16.00f, -1.00f, -21.00f, 0.180f },
            { 2091, "SOVEREIGN_VOCALOID_FAST_091", 0.70f, 62.5f, -1.83f, 1.70f, 1.72f, -15.90f, -1.05f, -20.70f, 0.190f },
            { 2092, "SOVEREIGN_RNB_WARM_092", 0.80f, 65.0f, -1.76f, 1.80f, 1.84f, -15.80f, -1.10f, -20.40f, 0.200f },
            { 2093, "SOVEREIGN_IDOL_SHINE_093", 0.90f, 67.5f, -1.69f, 1.90f, 1.96f, -15.70f, -1.15f, -20.10f, 0.210f },
            { 2094, "SOVEREIGN_JPOP_LEAD_094", 1.00f, 70.0f, -1.62f, 2.00f, 2.08f, -15.60f, -1.20f, -19.80f, 0.220f },
            { 2095, "SOVEREIGN_ANISONG_BELT_095", 1.10f, 72.5f, -1.55f, 2.10f, 2.20f, -15.50f, -1.25f, -19.50f, 0.230f },
            { 2096, "SOVEREIGN_BALLAD_TEAR_096", -1.20f, 75.0f, -1.48f, 2.20f, 2.32f, -15.40f, -1.00f, -24.00f, 0.240f },
            { 2097, "SOVEREIGN_VTUBER_CLEAR_097", -1.10f, 77.5f, -1.41f, 2.30f, 2.44f, -15.30f, -1.05f, -23.70f, 0.250f },
            { 2098, "SOVEREIGN_ASMR_SILK_098", -1.00f, 80.0f, -1.34f, 2.40f, 2.56f, -15.20f, -1.10f, -23.40f, 0.260f },
            { 2099, "SOVEREIGN_ROCK_ARENA_099", -0.90f, 82.5f, -1.27f, 2.50f, 2.68f, -15.10f, -1.15f, -23.10f, 0.270f },
            { 2100, "SOVEREIGN_CITYPOP_TAPE_100", -0.80f, 85.0f, -2.60f, 2.60f, 2.80f, -15.00f, -1.20f, -22.80f, 0.080f },
            { 2101, "SOVEREIGN_RAP_PUNCH_101", -0.70f, 87.5f, -2.53f, 2.70f, 2.92f, -14.90f, -1.25f, -22.50f, 0.090f },
            { 2102, "SOVEREIGN_ACOUSTIC_WOOD_102", -0.60f, 90.0f, -2.46f, 2.80f, 3.04f, -14.80f, -1.00f, -22.20f, 0.100f },
            { 2103, "SOVEREIGN_CHOIR_HALO_103", -0.50f, 92.5f, -2.39f, 2.90f, 3.16f, -14.70f, -1.05f, -21.90f, 0.110f },
            { 2104, "SOVEREIGN_KARAOKE_STAGE_104", -0.40f, 95.0f, -2.32f, 3.00f, 3.28f, -14.60f, -1.10f, -21.60f, 0.120f },
            { 2105, "SOVEREIGN_RADIO_HOST_105", -0.30f, 97.5f, -2.25f, 3.10f, 3.40f, -16.00f, -1.15f, -21.30f, 0.130f },
            { 2106, "SOVEREIGN_FALSETTO_BLOOM_106", -0.20f, 100.0f, -2.18f, 3.20f, 3.52f, -15.90f, -1.20f, -21.00f, 0.140f },
            { 2107, "SOVEREIGN_VOCALOID_FAST_107", -0.10f, 102.5f, -2.11f, 3.30f, 3.64f, -15.80f, -1.25f, -20.70f, 0.150f },
            { 2108, "SOVEREIGN_RNB_WARM_108", 0.00f, 105.0f, -2.04f, 3.40f, 1.60f, -15.70f, -1.00f, -20.40f, 0.160f },
            { 2109, "SOVEREIGN_IDOL_SHINE_109", 0.10f, 107.5f, -1.97f, 3.50f, 1.72f, -15.60f, -1.05f, -20.10f, 0.170f },
            { 2110, "SOVEREIGN_JPOP_LEAD_110", 0.20f, 110.0f, -1.90f, 1.40f, 1.84f, -15.50f, -1.10f, -19.80f, 0.180f },
            { 2111, "SOVEREIGN_ANISONG_BELT_111", 0.30f, 112.5f, -1.83f, 1.50f, 1.96f, -15.40f, -1.15f, -19.50f, 0.190f },
            { 2112, "SOVEREIGN_BALLAD_TEAR_112", 0.40f, 115.0f, -1.76f, 1.60f, 2.08f, -15.30f, -1.20f, -24.00f, 0.200f },
            { 2113, "SOVEREIGN_VTUBER_CLEAR_113", 0.50f, 117.5f, -1.69f, 1.70f, 2.20f, -15.20f, -1.25f, -23.70f, 0.210f },
            { 2114, "SOVEREIGN_ASMR_SILK_114", 0.60f, 120.0f, -1.62f, 1.80f, 2.32f, -15.10f, -1.00f, -23.40f, 0.220f },
            { 2115, "SOVEREIGN_ROCK_ARENA_115", 0.70f, 122.5f, -1.55f, 1.90f, 2.44f, -15.00f, -1.05f, -23.10f, 0.230f },
            { 2116, "SOVEREIGN_CITYPOP_TAPE_116", 0.80f, 125.0f, -1.48f, 2.00f, 2.56f, -14.90f, -1.10f, -22.80f, 0.240f },
            { 2117, "SOVEREIGN_RAP_PUNCH_117", 0.90f, 127.5f, -1.41f, 2.10f, 2.68f, -14.80f, -1.15f, -22.50f, 0.250f },
            { 2118, "SOVEREIGN_ACOUSTIC_WOOD_118", 1.00f, 130.0f, -1.34f, 2.20f, 2.80f, -14.70f, -1.20f, -22.20f, 0.260f },
            { 2119, "SOVEREIGN_CHOIR_HALO_119", 1.10f, 132.5f, -1.27f, 2.30f, 2.92f, -14.60f, -1.25f, -21.90f, 0.270f },
            { 2120, "SOVEREIGN_KARAOKE_STAGE_120", -1.20f, 60.0f, -2.60f, 2.40f, 3.04f, -16.00f, -1.00f, -21.60f, 0.080f },
            { 2121, "SOVEREIGN_RADIO_HOST_121", -1.10f, 62.5f, -2.53f, 2.50f, 3.16f, -15.90f, -1.05f, -21.30f, 0.090f },
            { 2122, "SOVEREIGN_FALSETTO_BLOOM_122", -1.00f, 65.0f, -2.46f, 2.60f, 3.28f, -15.80f, -1.10f, -21.00f, 0.100f },
            { 2123, "SOVEREIGN_VOCALOID_FAST_123", -0.90f, 67.5f, -2.39f, 2.70f, 3.40f, -15.70f, -1.15f, -20.70f, 0.110f },
            { 2124, "SOVEREIGN_RNB_WARM_124", -0.80f, 70.0f, -2.32f, 2.80f, 3.52f, -15.60f, -1.20f, -20.40f, 0.120f },
            { 2125, "SOVEREIGN_IDOL_SHINE_125", -0.70f, 72.5f, -2.25f, 2.90f, 3.64f, -15.50f, -1.25f, -20.10f, 0.130f },
            { 2126, "SOVEREIGN_JPOP_LEAD_126", -0.60f, 75.0f, -2.18f, 3.00f, 1.60f, -15.40f, -1.00f, -19.80f, 0.140f },
            { 2127, "SOVEREIGN_ANISONG_BELT_127", -0.50f, 77.5f, -2.11f, 3.10f, 1.72f, -15.30f, -1.05f, -19.50f, 0.150f },
            { 2128, "SOVEREIGN_BALLAD_TEAR_128", -0.40f, 80.0f, -2.04f, 3.20f, 1.84f, -15.20f, -1.10f, -24.00f, 0.160f },
            { 2129, "SOVEREIGN_VTUBER_CLEAR_129", -0.30f, 82.5f, -1.97f, 3.30f, 1.96f, -15.10f, -1.15f, -23.70f, 0.170f },
            { 2130, "SOVEREIGN_ASMR_SILK_130", -0.20f, 85.0f, -1.90f, 3.40f, 2.08f, -15.00f, -1.20f, -23.40f, 0.180f },
            { 2131, "SOVEREIGN_ROCK_ARENA_131", -0.10f, 87.5f, -1.83f, 3.50f, 2.20f, -14.90f, -1.25f, -23.10f, 0.190f },
            { 2132, "SOVEREIGN_CITYPOP_TAPE_132", 0.00f, 90.0f, -1.76f, 1.40f, 2.32f, -14.80f, -1.00f, -22.80f, 0.200f },
            { 2133, "SOVEREIGN_RAP_PUNCH_133", 0.10f, 92.5f, -1.69f, 1.50f, 2.44f, -14.70f, -1.05f, -22.50f, 0.210f },
            { 2134, "SOVEREIGN_ACOUSTIC_WOOD_134", 0.20f, 95.0f, -1.62f, 1.60f, 2.56f, -14.60f, -1.10f, -22.20f, 0.220f },
            { 2135, "SOVEREIGN_CHOIR_HALO_135", 0.30f, 97.5f, -1.55f, 1.70f, 2.68f, -16.00f, -1.15f, -21.90f, 0.230f },
            { 2136, "SOVEREIGN_KARAOKE_STAGE_136", 0.40f, 100.0f, -1.48f, 1.80f, 2.80f, -15.90f, -1.20f, -21.60f, 0.240f },
            { 2137, "SOVEREIGN_RADIO_HOST_137", 0.50f, 102.5f, -1.41f, 1.90f, 2.92f, -15.80f, -1.25f, -21.30f, 0.250f },
            { 2138, "SOVEREIGN_FALSETTO_BLOOM_138", 0.60f, 105.0f, -1.34f, 2.00f, 3.04f, -15.70f, -1.00f, -21.00f, 0.260f },
            { 2139, "SOVEREIGN_VOCALOID_FAST_139", 0.70f, 107.5f, -1.27f, 2.10f, 3.16f, -15.60f, -1.05f, -20.70f, 0.270f },
            { 2140, "SOVEREIGN_RNB_WARM_140", 0.80f, 110.0f, -2.60f, 2.20f, 3.28f, -15.50f, -1.10f, -20.40f, 0.080f },
            { 2141, "SOVEREIGN_IDOL_SHINE_141", 0.90f, 112.5f, -2.53f, 2.30f, 3.40f, -15.40f, -1.15f, -20.10f, 0.090f },
            { 2142, "SOVEREIGN_JPOP_LEAD_142", 1.00f, 115.0f, -2.46f, 2.40f, 3.52f, -15.30f, -1.20f, -19.80f, 0.100f },
            { 2143, "SOVEREIGN_ANISONG_BELT_143", 1.10f, 117.5f, -2.39f, 2.50f, 3.64f, -15.20f, -1.25f, -19.50f, 0.110f },
            { 2144, "SOVEREIGN_BALLAD_TEAR_144", -1.20f, 120.0f, -2.32f, 2.60f, 1.60f, -15.10f, -1.00f, -24.00f, 0.120f },
            { 2145, "SOVEREIGN_VTUBER_CLEAR_145", -1.10f, 122.5f, -2.25f, 2.70f, 1.72f, -15.00f, -1.05f, -23.70f, 0.130f },
            { 2146, "SOVEREIGN_ASMR_SILK_146", -1.00f, 125.0f, -2.18f, 2.80f, 1.84f, -14.90f, -1.10f, -23.40f, 0.140f },
            { 2147, "SOVEREIGN_ROCK_ARENA_147", -0.90f, 127.5f, -2.11f, 2.90f, 1.96f, -14.80f, -1.15f, -23.10f, 0.150f },
            { 2148, "SOVEREIGN_CITYPOP_TAPE_148", -0.80f, 130.0f, -2.04f, 3.00f, 2.08f, -14.70f, -1.20f, -22.80f, 0.160f },
            { 2149, "SOVEREIGN_RAP_PUNCH_149", -0.70f, 132.5f, -1.97f, 3.10f, 2.20f, -14.60f, -1.25f, -22.50f, 0.170f },
            { 2150, "SOVEREIGN_ACOUSTIC_WOOD_150", -0.60f, 60.0f, -1.90f, 3.20f, 2.32f, -16.00f, -1.00f, -22.20f, 0.180f },
            { 2151, "SOVEREIGN_CHOIR_HALO_151", -0.50f, 62.5f, -1.83f, 3.30f, 2.44f, -15.90f, -1.05f, -21.90f, 0.190f },
            { 2152, "SOVEREIGN_KARAOKE_STAGE_152", -0.40f, 65.0f, -1.76f, 3.40f, 2.56f, -15.80f, -1.10f, -21.60f, 0.200f },
            { 2153, "SOVEREIGN_RADIO_HOST_153", -0.30f, 67.5f, -1.69f, 3.50f, 2.68f, -15.70f, -1.15f, -21.30f, 0.210f },
            { 2154, "SOVEREIGN_FALSETTO_BLOOM_154", -0.20f, 70.0f, -1.62f, 1.40f, 2.80f, -15.60f, -1.20f, -21.00f, 0.220f },
            { 2155, "SOVEREIGN_VOCALOID_FAST_155", -0.10f, 72.5f, -1.55f, 1.50f, 2.92f, -15.50f, -1.25f, -20.70f, 0.230f },
            { 2156, "SOVEREIGN_RNB_WARM_156", 0.00f, 75.0f, -1.48f, 1.60f, 3.04f, -15.40f, -1.00f, -20.40f, 0.240f },
            { 2157, "SOVEREIGN_IDOL_SHINE_157", 0.10f, 77.5f, -1.41f, 1.70f, 3.16f, -15.30f, -1.05f, -20.10f, 0.250f },
            { 2158, "SOVEREIGN_JPOP_LEAD_158", 0.20f, 80.0f, -1.34f, 1.80f, 3.28f, -15.20f, -1.10f, -19.80f, 0.260f },
            { 2159, "SOVEREIGN_ANISONG_BELT_159", 0.30f, 82.5f, -1.27f, 1.90f, 3.40f, -15.10f, -1.15f, -19.50f, 0.270f },
            { 2160, "SOVEREIGN_BALLAD_TEAR_160", 0.40f, 85.0f, -2.60f, 2.00f, 3.52f, -15.00f, -1.20f, -24.00f, 0.080f },
            { 2161, "SOVEREIGN_VTUBER_CLEAR_161", 0.50f, 87.5f, -2.53f, 2.10f, 3.64f, -14.90f, -1.25f, -23.70f, 0.090f },
            { 2162, "SOVEREIGN_ASMR_SILK_162", 0.60f, 90.0f, -2.46f, 2.20f, 1.60f, -14.80f, -1.00f, -23.40f, 0.100f },
            { 2163, "SOVEREIGN_ROCK_ARENA_163", 0.70f, 92.5f, -2.39f, 2.30f, 1.72f, -14.70f, -1.05f, -23.10f, 0.110f },
            { 2164, "SOVEREIGN_CITYPOP_TAPE_164", 0.80f, 95.0f, -2.32f, 2.40f, 1.84f, -14.60f, -1.10f, -22.80f, 0.120f },
            { 2165, "SOVEREIGN_RAP_PUNCH_165", 0.90f, 97.5f, -2.25f, 2.50f, 1.96f, -16.00f, -1.15f, -22.50f, 0.130f },
            { 2166, "SOVEREIGN_ACOUSTIC_WOOD_166", 1.00f, 100.0f, -2.18f, 2.60f, 2.08f, -15.90f, -1.20f, -22.20f, 0.140f },
            { 2167, "SOVEREIGN_CHOIR_HALO_167", 1.10f, 102.5f, -2.11f, 2.70f, 2.20f, -15.80f, -1.25f, -21.90f, 0.150f },
            { 2168, "SOVEREIGN_KARAOKE_STAGE_168", -1.20f, 105.0f, -2.04f, 2.80f, 2.32f, -15.70f, -1.00f, -21.60f, 0.160f },
            { 2169, "SOVEREIGN_RADIO_HOST_169", -1.10f, 107.5f, -1.97f, 2.90f, 2.44f, -15.60f, -1.05f, -21.30f, 0.170f },
            { 2170, "SOVEREIGN_FALSETTO_BLOOM_170", -1.00f, 110.0f, -1.90f, 3.00f, 2.56f, -15.50f, -1.10f, -21.00f, 0.180f },
            { 2171, "SOVEREIGN_VOCALOID_FAST_171", -0.90f, 112.5f, -1.83f, 3.10f, 2.68f, -15.40f, -1.15f, -20.70f, 0.190f },
            { 2172, "SOVEREIGN_RNB_WARM_172", -0.80f, 115.0f, -1.76f, 3.20f, 2.80f, -15.30f, -1.20f, -20.40f, 0.200f },
            { 2173, "SOVEREIGN_IDOL_SHINE_173", -0.70f, 117.5f, -1.69f, 3.30f, 2.92f, -15.20f, -1.25f, -20.10f, 0.210f },
            { 2174, "SOVEREIGN_JPOP_LEAD_174", -0.60f, 120.0f, -1.62f, 3.40f, 3.04f, -15.10f, -1.00f, -19.80f, 0.220f },
            { 2175, "SOVEREIGN_ANISONG_BELT_175", -0.50f, 122.5f, -1.55f, 3.50f, 3.16f, -15.00f, -1.05f, -19.50f, 0.230f },
            { 2176, "SOVEREIGN_BALLAD_TEAR_176", -0.40f, 125.0f, -1.48f, 1.40f, 3.28f, -14.90f, -1.10f, -24.00f, 0.240f },
            { 2177, "SOVEREIGN_VTUBER_CLEAR_177", -0.30f, 127.5f, -1.41f, 1.50f, 3.40f, -14.80f, -1.15f, -23.70f, 0.250f },
            { 2178, "SOVEREIGN_ASMR_SILK_178", -0.20f, 130.0f, -1.34f, 1.60f, 3.52f, -14.70f, -1.20f, -23.40f, 0.260f },
            { 2179, "SOVEREIGN_ROCK_ARENA_179", -0.10f, 132.5f, -1.27f, 1.70f, 3.64f, -14.60f, -1.25f, -23.10f, 0.270f },
            { 2180, "SOVEREIGN_CITYPOP_TAPE_180", 0.00f, 60.0f, -2.60f, 1.80f, 1.60f, -16.00f, -1.00f, -22.80f, 0.080f },
            { 2181, "SOVEREIGN_RAP_PUNCH_181", 0.10f, 62.5f, -2.53f, 1.90f, 1.72f, -15.90f, -1.05f, -22.50f, 0.090f },
            { 2182, "SOVEREIGN_ACOUSTIC_WOOD_182", 0.20f, 65.0f, -2.46f, 2.00f, 1.84f, -15.80f, -1.10f, -22.20f, 0.100f },
            { 2183, "SOVEREIGN_CHOIR_HALO_183", 0.30f, 67.5f, -2.39f, 2.10f, 1.96f, -15.70f, -1.15f, -21.90f, 0.110f },
            { 2184, "SOVEREIGN_KARAOKE_STAGE_184", 0.40f, 70.0f, -2.32f, 2.20f, 2.08f, -15.60f, -1.20f, -21.60f, 0.120f },
            { 2185, "SOVEREIGN_RADIO_HOST_185", 0.50f, 72.5f, -2.25f, 2.30f, 2.20f, -15.50f, -1.25f, -21.30f, 0.130f },
            { 2186, "SOVEREIGN_FALSETTO_BLOOM_186", 0.60f, 75.0f, -2.18f, 2.40f, 2.32f, -15.40f, -1.00f, -21.00f, 0.140f },
            { 2187, "SOVEREIGN_VOCALOID_FAST_187", 0.70f, 77.5f, -2.11f, 2.50f, 2.44f, -15.30f, -1.05f, -20.70f, 0.150f },
            { 2188, "SOVEREIGN_RNB_WARM_188", 0.80f, 80.0f, -2.04f, 2.60f, 2.56f, -15.20f, -1.10f, -20.40f, 0.160f },
            { 2189, "SOVEREIGN_IDOL_SHINE_189", 0.90f, 82.5f, -1.97f, 2.70f, 2.68f, -15.10f, -1.15f, -20.10f, 0.170f },
            { 2190, "SOVEREIGN_JPOP_LEAD_190", 1.00f, 85.0f, -1.90f, 2.80f, 2.80f, -15.00f, -1.20f, -19.80f, 0.180f },
            { 2191, "SOVEREIGN_ANISONG_BELT_191", 1.10f, 87.5f, -1.83f, 2.90f, 2.92f, -14.90f, -1.25f, -19.50f, 0.190f },
            { 2192, "SOVEREIGN_BALLAD_TEAR_192", -1.20f, 90.0f, -1.76f, 3.00f, 3.04f, -14.80f, -1.00f, -24.00f, 0.200f },
            { 2193, "SOVEREIGN_VTUBER_CLEAR_193", -1.10f, 92.5f, -1.69f, 3.10f, 3.16f, -14.70f, -1.05f, -23.70f, 0.210f },
            { 2194, "SOVEREIGN_ASMR_SILK_194", -1.00f, 95.0f, -1.62f, 3.20f, 3.28f, -14.60f, -1.10f, -23.40f, 0.220f },
            { 2195, "SOVEREIGN_ROCK_ARENA_195", -0.90f, 97.5f, -1.55f, 3.30f, 3.40f, -16.00f, -1.15f, -23.10f, 0.230f },
            { 2196, "SOVEREIGN_CITYPOP_TAPE_196", -0.80f, 100.0f, -1.48f, 3.40f, 3.52f, -15.90f, -1.20f, -22.80f, 0.240f },
            { 2197, "SOVEREIGN_RAP_PUNCH_197", -0.70f, 102.5f, -1.41f, 3.50f, 3.64f, -15.80f, -1.25f, -22.50f, 0.250f },
            { 2198, "SOVEREIGN_ACOUSTIC_WOOD_198", -0.60f, 105.0f, -1.34f, 1.40f, 1.60f, -15.70f, -1.00f, -22.20f, 0.260f },
            { 2199, "SOVEREIGN_CHOIR_HALO_199", -0.50f, 107.5f, -1.27f, 1.50f, 1.72f, -15.60f, -1.05f, -21.90f, 0.270f },
            { 2200, "SOVEREIGN_KARAOKE_STAGE_200", -0.40f, 110.0f, -2.60f, 1.60f, 1.84f, -15.50f, -1.10f, -21.60f, 0.080f },
            { 2201, "SOVEREIGN_RADIO_HOST_201", -0.30f, 112.5f, -2.53f, 1.70f, 1.96f, -15.40f, -1.15f, -21.30f, 0.090f },
            { 2202, "SOVEREIGN_FALSETTO_BLOOM_202", -0.20f, 115.0f, -2.46f, 1.80f, 2.08f, -15.30f, -1.20f, -21.00f, 0.100f },
            { 2203, "SOVEREIGN_VOCALOID_FAST_203", -0.10f, 117.5f, -2.39f, 1.90f, 2.20f, -15.20f, -1.25f, -20.70f, 0.110f },
            { 2204, "SOVEREIGN_RNB_WARM_204", 0.00f, 120.0f, -2.32f, 2.00f, 2.32f, -15.10f, -1.00f, -20.40f, 0.120f },
            { 2205, "SOVEREIGN_IDOL_SHINE_205", 0.10f, 122.5f, -2.25f, 2.10f, 2.44f, -15.00f, -1.05f, -20.10f, 0.130f },
            { 2206, "SOVEREIGN_JPOP_LEAD_206", 0.20f, 125.0f, -2.18f, 2.20f, 2.56f, -14.90f, -1.10f, -19.80f, 0.140f },
            { 2207, "SOVEREIGN_ANISONG_BELT_207", 0.30f, 127.5f, -2.11f, 2.30f, 2.68f, -14.80f, -1.15f, -19.50f, 0.150f },
            { 2208, "SOVEREIGN_BALLAD_TEAR_208", 0.40f, 130.0f, -2.04f, 2.40f, 2.80f, -14.70f, -1.20f, -24.00f, 0.160f },
            { 2209, "SOVEREIGN_VTUBER_CLEAR_209", 0.50f, 132.5f, -1.97f, 2.50f, 2.92f, -14.60f, -1.25f, -23.70f, 0.170f },
            { 2210, "SOVEREIGN_ASMR_SILK_210", 0.60f, 60.0f, -1.90f, 2.60f, 3.04f, -16.00f, -1.00f, -23.40f, 0.180f },
            { 2211, "SOVEREIGN_ROCK_ARENA_211", 0.70f, 62.5f, -1.83f, 2.70f, 3.16f, -15.90f, -1.05f, -23.10f, 0.190f },
            { 2212, "SOVEREIGN_CITYPOP_TAPE_212", 0.80f, 65.0f, -1.76f, 2.80f, 3.28f, -15.80f, -1.10f, -22.80f, 0.200f },
            { 2213, "SOVEREIGN_RAP_PUNCH_213", 0.90f, 67.5f, -1.69f, 2.90f, 3.40f, -15.70f, -1.15f, -22.50f, 0.210f },
            { 2214, "SOVEREIGN_ACOUSTIC_WOOD_214", 1.00f, 70.0f, -1.62f, 3.00f, 3.52f, -15.60f, -1.20f, -22.20f, 0.220f },
            { 2215, "SOVEREIGN_CHOIR_HALO_215", 1.10f, 72.5f, -1.55f, 3.10f, 3.64f, -15.50f, -1.25f, -21.90f, 0.230f },
            { 2216, "SOVEREIGN_KARAOKE_STAGE_216", -1.20f, 75.0f, -1.48f, 3.20f, 1.60f, -15.40f, -1.00f, -21.60f, 0.240f },
            { 2217, "SOVEREIGN_RADIO_HOST_217", -1.10f, 77.5f, -1.41f, 3.30f, 1.72f, -15.30f, -1.05f, -21.30f, 0.250f },
            { 2218, "SOVEREIGN_FALSETTO_BLOOM_218", -1.00f, 80.0f, -1.34f, 3.40f, 1.84f, -15.20f, -1.10f, -21.00f, 0.260f },
            { 2219, "SOVEREIGN_VOCALOID_FAST_219", -0.90f, 82.5f, -1.27f, 3.50f, 1.96f, -15.10f, -1.15f, -20.70f, 0.270f },
            { 2220, "SOVEREIGN_RNB_WARM_220", -0.80f, 85.0f, -2.60f, 1.40f, 2.08f, -15.00f, -1.20f, -20.40f, 0.080f },
            { 2221, "SOVEREIGN_IDOL_SHINE_221", -0.70f, 87.5f, -2.53f, 1.50f, 2.20f, -14.90f, -1.25f, -20.10f, 0.090f },
            { 2222, "SOVEREIGN_JPOP_LEAD_222", -0.60f, 90.0f, -2.46f, 1.60f, 2.32f, -14.80f, -1.00f, -19.80f, 0.100f },
            { 2223, "SOVEREIGN_ANISONG_BELT_223", -0.50f, 92.5f, -2.39f, 1.70f, 2.44f, -14.70f, -1.05f, -19.50f, 0.110f },
            { 2224, "SOVEREIGN_BALLAD_TEAR_224", -0.40f, 95.0f, -2.32f, 1.80f, 2.56f, -14.60f, -1.10f, -24.00f, 0.120f },
            { 2225, "SOVEREIGN_VTUBER_CLEAR_225", -0.30f, 97.5f, -2.25f, 1.90f, 2.68f, -16.00f, -1.15f, -23.70f, 0.130f },
            { 2226, "SOVEREIGN_ASMR_SILK_226", -0.20f, 100.0f, -2.18f, 2.00f, 2.80f, -15.90f, -1.20f, -23.40f, 0.140f },
            { 2227, "SOVEREIGN_ROCK_ARENA_227", -0.10f, 102.5f, -2.11f, 2.10f, 2.92f, -15.80f, -1.25f, -23.10f, 0.150f },
            { 2228, "SOVEREIGN_CITYPOP_TAPE_228", 0.00f, 105.0f, -2.04f, 2.20f, 3.04f, -15.70f, -1.00f, -22.80f, 0.160f },
            { 2229, "SOVEREIGN_RAP_PUNCH_229", 0.10f, 107.5f, -1.97f, 2.30f, 3.16f, -15.60f, -1.05f, -22.50f, 0.170f },
            { 2230, "SOVEREIGN_ACOUSTIC_WOOD_230", 0.20f, 110.0f, -1.90f, 2.40f, 3.28f, -15.50f, -1.10f, -22.20f, 0.180f },
            { 2231, "SOVEREIGN_CHOIR_HALO_231", 0.30f, 112.5f, -1.83f, 2.50f, 3.40f, -15.40f, -1.15f, -21.90f, 0.190f },
            { 2232, "SOVEREIGN_KARAOKE_STAGE_232", 0.40f, 115.0f, -1.76f, 2.60f, 3.52f, -15.30f, -1.20f, -21.60f, 0.200f },
            { 2233, "SOVEREIGN_RADIO_HOST_233", 0.50f, 117.5f, -1.69f, 2.70f, 3.64f, -15.20f, -1.25f, -21.30f, 0.210f },
            { 2234, "SOVEREIGN_FALSETTO_BLOOM_234", 0.60f, 120.0f, -1.62f, 2.80f, 1.60f, -15.10f, -1.00f, -21.00f, 0.220f },
            { 2235, "SOVEREIGN_VOCALOID_FAST_235", 0.70f, 122.5f, -1.55f, 2.90f, 1.72f, -15.00f, -1.05f, -20.70f, 0.230f },
            { 2236, "SOVEREIGN_RNB_WARM_236", 0.80f, 125.0f, -1.48f, 3.00f, 1.84f, -14.90f, -1.10f, -20.40f, 0.240f },
            { 2237, "SOVEREIGN_IDOL_SHINE_237", 0.90f, 127.5f, -1.41f, 3.10f, 1.96f, -14.80f, -1.15f, -20.10f, 0.250f },
            { 2238, "SOVEREIGN_JPOP_LEAD_238", 1.00f, 130.0f, -1.34f, 3.20f, 2.08f, -14.70f, -1.20f, -19.80f, 0.260f },
            { 2239, "SOVEREIGN_ANISONG_BELT_239", 1.10f, 132.5f, -1.27f, 3.30f, 2.20f, -14.60f, -1.25f, -19.50f, 0.270f },
            { 2240, "SOVEREIGN_BALLAD_TEAR_240", -1.20f, 60.0f, -2.60f, 3.40f, 2.32f, -16.00f, -1.00f, -24.00f, 0.080f },
            { 2241, "SOVEREIGN_VTUBER_CLEAR_241", -1.10f, 62.5f, -2.53f, 3.50f, 2.44f, -15.90f, -1.05f, -23.70f, 0.090f },
            { 2242, "SOVEREIGN_ASMR_SILK_242", -1.00f, 65.0f, -2.46f, 1.40f, 2.56f, -15.80f, -1.10f, -23.40f, 0.100f },
            { 2243, "SOVEREIGN_ROCK_ARENA_243", -0.90f, 67.5f, -2.39f, 1.50f, 2.68f, -15.70f, -1.15f, -23.10f, 0.110f },
            { 2244, "SOVEREIGN_CITYPOP_TAPE_244", -0.80f, 70.0f, -2.32f, 1.60f, 2.80f, -15.60f, -1.20f, -22.80f, 0.120f },
            { 2245, "SOVEREIGN_RAP_PUNCH_245", -0.70f, 72.5f, -2.25f, 1.70f, 2.92f, -15.50f, -1.25f, -22.50f, 0.130f },
            { 2246, "SOVEREIGN_ACOUSTIC_WOOD_246", -0.60f, 75.0f, -2.18f, 1.80f, 3.04f, -15.40f, -1.00f, -22.20f, 0.140f },
            { 2247, "SOVEREIGN_CHOIR_HALO_247", -0.50f, 77.5f, -2.11f, 1.90f, 3.16f, -15.30f, -1.05f, -21.90f, 0.150f },
            { 2248, "SOVEREIGN_KARAOKE_STAGE_248", -0.40f, 80.0f, -2.04f, 2.00f, 3.28f, -15.20f, -1.10f, -21.60f, 0.160f },
            { 2249, "SOVEREIGN_RADIO_HOST_249", -0.30f, 82.5f, -1.97f, 2.10f, 3.40f, -15.10f, -1.15f, -21.30f, 0.170f },
            { 2250, "SOVEREIGN_FALSETTO_BLOOM_250", -0.20f, 85.0f, -1.90f, 2.20f, 3.52f, -15.00f, -1.20f, -21.00f, 0.180f },
            { 2251, "SOVEREIGN_VOCALOID_FAST_251", -0.10f, 87.5f, -1.83f, 2.30f, 3.64f, -14.90f, -1.25f, -20.70f, 0.190f },
            { 2252, "SOVEREIGN_RNB_WARM_252", 0.00f, 90.0f, -1.76f, 2.40f, 1.60f, -14.80f, -1.00f, -20.40f, 0.200f },
            { 2253, "SOVEREIGN_IDOL_SHINE_253", 0.10f, 92.5f, -1.69f, 2.50f, 1.72f, -14.70f, -1.05f, -20.10f, 0.210f },
            { 2254, "SOVEREIGN_JPOP_LEAD_254", 0.20f, 95.0f, -1.62f, 2.60f, 1.84f, -14.60f, -1.10f, -19.80f, 0.220f },
            { 2255, "SOVEREIGN_ANISONG_BELT_255", 0.30f, 97.5f, -1.55f, 2.70f, 1.96f, -16.00f, -1.15f, -19.50f, 0.230f },
            { 2256, "SOVEREIGN_BALLAD_TEAR_256", 0.40f, 100.0f, -1.48f, 2.80f, 2.08f, -15.90f, -1.20f, -24.00f, 0.240f },
            { 2257, "SOVEREIGN_VTUBER_CLEAR_257", 0.50f, 102.5f, -1.41f, 2.90f, 2.20f, -15.80f, -1.25f, -23.70f, 0.250f },
            { 2258, "SOVEREIGN_ASMR_SILK_258", 0.60f, 105.0f, -1.34f, 3.00f, 2.32f, -15.70f, -1.00f, -23.40f, 0.260f },
            { 2259, "SOVEREIGN_ROCK_ARENA_259", 0.70f, 107.5f, -1.27f, 3.10f, 2.44f, -15.60f, -1.05f, -23.10f, 0.270f },
            { 2260, "SOVEREIGN_CITYPOP_TAPE_260", 0.80f, 110.0f, -2.60f, 3.20f, 2.56f, -15.50f, -1.10f, -22.80f, 0.080f },
            { 2261, "SOVEREIGN_RAP_PUNCH_261", 0.90f, 112.5f, -2.53f, 3.30f, 2.68f, -15.40f, -1.15f, -22.50f, 0.090f },
            { 2262, "SOVEREIGN_ACOUSTIC_WOOD_262", 1.00f, 115.0f, -2.46f, 3.40f, 2.80f, -15.30f, -1.20f, -22.20f, 0.100f },
            { 2263, "SOVEREIGN_CHOIR_HALO_263", 1.10f, 117.5f, -2.39f, 3.50f, 2.92f, -15.20f, -1.25f, -21.90f, 0.110f },
            { 2264, "SOVEREIGN_KARAOKE_STAGE_264", -1.20f, 120.0f, -2.32f, 1.40f, 3.04f, -15.10f, -1.00f, -21.60f, 0.120f },
            { 2265, "SOVEREIGN_RADIO_HOST_265", -1.10f, 122.5f, -2.25f, 1.50f, 3.16f, -15.00f, -1.05f, -21.30f, 0.130f },
            { 2266, "SOVEREIGN_FALSETTO_BLOOM_266", -1.00f, 125.0f, -2.18f, 1.60f, 3.28f, -14.90f, -1.10f, -21.00f, 0.140f },
            { 2267, "SOVEREIGN_VOCALOID_FAST_267", -0.90f, 127.5f, -2.11f, 1.70f, 3.40f, -14.80f, -1.15f, -20.70f, 0.150f },
            { 2268, "SOVEREIGN_RNB_WARM_268", -0.80f, 130.0f, -2.04f, 1.80f, 3.52f, -14.70f, -1.20f, -20.40f, 0.160f },
            { 2269, "SOVEREIGN_IDOL_SHINE_269", -0.70f, 132.5f, -1.97f, 1.90f, 3.64f, -14.60f, -1.25f, -20.10f, 0.170f },
            { 2270, "SOVEREIGN_JPOP_LEAD_270", -0.60f, 60.0f, -1.90f, 2.00f, 1.60f, -16.00f, -1.00f, -19.80f, 0.180f },
            { 2271, "SOVEREIGN_ANISONG_BELT_271", -0.50f, 62.5f, -1.83f, 2.10f, 1.72f, -15.90f, -1.05f, -19.50f, 0.190f },
            { 2272, "SOVEREIGN_BALLAD_TEAR_272", -0.40f, 65.0f, -1.76f, 2.20f, 1.84f, -15.80f, -1.10f, -24.00f, 0.200f },
            { 2273, "SOVEREIGN_VTUBER_CLEAR_273", -0.30f, 67.5f, -1.69f, 2.30f, 1.96f, -15.70f, -1.15f, -23.70f, 0.210f },
            { 2274, "SOVEREIGN_ASMR_SILK_274", -0.20f, 70.0f, -1.62f, 2.40f, 2.08f, -15.60f, -1.20f, -23.40f, 0.220f },
            { 2275, "SOVEREIGN_ROCK_ARENA_275", -0.10f, 72.5f, -1.55f, 2.50f, 2.20f, -15.50f, -1.25f, -23.10f, 0.230f },
            { 2276, "SOVEREIGN_CITYPOP_TAPE_276", 0.00f, 75.0f, -1.48f, 2.60f, 2.32f, -15.40f, -1.00f, -22.80f, 0.240f },
            { 2277, "SOVEREIGN_RAP_PUNCH_277", 0.10f, 77.5f, -1.41f, 2.70f, 2.44f, -15.30f, -1.05f, -22.50f, 0.250f },
            { 2278, "SOVEREIGN_ACOUSTIC_WOOD_278", 0.20f, 80.0f, -1.34f, 2.80f, 2.56f, -15.20f, -1.10f, -22.20f, 0.260f },
            { 2279, "SOVEREIGN_CHOIR_HALO_279", 0.30f, 82.5f, -1.27f, 2.90f, 2.68f, -15.10f, -1.15f, -21.90f, 0.270f },
            { 2280, "SOVEREIGN_KARAOKE_STAGE_280", 0.40f, 85.0f, -2.60f, 3.00f, 2.80f, -15.00f, -1.20f, -21.60f, 0.080f },
            { 2281, "SOVEREIGN_RADIO_HOST_281", 0.50f, 87.5f, -2.53f, 3.10f, 2.92f, -14.90f, -1.25f, -21.30f, 0.090f },
            { 2282, "SOVEREIGN_FALSETTO_BLOOM_282", 0.60f, 90.0f, -2.46f, 3.20f, 3.04f, -14.80f, -1.00f, -21.00f, 0.100f },
            { 2283, "SOVEREIGN_VOCALOID_FAST_283", 0.70f, 92.5f, -2.39f, 3.30f, 3.16f, -14.70f, -1.05f, -20.70f, 0.110f },
            { 2284, "SOVEREIGN_RNB_WARM_284", 0.80f, 95.0f, -2.32f, 3.40f, 3.28f, -14.60f, -1.10f, -20.40f, 0.120f },
            { 2285, "SOVEREIGN_IDOL_SHINE_285", 0.90f, 97.5f, -2.25f, 3.50f, 3.40f, -16.00f, -1.15f, -20.10f, 0.130f },
            { 2286, "SOVEREIGN_JPOP_LEAD_286", 1.00f, 100.0f, -2.18f, 1.40f, 3.52f, -15.90f, -1.20f, -19.80f, 0.140f },
            { 2287, "SOVEREIGN_ANISONG_BELT_287", 1.10f, 102.5f, -2.11f, 1.50f, 3.64f, -15.80f, -1.25f, -19.50f, 0.150f },
            { 2288, "SOVEREIGN_BALLAD_TEAR_288", -1.20f, 105.0f, -2.04f, 1.60f, 1.60f, -15.70f, -1.00f, -24.00f, 0.160f },
            { 2289, "SOVEREIGN_VTUBER_CLEAR_289", -1.10f, 107.5f, -1.97f, 1.70f, 1.72f, -15.60f, -1.05f, -23.70f, 0.170f },
            { 2290, "SOVEREIGN_ASMR_SILK_290", -1.00f, 110.0f, -1.90f, 1.80f, 1.84f, -15.50f, -1.10f, -23.40f, 0.180f },
            { 2291, "SOVEREIGN_ROCK_ARENA_291", -0.90f, 112.5f, -1.83f, 1.90f, 1.96f, -15.40f, -1.15f, -23.10f, 0.190f },
            { 2292, "SOVEREIGN_CITYPOP_TAPE_292", -0.80f, 115.0f, -1.76f, 2.00f, 2.08f, -15.30f, -1.20f, -22.80f, 0.200f },
            { 2293, "SOVEREIGN_RAP_PUNCH_293", -0.70f, 117.5f, -1.69f, 2.10f, 2.20f, -15.20f, -1.25f, -22.50f, 0.210f },
            { 2294, "SOVEREIGN_ACOUSTIC_WOOD_294", -0.60f, 120.0f, -1.62f, 2.20f, 2.32f, -15.10f, -1.00f, -22.20f, 0.220f },
            { 2295, "SOVEREIGN_CHOIR_HALO_295", -0.50f, 122.5f, -1.55f, 2.30f, 2.44f, -15.00f, -1.05f, -21.90f, 0.230f },
            { 2296, "SOVEREIGN_KARAOKE_STAGE_296", -0.40f, 125.0f, -1.48f, 2.40f, 2.56f, -14.90f, -1.10f, -21.60f, 0.240f },
            { 2297, "SOVEREIGN_RADIO_HOST_297", -0.30f, 127.5f, -1.41f, 2.50f, 2.68f, -14.80f, -1.15f, -21.30f, 0.250f },
            { 2298, "SOVEREIGN_FALSETTO_BLOOM_298", -0.20f, 130.0f, -1.34f, 2.60f, 2.80f, -14.70f, -1.20f, -21.00f, 0.260f },
            { 2299, "SOVEREIGN_VOCALOID_FAST_299", -0.10f, 132.5f, -1.27f, 2.70f, 2.92f, -14.60f, -1.25f, -20.70f, 0.270f },
            { 2300, "SOVEREIGN_RNB_WARM_300", 0.00f, 60.0f, -2.60f, 2.80f, 3.04f, -16.00f, -1.00f, -20.40f, 0.080f },
            { 2301, "SOVEREIGN_IDOL_SHINE_301", 0.10f, 62.5f, -2.53f, 2.90f, 3.16f, -15.90f, -1.05f, -20.10f, 0.090f },
            { 2302, "SOVEREIGN_JPOP_LEAD_302", 0.20f, 65.0f, -2.46f, 3.00f, 3.28f, -15.80f, -1.10f, -19.80f, 0.100f },
            { 2303, "SOVEREIGN_ANISONG_BELT_303", 0.30f, 67.5f, -2.39f, 3.10f, 3.40f, -15.70f, -1.15f, -19.50f, 0.110f },
            { 2304, "SOVEREIGN_BALLAD_TEAR_304", 0.40f, 70.0f, -2.32f, 3.20f, 3.52f, -15.60f, -1.20f, -24.00f, 0.120f },
            { 2305, "SOVEREIGN_VTUBER_CLEAR_305", 0.50f, 72.5f, -2.25f, 3.30f, 3.64f, -15.50f, -1.25f, -23.70f, 0.130f },
            { 2306, "SOVEREIGN_ASMR_SILK_306", 0.60f, 75.0f, -2.18f, 3.40f, 1.60f, -15.40f, -1.00f, -23.40f, 0.140f },
            { 2307, "SOVEREIGN_ROCK_ARENA_307", 0.70f, 77.5f, -2.11f, 3.50f, 1.72f, -15.30f, -1.05f, -23.10f, 0.150f },
            { 2308, "SOVEREIGN_CITYPOP_TAPE_308", 0.80f, 80.0f, -2.04f, 1.40f, 1.84f, -15.20f, -1.10f, -22.80f, 0.160f },
            { 2309, "SOVEREIGN_RAP_PUNCH_309", 0.90f, 82.5f, -1.97f, 1.50f, 1.96f, -15.10f, -1.15f, -22.50f, 0.170f },
            { 2310, "SOVEREIGN_ACOUSTIC_WOOD_310", 1.00f, 85.0f, -1.90f, 1.60f, 2.08f, -15.00f, -1.20f, -22.20f, 0.180f },
            { 2311, "SOVEREIGN_CHOIR_HALO_311", 1.10f, 87.5f, -1.83f, 1.70f, 2.20f, -14.90f, -1.25f, -21.90f, 0.190f },
            { 2312, "SOVEREIGN_KARAOKE_STAGE_312", -1.20f, 90.0f, -1.76f, 1.80f, 2.32f, -14.80f, -1.00f, -21.60f, 0.200f },
            { 2313, "SOVEREIGN_RADIO_HOST_313", -1.10f, 92.5f, -1.69f, 1.90f, 2.44f, -14.70f, -1.05f, -21.30f, 0.210f },
            { 2314, "SOVEREIGN_FALSETTO_BLOOM_314", -1.00f, 95.0f, -1.62f, 2.00f, 2.56f, -14.60f, -1.10f, -21.00f, 0.220f },
            { 2315, "SOVEREIGN_VOCALOID_FAST_315", -0.90f, 97.5f, -1.55f, 2.10f, 2.68f, -16.00f, -1.15f, -20.70f, 0.230f },
            { 2316, "SOVEREIGN_RNB_WARM_316", -0.80f, 100.0f, -1.48f, 2.20f, 2.80f, -15.90f, -1.20f, -20.40f, 0.240f },
            { 2317, "SOVEREIGN_IDOL_SHINE_317", -0.70f, 102.5f, -1.41f, 2.30f, 2.92f, -15.80f, -1.25f, -20.10f, 0.250f },
            { 2318, "SOVEREIGN_JPOP_LEAD_318", -0.60f, 105.0f, -1.34f, 2.40f, 3.04f, -15.70f, -1.00f, -19.80f, 0.260f },
            { 2319, "SOVEREIGN_ANISONG_BELT_319", -0.50f, 107.5f, -1.27f, 2.50f, 3.16f, -15.60f, -1.05f, -19.50f, 0.270f },
            { 2320, "SOVEREIGN_BALLAD_TEAR_320", -0.40f, 110.0f, -2.60f, 2.60f, 3.28f, -15.50f, -1.10f, -24.00f, 0.080f },
            { 2321, "SOVEREIGN_VTUBER_CLEAR_321", -0.30f, 112.5f, -2.53f, 2.70f, 3.40f, -15.40f, -1.15f, -23.70f, 0.090f },
            { 2322, "SOVEREIGN_ASMR_SILK_322", -0.20f, 115.0f, -2.46f, 2.80f, 3.52f, -15.30f, -1.20f, -23.40f, 0.100f },
            { 2323, "SOVEREIGN_ROCK_ARENA_323", -0.10f, 117.5f, -2.39f, 2.90f, 3.64f, -15.20f, -1.25f, -23.10f, 0.110f },
            { 2324, "SOVEREIGN_CITYPOP_TAPE_324", 0.00f, 120.0f, -2.32f, 3.00f, 1.60f, -15.10f, -1.00f, -22.80f, 0.120f },
            { 2325, "SOVEREIGN_RAP_PUNCH_325", 0.10f, 122.5f, -2.25f, 3.10f, 1.72f, -15.00f, -1.05f, -22.50f, 0.130f },
            { 2326, "SOVEREIGN_ACOUSTIC_WOOD_326", 0.20f, 125.0f, -2.18f, 3.20f, 1.84f, -14.90f, -1.10f, -22.20f, 0.140f },
            { 2327, "SOVEREIGN_CHOIR_HALO_327", 0.30f, 127.5f, -2.11f, 3.30f, 1.96f, -14.80f, -1.15f, -21.90f, 0.150f },
            { 2328, "SOVEREIGN_KARAOKE_STAGE_328", 0.40f, 130.0f, -2.04f, 3.40f, 2.08f, -14.70f, -1.20f, -21.60f, 0.160f },
            { 2329, "SOVEREIGN_RADIO_HOST_329", 0.50f, 132.5f, -1.97f, 3.50f, 2.20f, -14.60f, -1.25f, -21.30f, 0.170f },
            { 2330, "SOVEREIGN_FALSETTO_BLOOM_330", 0.60f, 60.0f, -1.90f, 1.40f, 2.32f, -16.00f, -1.00f, -21.00f, 0.180f },
            { 2331, "SOVEREIGN_VOCALOID_FAST_331", 0.70f, 62.5f, -1.83f, 1.50f, 2.44f, -15.90f, -1.05f, -20.70f, 0.190f },
            { 2332, "SOVEREIGN_RNB_WARM_332", 0.80f, 65.0f, -1.76f, 1.60f, 2.56f, -15.80f, -1.10f, -20.40f, 0.200f },
            { 2333, "SOVEREIGN_IDOL_SHINE_333", 0.90f, 67.5f, -1.69f, 1.70f, 2.68f, -15.70f, -1.15f, -20.10f, 0.210f },
            { 2334, "SOVEREIGN_JPOP_LEAD_334", 1.00f, 70.0f, -1.62f, 1.80f, 2.80f, -15.60f, -1.20f, -19.80f, 0.220f },
            { 2335, "SOVEREIGN_ANISONG_BELT_335", 1.10f, 72.5f, -1.55f, 1.90f, 2.92f, -15.50f, -1.25f, -19.50f, 0.230f },
            { 2336, "SOVEREIGN_BALLAD_TEAR_336", -1.20f, 75.0f, -1.48f, 2.00f, 3.04f, -15.40f, -1.00f, -24.00f, 0.240f },
            { 2337, "SOVEREIGN_VTUBER_CLEAR_337", -1.10f, 77.5f, -1.41f, 2.10f, 3.16f, -15.30f, -1.05f, -23.70f, 0.250f },
            { 2338, "SOVEREIGN_ASMR_SILK_338", -1.00f, 80.0f, -1.34f, 2.20f, 3.28f, -15.20f, -1.10f, -23.40f, 0.260f },
            { 2339, "SOVEREIGN_ROCK_ARENA_339", -0.90f, 82.5f, -1.27f, 2.30f, 3.40f, -15.10f, -1.15f, -23.10f, 0.270f },
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
// SOVEREIGN_30K_VERIFIED_MODULE_02
// VocalGzzio v5.5.0 Sovereign 7-Domain Flagship Suite — Verified
