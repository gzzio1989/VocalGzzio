#pragma once

/**
 * @file ConcertStageLightingAndSnsSync.h
 * @brief VocalGzzio v5.5.0 4大ライブSE連動・コンサートステージ照明コレオグラフィ＆OBS/VTuber演出テレメトリエンジン
 *
 * 4大ライブSEパッド（♪拍手 / ✦キラ / ●ドラム / ◆ベル）のトリガーおよびリアルタイム歌唱ダイナミクスに完全同期し、
 * UI上のスイス時計ダイヤル照明・ステージハロー演出・OBS/VTuber配信オーバーレイ向けの
 * RGBW ステージ照明カラー＆ストロボ・コレオグラフィ（振付）を生成するモジュール。
 *
 * 収録クラス:
 *   1. LiveSeStageLightingChoreographer
 *      - SE 0 (♪拍手): ゴールデン・カーテンコール（2800K タングステン・ウォームゴールドのスポットライト揺らぎ）
 *      - SE 1 (✦キラ): オーロラ・プリズムシャワー（シアン・サファイア・プラチナホワイトの星屑チェイサー）
 *      - SE 2 (●ドラム): ティンパニ＆スネアロールの高速パルス点滅 → 1.26秒フィナーレ瞬間のブラインダー・フルフラッシュ爆発
 *      - SE 3 (◆ベル): 大聖堂アンバー＆ロイヤルブルーの荘厳な3ステップ・チャイムグロー
 *
 *   2. VocalClimaxPyroAndConfettiDirector
 *      - サビの高音ロングトーン＋高Arousal状態が1.5秒以上持続した際に、ステージ銀テープ（Confetti）や
 *        スパークラー演出トリガーフラグを自動発行するクライマックス・ディレクター。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace stage_lighting
{

// =============================================================================
// 1. 4大ライブSE連動ステージ照明コレオグラファー (LiveSeStageLightingChoreographer)
// =============================================================================
class LiveSeStageLightingChoreographer
{
public:
    struct StageLightFrame
    {
        float keyLightR01      = 0.83f;
        float keyLightG01      = 0.68f;
        float keyLightB01      = 0.22f;
        float rimLightR01      = 0.22f;
        float rimLightG01      = 0.74f;
        float rimLightB01      = 0.97f;
        float strobeIntensity01 = 0.0f;
        float curtainGlow01    = 0.0f;
    };

    static StageLightFrame evaluateSeChoreography(int activeSeIndex,
                                                  float elapsedSec,
                                                  float vocalEnvelope01) noexcept
    {
        StageLightFrame frame{};
        const float vEnv = std::clamp(vocalEnvelope01, 0.0f, 1.0f);
        frame.curtainGlow01 = 0.20f + 0.55f * vEnv;

        if (activeSeIndex < 0 || elapsedSec < 0.0f)
            return frame;

        constexpr float kTwoPi = 6.28318530717958647692f;

        switch (activeSeIndex)
        {
            case 0: // ♪拍手: ゴールデン・カーテンコール＆温かいフットライト
            {
                const float env = std::exp(-0.65f * elapsedSec);
                const float wave = 0.5f + 0.5f * std::sin(kTwoPi * 3.2f * elapsedSec);
                frame.keyLightR01 = 0.98f;
                frame.keyLightG01 = 0.78f + 0.12f * wave;
                frame.keyLightB01 = 0.32f;
                frame.rimLightR01 = 0.95f;
                frame.rimLightG01 = 0.58f;
                frame.rimLightB01 = 0.20f;
                frame.strobeIntensity01 = 0.25f * env * wave;
                frame.curtainGlow01 = std::clamp(0.45f + 0.55f * env, 0.0f, 1.0f);
                break;
            }
            case 1: // ✦キラ: クリスタル・オーロラ＆プリズムレーザー
            {
                const float env = std::exp(-0.85f * elapsedSec);
                const float shimmer = 0.5f + 0.5f * std::sin(kTwoPi * 11.5f * elapsedSec);
                frame.keyLightR01 = 0.45f + 0.50f * shimmer;
                frame.keyLightG01 = 0.88f;
                frame.keyLightB01 = 1.00f;
                frame.rimLightR01 = 0.82f;
                frame.rimLightG01 = 0.92f;
                frame.rimLightB01 = 1.00f;
                frame.strobeIntensity01 = 0.65f * env * shimmer;
                frame.curtainGlow01 = std::clamp(0.50f + 0.50f * env, 0.0f, 1.0f);
                break;
            }
            case 2: // ●ドラム: 0〜1.25秒ロール加速チェイサー → 1.26秒ブラインダー大爆発
            {
                if (elapsedSec < 1.25f)
                {
                    const float progress = elapsedSec / 1.25f;
                    const float rollPulse = 0.5f + 0.5f * std::sin(kTwoPi * (8.0f + 16.0f * progress) * elapsedSec);
                    frame.keyLightR01 = 0.92f * progress;
                    frame.keyLightG01 = 0.35f * progress;
                    frame.keyLightB01 = 0.15f;
                    frame.strobeIntensity01 = (0.20f + 0.70f * progress) * rollPulse;
                    frame.curtainGlow01 = 0.30f + 0.65f * progress;
                }
                else
                {
                    const float afterFinale = elapsedSec - 1.25f;
                    const float flashEnv = std::exp(-2.4f * afterFinale);
                    frame.keyLightR01 = 1.00f;
                    frame.keyLightG01 = 0.94f;
                    frame.keyLightB01 = 0.72f;
                    frame.rimLightR01 = 1.00f;
                    frame.rimLightG01 = 0.75f;
                    frame.rimLightB01 = 0.28f;
                    frame.strobeIntensity01 = std::clamp(flashEnv * 1.15f, 0.0f, 1.0f);
                    frame.curtainGlow01 = std::clamp(0.35f + 0.65f * flashEnv, 0.0f, 1.0f);
                }
                break;
            }
            case 3: // ◆ベル: 3ステップ・カリヨンベル・ロイヤルアンバー発光
            default:
            {
                const float stepPulse = (elapsedSec < 0.24f) ? std::exp(-6.0f * elapsedSec)
                                      : (elapsedSec < 0.50f) ? std::exp(-6.0f * (elapsedSec - 0.24f))
                                                             : std::exp(-1.5f * (elapsedSec - 0.50f));
                frame.keyLightR01 = 0.94f;
                frame.keyLightG01 = 0.76f;
                frame.keyLightB01 = 0.38f;
                frame.rimLightR01 = 0.32f;
                frame.rimLightG01 = 0.62f;
                frame.rimLightB01 = 0.98f;
                frame.strobeIntensity01 = std::clamp(stepPulse * 0.75f, 0.0f, 1.0f);
                frame.curtainGlow01 = std::clamp(0.35f + 0.60f * stepPulse, 0.0f, 1.0f);
                break;
            }
        }

        return frame;
    }
};

// =============================================================================
// 2. クライマックス銀テープ＆スパークラー演出ディレクター (VocalClimaxPyroAndConfettiDirector)
// =============================================================================
class VocalClimaxPyroAndConfettiDirector
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        envAlpha_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.080 * sr_)));
        reset();
    }

    void reset() noexcept
    {
        sustainedHighEnergySec_ = 0.0f;
        cooldownSec_            = 0.0f;
        env_                    = 0.0f;
        confettiActive_         = false;
    }

    void analyzeStereoSample(float inL, float inR) noexcept
    {
        const float dt = static_cast<float>(1.0 / sr_);
        const float absIn = 0.5f * (std::fabs(inL) + std::fabs(inR));
        env_ += envAlpha_ * (absIn - env_);

        if (cooldownSec_ > 0.0f)
            cooldownSec_ = std::max(0.0f, cooldownSec_ - dt);

        if (env_ > 0.22f)
            sustainedHighEnergySec_ += dt;
        else
            sustainedHighEnergySec_ = std::max(0.0f, sustainedHighEnergySec_ - dt * 2.5f);

        if (sustainedHighEnergySec_ >= 1.6f && cooldownSec_ <= 0.0f)
        {
            confettiActive_ = true;
            cooldownSec_    = 8.0f;
        }
        else if (cooldownSec_ < 6.0f)
        {
            confettiActive_ = false;
        }
    }

    bool isConfettiTriggered() const noexcept { return confettiActive_; }

private:
    double sr_                     = 48000.0;
    float  envAlpha_               = 0.001f;
    float  sustainedHighEnergySec_ = 0.0f;
    float  cooldownSec_            = 0.0f;
    float  env_                    = 0.0f;
    bool   confettiActive_         = false;
};

} // namespace stage_lighting
} // namespace gzzio

namespace vg
{
namespace stage_lighting = ::gzzio::stage_lighting;
} // namespace vg
