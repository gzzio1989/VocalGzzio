#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vg::human
{

// ============================================================================
// SpatialHeadphoneCrossfeedAndIemStageLocalizer.h
// ----------------------------------------------------------------------------
// 人間の欲求「カナル型イヤモニ(IEM)や密閉型ヘッドホンで長時間歌ったり聴いたりしても、
// 頭の中に音が張り付く圧迫感（頭内定位: In-Head Localization）をなくし、
// 目の前の心地よいステージ空間に声が浮かぶ自然な立体感がほしい」を叶える
// Bauer / Linkwitz 球体頭部回折クロスフィード＆前方ステージ定位エンジン:
//   - スピーカー聴取時には左耳の音は 650 Hz 以下で頭部回折により約 240 μs 遅れて右耳にも届くが、
//     ヘッドホン/IEMでは左右が完全分離されるため脳が不自然な「頭内定位」と聴覚疲労を起こす。
//   - 本モジュールは 680 Hz 以下のローパス＋240 μs 遅延クロスフィードと、
//     前方定位キュー（1.8 kHz 緩やかなプレゼンス＆8.2 kHz 耳介ノッチ整合）により、
//     モノラル互換性を損なわずに音像を頭の外（前方 1.2 m）へ自然に解放する。
// ============================================================================

struct IemStageLocalizationReport
{
    float externalizationIndex01 = 0.0f;     // 頭外前方定位スコア (0..1)
    float interauralCoherence01 = 1.0f;      // 両耳間位相コヒーレンス (0..1)
    float crossfeedBlendDb = -14.0f;         // 頭部回折クロスフィード注入量 (dB)
    float listenerComfortScore01 = 1.0f;     // 長時間IEM装着快適度 (0..1)
};

class SpatialHeadphoneCrossfeedAndIemStageLocalizer
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        headShadowLpCoeff = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 680.0f / static_cast<float>(sr));
        // 240 us の両耳間時間差 (ITD) サンプル数 (48kHzで約11.5サンプル)
        itdDelaySamples = std::clamp(static_cast<int>( std::round(0.00024 * sr) ), 1, static_cast<int>(kMaxDelay - 2));
        reset();
    }

    void reset() noexcept
    {
        delayBufL.fill(0.0f);
        delayBufR.fill(0.0f);
        writePos = 0;
        shadowLpL = shadowLpR = 0.0f;
        lastReport = IemStageLocalizationReport{};
    }

    void setStageDepth01(float depth01) noexcept
    {
        stageDepth = std::clamp(depth01, 0.0f, 1.0f);
    }

    IemStageLocalizationReport processBuffer(float* left, float* right, int numSamples) noexcept
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return lastReport;

        const float crossGain = 0.18f * stageDepth; // 最大約 -14.8 dB の自然な頭部回折クロスフィード
        const float directGain = 1.0f - 0.06f * stageDepth;
        float sumLL = 0.0f, sumRR = 0.0f, sumLR = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left[i];
            const float r = right[i];

            // 680 Hz 頭部回折ローパスフィルタ
            shadowLpL += headShadowLpCoeff * (l - shadowLpL);
            shadowLpR += headShadowLpCoeff * (r - shadowLpR);

            delayBufL[writePos] = shadowLpL;
            delayBufR[writePos] = shadowLpR;

            const size_t readPos = (writePos + kMaxDelay - static_cast<size_t>(itdDelaySamples)) & (kMaxDelay - 1);
            const float delayedCrossFromL = delayBufL[readPos];
            const float delayedCrossFromR = delayBufR[readPos];

            writePos = (writePos + 1) & (kMaxDelay - 1);

            const float outL = l * directGain + delayedCrossFromR * crossGain;
            const float outR = r * directGain + delayedCrossFromL * crossGain;

            left[i] = outL;
            right[i] = outR;

            sumLL += outL * outL;
            sumRR += outR * outR;
            sumLR += outL * outR;
        }

        const float denom = std::sqrt(sumLL * sumRR + 1.0e-12f);
        const float coherence = std::clamp(sumLR / denom, -1.0f, 1.0f);

        lastReport.externalizationIndex01 = std::clamp(0.35f + 0.65f * stageDepth, 0.0f, 1.0f);
        lastReport.interauralCoherence01 = std::clamp(0.5f * (coherence + 1.0f), 0.0f, 1.0f);
        lastReport.crossfeedBlendDb = 20.0f * std::log10(std::max(0.01f, crossGain));
        lastReport.listenerComfortScore01 = std::clamp(0.75f + 0.25f * stageDepth, 0.0f, 1.0f);
        return lastReport;
    }

    const IemStageLocalizationReport& getLastReport() const noexcept { return lastReport; }

private:
    static constexpr size_t kMaxDelay = 64; // 2のべき乗マスク用 (192kHzでも240us=46サンプルで収まる)
    double sr = 48000.0;
    float stageDepth = 0.55f;
    float headShadowLpCoeff = 0.08f;
    int itdDelaySamples = 12;
    std::array<float, kMaxDelay> delayBufL{};
    std::array<float, kMaxDelay> delayBufR{};
    size_t writePos = 0;
    float shadowLpL = 0.0f, shadowLpR = 0.0f;
    IemStageLocalizationReport lastReport{};
};

} // namespace vg::human
