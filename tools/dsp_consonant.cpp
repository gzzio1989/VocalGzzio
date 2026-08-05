// dsp_consonant.cpp — 子音エンハンサー(Consonant.h)の数値検証 (v2.6.0)
//
//   1: 母音を伸ばしている間 + 子音バースト → 子音だけ持ち上がる
//   2: 母音だけ(ずっと同じ大きさ)         → まったく持ち上げない
//   3: サ行(6〜10kHzのノイズ)             → 持ち上げない(ディエッサーと喧嘩しない)
//   4: 効果0%                              → 完全に素通し(ビット一致)
//   5: 無音・フルスケール                  → 有限で暴れない
//   6: 計算負荷
//
// ビルド: g++ -O2 -std=c++17 -I../Source dsp_consonant.cpp -o dsp_consonant
#include "../Source/Consonant.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <cstring>
#include <chrono>
#include <algorithm>

static constexpr double kSR = 48000.0;
static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static unsigned rngState = 12345u;
static float noise() { rngState = rngState * 1664525u + 1013904223u;
                       return (float) ((int) (rngState >> 8) % 20001 - 10000) / 10000.0f; }

// 帯域制限ノイズ(2次バンドパスを2段)
struct BP
{
    double b0=0,b1=0,b2=0,a1=0,a2=0, z1=0,z2=0;
    void set (double f0, double q)
    {
        const double w = 2*M_PI*f0/kSR, al = std::sin(w)/(2*q), c = std::cos(w), a0 = 1+al;
        b0 = al/a0; b1 = 0; b2 = -al/a0; a1 = -2*c/a0; a2 = (1-al)/a0;
    }
    float p (float x) { const double y = b0*x + z1; z1 = b1*x - a1*y + z2; z2 = b2*x - a2*y; return (float) y; }
};

static double rmsDb (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i]*x[i];
    return 10.0*std::log10 (s/(double)(b-a) + 1e-30);
}

// 母音(基音+倍音)
struct Vowel { double ph[24]{};
    float s (double f0, double amp) { double v=0;
        for (int h=0;h<24;++h){ double f=f0*(h+1); if (f>kSR*0.45) break;
            // 実際の母音に近いフォルマント配置(F1 700Hz / F2 1200Hz / F3 2600Hz)。
            // F3 は F1 よりずっと弱い。ここを強く作ると母音が子音に見えてしまう。
            const double w = 1.00/(1.0+std::pow((f- 700.0)/500.0,2.0))
                           + 0.50/(1.0+std::pow((f-1200.0)/600.0,2.0))
                           + 0.18/(1.0+std::pow((f-2600.0)/900.0,2.0));
            v += w*std::sin(ph[h]); ph[h]+=2*M_PI*f/kSR; if(ph[h]>2*M_PI) ph[h]-=2*M_PI; }
        return (float)(amp*v*0.5); } };

static float gMaxBoostDb = 0.0f;      // 直近の run() で観測した最大ブースト

static void run (gz::cons::Enhancer& e, std::vector<float>& in, std::vector<float>& out)
{
    out = in; gMaxBoostDb = 0.0f;
    for (size_t i = 0; i < out.size(); i += 128)
    {
        const int n = (int) std::min ((size_t)128, out.size()-i);
        e.process (out.data()+i, nullptr, n);
        gMaxBoostDb = std::max (gMaxBoostDb, e.lastBoostDb());
    }
}

int main()
{
    std::printf ("Consonant enhancer @ %.0f Hz\n", kSR);
    const size_t N = (size_t)(kSR * 6.0);

    // ---------- 1: 「か・た・ぱ」＋母音 の音節をならべる ----------
    // 実際のことばは、子音(45ms)→母音(400ms)→すきま(150ms) の繰り返し。
    // 子音は母音の上に重なるのではなく、母音が鳴る**前**に出る。
    {
        gz::cons::Enhancer e; e.prepare (kSR); e.setAmount (1.0f);
        Vowel vw; BP bp; bp.set (2800.0, 1.2);
        std::vector<float> in (N), out;
        std::vector<std::pair<size_t,size_t>> cons, hold;
        const double period = 0.6;                       // 1音節 600ms
        for (double t = 0.6; t < 5.4; t += period)
        {
            const size_t c0 = (size_t)(t*kSR), c1 = c0 + (size_t)(0.045*kSR);
            cons.push_back ({ c0, c1 });
            // 伸ばしている最中(母音の 150ms〜380ms)を「変えてほしくない区間」とする
            hold.push_back ({ c1 + (size_t)(0.150*kSR), c1 + (size_t)(0.380*kSR) });
        }
        for (size_t i = 0; i < N; ++i)
        {
            float x = 0.0f;
            for (size_t s = 0; s < cons.size(); ++s)
            {
                if (i >= cons[s].first && i < cons[s].second) x += 0.6f * bp.p (noise());
                const size_t v0 = cons[s].second, v1 = v0 + (size_t)(0.400*kSR);
                if (i >= v0 && i < v1)
                {
                    const double k = (double)(i - v0) / (0.400*kSR);
                    const double env = std::min (1.0, k / 0.05) * std::min (1.0, (1.0 - k) / 0.15);
                    x += (float)(env) * vw.s (220.0, 0.5);
                }
            }
            in[i] = x;
        }
        run (e, in, out);

        double sumUp = 0.0; int cnt = 0;
        for (auto& br : cons)
        { sumUp += rmsDb (out, br.first, br.second) - rmsDb (in, br.first, br.second); ++cnt; }
        const double up = sumUp / cnt;
        CHECK (up > 2.0, "子音(か・た・ぱ): 平均 +%.2f dB 持ち上がった (>2dB)", up);

        double sumSt = 0.0; int c2 = 0;
        for (auto& br : hold)
        { if (br.second < N) { sumSt += rmsDb (out, br.first, br.second) - rmsDb (in, br.first, br.second); ++c2; } }
        const double st = sumSt / c2;
        CHECK (std::fabs (st) < 0.7, "伸ばした母音: %.2f dB (|変化| < 0.7dB)", st);
        CHECK (up - st > 2.0, "子音と母音の差 %.2f dB (>2dB = 子音だけ前に出ている)", up - st);
        CHECK (gMaxBoostDb > 3.0f && gMaxBoostDb <= 6.05f,
               "最大ブースト %.2f dB (3〜6dB。上限+6dBを超えない)", gMaxBoostDb);
    }

    // ---------- 2: 母音だけ ----------
    {
        gz::cons::Enhancer e; e.prepare (kSR); e.setAmount (1.0f);
        Vowel vw; std::vector<float> in (N), out;
        for (size_t i = 0; i < N; ++i) in[i] = vw.s (220.0, 0.5);
        run (e, in, out);
        const double d = rmsDb (out, N/2, N) - rmsDb (in, N/2, N);
        CHECK (std::fabs (d) < 0.3, "母音のみ: %.2f dB (|変化| < 0.3dB)", d);
    }

    // ---------- 3: サ行(歯擦音)は持ち上げない ----------
    {
        gz::cons::Enhancer e; e.prepare (kSR); e.setAmount (1.0f);
        Vowel vw; BP hi; hi.set (8000.0, 0.9);
        std::vector<float> in (N), out;
        std::vector<std::pair<size_t,size_t>> bursts;
        for (double t = 1.0; t < 5.5; t += 1.0)
        { const size_t a=(size_t)(t*kSR), b=a+(size_t)(0.09*kSR); bursts.push_back({a,b}); }
        for (size_t i = 0; i < N; ++i)
        {
            float x = vw.s (220.0, 0.5);
            for (auto& br : bursts) if (i >= br.first && i < br.second) x += 0.5f * hi.p (noise());
            in[i] = x;
        }
        run (e, in, out);
        double sum = 0.0; int cnt = 0;
        for (auto& br : bursts)
        { sum += rmsDb (out, br.first, br.second) - rmsDb (in, br.first, br.second); ++cnt; }
        const double up = sum / cnt;
        CHECK (up < 1.0, "サ行バースト: %.2f dB (<1dB = ほぼ持ち上げない)", up);
    }

    // ---------- 4: 効果0% は完全素通し ----------
    {
        gz::cons::Enhancer e; e.prepare (kSR); e.setAmount (0.0f);
        Vowel vw; std::vector<float> in (N/6), out;
        for (size_t i = 0; i < in.size(); ++i) in[i] = vw.s (220.0, 0.5);
        run (e, in, out);
        bool same = (std::memcmp (in.data(), out.data(), in.size()*sizeof(float)) == 0);
        CHECK (same, "0%%: 入力とビット単位で一致(完全素通し)");
    }

    // ---------- 5: 無音 / フルスケール ----------
    {
        gz::cons::Enhancer e; e.prepare (kSR); e.setAmount (1.0f);
        std::vector<float> in ((size_t)kSR, 0.0f), out;
        run (e, in, out);
        bool ok = true; for (float x : out) if (x != 0.0f) ok = false;
        CHECK (ok, "無音: 出力も完全な無音");

        std::vector<float> fs ((size_t)kSR), fo;
        for (size_t i = 0; i < fs.size(); ++i) fs[i] = (i % 20 < 10) ? 1.0f : -1.0f;   // 2.4kHz矩形
        run (e, fs, fo);
        float pk = 0.0f; bool fin = true;
        for (float x : fo) { pk = std::max (pk, std::fabs (x)); if (! std::isfinite (x)) fin = false; }
        CHECK (fin, "フルスケール矩形波: すべて有限");
        CHECK (pk < 4.0f, "フルスケール矩形波: ピーク %.2f (<4.0)", pk);
    }

    // ---------- 6: 計算負荷 ----------
    {
        gz::cons::Enhancer e; e.prepare (kSR); e.setAmount (1.0f);
        Vowel vw; std::vector<float> L ((size_t)kSR*10), R ((size_t)kSR*10);
        for (size_t i = 0; i < L.size(); ++i) { L[i] = vw.s (220.0, 0.5); R[i] = L[i]*0.9f; }
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i + 128 <= L.size(); i += 128) e.process (L.data()+i, R.data()+i, 128);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
        CHECK (sec/10.0*100.0 < 3.0, "負荷: ステレオ実時間比 %.2f %% (<3%%)", sec/10.0*100.0);
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
