// 「なめらか」動的レゾナンス抑制の検証。
// Source/Resonance.h は JUCE 非依存なので、本物をそのまま #include して試験する。
//
//   g++ -O2 -std=c++17 dsp_resonance.cpp -o dsp_resonance && ./dsp_resonance
//
// 合格条件(このファイルの最後で判定):
//   A) 刺さる帯域(+12dBのレゾナンス)を 4dB 以上削る
//   B) そのとき離れた基準帯域(1kHz)は 1dB 未満しか動かない
//   C) レゾナンスの無いきれいな声(ビブラート付き)はほぼ触らない(RMS変化 <0.7dB, 最大カット <3dB)
//   D) レゾナンスが動いたら追いかける(移動後も 3dB 以上削れる)
//   E) 実時間の 3% 未満で処理できる(ゼロ遅延・低負荷の裏付け)
#include "../Source/Resonance.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <chrono>
#include <algorithm>

static const double SR = 48000.0;

// ---- 計測用の道具 -----------------------------------------------------------
struct Biquad
{
    double b0=1,b1=0,b2=0,a1=0,a2=0, z1=0,z2=0;
    float process(float x){ double y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return (float)y; }
    void reset(){ z1=z2=0; }
    void setPeak(double f,double q,double gDb){
        double A=std::pow(10.0,gDb/40.0), w=2*M_PI*f/SR, al=std::sin(w)/(2*q), c=std::cos(w);
        double n=1+al/A;
        b0=(1+al*A)/n; b1=-2*c/n; b2=(1-al*A)/n; a1=-2*c/n; a2=(1-al/A)/n;
    }
    void setBP(double f,double q){
        double w=2*M_PI*f/SR, al=std::sin(w)/(2*q), c=std::cos(w), n=1+al;
        b0=al/n; b1=0; b2=-al/n; a1=-2*c/n; a2=(1-al)/n;
    }
};

// 帯域RMS(dB): fc を Q=10 のバンドパスで取り出して区間RMS
static double bandDb (const std::vector<float>& x, double fc, int from, int to)
{
    Biquad bp; bp.setBP (fc, 10.0);
    double acc = 0.0; int cnt = 0;
    for (int n = 0; n < (int) x.size() && n < to; ++n)
    {
        float v = bp.process (x[n]);
        if (n >= from) { acc += (double) v * v; ++cnt; }
    }
    return 10.0 * std::log10 (acc / std::max (1, cnt) + 1e-20);
}

static double rmsDb (const std::vector<float>& x, int from, int to)
{
    double acc = 0.0; int cnt = 0;
    for (int n = from; n < to && n < (int) x.size(); ++n) { acc += (double) x[n]*x[n]; ++cnt; }
    return 10.0 * std::log10 (acc / std::max (1, cnt) + 1e-20);
}

// ---- 声のシミュレータ: 倍音列 + ビブラート + 息成分 --------------------------
static std::vector<float> makeVoice (double f0, double seconds, double vibHz, double vibCents)
{
    const int N = (int) (seconds * SR);
    std::vector<float> v (N, 0.0f);
    double phase[40] = {};
    unsigned rng = 22222;
    for (int n = 0; n < N; ++n)
    {
        const double t   = n / SR;
        const double vib = std::pow (2.0, vibCents * std::sin (2*M_PI*vibHz*t) / 1200.0);
        const double f   = f0 * vib;
        double s = 0.0;
        for (int h = 1; h <= 36; ++h)
        {
            const double fh = f * h;
            if (fh > 11000.0) break;
            phase[h] += 2*M_PI*fh / SR;
            if (phase[h] > 2*M_PI) phase[h] -= 2*M_PI;
            s += std::sin (phase[h]) / std::pow ((double) h, 1.15);   // ゆるい -7dB/oct
        }
        rng = rng*1664525u + 1013904223u;
        const double noise = ((int)(rng>>9) / 4194304.0 - 1.0) * 0.004;  // 息
        v[n] = (float) ((s * 0.16 + noise) * (0.75 + 0.25*std::sin(2*M_PI*0.7*t)));
    }
    return v;
}

int main()
{
    using clock = std::chrono::steady_clock;
    printf ("=== なめらか(動的レゾナンス抑制) 検証 ===\n");
    bool ok = true;

    // ---------- A/B: 刺さるレゾナンスを削る / 離れた帯域は触らない ----------
    {
        auto dry = makeVoice (130.0, 4.0, 5.0, 20.0);          // 低めの男声
        Biquad res; res.setPeak (3100.0, 8.0, 12.0);           // 3.1kHz +12dB の刺さり
        std::vector<float> in (dry.size());
        for (size_t n = 0; n < dry.size(); ++n) in[n] = res.process (dry[n]);

        std::vector<float> out = in;
        gz::res::Tamer t; t.prepare (SR); t.setAmount (1.0f);
        t.process (out.data(), nullptr, (int) out.size());

        const int a = (int)(1.5*SR), b = (int)(4.0*SR);        // 立ち上がり後を測る
        const double cut3k  = bandDb (in, 3100.0, a, b) - bandDb (out, 3100.0, a, b);
        const double mov1k  = bandDb (in, 1000.0, a, b) - bandDb (out, 1000.0, a, b);
        printf ("[A] 3.1kHz(+12dBの刺さり) のカット: %.1f dB  (>=4 で合格)\n", cut3k);
        printf ("[B] 1kHz(基準) の変化:            %.2f dB (<1 で合格)\n", std::fabs (mov1k));
        ok &= (cut3k >= 4.0);  ok &= (std::fabs (mov1k) < 1.0);
    }

    // ---------- C: きれいな声はほぼ触らない(女声・ビブラート付き) ----------
    {
        auto in = makeVoice (220.0, 4.0, 5.5, 35.0);
        std::vector<float> out = in;
        gz::res::Tamer t; t.prepare (SR); t.setAmount (1.0f);
        t.process (out.data(), nullptr, (int) out.size());
        const int a = (int)(1.0*SR), b = (int)(4.0*SR);
        const double dRms = std::fabs (rmsDb (in, a, b) - rmsDb (out, a, b));
        printf ("[C] きれいな女声: RMS変化 %.2f dB (<0.7), 最大カット %.1f dB (<3)\n",
                dRms, t.lastMaxCutDb());
        ok &= (dRms < 0.7);  ok &= (t.lastMaxCutDb() < 3.0);
    }

    // ---------- D: レゾナンスが動いたら追いかける ----------
    {
        auto dry = makeVoice (150.0, 6.0, 5.0, 20.0);
        std::vector<float> in (dry.size());
        {
            Biquad r1; r1.setPeak (2600.0, 8.0, 12.0);
            Biquad r2; r2.setPeak (4300.0, 8.0, 12.0);
            const int half = (int) dry.size() / 2;
            for (int n = 0; n < (int) dry.size(); ++n)
                in[n] = (n < half) ? r1.process (dry[n]) : r2.process (dry[n]);
        }
        std::vector<float> out = in;
        gz::res::Tamer t; t.prepare (SR); t.setAmount (1.0f);
        t.process (out.data(), nullptr, (int) out.size());

        const int h = (int) dry.size() / 2;
        const double cut1 = bandDb (in, 2600.0, (int)(1.5*SR), h)
                          - bandDb (out, 2600.0, (int)(1.5*SR), h);
        const double cut2 = bandDb (in, 4300.0, h + (int)(1.0*SR), (int) dry.size())
                          - bandDb (out, 4300.0, h + (int)(1.0*SR), (int) dry.size());
        printf ("[D] 前半2.6kHzのカット %.1f dB / 移動後4.3kHzのカット %.1f dB (両方>=3)\n", cut1, cut2);
        ok &= (cut1 >= 3.0 && cut2 >= 3.0);
    }

    // ---------- E: 負荷(実時間比) ----------
    {
        auto in = makeVoice (180.0, 10.0, 5.0, 20.0);
        std::vector<float> L = in, R = in;
        gz::res::Tamer t; t.prepare (SR); t.setAmount (1.0f);
        auto t0 = clock::now();
        t.process (L.data(), R.data(), (int) L.size());
        const double sec = std::chrono::duration<double> (clock::now() - t0).count();
        const double rtf = sec / 10.0 * 100.0;
        printf ("[E] ステレオ10秒の処理 %.0f ms = 実時間の %.1f %% (<3%%)\n", sec*1000.0, rtf);
        ok &= (rtf < 3.0);
    }

    printf ("=== %s ===\n", ok ? "ぜんぶ合格" : "!! 不合格あり !!");
    return ok ? 0 : 1;
}
