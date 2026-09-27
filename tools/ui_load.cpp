// ui_load.cpp — v3.0-c「画面が音を止めていないか」を機械で測る。
//
//  きっかけ: REAPER で「周期的に音が入らなくなり、ピーク音がジッジッジッ」という
//  報告。音の処理そのものは dsp_cpu / bench_lat で締切内に収まっているのに、
//  実機で音が途切れるなら、疑うのは**画面がメッセージスレッドを食い潰していないか**。
//
//  ここで測ること:
//   [1] 画面ぜんぶを1回描くのに何ms掛かるか
//   [2] カード1枚を描くのに何ms掛かるか（日本語の折り返し計測が効いている所）
//   [3] メーターの帯だけを描き直すと何ms掛かるか（v3.0-cで直した形）
//   [4] 20Hz で画面を描きながら 124サンプルの音を回して、**締切(2.81ms)を
//       落とした回数**が増えないか
//
//  [4] が本番。DAW が何であれ、
//  「画面を描いている間に音のブロックが締切を落とす」なら音は途切れる。
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestPaths.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

using Clock = std::chrono::steady_clock;
static double msSince (Clock::time_point t)
{
    return std::chrono::duration<double, std::milli> (Clock::now() - t).count();
}

// 画面の中から ModuleCard を全部拾う（列のカード8枚）
static void collectCards (juce::Component& c, std::vector<ModuleCard*>& out)
{
    for (auto* k : c.getChildren())
    {
        if (auto* m = dynamic_cast<ModuleCard*> (k)) out.push_back (m);
        collectCards (*k, out);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // 画面の設定は既定（くわしい画面・100%）で測る
    {
        auto f = gz::dataDirectory().getChildFile ("VocalGzzio.settings");
        f.getParentDirectory().createDirectory();
        f.replaceWithText ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PROPERTIES>\n"
                           "  <VALUE name=\"ui_advanced\" val=\"1\"/>\n"
                           "  <VALUE name=\"ui_layout_revision\" val=\"2\"/>\n"
            "  <VALUE name=\"ui_overview\" val=\"0\"/>\n"
            "  <VALUE name=\"ui_focus\" val=\"0\"/>\n"
                           "  <VALUE name=\"ui_theme\" val=\"1\"/>\n"
                           "  <VALUE name=\"ui_font\" val=\"1.0\"/>\n"
                           "  <VALUE name=\"ui_zoom\" val=\"1.0\"/>\n</PROPERTIES>\n");
    }

    std::printf ("画面の重さと、音の締切への影響\n\n");

    VocalGzzioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    if (ed == nullptr) { std::printf ("  FAIL: エディタが作れない\n"); return 1; }
    ed->setSize (ed->getWidth(), ed->getHeight());

    std::vector<ModuleCard*> cards;
    collectCards (*ed, cards);
    std::printf ("とおり道のカード: %d 枚\n\n", (int) cards.size());

    // オフスクリーンのCPU描画を測る。既定のGPU画像では、リモートCIの
    // GPU転送待ちまで毎回の小さな再描画コストとして数えてしまう。
    juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true, juce::SoftwareImageType());

    // ---------------------------------------------------------------- [1]
    std::printf ("[1] 画面ぜんぶを1回描く\n");
    double fullMs = 0.0;
    {
        for (int i = 0; i < 3; ++i) { juce::Graphics g (img); ed->paintEntireComponent (g, false); }  // 助走
        const auto t0 = Clock::now();
        const int n = 12;
        for (int i = 0; i < n; ++i) { juce::Graphics g (img); ed->paintEntireComponent (g, false); }
        fullMs = msSince (t0) / n;
        std::printf ("  1回 %.2f ms\n", fullMs);
    }

    // ---------------------------------------------------------------- [2][3]
    double cardMs = 0.0, meterMs = 0.0;
    if (! cards.empty())
    {
        std::printf ("\n[2] カード1枚をぜんぶ描く（名前・ひとことの折り返し計測を含む）\n");
        auto* c0 = cards[0];
        juce::Image ci (juce::Image::ARGB, juce::jmax (1, c0->getWidth()),
                        juce::jmax (1, c0->getHeight()), true, juce::SoftwareImageType());
        for (int i = 0; i < 5; ++i) { juce::Graphics g (ci); c0->paintEntireComponent (g, false); }
        {
            const auto t0 = Clock::now();
            const int n = 200;
            for (int i = 0; i < n; ++i) { juce::Graphics g (ci); c0->paintEntireComponent (g, false); }
            cardMs = msSince (t0) / n;
            std::printf ("  1枚 %.3f ms  → 8枚×20回/秒なら %.1f ms/秒\n",
                         cardMs, cardMs * 8.0 * 20.0);
        }


        // ★これが本命の数字。
        //  カードは下地が透けているので、カード1枚を描き直すよう頼むと
        //  **その席の背景（テーマの絵・マスコット・ガラス）まで**描き直される。
        //  直す前は、メーターが動くたびにこれが8枚ぶん走っていた。
        std::printf ("\n[2b] カードの席を、画面ぜんたい側から描き直す（透けている下地ごと）\n");
        double bgMs = 0.0;
        {
            const auto area = ed->getLocalArea (c0, c0->getLocalBounds());
            for (int i = 0; i < 3; ++i)
            { juce::Graphics g (img); g.reduceClipRegion (area); ed->paintEntireComponent (g, false); }
            const auto t0 = Clock::now();
            const int n = 40;
            for (int i = 0; i < n; ++i)
            { juce::Graphics g (img); g.reduceClipRegion (area); ed->paintEntireComponent (g, false); }
            bgMs = msSince (t0) / n;
            std::printf ("  1枚ぶん %.3f ms  → 8枚×20回/秒なら **%.0f ms/秒**\n",
                         bgMs, bgMs * 8.0 * 20.0);
            std::printf ("  （1秒は1000ms。ここが数百msになると、実機で音に触る）\n");
        }
        std::printf ("\n[3] v3.0-c の形: メーターは**不透明な子**なので、親は描き直されない\n");
        {
            MeterBar* bar = nullptr;
            for (auto* k : c0->getChildren())
                if (auto* b = dynamic_cast<MeterBar*> (k)) bar = b;
            if (bar == nullptr) { std::printf ("  FAIL: MeterBar が見つからない\n"); ++gFail; }
            else
            {
                CHECK (bar->isOpaque(),
                       "メーターの帯は不透明（＝JUCEは親を描き直さない）");
                juce::Image bi (juce::Image::ARGB, juce::jmax (1, bar->getWidth()),
                                juce::jmax (1, bar->getHeight()), true, juce::SoftwareImageType());
                for (int i = 0; i < 10; ++i)
                { juce::Graphics g (bi); bar->paintEntireComponent (g, false); }
                const auto t0 = Clock::now();
                const int n = 400;
                for (int i = 0; i < n; ++i)
                { juce::Graphics g (bi); bar->paintEntireComponent (g, false); }
                meterMs = msSince (t0) / n;
                std::printf ("  1枚ぶん %.4f ms  → 8枚×20回/秒なら **%.1f ms/秒**\n",
                             meterMs, meterMs * 8.0 * 20.0);
            }
        }
        // マシン固有の5ms閾値はCIや描画バックエンドで変動する。
        // 同じ環境で旧描画より4倍以上軽いことと、1コアの5%以内を同時に要求する。
        CHECK (meterMs < bgMs * 0.25 && meterMs * 8.0 * 20.0 < 50.0,
               "部分描画は旧描画の25%%未満、1秒の5%%以内 (%.1f ms/秒)", meterMs * 8.0 * 20.0);
        std::printf ("  比較: 直す前の形（カードの席を下地ごと）だと 1秒あたり %.0f ms でした\n",
                     bgMs * 8.0 * 20.0);
    }

    // ---------------------------------------------------------------- [4]
    std::printf ("\n[4] 画面を描きながら、音のブロックが締切を落とさないか\n");
    //  124サンプル @44.1k = 2.81 ms が締切。音のスレッドを別に立てて、
    //  こちら（メッセージスレッド）で画面を描き続ける。
    //  DAW が Cubase でも REAPER でも、この関係は同じ。
    const int    bs       = 124;
    const double deadline = 1000.0 * (double) bs / 44100.0;
    proc.prepareToPlay (44100.0, bs);

    auto runAudio = [&] (int seconds, std::atomic<bool>& stop) -> std::pair<int,double>
    {
        juce::AudioBuffer<float> buf (2, bs);
        juce::MidiBuffer midi;
        int over = 0; double worst = 0.0; int blocks = 0;
        double ph = 0.0;
        const auto until = Clock::now() + std::chrono::seconds (seconds);
        while (Clock::now() < until && ! stop.load())
        {
            for (int i = 0; i < bs; ++i)
            {
                const float v = (float) (0.2 * std::sin (ph));
                ph += 2.0 * juce::MathConstants<double>::pi * 220.0 / 44100.0;
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            const auto t0 = Clock::now();
            proc.processBlock (buf, midi);
            const double ms = msSince (t0);
            worst = juce::jmax (worst, ms);
            if (ms > deadline) ++over;
            ++blocks;
            std::this_thread::sleep_for (std::chrono::microseconds (
                (int) juce::jmax (0.0, (deadline - ms) * 1000.0)));
        }
        return { blocks > 0 ? (int) (100.0 * over / blocks) : 0, worst };
    };

    // (a) 画面を描かないとき
    std::atomic<bool> stop { false };
    auto quiet = runAudio (3, stop);
    std::printf ("  画面を描かない: 締切超え %d%% / 最悪 %.2f ms（締切 %.2f ms）\n",
                 quiet.first, quiet.second, deadline);

    // (b) 20Hz で画面を描きながら
    std::atomic<bool> stopB { false };
    std::pair<int,double> loaded { 0, 0.0 };
    std::thread audio ([&] { loaded = runAudio (3, stopB); });
    {
        const auto until = Clock::now() + std::chrono::seconds (3);
        while (Clock::now() < until)
        {
            for (auto* c : cards) c->setMeter ((float) (0.2 + 0.6 * std::sin ((double) juce::Random::getSystemRandom().nextInt (100))));
            { juce::Graphics g (img); ed->paintEntireComponent (g, false); }
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
        }
    }
    audio.join();
    std::printf ("  20Hzで描きながら: 締切超え %d%% / 最悪 %.2f ms\n",
                 loaded.first, loaded.second);

    CHECK (loaded.first <= quiet.first + 2,
           "画面を描いても締切超えが増えない (%d%% → %d%%)", quiet.first, loaded.first);

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
