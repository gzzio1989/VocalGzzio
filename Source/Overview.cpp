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

// 総合画面は同じパラメータ部品を使う。表示の切り替えで音を変えない。
void VocalGzzioContent::initialiseOverview()
{
    srcModeBox.setComponentID ("sourceMode");
    learnButton.setComponentID ("learnNoise");
    relearnBtn.setComponentID ("autoLearnNoise");
    finishPresetBox.setComponentID ("finishPreset");
    finishPresetBox.setTooltip (tip::T ("目的から仕上がりを選びます。音色・音量の整え方・残響が一緒に変わります。入力音量と出力音量、ノイズ学習、音程の設定は保持します。", "Choose a complete tone, dynamics and ambience recipe. Input/output gain, noise learning and pitch settings are preserved."));
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
        overviewHint = juce::String::fromUTF8 (tip::english ? gzzio::kFinishPresets[index].descEn : gzzio::kFinishPresets[index].desc);
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
    for (const auto& item : overviewOldVisibility) item.first->setVisible (item.second);
    overviewOldVisibility.clear();
    for (auto* k : { &inGainK, &makeupK, &denoiseK, &gate, &lowCut, &mudK,
                     &comp2K, &deessK, &presenceK, &airK, &warmthK, &resK,
                     &revMixK, &revSizeK, &delayK, &widthK })
    { k->slider.setEnabled (true); k->label.setAlpha (1.0f); }
    for (auto* c : std::initializer_list<juce::Component*> { &finishPresetBox, &overviewDetails, &overviewCompare,
                     &overviewPitch, &overviewMore[0], &overviewMore[1], &overviewMore[2],
                     &overviewSectionPower[0], &overviewSectionPower[1], &overviewSectionPower[2] })
        c->setVisible (false);
}

void VocalGzzioContent::updateOverviewVisibility()
{
    if (! isOverview()) return;
    if (overviewOldVisibility.empty())
        for (auto* c : getChildren()) overviewOldVisibility.emplace_back (c, c->isVisible());
    for (auto* c : getChildren()) c->setVisible (false);
    for (auto* c : { (juce::Component*) &finishPresetBox, (juce::Component*) &srcModeBox,
         (juce::Component*) &overviewDetails, (juce::Component*) &overviewCompare,
         (juce::Component*) &overviewPitch, (juce::Component*) &sizePopBtn,
         (juce::Component*) &sessionButton,
         (juce::Component*) &tuningPopBtn, (juce::Component*) &saveButton,
         (juce::Component*) &loadButton, (juce::Component*) &abA, (juce::Component*) &abB,
         (juce::Component*) &abCopy, (juce::Component*) &tuner, (juce::Component*) &eqGraph,
         (juce::Component*) &learnButton, (juce::Component*) &relearnBtn,
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
    // 概要からは音の出ない操作を除き、詳細への入口は常に残す。
    applyUseModeMask();
    overviewReturn.setVisible (false);
    updateOverviewState();
}

void VocalGzzioContent::updateOverviewState()
{
    auto active = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load() > 0.5f; };
    for (int i = 0; i < 3; ++i)
    {
        const auto modules = overviewModules (i);
        int count = 0;
        for (auto* id : modules) if (active (id)) ++count;
        auto& button = overviewSectionPower[(size_t) i];
        button.setToggleState (count > 0, juce::dontSendNotification);
        button.setButtonText (count == (int) modules.size() ? tip::T ("入", "ON") : count == 0 ? tip::T ("切", "OFF") : tip::T ("一部", "PART"));
        button.setTooltip (tip::T ("この区画の処理をまとめて入／切します。詳細画面で一部だけ切っている場合は「一部」と表示します。", "Enable or bypass this section. PART means some processors were bypassed in the detailed view."));
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
    learnButton.setButtonText (tip::T ("ノイズを測る", "Learn noise"));
    relearnBtn.setButtonText (tip::dn_relearn_label());
    const int selected = finishPresetBox.getSelectedId();
    finishPresetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < gzzio::kNumFinishPresets; ++i)
        finishPresetBox.addItem (juce::String::fromUTF8 (tip::english ? gzzio::kFinishPresets[i].nameEn : gzzio::kFinishPresets[i].name), i + 1);
    finishPresetBox.setTextWhenNothingSelected (tip::T ("仕上がりを選ぶ", "Choose a finish"));
    finishPresetBox.setSelectedId (selected > 0 ? selected : finishPresetIndex + 1, juce::dontSendNotification);
    overviewDetails.setButtonText (tip::T ("詳細調整", "All controls"));
    overviewReturn.setButtonText (tip::T ("総合画面", "Overview"));
    overviewPitch.setButtonText (tip::T ("音程・ハモリ", "Pitch & harmony"));
    overviewCompare.setButtonText (tip::T ("押して原音と比較", "Hold to compare"));
    overviewCompare.setTooltip (tip::T ("押している間は処理前の音を聴けます。離すと元の設定に戻ります。音量の差だけで判断せず、声の輪郭や余韻を比べてください。", "Hold to hear the unprocessed input. Release to return. Compare the tone and tail as well as loudness."));
    reverbPower.setButtonText (tip::T ("リバーブ 入／切", "Reverb ON / OFF"));
    reverbPower.setTooltip (tip::rev_tip());
    reverbPower.setColour (juce::TextButton::buttonOnColourId, Palette::green);
    for (auto& power : overviewSectionPower) power.setColour (juce::TextButton::buttonOnColourId, Palette::green);
    tuningPopBtn.setButtonText (tip::T ("チューナー", "Tuner"));
    sessionButton.setButtonText (tip::T ("低遅延モード", "Low latency"));
    for (auto& b : overviewMore) b.setButtonText (tip::T ("詳しく調整", "More"));
    auto label = [] (Knob& k, const char* jp, const char* en)
    { k.label.setText (tip::T (jp, en), juce::dontSendNotification); };
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
        // 同じノブ部品を再利用するため、総合画面の長いラベルを詳細へ持ち越さない。
        // テーマ変更時もモード変更時も、その画面で使う長さへ戻す。
        label (inGainK, "入力音量", "Input"); label (makeupK, "出力音量", "Output");
        label (denoiseK, "ノイズ除去", "DENOISE"); label (gate, "ゲート", "GATE");
        label (lowCut, "ローカット", "LOW CUT"); label (mudK, "こもり", "MUD");
        label (comp2K, advancedMode ? "ならし圧縮" : "音量をそろえる", "LEVELER");
        label (deessK, "サ行おさえ", "DE-ESS");
        label (presenceK, advancedMode ? "ヌケ感" : "声の明るさ", "PRESENCE");
        label (airK, "キラキラ", "AIR"); label (warmthK, "あたたかみ", "WARMTH");
        resK.label.setText (tip::res_label(), juce::dontSendNotification);
        label (revMixK, "ひびき", "REVERB"); label (revSizeK, "部屋の広さ", "ROOM SIZE");
        label (delayK, "やまびこ", "ECHO"); label (widthK, "ひろがり", "WIDTH");
    }
    auto explain = [] (Knob& k, const char* jp, const char* en)
    { k.slider.setTooltip (tip::T (jp, en)); k.label.setTooltip (tip::T (jp, en)); };
    explain (denoiseK, "一定のサー音を小さくします。まず黙った状態で「ノイズを測る」を押し、量を少しずつ上げます。声が薄くなったら下げてください。", "Reduces steady hiss. Measure the noise while silent, then raise the amount gradually. Back off if the voice becomes thin.");
    explain (comp2K, "強い声と弱い声の差を小さくします。上げるほど声量がそろいます。抑揚が平らになったら下げます。出力音量とは別の調整です。", "Reduces the difference between loud and quiet phrases. Increase for steadier level; reduce to preserve expression. This is not output gain.");
    explain (presenceK, "言葉の輪郭が聞こえる中高音を調整します。伴奏に埋もれるときは上げ、硬く耳に当たるときは下げます。", "Adjusts the upper mids that carry vocal clarity. Raise it to cut through a mix, lower it if the voice sounds hard.");
    explain (revMixK, "原音に足す残響の大きさです。上げるほど遠く、包まれる印象になります。言葉がぼやけたら下げます。入／切で残響だけ比較できます。", "Controls how much reverberation is added. Raise for distance and ambience, lower if words blur. Use the reverb switch to compare.");
    explain (revSizeK, "残響の余韻を調整します。上げるほど広い空間や長い余韻になります。速い曲では短くすると次の言葉を邪魔しにくくなります。", "Controls the size and decay of the selected space. Shorter settings leave more room for fast phrases.");
}

void VocalGzzioContent::resizedOverview()
{
    if (getWidth() < 100 || getHeight() < 100) return;
    const int w = getWidth(), h = getHeight(), edge = 24, gap = 16;
    crossArea = heroArea = cleanArea = dynArea = toneArea = spaceArea = seqArea = {};
    focusHeadArea = focusTipArea = focusFinishArea = focusSideArea = focusMeterArea = {};
    revTypeLabArea = crushReadArea = fxWarnArea = voiceStageArea = {};
    const int buttonH = 40;
    auto toolbar = juce::Rectangle<int> (w - 628, 22, 604, buttonH);
    sizePopBtn.setBounds (toolbar.removeFromRight (170)); toolbar.removeFromRight (10);
    overviewDetails.setBounds (toolbar.removeFromRight (136)); toolbar.removeFromRight (10);
    tuningPopBtn.setBounds (toolbar.removeFromRight (126)); toolbar.removeFromRight (10);
    overviewPitch.setBounds (toolbar);
    sessionButton.setBounds (718, 22, 200, buttonH);
    latBadgeArea = { 354, 24, 350, 36 };
    auto preset = juce::Rectangle<int> (edge, 99, w - edge * 2, 42);
    preset.removeFromLeft (100);
    finishPresetBox.setBounds (preset.removeFromLeft (w * 29 / 100)); preset.removeFromLeft (18);
    srcModeBox.setBounds (preset.removeFromLeft (174)); preset.removeFromLeft (18);
    overviewCompare.setBounds (preset.removeFromLeft (210)); preset.removeFromLeft (18);
    abA.setBounds (preset.removeFromLeft (44)); preset.removeFromLeft (6);
    abB.setBounds (preset.removeFromLeft (44)); preset.removeFromLeft (6);
    abCopy.setBounds (preset.removeFromLeft (66)); preset.removeFromLeft (18);
    saveButton.setBounds (preset.removeFromLeft (94)); preset.removeFromLeft (8);
    loadButton.setBounds (preset.removeFromLeft (94));
    const int top = 163, monitorH = 224;
    const int tunerW = w * 49 / 100, outputW = 274;
    tuner.setBounds (edge, top, tunerW, monitorH);
    overviewOutput = { w - edge - outputW, top, outputW, monitorH };
    overviewMonitor = { tuner.getRight() + gap, top, overviewOutput.getX() - gap * 2 - tuner.getRight(), monitorH };
    eqGraph.setBounds (overviewMonitor.reduced (14).withTrimmedTop (35).withTrimmedBottom (8));
    const int cardsTop = top + monitorH + gap;
    const int bottom = h - 98;
    const int usable = w - edge * 2 - gap * 2;
    const int widths[] = { usable / 3, usable / 3, usable - 2 * (usable / 3) };
    int x = edge;
    for (int i = 0; i < 3; ++i)
    {
        overviewCards[(size_t) i] = { x, cardsTop, widths[i], bottom - cardsTop };
        overviewMore[(size_t) i].setBounds (x + widths[i] - 136, cardsTop + 14, 120, 36);
        overviewSectionPower[(size_t) i].setBounds (x + widths[i] - 224, cardsTop + 14, 78, 36);
        x += widths[i] + gap;
    }
    auto grid = [this] (juce::Rectangle<int> r, std::initializer_list<Knob*> knobs, int columns)
    {
        const int count = (int) knobs.size(), rows = (count + columns - 1) / columns;
        const int cw = r.getWidth() / columns, ch = r.getHeight() / rows;
        int i = 0;
        for (auto* k : knobs)
        {
            auto cell = juce::Rectangle<int> (r.getX() + i % columns * cw, r.getY() + i / columns * ch, cw, ch).reduced (5, 2);
            k->label.getProperties().set ("fontH", 13.5);
            k->label.setBounds (cell.removeFromTop (34));
            k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, juce::jmin (100, cell.getWidth()), 27);
            k->slider.setBounds (cell.reduced (4, 0));
            ++i;
        }
    };
    grid (overviewOutput.reduced (10).withTrimmedTop (60), { &inGainK, &makeupK }, 2);
    grid (overviewCards[0].reduced (16).withTrimmedTop (102), { &denoiseK, &gate, &lowCut, &mudK }, 2);
    grid (overviewCards[1].reduced (16).withTrimmedTop (102), { &comp2K, &deessK, &presenceK, &warmthK }, 2);
    grid (overviewCards[2].reduced (16).withTrimmedTop (102), { &revMixK, &revSizeK, &delayK, &widthK }, 2);
    auto learning = overviewCards[0].reduced (16).withTrimmedTop (55).withHeight (36);
    learnButton.setBounds (learning.removeFromLeft (learning.getWidth() / 2)); learning.removeFromLeft (8);
    relearnBtn.setBounds (learning);
    auto reverb = overviewCards[2].reduced (16).withTrimmedTop (55).withHeight (36);
    reverbPower.setBounds (reverb.removeFromLeft (juce::jmin (210, reverb.getWidth() / 2)));
    reverb.removeFromLeft (8); revTypeBox.setBounds (reverb);
    for (auto* lamp : { &lampDn, &lampGate, &lampDs, &lampDly }) lamp->btn.setSize (28, 28);
    auto placePower = [] (Lamp& lamp, Knob& k)
    {
        const auto b = k.slider.getBounds();
        lamp.btn.setBounds (b.getRight() - 30, b.getY() + 2, 28, 28);
        lamp.btn.toFront (false);
    };
    placePower (lampDn, denoiseK); placePower (lampGate, gate);
    placePower (lampDs, deessK); placePower (lampDly, delayK);
    overviewHelp = { edge, h - 82, w - edge * 2, 64 };
}

void VocalGzzioContent::paintOverview (juce::Graphics& g)
{
    GzzioLnF::setUseKawaiiFont (pastelTheme());
    g.fillAll (Palette::bgBot);
    auto text = [&] (const juce::String& value, juce::Rectangle<int> r, float height, juce::Colour colour, bool bold = false)
    {
        g.setColour (colour);
        g.setFont (GzzioLnF::uiFont (height * juce::jlimit (1.0f, 1.3f, fontScale / 1.5f), bold));
        g.drawText (value, r, juce::Justification::centredLeft);
    };
    g.setColour (Palette::yellow);
    g.fillRoundedRectangle (24.0f, 24.0f, 38.0f, 38.0f, 11.0f);
    g.setColour (Palette::bgBot);
    const float bars[] = { 9, 19, 27, 14 };
    for (int i = 0; i < 4; ++i) g.fillRoundedRectangle (33.0f + i * 6.0f, 43.0f - bars[i] * 0.5f, 3.0f, bars[i], 1.5f);
    text ("VocalGzzio", { 76, 18, 270, 42 }, 31, Palette::ink, true);
    const auto latencyMs = 1000.0 * processor.addedLatencySamples() / juce::jmax (1.0, processor.getTunerSampleRate());
   #if VOCALGZZIO_TRIAL
    text ("4.2 / " + tip::T ("体験版 / 遅延 ", "TRIAL / Latency ") + juce::String (latencyMs, 1) + " ms", latBadgeArea, 19, Palette::yellow, true);
   #else
    text ("4.2  /  " + tip::T ("追加遅延 ", "Latency ") + juce::String (latencyMs, 1) + " ms", latBadgeArea, 21, Palette::inkSoft);
   #endif
    g.setColour (Palette::panelLn);
    g.drawHorizontalLine (80, 24.0f, (float) getWidth() - 24.0f);
    text (tip::T ("仕上がり", "FINISH"), { 28, 100, 90, 40 }, 19, Palette::ink, true);
    auto card = [&] (juce::Rectangle<int> r, juce::Colour accent)
    {
        g.setColour (Palette::panel); g.fillRoundedRectangle (r.toFloat(), 16.0f);
        g.setColour (Palette::panelLn); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 16.0f, 1.0f);
        g.setColour (accent); g.fillRoundedRectangle ((float) r.getX() + 18, (float) r.getY() + 20, 4.0f, 24.0f, 2.0f);
    };
    card (overviewMonitor, Palette::blue);
    text (tip::T ("音の輪郭", "SPECTRUM"), overviewMonitor.reduced (30, 12).withHeight (30), 17, Palette::ink, true);
    card (overviewOutput, Palette::green);
    text (tip::T ("入出力", "LEVELS"), overviewOutput.reduced (30, 12).withHeight (30), 17, Palette::ink, true);
    const char* namesJa[] = { "整える", "声をつくる", "空間をつくる" };
    const char* namesEn[] = { "CLEAN", "CHARACTER", "SPACE" };
    const juce::Colour accents[] = { Palette::green, Palette::blue, Palette::yellow };
    for (int i = 0; i < 3; ++i)
    {
        auto r = overviewCards[(size_t) i];
        card (r, accents[i]);
        text (juce::String::fromUTF8 (tip::english ? namesEn[i] : namesJa[i]), r.reduced (32, 10).withHeight (40), 23, Palette::ink, true);
    }
    text (tip::T ("音量の差と、声の質感を整える", "Shape dynamics and vocal texture"), overviewCards[1].reduced (24).withTrimmedTop (40).withHeight (36), 20, Palette::inkSoft);
    const float levels[] = { processor.getInputLevel(), processor.getOutputLevel() };
    for (int i = 0; i < 2; ++i)
    {
        auto r = overviewOutput.reduced (20).withTrimmedTop (28).withHeight (6);
        r.setWidth ((overviewOutput.getWidth() - 52) / 2);
        r.setX (overviewOutput.getX() + 20 + i * (r.getWidth() + 12));
        g.setColour (Palette::track); g.fillRoundedRectangle (r.toFloat(), 3.0f);
        const float db = juce::Decibels::gainToDecibels (levels[i], -60.0f);
        g.setColour (db > -1.0f ? Palette::salmon : Palette::green);
        g.fillRoundedRectangle (r.withWidth (juce::roundToInt (r.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f))).toFloat(), 3.0f);
    }
    g.setColour (Palette::panel2); g.fillRoundedRectangle (overviewHelp.toFloat(), 12.0f);
   #if VOCALGZZIO_TRIAL
    const auto defaultHint = tip::T ("体験版：60秒ごとに0.6秒だけ音量が下がります。設定の保存・読込は利用できません。つまみにカーソルを合わせると、効果と使いどころを表示します。", "Trial: the sound dips for 0.6 seconds every 60 seconds. Settings cannot be saved or loaded. Hover over a control to see its effect and when to use it.");
   #else
    const auto defaultHint = tip::T ("仕上がりを選び、原音と比べながら調整。つまみにカーソルを合わせると、変化と使いどころをここに表示します。", "Choose a finish, compare it with the original, then adjust. Hover over a control to see what it changes and when to use it.");
   #endif
    const auto hint = overviewHint.isNotEmpty() ? overviewHint : defaultHint;
    g.setColour (Palette::ink);
    g.setFont (GzzioLnF::uiFont (21.0f * juce::jlimit (1.0f, 1.3f, fontScale / 1.5f), false));
    g.drawFittedText (hint, overviewHelp.reduced (18, 8), juce::Justification::centredLeft, 2, 1.0f);
}
