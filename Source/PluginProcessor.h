#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>

//==============================================================================
// VocalGzzio (ボーカルグッジオ) - real-time vocal channel strip, zero latency.
// Chain: Gate -> HPF -> Mud cut -> Harsh cut -> Comp1 (peaks) -> Comp2 (level)
//        -> De-esser -> Presence -> Air -> Warmth -> Doubler/Width -> Delay -> Reverb
// Tuner + meters are analysis-only (audio is never delayed).
//==============================================================================
class VocalGzzioProcessor : public juce::AudioProcessor
{
public:
    VocalGzzioProcessor();
    ~VocalGzzioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMS", createParameterLayout() };

    //== Tuner (analysis ring, read by editor timer) ==
    static constexpr int tunerSize = 2048;
    void   readTunerBuffer (std::vector<float>& dest) const;
    double getTunerSampleRate() const noexcept { return currentSampleRate; }

    //== Meters ==
    float getInputLevel()  const noexcept { return meterIn.load();  }
    float getOutputLevel() const noexcept { return meterOut.load(); }
    float getGainReductionDb() const noexcept { return meterGR.load(); }
    float getDeEssActivity()   const noexcept { return meterDS.load(); }   // 0..1
    float getDenoiseActivity() const noexcept { return meterDN.load(); }   // 0..1

    //== De-noise learn (RX-style: capture noise profile while silent) ==
    void requestDenoiseLearn() noexcept
    {
        learnCountdown.store ((int) (1.5 * currentSampleRate));
        for (auto& f : dnFloorLearn) f = 1e-6f;
    }
    bool isDenoiseLearning() const noexcept { return learnCountdown.load() > 0; }

private:
    using Filter       = juce::dsp::IIR::Filter<float>;
    using Coefficients = juce::dsp::IIR::Coefficients<float>;
    using StereoFilter = juce::dsp::ProcessorDuplicator<Filter, Coefficients>;

    StereoFilter hpf, mud, harsh, presence, air;
    juce::dsp::Compressor<float> comp1, comp2;
    juce::dsp::Gain<float>       makeup;
    juce::dsp::Reverb            reverb;

    // De-esser: side-chain band-pass detector + high-shelf gain reduction
    StereoFilter deessDetectHP;                 // detector isolate (~5 kHz+)
    float dsEnv = 0.0f, dsGain = 1.0f;
    float dsEnvAtk = 0.0f, dsEnvRel = 0.0f;
    juce::dsp::IIR::Filter<float> dsShelfL, dsShelfR;   // applied reduction shelf
    float dsCurrentReduction = 0.0f;            // smoothed dB of shelf cut

    juce::AudioBuffer<float> dryBuffer, scratch;
    double currentSampleRate = 44100.0;

    // Noise gate
    float gateEnv = 0.0f, gateGain = 1.0f;
    float gateEnvAtk = 0, gateEnvRel = 0, gateOpenCoef = 0, gateCloseCoef = 0;

    // ---- De-noise: 4-band Linkwitz-Riley split + per-band downward expander ----
    // Crossovers at 250 / 1200 / 5000 Hz. Zero latency (IIR).
    juce::dsp::LinkwitzRileyFilter<float> lrLP1, lrHP1, lrLP2, lrHP2, lrLP3, lrHP3;
    juce::AudioBuffer<float> bandBuf[4];
    float dnEnv[4]   = {};          // per-band envelope
    float dnFloor[4] = { 1e-5f, 1e-5f, 1e-5f, 1e-5f };   // estimated noise floor
    float dnFloorLearn[4] = {};     // capture during learn
    float dnGain[4]  = { 1, 1, 1, 1 };
    float dnEnvAtk = 0, dnEnvRel = 0, dnOpenCoef = 0, dnCloseCoef = 0;
    float dnFloorRise = 1.0f;       // adaptive: slow upward drift per sample
    bool  dnLearned = false;
    std::atomic<int> learnCountdown { 0 };

    // ---- Sustain (のび): tail lift + even-harmonic generation ----
    float susEnv = 0.0f;
    float susEnvAtk = 0, susEnvRel = 0;
    float susLift = 0.0f;           // smoothed lift gain (dB)

    // Doubler / width (mono-safe)
    std::vector<float> dblBuf;
    int dblWrite = 0;
    float dblLfoPhase = 0.0f;

    // Delay (simple echo, zero latency feed)
    std::vector<float> dlyBufL, dlyBufR;
    int dlyWrite = 0;

    // Tuner ring
    float            tunerBuf[tunerSize] = {};
    std::atomic<int> tunerPos { 0 };

    // Meters
    std::atomic<float> meterIn { 0 }, meterOut { 0 }, meterGR { 0 }, meterDS { 0 }, meterDN { 0 };

    void updateParameters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalGzzioProcessor)
};
