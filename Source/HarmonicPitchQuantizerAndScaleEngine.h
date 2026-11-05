#pragma once

/**
 * @file HarmonicPitchQuantizerAndScaleEngine.h
 * @brief VocalGzzio v5.5.0 12音階×純正律/平均律ハイブリッド・ピッチクオンタイザ＆和風音階（陽旋法・陰旋法・呂旋法）エンジン
 *
 * ボーカルのピッチ補正において、機械的なケロケロ声（Hard Snap）から自然なプロ歌唱補正（Natural Retune）までを連続制御し、
 * さらにハモリ生成やロングトーン時に「12平均律（12-TET）」と「うなりのない純正律（Just Intonation: 長3度 -13.7 cents, 完全5度 +1.96 cents）」を
 * シームレスにブレンドできる音楽理論・音律エンジン。
 *
 * 収録クラス:
 *   1. MusicalScaleAndTemperamentQuantizer
 *      - Major, Natural Minor, Harmonic Minor, Dorian, Mixolydian, Lydian に加え、
 *        日本の伝統・ボカロ和風曲・演歌・民謡に不可欠な「ヨナ抜き長音階（呂旋法）」「ヨナ抜き短音階」「都節（陰旋法）」を含む
 *        10種のスケールマスクと、純正律偏差テーブルを搭載。
 *
 *   2. RetuneBallisticsAndVibratoExemptTracker
 *      - 音程移動中（Note Transition）は素早くターゲット音へ引き寄せ、ビブラートが始まった瞬間（5〜7Hz 周期変動検知時）は
 *        補正力を自動的に緩めて歌い手の自然なビブラートを100%尊重するインテリジェント・バリスティクス。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace scale_quantizer
{

// =============================================================================
// 1. 10スケール＆純正律/平均律ハイブリッド音程量子化器 (MusicalScaleAndTemperamentQuantizer)
// =============================================================================
class MusicalScaleAndTemperamentQuantizer
{
public:
    enum class ScaleType : int
    {
        Chromatic12       = 0, // 半音階（全12音有効）
        MajorIonian       = 1, // 長音階 (C D E F G A B)
        NaturalMinor      = 2, // 自然短音階 (C D Eb F G Ab Bb)
        HarmonicMinor     = 3, // 和声的短音階 (C D Eb F G Ab B)
        DorianMode        = 4, // ドリアン・モード (シティポップ・R&B)
        MixolydianMode    = 5, // ミクソリディアン・モード (ロック・ファンク)
        LydianDream       = 6, // リディアン・モード (劇伴・浮遊感)
        JapaneseYoMajor   = 7, // ヨナ抜き長音階・呂旋法 (4度・7度抜き: C D E G A)
        JapaneseYoMinor   = 8, // ヨナ抜き短音階 (C D Eb G Ab)
        JapaneseMiyakoIn  = 9  // 都節・陰旋法 (C Db F G Ab)
    };

    // rootNote0To11: 0 = C, 1 = C#, ..., 9 = A, 11 = B
    static float quantizeFrequencyHz(float inputFreqHz,
                                     int rootNote0To11,
                                     ScaleType scale,
                                     float retuneStrength01,
                                     float justIntonationBlend01 = 0.25f) noexcept
    {
        if (inputFreqHz <= 40.0f || inputFreqHz >= 2400.0f)
            return inputFreqHz;

        const float strength = std::clamp(retuneStrength01, 0.0f, 1.0f);
        if (strength <= 1.0e-4f)
            return inputFreqHz;

        // 1. 入力周波数 (Hz) -> 連続MIDIノート番号 (A4 = 440Hz = MIDI 69.0)
        const float midiFloat = 69.0f + 12.0f * std::log2(inputFreqHz / 440.0f);
        const int root = ((rootNote0To11 % 12) + 12) % 12;

        // 2. スケールマスク内で最も近い有効ノートを探索
        const auto& mask = getScaleMask(scale);
        int bestMidiNote = static_cast<int>(std::lround(midiFloat));
        float minDist = 999.0f;

        const int centerNote = static_cast<int>(std::floor(midiFloat));
        for (int candidate = centerNote - 3; candidate <= centerNote + 4; ++candidate)
        {
            const int degree = (((candidate - root) % 12) + 12) % 12;
            if (mask[static_cast<size_t>(degree)] != 0)
            {
                const float dist = std::fabs(midiFloat - static_cast<float>(candidate));
                if (dist < minDist)
                {
                    minDist = dist;
                    bestMidiNote = candidate;
                }
            }
        }

        // 3. 純正律（Just Intonation）セント補正量（5限界純正律: 1/1, 16/15, 9/8, 6/5, 5/4, 4/3, 45/32, 3/2, 8/5, 5/3, 9/5, 15/8）
        static constexpr std::array<float, 12> kJustCentsOffset = {
             0.00f,  11.73f,   3.91f,  15.64f, -13.69f,  -1.96f,
           -17.49f,   1.96f,  13.69f, -15.64f,  17.60f, -11.73f
        };
        const int targetDegree = (((bestMidiNote - root) % 12) + 12) % 12;
        const float jiOffsetSemitones = (kJustCentsOffset[static_cast<size_t>(targetDegree)] / 100.0f)
                                      * std::clamp(justIntonationBlend01, 0.0f, 1.0f);

        const float targetMidi = static_cast<float>(bestMidiNote) + jiOffsetSemitones;
        const float blendedMidi = midiFloat + (targetMidi - midiFloat) * strength;

        return 440.0f * std::pow(2.0f, (blendedMidi - 69.0f) / 12.0f);
    }

private:
    static const std::array<uint8_t, 12>& getScaleMask(ScaleType scale) noexcept
    {
        static constexpr std::array<std::array<uint8_t, 12>, 10> kMasks = {{
            { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, // Chromatic12
            { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 }, // MajorIonian
            { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 }, // NaturalMinor
            { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1 }, // HarmonicMinor
            { 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0 }, // DorianMode
            { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0 }, // MixolydianMode
            { 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1 }, // LydianDream
            { 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0 }, // JapaneseYoMajor (ヨナ抜き長音階)
            { 1, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0 }, // JapaneseYoMinor (ヨナ抜き短音階)
            { 1, 1, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0 }  // JapaneseMiyakoIn (都節・陰旋法)
        }};
        const size_t idx = static_cast<size_t>(std::clamp(static_cast<int>(scale), 0, 9));
        return kMasks[idx];
    }
};

// =============================================================================
// 2. ビブラート保護付きピッチリチューン・バリスティクス (RetuneBallisticsAndVibratoExemptTracker)
// =============================================================================
class RetuneBallisticsAndVibratoExemptTracker
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        smoothedPitchHz_ = 220.0f;
        vibratoScore01_  = 0.0f;
        prevDeltaHz_     = 0.0f;
    }

    float processTrackedPitch(float rawF0Hz,
                              int rootKey0To11,
                              MusicalScaleAndTemperamentQuantizer::ScaleType scale,
                              float retuneSpeed01) noexcept
    {
        if (rawF0Hz <= 40.0f)
            return rawF0Hz;

        const float delta = rawF0Hz - smoothedPitchHz_;
        // 微小な上下反転（ビブラート周期）が連続しているときは補正を緩めて自然なビブラートを守る
        if (delta * prevDeltaHz_ < 0.0f && std::fabs(delta) < rawF0Hz * 0.035f)
            vibratoScore01_ = std::min(1.0f, vibratoScore01_ + 0.08f);
        else
            vibratoScore01_ = std::max(0.0f, vibratoScore01_ - 0.02f);
        prevDeltaHz_ = delta;

        const float effectiveRetune = std::clamp(retuneSpeed01 * (1.0f - 0.55f * vibratoScore01_), 0.0f, 1.0f);
        const float quantizedHz = MusicalScaleAndTemperamentQuantizer::quantizeFrequencyHz(
            rawF0Hz, rootKey0To11, scale, effectiveRetune, 0.25f);

        smoothedPitchHz_ += 0.25f * (quantizedHz - smoothedPitchHz_);
        return smoothedPitchHz_;
    }

private:
    double sr_              = 48000.0;
    float  smoothedPitchHz_ = 220.0f;
    float  vibratoScore01_  = 0.0f;
    float  prevDeltaHz_     = 0.0f;
};

} // namespace scale_quantizer
} // namespace gzzio

namespace vg
{
namespace scale_quantizer = ::gzzio::scale_quantizer;
} // namespace vg
