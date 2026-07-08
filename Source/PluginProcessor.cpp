#include <cmath>
#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
VocalGzzioProcessor::VocalGzzioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VocalGzzioProcessor::createParameterLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto freqRange = [] (float lo, float hi) {
        R r (lo, hi, 1.0f);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    };

    using A = juce::AudioParameterFloatAttributes;
    auto unit = [] (const char* u) { return A().withLabel (u); };
    auto pid  = [] (const char* id) { return juce::ParameterID { id, 1 }; };

    // Cleanup
    layout.add (std::make_unique<P>(pid ("gate"),     "Gate",       R (-80.0f, -20.0f, 0.5f), -80.0f, unit ("dB")));
    layout.add (std::make_unique<P>(pid ("lowcut"),   "Low Cut",    freqRange (60.0f, 220.0f), 100.0f, unit ("Hz")));
    layout.add (std::make_unique<P>(pid ("mud"),      "Mud",        R (-12.0f, 0.0f, 0.1f),    -3.0f, unit ("dB")));
    layout.add (std::make_unique<P>(pid ("harsh"),    "Harsh",      R (-12.0f, 0.0f, 0.1f),    -1.5f, unit ("dB")));
    layout.add (std::make_unique<P>(pid ("denoise"),  "De-Noise",   R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));
    // Dynamics
    layout.add (std::make_unique<P>(pid ("comp1"),    "Peak Comp",  R (0.0f, 100.0f, 1.0f),    35.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("comp2"),    "Level Comp", R (0.0f, 100.0f, 1.0f),    30.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("attack"),   "Attack",     R (1.0f, 60.0f, 1.0f),      8.0f, unit ("ms")));
    layout.add (std::make_unique<P>(pid ("release"),  "Release",    R (30.0f, 500.0f, 1.0f),  120.0f, unit ("ms")));
    layout.add (std::make_unique<P>(pid ("deess"),    "De-Esser",   R (0.0f, 100.0f, 1.0f),    35.0f, unit ("%")));
    // Tone
    layout.add (std::make_unique<P>(pid ("presence"), "Presence",   R (-6.0f, 9.0f, 0.1f),      1.5f, unit ("dB")));
    layout.add (std::make_unique<P>(pid ("air"),      "Air",        R (0.0f, 9.0f, 0.1f),       2.0f, unit ("dB")));
    layout.add (std::make_unique<P>(pid ("drive"),    "Warmth",     R (0.0f, 100.0f, 1.0f),    15.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("sustain"),  "Sustain",    R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));
    // Output & space
    layout.add (std::make_unique<P>(pid ("makeup"),   "Makeup",     R (0.0f, 18.0f, 0.1f),      3.0f, unit ("dB")));
    layout.add (std::make_unique<P>(pid ("mix"),      "Mix",        R (0.0f, 100.0f, 1.0f),   100.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("width"),    "Width",      R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("doubler"),  "Doubler",    R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("delay"),    "Delay",      R (0.0f, 100.0f, 1.0f),     0.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("revsize"),  "Rev Size",   R (0.0f, 100.0f, 1.0f),    30.0f, unit ("%")));
    layout.add (std::make_unique<P>(pid ("revmix"),   "Reverb",     R (0.0f, 100.0f, 1.0f),    10.0f, unit ("%")));
    // Tuner
    layout.add (std::make_unique<P>(pid ("refpitch"), "Ref Pitch",  R (415.0f, 445.0f, 1.0f), 440.0f, unit ("Hz")));

    return layout;
}

//==============================================================================
void VocalGzzioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = 2;

    hpf.prepare (spec); mud.prepare (spec); harsh.prepare (spec);
    presence.prepare (spec); air.prepare (spec);
    deessDetectHP.prepare (spec);

    juce::dsp::ProcessSpec mono = spec; mono.numChannels = 1;
    dsShelfL.prepare (mono); dsShelfR.prepare (mono);

    comp1.prepare (spec);
    comp2.prepare (spec);
    comp2.setAttack (25.0f);
    comp2.setRelease (250.0f);

    makeup.prepare (spec);
    makeup.setRampDurationSeconds (0.02);
    reverb.prepare (spec);

    dryBuffer.setSize (2, samplesPerBlock, false, false, true);
    scratch.setSize   (2, samplesPerBlock, false, false, true);

    // gate constants
    gateEnvAtk    = 1.0f - std::exp (-1.0f / (0.004f * (float) sampleRate));
    gateEnvRel    = 1.0f - std::exp (-1.0f / (0.070f * (float) sampleRate));
    gateOpenCoef  = 1.0f - std::exp (-1.0f / (0.006f * (float) sampleRate));
    gateCloseCoef = 1.0f - std::exp (-1.0f / (0.140f * (float) sampleRate));
    gateEnv = 0; gateGain = 1;

    // ---- de-noise: LR4 crossovers @ 250 / 1200 / 5000 Hz ----
    for (auto* f : { &lrLP1, &lrHP1, &lrLP2, &lrHP2, &lrLP3, &lrHP3 })
    {
        f->prepare (spec);
        f->setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    }
    lrHP1.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    lrHP2.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    lrHP3.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    lrLP1.setCutoffFrequency (250.0f);  lrHP1.setCutoffFrequency (250.0f);
    lrLP2.setCutoffFrequency (1200.0f); lrHP2.setCutoffFrequency (1200.0f);
    lrLP3.setCutoffFrequency (5000.0f); lrHP3.setCutoffFrequency (5000.0f);
    for (auto& b : bandBuf) b.setSize (2, samplesPerBlock, false, false, true);
    dnEnvAtk    = 1.0f - std::exp (-1.0f / (0.002f * (float) sampleRate));
    dnEnvRel    = 1.0f - std::exp (-1.0f / (0.060f * (float) sampleRate));
    dnOpenCoef  = 1.0f - std::exp (-1.0f / (0.003f * (float) sampleRate));
    dnCloseCoef = 1.0f - std::exp (-1.0f / (0.045f * (float) sampleRate));
    dnFloorRise = std::pow (10.0f, 3.0f / 20.0f / (float) sampleRate);   // +3 dB/s adaptive drift
    for (int b = 0; b < 4; ++b) { dnEnv[b] = 0; dnGain[b] = 1; dnFloor[b] = 1e-5f; }
    dnLearned = false; learnCountdown.store (0);

    // ---- sustain constants ----
    susEnvAtk = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
    susEnvRel = 1.0f - std::exp (-1.0f / (0.180f * (float) sampleRate));
    susEnv = 0; susLift = 0;

    // de-esser envelope constants (fast attack, medium release)
    dsEnvAtk = 1.0f - std::exp (-1.0f / (0.0015f * (float) sampleRate));
    dsEnvRel = 1.0f - std::exp (-1.0f / (0.050f  * (float) sampleRate));
    dsEnv = 0; dsGain = 1; dsCurrentReduction = 0;

    // doubler buffer (max ~40 ms)
    dblBuf.assign ((size_t) ((int) (0.045 * sampleRate) + 8), 0.0f);
    dblWrite = 0; dblLfoPhase = 0;

    // delay buffer (fixed 340 ms slap-ish echo)
    const int dlyLen = (int) (0.75 * sampleRate) + 8;
    dlyBufL.assign ((size_t) dlyLen, 0.0f);
    dlyBufR.assign ((size_t) dlyLen, 0.0f);
    dlyWrite = 0;

    std::fill (std::begin (tunerBuf), std::end (tunerBuf), 0.0f);
    tunerPos.store (0);

    setLatencySamples (0);
    updateParameters();
}

void VocalGzzioProcessor::updateParameters()
{
    const auto sr = currentSampleRate;
    auto p = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto toGain = [] (float db) { return juce::Decibels::decibelsToGain (db); };

    *hpf.state      = *Coefficients::makeHighPass   (sr, p ("lowcut"), 0.707f);
    *mud.state      = *Coefficients::makePeakFilter (sr, 300.0, 1.0f,  toGain (p ("mud")));
    *harsh.state    = *Coefficients::makePeakFilter (sr, 3200.0, 1.2f, toGain (p ("harsh")));
    *presence.state = *Coefficients::makePeakFilter (sr, 4200.0, 0.9f, toGain (p ("presence")));
    *air.state      = *Coefficients::makeHighShelf  (sr, 11000.0, 0.707f, toGain (p ("air")));
    *deessDetectHP.state = *Coefficients::makeHighPass (sr, 5200.0, 0.9f);

    // comp1: fast peak catcher; amount maps threshold -8..-30 dB, ratio 4:1
    const float c1 = p ("comp1") * 0.01f;
    comp1.setThreshold (juce::jmap (c1, -8.0f, -30.0f));
    comp1.setRatio (4.0f);
    comp1.setAttack  (juce::jmax (1.0f, p ("attack") * 0.5f));
    comp1.setRelease (p ("release") * 0.6f);

    // comp2: slow leveller; amount maps threshold -10..-35 dB, ratio 2.5:1
    const float c2 = p ("comp2") * 0.01f;
    comp2.setThreshold (juce::jmap (c2, -10.0f, -35.0f));
    comp2.setRatio (2.5f);

    makeup.setGainDecibels (p ("makeup"));

    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = juce::jlimit (0.0f, 1.0f, p ("revsize") * 0.01f);
    rp.damping  = 0.55f;
    rp.width    = 1.0f;
    rp.wetLevel = juce::jlimit (0.0f, 1.0f, p ("revmix") * 0.01f);
    rp.dryLevel = 1.0f;
    reverb.setParameters (rp);
}

//==============================================================================
bool VocalGzzioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void VocalGzzioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    updateParameters();
    dryBuffer.makeCopyOf (buffer, true);

    // tuner feed (dry mono)
    {
        const float* dl = dryBuffer.getReadPointer (0);
        const float* dr = dryBuffer.getReadPointer (juce::jmin (1, dryBuffer.getNumChannels() - 1));
        int pos = tunerPos.load (std::memory_order_relaxed);
        for (int n = 0; n < numSamples; ++n)
        {
            tunerBuf[pos] = 0.5f * (dl[n] + dr[n]);
            pos = (pos + 1) % tunerSize;
        }
        tunerPos.store (pos, std::memory_order_release);
    }

    // input meter
    {
        float pk = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* q = dryBuffer.getReadPointer (ch);
            for (int n = 0; n < numSamples; ++n) pk = juce::jmax (pk, std::abs (q[n]));
        }
        const float cur = meterIn.load();
        meterIn.store (pk > cur ? pk : cur * 0.985f);
    }

    // ---- gate ----
    {
        const float thr = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("gate")->load());
        for (int n = 0; n < numSamples; ++n)
        {
            float inPk = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                inPk = juce::jmax (inPk, std::abs (buffer.getSample (ch, n)));
            gateEnv += (inPk > gateEnv ? gateEnvAtk : gateEnvRel) * (inPk - gateEnv);
            const float target = gateEnv >= thr ? 1.0f : 0.0f;
            gateGain += (target > gateGain ? gateOpenCoef : gateCloseCoef) * (target - gateGain);
            for (int ch = 0; ch < numCh; ++ch)
                buffer.setSample (ch, n, buffer.getSample (ch, n) * gateGain);
        }
    }

    // ---- de-noise (4-band adaptive downward expander, RX-style Learn) ----
    {
        const float amount = apvts.getRawParameterValue ("denoise")->load() * 0.01f;
        int learn = learnCountdown.load();

        if (amount > 0.001f || learn > 0)
        {
            // split into 4 bands (LR4 crossovers sum flat)
            bandBuf[0].makeCopyOf (buffer, true);
            bandBuf[1].makeCopyOf (buffer, true);
            {
                juce::dsp::AudioBlock<float> b0 (bandBuf[0]);
                juce::dsp::ProcessContextReplacing<float> c0 (b0);
                lrLP1.process (c0);                                  // band0 = LP250
                juce::dsp::AudioBlock<float> b1 (bandBuf[1]);
                juce::dsp::ProcessContextReplacing<float> c1x (b1);
                lrHP1.process (c1x);                                 // rest = HP250
            }
            bandBuf[2].makeCopyOf (bandBuf[1], true);
            {
                juce::dsp::AudioBlock<float> b1 (bandBuf[1]);
                juce::dsp::ProcessContextReplacing<float> c1x (b1);
                lrLP2.process (c1x);                                 // band1 = 250-1200
                juce::dsp::AudioBlock<float> b2 (bandBuf[2]);
                juce::dsp::ProcessContextReplacing<float> c2x (b2);
                lrHP2.process (c2x);                                 // rest = HP1200
            }
            bandBuf[3].makeCopyOf (bandBuf[2], true);
            {
                juce::dsp::AudioBlock<float> b2 (bandBuf[2]);
                juce::dsp::ProcessContextReplacing<float> c2x (b2);
                lrLP3.process (c2x);                                 // band2 = 1200-5000
                juce::dsp::AudioBlock<float> b3 (bandBuf[3]);
                juce::dsp::ProcessContextReplacing<float> c3x (b3);
                lrHP3.process (c3x);                                 // band3 = HP5000
            }

            const float maxAttenDb = 24.0f * amount;
            float attenSum = 0.0f;

            for (int n = 0; n < numSamples; ++n)
            {
                for (int b = 0; b < 4; ++b)
                {
                    float pk = 0.0f;
                    for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
                        pk = juce::jmax (pk, std::abs (bandBuf[b].getSample (ch, n)));

                    dnEnv[b] += (pk > dnEnv[b] ? dnEnvAtk : dnEnvRel) * (pk - dnEnv[b]);

                    if (learn > 0)
                    {
                        dnFloorLearn[b] = juce::jmax (dnFloorLearn[b], dnEnv[b]);
                    }
                    else if (! dnLearned)
                    {
                        // adaptive minimum tracking: fast down, slow drift up
                        if (dnEnv[b] < dnFloor[b]) dnFloor[b] = dnEnv[b];
                        else                       dnFloor[b] = juce::jmin (dnFloor[b] * dnFloorRise, 0.5f);
                        dnFloor[b] = juce::jmax (dnFloor[b], 1e-6f);
                    }

                    const float openThr = dnFloor[b] * 2.5f;   // ~+8 dB above floor
                    float targetGain = 1.0f;
                    if (dnEnv[b] < openThr)
                    {
                        const float below = juce::jlimit (0.0f, 1.0f,
                                              (openThr - dnEnv[b]) / juce::jmax (openThr, 1e-9f));
                        targetGain = juce::Decibels::decibelsToGain (-maxAttenDb * below);
                    }
                    dnGain[b] += (targetGain > dnGain[b] ? dnOpenCoef : dnCloseCoef)
                                     * (targetGain - dnGain[b]);
                    attenSum += 1.0f - dnGain[b];

                    for (int ch = 0; ch < numCh; ++ch)
                        bandBuf[b].setSample (ch, n,
                            bandBuf[b].getSample (juce::jmin (ch, 1), n) * dnGain[b]);
                }

                if (learn > 0) --learn;
            }

            if (learnCountdown.load() > 0)
            {
                learnCountdown.store (learn);
                if (learn <= 0)   // learning just finished: commit profile with margin
                {
                    for (int b = 0; b < 4; ++b)
                        dnFloor[b] = juce::jmax (dnFloorLearn[b] * 1.4f, 1e-6f);
                    dnLearned = true;
                }
            }

            // recombine bands
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* out = buffer.getWritePointer (ch);
                for (int n = 0; n < numSamples; ++n)
                    out[n] = bandBuf[0].getSample (ch, n) + bandBuf[1].getSample (ch, n)
                           + bandBuf[2].getSample (ch, n) + bandBuf[3].getSample (ch, n);
            }

            meterDN.store (juce::jlimit (0.0f, 1.0f,
                               attenSum / (4.0f * (float) juce::jmax (1, numSamples))));
        }
        else
            meterDN.store (meterDN.load() * 0.9f);
    }

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);

    // ---- subtractive EQ ----
    hpf.process (ctx);
    mud.process (ctx);
    harsh.process (ctx);

    // ---- 2-stage compression with GR metering ----
    float prePk = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* q = buffer.getReadPointer (ch);
        for (int n = 0; n < numSamples; ++n) prePk = juce::jmax (prePk, std::abs (q[n]));
    }
    prePk = juce::jmax (prePk, 1e-7f);

    if (apvts.getRawParameterValue ("comp1")->load() > 0.5f) comp1.process (ctx);
    if (apvts.getRawParameterValue ("comp2")->load() > 0.5f) comp2.process (ctx);

    {
        float postPk = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* q = buffer.getReadPointer (ch);
            for (int n = 0; n < numSamples; ++n) postPk = juce::jmax (postPk, std::abs (q[n]));
        }
        const float grDb = 20.0f * std::log10 (juce::jmax (postPk, 1e-7f) / prePk);
        const float cur  = meterGR.load();
        meterGR.store (grDb < cur ? grDb : cur * 0.90f + grDb * 0.10f);
    }

    // ---- de-esser (split-band style: detect >5.2k, duck a 6.5k high shelf) ----
    {
        const float amount = apvts.getRawParameterValue ("deess")->load() * 0.01f;
        if (amount > 0.001f)
        {
            scratch.makeCopyOf (buffer, true);
            juce::dsp::AudioBlock<float> sblock (scratch);
            juce::dsp::ProcessContextReplacing<float> sctx (sblock);
            deessDetectHP.process (sctx);   // detector band

            // envelope of sibilant band (block-wise per sample)
            const float thr = juce::Decibels::decibelsToGain (-30.0f + (1.0f - amount) * 12.0f);
            const float maxCutDb = 12.0f * amount + 4.0f;   // up to ~16 dB

            auto* sL = scratch.getReadPointer (0);
            auto* sR = scratch.getReadPointer (juce::jmin (1, scratch.getNumChannels() - 1));
            auto* bL = buffer.getWritePointer (0);
            auto* bR = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

            for (int n = 0; n < numSamples; ++n)
            {
                const float det = juce::jmax (std::abs (sL[n]), std::abs (sR[n]));
                dsEnv += (det > dsEnv ? dsEnvAtk : dsEnvRel) * (det - dsEnv);

                // desired shelf cut in dB when sibilance exceeds threshold
                float wantDb = 0.0f;
                if (dsEnv > thr)
                    wantDb = juce::jlimit (0.0f, maxCutDb,
                                           20.0f * std::log10 (dsEnv / thr) * 1.5f);
                dsCurrentReduction += 0.02f * (wantDb - dsCurrentReduction);

                // update shelf every 32 samples (cheap enough, smooth enough)
                if ((n & 31) == 0)
                {
                    auto co = Coefficients::makeHighShelf (currentSampleRate, 6500.0, 0.8f,
                                  juce::Decibels::decibelsToGain (-dsCurrentReduction));
                    dsShelfL.coefficients = co;
                    dsShelfR.coefficients = co;
                }
                bL[n] = dsShelfL.processSample (bL[n]);
                if (bR) bR[n] = dsShelfR.processSample (bR[n]);
            }
            meterDS.store (juce::jlimit (0.0f, 1.0f, dsCurrentReduction / 16.0f));
        }
        else
        {
            dsCurrentReduction *= 0.9f;
            meterDS.store (juce::jlimit (0.0f, 1.0f, dsCurrentReduction / 16.0f));
        }
    }

    // ---- additive EQ ----
    presence.process (ctx);
    air.process (ctx);

    makeup.process (ctx);

    // ---- Warmth + Sustain (のび) + dry/wet ----
    const float wet = apvts.getRawParameterValue ("mix")->load() * 0.01f;
    const float dry = 1.0f - wet;
    const float driveAmt = apvts.getRawParameterValue ("drive")->load() * 0.01f;
    const float k = 1.0f + driveAmt * 5.0f;
    const float susAmt = apvts.getRawParameterValue ("sustain")->load() * 0.01f;

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* w = buffer.getWritePointer (ch);
        auto* d = dryBuffer.getReadPointer (juce::jmin (ch, dryBuffer.getNumChannels() - 1));
        for (int n = 0; n < numSamples; ++n)
        {
            float x = w[n];
            if (driveAmt > 0.001f)
                x = std::tanh (x * k) / k;

            // sustain: track envelope on ch0, lift the decaying tail + even harmonics
            if (susAmt > 0.001f)
            {
                if (ch == 0)
                {
                    const float pk = std::abs (x);
                    susEnv += (pk > susEnv ? susEnvAtk : susEnvRel) * (pk - susEnv);
                    const float envDb = 20.0f * std::log10 (juce::jmax (susEnv, 1e-6f));
                    // below -18 dB the tail gets lifted, up to +7 dB at -40 dB
                    float wantLift = 0.0f;
                    if (envDb < -18.0f && envDb > -55.0f)
                        wantLift = juce::jlimit (0.0f, 7.0f, (-18.0f - envDb) * 0.32f) * susAmt;
                    susLift += 0.002f * (wantLift - susLift);
                }
                const float lg = juce::Decibels::decibelsToGain (susLift);
                const float bias = 0.35f * susAmt;
                const float sat  = std::tanh (x * lg * (1.0f + susAmt) + bias) - std::tanh (bias);
                x = x * (1.0f - 0.5f * susAmt) + sat * 0.5f * susAmt + x * (lg - 1.0f) * 0.6f;
            }

            w[n] = x * wet + d[n] * dry;
        }
    }

    // ---- Doubler (modulated short delay, mono-safe L/R inversion) + Width ----
    {
        const float dblAmt   = apvts.getRawParameterValue ("doubler")->load() * 0.01f;
        const float widthAmt = apvts.getRawParameterValue ("width")->load() * 0.01f;
        if (numCh >= 2 && (dblAmt > 0.001f || widthAmt > 0.001f))
        {
            auto* L = buffer.getWritePointer (0);
            auto* R = buffer.getWritePointer (1);
            const int sz = (int) dblBuf.size();
            const float baseDelay = 0.017f * (float) currentSampleRate;   // 17 ms
            const float lfoInc = juce::MathConstants<float>::twoPi * 0.7f / (float) currentSampleRate;

            for (int n = 0; n < numSamples; ++n)
            {
                const float mid = 0.5f * (L[n] + R[n]);
                dblBuf[(size_t) dblWrite] = mid;

                // modulated tap for the doubler voice
                dblLfoPhase += lfoInc;
                if (dblLfoPhase > juce::MathConstants<float>::twoPi)
                    dblLfoPhase -= juce::MathConstants<float>::twoPi;
                const float mod = std::sin (dblLfoPhase) * 0.004f * (float) currentSampleRate; // +-4ms
                float rpF = (float) dblWrite - (baseDelay + mod);
                while (rpF < 0) rpF += (float) sz;
                const int   i0 = (int) rpF % sz;
                const int   i1 = (i0 + 1) % sz;
                const float fr = rpF - std::floor (rpF);
                const float tap = dblBuf[(size_t) i0] * (1.0f - fr) + dblBuf[(size_t) i1] * fr;

                // fixed 12 ms tap for width decorrelation
                int wp = dblWrite - (int) (0.012f * (float) currentSampleRate);
                while (wp < 0) wp += sz;
                const float wtap = dblBuf[(size_t) wp];

                dblWrite = (dblWrite + 1) % sz;

                const float side = dblAmt * 0.5f * tap + widthAmt * 0.8f * wtap;
                L[n] = mid + side;
                R[n] = mid - side;
            }
        }
    }

    // ---- Delay (subtle echo) ----
    {
        const float dAmt = apvts.getRawParameterValue ("delay")->load() * 0.01f;
        if (dAmt > 0.001f && numCh >= 1)
        {
            const int sz = (int) dlyBufL.size();
            const int dlySamps = (int) (0.34 * currentSampleRate);   // 340 ms
            const float fb = 0.28f;
            auto* L = buffer.getWritePointer (0);
            auto* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
            for (int n = 0; n < numSamples; ++n)
            {
                int rp = dlyWrite - dlySamps; while (rp < 0) rp += sz;
                const float eL = dlyBufL[(size_t) rp];
                const float eR = dlyBufR[(size_t) rp];
                dlyBufL[(size_t) dlyWrite] = L[n] + eL * fb;
                dlyBufR[(size_t) dlyWrite] = (R ? R[n] : L[n]) + eR * fb;
                dlyWrite = (dlyWrite + 1) % sz;
                L[n] += eL * dAmt * 0.45f;
                if (R) R[n] += eR * dAmt * 0.45f;
            }
        }
    }

    // ---- Reverb ----
    juce::dsp::AudioBlock<float> rb (buffer);
    juce::dsp::ProcessContextReplacing<float> rc (rb);
    reverb.process (rc);

    // output meter
    {
        float pk = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* q = buffer.getReadPointer (ch);
            for (int n = 0; n < numSamples; ++n) pk = juce::jmax (pk, std::abs (q[n]));
        }
        const float cur = meterOut.load();
        meterOut.store (pk > cur ? pk : cur * 0.985f);
    }
}

//==============================================================================
void VocalGzzioProcessor::readTunerBuffer (std::vector<float>& dest) const
{
    dest.resize (tunerSize);
    const int pos = tunerPos.load (std::memory_order_acquire);
    for (int i = 0; i < tunerSize; ++i)
        dest[(size_t) i] = tunerBuf[(pos + i) % tunerSize];
}

//==============================================================================
juce::AudioProcessorEditor* VocalGzzioProcessor::createEditor()
{
    return new VocalGzzioEditor (*this);
}

void VocalGzzioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalGzzioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalGzzioProcessor();
}
