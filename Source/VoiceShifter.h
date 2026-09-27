#pragma once
// 位相を倍音ピークへそろえる短時間フーリエ変換による単音用の移調。
// 声のスペクトル包絡を保ち、子音には原音を使うことができる。
// 遅延は N-1 標本。標準窓2048では48kHz時に約42.6ms。
// 客観的な回帰検査は tools/dsp_pitchquality.cpp と dsp_vc_uv.cpp。
// 合成音での検査だけで実際の歌声の聴感を保証するものではない。

#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

namespace gz
{

class VoiceShifter
{
public:
    static int recommendedWindowOrder (double sampleRate) noexcept
    {
        // 約43ms以上を確保する。96kHzでも低音を分析できる周期数を保つ。
        return sampleRate > 96000.0 ? 13 : sampleRate > 48000.0 ? 12 : 11;
    }

    void prepare (double sampleRate, int fftOrder = 0, int overlap = 4)
    {
        if (fftOrder == 0) fftOrder = recommendedWindowOrder (sampleRate);
        // v2.12.0: 無声ガードの係数計算に使う(それまでは未使用だった)
        srHz = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        uvAtk = 1.0f - std::exp (-1.0f / (0.003f * (float) srHz));   // 声→子音 3ms
        uvRel = 1.0f - std::exp (-1.0f / (0.008f * (float) srHz));   // 子音→声 8ms
        // fftOrder=10 -> N=1024 (was 512). Twice the frequency resolution, which
        // removed most of the smearing and grain in the low register. overlap=4 (75%) kept.
        // v1.9.6: prepare で確保したサイズを最大値として覚えておく。低遅延モードは
        //         この範囲内で N を縮めるだけなので、メモリの再確保が起きない
        //         = オーディオスレッドから安全に切り替えられる。
        maxN = 1 << fftOrder; ovl = overlap;
        N = 1 << fftOrder; hop = N / overlap; bins = N / 2 + 1;
        // フレーム最古の標本を現在位置から重ねるので実遅延は N-1。
        // N-hop とすると原音との中間ミックスや子音保護でコムフィルターになる。
        dryDelay = N - 1;
        win.resize ((size_t) N);
        double wsum2 = 0.0;
        for (int n = 0; n < N; ++n)
        {
            win[(size_t) n] = 0.5f * (1.0f - std::cos (2.0f * PI * (float) n / (float) N));
            wsum2 += (double) win[(size_t) n] * win[(size_t) n];
        }
        winNorm = (float) (wsum2 / hop); if (winNorm < 1.0e-9f) winNorm = 1.0f;

        hist.assign ((size_t) N, 0.0f);
        ola .assign ((size_t) N, 0.0f);
        dryLine.assign ((size_t) N, 0.0f);
        frame.assign ((size_t) N, cf (0.0f, 0.0f));
        mag.assign ((size_t) bins, 0.0f);  lastPhase.assign ((size_t) bins, 0.0f);
        trueFreq.assign ((size_t) bins, 0.0f);
        env.assign ((size_t) bins, 0.0f);
        phaseOffset.assign ((size_t) bins, 0.0f);
        nextOffset.assign ((size_t) bins, 0.0f);
        peakOwner.assign ((size_t) bins, 0);
        nextOwner.assign ((size_t) bins, 0);
        reset();
        setParams (0.0f, 0.0f, 1.0f);
    }

    void reset()
    {
        std::fill (hist.begin(), hist.end(), 0.0f);
        std::fill (ola.begin(),  ola.end(),  0.0f);
        std::fill (dryLine.begin(), dryLine.end(), 0.0f);
        std::fill (lastPhase.begin(), lastPhase.end(), 0.0f);
        std::fill (phaseOffset.begin(), phaseOffset.end(), 0.0f);
        for (int k = 0; k < bins; ++k) peakOwner[(size_t) k] = k;
        histPos = olaHead = dryPos = samplesSinceFrame = 0;
        uvGain = 1.0f; uvTarget = 1.0f;                     // v2.12.0 無声ガード
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
    void setParams (float pitchSemi, float formantSemi, float mixAmt)
    {
        pitchFactor   = std::pow (2.0f, std::clamp (std::isfinite (pitchSemi) ? pitchSemi : 0.0f, -36.0f, 36.0f) / 12.0f);
        formantFactor = std::pow (2.0f, std::clamp (std::isfinite (formantSemi) ? formantSemi : 0.0f, -36.0f, 36.0f) / 12.0f);
        mix = std::clamp (std::isfinite (mixAmt) ? mixAmt : 0.0f, 0.0f, 1.0f);
    }

    // v1.9.6 低遅延モード: 窓を 1024 -> 512 に縮めると遅延が半分になる。
    // 周波数分解能は落ちるので、低い声ではわずかにざらつきが増える代わりに、
    // 歌いながらモニターしたときの違和感が消える。割り当ては発生しない。
    void setWindow (int fftOrder)
    {
        const int newN = 1 << fftOrder;
        if (newN == N || newN > maxN || newN < 128) return;
        N = newN; hop = N / ovl; bins = N / 2 + 1; dryDelay = N - 1;
        double wsum2 = 0.0;
        for (int n = 0; n < N; ++n)
        {
            win[(size_t) n] = 0.5f * (1.0f - std::cos (2.0f * PI * (float) n / (float) N));
            wsum2 += (double) win[(size_t) n] * win[(size_t) n];
        }
        winNorm = (float) (wsum2 / hop); if (winNorm < 1.0e-9f) winNorm = 1.0f;
        // fft() は N ではなく frame.size() を見るため、ここを合わせないと
        // 512点しか書いていないバッファに1024点FFTをかけてしまう(v1.9.6のクラッシュ原因)。
        // prepare で maxN 分を確保済みなので resize しても再確保は起きない。
        frame.resize ((size_t) N);
        reset();
    }
    int currentWindow() const { return N; }

    int latencySamples() const { return dryDelay; }

    void processBlock (float* d, int num)   { for (int i = 0; i < num; ++i) d[i] = tick (d[i]); }

private:
    using cf = std::complex<float>;
    static constexpr float PI = 3.14159265358979323846f;

    static void fft (std::vector<cf>& a, bool inv)
    {
        const int n = (int) a.size();
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[(size_t) i], a[(size_t) j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const float ang = 2.0f * PI / (float) len * (inv ? 1.0f : -1.0f);
            const cf wlen (std::cos (ang), std::sin (ang));
            for (int i = 0; i < n; i += len)
            {
                cf w (1.0f, 0.0f);
                for (int k = 0; k < len / 2; ++k)
                {
                    const cf u = a[(size_t)(i + k)], v = a[(size_t)(i + k + len / 2)] * w;
                    a[(size_t)(i + k)] = u + v; a[(size_t)(i + k + len / 2)] = u - v; w *= wlen;
                }
            }
        }
        if (inv) for (auto& x : a) x /= (float) n;
    }

    float tick (float x)
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

    void renderFrame()
    {
        // v2.12.0 無声ガード用: 窓を掛ける前の波形でゼロ交差率を数える
        int zc = 0; float prevS = 0.0f; double frameE = 0.0;
        for (int n = 0; n < N; ++n)
        {
            int idx = histPos + n; if (idx >= N) idx -= N;
            const float s = hist[(size_t) idx];
            if (uvMode != 0)
            {
                if (n > 0 && ((s > 0.0f) != (prevS > 0.0f))) ++zc;
                prevS = s; frameE += (double) s * s;
            }
            frame[(size_t) n] = cf (s * win[(size_t) n], 0.0f);
        }
        fft (frame, false);
        for (int k = 0; k < bins; ++k)
        {
            const float re = frame[(size_t) k].real(), im = frame[(size_t) k].imag();
            float m0 = std::sqrt (re * re + im * im);
            float ph = std::atan2 (im, re);
            // v2.6.0: 万一ここまでに非有限が生まれても、再帰状態(lastPhase)へは
            // 絶対に入れない。入れてしまうと二度と自力で戻れなくなる。
            if (! std::isfinite (m0)) m0 = 0.0f;
            if (! std::isfinite (ph)) ph = 0.0f;
            mag[(size_t) k] = m0;
            float dphi = ph - lastPhase[(size_t) k]; lastPhase[(size_t) k] = ph;
            dphi -= 2.0f * PI * (float) hop * (float) k / (float) N;
            dphi -= 2.0f * PI * std::round (dphi / (2.0f * PI));
            trueFreq[(size_t) k] = (float) k + dphi * (float) N / (2.0f * PI * (float) hop);
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

        // spectral envelope: zero-phase one-pole smoothing (fwd+bwd, two passes)
        for (int k = 0; k < bins; ++k) env[(size_t) k] = mag[(size_t) k];
        smooth (env, bins, 0.78f); smooth (env, bins, 0.42f);
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
        // 以前は同じ出力帯域へ入る成分が周波数を上書きし合っていたため、
        // 補正ゼロでも原音の位相が失われ、薄い声と金属的な揺れを作っていた。
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
        const float phaseStep = 2.0f * PI * (float) hop / (float) N;
        for (int k = 0; k < bins; ++k)
        {
            const int oldPeak = peakOwner[(size_t) k];
            nextOffset[(size_t) k] = std::remainder (phaseOffset[(size_t) oldPeak]
                 + phaseStep * trueFreq[(size_t) k] * (pitchFactor - 1.0f), 2.0f * PI);
            if (! std::isfinite (nextOffset[(size_t) k])) nextOffset[(size_t) k] = 0.0f;
        }
        for (int k = 0; k < bins; ++k)
        {
            phaseOffset[(size_t) k] = nextOffset[(size_t) k];
            peakOwner[(size_t) k] = nextOwner[(size_t) k];
        }
        std::fill (frame.begin(), frame.end(), cf (0.0f, 0.0f));
        auto envelopeAt = [&] (float position)
        {
            position = std::clamp (position, 0.0f, (float) (bins - 1));
            const int index = (int) position;
            const int next = std::min (index + 1, bins - 1);
            return env[(size_t) index] + (position - (float) index)
                        * (env[(size_t) next] - env[(size_t) index]);
        };
        double inPower = 0.0;
        for (int k = 0; k < bins; ++k)
        {
            const int owner = peakOwner[(size_t) k];
            // ピーク周りの窓の形は引き伸ばさず、まとまりのまま移動する。
            // 帯域番号そのものを倍にすると窓に穴が空き、オクターブで音が消える。
            const float position = (float) k + trueFreq[(size_t) owner] * (pitchFactor - 1.0f);
            const int dest = (int) std::lround (position);
            if (dest < 0 || dest >= bins) continue;
            const float formantGain = std::clamp (envelopeAt (position / formantFactor)
                                         / env[(size_t) k], 0.25f, 4.0f);
            const float magnitude = mag[(size_t) k] * formantGain;
            const float phase = lastPhase[(size_t) k] + phaseOffset[(size_t) owner];
            frame[(size_t) dest] += cf (magnitude * std::cos (phase), magnitude * std::sin (phase));
            inPower += (double) mag[(size_t) k] * mag[(size_t) k];
        }
        double outPower = 0.0;
        for (int k = 0; k < bins; ++k) outPower += std::norm (frame[(size_t) k]);
        // フォルマントを保つ処理で声量まで大きく変えない。無音の床は持ち上げない。
        const float gain = (outPower > 1.0e-18 && inPower > 1.0e-18)
                         ? (float) std::clamp (std::sqrt (inPower / outPower), 0.25, 4.0) : 1.0f;
        for (int k = 0; k < bins; ++k) frame[(size_t) k] *= gain;
        frame[0] = cf (frame[0].real(), 0.0f);
        frame[(size_t)(bins - 1)] = cf (frame[(size_t)(bins - 1)].real(), 0.0f);
        for (int k = 1; k < N - (bins - 1); ++k)
            frame[(size_t)(N - k)] = std::conj (frame[(size_t) k]);
        fft (frame, true);

        const float norm = 1.0f / winNorm; int pos = olaHead;
        for (int n = 0; n < N; ++n)
        {
            ola[(size_t) pos] += frame[(size_t) n].real() * win[(size_t) n] * norm;
            if (++pos >= N) pos = 0;
        }
    }

    static void smooth (std::vector<float>& v, int n, float a)
    {
        const float b = 1.0f - a;
        for (int k = 1; k < n; ++k)    v[(size_t) k] = a * v[(size_t)(k-1)] + b * v[(size_t) k];
        for (int k = n - 2; k >= 0; --k) v[(size_t) k] = a * v[(size_t)(k+1)] + b * v[(size_t) k];
    }

    int N = 512, hop = 128, bins = 257;
    std::vector<float> win; float winNorm = 1.0f;
    std::vector<float> hist, ola, dryLine;
    int histPos = 0, olaHead = 0, dryPos = 0, samplesSinceFrame = 0, dryDelay = 511;
    int maxN = 1024, ovl = 4;                 // v1.9.6: 低遅延モード用
    std::vector<cf> frame;
    std::vector<float> mag, lastPhase, trueFreq, env;
    std::vector<float> phaseOffset, nextOffset;
    std::vector<int> peakOwner, nextOwner;
    float pitchFactor = 1.0f, formantFactor = 1.0f, mix = 1.0f;
    // v2.12.0 無声ガード
    int    uvMode = 0;
    float  uvGain = 1.0f, uvTarget = 1.0f, uvAtk = 0.01f, uvRel = 0.005f;
    double srHz = 48000.0;
};

} // namespace gz
