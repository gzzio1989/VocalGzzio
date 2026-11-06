#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vg::human
{

// ============================================================================
// DopamineAnticipationBuildAndDropClimaxEngine.h
// ----------------------------------------------------------------------------
// 人間の欲求「サビに入る直前の『来るぞ、来るぞ…！』という胸の高鳴り（予期興奮）と、
// サビが解き放たれた瞬間の脳内物質ドパミンが溢れ出すような圧倒的解放感を作りたい」を叶える
// Zatorre & Salimpoor (Nature Neuroscience, 2011) 音楽報酬系モデルに基づく演出エンジン:
//   - 音楽による快感（ドパミン放出）は2段階で起こる:
//     第1段階【尾状核 (Caudate Nucleus) 予期フェーズ】:
//       Bメロ終盤〜サビ直前のエネルギー上昇勾配（Build-up）や一瞬のブレイク無音で期待が最高潮に達する。
//       この区間ではステレオ音像をわずかに中央へ凝縮（-8%）し、中域の密度を高めて「タメ」を作る。
//     第2段階【側坐核 (Nucleus Accumbens) 報酬解放フェーズ】:
//       サビ頭の第1拍アタックが到来した瞬間、凝縮していたステレオ幅を一気に +18% パノラマ解放し、
//       12 kHz エアシマーと 85 Hz のチェスト・インパクトを同時開花させる。
// ============================================================================

struct DopamineArcTelemetry
{
    float caudateAnticipationIndex01 = 0.0f;   // 尾状核「予期・タメ」蓄積度 (0..1)
    float accumbensDropRewardIndex01 = 0.0f;   // 側坐核「サビ解放報酬」開花度 (0..1)
    float dynamicWidthModulationRatio = 1.0f;  // ステレオ幅コントラスト倍率 (0.92 .. 1.18)
    float preDropSilenceTension01 = 0.0f;      // サビ直前ブレイク（一瞬の静寂）緊張度 (0..1)
};

class DopamineAnticipationBuildAndDropClimaxEngine
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        airHpCoeff = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 11500.0f / static_cast<float>(sr));
        chestLpCoeff = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 95.0f / static_cast<float>(sr));
        reset();
    }

    void reset() noexcept
    {
        shortTermRms = 0.0f;
        longTermRms = 0.0f;
        prevLongTermRms = 0.0f;
        anticipationCharge01 = 0.0f;
        preDropPauseTension01 = 0.0f;
        dropRewardBloom01 = 0.0f;
        airHpStateL = airHpStateR = 0.0f;
        chestLpStateL = chestLpStateR = 0.0f;
        lastTelemetry = DopamineArcTelemetry{};
    }

    void setIntensity01(float intensity01) noexcept
    {
        intensity = std::clamp(intensity01, 0.0f, 1.0f);
    }

    DopamineArcTelemetry processBuffer(float* left, float* right, int numSamples) noexcept
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return lastTelemetry;

        const float dt = 1.0f / static_cast<float>(sr);
        float widthModLast = 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left[i];
            const float r = right[i];
            const float mid = 0.5f * (l + r);
            const float side = 0.5f * (l - r);
            const float absMid = std::fabs(mid);

            // 短期 (35 ms) と中期 (850 ms) のエネルギー追跡
            shortTermRms += (1.0f - std::exp(-dt / 0.035f)) * (absMid - shortTermRms);
            prevLongTermRms = longTermRms;
            longTermRms += (1.0f - std::exp(-dt / 0.850f)) * (absMid - longTermRms);

            // 1. 尾状核「ビルドアップ予期」のチャージ検出（中期エネルギーが持続的に上昇中）
            const float buildRate = (longTermRms - prevLongTermRms) * static_cast<float>(sr);
            if (buildRate > 0.008f && longTermRms > 0.025f)
            {
                anticipationCharge01 = std::min(1.0f, anticipationCharge01 + dt * 0.85f);
            }
            else
            {
                anticipationCharge01 = std::max(0.0f, anticipationCharge01 - dt * 0.25f);
            }

            // 2. サビ直前の「息を呑む一瞬のブレイク静寂 (Pre-Drop Pause)」検出
            if (anticipationCharge01 > 0.35f && shortTermRms < 0.015f)
            {
                preDropPauseTension01 = std::min(1.0f, preDropPauseTension01 + dt * 3.2f);
            }
            else
            {
                preDropPauseTension01 = std::max(0.0f, preDropPauseTension01 - dt * 1.4f);
            }

            // 3. 側坐核「サビ頭ドロップ報酬」のトリガー検出
            //    予期チャージまたはブレイク緊張が溜まった状態で急峻なアタックが到来した瞬間！
            const float combinedTension = std::max(anticipationCharge01, preDropPauseTension01);
            if (combinedTension > 0.30f && shortTermRms > longTermRms * 1.45f && shortTermRms > 0.07f)
            {
                dropRewardBloom01 = std::max(dropRewardBloom01, std::min(1.0f, combinedTension * 1.35f));
                anticipationCharge01 *= 0.4f;
                preDropPauseTension01 *= 0.4f;
            }
            else
            {
                dropRewardBloom01 = std::max(0.0f, dropRewardBloom01 - dt * 0.45f);
            }

            // ステレオ幅コントラスト:
            //   タメ区間では -8% 凝縮して緊張感を高め、ドロップ解放で +18% パノラマ展開
            const float widthFactor = 1.0f
                - 0.08f * intensity * anticipationCharge01
                + 0.18f * intensity * dropRewardBloom01;
            widthModLast = widthMod;
            widthMod = widthFactor;

            const float newSide = side * widthFactor;
            float outL = mid + newSide;
            float outR = mid - newSide;

            // ドロップ解放時の 11.5 kHz エアシマー＆95 Hz チェスト・ウォームス付加
            const float airL = outL - airHpStateL;
            airHpStateL += airHpCoeff * airL;
            const float airR = outR - airHpStateR;
            airHpStateR += airHpCoeff * airR;

            chestLpStateL += chestLpCoeff * (outL - chestLpStateL);
            chestLpStateR += chestLpCoeff * (outR - chestLpStateR);

            const float rewardGain = intensity * dropRewardBloom01;
            outL += 0.14f * rewardGain * airL + 0.09f * rewardGain * chestLpStateL;
            outR += 0.14f * rewardGain * airR + 0.09f * rewardGain * chestLpStateR;

            left[i] = outL;
            right[i] = outR;
        }

        lastTelemetry.caudateAnticipationIndex01 = anticipationCharge01;
        lastTelemetry.accumbensDropRewardIndex01 = dropRewardBloom01;
        lastTelemetry.dynamicWidthModulationRatio = widthModLast;
        lastTelemetry.preDropSilenceTension01 = preDropPauseTension01;
        return lastTelemetry;
    }

    const DopamineArcTelemetry& getLastTelemetry() const noexcept { return lastTelemetry; }

private:
    double sr = 48000.0;
    float intensity = 0.65f;
    float airHpCoeff = 0.75f;
    float chestLpCoeff = 0.012f;
    float shortTermRms = 0.0f;
    float longTermRms = 0.0f;
    float prevLongTermRms = 0.0f;
    float anticipationCharge01 = 0.0f;
    float preDropPauseTension01 = 0.0f;
    float dropRewardBloom01 = 0.0f;
    float widthMod = 1.0f;
    float airHpStateL = 0.0f, airHpStateR = 0.0f;
    float chestLpStateL = 0.0f, chestLpStateR = 0.0f;
    DopamineArcTelemetry lastTelemetry{};
};

} // namespace vg::human
