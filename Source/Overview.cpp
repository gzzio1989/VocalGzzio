#include "PluginEditor.h"
#include "PresetDefs.h"
#include "Tooltips.h"

namespace
{
std::vector<const char*> overviewModules (int section)
{
    if (section == 0) return { "mod_souji", "mod_totonoe" };
    if (section == 1) return { "mod_soroe", "mod_sagyo", "mod_neiro" };
    return { "mod_hirogari" };
}

// ★ Red Dot & Interactive Soundscape 仕様: クリック・ドラッグ・歌声に呼応して広がる「水紋・流星・蛍火・紅葉＆初雪」波紋エンジン
struct InteractiveRipple
{
    float x { 0.0f }, y { 0.0f };
    juce::uint32 birthMs { 0 };
    int seed { 0 };
    bool active { false };
};
InteractiveRipple g_ripples[16];
int g_rippleWriteIdx = 0;
int g_vistaMood      = 0; // 0=Harmony, 1=Prism, 2=Deep Resonance

void spawnInteractiveRipple (float x, float y, int seedOffset = 0) noexcept
{
    const juce::uint32 now = juce::Time::getMillisecondCounter();
    // ドラッグ中の連続発火を適度な間隔(45ms)に間引いて美しい多重波紋を作る
    const int prevIdx = (g_rippleWriteIdx + 15) % 16;
    if (g_ripples[prevIdx].active && (now - g_ripples[prevIdx].birthMs) < 45
        && std::hypot (x - g_ripples[prevIdx].x, y - g_ripples[prevIdx].y) < 18.0f)
        return;

    auto& rp = g_ripples[g_rippleWriteIdx];
    rp.x       = x;
    rp.y       = y;
    rp.birthMs = now;
    rp.seed    = g_rippleWriteIdx * 37 + seedOffset + (int) (now & 0xff);
    rp.active  = true;
    g_rippleWriteIdx = (g_rippleWriteIdx + 1) % 16;
}
}

void VocalGzzioContent::setOverviewLayoutMode (int mode, bool manual)
{
    overviewLayoutMode   = juce::jlimit (0, 3, mode);
    overviewLayoutManual = manual;
    if (manual && onUiStateChange)
        onUiStateChange ("ui_arch", overviewLayoutMode);
    if (isOverview())
    {
        resizedOverview();
        repaint();
    }
}

void VocalGzzioContent::setLiteMode (bool on)
{
    liteMode = on;
    eqGraph.setLiteMode (liteMode);
    tuner.setLiteMode (liteMode);
    startTimerHz (liteMode ? 10 : 20);
    applyThemeToKnobs();
    if (onUiStateChange) onUiStateChange ("ui_lite", liteMode ? 1 : 0);
    overviewHint = liteMode
        ? language->T ("【軽量モード 入】背景アニメーションとスペクトラム表示を休止し、画面描画の負荷を軽減します。音声処理の設定は変わりません。",
                       "[LITE MODE ON] Pauses background animations and the spectrum display to reduce drawing work. Audio processing settings are unchanged.")
        : language->T ("【軽量モード 切】背景アニメーションとスペクトラム表示を再開します。音声処理の設定は変わりません。",
                       "[LITE MODE OFF] Resumes background animations and the spectrum display. Audio processing settings are unchanged.");
    repaint();
}

void VocalGzzioContent::setAccessMode (int m)
{
    accessMode = juce::jlimit (0, 3, m);
    GzzioLnF::setAccessMode (accessMode);
    Palette::applyTheme (themeMode);
    if (accessMode > 0)
        Palette::applyAccessOverride (accessMode);

    if (auto* g = dynamic_cast<GzzioLnF*> (&getLookAndFeel()))
        g->refreshPaletteColours();
    sendLookAndFeelChange();

    // 老眼・くっきり大文字モード(1)ではフォント倍率を自動で大きく見やすく引き上げる
    if (accessMode == 1)
    {
        fontScale = 1.92f;
        if (auto* g = dynamic_cast<GzzioLnF*> (&getLookAndFeel()))
            g->setFontScale (fontScale);
    }
    else if (accessMode == 2 || accessMode == 3)
    {
        fontScale = 1.65f;
        if (auto* g = dynamic_cast<GzzioLnF*> (&getLookAndFeel()))
            g->setFontScale (fontScale);
    }

    applyThemeToKnobs();
    eqGraph.resized();
    pathRail.refreshColours();
    refreshOverviewText();
    if (isOverview())
    {
        updateOverviewVisibility();
        resizedOverview();
    }
    else
    {
        resized();
    }

    const char* hintsJa[4] = {
        "【標準デザイン】通常のテーマ配色・アニメーション・全コントロールを表示します。",
        "【◉ くっきり・大文字モード】背景透けゼロの超高コントラスト(WCAG AAA 12:1以上)・極太5px針・8分割大目盛り・特大文字・形状記号(▼削る/▲足す/○待機)で小さな文字や目盛りを読みやすくします。",
        "【★ かんたん3ステップ・ガイドモード】使用頻度の低いスイッチや背景の動きを隠して、「STEP 1 ➔ STEP 2 ➔ STEP 3」の順路ガイドと「★まずここ」主役ハイライト・待機ノブ減光で迷わず操作できます。",
        "【● マット・具体表記モード】ネオン発光・眩しい反射・グラデーションをすべて抑えたウォームスレート＆セージのマット配色と、水平グリッド整列・具体的で分かりやすい機能表記に切り替えます。"
    };
    const char* hintsEn[4] = {
        "[STANDARD] Full theme visuals and controls.",
        "[HIGH CONTRAST & LARGE TEXT] WCAG AAA 12:1 contrast, bold 5px needle, 8 large ticks, enlarged text, and shape indicators (▼/▲/○).",
        "[3-STEP QUICK GUIDE] Streamlined 3-step guided workflow, hero knob callouts, and dimmed standby controls.",
        "[MATTE & CONCRETE LABELS] Low-glare warm matte sage palette, flat horizontal grid, and concrete functional labels."
    };
    overviewHint = language->T (hintsJa[accessMode], hintsEn[accessMode]);
    repaint();
}

void VocalGzzioContent::syncArgonOrbFromParams() noexcept
{
    const float v = processor.apvts.getRawParameterValue ("argon_velvet")->load();
    const float s = processor.apvts.getRawParameterValue ("argon_silk")->load();
    const float h = processor.apvts.getRawParameterValue ("argon_halo")->load();
    if (v > 0.5f || s > 0.5f || h > 0.5f)
    {
        argonOrbX = juce::jlimit (0.08f, 0.92f, 0.50f + (s - v) * 0.0055f);
        argonOrbY = juce::jlimit (0.08f, 0.92f, h / 92.0f);
    }
    else
    {
        const float pres = processor.apvts.getRawParameterValue ("presence")->load();
        const float drv  = processor.apvts.getRawParameterValue ("drive")->load();
        const float rev  = processor.apvts.getRawParameterValue ("revmix")->load();
        argonOrbX = juce::jlimit (0.12f, 0.88f, 0.50f + pres * 0.06f - drv * 0.005f);
        argonOrbY = juce::jlimit (0.12f, 0.88f, 0.16f + rev * 0.022f);
    }
}

void VocalGzzioContent::applyArgonOrbFromXY (float nx, float ny)
{
    argonOrbX = juce::jlimit (0.0f, 1.0f, nx);
    argonOrbY = juce::jlimit (0.0f, 1.0f, ny);
    argonActiveTarget = -1;

    // X軸: 左(0.0)=Warm Velvet(母音2次倍音・胸声アンカー) ⇄ 中央=Focus(2.8kHz声芯) ⇄ 右(1.0)=Silk Air(14kHz透明感・子音保護)
    // Y軸: 上(0.0)=Up-Front ASMR(超近接・耳元密度)        ⇄ 下(1.0)=3D Stage Halo(バイノーラル立体空間)
    const float velvet = juce::jlimit (0.0f, 100.0f, (1.0f - argonOrbX * 0.52f) * 78.0f + (1.0f - argonOrbY) * 22.0f);
    const float focus  = juce::jlimit (0.0f, 100.0f, (1.0f - std::abs (argonOrbX - 0.54f) * 1.10f) * 66.0f + (1.0f - argonOrbY * 0.58f) * 32.0f);
    const float silk   = juce::jlimit (0.0f, 100.0f, (0.22f + argonOrbX * 0.78f) * 88.0f);
    const float halo   = juce::jlimit (0.0f, 100.0f, argonOrbY * 92.0f);

    auto setParam = [this] (const char* id, float val)
    {
        if (auto* p = processor.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (processor.apvts.getParameterRange (id).convertTo0to1 (val));
            p->endChangeGesture();
        }
    };
    setParam ("argon_velvet", velvet);
    setParam ("argon_focus",  focus);
    setParam ("argon_silk",   silk);
    setParam ("argon_halo",   halo);

    overviewHint = language->T (
        "左で温かく、右で明るく、下で広がる声へ。中央の4つの効果をまとめて調整します。ダブルクリックで4つとも0%に戻します。リバーブや既存の音色つまみは変えません。",
        "Drag left for warmth, right for air, and down for stereo space. Adjusts the four Argon effects only; double-click resets all four to 0%. Reverb and other tone controls stay unchanged.");
    repaint();
}

void VocalGzzioContent::applyArgonVoiceTarget (int idx)
{
    idx = juce::jlimit (0, 3, idx);
    struct Target
    {
        float ox, oy;
        float velvet, focus, silk, halo;
        float comp2, pres, drive, revmix, revsize;
        const char* hintJa;
        const char* hintEn;
    };
    static const Target kTargets[4] = {
        // 0: ★ VELVET 歌枠 (王道ベルベットボーカル)
        { 0.44f, 0.42f, 64.0f, 58.0f, 58.0f, 46.0f, 45.0f, 2.2f, 24.0f, 16.0f, 38.0f,
          "【歌枠】声の厚みと芯を足す設定です。明るさ・圧縮・リバーブも合わせて変わります。原音と比べ、濁りやサ行が目立つ場合は量を下げてください。",
          "[VELVET VOCAL] Adds vocal warmth and focus with coordinated tone, compression and reverb. Compare with the original and reduce the effects if mud or sibilance stands out." },
        // 1: ♥ ASMR ささやき (超近接・耳元ブレス)
        { 0.28f, 0.14f, 76.0f, 44.0f, 72.0f, 18.0f, 58.0f, 1.4f, 28.0f, 5.0f, 20.0f,
          "【ささやき】残響を抑え、声の厚みと息の明るさを足す設定です。背景ノイズが目立つ場合は空気感を下げ、ノイズ除去は別に調整してください。",
          "[WHISPER ASMR] Uses less reverb with added body and breath detail. Reduce air if background hiss becomes prominent; noise reduction remains a separate adjustment." },
        // 2: ✦ CRYSTAL ポップス (高抜け・ボカロ/POPS)
        { 0.80f, 0.34f, 38.0f, 78.0f, 86.0f, 42.0f, 52.0f, 3.6f, 14.0f, 14.0f, 34.0f,
          "【ポップス】声の芯と高音の明るさを強め、伴奏から聞き取りやすくする設定です。硬く感じたら声芯や空気感を下げてください。",
          "[CRYSTAL POP] Emphasizes vocal focus and high-frequency air to stand out in accompaniment. Reduce Focus or Air if the voice sounds hard." },
        // 3: ◆ HALO バラード (3D立体ハロー空間)
        { 0.54f, 0.84f, 60.0f, 52.0f, 66.0f, 86.0f, 42.0f, 2.0f, 22.0f, 26.0f, 56.0f,
          "【バラード】左右の広がりとリバーブを多めにした設定です。ステレオで聴き比べ、言葉がぼやける場合は立体や残響を下げてください。",
          "[HALO BALLAD] Adds stereo width and more reverb. Compare in stereo and reduce Halo or reverb if words become unclear." }
    };

    const auto& t = kTargets[idx];
    argonOrbX = t.ox;
    argonOrbY = t.oy;
    argonActiveTarget = idx;

    auto setParam = [this] (const char* id, float val)
    {
        if (auto* p = processor.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (processor.apvts.getParameterRange (id).convertTo0to1 (val));
            p->endChangeGesture();
        }
    };
    setParam ("argon_velvet", t.velvet);
    setParam ("argon_focus",  t.focus);
    setParam ("argon_silk",   t.silk);
    setParam ("argon_halo",   t.halo);
    setParam ("comp2",        t.comp2);
    setParam ("presence",     t.pres);
    setParam ("drive",        t.drive);
    setParam ("revmix",       t.revmix);
    setParam ("revsize",      t.revsize);
    overviewHint = language->T (t.hintJa, t.hintEn);
    repaint();
}

bool VocalGzzioContent::handleOverviewMouseDoubleClick (juce::Point<int> pt)
{
    if (! argonPadRect.isEmpty() && argonPadRect.contains (pt))
    {
        argonOrbX = 0.50f;
        argonOrbY = 0.50f;
        argonActiveTarget = -1;
        argonDragging = -1;
        for (const char* id : { "argon_velvet", "argon_focus", "argon_silk", "argon_halo" })
        {
            if (auto* p = processor.apvts.getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (0.0f);
                p->endChangeGesture();
            }
        }
        overviewHint = language->T (
            "【Argon 2D 重力場オーブ リセット】倍音(VELVET)・声芯(FOCUS)・空気感(SILK)・立体ハロー(HALO)をニュートラル(0%)へリセットしました。",
            "[Argon 2D Orb Reset] Velvet, Focus, Silk Air, and 3D Halo reset to neutral (0%).");
        repaint();
        return true;
    }
    for (int i = 0; i < 4; ++i)
    {
        if (! argonBarRects[(size_t) i].isEmpty() && argonBarRects[(size_t) i].expanded (0, 2).contains (pt))
        {
            const char* ids[4] = { "argon_velvet", "argon_focus", "argon_silk", "argon_halo" };
            if (auto* p = processor.apvts.getParameter (ids[i]))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (0.0f);
                p->endChangeGesture();
            }
            argonActiveTarget = -1;
            argonDragging = -1;
            repaint();
            return true;
        }
    }
    return false;
}

bool VocalGzzioContent::handleOverviewMouseDown (juce::Point<int> pt)
{
    // 0. ユニバーサル・アクセシビリティモード切替ピル (標準 / 👓老眼・大文字 / 🧭ADHD 3ステップ / 🌿ASD 低刺激)
    for (int a = 0; a < 4; ++a)
    {
        if (! accessPillRects[(size_t) a].isEmpty() && accessPillRects[(size_t) a].contains (pt))
        {
            setAccessMode (a);
            return true;
        }
    }

    // 1. ヘッダの4種ワークスペース配置切替ピル (★ARGON / ♥LIVER / 🎛STUDIO / ◎CLASSIC)
    for (int i = 0; i < 4; ++i)
    {
        if (archPillRects[(size_t) i].contains (pt))
        {
            const bool wasLiverArch = (overviewLayoutMode == 1);
            setOverviewLayoutMode (i, true);
            if (i == 1 && wasLiverArch && liverTheme())
            {
                setThemeMode (Palette::themeOfCouture ((Palette::coutureIndex (themeMode) + 1) % 5));
            }
            else if (i == 0 && ! wasLiverArch && overviewLayoutMode == 0 && oceanStarTheme())
            {
                setThemeMode (Palette::themeOfOceanStar ((Palette::oceanStarIndex (themeMode) + 1) % 5));
            }
            else if (isShowing())
            {
                if (i == 1 && ! (liverTheme() || themeMode == 1 || themeMode == 8))
                    setThemeMode (lastCoutureTheme);
                else if (i == 2 && themeMode != 9)
                    setThemeMode (9);
                else if (i == 0 && (liverTheme() || themeMode == 9))
                    setThemeMode (lastOceanStarTheme);
            }
            const char* archHintsJa[4] = {
                "【★ ARGON DECK】中央に3D回転トラッカーボールと4軸トリガーを据えたコックピット配置です（中央右上の5色ピルで星空・海・黄昏テーマをワンタッチ切替できます）。",
                "【♥ LIVER STAGE】クチュール・クッションパクト球体＆12連コスメパレットを並べた、Vライバー・歌枠ステージ配置です（もう一度押すか中央右上の5色ピルでコスメ配色をワンタッチ切替できます）。",
                "【🎛 STUDIO RACK】左から右へ信号経路が並ぶ、プロ音楽家向け5列ヴィンテージ・アナログコンソール配置です。",
                "【◎ CLASSIC】上部に大型チューナー、下部に3枚のセクションカードを並べた従来の3カード配置です。"
            };
            const char* archHintsEn[4] = {
                "[ARGON DECK] Next-gen 3-wing cockpit centered around the 3D Orb and 4-axis trigger engine (switch Starry Sky & Sea themes with the 5 pills).",
                "[LIVER STAGE] Couture Cosmetics stage panorama with Cushion Compact sphere, 12-dial ribbon, and 5 one-click colorways.",
                "[STUDIO RACK] 5-column vertical hardware channel strip following strict left-to-right console signal flow.",
                "[CLASSIC] Traditional 3-card overview layout."
            };
            overviewHint = language->T (archHintsJa[i], archHintsEn[i]);
            return true;
        }
    }

    // 1a. 💄 クチュール・コスメ 5色 ／ 🌌 星空・海・黄昏コレクション 5色 ワンタッチ切替ピル
    for (int c = 0; c < 5; ++c)
    {
        if (! coutureSwatchRects[(size_t) c].isEmpty() && coutureSwatchRects[(size_t) c].contains (pt))
        {
            const bool isCoutureSw = liverTheme() || (overviewLayoutMode == 1 && ! oceanStarTheme() && themeMode != 2 && themeMode != 4);
            if (isCoutureSw)
            {
                setThemeMode (Palette::themeOfCouture (c));
                static const char* const kSwatchHintJa[5] = {
                    "【01 ロゼ・クチュール (Rose Gold & Ruby)】ポーセリンファンデーション下地にローズゴールド金具と深紅ルビー鏡面をあしらった王道デパコス配色です。",
                    "【02 アイシーラベンダー＆プラチナシルバー (Glacier Lilac)】透明感ラベンダー下地にプラチナシルバー金具とサファイア鏡面をあしらったブルベ透明感配色です。",
                    "【03 ノワール・ロリータ＆ベルベットゴシック (Noir Ruban)】漆黒ベルベット＆ダークプラム下地にアンティーク銀金具とガーネット鏡面が映える小悪魔ゴシック配色です。",
                    "【04 ティファニーミント＆パールホワイト (Celadon Bijou)】セラドンミント＆パール下地にシャンパンゴールド金具とエメラルド鏡面をあしらったジュエリー配色です。",
                    "【05 多幸感ハニーピーチ＆アプリコット・グロウ (Apricot Miel)】ウォームバニラ＆白桃コーラル下地に24Kハニーゴールド金具をあしらった多幸感イエベ春コスメ配色です。"
                };
                static const char* const kSwatchHintEn[5] = {
                    "[01 ROSE COUTURE] Porcelain cushion foundation with Champagne Rose-Gold hardware and Ruby Rose-Quartz mirror.",
                    "[02 GLACIER LILAC] Tone-up lavender foundation with Platinum Sterling Silver hardware and Starlight Sapphire mirror.",
                    "[03 NOIR RUBAN] Dark velvet lace and plum case with Antique Silver hardware and Obsidian Garnet mirror.",
                    "[04 CELADON BIJOU] Pearl-ivory and celadon mint powder with Warm Champagne-Gold hardware and Emerald mirror.",
                    "[05 APRICOT MIEL] Warm vanilla and honey-peach blush with 24K Honey-Gold hardware and Amber-Peach mirror."
                };
                overviewHint = language->T (kSwatchHintJa[c], kSwatchHintEn[c]);
            }
            else
            {
                setThemeMode (Palette::themeOfOceanStar (c));
                static const char* const kOceanHintJa[5] = {
                    "【🌌 01 星降る蛍火の夜 (Starlit Firefly Nocturne)】深いミッドナイト・サファイア夜空に、黄金アンバーの蛍火ボケとバイオルミネセンス水色＆ローズ星屑が浮かぶ幻想夜景テーマです。",
                    "【🌊 02 蒼海オペラ座・深海ベルベット (Grand Opera Sapphire)】ロイヤルコバルトのベルベット幕と深海サファイア照明に、アンティーク真鍮シャンデリアの黄金が輝く重厚ステージテーマです。",
                    "【🌅 03 夕凪の海と黄昏の水鏡 (Twilight Sea & Sunset Mirror)】黄昏のティール・インディゴ空から茜色アプリコットの水平線、さざ波が揺れる水鏡リフレクションを描いたマジックアワーテーマです。",
                    "【🐬 04 珊瑚礁の碧海・ルミナスラグーン (Luminous Coral Lagoon)】陽光の差し込む透明なアクアマリン＆ターコイズの海に、珊瑚ピンクと真珠ゴールドがきらめく海中ラグーンテーマです。",
                    "【🌠 05 天の川と極光プラネタリウム (Celestial Milky Way & Aurora)】深宇宙バイオレットの夜空を横切る天の川星雲と、黄金のアストロラーベ星座線・極光エメラルドが輝く天文台テーマです。"
                };
                static const char* const kOceanHintEn[5] = {
                    "[01 STARLIT FIREFLY NOCTURNE] Deep midnight sapphire night sky with glowing golden-amber firefly bokeh and bioluminescent cyan stardust.",
                    "[02 GRAND OPERA SAPPHIRE] Royal cobalt velvet stage drapery, electric abyssal blue spotlights, and antique chandelier gold hardware.",
                    "[03 TWILIGHT SEA & SUNSET MIRROR] Twilight teal-indigo sky melting into a golden apricot sunset horizon and shimmering water mirror ripples.",
                    "[04 LUMINOUS CORAL LAGOON] Sunlit aquamarine and turquoise reef caustics with coral-pink and pearl-shell gold highlights.",
                    "[05 CELESTIAL MILKY WAY] Cosmic violet-navy sky with Milky Way nebula clouds, golden astrolabe constellations, and emerald aurora."
                };
                overviewHint = language->T (kOceanHintJa[c], kOceanHintEn[c]);
            }
            return true;
        }
    }

    // 1b. ヘッダの「⚡軽量」ワンクリック省負荷ピル
    if (! litePillRect.isEmpty() && litePillRect.contains (pt))
    {
        setLiteMode (! liteMode);
        return true;
    }

    // 1c. ヘッダの「🎯入出力・モノラル」ルーティング設定ピル ＆ 計器カード内の「🎤入力:モノ/ステレオ」直結ピル
    if ((! routePillRect.isEmpty() && routePillRect.contains (pt))
        || (! ioInMonoRect.isEmpty() && ioInMonoRect.contains (pt)))
    {
        const auto anchor = (! ioInMonoRect.isEmpty() && ioInMonoRect.contains (pt)) ? ioInMonoRect : routePillRect;
        openRoutePopup (localAreaToGlobal (anchor));
        overviewHint = language->T ("【🎯 入力・出力 モノラル／ステレオ設定】マイク入力（自動モノラル中央／入力1モノラル／入力2モノラル／ステレオ）と、出力（モノラル／ステレオ）を切り替えます。",
                                    "[INPUT / OUTPUT MONO & STEREO] Switch mic input (Auto Mono, Input 1 Mono, Input 2 Mono, Stereo) and output (Mono / Stereo).");
        return true;
    }

    // 1c-2. 計器＆入出力カード内の「🔊出力: モノラル / ステレオ」ワンクリック切替ピル
    if (! ioOutMonoRect.isEmpty() && ioOutMonoRect.contains (pt))
    {
        const bool nextMono = ! processor.getOutputMonoMode();
        processor.setOutputMonoMode (nextMono);
        overviewHint = nextMono
            ? language->T ("【🔊 出力: モノラル ON】左右のスピーカー／ヘッドホンおよび配信出力へ完全同一のモノラル音声（L=R）を出力します。もう一度押すとステレオ出力に戻ります。",
                           "[OUTPUT: MONO ON] Outputs identical dual-mono audio (L=R) to speakers/headphones and stream output. Click again for Stereo.")
            : language->T ("【🔊 出力: ステレオ ON】通常の立体ステレオ出力に戻しました。もう一度押すとモノラル出力に切り替わります。",
                           "[OUTPUT: STEREO ON] Restored normal stereo output. Click again to switch to Mono output.");
        repaint();
        return true;
    }

    // 1d. ヘッダの「📡外部出力 (VocalGzzio Driver (WDM) / SYNCROOM / VB-CABLE)」ブリッジ設定ピル
    if (! streamPillRect.isEmpty() && streamPillRect.contains (pt))
    {
        openStreamOutPopup (localAreaToGlobal (streamPillRect));
        overviewHint = language->T ("【📡 外部出力 (VocalGzzio Driver (WDM))】ASIOで自分の声を聴きながら、専用仮想ドライバ「ライン (VocalGzzio Driver (WDM))」へ処理後の声を同時出力します。",
                                    "[WDM / SYNC OUT] Bridges processed audio directly into 'ライン (VocalGzzio Driver (WDM))' while keeping your main interface on ASIO.");
        return true;
    }

    // 1d-2. 2段目の「🎤歌枠 ⇄ 💬雑談MC」ワンクリック瞬間ライブ切替ピル
    if (! talkPillRect.isEmpty() && talkPillRect.contains (pt))
    {
        processor.toggleLiveTalkMode();
        const bool isMc = processor.getLiveTalkMode();
        overviewHint = isMc
            ? language->T ("【💬 雑談MCモード 入】歌枠の設定を1ミリも変えずに、残響・やまびこ・ピッチ補正・ハモリをスッと消して聞き取りやすいMCトーク声へ切り替えました。もう一度押すと一瞬で歌枠へ戻ります。",
                           "[MC TALK MODE ON] Smoothly mutes reverb, echo, pitch correction, and harmony for clear speech without altering your singing settings. Click again to return to singing.")
            : language->T ("【🎤 歌枠モード 復帰】雑談MCを解除し、設定してあった残響・やまびこ・ピッチ補正・ハモリをそのまま復元しました。",
                           "[SING MODE RESTORED] Exited MC Talk mode and restored your reverb, echo, pitch correction, and harmony settings.");
        repaint();
        return true;
    }

    // 1d-3. 2段目の「🔄5秒試聴ルーパー」録音・手放しループ再生ピル
    if (! loopPillRect.isEmpty() && loopPillRect.contains (pt))
    {
        processor.toggleAudition();
        const int st = processor.getAuditionState();
        overviewHint = (st == VocalGzzioProcessor::AuditionRecording)
            ? language->T ("【● 5秒試聴録音中】マイクに向かって5秒ほど歌うか話してください。5秒経つと自動でループ再生に切り替わり、手放しで音作りや原音比較ができます（途中で押すとそこまでを即ループ再生します）。",
                           "[RECORDING 5s PHRASE] Sing or speak for up to 5 seconds. Loops automatically so you can tweak the orb or compare hands-free (click again to loop early).")
            : (st == VocalGzzioProcessor::AuditionPlaying)
                ? language->T ("【▶ 手放し試聴ループ再生中】録音したあなたの声を繰り返し流しています。声を出さずに2Dオーブや仕上がり・配色を切り替えたり、「押して原音と比較」で聴き比べられます（もう一度押すと停止）。",
                               "[LOOP AUDITION PLAYING] Looping your recorded voice through the live DSP chain. Adjust the 2D orb, finishes, or hold Compare hands-free (click again to stop).")
                : language->T ("【🔄 5秒試聴ルーパー 停止】試聴ループ再生を停止し、通常の生マイク入力へ戻しました。",
                               "[AUDITION LOOPER STOPPED] Stopped loop playback and returned to live microphone input.");
        repaint();
        return true;
    }

    // 1d-4. 2段目の「😷咳ミュート」ワンクリック瞬間無音ミュートピル（8ms滑らか遮断・残響鳴り残り防止）
    if (! coughPillRect.isEmpty() && coughPillRect.contains (pt))
    {
        processor.toggleCoughMute();
        const bool muted = processor.getCoughMute();
        overviewHint = muted
            ? language->T ("【🔇 咳払い瞬間ミュート中】マイク入力をリバーブ・ディレイの手前で8ms無音遮断しました（ポップノイズも残響の鳴り残りもゼロ）。もう一度押すと即座に復帰します。",
                           "[COUGH MUTE ON] Smoothly muted mic input in 8ms before reverb/delay (zero pop noise or reverb tail). Click again to unmute.")
            : language->T ("【😷 咳払いミュート解除】マイク音声を滑らかに復帰しました。",
                           "[COUGH MUTE OFF] Restored microphone audio.");
        repaint();
        return true;
    }

    // 1d-5. 2段目の「🎵BGM・録音・診断」ステージ＆スタジオ統合パネルピル
    if (! stagePillRect.isEmpty() && stagePillRect.contains (pt))
    {
        openStageStudioPopup (localAreaToGlobal (stagePillRect));
        overviewHint = language->T ("【🎵 BGM・録音・声質診断・絶叫ガード】カラオケ伴奏プレイヤー（キー変更・自動ダッキング・WDM混音選択）、1クリックWAV録音＆直前30秒さかのぼり保存、5秒声質ターゲット診断、絶叫セーフティガードを開きます。",
                                    "[STAGE & STUDIO HUB] Opens Karaoke/BGM player (key shift, auto-ducking, WDM mix), 1-click WAV recorder & 30s retroactive save, 5s Voice Target AI tuner, and Scream Safety Guard.");
        return true;
    }

    // 1e. Output boost trim. The visible button and this hit-test use the same action.
    if (! levelBadgeRect.isEmpty() && levelBadgeRect.contains (pt))
    {
        trimOverviewOutputLevel();
        return true;
    }

    // 1f. 03 SPACE カード内の「🌊 IR波形・外部IR読込・声優先ダッキング」カプセルクリック
    if (! irOverviewRect.isEmpty() && irOverviewRect.contains (pt))
    {
        openIrSelectorMenu (localAreaToGlobal (irOverviewRect));
        overviewHint = language->T ("【🌊 IR残響・外部IR読込・声優先ダッキング】内蔵6部屋のIR切替、外部IRファイル(.wav/.aiff)の読込、および「声優先ダッキング（歌っている最中は残響を抑え語尾で広げる）」を設定します。",
                                    "[IR CONVOLUTION & DUCKING] Switch between the 6 built-in IR spaces, load a custom IR (.wav/.aiff), or set Vocal-Priority Space Ducking.");
        return true;
    }

    // 2. Argon Voice DNA ワンタッチターゲット (4種)
    for (int i = 0; i < 4; ++i)
    {
        if (! argonTargetRects[(size_t) i].isEmpty() && argonTargetRects[(size_t) i].contains (pt))
        {
            applyArgonVoiceTarget (i);
            return true;
        }
    }

    // 3. 2D Voice Gravity Orb フィールドのドラッグ開始
    if (! argonPadRect.isEmpty() && argonPadRect.contains (pt))
    {
        argonDragging = 0;
        spawnInteractiveRipple ((float) pt.x, (float) pt.y, 11);
        handleOverviewMouseDrag (pt);
        return true;
    }

    // 4. Argon Velvet Engine 4本のダイレクトバー (VELVET / FOCUS / SILK / HALO)
    for (int i = 0; i < 4; ++i)
    {
        if (! argonBarRects[(size_t) i].isEmpty() && argonBarRects[(size_t) i].expanded (0, 2).contains (pt))
        {
            argonDragging = i + 1;
            spawnInteractiveRipple ((float) pt.x, (float) pt.y, 23 + i * 7);
            handleOverviewMouseDrag (pt);
            return true;
        }
    }

    // 5. ★ 風景・季節パノラマウィンドウ（左右の中段ビスタ窓）クリックでインタラクティブ水紋・流星・紅葉＆初雪バーストを発火！
    if (oceanStarTheme() && overviewLayoutMode == 0 && ! liteMode)
    {
        const juce::Rectangle<int> leftVista  (tuner.getX(),          tuner.getBottom() + 5,
                                               tuner.getWidth(),      overviewCards[0].getY() - tuner.getBottom() - 10);
        const juce::Rectangle<int> rightVista (overviewOutput.getX(), overviewOutput.getBottom() + 5,
                                               overviewOutput.getWidth(), overviewCards[2].getY() - overviewOutput.getBottom() - 10);
        if (leftVista.contains (pt) || rightVista.contains (pt))
        {
            spawnInteractiveRipple ((float) pt.x, (float) pt.y, 53);
            g_vistaMood = (g_vistaMood + 1) % 3;
            static const char* const kMoodJa[3] = {
                "【✦ インタラクティブ風景窓：HARMONY AURA】クリックした位置から水紋・流星・蛍火・紅葉と初雪が広がり、あなたの声の倍音と共鳴します。",
                "【✦ インタラクティブ風景窓：PRISM CAUSTICS】水面コースティクスと天球のプリズム反射を強調しました。もう一度クリックで表現モードを切り替えられます。",
                "【✦ インタラクティブ風景窓：DEEP RESONANCE】水鏡の32バンド・シンメトリック倍音ファウンテンと残響波紋をフル展開しました。"
            };
            static const char* const kMoodEn[3] = {
                "[INTERACTIVE SOUNDSCAPE: HARMONY AURA] Ripples, shooting stars, fireflies, or autumn leaves & snow bloom from your touch and resonate with your voice.",
                "[INTERACTIVE SOUNDSCAPE: PRISM CAUSTICS] Enhanced water caustics and celestial prism reflections. Click again to cycle visual resonance modes.",
                "[INTERACTIVE SOUNDSCAPE: DEEP RESONANCE] Full 32-band symmetrical water-mirror harmonic fountain and deep spatial ripples."
            };
            overviewHint = language->T (kMoodJa[g_vistaMood], kMoodEn[g_vistaMood]);
            repaint();
            return true;
        }
    }

    return false;
}

void VocalGzzioContent::handleOverviewMouseDrag (juce::Point<int> pt)
{
    if (argonDragging == 0 && ! argonPadRect.isEmpty())
    {
        spawnInteractiveRipple ((float) pt.x, (float) pt.y, 17);
        const float nx = (float) (pt.x - argonPadRect.getX()) / (float) juce::jmax (1, argonPadRect.getWidth());
        const float ny = (float) (pt.y - argonPadRect.getY()) / (float) juce::jmax (1, argonPadRect.getHeight());
        applyArgonOrbFromXY (nx, ny);
    }
    else if (argonDragging >= 1 && argonDragging <= 4)
    {
        const int idx = argonDragging - 1;
        const auto& r = argonBarRects[(size_t) idx];
        if (! r.isEmpty())
        {
            spawnInteractiveRipple ((float) pt.x, (float) r.getCentreY(), 31 + idx);
            const float norm = juce::jlimit (0.0f, 1.0f, (float) (pt.x - r.getX()) / (float) juce::jmax (1, r.getWidth()));
            const char* ids[4] = { "argon_velvet", "argon_focus", "argon_silk", "argon_halo" };
            if (auto* p = processor.apvts.getParameter (ids[idx]))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (norm);
                p->endChangeGesture();
            }
            argonActiveTarget = -1;
            repaint();
        }
    }
}

// 総合画面は同じパラメータ部品を使う。表示の切り替えで音を変えない。
void VocalGzzioContent::initialiseOverview()
{
    srcModeBox.setComponentID ("sourceMode");
    learnButton.setComponentID ("learnNoise");
    relearnBtn.setComponentID ("autoLearnNoise");
    autoSetupButton.setComponentID ("autoSetup");
    songSetupButton.setComponentID ("songSetup");
    resetButton.setComponentID ("resetAll");
    abA.setComponentID ("abSlotA");
    abB.setComponentID ("abSlotB");
    abCopy.setComponentID ("abCopy");
    crushBtn.setComponentID ("crushGuard");
    seqOnButton.setComponentID ("smartEqPower");
    atOnButton.setComponentID ("pitchFixPower");
    jnOnButton.setComponentID ("unisonPower");
    vcOnButton.setComponentID ("voiceFxPower");
    revTypeBox.setComponentID ("reverbType");
    tempoFitButton.setComponentID ("tempoFit");
    sessionButton.setComponentID ("sessionMode");
    checkupButton.setComponentID ("openCheckup");
    overviewNoiseStatus.setComponentID ("noiseStatus");
    overviewNoiseStatus.getProperties().set ("fontH", 13.0);
    overviewNoiseStatus.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (overviewNoiseStatus);
    dnClearButton.setComponentID ("clearNoise");
    finishPresetBox.setComponentID ("finishPreset");
    finishPresetBox.setTooltip (language->T ("目的から仕上がりを選びます。音色・音量の整え方・残響が一緒に変わります。入力音量と出力音量、ノイズ学習、音程の設定は保持します。", "Choose a complete tone, dynamics and ambience recipe. Input/output gain, noise learning and pitch settings are preserved."));
    addChildComponent (finishPresetBox);
    finishPresetBox.onChange = [this]
    {
        const int index = finishPresetBox.getSelectedId() - 1;
        if (index < 0 || index >= gzzio::kNumFinishPresets) return;
        gzzio::applyFinishPreset (index, [this] (const char* id, float value)
        {
            if (auto* p = processor.apvts.getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (processor.apvts.getParameterRange (id).convertTo0to1 (value));
                p->endChangeGesture();
            }
        });
        finishPresetIndex = index;
        processor.apvts.state.setProperty ("finish_preset", index, nullptr);
        syncArgonOrbFromParams();
        overviewHint = juce::String::fromUTF8 (language->english ? gzzio::kFinishPresets[index].descEn : gzzio::kFinishPresets[index].desc);
        repaint();
    };
    finishPresetIndex = (int) processor.apvts.state.getProperty ("finish_preset", -1);
    for (auto* button : { &overviewDetails, &overviewReturn, &overviewPitch, &reverbPower, &overviewOutputTrim })
        styleButton (*button);
    overviewDetails.setComponentID ("openDetails");
    overviewReturn.setComponentID ("openOverview");
    overviewPitch.setComponentID ("openPitch");
    reverbPower.setComponentID ("reverbPower");
    overviewOutputTrim.setComponentID ("trimOutputBoost");
    overviewOutputTrim.getProperties().set ("fontH", 14.0);
    overviewOutputTrim.onClick = [this] { trimOverviewOutputLevel(); };
    reverbPower.setClickingTogglesState (true);
    reverbPowerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
        (processor.apvts, "revon", reverbPower);
    overviewDetails.onClick = [this] { setOverview (false); };
    overviewReturn.onClick = [this] { setOverview (true); };
    overviewPitch.onClick = [this]
    {
        setOverview (false);
        setFocusedEditing (true);
        applyFocusSelection (gz::ModuleChain::Henshin);
    };
    for (int i = 0; i < 3; ++i)
    {
        auto& power = overviewSectionPower[(size_t) i];
        styleButton (power);
        power.setClickingTogglesState (true);
        power.setComponentID ("overviewPower" + juce::String (i));
        power.onClick = [this, i]
        {
            const bool enabled = overviewSectionPower[(size_t) i].getToggleState();
            for (const char* id : overviewModules (i))
                if (auto* p = processor.apvts.getParameter (id))
                { p->beginChangeGesture(); p->setValueNotifyingHost (enabled ? 1.0f : 0.0f); p->endChangeGesture(); }
            updateOverviewState();
        };
        styleButton (overviewMore[(size_t) i]);
        overviewMore[(size_t) i].setComponentID ("overviewMore" + juce::String (i));
        overviewMore[(size_t) i].onClick = [this, i]
        {
            setOverview (false);
            setFocusedEditing (true);
            applyFocusSelection (i == 0 ? gz::ModuleChain::Souji : i == 1 ? gz::ModuleChain::Neiro : gz::ModuleChain::Hirogari);
        };
    }
    for (auto* hk : { &denoiseK, &comp2K, &presenceK, &revMixK, &makeupK })
        hk->slider.getProperties().set ("heroKnob", true);
    addChildComponent (overviewCompare);
    overviewCompare.setComponentID ("compareOriginal");
    overviewCompare.onDown = [this] { processor.compareBypass.store (true); repaint(); };
    overviewCompare.onUp = [this] { processor.compareBypass.store (false); repaint(); };
    syncArgonOrbFromParams();
    refreshOverviewText();
}

void VocalGzzioContent::setOverview (bool on)
{
    restoreOverviewVisibility();
    overviewEnabled = on;
    if (on) { advancedMode = true; focusMode = false; }
    if (onUiStateChange) onUiStateChange ("ui_overview", on ? 1 : 0);
    applyModeVisibility();
    refreshOverviewText();
}

void VocalGzzioContent::restoreOverviewVisibility()
{
    for (const auto& item : overviewOldVisibility)
        if (auto* component = item.first.getComponent()) component->setVisible (item.second);
    overviewOldVisibility.clear();
    for (auto* k : { &inGainK, &makeupK, &denoiseK, &gate, &lowCut, &mudK,
                     &comp2K, &deessK, &presenceK, &airK, &warmthK, &resK,
                     &revMixK, &revSizeK, &delayK, &widthK })
    {
        k->slider.setEnabled (true);
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 20);
        k->label.setAlpha (1.0f);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &finishPresetBox, &overviewDetails, &overviewCompare,
                     &overviewNoiseStatus,
                     &overviewPitch, &overviewOutputTrim, &overviewMore[0], &overviewMore[1], &overviewMore[2],
                     &overviewSectionPower[0], &overviewSectionPower[1], &overviewSectionPower[2] })
        c->setVisible (false);
}

void VocalGzzioContent::updateOverviewVisibility()
{
    if (! isOverview()) return;
    if (overviewOldVisibility.empty())
        for (auto* c : getChildren())
            if (dynamic_cast<juce::CallOutBox*> (c) == nullptr)
                overviewOldVisibility.emplace_back (c, c->isVisible());
    for (auto* c : getChildren())
        if (dynamic_cast<juce::CallOutBox*> (c) == nullptr) c->setVisible (false);
    for (auto* c : { (juce::Component*) &finishPresetBox, (juce::Component*) &srcModeBox,
         (juce::Component*) &overviewDetails, (juce::Component*) &overviewCompare,
         (juce::Component*) &overviewPitch, (juce::Component*) &overviewOutputTrim, (juce::Component*) &sizePopBtn,
         (juce::Component*) &checkupButton,
         (juce::Component*) &autoSetupButton, (juce::Component*) &songSetupButton,
         (juce::Component*) &resetButton,
         (juce::Component*) &crushBtn, (juce::Component*) &seqOnButton,
         (juce::Component*) &atOnButton, (juce::Component*) &jnOnButton, (juce::Component*) &vcOnButton,
         (juce::Component*) &tempoFitButton, (juce::Component*) &sessionButton,
         (juce::Component*) &tuningPopBtn, (juce::Component*) &saveButton,
         (juce::Component*) &loadButton, (juce::Component*) &abA, (juce::Component*) &abB,
         (juce::Component*) &abCopy, (juce::Component*) &tuner, (juce::Component*) &eqGraph,
         (juce::Component*) &learnButton, (juce::Component*) &relearnBtn,
         (juce::Component*) &overviewNoiseStatus,
         (juce::Component*) &reverbPower, (juce::Component*) &revTypeBox,
         (juce::Component*) &overviewMore[0], (juce::Component*) &overviewMore[1],
         (juce::Component*) &overviewMore[2] }) c->setVisible (true);
    for (auto* k : { &inGainK, &makeupK, &denoiseK, &gate, &lowCut, &mudK,
                     &comp2K, &deessK, &presenceK, &warmthK,
                     &revMixK, &revSizeK, &delayK, &widthK })
    {
        k->slider.setVisible (true);
        k->label.setVisible (true);
    }
    for (auto* lamp : { &lampDn, &lampGate, &lampDs, &lampDly }) lamp->btn.setVisible (true);
    for (auto& power : overviewSectionPower) power.setVisible (true);
    applyUseModeMask();
    // 🧭 ADHD 集中・3ステップモード(accessMode == 2)では、使用頻度の低い2段目サブスイッチを隠して認知負荷と選択麻痺を防ぐ
    if (accessMode == 2)
    {
        for (auto* sub : { (juce::Component*) &jnOnButton, (juce::Component*) &vcOnButton,
                           (juce::Component*) &tempoFitButton, (juce::Component*) &sessionButton })
            sub->setVisible (false);
    }
    overviewReturn.setVisible (false);
    updateOverviewState();
}

void VocalGzzioContent::trimOverviewOutputLevel()
{
    const float inLevel = processor.getInputLevel();
    const float outLevel = processor.getOutputLevel();
    const float curMakeup = processor.apvts.getRawParameterValue ("makeup")->load();
    const float inDb = juce::Decibels::gainToDecibels (inLevel, -60.0f);
    const float outDb = juce::Decibels::gainToDecibels (outLevel, -60.0f);
    // This is a conservative trim of the existing boost, not a loudness meter or limiter.
    if (! std::isfinite (inLevel) || ! std::isfinite (outLevel) || inDb <= -52.0f || outDb <= -52.0f)
        overviewHint = language->T ("音を確認できないため、出力音量は変えていません。普段の大きさで声を出してから押してください。",
                                    "No usable input was detected; output gain is unchanged. Sing or speak at your normal level, then press again.");
    else if (outDb <= -14.0f)
        overviewHint = language->T ("現在の出力ピークでは増幅を下げる必要がないため、音量は変えていません。小さな音を自動で持ち上げる機能ではありません。",
                                    "The current output peak does not need less boost, so gain is unchanged. This control never raises quiet signals.");
    else
    {
        const auto range = processor.apvts.getParameterRange ("makeup");
        const float nextMakeup = range.snapToLegalValue (juce::jlimit (range.start, curMakeup, curMakeup - (outDb + 14.0f)));
        if (auto* p = processor.apvts.getParameter ("makeup"))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (range.convertTo0to1 (nextMakeup));
            p->endChangeGesture();
        }
        if (nextMakeup <= range.start + 0.05f)
            overviewHint = language->T ("出力の追加増幅を0 dBまで下げました。それでも大きいときは入力音量を下げてください。音割れを防ぐリミッターではありません。",
                                        "Output boost is at 0 dB. If the signal is still too loud, lower the input gain. This is not a clipping limiter.");
        else
            overviewHint = language->T ("現在のピークを目安に、出力の追加増幅だけを下げました。歌や会話を続けてメーターを確認してください。平均音量を測る機能ではありません。",
                                        "Reduced output boost using the current peak as a guide. Keep singing or speaking and check the meter. This is not a loudness measurement.");
    }
    repaint();
}

juce::String VocalGzzioContent::overviewHelpAt (juce::Point<int> pt) const
{
    if (argonPadRect.contains (pt))
        return language->T ("丸を左へ動かすと温かく、右で明るく、下で広がる声になります。4本のバーも連動します。ダブルクリックで4つの効果を0%に戻します。",
                            "Drag the orb left for warmth, right for air, and down for stereo space. The four bars follow it. Double-click resets the four effects to 0%.");
    const char* ja[] = {
        "声の温かさと厚みを足します。上げすぎて濁ったら下げます。左右にドラッグで量を調整し、ダブルクリックで0%に戻します。",
        "言葉の芯を前に出します。伴奏に埋もれる声は少し上げ、硬く感じたら下げます。左右にドラッグで調整、ダブルクリックで0%に戻します。",
        "声の高音に明るさを足します。サー音やサ行が目立つ場合は下げます。左右にドラッグで調整、ダブルクリックで0%に戻します。",
        "声の左右への広がりを足します。ステレオで聴き比べてください。左右にドラッグで調整、ダブルクリックで0%に戻します。リバーブとは別の効果です。"
    };
    const char* en[] = {
        "Adds vocal warmth and body. Reduce it if the voice gets muddy. Drag horizontally to adjust; double-click resets to 0%.",
        "Brings vocal articulation forward. Raise gently to cut through accompaniment; reduce if it sounds hard. Drag to adjust; double-click resets to 0%.",
        "Adds high-frequency air. Reduce it if hiss or sibilance becomes prominent. Drag to adjust; double-click resets to 0%.",
        "Adds stereo width around the voice. Compare using stereo playback. Drag to adjust; double-click resets to 0%. This is separate from reverb."
    };
    for (size_t i = 0; i < argonBarRects.size(); ++i)
        if (argonBarRects[i].contains (pt)) return language->T (ja[i], en[i]);
    for (const auto& rect : argonTargetRects)
        if (rect.contains (pt))
            return language->T ("声の仕上がりをまとめて切り替えます。4つの効果と音色・圧縮・残響が変わります。入力音量とノイズ除去の設定は保ちます。",
                                "Chooses a vocal character, changing the four effects, tone, compression and reverb. Input gain and noise reduction settings are preserved.");
    if (routePillRect.contains (pt) || ioInMonoRect.contains (pt))
        return language->T ("【🎤 入力・出力 モノラル／ステレオ設定】マイク入力をモノラル（自動中央寄せ／入力1モノラル／入力2モノラル／1+2合成モノラル）またはステレオに切り替え、出力のモノラル／ステレオも設定します。",
                            "[INPUT / OUTPUT MONO & STEREO] Switch mic input between Mono (Auto, Input 1, Input 2, Sum 1+2) and Stereo, and toggle Mono/Stereo output.");
    if (ioOutMonoRect.contains (pt))
        return language->T ("【🔊 出力: モノラル ⇄ ステレオ切替】クリックすると、出力音声を「モノラル（左右同一 L=R）」と「ステレオ（立体）」の間で1クリックで切り替えます。",
                            "[OUTPUT: MONO / STEREO] Click to toggle output audio between Mono (identical L=R) and Stereo.");
    if (streamPillRect.contains (pt))
        return language->T ("ASIOで自分の声を聴きながら、専用仮想ドライバ「ライン (VocalGzzio Driver (WDM))」（または SYNCROOM Driver / VB-CABLE）へ処理後の声を同時に流し、OBS・Discord・VRChat・SYNCROOMへ直接渡します。",
                            "Simultaneously streams processed audio to 'ライン (VocalGzzio Driver (WDM))' (or SYNCROOM Driver / VB-CABLE) while keeping your main interface on ASIO.");
    if (irOverviewRect.contains (pt))
        return language->T ("実空間IR（インパルス応答）残響の音響減衰カーブと声優先ダッキングを表示します。クリックすると内蔵6部屋のIR切替・外部IRファイル(.wav/.aiff)の読込・歌っている最中の残響自動回避（声優先ダッキング）が選べます。",
                            "Displays the IR convolution decay curve and vocal-priority space ducking. Click to switch between 6 built-in rooms, load a custom IR (.wav/.aiff), or set vocal-priority ducking.");
    if (talkPillRect.contains (pt))
        return language->T ("【歌枠⇄雑談MC 瞬間切替】クリックすると歌設定（残響・エコー・ハモリ・広がり）を一切壊さず一時ミュートし、曲間のトークでもお風呂場にならないクリアなMC声へワンタッチで切り替えます。",
                            "[Singing / MC Talk Toggle] Non-destructively mutes reverb, echo, harmony, and stereo width for clean between-song MC talk while preserving 100% of your singing settings for instant return.");
    if (loopPillRect.contains (pt))
        return language->T ("【5秒録音・手放し試聴ルーパー】1回押すとマイクの素の声を5秒間録音し、直後から自動ループ再生します。骨伝導のない客観的な自分の声を聴きながら、両手で仕上がりや2Dオーブ・原音比較を追い込めます。",
                            "[5s Quick Audition Looper] Click once to record 5 seconds of your dry voice, then automatically loop it through the DSP chain so you can tweak settings and compare raw vs. processed hands-free.");
    if (coughPillRect.contains (pt))
        return language->T ("【😷 咳払い・くしゃみ瞬間ミュート】リバーブやディレイへ入る手前でマイクを8ms滑らかに無音遮断します。ポップノイズも残響の鳴り残りもゼロで、BGM再生は止めずに咳払いだけを隠せます。",
                            "[Cough Instant Mute] Smoothly mutes microphone input in 8ms before reverb/delay with zero pop noise or reverb tail while keeping BGM playing.");
    if (stagePillRect.contains (pt))
        return language->T ("【🎵 BGM・録音・声質ターゲット診断・絶叫ガード】カラオケ伴奏プレイヤー（キー変更±6・自動BGMダッキング・WDM混音選択）、1クリックWAV録音＆直前30秒さかのぼり保存（Wet+Dry同時）、5秒声質ターゲット診断、絶叫セーフティガードを開きます。",
                            "[BGM / WAV Recorder / Voice Target AI / Scream Guard] Opens Karaoke BGM player (key shift, auto-ducking, WDM mix), 1-click WAV & 30s retroactive recorder (Wet+Dry), 5s Voice Target AI tuner, and Scream Safety Guard.");
    for (size_t c = 0; c < coutureSwatchRects.size(); ++c)
        if (! coutureSwatchRects[c].isEmpty() && coutureSwatchRects[c].contains (pt))
            return language->T ("クチュール・コスメ5色のカラーバリエーション（01ロゼ／02アイシーラベンダー／03ノワールロリータ／04ティファニーミント／05ハニーピーチ）をワンタッチで切り替えます。",
                                "Switches between the 5 Couture Cosmetics colorways (01 Rose, 02 Lilac, 03 Noir, 04 Mint, 05 Peach) with a single click.");
    return {};
}

void VocalGzzioContent::updateOverviewState()
{
    auto active = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load() > 0.5f; };
    const bool learned = processor.isDenoiseLearned();
    const bool learning = processor.isDenoiseLearning();
    const bool enabled = active ("mod_souji") && active ("dn_on");
    const int amount = juce::roundToInt (processor.apvts.getRawParameterValue ("denoise")->load());
    const auto measured = learned ? language->T ("学習済", "Learned") : language->T ("自動推定", "Adaptive");
    const auto status = learning ? language->T ("測定中：声を出さないで", "Learning: stay quiet")
                      : ! enabled ? measured + language->T (" ／ ノイズ除去は切", " / Reduction OFF")
                      : measured + language->T (" ／ 除去量 ", " / Amount ") + juce::String (amount) + "%";
    overviewNoiseStatus.setText (status, juce::dontSendNotification);
    dnClearButton.setVisible (learned);
    overviewNoiseStatus.setColour (juce::Label::textColourId, ! enabled || amount == 0 ? Palette::yellow : Palette::inkSoft);
    const auto help = language->learn_tip() + "\n" + language->T ("測定だけでは除去量は変わりません。処理を入れ、ノイズ除去を20〜40%から上げて聴き比べてください。", "Learning does not change the amount. Enable processing, then start noise reduction at 20-40% and compare.");
    overviewNoiseStatus.setTooltip (help);
    const bool canTrim = processor.getInputLevel() > juce::Decibels::decibelsToGain (-52.0f)
                     && processor.getOutputLevel() > juce::Decibels::decibelsToGain (-14.0f)
                     && processor.apvts.getRawParameterValue ("makeup")->load() > 0.05f;
    overviewOutputTrim.setButtonText (canTrim ? language->T ("増幅を下げる", "Trim boost") : language->T ("音量確認", "Check level"));
    for (int i = 0; i < 3; ++i)
    {
        const auto modules = overviewModules (i);
        int count = 0;
        for (auto* id : modules) if (active (id)) ++count;
        auto& button = overviewSectionPower[(size_t) i];
        button.setToggleState (count > 0, juce::dontSendNotification);
        button.setButtonText (count == (int) modules.size() ? language->T ("入", "ON") : count == 0 ? language->T ("切", "OFF") : language->T ("一部", "PART"));
        button.setTooltip (language->T ("この区画の処理をまとめて入／切します。詳細画面で一部だけ切っている場合は「一部」と表示します。", "Enable or bypass this section. PART means some processors were bypassed in the detailed view."));
    }
    auto enable = [] (Knob& k, bool state) { k.slider.setEnabled (state); k.label.setAlpha (state ? 1.0f : 0.5f); };
    for (auto* k : { &denoiseK, &gate }) enable (*k, active ("mod_souji"));
    for (auto* k : { &lowCut, &mudK, &resK }) enable (*k, active ("mod_totonoe"));
    enable (comp2K, active ("mod_soroe"));
    enable (deessK, active ("mod_sagyo"));
    for (auto* k : { &presenceK, &airK, &warmthK }) enable (*k, active ("mod_neiro"));
    for (auto* k : { &revMixK, &revSizeK, &delayK, &widthK }) enable (*k, active ("mod_hirogari"));
}

void VocalGzzioContent::refreshOverviewText()
{
    overviewHint.clear();
    finishPresetBox.setTooltip (language->T ("目的から仕上がりを選びます。音色・音量の整え方・残響が一緒に変わります。入力音量と出力音量、ノイズ学習、音程の設定は保持します。", "Choose a complete tone, dynamics and ambience recipe. Input/output gain, noise learning and pitch settings are preserved."));
    learnButton.setButtonText (language->T ("ノイズを測る", "Learn noise"));
    relearnBtn.setButtonText (language->dn_relearn_label());
    dnClearButton.setButtonText (isOverview() ? language->T ("学習を消す", "Clear noise") : language->dnlearn_done());
    const int selected = finishPresetBox.getSelectedId();
    finishPresetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < gzzio::kNumFinishPresets; ++i)
        finishPresetBox.addItem (juce::String::fromUTF8 (language->english ? gzzio::kFinishPresets[i].nameEn : gzzio::kFinishPresets[i].name), i + 1);
    finishPresetBox.setTextWhenNothingSelected (language->T ("仕上がりを選ぶ", "Choose a finish"));
    finishPresetBox.setSelectedId (selected > 0 ? selected : (finishPresetIndex >= 0 ? finishPresetIndex + 1 : 0), juce::dontSendNotification);
    overviewDetails.setButtonText (language->T ("詳細調整", "All controls"));
    overviewDetails.setTooltip (language->T ("すべてのモジュールと細かいパラメータを一覧できる詳細画面を開きます。", "Open the detailed view with all modules and fine controls."));
    overviewReturn.setButtonText (language->T ("総合画面", "Overview"));
    overviewReturn.setTooltip (language->T ("主要な操作を1画面にまとめた総合画面へ戻ります。", "Return to the main overview screen."));
    overviewPitch.setButtonText (language->T ("音程・ハモリ", "Pitch & harmony"));
    overviewPitch.setTooltip (language->T ("ピッチ補正のキー設定、ハモリの種類、ボイス変換の詳細設定を開きます。", "Open detailed key/scale settings for pitch correction, harmony and voice changer."));
    overviewCompare.setButtonText (language->T ("押して原音と比較", "Hold to compare"));
    overviewCompare.setTooltip (language->T ("押している間は処理前の音を聴けます。離すと元の設定に戻ります。音量の差だけで判断せず、声の輪郭や余韻を比べてください。", "Hold to hear the unprocessed input. Release to return. Compare the tone and tail as well as loudness."));
    reverbPower.setButtonText (language->T ("リバーブ 入／切", "Reverb ON / OFF"));
    reverbPower.setTooltip (language->rev_tip());
    overviewOutputTrim.setButtonText (language->T ("音量確認", "Check level"));
    overviewOutputTrim.setTooltip (language->T ("声を出しながら押すと、現在の出力ピークを目安に追加増幅だけを下げます。0 dBより下げることや、小さな音の自動増幅はできません。音割れが残るときは入力音量を下げてください。",
                                               "While singing or speaking, press to reduce output boost using the current peak. It cannot go below 0 dB and never boosts quiet audio. Lower input gain if clipping remains."));
    reverbPower.setColour (juce::TextButton::buttonOnColourId, Palette::green);
    atOnButton.setColour (juce::TextButton::buttonOnColourId, Palette::blue);
    jnOnButton.setColour (juce::TextButton::buttonOnColourId, Palette::yellow);
    vcOnButton.setColour (juce::TextButton::buttonOnColourId, Palette::salmon);
    for (auto& power : overviewSectionPower) power.setColour (juce::TextButton::buttonOnColourId, Palette::green);
    tuningPopBtn.setButtonText (language->T ("チューナー", "Tuner"));
    sessionButton.setButtonText (language->T ("低遅延モード", "Low latency"));
    for (auto& b : overviewMore)
    {
        b.setButtonText (language->T ("詳しく調整", "More"));
        b.setTooltip (language->T ("このセクションの詳細パラメータを開いて細かく追い込みます。", "Open the detailed controls for this section."));
    }
    auto label = [this] (Knob& k, const char* jp, const char* en)
    { k.label.setText (language->T (jp, en), juce::dontSendNotification); };
    if (isOverview())
    {
        if (accessMode == 3)
        {
            // 🌿 ASD・感覚過敏モード: 抽象的な比喩表現を避け、物理的・具体的な音響機能名を表示
            label (inGainK, "入力ゲイン", "Input gain");     label (makeupK, "出力ゲイン", "Output gain");
            label (denoiseK, "背景ノイズ除去", "Denoise");    label (gate, "無音時ミュート", "Noise gate");
            label (lowCut, "低域カット(Hz)", "Low-cut Hz");  label (mudK, "中低域(250Hz)", "250Hz EQ");
            label (comp2K, "音量の均一化", "Compressor");    label (deessK, "歯擦音(サ行)抑制", "De-esser");
            label (presenceK, "中高域(明瞭度)", "Presence"); label (airK, "高域ブースト", "High air");
            label (warmthK, "倍音付加(温かみ)", "Harmonics"); label (resK, "共振ピーク抑制", "Resonance");
            label (revMixK, "残響ブレンド量", "Reverb mix"); label (revSizeK, "残響空間サイズ", "Room size");
            label (delayK, "反響ディレイ量", "Delay mix");   label (widthK, "ステレオ左右幅", "Stereo width");
        }
        else
        {
            label (inGainK, "入力音量", "Input"); label (makeupK, "出力音量", "Output");
            label (denoiseK, "ノイズ除去", "Noise reduction"); label (gate, "無音時のカット", "Noise gate");
            label (lowCut, "低音カット", "Low cut"); label (mudK, "こもり", "Low mids");
            label (comp2K, "音量そろえ", "Leveling"); label (deessK, "サ行おさえ", "De-essing");
            label (presenceK, "声の明るさ", "Presence"); label (airK, "きらめき", "Air");
            label (warmthK, "厚み・歪み", "Warmth"); label (resK, "耳ざわり", "Resonance");
            label (revMixK, "残響の量", "Reverb mix"); label (revSizeK, "残響の長さ", "Reverb size");
            label (delayK, "やまびこの量", "Delay mix"); label (widthK, "左右の広がり", "Stereo width");
        }
    }
    else
    {
        label (inGainK, "入力音量", "Input"); label (makeupK, "出力音量", "Output");
        label (denoiseK, "ノイズ除去", "DENOISE"); label (gate, "ゲート", "GATE");
        label (lowCut, "ローカット", "LOW CUT"); label (mudK, "こもり", "MUD");
        label (comp2K, advancedMode ? "ならし圧縮" : "音量をそろえる", "LEVELER");
        label (deessK, "サ行おさえ", "DE-ESS");
        label (presenceK, advancedMode ? "ヌケ感" : "声の明るさ", "PRESENCE");
        label (airK, "キラキラ", "AIR"); label (warmthK, "あたたかみ", "WARMTH");
        resK.label.setText (language->res_label(), juce::dontSendNotification);
        label (revMixK, "ひびき", "REVERB"); label (revSizeK, "部屋の広さ", "ROOM SIZE");
        label (delayK, "やまびこ", "ECHO"); label (widthK, "ひろがり", "WIDTH");
    }
    auto explain = [this] (Knob& k, const char* jp, const char* en)
    { k.slider.setTooltip (language->T (jp, en)); k.label.setTooltip (language->T (jp, en)); };
    explain (denoiseK, "一定のサー音を小さくします。まず黙った状態で「ノイズを測る」を押し、量を少しずつ上げます。声が薄くなったら下げてください。", "Reduces steady hiss. Measure the noise while silent, then raise the amount gradually. Back off if the voice becomes thin.");
    explain (comp2K, "強い声と弱い声の差を小さくします。上げるほど声量がそろいます。抑揚が平らになったら下げます。出力音量とは別の調整です。", "Reduces the difference between loud and quiet phrases. Increase for steadier level; reduce to preserve expression. This is not output gain.");
    explain (presenceK, "言葉の輪郭が聞こえる中高音を調整します。伴奏に埋もれるときは上げ、硬く耳に当たるときは下げます。", "Adjusts the upper mids that carry vocal clarity. Raise it to cut through a mix, lower it if the voice sounds hard.");
    explain (revMixK, "原音に足す残響の大きさです。上げるほど遠く、包まれる印象になります。言葉がぼやけたら下げます。入／切で残響だけ比較できます。", "Controls how much reverberation is added. Raise for distance and ambience, lower if words blur. Use the reverb switch to compare.");
    explain (revSizeK, "残響の余韻を調整します。上げるほど広い空間や長い余韻になります。速い曲では短くすると次の言葉を邪魔しにくくなります。", "Controls the size and decay of the selected space. Shorter settings leave more room for fast phrases.");
}

void VocalGzzioContent::resizedOverview()
{
    if (getWidth() < 100 || getHeight() < 100) return;
    const int w = getWidth(), h = getHeight(), edge = 24, gap = 14;
    themePainter.setBounds (getLocalBounds());
    crossArea = heroArea = cleanArea = dynArea = toneArea = spaceArea = seqArea = {};
    focusHeadArea = focusTipArea = focusFinishArea = focusSideArea = focusMeterArea = {};
    revTypeLabArea = crushReadArea = fxWarnArea = voiceStageArea = {};
    argonPadRect = {};
    levelBadgeRect = {};
    irOverviewRect = {};
    ioInMonoRect = {};
    ioOutMonoRect = {};
    talkPillRect = {};
    loopPillRect = {};
    coughPillRect = {};
    stagePillRect = {};
    for (auto& r : argonBarRects) r = {};
    for (auto& r : argonTargetRects) r = {};
    for (auto& r : coutureSwatchRects) r = {};
    for (auto& r : accessPillRects) r = {};

    // ---- 1段目: ロゴ・4種ワークスペース配置ピル・軽量ピル・中央寄せ/MONOピル・外部出力(SYNCROOM)ピル・遅延バッジ・ナビツールバー ----
    const int buttonH = 38;
    for (int i = 0; i < 4; ++i)
        archPillRects[(size_t) i] = { 236 + i * 68, 20, 64, 34 };
    litePillRect   = { 508, 20, 56, 34 };
    routePillRect  = { 568, 20, 74, 34 };
    streamPillRect = { 646, 20, 74, 34 };
    latBadgeArea   = { 724, 20, 64, 34 };

    auto toolbar = juce::Rectangle<int> (790, 18, w - edge - 790, buttonH);
    loadButton     .setBounds (toolbar.removeFromRight (78));  toolbar.removeFromRight (5);
    saveButton     .setBounds (toolbar.removeFromRight (78));  toolbar.removeFromRight (6);
    sizePopBtn     .setBounds (toolbar.removeFromRight (138)); toolbar.removeFromRight (5);
    overviewDetails.setBounds (toolbar.removeFromRight (112)); toolbar.removeFromRight (5);
    tuningPopBtn   .setBounds (toolbar.removeFromRight (104)); toolbar.removeFromRight (5);
    overviewPitch  .setBounds (toolbar.removeFromRight (126)); toolbar.removeFromRight (5);
    checkupButton  .setBounds (toolbar.removeFromRight (86));

    // ---- 2段目: 仕上がり・音の種類・ワンクリック自動設定・A/B比較・歌枠⇄雑談MC・5秒試聴・咳ミュート・BGM録音診断・原音比較・初期化 ----
    const int barY = 72, barH = 40;
    finishPresetBox.setBounds (110, barY, 212, barH);
    srcModeLabelArea = { 324, barY, 74, barH };
    srcModeBox     .setBounds (400, barY, 136, barH);
    autoSetupButton.setBounds (542, barY, 114, barH);
    songSetupButton.setBounds (660, barY, 114, barH);
    abA            .setBounds (780, barY, 36,  barH);
    abB            .setBounds (820, barY, 36,  barH);
    abCopy         .setBounds (860, barY, 52,  barH);
    talkPillRect   = { 918,  barY + 3, 94,  barH - 6 };
    loopPillRect   = { 1016, barY + 3, 94,  barH - 6 };
    coughPillRect  = { 1114, barY + 3, 98,  barH - 6 };
    stagePillRect  = { 1216, barY + 3, 114, barH - 6 };
    overviewCompare.setBounds (1336, barY, 144, barH);
    resetButton    .setBounds (1484, barY, w - edge - 1484, barH);

    auto grid = [this] (juce::Rectangle<int> r, std::initializer_list<Knob*> knobs, int columns)
    {
        const int count = (int) knobs.size(), rows = (count + columns - 1) / columns;
        const int cw = r.getWidth() / columns, ch = r.getHeight() / rows;
        int i = 0;
        for (auto* k : knobs)
        {
            auto cell = juce::Rectangle<int> (r.getX() + i % columns * cw, r.getY() + i / columns * ch, cw, ch).reduced (4, 2);
            k->label.getProperties().set ("fontH", (accessMode == 1) ? 16.0 : 13.5);
            k->label.setBounds (cell.removeFromTop (30));
            k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (104, cell.getWidth()), (accessMode == 1) ? 26 : 24);
            k->slider.setBounds (cell.reduced (3, 0));
            ++i;
        }
    };

    // ★ 主役ノブ大型化＋3連サテライトベイ配置 (大きさ・形・配置のメリハリを劇的に強化)
    auto heroBay = [] (juce::Rectangle<int> r, Knob* hero, Knob* sat0, Knob* sat1, Knob* sat2)
    {
        const int heroW = r.getWidth() * 51 / 100;
        auto leftHero = r.removeFromLeft (heroW).reduced (4, 2);
        r.removeFromLeft (6);
        hero->label.getProperties().set ("fontH", 14.5);
        hero->label.setBounds (leftHero.removeFromTop (30));
        hero->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (112, leftHero.getWidth()), 26);
        hero->slider.setBounds (leftHero.reduced (2, 0));

        Knob* sats[3] = { sat0, sat1, sat2 };
        const int satH = r.getHeight() / 3;
        for (int s = 0; s < 3; ++s)
        {
            auto cell = juce::Rectangle<int> (r.getX(), r.getY() + s * satH, r.getWidth(), satH).reduced (4, 2);
            sats[s]->label.getProperties().set ("fontH", 13.5);
            sats[s]->label.setBounds (cell.removeFromTop (24));
            sats[s]->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 68, 22);
            sats[s]->slider.setBounds (cell.reduced (2, 0));
        }
    };

    // ★ 中央コア4連コンソールの大・小リズム配置 (主役2基を大型化し、副ノブ2基を段違い配置)
    auto staggeredConsole = [] (juce::Rectangle<int> r, Knob* heroA, Knob* subA, Knob* heroB, Knob* subB)
    {
        const int wHero = r.getWidth() * 27 / 100; // ~154px
        const int wSub  = (r.getWidth() - wHero * 2) / 2; // ~131px
        Knob* ks[4]     = { heroA, subA, heroB, subB };
        const int ws[4] = { wHero, wSub, wHero, wSub };
        const bool isH[4] = { true, false, true, false };
        int cx = r.getX();
        for (int i = 0; i < 4; ++i)
        {
            auto cell = juce::Rectangle<int> (cx, r.getY() + (isH[i] ? 0 : 18), ws[i], r.getHeight() - (isH[i] ? 0 : 18)).reduced (3, 2);
            ks[i]->label.getProperties().set ("fontH", isH[i] ? 14.0 : 13.5);
            ks[i]->label.setBounds (cell.removeFromTop (28));
            ks[i]->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (98, cell.getWidth()), 24);
            ks[i]->slider.setBounds (cell.reduced (2, 0));
            cx += ws[i];
        }
    };

    // ✦ 星空・海・黄昏コレクション / 見やすさ・操作スタイルモード専用：4連ダイヤル配置
    //   （スタイルモード時は視線移動が一切ブレない完全水平グリッド dy=0 に整列！）
    auto celestialArc = [this] (juce::Rectangle<int> r, Knob* k0, Knob* k1, Knob* k2, Knob* k3)
    {
        Knob* ks[4] = { k0, k1, k2, k3 };
        const int cw = r.getWidth() / 4;
        const bool flatGrid = (accessMode > 0);
        const int dy[4] = { flatGrid ? 0 : 10, flatGrid ? 0 : -4, flatGrid ? 0 : -4, flatGrid ? 0 : 10 };
        const int lblH = (accessMode == 1) ? 24 : 22;
        const int boxH = (accessMode == 1) ? 24 : 22;
        for (int i = 0; i < 4; ++i)
        {
            const int x = r.getX() + i * cw;
            const int wCell = (i == 3) ? (r.getRight() - x) : cw;
            const int yOff = juce::jmax (0, dy[i]);
            const int hSub = (dy[i] < 0) ? -dy[i] : 0;
            auto cell = juce::Rectangle<int> (x, r.getY() + yOff, wCell, r.getHeight() - yOff - hSub).reduced (3, 3);
            if (accessMode == 2 && i == 0)
                cell.removeFromTop (14); // 「★まずここ」ピル分の余白を確保してラベルとの重なりを完全防止
            ks[i]->label.getProperties().set ("fontH", (accessMode == 1) ? 15.5 : ((accessMode > 0) ? 14.2 : 13.5));
            ks[i]->label.setBounds (cell.removeFromTop (lblH));
            ks[i]->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (104, cell.getWidth() - 2), boxH);
            ks[i]->slider.setBounds (cell.reduced (1, 0));
        }
    };

    const int top = 122, bottom = h - 92;

    if (overviewLayoutMode == 0)
    {
        // =========================================================================
        // ★ Architecture 0:
        //   - 通常時: ARGON COCKPIT (次世代3ウイング・コックピット配置)
        //   - 星空・海・黄昏テーマ時 (oceanStarTheme) または 見やすさスタイル時 (accessMode > 0):
        //     上段はドーム＆左右計器、中段左右に84pxの「パノラマ窓／スタイル案内＆切替デッキ」を開放し、
        //     下段は y=418 で水平一列に揃う3連デッキを展開！
        // =========================================================================
        const int wingW = 456;
        const int centerW = w - edge * 2 - wingW * 2 - gap * 2; // 1582 - 48 - 912 - 28 = 594
        const int leftX = edge;
        const int centerX = leftX + wingW + gap;
        const int rightX = centerX + centerW + gap;

        if (oceanStarTheme() || accessMode > 0)
        {
            const int upperWingH = 208; // y = 122..330 (中段 y=335..413 に水平線パノラマ窓／スタイル案内デッキを開放)
            const int domeH      = 286; // y = 122..408 (中央プラネタリウム・オペラドーム)
            const int mirrorTop  = 418; // 下段3枚が水平線で一直線に揃うデッキ (y = 418..698, 高さ280px)

            tuner.setBounds (leftX, top, wingW, upperWingH);
            overviewOutput = { rightX, top, wingW, upperWingH };

            overviewMonitor = { centerX, top, centerW, domeH };
            argonPadRect    = { centerX + 16, top + 40, 304, 192 };
            eqGraph.setBounds (centerX + 332, top + 40, centerW - 348, 76);
            for (int b = 0; b < 4; ++b)
                argonBarRects[(size_t) b] = { centerX + 332, top + 124 + b * 28, centerW - 348, 23 };
            const int targetW = (centerW - 32 - 18) / 4;
            for (int t = 0; t < 4; ++t)
                argonTargetRects[(size_t) t] = { centerX + 16 + t * (targetW + 6), top + 240, targetW, 36 };

            // 中段右デッキ下部に画面の見やすさ・操作スタイル4モード切替ピルを配置
            const int accY = top + upperWingH + 5 + 48;
            const int accGap = 5;
            const int accW = (wingW - 16 - accGap * 3) / 4;
            for (int a = 0; a < 4; ++a)
                accessPillRects[(size_t) a] = { rightX + 8 + a * (accW + accGap), accY, accW, 24 };

            overviewCards[0] = { leftX,   mirrorTop, wingW,   bottom - mirrorTop };
            overviewCards[1] = { centerX, mirrorTop, centerW, bottom - mirrorTop };
            overviewCards[2] = { rightX,  mirrorTop, wingW,   bottom - mirrorTop };

            for (int i = 0; i < 3; ++i)
            {
                const auto& c = overviewCards[(size_t) i];
                overviewMore[(size_t) i].setBounds (c.getRight() - 130, c.getY() + 10, 116, 34);
                overviewSectionPower[(size_t) i].setBounds (c.getRight() - 210, c.getY() + 10, 72, 34);
            }

            grid (juce::Rectangle<int> (overviewOutput.getRight() - 224, overviewOutput.getY() + 38, 214, overviewOutput.getHeight() - 42),
                  { &inGainK, &makeupK }, 2);
            celestialArc (overviewCards[0].reduced (10).withTrimmedTop (120), &denoiseK, &gate, &lowCut, &mudK);
            celestialArc (overviewCards[1].reduced (12).withTrimmedTop (120), &comp2K, &deessK, &presenceK, &warmthK);
            celestialArc (overviewCards[2].reduced (10).withTrimmedTop (120), &revMixK, &revSizeK, &delayK, &widthK);
        }
        else
        {
            // 左ウイング: 上=Tuner(224px), 下=Card 0(Clean)
            tuner.setBounds (leftX, top, wingW, 224);
            overviewCards[0] = { leftX, top + 224 + gap, wingW, bottom - (top + 224 + gap) };

            // 中央コア: 上=Argon 3D Trackball Core(344px), 下=Card 1(Character 段違い4連ストリップ)
            overviewMonitor = { centerX, top, centerW, 344 };
            argonPadRect    = { centerX + 16, top + 44, 316, 236 };
            eqGraph.setBounds (centerX + 344, top + 44, centerW - 360, 100);
            for (int b = 0; b < 4; ++b)
                argonBarRects[(size_t) b] = { centerX + 344, top + 154 + b * 32, centerW - 360, 26 };
            const int targetW = (centerW - 32 - 18) / 4;
            for (int t = 0; t < 4; ++t)
                argonTargetRects[(size_t) t] = { centerX + 16 + t * (targetW + 6), top + 290, targetW, 40 };

            overviewCards[1] = { centerX, top + 344 + gap, centerW, bottom - (top + 344 + gap) };

            // 右ウイング: 上=5ch Telemetry & Master I/O(224px), 下=Card 2(Space)
            overviewOutput   = { rightX, top, wingW, 224 };
            overviewCards[2] = { rightX, top + 224 + gap, wingW, bottom - (top + 224 + gap) };

            for (int i = 0; i < 3; ++i)
            {
                const auto& c = overviewCards[(size_t) i];
                overviewMore[(size_t) i].setBounds (c.getRight() - 130, c.getY() + 10, 116, 34);
                overviewSectionPower[(size_t) i].setBounds (c.getRight() - 210, c.getY() + 10, 72, 34);
            }

            // 右上 Telemetry の右半分に入出力ノブ2基、左半分に5chメーター
            grid (juce::Rectangle<int> (overviewOutput.getRight() - 224, overviewOutput.getY() + 42, 214, overviewOutput.getHeight() - 48),
                  { &inGainK, &makeupK }, 2);
            heroBay (overviewCards[0].reduced (14).withTrimmedTop (122), &denoiseK, &gate, &lowCut, &mudK);
            staggeredConsole (overviewCards[1].reduced (12).withTrimmedTop (122), &comp2K, &deessK, &presenceK, &warmthK);
            heroBay (overviewCards[2].reduced (14).withTrimmedTop (122), &revMixK, &revSizeK, &delayK, &widthK);
        }
    }
    else if (overviewLayoutMode == 1)
    {
        // =========================================================================
        // ♥ Architecture 1: V-LIVER STAGE (女性Vライバー・歌枠ステージパノラマ配置)
        //   上段パノラマ(306px): 左=2D声キャラメイクパッド / 中央=ステージ音程モニター / 右=配信あんしん音量
        //   下段リボン  (358px): 3枚のステージデッキに 4ノブ×3 = 12連マカロン横一列リボン！
        // =========================================================================
        const int stageTopH = 306;
        const int monW = 556, tunW = 508;
        const int outW = w - edge * 2 - monW - tunW - gap * 2; // 442

        overviewMonitor = { edge, top, monW, stageTopH };
        argonPadRect    = { overviewMonitor.getX() + 16, top + 42, 296, 204 };
        eqGraph.setBounds (overviewMonitor.getX() + 324, top + 42, monW - 340, 84);
        for (int b = 0; b < 4; ++b)
            argonBarRects[(size_t) b] = { overviewMonitor.getX() + 324, top + 134 + b * 29, monW - 340, 24 };
        const int targetW = (monW - 32 - 18) / 4;
        for (int t = 0; t < 4; ++t)
            argonTargetRects[(size_t) t] = { overviewMonitor.getX() + 16 + t * (targetW + 6), top + 254, targetW, 40 };

        tuner.setBounds (overviewMonitor.getRight() + gap, top, tunW, stageTopH);
        overviewOutput = { tuner.getRight() + gap, top, outW, stageTopH };

        // 下段: 3枚のパステル・ステージデッキ（各カードとも 4ノブ横一列＝合計12連マカロンリボン！）
        const int cardsTop = top + stageTopH + gap;
        const int usable = w - edge * 2 - gap * 2;
        const int cw = usable / 3;
        int x = edge;
        for (int i = 0; i < 3; ++i)
        {
            const int wi = (i == 2) ? (w - edge - x) : cw;
            overviewCards[(size_t) i] = { x, cardsTop, wi, bottom - cardsTop };
            overviewMore[(size_t) i].setBounds (x + wi - 130, cardsTop + 10, 116, 34);
            overviewSectionPower[(size_t) i].setBounds (x + wi - 210, cardsTop + 10, 72, 34);
            x += wi + gap;
        }

        grid (juce::Rectangle<int> (overviewOutput.getRight() - 220, overviewOutput.getY() + 44, 210, overviewOutput.getHeight() - 52),
              { &inGainK, &makeupK }, 2);
        grid (overviewCards[0].reduced (10).withTrimmedTop (122), { &denoiseK, &gate, &lowCut, &mudK }, 4);
        grid (overviewCards[1].reduced (10).withTrimmedTop (122), { &comp2K, &deessK, &presenceK, &warmthK }, 4);
        grid (overviewCards[2].reduced (10).withTrimmedTop (122), { &revMixK, &revSizeK, &delayK, &widthK }, 4);
    }
    else if (overviewLayoutMode == 2)
    {
        // =========================================================================
        // 🎛 Architecture 2: STUDIO RACK (プロ音楽家・エンジニア向け 縦5列チャンネルストリップ配置)
        //   左→右の厳格なアナログコンソール信号経路（全高 678px の縦型5ベイ・ラック）：
        //   BAY 1(312px): 01 PRE & CLEAN
        //   BAY 2(312px): 02 DYNAMICS & TONE
        //   BAY 3(380px): CENTER ARGON VECTOR MATRIX + RTA SPECTRUM + STROBE TUNER
        //   BAY 4(312px): 03 SPATIAL & AMBIENCE
        //   BAY 5(170px): MASTER BUS & VERTICAL I/O FADERS
        // =========================================================================
        const int bayGap = 12;
        const int b1W = 312, b2W = 312, b3W = 380, b4W = 312;
        const int fullH = bottom - top;
        int x = edge;

        overviewCards[0] = { x, top, b1W, fullH }; x += b1W + bayGap;
        overviewCards[1] = { x, top, b2W, fullH }; x += b2W + bayGap;
        const int centerX = x;                     x += b3W + bayGap;
        overviewCards[2] = { x, top, b4W, fullH }; x += b4W + bayGap;
        overviewOutput   = { x, top, w - edge - x, fullH };

        // BAY 3 (中央縦ラック): 上=Argon Matrix & RTA(420px), 下=Rack Strobe Tuner(246px)
        overviewMonitor = { centerX, top, b3W, 420 };
        argonPadRect    = { centerX + 14, top + 42, 206, 198 };
        for (int b = 0; b < 4; ++b)
            argonBarRects[(size_t) b] = { centerX + 228, top + 44 + b * 48, b3W - 242, 34 };
        const int targetW = (b3W - 28 - 18) / 4;
        for (int t = 0; t < 4; ++t)
            argonTargetRects[(size_t) t] = { centerX + 14 + t * (targetW + 6), top + 248, targetW, 36 };
        eqGraph.setBounds (centerX + 14, top + 294, b3W - 28, 114);

        tuner.setBounds (centerX, top + 432, b3W, fullH - 432);

        for (int i = 0; i < 3; ++i)
        {
            const auto& c = overviewCards[(size_t) i];
            overviewMore[(size_t) i].setBounds (c.getRight() - 118, c.getY() + 10, 106, 34);
            overviewSectionPower[(size_t) i].setBounds (c.getRight() - 190, c.getY() + 10, 66, 34);
        }

        // BAY 5 (Master Bus): 上半分=5連メーター、下半分=縦2段の大型入出力ノブ (columns = 1)
        grid (overviewOutput.reduced (10).withTrimmedTop (248), { &inGainK, &makeupK }, 1);
        grid (overviewCards[0].reduced (12).withTrimmedTop (126), { &denoiseK, &gate, &lowCut, &mudK }, 2);
        grid (overviewCards[1].reduced (12).withTrimmedTop (126), { &comp2K, &deessK, &presenceK, &warmthK }, 2);
        grid (overviewCards[2].reduced (12).withTrimmedTop (126), { &revMixK, &revSizeK, &delayK, &widthK }, 2);
    }
    else
    {
        // =========================================================================
        // ◎ Architecture 3: CLASSIC (従来の上部チューナー＋下部3カード配置)
        // =========================================================================
        const int monitorH = 230;
        const int tunerW = w * 48 / 100, outputW = 304;
        tuner.setBounds (edge, top, tunerW, monitorH);
        overviewOutput  = { w - edge - outputW, top, outputW, monitorH };
        overviewMonitor = { tuner.getRight() + gap, top, overviewOutput.getX() - gap * 2 - tuner.getRight(), monitorH };
        eqGraph.setBounds (overviewMonitor.reduced (14).withTrimmedTop (34).withTrimmedBottom (8));

        const int cardsTop = top + monitorH + gap;
        const int usable = w - edge * 2 - gap * 2;
        const int widths[] = { usable / 3, usable / 3, usable - 2 * (usable / 3) };
        int x = edge;
        for (int i = 0; i < 3; ++i)
        {
            overviewCards[(size_t) i] = { x, cardsTop, widths[i], bottom - cardsTop };
            overviewMore[(size_t) i].setBounds (x + widths[i] - 134, cardsTop + 12, 118, 34);
            overviewSectionPower[(size_t) i].setBounds (x + widths[i] - 216, cardsTop + 12, 74, 34);
            x += widths[i] + gap;
        }
        grid (overviewOutput.reduced (10).withTrimmedTop (94), { &inGainK, &makeupK }, 2);
        grid (overviewCards[0].reduced (16).withTrimmedTop (126), { &denoiseK, &gate, &lowCut, &mudK }, 2);
        grid (overviewCards[1].reduced (16).withTrimmedTop (126), { &comp2K, &deessK, &presenceK, &warmthK }, 2);
        grid (overviewCards[2].reduced (16).withTrimmedTop (126), { &revMixK, &revSizeK, &delayK, &widthK }, 2);
    }

    if (accessMode == 0 && (liverTheme() || oceanStarTheme() || themeMode == 2 || themeMode == 4
        || (overviewLayoutMode == 1 && themeMode != 1 && themeMode != 8 && themeMode != 9)))
    {
        const int swW = (overviewMonitor.getWidth() < 460) ? 44 : 58;
        const int swGap = 4;
        const int swTotal = swW * 5 + swGap * 4;
        const int swX0 = overviewMonitor.getRight() - 12 - swTotal;
        const int swY = overviewMonitor.getY() + 9;
        for (int c = 0; c < 5; ++c)
            coutureSwatchRects[(size_t) c] = { swX0 + c * (swW + swGap), swY, swW, 26 };
    }

    const int levelButtonWidth = overviewOutput.getWidth() < 240 ? 74 : (overviewOutput.getWidth() < 340 ? 76 : 94);
    levelBadgeRect = { overviewOutput.getRight() - levelButtonWidth - 10, overviewOutput.getY() + 10, levelButtonWidth, 24 };
    overviewOutputTrim.setBounds (levelBadgeRect);

    // 計器＆入出力カード内の「🎤入力: モノラル/ステレオ」「🔊出力: モノラル/ステレオ」直結ピル配置
    if (overviewLayoutMode == 2)
    {
        // STUDIO RACK (幅170px・縦長): 5連メーターと入出力ノブの間に横並びで配置
        const int pillW = (overviewOutput.getWidth() - 20) / 2;
        ioInMonoRect  = { overviewOutput.getX() + 8,              overviewOutput.getY() + 206, pillW, 26 };
        ioOutMonoRect = { overviewOutput.getX() + 12 + pillW,     overviewOutput.getY() + 206, pillW, 26 };
    }
    else if (overviewOutput.getWidth() >= 400)
    {
        // ARGON (456px) / V-LIVER (442px): カードヘッダ行に見出しと音量確認ボタンの間へ配置
        const int rightLimit = levelBadgeRect.getX() - 6;
        const int outW = 108;
        const int inW  = 114;
        ioOutMonoRect = { rightLimit - outW,               overviewOutput.getY() + 9, outW, 26 };
        ioInMonoRect  = { ioOutMonoRect.getX() - 5 - inW,  overviewOutput.getY() + 9, inW,  26 };
    }
    else
    {
        // CLASSIC (304px): カードヘッダ行にコンパクト配置
        const int rightLimit = levelBadgeRect.getX() - 4;
        const int outW = 76;
        const int inW  = 80;
        ioOutMonoRect = { rightLimit - outW,               overviewOutput.getY() + 9, outW, 26 };
        ioInMonoRect  = { ioOutMonoRect.getX() - 4 - inW,  overviewOutput.getY() + 9, inW,  26 };
    }

    // ---- 各セクションカード上段のダイレクトスイッチ群（全配置共通でカード上部に整列） ----
    // カード0（整える）上段2行: ノイズ測定・自動追従・状態表示
    auto learning = overviewCards[0].reduced (12).withTrimmedTop (50).withHeight (34);
    learnButton.setBounds (learning.removeFromLeft ((learning.getWidth() - 6) / 2));
    learning.removeFromLeft (6);
    relearnBtn.setBounds (learning);
    auto noiseState = overviewCards[0].reduced (12).withTrimmedTop (88).withHeight (30);
    dnClearButton.setBounds (noiseState.removeFromRight (108));
    noiseState.removeFromRight (6);
    overviewNoiseStatus.setBounds (noiseState);

    // カード1（声をつくる）上段2行: つぶさない＆スマートEQ、ピッチ補正・5人ユニゾン・ボイス変換
    auto charRow1 = overviewCards[1].reduced (12).withTrimmedTop (50).withHeight (34);
    crushBtn   .setBounds (charRow1.removeFromLeft ((charRow1.getWidth() - 6) / 2));
    charRow1.removeFromLeft (6);
    seqOnButton.setBounds (charRow1);

    auto charRow2 = overviewCards[1].reduced (12).withTrimmedTop (88).withHeight (30);
    if (accessMode == 2)
    {
        atOnButton.setBounds (charRow2);
    }
    else
    {
        const int swW = (charRow2.getWidth() - 12) / 3;
        atOnButton.setBounds (charRow2.removeFromLeft (swW)); charRow2.removeFromLeft (6);
        jnOnButton.setBounds (charRow2.removeFromLeft (swW)); charRow2.removeFromLeft (6);
        vcOnButton.setBounds (charRow2);
    }

    // カード2（空間をつくる）上段2行: リバーブ電源・種類選択、テンポ合わせ・低遅延モード
    auto spaceRow1 = overviewCards[2].reduced (12).withTrimmedTop (50).withHeight (34);
    reverbPower.setBounds (spaceRow1.removeFromLeft (juce::jmin (180, (spaceRow1.getWidth() - 6) / 2)));
    spaceRow1.removeFromLeft (6);
    revTypeBox.setBounds (spaceRow1);

    auto spaceRow2 = overviewCards[2].reduced (12).withTrimmedTop (88).withHeight (30);
    if (accessMode == 2)
    {
        irOverviewRect = spaceRow2;
    }
    else
    {
        const int spBtnW = (spaceRow2.getWidth() - 12) * 32 / 100;
        tempoFitButton.setBounds (spaceRow2.removeFromLeft (spBtnW));
        spaceRow2.removeFromLeft (6);
        sessionButton .setBounds (spaceRow2.removeFromLeft (spBtnW));
        spaceRow2.removeFromLeft (6);
        irOverviewRect = spaceRow2;
    }

    for (auto* lamp : { &lampDn, &lampGate, &lampDs, &lampDly }) lamp->btn.setSize (28, 28);
    auto placePower = [] (Lamp& lamp, Knob& k)
    {
        const auto b = k.slider.getBounds();
        if (k.slider.getTextBoxPosition() == juce::Slider::TextBoxRight)
            lamp.btn.setBounds (b.getX() + 2, b.getY() + 2, 28, 28);
        else
            lamp.btn.setBounds (b.getRight() - 30, b.getY() + 2, 28, 28);
        lamp.btn.toFront (false);
    };
    placePower (lampDn, denoiseK); placePower (lampGate, gate);
    placePower (lampDs, deessK); placePower (lampDly, delayK);
    overviewHelp = { edge, h - 78, w - edge * 2, 60 };
}

void VocalGzzioContent::paintOverview (juce::Graphics& g)
{
    GzzioLnF::setUseKawaiiFont (pastelTheme() && accessMode == 0);

    // 軽量モード(liteMode)または見やすさスタイルモード(accessMode > 0)時は背景の細かい模様やパーティクルを止め、文字が際立つクリーンな下地にする
    if (liteMode || accessMode == 3)
    {
        g.fillAll (Palette::bgTop);
    }
    else if (accessMode == 1 || accessMode == 2)
    {
        g.setGradientFill (juce::ColourGradient (Palette::bgTop, 0.0f, 0.0f,
                                                 Palette::bgBot, 0.0f, (float) getHeight(), false));
        g.fillRect (getLocalBounds());
    }
    else
    {
        // テーマ背景を総合画面にも全面適用（四季・オーロラ・Vライバー・ネオン・スタジオ等が映える）
        themePainter.setBounds (getLocalBounds());
        if (! themePainter.paint (g))
        {
            g.setGradientFill (juce::ColourGradient (Palette::bgTop, 0.0f, 0.0f,
                                                     Palette::bgBot, 0.0f, (float) getHeight(), false));
            g.fillRect (getLocalBounds());
            if (themeMode == 0)
            {
                g.setGradientFill (juce::ColourGradient (Palette::ice.withAlpha (0.08f),
                    (float) getWidth() * 0.5f, 180.0f, juce::Colours::transparentBlack,
                    (float) getWidth() * 0.5f, (float) getHeight(), true));
                g.fillRect (getLocalBounds());
            }
        }
    }

    auto text = [&] (const juce::String& value, juce::Rectangle<int> r, float height, juce::Colour colour, bool bold = false,
                     juce::Justification just = juce::Justification::centredLeft)
    {
        g.setColour (colour);
        fitText (g, value, r, just, juce::jmax (14.0f, height), bold);
    };

    // ---- 2段コマンドデッキ・コンソールバー（通常モード時は立体ガラスデッキ＆イルミネーションラインを描画） ----
    if (! liteMode)
    {
        const bool lightDeck = Palette::panel.getPerceivedBrightness() > 0.55f;
        const float deckAlpha = (accessMode > 0) ? 0.98f : (nightTheme() ? 0.78f : (lightDeck ? 0.94f : 0.66f));
        const juce::Rectangle<float> topDeck (16.0f, 9.0f, (float) getWidth() - 32.0f, 52.0f);
        const juce::Rectangle<float> botDeck (16.0f, 65.0f, (float) getWidth() - 32.0f, 52.0f);
        for (const auto& d : { topDeck, botDeck })
        {
            g.setColour (lightDeck ? Palette::metalDark.withAlpha (0.16f) : juce::Colours::black.withAlpha (nightTheme() ? 0.28f : 0.08f));
            g.fillRoundedRectangle (d.translated (0.0f, 2.5f), 14.0f);
            const auto dTop = lightDeck ? Palette::panel.brighter (0.03f) : Palette::panel.brighter (0.08f).withMultipliedAlpha (deckAlpha);
            const auto dBot = lightDeck ? Palette::panel2.interpolatedWith (Palette::panel, 0.42f) : Palette::panel.darker (0.08f).withMultipliedAlpha (deckAlpha);
            g.setGradientFill (juce::ColourGradient (dTop, d.getX(), d.getY(), dBot, d.getX(), d.getBottom(), false));
            g.fillRoundedRectangle (d, 14.0f);
            g.setColour (juce::Colours::white.withAlpha (nightTheme() ? 0.16f : 0.82f));
            g.drawHorizontalLine (juce::roundToInt (d.getY() + 1.2f), d.getX() + 14.0f, d.getRight() - 14.0f);
            g.setColour (liverTheme() ? Palette::metalMid
                       : lightDeck    ? Palette::panelLn.darker (0.08f)
                                      : Palette::panelLn.withAlpha (0.90f));
            g.drawRoundedRectangle (d.reduced (0.6f), 14.0f, (lightDeck || liverTheme()) ? 1.5f : 1.1f);
        }

        // 3D発光ブランド・エンブレムバッジ（ペルソナ別にクチュール・カメオ宝石／マカロン／5連バーを描き分け）
        const juce::Rectangle<float> badge (24.0f, 16.0f, 38.0f, 38.0f);
        const bool badgeLiver = liverTheme() || (overviewLayoutMode == 1 && pastelTheme());
        const bool badgeYuru  = (themeMode == 1 || themeMode == 8);
        const auto badgeCol   = badgeLiver ? Palette::salmon : Palette::yellow;
        g.setColour (badgeLiver ? Palette::metalMid.withAlpha (0.65f) : badgeCol.withAlpha (0.32f));
        g.fillRoundedRectangle (badge.expanded (2.5f), badgeLiver || badgeYuru ? 16.0f : 12.5f);
        g.setGradientFill (juce::ColourGradient (badgeCol.brighter (0.22f), badge.getX(), badge.getY(),
                                                 badgeLiver ? Palette::salmon.darker (0.32f) : Palette::yellowDk,
                                                 badge.getRight(), badge.getBottom(), false));
        g.fillRoundedRectangle (badge, badgeLiver || badgeYuru ? 14.0f : 11.0f);
        g.setColour (badgeLiver ? Palette::metalHi : juce::Colours::white.withAlpha (0.78f));
        g.drawRoundedRectangle (badge.reduced (1.0f), badgeLiver || badgeYuru ? 13.0f : 10.0f, 1.5f);
        if (badgeLiver)
        {
            // ♥ V-Liver: クチュール金具縁の3Dハート＆ティアラ・カメオエンブレム
            const float hx = badge.getCentreX(), hy = badge.getCentreY() + 1.5f, hs = 11.2f;
            juce::Path hp;
            hp.startNewSubPath (hx, hy + hs * 0.58f);
            hp.cubicTo (hx - hs * 1.10f, hy - hs * 0.10f, hx - hs * 0.58f, hy - hs * 0.92f, hx, hy - hs * 0.28f);
            hp.cubicTo (hx + hs * 0.58f, hy - hs * 0.92f, hx + hs * 1.10f, hy - hs * 0.10f, hx, hy + hs * 0.58f);
            hp.closeSubPath();
            g.setColour (juce::Colours::white.withAlpha (0.96f));
            g.fillPath (hp);
            g.setColour (Palette::metalHi);
            g.strokePath (hp, juce::PathStrokeType (1.2f));
            g.fillEllipse (hx - 2.3f, hy - hs * 0.88f, 4.6f, 4.6f);
        }
        else if (badgeYuru)
        {
            // 🍰 Yuru-fuwa: パティスリー・フランボワーズマカロン＆金箔エンブレム
            const float mx = badge.getCentreX(), my = badge.getCentreY() + 2.0f;
            g.setColour (juce::Colour (0xfffffdf8));
            g.fillRoundedRectangle (mx - 11.0f, my - 6.5f, 22.0f, 6.0f, 3.0f);
            g.fillRoundedRectangle (mx - 11.0f, my + 1.5f, 22.0f, 5.5f, 2.8f);
            g.setColour (Palette::salmon);
            g.fillRoundedRectangle (mx - 12.0f, my - 1.5f, 24.0f, 3.5f, 1.7f);
            g.fillEllipse (mx - 3.5f, my - 11.5f, 7.0f, 7.0f);
        }
        else
        {
            const float bars[] = { 10.0f, 20.0f, 28.0f, 16.0f, 22.0f };
            g.setColour (Palette::readableOn (Palette::yellow));
            for (int i = 0; i < 5; ++i)
            {
                const float bx = 29.5f + (float) i * 5.8f;
                g.fillRoundedRectangle (bx, 35.0f - bars[i] * 0.5f, 3.0f, bars[i], 1.5f);
            }
        }
    }
    else
    {
        g.setColour (Palette::yellow);
        g.fillRoundedRectangle (24.0f, 18.0f, 38.0f, 38.0f, 11.0f);
        g.setColour (Palette::readableOn (Palette::yellow));
        const float bars[] = { 9, 19, 27, 14 };
        for (int i = 0; i < 4; ++i) g.fillRoundedRectangle (33.0f + i * 6.0f, 37.0f - bars[i] * 0.5f, 3.0f, bars[i], 1.5f);
    }
    text ("VocalGzzio", { 70, 14, 162, 42 }, 27, Palette::ink, true);

    // ---- 1段目中央: 4種ワークスペース配置アーキテクチャ切替ピル ＋ [⚡軽量] ＋ [🎯中央/MONO] ＋ [📡外部出力] ----
    {
        auto drawHeaderPill = [&] (juce::Rectangle<int> ri, bool active, juce::Colour col, const juce::String& label)
        {
            if (ri.isEmpty()) return;
            const auto r = ri.toFloat();
            const float pillRad = pastelTheme() ? r.getHeight() * 0.5f : 8.5f;
            if (liteMode)
            {
                g.setColour (active ? col.withAlpha (0.35f) : Palette::panel2);
                g.fillRoundedRectangle (r, pillRad);
            }
            else if (pastelTheme() && ! nightTheme())
            {
                // ♥ クチュール・エナメル＆宝飾金具・ヘッダーピル
                g.setColour (Palette::panelLn.withAlpha (active ? 0.38f : 0.22f));
                g.fillRoundedRectangle (r.translated (0.0f, 1.8f), pillRad);
                const auto topC = active ? col.brighter (0.12f) : Palette::panel.brighter (0.04f);
                const auto botC = active ? col.darker (0.08f)   : Palette::panel2;
                g.setGradientFill (juce::ColourGradient (topC, r.getX(), r.getY(), botC, r.getX(), r.getBottom(), false));
                g.fillRoundedRectangle (r, pillRad);
                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (active ? 0.45f : 0.75f), r.getX(), r.getY() + 1.0f,
                                                         juce::Colours::transparentWhite, r.getX(), r.getY() + r.getHeight() * 0.45f, false));
                g.fillRoundedRectangle (r.reduced (2.0f, 1.0f).withHeight (r.getHeight() * 0.44f), pillRad * 0.85f);
                const juce::Colour goldBorder = liverTheme() ? Palette::metalMid : Palette::panelLn.darker (0.12f);
                g.setColour (active ? col.darker (0.28f) : goldBorder);
                g.drawRoundedRectangle (r.reduced (0.5f), pillRad, active ? 1.8f : 1.3f);
                g.setColour (active ? Palette::readableOn (col) : Palette::ink);
                fitText (g, label, ri.reduced (2, 0), juce::Justification::centred, 14.0f, true);
                return;
            }
            else
            {
                g.setColour (pastelTheme() ? col.withAlpha (active ? 0.24f : 0.10f)
                                           : juce::Colours::black.withAlpha (active ? 0.26f : 0.12f));
                g.fillRoundedRectangle (r.translated (0.0f, 1.5f), pillRad);
                const auto topC = active ? col.withAlpha (pastelTheme() ? 0.34f : 0.42f) : Palette::panel2.brighter (0.08f).withAlpha (0.92f);
                const auto botC = active ? col.darker (0.14f).withAlpha (0.24f)          : Palette::panel2.darker (0.06f).withAlpha (0.92f);
                g.setGradientFill (juce::ColourGradient (topC, r.getX(), r.getY(), botC, r.getX(), r.getBottom(), false));
                g.fillRoundedRectangle (r, pillRad);
                g.setColour (juce::Colours::white.withAlpha (pastelTheme() ? 0.65f : (active ? 0.36f : 0.18f)));
                g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.0f), r.getX() + 7.0f, r.getRight() - 7.0f);
                if (active)
                {
                    g.setColour (col.withAlpha (0.95f));
                    g.fillRoundedRectangle (r.getX() + 8.0f, r.getBottom() - 3.5f, r.getWidth() - 16.0f, 2.5f, 1.2f);
                }
            }
            g.setColour (active ? col : (liverTheme() ? Palette::metalMid : Palette::panelLn));
            g.drawRoundedRectangle (r.reduced (0.5f), pillRad, active ? 1.8f : 1.0f);
            g.setColour (active ? Palette::ink : Palette::inkSoft);
            fitText (g, label, ri.reduced (2, 0), juce::Justification::centred, 14.0f, active);
        };

        const char* pillJa[4] = { oceanStarTheme() ? "✦天球水鏡" : "★ARGON", "♥LIVER", "🎛STUDIO", "◎従来" };
        const char* pillEn[4] = { oceanStarTheme() ? "HORIZON"   : "ARGON",  "LIVER",  "STUDIO",  "CLASSIC" };
        const juce::Colour pillCols[4] = { Palette::ice, Palette::salmon, Palette::yellow, Palette::blue };
        for (int i = 0; i < 4; ++i)
        {
            drawHeaderPill (archPillRects[(size_t) i], overviewLayoutMode == i, pillCols[i],
                            juce::String::fromUTF8 (language->english ? pillEn[i] : pillJa[i]));
        }

        // [⚡軽量 / LITE] ワンタッチ省負荷モードピル
        drawHeaderPill (litePillRect, liteMode, Palette::green, language->T ("⚡軽量", "LITE"));

        // [🎯入出力モノラル / MONO] 入力・出力モノラル／ステレオ切替ピル
        if (! routePillRect.isEmpty())
        {
            const int rMode = processor.getInputRouteMode();
            const bool outMono = processor.getOutputMonoMode();
            const bool rActive = outMono || (rMode != 4);
            const auto rCol = outMono ? Palette::blue : Palette::ice;
            const char* rJa[5] = { "🎯入:自動モノ", "🎯入:IN1モノ", "🎯入:IN2モノ", "🎯入:1+2モノ", "🎯ステレオ" };
            const char* rEn[5] = { "IN:AUTO-M",    "IN:1-MONO",    "IN:2-MONO",    "IN:1+2-M",     "STEREO"     };
            const int idx = juce::jlimit (0, 4, rMode);
            const juce::String rLabel = outMono
                ? language->T ("🎯入出力:モノ", "I/O:MONO")
                : juce::String::fromUTF8 (language->english ? rEn[idx] : rJa[idx]);
            drawHeaderPill (routePillRect, rActive, rCol, rLabel);
        }

        // [📡外部出力] VocalGzzio Driver (WDM) / SYNCROOM / VB-CABLE 同時出力ブリッジピル
        if (! streamPillRect.isEmpty())
        {
            const bool sOn = processor.getStreamOut().isRunning();
            const bool isVgWdm = sOn && processor.getStreamOut().getDeviceName().containsIgnoreCase ("VocalGzzio");
            drawHeaderPill (streamPillRect, sOn, sOn ? Palette::green : Palette::salmon,
                            sOn ? (isVgWdm ? language->T ("📡WDM出力中", "WDM ON")
                                           : language->T ("📡SYNC中", "SYNC ON"))
                                : language->T ("📡外部出力", "SYNC OUT"));
        }

        // [🎤歌枠 ⇄ 💬雑談MC] 歌設定を100%保ったまま残響・エコー・ハモリを一時オフにするライブ瞬間切替ピル
        if (! talkPillRect.isEmpty())
        {
            const bool isTalk = processor.getLiveTalkMode();
            drawHeaderPill (talkPillRect, isTalk, isTalk ? Palette::yellow : Palette::ice,
                            isTalk ? language->T ("💬雑談MC中", "MC TALK") : language->T ("🎤歌枠/MC", "SING/MC"));
        }

        // [🔄5秒試聴] 5秒録って手放し聴き比べできる試聴ルーパーピル（録音中・再生中はプログレスバーも表示）
        if (! loopPillRect.isEmpty())
        {
            const int aState = processor.getAuditionState();
            const bool aActive = (aState != VocalGzzioProcessor::AuditionOff);
            const auto aCol = (aState == VocalGzzioProcessor::AuditionRecording) ? Palette::salmon
                            : (aState == VocalGzzioProcessor::AuditionPlaying)   ? Palette::green : Palette::blue;
            const juce::String aLabel = (aState == VocalGzzioProcessor::AuditionRecording)
                ? language->T ("●録音中...", "●REC 5s")
                : (aState == VocalGzzioProcessor::AuditionPlaying)
                    ? language->T ("🔄試聴再生", "LOOP ON")
                    : language->T ("🔄5秒試聴", "5s LOOP");
            drawHeaderPill (loopPillRect, aActive, aCol, aLabel);
            if (aActive)
            {
                const float prog = juce::jlimit (0.0f, 1.0f, processor.getAuditionProgress());
                auto bar = loopPillRect.toFloat().reduced (8.0f, 0.0f);
                bar = bar.withTop (bar.getBottom() - 3.5f).withHeight (2.2f);
                g.setColour (Palette::panel.withAlpha (0.65f));
                g.fillRoundedRectangle (bar, 1.1f);
                g.setColour (aCol);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * prog), 1.1f);
            }
        }

        // [😷咳ミュート] 咳払い・くしゃみ瞬間無音遮断ピル（8ms滑らかフェード・残響鳴り残り防止）
        if (! coughPillRect.isEmpty())
        {
            const bool cMute = processor.getCoughMute();
            drawHeaderPill (coughPillRect, cMute, cMute ? Palette::salmon : Palette::inkSoft,
                            cMute ? language->T ("🔇ミュート中", "MUTED") : language->T ("😷咳ミュート", "COUGH"));
        }

        // [🎵BGM・録音・診断] カラオケ伴奏・1クリックWAV録音・30秒さかのぼり・声質ターゲット診断ピル
        if (! stagePillRect.isEmpty())
        {
            const bool recOn = processor.isWavRecording();
            const bool bgmOn = processor.isBgmPlaying();
            const bool stAct = recOn || bgmOn;
            const auto stCol = recOn ? Palette::salmon : (bgmOn ? Palette::green : Palette::yellow);
            const juce::String stLab = recOn
                ? language->T ("🔴WAV録音中", "●WAV REC")
                : bgmOn
                    ? language->T ("🎵BGM再生中", "🎵BGM ON")
                    : language->T ("🎵BGM・録音", "BGM/REC");
            drawHeaderPill (stagePillRect, stAct, stCol, stLab);
            if (bgmOn)
            {
                const float prog = juce::jlimit (0.0f, 1.0f, processor.getBgmProgress());
                auto bar = stagePillRect.toFloat().reduced (8.0f, 0.0f);
                bar = bar.withTop (bar.getBottom() - 3.5f).withHeight (2.2f);
                g.setColour (Palette::panel.withAlpha (0.65f));
                g.fillRoundedRectangle (bar, 1.1f);
                g.setColour (stCol);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * prog), 1.1f);
            }
        }
    }

    const auto latencyMs = 1000.0 * processor.addedLatencySamples() / juce::jmax (1.0, processor.getTunerSampleRate());
    g.setColour (Palette::inkSoft);
   #if VOCALGZZIO_TRIAL
    fitText (g, juce::String (JucePlugin_VersionString) + " " + language->T ("体験 ", "TRIAL ") + juce::String (latencyMs, 1) + "ms",
             latBadgeArea, juce::Justification::centredLeft, 14.0f, true);
   #else
    fitText (g, juce::String (JucePlugin_VersionString) + (liteMode ? " ECO " : " ") + juce::String (latencyMs, 1) + "ms",
             latBadgeArea, juce::Justification::centredLeft, 14.0f, liteMode);
   #endif
    if (liteMode)
    {
        g.setColour (Palette::panelLn);
        g.drawHorizontalLine (64, 24.0f, (float) getWidth() - 24.0f);
    }
    else
    {
        const juce::Colour divCol = (overviewLayoutMode == 1 || liverTheme())   ? Palette::salmon
                                  : (overviewLayoutMode == 2 || themeMode == 9) ? Palette::yellow : Palette::ice;
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 24.0f, 63.0f,
                                                 divCol.withAlpha (0.55f), (float) getWidth() * 0.5f, 63.0f, false));
        g.fillRect (24.0f, 62.5f, (float) getWidth() * 0.5f - 24.0f, 1.5f);
        g.setGradientFill (juce::ColourGradient (divCol.withAlpha (0.55f), (float) getWidth() * 0.5f, 63.0f,
                                                 juce::Colours::transparentBlack, (float) getWidth() - 24.0f, 63.0f, false));
        g.fillRect ((float) getWidth() * 0.5f, 62.5f, (float) getWidth() * 0.5f - 24.0f, 1.5f);
    }
    text (language->T ("仕上がり", "FINISH"), { 26, 72, 86, 40 }, 16, Palette::ink, true);
    if (! srcModeLabelArea.isEmpty())
        text (language->src_label(), srcModeLabelArea.withTrimmedRight (6), 15, Palette::inkSoft, true, juce::Justification::centredRight);

    // テーマ風景が透けて見えるよう、通常時は半透明ガラス調、軽量モード・アクセシビリティモード時はAlpha=1.0の完全不透明マット下地にする
    const float cardAlpha = (liteMode || accessMode > 0) ? 1.0f
                          : oceanStarTheme() ? 0.38f
                          : liverTheme() ? 0.96f
                          : (themeMode == 2 || themeMode == 4 || themeMode == 6) ? 0.86f
                          : (themeMode == 1 || themeMode == 8 || themeMode == 9) ? 0.94f : 0.96f;

    // ★ マルチペルソナ・デザイン造形判定（かっこいいSF系／Vライバーキラキラ系／ゆるふわカフェ系／星空・海・ボタニカル天球系／名機スタジオ系）
    const bool isYuruStyle   = (themeMode == 1 || themeMode == 8);
    const bool isLiverStyle  = liverTheme() || (overviewLayoutMode == 1 && ! isYuruStyle && themeMode != 9 && ! oceanStarTheme());
    const bool isNatureAura  = (themeMode == 2 || themeMode == 3 || themeMode == 4 || oceanStarTheme());
    const bool isStudioStyle = (themeMode == 9) || (overviewLayoutMode == 2 && ! isLiverStyle && ! isYuruStyle && ! oceanStarTheme());
    const bool isSciFiStyle  = ! (isLiverStyle || isYuruStyle || isNatureAura || isStudioStyle) && (overviewLayoutMode == 0);

    // 8角形チャンファー（斜めカット装甲プレート）パス生成ヘルパー
    auto makeChamferPath = [] (juce::Rectangle<float> b, float ch)
    {
        juce::Path p;
        p.startNewSubPath (b.getX() + ch, b.getY());
        p.lineTo (b.getRight() - ch, b.getY());
        p.lineTo (b.getRight(), b.getY() + ch);
        p.lineTo (b.getRight(), b.getBottom() - ch);
        p.lineTo (b.getRight() - ch, b.getBottom());
        p.lineTo (b.getX() + ch, b.getBottom());
        p.lineTo (b.getX(), b.getBottom() - ch);
        p.lineTo (b.getX(), b.getY() + ch);
        p.closeSubPath();
        return p;
    };

    // ハート＆4芒星＆リボン描画ヘルパー（Vライバー・女子向けデザイン用）
    auto drawHeartShape = [&] (float hx, float hy, float hs, juce::Colour col)
    {
        juce::Path hp;
        hp.startNewSubPath (hx, hy + hs * 0.58f);
        hp.cubicTo (hx - hs * 1.08f, hy - hs * 0.10f, hx - hs * 0.56f, hy - hs * 0.92f, hx, hy - hs * 0.28f);
        hp.cubicTo (hx + hs * 0.56f, hy - hs * 0.92f, hx + hs * 1.08f, hy - hs * 0.10f, hx, hy + hs * 0.58f);
        hp.closeSubPath();
        g.setColour (col);
        g.fillPath (hp);
    };
    auto drawSparkle4 = [&] (float sx, float sy, float sr, juce::Colour col)
    {
        juce::Path sp;
        for (int p = 0; p < 8; ++p)
        {
            const float a = (float) p * juce::MathConstants<float>::pi * 0.25f;
            const float rad = (p % 2 == 0) ? sr : sr * 0.34f;
            const float px = sx + std::cos (a) * rad, py = sy + std::sin (a) * rad;
            if (p == 0) sp.startNewSubPath (px, py); else sp.lineTo (px, py);
        }
        sp.closeSubPath();
        g.setColour (col);
        g.fillPath (sp);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillEllipse (sx - sr * 0.22f, sy - sr * 0.22f, sr * 0.44f, sr * 0.44f);
    };
    auto drawRibbonBow = [&] (float rx, float ry, float rw, juce::Colour col)
    {
        juce::Path bow;
        bow.startNewSubPath (rx, ry);
        bow.cubicTo (rx - rw, ry - rw * 0.72f, rx - rw, ry + rw * 0.72f, rx, ry);
        bow.cubicTo (rx + rw, ry - rw * 0.72f, rx + rw, ry + rw * 0.72f, rx, ry);
        bow.closeSubPath();
        g.setColour (col);
        g.fillPath (bow);
        g.setColour (juce::Colours::white.withAlpha (0.80f));
        g.fillEllipse (rx - rw * 0.22f, ry - rw * 0.22f, rw * 0.44f, rw * 0.44f);
    };

    auto card = [&] (juce::Rectangle<int> r, juce::Colour accent)
    {
        const auto rf = r.toFloat();
        if (liteMode || accessMode > 0)
        {
            g.setColour (Palette::panel);
            g.fillRoundedRectangle (rf, 14.0f);
            // セクション見出し行の視認性を高めるマット帯
            if (accessMode == 1)
            {
                g.setColour (Palette::panel2);
                g.fillRoundedRectangle (rf.withHeight (46.0f), 14.0f);
                g.setColour (accent);
                g.drawLine (rf.getX() + 12.0f, rf.getY() + 46.0f, rf.getRight() - 12.0f, rf.getY() + 46.0f, 2.2f);
                g.setColour (Palette::panelLn);
                g.drawRoundedRectangle (rf.reduced (0.8f), 14.0f, 2.2f);
                g.setColour (accent);
                g.fillRoundedRectangle (rf.getX() + 12.0f, rf.getY() + 10.0f, 6.0f, 26.0f, 3.0f);
            }
            else if (accessMode == 2)
            {
                g.setColour (accent.withAlpha (0.14f));
                g.fillRoundedRectangle (rf.withHeight (46.0f), 14.0f);
                g.setColour (accent.withAlpha (0.65f));
                g.drawLine (rf.getX() + 14.0f, rf.getY() + 46.0f, rf.getRight() - 14.0f, rf.getY() + 46.0f, 1.5f);
                g.setColour (Palette::panelLn);
                g.drawRoundedRectangle (rf.reduced (0.6f), 14.0f, 1.6f);
                g.setColour (accent);
                g.fillRoundedRectangle (rf.getX() + 14.0f, rf.getY() + 12.0f, 5.0f, 22.0f, 2.5f);
            }
            else
            {
                g.setColour (Palette::panel2.withAlpha (0.65f));
                g.fillRoundedRectangle (rf.withHeight (45.0f), 14.0f);
                g.setColour (Palette::panelLn);
                g.drawLine (rf.getX() + 14.0f, rf.getY() + 45.0f, rf.getRight() - 14.0f, rf.getY() + 45.0f, 1.0f);
                g.drawRoundedRectangle (rf.reduced (0.5f), 14.0f, 1.2f);
                g.setColour (accent);
                g.fillRoundedRectangle (rf.getX() + 14.0f, rf.getY() + 13.0f, 4.0f, 20.0f, 2.0f);
            }
            return;
        }

        // ★ 1. ARGON SCI-FI / CYBER NEON: 斜めカット8角形メカ・アーマーフレーム＆台形ネームプレート
        if (isSciFiStyle)
        {
            const float ch = 15.0f;
            const auto armorPath   = makeChamferPath (rf, ch);
            const auto shadowPath  = makeChamferPath (rf.translated (0.0f, 4.0f), ch);
            const auto innerBezel  = makeChamferPath (rf.reduced (1.8f), ch - 1.0f);

            g.setColour (juce::Colours::black.withAlpha (nightTheme() ? 0.42f : 0.14f));
            g.fillPath (shadowPath);
            g.setColour (accent.withAlpha (nightTheme() ? 0.12f : 0.06f));
            g.strokePath (armorPath, juce::PathStrokeType (4.0f));

            const auto topBg = nightTheme() ? Palette::panel.brighter (0.06f).withMultipliedAlpha (cardAlpha)
                                            : Palette::panel.brighter (0.04f).withMultipliedAlpha (cardAlpha);
            const auto botBg = nightTheme() ? Palette::bgBot.darker   (0.18f).withMultipliedAlpha (cardAlpha)
                                            : Palette::panel.darker   (0.05f).withMultipliedAlpha (cardAlpha);
            g.setGradientFill (juce::ColourGradient (topBg, rf.getX(), rf.getY(), botBg, rf.getX(), rf.getBottom(), false));
            g.fillPath (armorPath);

            // 台形コックピット・ネームプレート帯 (y .. y+46) + 斜めハザードテレメトリストライプ
            {
                juce::Path hdrTrap;
                const float trapW = rf.getWidth() * 0.66f;
                hdrTrap.startNewSubPath (rf.getX() + ch, rf.getY() + 1.0f);
                hdrTrap.lineTo (rf.getX() + trapW, rf.getY() + 1.0f);
                hdrTrap.lineTo (rf.getX() + trapW - 22.0f, rf.getY() + 45.0f);
                hdrTrap.lineTo (rf.getX() + 1.0f, rf.getY() + 45.0f);
                hdrTrap.lineTo (rf.getX() + 1.0f, rf.getY() + ch);
                hdrTrap.closeSubPath();
                g.setGradientFill (juce::ColourGradient (accent.withAlpha (nightTheme() ? 0.30f : 0.18f), rf.getX(), rf.getY(),
                                                         accent.withAlpha (0.02f), rf.getX() + trapW, rf.getY() + 45.0f, false));
                g.fillPath (hdrTrap);

                // 斜めテレメトリ・シェブロンスリット (///)
                g.setColour (accent.withAlpha (nightTheme() ? 0.28f : 0.18f));
                for (int sl = 0; sl < 4; ++sl)
                {
                    const float sx = rf.getX() + trapW - 44.0f + (float) sl * 9.0f;
                    g.drawLine (sx + 10.0f, rf.getY() + 8.0f, sx, rf.getY() + 38.0f, 2.2f);
                }
            }

            // ヘッダー仕切りレーザーコンジットライン
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.78f), rf.getX() + 12.0f, (float) (r.getY() + 45),
                                                     accent.withAlpha (0.10f), rf.getRight() - 12.0f, (float) (r.getY() + 45), false));
            g.fillRect (rf.getX() + 12.0f, (float) (r.getY() + 45), rf.getWidth() - 24.0f, 1.8f);

            // 8角形ダブルベゼル＆四隅の斜め発光コンジット
            g.setColour (juce::Colours::white.withAlpha (nightTheme() ? 0.07f : 0.25f));
            g.strokePath (innerBezel, juce::PathStrokeType (1.0f));
            g.setColour (Palette::panelLn.brighter (0.12f));
            g.strokePath (armorPath, juce::PathStrokeType (1.4f));

            // 4箇所の斜めチャンファー角をアクセント色でハイライト発光
            g.setColour (accent.withAlpha (0.90f));
            g.drawLine (rf.getX(), rf.getY() + ch, rf.getX() + ch, rf.getY(), 2.4f);
            g.drawLine (rf.getRight() - ch, rf.getY(), rf.getRight(), rf.getY() + ch, 2.4f);
            g.drawLine (rf.getX(), rf.getBottom() - ch, rf.getX() + ch, rf.getBottom(), 2.4f);
            g.drawLine (rf.getRight() - ch, rf.getBottom(), rf.getRight(), rf.getBottom() - ch, 2.4f);

            // 下辺センターのサイバー・エネルギーレール
            const float railW = juce::jmin (110.0f, rf.getWidth() * 0.32f);
            g.setColour (accent.withAlpha (0.65f));
            g.fillRoundedRectangle (rf.getCentreX() - railW * 0.5f, rf.getBottom() - 3.2f, railW, 2.4f, 1.2f);

            // 3D発光アクセント・ピラーバッジ
            g.setColour (accent.withAlpha (0.36f));
            g.fillRoundedRectangle (rf.getX() + 13.5f, rf.getY() + 11.5f, 7.0f, 25.0f, 3.5f);
            g.setGradientFill (juce::ColourGradient (accent.brighter (0.35f), rf.getX() + 15.0f, rf.getY() + 13.0f,
                                                     accent.darker   (0.15f), rf.getX() + 15.0f, rf.getY() + 35.0f, false));
            g.fillRoundedRectangle (rf.getX() + 15.0f, rf.getY() + 13.0f, 4.5f, 22.0f, 2.2f);
            g.setColour (Palette::ink);
            return;
        }

        // 共通ベース: ドロップシャドウ＋ソフトオーラ＋クチュール・ファンデーション下地塗り
        const float cardRad = (isLiverStyle || isYuruStyle) ? 18.0f : 16.0f;
        g.setColour (isLiverStyle ? Palette::metalDark.darker (0.35f).withAlpha (nightTheme() ? 0.38f : 0.22f)
                   : isYuruStyle  ? juce::Colour (0x266b4228)
                                  : juce::Colours::black.withAlpha (nightTheme() ? 0.32f : 0.10f));
        g.fillRoundedRectangle (rf.translated (0.0f, 3.8f), cardRad);
        g.setColour (accent.withAlpha (nightTheme() ? 0.10f : 0.08f));
        g.fillRoundedRectangle (rf.expanded (1.5f), cardRad + 1.0f);

        if (isLiverStyle && ! nightTheme())
        {
            // ♥ クチュール・クッションファンデーション下地（ロゼ／アイシーラベンダー／ミント／ハニーピーチ完全連動）
            const juce::Colour fndTop = Palette::panel;
            const juce::Colour fndMid = Palette::panel.interpolatedWith (Palette::panel2, 0.55f).interpolatedWith (accent, 0.06f);
            const juce::Colour fndBot = Palette::panel2.interpolatedWith (accent, 0.10f);
            juce::ColourGradient fndGrad (fndTop, rf.getX(), rf.getY(), fndBot, rf.getX(), rf.getBottom(), false);
            fndGrad.addColour (0.55, fndMid);
            g.setGradientFill (fndGrad);
            g.fillRoundedRectangle (rf, cardRad);
        }
        else if (isYuruStyle && ! nightTheme())
        {
            // 🍰 パティスリー・バニラカスタード〜ベイクドサブレ下地
            g.setGradientFill (juce::ColourGradient (Palette::panel, rf.getX(), rf.getY(),
                                                     Palette::panel2.interpolatedWith (Palette::panel, 0.42f), rf.getX(), rf.getBottom(), false));
            g.fillRoundedRectangle (rf, cardRad);
        }
        else if (oceanStarTheme())
        {
            // 🌌🌊🌅🍁👑 風景・季節コレクション: 背景の銀河・水紋・朝焼け・紅葉と雪が透ける高透過クリスタルガラス＋世界観別カード内質感
            g.setGradientFill (juce::ColourGradient (Palette::panel.brighter (0.08f).withMultipliedAlpha (cardAlpha), rf.getX(), rf.getY(),
                                                     Palette::mirrorBot.withMultipliedAlpha (cardAlpha * 0.85f),      rf.getX(), rf.getBottom(), false));
            g.fillRoundedRectangle (rf, cardRad);

            juce::Path cardClip;
            cardClip.addRoundedRectangle (rf.reduced (1.5f), cardRad - 1.0f);
            juce::Graphics::ScopedSaveState cs (g);
            g.reduceClipRegion (cardClip);

            if (themeMode == 15)
            {
                // 🌊 02 碧海: カード内部に揺らめく水紋コースティクス＆上縁のぷるんとした水面メニスカス反射
                ThemePainter::drawCausticWaveMesh (g, rf, Palette::ice, 0.22f, r.getX() / 40);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0x55caf0f8), rf.getCentreX(), rf.getY(),
                                                         juce::Colours::transparentWhite, rf.getCentreX(), rf.getY() + 28.0f, false));
                g.fillRect (rf.withHeight (28.0f));
            }
            else if (themeMode == 14)
            {
                // 🌌 01 銀河: カードを斜めに横切る星雲グロー＆微細な星屑
                g.setGradientFill (juce::ColourGradient (Palette::blue.withAlpha (0.20f), rf.getX() + rf.getWidth() * 0.25f, rf.getBottom() - 20.0f,
                                                         juce::Colours::transparentBlack, rf.getRight(), rf.getY(), true));
                g.fillRect (rf);
                g.setGradientFill (juce::ColourGradient (Palette::salmon.withAlpha (0.16f), rf.getRight() - rf.getWidth() * 0.25f, rf.getY() + 20.0f,
                                                         juce::Colours::transparentBlack, rf.getX(), rf.getBottom(), true));
                g.fillRect (rf);
            }
            else if (themeMode == 16)
            {
                // 🌅 03 朝焼け: カード下辺から立ち昇るゴールデン・サンライズグロー
                g.setGradientFill (juce::ColourGradient (juce::Colour (0x32ffbe0b), rf.getCentreX(), rf.getBottom(),
                                                         juce::Colours::transparentBlack, rf.getCentreX(), rf.getY() + rf.getHeight() * 0.35f, false));
                g.fillRect (rf);
            }
            else if (themeMode == 17)
            {
                // 🍁 04 晩秋〜初冬: 上辺の白い冬霞ミストと下辺の夕茜リフレクション
                g.setGradientFill (juce::ColourGradient (juce::Colour (0x24e0f2fe), rf.getCentreX(), rf.getY(),
                                                         juce::Colours::transparentBlack, rf.getCentreX(), rf.getY() + 42.0f, false));
                g.fillRect (rf.withHeight (42.0f));
                g.setGradientFill (juce::ColourGradient (juce::Colour (0x28e04f44), rf.getCentreX(), rf.getBottom(),
                                                         juce::Colours::transparentBlack, rf.getCentreX(), rf.getBottom() - 64.0f, false));
                g.fillRect (rf.withTop (rf.getBottom() - 64.0f));
            }
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (Palette::panel.brighter (0.05f).withMultipliedAlpha (cardAlpha), rf.getX(), rf.getY(),
                                                     Palette::panel.darker   (0.04f).withMultipliedAlpha (cardAlpha), rf.getX(), rf.getBottom(), false));
            g.fillRoundedRectangle (rf, cardRad);
        }

        // カード上部のヘッダーリボン帯 (y .. y+45)
        {
            juce::Path hdrClip;
            hdrClip.addRoundedRectangle (rf.getX() + 1.0f, rf.getY() + 1.0f, rf.getWidth() - 2.0f, 44.0f,
                                         cardRad - 1.0f, cardRad - 1.0f, true, true, false, false);
            const float hdrA = nightTheme() ? 0.24f : (isLiverStyle ? 0.28f : (isYuruStyle ? 0.22f : 0.16f));
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (hdrA), rf.getX(), rf.getY(),
                                                     accent.withAlpha (isLiverStyle ? 0.08f : 0.02f), rf.getX() + rf.getWidth() * 0.92f, rf.getY(), false));
            g.fillPath (hdrClip);
        }

        g.setColour (juce::Colours::white.withAlpha (nightTheme() ? 0.18f : 0.80f));
        g.drawHorizontalLine (r.getY() + 1, rf.getX() + 16.0f, rf.getRight() - 16.0f);

        // ♥ 2. V-LIVER MAISON DE BEAUTÉ: マトラッセ・キルティング下地＋クチュール宝飾枠＋ハートカメオ装飾
        if (isLiverStyle)
        {
            const juce::Colour goldRim  = Palette::metalMid;
            const juce::Colour goldDark = Palette::metalDark;
            const float laceY = (float) (r.getY() + 45);

            // カード内部の繊細なマトラッセ（ダイヤ・キルティング）シームライン
            {
                juce::Path bodyClip;
                bodyClip.addRoundedRectangle (rf.reduced (4.0f).withTrimmedTop (45.0f), cardRad - 3.0f);
                juce::Graphics::ScopedSaveState qs (g);
                g.reduceClipRegion (bodyClip);
                g.setColour (Palette::panelLn.withAlpha (nightTheme() ? 0.22f : 0.16f));
                const float step = 38.0f;
                for (float qx = rf.getX() - rf.getHeight(); qx < rf.getRight() + rf.getHeight(); qx += step)
                {
                    g.drawLine (qx, rf.getY() + 45.0f, qx + rf.getHeight(), rf.getBottom(), 0.9f);
                    g.drawLine (qx, rf.getBottom(), qx + rf.getHeight(), rf.getY() + 45.0f, 0.9f);
                }
            }

            // ヘッダー境界のスカラップ・レース＆クチュール宝飾バー
            g.setColour (accent.withAlpha (0.28f));
            for (float lx = rf.getX() + 14.0f; lx < rf.getRight() - 22.0f; lx += 14.0f)
                g.fillEllipse (lx, laceY - 3.5f, 14.0f, 7.0f);
            g.setGradientFill (juce::ColourGradient (goldDark, rf.getX() + 10.0f, laceY,
                                                     goldRim.brighter (0.25f), rf.getCentreX(), laceY, false));
            g.fillRect (rf.getX() + 10.0f, laceY - 1.0f, rf.getWidth() - 20.0f, 2.0f);
            // パールカボション粒ドット
            for (float px = rf.getX() + 21.0f; px < rf.getRight() - 20.0f; px += 14.0f)
            {
                g.setColour (goldDark.withAlpha (0.75f));
                g.fillEllipse (px - 2.1f, laceY - 2.1f, 4.2f, 4.2f);
                g.setColour (juce::Colour (0xfffffdf9));
                g.fillEllipse (px - 1.4f, laceY - 1.4f, 2.8f, 2.8f);
            }

            // 二重宝飾メタル＆クチュールフレーム
            g.setColour (goldRim.withAlpha (0.65f));
            g.drawRoundedRectangle (rf.reduced (3.8f), cardRad - 3.0f, 1.2f);
            g.setColour (goldDark.withAlpha (0.88f));
            g.drawRoundedRectangle (rf.reduced (0.7f), cardRad, 1.8f);

            // 四隅のコーナー宝飾クラスプ（コンパクトケースの隅金具）
            for (float cxCorner : { rf.getX() + 10.5f, rf.getRight() - 10.5f })
            {
                for (float cyCorner : { rf.getY() + 10.5f, rf.getBottom() - 10.5f })
                {
                    g.setColour (goldDark);
                    g.fillEllipse (cxCorner - 4.2f, cyCorner - 4.2f, 8.4f, 8.4f);
                    g.setColour (accent);
                    g.fillEllipse (cxCorner - 2.6f, cyCorner - 2.6f, 5.2f, 5.2f);
                    g.setColour (juce::Colours::white.withAlpha (0.90f));
                    g.fillEllipse (cxCorner - 1.1f, cyCorner - 1.1f, 2.2f, 2.2f);
                }
            }

            // 左上見出しの3D宝飾台座＆ハート・カメオエンブレム
            g.setGradientFill (juce::ColourGradient (Palette::metalHi, rf.getX() + 9.0f, rf.getY() + 14.0f,
                                                     goldDark, rf.getX() + 25.0f, rf.getY() + 32.0f, false));
            g.fillEllipse (rf.getX() + 9.0f, rf.getY() + 14.5f, 17.0f, 17.0f);
            drawHeartShape (rf.getX() + 17.5f, rf.getY() + 23.5f, 6.4f, accent);
            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.fillEllipse (rf.getX() + 14.8f, rf.getY() + 19.8f, 2.2f, 1.8f);

            // 下辺センターの宝飾メタル＆サテンリボンチャーム
            drawRibbonBow (rf.getCentreX(), rf.getBottom() - 4.2f, 8.0f, goldDark.withAlpha (0.85f));
            drawRibbonBow (rf.getCentreX(), rf.getBottom() - 4.2f, 6.5f, accent.withAlpha (0.85f));
            g.setColour (Palette::ink);
            return;
        }

        // 🍰 3. YURU-FUWA SALON DE THÉ: 焦がしキャラメル金枠＋手縫いステッチ＋フランボワーズ・マカロン装飾
        if (isYuruStyle)
        {
            const float divY = (float) (r.getY() + 45);
            // ヘッダー仕切りの手縫いステッチ破線
            g.setColour (accent.withAlpha (0.68f));
            for (float sx = rf.getX() + 16.0f; sx < rf.getRight() - 20.0f; sx += 9.0f)
                g.fillRoundedRectangle (sx, divY - 1.0f, 5.2f, 2.2f, 1.0f);

            // カード内周のミシン目ステッチ枠 (4辺の優しい破線ステッチ)
            const auto stR = rf.reduced (4.5f);
            g.setColour (accent.withAlpha (nightTheme() ? 0.42f : 0.52f));
            for (float sx = stR.getX() + 14.0f; sx < stR.getRight() - 14.0f; sx += 10.0f)
            {
                g.fillRoundedRectangle (sx, stR.getY(), 5.2f, 1.6f, 0.8f);
                g.fillRoundedRectangle (sx, stR.getBottom() - 1.6f, 5.2f, 1.6f, 0.8f);
            }
            for (float sy = stR.getY() + 14.0f; sy < stR.getBottom() - 14.0f; sy += 10.0f)
            {
                g.fillRoundedRectangle (stR.getX(), sy, 1.6f, 5.2f, 0.8f);
                g.fillRoundedRectangle (stR.getRight() - 1.6f, sy, 1.6f, 5.2f, 0.8f);
            }
            g.setColour (Palette::panelLn.darker (0.10f));
            g.drawRoundedRectangle (rf.reduced (0.7f), cardRad, 1.6f);

            // 左上見出しのベリー＆マカロン・ゴールドバッジ
            g.setColour (juce::Colour (0xffb8865c));
            g.fillEllipse (rf.getX() + 10.0f, rf.getY() + 16.0f, 15.0f, 15.0f);
            g.setColour (accent);
            g.fillEllipse (rf.getX() + 12.0f, rf.getY() + 18.0f, 11.0f, 11.0f);
            g.setColour (juce::Colours::white.withAlpha (0.88f));
            g.fillEllipse (rf.getX() + 14.2f, rf.getY() + 19.8f, 3.2f, 3.2f);
            g.setColour (Palette::ink);
            return;
        }

        // 🌿✦🌌🌊🌅🍁👑 4. BOTANICAL NATURE / AURORA / OCEAN-STAR / BRAND: エッジライト屈折クリスタルガラス＋クロノグラフ隅金具＋世界観別オーナメント
        if (isNatureAura)
        {
            const auto filCol = oceanStarTheme() ? Palette::metalHi : accent;
            // ヘッダー下部のデュアルトーン・レーザー光導波路ライン
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.88f), rf.getX() + 14.0f, (float) (r.getY() + 45),
                                                     filCol.withAlpha (0.22f), rf.getRight() - 14.0f, (float) (r.getY() + 45), false));
            g.fillRect (rf.getX() + 14.0f, (float) (r.getY() + 45), rf.getWidth() - 28.0f, 1.6f);

            // VisionOS / Leica 風の左上ディレクショナル・スペキュラエッジリム光沢
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (oceanStarTheme() ? 0.38f : 0.24f), rf.getX(), rf.getY(),
                                                      filCol.withAlpha (0.08f), rf.getRight(), rf.getBottom(), false));
            g.drawRoundedRectangle (rf.reduced (1.6f), cardRad - 1.0f, 1.1f);

            g.setColour (filCol.withAlpha (oceanStarTheme() ? 0.50f : 0.34f));
            g.drawRoundedRectangle (rf.reduced (3.8f), cardRad - 2.5f, 1.0f);
            g.setColour (oceanStarTheme() ? Palette::metalMid.withAlpha (0.85f) : Palette::panelLn.brighter (0.14f));
            g.drawRoundedRectangle (rf.reduced (0.5f), cardRad, 1.5f);

            // ★ Red Dot クロノグラフ・コーナーラグ金具（四隅のL字精密ベゼルアクセント）
            if (oceanStarTheme())
            {
                const float lugLen = 14.0f;
                const float inset  = 3.8f;
                g.setColour (accent.withAlpha (0.78f));
                // Top-Left & Top-Right
                g.drawLine (rf.getX() + inset, rf.getY() + cardRad, rf.getX() + inset, rf.getY() + cardRad + lugLen, 2.0f);
                g.drawLine (rf.getRight() - inset, rf.getY() + cardRad, rf.getRight() - inset, rf.getY() + cardRad + lugLen, 2.0f);
                // Bottom-Left & Bottom-Right
                g.drawLine (rf.getX() + inset, rf.getBottom() - cardRad - lugLen, rf.getX() + inset, rf.getBottom() - cardRad, 2.0f);
                g.drawLine (rf.getRight() - inset, rf.getBottom() - cardRad - lugLen, rf.getRight() - inset, rf.getBottom() - cardRad, 2.0f);

                // 下辺センターのオプティカル・ライトパイプバー
                const float botBarW = juce::jmin (96.0f, rf.getWidth() * 0.28f);
                g.setGradientFill (juce::ColourGradient (juce::Colours::transparentWhite, rf.getCentreX() - botBarW * 0.5f, rf.getBottom() - 2.5f,
                                                         accent.withAlpha (0.82f), rf.getCentreX(), rf.getBottom() - 2.5f, false));
                g.fillRoundedRectangle (rf.getCentreX() - botBarW * 0.5f, rf.getBottom() - 3.0f, botBarW * 0.5f, 2.0f, 1.0f);
                g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.82f), rf.getCentreX(), rf.getBottom() - 2.5f,
                                                         juce::Colours::transparentWhite, rf.getCentreX() + botBarW * 0.5f, rf.getBottom() - 2.5f, false));
                g.fillRoundedRectangle (rf.getCentreX(), rf.getBottom() - 3.0f, botBarW * 0.5f, 2.0f, 1.0f);
            }

            if (themeMode == 15)
            {
                // 🌊 02 碧海: 見出しとカード四隅にぷるんとした立体3D水滴・気泡を配置
                ThemePainter::drawWaterDroplet (g, rf.getX() + 17.0f, rf.getY() + 24.0f, 6.8f, accent, 0.92f);
                ThemePainter::drawWaterDroplet (g, rf.getRight() - 18.0f, rf.getY() + 16.0f, 5.5f, Palette::ice, 0.82f);
                ThemePainter::drawWaterDroplet (g, rf.getRight() - 30.0f, rf.getY() + 26.0f, 3.4f, Palette::green, 0.78f);
                ThemePainter::drawWaterDroplet (g, rf.getX() + 14.0f, rf.getBottom() - 14.0f, 4.8f, Palette::ice, 0.76f);
                ThemePainter::drawWaterDroplet (g, rf.getRight() - 14.0f, rf.getBottom() - 14.0f, 5.2f, accent, 0.78f);
            }
            else if (themeMode == 17)
            {
                // 🍁 04 晩秋〜初冬: 見出しと四隅に舞い散る紅葉（🍁）と初雪の結晶（❄）
                ThemePainter::drawMapleLeaf   (g, rf.getX() + 17.0f, rf.getY() + 24.0f, 8.2f, -0.35f, juce::Colour (0xffe11d48), 0.92f);
                ThemePainter::drawSnowCrystal (g, rf.getRight() - 16.0f, rf.getY() + 16.0f, 6.2f, 0.25f, juce::Colour (0xffe0f2fe), 0.85f);
                ThemePainter::drawMapleLeaf   (g, rf.getRight() - 34.0f, rf.getY() + 26.0f, 6.8f, 0.65f, juce::Colour (0xfff59e0b), 0.82f);
                ThemePainter::drawSnowCrystal (g, rf.getX() + 12.0f, rf.getBottom() - 12.0f, 5.2f, 0.0f, juce::Colour (0xffbae6fd), 0.75f);
                ThemePainter::drawMapleLeaf   (g, rf.getRight() - 14.0f, rf.getBottom() - 14.0f, 6.5f, -0.8f, juce::Colour (0xffea580c), 0.78f);
            }
            else
            {
                drawSparkle4 (rf.getX() + 17.0f, rf.getY() + 24.0f, 6.8f, accent);
                drawSparkle4 (rf.getX() + 10.0f, rf.getY() + 10.0f, 4.2f, filCol.withAlpha (0.78f));
                drawSparkle4 (rf.getRight() - 10.0f, rf.getY() + 10.0f, 4.2f, filCol.withAlpha (0.78f));
                drawSparkle4 (rf.getX() + 10.0f, rf.getBottom() - 10.0f, 4.2f, filCol.withAlpha (0.78f));
                drawSparkle4 (rf.getRight() - 10.0f, rf.getBottom() - 10.0f, 4.2f, filCol.withAlpha (0.78f));
            }
            g.setColour (Palette::ink);
            return;
        }

        // 🎛 5. STUDIO RACK / CLASSIC: ヘアライン金属＆真鍮ラックマウント・コンソールカード
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.55f), rf.getX() + 14.0f, (float) (r.getY() + 45),
                                                 accent.withAlpha (0.08f), rf.getRight() - 14.0f, (float) (r.getY() + 45), false));
        g.fillRect (rf.getX() + 14.0f, (float) (r.getY() + 45), rf.getWidth() - 28.0f, 1.5f);

        g.setColour (juce::Colours::white.withAlpha (nightTheme() ? 0.05f : 0.22f));
        g.drawRoundedRectangle (rf.reduced (1.5f), 15.0f, 1.0f);
        g.setColour (Palette::panelLn.brighter (0.08f));
        g.drawRoundedRectangle (rf.reduced (0.5f), 16.0f, 1.2f);

        g.setColour (accent.withAlpha (0.32f));
        g.fillRoundedRectangle (rf.getX() + 13.5f, rf.getY() + 11.5f, 7.0f, 25.0f, 3.5f);
        g.setGradientFill (juce::ColourGradient (accent.brighter (0.30f), rf.getX() + 15.0f, rf.getY() + 13.0f,
                                                 accent.darker   (0.15f), rf.getX() + 15.0f, rf.getY() + 35.0f, false));
        g.fillRoundedRectangle (rf.getX() + 15.0f, rf.getY() + 13.0f, 4.5f, 22.0f, 2.2f);

        if (isStudioStyle)
        {
            // 🎛 STUDIO RACK: アノダイズド・ヘアライン溝＋3D真鍮/クローム・ラックボルト
            g.setColour (Palette::inkSoft.withAlpha (0.06f));
            for (int hy = r.getY() + 8; hy < r.getY() + 42; hy += 4)
                g.drawHorizontalLine (hy, rf.getX() + 26.0f, rf.getRight() - 26.0f);

            auto drawRackBolt = [&] (float bx, float by)
            {
                g.setColour (juce::Colours::black.withAlpha (0.42f));
                g.fillEllipse (bx - 5.2f, by - 4.2f, 10.4f, 10.4f);
                g.setGradientFill (juce::ColourGradient (Palette::panel2.brighter (0.30f), bx - 4.0f, by - 4.0f,
                                                         Palette::panelLn.darker (0.45f),  bx + 4.0f, by + 4.0f, false));
                g.fillEllipse (bx - 5.0f, by - 5.0f, 10.0f, 10.0f);
                g.setColour (accent.withAlpha (0.55f));
                g.drawEllipse (bx - 5.0f, by - 5.0f, 10.0f, 10.0f, 1.1f);
                g.setColour (Palette::inkSoft.withAlpha (0.75f));
                g.drawLine (bx - 2.5f, by, bx + 2.5f, by, 1.2f);
                g.drawLine (bx, by - 2.5f, bx, by + 2.5f, 1.2f);
            };
            drawRackBolt (rf.getX() + 9.5f, rf.getY() + 9.5f);
            drawRackBolt (rf.getRight() - 9.5f, rf.getY() + 9.5f);
            drawRackBolt (rf.getX() + 9.5f, rf.getBottom() - 9.5f);
            drawRackBolt (rf.getRight() - 9.5f, rf.getBottom() - 9.5f);
        }
        g.setColour (Palette::ink);
    };

    card (overviewMonitor, isLiverStyle ? Palette::salmon : (isYuruStyle ? Palette::yellow : Palette::ice));
    g.setColour (Palette::ink);
    const bool hasSwatches = ! coutureSwatchRects[0].isEmpty();
    const juce::String monTitle = (accessMode == 1)
        ? language->T ("◉ 声の4方向バランス調整 ＆ 周波数グラフ（くっきり・大文字）", "◉ 4-WAY VOICE BALANCE & EQ SPECTRUM (HIGH CONTRAST)")
        : (accessMode == 2)
        ? language->T ("★ 丸を動かすだけで声のキャラクターを直感調整（3ステップガイド）", "★ DRAG THE ORB TO SHAPE YOUR VOICE (3-STEP GUIDE)")
        : (accessMode == 3)
        ? language->T ("● 2軸パラメータ座標パッド (X:倍音/高域 ・ Y:近接/空間幅) ＆ 周波数特性", "● 2D PARAMETER MATRIX & FREQUENCY RESPONSE")
        : (overviewLayoutMode == 3)
        ? language->T ("音の輪郭", "SPECTRUM")
        : isLiverStyle
        ? (hasSwatches ? language->T ("♥ クチュール・パクト", "COUTURE COMPACT")
                       : language->T ("♥ クチュール・クッションパクト ＆ 歌声ステージRTA", "COUTURE CUSHION COMPACT & STAGE RTA"))
        : isYuruStyle
        ? language->T ("★ パティスリー・ガラスドーム ＆ 歌声カフェRTA", "PATISSERIE GLASS DOME & SPECTRUM")
        : oceanStarTheme()
        ? (overviewLayoutMode == 2
            ? (themeMode == 14 ? language->T ("🌌 銀河天球", "GALAXY ORB")
             : themeMode == 15 ? language->T ("🌊 碧海水球", "AQUA ORB")
             : themeMode == 16 ? language->T ("🌅 朝焼天球", "DAWN ORB")
             : themeMode == 17 ? language->T ("🍁 晩秋水鏡", "AUTUMN ORB")
                               : language->T ("👑 蒼劇天球", "OPERA ORB"))
            : (themeMode == 14 ? language->T ("🌌 天の川銀河天球", "GALACTIC MILKY WAY ORB")
             : themeMode == 15 ? language->T ("🌊 瑞々しい碧海水球", "LUSH AQUA WATER ORB")
             : themeMode == 16 ? language->T ("🌅 朝焼けの暁天球", "RADIANT DAWN SUNRISE ORB")
             : themeMode == 17 ? language->T ("🍁 晩秋と初雪の水鏡", "AUTUMN & FIRST SNOW MIRROR")
                               : language->T ("👑 蒼海オペラ天球", "GRAND OPERA SAPPHIRE ORB")))
        : (themeMode == 2)
        ? language->T ("✦ 四季の水紋オーブ ＆ 音の輪郭モニター", "BOTANICAL WATER ORB & SPECTRUM")
        : (themeMode == 4)
        ? language->T ("✦ 星屑プラネタリウム天球儀 ＆ 輪郭モニター", "CELESTIAL ASTROLABE & SPECTRUM")
        : isStudioStyle
        ? language->T ("◆ アナログ位相ゴニオメーター ＆ スペクトラム RTA", "ANALOG GONIOMETER & RTA")
        : language->T ("ARGON 3D 回転トラッカーボール ＆ 4軸トリガー機関", "ARGON 3D TRACKBALL & 4-AXIS TRIGGER");
    auto monTitleRect = overviewMonitor.reduced (28, 8).withHeight (28);
    if (hasSwatches)
        monTitleRect.setRight (juce::jmax (monTitleRect.getX() + 60, coutureSwatchRects[0].getX() - 6));
    fitText (g, monTitle, monTitleRect, juce::Justification::centredLeft, (accessMode == 1) ? 17.5f : 16.0f, true);

    // ---- 💄 クチュール・コスメ / 🌌 風景・季節コレクション 5色ワンタッチ・ジュエルスウォッチピル描画 ----
    if (hasSwatches)
    {
        struct SwatchDef { juce::uint32 bgTop, bgBot, gem, rim, ink; const char* ja; const char* jaShort; const char* en; const char* enShort; };
        static const SwatchDef kSw[5] = {
            { 0xfffff4f7, 0xfff8dce8, 0xffd63264, 0xffd4967a, 0xff44182b, "01ロゼ", "ロゼ", "01RS", "RS" },
            { 0xfff6f5ff, 0xffe2dff8, 0xff6b52dc, 0xff9aa6c6, 0xff262042, "02氷紫", "氷紫", "02LI", "LI" },
            { 0xff2f1f34, 0xff1c131e, 0xfff43868, 0xffc88ca8, 0xfffcecf4, "03黒姫", "黒姫", "03NR", "NR" },
            { 0xfff2fcf9, 0xffd4efe6, 0xff229a82, 0xffcfa262, 0xff163830, "04翡翠", "翡翠", "04MT", "MT" },
            { 0xfffff8f1, 0xfffce0c8, 0xffe66b30, 0xffda9c48, 0xff462416, "05白桃", "白桃", "05PC", "PC" }
        };
        static const SwatchDef kOceanSw[5] = {
            { 0xff181648, 0xff070a24, 0xfffde047, 0xffa855f7, 0xfffffaed, "01銀河", "銀河", "01MW", "MW" },
            { 0xff0077b6, 0xff032b44, 0xff00f5d4, 0xff72efdd, 0xfff2feff, "02碧海", "碧海", "02AQ", "AQ" },
            { 0xff5a1e48, 0xff2b1432, 0xffffbe0b, 0xffff6b8b, 0xfffff8f2, "03朝焼", "朝焼", "03DW", "DW" },
            { 0xff2b2236, 0xff161828, 0xffe11d48, 0xffbae6fd, 0xfff9f3ec, "04晩秋", "晩秋", "04AU", "AU" },
            { 0xff0d2864, 0xff05102e, 0xfff2c94c, 0xff3b82f6, 0xfffff8eb, "05蒼劇", "蒼劇", "05OP", "OP" }
        };
        const bool isCoutureSw = liverTheme() || (overviewLayoutMode == 1 && ! oceanStarTheme() && themeMode != 2 && themeMode != 4);
        for (int c = 0; c < 5; ++c)
        {
            const auto sr = coutureSwatchRects[(size_t) c];
            const auto srf = sr.toFloat();
            const float sRad = srf.getHeight() * 0.5f;
            const bool active = isCoutureSw ? (themeMode == Palette::themeOfCouture (c))
                                            : (themeMode == Palette::themeOfOceanStar (c));
            const auto& d = isCoutureSw ? kSw[c] : kOceanSw[c];

            if (! liteMode)
            {
                g.setColour (juce::Colour (d.gem).withAlpha (active ? 0.35f : 0.14f));
                g.fillRoundedRectangle (srf.translated (0.0f, 1.5f), sRad);
            }
            g.setGradientFill (juce::ColourGradient (juce::Colour (d.bgTop), srf.getX(), srf.getY(),
                                                     juce::Colour (d.bgBot), srf.getX(), srf.getBottom(), false));
            g.fillRoundedRectangle (srf, sRad);

            // 左端のジュエル・ピグメント粒
            const float gx = srf.getX() + 7.5f, gy = srf.getCentreY();
            g.setColour (juce::Colour (d.rim));
            g.fillEllipse (gx - 4.0f, gy - 4.0f, 8.0f, 8.0f);
            g.setColour (juce::Colour (d.gem));
            g.fillEllipse (gx - 2.9f, gy - 2.9f, 5.8f, 5.8f);
            g.setColour (juce::Colours::white.withAlpha (0.90f));
            g.fillEllipse (gx - 1.3f, gy - 1.5f, 1.9f, 1.5f);

            g.setColour (active ? juce::Colour (d.gem) : juce::Colour (d.rim));
            g.drawRoundedRectangle (srf.reduced (0.6f), sRad, active ? 2.0f : 1.2f);

            g.setColour (juce::Colour (d.ink));
            const bool compactSw = (sr.getWidth() < 52);
            const char* swTxt = language->english ? (compactSw ? d.enShort : d.en)
                                                  : (compactSw ? d.jaShort : d.ja);
            fitText (g, juce::String::fromUTF8 (swTxt),
                     sr.withTrimmedLeft (11).reduced (1, 0), juce::Justification::centred, 14.0f, true);
        }
    }

    // ---- ★ 中央3D球体コントローラー（ペルソナ別に魔法コンパクト／スノードーム／天球儀／位相スコープ／SFトラックボールを描き分け） ----
    const float vVal = processor.apvts.getRawParameterValue ("argon_velvet")->load();
    const float fVal = processor.apvts.getRawParameterValue ("argon_focus")->load();
    const float sVal = processor.apvts.getRawParameterValue ("argon_silk")->load();
    const float hVal = processor.apvts.getRawParameterValue ("argon_halo")->load();
    const float vAct = processor.getArgonVelvetMeter();
    const float fAct = processor.getArgonFocusMeter();
    const float sAct = processor.getArgonSilkMeter();
    const float hAct = processor.getArgonHaloMeter();

    if (! argonPadRect.isEmpty())
    {
        const auto padF = argonPadRect.toFloat();
        const auto sphereZone = padF.reduced (8.0f, 22.0f);
        const float cx = sphereZone.getCentreX(), cy = sphereZone.getCentreY();
        const float ballR   = juce::jmin (sphereZone.getWidth(), sphereZone.getHeight()) * 0.44f;
        const float socketR = ballR + 5.5f;

        const float normDx = (argonOrbX - 0.5f) * 2.0f; // -1 .. +1
        const float normDy = (argonOrbY - 0.5f) * 2.0f; // -1 .. +1
        const float dist2D = std::sqrt (normDx * normDx + normDy * normDy);
        const float clampScale = (dist2D > 0.82f) ? (0.82f / dist2D) : 1.0f;
        const float ox = cx + normDx * clampScale * ballR;
        const float oy = cy + normDy * clampScale * ballR;

        if (liteMode)
        {
            g.setColour (pastelTheme() ? Palette::panel2 : Palette::bgTop.darker (0.15f));
            g.fillRoundedRectangle (padF, 12.0f);
            g.setColour (Palette::panelLn);
            g.drawEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f, 1.5f);
            g.setColour (Palette::ice);
            g.drawLine (cx, cy, ox, oy, 2.0f);
            g.setColour (isLiverStyle ? Palette::salmon : Palette::yellow);
            g.fillEllipse (ox - 9.0f, oy - 9.0f, 18.0f, 18.0f);
        }
        else if (accessMode > 0)
        {
            // ★ ユニバーサル・アクセシビリティ 2Dパッド描画（反射・眩しさゼロ、明快な4方向ガイド＆大型ターゲット）
            g.setColour (Palette::bgTop);
            g.fillRoundedRectangle (padF, 12.0f);
            g.setColour (Palette::panelLn);
            g.drawRoundedRectangle (padF.reduced (0.8f), 12.0f, (accessMode == 1) ? 2.2f : 1.4f);

            // 同心円リファレンスリングと十字軸
            g.setColour (Palette::panel2);
            g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);
            g.setColour (Palette::panelLn.withAlpha ((accessMode == 1) ? 0.85f : 0.55f));
            g.drawEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f, (accessMode == 1) ? 2.2f : 1.4f);
            g.drawEllipse (cx - ballR * 0.55f, cy - ballR * 0.55f, ballR * 1.1f, ballR * 1.1f, 1.1f);
            g.drawLine (cx - ballR, cy, cx + ballR, cy, (accessMode == 1) ? 1.6f : 1.1f);
            g.drawLine (cx, cy - ballR, cx, cy + ballR, (accessMode == 1) ? 1.6f : 1.1f);

            // 中心から現在位置への太いベクトルラインと大型ターゲットマーカー
            g.setColour (Palette::ice);
            g.drawLine (cx, cy, ox, oy, (accessMode == 1) ? 3.6f : 2.2f);
            const float puckR = (accessMode == 1) ? 13.0f : 10.5f;
            g.setColour ((accessMode == 3) ? Palette::green : Palette::yellow);
            g.fillEllipse (ox - puckR, oy - puckR, puckR * 2.0f, puckR * 2.0f);
            g.setColour ((accessMode == 1) ? juce::Colours::white : Palette::bgTop);
            g.drawEllipse (ox - puckR, oy - puckR, puckR * 2.0f, puckR * 2.0f, (accessMode == 1) ? 2.6f : 1.8f);
            g.setColour (Palette::bgTop);
            g.fillEllipse (ox - 3.5f, oy - 3.5f, 7.0f, 7.0f);

            // 上下左右の4方向ラベル（老眼モードは特大・高コントラスト、ASDモードは文字通りの機能名）
            g.setColour (Palette::ink);
            const float dirFont = (accessMode == 1) ? 15.0f : 13.5f;
            fitText (g, (accessMode == 3) ? language->T ("▲ Y+: 近接・高密度", "▲ UP: NEAR")
                                          : language->T ("▲ 上: 近い声・耳元", "▲ UP: CLOSE"),
                     juce::Rectangle<int> ((int) padF.getX() + 8, (int) padF.getY() + 2, (int) padF.getWidth() - 16, 20),
                     juce::Justification::centred, dirFont, true);
            fitText (g, (accessMode == 3) ? language->T ("▼ Y-: 左右空間拡大", "▼ DOWN: WIDTH")
                                          : language->T ("▼ 下: 立体的に広がる", "▼ DOWN: WIDE"),
                     juce::Rectangle<int> ((int) padF.getX() + 8, (int) padF.getBottom() - 22, (int) padF.getWidth() - 16, 20),
                     juce::Justification::centred, dirFont, true);
            g.setColour (Palette::salmon);
            fitText (g, (accessMode == 3) ? language->T ("◀倍音", "◀WARM") : language->T ("◀温かさ", "◀WARM"),
                     juce::Rectangle<int> ((int) padF.getX() + 4, (int) cy - 11, 68, 22),
                     juce::Justification::centredLeft, dirFont, true);
            g.setColour (Palette::ice);
            fitText (g, (accessMode == 3) ? language->T ("高域▶", "AIR▶") : language->T ("明るさ▶", "AIR▶"),
                     juce::Rectangle<int> ((int) padF.getRight() - 72, (int) cy - 11, 68, 22),
                     juce::Justification::centredRight, dirFont, true);
        }
        else if (isLiverStyle)
        {
            // =========================================================================
            // ♥ PERSONA A (V-LIVER): クチュール・クッションパクト（5種の金属ヒンジ＆ブリリアント鏡面球体）
            // =========================================================================
            // 1. ポーセリン／ベルベット・クッション台座＋貴金属二重額縁
            g.setGradientFill (juce::ColourGradient (
                (themeMode == 11) ? Palette::panel2.brighter (0.06f) : Palette::panel.brighter (0.05f), padF.getX(), padF.getY(),
                (themeMode == 11) ? Palette::panel.darker (0.08f)    : Palette::panel2,                 padF.getRight(), padF.getBottom(), false));
            g.fillRoundedRectangle (padF, 18.0f);
            // 内側のサテン・チーク陰影グロー
            g.setGradientFill (juce::ColourGradient (Palette::salmon.withAlpha (0.24f), cx, cy,
                                                     juce::Colours::transparentWhite, padF.getX(), padF.getY(), true));
            g.fillRoundedRectangle (padF.reduced (2.0f), 16.5f);
            g.setColour (Palette::metalMid);
            g.drawRoundedRectangle (padF.reduced (0.7f), 18.0f, 1.6f);
            g.setColour ((themeMode == 11) ? Palette::metalHi.withAlpha (0.42f) : juce::Colours::white.withAlpha (0.90f));
            g.drawRoundedRectangle (padF.reduced (2.8f), 16.0f, 1.1f);

            // 2. コンパクト外周のドロップシャドウ＆上部の貴金属バレルヒンジ＋下部の宝石留め具（クラスプ）
            g.setColour (Palette::mirrorBot.darker (0.35f).withAlpha (0.26f));
            g.fillEllipse (cx - socketR - 2.5f, cy - socketR + 3.0f, (socketR + 2.5f) * 2.0f, (socketR + 2.5f) * 2.0f);

            // 上部：3Dクチュール貴金属・コンパクトヒンジ（蝶番）
            const juce::Rectangle<float> hingeR (cx - 22.0f, cy - socketR - 5.5f, 44.0f, 9.0f);
            g.setGradientFill (juce::ColourGradient (Palette::metalHi,  hingeR.getX(), hingeR.getY(),
                                                     Palette::metalMid, hingeR.getRight(), hingeR.getBottom(), false));
            g.fillRoundedRectangle (hingeR, 4.5f);
            g.setColour (Palette::metalDark);
            g.drawRoundedRectangle (hingeR, 4.5f, 1.1f);
            g.drawLine (cx - 10.0f, hingeR.getY() + 1.0f, cx - 10.0f, hingeR.getBottom() - 1.0f, 1.0f);
            g.drawLine (cx + 10.0f, hingeR.getY() + 1.0f, cx + 10.0f, hingeR.getBottom() - 1.0f, 1.0f);

            // 下部：3Dクチュール貴金属＆ハート宝石・プッシュクラスプ（留め具）
            const juce::Rectangle<float> claspR (cx - 16.0f, cy + socketR - 3.5f, 32.0f, 8.5f);
            g.setGradientFill (juce::ColourGradient (Palette::metalHi,  claspR.getX(), claspR.getY(),
                                                     Palette::metalMid, claspR.getRight(), claspR.getBottom(), false));
            g.fillRoundedRectangle (claspR, 4.2f);
            g.setColour (Palette::metalDark);
            g.drawRoundedRectangle (claspR, 4.2f, 1.1f);
            drawHeartShape (cx, claspR.getCentreY(), 5.2f, Palette::blue);

            // 3. 36刻みフルーテッド・貴金属ベゼル (socketR)
            g.setGradientFill (juce::ColourGradient (Palette::metalHi,  cx - socketR, cy - socketR,
                                                     Palette::metalMid, cx + socketR, cy + socketR, false));
            g.fillEllipse (cx - socketR, cy - socketR, socketR * 2.0f, socketR * 2.0f);
            for (int p = 0; p < 36; ++p)
            {
                const float pa = (float) p * juce::MathConstants<float>::twoPi / 36.0f;
                const float r0 = ballR + 0.8f;
                const float r1 = socketR - 0.8f;
                g.setColour ((p % 2 == 0) ? Palette::metalHi.withAlpha (0.58f) : Palette::metalDark.withAlpha (0.45f));
                g.drawLine (cx + r0 * std::sin (pa), cy - r0 * std::cos (pa),
                            cx + r1 * std::sin (pa), cy - r1 * std::cos (pa), 1.3f);
            }
            g.setColour (Palette::metalDark);
            g.drawEllipse (cx - socketR, cy - socketR, socketR * 2.0f, socketR * 2.0f, 1.4f);

            // ベゼル上の12粒のベビーパール＆カボション宝石装飾
            for (int j = 0; j < 12; ++j)
            {
                const float ja = (float) j * juce::MathConstants<float>::twoPi / 12.0f;
                const float jx = cx + (socketR - 2.8f) * std::sin (ja);
                const float jy = cy - (socketR - 2.8f) * std::cos (ja);
                const bool isGem = (j % 3 == 0);
                g.setColour (isGem ? Palette::blue : juce::Colour (0xfffff9fb));
                g.fillEllipse (jx - 2.4f, jy - 2.4f, 4.8f, 4.8f);
                g.setColour (Palette::metalDark.withAlpha (0.65f));
                g.drawEllipse (jx - 2.4f, jy - 2.4f, 4.8f, 4.8f, 0.8f);
                g.setColour (juce::Colours::white.withAlpha (0.92f));
                g.fillEllipse (jx - 1.1f, jy - 1.2f, 1.8f, 1.5f);
            }

            // 4. カラバリ別ブリリアントカット鏡面球体（白の天使羽と貴金属フィリグリーが鮮烈に映える深み）
            {
                juce::Path sphereClip;
                sphereClip.addEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);
                juce::Graphics::ScopedSaveState clipState (g);
                g.reduceClipRegion (sphereClip);

                // カラバリ別の鏡面ドーム球面グラデーション (mirrorTop -> mirrorBot)
                g.setGradientFill (juce::ColourGradient (
                    Palette::mirrorTop, ox * 0.55f + cx * 0.45f, oy * 0.55f + cy * 0.45f,
                    Palette::mirrorBot, cx + (cx - ox) * 0.85f, cy + (cy - oy) * 0.85f, true));
                g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);

                // 16分割ブリリアント・宝石ファセット光条
                for (int f = 0; f < 16; ++f)
                {
                    const float a0 = (float) f * juce::MathConstants<float>::twoPi / 16.0f;
                    const float a1 = a0 + juce::MathConstants<float>::twoPi / 32.0f;
                    juce::Path facet;
                    facet.startNewSubPath (ox * 0.35f + cx * 0.65f, oy * 0.35f + cy * 0.65f);
                    facet.lineTo (cx + std::cos (a0) * ballR * 1.15f, cy + std::sin (a0) * ballR * 1.15f);
                    facet.lineTo (cx + std::cos (a1) * ballR * 1.15f, cy + std::sin (a1) * ballR * 1.15f);
                    facet.closeSubPath();
                    g.setColour ((f % 2 == 0) ? juce::Colours::white.withAlpha (0.09f)
                                              : Palette::mirrorBot.darker (0.45f).withAlpha (0.14f));
                    g.fillPath (facet);
                }

                // 4象限のクチュール・ピグメントオーラ
                g.setGradientFill (juce::ColourGradient (Palette::salmon.withAlpha (0.34f + vVal * 0.003f), cx - ballR * 0.65f, cy,
                                                         juce::Colours::transparentBlack, cx + ballR * 0.2f, cy, true));
                g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);
                g.setGradientFill (juce::ColourGradient (Palette::ice.withAlpha (0.34f + sVal * 0.003f), cx + ballR * 0.65f, cy,
                                                         juce::Colours::transparentBlack, cx - ballR * 0.2f, cy, true));
                g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);
                g.setGradientFill (juce::ColourGradient (Palette::metalHi.withAlpha (0.30f + fVal * 0.003f), cx, cy - ballR * 0.65f,
                                                         juce::Colours::transparentBlack, cx, cy + ballR * 0.2f, true));
                g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);
                g.setGradientFill (juce::ColourGradient (Palette::blue.withAlpha (0.34f + hVal * 0.003f), cx, cy + ballR * 0.65f,
                                                         juce::Colours::transparentBlack, cx, cy - ballR * 0.2f, true));
                g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);

                // 3D回転する貴金属フィリグリー（金線／銀線細工）天球メッシュ
                const float rotY = (argonOrbX - 0.5f) * 2.35f - 0.30f;
                const float rotX = -(argonOrbY - 0.5f) * 2.35f + 0.34f;
                const float cosY = std::cos (rotY), sinY = std::sin (rotY);
                const float cosX = std::cos (rotX), sinX = std::sin (rotX);
                auto project3D = [&] (float x0, float y0, float z0, float& sx, float& sy, float& sz)
                {
                    const float x1 =  x0 * cosY + z0 * sinY;
                    const float z1 = -x0 * sinY + z0 * cosY;
                    const float y2 =  y0 * cosX - z1 * sinX;
                    const float z2 =  y0 * sinX + z1 * cosX;
                    sx = cx + x1 * ballR;
                    sy = cy + y2 * ballR;
                    sz = z2;
                };

                for (int latIdx = -2; latIdx <= 2; ++latIdx)
                {
                    const float lat = (float) latIdx * 0.48f;
                    const float rLat = std::cos (lat), y0 = std::sin (lat);
                    const bool isEq = (latIdx == 0);
                    juce::Path latPath;
                    bool penDown = false;
                    for (int step = 0; step <= 36; ++step)
                    {
                        const float lon = -juce::MathConstants<float>::pi + (float) step * juce::MathConstants<float>::twoPi / 36.0f;
                        float sx, sy, sz;
                        project3D (rLat * std::sin (lon), y0, rLat * std::cos (lon), sx, sy, sz);
                        if (sz > -0.05f)
                        {
                            if (! penDown) { latPath.startNewSubPath (sx, sy); penDown = true; }
                            else           { latPath.lineTo (sx, sy); }
                        }
                        else penDown = false;
                    }
                    g.setColour (isEq ? Palette::metalHi.withAlpha (0.76f) : Palette::mirrorTop.brighter (0.45f).withAlpha (0.36f));
                    g.strokePath (latPath, juce::PathStrokeType (isEq ? 1.7f : 1.0f));
                }
                for (int lonIdx = -3; lonIdx <= 3; ++lonIdx)
                {
                    const float lon = (float) lonIdx * 0.48f;
                    const float sinLon = std::sin (lon), cosLon = std::cos (lon);
                    const bool isPrime = (lonIdx == 0);
                    juce::Path lonPath;
                    bool penDown = false;
                    for (int step = 0; step <= 32; ++step)
                    {
                        const float lat = -juce::MathConstants<float>::halfPi + (float) step * juce::MathConstants<float>::pi / 32.0f;
                        const float rLat = std::cos (lat);
                        float sx, sy, sz;
                        project3D (rLat * sinLon, std::sin (lat), rLat * cosLon, sx, sy, sz);
                        if (sz > -0.05f)
                        {
                            if (! penDown) { lonPath.startNewSubPath (sx, sy); penDown = true; }
                            else           { lonPath.lineTo (sx, sy); }
                        }
                        else penDown = false;
                    }
                    g.setColour (isPrime ? Palette::metalHi.withAlpha (0.76f) : Palette::mirrorTop.brighter (0.45f).withAlpha (0.34f));
                    g.strokePath (lonPath, juce::PathStrokeType (isPrime ? 1.7f : 1.0f));
                }

                // 操作極 (ox, oy) の貴金属カメオ光輪＋4つのジュエル・スターサテライト
                const float inLv = juce::jlimit (0.0f, 1.0f, processor.getInputLevel() * 3.5f);
                const float pulseR = 15.0f + (vAct + fAct + sAct + hAct + inLv) * 10.0f;
                g.setGradientFill (juce::ColourGradient (Palette::metalHi.withAlpha (0.55f), ox, oy,
                                                         juce::Colours::transparentWhite, ox + pulseR * 1.7f, oy, true));
                g.fillEllipse (ox - pulseR * 1.7f, oy - pulseR * 1.7f, pulseR * 3.4f, pulseR * 3.4f);
                g.setColour (Palette::metalHi.withAlpha (0.85f));
                g.drawEllipse (ox - pulseR, oy - pulseR, pulseR * 2.0f, pulseR * 2.0f, 1.5f);

                const float satVals[4] = { vVal, fVal, sVal, hVal };
                const float satActs[4] = { vAct, fAct, sAct, hAct };
                const juce::Colour satCols[4] = {
                    Palette::salmon.brighter (0.42f), Palette::metalHi,
                    Palette::ice.brighter (0.42f),    Palette::blue.brighter (0.42f)
                };
                const float baseAngles[4] = { -2.45f, -1.57f, -0.55f, 1.57f };
                for (int s = 0; s < 4; ++s)
                {
                    const float satDist = 14.0f + satVals[s] * 0.13f + satActs[s] * 7.5f;
                    const float satAng  = baseAngles[s] + satActs[s] * 1.2f;
                    const float sx = ox + std::cos (satAng) * satDist;
                    const float sy = oy + std::sin (satAng) * satDist;
                    g.setColour (Palette::metalHi.withAlpha (0.82f));
                    g.drawLine (ox, oy, sx, sy, 1.4f);
                    drawSparkle4 (sx, sy, 6.2f, satCols[s]);
                }

                // ★ ポインター本体: 純白エンジェルウィング（貴金属縁）付き 3D ハートジュエル (`♥`)
                g.setColour (Palette::mirrorBot.darker (0.55f).withAlpha (0.42f));
                g.fillEllipse (ox - 19.5f, oy - 4.0f, 12.0f, 7.0f);
                g.fillEllipse (ox + 7.5f,  oy - 4.0f, 12.0f, 7.0f);
                g.setColour (juce::Colours::white.withAlpha (0.98f));
                g.fillEllipse (ox - 19.0f, oy - 5.8f, 11.5f, 6.4f);
                g.fillEllipse (ox + 7.5f,  oy - 5.8f, 11.5f, 6.4f);
                g.setColour (Palette::metalMid);
                g.drawEllipse (ox - 19.0f, oy - 5.8f, 11.5f, 6.4f, 1.2f);
                g.drawEllipse (ox + 7.5f,  oy - 5.8f, 11.5f, 6.4f, 1.2f);

                drawHeartShape (ox, oy + 1.4f, 13.0f, Palette::metalHi);                  // 貴金属台座
                drawHeartShape (ox, oy,        11.0f, Palette::blue);                     // ディープ宝石コア
                drawHeartShape (ox, oy - 0.6f, 7.2f,  Palette::salmon.brighter (0.28f));  // ブリリアント内芯
                g.setColour (juce::Colours::white.withAlpha (0.95f));
                g.fillEllipse (ox - 4.6f, oy - 4.8f, 3.8f, 2.8f);

                // クリスタル鏡面ドーム上部のハイグロス反射ハイライト＆内縁ビネット
                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.52f), cx - ballR * 0.25f, cy - ballR * 0.92f,
                                                         juce::Colours::transparentWhite,        cx - ballR * 0.10f, cy - ballR * 0.12f, false));
                g.fillEllipse (cx - ballR * 0.76f, cy - ballR * 0.90f, ballR * 1.52f, ballR * 0.80f);
                g.setColour (Palette::mirrorBot.darker (0.55f).withAlpha (0.42f));
                g.drawEllipse (cx - ballR + 1.0f, cy - ballR + 1.0f, (ballR - 1.0f) * 2.0f, (ballR - 1.0f) * 2.0f, 2.8f);
            }
            g.setColour (Palette::metalMid);
            g.drawEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f, 1.8f);
        }
        else if (isYuruStyle)
        {
            // =========================================================================
            // 🍰 PERSONA B (YURU-FUWA PATISSERIE): 焦がしキャラメル＆ベリー・パティスリーガラスドーム
            // =========================================================================
            g.setGradientFill (juce::ColourGradient (Palette::panel.brighter (0.04f), padF.getX(), padF.getY(),
                                                     Palette::panel2.darker (0.06f), padF.getRight(), padF.getBottom(), false));
            g.fillRoundedRectangle (padF, 18.0f);
            g.setColour (Palette::panelLn);
            g.drawRoundedRectangle (padF.reduced (0.6f), 18.0f, 1.5f);
            // パティスリー・ゴールド箔ステッチ内枠
            g.setColour (Palette::yellow.withAlpha (0.65f));
            for (float sx = padF.getX() + 14.0f; sx < padF.getRight() - 14.0f; sx += 10.0f)
            {
                g.fillRoundedRectangle (sx, padF.getY() + 3.5f, 5.0f, 1.6f, 0.8f);
                g.fillRoundedRectangle (sx, padF.getBottom() - 5.0f, 5.0f, 1.6f, 0.8f);
            }

            // 外周の20パフ・生クリーム絞り飾りリム（カカオキャラメル縁取り付きで白飛び防止）
            for (int p = 0; p < 20; ++p)
            {
                const float pa = (float) p * juce::MathConstants<float>::twoPi / 20.0f;
                const float wx = cx + (socketR + 2.0f) * std::sin (pa);
                const float wy = cy - (socketR + 2.0f) * std::cos (pa);
                g.setColour (nightTheme() ? Palette::panel.brighter (0.22f) : juce::Colour (0xfffff7ee));
                g.fillEllipse (wx - 6.5f, wy - 6.5f, 13.0f, 13.0f);
                g.setColour (Palette::panelLn.withAlpha (0.65f));
                g.drawEllipse (wx - 6.5f, wy - 6.5f, 13.0f, 13.0f, 1.0f);
            }
            // 焦がしキャラメル・ビスケット＆フランボワーズのソケットリング
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2c288), cx - socketR, cy - socketR,
                                                     juce::Colour (0xffb86f42), cx + socketR, cy + socketR, false));
            g.fillEllipse (cx - socketR, cy - socketR, socketR * 2.0f, socketR * 2.0f);
            g.setColour (juce::Colour (0xff8c4f2b));
            g.drawEllipse (cx - socketR, cy - socketR, socketR * 2.0f, socketR * 2.0f, 1.4f);

            // カラフルなドラジェ（アラザン糖衣）粒
            for (int sp = 0; sp < 12; ++sp)
            {
                const float sa = (float) sp * juce::MathConstants<float>::twoPi / 12.0f;
                const float sx = cx + (socketR - 2.6f) * std::sin (sa);
                const float sy = cy - (socketR - 2.6f) * std::cos (sa);
                const juce::Colour sc = (sp % 3 == 0) ? Palette::salmon : (sp % 3 == 1) ? Palette::green : Palette::blue;
                g.setColour (sc);
                g.fillEllipse (sx - 2.5f, sy - 2.5f, 5.0f, 5.0f);
                g.setColour (juce::Colours::white.withAlpha (0.88f));
                g.fillEllipse (sx - 1.1f, sy - 1.2f, 1.8f, 1.5f);
            }

            // パティスリー・ガラスドーム球体内部（ベリーショコラ＆キャラメルカスタードの深み）
            {
                juce::Path sphereClip;
                sphereClip.addEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);
                juce::Graphics::ScopedSaveState clipState (g);
                g.reduceClipRegion (sphereClip);

                const auto domeTop = nightTheme() ? juce::Colour (0xff3d2638) : juce::Colour (0xfffbe4d8);
                const auto domeBot = nightTheme() ? juce::Colour (0xff1e131d) : juce::Colour (0xffc97a88);
                g.setGradientFill (juce::ColourGradient (domeTop, ox * 0.5f + cx * 0.5f, oy * 0.5f + cy * 0.5f,
                                                         domeBot, cx + (cx - ox) * 0.85f, cy + (cy - oy) * 0.85f, true));
                g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);

                // パティスリー・フルーツソースの4象限ブルーム
                const juce::Colour bloomCols[4] = { Palette::salmon, Palette::yellow, Palette::green, Palette::blue };
                const float bloomVals[4] = { vVal, fVal, sVal, hVal };
                const float bxOff[4] = { -0.55f, 0.0f, 0.55f, 0.0f };
                const float byOff[4] = { 0.0f, -0.55f, 0.0f, 0.55f };
                for (int b = 0; b < 4; ++b)
                {
                    g.setGradientFill (juce::ColourGradient (bloomCols[b].withAlpha (0.28f + bloomVals[b] * 0.0028f),
                                                             cx + ballR * bxOff[b], cy + ballR * byOff[b],
                                                             bloomCols[b].withAlpha (0.0f), cx, cy, true));
                    g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);
                }

                // 3D回転するキャラメル＆ベリー・リボンメッシュ
                const float rotY = (argonOrbX - 0.5f) * 2.2f - 0.25f;
                const float rotX = -(argonOrbY - 0.5f) * 2.2f + 0.30f;
                const float cosY = std::cos (rotY), sinY = std::sin (rotY);
                const float cosX = std::cos (rotX), sinX = std::sin (rotX);
                auto project3D = [&] (float x0, float y0, float z0, float& sx, float& sy, float& sz)
                {
                    const float x1 =  x0 * cosY + z0 * sinY;
                    const float z1 = -x0 * sinY + z0 * cosY;
                    const float y2 =  y0 * cosX - z1 * sinX;
                    const float z2 =  y0 * sinX + z1 * cosX;
                    sx = cx + x1 * ballR; sy = cy + y2 * ballR; sz = z2;
                };
                for (int latIdx = -2; latIdx <= 2; ++latIdx)
                {
                    const float lat = (float) latIdx * 0.48f, rLat = std::cos (lat), y0 = std::sin (lat);
                    juce::Path p; bool down = false;
                    for (int step = 0; step <= 32; ++step)
                    {
                        const float lon = -juce::MathConstants<float>::pi + (float) step * juce::MathConstants<float>::twoPi / 32.0f;
                        float sx, sy, sz; project3D (rLat * std::sin (lon), y0, rLat * std::cos (lon), sx, sy, sz);
                        if (sz > -0.05f) { if (! down) { p.startNewSubPath (sx, sy); down = true; } else p.lineTo (sx, sy); }
                        else down = false;
                    }
                    g.setColour (Palette::ink.withAlpha (latIdx == 0 ? 0.42f : 0.20f));
                    g.strokePath (p, juce::PathStrokeType (latIdx == 0 ? 1.6f : 1.0f));
                }
                for (int lonIdx = -2; lonIdx <= 2; ++lonIdx)
                {
                    const float lon = (float) lonIdx * 0.55f, sinLon = std::sin (lon), cosLon = std::cos (lon);
                    juce::Path p; bool down = false;
                    for (int step = 0; step <= 32; ++step)
                    {
                        const float lat = -juce::MathConstants<float>::halfPi + (float) step * juce::MathConstants<float>::pi / 32.0f;
                        float sx, sy, sz; project3D (std::cos (lat) * sinLon, std::sin (lat), std::cos (lat) * cosLon, sx, sy, sz);
                        if (sz > -0.05f) { if (! down) { p.startNewSubPath (sx, sy); down = true; } else p.lineTo (sx, sy); }
                        else down = false;
                    }
                    g.setColour (Palette::salmon.withAlpha (lonIdx == 0 ? 0.52f : 0.24f));
                    g.strokePath (p, juce::PathStrokeType (lonIdx == 0 ? 1.6f : 1.0f));
                }

                // 4つのミニマカロン・サテライト
                const float satVals[4] = { vVal, fVal, sVal, hVal };
                const float satActs[4] = { vAct, fAct, sAct, hAct };
                const float baseAngles[4] = { -2.45f, -1.57f, -0.55f, 1.57f };
                for (int s = 0; s < 4; ++s)
                {
                    const float satDist = 14.0f + satVals[s] * 0.13f + satActs[s] * 7.5f;
                    const float satAng  = baseAngles[s] + satActs[s] * 1.2f;
                    const float sx = ox + std::cos (satAng) * satDist;
                    const float sy = oy + std::sin (satAng) * satDist;
                    g.setColour (Palette::ink.withAlpha (0.55f));
                    g.drawLine (ox, oy, sx, sy, 1.4f);
                    g.setColour (bloomCols[s]);
                    g.fillEllipse (sx - 4.8f, sy - 4.8f, 9.6f, 9.6f);
                    g.setColour (juce::Colours::white.withAlpha (0.95f));
                    g.fillRoundedRectangle (sx - 5.0f, sy - 1.1f, 10.0f, 2.2f, 1.0f);
                    g.setColour (Palette::ink.withAlpha (0.65f));
                    g.drawEllipse (sx - 4.8f, sy - 4.8f, 9.6f, 9.6f, 0.9f);
                }

                // ★ ポインター本体: 渦巻きフランボワーズ・ロリポップ (Swirl Lollipop)
                g.setColour (Palette::ink.withAlpha (0.75f));
                g.drawLine (ox + 1.0f, oy + 8.5f, ox + 8.0f, oy + 19.5f, 4.2f);
                g.setColour (juce::Colour (0xfffff8f0));
                g.drawLine (ox, oy + 8.0f, ox + 7.0f, oy + 19.0f, 2.6f); // キャンディの棒
                g.setGradientFill (juce::ColourGradient (Palette::salmon.brighter (0.22f), ox - 5.0f, oy - 5.0f,
                                                         Palette::salmon.darker (0.18f), ox + 6.0f, oy + 6.0f, true));
                g.fillEllipse (ox - 10.8f, oy - 10.8f, 21.6f, 21.6f);
                // キャンディの白い渦巻きライン
                juce::Path swirl;
                for (int sw = 0; sw <= 24; ++sw)
                {
                    const float t = (float) sw / 24.0f;
                    const float a = t * juce::MathConstants<float>::twoPi * 1.6f;
                    const float rSw = t * 8.6f;
                    const float sx = ox + std::cos (a) * rSw, sy = oy + std::sin (a) * rSw;
                    if (sw == 0) swirl.startNewSubPath (sx, sy); else swirl.lineTo (sx, sy);
                }
                g.setColour (juce::Colour (0xfffff8f2).withAlpha (0.95f));
                g.strokePath (swirl, juce::PathStrokeType (2.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (Palette::ink.withAlpha (0.82f));
                g.drawEllipse (ox - 10.8f, oy - 10.8f, 21.6f, 21.6f, 1.5f);

                // スノードーム上部のガラスハイライト
                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.52f), cx - ballR * 0.25f, cy - ballR * 0.90f,
                                                         juce::Colours::transparentWhite,        cx - ballR * 0.10f, cy - ballR * 0.10f, false));
                g.fillEllipse (cx - ballR * 0.76f, cy - ballR * 0.90f, ballR * 1.52f, ballR * 0.82f);
            }
            g.setColour (juce::Colour (0xff8c4f2b));
            g.drawEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f, 1.7f);
        }
        else
        {
            // =========================================================================
            // 🌿✦🎛★ PERSONA C / D / E:
            //   - isNatureAura (Nature/Aurora/Brand): 黄金アストロラーベ天球儀＆水紋オーブ
            //   - isStudioStyle (Studio Pro): 真鍮ベゼル＆琥珀色リサジュー位相ゴニオメーター
            //   - isSciFiStyle (Argon/Neon): 削り出しチタン・ジンバル＆3D回転トラッカーボール
            // =========================================================================
            const auto deckBgTop = oceanStarTheme() ? Palette::mirrorTop.withAlpha (0.36f)
                                 : pastelTheme()    ? Palette::panel2.darker (0.04f) : Palette::bgTop.darker (0.48f);
            const auto deckBgBot = oceanStarTheme() ? Palette::mirrorBot.withAlpha (0.48f)
                                 : pastelTheme()    ? Palette::panel.darker  (0.08f) : Palette::panel.darker (0.30f);
            g.setGradientFill (juce::ColourGradient (deckBgTop, padF.getX(), padF.getY(),
                                                     deckBgBot, padF.getRight(), padF.getBottom(), false));
            g.fillRoundedRectangle (padF, 14.0f);
            g.setColour (oceanStarTheme() ? Palette::metalMid.withAlpha (0.68f)
                       : isStudioStyle    ? Palette::yellow.withAlpha (0.55f) : Palette::panelLn.withAlpha (0.65f));
            g.drawRoundedRectangle (padF.reduced (0.5f), 14.0f, 1.2f);

            // 外周の3D回転ガイド軌道アーク ＆ 32バンド・ポーラースペクトラム光冠ハロー
            const auto rimAccent = oceanStarTheme() ? Palette::metalHi
                                 : (isStudioStyle || themeMode == 3) ? Palette::yellow : Palette::ice;
            for (int q = 0; q < 4; ++q)
            {
                const float qa = (float) q * juce::MathConstants<float>::halfPi + 0.785f;
                juce::Path orbitArc;
                orbitArc.addCentredArc (cx, cy, socketR + 6.5f, socketR + 6.5f, 0.0f,
                                        qa - 0.34f, qa + 0.34f, true);
                g.setColour (rimAccent.withAlpha (0.45f));
                g.strokePath (orbitArc, juce::PathStrokeType (1.4f));
            }
            // ★ 32本の放射状ポーラー倍音スペクトラム・ハローバー（歌声と4軸エネルギーに呼応して天球外周が脈動）
            for (int sb = 0; sb < 32; ++sb)
            {
                const float sa = (float) sb * juce::MathConstants<float>::twoPi / 32.0f;
                const float dirW = 0.5f + 0.5f * std::cos (sa - std::atan2 (oy - cy, ox - cx));
                const float waveMod = 0.5f + 0.5f * std::sin ((float) sb * 1.7f + (vVal + sVal) * 0.05f);
                const float barEnergy = juce::jlimit (0.15f, 1.0f,
                    0.22f + 0.42f * dirW + 0.25f * waveMod + (vAct + fAct + sAct + hAct) * 0.25f);
                const float rIn  = socketR + 1.8f;
                const float rOut = rIn + 2.5f + barEnergy * 7.5f;
                const juce::Colour barCol = (sb % 4 == 0) ? Palette::yellow
                                          : (sb % 2 == 0) ? Palette::ice : Palette::salmon;
                g.setColour (barCol.withAlpha (0.32f + barEnergy * 0.52f));
                g.drawLine (cx + std::cos (sa) * rIn,  cy + std::sin (sa) * rIn,
                            cx + std::cos (sa) * rOut, cy + std::sin (sa) * rOut,
                            (sb % 4 == 0) ? 2.0f : 1.3f);
            }

            // 2. ソケットリング（OceanStarは天球アストロラーベ金環、Studio/Brandは真鍮ゴールド、Natureはエメラルド/ゴールド、Argonはチタン）
            g.setColour (juce::Colours::black.withAlpha (pastelTheme() ? 0.18f : 0.55f));
            g.fillEllipse (cx - socketR - 2.0f, cy - socketR + 2.0f, (socketR + 2.0f) * 2.0f, (socketR + 2.0f) * 2.0f);
            if (oceanStarTheme())
            {
                g.setGradientFill (juce::ColourGradient (Palette::metalHi,   cx - socketR, cy - socketR,
                                                         Palette::metalDark, cx + socketR, cy + socketR, false));
            }
            else if (isStudioStyle || themeMode == 3)
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8b268), cx - socketR, cy - socketR,
                                                         juce::Colour (0xff59421c), cx + socketR, cy + socketR, false));
            }
            else
            {
                g.setGradientFill (juce::ColourGradient (Palette::panel2.brighter (0.28f), cx - socketR, cy - socketR,
                                                         Palette::bgTop.darker (pastelTheme() ? 0.15f : 0.50f), cx + socketR, cy + socketR, false));
            }
            g.fillEllipse (cx - socketR, cy - socketR, socketR * 2.0f, socketR * 2.0f);
            g.setColour (rimAccent.withAlpha (0.55f));
            g.drawEllipse (cx - socketR, cy - socketR, socketR * 2.0f, socketR * 2.0f, 1.4f);

            // ソケット外周の36刻み精密目盛り（天球儀／ゴニオメーター／エンコーダ）
            for (int deg = 0; deg < 36; ++deg)
            {
                const float a = (float) deg * juce::MathConstants<float>::twoPi / 36.0f;
                const bool major = (deg % 3 == 0);
                g.setColour (major ? rimAccent.withAlpha (0.65f) : Palette::inkSoft.withAlpha (0.28f));
                g.drawLine (cx + std::cos (a) * (socketR - 3.5f), cy + std::sin (a) * (socketR - 3.5f),
                            cx + std::cos (a) * (socketR - 0.5f), cy + std::sin (a) * (socketR - 0.5f),
                            major ? 1.4f : 0.9f);
            }

            // 4方位のベアリング／天球儀コンパス星
            if (isNatureAura)
            {
                drawSparkle4 (cx, cy - socketR - 1.0f, 5.5f, Palette::yellow);
                drawSparkle4 (cx, cy + socketR + 1.0f, 5.5f, Palette::blue);
                drawSparkle4 (cx - socketR - 1.0f, cy, 5.5f, Palette::salmon);
                drawSparkle4 (cx + socketR + 1.0f, cy, 5.5f, Palette::ice);
            }
            else
            {
                const float brAngles[4] = { juce::MathConstants<float>::pi, -juce::MathConstants<float>::halfPi, 0.0f, juce::MathConstants<float>::halfPi };
                const juce::Colour brCols[4] = { Palette::salmon, Palette::yellow, Palette::ice, Palette::blue };
                const float brVals[4] = { vVal, fVal, sVal, hVal };
                for (int b = 0; b < 4; ++b)
                {
                    const float ba = brAngles[b];
                    juce::Path bearing;
                    bearing.addCentredArc (cx, cy, socketR + 1.5f, socketR + 1.5f, 0.0f,
                                           ba - 0.24f + juce::MathConstants<float>::halfPi,
                                           ba + 0.24f + juce::MathConstants<float>::halfPi, true);
                    g.setColour (brCols[b].withAlpha (0.38f + 0.55f * (brVals[b] * 0.01f)));
                    g.strokePath (bearing, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }

            // 3. 球体内部のシェーディング＆3Dメッシュ
            {
                juce::Path sphereClip;
                sphereClip.addEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);
                juce::Graphics::ScopedSaveState clipState (g);
                g.reduceClipRegion (sphereClip);

                const auto spTop = (themeMode == 15) ? juce::Colour (0xff00f5d4)
                                 : oceanStarTheme()  ? Palette::mirrorTop.brighter (0.18f)
                                 : (themeMode == 2)  ? juce::Colour (0xffe8f6ee)
                                 : isStudioStyle     ? juce::Colour (0xff2c2419)
                                                     : Palette::panel2.brighter (0.20f);
                const auto spBot = (themeMode == 15) ? juce::Colour (0xff023e8a)
                                 : oceanStarTheme()  ? Palette::mirrorBot.darker (0.25f)
                                 : (themeMode == 2)  ? juce::Colour (0xffb8dec8)
                                 : isStudioStyle     ? juce::Colour (0xff120e0a)
                                                     : Palette::bgBot.darker (0.68f);
                g.setGradientFill (juce::ColourGradient (
                    spTop, ox * 0.6f + cx * 0.4f, oy * 0.6f + cy * 0.4f,
                    spBot, cx + (cx - ox) * 0.9f, cy + (cy - oy) * 0.9f, true));
                g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);

                // 🌌🌊🌅🍁👑 風景・季節コレクション専用：中央3Dオーブ内の世界観ジオラマ描画
                if (themeMode == 15)
                {
                    // 🌊 02 瑞々しい碧海: ぷるんとした水球内のコースティクス水紋＋下部メニスカス反射＋6個の気泡
                    ThemePainter::drawCausticWaveMesh (g, { cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f },
                                                       juce::Colour (0xffcaf0f8), 0.46f, 2);
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0xaa90f1ef), cx, cy + ballR * 0.92f,
                                                             juce::Colours::transparentBlack, cx, cy + ballR * 0.15f, false));
                    g.fillEllipse (cx - ballR * 0.88f, cy - ballR * 0.50f, ballR * 1.76f, ballR * 1.40f);
                    for (int b = 0; b < 6; ++b)
                    {
                        const float bx = cx + (ThemePainter::hash01 (b * 19 + 3) - 0.5f) * ballR * 1.35f;
                        const float by = cy + (ThemePainter::hash01 (b * 19 + 11) - 0.5f) * ballR * 1.35f;
                        ThemePainter::drawWaterDroplet (g, bx, by, 3.2f + 4.5f * ThemePainter::hash01 (b * 19 + 23),
                                                        juce::Colour (0xff48cae4), 0.85f);
                    }
                }
                else if (themeMode == 14)
                {
                    // 🌌 01 天の川銀河: 球体内部を斜めに横切る天の川ネビュラと星屑
                    juce::Path galBand;
                    galBand.startNewSubPath (cx - ballR * 0.9f, cy + ballR * 0.65f);
                    galBand.quadraticTo (cx, cy - ballR * 0.10f, cx + ballR * 0.9f, cy - ballR * 0.65f);
                    g.setColour (juce::Colour (0x55ec4899));
                    g.strokePath (galBand, juce::PathStrokeType (ballR * 0.42f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.setColour (juce::Colour (0x6638bdf8));
                    g.strokePath (galBand, juce::PathStrokeType (ballR * 0.22f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    for (int s = 0; s < 24; ++s)
                    {
                        const float sx = cx + (ThemePainter::hash01 (s * 17 + 5) - 0.5f) * ballR * 1.6f;
                        const float sy = cy + (ThemePainter::hash01 (s * 17 + 13) - 0.5f) * ballR * 1.6f;
                        g.setColour ((s % 3 == 0) ? juce::Colour (0xfffef08a) : juce::Colours::white);
                        g.fillEllipse (sx - 1.2f, sy - 1.2f, 2.4f, 2.4f);
                    }
                }
                else if (themeMode == 16)
                {
                    // 🌅 03 朝焼けの海: 球体赤道から昇る黄金の朝日と海面リフレクション
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0x99ffbe0b), cx, cy + ballR * 0.12f,
                                                             juce::Colours::transparentBlack, cx + ballR * 0.78f, cy + ballR * 0.12f, true));
                    g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);
                    for (int rp = 0; rp < 7; ++rp)
                    {
                        const float ry = cy + ballR * (0.16f + 0.10f * (float) rp);
                        const float rw = ballR * (0.68f - 0.06f * (float) rp);
                        g.setColour (juce::Colour (0xfffff3b0).withAlpha (0.58f - (float) rp * 0.06f));
                        g.drawLine (cx - rw, ry, cx + rw, ry, 1.6f);
                    }
                }
                else if (themeMode == 17)
                {
                    // 🍁 04 晩秋〜初冬: 球体内に舞う紅葉（🍁）と初雪の結晶（❄）と水鏡波紋
                    g.setColour (juce::Colour (0x55bae6fd));
                    g.drawEllipse (cx - ballR * 0.65f, cy + ballR * 0.18f, ballR * 1.30f, ballR * 0.34f, 1.2f);
                    ThemePainter::drawMapleLeaf   (g, cx - ballR * 0.35f, cy - ballR * 0.18f, 7.5f, -0.4f, juce::Colour (0xffe11d48), 0.88f);
                    ThemePainter::drawMapleLeaf   (g, cx + ballR * 0.32f, cy + ballR * 0.25f, 6.8f,  0.6f, juce::Colour (0xfff59e0b), 0.85f);
                    ThemePainter::drawSnowCrystal (g, cx + ballR * 0.28f, cy - ballR * 0.34f, 6.2f,  0.2f, juce::Colour (0xffe0f2fe), 0.88f);
                    ThemePainter::drawSnowCrystal (g, cx - ballR * 0.24f, cy + ballR * 0.32f, 5.4f,  0.7f, juce::Colour (0xffbae6fd), 0.82f);
                }

                // Studio Pro: ヴィンテージCRTゴニオメーターのX/Y軸＆位相対角線目盛り
                if (isStudioStyle)
                {
                    g.setColour (Palette::yellow.withAlpha (0.22f));
                    g.drawHorizontalLine (juce::roundToInt (cy), cx - ballR, cx + ballR);
                    g.drawVerticalLine   (juce::roundToInt (cx), cy - ballR, cy + ballR);
                    g.drawLine (cx - ballR * 0.7f, cy - ballR * 0.7f, cx + ballR * 0.7f, cy + ballR * 0.7f, 0.9f);
                    g.drawLine (cx - ballR * 0.7f, cy + ballR * 0.7f, cx + ballR * 0.7f, cy - ballR * 0.7f, 0.9f);
                }

                // 4象限の倍音オーラ
                g.setGradientFill (juce::ColourGradient (Palette::salmon.withAlpha (0.14f + vVal * 0.0028f), cx - ballR * 0.7f, cy,
                                                         juce::Colours::transparentBlack, cx + ballR * 0.2f, cy, true));
                g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);
                g.setGradientFill (juce::ColourGradient (Palette::ice.withAlpha (0.14f + sVal * 0.0028f), cx + ballR * 0.7f, cy,
                                                         juce::Colours::transparentBlack, cx - ballR * 0.2f, cy, true));
                g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);
                g.setGradientFill (juce::ColourGradient (Palette::yellow.withAlpha (0.12f + fVal * 0.0024f), cx, cy - ballR * 0.7f,
                                                         juce::Colours::transparentBlack, cx, cy + ballR * 0.2f, true));
                g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);
                g.setGradientFill (juce::ColourGradient (Palette::blue.withAlpha (0.14f + hVal * 0.0028f), cx, cy + ballR * 0.7f,
                                                         juce::Colours::transparentBlack, cx, cy - ballR * 0.2f, true));
                g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);

                // 本物の3D球面回転メッシュ
                const float rotY = (argonOrbX - 0.5f) * 2.35f - 0.30f;
                const float rotX = -(argonOrbY - 0.5f) * 2.35f + 0.34f;
                const float cosY = std::cos (rotY), sinY = std::sin (rotY);
                const float cosX = std::cos (rotX), sinX = std::sin (rotX);

                auto project3D = [&] (float x0, float y0, float z0, float& sx, float& sy, float& sz)
                {
                    const float x1 =  x0 * cosY + z0 * sinY;
                    const float z1 = -x0 * sinY + z0 * cosY;
                    const float y2 =  y0 * cosX - z1 * sinX;
                    const float z2 =  y0 * sinX + z1 * cosX;
                    sx = cx + x1 * ballR;
                    sy = cy + y2 * ballR;
                    sz = z2;
                };

                const auto meshCol = isStudioStyle ? Palette::yellow : Palette::ice;
                for (int latIdx = -3; latIdx <= 3; ++latIdx)
                {
                    const float lat = (float) latIdx * 0.36f;
                    const float rLat = std::cos (lat);
                    const float y0   = std::sin (lat);
                    const bool isEq  = (latIdx == 0);
                    juce::Path latPath;
                    bool penDown = false;
                    for (int step = 0; step <= 40; ++step)
                    {
                        const float lon = -juce::MathConstants<float>::pi + (float) step * juce::MathConstants<float>::twoPi / 40.0f;
                        float sx, sy, sz;
                        project3D (rLat * std::sin (lon), y0, rLat * std::cos (lon), sx, sy, sz);
                        if (sz > -0.08f)
                        {
                            if (! penDown) { latPath.startNewSubPath (sx, sy); penDown = true; }
                            else           { latPath.lineTo (sx, sy); }
                        }
                        else penDown = false;
                    }
                    g.setColour (isEq ? meshCol.withAlpha (0.62f) : meshCol.withAlpha (0.24f));
                    g.strokePath (latPath, juce::PathStrokeType (isEq ? 1.8f : 1.0f));
                }

                for (int lonIdx = -4; lonIdx <= 4; ++lonIdx)
                {
                    const float lon = (float) lonIdx * 0.38f;
                    const float sinLon = std::sin (lon), cosLon = std::cos (lon);
                    const bool isPrime = (lonIdx == 0);
                    juce::Path lonPath;
                    bool penDown = false;
                    for (int step = 0; step <= 36; ++step)
                    {
                        const float lat = -juce::MathConstants<float>::halfPi + (float) step * juce::MathConstants<float>::pi / 36.0f;
                        const float rLat = std::cos (lat);
                        float sx, sy, sz;
                        project3D (rLat * sinLon, std::sin (lat), rLat * cosLon, sx, sy, sz);
                        if (sz > -0.08f)
                        {
                            if (! penDown) { lonPath.startNewSubPath (sx, sy); penDown = true; }
                            else           { lonPath.lineTo (sx, sy); }
                        }
                        else penDown = false;
                    }
                    g.setColour (isPrime ? Palette::salmon.withAlpha (0.62f) : meshCol.withAlpha (0.24f));
                    g.strokePath (lonPath, juce::PathStrokeType (isPrime ? 1.8f : 1.0f));
                }

                // ★ 球体中心 (cx, cy) から操作極 (ox, oy) へ伸びる「コメット・テイル（彗星軌道・水流ストリーム）」
                if (std::hypot (ox - cx, oy - cy) > 4.0f)
                {
                    const float ctrlX = (cx + ox) * 0.5f - (oy - cy) * 0.22f;
                    const float ctrlY = (cy + oy) * 0.5f + (ox - cx) * 0.22f;
                    juce::Path cometArc;
                    cometArc.startNewSubPath (cx, cy);
                    cometArc.quadraticTo (ctrlX, ctrlY, ox, oy);
                    g.setColour (Palette::ice.withAlpha (0.35f));
                    g.strokePath (cometArc, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.setColour (Palette::yellow.withAlpha (0.85f));
                    g.strokePath (cometArc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    for (int tn = 1; tn <= 5; ++tn)
                    {
                        const float tt = (float) tn / 6.0f;
                        const float omt = 1.0f - tt;
                        const float tx = omt * omt * cx + 2.0f * omt * tt * ctrlX + tt * tt * ox;
                        const float ty = omt * omt * cy + 2.0f * omt * tt * ctrlY + tt * tt * oy;
                        const float tr = 1.4f + tt * 2.2f;
                        g.setColour (juce::Colours::white.withAlpha (0.35f + tt * 0.55f));
                        g.fillEllipse (tx - tr, ty - tr, tr * 2.0f, tr * 2.0f);
                    }
                }

                // 球面上の操作極 (ox, oy) を囲む3Dエネルギー・レティクル（Natureでは多重水紋リング）
                const float inLv = juce::jlimit (0.0f, 1.0f, processor.getInputLevel() * 3.5f);
                const float pulseR = 14.0f + (vAct + fAct + sAct + hAct + inLv) * 10.0f;
                g.setGradientFill (juce::ColourGradient (Palette::yellow.withAlpha (0.48f), ox, oy,
                                                         juce::Colours::transparentBlack, ox + pulseR * 1.8f, oy, true));
                g.fillEllipse (ox - pulseR * 1.8f, oy - pulseR * 1.8f, pulseR * 3.6f, pulseR * 3.6f);

                g.setColour (Palette::salmon.withAlpha (0.68f));
                g.drawEllipse (ox - pulseR, oy - pulseR, pulseR * 2.0f, pulseR * 2.0f, 1.6f);
                if (isNatureAura)
                {
                    g.setColour (Palette::ice.withAlpha (0.35f));
                    g.drawEllipse (ox - pulseR * 1.45f, oy - pulseR * 1.45f, pulseR * 2.9f, pulseR * 2.9f, 1.1f);
                }

                const float satVals[4] = { vVal, fVal, sVal, hVal };
                const float satActs[4] = { vAct, fAct, sAct, hAct };
                const juce::Colour satCols[4] = { Palette::salmon, Palette::yellow, Palette::ice, Palette::blue };
                const float baseAngles[4] = { -2.45f, -1.57f, -0.55f, 1.57f };
                for (int s = 0; s < 4; ++s)
                {
                    const float satDist = 13.0f + satVals[s] * 0.13f + satActs[s] * 7.5f;
                    const float satAng  = baseAngles[s] + satActs[s] * 1.2f;
                    const float sx = ox + std::cos (satAng) * satDist;
                    const float sy = oy + std::sin (satAng) * satDist;
                    g.setColour (satCols[s].withAlpha (0.75f));
                    g.drawLine (ox, oy, sx, sy, 1.5f);
                    if (isNatureAura)
                    {
                        drawSparkle4 (sx, sy, 5.2f, satCols[s]);
                    }
                    else
                    {
                        g.setColour (satCols[s]);
                        g.fillEllipse (sx - 3.8f, sy - 3.8f, 7.6f, 7.6f);
                        g.setColour (juce::Colours::white.withAlpha (0.85f));
                        g.fillEllipse (sx - 1.5f, sy - 1.5f, 3.0f, 3.0f);
                    }
                }

                // 球面上の3Dポインター・ホットスポット核
                g.setGradientFill (juce::ColourGradient (Palette::yellow.brighter (0.40f), ox - 4.0f, oy - 4.0f,
                                                         Palette::yellowDk,                ox + 6.0f, oy + 7.0f, true));
                g.fillEllipse (ox - 10.0f, oy - 10.0f, 20.0f, 20.0f);
                g.setColour (Palette::ink);
                g.drawEllipse (ox - 10.0f, oy - 10.0f, 20.0f, 20.0f, 2.0f);
                if (isNatureAura)
                    drawSparkle4 (ox, oy, 8.5f, juce::Colours::white);
                else
                {
                    g.setColour (juce::Colours::white.withAlpha (0.90f));
                    g.fillEllipse (ox - 5.0f, oy - 5.0f, 4.2f, 3.2f);
                }

                // 球体上部の大型3D曲面ガラス・スペキュラドーム反射
                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), cx - ballR * 0.25f, cy - ballR * 0.92f,
                                                         juce::Colours::transparentWhite,        cx - ballR * 0.10f, cy - ballR * 0.05f, false));
                g.fillEllipse (cx - ballR * 0.78f, cy - ballR * 0.92f, ballR * 1.56f, ballR * 0.88f);

                // 球体外周リムの内部アンビエントシャドウ
                g.setColour (juce::Colours::black.withAlpha (pastelTheme() ? 0.16f : 0.48f));
                g.drawEllipse (cx - ballR + 1.0f, cy - ballR + 1.0f, (ballR - 1.0f) * 2.0f, (ballR - 1.0f) * 2.0f, 3.2f);
            }

            // 球体外周のネオン/真鍮リムリング
            g.setColour (rimAccent.withAlpha (0.82f));
            g.drawEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f, 1.6f);
        }

        // ★ Red Dot 四隅マイクロテレメトリ・インジケーターバー（左上=VELVET、右上=SILK、左下=FOCUS、右下=HALO）
        if (! liteMode && argonPadRect.getWidth() >= 230)
        {
            const float cVals[4] = { vVal, sVal, fVal, hVal };
            const juce::Colour cCols[4] = { Palette::salmon, Palette::ice, Palette::yellow, Palette::blue };
            const float cxs[4] = { padF.getX() + 8.0f, padF.getRight() - 46.0f, padF.getX() + 8.0f, padF.getRight() - 46.0f };
            const float cys[4] = { padF.getY() + 24.0f, padF.getY() + 24.0f, padF.getBottom() - 46.0f, padF.getBottom() - 46.0f };
            for (int c = 0; c < 4; ++c)
            {
                const juce::Rectangle<float> cr (cxs[c], cys[c], 38.0f, 5.0f);
                g.setColour (Palette::bgTop.darker (0.45f).withAlpha (0.72f));
                g.fillRoundedRectangle (cr, 2.5f);
                g.setColour (cCols[c].withAlpha (0.88f));
                g.fillRoundedRectangle (cr.withWidth (juce::jmax (4.0f, cr.getWidth() * juce::jlimit (0.0f, 1.0f, cVals[c] * 0.01f))), 2.5f);
                g.setColour (Palette::metalMid.withAlpha (0.55f));
                g.drawRoundedRectangle (cr, 2.5f, 0.8f);
            }
        }

        // 四隅・上下のHUD軸ラベル（球体ソケットと重ならないよう上下22px帯と下隅に配置。アクセシビリティ時は専用ラベルを描画済み）
        if (accessMode == 0)
        {
            const bool compactPad = (argonPadRect.getWidth() < 240);
            g.setColour (Palette::ink.withAlpha (0.92f));
            fitText (g, compactPad ? language->T ("◀倍音", "<VELV") : language->T ("◀ ベルベット", "< VELVET"),
                     argonPadRect.withWidth (compactPad ? 62 : 94).withHeight (20).translated (6, (int) (padF.getHeight() - 40.0f)),
                     juce::Justification::centredLeft, 14.0f, true);
            fitText (g, compactPad ? language->T ("空気▶", "SILK>") : language->T ("シルク空気 ▶", "SILK AIR >"),
                     argonPadRect.withX (argonPadRect.getRight() - (compactPad ? 68 : 100)).withWidth (compactPad ? 62 : 94).withHeight (20).translated (0, (int) (padF.getHeight() - 40.0f)),
                     juce::Justification::centredRight, 14.0f, true);
            fitText (g, language->T ("▲ 声芯フォーカス・近接", "^ FOCUS & ASMR"),
                     argonPadRect.withHeight (20).reduced (10, 0).translated (0, 2),
                     juce::Justification::centred, 14.0f, true);
            fitText (g, language->T ("▼ 3D立体ステージハロー", "v 3D STAGE HALO"),
                     argonPadRect.withY (argonPadRect.getBottom() - 21).withHeight (20).reduced (10, 0),
                     juce::Justification::centred, 14.0f, true);
        }
    }

    // ---- ★ Argon Velvet Engine 4本のフェーダー（ペルソナ別にハート宝石チャーム／マカロンキャンディ／24セグメント光導波路フェーダーを描き分け） ----
    if (! argonBarRects[0].isEmpty())
    {
        const float vals[4] = { vVal, fVal, sVal, hVal };
        const float acts[4] = { vAct, fAct, sAct, hAct };
        const char* barJa[4] = { "倍音 VELVET", "声芯 FOCUS", "空気 SILK",  "立体 HALO" };
        const char* barEn[4] = { "VELVET",      "FOCUS",      "SILK AIR",   "3D HALO" };
        const juce::Colour barCols[4] = { Palette::salmon, Palette::yellow, Palette::ice, Palette::blue };
        const bool kawaiiBars = (isLiverStyle || isYuruStyle);

        for (int b = 0; b < 4; ++b)
        {
            const auto r = argonBarRects[(size_t) b];
            const auto rf = r.toFloat();
            const float norm = juce::jlimit (0.0f, 1.0f, vals[b] * 0.01f);
            const float barRad = kawaiiBars ? rf.getHeight() * 0.5f : 6.5f;

            if (liteMode)
            {
                g.setColour (Palette::track);
                g.fillRoundedRectangle (rf, barRad);
                if (norm > 0.005f)
                {
                    g.setColour (barCols[b].withAlpha (0.55f));
                    g.fillRoundedRectangle (r.withWidth (juce::jmax (6, juce::roundToInt (r.getWidth() * norm))).toFloat(), barRad);
                }
            }
            else if (kawaiiBars)
            {
                // ♥🍰 V-Liver / Yuru-fuwa: クチュール・リップグロス／リキッドファンデーション・ガラスバイアル（貴金属キャップ付き）
                g.setGradientFill (juce::ColourGradient (
                    nightTheme() ? Palette::panel2.darker (0.18f) : Palette::panel.brighter (0.05f), rf.getX(), rf.getY(),
                    nightTheme() ? Palette::panel.darker (0.08f)  : Palette::panel2,                 rf.getX(), rf.getBottom(), false));
                g.fillRoundedRectangle (rf, barRad);

                const float goldCapW = 10.0f;
                const juce::Rectangle<float> goldCapR (rf.getX() + 1.0f, rf.getY() + 1.0f, goldCapW, rf.getHeight() - 2.0f);
                g.setGradientFill (juce::ColourGradient (Palette::metalHi,  goldCapR.getX(), goldCapR.getY(),
                                                         Palette::metalMid, goldCapR.getX(), goldCapR.getBottom(), false));
                g.fillRoundedRectangle (goldCapR, barRad - 1.0f);
                g.setColour (Palette::metalDark.withAlpha (0.65f));
                g.drawLine (goldCapR.getRight(), goldCapR.getY() + 1.5f, goldCapR.getRight(), goldCapR.getBottom() - 1.5f, 1.2f);

                const float tubeX = rf.getX() + goldCapW + 1.5f;
                const float tubeMaxW = juce::jmax (8.0f, rf.getRight() - tubeX - 3.0f);
                const int fillW = juce::roundToInt (tubeMaxW * norm);
                if (fillW > 2)
                {
                    const juce::Rectangle<float> fillRf (tubeX, rf.getY() + 2.2f, (float) fillW, rf.getHeight() - 4.4f);
                    g.setGradientFill (juce::ColourGradient (barCols[b].withAlpha (0.38f), fillRf.getX(), fillRf.getY(),
                                                             barCols[b].withAlpha (0.78f), fillRf.getRight(), fillRf.getY(), false));
                    g.fillRoundedRectangle (fillRf, (rf.getHeight() - 4.4f) * 0.45f);

                    juce::Path clipP;
                    clipP.addRoundedRectangle (fillRf, (rf.getHeight() - 4.4f) * 0.45f);
                    juce::Graphics::ScopedSaveState ss (g);
                    g.reduceClipRegion (clipP);
                    g.setColour (juce::Colours::white.withAlpha (nightTheme() ? 0.16f : 0.34f));
                    for (float sx = fillRf.getX() - 14.0f; sx < fillRf.getRight() + 8.0f; sx += 16.0f)
                        g.drawLine (sx, fillRf.getBottom(), sx + 9.0f, fillRf.getY(), 2.4f);
                }

                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (nightTheme() ? 0.24f : 0.78f),
                                                         tubeX + 4.0f, rf.getY() + 1.8f,
                                                         juce::Colours::transparentWhite,
                                                         tubeX + 4.0f, rf.getY() + rf.getHeight() * 0.48f, false));
                g.fillRoundedRectangle (tubeX + 2.0f, rf.getY() + 1.8f, tubeMaxW - 6.0f, rf.getHeight() * 0.36f, 3.0f);

                const float cxCap = tubeX + 6.0f + norm * (tubeMaxW - 12.0f);
                const float cyCap = rf.getCentreY();
                if (isLiverStyle)
                {
                    drawHeartShape (cxCap, cyCap + 0.8f, 8.2f, Palette::metalHi);
                    drawHeartShape (cxCap, cyCap + 0.4f, 6.5f, barCols[b]);
                    g.setColour (juce::Colours::white.withAlpha (0.92f));
                    g.fillEllipse (cxCap - 2.6f, cyCap - 2.6f, 2.2f, 1.8f);
                }
                else
                {
                    g.setColour (Palette::ink.withAlpha (0.72f));
                    g.fillEllipse (cxCap - 7.0f, cyCap - 7.0f, 14.0f, 14.0f);
                    g.setColour (juce::Colour (0xfffff8f0));
                    g.fillEllipse (cxCap - 6.0f, cyCap - 6.0f, 12.0f, 12.0f);
                    g.setColour (barCols[b]);
                    g.fillEllipse (cxCap - 4.5f, cyCap - 4.5f, 9.0f, 9.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.88f));
                    g.fillEllipse (cxCap - 2.0f, cyCap - 2.3f, 2.8f, 2.2f);
                }
            }
            else
            {
                // ★ Red Dot 仕様: 24セグメント・オプティカルライトパイプ・タッチフェーダー
                g.setColour (pastelTheme() ? Palette::panel2.darker (0.05f) : Palette::bgTop.darker (0.48f).withAlpha (0.94f));
                g.fillRoundedRectangle (rf, 6.5f);

                // 24個のマイクロLEDライトパイプセル（ラベル領域と右端%表示の間を精密切削）
                const float lblW = juce::jmin (90.0f, rf.getWidth() * 0.44f);
                const float pctW = 46.0f;
                const float trkX = rf.getX() + lblW + 3.0f;
                const float trkW = juce::jmax (24.0f, rf.getWidth() - lblW - pctW - 6.0f);
                const int numCells = (trkW >= 72.0f) ? 20 : 10;
                const float cellStep = trkW / (float) numCells;
                const float cellW    = juce::jmax (1.8f, cellStep - 1.4f);
                for (int c = 0; c < numCells; ++c)
                {
                    const float cNorm = (float) (c + 1) / (float) numCells;
                    const bool lit = (norm >= cNorm - (0.5f / (float) numCells));
                    const juce::Colour cellCol = barCols[b].interpolatedWith (Palette::yellow, cNorm * 0.32f);
                    const float cxCell = trkX + (float) c * cellStep;
                    g.setColour (lit ? cellCol.withAlpha (0.62f) : cellCol.withAlpha (0.12f));
                    g.fillRoundedRectangle (cxCell, rf.getY() + 2.4f, cellW, rf.getHeight() - 4.8f, 1.8f);
                    if (lit)
                    {
                        g.setColour (cellCol.brighter (0.38f).withAlpha (0.92f));
                        g.fillRect (cxCell, rf.getY() + 2.0f, cellW, 2.0f);
                        g.fillRect (cxCell, rf.getBottom() - 4.0f, cellW, 2.0f);
                    }
                }

                // 実音倍音ピーク輝線
                if (acts[b] > 0.02f)
                {
                    const float liveNorm = juce::jlimit (0.0f, 1.0f, norm * 0.7f + acts[b] * 0.6f);
                    const float peakX = trkX + liveNorm * (trkW - 4.0f);
                    g.setColour (barCols[b].brighter (0.45f));
                    g.fillRoundedRectangle (peakX, rf.getY() + 2.0f, 2.8f, rf.getHeight() - 4.0f, 1.4f);
                }

                // 3Dオプティカルプリズム・フェーダーキャリッジ（上下の貴金属/チタン爪＋中央ホワイトホットレーザー刃）
                const float capW = 11.0f;
                const float capX = trkX + norm * juce::jmax (2.0f, trkW - capW);
                const float cxCap = capX + capW * 0.5f;
                g.setColour (oceanStarTheme() ? Palette::metalHi : Palette::panel2.brighter (0.38f));
                g.fillRoundedRectangle (capX, rf.getY() + 0.5f, capW, 4.8f, 2.0f);
                g.fillRoundedRectangle (capX, rf.getBottom() - 5.3f, capW, 4.8f, 2.0f);
                g.setColour (barCols[b]);
                g.drawRoundedRectangle (capX, rf.getY() + 0.5f, capW, 4.8f, 2.0f, 1.1f);
                g.drawRoundedRectangle (capX, rf.getBottom() - 5.3f, capW, 4.8f, 2.0f, 1.1f);
                g.setColour (juce::Colours::white.withAlpha (0.95f));
                g.fillRect (cxCap - 1.0f, rf.getY() + 2.5f, 2.0f, rf.getHeight() - 5.0f);

                // 左端のダーククリスタル・ラベルカプセル下地（文字の視認性を100%保証）
                const juce::Rectangle<float> lblPill (rf.getX() + 2.2f, rf.getY() + 2.2f, lblW - 1.0f, rf.getHeight() - 4.4f);
                g.setColour (Palette::bgTop.darker (0.58f).withAlpha (0.86f));
                g.fillRoundedRectangle (lblPill, 4.5f);
                g.setColour (barCols[b].withAlpha (0.52f));
                g.drawRoundedRectangle (lblPill, 4.5f, 0.9f);

                // 右端のOLEDパーセント表示カプセル下地
                const juce::Rectangle<float> pctPill (rf.getRight() - pctW, rf.getY() + 2.2f, pctW - 2.2f, rf.getHeight() - 4.4f);
                g.setColour (Palette::bgTop.darker (0.58f).withAlpha (0.86f));
                g.fillRoundedRectangle (pctPill, 4.5f);
                g.setColour (barCols[b].withAlpha (0.58f));
                g.drawRoundedRectangle (pctPill, 4.5f, 0.9f);
            }

            g.setColour (kawaiiBars ? Palette::metalMid : (oceanStarTheme() ? Palette::metalMid.withAlpha (0.85f) : barCols[b].withAlpha (0.88f)));
            g.drawRoundedRectangle (rf.reduced (0.5f), barRad, 1.3f);
            g.setColour (Palette::ink);
            const int lblRectW = kawaiiBars ? (r.getWidth() - 52)
                                            : juce::roundToInt (juce::jmin (90.0f, rf.getWidth() * 0.44f));
            fitText (g, juce::String::fromUTF8 (language->english ? barEn[b] : barJa[b]),
                     r.withWidth (lblRectW).reduced (kawaiiBars ? 12 : 6, 1),
                     juce::Justification::centredLeft, 13.5f, true);
            fitText (g, juce::String (juce::roundToInt (vals[b])) + "%",
                     r.withTrimmedLeft (r.getWidth() - 46).reduced (4, 1),
                     juce::Justification::centredRight, 13.5f, true);
        }
    }

    // ---- Argon Voice DNA 4つのワンタッチ・ターゲットピル ----
    if (! argonTargetRects[0].isEmpty())
    {
        const bool compactPill = (argonTargetRects[0].getWidth() < 96);
        const char* tJaFull[4]  = { "★ VELVET 歌枠", "♥ ASMR 囁き", "✦ CRYSTAL 抜け", "◆ HALO 立体" };
        const char* tJaShort[4] = { "★ 歌枠",        "♥ ASMR",      "✦ 透明抜け",     "◆ 3D立体" };
        const char* tEn[4]      = { "VELVET",        "ASMR",        "CRYSTAL",        "3D HALO" };
        const juce::Colour tCols[4] = { Palette::salmon, Palette::yellow, Palette::ice, Palette::blue };
        const bool kawaiiPills = (isLiverStyle || isYuruStyle);
        for (int t = 0; t < 4; ++t)
        {
            const auto r = argonTargetRects[(size_t) t];
            const auto rf = r.toFloat();
            const float tRad = kawaiiPills ? rf.getHeight() * 0.5f : 8.0f;
            const bool active = (argonActiveTarget == t);
            if (! liteMode)
            {
                g.setColour (kawaiiPills ? tCols[t].withAlpha (active ? 0.28f : 0.14f)
                                         : juce::Colours::black.withAlpha (active ? 0.28f : 0.14f));
                g.fillRoundedRectangle (rf.translated (0.0f, 2.0f), tRad);
            }
            if (kawaiiPills && ! nightTheme())
            {
                // クチュール・エナメル＆クッションピル
                g.setGradientFill (juce::ColourGradient (
                    active ? tCols[t].brighter (0.55f)  : Palette::panel.brighter (0.05f), rf.getX(), rf.getY(),
                    active ? tCols[t].withAlpha (0.42f) : Palette::panel2.darker (0.04f),  rf.getX(), rf.getBottom(), false));
                g.fillRoundedRectangle (rf, tRad);
                g.setColour (juce::Colours::white.withAlpha (0.68f));
                g.fillRoundedRectangle (rf.getX() + 4.0f, rf.getY() + 1.5f, rf.getWidth() - 8.0f, rf.getHeight() * 0.40f, tRad * 0.45f);
            }
            else
            {
                g.setGradientFill (juce::ColourGradient (
                    active ? tCols[t].withAlpha (0.42f) : Palette::panel2.brighter (0.06f).withAlpha (0.92f), rf.getX(), rf.getY(),
                    active ? tCols[t].darker (0.15f).withAlpha (0.28f) : Palette::panel2.darker (0.05f).withAlpha (0.92f), rf.getX(), rf.getBottom(), false));
                g.fillRoundedRectangle (rf, tRad);
            }
            if (! liteMode)
            {
                g.setColour (tCols[t].withAlpha (active ? 0.95f : 0.50f));
                g.fillRoundedRectangle (rf.getX() + 10.0f, rf.getBottom() - 3.2f, rf.getWidth() - 20.0f, 2.2f, 1.1f);
            }
            g.setColour (active ? tCols[t] : (kawaiiPills ? Palette::metalMid : Palette::panelLn));
            g.drawRoundedRectangle (rf.reduced (0.5f), tRad, active ? 1.8f : 1.2f);
            g.setColour (active ? Palette::ink : Palette::inkSoft);
            const char* jaTxt = compactPill ? tJaShort[t] : tJaFull[t];
            fitText (g, juce::String::fromUTF8 (language->english ? tEn[t] : jaTxt),
                     r.reduced (4, 0), juce::Justification::centred, 14.0f, active);
        }
    }

    card (overviewOutput, Palette::green);
    g.setColour (Palette::ink);
    const bool compactOut = (overviewOutput.getWidth() < 340);
    const int titleW = (overviewOutput.getWidth() >= 400 && ! ioInMonoRect.isEmpty())
                     ? (ioInMonoRect.getX() - overviewOutput.getX() - 28)
                     : (compactOut ? 60 : 124);
    fitText (g, compactOut ? language->T ("計器", "I/O") : language->T ("計器・入出力", "TELEMETRY & I/O"),
             juce::Rectangle<int> (overviewOutput.getX() + 24, overviewOutput.getY() + 8, juce::jmax (44, titleW), 28),
             juce::Justification::centredLeft, 15.5f, true);

    // ---- 計器＆入出力カード内の「🎤入力: モノラル/ステレオ」「🔊出力: モノラル/ステレオ」直結ピル描画 ----
    {
        auto drawIoPill = [&] (juce::Rectangle<int> r, bool active, juce::Colour col, const juce::String& label)
        {
            if (r.isEmpty()) return;
            const auto rf = r.toFloat();
            const float rad = rf.getHeight() * 0.46f;
            g.setColour (active ? col.withAlpha (pastelTheme() ? 0.30f : 0.36f)
                                : Palette::panel2.withAlpha (0.92f));
            g.fillRoundedRectangle (rf, rad);
            g.setColour (active ? col : Palette::panelLn);
            g.drawRoundedRectangle (rf.reduced (0.5f), rad, active ? 1.6f : 1.0f);
            g.setColour (active ? Palette::ink : Palette::inkSoft);
            fitText (g, label, r.reduced (4, 0), juce::Justification::centred, 12.5f, active);
        };
        const int rMode = juce::jlimit (0, 4, processor.getInputRouteMode());
        const bool inMonoActive = (rMode != 4);
        const bool outMonoActive = processor.getOutputMonoMode();
        const bool shortIo = (ioInMonoRect.getWidth() < 90);
        const char* inJaFull[5]  = { "🎤入力:自動モノ", "🎤入力:IN1モノ", "🎤入力:IN2モノ", "🎤入力:1+2モノ", "🎤入力:ステレオ" };
        const char* inJaShort[5] = { "🎤入:自動",       "🎤入:IN1",       "🎤入:IN2",       "🎤入:1+2",       "🎤入:ステ"       };
        const char* inEn[5]      = { "IN:AUTO-M",       "IN:1-MONO",      "IN:2-MONO",      "IN:1+2-M",       "IN:STEREO"       };
        const juce::String inTxt = juce::String::fromUTF8 (language->english ? inEn[rMode] : (shortIo ? inJaShort[rMode] : inJaFull[rMode]));
        const juce::String outTxt = outMonoActive
            ? (shortIo ? language->T ("🔊出:モノ", "OUT:M") : language->T ("🔊出力:モノラル", "OUT:MONO"))
            : (shortIo ? language->T ("🔊出:ステ", "OUT:S") : language->T ("🔊出力:ステレオ", "OUT:STEREO"));
        drawIoPill (ioInMonoRect,  inMonoActive,  Palette::green, inTxt);
        drawIoPill (ioOutMonoRect, outMonoActive, Palette::blue,  outTxt);
    }

    // ---- 5ch リアルタイム計器盤（Kawaii時はパステル・ペンライト粒＆ハートピーク、通常時はスタジオLEDバーグラフ） ----
    {
        const float inDb  = juce::Decibels::gainToDecibels (processor.getInputLevel(),  -60.0f);
        const float outDb = juce::Decibels::gainToDecibels (processor.getOutputLevel(), -60.0f);
        const float grDb  = juce::jlimit (0.0f, 24.0f, processor.getGainReductionDb());
        const float vPct  = processor.apvts.getRawParameterValue ("argon_velvet")->load();
        const float fPct  = processor.apvts.getRawParameterValue ("argon_focus")->load();
        const float hPct  = processor.apvts.getRawParameterValue ("argon_halo")->load();

        // メーター描画領域: Studio Rack(mode 2) と Classic(mode 3) は上部全幅、Argon(0) と Liver(1) は左半分
        const int meterW = (overviewLayoutMode == 0 || overviewLayoutMode == 1)
                         ? (overviewOutput.getWidth() - 236)
                         : (overviewOutput.getWidth() - 24);
        const int numRows = (overviewLayoutMode == 3) ? 3 : 5;
        const int rowStep = (overviewLayoutMode == 3) ? 18 : ((oceanStarTheme() && overviewLayoutMode == 0) ? 30 : 32);
        const int rowStartY = (oceanStarTheme() && overviewLayoutMode == 0) ? 39 : 42;
        const bool kawaiiMeters = (isLiverStyle || isYuruStyle);

        struct MeterRow { juce::String name; float norm; juce::String valTxt; juce::Colour col; bool levelZone; };
        const float fhPct = juce::jmax (fPct, hPct);
        const MeterRow rows[5] = {
            { language->T ("入力", "IN"),
              juce::jlimit (0.0f, 1.0f, (inDb + 60.0f) / 60.0f),
              inDb <= -59.5f ? "-inf" : (juce::String (juce::roundToInt (inDb)) + "dB"),
              inDb > -1.0f ? Palette::salmon : Palette::green, true },
            { language->T ("出力", "OUT"),
              juce::jlimit (0.0f, 1.0f, (outDb + 60.0f) / 60.0f),
              outDb <= -59.5f ? "-inf" : (juce::String (juce::roundToInt (outDb)) + "dB"),
              outDb > -1.0f ? Palette::salmon : (outDb >= -20.0f ? Palette::green : Palette::ice), true },
            { language->T ("圧縮", "COMP"),
              juce::jlimit (0.0f, 1.0f, grDb / 18.0f),
              "-" + juce::String (grDb, 1) + "dB",
              Palette::yellow, false },
            { language->T ("倍音", "VELV"),
              juce::jlimit (0.0f, 1.0f, vPct * 0.01f),
              juce::String (juce::roundToInt (vPct)) + "%",
              Palette::salmon, false },
            { language->T ("声芯", "FOC"),
              juce::jlimit (0.0f, 1.0f, fhPct * 0.01f),
              juce::String (juce::roundToInt (fhPct)) + "%",
              Palette::blue, false }
        };
        for (int i = 0; i < numRows; ++i)
        {
            const int ry = overviewOutput.getY() + rowStartY + i * rowStep;
            auto labR = juce::Rectangle<int> (overviewOutput.getX() + 14, ry, 44, 18);
            auto valR = juce::Rectangle<int> (overviewOutput.getX() + 14 + meterW - 52, ry, 52, 18);
            const int barH = (overviewLayoutMode == 3) ? 8 : 11;
            auto barR = juce::Rectangle<int> (labR.getRight() + 4, ry + (18 - barH) / 2,
                                              juce::jmax (16, valR.getX() - labR.getRight() - 8), barH);
            g.setColour (Palette::ink);
            fitText (g, rows[i].name, labR, juce::Justification::centredLeft, 14.0f, true);

            if (liteMode || barR.getWidth() < 28)
            {
                g.setColour (Palette::track);
                g.fillRoundedRectangle (barR.toFloat(), 4.0f);
                if (rows[i].norm > 0.005f)
                {
                    g.setColour (rows[i].col);
                    g.fillRoundedRectangle (barR.withWidth (juce::jmax (4, juce::roundToInt (barR.getWidth() * rows[i].norm))).toFloat(), 4.0f);
                }
            }
            else if (kawaiiMeters)
            {
                // ♥🍰 パール＆ローズジュエル・インジケーターバー
                const auto brf = barR.toFloat();
                g.setColour (Palette::track.withAlpha (0.85f));
                g.fillRoundedRectangle (brf, brf.getHeight() * 0.5f);
                g.setColour (Palette::panelLn.withAlpha (0.85f));
                g.drawRoundedRectangle (brf.reduced (0.5f), brf.getHeight() * 0.5f, 1.0f);

                const int numSegs = (barR.getWidth() < 64) ? 8 : 14;
                const float padX = 2.5f;
                const float segStep = (brf.getWidth() - padX * 2.0f) / (float) numSegs;
                const float segW = juce::jmax (2.2f, segStep - 1.6f);
                const float segH = brf.getHeight() - 3.2f;
                for (int s = 0; s < numSegs; ++s)
                {
                    const float sNorm = (float) (s + 1) / (float) numSegs;
                    const bool lit = (rows[i].norm >= sNorm - (0.5f / (float) numSegs));
                    juce::Colour segCol = rows[i].col;
                    if (rows[i].levelZone)
                    {
                        segCol = (sNorm > 0.88f) ? Palette::salmon
                               : (sNorm > 0.70f) ? Palette::yellow
                               : Palette::green;
                    }
                    const float sx = brf.getX() + padX + (float) s * segStep;
                    const float sy = brf.getY() + 1.6f;
                    g.setColour (lit ? segCol : segCol.withAlpha (0.22f));
                    g.fillRoundedRectangle (sx, sy, segW, segH, segH * 0.5f);
                    if (lit)
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.80f));
                        g.fillEllipse (sx + segW * 0.25f, sy + 1.0f, segW * 0.45f, 2.0f);
                    }
                }
            }
            else
            {
                // スタジオコンソール風 LEDバーグラフ (標準18セグメント / コンパクト幅10セグメント)
                const auto brf = barR.toFloat();
                g.setColour (pastelTheme() ? Palette::track.darker (0.08f) : Palette::bgTop.darker (0.30f).withAlpha (0.85f));
                g.fillRoundedRectangle (brf, 4.0f);
                g.setColour (Palette::panelLn.withAlpha (0.65f));
                g.drawRoundedRectangle (brf.reduced (0.5f), 4.0f, 1.0f);

                const int numSegs = (barR.getWidth() < 64) ? 10 : 18;
                const float padX = 2.0f;
                const float segStep = (brf.getWidth() - padX * 2.0f) / (float) numSegs;
                const float segW = juce::jmax (1.5f, segStep - 1.3f);
                for (int s = 0; s < numSegs; ++s)
                {
                    const float sNorm = (float) (s + 1) / (float) numSegs;
                    const bool lit = (rows[i].norm >= sNorm - (0.5f / (float) numSegs));
                    juce::Colour segCol = rows[i].col;
                    if (rows[i].levelZone)
                    {
                        segCol = (sNorm > 0.88f) ? Palette::salmon
                               : (sNorm > 0.70f) ? Palette::yellow
                               : Palette::green;
                    }
                    g.setColour (lit ? segCol : segCol.withAlpha (0.14f));
                    g.fillRoundedRectangle (brf.getX() + padX + (float) s * segStep, brf.getY() + 1.8f,
                                            segW, brf.getHeight() - 3.6f, 1.2f);
                }
            }
            g.setColour (Palette::inkSoft);
            fitText (g, rows[i].valTxt, valR, juce::Justification::centredRight, 14.0f, false);
        }
    }

    // ---- ★ ユニバーサル・アクセシビリティ専用：中段「視覚サポート案内 ＆ ワンタッチ4モード切替デッキ」 ----
    if (accessMode > 0 && overviewLayoutMode == 0)
    {
        const auto leftVista  = juce::Rectangle<float> ((float) tuner.getX(),          (float) (tuner.getBottom() + 5),
                                                        (float) tuner.getWidth(),      (float) (overviewCards[0].getY() - tuner.getBottom() - 10));
        const auto rightVista = juce::Rectangle<float> ((float) overviewOutput.getX(), (float) (overviewOutput.getBottom() + 5),
                                                        (float) overviewOutput.getWidth(), (float) (overviewCards[2].getY() - overviewOutput.getBottom() - 10));

        const float dnPct   = processor.apvts.getRawParameterValue ("denoise")->load();
        const float mudDb   = processor.apvts.getRawParameterValue ("mud")->load();
        const float compPct = processor.apvts.getRawParameterValue ("comp2")->load();
        const float revPct  = processor.apvts.getRawParameterValue ("revmix")->load();

        // 1. 左デッキ：スタイル別の視覚ガイド（◉くっきり大文字＝形状記号＆特大文字凡例 / ★かんたん3ステップ＝順路チャート / ●マット・具体表記＝フラット数値一覧）
        if (leftVista.getHeight() >= 48.0f)
        {
            g.setColour (Palette::panel);
            g.fillRoundedRectangle (leftVista, 12.0f);
            g.setColour ((accessMode == 1) ? Palette::yellow : Palette::panelLn);
            g.drawRoundedRectangle (leftVista.reduced (0.8f), 12.0f, (accessMode == 1) ? 2.2f : 1.4f);

            if (accessMode == 1)
            {
                // ◉ くっきり・大文字モード：色だけでなく「形（▼/▲/○）」と極太文字で判別できる高コントラスト案内
                g.setColour (Palette::yellow);
                fitText (g, language->T ("◉ くっきり・大文字モード（超高コントラスト・太針・形記号表示）",
                                         "◉ HIGH CONTRAST & LARGE TEXT MODE (BOLD NEEDLE & SHAPE ICONS)"),
                         leftVista.toNearestInt().reduced (12, 4).withHeight (24), juce::Justification::centredLeft, 15.0f, true);

                struct ShapeGuide { juce::String txt; juce::Colour col; };
                const ShapeGuide guides[3] = {
                    { language->T ("▼ 削る・抑える (雑音 ", "▼ CUT / CLEAN (") + juce::String (juce::roundToInt (dnPct)) + "%)", Palette::green },
                    { language->T ("▲ 足す・揃える (声量 ", "▲ BOOST / LEVEL (") + juce::String (juce::roundToInt (compPct)) + "%)", Palette::yellow },
                    { language->T ("○ 待機中 (0% / 変化なし)", "○ STANDBY (0%)"), Palette::inkSoft }
                };
                const float gGap = 6.0f;
                const float gW = (leftVista.getWidth() - 20.0f - gGap * 2.0f) / 3.0f;
                for (int gi = 0; gi < 3; ++gi)
                {
                    const juce::Rectangle<float> gr (leftVista.getX() + 10.0f + (float) gi * (gW + gGap), leftVista.getY() + 32.0f, gW, 34.0f);
                    g.setColour (Palette::bgTop);
                    g.fillRoundedRectangle (gr, 7.0f);
                    g.setColour (guides[gi].col);
                    g.drawRoundedRectangle (gr, 7.0f, 2.0f);
                    g.setColour (Palette::ink);
                    fitText (g, guides[gi].txt, gr.toNearestInt().reduced (6, 2), juce::Justification::centred, 14.5f, true);
                }
            }
            else if (accessMode == 2)
            {
                // ★ かんたん3ステップ・ガイドモード：次にどこを触ればいいか一目で分かる「STEP 1 ➔ STEP 2 ➔ STEP 3」順路チャート
                g.setColour (Palette::yellow);
                fitText (g, language->T ("★ かんたん3ステップ・ガイド（★印の主役ノブ3つを順番に回すだけで完成！）",
                                         "★ 3-STEP QUICK GUIDE (TURN THE 3 ★ HERO KNOBS IN ORDER)"),
                         leftVista.toNearestInt().reduced (12, 4).withHeight (22), juce::Justification::centredLeft, 14.5f, true);

                struct StepBox { juce::String step; juce::String desc; juce::Colour col; bool done; };
                const StepBox steps[3] = {
                    { "STEP 1", language->T ("★ノイズ除去で雑音消し", "1. Clean Noise"), Palette::green,  dnPct > 1.0f },
                    { "STEP 2", language->T ("★音量そろえで声量均一", "2. Level Voice"), Palette::blue,   compPct > 1.0f },
                    { "STEP 3", language->T ("★残響の量で余韻を足す", "3. Add Reverb"),  Palette::yellow, revPct > 1.0f }
                };
                const float sGap = 14.0f;
                const float sW = (leftVista.getWidth() - 20.0f - sGap * 2.0f) / 3.0f;
                for (int si = 0; si < 3; ++si)
                {
                    const juce::Rectangle<float> sr (leftVista.getX() + 10.0f + (float) si * (sW + sGap), leftVista.getY() + 30.0f, sW, 36.0f);
                    g.setColour (steps[si].done ? steps[si].col.withAlpha (0.20f) : Palette::bgTop);
                    g.fillRoundedRectangle (sr, 8.0f);
                    g.setColour (steps[si].col);
                    g.drawRoundedRectangle (sr, 8.0f, steps[si].done ? 2.0f : 1.2f);
                    g.setColour (steps[si].col);
                    fitText (g, steps[si].step, sr.toNearestInt().withHeight (16).reduced (6, 1), juce::Justification::centredLeft, 12.0f, true);
                    g.setColour (Palette::ink);
                    fitText (g, steps[si].desc, sr.toNearestInt().withTrimmedTop (15).reduced (6, 1), juce::Justification::centredLeft, 13.0f, true);
                    if (si < 2)
                    {
                        g.setColour (Palette::yellow);
                        fitText (g, juce::String::fromUTF8 ("▶"),
                                 juce::Rectangle<int> ((int) sr.getRight(), (int) sr.getY() + 6, (int) sGap, 24),
                                 juce::Justification::centred, 12.5f, true);
                    }
                }
            }
            else
            {
                // ● マット・具体表記モード：光やアニメを抑えた静かなフラット数値ステータス表
                g.setColour (Palette::ink);
                fitText (g, language->T ("● マット・具体表記モード（発光や点滅を抑えた配色 ／ 具体的な機能名で統一）",
                                         "● MATTE & CONCRETE LABELS MODE (LOW GLARE / LITERAL LABELS)"),
                         leftVista.toNearestInt().reduced (12, 4).withHeight (22), juce::Justification::centredLeft, 14.0f, true);

                struct CalmStat { juce::String label; juce::String val; bool active; };
                const CalmStat stats[4] = {
                    { language->T ("背景ノイズ除去", "Denoise"),   juce::String (juce::roundToInt (dnPct)) + "%",   dnPct > 1.0f },
                    { language->T ("中低域(250Hz)",  "250Hz EQ"),  juce::String (mudDb, 1) + "dB",                  std::abs (mudDb) > 0.1f },
                    { language->T ("音量の均一化",   "Leveling"),  juce::String (juce::roundToInt (compPct)) + "%", compPct > 1.0f },
                    { language->T ("残響ブレンド",   "Reverb"),    juce::String (juce::roundToInt (revPct)) + "%",  revPct > 1.0f }
                };
                const float cGap = 6.0f;
                const float cW = (leftVista.getWidth() - 20.0f - cGap * 3.0f) / 4.0f;
                for (int ci = 0; ci < 4; ++ci)
                {
                    const juce::Rectangle<float> cr (leftVista.getX() + 10.0f + (float) ci * (cW + cGap), leftVista.getY() + 30.0f, cW, 36.0f);
                    g.setColour (Palette::bgTop);
                    g.fillRoundedRectangle (cr, 6.0f);
                    g.setColour (stats[ci].active ? Palette::green : Palette::panelLn);
                    g.drawRoundedRectangle (cr, 6.0f, stats[ci].active ? 1.5f : 1.0f);
                    g.setColour (Palette::inkSoft);
                    fitText (g, stats[ci].label, cr.toNearestInt().withHeight (17).reduced (4, 1), juce::Justification::centred, 12.0f, false);
                    g.setColour (stats[ci].active ? Palette::ink : Palette::inkSoft);
                    fitText (g, stats[ci].val, cr.toNearestInt().withTrimmedTop (16).reduced (4, 1), juce::Justification::centred, 13.5f, stats[ci].active);
                }
            }
        }

        // 2. 右デッキ：画面の見やすさ・操作スタイル 4モード・ワンタッチ切替バー（いつでも1クリックで切替可能）
        if (rightVista.getHeight() >= 48.0f)
        {
            g.setColour (Palette::panel);
            g.fillRoundedRectangle (rightVista, 12.0f);
            g.setColour ((accessMode == 1) ? Palette::ice : Palette::panelLn);
            g.drawRoundedRectangle (rightVista.reduced (0.8f), 12.0f, (accessMode == 1) ? 2.2f : 1.4f);

            const juce::String rightHeader = (accessMode == 1)
                ? language->T ("画面の見やすさ・操作スタイル切替（現在：◉ くっきり大文字）", "DISPLAY & CONTROL STYLE (HIGH CONTRAST)")
                : (accessMode == 2)
                ? language->T ("画面の見やすさ・操作スタイル切替（現在：★ かんたん3ステップ）", "DISPLAY & CONTROL STYLE (3-STEP GUIDE)")
                : language->T ("画面の見やすさ・操作スタイル切替（現在：● マット・具体表記）", "DISPLAY & CONTROL STYLE (MATTE & CONCRETE)");
            g.setColour (Palette::ink);
            fitText (g, rightHeader, rightVista.toNearestInt().reduced (12, 5).withHeight (22), juce::Justification::centredLeft, 14.0f, true);

            const char* modeJa[4] = { "標準デザイン", "◉ くっきり大文字", "★ かんたん3ステップ", "● マット・具体表記" };
            const char* modeEn[4] = { "Standard",     "◉ High Contrast",  "★ 3-Step Guide",      "● Matte & Concrete" };
            const juce::Colour modeCols[4] = { Palette::inkSoft, Palette::yellow, Palette::blue, Palette::green };
            for (int a = 0; a < 4; ++a)
            {
                const auto pr = accessPillRects[(size_t) a];
                if (pr.isEmpty()) continue;
                const auto prf = pr.toFloat();
                const bool isCur = (accessMode == a);
                g.setColour (isCur ? modeCols[a].withAlpha ((accessMode == 3) ? 0.28f : 0.35f) : Palette::bgTop);
                g.fillRoundedRectangle (prf, 7.0f);
                g.setColour (isCur ? modeCols[a] : Palette::panelLn);
                g.drawRoundedRectangle (prf.reduced (0.5f), 7.0f, isCur ? 2.2f : 1.1f);
                g.setColour (isCur ? Palette::ink : Palette::inkSoft);
                fitText (g, juce::String::fromUTF8 (language->english ? modeEn[a] : modeJa[a]),
                         pr.reduced (4, 1), juce::Justification::centred, (accessMode == 1) ? 14.0f : 13.0f, isCur);
            }
        }
    }

    // ---- ✦ 風景・季節コレクション専用：中段「水平線・星空パノラマウィンドウ (Open Sky & Sea Horizon Vista)」 ----
    if (oceanStarTheme() && accessMode == 0 && overviewLayoutMode == 0 && ! liteMode)
    {
        const auto leftVista  = juce::Rectangle<float> ((float) tuner.getX(),          (float) (tuner.getBottom() + 5),
                                                        (float) tuner.getWidth(),      (float) (overviewCards[0].getY() - tuner.getBottom() - 10));
        const auto rightVista = juce::Rectangle<float> ((float) overviewOutput.getX(), (float) (overviewOutput.getBottom() + 5),
                                                        (float) overviewOutput.getWidth(), (float) (overviewCards[2].getY() - overviewOutput.getBottom() - 10));

        // 1. 左パノラマ窓：背景の絶景を素通しで見せつつ世界観ごとの象徴モチーフを添える
        if (leftVista.getHeight() >= 48.0f)
        {
            g.setGradientFill (juce::ColourGradient (Palette::bgTop.withAlpha (0.12f), leftVista.getX(), leftVista.getY(),
                                                     Palette::mirrorBot.withAlpha (0.18f), leftVista.getX(), leftVista.getBottom(), false));
            g.fillRoundedRectangle (leftVista, 13.0f);
            g.setColour (Palette::metalMid.withAlpha (0.62f));
            g.drawRoundedRectangle (leftVista.reduced (0.6f), 13.0f, 1.2f);
            g.setColour (Palette::metalHi.withAlpha (0.28f));
            g.drawRoundedRectangle (leftVista.reduced (3.2f), 10.5f, 0.8f);

            const float hy = leftVista.getBottom() - 15.0f;
            if (themeMode == 15)
            {
                // 🌊 02 瑞々しい碧海: パノラマ水紋コースティクス＆ぷるんと浮かぶ8つの立体3D水滴・気泡
                ThemePainter::drawCausticWaveMesh (g, leftVista.reduced (6.0f, 4.0f), Palette::ice, 0.36f, 5);
                for (int d = 0; d < 8; ++d)
                {
                    const float dx = leftVista.getX() + 28.0f + (leftVista.getWidth() - 56.0f) * ((float) d / 7.0f);
                    const float dy = leftVista.getY() + 26.0f + std::sin ((float) d * 1.5f) * 12.0f;
                    const float dr = (d % 2 == 0) ? 7.2f : 4.6f;
                    ThemePainter::drawWaterDroplet (g, dx, dy, dr, (d % 3 == 0) ? Palette::green : Palette::ice, 0.90f);
                }
            }
            else if (themeMode == 16)
            {
                // 🌅 03 朝焼けの海: 水平線から昇る太陽コロナ・光芒＆朝焼け彩雲
                const float sunCx = leftVista.getCentreX();
                for (int ray = -3; ray <= 3; ++ray)
                {
                    const float rx = sunCx + (float) ray * 42.0f;
                    g.setColour (juce::Colour (0x55ffea9f));
                    g.drawLine (sunCx, hy, rx, leftVista.getY() + 18.0f, 2.2f);
                }
                {
                    juce::Graphics::ScopedSaveState ss (g);
                    g.reduceClipRegion (leftVista.toNearestInt().withBottom ((int) hy));
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff), sunCx, hy - 4.0f,
                                                             juce::Colour (0xffff9e00), sunCx + 24.0f, hy, true));
                    g.fillEllipse (sunCx - 22.0f, hy - 18.0f, 44.0f, 36.0f);
                }
                for (int cl = 0; cl < 4; ++cl)
                {
                    const float cx = leftVista.getX() + 65.0f + (float) cl * 105.0f;
                    const float cy = leftVista.getY() + 34.0f + (cl % 2) * 8.0f;
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0x88ff6b8b), cx, cy - 8.0f,
                                                             juce::Colour (0xccffbe0b), cx, cy + 8.0f, false));
                    g.fillRoundedRectangle (cx - 38.0f, cy - 6.0f, 76.0f, 12.0f, 6.0f);
                }
                g.setColour (juce::Colour (0xffffea9f).withAlpha (0.85f));
                g.drawLine (leftVista.getX() + 16.0f, hy, leftVista.getRight() - 16.0f, hy, 1.8f);
            }
            else if (themeMode == 17)
            {
                // 🍁 04 晩秋から初冬へ: 白い朝靄・川霧と舞い散る紅葉（🍁）＆初雪の結晶（❄）
                g.setGradientFill (juce::ColourGradient (juce::Colour (0x48e2e8f0), leftVista.getCentreX(), hy - 10.0f,
                                                         juce::Colours::transparentWhite, leftVista.getCentreX(), hy + 10.0f, false));
                g.fillRect (leftVista.getX() + 10.0f, hy - 12.0f, leftVista.getWidth() - 20.0f, 22.0f);
                for (int m = 0; m < 7; ++m)
                {
                    const float px = leftVista.getX() + 36.0f + (leftVista.getWidth() - 72.0f) * ((float) m / 6.0f);
                    const float py = leftVista.getY() + 33.0f + std::sin ((float) m * 1.7f) * 10.0f;
                    if (m % 2 == 0)
                        ThemePainter::drawMapleLeaf (g, px, py, 11.2f, (float) m * 0.7f,
                                                     (m % 4 == 0) ? juce::Colour (0xffff2e4c) : juce::Colour (0xffff9e00), 0.96f);
                    else
                        ThemePainter::drawSnowCrystal (g, px, py, 7.8f, (float) m * 0.5f, juce::Colour (0xffe0f2fe), 0.94f);
                }
            }
            else
            {
                // 🌌 01 天の川銀河 / 👑 05 蒼海オペラ座: 星座アストロラーベ天球図＆流星
                g.setGradientFill (juce::ColourGradient (juce::Colours::transparentWhite, leftVista.getX() + 12.0f, hy,
                                                         Palette::metalHi.withAlpha (0.55f), leftVista.getCentreX(), hy, false));
                g.fillRect (leftVista.getX() + 12.0f, hy, leftVista.getWidth() * 0.5f - 12.0f, 1.1f);
                g.setGradientFill (juce::ColourGradient (Palette::metalHi.withAlpha (0.55f), leftVista.getCentreX(), hy,
                                                         juce::Colours::transparentWhite, leftVista.getRight() - 12.0f, hy, false));
                g.fillRect (leftVista.getCentreX(), hy, leftVista.getWidth() * 0.5f - 12.0f, 1.1f);

                const float stX[7] = { 0.08f, 0.21f, 0.36f, 0.51f, 0.66f, 0.80f, 0.92f };
                const float stY[7] = { 0.64f, 0.44f, 0.56f, 0.30f, 0.48f, 0.36f, 0.60f };
                juce::Path constel;
                for (int s = 0; s < 7; ++s)
                {
                    const float px = leftVista.getX() + leftVista.getWidth() * stX[s];
                    const float py = leftVista.getY() + leftVista.getHeight() * stY[s];
                    if (s == 0) constel.startNewSubPath (px, py);
                    else        constel.lineTo (px, py);
                }
                g.setColour (Palette::metalHi.withAlpha (0.52f));
                g.strokePath (constel, juce::PathStrokeType (1.2f));
                for (int s = 0; s < 7; ++s)
                {
                    const float px = leftVista.getX() + leftVista.getWidth() * stX[s];
                    const float py = leftVista.getY() + leftVista.getHeight() * stY[s];
                    ThemePainter::drawStar8 (g, px, py, (s == 3) ? 6.8f : 4.8f, (s % 2 == 0) ? Palette::yellow : Palette::ice);
                }
            }

            const juce::String themePoem = (themeMode == 14) ? language->T ("🌌 01 星降る天の川銀河と蛍火の夜 — GALACTIC MILKY WAY", "🌌 01 GALACTIC MILKY WAY & FIREFLY")
                                         : (themeMode == 15) ? language->T ("🌊 02 瑞々しい碧海と水紋ラグーン — LUSH AQUA CAUSTICS", "🌊 02 LUSH AQUA SEA & CAUSTICS")
                                         : (themeMode == 16) ? language->T ("🌅 03 朝焼けの海と暁の水平線 — RADIANT DAWN SUNRISE", "🌅 03 RADIANT DAWN & SUNRISE SEA")
                                         : (themeMode == 17) ? language->T ("🍁 04 晩秋から初冬へ・茜空と初雪の水鏡 — AUTUMN TO WINTER", "🍁 04 LATE AUTUMN & FIRST SNOW")
                                                             : language->T ("👑 05 蒼海オペラ座と黄金シャンデリア — GRAND OPERA SAPPHIRE", "👑 05 GRAND OPERA SAPPHIRE");
            const juce::Rectangle<float> leftTitlePill (leftVista.getX() + 8.0f, leftVista.getY() + 4.0f, leftVista.getWidth() - 136.0f, 20.0f);
            g.setColour (Palette::bgTop.darker (0.48f).withAlpha (0.72f));
            g.fillRoundedRectangle (leftTitlePill, 9.0f);
            g.setColour (Palette::ink.withAlpha (0.98f));
            fitText (g, themePoem, leftTitlePill.toNearestInt().reduced (8, 0), juce::Justification::centredLeft, 14.0f, true);

            // 右上のインタラクティブ・タッチオーラバッジ
            const juce::Rectangle<float> auraBadge (leftVista.getRight() - 122.0f, leftVista.getY() + 4.0f, 112.0f, 20.0f);
            g.setColour (Palette::bgTop.darker (0.45f).withAlpha (0.82f));
            g.fillRoundedRectangle (auraBadge, 10.0f);
            g.setColour (Palette::metalHi.withAlpha (0.72f));
            g.drawRoundedRectangle (auraBadge, 10.0f, 1.0f);
            g.setColour (Palette::yellow);
            static const char* const kMoodBadge[3] = { "✦ TOUCH AURA", "✦ PRISM WAVE", "✦ DEEP ECHO" };
            fitText (g, juce::String::fromUTF8 (kMoodBadge[g_vistaMood % 3]), auraBadge.toNearestInt().reduced (4, 0), juce::Justification::centred, 14.0f, true);

            // ★ 音の変化ビジュアライザー (Before 原音ノイズ波形 vs After 整音・美声波形 ＆ 4連リアルタイム効果カプセル)
            {
                const float dnPct   = processor.apvts.getRawParameterValue ("denoise")->load();
                const float mudDb   = processor.apvts.getRawParameterValue ("mud")->load();
                const float compPct = processor.apvts.getRawParameterValue ("comp2")->load();
                const float velvPct = juce::jmax (vVal, fVal);

                // 中段：Before（点線の荒れた原音波形）→ After（滑らかに整音・声量均一化された発光波形）のリアルタイム比較
                const float waveY = leftVista.getY() + 40.0f;
                const float waveX0 = leftVista.getX() + 14.0f;
                const float waveW  = leftVista.getWidth() - 28.0f;
                juce::Path rawWave, cleanWave;
                for (int wi = 0; wi <= 56; ++wi)
                {
                    const float t = (float) wi / 56.0f;
                    const float wx = waveX0 + waveW * t;
                    const float env = std::sin (t * juce::MathConstants<float>::pi);
                    // 原音（ノイズのギザギザ＋音量ムラ）
                    const float jitter = std::sin (t * 85.0f) * 3.2f + std::cos (t * 43.0f) * 2.4f;
                    const float rawDy  = (std::sin (t * 18.0f) * 8.5f + jitter) * env;
                    // 処理後（雑音が消え、声量が揃い、倍音の艶が乗った滑らかな正弦波）
                    const float cleanAmp = 7.2f * (0.75f + 0.25f * (compPct * 0.01f));
                    const float cleanDy  = std::sin (t * 18.0f + 0.25f) * cleanAmp * env;
                    if (wi == 0)
                    {
                        rawWave.startNewSubPath (wx, waveY + rawDy);
                        cleanWave.startNewSubPath (wx, waveY + cleanDy);
                    }
                    else
                    {
                        rawWave.lineTo (wx, waveY + rawDy);
                        cleanWave.lineTo (wx, waveY + cleanDy);
                    }
                }
                g.setColour (Palette::inkSoft.withAlpha (0.45f));
                g.strokePath (rawWave, juce::PathStrokeType (1.1f));
                g.setColour (Palette::green.withAlpha (0.32f));
                g.strokePath (cleanWave, juce::PathStrokeType (4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setGradientFill (juce::ColourGradient (Palette::green, waveX0, waveY,
                                                         Palette::yellow, waveX0 + waveW, waveY, false));
                g.strokePath (cleanWave, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                // 下段：今どの処理がどれだけ効いているか一目で分かる4連「音の変化カプセル」
                struct EffChip { juce::String text; float norm; juce::Colour col; };
                const EffChip chips[4] = {
                    { language->T ("🧹雑音 ", "🧹NR ")   + juce::String (juce::roundToInt (dnPct)) + "%",
                      juce::jlimit (0.0f, 1.0f, dnPct / 60.0f), Palette::green },
                    { language->T ("💎こもり ", "💎MUD ") + juce::String (mudDb, 1) + "dB",
                      juce::jlimit (0.0f, 1.0f, std::abs (mudDb) / 6.0f), Palette::ice },
                    { language->T ("⚖声量 ", "⚖COMP ")  + juce::String (juce::roundToInt (compPct)) + "%",
                      juce::jlimit (0.0f, 1.0f, compPct / 80.0f), Palette::blue },
                    { language->T ("✨倍音 ", "✨GLOW ")  + juce::String (juce::roundToInt (velvPct)) + "%",
                      juce::jlimit (0.0f, 1.0f, velvPct / 90.0f), Palette::yellow }
                };
                const float chipGap = 5.0f;
                const float chipW = (leftVista.getWidth() - 16.0f - chipGap * 3.0f) / 4.0f;
                const float chipY = leftVista.getBottom() - 25.0f;
                for (int cIdx = 0; cIdx < 4; ++cIdx)
                {
                    const float cx = leftVista.getX() + 8.0f + (float) cIdx * (chipW + chipGap);
                    const juce::Rectangle<float> cr (cx, chipY, chipW, 20.0f);
                    const bool on = chips[cIdx].norm > 0.04f;
                    g.setColour (Palette::bgTop.darker (0.55f).withAlpha (0.86f));
                    g.fillRoundedRectangle (cr, 6.0f);
                    if (on)
                    {
                        g.setColour (chips[cIdx].col.withAlpha (0.30f));
                        g.fillRoundedRectangle (cr.getX() + 2.0f, cr.getBottom() - 3.2f, (cr.getWidth() - 4.0f) * chips[cIdx].norm, 2.2f, 1.0f);
                    }
                    g.setColour (on ? chips[cIdx].col.withAlpha (0.85f) : Palette::panelLn.withAlpha (0.45f));
                    g.drawRoundedRectangle (cr, 6.0f, on ? 1.2f : 0.8f);
                    g.setColour (on ? Palette::ink : Palette::inkSoft);
                    fitText (g, chips[cIdx].text, cr.toNearestInt().reduced (4, 0), juce::Justification::centred, 14.0f, on);
                }
            }
        }

        // 2. 右パノラマ窓：水鏡リフレクション音響波紋＋32バンド・ウォーターミラー倍音ファウンテン＋立体空間の広がりカプセル
        if (rightVista.getHeight() >= 48.0f)
        {
            g.setGradientFill (juce::ColourGradient (Palette::bgTop.withAlpha (0.12f), rightVista.getX(), rightVista.getY(),
                                                     Palette::mirrorBot.withAlpha (0.20f), rightVista.getX(), rightVista.getBottom(), false));
            g.fillRoundedRectangle (rightVista, 13.0f);
            g.setColour (Palette::metalMid.withAlpha (0.62f));
            g.drawRoundedRectangle (rightVista.reduced (0.6f), 13.0f, 1.2f);
            g.setColour (Palette::metalHi.withAlpha (0.28f));
            g.drawRoundedRectangle (rightVista.reduced (3.2f), 10.5f, 0.8f);

            const float wy = rightVista.getY() + rightVista.getHeight() * 0.48f;
            const float wx0 = rightVista.getX() + 18.0f;
            const float ww  = rightVista.getWidth() - 36.0f;
            const float amp = rightVista.getHeight() * 0.24f;

            const float revMixPct = processor.apvts.getRawParameterValue ("revmix")->load();
            const float dlyPct    = processor.apvts.getRawParameterValue ("delay")->load();
            const float widPct    = processor.apvts.getRawParameterValue ("width")->load();

            // ★ 立体音場ウイング＆やまびこリング（Width・Reverb・Delayの効き具合で左右の翼と残響リングが広がる！）
            {
                const float vcx = rightVista.getCentreX();
                const float wingSpan = 42.0f + juce::jlimit (0.0f, 1.0f, std::abs (widPct) / 80.0f) * (ww * 0.42f);
                g.setGradientFill (juce::ColourGradient (Palette::ice.withAlpha (0.34f), vcx, wy,
                                                         Palette::ice.withAlpha (0.0f),  vcx - wingSpan, wy, false));
                g.fillEllipse (vcx - wingSpan, wy - 14.0f, wingSpan * 2.0f, 28.0f);
                if (dlyPct > 1.0f)
                {
                    for (int ec = 1; ec <= 3; ++ec)
                    {
                        const float er = 22.0f + (float) ec * 26.0f * (0.4f + 0.6f * (dlyPct / 60.0f));
                        g.setColour (Palette::yellow.withAlpha (0.42f / (float) ec));
                        g.drawEllipse (vcx - er, wy - er * 0.28f, er * 2.0f, er * 0.56f, 1.2f);
                    }
                }
            }

            juce::Path skyWave, seaMirror;
            skyWave.startNewSubPath (wx0, wy);
            seaMirror.startNewSubPath (wx0, wy);
            for (int p = 0; p <= 48; ++p)
            {
                const float t = (float) p / 48.0f;
                const float px = wx0 + ww * t;
                const float env = std::sin (t * juce::MathConstants<float>::pi);
                const float wv  = (0.52f * std::sin (t * 18.8f + vVal * 0.04f)
                                 + 0.30f * std::cos (t * 31.4f - sVal * 0.03f)
                                 + 0.18f * std::sin (t * 9.4f)) * env;
                const float dy = std::abs (wv) * amp + 2.0f * env;
                skyWave.lineTo (px, wy - dy);
                seaMirror.lineTo (px, wy + dy * 0.78f);
            }
            skyWave.lineTo (wx0 + ww, wy);
            skyWave.closeSubPath();
            seaMirror.lineTo (wx0 + ww, wy);
            seaMirror.closeSubPath();

            g.setGradientFill (juce::ColourGradient (Palette::yellow.withAlpha (0.48f), rightVista.getCentreX(), wy - amp,
                                                     Palette::ice.withAlpha (0.12f),    rightVista.getCentreX(), wy, false));
            g.fillPath (skyWave);
            g.setColour (Palette::yellow.withAlpha (0.85f));
            g.strokePath (skyWave, juce::PathStrokeType (1.3f));

            g.setGradientFill (juce::ColourGradient (Palette::salmon.withAlpha (0.38f), rightVista.getCentreX(), wy,
                                                     Palette::blue.withAlpha (0.08f),   rightVista.getCentreX(), wy + amp, false));
            g.fillPath (seaMirror);
            g.setColour (Palette::ice.withAlpha (0.65f));
            g.strokePath (seaMirror, juce::PathStrokeType (1.0f));

            // ★ 32バンド・シンメトリック水鏡スペクトラム・ファウンテン（水平線の上下に光の柱が鏡面反射する）
            for (int b = 0; b < 32; ++b)
            {
                const float t = ((float) b + 0.5f) / 32.0f;
                const float bx = wx0 + ww * t;
                const float env = std::sin (t * juce::MathConstants<float>::pi);
                const float bh  = (0.28f + 0.72f * std::abs (std::sin (t * 15.7f + (vVal + fVal) * 0.03f))) * amp * 0.85f * env;
                const juce::Colour bCol = (b % 3 == 0) ? Palette::yellow : (b % 2 == 0) ? Palette::ice : Palette::salmon;
                g.setColour (bCol.withAlpha (0.62f));
                g.fillRoundedRectangle (bx - 1.5f, wy - bh, 3.0f, bh, 1.2f);
                g.setColour (bCol.withAlpha (0.32f));
                g.fillRoundedRectangle (bx - 1.5f, wy + 1.5f, 3.0f, bh * 0.68f, 1.2f);
            }

            g.setColour (Palette::metalHi.withAlpha (0.85f));
            g.drawLine (wx0, wy, wx0 + ww, wy, 1.4f);

            if (themeMode == 15)
            {
                for (int b = 0; b < 5; ++b)
                    ThemePainter::drawWaterDroplet (g, wx0 + 40.0f + (float) b * 82.0f, wy - 6.0f + (b % 2) * 12.0f,
                                                    4.2f + (b % 2) * 2.2f, Palette::green, 0.85f);
            }
            else if (themeMode == 17)
            {
                g.setColour (juce::Colour (0x77fde6c4));
                g.drawEllipse (rightVista.getCentreX() - 48.0f, wy + 2.0f, 96.0f, 14.0f, 1.1f);
                ThemePainter::drawMapleLeaf   (g, rightVista.getCentreX() - 12.0f, wy + 7.0f, 7.5f, 0.9f, juce::Colour (0xffe11d48), 0.90f);
                ThemePainter::drawSnowCrystal (g, rightVista.getRight() - 42.0f, wy - 14.0f, 6.2f, 0.3f, juce::Colour (0xffe0f2fe), 0.85f);
            }

            const juce::String rightTitle = (themeMode == 15) ? language->T ("💧 水紋コースティクス ＆ 瑞々しい倍音ストリーム", "💧 AQUA CAUSTICS & HARMONIC STREAM")
                                          : (themeMode == 16) ? language->T ("🌅 朝日の光の道 ＆ 暁の倍音リフレクション", "🌅 SUNRISE GOLDEN PATH & HARMONICS")
                                          : (themeMode == 17) ? language->T ("🍁 散り紅葉と初雪が溶け合う静寂の水鏡波紋", "🍁 FALLING MAPLE & FIRST SNOW RIPPLES")
                                                              : language->T ("🌊 水鏡の倍音・残響リフレクション — SEA-MIRROR HORIZON", "🌊 SEA-MIRROR HARMONIC HORIZON");
            const juce::Rectangle<float> rightTitlePill (rightVista.getX() + 8.0f, rightVista.getY() + 4.0f, rightVista.getWidth() - 136.0f, 20.0f);
            g.setColour (Palette::bgTop.darker (0.48f).withAlpha (0.72f));
            g.fillRoundedRectangle (rightTitlePill, 9.0f);
            g.setColour (Palette::ink.withAlpha (0.98f));
            fitText (g, rightTitle, rightTitlePill.toNearestInt().reduced (8, 0), juce::Justification::centredLeft, 14.0f, true);

            const juce::Rectangle<float> rtaBadge (rightVista.getRight() - 122.0f, rightVista.getY() + 4.0f, 112.0f, 20.0f);
            g.setColour (Palette::bgTop.darker (0.45f).withAlpha (0.82f));
            g.fillRoundedRectangle (rtaBadge, 10.0f);
            g.setColour (Palette::metalHi.withAlpha (0.72f));
            g.drawRoundedRectangle (rtaBadge, 10.0f, 1.0f);
            g.setColour (Palette::ice);
            fitText (g, juce::String::fromUTF8 ("✦ MIRROR 32ch"), rtaBadge.toNearestInt().reduced (4, 0), juce::Justification::centred, 14.0f, true);

            // 下段：空間・残響・やまびこ・左右の広がりがどう効いているか一目で伝わる3連「空間変化カプセル」
            {
                struct SpChip { juce::String text; float norm; juce::Colour col; };
                const SpChip spChips[3] = {
                    { language->T ("🏛残響余韻 ", "🏛REV ")  + juce::String (juce::roundToInt (revMixPct)) + "%",
                      juce::jlimit (0.0f, 1.0f, revMixPct / 50.0f), Palette::yellow },
                    { language->T ("🔊やまびこ ", "🔊ECHO ") + juce::String (juce::roundToInt (dlyPct)) + "%",
                      juce::jlimit (0.0f, 1.0f, dlyPct / 55.0f), Palette::salmon },
                    { language->T ("↔左右拡大 +", "↔WIDE +") + juce::String (juce::roundToInt (widPct)) + "%",
                      juce::jlimit (0.0f, 1.0f, std::abs (widPct) / 70.0f), Palette::ice }
                };
                const float chipGap = 6.0f;
                const float chipW = (rightVista.getWidth() - 16.0f - chipGap * 2.0f) / 3.0f;
                const float chipY = rightVista.getBottom() - 25.0f;
                for (int cIdx = 0; cIdx < 3; ++cIdx)
                {
                    const float cx = rightVista.getX() + 8.0f + (float) cIdx * (chipW + chipGap);
                    const juce::Rectangle<float> cr (cx, chipY, chipW, 20.0f);
                    const bool on = spChips[cIdx].norm > 0.04f;
                    g.setColour (Palette::bgTop.darker (0.55f).withAlpha (0.86f));
                    g.fillRoundedRectangle (cr, 6.0f);
                    if (on)
                    {
                        g.setColour (spChips[cIdx].col.withAlpha (0.30f));
                        g.fillRoundedRectangle (cr.getX() + 2.0f, cr.getBottom() - 3.2f, (cr.getWidth() - 4.0f) * spChips[cIdx].norm, 2.2f, 1.0f);
                    }
                    g.setColour (on ? spChips[cIdx].col.withAlpha (0.85f) : Palette::panelLn.withAlpha (0.45f));
                    g.drawRoundedRectangle (cr, 6.0f, on ? 1.2f : 0.8f);
                    g.setColour (on ? Palette::ink : Palette::inkSoft);
                    fitText (g, spChips[cIdx].text, cr.toNearestInt().reduced (4, 0), juce::Justification::centred, 14.0f, on);
                }
            }
        }

        // 3. ★ インタラクティブ・タッチ水紋＆パーティクルバースト描画（クリック・ドラッグ位置から広がる波紋と世界観粒子）
        const juce::uint32 nowMs = juce::Time::getMillisecondCounter();
        bool anyRipple = false;
        for (const auto& rp : g_ripples)
            if (rp.active) anyRipple = true;
        if (! anyRipple && leftVista.getHeight() >= 48.0f)
        {
            g_ripples[0] = { leftVista.getCentreX() + 28.0f,  leftVista.getY() + leftVista.getHeight() * 0.54f,  nowMs - 480u, 1u, true };
            g_ripples[1] = { rightVista.getCentreX() - 36.0f, rightVista.getY() + rightVista.getHeight() * 0.50f, nowMs - 720u, 4u, true };
        }
        for (auto& rp : g_ripples)
        {
            if (! rp.active) continue;
            const float ageSec = (float) (nowMs - rp.birthMs) * 0.001f;
            if (ageSec < 0.0f || ageSec > 1.85f)
            {
                rp.active = false;
                continue;
            }
            const float prog  = ageSec / 1.85f;
            const float alpha = (1.0f - prog) * (1.0f - prog);
            const float rad1  = 10.0f + prog * 78.0f;
            const float rad2  = 4.0f  + prog * 46.0f;
            g.setColour (Palette::ice.withAlpha (alpha * 0.65f));
            g.drawEllipse (rp.x - rad1, rp.y - rad1 * 0.55f, rad1 * 2.0f, rad1 * 1.10f, 1.6f);
            g.setColour (Palette::yellow.withAlpha (alpha * 0.55f));
            g.drawEllipse (rp.x - rad2, rp.y - rad2 * 0.55f, rad2 * 2.0f, rad2 * 1.10f, 1.2f);
            for (int p = 0; p < 4; ++p)
            {
                const float pa = (float) p * juce::MathConstants<float>::halfPi + prog * 2.2f + (float) (rp.seed % 7);
                const float px = rp.x + std::cos (pa) * (12.0f + prog * 48.0f);
                const float py = rp.y + std::sin (pa) * (8.0f  + prog * 28.0f) - prog * 16.0f;
                if (themeMode == 15)
                    ThemePainter::drawWaterDroplet (g, px, py, 4.2f * (1.0f - prog * 0.4f), Palette::ice, alpha * 0.90f);
                else if (themeMode == 17)
                {
                    if (p % 2 == 0)
                        ThemePainter::drawMapleLeaf (g, px, py, 6.8f, pa, juce::Colour (0xffe11d48), alpha * 0.90f);
                    else
                        ThemePainter::drawSnowCrystal (g, px, py, 5.5f, pa, juce::Colour (0xffe0f2fe), alpha * 0.90f);
                }
                else
                    ThemePainter::drawStar8 (g, px, py, 5.2f * (1.0f - prog * 0.3f), Palette::yellow.withAlpha (alpha));
            }
        }
    }

    const char* namesJaStd[]      = { "01 整える",           "02 声をつくる",       "03 空間をつくる" };
    const char* namesJaStep[]     = { "STEP 1 雑音を消す",   "STEP 2 声を揃える",   "STEP 3 残響を足す" };
    const char* namesJaConcrete[] = { "01 雑音・不要帯域除去", "02 音量均一・周波数", "03 残響・空間幅" };
    const char* namesEn[]         = { "01 CLEAN",            "02 CHARACTER",        "03 SPACE" };
    // V-Liver時は 01=ピーチコーラルチーク, 02=モーヴアイシャドウ, 03=ベリーローズティント のコスメ3色パレットに統一
    const juce::Colour accents[] = {
        Palette::green,
        (isLiverStyle && accessMode == 0) ? Palette::yellow : Palette::blue,
        (isLiverStyle && accessMode == 0) ? Palette::blue   : Palette::yellow
    };

    // ★ Red Dot 仕様: セクションカード間を結ぶ「オプティカル信号フロー・ライトパイプ (01 ➔ 02 ➔ 03)」
    if (! liteMode && accessMode != 3)
    {
        auto drawBridge = [&] (const juce::Rectangle<int>& cL, const juce::Rectangle<int>& cR, juce::Colour colL, juce::Colour colR)
        {
            const float x0 = (float) cL.getRight() + 1.0f;
            const float x1 = (float) cR.getX() - 1.0f;
            const float ym = (float) (cL.getY() + 26);
            if (x1 > x0 + 2.0f && (x1 - x0) <= 28.0f)
            {
                g.setGradientFill (juce::ColourGradient (colL.withAlpha (0.85f), x0, ym,
                                                         colR.withAlpha (0.85f), x1, ym, false));
                g.fillRoundedRectangle (x0, ym - 2.0f, x1 - x0, 4.0f, 2.0f);
                const float xm = (x0 + x1) * 0.5f;
                g.setColour (juce::Colours::white.withAlpha (0.90f));
                g.fillEllipse (xm - 2.2f, ym - 2.2f, 4.4f, 4.4f);
            }
        };
        drawBridge (overviewCards[0], overviewCards[1], accents[0], accents[1]);
        drawBridge (overviewCards[1], overviewCards[2], accents[1], accents[2]);
        if (! overviewMonitor.isEmpty())
        {
            drawBridge (overviewCards[1], overviewMonitor, accents[1], Palette::salmon);
            drawBridge (overviewMonitor, overviewCards[2], Palette::salmon, accents[2]);
        }
    }

    Knob* const cardKnobs[3][4] = {
        { &denoiseK, &gate,   &lowCut,    &mudK    },
        { &comp2K,   &deessK, &presenceK, &warmthK },
        { &revMixK,  &revSizeK, &delayK,  &widthK  }
    };

    for (int i = 0; i < 3; ++i)
    {
        auto r = overviewCards[(size_t) i];
        card (r, accents[i]);

        // 各カード内の4つのツマミが「いま音を変えているか (effInt > 0.02f)」を集計
        float kEff[4] = {};
        int   kDir[4] = {};
        juce::String kTagJa[4], kTagEn[4];
        int activeKnobCount = 0;
        float sumEff = 0.0f;
        for (int kIdx = 0; kIdx < 4; ++kIdx)
        {
            auto& sl = cardKnobs[i][kIdx]->slider;
            const float pos = (float) sl.valueToProportionOfLength (sl.getValue());
            GzzioLnF::queryKnobEffect (sl, pos, kEff[kIdx], kDir[kIdx], kTagJa[kIdx], kTagEn[kIdx]);
            if (! sl.isEnabled()) kEff[kIdx] = 0.0f;
            if (kEff[kIdx] > 0.02f)
            {
                ++activeKnobCount;
                sumEff += kEff[kIdx];
            }
        }

        // ★ 3ウイング配置 / 天球・水鏡ホライズン配置 (overviewLayoutMode == 0): ペルソナ別にノブ台座＆軌道を装飾
        //   さらに「効いているツマミ」の台座だけオーラ発光し、「待機中(0% / 0dB / -80dB)」の台座は静かに沈む！
        if ((overviewLayoutMode == 0 || (accessMode > 0 && overviewLayoutMode != 2)) && ! liteMode)
        {
            if (accessMode > 0)
            {
                // ★ 見やすさ・操作スタイルモード専用の水平4連ノブ台座＆優先度コールアウト描画
                auto body = r.reduced ((i == 1) ? 12 : 10).withTrimmedTop (120);
                const int cw = body.getWidth() / 4;
                for (int kIdx = 0; kIdx < 4; ++kIdx)
                {
                    const int kx0 = body.getX() + kIdx * cw;
                    const int kw  = (kIdx == 3) ? (body.getRight() - kx0) : cw;
                    const auto cellF = juce::Rectangle<int> (kx0, body.getY(), kw, body.getHeight()).reduced (3, 1).toFloat();
                    const bool kOn = (kEff[kIdx] > 0.02f);
                    const bool isPrimaryHero = (kIdx == 0) || (i == 1 && kIdx == 2);

                    if (accessMode == 1)
                    {
                        // ◉ くっきり大文字モード：背景と明確に分離する高コントラスト・ソリッド枠台座
                        g.setColour (Palette::bgTop);
                        g.fillRoundedRectangle (cellF, 8.0f);
                        const juce::Colour bCol = kOn ? ((kDir[kIdx] < 0) ? Palette::green : Palette::yellow) : Palette::panelLn.withAlpha (0.55f);
                        g.setColour (bCol);
                        g.drawRoundedRectangle (cellF.reduced (0.5f), 8.0f, kOn ? 2.2f : 1.1f);
                    }
                    else if (accessMode == 2)
                    {
                        // ★ かんたん3ステップモード：主役ノブ（★まずここ）を強調枠で囲み、待機ノブは静かに減光
                        if (isPrimaryHero || kOn)
                        {
                            g.setColour (kOn ? accents[i].withAlpha (0.14f) : Palette::panel2.withAlpha (0.65f));
                            g.fillRoundedRectangle (cellF, 9.0f);
                            g.setColour (kOn ? accents[i] : Palette::yellow.withAlpha (0.75f));
                            g.drawRoundedRectangle (cellF.reduced (0.6f), 9.0f, isPrimaryHero ? 2.0f : 1.4f);
                        }
                        if (kIdx == 0)
                        {
                            // 各ステップで一番最初に触るべき主役ノブの最上段（ラベルの上）に「★まずここ」ピルを表示
                            const juce::Rectangle<float> starBadge (cellF.getX() + 6.0f, cellF.getY() + 2.5f, cellF.getWidth() - 12.0f, 14.5f);
                            g.setColour (Palette::yellow);
                            g.fillRoundedRectangle (starBadge, 4.0f);
                            g.setColour (Palette::bgTop);
                            fitText (g, language->T ("★まずここ", "★START HERE"), starBadge.toNearestInt().reduced (2, 0), juce::Justification::centred, 11.0f, true);
                        }
                    }
                    else
                    {
                        // ● マット・具体表記モード：完全水平・均一なマット枠台座（光・グラデーション一切なし）
                        g.setColour (Palette::bgTop.withAlpha (0.65f));
                        g.fillRoundedRectangle (cellF, 6.0f);
                        g.setColour (kOn ? accents[i].withAlpha (0.85f) : Palette::panelLn.withAlpha (0.55f));
                        g.drawRoundedRectangle (cellF.reduced (0.5f), 6.0f, kOn ? 1.5f : 1.0f);
                    }
                }
            }
            else if (oceanStarTheme())
            {
                // ✦ 風景・季節コレクション専用：4基のノブを結ぶ軌道と世界観別エフェクト（水紋・紅葉・雪・星雲・朝日）
                auto body = r.reduced ((i == 1) ? 12 : 10).withTrimmedTop (120);
                const int cw = body.getWidth() / 4;
                const int dy[4] = { 10, -4, -4, 10 };
                juce::Path arcOrbit;
                for (int kIdx = 0; kIdx < 4; ++kIdx)
                {
                    const float kx = (float) (body.getX() + kIdx * cw + cw / 2);
                    const float ky = (float) (body.getY() + 24 + (body.getHeight() - 46) / 2 + dy[kIdx]);
                    if (kIdx == 0) arcOrbit.startNewSubPath (kx, ky);
                    else           arcOrbit.lineTo (kx, ky);

                    const float haloR = (i == 1) ? 42.0f : 36.0f;
                    const bool kOn = (kEff[kIdx] > 0.02f);
                    const juce::Colour kCol = (kDir[kIdx] < 0) ? accents[i].interpolatedWith (Palette::ice, 0.35f)
                                                               : accents[i].interpolatedWith (Palette::yellow, 0.35f);
                    if (kOn)
                    {
                        g.setGradientFill (juce::ColourGradient (kCol.withAlpha (0.28f + 0.22f * kEff[kIdx]), kx, ky - haloR,
                                                                 Palette::mirrorBot.withAlpha (0.34f), kx, ky + haloR, false));
                        g.fillEllipse (kx - haloR, ky - haloR, haloR * 2.0f, haloR * 2.0f);
                        g.setColour (kCol.withAlpha (0.72f + 0.25f * kEff[kIdx]));
                        g.drawEllipse (kx - haloR, ky - haloR, haloR * 2.0f, haloR * 2.0f, 1.5f);
                        // 効いているノブの外周に広がる音響波紋リング
                        g.setColour (kCol.withAlpha (0.36f * kEff[kIdx]));
                        g.drawEllipse (kx - haloR - 4.5f, ky - haloR - 4.5f, (haloR + 4.5f) * 2.0f, (haloR + 4.5f) * 2.0f, 1.1f);
                    }
                    else
                    {
                        // 待機中（0% / -80dB / 0dB）は静かなスリープ台座
                        g.setColour (Palette::bgTop.darker (0.45f).withAlpha (0.38f));
                        g.fillEllipse (kx - haloR, ky - haloR, haloR * 2.0f, haloR * 2.0f);
                        g.setColour (Palette::panelLn.withAlpha (0.28f));
                        g.drawEllipse (kx - haloR, ky - haloR, haloR * 2.0f, haloR * 2.0f, 0.9f);
                    }
                }
                g.setColour (Palette::metalHi.withAlpha (0.45f));
                g.strokePath (arcOrbit, juce::PathStrokeType (1.2f));
                for (int kIdx = 0; kIdx < 3; ++kIdx)
                {
                    const float mx = (float) (body.getX() + (kIdx + 1) * cw);
                    const float my = (float) (body.getY() + 24 + (body.getHeight() - 46) / 2 + (dy[kIdx] + dy[kIdx + 1]) / 2);
                    if (themeMode == 15)
                        ThemePainter::drawWaterDroplet (g, mx, my, 4.8f, Palette::ice, 0.88f);
                    else if (themeMode == 17)
                    {
                        if (kIdx % 2 == 0)
                            ThemePainter::drawMapleLeaf (g, mx, my, 6.2f, 0.4f, juce::Colour (0xffe11d48), 0.86f);
                        else
                            ThemePainter::drawSnowCrystal (g, mx, my, 5.4f, 0.2f, juce::Colour (0xffe0f2fe), 0.86f);
                    }
                    else
                        drawSparkle4 (mx, my, 4.4f, Palette::metalHi.withAlpha (0.82f));
                }
            }
            else
            {
                const bool softPedestal = (isLiverStyle || isYuruStyle || pastelTheme());
                if (i == 0 || i == 2)
                {
                    auto body = r.reduced (14).withTrimmedTop (122);
                    const int heroW = body.getWidth() * 51 / 100;
                    const auto heroRect = body.removeFromLeft (heroW).reduced (2).toFloat();
                    body.removeFromLeft (6);
                    const bool heroOn = (kEff[0] > 0.02f);

                    if (softPedestal)
                    {
                        // ♥🍰 プレストチーク・サテンクッション台座＆貴金属リム（効いている時は華やかに発光）
                        g.setGradientFill (juce::ColourGradient (accents[i].withAlpha (heroOn ? (0.26f + 0.16f * kEff[0]) : 0.08f), heroRect.getX(), heroRect.getY(),
                                                                 Palette::panel2.withAlpha (0.84f), heroRect.getCentreX(), heroRect.getBottom(), false));
                        g.fillRoundedRectangle (heroRect, 16.0f);
                        g.setColour (isLiverStyle ? Palette::metalMid.withAlpha (heroOn ? 0.95f : 0.45f) : accents[i].withAlpha (heroOn ? 0.72f : 0.28f));
                        g.drawRoundedRectangle (heroRect.reduced (0.8f), 16.0f, heroOn ? 1.8f : 1.1f);
                        g.setColour ((themeMode == 11) ? Palette::metalHi.withAlpha (0.35f) : juce::Colours::white.withAlpha (0.80f));
                        g.drawRoundedRectangle (heroRect.reduced (3.0f), 13.5f, 1.0f);

                        const int satH = body.getHeight() / 3;
                        for (int s = 0; s < 3; ++s)
                        {
                            const bool satOn = (kEff[s + 1] > 0.02f);
                            const auto satR = juce::Rectangle<int> (body.getX(), body.getY() + s * satH, body.getWidth(), satH).reduced (2, 3).toFloat();
                            g.setGradientFill (juce::ColourGradient (
                                satOn ? accents[i].withAlpha (0.22f + 0.14f * kEff[s + 1])
                                      : (nightTheme() ? Palette::panel2.darker (0.15f).withAlpha (0.68f) : Palette::panel2.withAlpha (0.68f)),
                                satR.getX(), satR.getY(),
                                Palette::panel2.withAlpha (0.88f), satR.getX(), satR.getBottom(), false));
                            g.fillRoundedRectangle (satR, 12.0f);
                            g.setColour (satOn ? accents[i].withAlpha (0.85f)
                                               : (isLiverStyle ? Palette::metalMid.withAlpha (0.38f) : Palette::panelLn.withAlpha (0.45f)));
                            g.drawRoundedRectangle (satR, 12.0f, satOn ? 1.6f : 1.0f);
                            drawHeartShape (satR.getX() + 6.5f, satR.getCentreY(), 3.8f,
                                            satOn ? accents[i].withAlpha (0.95f) : Palette::inkSoft.withAlpha (0.35f));
                        }
                    }
                    else
                    {
                        // 左側: 大型主役ノブ専用の8角形凹みリアクター台座
                        const auto pedPath = makeChamferPath (heroRect, 10.0f);
                        g.setGradientFill (juce::ColourGradient (Palette::bgTop.darker (0.28f).withAlpha (0.68f), heroRect.getX(), heroRect.getY(),
                                                                 accents[i].withAlpha (heroOn ? (0.18f + 0.18f * kEff[0]) : 0.04f), heroRect.getCentreX(), heroRect.getBottom(), false));
                        g.fillPath (pedPath);
                        g.setColour (heroOn ? accents[i].withAlpha (0.68f) : Palette::panelLn.withAlpha (0.35f));
                        g.strokePath (pedPath, juce::PathStrokeType (heroOn ? 1.5f : 1.0f));

                        // 右側: 3連サテライトノブのハードウェア・モジュールカプセル枠（効いているモジュールだけ左端LEDバーが鮮烈に点灯！）
                        const int satH = body.getHeight() / 3;
                        for (int s = 0; s < 3; ++s)
                        {
                            const bool satOn = (kEff[s + 1] > 0.02f);
                            const auto satR = juce::Rectangle<int> (body.getX(), body.getY() + s * satH, body.getWidth(), satH).reduced (2, 3).toFloat();
                            g.setColour (Palette::bgTop.darker (satOn ? 0.18f : 0.35f).withAlpha (satOn ? 0.72f : 0.48f));
                            g.fillRoundedRectangle (satR, 8.0f);
                            g.setColour (satOn ? accents[i].withAlpha (0.68f) : Palette::panelLn.withAlpha (0.35f));
                            g.drawRoundedRectangle (satR, 8.0f, satOn ? 1.3f : 0.9f);
                            g.setColour (satOn ? accents[i].brighter (0.20f).withAlpha (0.95f) : Palette::inkSoft.withAlpha (0.22f));
                            g.fillRoundedRectangle (satR.getX() + 3.0f, satR.getY() + 8.0f, satOn ? 3.2f : 2.0f, satR.getHeight() - 16.0f, 1.4f);
                        }
                    }
                }
                else if (i == 1)
                {
                    // 中央下 Card 1: 大・小段違い4連コンソールの主役台座＆信号コンジットライン
                    auto body = r.reduced (12).withTrimmedTop (122);
                    const int wHero = body.getWidth() * 27 / 100;
                    const int wSub  = (body.getWidth() - wHero * 2) / 2;
                    const int ws[4] = { wHero, wSub, wHero, wSub };
                    const bool isH[4] = { true, false, true, false };
                    int cx = body.getX();
                    g.setColour (accents[i].withAlpha (0.32f));
                    g.drawHorizontalLine (body.getCentreY() + 10, (float) body.getX() + 24.0f, (float) body.getRight() - 24.0f);
                    for (int kIdx = 0; kIdx < 4; ++kIdx)
                    {
                        const bool kOn = (kEff[kIdx] > 0.02f);
                        if (isH[kIdx])
                        {
                            const auto hr = juce::Rectangle<int> (cx, body.getY(), ws[kIdx], body.getHeight()).reduced (3, 2).toFloat();
                            g.setColour (softPedestal ? accents[i].withAlpha (kOn ? 0.22f : 0.08f)
                                                      : Palette::bgTop.darker (0.24f).withAlpha (kOn ? 0.65f : 0.42f));
                            g.fillRoundedRectangle (hr, softPedestal ? 15.0f : 10.0f);
                            g.setColour (kOn ? (isLiverStyle ? Palette::metalMid : accents[i].withAlpha (0.68f))
                                             : Palette::panelLn.withAlpha (0.35f));
                            g.drawRoundedRectangle (hr, softPedestal ? 15.0f : 10.0f, kOn ? 1.5f : 1.0f);
                        }
                        cx += ws[kIdx];
                    }
                }
            }
        }

        // ♥ V-LIVER STAGE 配置では下段12連コスメパレットの背後に貴金属の五線譜とカメオ装飾＋効いているノブのオーラハローを添える
        if (overviewLayoutMode == 1 && ! liteMode && accessMode == 0)
        {
            g.setColour ((isLiverStyle ? Palette::metalMid : accents[i]).withAlpha (0.22f));
            const int staffY = r.getY() + 190;
            for (int sl = 0; sl < 5; ++sl)
                g.drawHorizontalLine (staffY + sl * 9, (float) r.getX() + 14.0f, (float) r.getRight() - 14.0f);
            for (int n = 0; n < 4; ++n)
            {
                const float nx = (float) r.getX() + 56.0f + (float) n * ((float) (r.getWidth() - 112) / 3.0f);
                const float ny = (float) staffY + (float) ((n + i) % 4) * 9.0f;
                const bool kOn = (kEff[n] > 0.02f);
                if (kOn)
                {
                    const auto sb = cardKnobs[i][n]->slider.getBounds().toFloat();
                    const float scx = sb.getCentreX(), scy = sb.getCentreY() - 10.0f;
                    g.setGradientFill (juce::ColourGradient (accents[i].withAlpha (0.26f * kEff[n]), scx, scy,
                                                             accents[i].withAlpha (0.0f), scx + 42.0f, scy, true));
                    g.fillEllipse (scx - 42.0f, scy - 42.0f, 84.0f, 84.0f);
                }
                g.setColour (accents[i].withAlpha (kOn ? 0.55f : 0.25f));
                g.fillEllipse (nx - 4.2f, ny - 3.2f, 8.4f, 6.4f);
                g.drawLine (nx + 3.6f, ny, nx + 3.6f, ny - 16.0f, 1.4f);
                g.drawLine (nx + 3.6f, ny - 16.0f, nx + 9.0f, ny - 12.5f, 1.4f);
                if (n % 2 == 0)
                    drawHeartShape (nx + 24.0f, (float) staffY - 6.0f, 4.8f, accents[i].withAlpha (kOn ? 0.65f : 0.30f));
                else
                    drawSparkle4 (nx + 24.0f, (float) staffY + 42.0f, 4.8f, Palette::metalMid.withAlpha (kOn ? 0.80f : 0.45f));
            }
        }
        else if (overviewLayoutMode >= 2 && ! liteMode && accessMode == 0)
        {
            for (int n = 0; n < 4; ++n)
            {
                const auto sb = cardKnobs[i][n]->slider.getBounds().toFloat();
                const float scx = sb.getCentreX(), scy = sb.getCentreY() - 10.0f;
                const float rad = juce::jmin (sb.getWidth(), sb.getHeight()) * 0.44f;
                if (kEff[n] > 0.02f)
                {
                    g.setGradientFill (juce::ColourGradient (accents[i].withAlpha (0.24f * kEff[n]), scx, scy,
                                                             accents[i].withAlpha (0.0f), scx + rad, scy, true));
                    g.fillEllipse (scx - rad, scy - rad, rad * 2.0f, rad * 2.0f);
                }
            }
        }

        const int pwrX = overviewSectionPower[(size_t) i].getX();
        const int titleMaxW = (overviewLayoutMode == 2) ? juce::jmax (60, pwrX - (r.getX() + 30))
                            : (accessMode == 2 || accessMode == 3) ? (i == 1 ? 168 : 150)
                                                                   : (i == 1 ? 138 : 112);
        auto titleR = juce::Rectangle<int> (r.getX() + 26, r.getY() + 8, titleMaxW, 34);
        g.setColour ((accessMode == 2) ? accents[i] : Palette::ink);
        const char* curJa = (accessMode == 2) ? namesJaStep[i] : (accessMode == 3) ? namesJaConcrete[i] : namesJaStd[i];
        fitText (g, juce::String::fromUTF8 (language->english ? namesEn[i] : curJa),
                 titleR, juce::Justification::centredLeft, (accessMode == 1) ? 19.0f : 18.0f, true);

        // ★ 各セクション見出し横の「リアルタイム音変化ステータス・ピル」:
        //   そのセクションで今どんな変化が起きているか（雑音・こもり除去 / 声量均一＆倍音 / 残響＆やまびこ）を視覚表示！
        const int badgeX = titleR.getRight() + 4;
        const int badgeW = pwrX - badgeX - 6;
        if (badgeW >= 68 && ! liteMode)
        {
            const juce::Rectangle<float> stPill ((float) badgeX, (float) (r.getY() + 13), (float) badgeW, 24.0f);
            const bool secActive = (activeKnobCount > 0);
            const float avgEff = secActive ? juce::jlimit (0.15f, 1.0f, sumEff / (float) activeKnobCount) : 0.0f;
            const juce::String secSummary = (accessMode == 3)
                ? (secActive ? language->T ("● 動作中", "● ACTIVE") : language->T ("○ 待機中", "○ STANDBY"))
                : (accessMode == 1)
                ? (i == 0 ? language->T ("▼ 雑音・こもりカット", "▼ CUT")
                 : i == 1 ? language->T ("▲ 声量・明るさ補正", "▲ TONE")
                          : language->T ("▲ 残響・広がり付加", "▲ SPACE"))
                : (i == 0)
                ? (badgeW >= 118 ? language->T ("🧹 雑音・こもり除去", "🧹 CLEAN ACTIVE") : language->T ("🧹 整音中", "🧹 CLEAN"))
                : (i == 1)
                ? (badgeW >= 150 ? language->T ("✨ 声量均一＆美声ツヤ", "✨ LEVEL & TONE") : language->T ("✨ 美声中", "✨ TONE"))
                : (badgeW >= 118 ? language->T ("🌊 残響・空間拡大", "🌊 3D SPACE ON")     : language->T ("🌊 空間ON", "🌊 SPACE"));

            g.setColour (pastelTheme() && ! nightTheme() && accessMode == 0
                         ? (secActive ? accents[i].withAlpha (0.22f) : Palette::panel2.withAlpha (0.75f))
                         : Palette::bgTop.darker (0.48f).withAlpha (0.92f));
            g.fillRoundedRectangle (stPill, (accessMode == 3) ? 6.0f : 12.0f);
            if (secActive && accessMode != 3)
            {
                g.setColour (accents[i].withAlpha (0.35f));
                g.fillRoundedRectangle (stPill.getX() + 8.0f, stPill.getBottom() - 3.2f,
                                        (stPill.getWidth() - 16.0f) * avgEff, 2.2f, 1.1f);
            }
            g.setColour (secActive ? accents[i].withAlpha (0.90f) : Palette::panelLn.withAlpha (0.55f));
            g.drawRoundedRectangle (stPill, (accessMode == 3) ? 6.0f : 12.0f, (accessMode == 1) ? 1.8f : (secActive ? 1.2f : 0.8f));
            g.setColour (secActive ? Palette::ink : Palette::inkSoft);
            fitText (g, secSummary, stPill.toNearestInt().reduced (6, 0), juce::Justification::centred, 14.0f, secActive);
        }
    }

    // ---- 🌊 03 SPACE カード内: 実空間IR 音響減衰カーブ・オシロスコープ＆外部IR(.wav/.aiff)読込カプセル ----
    if (! irOverviewRect.isEmpty())
    {
        const auto irF = irOverviewRect.toFloat();
        const int revType = (int) processor.apvts.getRawParameterValue ("rev_type")->load();
        const bool isHeya = (revType >= kHeyaFirst);
        const bool hasCustom = processor.hasCustomIr();
        const auto accentCol = hasCustom ? Palette::ice : (isHeya ? (isLiverStyle ? Palette::salmon : Palette::yellow) : Palette::inkSoft);
        const float irRad = (isLiverStyle || isYuruStyle) ? irF.getHeight() * 0.5f : 7.5f;

        if (liteMode)
        {
            g.setColour (isHeya ? accentCol.withAlpha (0.24f) : Palette::panel2);
            g.fillRoundedRectangle (irF, irRad);
        }
        else
        {
            g.setColour (pastelTheme() ? Palette::panel2.brighter (0.04f).withAlpha (0.92f)
                                       : Palette::bgTop.darker (0.28f).withAlpha (0.88f));
            g.fillRoundedRectangle (irF, irRad);

            float irCurve[24] {};
            processor.getReverbIrCurve (irCurve, 24);
            juce::Path waveStroke, waveFill;
            const float wx0 = irF.getX() + 6.0f;
            const float ww  = juce::jmax (10.0f, irF.getWidth() - 12.0f);
            const float wyBot = irF.getBottom() - 2.5f;
            const float wh  = juce::jmax (6.0f, irF.getHeight() - 7.0f);
            waveFill.startNewSubPath (wx0, wyBot);
            for (int p = 0; p < 24; ++p)
            {
                const float px = wx0 + ww * (float) p / 23.0f;
                const float py = wyBot - wh * juce::jlimit (0.0f, 1.0f, irCurve[p]);
                if (p == 0) waveStroke.startNewSubPath (px, py);
                else        waveStroke.lineTo (px, py);
                waveFill.lineTo (px, py);
            }
            waveFill.lineTo (wx0 + ww, wyBot);
            waveFill.closeSubPath();

            g.setColour (accentCol.withAlpha (isHeya ? 0.30f : 0.16f));
            g.fillPath (waveFill);
            g.setColour (accentCol.withAlpha (isHeya ? 0.85f : 0.45f));
            g.strokePath (waveStroke, juce::PathStrokeType (isHeya ? 1.5f : 1.0f));
        }

        const float duckPct = processor.apvts.getRawParameterValue ("duck")->load();
        const float duckDb  = processor.getSpaceDuckDb();
        if (duckPct > 1.0f && ! liteMode)
        {
            const float duckRatio = juce::jlimit (0.05f, 1.0f, (duckDb > 0.2f ? duckDb / 12.0f : duckPct / 100.0f * 0.35f));
            auto dBar = irF.reduced (6.0f, 0.0f);
            dBar = dBar.withTop (irF.getY() + 2.0f).withHeight (2.2f);
            g.setColour (Palette::yellow.withAlpha (duckDb > 0.2f ? 0.90f : 0.45f));
            g.fillRoundedRectangle (dBar.withWidth (dBar.getWidth() * duckRatio), 1.1f);
        }

        g.setColour (isHeya ? accentCol : (duckPct > 1.0f ? Palette::yellow.withAlpha (0.75f) : Palette::panelLn));
        g.drawRoundedRectangle (irF.reduced (0.5f), irRad, (isHeya || duckPct > 1.0f) ? 1.5f : 1.0f);
        g.setColour ((isHeya || duckPct > 1.0f) ? Palette::ink : Palette::inkSoft);
        const bool compactIr = (irOverviewRect.getWidth() < 110);
        const juce::String irLabel = hasCustom
            ? (compactIr ? language->T ("IR:外部", "IR:WAV") : language->T ("IR:外部読込", "IR: CUSTOM"))
            : (duckPct > 1.0f)
                ? (compactIr ? language->T ("IR/声優先", "IR/DUCK") : language->T ("IR・声優先", "IR + DUCK"))
                : isHeya
                    ? (compactIr ? language->T ("IR波形", "IR ON") : language->T ("IR波形・読込", "IR WAVE"))
                    : (compactIr ? language->T ("IR読込", "IR...") : language->T ("IR切替・読込", "IR LOAD"));
        fitText (g, irLabel, irOverviewRect.reduced (4, 0), juce::Justification::centred, 14.0f, isHeya || duckPct > 1.0f);
    }

    // パステル・ライバーテーマではぷにぐじマスコットとジュースサーバーも総合画面に優しく添える（軽量モード時は省略）
    if (! liteMode)
    {
        if (pastelTheme() && kawaiiMascot.isValid())
        {
            const float mh = 180.0f;
            const float mw = mh * (float) kawaiiMascot.getWidth() / (float) juce::jmax (1, kawaiiMascot.getHeight());
            g.setOpacity (nightTheme() ? 0.14f : 0.18f);
            g.drawImage (kawaiiMascot,
                         juce::Rectangle<float> ((float) getWidth() - mw - 72.0f, (float) getHeight() - mh - 14.0f, mw, mh));
            g.setOpacity (1.0f);
        }
        drawJuiceServer (g);
    }

    g.setColour (Palette::panel2.withMultipliedAlpha (cardAlpha));
    g.fillRoundedRectangle (overviewHelp.toFloat(), 12.0f);
    g.setColour (Palette::panelLn);
    g.drawRoundedRectangle (overviewHelp.toFloat().reduced (0.5f), 12.0f, 1.0f);
   #if VOCALGZZIO_TRIAL
    const auto defaultHint = language->T ("体験版：60秒ごとに0.6秒だけ音量が下がります。中央の丸や4本のバーで声の温かさ・芯・明るさ・広がりを調整できます。操作部にカーソルを合わせると説明が表示されます。", "Trial: sound dips for 0.6s every 60s. Use the orb or four bars for warmth, focus, air and stereo space. Hover over a control for help.");
   #else
    const auto defaultHint = language->T ("中央の丸や4本のバーで、声の温かさ・芯・明るさ・広がりを調整できます。操作部にカーソルを合わせると使い方が表示されます。「押して原音と比較」で効果を聴き比べてください。", "Use the orb or four bars for warmth, focus, air and stereo space. Hover over a control for help, and hold Compare to hear the unprocessed input.");
   #endif
    const auto hint = dnMsgTicks > 0 && infoText.isNotEmpty() ? infoText
                    : overviewHint.isNotEmpty() ? overviewHint : defaultHint;
    g.setColour (Palette::ink);
    g.setFont (GzzioLnF::uiFont (18.0f * juce::jlimit (1.0f, 1.3f, fontScale / 1.5f), false));
    g.drawFittedText (hint, overviewHelp.reduced (18, 6), juce::Justification::centredLeft, 2, 1.0f);
}
