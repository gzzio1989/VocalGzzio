#pragma once

/**
 * @file HorologyGrandComplicationHud.h
 * @brief VocalGzzio v5.5.0 スイス時計グランド・コンプリケーション機構 HUD（均時差カム・レトログラード倍音扇形針・ムーンフェイズ声区ディスク）
 *
 * Patek Philippe Sky Moon Tourbillon や Vacheron Constantin Les Cabinotiers 等の
 * 世界最高峰グランド・コンプリケーション機械式時計に搭載される3大複雑機構を、
 * ボーカル音響テレメトリのリアルタイム可視化ギミックへと昇華させた JUCE Graphics 描画エンジン。
 *
 * 収録クラス:
 *   1. EquationOfTimeKidneyCamRenderer (Équation du Temps — 均時差キドニーカム機構)
 *      - 太陽時と平均時の差を機械的に計算する腎臓型カム（Kidney Cam）とフィーラー・スピンドルを描画し、
 *        ボーカルの「タイミング・グルーヴ偏差（ポケットの前後感）」を滑らかな黄金カムの回転として可視化。
 *
 *   2. Retrograde16BandHarmonicSectorRenderer (Rétrograde — 16バンド倍音レトログラード扇形針)
 *      - 120°の扇形アーチ目盛り上を針が進み、右端（100%）に達すると瞬時にフライバック帰零する
 *        レトログラード機構と16本のルビー軸受け倍音コラムを融合。
 *
 *   3. AstronomicalVocalRegisterMoonphaseRenderer (Phase de Lune — 声区ムーンフェイズ天体ディスク)
 *      - チェストボイス（黄金の太陽）とヘッドボイス／ファルセット（プラチナの月と星座盤）のブレンド比率を、
 *        ラピスラズリ文字盤上の高精度ムーンフェイズ回転ディスクとして描画。
 */

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace gzzio
{
namespace horology_grand
{

// =============================================================================
// 1. 均時差キドニーカム（Equation of Time Kidney Cam）レンダラー
// =============================================================================
class EquationOfTimeKidneyCamRenderer
{
public:
    static void drawKidneyCamMechanism(juce::Graphics& g,
                                       juce::Rectangle<float> bounds,
                                       float camAngleRadians,
                                       float grooveOffsetNeg1To1)
    {
        if (bounds.getWidth() < 16.0f || bounds.getHeight() < 16.0f)
            return;

        const auto center = bounds.getCentre();
        const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.42f;

        // 1. コート・ド・ジュネーブ風地板リング
        g.setColour(juce::Colour(0xFF0A1424));
        g.fillEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(juce::Colour(0x55D4AF37));
        g.drawEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f, 1.1f);

        // 2. 腎臓型カム（Kidney Cam: r(theta) = R0 * (1 + 0.22 * cos(2*theta) - 0.12 * sin(theta))）
        juce::Path kidneyCam;
        constexpr int kSteps = 48;
        for (int i = 0; i <= kSteps; ++i)
        {
            const float u = static_cast<float>(i) / static_cast<float>(kSteps);
            const float theta = u * juce::MathConstants<float>::twoPi;
            const float rShape = radius * 0.58f * (1.0f + 0.22f * std::cos(2.0f * theta) - 0.11f * std::sin(theta));
            const float rotTheta = theta + camAngleRadians;
            const float px = center.x + std::cos(rotTheta) * rShape;
            const float py = center.y + std::sin(rotTheta) * rShape;
            if (i == 0)
                kidneyCam.startNewSubPath(px, py);
            else
                kidneyCam.lineTo(px, py);
        }
        kidneyCam.closeSubPath();

        juce::ColourGradient camGrad(juce::Colour(0xFFE8C86B), center.x - radius * 0.4f, center.y - radius * 0.4f,
                                     juce::Colour(0xFF8C691E), center.x + radius * 0.4f, center.y + radius * 0.4f, false);
        g.setGradientFill(camGrad);
        g.fillPath(kidneyCam);
        g.setColour(juce::Colour(0xFFFFE599));
        g.strokePath(kidneyCam, juce::PathStrokeType(1.2f));

        // 3. カム外周を追従するルビー・フィーラーアーム（Feeler Follower）
        const float feelerPivotX = center.x - radius * 0.78f;
        const float feelerPivotY = center.y + radius * 0.32f;
        const float contactAngle = std::clamp(grooveOffsetNeg1To1, -1.0f, 1.0f) * 0.45f;
        const float tipX = center.x + std::cos(contactAngle) * (radius * 0.45f);
        const float tipY = center.y + std::sin(contactAngle) * (radius * 0.45f);

        g.setColour(juce::Colour(0xFFD8E4F2));
        g.drawLine(feelerPivotX, feelerPivotY, tipX, tipY, 1.6f);

        // 人工ルビー軸受石（Pigeon Blood Ruby Jewel）
        g.setColour(juce::Colour(0xFFE01E5A));
        g.fillEllipse(center.x - 3.0f, center.y - 3.0f, 6.0f, 6.0f);
        g.fillEllipse(feelerPivotX - 2.5f, feelerPivotY - 2.5f, 5.0f, 5.0f);
    }
};

// =============================================================================
// 2. レトログラード16バンド倍音セクター（Retrograde 16-Band Harmonic Sector）レンダラー
// =============================================================================
class Retrograde16BandHarmonicSectorRenderer
{
public:
    static void drawRetrogradeArc(juce::Graphics& g,
                                  juce::Rectangle<float> bounds,
                                  const std::array<float, 16>& bandEnergies01,
                                  float retrogradeHand01)
    {
        if (bounds.getWidth() < 24.0f || bounds.getHeight() < 16.0f)
            return;

        const float pivotX = bounds.getCentreX();
        const float pivotY = bounds.getBottom() - 4.0f;
        const float arcRadius = std::min(bounds.getWidth() * 0.46f, bounds.getHeight() * 0.86f);

        constexpr float kStartAngle = -juce::MathConstants<float>::pi * 0.36f;
        constexpr float kEndAngle   =  juce::MathConstants<float>::pi * 0.36f;

        // 1. 扇形ギョーシェ背景アーク
        juce::Path sectorArc;
        sectorArc.addCentredArc(pivotX, pivotY, arcRadius, arcRadius, 0.0f, kStartAngle, kEndAngle, true);
        g.setColour(juce::Colour(0x66D4AF37));
        g.strokePath(sectorArc, juce::PathStrokeType(1.3f));

        // 2. 16本の放射状倍音インデックス・ピラー
        for (size_t i = 0; i < 16; ++i)
        {
            const float u = static_cast<float>(i) / 15.0f;
            const float ang = kStartAngle + u * (kEndAngle - kStartAngle) - juce::MathConstants<float>::halfPi;
            const float energy = std::clamp(bandEnergies01[i], 0.05f, 1.0f);
            const float rInner = arcRadius * 0.38f;
            const float rOuter = rInner + (arcRadius * 0.56f) * energy;

            const float x0 = pivotX + std::cos(ang) * rInner;
            const float y0 = pivotY + std::sin(ang) * rInner;
            const float x1 = pivotX + std::cos(ang) * rOuter;
            const float y1 = pivotY + std::sin(ang) * rOuter;

            const juce::Colour barCol = juce::Colour(0xFF38BDF8).interpolatedWith(juce::Colour(0xFFF5D061), u);
            g.setColour(barCol.withAlpha(0.85f));
            g.drawLine(x0, y0, x1, y1, 2.1f);
        }

        // 3. 青焼き鋼（Blued Steel）レトログラード指針
        const float handU = std::clamp(retrogradeHand01, 0.0f, 1.0f);
        const float handAng = kStartAngle + handU * (kEndAngle - kStartAngle) - juce::MathConstants<float>::halfPi;
        const float hx = pivotX + std::cos(handAng) * (arcRadius * 0.96f);
        const float hy = pivotY + std::sin(handAng) * (arcRadius * 0.96f);

        g.setColour(juce::Colour(0xFFF8FAFC));
        g.drawLine(pivotX, pivotY, hx, hy, 1.8f);
        g.setColour(juce::Colour(0xFFD4AF37));
        g.fillEllipse(pivotX - 3.5f, pivotY - 3.5f, 7.0f, 7.0f);
    }
};

// =============================================================================
// 3. 声区ムーンフェイズ天体ディスク（Vocal Register Moonphase Disk）レンダラー
// =============================================================================
class AstronomicalVocalRegisterMoonphaseRenderer
{
public:
    // chestToHeadRatio01: 0.0 = Chest Voice (Gold Sun), 1.0 = Head Voice / Air (Silver Moon)
    static void drawMoonphaseWindow(juce::Graphics& g,
                                    juce::Rectangle<float> bounds,
                                    float chestToHeadRatio01)
    {
        if (bounds.getWidth() < 16.0f || bounds.getHeight() < 16.0f)
            return;

        const auto center = bounds.getCentre();
        const float r = std::min(bounds.getWidth(), bounds.getHeight()) * 0.44f;

        // 1. アベンチュリン（星空ガラス）ディープネイビー円盤
        g.setColour(juce::Colour(0xFF060E1E));
        g.fillEllipse(center.x - r, center.y - r, r * 2.0f, r * 2.0f);

        // 微細な金砂の星々
        g.setColour(juce::Colour(0xBBEFD07B));
        for (int s = 0; s < 9; ++s)
        {
            const float a = static_cast<float>(s) * 0.698f;
            const float sr = r * (0.35f + 0.45f * ((s % 3) * 0.3f));
            g.fillEllipse(center.x + std::cos(a) * sr - 1.0f, center.y + std::sin(a) * sr - 1.0f, 2.0f, 2.0f);
        }

        // 2. 太陽／月の回転天体ディスク
        const float phaseAngle = std::clamp(chestToHeadRatio01, 0.0f, 1.0f) * juce::MathConstants<float>::pi;
        const float orbRadius  = r * 0.34f;
        const float moonX = center.x + std::cos(phaseAngle - juce::MathConstants<float>::halfPi) * (r * 0.36f);
        const float moonY = center.y + std::sin(phaseAngle - juce::MathConstants<float>::halfPi) * (r * 0.22f);

        juce::ColourGradient moonGrad(juce::Colour(0xFFFFE599), moonX - orbRadius * 0.4f, moonY - orbRadius * 0.4f,
                                      juce::Colour(0xFFC4973B), moonX + orbRadius * 0.6f, moonY + orbRadius * 0.6f, true);
        g.setGradientFill(moonGrad);
        g.fillEllipse(moonX - orbRadius, moonY - orbRadius, orbRadius * 2.0f, orbRadius * 2.0f);

        // 3. ムーンフェイズ特有のダブルボソム（双円弧ボソム窓マスク）
        juce::Path bosomCover;
        bosomCover.addEllipse(center.x - r * 0.85f, center.y + r * 0.05f, r * 0.90f, r * 0.90f);
        bosomCover.addEllipse(center.x - r * 0.05f, center.y + r * 0.05f, r * 0.90f, r * 0.90f);
        g.setColour(juce::Colour(0xFF0B1628));
        g.fillPath(bosomCover);

        g.setColour(juce::Colour(0x88D4AF37));
        g.drawEllipse(center.x - r, center.y - r, r * 2.0f, r * 2.0f, 1.2f);
    }
};

} // namespace horology_grand
} // namespace gzzio

namespace vg
{
namespace horology_grand = ::gzzio::horology_grand;
} // namespace vg
