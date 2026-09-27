#include "TestPaths.h"
// dsp_dndrift.cpp — 「30分たつと同じ声でも出音が変わる」かどうかの、当てずっぽうでない検出。
//
//  ユーザ報告(2026-08-31):
//    ・ノイズ除去の「学習」は押した
//    ・10〜30分くらい経つと、喋りはじめに サー となってから消える
//
//  仮説を立てて狙い撃ちすると外す（実際 dsp_dnslow で2つ外した）。
//  そこで、**仮説を使わない**測りかたにする:
//
//    まったく同じ8秒（部屋ノイズ＋声）を、2分めと 28分めに1回ずつ流す。
//    乱数の種をその周期の頭で固定するので、入力は1サンプルも違わない。
//    それでも出力が違うなら、プラグインの中に「時間とともに変わる何か」がある。
//    違わないなら、時間で変わる物は無い ＝ 原因は外（ホスト・CPU・機材）にある。
//
//  どこが違うかも出す: 100msごとの差を並べれば、喋りはじめに集中しているか
//  ずっと違うのかが分かる。
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

static void flatten (VocalGzzioProcessor& p)
{
    for (const char* id : { "comp1", "comp2", "ride_amt", "res_amt", "deess",
                            "seq_amount", "drive", "sustain", "cons_amt", "br_amt",
                            "ring", "hum_amt", "denoise", "makeup", "prox_amt",
                            "doubler", "width", "delay", "revmix" })
        setP (p, id, 0.0f);
    setP (p, "mud", 0.0f);      setP (p, "harsh", 0.0f);
    setP (p, "air", 0.0f);      setP (p, "presence", 0.0f);
    setP (p, "seq_on", 0.0f);   setP (p, "revon", 0.0f);
    setP (p, "dly_on", 0.0f);   setP (p, "gate", -80.0f);
}

static const double SR = 44100.0;
static const int    BS = 128;
static const int    nQuiet = (int) (6.5 * SR);
static const int    nTalk  = (int) (1.5 * SR);
static const int    nCycle = nQuiet + nTalk;
static const int    nA     = (int) (2.5 * SR);

// 8秒ぶんの「同じ入力」を作る（乱数は毎回この種から）
static void makeCycle (std::vector<float>& v)
{
    v.resize ((size_t) nCycle);
    unsigned rs = 12345u;
    double ph = 0.0;
    for (int t = 0; t < nCycle; ++t)
    {
        rs = rs * 1664525u + 1013904223u;
        float x = (float) ((int) (rs >> 9) - 4194304) / 4194304.0f * 0.004f;   // -48dBFS
        if (t >= nQuiet)
        {
            double s = 0.0;
            for (int h = 1; h <= 10; ++h) s += std::sin (ph * h) / h;
            ph += 2.0 * juce::MathConstants<double>::pi * 196.0 / SR;
            x += (float) (0.10 * s);                                           // -20dBFS
        }
        v[(size_t) t] = x;
    }
}

//  relearn: 自動学びなおしを入れるか
//  earlyCycle / lateCycle: 何周期めを記録するか（8秒周期）
static void run (bool relearn, int earlyCycle, int lateCycle,
                 std::vector<float>& early, std::vector<float>& late)
{
    VocalGzzioProcessor p;
    flatten (p);
    setP (p, "dn_on", 1);
    setP (p, "denoise", 60);
    setP (p, "dn_relearn", relearn ? 1.0f : 0.0f);
    p.prepareToPlay (SR, BS);

    std::vector<float> cyc; makeCycle (cyc);

    juce::AudioBuffer<float> buf (2, BS);
    juce::MidiBuffer midi;

    unsigned rs = 7u;
    auto room = [&rs] { rs = rs * 1664525u + 1013904223u;
        return (float) ((int) (rs >> 9) - 4194304) / 4194304.0f * 0.004f; };
    double ph = 0.0;
    auto voice = [&ph] { double s = 0.0;
        for (int h = 1; h <= 10; ++h) s += std::sin (ph * h) / h;
        ph += 2.0 * juce::MathConstants<double>::pi * 196.0 / SR;
        return (float) (0.10 * s); };

    early.assign ((size_t) nCycle, 0.0f);
    late .assign ((size_t) nCycle, 0.0f);

    const int total = nA + (lateCycle + 1) * nCycle;
    const int learnAt = (int) (0.5 * SR) / BS * BS;

    for (int done = 0; done < total; done += BS)
    {
        if (done == learnAt) p.requestDenoiseLearn();

        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);
        for (int i = 0; i < BS; ++i)
        {
            const int t = done + i;
            float v;
            if (t < nA) { v = room(); }
            else
            {
                const int c = (t - nA) / nCycle;
                const int k = (t - nA) % nCycle;
                if (c == earlyCycle || c == lateCycle)
                    v = cyc[(size_t) k];              // ★ここだけ「同じ8秒」を流す
                else
                {
                    v = room();
                    if (k >= nQuiet) v += voice();
                }
            }
            L[i] = v; R[i] = v;
        }
        p.processBlock (buf, midi);

        for (int i = 0; i < BS; ++i)
        {
            const int t = done + i;
            if (t < nA) continue;
            const int c = (t - nA) / nCycle;
            const int k = (t - nA) % nCycle;
            if (c == earlyCycle) early[(size_t) k] = buf.getSample (0, i);
            if (c == lateCycle)  late [(size_t) k] = buf.getSample (0, i);
        }
    }
}

static void compare (const char* title,
                     const std::vector<float>& a, const std::vector<float>& b)
{
    std::printf ("\n%s\n", title);
    const int win = (int) (0.100 * SR);
    double worst = 0.0; int worstAt = -1;
    std::vector<double> ratios;
    for (int s = 0; s + win <= nCycle; s += win)
    {
        double ea = 0.0, eb = 0.0;
        for (int i = s; i < s + win; ++i)
        { ea += (double) a[(size_t) i] * a[(size_t) i];
          eb += (double) b[(size_t) i] * b[(size_t) i]; }
        const double d = 10.0 * std::log10 ((eb + 1e-20) / (ea + 1e-20));
        ratios.push_back (d);
        if (std::abs (d) > std::abs (worst)) { worst = d; worstAt = s; }
    }
    // 喋りはじめ(=nQuiet)の前後だけ並べる
    std::printf ("    喋りはじめ前後の 100msごとの差（おそい回 − はやい回）:\n      ");
    for (int s = nQuiet - 3 * win; s <= nQuiet + 6 * win; s += win)
    {
        if (s < 0 || s + win > nCycle) continue;
        std::printf ("%+.2f ", ratios[(size_t) (s / win)]);
    }
    std::printf ("dB\n");
    std::printf ("    いちばん違うところ: %+.2f dB（%.2f 秒め / 喋りはじめは %.2f 秒め）\n",
                 worst, worstAt / SR, nQuiet / SR);
    CHECK (std::abs (worst) < 1.0,
           "同じ入力なら、30分後でも出音は同じ（最大差 %.2f dB < 1dB）", std::abs (worst));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_dndrift");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } restore { autosave, backup };
    autosave.deleteFile();

    std::printf ("同じ8秒を「2分め」と「28分め」に流して、出音が変わるかを見る\n");
    std::printf ("（入力は1サンプルも違わない。違えば中に時間で変わる物がある）\n");

    std::vector<float> e1, l1, e2, l2;
    run (true,  15, 210, e1, l1);      // 15周期=2.0分め / 210周期=28.0分め
    run (false, 15, 210, e2, l2);

    compare ("[1] 学習ずみ + 自動学びなおし ON（トーク配信のおまかせ設定）", e1, l1);
    compare ("[2] 学習ずみ + 自動学びなおし OFF（既定）", e2, l2);

    std::printf ("\n=======================================\n");
    std::printf (gFail == 0 ? "  dsp_dndrift: ぜんぶ PASS\n" : "  dsp_dndrift: %d 件 FAIL\n", gFail);
    std::printf ("=======================================\n");
    return gFail == 0 ? 0 : 1;
}
