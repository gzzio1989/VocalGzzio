#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"
#include <cstdio>

// 実際の画面部品を描画する。端末の音声機器や利用者の設定には触れない。
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) return 2;
    VocalGzzioProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);
    GzzioLnF look;
    look.setKawaiiTypeface (juce::Typeface::createSystemTypefaceFor (
        BinaryData::kawaii_font_ttf, BinaryData::kawaii_font_ttfSize));
    const float fontScale = argc > 4 ? (float) std::atof (argv[4]) : 1.5f;
    look.setFontScale (fontScale);
    {
        VocalGzzioContent content (processor);
        content.setLookAndFeel (&look);
        content.setSize (VocalGzzioEditor::baseW, VocalGzzioEditor::baseH);
        content.setFontScale (fontScale);
        const int theme = argc > 2 ? std::atoi (argv[2]) : 0;
        const bool detailed = argc > 3 && std::atoi (argv[3]) != 0;
        content.restoreUiState (detailed, 0, false, theme);
        if (argc > 5) content.setFocusedEditing (std::atoi (argv[5]) != 0);
        if (argc > 6) content.setOverview (std::atoi (argv[6]) != 0);
        if (argc > 7 && std::atoi (argv[7]) >= 0) content.setOverviewLayoutMode (std::atoi (argv[7]));
        if (argc > 8 && std::atoi (argv[8]) != 0)
        {
            auto setParam = [&] (const char* id, float val)
            {
                if (auto* p = processor.apvts.getParameter (id))
                    p->setValueNotifyingHost (processor.apvts.getParameterRange (id).convertTo0to1 (val));
            };
            setParam ("argon_velvet", 62.0f);
            setParam ("argon_focus",  58.0f);
            setParam ("argon_silk",   54.0f);
            setParam ("argon_halo",   42.0f);
            setParam ("denoise",      28.0f);
            setParam ("comp2",        48.0f);
            setParam ("air",          36.0f);
            setParam ("revmix",       24.0f);
            content.syncArgonOrbFromParams();
            juce::AudioBuffer<float> demoBuf (2, 256);
            juce::MidiBuffer midi;
            double phase = 0.0;
            const double f0 = 220.0; // A3 vocal note
            for (int b = 0; b < 24; ++b)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float s = (float) (0.32 * std::sin (phase)
                                           + 0.16 * std::sin (phase * 2.0)
                                           + 0.09 * std::sin (phase * 3.0)
                                           + 0.05 * std::sin (phase * 5.0)
                                           + 0.02 * std::sin (phase * 14.0));
                    demoBuf.setSample (0, i, s);
                    demoBuf.setSample (1, i, s);
                    phase += 2.0 * juce::MathConstants<double>::pi * f0 / 48000.0;
                }
                processor.processBlock (demoBuf, midi);
            }
            content.setOverview (content.isOverview());
        }
        if (argc > 9 && std::atoi (argv[9]) != 0) content.setLiteMode (true);
        content.resized();
        auto picture = content.createComponentSnapshot (content.getLocalBounds());
        const auto target = juce::File::getCurrentWorkingDirectory().getChildFile (
            juce::String::fromUTF8 (argv[1]));
        target.getParentDirectory().createDirectory();
        juce::FileOutputStream output (target);
        if (! output.openedOk()) return 3;
        output.setPosition (0);
        output.truncate();
        juce::PNGImageFormat format;
        if (! format.writeImageToStream (picture, output)) return 4;
        content.setLookAndFeel (nullptr);
    }
    processor.releaseResources();
    return 0;
}
