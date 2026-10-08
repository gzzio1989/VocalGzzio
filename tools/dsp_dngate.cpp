// 実機報告(2026-10-06): Win10 / FireFace UC / Cakewalk SONAR / スタンドアロン /
// 「ノイズ除去が動作しない」。4.2.1・4.3.0 とも同じ症状の修正を謳っているのに
// まだ報告が来る。＝ 既存の検査が実機の構成を再現できていない。
//
// 既存のノイズ除去検査は setup() で "gate_on" を 0 にし、2ch・既定ブロックで測る。
// ここでは出荷状態のまま、実機にありうる構成を総当たりする。
// ★規則15: 「効かない側」だけを見ない。必ず「効く側」と対で並べる。
#include "TestPaths.h"
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include <string>

static int failures = 0;
#define CHECK(ok, ...) do { std::printf ((ok) ? "合格: " : "不合格: "); \
    std::printf (__VA_ARGS__); std::printf ("\n"); if (!(ok)) ++failures; } while (false)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* q = p.apvts.getParameter (id))
        q->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}
struct Noise { unsigned s = 22222; float next() { s = s*1664525u+1013904223u; return (float)(s>>8)/8388608.0f-1.0f; } };

struct Cfg {
    std::string name;
    double sr; int block; int channels;
    bool gateOn; float gateThr;
    bool monoSource;    // 片chだけに音が入る（マイク1本をステレオ器に挿した状態）
};

static double measure (const Cfg& c)
{
    auto p = std::make_unique<VocalGzzioProcessor>();
    auto layout = p->getBusesLayout();
    layout.inputBuses.set (0, c.channels == 1 ? juce::AudioChannelSet::mono()
                                              : juce::AudioChannelSet::stereo());
    layout.outputBuses.set (0, layout.inputBuses[0]);
    p->setBusesLayout (layout);

    for (int m = 0; m < gz::ModuleChain::Count; ++m)
        setP (*p, gz::ModuleChain::paramId (m), m == gz::ModuleChain::Souji ? 1.0f : 0.0f);
    for (auto* id : { "hum_amt", "pop_amt", "lip_amt", "prox_amt", "in_gain", "makeup", "crush_on" })
        setP (*p, id, 0);
    setP (*p, "src_mode", 0); setP (*p, "mix", 100);
    setP (*p, "dn_on", 1); setP (*p, "denoise", 60); setP (*p, "dn_relearn", 1);
    setP (*p, "gate_on", c.gateOn ? 1.0f : 0.0f); setP (*p, "gate", c.gateThr);
    p->clearDenoiseLearn();
    p->prepareToPlay (c.sr, c.block);

    Noise noise; float pink[3] {};
    juce::AudioBuffer<float> buffer (c.channels, c.block);
    juce::MidiBuffer midi;
    const int total = (int) (c.sr * 25);
    const float amp = juce::Decibels::decibelsToGain (-48.0f) * std::sqrt (3.0f);
    double inE = 0, outE = 0;
    const double from = c.sr * 21, to = c.sr * 24;      // 最後の黙っている3秒

    for (int start = 0; start < total; start += c.block)
    {
        const int count = juce::jmin (c.block, total - start);
        buffer.setSize (c.channels, count, false, false, true);
        for (int i = 0; i < count; ++i)
        {
            const double n = start + i, t = n / c.sr;
            const float w = noise.next();
            pink[0] = 0.99765f*pink[0] + w*0.0990460f;
            pink[1] = 0.96300f*pink[1] + w*0.2965164f;
            pink[2] = 0.57000f*pink[2] + w*1.0526913f;
            float x = amp * (pink[0]+pink[1]+pink[2]+w*0.1848f) * 0.2f;
            const double phase = std::fmod (t, 4.0);
            if (t < 20.0 && phase < 1.5)
                for (int h = 1; h <= 6; ++h)
                    x += 0.05f * (float) std::sin (2*juce::MathConstants<double>::pi*160.0*h*t) / h;
            for (int ch = 0; ch < c.channels; ++ch)
                buffer.setSample (ch, i, (c.monoSource && ch == 1) ? 0.0f : x);
            if (n >= from && n < to) inE += (double) x * x;
        }
        p->processBlock (buffer, midi);
        for (int i = 0; i < count; ++i)
        {
            const double n = start + i;
            if (n >= from && n < to) { const double y = buffer.getSample (0, i); outE += y*y; }
        }
    }
    return 10 * std::log10 ((outE + 1e-30) / (inE + 1e-30));
}

int main()
{
    std::printf ("=== 実機にありうる構成の総当たり（出荷状態のまま）===\n");
    std::printf ("部屋 -48dBFS ピンクノイズ / ノイズ除去60%% / 学びなおしON / 25秒\n");
    std::printf ("最後の黙っている3秒の 出力÷入力。-6dB より下なら「効いている」\n\n");

    std::vector<Cfg> cfgs = {
      // 名前                              SR     blk ch  gate  thr   mono
      { "基準: 2ch 48k b128 ゲートOFF",   48000,  128, 2, false, -80, false },
      { "出荷既定: 2ch 48k b128 ゲートON",48000,  128, 2, true,  -80, false },
      { "FireFace 低遅延 b64",            48000,   64, 2, true,  -80, false },
      { "FireFace b32",                   48000,   32, 2, true,  -80, false },
      { "44.1k b256",                     44100,  256, 2, true,  -80, false },
      { "96k b128",                       96000,  128, 2, true,  -80, false },
      { "96k b64",                        96000,   64, 2, true,  -80, false },
      { "192k b128",                     192000,  128, 2, true,  -80, false },
      { "モノ入力 1ch 48k",               48000,  128, 1, true,  -80, false },
      { "★マイク1本を2chに挿した状態",    48000,  128, 2, true,  -80, true  },
      { "★同上・44.1k",                   44100,  128, 2, true,  -80, true  },
      { "★同上・96k",                     96000,  128, 2, true,  -80, true  },
    };

    for (const auto& c : cfgs)
    {
        const double d = measure (c);
        std::printf ("  %-34s : %8.2f dB\n", c.name.c_str(), d);
        CHECK (d < -6.0, "%s でノイズ除去が効く（%.2f dB < -6）", c.name.c_str(), d);
    }
    std::printf ("\n%s（不合格 %d 件）\n", failures ? "★NG" : "すべて合格", failures);
    return failures ? 1 : 0;
}
