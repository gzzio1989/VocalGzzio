// dsp_hum.cpp — ハムノイズ自動除去(HumKiller)の数値検証 (v2.6.0)
//
//   1: 50Hz ハム+倍音 → 50Hz を自動検出し、ハムが十分小さくなる
//   2: 60Hz ハム+倍音 → 60Hz を自動検出し、ハムが十分小さくなる
//   3: 歌だけ(ハム無し) → 何も検出せず、音がまったく変わらない
//   4: 歌 + 50Hzハム    → ハムだけ減り、歌の帯域は削られない
//   5: 男声の低い持続音(110Hz/123Hz) → 誤って削らない
//   6: 60Hz付近の持続低音(B1=61.7Hz) → 誤検出で削り取らない
//   7: 計算負荷
//
// ビルド: g++ -O2 -std=c++17 -I../Source dsp_hum.cpp -o dsp_hum
#include "../Source/HumKiller.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <complex>
#include <chrono>

static constexpr double kSR = 48000.0;
static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

// 指定周波数の成分の振幅を測る(ゴーツェル相当の素朴な相関)
static double ampAt (const std::vector<float>& x, double f, size_t from, size_t to)
{
    double re = 0.0, im = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const double w = 2.0 * M_PI * f * (double) i / kSR;
        re += x[i] * std::cos (w); im += x[i] * std::sin (w);
    }
    const double n = (double) (to - from);
    return 2.0 * std::sqrt (re * re + im * im) / n;
}

static double rmsDb (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0; for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return 10.0 * std::log10 (s / (double)(to - from) + 1e-30);
}

// 歌声っぽい信号(倍音+ビブラート)
struct Voice
{
    double ph[16] {}, vp = 0.0;
    float sample (double f0, double amp)
    {
        const double vib = 1.0 + 0.015 * std::sin (vp); vp += 2.0 * M_PI * 5.5 / kSR;
        double s = 0.0;
        for (int h = 0; h < 16; ++h)
        {
            const double fh = f0 * vib * (h + 1); if (fh > kSR * 0.45) break;
            s += std::sin (ph[h]) / (h + 1);
            ph[h] += 2.0 * M_PI * fh / kSR; if (ph[h] > 2 * M_PI) ph[h] -= 2 * M_PI;
        }
        return (float) (amp * s / 1.7);
    }
};

// ハム(基本波+倍音、位相はバラバラ)
static float humSample (double f0, double amp, long n)
{
    const double t = (double) n / kSR;
    double s = 0.0;
    const double hAmp[6] = { 1.0, 0.55, 0.35, 0.20, 0.12, 0.08 };
    const double hPh [6] = { 0.3, 1.9, 2.7, 0.9, 1.4, 2.2 };
    for (int h = 0; h < 6; ++h)
        s += hAmp[h] * std::sin (2.0 * M_PI * f0 * (h + 1) * t + hPh[h]);
    return (float) (amp * s);
}

// blk 単位で流す共通ドライバ。out に処理後を貯める
static void run (gz::hum::Killer& k, std::vector<float>& sig, std::vector<float>& out)
{
    const int blk = 128;
    out.resize (sig.size());
    for (size_t i = 0; i < sig.size(); i += blk)
    {
        const int n = (int) std::min ((size_t) blk, sig.size() - i);
        std::vector<float> tmp (sig.begin() + i, sig.begin() + i + n);
        k.process (tmp.data(), nullptr, n);
        for (int j = 0; j < n; ++j) out[i + j] = tmp[j];
    }
}

int main()
{
    std::printf ("HumKiller @ %.0f Hz\n", kSR);
    const size_t N = (size_t) (kSR * 16.0);          // 16秒
    const size_t tail0 = (size_t) (kSR * 13.0), tail1 = N;   // 最後の3秒で評価

    // ---------- 1 / 2: ハムだけ ----------
    for (int t = 0; t < 2; ++t)
    {
        const double f0 = (t == 0) ? 50.0 : 60.0;
        gz::hum::Killer k; k.prepare (kSR); k.setAmount (1.0f);
        std::vector<float> sig (N), out;
        for (size_t i = 0; i < N; ++i) sig[i] = humSample (f0, 0.02, (long) i);   // -34dBFS級
        run (k, sig, out);

        const double inDb  = rmsDb (sig, tail0, tail1);
        const double outDb = rmsDb (out, tail0, tail1);
        CHECK (k.detectedHz() == (int) f0, "%.0fHzハム: 検出 %d Hz (正解 %.0f)", f0, k.detectedHz(), f0);
        CHECK (inDb - outDb > 20.0, "%.0fHzハム: %.1f dB 減った (>20dB)", f0, inDb - outDb);
        const double a1in  = ampAt (sig, f0, tail0, tail1);
        const double a1out = ampAt (out, f0, tail0, tail1);
        CHECK (20.0 * std::log10 (a1out / a1in) < -25.0,
               "%.0fHzハム: 基本波 %.1f dB (<-25dB)", f0, 20.0 * std::log10 (a1out / a1in));
        const double a3in  = ampAt (sig, f0 * 3, tail0, tail1);
        const double a3out = ampAt (out, f0 * 3, tail0, tail1);
        CHECK (20.0 * std::log10 (a3out / a3in) < -18.0,
               "%.0fHzハム: 第3倍音(%.0fHz) %.1f dB (<-18dB)", f0, f0 * 3,
               20.0 * std::log10 (a3out / a3in));
    }

    // ---------- 3: 歌だけ(ハム無し) → 何もしない ----------
    {
        gz::hum::Killer k; k.prepare (kSR); k.setAmount (1.0f);
        Voice v; std::vector<float> sig (N), out;
        for (size_t i = 0; i < N; ++i)
        {
            const double t = i / kSR;
            const bool sing = std::fmod (t, 2.8) < 2.2;
            sig[i] = sing ? v.sample (196.0 + 40.0 * std::sin (2 * M_PI * t / 7.0), 0.5) : 0.0f;
        }
        run (k, sig, out);
        double maxDiff = 0.0;
        for (size_t i = 0; i < N; ++i) maxDiff = std::max (maxDiff, (double) std::fabs (out[i] - sig[i]));
        CHECK (k.detectedHz() == 0, "ハム無し: 検出 %d (0が正)", k.detectedHz());
        CHECK (maxDiff < 1.0e-6, "ハム無し: 波形の最大差 %.2g (実質ゼロ)", maxDiff);
    }

    // ---------- 4: 歌 + 50Hzハム ----------
    {
        gz::hum::Killer k; k.prepare (kSR); k.setAmount (1.0f);
        Voice v, v2; std::vector<float> sig (N), voiceOnly (N), out;
        for (size_t i = 0; i < N; ++i)
        {
            const double t = i / kSR;
            const bool sing = std::fmod (t, 2.8) < 2.2;
            const float vv = sing ? v.sample (196.0 + 40.0 * std::sin (2 * M_PI * t / 7.0), 0.5) : 0.0f;
            voiceOnly[i] = vv;
            sig[i] = vv + humSample (50.0, 0.012, (long) i);
        }
        run (k, sig, out);
        CHECK (k.detectedHz() == 50, "歌+ハム: 検出 %d Hz (50が正)", k.detectedHz());
        // ハムの減り具合は「歌を引いた残り」で測る。
        // 出力をそのまま測ると、200Hz などは歌の倍音がそこに乗っているので
        // 「減っていない」ように見えてしまい、正しく評価できない。
        std::vector<float> humIn (N), humOut (N);
        for (size_t i = 0; i < N; ++i)
        { humIn[i] = sig[i] - voiceOnly[i]; humOut[i] = out[i] - voiceOnly[i]; }
        for (int h = 1; h <= 4; ++h)
        {
            const double f = 50.0 * h;
            const double aIn  = ampAt (humIn,  f, tail0, tail1);
            const double aOut = ampAt (humOut, f, tail0, tail1);
            CHECK (20.0 * std::log10 (aOut / aIn) < -12.0,
                   "歌+ハム: %.0fHz のハム成分が %.1f dB (<-12dB)", f,
                   20.0 * std::log10 (aOut / aIn));
        }
        // 歌そのものが痩せていないか: 元の歌だけとの差(残差)を測る
        double se = 0.0, sv = 0.0;
        for (size_t i = tail0; i < tail1; ++i)
        { const double d = out[i] - voiceOnly[i]; se += d * d; sv += (double) voiceOnly[i] * voiceOnly[i]; }
        const double resDb = 10.0 * std::log10 (se / (sv + 1e-30));
        CHECK (resDb < -24.0, "歌+ハム: 歌との残差 %.1f dB (<-24dB = 歌はほぼ無傷)", resDb);
    }

    // ---------- 5: 男声の低い持続音を誤って削らないか ----------
    {
        for (double f0 : { 110.0, 123.5 })   // A2 / B2 (男性のいちばん低いあたり)
        {
            gz::hum::Killer k; k.prepare (kSR); k.setAmount (1.0f);
            std::vector<float> sig (N), out;
            for (size_t i = 0; i < N; ++i)
                sig[i] = (float) (0.30 * std::sin (2 * M_PI * f0 * i / kSR));   // ずっと同じ音
            run (k, sig, out);
            const double aIn  = ampAt (sig, f0, tail0, tail1);
            const double aOut = ampAt (out, f0, tail0, tail1);
            const double dB   = 20.0 * std::log10 (aOut / aIn);
            CHECK (dB > -1.0, "持続音 %.1fHz: 変化 %.2f dB (>-1dB = 削られていない)", f0, dB);
        }
    }

    // ---------- 6: 60Hz付近の持続低音(B1) を誤検出しないか ----------
    {
        gz::hum::Killer k; k.prepare (kSR); k.setAmount (1.0f);
        std::vector<float> sig (N), out;
        for (size_t i = 0; i < N; ++i)
            sig[i] = (float) (0.25 * std::sin (2 * M_PI * 61.74 * i / kSR));    // B1
        run (k, sig, out);
        const double aIn  = ampAt (sig, 61.74, tail0, tail1);
        const double aOut = ampAt (out, 61.74, tail0, tail1);
        const double dB   = 20.0 * std::log10 (aOut / aIn);
        // 61.74Hz は 60Hz から 1.74Hz ずれている。適応は追えないので残るのが正しい
        CHECK (dB > -3.0, "B1(61.7Hz)持続音: 変化 %.2f dB (>-3dB = 消されない)", dB);
    }

    // ---------- 7: ハムが途中で消えたら解除されるか(ケーブルを直した等) ----------
    {
        gz::hum::Killer k; k.prepare (kSR); k.setAmount (1.0f);
        std::vector<float> sig (N), out;
        for (size_t i = 0; i < N; ++i)
            sig[i] = (i < N / 2) ? humSample (50.0, 0.02, (long) i) : 0.0f;
        run (k, sig, out);
        CHECK (k.detectedHz() == 0, "ハム停止後: 検出 %d (0に戻るのが正)", k.detectedHz());
        double peakAfter = 0.0;
        for (size_t i = tail0; i < tail1; ++i) peakAfter = std::max (peakAfter, (double) std::fabs (out[i]));
        CHECK (peakAfter < 1.0e-4, "ハム停止後: 出力の残り %.2g (何も足していない)", peakAfter);
    }

    // ---------- 8: 低い声の持続音(100Hz=50Hzの2倍音)＋ハム ----------
    // いちばん厳しい条件。ハムの倍音とまったく同じ高さを男性が伸ばした場合。
    {
        gz::hum::Killer k; k.prepare (kSR); k.setAmount (1.0f);
        Voice v; std::vector<float> sig (N), noteOnly (N), out;
        for (size_t i = 0; i < N; ++i)
        {
            noteOnly[i] = v.sample (100.0, 0.5);            // 倍音もビブラートもある本物の声
            sig[i] = noteOnly[i] + humSample (50.0, 0.02, (long) i);
        }
        run (k, sig, out);
        double se = 0.0, sv = 0.0;
        for (size_t i = tail0; i < tail1; ++i)
        { const double d = out[i] - noteOnly[i]; se += d * d; sv += (double) noteOnly[i] * noteOnly[i]; }
        CHECK (10.0 * std::log10 (se / sv) < -20.0,
               "100Hz持続+ハム: 声との残差 %.1f dB (<-20dB = 声は残る)",
               10.0 * std::log10 (se / sv));
    }

    // ---------- 9: 計算負荷 ----------
    {
        gz::hum::Killer k; k.prepare (kSR); k.setAmount (1.0f);
        std::vector<float> L ((size_t) kSR * 10), R ((size_t) kSR * 10);
        for (size_t i = 0; i < L.size(); ++i)
        { L[i] = humSample (50.0, 0.02, (long) i); R[i] = L[i] * 0.9f; }
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i + 128 <= L.size(); i += 128)
            k.process (L.data() + i, R.data() + i, 128);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double load = sec / 10.0 * 100.0;
        CHECK (load < 5.0, "負荷: ステレオ実時間比 %.2f %% (<5%%)", load);
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
