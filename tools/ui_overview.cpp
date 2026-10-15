#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetDefs.h"
#include <cstdio>

static bool containsJapanese (const juce::String& text)
{
    for (auto c : text)
        if ((c >= 0x3040 && c <= 0x30ff) || (c >= 0x3400 && c <= 0x9fff)
            || (c >= 0xff66 && c <= 0xff9d)) return true;
    return false;
}

static int checkBrandLanguage (juce::Component& root)
{
    int failures = 0;
    auto check = [&] (const juce::String& text, const char* kind)
    {
        if (containsJapanese (text))
        {
            ++failures;
            std::printf ("FAIL: brand %s: %s\n", kind, text.toRawUTF8());
        }
    };
    for (auto* c : root.getChildren())
    {
        if (! c->isVisible()) continue;
        if (auto* b = dynamic_cast<juce::Button*> (c)) check (b->getButtonText(), "button");
        if (auto* l = dynamic_cast<juce::Label*> (c)) check (l->getText(), "label");
        if (auto* box = dynamic_cast<juce::ComboBox*> (c))
            for (int i = 0; i < box->getNumItems(); ++i) check (box->getItemText (i), "choice");
        if (auto* help = dynamic_cast<juce::TooltipClient*> (c)) check (help->getTooltip(), "help");
        check (c->getTitle(), "accessible title");
        failures += checkBrandLanguage (*c);
    }
    return failures;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto processor = std::make_unique<VocalGzzioProcessor>();
    processor->setPlayConfigDetails (2, 2, 48000.0, 256);
    processor->prepareToPlay (48000.0, 256);
    GzzioLnF look;
    VocalGzzioContent view (*processor);
    view.setLookAndFeel (&look);
    view.setSize (VocalGzzioEditor::baseW, VocalGzzioEditor::baseH);
    int failures = 0;
    auto check = [&] (bool ok, const char* reason)
    { if (! ok) { ++failures; std::printf ("FAIL: %s\n", reason); } };
    auto find = [&] (const char* id) { return view.findChildWithID (id); };
    check (view.isOverview(), "overview is the default");
    auto setParameter = [&] (const char* id, float value)
    {
        auto* p = processor->apvts.getParameter (id);
        p->setValueNotifyingHost (processor->apvts.getParameterRange (id).convertTo0to1 (value));
        view.setOverview (true);
    };
    auto* noiseStatus = dynamic_cast<juce::Label*> (find ("noiseStatus"));
    check (noiseStatus && noiseStatus->isVisible(), "noise processing state remains visible");
    setParameter ("denoise", 0.0f);
    check (noiseStatus && noiseStatus->getText().contains ("0%"), "zero amount is explicitly displayed");
    auto* learnButton = dynamic_cast<juce::TextButton*> (find ("learnNoise"));
    if (learnButton) learnButton->onClick();
    check (processor->apvts.getRawParameterValue ("denoise")->load() == 0.0f, "learning does not change the amount");
    view.setOverview (true);
    check (noiseStatus && noiseStatus->getText().contains (juce::String::fromUTF8 ("測定中")), "learning progress remains visible");
    processor->clearDenoiseLearn();
    setParameter ("denoise", 35.0f);
    setParameter ("mod_souji", 0.0f);
    view.setThemeMode (3);
    check (noiseStatus && noiseStatus->getText().contains ("OFF"), "bypassed noise section is explicitly displayed");
    setParameter ("mod_souji", 1.0f);
    setParameter ("dn_on", 0.0f);
    check (noiseStatus && noiseStatus->getText().contains ("OFF"), "bypassed denoise is explicitly displayed");
    setParameter ("dn_on", 1.0f);
    check (noiseStatus && noiseStatus->getText().contains ("35%"), "active amount is displayed");
    setParameter ("denoise", 0.0f);
    // The learned-profile reset used to reappear at its old detail-view coordinates
    // after the timer ran. Exercise a real measurement before checking its hit area.
    processor->requestDenoiseLearn();
    juce::AudioBuffer<float> noise (2, 256);
    juce::MidiBuffer midi;
    unsigned seed = 4173;
    for (int block = 0; block < 375; ++block)
    {
        for (int i = 0; i < noise.getNumSamples(); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const float value = ((float) (seed >> 8) / 8388608.0f - 1.0f) * 0.014f;
            noise.setSample (0, i, value); noise.setSample (1, i, value);
        }
        processor->processBlock (noise, midi);
    }
    check (processor->isDenoiseLearned(), "steady noise was measured for the UI state check");
    view.setOverview (true);
    auto* clearNoise = dynamic_cast<juce::TextButton*> (find ("clearNoise"));
    check (clearNoise && clearNoise->isVisible(), "learned noise can be cleared from the overview");
    check (clearNoise && noiseStatus && clearNoise->getY() == noiseStatus->getY()
           && ! clearNoise->getBounds().intersects (noiseStatus->getBounds()), "learned reset has a dedicated position beside its state");
    std::vector<float> soundSettings;
    for (auto* parameter : processor->getParameters()) soundSettings.push_back (parameter->getValue());
    for (int theme : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 })
        for (float size : { 1.5f, 1.875f, 2.25f })
        {
            look.setFontScale (size); view.setFontScale (size); view.setThemeMode (theme);
            view.setOverview (true);
            std::vector<juce::Component*> controls;
            for (auto* c : view.getChildren())
            {
                if (! c->isVisible()) continue;
                check (! c->getBounds().isEmpty(), "visible control has a hit area");
                check (view.getLocalBounds().contains (c->getBounds()), "visible control stays inside window");
                if (dynamic_cast<juce::ComboBox*> (c) || dynamic_cast<juce::TextButton*> (c)) controls.push_back (c);
            }
            for (size_t i = 0; i < controls.size(); ++i)
                for (size_t j = i + 1; j < controls.size(); ++j)
                    check (! controls[i]->getBounds().intersects (controls[j]->getBounds()), "buttons and selectors do not overlap");
            auto* power = dynamic_cast<juce::TextButton*> (find ("reverbPower"));
            check (power != nullptr && power->isVisible(), "reverb ON/OFF always reachable");
            auto* preset = dynamic_cast<juce::ComboBox*> (find ("finishPreset"));
            check (preset != nullptr && preset->getNumItems() == gzzio::kNumFinishPresets, "complete finish presets reachable");
            for (const char* directId : { "autoSetup", "songSetup", "resetAll", "abSlotA", "abSlotB", "abCopy",
                                          "crushGuard", "smartEqPower", "pitchFixPower", "unisonPower", "voiceFxPower",
                                          "reverbType", "tempoFit", "sessionMode", "openCheckup", "trimOutputBoost" })
            {
                auto* direct = find (directId);
                check (direct != nullptr && direct->isVisible(), "direct overview control reachable without leaving overview");
            }
            if (theme == 3)
            {
                check (power->getButtonText().contains ("Reverb"), "brand mode uses English");
                auto* source = dynamic_cast<juce::ComboBox*> (find ("sourceMode"));
                auto* learn = dynamic_cast<juce::TextButton*> (find ("learnNoise"));
                auto* autoLearn = dynamic_cast<juce::TextButton*> (find ("autoLearnNoise"));
                check (source && source->getItemText (0) == "Vocal", "source modes use English");
                check (learn && learn->getButtonText() == "Learn noise", "noise learning uses English");
                check (autoLearn && autoLearn->getButtonText() == "Auto learn", "automatic learning uses English");
                failures += checkBrandLanguage (view);
            }
            auto* details = dynamic_cast<juce::TextButton*> (find ("openDetails"));
            details->onClick();
            check (! view.isOverview(), "all controls can be opened");
            if (theme == 3)
            {
                failures += checkBrandLanguage (view);
                view.restoreUiState (true, 1, false, theme);
                failures += checkBrandLanguage (view);
                view.restoreUiState (true, 0, false, theme);
            }
            auto* back = dynamic_cast<juce::TextButton*> (find ("openOverview"));
            check (back && back->isVisible(), "overview return button reachable");
            if (back) back->onClick();
            check (view.isOverview(), "return restores overview");
        }
    for (int i = 0; i < processor->getParameters().size(); ++i)
        check (processor->getParameters()[i]->getValue() == soundSettings[(size_t) i], "language and view changes preserve sound settings");
    view.setThemeMode (0);
    check (learnButton && learnButton->getButtonText() == juce::String::fromUTF8 ("ノイズを測る"), "normal mode restores Japanese");
    if (clearNoise) clearNoise->onClick();
    view.setOverview (true);
    check (! processor->isDenoiseLearned() && clearNoise && ! clearNoise->isVisible(), "clearing the profile removes the learned reset");
    check (processor->apvts.getRawParameterValue ("denoise")->load() == 0.0f, "clearing the profile preserves the amount");
    if (auto* finish = dynamic_cast<juce::ComboBox*> (find ("finishPreset")))
    {
        finish->setSelectedId (1, juce::sendNotificationSync);
        check (finish->getSelectedId() == 1, "finish preset can be selected");
        if (auto* resetBtn = dynamic_cast<juce::TextButton*> (find ("resetAll")))
            resetBtn->onClick();
        check (finish->getSelectedId() == 0, "reset button clears finish preset selection");
    }
    // Two editor instances can use different languages in the same DAW.
    // Creating or changing the second editor must not relabel the first one,
    // including labels regenerated by an action after the theme change.
    {
        view.setThemeMode (3);
        auto secondProcessor = std::make_unique<VocalGzzioProcessor>();
        VocalGzzioContent second (*secondProcessor);
        GzzioLnF secondLook;
        second.setLookAndFeel (&secondLook);
        second.setSize (VocalGzzioEditor::baseW, VocalGzzioEditor::baseH);
        second.setThemeMode (0);
        view.setOverview (true);
        failures += checkBrandLanguage (view);
        auto* otherLearn = dynamic_cast<juce::TextButton*> (second.findChildWithID ("learnNoise"));
        check (otherLearn && otherLearn->getButtonText() == juce::String::fromUTF8 ("ノイズを測る"),
               "new Japanese editor does not inherit another editor's language");
        second.setThemeMode (3);
        view.setThemeMode (0);
        second.setOverview (true);
        failures += checkBrandLanguage (second);
        view.setOverview (true);
        check (learnButton && learnButton->getButtonText() == juce::String::fromUTF8 ("ノイズを測る"),
               "changing the other editor preserves Japanese action labels");
        second.setLookAndFeel (nullptr);
    }
    // Verify all 4 Gemini4Argon workspace architectures (0: ARGON COCKPIT, 1: V-LIVER STAGE, 2: STUDIO RACK, 3: CLASSIC)
    for (int arch : { 0, 1, 2, 3 })
    {
        view.setThemeMode (0);
        view.setOverview (true);
        view.setOverviewLayoutMode (arch);
        check (view.getOverviewLayoutMode() == arch, "workspace architecture mode switches cleanly");
        std::vector<juce::Component*> controls;
        for (auto* c : view.getChildren())
        {
            if (! c->isVisible()) continue;
            check (! c->getBounds().isEmpty(), "architecture control has non-empty bounds");
            check (view.getLocalBounds().contains (c->getBounds()), "architecture control stays inside window");
            if (dynamic_cast<juce::ComboBox*> (c) || dynamic_cast<juce::TextButton*> (c)) controls.push_back (c);
        }
        for (size_t i = 0; i < controls.size(); ++i)
            for (size_t j = i + 1; j < controls.size(); ++j)
                check (! controls[i]->getBounds().intersects (controls[j]->getBounds()), "architecture controls do not overlap");
    }
    // Verify Argon Velvet Engine DSP: mono-compatibility of Binaural Halo and Voiced vs Sibilant harmonic gating
    {
        gz::argon::VelvetEngine engine;
        engine.prepare (48000.0);
        constexpr int samples = 48000;
        std::vector<float> left (samples, 0.0f), right (samples, 0.0f);
        for (int i = 0; i < samples; ++i)
        {
            const float v = 0.95f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) i / 48000.0f);
            left[(size_t) i] = v;
            right[(size_t) i] = v;
        }
        engine.setParams (0.0f, 0.0f, 0.85f, 0.0f, true);
        engine.process (left.data(), right.data(), samples);
        float maxMonoDiff = 0.0f, maxSide = 0.0f;
        for (int i = 0; i < samples; ++i)
        {
            const float expectedMono = 1.9f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) i / 48000.0f);
            maxMonoDiff = std::max (maxMonoDiff, std::abs ((left[(size_t) i] + right[(size_t) i]) - expectedMono));
            maxSide = std::max (maxSide, std::abs (left[(size_t) i] - right[(size_t) i]));
        }
        check (maxSide > 0.001f, "Halo delay has actually produced audible stereo width before testing mono sum");
        check (maxMonoDiff < 1.0e-5f, "Argon Binaural Halo is 100% mono-compatible (zero mono comb filtering)");
    }
    // Verify Lightweight (Eco) Mode toggles cleanly and preserves all layout invariants
    {
        view.setLiteMode (true);
        check (view.isLiteMode(), "Lite mode enables cleanly");
        view.setLiteMode (false);
        check (! view.isLiteMode(), "Lite mode disables cleanly and restores full craftsmanship");
    }
    // Verify interactive mouse clicks/drags/double-clicks on Argon pills, targets, 2D Orb, and bars + full paint pass
    {
        view.setOverview (true);
        for (int arch = 0; arch < 4; ++arch)
        {
            const bool hit = view.handleOverviewMouseDown ({ 236 + arch * 68 + 20, 30 });
            check (hit && view.getOverviewLayoutMode() == arch, "clicking architecture pill switches layout mode");
            for (bool lite : { false, true })
            {
                view.setLiteMode (lite);
                juce::Image canvas (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
                juce::Graphics g (canvas);
                view.paintEntireComponent (g, true);
            }
        }
        view.setLiteMode (false);
        // Click Lite pill at {510 + 20, 30}
        check (view.handleOverviewMouseDown ({ 530, 30 }) && view.isLiteMode(), "clicking Lite pill enables Lite Mode");
        check (view.handleOverviewMouseDown ({ 530, 30 }) && ! view.isLiteMode(), "clicking Lite pill again disables Lite Mode");

        // Switch to Argon Cockpit (mode 0) and test 4 Voice DNA target pills, 2D Orb drag/reset, 4 direct bars, and Smart Level-Match badge
        view.setOverviewLayoutMode (0);
        {
            juce::Image canvas (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
            juce::Graphics g (canvas);
            view.paintEntireComponent (g, true);
        }
        for (int t = 0; t < 4; ++t)
        {
            setParameter ("denoise", 0.0f);
            setParameter ("dn_relearn", 0.0f);
            setParameter ("hum_amt", 15.0f);
            view.applyArgonVoiceTarget (t);
            check (processor->apvts.getRawParameterValue ("argon_velvet")->load() > 10.0f, "Argon Voice Target sets velvet");
            check (processor->apvts.getRawParameterValue ("argon_focus")->load()  > 10.0f, "Argon Voice Target sets focus");
            check (processor->apvts.getRawParameterValue ("argon_silk")->load()   > 10.0f, "Argon Voice Target sets silk");
            check (processor->apvts.getRawParameterValue ("argon_halo")->load()   > 10.0f, "Argon Voice Target sets halo");
            check (processor->apvts.getRawParameterValue ("denoise")->load() == 0.0f
                && processor->apvts.getRawParameterValue ("dn_relearn")->load() == 0.0f
                && processor->apvts.getRawParameterValue ("hum_amt")->load() == 15.0f,
                   "voice targets preserve noise reduction, learning and hum settings");
        }
        setParameter ("presence", -2.0f);
        setParameter ("drive", 12.0f);
        setParameter ("revmix", 17.0f);
        // Drag 2D Orb inside argonPadRect ({centerX + 16, top + 44, 316, 236} -> center around {650, 280})
        check (view.handleOverviewMouseDown ({ 550, 200 }), "2D Gravity Orb mouseDown hits pad");
        view.handleOverviewMouseDrag ({ 780, 340 });
        check (processor->apvts.getRawParameterValue ("argon_focus")->load() > 10.0f, "2D Orb drag updates 2.8kHz Focus");
        check (processor->apvts.getRawParameterValue ("argon_silk")->load()  > 50.0f, "2D Orb drag right increases Silk Air");
        check (processor->apvts.getRawParameterValue ("argon_halo")->load()  > 50.0f, "2D Orb drag down increases 3D Halo");
        check (view.handleOverviewMouseDoubleClick ({ 650, 280 }), "2D Orb double-click hits pad");
        check (processor->apvts.getRawParameterValue ("argon_velvet")->load() == 0.0f
            && processor->apvts.getRawParameterValue ("argon_focus")->load()  == 0.0f
            && processor->apvts.getRawParameterValue ("argon_silk")->load()   == 0.0f
            && processor->apvts.getRawParameterValue ("argon_halo")->load()   == 0.0f,
               "2D Orb double-click resets Velvet, Focus, Silk, and Halo to 0%");
        check (processor->apvts.getRawParameterValue ("presence")->load() == -2.0f
            && processor->apvts.getRawParameterValue ("drive")->load() == 12.0f
            && processor->apvts.getRawParameterValue ("revmix")->load() == 17.0f,
               "orb drag and reset preserve separately controlled tone and reverb");
        check (view.overviewHelpAt ({ 650, 280 }).isNotEmpty(), "orb has hover help");
        for (int i = 0; i < 4; ++i)
            check (view.overviewHelpAt ({ 920, 288 + i * 32 }).isNotEmpty(), "each Argon bar has hover help");
        view.setThemeMode (3);
        check (! containsJapanese (view.overviewHelpAt ({ 650, 280 })), "brand orb hover help uses English");
        view.setThemeMode (0);

        // Click One-Click Smart Level-Match badge in Telemetry (rightX = 1102, overviewOutput right = 1558, badge around {1490, 142})
        check (view.handleOverviewMouseDown ({ 1490, 142 }), "One-Click Smart Level-Match badge click hits levelBadgeRect");
    }
    // Output trim must use an actual signal and must never boost silence or quiet input.
    {
        auto trimProcessorOwner = std::make_unique<VocalGzzioProcessor>();
        auto& trimProcessor = *trimProcessorOwner;
        trimProcessor.setPlayConfigDetails (2, 2, 48000.0, 256);
        trimProcessor.prepareToPlay (48000.0, 256);
        auto setRaw = [&] (const char* id, float value)
        {
            auto* p = trimProcessor.apvts.getParameter (id);
            p->setValueNotifyingHost (trimProcessor.apvts.getParameterRange (id).convertTo0to1 (value));
        };
        for (const char* id : { "mod_souji", "mod_henshin", "mod_totonoe", "mod_soroe",
                                "mod_sagyo", "mod_neiro", "mod_hirogari" })
            if (trimProcessor.apvts.getParameter (id)) setRaw (id, 0.0f);
        auto trimViewOwner = std::make_unique<VocalGzzioContent> (trimProcessor);
        auto& trimView = *trimViewOwner;
        trimView.setSize (VocalGzzioEditor::baseW, VocalGzzioEditor::baseH);
        trimView.setOverview (true);
        auto* trim = dynamic_cast<juce::TextButton*> (trimView.findChildWithID ("trimOutputBoost"));
        check (trim && trim->isVisible() && trim->getTooltip().isNotEmpty(), "output trim has a reachable button and explanatory help");
        auto runSignal = [&] (float amplitude)
        {
            juce::AudioBuffer<float> signal (2, 256);
            juce::MidiBuffer events;
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float value = amplitude * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (b * 256 + i) / 48000.0f);
                    signal.setSample (0, i, value);
                    signal.setSample (1, i, value);
                }
                trimProcessor.processBlock (signal, events);
            }
        };
        auto gain = [&] { return trimProcessor.apvts.getRawParameterValue ("makeup")->load(); };
        setRaw ("makeup", 12.0f);
        runSignal (0.0f);
        if (trim) trim->onClick();
        check (gain() == 12.0f, "output trim leaves gain unchanged during silence");
        runSignal (0.003f);
        if (trim) trim->onClick();
        check (gain() == 12.0f, "output trim never raises quiet audio");
        setRaw ("makeup", 18.0f);
        runSignal (0.6f);
        check (trimProcessor.getOutputLevel() > juce::Decibels::decibelsToGain (-14.0f), "trim test has a measured loud output");
        const float oldInputGain = trimProcessor.apvts.getRawParameterValue ("in_gain")->load();
        if (trim) trim->onClick();
        check (gain() < 18.0f && gain() >= 0.0f, "loud output reduces only available output boost");
        check (trimProcessor.apvts.getRawParameterValue ("in_gain")->load() == oldInputGain, "output trim does not silently alter input gain");
        setRaw ("makeup", 0.0f);
        runSignal (0.6f);
        if (trim) trim->onClick();
        check (gain() == 0.0f, "output trim respects the compatible zero-dB minimum");
    }
    // Verify full VocalGzzioProcessor::processBlock with 4-Axis Argon Engine across sample rates, NaN/Inf safety, bypass, voiced vs sibilant gating, and state save/restore
    {
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            VocalGzzioProcessor proc;
            // A processor restores the last closed instance's settings. The trim
            // test above intentionally saved the tone module OFF; restore a known
            // signal chain before testing the Argon detector, for every sample rate.
            for (auto* parameter : proc.getParameters())
                parameter->setValueNotifyingHost (parameter->getDefaultValue());
            proc.clearDenoiseLearn();
            proc.setPlayConfigDetails (2, 2, sr, 256);
            proc.prepareToPlay (sr, 256);
            auto setRaw = [&] (const char* id, float plainVal)
            {
                if (auto* p = proc.apvts.getParameter (id))
                    p->setValueNotifyingHost (proc.apvts.getParameterRange (id).convertTo0to1 (plainVal));
            };
            setRaw ("argon_velvet", 85.0f);
            setRaw ("argon_focus",  80.0f);
            setRaw ("argon_silk",   80.0f);
            setRaw ("argon_halo",   75.0f);

            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            bool allFinite = true;
            // 1) Voiced 220 Hz + 2800 Hz Singer's Formant vowel signal
            for (int block = 0; block < 20; ++block)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float t = (float) (block * 256 + i) / (float) sr;
                    const float v = 0.22f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * t)
                                  + 0.10f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * t)
                                  + 0.06f * std::sin (2.0f * juce::MathConstants<float>::pi * 2800.0f * t);
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                proc.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 256; ++i)
                        if (! std::isfinite (buf.getSample (ch, i))) allFinite = false;
            }
            const float voicedVelvetMeter = proc.getArgonVelvetMeter();
            const float voicedFocusMeter  = proc.getArgonFocusMeter();
            check (allFinite, "Argon processBlock output is 100% finite (no NaN/Inf)");
            check (voicedVelvetMeter > 0.05f, "Voiced vowel signal activates Argon Velvet harmonics");
            check (voicedFocusMeter  > 0.05f, "Singer's Formant signal activates Argon Focus");

            // 1b) All Argon amounts at zero must preserve hot floating-point audio exactly.
            {
                gz::argon::VelvetEngine bypassEng;
                bypassEng.prepare (sr);
                bypassEng.setParams (0.0f, 0.0f, 0.0f, 0.0f, true, 0.0f);
                std::vector<float> hotL (256, 1.5f), hotR (256, -1.5f);
                bypassEng.process (hotL.data(), hotR.data(), 256);
                bool exact = true;
                for (int i = 0; i < 256; ++i)
                    exact = exact && hotL[(size_t) i] == 1.5f && hotR[(size_t) i] == -1.5f;
                check (exact, "all-zero Argon preserves hot floating-point samples without clipping");
            }

            // 2) Sibilant 7500 Hz burst -> velvet meter should drop sharply
            for (int block = 0; block < 20; ++block)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float t = (float) (block * 256 + i) / (float) sr;
                    const float s = 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * 7500.0f * t);
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                proc.processBlock (buf, midi);
            }
            const float sibilantVelvetMeter = proc.getArgonVelvetMeter();
            std::printf ("Argon %.0f Hz: vowel velvet=%.6f, focus=%.6f, sibilant velvet=%.6f\n",
                         sr, voicedVelvetMeter, voicedFocusMeter, sibilantVelvetMeter);
            check (sibilantVelvetMeter < voicedVelvetMeter * 0.25f, "Sibilant burst automatically gates off Argon Velvet distortion");

            // 3) State save & restore preserves all 4 Argon parameters
            juce::MemoryBlock stateData;
            proc.getStateInformation (stateData);
            VocalGzzioProcessor restored;
            restored.setPlayConfigDetails (2, 2, sr, 256);
            restored.prepareToPlay (sr, 256);
            restored.setStateInformation (stateData.getData(), (int) stateData.getSize());
            check (std::abs (restored.apvts.getRawParameterValue ("argon_velvet")->load() - 85.0f) < 0.5f
                && std::abs (restored.apvts.getRawParameterValue ("argon_focus")->load()  - 80.0f) < 0.5f
                && std::abs (restored.apvts.getRawParameterValue ("argon_silk")->load()   - 80.0f) < 0.5f
                && std::abs (restored.apvts.getRawParameterValue ("argon_halo")->load()   - 75.0f) < 0.5f,
                   "DAW state save/restore preserves Argon Velvet, Focus, Silk, and Halo parameters");

            // The whole tone module can be bypassed from the overview. Its meters
            // must settle to zero after the existing 10 ms module fade completes.
            setRaw ("mod_neiro", 0.0f);
            for (int block = 0; block < 20; ++block)
            {
                buf.clear();
                proc.processBlock (buf, midi);
            }
            check (proc.getArgonVelvetMeter() == 0.0f && proc.getArgonFocusMeter() == 0.0f
                && proc.getArgonSilkMeter() == 0.0f && proc.getArgonHaloMeter() == 0.0f
                && proc.getArgonVoicedRatio() == 0.0f,
                   "bypassing the tone module clears all Argon activity meters");
            proc.releaseResources();
            restored.releaseResources();
        }
    }
    view.setLookAndFeel (nullptr);
    processor->releaseResources();
    std::printf ("Overview failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
