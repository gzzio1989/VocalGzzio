#include "PluginEditor.h"
#include <cstring>
#include "PresetDefs.h"
#include "BinaryData.h"
#include "Tooltips.h"
#include <cmath>
#include <cstdlib>   // v1.7.0: std::getenv for the GZ_THEME test hook

namespace
{
    const char* kNoteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
}

// v1.7.0 yuru-kawa: embedded rounded-font statics (shared by the static uiFont)
juce::Typeface::Ptr GzzioLnF::s_kawaiiFace;
bool                GzzioLnF::s_useKawaii = false;
float               GzzioLnF::s_juicePhase = 0.0f;
std::atomic<int>    GzzioLnF::s_instances { 0 };

//==============================================================================
// Tuner
//==============================================================================
void VocalTuner::refreshLanguage()
{
    railToggle .setButtonText (tip::T ("音程の履歴", "Pitch history"));
    railToggle .setTooltip    (tip::rail_tip());
    rangeButton.setButtonText (tip::range_label());
    const int reference = refPitchBox.getSelectedId();
    refPitchBox.clear (juce::dontSendNotification);
    for (int hz = 415; hz <= 445; ++hz)
        refPitchBox.addItem (tip::T ("基準 ", "A = ") + juce::String (hz) + " Hz", hz);
    refPitchBox.setSelectedId (reference > 0 ? reference : 440, juce::dontSendNotification);
    tunerModeBox.clear (juce::dontSendNotification);
    tunerModeBox.addItem (tip::T ("歌声・音程", "Vocal pitch"), 1);
    tunerModeBox.addItem (tip::T ("ギター調弦", "Guitar tuning"), 2);
    tunerModeBox.setSelectedId (guitarMode ? 2 : 1, juce::dontSendNotification);
    guitarStringBox.clear (juce::dontSendNotification);
    guitarStringBox.addItem (tip::T ("弦を自動判定", "Auto string"), 1);
    const char* ja[] = { "6弦 ミ E2", "5弦 ラ A2", "4弦 レ D3", "3弦 ソ G3", "2弦 シ B3", "1弦 ミ E4" };
    const char* en[] = { "6th string E2", "5th string A2", "4th string D3", "3rd string G3", "2nd string B3", "1st string E4" };
    for (int i = 0; i < 6; ++i) guitarStringBox.addItem (tip::T (ja[i], en[i]), i + 2);
    guitarStringBox.setSelectedId (guitarString + 1, juce::dontSendNotification);
    refPitchBox.setTooltip (tip::T ("ラの基準周波数。通常は440 Hz。伴奏やほかの楽器に合わせて変更できます。", "Reference frequency for A4. Usually 440 Hz. Match this to your accompaniment or other instruments."));
    refPitchBox.setTitle (tip::T ("チューナーの基準周波数", "Tuner reference pitch"));
    tunerModeBox.setTitle (tip::T ("チューナーの用途", "Tuner mode"));
    guitarStringBox.setTitle (tip::T ("調弦する弦", "Guitar string"));
    guitarStringBox.setTooltip (tip::T ("標準チューニング。開放弦を1本ずつ鳴らしてください。大きくずれているときは弦を指定します。", "Standard tuning. Play one open string at a time. Select the string manually if it is far out of tune."));
    repaint();
}

VocalTuner::VocalTuner (VocalGzzioProcessor& p) : proc (p)
{
    buffer.resize ((size_t) VocalGzzioProcessor::tunerSize);
    analysisBuffer.reserve (2048);
    for (int hz = 415; hz <= 445; ++hz)
        refPitchBox.addItem (juce::String::fromUTF8 ("基準 ") + juce::String (hz) + " Hz", hz);
    refPitchBox.setSelectedId (440, juce::dontSendNotification);
    refPitchBox.setJustificationType (juce::Justification::centred);
    refPitchBox.onChange = [this]
    {
        if (auto* prm = proc.apvts.getParameter ("refpitch"))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (
                proc.apvts.getParameterRange ("refpitch")
                    .convertTo0to1 ((float) refPitchBox.getSelectedId()));
            prm->endChangeGesture();
            clearPitchDisplay();
        }
    };
    refPitchBox.setTooltip (juce::String::fromUTF8 ("ラの基準周波数。通常は440 Hz。伴奏やほかの楽器に合わせて変更できます。"));
    refPitchBox.setTitle (juce::String::fromUTF8 ("チューナーの基準周波数"));
    addAndMakeVisible (refPitchBox);

    tunerModeBox.addItem (juce::String::fromUTF8 ("歌声・音程"), 1);
    tunerModeBox.addItem (juce::String::fromUTF8 ("ギター調弦"), 2);
    tunerModeBox.setSelectedId (1, juce::dontSendNotification);
    tunerModeBox.onChange = [this] { setGuitarMode (tunerModeBox.getSelectedId() == 2); };
    tunerModeBox.setTitle (juce::String::fromUTF8 ("チューナーの用途"));
    addAndMakeVisible (tunerModeBox);
    guitarStringBox.addItem (juce::String::fromUTF8 ("弦を自動判定"), 1);
    const char* strings[] = { "6弦 ミ E2", "5弦 ラ A2", "4弦 レ D3", "3弦 ソ G3", "2弦 シ B3", "1弦 ミ E4" };
    for (int i = 0; i < 6; ++i) guitarStringBox.addItem (juce::String::fromUTF8 (strings[i]), i + 2);
    guitarStringBox.setSelectedId (1, juce::dontSendNotification);
    guitarStringBox.onChange = [this] { setGuitarString (guitarStringBox.getSelectedId() - 1); };
    guitarStringBox.setTitle (juce::String::fromUTF8 ("調弦する弦"));
    guitarStringBox.setTooltip (juce::String::fromUTF8 ("標準チューニング。1本ずつ開放弦を鳴らしてください。大きくずれているときは弦を指定します。"));
    addChildComponent (guitarStringBox);

    // v1.4.0: note-rail toggle + vocal-range check (in the tuner header)
    railToggle.setButtonText (tip::T ("音程の履歴", "Pitch history"));
    railToggle.setTooltip (tip::rail_tip());
    railToggle.setClickingTogglesState (true);
    railToggle.setColour (juce::TextButton::buttonOnColourId, Palette::ice);
    railToggle.onClick = [this] { railMode = railToggle.getToggleState(); if (onRailChange) onRailChange (railMode); repaint(); };
    addAndMakeVisible (railToggle);

    rangeButton.setButtonText (tip::range_label());
    rangeButton.setTooltip (tip::range_tip());
    rangeButton.onClick = [this]
    {
        if (rangeChecking) stopRange();
        else               startRange();
    };
    addAndMakeVisible (rangeButton);

    for (auto& b : histValid) b = false;
    for (auto& m : histMidi)  m = -1;

    // v1.6.0: build the two gear silhouettes once (trapezoid teeth, hub hole and
    // three lightening holes via even-odd fill -> the "nikunuki" watch look)
    auto makeGear = [] (float rTip, int teeth, float hubR, float holeR)
    {
        juce::Path g2;
        const float rRoot = rTip * 0.80f;
        const float step  = juce::MathConstants<float>::twoPi / (float) teeth;
        for (int t = 0; t < teeth; ++t)
        {
            const float a0 = (float) t * step;
            const float a1 = a0 + step * 0.22f;
            const float a2 = a0 + step * 0.50f;
            const float a3 = a0 + step * 0.72f;
            auto pt = [] (float r, float a) { return juce::Point<float> (r * std::sin (a), -r * std::cos (a)); };
            if (t == 0) g2.startNewSubPath (pt (rRoot, a0));
            else        g2.lineTo          (pt (rRoot, a0));
            g2.lineTo (pt (rTip,  a1));
            g2.lineTo (pt (rTip,  a2));
            g2.lineTo (pt (rRoot, a3));
        }
        g2.closeSubPath();
        g2.setUsingNonZeroWinding (false);                       // even-odd: holes below
        g2.addEllipse (-hubR, -hubR, hubR * 2.0f, hubR * 2.0f);  // axle hole
        if (holeR > 0.0f)
            for (int h = 0; h < 3; ++h)
            {
                const float ha = (float) h * juce::MathConstants<float>::twoPi / 3.0f;
                const float hr = (rRoot + hubR) * 0.52f;
                g2.addEllipse (hr * std::sin (ha) - holeR, -hr * std::cos (ha) - holeR,
                               holeR * 2.0f, holeR * 2.0f);
            }
        return g2;
    };
    gearBig   = makeGear (16.0f, 12, 3.4f, 3.6f);
    gearSmall = makeGear (10.0f,  8, 2.6f, 0.0f);

    refreshLanguage();
    startTimerHz (30);
}

void VocalTuner::clearPitchDisplay()
{
    hasPitch = false;
    hold = 0;
    displayMidi = lastMidi = -1;
    dispCents = dispFreq = 0.0f;
    for (auto& valid : histValid) valid = false;
}

void VocalTuner::setGuitarMode (bool enabled)
{
    if (guitarMode != enabled) clearPitchDisplay();
    guitarMode = enabled;
    tunerModeBox.setSelectedId (enabled ? 2 : 1, juce::dontSendNotification);
    guitarStringBox.setVisible (enabled);
    railToggle.setVisible (rangeToolsVisible && ! enabled);
    rangeButton.setVisible (rangeToolsVisible && ! enabled);
    if (enabled && rangeChecking) stopRange();
    resized();
    repaint();
}

void VocalTuner::setGuitarString (int stringChoice)
{
    guitarString = juce::jlimit (0, 6, stringChoice);
    guitarStringBox.setSelectedId (guitarString + 1, juce::dontSendNotification);
    clearPitchDisplay();
    repaint();
}

int VocalTuner::controlHeight() const
{
    return getWidth() < 740 ? 84 : 44;
}

//==============================================================================
// v1.4.0: MIDI note -> Japanese singer-community notation.
// Confirmed rule from research: C4 = mid2C, A4 = hiA (440 Hz). The zone name
// changes at A, not C: ... mid2G, mid2G#, hiA, hiA#, hiB, hiC(=C5) ...
// Zones by octave-of-A: A2/A#2/B2 -> low ; then mid1 (from A3? no) -- we map by
// the A-anchored band index so boundaries land correctly.
juce::String VocalTuner::midiToJp (int midi)
{
    if (midi < 0) return "--";
    static const char* names[12] =
        { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int pc = ((midi % 12) + 12) % 12;
    // Zones split at A (not C): mid1 = A2..G#3, mid2 = A3..G#4, hi = A4..G#5.
    // Anchor each note to the A at/below it, then label that A's octave band.
    const int semiFromA = ((pc - 9) + 12) % 12;              // 0 at A, 11 at G#
    const int band      = (midi - 9 - semiFromA) / 12 - 1;   // octave of the anchoring A
    const char* z;
    switch (band)
    {
        case 0:  z = "lowlow"; break;
        case 1:  z = "low";    break;
        case 2:  z = "mid1";   break;
        case 3:  z = "mid2";   break;
        case 4:  z = "hi";     break;
        case 5:  z = "hihi";   break;
        case 6:  z = "hihihi"; break;
        default: z = (band < 0 ? "lowlow" : "hihihi"); break;
    }
    return juce::String (z) + names[pc];
}

juce::String VocalTuner::rangeResultText() const
{
    if (rangeHi < 0) return juce::String();
    const int span = rangeHi - rangeLo;
    const int oct  = span / 12;
    const int semi = span % 12;
    juce::String s = midiToJp (rangeLo) + juce::String::fromUTF8 ("\x20\xe3\x80\x9c\x20")   // " 〜 "
                   + midiToJp (rangeHi) + tip::T ("\x20\x28\xe7\xb4\x84", " (approx. ")   // " (約"
                   + juce::String (oct) + tip::T ("\xe3\x82\xaa\xe3\x82\xaf\xe3\x82\xbf\xe3\x83\xbc\xe3\x83\x96", " oct")   // オクターブ
                   + (semi > 0 ? juce::String (semi) + tip::T ("\xe9\x9f\xb3", " semitones") : juce::String())   // N音
                   + ")";
    return s;
}

// v1.4.0 P5: YIN pitch detection (de Cheveigne & Kawahara 2002).
// Replaces the old raw-autocorrelation detector, which biased toward the shortest
// lag and produced frequent octave errors -- the reason the range check mis-read.
// YIN's cumulative-mean-normalised difference + absolute threshold is the standard
// robust monophonic voice pitch method and removes almost all octave jumps.
void VocalTuner::analyse()
{
    proc.readTunerBuffer (buffer);
    hasPitch = false; pitchConf = 0.0f;
    const double sourceRate = proc.getTunerSampleRate();
    if (buffer.size() < 512 || sourceRate < 8000.0) return;

    // Keep the same physical observation window at every host rate. The old
    // 2048-sample window could not contain two E2 periods at 96/192 kHz.
    const int step = juce::jmax (1, (int) std::round (sourceRate / 16000.0));
    const int count = juce::jmin ((int) buffer.size(), (int) std::ceil (sourceRate * 0.095));
    const double sr = sourceRate / step;
    analysisBuffer.clear();
    const float alpha = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 2600.0f / (float) sourceRate);
    float low1 = 0.0f, low2 = 0.0f;
    int decimation = 0;
    for (int i = (int) buffer.size() - count; i < (int) buffer.size(); ++i)
    {
        const float sample = buffer[(size_t) i];
        if (! std::isfinite (sample)) return;
        low1 += alpha * (sample - low1);
        low2 += alpha * (low1 - low2);
        if (++decimation == step) { analysisBuffer.push_back (low2); decimation = 0; }
    }
    const int N = (int) analysisBuffer.size();
    if (N < 256) return;

    // remove DC and reject silence
    double mean = 0.0;
    for (int i = 0; i < N; ++i) mean += analysisBuffer[(size_t) i];
    mean /= N;
    double energy = 0.0;
    for (int i = 0; i < N; ++i) { const double v = analysisBuffer[(size_t) i] - mean; energy += v * v; }
    const double rms = std::sqrt (energy / N);
    if (rms < 0.0016) return;

    // search lags for ~70 Hz (low male) up to ~1100 Hz (soprano); keep a constant
    // number of difference terms so the CMND normalisation stays comparable.
    const int maxLag = juce::jmin (N / 2, (int) (sr / 65.0));
    const int minLag = juce::jmax (2,     (int) (sr / 1100.0));
    if (maxLag <= minLag + 2) return;
    const int W = N - maxLag;   // difference-window length

    std::vector<double> d   ((size_t) (maxLag + 1), 0.0);   // difference function
    std::vector<double> cm  ((size_t) (maxLag + 1), 1.0);   // cumulative-mean-normalised

    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double s = 0.0;
        const float* x = analysisBuffer.data();
        for (int j = 0; j < W; ++j)
        {
            const double diff = (double) x[j] - (double) x[j + tau];
            s += diff * diff;
        }
        d[(size_t) tau] = s;
    }

    double running = 0.0;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        running += d[(size_t) tau];
        cm[(size_t) tau] = running > 0.0 ? d[(size_t) tau] * (double) tau / running : 1.0;
    }

    // absolute threshold: first dip below THRESH, then descend to its local minimum
    const double THRESH = 0.14;
    int tau = -1;
    for (int t = minLag; t < maxLag; ++t)
    {
        if (cm[(size_t) t] < THRESH)
        {
            while (t + 1 <= maxLag && cm[(size_t) (t + 1)] < cm[(size_t) t]) ++t;
            tau = t; break;
        }
    }
    if (tau < 0)   // nothing crossed the threshold: fall back to the global minimum
    {
        double bestv = 1e18; int bestt = -1;
        for (int t = minLag; t <= maxLag; ++t)
            if (cm[(size_t) t] < bestv) { bestv = cm[(size_t) t]; bestt = t; }
        if (bestt < 0 || bestv > 0.25) return;   // noise must not look like a tuned string
        tau = bestt;
    }

    // parabolic interpolation around the chosen lag for sub-sample precision
    double period = tau;
    if (tau > minLag && tau < maxLag)
    {
        const double a = cm[(size_t) (tau - 1)];
        const double b = cm[(size_t) tau];
        const double c = cm[(size_t) (tau + 1)];
        const double denom = (a - 2.0 * b + c);
        if (std::abs (denom) > 1e-12)
            period = (double) tau + 0.5 * (a - c) / denom;
    }
    if (period < 1.0) return;

    freq = (float) (sr / period);
    pitchConf = (float) juce::jlimit (0.0, 1.0, 1.0 - cm[(size_t) tau]);

    const float refHz = proc.apvts.getRawParameterValue ("refpitch")->load();
    const double midi = 69.0 + 12.0 * std::log2 (freq / refHz);
    const int nearest = (int) std::lround (midi);
    int target = nearest;
    if (guitarMode)
    {
        detectedString = guitarString;
        if (detectedString == 0)
        {
            double nearestDistance = 1.0e9;
            for (int i = 0; i < 6; ++i)
            {
                const double distance = std::abs (midi - guitarNotes[i]);
                if (distance < nearestDistance) { nearestDistance = distance; detectedString = i + 1; }
            }
        }
        target = guitarNotes[detectedString - 1];
    }
    cents = (float) ((midi - target) * 100.0);
    const int nn  = ((nearest % 12) + 12) % 12;
    const int oct = nearest / 12 - 1;
    noteName = juce::String (kNoteNames[nn]) + juce::String (oct);
    lastMidi = nearest;
    hasPitch = true;

    // ---- vocal-range capture: only extend on a stably HELD, confident note ----
    // A note must be the same for several consecutive frames and sit inside the
    // plausible sung band; this blocks single-frame octave/harmonic glitches from
    // stretching the measured range.
    if (rangeChecking && ! guitarMode)
    {
        const bool trustworthy = pitchConf > 0.55f
                              && nearest >= kRangeLoMidi && nearest <= kRangeHiMidi;
        if (trustworthy)
        {
            if (nearest == rangeCand) ++rangeCandCount;
            else { rangeCand = nearest; rangeCandCount = 1; }

            if (rangeCandCount >= 3)   // ~100 ms held at 30 Hz -> accept
            {
                rangeLo = juce::jmin (rangeLo, nearest);
                rangeHi = juce::jmax (rangeHi, nearest);
            }
        }
        else { rangeCand = -1; rangeCandCount = 0; }
    }
}

void VocalTuner::timerCallback()
{
    analyse();

    // v1.6.0 gear meter: rotation speed follows the output level (near-still
    // when silent, lively while singing), like a living wound-up movement.
    {
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        const float dt = gearLastMs == 0 ? 0.033f
                                         : juce::jlimit (0.0f, 0.06f, (float) (now - gearLastMs) * 0.001f);
        gearLastMs = now;
        const float lvl = juce::jlimit (0.0f, 1.0f, dispOut * 1.6f);
        gearAngle = std::fmod (gearAngle + dt * (18.0f + 560.0f * lvl), 360.0f);
    }


    if (hasPitch)
    {
        // A note change is a new measurement, not a sweep through intervening
        // pitches. Short gaps keep the last value but can never turn it green.
        const int newDisplayMidi = guitarMode ? guitarNotes[detectedString - 1] : lastMidi;
        if (hold == 0 || displayMidi != newDisplayMidi)
        {
            dispCents = cents;
            dispFreq = freq;
        }
        else
        {
            dispCents += (cents - dispCents) * 0.38f;
            dispFreq += (freq - dispFreq) * 0.38f;
        }
        displayMidi = newDisplayMidi;
        hold = 8;
    }
    else if (hold > 0)
    {
        --hold;
    }

    // advance scrolling pitch history (newest sample stored at histPos)
    histCents[histPos] = juce::jlimit (-50.0f, 50.0f, cents);
    histValid[histPos] = hasPitch;
    histMidi [histPos] = hasPitch ? lastMidi : -1;
    histPos = (histPos + 1) % histLen;

    dispIn  += (proc.getInputLevel()      - dispIn)  * 0.25f;
    dispOut += (proc.getOutputLevel()     - dispOut) * 0.25f;
    dispGR  += (proc.getGainReductionDb() - dispGR)  * 0.25f;
    dispDS  += (proc.getDeEssActivity()   - dispDS)  * 0.30f;
    dispDN  += (proc.getDenoiseActivity() - dispDN)  * 0.30f;

    const int apvtsHz = (int) std::round (proc.apvts.getRawParameterValue ("refpitch")->load());
    if (refPitchBox.getSelectedId() != apvtsHz)
        refPitchBox.setSelectedId (apvtsHz, juce::dontSendNotification);

    const auto rWant = rangeChecking ? tip::range_stop() : tip::range_label();
    if (rangeButton.getButtonText() != rWant)
        rangeButton.setButtonText (rWant);
    rangeButton.setColour (juce::TextButton::buttonColourId,
                           rangeChecking ? Palette::salmon : Palette::track.withAlpha (0.5f));

    repaint();
}

void VocalTuner::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (full, 14.0f);
    g.setColour (Palette::panelLn);
    g.drawRoundedRectangle (full.reduced (0.5f), 14.0f, 1.0f);

    float fs = 1.0f;
    if (auto* look = dynamic_cast<GzzioLnF*> (&getLookAndFeel()))
        fs = juce::jlimit (1.0f, 1.5f, look->getFontScale());
    auto font = [fs] (float size, bool bold = false)
    {
        return GzzioLnF::uiFont (size * (0.85f + fs * 0.15f), bold);
    };
    const auto secondary = Palette::accentOn (Palette::inkSoft, Palette::panel);
    const bool show = hold > 0;
    const bool inTune = hasPitch && std::abs (dispCents) <= 5.0f;
    const auto stateColour = Palette::accentOn (inTune ? Palette::green : Palette::yellowDk, Palette::panel);
    auto body = full.reduced (12.0f).withTrimmedTop ((float) controlHeight() - 4.0f);
    if (body.getHeight() < 45.0f) return;

    // Meter labels occupy their own bottom row. They never reduce the main
    // note to a tiny caption, and never sit behind a pitch trace.
    auto meters = body.getHeight() >= 135.0f ? body.removeFromBottom (30.0f)
                                           : juce::Rectangle<float>();
    auto gauge = body.getHeight() >= 103.0f ? body.removeFromBottom (38.0f)
                                          : juce::Rectangle<float>();
    auto history = juce::Rectangle<float>();
    auto reading = body;
    if (reading.getWidth() >= 680.0f)
    {
        history = reading.removeFromRight (reading.getWidth() - 390.0f).reduced (8.0f, 4.0f);
        reading.removeFromRight (12.0f);
    }
    else if (reading.getHeight() >= 132.0f)
        history = reading.removeFromBottom (reading.getHeight() - 84.0f).reduced (0.0f, 5.0f);

    const float noteWidth = juce::jlimit (95.0f, 128.0f, reading.getWidth() * 0.30f);
    auto note = reading.removeFromLeft (noteWidth).reduced (0.0f, 3.0f);
    g.setColour (inTune ? Palette::green.withAlpha (0.14f) : Palette::panel2);
    g.fillRoundedRectangle (note, 10.0f);
    auto noteText = note.withTrimmedBottom (21.0f);
    g.setColour (Palette::ink);
    g.setFont (font (juce::jlimit (28.0f, 48.0f, noteText.getHeight() * 0.72f), true));
    g.drawText (show ? noteName : juce::String ("--"), noteText, juce::Justification::centred);
    g.setColour (Palette::accentOn (secondary, Palette::panel2));
    g.setFont (font (16.0f));
    g.drawText (show ? juce::String (dispFreq, 1) + " Hz" : tip::T ("入力待ち", "Listening"),
                note.removeFromBottom (22.0f), juce::Justification::centred);

    reading.removeFromLeft (14.0f);
    auto status = reading.removeFromTop (juce::jmin (34.0f, reading.getHeight() * 0.42f));
    juce::String state = ! show ? (guitarMode ? tip::T ("1本ずつ鳴らす", "Play one string") : tip::T ("声を出してください", "Sing a note"))
                       : ! hasPitch ? tip::T ("もう一度鳴らす", "Play again")
                       : inTune ? tip::T ("ぴったり", "In tune")
                       : dispCents < 0.0f ? tip::T ("低い  ↑ 上げる", "Flat  /  tune up") : tip::T ("高い  ↓ 下げる", "Sharp  /  tune down");
    g.setColour (hasPitch ? stateColour : secondary);
    g.setFont (font (23.0f, true));
    g.drawText (state, status, juce::Justification::centredLeft);
    auto amount = reading.removeFromTop (juce::jmin (32.0f, reading.getHeight()));
    g.setColour (Palette::ink);
    g.setFont (font (24.0f, true));
    g.drawText (show ? ((dispCents >= 0 ? "+" : "") + juce::String (juce::roundToInt (dispCents))
                        + tip::T (" セント", " cents"))
                    : (guitarMode ? tip::T ("標準チューニング", "Standard tuning") : tip::T ("補正前の音程", "Input pitch")),
                amount, juce::Justification::centredLeft);
    if (reading.getHeight() >= 19.0f)
    {
        juce::String detail = tip::T ("中央が正しい音程", "Aim for the centre");
        if (guitarMode)
        {
            const int s = guitarString != 0 ? guitarString : (show ? detectedString : 0);
            if (s > 0)
            {
                const int midi = guitarNotes[s - 1];
                detail = juce::String (7 - s) + tip::T ("弦  目標 ", " string  /  target ")
                       + kNoteNames[midi % 12] + juce::String (midi / 12 - 1);
            }
            else detail = tip::T ("開放弦を1本ずつ鳴らす", "Play one open string at a time");
        }
        else if (rangeChecking || rangeHi >= 0)
            detail = rangeChecking ? tip::range_capturing() : rangeResultText();
        g.setColour (secondary);
        g.setFont (font (16.0f));
        g.drawText (detail, reading, juce::Justification::centredLeft);
    }

    if (! gauge.isEmpty())
    {
        // The scale text and the needle have separate rows; meaning is conveyed
        // by position, signed cents and words, not green/red alone.
        auto labels = gauge.removeFromTop (20.0f);
        g.setColour (secondary);
        g.setFont (font (16.0f, true));
        g.drawText (tip::T ("低い  −50", "Flat  -50"), labels.removeFromLeft (116.0f), juce::Justification::centredLeft);
        g.drawText (tip::T ("+50  高い", "+50  Sharp"), labels.removeFromRight (116.0f), juce::Justification::centredRight);
        g.drawText ("0", labels, juce::Justification::centred);
        auto track = gauge.reduced (8.0f, 5.0f);
        g.setColour (Palette::track);
        g.fillRoundedRectangle (track, 4.0f);
        g.setColour (Palette::accentOn (Palette::green, Palette::track));
        g.fillRoundedRectangle (track.withWidth (track.getWidth() * 0.10f).withCentre (track.getCentre()), 4.0f);
        g.setColour (Palette::ink);
        g.drawVerticalLine ((int) track.getCentreX(), track.getY() - 4.0f, track.getBottom() + 4.0f);
        if (show)
        {
            const float x = track.getCentreX() + juce::jlimit (-50.0f, 50.0f, dispCents) / 100.0f * track.getWidth();
            g.setColour (hasPitch ? stateColour : secondary);
            g.fillEllipse (x - 6.0f, track.getCentreY() - 6.0f, 12.0f, 12.0f);
            g.setColour (Palette::ink);
            g.drawEllipse (x - 6.0f, track.getCentreY() - 6.0f, 12.0f, 12.0f, 1.3f);
        }
    }

    if (history.getWidth() > 150.0f && history.getHeight() >= 40.0f)
    {
        g.setColour (Palette::bgBot);
        g.fillRoundedRectangle (history, 8.0f);
        const bool notes = railMode && ! guitarMode;
        auto plot = history.reduced (8.0f);
        auto axis = plot.removeFromLeft (76.0f);
        if (plot.getHeight() > 24.0f)
        {
            int low = 60, high = 72;
            if (notes && show) { low = lastMidi - 6; high = lastMidi + 6; }
            const auto yFor = [&] (float v)
            {
                return notes ? plot.getBottom() - (v - low) / (high - low) * plot.getHeight()
                             : plot.getCentreY() - v / 50.0f * plot.getHeight() * 0.5f;
            };
            g.setFont (font (15.0f, true));
            g.setColour (Palette::accentOn (secondary, Palette::bgBot));
            if (notes)
            {
                for (int i = 0; i < 3; ++i)
                {
                    const int midi = high - i * 6;
                    const auto label = juce::String (kNoteNames[midi % 12]) + juce::String (midi / 12 - 1);
                    const float y = axis.getY() + (axis.getHeight() - 19.0f) * i * 0.5f;
                    g.drawText (label, juce::Rectangle<float> (axis.getX(), y, axis.getWidth() - 8.0f, 19.0f), juce::Justification::centredRight);
                }
            }
            else
            {
                g.drawText (tip::T ("高い", "Sharp"), axis.withHeight (19.0f), juce::Justification::centredRight);
                g.drawText (tip::T ("低い", "Flat"), axis.withHeight (19.0f).withY (axis.getBottom() - 19.0f), juce::Justification::centredRight);
            }
            g.setColour (Palette::panelLn);
            g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.toNearestInt());
            float px = 0.0f, py = 0.0f;
            bool previous = false;
            for (int i = 0; i < histLen; ++i)
            {
                const int index = (histPos + i) % histLen;
                if (! histValid[index]) { previous = false; continue; }
                const float x = plot.getX() + plot.getWidth() * i / (histLen - 1);
                const float value = notes ? (float) histMidi[index] + histCents[index] * 0.01f
                                          : juce::jlimit (-50.0f, 50.0f, histCents[index]);
                const float y = yFor (value);
                if (previous)
                {
                    const auto colour = Palette::accentOn (std::abs (histCents[index]) <= 5.0f ? Palette::green : Palette::yellowDk, Palette::bgBot);
                    g.setColour (colour.withAlpha (0.35f + 0.65f * i / (histLen - 1)));
                    g.drawLine (px, py, x, y, 2.2f);
                }
                px = x; py = y; previous = true;
            }
        }
    }

    if (! meters.isEmpty())
    {
        struct Meter { const char* name; float value; };
        const auto level = [] (float v) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (v, -60.0f) + 60.0f) / 60.0f); };
        const Meter values[] = { { "入力", level (dispIn) }, { "出力", level (dispOut) },
                                 { "圧縮", -dispGR / 18.0f }, { "歯擦音", dispDS }, { "ノイズ", dispDN } };
        const int numMeters = meters.getWidth() >= 640.0f ? 5 : 2;
        const float width = meters.getWidth() / numMeters;
        for (int i = 0; i < numMeters; ++i)
        {
            auto cell = meters.removeFromLeft (width).reduced (4.0f, 1.0f);
            g.setColour (secondary);
            g.setFont (font (15.0f, true));
            const char* enNames[] = { "In", "Out", "Comp", "De-ess", "Noise" };
            g.drawText (tip::T (values[i].name, enNames[i]), cell.removeFromLeft (58.0f), juce::Justification::centredLeft);
            auto track = cell.withHeight (7.0f).withCentre (cell.getCentre());
            g.setColour (Palette::track);
            g.fillRoundedRectangle (track, 3.0f);
            g.setColour (Palette::accentOn (Palette::green, Palette::track));
            g.fillRoundedRectangle (track.withWidth (track.getWidth() * juce::jlimit (0.0f, 1.0f, values[i].value)), 3.0f);
        }
    }
}

void VocalTuner::resized()
{
    auto bounds = getLocalBounds().reduced (12, 8);
    const int gap = 8, rowHeight = 36;
    auto row = bounds.removeFromTop (rowHeight);
    if (getWidth() >= 740)
    {
        tunerModeBox.setBounds (row.removeFromLeft (160));
        row.removeFromLeft (gap);
        refPitchBox.setBounds (row.removeFromRight (176));
        row.removeFromRight (gap);
        guitarStringBox.setBounds (row);
        if (! guitarMode)
        {
            const int width = juce::jmin (170, (row.getWidth() - gap) / 2);
            railToggle.setBounds (row.removeFromLeft (width));
            row.removeFromLeft (gap);
            rangeButton.setBounds (row.removeFromLeft (width));
        }
    }
    else
    {
        refPitchBox.setBounds (row.removeFromRight (juce::jmin (190, row.getWidth() / 2)));
        row.removeFromRight (gap);
        tunerModeBox.setBounds (row);
        bounds.removeFromTop (gap);
        row = bounds.removeFromTop (rowHeight);
        guitarStringBox.setBounds (row);
        if (! guitarMode)
        {
            railToggle.setBounds (row.removeFromLeft ((row.getWidth() - gap) / 2));
            row.removeFromLeft (gap);
            rangeButton.setBounds (row);
        }
    }
}

//==============================================================================
// EQ graph
//==============================================================================
void EQGraph::updateSpectrum()
{
    const int N = VocalGzzioProcessor::analyzerSize;
    proc.readAnalyzerBuffer (timeData);

    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (timeData.begin(), timeData.end(), fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), (size_t) N);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const float norm = 4.0f / (float) N;   // hann coherent gain + bin normalisation (approx)
    for (int k = 0; k <= N / 2; ++k)
    {
        const float db = 20.0f * std::log10 (juce::jmax (fftData[(size_t) k] * norm, 1.0e-6f));
        float& s = specDb[(size_t) k];
        s += (db > s ? 0.55f : 0.16f) * (db - s);   // fast rise, slow fall
    }
}

void EQGraph::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    g.setColour (Palette::bgTop);
    g.fillRoundedRectangle (full, 8.0f);

    // inner plot area (leave a margin for axis labels)
    plot = full.reduced (10.0f).withTrimmedBottom (18.0f).withTrimmedLeft (22.0f);

    auto lf = GzzioLnF::uiFont;

    // ---- v1.5.0: the grid + axis labels never change at a given size, so they
    //      are rendered once into an image (rebuilt on resize). This removes a
    //      few dozen text-layout calls from every animation frame. ----
    if (! gridImage.isValid())
    {
        gridImage = juce::Image (juce::Image::ARGB,
                                 juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
        juce::Graphics gg (gridImage);

        // vertical grid: decade + 1-2-5 log lines
        const float decadeMarks[] = { 20, 30, 50, 100, 200, 300, 500,
                                      1000, 2000, 3000, 5000, 10000, 20000 };
        gg.setFont (lf (14.0f, false));
        for (float hz : decadeMarks)
        {
            const float gx = freqToX (hz);
            const bool major = (hz == 100 || hz == 1000 || hz == 10000);
            gg.setColour (Palette::panelLn.withAlpha (major ? 0.7f : 0.35f));
            gg.fillRect (gx, plot.getY(), 1.0f, plot.getHeight());
            if (major)
            {
                gg.setColour (Palette::inkSoft.withAlpha (0.9f));
                const juce::String lab = hz >= 1000.0f ? juce::String ((int) (hz / 1000)) + "k"
                                                       : juce::String ((int) hz);
                gg.drawText (lab, juce::Rectangle<float> (gx - 16, plot.getBottom() + 1, 32, 16),
                             juce::Justification::centred);
            }
        }

        // horizontal grid: dB lines at 0 / +-6 / +-12 / +-18
        for (int db = -18; db <= 18; db += 6)
        {
            const float gy = dbToY ((float) db);
            const bool zero = (db == 0);
            gg.setColour (zero ? Palette::panelLn.withAlpha (0.9f)
                               : Palette::panelLn.withAlpha (0.30f));
            gg.fillRect (plot.getX(), gy, plot.getWidth(), zero ? 1.4f : 1.0f);
            gg.setColour (Palette::inkSoft.withAlpha (0.8f));
            gg.setFont (lf (14.0f, false));
            gg.drawText ((db > 0 ? "+" : "") + juce::String (db),
                         juce::Rectangle<float> (full.getX() + 1, gy - 8, 21, 16),
                         juce::Justification::centredLeft);
        }
    }
    g.drawImageAt (gridImage, 0, 0);

    double sr = proc.getSampleRate();
    if (sr < 8000.0) sr = 48000.0;

    const int px = juce::jmax (2, (int) plot.getWidth());

    // ---- output spectrum (behind curves) ----
    {
        const float floorDb = -78.0f;
        juce::Path line;
        for (int i = 0; i <= px; ++i)
        {
            const float x   = plot.getX() + (float) i;
            const float hz  = xToFreq (x);
            const float bin = (float) (hz / (sr * 0.5) * (double) (VocalGzzioProcessor::analyzerSize / 2));
            const int   b0  = juce::jlimit (0, (int) specDb.size() - 2, (int) bin);
            const float fr  = juce::jlimit (0.0f, 1.0f, bin - (float) b0);
            const float db  = specDb[(size_t) b0] * (1.0f - fr) + specDb[(size_t) b0 + 1] * fr;
            const float t   = juce::jlimit (0.0f, 1.0f, (db - floorDb) / (0.0f - floorDb));
            const float yy  = plot.getBottom() - t * plot.getHeight();
            if (i == 0) line.startNewSubPath (x, yy); else line.lineTo (x, yy);
        }
        juce::Path fillP (line);
        fillP.lineTo (plot.getRight(), plot.getBottom());
        fillP.lineTo (plot.getX(),     plot.getBottom());
        fillP.closeSubPath();

        // v1.4.0 stream-friendly look: rainbow spectrum, warm lows -> cool highs,
        // drawn as soft glow + bright core so it reads well on a capture.
        juce::ColourGradient rain (juce::Colour (0xffff6b5e), plot.getX(),     0.0f,
                                   juce::Colour (0xff8b6bff), plot.getRight(), 0.0f, false);
        rain.addColour (0.30, juce::Colour (0xffffc94d));
        rain.addColour (0.52, juce::Colour (0xff4ade9e));
        rain.addColour (0.75, juce::Colour (0xff58b6ff));

        auto fillGrad = rain; fillGrad.multiplyOpacity (0.26f);
        g.setGradientFill (fillGrad);
        g.fillPath (fillP);

        auto glowGrad = rain; glowGrad.multiplyOpacity (0.28f);
        g.setGradientFill (glowGrad);
        g.strokePath (line, juce::PathStrokeType (4.6f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        auto coreGrad = rain; coreGrad.multiplyOpacity (0.95f);
        g.setGradientFill (coreGrad);
        g.strokePath (line, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    // ---- EQ curves: static corrective chain + current smart-EQ cuts ----
    using Co = juce::dsp::IIR::Coefficients<float>;
    auto pv = [this] (const char* id)
    {
        auto* r = proc.apvts.getRawParameterValue (id);
        return r ? r->load() : 0.0f;
    };
    auto rawGain = [] (float db) { return std::pow (10.0f, db / 20.0f); };

    Co::Ptr chain[5] =
    {
        Co::makeHighPass   (sr, juce::jmax (20.0f, pv ("lowcut")), 0.707f),
        Co::makePeakFilter (sr, 300.0,   1.0f,   rawGain (pv ("mud"))),
        Co::makePeakFilter (sr, 3200.0,  1.2f,   rawGain (pv ("harsh"))),
        Co::makePeakFilter (sr, 4200.0,  0.9f,   rawGain (pv ("presence"))),
        Co::makeHighShelf  (sr, 11000.0, 0.707f, rawGain (pv ("air")))
    };

    const bool  eqOn = pv ("seq_on") > 0.5f;
    const int   nb   = juce::jmin (proc.getSeqBandCount(), 6);
    Co::Ptr bandCo[6];
    float   bandCut[6] = {};
    float   bandHz[6]  = {};
    int     nAct = 0;
    if (eqOn)
        for (int b = 0; b < nb; ++b)
        {
            bandCut[b] = proc.getSeqBandCutDb (b);
            bandHz[b]  = juce::jlimit (30.0f, 18000.0f, proc.getSeqBandFreq (b));
            if (bandCut[b] > 0.05f)
            {
                const float qb = juce::jmax (0.3f, proc.getSeqBandQ (b));
                bandCo[b] = Co::makePeakFilter (sr, bandHz[b], qb,
                                                juce::Decibels::decibelsToGain (-bandCut[b]));
                ++nAct;
            }
        }

    std::vector<float> statDb ((size_t) px + 1), liveDb ((size_t) px + 1);
    for (int i = 0; i <= px; ++i)
    {
        const double hz = (double) xToFreq (plot.getX() + (float) i);
        double m = 1.0;
        for (auto& c : chain)
            m *= c->getMagnitudeForFrequency (hz, sr);
        const float s = 20.0f * (float) std::log10 (juce::jmax (1.0e-4, m));
        double ma = 1.0;
        for (int b = 0; b < nb; ++b)
            if (bandCo[b] != nullptr)
                ma *= bandCo[b]->getMagnitudeForFrequency (hz, sr);
        statDb[(size_t) i] = s;
        liveDb[(size_t) i] = s + 20.0f * (float) std::log10 (juce::jmax (1.0e-4, ma));
    }

    auto curveOf = [&] (const std::vector<float>& v)
    {
        juce::Path p;
        for (int i = 0; i <= px; ++i)
        {
            const float x = plot.getX() + (float) i;
            const float y = dbToY (v[(size_t) i]);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };

    // cut region between the base curve and the live curve
    if (nAct > 0)
    {
        juce::Path region;
        region.startNewSubPath (plot.getX(), dbToY (statDb[0]));
        for (int i = 1; i <= px; ++i)
            region.lineTo (plot.getX() + (float) i, dbToY (statDb[(size_t) i]));
        for (int i = px; i >= 0; --i)
            region.lineTo (plot.getX() + (float) i, dbToY (liveDb[(size_t) i]));
        region.closeSubPath();
        g.setColour (Palette::salmon.withAlpha (0.30f));
        g.fillPath (region);
    }

    auto base = curveOf (statDb);
    g.setColour (Palette::blue.withAlpha (nAct > 0 ? 0.75f : 1.0f));
    g.strokePath (base, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    if (nAct > 0)
    {
        auto live = curveOf (liveDb);
        g.setColour (Palette::yellow);
        g.strokePath (live, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // band markers + cut amount labels
        g.setFont (lf (14.0f, true));
        for (int b = 0; b < nb; ++b)
            if (bandCo[b] != nullptr)
            {
                const float bx = freqToX (bandHz[b]);
                const int   ii = juce::jlimit (0, px, (int) (bx - plot.getX()));
                const float by = dbToY (liveDb[(size_t) ii]);
                g.setColour (Palette::salmon);
                g.fillEllipse (bx - 3.5f, by - 3.5f, 7.0f, 7.0f);
                if (bandCut[b] > 0.8f)
                    g.drawText ("-" + juce::String (bandCut[b], 1),
                                juce::Rectangle<float> (bx - 22, by + 5, 44, 12),
                                juce::Justification::centred);
            }
    }

    // legend (top-left inside the plot)
    {
        struct Item { juce::Colour c; const char* utf8; };
        const Item items[3] =
        {
            { Palette::green,  "\xe3\x82\xb9\xe3\x83\x9a\xe3\x82\xaf\xe3\x83\x88\xe3\x83\xa9\xe3\x83\xa0" },   // spectrum
            { Palette::blue,   "\x45\x51\xe3\x82\xab\xe3\x83\xbc\xe3\x83\x96" },                               // EQ curve
            { Palette::yellow, "\xe8\x87\xaa\xe5\x8b\x95\x45\x51\xe5\xbe\x8c" }                                // after auto EQ
        };
        float lx = plot.getX() + 8.0f;
        const float ly = plot.getY() + 6.0f;
        g.setFont (lf (14.5f, true));
        int legendIndex = 0;
        const char* englishLegend[] = { "Spectrum", "EQ curve", "After auto EQ" };
        for (auto& it : items)
        {
            g.setColour (it.c);
            g.fillRoundedRectangle (lx, ly + 3.0f, 12.0f, 12.0f, 3.0f);
            const auto txt = tip::T (it.utf8, englishLegend[legendIndex++]);
            const float tw = (float) g.getCurrentFont().getStringWidth (txt) + 6.0f;
            g.setColour (Palette::ink.withAlpha (0.92f));
            g.drawText (txt, juce::Rectangle<float> (lx + 17.0f, ly, tw, 18.0f),
                        juce::Justification::centredLeft);
            lx += 17.0f + tw + 14.0f;
        }
    }

    // ---- v1.4.0 F6-style drag handles for the 3 manual bands ----
    if (manualEditActive())
    {
        g.setFont (lf (14.0f, true));
        for (int b = 0; b < 3; ++b)
        {
            const auto p   = bandNodePos (b);
            const bool hot = (b == dragBand || b == hoverBand);
            const float r  = hot ? 9.0f : 7.0f;
            g.setColour (Palette::bgTop.withAlpha (0.85f));
            g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
            g.setColour (hot ? Palette::yellow : Palette::yellow.withAlpha (0.85f));
            g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, hot ? 2.4f : 1.8f);
            g.drawText (juce::String (b + 1),
                        juce::Rectangle<float> (p.x - 8.0f, p.y - 7.0f, 16.0f, 14.0f),
                        juce::Justification::centred);

            if (b == dragBand)   // live readout while dragging
            {
                const float pf = pv (b == 0 ? "seq_f1" : b == 1 ? "seq_f2" : "seq_f3");
                const float pd = pv (b == 0 ? "seq_d1" : b == 1 ? "seq_d2" : "seq_d3");
                const float pq = pv (b == 0 ? "seq_q1" : b == 1 ? "seq_q2" : "seq_q3");
                const juce::String txt = juce::String ((int) pf) + " Hz  -"
                                       + juce::String (pd, 1) + " dB  Q " + juce::String (pq, 1);
                g.setColour (Palette::ink);
                g.drawText (txt, juce::Rectangle<float> (p.x - 80.0f, p.y - 27.0f, 160.0f, 14.0f),
                            juce::Justification::centred);
            }
        }
        g.setColour (Palette::inkSoft.withAlpha (0.75f));
        g.setFont (lf (14.0f, false));
        g.drawText (tip::seq_drag_hint(),
                    juce::Rectangle<float> (plot.getRight() - 320.0f, plot.getY() + 4.0f, 312.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    // frame
    g.setColour (Palette::panelLn);
    g.drawRoundedRectangle (full, 8.0f, 1.0f);
}

//==============================================================================
// v1.4.0 F6-style manual band editing on the EQ graph
//==============================================================================
namespace
{
    const char* seqFreqIds[3]  = { "seq_f1", "seq_f2", "seq_f3" };
    const char* seqDepthIds[3] = { "seq_d1", "seq_d2", "seq_d3" };
    const char* seqQIds[3]     = { "seq_q1", "seq_q2", "seq_q3" };
}

bool EQGraph::manualEditActive() const
{
    auto* on = proc.apvts.getRawParameterValue ("seq_on");
    auto* md = proc.apvts.getRawParameterValue ("seq_mode");
    return on != nullptr && md != nullptr
        && on->load() > 0.5f && (int) md->load() == 1;
}

juce::Point<float> EQGraph::bandNodePos (int b) const
{
    const float f = proc.apvts.getRawParameterValue (seqFreqIds [b])->load();
    const float d = proc.apvts.getRawParameterValue (seqDepthIds[b])->load();
    return { freqToX (f), dbToY (-d) };
}

int EQGraph::hitTestBand (juce::Point<float> p) const
{
    if (! manualEditActive())
        return -1;
    for (int b = 0; b < 3; ++b)
        if (bandNodePos (b).getDistanceFrom (p) < 12.0f)
            return b;
    return -1;
}

void EQGraph::mouseDown (const juce::MouseEvent& e)
{
    dragBand = hitTestBand (e.position);
    if (dragBand >= 0)
    {
        if (auto* pf = proc.apvts.getParameter (seqFreqIds [dragBand])) pf->beginChangeGesture();
        if (auto* pd = proc.apvts.getParameter (seqDepthIds[dragBand])) pd->beginChangeGesture();
        repaint();
    }
}

void EQGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0)
        return;
    const float f = juce::jlimit (120.0f, 8000.0f, xToFreq (e.position.x));
    const float d = juce::jlimit (0.0f, 15.0f, -yToDb (e.position.y));
    if (auto* pf = proc.apvts.getParameter (seqFreqIds [dragBand]))
        pf->setValueNotifyingHost (proc.apvts.getParameterRange (seqFreqIds [dragBand]).convertTo0to1 (f));
    if (auto* pd = proc.apvts.getParameter (seqDepthIds[dragBand]))
        pd->setValueNotifyingHost (proc.apvts.getParameterRange (seqDepthIds[dragBand]).convertTo0to1 (d));
}

void EQGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0)
    {
        if (auto* pf = proc.apvts.getParameter (seqFreqIds [dragBand])) pf->endChangeGesture();
        if (auto* pd = proc.apvts.getParameter (seqDepthIds[dragBand])) pd->endChangeGesture();
    }
    dragBand = -1;
    repaint();
}

void EQGraph::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTestBand (e.position);
    if (h != hoverBand)
    {
        hoverBand = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::UpDownLeftRightResizeCursor
                               : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EQGraph::mouseExit (const juce::MouseEvent&)
{
    hoverBand = -1;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void EQGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int b = dragBand >= 0 ? dragBand : hitTestBand (e.position);
    if (b < 0)
        return;
    const float cur = proc.apvts.getRawParameterValue (seqQIds[b])->load();
    const float q   = juce::jlimit (0.5f, 8.0f, cur * std::pow (2.0f, w.deltaY));
    if (auto* pq = proc.apvts.getParameter (seqQIds[b]))
        pq->setValueNotifyingHost (proc.apvts.getParameterRange (seqQIds[b]).convertTo0to1 (q));
    repaint();
}

//==============================================================================
// Content
//==============================================================================
// v1.9.0: mic presets carry a generic English name plus a Japanese one; pick by language.
static juce::String voiceItemName (int i)
{
    const auto& v = gzzio::kVoicePresets[i];
    return juce::String::fromUTF8 (tip::english ? v.nameEn : v.name);
}
static juce::String eqItemName (int i)
{
    const auto& e = gzzio::kEqPresets[i];
    return juce::String::fromUTF8 (tip::english ? e.nameEn : e.name);
}
static juce::String charItemName (int i)
{
    const auto& c = gzzio::kCharPresets[i];
    return juce::String::fromUTF8 (tip::english ? c.nameEn : c.name);
}

static juce::String micItemName (int i)
{
    const auto& m = gzzio::kMicPresets[i];
    return juce::String::fromUTF8 (tip::english ? m.name : m.nameJa);
}

// (Re)builds the mic combo. Called at construction and whenever the language flips.
static void fillMicBox (juce::ComboBox& box)
{
    const int keep = box.getSelectedId();
    box.clear (juce::dontSendNotification);
    box.addSectionHeading (tip::dyn_head());
    for (int i = 0;  i < 5;  ++i) box.addItem (micItemName (i), i + 1);
    for (int i = 10; i < 15; ++i) box.addItem (micItemName (i), i + 1);
    box.addSectionHeading (tip::cond_head());
    for (int i = 5;  i < 10; ++i) box.addItem (micItemName (i), i + 1);
    for (int i = 15; i < 20; ++i) box.addItem (micItemName (i), i + 1);
    if (keep > 0) box.setSelectedId (keep, juce::dontSendNotification);
}

// v1.9.0: bilingual label for an auto-tune scale index (matches gz::scale::* order).
static juce::String scaleItemName (int i)
{
    switch (i)
    {
        case 0:  return tip::at_sc0();
        case 1:  return tip::at_sc1();
        case 2:  return tip::at_sc2();
        case 3:  return tip::at_sc3();
        case 4:  return tip::at_sc4();
        case 5:  return tip::at_sc5();
        case 6:  return tip::at_sc6();
        case 7:  return tip::at_sc7();
        default: return tip::at_sc8();
    }
}

//==============================================================================
// v2.1.0 MIDIスイッチ設定パネル(「MIDI設定」ボタンのCallOutBoxの中身)。
// 8スロット × [アクション / 割当表示 / 学習 / 解除]。学習はプロセッサ側の
// midiLearnArmed を立てるだけで、実際の取り込みはオーディオスレッドが行う。
class MidiMapPanel : public juce::Component, private juce::Timer
{
public:
    explicit MidiMapPanel (VocalGzzioProcessor& p) : processor (p)
    {
        // CallOutBox は独立したウィンドウなので、プラグイン本体の見た目を
        // 引き継がない。自前のLookAndFeelを持たせてテーマ配色をそのまま使う
        // (エディタ側のLnFを借りると、閉じる順によっては参照が切れて危険)。
        ownLnf.refreshPaletteColours();
        setLookAndFeel (&ownLnf);

        for (int s = 0; s < VocalGzzioProcessor::kMidiSlots; ++s)
        {
            auto& r = rows[(size_t) s];
            for (int a = 0; a < VocalGzzioProcessor::maCount; ++a)
                r.action.addItem (tip::midi_action_name (a), a + 1);
            r.action.setSelectedId (processor.midiMap[s].act.load() + 1, juce::dontSendNotification);
            r.action.onChange = [this, s]
            {
                processor.midiMap[s].act.store (rows[(size_t) s].action.getSelectedId() - 1);
                processor.markStateDirtyPublic();
            };
            addAndMakeVisible (r.action);

            r.assign.setJustificationType (juce::Justification::centred);
            r.assign.setColour (juce::Label::textColourId, Palette::ink);
            addAndMakeVisible (r.assign);

            r.learn.setClickingTogglesState (false);
            r.learn.setColour (juce::TextButton::buttonOnColourId, Palette::yellow);
            r.learn.onClick = [this, s]
            {
                const bool arming = processor.midiLearnArmed.load() != s;
                processor.midiLearnArmed.store (arming ? s : -1);
                refresh();
            };
            addAndMakeVisible (r.learn);

            r.clear.onClick = [this, s]
            {
                processor.midiLearnArmed.store (-1);
                processor.clearMidiSlot (s);
                refresh();
            };
            addAndMakeVisible (r.clear);
        }
        hint.setJustificationType (juce::Justification::centredLeft);
        hint.setColour (juce::Label::textColourId, Palette::inkSoft);
        addAndMakeVisible (hint);
        relabel();
        setSize (520, VocalGzzioProcessor::kMidiSlots * 32 + 40);
        refresh();
        startTimerHz (8);
    }

    ~MidiMapPanel() override
    {
        processor.midiLearnArmed.store (-1);   // 学習待ちを残さない
        setLookAndFeel (nullptr);
    }

    // CallOutBox の枠は JUCE 既定の見た目で描かれる(別ウィンドウなのでプラグイン
    // 本体の LookAndFeel を継承しない)。中身をこちらで塗りつぶし、テーマの地色に
    // 合わせる。これで淡いテーマでも文字が沈まない。
    void paint (juce::Graphics& g) override
    {
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (Palette::panelLn);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (6);
        hint.setBounds (b.removeFromBottom (24));
        for (auto& r : rows)
        {
            auto row = b.removeFromTop (32).reduced (0, 3);
            r.action.setBounds (row.removeFromLeft (196));
            row.removeFromLeft (6);
            r.clear.setBounds (row.removeFromRight (58));
            row.removeFromRight (6);
            r.learn.setBounds (row.removeFromRight (84));
            row.removeFromRight (6);
            r.assign.setBounds (row);
        }
    }

private:
    void timerCallback() override
    {
        if (lastDirty != processor.midiUiDirty.load()
            || lastArmed != processor.midiLearnArmed.load())
            refresh();
    }

    void relabel()
    {
        for (auto& r : rows) { r.learn.setButtonText (tip::midi_learn()); r.clear.setButtonText (tip::midi_clear()); }
        hint.setText (tip::midi_hint(), juce::dontSendNotification);
    }

    void refresh()
    {
        lastDirty = processor.midiUiDirty.load();
        lastArmed = processor.midiLearnArmed.load();
        for (int s = 0; s < VocalGzzioProcessor::kMidiSlots; ++s)
        {
            auto& r = rows[(size_t) s];
            const auto& m = processor.midiMap[s];
            juce::String txt;
            if      (m.type.load() == 1) txt = "Note " + juce::MidiMessage::getMidiNoteName (m.num.load(), true, true, 4);
            else if (m.type.load() == 2) txt = "CC "   + juce::String (m.num.load());
            else                         txt = tip::midi_unassigned();
            r.assign.setText (txt, juce::dontSendNotification);
            r.learn.setButtonText (lastArmed == s ? tip::midi_wait() : tip::midi_learn());
            r.learn.setToggleState (lastArmed == s, juce::dontSendNotification);
            if (r.action.getSelectedId() != m.act.load() + 1)
                r.action.setSelectedId (m.act.load() + 1, juce::dontSendNotification);
        }
        repaint();
    }

    struct Row { juce::ComboBox action; juce::Label assign; juce::TextButton learn, clear; };
    GzzioLnF ownLnf;                       // ← 先頭に置く(子より後に破棄されるように)
    VocalGzzioProcessor& processor;
    Row rows[VocalGzzioProcessor::kMidiSlots];
    juce::Label hint;
    int lastDirty = -1, lastArmed = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiMapPanel)
};


//==============================================================================
// v2.10.0「点検」パネル — 音を作らない4つをここに集めた。
//   #73 ゼロ遅延の自己証明 / #76 名前ごとの設定 / #74 手がかりの書き出し / #75 使われ方
// どれもファイル入出力を伴うので、必ず画面(メッセージ)スレッドから呼ぶ。
//==============================================================================
class CheckupPanel : public juce::Component,
                     private juce::Timer
{
public:
    explicit CheckupPanel (VocalGzzioProcessor& p) : processor (p)
    {
        setLookAndFeel (&lnf);

        auto head = [this] (juce::Label& l, const juce::String& s)
        {
            l.setText (s, juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, Palette::ink);
            l.setFont (GzzioLnF::uiFont (14.0f, true));
            addAndMakeVisible (l);
        };
        auto note = [this] (juce::Label& l, const juce::String& s)
        {
            l.setText (s, juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, Palette::inkSoft);
            l.setFont (GzzioLnF::uiFont (14.0f, false));
            l.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (l);
        };

        // ---- #73 ゼロ遅延の自己証明 ----
        head (stTitle, tip::st_title());
        note (stNote,  tip::st_note());
        stRun.setButtonText (tip::st_run());
        stRun.setColour (juce::TextButton::buttonColourId, Palette::green.withAlpha (0.18f));
        stRun.onClick = [this] { processor.requestLatencySelfTest(); refresh(); };
        addAndMakeVisible (stRun);
        stResult.setColour (juce::Label::textColourId, Palette::ink);
        stResult.setFont (GzzioLnF::uiFont (14.0f, true));
        addAndMakeVisible (stResult);

        // ---- #76 名前ごとの設定 ----
        head (rcTitle, tip::rc_title());
        note (rcNote,  tip::rc_note());
        rcName.setMultiLine (false);
        rcName.setTextToShowWhenEmpty (juce::String ("..."), Palette::inkSoft);
        addAndMakeVisible (rcName);
        rcSave.setButtonText (tip::rc_save());
        rcSave.setColour (juce::TextButton::buttonColourId, Palette::yellow.withAlpha (0.25f));
        rcSave.onClick = [this]
        {
            const auto n = currentName();
            if (n.isEmpty()) { rcState.setText (juce::String ("?"), juce::dontSendNotification); return; }
            if (auto xml = processor.apvts.copyState().createXml())
                gz::checkup::Recall::store (n, *xml);
            refresh();
        };
        addAndMakeVisible (rcSave);
        rcLoad.setButtonText (tip::rc_load());
        rcLoad.setColour (juce::TextButton::buttonColourId, Palette::ice.withAlpha (0.25f));
        rcLoad.onClick = [this]
        {
            if (auto xml = gz::checkup::Recall::fetch (currentName()))
                processor.apvts.replaceState (juce::ValueTree::fromXml (*xml));
            refresh();
        };
        addAndMakeVisible (rcLoad);
        rcState.setColour (juce::Label::textColourId, Palette::inkSoft);
        rcState.setFont (GzzioLnF::uiFont (14.0f, false));
        addAndMakeVisible (rcState);

        // ---- #74 手がかりの書き出し ----
        head (rpTitle, tip::rp_title());
        note (rpNote,  tip::rp_note());
        rpSave.setButtonText (tip::rp_save());
        rpSave.setColour (juce::TextButton::buttonColourId, Palette::salmon.withAlpha (0.22f));
        rpSave.onClick = [this] { exportReport(); };
        addAndMakeVisible (rpSave);

        // ---- #75 使われ方 ----
        head (usTitle, tip::us_title());
        usBody.setColour (juce::Label::textColourId, Palette::inkSoft);
        usBody.setFont (GzzioLnF::uiFont (14.0f, false));
        usBody.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (usBody);

        setSize (560, 430);
        refresh();
        startTimerHz (4);
    }

    ~CheckupPanel() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (Palette::panelLn);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (12);
        auto section = [&b] (juce::Label& title, int bodyH)
        {
            title.setBounds (b.removeFromTop (20));
            return b.removeFromTop (bodyH);
        };

        {   auto r = section (stTitle, 52);
            auto row = r.removeFromTop (26);
            stRun.setBounds (row.removeFromLeft (96));
            row.removeFromLeft (8);
            stResult.setBounds (row);
            stNote.setBounds (r); }
        b.removeFromTop (10);

        {   auto r = section (rcTitle, 60);
            auto row = r.removeFromTop (26);
            rcName.setBounds (row.removeFromLeft (200));
            row.removeFromLeft (8);
            rcSave.setBounds (row.removeFromLeft (86));
            row.removeFromLeft (6);
            rcLoad.setBounds (row.removeFromLeft (92));
            row.removeFromLeft (8);
            rcState.setBounds (row);
            rcNote.setBounds (r); }
        b.removeFromTop (10);

        {   auto r = section (rpTitle, 52);
            auto row = r.removeFromTop (26);
            rpSave.setBounds (row.removeFromLeft (160));
            rpNote.setBounds (r); }
        b.removeFromTop (10);

        usTitle.setBounds (b.removeFromTop (20));
        usBody.setBounds (b);
    }

private:
    juce::String currentName() const
    {
        const auto typed = rcName.getText().trim();
        return typed.isNotEmpty() ? typed : processor.getHostTrackName().trim();
    }

    void exportReport()
    {
        const auto text = processor.buildReportText();
        chooser = std::make_unique<juce::FileChooser> (
            "VocalGzzio report",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                .getChildFile ("VocalGzzio_report.txt"), "*.txt");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::warnAboutOverwriting,
            [text] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f != juce::File()) f.replaceWithText (text);
            });
    }

    void refresh()
    {
        // --- 自己証明の結果 ---
        const int declared = processor.addedLatencySamples();
        const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
        if (processor.isSelfTestRunning())
            stResult.setText (juce::String ("...."), juce::dontSendNotification);
        else
        {
            const int m = processor.getSelfTestMeasured();
            if (m < 0)
                stResult.setText (juce::String ("-"), juce::dontSendNotification);
            else
                stResult.setText (juce::String (declared) + " / " + juce::String (m)
                                    + " smp   (" + juce::String (m * 1000.0 / sr, 2) + " ms)",
                                  juce::dontSendNotification);
        }

        // --- 名前の状態 ---
        const auto host = processor.getHostTrackName().trim();
        if (rcName.getText().isEmpty() && host.isNotEmpty())
            rcName.setText (host, juce::dontSendNotification);
        const auto n = currentName();
        rcState.setText (n.isEmpty() ? juce::String()
                         : (gz::checkup::Recall::fetch (n) != nullptr ? juce::String ("OK")
                                                                     : juce::String ("--")),
                         juce::dontSendNotification);

        // --- 使われ方 ---
        auto& u = processor.usage();
        juce::String s;
        s << "launches " << u.getLaunches() << "\n";
        const auto keys = u.all().getAllKeys();
        int shown = 0;
        for (const auto& k : keys)
        {
            const double sec = u.all()[k].getDoubleValue();
            if (sec < 30.0) continue;
            s << k << "  " << juce::String (sec / 60.0, 1) << " min\n";
            if (++shown >= 6) break;
        }
        usBody.setText (s, juce::dontSendNotification);
    }

    void timerCallback() override { refresh(); }

    VocalGzzioProcessor& processor;
    GzzioLnF lnf;
    juce::Label stTitle, stNote, stResult, rcTitle, rcNote, rcState, rpTitle, rpNote, usTitle, usBody;
    juce::TextButton stRun, rcSave, rcLoad, rpSave;
    juce::TextEditor rcName;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
// v2.2.0 配信出力パネル(単体起動版のみ)。処理後の音を「もう1つの出力先」へ
// 同時に流し、OBS 等で拾えるようにする。仮想オーディオデバイス(VB-CABLE 等)を
// 選ぶ想定。ASIO は他アプリと排他になりやすいので一覧に出していない。
class StreamOutPanel : public juce::Component, private juce::Timer
{
public:
    explicit StreamOutPanel (VocalGzzioProcessor& p) : processor (p)
    {
        ownLnf.refreshPaletteColours();
        setLookAndFeel (&ownLnf);

        onButton.setClickingTogglesState (true);
        onButton.setButtonText (tip::so_on());
        onButton.setColour (juce::TextButton::buttonOnColourId, Palette::green);
        onButton.setToggleState (processor.getStreamOut().isRunning(), juce::dontSendNotification);
        onButton.onClick = [this] { apply(); };
        addAndMakeVisible (onButton);

        devLabel.setText (tip::so_dev(), juce::dontSendNotification);
        devLabel.setColour (juce::Label::textColourId, Palette::ink);
        addAndMakeVisible (devLabel);

        auto names = processor.getStreamOut().getOutputDeviceNames();
        for (int i = 0; i < names.size(); ++i) devBox.addItem (names[i], i + 1);
        if (names.isEmpty()) { devBox.addItem (tip::so_none(), 1); devBox.setEnabled (false); onButton.setEnabled (false); }
        {
            const auto wanted = processor.getStreamDeviceWanted();
            const int idx = names.indexOf (wanted);
            devBox.setSelectedId (idx >= 0 ? idx + 1 : 1, juce::dontSendNotification);
        }
        devBox.onChange = [this] { if (onButton.getToggleState()) apply(); };
        addAndMakeVisible (devBox);

        status.setJustificationType (juce::Justification::centredLeft);
        status.setColour (juce::Label::textColourId, Palette::inkSoft);
        addAndMakeVisible (status);

        hint.setText (tip::so_hint(), juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, Palette::inkSoft);
        addAndMakeVisible (hint);

        setSize (520, 150);
        refresh();
        startTimerHz (4);
    }

    ~StreamOutPanel() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        g.setColour (Palette::panelLn);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (10);
        auto row1 = b.removeFromTop (30);
        onButton.setBounds (row1.removeFromLeft (150));
        row1.removeFromLeft (10);
        status.setBounds (row1);
        b.removeFromTop (8);
        auto row2 = b.removeFromTop (28);
        devLabel.setBounds (row2.removeFromLeft (70));
        devBox.setBounds (row2);
        b.removeFromTop (6);
        hint.setBounds (b);
    }

private:
    void apply()
    {
        processor.setStreamOutput (onButton.getToggleState(), devBox.getText());
        refresh();
    }

    void timerCallback() override { refresh(); }

    void refresh()
    {
        auto& so = processor.getStreamOut();
        juce::String t;
        if (so.isRunning())
            t = tip::so_run_txt() + "  (" + juce::String ((int) so.getDeviceSampleRate()) + " Hz)";
        else if (onButton.getToggleState())
            t = tip::so_fail_txt() + "  " + so.getLastError();
        else
            t = tip::so_off_txt();
        if (status.getText() != t) status.setText (t, juce::dontSendNotification);
        if (onButton.getToggleState() != so.isRunning() && ! onButton.getToggleState())
            onButton.setToggleState (so.isRunning(), juce::dontSendNotification);
    }

    GzzioLnF ownLnf;                        // 子より後に破棄されるよう先頭に置く
    VocalGzzioProcessor& processor;
    juce::TextButton onButton;
    juce::Label devLabel, status, hint;
    juce::ComboBox devBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StreamOutPanel)
};

//==============================================================================
// v3.0-c 「音のとおり道」— 常設のカード列
//
//  寸法はすべて 100%表示のときの実寸(px)で書き、ユーザーの文字% (scale) を掛ける。
//  固定幅は書かない。文字の実測から箱の大きさを決める（v2.12.0 でヘッダを
//  作り直したときと同じ規律）。
//==============================================================================
namespace railGeom
{
    // 100% のときの実寸。設計書 §4-0 の「26/20/17/14」段階に合わせる。
    constexpr float kNameH  = 17.0f;   // 名前
    constexpr float kNoteH  = 14.0f;   // ひとこと（これ未満にしない）
    constexpr float kNumH   = 15.0f;   // 番号
    constexpr int   kPad    = 10;
    constexpr int   kNumW   = 26;
    constexpr int   kSwW    = 62;      // 押す物は 36px 以上（設計書）
    constexpr int   kSwH    = 30;
    constexpr int   kNameLn = 24;      // 名前の行送り
    constexpr int   kNoteLn = 19;      // ひとことの行送り
    constexpr int   kMeterH = 6;
    constexpr int   kGapY   = 3;       // カード間
    constexpr int   kArrowW = 30;      // 順番を動かす矢印（押す物なので小さくしない）
    constexpr int   kArrowH = 28;
    constexpr int   kPadT   = 6;       // カードの上下の余白
    constexpr int   kMeterG = 4;       // ひとこととメーターの間

    inline int sc (int v, float s) { return juce::roundToInt ((float) v * s); }

    // ★日本語は自分で折り返す。
    //  JUCE の drawFittedText は**空白でしか改行できない**ので、日本語の文には
    //  改行位置が1つも無い。結果、行数を2にしても折り返されず、横に潰してから
    //  「…」で切る——つまり「省略は論外」に真正面からぶつかる。
    //  （150%表示で「ノイズと雑音…」と出て気づいた。カード側の計算は2行だと
    //   言っていたのに、描画が1行にしていた。）
    //  なので文字幅を測りながら1文字ずつ詰め、行頭に来てはいけない字（、。っ ー 等）
    //  だけ1文字戻す＝簡易のぶら下げ組みにする。英文は最後の空白で折る。
    inline juce::StringArray wrapJa (const juce::Font& f, const juce::String& t,
                                     int width, int maxLines)
    {
        juce::StringArray out;
        if (t.isEmpty() || width <= 8 || maxLines <= 0) { out.add (t); return out; }
        static const juce::String noStart = juce::String::fromUTF8 (
            "\xe3\x80\x81\xe3\x80\x82\xe3\x83\xbb\xef\xbc\x89\xe3\x80\x8d\xe3\x80\x8f"
            "\xe3\x83\xbc\xe3\x80\x9c\xef\xbc\x81\xef\xbc\x9f"
            "\xe3\x81\x81\xe3\x81\x83\xe3\x81\x85\xe3\x81\x87\xe3\x81\x89"
            "\xe3\x81\xa3\xe3\x82\x83\xe3\x82\x85\xe3\x82\x87"
            "\xe3\x82\xa1\xe3\x82\xa3\xe3\x82\xa5\xe3\x82\xa7\xe3\x82\xa9"
            "\xe3\x83\x83\xe3\x83\xa3\xe3\x83\xa5\xe3\x83\xa7");
        juce::String rest = t;
        while (rest.isNotEmpty())
        {
            if (out.size() == maxLines - 1
                || juce::GlyphArrangement::getStringWidthInt (f, rest) <= width)
            { out.add (rest); break; }

            int n = 1;
            while (n < rest.length()
                   && juce::GlyphArrangement::getStringWidthInt (f, rest.substring (0, n + 1)) <= width)
                ++n;
            if (rest.substring (0, n).containsChar (' '))          // 英文は単語で折る
            {
                const int sp = rest.substring (0, n + 1).lastIndexOfChar (' ');
                if (sp > 0) n = sp;
            }
            else
            {
                // 中黒や読点があれば、そこで折ったほうが意味の切れ目に合う。
                // （「声を変える・ハモる」が「…・ハ / モる」と割れて読みにくかった）
                const juce::String head = rest.substring (0, n);
                int best = -1;
                for (int i = head.length() - 1; i >= n / 2; --i)
                    if (head[i] == juce::juce_wchar (0x30FB)        // ・
                        || head[i] == juce::juce_wchar (0x3001)     // 、
                        || head[i] == juce::juce_wchar (0x3002))    // 。
                    { best = i + 1; break; }
                if (best > 0) n = best;
                else if (n > 1 && n < rest.length() && noStart.containsChar (rest[n]))
                    --n;                                            // 行頭禁則
            }
            out.add (rest.substring (0, n).trimEnd());
            rest = rest.substring (n).trimStart();
        }
        return out;
    }
}

ModuleCard::ModuleCard (VocalGzzioProcessor& pr, int index, int endpointKind)
    : proc (pr), id (index)
{
    endpoint = endpointKind;
    // v3.1 §4「入」「出」は箱ではない。ON/OFF も入れ替えも無いので、
    //  スイッチも矢印もパラメータの結びつけも作らない（作ると押せてしまう）。
    if (endpoint != 0)
    {
        shown = index;
        addAndMakeVisible (bar);
        refreshColours();
        refreshText();
        return;
    }
    sw.setClickingTogglesState (true);
    refreshColours();
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
              (proc.apvts, gz::ModuleChain::paramId (id), sw);
    // ★カードの地・番号・文字は**この**コンポーネントが描き、スイッチは自分の四角
    //  だけを描き直す。これが無いと、押したときカードの片側だけ古い絵が残る
    //  （v3.0-c の第1歩で実際にそうなった。番号が半分だけ緑のまま残る）。
    sw.onClick = [this] { updateBarColours(); repaint(); if (onToggled) onToggled(); };
    addAndMakeVisible (sw);

    // v3.0-c 順番の分岐。動かせるカードにだけ出る矢印。
    //  ★「▲▼」は**サブセットフォントに入っていない**（fontTools で確認）。
    //   文字で書くと豆腐になるので、図形で描く juce::ArrowButton を使う。
    //   フォントを作り直すより、字に依存しないほうが安全。
    shown = index;
    addAndMakeVisible (bar);
    refreshText();
}

void ModuleCard::setDisplayIndex (int n)
{
    if (shown == n) return;
    shown = n; repaint();
}

// v3.1 §4「1つずつ」— カードの地を押したら、そのカードを選ぶ。
//  ★スイッチ(sw)・矢印(moveBtn)・メーター(bar) は子コンポーネントなので、
//   そこを押したときは JUCE がそちらへイベントを渡す＝ここへは来ない。
//   だから「ON/OFF したいだけなのに画面が切り替わる」ことは起きない。
void ModuleCard::mouseDown (const juce::MouseEvent&)
{
    if (onSelect) onSelect (id);
}

void ModuleCard::setSelected (bool v)
{
    if (selected == v) return;
    selected = v; repaint();
}

void ModuleCard::setMove (int dir, const juce::String& tipText)
{
    if (dir != moveDir || moveBtn == nullptr)
    {
        moveDir = dir;
        moveBtn.reset();
        if (dir != 0)
        {
            moveBtn = std::make_unique<juce::ArrowButton> (
                "mv", dir < 0 ? 0.75f : 0.25f,           // 0.75=上 / 0.25=下
                Palette::accentOn (Palette::green, Palette::panel2));
            moveBtn->onClick = [this] { if (onMove) onMove(); };
            addAndMakeVisible (*moveBtn);
        }
    }
    if (moveBtn != nullptr) moveBtn->setTooltip (tipText);
    resized(); repaint();
}

juce::String ModuleCard::name() const
{
    if (endpoint == 1) return tip::end_in_name();
    if (endpoint == 2) return tip::end_out_name();
    return juce::String::fromUTF8 (tip::english ? gz::ModuleChain::enName (id)
                                                : gz::ModuleChain::jpName (id));
}
juce::String ModuleCard::note() const
{
    if (endpoint == 1) return tip::end_in_note();
    if (endpoint == 2) return tip::end_out_note();
    return juce::String::fromUTF8 (tip::english ? gz::ModuleChain::enNote (id)
                                                : gz::ModuleChain::jpNote (id));
}

void ModuleCard::refreshText()
{
    sw.setTooltip (note());
    cachedRowsW = cachedStackW = -1;      // 言語が変わったら測り直す
    repaint();
}

void ModuleCard::setUiScale (float s)
{
    scale = juce::jlimit (0.7f, 2.2f, s);
    cachedRowsW = cachedStackW = -1;
    resized();
    repaint();
}

void ModuleCard::setMeter (float v)
{
    // ★ここでは**カードを描き直さない**。不透明な子（MeterBar）に渡すだけ。
    //  JUCE は不透明な子の下を描き直さないので、テーマの背景も
    //  カードの文字も触られない。実測 0.70ms → 0.003ms。
    meter = juce::jlimit (0.0f, 1.0f, v);
    bar.setLevel (meter);
}

// ひとことが何行に折り返るか。**縮めず・省略せず、折り返す**（設計書 §4）。
int ModuleCard::noteLines (int width) const
{
    return noteRows (width).size();
}

juce::StringArray ModuleCard::noteRows (int width) const
{
    using namespace railGeom;
    // ★キャッシュ必須。ここは1文字ずつ幅を測るので、毎フレーム呼ぶと重い。
    //  幅・文字%・文言のどれかが変わったときだけ計算し直す。
    const auto t = note();
    const bool arrow = (moveBtn != nullptr);
    if (width == cachedRowsW && scale == cachedRowsSc && t == cachedNote
        && arrow == cachedRowsArrow)
        return cachedRows;
    //  ★矢印の席を先に空ける（第23歩）。ここを引かないと、ひとことの最後の1字が
    //   ▲▼ に触る。帯（メーター）は前から引いてあったのに、文字だけ引き忘れていた。
    const int avail = width - sc (kPad, scale) * 2 - sc (kNumW, scale) - 6
                    - (arrow ? sc (kArrowW, scale) + 6 : 0);
    const auto f = GzzioLnF::uiFont (kNoteH * scale, false);
    cachedRows  = wrapJa (f, t, juce::jmax (24, avail), 3);
    cachedRowsW = width; cachedRowsSc = scale; cachedNote = t; cachedRowsArrow = arrow;
    return cachedRows;
}

int ModuleCard::wantedWidth() const
{
    using namespace railGeom;
    const auto fn = GzzioLnF::uiFont (kNameH * scale, true);
    const auto fo = GzzioLnF::uiFont (kNoteH * scale, false);
    const int  nameNeed = juce::GlyphArrangement::getStringWidthInt (fn, name());
    const int  noteNeed = juce::GlyphArrangement::getStringWidthInt (fo, note());
    const int chrome = sc (kPad, scale) * 2 + sc (kNumW, scale) + 6;
    const int arrowW = (moveBtn != nullptr) ? sc (kArrowW, scale) + 6 : 0;
    return juce::jmax (chrome + nameNeed + 8 + sc (kSwW, scale),   // 1行目: 名前＋スイッチ
                       chrome + noteNeed + arrowW);                 // 2行目: ひとこと＋矢印
}

// 幅が足りないなら、名前を縮めるのではなく**積む**（省略は論外）。
bool ModuleCard::stacked (int width) const
{
    using namespace railGeom;
    if (endpoint != 0) return false;      // スイッチが無いので縦積みは要らない
    if (width == cachedStackW && scale == cachedStackSc) return cachedStacked;
    const auto fn = GzzioLnF::uiFont (kNameH * scale, true);
    const int  nameNeed = juce::GlyphArrangement::getStringWidthInt (fn, name());
    const int  chrome = sc (kPad, scale) * 2 + sc (kNumW, scale) + 6;
    cachedStacked = nameNeed + 8 + sc (kSwW, scale) > width - chrome;
    cachedStackW  = width; cachedStackSc = scale;
    return cachedStacked;
}

int ModuleCard::preferredHeight (int width) const
{
    using namespace railGeom;
    const int rows = noteLines (width);
    const int body = sc (kPadT, scale) + rows * sc (kNoteLn, scale) + sc (kMeterG, scale)
                   + sc (kPadT, scale);
    if (! stacked (width))
        return body + juce::jmax (sc (kNameLn, scale), sc (kSwH, scale)) + sc (kMeterH, scale);
    // 積むとき: 名前 / ひとこと / （スイッチ＋メーター）の3段
    return body + sc (kNameLn, scale) + juce::jmax (sc (kSwH, scale), sc (kMeterH, scale));
}

void ModuleCard::resized()
{
    using namespace railGeom;
    const int pad = sc (kPad, scale);
    const int swW = sc (kSwW, scale), swH = sc (kSwH, scale);
    if (stacked (getWidth()))
        sw.setBounds (pad, getHeight() - sc (kPadT, scale) - swH, swW, swH);
    else
        sw.setBounds (getWidth() - pad - swW, sc (kPadT, scale), swW, swH);

    // 矢印はカードの右下（メーターの終わりに席を作る）。押す物なので28px以上。
    if (moveBtn != nullptr)
    {
        const int aw = sc (kArrowW, scale), ah = sc (kArrowH, scale);
        moveBtn->setBounds (getWidth() - pad - aw,
                            getHeight() - sc (kPadT, scale) - ah, aw, ah);
    }

    // メーターの帯の席をここで決めておく（paint と同じ式）。
    //  20Hz で描き直すのはこの矩形だけにする。
    {
        const bool tall = stacked (getWidth());
        const int mh = sc (kMeterH, scale);
        const int mx = tall ? (pad + sc (kSwW, scale) + 8) : pad;
        const int my = tall ? (sw.getY() + (sw.getHeight() - mh) / 2)
                            : (getHeight() - sc (kPadT, scale) - mh);
        const int mRight = pad + (moveBtn != nullptr ? sc (kArrowW, scale) + 6 : 0);
        meterRect = { mx, my, juce::jmax (12, getWidth() - mx - mRight), mh };
        bar.setBounds (meterRect);
        updateBarColours();
    }
}

// 色は**毎回テーマから取り直す**。ここを constructor で1回だけ入れていたら、
// 起動時のテーマ適用より前の色（暗いほう）を掴んだまま、明るいテーマでも
// 濃紺のボタンが2つだけ残った。カードの地は paint で毎回読むので正しく、
// ボタンだけ取り残される——という気づきにくい壊れ方をする。
void ModuleCard::refreshColours()
{
    updateBarColours();
    sw.setColour (juce::TextButton::buttonOnColourId, Palette::green);
    sw.setColour (juce::TextButton::textColourOnId,   Palette::readableOn (Palette::green));
    sw.setColour (juce::TextButton::textColourOffId,  Palette::ink);
    sw.setColour (juce::TextButton::buttonColourId,   Palette::panel2);
    repaint();
}

void VocalGzzioContent::FocusEmptyNote::paint (juce::Graphics& g)
{
    auto sc2 = [this] (float v) { return (int) (v * scale); };
    auto r = getLocalBounds().reduced (sc2 (20.0f));
    const int lh1 = sc2 (30.0f), lh2 = sc2 (24.0f);
    const auto f2 = GzzioLnF::uiFont (17.0f * scale, false);
    //  日本語は drawFittedText では折れないので、ここでも自前で折る。
    const auto rows = railGeom::wrapJa (f2, tip::use_off_how(), r.getWidth(), 2);
    //  ★2行ぶんをひとかたまりにして、席の**まん中**に置く。上に貼り付けると
    //   下に大きな空白が残って、やはり壊れて見える。
    const int blockH = lh1 + sc2 (6.0f) + rows.size() * lh2;
    int y = r.getY() + juce::jmax (0, (r.getHeight() - blockH) / 2);
    g.setColour (Palette::inkSoft);
    g.setFont (GzzioLnF::uiFont (20.0f * scale, true));
    g.drawText (tip::use_off_note(), r.withTop (y).withHeight (lh1),
                juce::Justification::centredTop);
    y += lh1 + sc2 (6.0f);
    g.setColour (Palette::inkSoft.withAlpha (0.75f));
    g.setFont (f2);
    for (int i = 0; i < rows.size(); ++i)
        g.drawText (rows[i], r.withTop (y + i * lh2).withHeight (lh2),
                    juce::Justification::centredTop);
}

void ModuleCard::paint (juce::Graphics& g)
{
    using namespace railGeom;
    //  「入」「出」はいつも通る所なので、常に ON の見た目にする。
    const bool on  = (endpoint != 0) || sw.getToggleState();

    const int  pad = sc (kPad, scale);
    const int  numW = sc (kNumW, scale);
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    // ---- カードの地。OFF は地ごと薄くする（スイッチだけ見て判断させない）----
    g.setColour (on ? Palette::panel2 : Palette::panel2.withAlpha (0.42f));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (Palette::panelLn.withAlpha (on ? 0.9f : 0.35f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    // v3.1 いま見ている1枚。太い枠で言い切る（色だけだと色覚テーマで消える）。
    if (selected)
    {
        g.setColour (Palette::green);
        g.drawRoundedRectangle (r.reduced (1.5f), 10.0f, 3.0f);
    }

    const bool tall   = stacked (getWidth());
    const int textX   = pad + numW + 6;
    const int textW   = getWidth() - textX - pad;
    const int nameTop = sc (kPadT, scale);
    const int nameH   = sc (kNameLn, scale);

    // ---- 番号。上から下へ＝音の流れであることを、数字で言い切る ----
    g.setColour (on ? Palette::accentOn (Palette::green, Palette::panel2)
                    : Palette::inkSoft.withAlpha (0.45f));
    g.setFont (GzzioLnF::uiFont (kNumH * scale, true));
    //  番号のかわりに「入」「出」。数字にすると 1〜8 の並びに割り込んで見える。
    const juce::String numText = endpoint == 1 ? tip::end_in_mark()
                               : endpoint == 2 ? tip::end_out_mark()
                                               : juce::String (shown + 1);
    g.drawText (numText, juce::Rectangle<int> (pad, nameTop, numW, nameH),
                juce::Justification::centred);

    // ---- 名前。横に並べるときだけスイッチのぶんを譲る ----
    //  「入」「出」にはスイッチが無いので、譲らずに全幅を使う。
    const int nameW = (endpoint != 0) ? textW
                    : (tall ? textW : (textW - sc (kSwW, scale) - 8));
    g.setColour (Palette::ink.withAlpha (on ? 1.0f : 0.45f));
    {
        const auto f = GzzioLnF::uiFont (kNameH * scale, true);
        g.setFont (GzzioLnF::fitFont (f, name(), juce::jmax (10, nameW)));
        g.drawText (name(), juce::Rectangle<int> (textX, nameTop, juce::jmax (10, nameW), nameH),
                    juce::Justification::centredLeft);
    }

    // ---- ひとこと。入り切らなければ**折り返す**（省略は論外）----
    //  日本語は drawFittedText では折れないので、自前で行に割ってから描く。
    const int noteTop = nameTop + (tall ? nameH
                                        : juce::jmax (nameH, sc (kSwH, scale) - sc (2, scale)));
    const auto rows = noteRows (getWidth());
    const int  lnH  = sc (kNoteLn, scale);
    g.setColour (Palette::inkSoft.withAlpha (on ? 0.95f : 0.4f));
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto f = GzzioLnF::uiFont (kNoteH * scale, false);
        g.setFont (GzzioLnF::fitFont (f, rows[i], textW));
        g.drawText (rows[i], juce::Rectangle<int> (textX, noteTop + i * lnH, textW, lnH),
                    juce::Justification::centredLeft);
    }

    // ---- はたらき量は子（MeterBar）が描く。ここでは何もしない ----
}

// カードの地の色を、そのまま帯の下地として渡す。
//  ON のときの地は不透明なので、帯を opaque にできる＝親を描き直させない。
//  OFF のときは半透明なので opaque にできないが、メーターは0で止まっているので
//  速さは要らない（動かない物は描き直されない）。
void ModuleCard::updateBarColours()
{
    const bool on = sw.getToggleState();
    bar.setColours (on ? Palette::panel2 : juce::Colours::transparentBlack,
                    Palette::track.withAlpha (on ? 0.9f : 0.35f),
                    Palette::green.withAlpha (0.92f));
}

//------------------------------------------------------------------------------
PathRail::PathRail (VocalGzzioProcessor& pr) : proc (pr)
{
    for (int m = 0; m < gz::ModuleChain::Count; ++m)
    {
        auto* c = cards.add (new ModuleCard (proc, m));
        // 矢印を押したら、対応する分岐パラメータを反転する。
        //  パラメータなので**保存にもオートメーションにも載る**（くらべる とは逆に、
        //  こちらは「決めた設定」なので残ってほしい）。
        c->onMove = [this, m]
        {
            const char* id = (m == gz::ModuleChain::Sagyo)    ? "ord_deess"
                           : (m == gz::ModuleChain::Totonoe)  ? "ord_eq"
                           : (m == gz::ModuleChain::Hirogari) ? "ord_space" : nullptr;
            if (id == nullptr) return;
            if (auto* prm = proc.apvts.getParameter (id))
            {
                const bool now = prm->getValue() > 0.5f;
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (now ? 0.0f : 1.0f);
                prm->endChangeGesture();
            }
            applyOrder();
        };
        // v3.1 §4「1つずつ」: カードを押したら、その1枚を選ぶ。
        c->onSelect = [this] (int mod) { setSelected (mod); if (onSelect) onSelect (mod); };
        addAndMakeVisible (c);
    }
    // v3.1 §4 案A の「入 マイクから」「出 しあげ」。
    //  ★選ぶときの番号は 8(入) / 9(出)。箱は 0〜7 なので、番号がぶつからない。
    //  ★cards には入れない。入れると「順番の分岐」や「ぜんぶ入れる/切る」の
    //   対象になってしまう（この2つは、いつも通る所なので切れない）。
    inCard  = std::make_unique<ModuleCard> (proc, 8, 1);
    outCard = std::make_unique<ModuleCard> (proc, 9, 2);
    inCard ->onSelect = [this] (int m) { setSelected (m); if (onSelect) onSelect (m); };
    outCard->onSelect = [this] (int m) { setSelected (m); if (onSelect) onSelect (m); };
    addAndMakeVisible (*inCard);
    addAndMakeVisible (*outCard);

    auto setAll = [this] (bool on)
    {
        for (int m = 0; m < gz::ModuleChain::Count; ++m)
            if (auto* prm = proc.apvts.getParameter (gz::ModuleChain::paramId (m)))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (on ? 1.0f : 0.0f);
                prm->endChangeGesture();
            }
        for (auto* c : cards) c->repaint();
    };
    allOn .onClick = [setAll] { setAll (true);  };
    allOff.onClick = [setAll] { setAll (false); };
    for (auto* b : { &allOn, &allOff }) addAndMakeVisible (b);

    // v3.1 §4「1つずつ」。押すと中央が「選んだカードの中身だけ」になる。
    focusBtn.setClickingTogglesState (true);
    focusBtn.setComponentID ("focusBtn");   // 検査が**本物のボタンを押す**ための名札
    focusBtn.onClick = [this]
    {
        if (onFocusToggled) onFocusToggled (focusBtn.getToggleState());
    };
    addAndMakeVisible (focusBtn);

    // v3.0-c「くらべる」（設計書 §4 の最後の1項目）。
    //  説明を読むより、**耳で1回きくほうが早い**。押している間だけ素通しにする。
    //  onClick ではなく押し下げ/離しを見る必要があるので、Button::Listener 相当を
    //  ラムダで置き換えられない。専用の小さなクラスにする。
    addAndMakeVisible (compareBtn);
    compareBtn.onDown = [this] { proc.compareBypass.store (true,  std::memory_order_relaxed); repaint(); };
    compareBtn.onUp   = [this] { proc.compareBypass.store (false, std::memory_order_relaxed); repaint(); };

    // v3.0-c サビリフトの互換スイッチ。関係のある人にだけ見えるようにする。
    legacyBtn.setClickingTogglesState (true);
    legacyAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                    (proc.apvts, "lift_legacy", legacyBtn);
    addChildComponent (legacyBtn);

    refreshColours();
    refreshText();
    applyOrder();
}

PathRail::~PathRail()
{
    // 押したまま画面を閉じられても、素通しのまま取り残さない
    proc.compareBypass.store (false, std::memory_order_relaxed);
}

void PathRail::refreshColours()
{
    // ぜんぶ入れる/切る は**色を指定しない**。指定すると LnF のテーマ追随から
    // 外れて、明るいテーマでもここだけ濃紺のまま残る（実際そうなった）。
    for (auto* b : { &allOn, &allOff })
    {
        b->removeColour (juce::TextButton::buttonColourId);
        b->removeColour (juce::TextButton::textColourOffId);
    }
    compareBtn.removeColour (juce::TextButton::buttonColourId);
    compareBtn.removeColour (juce::TextButton::textColourOffId);
    // これはトグルなので、カードのスイッチと同じ色づけにする。
    //  色を外すと、LnF のトグル既定（濃い地）になって明るいテーマで読めなかった。
    legacyBtn.setColour (juce::TextButton::buttonOnColourId, Palette::green);
    legacyBtn.setColour (juce::TextButton::textColourOnId,   Palette::readableOn (Palette::green));
    legacyBtn.setColour (juce::TextButton::textColourOffId,  Palette::ink);
    legacyBtn.setColour (juce::TextButton::buttonColourId,   Palette::panel2);
    // v3.1「1つずつ」もトグル。legacyBtn と同じ色づけにそろえる。
    focusBtn.setColour (juce::TextButton::buttonOnColourId, Palette::green);
    focusBtn.setColour (juce::TextButton::textColourOnId,   Palette::readableOn (Palette::green));
    focusBtn.setColour (juce::TextButton::textColourOffId,  Palette::ink);
    focusBtn.setColour (juce::TextButton::buttonColourId,   Palette::panel2);
    for (auto* c : cards) c->refreshColours();
    if (inCard  != nullptr) inCard ->refreshColours();
    if (outCard != nullptr) outCard->refreshColours();
    repaint();
}

void PathRail::refreshText()
{
    allOn .setButtonText (tip::mods_all_on());
    allOff.setButtonText (tip::mods_all_off());
    compareBtn.setButtonText (tip::compare_label());
    compareBtn.setTooltip (tip::compare_tip());
    focusBtn.setButtonText (tip::focus_label());
    focusBtn.setTooltip (tip::focus_tip());
    legacyBtn.setButtonText (tip::lift_legacy_label());
    legacyBtn.setTooltip (tip::lift_legacy_tip());
    for (auto* c : cards) c->refreshText();
    repaint();
}

void PathRail::setUiScale (float s)
{
    scale = juce::jlimit (0.7f, 2.2f, s);
    for (auto* c : cards) c->setUiScale (scale);
    resized();
    repaint();
}

// v3.0-c いまの実際の順。分岐パラメータ2つから作る。
//  自由な並べ替えではないので、起こりうるのは4通りしかない。
//  ここを processBlock の並びと**同じ規則**で書いておかないと、
//  画面の順と実際の音の順がずれる（それがいちばん困る嘘になる）。
// v3.1 その箱が「いま何番目を通るか」。カードに描いている数字と必ず同じにする。
int PathRail::displayPosOf (int module) const
{
    const auto ord = (shownOrder[0] < 0) ? currentOrder() : shownOrder;
    for (int i = 0; i < (int) ord.size(); ++i)
        if (ord[(size_t) i] == module) return i;
    return module;
}

// v3.1 §4「1つずつ」。選ばれている1枚を覚え、カード側の枠を切り替える。
void PathRail::setSelected (int module)
{
    if (selectedModule == module) return;
    selectedModule = module;
    for (int m = 0; m < cards.size(); ++m)
        cards[m]->setSelected (m == module);
    if (inCard  != nullptr) inCard ->setSelected (module == 8);
    if (outCard != nullptr) outCard->setSelected (module == 9);
}

std::array<int, gz::ModuleChain::Count> PathRail::currentOrder() const
{
    using M = gz::ModuleChain;
    const bool deessEarly = proc.apvts.getRawParameterValue ("ord_deess")->load() > 0.5f;
    const bool eqLate     = proc.apvts.getRawParameterValue ("ord_eq")->load()    > 0.5f;
    const bool spaceEarly = proc.apvts.getRawParameterValue ("ord_space")->load() > 0.5f;

    std::array<int, M::Count> o { M::Souji, M::Henshin, 0, 0, 0, M::Neiro, M::Chara, M::Hirogari };
    // 真ん中の3つ（ととのえ・音量そろえ・サ行おさえ）だけが動く
    if (! deessEarly && ! eqLate) { o[2] = M::Totonoe; o[3] = M::Soroe;   o[4] = M::Sagyo;   }
    else if (deessEarly && ! eqLate) { o[2] = M::Totonoe; o[3] = M::Sagyo; o[4] = M::Soroe;  }
    else if (! deessEarly && eqLate) { o[2] = M::Soroe;   o[3] = M::Sagyo; o[4] = M::Totonoe; }
    else                             { o[2] = M::Sagyo;   o[3] = M::Soroe; o[4] = M::Totonoe; }
    // v3.0-c 分岐3: 最後の2つ（キャラ声・ひろがり）を入れ替える
    if (spaceEarly) { o[6] = M::Hirogari; o[7] = M::Chara; }
    return o;
}

void PathRail::applyOrder()
{
    using M = gz::ModuleChain;
    const auto o = currentOrder();
    if (o == shownOrder) return;               // 変わっていなければ何もしない
    shownOrder = o;

    const bool deessEarly = proc.apvts.getRawParameterValue ("ord_deess")->load() > 0.5f;
    const bool eqLate     = proc.apvts.getRawParameterValue ("ord_eq")->load()    > 0.5f;
    const bool spaceEarly = proc.apvts.getRawParameterValue ("ord_space")->load() > 0.5f;

    for (int pos = 0; pos < (int) o.size(); ++pos)
    {
        auto* c = cards[o[(size_t) pos]];
        c->setDisplayIndex (pos);
        if (o[(size_t) pos] == M::Sagyo)
            c->setMove (deessEarly ? +1 : -1,
                        deessEarly ? tip::ord_deess_back() : tip::ord_deess_up());
        else if (o[(size_t) pos] == M::Totonoe)
            c->setMove (eqLate ? -1 : +1,
                        eqLate ? tip::ord_eq_back() : tip::ord_eq_down());
        else if (o[(size_t) pos] == M::Hirogari)
            c->setMove (spaceEarly ? +1 : -1,
                        spaceEarly ? tip::ord_space_back() : tip::ord_space_up());
        else
            c->setMove (0, {});
    }
    resized();
    repaint();
}


// v3.0-c 互換スイッチを「意味があるときだけ」出す。
//  出す条件はどちらか:
//   ・すでに入っている（古いプロジェクトを開いた／自分で入れた）
//   ・サビリフトかエモを使っていて、しかもキャラ声を切っている
//     ＝ **まさに「効かないぞ」と思う状況**
//  それ以外の人には最初から見えない。関係のない札を常に置いておかないため。
void PathRail::updateLegacyVisibility()
{
    const bool on   = proc.apvts.getRawParameterValue ("lift_legacy")->load() > 0.5f;
    const bool uses = proc.apvts.getRawParameterValue ("lift_amt")->load() > 0.5f
                   || proc.apvts.getRawParameterValue ("emo_amt")->load()  > 0.5f;
    const bool charaOff = proc.apvts.getRawParameterValue (
                              gz::ModuleChain::paramId (gz::ModuleChain::Chara))->load() <= 0.5f;
    const bool want = on || (uses && charaOff);
    if (want == legacyShown) return;
    legacyShown = want;
    legacyBtn.setVisible (want);
    setSize (getWidth(), preferredHeight (getWidth()));
    resized();
    repaint();
}

void PathRail::tick()
{
    applyOrder();                              // ホストのオートメーションにも追随する
    updateLegacyVisibility();
    const auto& mc = proc.moduleChain();
    for (int m = 0; m < cards.size(); ++m)
        cards[m]->setMeter (mc.workAmount (m));
    //  「入」「出」のメーターは、そのまま入出力の大きさ。
    if (inCard  != nullptr) inCard ->setMeter (juce::jlimit (0.0f, 1.0f, proc.getInputLevel()  * 2.0f));
    if (outCard != nullptr) outCard->setMeter (juce::jlimit (0.0f, 1.0f, proc.getOutputLevel() * 2.0f));
}

int PathRail::wantedWidth() const
{
    int w = 196;
    for (auto* c : cards) w = juce::jmax (w, c->wantedWidth());
    if (inCard  != nullptr) w = juce::jmax (w, inCard ->wantedWidth());
    if (outCard != nullptr) w = juce::jmax (w, outCard->wantedWidth());
    return w;
}

juce::StringArray PathRail::headerRows (int width) const
{
    using namespace railGeom;
    return wrapJa (GzzioLnF::uiFont (16.0f * scale, true), tip::mods_path(),
                   juce::jmax (40, width - 4), 2);
}

int PathRail::headerHeight (int width) const
{
    using namespace railGeom;
    return headerRows (width).size() * sc (24, scale) + sc (8, scale);
}

int PathRail::preferredHeight (int width) const
{
    using namespace railGeom;
    int h = headerHeight (width);
    if (inCard  != nullptr) h += inCard ->preferredHeight (width) + sc (kGapY, scale);
    for (auto* c : cards) h += c->preferredHeight (width) + sc (kGapY, scale);
    if (outCard != nullptr) h += outCard->preferredHeight (width) + sc (kGapY, scale);
    // ぜんぶ入れる/切る の行 ＋ くらべる の行（＋出ているときだけ互換スイッチ）
    //  ぜんぶ入れる/切る（34）＋ 1つずつ（34）＋ くらべる（38）
    return h + sc (6, scale) + sc (34, scale) + sc (6, scale) + sc (34, scale)
             + sc (6, scale) + sc (38, scale) + sc (4, scale)
             + (legacyShown ? sc (30, scale) + sc (6, scale) : 0);
}

void PathRail::resized()
{
    using namespace railGeom;
    const int W = getWidth();
    int y = headerHeight (W);
    //  「入」は箱より前、「出」は箱より後ろ。ここは並べ替えの対象にしない。
    if (inCard != nullptr)
    {
        const int ch = inCard->preferredHeight (W);
        inCard->setBounds (0, y, W, ch);
        y += ch + sc (kGapY, scale);
    }
    // ★並べるのは「実際の順」。カードの持ち主(id)ではなく、いまの順で置く。
    //  ここが processBlock の並びとずれると、画面が嘘をつくことになる。
    const auto ord = (shownOrder[0] < 0) ? currentOrder() : shownOrder;
    for (int pos = 0; pos < (int) ord.size(); ++pos)
    {
        auto* c = cards[ord[(size_t) pos]];
        const int ch = c->preferredHeight (W);
        c->setBounds (0, y, W, ch);
        y += ch + sc (kGapY, scale);
    }
    if (outCard != nullptr)
    {
        const int ch = outCard->preferredHeight (W);
        outCard->setBounds (0, y, W, ch);
        y += ch + sc (kGapY, scale);
    }
    y += sc (6, scale);
    const int bh = sc (34, scale);
    const int bw = (W - 8) / 2;
    allOn .setBounds (0, y, bw, bh);
    allOff.setBounds (W - bw, y, bw, bh);
    // v3.1「1つずつ」を1行。押す物なので 34px を確保する（設計書§4-0）。
    y += bh + sc (6, scale);
    focusBtn.setBounds (0, y, W, bh);
    // 「くらべる」は押しっぱなしにする物なので、指が乗る大きさを確保して1行取る
    y += bh + sc (6, scale);
    compareBtn.setBounds (0, y, W, sc (38, scale));
    if (legacyShown)
    {
        y += sc (38, scale) + sc (6, scale);
        legacyBtn.setBounds (0, y, W, sc (30, scale));
    }
}

void PathRail::paint (juce::Graphics& g)
{
    using namespace railGeom;
    const auto rows = headerRows (getWidth());
    const int  lnH  = sc (24, scale);
    g.setColour (Palette::ink);
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto f = GzzioLnF::uiFont (16.0f * scale, true);
        g.setFont (GzzioLnF::fitFont (f, rows[i], getWidth() - 4));
        g.drawText (rows[i], juce::Rectangle<int> (2, sc (4, scale) + i * lnH, getWidth() - 4, lnH),
                    juce::Justification::centredLeft);
    }
}

// 「くらべる」を押している間は、列ぜんたいに薄い幕をかけて「いま生の声です」と出す。
//  スイッチ自体は入ったままなので、カードだけ見ると入っているように見える——
//  そこで取り違えないように、押している間だけはっきり言い切る。
void PathRail::paintOverChildren (juce::Graphics& g)
{
    using namespace railGeom;
    if (! compareBtn.isHeld()) return;
    auto r = getLocalBounds().withTrimmedBottom (compareBtn.getHeight() + sc (6, scale));
    g.setColour (Palette::panel.withAlpha (0.70f));
    g.fillRect (r);

    // 札にして、下地が何色でも読めるようにする（薄い字を veil の上に置くと沈む）
    const auto t = tip::compare_now();
    const auto f = GzzioLnF::uiFont (19.0f * scale, true);
    const int  tw = juce::GlyphArrangement::getStringWidthInt (f, t);
    const int  bw = juce::jmin (r.getWidth() - 8, tw + sc (28, scale));
    const int  bh = sc (40, scale);
    juce::Rectangle<int> plate (r.getCentreX() - bw / 2, r.getCentreY() - bh / 2, bw, bh);
    // 淡いテーマだと salmon が地に溶ける（実際「いま 生の声」が読めなかった）。
    // 下地とのコントラストが出るところまで寄せた色を使う。
    const auto plateCol = Palette::accentOn (Palette::salmon, Palette::panel);
    g.setColour (plateCol);
    g.fillRoundedRectangle (plate.toFloat(), (float) bh * 0.5f);
    g.setColour (Palette::readableOn (plateCol));
    g.setFont (GzzioLnF::fitFont (f, t, plate.getWidth() - 10));
    g.drawText (t, plate, juce::Justification::centred);
}

//==============================================================================
VocalGzzioContent::VocalGzzioContent (VocalGzzioProcessor& p)
    : processor (p), tuner (p), eqGraph (p), pathRail (p)
{
    mascot = juce::ImageCache::getFromMemory (BinaryData::character_png, BinaryData::character_pngSize);
    kawaiiMascot = juce::ImageCache::getFromMemory (BinaryData::puniguji_kawaii_png, BinaryData::puniguji_kawaii_pngSize);
    themePainter.setMascot (kawaiiMascot);

    // knobs (order = signal flow, grouped)
    addKnob (gate,     "gate",     juce::String::fromUTF8 ("\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\x88"), tip::gate_tip(),  "dB");
    addKnob (lowCut,   "lowcut",   juce::String::fromUTF8 ("\xe3\x83\xad\xe3\x83\xbc\xe3\x82\xab\xe3\x83\x83\xe3\x83\x88"), tip::lowcut(),    "Hz");
    addKnob (mudK,     "mud",      juce::String::fromUTF8 ("\xe3\x81\x93\xe3\x82\x82\xe3\x82\x8a"), tip::mud(), "dB");
    addKnob (harshK,   "harsh",    juce::String::fromUTF8 ("\xe3\x82\xad\xe3\x83\xb3\xe3\x82\xad\xe3\x83\xb3"), tip::harsh(), "dB");
    addKnob (denoiseK, "denoise",  juce::String::fromUTF8 ("\xe3\x83\x8e\xe3\x82\xa4\xe3\x82\xba\xe9\x99\xa4\xe5\x8e\xbb"), tip::denoise_tip(), "%");
    addKnob (popK,     "pop_amt",  tip::pop_label(), tip::pop_tip(), "%");   // v2.3.0
    addKnob (lipK,     "lip_amt",  tip::lip_label(), tip::lip_tip(), "%");
    addKnob (resK,     "res_amt",  tip::res_label(), tip::res_tip(), "%");   // v2.4.0 なめらか
    addKnob (pickK,    "pick_amt", tip::pick_label(), tip::pick_tip(), "%");  // v3.1 ピックおさえ
    addKnob (inGainK,  "in_gain",  tip::mic_label(), tip::mic_tip(), "dB");  // v2.4.0 マイク音量
    addKnob (rideK,    "ride_amt", tip::ride_label(), tip::ride_tip(), "%"); // v2.4.0 音量キープ
    addKnob (humK,     "hum_amt",  tip::hum_label(),  tip::hum_tip(),  "%"); // v2.6.0 ジー音
    addKnob (consK,    "cons_amt", tip::cons_label(), tip::cons_tip(), "%"); // v2.6.0 ことば

    addKnob (comp1K,   "comp1",    juce::String::fromUTF8 ("\xe3\x83\x94\xe3\x83\xbc\xe3\x82\xaf\xe5\x9c\xa7\xe7\xb8\xae"), tip::comp1_tip(), "%");
    addKnob (comp2K,   "comp2",    juce::String::fromUTF8 ("\xe3\x81\xaa\xe3\x82\x89\xe3\x81\x97\xe5\x9c\xa7\xe7\xb8\xae"),tip::comp2_tip(), "%");
    addKnob (attackK,  "attack",   juce::String::fromUTF8 ("\xe3\x82\xa2\xe3\x82\xbf\xe3\x83\x83\xe3\x82\xaf"), tip::attack(),    "ms");
    addKnob (releaseK, "release",  juce::String::fromUTF8 ("\xe3\x83\xaa\xe3\x83\xaa\xe3\x83\xbc\xe3\x82\xb9"), tip::release(),   "ms");
    addKnob (deessK,   "deess",    juce::String::fromUTF8 ("\xe3\x82\xb5\xe8\xa1\x8c\xe3\x81\x8a\xe3\x81\x95\xe3\x81\x88"), tip::deess(),     "%");

    addKnob (presenceK,"presence", juce::String::fromUTF8 ("\xe3\x83\x8c\xe3\x82\xb1\xe6\x84\x9f"), tip::presence(),  "dB");
    addKnob (airK,     "air",      juce::String::fromUTF8 ("\xe3\x82\xad\xe3\x83\xa9\xe3\x82\xad\xe3\x83\xa9"), tip::air(),       "dB");
    addKnob (warmthK,  "drive",    juce::String::fromUTF8 ("\xe3\x81\x82\xe3\x81\x9f\xe3\x81\x9f\xe3\x81\x8b\xe3\x81\xbf"), tip::warmth(),    "%");
    addKnob (sustainK, "sustain",  juce::String::fromUTF8 ("\xe3\x81\xae\xe3\x81\xb3"), tip::sustain_tip(), "%");
    addKnob (ringK,    "ring",     tip::ring_label(), tip::ring_tip(), "%");   // v1.9.5 艶

    addKnob (liftK,    "lift_amt", tip::lift_label(), tip::lift_tip(), "%");   // v2.0.0 サビリフト

    addKnob (makeupK,  "makeup",   juce::String::fromUTF8 ("\xe4\xbb\x95\xe4\xb8\x8a\xe3\x81\x92\xe9\x9f\xb3\xe9\x87\x8f"), tip::makeup(),    "dB");
    addKnob (mixK,     "mix",      juce::String::fromUTF8 ("\xe3\x82\xa8\xe3\x83\x95\xe3\x82\xa7\xe3\x82\xaf\xe3\x83\x88\xe9\x87\x8f"), tip::mix(),       "%");
    addKnob (widthK,   "width",    juce::String::fromUTF8 ("\xe3\x81\xb2\xe3\x82\x8d\xe3\x81\x8c\xe3\x82\x8a"), tip::width(),     "%");
    addKnob (doublerK, "doubler",  juce::String::fromUTF8 ("\xe3\x81\x8b\xe3\x81\x95\xe3\x81\xad"), tip::doubler(),   "%");
    addKnob (delayK,   "delay",    juce::String::fromUTF8 ("\xe3\x82\x84\xe3\x81\xbe\xe3\x81\xb3\xe3\x81\x93"), tip::delay_tip(), "%");
    addKnob (revSizeK, "revsize",  juce::String::fromUTF8 ("\xe9\x83\xa8\xe5\xb1\x8b\xe3\x81\xae\xe5\xba\x83\xe3\x81\x95"), tip::revsize(),   "%");
    addKnob (revMixK,  "revmix",   juce::String::fromUTF8 ("\xe3\x81\xb2\xe3\x81\xb3\xe3\x81\x8d"), tip::revmix(),    "%");

    // ---- Smart Dynamic EQ knobs ----
    addKnob (seqAmountK, "seq_amount", tip::seq_amount_label(), tip::seq_amount_tip(), "%");
    addKnob (seqFocusK,  "seq_focus",  tip::seq_focus_label(),  tip::seq_focus_tip(),  "%");
    addKnob (seqF1K, "seq_f1", tip::seq_freq_label(),  tip::seq_freq_tip(),  "Hz");
    addKnob (seqD1K, "seq_d1", tip::seq_depth_label(), tip::seq_depth_tip(), "dB");
    addKnob (seqF2K, "seq_f2", tip::seq_freq_label(),  tip::seq_freq_tip(),  "Hz");
    addKnob (seqD2K, "seq_d2", tip::seq_depth_label(), tip::seq_depth_tip(), "dB");
    addKnob (seqF3K, "seq_f3", tip::seq_freq_label(),  tip::seq_freq_tip(),  "Hz");
    addKnob (seqD3K, "seq_d3", tip::seq_depth_label(), tip::seq_depth_tip(), "dB");

    // Smart EQ on/off (panel header)
    seqOnButton.setClickingTogglesState (true);
    seqOnButton.setTooltip (tip::seq_on_tip());
    seqOnButton.setColour (juce::TextButton::buttonOnColourId, Palette::green);
    seqOnButton.onClick = [this]
    {
        seqOnButton.setButtonText (seqOnButton.getToggleState()
            ? juce::String::fromUTF8 ("\x45\x51\x20\x4f\x4e") : juce::String::fromUTF8 ("\x45\x51\x20\x4f\x46\x46"));
    };
    styleButton (seqOnButton);
    seqOnAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                      (processor.apvts, "seq_on", seqOnButton);
    seqOnButton.setButtonText (seqOnButton.getToggleState()
        ? juce::String::fromUTF8 ("\x45\x51\x20\x4f\x4e") : juce::String::fromUTF8 ("\x45\x51\x20\x4f\x46\x46"));

    // Smart EQ mode selector (自動 / 手動)
    seqModeBox.addItem (tip::seq_mode_auto(),   1);
    seqModeBox.addItem (tip::seq_mode_manual(), 2);
    seqModeBox.setTooltip (tip::seq_mode_tip());
    seqModeBox.setJustificationType (juce::Justification::centred);
    seqModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
                        (processor.apvts, "seq_mode", seqModeBox);
    seqModeBox.onChange = [this] { updateSeqModeVisibility(); };
    addAndMakeVisible (seqModeBox);

    // v2.10.0 #77/#78 音源モード → v3.1「使いかた4種」
    //  ★並びは APVTS の選択肢と**同じ順**でなければならない。ComboBox の
    //   itemId は 1 から、パラメータの番号は 0 から。ここを入れ替えると
    //   古いプロジェクトが別の使いかたで開く。足すときは必ず末尾へ。
    srcModeBox.addItem (tip::src_uta(),         1);   // 0 うた
    srcModeBox.addItem (tip::src_gita(),        2);   // 1 アコギだけ
    srcModeBox.addItem (tip::src_shaberi(),     3);   // 2 しゃべり
    srcModeBox.addItem (tip::src_hikigatari(),  4);   // 3 弾き語り（v3.1 新設）
    srcModeBox.setTooltip (tip::src_tip());
    srcModeBox.setJustificationType (juce::Justification::centred);
    srcModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
                        (processor.apvts, "src_mode", srcModeBox);
    // v3.0 使いかたを選び直したら「つぶさない」も、その使いかたのおすすめに合わせる。
    //  うた／アコギだけ／弾き語り = ON（大きく出したところが曲の山なので潰さない）
    //  しゃべり                   = OFF（話し声は音量がそろっている方が聞き取りやすい）
    //  ★あくまで**選び直したときの初期値**。そのあとスイッチを触れば、そちらが残る。
    //   ホストのオートメーションで src_mode が動いたときは触らない（勝手に音を
    //   変えないため）。人が選んだときだけ効く。
    srcModeBox.onChange = [this]
    {
        const int m = juce::jmax (0, srcModeBox.getSelectedItemIndex());
        if (auto* prm = processor.apvts.getParameter ("crush_on"))
        {
            const bool want = crushRecommendedFor (m);
            if ((prm->getValue() > 0.5f) != want)
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (want ? 1.0f : 0.0f);
                prm->endChangeGesture();
            }
        }
        // v3.1 使いかたごとに覚えてある値を出し入れする（設計書§3）。
        //  タイマー(500ms)でも同じものを呼んでいるが、画面から選んだときに
        //  0.5秒待たされると「効いていない」と見えるので、ここでも即座に呼ぶ。
        //  二重に呼ばれても、使いかたが変わっていなければ何もしない。
        processor.applyUseModeMemoryIfChanged();

        // v3.1 使いかたが変わったら、その使いかたで**通らない**ツマミを出し入れする。
        //  applyModeVisibility() を丸ごと呼ばないのは、あちらが easyComboPhase を
        //  0 に戻すから（10秒おまかせの途中経過が黙って消える）。
        applyKnobVisibility();
        resized();
        repaint();
    };
    addAndMakeVisible (srcModeBox);

    // ---- preset combos: voice type (10) & mic model (10) ----
    voiceBox.setTextWhenNothingSelected (tip::voice_placeholder());
    voiceBox.addSectionHeading (tip::female_head());
    for (int i = 0;  i < 5;  ++i) voiceBox.addItem (voiceItemName (i), i + 1);
    for (int i = 10; i < 15; ++i) voiceBox.addItem (voiceItemName (i), i + 1);
    voiceBox.addSectionHeading (tip::male_head());
    for (int i = 5;  i < 10; ++i) voiceBox.addItem (voiceItemName (i), i + 1);
    for (int i = 15; i < 20; ++i) voiceBox.addItem (voiceItemName (i), i + 1);
    voiceBox.setTooltip (tip::voicebox_tip() + "\n" + tip::note_override());
    voiceBox.onChange = [this]
    {
        const int id = voiceBox.getSelectedId();
        if (id > 0 && id <= gzzio::kNumVoicePresets)
        {
            applyVoicePreset (id);
            autoSetupMsg.clear();   // presets replace the auto result
            processor.apvts.state.setProperty ("ui_voice_preset", id, nullptr);
            infoText = juce::String::fromUTF8 (gzzio::kVoicePresets[id - 1].desc);
            voiceBox.setTooltip (infoText);
            repaint();
        }
    };
    addAndMakeVisible (voiceBox);

    micBox.setTextWhenNothingSelected (tip::mic_placeholder());
    fillMicBox (micBox);
    micBox.setTooltip (tip::micbox_tip() + "\n" + tip::note_override());
    micBox.onChange = [this]
    {
        const int id = micBox.getSelectedId();
        if (id > 0 && id <= gzzio::kNumMicPresets)
        {
            applyMicPreset (id);
            autoSetupMsg.clear();
            processor.apvts.state.setProperty ("ui_mic_preset", id, nullptr);
            infoText = juce::String::fromUTF8 (gzzio::kMicPresets[id - 1].desc);
            micBox.setTooltip (infoText);
            repaint();
        }
    };
    addAndMakeVisible (micBox);

    // EQ preset combo: famous whole-tone recipes with usage descriptions
    eqPresetBox.setTextWhenNothingSelected (tip::eqpreset_placeholder());
    for (int i = 0; i < gzzio::kNumEqPresets; ++i)
        eqPresetBox.addItem (eqItemName (i), i + 1);
    eqPresetBox.setTooltip (tip::eqpreset_tip() + "\n" + tip::note_override());
    eqPresetBox.onChange = [this]
    {
        const int id = eqPresetBox.getSelectedId();
        if (id > 0 && id <= gzzio::kNumEqPresets)
        {
            applyEqPreset (id);
            autoSetupMsg.clear();
            processor.apvts.state.setProperty ("ui_eq_preset", id, nullptr);
            infoText = juce::String::fromUTF8 (gzzio::kEqPresets[id - 1].desc);
            eqPresetBox.setTooltip (infoText);
            repaint();
        }
    };
    addAndMakeVisible (eqPresetBox);


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
    // v3.0-c ★シーンは「意思を持って先に選ぶ物」なので、
    //  (a) 選んだことを**プロジェクトに覚えさせる**（画面を開き直しても戻らない）
    //  (b) うた自動のあとに、シーンが決める7つだけ**戻す**
    //  ここに至った経緯: currentScene はどこにも保存されておらず、
    //  画面を開き直すと必ず「トーク配信」に戻っていた。さらに うた自動 は
    //  シーンが書いた width/doubler/delay/revsize/revmix/makeup を上書きするので、
    //  ボタンだけ光ったまま中身が消えていた（＝ボタンが嘘をついていた）。
    auto pickScene = [this] (int n)
    {
        currentScene = n;
        sceneChosen  = true;
        processor.apvts.state.setProperty ("ui_scene", n, nullptr);
        applyScene();
    };
    sceneSolo.onClick = [this, pickScene] { pickScene (0); };
    sceneTalk.onClick = [this, pickScene] { pickScene (1); };
    sceneBand.onClick = [this, pickScene] { pickScene (2); };

    // learn button (denoise profile capture)
    learnButton.setTooltip (tip::learn_tip() + "\n" + tip::note_learn());
    learnButton.setColour (juce::TextButton::buttonColourId, Palette::green.withAlpha (0.16f));
    learnButton.onClick = [this] { processor.requestDenoiseLearn(); };
    // v3.0 じどう学びなおし。LEARN の隣。パラメータなので保存にも載る。
    relearnBtn.setButtonText (tip::dn_relearn_label());
    relearnBtn.setTooltip (tip::dn_relearn_tip());
    relearnBtn.setClickingTogglesState (true);
    relearnBtn.setColour (juce::TextButton::buttonOnColourId, Palette::green);
    styleButton (relearnBtn);
    relearnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                     (processor.apvts, "dn_relearn", relearnBtn);
    addAndMakeVisible (relearnBtn);

    // v2.10.0 覚えたノイズ床を捨てて自動追従へ戻す。
    // v2.8.0 の fxWarnButton と同じ作法: **効いている間だけ**サーモン色で出て、
    // 押せばその場で消える。出ていること自体が「覚えている」の表示になる。
    dnClearButton.setButtonText (tip::dnlearn_done());
    dnClearButton.setTooltip (tip::dnclear_tip());
    dnClearButton.setColour (juce::TextButton::buttonColourId, Palette::salmon.withAlpha (0.30f));
    dnClearButton.onClick = [this]
    {
        processor.clearDenoiseLearn();
        infoText = tip::dnclear_tip();
        dnMsgTicks = 100;                      // 5秒だけ出す
        repaint();
    };
    styleButton (dnClearButton);
    addChildComponent (dnClearButton);         // 出すのは timerCallback だけ
    styleButton (learnButton);

    // ---- v3.0「つぶさない」（音量が上がっても潰れないモード）----
    //  「音量が上がったときに音が潰れないようにするモードも実装してください」への答え。
    //  置き場所は「2 音量をそろえる」の見出し。潰しているのは主にここ（圧縮）なので、
    //  効いている場所のとなりにスイッチがあるのが素直だと考えた。
    crushBtn.setButtonText (tip::crush_label());
    crushBtn.setTooltip (tip::crush_tip());
    crushBtn.setClickingTogglesState (true);          // ランプが付く＝ON/OFFが形で分かる
    crushBtn.setColour (juce::TextButton::buttonOnColourId, Palette::green);
    styleButton (crushBtn);
    crushAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                      (processor.apvts, "crush_on", crushBtn);
    addAndMakeVisible (crushBtn);

    startTimerHz (20);   // refresh learn/auto-setup state + stream-loudness meter

    // ---- v1.5.0 hero row: two one-press auto setups (talk 5 s / sing 8 s) ----
    autoSetupButton.setButtonText (tip::autoset_label());
    autoSetupButton.setTooltip (tip::autoset_tip());
    autoSetupButton.setColour (juce::TextButton::buttonColourId, Palette::ice.withAlpha (0.18f));
    autoSetupButton.onClick = [this]
    {
        processor.requestAutoSetup (0);
        autoSetupMsg.clear();
        repaint();
    };
    styleButton (autoSetupButton);
    addAndMakeVisible (autoSetupButton);

    songSetupButton.setButtonText (tip::autoset_sing_label());
    songSetupButton.setTooltip (tip::autoset_sing_tip());
    songSetupButton.setColour (juce::TextButton::buttonColourId, Palette::yellow);
    songSetupButton.setColour (juce::TextButton::textColourOffId, Palette::readableOn (Palette::yellow));
    songSetupButton.onClick = [this]
    {
        // v2.4.0 かんたんモード: 1回押すだけの10秒おまかせ。
        //   0.0-0.5s 間(話し終わりの声を拾わないための猶予)
        //   0.5-2.0s ノイズ学習(LEARNと同じ1.5秒。しずかにしてもらう)
        //   2.0-10.0s うた自動(8秒)
        if (! advancedMode)
        {
            if (easyComboPhase != 0 || processor.isAutoSetupRunning()) return;
            easyComboPhase = 1; easyComboTick = 0;
            autoSetupMsg.clear();
            repaint();
            return;
        }
        processor.requestAutoSetup (1);
        autoSetupMsg.clear();
        repaint();
    };
    styleButton (songSetupButton);
    addAndMakeVisible (songSetupButton);

    // TEMPO FIT: set delay/reverb lengths from the current BPM
    tempoFitButton.setButtonText (tip::tempofit_label());
    tempoFitButton.setTooltip (tip::tempofit_tip());
    tempoFitButton.setColour (juce::TextButton::buttonColourId, Palette::blue.withAlpha (0.16f));
    tempoFitButton.onClick = [this] { applyTempoFit(); };
    styleButton (tempoFitButton);
    addAndMakeVisible (tempoFitButton);

    // v2.8.0 かんたんモードで「見えないのに効いている」エフェクトの案内ボタン
    fxWarnButton.setButtonText (tip::fxwarn_label());
    fxWarnButton.setTooltip (tip::fxwarn_tip());
    fxWarnButton.setColour (juce::TextButton::buttonColourId, Palette::salmon.withAlpha (0.30f));
    fxWarnButton.onClick = [this] { clearHiddenFx(); };
    styleButton (fxWarnButton);
    addChildComponent (fxWarnButton);        // 出すのは updateHiddenFxWarning() だけ

    // KEY/SCALE detection (advanced, Effects tab): 8 s chroma capture -> K-S key
    keyScaleButton.setButtonText (tip::keyscale_label());
    keyScaleButton.setTooltip (tip::keyscale_tip());
    keyScaleButton.setColour (juce::TextButton::buttonColourId, Palette::ice.withAlpha (0.18f));
    keyScaleButton.onClick = [this] { processor.requestKeyScan (8.0); keyScaleMsg.clear(); chordMsg.clear(); repaint(); };
    styleButton (keyScaleButton);

    // CHORD-progression suggestion from the detected key (honest: not real chord detection)
    analyzeButton.setButtonText (tip::chord_label());
    analyzeButton.setTooltip (tip::chord_tip());
    analyzeButton.setColour (juce::TextButton::buttonColourId, Palette::blue.withAlpha (0.16f));
    analyzeButton.onClick = [this]
    {
        int tonic; bool minor; float conf;
        if (processor.getKeyResult (tonic, minor, conf))
        {
            chordMsg = tip::chord_prefix() + suggestChords (tonic, minor);
            analyzeMsgTtl = 20 * 8;
            infoText = chordMsg;
        }
        repaint();
    };
    styleButton (analyzeButton);

    styleButton (resetButton);
    resetButton.setColour (juce::TextButton::buttonColourId, Palette::salmon);
    resetButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    resetButton.setTooltip (tip::T ("\xe5\x85\xa8\xe3\x81\xa6\xe3\x81\xae\xe3\x83\x84\xe3\x83\x9e\xe3\x83\x9f\xe3\x82\x92\xe5\x88\x9d\xe6\x9c\x9f\xe5\x80\xa4\xe3\x81\xab\xe6\x88\xbb\xe3\x81\x97\xe3\x81\xbe\xe3\x81\x99", "Returns every knob to its default value") + "\n" + tip::note_reset());
    resetButton.onClick = [this]
    {
        for (auto* param : processor.getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
        auto& st = processor.apvts.state;
        st.setProperty ("ui_voice_preset", 0, nullptr);
        st.setProperty ("ui_mic_preset",   0, nullptr);
        st.setProperty ("ui_eq_preset",    0, nullptr);
        st.setProperty ("ui_char_preset",  0, nullptr);
        infoText.clear();
        fxInfoText.clear();
        autoSetupMsg.clear();
        refreshPresetDisplays();
        repaint();
    };

    styleButton (abA); styleButton (abB); styleButton (abCopy);
    styleButton (saveButton); styleButton (loadButton);
    abA.setClickingTogglesState (false);
    abB.setClickingTogglesState (false);
    abA.setColour (juce::TextButton::buttonOnColourId, Palette::yellow);
    abB.setColour (juce::TextButton::buttonOnColourId, Palette::yellow);
    // v2.1.0: A/Bはプロセッサ所有(MIDIスイッチからも切り替わるため、表示は
    // タイマーで abUiDirty を拾って追随する)
    abA.onClick    = [this] { processor.abSwitch (0); };
    abB.onClick    = [this] { processor.abSwitch (1); };
    abCopy.onClick = [this]
    {
        processor.abCopyToOther();
        abCopy.setButtonText (juce::String::fromUTF8 ("\xe2\x9c\x93"));   // tick
        juce::Component::SafePointer<VocalGzzioContent> safe (this);
        juce::Timer::callAfterDelay (450, [safe]() mutable { if (safe != nullptr) safe->updateABButtons(); });
    };
    saveButton.onClick = [this] { savePreset(); };
    loadButton.onClick = [this] { loadPreset(); };
   #if VOCALGZZIO_TRIAL
    // 体験版: 保存系は使えない。消すのではなく「押せない+理由」を出す
    // （消すと「機能が無い製品」に見える。フル版に有ることが伝わる形にする）。
    saveButton.setEnabled (false); loadButton.setEnabled (false);
    saveButton.setTooltip (tip::trial_nosave());
    loadButton.setTooltip (tip::trial_nosave());
    infoText = tip::trial_notice();          // 起動直後の案内欄で1回説明する
   #endif

    // ================= v1.4.0 =================
    // effects-tab knobs
    addKnob (duckK,     "duck",      tip::duck_label(),      tip::duck_tip(),      "%");
    addKnob (choAmtK,   "cho_amt",   tip::cho_label(),       tip::cho_tip(),       "%");
    addKnob (bpmK,      "bpm",       "BPM",                  tip::bpm_tip(),       "");
    addKnob (dlyMsK,    "dly_ms",    "TIME",                 tip::dly_ms_tip(),    "ms");
    addKnob (dlyFbK,    "dly_fb",    tip::dly_fb_label(),    tip::dly_fb_tip(),    "%");
    addKnob (dlyHcK,    "dly_hc",    tip::dly_hc_label(),    tip::dly_hc_tip(),    "Hz");
    addKnob (megaAmtK,  "mega_amt",  tip::mega_amt_label(),  tip::mega_amt_tip(),  "%");
    addKnob (roboFreqK, "robo_freq", tip::robo_freq_label(), tip::robo_freq_tip(), "Hz");
    addKnob (roboMixK,  "robo_mix",  tip::robo_mix_label(),  tip::robo_mix_tip(),  "%");

    // v1.8.0 voice changer (formant-preserving pitch shift) + 5-voice unison
    addKnob (vcPitchK, "vc_pitch", tip::vc_pitch_label(), tip::vc_pitch_tip(), "st");
    // v2.10.0 きょり（距離ならし）。マイクまわりの調整なので音量パネルへ。
    addKnob (proxK,    "prox_amt", tip::prox_label(),     tip::prox_tip(),     "%");
    addKnob (vcFormK,  "vc_form",  tip::vc_form_label(),  tip::vc_form_tip(),  "st");
    addKnob (jnMixK,   "jn_mix",   tip::jn_mix_label(),   tip::jn_mix_tip(),   "%");

    // v2.0.0 エモート: 息(小声で息づかい) + エモ(ロングトーンで響きが開く)
    addKnob (brK,  "br_amt",  tip::br_label(),  tip::br_tip(),  "%");
    addKnob (emoK, "emo_amt", tip::emo_label(), tip::emo_tip(), "%");
    vcOnButton.setClickingTogglesState (true);
    jnOnButton.setClickingTogglesState (true);
    vcOnButton.setButtonText (tip::vc_on_label());
    jnOnButton.setButtonText (tip::jn_on_label());
    vcOnAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                     (processor.apvts, "vc_on", vcOnButton);
    jnOnAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                     (processor.apvts, "jn_on", jnOnButton);
    addAndMakeVisible (vcOnButton);
    addAndMakeVisible (jnOnButton);
    // v2.0.0: DSP側は9モードあるのにUIは4項目しか出しておらず、「上5度」を選ぶと
    // 実際は3度下が鳴るなど表示と音がズレていた。全9項目を正しい順で並べる。
    for (int hi = 0; hi < 9; ++hi)
        jnHarmBox.addItem (tip::jn_harm_item (hi), hi + 1);
    jnHarmBox.setTooltip (tip::jn_harm_tip());
    jnHarmAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
                       (processor.apvts, "jn_harm", jnHarmBox);
    addAndMakeVisible (jnHarmBox);

    // ---- v1.9.0 auto-tune controls ----
    jnSoloButton.setClickingTogglesState (true);
    jnSoloButton.setButtonText (tip::jnsolo_label());
    jnSoloButton.setTooltip (tip::jnsolo_tip());
    jnSoloAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                       (processor.apvts, "jn_solo", jnSoloButton);
    addAndMakeVisible (jnSoloButton);

    // v2.9.0 セッションモード。旧「低遅延」ボタン(768→384サンプル)の置き換え。
    // 半分に減らしても、オンラインセッションでは自分の持ち分がほぼ残らないので
    // 足りない。「足さない(0サンプル)」を保証する構えにした。
    sessionButton.setClickingTogglesState (true);
    sessionButton.setButtonText (tip::session_label());
    sessionButton.setTooltip (tip::session_tip());
    // ★ONの色を指定しないと、既定の暗い地に暗い文字が乗って「セッション」が読めない
    //   (実起動のスクショで発覚)。追加遅延バッジと同じ緑にそろえる。
    sessionButton.setColour (juce::TextButton::buttonOnColourId, Palette::green);
    sessionButton.setColour (juce::TextButton::textColourOnId, Palette::readableOn (Palette::green));
    sessionAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                       (processor.apvts, "session", sessionButton);
    sessionButton.onClick = [this] { applySessionLock(); repaint(); };
    addAndMakeVisible (sessionButton);

    atOnButton.setClickingTogglesState (true);
    atOnButton.setButtonText (tip::at_on_label());
    atOnButton.setTooltip (tip::at_on_tip());
    atOnAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                     (processor.apvts, "at_on", atOnButton);
    addAndMakeVisible (atOnButton);

    // Key: language-neutral note names, filled straight from the choice param.
    fillComboFromChoiceParam (atKeyBox, "at_key");
    atKeyBox.setTooltip (tip::at_on_tip());
    atKeyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
                      (processor.apvts, "at_key", atKeyBox);
    addAndMakeVisible (atKeyBox);

    // Scale: bilingual labels rebuilt on language change (see refreshLanguage).
    for (int i = 0; i < 9; ++i) atScaleBox.addItem (scaleItemName (i), i + 1);
    atScaleBox.setTooltip (tip::at_on_tip());
    atScaleAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
                        (processor.apvts, "at_scale", atScaleBox);
    addAndMakeVisible (atScaleBox);

    auto setupAtSlider = [this] (juce::Slider& s, const juce::String& id, const juce::String& tt,
                                 std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& at)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
        s.setColour (juce::Slider::textBoxTextColourId, Palette::ink);
        s.setColour (juce::Slider::textBoxOutlineColourId, Palette::panelLn);
        s.setColour (juce::Slider::textBoxBackgroundColourId, Palette::track.withAlpha (0.35f));
        s.setTextValueSuffix (" %");
        s.setTooltip (tt);
        addAndMakeVisible (s);
        at = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, s);
    };
    setupAtSlider (atAmountSlider, "at_amount", tip::at_amount_tip(), atAmountAttach);
    setupAtSlider (atSpeedSlider,  "at_speed",  tip::at_speed_tip(),  atSpeedAttach);
    setupAtSlider (ornSlider,      "orn_amt",   tip::orn_tip(),       ornAttach);   // v2.7.0 こぶし

    // effects-tab combos (items mirror the choice parameters)
    fillComboFromChoiceParam (revTypeBox, "rev_type");
    revTypeBox.setTooltip (tip::rev_type_tip());
    revTypeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
                        (processor.apvts, "rev_type", revTypeBox);
    revTypeBox.onChange = [this]
    {
        const juce::String d[7] = { tip::rev_desc_normal(), tip::rev_desc_room(),
                                    tip::rev_desc_plate(),  tip::rev_desc_hall(),
                                    tip::rev_desc_church(), tip::rev_desc_spring(),
                                    tip::rev_desc_shimmer() };
        // v4.0.0: 7 以上は「へや」。ここを jlimit(0,6) のままにしておくと
        //         どの部屋を選んでもシマーの説明が出る。
        const int sel = juce::jlimit (0, kRevTypeCount - 1, revTypeBox.getSelectedId() - 1);
        const juce::String desc = (sel >= kHeyaFirst) ? tip::rev_desc_heya (sel - kHeyaFirst)
                                                      : d[sel];
        fxInfoText = desc;
        infoText   = desc;   // v1.6.0: the combo lives in section 4, so explain there too
        revTypeBox.setTooltip (tip::rev_type_tip() + "\n" + desc);
        repaint();
    };

    fillComboFromChoiceParam (dlySyncBox, "dly_sync");
    dlySyncBox.setTooltip (tip::dly_sync_tip());
    dlySyncAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
                        (processor.apvts, "dly_sync", dlySyncBox);
    dlySyncBox.onChange = [this]
    {
        const bool msMode = (dlySyncBox.getSelectedId() == 1);
        dlyMsK.slider.setEnabled (msMode);
        dlyMsK.slider.setAlpha (msMode ? 1.0f : 0.4f);
        repaint();
    };

    fillComboFromChoiceParam (megaTypeBox, "mega_type");
    megaTypeBox.setTooltip (tip::mega_type_tip());
    megaTypeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
                         (processor.apvts, "mega_type", megaTypeBox);
    megaTypeBox.onChange = [this]
    {
        const juce::String d[3] = { tip::mega_desc_kakusei(), tip::mega_desc_radio(),
                                    tip::mega_desc_lofi() };
        fxInfoText = d[juce::jlimit (0, 2, megaTypeBox.getSelectedId() - 1)];
        repaint();
    };

    // character-voice presets (robot + megaphone combos, display persisted)
    charBox.setTextWhenNothingSelected (tip::char_placeholder());
    charBox.setTooltip (tip::char_tip());
    charBox.setJustificationType (juce::Justification::centredLeft);
    for (int i = 0; i < gzzio::kNumCharPresets; ++i)
        charBox.addItem (charItemName (i), i + 1);
    charBox.onChange = [this]
    {
        const int id = charBox.getSelectedId();
        if (id <= 0) return;
        const auto& c = gzzio::kCharPresets[juce::jlimit (0, gzzio::kNumCharPresets - 1, id - 1)];
        auto setP = [this] (const juce::String& pid, float v)
        {
            if (auto* prm = processor.apvts.getParameter (pid))
                prm->setValueNotifyingHost (processor.apvts.getParameterRange (pid).convertTo0to1 (v));
        };
        setP ("robo_freq", c.roboFreq);
        setP ("robo_mix",  c.roboMix);
        setP ("mega_type", (float) c.megaType);
        setP ("mega_amt",  c.megaAmt);
        processor.apvts.state.setProperty ("ui_char_preset", id, nullptr);
        fxInfoText = juce::String::fromUTF8 (c.desc);
        charBox.setTooltip (fxInfoText);
        repaint();
    };
    addAndMakeVisible (charBox);

    // v2.1.0 MIDIスイッチ設定(フットスイッチ/パッド割当)
    midiButton.setButtonText (tip::midi_btn_label());
    midiButton.setTooltip (tip::midi_btn_tip());
    midiButton.setColour (juce::TextButton::buttonColourId, Palette::green.withAlpha (0.16f));
    styleButton (midiButton);
    midiButton.onClick = [this]
    {
        auto panel = std::make_unique<MidiMapPanel> (processor);
        juce::CallOutBox::launchAsynchronously (std::move (panel),
                                                midiButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (midiButton);

    // v2.10.0 点検（ゼロ遅延の自己証明／名前ごとの設定／不具合の手がかり）
    checkupButton.setButtonText (tip::checkup_label());
    checkupButton.setTooltip (tip::checkup_tip());
    checkupButton.setColour (juce::TextButton::buttonColourId, Palette::ice.withAlpha (0.20f));
    styleButton (checkupButton);
    checkupButton.onClick = [this]
    {
        auto panel = std::make_unique<CheckupPanel> (processor);
        juce::CallOutBox::launchAsynchronously (std::move (panel),
                                                checkupButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (checkupButton);

    // v2.2.0 配信出力(単体起動版のみ。プラグイン版はホストが配線するので出さない)
    streamButton.setButtonText (tip::so_btn_label());
    streamButton.setTooltip (tip::so_btn_tip());
    streamButton.setColour (juce::TextButton::buttonColourId, Palette::salmon.withAlpha (0.18f));
    styleButton (streamButton);
    streamButton.onClick = [this]
    {
        auto panel = std::make_unique<StreamOutPanel> (processor);
        juce::CallOutBox::launchAsynchronously (std::move (panel),
                                                streamButton.getScreenBounds(), nullptr);
    };
    addChildComponent (streamButton);   // 表示は applyTabVisibility 側で決める

    // TAP tempo: average the last few intervals into the BPM parameter
    tapButton.setTooltip (tip::tap_tip());
    tapButton.setColour (juce::TextButton::buttonColourId, Palette::blue.withAlpha (0.16f));
    styleButton (tapButton);
    tapButton.onClick = [this]
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (lastTapMs > 0.0 && now - lastTapMs < 2000.0 && now > lastTapMs + 150.0)
        {
            const float bpmNow = (float) (60000.0 / (now - lastTapMs));
            tapBpm = tapCount > 0
                       ? tapBpm + (bpmNow - tapBpm) / (float) juce::jmin (tapCount + 1, 4)
                       : bpmNow;
            ++tapCount;
            if (auto* prm = processor.apvts.getParameter ("bpm"))
                prm->setValueNotifyingHost (processor.apvts.getParameterRange ("bpm")
                    .convertTo0to1 (juce::jlimit (50.0f, 300.0f, tapBpm)));
        }
        else
        {
            tapCount = 0;
            tapBpm = 0.0f;
        }
        lastTapMs = now;
    };

    // right-column tabs (EQ | effects)
    tabFxButton.setButtonText (tip::fx_tab_fx());
    auto setupTab = [this] (juce::TextButton& b, bool on)
    {
        b.setClickingTogglesState (true);
        b.setRadioGroupId (2003);
        b.setToggleState (on, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonOnColourId, Palette::blue);
        styleButton (b);
    };
    setupTab (tabEqButton, true);
    setupTab (tabFxButton, false);
    tabEqButton.onClick = [this] { currentTab = 0; if (onUiStateChange) onUiStateChange ("ui_tab", 0); updateTabVisibility(); };
    tabFxButton.onClick = [this] { currentTab = 1; if (onUiStateChange) onUiStateChange ("ui_tab", 1); updateTabVisibility(); };
    //  検査が**本物のタブを押す**ための名札（へんしんの中身は「エフェクト」側にある）
    tabEqButton.setComponentID ("tabEq");
    tabFxButton.setComponentID ("tabFx");

    // v2.4.0: 「かんたん/こだわり」ボタンは廃止。ヘッダの大きなスイッチ
    //         (drawThemeCross / mouseDown 内 idx=6) が同じ役目を担う。

    // red module lamps (knob-centre on/off)
    addLamp (lampGate, "gate_on");
    addLamp (lampDn,   "dn_on");
    addLamp (lampDs,   "ds_on");
    addLamp (lampDbl,  "dbl_on");
    addLamp (lampDly,  "dly_on");
    addLamp (lampRev,  "revon");
    addLamp (lampMega, "mega_on");
    addLamp (lampCho,  "cho_on");
    addLamp (lampRobo, "robo_on");
    // ==========================================

    // font-size slider (text only)
    fontSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    fontSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
    fontSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    fontSlider.valueFromTextFunction = [] (const juce::String& t)
    // v2.8.0: スライダーは150%まであるのに、数値を打ち込むと120%で止まっていた。
    { return juce::jlimit (0.80, 1.50, t.retainCharacters ("0123456789.").getDoubleValue() / 100.0); };
    fontSlider.setColour (juce::Slider::textBoxTextColourId, Palette::ink);
    fontSlider.setColour (juce::Slider::textBoxOutlineColourId, Palette::panelLn);
    fontSlider.setColour (juce::Slider::textBoxBackgroundColourId, Palette::track.withAlpha (0.35f));
    fontSlider.setRange (0.80, 1.50, 0.01);   // v1.5.0: up to 150 percent
    fontSlider.setValue (1.0, juce::dontSendNotification);
    fontSlider.setDoubleClickReturnValue (true, 1.0);
    fontSlider.setTooltip (tip::fontsize_tip());
    fontSlider.onValueChange = [this]
    {
        if (onFontChange) onFontChange ((float) fontSlider.getValue());
    };
    addAndMakeVisible (fontSlider);

    fontSliderLabel.setText (tip::fontsize_label(), juce::dontSendNotification);
    fontSliderLabel.setJustificationType (juce::Justification::centredRight);
    fontSliderLabel.getProperties().set ("fontH", 11.5);
    fontSliderLabel.getProperties().set ("bold", true);
    fontSliderLabel.setTooltip (tip::fontsize_tip());
    addAndMakeVisible (fontSliderLabel);

    // window-zoom slider (geometry ratio)
    zoomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
    zoomSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    zoomSlider.valueFromTextFunction = [] (const juce::String& t)
    { return juce::jlimit (0.70, 1.40, t.retainCharacters ("0123456789.").getDoubleValue() / 100.0); };
    zoomSlider.setColour (juce::Slider::textBoxTextColourId, Palette::ink);
    zoomSlider.setColour (juce::Slider::textBoxOutlineColourId, Palette::panelLn);
    zoomSlider.setColour (juce::Slider::textBoxBackgroundColourId, Palette::track.withAlpha (0.35f));
    zoomSlider.setRange (0.70, 1.40, 0.01);
    zoomSlider.setValue (1.00, juce::dontSendNotification);
    zoomSlider.setDoubleClickReturnValue (true, 1.00);
    zoomSlider.setTooltip (tip::zoom_tip());
    zoomSlider.onValueChange = [this]
    {
        if (onScaleChange) onScaleChange ((float) zoomSlider.getValue());
    };
    addAndMakeVisible (zoomSlider);

    zoomSliderLabel.setText (tip::zoom_label(), juce::dontSendNotification);
    zoomSliderLabel.setJustificationType (juce::Justification::centredRight);
    zoomSliderLabel.getProperties().set ("fontH", 11.5);
    zoomSliderLabel.getProperties().set ("bold", true);
    zoomSliderLabel.setTooltip (tip::zoom_tip());
    addAndMakeVisible (zoomSliderLabel);

    // v2.12.0 ヘッダの極小スライダー(高さ13px)は廃止して、ボタン→大きな
    // スライダーの吹き出しに。元の2本は値の器として残す(非表示)。
    fontSlider.setVisible (false); fontSliderLabel.setVisible (false);
    zoomSlider.setVisible (false); zoomSliderLabel.setVisible (false);
    // v3.1 §4 「ぜんたい」の列に常設したとき、検査から名指しで見つけられるように。
    fontSlider.setComponentID ("uiFontSize");
    zoomSlider.setComponentID ("uiZoom");
    // v2.12.0: テーマもここへ引っ越したので、名前は「文字・見た目」。
    //  ★英語(ブランド)テーマで「Text・見た目」と混ざっていたので T() を通す。
    sizePopBtn.setButtonText (tip::look_label());
    sizePopBtn.setTooltip (tip::fontsize_tip());
    styleButton (sizePopBtn);
    sizePopBtn.onClick = [this]
    {
        // 大きいスライダー2本の吹き出し。動かすと隠れている元スライダーへ
        // 値を送る→既存の onValueChange がそのまま働く(保存・復元も従来どおり)。
        class SizePanel : public juce::Component
        {
        public:
            SizePanel (VocalGzzioContent& owner, juce::Slider& fontS, juce::Slider& zoomS)
                : ow (owner)
            {
                auto setup = [this] (juce::Slider& big, juce::Slider& src,
                                     juce::Label& lab, const juce::String& text)
                {
                    lab.setText (text, juce::dontSendNotification);
                    lab.getProperties().set ("fontH", 17.0);
                    lab.getProperties().set ("bold", true);
                    addAndMakeVisible (lab);
                    big.setSliderStyle (juce::Slider::LinearHorizontal);
                    big.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 30);
                    big.setColour (juce::Slider::textBoxTextColourId, Palette::ink);
                    big.setColour (juce::Slider::textBoxOutlineColourId, Palette::panelLn);
                    big.setColour (juce::Slider::textBoxBackgroundColourId,
                                   Palette::track.withAlpha (0.35f));
                    // ★表示関数は setValue より前に。JUCE は setValue の時点で
                    //   文字を作るので、後から入れると「1.00」のままになる(実測)。
                    big.textFromValueFunction = [] (double v)
                        { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
                    big.valueFromTextFunction = [&src] (const juce::String& t)
                        { return juce::jlimit (src.getMinimum(), src.getMaximum(),
                                 t.retainCharacters ("0123456789.").getDoubleValue() / 100.0); };
                    big.setRange (src.getMinimum(), src.getMaximum(), src.getInterval());
                    big.setValue (src.getValue(), juce::dontSendNotification);
                    big.setDoubleClickReturnValue (true, 1.0);
                    big.updateText();
                    big.onValueChange = [&big, &src]
                        { src.setValue (big.getValue(), juce::sendNotificationSync); };
                    addAndMakeVisible (big);
                };
                setup (bigFont, fontS, labFont, tip::fontsize_label());
                setup (bigZoom, zoomS, labZoom, tip::zoom_label());

                // ---- v3.1「ぜんぶ表示」（使いかた4種の逃げ道）----
                //  使いかたで通らないツマミも画面に出す。上級者向けなので
                //  ヘッダには置かず、ここ（文字・見た目の吹き出し）に置く。
                showAllBtn.setButtonText (tip::showall_label());
                showAllBtn.setTooltip (tip::showall_tip());
                showAllBtn.setClickingTogglesState (true);
                showAllBtn.setToggleState (ow.showAllKnobs, juce::dontSendNotification);
                showAllBtn.onClick = [this]
                {
                    ow.showAllKnobs = showAllBtn.getToggleState();
                    ow.processor.apvts.state.setProperty ("ui_show_all",
                                                          ow.showAllKnobs, nullptr);
                    ow.applyKnobVisibility();
                    ow.resized();
                    ow.repaint();
                    refresh();
                };
                addAndMakeVisible (showAllBtn);

                // ---- v2.12.0 テーマ6枚（ヘッダのチップから引っ越し）----
                // ヘッダでは 58x21px・文字11.5px固定という、画面でいちばん
                // 読みにくい札だった。ここでは 132x40px・文字16pxで置ける。
                labTheme.setText (tip::hue_label(), juce::dontSendNotification);
                labTheme.getProperties().set ("fontH", 17.0);
                labTheme.getProperties().set ("bold", true);
                addAndMakeVisible (labTheme);
                for (int i = 0; i < kThemeCount; ++i)
                {
                    auto* b = themeBtns.add (new juce::TextButton());
                    b->setClickingTogglesState (true);   // ランプが付く＝ON/OFFが形で分かる
                    b->setRadioGroupId (7801);
                    b->onClick = [this, i] { ow.chooseTheme (i); refresh(); };
                    addAndMakeVisible (b);
                }
                refresh();
                setSize (420, 306);   // v3.1「ぜんぶ表示」の1行ぶん高くした
            }

            // テーマを変えると Palette が丸ごと入れ替わるので、色も名前も貼り直す。
            // (ゆるふわの席は押すたび ひる<->よる で名前が変わる)
            void refresh()
            {
                for (int i = 0; i < themeBtns.size(); ++i)
                {
                    auto* b = themeBtns[i];
                    b->setButtonText (ow.themeArmName (i));
                    b->setToggleState (ow.themeArmSelected (i), juce::dontSendNotification);
                    b->setColour (juce::TextButton::buttonColourId,   Palette::panel2);
                    b->setColour (juce::TextButton::buttonOnColourId, Palette::yellow);
                    b->setColour (juce::TextButton::textColourOffId,  Palette::ink);
                    b->setColour (juce::TextButton::textColourOnId,   Palette::bgTop);
                }
                showAllBtn.setColour (juce::TextButton::buttonColourId,   Palette::panel2);
                showAllBtn.setColour (juce::TextButton::buttonOnColourId, Palette::yellow);
                showAllBtn.setColour (juce::TextButton::textColourOffId,  Palette::ink);
                showAllBtn.setColour (juce::TextButton::textColourOnId,   Palette::bgTop);
                for (auto* s : { &bigFont, &bigZoom })
                {
                    s->setColour (juce::Slider::textBoxTextColourId, Palette::ink);
                    s->setColour (juce::Slider::textBoxOutlineColourId, Palette::panelLn);
                    s->setColour (juce::Slider::textBoxBackgroundColourId,
                                  Palette::track.withAlpha (0.35f));
                }
                if (auto* p = getParentComponent()) p->repaint();   // 吹き出しの枠ごと
                repaint();
            }

            void paint (juce::Graphics& g) override
            {   // テーマの紙色で塗る(既定のCallOutBoxは黒っぽく、明るいテーマで浮く)
                g.fillAll (Palette::panel);
                g.setColour (Palette::panelLn.withAlpha (0.6f));
                g.drawLine (10.0f, 118.0f, 410.0f, 118.0f, 1.0f);
            }
            void resized() override
            {
                labFont.setBounds (10,  12, 88, 34);
                bigFont.setBounds (104, 12, 284, 34);
                labZoom.setBounds (10,  64, 88, 34);
                bigZoom.setBounds (104, 64, 284, 34);
                labTheme.setBounds (10, 128, 400, 24);
                for (int i = 0; i < themeBtns.size(); ++i)
                {
                    // v2.12.0: 軽量を外して5枚になった。3+2 で並べ、下段の2枚は
                    //  幅を広げて余白を残さない（欠けた席が空いて見えるのを避ける）。
                    const int n2 = kThemeCount - 3;                       // 下段の枚数
                    if (i < 3) themeBtns[i]->setBounds (10 + i * 137, 158, 130, 40);
                    else
                    {
                        const int w = (400 - (n2 - 1) * 7) / juce::jmax (1, n2);
                        themeBtns[i]->setBounds (10 + (i - 3) * (w + 7), 206, w, 40);
                    }
                }
                showAllBtn.setBounds (10, 258, 400, 38);   // v3.1 テーマの下に1本
            }
        private:
            VocalGzzioContent& ow;
            juce::Slider bigFont, bigZoom;
            juce::Label  labFont, labZoom, labTheme;
            juce::OwnedArray<juce::TextButton> themeBtns;
            juce::TextButton showAllBtn;                   // v3.1
        };
        auto panel = std::make_unique<SizePanel> (*this, fontSlider, zoomSlider);
        // 親に this を渡す → GzzioLnF とテーマ色を継承する(nullptrだと素のJUCEの
        // 黒い吹き出しになり、明るいテーマで1か所だけ浮いてしまう)
        juce::CallOutBox::launchAsynchronously (std::move (panel),
            sizePopBtn.getBounds(), this);
    };
    addAndMakeVisible (sizePopBtn);

    // ---- v3.0「効果のオンオフ」----
    //  いちばん最初の指摘「各エフェクトのオンオフを選べるようにしてほしい。
    //  De-noiseだけ使いたいのに他がかかる」への、画面側の答え。
    //  ★本番の画面（左＝音のとおり道のカード）は v3.0-c で作る。ここは
    //   「作った機能に、いま手が届く」ための仮の入口。吹き出しにしたのは、
    //   ヘッダに8個ぶんの場所が無く、無理に押し込むと**また文字が切れる**から。
    modPopBtn.setButtonText (tip::mods_label());
    modPopBtn.setTooltip (tip::mods_tip());
    styleButton (modPopBtn);
    modPopBtn.onClick = [this]
    {
        // v3.0-c「音のとおり道」のカード。設計書の左1/4に置く物の**中身**を先に作る。
        //  カード = 番号 / 名前 / ひとこと / ON-OFF。上から下へ＝音の流れ。
        //  置き場所（左の1列）は次の回。中身と置き場所を同時に動かすと、
        //  どちらが原因で崩れたのか分からなくなるので、順番に出す。
        class ModPanel : public juce::Component
        {
        public:
            ModPanel (VocalGzzioProcessor& pr) : proc (pr)
            {
                lab.setText (tip::mods_path(), juce::dontSendNotification);
                lab.getProperties().set ("fontH", 18.0);
                lab.getProperties().set ("bold", true);
                addAndMakeVisible (lab);

                for (int m = 0; m < gz::ModuleChain::Count; ++m)
                {
                    auto* b = btns.add (new juce::TextButton());
                    b->setClickingTogglesState (true);
                    b->setColour (juce::TextButton::buttonOnColourId, Palette::green);
                    b->setColour (juce::TextButton::textColourOnId,  Palette::bgTop);
                    b->setColour (juce::TextButton::textColourOffId, Palette::ink);
                    b->setColour (juce::TextButton::buttonColourId,  Palette::panel2);
                    atts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment
                                  (proc.apvts, gz::ModuleChain::paramId (m), *b));
                    // ★カードの地・番号・文字は**親**が描いている。スイッチは自分の
                    //  四角だけを描き直すので、これが無いとカードの片側だけ古いまま
                    //  残る（実際、番号が半分だけ緑に残った）。押されたら全部描き直す。
                    b->onClick = [this] { repaint(); };
                    addAndMakeVisible (b);
                }
                auto setAll = [this] (bool on)
                {
                    for (int m = 0; m < gz::ModuleChain::Count; ++m)
                        if (auto* prm = proc.apvts.getParameter (gz::ModuleChain::paramId (m)))
                        {
                            prm->beginChangeGesture();
                            prm->setValueNotifyingHost (on ? 1.0f : 0.0f);
                            prm->endChangeGesture();
                        }
                };
                allOn.setButtonText (tip::mods_all_on());
                allOff.setButtonText (tip::mods_all_off());
                allOn.onClick  = [this, setAll] { setAll (true);  repaint(); };
                allOff.onClick = [this, setAll] { setAll (false); repaint(); };
                for (auto* b : { &allOn, &allOff })
                {
                    b->setColour (juce::TextButton::buttonColourId, Palette::panel2);
                    b->setColour (juce::TextButton::textColourOffId, Palette::ink);
                    addAndMakeVisible (b);
                }
                setSize (kW, 44 + gz::ModuleChain::Count * kCardH + 54);
            }

            void paint (juce::Graphics& g) override
            {
                g.fillAll (Palette::panel);
                for (int m = 0; m < gz::ModuleChain::Count; ++m)
                {
                    const juce::Rectangle<int> card (10, 40 + m * kCardH, kW - 20, kCardH - 6);
                    const bool on = btns[m]->getToggleState();

                    g.setColour (on ? Palette::panel2 : Palette::panel2.withAlpha (0.45f));
                    g.fillRoundedRectangle (card.toFloat(), 10.0f);
                    g.setColour (Palette::panelLn.withAlpha (on ? 0.9f : 0.4f));
                    g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10.0f, 1.0f);

                    // 番号。上から下へ＝音の流れであることを、数字で明示する。
                    g.setColour (on ? Palette::accentOn (Palette::green, Palette::panel2)
                                    : Palette::inkSoft.withAlpha (0.5f));
                    g.setFont (GzzioLnF::uiFont (17.0f, true));
                    g.drawText (juce::String (m + 1), card.withWidth (30).translated (8, 0),
                                juce::Justification::centred);

                    // 名前（大）とひとこと（小）。OFF は薄くして、切れているのを見せる。
                    g.setColour (Palette::ink.withAlpha (on ? 1.0f : 0.45f));
                    g.setFont (GzzioLnF::uiFont (17.0f, true));
                    g.drawText (juce::String::fromUTF8 (tip::english ? gz::ModuleChain::enName (m)
                                                                     : gz::ModuleChain::jpName (m)),
                                card.withTrimmedLeft (38).withTrimmedRight (86).withHeight (24)
                                    .translated (0, 5), juce::Justification::centredLeft);
                    g.setColour (Palette::inkSoft.withAlpha (on ? 0.95f : 0.4f));
                    g.setFont (GzzioLnF::uiFont (14.0f, false));
                    g.drawText (juce::String::fromUTF8 (tip::english ? gz::ModuleChain::enNote (m)
                                                                     : gz::ModuleChain::jpNote (m)),
                                card.withTrimmedLeft (38).withTrimmedRight (86)
                                    .withHeight (20).translated (0, 28),
                                juce::Justification::centredLeft);
                }
            }

            void resized() override
            {
                lab.setBounds (12, 8, kW - 24, 28);
                for (int m = 0; m < btns.size(); ++m)
                    btns[m]->setBounds (kW - 10 - 78, 40 + m * kCardH + 12, 70, kCardH - 30);
                const int y = 40 + gz::ModuleChain::Count * kCardH + 6;
                allOn .setBounds (10, y, (kW - 28) / 2, 36);
                allOff.setBounds (kW - 10 - (kW - 28) / 2, y, (kW - 28) / 2, 36);
            }
        private:
            enum { kW = 340, kCardH = 58 };   // ローカルクラスは static メンバを持てない
            VocalGzzioProcessor& proc;
            juce::Label lab;
            juce::OwnedArray<juce::TextButton> btns;
            juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> atts;
            juce::TextButton allOn, allOff;
        };
        juce::CallOutBox::launchAsynchronously (std::make_unique<ModPanel> (processor),
                                                modPopBtn.getBounds(), this);
    };
    addChildComponent (modPopBtn);        // v3.0-c: かんたんモードのときだけ出す

    tuningPopBtn.setComponentID ("openTuner");
    tuningPopBtn.setTooltip (juce::String::fromUTF8 ("大きな音程表示を開きます。ギターの弦を一本ずつ合わせるときにも使えます。"));
    tuningPopBtn.onClick = [this]
    {
        auto largeTuner = std::make_unique<VocalTuner> (processor);
        const int source = (int) processor.apvts.getRawParameterValue ("src_mode")->load();
        largeTuner->setGuitarMode (source == 1 || source == 3);
        largeTuner->setRangeToolsVisible (true);
        largeTuner->setSize (900, 340);
        juce::CallOutBox::launchAsynchronously (std::move (largeTuner), tuningPopBtn.getBounds(), this);
    };
    styleButton (tuningPopBtn);

    // v3.1 §4 使いかたで中身が消える箱の言い訳（ふだんは隠れている）
    focusEmpty.setComponentID ("focusEmptyNote");
    focusEmpty.setInterceptsMouseClicks (false, false);
    addChildComponent (focusEmpty);

    // v3.0-c 左の常設列。縦に入り切らないときだけ縦スクロールが出る
    //  （文字を150%にすると8枚では収まらなくなる。縮めるより送る）。
    // v3.1 §4「1つずつ」の配線。
    pathRail.onFocusToggled = [this] (bool on)
    {
        focusMode = on;
        if (onUiStateChange) onUiStateChange ("ui_focus", on ? 1 : 0);
        pathRail.setSelected (on ? focusModule : -1);
        applyKnobVisibility();
        resized();
        repaint();
    };
    pathRail.onSelect = [this] (int mod) { if (focusMode) applyFocusSelection (mod); };
    // その1枚だけ くらべる。離し忘れ・画面を閉じたときは必ず -1 に戻す。
    addAndMakeVisible (focusCompare);
    focusCompare.onDown = [this]
    { processor.compareOne.store (focusModule, std::memory_order_relaxed); repaint(); };
    focusCompare.onUp   = [this]
    { processor.compareOne.store (-1, std::memory_order_relaxed); repaint(); };
    focusCompare.setButtonText (tip::compare_label());
    focusCompare.setTooltip (tip::compare_tip());

    railView.setViewedComponent (&pathRail, false);
    railView.setScrollBarsShown (true, false);
    railView.setScrollBarThickness (10);
    addAndMakeVisible (railView);

    addAndMakeVisible (tuner);
    addAndMakeVisible (eqGraph);
    initialiseOverview();

    // update notice: hidden until the background check finds a newer release
    updateNotice.setFont (GzzioLnF::uiFont (14.0f, true), false, juce::Justification::centredRight);
    updateNotice.setColour (juce::HyperlinkButton::textColourId, Palette::yellow);
    updateNotice.setTooltip (tip::T ("\xe3\x83\x80\xe3\x82\xa6\xe3\x83\xb3\xe3\x83\xad\xe3\x83\xbc\xe3\x83\x89\xe3\x83\x9a\xe3\x83\xbc\xe3\x82\xb8\xe3\x82\x92\xe9\x96\x8b\xe3\x81\x8d\xe3\x81\xbe\xe3\x81\x99", "Opens the download page"));   // opens the download page
    addChildComponent (updateNotice);
    startUpdateCheck();

    refreshPresetDisplays();

    // (v2.1.0: A/Bスロットの初期化はプロセッサ側。空スロットへの切替は音を変えない)
    updateABButtons();

    updateSeqModeVisibility();
    // v3.1 開いた時点でも「使いかた」のふるいを掛ける（保存状態から復元した直後）。
    //  updateTabVisibility() だけだと、ことば・艶・ひびきなど主ツマミ側が出たままになる。
    applyKnobVisibility();
    dlySyncBox.onChange();   // sync the TIME knob enabled state
    applySessionLock();      // v2.9.0: 保存状態がセッションONなら開いた時点で灰色に
    // 既定サイズの正本は VocalGzzioEditor::baseW / baseH（そこだけを直す）。
    //  ここで別の数字を書くと、エディタ側の content.setSize(baseW,baseH) に
    //  上書きされて**効かない**。実際 1360×858 のまま組まれていた。
    setSize (VocalGzzioEditor::baseW, VocalGzzioEditor::baseH);
}


//==============================================================================
// Update check: one background HTTPS GET to the GitHub Releases API when the
// editor opens. Silent on any failure (offline, timeout, parse error).
//==============================================================================
static bool gzzioIsNewerVersion (const juce::String& latestTag, const juce::String& currentVer)
{
    auto clean = [] (juce::String s) { return s.trim().trimCharactersAtStart ("vV"); };
    const auto a = juce::StringArray::fromTokens (clean (latestTag), ".", "");
    const auto b = juce::StringArray::fromTokens (clean (currentVer), ".", "");
    for (int i = 0; i < 3; ++i)
    {
        const int ai = a[i].getIntValue();
        const int bi = b[i].getIntValue();
        if (ai != bi) return ai > bi;
    }
    return false;
}

void VocalGzzioContent::startUpdateCheck()
{
    // v2.3.0 重要な修正(Cubase 13が終了できない問題):
    //   ここは画面を開くたびに「切り離したスレッド」でGitHubへ通信していた。
    //   そのスレッドのコードはプラグインのDLL内にあるため、通信中(最長4秒)に
    //   DAWが終了してモジュールを解放しようとすると、解放がスレッドの終了を
    //   待って止まる = ホストが固まる。インサートから外すと終了できたのはこのため。
    //   → プラグイン版では通信しない(更新確認は単体起動版とホームページで)。
    //     加えて1プロセスにつき1回だけ、待ち時間も短くする。
    if (! processor.isStandalone()) return;

    static std::atomic<bool> alreadyChecked { false };
    if (alreadyChecked.exchange (true)) return;

    juce::Component::SafePointer<VocalGzzioContent> safe (this);
    juce::Thread::launch ([safe]
    {
        juce::URL url ("https://api.github.com/repos/gzzio1989/VocalGzzio/releases/latest");
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs (2500)
                        .withExtraHeaders ("User-Agent: VocalGzzio-UpdateCheck\r\n");
        std::unique_ptr<juce::InputStream> stream (url.createInputStream (opts));
        if (stream == nullptr)
            return;
        const auto body = stream->readEntireStreamAsString();
        const auto tag  = juce::JSON::parse (body).getProperty ("tag_name", juce::String()).toString();
        if (tag.isEmpty() || ! gzzioIsNewerVersion (tag, JucePlugin_VersionString))
            return;
        juce::MessageManager::callAsync ([safe, tag]
        {
            if (safe != nullptr)
                safe->showUpdateNotice (tag);
        });
    });
}

void VocalGzzioContent::showUpdateNotice (const juce::String& versionTag)
{
    updateNotice.setButtonText (tip::T ("\xe6\x96\xb0\xe3\x81\x97\xe3\x81\x84\xe3\x83\x90\xe3\x83\xbc\xe3\x82\xb8\xe3\x83\xa7\xe3\x83\xb3\x20", "Version ")   // new version
                                + versionTag
                                + tip::T ("\x20\xe3\x82\x92\xe5\x85\xac\xe9\x96\x8b\xe4\xb8\xad\x20\xe2\x86\x92\x20\xe3\x82\xaf\xe3\x83\xaa\xe3\x83\x83\xe3\x82\xaf\xe3\x81\xa7\xe5\x85\xa5\xe6\x89\x8b", " is out \xe2\x86\x92 click to get it"));   // available, click to get
    updateNotice.setVisible (true);
}

// Restore the three preset combo displays from properties stored in the plugin
// state. Display only: parameters are NOT re-applied, so manual knob tweaks
// made after picking a preset are preserved.
void VocalGzzioContent::refreshPresetDisplays()
{
    auto& st = processor.apvts.state;
    const int v = juce::jlimit (0, gzzio::kNumVoicePresets, (int) st.getProperty ("ui_voice_preset", 0));
    const int m = juce::jlimit (0, gzzio::kNumMicPresets,   (int) st.getProperty ("ui_mic_preset",   0));
    const int e = juce::jlimit (0, gzzio::kNumEqPresets,    (int) st.getProperty ("ui_eq_preset",    0));

    voiceBox   .setSelectedId (v, juce::dontSendNotification);
    micBox     .setSelectedId (m, juce::dontSendNotification);
    eqPresetBox.setSelectedId (e, juce::dontSendNotification);

    // v3.0-c シーン（弾き語り/トーク配信/Band）も、選ばれていれば復元する。
    //  -1 = まだ一度も選んでいない（＝うた自動のあとに戻す対象にしない）
    {
        // v3.1「ぜんぶ表示」も保存状態から戻す（プロジェクトごとに残る）
        showAllKnobs = (bool) st.getProperty ("ui_show_all", false);
        // v3.1 §4 見ていた1枚は戻す。「1つずつ」そのものは戻さない（覚えない）。
        //  ★ここは音質プリセットを選び直したときにも呼ばれる。以前は focusMode まで
        //   戻していたので、1つずつで作業中にプリセットを選ぶと画面が飛んだ。
        focusModule = juce::jlimit (0, 9, (int) st.getProperty ("ui_focus_mod", 0));
        pathRail.focusBtn.setToggleState (focusMode, juce::dontSendNotification);
        pathRail.setSelected (focusMode ? focusModule : -1);

        const int sc = (int) st.getProperty ("ui_scene", -1);
        sceneChosen = (sc >= 0 && sc <= 2);
        if (sceneChosen) currentScene = sc;
        sceneSolo.setToggleState (currentScene == 0, juce::dontSendNotification);
        sceneTalk.setToggleState (currentScene == 1, juce::dontSendNotification);
        sceneBand.setToggleState (currentScene == 2, juce::dontSendNotification);
    }

    const int c = juce::jlimit (0, gzzio::kNumCharPresets, (int) st.getProperty ("ui_char_preset", 0));
    charBox.setSelectedId (c, juce::dontSendNotification);
    if (c > 0)
        charBox.setTooltip (juce::String::fromUTF8 (gzzio::kCharPresets[c - 1].desc));

    voiceBox.setTooltip (v > 0 ? juce::String::fromUTF8 (gzzio::kVoicePresets[v - 1].desc)
                               : tip::voicebox_tip() + "\n" + tip::note_override());
    micBox  .setTooltip (m > 0 ? juce::String::fromUTF8 (gzzio::kMicPresets[m - 1].desc)
                               : tip::micbox_tip() + "\n" + tip::note_override());
    if (e > 0)
        eqPresetBox.setTooltip (juce::String::fromUTF8 (gzzio::kEqPresets[e - 1].desc));
}

// ====== v2.8.0 かんたんモードで見えないのに効いているエフェクトの検出 ======
// 対象は「こだわりモードのエフェクト欄にしか操作場所が無い」ものだけ。
// リバーブやディエッサーはかんたんモードにもツマミがあるので入れない。
//
// ★ここは「スイッチが入っているか」ではなく「実際に音が変わっているか」で見る。
//   やまびこ・コーラス・ロボ声の on スイッチは **既定値が true** で、代わりに
//   量つまみが 0% だから鳴らない、という作りになっている。スイッチだけを見ると
//   工場出荷状態の人にも警告が出てしまい、ただのオオカミ少年になる。
//   なので「スイッチON かつ 量が 0 より大きい」を条件にする。
//   (スイッチが無いメガホンは量だけ、逆に量の無いボイス変換はスイッチだけ)
namespace
{
    struct HiddenFx { const char* onId; const char* amtId; };
    const HiddenFx kHiddenFx[] = {
        { "vc_on",   nullptr     },   // 既定OFF。ONなら声そのものが変わる
        { "at_on",   "at_amount" },   // 既定OFF
        { "jn_on",   "jn_mix"    },   // 既定OFF
        { "robo_on", "robo_mix"  },   // 既定ON + 量0
        { "dly_on",  "delay"     },   // 既定ON + 量0
        { "cho_on",  "cho_amt"   },   // 既定ON + 量0
        { nullptr,   "mega_amt"  },   // スイッチ無し
        { nullptr,   "prox_amt"  },   // v2.10.0 きょり(こだわり専用・既定0%)
    };
    // ボイス変換は量つまみ(ピッチ/フォルマント)が0でも音がわずかに変わるので、
    // スイッチだけで「効いている」とみなす。ここは別扱い。
}

bool VocalGzzioContent::anyHiddenFxActive() const
{
    auto val = [this] (const char* id) -> float
    {
        if (id == nullptr) return 0.0f;
        if (auto* v = processor.apvts.getRawParameterValue (id)) return v->load();
        return 0.0f;
    };
    for (const auto& fx : kHiddenFx)
    {
        const bool on  = (fx.onId  == nullptr) || (val (fx.onId) > 0.5f);
        const bool amt = (fx.amtId == nullptr) || (std::abs (val (fx.amtId)) > 0.5f);
        if (on && amt) return true;
    }
    return false;
}

void VocalGzzioContent::clearHiddenFx()
{
    // 量つまみを 0 にし、既定OFFのスイッチだけ OFF に戻す。
    // 既定ONのスイッチ(やまびこ/コーラス/ロボ声)は触らない ── 量が0なら鳴らないので、
    // 勝手に既定から変えてしまうほうが不親切。
    auto setTo = [this] (const char* id, float value)
    {
        if (id == nullptr) return;
        if (auto* prm = processor.apvts.getParameter (id))
            prm->setValueNotifyingHost (processor.apvts.getParameterRange (id).convertTo0to1 (value));
    };
    for (const auto& fx : kHiddenFx)
    {
        setTo (fx.amtId, 0.0f);
        if (fx.onId != nullptr)
        {
            const bool defaultsOff = (std::strcmp (fx.onId, "vc_on") == 0
                                   || std::strcmp (fx.onId, "at_on") == 0
                                   || std::strcmp (fx.onId, "jn_on") == 0);
            if (defaultsOff) setTo (fx.onId, 0.0f);
        }
    }
    setTo ("vc_pitch", 0.0f);      // ピッチ/フォルマントも中央へ戻す
    setTo ("vc_form",  0.0f);
    updateHiddenFxWarning();
    repaint();
}

// v2.9.0 セッションモード中は、遅延をふやす3機能のツマミを灰色にする。
// スイッチの値そのものは変えない(セッションを抜ければ元どおり鳴る)。
// 「効いているように見えるのに音が出ない」を避けるための見た目の同期。
void VocalGzzioContent::applySessionLock()
{
    const bool locked = sessionButton.getToggleState();
    juce::Component* const gated[] = {
        &atOnButton, &atKeyBox, &atScaleBox, &atAmountSlider, &atSpeedSlider, &ornSlider,
        &vcOnButton, &jnOnButton, &jnHarmBox, &jnSoloButton,
        &vcPitchK.slider, &vcFormK.slider, &jnMixK.slider
    };
    for (auto* c : gated) c->setEnabled (! locked);
    lastSessionState = locked;
}

void VocalGzzioContent::updateHiddenFxWarning()
{
    const bool want = (! advancedMode) && anyHiddenFxActive();
    if (want == fxHiddenActive && fxWarnButton.isVisible() == want) return;
    fxHiddenActive = want;
    fxWarnButton.setVisible (want);
    resized();
    repaint();
}

void VocalGzzioContent::updateSeqModeVisibility()
{
    if (isOverview()) return;
    if (currentTab == 1)   // effects tab active: smart-EQ knobs stay hidden
        return;
    // v2.8.0: かんたんモードでは右列そのものを出さないので、ここで触ってはいけない。
    // 抜けていたため「こだわりでスマートEQを手動 → かんたんへ切替」で、
    // 20Hzのタイマーが 50ms 後に周波数/深さの6ツマミを画面へ復活させていた
    // (しかも位置はこだわりモードのままなので、パネルの外に浮いて見えた)。
    if (! advancedMode)
        return;
    const bool manual = (seqModeBox.getSelectedId() == 2);
    seqAmountK.slider.setVisible (! manual); seqAmountK.label.setVisible (! manual);
    seqFocusK .slider.setVisible (! manual); seqFocusK .label.setVisible (! manual);
    for (auto* k : { &seqF1K, &seqD1K, &seqF2K, &seqD2K, &seqF3K, &seqD3K })
    {
        k->slider.setVisible (manual);
        k->label .setVisible (manual);
    }
}

//==============================================================================
// v1.4.0 helpers: choice-combo fill, red lamps, right-column tab switch
//==============================================================================
// v1.9.0: Choice パラメータのラベルは日本語で定義されている。ブランドモード
// (英語UI)では、ここの対訳表を使って英語で並べ直す。順序はパラメータと一致。
static juce::StringArray choiceLabelsEnglish (const juce::String& paramID)
{
    if (paramID == "rev_type")  return { "Normal", "Room", "Plate", "Hall", "Church", "Spring", "Shimmer",
                                     // v4.0.0 へや(畳み込み)。並び順は rev_type と一致させること。
                                     "Room: Bath", "Room: Karaoke", "Room: Studio",
                                     "Room: Live House", "Room: Concert Hall", "Room: Plate" };
    // v2.8.0: 実際の選択肢は7つ(ms指定/1/4/1/8/付点1/8/1/8三連/1/16/付点1/4)なのに
    // 4つしか書いていなかったので、英語UIでは **1つずつ後ろにずれた名前** が並び、
    // 4番目以降は日本語のまま残っていた。「1/8 dotted」を選ぶと4分音符が鳴る状態。
    if (paramID == "dly_sync")  return { "ms", "1/4", "1/8", "1/8 dotted",
                                         "1/8 triplet", "1/16", "1/4 dotted" };
    if (paramID == "mega_type") return { "Megaphone", "Radio", "Lo-fi" };
    if (paramID == "seq_mode")  return { "Auto", "Manual" };
    return {};
}

void VocalGzzioContent::fillComboFromChoiceParam (juce::ComboBox& box, const juce::String& paramID)
{
    box.setJustificationType (juce::Justification::centredLeft);
    const int keep = box.getSelectedId();
    box.clear (juce::dontSendNotification);
    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramID)))
    {
        const juce::StringArray en = tip::english ? choiceLabelsEnglish (paramID) : juce::StringArray();
        int id = 1;
        for (auto& s : ch->choices)
        {
            const int i = id - 1;
            box.addItem (i < en.size() ? en[i] : s, id);
            ++id;
        }
    }
    if (keep > 0) box.setSelectedId (keep, juce::dontSendNotification);
    addAndMakeVisible (box);
}

void VocalGzzioContent::addLamp (Lamp& l, const juce::String& paramID)
{
    l.btn.setTooltip (tip::lamp_tip());
    addAndMakeVisible (l.btn);
    l.attach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
                   (processor.apvts, paramID, l.btn);
}

void VocalGzzioContent::placeLamp (Lamp& l, const Knob& k)
{
    // centre of the knob face (slider bounds minus the value box below).
    // v1.6.1: bigger lamp (16 -> 22 px, easier to see and to click) and the
    // trim follows the actual value-box height so it stays centred at 150%.
    auto r = k.slider.getBounds().withTrimmedBottom (k.slider.getTextBoxHeight());
    const int d = advancedMode ? 22 : 36;
    l.btn.setBounds (r.getCentreX() - d / 2, r.getCentreY() - d / 2, d, d);
    l.btn.toFront (false);
}

void VocalGzzioContent::updateTabVisibility()
{
    // In Standard mode the Effects tab is not available, so force the EQ view.
    if (! advancedMode) currentTab = 0;
    const bool fx   = (currentTab == 1);
    // v2.4.0: かんたんモードでは右列(EQ/エフェクト)をまるごと出さない
    const bool easy = ! advancedMode;

    eqGraph.setVisible (! easy && ! fx);   // v2.12.0: 軽量テーマの抑止は廃止
    seqOnButton.setVisible (! easy && ! fx);
    seqModeBox.setVisible (! easy && ! fx);
    if (easy || fx)
    {
        for (auto* k : { &seqAmountK, &seqFocusK, &seqF1K, &seqD1K, &seqF2K, &seqD2K, &seqF3K, &seqD3K })
        {
            k->slider.setVisible (false);
            k->label .setVisible (false);
        }
    }
    else
        updateSeqModeVisibility();

    for (auto* k : { &duckK, &choAmtK, &bpmK, &dlyMsK, &dlyFbK, &dlyHcK, &megaAmtK, &roboFreqK, &roboMixK,
                     &vcPitchK, &vcFormK, &jnMixK, &brK, &emoK })   // v2.0.0: 息・エモ
    {
        k->slider.setVisible (fx);
        k->label .setVisible (fx);
    }
    vcOnButton.setVisible (fx);
    jnOnButton.setVisible (fx);
    jnHarmBox.setVisible (fx);
    revTypeBox.setVisible (! easy); // v1.6.0: reverb type is always reachable (section 4)
    dlySyncBox.setVisible (fx);
    megaTypeBox.setVisible (fx);
    charBox.setVisible (fx);
    tapButton.setVisible (fx);
    midiButton.setVisible (fx);     // v2.1.0 MIDIスイッチ設定
    streamButton.setVisible (fx && processor.isStandalone());   // v2.2.0 配信出力
    lampMega.btn.setVisible (fx);
    lampCho .btn.setVisible (fx);
    lampRobo.btn.setVisible (fx);

    // analysis section (key/scale + chord) lives in the Effects tab, advanced only
    analyzeButton.setVisible (fx);
    keyScaleButton.setVisible (fx);

    // v1.9.0 auto-tune controls (same Effects-tab band)
    atOnButton.setVisible (fx);
    jnSoloButton.setVisible (fx);
    atKeyBox.setVisible (fx);
    atScaleBox.setVisible (fx);
    atAmountSlider.setVisible (fx);
    atSpeedSlider.setVisible (fx);
    ornSlider.setVisible (fx);                    // v2.7.0

    // v3.1 §4「1つずつ」のふるい。選んだ箱のツマミ（＋しあげ）以外は全部しまう。
    //  ★これも「隠すだけ」。出すのは上の行たちの仕事。
    //  かんたん画面には列そのものが出ないので、そちらでは効かせない。
    if (focusMode && advancedMode)
    {
        reverbPower.setVisible (focusModule == gz::ModuleChain::Hirogari);
        //  ★「しあげ」の常設帯はやめた。案A どおり「出」のカードを列に足したので、
        //   同じ2本が画面に二度出ることになる。出口へは1クリックで行けるし、
        //   出ていく音の大きさは右の「ぜんたい」にいつも出ている。
        //   常設帯をやめたぶん、説明の箱が どの箱でも入るようになった。
        auto keep = focusKnobs (focusModule);
        auto kept = [&keep] (Knob* k)
        { return std::find (keep.begin(), keep.end(), k) != keep.end(); };

        //  ★まず「出す」。1つずつ の画面にタブ(EQ/エフェクト)は無いので、
        //   タブの都合で隠れているツマミも、その箱のものなら出さなければならない。
        //   （これが無いと「へんしん」「キャラ声」を選んでも中身が空になる。
        //     実際そうなった: ひろがり からコーラスと やまびこの細部 が消えていた。）
        //   このあとに走る「使いかたのふるい」が、通っていない物だけを消す。
        for (auto* k : keep) { k->slider.setVisible (true); k->label.setVisible (true); }

        for (auto* k : { &gate, &lowCut, &mudK, &harshK, &denoiseK, &comp1K, &comp2K,
                         &attackK, &releaseK, &deessK, &presenceK, &airK, &warmthK,
                         &sustainK, &ringK, &makeupK, &mixK, &widthK, &doublerK, &delayK,
                         &revSizeK, &revMixK, &seqAmountK, &seqFocusK, &seqF1K, &seqD1K,
                         &seqF2K, &seqD2K, &seqF3K, &seqD3K, &duckK, &choAmtK, &bpmK,
                         &dlyMsK, &dlyFbK, &dlyHcK, &megaAmtK, &roboFreqK, &roboMixK,
                         &brK, &emoK, &liftK, &popK, &lipK, &resK, &pickK, &inGainK,
                         &rideK, &humK, &consK, &proxK, &vcPitchK, &vcFormK, &jnMixK })
            if (! kept (k)) { k->slider.setVisible (false); k->label.setVisible (false); }

        //  箱の外の物（グラフ・タブ・EQ の細かい設定など）もしまう。
        //  ★1つずつ の画面は「いま直したい所だけ」。ここに残すと結局にぎやかになる。
        for (auto* c : { (juce::Component*) &eqGraph, (juce::Component*) &seqOnButton,
                         (juce::Component*) &seqModeBox, (juce::Component*) &revTypeBox,
                         (juce::Component*) &dlySyncBox, (juce::Component*) &megaTypeBox,
                         (juce::Component*) &charBox,   (juce::Component*) &tapButton,
                         (juce::Component*) &analyzeButton, (juce::Component*) &keyScaleButton,
                         (juce::Component*) &tabFxButton,  (juce::Component*) &tabEqButton,
                         (juce::Component*) &fxWarnButton })
            c->setVisible (false);
        //  その箱のスイッチ・コンボ類は出し、ほかの箱のものはしまう。
        {
            auto ex = focusExtras (focusModule);
            auto inEx = [&ex] (juce::Component* c)
            { return std::find (ex.begin(), ex.end(), c) != ex.end(); };
            for (auto* c : { (juce::Component*) &vcOnButton, (juce::Component*) &jnOnButton,
                             (juce::Component*) &atOnButton, (juce::Component*) &jnSoloButton,
                             (juce::Component*) &jnHarmBox,  (juce::Component*) &atKeyBox,
                             (juce::Component*) &atScaleBox, (juce::Component*) &atAmountSlider,
                             (juce::Component*) &atSpeedSlider, (juce::Component*) &ornSlider,
                             (juce::Component*) &seqOnButton, (juce::Component*) &seqModeBox,
                             (juce::Component*) &crushBtn,   (juce::Component*) &megaTypeBox,
                             (juce::Component*) &charBox,    (juce::Component*) &revTypeBox,
                             (juce::Component*) &dlySyncBox, (juce::Component*) &tapButton,
                             (juce::Component*) &tempoFitButton,
                             (juce::Component*) &learnButton, (juce::Component*) &relearnBtn,
                             (juce::Component*) &dnClearButton })
                c->setVisible (inEx (c));
        }
        for (auto* l : { &lampGate, &lampDn, &lampDs, &lampRev, &lampDbl, &lampDly,
                         &lampMega, &lampCho, &lampRobo })
            l->btn.setVisible (false);
        //  「入」「出」は箱ではないので、くらべる（その1枚だけ素通し）は出さない。
        focusCompare.setVisible (focusModule < gz::ModuleChain::Count);
    }
    else
        focusCompare.setVisible (false);

    // v3.1 ★最後に「使いかた」のふるいを掛ける。ここが最後でなければならない:
    //  上の行たちは「くわしい/かんたん」と「EQ/エフェクト」だけを見て出し入れするので、
    //  先に掛けると、そのあとの setVisible(true) に上書きされて出てきてしまう。
    applyUseModeMask();

    //  ★ふるいの**あと**で、その箱に中身が1つも残らなかったかを見る。
    //   残らないなら「くらべる」も出さない（素通しにしても音は同じなので、
    //   押せるのに効かないボタンになる）。
    if (focusMode && focusModule < gz::ModuleChain::Count)
    {
        int vis = 0;
        for (auto* k : focusKnobs (focusModule)) if (k->slider.isVisible()) ++vis;
        for (auto* c : focusExtras (focusModule)) if (c->isVisible()) ++vis;
        if (vis == 0) focusCompare.setVisible (false);
    }

    // v3.1 席詰め(placeRow)が可視状態を見るようになったので、
    //  出し入れしたら必ず並べ直す。ここを忘れると穴が開いたままになる。
    if (isOverview()) updateOverviewVisibility();
    resized();
    repaint();
}

// v1.4.0 Standard vs Advanced. Standard keeps the high-frequency "make my voice
// better" controls (clean-up / dynamics / tone / EQ / reverb / meter / AUTO SETUP).
// Advanced additionally reveals the Effects tab (chorus, delay, character, key/
// scale + chord analysis) and the note-rail / range / tempo tools.
void VocalGzzioContent::applyModeVisibility()
{
    // 10秒おまかせのカウントダウン中にモードを切り替えたら中断(音の分析中は続行)
    easyComboPhase = 0;
    songSetupButton.setButtonText (advancedMode ? tip::autoset_sing_label()
                                                : tip::easy_sing_label());

    // v2.8.0: モードを切り替えた瞬間に案内帯の要否を決める
    // (タイマー待ちだと 50ms のあいだレイアウトがずれて見える)
    fxHiddenActive = (! advancedMode) && anyHiddenFxActive();
    fxWarnButton.setVisible (fxHiddenActive);

    // the Effects tab button only exists in Advanced
    tabFxButton.setVisible (advancedMode);
    tabEqButton.setVisible (advancedMode);      // tabs only meaningful when 2 exist

    // advanced-only side tools in the tuner + output panel
    tuner.setRangeToolsVisible (advancedMode);
    tempoFitButton.setVisible (advancedMode);

    applyKnobVisibility();
    resized();
    repaint();
}

// v3.1 ★見せる/隠すだけを切り出したもの。副作用（おまかせの中断など）は無い。
//  使いかた(音の種類)を変えたときは、こちらだけを回す。
//  applyModeVisibility() を丸ごと呼ぶと easyComboPhase が 0 に戻り、
//  10秒おまかせの途中経過が黙って消える（「意思を持って選んだのに戻る」の類）。
void VocalGzzioContent::applyKnobVisibility()
{
    restoreOverviewVisibility();
    // v2.4.0 かんたんモードは「よく使う7ツマミ」だけ。残りは丸ごと隠す。
    auto showKnob = [] (Knob& k, bool v) { k.slider.setVisible (v); k.label.setVisible (v); };
    for (auto* k : { &gate, &lowCut, &popK, &lipK, &resK, &rideK, &humK, &consK,
                     &pickK,                       // v3.1 ピックおさえ
                     &proxK,                       // v2.10.0 きょり
                     &comp1K, &attackK, &releaseK,
                     &mudK, &harshK, &deessK, &mixK,
                     &airK, &warmthK, &sustainK, &ringK,
                     &widthK, &doublerK, &delayK, &revSizeK, &liftK })
        showKnob (*k, advancedMode);
    for (auto* k : { &inGainK, &denoiseK, &comp2K, &presenceK, &revMixK, &makeupK })
        showKnob (*k, true);
    inGainK.label.setText (tip::T ("入力音量", "Input"), juce::dontSendNotification);
    comp2K.label.setText (tip::T (advancedMode ? "ならし圧縮" : "音量をそろえる", "Leveling"), juce::dontSendNotification);
    presenceK.label.setText (tip::T (advancedMode ? "ヌケ感" : "声の明るさ", "Presence"), juce::dontSendNotification);
    makeupK.label.setText (tip::T ("出力音量", "Output"), juce::dontSendNotification);
    // 隠したツマミのランプも一緒に消す(残ると宙に浮いて見える)
    lampGate.btn.setVisible (advancedMode);
    lampDbl .btn.setVisible (advancedMode);
    lampDly .btn.setVisible (advancedMode);
    lampDs  .btn.setVisible (advancedMode);
    revTypeBox.setVisible (advancedMode);
    reverbPower.setVisible (true);
    overviewReturn.setVisible (! isOverview());
    if (isOverview()) refreshOverviewText();

    if (! advancedMode) currentTab = 0;
    updateTabVisibility();          // ← この中の最後で applyUseModeMask() が走る
}

// v3.1 §4「1つずつ」— そのモジュールに属するツマミ。
//
//  ★どのツマミがどの箱に入るかは、**音の通り道と同じ並び**にする。
//   画面の見た目のためのグループではなく、processBlock で実際に通る順。
//   ここがずれると、カードのメーターが動いているのに中身が別、という
//   いちばん質の悪い嘘になる。
//
//  設計書§4 は「一度に最大7個まで」。7を超える箱は2行に分けて置く。
//  **出さない**のではなく**分ける**（隠すと届かなくなる）。
std::vector<VocalGzzioContent::Knob*> VocalGzzioContent::focusKnobs (int module)
{
    switch (module)
    {
        case gz::ModuleChain::Souji:
            //  ★マイク音量・ジー音・きょり は**ここではない**。
            //   processBlock で モジュールの門(mods.save)より前にあるので、
            //   おそうじ の中に描くと画面が嘘をつく。入(8)へ移した。
            return { &gate, &denoiseK, &popK, &lipK };
        case gz::ModuleChain::Henshin:
            return { &vcPitchK, &vcFormK, &jnMixK };
        case gz::ModuleChain::Totonoe:
            return { &lowCut, &mudK, &harshK, &resK };
        case gz::ModuleChain::Soroe:
            return { &comp1K, &comp2K, &attackK, &releaseK, &rideK, &seqAmountK, &seqFocusK };
        case gz::ModuleChain::Sagyo:
            return { &deessK };
        case gz::ModuleChain::Neiro:
            //  なめらか(resK) は ととのえ の物なので、ここには入れない
            //  （ModuleChain の並びがそう決めている。画面だけ別にすると嘘になる）。
            return { &pickK, &consK, &presenceK, &airK, &warmthK, &sustainK, &ringK, &brK };
        case gz::ModuleChain::Chara:
            return { &megaAmtK, &roboFreqK, &roboMixK, &emoK, &liftK, &duckK };
        case gz::ModuleChain::Hirogari:
            return { &widthK, &doublerK, &choAmtK, &delayK, &dlyMsK, &dlyFbK, &dlyHcK,
                     &revSizeK, &revMixK };
        //  v3.1 §4 案A の固定カード（箱ではない・いつも通る所）
        case 8: return { &inGainK, &humK, &proxK };     // 入 マイクから
        case 9: return { &makeupK, &mixK };             // 出 しあげ
        default: return {};
    }
}

// v3.1 §4 ツマミ以外の物。スイッチ・コンボ・横スライダーも、その箱のものは置く。
//  ★ここを空にすると「カードは選べるのに、中の機能を入り切りできない」箱ができる。
//   実際「へんしん」を選んでも、ボイス変換ON/ハモリ/キー/補正量が画面に出なかった。
std::vector<juce::Component*> VocalGzzioContent::focusExtras (int module)
{
    switch (module)
    {
        case gz::ModuleChain::Souji:
            return { &learnButton, &relearnBtn, &dnClearButton };
        case gz::ModuleChain::Henshin:
            return { &vcOnButton, &jnOnButton, &atOnButton, &jnSoloButton,
                     &jnHarmBox, &atKeyBox, &atScaleBox,
                     &atAmountSlider, &atSpeedSlider, &ornSlider };
        case gz::ModuleChain::Soroe:
            return { &seqOnButton, &seqModeBox, &crushBtn };
        case gz::ModuleChain::Chara:
            return { &megaTypeBox, &charBox };
        case gz::ModuleChain::Hirogari:
            return { &reverbPower, &revTypeBox, &dlySyncBox, &tapButton, &tempoFitButton };
        default: return {};
    }
}

// v3.1 §4 選んだ1枚を切り替える。列の枠と、中央の中身を合わせる。
void VocalGzzioContent::applyFocusSelection (int module)
{
    focusModule = juce::jlimit (0, 9, module);      // 0〜7=箱 / 8=入 / 9=出
    pathRail.setSelected (focusModule);
    processor.apvts.state.setProperty ("ui_focus_mod", focusModule, nullptr);
    focusTipText = {};                     // 別の箱に移ったら説明も空に戻す
    applyKnobVisibility();
    resized();
    repaint();
}

// v3.1「使いかた4種」（設計書§3）— 使いかたで**そもそも通っていない**ツマミを出さない。
//
//  ★出し入れの根拠を「設計書の表」ではなく「本当に音が通るかどうか」にした。
//   設計書は しゃべり でも へんしん・ハモリ を出さないと書いてあるが、
//   しゃべりは DSP 側では ピッチ系を**通している**。通っているのに画面から
//   消すと、へんしんONのまま切り替えた人が「声が変わっているのに止める場所が無い」
//   状態になる。これは隠す以前に不具合なので、そこは出したままにした。
//   （しゃべりでもピッチ系を止めるべきなら、DSP側から止める。要相談）
//
//  だから、ここで消えるものは全部「押しても何も起きないツマミ」だけ。
//  「ぜんぶ表示」を入れれば、それも出る。音は1サンプルも変わらない。
void VocalGzzioContent::applyUseModeMask()
{
    if (showAllKnobs) return;

    const int mode = (int) processor.apvts.getRawParameterValue ("src_mode")->load();
    const auto prof = sourceProfile (mode);

    auto hideK = [] (Knob& k) { k.slider.setVisible (false); k.label.setVisible (false); };
    auto hideC = [] (juce::Component& c) { c.setVisible (false); };

    if (! prof.voiceOnly)          // ことば・艶（アコギだけ）
    {
        hideK (consK);
        hideK (ringK);
    }
    if (! prof.breathOk)           // 息（アコギだけ・しゃべり）
        hideK (brK);
    if (! prof.pickOk)             // ピックおさえ（アコギだけ以外は音を通していない）
        hideK (pickK);
    if (! prof.pitchOk)            // ピッチ補正・ボイス変換・ハモリ（アコギだけ・声とギター）
    {
        for (auto* k : { &vcPitchK, &vcFormK, &jnMixK }) hideK (*k);
        for (auto* c : { (juce::Component*) &vcOnButton, (juce::Component*) &jnOnButton,
                         (juce::Component*) &atOnButton, (juce::Component*) &jnSoloButton,
                         (juce::Component*) &jnHarmBox,  (juce::Component*) &atKeyBox,
                         (juce::Component*) &atScaleBox, (juce::Component*) &atAmountSlider,
                         (juce::Component*) &atSpeedSlider, (juce::Component*) &ornSlider })
            hideC (*c);
    }
    if (! prof.spaceOk)            // ひびき・やまびこ（しゃべり）
    {
        for (auto* k : { &revMixK, &revSizeK, &delayK, &dlyMsK, &dlyFbK, &dlyHcK }) hideK (*k);
        for (auto* c : { (juce::Component*) &revTypeBox, (juce::Component*) &dlySyncBox,
                         (juce::Component*) &tapButton,  (juce::Component*) &bpmK.slider,
                         (juce::Component*) &bpmK.label })
            hideC (*c);
        lampDly.btn.setVisible (false);
    }
}

void VocalGzzioContent::styleButton (juce::TextButton& b)
{
    addAndMakeVisible (b);
}

// v1.4.0 analysis helpers: key label + a diatonic chord-progression suggestion.
// The voice is monophonic so we cannot detect real accompaniment chords; instead
// we offer well-worn progressions in the detected key (clearly labelled as such).
juce::String VocalGzzioContent::keyName (int tonic, bool isMinor)
{
    static const char* n[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return juce::String (n[((tonic % 12) + 12) % 12])
         + (isMinor ? tip::T ("\xe3\x83\x9e\xe3\x82\xa4\xe3\x83\x8a\xe3\x83\xbc", " minor")    // マイナー
                    : tip::T ("\xe3\x83\xa1\xe3\x82\xb8\xe3\x83\xa3\xe3\x83\xbc", " major"));  // メジャー
}

juce::String VocalGzzioContent::suggestChords (int tonic, bool isMinor)
{
    static const char* n[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    auto deg = [&] (int semi, bool minorChord)
    {
        return juce::String (n[((tonic + semi) % 12 + 12) % 12]) + (minorChord ? "m" : "");
    };
    if (! isMinor)
        // I-V-vi-IV (the "royal road" is more JP-pop: IV-V-iii-vi). Offer the JP one.
        return deg (5,false) + "-" + deg (7,false) + "-" + deg (4,true) + "-" + deg (9,true)
             + tip::T ("\x20\x28\xe7\x8e\x8b\xe9\x81\x93\xe9\x80\xb2\xe8\xa1\x8c\x29", " (classic progression)");   // (王道進行)
    else
        // vi-IV-I-V equivalent in minor: i-VI-III-VII (common JP minor loop)
        return deg (0,true) + "-" + deg (8,false) + "-" + deg (3,false) + "-" + deg (10,false);
}

// v1.4.0 TEMPO FIT: set delay time (dotted 1/8, the modern vocal default) and a
// short reverb size from the current BPM so echoes/tails don't smear fast songs.
void VocalGzzioContent::applyTempoFit()
{
    float bpm = processor.getHostBpm();
    if (bpm < 20.0f) bpm = processor.apvts.getRawParameterValue ("bpm")->load();
    bpm = juce::jlimit (50.0f, 300.0f, bpm);

    auto setP = [this] (const juce::String& id, float v)
    {
        if (auto* prm = processor.apvts.getParameter (id))
            prm->setValueNotifyingHost (processor.apvts.getParameterRange (id).convertTo0to1 (v));
    };

    const float quarterMs = 60000.0f / bpm;
    setP ("dly_sync", 3.0f);                                   // dotted 1/8 note value
    setP ("dly_ms",   juce::jlimit (60.0f, 900.0f, quarterMs * 0.75f));
    // faster tempo -> smaller room so the tail clears before the next phrase
    setP ("revsize",  juce::jlimit (18.0f, 60.0f, 90.0f - (bpm - 60.0f) * 0.22f));

    tempoFitMsg    = tip::tempofit_done();
    tempoFitMsgTtl = 20 * 4;  // ~4 s at 20 Hz
    infoText       = tempoFitMsg;
    repaint();
}

juce::Font VocalGzzioContent::cfont (float h, bool bold) const
{
    return GzzioLnF::uiFont (h * fontScale, bold);
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
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    // Value box is editable (double-click, or single-click via the mouse handler
    // installed below) so any parameter can be typed in directly.
    k.slider.setSliderSnapsToMousePosition (false);
    k.slider.setColour (juce::Slider::textBoxTextColourId, Palette::ink);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, Palette::panelLn);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, Palette::track.withAlpha (0.35f));
    if (suffix.isNotEmpty())
        k.slider.setTextValueSuffix (" " + suffix);
    k.slider.setTooltip (tooltip);
    k.slider.getProperties().set ("juice", GzzioLnF::juiceFruitFor (paramID));  // v1.7.0 juice knob
    const int signalGroup = (paramID == "in_gain" || paramID == "denoise" || paramID == "gate" || paramID == "lowcut") ? 0
                          : (paramID == "mud" || paramID == "harsh" || paramID == "comp1" || paramID == "comp2" || paramID == "deess") ? 1 : 2;
    k.slider.getProperties().set ("signalGroup", signalGroup);

    // v2.8.0 ★ダブルクリックで初期値に戻す。
    // DAWのプラグインではほぼ共通の操作なのに、これまで効かなかった。
    // 「触ってみたけど元に戻せない」で手が止まるのがいちばん多いつまずき方
    // なので、全ツマミに入れる(数値を打ちたいときは下の数値ボックス)。
    if (auto* prm = processor.apvts.getParameter (paramID))
        k.slider.setDoubleClickReturnValue (true,
            (double) processor.apvts.getParameterRange (paramID)
                         .convertFrom0to1 (prm->getDefaultValue()));
    // v2.8.0: キーボードでも動かせるようにする(クリックしてから矢印キー)。
    // マウスが使いにくい人がまったく操作できない状態だった。
    k.slider.setWantsKeyboardFocus (true);

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
    if (isOverview())
    {
        updateOverviewState();
        juce::String hint;
        if (auto* under = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse())
            if (isParentOf (under))
                for (auto* c = under; c != nullptr && c != this && hint.isEmpty(); c = c->getParentComponent())
                    if (auto* client = dynamic_cast<juce::TooltipClient*> (c)) hint = client->getTooltip();
        if (hint.isNotEmpty() && hint != overviewHint) { overviewHint = hint; repaint (overviewHelp); }
        repaint (overviewOutput);
    }
    // v3.0「つぶさない」の数字は毎回変わるので、20Hz で描き直す。
    // ★これが無いと、OFF にした瞬間の数字が**消えずに残る**（実際に残った）。
    //   ボタン自体は自分で描き直すが、その隣の直描きの数字は誰も消してくれない。
    if (! crushReadArea.isEmpty()) repaint (crushReadArea);

    // v3.0-c 「音のとおり道」のミニメーター。値が動いたカードだけ描き直す。
    if (railView.isVisible()) pathRail.tick();

    // v3.1 §4「1つずつ」: カーソルが乗っているツマミの使いかたを、説明の箱に出す。
    //  ★ツールチップの吹き出しを待たずに読めるようにするための物。
    //   20Hz で「いま下にある部品」を見るだけなので、描き直しは変わった時だけ。
    if (focusMode)
    {
        juce::String want;
        if (auto* c = juce::Desktop::getInstance().getMainMouseSource()
                          .getComponentUnderMouse())
            for (auto* p = c; p != nullptr && want.isEmpty(); p = p->getParentComponent())
            {
                if (p == this) break;
                //  ★ツマミ（回す物）の説明だけを出す。ボタンやコンボまで拾うと、
                //   「1つずつ」ボタンに触れただけで説明欄が別の話に変わる。
                if (auto* sl = dynamic_cast<juce::Slider*> (p))
                    want = sl->getTooltip();
            }
        if (want != focusTipText) { focusTipText = want; repaint (focusTipArea); }
        //  「ぜんたい」の入出力メーターは動く物なので、その帯だけ描き直す。
        if (! focusMeterArea.isEmpty()) repaint (focusMeterArea);
    }

    // v2.1.0: A/BがMIDIスイッチ(プロセッサ側)で切り替わったらボタン表示と
    // プリセット表示を追随させる(自分で押したときもこの経路で更新される)
    if (lastAbDirty != processor.abUiDirty.load())
    {
        lastAbDirty = processor.abUiDirty.load();
        updateABButtons();
        refreshPresetDisplays();
    }

    // v2.6.0: ジー音が見つかったら、ツマミの名前をそのまま表示にする。
    // 「50Hz を消し中」と出れば、効いていることが目で分かる(数字は嘘をつかない)。
    {
        const int hz = processor.getHumHz();
        if (hz != humShownHz)
        {
            humShownHz = hz;
            humK.label.setText (hz == 50 ? tip::hum_found50()
                              : hz == 60 ? tip::hum_found60()
                                         : tip::hum_label(),
                                juce::dontSendNotification);
        }
    }

    // v1.7.0 yuru-kawa: animate the juice knobs (rising bubbles + surface wobble)
    // v2.11.0: よる(8)でも同じように泡が上がる（配色が変わるだけの同じテーマなので）
    if (pastelTheme())
    {
        GzzioLnF::bumpJuicePhase (0.22f);
        for (auto* ch : getChildren())
            if (dynamic_cast<juce::Slider*> (ch)) ch->repaint();
    }

    // v1.8.0: seasons (自然) & idol stage (ジャニーズ) animate at ~10 fps.
    const bool jnOnNow = processor.apvts.getRawParameterValue ("jn_on")->load() > 0.5f;
    themePainter.jnActive = jnOnNow;
    {   // 演出更新: 入力レベルとカーソルを渡す (落ち葉等はカーソルで押し流せる)
        const auto mp = getMouseXYRelative();
        themePainter.update (0.05f, mp.x, mp.y,
                             juce::jlimit (0.0f, 1.0f, processor.getInputLevel() * 2.2f));
    }
    if (themeMode == 2 || themeMode == 4 || jnOnNow)
        if (((++themeAnimFrame) & 1) == 0) repaint();      // ~10fps

    const bool learning = processor.isDenoiseLearning();
    learnButton.setButtonText (learning ? tip::T ("学習中…", "Learning…") : tip::T ("ノイズを測る", "Learn noise"));
    learnButton.setColour (juce::TextButton::buttonColourId,
                           learning ? Palette::yellow : Palette::green.withAlpha (0.16f));
    learnButton.setColour (juce::TextButton::textColourOffId,
                           learning ? Palette::bgTop : Palette::ink);

    // ---- v2.10.0 学習の結果を出す／覚えている間は「もどす」を出す ----
    {
        const int res = processor.getDenoiseLearnResult();
        if (res != 0)
        {
            processor.clearDenoiseLearnResult();
            juce::String msg = res == 1 ? tip::dnlearn_ok()
                             : res == 2 ? tip::dnlearn_loud()
                                        : tip::dnlearn_noisy();
            if (res != 1)
            {
                // どのくらい大きかったのかも添える(押し直す判断がつくように)
                msg += " (" + juce::String (processor.getDenoiseLearnLevelDb(), 0) + " dB)";
            }
            // 10秒おまかせの最中なら、いま出しても8秒後に上書きされる。取っておく。
            if (easyComboPhase != 0) dnLearnMsgPending = msg;
            else                   { infoText = msg; dnMsgTicks = 100; }   // 5秒
            repaint();
        }
        if (dnMsgTicks > 0 && --dnMsgTicks == 0) repaint();

        const bool learned = processor.isDenoiseLearned();
        if (learned != dnClearButton.isVisible())
        {
            dnClearButton.setVisible (learned);
            repaint();
        }
    }

    // v2.4.0 かんたんモードの10秒おまかせ: フェーズ1(しずかに)をここで進める
    if (easyComboPhase == 1)
    {
        ++easyComboTick;                                   // 20Hz
        if (easyComboTick == 10)                           // 0.5s 経過: ノイズ学習開始
            processor.requestDenoiseLearn();
        const int remainSec = juce::jmax (1, (40 - easyComboTick + 19) / 20);
        songSetupButton.setButtonText (tip::combo_quiet() + juce::String (remainSec)
                                       + tip::T ("\xe7\xa7\x92", " s"));
        songSetupButton.setColour (juce::TextButton::buttonColourId, Palette::ice);
        songSetupButton.setColour (juce::TextButton::textColourOffId, Palette::ink);
        if (easyComboTick >= 40)                           // 2.0s 経過: うた自動へ
        {
            easyComboPhase = 2;
            processor.requestAutoSetup (1);
        }
    }

    // AUTO SETUP: reflect progress, apply on the message thread when capture ends
    if (processor.isAutoSetupRunning())
    {
        const int pct = (int) (processor.getAutoSetupProgress() * 100.0f);
        auto& runBtn = processor.getAutoSetupMode() == 1 ? songSetupButton : autoSetupButton;
        runBtn.setButtonText (tip::autoset_run() + juce::String (100 - pct) + "%");
        runBtn.setColour (juce::TextButton::buttonColourId, Palette::yellow);
        runBtn.setColour (juce::TextButton::textColourOffId, Palette::readableOn (Palette::yellow));
    }
    else
    {
        if (easyComboPhase == 2) easyComboPhase = 0;   // 10秒おまかせ完了
        const int res = processor.getAutoSetupResult();
        if (res == 100)                       // capture finished -> compute & apply now
        {
            processor.applyAutoSetup();
            const int applied  = processor.getAutoSetupResult();
            const bool suggest = applied >= 100;           // sing: noisy room -> LEARN hint
            const int  base    = applied % 100;
            if (base >= 10)                                // sing result (10..12)
                autoSetupMsg = base == 10 ? tip::autoset_sing_bright()
                             : base == 11 ? tip::autoset_sing_warm()
                                          : tip::autoset_sing_neutral();
            else                                           // talk result (0..2)
                autoSetupMsg = base == 0 ? tip::autoset_done_bright()
                             : base == 1 ? tip::autoset_done_warm()
                                         : tip::autoset_done_neutral();
            // v2.12.0 判断の根拠(実測値)を添える。「曖昧で雑」への答えは、
            // 何をどう測ってそう決めたかを見せること(点検と同じ思想)。
            autoSetupMsg += tip::autoset_meas (processor.getAutoBrightDb(),
                                               juce::roundToInt (processor.getAutoSibPct()));
            if (suggest)
                autoSetupMsg += " " + tip::autoset_learn_suggest();
            // v2.10.0 10秒おまかせは 0.5-2.0秒でノイズ床を自動学習している。
            // その結果(採用した/しなかった)をここで必ず伝える。黙って汚れた床が
            // 固定されるのが「急に痩せた・シャリついた」の正体だった。
            if (dnLearnMsgPending.isNotEmpty())
            {
                autoSetupMsg += " " + dnLearnMsgPending;
                dnLearnMsgPending.clear();
            }
            infoText = autoSetupMsg;          // surface it in the graph info strip too
            // the auto result replaced whatever the presets had set, so show the
            // combos as unselected again (values live in the knobs now)
            auto& st2 = processor.apvts.state;
            st2.setProperty ("ui_voice_preset", 0, nullptr);
            st2.setProperty ("ui_mic_preset",   0, nullptr);
            st2.setProperty ("ui_eq_preset",    0, nullptr);

            // ★v3.0-c: 自分で選んだシーンがあるなら、その7つだけ**戻す**。
            //  うた自動は「声を測って作る」もので、
            //  弾き語りなのか・トーク配信なのか・バンドの中なのかは**測りようがない**。
            //  状況を知っているのは選んだ本人だけなので、そこはシーンが勝つ。
            //  （プルダウン3つは自動に上書きされたので選択を外す。これは今までどおり）
            if (sceneChosen)
            {
                applyScene();
                autoSetupMsg += " " + tip::scene_kept (currentScene);
            }
            refreshPresetDisplays();
        }
        autoSetupButton.setButtonText (tip::autoset_label());
        autoSetupButton.setColour (juce::TextButton::buttonColourId, Palette::ice.withAlpha (0.18f));
        autoSetupButton.setColour (juce::TextButton::textColourOffId, Palette::ink);
        if (easyComboPhase == 0)   // 10秒おまかせのカウントダウン表示を上書きしない
        {
            songSetupButton.setButtonText (advancedMode ? tip::autoset_sing_label()
                                                        : tip::easy_sing_label());
            songSetupButton.setColour (juce::TextButton::buttonColourId, Palette::yellow);
            songSetupButton.setColour (juce::TextButton::textColourOffId, Palette::readableOn (Palette::yellow));
        }
    }

    if (tempoFitMsgTtl > 0 && --tempoFitMsgTtl == 0)
        repaint();

    // KEY/SCALE scan progress + completion
    if (processor.isKeyScanRunning())
    {
        const int pct = (int) (processor.getKeyScanProgress() * 100.0f);
        keyScaleButton.setButtonText (tip::keyscale_run() + juce::String (100 - pct) + "%");
        keyScaleButton.setColour (juce::TextButton::buttonColourId, Palette::yellow);
        keyScaleButton.setColour (juce::TextButton::textColourOffId, Palette::readableOn (Palette::yellow));
    }
    else
    {
        if (keyScaleButton.getButtonText() != tip::keyscale_label())
        {
            // just finished (or idle): if a fresh capture exists, show the result
            int tonic; bool minor; float conf;
            if (processor.getKeyResult (tonic, minor, conf) && keyScaleMsg.isEmpty())
            {
                keyScaleMsg = tip::keyscale_prefix() + keyName (tonic, minor)
                            + (conf < 0.55f ? tip::keyscale_lowconf() : juce::String());
                infoText = keyScaleMsg;

                // v1.9.0: the user pressed "detect key" — auto-fill auto-tune's
                // key + scale (major/minor) from the result so it's ready to use.
                if (auto* pk = processor.apvts.getParameter ("at_key"))
                    pk->setValueNotifyingHost (pk->convertTo0to1 ((float) juce::jlimit (0, 11, tonic)));
                if (auto* ps = processor.apvts.getParameter ("at_scale"))
                    ps->setValueNotifyingHost (ps->convertTo0to1 ((float) (minor ? 2 : 1)));  // 2=Minor,1=Major
            }
            keyScaleButton.setButtonText (tip::keyscale_label());
            keyScaleButton.setColour (juce::TextButton::buttonColourId, Palette::ice.withAlpha (0.18f));
            keyScaleButton.setColour (juce::TextButton::textColourOffId, Palette::ink);
        }
    }
    if (analyzeMsgTtl > 0) --analyzeMsgTtl;

    if (! updateNotice.isVisible())          // refresh the stream-loudness meter
        repaint (lvMeterArea.expanded (2));

    // keep Smart EQ on/off label + mode visibility in sync (EQ tab only)
    const auto seqWant = seqOnButton.getToggleState()
        ? juce::String::fromUTF8 ("\x45\x51\x20\x4f\x4e") : juce::String::fromUTF8 ("\x45\x51\x20\x4f\x46\x46");
    if (seqOnButton.getButtonText() != seqWant)
        seqOnButton.setButtonText (seqWant);

    if (currentTab == 0)
    {
        const bool manualNow = (seqModeBox.getSelectedId() == 2);
        if (seqF1K.slider.isVisible() != manualNow)   // mode changed elsewhere -> refresh
            updateSeqModeVisibility();
    }

    // v2.8.0: かんたんモードで見えないエフェクトが効いていないか監視する。
    // プリセット読み込み・A/B切替・MIDIスイッチ経由の変化もここで拾える。
    updateHiddenFxWarning();

    // v2.10.0 #75 使われ方: 0.5秒ごとに「いま効いている機能」を数えて秒を足す。
    // ★送信はしない。手元のファイルに貯めるだけ。
    // ★音声スレッドでは一切触らない（ここは画面の20Hzタイマー）。
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        if (nowMs - lastUsageMs > 500.0)
        {
            const double dt = (lastUsageMs > 0.0) ? (nowMs - lastUsageMs) / 1000.0 : 0.5;
            lastUsageMs = nowMs;
            auto amt = [this] (const char* id) -> float
            {
                if (auto* v = processor.apvts.getRawParameterValue (id)) return v->load();
                return 0.0f;
            };
            auto on = [&amt] (const char* id) { return amt (id) > 0.5f; };
            struct U { const char* key; bool active; };
            const U list[] = {
                { "session",  on ("session") },
                { "prox",     amt ("prox_amt")  > 0.5f },
                { "hum",      amt ("hum_amt")   > 0.5f },
                { "cons",     amt ("cons_amt")  > 0.5f },
                { "orn",      amt ("orn_amt")   > 0.5f },
                { "autotune", on ("at_on") },
                { "voicefx",  on ("vc_on") },
                { "harmony",  on ("jn_on") },
                { "smarteq",  on ("seq_on") },
                { "reverb",   on ("revon") },
            };
            for (const auto& u : list)
                if (u.active) processor.usage().add (u.key, dt);
        }
    }

    // v2.9.0: セッションモードも同じ経路(プリセット/A/B/オートメーション)で
    // 変わりうるので、ここで見た目を追随させる。
    if (sessionButton.getToggleState() != lastSessionState)
    {
        applySessionLock();
        repaint();
    }
    // 追加遅延バッジは、値が変わったときだけ塗り直す(毎フレーム repaint しない)。
    {
        const int latNow = processor.addedLatencySamples();
        if (latNow != lastShownLatency)
        {
            lastShownLatency = latNow;
            repaint (latBadgeArea.expanded (2));
        }
    }
}

// Voice presets own: comps, attack/release, presence, warmth, sustain
void VocalGzzioContent::applyVoicePreset (int id)
{
    auto setP = [this] (const juce::String& pid, float v)
    {
        if (auto* prm = processor.apvts.getParameter (pid))
            prm->setValueNotifyingHost (processor.apvts.getParameterRange (pid).convertTo0to1 (v));
    };

    const auto& v = gzzio::kVoicePresets[juce::jlimit (0, gzzio::kNumVoicePresets - 1, id - 1)];
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

    const auto& m = gzzio::kMicPresets[juce::jlimit (0, gzzio::kNumMicPresets - 1, id - 1)];
    setP ("lowcut", m.lc);  setP ("mud", m.mud);
    setP ("harsh", m.harsh); setP ("air", m.air);
    setP ("deess", m.ds);
}

// EQ presets own the whole tone recipe: lowcut, mud, harsh, presence, air,
// warmth. Selecting one after voice/mic overwrites those tone choices.
void VocalGzzioContent::applyEqPreset (int id)
{
    auto setP = [this] (const juce::String& pid, float v)
    {
        if (auto* prm = processor.apvts.getParameter (pid))
            prm->setValueNotifyingHost (processor.apvts.getParameterRange (pid).convertTo0to1 (v));
    };

    const auto& e = gzzio::kEqPresets[juce::jlimit (0, gzzio::kNumEqPresets - 1, id - 1)];
    setP ("lowcut", e.lc);     setP ("mud", e.mud);
    setP ("harsh", e.harsh);   setP ("presence", e.pres);
    setP ("air", e.air);       setP ("drive", e.drv);
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

void VocalGzzioContent::updateABButtons()
{
    const int cur = processor.getAbCurrent();   // v2.1.0: 状態はプロセッサが持つ
    abA.setToggleState (cur == 0, juce::dontSendNotification);
    abB.setToggleState (cur == 1, juce::dontSendNotification);
    abCopy.setButtonText (cur == 0
        ? juce::String ("A") + juce::String::fromUTF8 ("\xe2\x96\xb6") + "B"    // A▶B
        : juce::String ("B") + juce::String::fromUTF8 ("\xe2\x96\xb6") + "A");  // B▶A
}

void VocalGzzioContent::savePreset()
{
    chooser = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("声の設定を保存"),
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
    chooser = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("声の設定を読み込む"),
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
                    {
                        processor.apvts.replaceState (juce::ValueTree::fromXml (*xml));
                        refreshPresetDisplays();
                    }
        });
}

//==============================================================================
void VocalGzzioContent::paint (juce::Graphics& g)
{
    if (isOverview()) { paintOverview (g); return; }
    // v1.7.0: select the embedded rounded font for THIS instance's paint (uiFont is
    // static & shared, so set it per-paint to stay correct with multiple instances).
    GzzioLnF::setUseKawaiiFont (pastelTheme());

    // v1.7.0: themed backdrop first. Yuru-kawa paints its own static background
    // (mint gradient + bokeh + mascot); when it does, we skip the neutral one so
    // the header bar and cards still draw ON TOP. No theme -> neutral dark bg.
    if (! themePainter.paint (g))
    {
        g.setGradientFill (juce::ColourGradient (Palette::bgTop, 0, 0,
                                                 Palette::bgBot, 0, (float) getHeight(), false));
        g.fillRect (getLocalBounds());

        // 標準画面には静かな面の奥行きだけを加える。
        if (themeMode == 0)
        {
            g.setGradientFill (juce::ColourGradient (Palette::green.withAlpha (0.06f),
                0.0f, (float) getHeight(), juce::Colours::transparentBlack,
                (float) getWidth() * 0.75f, 0.0f, false));
            g.fillRect (getLocalBounds());
        }
    }

    // ---- header bar (72 px: taller row 1 for larger, legible combos) ----
    auto hdr = getLocalBounds().withHeight (juce::jmax (72, heroArea.getY() - 8));
    g.setColour (Palette::panel);
    g.fillRect (hdr);
    g.setColour (juce::Colours::white.withAlpha (0.04f));       // top elevation highlight
    g.fillRect (hdr.withHeight (1));
    g.setColour (Palette::panelLn);
    g.fillRect (hdr.withY (hdr.getBottom() - 1).withHeight (1));

    // v2.12.0 ★ここにあった drawCrossSwitch(g) を消した。
    //  ヘッダ宣言には「未使用」と書いてあったが**実際は毎フレーム呼ばれていた**。
    //  v2.4.0 でチップ版(drawThemeCross)に置き換わってから、描いた絵はチップに
    //  上書きされて見えなくなっていただけ——つまり4版ぶん、見えない絵を毎回
    //  描き続けていた。コメントを信じずに呼び出し元を数えるべきだった。
    //  しかも今回チップの席を空にしたので、空矩形を expanded(3) した 6x6 の
    //  染みが左上に出るところだった。実装・呼び出しとも撤去。

    // mascot: medium 80px illustration next to the logo (bright art on a light card)
    // v2.10.0 ★どのテーマでも「ぷにぐじくん」にそろえた。
    //   以前は ゆるかわ(themeMode==1) のときだけ ぷにぐじくん で、それ以外は
    //   ラーメンの絵(character.png)が出ていた。アプリのアイコン・インストーラ・
    //   サイトはどれも ぷにぐじくん なので、**テーマを変えた瞬間だけ顔が変わる**
    //   状態になっていた。看板は1つにする。
    if (themeMode == 0)
    {
        const juce::Rectangle<float> mark (16.0f, 12.0f, 42.0f, 42.0f);
        g.setColour (Palette::yellow);
        g.fillRoundedRectangle (mark, 12.0f);
        g.setColour (Palette::bgBot);
        const float heights[] = { 10.0f, 21.0f, 29.0f, 17.0f, 8.0f };
        for (int i = 0; i < 5; ++i)
            g.fillRoundedRectangle (mark.getX() + 9.0f + i * 5.3f,
                mark.getCentreY() - heights[i] * 0.5f, 3.0f, heights[i], 1.5f);
    }
    else if (kawaiiMascot.isValid() || mascot.isValid())
    {
        juce::Rectangle<float> badge (10.0f, 6.0f, 52.0f, 52.0f);
        g.setColour (Palette::badge);
        g.fillRoundedRectangle (badge, 11.0f);
        g.setColour (Palette::panelLn);
        g.drawRoundedRectangle (badge.reduced (0.5f), 11.0f, 1.0f);
        g.drawImage (kawaiiMascot.isValid() ? kawaiiMascot : mascot,
                     badge.reduced (3.0f), juce::RectanglePlacement::centred, false);
    }

    // v1.5.0 brand: butter title on navy (site palette), single clean pass
    // v2.0.1: 明るいモードで黄タイトルが沈む対策 + サブ行/バージョンが小さすぎて
    //         読めなかったので拡大(11/10px -> 12.5px、バージョンは太字)。
    g.setFont (GzzioLnF::uiFont (20.0f, true));
    {
        const juce::Rectangle<int> tr (70, 5, 180, 28);
        g.setColour (Palette::ink);
        g.drawText (tip::title(), tr, juce::Justification::centredLeft);
    }

    g.setColour (Palette::accentOn (Palette::inkSoft, Palette::bgTop));
    g.setFont (GzzioLnF::uiFont (14.0f, false));
    g.drawText (juce::String::fromUTF8 ("あなたの声を、主役に。"), juce::Rectangle<int> (72, 33, 178, 15),
                juce::Justification::centredLeft);
    g.setColour (Palette::accentOn (Palette::ink, Palette::bgTop));
    g.setFont (GzzioLnF::uiFont (14.0f, true));
    g.drawText (juce::String ("v") + JucePlugin_VersionString
               #if VOCALGZZIO_TRIAL
                + tip::T ("\xe3\x80\x80\xe4\xbd\x93\xe9\xa8\x93\xe7\x89\x88", " TRIAL")
               #endif
                ,
                juce::Rectangle<int> (72, 48, 200, 15), juce::Justification::centredLeft);

    // ---- v1.4.0 stream-loudness meter (header): low / good / hot zones ----
    // Practice for stream mic level: sitting in green = too quiet; aim for the
    // yellow "good" zone; brief red is fine, constant red clips. Maps RMS dB.
    if (! updateNotice.isVisible())
    {
        auto lm = lvMeterArea.toFloat();
        // v2.0.0: どのモードでも背景に沈まない色へ(以前は淡色地でほぼ読めなかった)
        g.setColour (Palette::accentOn (Palette::inkSoft, Palette::bgTop));
        g.setFont (cfont (9.5f, true));
        g.drawText (tip::lv_label(), lm.removeFromTop (11.0f).toNearestInt(),
                    juce::Justification::centredLeft);

        auto bar = lm.removeFromTop (11.0f).reduced (0.0f, 1.0f);
        // zone thresholds along the bar (relative): green .. yellow .. red
        const float gGood = 0.55f, gHot = 0.82f;
        g.setColour (Palette::green.withAlpha (0.30f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * gGood), 2.0f);
        g.setColour (Palette::yellow.withAlpha (0.34f));
        g.fillRoundedRectangle (bar.withX (bar.getX() + bar.getWidth() * gGood)
                                   .withWidth (bar.getWidth() * (gHot - gGood)), 2.0f);
        g.setColour (Palette::salmon.withAlpha (0.34f));
        g.fillRoundedRectangle (bar.withX (bar.getX() + bar.getWidth() * gHot)
                                   .withWidth (bar.getWidth() * (1.0f - gHot)), 2.0f);

        // current level marker (RMS dB -60..0 -> 0..1)
        const float rms = processor.getOutputRmsDb();
        const float t   = juce::jlimit (0.0f, 1.0f, (rms + 40.0f) / 40.0f);   // -40..0 dB usable span
        const float mx  = bar.getX() + t * bar.getWidth();
        const bool  clip = processor.getOutputLevel() > 0.98f;
        const juce::Colour zc = t < gGood ? Palette::green : t < gHot ? Palette::yellow : Palette::salmon;
        g.setColour (zc);
        g.fillRoundedRectangle (mx - 2.0f, bar.getY() - 1.0f, 4.0f, bar.getHeight() + 2.0f, 1.5f);

        // zone caption
        g.setColour (Palette::accentOn (Palette::inkSoft, Palette::bgTop));
        g.setFont (cfont (9.34f, false));   // §4: 100%で実寸14.0px
        juce::String cap = clip ? juce::String::fromUTF8 ("\xe2\x9a\xa0 ") + tip::lv_clip()
                          : t < gGood ? tip::lv_low() : t < gHot ? tip::lv_good() : tip::lv_high();
        g.drawText (cap, lm.toNearestInt(), juce::Justification::centredLeft);
    }

    // ---- v1.5.0 hero band: title + hint / latest auto-setup result ----
    {
        auto r = heroArea.toFloat();
        g.setColour (Palette::panel2);
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (Palette::yellow.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.6f), 14.0f, 1.4f);
        // v2.0.0: 明るいモードでは黄がクリーム地に溶けていたので読める濃さへ寄せる
        g.setColour (Palette::accentOn (Palette::yellow, Palette::panel2));
        // v2.12.0: 118px 固定の見出しで「おまか…」になっていた。fitText で受ける。
        // v3.0-c: かんたんモードの席は resized() が実測で決める（ボタンと重ならない幅）。
        fitText (g, tip::hero_title(),
                 heroBig && ! heroTitleArea.isEmpty()
                     ? heroTitleArea
                     : heroArea.withTrimmedLeft (16).withWidth (118),
                 juce::Justification::centredLeft, heroBig ? 20.0f : 15.5f, true);
        g.setColour (autoSetupMsg.isNotEmpty() ? Palette::ink : Palette::inkSoft);
        g.setFont (cfont (heroBig ? 14.0f : 12.5f, autoSetupMsg.isNotEmpty()));
        // かんたんモードは中央の大ボタンの右側へ、結果/ヒントを描く
        // v2.9.0: 右端はセッション席(バッジの左)で止める。以前は帯の右端まで
        // 描いていたので、そのままだとバッジの下へ潜り込む。
        const int msgX = heroBig ? songSetupButton.getRight() + 18
                                 : heroArea.getX() + 10 + 128 + 128 + 8 + 128 + 14;
        const int msgR = srcModeLabelArea.isEmpty() ? checkupButton.getX() - 12
                                                    : srcModeLabelArea.getX() - 12;
        if (msgR > msgX + 40)
            fitText (g, autoSetupMsg.isNotEmpty() ? autoSetupMsg
                        : heroBig ? tip::easy_go_hint() : tip::hero_hint(),
                     juce::Rectangle<int> (msgX, heroArea.getY(),
                                           msgR - msgX, heroArea.getHeight()),
                     juce::Justification::centredLeft,
                     heroBig ? 14.0f : 12.5f, autoSetupMsg.isNotEmpty());

        // ---- v2.9.0 追加遅延バッジ ----
        // 「ゼロ遅延」は宣伝文句ではなく測った数字だ、というのが製品の芯なので、
        // いま何サンプル足しているかを常に出す。0なら緑、足していたら黄。
        {
            const int  lat   = processor.addedLatencySamples();
            const double sr  = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
            const bool  zero = (lat <= 0);
            auto br = latBadgeArea.toFloat();
            g.setColour ((zero ? Palette::green : Palette::yellow).withAlpha (0.16f));
            g.fillRoundedRectangle (br, 8.0f);
            g.setColour ((zero ? Palette::green : Palette::yellow).withAlpha (0.75f));
            g.drawRoundedRectangle (br.reduced (0.6f), 8.0f, 1.2f);
            // ★メンバをそのまま removeFromTop すると毎回の再描画で領域が縮む。
            //   かならずコピーを削る。
            auto badge = latBadgeArea;
            g.setColour (Palette::inkSoft);
            g.setFont (cfont (10.5f, false));
            g.drawText (tip::lat_badge_label(), badge.removeFromTop (14),
                        juce::Justification::centred);
            g.setColour (Palette::accentOn (zero ? Palette::green : Palette::yellow, Palette::panel2));
            g.setFont (cfont (heroBig ? 17.0f : 14.0f, true));
            g.drawText ("+" + juce::String (lat * 1000.0 / sr, 1) + " ms",
                        badge, juce::Justification::centred);
        }
    }

    auto drawPanel = [&g, this] (juce::Rectangle<int> area, const juce::String& title)
    {
        if (area.isEmpty()) return;      // v2.4.0 かんたんモードで使わないパネル
        auto r = area.toFloat();
        // v1.8.5: 自然モードでは背景(四季)がパネル越しに透けるよう半透明に。
        // ノブの視認性は保ちたいので、うっすら地色を残す程度(alpha 0.72)。
        const float panelA = (themeMode == 2) ? 0.72f : 1.0f;
        g.setColour (Palette::panel.withMultipliedAlpha (panelA));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));   // top elevation highlight
        g.fillRect (r.reduced (10.0f, 0.0f).withHeight (1.0f).translated (0.0f, 1.0f));
        g.setColour (Palette::panelLn);
        g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.2f);
        if (title.isNotEmpty())
        {
            g.setColour (Palette::yellow);                       // butter section chip
            g.fillRoundedRectangle ((float) area.getX() + 14.0f, (float) area.getY() + 8.0f,
                                    4.0f, 12.0f, 2.0f);
            g.setColour (Palette::ink);
            g.setFont (cfont (13.5f, true));
            g.drawText (title, area.withTrimmedLeft (24).withHeight (22).translated (0, 3),
                        juce::Justification::centredLeft);
        }
    };
    // v2.10.0 音源モードの見出し（コンボの左に置く。どちらのモードでも出す）
    if (! srcModeLabelArea.isEmpty())
    {
        g.setColour (Palette::ink.withAlpha (0.75f));
        fitText (g, tip::src_label(), srcModeLabelArea,
                 juce::Justification::centredRight, 13.0f, true);   // v2.12.0「音の…」対策
    }

    // ---- v3.1 §4「1つずつ」の中央 ----
    if (focusMode && ! focusHeadArea.isEmpty())
    {
        auto sc2 = [this] (float v) { return v * fontScale; };
        // 見出し: 番号（実際の順）＋ 名前
        {
            g.setColour (Palette::ink);
            //  番号は「いま実際に何番目を通るか」。列のカードと同じ数字にする。
            const juce::String num = focusModule == 8 ? tip::end_in_mark()
                                   : focusModule == 9 ? tip::end_out_mark()
                                   : juce::String (pathRail.displayPosOf (focusModule) + 1);
            const auto bigF = cfont (26.0f, true);
            g.setFont (bigF);
            const int numW = juce::GlyphArrangement::getStringWidthInt (bigF, num) + 16;
            auto nameArea = focusHeadArea.withTrimmedLeft (numW).withHeight ((int) sc2 (34));
            //  番号は名前と同じ行に、同じ高さでそろえる（下にずれて見えていた）
            g.drawText (num, focusHeadArea.withWidth (numW).withHeight (nameArea.getHeight()),
                        juce::Justification::centredLeft);
            const juce::String nm = focusModule == 8 ? tip::end_in_name()
                                  : focusModule == 9 ? tip::end_out_name()
                                  : juce::String::fromUTF8 (tip::english
                                        ? gz::ModuleChain::enName (focusModule)
                                        : gz::ModuleChain::jpName (focusModule));
            g.drawText (nm, nameArea, juce::Justification::centredLeft);
            g.setColour (Palette::inkSoft);
            g.setFont (cfont (17.0f));
            const juce::String nt = focusModule == 8 ? tip::end_in_note()
                                  : focusModule == 9 ? tip::end_out_note()
                                  : juce::String::fromUTF8 (tip::english
                                        ? gz::ModuleChain::enNote (focusModule)
                                        : gz::ModuleChain::jpNote (focusModule));
            g.drawText (nt, focusHeadArea.withTrimmedLeft (numW).withTop (nameArea.getBottom()),
                        juce::Justification::topLeft);
        }
        // 説明の箱（ツマミに乗ると、その使いかたが出る）。席が無いときは出さない。
        if (! focusTipArea.isEmpty())
        {
            const auto r = focusTipArea.toFloat();
            g.setColour (Palette::panel2.withAlpha (0.9f));
            g.fillRoundedRectangle (r, 12.0f);
            g.setColour (Palette::panelLn);
            g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
            const auto body = focusTipArea.reduced ((int) sc2 (14), (int) sc2 (10));
            const bool has = focusTipText.isNotEmpty();
            g.setColour (has ? Palette::ink : Palette::inkSoft.withAlpha (0.75f));
            g.setFont (cfont (has ? 17.0f : 15.0f));
            // ★日本語は drawFittedText では折れない。カードと同じ自前の折り返しを使う。
            const auto f = cfont (has ? 17.0f : 15.0f);
            const int lh = (int) sc2 (22);
            //  ★箱に入る行数までしか折らない。4行で折ってから2行しか描けない箱に
            //   入れると、はみ出した行が下の物に重なる（実際そうなった）。
            const int maxRows = juce::jmax (1, body.getHeight() / lh);
            //  入/出はスイッチが無い。なぜ無いのかを、何にも触れていないときに言う。
            const juce::String idle = focusModule >= 8 ? tip::end_why() : tip::focus_hint();
            const auto rows = railGeom::wrapJa (f, has ? focusTipText : idle,
                                                body.getWidth(), maxRows);
            for (int i = 0; i < rows.size(); ++i)
                g.drawText (rows[i], body.withTop (body.getY() + i * lh).withHeight (lh),
                            juce::Justification::centredLeft);
        }
        // ---- 右1/4「ぜんたい」----
        if (! focusSideArea.isEmpty())
        {
            const auto r = focusSideArea.toFloat();
            g.setColour (Palette::panel.withMultipliedAlpha (themeMode == 2 ? 0.72f : 1.0f));
            g.fillRoundedRectangle (r, 14.0f);
            g.setColour (Palette::panelLn);
            g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.2f);

            auto inner = focusSideArea.reduced ((int) sc2 (14), (int) sc2 (12));
            g.setColour (Palette::ink);
            g.setFont (cfont (20.0f, true));
            g.drawText (tip::side_title(), inner.withHeight ((int) sc2 (26)),
                        juce::Justification::centredLeft);

            // 入出力メーター。案A の「マイクの音 / 出ていく音」。
            //  ★数字ではなく帯で出す。ここは「出ているか / 割れていないか」を
            //   横目で見る所なので、読む物にしない。
            if (! focusMeterArea.isEmpty())
            {
                auto m = focusMeterArea;
                const int lh = (int) sc2 (20), bh2 = (int) sc2 (14);
                auto bar = [&] (const juce::String& name, float lvl)
                {
                    g.setColour (Palette::inkSoft);
                    g.setFont (cfont (15.0f));
                    g.drawText (name, m.removeFromTop (lh), juce::Justification::centredLeft);
                    auto b = m.removeFromTop (bh2).toFloat();
                    g.setColour (Palette::track.withAlpha (0.8f));
                    g.fillRoundedRectangle (b, 7.0f);
                    //  -60dB を左端、0dB を右端。人の耳に近い出かたにする。
                    const float db = juce::Decibels::gainToDecibels (lvl, -60.0f);
                    const float t  = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
                    if (t > 0.001f)
                    {
                        g.setColour (db > -1.0f ? Palette::salmon : Palette::green);
                        g.fillRoundedRectangle (b.withWidth (juce::jmax (7.0f, b.getWidth() * t)), 7.0f);
                    }
                    m.removeFromTop ((int) sc2 (8));
                };
                bar (tip::side_in(),  processor.getInputLevel());
                bar (tip::side_out(), processor.getOutputLevel());
            }

            // 文字/画面の大きさ。スライダーは子コンポーネントなので、ここは見出しだけ。
            auto labelFor = [&] (const juce::Rectangle<int>& sl, const juce::String& nm, double v)
            {
                auto lr = juce::Rectangle<int> (inner.getX(), sl.getY() - (int) sc2 (24),
                                                inner.getWidth(), (int) sc2 (24));
                g.setColour (Palette::ink);
                g.setFont (cfont (17.0f, true));
                g.drawText (nm, lr, juce::Justification::centredLeft);
                g.setColour (Palette::inkSoft);
                g.drawText (juce::String (juce::roundToInt (v * 100.0)) + "%", lr,
                            juce::Justification::centredRight);
            };
            labelFor (fontSlider.getBounds(), tip::side_fontsize(), fontSlider.getValue());
            labelFor (zoomSlider.getBounds(), tip::side_zoom(),     zoomSlider.getValue());

            //  ひとこと（設計書§4-0 の宣言をそのまま画面に出す）
            {
                auto nr = focusSideArea.withTop (zoomSlider.getBottom() + (int) sc2 (10))
                                       .reduced ((int) sc2 (14), 0)
                                       .withHeight ((int) sc2 (72));
                g.setColour (Palette::inkSoft.withAlpha (0.85f));
                const auto f = cfont (14.0f);
                g.setFont (f);
                const auto rows = railGeom::wrapJa (f, tip::side_note(), nr.getWidth(), 4);
                const int nlh = (int) sc2 (19);
                for (int i = 0; i < rows.size(); ++i)
                    g.drawText (rows[i], nr.withTop (nr.getY() + i * nlh).withHeight (nlh),
                                juce::Justification::centredLeft);
            }
        }

    }

    if (! advancedMode)
    {
        // ---- v2.8.0 見えないのに効いているエフェクトの案内帯 ----
        // 「声が変なままなのに、直す場所が画面のどこにも無い」を無くすための帯。
        if (fxHiddenActive && ! fxWarnArea.isEmpty())
        {
            const auto r = fxWarnArea.toFloat();
            g.setColour (Palette::salmon.withAlpha (0.16f));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (Palette::salmon.withAlpha (0.55f));
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.2f);
            g.setColour (Palette::accentOn (Palette::ink, Palette::panel));
            g.setFont (cfont (13.0f, true));
            g.drawText (tip::fxwarn_msg(),
                        fxWarnArea.withTrimmedLeft (14).withTrimmedRight (156),
                        juce::Justification::centredLeft);
        }

        if (! voiceStageArea.isEmpty())
        {
            const auto stage = voiceStageArea.toFloat();
            g.setGradientFill (juce::ColourGradient (Palette::panel2, stage.getTopLeft(),
                                                     Palette::panel, stage.getBottomRight(), false));
            g.fillRoundedRectangle (stage, 16.0f);
            g.setColour (Palette::panelLn);
            g.drawRoundedRectangle (stage.reduced (0.5f), 16.0f, 1.0f);
            auto textArea = voiceStageArea.reduced (28, 20);
            g.setColour (Palette::accentOn (Palette::green, Palette::panel));
            g.setFont (GzzioLnF::uiFont (18.0f, true));
            g.drawText (juce::String::fromUTF8 ("歌う。話す。あなたらしく。"),
                        textArea.removeFromTop (22), juce::Justification::centredLeft);
            g.setColour (Palette::ink);
            g.setFont (GzzioLnF::uiFont (54.0f, true));
            g.drawText (juce::String::fromUTF8 ("声を、まんなかに。"),
                        textArea.removeFromTop (72), juce::Justification::centredLeft);
            g.setColour (Palette::inkSoft);
            g.setFont (GzzioLnF::uiFont (20.0f, false));
            g.drawText (juce::String::fromUTF8 ("ノイズを整え、声の表情をつくる。"),
                        textArea.removeFromTop (26), juce::Justification::centredLeft);
            auto steps = textArea.removeFromBottom (34);
            const char* captions[] = { "01  整える", "02  音をつくる", "03  届ける" };
            const juce::Colour accents[] = { Palette::green, Palette::blue, Palette::yellow };
            const int cellW = steps.getWidth() / 3;
            for (int i = 0; i < 3; ++i)
            {
                auto cell = steps.removeFromLeft (cellW).reduced (0, 2).withTrimmedRight (8);
                g.setColour (accents[i].withAlpha (0.08f));
                g.fillRoundedRectangle (cell.toFloat(), 6.0f);
                g.setColour (Palette::accentOn (accents[i], Palette::panel));
                g.setFont (GzzioLnF::uiFont (14.0f, true));
                g.drawText (juce::String::fromUTF8 (captions[i]), cell,
                            juce::Justification::centred);
            }
        }

        // かんたん画面は一列のダイヤルと三つの処理段階で案内する。
        // 見出しもヒントも、ツマミが大きくなったぶんに合わせて大きく描く
        // (パネル見出しの既定 13.5px のままだと、下のツマミに完全に負ける)。
        drawPanel (cleanArea, juce::String());
        const int hh = 36;
        g.setColour (Palette::yellow);
        g.fillRoundedRectangle ((float) cleanArea.getX() + 16.0f, (float) cleanArea.getY() + 12.0f,
                                5.0f, 18.0f, 2.5f);
        g.setColour (Palette::accentOn (Palette::ink, Palette::panel));
        g.setFont (cfont (17.5f, true));
        g.drawText (tip::easy_panel(),
                    cleanArea.withTrimmedLeft (30).withHeight (hh).translated (0, 5),
                    juce::Justification::centredLeft);
        g.setColour (Palette::accentOn (Palette::inkSoft, Palette::panel));
        g.setFont (cfont (12.5f, false));
        g.drawText (juce::String::fromUTF8 ("まずは静かな状態でノイズを学習"),
                    cleanArea.withHeight (hh).translated (0, 5).withTrimmedRight (310),
                    juce::Justification::centredRight);

        // 調整の流れは、対応するつまみの真下にまとめて置く。
        auto guide = cleanArea.withTrimmedTop (cleanArea.getHeight() - 57).reduced (20, 9);
        const char* groupNames[] = { "整える  /  入力とノイズ", "磨く  /  声の輪郭と音量", "届ける  /  ひびきと仕上げ" };
        const juce::Colour groupColours[] = { Palette::green, Palette::blue, Palette::yellow };
        const int shownKnobs = revMixK.slider.isVisible() ? 6 : 5;
        const int pairWidth = guide.getWidth() * 2 / shownKnobs;
        const int groupWidths[] = { pairWidth, pairWidth, guide.getWidth() - pairWidth * 2 };
        for (int i = 0; i < 3; ++i)
        {
            auto group = guide.removeFromLeft (groupWidths[i]).reduced (8, 0);
            g.setColour (groupColours[i].withAlpha (0.35f));
            g.fillRect (group.getX(), group.getY(), group.getWidth(), 1);
            g.setColour (Palette::accentOn (groupColours[i], Palette::panel));
            g.setFont (GzzioLnF::uiFont (14.0f, true));
            g.drawText (juce::String::fromUTF8 (groupNames[i]), group.withTrimmedTop (8),
                        juce::Justification::centred);
        }
    }
    else
    {
    drawPanel (cleanArea, tip::T ("\x31\x20\xe3\x81\x8d\xe3\x82\x8c\xe3\x81\x84\xe3\x81\xab\xe3\x81\x99\xe3\x82\x8b", "1 CLEAN UP"));
    drawPanel (dynArea,   tip::T ("\x32\x20\xe9\x9f\xb3\xe9\x87\x8f\xe3\x82\x92\xe3\x81\x9d\xe3\x82\x8d\xe3\x81\x88\xe3\x82\x8b", "2 LEVEL"));
    // v3.0「つぶさない」がいま何dBゆるめているか。**数字を出す**のは、
    //  「判断が曖昧で雑」と言われたときの答えが「賢くします」ではなく
    //  「何をどう測ってそう決めたかを見せます」だと思っているから（点検と同じ思想）。
    if (! crushReadArea.isEmpty() && crushBtn.getToggleState())
    {
        const float db = processor.getCrushGuardDb();
        g.setColour (db > 0.3f ? Palette::accentOn (Palette::green, Palette::panel)
                               : Palette::inkSoft.withAlpha (0.6f));
        fitText (g, juce::String (db, 1) + " dB", crushReadArea,
                 juce::Justification::centredRight, 13.0f, true);
    }
    drawPanel (toneArea,  tip::T ("\x33\x20\xe9\x9f\xb3\xe8\x89\xb2\xe3\x82\x92\xe3\x81\xa4\xe3\x81\x8f\xe3\x82\x8b", "3 TONE"));
    drawPanel (spaceArea, {}); // 見出しの席を明示的なリバーブ入／切に使う。
    // v1.6.0: caption for the reverb-type pulldown in the section-4 header
    // v3.0-c: 席は resized() が実測で決める（空なら「テンポフィット」に譲った合図）
    if (! revTypeLabArea.isEmpty())
    {
        g.setColour (Palette::accentOn (Palette::inkSoft, Palette::panel));   // v2.0.0 読める濃さへ
        fitText (g, tip::T ("\xe3\x81\xb2\xe3\x81\xb3\xe3\x81\x8d\xe3\x81\xae\xe7\xa8\xae\xe9\xa1\x9e", "Reverb type"),
                 revTypeLabArea, juce::Justification::centredRight, 11.5f, true);
    }
    drawPanel (seqArea,   juce::String());   // header hosts the EQ | effects tabs

    if (currentTab == 0)
    {
        // The EQ graph itself is drawn by the EQGraph child component (eqGraphArea).
        // Info line in the gap between the graph and the knob strip shows the
        // description of the last selected preset (default: usage hint).
        g.setColour (Palette::accentOn (Palette::inkSoft, Palette::panel));   // v2.0.0
        // v3.0-c ★1行 drawText だったので、150% では末尾が「…」で切れていた。
        //  説明文は**折り返してよい**（設計書 §4）。2行まで使って省略しない。
        const auto info = infoText.isNotEmpty() ? infoText : tip::subtitle_hint();
        const auto ifont = cfont (12.0f);
        const int  need  = juce::GlyphArrangement::getStringWidthInt (ifont, info);
        const int  lines = (need > seqArea.getWidth() - 8) ? 2 : 1;
        g.setFont (ifont);
        g.drawFittedText (info,
                          juce::Rectangle<int> (seqArea.getX() + 4, eqGraphArea.getBottom() + 3,
                                                seqArea.getWidth() - 8, lines * 18),
                          juce::Justification::centredTop, lines, 1.0f);
    }
    else
    {
        // effects tab: section mini-headers, hairline separators, description strip
        g.setColour (Palette::accentOn (Palette::inkSoft, Palette::panel));   // v2.0.0
        g.setFont (cfont (11.0f, true));
        g.drawText (tip::fx_sec_space(), fxRow1.withHeight (18).withTrimmedLeft (14),
                    juce::Justification::centredLeft);
        g.drawText (tip::fx_sec_delay(), fxRow2.withHeight (18).withTrimmedLeft (14),
                    juce::Justification::centredLeft);
        g.drawText (tip::fx_sec_char(),  fxRow3.withHeight (18).withTrimmedLeft (14),
                    juce::Justification::centredLeft);
        g.setColour (Palette::panelLn.withAlpha (0.6f));
        g.fillRect (fxRow2.getX() + 8, fxRow2.getY() - 1, fxRow2.getWidth() - 16, 1);
        g.fillRect (fxRow3.getX() + 8, fxRow3.getY() - 1, fxRow3.getWidth() - 16, 1);

        // ---- v1.9.0: framed "Auto-Tune / Key" band ----
        {
            auto abx = analysisArea.toFloat();
            g.setColour (Palette::panel2);
            g.fillRoundedRectangle (abx, 8.0f);
            g.setColour (Palette::ice.withAlpha (0.55f));
            g.drawRoundedRectangle (abx.reduced (0.5f), 8.0f, 1.2f);

            // faint divider between the auto-tune column and the key-detect column
            if (! keyColArea.isEmpty())
            {
                g.setColour (Palette::ice.withAlpha (0.25f));
                g.fillRect (keyColArea.getX() - 6, analysisArea.getY() + 10, 1, analysisArea.getHeight() - 20);
            }

            g.setColour (Palette::ice);
            g.setFont (cfont (11.5f, true));
            g.drawText (tip::at_sec_title(),   // "オートチューン" over the left column
                        juce::Rectangle<int> (atColArea.getX(), analysisArea.getY() + 2, atColArea.getWidth(), 15),
                        juce::Justification::centredLeft);
            g.drawText (tip::keydetect_head(),  // "キー検出"
                        juce::Rectangle<int> (keyColArea.getX(), analysisArea.getY() + 2, keyColArea.getWidth(), 15),
                        juce::Justification::centredLeft);

            // slider labels ("補正" / "速さ")
            g.setColour (Palette::inkSoft);
            fitText (g, tip::at_amount_label(), atLabelAmt, juce::Justification::centredLeft, 10.5f, false);
            fitText (g, tip::at_speed_label(),  atLabelSpd, juce::Justification::centredLeft, 10.5f, false);
            fitText (g, tip::orn_label(),       atLabelOrn, juce::Justification::centredLeft, 10.5f, false);

            // v2.7.0: いま何を守っているかを出す。効いているのが目で分かると、
            // 「本当に働いているのか」という不安が消える(数字は嘘をつかない)。
            {
                const float pr = processor.getOrnProtect();
                if (pr > 0.12f)
                {
                    const int kd = processor.getOrnKind();
                    g.setColour (Palette::ice.withAlpha (juce::jlimit (0.35f, 1.0f, pr)));
                    g.drawText (kd == 2 ? tip::orn_kobu() : tip::orn_scoop(),
                                ornStatusArea, juce::Justification::centredLeft);
                    g.setColour (Palette::inkSoft);
                }
            }

            // key-detection result read-out (below the buttons in the key column)
            juce::Rectangle<int> res (keyColArea.getX(), keyColArea.getY() + 30,
                                      keyColArea.getWidth(), juce::jmax (14, keyColArea.getHeight() - 30));
            juce::String rtxt;
            if (keyScaleMsg.isNotEmpty()) rtxt = keyScaleMsg;
            if (chordMsg.isNotEmpty())    rtxt += (rtxt.isNotEmpty() ? juce::String ("\n") : juce::String()) + chordMsg;
            const bool haveRes = rtxt.isNotEmpty();
            g.setColour (haveRes ? Palette::ink : Palette::inkSoft.withAlpha (0.7f));
            g.setFont (cfont (10.5f, false));
            g.drawFittedText (haveRes ? rtxt : tip::at_detect_hint(),
                              res, juce::Justification::topLeft, 2, 0.9f);
        }

        g.setColour (Palette::accentOn (Palette::inkSoft, Palette::panel));   // v2.0.0
        g.setFont (cfont (10.5f));
        g.drawText (fxInfoText.isNotEmpty() ? fxInfoText : tip::fx_hint(),
                    juce::Rectangle<int> (seqArea.getX(), seqArea.getBottom() - 22,
                                          seqArea.getWidth(), 15),
                    juce::Justification::centred);
    }
    }   // end of advanced-only panels (v2.4.0)

    drawJuiceServer (g);   // v1.7.0 corner juice dispenser (yuru-kawa only)

    // v1.7.0 yuru-kawa: puniguji as a BIG, translucent ("see-through") hero in the
    // bottom-right. Large presence, but low opacity so the controls under it stay
    // usable and readable.
    if (pastelTheme() && kawaiiMascot.isValid())
    {
        const float mh = 500.0f;
        const float mw = mh * (float) kawaiiMascot.getWidth() / (float) kawaiiMascot.getHeight();
        const float mx = -mw * 0.26f;                        // anchored LEFT (part off-edge)
        const float my = (float) getHeight() - mh * 0.82f;   // anchored bottom
        // v2.11.0 よる: 白っぽい絵なので、昼と同じ濃さだと暗い下地で浮きすぎる。
        // 薄くしたうえで、後ろに月あかりのかさを敷いて「照らされている」ようにする。
        if (nightTheme())
        {
            const float cx = mx + mw * 0.5f, cy = my + mh * 0.42f;
            const float hr = mh * 0.62f;
            g.setGradientFill (juce::ColourGradient (juce::Colour (0x1ef3e6b8), cx, cy,
                                                     juce::Colour (0x00f3e6b8), cx + hr, cy, true));
            g.fillEllipse (cx - hr, cy - hr, hr * 2.0f, hr * 2.0f);
        }
        g.setOpacity (nightTheme() ? 0.26f : 0.38f);
        g.drawImageTransformed (kawaiiMascot,
            juce::AffineTransform::scale (mw / (float) kawaiiMascot.getWidth(),
                                          mh / (float) kawaiiMascot.getHeight())
                .translated (mx, my), false);
        g.setOpacity (1.0f);
    }
}

void VocalGzzioContent::placeRow (juce::Rectangle<int> area, std::initializer_list<Knob*> ks)
{
    placeRowVec (area, std::vector<Knob*> (ks));
}

void VocalGzzioContent::placeRowVec (juce::Rectangle<int> area, const std::vector<Knob*>& ks)
{
    area.removeFromTop (juce::roundToInt (22.0f * rowScale));   // panel header
    area.reduce (8, 2);
    // v1.5.0: label / value boxes grow with the text-size slider (they used to be
    // fixed 16/18 px, which silently capped the font growth via drawFittedText).
    // v2.4.0: かんたんモードは rowScale で「文字も箱もまとめて」大きくする。
    //         箱だけ広げても drawFittedText の都合で字は大きくならないので、
    //         Label の "fontH" プロパティ(GzzioLnF::getLabelFont が読む)も上げる。
    const float fs   = fontScale * rowScale;
    const int   labH = juce::jlimit (16, 52, juce::roundToInt (13.0f * fs));
    const int   valH = juce::jlimit (18, 52, juce::roundToInt (12.5f * fs));
    const int   tbW  = juce::roundToInt (78.0f * rowScale);
    // v3.1 ★出していないツマミの席は詰める。
    //  「使いかた」で ことば・艶 などを画面から出さなくなったので、席を空けたままだと
    //  行の途中に穴が開いて、壊れて見える（実際そう見えた）。
    //  nullptr は今までどおり「わざと空けた余白」なので席を1つ使う。
    //  出していないツマミ（setVisible(false)）だけを席から外す。
    int n = 0;
    for (auto* k : ks)
        if (k == nullptr || k->slider.isVisible()) ++n;
    const int cw = area.getWidth() / juce::jmax (1, n);
    int i = 0;
    for (auto* k : ks)
    {
        if (k != nullptr && ! k->slider.isVisible())
            continue;       // 席そのものを使わない（次のツマミが左へ詰まる）
        if (k != nullptr)   // nullptr = 空き枠(セル幅をそろえるための余白)
        {
            k->slider.getProperties().set ("wideGlass", rowScale > 1.05f);
            k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, tbW, valH);
            juce::Rectangle<int> cell (area.getX() + i * cw, area.getY(), cw, area.getHeight());
            k->label.setBounds (cell.removeFromTop (labH));
            k->slider.setBounds (cell.reduced (1));

            // ツマミ名と、下の数値表示のフォント
            k->label.getProperties().set ("fontH", 12.0 * rowScale);
            for (auto* c : k->slider.getChildren())
                if (auto* lb = dynamic_cast<juce::Label*> (c))
                {
                    lb->getProperties().set ("fontH", 13.0 * rowScale);
                    lb->getProperties().set ("bold", rowScale > 1.05f);
                }
        }
        ++i;
    }
}

void VocalGzzioContent::resized()
{
    if (isOverview()) { resizedOverview(); return; }
    const int W   = getWidth();
    const int H   = getHeight();
    const int pad = 12;
    const int bh  = 22;
    voiceStageArea = {};

    // cell placer without a panel header (shared by several sections)
    auto placeCells = [fs2 = fontScale] (juce::Rectangle<int> area, std::initializer_list<Knob*> ks)
    {
        const int labH = juce::jlimit (15, 28, juce::roundToInt (12.0f * fs2));
        const int valH = juce::jlimit (18, 28, juce::roundToInt (12.0f * fs2));
        const int n  = (int) ks.size();
        const int cw = area.getWidth() / juce::jmax (1, n);
        int i = 0;
        for (auto* k : ks)
        {
            k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, valH);
            juce::Rectangle<int> cell (area.getX() + i * cw, area.getY(), cw, area.getHeight());
            k->label.setBounds (cell.removeFromTop (labH));
            k->slider.setBounds (cell.reduced (1));
            ++i;
        }
    };

    // ======================= ヘッダ ==========================================
    //
    // v2.12.0 ★ヘッダの席取りを作り直した。ここは指摘そのものへの返事なので、
    //  何が壊れていて、なぜこの形にしたのかを残す。
    //
    //  壊れていたこと:
    //   1段目は ロゴ252 + コンボ3本×170 = 762px を左から使い、そこへ右から
    //   368px のテーマチップ帯を**重ねて**いた。1280px の窓に対して合計は
    //   1130px。使いかたボタン3つ(弾き語り/トーク配信/Band)の居場所は最初から
    //   無く、Band がチップの下敷きになって消えていた。2段目も RESET が
    //   幅58px に押し込まれて「RES…」になっていた。
    //
    //  直しかた（3つ）:
    //   (a) テーマチップ6枚をヘッダから追い出した。「一度決めたら滅多に触らない
    //       設定」が、いちばん良い席を368pxも占めていた。しかも文字は11.5px固定で
    //       文字サイズを上げても大きくならない——読めない文字が特等席にいた。
    //       行き先は「文字・見た目」の吹き出し（132x40px・文字16px）。
    //   (b) 幅は**測って**決める。固定幅は、文字を大きくすればいつか必ず破れる。
    //   (c) それでも入りきらないときは、**縮めずに段を増やす**。
    //       縮めれば「…」になる＝論外。だから 150% では3段に組み替える。
    //       （どの%でも同じ位置に見えることより、読めることを優先する）
    //
    //  段組み A（2段・既定）        段組み B（3段・大きい文字）
    //   1: コンボ3 + 使いかた3 + 見た目   1: 使いかた3 ......... 見た目
    //   2: A/B/保存/読込/RESET + メーター 2: コンボ3 ......... かんたんモード
    //      ......... かんたんモード       3: A/B/保存/読込/RESET + メーター
    // =========================================================================
    auto* lnf = dynamic_cast<GzzioLnF*> (&getLookAndFeel());
    const float fRatio = juce::jmax (1.0f, fontScale / 1.5f);   // ユーザーの%（1.0=100%）
    const int   rowH   = juce::roundToInt (36.0f * juce::jmin (1.35f, fRatio));
    const int   gapY   = 6;

    // --- それぞれの「文字が入る幅」をまず測る -------------------------------
    juce::TextButton* scenes[3] = { &sceneSolo, &sceneTalk, &sceneBand };
    int sceneW[3] = { 96, 88, 78 };                 // LnF が取れないときの保険
    int sceneTotal = 0;
    for (int i = 0; i < 3; ++i)
    {
        if (lnf != nullptr) sceneW[i] = lnf->buttonWidthFor (*scenes[i], rowH, 24);
        sceneTotal += sceneW[i] + 6;
    }
    const int lookW = lnf != nullptr ? juce::jmax (124, lnf->buttonWidthFor (sizePopBtn, rowH, 28))
                                     : 148;
    // v3.0「効果のオンオフ」も実測（英語表記でも切れないように）。
    // v3.0-c: くわしい画面では左の常設列が本体なので、このボタンは
    //         **かんたんモードのときだけ**出す。ヘッダに 152px 返ってくる。
    modPopBtn.setVisible (! advancedMode);
    const int modsW = ! advancedMode
                        ? (lnf != nullptr ? juce::jmax (128, lnf->buttonWidthFor (modPopBtn, rowH, 28))
                                          : 152)
                        : 0;
    // コンボは「いま出ている文字」が切れない幅（矢印と余白で+46px）。
    auto comboNeed = [this, lnf] (juce::ComboBox& c, const juce::String& placeholder)
    {
        if (lnf == nullptr) return 165;
        const auto  f = lnf->getComboBoxFont (c);
        const auto& t = c.getText().isNotEmpty() ? c.getText() : placeholder;
        return juce::jmax (juce::GlyphArrangement::getStringWidthInt (f, t),
                           juce::GlyphArrangement::getStringWidthInt (f, placeholder)) + 46;
    };
    const int comboNeedW = juce::jmax (comboNeed (voiceBox, tip::voice_placeholder()),
                                       juce::jmax (comboNeed (micBox, tip::mic_placeholder()),
                                                   comboNeed (eqPresetBox,
                                                              tip::eqpreset_placeholder())));
    const int btnRowW = [&]
    {
        int t = 0;
        for (auto* b : { &abA, &abB, &abCopy, &saveButton, &loadButton, &resetButton })
            t += (lnf != nullptr ? lnf->buttonWidthFor (*b, rowH, 18) : 54) + 6;
        return t;
    }();

    // --- A で入るか？ 1段目(コンボ3+使いかた3+見た目)と2段目(操作列+メーター) --
    const int kLogoW = 252, kMeterW = 250;
    const int tuneW = lnf != nullptr ? juce::jmax (128, lnf->buttonWidthFor (tuningPopBtn, rowH, 24)) : 140;
    const bool fitsA = (kLogoW + comboNeedW * 3 + 10 + sceneTotal + 10 + lookW + tuneW + 8 <= W - pad)
                    && (kLogoW + btnRowW + 10 + kMeterW + 10 + kSwitchW <= W - pad);
    const int rows = fitsA ? 2 : 3;

    int yRow = 8;
    int x    = kLogoW;
    const int comboH = juce::jmax (30, rowH + 2);

    if (fitsA)
    {
        // --- 段組み A: 1段目 = コンボ3 + 使いかた3 + 見た目 -------------------
        sizePopBtn.setBounds (W - pad - lookW, yRow, lookW, rowH);
        tuningPopBtn.setBounds (sizePopBtn.getX() - 8 - tuneW, yRow, tuneW, rowH);
        const int span   = tuningPopBtn.getX() - 12 - x - sceneTotal;
        const int comboW = juce::jlimit (comboNeedW, 200, span / 3 - 5);
        for (auto* c : { &voiceBox, &micBox, &eqPresetBox })
        { c->setBounds (x, yRow, comboW, comboH); x += comboW + 5; }
        x += 5;
        for (int i = 0; i < 3; ++i)
        { scenes[i]->setBounds (x, yRow + 1, sceneW[i], rowH); x += sceneW[i] + 6; }
        yRow += comboH + gapY;
    }
    else
    {
        // --- 段組み B: 1段目 = 使いかた3 ......... 見た目 --------------------
        //  文字が大きいときは「いま何をしているか(使いかた)」が最上段に単独で並ぶ。
        //  情報の重みとしても、こちらのほうが正しい並びになる。
        sizePopBtn.setBounds (W - pad - lookW, yRow, lookW, rowH);
        for (int i = 0; i < 3; ++i)
        { scenes[i]->setBounds (x, yRow, sceneW[i], rowH); x += sceneW[i] + 6; }
        yRow += rowH + gapY;

        // 2段目 = コンボ3 ......... かんたんモード
        tuningPopBtn.setBounds (sizePopBtn.getX() - 8 - tuneW, 8, tuneW, rowH);
        x = kLogoW;
        const int span   = (W - pad - kSwitchW - 14) - x;
        const int comboW = juce::jlimit (comboNeedW, 340, span / 3 - 5);
        for (auto* c : { &voiceBox, &micBox, &eqPresetBox })
        { c->setBounds (x, yRow, comboW, comboH); x += comboW + 5; }
        yRow += comboH + gapY;
    }

    // --- かんたんモードのスイッチ: A=2段目 / B=2段目 の右端 -------------------
    crossArea = { W - pad - kSwitchW,
                  rows == 2 ? 8 + comboH + gapY : 8 + rowH + gapY,
                  kSwitchW, juce::jmax (kSwitchH, rowH) };
    themePainter.setBounds (getLocalBounds());

    // --- 最終段: A/B/保存/読込/RESET（左）+ 音量メーター ----------------------
    // v2.12.0 ★ここも固定幅をやめた。RESET が幅58pxで「RES…」になっていた。
    x = kLogoW;
    auto place = [&] (juce::TextButton& b, int minW)
    {
        const int bw = lnf != nullptr ? juce::jmax (minW, lnf->buttonWidthFor (b, rowH, 18))
                                      : minW;
        b.setBounds (x, yRow, bw, rowH);
        x += bw + 6;
    };
    place (abA, 34); place (abB, 34); place (abCopy, 46);
    place (saveButton, 54); place (loadButton, 54); place (resetButton, 58);

    // メーターは操作列の**右隣**から。以前は x=600 固定で、文字を大きくすると
    // RESET が「配信音量メーター」の見出しに重なっていた。
    // v3.0「効果のオンオフ」は最終段の右端へ。
    //  1段目に置くと、その幅ぶんで**100%でも3段に落ちて**しまい、
    //  いつもの画面が間延びした（実際にそうなったので置き場所を変えた）。
    //  2段のときは「かんたんモード」の左、3段のときは行の右端。
    const int modsRight = (modsW > 0)
                            ? (rows == 2 ? crossArea.getX() - 8 - modsW : W - pad - modsW)
                            : (rows == 2 ? crossArea.getX() - 8 : W - pad);
    if (modsW > 0) modPopBtn.setBounds (modsRight, yRow, modsW, rowH);

    const int meterX = juce::jmax (600, x + 14);
    const int meterW = juce::jlimit (120, kMeterW, modsRight - 10 - meterX);
    lvMeterArea  = { meterX, yRow - 2, meterW, juce::jmax (30, rowH + 4) };
    updateNotice.setBounds (meterX, yRow, juce::jmax (120, modsRight - 10 - meterX), rowH);
    yRow += rowH + gapY;

    // v2.12.0: 極小スライダー2本(高さ13px)は廃止。値の器としてだけ残して隠す。
    //  文字を大きくするための操作が、いちばん小さくて押しにくい——本末転倒だった。
    //  実物は「文字・見た目」ボタン → 吹き出しの大きなスライダー(高さ34px)。
    fontSliderLabel.setVisible (false); fontSlider.setVisible (false);
    zoomSliderLabel.setVisible (false); zoomSlider.setVisible (false);
    fontSlider.setBounds (W - pad - 80, 40, 78, 13);
    zoomSlider.setBounds (W - pad - 80, 55, 78, 13);
    juce::ignoreUnused (bh);

    // ---- v1.5.0 hero band: one-press auto setup, right under the header ----
    // v2.4.0: かんたんモードでは、この2つのボタンが主役なので大きくする。
    //         (ツマミだけ大きくしてボタンが小さいままだと、視線の行き先が逆になる)
    // v2.12.0: ヘッダが2段/3段で伸び縮みするので、開始位置はその下端に追従させる。
    heroBig  = ! advancedMode;
    heroTitleArea = {};                       // かんたんモードのときだけ resized が埋める
    heroArea = { pad, juce::jmax (78, yRow), W - pad * 2, heroBig ? 76 : 50 };
    overviewReturn.setBounds (heroArea.getX() + 8, heroArea.getY() + 7, 120, 36);

    // v2.9.0 セッションモード: ヒーロー帯の右端に「セッション」スイッチと
    // 「追加遅延 +0.0ms」バッジを置く。**かんたん/こだわりのどちらでも同じ位置**に
    // 出すので、セッション中にモードを行き来しても迷子にならない。
    // 先に席を取ってから、残りの幅で中央のボタンとヒント文を割り振る。
    const int kSrcBoxW   = 112;    // v2.10.0 音源モード(うた/アコギ/しゃべり)
    const int kSrcLabW   = 62;     //          その見出し「音の種類」
    const int kSessBtnW  = 116;
    const int kSessBadgeW = 108;
    const int kChkBtnW    = 66;    // v2.10.0 点検
    // v2.10.0 音源モードもこの席に入れる。ヘッダ2段目に置いたら、テーマ帯
    //  (crossArea は x=W-380 から、y も2段目まで届く)の下敷きになって見えなかった。
    //  ここは「その回ぜんぶに効く大きな選択」が並ぶ場所なので、意味の上でも合っている。
    const int kSessBlockW = kSrcLabW + 4 + kSrcBoxW + 12
                          + kChkBtnW + 8 + kSessBadgeW + 8 + kSessBtnW + 12;
    {
        const int sbH = 36;
        const int yTop = heroArea.getY() + (heroArea.getHeight() - sbH) / 2;
        sessionButton.setBounds (heroArea.getRight() - 12 - kSessBtnW, yTop, kSessBtnW, sbH);
        latBadgeArea = { sessionButton.getX() - 8 - kSessBadgeW, heroArea.getY() + 3,
                         kSessBadgeW, heroArea.getHeight() - 6 };
        // 点検はバッジのすぐ左。バッジの数字の「出どころ」を確かめる場所なので、隣が自然。
        checkupButton.setBounds (latBadgeArea.getX() - 8 - kChkBtnW, yTop, kChkBtnW, sbH);
        // 音源モードは点検のさらに左。見出し「音の種類」はその左に描く。
        srcModeBox.setBounds (checkupButton.getX() - 12 - kSrcBoxW, yTop, kSrcBoxW, sbH);
        srcModeLabelArea = { srcModeBox.getX() - 4 - kSrcLabW, heroArea.getY() + 3,
                             kSrcLabW, heroArea.getHeight() - 6 };
    }

    if (heroBig)
    {
        // v2.4.0: うた自動(10秒おまかせ)が主役。画面中央にどんと置き、
        // トーク自動はその左に控えめに。ヒントは右側の余白に描く。
        // v2.9.0: セッション席(右端244px)を除いた中央へ寄せる。真ん中のままだと
        // ヒント文の幅が170pxまで潰れて読めなくなる。
        const int bw = 340, bh = 56;
        const int cx = (W - kSessBlockW) / 2;
        // v3.0-c ★「トーク自動」が見出し「おまかせ設定」に食い込んでいた。
        //  見出しの席は 164px 固定、ボタンは中央から左へ 168+12 の固定引き算で、
        //  どちらも相手を見ていなかった。見出しの実幅を測り、足りなければ
        //  2つのボタンをまとめて右へずらす（縮めない・重ねない）。
        const int autoW = 168;
        int songX = cx - bw / 2;
        int autoX = songX - 12 - autoW;
        const int titleNeed = juce::GlyphArrangement::getStringWidthInt (
            cfont (20.0f, true), tip::hero_title());
        heroTitleArea = { heroArea.getX() + 22, heroArea.getY(),
                          juce::jmax (164, titleNeed + 8), heroArea.getHeight() };
        if (autoX < heroTitleArea.getRight() + 14)
        {
            const int shift = heroTitleArea.getRight() + 14 - autoX;
            autoX += shift; songX += shift;
        }
        // 右のセッション席へめり込まないところで止める
        const int rightStop = srcModeLabelArea.getX() - 12;
        if (songX + bw > rightStop) songX = juce::jmax (autoX + autoW + 12, rightStop - bw);
        songSetupButton.setBounds (songX, heroArea.getY() + (heroArea.getHeight() - bh) / 2, bw, bh);
        autoSetupButton.setBounds (autoX, heroArea.getY() + (heroArea.getHeight() - 44) / 2,
                                   autoW, 44);
        songSetupButton.getProperties().set ("fontH", 20.0);   // 大ボタンに合う文字へ
        autoSetupButton.getProperties().set ("fontH", 15.0);
    }
    else
    {
        const int bw = 128, bh = 36;
        auto hb = heroArea.reduced (10, (heroArea.getHeight() - bh) / 2);
        hb.removeFromLeft (128);                       // painted title (hero_title)
        autoSetupButton.setBounds (hb.removeFromLeft (bw).withHeight (bh));
        hb.removeFromLeft (8);
        songSetupButton.setBounds (hb.removeFromLeft (bw).withHeight (bh));
        songSetupButton.getProperties().remove ("fontH");
        autoSetupButton.getProperties().remove ("fontH");
        // the rest of the band paints the hint / result message
    }

    // ---- left column: tuner + CLEAN UP / DYNAMICS / TONE (big knobs) ----
    const int top   = heroArea.getBottom() + 8;   // header (72) + hero band + gap
    const int gap   = 6;

    // ================= v3.0-c 「音のとおり道」の常設列 ========================
    //  設計書 §4「左1/4 = 音のとおり道」。前回は吹き出しの中だったカードを、
    //  ここで画面の左端へ**常時**出す。
    //
    //  幅は固定しない。カードの中の名前とひとことを実測して、いちばん長い物が
    //  切れない幅を出す（文字を150%にすれば列も広がる）。ただし画面の 1/3 を
    //  超えたら打ち止めにして、あふれるぶんはカード側で折り返す。
    //  縦に入り切らないぶんは Viewport が縦スクロールを出す。
    // =========================================================================
    const int railGap = 10;
    const int leftW   = 520;   // v2.3.0: 1段目が7ノブになったぶん左列を拡張
    int railW = 0;
    if (advancedMode)
    {
        // ★列の幅は「文字%」で太らせない。
        //  最初は文字に合わせて広げたが、150%で列が 100px 太り、そのぶん右のEQ側が
        //  痩せて「自動」コンボが「エフェクト」に重なった（ui_fit が 125%/150% で
        //  検出）。**新しい物を足したせいで、前からあった物が壊れる**のがいちばん悪い。
        //  なので取り分は 100% のときの幅に固定し、文字が大きいぶんは
        //  カードの中で縦に積む＋列を縦にスクロールさせて吸収する。
        //  窓を横に広げたぶんだけは、列も広がってよい（右列の取り分を割らない範囲で）。
        const int kRightMin = 804;             // 100%のときの右列の幅。ここは削らない
        //  ★スクロールバーのぶんは**先に**足しておく（第23歩）。
        //   あとから引くだけだと、カードの取り分が 12px 減って「名前＋スイッチ」が
        //   横に並ばなくなり、途中の何枚かだけが縦積みになる（列の途中で形が変わる）。
        //   入/出を足して列がスクロールするようになった回に、実際にそうなった。
        const int kBar = 12;
        pathRail.setUiScale (1.0f);
        const int natural = juce::jlimit (196, 260, pathRail.wantedWidth() + kBar);
        pathRail.setUiScale (fRatio);
        const int want  = juce::jmax (natural, pathRail.wantedWidth() + kBar);
        const int spare = juce::jmax (0, W - pad * 2 - natural - railGap - leftW - 12 - kRightMin);
        railW = natural + juce::jmin (spare, want - natural);
        const int railH = juce::jmax (200, (H - 8) - top);
        railView.setBounds (pad, top, railW, railH);
        int inner = railW;
        if (pathRail.preferredHeight (inner) > railH) inner -= kBar; // スクロールバーのぶん
        pathRail.setSize (inner, pathRail.preferredHeight (inner));
        railView.setVisible (true);
    }
    else
    {
        railView.setVisible (false);
    }
    const int leftX = pad + (advancedMode ? railW + railGap : 0);
    int y = top;

    // ================= v2.4.0 かんたんモードの専用レイアウト =================
    // 旧「スタンダード」は右列(EQ/エフェクト)もツマミも全部出ていて、名前ほど
    // 簡単ではなかった。かんたんモードでは画面を横いっぱいに使い、
    //   おまかせ設定 → チューナー → よく使う7ツマミ
    // だけにする。ツマミが大きくなるぶん、初めての人でも触る場所に迷わない。
    if (! advancedMode)
    {
        const int fullW   = W - pad * 2;
        tuner.setVisible (true);
        const int bgStrip = 46;                       // 下部は背景が見える帯(共通)
        // ジュースグラスは drawJuiceKnob で「セル幅×0.62 / セル高×0.88」に描かれる。
        // セル幅は 1336/7 ≒ 190 なのでグラス幅は約118px。高さを伸ばすほど細長い
        // 試験管になるので、パネル高は使える高さの45%(最大330px)で頭打ちにし、
        // 余りはチューナーへ回す。ツマミだけ縦に伸びる、という崩れ方を防ぐ。
        // 上から素直に積む。余りは下(背景とマスコットが出る帯)へ逃がす。
        // 上下に振り分けると、おまかせ設定とチューナーの間に用途のない空白が
        // できて「間延びした」見え方になる。
        int availTop = top;
        // v2.8.0: 見えないのに効いているエフェクトがあるときだけ、上に案内の帯を出す
        fxWarnArea = {};
        if (fxHiddenActive)
        {
            fxWarnArea = { pad, availTop, fullW, 30 };
            fxWarnButton.setBounds (fxWarnArea.getRight() - 148, fxWarnArea.getY() + 3, 140, 24);
            availTop += 34;
        }
        const int avail   = H - bgStrip - availTop - 8;
        const int tunerH  = 240;
        const int panelH  = juce::jmin (390, avail - tunerH - 14);
        const int stageW = juce::roundToInt ((float) fullW * 0.44f);
        tuner.setBounds (pad, availTop, fullW - stageW - 14, tunerH);
        voiceStageArea = { pad + fullW - stageW, availTop, stageW, tunerH };
        cleanArea = { pad, availTop + tunerH + 14, fullW, panelH };
        // 使わないパネルは空にしておく(paint 側は空なら描かない)
        dynArea = toneArea = spaceArea = seqArea = {};
        eqGraphArea = analysisArea = atColArea = keyColArea = {};

        rowScale = 1.55f;                             // ツマミ名も数値も一回り大きく
        // v2.4.0: マイク音量を先頭に(いちばん基本の「小さい/大きい」を直す場所)
        placeRow (cleanArea.withTrimmedBottom (57), { &inGainK, &denoiseK, &comp2K, &presenceK, &revMixK, &makeupK });
        rowScale = 1.0f;
        learnButton.setBounds (cleanArea.getRight() - 146, cleanArea.getY() + 6, 134, 32);
        relearnBtn.setVisible (false);   // かんたん画面では出さない（トーク自動が面倒を見る）
        // v2.10.0 かんたんモードでも「学習ずみ」を出す(こちらの方が事故りやすい:
        // 10秒おまかせが毎回、自動でノイズ床を学習し直しているのはこの画面)。
        dnClearButton.setBounds (learnButton.getX() - 8 - 116, cleanArea.getY() + 6, 116, 32);

        placeLamp (lampDn,  denoiseK);
        placeLamp (lampRev, revMixK);
        reverbPower.setBounds (revMixK.slider.getX(), cleanArea.getBottom() - 50, revMixK.slider.getWidth(), 36);
        // v3.0-c ★ここは setBounds(0,0,0,0) にしていた。見た目には出ないが
        //  「見えていることになっている幅0のボタン」なので、検査が
        //  「使える幅 -28px」＝**必ず「…」になる物**として拾い続けていた。
        //  隠すつもりなら、ちゃんと隠す。
        crushBtn.setVisible (false);
        crushReadArea = {};
        return;
    }

    // ================= v3.1 §4「1つずつ」の専用レイアウト =================
    //  設計書§4「中央 = 選んだモジュールの詳細。ツマミは一度に最大7個まで」。
    //  左の列（音のとおり道）はそのまま。中央に、選んだ1枚の中身だけを大きく置く。
    //  ★ツマミそのものは今までと同じ物を使い回す（作り直すと値が飛ぶ）。
    //   ここでやるのは「どこへ置くか」だけ。
    if (focusMode)
    {
        //  100%表示のときの実寸で書き、文字% を掛ける（設計書§4-0 の規律）。
        auto sc2 = [this] (int v) { return juce::roundToInt ((float) v * fontScale); };
        fxWarnArea = {};
        const int cx = leftX;
        int cw = W - pad - cx;
        int fy = top;

        // ---- 右1/4「ぜんたい」（設計書§4）----
        //  入出力メーターと、文字/画面の大きさ。
        //  ★案A には「表示をかるくする」もあるが、その正体だった軽量モードは
        //   v2.12.0 で**撤廃した**もの。撤廃した物を、設計図に残っているという
        //   理由だけで戻すのは、決めたことを忘れるのと同じなので入れない。
        //  ★狭いときは出さない。出すために中央を潰したら本末転倒。
        const int sideW = sc2 (272);
        if (cw >= sideW + sc2 (560))
        {
            focusSideArea = { cx + cw - sideW, fy, sideW, juce::jmax (sc2 (200), (H - 10) - fy) };
            cw -= sideW + sc2 (12);
        }
        else
            focusSideArea = {};

        // ★チューナーは「1つずつ」では出さない（設計図・案A のとおり）。
        //  出すか出さないかで席の残りが 130px 変わり、その 130px を巡って
        //  説明の箱と「しあげ」が席を奪い合う。実測すると、チューナーを出した
        //  とたんに 8つの箱のうち 5つで「しあげ」が消えていた（ui_focus が検出）。
        //  チューナーは「1つずつ」を切れば元の画面にいつでもある物なので、
        //  ここでは譲る。迷ったら、**消える物を作らないほう**を選ぶ。
        const int headH0 = sc2 (62);
        tuner.setVisible (false);

        // 見出し（番号＋名前＋ひとこと）
        focusHeadArea = { cx, fy, cw, headH0 };
        fy += headH0 + sc2 (6);

        //  ★スイッチ帯の高さは、席を割り当てる**前**に出す。
        //   あとから「ツマミの席の中から借りる」形にすると、借りきれないときに
        //   説明の箱へはみ出す（実際そうなった: へんしん の2段目が箱に重なった）。
        std::vector<juce::Component*> ex;
        for (auto* c : focusExtras (focusModule)) if (c->isVisible()) ex.push_back (c);
        const int exRows = ex.empty() ? 0 : ((ex.size() > 5) ? 2 : 1);
        const int exH    = ex.empty() ? 0 : (exRows * sc2 (34) + sc2 (8));

        //  ★その使いかたで中身が丸ごと消える箱（しゃべりの「へんしん」など）。
        //   ここを**席を配る前**に知っておく。空だと分かっているのに
        //   「ツマミに乗せると説明が出ます」の箱と「くらべる」を出すと、
        //   どちらも空振りする物になる（押せるのに効かない物は作らない）。
        int visKnobs = 0;
        for (auto* k : focusKnobs (focusModule)) if (k->slider.isVisible()) ++visKnobs;
        const bool nothing = (visKnobs == 0 && ex.empty());

        // ★席の割り当ては「上から順に、残りを見ながら」1回で決める。
        //  ここを「希望を引き算 → 足りなければ下限で止める」形で書くと、
        //  下限どうしの合計が窓を越えたときに**重なる**（実際2回やった:
        //  説明の箱にツマミが乗り、次はスイッチ帯が乗った）。
        //  下限で止めた物どうしは、止めた瞬間に互いを知らないので必ずぶつかる。
        //  なので残りを1本の変数で持ち回して、**引けるぶんしか引かない**。
        const int gapY    = sc2 (8);
        const int bottom  = H - 10;
        const int knobTop = fy;
        const int keepMin = sc2 (96) + exH;                    // ツマミ帯＋スイッチ帯の下限
        int rest = juce::jmax (sc2 (200), bottom - knobTop);   // ここから配る

        const int compareH = sc2 (44);       // 押す物。縮めない
        const int wantKnob = sc2 (190) + exH, minKnob = sc2 (96) + exH;
        const int wantTip  = sc2 (112),       minTip  = sc2 (76);
        juce::ignoreUnused (wantKnob);

        //  ★配りかた: まず**下限で全部の席を確保**し、余りを希望の順に配る。
        //   「欲しいぶんを上から取って、残りを次へ」だと、先に取った物が
        //   余力を吸い切って、後ろの物が席ゼロで消える。
        //   実際それで、スイッチ帯のある5つの箱から「しあげ」が消えていた
        //   （ui_focus が 8つの箱ぜんぶを見て見つけた。目で見て気づける物ではない）。
        int knobH = minKnob, tipH = 0;
        int fixed = compareH + gapY;                     // ツマミ→次
        if (nothing) fixed = 0;                          // くらべるも出さない
        if (! nothing && rest >= minKnob + fixed + minTip + gapY)
        { tipH = minTip; fixed += gapY; }                // 説明→くらべる のぶん

        int surplus = rest - (knobH + tipH + fixed);
        if (surplus < 0)
        {
            //  足りないときに削る順: 説明 → ツマミ。
            int cut = -surplus;
            const int c1 = juce::jmin (cut, tipH);
            if (c1 > 0 && c1 == tipH) fixed -= gapY;
            tipH -= c1; cut -= c1;
            knobH = juce::jmax (sc2 (60) + exH, knobH - cut);
            surplus = 0;
        }
        if (tipH > 0)
        {   const int add = juce::jmin (surplus, wantTip - tipH);
            tipH += add; surplus -= add; }
        knobH += juce::jmax (0, surplus);

        //  上から順に置くだけ。引けるぶんしか引いていないので、重ならない。
        int py = knobTop + knobH + gapY;
        if (tipH > 0) { focusTipArea = { cx, py, cw, tipH }; py += tipH + gapY; }
        else            focusTipArea = {};
        focusCompare.setBounds (cx, py, cw, compareH);
        focusFinishArea = {};

        auto ks = focusKnobs (focusModule);
        std::vector<Knob*> shownK;
        for (auto* k : ks) if (k->slider.isVisible()) shownK.push_back (k);
        const int n = (int) shownK.size();
        //  空の席をそのまま見せると「壊れている」と読まれるので、理由を書く。
        focusEmpty.scale = fontScale;
        focusEmpty.setVisible (nothing);
        if (nothing) focusEmpty.setBounds (cx, knobTop, cw, knobH);
        //  ★2行にするのは「2行ぶんの高さが本当にある」ときだけ。
        //   高さを見ずに2行へ割ると、1行あたり90pxになってグラスが**潰れた
        //   横棒**になる（実際そうなった）。9本なら1行でも1本155pxとれるので、
        //   横に並べたほうがずっと読める。
        const int knobBand = knobH - exH;
        const int rows2 = (n > 7 && knobBand >= sc2 (300)) ? 2 : 1;
        const int rowH  = knobBand / rows2;
        //  ★横幅も絞る。1〜3本しか無い箱に 1400px を等分させると、グラスだけ
        //   バカでかくなって「ここが主役」に見える（サ行おさえ は1本しかない）。
        const int perCell = sc2 (196);
        for (int r = 0; r < rows2; ++r)
        {
            const int from = (r * n) / rows2, to = ((r + 1) * n) / rows2;
            std::vector<Knob*> line (shownK.begin() + from, shownK.begin() + to);
            const int lw = juce::jmin (cw, (int) line.size() * perCell);
            placeRowVec ({ cx + (cw - lw) / 2, knobTop + r * rowH, lw, rowH }, line);
        }
        //  スイッチ帯
        if (! ex.empty())
        {
            const int bh2 = sc2 (34);
            const int ey0 = knobTop + knobBand + sc2 (8);
            for (int r = 0; r < exRows; ++r)
            {
                const int from = (r * (int) ex.size()) / exRows;
                const int to   = ((r + 1) * (int) ex.size()) / exRows;
                const int cnt  = juce::jmax (1, to - from);
                const int ew   = juce::jmin (sc2 (150), (cw - (cnt - 1) * sc2 (8)) / cnt);
                const int tot  = cnt * ew + (cnt - 1) * sc2 (8);
                int ex0 = cx + (cw - tot) / 2;
                for (int i = from; i < to; ++i)
                {
                    ex[(size_t) i]->setBounds (ex0, ey0 + r * bh2, ew, sc2 (30));
                    ex0 += ew + sc2 (8);
                }
            }
        }
        // しあげ（出口）は、どの箱を見ていても触れるように常に置く。
        //  ★幅は絞る。2本しか無い行に 1400px を等分させると、グラスだけ
        //   バカでかくなって「ここが主役」に見えてしまう（実際そうなった）。
        // 使わない席は空に（paint 側は空なら描かない）
        cleanArea = dynArea = toneArea = spaceArea = seqArea = {};
        eqGraphArea = analysisArea = atColArea = keyColArea = {};
        crushBtn.setVisible (false);
        crushReadArea = {};
        //  ★「ひびきの種類」の見出しも消す。ここを消し忘れていて、
        //   ふつうの画面で最後に決まった席のまま、1つずつの右下に
        //   文字だけが浮いていた（スクショで見つけた。検査は部品しか見ないので
        //   **描くだけの文字**は数えられない）。
        revTypeLabArea = {};

        // ---- 「ぜんたい」の中身 ----
        //  文字/画面の大きさは、これまで「文字・見た目」の吹き出しの中にしか
        //  無かった（ヘッダの極小スライダーは v2.12.0 で撤去）。ここでは
        //  設計書§4-0 のとおり **高さ 32px の大きなスライダー**で常設する。
        //  値の器はもとの fontSlider / zoomSlider をそのまま使う（吹き出しと同じ物）。
        if (! focusSideArea.isEmpty())
        {
            auto r = focusSideArea.reduced (sc2 (14), sc2 (12));
            r.removeFromTop (sc2 (26));                       // 見出し「ぜんたい」
            focusMeterArea = r.removeFromTop (sc2 (86));      // マイクの音 / 出ていく音
            r.removeFromTop (sc2 (14));
            const int lh = sc2 (24), sh = sc2 (32);
            r.removeFromTop (lh);                             // 「文字の大きさ  115%」
            //  ★数値は見出しの右に描いているので、スライダー自身の数値欄は消す
            //   （両方出すと「100%  100%」と二重に見える。実際そうなった）。
            fontSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            zoomSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            //  色はテーマから取り直す（既定のままだと、明るいテーマの中で
            //  ここだけ JUCE の青が残る）。
            for (auto* sl : { &fontSlider, &zoomSlider })
            {
                sl->setSliderStyle (juce::Slider::LinearHorizontal);
                sl->setColour (juce::Slider::backgroundColourId, Palette::track);
                sl->setColour (juce::Slider::trackColourId,      Palette::green);
                sl->setColour (juce::Slider::thumbColourId,      Palette::green);
            }
            fontSlider.setBounds (r.removeFromTop (sh));
            r.removeFromTop (sc2 (18));
            r.removeFromTop (lh);                             // 「画面の大きさ  100%」
            zoomSlider.setBounds (r.removeFromTop (sh));
            fontSlider.setVisible (true);
            zoomSlider.setVisible (true);
        }
        else
        {
            focusMeterArea = {};
            fontSlider.setVisible (false);
            zoomSlider.setVisible (false);
        }
        return;
    }

    focusHeadArea = focusTipArea = focusFinishArea = {};
    focusEmpty.setVisible (false);
    focusSideArea = focusMeterArea = {};
    focusCompare.setVisible (false);
    //  ふつうの画面では、文字/画面の大きさは「文字・見た目」の吹き出しの中だけ
    //  （v2.12.0 でヘッダの極小スライダーを撤去したときの決定）。器に戻す。
    fontSlider.setVisible (false);
    zoomSlider.setVisible (false);
    tuner.setVisible (true);     // 1つずつ で譲ったときのために、必ず戻す

    fxWarnArea = {};                              // v2.8.0: 案内帯はかんたんモード専用

    // v2.12.0 ★左列の高さを「余っている分だけ」に直した。
    //  それまでは チューナー160 + パネル166×3 + 隙間 = top+676 の**固定**で、
    //  top=128・H=790 のとき既に 14px はみ出していた(3段目の数値が窓の縁に
    //  張り付いていたのはこれ)。文字を大きくしてヘッダが3段になると、
    //  そのぶん丸ごと下へ押し出されて数値が切れる。
    //  高さは残りから逆算する。ツマミが少し小さくなっても、切れるよりは良い。
    const int colBottom = H - 8;
    const int availLeft = juce::jmax (360, colBottom - top);
    const int tunerH    = 240;
    const int panelH    = juce::jmax (120, (availLeft - tunerH - gap * 3) / 3);
    tuner.setBounds (leftX, y, leftW, tunerH);     // v1.8.3: チューナーを圧縮し3行を同高に
    y += tunerH + gap;

    cleanArea = { leftX, y, leftW, panelH }; y += panelH + gap;
    dynArea   = { leftX, y, leftW, panelH }; y += panelH + gap;
    toneArea  = { leftX, y, leftW, panelH };       // v1.8.3: 他行と同高=ノブ同径

    // v2.3.0: ポップ/リップを追加して7ノブに(左列を520pxへ広げて径を確保)
    // v2.6.0: ジー音(電源ハムの自動除去)を掃除セクションへ追加して8ノブ
    placeRow (cleanArea, { &gate, &lowCut, &mudK, &harshK, &denoiseK, &popK, &lipK, &humK });
    // v2.4.0: マイク音量(入力トリム)と音量キープ(Vocal Rider相当)を追加して7ノブ
    placeRow (dynArea,   { &inGainK, &proxK, &comp1K, &comp2K, &attackK, &releaseK, &deessK, &rideK });
    // v1.8.3: 音色のノブ径を他セクション(5ノブ行)と同径化。
    // placeRow は area幅をノブ数で等分するため、4ノブだと1本あたりが広く=大きくなる。
    // ダミー1枠ぶん右に余白を確保して5等分にそろえ、実ノブは左4枠に置く。
    // v2.4.0: なめらか を音色セクションの先頭に追加(6ノブ)
    // v2.6.0: ことば(子音エンハンサー)を追加して7ノブ(他セクションと同数)
    placeRow (toneArea, { &resK, &pickK, &consK, &presenceK, &airK, &warmthK, &sustainK, &ringK });

    // ---- right column: dynamic EQ (top) + OUTPUT / SPACE (bottom) ----
    const int eqX = leftX + leftW + 12;
    const int rW  = W - pad - eqX;

    // v1.8.3: 下部は背景(季節/オーロラ)が見える帯。
    // v2.12.0: 文字が大きくてヘッダが3段のときは、この飾りの帯を先に譲る
    //          (飾りより、ツマミの名前と数値が切れないことが先)。
    const int kBgStrip = (rows == 3 ? 12 : 46);
    spaceArea = { eqX, H - kBgStrip - 158, rW, 158 };
    // v2.0.0: サビリフトを仕上げ列の末尾に追加(8ノブ)
    placeRow (spaceArea, { &makeupK, &mixK, &widthK, &doublerK, &delayK, &revSizeK, &revMixK, &liftK });
    // v1.6.0: reverb-type selector lives in the section-4 header (both modes)
    // v3.0-c ★ここも固定席(getRight()-178 から 170px / 見出しは -274 から 90px)
    //  だった。文字を150%にすると見出しもコンボの中身も伸びて、左隣の
    //  「テンポフィット」に食い込む（ui_fit が検出）。実測して右から詰める。
    {
        const int hy = spaceArea.getY() + 3;
        int rx = spaceArea.getRight() - 8;
        int cbW = 170;
        if (lnf != nullptr)
        {
            const auto cf = lnf->getComboBoxFont (revTypeBox);
            int longest = 0;
            for (int i = 0; i < revTypeBox.getNumItems(); ++i)
                longest = juce::jmax (longest, juce::GlyphArrangement::getStringWidthInt (
                                                   cf, revTypeBox.getItemText (i)));
            cbW = juce::jmax (120, longest + 46);      // 矢印と余白
        }
        const int cbH = juce::jmax (22, juce::roundToInt (22.0f * juce::jmin (1.35f, fRatio)));
        revTypeBox.setBounds (rx - cbW, hy, cbW, cbH);
        rx -= cbW + 8;
        const int labW = juce::GlyphArrangement::getStringWidthInt (
            cfont (11.5f, true),
            tip::T ("\xe3\x81\xb2\xe3\x81\xb3\xe3\x81\x8d\xe3\x81\xae\xe7\xa8\xae\xe9\xa1\x9e", "Reverb type")) + 6;
        revTypeLabArea = { rx - labW, hy, labW, cbH };
        revHeaderLeft  = rx - labW - 10;              // ここより左が「テンポフィット」の取り分
    }

    seqArea = { eqX, top, rW, spaceArea.getY() - gap - top };

    // knob strip pinned to the bottom of the EQ panel
    auto knobStrip = seqArea.withTrimmedTop (seqArea.getHeight() - 118).reduced (8, 4);
    eqGraphArea   = seqArea.reduced (12).withTrimmedTop (16).withTrimmedBottom (140);
    eqGraph.setBounds (eqGraphArea);

    // manual = 6 knobs across; auto = 2 knobs centred (overlap, visibility switches)
    placeCells (knobStrip, { &seqF1K, &seqD1K, &seqF2K, &seqD2K, &seqF3K, &seqD3K });
    {
        const int autoW = 340;
        auto autoRow = knobStrip.withWidth (autoW)
                                .withX (knobStrip.getX() + (knobStrip.getWidth() - autoW) / 2);
        placeCells (autoRow, { &seqAmountK, &seqFocusK });
    }

    // ---- v1.4.0: effects-tab layout (same region as the EQ tab, visibility switches) ----
    {
        auto fx = seqArea.withTrimmedTop (26).reduced (10, 4);
        fx.removeFromBottom (20);                      // description strip (painted)
        analysisArea = fx.removeFromBottom (104);      // v1.9.0: taller band = Auto-Tune + Key
        fx.removeFromBottom (4);
        const int rowH = fx.getHeight() / 3;
        fxRow1 = fx.removeFromTop (rowH);
        fxRow2 = fx.removeFromTop (rowH);
        fxRow3 = fx;

        auto r1 = fxRow1; r1.removeFromTop (20); r1.reduce (8, 2);
        // v2.12.0 ★ここも固定幅88pxで「ボイス…」「5人ユ…」に切れていた。
        //  2列(ボイス変換/5人ユニゾン と ハモ種別/ハモだけ)の幅を実測して席を取る。
        auto bW = [lnf] (juce::TextButton& b, int fb)
            { return lnf != nullptr ? juce::jmax (fb, lnf->buttonWidthFor (b, 26, 18)) : fb; };
        //  ただし上限も要る。150% では「5人ユニゾン」だけで 248px 必要になり、
        //  素直に測ると左の4ツマミ(ダッキング/コーラス/息/エモ)の席が消える
        //  ——実際に一度そうなった。ここは**取り合い**なので上限で止め、
        //  はみ出すぶんは fitFont が文字を縮めて受け止める。
        //  (エフェクト面の本当の作り直しは v3.0 のモジュール化で行う)
        const int colA = juce::jlimit (96, 132, juce::jmax (bW (vcOnButton, 96),
                                                            bW (jnOnButton, 96)));
        const int colB = juce::jlimit (92, 124, bW (jnSoloButton, 92));
        auto vrow = r1.removeFromRight (juce::jlimit (410, r1.getWidth() * 64 / 100,
                                                      colA + colB + 12 + 300));
        // v2.0.0: 息・エモを空間セクションに追加(4ノブ)
        placeCells (r1, { &duckK, &choAmtK, &brK, &emoK });
        auto vb = vrow.removeFromLeft (colA + 8);
        vcOnButton.setBounds (vb.getX() + 4, vb.getCentreY() - 27, colA, 26);
        jnOnButton.setBounds (vb.getX() + 4, vb.getCentreY() + 1,  colA, 26);
        auto hb = vrow.removeFromLeft (colB + 4);
        jnHarmBox.setBounds (hb.getX() + 2, hb.getCentreY() - 27, colB, 26);
        jnSoloButton.setBounds (hb.getX() + 2, hb.getCentreY() + 1, colB, 26);
        placeCells (vrow, { &vcPitchK, &vcFormK, &jnMixK });

        auto r2 = fxRow2; r2.removeFromTop (20); r2.reduce (8, 2);
        auto c2 = r2.removeFromLeft (185);
        dlySyncBox.setBounds (c2.getX() + 4, c2.getCentreY() - 28, 168, 26);
        tapButton .setBounds (c2.getX() + 4, c2.getCentreY() + 4, 72, 24);
        placeCells (r2, { &bpmK, &dlyMsK, &dlyFbK, &dlyHcK });

        auto r3 = fxRow3; r3.removeFromTop (20); r3.reduce (8, 2);
        auto c3 = r3.removeFromLeft (185);
        charBox    .setBounds (c3.getX() + 4, c3.getCentreY() - 28, 168, 26);
        megaTypeBox.setBounds (c3.getX() + 4, c3.getCentreY() + 4, 168, 26);
        auto mb = r3.removeFromRight (104);            // v2.1.0/v2.2.0 設定ボタン2つ
        midiButton  .setBounds (mb.getX() + 4, mb.getCentreY() - 28, 96, 26);
        streamButton.setBounds (mb.getX() + 4, mb.getCentreY() + 2,  96, 26);
        placeCells (r3, { &megaAmtK, &roboFreqK, &roboMixK });

        // v1.9.0: the framed band now hosts AUTO-TUNE (left) and KEY DETECT (right).
        // The existing Krumhansl key scan feeds auto-tune's key/scale on completion.
        auto ab = analysisArea.reduced (10, 4);
        ab.removeFromTop (17);                        // section header row (painted)
        atColArea  = ab.removeFromLeft (ab.getWidth() * 60 / 100);
        ab.removeFromLeft (12);
        keyColArea = ab;

        {   // --- auto-tune column: row1 = ON/Key/Scale, row2 = Amount/Speed sliders ---
            auto col = atColArea;
            auto row1 = col.removeFromTop (28);
            col.removeFromTop (6);
            // v2.7.0: row2 を上下2段に分け、下段に「こぶし」を置く。
            // analysisArea は 104px あり、ヘッダ17 + row1(28) + 6 を引いても
            // 45px 残るので、22px ずつの2段がちょうど収まる。
            auto row2 = col.removeFromTop (22);
            col.removeFromTop (1);
            auto row3 = col.removeFromTop (22);
            // v2.12.0: 96px 固定だと「ピッチ補正」が「ピッチ…」に切れていた
            //  （ランプ20px + 余白8px を引くと 68px しか残らない）。実測に切替。
            atOnButton.setBounds (row1.removeFromLeft (
                lnf != nullptr ? juce::jmax (96, lnf->buttonWidthFor (atOnButton, 24, 14)) : 116)
                    .reduced (1, 2));
            row1.removeFromLeft (6);
            atKeyBox  .setBounds (row1.removeFromLeft (56).reduced (0, 1));
            row1.removeFromLeft (6);
            atScaleBox.setBounds (row1.reduced (0, 1));
            auto amtCell = row2.removeFromLeft (row2.getWidth() / 2);
            auto spdCell = row2;
            // v1.9.1: 英語だと "Amount"/"Speed" が 40px で見切れていた
            const int labW = tip::english ? 58 : 40;
            atLabelAmt = amtCell.removeFromLeft (labW);
            atAmountSlider.setBounds (amtCell.reduced (2, 1));
            atLabelSpd = spdCell.removeFromLeft (labW);
            atSpeedSlider .setBounds (spdCell.reduced (2, 1));
            // 下段: 「こぶし」スライダー(左半分) + いま守っている表示(右半分)
            auto ornCell = row3.removeFromLeft (row3.getWidth() / 2);
            atLabelOrn = ornCell.removeFromLeft (labW);
            ornSlider.setBounds (ornCell.reduced (2, 1));
            ornStatusArea = row3;
        }

        {   // --- key-detect column: two buttons on top, result read-out painted below ---
            auto col = keyColArea;
            auto kR1 = col.removeFromTop (28);
            keyScaleButton.setBounds (kR1.removeFromLeft (juce::jmin (150, kR1.getWidth() * 52 / 100)));
            kR1.removeFromLeft (6);
            analyzeButton .setBounds (kR1);
            // v2.9.0: ここにあった「低遅延」ボタンは廃止。セッションはヒーロー帯へ。
        }
    }

    // LEARN button: top-right inside the CLEAN UP panel header
    learnButton.setBounds (cleanArea.getRight() - 84, cleanArea.getY() + 3, 78, 19);
    // v3.0 じどう学びなおしは「学習ずみ(もどす)」のさらに左。幅は実測。
    //  席順: [じどう学習] [学習ずみ] [LEARN]（ui_fit が重なりを毎回見張る）
    {
        const int rw = lnf != nullptr ? juce::jmax (72, lnf->buttonWidthFor (relearnBtn, 19, 14)) : 92;
        relearnBtn.setVisible (true);
        relearnBtn.setBounds (cleanArea.getRight() - 84 - 6 - 72 - 6 - rw, cleanArea.getY() + 3, rw, 19);
    }
    // v3.0「つぶさない」: 音量そろえの見出し右。幅は実測（文字が切れないように）
    {
        const int h = juce::jmax (20, juce::roundToInt (20.0f * juce::jmin (1.35f, fRatio)));
        const int w = lnf != nullptr ? juce::jmax (110, lnf->buttonWidthFor (crushBtn, h, 18)) : 130;
        crushBtn.setVisible (true);
        crushBtn.setBounds (dynArea.getRight() - 8 - w, dynArea.getY() + 3, w, h);
        crushReadArea = { dynArea.getRight() - 8 - w - 76, dynArea.getY() + 3, 72, h };
    }
    // v2.10.0 その左に「学習ずみ」。覚えている間だけ出る(timerCallback が制御)。
    dnClearButton.setBounds (cleanArea.getRight() - 84 - 6 - 72, cleanArea.getY() + 3, 72, 19);
    // (v1.5.0: auto-setup buttons moved to the hero band above the columns)
    // TEMPO FIT: v2.8.0 ★位置を直した。
    // 右端に置いていたが、同じ場所に「ひびきの種類」プルダウン
    // (getRight()-178 から 170px) があり、110px のうち 108px が下敷きに
    // なっていた。プルダウンのほうが手前なので、押すとプルダウンが開く＝
    // **このボタンは事実上押せなかった**。
    // 右側はプルダウンとその見出し(getRight()-274 から 90px)で埋まっているので、
    // パネル見出し「4 ひろがり・仕上げ」の右隣、左寄りの空き地へ移す。
    // v2.12.0: この行の固定幅(110/56/100/72)も実測に置き換え。「EQ OFF」が
    //  72px に対して 86px 必要で、150% で「EQ O…」になっていた。
    const int tabH = juce::jmax (20, juce::roundToInt (20.0f * juce::jmin (1.35f, fRatio)));
    auto fitW = [lnf] (juce::TextButton& b, int h, int fb, int padPx = 16)
        { return lnf != nullptr ? juce::jmax (fb, lnf->buttonWidthFor (b, h, padPx)) : fb; };

    // v2.12.0 ★x+168 の固定席をやめた。パネル見出し「4 ひろがり・仕上げ」は
    //  cfont(13.5) ＝ 文字サイズに追随するので、150% では 168px の席まで
    //  伸びてボタンと重なり「仕上ゲンポフィット」と読めていた。
    //  見出しの実幅を測って、その右隣に置く。
    {

        const int powerW = fitW (reverbPower, tabH, 156);
        reverbPower.setBounds (spaceArea.getX() + 12, spaceArea.getY() + 3, powerW, tabH);
        const int tfX = reverbPower.getRight() + 14;
        const int tfW = fitW (tempoFitButton, tabH, 110);
        // 入り切らないときは、まず**見出し「ひびきの種類」を譲る**。
        //  コンボには中身（ノーマル/ルーム/…）が出ていて、触れば説明も出るので、
        //  見出しが無くても意味は取れる。押せないボタンを作るよりは良い。
        if (tfX + tfW > revHeaderLeft)
        {
            revHeaderLeft += revTypeLabArea.getWidth() + 10;
            revTypeLabArea = {};
        }
        tempoFitButton.setBounds (tfX, spaceArea.getY() + 3, tfW, tabH);
        tempoFitButton.setVisible (tfX + tfW <= revHeaderLeft);
    }
    // right-column tabs: left side of the panel header
    {
        int tx = seqArea.getX() + 10;
        const int we = fitW (tabEqButton, tabH, 56);
        tabEqButton.setBounds (tx, seqArea.getY() + 3, we, tabH); tx += we + 8;
        tabFxButton.setBounds (tx, seqArea.getY() + 3, fitW (tabFxButton, tabH, 100), tabH);
    }
    // Smart EQ mode + on/off: top-right inside the panel header (EQ tab only)
    {
        // 「EQ ON」と「EQ OFF」で幅が変わるので、広いほう(OFF)で席を取る
        //  ——押すたびに箱の幅が動くのは、それ自体が落ち着かない。
        const int wOn = fitW (seqOnButton, tabH, 72) + 10;
        seqOnButton.setBounds (seqArea.getRight() - 6 - wOn, seqArea.getY() + 3, wOn, tabH);
        seqModeBox .setBounds (seqOnButton.getX() - 6 - 84,  seqArea.getY() + 2, 84, tabH + 1);
    }

    // red lamps at knob centres (after every knob has its bounds)
    placeLamp (lampGate, gate);
    placeLamp (lampDn,   denoiseK);
    placeLamp (lampDs,   deessK);
    placeLamp (lampDbl,  doublerK);
    placeLamp (lampDly,  delayK);
    placeLamp (lampRev,  revMixK);
    placeLamp (lampMega, megaAmtK);
    placeLamp (lampCho,  choAmtK);
    placeLamp (lampRobo, roboMixK);
}

//==============================================================================
// Editor: owns LookAndFeel and scales the whole GUI proportionally.
// The content lives at a fixed base size in its own coordinate space; we apply
// an affine scale so knobs, panels, AND text all grow together by ratio.
//==============================================================================
//==============================================================================
// v1.7.0 theme cross-switch (header). A compact 5-cell "plus": centre = neutral,
// left = yuru-kawa; right/up/down are reserved for future themes (they fall back
// to neutral for now). Clicking a cell recolours the whole UI palette at once.
juce::Rectangle<int> VocalGzzioContent::crossHit (int idx) const
{
    // v2.4.0: ＊型グリッド + 中央N をやめ、上段=テーマ6枚のチップ / 下段=大きな
    //         「かんたんモード」スイッチ、という素直な2段にした。
    // v2.12.0 ★上段のテーマチップ6枚をここから外した。
    //   理由は resized() に書いたとおり2つ。(1) ヘッダ1段目に 368px の帯を
    //   右から重ねていたため「Band」が下敷きになって消えていた。
    //   (2) チップの文字は 11.5px 固定で、文字サイズを上げても大きくならず、
    //       いちばん読みにくい文字がいちばん良い席に座っていた。
    //   テーマは「文字・見た目」の吹き出しへ移し、大きな字で選べるようにした。
    //   ここに残るのは 6 = かんたんモードのスイッチだけ。
    if (idx == 6) return crossArea;
    return {};        // 0..5 は席を持たない(空矩形は contains() が必ず false)
}

// v2.12.0: 旧 v1.7.0 の「＊型アイコン版テーマスイッチ」(drawCrossSwitch) を削除した。
//  v2.4.0 でチップ版に置き換わってから一度も呼ばれておらず、しかも中の色表に
//  「2 軽量」「4 ゆるふわ軽量」という**もう存在しないモード**が残っていた。
//  読む人を惑わせるだけなので、実装ごと消す（履歴は git にある）。


void VocalGzzioContent::applyThemeToKnobs()
{
    // Push the per-theme arc colour into the shared LookAndFeel (owned by the
    // editor). Runs AFTER Palette::applyTheme, so Palette::ice already holds the
    // correct per-theme mint.
    if (auto* l = dynamic_cast<GzzioLnF*> (&getLookAndFeel()))
    {
        l->setThemeArc (Palette::ice);
        l->setUseKawaiiFont (pastelTheme());   // rounded font in the two yuru modes
    }

    // Label / Slider text colours are assigned once at construction, so after a
    // theme switch they keep the OLD theme's ink and vanish (e.g. light text on
    // the light yuru-kawa cards). Refresh every child's text colours here.
    for (auto* ch : getChildren())
    {
        if (auto* lbl = dynamic_cast<juce::Label*> (ch))
            lbl->setColour (juce::Label::textColourId, Palette::ink);
        if (auto* sl = dynamic_cast<juce::Slider*> (ch))
        {
            sl->setColour (juce::Slider::textBoxTextColourId,       Palette::ink);
            sl->setColour (juce::Slider::textBoxOutlineColourId,    Palette::panelLn);
            sl->setColour (juce::Slider::textBoxBackgroundColourId, Palette::track.withAlpha (0.35f));
        }
        ch->repaint();
    }
}

void VocalGzzioContent::forceSeason (int s)
{
    // v1.8.3: 季節は15秒周期。各季の中央(遷移と重ならない位置)へ合わせる
    themePainter.animT = 15.0f * (float) juce::jlimit (0, 3, s) + 6.0f;
    themePainter.freezeTime = true;          // 検証: この季節で固定
    repaint();
}

// v1.8.0: re-apply every user-facing string in the current language (tip::english).
// Painted strings switch automatically; widgets that cached text are re-set here.
void VocalGzzioContent::refreshLanguage()
{
    const int sourceId = srcModeBox.getSelectedId();
    srcModeBox.clear (juce::dontSendNotification);
    srcModeBox.addItem (tip::src_uta(), 1);
    srcModeBox.addItem (tip::src_gita(), 2);
    srcModeBox.addItem (tip::src_shaberi(), 3);
    srcModeBox.addItem (tip::src_hikigatari(), 4);
    srcModeBox.setSelectedId (sourceId, juce::dontSendNotification);
    srcModeBox.setTooltip (tip::src_tip());
    learnButton.setButtonText (tip::T ("ノイズを測る", "Learn noise"));
    learnButton.setTooltip (tip::learn_tip() + "\n" + tip::note_learn());
    relearnBtn.setButtonText (tip::dn_relearn_label());
    relearnBtn.setTooltip (tip::dn_relearn_tip());
    // v2.8.0: 英語UIへ切り替えたときに案内帯も訳す
    fxWarnButton.setButtonText (tip::fxwarn_label());
    fxWarnButton.setTooltip (tip::fxwarn_tip());
    // v2.12.0: ここが抜けていて、ブランド(英語)で「Text・見た目」と混ざっていた
    sizePopBtn.setButtonText (tip::look_label());
    modPopBtn.setButtonText (tip::mods_label());
    pathRail.refreshText();       // v3.0-c 常設列（見出し・8枚の名前とひとこと）

    gate.label.setText (tip::T ("\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\x88", "GATE"), juce::dontSendNotification);
    gate.slider.setTooltip (tip::gate_tip());
    lowCut.label.setText (tip::T ("\xe3\x83\xad\xe3\x83\xbc\xe3\x82\xab\xe3\x83\x83\xe3\x83\x88", "LOW CUT"), juce::dontSendNotification);
    lowCut.slider.setTooltip (tip::lowcut());
    mudK.label.setText (tip::T ("\xe3\x81\x93\xe3\x82\x82\xe3\x82\x8a", "MUD"), juce::dontSendNotification);
    mudK.slider.setTooltip (tip::mud());
    harshK.label.setText (tip::T ("\xe3\x82\xad\xe3\x83\xb3\xe3\x82\xad\xe3\x83\xb3", "HARSH"), juce::dontSendNotification);
    harshK.slider.setTooltip (tip::harsh());
    denoiseK.label.setText (tip::T ("\xe3\x83\x8e\xe3\x82\xa4\xe3\x82\xba\xe9\x99\xa4\xe5\x8e\xbb", "DENOISE"), juce::dontSendNotification);
    denoiseK.slider.setTooltip (tip::denoise_tip());
    comp1K.label.setText (tip::T ("\xe3\x83\x94\xe3\x83\xbc\xe3\x82\xaf\xe5\x9c\xa7\xe7\xb8\xae", "PEAK"), juce::dontSendNotification);
    comp1K.slider.setTooltip (tip::comp1_tip());
    comp2K.label.setText (tip::T ("\xe3\x81\xaa\xe3\x82\x89\xe3\x81\x97\xe5\x9c\xa7\xe7\xb8\xae", "LEVELER"), juce::dontSendNotification);
    comp2K.slider.setTooltip (tip::comp2_tip());
    attackK.label.setText (tip::T ("\xe3\x82\xa2\xe3\x82\xbf\xe3\x83\x83\xe3\x82\xaf", "ATTACK"), juce::dontSendNotification);
    attackK.slider.setTooltip (tip::attack());
    releaseK.label.setText (tip::T ("\xe3\x83\xaa\xe3\x83\xaa\xe3\x83\xbc\xe3\x82\xb9", "RELEASE"), juce::dontSendNotification);
    releaseK.slider.setTooltip (tip::release());
    deessK.label.setText (tip::T ("\xe3\x82\xb5\xe8\xa1\x8c\xe3\x81\x8a\xe3\x81\x95\xe3\x81\x88", "DE-ESS"), juce::dontSendNotification);
    deessK.slider.setTooltip (tip::deess());
    presenceK.label.setText (tip::T ("\xe3\x83\x8c\xe3\x82\xb1\xe6\x84\x9f", "PRESENCE"), juce::dontSendNotification);
    presenceK.slider.setTooltip (tip::presence());
    airK.label.setText (tip::T ("\xe3\x82\xad\xe3\x83\xa9\xe3\x82\xad\xe3\x83\xa9", "AIR"), juce::dontSendNotification);
    airK.slider.setTooltip (tip::air());
    ringK.label.setText (tip::ring_label(), juce::dontSendNotification);
    ringK.slider.setTooltip (tip::ring_tip());
    warmthK.label.setText (tip::T ("\xe3\x81\x82\xe3\x81\x9f\xe3\x81\x9f\xe3\x81\x8b\xe3\x81\xbf", "WARMTH"), juce::dontSendNotification);
    warmthK.slider.setTooltip (tip::warmth());
    sustainK.label.setText (tip::T ("\xe3\x81\xae\xe3\x81\xb3", "SUSTAIN"), juce::dontSendNotification);
    sustainK.slider.setTooltip (tip::sustain_tip());
    makeupK.label.setText (tip::T ("\xe4\xbb\x95\xe4\xb8\x8a\xe3\x81\x92\xe9\x9f\xb3\xe9\x87\x8f", "MAKEUP"), juce::dontSendNotification);
    makeupK.slider.setTooltip (tip::makeup());
    mixK.label.setText (tip::T ("\xe3\x82\xa8\xe3\x83\x95\xe3\x82\xa7\xe3\x82\xaf\xe3\x83\x88\xe9\x87\x8f", "FX MIX"), juce::dontSendNotification);
    mixK.slider.setTooltip (tip::mix());
    widthK.label.setText (tip::T ("\xe3\x81\xb2\xe3\x82\x8d\xe3\x81\x8c\xe3\x82\x8a", "WIDTH"), juce::dontSendNotification);
    widthK.slider.setTooltip (tip::width());
    doublerK.label.setText (tip::T ("\xe3\x81\x8b\xe3\x81\x95\xe3\x81\xad", "DOUBLER"), juce::dontSendNotification);
    doublerK.slider.setTooltip (tip::doubler());
    delayK.label.setText (tip::T ("\xe3\x82\x84\xe3\x81\xbe\xe3\x81\xb3\xe3\x81\x93", "ECHO"), juce::dontSendNotification);
    delayK.slider.setTooltip (tip::delay_tip());
    revSizeK.label.setText (tip::T ("\xe9\x83\xa8\xe5\xb1\x8b\xe3\x81\xae\xe5\xba\x83\xe3\x81\x95", "ROOM SIZE"), juce::dontSendNotification);
    revSizeK.slider.setTooltip (tip::revsize());
    revMixK.label.setText (tip::T ("\xe3\x81\xb2\xe3\x81\xb3\xe3\x81\x8d", "REVERB"), juce::dontSendNotification);
    revMixK.slider.setTooltip (tip::revmix());
    seqAmountK.label.setText (tip::seq_amount_label(), juce::dontSendNotification);
    seqAmountK.slider.setTooltip (tip::seq_amount_tip());
    seqFocusK.label.setText (tip::seq_focus_label(), juce::dontSendNotification);
    seqFocusK.slider.setTooltip (tip::seq_focus_tip());
    seqF1K.label.setText (tip::seq_freq_label(), juce::dontSendNotification);
    seqF1K.slider.setTooltip (tip::seq_freq_tip());
    seqD1K.label.setText (tip::seq_depth_label(), juce::dontSendNotification);
    seqD1K.slider.setTooltip (tip::seq_depth_tip());
    seqF2K.label.setText (tip::seq_freq_label(), juce::dontSendNotification);
    seqF2K.slider.setTooltip (tip::seq_freq_tip());
    seqD2K.label.setText (tip::seq_depth_label(), juce::dontSendNotification);
    seqD2K.slider.setTooltip (tip::seq_depth_tip());
    seqF3K.label.setText (tip::seq_freq_label(), juce::dontSendNotification);
    seqF3K.slider.setTooltip (tip::seq_freq_tip());
    seqD3K.label.setText (tip::seq_depth_label(), juce::dontSendNotification);
    seqD3K.slider.setTooltip (tip::seq_depth_tip());
    duckK.label.setText (tip::duck_label(), juce::dontSendNotification);
    duckK.slider.setTooltip (tip::duck_tip());
    choAmtK.label.setText (tip::cho_label(), juce::dontSendNotification);
    choAmtK.slider.setTooltip (tip::cho_tip());
    dlyFbK.label.setText (tip::dly_fb_label(), juce::dontSendNotification);
    dlyFbK.slider.setTooltip (tip::dly_fb_tip());
    dlyHcK.label.setText (tip::dly_hc_label(), juce::dontSendNotification);
    dlyHcK.slider.setTooltip (tip::dly_hc_tip());
    megaAmtK.label.setText (tip::mega_amt_label(), juce::dontSendNotification);
    megaAmtK.slider.setTooltip (tip::mega_amt_tip());
    roboFreqK.label.setText (tip::robo_freq_label(), juce::dontSendNotification);
    roboFreqK.slider.setTooltip (tip::robo_freq_tip());
    roboMixK.label.setText (tip::robo_mix_label(), juce::dontSendNotification);
    roboMixK.slider.setTooltip (tip::robo_mix_tip());
    vcPitchK.label.setText (tip::vc_pitch_label(), juce::dontSendNotification);
    vcPitchK.slider.setTooltip (tip::vc_pitch_tip());
    vcFormK.label.setText (tip::vc_form_label(), juce::dontSendNotification);
    vcFormK.slider.setTooltip (tip::vc_form_tip());
    jnMixK.label.setText (tip::jn_mix_label(), juce::dontSendNotification);
    jnMixK.slider.setTooltip (tip::jn_mix_tip());
    bpmK.slider.setTooltip (tip::bpm_tip());
    dlyMsK.slider.setTooltip (tip::dly_ms_tip());

    sceneSolo.setButtonText (tip::scene_solo());
    sceneTalk.setButtonText (tip::scene_talk());
    sceneBand.setButtonText (tip::T ("バンド", "Band"));
    saveButton.setButtonText (tip::T ("保存", "Save"));
    loadButton.setButtonText (tip::T ("読込", "Load"));
    resetButton.setButtonText (tip::T ("初期化", "Reset"));
    autoSetupButton.setButtonText (tip::autoset_label());
    songSetupButton.setButtonText (advancedMode ? tip::autoset_sing_label() : tip::easy_sing_label());
    tempoFitButton .setButtonText (tip::tempofit_label());
    keyScaleButton .setButtonText (tip::keyscale_label());
    analyzeButton  .setButtonText (tip::chord_label());
    tabFxButton    .setButtonText (tip::fx_tab_fx());
    vcOnButton.setButtonText (tip::vc_on_label());
    jnOnButton.setButtonText (tip::jn_on_label());

    voiceBox   .setTextWhenNothingSelected (tip::voice_placeholder());
    micBox     .setTextWhenNothingSelected (tip::mic_placeholder());
    fillMicBox (micBox);                     // v1.9.0: generic mic names are bilingual
    eqPresetBox.setTextWhenNothingSelected (tip::eqpreset_placeholder());
    eqPresetBox.setTooltip (tip::eqpreset_tip() + "\n" + tip::note_override());
    tuner.refreshLanguage();                 // v1.9.1: おんぷレール等

    // v1.9.0: ブランドモード(英語UI)で日本語のまま残っていたプルダウンを作り直す
    {
        const int vKeep = voiceBox.getSelectedId();
        voiceBox.clear (juce::dontSendNotification);
        voiceBox.addSectionHeading (tip::female_head());
        for (int i = 0;  i < 5;  ++i) voiceBox.addItem (voiceItemName (i), i + 1);
        for (int i = 10; i < 15; ++i) voiceBox.addItem (voiceItemName (i), i + 1);
        voiceBox.addSectionHeading (tip::male_head());
        for (int i = 5;  i < 10; ++i) voiceBox.addItem (voiceItemName (i), i + 1);
        for (int i = 15; i < 20; ++i) voiceBox.addItem (voiceItemName (i), i + 1);
        if (vKeep > 0) voiceBox.setSelectedId (vKeep, juce::dontSendNotification);

        const int eKeep = eqPresetBox.getSelectedId();
        eqPresetBox.clear (juce::dontSendNotification);
        for (int i = 0; i < gzzio::kNumEqPresets; ++i) eqPresetBox.addItem (eqItemName (i), i + 1);
        if (eKeep > 0) eqPresetBox.setSelectedId (eKeep, juce::dontSendNotification);

        const int cKeep = charBox.getSelectedId();
        charBox.clear (juce::dontSendNotification);
        for (int i = 0; i < gzzio::kNumCharPresets; ++i) charBox.addItem (charItemName (i), i + 1);
        if (cKeep > 0) charBox.setSelectedId (cKeep, juce::dontSendNotification);

        fillComboFromChoiceParam (revTypeBox,  "rev_type");
        fillComboFromChoiceParam (dlySyncBox,  "dly_sync");
        fillComboFromChoiceParam (megaTypeBox, "mega_type");
    }
    charBox    .setTextWhenNothingSelected (tip::char_placeholder());
    {
        const int hkeep = jnHarmBox.getSelectedId();
        jnHarmBox.clear (juce::dontSendNotification);
        for (int hi = 0; hi < 9; ++hi)                     // v2.0.0: 全9モード
            jnHarmBox.addItem (tip::jn_harm_item (hi), hi + 1);
        jnHarmBox.setSelectedId (hkeep > 0 ? hkeep : 1, juce::dontSendNotification);
        jnHarmBox.setTooltip (tip::jn_harm_tip());
    }

    popK.label.setText (tip::pop_label(), juce::dontSendNotification);   // v2.3.0
    popK.slider.setTooltip (tip::pop_tip());
    lipK.label.setText (tip::lip_label(), juce::dontSendNotification);
    lipK.slider.setTooltip (tip::lip_tip());
    resK.label.setText (tip::res_label(), juce::dontSendNotification);   // v2.4.0
    resK.slider.setTooltip (tip::res_tip());
    inGainK.label.setText (tip::mic_label(), juce::dontSendNotification);
    inGainK.slider.setTooltip (tip::mic_tip());
    rideK.label.setText (tip::ride_label(), juce::dontSendNotification);
    rideK.slider.setTooltip (tip::ride_tip());
    humK.label.setText (tip::hum_label(), juce::dontSendNotification);   // v2.6.0
    humK.slider.setTooltip (tip::hum_tip());
    humShownHz = -1;                                    // 言語が変わったら出し直す
    consK.label.setText (tip::cons_label(), juce::dontSendNotification);
    consK.slider.setTooltip (tip::cons_tip());

    // v2.1.0/v2.2.0 設定ボタンも言語に追随
    midiButton.setButtonText (tip::midi_btn_label());
    midiButton.setTooltip (tip::midi_btn_tip());
    streamButton.setButtonText (tip::so_btn_label());
    streamButton.setTooltip (tip::so_btn_tip());

    // v2.0.0 エモート3ノブもテーマ言語に追随
    brK  .label.setText (tip::br_label(),   juce::dontSendNotification);
    brK  .slider.setTooltip (tip::br_tip());
    emoK .label.setText (tip::emo_label(),  juce::dontSendNotification);
    emoK .slider.setTooltip (tip::emo_tip());
    liftK.label.setText (tip::lift_label(), juce::dontSendNotification);
    liftK.slider.setTooltip (tip::lift_tip());
    jnSoloButton.setButtonText (tip::jnsolo_label());
    jnSoloButton.setTooltip (tip::jnsolo_tip());

    // v1.9.0 auto-tune: button text + scale combo re-labelled per language
    atOnButton.setButtonText (tip::at_on_label());
    atOnButton.setTooltip (tip::at_on_tip());
    {
        const int skeep = atScaleBox.getSelectedId();
        atScaleBox.clear (juce::dontSendNotification);
        for (int i = 0; i < 9; ++i) atScaleBox.addItem (scaleItemName (i), i + 1);
        atScaleBox.setSelectedId (skeep > 0 ? skeep : 2, juce::dontSendNotification);
        atScaleBox.setTooltip (tip::at_on_tip());
    }
    atAmountSlider.setTooltip (tip::at_amount_tip());
    atSpeedSlider .setTooltip (tip::at_speed_tip());
    ornSlider     .setTooltip (tip::orn_tip());        // v2.7.0

    resized();
    repaint();
}

void VocalGzzioContent::setThemeMode (int m)
{
    // v2.4.0: オーロラ(4)はテーマ列の席をかんたんモードのスイッチに譲って廃止。
    //         旧セーブデータの 4 / 6 はゆるふわ(1)へ寄せる(黒画面にしないため)。
    if (m == 6 || m == 4) m = 1;
    // v2.12.0: 軽量(5)を廃止。もともと配色は 0 を流用していたので、
    //          軽量を選んでいた人は**見た目そのままで**ふつう(0)に着地する。
    //          変わるのは「EQスペクトラムが出るようになる」の1点だけ。
    if (m == 5) m = 0;
    themeMode = juce::jlimit (0, 8, m);          // v2.11.0: 8 = ゆるふわ・よる
    if (themeMode == 1 || themeMode == 8) yuruNight = (themeMode == 8);
    if (themeMode != 0) lastTheme = themeMode;   // 中央Nトグルの復帰先

    Palette::applyTheme (themeMode);             // 7=色覚CUD もそのまま 1:1
    // v1.9.0: ポップアップ/ツールチップ/ボタンの色をテーマに追随させる
    if (auto* g = dynamic_cast<GzzioLnF*> (&getLookAndFeel())) g->refreshPaletteColours();

    // ブランド(3)で全UI英語化
    const bool wantEnglish = themeMode == 3;
    if (tip::english != wantEnglish) { tip::english = wantEnglish; refreshLanguage(); }

    themePainter.setMode (themeMode, getLocalBounds());
    applyThemeToKnobs();
    // v3.0-c 常設列の色もここで取り直す（スクロールバーも地の色に合わせる）
    pathRail.refreshColours();
    railView.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId,
                                               Palette::inkSoft.withAlpha (0.55f));
    railView.getVerticalScrollBar().setColour (juce::ScrollBar::trackColourId,
                                               Palette::panel.withAlpha (0.0f));
    // v2.8.0: ここに advancedMode の条件が抜けていた。そのため
    //  ・かんたんモードでテーマを押すと EQ グラフがツマミの上に出てくる
    //  ・初期状態では 0x0 の大きさで「表示中」になり、誰にも見えないまま
    //    30Hz で 4096点FFTが回り続ける（無駄なCPU）
    // という2つが起きていた。タブ表示の判定と同じ式にそろえる。
    eqGraph.setVisible (isOverview() || (advancedMode && currentTab != 1));
    refreshOverviewText();
    if (isOverview()) updateOverviewVisibility();
    if (onUiStateChange) onUiStateChange ("ui_theme", themeMode);
    repaint();
}

void VocalGzzioContent::drawJuiceServer (juce::Graphics& g)
{
    if (! pastelTheme()) return;

    const float w = 64.0f, h = 94.0f;
    const float x = (float) getWidth()  - w - 12.0f;
    const float y = (float) getHeight() - h - 8.0f;

    // v2.11.0 よる: 夜の店先みたいに、サーバーだけがぼんやり光っている絵にする。
    // （中身のジュースは昼と同じ色。暗い下地なので、そのままで自然に光って見える）
    if (nightTheme())
    {
        const float cx = x + w * 0.5f, cy = y + h * 0.5f, hr = h * 1.15f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x26ffcf8a), cx, cy,
                                                 juce::Colour (0x00ffcf8a), cx + hr, cy, true));
        g.fillEllipse (cx - hr, cy - hr, hr * 2.0f, hr * 2.0f);
    }

    // stand / legs
    g.setColour (juce::Colour (nightTheme() ? 0xff6b5f52 : 0xffd7c4ad));
    g.fillRoundedRectangle (x + w*0.14f, y + h*0.86f, w*0.72f, h*0.10f, 3.0f);
    g.fillRect (x + w*0.24f, y + h*0.92f, w*0.06f, h*0.08f);
    g.fillRect (x + w*0.70f, y + h*0.92f, w*0.06f, h*0.08f);

    // glass tank
    juce::Rectangle<float> tank (x + w*0.12f, y + h*0.18f, w*0.76f, h*0.66f);
    g.setColour (juce::Colour (0x30ffffff));
    g.fillRoundedRectangle (tank, 7.0f);

    // juice (mixed-fruit gradient), ~72% full, with a few bubbles
    {
        juce::Path clip; clip.addRoundedRectangle (tank.reduced (2.0f), 5.0f);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (clip);
        const float top = tank.getY() + tank.getHeight() * 0.28f;
        juce::ColourGradient jg (juce::Colour (0xffffb14a), 0, top,
                                 juce::Colour (0xfff06aa0), 0, tank.getBottom(), false);
        jg.addColour (0.5, juce::Colour (0xfff5866a));
        g.setGradientFill (jg);
        g.fillRect (tank.withTop (top));
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.fillEllipse (tank.getX(), top - 3.5f, tank.getWidth(), 7.0f);
        g.setColour (juce::Colours::white.withAlpha (0.30f));
        for (int b = 0; b < 4; ++b)
            g.fillEllipse (tank.getX() + tank.getWidth() * (0.24f + 0.18f * b),
                           tank.getBottom() - tank.getHeight() * (0.18f + 0.13f * b), 3.0f, 3.0f);
    }
    g.setColour (juce::Colour (nightTheme() ? 0xff7d8790 : 0xffc2d0d8));
    g.drawRoundedRectangle (tank, 7.0f, 1.5f);

    // lid + knob
    g.setColour (juce::Colour (0xffef7896));
    g.fillRoundedRectangle (x + w*0.05f, y + h*0.07f, w*0.90f, h*0.13f, 5.0f);
    g.setColour (juce::Colour (0xffd94f6e));
    g.fillEllipse (x + w*0.44f, y + h*0.005f, w*0.12f, h*0.07f);

    // spigot / tap
    g.setColour (juce::Colour (0xffb6bec6));
    g.fillRoundedRectangle (x + w*0.40f, y + h*0.78f, w*0.20f, h*0.09f, 2.0f);
    g.fillRect (x + w*0.47f, y + h*0.84f, w*0.07f, h*0.07f);

    // cherry on top
    g.setColour (juce::Colour (0xffe23c50));
    g.fillEllipse (x + w*0.28f, y + h*0.00f, w*0.13f, w*0.13f);
    g.setColour (juce::Colour (0xff7a9a4a));
    g.drawLine (x + w*0.345f, y + h*0.02f, x + w*0.28f, y - h*0.055f, 1.5f);

    // two straws hinting the glasses are "served" from here
    g.setColour (juce::Colour (0xffef7896).withAlpha (0.60f));
    g.drawLine (x + w*0.16f, y + h*0.42f, x - w*0.55f, y + h*0.10f, 3.0f);
    g.drawLine (x + w*0.16f, y + h*0.56f, x - w*0.75f, y + h*0.50f, 3.0f);
}

// v2.12.0 テーマの名前。ヘッダのチップから「文字・見た目」の吹き出しへ引っ越した
// ので、描画側と選択パネルの両方から呼べる形にしてある(名前が2か所にあると必ず
// ずれる)。ゆるふわの席は ひる/よる の2状態で、いま出ている名前が現在の状態。
juce::String VocalGzzioContent::themeArmName (int i) const
{
    switch (i)
    {
        case 0:  return tip::theme_plain();
        case 1:  return themeMode == 8 ? tip::th_yoru()
                     : tip::T ("\xe3\x82\x86\xe3\x82\x8b\xe3\x81\xb5\xe3\x82\x8f", "Yuru");
        case 2:  return tip::T ("\xe8\x87\xaa\xe7\x84\xb6", "Nature");
        case 3:  return tip::T ("\xe3\x83\x96\xe3\x83\xa9\xe3\x83\xb3\xe3\x83\x89", "Brand");
        default: return tip::T ("\xe8\x89\xb2\xe8\xa6\x9a", "CUD");   // 4
    }
}

bool VocalGzzioContent::themeArmSelected (int i) const
{
    // ゆるふわの席は ひる(1) と よる(8) のどちらでも「選択中」
    return (themeMode == kThemeOfChip[i]) || (i == 1 && themeMode == 8);
}

void VocalGzzioContent::chooseTheme (int i)
{
    // v2.11.0: ゆるふわの席だけ2状態。ゆるふわを表示中にもう一度押すと
    //          ひる <-> よる が入れ替わる。ほかのテーマから戻ってきたときは、
    //          前に選んでいたほう(yuruNight)で戻す。
    if (i == 1)
    {
        const bool inYuru = (themeMode == 1 || themeMode == 8);
        if (inYuru) yuruNight = ! yuruNight;
        setThemeMode (yuruNight ? 8 : 1);
    }
    else if (i >= 0 && i < kThemeCount) setThemeMode (kThemeOfChip[i]);
}

// ヘッダに残った「かんたんモード」スイッチ(テーマチップは吹き出しへ引っ越し済み)
void VocalGzzioContent::drawThemeCross (juce::Graphics& g)
{
    if (crossArea.isEmpty()) return;

    // v2.8.0 ★ベールの範囲を絞った。crossArea を丸ごと塗ると、隣の文字にまで
    // 55% の膜がかかって読みにくくなる(paintOverChildren なので隠しようがない)。
    // 中身はスイッチ1つだけなので、そこだけ塗る。
    g.setColour (Palette::panel.withAlpha (0.55f));
    g.fillRoundedRectangle (crossHit (6).toFloat().expanded (4.0f, 3.0f), 10.0f);

    // ---- 下段: かんたんモードのスイッチ ----
    // 以前は「かんたん/こだわり」と名前が入れ替わるボタンで、いま何モードなのか
    // 押すまで分からなかった。ON/OFF が形で見える普通のスイッチにする。
    {
        const auto  r    = crossHit (6).toFloat();
        const bool  on   = ! advancedMode;
        const bool  hov  = (crossHover == 6);
        const auto  onCol = Palette::green;

        g.setColour (on ? onCol.withAlpha (hov ? 0.42f : 0.30f)
                        : Palette::panel2.withAlpha (hov ? 1.0f : 0.85f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (on ? onCol.darker (0.25f) : Palette::panelLn);
        g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, hov ? 1.8f : 1.2f);

        // 文字は左、トグルの溝は右
        const float trackW = 44.0f, trackH = r.getHeight() - 12.0f;
        juce::Rectangle<float> track (r.getRight() - trackW - 7.0f,
                                      r.getCentreY() - trackH * 0.5f, trackW, trackH);
        g.setColour (on ? onCol.darker (0.15f) : Palette::inkSoft.withAlpha (0.35f));
        g.fillRoundedRectangle (track, trackH * 0.5f);
        const float kd = trackH - 4.0f;
        g.setColour (juce::Colours::white.withAlpha (0.96f));
        g.fillEllipse (on ? track.getRight() - kd - 2.0f : track.getX() + 2.0f,
                       track.getCentreY() - kd * 0.5f, kd, kd);

        g.setColour (Palette::accentOn (Palette::ink, Palette::panel));
        // v2.12.0: ここも 13px 固定だった。文字サイズのつまみを動かしても
        //  変わらない文字が画面に残っているのは、指摘のとおり良くない。
        //  スライダーの比(fontScale / kFontRebase = ユーザーの%)を掛ける。
        //  (fontScale はユーザーの% × 1.5。1.5 で割ると素の% に戻る)
        g.setFont (GzzioLnF::uiFont (juce::jlimit (14.0f, 20.0f,
                                                   14.0f * fontScale / 1.5f), true));
        g.drawText (tip::mode_easy_switch(),
                    r.withTrimmedLeft (13).withTrimmedRight ((int) trackW + 12).toNearestInt(),
                    juce::Justification::centredLeft);
    }
}

void VocalGzzioContent::mouseDown (const juce::MouseEvent& e)
{
    // v2.12.0: テーマチップ(0..5)は「文字・見た目」の吹き出しへ引っ越したので、
    //          ここに残っているのは 6 =「かんたんモード」スイッチだけ。
    if (crossArea.isEmpty()) return;
    if (crossHit (6).contains (e.getPosition()))
        setAdvanced (! advancedMode);
}

void VocalGzzioContent::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    if (! crossArea.isEmpty() && crossHit (6).contains (e.getPosition())) h = 6;
    if (h != crossHover) { crossHover = h; repaint (crossArea.expanded (4, 4)); }
}

void VocalGzzioContent::mouseExit (const juce::MouseEvent&)
{
    if (crossHover != -1) { crossHover = -1; repaint (crossArea.expanded (80, 40)); }
}

void VocalGzzioContent::paintOverChildren (juce::Graphics& g)
{
    if (isOverview()) return;
    // v1.8.0: scatter theme decorations OVER the panels (translucent)
    themePainter.paintForeground (g);

    // v2.12.0: 「スペクトラム非表示 (軽量)」の注記は、軽量テーマごと廃止した。
    //  スペクトラムを止めたいときは エフェクトタブ か かんたんモードへ。
    //  どちらもグラフが隠れた時点で 30Hz のタイマーが止まる(visibilityChanged)。

    drawThemeCross (g);
}

//==============================================================================
VocalGzzioEditor::VocalGzzioEditor (VocalGzzioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), content (p)
{
    setLookAndFeel (&lnf);
    lnf.setKawaiiTypeface (juce::Typeface::createSystemTypefaceFor (
        BinaryData::kawaii_font_ttf, BinaryData::kawaii_font_ttfSize));
    addAndMakeVisible (content);
    content.setSize (baseW, baseH);

    // per-user UI prefs (font/zoom) in AppData: shared by every instance
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "VocalGzzio";
        o.filenameSuffix  = ".settings";
        o.folderName      = "VocalGzzio";
       #if VOCALGZZIO_TESTING
        o.folderName = gz::dataDirectory().getFullPathName();
        o.osxLibrarySubFolder = "Application Support";
       #endif
        o.storageFormat   = juce::PropertiesFile::storeAsXML;
        o.millisecondsBeforeSaving = 400;
        appProps.setStorageParameters (o);
    }
    auto* prefs = appProps.getUserSettings();

    // window-zoom slider drives geometry
    content.onScaleChange = [this, prefs] (float s)
    {
        applyScale (s);
        if (prefs != nullptr) prefs->setValue ("ui_zoom", (double) s);
    };
    // font slider drives text size only (no geometry change)
    content.onFontChange = [this, prefs] (float f)
    {
        lnf.setFontScale (f * kFontRebase);
        content.setFontScale (f * kFontRebase);
        if (prefs != nullptr) prefs->setValue ("ui_font", (double) f);
    };

    // corner-drag resize (keeps 1280x790 aspect, 70..140 percent)
    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) baseW / (double) baseH);
        c->setSizeLimits (juce::roundToInt (baseW * 0.70f), juce::roundToInt (baseH * 0.70f),
                          juce::roundToInt (baseW * 1.40f), juce::roundToInt (baseH * 1.40f));
    }

    // restore saved prefs (defaults: font 100 percent of the rebased size, zoom 100)
    const float f0 = prefs != nullptr ? (float) prefs->getDoubleValue ("ui_font", 1.0) : 1.0f;
    const float z0 = prefs != nullptr ? (float) prefs->getDoubleValue ("ui_zoom", 1.0) : 1.0f;
    content.setUiPrefDisplays (juce::jlimit (0.80f, 1.50f, f0), juce::jlimit (0.70f, 1.40f, z0));
    lnf.setFontScale (juce::jlimit (0.80f, 1.50f, f0) * kFontRebase);
    content.setFontScale (juce::jlimit (0.80f, 1.50f, f0) * kFontRebase);
    applyScale (juce::jlimit (0.70f, 1.40f, z0));

    // ---- v1.4.0: persist editor-only UI state so it survives window close ----
    // (the editor object is destroyed/recreated; these live in the shared file)
    content.onUiStateChange = [this, prefs] (const juce::String& key, int val)
    {
        if (prefs != nullptr) { prefs->setValue (key, val); appProps.saveIfNeeded(); }
    };
    content.getTuner().onRailChange = [this, prefs] (bool r)
    {
        if (prefs != nullptr) { prefs->setValue ("ui_rail", r ? 1 : 0); appProps.saveIfNeeded(); }
    };
    {
        const bool revised = prefs == nullptr || prefs->getIntValue ("ui_layout_revision", 0) < 2;
        const bool adv  = revised || prefs->getBoolValue ("ui_advanced", true);
        const int  tab  = prefs != nullptr ? prefs->getIntValue ("ui_tab", 0) : 0;
        const bool rail = prefs != nullptr && prefs->getBoolValue ("ui_rail", false);
        int theme = prefs != nullptr ? prefs->getIntValue ("ui_theme", 0) : 0;
        if (const char* tm = std::getenv ("GZ_THEME"))   // test hook: force a theme
            theme = juce::String (tm).getIntValue();

        int tabQ = tab, advQ = adv;
        if (const char* tb = std::getenv ("GZ_TAB"))     // test hook: force right-column tab
            tabQ = juce::String (tb).getIntValue();
        if (const char* av = std::getenv ("GZ_ADV"))     // test hook: force advanced mode
            advQ = juce::String (av).getIntValue();
        content.restoreUiState (advQ, tabQ, rail, theme);
        content.setFocusedEditing (! revised && prefs->getBoolValue ("ui_focus", false));
        content.setOverview (revised || prefs->getBoolValue ("ui_overview", true));
        if (prefs != nullptr && revised)
        {
            prefs->setValue ("ui_layout_revision", 2);
            prefs->setValue ("ui_focus", false);
            prefs->setValue ("ui_advanced", true);
            prefs->setValue ("ui_overview", true);
        }
        if (const char* ss = std::getenv ("GZ_SEASON"))  // test hook: force a season (0-3)
            content.forceSeason (juce::String (ss).getIntValue());
    }
}

VocalGzzioEditor::~VocalGzzioEditor()
{
    appProps.saveIfNeeded();
    setLookAndFeel (nullptr);
}

void VocalGzzioEditor::applyScale (float s)
{
    uiScale = juce::jlimit (0.70f, 1.40f, s);
    setSize (juce::roundToInt (baseW * uiScale), juce::roundToInt (baseH * uiScale));
}

void VocalGzzioEditor::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    // Any size change (zoom slider, corner drag, or host restore) lands here.
    const float s = juce::jlimit (0.70f, 1.40f, (float) getWidth() / (float) baseW);
    if (std::abs (s - uiScale) > 0.001f)
    {
        uiScale = s;
        content.syncZoomDisplay (uiScale);   // keep the slider in step, silently
        if (auto* pr = appProps.getUserSettings())
            pr->setValue ("ui_zoom", (double) uiScale);
    }
    content.setBounds (0, 0, baseW, baseH);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) baseW,
                                                        (float) getHeight() / (float) baseH));
}
