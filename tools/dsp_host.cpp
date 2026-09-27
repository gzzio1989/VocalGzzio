#include "TestPaths.h"
// dsp_host.cpp — 「周期的に音が入らなくなり、ピーク音がジッジッジッ」の再現を狙う検査。
//
//  報告環境は REAPER。REAPER は Cubase と違って
//   ・**ブロック長を毎回そろえてこない**（先読み処理や自動化で揺れる）
//   ・プラグインを先読みして別スレッドで回す（Anticipative FX）
//   ・再生/停止のたびに prepareToPlay を投げ直すことがある
//  という癖がある。ここではその癖を模して長く回し、
//  「途切れ」と「プチプチ」を**数で**捕まえる。
//
//  見るもの:
//   [1] ブロック長がばらついても、出力が途切れないか（無音の窓が出ないか）
//   [2] 連続した正弦を入れて、**不連続（プチッ）が周期的に出ないか**
//   [3] 途中で prepareToPlay を投げ直しても壊れないか（REAPERは投げ直す）
//   [4] NaN/Inf が出ないか
//   [5] 長く回すと悪化しないか（前半と後半で不連続の数を比べる）
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static constexpr double kSR = 44100.0;

// 声っぽさより「不連続が見えること」を優先して、なめらかな正弦にする。
// 220Hz の正弦なら、隣り合うサンプルの差は最大でも 2π*220/44100*振幅 ≒ 0.031*振幅。
// それを大きく超える跳びがあれば、それはプラグインが作った段差。
struct Sine
{
    double ph = 0.0;
    void fill (float* d, int n, float amp)
    {
        for (int i = 0; i < n; ++i)
        {
            d[i] = (float) (amp * std::sin (ph));
            ph += 2.0 * juce::MathConstants<double>::pi * 220.0 / kSR;
            if (ph > 2.0 * juce::MathConstants<double>::pi) ph -= 2.0 * juce::MathConstants<double>::pi;
        }
    }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_host");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } rst { autosave, backup };
    autosave.deleteFile();

    std::printf ("DAWの癖（ブロック長のばらつき・投げ直し）に対する耐性\n\n");

    const int maxBlock = 512;          // REAPER の既定あたり。ここまでの長さが来る前提で用意する
    VocalGzzioProcessor p;
    p.prepareToPlay (kSR, maxBlock);

    // 全部入りにして、いちばん重い経路を通す
    auto setP = [&p] (const char* id, float v)
    {
        if (auto* prm = p.apvts.getParameter (id))
            prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
    };
    setP ("dn_on", 1); setP ("denoise", 60);
    setP ("ds_on", 1); setP ("deess", 70);
    setP ("comp1", 50); setP ("comp2", 60);
    setP ("revon", 1); setP ("revmix", 40); setP ("revsize", 40);
    setP ("doubler", 30); setP ("width", 40); setP ("delay", 25);
    setP ("res_amt", 50); setP ("cons_amt", 40);
    setP ("mix", 100);

    juce::AudioBuffer<float> buf (2, maxBlock);
    juce::MidiBuffer midi;
    Sine sine;
    juce::Random rng (20260813);

    const double seconds = 180.0;                 // 3分
    const long   totalSamples = (long) (kSR * seconds);
    long done = 0;

    // 隣り合うサンプルの差の上限（正弦の傾き＋処理が足す変化ぶんの余裕）
    // 実測でふつうは 0.06 前後。0.35 を超えたら「段差」とみなす。
    const double stepLimit = 0.35;

    std::vector<double> clickTimes;               // 段差が出た時刻（秒）
    std::vector<double> silentTimes;              // 出力が落ちた時刻（秒）
    long nanCount = 0;
    float prevOut = 0.0f;
    int   reprepares = 0;

    double winInSq = 0.0, winOutSq = 0.0; long winN = 0;   // 0.1秒の窓で入出力を比べる

    while (done < totalSamples)
    {
        // ---- [1] ブロック長をばらつかせる（REAPER風）----
        //  たいていは 512 だが、ときどき半端な長さが来る。
        int n = maxBlock;
        const int r = rng.nextInt (100);
        if      (r < 10) n = 124;                 // 報告環境の ASIO 実測値
        else if (r < 18) n = 64;
        else if (r < 24) n = rng.nextInt ({ 17, 511 });
        n = (int) juce::jmin ((long) n, totalSamples - done);
        if (n <= 0) break;

        // ---- [3] ときどき投げ直す（再生/停止・デバイス変更の模擬）----
        if (rng.nextInt (400) == 0)
        {
            p.prepareToPlay (kSR, maxBlock);
            ++reprepares;
        }

        float in[512];
        sine.fill (in, n, 0.25f);
        buf.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c)
            juce::FloatVectorOperations::copy (buf.getWritePointer (c), in, n);

        p.processBlock (buf, midi);

        const float* out = buf.getReadPointer (0);
        for (int i = 0; i < n; ++i)
        {
            const float y = out[i];
            const double t = (double) (done + i) / kSR;

            if (! std::isfinite (y)) { ++nanCount; continue; }

            // ---- [2] 段差（プチッ）----
            if (done + i > (long) kSR)            // 立ち上がりは見ない
            {
                const double d = std::abs ((double) y - (double) prevOut);
                if (d > stepLimit) clickTimes.push_back (t);
            }
            prevOut = y;

            // ---- [1] 途切れ（0.1秒の窓で、入力はあるのに出力が落ちる）----
            winInSq  += (double) in[i] * in[i];
            winOutSq += (double) y * y;
            if (++winN >= (long) (kSR * 0.1))
            {
                const double inRms  = std::sqrt (winInSq  / (double) winN);
                const double outRms = std::sqrt (winOutSq / (double) winN);
                if (t > 2.0 && inRms > 0.05 && outRms < inRms * 0.05)
                    silentTimes.push_back (t);
                winInSq = winOutSq = 0.0; winN = 0;
            }
        }
        done += n;
    }

    std::printf ("3分ぶん流しました（ブロック長を毎回ばらつかせ、%d 回 投げ直し）\n\n", reprepares);

    // ---------------------------------------------------------------- [4]
    std::printf ("[4] NaN / Inf\n");
    CHECK (nanCount == 0, "こわれた数値は出ていない (%ld 個)", nanCount);

    // ---------------------------------------------------------------- [1]
    std::printf ("\n[1] 音が途切れないか（入力はあるのに出力が落ちる窓）\n");
    CHECK (silentTimes.empty(), "途切れた窓は %d 個", (int) silentTimes.size());
    for (size_t i = 0; i < silentTimes.size() && i < 8; ++i)
        std::printf ("    %.2f 秒\n", silentTimes[i]);

    // ---------------------------------------------------------------- [2]
    std::printf ("\n[2] プチッ（隣り合うサンプルの段差が %.2f を超える）\n", stepLimit);
    std::printf ("  見つけた段差: %d 個 / 3分\n", (int) clickTimes.size());
    CHECK (clickTimes.size() < 20, "段差は 20個未満 (%d 個)", (int) clickTimes.size());

    // 周期性: 段差どうしの間隔がそろっていたら「周期的なプチプチ」
    if (clickTimes.size() >= 6)
    {
        std::vector<double> gaps;
        for (size_t i = 1; i < clickTimes.size(); ++i)
            if (clickTimes[i] - clickTimes[i-1] > 1e-3) gaps.push_back (clickTimes[i] - clickTimes[i-1]);
        if (! gaps.empty())
        {
            std::sort (gaps.begin(), gaps.end());
            const double med = gaps[gaps.size()/2];
            int near = 0;
            for (double g : gaps) if (std::abs (g - med) < med * 0.15) ++near;
            const double ratio = (double) near / (double) gaps.size();
            std::printf ("  間隔の中央値 %.3f 秒 / そのうち %.0f%% が同じ間隔\n", med, ratio * 100.0);
            CHECK (ratio < 0.6, "段差は周期的ではない (同じ間隔 %.0f%%)", ratio * 100.0);
        }
    }

    // ---------------------------------------------------------------- [5]
    std::printf ("\n[5] 長く回すと悪化しないか\n");
    {
        int early = 0, late = 0;
        for (double t : clickTimes) { if (t < seconds * 0.5) ++early; else ++late; }
        std::printf ("  前半 %d 個 / 後半 %d 個\n", early, late);
        CHECK (late <= early + 5, "後半で増えていない (%d → %d)", early, late);
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
