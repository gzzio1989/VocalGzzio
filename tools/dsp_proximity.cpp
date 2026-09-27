// dsp_proximity.cpp — v2.10.0「距離ならし」(近接効果の自動補正) の数値検証
//
//  何を確かめるか
//   1. 0% のとき、入力が **1サンプルも変わらない** こと（既存ユーザーの音を守る）
//   2. マイクに近づいた状況（低域が持ち上がった音）を入れると、低域が**減る**こと
//   3. 離れた状況（低域が痩せた音）を入れると、低域が**戻る**こと
//   4. 距離が一定なら、補正が 0 付近に収束すること（勝手に動かない）
//   5. 声の高さを変えても誤作動しないこと（音程ではなく距離に反応する）
//   6. 遅延が 0 サンプルであること
#include "../Source/Proximity.h"
#include <cstdio>
#include <vector>
#include <cmath>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static constexpr double SR = 48000.0;
static constexpr float PI = 3.14159265358979f;

// 倍音のある「声もどき」。lowTiltDb で低域だけを持ち上げ／下げできる＝距離の代わり。
struct Voice
{
    double ph[16] {};
    float next (double f0, float lowTiltDb)
    {
        double s = 0.0;
        for (int h = 0; h < 16; ++h)
        {
            const double fh = f0 * (h + 1);
            if (fh > SR * 0.45) break;
            // 低い倍音ほど lowTiltDb の影響を受ける（220Hz を境に効きが落ちる）
            const double w = 1.0 / (1.0 + std::pow (fh / 220.0, 2.0));
            const double g = std::pow (10.0, (lowTiltDb * w) / 20.0);
            s += g * std::sin (ph[h]) / (h + 1);
            ph[h] += 2.0 * PI * fh / SR;
            if (ph[h] > 2 * PI) ph[h] -= 2 * PI;
        }
        return (float) (0.25 * s / 1.8);
    }
};

// 低域と中域のエネルギー比を dB で測る（検証側の独立な物差し）。
// 低域は 60-200Hz。※一度 60-150Hz に狭めてみたが、男声(f0=196Hz)は
// そこに倍音を持たないので、フィルタの裾しか測れず差が縮んだ。基本波を
// 含む 60-200Hz が正しい物差し。
static float measureTiltDb (const std::vector<float>& x)
{
    auto band = [&] (float f0, float f1)
    {
        double acc = 0.0;
        const int N = (int) x.size();
        for (double f = f0; f <= f1; f *= 1.06)
        {
            double re = 0.0, im = 0.0;
            for (int n = 0; n < N; ++n)
            {
                const double w = 2.0 * PI * f * n / SR;
                re += x[(size_t) n] * std::cos (w);
                im += x[(size_t) n] * std::sin (w);
            }
            acc += (re * re + im * im) / (double) N / (double) N;
        }
        return acc;
    };
    const double lo = band (60.0, 200.0);
    const double md = band (500.0, 3000.0);
    return (float) (10.0 * std::log10 ((lo + 1e-18) / (md + 1e-18)));
}

// settle 秒だけ settleTilt で「いつもの距離」を覚えさせてから tiltDb へ動かし、
// 動かした 0.5 秒後からの 1 秒ぶんを返す。
//   ※基準は 40 秒かけて追従するので、動かしてから何十秒も経つと
//     「その距離がいつも」になって補正は 0 へ戻る。これは意図した挙動なので、
//     検証は「動かした直後」を見る。
static std::vector<float> run (float amount, float tiltDb, double f0,
                               double settleSec, float settleTilt,
                               float* corrOut = nullptr)
{
    gz::prox::Evener ev;
    ev.prepare (SR);
    ev.setAmount (amount);
    Voice v;
    const int block = 128;
    const int settleN = (int) (settleSec * SR);
    const int grabFrom = settleN + (int) (0.5 * SR);
    const int total    = grabFrom + (int) SR;
    std::vector<float> tail;
    tail.reserve ((size_t) SR + 256);

    std::vector<float> buf ((size_t) block);
    for (int done = 0; done < total; done += block)
    {
        const float t = (done < settleN) ? settleTilt : tiltDb;
        for (int n = 0; n < block; ++n) buf[(size_t) n] = v.next (f0, t);
        ev.process (buf.data(), nullptr, block, true);
        if (done >= grabFrom)
            for (int n = 0; n < block; ++n) tail.push_back (buf[(size_t) n]);
    }
    if (corrOut) *corrOut = ev.currentCorrectionDb();
    return tail;
}

int main()
{
    std::printf ("距離ならし（近接効果の自動補正）の検証 @ %.0f Hz\n\n", SR);

    // ---- 0. ローシェルフの利得そのものを確かめる ----
    // corrDb がそのまま音になるかは、この式が正しいかに全部かかっている。
    // 実装より先に、ここを独立に検証しておく。
    std::printf ("[0] ローシェルフ単体（肩 220Hz）\n");
    {
        auto gainAt = [] (float gainDb, float f)
        {
            gz::prox::LowShelf s; s.set (SR, 220.0f, gainDb);
            // 正弦波を通して振幅比を見る（過渡を捨てるため前半は読み捨て）
            double pk = 0.0;
            const int N = (int) (SR * 0.5);
            for (int n = 0; n < N; ++n)
            {
                const float y = s.tick ((float) std::sin (2.0 * PI * f * n / SR));
                if (n > N / 2) pk = std::max (pk, (double) std::abs (y));
            }
            return (float) (20.0 * std::log10 (pk + 1e-12));
        };
        const float dcCut  = gainAt (-6.0f, 20.0f);     // 直流側は指定どおり出るはず
        const float hfCut  = gainAt (-6.0f, 12000.0f);  // 高域は素通しのはず
        const float dcBst  = gainAt (+6.0f, 20.0f);
        std::printf ("  -6dB指定: 20Hz %.2f dB / 12kHz %.2f dB   +6dB指定: 20Hz %.2f dB\n",
                     dcCut, hfCut, dcBst);
        CHECK (std::abs (dcCut - (-6.0f)) < 0.3f, "下げ指定が低域で出る (%.2f dB)", dcCut);
        CHECK (std::abs (dcBst - (+6.0f)) < 0.3f, "上げ指定が低域で出る (%.2f dB)", dcBst);
        CHECK (std::abs (hfCut) < 0.3f, "高域は素通し (%.2f dB)", hfCut);
        std::printf ("\n");
    }

    // ---- 1. 0% なら 1 サンプルも変わらない ----
    {
        gz::prox::Evener ev; ev.prepare (SR); ev.setAmount (0.0f);
        Voice v; float worst = 0.0f;
        std::vector<float> buf (128);
        for (int b = 0; b < 400; ++b)
        {
            std::vector<float> ref (128);
            for (int n = 0; n < 128; ++n) { buf[(size_t) n] = ref[(size_t) n] = v.next (196.0, 6.0f); }
            ev.process (buf.data(), nullptr, 128, true);
            for (int n = 0; n < 128; ++n)
                worst = std::max (worst, std::abs (buf[(size_t) n] - ref[(size_t) n]));
        }
        std::printf ("[1] 設定 0%% のとき\n");
        CHECK (worst == 0.0f, "入力と出力が完全に同じ (最大差 %.3g)", (double) worst);
        std::printf ("\n");
    }

    // ---- 2. 近づいた（低域 +6dB）→ 低域が減る ----
    // 前半は基準(0dB)で「いつも」を覚えさせ、後半で近づける。
    std::printf ("[2] マイクに近づいたとき（低域 +6dB の入力）\n");
    {
        float corr = 0.0f;
        const auto off = run (0.0f, 6.0f, 196.0, 12.0, 0.0f);
        const auto on  = run (1.0f, 6.0f, 196.0, 12.0, 0.0f, &corr);
        const float tOff = measureTiltDb (off), tOn = measureTiltDb (on);
        std::printf ("  低域/中域の比: 補正なし %.2f dB → 補正あり %.2f dB (補正量 %.2f dB)\n",
                     tOff, tOn, corr);
        CHECK (tOn < tOff - 0.5f, "低域が減った (%.2f → %.2f dB)", tOff, tOn);
        CHECK (corr < -0.5f, "補正は下げ方向 (%.2f dB)", corr);
        std::printf ("\n");
    }

    // ---- 3. 離れた（低域 -6dB）→ 低域が戻る ----
    std::printf ("[3] マイクから離れたとき（低域 -6dB の入力）\n");
    {
        float corr = 0.0f;
        const auto off = run (0.0f, -6.0f, 196.0, 12.0, 0.0f);
        const auto on  = run (1.0f, -6.0f, 196.0, 12.0, 0.0f, &corr);
        const float tOff = measureTiltDb (off), tOn = measureTiltDb (on);
        std::printf ("  低域/中域の比: 補正なし %.2f dB → 補正あり %.2f dB (補正量 %.2f dB)\n",
                     tOff, tOn, corr);
        CHECK (tOn > tOff + 0.5f, "低域が戻った (%.2f → %.2f dB)", tOff, tOn);
        CHECK (corr > 0.5f, "補正は上げ方向 (%.2f dB)", corr);
        std::printf ("\n");
    }

    // ---- 4. 距離が一定なら補正は 0 に収束（勝手に動かない） ----
    std::printf ("[4] 距離が変わらないとき\n");
    {
        float corr = 0.0f;
        run (1.0f, 3.0f, 196.0, 40.0, 3.0f, &corr);   // ずっと同じ傾き
        std::printf ("  60秒後の補正量: %.3f dB\n", corr);
        CHECK (std::abs (corr) < 1.0f, "補正がほぼ 0 に収束 (%.3f dB)", corr);
        std::printf ("\n");
    }

    // ---- 5. 声の高さを変えても誤作動しない ----
    std::printf ("[5] 距離は同じで、声の高さだけ変えたとき\n");
    {
        float cLow = 0.0f, cHigh = 0.0f;
        run (1.0f, 0.0f, 130.8, 20.0, 0.0f, &cLow);   // C3
        run (1.0f, 0.0f, 392.0, 20.0, 0.0f, &cHigh);  // G4
        std::printf ("  低い声 %.3f dB / 高い声 %.3f dB\n", cLow, cHigh);
        CHECK (std::abs (cLow)  < 1.5f, "低い声でも補正はほぼ 0 (%.3f dB)", cLow);
        CHECK (std::abs (cHigh) < 1.5f, "高い声でも補正はほぼ 0 (%.3f dB)", cHigh);
        std::printf ("\n");
    }

    // ---- 6. 遅延ゼロ ----
    std::printf ("[6] 遅延\n");
    {
        gz::prox::Evener ev; ev.prepare (SR); ev.setAmount (1.0f);
        // 補正量を動かしてからインパルスを入れる（係数が動いていても遅延は増えない）
        Voice v; std::vector<float> warm (128);
        for (int b = 0; b < 200; ++b)
        {
            for (int n = 0; n < 128; ++n) warm[(size_t) n] = v.next (196.0, 6.0f);
            ev.process (warm.data(), nullptr, 128, true);
        }
        std::vector<float> imp (512, 0.0f);
        imp[5] = 1.0f;
        ev.process (imp.data(), nullptr, 512, true);
        int arg = 0; float best = 0.0f;
        for (int i = 0; i < 512; ++i)
            if (std::abs (imp[(size_t) i]) > best) { best = std::abs (imp[(size_t) i]); arg = i; }
        std::printf ("  インパルスのピーク位置: %d（入れた場所 5）\n", arg);
        CHECK (arg == 5, "遅延 0 サンプル (ピーク %d)", arg);
        std::printf ("\n");
    }

    std::printf (gFail ? "== %d 件 FAIL ==\n" : "== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
