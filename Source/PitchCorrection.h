#pragma once
#include "PitchDetector.h"

namespace gz
{
// 音程の行き先だけを決める。波形加工やメモリ確保は行わない。
// 自然な補正は一つの音符の中で中心を追い、次の音符には古い中心を持ち越さない。
class PitchCorrection
{
public:
    void reset() noexcept
    {
        center = heldNote = candidate = 0.0f;
        candidateTime = silentTime = 0.0f;
        lastKey = lastScale = -1;
    }

    float target (float midi, float dt, int key, int scaleId, float speed, float amount) noexcept
    {
        if (! std::isfinite (midi) || midi <= 0.0f)
        {
            silentTime += dt;
            if (silentTime > 0.030f) reset();
            return 0.0f;
        }
        silentTime = 0.0f;
        const float nearest = scale::snap (midi, key, scaleId);
        const bool hard = speed <= 8.0f;
        const float margin = hard ? 0.03f : 0.09f;
        if (center <= 0.0f || key != lastKey || scaleId != lastScale)
        {
            center = midi;
            heldNote = candidate = nearest;
            candidateTime = 0.0f;
        }
        else if (nearest != heldNote
                 && std::abs (midi - nearest) + margin < std::abs (midi - heldNote))
        {
            if (candidate != nearest) { candidate = nearest; candidateTime = 0.0f; }
            candidateTime += dt;
            if (candidateTime >= (hard ? 0.004f : 0.012f) || std::abs (midi - heldNote) > 2.0f)
            {
                heldNote = nearest;
                center = midi;
                candidateTime = 0.0f;
            }
        }
        else candidateTime = 0.0f;
        lastKey = key; lastScale = scaleId;
        const float alpha = 1.0f - std::exp (-dt / 0.160f);
        center += (midi - center) * alpha;
        const float centerWeight = std::clamp ((speed - 20.0f) / 50.0f, 0.0f, 1.0f);
        const float reference = midi + centerWeight * (center - midi);
        return std::clamp (heldNote - reference, -6.0f, 6.0f) * std::clamp (amount, 0.0f, 1.0f);
    }

private:
    float center = 0.0f, heldNote = 0.0f, candidate = 0.0f;
    float candidateTime = 0.0f, silentTime = 0.0f;
    int lastKey = -1, lastScale = -1;
};
}
