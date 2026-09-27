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
        cosTab.resize ((size_t) size / 2);
        sinTab.resize ((size_t) size / 2);
        for (int i = 0; i < size / 2; ++i)
        {
            const double a = -2.0 * 3.14159265358979323846 * i / size;
            cosTab[(size_t) i] = (float) std::cos (a);
            sinTab[(size_t) i] = (float) std::sin (a);
        }
        rev.resize ((size_t) size);
        for (int i = 0; i < size; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b) r = (r << 1) | ((i >> b) & 1);
            rev[(size_t) i] = r;
        }
    }

    // その場で複素FFT。re/im は size 要素。inverse は共役トリックではなく符号反転表を使わず、
    // 呼び出し側が im の符号を反転して呼ぶのではなく inverse フラグで回す（読みやすさ優先）。
    void run (float* re, float* im, bool inverse) const noexcept
    {
        for (int i = 0; i < size; ++i)
        {
            const int r = rev[(size_t) i];
            if (r > i) { std::swap (re[i], re[r]); std::swap (im[i], im[r]); }
        }
        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len >> 1, step = size / len;
            for (int i = 0; i < size; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const int ti = j * step;
                    const float wr = cosTab[(size_t) ti];
                    const float wi = inverse ? -sinTab[(size_t) ti] : sinTab[(size_t) ti];
                    const int a = i + j, b = a + half;
                    const float xr = re[b] * wr - im[b] * wi;
                    const float xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi;
                    re[a] += xr;        im[a] += xi;
                }
        }
        if (inverse)
        {
            const float g = 1.0f / (float) size;
            for (int i = 0; i < size; ++i) { re[i] *= g; im[i] *= g; }
        }
    }

    int order = 0, size = 0;
private:
    std::vector<float> cosTab, sinTab;
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
};

inline void generateRoomIr (int roomIndex, double sr, GeneratedIr& out)
{
    const RoomSpec& spec = rooms()[(size_t) roomIndex];
    const int N = std::max (256, (int) std::lround (spec.tailSec * sr));
    out.L.assign ((size_t) N, 0.0f);
    out.R.assign ((size_t) N, 0.0f);
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
                // 距離減衰 1/r と、壁で跳ねるたびの吸収
                const float wall = std::pow (1.0f - spec.absorb, (float) bounces);
                const float g = spec.erGain * wall / std::max (0.6f, dist);
                // 高次ほど高域が死ぬ→サンプル1本でなく2本に割ってなまらせる
                std::vector<float>& ch = (ear == 0 ? out.L : out.R);
                const float soft = std::min (0.85f, 0.12f * (float) bounces);
                ch[(size_t) idx] += g * (1.0f - soft);
                if (idx + 1 < N) ch[(size_t) idx + 1] += g * soft;
            }
        }
    }

    // ---- 2) 帯域別に減衰するノイズテール ------------------------------------
    {
        static const double bandHz[6] = { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0 };
        const int tailStart = isPlate ? (int) (0.004 * sr)
                                      : (int) (std::min (0.030f, spec.tailSec * 0.25f) * sr);
        for (int band = 0; band < 6; ++band)
        {
            GenBiquad bpL, bpR;
            const double q = 0.9;
            bpL.bandpass (bandHz[band], q, sr);
            bpR.bandpass (bandHz[band], q, sr);
            Rng rngL ((uint32_t) (roomIndex * 7919 + band * 131 + 17));
            Rng rngR ((uint32_t) (roomIndex * 7919 + band * 131 + 9173));

            // RT60 → 1サンプルごとの減衰
            const double rt = std::max (0.05f, spec.rt60[band]);
            const double decay = std::pow (10.0, -3.0 / (rt * sr));
            double envL = 1.0, envR = 1.0;

            // 密度の立ち上がり（部屋は徐々に密になる。プレートは最初から密）
            const double densRamp = isPlate ? 0.002 : 0.020;

            for (int n = tailStart; n < N; ++n)
            {
                const double t = (n - tailStart) / sr;
                const double ramp = 1.0 - std::exp (-t / densRamp);
                float wL = bpL.tick (rngL.gauss());
                float wR0 = bpR.tick (rngR.gauss());
                // 低音側は左右を混ぜて自然な相関を残す
                const float mixCorr = spec.lowCorr * (band < 2 ? 1.0f : (band == 2 ? 0.5f : 0.15f));
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
    //   判定は願望でなく |H(f)| の実測（FFT）。
    {
        int ord = 1; while ((1 << ord) < N * 2) ++ord;
        const int NF = 1 << ord;
        std::vector<float> re ((size_t) NF, 0.0f), im ((size_t) NF, 0.0f);
        Fft f; f.prepare (ord);
        for (int n = 0; n < N; ++n) re[(size_t) n] = out.L[(size_t) n];
        f.run (re.data(), im.data(), false);
        double hMax = 0.0;
        const int kLo = (int) (80.0 * NF / sr), kHi = (int) (12000.0 * NF / sr);
        for (int k = std::max (1, kLo); k < std::min (NF / 2, kHi); ++k)
            hMax = std::max (hMax, std::sqrt ((double) re[(size_t) k] * re[(size_t) k]
                                            + (double) im[(size_t) k] * im[(size_t) k]));
        const double cap = 3.5;   // ≈ +10.9dB
        if (hMax > cap)
        {
            const float g2 = (float) (cap / hMax);
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

    // 全部屋ぶんのスペクトルを prepare で作り置き。実行時の切替は瞬間・確保ゼロ。
    void prepare (double sampleRate, int /*maxBlock*/)
    {
        if (std::abs (sampleRate - sr) < 0.5 && ! roomData.empty())
        {   reset(); return; }            // 同じレートなら作り直さない

        sr = sampleRate;
        fadeCoef = (float) (1.0 - std::pow (1.0 - 0.0035, 48000.0 / sr));
        sizeSlew = (float) (1.0 - std::pow (0.75, 48000.0 / sr));
        fft.prepare (FFT_ORDER);

        roomData.clear();
        roomData.resize (kNumRooms);
        maxParts = 0;

        GeneratedIr ir;
        std::vector<float> re ((size_t) NFFT), im ((size_t) NFFT);
        for (int room = 0; room < kNumRooms; ++room)
        {
            generateRoomIr (room, sr, ir);
            RoomData& rd = roomData[(size_t) room];

            // 先頭 B サンプル = 直接FIR（ゼロ遅延の要）。
            // ★逆順で持つ: y = Σ h[t]·x[n-t] を「前向き×前向き」の内積にして
            //   コンパイラの自動ベクトル化(SIMD)に乗せる。
            rd.headRevL.assign ((size_t) B, 0.0f);
            rd.headRevR.assign ((size_t) B, 0.0f);
            for (int n = 0; n < B && n < ir.lengthSamples; ++n)
            {
                rd.headRevL[(size_t) (B - 1 - n)] = ir.L[(size_t) n];
                rd.headRevR[(size_t) (B - 1 - n)] = ir.R[(size_t) n];
            }

            // 残り = B ごとのパーティションを周波数領域へ
            const int tail = std::max (0, ir.lengthSamples - B);
            rd.numParts = (tail + B - 1) / B;
            rd.spec.assign ((size_t) std::max (1, rd.numParts) * NBINS * 4, 0.0f);
            for (int p = 0; p < rd.numParts; ++p)
            {
                for (int chn = 0; chn < 2; ++chn)
                {
                    const std::vector<float>& src = (chn == 0 ? ir.L : ir.R);
                    std::fill (re.begin(), re.end(), 0.0f);
                    std::fill (im.begin(), im.end(), 0.0f);
                    for (int n = 0; n < B; ++n)
                    {
                        const int k = B + p * B + n;
                        if (k < ir.lengthSamples) re[(size_t) n] = src[(size_t) k];
                    }
                    fft.run (re.data(), im.data(), false);
                    float* dst = rd.spec.data() + ((size_t) p * NBINS * 4) + (size_t) chn * NBINS * 2;
                    for (int k2 = 0; k2 < NBINS; ++k2)
                    {
                        dst[(size_t) (2 * k2)]     = re[(size_t) k2];
                        dst[(size_t) (2 * k2) + 1] = im[(size_t) k2];
                    }
                }
            }
            maxParts = std::max (maxParts, rd.numParts);
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
        curRoom = 0; pendingRoom = 0; sizeAmt = 1.0f; sizeDirty = true;
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

        // ★入れ替え途中だったら、ここで**やりきる**こと。
        //   reset() は fadeTarget を 1 に戻すので、pendingRoom != curRoom のまま
        //   放っておくと「入れ替え待ちなのにフェードは戻ってしまった」状態で固まり、
        //   同じ部屋を setRoom し直しても（room == pendingRoom なので）どちらの
        //   分岐にも入らず、**その部屋には一生切り替わらない**。
        //   prepareToPlay が切替の最中に呼ばれると実際にこれが起きる
        //   （ホストの再prepare・レート変更・再生開始）。
        //   履歴を全部ゼロにする場所なので、ここで即座に替えてもプチッと言わない。
        curRoom = pendingRoom;
        fadeGain = 1.0f; fadeTarget = 1.0f;
        if (! partGain.empty()) updatePartGains (true);
    }

    /** 部屋を選ぶ(0..5)。鳴っている最中でも安全（10msフェードで入れ替え）。 */
    void setRoom (int room) noexcept
    {
        room = std::min (kNumRooms - 1, std::max (0, room));
        if (room != curRoom && room != pendingRoom) { pendingRoom = room; fadeTarget = 0.0f; }
        else if (room == curRoom && pendingRoom != curRoom) { pendingRoom = curRoom; fadeTarget = 1.0f; }
    }

    /** 部屋の広さ 0..1（revsize をそのまま渡す）。テールの長さを実時間で変える。 */
    void setSize (float amt01) noexcept
    {
        const float a = std::min (1.0f, std::max (0.0f, amt01));
        if (std::abs (a - sizeAmt) > 0.002f) { sizeAmt = a; sizeDirty = true; }
    }

    int  currentRoom() const noexcept { return curRoom; }

    /** 部屋の減衰カーブ（画面用）。0..1 の縦値を nPoints ぶん詰める。 */
    void getDecayCurve (int room, float* dest, int nPoints) const
    {
        room = std::min (kNumRooms - 1, std::max (0, room));
        if (roomData.empty() || nPoints <= 0) return;
        const RoomData& rd = roomData[(size_t) room];
        for (int i = 0; i < nPoints; ++i)
        {
            const int p0 = (int) ((int64_t) i * rd.numParts / std::max (1, nPoints));
            float e = 0.0f;
            if (p0 < rd.numParts)
            {
                const float* sp = rd.spec.data() + (size_t) p0 * NBINS * 4;
                for (int k = 0; k < NBINS * 2; k += 16) e += sp[(size_t) k] * sp[(size_t) k];
            }
            dest[(size_t) i] = e;
        }
        float mx = 1e-12f;
        for (int i = 0; i < nPoints; ++i) mx = std::max (mx, dest[(size_t) i]);
        for (int i = 0; i < nPoints; ++i)
            dest[(size_t) i] = std::sqrt (dest[(size_t) i] / mx);
    }

    /** wet インプレース処理。L/R は同じ長さ n。★この中で確保しない。 */
    void process (float* Lc, float* Rc, int n) noexcept
    {
        if (roomData.empty()) return;

        const RoomData* rd = &roomData[(size_t) curRoom];
        for (int i = 0; i < n; ++i)
        {
            const float xl = Lc[i], xr = Rc[i];

            // ---- 直接FIR（先頭 B タップ）: 出力サンプルを即座に作る ----
            histL[(size_t) histPos] = xl; histL[(size_t) (histPos + B)] = xl;
            histR[(size_t) histPos] = xr; histR[(size_t) (histPos + B)] = xr;
            float yl = 0.0f, yr = 0.0f;
            {
                const float* hL = rd->headRevL.data();
                const float* hR = rd->headRevR.data();
                const float* wL = histL.data() + histPos + 1;   // 古→新 の連続 B サンプル
                const float* wR = histR.data() + histPos + 1;
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
        std::vector<float> spec;   // numParts × [L(re,im)×NFFT | R(re,im)×NFFT]
        int numParts = 0;
    };

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

    // 1パーティションぶんの積和（上半分だけ・対称は境界で復元）
    void macPartition (const RoomData& rd, int p) noexcept
    {
        const float g = partGain[(size_t) p];
        if (g < 1e-5f) return;
        // 次の境界で押す X_{t+1} に対する p 番目 → スロット (fdlPos - p + 1)…
        // fdlPos は「次に書く場所」。partition p は fdlPos - p を読む
        // （p=1 が直前に書いたスロット）。
        int slot = fdlPos - p; while (slot < 0) slot += maxParts;
        const float* xr  = fdlRe.data()  + (size_t) slot * NBINS;
        const float* xi  = fdlIm.data()  + (size_t) slot * NBINS;
        const float* xrr = fdlReR.data() + (size_t) slot * NBINS;
        const float* xir = fdlImR.data() + (size_t) slot * NBINS;
        const float* hs  = rd.spec.data() + (size_t) p * NBINS * 4;
        const float* hL  = hs;
        const float* hR  = hs + (size_t) NBINS * 2;
        float* aR  = accRe.data();  float* aI  = accIm.data();
        float* aRR = accReR.data(); float* aIR = accImR.data();
        for (int k = 0; k <= NFFT / 2; ++k)
        {
            const float hre = hL[(size_t) (2 * k)], him = hL[(size_t) (2 * k) + 1];
            aR[k] += g * (xr[k] * hre - xi[k] * him);
            aI[k] += g * (xr[k] * him + xi[k] * hre);
            const float hreR = hR[(size_t) (2 * k)], himR = hR[(size_t) (2 * k) + 1];
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
            const float* hs = rd.spec.data();
            const float* hL = hs;
            const float* hR = hs + (size_t) NBINS * 2;
            const float* xr  = fdlRe.data()  + (size_t) fdlPos * NBINS;
            const float* xi  = fdlIm.data()  + (size_t) fdlPos * NBINS;
            const float* xrr = fdlReR.data() + (size_t) fdlPos * NBINS;
            const float* xir = fdlImR.data() + (size_t) fdlPos * NBINS;
            for (int k = 0; k <= NFFT / 2; ++k)
            {
                const float hre = hL[(size_t) (2 * k)], him = hL[(size_t) (2 * k) + 1];
                accRe[(size_t) k] += g0 * (xr[k] * hre - xi[k] * him);
                accIm[(size_t) k] += g0 * (xr[k] * him + xi[k] * hre);
                const float hreR = hR[(size_t) (2 * k)], himR = hR[(size_t) (2 * k) + 1];
                accReR[(size_t) k] += g0 * (xrr[k] * hreR - xir[k] * himR);
                accImR[(size_t) k] += g0 * (xrr[k] * himR + xir[k] * hreR);
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
    int maxParts = 0;

    std::vector<float> fdlRe, fdlIm, fdlReR, fdlImR;
    std::vector<float> accRe, accIm, accReR, accImR;
    std::vector<float> workRe, workIm;
    std::vector<float> partGain, partGainTarget;
    std::vector<float> histL, histR;     // 直接FIR用の履歴（2B 二重書き）
    std::vector<float> hopL, hopR;       // 2B: [直前ブロック|貯め中]
    std::vector<float> tailL, tailR;     // 次の B 出力に足すテール

    int histPos = 0, hopPos = 0, fdlPos = 0, spreadPart = 1;
    int curRoom = 0, pendingRoom = 0;
    float sizeAmt = 1.0f;
    bool  sizeDirty = true;
    float fadeGain = 1.0f, fadeTarget = 1.0f;
    float fadeCoef = 0.0035f;
    float sizeSlew = 0.25f;
};

} // namespace heya
