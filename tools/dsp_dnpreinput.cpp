#include "TestPaths.h"
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <memory>

// 前段のゲートで測定対象を消すと、古い学習済み表示のまま除去不能になる。
// 学習をやり直す時と自動追従の両方を、保存・再起動を含め実出力で検査する。
static int failures = 0;
#define CHECK(ok, ...) do { const bool pass = (ok); std::printf (pass ? "合格: " : "不合格: "); \
    std::printf (__VA_ARGS__); std::printf ("\n"); if (!pass) ++failures; } while (false)

static void setP (VocalGzzioProcessor& p, const char* id, float value)
{
    auto* parameter = p.apvts.getParameter (id);
    parameter->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (value));
}

static void setup (VocalGzzioProcessor& p, double sr, int block, int channels)
{
    auto layout = p.getBusesLayout();
    layout.inputBuses.set (0, channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    layout.outputBuses.set (0, layout.inputBuses[0]);
    p.setBusesLayout (layout);
    for (int m = 0; m < gz::ModuleChain::Count; ++m)
        setP (p, gz::ModuleChain::paramId (m), m == gz::ModuleChain::Souji ? 1.0f : 0.0f);
    for (auto* id : { "gate_on", "hum_amt", "pop_amt", "lip_amt", "prox_amt", "in_gain", "makeup", "dn_relearn", "crush_on" })
        setP (p, id, 0);
    setP (p, "src_mode", 0); setP (p, "mix", 100);
    setP (p, "dn_on", 1); setP (p, "denoise", 100);
    p.clearDenoiseLearn();
    p.prepareToPlay (sr, block);
}

static double run (VocalGzzioProcessor& p, double sr, int block, int channels,
                   float noiseDb, double seconds, bool learn = false)
{
    unsigned random = 981723;
    juce::AudioBuffer<float> buffer (channels, block);
    juce::MidiBuffer midi;
    const int total = (int) (sr * seconds);
    const float amplitude = juce::Decibels::decibelsToGain (noiseDb) * std::sqrt (3.0f);
    double input = 0, output = 0;
    bool requested = false;
    for (int start = 0; start < total; start += block)
    {
        if (learn && !requested && start >= sr * 0.6) { p.requestDenoiseLearn(); requested = true; }
        const int count = juce::jmin (block, total - start);
        buffer.setSize (channels, count, false, false, true);
        for (int n = 0; n < count; ++n)
        {
            random = random * 1664525u + 1013904223u;
            const float x = amplitude * ((float) (random >> 8) / 8388608.0f - 1.0f);
            for (int ch = 0; ch < channels; ++ch) buffer.setSample (ch, n, x);
            if (start + n >= total - sr) input += (double) x * x;
        }
        p.processBlock (buffer, midi);
        for (int n = 0; n < count; ++n)
            if (start + n >= total - sr) output += std::pow ((double) buffer.getSample (0, n), 2);
    }
    return 10 * std::log10 ((output + 1e-30) / (input + 1e-30));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
        for (const bool automatic : { false, true })
        {
            gz::dataDirectory().getChildFile ("autosave.xml").deleteFile();
            auto p = std::make_unique<VocalGzzioProcessor>();
            const int channels = sr == 44100 ? 1 : 2, block = sr == 48000 ? 512 : 127;
            setup (*p, sr, block, channels);
            run (*p, sr, block, channels, -66, 4, true);
            CHECK (p->isDenoiseLearned(), "最初の静かな環境を学習済み %.0f Hz", sr);
            setP (*p, "gate_on", 1); setP (*p, "gate", -20);
            setP (*p, "dn_relearn", automatic ? 1.0f : 0.0f);
            run (*p, sr, block, channels, -34, 10, !automatic);
            if (!automatic)
                CHECK (p->getDenoiseLearnResult() == 1, "ゲートが閉じていても再学習が成功 %.0f Hz（結果%d）", sr, p->getDenoiseLearnResult());
            // 自動追従を止め、ゲートを開く。学習が済んでいれば直ちに効く。
            setP (*p, "dn_relearn", 0); setP (*p, "gate_on", 0);
            const double reduction = run (*p, sr, block, channels, -34, 2);
            CHECK (reduction < -8, "%s・ゲート解除後もノイズ自体を除去 %.0f Hz（%.2f dB）",
                   automatic ? "自動" : "手動", sr, reduction);
            juce::MemoryBlock state;
            p->getStateInformation (state);
            p.reset();
            p = std::make_unique<VocalGzzioProcessor>();
            setup (*p, sr, block, channels);
            p->setStateInformation (state.getData(), (int) state.getSize());
            const double restored = run (*p, sr, block, channels, -34, 2);
            CHECK (p->isDenoiseLearned() && restored < -8,
                   "%s・保存して起動し直しても除去 %.0f Hz（%.2f dB）", automatic ? "自動" : "手動", sr, restored);
        }
    return failures == 0 ? 0 : 1;
}
