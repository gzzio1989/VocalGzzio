#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetDefs.h"
#include <cstdio>

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
            }
            auto* details = dynamic_cast<juce::TextButton*> (find ("openDetails"));
            details->onClick();
            check (! view.isOverview(), "all controls can be opened");
            auto* back = dynamic_cast<juce::TextButton*> (find ("openOverview"));
            check (back && back->isVisible(), "overview return button reachable");
            if (back) back->onClick();
            check (view.isOverview(), "return restores overview");
        }
    view.setLookAndFeel (nullptr);
    processor->releaseResources();
    std::printf ("Overview failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
