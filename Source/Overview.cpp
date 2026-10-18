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
            else if (isShowing())
            {
                if (i == 1 && ! (liverTheme() || themeMode == 1 || themeMode == 8))
                    setThemeMode (lastCoutureTheme);
                else if (i == 2 && themeMode != 9)
                    setThemeMode (9);
                else if (i == 0 && (liverTheme() || themeMode == 9))
                    setThemeMode (0);
            }
            const char* archHintsJa[4] = {
                "【★ ARGON DECK】中央に3D回転トラッカーボールと4軸トリガーを据えた、近未来SFコックピット配置です。",
                "【♥ LIVER STAGE】クチュール・クッションパクト球体＆12連コスメパレットを並べた、Vライバー・歌枠ステージ配置です（もう一度押すか中央右上の5色ピルでコスメ配色をワンタッチ切替できます）。",
                "【🎛 STUDIO RACK】左から右へ信号経路が並ぶ、プロ音楽家向け5列ヴィンテージ・アナログコンソール配置です。",
                "【◎ CLASSIC】上部に大型チューナー、下部に3枚のセクションカードを並べた従来の3カード配置です。"
            };
            const char* archHintsEn[4] = {
                "[ARGON DECK] Next-gen 3-wing sci-fi cockpit centered around the 3D Trackball and 4-axis trigger engine.",
                "[LIVER STAGE] Couture Cosmetics stage panorama with Cushion Compact sphere, 12-dial ribbon, and 5 one-click colorways.",
                "[STUDIO RACK] 5-column vertical hardware channel strip following strict left-to-right console signal flow.",
                "[CLASSIC] Traditional 3-card overview layout."
            };
            overviewHint = language->T (archHintsJa[i], archHintsEn[i]);
            return true;
        }
    }

    // 1a. 💄 クチュール・コスメ 5色ワンタッチ切替ピル (01 ロゼ / 02 氷紫 / 03 黒薔薇 / 04 ミント / 05 白桃)
    for (int c = 0; c < 5; ++c)
    {
        if (! coutureSwatchRects[(size_t) c].isEmpty() && coutureSwatchRects[(size_t) c].contains (pt))
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
            return true;
        }
    }

    // 1b. ヘッダの「⚡軽量」ワンクリック省負荷ピル
    if (! litePillRect.isEmpty() && litePillRect.contains (pt))
    {
        setLiteMode (! liteMode);
        return true;
    }

    // 1c. ヘッダの「🎯中央寄せ / MONO」ルーティング設定ピル
    if (! routePillRect.isEmpty() && routePillRect.contains (pt))
    {
        openRoutePopup (localAreaToGlobal (routePillRect));
        overviewHint = language->T ("【🎯 中央寄せ / MONO】片耳だけマイクが聴こえる問題を解消し、入力1/2の中央寄せや完全モノラル出力・全出力ミラーリングを設定します。",
                                    "[CENTER / MONO] Fixes single-sided mic input by centering Input 1/2 to L+R or forcing full mono output across all channels.");
        return true;
    }

    // 1d. ヘッダの「📡外部出力 (SYNCROOM / VB-CABLE)」ブリッジ設定ピル
    if (! streamPillRect.isEmpty() && streamPillRect.contains (pt))
    {
        openStreamOutPopup (localAreaToGlobal (streamPillRect));
        overviewHint = language->T ("【📡 外部出力 (SYNCROOM / VB-CABLE)】ASIOで自分の声を聴きながら、Yamaha SYNCROOM Driver (WDM) や VB-CABLE へ処理後の声を同時出力します。",
                                    "[SYNC OUT] Bridges processed audio directly into Yamaha SYNCROOM Driver (WDM) or VB-CABLE while keeping your main interface on ASIO.");
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
        handleOverviewMouseDrag (pt);
        return true;
    }

    // 4. Argon Velvet Engine 4本のダイレクトバー (VELVET / FOCUS / SILK / HALO)
    for (int i = 0; i < 4; ++i)
    {
        if (! argonBarRects[(size_t) i].isEmpty() && argonBarRects[(size_t) i].expanded (0, 2).contains (pt))
        {
            argonDragging = i + 1;
            handleOverviewMouseDrag (pt);
            return true;
        }
    }

    return false;
}

void VocalGzzioContent::handleOverviewMouseDrag (juce::Point<int> pt)
{
    if (argonDragging == 0 && ! argonPadRect.isEmpty())
    {
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
    if (routePillRect.contains (pt))
        return language->T ("マイクが片耳（左だけ・右だけ）から聴こえる問題を解消します。入力1/2の中央寄せ(L+R)や、最終出力の完全モノラル化・出力3-4等へのミラーリングを設定します。",
                            "Fixes single-sided mic audio by centering Input 1 or Input 2 to both L+R, or forcing full mono output across all output pairs.");
    if (streamPillRect.contains (pt))
        return language->T ("ASIOで自分の声を聴きながら、「Yamaha SYNCROOM Driver (WDM)」や「VB-CABLE」等の外部デバイスへ処理後の声を同時に流し、SYNCROOMやOBSへ直接渡します。",
                            "Simultaneously streams processed audio to external virtual devices like 'Yamaha SYNCROOM Driver (WDM)' or 'VB-CABLE' while keeping your main interface on ASIO.");
    if (irOverviewRect.contains (pt))
        return language->T ("実空間IR（インパルス応答）残響の音響減衰カーブと声優先ダッキングを表示します。クリックすると内蔵6部屋のIR切替・外部IRファイル(.wav/.aiff)の読込・歌っている最中の残響自動回避（声優先ダッキング）が選べます。",
                            "Displays the IR convolution decay curve and vocal-priority space ducking. Click to switch between 6 built-in rooms, load a custom IR (.wav/.aiff), or set vocal-priority ducking.");
    if (talkPillRect.contains (pt))
        return language->T ("【歌枠⇄雑談MC 瞬間切替】クリックすると歌設定（残響・エコー・ハモリ・広がり）を一切壊さず一時ミュートし、曲間のトークでもお風呂場にならないクリアなMC声へワンタッチで切り替えます。",
                            "[Singing / MC Talk Toggle] Non-destructively mutes reverb, echo, harmony, and stereo width for clean between-song MC talk while preserving 100% of your singing settings for instant return.");
    if (loopPillRect.contains (pt))
        return language->T ("【5秒録音・手放し試聴ルーパー】1回押すとマイクの素の声を5秒間録音し、直後から自動ループ再生します。骨伝導のない客観的な自分の声を聴きながら、両手で仕上がりや2Dオーブ・原音比較を追い込めます。",
                            "[5s Quick Audition Looper] Click once to record 5 seconds of your dry voice, then automatically loop it through the DSP chain so you can tweak settings and compare raw vs. processed hands-free.");
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
        label (inGainK, "入力音量", "Input"); label (makeupK, "出力音量", "Output");
        label (denoiseK, "ノイズ除去", "Noise reduction"); label (gate, "無音時のカット", "Noise gate");
        label (lowCut, "低音カット", "Low cut"); label (mudK, "こもり", "Low mids");
        label (comp2K, "音量そろえ", "Leveling"); label (deessK, "サ行おさえ", "De-essing");
        label (presenceK, "声の明るさ", "Presence"); label (airK, "きらめき", "Air");
        label (warmthK, "厚み・歪み", "Warmth"); label (resK, "耳ざわり", "Resonance");
        label (revMixK, "残響の量", "Reverb mix"); label (revSizeK, "残響の長さ", "Reverb size");
        label (delayK, "やまびこの量", "Delay mix"); label (widthK, "左右の広がり", "Stereo width");
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
    talkPillRect = {};
    loopPillRect = {};
    for (auto& r : argonBarRects) r = {};
    for (auto& r : argonTargetRects) r = {};
    for (auto& r : coutureSwatchRects) r = {};

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

    // ---- 2段目: 仕上がり・音の種類・ワンクリック自動設定・A/B比較・歌枠⇄雑談MC・5秒試聴ルーパー・原音比較・初期化 ----
    const int barY = 72, barH = 40;
    finishPresetBox.setBounds (116, barY, 232, barH);
    srcModeLabelArea = { 352, barY, 84, barH };
    srcModeBox     .setBounds (440, barY, 152, barH);
    autoSetupButton.setBounds (600, barY, 134, barH);
    songSetupButton.setBounds (740, barY, 134, barH);
    abA            .setBounds (882, barY, 42,  barH);
    abB            .setBounds (928, barY, 42,  barH);
    abCopy         .setBounds (974, barY, 62,  barH);
    talkPillRect   = { 1044, barY + 3, 102, barH - 6 };
    loopPillRect   = { 1152, barY + 3, 106, barH - 6 };
    overviewCompare.setBounds (1266, barY, 188, barH);
    resetButton    .setBounds (1460, barY, w - edge - 1460, barH);

    auto grid = [this] (juce::Rectangle<int> r, std::initializer_list<Knob*> knobs, int columns)
    {
        const int count = (int) knobs.size(), rows = (count + columns - 1) / columns;
        const int cw = r.getWidth() / columns, ch = r.getHeight() / rows;
        int i = 0;
        for (auto* k : knobs)
        {
            auto cell = juce::Rectangle<int> (r.getX() + i % columns * cw, r.getY() + i / columns * ch, cw, ch).reduced (4, 2);
            k->label.getProperties().set ("fontH", 13.5);
            k->label.setBounds (cell.removeFromTop (30));
            k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (98, cell.getWidth()), 24);
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

    const int top = 122, bottom = h - 92;

    if (overviewLayoutMode == 0)
    {
        // =========================================================================
        // ★ Architecture 0: ARGON COCKPIT (次世代3ウイング・コックピット配置)
        //   左ウイング: ピッチ＆クリーン (Tuner + 01 CLEAN: 大型主役DENOISE＋3連サテライト)
        //   中央コア  : 3D回転トラッカーボール & 4軸トリガーフェーダー + 大・小段違い4連コンソール (02 CORE TONE)
        //   右ウイング: 5ch テレメトリ計器盤＆マスターI/O + 3D空間 (03 SPACE: 大型主役REVERB＋3連サテライト)
        // =========================================================================
        const int wingW = 456;
        const int centerW = w - edge * 2 - wingW * 2 - gap * 2; // 1582 - 48 - 912 - 28 = 594
        const int leftX = edge;
        const int centerX = leftX + wingW + gap;
        const int rightX = centerX + centerW + gap;

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

    if (liverTheme() || (overviewLayoutMode == 1 && themeMode != 1 && themeMode != 8 && themeMode != 9))
    {
        const int swW = (overviewMonitor.getWidth() < 460) ? 44 : 58;
        const int swGap = 4;
        const int swTotal = swW * 5 + swGap * 4;
        const int swX0 = overviewMonitor.getRight() - 12 - swTotal;
        const int swY = overviewMonitor.getY() + 9;
        for (int c = 0; c < 5; ++c)
            coutureSwatchRects[(size_t) c] = { swX0 + c * (swW + swGap), swY, swW, 26 };
    }

    const int levelButtonWidth = overviewOutput.getWidth() < 240 ? 80 : 124;
    levelBadgeRect = { overviewOutput.getRight() - levelButtonWidth - 10, overviewOutput.getY() + 10, levelButtonWidth, 24 };
    overviewOutputTrim.setBounds (levelBadgeRect);

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
    const int swW = (charRow2.getWidth() - 12) / 3;
    atOnButton.setBounds (charRow2.removeFromLeft (swW)); charRow2.removeFromLeft (6);
    jnOnButton.setBounds (charRow2.removeFromLeft (swW)); charRow2.removeFromLeft (6);
    vcOnButton.setBounds (charRow2);

    // カード2（空間をつくる）上段2行: リバーブ電源・種類選択、テンポ合わせ・低遅延モード
    auto spaceRow1 = overviewCards[2].reduced (12).withTrimmedTop (50).withHeight (34);
    reverbPower.setBounds (spaceRow1.removeFromLeft (juce::jmin (180, (spaceRow1.getWidth() - 6) / 2)));
    spaceRow1.removeFromLeft (6);
    revTypeBox.setBounds (spaceRow1);

    auto spaceRow2 = overviewCards[2].reduced (12).withTrimmedTop (88).withHeight (30);
    const int spBtnW = (spaceRow2.getWidth() - 12) * 32 / 100;
    tempoFitButton.setBounds (spaceRow2.removeFromLeft (spBtnW));
    spaceRow2.removeFromLeft (6);
    sessionButton .setBounds (spaceRow2.removeFromLeft (spBtnW));
    spaceRow2.removeFromLeft (6);
    irOverviewRect = spaceRow2;

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
    GzzioLnF::setUseKawaiiFont (pastelTheme());

    // 軽量モード(liteMode)時は全画面グラデーションやパーティクル描画を完全バイパスし、単色フラット塗りにする
    if (liteMode)
    {
        g.fillAll (Palette::bgTop);
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
        g.setFont (GzzioLnF::uiFont (juce::jmax (14.0f, height) * juce::jlimit (1.0f, 1.3f, fontScale / 1.5f), bold));
        g.drawText (value, r, just);
    };

    // ---- 2段コマンドデッキ・コンソールバー（通常モード時は立体ガラスデッキ＆イルミネーションラインを描画） ----
    if (! liteMode)
    {
        const bool lightDeck = Palette::panel.getPerceivedBrightness() > 0.55f;
        const float deckAlpha = nightTheme() ? 0.78f : (lightDeck ? 0.94f : 0.66f);
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

        const char* pillJa[4] = { "★ARGON", "♥LIVER", "🎛STUDIO", "◎従来" };
        const char* pillEn[4] = { "ARGON",  "LIVER",  "STUDIO",  "CLASSIC" };
        const juce::Colour pillCols[4] = { Palette::ice, Palette::salmon, Palette::yellow, Palette::blue };
        for (int i = 0; i < 4; ++i)
        {
            drawHeaderPill (archPillRects[(size_t) i], overviewLayoutMode == i, pillCols[i],
                            juce::String::fromUTF8 (language->english ? pillEn[i] : pillJa[i]));
        }

        // [⚡軽量 / LITE] ワンタッチ省負荷モードピル
        drawHeaderPill (litePillRect, liteMode, Palette::green, language->T ("⚡軽量", "LITE"));

        // [🎯中央寄せ / MONO] 片耳マイク自動中央寄せ・モノラル最適化ピル
        if (! routePillRect.isEmpty())
        {
            const int rMode = processor.getInputRouteMode();
            const bool outMono = processor.getOutputMonoMode();
            const bool rActive = outMono || (rMode != 4);
            const auto rCol = outMono ? Palette::blue : Palette::ice;
            const char* rJa[5] = { "🎯中央音", "🎯IN1中央", "🎯IN2中央", "🎯1+2中央", "🎯ステレオ" };
            const char* rEn[5] = { "CENTER",   "IN1->C",    "IN2->C",    "1+2->C",    "STEREO"    };
            const int idx = juce::jlimit (0, 4, rMode);
            const juce::String rLabel = outMono
                ? language->T ("🎯単音化", "MONO")
                : juce::String::fromUTF8 (language->english ? rEn[idx] : rJa[idx]);
            drawHeaderPill (routePillRect, rActive, rCol, rLabel);
        }

        // [📡外部出力] SYNCROOM / VB-CABLE 同時出力ブリッジピル
        if (! streamPillRect.isEmpty())
        {
            const bool sOn = processor.getStreamOut().isRunning();
            drawHeaderPill (streamPillRect, sOn, sOn ? Palette::green : Palette::salmon,
                            sOn ? language->T ("📡SYNC中", "SYNC ON") : language->T ("📡外部出力", "SYNC OUT"));
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

    // テーマ風景が透けて見えるよう、通常時は半透明ガラス調、軽量モード時はAlpha=1.0の高速ソリッド描画にする
    const float cardAlpha = liteMode ? 1.0f
                          : liverTheme() ? 0.96f
                          : (themeMode == 2 || themeMode == 4 || themeMode == 6) ? 0.86f
                          : (themeMode == 1 || themeMode == 8 || themeMode == 9) ? 0.94f : 0.96f;

    // ★ マルチペルソナ・デザイン造形判定（かっこいいSF系／Vライバーキラキラ系／ゆるふわカフェ系／ボタニカル天球系／名機スタジオ系）
    const bool isYuruStyle   = (themeMode == 1 || themeMode == 8);
    const bool isLiverStyle  = liverTheme() || (overviewLayoutMode == 1 && ! isYuruStyle && themeMode != 9);
    const bool isNatureAura  = (themeMode == 2 || themeMode == 3 || themeMode == 4);
    const bool isStudioStyle = (themeMode == 9) || (overviewLayoutMode == 2 && ! isLiverStyle && ! isYuruStyle);
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
        if (liteMode)
        {
            g.setColour (Palette::panel);
            g.fillRoundedRectangle (rf, 14.0f);
            g.setColour (Palette::panelLn);
            g.drawRoundedRectangle (rf.reduced (0.5f), 14.0f, 1.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (rf.getX() + 16.0f, rf.getY() + 14.0f, 4.0f, 22.0f, 2.0f);
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

        // 🌿✦ 4. BOTANICAL NATURE / AURORA / BRAND: 二重フィリグリー装飾枠＋四隅ダイヤ星オーナメント
        if (isNatureAura)
        {
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.62f), rf.getX() + 14.0f, (float) (r.getY() + 45),
                                                     accent.withAlpha (0.10f), rf.getRight() - 14.0f, (float) (r.getY() + 45), false));
            g.fillRect (rf.getX() + 14.0f, (float) (r.getY() + 45), rf.getWidth() - 28.0f, 1.5f);
            g.setColour (accent.withAlpha (0.34f));
            g.drawRoundedRectangle (rf.reduced (3.8f), cardRad - 2.5f, 1.0f);
            g.setColour (Palette::panelLn.brighter (0.12f));
            g.drawRoundedRectangle (rf.reduced (0.5f), cardRad, 1.3f);
            drawSparkle4 (rf.getX() + 17.0f, rf.getY() + 24.0f, 6.5f, accent);
            drawSparkle4 (rf.getX() + 10.0f, rf.getY() + 10.0f, 4.0f, accent.withAlpha (0.65f));
            drawSparkle4 (rf.getRight() - 10.0f, rf.getY() + 10.0f, 4.0f, accent.withAlpha (0.65f));
            drawSparkle4 (rf.getX() + 10.0f, rf.getBottom() - 10.0f, 4.0f, accent.withAlpha (0.65f));
            drawSparkle4 (rf.getRight() - 10.0f, rf.getBottom() - 10.0f, 4.0f, accent.withAlpha (0.65f));
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
    const juce::String monTitle = (overviewLayoutMode == 3)
        ? language->T ("音の輪郭", "SPECTRUM")
        : isLiverStyle
        ? (hasSwatches ? language->T ("♥ クチュール・パクト", "COUTURE COMPACT")
                       : language->T ("♥ クチュール・クッションパクト ＆ 歌声ステージRTA", "COUTURE CUSHION COMPACT & STAGE RTA"))
        : isYuruStyle
        ? language->T ("🍰 パティスリー・ガラスドーム ＆ 歌声カフェRTA", "PATISSERIE GLASS DOME & SPECTRUM")
        : (themeMode == 2)
        ? language->T ("🌿 四季の水紋オーブ ＆ 音の輪郭モニター", "BOTANICAL WATER ORB & SPECTRUM")
        : (themeMode == 4)
        ? language->T ("✦ 星屑プラネタリウム天球儀 ＆ 輪郭モニター", "CELESTIAL ASTROLABE & SPECTRUM")
        : isStudioStyle
        ? language->T ("🎛 アナログ位相ゴニオメーター ＆ スペクトラム RTA", "ANALOG GONIOMETER & RTA")
        : language->T ("ARGON 3D 回転トラッカーボール ＆ 4軸トリガー機関", "ARGON 3D TRACKBALL & 4-AXIS TRIGGER");
    auto monTitleRect = overviewMonitor.reduced (28, 8).withHeight (28);
    if (hasSwatches)
        monTitleRect.setRight (juce::jmax (monTitleRect.getX() + 60, coutureSwatchRects[0].getX() - 6));
    fitText (g, monTitle, monTitleRect, juce::Justification::centredLeft, 16.0f, true);

    // ---- 💄 クチュール・コスメ 5色ワンタッチ・ジュエルスウォッチピル描画 ----
    if (hasSwatches)
    {
        struct SwatchDef { juce::uint32 bgTop, bgBot, gem, rim, ink; const char* ja; const char* en; };
        static const SwatchDef kSw[5] = {
            { 0xfffff4f7, 0xfff8dce8, 0xffd63264, 0xffd4967a, 0xff44182b, "01ロゼ", "01RS" },
            { 0xfff6f5ff, 0xffe2dff8, 0xff6b52dc, 0xff9aa6c6, 0xff262042, "02氷紫", "02LI" },
            { 0xff2f1f34, 0xff1c131e, 0xfff43868, 0xffc88ca8, 0xfffcecf4, "03黒姫", "03NR" },
            { 0xfff2fcf9, 0xffd4efe6, 0xff229a82, 0xffcfa262, 0xff163830, "04翡翠", "04MT" },
            { 0xfffff8f1, 0xfffce0c8, 0xffe66b30, 0xffda9c48, 0xff462416, "05白桃", "05PC" }
        };
        for (int c = 0; c < 5; ++c)
        {
            const auto sr = coutureSwatchRects[(size_t) c];
            const auto srf = sr.toFloat();
            const float sRad = srf.getHeight() * 0.5f;
            const bool active = (themeMode == Palette::themeOfCouture (c));
            const auto& d = kSw[c];

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
            fitText (g, juce::String::fromUTF8 (language->english ? d.en : d.ja),
                     sr.withTrimmedLeft (12).reduced (1, 0), juce::Justification::centred, 14.0f, true);
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
            const auto deckBgTop = pastelTheme() ? Palette::panel2.darker (0.04f) : Palette::bgTop.darker (0.48f);
            const auto deckBgBot = pastelTheme() ? Palette::panel.darker  (0.08f) : Palette::panel.darker (0.30f);
            g.setGradientFill (juce::ColourGradient (deckBgTop, padF.getX(), padF.getY(),
                                                     deckBgBot, padF.getRight(), padF.getBottom(), false));
            g.fillRoundedRectangle (padF, 14.0f);
            g.setColour (isStudioStyle ? Palette::yellow.withAlpha (0.55f) : Palette::panelLn.withAlpha (0.65f));
            g.drawRoundedRectangle (padF.reduced (0.5f), 14.0f, 1.2f);

            // 外周の3D回転ガイド軌道アーク
            const auto rimAccent = (isStudioStyle || themeMode == 3) ? Palette::yellow : Palette::ice;
            for (int q = 0; q < 4; ++q)
            {
                const float qa = (float) q * juce::MathConstants<float>::halfPi + 0.785f;
                juce::Path orbitArc;
                orbitArc.addCentredArc (cx, cy, socketR + 5.5f, socketR + 5.5f, 0.0f,
                                        qa - 0.32f, qa + 0.32f, true);
                g.setColour (rimAccent.withAlpha (0.32f));
                g.strokePath (orbitArc, juce::PathStrokeType (1.2f));
            }

            // 2. ソケットリング（Studio/Brandは真鍮ゴールド、Natureはエメラルド/ゴールド、Argonはチタン）
            g.setColour (juce::Colours::black.withAlpha (pastelTheme() ? 0.18f : 0.55f));
            g.fillEllipse (cx - socketR - 2.0f, cy - socketR + 2.0f, (socketR + 2.0f) * 2.0f, (socketR + 2.0f) * 2.0f);
            if (isStudioStyle || themeMode == 3)
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

                const auto spTop = (themeMode == 2) ? juce::Colour (0xffe8f6ee)
                                 : isStudioStyle    ? juce::Colour (0xff2c2419)
                                                    : Palette::panel2.brighter (0.20f);
                const auto spBot = (themeMode == 2) ? juce::Colour (0xffb8dec8)
                                 : isStudioStyle    ? juce::Colour (0xff120e0a)
                                                    : Palette::bgBot.darker (0.68f);
                g.setGradientFill (juce::ColourGradient (
                    spTop, ox * 0.6f + cx * 0.4f, oy * 0.6f + cy * 0.4f,
                    spBot, cx + (cx - ox) * 0.9f, cy + (cy - oy) * 0.9f, true));
                g.fillEllipse (cx - ballR, cy - ballR, ballR * 2.0f, ballR * 2.0f);

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

        // 四隅・上下のHUD軸ラベル（球体ソケットと重ならないよう上下22px帯と下隅に配置）
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

    // ---- ★ Argon Velvet Engine 4本のフェーダー（ペルソナ別にハート宝石チャーム／マカロンキャンディ／3Dメカトリガーを描き分け） ----
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
                // 1. ガラスバイアル外筒（厚手クリスタルガラスの奥行きとクッションファンデ／ベルベット色）
                g.setGradientFill (juce::ColourGradient (
                    nightTheme() ? Palette::panel2.darker (0.18f) : Palette::panel.brighter (0.05f), rf.getX(), rf.getY(),
                    nightTheme() ? Palette::panel.darker (0.08f)  : Palette::panel2,                 rf.getX(), rf.getBottom(), false));
                g.fillRoundedRectangle (rf, barRad);

                // 2. 左端のフルーテッド貴金属・アプリケーターキャップ（ローズゴールド／プラチナ／アンティーク／シャンパン／24Kハニー）
                const float goldCapW = 10.0f;
                const juce::Rectangle<float> goldCapR (rf.getX() + 1.0f, rf.getY() + 1.0f, goldCapW, rf.getHeight() - 2.0f);
                g.setGradientFill (juce::ColourGradient (Palette::metalHi,  goldCapR.getX(), goldCapR.getY(),
                                                         Palette::metalMid, goldCapR.getX(), goldCapR.getBottom(), false));
                g.fillRoundedRectangle (goldCapR, barRad - 1.0f);
                g.setColour (Palette::metalDark.withAlpha (0.65f));
                g.drawLine (goldCapR.getRight(), goldCapR.getY() + 1.5f, goldCapR.getRight(), goldCapR.getBottom() - 1.5f, 1.2f);

                // 3. リキッド・グロス／サテンピグメント充填バー
                const float tubeX = rf.getX() + goldCapW + 1.5f;
                const float tubeMaxW = juce::jmax (8.0f, rf.getRight() - tubeX - 3.0f);
                const int fillW = juce::roundToInt (tubeMaxW * norm);
                if (fillW > 2)
                {
                    const juce::Rectangle<float> fillRf (tubeX, rf.getY() + 2.2f, (float) fillW, rf.getHeight() - 4.4f);
                    g.setGradientFill (juce::ColourGradient (barCols[b].withAlpha (0.38f), fillRf.getX(), fillRf.getY(),
                                                             barCols[b].withAlpha (0.78f), fillRf.getRight(), fillRf.getY(), false));
                    g.fillRoundedRectangle (fillRf, (rf.getHeight() - 4.4f) * 0.45f);

                    // グロス液面の斜めシマースパークル帯
                    juce::Path clipP;
                    clipP.addRoundedRectangle (fillRf, (rf.getHeight() - 4.4f) * 0.45f);
                    juce::Graphics::ScopedSaveState ss (g);
                    g.reduceClipRegion (clipP);
                    g.setColour (juce::Colours::white.withAlpha (nightTheme() ? 0.16f : 0.34f));
                    for (float sx = fillRf.getX() - 14.0f; sx < fillRf.getRight() + 8.0f; sx += 16.0f)
                        g.drawLine (sx, fillRf.getBottom(), sx + 9.0f, fillRf.getY(), 2.4f);
                }

                // 4. クリスタルガラス管の上部ハイグロス反射ストリップ
                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (nightTheme() ? 0.24f : 0.78f),
                                                         tubeX + 4.0f, rf.getY() + 1.8f,
                                                         juce::Colours::transparentWhite,
                                                         tubeX + 4.0f, rf.getY() + rf.getHeight() * 0.48f, false));
                g.fillRoundedRectangle (tubeX + 2.0f, rf.getY() + 1.8f, tubeMaxW - 6.0f, rf.getHeight() * 0.36f, 3.0f);

                // 5. スライダーチャームつまみ（V-Liverは貴金属縁ハート宝石、ゆるふわは焦がしキャラメル縁マカロン）
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
                // フェーダーベゼル外枠と上下のガイドレール（中央テキスト帯を横切る黒溝を廃止し文字の視認性を100%確保）
                g.setColour (pastelTheme() ? Palette::panel2.darker (0.05f) : Palette::bgTop.darker (0.28f).withAlpha (0.90f));
                g.fillRoundedRectangle (rf, 6.5f);
                g.setColour (Palette::panelLn.withAlpha (0.45f));
                g.fillRect (rf.getX() + 6.0f, rf.getY() + 1.5f, rf.getWidth() - 12.0f, 1.2f);
                g.fillRect (rf.getX() + 6.0f, rf.getBottom() - 2.7f, rf.getWidth() - 12.0f, 1.2f);

                // 上下の精密フェーダー目盛り刻み
                g.setColour (Palette::inkSoft.withAlpha (0.32f));
                for (int t = 1; t < 10; ++t)
                {
                    const float tx = rf.getX() + (rf.getWidth() * (float) t) / 10.0f;
                    g.drawVerticalLine ((int) tx, rf.getY() + 1.5f, rf.getY() + 4.2f);
                    g.drawVerticalLine ((int) tx, rf.getBottom() - 4.2f, rf.getBottom() - 1.5f);
                }

                const int fillW = juce::roundToInt ((rf.getWidth() - 12.0f) * norm);
                if (fillW > 2)
                {
                    const juce::Rectangle<float> fillRf (rf.getX() + 2.0f, rf.getY() + 2.5f, (float) fillW + 4.0f, rf.getHeight() - 5.0f);
                    g.setGradientFill (juce::ColourGradient (barCols[b].darker (0.12f).withAlpha (0.42f), fillRf.getX(), fillRf.getY(),
                                                             barCols[b].brighter (0.18f).withAlpha (0.72f), fillRf.getRight(), fillRf.getY(), false));
                    g.fillRoundedRectangle (fillRf, 4.5f);
                }

                // 実音倍音ピーク輝線
                if (acts[b] > 0.02f)
                {
                    const float liveNorm = juce::jlimit (0.0f, 1.0f, norm * 0.7f + acts[b] * 0.6f);
                    const float peakX = rf.getX() + 4.0f + liveNorm * (rf.getWidth() - 12.0f);
                    g.setColour (barCols[b].brighter (0.45f));
                    g.fillRoundedRectangle (peakX, rf.getY() + 2.0f, 3.0f, rf.getHeight() - 4.0f, 1.5f);
                }

                // ★ 3Dバイレベル・メカニカルトリガーキャリッジ（上下のスライド爪＋中央レーザーカーソル刃）
                const float capW = 12.0f;
                const float capX = rf.getX() + 2.0f + norm * (rf.getWidth() - capW - 4.0f);
                const float cxCap = capX + capW * 0.5f;
                g.setColour (Palette::panel2.brighter (0.35f));
                g.fillRoundedRectangle (capX, rf.getY() + 0.5f, capW, 4.8f, 2.0f);
                g.fillRoundedRectangle (capX, rf.getBottom() - 5.3f, capW, 4.8f, 2.0f);
                g.setColour (barCols[b]);
                g.drawRoundedRectangle (capX, rf.getY() + 0.5f, capW, 4.8f, 2.0f, 1.1f);
                g.drawRoundedRectangle (capX, rf.getBottom() - 5.3f, capW, 4.8f, 2.0f, 1.1f);
                g.setColour (barCols[b].withAlpha (0.85f));
                g.fillRect (cxCap - 1.2f, rf.getY() + 3.0f, 2.4f, rf.getHeight() - 6.0f);
            }

            g.setColour (kawaiiBars ? Palette::metalMid : barCols[b].withAlpha (0.88f));
            g.drawRoundedRectangle (rf.reduced (0.5f), barRad, 1.3f);
            g.setColour (Palette::ink);
            fitText (g, juce::String::fromUTF8 (language->english ? barEn[b] : barJa[b]),
                     r.reduced (kawaiiBars ? 14 : 8, 0).withTrimmedRight (48),
                     juce::Justification::centredLeft, 14.0f, true);
            fitText (g, juce::String (juce::roundToInt (vals[b])) + "%",
                     r.reduced (6, 0).withTrimmedLeft (r.getWidth() - 56), juce::Justification::centredRight, 14.0f, true);
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
    const bool compactOut = (overviewOutput.getWidth() < 240);
    const int bw = compactOut ? 80 : 124;
    fitText (g, compactOut ? language->T ("計器", "I/O") : language->T ("計器 ＆ 入出力", "TELEMETRY & I/O"),
             juce::Rectangle<int> (overviewOutput.getX() + 24, overviewOutput.getY() + 8, overviewOutput.getWidth() - bw - 36, 28),
             juce::Justification::centredLeft, 16.0f, true);

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
        const int rowStep = (overviewLayoutMode == 3) ? 18 : 32;
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
            const int ry = overviewOutput.getY() + 42 + i * rowStep;
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

    const char* namesJa[] = { "01 整える", "02 声をつくる", "03 空間をつくる" };
    const char* namesEn[] = { "01 CLEAN",  "02 CHARACTER",  "03 SPACE" };
    // V-Liver時は 01=ピーチコーラルチーク, 02=モーヴアイシャドウ, 03=ベリーローズティント のコスメ3色パレットに統一
    const juce::Colour accents[] = {
        Palette::green,
        isLiverStyle ? Palette::yellow : Palette::blue,
        isLiverStyle ? Palette::blue   : Palette::yellow
    };
    for (int i = 0; i < 3; ++i)
    {
        auto r = overviewCards[(size_t) i];
        card (r, accents[i]);

        // ★ 3ウイング配置 (overviewLayoutMode == 0): 主役ノブ台座＆3連サテライトカプセルもペルソナ別に描き分け
        if (overviewLayoutMode == 0 && ! liteMode)
        {
            const bool softPedestal = (isLiverStyle || isYuruStyle || pastelTheme());
            if (i == 0 || i == 2)
            {
                auto body = r.reduced (14).withTrimmedTop (122);
                const int heroW = body.getWidth() * 51 / 100;
                const auto heroRect = body.removeFromLeft (heroW).reduced (2).toFloat();
                body.removeFromLeft (6);

                if (softPedestal)
                {
                    // ♥🍰 プレストチーク・サテンクッション台座＆貴金属リム
                    g.setGradientFill (juce::ColourGradient (accents[i].withAlpha (0.20f), heroRect.getX(), heroRect.getY(),
                                                             Palette::panel2.withAlpha (0.82f), heroRect.getCentreX(), heroRect.getBottom(), false));
                    g.fillRoundedRectangle (heroRect, 16.0f);
                    g.setColour (isLiverStyle ? Palette::metalMid : accents[i].withAlpha (0.52f));
                    g.drawRoundedRectangle (heroRect.reduced (0.8f), 16.0f, 1.4f);
                    g.setColour ((themeMode == 11) ? Palette::metalHi.withAlpha (0.35f) : juce::Colours::white.withAlpha (0.80f));
                    g.drawRoundedRectangle (heroRect.reduced (3.0f), 13.5f, 1.0f);

                    const int satH = body.getHeight() / 3;
                    for (int s = 0; s < 3; ++s)
                    {
                        const auto satR = juce::Rectangle<int> (body.getX(), body.getY() + s * satH, body.getWidth(), satH).reduced (2, 3).toFloat();
                        g.setGradientFill (juce::ColourGradient (
                            nightTheme() ? Palette::panel2.brighter (0.06f).withAlpha (0.88f) : Palette::panel.brighter (0.04f).withAlpha (0.88f), satR.getX(), satR.getY(),
                            Palette::panel2.withAlpha (0.88f), satR.getX(), satR.getBottom(), false));
                        g.fillRoundedRectangle (satR, 12.0f);
                        g.setColour (isLiverStyle ? Palette::metalMid.withAlpha (0.72f) : accents[i].withAlpha (0.45f));
                        g.drawRoundedRectangle (satR, 12.0f, 1.2f);
                        drawHeartShape (satR.getX() + 6.5f, satR.getCentreY(), 3.8f, accents[i].withAlpha (0.75f));
                    }
                }
                else
                {
                    // 左側: 大型主役ノブ専用の8角形凹みリアクター台座
                    const auto pedPath = makeChamferPath (heroRect, 10.0f);
                    g.setGradientFill (juce::ColourGradient (Palette::bgTop.darker (0.28f).withAlpha (0.68f), heroRect.getX(), heroRect.getY(),
                                                             accents[i].withAlpha (0.10f), heroRect.getCentreX(), heroRect.getBottom(), false));
                    g.fillPath (pedPath);
                    g.setColour (accents[i].withAlpha (0.42f));
                    g.strokePath (pedPath, juce::PathStrokeType (1.2f));

                    // 右側: 3連サテライトノブのハードウェア・モジュールカプセル枠
                    const int satH = body.getHeight() / 3;
                    for (int s = 0; s < 3; ++s)
                    {
                        const auto satR = juce::Rectangle<int> (body.getX(), body.getY() + s * satH, body.getWidth(), satH).reduced (2, 3).toFloat();
                        g.setColour (Palette::bgTop.darker (0.24f).withAlpha (0.58f));
                        g.fillRoundedRectangle (satR, 8.0f);
                        g.setColour (Palette::panelLn.withAlpha (0.55f));
                        g.drawRoundedRectangle (satR, 8.0f, 1.0f);
                        g.setColour (accents[i].withAlpha (0.55f));
                        g.fillRoundedRectangle (satR.getX() + 3.0f, satR.getY() + 8.0f, 2.5f, satR.getHeight() - 16.0f, 1.2f);
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
                    if (isH[kIdx])
                    {
                        const auto hr = juce::Rectangle<int> (cx, body.getY(), ws[kIdx], body.getHeight()).reduced (3, 2).toFloat();
                        g.setColour (softPedestal ? accents[i].withAlpha (0.16f) : Palette::bgTop.darker (0.24f).withAlpha (0.55f));
                        g.fillRoundedRectangle (hr, softPedestal ? 15.0f : 10.0f);
                        g.setColour (isLiverStyle ? Palette::metalMid : accents[i].withAlpha (0.45f));
                        g.drawRoundedRectangle (hr, softPedestal ? 15.0f : 10.0f, 1.3f);
                    }
                    cx += ws[kIdx];
                }
            }
        }

        // ♥ V-LIVER STAGE 配置では下段12連コスメパレットの背後に貴金属の五線譜とカメオ装飾を添える
        if (overviewLayoutMode == 1 && ! liteMode)
        {
            g.setColour ((isLiverStyle ? Palette::metalMid : accents[i]).withAlpha (0.22f));
            const int staffY = r.getY() + 190;
            for (int sl = 0; sl < 5; ++sl)
                g.drawHorizontalLine (staffY + sl * 9, (float) r.getX() + 14.0f, (float) r.getRight() - 14.0f);
            for (int n = 0; n < 4; ++n)
            {
                const float nx = (float) r.getX() + 56.0f + (float) n * ((float) (r.getWidth() - 112) / 3.0f);
                const float ny = (float) staffY + (float) ((n + i) % 4) * 9.0f;
                g.setColour (accents[i].withAlpha (0.35f));
                g.fillEllipse (nx - 4.2f, ny - 3.2f, 8.4f, 6.4f);
                g.drawLine (nx + 3.6f, ny, nx + 3.6f, ny - 16.0f, 1.4f);
                g.drawLine (nx + 3.6f, ny - 16.0f, nx + 9.0f, ny - 12.5f, 1.4f);
                if (n % 2 == 0)
                    drawHeartShape (nx + 24.0f, (float) staffY - 6.0f, 4.8f, accents[i].withAlpha (0.42f));
                else
                    drawSparkle4 (nx + 24.0f, (float) staffY + 42.0f, 4.8f, Palette::metalMid.withAlpha (0.65f));
            }
        }

        auto titleR = juce::Rectangle<int> (r.getX() + 26, r.getY() + 8,
                                            juce::jmax (60, overviewSectionPower[(size_t) i].getX() - (r.getX() + 30)), 36);
        g.setColour (Palette::ink);
        fitText (g, juce::String::fromUTF8 (language->english ? namesEn[i] : namesJa[i]),
                 titleR, juce::Justification::centredLeft, 18.0f, true);
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
