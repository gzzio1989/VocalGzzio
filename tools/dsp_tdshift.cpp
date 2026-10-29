// dsp_tdshift.cpp — v5.1.2 時間領域ピッチ同期シフタ(TdPitchShifter)の回帰検査。JUCE不要。
//  [1] 補正ゼロ(比率1)・Mix 0/1 で原音と完全一致し、申告遅延どおりにインパルスが出る
//  [2] ±1〜±7半音で狙った周波数へ移る(誤差・純音比)
//  [3] 音量が極端に変わらない
//  [4] つなぎ目でプチッと言わない(隣接サンプル差が入力の最大差の1.6倍以内)
//  [5] ブロック分割に左右されない
#include "../Source/TdPitchShifter.h"
#include <cstdio>
#include <complex>
#include <limits>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;
int failures = 0;
void check (bool ok, const char* name)
{
    std::printf ("%s: %s\n", ok ? "合格" : "不合格", name);
    if (! ok) ++failures;
}
std::vector<float> voice (double sr, double hz, double sec)
{
    std::vector<float> v ((size_t) std::lround (sr * sec));
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double p = 2.0 * pi * hz * (double) i / sr;
        v[i] = (float) (0.25 * std::sin (p) + 0.12 * std::sin (2 * p) + 0.07 * std::sin (3 * p) + 0.04 * std::sin (4 * p));
    }
    return v;
}
struct M { double cents, coherence, gain; };
M measure (const std::vector<float>& out, const std::vector<float>& in, double sr, double wanted)
{
    const int first = (int) (0.20 * sr), half = (int) (0.20 * sr);
    std::complex<double> s[2] {};
    double e = 0.0, ein = 0.0;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < half; ++i)
        {
            const int n = first + j * half + i;
            const double p = -2.0 * pi * wanted * n / sr;
            s[j] += (double) out[(size_t) n] * std::complex<double> (std::cos (p), std::sin (p));
            e += (double) out[(size_t) n] * out[(size_t) n];
            ein += (double) in[(size_t) n] * in[(size_t) n];
        }
    const double dph = std::arg (s[1] * std::conj (s[0]));
    const double hz = wanted + dph * sr / (2.0 * pi * half);
    // 基音成分(0.25振幅)のエネルギー比で純度を見る
    const double fundE = (std::norm (s[0]) + std::norm (s[1])) * 2.0 / half;
    const double expectFund = 0.25 * 0.25 / 2.0 * 2.0 * half;
    return { 1200.0 * std::log2 (hz / wanted), fundE / std::max (expectFund, 1e-12), std::sqrt (e / std::max (ein, 1e-12)) };
}
}

int main()
{
    std::printf ("時間領域ピッチ同期シフタ(TdPitchShifter)の検証\n\n");

    // [1] 恒等性と遅延
    {
        bool ok = true; double worst = 0.0;
        for (double sr : { 44100.0, 48000.0, 96000.0 })
            for (float mix : { 0.0f, 1.0f })
            {
                gz::TdPitchShifter sh; sh.prepare (sr); sh.setPitchHz (220.0f); sh.setParams (0.0f, mix);
                auto in = voice (sr, 220.0, 0.2); auto out = in;
                sh.processBlock (out.data(), (int) out.size());
                const int L = sh.latencySamples();
                for (size_t i = (size_t) L + 10; i < out.size(); ++i)
                    worst = std::max (worst, std::abs ((double) out[i] - in[i - (size_t) L]));
                sh.reset();
                std::vector<float> imp (4096, 0.0f); imp[1000] = 0.5f;
                sh.processBlock (imp.data(), (int) imp.size());
                const auto mx = std::max_element (imp.begin(), imp.end());
                ok = ok && (mx - imp.begin() == 1000 + L) && std::abs (*mx - 0.5f) < 1e-5f;
            }
        gz::TdPitchShifter t; t.prepare (48000.0);
        std::printf ("  申告遅延 %d サンプル (%.2f ms @48kHz)、恒等時の最大差 %.2e\n",
                     t.latencySamples(), t.latencySamples() * 1000.0 / 48000.0, worst);
        check (worst < 1e-5, "補正ゼロで原音と一致");
        check (ok, "インパルスが申告遅延どおりに出る");
        check (t.latencySamples() * 1000.0 / 48000.0 < 3.5, "申告遅延が 3.5ms 未満");
    }

    // [2][3][4] 移調
    {
        double worstC = 0.0, minCoh = 9.0, minG = 9.0, maxG = 0.0, worstJump = 0.0;
        for (double sr : { 44100.0, 48000.0 })
            for (double hz : { 110.0, 220.0, 440.0 })
                for (float st : { -7.0f, -3.0f, -1.0f, -0.3f, 0.3f, 1.0f, 3.0f, 7.0f })
                {
                    gz::TdPitchShifter sh; sh.prepare (sr);
                    sh.setPitchHz ((float) hz); sh.setParams (st, 1.0f);
                    auto in = voice (sr, hz, 0.7); auto out = in;
                    for (size_t i = 0; i < out.size(); i += 128)
                        sh.processBlock (out.data() + i, (int) std::min ((size_t) 128, out.size() - i));
                    const double want = hz * std::pow (2.0, st / 12.0);
                    const auto m = measure (out, in, sr, want);
                    worstC = std::max (worstC, std::abs (m.cents));
                    minCoh = std::min (minCoh, m.coherence);
                    minG = std::min (minG, m.gain); maxG = std::max (maxG, m.gain);
                    double inJ = 0.0, outJ = 0.0;
                    for (size_t i = (size_t) (0.05 * sr); i < out.size(); ++i)
                    {
                        inJ  = std::max (inJ,  (double) std::abs (in[i]  - in[i - 1]));
                        outJ = std::max (outJ, (double) std::abs (out[i] - out[i - 1]));
                    }
                    worstJump = std::max (worstJump, outJ / std::max (inJ * std::pow (2.0, std::max (0.0f, st) / 12.0), 1e-9));
                    if (std::abs (m.cents) > 6.0 || m.coherence < 0.80)
                        std::printf ("  条件 %.0fHz / %.0fHz / %+.1f半音: 誤差 %.2f セント、純度 %.3f、音量 %.3f\n",
                                     sr, hz, st, m.cents, m.coherence, m.gain);
                }
        std::printf ("  移調誤差最大 %.2f セント、基音純度最小 %.3f、音量比 %.3f〜%.3f、つなぎ目跳躍比 %.2f\n",
                     worstC, minCoh, minG, maxG, worstJump);
        check (worstC < 6.0 && minCoh > 0.80, "±0.3〜±7半音で狙った音程へ移る");
        check (minG > 0.80 && maxG < 1.20, "移調で音量が極端に変わらない");
        check (worstJump < 1.6, "つなぎ目でプチッと言わない");
    }

    // [5] ブロック分割
    {
        gz::TdPitchShifter a, b; a.prepare (48000); b.prepare (48000);
        a.setPitchHz (196.0f); b.setPitchHz (196.0f);
        a.setParams (2.0f, 0.8f); b.setParams (2.0f, 0.8f);
        auto whole = voice (48000, 196.0, 0.3), split = whole;
        a.processBlock (whole.data(), (int) whole.size());
        for (size_t i = 0; i < split.size(); i += 37)
            b.processBlock (split.data() + i, (int) std::min ((size_t) 37, split.size() - i));
        check (whole == split, "ブロック分割に左右されない");
    }

    // [6] 周期の推定が外れていても(WSOLAの相関探索で)崩れない・雑音や非有限値で壊れない
    {
        double minCoh = 9.0;
        for (float hint : { 205.0f, 236.0f, 0.0f })
        {
            gz::TdPitchShifter sh; sh.prepare (48000);
            sh.setPitchHz (hint); sh.setParams (1.0f, 1.0f);
            auto in = voice (48000, 220.0, 0.7); auto out = in;
            sh.processBlock (out.data(), (int) out.size());
            minCoh = std::min (minCoh, measure (out, in, 48000, 220.0 * std::pow (2.0, 1.0 / 12.0)).coherence);
        }
        std::printf ("  周期推定ずれ(±7%%・未検出)時の基音純度最小 %.3f\n", minCoh);
        check (minCoh > 0.80, "周期推定がずれても音程と純度を保つ");

        gz::TdPitchShifter sh; sh.prepare (48000); sh.setPitchHz (180.0f); sh.setParams (-4.0f, 1.0f);
        std::vector<float> n (48000);
        unsigned s = 9u;
        for (auto& v : n) { s = s * 1664525u + 1013904223u; v = (float) ((int) (s >> 9) - 4194304) / 4194304.0f * 0.3f; }
        n[1000] = std::numeric_limits<float>::quiet_NaN(); n[2000] = std::numeric_limits<float>::infinity();
        sh.processBlock (n.data(), (int) n.size());
        bool finite = true; float peak = 0.0f;
        for (float v : n) { finite = finite && std::isfinite (v); peak = std::max (peak, std::abs (v)); }
        check (finite && peak < 0.6f, "雑音・非有限入力でも有限で暴れない");
    }

    // [7] v5.1.3 長時間: 補正量が揺れ続けても読み出し遅延が申告値から離れていかない
    //     (旧版は D がランダムウォークで離れ、補正の入り切りで櫛形フィルタ＝時間とともに劣化)
    {
        gz::TdPitchShifter sh; sh.prepare (48000);
        const int L = sh.latencySamples();
        const double sr = 48000.0;
        const long total = (long) (sr * 600.0);     // 10分
        double ph = 0.0; float worstEarly = 0.0f, worstLate = 0.0f;
        std::vector<float> blk (256);
        for (long n = 0; n < total; n += 256)
        {
            const double t = n / sr;
            const double phrase = std::fmod (t, 4.0);
            const bool sing = phrase < 3.3;
            const double f0 = 196.0 * std::pow (2.0, std::floor (std::fmod (t / 4.0, 5.0)) / 12.0);
            // 補正量: ビブラートに追従して ±0.4 半音でゆらぎ、ときどき 0 へ戻る
            const float corr = (float) (0.4 * std::sin (2.0 * pi * 5.5 * t) + 0.15 * std::sin (2.0 * pi * 0.37 * t));
            const float amt  = (std::fmod (t, 1.7) < 1.2) ? 1.0f : 0.0f;
            sh.setPitchHz (sing ? (float) f0 : 0.0f);
            sh.setParams (corr, amt);
            for (auto& v : blk) { v = sing ? (float) (0.2 * std::sin (ph)) : 0.0f; ph += 2.0 * pi * f0 / sr; }
            sh.processBlock (blk.data(), (int) blk.size());
            const float dev = std::abs (sh.currentDelay() - (float) L);
            if (t < 30.0) worstEarly = std::max (worstEarly, dev);
            else if (t > 540.0) worstLate = std::max (worstLate, dev);
        }
        const float bound = (float) (48000.0 * 0.0035 / 2.0 + 24.0 + 48000.0 / 196.0);
        std::printf ("  読み出し遅延の申告値からのずれ: 序盤 %.1f / 終盤 %.1f サンプル (上限 %.0f)\n",
                     worstEarly, worstLate, bound);
        check (worstLate <= bound && worstLate <= worstEarly + 48.0f,
               "10分たっても読み出し遅延が申告値の近くに留まる");
    }

    // [8] v5.3.0 ささやき・語尾(-54dBFS付近)での250Hz原点復帰ループ(チリチリ音)防止検査
    {
        const double sr = 48000.0, hz = 220.0;
        const float st = 1.5f;
        gz::TdPitchShifter sh; sh.prepare (sr);
        sh.setPitchHz ((float) hz); sh.setParams (st, 1.0f);
        auto in = voice (sr, hz, 0.8);
        for (auto& v : in) v *= 0.008f; // 約-54dBFS(ささやき・語尾領域: 旧kQuiet=0.0025f未満)
        auto out = in;
        sh.processBlock (out.data(), (int) out.size());
        double inJ = 0.0, outJ = 0.0;
        for (size_t i = (size_t) (0.05 * sr); i < out.size(); ++i)
        {
            inJ  = std::max (inJ,  (double) std::abs (in[i]  - in[i - 1]));
            outJ = std::max (outJ, (double) std::abs (out[i] - out[i - 1]));
        }
        const double jumpRatio = outJ / std::max (inJ * std::pow (2.0, st / 12.0), 1e-9);
        std::printf ("  ささやき・語尾(-54dBFS)シフト時の隣接サンプル跳躍比 %.2f\n", jumpRatio);
        check (jumpRatio < 1.6, "ささやき・語尾(-54dBFS)でも250Hzチリチリ発振が起きない");
    }

    std::printf ("\n不合格: %d 件\n", failures);
    return failures == 0 ? 0 : 1;
}
