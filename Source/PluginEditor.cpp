#include "PluginEditor.h"
#include "BinaryData.h"
#include "Tooltips.h"
#include <cmath>

namespace
{
    juce::Font makeFont (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions().withHeight (h).withStyle (bold ? "Bold" : "Regular"));
    }
    const char* kNoteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
}

//==============================================================================
// Tuner
//==============================================================================
void VocalTuner::analyse()
{
    proc.readTunerBuffer (buffer);
    const int    N  = (int) buffer.size();
    const double sr = proc.getTunerSampleRate();
    if (N < 256 || sr < 8000.0) { hasPitch = false; return; }

    double sum = 0.0, mean = 0.0;
    for (int i = 0; i < N; ++i) mean += buffer[(size_t) i];
    mean /= N;
    for (int i = 0; i < N; ++i) { double v = buffer[(size_t) i] - mean; sum += v * v; }
    const double rms = std::sqrt (sum / N);
    if (rms < 0.0012) { hasPitch = false; return; }

    const int minLag = juce::jmax (2, (int) (sr / 1200.0));   // up to ~1.2 kHz (soprano head voice)
    const int maxLag = juce::jmin (N - 2, (int) (sr / 65.0)); // down to ~65 Hz (low bass voice)
    if (maxLag <= minLag) { hasPitch = false; return; }

    std::vector<double> corr ((size_t) (maxLag + 1), 0.0);
    double best = 0.0; int bestLag = -1;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double ac = 0.0;
        for (int i = 0; i < N - lag; ++i)
            ac += (buffer[(size_t) i] - mean) * (buffer[(size_t) (i + lag)] - mean);
        corr[(size_t) lag] = ac;
        if (ac > best) { best = ac; bestLag = lag; }
    }
    if (bestLag <= 0 || best <= 0.0) { hasPitch = false; return; }

    double period = bestLag;
    if (bestLag > minLag && bestLag < maxLag)
    {
        const double a = corr[(size_t) (bestLag - 1)];
        const double b = corr[(size_t) bestLag];
        const double c = corr[(size_t) (bestLag + 1)];
        const double denom = (a - 2.0 * b + c);
        if (std::abs (denom) > 1e-12)
            period = bestLag + 0.5 * (a - c) / denom;
    }

    freq = (float) (sr / period);
    const float refHz = proc.apvts.getRawParameterValue ("refpitch")->load();
    const double midi = 69.0 + 12.0 * std::log2 (freq / refHz);
    const int nearest = (int) std::lround (midi);
    cents = (float) ((midi - nearest) * 100.0);
    const int nn  = ((nearest % 12) + 12) % 12;
    const int oct = nearest / 12 - 1;
    noteName = juce::String (kNoteNames[nn]) + juce::String (oct);
    hasPitch = true;
}

void VocalTuner::timerCallback()
{
    analyse();

    if (hasPitch)
    {
        hold = 18;
        const float sm = 0.12f;
        dispCents += (cents - dispCents) * sm;
        dispFreq  += (freq  - dispFreq)  * sm;
        if (dispFreq <= 0.0f) dispFreq = freq;
    }
    else if (hold > 0)
    {
        --hold;
        dispCents += (0.0f - dispCents) * 0.06f;
    }

    dispIn  += (proc.getInputLevel()      - dispIn)  * 0.25f;
    dispOut += (proc.getOutputLevel()     - dispOut) * 0.25f;
    dispGR  += (proc.getGainReductionDb() - dispGR)  * 0.25f;
    dispDS  += (proc.getDeEssActivity()   - dispDS)  * 0.30f;
    dispDN  += (proc.getDenoiseActivity() - dispDN)  * 0.30f;

    const int apvtsHz = (int) std::round (proc.apvts.getRawParameterValue ("refpitch")->load());
    if (refPitchBox.getSelectedId() != apvtsHz)
        refPitchBox.setSelectedId (apvtsHz, juce::dontSendNotification);

    repaint();
}

void VocalTuner::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (Palette::panelLn);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.2f);

    g.setColour (Palette::inkSoft);
    g.setFont (makeFont (11.0f, true));
    g.drawText ("TUNER  &  METER", r.withTrimmedLeft (14).withHeight (22).translated (0, 4),
                juce::Justification::centredLeft);

    auto body = getLocalBounds().reduced (16, 8).withTrimmedTop (16);
    const bool show = (hold > 0);

    auto noteBox = body.removeFromLeft (108);
    g.setColour (show ? Palette::ink : Palette::ink.withAlpha (0.35f));
    g.setFont (makeFont (38.0f, true));
    g.drawText (show ? noteName : juce::String ("--"),
                noteBox.withHeight (46), juce::Justification::centred);

    auto meter = body.withHeight (26).reduced (8, 4).toFloat();
    const float midX = meter.getCentreX();
    g.setColour (Palette::track);
    g.fillRoundedRectangle (meter, 5.0f);
    g.setColour (Palette::inkSoft);
    g.fillRect (midX - 1.0f, meter.getY(), 2.0f, meter.getHeight());

    if (show)
    {
        const float norm = juce::jlimit (-1.0f, 1.0f, dispCents / 50.0f);
        const float px = midX + norm * (meter.getWidth() * 0.5f - 6.0f);
        const bool inTune = std::abs (dispCents) < 5.0f;
        g.setColour (inTune ? Palette::green : Palette::yellowDk);
        g.fillRoundedRectangle (px - 4.0f, meter.getY() + 2.0f, 8.0f, meter.getHeight() - 4.0f, 3.0f);

        g.setColour (inTune ? Palette::green : Palette::inkSoft);
        g.setFont (makeFont (11.5f, true));
        g.drawText ((dispCents >= 0 ? "+" : "") + juce::String (juce::roundToInt (dispCents)) + " cent / "
                        + juce::String (dispFreq, 1) + " Hz",
                    body.withTrimmedTop (28).withHeight (16),
                    juce::Justification::centredLeft);
    }

    // meter bars: IN OUT GR DS
    {
        auto mArea = getLocalBounds().reduced (14, 4).withTop (getHeight() - 56).toFloat();
        const float bH = 7.0f, bGap = 3.0f;
        auto toNorm = [] (float lin) {
            float db = 20.0f * std::log10 (juce::jmax (lin, 1e-6f));
            return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        };
        struct Bar { float val; const char* label; juce::Colour col; };
        Bar bars[5] = {
            { toNorm (dispIn),  "IN",  Palette::green    },
            { toNorm (dispOut), "OUT", Palette::yellowDk },
            { juce::jlimit (0.0f, 1.0f, -dispGR / 18.0f), "GR", Palette::salmon },
            { juce::jlimit (0.0f, 1.0f,  dispDS),         "DS", Palette::ink    },
            { juce::jlimit (0.0f, 1.0f,  dispDN),         "DN", Palette::green.darker (0.2f) }
        };
        for (auto& bar : bars)
        {
            g.setColour (Palette::track);
            g.fillRoundedRectangle (mArea.withHeight (bH), 3.0f);
            g.setColour (bar.col);
            g.fillRoundedRectangle (mArea.withWidth (juce::jmax (2.0f, mArea.getWidth() * bar.val)).withHeight (bH), 3.0f);
            g.setColour (Palette::inkSoft);
            g.setFont (makeFont (9.0f, true));
            g.drawText (bar.label, mArea.withHeight (bH).translated (2, -1), juce::Justification::centredLeft);
            mArea.setY (mArea.getY() + bH + bGap);
        }
    }
}

void VocalTuner::resized()
{
    refPitchBox.setBounds (getWidth() - 96, 4, 90, 20);
}

//==============================================================================
// Content
//==============================================================================
VocalGzzioContent::VocalGzzioContent (VocalGzzioProcessor& p)
    : processor (p), tuner (p)
{
    mascot = juce::ImageCache::getFromMemory (BinaryData::character_png, BinaryData::character_pngSize);

    // knobs (order = signal flow, grouped)
    addKnob (gate,     "gate",     "GATE",      tip::gate_tip(),  "dB");
    addKnob (lowCut,   "lowcut",   "LOW CUT",   tip::lowcut(),    "Hz");
    addKnob (mudK,     "mud",      juce::String::fromUTF8 ("\xe3\x81\x93\xe3\x82\x82\xe3\x82\x8a"), tip::mud(), "dB");
    addKnob (harshK,   "harsh",    juce::String::fromUTF8 ("\xe3\x82\xad\xe3\x83\xb3\xe3\x82\xad\xe3\x83\xb3"), tip::harsh(), "dB");
    addKnob (denoiseK, "denoise",  "DE-NOISE",  tip::denoise_tip(), "%");

    addKnob (comp1K,   "comp1",    "PEAK COMP", tip::comp1_tip(), "%");
    addKnob (comp2K,   "comp2",    "LEVEL COMP",tip::comp2_tip(), "%");
    addKnob (attackK,  "attack",   "ATTACK",    tip::attack(),    "ms");
    addKnob (releaseK, "release",  "RELEASE",   tip::release(),   "ms");
    addKnob (deessK,   "deess",    "DE-ESSER",  tip::deess(),     "%");

    addKnob (presenceK,"presence", "PRESENCE",  tip::presence(),  "dB");
    addKnob (airK,     "air",      "AIR",       tip::air(),       "dB");
    addKnob (warmthK,  "drive",    "WARMTH",    tip::warmth(),    "%");
    addKnob (sustainK, "sustain",  juce::String::fromUTF8 ("\xe3\x81\xae\xe3\x81\xb3"), tip::sustain_tip(), "%");

    addKnob (makeupK,  "makeup",   "MAKEUP",    tip::makeup(),    "dB");
    addKnob (mixK,     "mix",      "MIX",       tip::mix(),       "%");
    addKnob (widthK,   "width",    "WIDTH",     tip::width(),     "%");
    addKnob (doublerK, "doubler",  "DOUBLER",   tip::doubler(),   "%");
    addKnob (delayK,   "delay",    "DELAY",     tip::delay_tip(), "%");
    addKnob (revSizeK, "revsize",  "REV SIZE",  tip::revsize(),   "%");
    addKnob (revMixK,  "revmix",   "REVERB",    tip::revmix(),    "%");

    // ---- preset combos: voice type (10) & mic model (10) ----
    voiceBox.setTextWhenNothingSelected (tip::voice_placeholder());
    voiceBox.addSectionHeading (tip::female_head());
    voiceBox.addItem (tip::vp1(), 1);  voiceBox.addItem (tip::vp2(), 2);
    voiceBox.addItem (tip::vp3(), 3);  voiceBox.addItem (tip::vp4(), 4);
    voiceBox.addItem (tip::vp5(), 5);
    voiceBox.addSectionHeading (tip::male_head());
    voiceBox.addItem (tip::vp6(), 6);  voiceBox.addItem (tip::vp7(), 7);
    voiceBox.addItem (tip::vp8(), 8);  voiceBox.addItem (tip::vp9(), 9);
    voiceBox.addItem (tip::vp10(), 10);
    voiceBox.setTooltip (tip::voicebox_tip());
    voiceBox.onChange = [this] { if (voiceBox.getSelectedId() > 0) applyVoicePreset (voiceBox.getSelectedId()); };
    addAndMakeVisible (voiceBox);

    micBox.setTextWhenNothingSelected (tip::mic_placeholder());
    micBox.addSectionHeading (tip::dyn_head());
    micBox.addItem ("Shure SM58", 1);        micBox.addItem ("Shure SM7B", 2);
    micBox.addItem ("EV RE20", 3);           micBox.addItem ("Sennheiser e935", 4);
    micBox.addItem ("Sennheiser MD421", 5);
    micBox.addSectionHeading (tip::cond_head());
    micBox.addItem ("Shure BETA 87A", 6);    micBox.addItem ("Neumann U87", 7);
    micBox.addItem ("AKG C414", 8);          micBox.addItem ("AT AT2020", 9);
    micBox.addItem ("RODE NT1", 10);
    micBox.setTooltip (tip::micbox_tip());
    micBox.onChange = [this] { if (micBox.getSelectedId() > 0) applyMicPreset (micBox.getSelectedId()); };
    addAndMakeVisible (micBox);

    // scene row
    sceneSolo.setButtonText (tip::scene_solo());
    sceneTalk.setButtonText (tip::scene_talk());
    auto setupRadio = [this] (juce::TextButton& b, int group, bool on)
    {
        b.setClickingTogglesState (true);
        b.setRadioGroupId (group);
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonOnColourId, Palette::yellow);
        styleButton (b);
    };
    setupRadio (sceneSolo, 2002, false);
    setupRadio (sceneTalk, 2002, true);
    setupRadio (sceneBand, 2002, false);
    sceneSolo.onClick = [this] { currentScene = 0; applyScene(); };
    sceneTalk.onClick = [this] { currentScene = 1; applyScene(); };
    sceneBand.onClick = [this] { currentScene = 2; applyScene(); };

    // learn button (denoise profile capture)
    learnButton.setTooltip (tip::learn_tip());
    learnButton.setColour (juce::TextButton::buttonColourId, Palette::green.withAlpha (0.16f));
    learnButton.onClick = [this] { processor.requestDenoiseLearn(); };
    styleButton (learnButton);
    startTimerHz (8);   // refresh learn-button state

    styleButton (resetButton);
    resetButton.setColour (juce::TextButton::buttonColourId, Palette::salmon);
    resetButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    resetButton.setTooltip (juce::String::fromUTF8 ("\xe5\x85\xa8\xe3\x81\xa6\xe3\x81\xae\xe3\x83\x84\xe3\x83\x9e\xe3\x83\x9f\xe3\x82\x92\xe5\x88\x9d\xe6\x9c\x9f\xe5\x80\xa4\xe3\x81\xab\xe6\x88\xbb\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x99"));
    resetButton.onClick = [this]
    {
        for (auto* param : processor.getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    };

    styleButton (abA); styleButton (abB); styleButton (abCopy);
    styleButton (saveButton); styleButton (loadButton);
    abA.setClickingTogglesState (false);
    abB.setClickingTogglesState (false);
    abA.setColour (juce::TextButton::buttonOnColourId, Palette::yellow);
    abB.setColour (juce::TextButton::buttonOnColourId, Palette::yellow);
    abA.onClick    = [this] { switchSlot (0); };
    abB.onClick    = [this] { switchSlot (1); };
    abCopy.onClick = [this] { (currentSlot == 0 ? stateB : stateA) = processor.apvts.copyState(); };
    saveButton.onClick = [this] { savePreset(); };
    loadButton.onClick = [this] { loadPreset(); };

    zoomButton.setClickingTogglesState (true);
    zoomButton.setButtonText (tip::zoom_big());
    zoomButton.setTooltip (tip::zoom_tip());
    zoomButton.setColour (juce::TextButton::buttonOnColourId, Palette::yellow);
    zoomButton.onClick = [this]
    {
        const bool large = zoomButton.getToggleState();
        zoomButton.setButtonText (large ? tip::zoom_normal() : tip::zoom_big());
        if (onZoomToggle) onZoomToggle (large);
    };
    styleButton (zoomButton);

    addAndMakeVisible (tuner);

    stateA = processor.apvts.copyState();
    stateB = stateA.createCopy();
    updateABButtons();

    setSize (760, 924);
}

void VocalGzzioContent::styleButton (juce::TextButton& b)
{
    addAndMakeVisible (b);
}

juce::Font VocalGzzioContent::cfont (float h, bool bold) const
{
    return juce::Font (juce::FontOptions().withHeight (h * fontScale)
                                          .withStyle (bold ? "Bold" : "Regular"));
}

void VocalGzzioContent::setFontScale (float s)
{
    fontScale = juce::jmax (0.5f, s);
    sendLookAndFeelChange();
    resized();
    repaint();
}

void VocalGzzioContent::addKnob (Knob& k, const juce::String& paramID, const juce::String& text,
                                 const juce::String& tooltip, const juce::String& suffix)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 20);
    if (suffix.isNotEmpty())
        k.slider.setTextValueSuffix (" " + suffix);
    k.slider.setTooltip (tooltip);
    addAndMakeVisible (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.getProperties().set ("fontH", 12.0);
    k.label.getProperties().set ("bold", true);
    k.label.setColour (juce::Label::textColourId, Palette::ink);
    k.label.setTooltip (tooltip);
    addAndMakeVisible (k.label);

    k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
                   (processor.apvts, paramID, k.slider);
}

void VocalGzzioContent::timerCallback()
{
    const bool learning = processor.isDenoiseLearning();
    learnButton.setButtonText (learning ? juce::String ("LEARN...") : juce::String ("LEARN"));
    learnButton.setColour (juce::TextButton::buttonColourId,
                           learning ? Palette::yellow : Palette::green.withAlpha (0.16f));
}

// Voice presets own: comps, attack/release, presence, warmth, sustain
void VocalGzzioContent::applyVoicePreset (int id)
{
    auto setP = [this] (const juce::String& pid, float v)
    {
        if (auto* prm = processor.apvts.getParameter (pid))
            prm->setValueNotifyingHost (processor.apvts.getParameterRange (pid).convertTo0to1 (v));
    };

    //                    c1  c2  atk rel  pres  drive sus
    struct V { float c1, c2, atk, rel, pres, drv, sus; };
    static const V t[10] = {
        { 30, 30, 10, 140,  2.0f,  8, 25 },   // 1 ソプラノ/クリア
        { 30, 35, 12, 180,  0.5f, 18, 30 },   // 2 アルト/しっとり
        { 20, 45, 15, 220,  1.0f, 12, 40 },   // 3 ウィスパー/ASMR
        { 55, 40,  5, 100,  2.5f, 15, 15 },   // 4 パワフル/ベルト
        { 40, 35,  8, 120,  3.0f, 10, 20 },   // 5 ポップ/アイドル
        { 35, 35,  8, 130,  2.0f, 15, 22 },   // 6 テノール/ポップ
        { 35, 35, 10, 160,  1.0f, 22, 25 },   // 7 バリトン/シンガー
        { 30, 40, 12, 200,  1.5f, 28, 18 },   // 8 低音/ナレーター
        { 45, 35,  6, 110,  1.5f, 35, 20 },   // 9 ハスキー/ロック
        { 45, 30,  4,  90,  2.5f, 18,  8 },   // 10 ラップ/トーク
    };
    const auto& v = t[juce::jlimit (0, 9, id - 1)];
    setP ("comp1", v.c1);   setP ("comp2", v.c2);
    setP ("attack", v.atk); setP ("release", v.rel);
    setP ("presence", v.pres);
    setP ("drive", v.drv);  setP ("sustain", v.sus);
}

// Mic presets own: corrective EQ (lowcut, mud, harsh, air) + de-esser base.
// Values compensate each mic's widely known character (approximate).
void VocalGzzioContent::applyMicPreset (int id)
{
    auto setP = [this] (const juce::String& pid, float v)
    {
        if (auto* prm = processor.apvts.getParameter (pid))
            prm->setValueNotifyingHost (processor.apvts.getParameterRange (pid).convertTo0to1 (v));
    };

    //                     lc   mud   harsh  air   ds
    struct M { float lc, mud, harsh, air, ds; };
    static const M t[10] = {
        { 100, -3.0f, -1.0f, 3.5f, 30 },  // 1 SM58   (近接の膨らみ+高域控えめ→上を足す)
        {  90, -2.0f, -0.5f, 4.0f, 25 },  // 2 SM7B   (ダークで滑らか→上を持ち上げる)
        {  85, -1.5f, -1.0f, 2.0f, 28 },  // 3 RE20   (フラット寄り→軽い味付け)
        { 100, -2.5f, -1.5f, 1.5f, 32 },  // 4 e935   (プレゼンス強め→刺さり軽減)
        {  95, -2.0f, -2.5f, 2.0f, 30 },  // 5 MD421  (中域前面→中域の角を取る)
        {  90, -2.0f, -1.0f, 1.5f, 38 },  // 6 BETA87A(プレゼンス上昇内蔵→盛らずサ行ケア)
        {  95, -3.5f, -1.0f, 1.5f, 35 },  // 7 U87    (豊かな低中域→こもり整理)
        { 100, -2.0f, -2.0f, 0.5f, 40 },  // 8 C414   (明るくエアリー→上を盛らずDS強め)
        { 100, -2.5f, -2.5f, 1.0f, 40 },  // 9 AT2020 (高域やや硬い→角とDSケア)
        {  95, -2.0f, -1.5f, 1.0f, 38 },  // 10 NT1   (ニュートラル+軽いエア内蔵)
    };
    const auto& m = t[juce::jlimit (0, 9, id - 1)];
    setP ("lowcut", m.lc);  setP ("mud", m.mud);
    setP ("harsh", m.harsh); setP ("air", m.air);
    setP ("deess", m.ds);
}

// Scene owns: gate + space + output trim
void VocalGzzioContent::applyScene()
{
    auto setP = [this] (const juce::String& pid, float v)
    {
        if (auto* prm = processor.apvts.getParameter (pid))
            prm->setValueNotifyingHost (processor.apvts.getParameterRange (pid).convertTo0to1 (v));
    };

    switch (currentScene)
    {
        case 0: // 弾き語り
            setP ("gate", -66); setP ("makeup", 3);
            setP ("width", 20); setP ("doubler", 15); setP ("delay", 10);
            setP ("revsize", 38); setP ("revmix", 14);
            break;
        case 2: // Band
            setP ("gate", -56); setP ("makeup", 5);
            setP ("width", 5);  setP ("doubler", 0);  setP ("delay", 0);
            setP ("revsize", 20); setP ("revmix", 5);
            break;
        default: // トーク配信
            setP ("gate", -60); setP ("makeup", 4);
            setP ("width", 0);  setP ("doubler", 0);  setP ("delay", 0);
            setP ("revsize", 25); setP ("revmix", 6);
            break;
    }
}

void VocalGzzioContent::switchSlot (int target)
{
    if (target == currentSlot) return;
    (currentSlot == 0 ? stateA : stateB) = processor.apvts.copyState();
    const auto& t = (target == 0 ? stateA : stateB);
    if (t.isValid())
        processor.apvts.replaceState (t.createCopy());
    currentSlot = target;
    updateABButtons();
}

void VocalGzzioContent::updateABButtons()
{
    abA.setToggleState (currentSlot == 0, juce::dontSendNotification);
    abB.setToggleState (currentSlot == 1, juce::dontSendNotification);
}

void VocalGzzioContent::savePreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Save VocalGzzio preset",
                  juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                      .getChildFile ("VocalGzzio.xml"),
                  "*.xml");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                          | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f != juce::File())
                if (auto xml = processor.apvts.copyState().createXml())
                    xml->writeTo (f);
        });
}

void VocalGzzioContent::loadPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Load VocalGzzio preset",
                  juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                  "*.xml");
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.existsAsFile())
                if (auto xml = juce::XmlDocument::parse (f))
                    if (xml->hasTagName (processor.apvts.state.getType()))
                        processor.apvts.replaceState (juce::ValueTree::fromXml (*xml));
        });
}

//==============================================================================
void VocalGzzioContent::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (Palette::bgTop, 0, 0,
                                             Palette::bgBot, 0, (float) getHeight(), false));
    g.fillRect (getLocalBounds());

    if (mascot.isValid())
    {
        const float side = 118.0f;
        juce::Rectangle<float> dst (0, 0, side, side);
        dst.setCentre ((float) getWidth() * 0.5f, 66.0f);
        g.drawImage (mascot, dst, juce::RectanglePlacement::centred, false);
    }

    g.setColour (Palette::ink);
    g.setFont (cfont (27.0f, true));
    g.drawText (tip::title(), juce::Rectangle<int> (0, 124, getWidth(), 30), juce::Justification::centred);

    g.setColour (Palette::inkSoft);
    g.setFont (cfont (10.5f));
    g.drawText ("G'zzio  /  VOCAL CHANNEL STRIP", juce::Rectangle<int> (0, 152, getWidth(), 14),
                juce::Justification::centred);

    auto drawPanel = [&g, this] (juce::Rectangle<int> area, const juce::String& title)
    {
        auto r = area.toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (Palette::panelLn);
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.2f);
        g.setColour (Palette::inkSoft);
        g.setFont (cfont (11.5f, true));
        g.drawText (title, area.withTrimmedLeft (14).withHeight (22).translated (0, 4),
                    juce::Justification::centredLeft);
    };
    drawPanel (cleanArea, "1. CLEAN UP");
    drawPanel (dynArea,   "2. DYNAMICS");
    drawPanel (toneArea,  "3. TONE");
    drawPanel (spaceArea, "4. OUTPUT / SPACE");

    g.setColour (Palette::inkSoft.withAlpha (0.8f));
    g.setFont (cfont (10.5f));
    g.drawText (tip::subtitle_hint(), juce::Rectangle<int> (0, getHeight() - 20, getWidth(), 16),
                juce::Justification::centred);
}

void VocalGzzioContent::placeRow (juce::Rectangle<int> area, std::initializer_list<Knob*> ks)
{
    area.removeFromTop (26);
    area.reduce (10, 4);
    const int n  = (int) ks.size();
    const int cw = area.getWidth() / n;
    int i = 0;
    for (auto* k : ks)
    {
        juce::Rectangle<int> cell (area.getX() + i * cw, area.getY(), cw, area.getHeight());
        k->label.setBounds (cell.removeFromTop (20));
        k->slider.setBounds (cell.reduced (2));
        ++i;
    }
}

void VocalGzzioContent::resized()
{
    const int pad = 12;
    const int w   = getWidth() - 2 * pad;
    const int bh  = 24;

    // preset combos row: voice type | mic model
    int y = 170;
    const int half = (w - 6) / 2;
    voiceBox.setBounds (pad,            y, half, bh);
    micBox  .setBounds (pad + half + 6, y, half, bh);

    // scene row
    y = 198;
    const int vw = (w - 2 * 6) / 3;
    sceneSolo.setBounds (pad,                y, vw, bh);
    sceneTalk.setBounds (pad + vw + 6,       y, vw, bh);
    sceneBand.setBounds (pad + 2 * (vw + 6), y, vw, bh);

    // control row
    y = 226;
    resetButton.setBounds (getWidth() - pad - 70, y, 70, bh);
    int bx = pad;
    auto place = [&] (juce::TextButton& b, int bw) { b.setBounds (bx, y, bw, bh); bx += bw + 6; };
    place (abA, 40); place (abB, 40); place (abCopy, 50); place (saveButton, 58); place (loadButton, 58);
    place (zoomButton, 104);

    // tuner + meters (taller: 5 bars)
    tuner.setBounds (pad, 256, w, 114);

    // sections: 5 / 5 / 4 / 7 knobs
    const int top = 378, gap = 6;
    const int secH1 = 124, secH2 = 124, secH3 = 124, secH4 = 130;
    cleanArea = { pad, top,                                        w, secH1 };
    dynArea   = { pad, top +  secH1 + gap,                         w, secH2 };
    toneArea  = { pad, top +  secH1 + secH2 + 2 * gap,             w, secH3 };
    spaceArea = { pad, top +  secH1 + secH2 + secH3 + 3 * gap,     w, secH4 };

    placeRow (cleanArea, { &gate, &lowCut, &mudK, &harshK, &denoiseK });
    placeRow (dynArea,   { &comp1K, &comp2K, &attackK, &releaseK, &deessK });
    placeRow (toneArea,  { &presenceK, &airK, &warmthK, &sustainK });
    placeRow (spaceArea, { &makeupK, &mixK, &widthK, &doublerK, &delayK, &revSizeK, &revMixK });

    // LEARN button: top-right inside the CLEAN UP panel header
    learnButton.setBounds (cleanArea.getRight() - 88, cleanArea.getY() + 3, 80, 20);
}

//==============================================================================
// Thin editor: LookAndFeel owner + font-only zoom
//==============================================================================
VocalGzzioEditor::VocalGzzioEditor (VocalGzzioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), content (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.onZoomToggle = [this] (bool large)
    {
        const float s = large ? 1.3f : 1.0f;
        lnf.setFontScale (s);
        content.setFontScale (s);
    };
    setSize (baseW, baseH);
}

VocalGzzioEditor::~VocalGzzioEditor()
{
    setLookAndFeel (nullptr);
}

void VocalGzzioEditor::resized()
{
    content.setBounds (getLocalBounds());
}
