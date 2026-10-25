#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetDefs.h"
#include <juce_audio_formats/juce_audio_formats.h>
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
        if (p == nullptr) return;
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
    for (int theme : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18 })
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

        // Verify Tablet PC 1-Tap [↺ 0%] Reset pill (argonResetRect at top-right of argonPadRect: {797, 177})
        check (view.handleOverviewMouseDown ({ 550, 200 }), "2D Gravity Orb mouseDown hits pad before 1-tap reset");
        view.handleOverviewMouseDrag ({ 780, 340 });
        check (processor->apvts.getRawParameterValue ("argon_silk")->load() > 50.0f, "2D Orb drag raises Silk Air before 1-tap reset");
        check (view.overviewHelpAt ({ 797, 177 }).isNotEmpty(), "1-Tap Orb Reset pill has hover/touch help");
        check (view.handleOverviewMouseDown ({ 797, 177 }), "1-Tap Orb Reset pill single tap is handled");
        check (processor->apvts.getRawParameterValue ("argon_velvet")->load() == 0.0f
            && processor->apvts.getRawParameterValue ("argon_focus")->load()  == 0.0f
            && processor->apvts.getRawParameterValue ("argon_silk")->load()   == 0.0f
            && processor->apvts.getRawParameterValue ("argon_halo")->load()   == 0.0f,
               "1-Tap Orb Reset pill resets Velvet, Focus, Silk, and Halo to 0% without double-clicking");
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
    // Verify v4.6.0: Header pills (Center/Mono routing & SYNCROOM external output), Input Centering, Output Mono, and Virtual Device detection
    {
        view.setOverview (true);
        check (view.overviewHelpAt ({ 590, 30 }).isNotEmpty(), "route pill has hover help");
        check (view.overviewHelpAt ({ 670, 30 }).isNotEmpty(), "stream pill has hover help");
        const int prevRoute = processor->getInputRouteMode();
        check (view.handleOverviewMouseDown ({ 590, 30 }), "clicking route pill is handled");
        check (processor->getInputRouteMode() == (prevRoute + 1) % 5, "headless click on route pill cycles inputRouteMode");
        processor->setInputRouteMode (0);
        check (view.handleOverviewMouseDown ({ 670, 30 }), "clicking stream pill is handled");

        // Verify virtual audio driver recognition (Yamaha SYNCROOM Driver, VB-CABLE, VoiceMeeter)
        check (gz::StreamOut::isVirtualLoopbackName (juce::String::fromUTF8 ("ライン (Yamaha SYNCROOM Driver (WDM))")),
               "Yamaha SYNCROOM Driver (WDM) is recognized as a preferred virtual audio device");
        check (gz::StreamOut::isVirtualLoopbackName ("CABLE Input (VB-Audio Virtual Cable)"),
               "VB-CABLE is recognized as a preferred virtual audio device");

       #if JUCE_WINDOWS
        {
            juce::AudioDeviceManager adm;
            bool hasAsio = false;
            for (auto* t : adm.getAvailableDeviceTypes())
                if (t->getTypeName().containsIgnoreCase ("ASIO")) hasAsio = true;
            check (hasAsio, "Windows Standalone AudioDeviceManager includes ASIO device type");
        }
       #endif

        // Verify Input 1 -> Center (L+R) and VST3 Mono Optimization Mode (cluster / VRChat) + 4-channel mirroring
        auto routeProc = std::make_unique<VocalGzzioProcessor>();
        routeProc->setPlayConfigDetails (2, 2, 48000.0, 256);
        routeProc->prepareToPlay (48000.0, 256);
        routeProc->setInputRouteMode (0); // Auto route in VST3 mode
        routeProc->setOutputMonoMode (true); // Mono Optimization Mode ON
        if (auto* p = routeProc->apvts.getParameter ("doubler"))
            p->setValueNotifyingHost (routeProc->apvts.getParameterRange ("doubler").convertTo0to1 (80.0f));
        if (auto* p = routeProc->apvts.getParameter ("width"))
            p->setValueNotifyingHost (routeProc->apvts.getParameterRange ("width").convertTo0to1 (100.0f));
        if (auto* p = routeProc->apvts.getParameter ("argon_halo"))
            p->setValueNotifyingHost (routeProc->apvts.getParameterRange ("argon_halo").convertTo0to1 (85.0f));
        juce::AudioBuffer<float> buf4 (4, 256);
        juce::MidiBuffer routeMidi;
        for (int b = 0; b < 10; ++b)
        {
            buf4.clear();
            for (int i = 0; i < 256; ++i)
                buf4.setSample (0, i, 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) (b * 256 + i) / 48000.0f));
            routeProc->processBlock (buf4, routeMidi);
        }
        float rms0 = buf4.getRMSLevel (0, 0, 256);
        float rms1 = buf4.getRMSLevel (1, 0, 256);
        float rms2 = buf4.getRMSLevel (2, 0, 256);
        float rms3 = buf4.getRMSLevel (3, 0, 256);
        check (rms0 > 0.05f && std::abs (rms0 - rms1) < 1.0e-5f,
               "VST3 Mono Optimization Mode auto-centers single-sided mic and outputs identical L/R with Doubler/Width/Halo active");
        check (std::abs (rms0 - rms2) < 1.0e-5f && std::abs (rms1 - rms3) < 1.0e-5f,
               "Centered output is mirrored to extra multi-channel outputs (Out 3/4)");
        routeProc->releaseResources();
    }
    // Verify IR Reverb Acoustic Decay Curve Visualizer, Custom IR WAV Loader, and 1st-Order ADAA saturation
    {
        view.setOverview (true);
        view.setOverviewLayoutMode (0);
        // In Argon Cockpit (mode 0), overviewCards[2] is at rightX=1102, y=360, spaceRow2 is at y=360+12+88=460, right side around x=1450..1530
        const juce::Point<int> irPt (1480, 475);
        check (view.overviewHelpAt (irPt).isNotEmpty(), "IR Reverb capsule in 03 SPACE has hover help");
        setParameter ("rev_type", 0.0f);
        check (view.handleOverviewMouseDown (irPt), "clicking IR Reverb capsule in 03 SPACE is handled");
        check ((int) processor->apvts.getRawParameterValue ("rev_type")->load() >= kHeyaFirst,
               "clicking IR Reverb capsule switches reverb type to Heya IR room");

        float curve[24] {};
        processor->getReverbIrCurve (curve, 24);
        check (curve[0] > 0.1f && curve[0] >= curve[23], "getReverbIrCurve returns a valid acoustic decay envelope");

        // Synthesize a temporary stereo IR WAV file and verify loadCustomIrFile + audio convolution + state persistence
        auto tmpIrFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("vocalgzzio_test_custom_ir.wav");
        tmpIrFile.deleteFile();
        {
            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::FileOutputStream> outStream (tmpIrFile.createOutputStream());
            if (outStream != nullptr)
            {
                std::unique_ptr<juce::AudioFormatWriter> writer (
                    wavFormat.createWriterFor (outStream.get(), 48000.0, 2, 16, {}, 0));
                if (writer != nullptr)
                {
                    outStream.release();
                    juce::AudioBuffer<float> irBuf (2, 12000);
                    unsigned rng = 9001u;
                    for (int i = 0; i < 12000; ++i)
                    {
                        const float env = std::exp (-4.5f * (float) i / 12000.0f);
                        rng = rng * 1664525u + 1013904223u;
                        const float nL = ((float) (rng >> 8) / 8388608.0f - 1.0f) * env;
                        rng = rng * 1664525u + 1013904223u;
                        const float nR = ((float) (rng >> 8) / 8388608.0f - 1.0f) * env;
                        irBuf.setSample (0, i, (i == 0 ? 0.8f : 0.0f) + 0.25f * nL);
                        irBuf.setSample (1, i, (i == 0 ? 0.8f : 0.0f) + 0.25f * nR);
                    }
                    writer->writeFromAudioSampleBuffer (irBuf, 0, irBuf.getNumSamples());
                }
            }
        }
        check (processor->loadCustomIrFile (tmpIrFile), "loadCustomIrFile loads external stereo WAV IR file");
        check (processor->hasCustomIr() && processor->getCustomIrName().contains ("vocalgzzio_test_custom_ir"),
               "processor reports active custom IR file name");
        float customCurve[24] {};
        processor->getReverbIrCurve (customCurve, 24);
        check (customCurve[0] > 0.1f, "getReverbIrCurve reflects loaded custom IR envelope");

        // Verify state save/restore persists custom IR
        juce::MemoryBlock irState;
        processor->getStateInformation (irState);
        processor->clearCustomIrFile();
        check (! processor->hasCustomIr(), "clearCustomIrFile clears custom IR");
        processor->setStateInformation (irState.getData(), (int) irState.getSize());
        check (processor->hasCustomIr(), "setStateInformation restores custom IR from saved file path");
        processor->clearCustomIrFile();
        tmpIrFile.deleteFile();

        // Verify 1st-Order ADAA saturation (adaaTanh) smoothness & finite output
        float prevU = 0.0f;
        bool adaaFinite = true;
        for (int i = 0; i < 200; ++i)
        {
            const float u = 2.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 3100.0f * (float) i / 48000.0f);
            const float y = gz::argon::VelvetEngine::adaaTanh (u, prevU);
            if (! std::isfinite (y) || std::abs (y) > 1.001f) adaaFinite = false;
        }
        check (adaaFinite, "1st-Order ADAA tanh saturation is bounded and finite across rapid high-frequency swings");
    }
    // Verify 5 Couture Cosmetics Colorways (01 Rose, 02 Lilac, 03 Noir, 04 Mint, 05 Peach) one-click switching & cycling
    {
        view.setOverview (true);
        view.setOverviewLayoutMode (1); // V-LIVER STAGE
        view.setThemeMode (5);
        check (Palette::isCoutureTheme (view.getThemeMode()) && Palette::coutureIndex (view.getThemeMode()) == 0,
               "Couture theme 01 Rose starts at index 0");
        // Clicking ♥LIVER pill ({236 + 1*68 + 20, 30} = {324, 30}) when already in layout 1 cycles through 5 -> 10 -> 11 -> 12 -> 13 -> 5
        const int expectedCycle[5] = { 10, 11, 12, 13, 5 };
        for (int step = 0; step < 5; ++step)
        {
            check (view.handleOverviewMouseDown ({ 324, 30 }), "clicking ♥LIVER pill in layout 1 is handled");
            check (view.getThemeMode() == expectedCycle[step], "clicking ♥LIVER pill cycles through all 5 Couture colorways");
        }
    }
    // Verify v4.7.0 features:
    //   1. Singing <-> MC Talk 1-click non-destructive toggle (liveTalkMode / talkPillRect)
    //   2. 5-Second Quick Audition Looper (AuditionRecording -> AuditionPlaying -> AuditionOff / loopPillRect)
    //   3. Vocal-Priority Space Ducking meter & DSP response (getSpaceDuckDb)
    {
        view.setOverview (true);
        view.setOverviewLayoutMode (0);
        const juce::Point<int> talkPt  { 965,  92 };
        const juce::Point<int> loopPt  { 1063, 92 };
        const juce::Point<int> coughPt { 1163, 92 };
        const juce::Point<int> stagePt { 1273, 92 };
        check (view.overviewHelpAt (talkPt).isNotEmpty(),  "hovering Singing/MC Talk pill shows explanation");
        check (view.overviewHelpAt (loopPt).isNotEmpty(),  "hovering 5s Audition Looper pill shows explanation");
        check (view.overviewHelpAt (coughPt).isNotEmpty(), "hovering Cough Instant Mute pill shows explanation");
        check (view.overviewHelpAt (stagePt).isNotEmpty(), "hovering BGM/REC Stage pill shows explanation");

        // Set rich singing reverb/delay/width parameters
        setParameter ("mod_hirogari", 1.0f);
        setParameter ("revon", 1.0f);
        setParameter ("revmix", 55.0f);
        setParameter ("delay", 35.0f);
        setParameter ("width", 45.0f);
        setParameter ("duck", 65.0f);

        check (! processor->getLiveTalkMode(), "liveTalkMode starts OFF (Singing mode)");
        check (view.handleOverviewMouseDown (talkPt), "clicking Singing/MC Talk pill is handled");
        check (processor->getLiveTalkMode(), "clicking Singing/MC Talk pill enables MC Talk mode");
        check (std::abs (processor->apvts.getRawParameterValue ("revmix")->load() - 55.0f) < 0.01f
            && std::abs (processor->apvts.getRawParameterValue ("delay")->load() - 35.0f) < 0.01f
            && std::abs (processor->apvts.getRawParameterValue ("width")->load() - 45.0f) < 0.01f,
               "MC Talk mode preserves 100% of APVTS singing parameters");
        check (view.handleOverviewMouseDown (talkPt), "clicking Singing/MC Talk pill again is handled");
        check (! processor->getLiveTalkMode(), "clicking Singing/MC Talk pill returns to Singing mode");

        // Verify 5-Second Quick Audition Looper lifecycle (Off -> Recording -> Playing -> Off)
        check (processor->getAuditionState() == VocalGzzioProcessor::AuditionOff, "Audition Looper starts Off");
        check (view.handleOverviewMouseDown (loopPt), "clicking 5s Audition Looper pill starts recording");
        check (processor->getAuditionState() == VocalGzzioProcessor::AuditionRecording, "Audition Looper enters Recording state");

        juce::MidiBuffer loopMidi;
        juce::AudioBuffer<float> voiceBuf (2, 256);
        // Feed 5.0 seconds (48000 * 5 = 240000 samples -> 940 blocks of 256) of synthetic singing
        for (int b = 0; b < 940; ++b)
        {
            for (int i = 0; i < 256; ++i)
            {
                const float s = 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) (b * 256 + i) / 48000.0f);
                voiceBuf.setSample (0, i, s);
                voiceBuf.setSample (1, i, s);
            }
            processor->processBlock (voiceBuf, loopMidi);
        }
        check (processor->getAuditionState() == VocalGzzioProcessor::AuditionPlaying,
               "Audition Looper automatically transitions from 5s Recording to hands-free Loop Playback");
        check (processor->getSpaceDuckDb() > 0.2f,
               "Vocal-Priority Space Ducking actively ducks reverb while singing and reports dB reduction");

        // Feed pure silence while in AuditionPlaying and verify the looper outputs the recorded phrase
        float loopOutPeak = 0.0f;
        for (int b = 0; b < 10; ++b)
        {
            voiceBuf.clear();
            processor->processBlock (voiceBuf, loopMidi);
            loopOutPeak = juce::jmax (loopOutPeak, voiceBuf.getMagnitude (0, 256));
        }
        check (loopOutPeak > 0.05f, "Audition Looper replays recorded vocal phrase hands-free even when mic input is silent");

        check (view.handleOverviewMouseDown (loopPt), "clicking 5s Audition Looper pill while playing stops loop");
        check (processor->getAuditionState() == VocalGzzioProcessor::AuditionOff, "Audition Looper returns to Off state");

        // Verify v4.8.0 Stage & Studio 4 Pillars:
        //   Pillar 4: Cough Instant Mute (coughPillRect) & Scream Safety Guard
        check (! processor->getCoughMute(), "Cough Instant Mute starts OFF");
        check (view.handleOverviewMouseDown (coughPt), "clicking Cough Instant Mute pill is handled");
        check (processor->getCoughMute(), "clicking Cough Instant Mute pill enables mute");
        check (view.handleOverviewMouseDown (coughPt), "clicking Cough Instant Mute pill again unmutes");
        check (! processor->getCoughMute(), "Cough Instant Mute returns to OFF");

        //   Pillar 1: Built-in BGM Player + Key Shift (±6st) + Pitch Sync + Auto Ducking
        check (! processor->isBgmPlaying(), "BGM Player starts paused");
        check (view.handleOverviewMouseDown (stagePt), "clicking BGM/REC Stage pill in headless test toggles BGM playback");
        check (processor->hasBgmLoaded(), "Built-in demo BGM track is loaded on demand");
        check (processor->isBgmPlaying(), "BGM Player starts playing");
        processor->setBgmPitchSemi (2);
        check (processor->getBgmPitchSemi() == 2, "BGM Key Shift sets +2 semitones");
        processor->setBgmPitchSemi (0);
        processor->setBgmPlaying (false);

        //   Pillar 2: 30s Retroactive WAV Save & 1-Click WAV Recording (Wet+Dry)
        check (processor->getRetroBufferedSec() >= 4.0f, "Retroactive 30s buffer captured singing audio");
        check (processor->saveRetroWavRecording(), "Retroactive 30s Wet+Dry 24-bit WAV save succeeds");

        //   Pillar 3: 5s Voice Target AI Diagnosis (all 5 targets + low-SR 16kHz Nyquist safety)
        for (int t = 0; t < 5; ++t)
        {
            check (processor->runVoiceTargetDiagnosis (t), "5s Voice Target AI Diagnosis succeeds for target style");
            check (processor->getLastVoiceTargetStyle() == t, "lastVoiceTargetStyle matches diagnosed target");
        }
        processor->prepareToPlay (16000.0, 256);
        check (processor->runVoiceTargetDiagnosis (0), "Voice Target AI Diagnosis is Nyquist-safe even at 16kHz headset sample rate");
        processor->prepareToPlay (48000.0, 256);

        // Verify v5.0.0 All-in-One Live & Cover Production Suite 4 Pillars:
        //   Pillar 4: 1-Click Chorus Boost (chorusPillRect at footer right deck)
        const juce::Point<int> chorusPt { 1120, 844 };
        const juce::Point<int> se0Pt    { 1220, 844 };
        const juce::Point<int> se1Pt    { 1313, 844 };
        const juce::Point<int> se2Pt    { 1406, 844 };
        const juce::Point<int> se3Pt    { 1499, 844 };
        check (view.overviewHelpAt (chorusPt).isNotEmpty(), "hovering Chorus Boost pill shows explanation");
        check (view.overviewHelpAt (se0Pt).isNotEmpty(),    "hovering Live SE Applause pill shows explanation");
        processor->setChorusBoostMode (false);
        processor->setAutoChorusMode (false);
        check (! processor->getChorusBoostMode() && ! processor->getAutoChorusMode(), "Chorus Boost starts OFF");
        check (view.handleOverviewMouseDown (chorusPt), "clicking Chorus Boost pill is handled");
        check (processor->getChorusBoostMode() && ! processor->getAutoChorusMode(), "clicking Chorus Boost pill enables Chorus Boost");
        check (view.handleOverviewMouseDown (chorusPt), "clicking Chorus Boost pill again is handled");
        check (! processor->getChorusBoostMode() && processor->getAutoChorusMode(), "second click enters Auto-Chorus Director mode");
        check (view.handleOverviewMouseDown (chorusPt), "clicking Chorus Boost pill third time is handled");
        check (! processor->getChorusBoostMode() && ! processor->getAutoChorusMode(), "third click returns to verse balance (OFF)");

        //   Pillar 3: 4 Live SE Trigger Pads (Applause, Sparkle, Drumroll, Chime)
        const juce::Point<int> sePts[4] = { se0Pt, se1Pt, se2Pt, se3Pt };
        for (int s = 0; s < 4; ++s)
        {
            check (view.handleOverviewMouseDown (sePts[s]), "clicking Live SE pad triggers SE");
            check (processor->getActiveLiveSe() == s, "activeLiveSe matches triggered SE index");
            voiceBuf.clear();
            processor->processBlock (voiceBuf, loopMidi);
            const float mag0 = voiceBuf.getMagnitude (0, 256);
            check (mag0 > 0.001f && mag0 <= 0.15f, "Live SE pad outputs gentle, non-clipping stereo audio in processBlock (peak <= 0.15)");
        }
        check (view.handleOverviewMouseDown (sePts[3]), "clicking active Live SE pad again stops SE");
        check (processor->getActiveLiveSe() == -1, "Live SE returns to stopped (-1)");
        check (view.overviewHelpAt ({ 100, 40 }).isNotEmpty(), "hovering top-left brand/version badge in Overview shows Legacy Time Machine help");

        //   Pillar 1 & 2: 1-Click BGM Vocal Cancel (Karaoke) + -14 LUFS Auto-Master & FullMix Export
        check (! processor->getBgmVocalCancel(), "BGM Vocal Cancel starts OFF");
        processor->setBgmVocalCancel (true);
        check (processor->getBgmVocalCancel(), "BGM Vocal Cancel turns ON");
        processor->setBgmPlaying (true);
        for (int b = 0; b < 40; ++b)
        {
            for (int i = 0; i < 256; ++i)
            {
                const float s = 0.22f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) (b * 256 + i) / 48000.0f);
                voiceBuf.setSample (0, i, s);
                voiceBuf.setSample (1, i, s);
            }
            processor->processBlock (voiceBuf, loopMidi);
        }
        processor->setBgmPlaying (false);
        processor->setBgmVocalCancel (false);
        check (processor->getRecAutoMaster(), "-14 LUFS Auto-Mastering defaults to ON");
        check (processor->saveRetroWavRecording(), "-14 LUFS Auto-Master + FullMix + Wet + Dry retroactive WAV save succeeds");
        check (processor->getLastSavedWavStatus().contains (juce::String::fromUTF8 ("完パケMIX")),
               "Saved status confirms 3-file FullMix (-14 LUFS) export when BGM was active");

        // Verify Interactive Preset Palette (Hover-Before-Select Inspection + Click to Choose)
        if (auto* resetBtn = dynamic_cast<juce::TextButton*> (find ("resetAll")))
            resetBtn->onClick();
        view.setThemeMode (14); // Galaxy theme for visual inspection
        view.setOverview (true);
        check (view.handleOverviewMouseDown ({ 50, 90 }), "clicking 3-Axis Preset badge opens Preset Palette");

        juce::Component* paletteComp = nullptr;
        juce::CallOutBox* paletteCallout = nullptr;
        for (auto* c : view.getChildren())
        {
            if (auto* box = dynamic_cast<juce::CallOutBox*> (c))
            {
                paletteCallout = box;
                paletteComp = box->findChildWithID ("presetPalette");
            }
        }
        check (paletteComp != nullptr && paletteComp->isVisible(), "PresetPalettePopup is open and visible inside CallOutBox");
        if (paletteComp != nullptr)
        {
            const int prevVoiceId = (int) processor->apvts.state.getProperty ("ui_voice_preset", 0);
            check (prevVoiceId == 0, "no voice preset selected before hovering");

            // Simulate mouseMove over card 0 (at {200, 145} inside PresetPalettePopup)
            auto& desktop = juce::Desktop::getInstance();
            juce::MouseEvent hoverEv (desktop.getMainMouseSource(),
                                      juce::Point<float> (200.0f, 145.0f),
                                      juce::ModifierKeys(),
                                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                      paletteComp, paletteComp,
                                      juce::Time::getCurrentTime(),
                                      juce::Point<float> (200.0f, 145.0f),
                                      juce::Time::getCurrentTime(),
                                      0, false);
            paletteComp->mouseMove (hoverEv);
            check ((int) processor->apvts.state.getProperty ("ui_voice_preset", 0) == 0,
                   "hovering over preset card does NOT prematurely select the preset");

            auto savePaletteScreenshot = [&] (const char* filePath)
            {
                juce::Image snap (juce::Image::ARGB, view.getWidth(), view.getHeight(), true, juce::SoftwareImageType());
                {
                    juce::Graphics sg (snap);
                    if (paletteCallout != nullptr) paletteCallout->setVisible (false);
                    view.paintEntireComponent (sg, true);
                    if (paletteCallout != nullptr) paletteCallout->setVisible (true);

                    const auto palArea = view.getLocalArea (paletteComp, paletteComp->getLocalBounds());
                    sg.setColour (juce::Colours::black.withAlpha (0.55f));
                    sg.fillRoundedRectangle (palArea.expanded (10).toFloat().translated (0.0f, 6.0f), 18.0f);

                    juce::Image palSnap (juce::Image::ARGB, paletteComp->getWidth(), paletteComp->getHeight(), true, juce::SoftwareImageType());
                    {
                        juce::Graphics pg (palSnap);
                        paletteComp->paintEntireComponent (pg, true);
                    }
                    sg.setOpacity (1.0f);
                    sg.drawImageAt (palSnap, palArea.getX(), palArea.getY());
                }
                juce::File outPng (filePath);
                if (outPng.getParentDirectory().isDirectory())
                {
                    outPng.deleteFile();
                    juce::FileOutputStream fos (outPng);
                    if (fos.openedOk())
                    {
                        juce::PNGImageFormat pngFmt;
                        pngFmt.writeImageToStream (snap, fos);
                    }
                }
            };

            // Render screenshot of the open Preset Palette while hovering over a card
            savePaletteScreenshot ("C:/Users/silve/.gemini/antigravity/brain/8f2542b1-d681-42f3-92c9-1e836ae605e5/v5_preset_palette_hover.png");

            // Now simulate mouseDown on that card to choose it ("気に入ったら選べる")
            juce::MouseEvent clickEv (desktop.getMainMouseSource(),
                                      juce::Point<float> (200.0f, 145.0f),
                                      juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                      paletteComp, paletteComp,
                                      juce::Time::getCurrentTime(),
                                      juce::Point<float> (200.0f, 145.0f),
                                      juce::Time::getCurrentTime(),
                                      1, false);
            paletteComp->mouseDown (clickEv);
            check ((int) processor->apvts.state.getProperty ("ui_voice_preset", 0) == 1,
                   "clicking hovered preset card in palette selects and applies the preset");

            // Also switch to Tab 3 (Genre & Style, at {1040, 70}) and render a 2nd screenshot
            juce::MouseEvent tabClickEv (desktop.getMainMouseSource(),
                                         juce::Point<float> (1040.0f, 70.0f),
                                         juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                         paletteComp, paletteComp,
                                         juce::Time::getCurrentTime(),
                                         juce::Point<float> (1040.0f, 70.0f),
                                         juce::Time::getCurrentTime(),
                                         1, false);
            paletteComp->mouseDown (tabClickEv);

            // Verify Tablet PC / Touch 2-Step Preview, Finger-Scrubbing (mouseDrag), and Dock Audition button:
            // 1) Toggle keepOpen OFF ({1100, 24}) so we test standard close-on-confirm mode on a tablet
            auto makeLeftClick = [&] (float x, float y)
            {
                return juce::MouseEvent (desktop.getMainMouseSource(),
                                         juce::Point<float> (x, y),
                                         juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                         paletteComp, paletteComp,
                                         juce::Time::getCurrentTime(),
                                         juce::Point<float> (x, y),
                                         juce::Time::getCurrentTime(),
                                         1, false);
            };
            paletteComp->mouseDown (makeLeftClick (1100.0f, 24.0f));
            // 2) Unhovered 1st tap on Card 1 ({200, 185}) previews without selecting or closing
            paletteComp->mouseDown (makeLeftClick (200.0f, 185.0f));
            check ((int) processor->apvts.state.getProperty ("ui_eq_preset", 0) == 0,
                   "Touch / unhovered 1st tap on preset card previews description & recipe without prematurely applying");
            // 3) Finger-scrubbing (mouseDrag) across Card 2 ({200, 224}) updates preview without applying
            paletteComp->mouseDrag (makeLeftClick (200.0f, 224.0f));
            check ((int) processor->apvts.state.getProperty ("ui_eq_preset", 0) == 0,
                   "Touch finger-scrubbing (mouseDrag) across cards updates preview without prematurely applying");
            // 4) Tapping [🔊 開いたまま試聴] (dockAuditionRect at {1050, 580}) auditions the scrubbed Card 2 (1-based ID = 3)
            paletteComp->mouseDown (makeLeftClick (1050.0f, 580.0f));
            check ((int) processor->apvts.state.getProperty ("ui_eq_preset", 0) == 3,
                   "Tapping [🔊 Audition Here] button in bottom dock applies the scrubbed preset while keeping palette open");
            // 5) Test Fat-Finger [次へ ▶] ({840, 580}) and [◀ 前へ] ({730, 580}) stepper buttons in bottom dock:
            paletteComp->mouseDown (makeLeftClick (840.0f, 580.0f));
            check ((int) processor->apvts.state.getProperty ("ui_eq_preset", 0) == 4,
                   "Tapping [Next ▶] stepper button in bottom dock steps to preset 4 and auditions immediately");
            paletteComp->mouseDown (makeLeftClick (730.0f, 580.0f));
            check ((int) processor->apvts.state.getProperty ("ui_eq_preset", 0) == 3,
                   "Tapping [◀ Prev] stepper button in bottom dock steps back to preset 3 and auditions immediately");

            // 6) Test Fat-Finger [🖐 特大カード (01-10)] ({550, 580}) Giant Card Mode (76px tall cards, 8px gap):
            paletteComp->mouseDown (makeLeftClick (550.0f, 580.0f));
            paletteComp->mouseMove (hoverEv);
            savePaletteScreenshot ("C:/Users/silve/.gemini/antigravity/brain/8f2542b1-d681-42f3-92c9-1e836ae605e5/v5_preset_palette_fatfinger_large.png");
            // Cycle back through Giant (11-20) -> All 20 View:
            paletteComp->mouseDown (makeLeftClick (550.0f, 580.0f));
            paletteComp->mouseDown (makeLeftClick (550.0f, 580.0f));

            // Restore keepOpen ON ({1100, 24})
            paletteComp->mouseDown (makeLeftClick (1100.0f, 24.0f));

            paletteComp->mouseMove (hoverEv);
            savePaletteScreenshot ("C:/Users/silve/.gemini/antigravity/brain/8f2542b1-d681-42f3-92c9-1e836ae605e5/v5_preset_palette_genre.png");

            // Also switch to Tab 0 (Quick Finish 6 items, at {150, 70}) and render a 3rd screenshot
            juce::MouseEvent tab0ClickEv (desktop.getMainMouseSource(),
                                          juce::Point<float> (150.0f, 70.0f),
                                          juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                          0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                          paletteComp, paletteComp,
                                          juce::Time::getCurrentTime(),
                                          juce::Point<float> (150.0f, 70.0f),
                                          juce::Time::getCurrentTime(),
                                          1, false);
            paletteComp->mouseDown (tab0ClickEv);
            juce::MouseEvent hover0Ev (desktop.getMainMouseSource(),
                                       juce::Point<float> (200.0f, 220.0f),
                                       juce::ModifierKeys(),
                                       0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                       paletteComp, paletteComp,
                                       juce::Time::getCurrentTime(),
                                       juce::Point<float> (200.0f, 220.0f),
                                       juce::Time::getCurrentTime(),
                                       0, false);
            paletteComp->mouseMove (hover0Ev);
            savePaletteScreenshot ("C:/Users/silve/.gemini/antigravity/brain/8f2542b1-d681-42f3-92c9-1e836ae605e5/v5_preset_palette_finish.png");
        }
        if (paletteCallout != nullptr)
        {
            paletteCallout->setVisible (false);
            view.removeChildComponent (paletteCallout);
            paletteCallout->dismiss();
        }

        // Render screenshot of Fat-Finger Floating Touch Value Loupe while dragging a rotary knob on Ocean Star theme
        view.setThemeMode (0);
        juce::Slider* targetKnob = nullptr;
        for (auto* c : view.getChildren())
        {
            if (auto* sl = dynamic_cast<juce::Slider*> (c))
            {
                if (sl->isVisible() && sl->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag
                    && sl->getX() > 700 && sl->getX() < 950 && sl->getY() > 450)
                {
                    targetKnob = sl;
                    break;
                }
            }
        }
        if (targetKnob != nullptr)
        {
            targetKnob->setValue (3.5, juce::sendNotificationSync);
            if (targetKnob->onDragStart != nullptr)
                targetKnob->onDragStart();
            {
                juce::Image loupeImg (juce::Image::ARGB, view.getWidth(), view.getHeight(), true, juce::SoftwareImageType());
                {
                    juce::Graphics lg (loupeImg);
                    view.paintEntireComponent (lg, true);
                }
                juce::File outLoupe ("C:/Users/silve/.gemini/antigravity/brain/8f2542b1-d681-42f3-92c9-1e836ae605e5/v5_fatfinger_floating_loupe.png");
                if (outLoupe.getParentDirectory().isDirectory())
                {
                    outLoupe.deleteFile();
                    juce::FileOutputStream fos (outLoupe);
                    if (fos.openedOk())
                    {
                        juce::PNGImageFormat pngFmt;
                        pngFmt.writeImageToStream (loupeImg, fos);
                    }
                }
            }
            if (targetKnob->onDragEnd != nullptr)
                targetKnob->onDragEnd();

            // Verify Phase 2 Fat-Finger Fine-Tune Stepper Dock (fineTargetRect / fineMinusRect / finePlusRect / fineResetRect)
            check (! view.getFinePlusRect().isEmpty() && ! view.getFineMinusRect().isEmpty() && ! view.getFineResetRect().isEmpty(),
                   "Fat-Finger Fine-Tune Stepper Dock buttons (- / + / Reset) are present in Overview footer");
            const double valBeforePlus = targetKnob->getValue();
            check (view.handleOverviewMouseDown (view.getFinePlusRect().getCentre()),
                   "Tapping [+] in Fat-Finger Fine-Tune Stepper Dock is handled");
            check (std::abs (targetKnob->getValue() - (valBeforePlus + 0.5)) < 0.05,
                   "Tapping [+] steps target dB knob up by +0.5 dB");
            check (view.handleOverviewMouseDown (view.getFineMinusRect().getCentre()),
                   "Tapping [-] in Fat-Finger Fine-Tune Stepper Dock is handled");
            check (std::abs (targetKnob->getValue() - valBeforePlus) < 0.05,
                   "Tapping [-] steps target dB knob down by -0.5 dB");
            check (view.handleOverviewMouseDown (view.getFineResetRect().getCentre()),
                   "Tapping [Reset] in Fat-Finger Fine-Tune Stepper Dock is handled");
            check (std::abs (targetKnob->getValue() - targetKnob->getDoubleClickReturnValue()) < 0.05,
                   "Tapping [Reset] in Fat-Finger Fine-Tune Stepper Dock resets target knob to its default value");
            check (view.handleOverviewMouseDown (view.getFineTargetRect().getCentre()),
                   "Tapping target pill in Fat-Finger Fine-Tune Stepper Dock cycles to next knob");

            // Verify ClickToEditSlider left/right value-box edge tap steps by fine increment without opening text editor
            if (auto* ctes = dynamic_cast<ClickToEditSlider*> (targetKnob))
            {
                ctes->setValue (2.0, juce::sendNotificationSync);
                ctes->stepByFineIncrement (-1);
                check (std::abs (ctes->getValue() - 1.5) < 0.05, "ClickToEditSlider::stepByFineIncrement(-1) steps dB knob by -0.5 dB");
                ctes->stepByFineIncrement (+1);
                check (std::abs (ctes->getValue() - 2.0) < 0.05, "ClickToEditSlider::stepByFineIncrement(+1) steps dB knob by +0.5 dB");
                ctes->setValue (0.0, juce::sendNotificationSync);
            }
        }

        // Verify Argon bar left-end (-5% step) and right-end (+5% step) fat-finger protection
        {
            view.setOverview (true);
            view.setOverviewLayoutMode (0);
            setParameter ("argon_velvet", 50.0f);
            // In Argon Cockpit (theme 0), argonBarRects[0] is at { 456 + 24 + 14 + 344 = 838, 122 + 154 = 276, 234, 28 }
            const juce::Point<int> barLeftTap  (838 + 20, 276 + 14);
            const juce::Point<int> barRightTap (838 + 234 - 16, 276 + 14);
            check (view.handleOverviewMouseDown (barLeftTap), "Tapping left label end of Argon Velvet bar is handled");
            const float afterMinus = processor->apvts.getRawParameterValue ("argon_velvet")->load();
            check (std::abs (afterMinus - 45.0f) < 1.0f, "Tapping left label end of Argon bar steps -5% instead of slamming to 0%");
            check (view.handleOverviewMouseDown (barRightTap), "Tapping right % end of Argon Velvet bar is handled");
            const float afterPlus = processor->apvts.getRawParameterValue ("argon_velvet")->load();
            check (std::abs (afterPlus - 50.0f) < 1.0f, "Tapping right % end of Argon bar steps +5% instead of slamming to 100%");
        }

        // Verify EQGraph fat-finger 28px catch radius and double-click Q cycling
        {
            setParameter ("seq_on", 1.0f);
            setParameter ("seq_mode", 1.0f); // Manual mode
            setParameter ("seq_f1", 500.0f);
            setParameter ("seq_d1", 6.0f);
            setParameter ("seq_q1", 3.0f);
            auto& eq = view.getEqGraph();
            // Trigger paint so plot rect is initialized
            {
                juce::Image eqImg (juce::Image::ARGB, juce::jmax (64, eq.getWidth()), juce::jmax (32, eq.getHeight()), true);
                juce::Graphics eg (eqImg);
                eq.paint (eg);
            }
            const auto node0 = eq.getBandNodePosPublic (0);
            // Test a fat-finger touch 22px to the left of node0 center (previously failed at >12px!)
            const int hitIdx = eq.hitTestBandPublic (node0.translated (-22.0f, 0.0f));
            check (hitIdx == 0, "EQGraph::hitTestBand catches fat-finger touch 22px away from band node center (28px radius)");
            auto& desktop = juce::Desktop::getInstance();
            juce::MouseEvent dblEv (desktop.getMainMouseSource(),
                                    node0,
                                    juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                    &eq, &eq,
                                    juce::Time::getCurrentTime(),
                                    node0,
                                    juce::Time::getCurrentTime(),
                                    2, false);
            eq.mouseDoubleClick (dblEv);
            const float qAfterDbl = processor->apvts.getRawParameterValue ("seq_q1")->load();
            check (std::abs (qAfterDbl - 6.0f) < 0.2f, "Double-tapping EQ band node cycles Q width (3.0 -> 6.0)");
            setParameter ("seq_mode", 0.0f);
        }

        // Verify On-Demand Lazy Heya IR Synthesis & Legacy Version Time Machine (v1.0.0 - v5.0.0)
        {
            heya::Convolver lazyConv;
            lazyConv.prepare (48000.0, 256, true);
            for (int r = 0; r < heya::kNumRooms; ++r)
                check (! lazyConv.isRoomBuilt (r), "Heya lazyRooms=true does not eagerly synthesize room IRs at startup");
            lazyConv.ensureRoomBuilt (2);
            check (lazyConv.isRoomBuilt (2) && ! lazyConv.isRoomBuilt (0) && ! lazyConv.isRoomBuilt (5),
                   "Heya::ensureRoomBuilt synthesizes only the requested room on demand");

            // Test recalling all 13 generations (0 = v5.0.0 .. 12 = v1.0.0)
            for (int vid = 0; vid < VocalGzzioContent::kLegacyVersionCount; ++vid)
            {
                const auto& spec = VocalGzzioContent::getLegacyVersionSpec (vid);
                view.applyLegacyVersion (vid, true);
                check (processor->getLegacyVersionId()  == spec.id,  "applyLegacyVersion sets processor legacyVersionId");
                check (processor->getLegacyVersionEra() == spec.era, "applyLegacyVersion sets processor legacyVersionEra");
                check (view.isOverview() == (spec.pageMode == 0),    "applyLegacyVersion switches pageMode (Overview vs Detail/Easy)");

                juce::Image snap (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
                juce::Graphics sg (snap);
                view.paintEntireComponent (sg, true);

                // Run a real audio block in this era to verify 0-CPU sleep gating works cleanly
                juce::AudioBuffer<float> buf (2, 256);
                buf.clear();
                juce::MidiBuffer midi;
                processor->processBlock (buf, midi);
            }

            // Verify XML state round-trip of <LEGACY_VER id="v1Idx" era="100" eco="1" />
            const int v1Idx = VocalGzzioContent::kLegacyVersionCount - 1;
            view.applyLegacyVersion (v1Idx, true);
            juce::MemoryBlock stateData;
            processor->getStateInformation (stateData);
            view.applyLegacyVersion (0, true);
            check (processor->getLegacyVersionId() == 0 && processor->getLegacyVersionEra() >= 500,
                   "Reset to v5.0.0 clears legacy version id to 0");
            processor->setStateInformation (stateData.getData(), (int) stateData.getSize());
            check (processor->getLegacyVersionId() == v1Idx && processor->getLegacyVersionEra() == 100,
                   "setStateInformation restores saved legacy version id=12 (v1.0.0) and era=100");

            // Restore v5.0.0 and verify LegacyVersionPopup launches cleanly
            view.applyLegacyVersion (0, true);
            view.openLegacyVersionPopup ({ 724, 20, 64, 34 });
            {
                juce::Image popSnap (juce::Image::ARGB, view.getWidth(), view.getHeight(), true);
                juce::Graphics pg (popSnap);
                view.paintEntireComponent (pg, true);
            }
        }
    }
    view.setLookAndFeel (nullptr);
    processor->releaseResources();
    std::printf ("Overview failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}

