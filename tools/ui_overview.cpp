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
    for (int theme : { 0, 1, 2, 3, 7, 8 })
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
    view.setLookAndFeel (nullptr);
    processor->releaseResources();
    std::printf ("Overview failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
