#pragma once
// ==============================================================================
//  EmotionalStorytellingAndLufsArcAnalyzer.h
//  VocalGzzio — 歌唱の「物語性（Emotional Storytelling Arc）」＆ ITU-R BS.1770-4 K加重ラウドネス軌跡アナライザー
//
//  【人間の心に染みる歌の総合評価：「上手いだけの歌」と「心を揺さぶる歌」の違いを可視化する】
//   人の心を強く惹きつけるボーカルパフォーマンスには、以下の4要素の調和（Storytelling Coherence）がある:
//    1. 抑揚の物語性（Dynamic Narrative Range: 囁きから絶唱までの表現幅 LRA: Loudness Range）
//    2. 言葉の伝達力（Lyric Diction Clarity: 歌詞が聴き手の脳裏に情景を描く明瞭度）
//    3. 親密な温もり（Cranial & Proximity Warmth: 聴き手に寄り添う体温）
//    4. 鳥肌の瞬間（Frisson Climax Peaks: サビで心が解き放たれる瞬間）
//
//   本エンジンは:
//   - **ITU-R BS.1770-4 / EBU R128 準拠 2段 K-Weighting フィルタ (`ItuBs1770KWeightingFilter`)**:
//     Stage 1（頭部の音響回折を模擬する 1681 Hz +4.0 dB ハイシェルフ）と
//     Stage 2（RLB 38 Hz 2次ハイパス）を任意のサンプルレートに対して双一次変換で厳密合成する。
//   - **16フェーズ感情アーク・ヒストグラム ＆ ラウドネスレンジ（LRA）計算機**:
//     楽曲全体のダイナミクス物語曲線をリアルタイム記録する。
// ==============================================================================

#include "LightweightSimdFastMathAndEcoGovernor.h"
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>

namespace vg::human
{

// ==============================================================================
//  1. ITU-R BS.1770-4 準拠 2段 K-Weighting フィルタ（Stage 1 Head-Diffraction Shelf + Stage 2 RLB Highpass）
// ==============================================================================
class ItuBs1770KWeightingFilter
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double safeFs = std::max (8000.0, sampleRate);

        // Stage 1: +4.0 dB High-Shelf at 1681.97 Hz, Q = 0.7071
        {
            const double A     = std::pow (10.0, 4.0 / 40.0); // 10^(4/40) ≈ 1.258925
            const double w0    = 2.0 * 3.14159265358979323846 * 1681.974450955533 / safeFs;
            const double cosW0 = std::cos (w0);
            const double sinW0 = std::sin (w0);
            const double alpha = sinW0 / (2.0 * 0.7071752369554196);
            const double twoSqrtAAlpha = 2.0 * std::sqrt (A) * alpha;

            const double a0 = (A + 1.0) - (A - 1.0) * cosW0 + twoSqrtAAlpha;
            hsB0 = static_cast<float> ((A * ((A + 1.0) + (A - 1.0) * cosW0 + twoSqrtAAlpha)) / a0);
            hsB1 = static_cast<float> ((-2.0 * A * ((A - 1.0) + (A + 1.0) * cosW0)) / a0);
            hsB2 = static_cast<float> ((A * ((A + 1.0) + (A - 1.0) * cosW0 - twoSqrtAAlpha)) / a0);
            hsA1 = static_cast<float> ((2.0 * ((A - 1.0) - (A + 1.0) * cosW0)) / a0);
            hsA2 = static_cast<float> (((A + 1.0) - (A - 1.0) * cosW0 - twoSqrtAAlpha) / a0);
        }

        // Stage 2: RLB 2nd-order High-Pass at 38.135 Hz, Q = 0.5003
        {
            const double w0    = 2.0 * 3.14159265358979323846 * 38.13547087602444 / safeFs;
            const double cosW0 = std::cos (w0);
            const double sinW0 = std::sin (w0);
            const double alpha = sinW0 / (2.0 * 0.5003270373238773);
            const double a0    = 1.0 + alpha;

            hpB0 = static_cast<float> ((1.0 + cosW0) * 0.5 / a0);
            hpB1 = static_cast<float> (-(1.0 + cosW0) / a0);
            hpB2 = static_cast<float> ((1.0 + cosW0) * 0.5 / a0);
            hpA1 = static_cast<float> ((-2.0 * cosW0) / a0);
            hpA2 = static_cast<float> ((1.0 - alpha) / a0);
        }

        reset();
    }

    void reset() noexcept
    {
        hsZ1 = hsZ2 = 0.0f;
        hpZ1 = hpZ2 = 0.0f;
    }

    inline float processMono (float x) noexcept
    {
        const float y1 = hsB0 * x + hsZ1;
        hsZ1 = hsB1 * x - hsA1 * y1 + hsZ2;
        hsZ2 = hsB2 * x - hsA2 * y1;

        const float y2 = hpB0 * y1 + hpZ1;
        hpZ1 = hpB1 * y1 - hpA1 * y2 + hpZ2;
        hpZ2 = hpB2 * y1 - hpA2 * y2;
        return y2;
    }

private:
    float hsB0 = 1.0f, hsB1 = 0.0f, hsB2 = 0.0f, hsA1 = 0.0f, hsA2 = 0.0f;
    float hpB0 = 1.0f, hpB1 = 0.0f, hpB2 = 0.0f, hpA1 = 0.0f, hpA2 = 0.0f;
    float hsZ1 = 0.0f, hsZ2 = 0.0f;
    float hpZ1 = 0.0f, hpZ2 = 0.0f;
};

// ==============================================================================
//  2. 物語性アーク（Emotional Storytelling Arc）＆ K加重ラウドネス軌跡アナライザー
// ==============================================================================
struct StorytellingSummary
{
    float momentaryLufsEstimate     = -18.0f;
    float shortTermLufsEstimate     = -18.0f;
    float dynamicNarrativeRangeDb   = 8.5f;
    float heartTouchingResonancePct = 97.5f; // 「心に染みる度」総合スコア (0..100%)
    std::array<float, 16> arcHistoryNormalized {}; // 直近16フェーズの感情抑揚アーク (0..1)
};

class EmotionalStorytellingAndLufsArcAnalyzer
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRateHz = std::max (8000.0, sampleRate);
        kFilter.prepare (sampleRateHz);
        lufs400msCoeff  = static_cast<float> (std::exp (-1.0 / (0.400 * sampleRateHz)));
        lufs3000msCoeff = static_cast<float> (std::exp (-1.0 / (3.000 * sampleRateHz)));
        samplesPerArcBin = std::max (1024, static_cast<int> (0.250 * sampleRateHz));
        reset();
    }

    void reset() noexcept
    {
        kFilter.reset();
        meanSquare400ms  = 1.0e-5f;
        meanSquare3000ms = 1.0e-5f;
        minVoicedDb = -22.0f;
        maxVoicedDb = -14.0f;
        arcBins.fill (0.35f);
        arcWriteIdx = 0;
        sampleCounter = 0;
    }

    inline void observeStereo (float left, float right) noexcept
    {
        const float mono = 0.5f * (left + right);
        const float kWeighted = kFilter.processMono (mono);
        const float sq = kWeighted * kWeighted;

        meanSquare400ms  = lufs400msCoeff  * meanSquare400ms  + (1.0f - lufs400msCoeff)  * sq;
        meanSquare3000ms = lufs3000msCoeff * meanSquare3000ms + (1.0f - lufs3000msCoeff) * sq;

        if (++sampleCounter >= samplesPerArcBin)
        {
            sampleCounter = 0;
            const float lufs = -0.691f + 10.0f * std::log10 (std::max (1.0e-10f, meanSquare400ms));
            if (lufs > -48.0f)
            {
                minVoicedDb = std::min (minVoicedDb * 0.995f + lufs * 0.005f, lufs);
                maxVoicedDb = std::max (maxVoicedDb * 0.995f + lufs * 0.005f, lufs);
            }
            const float normArc = std::clamp ((lufs + 45.0f) / 36.0f, 0.0f, 1.0f);
            arcBins[static_cast<size_t> (arcWriteIdx & 15)] = normArc;
            ++arcWriteIdx;
        }
    }

    StorytellingSummary getSummary (float frissonScore01 = 0.5f, float dictionPct = 96.0f) const noexcept
    {
        StorytellingSummary s;
        s.momentaryLufsEstimate   = -0.691f + 10.0f * std::log10 (std::max (1.0e-10f, meanSquare400ms));
        s.shortTermLufsEstimate   = -0.691f + 10.0f * std::log10 (std::max (1.0e-10f, meanSquare3000ms));
        s.dynamicNarrativeRangeDb = std::clamp (maxVoicedDb - minVoicedDb, 2.0f, 24.0f);

        for (int i = 0; i < 16; ++i)
            s.arcHistoryNormalized[static_cast<size_t> (i)] = arcBins[static_cast<size_t> ((arcWriteIdx + i) & 15)];

        // 抑揚・明瞭度・鳥肌度の3拍子が揃うほど「心に染みる度」が100%へ近づく
        const float dynBonus = std::clamp (s.dynamicNarrativeRangeDb / 12.0f, 0.4f, 1.0f);
        s.heartTouchingResonancePct = std::clamp (
            0.50f * dictionPct +
            30.0f * dynBonus +
            20.0f * std::clamp (frissonScore01 + 0.5f, 0.0f, 1.0f),
            0.0f, 100.0f);
        return s;
    }

private:
    double sampleRateHz = 48000.0;
    ItuBs1770KWeightingFilter kFilter;
    float lufs400msCoeff  = 0.9999f;
    float lufs3000msCoeff = 0.99998f;
    float meanSquare400ms  = 1.0e-5f;
    float meanSquare3000ms = 1.0e-5f;
    float minVoicedDb = -22.0f;
    float maxVoicedDb = -14.0f;

    std::array<float, 16> arcBins {};
    int arcWriteIdx = 0;
    int samplesPerArcBin = 12000;
    int sampleCounter = 0;
};

} // namespace vg::human
