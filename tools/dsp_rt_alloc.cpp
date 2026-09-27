#include "TestPaths.h"
// dsp_rt_alloc.cpp — v2.10.0「音声コールバックの中で確保しない」を**本体で**実測する
//
//  既存の dsp_noalloc.cpp は JUCE の係数ヘルパだけを見ている。
//  こちらは **VocalGzzioProcessor::processBlock を実際に回して**、その最中に
//  global operator new が何回呼ばれたかを数える。v2.10.0 で足した経路
//  （音源モードの切替 → なめらかの組み直し／ノイズ床の学習 → 度数分布）が
//  本当に確保なしかは、ここでしか確かめられない。
//
//  ※ ここで「0回」と言えないと、配信中に一瞬プツッと切れる原因になり得る。
//     締切は 124サンプル/44.1kHz で 2.81ms しかない。
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <new>
#include <atomic>

static std::atomic<long> gNewCount { 0 };
static std::atomic<bool> gCounting { false };

void* operator new (std::size_t n)
{
    if (gCounting.load (std::memory_order_relaxed))
        gNewCount.fetch_add (1, std::memory_order_relaxed);
    void* p = std::malloc (n ? n : 1);
    if (! p) throw std::bad_alloc();
    return p;
}
void* operator new[] (std::size_t n) { return operator new (n); }
void  operator delete   (void* p) noexcept { std::free (p); }
void  operator delete[] (void* p) noexcept { std::free (p); }
void  operator delete   (void* p, std::size_t) noexcept { std::free (p); }
void  operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

#include "PluginProcessor.h"
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

struct Src
{
    double ph = 0.0; unsigned s = 999u;
    float next() noexcept
    {
        s = s * 1664525u + 1013904223u;
        const float n = (float) ((int) (s >> 9) - 4194304) / 4194304.0f;
        double v = 0.0;
        for (int h = 1; h <= 6; ++h) v += std::sin (ph * h) / h;
        ph += 2.0 * juce::MathConstants<double>::pi * 196.0 / 48000.0;
        return (float) (0.18 * v / 1.6 + n * 0.0012);
    }
};

// blocks ブロックぶん回して、そのあいだの new の回数を返す
static long runCounting (VocalGzzioProcessor& p, Src& src, int blocks, int blockSize = 128,
                         int switchModeAt = -1, int learnAt = -1)
{
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    gNewCount.store (0);
    gCounting.store (true);
    for (int b = 0; b < blocks; ++b)
    {
        // ★モード切替も学習も、数えている最中に起こす（そこが今回の新しい経路）
        if (b == switchModeAt) setP (p, "src_mode", 1.0f);   // うた → アコギ
        if (b == learnAt)      p.requestDenoiseLearn();
        auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
        for (int n = 0; n < blockSize; ++n) { const float v = src.next(); L[n] = v; R[n] = v; }
        p.processBlock (buf, midi);
    }
    gCounting.store (false);
    return gNewCount.load();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_rtalloc");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } restore { autosave, backup };
    auto fresh = [&] { autosave.deleteFile(); };

    std::printf ("音声コールバックの中でメモリを確保していないか（本体で実測）\n\n");

    // ---- 1. 出荷状態でひたすら回す ----
    std::printf ("[1] 出荷状態（1000ブロック = 約2.7秒）\n");
    {
        fresh(); VocalGzzioProcessor p; Src src;
        p.prepareToPlay (48000.0, 128);
        runCounting (p, src, 50);                       // 助走（初回の遅延確保を済ませる）
        const long n = runCounting (p, src, 1000);
        CHECK (n == 0, "確保 %ld 回", n);
    }
    std::printf ("\n");

    // ---- 2. ぜんぶ効かせた状態 ----
    std::printf ("[2] 機能をぜんぶ入れた状態\n");
    {
        fresh(); VocalGzzioProcessor p; Src src;
        setP (p, "hum_amt", 100); setP (p, "cons_amt", 50); setP (p, "res_amt", 60);
        setP (p, "ride_amt", 50); setP (p, "comp1", 40); setP (p, "comp2", 40);
        setP (p, "deess", 40); setP (p, "dn_on", 1); setP (p, "denoise", 60);
        setP (p, "gate", -50); setP (p, "br_amt", 40); setP (p, "ring", 30);
        setP (p, "seq_on", 1); setP (p, "seq_amount", 60);
        setP (p, "prox_amt", 50); setP (p, "revon", 1); setP (p, "revmix", 20);
        setP (p, "dly_on", 1); setP (p, "delay", 20); setP (p, "doubler", 20); setP (p, "width", 20);
        setP (p, "at_on", 1); setP (p, "at_amount", 80); setP (p, "orn_amt", 70);
        setP (p, "jn_on", 1); setP (p, "jn_mix", 55); setP (p, "vc_on", 1); setP (p, "vc_pitch", 3);
        p.prepareToPlay (48000.0, 128);
        runCounting (p, src, 50);
        const long n = runCounting (p, src, 1000);
        CHECK (n == 0, "確保 %ld 回", n);
    }
    std::printf ("\n");

    // ---- 3. ★音源モードを鳴らしながら切り替える（v2.10.0 の新しい経路）----
    std::printf ("[3] 鳴らしながら音源モードを切り替える（なめらかを組み直す）\n");
    {
        fresh(); VocalGzzioProcessor p; Src src;
        setP (p, "res_amt", 60); setP (p, "deess", 40);
        p.prepareToPlay (48000.0, 128);
        runCounting (p, src, 50);
        const long n = runCounting (p, src, 400, 128, /*switchModeAt*/ 200);
        CHECK (n == 0, "確保 %ld 回（切替をまたいで）", n);
    }
    std::printf ("\n");

    // ---- 4. ★ノイズ床の学習を鳴らしながら走らせる（v2.10.0 の新しい経路）----
    std::printf ("[4] 鳴らしながらノイズ床を学習する（度数分布を積む）\n");
    {
        fresh(); VocalGzzioProcessor p; Src src;
        setP (p, "dn_on", 1); setP (p, "denoise", 60);
        p.prepareToPlay (48000.0, 128);
        runCounting (p, src, 50);
        // 1.5秒 = 約563ブロック。学習の開始から終わり(採否の判定)まで通す
        const long n = runCounting (p, src, 800, 128, -1, /*learnAt*/ 50);
        std::printf ("      （学習の判定は歌の最中なので「採用しない」側を通る）\n");
        CHECK (n == 0, "確保 %ld 回（学習をまたいで）", n);
    }
    std::printf ("\n");

    // ---- 5. ★学習が「採用される」側も通す（markStateDirty が走る道）----
    std::printf ("[5] 学習が採用される側（無音で学習 → 保存の合図が出る）\n");
    {
        fresh(); VocalGzzioProcessor p;
        setP (p, "dn_on", 1); setP (p, "denoise", 60);
        p.prepareToPlay (48000.0, 128);
        juce::AudioBuffer<float> buf (2, 128); juce::MidiBuffer midi;
        unsigned s = 7u;
        auto quietBlock = [&]
        {
            auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
            for (int n = 0; n < 128; ++n)
            {
                s = s * 1664525u + 1013904223u;
                const float v = (float) ((int) (s >> 9) - 4194304) / 4194304.0f * 0.0012f;
                L[n] = v; R[n] = v;
            }
            p.processBlock (buf, midi);
        };
        for (int i = 0; i < 200; ++i) quietBlock();      // 助走
        gNewCount.store (0); gCounting.store (true);
        p.requestDenoiseLearn();
        for (int i = 0; i < 800; ++i) quietBlock();      // 学習1.5秒＋その後
        gCounting.store (false);
        const long n = gNewCount.load();
        CHECK (p.isDenoiseLearned(), "実際に採用された（この道を通った）");
        CHECK (n == 0, "確保 %ld 回（採用と保存の合図をまたいで）", n);
    }
    std::printf ("\n");

    std::printf (gFail ? "== %d 件 FAIL ==\n" : "== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
