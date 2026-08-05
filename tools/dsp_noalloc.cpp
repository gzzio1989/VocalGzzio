// dsp_noalloc.cpp — v2.8.0「音声コールバックの中でメモリを確保しない」の実測
//
//  なぜ測るのか
//    juce の `IIR::Coefficients::makeXxx()` は中身が `return *new Coefficients(...)`。
//    フィルタ係数の作り直しは1ブロックに11回＋ディエッサー/スマートEQで
//    32サンプルごとにも走っていたので、64サンプルのバッファだと 1.3ms ごとに
//    数十回の malloc/free が起きていた。「配信中つけっぱなし」を売りにしている
//    製品としては致命的なので、`ArrayCoefficients`（std::array を値で返す＝
//    ヒープを触らない）へ全部置き換えた。
//    ここでは global operator new を数えて、**本当にゼロになったか**を見る。
//
//  ビルド(このファイルだけ特別。JUCEのヘッダを使う):
//    g++ -O2 -std=c++17 -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
//        -I<juce>/modules dsp_noalloc.cpp -o dsp_noalloc
//  ※ 実際のコマンドは tools/run_noalloc.sh に書いてある。
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <new>
#include <atomic>

static std::atomic<long> gNewCount { 0 };
static bool gCounting = false;

void* operator new (std::size_t n)
{
    if (gCounting) gNewCount.fetch_add (1, std::memory_order_relaxed);
    void* p = std::malloc (n ? n : 1);
    if (! p) throw std::bad_alloc();
    return p;
}
void* operator new[] (std::size_t n) { return operator new (n); }
void  operator delete   (void* p) noexcept { std::free (p); }
void  operator delete[] (void* p) noexcept { std::free (p); }
void  operator delete   (void* p, std::size_t) noexcept { std::free (p); }
void  operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

#include <juce_dsp/juce_dsp.h>

using Coefs  = juce::dsp::IIR::Coefficients<float>;
using ACoefs = juce::dsp::IIR::ArrayCoefficients<float>;

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

int main()
{
    const double sr = 48000.0;
    const int    blocks = 1000;          // 64サンプル換算で約1.3秒ぶん

    // 準備段階の確保は数えない（prepareToPlay 相当）
    juce::dsp::IIR::Filter<float> f;
    f.coefficients = Coefs::makeHighShelf (sr, 6500.0f, 0.8f, 1.0f);
    Coefs::Ptr shared = Coefs::makePeakFilter (sr, 3000.0f, 1.4f, 1.0f);
    volatile float sink = 0.0f;

    std::printf ("フィルタ係数の作り直し %d 回あたりのヒープ確保回数\n", blocks);

    // ---- 直す前の書き方: makeXxx() は毎回 new する ----
    gNewCount = 0; gCounting = true;
    for (int i = 0; i < blocks; ++i)
    {
        const float gain = 1.0f + 0.001f * (float) (i % 100);
        auto p = Coefs::makeHighShelf (sr, 6500.0f, 0.8f, gain);
        sink += p->getRawCoefficients()[0];
    }
    gCounting = false;
    const long before = gNewCount.load();
    CHECK (before >= blocks, "直す前(makeXxx): %ld 回確保した (>= %d = 毎回newしている)",
           before, blocks);

    // ---- 直したあとの書き方: ArrayCoefficients を既存の係数へ入れる ----
    // 1回目だけ Array の内部確保が起きる可能性があるので、まず1回回してから数える。
    *f.coefficients = ACoefs::makeHighShelf (sr, 6500.0f, 0.8f, 1.0f);
    gNewCount = 0; gCounting = true;
    for (int i = 0; i < blocks; ++i)
    {
        const float gain = 1.0f + 0.001f * (float) (i % 100);
        *f.coefficients = ACoefs::makeHighShelf (sr, 6500.0f, 0.8f, gain);
        sink += f.coefficients->getRawCoefficients()[0];
    }
    gCounting = false;
    const long after = gNewCount.load();
    CHECK (after == 0, "直したあと(ArrayCoefficients): %ld 回確保した (0 が正)", after);

    // ---- ピーク型も同じか（スマートEQ・艶で使う） ----
    *shared = ACoefs::makePeakFilter (sr, 3000.0f, 1.4f, 1.0f);
    gNewCount = 0; gCounting = true;
    for (int i = 0; i < blocks; ++i)
    {
        *shared = ACoefs::makePeakFilter (sr, 3000.0f, 1.4f, 1.0f + 0.001f * (float) (i % 100));
        sink += shared->getRawCoefficients()[0];
    }
    gCounting = false;
    CHECK (gNewCount.load() == 0, "ピーク型も 0 回 (%ld)", gNewCount.load());

    // ---- 音が同じかどうか（置き換えで音を変えていないことの確認） ----
    {
        auto a = Coefs::makeHighShelf (sr, 6500.0f, 0.8f, 1.7f);
        Coefs b; b = ACoefs::makeHighShelf (sr, 6500.0f, 0.8f, 1.7f);
        float worst = 0.0f;
        for (int i = 0; i < 5; ++i)
            worst = juce::jmax (worst, std::abs (a->getRawCoefficients()[i]
                                               - b.getRawCoefficients()[i]));
        CHECK (worst < 1.0e-6f, "係数の中身は同一 (最大差 %.3g = 音は変わらない)", (double) worst);
    }

    juce::ignoreUnused (sink);
    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
