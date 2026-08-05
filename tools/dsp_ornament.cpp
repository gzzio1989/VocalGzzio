// dsp_ornament.cpp — こぶし・しゃくり保護(Ornament.h)の数値検証 (v2.8.0)
//
//  「補正を残す割合(keep)」が、場面ごとに正しく動くかを数値で確かめる。
//   1: まっすぐな音痴（40セント下）      → 保護しない（keep≈1）＝ちゃんと直る
//   2: しゃくり（120msで2半音上げて着地） → 出だしだけ保護、着地後は戻る
//   3: こぶし（±1半音の上下動を2往復）   → 動いている間だけ保護
//   4: ビブラート ±0.4半音 5.5Hz         → 保護しない（＝補正が効き続ける）
//   5: 深いビブラート ±0.9半音 5.5Hz     → 規則性で見分けて保護しない
//   6: 無声（息継ぎ）                     → keep=1
//   7: 効果0%                             → 常に keep=1（従来と完全に同じ）
//   8: 音の乗り換え（レガートで3半音上）   → 乗り換え直後だけ保護
//   9: バッファ128 / 4096 で効きが同じか   → 実時間で数えているかの確認(v2.8.0)
//
// ビルド: g++ -O2 -std=c++17 -I../Source dsp_ornament.cpp -o dsp_ornament
#include "../Source/Ornament.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <utility>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static constexpr double kSR = 48000.0;
static constexpr int    kBlock = 128;                    // ピッチ検出はブロックごと
static const float      kDt = (float) (kBlock / kSR);    // 約2.67ms

// 音高の並び(半音)を流して keep を集める。無声は 0 以下で表す。
static std::vector<float> run (gz::orn::Guard& g, const std::vector<float>& pitch)
{
    std::vector<float> keep; keep.reserve (pitch.size());
    for (float p : pitch) keep.push_back (g.process (p, kDt));
    return keep;
}
static int nBlocks (double sec) { return (int) std::lround (sec / kDt); }
static double mean (const std::vector<float>& v, int a, int b)
{
    a = std::max (0, a); b = std::min ((int) v.size(), b);
    if (b <= a) return 0.0;
    double s = 0; for (int i = a; i < b; ++i) s += v[i];
    return s / (b - a);
}
static double minv (const std::vector<float>& v, int a, int b)
{
    a = std::max (0, a); b = std::min ((int) v.size(), b);
    double m = 1e9; for (int i = a; i < b; ++i) m = std::min (m, (double) v[i]);
    return m;
}

int main()
{
    std::printf ("こぶし・しゃくり保護 @ %.0fHz / %dサンプルごとに判定 (%.2fms)\n",
                 kSR, kBlock, kDt * 1000.0);

    // ---------- 1: まっすぐな音痴 ----------
    {
        gz::orn::Guard g; g.prepare (kSR); g.setAmount (1.0f);
        std::vector<float> p (nBlocks (2.0), 60.0f - 0.40f);   // 40セント下でずっと一定
        auto k = run (g, p);
        const double m = mean (k, nBlocks (0.3), (int) k.size());
        CHECK (m > 0.98, "まっすぐな音痴: 補正を残す割合 %.3f (>0.98 = しっかり直る)", m);
    }

    // ---------- 2: しゃくり ----------
    {
        gz::orn::Guard g; g.prepare (kSR); g.setAmount (1.0f);
        std::vector<float> p;
        const int scoop = nBlocks (0.12);
        for (int i = 0; i < scoop; ++i)                        // 120msで2半音駆け上がる
            p.push_back (58.0f + 2.0f * (float) i / scoop);
        for (int i = 0; i < nBlocks (1.0); ++i) p.push_back (60.0f);   // 着地して伸ばす
        auto k = run (g, p);
        const double duringLo = minv (k, 0, scoop);
        const double after    = mean (k, scoop + nBlocks (0.35), (int) k.size());
        CHECK (duringLo < 0.35, "しゃくり: 出だしの最小 %.3f (<0.35 = 保護されている)", duringLo);
        CHECK (after > 0.90, "しゃくり: 着地350ms後 %.3f (>0.90 = 補正が戻る)", after);
    }

    // ---------- 3: こぶし ----------
    {
        gz::orn::Guard g; g.prepare (kSR); g.setAmount (1.0f);
        std::vector<float> p;
        const int pre = nBlocks (0.6);
        for (int i = 0; i < pre; ++i) p.push_back (60.0f);
        // ±1半音の上下動を2往復（1往復 240ms ≒ 4.2Hz 相当だが2回で終わる）
        const int wig = nBlocks (0.48);
        for (int i = 0; i < wig; ++i)
            p.push_back (60.0f + 1.0f * std::sin (2.0 * M_PI * 2.0 * (i * kDt) / 0.48 * 0.48 / 0.24 * 0.5));
        const int post = nBlocks (1.0);
        for (int i = 0; i < post; ++i) p.push_back (60.0f);
        auto k = run (g, p);
        const double duringLo = minv (k, pre, pre + wig);
        const double after    = mean (k, pre + wig + nBlocks (0.4), (int) k.size());
        const double before   = mean (k, nBlocks (0.2), pre);
        CHECK (before > 0.95, "こぶし前の伸ばし: %.3f (>0.95 = 触らない)", before);
        CHECK (duringLo < 0.40, "こぶし中の最小: %.3f (<0.40 = 保護されている)", duringLo);
        CHECK (after > 0.90, "こぶし後400ms: %.3f (>0.90 = 補正が戻る)", after);
    }

    // ---------- 4 / 5: ビブラート（浅い・深い） ----------
    for (double dep : { 0.4, 0.9 })
    {
        gz::orn::Guard g; g.prepare (kSR); g.setAmount (1.0f);
        std::vector<float> p;
        for (int i = 0; i < nBlocks (2.5); ++i)
            p.push_back ((float) (60.0 + dep * std::sin (2.0 * M_PI * 5.5 * i * kDt)));
        auto k = run (g, p);
        const double m = mean (k, nBlocks (0.6), (int) k.size());
        CHECK (m > 0.80, "ビブラート ±%.1f半音 5.5Hz: %.3f (>0.80 = こぶしと誤認しない)", dep, m);
    }

    // ---------- 6: 無声 ----------
    {
        gz::orn::Guard g; g.prepare (kSR); g.setAmount (1.0f);
        std::vector<float> p (nBlocks (1.0), 0.0f);          // ずっと無声
        auto k = run (g, p);
        CHECK (mean (k, 0, (int) k.size()) > 0.999, "無声: keep=%.4f (1.0 = 何もしない)",
               mean (k, 0, (int) k.size()));
    }

    // ---------- 7: 効果0% は完全に従来どおり ----------
    {
        gz::orn::Guard g; g.prepare (kSR); g.setAmount (0.0f);
        std::vector<float> p;
        for (int i = 0; i < nBlocks (0.12); ++i) p.push_back (58.0f + 2.0f * i * kDt / 0.12f);
        for (int i = 0; i < nBlocks (0.5); ++i)  p.push_back (60.0f);
        auto k = run (g, p);
        bool allOne = true; for (float v : k) if (v != 1.0f) allOne = false;
        CHECK (allOne, "0%%: すべて keep=1.0（従来と完全に同じ）");
    }

    // ---------- 8: 音の乗り換え（レガートで3半音上へ） ----------
    {
        gz::orn::Guard g; g.prepare (kSR); g.setAmount (1.0f);
        std::vector<float> p;
        const int a = nBlocks (0.8);
        for (int i = 0; i < a; ++i) p.push_back (60.0f);
        const int slide = nBlocks (0.08);
        for (int i = 0; i < slide; ++i) p.push_back (60.0f + 3.0f * i / slide);
        const int b = nBlocks (1.0);
        for (int i = 0; i < b; ++i) p.push_back (63.0f);
        auto k = run (g, p);
        const double during = minv (k, a, a + slide + nBlocks (0.05));
        const double after  = mean (k, a + slide + nBlocks (0.45), (int) k.size());
        CHECK (during < 0.5, "音の乗り換え中: %.3f (<0.5 = 渡りは保護)", during);
        CHECK (after > 0.90, "乗り換え後450ms: %.3f (>0.90 = 新しい音は補正する)", after);
    }

    // ---------- 9: バッファサイズを変えても効きが変わらないか (v2.8.0) ----------
    // 以前は dt を 50ms で頭打ちにしていたので、4096サンプル(85ms)のときだけ
    // 時間の進みが遅くなり、同じ歌でも保護が長引いていた。
    {
        // 同じ「しゃくり→伸ばし」を、判定間隔だけ変えて2回流す。
        auto runAt = [] (double blockSamples)
        {
            const float dt = (float) (blockSamples / kSR);
            gz::orn::Guard g; g.prepare (kSR); g.setAmount (1.0f);
            const double scoopSec = 0.12, holdSec = 1.20;
            std::vector<float> keep; std::vector<double> tsec;
            double t = 0.0;
            while (t < scoopSec + holdSec)
            {
                const float pitch = (t < scoopSec) ? (float) (58.0 + 2.0 * (t / scoopSec)) : 60.0f;
                keep.push_back (g.process (pitch, dt));
                tsec.push_back (t);
                t += dt;
            }
            // 「着地から 0.35 秒後」に補正が戻っているかを実時間で見る
            double at = 1.0;
            for (size_t i = 0; i < tsec.size(); ++i)
                if (tsec[i] >= scoopSec + 0.35) { at = keep[i]; break; }
            double lo = 1.0;
            for (size_t i = 0; i < tsec.size(); ++i)
                if (tsec[i] <= scoopSec) lo = std::min (lo, (double) keep[i]);
            return std::pair<double, double> (lo, at);
        };
        const auto small = runAt (128.0);
        const auto big   = runAt (4096.0);
        CHECK (std::fabs (small.second - big.second) < 0.06,
               "バッファ128と4096で着地350ms後が一致: %.3f / %.3f (差<0.06)", small.second, big.second);
        CHECK (small.first < 0.35 && big.first < 0.35,
               "どちらのバッファでも出だしは保護: %.3f / %.3f (<0.35)", small.first, big.first);
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
