#pragma once

/**
 * @file HorologyInteractiveDeck.h
 * @brief VocalGzzio 宵祭ランタン粒子物理・トゥールビヨン脱進機ビジュアライザ・グラミーシグナルチェインストリップ＆IEEE機械式VUバリスティック計器
 *
 * 【デザイン哲学と独自UI/UX設計】
 * 1. YoimatsuriParticleFieldEngine (宵祭ランタン火の粉＆花火ボケ味オーディオリアクティブ物理エンジン):
 *    - ユーザー指定のカラーリファレンス画像 (深い夜空の藍 #040B1E、無数の温かい提灯アンバー #FFAA2B / #FFD875、
 *      遠くに咲く花火コーラル #FF6E8E と星明かりシアン #3CE8D5) の空気感を、
 *      単なる静止画ではなく「歌声の倍音と呼吸に呼応してゆらめく決定論的パーティクル物理」として実装。
 *
 * 2. TourbillonBalanceWheelVisualizer (スイス高級時計トゥールビヨン脱進機＆ヒゲゼンマイ呼吸インジケーター):
 *    - 歌手のリアルタイム基本周波数 (F0) と音程ロック度 (Pitch Lock Score) に連動して、
 *      ブレゲひげゼンマイ (Breguet Overcoil Hairspring) が呼吸し、人工ルビー軸受 (#E84A6F) と
 *      面取り真鍮ブリッジが精密に回転する、世界に一つだけの「触りたくなる機械式音響計器」を描画します。
 *
 * 3. AnalogVuBallisticMeterRenderer (ANSI C16.5 / IEEE 300ms 機械式VUメーター物理＆パララックス影描画):
 *    - 300ms 立ち上がり・1.2% オーバーシュートの2次減衰振り子微分方程式によって、
 *      本物のアナログコンソール VU 針の質量感と慣性を再現します。
 *
 * 4. GrammyMasteringChainFlowStrip (6段グラミー・マスタリングチェイン・ライブテレメトリバー):
 *    - [1073 Pre] -> [Weiss De-Ess] -> [Pultec EQP] -> [Serban Comp] -> [Bricasti Space] -> [FIR TruePeak] の
 *      各ステージの稼働量とルビー/アンバーのジュエルインジケーターを一目で把握できるシグナルストリップです。
 */

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "HorologyTactileUi.h"

namespace vg::horology_deck
{

//==============================================================================
// 1. 宵祭 (Yoimatsuri) ランタン＆花火ボケ味オーディオリアクティブ物理エンジン
//==============================================================================
class YoimatsuriParticleFieldEngine
{
public:
    static constexpr int kNumEmbers = 24;

    struct LanternEmber
    {
        float x01 = 0.5f;
        float y01 = 0.5f;
        float radiusPx = 12.0f;
        float phase = 0.0f;
        float driftSpeed = 0.02f;
        uint8_t colorType = 0; // 0: Gold Lantern, 1: Firework Coral, 2: Starlight Cyan
    };

    YoimatsuriParticleFieldEngine() noexcept
    {
        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < kNumEmbers; ++i)
        {
            auto& e = embers[static_cast<size_t>(i)];
            const float fi = static_cast<float>(i);
            e.x01 = std::fmod(0.07f + fi * 0.6180339887f, 1.0f);
            e.y01 = std::fmod(0.13f + fi * 0.3819660112f, 1.0f);
            e.radiusPx = 8.0f + std::fmod(fi * 7.3f, 26.0f);
            e.phase = fi * 0.45f;
            e.driftSpeed = 0.008f + std::fmod(fi * 0.0037f, 0.018f);
            e.colorType = static_cast<uint8_t>((i % 5 == 0) ? 1 : ((i % 7 == 0) ? 2 : 0));
        }
        animTimeSec = 0.0f;
        smoothedVocalEnergy = 0.0f;
        smoothedHarmonicBloom = 0.0f;
    }

    void stepPhysics(float dtSeconds, float vocalLevel01, float harmonicBloom01) noexcept
    {
        const float dt = std::clamp(dtSeconds, 0.001f, 0.100f);
        animTimeSec += dt;
        smoothedVocalEnergy += 0.12f * (std::clamp(vocalLevel01, 0.0f, 1.0f) - smoothedVocalEnergy);
        smoothedHarmonicBloom += 0.10f * (std::clamp(harmonicBloom01, 0.0f, 1.0f) - smoothedHarmonicBloom);

        const float speedBoost = 1.0f + smoothedVocalEnergy * 1.4f;
        for (auto& e : embers)
        {
            e.y01 -= e.driftSpeed * speedBoost * dt;
            if (e.y01 < -0.08f)
                e.y01 += 1.16f;

            e.phase += dt * (0.8f + smoothedHarmonicBloom * 1.2f);
            if (e.phase > 62.831853f)
                e.phase -= 62.831853f;
        }
    }

    void drawField(juce::Graphics& g, juce::Rectangle<float> bounds) const
    {
        using namespace vg::horology_ui;
        for (const auto& e : embers)
        {
            const float swayX = 0.018f * std::sin(e.phase);
            const float px = bounds.getX() + std::clamp(e.x01 + swayX, 0.0f, 1.0f) * bounds.getWidth();
            const float py = bounds.getY() + e.y01 * bounds.getHeight();
            const float pulse = 0.75f + 0.25f * std::sin(e.phase * 1.7f);
            const float r = e.radiusPx * (0.90f + 0.35f * smoothedHarmonicBloom) * pulse;

            juce::Colour baseCol = MidnightLanternPalette::lanternAmber();
            if (e.colorType == 1)
                baseCol = MidnightLanternPalette::fireworkCoral();
            else if (e.colorType == 2)
                baseCol = MidnightLanternPalette::starlightCyan();

            const float alpha = (0.045f + 0.065f * smoothedVocalEnergy) * pulse;
            juce::ColourGradient bokehGrad(
                baseCol.withAlpha(alpha), px, py,
                baseCol.withAlpha(0.0f), px + r, py, true);
            bokehGrad.addColour(0.55, baseCol.withAlpha(alpha * 0.45f));
            g.setGradientFill(bokehGrad);
            g.fillEllipse(px - r, py - r, r * 2.0f, r * 2.0f);
        }
    }

private:
    std::array<LanternEmber, kNumEmbers> embers{};
    float animTimeSec = 0.0f;
    float smoothedVocalEnergy = 0.0f;
    float smoothedHarmonicBloom = 0.0f;
};

//==============================================================================
// 2. スイス高級時計トゥールビヨン脱進機＆ブレゲひげゼンマイ・ビジュアライザ
//==============================================================================
class TourbillonBalanceWheelVisualizer
{
public:
    static void drawTourbillonCage(
        juce::Graphics& g,
        juce::Point<float> center,
        float radius,
        float phaseRadians,
        float pitchLockScore01,
        float singersFormant01)
    {
        using namespace vg::horology_ui;
        if (radius < 10.0f)
            return;

        pitchLockScore01 = std::clamp(pitchLockScore01, 0.0f, 1.0f);
        singersFormant01 = std::clamp(singersFormant01, 0.0f, 1.0f);

        // 1. 外周面取りアングラージュ・ベゼル
        g.setColour(MidnightLanternPalette::obsidianRecess().withAlpha(0.88f));
        g.fillEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);

        g.setColour(MidnightLanternPalette::brushedBrass().withAlpha(0.65f));
        g.drawEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);

        // 2. ブレゲひげゼンマイ (Breguet Concentric Breathing Hairspring)
        const float breatheScale = 1.0f + 0.08f * std::sin(phaseRadians * 3.0f);
        for (int ring = 1; ring <= 3; ++ring)
        {
            const float rr = radius * (0.22f + 0.14f * static_cast<float>(ring)) * breatheScale;
            const juce::Colour springCol = MidnightLanternPalette::starlightCyan()
                                               .interpolatedWith(MidnightLanternPalette::lanternGold(), singersFormant01);
            g.setColour(springCol.withAlpha(0.32f + 0.25f * pitchLockScore01));
            g.drawEllipse(center.x - rr, center.y - rr, rr * 2.0f, rr * 2.0f, 0.9f);
        }

        // 3. 3本スポークのトゥールビヨン・キャリッジ (回転ケージ)
        const float cageRadius = radius * 0.78f;
        juce::Path cageSpokes;
        for (int arm = 0; arm < 3; ++arm)
        {
            const float a = phaseRadians + static_cast<float>(arm) * 2.09439510239f; // 120 deg
            const float xTip = center.x + std::cos(a) * cageRadius;
            const float yTip = center.y + std::sin(a) * cageRadius;
            cageSpokes.startNewSubPath(center.x, center.y);
            cageSpokes.lineTo(xTip, yTip);

            // リム上のバランスウェイト・金ネジ
            g.setColour(MidnightLanternPalette::lanternGold().withAlpha(0.90f));
            g.fillEllipse(xTip - 2.2f, yTip - 2.2f, 4.4f, 4.4f);
        }
        g.setColour(MidnightLanternPalette::champagneHighlight().withAlpha(0.70f));
        g.strokePath(cageSpokes, juce::PathStrokeType(1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // 4. 中心の人工ルビー軸受 (Haute Horlogerie Ruby Pivot Jewel #E84A6F)
        const float rubyR = std::max(2.8f, radius * 0.14f);
        g.setColour(MidnightLanternPalette::fireworkCoral().withAlpha(0.92f));
        g.fillEllipse(center.x - rubyR, center.y - rubyR, rubyR * 2.0f, rubyR * 2.0f);
        g.setColour(MidnightLanternPalette::champagneHighlight().withAlpha(0.85f));
        g.drawEllipse(center.x - rubyR, center.y - rubyR, rubyR * 2.0f, rubyR * 2.0f, 0.8f);
    }
};

//==============================================================================
// 3. IEEE / ANSI C16.5 機械式 300ms VU バリスティック・メーター物理＆描画
//==============================================================================
class AnalogVuBallisticMeterRenderer
{
public:
    void reset() noexcept
    {
        needlePosition01 = 0.0f;
        needleVelocity = 0.0f;
        peakHold01 = 0.0f;
    }

    /**
     * @brief 300ms 立ち上がり・1.2% オーバーシュートの2次減衰振り子物理を更新
     */
    void updateBallistics(float targetVuNormalized01, float peakLevel01, float dtSeconds) noexcept
    {
        const float dt = std::clamp(dtSeconds, 0.001f, 0.050f);
        const float target = std::clamp(targetVuNormalized01, 0.0f, 1.0f);

        // 固有角周波数 ωn = 13.5 rad/s, 減衰比 ζ = 0.82 (1.2% オーバーシュート)
        constexpr float omegaN = 13.5f;
        constexpr float zeta = 0.82f;
        const float accel = (omegaN * omegaN) * (target - needlePosition01) - (2.0f * zeta * omegaN) * needleVelocity;
        needleVelocity += accel * dt;
        needlePosition01 = std::clamp(needlePosition01 + needleVelocity * dt, 0.0f, 1.05f);

        if (peakLevel01 >= peakHold01)
            peakHold01 = std::clamp(peakLevel01, 0.0f, 1.0f);
        else
            peakHold01 = std::max(0.0f, peakHold01 - dt * 0.35f);
    }

    void drawVuMeter(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& label) const
    {
        using namespace vg::horology_ui;
        EditorialBeveledCardRenderer::drawCard(g, bounds, MidnightLanternPalette::lanternAmber(), true, 7.0f);

        const float pivotX = bounds.getCentreX();
        const float pivotY = bounds.getBottom() + bounds.getHeight() * 0.25f;
        const float needleLen = bounds.getHeight() * 1.08f;

        // スケールアーク (-0.68 rad 〜 +0.68 rad)
        constexpr float minAngle = -0.65f;
        constexpr float maxAngle = +0.65f;
        const float zeroVuAngle = minAngle + 0.72f * (maxAngle - minAngle);

        juce::Path normalArc;
        normalArc.addCentredArc(pivotX, pivotY, needleLen * 0.86f, needleLen * 0.86f,
                                0.0f, minAngle, zeroVuAngle, true);
        g.setColour(MidnightLanternPalette::lanternAmber().withAlpha(0.55f));
        g.strokePath(normalArc, juce::PathStrokeType(2.0f));

        juce::Path redZoneArc;
        redZoneArc.addCentredArc(pivotX, pivotY, needleLen * 0.86f, needleLen * 0.86f,
                                 0.0f, zeroVuAngle, maxAngle, true);
        g.setColour(MidnightLanternPalette::fireworkCoral().withAlpha(0.85f));
        g.strokePath(redZoneArc, juce::PathStrokeType(2.4f));

        // パララックス・ニードル影と本針
        const float currentAngle = minAngle + std::clamp(needlePosition01, 0.0f, 1.0f) * (maxAngle - minAngle);
        const float tipX = pivotX + std::sin(currentAngle) * needleLen;
        const float tipY = pivotY - std::cos(currentAngle) * needleLen;

        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.drawLine(pivotX + 2.0f, pivotY, tipX + 2.0f, tipY + 1.5f, 1.8f);

        g.setColour(MidnightLanternPalette::champagneHighlight());
        g.drawLine(pivotX, pivotY, tipX, tipY, 1.6f);

        // 右上のピーク・ルビージュエル LED
        const bool isHot = (peakHold01 > 0.82f);
        const juce::Colour jewelCol = isHot ? MidnightLanternPalette::fireworkCoral()
                                            : MidnightLanternPalette::starlightCyan().withAlpha(0.45f);
        g.setColour(jewelCol);
        g.fillEllipse(bounds.getRight() - 12.0f, bounds.getY() + 6.0f, 6.0f, 6.0f);

        g.setColour(MidnightLanternPalette::warmIvoryText().withAlpha(0.80f));
        g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        g.drawText(label, bounds.reduced(6.0f, 3.0f), juce::Justification::topLeft, false);
    }

    float getNeedlePosition01() const noexcept { return needlePosition01; }

private:
    float needlePosition01 = 0.0f;
    float needleVelocity = 0.0f;
    float peakHold01 = 0.0f;
};

//==============================================================================
// 4. 6段グラミー・マスタリングチェイン・ライブテレメトリストリップ
//==============================================================================
class GrammyMasteringChainFlowStrip
{
public:
    struct ChainStageMetrics
    {
        float preampDrive01 = 0.25f;
        float deEssReduction01 = 0.15f;
        float pultecAir01 = 0.45f;
        float serbanGlue01 = 0.35f;
        float bricastiSpace01 = 0.30f;
        float truePeakGuard01 = 0.20f;
    };

    static void drawChainStrip(
        juce::Graphics& g,
        juce::Rectangle<float> bounds,
        const ChainStageMetrics& metrics)
    {
        using namespace vg::horology_ui;
        static constexpr std::array<const char*, 6> kStageNames = {
            "1073 PRE", "WEISS DS1", "PULTEC EQP", "SERBAN BUS", "M7 SPACE", "FIR dBTP"
        };
        const std::array<float, 6> values = {
            metrics.preampDrive01,
            metrics.deEssReduction01,
            metrics.pultecAir01,
            metrics.serbanGlue01,
            metrics.bricastiSpace01,
            metrics.truePeakGuard01
        };

        const float slotW = bounds.getWidth() / 6.0f;
        for (size_t i = 0; i < 6; ++i)
        {
            auto slot = juce::Rectangle<float>(
                bounds.getX() + static_cast<float>(i) * slotW + 2.0f,
                bounds.getY(),
                slotW - 4.0f,
                bounds.getHeight());

            const float v = std::clamp(values[i], 0.05f, 1.0f);
            const juce::Colour accent = (i == 1 || i == 5)
                                            ? MidnightLanternPalette::starlightCyan()
                                            : MidnightLanternPalette::lanternAmber();

            g.setColour(MidnightLanternPalette::obsidianRecess().withAlpha(0.84f));
            g.fillRoundedRectangle(slot, 4.5f);
            g.setColour(accent.withAlpha(0.32f + 0.45f * v));
            g.drawRoundedRectangle(slot, 4.5f, 1.0f);

            // 下部マイクロレベルバー
            auto barRect = slot.reduced(4.0f, 3.0f).removeFromBottom(3.0f);
            g.setColour(accent.withAlpha(0.88f));
            g.fillRoundedRectangle(barRect.withWidth(barRect.getWidth() * v), 1.5f);

            g.setColour(MidnightLanternPalette::warmIvoryText().withAlpha(0.88f));
            g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
            g.drawText(kStageNames[i], slot.reduced(3.0f, 2.0f), juce::Justification::centredTop, false);
        }
    }
};

} // namespace vg::horology_deck
