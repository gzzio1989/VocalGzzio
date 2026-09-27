#include "TestPaths.h"
#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <cstdio>
#include <thread>

// 本体を通して測る。分割器だけの模型では、原音混合による打ち消しを見逃す。
static int failures = 0;
#define CHECK(ok, ...) do { std::printf ((ok) ? "合格: " : "不合格: "); \
    std::printf (__VA_ARGS__); std::printf ("\n"); if (!(ok)) ++failures; } while (false)

static void setP (VocalGzzioProcessor& p, const char* id, float value)
{
    if (auto* parameter = p.apvts.getParameter (id))
        parameter->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (value));
}

static void setup (VocalGzzioProcessor& p, double sr, int block, float mix = 100)
{
    for (int m = 0; m < gz::ModuleChain::Count; ++m)
        setP (p, gz::ModuleChain::paramId (m), m == gz::ModuleChain::Souji ? 1.0f : 0.0f);
    for (auto* id : { "gate_on", "hum_amt", "pop_amt", "lip_amt", "prox_amt",
                     "in_gain", "makeup", "dn_relearn", "crush_on" })
        setP (p, id, 0);
    setP (p, "src_mode", 0);
    setP (p, "mix", mix);
    setP (p, "dn_on", 1);
    setP (p, "denoise", 60);
    p.clearDenoiseLearn();
    p.prepareToPlay (sr, block);
}

static void fresh()
{
    gz::dataDirectory().getChildFile ("autosave.xml").deleteFile();
}

static double dbRatio (double numerator, double denominator)
{
    return 10.0 * std::log10 ((numerator + 1e-30) / (denominator + 1e-30));
}

static double toneGain (double sr, int block, float mix, double hz)
{
    fresh();
    VocalGzzioProcessor p;
    setup (p, sr, block, mix);
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    double inputEnergy = 0, outputEnergy = 0;
    const int total = (int) sr;
    for (int start = 0; start < total; start += block)
    {
        const int count = juce::jmin (block, total - start);
        buffer.setSize (2, count, false, false, true);
        for (int i = 0; i < count; ++i)
        {
            const float x = 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (start + i) / sr);
            buffer.setSample (0, i, x);
            buffer.setSample (1, i, x);
            if (start + i >= total / 2) inputEnergy += (double) x * x;
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < count; ++i)
            if (start + i >= total / 2)
            {
                const double x = buffer.getSample (0, i);
                outputEnergy += x * x;
            }
    }
    return dbRatio (outputEnergy, inputEnergy);
}

struct Noise
{
    unsigned state = 7831;
    float next()
    {
        state = state * 1664525u + 1013904223u;
        return ((float) (state >> 8) / 8388608.0f - 1.0f) * 0.002f;
    }
};

struct HighBand
{
    juce::dsp::IIR::Filter<float> a, b;
    HighBand()
    {
        a.coefficients = b.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (48000, 6500, 0.7071f);
        a.prepare ({ 48000, 1, 1 }); b.prepare ({ 48000, 1, 1 });
    }
    float next (float x) { return b.processSample (a.processSample (x)); }
};

static void noisePumping()
{
    fresh();
    constexpr int block = 128, sr = 48000;
    VocalGzzioProcessor p;
    setup (p, sr, block);
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    Noise noise;
    HighBand inHigh, outHigh;
    double inEnergy[2] {}, outEnergy[2] {};
    juce::AudioBuffer<float> before (1, sr * 6), after (1, sr * 6);
    // 最初の3秒は学習、その後1秒の休みと1秒の母音を3回繰り返す。
    for (int start = 0; start < sr * 9; start += block)
    {
        if (start == 24064) p.requestDenoiseLearn();
        float inputs[block] {};
        for (int i = 0; i < block; ++i)
        {
            const double time = (start + i) / (double) sr;
            const bool voice = time >= 3 && ((int) time & 1) == 0;
            float x = noise.next();
            if (voice)
                x += 0.07f * (float) std::sin (2 * juce::MathConstants<double>::pi * 250 * time)
                   + 0.035f * (float) std::sin (2 * juce::MathConstants<double>::pi * 500 * time);
            inputs[i] = x;
            buffer.setSample (0, i, x); buffer.setSample (1, i, x);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < block; ++i)
        {
            const double time = (start + i) / (double) sr;
            const double highIn = inHigh.next (inputs[i]);
            const double highOut = outHigh.next (buffer.getSample (0, i));
            const int recordIndex = start + i - sr * 3;
            if (recordIndex >= 0 && recordIndex < before.getNumSamples())
            {
                // 比較用の合成音。両方を同じ約12dBだけ持ち上げて聴きやすくする。
                before.setSample (0, recordIndex, inputs[i] * 4.0f);
                after.setSample (0, recordIndex, buffer.getSample (0, i) * 4.0f);
            }
            // 開始300msは過渡区間。声の有無によって定常ヒスが持ち上がらないか測る。
            if (time >= 3 && time - std::floor (time) > 0.3)
            {
                const int index = ((int) time & 1) == 0 ? 1 : 0;
                inEnergy[index] += highIn * highIn;
                outEnergy[index] += highOut * highOut;
            }
        }
    }
    const double quiet = dbRatio (outEnergy[0], inEnergy[0]);
    const double voice = dbRatio (outEnergy[1], inEnergy[1]);
    CHECK (p.isDenoiseLearned(), "測定前に部屋ノイズを学習できる");
    CHECK (quiet < -7, "無声区間の高域ノイズ抑制 %.2f dB", quiet);
    CHECK (voice < -7, "母音区間の高域ノイズ抑制 %.2f dB", voice);
    CHECK (voice - quiet < 2, "母音でノイズが戻る量 %.2f dB（許容2 dB未満）", voice - quiet);
    auto saveWave = [] (const char* filename, const juce::AudioBuffer<float>& samples)
    {
        const auto file = gz::dataDirectory().getChildFile (juce::String::fromUTF8 (filename));
        file.deleteFile();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
            file.createOutputStream().release(), 48000, 1, 24, {}, 0));
        if (writer != nullptr) writer->writeFromAudioSampleBuffer (samples, 0, samples.getNumSamples());
    };
    saveWave ("合成音_処理前.wav", before);
    saveWave ("合成音_ノイズ除去後.wav", after);
}

static void bypassAndChannels()
{
    for (int channels : { 1, 2 })
        for (bool denoiseOn : { false, true })
        {
            fresh();
            VocalGzzioProcessor p;
            if (channels == 1)
            {
                auto layout = p.getBusesLayout();
                layout.inputBuses.set (0, juce::AudioChannelSet::mono());
                layout.outputBuses.set (0, juce::AudioChannelSet::mono());
                CHECK (p.setBusesLayout (layout), "モノラル入出力を設定できる");
            }
            setup (p, 48000, 511, denoiseOn ? 0 : 50);
            setP (p, "dn_on", denoiseOn ? 1 : 0);
            juce::AudioBuffer<float> buffer (channels, 511);
            juce::MidiBuffer midi;
            Noise noise;
            float maxDifference = 0;
            for (int block = 0; block < 100; ++block)
            {
                float input[511] {};
                for (int i = 0; i < 511; ++i)
                {
                    input[i] = noise.next() * 30;
                    for (int ch = 0; ch < channels; ++ch) buffer.setSample (ch, i, input[i]);
                }
                p.processBlock (buffer, midi);
                if (block > 10)
                    for (int ch = 0; ch < channels; ++ch)
                        for (int i = 0; i < 511; ++i)
                            maxDifference = juce::jmax (maxDifference, std::abs (buffer.getSample (ch, i) - input[i]));
            }
            CHECK (maxDifference < 1e-7f, "%dチャンネル・%sの原音との差 %.3g",
                   channels, denoiseOn ? "原音混合0%" : "ノイズ除去無効", maxDifference);
        }

    fresh();
    VocalGzzioProcessor p;
    setup (p, 48000, 128, 50);
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    Noise noise;
    float maxDifference = 0;
    for (int block = 0; block < 2000; ++block)
    {
        if (block == 200) p.requestDenoiseLearn();
        if (block == 900) setP (p, "dn_on", 0);
        if (block == 1200) setP (p, "dn_on", 1);
        for (int i = 0; i < 128; ++i)
        {
            const float x = noise.next();
            buffer.setSample (0, i, x); buffer.setSample (1, i, -x);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < 128; ++i)
            maxDifference = juce::jmax (maxDifference, std::abs (buffer.getSample (0, i) + buffer.getSample (1, i)));
    }
    CHECK (maxDifference < 1e-7f, "学習・切替を含め、左右逆相の対称性を保つ（差 %.3g）", maxDifference);
}

static void repeatedLearn()
{
    fresh();
    VocalGzzioProcessor p;
    setup (p, 48000, 128);
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    Noise noise;
    std::atomic<bool> done { false };
    std::thread controls ([&]
    {
        for (int i = 0; i < 4000; ++i)
        {
            p.requestDenoiseLearn();
            if ((i % 3) == 0) p.clearDenoiseLearn();
            std::this_thread::yield();
        }
        done.store (true);
    });
    bool finite = true;
    int processed = 0;
    while (! done.load() || processed < 100)
    {
        for (int i = 0; i < 128; ++i)
        {
            const float x = noise.next();
            buffer.setSample (0, i, x); buffer.setSample (1, i, x);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < 128; ++i)
            finite = finite && std::isfinite (buffer.getSample (0, i));
        ++processed;
    }
    controls.join();
    p.clearDenoiseLearn();
    buffer.clear(); p.processBlock (buffer, midi);
    CHECK (finite && ! p.isDenoiseLearning() && ! p.isDenoiseLearned(),
           "学習と解除を別スレッドで連打しても正常終了し、最後の解除が反映される");
}

static void learnWhileBypassed()
{
    fresh();
    VocalGzzioProcessor p;
    setup (p, 48000, 128);
    setP (p, "mod_souji", 0);
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    Noise noise;
    float maxDifference = 0;
    for (int block = 0; block < 1000; ++block)
    {
        if (block == 100) p.requestDenoiseLearn();
        float inputs[128] {};
        for (int i = 0; i < 128; ++i)
        {
            inputs[i] = noise.next();
            buffer.setSample (0, i, inputs[i]);
            buffer.setSample (1, i, inputs[i]);
        }
        p.processBlock (buffer, midi);
        if (block >= 100)
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 128; ++i)
                    maxDifference = juce::jmax (maxDifference,
                        std::abs (buffer.getSample (ch, i) - inputs[i]));
    }
    CHECK (! p.isDenoiseLearning() && p.getDenoiseLearnResult() == 1,
           "おそうじ無効中でもノイズ学習が完了する");
    CHECK (maxDifference < 1e-7f,
           "おそうじ無効中の学習は原音を変えない（差 %.3g）", maxDifference);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 64, 511 })
            for (float mix : { 25.0f, 50.0f, 100.0f })
            {
                double worst = 0, worstHz = 0;
                for (double hz : { 100.0, 250.0, 500.0, 1200.0, 2500.0, 5000.0, 8000.0, 12000.0 })
                {
                    const double gain = toneGain (sr, block, mix, hz);
                    if (std::abs (gain) > std::abs (worst)) { worst = gain; worstHz = hz; }
                }
                CHECK (std::abs (worst) < 0.2,
                       "%.0f Hz・%dサンプル・加工音%.0f%%: 最悪の音量差 %.3f dB（%.0f Hz）",
                       sr, block, mix, worst, worstHz);
            }
    noisePumping();
    bypassAndChannels();
    repeatedLearn();
    learnWhileBypassed();
    fresh();
    std::printf ("検査失敗: %d件\n", failures);
    return failures == 0 ? 0 : 1;
}
