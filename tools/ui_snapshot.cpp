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
