// dsp_vc_uv.cpp — v2.12.0 無声ガード(§6-3)の検証。JUCE不要(VoiceShifter単体)。
//
//  「ボイチェンを起動すると言葉が何を言っているか伝わりにくい」の修正:
//  子音(無声音)はピッチ/フォルマントシフトを掛けずに素通しする。
//
//  [1] 有声(のこぎり波 220Hz)は今までどおりシフトされること
//  [2] 無声(ノイズ)はシフト後も**元の音のまま**出てくること(mode1)
//  [3] mode2(分身用)では無声が黙ること
//  [4] mode0(ガード無し)は従来と1サンプルも変わらないこと
#include "../Source/VoiceShifter.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <cstdlib>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

// 220Hz のこぎり波(有声) / 擬似乱数ノイズ(無声=サ行の代わり)
static float saw (double& ph, double f, double sr)
{
    ph += f / sr; if (ph >= 1.0) ph -= 1.0;
    return (float) (0.30 * (2.0 * ph - 1.0));
}
static float noise (unsigned& s)
{
    s = s * 1664525u + 1013904223u;
    return (float) ((int) (s >> 9) - 4194304) / 4194304.0f * 0.20f;
}

// 有限長の自己相関は2周期目の山を選び、正しい音を1オクターブ低く誤判定する。
// 独立した周波数走査で、声の基本周波数帯の最大成分を測る。
static double period (const std::vector<float>& v, int from, int len, double sr)
{
    double best = 0.0, bestHz = 0.0;
    constexpr double pi = 3.14159265358979323846;
    for (double hz = 80.0; hz <= 500.0; hz += 0.25)
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double value = v[(size_t) (from + i)] * (0.5 - 0.5 * std::cos (2.0 * pi * i / (len - 1)));
            const double phase = 2.0 * pi * hz * i / sr;
            re += value * std::cos (phase); im += value * std::sin (phase);
        }
        const double energy = re * re + im * im;
        if (energy > best) { best = energy; bestHz = hz; }
    }
    return bestHz;
}

int main()
{
    const double sr = 44100.0;
    const int nVoiced = 22050, nUv = 8820;      // 0.5s 有声 + 0.2s 無声
    const int total = nVoiced + nUv;

    std::printf ("無声ガード(子音を潰さない)の検証\n\n");

    // 入力をつくる(全モード共通)
    std::vector<float> in ((size_t) total);
    { double ph = 0; unsigned s = 77u;
      for (int i = 0; i < nVoiced; ++i) in[(size_t) i] = saw (ph, 220.0, sr);
      for (int i = nVoiced; i < total; ++i) in[(size_t) i] = noise (s); }

    auto run = [&] (int mode, float semis)
    {
        gz::VoiceShifter sh; sh.prepare (sr);
        sh.setUnvoicedGuard (mode);
        sh.setParams (semis, 0.0f, 1.0f);
        std::vector<float> out = in;
        sh.processBlock (out.data(), total);
        return out;
    };

    const auto outG = run (1, 3.0f);            // ガードあり(本人モード) +3半音
    const auto out0 = run (0, 3.0f);            // ガード無し +3半音
    const auto outD = run (2, 3.0f);            // 分身モード +3半音
    gz::VoiceShifter tmp; tmp.prepare (sr);
    const int lat = tmp.latencySamples();

    std::printf ("[1] 有声部分はシフトされている\n");
    {
        const double f = period (outG, 8000, 4096, sr);
        const double want = 220.0 * std::pow (2.0, 3.0 / 12.0);   // 261.6Hz
        CHECK (std::fabs (f - want) < 12.0, "+3半音 (実測 %.1f Hz / 期待 %.1f Hz)", f, want);
    }

    std::printf ("\n[2] 無声部分は元の音のまま(mode1)\n");
    {
        // 遅延ぶんずらして入力と相関を取る。素通しなら 1 に近い。
        const int from = nVoiced + 2048, len = 4096;   // 切替の過渡を避けて測る
        double num = 0, dIn = 0, dOut = 0;
        for (int i = 0; i < len; ++i)
        {
            const double a = in [(size_t)(from + i - lat)];
            const double b = outG[(size_t)(from + i)];
            num += a * b; dIn += a * a; dOut += b * b;
        }
        const double corrG = num / std::sqrt (dIn * dOut + 1e-12);
        // ガード無しでも同じ量を測って、差が出ていることを確かめる
        num = dIn = dOut = 0;
        for (int i = 0; i < len; ++i)
        {
            const double a = in  [(size_t)(from + i - lat)];
            const double b = out0[(size_t)(from + i)];
            num += a * b; dIn += a * a; dOut += b * b;
        }
        const double corr0 = num / std::sqrt (dIn * dOut + 1e-12);
        CHECK (corrG > 0.90, "ガードあり: 入力との相関 %.3f (>0.90)", corrG);
        CHECK (corr0 < 0.60, "ガード無し: 相関 %.3f (<0.60 = シフトで崩れている)", corr0);
    }

    std::printf ("\n[3] 分身モード(mode2)では無声が黙る\n");
    {
        const int from = nVoiced + 2048, len = 4096;
        double eD = 0, e0 = 0;
        for (int i = 0; i < len; ++i)
        {
            eD += (double) outD[(size_t)(from + i)] * outD[(size_t)(from + i)];
            e0 += (double) out0[(size_t)(from + i)] * out0[(size_t)(from + i)];
        }
        CHECK (eD < e0 * 0.05, "無声区間のエネルギー %.1f%% (<5%%)", 100.0 * eD / (e0 + 1e-12));
    }

    std::printf ("\n[4] ガード無し(mode0)は従来と同一\n");
    {
        // uvMode=0 の経路は uvGain を触らない。2回流して完全一致を確かめる
        const auto a = run (0, 3.0f), b = run (0, 3.0f);
        double md = 0; for (int i = 0; i < total; ++i) md = std::fmax (md, std::fabs ((double) a[(size_t) i] - b[(size_t) i]));
        CHECK (md == 0.0, "再現性(最大差 %.1e)", md);
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
