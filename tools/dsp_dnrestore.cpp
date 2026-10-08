// 実機報告(2026-10-06): Win10 / FireFace UC / スタンドアロン / 4.3 で
// 「ノイズ除去が動作しない」。4.3 を直接渡した上での報告なので、
// 「直っているが未配布」ではない。
//
// 起動直後からやり直す検査(dsp_dnstartup/dsp_dngate)はすべて合格する。
// 違うのは「保存された設定を読み込んで始める」道。スタンドアロンは設定を
// ファイルに持つので、実機は必ずこの道を通る。
//
// setStateInformation の採用条件は「うるさすぎる床」しか弾いていない:
//     validFloor = isfinite && floor >= 1e-6f && floor <= 0.5f
//     if (!validFloor || (learned && validation < 1 && dB(loudest) > -45)) ...
// → **低すぎる床はそのまま採用される。** 床が低いと openThr = 床×2.5 が
//   本物のノイズより下に来て、エキスパンダーが一度も閉じない＝無処理。
// （2026-09-01 の「v4.0.1 でやる：ノイズ除去の根治」§B と同じ根っこ）
//
// ★規則15: 「効かない側」だけを見ない。効く側（やり直し）と対で測る。
#include "TestPaths.h"
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <memory>

static int failures = 0;
#define CHECK(ok, ...) do { std::printf ((ok) ? "合格: " : "不合格: "); \
    std::printf (__VA_ARGS__); std::printf ("\n"); if (!(ok)) ++failures; } while (false)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* q = p.apvts.getParameter (id))
        q->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}
struct Noise { unsigned s = 4242; float next() { s = s*1664525u+1013904223u; return (float)(s>>8)/8388608.0f-1.0f; } };

static void configure (VocalGzzioProcessor& p, bool relearn)
{
    auto layout = p.getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
    layout.outputBuses.set (0, layout.inputBuses[0]);
    p.setBusesLayout (layout);
    for (int m = 0; m < gz::ModuleChain::Count; ++m)
        setP (p, gz::ModuleChain::paramId (m), m == gz::ModuleChain::Souji ? 1.0f : 0.0f);
    for (auto* id : { "hum_amt", "pop_amt", "lip_amt", "prox_amt", "in_gain", "makeup", "crush_on" })
        setP (p, id, 0);
    setP (p, "src_mode", 0); setP (p, "mix", 100);
    setP (p, "dn_on", 1); setP (p, "denoise", 60);
    setP (p, "dn_relearn", relearn ? 1.0f : 0.0f);
    setP (p, "gate_on", 1); setP (p, "gate", -80.0f);
}

// 保存された設定を作る。storedFloor を 4 帯域ぶん書き込み、learned=true にする。
static juce::MemoryBlock makeSavedState (bool relearn, float storedFloor, int validation)
{
    auto p = std::make_unique<VocalGzzioProcessor>();
    configure (*p, relearn);
    juce::MemoryBlock mb;
    p->getStateInformation (mb);
    auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    if (xml == nullptr) return mb;
    auto* d = xml->getChildByName ("DENOISE");
    if (d == nullptr) d = xml->createNewChildElement ("DENOISE");
    d->setAttribute ("learned", true);
    d->setAttribute ("validation", validation);
    for (int b = 0; b < 4; ++b)
        d->setAttribute ("f" + juce::String (b), (double) storedFloor);
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary (*xml, out);
    return out;
}

// 部屋ノイズ -48dBFS を 25 秒。最後の黙っている 3 秒で 出力÷入力 を測る。
static double measure (bool relearn, const juce::MemoryBlock* saved)
{
    auto p = std::make_unique<VocalGzzioProcessor>();
    configure (*p, relearn);
    if (saved != nullptr) p->setStateInformation (saved->getData(), (int) saved->getSize());
    else                  p->clearDenoiseLearn();
    const double sr = 48000; const int block = 128;
    p->prepareToPlay (sr, block);

    Noise noise; float pink[3] {};
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    const int total = (int) (sr * 25);
    const float amp = juce::Decibels::decibelsToGain (-48.0f) * std::sqrt (3.0f);
    double inE = 0, outE = 0;
    const double from = sr * 21, to = sr * 24;
    for (int start = 0; start < total; start += block)
    {
        const int count = juce::jmin (block, total - start);
        buffer.setSize (2, count, false, false, true);
        for (int i = 0; i < count; ++i)
        {
            const double n = start + i, t = n / sr;
            const float w = noise.next();
            pink[0] = 0.99765f*pink[0] + w*0.0990460f;
            pink[1] = 0.96300f*pink[1] + w*0.2965164f;
            pink[2] = 0.57000f*pink[2] + w*1.0526913f;
            float x = amp * (pink[0]+pink[1]+pink[2]+w*0.1848f) * 0.2f;
            if (t < 20.0 && std::fmod (t, 4.0) < 1.5)
                for (int h = 1; h <= 6; ++h)
                    x += 0.05f * (float) std::sin (2*juce::MathConstants<double>::pi*160.0*h*t) / h;
            for (int ch = 0; ch < 2; ++ch) buffer.setSample (ch, i, x);
            if (n >= from && n < to) inE += (double) x * x;
        }
        p->processBlock (buffer, midi);
        for (int i = 0; i < count; ++i)
        { const double n = start + i;
          if (n >= from && n < to) { const double y = buffer.getSample (0, i); outE += y*y; } }
    }
    return 10 * std::log10 ((outE + 1e-30) / (inE + 1e-30));
}

int main()
{
    std::printf ("=== 保存された設定から始めたときのノイズ除去 ===\n");
    std::printf ("部屋 -48dBFS / 除去量60%% / 25秒 / 最後の黙っている3秒の 出力÷入力\n");
    std::printf ("-6dB より下なら「効いている」\n\n");

    // --- 対照: 保存を読まずにやり直し。ここは効かないといけない ---
    const double fresh = measure (true, nullptr);
    std::printf ("  [対照] 保存なし・やり直し            : %8.2f dB\n", fresh);
    CHECK (fresh < -6.0, "対照: 保存なしならノイズ除去は効く（%.2f dB）", fresh);

    // --- まともな床が保存されている場合 ---
    {
        auto st = makeSavedState (true, 4.0e-3f, 1);
        const double d = measure (true, &st);
        std::printf ("  まともな床(4.0e-3)を復元・学びなおしON: %8.2f dB\n", d);
        CHECK (d < -6.0, "まともな床を復元してもノイズ除去は効く（%.2f dB）", d);
    }

    // --- ★低すぎる床が保存されている場合（報告の再現を狙う） ---
    for (const float stored : { 1.0e-6f, 1.0e-5f })
    {
        {
            auto st = makeSavedState (true, stored, 1);
            const double d = measure (true, &st);
            std::printf ("  ★低い床(%.0e)を復元・学びなおしON   : %8.2f dB\n", stored, d);
            CHECK (d < -6.0, "低い床(%.0e)・学びなおしONでも効く（%.2f dB）", stored, d);
        }
        {
            auto st = makeSavedState (false, stored, 1);
            const double d = measure (false, &st);
            std::printf ("  ★低い床(%.0e)を復元・学びなおしOFF  : %8.2f dB\n", stored, d);
            CHECK (d < -6.0, "低い床(%.0e)・学びなおしOFFでも効く（%.2f dB）", stored, d);
        }
    }

    // --- 採用の門: 無入力で「ノイズを測る」を押しても採用しないこと ---
    //  ここが悪い床の発生源。FireFace UC のように入力が多い機材では、
    //  入力chの選び間違い・ミュートのまま押す、が普通に起きる。
    //  ★対になる確認: ちゃんと部屋の音が入っていれば採用されること。
    {
        auto press = [] (float noiseDb) {
            auto p = std::make_unique<VocalGzzioProcessor>();
            configure (*p, true);
            p->clearDenoiseLearn();
            const double sr = 48000; const int block = 128;
            p->prepareToPlay (sr, block);
            Noise noise; float pink[3] {};
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            const float amp = noiseDb <= -200.0f ? 0.0f
                            : juce::Decibels::decibelsToGain (noiseDb) * std::sqrt (3.0f);
            bool asked = false;
            for (int start = 0; start < (int) (sr * 6); start += block)
            {
                if (! asked && start >= sr * 2) { p->requestDenoiseLearn(); asked = true; }
                buffer.setSize (2, block, false, false, true);
                for (int i = 0; i < block; ++i)
                {
                    const float w = noise.next();
                    pink[0] = 0.99765f*pink[0] + w*0.0990460f;
                    pink[1] = 0.96300f*pink[1] + w*0.2965164f;
                    pink[2] = 0.57000f*pink[2] + w*1.0526913f;
                    const float x = amp * (pink[0]+pink[1]+pink[2]+w*0.1848f) * 0.2f;
                    for (int ch = 0; ch < 2; ++ch) buffer.setSample (ch, i, x);
                }
                p->processBlock (buffer, midi);
            }
            return p->getDenoiseLearnResult();
        };
        const int quiet = press (-500.0f);   // 完全な無入力
        const int room  = press (-48.0f);    // ふつうの部屋
        std::printf ("\n  無入力で「測る」を押した結果 : %d（1=採用 2=大きすぎ 3=測れず）\n", quiet);
        std::printf ("  部屋の音で「測る」を押した結果: %d\n", room);
        CHECK (quiet != 1, "無入力の学習は採用しない（結果=%d）", quiet);
        CHECK (room == 1, "対: ふつうの部屋の音なら採用する（結果=%d）", room);
    }

    std::printf ("\n%s（不合格 %d 件）\n", failures ? "★NG" : "すべて合格", failures);
    return failures ? 1 : 0;
}
