// 主画面の仕上がりを音声処理へ通し、音量差だけではない違いを検査する。
#include "PluginProcessor.h"
#include "PresetDefs.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#if ! VOCALGZZIO_TESTING
 #error この検査は設定を隔離した検証用ビルドで実行してください。
#endif

namespace
{
int failures = 0;
constexpr double pi = juce::MathConstants<double>::pi;
using Signal = std::vector<float>;
void check (bool ok, const char* what, double value = 0.0)
{
    std::printf ("%s: %s = %.6f\n", ok ? "合格" : "不合格", what, value);
    if (! ok) ++failures;
}
void set (VocalGzzioProcessor& p, const char* id, float value)
{
    auto* parameter = p.apvts.getParameter (id);
    if (parameter == nullptr) { check (false, id); return; }
    const auto range = p.apvts.getParameterRange (id);
    if (value < range.start || value > range.end) check (false, id, value);
    parameter->setValueNotifyingHost (range.convertTo0to1 (value));
}
float get (VocalGzzioProcessor& p, const char* id)
{
    return p.apvts.getRawParameterValue (id)->load();
}
Signal makeVoice (double rate)
{
    // 母音の倍音、息の帯域、20 dB の大小、語尾と無音を含む。
    // 実録音の聴感評価の代わりではなく、処理の実効性を測る試験音。
    Signal x ((size_t) (rate * 5.0), 0.0f);
    double phase = 0.0;
    for (size_t i = 0; i < (size_t) (rate * 3.0); ++i)
    {
        const double t = (double) i / rate;
        const double f = t < 1.5 ? 165.0 : 245.0;
        phase += 2.0 * pi * f / rate;
        const double inSyllable = std::fmod (t, 0.5);
        const double fade = std::min (1.0, inSyllable / 0.012)
                          * std::min (1.0, (0.5 - inSyllable) / 0.035);
        const double level = ((int) (t / 0.5) % 2) == 0 ? 0.35 : 0.035;
        double voice = 0.0;
        for (int h = 1; h <= 45; ++h)
        {
            const double hz = f * h;
            const double formant = 0.20 + 0.65 * std::exp (-std::pow ((hz - 700.0) / 350.0, 2))
                                       + 0.55 * std::exp (-std::pow ((hz - 2700.0) / 700.0, 2));
            voice += formant / std::pow ((double) h, 0.9) * std::sin (h * phase + h * h * 0.14);
        }
        voice += 0.10 * std::sin (2.0 * pi * 6137.0 * t) * std::sin (2.0 * pi * 1371.0 * t);
        x[i] = (float) (voice * level * fade);
    }
    return x;
}
Signal render (const Signal& input, double rate, int blockSize, int preset,
               bool noSpace = false, int source = 0)
{
    VocalGzzioProcessor p;
    for (auto* parameter : p.getParameters())
        parameter->setValueNotifyingHost (parameter->getDefaultValue());
    set (p, "src_mode", (float) source);
    set (p, "in_gain", 0); set (p, "makeup", 0);
    set (p, "gate_on", 0); set (p, "dn_on", 0); set (p, "dn_relearn", 0);
    set (p, "mod_henshin", 0);
    gzzio::applyFinishPreset (preset, [&] (const char* id, float value) { set (p, id, value); });
    if (noSpace) { set (p, "revon", 0); set (p, "dly_on", 0); }
    p.prepareToPlay (rate, blockSize);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int n = 0; n < (int) (rate * 0.05); n += blockSize)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
    }
    Signal output (input.size());
    for (size_t offset = 0; offset < input.size(); offset += (size_t) blockSize)
    {
        const int length = juce::jmin (blockSize, (int) (input.size() - offset));
        buffer.setSize (2, length, false, false, true);
        for (int ch = 0; ch < 2; ++ch) buffer.copyFrom (ch, 0, input.data() + offset, length);
        p.processBlock (buffer, midi);
        for (int i = 0; i < length; ++i) output[offset + (size_t) i] = buffer.getSample (0, i);
    }
    p.releaseResources();
    return output;
}
double rms (const Signal& a, size_t start, size_t end)
{
    double sum = 0;
    for (size_t i = start; i < end; ++i) sum += (double) a[i] * a[i];
    return std::sqrt (sum / (double) (end - start));
}
double db (double x) { return 20.0 * std::log10 (std::max (1.0e-12, x)); }
double equalLevelDifference (const Signal& a, const Signal& b, size_t n)
{
    // 単なる音量違いなら、実効音量を一致させた差はゼロになる。
    const double aRms = rms (a, 0, n), bRms = rms (b, 0, n);
    double sum = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double d = a[i] / aRms - b[i] / bRms;
        sum += d * d;
    }
    return std::sqrt (sum / (double) n);
}
double amplitude (const Signal& a, double rate, double hz)
{
    double re = 0, im = 0;
    const auto start = (size_t) rate, end = (size_t) (rate * 2.0);
    for (size_t i = start; i < end; ++i)
    {
        const double phase = 2.0 * pi * hz * (double) i / rate;
        re += a[i] * std::cos (phase); im += a[i] * std::sin (phase);
    }
    return std::sqrt (re * re + im * im) / (double) (end - start);
}
void checkApplication()
{
    VocalGzzioProcessor p;
    const std::map<std::string, float> preserve = {
        { "in_gain", -7 }, { "makeup", 4 }, { "denoise", 43 }, { "gate", -57 },
        { "src_mode", 3 }, { "at_key", 7 }, { "at_amount", 64 }, { "at_on", 1 },
        { "refpitch", 442 }, { "jn_on", 1 }, { "jn_mix", 25 }
    };
    for (const auto& v : preserve) set (p, v.first.c_str(), v.second);
    bool deterministic = true, retained = true;
    for (int recipe = 0; recipe < gzzio::kNumFinishPresets; ++recipe)
    {
        std::map<std::string, float> expected;
        gzzio::applyFinishPreset (recipe, [&] (const char* id, float value) { expected[id] = value; });
        for (int previous = 0; previous < gzzio::kNumFinishPresets; ++previous)
        {
            gzzio::applyFinishPreset (previous, [&] (const char* id, float value) { set (p, id, value); });
            // モジュール無効やMix=0からでも、新しい仕上がりが実際に有効になる。
            for (const auto& v : expected) set (p, v.first.c_str(), p.apvts.getParameterRange (v.first).start);
            gzzio::applyFinishPreset (recipe, [&] (const char* id, float value) { set (p, id, value); });
            for (const auto& v : expected) deterministic &= std::abs (get (p, v.first.c_str()) - v.second) < 0.11f;
            for (const auto& v : preserve) retained &= std::abs (get (p, v.first.c_str()) - v.second) < 0.11f;
        }
    }
    check (deterministic, "前の仕上がりや無効状態によらず指定値になる");
    check (retained, "入力・出力・用途・ノイズ設定・音程設定を保持する");
    int calls = 0;
    const auto invalid = [&] (const char*, float) { ++calls; };
    check (! gzzio::applyFinishPreset (-1, invalid)
        && ! gzzio::applyFinishPreset (gzzio::kNumFinishPresets, invalid) && calls == 0,
        "範囲外の選択は音を変えない");
}
}
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    checkApplication();
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf ("\n標本化周波数 %.0f\n", rate);
        const auto voice = makeVoice (rate);
        std::array<Signal, gzzio::kNumFinishPresets> results;
        for (int i = 0; i < gzzio::kNumFinishPresets; ++i)
        {
            results[(size_t) i] = render (voice, rate, 256, i);
            bool finite = true; float peak = 0;
            for (float v : results[(size_t) i]) { finite &= std::isfinite (v); peak = std::max (peak, std::abs (v)); }
            std::printf ("仕上がり %d %s / 実効音量 %.2f dBFS\n", i + 1,
                         gzzio::kFinishPresets[i].name, db (rms (results[(size_t) i], 0, (size_t) (rate * 3))));
            check (finite && peak < 0.98f && peak > 0.005f, "有限値・過大出力・無音化", peak);
        }
        double closest = 1.0e9;
        for (int a = 0; a < gzzio::kNumFinishPresets; ++a)
            for (int b = a + 1; b < gzzio::kNumFinishPresets; ++b)
            {
                const double diff = equalLevelDifference (results[(size_t) a], results[(size_t) b], (size_t) (rate * 3));
                std::printf ("  %d / %d 音量を揃えた差 %.4f\n", a + 1, b + 1, diff);
                closest = std::min (closest, diff);
            }
        check (closest > 0.08, "全15組で音量だけではない差が残る", closest);
        const auto tail = [&] (const Signal& y)
        { return db (rms (y, (size_t) (rate * 3.5), (size_t) (rate * 4.5))
                   / rms (y, (size_t) (rate * 2), (size_t) (rate * 3))); };
        const double naturalTail = tail (results[0]), wideTail = tail (results[5]);
        check (wideTail - naturalTail > 12.0, "広い響きは自然な部屋より長い余韻が残る", wideTail - naturalTail);
        check (rms (results[4], (size_t) (rate * 3.5), (size_t) (rate * 4.5)) < 1.0e-6,
               "語りの仕上がりは残響を残さない");
        // 小振幅で動的処理を避け、音色の狙いを単独で確認する。
        Signal tones ((size_t) (rate * 2), 0.0f);
        for (size_t i = 0; i < tones.size(); ++i)
            for (double f : { 165.0, 330.0, 990.0, 3300.0, 7920.0 })
                tones[i] += (float) (0.001 * std::sin (2.0 * pi * f * (double) i / rate));
        const auto forward = render (tones, rate, 256, 1, true);
        const auto warm = render (tones, rate, 256, 2, true);
        const auto bright = [&] (const Signal& y)
        { return db (amplitude (y, rate, 3300) / amplitude (y, rate, 330)); };
        check (bright (forward) - bright (warm) > 5.0,
               "歌詞を前へは丸く親密により中高音が前へ出る", bright (forward) - bright (warm));
        const auto naturalDry = render (voice, rate, 256, 0, true);
        const auto powerDry = render (voice, rate, 256, 3, true);
        const auto range = [&] (const Signal& y)
        { return db (rms (y, (size_t) (rate * 0.20), (size_t) (rate * 0.40))
                   / rms (y, (size_t) (rate * 0.70), (size_t) (rate * 0.90))); };
        check (range (naturalDry) - range (powerDry) > 3.0,
               "太く力強くは自然な仕上がりより大小の差を縮める", range (naturalDry) - range (powerDry));
    }
    return failures == 0 ? 0 : 1;
}
