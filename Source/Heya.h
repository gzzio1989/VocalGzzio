#pragma once
// ============================================================================
// Heya.h — へや（計算生成した空間IR・ゼロ遅延コンボリューション）
//
//  なにをするものか:
//    実際の部屋の反射を物理計算で起こした「部屋の指紋(IR)」に声を通す。
//    アルゴリズムの残響(juce::dsp::Reverb)と違い、壁の一次反射・二次反射が
//    実在の部屋と同じ時刻に並ぶので、「その場所にいる」音になる。
//
//  なぜゼロ遅延でできるのか（この製品の生命線なので書き残す）:
//    畳み込みは普通、FFTのブロックぶん(数ms〜数十ms)遅れる。ここでは
//    Gardner(1995)の分割法を使う——
//      ・IRの先頭 256 サンプルは「直接FIR」= 1サンプルずつ即座に畳む（遅延0）
//      ・256 サンプル目より先は、どうせ 256 サンプル後にしか出番が来ない。
//        だから「1ホップ前までの入力」でFFT計算しても間に合う。
//    → どんなホストブロックサイズ(32でも4096でも)でも追加遅延は 0 サンプル。
//      これは願望ではなく tools/dsp_heya.cpp が毎回、直接畳み込みとの
//      サンプル一致で証明する。
//
//  部屋はどう作っているか（権利は100%クリア＝全部このファイルが計算で起こす）:
//    ・前半(〜80ms): 鏡像法(image source)。箱型の部屋に音源と両耳を置き、
//      壁で折り返した鏡像からの到達時刻・距離減衰・壁での高域吸収を足し込む。
//      左右の耳は 18cm 離して別々に計算する＝本物の広がりと定位。
//    ・後半: 帯域別のRT60(残響時間)を持つ減衰ノイズ。6帯域に分けて
//      「低音は長く・高音は短く」など部屋ごとの性格を作る。左右は別のノイズ
//      （ただし低音側は少し混ぜて自然な相関を残す）。
//    ・プレートだけは鏡像法を使わず、最初から密度の高い拡散にする（鉄板に
//      部屋は無いので）。
//
//  約束ごと（VocalGzzio の掟）:
//    ・process() の中で new/malloc をしない。すべて prepare() で確保。
//    ・JUCEに依存しない（tools/ の検査が g++ 一発で回るように）。
//    ・部屋の切替は音声スレッドから安全（10msのフェードで無音クリック無し）。
//    ・同じサンプルレートなら prepare() を何度呼んでも作り直さない
//      （再生開始のたびに待たせない）。
// ============================================================================

#include <vector>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <mutex>

namespace heya
{

static constexpr int kNumRooms = 6;

//==============================================================================
// 決定的な乱数（同じ部屋は誰のPCでも同じ音）
//==============================================================================
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed ? seed : 0x9E3779B9u) {}
    inline uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float uni() noexcept   { return (float) (next() >> 8) * (1.0f / 16777216.0f); }        // 0..1
    inline float bi()  noexcept   { return uni() * 2.0f - 1.0f; }                                  // -1..1
    // ほぼ正規分布（12個の和）— テールのノイズが「サー」でなく「ホワッ」と鳴るため
    inline float gauss() noexcept { float a = 0; for (int i = 0; i < 4; ++i) a += bi(); return a * 0.5f; }
};

//==============================================================================
// 生成用の小さなフィルタ（IRを起こすときだけ使う。音声スレッドでは使わない）
//==============================================================================
struct GenBiquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float tick (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void bandpass (double f, double q, double sr)
    {
        const double w = 2.0 * 3.14159265358979323846 * std::min (f, sr * 0.45) / sr;
        const double c = std::cos (w), s = std::sin (w), al = s / (2.0 * q), ia = 1.0 / (1.0 + al);
        b0 = al * ia; b1 = 0; b2 = -al * ia; a1 = -2.0 * c * ia; a2 = (1.0 - al) * ia; reset();
    }
    void highpass (double f, double q, double sr)
    {
        const double w = 2.0 * 3.14159265358979323846 * std::min (f, sr * 0.45) / sr;
        const double c = std::cos (w), s = std::sin (w), al = s / (2.0 * q), ia = 1.0 / (1.0 + al);
        b0 = (1 + c) * 0.5 * ia; b1 = -(1 + c) * ia; b2 = (1 + c) * 0.5 * ia;
        a1 = -2.0 * c * ia; a2 = (1 - al) * ia; reset();
    }
    void lowpass (double f, double q, double sr)
    {
        const double w = 2.0 * 3.14159265358979323846 * std::min (f, sr * 0.45) / sr;
        const double c = std::cos (w), s = std::sin (w), al = s / (2.0 * q), ia = 1.0 / (1.0 + al);
        b0 = (1 - c) * 0.5 * ia; b1 = (1 - c) * ia; b2 = (1 - c) * 0.5 * ia;
        a1 = -2.0 * c * ia; a2 = (1 - al) * ia; reset();
    }
};

//==============================================================================
// FFT（基数2・反復型）。prepare で表を作り、実行時は確保ゼロ。
//==============================================================================
class Fft
{
public:
    void prepare (int fftOrder)
    {
        order = fftOrder; size = 1 << order;
        rev.resize ((size_t) size);
        for (int i = 0; i < size; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b) r = (r << 1) | ((i >> b) & 1);
            rev[(size_t) i] = r;
        }
        size_t totalTw = 0;
        for (int len = 8; len <= size; len <<= 1) totalTw += (size_t) (len >> 1);
        stageCos.resize (totalTw);
        stageSin.resize (totalTw);
        size_t pos = 0;
        for (int len = 8; len <= size; len <<= 1)
        {
            const int half = len >> 1;
            const double baseAng = -2.0 * 3.14159265358979323846 / (double) len;
            for (int j = 0; j < half; ++j)
            {
                const double a = baseAng * (double) j;
                stageCos[pos + (size_t) j] = (float) std::cos (a);
                stageSin[pos + (size_t) j] = (float) std::sin (a);
            }
            pos += (size_t) half;
        }
    }

    // その場で複素FFT。ステージ連続配置の回転因子と len=2/4 乗算ゼロ特化段で SIMD ベクトル化を引き出す。
    void run (float* __restrict re, float* __restrict im, bool inverse) const noexcept
    {
        const int n = size;
        const int* __restrict rv = rev.data();
        for (int i = 0; i < n; ++i)
        {
            const int r = rv[i];
            if (r > i)
            {
                const float tr = re[i]; re[i] = re[r]; re[r] = tr;
                const float ti = im[i]; im[i] = im[r]; im[r] = ti;
            }
        }
        if (n >= 2)
        {
            for (int i = 0; i < n; i += 2)
            {
                const float ar = re[i],     ai = im[i];
                const float br = re[i + 1], bi = im[i + 1];
                re[i]     = ar + br; im[i]     = ai + bi;
                re[i + 1] = ar - br; im[i + 1] = ai - bi;
            }
        }
        if (n >= 4)
        {
            if (! inverse)
            {
                for (int i = 0; i < n; i += 4)
                {
                    const float ar0 = re[i],     ai0 = im[i];
                    const float br0 = re[i + 2], bi0 = im[i + 2];
                    re[i]     = ar0 + br0; im[i]     = ai0 + bi0;
                    re[i + 2] = ar0 - br0; im[i + 2] = ai0 - bi0;

                    const float ar1 = re[i + 1], ai1 = im[i + 1];
                    const float br1 = re[i + 3], bi1 = im[i + 3];
                    re[i + 1] = ar1 + bi1; im[i + 1] = ai1 - br1;
                    re[i + 3] = ar1 - bi1; im[i + 3] = ai1 + br1;
                }
            }
            else
            {
                for (int i = 0; i < n; i += 4)
                {
                    const float ar0 = re[i],     ai0 = im[i];
                    const float br0 = re[i + 2], bi0 = im[i + 2];
                    re[i]     = ar0 + br0; im[i]     = ai0 + bi0;
                    re[i + 2] = ar0 - br0; im[i + 2] = ai0 - bi0;

                    const float ar1 = re[i + 1], ai1 = im[i + 1];
                    const float br1 = re[i + 3], bi1 = im[i + 3];
                    re[i + 1] = ar1 - bi1; im[i + 1] = ai1 + br1;
                    re[i + 3] = ar1 + bi1; im[i + 3] = ai1 - br1;
                }
            }
        }
        size_t twPos = 0;
        if (! inverse)
        {
            for (int len = 8; len <= n; len <<= 1)
            {
                const int half = len >> 1;
                const float* __restrict wc = stageCos.data() + twPos;
                const float* __restrict ws = stageSin.data() + twPos;
                for (int i = 0; i < n; i += len)
                {
                    float* __restrict r0 = re + i;
                    float* __restrict i0 = im + i;
                    float* __restrict r1 = r0 + half;
                    float* __restrict i1 = i0 + half;
                    for (int j = 0; j < half; ++j)
                    {
                        const float wr = wc[j], wi = ws[j];
                        const float br = r1[j], bi = i1[j];
                        const float xr = br * wr - bi * wi;
                        const float xi = br * wi + bi * wr;
                        const float ar = r0[j], ai = i0[j];
                        r0[j] = ar + xr; i0[j] = ai + xi;
                        r1[j] = ar - xr; i1[j] = ai - xi;
                    }
                }
                twPos += (size_t) half;
            }
        }
        else
        {
            for (int len = 8; len <= n; len <<= 1)
            {
                const int half = len >> 1;
                const float* __restrict wc = stageCos.data() + twPos;
                const float* __restrict ws = stageSin.data() + twPos;
                for (int i = 0; i < n; i += len)
                {
                    float* __restrict r0 = re + i;
                    float* __restrict i0 = im + i;
                    float* __restrict r1 = r0 + half;
                    float* __restrict i1 = i0 + half;
                    for (int j = 0; j < half; ++j)
                    {
                        const float wr = wc[j], wi = ws[j];
                        const float br = r1[j], bi = i1[j];
                        const float xr = br * wr + bi * wi;
                        const float xi = bi * wr - br * wi;
                        const float ar = r0[j], ai = i0[j];
                        r0[j] = ar + xr; i0[j] = ai + xi;
                        r1[j] = ar - xr; i1[j] = ai - xi;
                    }
                }
                twPos += (size_t) half;
            }
            const float g = 1.0f / (float) n;
            for (int i = 0; i < n; ++i) { re[i] *= g; im[i] *= g; }
        }
    }

    int order = 0, size = 0;
private:
    std::vector<float> stageCos, stageSin;
    std::vector<int> rev;
};

//==============================================================================
// 部屋の設計図
//==============================================================================
struct RoomSpec
{
    const char* nameJa;      // 画面に出す名前（UTF-8）
    const char* nameEn;      // オートメーション表示
    float lx, ly, lz;        // 部屋の寸法(m)。プレートは 0 = 鏡像法を使わない
    float absorb;            // 壁の吸音(0=鏡・1=無響)。高域はこれより速く死ぬ
    float rt60[6];           // 残響時間(s) 帯域: 125/250/500/1k/2k/4k(以上)Hz
    float tailSec;           // IRの長さ(s)
    float erGain;            // 一次反射の強さ
    float lowCorr;           // 低音側の左右相関(0..1)
    float preDelayMs;        // おすすめプリディレイ（Processor側の表から使う）
    float tilt;              // 明るさの傾き(dB/oct 相当の味付け, +で明るい)
};

// ★6つの部屋。数字は実測RT60の相場（コンサートホール2.0-2.6s、浴室1.2-1.8s、
//   録音ブース0.15-0.3s、ライブハウス0.8-1.2s 等）に合わせてある。
inline const std::array<RoomSpec, kNumRooms>& rooms()
{
    static const std::array<RoomSpec, kNumRooms> r = { {
        // おふろ: 小さな固い箱。タイルは高音まで生き残り、フラッターが鳴る
        { "\xe3\x81\x8a\xe3\x81\xb5\xe3\x82\x8d",                                  "Bath IR",
          2.1f, 1.7f, 2.3f, 0.10f, { 1.10f, 1.30f, 1.45f, 1.45f, 1.30f, 1.00f }, 1.6f, 1.00f, 0.55f,  8.0f,  1.5f },
        // カラオケ箱: ソファと絨毯の小部屋。短くて温かい
        { "\xe3\x82\xab\xe3\x83\xa9\xe3\x82\xaa\xe3\x82\xb1\xe7\xae\xb1",          "Karaoke IR",
          3.4f, 2.6f, 2.4f, 0.38f, { 0.55f, 0.50f, 0.45f, 0.40f, 0.33f, 0.26f }, 0.7f, 0.80f, 0.60f, 10.0f, -1.0f },
        // 録音スタジオ: 吸音ブース。ほぼ響かないのに「部屋の空気」だけ残る
        { "\xe9\x8c\xb2\xe9\x9f\xb3\xe3\x82\xb9\xe3\x82\xbf\xe3\x82\xb8\xe3\x82\xaa", "Studio IR",
          2.0f, 1.6f, 2.2f, 0.62f, { 0.30f, 0.26f, 0.22f, 0.18f, 0.15f, 0.12f }, 0.45f, 0.55f, 0.70f,  5.0f, -0.5f },
        // ライブハウス: 中箱。中域が前に出る、ステージの音
        { "\xe3\x83\xa9\xe3\x82\xa4\xe3\x83\x96\xe3\x83\x8f\xe3\x82\xa6\xe3\x82\xb9", "Livehouse IR",
          13.0f, 9.0f, 4.2f, 0.30f, { 1.30f, 1.20f, 1.10f, 1.00f, 0.85f, 0.60f }, 1.5f, 0.70f, 0.45f, 16.0f,  0.0f },
        // コンサートホール: 大箱。低音が長く、なめらかに消える
        { "\xe3\x82\xb3\xe3\x83\xb3\xe3\x82\xb5\xe3\x83\xbc\xe3\x83\x88\xe3\x83\x9b\xe3\x83\xbc\xe3\x83\xab", "Hall IR",
          30.0f, 21.0f, 13.0f, 0.24f, { 2.55f, 2.40f, 2.25f, 2.05f, 1.70f, 1.20f }, 2.7f, 0.55f, 0.35f, 28.0f, -1.5f },
        // プレート: 鉄板。部屋ではないので鏡像は無し、最初から密い
        { "\xe3\x83\x97\xe3\x83\xac\xe3\x83\xbc\xe3\x83\x88",                          "Plate IR",
          0.0f, 0.0f, 0.0f, 0.0f,  { 2.20f, 2.20f, 2.15f, 2.05f, 1.90f, 1.55f }, 2.4f, 0.0f, 0.30f, 20.0f,  2.0f },
    } };
    return r;
}

//==============================================================================
// IRを起こす（prepare 時だけ動く。音声スレッドでは呼ばない）
//==============================================================================
struct GeneratedIr
{
    std::vector<float> L, R;   // 部屋のインパルス応答（直接音は含まない＝ドライはドライのまま）
    int lengthSamples = 0;
    // 共鳴安全弁FFT用の作業バッファ（事前確保されていれば動的ヒープ確保ゼロで再利用）
    std::vector<float> capRe, capIm;
    std::vector<Fft> capFfts;
};

inline void generateRoomIr (int roomIndex, double sr, GeneratedIr& out)
{
    const RoomSpec& spec = rooms()[(size_t) roomIndex];
    const int N = std::max (256, (int) std::lround (spec.tailSec * sr));
    if ((int) out.L.capacity() < N) out.L.reserve ((size_t) N);
    if ((int) out.R.capacity() < N) out.R.reserve ((size_t) N);
    out.L.resize ((size_t) N);
    out.R.resize ((size_t) N);
    std::fill (out.L.begin(), out.L.end(), 0.0f);
    std::fill (out.R.begin(), out.R.end(), 0.0f);
    out.lengthSamples = N;

    const float c = 343.0f;                    // 音速 m/s
    const bool isPlate = spec.lx <= 0.0f;

    // ---- 1) 鏡像法の一次〜高次反射（プレート以外） -------------------------
    if (! isPlate)
    {
        // 音源(歌う人の口)と両耳。部屋の中で少し非対称に置く（対称だと櫛になる）
        const float sx = spec.lx * 0.38f, sy = spec.ly * 0.44f, sz = 1.55f;
        const float ex = spec.lx * 0.55f, ey = spec.ly * 0.52f, ez = 1.58f;
        const float earOff = 0.09f;            // 両耳 18cm

        const float erWindowSec = std::min (0.085f, spec.tailSec * 0.5f);
        const int maxOrd = 24;                 // 折り返し回数の上限（時間窓が先に効く）

        for (int ox = -maxOrd; ox <= maxOrd; ++ox)
        for (int oy = -maxOrd; oy <= maxOrd; ++oy)
        for (int oz = -6; oz <= 6; ++oz)
        {
            if (ox == 0 && oy == 0 && oz == 0) continue;   // 直接音は入れない
            // 鏡像の座標: 偶数回折り返し = 2kL+s、奇数回 = 2kL-s（→ oxL + (L-s)）
            const float mx = ox * spec.lx + (((ox & 1) == 0) ? sx : spec.lx - sx);
            const float my = oy * spec.ly + (((oy & 1) == 0) ? sy : spec.ly - sy);
            const float mz = oz * spec.lz + (((oz & 1) == 0) ? sz : spec.lz - sz);
            const int bounces = std::abs (ox) + std::abs (oy) + std::abs (oz);
            if (bounces > maxOrd) continue;

            for (int ear = 0; ear < 2; ++ear)
            {
                const float exr = ex + (ear == 0 ? -earOff : earOff);
                const float dx = mx - exr, dy = my - ey, dz = mz - ez;
                const float dist = std::sqrt (dx * dx + dy * dy + dz * dz);
                const float tSec = dist / c;
                if (tSec > erWindowSec) continue;
                const int   idx = (int) std::lround (tSec * sr);
                if (idx <= 0 || idx >= N) continue;
                // 距離減衰 1/r と、壁で跳ねるたびの吸収 + 両耳間の頭部遮蔽(ILD/耳介減衰)
                const float wall = std::pow (1.0f - spec.absorb, (float) bounces);
                const float earSign = (ear == 0) ? -1.0f : 1.0f;
                const float latCos  = dx / std::max (0.6f, dist);
                const float shadow  = (latCos * earSign < 0.0f) ? (1.0f - 0.14f * std::abs (latCos)) : 1.0f;
                const float g = spec.erGain * wall * shadow / std::max (0.6f, dist);
                // 高次ほど高域が死ぬ→サンプル1本でなく2本に割ってなまらせる（反対耳は頭部遮蔽でさらに丸める）
                std::vector<float>& ch = (ear == 0 ? out.L : out.R);
                const float pinnaSoft = (latCos * earSign < 0.0f) ? 0.08f * std::abs (latCos) : 0.0f;
                const float soft = std::min (0.88f, 0.12f * (float) bounces + pinnaSoft);
                ch[(size_t) idx] += g * (1.0f - soft);
                if (idx + 1 < N) ch[(size_t) idx + 1] += g * soft;
            }
        }
    }

    // ---- 2) 帯域別に減衰するノイズテール（8kHz シルク空気帯域を含む7帯域） --
    {
        static const double bandHz[7] = { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };
        const int tailStart = isPlate ? (int) (0.004 * sr)
                                      : (int) (std::min (0.030f, spec.tailSec * 0.25f) * sr);
        for (int band = 0; band < 7; ++band)
        {
            GenBiquad bpL, bpR;
            const double q = (band == 6) ? 0.85 : 0.9;
            bpL.bandpass (bandHz[band], q, sr);
            bpR.bandpass (bandHz[band], q, sr);
            Rng rngL ((uint32_t) (roomIndex * 7919 + band * 131 + 17));
            Rng rngR ((uint32_t) (roomIndex * 7919 + band * 131 + 9173));

            // RT60 → 1サンプルごとの減衰（8kHz帯は空気吸収により4kHz帯の0.78倍で自然減衰）
            const float rtSpec = (band < 6) ? spec.rt60[band] : (spec.rt60[5] * 0.78f);
            const double rt = std::max (0.05f, rtSpec);
            const double decay = std::pow (10.0, -3.0 / (rt * sr));
            double envL = (band == 6) ? 0.55 : 1.0;
            double envR = (band == 6) ? 0.55 : 1.0;

            // 密度の立ち上がり（部屋は徐々に密になる。プレートは最初から密）
            const double densRamp = isPlate ? 0.002 : 0.020;

            for (int n = tailStart; n < N; ++n)
            {
                const double t = (n - tailStart) / sr;
                const double ramp = 1.0 - std::exp (-t / densRamp);
                float wL = bpL.tick (rngL.gauss());
                float wR0 = bpR.tick (rngR.gauss());
                // 低音側は左右を混ぜて自然な相関を残す
                const float mixCorr = spec.lowCorr * (band < 2 ? 1.0f : (band == 2 ? 0.5f : (band < 6 ? 0.15f : 0.05f)));
                const float wR = wR0 * (1.0f - mixCorr) + wL * mixCorr;
                out.L[(size_t) n] += (float) (wL * envL * ramp);
                out.R[(size_t) n] += (float) (wR * envR * ramp);
                envL *= decay; envR *= decay;
            }
        }
    }

    // ---- 3) 明るさの味付け（tilt）と超低域の掃除 ---------------------------
    {
        GenBiquad hpL, hpR; hpL.highpass (55.0, 0.71, sr); hpR.highpass (55.0, 0.71, sr);
        GenBiquad shL, shR;
        // tilt>0 = 高域を残す(=ローを軽く下げる)、tilt<0 = 高域を落とす
        if (spec.tilt >= 0.0f) { shL.highpass (120.0 + 60.0 * spec.tilt, 0.6, sr); shR.highpass (120.0 + 60.0 * spec.tilt, 0.6, sr); }
        else                   { shL.lowpass  (9000.0 * std::pow (2.0, spec.tilt * 0.5), 0.6, sr);
                                 shR.lowpass  (9000.0 * std::pow (2.0, spec.tilt * 0.5), 0.6, sr); }
        for (int n = 0; n < N; ++n)
        {
            out.L[(size_t) n] = shL.tick (hpL.tick (out.L[(size_t) n]));
            out.R[(size_t) n] = shR.tick (hpR.tick (out.R[(size_t) n]));
        }
    }

    // ---- 4) 音量を部屋どうしで揃える（切り替えても音量が跳ねない） ----------
    {
        double e = 0.0;
        for (int n = 0; n < N; ++n) e += (double) out.L[(size_t) n] * out.L[(size_t) n]
                                       + (double) out.R[(size_t) n] * out.R[(size_t) n];
        const double norm = e > 1e-12 ? 1.0 / std::sqrt (e * 0.5) : 1.0;
        // 全部屋を「総エネルギー=1」に。ミックス量の感じ方が部屋間で揃う
        const float g = (float) (norm * 0.72);
        for (int n = 0; n < N; ++n) { out.L[(size_t) n] *= g; out.R[(size_t) n] *= g; }
    }

    // ---- 5) 共鳴の安全弁 -----------------------------------------------------
    //   固い小部屋は本物でも1音だけワンと鳴る。味は残すが、事故になる高さは削る。
    //   判定は願望でなく |H(f)| の実測（FFT）。作業領域が事前確保済みなら動的確保ゼロ。
    {
        int ord = 1; while ((1 << ord) < N * 2) ++ord;
        const int NF = 1 << ord;
        if ((int) out.capRe.capacity() < NF) out.capRe.reserve ((size_t) NF);
        if ((int) out.capIm.capacity() < NF) out.capIm.reserve ((size_t) NF);
        out.capRe.resize ((size_t) NF);
        out.capIm.resize ((size_t) NF);
        std::fill (out.capRe.begin(), out.capRe.end(), 0.0f);
        std::fill (out.capIm.begin(), out.capIm.end(), 0.0f);
        Fft localFft;
        Fft& f = (ord >= 1 && ord < (int) out.capFfts.size()) ? out.capFfts[(size_t) ord] : localFft;
        if (f.order != ord) f.prepare (ord);
        for (int n = 0; n < N; ++n) out.capRe[(size_t) n] = out.L[(size_t) n];
        f.run (out.capRe.data(), out.capIm.data(), false);
        double hMax = 0.0;
        const int kLo = (int) (80.0 * NF / sr), kHi = (int) (12000.0 * NF / sr);
        for (int k = std::max (1, kLo); k < std::min (NF / 2, kHi); ++k)
            hMax = std::max (hMax, std::sqrt ((double) out.capRe[(size_t) k] * out.capRe[(size_t) k]
                                            + (double) out.capIm[(size_t) k] * out.capIm[(size_t) k]));
        const double cap = 3.5;   // ≈ +10.9dB
        if (hMax > cap)
        {
            const float g2 = std::max (0.64f, (float) (cap / hMax));
            for (int n = 0; n < N; ++n) { out.L[(size_t) n] *= g2; out.R[(size_t) n] *= g2; }
        }
    }
}

//==============================================================================
// ゼロ遅延・分割コンボリューション本体
//
//  負荷のならし（v4.0.0 で実測して入れた仕組み）:
//    素朴に作ると「ホップ境界の1サンプル」に全パーティションの積和が集中し、
//    ホールで締切の80%を一撃で食う（このVMでの実測）。ここでは
//    p=1..M-1 の積和を **次の256サンプルに均等に散らし**、境界では
//    FFT・p=0・逆FFTだけをやる。答えは同じ・山だけ消える。
//==============================================================================
class Convolver
{
public:
    static constexpr int B = 256;         // パーティション（ホップ）長
    static constexpr int FFT_ORDER = 9;   // 512
    static constexpr int NFFT = 1 << FFT_ORDER;
    // ★実数入力のスペクトルはエルミート対称なので、下半分＋ナイキストだけ持てば足りる。
    //   積和(macPartition/finishHop)はもともと k<=NFFT/2 しか読んでいなかったので、
    //   上半分は「書いているが誰も読まない」死んだメモリだった。
    //   257/512 に詰めて、記憶容量をほぼ半分にする。音は1サンプルも変わらない。
    static constexpr int NBINS = NFFT / 2 + 1;   // 257
    // カスタムIR用トリプルバッファ・スロット（鳩の巣原理により curRoom/pendingRoom と絶対に衝突しない空きスロットを保証）
    static constexpr int kCustomSlotA = kNumRooms;
    static constexpr int kCustomSlotB = kNumRooms + 1;
    static constexpr int kCustomSlotC = kNumRooms + 2;

    // 全部屋ぶんのスペクトルを prepare で作り置き（またはプロセス内サンプルレート別キャッシュから即複写）。
    // 事前確保は prepare で済ませるため、実行時の部屋切替・カスタムIR切替はヒープ確保もストールもゼロ。
    void prepare (double sampleRate, int /*maxBlock*/, bool lazyRooms = false)
    {
        if (std::abs (sampleRate - sr) < 0.5 && ! roomData.empty())
        {
            if (! lazyRooms)
                populateBuiltInRoomsFromCache (sr);
            reset();
            return;
        }

        sr = sampleRate;
        fadeCoef = (float) (1.0 - std::pow (1.0 - 0.0035, 48000.0 / sr));
        sizeSlew = (float) (1.0 - std::pow (0.75, 48000.0 / sr));
        fft.prepare (FFT_ORDER);

        roomData.clear();
        roomData.resize (kNumRooms + 3);
        roomBuilt.fill (false);
        maxParts = std::max (1, ((int) std::lround (3.0 * sr) - B + B - 1) / B);

        int maxIrLen = 256;
        int maxCapNf = 512;
        if (lazyIr.capFfts.size() < 22)
            lazyIr.capFfts.resize (22);
        for (int room = 0; room < kNumRooms; ++room)
        {
            const int nRoom = std::max (256, (int) std::lround (rooms()[(size_t) room].tailSec * sr));
            maxIrLen = std::max (maxIrLen, nRoom);
            int ord = 1; while ((1 << ord) < nRoom * 2) ++ord;
            maxCapNf = std::max (maxCapNf, 1 << ord);
            if (ord >= 1 && ord < (int) lazyIr.capFfts.size() && lazyIr.capFfts[(size_t) ord].order != ord)
                lazyIr.capFfts[(size_t) ord].prepare (ord);

            const int npRoom = std::max (1, ( std::max (0, nRoom - B) + B - 1 ) / B);
            maxParts = std::max (maxParts, npRoom);
            RoomData& rd = roomData[(size_t) room];
            rd.headRevL.assign ((size_t) B, 0.0f);
            rd.headRevR.assign ((size_t) B, 0.0f);
            rd.numParts = npRoom;
            rd.spec.assign ((size_t) npRoom * NBINS * 4, 0.0f);
        }

        // オンデマンド構築用の作業バッファも共鳴安全弁FFT領域ごと prepare で一括確保（動的確保を完全ゼロにする）
        lazyIr.L.assign ((size_t) maxIrLen, 0.0f);
        lazyIr.R.assign ((size_t) maxIrLen, 0.0f);
        lazyIr.capRe.assign ((size_t) maxCapNf, 0.0f);
        lazyIr.capIm.assign ((size_t) maxCapNf, 0.0f);
        lazyPartRe.assign ((size_t) NFFT, 0.0f);
        lazyPartIm.assign ((size_t) NFFT, 0.0f);

        if (! lazyRooms)
        {
            populateBuiltInRoomsFromCache (sr);
        }

        // カスタムIR用トリプルバッファ・スロットも事前確保（UIからの非同期ロード時の再確保・競合を防ぐ）
        for (int s = kCustomSlotA; s <= kCustomSlotC; ++s)
        {
            RoomData& crd = roomData[(size_t) s];
            crd.headRevL.assign ((size_t) B, 0.0f);
            crd.headRevR.assign ((size_t) B, 0.0f);
            crd.numParts = 1;
            crd.spec.assign ((size_t) std::max (1, maxParts) * NBINS * 4, 0.0f);
        }

        // 実行時バッファ（最大の部屋に合わせて1回だけ確保）
        const int mp = std::max (1, maxParts);
        fdlRe.assign  ((size_t) mp * NBINS, 0.0f);
        fdlIm.assign  ((size_t) mp * NBINS, 0.0f);
        fdlReR.assign ((size_t) mp * NBINS, 0.0f);
        fdlImR.assign ((size_t) mp * NBINS, 0.0f);
        accRe.assign  ((size_t) NFFT, 0.0f);  accIm.assign  ((size_t) NFFT, 0.0f);
        accReR.assign ((size_t) NFFT, 0.0f);  accImR.assign ((size_t) NFFT, 0.0f);
        workRe.assign ((size_t) NFFT, 0.0f);  workIm.assign ((size_t) NFFT, 0.0f);
        partGain.assign ((size_t) mp, 1.0f);
        partGainTarget.assign ((size_t) mp, 1.0f);
        histL.assign ((size_t) (2 * B), 0.0f); histR.assign ((size_t) (2 * B), 0.0f);
        hopL.assign ((size_t) (2 * B), 0.0f);  hopR.assign ((size_t) (2 * B), 0.0f);
        tailL.assign ((size_t) B, 0.0f);       tailR.assign ((size_t) B, 0.0f);

        if (hasCustom.load (std::memory_order_acquire) && ! savedCustomL.empty())
        {
            const int cSlot = activeCustomSlot.load (std::memory_order_acquire);
            buildRoomSlot (roomData[(size_t) cSlot],
                           savedCustomL.data(), savedCustomR.data(), (int) savedCustomL.size(),
                           lazyPartRe, lazyPartIm);
            curRoom = cSlot;
            pendingRoom = cSlot;
        }
        else
        {
            curRoom = lastBuiltInRoom;
            pendingRoom = lastBuiltInRoom;
        }
        audioCurRoom.store (curRoom, std::memory_order_relaxed);
        audioPendingRoom.store (pendingRoom, std::memory_order_relaxed);
        sizeAmt = 1.0f; sizeDirty = true;
        reset();
    }

    void reset() noexcept
    {
        std::fill (fdlRe.begin(),  fdlRe.end(),  0.0f);
        std::fill (fdlIm.begin(),  fdlIm.end(),  0.0f);
        std::fill (fdlReR.begin(), fdlReR.end(), 0.0f);
        std::fill (fdlImR.begin(), fdlImR.end(), 0.0f);
        std::fill (accRe.begin(),  accRe.end(),  0.0f);
        std::fill (accIm.begin(),  accIm.end(),  0.0f);
        std::fill (accReR.begin(), accReR.end(), 0.0f);
        std::fill (accImR.begin(), accImR.end(), 0.0f);
        std::fill (histL.begin(), histL.end(), 0.0f);
        std::fill (histR.begin(), histR.end(), 0.0f);
        std::fill (hopL.begin(),  hopL.end(),  0.0f);
        std::fill (hopR.begin(),  hopR.end(),  0.0f);
        std::fill (tailL.begin(), tailL.end(), 0.0f);
        std::fill (tailR.begin(), tailR.end(), 0.0f);
        histPos = 0; hopPos = 0; fdlPos = 0; spreadPart = 1;

        if (! roomData.empty())
        {
            const int target = hasCustom.load (std::memory_order_acquire)
                             ? activeCustomSlot.load (std::memory_order_acquire)
                             : lastBuiltInRoom;
            pendingRoom = target;
        }
        // ★入れ替え途中だったら、ここで**やりきる**こと。
        curRoom = pendingRoom;
        audioCurRoom.store (curRoom, std::memory_order_relaxed);
        audioPendingRoom.store (pendingRoom, std::memory_order_relaxed);
        fadeGain = 1.0f; fadeTarget = 1.0f;
        if (! partGain.empty()) updatePartGains (true);
    }

    /** 指定された部屋(0..5)のIRが未構築なら、キャッシュ複写または事前確保済みバッファで構築する（動的確保ゼロ）。 */
    void ensureRoomBuilt (int room) noexcept
    {
        if (roomData.empty() || sr <= 0.0) return;
        const int rClamped = std::min (kNumRooms - 1, std::max (0, room));
        if (roomBuilt[(size_t) rClamped]) return;
        if (tryCopySingleRoomFromCache (sr, rClamped, roomData[(size_t) rClamped]))
        {
            roomBuilt[(size_t) rClamped] = true;
            return;
        }
        generateRoomIr (rClamped, sr, lazyIr);
        buildRoomSlot (roomData[(size_t) rClamped], lazyIr.L.data(), lazyIr.R.data(), lazyIr.lengthSamples,
                       lazyPartRe, lazyPartIm);
        roomBuilt[(size_t) rClamped] = true;
    }

    bool isRoomBuilt (int room) const noexcept
    {
        const int rClamped = std::min (kNumRooms - 1, std::max (0, room));
        return roomBuilt[(size_t) rClamped];
    }

    /** 部屋を選ぶ(0..5)。鳴っている最中でも安全（10msフェードで入れ替え）。 */
    void setRoom (int room) noexcept
    {
        lastBuiltInRoom = std::min (kNumRooms - 1, std::max (0, room));
        syncTargetRoom();
    }

    /** 外部ファイル等のカスタム応答波形を読み込み、ロックフリー三面スロット経由で10msフェード切替する。 */
    bool loadCustomIr (const float* irL, const float* irR, int numSamples)
    {
        if (irL == nullptr || numSamples <= 0) return false;
        const float* srcR = (irR != nullptr) ? irR : irL;
        const double useSr = (sr > 8000.0) ? sr : 48000.0;
        const int maxLen = std::max (B * 2, (int) std::lround (3.0 * useSr));
        const int N = std::min (numSamples, maxLen);

        savedCustomL.assign (irL, irL + N);
        savedCustomR.assign (srcR, srcR + N);

        // エネルギー正規化（内蔵の部屋と音量感を揃えて耳を守る）
        double e = 0.0;
        for (int n = 0; n < N; ++n)
            e += (double) savedCustomL[(size_t) n] * savedCustomL[(size_t) n]
               + (double) savedCustomR[(size_t) n] * savedCustomR[(size_t) n];
        const float normG = (e > 1e-12) ? (float) (0.72 / std::sqrt (e * 0.5)) : 1.0f;
        for (int n = 0; n < N; ++n)
        {
            savedCustomL[(size_t) n] *= normG;
            savedCustomR[(size_t) n] *= normG;
        }

        if (roomData.size() >= (size_t) (kCustomSlotC + 1))
        {
            // 音声スレッドが現在読んでいる curRoom / pendingRoom のどちらとも衝突しない空きスロットを選ぶ
            const int inUseCur  = audioCurRoom.load (std::memory_order_acquire);
            const int inUsePend = audioPendingRoom.load (std::memory_order_acquire);
            const int prevAct   = activeCustomSlot.load (std::memory_order_relaxed);
            int nextSlot = kCustomSlotA;
            bool picked = false;
            for (int s = kCustomSlotA; s <= kCustomSlotC; ++s)
            {
                if (s != inUseCur && s != inUsePend && s != prevAct) { nextSlot = s; picked = true; break; }
            }
            if (! picked)
            {
                for (int s = kCustomSlotA; s <= kCustomSlotC; ++s)
                    if (s != inUseCur && s != inUsePend) { nextSlot = s; break; }
            }
            std::vector<float> customRe ((size_t) NFFT, 0.0f), customIm ((size_t) NFFT, 0.0f);
            buildRoomSlot (roomData[(size_t) nextSlot], savedCustomL.data(), savedCustomR.data(), N,
                           customRe, customIm);
            activeCustomSlot.store (nextSlot, std::memory_order_release);
            hasCustom.store (true, std::memory_order_release);
        }
        else
        {
            hasCustom.store (true, std::memory_order_release);
        }
        return true;
    }

    /** カスタム応答を解除し、内蔵の部屋へフェード復帰する（UIスレッドからロックフリーで通知）。 */
    void clearCustomIr() noexcept
    {
        hasCustom.store (false, std::memory_order_release);
    }

    bool isCustomIrActive() const noexcept { return hasCustom.load (std::memory_order_acquire); }

    /** 部屋の広さ 0..1（revsize をそのまま渡す）。テールの長さを実時間で変える。 */
    void setSize (float amt01) noexcept
    {
        const float a = std::min (1.0f, std::max (0.0f, amt01));
        if (std::abs (a - sizeAmt) > 0.002f) { sizeAmt = a; sizeDirty = true; }
    }

    int  currentRoom() const noexcept
    {
        return hasCustom.load (std::memory_order_relaxed)
             ? lastBuiltInRoom
             : std::min (kNumRooms - 1, std::max (0, audioCurRoom.load (std::memory_order_relaxed)));
    }

    /** 部屋の減衰カーブ（画面用）。0..1 の縦値を nPoints ぶん詰める。 */
    void getDecayCurve (int room, float* dest, int nPoints) const
    {
        if (dest == nullptr || nPoints <= 0) return;
        const int rClamped = std::min (kNumRooms - 1, std::max (0, room));
        const bool useCust = hasCustom.load (std::memory_order_acquire);
        const int cSlot = activeCustomSlot.load (std::memory_order_acquire);
        if (roomData.empty() || (! useCust && ! roomBuilt[(size_t) rClamped]))
        {
            const RoomSpec& sp = rooms()[(size_t) rClamped];
            const float rt = std::max (0.12f, 0.5f * (sp.rt60[2] + sp.rt60[3]) * (0.25f + 0.75f * sizeAmt));
            for (int i = 0; i < nPoints; ++i)
            {
                const float t = (float) i / (float) std::max (1, nPoints - 1) * sp.tailSec;
                dest[(size_t) i] = std::exp (-3.45f * t / rt);
            }
            return;
        }
        const int idx = (useCust && cSlot < (int) roomData.size() && roomData[(size_t) cSlot].numParts > 0)
            ? cSlot
            : rClamped;
        const RoomData& rd = roomData[(size_t) idx];
        if (rd.numParts <= 0)
        {
            for (int i = 0; i < nPoints; ++i) dest[(size_t) i] = 0.0f;
            return;
        }
        const float shrink = 1.0f - sizeAmt;
        const float perPart = shrink * shrink * 0.55f * (float) (48000.0 / std::max (8000.0, sr));
        for (int i = 0; i < nPoints; ++i)
        {
            const int p0 = (int) ((int64_t) i * rd.numParts / std::max (1, nPoints));
            float e = 0.0f;
            if (p0 < rd.numParts)
            {
                const float g = std::exp (-perPart * (float) p0);
                const float* sp = rd.spec.data() + (size_t) p0 * NBINS * 4;
                for (int k = 0; k < NBINS; k += 4)
                    e += sp[(size_t) k] * sp[(size_t) k] + sp[(size_t) (NBINS + k)] * sp[(size_t) (NBINS + k)];
                e *= (g * g);
            }
            dest[(size_t) i] = std::sqrt (std::max (0.0f, e));
        }
        for (int i = nPoints - 2; i >= 0; --i)
            dest[(size_t) i] = std::max (dest[(size_t) i], dest[(size_t) (i + 1)] * 0.96f);
        float mx = 1e-12f;
        for (int i = 0; i < nPoints; ++i) mx = std::max (mx, dest[(size_t) i]);
        for (int i = 0; i < nPoints; ++i)
            dest[(size_t) i] = dest[(size_t) i] / mx;
    }

    /** wet インプレース処理。L/R は同じ長さ n。★この中で確保しない。 */
    void process (float* Lc, float* Rc, int n) noexcept
    {
        if (roomData.empty()) return;
        syncTargetRoom();
        if (curRoom >= 0 && curRoom < kNumRooms && ! roomBuilt[(size_t) curRoom])
            ensureRoomBuilt (curRoom);
        if (pendingRoom >= 0 && pendingRoom < kNumRooms && ! roomBuilt[(size_t) pendingRoom])
            ensureRoomBuilt (pendingRoom);

        const RoomData* rd = &roomData[(size_t) curRoom];
        for (int i = 0; i < n; ++i)
        {
            const float xl = Lc[i], xr = Rc[i];

            // ---- 直接FIR（先頭 B タップ）: 出力サンプルを即座に作る ----
            histL[(size_t) histPos] = xl; histL[(size_t) (histPos + B)] = xl;
            histR[(size_t) histPos] = xr; histR[(size_t) (histPos + B)] = xr;
            float yl = 0.0f, yr = 0.0f;
            {
                const float* __restrict hL = rd->headRevL.data();
                const float* __restrict hR = rd->headRevR.data();
                const float* __restrict wL = histL.data() + histPos + 1;   // 古→新 の連続 B サンプル
                const float* __restrict wR = histR.data() + histPos + 1;
                for (int t = 0; t < B; ++t)
                {
                    yl += hL[t] * wL[t];
                    yr += hR[t] * wR[t];
                }
            }
            if (++histPos >= B) histPos = 0;

            // ---- テール（前ホップまでに作り終えた B サンプルの読み出し） ----
            yl += tailL[(size_t) hopPos];
            yr += tailR[(size_t) hopPos];

            // ---- ホップの入力を貯める ----
            hopL[(size_t) (B + hopPos)] = xl;
            hopR[(size_t) (B + hopPos)] = xr;

            // ---- ★負荷ならし: p=1.. の積和をこの256サンプルに均等に散らす ----
            {
                const int M = rd->numParts;
                const int until = 1 + (int) ((int64_t) (hopPos + 1) * (M - 1) / B);
                while (spreadPart < until && spreadPart < M)
                {
                    macPartition (*rd, spreadPart);
                    ++spreadPart;
                }
            }
            ++hopPos;

            // ---- フェード（部屋切替のクリック除去） ----
            fadeGain += (fadeTarget - fadeGain) * fadeCoef;
            // ホップ境界で入れ替え、次サンプルから新しいテールを読む。
            // 途中で替えると、旧テールをフェードインしてから境界で新テールへ
            // 飛ぶため、音量フェードがあってもクリックが発生する。
            if (fadeTarget < 0.5f && fadeGain < 0.003f && hopPos >= B)
            {
                curRoom = pendingRoom; rd = &roomData[(size_t) curRoom];
                audioCurRoom.store (curRoom, std::memory_order_relaxed);
                // ★入力履歴(FDL)は消さない。履歴は部屋に依存しない「過去の声」で、
                //   新しい部屋のHと畳めば「最初からその部屋で歌っていた」音になる。
                //   （開発中ここを消していて、残響がホップ単位の階段で積み上がる
                //     クリックを作った。検査8が見つけた。実測0.58の跳び。）
                //   捨てるのは作りかけの積和だけ。次の境界で新しいHで積み直す。
                std::fill (accRe.begin(),  accRe.end(),  0.0f);
                std::fill (accIm.begin(),  accIm.end(),  0.0f);
                std::fill (accReR.begin(), accReR.end(), 0.0f);
                std::fill (accImR.begin(), accImR.end(), 0.0f);
                spreadPart = 1;
                updatePartGains (true);
                fadeTarget = 1.0f;
            }
            // モノの呼び出しでは両耳の平均を返す。後から右耳で上書きしない。
            if (Lc == Rc)
                Lc[i] = (yl + yr) * (0.5f * fadeGain);
            else
            {
                Lc[i] = yl * fadeGain;
                Rc[i] = yr * fadeGain;
            }

            // ---- ホップ境界: FFT・p=0・逆FFT だけ（重い積和は済んでいる） ----
            if (hopPos >= B)
            {
                hopPos = 0;
                finishHop (*rd);
            }
        }
    }

    int numPartsOf (int room) const
    {
        if (roomData.empty()) return 0;
        return roomData[(size_t) std::min (kNumRooms - 1, std::max (0, room))].numParts;
    }

private:
    struct RoomData
    {
        std::vector<float> headRevL, headRevR;   // 各 B（逆順＝前向き内積用）
        std::vector<float> spec;   // numParts × [L(re,im)×NBINS | R(re,im)×NBINS]
        int numParts = 0;
    };

    struct CachedRoomBank
    {
        double sr = 0.0;
        uint64_t touch = 0;
        bool ready = false;
        std::array<RoomData, kNumRooms> rooms;
    };

    static std::mutex& bankCacheMutex()
    {
        static std::mutex m;
        return m;
    }

    static std::array<CachedRoomBank, 4>& bankCacheSlots()
    {
        static std::array<CachedRoomBank, 4> slots;
        return slots;
    }

    static uint64_t& bankCacheSeq()
    {
        static uint64_t seq = 0;
        return seq;
    }

    static void copyRoomDataNoRealloc (RoomData& dst, const RoomData& src)
    {
        if (dst.headRevL.size() < src.headRevL.size()) dst.headRevL.resize (src.headRevL.size());
        if (dst.headRevR.size() < src.headRevR.size()) dst.headRevR.resize (src.headRevR.size());
        std::copy (src.headRevL.begin(), src.headRevL.end(), dst.headRevL.begin());
        std::copy (src.headRevR.begin(), src.headRevR.end(), dst.headRevR.begin());
        dst.numParts = src.numParts;
        const size_t needed = (size_t) std::max (1, src.numParts) * NBINS * 4;
        if (dst.spec.size() < needed) dst.spec.resize (needed);
        std::copy (src.spec.begin(), src.spec.begin() + (ptrdiff_t) needed, dst.spec.begin());
    }

    static bool tryCopySingleRoomFromCache (double sampleRate, int roomIdx, RoomData& dst) noexcept
    {
        std::lock_guard<std::mutex> lk (bankCacheMutex());
        auto& slots = bankCacheSlots();
        for (auto& sl : slots)
        {
            if (sl.ready && std::abs (sl.sr - sampleRate) < 0.5)
            {
                const auto& src = sl.rooms[(size_t) roomIdx];
                const size_t needed = (size_t) std::max (1, src.numParts) * NBINS * 4;
                if (dst.headRevL.size() >= src.headRevL.size()
                    && dst.headRevR.size() >= src.headRevR.size()
                    && dst.spec.size() >= needed)
                {
                    std::copy (src.headRevL.begin(), src.headRevL.end(), dst.headRevL.begin());
                    std::copy (src.headRevR.begin(), src.headRevR.end(), dst.headRevR.begin());
                    dst.numParts = src.numParts;
                    std::copy (src.spec.begin(), src.spec.begin() + (ptrdiff_t) needed, dst.spec.begin());
                    return true;
                }
            }
        }
        return false;
    }

    // プロセス内で同一サンプルレートの6部屋スペクトルを1回だけ生成してキャッシュし、
    // 2回目以降の prepare() や複数インスタンス起動では <1ms のメモリ複写だけで即完了させる。
    void populateBuiltInRoomsFromCache (double sampleRate)
    {
        {
            std::lock_guard<std::mutex> lk (bankCacheMutex());
            auto& slots = bankCacheSlots();
            for (auto& sl : slots)
            {
                if (sl.ready && std::abs (sl.sr - sampleRate) < 0.5)
                {
                    sl.touch = ++bankCacheSeq();
                    for (int r = 0; r < kNumRooms; ++r)
                    {
                        copyRoomDataNoRealloc (roomData[(size_t) r], sl.rooms[(size_t) r]);
                        roomBuilt[(size_t) r] = true;
                    }
                    return;
                }
            }
        }

        // 未キャッシュ時はここで全6部屋を1回生成してキャッシュへ登録
        std::array<RoomData, kNumRooms> built;
        for (int r = 0; r < kNumRooms; ++r)
        {
            generateRoomIr (r, sampleRate, lazyIr);
            buildRoomSlot (built[(size_t) r], lazyIr.L.data(), lazyIr.R.data(), lazyIr.lengthSamples,
                           lazyPartRe, lazyPartIm);
            copyRoomDataNoRealloc (roomData[(size_t) r], built[(size_t) r]);
            roomBuilt[(size_t) r] = true;
        }

        {
            std::lock_guard<std::mutex> lk (bankCacheMutex());
            auto& slots = bankCacheSlots();
            for (auto& sl : slots)
                if (sl.ready && std::abs (sl.sr - sampleRate) < 0.5) return;
            size_t victim = 0;
            for (size_t i = 0; i < slots.size(); ++i)
            {
                if (! slots[i].ready) { victim = i; break; }
                if (slots[i].touch < slots[victim].touch) victim = i;
            }
            slots[victim].sr    = sampleRate;
            slots[victim].touch = ++bankCacheSeq();
            slots[victim].rooms = std::move (built);
            slots[victim].ready = true;
        }
    }

    void syncTargetRoom() noexcept
    {
        const bool useCust = hasCustom.load (std::memory_order_acquire);
        const int target = useCust ? activeCustomSlot.load (std::memory_order_acquire) : lastBuiltInRoom;
        if (target != curRoom && target != pendingRoom) { pendingRoom = target; fadeTarget = 0.0f; }
        else if (target == curRoom && pendingRoom != curRoom) { pendingRoom = curRoom; fadeTarget = 1.0f; }
        audioCurRoom.store (curRoom, std::memory_order_relaxed);
        audioPendingRoom.store (pendingRoom, std::memory_order_relaxed);
    }

    void buildRoomSlot (RoomData& rd, const float* srcL, const float* srcR, int lengthSamples,
                        std::vector<float>& pRe, std::vector<float>& pIm)
    {
        if (rd.headRevL.size() < (size_t) B) rd.headRevL.assign ((size_t) B, 0.0f);
        else std::fill (rd.headRevL.begin(), rd.headRevL.begin() + B, 0.0f);
        if (rd.headRevR.size() < (size_t) B) rd.headRevR.assign ((size_t) B, 0.0f);
        else std::fill (rd.headRevR.begin(), rd.headRevR.begin() + B, 0.0f);
        for (int n = 0; n < B && n < lengthSamples; ++n)
        {
            rd.headRevL[(size_t) (B - 1 - n)] = srcL[n];
            rd.headRevR[(size_t) (B - 1 - n)] = srcR[n];
        }

        const int tail = std::max (0, lengthSamples - B);
        int np = (tail + B - 1) / B;
        if (maxParts > 0) np = std::min (np, maxParts);
        rd.numParts = np;
        const size_t needed = (size_t) std::max (1, np) * NBINS * 4;
        if (rd.spec.size() < needed)
            rd.spec.assign (needed, 0.0f);
        else
            std::fill (rd.spec.begin(), rd.spec.begin() + (ptrdiff_t) needed, 0.0f);

        if (pRe.size() < (size_t) NFFT) pRe.assign ((size_t) NFFT, 0.0f);
        if (pIm.size() < (size_t) NFFT) pIm.assign ((size_t) NFFT, 0.0f);
        for (int p = 0; p < rd.numParts; ++p)
        {
            for (int chn = 0; chn < 2; ++chn)
            {
                const float* src = (chn == 0 ? srcL : srcR);
                std::fill (pRe.begin(), pRe.begin() + NFFT, 0.0f);
                std::fill (pIm.begin(), pIm.begin() + NFFT, 0.0f);
                for (int n = 0; n < B; ++n)
                {
                    const int k = B + p * B + n;
                    if (k < lengthSamples) pRe[(size_t) n] = src[k];
                }
                fft.run (pRe.data(), pIm.data(), false);
                float* __restrict dstRe = rd.spec.data() + ((size_t) p * NBINS * 4) + (size_t) chn * NBINS * 2;
                float* __restrict dstIm = dstRe + NBINS;
                std::memcpy (dstRe, pRe.data(), sizeof (float) * (size_t) NBINS);
                std::memcpy (dstIm, pIm.data(), sizeof (float) * (size_t) NBINS);
            }
        }
    }

    // 広さツマミ → パーティションごとの目標ゲイン。
    // ★即時に飛ばすとテールが段差でプチッと言う（実測0.62）。ホップごとに
    //   25%ずつ寄せるランプにする（約50msで到達・耳には連続）。
    void updatePartGains (bool jump) noexcept
    {
        const RoomData& rd = roomData[(size_t) curRoom];
        const float shrink = 1.0f - sizeAmt;
        // パーティションの「数」でなく経過時間に対する減衰にする。
        // 96kHzでも48kHzと同じ広さ・残響時間になる。
        const float perPart = shrink * shrink * 0.55f * (float) (48000.0 / sr);
        for (int p = 0; p < rd.numParts; ++p)
            partGainTarget[(size_t) p] = std::exp (-perPart * (float) p);
        for (int p = rd.numParts; p < maxParts; ++p) partGainTarget[(size_t) p] = 0.0f;
        if (jump)
            for (int p = 0; p < maxParts; ++p) partGain[(size_t) p] = partGainTarget[(size_t) p];
        sizeDirty = false;
    }

    // 1パーティションぶんの積和（実部/虚部分離連続レイアウト＋__restrict で 4/8要素単位の SIMD ベクトル積和を実行）
    void macPartition (const RoomData& rd, int p) noexcept
    {
        const float g = partGain[(size_t) p];
        if (g < 1e-5f) return;
        int slot = fdlPos - p; while (slot < 0) slot += maxParts;
        const float* __restrict xr    = fdlRe.data()  + (size_t) slot * NBINS;
        const float* __restrict xi    = fdlIm.data()  + (size_t) slot * NBINS;
        const float* __restrict xrr   = fdlReR.data() + (size_t) slot * NBINS;
        const float* __restrict xir   = fdlImR.data() + (size_t) slot * NBINS;
        const float* __restrict hs    = rd.spec.data() + (size_t) p * NBINS * 4;
        const float* __restrict hReL  = hs;
        const float* __restrict hImL  = hs + NBINS;
        const float* __restrict hReR  = hs + NBINS * 2;
        const float* __restrict hImR  = hs + NBINS * 3;
        float* __restrict aR  = accRe.data();  float* __restrict aI  = accIm.data();
        float* __restrict aRR = accReR.data(); float* __restrict aIR = accImR.data();
        for (int k = 0; k < NBINS; ++k)
        {
            const float hre = hReL[k], him = hImL[k];
            aR[k] += g * (xr[k] * hre - xi[k] * him);
            aI[k] += g * (xr[k] * him + xi[k] * hre);
            const float hreR = hReR[k], himR = hImR[k];
            aRR[k] += g * (xrr[k] * hreR - xir[k] * himR);
            aIR[k] += g * (xrr[k] * himR + xir[k] * hreR);
        }
    }

    void finishHop (const RoomData& rd) noexcept
    {
        // 散らし残しがあれば拾う（部屋切替直後など）
        for (; spreadPart < rd.numParts; ++spreadPart) macPartition (rd, spreadPart);

        // いま完成した 2B 窓 → スペクトル → FDL（これが X_t）
        std::memcpy (workRe.data(), hopL.data(), sizeof (float) * (size_t) (2 * B));
        std::fill (workIm.begin(), workIm.end(), 0.0f);
        fft.run (workRe.data(), workIm.data(), false);
        std::memcpy (fdlRe.data() + (size_t) fdlPos * NBINS, workRe.data(), sizeof (float) * (size_t) NBINS);
        std::memcpy (fdlIm.data() + (size_t) fdlPos * NBINS, workIm.data(), sizeof (float) * (size_t) NBINS);

        std::memcpy (workRe.data(), hopR.data(), sizeof (float) * (size_t) (2 * B));
        std::fill (workIm.begin(), workIm.end(), 0.0f);
        fft.run (workRe.data(), workIm.data(), false);
        std::memcpy (fdlReR.data() + (size_t) fdlPos * NBINS, workRe.data(), sizeof (float) * (size_t) NBINS);
        std::memcpy (fdlImR.data() + (size_t) fdlPos * NBINS, workIm.data(), sizeof (float) * (size_t) NBINS);

        // p=0（X_t 自身）だけ境界で積む
        if (rd.numParts > 0)
        {
            const float g0 = partGain[0];
            const float* __restrict hs   = rd.spec.data();
            const float* __restrict hReL = hs;
            const float* __restrict hImL = hs + NBINS;
            const float* __restrict hReR = hs + NBINS * 2;
            const float* __restrict hImR = hs + NBINS * 3;
            const float* __restrict xr   = fdlRe.data()  + (size_t) fdlPos * NBINS;
            const float* __restrict xi   = fdlIm.data()  + (size_t) fdlPos * NBINS;
            const float* __restrict xrr  = fdlReR.data() + (size_t) fdlPos * NBINS;
            const float* __restrict xir  = fdlImR.data() + (size_t) fdlPos * NBINS;
            float* __restrict aR  = accRe.data();  float* __restrict aI  = accIm.data();
            float* __restrict aRR = accReR.data(); float* __restrict aIR = accImR.data();
            for (int k = 0; k < NBINS; ++k)
            {
                const float hre = hReL[k], him = hImL[k];
                aR[k]  += g0 * (xr[k] * hre - xi[k] * him);
                aI[k]  += g0 * (xr[k] * him + xi[k] * hre);
                const float hreR = hReR[k], himR = hImR[k];
                aRR[k] += g0 * (xrr[k] * hreR - xir[k] * himR);
                aIR[k] += g0 * (xrr[k] * himR + xir[k] * hreR);
            }
        }
        if (++fdlPos >= maxParts) fdlPos = 0;

        // 共役対称の復元 → 逆FFT → 後半 B サンプルが「次の B 出力」のテール
        for (int k = 1; k < NFFT / 2; ++k)
        {
            accRe [(size_t) (NFFT - k)] =  accRe [(size_t) k];
            accIm [(size_t) (NFFT - k)] = -accIm [(size_t) k];
            accReR[(size_t) (NFFT - k)] =  accReR[(size_t) k];
            accImR[(size_t) (NFFT - k)] = -accImR[(size_t) k];
        }
        fft.run (accRe.data(), accIm.data(), true);
        for (int n2 = 0; n2 < B; ++n2) tailL[(size_t) n2] = accRe[(size_t) (B + n2)];
        fft.run (accReR.data(), accImR.data(), true);
        for (int n2 = 0; n2 < B; ++n2) tailR[(size_t) n2] = accReR[(size_t) (B + n2)];

        // 次のテールへ向けて空にする
        std::fill (accRe.begin(),  accRe.end(),  0.0f);
        std::fill (accIm.begin(),  accIm.end(),  0.0f);
        std::fill (accReR.begin(), accReR.end(), 0.0f);
        std::fill (accImR.begin(), accImR.end(), 0.0f);
        spreadPart = 1;

        // hop バッファを1ブロックずらす（前半 = 直前の B サンプル）
        std::memcpy (hopL.data(), hopL.data() + B, sizeof (float) * (size_t) B);
        std::memcpy (hopR.data(), hopR.data() + B, sizeof (float) * (size_t) B);

        // 広さのランプ（ホップごとに25%ずつ寄せる）
        if (sizeDirty) updatePartGains (false);
        for (int p2 = 0; p2 < maxParts; ++p2)
            partGain[(size_t) p2] += (partGainTarget[(size_t) p2] - partGain[(size_t) p2]) * sizeSlew;
    }

    double sr = 0.0;
    Fft fft;
    std::vector<RoomData> roomData;
    std::array<bool, kNumRooms> roomBuilt {};
    GeneratedIr lazyIr;
    std::vector<float> lazyPartRe, lazyPartIm;
    int maxParts = 0;
    std::vector<float> savedCustomL, savedCustomR;

    std::vector<float> fdlRe, fdlIm, fdlReR, fdlImR;
    std::vector<float> accRe, accIm, accReR, accImR;
    std::vector<float> workRe, workIm;
    std::vector<float> partGain, partGainTarget;
    std::vector<float> histL, histR;     // 直接FIR用の履歴（2B 二重書き）
    std::vector<float> hopL, hopR;       // 2B: [直前ブロック|貯め中]
    std::vector<float> tailL, tailR;     // 次の B 出力に足すテール

    int histPos = 0, hopPos = 0, fdlPos = 0, spreadPart = 1;
    int curRoom = 0, pendingRoom = 0;
    int lastBuiltInRoom = 0;
    std::atomic<int>  activeCustomSlot { kCustomSlotA };
    std::atomic<bool> hasCustom        { false };
    std::atomic<int>  audioCurRoom     { 0 };
    std::atomic<int>  audioPendingRoom { 0 };
    float sizeAmt = 1.0f;
    bool  sizeDirty = true;
    float fadeGain = 1.0f, fadeTarget = 1.0f;
    float fadeCoef = 0.0035f;
    float sizeSlew = 0.25f;
};

} // namespace heya
