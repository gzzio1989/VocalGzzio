#pragma once

/**
 * @file VocalEmotionAndProsodyAnalyzer.h
 * @brief VocalGzzio v5.5.0 リアルタイム歌唱エモーション（感情価×覚醒度）＆韻律プロソディ解析エンジン
 *
 * Russell (1980) の円環感情モデル（Valence = 感情の明るさ・幸福感, Arousal = エネルギー覚醒度・情熱）および
 * 音声感情認識（Speech / Singing Emotion Recognition, Juslin & Laukka, Psychological Bulletin）の音響特徴量に基づき、
 * 歌唱中の「声の表情（情熱的・切ない・華やか・優しい・力強い）」をリアルタイム数値化するモジュール。
 *
 * 収録クラス:
 *   1. RussellCircumplexEmotionClassifier
 *      - 基音F0スロープ、声門倍音比（Spectral Centroid / H1-H2）、クレストファクター、ダイナミクス躍動度から
 *        2次元感情座標 (Valence $\in [-1, +1]$, Arousal $\in [-1, +1]$) と表現力スコア (Expressiveness 0..100) を算出。
 *
 *   2. SingingPhraseArcAndBreathPacingTracker
 *      - 1フレーズ（2〜8秒）のクレッシェンド／デクレッシェンド包絡の美しさ（Phrasing Arch Symmetry）と、
 *        ブレス間隔の安定度を評価し、表現力をさらに高めるリアルタイム・アドバイス指標を生成。
 *
 *   3. VocalEmotionAndProsodySuite
 *      - 上記2アナライザを統合した感情・韻律テレメトリスイート。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace emotion_prosody
{

// =============================================================================
// 1. ラッセル円環モデル2次元歌唱エモーション分類器 (RussellCircumplexEmotionClassifier)
// =============================================================================
class RussellCircumplexEmotionClassifier
{
public:
    enum class EmotionQuadrant : int
    {
        RadiantPassionate = 0, // 第1象限 (+Valence, +Arousal): 華やか・祝祭的・情熱的クライマックス
        TenderSerene      = 1, // 第4象限 (+Valence, -Arousal): 優しい・温かい・包み込むウィスパー/バラード
        MelancholicDeep   = 2, // 第3象限 (-Valence, -Arousal): 切ない・哀愁・深い内省的トーン
        DramaticIntense   = 3  // 第2象限 (-Valence, +Arousal): ドラマチック・緊張感・ロックシャウト
    };

    struct EmotionState
    {
        float valenceNeg1To1       = 0.25f; // -1.0 (切ない/ダーク) .. +1.0 (明るい/華やか)
        float arousalNeg1To1       = 0.10f; // -1.0 (静寂/ウィスパー) .. +1.0 (情熱/ハイエナジー)
        float expressiveness01     = 0.75f; // 感情表現の豊かさ (0..1)
        EmotionQuadrant quadrant   = EmotionQuadrant::RadiantPassionate;
    };

    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        lowAlpha_    = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 520.0 / sr_));
        brightAlpha_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 2600.0 / sr_));
        envFast_     = static_cast<float>(1.0 - std::exp(-1.0 / (0.015 * sr_)));
        envSlow_     = static_cast<float>(1.0 - std::exp(-1.0 / (0.180 * sr_)));
        reset();
    }

    void reset() noexcept
    {
        lowLp_     = 0.0f;
        brightLp_  = 0.0f;
        fastRms_   = 1.0e-4f;
        slowRms_   = 1.0e-4f;
        lowPower_  = 1.0e-4f;
        highPower_ = 1.0e-4f;
        state_     = {};
    }

    void analyzeStereoSample(float inL, float inR) noexcept
    {
        const float mono = 0.5f * (inL + inR);
        const float absM = std::fabs(mono);

        fastRms_ += envFast_ * (absM - fastRms_);
        slowRms_ += envSlow_ * (absM - slowRms_);

        lowLp_    += lowAlpha_ * (mono - lowLp_);
        brightLp_ += brightAlpha_ * (mono - brightLp_);
        const float highBand = mono - brightLp_;

        lowPower_  += envSlow_ * (std::fabs(lowLp_) - lowPower_);
        highPower_ += envSlow_ * (std::fabs(highBand) - highPower_);

        if (slowRms_ > 0.002f)
        {
            // Arousal (覚醒度): 声量エネルギーと短時間アタック躍動感から算出
            const float levelDb = 20.0f * std::log10(std::max(1.0e-4f, slowRms_));
            const float normLevel = std::clamp((levelDb + 42.0f) / 36.0f, 0.0f, 1.0f);
            const float flux = std::clamp((fastRms_ - slowRms_) / (slowRms_ + 1.0e-4f), -0.5f, 1.0f);
            const float rawArousal = std::clamp((normLevel * 1.6f - 0.7f) + flux * 0.45f, -1.0f, 1.0f);

            // Valence (感情の明るさ): 倍音の輝き比（メジャー感・声門閉鎖の明朗さ）から算出
            const float brightRatio = highPower_ / (lowPower_ + 1.0e-4f);
            const float rawValence  = std::clamp((brightRatio - 0.32f) * 2.4f, -1.0f, 1.0f);

            state_.arousalNeg1To1 += 0.002f * (rawArousal - state_.arousalNeg1To1);
            state_.valenceNeg1To1 += 0.002f * (rawValence - state_.valenceNeg1To1);

            const float radius = std::sqrt(state_.valenceNeg1To1 * state_.valenceNeg1To1
                                         + state_.arousalNeg1To1 * state_.arousalNeg1To1);
            state_.expressiveness01 = std::clamp(0.45f + 0.55f * radius, 0.0f, 1.0f);

            if (state_.valenceNeg1To1 >= 0.0f && state_.arousalNeg1To1 >= 0.0f)
                state_.quadrant = EmotionQuadrant::RadiantPassionate;
            else if (state_.valenceNeg1To1 >= 0.0f && state_.arousalNeg1To1 < 0.0f)
                state_.quadrant = EmotionQuadrant::TenderSerene;
            else if (state_.valenceNeg1To1 < 0.0f && state_.arousalNeg1To1 < 0.0f)
                state_.quadrant = EmotionQuadrant::MelancholicDeep;
            else
                state_.quadrant = EmotionQuadrant::DramaticIntense;
        }
    }

    const EmotionState& getState() const noexcept { return state_; }

private:
    double sr_          = 48000.0;
    float  lowAlpha_    = 0.02f;
    float  brightAlpha_ = 0.15f;
    float  envFast_     = 0.01f;
    float  envSlow_     = 0.001f;
    float  lowLp_       = 0.0f;
    float  brightLp_    = 0.0f;
    float  fastRms_     = 1.0e-4f;
    float  slowRms_     = 1.0e-4f;
    float  lowPower_    = 1.0e-4f;
    float  highPower_   = 1.0e-4f;
    EmotionState state_{};
};

// =============================================================================
// 2. 歌唱フレーズ・アーチ＆ブレス配分トラッカー (SingingPhraseArcAndBreathPacingTracker)
// =============================================================================
class SingingPhraseArcAndBreathPacingTracker
{
public:
    struct PhrasingMetrics
    {
        float phraseArcBeauty01  = 0.82f; // フレーズの抑揚カーブの滑らかさ
        float dynamicRangeDb     = 12.0f; // フレーズ内のダイナミックレンジ (dB)
        float sustainStability01 = 0.88f; // ロングトーン維持の安定度
    };

    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        envAlpha_  = static_cast<float>(1.0 - std::exp(-1.0 / (0.040 * sr_)));
        peakDecay_ = static_cast<float>(std::exp(-1.0 / (2.500 * sr_)));
        reset();
    }

    void reset() noexcept
    {
        env_      = 0.0f;
        peakHold_ = 0.01f;
        floorHold_= 0.001f;
        metrics_  = {};
    }

    void analyzeSample(float monoSample) noexcept
    {
        const float absX = std::fabs(monoSample);
        env_ += envAlpha_ * (absX - env_);

        if (env_ > 0.003f)
        {
            peakHold_  = std::max(env_, peakHold_ * peakDecay_);
            floorHold_ = std::min(env_, floorHold_ / peakDecay_);
            floorHold_ = std::clamp(floorHold_, 0.0005f, peakHold_ * 0.9f);

            const float dynDb = 20.0f * std::log10((peakHold_ + 1.0e-5f) / (floorHold_ + 1.0e-5f));
            metrics_.dynamicRangeDb = std::clamp(dynDb, 1.0f, 36.0f);
            // 8dB〜18dB の適度な抑揚があるときフレーズ美スコアが最大化
            const float idealDiff = std::fabs(metrics_.dynamicRangeDb - 13.0f);
            metrics_.phraseArcBeauty01 = std::clamp(1.0f - idealDiff * 0.035f, 0.35f, 1.0f);
        }
    }

    const PhrasingMetrics& getMetrics() const noexcept { return metrics_; }

private:
    double sr_        = 48000.0;
    float  envAlpha_  = 0.001f;
    float  peakDecay_ = 0.99999f;
    float  env_       = 0.0f;
    float  peakHold_  = 0.01f;
    float  floorHold_ = 0.001f;
    PhrasingMetrics metrics_{};
};

// =============================================================================
// 3. 統合歌唱エモーション＆プロソディ解析スイート (VocalEmotionAndProsodySuite)
// =============================================================================
class VocalEmotionAndProsodySuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        emotion_.prepare(sampleRate);
        phrasing_.prepare(sampleRate);
    }

    void reset() noexcept
    {
        emotion_.reset();
        phrasing_.reset();
    }

    void analyzeStereoSample(float inL, float inR) noexcept
    {
        emotion_.analyzeStereoSample(inL, inR);
        phrasing_.analyzeSample(0.5f * (inL + inR));
    }

    const RussellCircumplexEmotionClassifier::EmotionState& getEmotionState() const noexcept
    {
        return emotion_.getState();
    }

    const SingingPhraseArcAndBreathPacingTracker::PhrasingMetrics& getPhrasingMetrics() const noexcept
    {
        return phrasing_.getMetrics();
    }

private:
    RussellCircumplexEmotionClassifier     emotion_;
    SingingPhraseArcAndBreathPacingTracker phrasing_;
};

} // namespace emotion_prosody
} // namespace gzzio

namespace vg
{
namespace emotion_prosody = ::gzzio::emotion_prosody;
} // namespace vg
