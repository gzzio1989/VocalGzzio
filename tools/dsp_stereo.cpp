// 「のび」で左右がずれないことを、実際のプラグイン処理で確認する。
#include "PluginProcessor.h"
#include "TestPaths.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#if ! VOCALGZZIO_TESTING
 #error この検査は設定を隔離した検証用ビルドで実行してください。
#endif

using Stereo = std::array<std::vector<float>, 2>;
static int failures = 0;

static void check (bool ok, const char* text, double value)
{
    std::printf ("%s: %s = %.9g\n", ok ? "合格" : "不合格", text, value);
    if (! ok) ++failures;
}

static void set (VocalGzzioProcessor& p, const char* id, float value)
{
    auto* parameter = p.apvts.getParameter (id);
    jassert (parameter != nullptr);
    parameter->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (value));
}

// mode: 0=左右同じ、1=左だけ、2=右だけ、3=逆相。尾部の音量変化を繰り返す。
static Stereo render (double rate, int blockSize, int mode, float amount)
{
    VocalGzzioProcessor p;
    for (auto* parameter : p.getParameters())
        parameter->setValueNotifyingHost (parameter->getDefaultValue());
    for (int m = 0; m < gz::ModuleChain::Count; ++m)
        set (p, gz::ModuleChain::paramId (m), m == gz::ModuleChain::Neiro ? 1.0f : 0.0f);
    for (auto id : { "presence", "air", "drive", "makeup", "ring", "br_amt", "cons_amt" })
        set (p, id, 0.0f);
    set (p, "sustain", amount);
    set (p, "mix", 100.0f);
    p.prepareToPlay (rate, blockSize);

    const int total = (int) rate * 2;
    Stereo result;
    for (auto& channel : result) channel.resize ((size_t) total);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    // 他のモジュールの切替フェードが終了してから、のび単体を比較する。
    for (int samples = 0; samples < (int) (rate * 0.03); samples += blockSize)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
    }
    for (int offset = 0; offset < total; offset += blockSize)
    {
        const int length = juce::jmin (blockSize, total - offset);
        buffer.setSize (2, length, false, false, true);
        for (int i = 0; i < length; ++i)
        {
            const double t = (offset + i) / rate;
            const double envelope = 0.006 + 0.15 * std::exp (-12.0 * std::fmod (t, 0.4));
            const float x = (float) (envelope * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t));
            buffer.setSample (0, i, mode == 2 ? 0.0f : x);
            buffer.setSample (1, i, mode == 1 ? 0.0f : mode == 3 ? -x : x);
        }
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < length; ++i)
                result[(size_t) ch][(size_t) offset + (size_t) i] = buffer.getSample (ch, i);
    }
    p.releaseResources();
    return result;
}

static double difference (const std::vector<float>& a, const std::vector<float>& b)
{
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (! std::isfinite (a[i]) || ! std::isfinite (b[i])) return 1.0e9;
        worst = juce::jmax (worst, std::abs ((double) a[i] - b[i]));
    }
    return worst;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf ("\n標本化周波数 %.0f\n", rate);
        const auto mono = render (rate, 512, 0, 100.0f);
        const auto small = render (rate, 64, 0, 100.0f);
        const auto left = render (rate, 512, 1, 100.0f);
        const auto right = render (rate, 512, 2, 100.0f);
        const auto opposite = render (rate, 512, 3, 100.0f);
        const auto bypass = render (rate, 512, 0, 0.0f);
        const auto zero = std::vector<float> (mono[0].size(), 0.0f);
        auto error = difference (mono[0], mono[1]);
        check (error < 1.0e-6, "同じ左右入力の最大差", error);
        error = difference (left[0], right[1]);
        check (error < 1.0e-6, "左右を交換したときの最大差", error);
        error = difference (mono[0], opposite[0]);
        check (error < 1.0e-6, "逆相入力でも検出量が変わらない", error);
        error = juce::jmax (difference (left[1], zero), difference (right[0], zero));
        check (error < 1.0e-7, "入力のない側への漏れ", error);
        error = difference (mono[0], small[0]);
        check (error < 1.0e-5, "処理単位を変えたときの最大差", error);
        error = difference (mono[0], bypass[0]);
        check (error > 0.005 && error < 1.0, "のびの効果が実際に掛かる", error);
    }
    return failures == 0 ? 0 : 1;
}
