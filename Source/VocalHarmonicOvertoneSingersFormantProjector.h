#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vg::human
{

// ============================================================================
// VocalHarmonicOvertoneSingersFormantProjector.h
// ----------------------------------------------------------------------------
// 人間の欲求「伴奏をどれだけ豪華にしても、自分の声だけが主役として真っ直ぐ前に出てほしい」
// を叶える音響生理学・オペラ発声科学エンジン:
//   - Johan Sundberg & Wilmer Bartholomew が発見した「歌手のフォルマント
//     (Singer's Formant Cluster: Fs = 2.8 kHz 〜 3.4 kHz, 喉頭蓋管共鳴)」を再現。
//   - 一般的なバンド・オーケストラ・EDM伴奏のエネルギー密度は 400〜1,200 Hz でピークを迎え、
//     2.8〜3.4 kHz 付近で自然に減衰する一方、人間の聴覚（外耳道共鳴）はこの帯域に最も敏感。
//   - 単なるEQブースト（歯擦音まで刺さる）ではなく、声帯基本波の有声区間（Voiced）でのみ
//     第3〜第5フォルマントの密集共鳴（Cluster）を励起することで、
//     全体の音量（LUFS）を上げなくても「伴奏の壁を突き抜けて目の前で歌っている」存在感を実現。
// ============================================================================

struct SingersFormantTelemetry
{
    float clusterEnergyDb = -60.0f;          // 2.8-3.4 kHz 歌手フォルマント帯域エネルギー (dB)
    float projectionPresenceIndex01 = 0.0f;  // 伴奏貫通プレゼンス指数 (0..1)
    float chestToHeadBlendRatio01 = 0.5f;    // チェスト〜ヘッド倍音ブレンド比率 (0..1)
    float harmonicRichnessGainDb = 0.0f;     // 付与された倍音密度ゲイン (dB)
};

class VocalHarmonicOvertoneSingersFormantProjector
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        updateResonators();
        reset();
    }

    void reset() noexcept
    {
        bpF3_z1L = bpF3_z2L = bpF3_z1R = bpF3_z2R = 0.0f;
        bpF4_z1L = bpF4_z2L = bpF4_z1R = bpF4_z2R = 0.0f;
        bpF5_z1L = bpF5_z2L = bpF5_z1R = bpF5_z2R = 0.0f;
        lowVoicedEnv = 0.0f;
        sibilanceEnv = 0.0f;
        clusterRmsEnv = 0.0f;
        lpVoicedState = 0.0f;
        hpSibState = 0.0f;
        lastTelemetry = SingersFormantTelemetry{};
    }

    void setProjectionAmount01(float amount01) noexcept
    {
        projectionAmount = std::clamp(amount01, 0.0f, 1.0f);
    }

    SingersFormantTelemetry processBuffer(float* left, float* right, int numSamples) noexcept
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return lastTelemetry;

        float sumClusterSq = 0.0f;
        float maxPresence = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left[i];
            const float r = right[i];
            const float mono = 0.5f * (l + r);

            // 有声母音区間（< 900 Hz）と無声歯擦音区間（> 5.5 kHz）の比率から
            // 「母音が鳴っている瞬間だけ Singer's Formant Cluster を開く」ゲートを算出
            lpVoicedState += voicedLpCoeff * (mono - lpVoicedState);
            const float highDiff = mono - hpSibState;
            hpSibState += sibHpCoeff * highDiff;

            lowVoicedEnv += 0.015f * (std::fabs(lpVoicedState) - lowVoicedEnv);
            sibilanceEnv += 0.03f * (std::fabs(highDiff) - sibilanceEnv);

            const float voicedGate01 = std::clamp(
                (lowVoicedEnv - 1.6f * sibilanceEnv) / (lowVoicedEnv + 0.004f),
                0.0f, 1.0f);

            // 喉頭蓋管の3重クラスター共鳴 (F3=2850Hz, F4=3120Hz, F5=3380Hz)
            const float f3L = processBandpass(l, bpF3_b0, bpF3_a1, bpF3_a2, bpF3_z1L, bpF3_z2L);
            const float f3R = processBandpass(r, bpF3_b0, bpF3_a1, bpF3_a2, bpF3_z1R, bpF3_z2R);

            const float f4L = processBandpass(l, bpF4_b0, bpF4_a1, bpF4_a2, bpF4_z1L, bpF4_z2L);
            const float f4R = processBandpass(r, bpF4_b0, bpF4_a1, bpF4_a2, bpF4_z1R, bpF4_z2R);

            const float f5L = processBandpass(l, bpF5_b0, bpF5_a1, bpF5_a2, bpF5_z1L, bpF5_z2L);
            const float f5R = processBandpass(r, bpF5_b0, bpF5_a1, bpF5_a2, bpF5_z1R, bpF5_z2R);

            // 偶数次＋3次倍音のシルク飽和で密度を高めてからブレンド
            const float clusterRawL = 0.42f * f3L + 0.36f * f4L + 0.22f * f5L;
            const float clusterRawR = 0.42f * f3R + 0.36f * f4R + 0.22f * f5R;

            const float silkL = clusterRawL / (1.0f + 0.65f * std::fabs(clusterRawL));
            const float silkR = clusterRawR / (1.0f + 0.65f * std::fabs(clusterRawR));

            const float injectGain = projectionAmount * voicedGate01 * 0.34f;
            left[i] = l + silkL * injectGain;
            right[i] = r + silkR * injectGain;

            const float cMono = 0.5f * (silkL + silkR);
            sumClusterSq += cMono * cMono;
            maxPresence = std::max(maxPresence, voicedGate01 * projectionAmount);
        }

        const float clusterRms = std::sqrt(sumClusterSq / static_cast<float>(numSamples) + 1.0e-12f);
        clusterRmsEnv = 0.85f * clusterRmsEnv + 0.15f * clusterRms;

        lastTelemetry.clusterEnergyDb = 20.0f * std::log10(std::max(1.0e-6f, clusterRmsEnv));
        lastTelemetry.projectionPresenceIndex01 = std::clamp(maxPresence, 0.0f, 1.0f);
        lastTelemetry.chestToHeadBlendRatio01 = std::clamp(lowVoicedEnv / (lowVoicedEnv + clusterRmsEnv + 1.0e-5f), 0.0f, 1.0f);
        lastTelemetry.harmonicRichnessGainDb = 20.0f * std::log10(1.0f + 0.34f * maxPresence);
        return lastTelemetry;
    }

    const SingersFormantTelemetry& getLastTelemetry() const noexcept { return lastTelemetry; }

private:
    static inline float processBandpass(float x, float b0, float a1, float a2, float& z1, float& z2) noexcept
    {
        const float y = b0 * x + z1;
        z1 = -a1 * y + z2;
        z2 = -b0 * x - a2 * y;
        return y;
    }

    void computeBandpassCoeffs(float freqHz, float q, float& b0, float& a1, float& a2) const noexcept
    {
        const float w0 = 2.0f * 3.14159265358979323846f * std::clamp(freqHz, 200.0f, static_cast<float>(sr * 0.44)) / static_cast<float>(sr);
        const float alpha = std::sin(w0) / (2.0f * std::max(1.0f, q));
        const float invA0 = 1.0f / (1.0f + alpha);
        b0 = alpha * invA0;
        a1 = (-2.0f * std::cos(w0)) * invA0;
        a2 = (1.0f - alpha) * invA0;
    }

    void updateResonators() noexcept
    {
        voicedLpCoeff = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 850.0f / static_cast<float>(sr));
        sibHpCoeff = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 5400.0f / static_cast<float>(sr));
        computeBandpassCoeffs(2850.0f, 5.5f, bpF3_b0, bpF3_a1, bpF3_a2);
        computeBandpassCoeffs(3120.0f, 6.2f, bpF4_b0, bpF4_a1, bpF4_a2);
        computeBandpassCoeffs(3380.0f, 6.8f, bpF5_b0, bpF5_a1, bpF5_a2);
    }

    double sr = 48000.0;
    float projectionAmount = 0.55f;
    float voicedLpCoeff = 0.1f;
    float sibHpCoeff = 0.4f;
    float bpF3_b0 = 0.05f, bpF3_a1 = 0.0f, bpF3_a2 = 0.0f;
    float bpF4_b0 = 0.05f, bpF4_a1 = 0.0f, bpF4_a2 = 0.0f;
    float bpF5_b0 = 0.05f, bpF5_a1 = 0.0f, bpF5_a2 = 0.0f;
    float bpF3_z1L = 0.0f, bpF3_z2L = 0.0f, bpF3_z1R = 0.0f, bpF3_z2R = 0.0f;
    float bpF4_z1L = 0.0f, bpF4_z2L = 0.0f, bpF4_z1R = 0.0f, bpF4_z2R = 0.0f;
    float bpF5_z1L = 0.0f, bpF5_z2L = 0.0f, bpF5_z1R = 0.0f, bpF5_z2R = 0.0f;
    float lowVoicedEnv = 0.0f;
    float sibilanceEnv = 0.0f;
    float clusterRmsEnv = 0.0f;
    float lpVoicedState = 0.0f;
    float hpSibState = 0.0f;
    SingersFormantTelemetry lastTelemetry{};
};

} // namespace vg::human
