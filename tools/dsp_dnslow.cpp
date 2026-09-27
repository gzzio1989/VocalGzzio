#include "TestPaths.h"
// dsp_dnslow.cpp — 「時間が経つと、喋りはじめに サーッ となってから消える」の実測。
//
//  ユーザ報告(2026-08-31):
//    ・ノイズ除去の「学習」は押した状態
//    ・10〜30分くらい経つと、喋りはじめに サー となってから消えるようになる
//
//  仮説: 学習した床(dnFloor)は med×1.4 だが、v3.0 の「自動学びなおし」は
//        包絡 dnEnv そのものへ τ=2秒 で寄っていく（×1.4 の余裕を持たない）。
//        静かな区間だけ少しずつ進むので、効いてくるのが数十分後になる。
//        床が下がると 帯域3 の bandHasSignal 判定
//            dnEnv[3] > dnFloor[3] * 3.0f
//        が、母音がクロスオーバ(5kHz)から漏れてくるぶんだけで真になる。
//        すると「音があるバンド」とみなされて 1ms で全開になり、
//        そっと開ける(0.25秒)が効かなくなる ＝ 喋りはじめに サーッ。
//        そのあと hfNoiseOnly が 0.08秒で抑えるので「なってから消える」。
//
//  ここでは仮説を主張しない。**同じ入力を30分ぶん流して、数字が動くかどうか**
//  だけを見る。動かなければ仮説は捨てる。
//
//  測るもの: 各フレーズの「喋りはじめ150ms」に、出力の5kHz以上が何dBFS出たか。
//            入力(部屋ノイズ)はずっと同じなので、この値が上がる＝サーが育っている。
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

// dsp_dnonset と同じ「全部平ら」。既定ONの機能群が混ざると測定が汚れる。
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

// 5kHz より上だけを取り出す測定用フィルタ。
//  ★1次を2段では足りなかった。母音(-20dBFS, 倍音は1960Hzまで)の裾が
//    -45dBFS くらいで漏れてきて、測りたい部屋ノイズ(-60dBFS前後)を埋めてしまう。
//    初版はそれで「30分ずっと -46.71 dBFS」という、声しか見ていない数字を出した。
//    1次HPを8段(48dB/oct)にすると 2kHz で約-71dB。声の漏れは -91dBFS まで落ちる。
struct HiMeas
{
    static constexpr int N = 8;
    float z[N] = {};
    float hp (float x)
    {
        const float a = 0.55f;                    // ~5.6kHz @44.1k
        float y = x;
        for (int i = 0; i < N; ++i) { z[i] = a * (z[i] + y); y = y - z[i]; }
        return y;
    }
};

struct Cycle { int idx; double quietDbFS; double onsetDbFS; double floor3; };

// ---------------------------------------------------------------------------
//  1回ぶんの長回し。
//   relearn: 自動学びなおし(dn_relearn) を入れるか
//   doLearn: 学習ボタンを押すか（false なら順応式の床のまま）
//   minutes: 何分ぶん流すか
static std::vector<Cycle> longRun (bool relearn, bool doLearn, double minutes,
                                   double* learnedFloor3, bool noiseRise = false)
{
    const double SR = 44100.0;
    const int    bs = 128;

    VocalGzzioProcessor p;
    flatten (p);
    setP (p, "dn_on", 1);
    setP (p, "denoise", 60);
    setP (p, "dn_relearn", relearn ? 1.0f : 0.0f);
    p.prepareToPlay (SR, bs);

    juce::AudioBuffer<float> buf (2, bs);
    juce::MidiBuffer midi;

    unsigned rs = 7u;
    // 部屋ノイズ。既定は -48dBFS 固定（dsp_dnonset[3] と同じ「やや騒がしい部屋」）。
    // noiseRise のときは -54dBFS から -44dBFS へ、30分かけてゆっくり上がる。
    //  ＝ PCのファンが回りだす／エアコンが入る、長丁場の配信で実際に起きること。
    float roomAmp = 0.004f;
    auto room = [&rs, &roomAmp]
    {
        rs = rs * 1664525u + 1013904223u;
        return (float) ((int) (rs >> 9) - 4194304) / 4194304.0f * roomAmp;
    };
    double ph = 0.0;
    // 母音だけ。さ行は入れない ＝ 5kHz以上に出るのは「部屋ノイズ」と
    // 「クロスオーバからの漏れ」だけ。サーの正体を混ぜない。
    auto voice = [&ph]
    {
        double v = 0.0;
        for (int h = 1; h <= 10; ++h) v += std::sin (ph * h) / h;   // ~1960Hz
        ph += 2.0 * juce::MathConstants<double>::pi * 196.0 / 44100.0;
        return (float) (0.10 * v);                                  // ~-20dBFS
    };

    // 立ち上がり: [A] 2.5秒 部屋ノイズだけ。0.5秒目に学習。
    const int nA = (int) (2.5 * SR);
    const int learnAt = (int) (0.5 * SR) / bs * bs;

    // 以降くりかえし: 6.5秒 だまる → 1.5秒 しゃべる（8秒周期）
    const int nQuiet = (int) (6.5 * SR);
    const int nTalk  = (int) (1.5 * SR);
    const int nCycle = nQuiet + nTalk;
    const int cycles = (int) (minutes * 60.0 * SR) / nCycle;

    HiMeas mOut;
    bool grabbedLearn = false;
    std::vector<Cycle> out;
    double eOnset = 0.0; int nOnset = 0;
    double eQuiet = 0.0; int nQuietN = 0;
    const int onsetWin = (int) (0.150 * SR);      // 喋りはじめ150ms

    const int total = nA + cycles * nCycle;
    for (int done = 0; done < total; done += bs)
    {
        if (doLearn && done == learnAt) p.requestDenoiseLearn();

        if (noiseRise)
        {
            const double frac = (double) done / (double) juce::jmax (1, total);
            const double db = -54.0 + 10.0 * frac;      // -54 → -44 dBFS
            roomAmp = (float) (std::pow (10.0, db / 20.0) * std::sqrt (3.0));
        }
        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);
        for (int i = 0; i < bs; ++i)
        {
            const int t = done + i;
            float v = room();
            if (t >= nA)
            {
                const int k = (t - nA) % nCycle;
                if (k >= nQuiet) v += voice();
            }
            L[i] = v; R[i] = v;
        }
        p.processBlock (buf, midi);

        for (int i = 0; i < bs; ++i)
        {
            const int t = done + i;
            const float ho = mOut.hp (buf.getSample (0, i));
            if (t < nA) continue;
            const int c = (t - nA) / nCycle;
            const int k = (t - nA) % nCycle;
            // だまっている区間の終わり150ms = ゲートが閉じきった「静けさ」の基準
            if (k >= nQuiet - onsetWin && k < nQuiet)
            { eQuiet += (double) ho * ho; ++nQuietN; }
            if (k >= nQuiet && k < nQuiet + onsetWin)
            { eOnset += (double) ho * ho; ++nOnset; }
            if (k == nQuiet + onsetWin && nOnset > 0)
            {
                const double rms = std::sqrt (eOnset / (double) nOnset);
                const double rmsQ = std::sqrt (eQuiet / (double) juce::jmax (1, nQuietN));
                double f3 = 0.0;
                {   // 床は状態XMLに出ている（学習ずみのときだけ本物）
                    juce::MemoryBlock mb; p.getStateInformation (mb);
                    if (auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()))
                        if (auto* dn = xml->getChildByName ("DENOISE"))
                            f3 = dn->getDoubleAttribute ("f3", 0.0);
                }
                out.push_back ({ c, 20.0 * std::log10 (juce::jmax (1e-12, rmsQ)),
                                    20.0 * std::log10 (juce::jmax (1e-12, rms)), f3 });
                eOnset = 0.0; nOnset = 0; eQuiet = 0.0; nQuietN = 0;
            }
        }
        if (done >= nA && ! grabbedLearn && learnedFloor3 != nullptr)
        {
            juce::MemoryBlock mb; p.getStateInformation (mb);
            if (auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()))
                if (auto* dn = xml->getChildByName ("DENOISE"))
                    *learnedFloor3 = dn->getDoubleAttribute ("f3", 0.0);
            grabbedLearn = true;
        }
    }
    return out;
}

static void report (const char* title, const std::vector<Cycle>& v)
{
    std::printf ("\n%s\n", title);
    if (v.empty()) { std::printf ("  (周期が取れませんでした)\n"); return; }
    const int marks[] = { 0, 7, 22, 75, 150, 220 };
    for (int m : marks)
    {
        if (m >= (int) v.size()) continue;
        const auto& c = v[(size_t) m];
        std::printf ("    %5.1f分め   だまり %7.2f → 喋りはじめ %7.2f dBFS   段差 %+6.2f dB   床3 %.3e\n",
                     (c.idx * 8.0) / 60.0, c.quietDbFS, c.onsetDbFS,
                     c.onsetDbFS - c.quietDbFS, c.floor3);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_dnslow");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } restore { autosave, backup };
    autosave.deleteFile();

    std::printf ("時間が経つと「喋りはじめのサー」が育つか（30分ぶんを実際に流す）\n");
    std::printf ("入力はずっと同じ: 部屋ノイズ -48dBFS + 8秒ごとに1.5秒の母音 -20dBFS\n");
    std::printf ("さ行は一切入れていないので、5kHz以上に出るのは部屋ノイズだけ。\n");

    double lf1 = 0.0, lf2 = 0.0;
    auto onRelearn = longRun (true,  true, 30.0, &lf1);
    auto offRelearn = longRun (false, true, 30.0, &lf2);

    double lf3 = 0.0, lf4 = 0.0;
    auto riseOn  = longRun (true,  true, 30.0, &lf3, true);
    auto riseOff = longRun (false, true, 30.0, &lf4, true);

    report ("[1] 部屋の音は一定 / 学習ずみ + 自動学びなおし ON（トーク配信のおまかせ）", onRelearn);
    report ("[2] 部屋の音は一定 / 学習ずみ + 自動学びなおし OFF（既定）", offRelearn);
    report ("[3] 部屋が -54→-44dBFS に上がる / 学びなおし ON", riseOn);
    report ("[4] 部屋が -54→-44dBFS に上がる / 学びなおし OFF", riseOff);

    std::printf ("\n[3] 判定\n");
    auto growth = [] (const std::vector<Cycle>& v) -> double
    {
        if (v.size() < 30) return 0.0;
        // はじめの1分ぶん(7周期)と、おわりの1分ぶんの平均をくらべる
        double a = 0.0, b = 0.0; int na = 0, nb = 0;
        for (size_t i = 0; i < v.size(); ++i)
        {
            const double step = v[i].onsetDbFS - v[i].quietDbFS;   // サーの段差
            if ((int) i < 7)                        { a += step; ++na; }
            if ((int) i >= (int) v.size() - 7)      { b += step; ++nb; }
        }
        return (b / juce::jmax (1, nb)) - (a / juce::jmax (1, na));
    };
    const double gOn  = growth (onRelearn);
    const double gOff = growth (offRelearn);
    const double gRiseOn  = growth (riseOn);
    const double gRiseOff = growth (riseOff);
    std::printf ("    学びなおしON : サーの段差が はじめ→おわりで %+.2f dB\n", gOn);
    std::printf ("    学びなおしOFF: サーの段差が はじめ→おわりで %+.2f dB\n", gOff);
    std::printf ("    部屋が上がる ON : サーの段差が はじめ→おわりで %+.2f dB\n", gRiseOn);
    std::printf ("    部屋が上がる OFF: サーの段差が はじめ→おわりで %+.2f dB\n", gRiseOff);
    std::printf ("    学習直後の床3: ON %.3e / OFF %.3e\n", lf1, lf2);

    CHECK (gOn  < 3.0, "学びなおしONでも、喋りはじめのノイズは育たない (%+.2f dB < 3dB)", gOn);
    CHECK (gOff < 3.0, "学びなおしOFFでも、喋りはじめのノイズは育たない (%+.2f dB < 3dB)", gOff);
    CHECK (gRiseOn < 3.0, "部屋が10dB上がっても、学びなおしONなら段差は育たない (%+.2f dB < 3dB)", gRiseOn);

    std::printf ("\n=======================================\n");
    std::printf (gFail == 0 ? "  dsp_dnslow: ぜんぶ PASS\n" : "  dsp_dnslow: %d 件 FAIL\n", gFail);
    std::printf ("=======================================\n");
    return gFail == 0 ? 0 : 1;
}
