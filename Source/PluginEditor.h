#pragma once
#include <cstdlib>   // GZ_JN test hook

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <map>
#include <atomic>
#include "PluginProcessor.h"

//==============================================================================
// Dark palette (v1.3.0): blue-grey base with 3 elevation levels; accents are
// saturated versions of the mascot colours so they read well on dark bg.
namespace Palette
{
    // v1.6.1 "watchmaker" look: dark brushed gunmetal stage (like a premium
    // audio unit / skeleton watch), butter-brass + fresh mint accents kept.
    // v1.7.0: the palette is now MUTABLE so the theme switch (cross-switch)
    // can recolour every card/text/hairline at once. Defaults below are the
    // neutral "watchmaker" dark look; applyTheme() rewrites them per mode.
    inline juce::Colour bgTop    { 0xff181b21 };   // bg0 (brushed dark steel)
    inline juce::Colour bgBot    { 0xff0b0d11 };
    inline juce::Colour panel    { 0xff20242c };   // bg1 (cards, steel plate)
    inline juce::Colour panel2   { 0xff2a303a };   // bg2 (raised surfaces)
    inline juce::Colour panelLn  { 0xff3e4552 };   // hairlines / grid
    inline juce::Colour ink      { 0xffe9edf4 };   // primary text
    inline juce::Colour inkSoft  { 0xff9ba6b6 };   // secondary text
    inline juce::Colour yellow   { 0xfff2cf4a };   // butter / polished brass
    inline juce::Colour yellowDk { 0xffd8b331 };
    inline juce::Colour green    { 0xff43d492 };   // accent G
    inline juce::Colour salmon   { 0xffff7a85 };   // accent R (ruby bearings)
    inline juce::Colour blue     { 0xff6ea8ff };   // accent B
    inline juce::Colour ice      { 0xff5ce8bc };   // mint: knob arcs / highlights
    inline juce::Colour track    { 0xff30363f };   // knob/meter track
    inline juce::Colour badge    { 0xfffdfcf7 };   // paper badge for the mascot

    // mode: 1 = yuru-kawa (soft pastel, light cards), else neutral (dark)
    // v1.9.0: 下地の明るさに応じて必ず読める文字色を返す。
    // 明るいアクセント(黄・ピンク)の上に淡い色を置いてしまい、
    // ゆるかわ/自然モードで選択中の項目が読めなくなっていた対策。
    inline juce::Colour readableOn (juce::Colour bg)
    {
        return bg.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff2b2419)
                                                   : juce::Colour (0xfff7f3ec);
    }

    // v2.0.0: アクセント色をそのまま使うと下地によっては読めない(例: 明るいモードの
    // クリーム地に黄色の見出し)。WCAGのコントラスト比が4.5:1を超えるまで、下地が
    // 明るければ暗く・暗ければ明るく寄せる。届いていればそのままの色を返す。
    inline float wcagLum (juce::Colour c)
    {
        auto ch = [] (float u) { return u <= 0.03928f ? u / 12.92f
                                                      : std::pow ((u + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * ch (c.getFloatRed()) + 0.7152f * ch (c.getFloatGreen())
             + 0.0722f * ch (c.getFloatBlue());
    }
    inline float wcagContrast (juce::Colour a, juce::Colour b)
    {
        const float la = wcagLum (a) + 0.05f, lb = wcagLum (b) + 0.05f;
        return la > lb ? la / lb : lb / la;
    }
    inline juce::Colour accentOn (juce::Colour accent, juce::Colour bg, float minRatio = 4.5f)
    {
        juce::Colour c = accent;
        const bool darkBg = wcagLum (bg) < 0.30f;
        for (int i = 0; i < 14 && wcagContrast (c, bg) < minRatio; ++i)
            c = darkBg ? c.brighter (0.16f) : c.darker (0.16f);
        return c;
    }

    inline void applyTheme (int mode)
    {
        if (mode == 1)
        {
            bgTop    = juce::Colour (0xffeaf6ee);   // light mint (fallback bg)
            bgBot    = juce::Colour (0xfff7f3e8);   // soft cream
            panel    = juce::Colour (0xfffdfaf3);   // light cream cards
            panel2   = juce::Colour (0xfffceef1);   // soft pink-cream (hero band)
            panelLn  = juce::Colour (0xffe1d9c9);   // soft hairlines
            ink      = juce::Colour (0xff5b5147);   // warm dark text (readable)
            inkSoft  = juce::Colour (0xff9a9081);   // secondary text
            yellow   = juce::Colour (0xfff2cf4a);   // keep butter accent
            yellowDk = juce::Colour (0xffe0be55);
            green    = juce::Colour (0xff7fcbb4);   // soft mint accent
            salmon   = juce::Colour (0xfff2a9b6);   // soft pink (cheeks/cherry)
            blue     = juce::Colour (0xffa9c9e8);   // soft blue (sprinkles)
            ice      = juce::Colour (0xff8fd8c4);   // soft mint arcs/highlights
            track    = juce::Colour (0xffe6dfd0);   // light knob/meter track
            badge    = juce::Colour (0xfffffdf8);
        }
        else if (mode == 2)   // 自然（四季・癒し）: 明るい緑/土のやすらぎ配色
        {
            bgTop    = juce::Colour (0xffdfeee4);
            bgBot    = juce::Colour (0xffeef2e2);
            panel    = juce::Colour (0xfff6faf2);
            panel2   = juce::Colour (0xffe8f0e2);
            panelLn  = juce::Colour (0xffcdd9c4);
            ink      = juce::Colour (0xff3a4a38);
            inkSoft  = juce::Colour (0xff7c8a76);
            yellow   = juce::Colour (0xffe6b34a);
            yellowDk = juce::Colour (0xffcf9a33);
            green    = juce::Colour (0xff5aa86a);
            salmon   = juce::Colour (0xffe98f7a);
            blue     = juce::Colour (0xff7bafd0);
            ice      = juce::Colour (0xff5aa86a);
            track    = juce::Colour (0xffdde7d5);
            badge    = juce::Colour (0xfffbfef8);
        }
        else if (mode == 3)   // ブランド: ダークなレザー・ラグジュアリー（金の差し色）
        {
            // v1.9.0: 商用化に向けて茶×金(既存ブランド想起)から
            //          自社色「深いプラム × シャンパン」へ変更
            bgTop    = juce::Colour (0xff2a2038);
            bgBot    = juce::Colour (0xff140e1d);
            panel    = juce::Colour (0xff332742);
            panel2   = juce::Colour (0xff3d3050);
            panelLn  = juce::Colour (0xff574468);
            ink      = juce::Colour (0xfff3ebf8);
            inkSoft  = juce::Colour (0xffb9a9c8);
            yellow   = juce::Colour (0xffe0c48c);
            yellowDk = juce::Colour (0xffc7a86d);
            green    = juce::Colour (0xff9dc0a8);
            salmon   = juce::Colour (0xffd08fa8);
            blue     = juce::Colour (0xff8f9ed8);
            ice      = juce::Colour (0xffe0c48c);
            track    = juce::Colour (0xff453757);
            badge    = juce::Colour (0xfff3ebf8);
        }
        else if (mode == 4)   // ライブ会場: 暗い舞台＋鮮やかな照明の差し色
        {
            bgTop    = juce::Colour (0xff17131f);
            bgBot    = juce::Colour (0xff0a0810);
            panel    = juce::Colour (0xff201a2b);
            panel2   = juce::Colour (0xff2a2338);
            panelLn  = juce::Colour (0xff453a5a);
            ink      = juce::Colour (0xfff2eefa);
            inkSoft  = juce::Colour (0xffa39ab8);
            yellow   = juce::Colour (0xfff2c14a);
            yellowDk = juce::Colour (0xffd8a733);
            green    = juce::Colour (0xff43d492);
            salmon   = juce::Colour (0xffff5c8a);
            blue     = juce::Colour (0xff4bc8e8);
            ice      = juce::Colour (0xff4bc8e8);
            track    = juce::Colour (0xff342b45);
            badge    = juce::Colour (0xfff2eefa);
        }
        else if (mode == 7)   // 色覚サポート: Okabe-Ito カラーユニバーサル配色 (高コントラスト)
        {
            bgTop    = juce::Colour (0xff11161b);
            bgBot    = juce::Colour (0xff0a0e12);
            panel    = juce::Colour (0xff1b232b);
            panel2   = juce::Colour (0xff242e38);
            panelLn  = juce::Colour (0xff45535f);
            ink      = juce::Colour (0xfff4f7f9);
            inkSoft  = juce::Colour (0xffaebac4);
            yellow   = juce::Colour (0xffe69f00);   // CUD orange
            yellowDk = juce::Colour (0xffc98700);
            green    = juce::Colour (0xff009e73);   // CUD bluish green
            salmon   = juce::Colour (0xffd55e00);   // CUD vermillion
            blue     = juce::Colour (0xff0072b2);   // CUD blue
            ice      = juce::Colour (0xff56b4e9);   // CUD sky blue
            track    = juce::Colour (0xff2c3944);
            badge    = juce::Colour (0xfff4f7f9);
        }
        else
        {
            bgTop    = juce::Colour (0xff181b21);
            bgBot    = juce::Colour (0xff0b0d11);
            panel    = juce::Colour (0xff20242c);
            panel2   = juce::Colour (0xff2a303a);
            panelLn  = juce::Colour (0xff3e4552);
            ink      = juce::Colour (0xffe9edf4);
            inkSoft  = juce::Colour (0xff9ba6b6);
            yellow   = juce::Colour (0xfff2cf4a);
            yellowDk = juce::Colour (0xffd8b331);
            green    = juce::Colour (0xff43d492);
            salmon   = juce::Colour (0xffff7a85);
            blue     = juce::Colour (0xff6ea8ff);
            ice      = juce::Colour (0xff5ce8bc);
            track    = juce::Colour (0xff30363f);
            badge    = juce::Colour (0xfffdfcf7);
        }
    }
}

//==============================================================================
class GzzioLnF : public juce::LookAndFeel_V4
{
public:
    GzzioLnF()
    {
        // v1.6.1: unit gear silhouettes for the watch-movement knob faces.
        // Built once; every knob draws them scaled+rotated (two path fills).
        auto makeUnitGear = [] (int teeth, float hubR, float holeR)
        {
            juce::Path g2;
            const float rTip = 1.0f, rRoot = 0.80f;
            const float step = juce::MathConstants<float>::twoPi / (float) teeth;
            for (int t = 0; t < teeth; ++t)
            {
                const float a0 = (float) t * step;
                auto pt = [] (float r, float a) { return juce::Point<float> (r * std::sin (a), -r * std::cos (a)); };
                if (t == 0) g2.startNewSubPath (pt (rRoot, a0)); else g2.lineTo (pt (rRoot, a0));
                g2.lineTo (pt (rTip,  a0 + step * 0.22f));
                g2.lineTo (pt (rTip,  a0 + step * 0.50f));
                g2.lineTo (pt (rRoot, a0 + step * 0.72f));
            }
            g2.closeSubPath();
            g2.setUsingNonZeroWinding (false);
            g2.addEllipse (-hubR, -hubR, hubR * 2.0f, hubR * 2.0f);
            if (holeR > 0.0f)
                for (int h = 0; h < 3; ++h)
                {
                    const float ha = (float) h * juce::MathConstants<float>::twoPi / 3.0f + 0.5f;
                    const float hr = (0.80f + hubR) * 0.52f;
                    g2.addEllipse (hr * std::sin (ha) - holeR, -hr * std::cos (ha) - holeR,
                                   holeR * 2.0f, holeR * 2.0f);
                }
            return g2;
        };
        uGearBig   = makeUnitGear (12, 0.22f, 0.185f);
        uGearSmall = makeUnitGear (8,  0.26f, 0.0f);

        ++s_instances;
        refreshPaletteColours();
    }

    // v2.3.0 重要な修正(Cubase 13が終了できない問題):
    //   埋め込みフォントを static(s_kawaiiFace)に入れっぱなしにしていたため、
    //   参照がプラグインの寿命を超えて残り、DAW終了時のモジュール解放中
    //   (Windowsのローダーロック下)にフォントの解放処理が走って止まっていた。
    //   最後のLookAndFeelが消えた時点で必ず手放す。
    ~GzzioLnF() override
    {
        if (--s_instances <= 0)
        {
            s_kawaiiFace = nullptr;
            s_useKawaii  = false;
        }
    }


    // v1.9.0: 以前はコンストラクタで一度だけ色を入れていたため、テーマを
    //         切り替えてもポップアップ・ツールチップ・ボタンの色が前のまま
    //         残っていた。テーマ変更のたびにここを呼び直す。
    void refreshPaletteColours()
    {
        setColour (juce::Label::textColourId, Palette::ink);
        setColour (juce::Slider::textBoxTextColourId, Palette::ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TooltipWindow::backgroundColourId, Palette::panel2);
        setColour (juce::TooltipWindow::textColourId, Palette::ink);
        setColour (juce::TooltipWindow::outlineColourId, Palette::panelLn);
        setColour (juce::TextButton::buttonColourId, Palette::panel2);
        setColour (juce::TextButton::textColourOffId, Palette::ink);
        setColour (juce::TextButton::textColourOnId, Palette::readableOn (Palette::yellow));
        // v2.0.1: JUCEは未選択時の案内文字を「文字色の50%アルファ」で描くため、
        // 明るいテーマの淡い箱では計算上どうやっても読めない(v1.9.8対策でも不足)。
        // 明るいテーマではプルダウンだけ濃い箱+明るい文字に反転する(旧デザイン踏襲)。
        // 50%アルファの案内文字でもコントラスト比 約4.5:1 を確保できる。
        {
            const bool lightTheme = Palette::panel2.getPerceivedBrightness() > 0.55f;
            const juce::Colour comboBg = lightTheme ? juce::Colour (0xff2f2a33)   // 濃い箱
                                                    : Palette::panel2;
            setColour (juce::ComboBox::backgroundColourId, comboBg);
            setColour (juce::ComboBox::textColourId, Palette::readableOn (comboBg));
            setColour (juce::ComboBox::outlineColourId, lightTheme ? juce::Colour (0xff4a4452)
                                                                   : Palette::panelLn);
            setColour (juce::ComboBox::arrowColourId, Palette::readableOn (comboBg)
                                                          .withMultipliedAlpha (0.75f));
        }
        setColour (juce::PopupMenu::backgroundColourId, Palette::panel2);
        setColour (juce::PopupMenu::textColourId, Palette::ink);
        setColour (juce::PopupMenu::headerTextColourId, Palette::inkSoft);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::yellow);
        setColour (juce::PopupMenu::highlightedTextColourId, Palette::readableOn (Palette::yellow));
    }

    // v2.1.0: MIDI設定パネル(CallOutBox)もテーマに合わせる。JUCE既定の描画は
    // 配色スキーム固定なので、どのテーマでも同じダーク箱で開いて浮いてしまう。
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g,
                                   const juce::Path& path, juce::Image&) override
    {
        g.setColour (Palette::panel);
        g.fillPath (path);
        g.setColour (Palette::panelLn);
        g.strokePath (path, juce::PathStrokeType (1.4f));
    }

    void setFontScale (float s) { fontScale = juce::jmax (0.5f, s); }

    // v1.7.0: theme accent for knob arcs / pinion / jewel cap (per-theme mint)
    void setThemeArc (juce::Colour c) { themeArc = c; }

    // v1.7.0 yuru-kawa: swap in the embedded rounded font (Mochiy Pop One subset).
    // Static so uiFont() (which is static and used everywhere) can pick it up.
    static void setKawaiiTypeface (juce::Typeface::Ptr tf) { s_kawaiiFace = tf; }
    static void setUseKawaiiFont  (bool b)                 { s_useKawaii  = b; }
    static void bumpJuicePhase    (float d)                { s_juicePhase += d; }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        if (s_useKawaii && s_kawaiiFace != nullptr)
            return s_kawaiiFace;
        return juce::LookAndFeel_V4::getTypefaceForFont (f);
    }
    float getFontScale() const  { return fontScale; }

    // Prefer a clean, highly legible UI face. On Windows "Yu Gothic UI" / "Meiryo"
    // render Japanese crisply; fall back gracefully elsewhere.
    static juce::Font uiFont (float height, bool bold)
    {
        if (s_useKawaii && s_kawaiiFace != nullptr)
            return juce::Font (s_kawaiiFace).withHeight (height);  // rounded font (single weight)

        juce::FontOptions o;
       #if JUCE_WINDOWS
        o = juce::FontOptions ("Yu Gothic UI", height, bold ? juce::Font::bold : juce::Font::plain);
       #elif JUCE_MAC
        o = juce::FontOptions ("Hiragino Sans", height, bold ? juce::Font::bold : juce::Font::plain);
       #else
        o = juce::FontOptions().withHeight (height).withStyle (bold ? "Bold" : "Regular");
       #endif
        return juce::Font (o);
    }

    juce::Font getLabelFont (juce::Label& l) override
    {
        // A ComboBox draws its closed value through this same drawLabel path, so its
        // text was being fitted into the fixed label box and shrank at high scales.
        // Give combo labels a larger base (and taller boxes in resized()) so the
        // "voice / mic / EQ preset" text actually grows with the text-size slider.
        float base = (float) l.getProperties().getWithDefault ("fontH", 14.0);
        if (dynamic_cast<juce::ComboBox*> (l.getParentComponent()) != nullptr)
            base = 15.5f;
        const bool bold = (bool) l.getProperties().getWithDefault ("bold", false);
        return uiFont (base * fontScale, bold);
    }

    void drawLabel (juce::Graphics& g, juce::Label& l) override
    {
        if (l.isBeingEdited()) return;
        const auto f = getLabelFont (l);
        g.setColour (l.findColour (juce::Label::textColourId)
                         .withMultipliedAlpha (l.isEnabled() ? 1.0f : 0.5f));
        g.setFont (f);
        const auto area = l.getBorderSize().subtractedFrom (l.getLocalBounds());
        g.drawFittedText (l.getText(), area, l.getJustificationType(),
                          juce::jmax (1, (int) ((float) area.getHeight() / f.getHeight())),
                          l.getMinimumHorizontalScale());
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        // v2.4.0: 大きなボタン(かんたんモードのうた自動など)は "fontH" プロパティで
        // 文字サイズを指定できる。既定は従来どおり高さの60%(上限15.5px)。
        const float want = (float) b.getProperties().getWithDefault ("fontH", 0.0);
        if (want > 0.0f)
            return uiFont (want * fontScale, true);
        return uiFont (juce::jmin ((float) h * 0.60f, 15.5f) * fontScale, true);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return uiFont (14.0f * fontScale, false);
    }

    juce::Font getPopupMenuFont() override
    {
        return uiFont (15.0f * fontScale, false);
    }

private:
    // ---- v1.6.1 watch-movement knob internals -------------------------------
    juce::Path uGearBig, uGearSmall;        // unit-radius gear silhouettes
    std::map<int, juce::Image> faceCache;   // static dial plate, rendered once per size

    const juce::Image& getMovementFace (int px)
    {
        auto it = faceCache.find (px);
        if (it != faceCache.end())
            return it->second;

        juce::Image img (juce::Image::ARGB, juce::jmax (8, px), juce::jmax (8, px), true);
        juce::Graphics fg (img);
        const float P = (float) juce::jmax (8, px);
        const float R = P * 0.5f;

        // steel dial plate with a soft top-left sheen
        {
            juce::ColourGradient gr (Palette::panel2.brighter (0.22f), R * 0.62f, R * 0.42f,
                                     Palette::panel.darker (0.30f),   R * 1.55f, R * 1.65f, true);
            fg.setGradientFill (gr);
            fg.fillEllipse (0.0f, 0.0f, P, P);
        }
        // circular brushing: three faint rings
        fg.setColour (Palette::ink.withAlpha (0.05f));
        for (float rr : { 0.34f, 0.52f, 0.70f })
            fg.drawEllipse (R - R * rr, R - R * rr, R * rr * 2.0f, R * rr * 2.0f, 1.0f);
        // bridge plate (upper) with two screws
        fg.setColour (Palette::panel.darker (0.42f));
        fg.fillRoundedRectangle (P * 0.14f, P * 0.12f, P * 0.72f, P * 0.24f, P * 0.10f);
        fg.setColour (Palette::panelLn);
        fg.drawRoundedRectangle (P * 0.14f, P * 0.12f, P * 0.72f, P * 0.24f, P * 0.10f, 1.0f);
        fg.setColour (Palette::ink.withAlpha (0.55f));
        fg.fillEllipse (P * 0.205f, P * 0.195f, P * 0.055f, P * 0.055f);
        fg.fillEllipse (P * 0.740f, P * 0.195f, P * 0.055f, P * 0.055f);
        // ruby bearings peeking out from under the gears
        auto ruby = [&fg, P] (float cx, float cy, float r)
        {
            fg.setColour (Palette::salmon.darker (0.45f));
            fg.fillEllipse (P * cx - r - 1.2f, P * cy - r - 1.2f, (r + 1.2f) * 2.0f, (r + 1.2f) * 2.0f);
            fg.setColour (Palette::salmon);
            fg.fillEllipse (P * cx - r, P * cy - r, r * 2.0f, r * 2.0f);
        };
        ruby (0.26f, 0.72f, P * 0.045f);
        ruby (0.77f, 0.64f, P * 0.038f);
        // polished brass rim (the metal bezel of the movement)
        fg.setColour (Palette::yellowDk.withAlpha (0.95f));
        fg.drawEllipse (P * 0.025f, P * 0.025f, P * 0.95f, P * 0.95f, juce::jmax (1.6f, P * 0.05f));
        fg.setColour (Palette::yellow.withAlpha (0.35f));
        fg.drawEllipse (P * 0.10f, P * 0.10f, P * 0.80f, P * 0.80f, 1.0f);

        return faceCache.emplace (juce::jmax (8, px), std::move (img)).first->second;
    }

public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle, juce::Slider& sl) override
    {
        if (s_useKawaii)   // yuru-kawa: draw a juice glass (fill = value)
        {
            drawJuiceKnob (g, juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height),
                           pos, (int) sl.getProperties().getWithDefault ("juice", 0),
                           (bool) sl.getProperties().getWithDefault ("wideGlass", false));
            return;
        }
        // v1.6.1 watch-movement look. Load discipline:
        //  - the dial (plate/bridge/rubies/brass rim) is a CACHED image -> 1 blit
        //  - the two meshed gears rotate with the VALUE, so they only ever move
        //    while the knob is being turned (which repaints it anyway)
        //  - nothing here adds a timer; an idle knob costs zero repaints
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto  c      = bounds.getCentre();
        const float angle  = startAngle + pos * (endAngle - startAngle);
        const float ring   = juce::jmax (4.5f, radius * 0.13f);

        juce::Path track;
        track.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (Palette::track);
        g.strokePath (track, juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path value;
        value.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour (themeArc);
        g.strokePath (value, juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillEllipse (c.x + radius * std::sin (angle) - ring * 0.55f,
                       c.y - radius * std::cos (angle) - ring * 0.55f, ring * 1.1f, ring * 1.1f);

        // static movement face (cached per size)
        const float kr = radius - ring - 3.0f;
        if (kr < 5.0f)
            return;
        const int facePx = juce::roundToInt (kr * 2.0f);
        g.drawImage (getMovementFace (facePx),
                     juce::Rectangle<float> (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f));

        // meshed gears, geared to the value: turning the knob drives the train
        // (big brass wheel and mint pinion counter-rotate at 12:8)
        {
            const float turnsBig = pos * juce::MathConstants<float>::twoPi * 1.5f;
            const float turnsSml = -turnsBig * (12.0f / 8.0f) + 0.35f;
            const float rB = kr * 0.52f, rS = kr * 0.30f;
            const auto  tB = juce::AffineTransform::scale (rB).rotated (turnsBig)
                                 .translated (c.x - kr * 0.14f, c.y + kr * 0.16f);
            const auto  tS = juce::AffineTransform::scale (rS).rotated (turnsSml)
                                 .translated (c.x + kr * 0.44f, c.y - kr * 0.30f);
            g.setColour (Palette::yellowDk);
            g.fillPath (uGearBig, tB);
            g.setColour (Palette::panelLn.darker (0.2f));
            g.strokePath (uGearBig, juce::PathStrokeType (1.0f), tB);
            g.setColour (themeArc.withAlpha (0.92f));
            g.fillPath (uGearSmall, tS);
            g.setColour (Palette::panelLn.darker (0.2f));
            g.strokePath (uGearSmall, juce::PathStrokeType (1.0f), tS);
            // gear axle rubies
            g.setColour (Palette::salmon);
            g.fillEllipse (c.x - kr * 0.14f - kr * 0.05f, c.y + kr * 0.16f - kr * 0.05f, kr * 0.10f, kr * 0.10f);
            g.fillEllipse (c.x + kr * 0.44f - kr * 0.04f, c.y - kr * 0.30f - kr * 0.04f, kr * 0.08f, kr * 0.08f);
        }

        // hand on top: slim polished pointer with a mint jewel cap
        const float pOut = kr * 0.92f, pIn = kr * 0.30f;
        juce::Path pointer;
        pointer.startNewSubPath (c.x + pIn  * std::sin (angle), c.y - pIn  * std::cos (angle));
        pointer.lineTo          (c.x + pOut * std::sin (angle), c.y - pOut * std::cos (angle));
        g.setColour (Palette::ink);
        g.strokePath (pointer, juce::PathStrokeType (juce::jmax (2.6f, kr * 0.11f),
                                                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (themeArc.withAlpha (0.95f));
        g.fillEllipse (c.x - kr * 0.10f, c.y - kr * 0.10f, kr * 0.20f, kr * 0.20f);
    }

    // ================= v1.7.0 yuru-kawa: juice-glass knobs =================
    // fill height = value; each knob a different juice (per-panel fruit theme).
    static juce::Colour juiceColour (int fruit)
    {
        static const juce::uint32 cols[] = {
            0xffffa53c, 0xfff5d24a, 0xff9ccf5a, 0xfffa826e, 0xffe6d95a,  // 0-4  citrus
            0xfff05a72, 0xff7890d8, 0xffe86aa0, 0xff9b78c8, 0xffd23c50,  // 5-9  berry
            0xffffb43c, 0xfff5cd50, 0xfff58c46, 0xfffab9a5,             // 10-13 tropical
            0xffc8e196, 0xfff0505f, 0xffaad796, 0xffa0c86e, 0xffc8e1a0  // 14-18 melon/grape
        };
        const int n = (int) (sizeof (cols) / sizeof (cols[0]));
        return juce::Colour (cols[juce::jlimit (0, n - 1, fruit)]);
    }
    static int garnishType (int fruit)   // 0 citrus slice, 1 melon wedge, 2 berry cluster
    {
        switch (fruit) {
            case 15: case 16: return 1;
            case 5: case 6: case 7: case 8: case 9: case 14: case 17: return 2;
            default: return 0;
        }
    }
    // per-panel fruit assignment (by parameter id)
    static int juiceFruitFor (const juce::String& id)
    {
        if (id=="gate") return 0; if (id=="lowcut") return 1; if (id=="mud") return 2;
        if (id=="harsh") return 3; if (id=="denoise") return 4;                 // CLEAN UP: citrus
        if (id=="comp1") return 5; if (id=="comp2") return 6; if (id=="attack") return 7;
        if (id=="release") return 8; if (id=="deess") return 9;                 // DYNAMICS: berry
        if (id=="presence") return 10; if (id=="air") return 11;
        if (id=="drive") return 12; if (id=="sustain") return 13;               // TONE: tropical
        if (id=="makeup") return 14; if (id=="mix") return 15; if (id=="width") return 16;
        if (id=="doubler") return 17; if (id=="delay") return 18;
        if (id=="revsize") return 14; if (id=="revmix") return 16;              // SPACE: melon/grape
        if (id=="seq_amount") return 0; if (id=="seq_focus") return 5;
        if (id.startsWith ("seq_f")) return 2; if (id.startsWith ("seq_d")) return 10;
        if (id=="duck") return 9; if (id=="cho_amt") return 6; if (id=="bpm") return 1;
        if (id=="dly_ms") return 11; if (id=="dly_fb") return 7; if (id=="dly_hc") return 3;
        if (id=="mega_amt") return 15; if (id=="robo_freq") return 12; if (id=="robo_mix") return 17;
        return (int) (((id.hashCode() % 19) + 19) % 19);
    }

    void drawGarnish (juce::Graphics& g, int type, juce::Colour col, float cx, float cy, float s)
    {
        if (type == 1)   // melon wedge
        {
            juce::Path w;
            w.addPieSegment (cx - s, cy - s, s * 2.0f, s * 2.0f,
                             juce::degreesToRadians (198.0f), juce::degreesToRadians (342.0f), 0.0f);
            g.setColour (col); g.fillPath (w);
            g.setColour (juce::Colour (0xff5faf5f));
            g.strokePath (w, juce::PathStrokeType (s * 0.26f));
            g.setColour (juce::Colour (0xff40332e));
            for (int i = -1; i <= 1; ++i)
                g.fillEllipse (cx + i * s * 0.42f - s * 0.07f, cy - s * 0.30f, s * 0.14f, s * 0.20f);
        }
        else if (type == 2)   // berry cluster
        {
            const float r = s * 0.40f;
            const juce::Point<float> pts[] = { {cx-r,cy-r*0.3f},{cx+r,cy-r*0.3f},{cx,cy-r*1.05f},
                                               {cx-r*0.5f,cy+r*0.7f},{cx+r*0.6f,cy+r*0.7f} };
            for (auto& p : pts) {
                g.setColour (col);              g.fillEllipse (p.x - r, p.y - r, r*2, r*2);
                g.setColour (col.brighter(0.45f)); g.fillEllipse (p.x - r*0.5f, p.y - r*0.6f, r*0.5f, r*0.5f);
            }
        }
        else   // citrus slice
        {
            g.setColour (juce::Colours::white);              g.fillEllipse (cx - s, cy - s, s*2, s*2);
            g.setColour (col);                               g.fillEllipse (cx - s*0.80f, cy - s*0.80f, s*1.6f, s*1.6f);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            for (int a = 0; a < 360; a += 45) {
                const float rd = juce::degreesToRadians ((float) a);
                g.drawLine (cx, cy, cx + std::cos (rd) * s * 0.76f, cy + std::sin (rd) * s * 0.76f, 1.3f);
            }
            g.setColour (col.brighter (0.5f));               g.fillEllipse (cx - s*0.16f, cy - s*0.16f, s*0.32f, s*0.32f);
        }
    }

    // v2.4.0: wide=true でグラスをセル幅いっぱいに近づける。かんたんモードは
    //         7本を横いっぱいに並べるのでセルが広く、0.62 のままだと細長い
    //         試験管みたいに見えてしまう。
    void drawJuiceKnob (juce::Graphics& g, juce::Rectangle<float> area, float pos, int fruit,
                        bool wide = false)
    {
        const float gw = area.getWidth()  * (wide ? 0.90f : 0.62f);
        const float gh = area.getHeight() * 0.88f;
        juce::Rectangle<float> glass (area.getCentreX() - gw * 0.5f,
                                      area.getCentreY() - gh * 0.5f + gh * 0.05f, gw, gh);
        const float corner = gw * 0.16f;
        juce::Rectangle<float> inner = glass.reduced (gw * 0.06f);
        const float fillH = juce::jmax (0.0f, inner.getHeight() * pos);
        const juce::Colour jc = juiceColour (fruit);

        // faint glass body
        g.setColour (juce::Colour (0x24ffffff));
        g.fillRoundedRectangle (glass, corner);

        if (fillH > 2.0f)
        {
            juce::Path clip; clip.addRoundedRectangle (inner, corner * 0.8f);
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (clip);

            const float surfY = inner.getBottom() - fillH;
            juce::ColourGradient jg (jc.brighter (0.10f), 0, surfY, jc.darker (0.12f), 0, inner.getBottom(), false);
            g.setGradientFill (jg);
            g.fillRect (juce::Rectangle<float> (inner.getX() - 2, surfY, inner.getWidth() + 4, fillH + 6));

            // wobbling surface highlight
            const float wob = std::sin (s_juicePhase * 1.3f + area.getX() * 0.05f) * (gh * 0.012f);
            g.setColour (jc.brighter (0.30f));
            g.fillEllipse (inner.getX() - 2, surfY - gh * 0.028f + wob, inner.getWidth() + 4, gh * 0.056f);

            // bubbles rising through the juice
            for (int b = 0; b < 4; ++b)
            {
                const float seed = area.getX() * 0.13f + b * 1.7f;
                const float br = gw * (0.028f + 0.018f * std::fmod (seed, 1.0f));
                const float bx = inner.getX() + inner.getWidth() * (0.2f + 0.6f * std::fmod (seed * 0.37f, 1.0f));
                const float span = juce::jmax (1.0f, fillH);
                const float by = inner.getBottom()
                                 - std::fmod (s_juicePhase * (gh * 0.02f) + seed * 11.0f, span);
                g.setColour (juce::Colours::white.withAlpha (0.28f));
                g.fillEllipse (bx - br, by - br, br * 2, br * 2);
            }
        }

        // glass outline + vertical shine
        g.setColour (juce::Colour (0xffc2d0d8));
        g.drawRoundedRectangle (glass.reduced (0.6f), corner, juce::jmax (1.5f, gw * 0.03f));
        g.setColour (juce::Colours::white.withAlpha (0.40f));
        g.fillRoundedRectangle (juce::Rectangle<float> (glass.getX() + gw * 0.13f, glass.getY() + gh * 0.12f,
                                                        gw * 0.07f, gh * 0.60f), gw * 0.035f);

        // straw poking out of the top-right
        {
            const float x1 = area.getCentreX() + gw * 0.12f, y1 = glass.getY() + gh * 0.28f;
            const float x2 = area.getCentreX() + gw * 0.30f, y2 = glass.getY() - gh * 0.18f;
            g.setColour (juce::Colour (0xffef7896));
            g.drawLine (x1, y1, x2, y2, gw * 0.085f);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawLine (x1, y1, x2, y2, gw * 0.02f);
        }

        // fruit garnish hooked on the rim (top-left)
        drawGarnish (g, garnishType (fruit), jc.brighter (0.05f),
                     glass.getX() + gw * 0.18f, glass.getY() - gh * 0.01f, gw * 0.19f);
    }
    // =======================================================================

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                               bool over, bool down) override
    {
        // v1.5.0: full pill buttons (radius = height/2) for the friendly brand look
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float rad = r.getHeight() * 0.5f;
        auto col = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId)
                                      : bg;
        if (down) col = col.brighter (0.12f);
        else if (over) col = col.brighter (0.06f);
        g.setColour (col);
        g.fillRoundedRectangle (r, rad);
        g.setColour (b.getToggleState() ? col.brighter (0.25f) : Palette::panelLn);
        g.drawRoundedRectangle (r, rad, b.getToggleState() ? 1.6f : 1.2f);
    }

    // ---- Larger, wrapped tooltips (fixes "description text too small") ----
    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos,
                                           juce::Rectangle<int> parentArea) override
    {
        const float fh = 14.5f * juce::jmax (1.0f, fontScale);
        const int   maxW = juce::jmin (420, parentArea.getWidth() - 24);
        juce::AttributedString s;
        s.append (tip, uiFont (fh, false));
        juce::TextLayout tl;
        tl.createLayout (s, (float) (maxW - tipPad * 2));

        const int w = juce::jmin (maxW, (int) std::ceil (tl.getWidth()) + tipPad * 2);
        const int h = (int) std::ceil (tl.getHeight()) + tipPad * 2;

        const int xLo = parentArea.getX();
        const int xHi = juce::jmax (xLo, parentArea.getRight() - w);
        const int yLo = parentArea.getY();
        const int yHi = juce::jmax (yLo, parentArea.getBottom() - h);
        auto x = juce::jlimit (xLo, xHi, pos.x - w / 2);
        auto y = pos.y > parentArea.getCentreY() ? pos.y - (h + 14) : pos.y + 22;
        y = juce::jlimit (yLo, yHi, y);
        return { x, y, w, h };
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height) override
    {
        juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        const float fh = 14.5f * juce::jmax (1.0f, fontScale);
        juce::AttributedString s;
        s.append (text, uiFont (fh, false), findColour (juce::TooltipWindow::textColourId));
        s.setLineSpacing (2.0f);
        juce::TextLayout tl;
        tl.createLayout (s, (float) (width - tipPad * 2));
        tl.draw (g, bounds.reduced ((float) tipPad).withTrimmedTop (2.0f));
    }

private:
    float fontScale { 1.0f };
    static constexpr int tipPad = 10;
    juce::Colour themeArc { 0xff5ce8bc };   // v1.7.0: current knob-arc accent
    static juce::Typeface::Ptr s_kawaiiFace;   // embedded rounded font (yuru-kawa)
    static std::atomic<int>    s_instances;    // v2.3.0: 最後の1つが消えたらフォントを手放す
    static bool s_useKawaii;
    static float s_juicePhase;                 // juice bubble/wobble animation phase
};

//==============================================================================
// Pitch stability view + meters. Analysis only; audio is never delayed.
// The centrepiece is a scrolling pitch-history graph: instead of a fast-moving
// needle, it plots the last few seconds of cents-error so the singer can SEE
// whether they are holding pitch steadily. Green centre band = in tune.
class VocalTuner : public juce::Component, private juce::Timer
{
public:
    explicit VocalTuner (VocalGzzioProcessor& p);

    void paint   (juce::Graphics&) override;
    void resized ()                override;

private:
    void timerCallback() override;
    void analyse();

    VocalGzzioProcessor& proc;
    std::vector<float>   buffer;
    juce::ComboBox       refPitchBox;

    juce::String noteName { "--" };
    float cents { 0 }, freq { 0 };
    bool  hasPitch { false };
    int   lastMidi { -1 };                 // last detected MIDI note (for note-rail + range)
    float dispCents { 0 }, dispFreq { 0 };
    int   hold { 0 };
    float dispIn { 0 }, dispOut { 0 }, dispGR { 0 }, dispDS { 0 }, dispDN { 0 };

    // ---- v1.6.0 gear meter (from the mixer-app motif): a brass + mint gear
    //      pair spins at a speed proportional to the output level. Drawn inside
    //      the meter zone, which already repaints at 30 Hz -> ~zero extra cost.
    juce::Path  gearBig, gearSmall;
    float       gearAngle { 0.0f };          // degrees
    juce::uint32 gearLastMs { 0 };

    // Scrolling pitch history: cents error over time, newest at the right.
    static constexpr int histLen = 240;   // ~8 s at 30 Hz
    float histCents[histLen] = {};
    bool  histValid[histLen] = {};
    int   histMidi[histLen]  = {};        // note-rail: absolute pitch per frame
    int   histPos { 0 };

    // ---- v1.4.0 note rail + vocal-range check ----
    bool  railMode { false };             // false = classic cents strip, true = scale-lane note rail
    int   rangeLo { 127 }, rangeHi { -1 }; // captured min/max MIDI while checking
    bool  rangeChecking { false };
    juce::TextButton railToggle, rangeButton;

    // v1.4.0 P5: YIN pitch confidence (0..1) and held-note gating for range capture.
    // A single glitch frame must never corrupt the min/max, so we only extend the
    // range once the SAME note has been held steadily for a few frames.
    float pitchConf { 0.0f };             // 1 - YIN CMND at the chosen lag
    int   rangeCand { -1 };               // note currently being held as a candidate
    int   rangeCandCount { 0 };           // consecutive stable frames of rangeCand
    // plausible sung range clamp (E2..E6): rejects octave/harmonic detection errors
    static constexpr int kRangeLoMidi = 40;   // E2
    static constexpr int kRangeHiMidi = 88;   // E6

public:
    // MIDI -> singer-community notation (C4 = mid2C, A4 = hiA; A is the zone boundary)
    static juce::String midiToJp (int midi);
    void toggleRail()   { railMode = ! railMode; railToggle.setToggleState (railMode, juce::dontSendNotification); repaint(); }
    void setRailMode (bool r) { railMode = r; railToggle.setToggleState (r, juce::dontSendNotification); repaint(); }
    // v1.9.1: 言語切替でチューナー内のボタン文言も貼り替える（従来は生成時のみで日本語のまま）
    void refreshLanguage();
    bool isRailMode() const   { return railMode; }
    void setRangeToolsVisible (bool v)   // note-rail toggle + range check (advanced only)
    {
        railToggle .setVisible (v);
        rangeButton.setVisible (v);
        if (! v) { railMode = false; railToggle.setToggleState (false, juce::dontSendNotification);
                   if (rangeChecking) stopRange(); }
        resized();
    }
    std::function<void (bool)> onRailChange;   // persist rail toggle across editor recreation
    void startRange()   { rangeChecking = true; rangeLo = 127; rangeHi = -1;
                          rangeCand = -1; rangeCandCount = 0; repaint(); }
    void stopRange()    { rangeChecking = false; repaint(); }
    bool isRangeChecking() const { return rangeChecking; }
    juce::String rangeResultText() const;

};

//==============================================================================
// EQ graph: log-frequency (20 Hz - 20 kHz) x dB (-18..+18) display.
// Layers: output spectrum (FFT of the analyzer ring) -> auto-cut region ->
// static corrective curve -> live curve (static + current smart-EQ cuts).
// Display only: it reads parameter values / atomics and never touches audio.
class EQGraph : public juce::Component, private juce::Timer
{
public:
    explicit EQGraph (VocalGzzioProcessor& p) : proc (p)
    {
        timeData.resize ((size_t) VocalGzzioProcessor::analyzerSize, 0.0f);
        fftData .resize ((size_t) VocalGzzioProcessor::analyzerSize * 2, 0.0f);
        specDb  .resize ((size_t) VocalGzzioProcessor::analyzerSize / 2 + 1, -120.0f);
        startTimerHz (30);
    }

    void paint (juce::Graphics&) override;
    void resized() override { gridImage = juce::Image(); }   // v1.5.0: invalidate grid cache

    // v1.7.0: run the FFT/analyzer only while actually visible. When the graph is
    // hidden (Effects tab, or the lightweight display modes) the timer stops, so no
    // FFT and no repaints happen — a real CPU saving.
    void visibilityChanged() override
    {
        if (isVisible()) startTimerHz (30);
        else             stopTimer();
    }

    // ---- v1.4.0 F6-style editing: drag manual bands on the graph ----
    // (freq = horizontal, depth = vertical, mouse wheel = Q width)
    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;
    void mouseMove  (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // log freq <-> x, dB <-> y helpers (public so bands can reuse them later)
    float freqToX (float hz) const
    {
        const float lo = std::log10 (fMin), hi = std::log10 (fMax);
        return plot.getX() + (std::log10 (juce::jlimit (fMin, fMax, hz)) - lo) / (hi - lo) * plot.getWidth();
    }
    float xToFreq (float x) const
    {
        const float lo = std::log10 (fMin), hi = std::log10 (fMax);
        const float t = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
        return std::pow (10.0f, lo + t * (hi - lo));
    }
    float dbToY (float db) const
    {
        return plot.getCentreY() - (juce::jlimit (-dbMax, dbMax, db) / dbMax) * (plot.getHeight() * 0.5f);
    }
    float yToDb (float y) const
    {
        return juce::jlimit (-dbMax, dbMax,
                             (plot.getCentreY() - y) / juce::jmax (1.0f, plot.getHeight() * 0.5f) * dbMax);
    }

private:
    void timerCallback() override { updateSpectrum(); repaint(); }
    void updateSpectrum();

    bool manualEditActive() const;                    // seq_on && manual mode
    int  hitTestBand (juce::Point<float>) const;      // node under the mouse, or -1
    juce::Point<float> bandNodePos (int b) const;     // node = (freq, -depth)
    int dragBand { -1 }, hoverBand { -1 };

    VocalGzzioProcessor& proc;
    juce::Rectangle<float> plot;         // inner plotting area (set in paint)
    juce::Image gridImage;               // v1.5.0: cached static grid (rebuilt on resize)

    juce::dsp::FFT fft { 12 };           // 2^12 = 4096 = analyzerSize
    juce::dsp::WindowingFunction<float> window { (size_t) VocalGzzioProcessor::analyzerSize,
                                                 juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> timeData, fftData, specDb;

    static constexpr float fMin  = 20.0f;
    static constexpr float fMax  = 20000.0f;
    static constexpr float dbMax = 18.0f;
};

//==============================================================================
// Slider whose value box opens for typing on a single click (the knob face
// still drags normally). Makes every parameter directly type-editable.
class ClickToEditSlider : public juce::Slider
{
public:
    ClickToEditSlider() = default;

    void mouseDown (const juce::MouseEvent& e) override
    {
        // If the click lands in the text-box strip at the bottom, edit it.
        if (getTextBoxPosition() != juce::Slider::NoTextBox && textBoxArea().contains (e.getPosition()))
        {
            showTextBox();
            return;
        }
        juce::Slider::mouseDown (e);
    }

private:
    juce::Rectangle<int> textBoxArea() const
    {
        // Mirror the layout for each text-box position.
        switch (getTextBoxPosition())
        {
            case juce::Slider::TextBoxRight: return getLocalBounds().removeFromRight (getTextBoxWidth());
            case juce::Slider::TextBoxLeft:  return getLocalBounds().removeFromLeft  (getTextBoxWidth());
            case juce::Slider::TextBoxAbove: return getLocalBounds().removeFromTop    (getTextBoxHeight());
            default:                         return getLocalBounds().removeFromBottom (getTextBoxHeight());
        }
    }
};

//==============================================================================
// Small red LED placed at the centre of a knob. Lit = module active; click to
// bypass the whole module (lamp goes dark). Wired to a bool parameter.
class LampButton : public juce::Button
{
public:
    LampButton() : juce::Button ({}) { setClickingTogglesState (true); }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        const auto c   = r.getCentre();
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 1.0f;
        const float ir  = rad * 0.72f;   // v1.6.1: bigger lamp body (was 0.60)
        if (getToggleState())
        {
            g.setColour (Palette::salmon.withAlpha (0.30f));   // glow halo
            g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
            g.setColour (over ? Palette::salmon.brighter (0.25f) : Palette::salmon);
            g.fillEllipse (c.x - ir, c.y - ir, ir * 2.0f, ir * 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.7f));   // specular dot
            g.fillEllipse (c.x - ir * 0.4f, c.y - ir * 0.6f, ir * 0.55f, ir * 0.45f);
        }
        else
        {
            g.setColour (juce::Colour (0xff141920));
            g.fillEllipse (c.x - ir, c.y - ir, ir * 2.0f, ir * 2.0f);
            g.setColour (over ? Palette::inkSoft : Palette::panelLn);
            g.drawEllipse (c.x - ir, c.y - ir, ir * 2.0f, ir * 2.0f, 1.2f);
        }
    }
};

//==============================================================================
//==============================================================================
// v1.7.0 theme backdrop. Drawn at the very start of Content::paint (a paint
// helper, NOT a child component) so the header bar and cards render on top of
// it. Yuru-kawa is a fully STATIC image (soft mint gradient + a few bokeh dots
// + the mascot) -> zero extra repaints, safe for a real-time plug-in.
class ThemePainter
{
public:
    void setMascot (const juce::Image& img) { mascot = img; }
    void setBounds (juce::Rectangle<int> b) { area = b; }
    void setMode   (int m, juce::Rectangle<int> b) { mode = m; area = b; }

    // returns true if a themed background was painted (caller then skips its own)
    bool paint (juce::Graphics& g)
    {
        if (mode == 1) { paintYuruKawa (g); return true; }
        if (mode == 2) { paintNature   (g); return true; }
        if (mode == 3) { paintBrand    (g); return true; }
        if (mode == 4) { paintAurora   (g); return true; }
        return false;
    }

private:
    void paintYuruKawa (juce::Graphics& g)
    {
        auto r = area.toFloat();

        // milky cream base
        g.fillAll (juce::Colour (0xfffbf7f1));

        // subtle WATERCOLOUR blooms: radial gradients fading to transparent, so the
        // colour bleeds softly with no hard edge. Low alpha keeps it light/washy.
        struct Bloom { float x, y, rad; juce::uint32 col; };
        static const Bloom blooms[] = {
            { 0.12f, 0.15f, 0.34f, 0x26afddcb },   // mint
            { 0.82f, 0.10f, 0.30f, 0x24f4b4c2 },   // sakura
            { 0.92f, 0.60f, 0.40f, 0x20aec9e4 },   // sky blue
            { 0.28f, 0.84f, 0.34f, 0x1cf0d27a },   // butter
            { 0.55f, 0.42f, 0.40f, 0x16f4b4c2 },   // soft pink centre
            { 0.04f, 0.58f, 0.30f, 0x1cafddcb }    // mint 2
        };
        const float rad = juce::jmax (r.getWidth(), r.getHeight());
        for (auto& b : blooms)
        {
            const float cx = r.getX() + r.getWidth()  * b.x;
            const float cy = r.getY() + r.getHeight() * b.y;
            const float rr = rad * b.rad;
            juce::Colour c (b.col);
            juce::ColourGradient grad (c, cx, cy, c.withAlpha (0.0f), cx + rr, cy, true);
            g.setGradientFill (grad);
            g.fillEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f);
        }
        // NB: the mascot is drawn big & translucent in the FOREGROUND (end of
        // Content::paint) so it reads as a "see-through" hero over the UI.
    }

    // ==================================================================
    // v1.8.1 themed scenery — 「でっかく・分かりやすく」全面刷新
    //  自然(2): 四季40秒サイクル(2秒クロスフェード)。象徴モチーフを大きく透過配置
    //  ブランド(3): モノグラム・キャンバス様式(組みイニシャル+四弁花+星の規則配置)
    //              +ダミエ(市松)帯+大型紋章 — 実在ロゴは商標のため自作紋章
    //  ライブ(4): 大型トラス/ムービングライト/ラインアレイ/サブ/ウェッジ
    //  ペンライトの海は「5人ユニゾンON」の演出として全テーマ共通 (jnActive)
    // ==================================================================
public:
    float animT = 0.0f;
    bool  jnActive = false;
    float level = 0.0f;                      // 平滑入力レベル 0..1 (声連動演出)
    int   mouseX = -9999, mouseY = -9999;    // カーソル(コンテンツ座標)
    bool  freezeTime = false;                // GZ_SEASON固定時: animTを進めない(検証用)

    // ---- パーティクル (花びら/落ち葉/雪): カーソルで押し流せる ----
    struct Pt { float x, y, vx, vy, rot, vr, size; int kind; };
    std::vector<Pt> parts;
    void seedParts (int season)
    {
        parts.clear();
        const int n = season == 3 ? 42 : 26;
        for (int i = 0; i < n; ++i)
        {
            Pt q {};
            q.x = area.getX() + hash01 (i * 7 + 1) * area.getWidth();
            q.y = area.getY() + hash01 (i * 7 + 2) * area.getHeight();
            q.vx = 0; q.vy = 0;
            q.rot = hash01 (i * 7 + 3) * 6.28f;
            q.vr  = (hash01 (i * 7 + 4) - 0.5f) * 2.2f;
            if (season == 0)      { q.kind = 0; q.size = 6 + 6 * hash01 (i); }        // 花びら
            else if (season == 2) { q.kind = (i % 3 == 2) ? 2 : 1;                     // 紅葉/いちょう
                                    q.size = 12 + 9 * hash01 (i); }
            else                  { q.kind = (i % 7 == 0) ? 4 : 3;                     // 雪/結晶
                                    q.size = q.kind == 4 ? 12 + 6 * hash01 (i)
                                                         : 2.5f + 3.5f * hash01 (i); }
            parts.push_back (q);
        }
        partSeason = season;
    }
    int partSeason = -1;
    void update (float dt, int mx, int my, float lv)
    {
        if (! freezeTime) animT += dt;       // GZ_SEASON固定時はanimTを止める
        level += (lv - level) * 0.25f;
        mouseX = mx; mouseY = my;
        if (mode != 2) return;
        const int season = currentSeason();
        if (season == 1) { parts.clear(); partSeason = 1; return; }   // 夏は波のみ
        if (partSeason != season || parts.empty()) seedParts (season);
        const float W = (float) area.getWidth();
        for (auto& q : parts)
        {
            // 基本挙動: ゆっくり落下 + 横揺れ
            const float sway = std::sin (animT * 0.8f + q.rot * 3.0f);
            float ax = sway * (q.kind == 3 ? 3.0f : 8.0f);
            float ay = (q.kind == 0 ? 26.0f : q.kind == 3 ? 22.0f : q.kind == 4 ? 14.0f : 30.0f);
            // カーソルで押し流す (半径120px)
            const float dx = q.x - (float) mouseX, dy = q.y - (float) mouseY;
            const float d2 = dx*dx + dy*dy;
            if (d2 < 120.0f * 120.0f && d2 > 1.0f)
            {
                const float d = std::sqrt (d2), f = (120.0f - d) / 120.0f * 1800.0f / d;
                q.vx += dx * f * dt;  q.vy += dy * f * dt;
            }
            q.vx += (ax - q.vx) * dt * 1.6f;
            q.vy += (ay - q.vy) * dt * 1.6f;
            q.x += q.vx * dt;  q.y += q.vy * dt;
            q.rot += q.vr * dt * (1.0f + std::abs (q.vx) * 0.02f);
            if (q.y > area.getBottom() + 24) { q.y = area.getY() - 20; q.x = area.getX() + hash01 ((int)(q.x*13)) * W; q.vy = 0; }
            if (q.x < area.getX() - 30)  q.x += W + 60;
            if (q.x > area.getRight() + 30) q.x -= W + 60;
        }
    }
    void drawParts (juce::Graphics& g, float a)
    {
        for (auto& q : parts)
        {
            switch (q.kind)
            {
                case 0:
                {
                    g.setColour (juce::Colour (0xffff9fc0).withAlpha (0.95f * a));
                    juce::Path pt; pt.addEllipse (-q.size, -q.size*0.62f, q.size*2, q.size*1.24f);
                    g.fillPath (pt, juce::AffineTransform::rotation (q.rot).translated (q.x, q.y));
                    break;
                }
                case 1: drawMaple (g, q.x, q.y, q.size, q.rot,
                                   (((int) (q.size*10)) % 2 == 0 ? juce::Colour (0xffd9503a)
                                                                 : juce::Colour (0xffe8862e))
                                       .withAlpha (a)); break;
                case 2: drawGinkgo (g, q.x, q.y, q.size, q.rot, a); break;
                case 4: drawFlake  (g, q.x, q.y, q.size, q.rot, 0.95f * a, true); break;
                default:
                    g.setColour (juce::Colour (0xff8aa4c4).withAlpha (0.55f * a));
                    g.fillEllipse (q.x-0.9f, q.y-0.9f, q.size+1.8f, q.size+1.8f);
                    g.setColour (juce::Colours::white.withAlpha (0.9f * a));
                    g.fillEllipse (q.x, q.y, q.size, q.size);
            }
        }
    }

    static float hash01 (int i)
    {
        const float s = std::sin ((float) i * 12.9898f + 4.1414f) * 43758.547f;
        return s - std::floor (s);
    }
    int currentSeason() const { return (int) (std::fmod (animT, 60.0f) / 15.0f) % 4; }

    void paintNature (juce::Graphics& g)
    {
        const float len = 15.0f, xf = 2.0f;   // v1.8.3: 季節サイクル高速化(15秒/季)
        const float t  = std::fmod (animT, len * 4.0f);
        const int   s  = (int) (t / len) % 4;
        const float ph = std::fmod (t, len);
        paintSeason (g, s, 1.0f);
        if (ph > len - xf) paintSeason (g, (s + 1) % 4, (ph - (len - xf)) / xf);
    }
    void paintSeason (juce::Graphics& g, int season, float alpha)
    {
        auto r = area.toFloat();
        const bool layer = alpha < 0.999f;
        if (layer) g.beginTransparencyLayer (alpha);
        switch (season)
        {
            case 0:  paintSpring (g, r); break;
            case 1:  paintSummer (g, r); break;
            case 2:  paintAutumn (g, r); break;
            default: paintWinter (g, r); break;
        }
        if (layer) g.endTransparencyLayer();
    }

    // ---- 共通: 大きな木 ----
    // ---- 春 ----
    void paintSpring (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setGradientFill ({ juce::Colour (0xffffd9e6), r.getCentreX(), r.getY(),
                             juce::Colour (0xfffff6ea), r.getCentreX(), r.getBottom(), false });
        g.fillRect (r);
        g.setColour (juce::Colour (0x4c9ed49e));
        g.fillEllipse (r.getX()-140, r.getBottom()-72, r.getWidth()*0.85f, 170);
        g.fillEllipse (r.getCentreX()-40, r.getBottom()-58, r.getWidth()*0.85f, 160);
    }
    void drawBlossom (juce::Graphics& g, float cx, float cy, float s, float a)
    {
        g.setColour (juce::Colour (0xffff9fc0).withMultipliedAlpha (a));
        for (int k = 0; k < 5; ++k)
        {
            const float an = juce::MathConstants<float>::twoPi * k / 5.0f - 1.5708f;
            g.fillEllipse (cx + std::cos(an)*s*0.55f - s*0.36f,
                           cy + std::sin(an)*s*0.55f - s*0.45f, s*0.72f, s*0.9f);
        }
        g.setColour (juce::Colour (0xffffe08a).withMultipliedAlpha (a));
        g.fillEllipse (cx - s*0.17f, cy - s*0.17f, s*0.34f, s*0.34f);
    }
    // ---- 夏 ----
    void paintSummer (juce::Graphics& g, juce::Rectangle<float> r)
    {
        // v1.8.4: UIパネルが下半分を覆うため、水平線を上部(見える帯)に置く。
        // 空は上26%、海は上26%〜下端まで。これで「空と海」が中段の隙間で明快に見える。
        const float hz = r.getY() + r.getHeight() * 0.26f;
        g.setGradientFill ({ juce::Colour (0xff5fb8f0), r.getCentreX(), r.getY(),
                             juce::Colour (0xffd8f0ff), r.getCentreX(), hz, false });
        g.fillRect (r.withBottom (hz));
        // 海: しっかり濃い青で「海」と分かる彩度に
        g.setGradientFill ({ juce::Colour (0xff0a6bb8), r.getCentreX(), hz,
                             juce::Colour (0xff1f92d8), r.getCentreX(), r.getBottom(), false });
        g.fillRect (r.withTop (hz));
        // 太陽 (空の右上)
        const float sx = r.getX() + r.getWidth() * 0.80f, sy = r.getY() + r.getHeight() * 0.10f;
        juce::Colour sun (0x77ffe9a0);
        g.setGradientFill ({ sun, sx, sy, sun.withAlpha (0.0f), sx + 180, sy, true });
        g.fillRect (r.withBottom (hz));
        g.setColour (juce::Colour (0xffffde6a)); g.fillEllipse (sx - 34, sy - 34, 68, 68);
        // 雲 (空を流れる)
        for (int c = 0; c < 2; ++c)
        {
            const float w = 150 + 60 * c;
            const float cx = std::fmod (animT * (7 + 4 * c) + c * 480.0f, r.getWidth() + w * 2) - w;
            const float cy = r.getY() + 30 + 40 * c;
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillEllipse (cx, cy, w * 0.55f, 34);
            g.fillEllipse (cx + w * 0.26f, cy - 16, w * 0.55f, 42);
            g.fillEllipse (cx + w * 0.52f, cy, w * 0.48f, 30);
        }
        // 帆船シルエット (海と分かる目印・ゆっくり横切る)
        {
            const float bx = std::fmod (animT * 9.0f + 200.0f, r.getWidth() + 220.0f) - 110.0f;
            const float by = hz + 24.0f;
            g.setColour (juce::Colour (0xcc0d3a5a));
            juce::Path hull; hull.startNewSubPath (bx - 34, by);
            hull.lineTo (bx + 34, by); hull.lineTo (bx + 24, by + 14);
            hull.lineTo (bx - 24, by + 14); hull.closeSubPath(); g.fillPath (hull);
            g.drawLine (bx, by, bx, by - 40, 2.5f);
            juce::Path sail; sail.startNewSubPath (bx + 2, by - 38);
            sail.lineTo (bx + 26, by - 6); sail.lineTo (bx + 2, by - 6); sail.closeSubPath();
            g.fillPath (sail);
            juce::Path jib; jib.startNewSubPath (bx - 2, by - 34);
            jib.lineTo (bx - 20, by - 6); jib.lineTo (bx - 2, by - 6); jib.closeSubPath();
            g.fillPath (jib);
        }
        // 海面の波: 濃い青の上に白い波頭を何本も (海だと一目で分かる)
        for (int wv = 0; wv < 9; ++wv)
        {
            const float y = hz + 22 + wv * (r.getHeight() * 0.74f / 9.0f);
            g.setColour (juce::Colours::white.withAlpha (0.55f - wv * 0.03f));
            juce::Path wp; wp.startNewSubPath (r.getX(), y);
            for (int sg = 1; sg <= 12; ++sg)
            {
                const float x1 = r.getX() + r.getWidth() * sg / 12.0f;
                const float ym = y + std::sin (animT * 0.8f + sg * 1.1f + wv * 1.7f) * 5;
                wp.quadraticTo (x1 - r.getWidth() / 24.0f, ym + 7, x1, ym);
            }
            g.strokePath (wp, juce::PathStrokeType (2.6f));
        }
        // 水面のきらめき (海全体)
        for (int i = 0; i < 60; ++i)
        {
            const float x = r.getX() + hash01 (i * 5 + 3) * r.getWidth();
            const float y = hz + 10 + hash01 (i * 5 + 4) * (r.getHeight() - (hz - r.getY()) - 10);
            const float tw = 0.5f + 0.5f * std::sin (animT * 2.2f + i * 2.1f);
            g.setColour (juce::Colours::white.withAlpha (0.45f * tw));
            g.fillEllipse (x, y, 3.0f, 1.6f);
        }
    }
    // ---- 秋 ----
    void paintAutumn (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setGradientFill ({ juce::Colour (0xffffdfae), r.getCentreX(), r.getY(),
                             juce::Colour (0xffffefd6), r.getCentreX(), r.getBottom(), false });
        g.fillRect (r);
        g.setColour (juce::Colour (0x55d9803a));
        g.fillEllipse (r.getX()-120, r.getBottom()-62, r.getWidth()*1.35f, 140);
    }
    void drawMaple (juce::Graphics& g, float cx, float cy, float s, float rot, juce::Colour col)
    {
        juce::Path p;
        for (int k = 0; k < 5; ++k)
        {
            const float an = -juce::MathConstants<float>::halfPi + (k - 2) * 0.62f;
            const float tx = std::cos(an)*s, ty = std::sin(an)*s;
            const float lx = std::cos(an-0.30f)*s*0.45f, ly = std::sin(an-0.30f)*s*0.45f;
            const float rx = std::cos(an+0.30f)*s*0.45f, ry = std::sin(an+0.30f)*s*0.45f;
            juce::Path lobe; lobe.startNewSubPath (0,0);
            lobe.lineTo (lx,ly); lobe.lineTo (tx,ty); lobe.lineTo (rx,ry); lobe.closeSubPath();
            p.addPath (lobe);
        }
        p.addRectangle (-s*0.05f, 0, s*0.1f, s*0.6f);
        g.setColour (col);
        g.fillPath (p, juce::AffineTransform::rotation (rot).translated (cx, cy));
    }
    void drawGinkgo (juce::Graphics& g, float cx, float cy, float s, float rot, float a)
    {
        juce::Path f; f.startNewSubPath (0, s*0.62f);
        f.lineTo (-s*0.08f, 0);
        f.addArc (-s, -s, s*2, s*2, -0.85f, -0.16f, false);
        f.lineTo (-s*0.03f, -s*0.1f);
        f.addArc (-s, -s, s*2, s*2, 0.16f, 0.85f, false);
        f.lineTo (s*0.08f, 0); f.closeSubPath();
        g.setColour (juce::Colour (0xffe8c23a).withMultipliedAlpha (a));
        g.fillPath (f, juce::AffineTransform::rotation (rot).translated (cx, cy));
    }
    // ---- 冬 (空を青灰に: 白い雪が「見える」) ----
    void paintWinter (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setGradientFill ({ juce::Colour (0xff9db6d0), r.getCentreX(), r.getY(),
                             juce::Colour (0xffdfeaf4), r.getCentreX(), r.getBottom(), false });
        g.fillRect (r);
        const float gy = r.getBottom() - r.getHeight()*0.12f;
        g.setColour (juce::Colour (0xfff2f7fc));
        g.fillRoundedRectangle (r.getX()-20, gy, r.getWidth()+40, r.getHeight()*0.14f+20, 18);
        g.setColour (juce::Colour (0x889db8d8));
        g.drawLine (r.getX(), gy+2, r.getRight(), gy+2, 2.5f);
    }
    void drawFlake (juce::Graphics& g, float cx, float cy, float s, float rot,
                    float alpha, bool outlined)
    {
        auto pass = [&] (juce::Colour c, float w)
        {
            g.setColour (c.withAlpha (alpha));
            for (int k = 0; k < 6; ++k)
            {
                const float an = rot + juce::MathConstants<float>::pi * k / 3.0f;
                const float dx = std::cos(an), dy = std::sin(an);
                g.drawLine (cx, cy, cx + dx*s, cy + dy*s, w);
                const float px = -dy, py = dx;
                for (float f : { 0.45f, 0.72f })
                {
                    g.drawLine (cx + dx*s*f, cy + dy*s*f,
                                cx + dx*s*(f+0.2f) + px*s*0.2f,
                                cy + dy*s*(f+0.2f) + py*s*0.2f, w*0.8f);
                    g.drawLine (cx + dx*s*f, cy + dy*s*f,
                                cx + dx*s*(f+0.2f) - px*s*0.2f,
                                cy + dy*s*(f+0.2f) - py*s*0.2f, w*0.8f);
                }
            }
            g.fillEllipse (cx - w, cy - w, w*2, w*2);
        };
        if (outlined) pass (juce::Colour (0xff5c7ca4), s*0.16f);
        pass (juce::Colours::white, s*0.085f);
    }
    // ---- ブランド (モノグラム・キャンバス様式 / 家紋様配置) ----
    void drawGZMono (juce::Graphics& g, float cx, float cy, float s, juce::Colour c, float a)
    {
        g.setColour (c.withMultipliedAlpha (a));
        juce::Path gp; gp.addArc (cx - s, cy - s, s*2, s*2, 0.65f, 5.9f, true);
        g.strokePath (gp, juce::PathStrokeType (s*0.30f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
        g.drawLine (cx + s*0.22f, cy + s*0.06f, cx + s*0.95f, cy + s*0.06f, s*0.30f);
        juce::Path z;
        z.startNewSubPath (cx - s*0.5f, cy - s*0.48f);
        z.lineTo (cx + s*0.5f, cy - s*0.48f); z.lineTo (cx - s*0.5f, cy + s*0.48f);
        z.lineTo (cx + s*0.5f, cy + s*0.48f);
        g.strokePath (z, juce::PathStrokeType (s*0.20f));
    }
    void drawNote8 (juce::Graphics& g, float cx, float cy, float s,
                    juce::Colour c, float a, bool flip)
    {   // 八分音符 (オリジナル・モチーフ)
        const float f = flip ? -1.0f : 1.0f;
        g.setColour (c.withMultipliedAlpha (a));
        juce::Path h; h.addEllipse (-s*0.52f, -s*0.34f, s*1.04f, s*0.68f);
        auto t = juce::AffineTransform::rotation (-0.35f * f)
                     .translated (cx - f*s*0.25f, cy + s*0.55f);
        g.fillPath (h, t);
        g.drawLine (cx + f*s*0.24f, cy + s*0.45f, cx + f*s*0.24f, cy - s*0.85f, s*0.16f);
        juce::Path fl; fl.startNewSubPath (cx + f*s*0.24f, cy - s*0.85f);
        fl.quadraticTo (cx + f*s*0.85f, cy - s*0.62f, cx + f*s*0.62f, cy - s*0.12f);
        g.strokePath (fl, juce::PathStrokeType (s*0.15f, juce::PathStrokeType::curved));
    }
    void drawLozenge (juce::Graphics& g, float cx, float cy, float s,
                      juce::Colour c, float a)
    {   // 菱形+中心点 (和の割り菱をアレンジ)
        juce::Path d;
        d.startNewSubPath (cx, cy - s);      d.lineTo (cx + s*0.72f, cy);
        d.lineTo (cx, cy + s);               d.lineTo (cx - s*0.72f, cy);
        d.closeSubPath();
        g.setColour (c.withMultipliedAlpha (a));
        g.strokePath (d, juce::PathStrokeType (s*0.15f));
        g.fillEllipse (cx - s*0.17f, cy - s*0.17f, s*0.34f, s*0.34f);
    }
    void drawMedallion (juce::Graphics& g, float cx, float cy, float s, float a)
    {   // 小メダリオン (円環+GZ) — 四隅用
        const juce::Colour gold (0xffe0c48c);
        g.setColour (juce::Colour (0xcc241b12).withMultipliedAlpha (a));
        g.fillEllipse (cx - s, cy - s, s*2, s*2);
        g.setColour (gold.withMultipliedAlpha (a));
        g.drawEllipse (cx - s, cy - s, s*2, s*2, s*0.12f);
        g.drawEllipse (cx - s*0.78f, cy - s*0.78f, s*1.56f, s*1.56f, s*0.05f);
        drawGZMono (g, cx, cy, s*0.42f, gold, a);
    }
    void drawMonoCanvas (juce::Graphics& g, juce::Rectangle<float> r, float a)
    {
        const juce::Colour beige (0xffe0c48c);   // v1.9.0: シャンパン
        const float cell = 66.0f;
        int row = 0;
        for (float y = r.getY() + 26; y < r.getBottom() + cell; y += cell*0.86f, ++row)
        {
            const float off = (row % 2) * cell * 0.5f;
            int col = 0;
            for (float x = r.getX() + off + 8; x < r.getRight() + cell; x += cell, ++col)
            {
                switch ((row + col*2) % 4)
                {
                    case 0:  drawGZMono  (g, x, y, 13.0f, beige, a);         break;
                    case 1:  drawNote8   (g, x, y, 11.0f, beige, a, false);  break;
                    case 2:  drawLozenge (g, x, y, 12.0f, beige, a);         break;
                    default: drawNote8   (g, x, y, 11.0f, beige, a, true);   break;
                }
            }
        }
    }
    // v1.9.0: 市松(ダミエ調)は既存ブランドを想起させるため、
    //          自社モチーフの「シェブロン織り」ストライプに置き換え。
    void drawWeaveBand (juce::Graphics& g, juce::Rectangle<float> band, float a)
    {
        const juce::Colour champagne (0xffe0c48c);
        const juce::Colour plum      (0xff2a2038);
        g.setColour (plum.withMultipliedAlpha (a));
        g.fillRect (band);

        const float h = band.getHeight();
        const float step = h * 0.9f;
        g.setColour (champagne.withMultipliedAlpha (0.55f * a));
        for (float x = band.getX() - h; x < band.getRight() + h; x += step)
        {
            juce::Path chev;                       // ＞ 型を並べた織り目
            chev.startNewSubPath (x,              band.getBottom());
            chev.lineTo          (x + h * 0.5f,   band.getCentreY());
            chev.lineTo          (x,              band.getY());
            g.strokePath (chev, juce::PathStrokeType (h * 0.16f));
        }
        g.setColour (champagne.withMultipliedAlpha (0.85f * a));
        g.drawRect (band, 1.2f);
    }
    void drawCrest (juce::Graphics& g, float cx, float cy, float s, float a)
    {
        const juce::Colour gold (0xffe0c48c);
        g.setColour (gold.withMultipliedAlpha (a));
        for (int side = -1; side <= 1; side += 2)
        {
            juce::Path br; br.startNewSubPath (cx + side*s*0.95f, cy + s*0.55f);
            br.quadraticTo (cx + side*s*1.28f, cy, cx + side*s*0.9f, cy - s*0.62f);
            g.strokePath (br, juce::PathStrokeType (s*0.05f));
            for (int L = 0; L < 6; ++L)
            {
                const float t = L / 5.0f;
                const float lx = cx + side*(s*1.02f + s*0.12f*std::sin (t*3.1f));
                const float ly = cy + s*0.55f - t * s*1.15f;
                juce::Path leaf; leaf.addEllipse (-s*0.055f, -s*0.15f, s*0.11f, s*0.3f);
                g.fillPath (leaf, juce::AffineTransform::rotation (side*0.6f).translated (lx, ly));
            }
        }
        juce::Path shield;
        shield.startNewSubPath (cx - s*0.6f, cy - s*0.62f);
        shield.lineTo (cx + s*0.6f, cy - s*0.62f);
        shield.quadraticTo (cx + s*0.62f, cy + s*0.25f, cx, cy + s*0.7f);
        shield.quadraticTo (cx - s*0.62f, cy + s*0.25f, cx - s*0.6f, cy - s*0.62f);
        shield.closeSubPath();
        g.setColour (juce::Colour (0xff1d1428).withMultipliedAlpha (a)); g.fillPath (shield);
        g.setColour (gold.withMultipliedAlpha (a));
        g.strokePath (shield, juce::PathStrokeType (s*0.07f));
        drawGZMono (g, cx, cy - s*0.03f, s*0.30f, gold, a);
        juce::Font ef (juce::FontOptions (s*0.36f));
        g.setFont (ef.boldened());
        g.drawText ("G'ZZIO", (int)(cx - s), (int)(cy + s*0.72f), (int)(s*2), (int)(s*0.44f),
                    juce::Justification::centred);
        juce::Font sf (juce::FontOptions (s*0.17f));
        g.setFont (sf);
        g.setColour (gold.withMultipliedAlpha (0.85f*a));
        g.drawText ("VOCAL MAISON", (int)(cx - s), (int)(cy + s*1.12f), (int)(s*2), (int)(s*0.26f),
                    juce::Justification::centred);
    }
    void paintBrand (juce::Graphics& g)
    {
        if (! brandCache.isValid()
            || brandCache.getWidth()  != area.getWidth()
            || brandCache.getHeight() != area.getHeight())
        {
            brandCache = juce::Image (juce::Image::RGB,
                                      juce::jmax (1, area.getWidth()),
                                      juce::jmax (1, area.getHeight()), true);
            juce::Graphics cg (brandCache);
            auto r = area.toFloat().withZeroOrigin();
            cg.setGradientFill ({ juce::Colour (0xff2a2038), r.getCentreX(), r.getY(),
                                  juce::Colour (0xff140e1d), r.getCentreX(), r.getBottom(), false });
            cg.fillRect (r);
            drawMonoCanvas (cg, r, 0.35f);
            drawCrest (cg, r.getCentreX(), r.getBottom() - 118.0f, 74.0f, 0.9f);   // パネル裏
            drawWeaveBand (cg, { r.getX(), r.getY(),              r.getWidth(), 14.0f }, 0.95f);
            drawWeaveBand (cg, { r.getX(), r.getBottom() - 14.0f, r.getWidth(), 14.0f }, 0.95f);
            auto fr = r.reduced (7.0f);
            cg.setColour (juce::Colour (0xaae0c48c));
            cg.drawRoundedRectangle (fr, 8.0f, 2.4f);
            cg.setColour (juce::Colour (0x8ce0c48c));
            for (float x = fr.getX()+20; x < fr.getRight()-20; x += 13)
            {
                cg.fillRoundedRectangle (x, fr.getY()+18,      7, 2, 1);
                cg.fillRoundedRectangle (x, fr.getBottom()-20,  7, 2, 1);
            }
        }
        g.drawImageAt (brandCache, area.getX(), area.getY());
    }

    // ---- オーロラ (声連動): 星空 + ゆらめくカーテン + 流れ星 ----
    void paintAurora (juce::Graphics& g)
    {
        auto r = area.toFloat();
        // 静的部分(夜空グラデ+基準の星90個)は1回だけ描いてキャッシュ
        if (! auroraCache.isValid()
            || auroraCache.getWidth()  != area.getWidth()
            || auroraCache.getHeight() != area.getHeight())
        {
            auroraCache = juce::Image (juce::Image::RGB,
                                       juce::jmax (1, area.getWidth()),
                                       juce::jmax (1, area.getHeight()), true);
            juce::Graphics cg (auroraCache);
            auto rz = r.withZeroOrigin();
            cg.setGradientFill ({ juce::Colour (0xff0b1026), rz.getCentreX(), rz.getY(),
                                  juce::Colour (0xff03050e), rz.getCentreX(), rz.getBottom(), false });
            cg.fillRect (rz);
            for (int i = 0; i < 90; ++i)
            {
                const float x = rz.getX() + hash01 (i*3+1) * rz.getWidth();
                const float y = rz.getY() + hash01 (i*3+2) * rz.getHeight() * 0.9f;
                const float sz = hash01 (i*3) < 0.12f ? 2.6f : 1.5f;
                cg.setColour (juce::Colours::white.withAlpha (0.30f + 0.35f * hash01 (i+7)));
                cg.fillEllipse (x, y, sz, sz);
            }
        }
        g.drawImageAt (auroraCache, area.getX(), area.getY());
        // 明るい星だけ声連動で瞬く (毎フレームは13個のみ)
        for (int i = 0; i < 90; i += 7)
        {
            const float x = r.getX() + hash01 (i*3+1) * r.getWidth();
            const float y = r.getY() + hash01 (i*3+2) * r.getHeight() * 0.9f;
            const float tw = 0.5f + 0.5f * std::sin (animT * (1.2f + hash01(i)) + i);
            const float aA = tw * (0.35f + 0.65f * juce::jmin (1.0f, 0.35f + level));
            g.setColour (juce::Colours::white.withAlpha (aA));
            g.fillEllipse (x - 0.6f, y - 0.6f, 2.8f, 2.8f);
            if (tw > 0.8f)
            {
                g.setColour (juce::Colours::white.withAlpha (aA * 0.5f));
                g.drawLine (x - 4, y + 1, x + 6, y + 1, 1.0f);
                g.drawLine (x + 1, y - 4, x + 1, y + 6, 1.0f);
            }
        }
        // オーロラ・カーテン 3本 (声量で明るく)
        static const juce::uint32 ac[3] = { 0xff43e8b8, 0xff6fb8ff, 0xffb08cff };
        for (int b2 = 0; b2 < 3; ++b2)
        {
            const float base = r.getY() + r.getHeight() * (0.16f + 0.10f * b2);
            const float amp  = 26.0f + 12.0f * b2;
            juce::Path top, bot;
            for (int sgx = 0; sgx <= 24; ++sgx)
            {
                const float x = r.getX() + r.getWidth() * sgx / 24.0f;
                const float ph = animT * (0.35f + 0.12f * b2) + b2 * 2.1f;
                const float y = base + std::sin (x * 0.006f + ph) * amp
                              + std::sin (x * 0.017f - ph * 1.7f) * amp * 0.35f;
                if (sgx == 0) { top.startNewSubPath (x, y); }
                else            top.lineTo (x, y);
            }
            juce::Path curtain (top);
            curtain.lineTo (r.getRight(), r.getY() + r.getHeight() * 0.62f);
            curtain.lineTo (r.getX(),     r.getY() + r.getHeight() * 0.62f);
            curtain.closeSubPath();
            const float aA = (0.16f + 0.05f * (2 - b2)) * (0.55f + 0.9f * level);
            juce::Colour c (ac[b2]);
            g.setGradientFill ({ c.withAlpha (aA), r.getCentreX(), base - amp,
                                 c.withAlpha (0.0f), r.getCentreX(),
                                 r.getY() + r.getHeight() * 0.62f, false });
            g.fillPath (curtain);
            g.setColour (c.withAlpha (juce::jmin (0.85f, aA * 2.2f)));
            g.strokePath (top, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
        }
        // 流れ星 (約7秒ごと)
        const float cyc = std::fmod (animT, 7.0f);
        if (cyc < 0.7f)
        {
            const int seed = (int) (animT / 7.0f);
            const float p = cyc / 0.7f;
            const float x0 = r.getX() + r.getWidth() * (0.15f + 0.7f * hash01 (seed));
            const float y0 = r.getY() + 30 + 60 * hash01 (seed + 9);
            const float x = x0 + p * 190.0f, y = y0 + p * 70.0f;
            g.setColour (juce::Colours::white.withAlpha ((1.0f - p) * 0.9f));
            g.drawLine (x - 46, y - 17, x, y, 2.2f);
            g.fillEllipse (x - 2, y - 2, 4, 4);
        }
        // 地平のシルエット
        g.setColour (juce::Colour (0xff060812));
        juce::Path hills; hills.startNewSubPath (r.getX(), r.getBottom());
        hills.lineTo (r.getX(), r.getBottom() - 26);
        hills.quadraticTo (r.getX() + r.getWidth()*0.3f, r.getBottom() - 54,
                           r.getCentreX(), r.getBottom() - 30);
        hills.quadraticTo (r.getX() + r.getWidth()*0.75f, r.getBottom() - 10,
                           r.getRight(), r.getBottom() - 34);
        hills.lineTo (r.getRight(), r.getBottom());
        hills.closeSubPath(); g.fillPath (hills);
        // v1.8.3: 下部の背景帯(46px)に湖面のオーロラ反射 (声で明るく)
        static const juce::uint32 rc[3] = { 0xff43e8b8, 0xff6fb8ff, 0xffb08cff };
        for (int i = 0; i < 3; ++i)
        {
            const juce::Colour c ((juce::uint32) rc[i]);
            const float aa = (0.10f + 0.06f * i) * (0.5f + 1.0f * level);
            const float y = r.getBottom() - 40 + i * 12.0f;
            g.setGradientFill ({ c.withAlpha (aa), r.getCentreX(), y,
                                 c.withAlpha (0.0f), r.getCentreX(), y + 18.0f, false });
            g.fillRect (juce::Rectangle<float> (r.getX(), y, r.getWidth(), 18.0f));
        }
    }
    juce::Image brandCache, brandFgCache, auroraCache;

    // ---- ライブ (大型機材) ----
    // ---- ペンライトの海 (5人ユニゾンON演出) ----
    void drawPenlights (juce::Graphics& g, juce::Rectangle<float> r, float a)
    {
        static const juce::uint32 pc[] = { 0xffff6fa0, 0xff53d4f2, 0xffffd75c,
                                           0xffb08cff, 0xff5ce8a8 };
        const float top = r.getBottom() - r.getHeight()*0.20f;
        int idx = 0;
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 18; ++col, ++idx)
            {
                const float x = r.getX() + (0.5f + col) * r.getWidth() / 18.0f
                              + (hash01(idx) - 0.5f) * 18
                              + std::sin (animT*1.6f + idx*0.7f) * 5;
                const float y = top + (0.3f + row) * r.getHeight()*0.20f / 5.0f
                              + (hash01(idx+200) - 0.5f) * 10;
                const juce::Colour c (pc[idx % 5]);
                g.setColour (c.withAlpha (0.16f * a));
                g.fillEllipse (x-7, y-7, 14, 14);
                g.setColour (c.withAlpha (0.9f * a));
                g.fillEllipse (x-2.6f, y-2.6f, 5.2f, 5.2f);
            }
    }

    // ---- 前景: パネルの上に大きく透過配置 ----
    void paintForeground (juce::Graphics& g)
    {
        auto r = area.toFloat();
        if (mode == 2)
        {
            drawParts (g, 0.6f);                 // 小粒パーティクルのみ(カーソルで押せる)
        }
        else if (mode == 3)
        {
            // 静的オーバーレイ (~200描画) は1回だけ描いて画像キャッシュ
            if (! brandFgCache.isValid()
                || brandFgCache.getWidth()  != area.getWidth()
                || brandFgCache.getHeight() != area.getHeight())
            {
                brandFgCache = juce::Image (juce::Image::ARGB,
                                            juce::jmax (1, area.getWidth()),
                                            juce::jmax (1, area.getHeight()), true);
                juce::Graphics cg (brandFgCache);
                auto rz = area.toFloat().withZeroOrigin();
                drawMonoCanvas (cg, rz, 0.08f);
                drawWeaveBand (cg, { rz.getX(), rz.getY(),              rz.getWidth(), 14.0f }, 0.55f);
                drawWeaveBand (cg, { rz.getX(), rz.getBottom() - 14.0f, rz.getWidth(), 14.0f }, 0.55f);
                const float m = 24.0f, sm = 19.0f;
                drawMedallion (cg, rz.getX() + m,      rz.getY() + m,      sm, 0.55f);
                drawMedallion (cg, rz.getRight() - m,  rz.getY() + m,      sm, 0.55f);
                drawMedallion (cg, rz.getX() + m,      rz.getBottom() - m, sm, 0.55f);
                drawMedallion (cg, rz.getRight() - m,  rz.getBottom() - m, sm, 0.55f);
            }
            g.drawImageAt (brandFgCache, area.getX(), area.getY());
        }
        else if (mode == 4)
        {
            // 上端だけ薄いオーロラのベール + 星の瞬き (操作域は素通し)
            static const juce::uint32 ac[2] = { 0xff43e8b8, 0xff6fb8ff };
            for (int b2 = 0; b2 < 2; ++b2)
            {
                juce::Path top;
                const float base = r.getY() + 26 + 16 * b2;
                for (int sgx = 0; sgx <= 20; ++sgx)
                {
                    const float x = r.getX() + r.getWidth() * sgx / 20.0f;
                    const float y = base + std::sin (x * 0.007f + animT * 0.4f + b2 * 2)
                                        * (12 + 6 * b2);
                    if (sgx == 0) top.startNewSubPath (x, y); else top.lineTo (x, y);
                }
                g.setColour (juce::Colour (ac[b2]).withAlpha (0.12f + 0.10f * level));
                g.strokePath (top, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved));
            }
        }
        if (jnActive)
            drawPenlights (g, r, 0.5f);
    }
    juce::Image          mascot;
    juce::Rectangle<int> area;
    int                  mode { 0 };
};

//==============================================================================
class VocalGzzioContent : public juce::Component, private juce::Timer
{
public:
    explicit VocalGzzioContent (VocalGzzioProcessor&);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;   // cross hover name / lite note (on top)
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // v1.7.0 theme cross-switch
    void mouseMove (const juce::MouseEvent&) override;   // cross hover (mode name)
    void mouseExit (const juce::MouseEvent&) override;

    void setFontScale (float s);
    std::function<void (float)> onScaleChange;   // window zoom ratio (geometry)
    std::function<void (float)> onFontChange;    // text size only

    // ---- v1.4.0 UI-state persistence (editor is recreated on window close) ----
    std::function<void (const juce::String&, int)> onUiStateChange;   // key,value -> persist
    void restoreUiState (bool advanced, int tab, bool rail, int theme = 0)
    {
        advancedMode = advanced;
        currentTab   = juce::jlimit (0, 1, tab);
        tuner.setRailMode (rail);
        applyModeVisibility();
        setThemeMode (theme);   // apply AFTER the LnF is attached (correct order)
    
        if (std::getenv ("GZ_JN") != nullptr)              // test hook: 5人ユニゾン強制ON
            if (auto* jp = processor.apvts.getParameter ("jn_on"))
                jp->setValueNotifyingHost (1.0f);
    }
    bool  isAdvanced() const { return advancedMode; }

    // ---- v1.7.0 theme selector ----
    // 0 ニュートラル(dark,既定) / 1 ゆるふわ(pastel) / 2 自然(四季) /
    // 3 ブランド(leather,English) / 4 ライブ(venue) / 5 軽量 / 6 ジャニーズ(unison)
    void setThemeMode (int m);
    void refreshLanguage();                                 // v1.8.0: JP <-> EN re-label
    void forceSeason (int s);                               // test hook (GZ_SEASON)
    int  getThemeMode() const { return themeMode; }
    bool pastelTheme() const { return themeMode == 1; }   // rounded font + juice knobs (ゆるふわ)
    bool liteTheme()   const { return themeMode == 5; }   // no graph, no animation (軽量)
    void  setAdvanced (bool a) { advancedMode = a; applyModeVisibility(); if (onUiStateChange) onUiStateChange ("ui_advanced", a ? 1 : 0); }
    VocalTuner& getTuner() { return tuner; }

private:
    struct Knob
    {
        ClickToEditSlider slider;
        juce::Label       label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    void timerCallback() override;   // learn-button state refresh
    void addKnob (Knob& k, const juce::String& paramID, const juce::String& text,
                  const juce::String& tooltip, const juce::String& suffix);
    void placeRow (juce::Rectangle<int> area, std::initializer_list<Knob*> ks);
    void styleButton (juce::TextButton& b);
    juce::Font cfont (float h, bool bold = false) const;
    void applyVoicePreset (int id);
    void applyMicPreset (int id);
    void applyEqPreset (int id);
    void refreshPresetDisplays();   // restore combo selections from apvts.state (display only)
    void applyTempoFit();           // set delay/reverb lengths from current BPM (v1.4.0)
    static juce::String keyName (int tonic, bool isMinor);       // v1.4.0 key label
    static juce::String suggestChords (int tonic, bool isMinor); // diatonic progression suggestion
    void  applyModeVisibility();    // show/hide advanced-only controls
    void applyScene();
    void updateABButtons();
    void savePreset();
    void loadPreset();

    // ---- v1.7.0 theme (cross-switch) ----
    void applyThemeToKnobs();                       // push per-theme arc colour to the LnF
    // v2.4.0: 0-5=テーマチップ(上段) / 6=かんたんモードのスイッチ(下段)
    juce::Rectangle<int> crossHit (int idx) const;
    void drawThemeCross (juce::Graphics& g);        // テーマチップ + かんたんモードのスイッチ
    void drawCrossSwitch (juce::Graphics&);         // (旧v1.7.0のアイコン版。未使用)
    float rowScale { 1.0f };   // v2.4.0 かんたんモードでツマミ周りの文字/箱を拡大
    bool  heroBig  { false };  // v2.4.0 おまかせ設定の帯を大きく描くか
    // v2.4.0 かんたんモードの10秒おまかせ: 0=待機 1=しずかに(2秒: 0.5s間+1.5sノイズ学習)
    // 2=うた自動(8秒)実行中。タイマー(20Hz)が easyComboTick で進める。
    int   easyComboPhase { 0 }, easyComboTick { 0 };
    // チップの並び順 -> themeMode (4=オーロラ は v2.4.0 で廃止)
    static constexpr int kThemeOfChip[6] = { 0, 1, 2, 3, 7, 5 };
    static constexpr int kChipH    = 21;            // テーマチップの高さ
    static constexpr int kSwitchW  = 244;           // かんたんモードのスイッチ(大きめ)
    static constexpr int kSwitchH  = 31;
    void drawJuiceServer (juce::Graphics&);         // v1.7.0 corner juice dispenser (yuru-kawa)

    VocalGzzioProcessor& processor;
    juce::Image          mascot;
    int                  themeMode { 0 };
    int                  crossHover { -1 };            // hovered cross cell (-1 none)
    juce::Image          kawaiiMascot;               // yuru-kawa backdrop art
    ThemePainter         themePainter;
    juce::Rectangle<int> crossArea;                  // header theme switch bounds
    int                  themeAnimFrame { 0 };       // v1.8.0 anim frame divider
    int                  lastTheme { 1 };             // v1.8.1 center-toggle restore target
    juce::TooltipWindow  tooltipWindow { this, 380 };
    VocalTuner           tuner;
    EQGraph              eqGraph;

    Knob gate, lowCut, mudK, harshK, denoiseK,
         comp1K, comp2K, attackK, releaseK, deessK,
         presenceK, airK, warmthK, sustainK, ringK,
         makeupK, mixK, widthK, doublerK, delayK, revSizeK, revMixK,
         seqAmountK, seqFocusK,                          // Smart EQ auto
         seqF1K, seqD1K, seqF2K, seqD2K, seqF3K, seqD3K; // Smart EQ manual (3 bands)

    // Preset system: voice type x mic model x scene, all combinable
    juce::ComboBox voiceBox, micBox;
    juce::ComboBox eqPresetBox;                            // famous vocal EQ recipes
    juce::String   infoText;                               // preset description shown under the graph
    juce::TextButton sceneSolo, sceneTalk, sceneBand { "Band" };
    int currentScene { 1 };   // 0 Solo, 1 Talk, 2 Band

    juce::TextButton learnButton { "LEARN" };
    juce::TextButton autoSetupButton, songSetupButton, tempoFitButton;

    // ---- v2.8.0 ★かんたんモードの「見えないのに効いている」対策 ----
    // ボイス変換やロボ声は「こだわりモード」のエフェクト欄にしかない。ところが
    // かんたんモードへ切り替えるとその欄が丸ごと消えるだけで、**音には効いたまま**
    // だった。つまり「声が変なままで、直す場所がどこにも無い」状態になり、
    // 全リセット以外に戻す手段が無かった。
    // → 効いているときだけボタンを出し、押せばその場で全部切れるようにする。
    juce::TextButton fxWarnButton;
    bool                 fxHiddenActive { false };   // かんたんモードで効いているか
    juce::Rectangle<int> fxWarnArea;                 // 文の描画位置
    bool  anyHiddenFxActive() const;                 // 判定
    void  clearHiddenFx();                           // まとめて切る
    void  updateHiddenFxWarning();                   // 表示の更新(タイマーから)
    juce::TextButton analyzeButton, keyScaleButton;   // v1.4.0 advanced analysis (Effects tab)
    juce::String     keyScaleMsg, chordMsg;
    int              analyzeMsgTtl { 0 };
    juce::String     autoSetupMsg, tempoFitMsg;
    int              tempoFitMsgTtl { 0 };   // frames left to show the tempo-fit confirmation
    juce::Rectangle<int> lvMeterArea;        // stream-loudness meter (header)
    juce::TextButton resetButton { "RESET" };
    juce::TextButton abA { "A" }, abB { "B" }, abCopy { "A>B" }, saveButton { "SAVE" }, loadButton { "LOAD" };

    // ---- v1.4.0: right-column tabs (EQ | Effects) ----
    juce::TextButton tabEqButton { "EQ" }, tabFxButton;
    // v2.4.0: modeButton(かんたん/こだわり)は廃止。ヘッダの大きなスイッチへ統合。
    bool  advancedMode { false };
    int currentTab { 0 };
    void updateTabVisibility();

    // ---- v1.4.0: effect controls (effects tab) ----
    Knob duckK, choAmtK, bpmK, dlyMsK, dlyFbK, dlyHcK, megaAmtK, roboFreqK, roboMixK;
    Knob brK, emoK, liftK;                                 // v2.0.0 エモート(息/エモ/サビリフト)
    Knob popK, lipK;                                       // v2.3.0 ポップ/リップ除去
    Knob resK;                                             // v2.4.0 なめらか(動的レゾナンス抑制)
    Knob inGainK;                                          // v2.4.0 マイク音量(入力トリム)
    Knob rideK;                                            // v2.4.0 音量キープ(自動ゲインライド)
    Knob humK;                                             // v2.6.0 ジー音(電源ハムの自動除去)
    Knob consK;                                            // v2.6.0 ことば(子音エンハンサー)
    int  humShownHz = -1;                                  // v2.6.0 ラベルに出している検出値
    Knob vcPitchK, vcFormK, jnMixK;                        // v1.8.0 voice changer / unison
    juce::TextButton vcOnButton, jnOnButton;
    juce::ComboBox   jnHarmBox;                            // ハモリ (ユニゾン/3度/5度/3&5)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> vcOnAttach, jnOnAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> jnHarmAttach;
    // ---- v1.9.0 auto-tune (pitch correction) UI ----
    juce::TextButton atOnButton;
    juce::TextButton jnSoloButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> jnSoloAttach;
    // v2.9.0: 「低遅延」ボタンを廃止し、ヒーロー帯の「セッション」スイッチへ置き換えた。
    juce::TextButton sessionButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sessionAttach;
    juce::Rectangle<int> latBadgeArea;      // 「追加遅延 +0.0 ms」を描く場所
    int  lastShownLatency = -1;             // 変化したときだけ塗り直す
    bool lastSessionState = false;
    void applySessionLock();                // セッション中は該当ツマミを灰色に
    juce::ComboBox   atKeyBox, atScaleBox;
    juce::Slider     atAmountSlider, atSpeedSlider;
    juce::Slider ornSlider;                                // v2.7.0 こぶし
    juce::Rectangle<int> atLabelOrn, ornStatusArea;         // v2.7.0 ラベルと状態表示
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   atOnAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> atKeyAttach, atScaleAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   atAmountAttach, atSpeedAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ornAttach;   // v2.7.0
    juce::ComboBox revTypeBox, dlySyncBox, megaTypeBox, charBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        revTypeAttach, dlySyncAttach, megaTypeAttach;
    juce::TextButton tapButton { "TAP" };
    juce::TextButton midiButton;                           // v2.1.0 MIDIスイッチ設定
    juce::TextButton streamButton;                         // v2.2.0 配信出力(単体起動版のみ)
    double lastTapMs { 0.0 };
    float  tapBpm { 0.0f };
    int    tapCount { 0 };
    juce::String fxInfoText;
    juce::Rectangle<int> fxRow1, fxRow2, fxRow3;
    juce::Rectangle<int> analysisArea;   // v1.4.0 P5: framed key/chord detection box
    // v1.9.0: auto-tune band sub-rects (set in resized(), painted in paint())
    juce::Rectangle<int> atColArea, keyColArea, atLabelAmt, atLabelSpd;
    void fillComboFromChoiceParam (juce::ComboBox&, const juce::String& paramID);

    // ---- v1.4.0: red module lamps at knob centres ----
    struct Lamp
    {
        LampButton btn;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attach;
    };
    Lamp lampGate, lampDn, lampDs, lampDbl, lampDly, lampRev, lampMega, lampCho, lampRobo;
    void addLamp (Lamp&, const juce::String& paramID);
    void placeLamp (Lamp&, const Knob&);

    ClickToEditSlider fontSlider;                          // text size only
    juce::Label  fontSliderLabel;
    ClickToEditSlider zoomSlider;                          // window zoom ratio
    juce::Label  zoomSliderLabel;

public:
    // called by the editor when the window is resized by its corner handle
    void syncZoomDisplay (float s) { zoomSlider.setValue (s, juce::dontSendNotification); }

    // set both slider displays without firing callbacks (startup restore)
    void setUiPrefDisplays (float fontVal, float zoomVal)
    {
        fontSlider.setValue (fontVal, juce::dontSendNotification);
        zoomSlider.setValue (zoomVal, juce::dontSendNotification);
    }

private:

    // Smart Dynamic EQ controls
    juce::TextButton seqOnButton;                          // on/off (panel header)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> seqOnAttach;
    juce::ComboBox seqModeBox;                             // 自動 / 手動
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> seqModeAttach;
    juce::Rectangle<int> seqArea;
    void updateSeqModeVisibility();

    // v2.1.0: A/Bスロットはプロセッサ所有へ移管(MIDIスイッチでエディタ無しでも
    // 切替できるようにするため)。エディタはボタン表示と表示更新だけを持つ。
    int lastAbDirty   { 0 };          // processor.abUiDirty の前回値
    int lastMidiDirty { 0 };          // processor.midiUiDirty の前回値(将来用)
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> cleanArea, dynArea, toneArea, spaceArea;
    juce::Rectangle<int> heroArea;           // v1.5.0 one-press auto-setup band
    juce::Rectangle<int> eqGraphArea;   // EQ graph region inside the seq panel

    // update notice (GitHub Releases check, display only)
    juce::HyperlinkButton updateNotice { juce::String(),
        juce::URL ("https://github.com/gzzio1989/VocalGzzio/releases/latest") };
    void startUpdateCheck();

public:
    void showUpdateNotice (const juce::String& versionTag);

private:
    float fontScale { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalGzzioContent)
};

//==============================================================================
class VocalGzzioEditor : public juce::AudioProcessorEditor
{
public:
    explicit VocalGzzioEditor (VocalGzzioProcessor&);
    ~VocalGzzioEditor() override;

    void resized() override;

private:
    static constexpr int baseW = 1360;  // v1.8.3: +80 横幅拡大
    static constexpr int baseH = 858;   // v1.8.3: +24 (下部46pxは背景が見える帯)
    static constexpr float kFontRebase = 1.5f;   // new 100 percent = old 150 percent

    void applyScale (float s);

    VocalGzzioProcessor& processor;
    GzzioLnF             lnf;
    VocalGzzioContent    content;
    float                uiScale { 1.00f };   // 1280x790 fits 1080p at 100%
    juce::ApplicationProperties appProps;     // remembers font/zoom across sessions

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalGzzioEditor)
};
