#include "TestPaths.h"
// dsp_rates.cpp — 売り物としての土台検査①: サンプルレートと多重起動。
//
//  開発機の検査はずっと 44.1kHz だけで回してきた。しかし現実の環境は
//   ・OBS / 配信ソフト … 48kHz が既定
//   ・音にこだわる録音 … 96kHz / 192kHz
//  で、フィルタ係数・包絡の時定数・FFT まわりが 44.1kHz 前提のままだと
//  「うちの環境だと音が変」という**買った人にしか見えない不具合**になる。
//
//  確かめること（各レート 44.1k / 48k / 96k / 192k）:
//   [1] 全部入りで5秒流して、NaN/Inf が出ない・音量が暴れない
//   [2] 申告遅延が 0 のまま（このプラグインの看板）
//   [3] ノイズ除去が 48kHz でも同じように効く（時定数がレート換算されているか）
//   [4] 同じプロセスに2つ同時に挿しても混ざらない（static な状態の混線検査。
//       DAWでは当たり前の使い方だが、テストでは一度もやっていなかった）
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <vector>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

static void fullSetup (VocalGzzioProcessor& p)
{
    setP (p, "dn_on", 1); setP (p, "denoise", 50);
    setP (p, "ds_on", 1); setP (p, "deess", 60);
    setP (p, "comp1", 50); setP (p, "comp2", 55);
    setP (p, "revon", 1); setP (p, "revmix", 35); setP (p, "revsize", 40);
    setP (p, "doubler", 25); setP (p, "width", 35); setP (p, "delay", 20);
    setP (p, "res_amt", 45); setP (p, "cons_amt", 40); setP (p, "hum_amt", 100);
    setP (p, "dn_relearn", 0); setP (p, "mix", 100);
}

struct Voice
{
    double ph = 0.0; unsigned s = 7u;
    float next (double sr)
    {
        double v = 0.0;
        for (int h = 1; h <= 12; ++h) v += std::sin (ph * h) / h;
        ph += 2.0 * juce::MathConstants<double>::pi * 220.0 / sr;
        s = s * 1664525u + 1013904223u;
        return (float) (0.1 * v) + (float) ((int) (s >> 9) - 4194304) / 4194304.0f * 0.002f;
    }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_rates");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } rst { autosave, backup };
    autosave.deleteFile();

    std::printf ("サンプルレート4種と、2つ同時挿しの検査\n");

    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    const int bs = 512;

    // ---------------------------------------------------------------- [1][2]
    for (double sr : rates)
    {
        std::printf ("\n[%g Hz]\n", sr);
        VocalGzzioProcessor p; fullSetup (p);
        p.prepareToPlay (sr, bs);
        CHECK (p.getLatencySamples() == 0, "申告遅延 0 サンプル (%d)", p.getLatencySamples());

        juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer midi;
        Voice v;
        long bad = 0; double peak = 0.0, rmsAcc = 0.0; long n = 0;
        const long total = (long) (sr * 5.0);
        for (long done = 0; done < total; done += bs)
        {
            for (int i = 0; i < bs; ++i)
            { const float x = v.next (sr); buf.setSample (0, i, x); buf.setSample (1, i, x); }
            p.processBlock (buf, midi);
            for (int i = 0; i < bs; ++i)
            {
                const float y = buf.getSample (0, i);
                if (! std::isfinite (y)) ++bad;
                peak = juce::jmax (peak, (double) std::abs (y));
                if ((double) done / sr > 1.0) { rmsAcc += (double) y * y; ++n; }
            }
        }
        const double rmsDb = 10.0 * std::log10 (rmsAcc / juce::jmax (1L, n) + 1e-20);
        CHECK (bad == 0, "NaN/Inf なし (%ld)", bad);
        CHECK (peak < 4.0, "音量が暴れない (山 %.2f)", peak);
        CHECK (rmsDb > -40.0 && rmsDb < 0.0, "音が出ている (RMS %.1f dBFS)", rmsDb);
    }

    // ---------------------------------------------------------------- [3]
    std::printf ("\n[3] ノイズ除去(LEARN済み)の効きが 44.1k と 48k で同等か\n");
    {
        double atten[2] = { 0, 0 }; int idx = 0;
        for (double sr : { 44100.0, 48000.0 })
        {
            VocalGzzioProcessor p;
            using M = gz::ModuleChain;
            for (int m : { (int) M::Henshin, (int) M::Totonoe, (int) M::Soroe,
                           (int) M::Sagyo, (int) M::Neiro, (int) M::Chara, (int) M::Hirogari })
                setP (p, M::paramId (m), 0.0f);
            setP (p, "dn_on", 1); setP (p, "denoise", 60); setP (p, "mix", 100);
            setP (p, "hum_amt", 0); setP (p, "dn_relearn", 0);
            p.prepareToPlay (sr, bs);
            juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer midi;
            unsigned s = 5u; float lp = 0;
            // まず部屋を1秒鳴らして LEARN（実際の使い方と同じ）
            {
                for (long done = 0; done < (long) sr; done += bs)
                {
                    for (int i = 0; i < bs; ++i)
                    {
                        s = s * 1664525u + 1013904223u;
                        const float w = (float) ((int) (s >> 9) - 4194304) / 4194304.0f;
                        lp += 0.25f * (w - lp);
                        const float x = lp * 2.0f * juce::Decibels::decibelsToGain (-55.0f);
                        buf.setSample (0, i, x); buf.setSample (1, i, x);
                    }
                    p.processBlock (buf, midi);
                }
                p.requestDenoiseLearn();
                while (p.isDenoiseLearning())
                {
                    for (int i = 0; i < bs; ++i)
                    {
                        s = s * 1664525u + 1013904223u;
                        const float w = (float) ((int) (s >> 9) - 4194304) / 4194304.0f;
                        lp += 0.25f * (w - lp);
                        const float x = lp * 2.0f * juce::Decibels::decibelsToGain (-55.0f);
                        buf.setSample (0, i, x); buf.setSample (1, i, x);
                    }
                    p.processBlock (buf, midi);
                }
            }
            double inSq = 0, outSq = 0; long n = 0;
            const long total = (long) (sr * 8.0);
            for (long done = 0; done < total; done += bs)
            {
                std::vector<float> in ((size_t) bs);
                for (int i = 0; i < bs; ++i)
                {
                    s = s * 1664525u + 1013904223u;
                    const float w = (float) ((int) (s >> 9) - 4194304) / 4194304.0f;
                    lp += 0.25f * (w - lp);
                    in[(size_t) i] = lp * 2.0f * juce::Decibels::decibelsToGain (-55.0f);
                    buf.setSample (0, i, in[(size_t) i]); buf.setSample (1, i, in[(size_t) i]);
                }
                p.processBlock (buf, midi);
                for (int i = 0; i < bs; ++i)
                    if ((double) done / sr > 3.0)
                    { inSq += (double) in[(size_t) i] * in[(size_t) i];
                      outSq += (double) buf.getSample (0, i) * buf.getSample (0, i); ++n; }
            }
            atten[idx++] = 10.0 * std::log10 ((outSq + 1e-20) / (inSq + 1e-20));
        }
        std::printf ("  44.1k %.1f dB / 48k %.1f dB\n", atten[0], atten[1]);
        CHECK (atten[0] < -6.0, "44.1k で効いている (%.1f dB)", atten[0]);
        CHECK (std::abs (atten[0] - atten[1]) < 3.0,
               "48k でも同等 (差 %.1f dB)", std::abs (atten[0] - atten[1]));
    }

    // ---------------------------------------------------------------- [4]
    std::printf ("\n[4] 2つ同時に挿しても混ざらない\n");
    {
        VocalGzzioProcessor a, b; fullSetup (a); fullSetup (b);
        a.prepareToPlay (44100.0, bs); b.prepareToPlay (44100.0, bs);
        juce::AudioBuffer<float> bufA (2, bs), bufB (2, bs); juce::MidiBuffer midi;
        Voice va;
        double bLeak = 0.0; long bad = 0;
        for (long done = 0; done < (long) (44100.0 * 3.0); done += bs)
        {
            for (int i = 0; i < bs; ++i)
            {
                const float x = va.next (44100.0);
                bufA.setSample (0, i, x); bufA.setSample (1, i, x);   // A: 声
                bufB.setSample (0, i, 0.0f); bufB.setSample (1, i, 0.0f); // B: 無音
            }
            a.processBlock (bufA, midi);
            b.processBlock (bufB, midi);
            for (int i = 0; i < bs; ++i)
            {
                if (! std::isfinite (bufA.getSample (0, i)) || ! std::isfinite (bufB.getSample (0, i))) ++bad;
                bLeak = juce::jmax (bLeak, (double) std::abs (bufB.getSample (0, i)));
            }
        }
        CHECK (bad == 0, "NaN/Inf なし (%ld)", bad);
        CHECK (bLeak < 1.0e-6, "無音側に声が漏れない (漏れ %.2e)", bLeak);
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
