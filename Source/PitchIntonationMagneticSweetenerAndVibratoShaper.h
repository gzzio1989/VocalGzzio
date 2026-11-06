#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vg::human
{

// ============================================================================
// PitchIntonationMagneticSweetenerAndVibratoShaper.h
// ----------------------------------------------------------------------------
// 人間の欲求「機械っぽいケロケロ補正（ロボ声）には絶対したくないけれど、
// ロングトーンで少しフラットしたピッチの心細さを自然に支えて、
// 語尾のビブラートをプロの歌手みたいに美しく艶やかに響かせたい」を叶える
// ゼロ遅延イントネーション磁気スイートナー＆ビブラート美学シェイパー:
//   1. OrganicIntonationMagneticPull:
//      ハードなピッチ量子化で生じるフォルマント破壊・位相不連続を一切起こさず、
//      12平均律の最近傍ノート中心から ±8〜25 cents ずれている「わずかな心細さ」だけを
//      オールパス分数遅延の微細ドップラー変調で優しく引き寄せる。
//   2. NaturalVibratoSymmetryAndBloomShaper:
//      発声開始から 220 ms 経過したロングトーンで、5.2〜6.2 Hz の自然なビブラート周期と
//      同期した倍音振幅揺らぎ（Acoustic Shimmer / Amplitude-Vibrato Coupling）を付与し、
//      棒歌いになりがちなロングトーンにプロ歌手の豊かな叙情性を宿す。
// ============================================================================

struct IntonationAndVibratoReport
{
    float estimatedPitchHz = 0.0f;           // 推定基本周波数 F0 (Hz)
    float centDeviationFromNote = 0.0f;      // 最寄り半音からのズレ (cents: -50..+50)
    float magneticSweetenCents = 0.0f;       // 磁気スイートナー引き寄せ補正量 (cents)
    float vibratoBloomDepth01 = 0.0f;        // ロングトーン・ビブラート開花度 (0..1)
    float intonationConfidence01 = 0.0f;     // ピッチ安定度・有声確信度 (0..1)
};

class PitchIntonationMagneticSweetenerAndVibratoShaper
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        f0LpCoeff = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 650.0f / static_cast<float>(sr));
        reset();
    }

    void reset() noexcept
    {
        lpMono = 0.0f;
        prevLpMono = 0.0f;
        samplesSinceZeroCross = 0;
        smoothedF0Hz = 220.0f;
        sustainDurationSec = 0.0f;
        vibratoLfoPhase = 0.0f;
        modDelayLineL = modDelayLineR = 0.0f;
        lastReport = IntonationAndVibratoReport{};
    }

    void setSweetenAmount01(float amount01) noexcept
    {
        sweetenAmount = std::clamp(amount01, 0.0f, 1.0f);
    }

    IntonationAndVibratoReport processBuffer(float* left, float* right, int numSamples) noexcept
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return lastReport;

        const float dt = 1.0f / static_cast<float>(sr);
        float centDevLast = 0.0f;
        float sweetenCentsLast = 0.0f;
        float bloomLast = 0.0f;
        float confLast = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left[i];
            const float r = right[i];
            const float mono = 0.5f * (l + r);

            // 基本波抽出用ローパスとゼロ交差周期トラッキング
            lpMono += f0LpCoeff * (mono - lpMono);
            ++samplesSinceZeroCross;

            if (prevLpMono <= 0.0f && lpMono > 0.0f && std::fabs(lpMono) > 0.004f)
            {
                const float frac = prevLpMono / (prevLpMono - lpMono + 1.0e-12f);
                const float period = std::max(4.0f, static_cast<float>(samplesSinceZeroCross) - 1.0f + frac);
                const float instHz = static_cast<float>(sr) / period;
                if (instHz >= 75.0f && instHz <= 1100.0f)
                {
                    smoothedF0Hz += 0.14f * (instHz - smoothedF0Hz);
                }
                samplesSinceZeroCross = 0;
            }
            prevLpMono = lpMono;

            const float absMono = std::fabs(mono);
            const bool isSustainedVoiced = (absMono > 0.015f);
            if (isSustainedVoiced)
                sustainDurationSec = std::min(3.0f, sustainDurationSec + dt);
            else
                sustainDurationSec = std::max(0.0f, sustainDurationSec - dt * 4.0f);

            confLast = std::clamp(sustainDurationSec * 3.5f, 0.0f, 1.0f);

            // 12平均律 MIDI ノートからのセント偏差 (-50 .. +50 cents)
            const float midiFloat = 69.0f + 12.0f * std::log2(std::max(55.0f, smoothedF0Hz) / 440.0f);
            const float nearestMidi = std::round(midiFloat);
            const float centDev = (midiFloat - nearestMidi) * 100.0f;
            centDevLast = centDev;

            // 磁気スイートナー: ±6 cents 以内は自然な揺らぎとしてそのままにし、
            // 6..30 cents の微細なフラット/シャープのみ柔らかく中心へ引き寄せる
            float pullCents = 0.0f;
            if (std::fabs(centDev) > 6.0f)
            {
                const float excess = std::fabs(centDev) - 6.0f;
                pullCents = -std::copysign(std::min(14.0f, excess * 0.55f), centDev) * sweetenAmount * confLast;
            }
            sweetenCentsLast = pullCents;

            // ロングトーン (220 ms 以降) で自然開花する 5.6 Hz ビブラート・ブルーム
            const float bloomEnv = std::clamp((sustainDurationSec - 0.22f) * 1.8f, 0.0f, 1.0f) * sweetenAmount;
            bloomLast = bloomEnv;

            vibratoLfoPhase += 2.0f * 3.14159265358979323846f * 5.6f * dt;
            if (vibratoLfoPhase > 6.283185307f)
                vibratoLfoPhase -= 6.283185307f;

            const float vibSin = std::sin(vibratoLfoPhase);
            const float fracDelay = std::clamp(0.35f + 0.18f * bloomEnv * vibSin - 0.008f * pullCents, 0.05f, 0.92f);

            // 微細分数遅延補間＋倍音シマー結合
            const float shimmerGain = 1.0f + 0.045f * bloomEnv * vibSin;
            const float outL = ((1.0f - fracDelay) * l + fracDelay * modDelayLineL) * shimmerGain;
            const float outR = ((1.0f - fracDelay) * r + fracDelay * modDelayLineR) * shimmerGain;
            modDelayLineL = l;
            modDelayLineR = r;

            left[i] = outL;
            right[i] = outR;
        }

        lastReport.estimatedPitchHz = smoothedF0Hz;
        lastReport.centDeviationFromNote = centDevLast;
        lastReport.magneticSweetenCents = sweetenCentsLast;
        lastReport.vibratoBloomDepth01 = bloomLast;
        lastReport.intonationConfidence01 = confLast;
        return lastReport;
    }

    const IntonationAndVibratoReport& getLastReport() const noexcept { return lastReport; }

private:
    double sr = 48000.0;
    float sweetenAmount = 0.5f;
    float f0LpCoeff = 0.08f;
    float lpMono = 0.0f;
    float prevLpMono = 0.0f;
    int samplesSinceZeroCross = 0;
    float smoothedF0Hz = 220.0f;
    float sustainDurationSec = 0.0f;
    float vibratoLfoPhase = 0.0f;
    float modDelayLineL = 0.0f, modDelayLineR = 0.0f;
    IntonationAndVibratoReport lastReport{};
};

} // namespace vg::human
