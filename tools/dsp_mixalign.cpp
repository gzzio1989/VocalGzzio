// dsp_mixalign.cpp — v2.8.0「Mixの原音を遅延にそろえる」の数値検証
//
//  何を確かめるか
//    ボイス変換などがONだと、加工側は約16ms(768サンプル)後ろにずれる。
//    そこへ遅れていない原音を Mix 50% で混ぜると、16ms のコムフィルタになり
//    62.5Hz おきに音が消える（＝Mixを中間にしたときだけスカスカになる）。
//    PluginProcessor.cpp と同じリングバッファの手順をここに写して、
//    「そろえた原音」なら周波数ごとの音量差が出ないことを数値で見る。
//
//  ビルド: g++ -O2 -std=c++17 dsp_mixalign.cpp -o dsp_mixalign
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static constexpr double kSR  = 48000.0;
static constexpr int    kLat = 768;      // vcSh の N-hop（窓1024 / 4重ね）

// 本体と同じ形のリング遅延。ブロックごとに呼ぶ。
struct DryRing
{
    std::vector<float> buf;
    int w = 0;
    void prepare (int len) { buf.assign ((size_t) len, 0.0f); w = 0; }
    void process (const float* in, float* out, int n, int lat)
    {
        const int len = (int) buf.size();
        int ww = w;
        for (int i = 0; i < n; ++i)
        {
            buf[(size_t) ww] = in[i];
            int rd = ww - lat; if (rd < 0) rd += len;
            out[i] = buf[(size_t) rd];
            if (++ww >= len) ww = 0;
        }
        w = (w + n) % len;
    }
};

// 「加工側」の代わり: 純粋に kLat だけ遅らせた信号（位相以外は同じ）を作る。
// 実機のシフターは中身も変えるが、コムフィルタが出るかどうかは
// 「遅れているものと遅れていないものを足すか」だけで決まる。
static double rmsAt (double hz, bool alignDry, int block)
{
    const int total = 24000;                    // 0.5 秒
    std::vector<float> in ((size_t) total), wetDelayed ((size_t) total, 0.0f);
    for (int n = 0; n < total; ++n)
        in[(size_t) n] = (float) std::sin (2.0 * M_PI * hz * n / kSR);
    for (int n = kLat; n < total; ++n) wetDelayed[(size_t) n] = in[(size_t) (n - kLat)];

    DryRing ring; ring.prepare (kLat + block * 2 + 8);
    std::vector<float> outBuf ((size_t) total, 0.0f), tmp ((size_t) block);

    for (int p = 0; p + block <= total; p += block)
    {
        if (alignDry) ring.process (&in[(size_t) p], tmp.data(), block, kLat);
        else          std::copy (in.begin() + p, in.begin() + p + block, tmp.begin());
        for (int i = 0; i < block; ++i)
            outBuf[(size_t) (p + i)] = 0.5f * wetDelayed[(size_t) (p + i)] + 0.5f * tmp[(size_t) i];
    }
    // 最初の 0.2 秒は立ち上がりなので捨てる
    double s = 0.0; int c = 0;
    for (int n = 9600; n < total; ++n) { s += (double) outBuf[(size_t) n] * outBuf[(size_t) n]; ++c; }
    return std::sqrt (s / std::max (1, c));
}

int main()
{
    std::printf ("Mixの原音そろえ @ %.0fHz / 遅延 %d サンプル (%.1f ms)\n",
                 kSR, kLat, kLat * 1000.0 / kSR);

    // コムの谷は 1/(2*16ms) = 31.25Hz の奇数倍。31.25Hz と 93.75Hz が谷、62.5Hz が山。
    const double dip1 = 31.25, dip2 = 93.75, peak = 62.5;

    // ---- 1: 直す前（そろえない）は谷ができる ----
    {
        const double a = rmsAt (dip1, false, 128);
        const double b = rmsAt (peak, false, 128);
        const double spreadDb = 20.0 * std::log10 (b / std::max (1e-9, a));
        CHECK (spreadDb > 20.0,
               "そろえないと 31.25Hz が %.1f dB も落ちる (>20dB = コムが出ている)", spreadDb);
    }

    // ---- 2: 直したあと（そろえる）は周波数で差が出ない ----
    for (int block : { 64, 128, 512 })
    {
        const double a = rmsAt (dip1, true, block);
        const double b = rmsAt (peak, true, block);
        const double c = rmsAt (dip2, true, block);
        const double lo = std::min (a, std::min (b, c));
        const double hi = std::max (a, std::max (b, c));
        const double spreadDb = 20.0 * std::log10 (hi / std::max (1e-9, lo));
        CHECK (spreadDb < 0.5,
               "そろえるとバッファ%4d でも差は %.2f dB (<0.5dB = 平ら)", block, spreadDb);
    }

    // ---- 3: 遅延ゼロ(シフターOFF)のときは原音がそのまま出るか ----
    {
        DryRing ring; ring.prepare (kLat + 256 + 8);
        std::vector<float> in (128), out (128);
        for (int n = 0; n < 128; ++n) in[(size_t) n] = (float) (n + 1);
        ring.process (in.data(), out.data(), 128, 0);   // lat = 0
        bool same = true;
        for (int n = 0; n < 128; ++n) if (out[(size_t) n] != in[(size_t) n]) same = false;
        CHECK (same, "遅延0なら原音は1サンプルも変わらない");
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
