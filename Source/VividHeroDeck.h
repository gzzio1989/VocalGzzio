#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <array>

namespace vg_vivid
{

// ベクター図形で PlayStation / Switch 共通の象徴記号（△ ○ × □ ◇）を美しく描画するヘルパー
inline void drawSymbolGlyph (juce::Graphics& g, int symbolType, juce::Point<float> c, float r, juce::Colour col, float strokeW = 2.2f)
{
    g.setColour (col);
    if (symbolType == 0) // △ Triangle
    {
        juce::Path p;
        p.startNewSubPath (c.x, c.y - r * 0.95f);
        p.lineTo (c.x + r * 0.92f, c.y + r * 0.72f);
        p.lineTo (c.x - r * 0.92f, c.y + r * 0.72f);
        p.closeSubPath();
        g.strokePath (p, juce::PathStrokeType (strokeW, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }
    else if (symbolType == 1) // ○ Circle
    {
        g.drawEllipse (c.x - r * 0.84f, c.y - r * 0.84f, r * 1.68f, r * 1.68f, strokeW);
    }
    else if (symbolType == 2) // × Cross
    {
        const float d = r * 0.72f;
        g.drawLine (c.x - d, c.y - d, c.x + d, c.y + d, strokeW);
        g.drawLine (c.x + d, c.y - d, c.x - d, c.y + d, strokeW);
    }
    else if (symbolType == 3) // □ Square
    {
        const float d = r * 0.76f;
        g.drawRoundedRectangle (c.x - d, c.y - d, d * 2.0f, d * 2.0f, 2.0f, strokeW);
    }
    else if (symbolType == 4) // ◇ Diamond
    {
        juce::Path p;
        p.startNewSubPath (c.x, c.y - r);
        p.lineTo (c.x + r * 0.88f, c.y);
        p.lineTo (c.x, c.y + r);
        p.lineTo (c.x - r * 0.88f, c.y);
        p.closeSubPath();
        g.strokePath (p, juce::PathStrokeType (strokeW, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }
}

inline void drawArrowGlyph (juce::Graphics& g, int dir, juce::Point<float> c, float r, juce::Colour col, float strokeW = 2.1f)
{
    // dir: 0=Up, 1=Right, 2=Left
    g.setColour (col);
    juce::Path p;
    if (dir == 0)
    {
        p.startNewSubPath (c.x, c.y + r * 0.85f);
        p.lineTo (c.x, c.y - r * 0.85f);
        p.startNewSubPath (c.x - r * 0.62f, c.y - r * 0.18f);
        p.lineTo (c.x, c.y - r * 0.85f);
        p.lineTo (c.x + r * 0.62f, c.y - r * 0.18f);
    }
    else if (dir == 1)
    {
        p.startNewSubPath (c.x - r * 0.85f, c.y);
        p.lineTo (c.x + r * 0.85f, c.y);
        p.startNewSubPath (c.x + r * 0.18f, c.y - r * 0.62f);
        p.lineTo (c.x + r * 0.85f, c.y);
        p.lineTo (c.x + r * 0.18f, c.y + r * 0.62f);
    }
    else
    {
        p.startNewSubPath (c.x + r * 0.85f, c.y);
        p.lineTo (c.x - r * 0.85f, c.y);
        p.startNewSubPath (c.x - r * 0.18f, c.y - r * 0.62f);
        p.lineTo (c.x - r * 0.85f, c.y);
        p.lineTo (c.x - r * 0.18f, c.y + r * 0.62f);
    }
    g.strokePath (p, juce::PathStrokeType (strokeW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// 凹みダークアノダイズド・モジュールベイ描画
inline void drawObsidianBay (juce::Graphics& g, juce::Rectangle<float> r, float rad = 18.0f)
{
    // ドロップシャドウ
    g.setColour (juce::Colours::black.withAlpha (0.68f));
    g.fillRoundedRectangle (r.translated (0.0f, 5.0f).expanded (1.0f), rad);

    // 外周ダークメタルリム
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b3140), r.getX(), r.getY(),
                                             juce::Colour (0xff0c0e14), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);

    // 本体マットオブシディアン面
    auto inner = r.reduced (1.6f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff171b24), inner.getX(), inner.getY(),
                                             juce::Colour (0xff0d1017), inner.getX(), inner.getBottom(), false));
    g.fillRoundedRectangle (inner, rad - 1.2f);

    // 上端スペキュラ・ハイライトライン
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), inner.getX() + 18.0f, inner.getY() + 0.8f,
                                             juce::Colours::white.withAlpha (0.02f), inner.getRight() - 18.0f, inner.getY() + 0.8f, false));
    g.fillRect (inner.getX() + 18.0f, inner.getY() + 0.6f, inner.getWidth() - 36.0f, 1.2f);

    // 内側シャドウ縁
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (inner.reduced (0.8f), rad - 1.8f, 1.2f);
}

// ネオン発光アーク描画（多層ブルーム＋高輝度コア）
inline void drawNeonArc (juce::Graphics& g, juce::Point<float> c, float radius, float aStart, float aEnd,
                         juce::Colour colStart, juce::Colour colEnd, float coreThickness = 5.5f, float glowAlpha = 0.45f)
{
    if (std::abs (aEnd - aStart) < 0.005f)
        return;

    juce::Path arc;
    arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, aStart, aEnd, true);

    // 3段階ソフトネオンブルーム
    const juce::Colour midCol = colStart.interpolatedWith (colEnd, 0.5f);
    g.setColour (midCol.withAlpha (glowAlpha * 0.24f));
    g.strokePath (arc, juce::PathStrokeType (coreThickness * 4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (midCol.withAlpha (glowAlpha * 0.48f));
    g.strokePath (arc, juce::PathStrokeType (coreThickness * 2.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // グラデーションコア
    const float x0 = c.x + std::sin (aStart) * radius, y0 = c.y - std::cos (aStart) * radius;
    const float x1 = c.x + std::sin (aEnd)   * radius, y1 = c.y - std::cos (aEnd)   * radius;
    g.setGradientFill (juce::ColourGradient (colStart, x0, y0, colEnd, x1, y1, false));
    g.strokePath (arc, juce::PathStrokeType (coreThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 中心ホワイトホット・フィラメント
    g.setGradientFill (juce::ColourGradient (colStart.interpolatedWith (juce::Colours::white, 0.58f), x0, y0,
                                             colEnd.interpolatedWith   (juce::Colours::white, 0.58f), x1, y1, false));
    g.strokePath (arc, juce::PathStrokeType (coreThickness * 0.42f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// 1. 『クリーン（△）』— 大型8角形削り出しチタンダイヤル＋エメラルドネオンリング
inline void drawOctagonCleanKnob (juce::Graphics& g, juce::Point<float> c, float r, float norm01)
{
    const float a0 = -juce::MathConstants<float>::pi * 0.76f;
    const float a1 =  juce::MathConstants<float>::pi * 0.76f;
    const float ang = a0 + juce::jlimit (0.0f, 1.0f, norm01) * (a1 - a0);
    const juce::Colour emStart (0xff00e88f), emEnd (0xff24ffd0);

    // 周囲アンビエントエメラルド光彩
    g.setGradientFill (juce::ColourGradient (emStart.withAlpha (0.20f + 0.16f * norm01), c.x, c.y,
                                             emStart.withAlpha (0.0f), c.x + r * 1.48f, c.y, true));
    g.fillEllipse (c.x - r * 1.48f, c.y - r * 1.48f, r * 2.96f, r * 2.96f);

    // 凹みトラック溝
    juce::Path bgArc;
    bgArc.addCentredArc (c.x, c.y, r * 1.14f, r * 1.14f, 0.0f, a0, a1, true);
    g.setColour (juce::Colour (0xff07090e));
    g.strokePath (bgArc, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (emStart.withAlpha (0.18f));
    g.strokePath (bgArc, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // アクティブ・ネオンエメラルドリング
    drawNeonArc (g, c, r * 1.14f, a0, ang, emStart, emEnd, 6.2f, 0.62f);

    // ドロップシャドウ
    auto makeOct = [&] (float rad, float rot, juce::Point<float> off = {})
    {
        juce::Path p;
        for (int i = 0; i < 8; ++i)
        {
            const float a = rot + (float) i * juce::MathConstants<float>::twoPi / 8.0f + juce::MathConstants<float>::pi / 8.0f;
            const float px = c.x + off.x + std::sin (a) * rad;
            const float py = c.y + off.y - std::cos (a) * rad;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        p.closeSubPath();
        return p;
    };

    g.setColour (juce::Colours::black.withAlpha (0.76f));
    g.fillPath (makeOct (r * 0.98f, ang * 0.35f, { 0.0f, 8.0f }));

    // 8面体面取りスカート（各ファセットにリアルな金属明暗を付与）
    const float rot = ang * 0.35f;
    const float rOuter = r * 0.96f, rInner = r * 0.78f;
    for (int i = 0; i < 8; ++i)
    {
        const float fa0 = rot + (float) i * juce::MathConstants<float>::twoPi / 8.0f + juce::MathConstants<float>::pi / 8.0f;
        const float fa1 = fa0 + juce::MathConstants<float>::twoPi / 8.0f;
        const float midA = 0.5f * (fa0 + fa1);
        // 左上光源(-0.75 rad)に対する法線ライティング
        const float light = 0.5f + 0.5f * std::cos (midA + 0.68f);
        juce::Path facet;
        facet.startNewSubPath (c.x + std::sin (fa0) * rInner, c.y - std::cos (fa0) * rInner);
        facet.lineTo          (c.x + std::sin (fa0) * rOuter, c.y - std::cos (fa0) * rOuter);
        facet.lineTo          (c.x + std::sin (fa1) * rOuter, c.y - std::cos (fa1) * rOuter);
        facet.lineTo          (c.x + std::sin (fa1) * rInner, c.y - std::cos (fa1) * rInner);
        facet.closeSubPath();
        const juce::Colour fCol = juce::Colour (0xff151922).interpolatedWith (juce::Colour (0xff5c677a), light * light);
        g.setColour (fCol);
        g.fillPath (facet);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.strokePath (facet, juce::PathStrokeType (0.9f));
    }

    // 8角形トップ天面（コニカル・スピンヘアライン金属光沢）
    juce::Path topOct = makeOct (rInner, rot);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (topOct);
        g.fillAll (juce::Colour (0xff262c38));
        // 放射状ヘアライン・スペキュラ扇（左上〜右下へ走る金属反射）
        for (int s = 0; s < 48; ++s)
        {
            const float sa0 = (float) s * juce::MathConstants<float>::twoPi / 48.0f;
            const float sa1 = sa0 + juce::MathConstants<float>::twoPi / 46.0f;
            const float sheen = std::pow (0.5f + 0.5f * std::cos (2.0f * (sa0 + 0.65f)), 2.4f);
            juce::Path wedge;
            wedge.startNewSubPath (c.x, c.y);
            wedge.lineTo (c.x + std::sin (sa0) * rInner * 1.15f, c.y - std::cos (sa0) * rInner * 1.15f);
            wedge.lineTo (c.x + std::sin (sa1) * rInner * 1.15f, c.y - std::cos (sa1) * rInner * 1.15f);
            wedge.closeSubPath();
            g.setColour (juce::Colour (0xff1b2029).interpolatedWith (juce::Colour (0xff8a96ab), sheen * 0.78f));
            g.fillPath (wedge);
        }
    }
    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.strokePath (topOct, juce::PathStrokeType (1.3f));

    // 中央の控えめな △ 発光エンブレム＆回転ポインタスリット
    drawSymbolGlyph (g, 0, c, r * 0.17f, emEnd.withAlpha (0.86f), 2.2f);
    const float p0x = c.x + std::sin (ang) * (rInner * 0.54f), p0y = c.y - std::cos (ang) * (rInner * 0.54f);
    const float p1x = c.x + std::sin (ang) * (rOuter * 0.94f), p1y = c.y - std::cos (ang) * (rOuter * 0.94f);
    g.setColour (emEnd.withAlpha (0.45f));
    g.drawLine (p0x, p0y, p1x, p1y, 5.5f);
    g.setColour (juce::Colour (0xffe8fff6));
    g.drawLine (p0x, p0y, p1x, p1y, 2.4f);
}

// 2. 『声の抜け（×）』— 斜めブレード・ポインターノブ＋エレクトリックシアン・リング
inline void drawBladePresenceKnob (juce::Graphics& g, juce::Point<float> c, float r, float norm01)
{
    const float a0 = -juce::MathConstants<float>::pi * 0.75f;
    const float a1 =  juce::MathConstants<float>::pi * 0.75f;
    const float ang = a0 + juce::jlimit (0.0f, 1.0f, norm01) * (a1 - a0);
    const juce::Colour cyStart (0xff0099ff), cyEnd (0xff26f2ff);

    // アンビエント・シアン光彩
    g.setGradientFill (juce::ColourGradient (cyEnd.withAlpha (0.18f + 0.16f * norm01), c.x, c.y,
                                             cyEnd.withAlpha (0.0f), c.x + r * 1.46f, c.y, true));
    g.fillEllipse (c.x - r * 1.46f, c.y - r * 1.46f, r * 2.92f, r * 2.92f);

    // 外周シアン・ネオンベースリング＆スイープアーク
    g.setColour (juce::Colour (0xff070a10));
    g.drawEllipse (c.x - r * 1.06f, c.y - r * 1.06f, r * 2.12f, r * 2.12f, 9.0f);
    g.setColour (cyStart.withAlpha (0.28f));
    g.drawEllipse (c.x - r * 1.06f, c.y - r * 1.06f, r * 2.12f, r * 2.12f, 2.5f);
    drawNeonArc (g, c, r * 1.06f, a0, ang, cyStart, cyEnd, 5.2f, 0.65f);

    // 凹みダークディッシュ
    const float dishR = r * 0.90f;
    g.setColour (juce::Colours::black.withAlpha (0.72f));
    g.fillEllipse (c.x - dishR, c.y - dishR + 6.0f, dishR * 2.0f, dishR * 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff252d3c), c.x - dishR * 0.6f, c.y - dishR * 0.6f,
                                             juce::Colour (0xff0d1118), c.x + dishR * 0.6f, c.y + dishR * 0.6f, false));
    g.fillEllipse (c.x - dishR, c.y - dishR, dishR * 2.0f, dishR * 2.0f);
    g.setColour (cyEnd.withAlpha (0.22f));
    g.drawEllipse (c.x - dishR + 1.0f, c.y - dishR + 1.0f, (dishR - 1.0f) * 2.0f, (dishR - 1.0f) * 2.0f, 1.2f);

    // 3D 削り出しブレード・ポインター（回転角 ang に沿ってディッシュを貫く立体的金属ブレード）
    const float ux = std::sin (ang), uy = -std::cos (ang);
    const float nx = -uy,            ny = ux;
    const float tipDist  = r * 1.14f;
    const float tailDist = r * 0.94f;
    const float halfW    = r * 0.22f;

    // ブレード影
    {
        juce::Path sh;
        sh.startNewSubPath (c.x + ux * tipDist,  c.y + uy * tipDist  + 6.0f);
        sh.lineTo          (c.x + nx * halfW,    c.y + ny * halfW    + 6.0f);
        sh.lineTo          (c.x - ux * tailDist + nx * halfW * 0.55f, c.y - uy * tailDist + ny * halfW * 0.55f + 6.0f);
        sh.lineTo          (c.x - ux * tailDist - nx * halfW * 0.55f, c.y - uy * tailDist - ny * halfW * 0.55f + 6.0f);
        sh.lineTo          (c.x - nx * halfW,    c.y - ny * halfW    + 6.0f);
        sh.closeSubPath();
        g.setColour (juce::Colours::black.withAlpha (0.68f));
        g.fillPath (sh);
    }

    // ブレード左斜面（ハイライト側）と右斜面（シャドウ側）でリアルな稜線を作る
    juce::Path leftBevel, rightBevel;
    leftBevel.startNewSubPath (c.x + ux * tipDist,  c.y + uy * tipDist);
    leftBevel.lineTo          (c.x - ux * tailDist, c.y - uy * tailDist);
    leftBevel.lineTo          (c.x - ux * tailDist - nx * halfW * 0.62f, c.y - uy * tailDist - ny * halfW * 0.62f);
    leftBevel.lineTo          (c.x - nx * halfW,    c.y - ny * halfW);
    leftBevel.closeSubPath();

    rightBevel.startNewSubPath (c.x + ux * tipDist,  c.y + uy * tipDist);
    rightBevel.lineTo          (c.x + nx * halfW,    c.y + ny * halfW);
    rightBevel.lineTo          (c.x - ux * tailDist + nx * halfW * 0.62f, c.y - uy * tailDist + ny * halfW * 0.62f);
    rightBevel.lineTo          (c.x - ux * tailDist, c.y - uy * tailDist);
    rightBevel.closeSubPath();

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff7d8ea6), c.x - nx * halfW, c.y - ny * halfW,
                                             juce::Colour (0xff2b3444), c.x, c.y, false));
    g.fillPath (leftBevel);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1e2532), c.x, c.y,
                                             juce::Colour (0xff0e1219), c.x + nx * halfW, c.y + ny * halfW, false));
    g.fillPath (rightBevel);

    // 中央稜線スペキュラ＆先端シアンレーザー発光スリット
    g.setColour (juce::Colours::white.withAlpha (0.46f));
    g.drawLine (c.x - ux * tailDist * 0.92f, c.y - uy * tailDist * 0.92f,
                c.x + ux * tipDist  * 0.96f, c.y + uy * tipDist  * 0.96f, 1.5f);

    const float slit0x = c.x + ux * (r * 0.36f), slit0y = c.y + uy * (r * 0.36f);
    const float slit1x = c.x + ux * (r * 1.02f), slit1y = c.y + uy * (r * 1.02f);
    g.setColour (cyEnd.withAlpha (0.55f));
    g.drawLine (slit0x, slit0y, slit1x, slit1y, 6.0f);
    g.setColour (juce::Colour (0xffe6ffff));
    g.drawLine (slit0x, slit0y, slit1x, slit1y, 2.3f);
}

// 3. 『クール ◀▶ あたたかい（○）』— 2色発光ネオンスタジアムスロット＆削り出しリブ金属スライダー
inline void drawCoolWarmBiColorSlider (juce::Graphics& g, juce::Rectangle<float> slotRect, float norm01)
{
    const float pos = juce::jlimit (0.0f, 1.0f, norm01);
    const float rad = slotRect.getHeight() * 0.5f;
    const juce::Colour coolCol (0xff00c8ff), warmCol (0xffff6a00);

    // 外側ソフト2色グロー
    g.setGradientFill (juce::ColourGradient (coolCol.withAlpha (0.28f * (1.2f - pos)), slotRect.getX(), slotRect.getCentreY(),
                                             warmCol.withAlpha (0.28f * (0.2f + pos)), slotRect.getRight(), slotRect.getCentreY(), false));
    g.fillRoundedRectangle (slotRect.expanded (10.0f, 8.0f), rad + 8.0f);

    // 凹みスタジアム外枠
    g.setColour (juce::Colour (0xff06080c));
    g.fillRoundedRectangle (slotRect, rad);

    // 左右ネオン発光チューブ（左=アイシーシアン、右=フレイムオレンジ）
    auto tube = slotRect.reduced (5.0f, 6.0f);
    const float tRad = tube.getHeight() * 0.5f;
    {
        juce::Path clip;
        clip.addRoundedRectangle (tube, tRad);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (clip);

        juce::ColourGradient biGrad (coolCol, tube.getX(), tube.getCentreY(),
                                     warmCol, tube.getRight(), tube.getCentreY(), false);
        biGrad.addColour (0.44, coolCol.interpolatedWith (juce::Colour (0xff1a2436), 0.35f));
        biGrad.addColour (0.56, warmCol.interpolatedWith (juce::Colour (0xff361e14), 0.35f));
        g.setGradientFill (biGrad);
        g.fillRect (tube);

        // 中央の暗いスリット溝（ネオンリング状のU字発光に見せる）
        auto centerSlot = tube.reduced (18.0f, tube.getHeight() * 0.27f);
        g.setColour (juce::Colour (0xff12151d));
        g.fillRoundedRectangle (centerSlot, centerSlot.getHeight() * 0.5f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawRoundedRectangle (tube, tRad, 1.1f);

    // 3D リブ加工メタリック・スライダーノブ
    const float thumbW = 46.0f, thumbH = slotRect.getHeight() + 8.0f;
    const float minX = slotRect.getX() + 18.0f + thumbW * 0.5f;
    const float maxX = slotRect.getRight() - 18.0f - thumbW * 0.5f;
    const float tx = minX + pos * (maxX - minX);
    const juce::Rectangle<float> thumb (tx - thumbW * 0.5f, slotRect.getCentreY() - thumbH * 0.5f, thumbW, thumbH);

    g.setColour (juce::Colours::black.withAlpha (0.72f));
    g.fillRoundedRectangle (thumb.translated (0.0f, 4.5f), 9.0f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffaeb8c8), thumb.getX(), thumb.getY(),
                                             juce::Colour (0xff485160), thumb.getX(), thumb.getBottom(), false));
    g.fillRoundedRectangle (thumb, 8.5f);

    // 中央の縦リブ溝＆左右のカラー反射エッジ
    g.setColour (coolCol.withAlpha (0.55f));
    g.drawVerticalLine (juce::roundToInt (thumb.getX() + 1.5f), thumb.getY() + 6.0f, thumb.getBottom() - 6.0f);
    g.setColour (warmCol.withAlpha (0.55f));
    g.drawVerticalLine (juce::roundToInt (thumb.getRight() - 2.0f), thumb.getY() + 6.0f, thumb.getBottom() - 6.0f);

    g.setColour (juce::Colour (0xff1e232d));
    g.fillRoundedRectangle (thumb.getCentreX() - 3.0f, thumb.getY() + 5.0f, 6.0f, thumb.getHeight() - 10.0f, 2.5f);
    g.setColour (juce::Colours::white.withAlpha (0.70f));
    g.drawVerticalLine (juce::roundToInt (thumb.getCentreX() - 4.5f), thumb.getY() + 6.0f, thumb.getBottom() - 6.0f);
    g.drawVerticalLine (juce::roundToInt (thumb.getCentreX() + 4.5f), thumb.getY() + 6.0f, thumb.getBottom() - 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.38f));
    g.drawRoundedRectangle (thumb.reduced (0.6f), 8.5f, 1.2f);
}

// 4. 『ひろがり（◇）』— 同心3重ネオンプラネタリーリング（マゼンタ〜バイオレット〜シアン）
inline void drawPlanetarySpaceRings (juce::Graphics& g, juce::Point<float> c, float r, float norm01)
{
    const float v = juce::jlimit (0.0f, 1.0f, norm01);
    const juce::Colour mag (0xfff72585), vio (0xff9b5de5), cya (0xff00f5d4);

    // 深淵空間ハロー
    g.setGradientFill (juce::ColourGradient (vio.withAlpha (0.18f + 0.22f * v), c.x, c.y,
                                             vio.withAlpha (0.0f), c.x + r * 1.48f, c.y, true));
    g.fillEllipse (c.x - r * 1.48f, c.y - r * 1.48f, r * 2.96f, r * 2.96f);

    // 3重同心リング溝＆グラデーションネオン発光
    const float radii[3] = { r * 1.12f, r * 0.87f, r * 0.63f };
    const float widths[3] = { 5.4f, 4.6f, 4.0f };
    for (int i = 0; i < 3; ++i)
    {
        const float ri = radii[i];
        g.setColour (juce::Colour (0xff080a11));
        g.drawEllipse (c.x - ri, c.y - ri, ri * 2.0f, ri * 2.0f, widths[i] + 5.0f);

        const float ringBoost = juce::jlimit (0.22f, 1.0f, 0.30f + v * (1.05f - (float) i * 0.14f));
        const juce::Colour cA = (i == 0) ? mag : (i == 1 ? vio : cya);
        const juce::Colour cB = (i == 0) ? cya : (i == 1 ? mag : vio);

        // 全周ソフトネオンオーラ
        g.setColour (cA.interpolatedWith (cB, 0.5f).withAlpha (0.20f * ringBoost));
        g.drawEllipse (c.x - ri, c.y - ri, ri * 2.0f, ri * 2.0f, widths[i] * 3.4f);

        g.setGradientFill (juce::ColourGradient (cA.withAlpha (0.55f + 0.45f * ringBoost), c.x - ri, c.y + ri * 0.6f,
                                                 cB.withAlpha (0.55f + 0.45f * ringBoost), c.x + ri, c.y - ri * 0.6f, false));
        g.drawEllipse (c.x - ri, c.y - ri, ri * 2.0f, ri * 2.0f, widths[i]);

        // アクティブ量に応じた高輝度アークオーバーレイ
        const float a0 = -juce::MathConstants<float>::pi * 0.78f;
        const float a1 = a0 + v * juce::MathConstants<float>::pi * 1.56f;
        drawNeonArc (g, c, ri, a0, a1, cA, cB, widths[i] * 0.95f, 0.55f * ringBoost);
    }

    // 中央のベルベット・オブシディアン凹面ウェル
    const float coreR = r * 0.46f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d2130), c.x - coreR * 0.5f, c.y - coreR * 0.5f,
                                             juce::Colour (0xff090b12), c.x + coreR * 0.7f, c.y + coreR * 0.7f, false));
    g.fillEllipse (c.x - coreR, c.y - coreR, coreR * 2.0f, coreR * 2.0f);
    g.setColour (vio.withAlpha (0.35f));
    g.drawEllipse (c.x - coreR, c.y - coreR, coreR * 2.0f, coreR * 2.0f, 1.3f);

    drawSymbolGlyph (g, 4, c, coreR * 0.34f, cya.interpolatedWith (mag, 0.45f).withAlpha (0.90f), 2.2f);
}

// 5. 『声の大きさ』— 22段上広がり発光ウェッジメーター ＋ 3Dダイヤローレット縦ホイール
inline void drawVolumeWedgeAndWheel (juce::Graphics& g, juce::Rectangle<float> wedgeArea, juce::Rectangle<float> wheelSlot,
                                     float volNorm01, float liveLevel01)
{
    // ---- 左: 上に向かって広がる22段ネオンウェッジメーター ----
    const float botW = wedgeArea.getWidth() * 0.34f;
    const float topW = wedgeArea.getWidth() * 0.96f;
    const float cx   = wedgeArea.getCentreX();

    juce::Path bezel;
    bezel.startNewSubPath (cx - topW * 0.54f, wedgeArea.getY() - 8.0f);
    bezel.lineTo          (cx + topW * 0.54f, wedgeArea.getY() - 8.0f);
    bezel.lineTo          (cx + botW * 0.62f, wedgeArea.getBottom() + 8.0f);
    bezel.lineTo          (cx - botW * 0.62f, wedgeArea.getBottom() + 8.0f);
    bezel.closeSubPath();
    g.setColour (juce::Colour (0xff07090e));
    g.fillPath (bezel);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.strokePath (bezel, juce::PathStrokeType (1.2f));

    constexpr int kSegs = 22;
    const float segStep = wedgeArea.getHeight() / (float) kSegs;
    const float segH    = segStep * 0.68f;
    const float dispLvl = juce::jlimit (0.0f, 1.0f, juce::jmax (volNorm01, liveLevel01 * volNorm01 * 1.15f));
    juce::ignoreUnused (dispLvl);

    for (int s = 0; s < kSegs; ++s)
    {
        const float t = (float) s / (float) (kSegs - 1); // 0=bottom, 1=top
        const float wSeg = botW + t * (topW - botW);
        const float sy   = wedgeArea.getBottom() - (float) (s + 1) * segStep + (segStep - segH) * 0.5f;
        const bool lit   = (t <= volNorm01 + 0.02f);
        const bool hot   = (t <= liveLevel01 + 0.02f) && lit;

        const juce::Colour segCol = (t < 0.45f)
            ? juce::Colour (0xff00f5a0).interpolatedWith (juce::Colour (0xff00d9f5), t / 0.45f)
            : (t < 0.76f)
                ? juce::Colour (0xff00d9f5).interpolatedWith (juce::Colour (0xffffb703), (t - 0.45f) / 0.31f)
                : juce::Colour (0xffff9e00).interpolatedWith (juce::Colour (0xffff4d00), (t - 0.76f) / 0.24f);

        const float halfGap = 1.8f;
        const float colW    = (wSeg - halfGap * 2.0f) * 0.5f;
        const juce::Rectangle<float> leftBar  (cx - halfGap - colW, sy, colW, segH);
        const juce::Rectangle<float> rightBar (cx + halfGap,        sy, colW, segH);

        if (lit)
        {
            g.setColour (segCol.withAlpha (hot ? 0.42f : 0.24f));
            g.fillRoundedRectangle (leftBar.expanded (3.0f, 1.8f),  2.5f);
            g.fillRoundedRectangle (rightBar.expanded (3.0f, 1.8f), 2.5f);
            g.setColour (hot ? segCol.brighter (0.35f) : segCol.withAlpha (0.92f));
            g.fillRoundedRectangle (leftBar,  2.0f);
            g.fillRoundedRectangle (rightBar, 2.0f);
        }
        else
        {
            g.setColour (segCol.withAlpha (0.11f));
            g.fillRoundedRectangle (leftBar,  2.0f);
            g.fillRoundedRectangle (rightBar, 2.0f);
        }
    }

    // ---- 右: 凹みスロット内の 3D ダイヤローレット（Knurled）縦サムホイール ----
    g.setColour (juce::Colour (0xff05060a));
    g.fillRoundedRectangle (wheelSlot, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (wheelSlot, 10.0f, 1.1f);

    auto wheel = wheelSlot.reduced (8.0f, 26.0f);
    g.setColour (juce::Colours::black.withAlpha (0.80f));
    g.fillRoundedRectangle (wheel.expanded (2.0f, 4.0f), 12.0f);

    // 円筒状の縦方向シェーディング（上下端は深い影、中央やや上が金属ハイライト）
    juce::ColourGradient cyl (juce::Colour (0xff141820), wheel.getCentreX(), wheel.getY(),
                              juce::Colour (0xff10131a), wheel.getCentreX(), wheel.getBottom(), false);
    cyl.addColour (0.22, juce::Colour (0xff586274));
    cyl.addColour (0.42, juce::Colour (0xffbfc8d8));
    cyl.addColour (0.52, juce::Colour (0xffdde5f2));
    cyl.addColour (0.66, juce::Colour (0xff6c778c));
    g.setGradientFill (cyl);
    g.fillRoundedRectangle (wheel, 11.0f);

    // ダイヤ目ローレット刻み（音量値に応じて位相がリアルに回転！）
    {
        juce::Path wClip;
        wClip.addRoundedRectangle (wheel.reduced (1.5f), 10.0f);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (wClip);

        const float phase = std::fmod (volNorm01 * 48.0f, 1.0f);
        const int numRidges = 26;
        for (int rIdx = -2; rIdx <= numRidges + 2; ++rIdx)
        {
            const float nt = ((float) rIdx - phase) / (float) numRidges;
            // 円筒投影 cos マッピングで上下端ほど刻み間隔が詰まるリアル立体表現
            const float ang = (nt - 0.5f) * juce::MathConstants<float>::pi * 0.92f;
            if (std::abs (ang) >= juce::MathConstants<float>::halfPi)
                continue;
            const float yPos = wheel.getCentreY() + std::sin (ang) * (wheel.getHeight() * 0.5f);
            const float vis  = std::cos (ang);
            g.setColour (juce::Colours::black.withAlpha (0.52f));
            g.drawLine (wheel.getX() + 3.0f, yPos - 3.5f * vis, wheel.getRight() - 3.0f, yPos + 3.5f * vis, 1.3f);
            g.drawLine (wheel.getX() + 3.0f, yPos + 3.5f * vis, wheel.getRight() - 3.0f, yPos - 3.5f * vis, 1.3f);
            g.setColour (juce::Colours::white.withAlpha (0.24f * vis));
            g.drawHorizontalLine (juce::roundToInt (yPos), wheel.getX() + 4.0f, wheel.getRight() - 4.0f);
        }
    }
    g.setColour (juce::Colours::white.withAlpha (0.30f));
    g.drawRoundedRectangle (wheel.reduced (0.5f), 11.0f, 1.2f);
}

// トップバーの 3D イルミネーション・ロッカースイッチ描画
inline void drawIlluminatedRocker (juce::Graphics& g, juce::Rectangle<int> box, bool active,
                                   juce::Colour leftGlow, juce::Colour rightGlow)
{
    const auto rf = box.toFloat();
    g.setColour (juce::Colour (0xff06080d));
    g.fillRoundedRectangle (rf, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.drawRoundedRectangle (rf.reduced (0.5f), 8.0f, 1.1f);

    auto cap = rf.reduced (4.5f, 4.0f);
    if (active)
    {
        g.setGradientFill (juce::ColourGradient (leftGlow.withAlpha (0.45f), cap.getX(), cap.getCentreY(),
                                                 rightGlow.withAlpha (0.45f), cap.getRight(), cap.getCentreY(), false));
        g.fillRoundedRectangle (cap.expanded (2.0f), 6.5f);
    }

    g.setGradientFill (juce::ColourGradient (active ? leftGlow.interpolatedWith (juce::Colour (0xff2b3548), 0.52f) : juce::Colour (0xff272d3a),
                                             cap.getX(), cap.getY(),
                                             active ? rightGlow.interpolatedWith (juce::Colour (0xff141924), 0.62f) : juce::Colour (0xff141821),
                                             cap.getRight(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 5.5f);

    // 左右のエッジ発光スリット（待機時も色分けが一目でわかる美しいネオンスリット）
    g.setColour (leftGlow.withAlpha (active ? 1.0f : 0.72f));
    g.fillRoundedRectangle (cap.getX() + 5.0f, cap.getY() + 4.5f, 3.6f, cap.getHeight() - 9.0f, 1.8f);
    g.setColour (rightGlow.withAlpha (active ? 0.98f : 0.58f));
    g.fillRoundedRectangle (cap.getRight() - 8.6f, cap.getY() + 4.5f, 3.6f, cap.getHeight() - 9.0f, 1.8f);

    g.setColour (juce::Colours::white.withAlpha (active ? 0.34f : 0.14f));
    g.drawRoundedRectangle (cap.reduced (0.5f), 5.5f, 1.0f);
}

// 四季パノラマ画面用の彫刻ダイヤル描画（0=春桜花弁 / 1=夏蒼海ブレード / 2=秋紅葉漆器 / 3=冬氷晶フルート）
inline void drawSeasonSculptedDial (juce::Graphics& g, int seasonIdx, juce::Point<float> c, float r, float norm01, bool isPrimary)
{
    const float a0 = -juce::MathConstants<float>::pi * 0.75f;
    const float a1 =  juce::MathConstants<float>::pi * 0.75f;
    const float ang = a0 + juce::jlimit (0.0f, 1.0f, norm01) * (a1 - a0);

    juce::Colour ringCol, bodyHi, bodyLo, rimCol;
    if (seasonIdx == 0) // 春霞・桜
    {
        ringCol = isPrimary ? juce::Colour (0xffe86f88) : juce::Colour (0xff6db878);
        bodyHi  = isPrimary ? juce::Colour (0xfff79eb0) : juce::Colour (0xff9ed99c);
        bodyLo  = isPrimary ? juce::Colour (0xffc9536d) : juce::Colour (0xff549c60);
        rimCol  = juce::Colour (0xfff5d5c8);
    }
    else if (seasonIdx == 1) // 紺碧・夏海
    {
        ringCol = isPrimary ? juce::Colour (0xff1a75ff) : juce::Colour (0xff00b4d8);
        bodyHi  = isPrimary ? juce::Colour (0xff5ca4ff) : juce::Colour (0xff56cfe1);
        bodyLo  = isPrimary ? juce::Colour (0xff0d3b8c) : juce::Colour (0xff095266);
        rimCol  = juce::Colour (0xffcfe8f5);
    }
    else if (seasonIdx == 2) // 錦秋・紅葉
    {
        ringCol = isPrimary ? juce::Colour (0xffe84a27) : juce::Colour (0xfff2a93b);
        bodyHi  = isPrimary ? juce::Colour (0xffe05336) : juce::Colour (0xfff5b84c);
        bodyLo  = isPrimary ? juce::Colour (0xff6e180b) : juce::Colour (0xff78440e);
        rimCol  = juce::Colour (0xffe6b873);
    }
    else // 銀嶺・雪化粧
    {
        ringCol = isPrimary ? juce::Colour (0xff38d9f5) : juce::Colour (0xff7cc6fe);
        bodyHi  = isPrimary ? juce::Colour (0xff8cf0ff) : juce::Colour (0xffd8e6f5);
        bodyLo  = isPrimary ? juce::Colour (0xff157a94) : juce::Colour (0xff6b8299);
        rimCol  = juce::Colour (0xffe8f2fc);
    }

    // ドロップシャドウ＆外周ベゼル
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillEllipse (c.x - r, c.y - r + 5.0f, r * 2.0f, r * 2.0f);

    drawNeonArc (g, c, r * 1.08f, a0, ang, ringCol.darker (0.15f), ringCol.brighter (0.25f), 4.5f, 0.45f);

    g.setGradientFill (juce::ColourGradient (rimCol, c.x - r, c.y - r, rimCol.darker (0.45f), c.x + r, c.y + r, false));
    g.fillEllipse (c.x - r * 0.96f, c.y - r * 0.96f, r * 1.92f, r * 1.92f);

    const float capR = r * 0.85f;
    g.setGradientFill (juce::ColourGradient (bodyHi, c.x - capR * 0.6f, c.y - capR * 0.7f,
                                             bodyLo, c.x + capR * 0.7f, c.y + capR * 0.7f, false));
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);

    // 春の主ダイヤルは上品な8枚花弁ファセットレリーフを彫刻
    if (seasonIdx == 0 && isPrimary)
    {
        for (int p = 0; p < 8; ++p)
        {
            const float pa = ang * 0.4f + (float) p * juce::MathConstants<float>::twoPi / 8.0f;
            juce::Path petal;
            petal.startNewSubPath (c.x, c.y);
            petal.quadraticTo (c.x + std::sin (pa - 0.28f) * capR * 0.88f, c.y - std::cos (pa - 0.28f) * capR * 0.88f,
                               c.x + std::sin (pa) * capR * 0.96f,         c.y - std::cos (pa) * capR * 0.96f);
            petal.quadraticTo (c.x + std::sin (pa + 0.28f) * capR * 0.88f, c.y - std::cos (pa + 0.28f) * capR * 0.88f,
                               c.x, c.y);
            g.setColour ((p % 2 == 0) ? juce::Colours::white.withAlpha (0.20f) : juce::Colours::black.withAlpha (0.08f));
            g.fillPath (petal);
        }
    }

    // 指針ライン
    const float x0 = c.x + std::sin (ang) * (capR * 0.30f), y0 = c.y - std::cos (ang) * (capR * 0.30f);
    const float x1 = c.x + std::sin (ang) * (capR * 0.90f), y1 = c.y - std::cos (ang) * (capR * 0.90f);
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.drawLine (x0, y0, x1, y1, isPrimary ? 3.2f : 2.5f);
}

} // namespace vg_vivid
