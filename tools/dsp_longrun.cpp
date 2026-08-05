// dsp_longrun.cpp — 「長い時間使うとジーが走る」報告を受けての長時間耐性検証 (v2.8.0)
//
//  実時間で何時間も回すわけにはいかないので、疑わしい部品だけを取り出して
//  「圧縮した長時間」を流し、時間とともに悪くなる量が無いかを数値で見る。
//
//   1: ジー音(HumKiller) 20分 … 歌＋本物のハムを20分流し、序盤と終盤の
//      「歌っていない瞬間の残りカス」を比べる。時間とともに増えたら、
//      声を掴んで溜め込んでいる(=ジーを注入している)ことになる。
//   2: ジー音の重み劣化 … 20分間の倍音重みの「本来のハムからのズレ」最大値。
//   3: ボイス変換(VoiceShifter) 8分 … 一定の声を流し続け、序盤と終盤の
//      出力の濁り(目的音以外のエネルギー)を比べる。位相の積算が壊れて
//      いれば終盤ほどザラつく。
//   4: 発振器の振幅ドリフト 6時間ぶん … 回転式発振器を6時間ぶん回して
//      半径が1のままかを見る。
//
// ビルド: g++ -O2 -std=c++17 -I../Source dsp_longrun.cpp -o dsp_longrun
#include "../Source/HumKiller.h"
#include "../Source/VoiceShifter.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static constexpr double kSR = 44100.0;   // 報告者の環境に合わせる

// 歌声もどき: 倍音16本＋ビブラート。音程はフレーズごとに変える。
struct Voice
{
    double ph[16] {}, vp = 0.0;
    float sample (double f0, double amp)
    {
        const double vib = 1.0 + 0.006 * std::sin (vp);   // ±10セント(控えめな歌い手)
        vp += 2.0 * M_PI * 5.3 / kSR;
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

// 本物のハム(50Hz系列・位相固定)。true の重み(a,b)も同じ式から作れる。
static double humAt (double t, int h)                     // h=0..3 の振幅
{
    static const double hAmp[4] = { 0.0030, 0.0016, 0.0010, 0.0005 };  // -50dB台
    return hAmp[h] * std::sin (2.0 * M_PI * 50.0 * (h + 1) * t + 0.7 * h);
}
static float humSample (double t)
{
    double s = 0.0; for (int h = 0; h < 4; ++h) s += humAt (t, h);
    return (float) s;
}

int main()
{
    std::printf ("長時間耐性 @ %.0fHz (時間圧縮シミュレーション)\n", kSR);

    // ---------- 1+2: ジー音 20分 ----------
    {
        gz::hum::Killer k; k.prepare (kSR); k.setAmount (1.0f);
        // フレーズ 3.2秒歌う + 0.9秒休む。音程は「わずかに外した」現実的な並び
        // (ハムの倍音 100/150/200Hz の近くを通る音を意図的に多くした・意地悪設定)
        static const double notes[6] = { 99.0, 148.5, 199.0, 123.0, 151.5, 196.0 };
        Voice v;
        const long total = (long) (kSR * 60.0 * 20.0);        // 20分
        double gapRms[2] = { 0.0, 0.0 };  long gapCnt[2] = { 0, 0 };
        double devMax = 0.0;              // 倍音重みの「本来のハム」からのズレ最大
        long   devCtl = 0;

        for (long n = 0; n < total; ++n)
        {
            const double t = n / kSR;
            const double phrase = std::fmod (t, 4.1);
            const bool  sing   = phrase < 3.2;
            const int   noteIx = (int) std::fmod (t / 4.1, 6.0);
            float x = humSample (t) + (sing ? v.sample (notes[noteIx], 0.15) : 0.0f);
            k.process (&x, nullptr, 1);

            // 休符(息継ぎ)のまん中 0.5秒だけ残りカスを測る。
            // 入力はハムだけなので、完璧なら出力はほぼ無音のはず。
            const bool inGap = (phrase > 3.4 && phrase < 3.9);
            if (inGap)
            {
                if (t < 120.0)                 { gapRms[0] += (double) x * x; ++gapCnt[0]; }   // 序盤2分
                else if (t > 60.0 * 18.0)      { gapRms[1] += (double) x * x; ++gapCnt[1]; }   // 終盤2分
            }
            // 0.1秒ごとに、採用系列の倍音重みが「本来のハムの振幅」から
            // どれだけズレたかを測る。歌の最中は隣の音を一時的に追いかけるのが
            // 正常動作(すぐ戻る)なので、**息継ぎに入って0.2秒後**だけを見る。
            // ここでズレが残っていたら「声を掴んで溜め込んだ」ことになる。
            if (++devCtl >= (long) (kSR * 0.1))
            {
                devCtl = 0;
                static const double trueAmp[4] = { 0.0030, 0.0016, 0.0010, 0.0005 };
                if (t > 30.0 && k.detectedHz() == 50 && phrase > 3.6 && phrase < 3.9)
                    for (int h = 1; h < 4; ++h)
                        devMax = std::max (devMax,
                                     std::fabs ((double) k.dbgHarmMag (h) - trueAmp[h]));
            }
        }
        const double early = std::sqrt (gapRms[0] / std::max (1L, gapCnt[0]));
        const double late  = std::sqrt (gapRms[1] / std::max (1L, gapCnt[1]));
        const double eDb = 20.0 * std::log10 (early + 1e-12);
        const double lDb = 20.0 * std::log10 (late  + 1e-12);
        CHECK (k.detectedHz() == 50, "20分後もハム検出は 50Hz のまま (%d)", k.detectedHz());
        CHECK (lDb < eDb + 3.0,
               "息継ぎの残りカス: 序盤 %.1f dB / 終盤 %.1f dB (悪化 <3dB = 溜め込みなし)", eDb, lDb);
        CHECK (lDb < -55.0, "終盤の残りカスの絶対値 %.1f dB (<-55dB = 聞こえない)", lDb);
        CHECK (devMax < 0.0012,
               "息継ぎ中の倍音重みの誤差 最大 %.5f (<0.0012 = 声を掴んで溜めていない)", devMax);
    }

    // ---------- 3: ボイス変換 8分 ----------
    {
        gz::VoiceShifter sh; sh.prepare (kSR); sh.setParams (2.0f, 0.0f, 1.0f);   // +2半音
        Voice v;
        const long total = (long) (kSR * 60.0 * 8.0);
        // 目的音(220Hz→+2半音=246.9Hz)の純度: 「目的の倍音列以外」のエネルギー比。
        // ビブラートで線が±1%揺れるので、0.25秒窓(帯域4Hz)×8枚で測って平均する。
        auto purity = [&] (const std::vector<float>& seg)
        {
            const int W = (int) (kSR * 0.25);
            double ratioSum = 0.0; int nWin = 0;
            for (size_t base = 0; base + (size_t) W <= seg.size(); base += (size_t) W)
            {
                double tot = 0.0;
                for (int i = 0; i < W; ++i) tot += (double) seg[base + (size_t) i] * seg[base + (size_t) i];
                double want = 0.0;
                for (int h = 1; h <= 10; ++h)
                {
                    const double f = 220.0 * std::pow (2.0, 2.0 / 12.0) * h;
                    if (f > kSR * 0.45) break;
                    const double w = 2.0 * M_PI * f / kSR;
                    double s1 = 0, s2 = 0;
                    for (int i = 0; i < W; ++i)
                    { const double s0 = seg[base + (size_t) i] + 2.0 * std::cos (w) * s1 - s2; s2 = s1; s1 = s0; }
                    const double p2 = s1 * s1 + s2 * s2 - 2.0 * std::cos (w) * s1 * s2;  // |X|^2
                    want += 2.0 * p2 / (double) W;    // 正弦1本のΣx^2への寄与 = 2|X|^2/N
                }
                ratioSum += (tot - std::min (want, tot)) / (tot + 1e-30);
                ++nWin;
            }
            return 10.0 * std::log10 (ratioSum / std::max (1, nWin) + 1e-12);
        };
        std::vector<float> early, late;
        for (long n = 0; n < total; ++n)
        {
            const double t = n / kSR;
            float x = v.sample (220.0, 0.3);
            sh.processBlock (&x, 1);
            if (t > 20.0 && t < 22.0)                       early.push_back (x);
            if (t > 60.0 * 8.0 - 22.0 && t < 60.0 * 8.0 - 20.0) late.push_back (x);
        }
        const double pe = purity (early), pl = purity (late);
        CHECK (pl < pe + 2.0, "+2半音の濁り: 序盤 %.1f dB → 8分後 %.1f dB (悪化<2dB = 位相は壊れない)", pe, pl);
    }

    // ---------- 4: 発振器の振幅ドリフト 6時間 ----------
    {
        for (double f : { 50.0, 60.0, 400.0 })
        {
            gz::hum::Osc o; o.set (f, kSR);
            const long total = (long) (kSR * 3600.0 * 6.0);   // 6時間
            float worst = 0.0f;
            for (long n = 0; n < total; ++n)
            {
                o.tick();
                if ((n & 0xFFFFF) == 0)      // ときどき半径を見る
                    worst = std::max (worst, std::fabs (o.c * o.c + o.s * o.s - 1.0f));
            }
            const float r2 = o.c * o.c + o.s * o.s;
            CHECK (std::fabs (r2 - 1.0f) < 1.0e-3f && worst < 1.0e-3f,
                   "%3.0fHz 発振器 6時間: 半径^2 = %.6f (ずれ最大 %.2g)", f, r2, (double) worst);
        }
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
