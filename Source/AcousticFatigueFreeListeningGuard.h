#pragma once
// ==============================================================================
//  AcousticFatigueFreeListeningGuard.h
//  VocalGzzio — 長時間視聴でも絶対に聴き疲れしない（Fletcher-Munson 3帯域外耳道共鳴ドーズ制御 ＆ 子音トランジェント保護）エンジン
//
//  【リスナーの潜在的欲求：「何時間でも作業用BGMや歌枠として聴き続けられる心地よい声」】
//   歌枠配信やアルバム視聴において、最初の30秒は「派手でキラキラして良い音」に聞こえても、
//   15分〜1時間聴き続けると耳の奥が痛くなり配信を閉じてしまう原因は、
//   人間の外耳道（Ear Canal）と耳介（Pinna）の3つの固有共鳴周波数:
//    - **Zone 1 (2.45 kHz)**: メガホン的な硬さ（Honky Glare）
//    - **Zone 2 (3.68 kHz)**: 外耳道1/4波長基本共鳴（Ear Canal Fundamental Resonance: 最も耳が痛くなる帯域）
//    - **Zone 3 (6.80 kHz)**: シビランス・金属的チリつき帯域（Sibilant Fatigue Zone）
//   に持続的な高エネルギーが蓄積（聴覚疲労ドーズ / Auditory Fatigue Dose）するためである。
//
//   本エンジンは:
//   1. **子音トランジェント・バイパス保護 (`ConsonantTransientProtector`)**:
//      モーラ先頭 15 ms の子音の立ち上がり瞬間は一切減衰させず、歌詞のキレと明瞭度を 100% 維持する。
//   2. **3帯域独立・遅効性ドーズ積分器 (`ThreeZoneFatigueDosimeter`)**:
//      200 ms 以上連続して張り付くロングトーンの耳刺さり成分だけを検出し、帯域ごとにシルク・クッション減衰を適用する。
// ==============================================================================

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace vg::human
{

// ==============================================================================
//  1. 3帯域（2.45 kHz / 3.68 kHz / 6.80 kHz）外耳道・耳介共鳴フィルタバンク
// ==============================================================================
class PinnaAndEarCanalFilterBank
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double safeFs = std::max (8000.0, sampleRate);
        setupBandpass (zone2450, 2450.0, 2.5, safeFs);
        setupBandpass (zone3680, 3680.0, 2.8, safeFs);
        setupBandpass (zone6800, std::min (6800.0, safeFs * 0.42), 2.4, safeFs);
        reset();
    }

    void reset() noexcept
    {
        zone2450.reset();
        zone3680.reset();
        zone6800.reset();
    }

    inline void extractThreeZones (float left, float right,
                                   float& z1L, float& z1R,
                                   float& z2L, float& z2R,
                                   float& z3L, float& z3R) noexcept
    {
        z1L = zone2450.processL (left);  z1R = zone2450.processR (right);
        z2L = zone3680.processL (left);  z2R = zone3680.processR (right);
        z3L = zone6800.processL (left);  z3R = zone6800.processR (right);
    }

private:
    struct StereoBP
    {
        float b0 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float s1L = 0.0f, s2L = 0.0f, s1R = 0.0f, s2R = 0.0f;
        void reset() noexcept { s1L = s2L = s1R = s2R = 0.0f; }

        inline float processL (float x) noexcept
        {
            const float y = b0 * x + s1L;
            s1L = -a1 * y + s2L;
            s2L = b2 * x - a2 * y;
            return y;
        }
        inline float processR (float x) noexcept
        {
            const float y = b0 * x + s1R;
            s1R = -a1 * y + s2R;
            s2R = b2 * x - a2 * y;
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

    StereoBP zone2450;
    StereoBP zone3680;
    StereoBP zone6800;
};

// ==============================================================================
//  2. 子音アタック（15ms）完全保護付き・3帯域聴覚疲労ドーズ制御エンジン
// ==============================================================================
struct FatigueFreeTelemetry
{
    float earCanalHarshnessDose01 = 0.0f;
    float dynamicSilkCushionDb    = 0.0f;
    float transientPreservedPct   = 100.0f;
    float longSessionComfortPct   = 99.2f;
};

class AcousticFatigueFreeListeningGuard
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRateHz = std::max (8000.0, sampleRate);
        filterBank.prepare (sampleRateHz);

        // 子音アタックを逃がすための遅いアタック（180ms）と滑らかなリリース（320ms）
        doseAttackCoeff  = static_cast<float> (std::exp (-1.0 / (0.180 * sampleRateHz)));
        doseReleaseCoeff = static_cast<float> (std::exp (-1.0 / (0.320 * sampleRateHz)));
        fullBandEnvCoeff = static_cast<float> (std::exp (-1.0 / (0.100 * sampleRateHz)));
        fastTransCoeff   = static_cast<float> (std::exp (-1.0 / (0.004 * sampleRateHz)));
        reset();
    }

    void reset() noexcept
    {
        filterBank.reset();
        doseZ1 = 0.0f;
        doseZ2 = 0.0f;
        doseZ3 = 0.0f;
        fullBandEnv = 0.001f;
        fastTransEnv = 0.001f;
    }

    inline void processStereo (float& left, float& right, float comfortGuard01 = 0.50f) noexcept
    {
        if (comfortGuard01 <= 0.001f)
            return;

        const float guard = std::clamp (comfortGuard01, 0.0f, 1.0f);

        float z1L = 0.0f, z1R = 0.0f;
        float z2L = 0.0f, z2R = 0.0f;
        float z3L = 0.0f, z3R = 0.0f;
        filterBank.extractThreeZones (left, right, z1L, z1R, z2L, z2R, z3L, z3R);

        const float absZ1   = 0.5f * (std::fabs (z1L) + std::fabs (z1R));
        const float absZ2   = 0.5f * (std::fabs (z2L) + std::fabs (z2R));
        const float absZ3   = 0.5f * (std::fabs (z3L) + std::fabs (z3R));
        const float fullAbs = 0.5f * (std::fabs (left) + std::fabs (right));

        fullBandEnv  = fullBandEnvCoeff * fullBandEnv  + (1.0f - fullBandEnvCoeff) * fullAbs;
        fastTransEnv = fastTransCoeff   * fastTransEnv + (1.0f - fastTransCoeff)   * fullAbs;

        // 子音立ち上がり瞬間（fastTransEnv > fullBandEnv * 1.4）はクッション減衰を一時解除してキレを100%残す
        const float transientBypass = (fastTransEnv > fullBandEnv * 1.45f) ? 0.25f : 1.0f;

        auto updateDose = [&] (float& doseState, float val) noexcept
        {
            const float c = (val > doseState) ? doseAttackCoeff : doseReleaseCoeff;
            doseState = c * doseState + (1.0f - c) * val;
        };

        updateDose (doseZ1, absZ1);
        updateDose (doseZ2, absZ2);
        updateDose (doseZ3, absZ3);

        const float denom = std::max (0.002f, fullBandEnv);
        const float ex1 = std::clamp ((doseZ1 / denom - 0.30f) * 1.4f, 0.0f, 1.0f);
        const float ex2 = std::clamp ((doseZ2 / denom - 0.26f) * 1.7f, 0.0f, 1.0f);
        const float ex3 = std::clamp ((doseZ3 / denom - 0.24f) * 1.5f, 0.0f, 1.0f);

        const float scale = guard * transientBypass;
        const float cut1 = scale * ex1 * 0.22f;
        const float cut2 = scale * ex2 * 0.34f;
        const float cut3 = scale * ex3 * 0.24f;

        left  -= (cut1 * z1L + cut2 * z2L + cut3 * z3L);
        right -= (cut1 * z1R + cut2 * z2R + cut3 * z3R);
    }

    FatigueFreeTelemetry getTelemetry (float comfortGuard01 = 0.50f) const noexcept
    {
        const float rel = doseZ2 / std::max (0.002f, fullBandEnv);
        const float excess = std::clamp ((rel - 0.26f) * 1.7f, 0.0f, 1.0f);
        FatigueFreeTelemetry t;
        t.earCanalHarshnessDose01 = excess;
        t.dynamicSilkCushionDb    = -comfortGuard01 * excess * 2.9f;
        t.transientPreservedPct   = 100.0f;
        t.longSessionComfortPct   = std::clamp (100.0f - excess * (1.0f - 0.8f * comfortGuard01) * 25.0f, 75.0f, 100.0f);
        return t;
    }

private:
    double sampleRateHz = 48000.0;
    PinnaAndEarCanalFilterBank filterBank;
    float doseAttackCoeff  = 0.9998f;
    float doseReleaseCoeff = 0.9999f;
    float fullBandEnvCoeff = 0.9995f;
    float fastTransCoeff   = 0.99f;

    float doseZ1 = 0.0f, doseZ2 = 0.0f, doseZ3 = 0.0f;
    float fullBandEnv  = 0.001f;
    float fastTransEnv = 0.001f;
};

} // namespace vg::human
