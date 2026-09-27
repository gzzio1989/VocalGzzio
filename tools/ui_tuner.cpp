#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    int failures = 0;
    const int notes[] = { 40, 45, 50, 55, 59, 64 };
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto processor = std::make_unique<VocalGzzioProcessor>();
        processor->setPlayConfigDetails (2, 2, rate, 256);
        processor->prepareToPlay (rate, 256);
        for (int m = 0; m < gz::ModuleChain::Count; ++m)
            processor->apvts.getParameter (gz::ModuleChain::paramId (m))->setValueNotifyingHost (0.0f);
        VocalTuner tuner (*processor);
        tuner.setGuitarMode (true);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        auto feed = [&] (double hz)
        {
            const int blocks = (int) std::ceil (juce::jmax (0.4, 33000.0 / rate) * rate / 256.0);
            for (int block = 0; block < blocks; ++block)
            {
                for (int n = 0; n < 256; ++n)
                {
                    const double phase = juce::MathConstants<double>::twoPi * hz * (block * 256 + n) / rate;
                    const float sample = hz > 0 ? (float) (0.18 * std::sin (phase) + 0.06 * std::sin (2 * phase) + 0.03 * std::sin (3 * phase)) : 0.0f;
                    buffer.setSample (0, n, sample); buffer.setSample (1, n, sample);
                }
                processor->processBlock (buffer, midi);
            }
        };
        for (int i = 0; i < 6; ++i)
        {
            const double target = 440.0 * std::pow (2.0, (notes[i] - 69) / 12.0);
            tuner.setGuitarString (0);
            feed (target);
            const float detected = tuner.measureForTest();
            const double error = detected > 0 ? std::abs (1200.0 * std::log2 (detected / target)) : 9999.0;
            const bool ok = error < 3.0 && tuner.detectedStringForTest() == i + 1;
            std::printf ("%.0f Hz / string %d: %.3f cents %s\n", rate, 6 - i, error, ok ? "PASS" : "FAIL");
            if (! ok) ++failures;
            tuner.setGuitarString (i + 1);
            feed (target * std::pow (2.0, 20.0 / 1200.0));
            tuner.measureForTest();
            if (std::abs (tuner.centsForTest() - 20.0f) > 3.0f) ++failures;
        }
        feed (0.0);
        if (tuner.measureForTest() != 0.0f) ++failures;
        processor->releaseResources();
    }
    return failures == 0 ? 0 : 1;
}
