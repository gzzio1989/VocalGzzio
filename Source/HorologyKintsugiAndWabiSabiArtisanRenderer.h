#pragma once
// ==============================================================================
//  HorologyKintsugiAndWabiSabiArtisanRenderer.h
//  VocalGzzio — 日本伝統工芸「金継ぎ（Kintsugi）・螺鈿（Raden）・漆黒溜塗（Urushi）・蒔絵梨地・七宝焼・組子麻の葉」× スイス時計工芸シェーダ
//
//  【日本の美意識と人間讃歌：「完璧な機械ではなく、人間の揺らぎ・傷跡さえも黄金の美に変える」】
//   1. **漆黒溜塗（Urushi Deep Lacquer）**:
//      単なる黒塗りではなく、奥底から紅柄（べんがら）と群青が透けて見える多層漆の深み。
//   2. **螺鈿（Raden Mother-of-Pearl Iridescence）**:
//      夜光貝・アワビ真珠層の薄膜干渉（シアン・翡翠・桜ピンク・金の角度依存虹彩）をプロシージャル再現。
//   3. **金継ぎ（Kintsugi Gold Vein）＆ 蒔絵梨地（Maki-e Nashi-ji Gold Dust）**:
//      有機的なベジェ曲線による本金粉・蒔絵（Maki-e）の金脈ラインと微細金砂子を描画し、
//      声のエネルギーが通う血管のように輝く。
//   4. **有線七宝（Shippo Cloisonné Enamel）＆ 組子細工「麻の葉（Asanoha）」音響拡散格子**:
//      スイス高級時計のグラン・フー・エナメル（Grand Feu Enamel）と日本の伝統組子細工を融合した
//      幾何学音響ディフューザー文様。
// ==============================================================================

#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>

namespace vg::horology_artisan
{

// ==============================================================================
//  1. 蒔絵・平目梨地（Maki-e Nashi-ji Gold Flake Field）決定論的プロシージャル生成器
//     ヒープ確保ゼロで漆層の奥に浮かぶ24K金箔・銀箔の微細フレークをレンダリングする。
// ==============================================================================
class MakiENashijiGoldDustShader
{
public:
    static void drawNashijiGoldFlakes (juce::Graphics& g,
                                       juce::Rectangle<float> bounds,
                                       float phaseSeconds,
                                       float vocalGlow01 = 0.5f,
                                       int flakeCount = 36) noexcept
    {
        if (bounds.getWidth() < 8.0f || bounds.getHeight() < 8.0f || flakeCount <= 0)
            return;

        const float glow = std::clamp (vocalGlow01, 0.0f, 1.0f);
        const int safeCount = std::clamp (flakeCount, 4, 96);

        uint32_t seed = 0x85EBCA6Bu;
        for (int i = 0; i < safeCount; ++i)
        {
            seed ^= (seed << 13);
            seed ^= (seed >> 17);
            seed ^= (seed << 5);
            const float rx = static_cast<float> (seed & 0xFFFFu) / 65535.0f;
            const float ry = static_cast<float> ((seed >> 16) & 0xFFFFu) / 65535.0f;

            const float px = bounds.getX() + bounds.getWidth()  * (0.06f + 0.88f * rx);
            const float py = bounds.getY() + bounds.getHeight() * (0.08f + 0.84f * ry);

            // 漆の深さに応じたきらめき位相
            const float depthLayer = static_cast<float> (i % 4) * 0.25f;
            const float shimmer = 0.5f + 0.5f * std::sin (phaseSeconds * (1.2f + depthLayer) + static_cast<float> (i) * 0.73f);
            const float alpha = (0.10f + 0.28f * (1.0f - depthLayer)) * (0.55f + 0.45f * shimmer) * (0.6f + 0.4f * glow);
            const float size  = 0.9f + 1.4f * (1.0f - depthLayer) + 0.6f * glow;

            const juce::Colour goldCol = (i % 5 == 0)
                ? juce::Colour (0xFFFFF3B0).withAlpha (alpha)  // 青金（Aokin: プラチナゴールド）
                : juce::Colour (0xFFFFB703).withAlpha (alpha); // 本金（Honkin: 24Kアンバーゴールド）

            g.setColour (goldCol);
            g.fillEllipse (px - size * 0.5f, py - size * 0.5f, size, size);
        }
    }
};

// ==============================================================================
//  2. 組子細工「麻の葉（Asanoha）」音響拡散ジオメトリ・バックドロップ
//     プロスタジオのQRD音響拡散パネル（Diffuser）と日本の組子細工を融合した六角幾何学文様。
// ==============================================================================
class KumikoAsanohaGeometricRenderer
{
public:
    static void drawAsanohaHexCell (juce::Graphics& g,
                                    juce::Point<float> center,
                                    float radius,
                                    juce::Colour lineColour,
                                    float strokeWidth = 0.85f) noexcept
    {
        if (radius < 3.0f)
            return;

        constexpr float kPi = 3.14159265358979323846f;
        std::array<juce::Point<float>, 6> hexVerts;
        for (int i = 0; i < 6; ++i)
        {
            const float angle = static_cast<float> (i) * (kPi / 3.0f) - (kPi / 6.0f);
            hexVerts[static_cast<size_t> (i)] = {
                center.x + radius * std::cos (angle),
                center.y + radius * std::sin (angle)
            };
        }

        juce::Path path;
        // 外周正六角形と中心から各頂点への放射線
        for (int i = 0; i < 6; ++i)
        {
            const auto& v0 = hexVerts[static_cast<size_t> (i)];
            const auto& v1 = hexVerts[static_cast<size_t> ((i + 1) % 6)];
            if (i == 0) path.startNewSubPath (v0);
            else        path.lineTo (v0);

            // 中心からの麻の葉スポーク
            path.startNewSubPath (center);
            path.lineTo (v0);

            // 三角形の重心（Incenter）を結ぶ麻の葉内接ダイヤ
            const juce::Point<float> triCentroid {
                (center.x + v0.x + v1.x) / 3.0f,
                (center.y + v0.y + v1.y) / 3.0f
            };
            path.startNewSubPath (center);
            path.lineTo (triCentroid);
            path.lineTo (v0);
            path.startNewSubPath (triCentroid);
            path.lineTo (v1);
        }

        g.setColour (lineColour);
        g.strokePath (path, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }

    static void drawKumikoLatticeStrip (juce::Graphics& g,
                                        juce::Rectangle<float> bounds,
                                        float vocalPresence01 = 0.5f) noexcept
    {
        if (bounds.getWidth() < 16.0f || bounds.getHeight() < 12.0f)
            return;

        const float cellR = std::clamp (bounds.getHeight() * 0.42f, 6.0f, 24.0f);
        const float stepX = cellR * 1.7320508f;
        const juce::Colour col = juce::Colour (0xFFFFD166).withAlpha (0.12f + 0.14f * std::clamp (vocalPresence01, 0.0f, 1.0f));

        const float cy = bounds.getCentreY();
        for (float cx = bounds.getX() + cellR; cx < bounds.getRight() - cellR * 0.5f; cx += stepX)
            drawAsanohaHexCell (g, { cx, cy }, cellR, col, 0.75f);
    }
};

// ==============================================================================
//  3. 有線七宝（Shippo Cloisonné Grand Feu Enamel）宝飾ベゼル・リング
// ==============================================================================
class ShippoCloisonneEnamelBezelRenderer
{
public:
    static void drawCloisonneBezelRing (juce::Graphics& g,
                                        juce::Point<float> center,
                                        float outerRadius,
                                        float innerRadius,
                                        float phaseSeconds,
                                        float resonance01 = 0.6f) noexcept
    {
        if (outerRadius <= innerRadius || innerRadius < 2.0f)
            return;

        const float res = std::clamp (resonance01, 0.0f, 1.0f);
        const float midR = 0.5f * (outerRadius + innerRadius);
        const float ringThickness = outerRadius - innerRadius;

        // 瑠璃紺（Ruri-kon: Deep Cobalt Enamel）＆ 翡翠（Hisui: Jade Enamel）グラデーション
        juce::ColourGradient enamelGrad (
            juce::Colour (0xFF0F2942).withAlpha (0.85f), center.x - outerRadius, center.y - outerRadius,
            juce::Colour (0xFF134E4A).withAlpha (0.85f), center.x + outerRadius, center.y + outerRadius, false);
        enamelGrad.addColour (0.5, juce::Colour (0xFF1E3A8A).withAlpha (0.75f + 0.20f * res));

        juce::Path ringPath;
        ringPath.addCentredArc (center.x, center.y, midR, midR, 0.0f, 0.0f, 6.2831853f, true);
        g.setGradientFill (enamelGrad);
        g.strokePath (ringPath, juce::PathStrokeType (ringThickness));

        // 24本の金線（Cloisonné Gold Wire Partitions）
        constexpr int kPartitions = 24;
        juce::Path wirePath;
        for (int i = 0; i < kPartitions; ++i)
        {
            const float angle = static_cast<float> (i) * (6.2831853f / static_cast<float> (kPartitions)) + phaseSeconds * 0.05f;
            const float c = std::cos (angle);
            const float s = std::sin (angle);
            wirePath.startNewSubPath (center.x + innerRadius * c, center.y + innerRadius * s);
            wirePath.lineTo (center.x + outerRadius * c, center.y + outerRadius * s);
        }
        g.setColour (juce::Colour (0xFFFFD166).withAlpha (0.50f + 0.30f * res));
        g.strokePath (wirePath, juce::PathStrokeType (0.85f));

        // 内外周の植線リム
        g.drawEllipse (center.x - outerRadius, center.y - outerRadius, outerRadius * 2.0f, outerRadius * 2.0f, 1.0f);
        g.drawEllipse (center.x - innerRadius, center.y - innerRadius, innerRadius * 2.0f, innerRadius * 2.0f, 0.9f);
    }
};

// ==============================================================================
//  4. 統合メダリオン描画器（漆黒溜塗 ＋ 螺鈿 ＋ 蒔絵梨地 ＋ 組子麻の葉 ＋ 金継ぎ金脈）
// ==============================================================================
class KintsugiAndRadenArtisanRenderer
{
public:
    static void drawUrushiRadenAndKintsugiMedallion (juce::Graphics& g,
                                                     juce::Rectangle<float> bounds,
                                                     float phaseSeconds,
                                                     float vocalWarmth01 = 0.5f) noexcept
    {
        if (bounds.getWidth() < 10.0f || bounds.getHeight() < 10.0f)
            return;

        const float warm = std::clamp (vocalWarmth01, 0.0f, 1.0f);
        const float corner = std::min (bounds.getWidth(), bounds.getHeight()) * 0.18f;

        // 1. 漆黒溜塗（Urushi Deep Lacquer）ベースグラデーション
        juce::ColourGradient urushiGrad (
            juce::Colour (0xFF0B1021).withAlpha (0.88f), bounds.getX(), bounds.getY(),
            juce::Colour (0xFF1A1423).withAlpha (0.92f), bounds.getRight(), bounds.getBottom(), false);
        urushiGrad.addColour (0.5, juce::Colour (0xFF121B2E).withAlpha (0.90f));
        g.setGradientFill (urushiGrad);
        g.fillRoundedRectangle (bounds, corner);

        // 2. 螺鈿（Raden Mother-of-Pearl）薄膜干渉イリデッセンス・インレイ
        const float iridShift = 0.5f + 0.5f * std::sin (phaseSeconds * 0.75f);
        const juce::Colour radenCyan   = juce::Colour (0xFF2DD4BF).withAlpha (0.14f + 0.10f * iridShift);
        const juce::Colour radenSakura = juce::Colour (0xFFF472B6).withAlpha (0.12f + 0.10f * (1.0f - iridShift));
        juce::ColourGradient radenGrad (
            radenCyan, bounds.getX() + bounds.getWidth() * 0.2f, bounds.getY(),
            radenSakura, bounds.getRight(), bounds.getBottom() * 0.8f, false);
        g.setGradientFill (radenGrad);
        g.fillRoundedRectangle (bounds.reduced (1.5f), corner * 0.9f);

        // 3. 組子「麻の葉」音響拡散文様と蒔絵梨地フレーク
        KumikoAsanohaGeometricRenderer::drawKumikoLatticeStrip (g, bounds.reduced (3.0f), warm);
        MakiENashijiGoldDustShader::drawNashijiGoldFlakes (g, bounds.reduced (2.0f), phaseSeconds, warm, 28);

        // 4. 金継ぎ（Kintsugi Gold Vein）有機金脈ベジェ曲線（主脈＋副脈の分岐構造）
        juce::Path kintsugiVein;
        const float x0 = bounds.getX() + bounds.getWidth() * 0.08f;
        const float y0 = bounds.getY() + bounds.getHeight() * 0.72f;
        const float x1 = bounds.getX() + bounds.getWidth() * 0.38f;
        const float y1 = bounds.getY() + bounds.getHeight() * (0.36f - 0.08f * warm);
        const float x2 = bounds.getX() + bounds.getWidth() * 0.68f;
        const float y2 = bounds.getY() + bounds.getHeight() * (0.64f + 0.06f * warm);
        const float x3 = bounds.getX() + bounds.getWidth() * 0.92f;
        const float y3 = bounds.getY() + bounds.getHeight() * 0.26f;

        kintsugiVein.startNewSubPath (x0, y0);
        kintsugiVein.cubicTo (x1, y1, x2, y2, x3, y3);

        // 分岐する副金脈（Branch Vein）
        const float bx0 = 0.5f * (x1 + x2);
        const float by0 = 0.5f * (y1 + y2);
        kintsugiVein.startNewSubPath (bx0, by0);
        kintsugiVein.quadraticTo (bounds.getX() + bounds.getWidth() * 0.62f,
                                  bounds.getY() + bounds.getHeight() * 0.22f,
                                  bounds.getX() + bounds.getWidth() * 0.84f,
                                  bounds.getY() + bounds.getHeight() * 0.16f);

        // 金粉の柔らかいハロー＋芯の24K本金ライン
        g.setColour (juce::Colour (0xFFFFB703).withAlpha (0.28f + 0.25f * warm));
        g.strokePath (kintsugiVein, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (juce::Colour (0xFFFFE57F).withAlpha (0.78f + 0.20f * warm));
        g.strokePath (kintsugiVein, juce::PathStrokeType (1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

} // namespace vg::horology_artisan
