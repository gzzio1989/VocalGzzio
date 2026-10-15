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
        ? language->T ("【⚡ 軽量モード ON】4096点FFTスペクトラム・背景パーティクル・半透明ガラス合成を休止し、GPU/CPU負荷を極小化しました（Argon倍音エンジンを含む音声DSPは100%最高音質のまま動作します）。",
                       "[LITE MODE ON] 4096-pt FFT spectrum, background animations, and glass overdraw paused for minimal GPU/CPU load (100% full audio DSP quality preserved).")
        : language->T ("【⚡ 軽量モード OFF】フルアニメーション・リアルタイムFFTスペクトラム・高精度ハードウェア描画を再開しました。",
                       "[LITE MODE OFF] Full animations, real-time FFT spectrum, and bespoke hardware rendering resumed.");
    repaint();
}

void VocalGzzioContent::applyArgonOrbFromXY (float nx, float ny)
{
    argonOrbX = juce::jlimit (0.0f, 1.0f, nx);
    argonOrbY = juce::jlimit (0.0f, 1.0f, ny);
    argonActiveTarget = -1;

    // X軸: 左(0.0)=Warm Velvet(母音2次倍音・胸声アンカー) ⇄ 右(1.0)=Silk Air(14kHz透明感・子音保護)
    // Y軸: 上(0.0)=Up-Front ASMR(超近接・耳元密度)        ⇄ 下(1.0)=3D Stage Halo(バイノーラル立体空間)
    const float velvet = juce::jlimit (0.0f, 100.0f, (1.0f - argonOrbX * 0.52f) * 78.0f + (1.0f - argonOrbY) * 22.0f);
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
    setParam ("argon_silk",   silk);
    setParam ("argon_halo",   halo);

    // 2D オーブに連動して基本トーン＆空間も音楽的に微調整する
    setParam ("presence", juce::jmap (argonOrbX, -0.5f, 4.2f));
    setParam ("drive",    juce::jmap (1.0f - argonOrbX, 8.0f, 38.0f));
    setParam ("revmix",   juce::jmap (argonOrbY, 4.0f, 28.0f));

    overviewHint = language->T (
        "Argon 2D 重力場オーブ：横軸で「温かい母音2次倍音 ⇄ 14kHzシルク空気感」、縦軸で「耳元ASMR近接感 ⇄ モノ互換3D立体ハロー」を同時に連続成形しています（ダブルクリックで0%へリセット）。",
        "Argon 2D Voice Gravity Orb: X-axis morphs Warm Triode Velvet <-> 14kHz Silk Air; Y-axis morphs Up-Front ASMR <-> Mono-Safe 3D Binaural Halo (double-click to reset).");
    repaint();
}

void VocalGzzioContent::applyArgonVoiceTarget (int idx)
{
    idx = juce::jlimit (0, 3, idx);
    struct Target
    {
        float ox, oy;
        float velvet, silk, halo;
        float comp2, pres, drive, revmix, revsize;
        const char* hintJa;
        const char* hintEn;
    };
    static const Target kTargets[4] = {
        // 0: ★ VELVET 歌枠 (王道ベルベットボーカル)
        { 0.44f, 0.42f, 64.0f, 58.0f, 46.0f, 45.0f, 2.2f, 24.0f, 16.0f, 38.0f,
          "【★ VELVET 歌枠】母音だけに非対称三極管2次倍音と胸鳴りを加え、サ行を一切ザラつかせずに太く艶やかな主役ボーカルへ仕上げました。",
          "[VELVET VOCAL] Voiced-gated triode 2nd harmonics and chest anchor for a rich, silky lead vocal without sibilant harshness." },
        // 1: ♥ ASMR ささやき (超近接・耳元ブレス)
        { 0.28f, 0.14f, 76.0f, 72.0f, 18.0f, 58.0f, 1.4f, 28.0f, 5.0f, 20.0f,
          "【♥ ASMR ささやき】135Hz胸声サブアンカーと小声で開く14kHzシルクエアを組み合わせ、すぐ耳元で囁いているような生々しい距離感を作りました。",
          "[WHISPER ASMR] Combines sub-fundamental chest anchor and whisper-blooming 14kHz silk air for ultra-close intimacy." },
        // 2: ✦ CRYSTAL ポップス (高抜け・ボカロ/POPS)
        { 0.80f, 0.34f, 38.0f, 86.0f, 42.0f, 52.0f, 3.6f, 14.0f, 14.0f, 34.0f,
          "【✦ CRYSTAL ポップス】クレスト保護つき14kHzシルクエアと子音ガードを最大化し、音数の多いオケの中でも耳に刺さらずスコーンと抜ける声にしました。",
          "[CRYSTAL POP] Maximizes crest-protected 14kHz silk air and sibilance guard to cut through dense mixes without piercing." },
        // 3: ◆ HALO バラード (3D立体ハロー空間)
        { 0.54f, 0.84f, 60.0f, 66.0f, 86.0f, 42.0f, 2.0f, 22.0f, 26.0f, 56.0f,
          "【◆ HALO バラード】直交オールパス拡散による3Dバイノーラル・ハローを展開。スマホのモノラル再生でも声が痩せず、ステレオでは光の輪のように空間が広がります。",
          "[3D HALO STAGE] Engages quadrature all-pass binaural halo. 100% mono-compatible while wrapping the vocal in a 3D stage aura." }
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
        for (const char* id : { "argon_velvet", "argon_silk", "argon_halo" })
        {
            if (auto* p = processor.apvts.getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (0.0f);
                p->endChangeGesture();
            }
        }
        overviewHint = language->T (
            "【Argon 2D 重力場オーブ リセット】倍音(VELVET)・空気感(SILK)・立体ハロー(HALO)をニュートラル(0%)へリセットしました。",
            "[Argon 2D Orb Reset] Velvet, Silk Air, and 3D Halo reset to neutral (0%).");
        repaint();
        return true;
    }
    for (int i = 0; i < 3; ++i)
    {
        if (! argonBarRects[(size_t) i].isEmpty() && argonBarRects[(size_t) i].expanded (0, 3).contains (pt))
        {
            const char* ids[3] = { "argon_velvet", "argon_silk", "argon_halo" };
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
            setOverviewLayoutMode (i, true);
            const char* archHintsJa[4] = {
                "【★ ARGON DECK】中央に2D声質重力場オーブとArgon倍音エンジンを据えた、画面遷移ゼロの次世代3ウイング・コックピット配置です。",
                "【♥ LIVER STAGE】上段に2D声キャラメイク＆ステージモニター、下段に12連マカロンリボンを並べた配信・Vライバー特化ステージ配置です。",
                "【🎛 STUDIO RACK】左から右へ信号経路（PRE/CLEAN ➔ DYN/TONE ➔ ARGON MATRIX/RTA ➔ SPATIAL ➔ MASTER VU）が並ぶ5列ハードウェア・チャンネルストリップ配置です。",
                "【◎ CLASSIC】上部に大型チューナー、下部に3枚のセクションカードを並べた従来の3カード配置です。"
            };
            const char* archHintsEn[4] = {
                "[ARGON DECK] Next-gen 3-wing cockpit centered around the interactive 2D Voice Gravity Orb and Argon Velvet Engine.",
                "[LIVER STAGE] Streamer & V-Liver stage panorama with 2D Voice Make-up Pad and a 12-dial horizontal ribbon.",
                "[STUDIO RACK] 5-column vertical hardware channel strip following strict left-to-right console signal flow.",
                "[CLASSIC] Traditional 3-card overview layout."
            };
            overviewHint = language->T (archHintsJa[i], archHintsEn[i]);
            return true;
        }
    }

    // 1b. ヘッダの「⚡軽量」ワンクリック省負荷ピル
    if (! litePillRect.isEmpty() && litePillRect.contains (pt))
    {
        setLiteMode (! liteMode);
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

    // 4. Argon Velvet Engine 3本のダイレクトバー (VELVET / SILK / HALO)
    for (int i = 0; i < 3; ++i)
    {
        if (! argonBarRects[(size_t) i].isEmpty() && argonBarRects[(size_t) i].expanded (0, 3).contains (pt))
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
    else if (argonDragging >= 1 && argonDragging <= 3)
    {
        const int idx = argonDragging - 1;
        const auto& r = argonBarRects[(size_t) idx];
        if (! r.isEmpty())
        {
            const float norm = juce::jlimit (0.0f, 1.0f, (float) (pt.x - r.getX()) / (float) juce::jmax (1, r.getWidth()));
            const char* ids[3] = { "argon_velvet", "argon_silk", "argon_halo" };
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
        overviewHint = juce::String::fromUTF8 (language->english ? gzzio::kFinishPresets[index].descEn : gzzio::kFinishPresets[index].desc);
        repaint();
    };
    finishPresetIndex = (int) processor.apvts.state.getProperty ("finish_preset", -1);
    for (auto* button : { &overviewDetails, &overviewReturn, &overviewPitch, &reverbPower })
        styleButton (*button);
    overviewDetails.setComponentID ("openDetails");
    overviewReturn.setComponentID ("openOverview");
    overviewPitch.setComponentID ("openPitch");
    reverbPower.setComponentID ("reverbPower");
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
    addChildComponent (overviewCompare);
    overviewCompare.setComponentID ("compareOriginal");
    overviewCompare.onDown = [this] { processor.compareBypass.store (true); repaint(); };
    overviewCompare.onUp = [this] { processor.compareBypass.store (false); repaint(); };
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
    { k->slider.setEnabled (true); k->label.setAlpha (1.0f); }
    for (auto* c : std::initializer_list<juce::Component*> { &finishPresetBox, &overviewDetails, &overviewCompare,
                     &overviewNoiseStatus,
                     &overviewPitch, &overviewMore[0], &overviewMore[1], &overviewMore[2],
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
         (juce::Component*) &overviewPitch, (juce::Component*) &sizePopBtn,
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
    for (auto& r : argonBarRects) r = {};
    for (auto& r : argonTargetRects) r = {};

    // ---- 1段目: ロゴ・4種ワークスペース配置ピル・軽量モードピル・遅延バッジ・ナビツールバー ----
    const int buttonH = 38;
    for (int i = 0; i < 4; ++i)
        archPillRects[(size_t) i] = { 236 + i * 68, 20, 64, 34 };
    litePillRect = { 510, 20, 62, 34 };
    latBadgeArea = { 576, 20, 114, 34 };

    auto toolbar = juce::Rectangle<int> (694, 18, w - edge - 694, buttonH);
    loadButton     .setBounds (toolbar.removeFromRight (84));  toolbar.removeFromRight (6);
    saveButton     .setBounds (toolbar.removeFromRight (84));  toolbar.removeFromRight (8);
    sizePopBtn     .setBounds (toolbar.removeFromRight (148)); toolbar.removeFromRight (6);
    overviewDetails.setBounds (toolbar.removeFromRight (122)); toolbar.removeFromRight (6);
    tuningPopBtn   .setBounds (toolbar.removeFromRight (114)); toolbar.removeFromRight (6);
    overviewPitch  .setBounds (toolbar.removeFromRight (136)); toolbar.removeFromRight (6);
    checkupButton  .setBounds (toolbar.removeFromRight (90));

    // ---- 2段目: 仕上がり・音の種類・ワンクリック自動設定・A/B比較・原音比較・初期化 ----
    const int barY = 72, barH = 40;
    finishPresetBox.setBounds (116, barY, 320, barH);
    srcModeLabelArea = { 444, barY, 88, barH };
    srcModeBox     .setBounds (536, barY, 156, barH);
    autoSetupButton.setBounds (704, barY, 134, barH);
    songSetupButton.setBounds (846, barY, 134, barH);
    abA            .setBounds (992, barY, 42,  barH);
    abB            .setBounds (1040, barY, 42, barH);
    abCopy         .setBounds (1088, barY, 62, barH);
    overviewCompare.setBounds (1162, barY, 288, barH);
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

    const int top = 122, bottom = h - 92;

    if (overviewLayoutMode == 0)
    {
        // =========================================================================
        // ★ Architecture 0: ARGON COCKPIT (次世代3ウイング・コックピット配置)
        //   左ウイング: ピッチ＆クリーン (Tuner + 01 CLEAN)
        //   中央コア  : 2D Voice Gravity Orb & Argon Velvet Engine + 4連横ストリップ (02 CORE TONE)
        //   右ウイング: 5ch テレメトリ計器盤＆マスターI/O + 3D空間 (03 SPACE)
        // =========================================================================
        const int wingW = 456;
        const int centerW = w - edge * 2 - wingW * 2 - gap * 2; // 1582 - 48 - 912 - 28 = 594
        const int leftX = edge;
        const int centerX = leftX + wingW + gap;
        const int rightX = centerX + centerW + gap;

        // 左ウイング: 上=Tuner(224px), 下=Card 0(Clean)
        tuner.setBounds (leftX, top, wingW, 224);
        overviewCards[0] = { leftX, top + 224 + gap, wingW, bottom - (top + 224 + gap) };

        // 中央コア: 上=Argon 2D Gravity Core(344px), 下=Card 1(Character 4連横ストリップ)
        overviewMonitor = { centerX, top, centerW, 344 };
        argonPadRect    = { centerX + 16, top + 44, 316, 236 };
        eqGraph.setBounds (centerX + 344, top + 44, centerW - 360, 102);
        for (int b = 0; b < 3; ++b)
            argonBarRects[(size_t) b] = { centerX + 344, top + 164 + b * 38, centerW - 360, 28 };
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
        grid (overviewCards[0].reduced (14).withTrimmedTop (122), { &denoiseK, &gate, &lowCut, &mudK }, 2);
        // 中央下 Card 1 は 594px 幅を活かして 4ノブ横一列 (4-Across Console Strip)
        grid (overviewCards[1].reduced (12).withTrimmedTop (122), { &comp2K, &deessK, &presenceK, &warmthK }, 4);
        grid (overviewCards[2].reduced (14).withTrimmedTop (122), { &revMixK, &revSizeK, &delayK, &widthK }, 2);
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
        eqGraph.setBounds (overviewMonitor.getX() + 324, top + 42, monW - 340, 86);
        for (int b = 0; b < 3; ++b)
            argonBarRects[(size_t) b] = { overviewMonitor.getX() + 324, top + 140 + b * 36, monW - 340, 26 };
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
        for (int b = 0; b < 3; ++b)
            argonBarRects[(size_t) b] = { centerX + 228, top + 56 + b * 54, b3W - 242, 32 };
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
    tempoFitButton.setBounds (spaceRow2.removeFromLeft ((spaceRow2.getWidth() - 6) / 2));
    spaceRow2.removeFromLeft (6);
    sessionButton .setBounds (spaceRow2);

    for (auto* lamp : { &lampDn, &lampGate, &lampDs, &lampDly }) lamp->btn.setSize (28, 28);
    auto placePower = [] (Lamp& lamp, Knob& k)
    {
        const auto b = k.slider.getBounds();
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
                g.setGradientFill (juce::ColourGradient (Palette::green.withAlpha (0.06f),
                    0.0f, (float) getHeight(), juce::Colours::transparentBlack,
                    (float) getWidth() * 0.75f, 0.0f, false));
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
    g.setColour (Palette::yellow);
    g.fillRoundedRectangle (24.0f, 18.0f, 38.0f, 38.0f, 11.0f);
    g.setColour (Palette::readableOn (Palette::yellow));
    const float bars[] = { 9, 19, 27, 14 };
    for (int i = 0; i < 4; ++i) g.fillRoundedRectangle (33.0f + i * 6.0f, 37.0f - bars[i] * 0.5f, 3.0f, bars[i], 1.5f);
    text ("VocalGzzio", { 70, 14, 162, 42 }, 27, Palette::ink, true);

    // ---- 1段目中央: 4種ワークスペース配置アーキテクチャ切替ピル ＋ [⚡軽量] エコモードピル ----
    {
        const char* pillJa[4] = { "★ARGON", "♥LIVER", "🎛STUDIO", "◎従来" };
        const char* pillEn[4] = { "ARGON",  "LIVER",  "STUDIO",  "CLASSIC" };
        const juce::Colour pillCols[4] = { Palette::ice, Palette::salmon, Palette::yellow, Palette::blue };
        for (int i = 0; i < 4; ++i)
        {
            const auto r = archPillRects[(size_t) i].toFloat();
            const bool sel = (overviewLayoutMode == i);
            const auto col = pillCols[i];
            g.setColour (sel ? col.withAlpha (liteMode ? 0.35f : 0.28f) : Palette::panel2.withAlpha (liteMode ? 1.0f : 0.75f));
            g.fillRoundedRectangle (r, 8.0f);
            if (sel && ! liteMode)
            {
                g.setColour (col.withAlpha (0.85f));
                g.fillRoundedRectangle (r.getX() + 10.0f, r.getBottom() - 3.5f, r.getWidth() - 20.0f, 2.5f, 1.2f);
            }
            g.setColour (sel ? col : Palette::panelLn);
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, sel ? 1.8f : 1.0f);
            g.setColour (sel ? Palette::ink : Palette::inkSoft);
            fitText (g, juce::String::fromUTF8 (language->english ? pillEn[i] : pillJa[i]),
                     archPillRects[(size_t) i].reduced (2, 0), juce::Justification::centred, 14.0f, sel);
        }

        // [⚡軽量 / LITE] ワンタッチ省負荷モードピル
        if (! litePillRect.isEmpty())
        {
            const auto lr = litePillRect.toFloat();
            const auto lCol = Palette::green;
            g.setColour (liteMode ? lCol.withAlpha (0.32f) : Palette::panel2.withAlpha (0.75f));
            g.fillRoundedRectangle (lr, 8.0f);
            g.setColour (liteMode ? lCol : Palette::panelLn);
            g.drawRoundedRectangle (lr.reduced (0.5f), 8.0f, liteMode ? 1.8f : 1.0f);
            g.setColour (liteMode ? Palette::ink : Palette::inkSoft);
            fitText (g, language->T ("⚡軽量", "LITE"),
                     litePillRect.reduced (2, 0), juce::Justification::centred, 14.0f, liteMode);
        }
    }

    const auto latencyMs = 1000.0 * processor.addedLatencySamples() / juce::jmax (1.0, processor.getTunerSampleRate());
   #if VOCALGZZIO_TRIAL
    fitText (g, juce::String (JucePlugin_VersionString) + " " + language->T ("体験 ", "TRIAL ") + juce::String (latencyMs, 1) + "ms",
             latBadgeArea, juce::Justification::centredLeft, 14.0f, true);
   #else
    fitText (g, juce::String (JucePlugin_VersionString) + (liteMode ? " ECO " : " / ") + juce::String (latencyMs, 1) + "ms",
             latBadgeArea, juce::Justification::centredLeft, 14.0f, liteMode);
   #endif
    g.setColour (Palette::panelLn);
    g.drawHorizontalLine (64, 24.0f, (float) getWidth() - 24.0f);
    text (language->T ("仕上がり", "FINISH"), { 26, 72, 86, 40 }, 16, Palette::ink, true);
    if (! srcModeLabelArea.isEmpty())
        text (language->src_label(), srcModeLabelArea.withTrimmedRight (6), 15, Palette::inkSoft, true, juce::Justification::centredRight);

    // テーマ風景が透けて見えるよう、通常時は半透明ガラス調、軽量モード時はAlpha=1.0の高速ソリッド描画にする
    const float cardAlpha = liteMode ? 1.0f
                          : (themeMode == 2 || themeMode == 4 || themeMode == 5 || themeMode == 6) ? 0.82f
                          : (themeMode == 1 || themeMode == 8 || themeMode == 9) ? 0.90f : 0.95f;
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

        // 通常モード: ドロップシャドウ＋ガラス立体グラデーション＋上部スペキュラ反射ハイライト
        g.setColour (juce::Colours::black.withAlpha (nightTheme() ? 0.26f : 0.08f));
        g.fillRoundedRectangle (rf.translated (0.0f, 3.0f), 16.0f);

        g.setGradientFill (juce::ColourGradient (Palette::panel.brighter (0.04f).withMultipliedAlpha (cardAlpha), rf.getX(), rf.getY(),
                                                 Palette::panel.darker (0.03f).withMultipliedAlpha (cardAlpha), rf.getX(), rf.getBottom(), false));
        g.fillRoundedRectangle (rf, 16.0f);

        // 上端のガラスエッジ反射ハイライトとヘッダー仕切りライン
        g.setColour (juce::Colours::white.withAlpha (nightTheme() ? 0.14f : 0.45f));
        g.drawHorizontalLine (r.getY() + 1, rf.getX() + 16.0f, rf.getRight() - 16.0f);
        g.setColour (accent.withAlpha (0.20f));
        g.drawHorizontalLine (r.getY() + 45, rf.getX() + 14.0f, rf.getRight() - 14.0f);

        g.setColour (Palette::panelLn);
        g.drawRoundedRectangle (rf.reduced (0.5f), 16.0f, 1.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (rf.getX() + 16.0f, rf.getY() + 14.0f, 4.0f, 22.0f, 2.0f);

        // ワークスペース構造ごとの専用クラフトマンシップ装飾
        if (overviewLayoutMode == 0)
        {
            // ★ ARGON COCKPIT: 四隅のSFターゲットL字ブラケット＆テレメトリ目盛り
            g.setColour (accent.withAlpha (0.72f));
            const float bLen = 12.0f, bIn = 4.0f;
            // 左上
            g.drawLine (rf.getX() + bIn, rf.getY() + bIn, rf.getX() + bIn + bLen, rf.getY() + bIn, 1.8f);
            g.drawLine (rf.getX() + bIn, rf.getY() + bIn, rf.getX() + bIn, rf.getY() + bIn + bLen, 1.8f);
            // 右上
            g.drawLine (rf.getRight() - bIn - bLen, rf.getY() + bIn, rf.getRight() - bIn, rf.getY() + bIn, 1.8f);
            g.drawLine (rf.getRight() - bIn, rf.getY() + bIn, rf.getRight() - bIn, rf.getY() + bIn + bLen, 1.8f);
            // 左下
            g.drawLine (rf.getX() + bIn, rf.getBottom() - bIn, rf.getX() + bIn + bLen, rf.getBottom() - bIn, 1.8f);
            g.drawLine (rf.getX() + bIn, rf.getBottom() - bIn - bLen, rf.getX() + bIn, rf.getBottom() - bIn, 1.8f);
            // 右下
            g.drawLine (rf.getRight() - bIn - bLen, rf.getBottom() - bIn, rf.getRight() - bIn, rf.getBottom() - bIn, 1.8f);
            g.drawLine (rf.getRight() - bIn, rf.getBottom() - bIn - bLen, rf.getRight() - bIn, rf.getBottom() - bIn, 1.8f);
        }
        else if (overviewLayoutMode == 1)
        {
            // ♥ V-LIVER STAGE: 内側パステルステッチ枠と四隅の4芒星ジュエル装飾
            g.setColour (accent.withAlpha (0.35f));
            g.drawRoundedRectangle (rf.reduced (4.0f), 13.0f, 1.2f);
            auto drawMiniStar = [&] (float sx, float sy, float sr)
            {
                juce::Path sp;
                for (int p = 0; p < 8; ++p)
                {
                    const float a = p * juce::MathConstants<float>::pi * 0.25f;
                    const float rad = (p % 2 == 0) ? sr : sr * 0.36f;
                    const float px = sx + std::cos (a) * rad, py = sy + std::sin (a) * rad;
                    if (p == 0) sp.startNewSubPath (px, py); else sp.lineTo (px, py);
                }
                sp.closeSubPath();
                g.setColour (accent.withAlpha (0.65f));
                g.fillPath (sp);
            };
            drawMiniStar (rf.getX() + 10.0f, rf.getY() + 10.0f, 4.5f);
            drawMiniStar (rf.getRight() - 10.0f, rf.getBottom() - 10.0f, 4.5f);
        }
        else if (overviewLayoutMode == 2)
        {
            // 🎛 STUDIO RACK: 四隅のハードウェア六角ラックマウントボルトとヘアライン溝
            auto drawRackBolt = [&] (float bx, float by)
            {
                g.setColour (Palette::panelLn.darker (0.35f));
                g.fillEllipse (bx - 4.5f, by - 4.5f, 9.0f, 9.0f);
                g.setColour (Palette::inkSoft.withAlpha (0.65f));
                g.drawEllipse (bx - 4.5f, by - 4.5f, 9.0f, 9.0f, 1.1f);
                g.drawLine (bx - 2.2f, by, bx + 2.2f, by, 1.1f);
                g.drawLine (bx, by - 2.2f, bx, by + 2.2f, 1.1f);
            };
            drawRackBolt (rf.getX() + 9.0f, rf.getY() + 9.0f);
            drawRackBolt (rf.getRight() - 9.0f, rf.getY() + 9.0f);
            drawRackBolt (rf.getX() + 9.0f, rf.getBottom() - 9.0f);
            drawRackBolt (rf.getRight() - 9.0f, rf.getBottom() - 9.0f);
        }
    };

    card (overviewMonitor, Palette::ice);
    const juce::String monTitle = (overviewLayoutMode == 1)
        ? language->T ("声キャラメイク 2D ＆ 輪郭モニター", "KAWAII 2D VOICE PAD & SPECTRUM")
        : (overviewLayoutMode == 2)
        ? language->T ("ARGON 倍音マトリクス ＆ スペクトラム RTA", "ARGON HARMONIC MATRIX & RTA")
        : (overviewLayoutMode == 0)
        ? language->T ("ARGON 2D 声の重力場オーブ ＆ 倍音機関", "ARGON 2D VOICE GRAVITY & VELVET ENGINE")
        : language->T ("音の輪郭", "SPECTRUM");
    fitText (g, monTitle, overviewMonitor.reduced (28, 8).withHeight (28), juce::Justification::centredLeft, 16.0f, true);

    // ---- Argon 2D Voice Gravity Orb & Velvet Engine 描画 ----
    const float vVal = processor.apvts.getRawParameterValue ("argon_velvet")->load();
    const float sVal = processor.apvts.getRawParameterValue ("argon_silk")->load();
    const float hVal = processor.apvts.getRawParameterValue ("argon_halo")->load();
    const float vAct = processor.getArgonVelvetMeter();
    const float sAct = processor.getArgonSilkMeter();
    const float hAct = processor.getArgonHaloMeter();

    if (! argonPadRect.isEmpty())
    {
        const auto padF = argonPadRect.toFloat();
        if (liteMode)
        {
            g.setColour (Palette::bgTop.darker (0.15f));
            g.fillRoundedRectangle (padF, 12.0f);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (Palette::bgTop.darker (0.28f), padF.getX(), padF.getY(),
                                                     Palette::panel2.darker (0.18f), padF.getRight(), padF.getBottom(), false));
            g.fillRoundedRectangle (padF, 12.0f);

            // 4象限の倍音カラーオーラ（左=VELVET紅、右=SILK氷、上=ASMR琥珀、下=HALO蒼）
            g.setGradientFill (juce::ColourGradient (Palette::salmon.withAlpha (0.14f), padF.getX() + 18.0f, padF.getCentreY(),
                                                     juce::Colours::transparentBlack, padF.getCentreX(), padF.getCentreY(), true));
            g.fillRoundedRectangle (padF, 12.0f);
            g.setGradientFill (juce::ColourGradient (Palette::ice.withAlpha (0.14f), padF.getRight() - 18.0f, padF.getCentreY(),
                                                     juce::Colours::transparentBlack, padF.getCentreX(), padF.getCentreY(), true));
            g.fillRoundedRectangle (padF, 12.0f);
        }
        g.setColour (Palette::panelLn);
        g.drawRoundedRectangle (padF.reduced (0.5f), 12.0f, 1.2f);

        // レーダー同心円＆コンパス目盛り＆クロスヘア
        const float cx = padF.getCentreX(), cy = padF.getCentreY();
        const float maxR = juce::jmin (padF.getWidth(), padF.getHeight()) * 0.42f;
        g.setColour (Palette::panelLn.withAlpha (0.45f));
        g.drawEllipse (cx - maxR, cy - maxR, maxR * 2.0f, maxR * 2.0f, 1.0f);
        g.drawEllipse (cx - maxR * 0.62f, cy - maxR * 0.62f, maxR * 1.24f, maxR * 1.24f, 1.0f);
        if (! liteMode)
        {
            g.drawEllipse (cx - maxR * 0.30f, cy - maxR * 0.30f, maxR * 0.60f, maxR * 0.60f, 0.8f);
            for (int deg = 0; deg < 8; ++deg)
            {
                const float a = deg * juce::MathConstants<float>::pi * 0.25f;
                const float cs = std::cos (a), sn = std::sin (a);
                g.drawLine (cx + cs * (maxR - 4.0f), cy + sn * (maxR - 4.0f),
                            cx + cs * (maxR + 3.0f), cy + sn * (maxR + 3.0f), 1.1f);
            }
        }
        g.drawHorizontalLine ((int) cy, padF.getX() + 8.0f, padF.getRight() - 8.0f);
        g.drawVerticalLine   ((int) cx, padF.getY() + 8.0f, padF.getBottom() - 8.0f);

        // 四辺の軸ラベル (14px以上厳守・狭いラック幅でも中央やオーブと重ならないよう配置)
        const bool compactPad = (argonPadRect.getWidth() < 240);
        g.setColour (Palette::inkSoft.withAlpha (0.85f));
        fitText (g, compactPad ? language->T ("◀ ベルベット", "< VELVET") : language->T ("◀ 濃密ベルベット", "< VELVET"),
                 argonPadRect.withWidth (compactPad ? 88 : 118).withHeight (20).translated (6, (int) (padF.getHeight() * 0.5f + 22.0f)),
                 juce::Justification::centredLeft, 14.0f, false);
        fitText (g, compactPad ? language->T ("シルク空気 ▶", "SILK >") : language->T ("シルク空気感 ▶", "SILK AIR >"),
                 argonPadRect.withX (argonPadRect.getRight() - (compactPad ? 94 : 124)).withWidth (compactPad ? 88 : 118).withHeight (20).translated (0, (int) (padF.getHeight() * 0.5f + 22.0f)),
                 juce::Justification::centredRight, 14.0f, false);
        fitText (g, language->T ("▲ 耳元・近接ASMR", "^ UP-FRONT ASMR"),
                 argonPadRect.withHeight (20).reduced (12, 0).translated (0, 4),
                 juce::Justification::centred, 14.0f, false);
        fitText (g, language->T ("▼ 3D立体ステージハロー", "v 3D STAGE HALO"),
                 argonPadRect.withY (argonPadRect.getBottom() - 24).withHeight (20).reduced (12, 0),
                 juce::Justification::centred, 14.0f, false);

        // 現在のオーブ位置と声量・倍音連動リップル
        const float ox = padF.getX() + 16.0f + argonOrbX * (padF.getWidth() - 32.0f);
        const float oy = padF.getY() + 16.0f + argonOrbY * (padF.getHeight() - 32.0f);
        const float inLv = juce::jlimit (0.0f, 1.0f, processor.getInputLevel() * 3.5f);
        const float pulseR = 16.0f + (vAct + sAct + hAct + inLv) * 14.0f;

        if (liteMode)
        {
            // 軽量モード: フラットな直線ベクトルとシンプルなオーブ円のみ
            g.setColour (Palette::ice.withAlpha (0.75f));
            g.drawLine (cx, cy, ox, oy, 2.0f);
            g.setColour (Palette::yellow);
            g.fillEllipse (ox - 9.0f, oy - 9.0f, 18.0f, 18.0f);
            g.setColour (Palette::ink);
            g.drawEllipse (ox - 9.0f, oy - 9.0f, 18.0f, 18.0f, 1.8f);
        }
        else
        {
            // 通常モード: 中心からのベジェ重力軌道＋声連動オーラ＋3基の倍音衛星オーブ (VELVET / SILK / HALO)
            juce::Path traj;
            traj.startNewSubPath (cx, cy);
            traj.quadraticTo ((cx + ox) * 0.5f, cy, ox, oy);
            g.setColour (Palette::ice.withAlpha (0.60f));
            g.strokePath (traj, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setGradientFill (juce::ColourGradient (Palette::ice.withAlpha (0.38f), ox, oy,
                                                     juce::Colours::transparentBlack, ox + pulseR * 1.6f, oy, true));
            g.fillEllipse (ox - pulseR * 1.6f, oy - pulseR * 1.6f, pulseR * 3.2f, pulseR * 3.2f);

            g.setColour (Palette::salmon.withAlpha (0.55f));
            g.drawEllipse (ox - pulseR, oy - pulseR, pulseR * 2.0f, pulseR * 2.0f, 1.6f);

            // 3基の倍音サテライトノード（オーブの周囲を各倍音成分の強さで軌道展開）
            const float satVals[3] = { vVal, sVal, hVal };
            const float satActs[3] = { vAct, sAct, hAct };
            const juce::Colour satCols[3] = { Palette::salmon, Palette::ice, Palette::blue };
            const float baseAngles[3] = { -2.35f, -0.55f, 1.57f };
            for (int s = 0; s < 3; ++s)
            {
                const float satDist = 14.0f + satVals[s] * 0.14f + satActs[s] * 8.0f;
                const float satAng  = baseAngles[s] + satActs[s] * 1.2f;
                const float sx = juce::jlimit (padF.getX() + 8.0f, padF.getRight() - 8.0f, ox + std::cos (satAng) * satDist);
                const float sy = juce::jlimit (padF.getY() + 8.0f, padF.getBottom() - 8.0f, oy + std::sin (satAng) * satDist);
                g.setColour (satCols[s].withAlpha (0.65f));
                g.drawLine (ox, oy, sx, sy, 1.3f);
                g.setColour (satCols[s]);
                g.fillEllipse (sx - 4.0f, sy - 4.0f, 8.0f, 8.0f);
                g.setColour (Palette::ink);
                g.drawEllipse (sx - 4.0f, sy - 4.0f, 8.0f, 8.0f, 1.0f);
            }

            // コア・オーブ本体＋スペキュラハイライト
            g.setColour (Palette::yellow);
            g.fillEllipse (ox - 10.0f, oy - 10.0f, 20.0f, 20.0f);
            g.setColour (Palette::ink);
            g.drawEllipse (ox - 10.0f, oy - 10.0f, 20.0f, 20.0f, 2.0f);
            g.setColour (Palette::readableOn (Palette::yellow));
            g.fillEllipse (ox - 3.5f, oy - 3.5f, 7.0f, 7.0f);
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.fillEllipse (ox - 6.0f, oy - 6.0f, 4.0f, 4.0f);
        }
    }

    // ---- Argon Velvet Engine 3本のダイレクトバー (VELVET / SILK / HALO) ----
    if (! argonBarRects[0].isEmpty())
    {
        const float vals[3] = { vVal, sVal, hVal };
        const float acts[3] = { vAct, sAct, hAct };
        const char* barJa[3] = { "倍音 VELVET", "空気 SILK", "立体 HALO" };
        const char* barEn[3] = { "VELVET",      "SILK AIR",  "3D HALO" };
        const juce::Colour barCols[3] = { Palette::salmon, Palette::ice, Palette::blue };

        for (int b = 0; b < 3; ++b)
        {
            const auto r = argonBarRects[(size_t) b];
            g.setColour (Palette::track.withAlpha (liteMode ? 1.0f : 0.75f));
            g.fillRoundedRectangle (r.toFloat(), 7.0f);
            const float norm = juce::jlimit (0.0f, 1.0f, vals[b] * 0.01f);
            if (norm > 0.005f)
            {
                const int fillW = juce::jmax (6, juce::roundToInt (r.getWidth() * norm));
                g.setColour (barCols[b].withAlpha (liteMode ? 0.55f : 0.42f));
                g.fillRoundedRectangle (r.withWidth (fillW).toFloat(), 7.0f);

                // 通常モードでは16セグメントのスタジオLEDスリットと実音倍音ピーク輝線を重ねる
                if (! liteMode)
                {
                    g.setColour (Palette::bgTop.withAlpha (0.28f));
                    const int segStep = juce::jmax (8, r.getWidth() / 16);
                    for (int sx = r.getX() + segStep; sx < r.getX() + fillW; sx += segStep)
                        g.drawVerticalLine (sx, (float) r.getY() + 2.0f, (float) r.getBottom() - 2.0f);

                    const float liveNorm = juce::jlimit (0.0f, 1.0f, norm * 0.7f + acts[b] * 0.6f);
                    const float peakX = (float) r.getX() + liveNorm * (float) (r.getWidth() - 4);
                    g.setColour (barCols[b].brighter (0.35f));
                    g.fillRoundedRectangle (peakX, (float) r.getY() + 2.0f, 3.0f, (float) r.getHeight() - 4.0f, 1.5f);
                }
            }
            g.setColour (barCols[b].withAlpha (0.85f));
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 7.0f, 1.2f);
            g.setColour (Palette::ink);
            fitText (g, juce::String::fromUTF8 (language->english ? barEn[b] : barJa[b]),
                     r.reduced (8, 0).withTrimmedRight (48), juce::Justification::centredLeft, 14.0f, true);
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
        for (int t = 0; t < 4; ++t)
        {
            const auto r = argonTargetRects[(size_t) t];
            const bool active = (argonActiveTarget == t);
            g.setColour (active ? tCols[t].withAlpha (0.32f) : Palette::panel2.withAlpha (0.85f));
            g.fillRoundedRectangle (r.toFloat(), 8.0f);
            g.setColour (active ? tCols[t] : Palette::panelLn);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, active ? 1.8f : 1.0f);
            g.setColour (active ? Palette::ink : Palette::inkSoft);
            const char* jaTxt = compactPill ? tJaShort[t] : tJaFull[t];
            fitText (g, juce::String::fromUTF8 (language->english ? tEn[t] : jaTxt),
                     r.reduced (4, 0), juce::Justification::centred, 14.0f, active);
        }
    }

    card (overviewOutput, Palette::green);
    const bool compactOut = (overviewOutput.getWidth() < 240);
    const int bw = compactOut ? 76 : 100;
    fitText (g, compactOut ? language->T ("計器", "I/O") : language->T ("計器 ＆ 入出力", "TELEMETRY & I/O"),
             juce::Rectangle<int> (overviewOutput.getX() + 24, overviewOutput.getY() + 8, overviewOutput.getWidth() - bw - 36, 28),
             juce::Justification::centredLeft, 16.0f, true);

    // ---- 5ch リアルタイム・テレメトリ計器盤 (IN / OUT / COMP / VELVET / HALO) ＋ 音量診断バッジ ----
    {
        const float inDb  = juce::Decibels::gainToDecibels (processor.getInputLevel(),  -60.0f);
        const float outDb = juce::Decibels::gainToDecibels (processor.getOutputLevel(), -60.0f);
        const float grDb  = juce::jlimit (0.0f, 24.0f, processor.getGainReductionDb());
        const float vPct  = processor.apvts.getRawParameterValue ("argon_velvet")->load();
        const float hPct  = processor.apvts.getRawParameterValue ("argon_halo")->load();

        const bool clipRisk = outDb > -1.0f;
        const bool goodLvl  = outDb >= -20.0f && outDb <= -1.0f;
        const bool quietLvl = outDb > -52.0f && outDb < -20.0f;
        const juce::String statusTxt = clipRisk ? language->T ("音割れ注意", "HOT / CLIP")
                                     : goodLvl  ? language->T ("適正音量", "GOOD LEVEL")
                                     : quietLvl ? language->T ("少し小さい", "A BIT LOW")
                                                : language->T ("待機中", "READY");
        const juce::Colour statusCol = clipRisk ? Palette::salmon
                                     : goodLvl  ? Palette::green
                                     : quietLvl ? Palette::yellow
                                                : Palette::inkSoft;
        auto badgeR = juce::Rectangle<int> (overviewOutput.getRight() - bw - 10, overviewOutput.getY() + 10, bw, 24);
        g.setColour (statusCol.withAlpha (0.18f));
        g.fillRoundedRectangle (badgeR.toFloat(), 6.0f);
        g.setColour (statusCol.withAlpha (0.65f));
        g.drawRoundedRectangle (badgeR.toFloat().reduced (0.5f), 6.0f, 1.0f);
        fitText (g, statusTxt, badgeR.reduced (4, 0), juce::Justification::centred, 14.0f, true);

        // メーター描画領域: Studio Rack(mode 2) と Classic(mode 3) は上部全幅、Argon(0) と Liver(1) は左半分
        const int meterW = (overviewLayoutMode == 0 || overviewLayoutMode == 1)
                         ? (overviewOutput.getWidth() - 236)
                         : (overviewOutput.getWidth() - 24);
        const int numRows = (overviewLayoutMode == 3) ? 3 : 5;
        const int rowStep = (overviewLayoutMode == 3) ? 18 : 32;

        struct MeterRow { juce::String name; float norm; juce::String valTxt; juce::Colour col; };
        const MeterRow rows[5] = {
            { language->T ("入力", "IN"),
              juce::jlimit (0.0f, 1.0f, (inDb + 60.0f) / 60.0f),
              inDb <= -59.5f ? "-inf" : (juce::String (juce::roundToInt (inDb)) + "dB"),
              inDb > -1.0f ? Palette::salmon : Palette::green },
            { language->T ("出力", "OUT"),
              juce::jlimit (0.0f, 1.0f, (outDb + 60.0f) / 60.0f),
              outDb <= -59.5f ? "-inf" : (juce::String (juce::roundToInt (outDb)) + "dB"),
              outDb > -1.0f ? Palette::salmon : (outDb >= -20.0f ? Palette::green : Palette::ice) },
            { language->T ("圧縮", "COMP"),
              juce::jlimit (0.0f, 1.0f, grDb / 18.0f),
              "-" + juce::String (grDb, 1) + "dB",
              Palette::yellow },
            { language->T ("倍音", "VELV"),
              juce::jlimit (0.0f, 1.0f, vPct * 0.01f),
              juce::String (juce::roundToInt (vPct)) + "%",
              Palette::salmon },
            { language->T ("空間", "HALO"),
              juce::jlimit (0.0f, 1.0f, hPct * 0.01f),
              juce::String (juce::roundToInt (hPct)) + "%",
              Palette::blue }
        };
        for (int i = 0; i < numRows; ++i)
        {
            const int ry = overviewOutput.getY() + 42 + i * rowStep;
            auto labR = juce::Rectangle<int> (overviewOutput.getX() + 14, ry, 44, 18);
            auto valR = juce::Rectangle<int> (overviewOutput.getX() + 14 + meterW - 52, ry, 52, 18);
            auto barR = juce::Rectangle<int> (labR.getRight() + 4, ry + 5, juce::jmax (16, valR.getX() - labR.getRight() - 8), 8);
            g.setColour (Palette::ink);
            fitText (g, rows[i].name, labR, juce::Justification::centredLeft, 14.0f, true);
            g.setColour (Palette::track);
            g.fillRoundedRectangle (barR.toFloat(), 4.0f);
            if (rows[i].norm > 0.005f)
            {
                g.setColour (rows[i].col);
                g.fillRoundedRectangle (barR.withWidth (juce::jmax (4, juce::roundToInt (barR.getWidth() * rows[i].norm))).toFloat(), 4.0f);
            }
            g.setColour (Palette::inkSoft);
            fitText (g, rows[i].valTxt, valR, juce::Justification::centredRight, 14.0f, false);
        }
    }

    const char* namesJa[] = { "01 整える", "02 声をつくる", "03 空間をつくる" };
    const char* namesEn[] = { "01 CLEAN",  "02 CHARACTER",  "03 SPACE" };
    const juce::Colour accents[] = { Palette::green, Palette::blue, Palette::yellow };
    for (int i = 0; i < 3; ++i)
    {
        auto r = overviewCards[(size_t) i];
        card (r, accents[i]);

        // ♥ V-LIVER STAGE 配置では下段12連マカロンノブの背後に優しい五線譜リボンを流す
        if (overviewLayoutMode == 1 && ! liteMode)
        {
            g.setColour (accents[i].withAlpha (0.13f));
            const int staffY = r.getY() + 190;
            for (int sl = 0; sl < 5; ++sl)
                g.drawHorizontalLine (staffY + sl * 9, (float) r.getX() + 14.0f, (float) r.getRight() - 14.0f);
        }

        auto titleR = juce::Rectangle<int> (r.getX() + 26, r.getY() + 8,
                                            juce::jmax (60, overviewSectionPower[(size_t) i].getX() - (r.getX() + 30)), 36);
        g.setColour (Palette::ink);
        fitText (g, juce::String::fromUTF8 (language->english ? namesEn[i] : namesJa[i]),
                 titleR, juce::Justification::centredLeft, 18.0f, true);
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
    const auto defaultHint = language->T ("体験版：60秒ごとに0.6秒だけ音量が下がります。中央の2D重力場オーブをドラッグすると、母音2次倍音・14kHzシルク空気感・3D立体ハローを直感的に成形できます。", "Trial: sound dips for 0.6s every 60s. Drag the 2D Voice Gravity Orb to sculpt Voiced Triode Velvet, 14kHz Silk Air, and 3D Binaural Halo.");
   #else
    const auto defaultHint = language->T ("【Argon 2D 重力場オーブ】中央のオーブをドラッグするか4つのターゲットを選ぶと、母音2次倍音(VELVET)・14kHz空気感(SILK)・モノ互換立体空間(HALO)を同時に成形できます。上部の [★ARGON / ♥LIVER / 🎛STUDIO] で画面構造、右隣の [⚡軽量] で省負荷エコ描画へ即座に切替できます。", "Drag the 2D Voice Gravity Orb or pick a target to sculpt Triode Velvet, 14kHz Silk Air and 3D Halo. Switch workspace architectures or toggle [LITE] eco mode in the top bar.");
   #endif
    const auto hint = dnMsgTicks > 0 && infoText.isNotEmpty() ? infoText
                    : overviewHint.isNotEmpty() ? overviewHint : defaultHint;
    g.setColour (Palette::ink);
    g.setFont (GzzioLnF::uiFont (18.0f * juce::jlimit (1.0f, 1.3f, fontScale / 1.5f), false));
    g.drawFittedText (hint, overviewHelp.reduced (18, 6), juce::Justification::centredLeft, 2, 1.0f);
}
