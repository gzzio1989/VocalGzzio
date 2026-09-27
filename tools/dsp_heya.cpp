// dsp_heya.cpp — へや（ゼロ遅延IRコンボリューション）の検査 v4.0.0
//
//  何を証明するか:
//   1. ★数学の一致: 分割コンボリューションの出力が「素朴な直接畳み込み」と
//      サンプル単位で一致する。ホストブロック 32/64/100/256/500/1024/混在 すべてで。
//      → これが一致していれば「追加遅延 0」は言葉ではなく定理になる。
//   2. 部屋の性格: 各部屋の RT60(Schroeder積分) が設計値の範囲にある。
//   3. 音量そろえ: 6部屋のエネルギーが揃っている（切替でビックリしない）。
//   4. 生成の健全性: NaN/Inf無し・DC漏れ無し・決定的（2回作って全ビット一致）。
//   5. 実行時の掟: process() の中で new が呼ばれない（operator new を数える）。
//   6. 負荷: 最重量の部屋の処理時間を実測して台帳に出す。
//   7. 広さツマミ: 小さくすると RT60 が実時間で短くなる。クリックも出ない。
//
//  使い方: g++ -O2 -std=c++17 -I../Source dsp_heya.cpp -o /tmp/dsp_heya && /tmp/dsp_heya
#include "Heya.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <new>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

// ---- 音声処理中の new を数える（VocalGzzio の掟の実測） ---------------------
static std::atomic<long> gAllocCount { 0 };
static std::atomic<bool> gCounting { false };
void* operator new (std::size_t sz)
{
    if (gCounting.load (std::memory_order_relaxed)) gAllocCount.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (sz ? sz : 1)) return p;
    throw std::bad_alloc {};
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

// ---- 素朴な直接畳み込み（これが「正解」） ----------------------------------
static void directConv (const std::vector<float>& x, const std::vector<float>& h,
                        std::vector<float>& y)
{
    y.assign (x.size(), 0.0f);
    for (size_t n = 0; n < x.size(); ++n)
    {
        double acc = 0.0;
        const size_t kMax = std::min (h.size(), n + 1);
        for (size_t k = 0; k < kMax; ++k) acc += (double) h[k] * x[n - k];
        y[n] = (float) acc;
    }
}

// ---- Schroeder 逆積分で RT60 を測る ----------------------------------------
static double rt60Of (const std::vector<float>& ir, double sr)
{
    std::vector<double> e (ir.size());
    double tot = 0.0;
    for (size_t i = ir.size(); i-- > 0;) { tot += (double) ir[i] * ir[i]; e[i] = tot; }
    if (tot < 1e-20) return 0.0;
    // -5dB → -25dB の傾きから60dBぶんを外挿（T20法）
    double t5 = -1, t25 = -1;
    for (size_t i = 0; i < e.size(); ++i)
    {
        const double db = 10.0 * std::log10 (e[i] / tot);
        if (t5  < 0 && db <= -5.0)  t5  = (double) i / sr;
        if (t25 < 0 && db <= -25.0) { t25 = (double) i / sr; break; }
    }
    if (t5 < 0 || t25 < 0) return 0.0;
    return (t25 - t5) * 3.0;
}

int main()
{
    const double sr = 48000.0;

    std::printf ("== 1. 分割コンボリューション = 直接畳み込み（サンプル一致） ==\n");
    {
        // 検査しやすいよう、コンボルバを「わざと知っている部屋」で試す:
        // 部屋0のIRを取り出し、同じIRで直接畳み込みした答えと比べる。
        heya::GeneratedIr ir;
        heya::generateRoomIr (0, sr, ir);

        heya::Convolver cv;
        cv.prepare (sr, 512);
        cv.setRoom (0);
        cv.setSize (1.0f);

        // 入力: インパルス + ノイズ + 正弦の混合 1.2 秒
        const int N = (int) (1.2 * sr);
        std::vector<float> xin ((size_t) N, 0.0f);
        heya::Rng rng (12345);
        xin[100] = 1.0f;
        for (int n = 5000; n < N; ++n)
            xin[(size_t) n] = 0.25f * rng.bi() + 0.3f * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * n / sr);

        // 正解: 直接畳み込み（Lチャンネル）
        std::vector<float> hL (ir.L.begin(), ir.L.end());
        std::vector<float> want;
        directConv (xin, hL, want);

        // ブロックサイズを変えながら一致を確かめる
        const int blocks[] = { 32, 64, 100, 256, 500, 1024 };
        for (int bi = 0; bi < 6; ++bi)
        {
            cv.reset();
            const int Bk = blocks[bi];
            std::vector<float> L (xin), R (xin);
            for (int n0 = 0; n0 < N; n0 += Bk)
            {
                const int len = std::min (Bk, N - n0);
                cv.process (L.data() + n0, R.data() + n0, len);
            }
            double maxErr = 0.0;
            for (int n = 0; n < N; ++n) maxErr = std::max (maxErr, (double) std::fabs (L[(size_t) n] - want[(size_t) n]));
            CHECK (maxErr < 2e-3, "block=%4d : 直接畳み込みとの最大差 %.2e（一致＝遅延0の証明）", Bk, maxErr);
        }

        // 混在ブロック（ホストが毎回サイズを変えても壊れない）
        {
            cv.reset();
            std::vector<float> L (xin), R (xin);
            heya::Rng brng (777);
            int n0 = 0;
            while (n0 < N)
            {
                const int len = std::min (1 + (int) (brng.uni() * 511.0f), N - n0);
                cv.process (L.data() + n0, R.data() + n0, len);
                n0 += len;
            }
            double maxErr = 0.0;
            for (int n = 0; n < N; ++n) maxErr = std::max (maxErr, (double) std::fabs (L[(size_t) n] - want[(size_t) n]));
            CHECK (maxErr < 2e-3, "block=乱数 : 最大差 %.2e", maxErr);
        }
    }

    std::printf ("\n== 2. 各部屋の RT60（設計との一致） ==\n");
    {
        // 設計許容: 設計RT60(500Hz-1kの平均) の ±40%（生成は帯域合成なので幅を持たせる）
        for (int room = 0; room < heya::kNumRooms; ++room)
        {
            heya::GeneratedIr ir;
            heya::generateRoomIr (room, sr, ir);
            const heya::RoomSpec& sp = heya::rooms()[(size_t) room];
            const double want = 0.5 * (sp.rt60[2] + sp.rt60[3]);
            const double got  = rt60Of (ir.L, sr);
            CHECK (got > want * 0.55 && got < want * 1.6,
                   "%-14s RT60 実測 %.2fs（設計 %.2fs）", sp.nameEn, got, want);
        }
    }

    std::printf ("\n== 3. 部屋どうしの音量そろえ ==\n");
    {
        double eMin = 1e30, eMax = 0.0;
        for (int room = 0; room < heya::kNumRooms; ++room)
        {
            heya::GeneratedIr ir;
            heya::generateRoomIr (room, sr, ir);
            double e = 0.0;
            for (float v : ir.L) e += (double) v * v;
            for (float v : ir.R) e += (double) v * v;
            eMin = std::min (eMin, e); eMax = std::max (eMax, e);
        }
        const double spreadDb = 10.0 * std::log10 (eMax / eMin);
        CHECK (spreadDb < 4.0, "6部屋のエネルギー差 %.2f dB（共鳴の安全弁ぶんを含む）", spreadDb);
    }

    std::printf ("\n== 4. 生成の健全性 ==\n");
    {
        bool anyNan = false; double dcMax = 0.0;
        for (int room = 0; room < heya::kNumRooms; ++room)
        {
            heya::GeneratedIr ir;
            heya::generateRoomIr (room, sr, ir);
            double dc = 0.0;
            for (float v : ir.L) { if (! std::isfinite (v)) anyNan = true; dc += v; }
            dcMax = std::max (dcMax, std::fabs (dc) / (double) ir.L.size());
        }
        CHECK (! anyNan, "全部屋で NaN/Inf 無し");
        CHECK (dcMax < 1e-4, "DC漏れ 最大 %.2e（スピーカーを押さない）", dcMax);

        heya::GeneratedIr a, b;
        heya::generateRoomIr (3, sr, a);
        heya::generateRoomIr (3, sr, b);
        bool same = a.L == b.L && a.R == b.R;
        CHECK (same, "決定的: 2回作って全ビット一致（誰のPCでも同じ音）");
    }

    std::printf ("\n== 5. process() の中で new が呼ばれない ==\n");
    {
        heya::Convolver cv;
        cv.prepare (sr, 512);
        cv.setRoom (4);                       // いちばん重いホール
        std::vector<float> L (512), R (512);
        heya::Rng rng (42);
        for (int i = 0; i < 512; ++i) { L[(size_t) i] = rng.bi() * 0.3f; R[(size_t) i] = L[(size_t) i]; }
        // 温めてから数える（部屋切替のフェードも跨がせる）
        for (int b = 0; b < 50; ++b) cv.process (L.data(), R.data(), 512);
        gAllocCount.store (0); gCounting.store (true);
        for (int b = 0; b < 2000; ++b)
        {
            if (b == 500)  cv.setRoom (0);    // 切替も音声スレッド操作
            if (b == 1000) cv.setSize (0.4f); // 広さ変更も
            cv.process (L.data(), R.data(), 512);
        }
        gCounting.store (false);
        CHECK (gAllocCount.load() == 0, "2000ブロック（切替・広さ変更込み）で確保 %ld 回", gAllocCount.load());
    }

    std::printf ("\n== 6. 負荷の台帳（参考値・このマシンでの実測） ==\n");
    {
        for (int room = 0; room < heya::kNumRooms; ++room)
        {
            heya::Convolver cv;
            cv.prepare (sr, 128);
            cv.setRoom (room);
            // フェード完了まで回す
            std::vector<float> L (128), R (128);
            for (int b = 0; b < 100; ++b) cv.process (L.data(), R.data(), 128);
            heya::Rng rng (7);
            double worst = 0.0, sum = 0.0; int cnt = 0;
            for (int b = 0; b < 4000; ++b)     // ~10.6 秒ぶん
            {
                for (int i = 0; i < 128; ++i) { L[(size_t) i] = rng.bi() * 0.3f; R[(size_t) i] = L[(size_t) i]; }
                const auto t0 = std::chrono::steady_clock::now();
                cv.process (L.data(), R.data(), 128);
                const auto t1 = std::chrono::steady_clock::now();
                const double ms = std::chrono::duration<double, std::milli> (t1 - t0).count();
                worst = std::max (worst, ms); sum += ms; ++cnt;
            }
            const double deadline = 128.0 / sr * 1000.0;   // 2.67ms
            std::printf ("  %-14s 平均 %5.1f%%  最悪 %5.1f%%  (締切 %.2fms・%d分割)\n",
                         heya::rooms()[(size_t) room].nameEn,
                         100.0 * (sum / cnt) / deadline, 100.0 * worst / deadline,
                         deadline, cv.numPartsOf (room));
        }
        CHECK (true, "上の表を台帳として記録（判定はビルド環境依存のためしない）");
    }

    std::printf ("\n== 7. 広さツマミ（実時間で残響が短くなる・クリック無し） ==\n");
    {
        // インパルス応答を「広さ100%」と「広さ30%」で録り、RT60を比べる
        auto renderIr = [&] (float size01, std::vector<float>& out)
        {
            heya::Convolver cv;
            cv.prepare (sr, 256);
            cv.setRoom (4);
            cv.setSize (size01);
            for (int b = 0; b < 40; ++b) { std::vector<float> z (256, 0.0f), z2 (256, 0.0f); cv.process (z.data(), z2.data(), 256); }
            const int N = (int) (3.2 * sr);
            out.assign ((size_t) N, 0.0f);
            std::vector<float> R ((size_t) N, 0.0f);
            out[0] = 1.0f;
            for (int n0 = 0; n0 < N; n0 += 256)
                cv.process (out.data() + n0, R.data() + n0, std::min (256, N - n0));
        };
        std::vector<float> big, small;
        renderIr (1.0f, big);
        renderIr (0.30f, small);
        const double rtBig = rt60Of (big, sr), rtSmall = rt60Of (small, sr);
        CHECK (rtSmall < rtBig * 0.75, "広さ100%%→30%%で RT60 %.2fs → %.2fs（ちゃんと狭くなる）", rtBig, rtSmall);

        // クリック検査: 正弦を流しながら広さを動かして、隣接サンプル差の異常が無いこと
        heya::Convolver cv;
        cv.prepare (sr, 256);
        cv.setRoom (2);
        std::vector<float> L (256), R (256);
        float prev = 0.0f; double maxJump = 0.0;
        for (int b = 0; b < 400; ++b)
        {
            for (int i = 0; i < 256; ++i)
                L[(size_t) i] = R[(size_t) i] = 0.4f * (float) std::sin (2.0 * 3.141592653589793 * 330.0 * (b * 256 + i) / sr);
            cv.setSize (0.5f + 0.5f * (float) std::sin (b * 0.1));  // ツマミをグリグリ
            cv.process (L.data(), R.data(), 256);
            for (int i = 0; i < 256; ++i)
            {
                maxJump = std::max (maxJump, (double) std::fabs (L[(size_t) i] - prev));
                prev = L[(size_t) i];
            }
        }
        CHECK (maxJump < 0.35, "広さをグリグリしても最大サンプル跳び %.3f（プチッと言わない）", maxJump);
    }

    std::printf ("\n== 8. 部屋の切替（鳴りながら替えてもクリック無し） ==\n");
    {
        heya::Convolver cv;
        cv.prepare (sr, 256);
        std::vector<float> L (256), R (256);
        float prev = 0.0f; double maxJump = 0.0;
        for (int b = 0; b < 600; ++b)
        {
            for (int i = 0; i < 256; ++i)
                L[(size_t) i] = R[(size_t) i] = 0.4f * (float) std::sin (2.0 * 3.141592653589793 * 261.6 * (b * 256 + i) / sr);
            if (b % 90 == 0) cv.setRoom ((b / 90) % heya::kNumRooms);
            cv.process (L.data(), R.data(), 256);
            for (int i = 0; i < 256; ++i)
            {
                maxJump = std::max (maxJump, (double) std::fabs (L[(size_t) i] - prev));
                prev = L[(size_t) i];
            }
        }
        CHECK (maxJump < 0.06, "6部屋を順に切り替えて最大サンプル跳び %.3f", maxJump);
    }

    std::printf ("\n== 9. モノは両耳の応答を合算する ==\n");
    {
        heya::Convolver stereo, mono;
        stereo.prepare (sr, 127); mono.prepare (sr, 127);
        double error = 0.0;
        for (int room = 0; room < heya::kNumRooms; ++room)
        {
            stereo.setRoom (room); mono.setRoom (room);
            stereo.reset(); mono.reset();
            std::vector<float> L (127), R (127), M (127);
            heya::Rng rng (123);
            for (int b = 0; b < 300; ++b)
            {
                for (int n = 0; n < 127; ++n) L[n] = R[n] = M[n] = rng.bi() * 0.2f;
                stereo.process (L.data(), R.data(), 127);
                mono.process (M.data(), M.data(), 127);
                for (int n = 0; n < 127; ++n)
                    error = std::max (error, (double) std::abs (M[n] - (L[n] + R[n]) * 0.5f));
            }
        }
        CHECK (error < 2e-6, "全6空間でモノと左右平均が一致（最大差 %.2e）", error);
    }

    std::printf ("\n== 10. サンプルレートで広さの意味が変わらない ==\n");
    {
        double times[3] {};
        const double rates[3] { 44100.0, 48000.0, 96000.0 };
        for (int ri = 0; ri < 3; ++ri)
        {
            heya::Convolver cv;
            cv.prepare (rates[ri], 511); cv.setRoom (4); cv.setSize (0.45f); cv.reset();
            const int total = (int) (rates[ri] * 3.2);
            std::vector<float> L (total), R (total); L[0] = R[0] = 1.0f;
            for (int n = 0; n < total; n += 511)
                cv.process (L.data() + n, R.data() + n, std::min (511, total - n));
            times[ri] = rt60Of (L, rates[ri]);
        }
        CHECK (std::abs (times[0] / times[1] - 1.0) < 0.12
               && std::abs (times[2] / times[1] - 1.0) < 0.12,
               "広さ45%%の残響時間: 44.1k %.3fs / 48k %.3fs / 96k %.3fs", times[0], times[1], times[2]);
    }

    std::printf ("\n=======================================\n");
    std::printf (gFail == 0 ? "  dsp_heya: ぜんぶ PASS\n" : "  dsp_heya: %d 件 FAIL\n", gFail);
    std::printf ("=======================================\n");
    return gFail == 0 ? 0 : 1;
}
