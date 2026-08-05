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
    railToggle .setButtonText (tip::rail_label());
    railToggle .setTooltip    (tip::rail_tip());
    rangeButton.setButtonText (tip::range_label());
    repaint();
}

VocalTuner::VocalTuner (VocalGzzioProcessor& p) : proc (p)
{
    buffer.resize ((size_t) VocalGzzioProcessor::tunerSize);
    for (int hz = 415; hz <= 445; ++hz)
        refPitchBox.addItem (juce::String (hz) + " Hz", hz);
    refPitchBox.setSelectedId (440, juce::dontSendNotification);
    refPitchBox.setJustificationType (juce::Justification::centred);
    refPitchBox.onChange = [this]
    {
        if (auto* prm = proc.apvts.getParameter ("refpitch"))
            prm->setValueNotifyingHost (
                proc.apvts.getParameterRange ("refpitch")
                    .convertTo0to1 ((float) refPitchBox.getSelectedId()));
    };
    addAndMakeVisible (refPitchBox);

    // v1.4.0: note-rail toggle + vocal-range check (in the tuner header)
    railToggle.setButtonText (tip::rail_label());
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

    startTimerHz (30);
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
    const int    N  = (int) buffer.size();
    const double sr = proc.getTunerSampleRate();
    hasPitch = false; pitchConf = 0.0f;
    if (N < 512 || sr < 8000.0) return;

    // remove DC and reject silence
    double mean = 0.0;
    for (int i = 0; i < N; ++i) mean += buffer[(size_t) i];
    mean /= N;
    double energy = 0.0;
    for (int i = 0; i < N; ++i) { const double v = buffer[(size_t) i] - mean; energy += v * v; }
    const double rms = std::sqrt (energy / N);
    if (rms < 0.0016) return;

    // search lags for ~70 Hz (low male) up to ~1100 Hz (soprano); keep a constant
    // number of difference terms so the CMND normalisation stays comparable.
    const int maxLag = juce::jmin (N / 2, (int) (sr / 70.0));
    const int minLag = juce::jmax (2,     (int) (sr / 1100.0));
    if (maxLag <= minLag + 2) return;
    const int W = N - maxLag;   // difference-window length

    std::vector<double> d   ((size_t) (maxLag + 1), 0.0);   // difference function
    std::vector<double> cm  ((size_t) (maxLag + 1), 1.0);   // cumulative-mean-normalised

    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double s = 0.0;
        const float* x = buffer.data();
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
        if (bestt < 0 || bestv > 0.55) return;   // too unvoiced to trust
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
    cents = (float) ((midi - nearest) * 100.0);
    const int nn  = ((nearest % 12) + 12) % 12;
    const int oct = nearest / 12 - 1;
    noteName = juce::String (kNoteNames[nn]) + juce::String (oct);
    lastMidi = nearest;
    hasPitch = true;

    // ---- vocal-range capture: only extend on a stably HELD, confident note ----
    // A note must be the same for several consecutive frames and sit inside the
    // plausible sung band; this blocks single-frame octave/harmonic glitches from
    // stretching the measured range.
    if (rangeChecking)
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
        hold = 18;
        const float sm = 0.20f;
        dispCents += (cents - dispCents) * sm;
        dispFreq  += (freq  - dispFreq)  * sm;
        if (dispFreq <= 0.0f) dispFreq = freq;
    }
    else if (hold > 0)
    {
        --hold;
        dispCents += (0.0f - dispCents) * 0.06f;
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
    auto full = getLocalBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (full, 14.0f);
    g.setColour (Palette::panelLn);
    g.drawRoundedRectangle (full.reduced (0.5f), 14.0f, 1.2f);

    auto lf = GzzioLnF::uiFont;   // legible face helper
    float fs = 1.0f;              // follow the text-size slider
    if (auto* g2 = dynamic_cast<GzzioLnF*> (&getLookAndFeel())) fs = g2->getFontScale();

    // header
    g.setColour (Palette::inkSoft);
    g.setFont (lf (12.0f, true));
    g.drawText (tip::T ("\xe9\x9f\xb3\xe7\xa8\x8b\xe3\x83\xbb\xe3\x83\xa1\xe3\x83\xbc\xe3\x82\xbf\xe3\x83\xbc", "PITCH / METERS"),   // 音程・メーター
                full.withTrimmedLeft (16).withHeight (24).translated (0, 5),
                juce::Justification::centredLeft);

    const bool show = (hold > 0);
    const bool rangeActive = (rangeChecking || rangeHi >= 0);

    // Layout zones (no overlaps): header (top) | badge+graph row | meter rows
    const int headerH = 26;
    const int meterRowH = 15;
    const int meterZoneH = meterRowH * 5 + 6;               // 5 bars + a little pad
    auto zone = getLocalBounds().reduced (16, 8).withTrimmedTop (headerH);
    auto topRow  = zone.removeFromTop (zone.getHeight() - meterZoneH);
    auto meterZone = zone;                                  // remainder

    // ---- Big note badge (left of the top row) ----
    auto noteBox = topRow.removeFromLeft (120);
    {
        auto nb = noteBox.reduced (0, 2).toFloat();
        g.setColour (show && std::abs (dispCents) < 5.0f ? Palette::green.withAlpha (0.12f)
                                                         : Palette::track.withAlpha (0.5f));
        g.fillRoundedRectangle (nb, 10.0f);
        auto nnArea = nb.withTrimmedBottom (20.0f);
        g.setColour (show ? Palette::ink : Palette::ink.withAlpha (0.30f));
        // v2.8.0 ★枠の高さに合わせる。
        // こだわりモードでは音程パネルが 160px しかないので、この枠は 13px まで
        // 縮む。そこへ 42px の字を描いていたため、上の見出しと下の
        // 「+3 cent / 440.0Hz」を **突き抜けて重なって** いた（かんたんモードは
        // 195px あるので目立たず、こだわりモードでだけ崩れていた）。
        g.setFont (lf (juce::jlimit (16.0f, 42.0f, nnArea.getHeight() * 0.86f), true));
        g.drawText (show ? noteName : juce::String ("--"), nnArea, juce::Justification::centred);
        g.setColour (show ? (std::abs (dispCents) < 5.0f ? Palette::green : Palette::inkSoft)
                          : Palette::inkSoft.withAlpha (0.4f));
        g.setFont (lf (15.0f, true));
        g.drawText (show ? ((dispCents >= 0 ? "+" : "") + juce::String (juce::roundToInt (dispCents))
                            + " cent / " + juce::String (dispFreq, 1) + "Hz")
                         : tip::T ("\xe5\xa3\xb0\xe3\x82\x92\xe5\x85\xa5\xe3\x82\x8c\xe3\x81\xa6\xe3\x81\xad", "make a sound"),  // 声を入れてね
                    nb.removeFromBottom (21.0f), juce::Justification::centred);
    }

    // ---- Scrolling pitch-history graph (the main feature) ----
    // X = time (older left -> newest right), Y = cents error (+50 top, -50 bottom).
    topRow.removeFromLeft (8);
    auto graphArea = topRow.reduced (2, 2).toFloat();

    // v1.4.0 P5: reserve an opaque strip at the top of the graph for the range
    // read-out, so its (now larger, font-scaled) text never sits on the trace.
    juce::Rectangle<float> rangeBanner;
    if (rangeActive)
    {
        // v2.8.0: バナーはグラフの高さの半分までにする。
        // こだわりモードのグラフは 33px しかないので、20px を丸ごと取ると
        // 残り 10px。そこへ 17px の「♯高い」「♭低い」を描いていたので、
        // 音域チェックを押した瞬間に文字とグラフが団子になっていた。
        const float bannerH = juce::jmin (juce::jmax (20.0f, 15.0f * fs),
                                          graphArea.getHeight() * 0.5f);
        rangeBanner = graphArea.removeFromTop (bannerH);
        graphArea.removeFromTop (juce::jmin (3.0f, graphArea.getHeight() * 0.1f));
    }
    auto gr = graphArea;

    g.setColour (Palette::bgBot);
    g.fillRoundedRectangle (gr, 8.0f);

    const float midY = gr.getCentreY();
    const float halfH = gr.getHeight() * 0.5f;

    if (railMode)
    {
        // ---- v1.4.0 note rail: scale lanes with the pitch trajectory by pitch ----
        // Auto-range the visible window around recent notes (fallback mid2C..hiC).
        int lo = 127, hi = -1;
        for (int i = 0; i < histLen; ++i)
            if (histValid[i] && histMidi[i] >= 0) { lo = juce::jmin (lo, histMidi[i]); hi = juce::jmax (hi, histMidi[i]); }
        if (hi < 0) { lo = 60; hi = 72; }                 // C4..C5 default
        lo -= 2; hi += 2;
        if (hi - lo < 12) { const int c = (lo + hi) / 2; lo = c - 6; hi = c + 6; }
        const int span = hi - lo;
        auto noteY = [&] (float m) { return gr.getBottom() - (m - (float) lo) / (float) span * gr.getHeight(); };

        // lane stripes: shade the white/black key rows, label each C and A
        static const bool black[12] = { false,true,false,true,false,false,true,false,true,false,true,false };
        g.setFont (lf (8.5f, false));
        for (int m = lo; m <= hi; ++m)
        {
            const int pc = ((m % 12) + 12) % 12;
            const float y0 = noteY ((float) m + 0.5f), y1 = noteY ((float) m - 0.5f);
            g.setColour (black[pc] ? juce::Colour (0xff10151c) : juce::Colour (0xff171d26));
            g.fillRect (gr.getX(), y0, gr.getWidth(), y1 - y0);
            if (pc == 0 || pc == 9)   // label C and A rows (zone anchors)
            {
                g.setColour (Palette::inkSoft.withAlpha (0.5f));
                g.drawText (midiToJp (m), juce::Rectangle<float> (gr.getX() + 2, y0, 52, y1 - y0),
                            juce::Justification::centredLeft);
            }
        }
        g.setColour (Palette::panelLn.withAlpha (0.5f));
        g.drawRoundedRectangle (gr, 8.0f, 1.0f);

        // trajectory: connect consecutive valid notes, snap to lane centre + cents
        const float dxr = gr.getWidth() / (float) (histLen - 1);
        float px = 0, py = 0; bool have = false;
        for (int i = 0; i < histLen; ++i)
        {
            const int idx = (histPos + i) % histLen;
            if (! histValid[idx] || histMidi[idx] < 0) { have = false; continue; }
            const float x = gr.getX() + i * dxr;
            const float m = (float) histMidi[idx] + juce::jlimit (-50.0f, 50.0f, histCents[idx]) / 100.0f;
            const float y = noteY (m);
            const float a = 0.3f + 0.7f * (i / (float) (histLen - 1));
            const bool  inTune = std::abs (histCents[idx]) < 20.0f;
            if (have)
            {
                const auto lc = inTune ? Palette::green : Palette::yellowDk;
                g.setColour (lc.withAlpha (a * 0.25f));
                g.drawLine (px, py, x, y, 5.0f);
                g.setColour (lc.withAlpha (a));
                g.drawLine (px, py, x, y, 2.2f);
            }
            px = x; py = y; have = true;
        }
    }
    else
    {
    // green "in-tune" band (+-10 cents)
    const float bandH = (10.0f / 50.0f) * halfH;
    g.setColour (Palette::green.withAlpha (0.16f));
    g.fillRect (gr.getX(), midY - bandH, gr.getWidth(), bandH * 2.0f);

    // gridlines at 0 / +-25 / +-50
    g.setColour (Palette::panelLn.withAlpha (0.7f));
    g.fillRect (gr.getX(), midY - 0.5f, gr.getWidth(), 1.0f);
    for (int k = 1; k <= 2; ++k)
    {
        const float off = (k * 25.0f / 50.0f) * halfH;
        g.setColour (Palette::panelLn.withAlpha (0.35f));
        g.fillRect (gr.getX(), midY - off, gr.getWidth(), 1.0f);
        g.fillRect (gr.getX(), midY + off, gr.getWidth(), 1.0f);
    }

    // labels ♯ / ♭ / ぴったり
    // v2.8.0: グラフが低いときは「♯高い/♭低い」を出さない。17px の字を
    // それより低い枠に描くと、上下の行に食い込んで団子になるだけで読めない。
    // 音域チェック中のこだわりモード(グラフ約16px)がこれに当たる。
    g.setColour (Palette::inkSoft.withAlpha (0.95f));
    g.setFont (lf (13.0f, true));
    if (gr.getHeight() >= 38.0f)
    {
        g.drawText (tip::T ("\xe2\x99\xaf\xe9\xab\x98\xe3\x81\x84", "\xe2\x99\xaf sharp"),   // ♯高い
                    gr.withHeight (17.0f).translated (4, 1), juce::Justification::topLeft);
        g.drawText (tip::T ("\xe2\x99\xad\xe4\xbd\x8e\xe3\x81\x84", "\xe2\x99\xad flat"),   // ♭低い
                    gr.withHeight (17.0f).translated (4, gr.getHeight() - 18.0f), juce::Justification::bottomLeft);
    }
    g.setColour (Palette::green);
    g.drawText (tip::pitchgraph_center(),
                gr.reduced (4, 0).withHeight (16.0f).withY (midY - 8.0f),
                juce::Justification::centredRight);

    // plot the trail: iterate oldest->newest across the width
    const float dx = gr.getWidth() / (float) (histLen - 1);
    float prevX = 0, prevY = 0; bool havePrev = false;
    for (int i = 0; i < histLen; ++i)
    {
        const int idx = (histPos + i) % histLen;         // oldest first
        const float x = gr.getX() + i * dx;
        if (! histValid[idx]) { havePrev = false; continue; }
        const float cy = juce::jlimit (-50.0f, 50.0f, histCents[idx]);
        const float yy = midY - (cy / 50.0f) * halfH;
        const float a  = 0.25f + 0.75f * (i / (float) (histLen - 1));   // fade older
        const bool  inTune = std::abs (cy) < 10.0f;
        if (havePrev)
        {
            const auto lc = inTune ? Palette::green : Palette::yellowDk;
            g.setColour (lc.withAlpha (a * 0.22f));           // soft glow underneath
            g.drawLine (prevX, prevY, x, yy, 5.6f);
            g.setColour (lc.withAlpha (a));
            g.drawLine (prevX, prevY, x, yy, 2.4f);
        }
        prevX = x; prevY = yy; havePrev = true;
    }
    // newest point marker
    if (show)
    {
        const float yy = midY - juce::jlimit (-50.0f, 50.0f, dispCents) / 50.0f * halfH;
        const bool inTune = std::abs (dispCents) < 10.0f;
        g.setColour (inTune ? Palette::green : Palette::yellowDk);
        g.fillEllipse (gr.getRight() - 5.0f, yy - 4.0f, 8.0f, 8.0f);
        g.setColour (Palette::panel);
        g.drawEllipse (gr.getRight() - 5.0f, yy - 4.0f, 8.0f, 8.0f, 1.2f);
    }
    g.setColour (Palette::panelLn);
    g.drawRoundedRectangle (gr, 8.0f, 1.0f);
    }   // end cents strip (else)

    // ---- meter bars with full labels: IN OUT GR DS DN ----
    // Drawn in EVERY mode (rail or cents) so IN/OUT/GR/DS/DN never disappear.
    {
        auto mArea = meterZone.toFloat();
        const float rowH = (float) meterRowH, bH = 8.0f;
        auto toNorm = [] (float lin) {
            float db = 20.0f * std::log10 (juce::jmax (lin, 1e-6f));
            return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        };
        struct Bar { float val; juce::String label; juce::Colour col; bool grad; };
        Bar bars[5] = {
            { toNorm (dispIn),  "IN",  Palette::green,    true  },
            { toNorm (dispOut), "OUT", Palette::yellowDk, true  },
            { juce::jlimit (0.0f, 1.0f, -dispGR / 18.0f), "GR", Palette::salmon, false },
            { juce::jlimit (0.0f, 1.0f,  dispDS),         "DS", Palette::ink,    false },
            { juce::jlimit (0.0f, 1.0f,  dispDN),         "DN", Palette::green.darker (0.2f), false }
        };
        // v1.5.0: short labels only (the long explanations moved to tooltips);
        // this removes the label pile-up and frees width for the bars.
        // v1.6.0: right strip hosts the gear meter; bars shorten accordingly
        auto gearZone = mArea.removeFromRight (54.0f);

        const float labelW = 46.0f;
        const int   segs   = 26;   // LED-segment look (reads well on stream captures)
        for (auto& bar : bars)
        {
            auto row  = mArea.removeFromTop (rowH);
            auto lab  = row.removeFromLeft (labelW);
            g.setColour (Palette::inkSoft);
            g.setFont (lf (juce::jlimit (13.5f, 16.5f, 13.5f * fs), true));
            g.drawFittedText (bar.label, lab.toNearestInt(), juce::Justification::centredLeft, 1);
            auto barR = row.withHeight (bH).translated (0, (rowH - bH) * 0.5f);
            g.setColour (Palette::track);
            g.fillRoundedRectangle (barR, 3.0f);

            const float segW = barR.getWidth() / (float) segs;
            const int   lit  = juce::jlimit (0, segs, (int) std::round (bar.val * (float) segs));
            for (int s = 0; s < lit; ++s)
            {
                const float t = (float) s / (float) (segs - 1);
                const juce::Colour c = bar.grad
                    ? (t < 0.62f ? Palette::green.interpolatedWith (Palette::yellow, t / 0.62f)
                                 : Palette::yellow.interpolatedWith (Palette::salmon, (t - 0.62f) / 0.38f))
                    : bar.col.withMultipliedBrightness (0.72f + 0.28f * t);
                g.setColour (c);
                g.fillRoundedRectangle (barR.getX() + (float) s * segW + 0.6f, barR.getY(),
                                        juce::jmax (1.0f, segW - 1.2f), barR.getHeight(), 1.5f);
            }
        }

        // v1.6.0 gear meter: brass drive gear + mint pinion, meshed and counter-
        // rotating (ratio 12:8). Speed follows the output level (set in the timer).
        {
            const float cy  = gearZone.getCentreY();
            const float bx  = gearZone.getX() + 20.0f;
            const float by  = cy + 8.0f;
            const float sx  = bx + 21.5f;
            const float syy = by - 18.5f;
            const float aB  = juce::degreesToRadians (gearAngle);
            const float aS  = juce::degreesToRadians (-gearAngle * (12.0f / 8.0f) + 14.0f);

            g.setColour (Palette::yellowDk);
            g.fillPath (gearBig, juce::AffineTransform::rotation (aB).translated (bx, by));
            g.setColour (Palette::panelLn);
            g.strokePath (gearBig, juce::PathStrokeType (1.0f),
                          juce::AffineTransform::rotation (aB).translated (bx, by));
            g.setColour (Palette::ice);
            g.fillPath (gearSmall, juce::AffineTransform::rotation (aS).translated (sx, syy));
            g.setColour (Palette::panelLn);
            g.strokePath (gearSmall, juce::PathStrokeType (1.0f),
                          juce::AffineTransform::rotation (aS).translated (sx, syy));
            // ruby bearings (watch-movement wink)
            g.setColour (Palette::salmon);
            g.fillEllipse (bx - 2.0f, by - 2.0f, 4.0f, 4.0f);
            g.fillEllipse (sx - 1.6f, syy - 1.6f, 3.2f, 3.2f);
        }
    }

    // ---- v1.4.0 P5 vocal-range check: opaque, font-scaled read-out banner ----
    // Sits in the strip reserved above the trace (solid background -> the text is
    // always legible and never tangles with the waveform behind it).
    if (rangeActive && ! rangeBanner.isEmpty())
    {
        const auto accent = rangeChecking ? Palette::salmon : Palette::ice;
        g.setColour (Palette::panel2);
        g.fillRoundedRectangle (rangeBanner, 6.0f);
        g.setColour (accent.withAlpha (0.85f));
        g.drawRoundedRectangle (rangeBanner.reduced (0.5f), 6.0f, 1.4f);
        g.setColour (accent);                                   // left accent tab
        g.fillRoundedRectangle (rangeBanner.withWidth (4.0f), 2.0f);

        juce::String txt;
        if (rangeChecking)
            txt = tip::range_capturing() + (rangeHi >= 0
                    ? juce::String ("  ") + midiToJp (rangeLo)
                      + juce::String::fromUTF8 ("\x20\xe3\x80\x9c\x20") + midiToJp (rangeHi)
                    : juce::String());
        else
            txt = tip::range_result_prefix() + rangeResultText();

        g.setColour (Palette::ink);
        g.setFont (lf (juce::jmax (12.5f, 12.0f * fs), true));
        g.drawFittedText (txt, rangeBanner.toNearestInt().reduced (12, 1),
                          juce::Justification::centredLeft, 1, 0.9f);
    }
}

void VocalTuner::resized()
{
    refPitchBox.setBounds (getWidth() - 100, 6, 92, 22);
    // note-rail toggle + range check, to the left of the ref-pitch box
    rangeButton.setBounds (getWidth() - 100 - 6 - 96, 6, 96, 22);
    railToggle .setBounds (getWidth() - 100 - 6 - 96 - 6 - 78, 6, 78, 22);
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
        gg.setFont (lf (11.0f, false));
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
            gg.setFont (lf (10.5f, false));
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
        g.setFont (lf (9.0f, true));
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
        for (auto& it : items)
        {
            g.setColour (it.c);
            g.fillRoundedRectangle (lx, ly + 3.0f, 12.0f, 12.0f, 3.0f);
            const auto txt = juce::String::fromUTF8 (it.utf8);
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
        g.setFont (lf (9.5f, true));
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
        g.setFont (lf (9.5f, false));
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

VocalGzzioContent::VocalGzzioContent (VocalGzzioProcessor& p)
    : processor (p), tuner (p), eqGraph (p)
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
    sceneSolo.onClick = [this] { currentScene = 0; applyScene(); };
    sceneTalk.onClick = [this] { currentScene = 1; applyScene(); };
    sceneBand.onClick = [this] { currentScene = 2; applyScene(); };

    // learn button (denoise profile capture)
    learnButton.setTooltip (tip::learn_tip() + "\n" + tip::note_learn());
    learnButton.setColour (juce::TextButton::buttonColourId, Palette::green.withAlpha (0.16f));
    learnButton.onClick = [this] { processor.requestDenoiseLearn(); };
    styleButton (learnButton);
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
        const int i = juce::jlimit (0, 6, revTypeBox.getSelectedId() - 1);
        fxInfoText = d[i];
        infoText   = d[i];   // v1.6.0: the combo lives in section 4, so explain there too
        revTypeBox.setTooltip (tip::rev_type_tip() + "\n" + d[i]);
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

    addAndMakeVisible (tuner);
    addAndMakeVisible (eqGraph);

    // update notice: hidden until the background check finds a newer release
    updateNotice.setFont (GzzioLnF::uiFont (12.0f, true), false, juce::Justification::centredRight);
    updateNotice.setColour (juce::HyperlinkButton::textColourId, Palette::yellow);
    updateNotice.setTooltip (tip::T ("\xe3\x83\x80\xe3\x82\xa6\xe3\x83\xb3\xe3\x83\xad\xe3\x83\xbc\xe3\x83\x89\xe3\x83\x9a\xe3\x83\xbc\xe3\x82\xb8\xe3\x82\x92\xe9\x96\x8b\xe3\x81\x8d\xe3\x81\xbe\xe3\x81\x99", "Opens the download page"));   // opens the download page
    addChildComponent (updateNotice);
    startUpdateCheck();

    refreshPresetDisplays();

    // (v2.1.0: A/Bスロットの初期化はプロセッサ側。空スロットへの切替は音を変えない)
    updateABButtons();

    updateSeqModeVisibility();
    updateTabVisibility();
    dlySyncBox.onChange();   // sync the TIME knob enabled state
    applySessionLock();      // v2.9.0: 保存状態がセッションONなら開いた時点で灰色に
    setSize (1360, 858);   // v1.8.3: 横+80 / 縦+24 (下部46pxは背景帯)
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
    if (paramID == "rev_type")  return { "Normal", "Room", "Plate", "Hall", "Church", "Spring", "Shimmer" };
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
    const int d = 22;
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

    eqGraph.setVisible (! easy && ! fx && ! liteTheme());   // v1.7.0: also hidden in lightweight modes
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

    // v2.4.0 かんたんモードは「よく使う7ツマミ」だけ。残りは丸ごと隠す。
    auto showKnob = [] (Knob& k, bool v) { k.slider.setVisible (v); k.label.setVisible (v); };
    for (auto* k : { &gate, &lowCut, &popK, &lipK, &resK, &rideK, &humK, &consK,
                     &comp1K, &attackK, &releaseK,
                     &presenceK, &airK, &warmthK, &sustainK, &ringK,
                     &makeupK, &widthK, &doublerK, &delayK, &revSizeK, &liftK })
        showKnob (*k, advancedMode);
    for (auto* k : { &inGainK, &denoiseK, &mudK, &harshK, &comp2K, &deessK, &revMixK, &mixK })
        showKnob (*k, true);
    // 隠したツマミのランプも一緒に消す(残ると宙に浮いて見える)
    lampGate.btn.setVisible (advancedMode);
    lampDbl .btn.setVisible (advancedMode);
    lampDly .btn.setVisible (advancedMode);
    revTypeBox.setVisible (advancedMode);

    if (! advancedMode) currentTab = 0;
    updateTabVisibility();
    resized();
    repaint();
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
    if (themeMode == 1)
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
    learnButton.setButtonText (learning ? juce::String ("LEARN...") : juce::String ("LEARN"));
    learnButton.setColour (juce::TextButton::buttonColourId,
                           learning ? Palette::yellow : Palette::green.withAlpha (0.16f));
    learnButton.setColour (juce::TextButton::textColourOffId,
                           learning ? Palette::bgTop : Palette::ink);

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
            if (suggest)
                autoSetupMsg += " " + tip::autoset_learn_suggest();
            infoText = autoSetupMsg;          // surface it in the graph info strip too
            // the auto result replaced whatever the presets had set, so show the
            // combos as unselected again (values live in the knobs now)
            auto& st2 = processor.apvts.state;
            st2.setProperty ("ui_voice_preset", 0, nullptr);
            st2.setProperty ("ui_mic_preset",   0, nullptr);
            st2.setProperty ("ui_eq_preset",    0, nullptr);
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
                    {
                        processor.apvts.replaceState (juce::ValueTree::fromXml (*xml));
                        refreshPresetDisplays();
                    }
        });
}

//==============================================================================
void VocalGzzioContent::paint (juce::Graphics& g)
{
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

        // v1.6.1 brushed-metal grain: only for the standard dark look; skipped in the
        // light and lightweight modes to keep those surfaces clean / minimal.
        if (themeMode == 0)
        {
            const int w = getWidth(), h = getHeight();
            g.setColour (Palette::ink.withAlpha (0.022f));
            for (int yy = 34; yy < h; yy += 56)
                g.drawHorizontalLine (yy, 0.0f, (float) w);
            g.setColour (juce::Colours::black.withAlpha (0.10f));
            for (int yy = 62; yy < h; yy += 56)
                g.drawHorizontalLine (yy, 0.0f, (float) w);
        }
    }

    // ---- header bar (72 px: taller row 1 for larger, legible combos) ----
    auto hdr = getLocalBounds().withHeight (72);
    g.setColour (Palette::panel);
    g.fillRect (hdr);
    g.setColour (juce::Colours::white.withAlpha (0.04f));       // top elevation highlight
    g.fillRect (hdr.withHeight (1));
    g.setColour (Palette::panelLn);
    g.fillRect (hdr.withY (hdr.getBottom() - 1).withHeight (1));

    drawCrossSwitch (g);   // v1.7.0 theme switch (painted on top of the header bar)

    // mascot: medium 80px illustration next to the logo (bright art on a light card)
    if (mascot.isValid())
    {
        juce::Rectangle<float> badge (10.0f, 6.0f, 52.0f, 52.0f);
        g.setColour (Palette::badge);
        g.fillRoundedRectangle (badge, 11.0f);
        g.setColour (Palette::panelLn);
        g.drawRoundedRectangle (badge.reduced (0.5f), 11.0f, 1.0f);
        g.drawImage (themeMode == 1 && kawaiiMascot.isValid() ? kawaiiMascot : mascot,
                     badge.reduced (3.0f), juce::RectanglePlacement::centred, false);
    }

    // v1.5.0 brand: butter title on navy (site palette), single clean pass
    // v2.0.1: 明るいモードで黄タイトルが沈む対策 + サブ行/バージョンが小さすぎて
    //         読めなかったので拡大(11/10px -> 12.5px、バージョンは太字)。
    g.setFont (GzzioLnF::uiFont (22.0f, true));    // logo text: fixed size, not font-scaled
    {
        const juce::Rectangle<int> tr (72, 5, 220, 28);
        g.setColour (Palette::accentOn (Palette::yellow, Palette::bgTop));
        g.drawText (tip::title(), tr, juce::Justification::centredLeft);
    }

    g.setColour (Palette::accentOn (Palette::inkSoft, Palette::bgTop));
    g.setFont (GzzioLnF::uiFont (12.5f, false));
    g.drawText ("G'zzio  /  VOCAL CHANNEL STRIP", juce::Rectangle<int> (72, 33, 240, 15),
                juce::Justification::centredLeft);
    g.setColour (Palette::accentOn (Palette::ink, Palette::bgTop));
    g.setFont (GzzioLnF::uiFont (12.5f, true));
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
        g.setFont (cfont (9.0f, false));
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
        g.setFont (cfont (heroBig ? 20.0f : 15.5f, true));
        g.drawText (tip::hero_title(),
                    heroArea.withTrimmedLeft (heroBig ? 22 : 16).withWidth (heroBig ? 164 : 118),
                    juce::Justification::centredLeft);
        g.setColour (autoSetupMsg.isNotEmpty() ? Palette::ink : Palette::inkSoft);
        g.setFont (cfont (heroBig ? 14.0f : 12.5f, autoSetupMsg.isNotEmpty()));
        // かんたんモードは中央の大ボタンの右側へ、結果/ヒントを描く
        // v2.9.0: 右端はセッション席(バッジの左)で止める。以前は帯の右端まで
        // 描いていたので、そのままだとバッジの下へ潜り込む。
        const int msgX = heroBig ? songSetupButton.getRight() + 18
                                 : heroArea.getX() + 10 + 128 + 128 + 8 + 128 + 14;
        const int msgR = latBadgeArea.getX() - 12;
        if (msgR > msgX + 40)
            g.drawText (autoSetupMsg.isNotEmpty() ? autoSetupMsg
                        : heroBig ? tip::easy_go_hint() : tip::hero_hint(),
                        juce::Rectangle<int> (msgX, heroArea.getY(),
                                              msgR - msgX, heroArea.getHeight()),
                        juce::Justification::centredLeft);

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

        // v2.4.0 かんたんモード: パネルは1枚だけ。番号も付けない。
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
        g.drawText (tip::easy_hint(),
                    cleanArea.withHeight (hh).translated (0, 5).withTrimmedRight (120),
                    juce::Justification::centredRight);
    }
    else
    {
    drawPanel (cleanArea, tip::T ("\x31\x20\xe3\x81\x8d\xe3\x82\x8c\xe3\x81\x84\xe3\x81\xab\xe3\x81\x99\xe3\x82\x8b", "1 CLEAN UP"));
    drawPanel (dynArea,   tip::T ("\x32\x20\xe9\x9f\xb3\xe9\x87\x8f\xe3\x82\x92\xe3\x81\x9d\xe3\x82\x8d\xe3\x81\x88\xe3\x82\x8b", "2 LEVEL"));
    drawPanel (toneArea,  tip::T ("\x33\x20\xe9\x9f\xb3\xe8\x89\xb2\xe3\x82\x92\xe3\x81\xa4\xe3\x81\x8f\xe3\x82\x8b", "3 TONE"));
    drawPanel (spaceArea, tip::T ("\x34\x20\xe3\x81\xb2\xe3\x82\x8d\xe3\x81\x8c\xe3\x82\x8a\xe3\x83\xbb\xe4\xbb\x95\xe4\xb8\x8a\xe3\x81\x92", "4 SPACE & FINISH"));
    // v1.6.0: caption for the reverb-type pulldown in the section-4 header
    g.setColour (Palette::accentOn (Palette::inkSoft, Palette::panel));   // v2.0.0 読める濃さへ
    g.setFont (cfont (11.5f, true));
    g.drawText (tip::T ("\xe3\x81\xb2\xe3\x81\xb3\xe3\x81\x8d\xe3\x81\xae\xe7\xa8\xae\xe9\xa1\x9e", "Reverb type"),
                juce::Rectangle<int> (spaceArea.getRight() - 178 - 96, spaceArea.getY() + 3, 90, 22),
                juce::Justification::centredRight);
    drawPanel (seqArea,   juce::String());   // header hosts the EQ | effects tabs

    if (currentTab == 0)
    {
        // The EQ graph itself is drawn by the EQGraph child component (eqGraphArea).
        // Info line in the gap between the graph and the knob strip shows the
        // description of the last selected preset (default: usage hint).
        g.setColour (Palette::accentOn (Palette::inkSoft, Palette::panel));   // v2.0.0
        g.setFont (cfont (12.0f));
        g.drawText (infoText.isNotEmpty() ? infoText : tip::subtitle_hint(),
                    juce::Rectangle<int> (seqArea.getX(), eqGraphArea.getBottom() + 3,
                                          seqArea.getWidth(), 18),
                    juce::Justification::centred);
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
            g.setFont (cfont (10.5f, false));
            g.drawText (tip::at_amount_label(), atLabelAmt, juce::Justification::centredLeft);
            g.drawText (tip::at_speed_label(),  atLabelSpd, juce::Justification::centredLeft);
            g.drawText (tip::orn_label(),       atLabelOrn, juce::Justification::centredLeft);

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
    if (themeMode == 1 && kawaiiMascot.isValid())
    {
        const float mh = 500.0f;
        const float mw = mh * (float) kawaiiMascot.getWidth() / (float) kawaiiMascot.getHeight();
        const float mx = -mw * 0.26f;                        // anchored LEFT (part off-edge)
        const float my = (float) getHeight() - mh * 0.82f;   // anchored bottom
        g.setOpacity (0.38f);
        g.drawImageTransformed (kawaiiMascot,
            juce::AffineTransform::scale (mw / (float) kawaiiMascot.getWidth(),
                                          mh / (float) kawaiiMascot.getHeight())
                .translated (mx, my), false);
        g.setOpacity (1.0f);
    }
}

void VocalGzzioContent::placeRow (juce::Rectangle<int> area, std::initializer_list<Knob*> ks)
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
    const int n  = (int) ks.size();
    const int cw = area.getWidth() / n;
    int i = 0;
    for (auto* k : ks)
    {
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
    const int W   = getWidth();
    const int H   = getHeight();
    const int pad = 12;
    const int bh  = 22;

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

    // ---- header row 1: voice / mic / EQ preset (TALL combos) + scene + mode ----
    // Combos are 32 px tall so their label font is no longer shrunk to fit (that
    // was why the text stayed small at 120%); wider too, for long preset names.
    const int comboH = 32;
    const int comboW = 165;   // v2.0.0: シーンボタンの文字が切れないぶんへ融通
    int x = 252;
    voiceBox   .setBounds (x, 8, comboW, comboH); x += comboW + 5;
    micBox     .setBounds (x, 8, comboW, comboH); x += comboW + 5;
    eqPresetBox.setBounds (x, 8, comboW, comboH); x += comboW + 5;
    // v2.0.0: 「弾き…」「トー…」と省略されていた。文字数に合わせた幅にする
    // v2.0.1: ゆるかわ書体(Mochiy Pop One)は字幅が広く「トーク」が62pxでも
    // 切れていた。実機スクショで74pxに収まっていた「弾き語り」はそのまま、
    // トークへ+8px移す(Bandは英字なので50pxで足りる。右端の余白は維持)。
    sceneSolo.setBounds (x, 11, 74, 26); x += 78;
    sceneTalk.setBounds (x, 11, 70, 26); x += 74;
    sceneBand.setBounds (x, 11, 50, 26);

    // v2.4.0: 「かんたん/こだわり」ボタンは廃止。空いた112pxぶんテーマ列を右へ広げ、
    //         下段にはひと目でわかる大きな「かんたんモード」スイッチを置く。
    //         右下(W-pad-112 以降)は文字/大きさスライダーの席なので、スイッチ幅は
    //         そこに食い込まない kSwitchW(244) に収めてある。
    crossArea = { W - pad - 368, 8, 368, kChipH + 6 + kSwitchH };
    themePainter.setBounds (getLocalBounds());

    // ---- header row 2: A/B, save/load, reset (left) | meter (mid) | font/zoom (right) ----
    x = 252;
    auto place = [&] (juce::TextButton& b, int bw) { b.setBounds (x, 44, bw, bh); x += bw + 6; };
    place (abA, 36); place (abB, 36); place (abCopy, 50);
    place (saveButton, 58); place (loadButton, 58); place (resetButton, 58);

    int rx = W - pad;
    // v1.8.2 ヘッダ再配置: 文字/大きさは「かんたん」ボタンの真下に縦積み
    // (＊型スイッチ・Bandピルとどの解像度でも干渉しない)
    // v2.8.0 ★数値が「1…」に切れていたのを直した。
    // juce は「スライダーの幅 - 30px」までしか数値ボックスに与えないので、
    // 幅62pxだと 32px しか取れず "100%" が入らなかった。**文字を大きくする
    // ための操作なのに、今いくつなのか読めない**のはさすがに困る。
    // 幅を 78px にすると 46px 取れて全部出る(左端はテーマ帯のすぐ右)。
    fontSliderLabel.setBounds (W - pad - 120, 40, 38, 13);
    fontSlider     .setBounds (W - pad -  80, 40, 78, 13);
    zoomSliderLabel.setBounds (W - pad - 120, 55, 38, 13);
    zoomSlider     .setBounds (W - pad -  80, 55, 78, 13);
    juce::ignoreUnused (rx, bh);

    // stream-loudness meter: sits in the row-2 centre gap (clear of both the left
    // buttons and the font/zoom sliders; it used to overlap the font slider).
    lvMeterArea = { 600, 42, 250, 30 };
    // the update-notice link shares that slot (mutually exclusive with the meter)
    updateNotice.setBounds (600, 44, 268, bh);

    // ---- v1.5.0 hero band: one-press auto setup, right under the header ----
    // v2.4.0: かんたんモードでは、この2つのボタンが主役なので大きくする。
    //         (ツマミだけ大きくしてボタンが小さいままだと、視線の行き先が逆になる)
    heroBig  = ! advancedMode;
    heroArea = { pad, 78, W - pad * 2, heroBig ? 76 : 42 };

    // v2.9.0 セッションモード: ヒーロー帯の右端に「セッション」スイッチと
    // 「追加遅延 +0.0ms」バッジを置く。**かんたん/こだわりのどちらでも同じ位置**に
    // 出すので、セッション中にモードを行き来しても迷子にならない。
    // 先に席を取ってから、残りの幅で中央のボタンとヒント文を割り振る。
    const int kSessBtnW  = 116;
    const int kSessBadgeW = 108;
    const int kSessBlockW = kSessBadgeW + 8 + kSessBtnW + 12;   // 244
    {
        const int sbH = heroBig ? 34 : 26;
        sessionButton.setBounds (heroArea.getRight() - 12 - kSessBtnW,
                                 heroArea.getY() + (heroArea.getHeight() - sbH) / 2,
                                 kSessBtnW, sbH);
        latBadgeArea = { sessionButton.getX() - 8 - kSessBadgeW, heroArea.getY() + 3,
                         kSessBadgeW, heroArea.getHeight() - 6 };
    }

    if (heroBig)
    {
        // v2.4.0: うた自動(10秒おまかせ)が主役。画面中央にどんと置き、
        // トーク自動はその左に控えめに。ヒントは右側の余白に描く。
        // v2.9.0: セッション席(右端244px)を除いた中央へ寄せる。真ん中のままだと
        // ヒント文の幅が170pxまで潰れて読めなくなる。
        const int bw = 340, bh = 56;
        const int cx = (W - kSessBlockW) / 2;
        songSetupButton.setBounds (cx - bw / 2,
                                   heroArea.getY() + (heroArea.getHeight() - bh) / 2, bw, bh);
        autoSetupButton.setBounds (cx - bw / 2 - 12 - 168,
                                   heroArea.getY() + (heroArea.getHeight() - 44) / 2, 168, 44);
        songSetupButton.getProperties().set ("fontH", 20.0);   // 大ボタンに合う文字へ
        autoSetupButton.getProperties().set ("fontH", 15.0);
    }
    else
    {
        const int bw = 128, bh = 28;
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
    const int leftW = 520;   // v2.3.0: 1段目が7ノブになったぶん左列を拡張
    const int gap   = 6;
    int y = top;

    // ================= v2.4.0 かんたんモードの専用レイアウト =================
    // 旧「スタンダード」は右列(EQ/エフェクト)もツマミも全部出ていて、名前ほど
    // 簡単ではなかった。かんたんモードでは画面を横いっぱいに使い、
    //   おまかせ設定 → チューナー → よく使う7ツマミ
    // だけにする。ツマミが大きくなるぶん、初めての人でも触る場所に迷わない。
    if (! advancedMode)
    {
        const int fullW   = W - pad * 2;
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
        const int tunerH  = juce::jmin (195, juce::jmax (150, avail - 260));
        const int panelH  = juce::jmin (390, avail - tunerH - 14);
        tuner.setBounds (pad, availTop, fullW, tunerH);
        cleanArea = { pad, availTop + tunerH + 14, fullW, panelH };
        // 使わないパネルは空にしておく(paint 側は空なら描かない)
        dynArea = toneArea = spaceArea = seqArea = {};
        eqGraphArea = analysisArea = atColArea = keyColArea = {};

        rowScale = 1.55f;                             // ツマミ名も数値も一回り大きく
        // v2.4.0: マイク音量を先頭に(いちばん基本の「小さい/大きい」を直す場所)
        placeRow (cleanArea, { &inGainK, &denoiseK, &mudK, &harshK, &comp2K, &deessK, &revMixK, &mixK });
        rowScale = 1.0f;
        learnButton.setBounds (cleanArea.getRight() - 104, cleanArea.getY() + 6, 96, 26);

        placeLamp (lampDn,  denoiseK);
        placeLamp (lampDs,  deessK);
        placeLamp (lampRev, revMixK);
        return;
    }

    fxWarnArea = {};                              // v2.8.0: 案内帯はかんたんモード専用
    tuner.setBounds (pad, y, leftW, 160);          // v1.8.3: チューナーを圧縮し3行を同高に
    y += 160 + gap;

    const int panelH = 166;
    cleanArea = { pad, y, leftW, panelH }; y += panelH + gap;
    dynArea   = { pad, y, leftW, panelH }; y += panelH + gap;
    toneArea  = { pad, y, leftW, panelH };         // v1.8.3: 他行と同高=ノブ同径

    // v2.3.0: ポップ/リップを追加して7ノブに(左列を520pxへ広げて径を確保)
    // v2.6.0: ジー音(電源ハムの自動除去)を掃除セクションへ追加して8ノブ
    placeRow (cleanArea, { &gate, &lowCut, &mudK, &harshK, &denoiseK, &popK, &lipK, &humK });
    // v2.4.0: マイク音量(入力トリム)と音量キープ(Vocal Rider相当)を追加して7ノブ
    placeRow (dynArea,   { &inGainK, &comp1K, &comp2K, &attackK, &releaseK, &deessK, &rideK });
    // v1.8.3: 音色のノブ径を他セクション(5ノブ行)と同径化。
    // placeRow は area幅をノブ数で等分するため、4ノブだと1本あたりが広く=大きくなる。
    // ダミー1枠ぶん右に余白を確保して5等分にそろえ、実ノブは左4枠に置く。
    // v2.4.0: なめらか を音色セクションの先頭に追加(6ノブ)
    // v2.6.0: ことば(子音エンハンサー)を追加して7ノブ(他セクションと同数)
    placeRow (toneArea, { &resK, &consK, &presenceK, &airK, &warmthK, &sustainK, &ringK });

    // ---- right column: dynamic EQ (top) + OUTPUT / SPACE (bottom) ----
    const int eqX = pad + leftW + 12;
    const int rW  = W - pad - eqX;

    const int kBgStrip = 46;                       // v1.8.3: 下部は背景(季節/オーロラ)が見える帯
    spaceArea = { eqX, H - kBgStrip - 158, rW, 158 };
    // v2.0.0: サビリフトを仕上げ列の末尾に追加(8ノブ)
    placeRow (spaceArea, { &makeupK, &mixK, &widthK, &doublerK, &delayK, &revSizeK, &revMixK, &liftK });
    // v1.6.0: reverb-type selector lives in the section-4 header (both modes)
    revTypeBox.setBounds (spaceArea.getRight() - 178, spaceArea.getY() + 3, 170, 22);

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
        auto vrow = r1.removeFromRight (juce::jmax (410, r1.getWidth() * 62 / 100));   // v1.8.0 voice cluster
        // v2.0.0: 息・エモを空間セクションに追加(4ノブ)
        placeCells (r1, { &duckK, &choAmtK, &brK, &emoK });
        auto vb = vrow.removeFromLeft (96);
        vcOnButton.setBounds (vb.getX() + 4, vb.getCentreY() - 26, 88, 24);
        jnOnButton.setBounds (vb.getX() + 4, vb.getCentreY() + 2,  88, 24);
        auto hb = vrow.removeFromLeft (92);
        jnHarmBox.setBounds (hb.getX() + 2, hb.getCentreY() - 26, 88, 24);
        jnSoloButton.setBounds (hb.getX() + 2, hb.getCentreY() + 2, 88, 24);
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
            atOnButton.setBounds (row1.removeFromLeft (96).reduced (1, 2));
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
    // (v1.5.0: auto-setup buttons moved to the hero band above the columns)
    // TEMPO FIT: v2.8.0 ★位置を直した。
    // 右端に置いていたが、同じ場所に「ひびきの種類」プルダウン
    // (getRight()-178 から 170px) があり、110px のうち 108px が下敷きに
    // なっていた。プルダウンのほうが手前なので、押すとプルダウンが開く＝
    // **このボタンは事実上押せなかった**。
    // 右側はプルダウンとその見出し(getRight()-274 から 90px)で埋まっているので、
    // パネル見出し「4 ひろがり・仕上げ」の右隣、左寄りの空き地へ移す。
    tempoFitButton.setBounds (spaceArea.getX() + 168, spaceArea.getY() + 3, 110, 19);
    // right-column tabs: left side of the panel header
    tabEqButton.setBounds (seqArea.getX() + 10, seqArea.getY() + 3, 56, 20);
    tabFxButton.setBounds (seqArea.getX() + 70, seqArea.getY() + 3, 100, 20);
    // Smart EQ mode + on/off: top-right inside the panel header (EQ tab only)
    seqOnButton.setBounds (seqArea.getRight() - 78,          seqArea.getY() + 3, 72, 19);
    seqModeBox .setBounds (seqArea.getRight() - 78 - 6 - 84, seqArea.getY() + 2, 84, 21);

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
    //         0..5 = テーマチップ(ふつう/ゆるふわ/自然/ブランド/色覚/軽量)
    //         6    = かんたんモードのスイッチ(下段いっぱい)
    const int x = crossArea.getX(), y = crossArea.getY();
    if (idx == 6) return { x, y + kChipH + 6, kSwitchW, kSwitchH };
    const int step = (crossArea.getWidth() + 4) / 6;      // 6等分(チップ間4px)
    return { x + idx * step, y, step - 4, kChipH };
}

void VocalGzzioContent::drawCrossSwitch (juce::Graphics& g)
{
    if (crossArea.isEmpty())
        return;

    // mode signature colours (index == mode == cross cell)
    const juce::Colour accents[5] = {
        juce::Colour (0xff7c8493),   // 0 標準       slate
        juce::Colour (0xfff4a9b6),   // 1 ゆるふわ    sakura
        juce::Colour (0xffa0a7af),   // 2 軽量       gray
        juce::Colour (0xfff2c14a),   // 3 ライト     amber (sun)
        juce::Colour (0xff9ad9c4)    // 4 ゆるふわ軽量 mint
    };

    // subtle track behind the plus so it reads as a single control
    juce::Rectangle<int> uni = crossHit (0);
    for (int i = 1; i < 5; ++i) uni = uni.getUnion (crossHit (i));
    g.setColour (Palette::panelLn.withAlpha (0.16f));
    g.fillRoundedRectangle (uni.toFloat().expanded (3.0f), 5.0f);

    auto drawIcon = [&g] (int mode, juce::Rectangle<float> cell, juce::Colour col)
    {
        const auto c = cell.getCentre();
        const float s = 3.4f;
        g.setColour (col);
        if (mode == 1 || mode == 4)          // heart (filled for yuru, outline for yuru-lite)
        {
            juce::Path h;
            h.startNewSubPath (c.x, c.y + s * 0.8f);
            h.cubicTo (c.x - s * 1.35f, c.y - s * 0.2f, c.x - s * 0.55f, c.y - s * 1.1f, c.x, c.y - s * 0.3f);
            h.cubicTo (c.x + s * 0.55f, c.y - s * 1.1f, c.x + s * 1.35f, c.y - s * 0.2f, c.x, c.y + s * 0.8f);
            h.closeSubPath();
            if (mode == 4) g.strokePath (h, juce::PathStrokeType (1.1f));
            else           g.fillPath (h);
        }
        else if (mode == 3)                  // sun
        {
            g.fillEllipse (c.x - s * 0.5f, c.y - s * 0.5f, s, s);
            for (int a = 0; a < 8; ++a)
            {
                const float rd = juce::degreesToRadians ((float) (a * 45));
                g.drawLine (c.x + std::cos (rd) * s * 0.9f,  c.y + std::sin (rd) * s * 0.9f,
                            c.x + std::cos (rd) * s * 1.35f, c.y + std::sin (rd) * s * 1.35f, 1.0f);
            }
        }
        else if (mode == 2)                  // "minimal": two short stacked lines
        {
            g.fillRoundedRectangle (c.x - s,        c.y - s * 0.55f, s * 2.0f, 1.5f, 0.7f);
            g.fillRoundedRectangle (c.x - s * 0.6f, c.y + s * 0.55f, s * 1.2f, 1.5f, 0.7f);
        }
        else                                 // 0 標準: a small solid dot
        {
            g.fillEllipse (c.x - s * 0.7f, c.y - s * 0.7f, s * 1.4f, s * 1.4f);
        }
    };

    for (int i = 0; i < 5; ++i)
    {
        auto cell = crossHit (i).toFloat();
        const bool active = (i == themeMode);
        const bool hover  = (i == crossHover);

        g.setColour (active ? accents[i]
                    : hover  ? accents[i].withAlpha (0.42f)
                             : Palette::panelLn.withAlpha (0.34f));
        g.fillRoundedRectangle (cell, 3.0f);
        g.setColour (active ? accents[i].darker (0.30f) : Palette::panelLn.withAlpha (0.55f));
        g.drawRoundedRectangle (cell.reduced (0.5f), 3.0f, 1.0f);

        drawIcon (i, cell, active ? juce::Colours::white.withAlpha (0.95f)
                                  : Palette::ink.withAlpha (hover ? 0.8f : 0.5f));
    }
}

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
    // v2.8.0: 英語UIへ切り替えたときに案内帯も訳す
    fxWarnButton.setButtonText (tip::fxwarn_label());
    fxWarnButton.setTooltip (tip::fxwarn_tip());

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
    comp1K.label.setText (tip::T ("\xe3\x83\x94\xe3\x83\xbc\xe3\x82\xaf\xe5\x9c\xa7\xe7\xb8\xae", "PEAK COMP"), juce::dontSendNotification);
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
    themeMode = juce::jlimit (0, 7, m);
    if (themeMode != 0) lastTheme = themeMode;   // 中央Nトグルの復帰先

    // palette: 軽量(5)はダーク(0)を流用、他は1:1 (7=色覚CUD)
    Palette::applyTheme (themeMode == 5 ? 0 : themeMode);
    // v1.9.0: ポップアップ/ツールチップ/ボタンの色をテーマに追随させる
    if (auto* g = dynamic_cast<GzzioLnF*> (&getLookAndFeel())) g->refreshPaletteColours();

    // ブランド(3)で全UI英語化
    const bool wantEnglish = (themeMode == 3);
    if (tip::english != wantEnglish) { tip::english = wantEnglish; refreshLanguage(); }

    themePainter.setMode (themeMode, getLocalBounds());
    applyThemeToKnobs();
    // v2.8.0: ここに advancedMode の条件が抜けていた。そのため
    //  ・かんたんモードでテーマを押すと EQ グラフがツマミの上に出てくる
    //  ・初期状態では 0x0 の大きさで「表示中」になり、誰にも見えないまま
    //    30Hz で 4096点FFTが回り続ける（無駄なCPU）
    // という2つが起きていた。タブ表示の判定と同じ式にそろえる。
    eqGraph.setVisible (advancedMode && currentTab != 1 && ! liteTheme());
    if (onUiStateChange) onUiStateChange ("ui_theme", themeMode);
    repaint();
}

void VocalGzzioContent::drawJuiceServer (juce::Graphics& g)
{
    if (themeMode != 1) return;

    const float w = 64.0f, h = 94.0f;
    const float x = (float) getWidth()  - w - 12.0f;
    const float y = (float) getHeight() - h - 8.0f;

    // stand / legs
    g.setColour (juce::Colour (0xffd7c4ad));
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
    g.setColour (juce::Colour (0xffc2d0d8));
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

// v1.8.1 モードスイッチ: 3x2の文字ピル(＊配置) + 中央N。全モード共通・ワンクリック
void VocalGzzioContent::drawThemeCross (juce::Graphics& g)
{
    if (crossArea.isEmpty()) return;
    // v2.4.0: 上段=テーマ6枚 / 下段=大きな「かんたんモード」スイッチ。
    //         オーロラは席を譲って廃止(themeMode 4 は ゆるふわ へ寄せる)。
    const juce::String armName[6] = {
        tip::theme_plain(),
        tip::T ("\xe3\x82\x86\xe3\x82\x8b\xe3\x81\xb5\xe3\x82\x8f", "Yuru"),
        tip::T ("\xe8\x87\xaa\xe7\x84\xb6", "Nature"),
        tip::T ("\xe3\x83\x96\xe3\x83\xa9\xe3\x83\xb3\xe3\x83\x89", "Brand"),
        tip::T ("\xe8\x89\xb2\xe8\xa6\x9a", "CUD"),
        tip::T ("\xe8\xbb\xbd\xe9\x87\x8f", "Lite")
    };

    // v2.8.0 ★ベールの範囲を絞った。
    // crossArea を丸ごと塗っていたので、その右下にある「文字」「大きさ」の
    // スライダーと数値まで 55% の膜がかかって読みにくくなっていた
    // (paintOverChildren で子の上から塗るので、隠しようがない)。
    // 実際に中身があるのは「上段=テーマチップの帯」と「下段=かんたんスイッチ」の
    // 2か所だけなので、そこだけ塗る。
    g.setColour (Palette::panel.withAlpha (0.55f));
    g.fillRoundedRectangle (juce::Rectangle<int> (crossArea.getX(), crossArea.getY(),
                                                 crossArea.getWidth(), kChipH)
                                .toFloat().expanded (4.0f, 3.0f), 10.0f);
    g.fillRoundedRectangle (crossHit (6).toFloat().expanded (4.0f, 3.0f), 10.0f);

    // ---- 上段: テーマチップ ----
    g.setFont (GzzioLnF::uiFont (11.5f, true));
    for (int i = 0; i < 6; ++i)
    {
        const auto r   = crossHit (i).toFloat();
        const bool sel = (themeMode == kThemeOfChip[i]);
        const bool hov = (crossHover == i);
        g.setColour (sel ? Palette::yellow
                         : hov ? Palette::panel2.brighter (0.25f) : Palette::panel2);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (sel ? Palette::yellowDk : Palette::panelLn);
        g.drawRoundedRectangle (r, 8.0f, hov ? 1.6f : 1.0f);
        g.setColour (sel ? Palette::bgTop : Palette::ink);
        g.drawText (armName[i], r.toNearestInt(), juce::Justification::centred);
    }

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
        g.setFont (GzzioLnF::uiFont (13.0f, true));
        g.drawText (tip::mode_easy_switch(),
                    r.withTrimmedLeft (13).withTrimmedRight ((int) trackW + 12).toNearestInt(),
                    juce::Justification::centredLeft);
    }
}

void VocalGzzioContent::mouseDown (const juce::MouseEvent& e)
{
    if (crossArea.isEmpty()) return;
    for (int i = 0; i < 7; ++i)
        if (crossHit (i).contains (e.getPosition()))
        {
            if (i < 6) setThemeMode (kThemeOfChip[i]);
            else       setAdvanced (! advancedMode); // v2.4.0 かんたんモードのON/OFF
            return;
        }
}

void VocalGzzioContent::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    if (! crossArea.isEmpty())
        for (int i = 0; i < 7; ++i)
            if (crossHit (i).contains (e.getPosition())) { h = i; break; }
    if (h != crossHover) { crossHover = h; repaint (crossArea.expanded (4, 4)); }
}

void VocalGzzioContent::mouseExit (const juce::MouseEvent&)
{
    if (crossHover != -1) { crossHover = -1; repaint (crossArea.expanded (80, 40)); }
}

void VocalGzzioContent::paintOverChildren (juce::Graphics& g)
{
    // v1.8.0: scatter theme decorations OVER the panels (translucent)
    themePainter.paintForeground (g);

    // lightweight modes: label the (hidden) spectrum area so it doesn't look broken
    if (liteTheme() && currentTab != 1 && ! eqGraphArea.isEmpty())
    {
        g.setColour (Palette::inkSoft.withAlpha (0.60f));
        g.setFont (GzzioLnF::uiFont (13.0f, false));
        g.drawText (tip::T ("\xe3\x82\xb9\xe3\x83\x9a\xe3\x82\xaf\xe3\x83\x88\xe3\x83\xa9\xe3\x83\xa0\xe9\x9d\x9e\xe8\xa1\xa8\xe7\xa4\xba\x20\x28\xe8\xbb\xbd\xe9\x87\x8f\x29", "Spectrum hidden (Lite)"),
                    eqGraphArea, juce::Justification::centred);
    }


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
        const bool adv  = prefs != nullptr && prefs->getBoolValue ("ui_advanced", false);
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
