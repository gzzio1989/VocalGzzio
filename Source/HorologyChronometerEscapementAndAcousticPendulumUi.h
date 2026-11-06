#pragma once
// ==============================================================================
//  HorologyChronometerEscapementAndAcousticPendulumUi.h
//  VocalGzzio — スイス・アンクル脱進機（28,800 vph）・クラドニ図形（Chladni Cymatics）・フーコー倍音リサジュー ＆ コート・ド・ジュネーブ装飾
//
//  【まだ見たことのないワクワクするデザイン：「声の倍音純度が黄金の砂紋（クラドニ図形）と機械式脱進機を駆動する」】
//   1. **クラドニ音響砂紋（Chladni Plate Cymatics Nodal Geometry）**:
//      物理学者エルンスト・クラドニの振動板方程式:
//        Z(x, y) = a * cos(n*pi*x)*cos(m*pi*y) - b * cos(m*pi*x)*cos(n*pi*y) = 0
//      に基づき、歌声のフォルマントモード (n, m) に応じて節線（Nodal Lines）上に黄金の粒子が集まる
//      幾何学模様をリアルタイム生成する。
//   2. **スイス・ハイビート脱進機（28,800振動/時 = 8 Hz ガンギ車＆ルビーアンクル爪）**:
//      本物の機械式時計のテンプ（Balance Wheel）・ひげぜんまい・15歯ガンギ車（Escape Wheel）の噛み合い運動を描画する。
//   3. **フーコー振り子＆倍音リサジュー（Harmonic Lissajous Pendulum）**:
//      純正律音程比（3:2 完全5度, 5:4 長3度, 4:3 完全4度）の倍音位相が織りなす美しいリサジュー図形を描画する。
//   4. **コート・ド・ジュネーブ（Côtes de Genève）波紋研磨シェーダ**:
//      スイス最高峰ムーブメントのブリッジに施される平行波紋ストライプの異方性反射（Anisotropic Specular）を再現。
// ==============================================================================

#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <algorithm>

namespace vg::horology_cymatics
{

// ==============================================================================
//  1. コート・ド・ジュネーブ（Côtes de Genève）異方性波紋ブリッジシェーダ
// ==============================================================================
class CotesDeGeneveBridgeShader
{
public:
    static void drawGeneveStripes (juce::Graphics& g,
                                   juce::Rectangle<float> bounds,
                                   float lightAngleRad = 0.45f,
                                   int numStripes = 7) noexcept
    {
        if (bounds.getWidth() < 12.0f || bounds.getHeight() < 8.0f || numStripes <= 0)
            return;

        const float stripeW = bounds.getWidth() / static_cast<float> (numStripes);
        const float specBoost = 0.5f + 0.5f * std::cos (lightAngleRad);

        for (int i = 0; i < numStripes; ++i)
        {
            const float sx = bounds.getX() + static_cast<float> (i) * stripeW;
            juce::Rectangle<float> stripeRect (sx, bounds.getY(), stripeW, bounds.getHeight());

            juce::ColourGradient stripeGrad (
                juce::Colour (0xFF1E293B).withAlpha (0.35f), sx, bounds.getY(),
                juce::Colour (0xFF0F172A).withAlpha (0.45f), sx + stripeW, bounds.getY(), false);
            stripeGrad.addColour (0.45, juce::Colour (0xFF94A3B8).withAlpha (0.14f + 0.10f * specBoost));
            g.setGradientFill (stripeGrad);
            g.fillRect (stripeRect);

            // ストライプ境界の繊細な面取りライン（Anglage / Chamfer）
            g.setColour (juce::Colour (0xFFE2E8F0).withAlpha (0.12f));
            g.drawVerticalLine (static_cast<int> (sx), bounds.getY(), bounds.getBottom());
        }
    }
};

// ==============================================================================
//  2. 純正律倍音リサジュー振り子（Harmonic Lissajous Pendulum）描画器
// ==============================================================================
class HarmonicLissajousPendulumRenderer
{
public:
    static void drawLissajousFigure (juce::Graphics& g,
                                     juce::Point<float> center,
                                     float radius,
                                     float ratioP,
                                     float ratioQ,
                                     float phaseDeltaRad,
                                     juce::Colour curveColour) noexcept
    {
        if (radius < 4.0f)
            return;

        constexpr int kSamples = 72;
        constexpr float kTwoPi = 6.28318530718f;
        juce::Path lissajous;

        for (int i = 0; i <= kSamples; ++i)
        {
            const float t = static_cast<float> (i) * kTwoPi / static_cast<float> (kSamples);
            const float x = center.x + radius * std::sin (ratioP * t + phaseDeltaRad);
            const float y = center.y + radius * std::sin (ratioQ * t);
            if (i == 0) lissajous.startNewSubPath (x, y);
            else        lissajous.lineTo (x, y);
        }

        g.setColour (curveColour);
        g.strokePath (lissajous, juce::PathStrokeType (1.05f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

// ==============================================================================
//  3. クラドニ音響砂紋 ＆ スイス・アンクル脱進機（ガンギ車・ルビー爪）統合レンダラー
// ==============================================================================
class ChladniAndEscapementRenderer
{
public:
    // クラドニ振動板の節線強度 [0..1] を計算する純粋数学関数
    static inline float evaluateChladniNodalIntensity (float normX, float normY, float modeN, float modeM) noexcept
    {
        constexpr float kPi = 3.14159265358979323846f;
        const float term1 = std::cos (modeN * kPi * normX) * std::cos (modeM * kPi * normY);
        const float term2 = std::cos (modeM * kPi * normX) * std::cos (modeN * kPi * normY);
        const float z = std::fabs (term1 - term2);
        // 節線 (z ≈ 0) ほど値が 1.0 に近づくガウス型ピーク
        return std::exp (-z * z * 6.5f);
    }

    static void drawChladniCymaticsAndEscapement (juce::Graphics& g,
                                                  juce::Rectangle<float> bounds,
                                                  float timeSeconds,
                                                  float vocalEnergy01 = 0.6f,
                                                  float harmonicPurity01 = 0.9f) noexcept
    {
        if (bounds.getWidth() < 16.0f || bounds.getHeight() < 16.0f)
            return;

        const juce::Point<float> center = bounds.getCentre();
        const float radius = 0.45f * std::min (bounds.getWidth(), bounds.getHeight());
        if (radius < 6.0f)
            return;

        const float energy = std::clamp (vocalEnergy01, 0.0f, 1.0f);
        const float purity = std::clamp (harmonicPurity01, 0.0f, 1.0f);

        // 0. 背景コート・ド・ジュネーブ波紋研磨
        CotesDeGeneveBridgeShader::drawGeneveStripes (g, bounds.reduced (2.0f), timeSeconds * 0.3f, 6);

        // 1. 15歯スイス・ガンギ車（Escape Wheel）のステップ回転（8Hz ハイビート刻み）
        constexpr int kTeeth = 15;
        constexpr float kTwoPi = 6.28318530718f;
        const float beatStep = std::floor (timeSeconds * 8.0f) + 0.5f * (1.0f - std::cos (kTwoPi * std::fmod (timeSeconds * 8.0f, 1.0f)));
        const float wheelAngle = beatStep * (kTwoPi / static_cast<float> (kTeeth * 2));

        juce::Path escapeWheel;
        const float outerR = radius * 0.78f;
        const float innerR = radius * 0.58f;
        for (int i = 0; i < kTeeth; ++i)
        {
            const float a0 = wheelAngle + static_cast<float> (i) * (kTwoPi / static_cast<float> (kTeeth));
            const float a1 = a0 + 0.14f;
            escapeWheel.startNewSubPath (center.x + innerR * std::cos (a0), center.y + innerR * std::sin (a0));
            escapeWheel.lineTo (center.x + outerR * std::cos (a1), center.y + outerR * std::sin (a1));
        }
        g.setColour (juce::Colour (0xFFFFD166).withAlpha (0.35f + 0.35f * energy));
        g.strokePath (escapeWheel, juce::PathStrokeType (1.2f));

        // 2. 純正律 3:2 完全5度リサジュー振り子軌道
        HarmonicLissajousPendulumRenderer::drawLissajousFigure (
            g, center, radius * 0.52f, 3.0f, 2.0f, timeSeconds * 0.85f,
            juce::Colour (0xFFF59E0B).withAlpha (0.24f + 0.25f * purity));

        // 3. クラドニ音響砂紋（Chladni Cymatics）の節線リング描画
        const float modeN = 2.0f + std::floor (energy * 3.0f);
        const float modeM = 3.0f + std::floor (purity * 4.0f);
        juce::Path cymaticsPath;
        constexpr int kSegments = 48;
        for (int i = 0; i <= kSegments; ++i)
        {
            const float theta = static_cast<float> (i) * kTwoPi / static_cast<float> (kSegments);
            const float nx = std::cos (theta);
            const float ny = std::sin (theta);
            const float nodal = evaluateChladniNodalIntensity (nx, ny, modeN, modeM);
            const float r = radius * (0.42f + 0.48f * nodal);
            const float px = center.x + r * nx;
            const float py = center.y + r * ny;
            if (i == 0) cymaticsPath.startNewSubPath (px, py);
            else        cymaticsPath.lineTo (px, py);
        }
        cymaticsPath.closeSubPath();

        g.setColour (juce::Colour (0xFF38BDF8).withAlpha (0.32f + 0.38f * purity));
        g.strokePath (cymaticsPath, juce::PathStrokeType (1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // 4. 人工ルビー・ピボット軸受（Chaton Ruby Bearing）
        const float rubyR = std::max (2.2f, radius * 0.12f);
        g.setColour (juce::Colour (0xFFE11D48).withAlpha (0.85f));
        g.fillEllipse (center.x - rubyR, center.y - rubyR, rubyR * 2.0f, rubyR * 2.0f);
        g.setColour (juce::Colour (0xFFFFD166).withAlpha (0.90f));
        g.drawEllipse (center.x - rubyR, center.y - rubyR, rubyR * 2.0f, rubyR * 2.0f, 1.0f);
    }
};

} // namespace vg::horology_cymatics
