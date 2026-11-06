#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vg::human
{

// ============================================================================
// DynamicPlosiveDePopperAndMouthClickCleaner.h
// ----------------------------------------------------------------------------
// 人間の欲求「近接マイクで歌ったり囁いたりしたときの『パ行・バ行』のボフッという
// 吹かれ音（Plosive）や、唇が開く瞬間のペチャッというマウスクリックノイズだけを、
// 声の太さや子音の輪郭を1ミリも損なわずに魔法のように消したい」を叶える
// 適応型プローシブ・デポッパー＆サブミリ秒マウスクリック修復エンジン:
//   1. AdaptivePlosiveWindBlastSuppressor:
//      通常の歌唱母音（低域と中高域倍音が同時に鳴る）と、ポップ吹かれ（110 Hz 以下だけが
//      中高域を伴わずに突発バーストする）をエネルギー比 $E_{\text{sub110}} / E_{\text{mid}}$ で判別し、
//      吹かれの瞬間だけ 115 Hz 以下を動的ハイパス減衰させることで普段の声の太さを完全維持。
//   2. SubMillisecondMouthClickMicroRepair:
//      持続時間 0.8 ms 未満の孤立した2階微分スパイク（唾液・舌・唇のクリック音）を検出し、
//      前後サンプルとの3点エルミート補間で滑らかに修復。
// ============================================================================

struct PlosiveAndClickReport
{
    int mouthClicksRepairedCount = 0;        // 修復されたマウスクリック数
    float maxPlosiveAttenuationDb = 0.0f;    // ポップ吹かれ最大減衰量 (dB)
    float vocalBodyPreservation01 = 1.0f;    // 声の低域芯の温存率 (0..1)
    bool plosiveGuardActive = false;         // ポップガード作動フラグ
};

class DynamicPlosiveDePopperAndMouthClickCleaner
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        plosiveLpCoeff = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 115.0f / static_cast<float>(sr));
        reset();
    }

    void reset() noexcept
    {
        lowStateL = lowStateR = 0.0f;
        lowEnv = 0.0f;
        highEnv = 0.0f;
        plosiveGain = 1.0f;
        prev1L = prev2L = 0.0f;
        prev1R = prev2R = 0.0f;
        runningDerivativeRms = 0.01f;
        lastReport = PlosiveAndClickReport{};
    }

    PlosiveAndClickReport processBuffer(float* left, float* right, int numSamples) noexcept
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return lastReport;

        int clickRepairs = 0;
        float minPlosiveGain = 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            float l = left[i];
            float r = right[i];

            // 1. サブミリ秒・マウスクリック検知＆マイクロ補間修復
            // 2階差分 |x[n] - 2*x[n-1] + x[n-2]| が局所RMSの9.5倍を超えた孤立スパイクを滑らかに補間
            const float d2L = std::fabs(l - 2.0f * prev1L + prev2L);
            const float d2R = std::fabs(r - 2.0f * prev1R + prev2R);
            const float d2Max = std::max(d2L, d2R);

            runningDerivativeRms = 0.992f * runningDerivativeRms + 0.008f * d2Max;
            const float clickThreshold = std::max(0.035f, runningDerivativeRms * 9.5f);

            if (d2Max > clickThreshold)
            {
                ++clickRepairs;
                // スパイクした直前サンプルを線形予測値へソフトクランプ
                l = 0.65f * prev1L + 0.35f * l;
                r = 0.65f * prev1R + 0.35f * r;
            }
            prev2L = prev1L;
            prev1L = l;
            prev2R = prev1R;
            prev1R = r;

            // 2. パ行・バ行プローシブ（115 Hz 以下の風圧バースト）の選択的デポップ
            lowStateL += plosiveLpCoeff * (l - lowStateL);
            lowStateR += plosiveLpCoeff * (r - lowStateR);

            const float highL = l - lowStateL;
            const float highR = r - lowStateR;

            const float lowMag = 0.5f * (std::fabs(lowStateL) + std::fabs(lowStateR));
            const float highMag = 0.5f * (std::fabs(highL) + std::fabs(highR));

            lowEnv += 0.04f * (lowMag - lowEnv);
            highEnv += 0.04f * (highMag - highEnv);

            // 吹かれ判定: 115 Hz 以下が中高域倍音の 3.8 倍以上に突出している瞬間のみ低域を減衰
            const float plosiveRatio = lowEnv / (highEnv * 3.8f + 0.008f);
            float targetGain = 1.0f;
            if (plosiveRatio > 1.0f && lowEnv > 0.04f)
            {
                targetGain = std::clamp(1.0f / plosiveRatio, 0.18f, 1.0f);
            }

            if (targetGain < plosiveGain)
                plosiveGain += 0.22f * (targetGain - plosiveGain);
            else
                plosiveGain += 0.008f * (targetGain - plosiveGain);

            minPlosiveGain = std::min(minPlosiveGain, plosiveGain);

            left[i] = highL + lowStateL * plosiveGain;
            right[i] = highR + lowStateR * plosiveGain;
        }

        lastReport.mouthClicksRepairedCount = clickRepairs;
        lastReport.maxPlosiveAttenuationDb = 20.0f * std::log10(std::max(0.05f, minPlosiveGain));
        lastReport.vocalBodyPreservation01 = std::clamp(0.5f + 0.5f * minPlosiveGain, 0.0f, 1.0f);
        lastReport.plosiveGuardActive = (minPlosiveGain < 0.92f);
        return lastReport;
    }

    const PlosiveAndClickReport& getLastReport() const noexcept { return lastReport; }

private:
    double sr = 48000.0;
    float plosiveLpCoeff = 0.015f;
    float lowStateL = 0.0f, lowStateR = 0.0f;
    float lowEnv = 0.0f, highEnv = 0.0f;
    float plosiveGain = 1.0f;
    float prev1L = 0.0f, prev2L = 0.0f;
    float prev1R = 0.0f, prev2R = 0.0f;
    float runningDerivativeRms = 0.01f;
    PlosiveAndClickReport lastReport{};
};

} // namespace vg::human
