#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
#include <chrono>

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
        enum class InputLayout { both, opposite, rightOnly, leftOnly };
        auto feed = [&] (double hz, bool dominantHarmonic = false,
                         InputLayout layout = InputLayout::both, float level = 1.0f)
        {
            const int blocks = (int) std::ceil (juce::jmax (0.4, 33000.0 / rate) * rate / 256.0);
            for (int block = 0; block < blocks; ++block)
            {
                for (int n = 0; n < 256; ++n)
                {
                    const double phase = juce::MathConstants<double>::twoPi * hz * (block * 256 + n) / rate;
                    const float sample = hz > 0 ? (float) ((dominantHarmonic ? 0.06 : 0.18) * std::sin (phase)
                        + (dominantHarmonic ? 0.18 : 0.06) * std::sin (2 * phase) + 0.03 * std::sin (3 * phase)) : 0.0f;
                    buffer.setSample (0, n, layout == InputLayout::rightOnly ? 0.0f : sample * level);
                    buffer.setSample (1, n, layout == InputLayout::leftOnly ? 0.0f
                                           : sample * level * (layout == InputLayout::opposite ? -1.0f : 1.0f));
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

        const int bassNotes[] = { 23, 28, 33, 38, 43 };
        for (int mode : { 2, 3 })
        {
            tuner.setTuningMode (mode);
            const int offset = mode == 2 ? 1 : 0;
            for (int i = offset; i < 5; ++i)
            {
                const int choice = i - offset + 1;
                const double target = 440.0 * std::pow (2.0, (bassNotes[i] - 69) / 12.0);
                tuner.setGuitarString (0);
                feed (target, true);
                const float detected = tuner.measureForTest();
                const double error = detected > 0 ? std::abs (1200.0 * std::log2 (detected / target)) : 9999.0;
                const bool ok = error < 3.0 && tuner.detectedStringForTest() == choice;
                std::printf ("%.0f Hz / bass mode %d MIDI %d (strong second harmonic): %.3f cents %s\n",
                             rate, mode, bassNotes[i], error, ok ? "PASS" : "FAIL");
                if (! ok) ++failures;
                tuner.setGuitarString (choice);
                for (double detuning : { -35.0, 25.0 })
                {
                    feed (target * std::pow (2.0, detuning / 1200.0));
                    const bool detectedPitch = tuner.measureForTest() > 0.0f;
                    if (! detectedPitch || std::abs (tuner.centsForTest() - detuning) > 3.0) ++failures;
                }
            }
        }

        // Stereo polarity and one-input instruments must not disappear in the
        // tuner. Quiet one-sided input is deliberately below the old averaged
        // detector's level threshold while the actual input remains audible.
        tuner.setTuningMode (3);
        const double lowB = 440.0 * std::pow (2.0, (23 - 69) / 12.0);
        for (auto layout : { InputLayout::opposite, InputLayout::rightOnly, InputLayout::leftOnly })
        {
            tuner.setGuitarString (0);
            feed (lowB, false, layout, layout == InputLayout::opposite ? 1.0f : 0.02f);
            const float detected = tuner.measureForTest();
            const double error = detected > 0 ? std::abs (1200.0 * std::log2 (detected / lowB)) : 9999.0;
            const bool ok = error < 3.0 && tuner.detectedStringForTest() == 1;
            std::printf ("%.0f Hz / B0 input layout %d: %.3f cents %s\n",
                         rate, (int) layout, error, ok ? "PASS" : "FAIL");
            if (! ok) ++failures;
        }

        // A below-range note must not silently retain a now-invalid string
        // index when changing from a six-string guitar to a four-string bass.
        tuner.setGuitarMode (true);
        tuner.setGuitarString (6);
        tuner.setTuningMode (2);
        const double lowE = 440.0 * std::pow (2.0, (28 - 69) / 12.0);
        feed (lowE);
        if (tuner.measureForTest() <= 0.0f || tuner.detectedStringForTest() != 1) ++failures;

        if (rate == 48000.0)
        {
            auto frame = [&] (double cents)
            {
                feed (lowE * std::pow (2.0, cents / 1200.0));
                tuner.measureForTest();
                tuner.advanceDisplayForTest();
            };
            tuner.setGuitarString (1);
            frame (0.0);
            if (tuner.inTuneForTest()) ++failures; // a fresh transient cannot turn green
            for (int i = 0; i < 10; ++i) frame (0.0);
            if (! tuner.stableForTest() || ! tuner.inTuneForTest()) ++failures;
            float maximumNeedleSwing = 0.0f;
            for (int i = 0; i < 18; ++i)
            {
                const float previous = tuner.displayCentsForTest();
                frame (i % 2 == 0 ? -6.0 : 6.0);
                maximumNeedleSwing = juce::jmax (maximumNeedleSwing, std::abs (tuner.displayCentsForTest() - previous));
            }
            const bool smooth = maximumNeedleSwing < 3.0f && ! tuner.stableForTest();
            std::printf ("Alternating +/-6 cent input: largest displayed step %.3f cents %s\n", maximumNeedleSwing, smooth ? "PASS" : "FAIL");
            if (! smooth) ++failures;
            frame (25.0);
            if (tuner.inTuneForTest()) ++failures; // smoothed display must not hide a new error
            for (int i = 0; i < 24; ++i) frame (25.0);
            if (! tuner.stableForTest() || std::abs (tuner.displayCentsForTest() - 25.0f) > 1.0f) ++failures;
            feed (0.0);
            tuner.measureForTest(); tuner.advanceDisplayForTest();
            if (tuner.inTuneForTest() || tuner.stableForTest()) ++failures;

            // Changing strings must acquire the new note without sweeping
            // through an invented in-tune centre reading for the previous one.
            tuner.setGuitarString (0);
            for (int i = 0; i < 6; ++i) frame (0.0);
            const double nextString = 55.0;
            for (int i = 0; i < 6; ++i)
            {
                feed (nextString); tuner.measureForTest(); tuner.advanceDisplayForTest();
                if (i == 0 && tuner.inTuneForTest()) ++failures;
            }
            if (tuner.displayNoteForTest() != 33 || ! tuner.inTuneForTest()) ++failures;

            tuner.setTuningMode (0);
            feed (220.0);
            if (std::abs (tuner.measureForTest() - 220.0f) > 0.5f) ++failures;
            tuner.setTuningMode (3);
            feed (30.867706);
            const auto begin = std::chrono::steady_clock::now();
            for (int i = 0; i < 100; ++i) tuner.measureForTest();
            const double milliseconds = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - begin).count() / 100.0;
            std::printf ("Bass analysis mean %.3f ms (100 measurements, no audio latency added)\n", milliseconds);
        }
        processor->releaseResources();
    }
    std::printf ("Tuner failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
