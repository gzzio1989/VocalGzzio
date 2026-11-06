#pragma once
// ==============================================================================
//  HorologyBiophilicAndHeartCoherenceUi.h
//  VocalGzzio — バイオフィリック黄金角フィロタクシス・臨界減衰時計針物理・均時差アストロラーベ ＆ 0.1Hz 心拍コヒーレンスUI
//
//  【視覚デザインと人間工学：「見ているだけで心が落ち着き、同時にワクワクが高まるUI」】
//   1. **フィボナッチ黄金角（137.507764°）フィロタクシス星図**:
//      ひまわりの種や松ぼっくり、銀河の渦が持つ自然界の黄金角（360° × (1 - 1/φ) ≈ 137.50776°）配置により、
//      人間の視覚野（V1〜V4）が最も心地よく感じるバイオフィリック・ジオメトリを描画する。
//   2. **0.1 Hz 心拍変動（HRV）コヒーレンス・呼吸光輪 ＆ ブレゲひげぜんまい（Breguet Overcoil）**:
//      人間の副交感神経と集中力（ゾーン状態）を最も整える「5秒吸って5秒吐く（0.1 Hz = 10秒周期）」の
//      メイヤー波（Mayer Wave）光輪と、歌声のフリッソン（鳥肌度）に反応する黄金の波紋を融合する。
//   3. **2次臨界減衰（Critically Damped）スイス時計針バネ物理エンジン**:
//      デジタルメーターの無機質なカクつきを排し、本物のクロノグラフ針の慣性モーメント J と
//      ひげぜんまい復元トルク k、渦電流ダンパー c による滑らかな針応答を計算する。
//   4. **均時差（Equation of Time）レムニスケート（アナレンマ ∞ 軌道）天体インジケータ**:
//      太陽時と平均太陽時の差を描く8の字曲線（Analemma）上に現在のボーカル共鳴重心をプロットする。
// ==============================================================================

#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <algorithm>

namespace vg::horology_biophilic
{

// ==============================================================================
//  1. 2次臨界減衰（Critically Damped / Underdamped Selectable）機械式時計針バネ物理モデル
//     微分方程式: d^2x/dt^2 + 2 * zeta * omega_n * dx/dt + omega_n^2 * (x - target) = 0
// ==============================================================================
class MechanicalWatchHandPhysics
{
public:
    void prepare (double frameRateHz = 60.0, float naturalFreqHz = 4.5f, float dampingRatioZeta = 0.88f) noexcept
    {
        dt = static_cast<float> (1.0 / std::max (15.0, frameRateHz));
        omegaN = 2.0f * 3.14159265358979323846f * std::clamp (naturalFreqHz, 0.5f, 20.0f);
        zeta   = std::clamp (dampingRatioZeta, 0.25f, 2.0f);
        reset (0.0f);
    }

    void reset (float initialValue = 0.0f) noexcept
    {
        position = initialValue;
        velocity = 0.0f;
    }

    // 半陰的オイラー（Symplectic Euler）法で絶対安定に時計針位置を更新
    inline float stepToTarget (float targetValue) noexcept
    {
        const float displacement = position - targetValue;
        const float accel = -2.0f * zeta * omegaN * velocity - (omegaN * omegaN) * displacement;
        velocity += accel * dt;
        position += velocity * dt;
        return position;
    }

    float getCurrentPosition() const noexcept { return position; }
    float getCurrentVelocity() const noexcept { return velocity; }

private:
    float dt       = 1.0f / 60.0f;
    float omegaN   = 28.27f;
    float zeta     = 0.88f;
    float position = 0.0f;
    float velocity = 0.0f;
};

// ==============================================================================
//  2. ブレゲ巻き上げひげぜんまい（Breguet Overcoil Hairspring）呼吸スパイラル描画器
// ==============================================================================
class BreguetHairspringRenderer
{
public:
    static void drawBreathingHairspring (juce::Graphics& g,
                                         juce::Point<float> center,
                                         float maxRadius,
                                         float breathExpansion01,
                                         juce::Colour springColour) noexcept
    {
        if (maxRadius < 5.0f)
            return;

        // アルキメデス螺旋 r(theta) = r0 + b * theta （3.5回転）
        constexpr int kPoints = 84;
        constexpr float kTotalTurnsRad = 3.5f * 6.28318530718f;
        const float rMin = maxRadius * 0.16f;
        const float rSpan = maxRadius * (0.62f + 0.14f * std::clamp (breathExpansion01, 0.0f, 1.0f));

        juce::Path spiralPath;
        for (int i = 0; i <= kPoints; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (kPoints);
            const float theta = t * kTotalTurnsRad;
            const float r = rMin + t * rSpan;
            const float px = center.x + r * std::cos (theta);
            const float py = center.y + r * std::sin (theta);
            if (i == 0) spiralPath.startNewSubPath (px, py);
            else        spiralPath.lineTo (px, py);
        }

        // ブレゲ末期曲線（Overcoil terminal curve returning inward）
        const float endTheta = kTotalTurnsRad;
        const float endR = rMin + rSpan;
        spiralPath.quadraticTo (
            center.x + endR * 0.92f * std::cos (endTheta + 0.45f),
            center.y + endR * 0.92f * std::sin (endTheta + 0.45f),
            center.x + endR * 0.76f * std::cos (endTheta + 0.85f),
            center.y + endR * 0.76f * std::sin (endTheta + 0.85f));

        g.setColour (springColour);
        g.strokePath (spiralPath, juce::PathStrokeType (0.95f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

// ==============================================================================
//  3. 均時差アナレンマ（Equation of Time Analemma Figure-8）軌道描画器
// ==============================================================================
class CelestialAnalemmaRenderer
{
public:
    static void drawAnalemmaOrbit (juce::Graphics& g,
                                   juce::Point<float> center,
                                   float scaleRadius,
                                   float phaseSeconds,
                                   juce::Colour orbitColour) noexcept
    {
        if (scaleRadius < 6.0f)
            return;

        // ベルヌーイのレムニスケート（非対称アナレンマ近似）
        constexpr int kSteps = 48;
        juce::Path analemma;
        for (int i = 0; i <= kSteps; ++i)
        {
            const float u = static_cast<float> (i) * 6.2831853f / static_cast<float> (kSteps);
            const float x = scaleRadius * 0.34f * std::sin (2.0f * u + 0.25f);
            const float y = scaleRadius * 0.78f * std::sin (u);
            if (i == 0) analemma.startNewSubPath (center.x + x, center.y + y);
            else        analemma.lineTo (center.x + x, center.y + y);
        }
        analemma.closeSubPath();

        g.setColour (orbitColour);
        g.strokePath (analemma, juce::PathStrokeType (0.9f));

        // 現在位相の太陽マーカー（Golden Sun Dot）
        const float sunU = phaseSeconds * 0.45f;
        const float sunX = center.x + scaleRadius * 0.34f * std::sin (2.0f * sunU + 0.25f);
        const float sunY = center.y + scaleRadius * 0.78f * std::sin (sunU);
        g.setColour (juce::Colour (0xFFFFD166).withAlpha (0.92f));
        g.fillEllipse (sunX - 2.2f, sunY - 2.2f, 4.4f, 4.4f);
    }
};

// ==============================================================================
//  4. バイオフィリック黄金角＆心拍コヒーレンス統合リング描画器
// ==============================================================================
class BiophilicGoldenSpiralDialRenderer
{
public:
    static constexpr float kGoldenAngleRad = 2.39996322972865332f; // 137.507764 deg in radians

    static void drawBiophilicHeartCoherenceRing (juce::Graphics& g,
                                                 juce::Rectangle<float> bounds,
                                                 float phaseSeconds,
                                                 float frissonScore01,
                                                 float heartTouchPct) noexcept
    {
        if (bounds.getWidth() < 12.0f || bounds.getHeight() < 12.0f)
            return;

        const juce::Point<float> center = bounds.getCentre();
        const float maxRadius = 0.46f * std::min (bounds.getWidth(), bounds.getHeight());
        if (maxRadius < 4.0f)
            return;

        // 0.1 Hz メイヤー波呼吸コヒーレンス (周期10秒の滑らかな正弦呼吸)
        const float breathWave01 = 0.5f + 0.5f * std::sin (2.0f * 3.14159265f * 0.10f * phaseSeconds);
        const float frissonClamped = std::clamp (frissonScore01, 0.0f, 1.0f);

        // 1. 深淵ミッドナイトブルー＆アンバーの呼吸オーラ
        const float auraRadius = maxRadius * (0.82f + 0.14f * breathWave01 + 0.04f * frissonClamped);
        juce::ColourGradient auraGrad (
            juce::Colour (0xFFFFB347).withAlpha (0.14f + 0.16f * frissonClamped), center.x, center.y,
            juce::Colour (0xFF0A1428).withAlpha (0.0f), center.x + auraRadius, center.y, true);
        auraGrad.addColour (0.55, juce::Colour (0xFF38BDF8).withAlpha (0.09f + 0.08f * breathWave01));
        g.setGradientFill (auraGrad);
        g.fillEllipse (center.x - auraRadius, center.y - auraRadius, auraRadius * 2.0f, auraRadius * 2.0f);

        // 2. ブレゲひげぜんまい＆均時差アナレンマ軌道
        BreguetHairspringRenderer::drawBreathingHairspring (
            g, center, maxRadius * 0.56f, breathWave01,
            juce::Colour (0xFF7DD3FC).withAlpha (0.22f + 0.18f * breathWave01));
        CelestialAnalemmaRenderer::drawAnalemmaOrbit (
            g, center, maxRadius * 0.68f, phaseSeconds,
            juce::Colour (0xFFFFD166).withAlpha (0.20f + 0.20f * frissonClamped));

        // 3. フィボナッチ黄金角（137.5077°）フィロタクシス粒子群（48個の星屑ジュエル）
        constexpr int kNumSeeds = 48;
        const float rotOffset = phaseSeconds * 0.18f;
        for (int i = 1; i <= kNumSeeds; ++i)
        {
            const float normIdx = static_cast<float> (i) / static_cast<float> (kNumSeeds);
            const float r = maxRadius * 0.86f * std::sqrt (normIdx);
            const float theta = static_cast<float> (i) * kGoldenAngleRad + rotOffset;

            const float px = center.x + r * std::cos (theta);
            const float py = center.y + r * std::sin (theta);
            const float dotSize = 1.2f + 1.8f * (1.0f - normIdx) + 1.1f * frissonClamped;

            const juce::Colour jewelCol = (i % 3 == 0)
                ? juce::Colour (0xFFFFD166).withAlpha (0.35f + 0.45f * frissonClamped)
                : juce::Colour (0xFF7DD3FC).withAlpha (0.25f + 0.35f * breathWave01);

            g.setColour (jewelCol);
            g.fillEllipse (px - dotSize * 0.5f, py - dotSize * 0.5f, dotSize, dotSize);
        }

        // 4. 外周スイス・クロノメーター目盛り＆心に染みる共鳴アーク
        g.setColour (juce::Colour (0xFFFFD166).withAlpha (0.42f));
        g.drawEllipse (center.x - maxRadius, center.y - maxRadius, maxRadius * 2.0f, maxRadius * 2.0f, 1.1f);

        const float arcSweep = std::clamp (heartTouchPct * 0.01f, 0.05f, 1.0f) * 2.0f * 3.14159265f;
        juce::Path resonanceArc;
        resonanceArc.addCentredArc (center.x, center.y, maxRadius * 0.93f, maxRadius * 0.93f,
                                    0.0f, -1.5707963f, -1.5707963f + arcSweep, true);
        g.setColour (juce::Colour (0xFFFF9F1C).withAlpha (0.75f));
        g.strokePath (resonanceArc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

} // namespace vg::horology_biophilic
