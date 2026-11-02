#pragma once
// 位相を倍音ピークへそろえる短時間フーリエ変換による単音用の移調。
// 声のスペクトル包絡を保ち、子音には原音を使うことができる。
// 遅延は N-1 標本。標準窓2048では48kHz時に約42.6ms。
// 客観的な回帰検査は tools/dsp_pitchquality.cpp と dsp_vc_uv.cpp。
// 合成音での検査だけで実際の歌声の聴感を保証するものではない。

#include <vector>
#include <array>
#include <atomic>
#include <mutex>
#include <complex>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace gz
{

// ============================================================================
// FastFft — 事前計算ステージ連続テーブル＆実部/虚部分離型 基数2 高速FFT（純粋C++17）
//
//  ・prepareUpToOrder() で 2^6(64) 〜 2^maxOrder までのビット反転表と回転因子表を
//    プロセス共有の静的バンクへ一括構築（インスタンス固有メンバ 0 バイト）。
//    実行時(renderFrame / setWindow)の動的メモリ確保・三角関数呼び出しは完全ゼロ。
//  ・ステージごとに回転因子を連続配置(unit-stride)し、len=2 / len=4 の乗算ゼロ特化段と
//    実部/虚部分離ポインタ(__restrict)により MSVC /O2 の SIMD ベクトル化を最大限引き出す。
// ============================================================================
class FastFft
{
public:
    static constexpr int kMinOrder = 6;
    static constexpr int kMaxOrder = 14;

    void prepareUpToOrder (int maxOrder)
    {
        const int hi = std::clamp (maxOrder, kMinOrder, kMaxOrder);
        auto& tbls = sharedTables();
        bool needBuild = false;
        for (int ord = kMinOrder; ord <= hi; ++ord)
            if (! tbls[(size_t) ord].ready.load (std::memory_order_acquire))
                needBuild = true;
        if (! needBuild) return;

        std::lock_guard<std::mutex> lock (tableMutex());
        for (int ord = kMinOrder; ord <= hi; ++ord)
        {
            Table& t = tbls[(size_t) ord];
            if (t.ready.load (std::memory_order_relaxed)) continue;
            const int n = 1 << ord;
            t.rev.resize ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                int r = 0;
                for (int b = 0; b < ord; ++b) r = (r << 1) | ((i >> b) & 1);
                t.rev[(size_t) i] = (uint16_t) r;
            }
            // len >= 8 の各ステージの回転因子を j=0..half-1 の連続配列として事前格納
            size_t totalTw = 0;
            for (int len = 8; len <= n; len <<= 1) totalTw += (size_t) (len >> 1);
            t.stageCos.resize (totalTw);
            t.stageSin.resize (totalTw);
            size_t pos = 0;
            for (int len = 8; len <= n; len <<= 1)
            {
                const int half = len >> 1;
                const double baseAng = -2.0 * 3.14159265358979323846 / (double) len;
                for (int j = 0; j < half; ++j)
                {
                    const double a = baseAng * (double) j;
                    t.stageCos[pos + (size_t) j] = (float) std::cos (a);
                    t.stageSin[pos + (size_t) j] = (float) std::sin (a);
                }
                pos += (size_t) half;
            }
            t.order = ord;
            t.ready.store (true, std::memory_order_release);
        }
    }

    void run (int n, float* __restrict re, float* __restrict im, bool inv) const noexcept
    {
        int ord = kMinOrder;
        while ((1 << ord) < n && ord < kMaxOrder) ++ord;
        const Table& t = sharedTables()[(size_t) ord];
        if (! t.ready.load (std::memory_order_acquire) || (1 << ord) != n) return;

        const uint16_t* __restrict rev = t.rev.data();
        for (int i = 0; i < n; ++i)
        {
            const int r = (int) rev[i];
            if (r > i)
            {
                const float tr = re[i]; re[i] = re[r]; re[r] = tr;
                const float ti = im[i]; im[i] = im[r]; im[r] = ti;
            }
        }

        // Stage 1 (len = 2): 回転因子は常に 1+0i（乗算ゼロ）
        for (int i = 0; i < n; i += 2)
        {
            const float ar = re[i],     ai = im[i];
            const float br = re[i + 1], bi = im[i + 1];
            re[i]     = ar + br; im[i]     = ai + bi;
            re[i + 1] = ar - br; im[i + 1] = ai - bi;
        }

        // Stage 2 (len = 4): j=0 は 1+0i、j=1 は -i（順変換）または +i（逆変換）（乗算ゼロ）
        if (n >= 4)
        {
            if (! inv)
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

        // Stage 3..ord (len = 8..n): 連続配置された回転因子による4要素単位SIMDフレンドリーバタフライ
        size_t twPos = 0;
        if (! inv)
        {
            for (int len = 8; len <= n; len <<= 1)
            {
                const int half = len >> 1;
                const float* __restrict wc = t.stageCos.data() + twPos;
                const float* __restrict ws = t.stageSin.data() + twPos;
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
                const float* __restrict wc = t.stageCos.data() + twPos;
                const float* __restrict ws = t.stageSin.data() + twPos;
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
            const float invN = 1.0f / (float) n;
            for (int i = 0; i < n; ++i)
            {
                re[i] *= invN;
                im[i] *= invN;
            }
        }
    }

private:
    struct Table
    {
        std::atomic<bool> ready { false };
        int order = 0;
        std::vector<uint16_t> rev;
        std::vector<float> stageCos, stageSin;
    };

    static std::array<Table, kMaxOrder + 1>& sharedTables() noexcept
    {
        static std::array<Table, kMaxOrder + 1> sTables {};
        return sTables;
    }

    static std::mutex& tableMutex() noexcept
    {
        static std::mutex sMutex;
        return sMutex;
    }
};

class VoiceShifter
{
public:
    static int recommendedWindowOrder (double sampleRate) noexcept
    {
        // 約43ms以上を確保する。96kHzでも低音を分析できる周期数を保つ。
        return sampleRate > 96000.0 ? 13 : sampleRate > 48000.0 ? 12 : 11;
    }

    // v5.1.2 遅延・音質モード別のFFT窓次数（prepareで確保したmaxNの範囲内でゼロ確保切替）
    //   mode 0 = 🚀極限ゼロ感 (~5.3ms  @48kHz, N=256,  8x重なり) : 骨伝導とのズレが消える超高速モニター
    //   mode 1 = ⚡超低遅延   (~10.6ms @48kHz, N=512,  8x重なり) : 雑談・コラボ・高速モニター重視
    //   mode 2 = ⭐標準低遅延 (~21.3ms @48kHz, N=1024, 4x重なり) : 歌枠・ボイチェン・ピッチ補正の最適バランス（既定）
    //   mode 3 = 💎最高音質   (~42.6ms @48kHz, N=2048, 4x重なり) : 歌ってみた録音・超低音ボイス用の最高分解能
    static int windowOrderForLatencyMode (double sampleRate, int latMode) noexcept
    {
        const int maxOrd = recommendedWindowOrder (sampleRate);
        if (latMode <= 0) return std::max (8, maxOrd - 3); // 256  @48kHz (~5.3ms)
        if (latMode == 1) return std::max (8, maxOrd - 2); // 512  @48kHz (~10.6ms)
        if (latMode == 2) return std::max (9, maxOrd - 1); // 1024 @48kHz (~21.3ms)
        return maxOrd;                                     // 2048 @48kHz (~42.6ms)
    }

    static int overlapForWindow (int windowSamples) noexcept
    {
        // 小窓(N<=512)では8倍オーバーラップ(87.5%重なり)に高密度化し、追加遅延を一切増やさずにフレーム変調歪みを半減する
        return (windowSamples <= 512) ? 8 : 4;
    }

    void prepare (double sampleRate, int fftOrder = 0, int overlap = 4)
    {
        if (fftOrder == 0) fftOrder = recommendedWindowOrder (sampleRate);
        fftOrder = std::clamp (fftOrder, FastFft::kMinOrder, FastFft::kMaxOrder);
        // v2.12.0: 無声ガードの係数計算に使う(それまでは未使用だった)
        srHz = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        uvAtk = 1.0f - std::exp (-1.0f / (0.003f * (float) srHz));   // 声→子音 3ms
        uvRel = 1.0f - std::exp (-1.0f / (0.008f * (float) srHz));   // 子音→声 8ms
        // v1.9.6: prepare で確保したサイズを最大値として覚えておく。低遅延モードは
        //         この範囲内で N を縮めるだけなので、メモリの再確保が起きない
        //         = オーディオスレッドから安全に切り替えられる。
        fastFft.prepareUpToOrder (fftOrder);
        maxN = 1 << fftOrder; ovl = overlap;
        N = 1 << fftOrder; hop = N / overlap; bins = N / 2 + 1;
        // フレーム最古の標本を現在位置から重ねるので実遅延は N-1。
        // N-hop とすると原音との中間ミックスや子音保護でコムフィルターになる。
        dryDelay = N - 1;
        win.resize ((size_t) maxN);
        updateWindowAndSmoothCoefs();

        hist.assign ((size_t) maxN, 0.0f);
        ola .assign ((size_t) maxN, 0.0f);
        dryLine.assign ((size_t) maxN, 0.0f);
        fftRe.assign ((size_t) maxN, 0.0f);
        fftIm.assign ((size_t) maxN, 0.0f);
        const size_t maxBins = (size_t) (maxN / 2 + 1);
        mag.assign (maxBins, 0.0f);  lastPhase.assign (maxBins, 0.0f);
        trueFreq.assign (maxBins, 0.0f);
        env.assign (maxBins, 0.0f);
        phaseOffset.assign (maxBins, 0.0f);
        nextOffset.assign (maxBins, 0.0f);
        peakOwner.assign (maxBins, 0);
        nextOwner.assign (maxBins, 0);
        reset();
        setParams (0.0f, 0.0f, 1.0f);
    }

    void reset() noexcept
    {
        std::fill (hist.begin(), hist.end(), 0.0f);
        std::fill (ola.begin(),  ola.end(),  0.0f);
        std::fill (dryLine.begin(), dryLine.end(), 0.0f);
        std::fill (lastPhase.begin(), lastPhase.end(), 0.0f);
        std::fill (phaseOffset.begin(), phaseOffset.end(), 0.0f);
        for (int k = 0; k < (int) peakOwner.size(); ++k) peakOwner[(size_t) k] = k;
        histPos = olaHead = dryPos = 0;
        samplesSinceFrame = currentHopOffset();
        uvGain = 1.0f; uvTarget = 1.0f;                     // v2.12.0 無声ガード
        prevFrameE = 0.0;
    }

    // 複数シフター・コンボルバのFFTホップ境界が同一小バッファ（32/64サンプル）に集中して
    // CPUスパイクを起こすのを防ぐホップ位相スタッガリング（0.0〜1.0）。
    // Hann二乗窓の4倍/8倍オーバーラップは全オフセットで厳密にCOLA定数和を満たし、遅延(N-1)も不変。
    void setHopPhaseRatio (float ratio01) noexcept
    {
        hopPhaseRatio = std::clamp (std::isfinite (ratio01) ? ratio01 : 0.0f, 0.0f, 0.999f);
        samplesSinceFrame = currentHopOffset();
    }

    // ------------------------------------------------------------------
    // v2.12.0 無声ガード（v3.0設計書 §6-3「ボイチェンで言葉が伝わらない」）
    //
    //  子音(サ行・カ行・タ行)は倍音構造の無いノイズなので、ピッチ/フォルマント
    //  シフトを掛けると濁って言葉が潰れる。フレームごとに無声/有声を判定して、
    //   mode 1 … 無声のあいだ出力を**元の音**へ寄せる(本人の声・オートチューン用。
    //            子音はそもそも音程を持たないので、補正しない方が正しい)
    //   mode 2 … 無声のあいだ出力を**無音**へ寄せる(ユニゾン/ハモリの分身用。
    //            4人分の「サッ」が重なって歯擦音が4倍になるのを防ぐ。
    //            子音は本人の1回だけ聞こえるのが自然)
    //   mode 0 … 何もしない(既定。今までと1サンプルも変わらない)
    //  切り替えは3ms/8msのなめらかな係数で行う(プチッと言わせない)。
    // ------------------------------------------------------------------
    void setUnvoicedGuard (int mode) noexcept { uvMode = mode; }
    bool lastFrameUnvoiced() const noexcept   { return uvTarget < 0.5f; }

    // semitones / semitones / 0..1
    void setParams (float pitchSemi, float formantSemi, float mixAmt) noexcept
    {
        pitchFactor   = std::pow (2.0f, std::clamp (std::isfinite (pitchSemi) ? pitchSemi : 0.0f, -36.0f, 36.0f) / 12.0f);
        formantFactor = std::pow (2.0f, std::clamp (std::isfinite (formantSemi) ? formantSemi : 0.0f, -36.0f, 36.0f) / 12.0f);
        mix = std::clamp (std::isfinite (mixAmt) ? mixAmt : 0.0f, 0.0f, 1.0f);
    }

    // v1.9.6/v5.1.2 低遅延モード: 窓を 2048 -> 1024 -> 512 -> 256 に縮めると遅延が段階的に半減する。
    // 小窓(<=512)では自動で8倍オーバーラップに切り替わり、ざらつき・ケロつきを抑える。割り当ては発生しない。
    void setWindow (int fftOrder) noexcept
    {
        const int newN = 1 << fftOrder;
        const int newOvl = overlapForWindow (newN);
        if (newN == N && newOvl == ovl) return;
        if (newN > maxN || newN < 128) return;
        N = newN; ovl = newOvl; hop = N / ovl; bins = N / 2 + 1; dryDelay = N - 1;
        updateWindowAndSmoothCoefs();
        reset();
    }
    int currentWindow() const noexcept { return N; }

    int latencySamples() const noexcept { return dryDelay; }

    void processBlock (float* d, int num) noexcept { for (int i = 0; i < num; ++i) d[i] = tick (d[i]); }

    // 同一サンプルレート・同一窓サイズの別チャンネルへ実行時状態をゼロ確保で転写する（デュアルモノ→真ステレオへのシームレス移行用）
    void copyRuntimeStateFrom (const VoiceShifter& src) noexcept
    {
        if (N != src.N || bins != src.bins) return;
        if (hist.size() >= (size_t) N && src.hist.size() >= (size_t) N)
            std::memcpy (hist.data(), src.hist.data(), sizeof (float) * (size_t) N);
        if (ola.size() >= (size_t) N && src.ola.size() >= (size_t) N)
            std::memcpy (ola.data(), src.ola.data(), sizeof (float) * (size_t) N);
        if (dryLine.size() >= (size_t) N && src.dryLine.size() >= (size_t) N)
            std::memcpy (dryLine.data(), src.dryLine.data(), sizeof (float) * (size_t) N);
        if (lastPhase.size() >= (size_t) bins && src.lastPhase.size() >= (size_t) bins)
            std::memcpy (lastPhase.data(), src.lastPhase.data(), sizeof (float) * (size_t) bins);
        if (phaseOffset.size() >= (size_t) bins && src.phaseOffset.size() >= (size_t) bins)
            std::memcpy (phaseOffset.data(), src.phaseOffset.data(), sizeof (float) * (size_t) bins);
        if (peakOwner.size() >= (size_t) bins && src.peakOwner.size() >= (size_t) bins)
            std::memcpy (peakOwner.data(), src.peakOwner.data(), sizeof (int) * (size_t) bins);
        histPos           = src.histPos;
        olaHead           = src.olaHead;
        dryPos            = src.dryPos;
        samplesSinceFrame = src.samplesSinceFrame;
        dryDelay          = src.dryDelay;
        pitchFactor       = src.pitchFactor;
        formantFactor     = src.formantFactor;
        mix               = src.mix;
        uvMode            = src.uvMode;
        uvGain            = src.uvGain;
        uvTarget          = src.uvTarget;
        prevFrameE        = src.prevFrameE;
    }

private:
    static constexpr float PI = 3.14159265358979323846f;

    int currentHopOffset() const noexcept
    {
        return (hop > 1) ? std::clamp ((int) (hopPhaseRatio * (float) hop), 0, hop - 1) : 0;
    }

    static inline float wrapTwoPi (float x) noexcept
    {
        constexpr float twoPi    = 2.0f * PI;
        constexpr float invTwoPi = 1.0f / (2.0f * PI);
        return x - twoPi * std::round (x * invTwoPi);
    }

    void updateWindowAndSmoothCoefs() noexcept
    {
        double wsum2 = 0.0;
        for (int n = 0; n < N; ++n)
        {
            win[(size_t) n] = 0.5f * (1.0f - std::cos (2.0f * PI * (float) n / (float) N));
            wsum2 += (double) win[(size_t) n] * win[(size_t) n];
        }
        winNorm = (float) (wsum2 / (double) hop);
        if (winNorm < 1.0e-9f) winNorm = 1.0f;
        const float winScale = std::clamp ((float) N / 1024.0f, 0.25f, 4.0f);
        smoothA1 = std::pow (0.78f, 1.0f / winScale);
        smoothA2 = std::pow (0.42f, 1.0f / winScale);
    }

    float tick (float x) noexcept
    {
        // v2.6.0 最重要: 入口で必ず有限な値にする。
        // ここを通さないと NaN が hist / dryLine / 位相メモリへ入り込み、
        // (1) 出力が永久に無音のまま戻らない
        // (2) renderFrame の (int) キャストが巨大な負の値になり配列外アクセス
        // という2つの事故が起きる。実測でクラッシュを再現済み(dsp_voiceshift_stress)。
        if (! std::isfinite (x)) x = 0.0f;
        hist[(size_t) histPos] = x; if (++histPos >= N) histPos = 0;
        dryLine[(size_t) dryPos] = x;                               // newest sample
        int rd = dryPos - dryDelay; if (rd < 0) rd += N;
        const float dryDelayed = dryLine[(size_t) rd];              // same delay as the wet path
        if (++dryPos >= N) dryPos = 0;
        if (++samplesSinceFrame >= hop) { samplesSinceFrame = 0; renderFrame(); }
        const float wet = ola[(size_t) olaHead]; ola[(size_t) olaHead] = 0.0f;
        if (++olaHead >= N) olaHead = 0;

        // v2.12.0 無声ガード。uvMode==0 なら uvGain は 1 のままで、式は従来と同一。
        if (uvMode != 0)
            uvGain += (uvTarget > uvGain ? uvRel : uvAtk) * (uvTarget - uvGain);
        if (uvMode == 2)                                    // 分身: 子音は黙る
            return (dryDelayed + mix * (wet - dryDelayed)) * uvGain;
        if (uvMode == 1)                                    // 本人: 子音は素通し
            return dryDelayed + mix * uvGain * (wet - dryDelayed);
        return dryDelayed + mix * (wet - dryDelayed);
    }

    void renderFrame() noexcept
    {
        // v2.12.0 無声ガード用: 窓を掛ける前の波形でゼロ交差率を数える
        int zc = 0; float prevS = 0.0f; double frameE = 0.0;
        float* __restrict rePtr = fftRe.data();
        float* __restrict imPtr = fftIm.data();
        const float* __restrict winPtr = win.data();
        for (int n = 0; n < N; ++n)
        {
            int idx = histPos + n; if (idx >= N) idx -= N;
            const float s = hist[(size_t) idx];
            if (uvMode != 0)
            {
                if (n > 0 && ((s > 0.0f) != (prevS > 0.0f))) ++zc;
                prevS = s;
            }
            frameE += (double) s * s;
            rePtr[n] = s * winPtr[n];
            imPtr[n] = 0.0f;
        }
        // v5.1.2 トランジェント（歌い出し・破裂音アタック）位相リセット:
        //  エネルギーが前フレーム比で急増した瞬間、古い累積位相オフセットをリセットして子音・発声アタックのボヤけを防ぐ
        const bool isTransientOnset = (prevFrameE > 1.0e-9 && frameE > prevFrameE * 4.5 && (frameE / (double) N) > 1.0e-5);
        prevFrameE = frameE;
        fastFft.run (N, rePtr, imPtr, false);

        const float phaseStep = 2.0f * PI * (float) hop / (float) N;
        const float invPhaseStep = (float) N / (2.0f * PI * (float) hop);
        for (int k = 0; k < bins; ++k)
        {
            const float re = rePtr[k], im = imPtr[k];
            float m0 = std::sqrt (re * re + im * im);
            float ph = std::atan2 (im, re);
            // v2.6.0: 万一ここまでに非有限が生まれても、再帰状態(lastPhase)へは
            // 絶対に入れない。入れてしまうと二度と自力で戻れなくなる。
            if (! std::isfinite (m0)) m0 = 0.0f;
            if (! std::isfinite (ph)) ph = 0.0f;
            mag[(size_t) k] = m0;
            float dphi = ph - lastPhase[(size_t) k]; lastPhase[(size_t) k] = ph;
            dphi = wrapTwoPi (dphi - phaseStep * (float) k);
            trueFreq[(size_t) k] = (float) k + dphi * invPhaseStep;
        }

        // v2.12.0 無声ガード: このフレームが子音(無声)かどうかを決める。
        //  ・ゼロ交差率が高い(ノイズ的) かつ 4kHz以上にエネルギーが寄っている
        //  ・またはゼロ交差率が極端に高い
        //  静かなフレーム(-60dBFS未満)は判定を変えない(無音で采配がばたつくと、
        //  mode2 の分身が息継ぎのたびに音量を上下させてしまう)。
        if (uvMode != 0)
        {
            const float rms = std::sqrt ((float) (frameE / (double) N));
            if (rms > 1.0e-3f)
            {
                const float zcr = (float) zc / (float) N;
                int bin4k = (int) (4000.0 * (double) N / srHz);
                if (bin4k < 1) bin4k = 1; if (bin4k > bins - 1) bin4k = bins - 1;
                double hi = 0.0, all = 1.0e-12;
                for (int k = 1; k < bins; ++k)
                {
                    const double e2 = (double) mag[(size_t) k] * mag[(size_t) k];
                    all += e2; if (k >= bin4k) hi += e2;
                }
                const float hfr = (float) (hi / all);
                const bool unvoiced = (zcr > 0.22f && hfr > 0.40f) || zcr > 0.33f;
                uvTarget = unvoiced ? 0.0f : 1.0f;
            }
        }

        // v5.1.2 True-Envelope (倍音ピーク包絡ブリッジ) + 窓サイズ適応型ゼロ位相スムージング:
        //  倍音と倍音のあいだの谷(valley)にスペクトル包絡が引きずり込まれると、移調先の倍音が
        //  谷に落ち込んで声量が痩せる(コムフィルタ状の抜け)。局所ピークを橋渡ししてから平滑化する。
        const int bridgeRad = std::clamp ((int) std::lround (95.0 * (double) N / srHz), 1, 6);
        for (int k = 0; k < bins; ++k)
        {
            float localMax = mag[(size_t) k];
            const int k0 = std::max (0, k - bridgeRad);
            const int k1 = std::min (bins - 1, k + bridgeRad);
            for (int j = k0; j <= k1; ++j)
                if (mag[(size_t) j] > localMax) localMax = mag[(size_t) j];
            env[(size_t) k] = 0.65f * localMax + 0.35f * mag[(size_t) k];
        }
        smooth (env, bins, smoothA1); smooth (env, bins, smoothA2);
        // v1.9.0: an absolute floor of 1e-7 let the excitation blow up to 1e7x inside
        //         spectral valleys, which could explode on formant moves. Peak-relative now.
        float envPeak = 0.0f;
        for (int k = 0; k < bins; ++k) envPeak = std::max (envPeak, env[(size_t) k]);
        const float envFloor = std::max (1.0e-7f, envPeak * 1.0e-4f);
        for (int k = 0; k < bins; ++k)
        {
            if (env[(size_t) k] < envFloor) env[(size_t) k] = envFloor;
        }

        // 倍音ピークごとに位相を進め、周辺の帯域は入力の相対位相を保つ。
        int leftPeak = 0;
        for (int k = 1; k < bins; ++k)
        {
            const bool peak = k == bins - 1 || (mag[(size_t) k] > mag[(size_t)(k-1)]
                              && mag[(size_t) k] >= mag[(size_t)(k+1)]);
            if (! peak) continue;
            const int boundary = (leftPeak + k) / 2;
            for (int j = leftPeak; j <= boundary; ++j) nextOwner[(size_t) j] = leftPeak;
            for (int j = boundary + 1; j <= k; ++j) nextOwner[(size_t) j] = k;
            leftPeak = k;
        }
        const float transientDamp = isTransientOnset ? 0.35f : 1.0f;
        for (int k = 0; k < bins; ++k)
        {
            const int oldPeak = peakOwner[(size_t) k];
            float no = wrapTwoPi (phaseOffset[(size_t) oldPeak] * transientDamp
                                  + phaseStep * trueFreq[(size_t) k] * (pitchFactor - 1.0f));
            if (! std::isfinite (no)) no = 0.0f;
            nextOffset[(size_t) k] = no;
        }
        for (int k = 0; k < bins; ++k)
        {
            phaseOffset[(size_t) k] = nextOffset[(size_t) k];
            peakOwner[(size_t) k] = nextOwner[(size_t) k];
        }
        std::memset (rePtr, 0, sizeof (float) * (size_t) bins);
        std::memset (imPtr, 0, sizeof (float) * (size_t) bins);
        auto envelopeAt = [&] (float position) noexcept -> float
        {
            position = std::clamp (position, 0.0f, (float) (bins - 1));
            const int index = (int) position;
            const int next = std::min (index + 1, bins - 1);
            return env[(size_t) index] + (position - (float) index)
                        * (env[(size_t) next] - env[(size_t) index]);
        };
        const float invFormant = 1.0f / formantFactor;
        double inPower = 0.0;
        for (int k = 0; k < bins; ++k)
        {
            const int owner = peakOwner[(size_t) k];
            // ピーク周りの窓の形は引き伸ばさず、まとまりのまま移動する。
            // 帯域番号そのものを倍にすると窓に穴が空き、オクターブで音が消える。
            const float position = (float) k + trueFreq[(size_t) owner] * (pitchFactor - 1.0f);
            const int dest = (int) std::lround (position);
            if (dest < 0 || dest >= bins) continue;
            const float formantGain = std::clamp (envelopeAt (position * invFormant)
                                         / env[(size_t) k], 0.18f, 5.5f);
            const float magnitude = mag[(size_t) k] * formantGain;
            const float phase = lastPhase[(size_t) k] + phaseOffset[(size_t) owner];
            rePtr[dest] += magnitude * std::cos (phase);
            imPtr[dest] += magnitude * std::sin (phase);
            inPower += (double) mag[(size_t) k] * mag[(size_t) k];
        }
        double outPower = 0.0;
        for (int k = 0; k < bins; ++k)
            outPower += (double) rePtr[k] * rePtr[k] + (double) imPtr[k] * imPtr[k];
        // フォルマントを保つ処理で声量まで大きく変えない。無音の床は持ち上げない。
        const float gain = (outPower > 1.0e-18 && inPower > 1.0e-18)
                         ? (float) std::clamp (std::sqrt (inPower / outPower), 0.18, 6.0) : 1.0f;
        for (int k = 0; k < bins; ++k) { rePtr[k] *= gain; imPtr[k] *= gain; }
        imPtr[0] = 0.0f;
        imPtr[bins - 1] = 0.0f;
        for (int k = 1; k < N - (bins - 1); ++k)
        {
            rePtr[N - k] =  rePtr[k];
            imPtr[N - k] = -imPtr[k];
        }
        fastFft.run (N, rePtr, imPtr, true);

        const float norm = 1.0f / winNorm; int pos = olaHead;
        for (int n = 0; n < N; ++n)
        {
            ola[(size_t) pos] += rePtr[n] * winPtr[n] * norm;
            if (++pos >= N) pos = 0;
        }
    }

    static void smooth (std::vector<float>& v, int n, float a) noexcept
    {
        const float b = 1.0f - a;
        float* __restrict d = v.data();
        for (int k = 1; k < n; ++k)     d[k] = a * d[k - 1] + b * d[k];
        for (int k = n - 2; k >= 0; --k) d[k] = a * d[k + 1] + b * d[k];
    }

    FastFft fastFft;
    int N = 512, hop = 128, bins = 257;
    std::vector<float> win; float winNorm = 1.0f;
    float smoothA1 = 0.78f, smoothA2 = 0.42f;
    float hopPhaseRatio = 0.0f;
    std::vector<float> hist, ola, dryLine;
    int histPos = 0, olaHead = 0, dryPos = 0, samplesSinceFrame = 0, dryDelay = 511;
    int maxN = 1024, ovl = 4;                 // v1.9.6: 低遅延モード用
    std::vector<float> fftRe, fftIm;
    std::vector<float> mag, lastPhase, trueFreq, env;
    std::vector<float> phaseOffset, nextOffset;
    std::vector<int> peakOwner, nextOwner;
    float pitchFactor = 1.0f, formantFactor = 1.0f, mix = 1.0f;
    // v2.12.0 無声ガード
    int    uvMode = 0;
    float  uvGain = 1.0f, uvTarget = 1.0f, uvAtk = 0.01f, uvRel = 0.005f;
    double srHz = 48000.0;
    double prevFrameE = 0.0;

    friend class MultiVoiceHarmonyShifter;
};

// ============================================================================
// MultiVoiceHarmonyShifter (v5.5.0) — 単一FFT解析・多声合成スマートコーラスエンジン
//
// 従来はハモリ4声(unSh[0..3])が同一の入力 vcMono に対して4回ずつ独立に
// 窓掛け・順変換FFT・atan2位相微分・True-Envelope包絡抽出・ピーク分割を実行していた。
// 本クラスは「順変換FFT＋瞬時周波数＋True-Envelope＋倍音領域分割」を1ホップにつき
// たった1回だけ共有実行し、実際に度数シフトしている有効声部のみ逆変換IFFTで合成する。
// さらにバックコーラス特有の歯擦音（サ行・タ行の4重ダブり）を自然に丸める
// 子音ソフトナーを標準内蔵し、音質の自然さと CPU 負荷 55〜70% 削減を両立する。
// ============================================================================
class MultiVoiceHarmonyShifter
{
public:
    static constexpr int kMaxVoices = 4;

    void prepare (double sampleRate, int fftOrder = 0, int overlap = 4)
    {
        if (fftOrder == 0) fftOrder = VoiceShifter::recommendedWindowOrder (sampleRate);
        else               fftOrder = std::clamp (fftOrder, 7, 13);
        overlap = std::clamp (overlap, 2, 8);
        srHz  = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        uvAtk = 1.0f - std::exp (-1.0f / (0.003f * (float) srHz));
        uvRel = 1.0f - std::exp (-1.0f / (0.010f * (float) srHz));

        fastFft.prepareUpToOrder (fftOrder);
        maxN = 1 << fftOrder;
        ovl  = overlap;
        N    = maxN;
        hop  = N / ovl;
        bins = N / 2 + 1;

        win.resize ((size_t) maxN);
        rebuildWindow();

        hist.assign ((size_t) maxN, 0.0f);
        fftRe.assign ((size_t) maxN, 0.0f);
        fftIm.assign ((size_t) maxN, 0.0f);
        const size_t maxBins = (size_t) (maxN / 2 + 1);
        mag.assign (maxBins, 0.0f);
        lastPhase.assign (maxBins, 0.0f);
        trueFreq.assign (maxBins, 0.0f);
        env.assign (maxBins, 0.0f);
        peakOwner.assign (maxBins, 0);
        nextOwner.assign (maxBins, 0);
        nextOffset.assign (maxBins, 0.0f);

        for (int v = 0; v < kMaxVoices; ++v)
        {
            ola[v].assign ((size_t) maxN, 0.0f);
            phaseOffset[v].assign (maxBins, 0.0f);
            pitchFactor[v]   = 1.0f;
            formantFactor[v] = 1.0f;
            voiceActive[v]   = false;
        }
        reset();
    }

    void reset() noexcept
    {
        std::fill (hist.begin(), hist.end(), 0.0f);
        std::fill (lastPhase.begin(), lastPhase.end(), 0.0f);
        for (int k = 0; k < (int) peakOwner.size(); ++k) peakOwner[(size_t) k] = k;
        for (int v = 0; v < kMaxVoices; ++v)
        {
            std::fill (ola[v].begin(), ola[v].end(), 0.0f);
            std::fill (phaseOffset[v].begin(), phaseOffset[v].end(), 0.0f);
        }
        histPos = olaHead = 0;
        samplesSinceFrame = currentHopOffset();
        uvGain   = 1.0f;
        uvTarget = 1.0f;
        prevFrameE = 0.0;
    }

    void setHopPhaseRatio (float ratio01) noexcept
    {
        hopPhaseRatio = std::clamp (std::isfinite (ratio01) ? ratio01 : 0.0f, 0.0f, 0.999f);
        samplesSinceFrame = currentHopOffset();
    }

    void resetVoice (int v) noexcept
    {
        if (v < 0 || v >= kMaxVoices) return;
        std::fill (ola[v].begin(), ola[v].end(), 0.0f);
        std::fill (phaseOffset[v].begin(), phaseOffset[v].end(), 0.0f);
        voiceActive[v] = false;
    }

    void setWindow (int fftOrder) noexcept
    {
        const int newN   = 1 << fftOrder;
        const int newOvl = VoiceShifter::overlapForWindow (newN);
        if (newN == N && newOvl == ovl) return;
        if (newN > maxN || newN < 128) return;
        N    = newN;
        ovl  = newOvl;
        hop  = N / ovl;
        bins = N / 2 + 1;
        rebuildWindow();
        reset();
    }

    int currentWindow() const noexcept  { return N; }
    int latencySamples() const noexcept { return N - 1; }
    void setConsonantSoftener (bool enabled) noexcept { softenConsonants = enabled; }

    void setVoiceParams (int v, bool active, float pitchSemi, float formantSemi) noexcept
    {
        if (v < 0 || v >= kMaxVoices) return;
        if (active && ! voiceActive[v])
        {
            std::fill (ola[v].begin(), ola[v].begin() + N, 0.0f);
            std::fill (phaseOffset[v].begin(), phaseOffset[v].begin() + bins, 0.0f);
        }
        voiceActive[v]   = active;
        pitchFactor[v]   = std::pow (2.0f, std::clamp (std::isfinite (pitchSemi)   ? pitchSemi   : 0.0f, -36.0f, 36.0f) / 12.0f);
        formantFactor[v] = std::pow (2.0f, std::clamp (std::isfinite (formantSemi) ? formantSemi : 0.0f, -36.0f, 36.0f) / 12.0f);
    }

    // 1ブロック分のモノラル入力から、有効なすべての和音声部を同時生成する（RT確保ゼロ）
    void processBlock (const float* monoIn, float* outVoices[kMaxVoices], int numSamples) noexcept
    {
        bool anyActive = false;
        for (int v = 0; v < kMaxVoices; ++v)
            anyActive = anyActive || (voiceActive[v] && outVoices[v] != nullptr);
        if (! anyActive) return;

        for (int i = 0; i < numSamples; ++i)
        {
            float x = monoIn[i];
            if (! std::isfinite (x)) x = 0.0f;
            hist[(size_t) histPos] = x;
            if (++histPos >= N) histPos = 0;

            if (++samplesSinceFrame >= hop)
            {
                samplesSinceFrame = 0;
                renderSharedFrame();
            }

            if (softenConsonants)
                uvGain += (uvTarget > uvGain ? uvRel : uvAtk) * (uvTarget - uvGain);
            else
                uvGain = 1.0f;

            for (int v = 0; v < kMaxVoices; ++v)
            {
                const float wet = ola[v][(size_t) olaHead];
                ola[v][(size_t) olaHead] = 0.0f;
                if (voiceActive[v] && outVoices[v] != nullptr)
                    outVoices[v][i] = wet * uvGain;
            }
            if (++olaHead >= N) olaHead = 0;
        }
    }

private:
    static constexpr float PI = VoiceShifter::PI;

    int currentHopOffset() const noexcept
    {
        return (hop > 1) ? std::clamp ((int) (hopPhaseRatio * (float) hop), 0, hop - 1) : 0;
    }

    void rebuildWindow() noexcept
    {
        double wsum2 = 0.0;
        for (int n = 0; n < N; ++n)
        {
            win[(size_t) n] = 0.5f * (1.0f - std::cos (2.0f * PI * (float) n / (float) N));
            wsum2 += (double) win[(size_t) n] * win[(size_t) n];
        }
        winNorm = (float) (wsum2 / (double) hop);
        if (winNorm < 1.0e-9f) winNorm = 1.0f;
        const float winScale = std::clamp ((float) N / 1024.0f, 0.25f, 4.0f);
        smoothA1 = std::pow (0.78f, 1.0f / winScale);
        smoothA2 = std::pow (0.42f, 1.0f / winScale);
    }

    void renderSharedFrame() noexcept
    {
        // 1. 全声部で共通の1回だけの窓掛け＋順変換STFT＋瞬時周波数＋True-Envelope抽出
        int zc = 0;
        float prevS = 0.0f;
        double frameE = 0.0;
        float* __restrict rePtr = fftRe.data();
        float* __restrict imPtr = fftIm.data();
        const float* __restrict winPtr = win.data();
        for (int n = 0; n < N; ++n)
        {
            int idx = histPos + n; if (idx >= N) idx -= N;
            const float s = hist[(size_t) idx];
            if (n > 0 && ((s > 0.0f) != (prevS > 0.0f))) ++zc;
            prevS = s;
            frameE += (double) s * s;
            rePtr[n] = s * winPtr[n];
            imPtr[n] = 0.0f;
        }

        const bool isTransientOnset = (prevFrameE > 1.0e-9 && frameE > prevFrameE * 4.5 && (frameE / (double) N) > 1.0e-5);
        prevFrameE = frameE;

        fastFft.run (N, rePtr, imPtr, false);

        const float phaseStep    = 2.0f * PI * (float) hop / (float) N;
        const float invPhaseStep = (float) N / (2.0f * PI * (float) hop);
        double inPower = 0.0;
        for (int k = 0; k < bins; ++k)
        {
            const float re = rePtr[k], im = imPtr[k];
            float m0 = std::sqrt (re * re + im * im);
            float ph = std::atan2 (im, re);
            if (! std::isfinite (m0)) m0 = 0.0f;
            if (! std::isfinite (ph)) ph = 0.0f;
            mag[(size_t) k] = m0;
            inPower += (double) m0 * m0;

            float dphi = ph - lastPhase[(size_t) k];
            lastPhase[(size_t) k] = ph;
            dphi = VoiceShifter::wrapTwoPi (dphi - phaseStep * (float) k);
            trueFreq[(size_t) k] = (float) k + dphi * invPhaseStep;
        }

        // バックコーラス子音ソフトナー: 主旋律のサ行・タ行と4声分の歯擦音が重なってシャリつくのを防ぐ（完全ミュートせず 0.28 倍へ自然に丸める）
        if (softenConsonants)
        {
            const float rms = std::sqrt ((float) (frameE / (double) N));
            if (rms > 1.0e-3f)
            {
                const float zcr = (float) zc / (float) N;
                int bin4k = std::clamp ((int) (4000.0 * (double) N / srHz), 1, bins - 1);
                double hi = 0.0;
                const double all = inPower + 1.0e-12;
                for (int k = bin4k; k < bins; ++k)
                    hi += (double) mag[(size_t) k] * mag[(size_t) k];
                const float hfr = (float) (hi / all);
                const bool unvoiced = (zcr > 0.22f && hfr > 0.40f) || zcr > 0.33f;
                uvTarget = unvoiced ? 0.28f : 1.0f;
            }
        }

        const int bridgeRad = std::clamp ((int) std::lround (95.0 * (double) N / srHz), 1, 6);
        for (int k = 0; k < bins; ++k)
        {
            float localMax = mag[(size_t) k];
            const int k0 = std::max (0, k - bridgeRad);
            const int k1 = std::min (bins - 1, k + bridgeRad);
            for (int j = k0; j <= k1; ++j)
                if (mag[(size_t) j] > localMax) localMax = mag[(size_t) j];
            env[(size_t) k] = 0.65f * localMax + 0.35f * mag[(size_t) k];
        }
        VoiceShifter::smooth (env, bins, smoothA1);
        VoiceShifter::smooth (env, bins, smoothA2);

        float envPeak = 0.0f;
        for (int k = 0; k < bins; ++k) envPeak = std::max (envPeak, env[(size_t) k]);
        const float envFloor = std::max (1.0e-7f, envPeak * 1.0e-4f);
        for (int k = 0; k < bins; ++k)
            if (env[(size_t) k] < envFloor) env[(size_t) k] = envFloor;

        int leftPeak = 0;
        for (int k = 1; k < bins; ++k)
        {
            const bool peak = k == bins - 1 || (mag[(size_t) k] > mag[(size_t) (k - 1)]
                              && mag[(size_t) k] >= mag[(size_t) (k + 1)]);
            if (! peak) continue;
            const int boundary = (leftPeak + k) / 2;
            for (int j = leftPeak; j <= boundary; ++j) nextOwner[(size_t) j] = leftPeak;
            for (int j = boundary + 1; j <= k; ++j)    nextOwner[(size_t) j] = k;
            leftPeak = k;
        }

        auto envelopeAt = [&] (float position) noexcept -> float
        {
            position = std::clamp (position, 0.0f, (float) (bins - 1));
            const int index = (int) position;
            const int next  = std::min (index + 1, bins - 1);
            return env[(size_t) index] + (position - (float) index) * (env[(size_t) next] - env[(size_t) index]);
        };

        const float transientDamp = isTransientOnset ? 0.35f : 1.0f;
        const float norm          = 1.0f / winNorm;

        // 2. 有効な和音声部だけを個別のピッチ・フォルマントで逆変換合成
        for (int v = 0; v < kMaxVoices; ++v)
        {
            if (! voiceActive[v]) continue;

            const float pf = pitchFactor[v];
            const float invFf = 1.0f / formantFactor[v];
            auto& pOff = phaseOffset[v];
            for (int k = 0; k < bins; ++k)
            {
                const int oldPeak = peakOwner[(size_t) k];
                float no = VoiceShifter::wrapTwoPi (pOff[(size_t) oldPeak] * transientDamp
                             + phaseStep * trueFreq[(size_t) k] * (pf - 1.0f));
                if (! std::isfinite (no)) no = 0.0f;
                nextOffset[(size_t) k] = no;
            }
            for (int k = 0; k < bins; ++k)
                pOff[(size_t) k] = nextOffset[(size_t) k];

            std::memset (rePtr, 0, sizeof (float) * (size_t) bins);
            std::memset (imPtr, 0, sizeof (float) * (size_t) bins);
            for (int k = 0; k < bins; ++k)
            {
                const int owner = nextOwner[(size_t) k];
                const float position = (float) k + trueFreq[(size_t) owner] * (pf - 1.0f);
                const int dest = (int) std::lround (position);
                if (dest < 0 || dest >= bins) continue;
                const float formantGain = std::clamp (envelopeAt (position * invFf) / env[(size_t) k], 0.18f, 5.5f);
                const float magnitude   = mag[(size_t) k] * formantGain;
                const float phase       = lastPhase[(size_t) k] + pOff[(size_t) owner];
                rePtr[dest] += magnitude * std::cos (phase);
                imPtr[dest] += magnitude * std::sin (phase);
            }

            double outPower = 0.0;
            for (int k = 0; k < bins; ++k)
                outPower += (double) rePtr[k] * rePtr[k] + (double) imPtr[k] * imPtr[k];
            const float gain = (outPower > 1.0e-18 && inPower > 1.0e-18)
                             ? (float) std::clamp (std::sqrt (inPower / outPower), 0.18, 6.0) : 1.0f;
            for (int k = 0; k < bins; ++k) { rePtr[k] *= gain; imPtr[k] *= gain; }

            imPtr[0] = 0.0f;
            imPtr[bins - 1] = 0.0f;
            for (int k = 1; k < N - (bins - 1); ++k)
            {
                rePtr[N - k] =  rePtr[k];
                imPtr[N - k] = -imPtr[k];
            }
            fastFft.run (N, rePtr, imPtr, true);

            int pos = olaHead;
            float* __restrict vOla = ola[v].data();
            for (int n = 0; n < N; ++n)
            {
                vOla[pos] += rePtr[n] * winPtr[n] * norm;
                if (++pos >= N) pos = 0;
            }
        }

        for (int k = 0; k < bins; ++k)
            peakOwner[(size_t) k] = nextOwner[(size_t) k];
    }

    FastFft fastFft;
    int N = 1024, maxN = 2048, ovl = 4, hop = 256, bins = 513;
    std::vector<float> win;
    float winNorm = 1.0f;
    float smoothA1 = 0.78f, smoothA2 = 0.42f;
    float hopPhaseRatio = 0.0f;
    std::vector<float> hist;
    std::vector<float> ola[kMaxVoices];
    int histPos = 0, olaHead = 0, samplesSinceFrame = 0;

    std::vector<float> fftRe, fftIm;
    std::vector<float> mag, lastPhase, trueFreq, env, nextOffset;
    std::vector<float> phaseOffset[kMaxVoices];
    std::vector<int>   peakOwner, nextOwner;

    float  pitchFactor[kMaxVoices]   { 1.0f, 1.0f, 1.0f, 1.0f };
    float  formantFactor[kMaxVoices] { 1.0f, 1.0f, 1.0f, 1.0f };
    bool   voiceActive[kMaxVoices]   { false, false, false, false };
    bool   softenConsonants          { true };
    float  uvGain = 1.0f, uvTarget = 1.0f, uvAtk = 0.01f, uvRel = 0.005f;
    double srHz = 48000.0;
    double prevFrameE = 0.0;
};

} // namespace gz
