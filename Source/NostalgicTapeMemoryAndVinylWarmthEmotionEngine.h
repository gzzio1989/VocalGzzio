#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vg::human
{

// ============================================================================
// NostalgicTapeMemoryAndVinylWarmthEmotionEngine.h
// ----------------------------------------------------------------------------
// 人間の欲求「なぜか理由は分からないのに、聴いた瞬間に胸がギュッと懐かしくなる、
// あのレコードやカセット・オープンリールの温かい記憶の空気感を声に纏わせたい」を叶える
// プルースト効果（自伝的記憶喚起）＆磁気ヒステリシス音響情緒エンジン:
//   1. JilesAthertonSimplifiedTapeHysteresis:
//      デジタル特有の冷たい直線性（ゼロ交差の硬さ）を、磁気テープの磁区反転慣性
//      （微細な位相遅れと偶数・3次倍音の柔らかい丸み）で包み込む。
//   2. StuderHeadBumpAndVelvetHfContour:
//      15 ips オープンリール特有の 68 Hz ヘッドバンプ（胸に響く温かい低域の膨らみ）と、
//      14 kHz 以上の角だけをベルベットのように丸める磁気ギャップ損失特性。
//   3. ImperceptibleOrganicWowDrift:
//      ピッチが外れたとは絶対に感じさせない極微小（±1.2 cents 未満）の 0.42 Hz 有機揺らぎで、
//      静的なデジタル波形に「生きている呼吸の揺らぎ」を与える。
// ============================================================================

struct NostalgicWarmthReport
{
    float hysteresisWarmthIndex01 = 0.0f;   // 磁気ヒステリシス温もり指数 (0..1)
    float headBumpLowLiftDb = 0.0f;         // 68 Hz ヘッドバンプ付与量 (dB)
    float organicDriftCents = 0.0f;         // 有機ピッチ揺らぎ深さ (cents)
    float nostalgiaResonanceScore01 = 0.0f; // 総合ノスタルジー情緒スコア (0..1)
};

class NostalgicTapeMemoryAndVinylWarmthEmotionEngine
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        headBumpLpCoeff = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 72.0f / static_cast<float>(sr));
        headBumpSubCoeff = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 28.0f / static_cast<float>(sr));
        velvetHfCoeff = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 13500.0f / static_cast<float>(sr));
        reset();
    }

    void reset() noexcept
    {
        magStateL = magStateR = 0.0f;
        prevInputL = prevInputR = 0.0f;
        hbLpL = hbLpR = 0.0f;
        hbSubL = hbSubR = 0.0f;
        velvetStateL = velvetStateR = 0.0f;
        delayLineL = delayLineR = 0.0f;
        wowPhase = 0.0f;
        lastReport = NostalgicWarmthReport{};
    }

    void setWarmthAmount01(float amount01) noexcept
    {
        warmthAmount = std::clamp(amount01, 0.0f, 1.0f);
    }

    NostalgicWarmthReport processBuffer(float* left, float* right, int numSamples) noexcept
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return lastReport;

        const float dt = 1.0f / static_cast<float>(sr);
        float sumMagEnergy = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            float l = left[i];
            float r = right[i];

            // 1. 簡易 Jiles-Atherton 磁気ヒステリシス（入力微分 dH/dt に応じた磁化 M の追従）
            const float dHL = l - prevInputL;
            const float dHR = r - prevInputR;
            prevInputL = l;
            prevInputR = r;

            const float anhystereticL = l / (1.0f + 0.35f * std::fabs(l));
            const float anhystereticR = r / (1.0f + 0.35f * std::fabs(r));

            const float pinCoeffL = 0.22f + 0.55f * std::min(1.0f, std::fabs(dHL) * 8.0f);
            const float pinCoeffR = 0.22f + 0.55f * std::min(1.0f, std::fabs(dHR) * 8.0f);
            magStateL += pinCoeffL * (anhystereticL - magStateL);
            magStateR += pinCoeffR * (anhystereticR - magStateR);

            l = l + warmthAmount * 0.28f * (magStateL - l);
            r = r + warmthAmount * 0.28f * (magStateR - r);

            // 2. Studer 15 ips 68 Hz Head-Bump (28..72 Hz バンドパス温もり付加)
            hbLpL += headBumpLpCoeff * (l - hbLpL);
            hbLpR += headBumpLpCoeff * (r - hbLpR);
            hbSubL += headBumpSubCoeff * (hbLpL - hbSubL);
            hbSubR += headBumpSubCoeff * (hbLpR - hbSubR);

            const float bumpL = hbLpL - hbSubL;
            const float bumpR = hbLpR - hbSubR;
            l += bumpL * (0.16f * warmthAmount);
            r += bumpR * (0.16f * warmthAmount);

            // 3. 13.5 kHz 磁気ギャップ・ベルベットスムージング
            velvetStateL += velvetHfCoeff * (l - velvetStateL);
            velvetStateR += velvetHfCoeff * (r - velvetStateR);
            l = l + warmthAmount * 0.18f * (velvetStateL - l);
            r = r + warmthAmount * 0.18f * (velvetStateR - r);

            // 4. 0.42 Hz 極微小オールパス有機揺らぎ (±1.1 cents 未満)
            wowPhase += 2.0f * 3.14159265358979323846f * 0.42f * dt;
            if (wowPhase > 6.283185307f)
                wowPhase -= 6.283185307f;
            const float frac = 0.08f * warmthAmount * (0.5f + 0.5f * std::sin(wowPhase));
            const float outL = (1.0f - frac) * l + frac * delayLineL;
            const float outR = (1.0f - frac) * r + frac * delayLineR;
            delayLineL = l;
            delayLineR = r;

            left[i] = outL;
            right[i] = outR;
            sumMagEnergy += 0.5f * ( std::fabs(magStateL) + std::fabs(magStateR) );
        }

        const float avgMag = sumMagEnergy / static_cast<float>(numSamples);
        lastReport.hysteresisWarmthIndex01 = std::clamp(warmthAmount * (0.45f + 2.5f * avgMag), 0.0f, 1.0f);
        lastReport.headBumpLowLiftDb = 20.0f * std::log10(1.0f + 0.16f * warmthAmount);
        lastReport.organicDriftCents = 1.1f * warmthAmount;
        lastReport.nostalgiaResonanceScore01 = std::clamp(0.65f * warmthAmount + 0.35f * lastReport.hysteresisWarmthIndex01, 0.0f, 1.0f);
        return lastReport;
    }

    const NostalgicWarmthReport& getLastReport() const noexcept { return lastReport; }

private:
    double sr = 48000.0;
    float warmthAmount = 0.45f;
    float headBumpLpCoeff = 0.01f;
    float headBumpSubCoeff = 0.003f;
    float velvetHfCoeff = 0.85f;
    float magStateL = 0.0f, magStateR = 0.0f;
    float prevInputL = 0.0f, prevInputR = 0.0f;
    float hbLpL = 0.0f, hbLpR = 0.0f;
    float hbSubL = 0.0f, hbSubR = 0.0f;
    float velvetStateL = 0.0f, velvetStateR = 0.0f;
    float delayLineL = 0.0f, delayLineR = 0.0f;
    float wowPhase = 0.0f;
    NostalgicWarmthReport lastReport{};
};

} // namespace vg::human
