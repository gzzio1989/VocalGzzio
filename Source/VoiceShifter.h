#pragma once
// VoiceShifter - STFT phase-vocoder pitch shifting with source-filter
// (formant) preservation. Validated in voice_shifter_bench.cpp:
//   * pitch +/-12 and +/-7 semitone accuracy on tones (440.1 / 110.4 / 329.7 Hz from 220)
//   * formant-only shift leaves pitch untouched (anti-chipmunk verified via
//     spectral centroid: pitch+12 keeps centroid x1.18 vs x2.07 for formant+12)
//   * silence / full-scale noise stay finite; no allocations in process
//   * worst-case load ~1.2-1.4 % of one core @44.1/48k (generic FFT, N=512/hop=128)
// Latency: exactly N samples (512 = approx 11.6 ms @44.1k). Mono; run one per channel.

#include <vector>
#include <complex>
#include <cmath>

namespace gz
{

class VoiceShifter
{
public:
    void prepare (double /*sampleRate*/, int fftOrder = 10, int overlap = 4)
    {
        // fftOrder=10 -> N=1024 (was 512). Twice the frequency resolution, which
        // removed most of the smearing and grain in the low register. overlap=4 (75%) kept.
        // v1.9.6: prepare で確保したサイズを最大値として覚えておく。低遅延モードは
        //         この範囲内で N を縮めるだけなので、メモリの再確保が起きない
        //         = オーディオスレッドから安全に切り替えられる。
        maxN = 1 << fftOrder; ovl = overlap;
        N = 1 << fftOrder; hop = N / overlap; bins = N / 2 + 1;
        // v1.9.0: measured wet latency is N-hop, but the dry line was N-1, so any
        //         mix between the two combed by ~5.8 ms. Align them here.
        dryDelay = N - hop;
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
        sumPhase.assign ((size_t) bins, 0.0f); trueFreq.assign ((size_t) bins, 0.0f);
        env.assign ((size_t) bins, 0.0f);  exc.assign ((size_t) bins, 0.0f);
        synMag.assign ((size_t) bins, 0.0f); synFreq.assign ((size_t) bins, 0.0f);
        finalMag.assign ((size_t) bins, 0.0f);
        reset();
        setParams (0.0f, 0.0f, 1.0f);
    }

    void reset()
    {
        std::fill (hist.begin(), hist.end(), 0.0f);
        std::fill (ola.begin(),  ola.end(),  0.0f);
        std::fill (dryLine.begin(), dryLine.end(), 0.0f);
        std::fill (lastPhase.begin(), lastPhase.end(), 0.0f);
        std::fill (sumPhase.begin(),  sumPhase.end(),  0.0f);
        histPos = olaHead = dryPos = samplesSinceFrame = 0;
    }

    // semitones / semitones / 0..1
    void setParams (float pitchSemi, float formantSemi, float mixAmt)
    {
        pitchFactor   = std::pow (2.0f, pitchSemi   / 12.0f);
        formantFactor = std::pow (2.0f, formantSemi / 12.0f);
        mix = mixAmt;
    }

    // v1.9.6 低遅延モード: 窓を 1024 -> 512 に縮めると遅延が半分になる。
    // 周波数分解能は落ちるので、低い声ではわずかにざらつきが増える代わりに、
    // 歌いながらモニターしたときの違和感が消える。割り当ては発生しない。
    void setWindow (int fftOrder)
    {
        const int newN = 1 << fftOrder;
        if (newN == N || newN > maxN || newN < 128) return;
        N = newN; hop = N / ovl; bins = N / 2 + 1; dryDelay = N - hop;
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

    int latencySamples() const { return N - hop; }   // v1.9.0: matches the measured delay

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
        return dryDelayed + mix * (wet - dryDelayed);
    }

    void renderFrame()
    {
        for (int n = 0; n < N; ++n)
        {
            int idx = histPos + n; if (idx >= N) idx -= N;
            frame[(size_t) n] = cf (hist[(size_t) idx] * win[(size_t) n], 0.0f);
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

        // spectral envelope: zero-phase one-pole smoothing (fwd+bwd, two passes)
        for (int k = 0; k < bins; ++k) env[(size_t) k] = mag[(size_t) k];
        smooth (env, 0.78f); smooth (env, 0.42f);
        // v1.9.0: an absolute floor of 1e-7 let the excitation blow up to 1e7x inside
        //         spectral valleys, which could explode on formant moves. Peak-relative now.
        float envPeak = 0.0f;
        for (int k = 0; k < bins; ++k) envPeak = std::max (envPeak, env[(size_t) k]);
        const float envFloor = std::max (1.0e-7f, envPeak * 1.0e-4f);
        for (int k = 0; k < bins; ++k)
        {
            if (env[(size_t) k] < envFloor) env[(size_t) k] = envFloor;
            exc[(size_t) k] = mag[(size_t) k] / env[(size_t) k];
        }

        // ---- Pitch shift: scale each analysis bin's true frequency by pitchFactor and
        //      spread its energy linearly across the two nearest synthesis bins.
        //      The old code just rounded k*pitchFactor to an integer bin, so several
        //      bins collapsed onto one, interfered, and rang metallic. Keeping the true
        //      frequency and recording it per bin preserves phase coherence.
        std::fill (synMag.begin(),  synMag.end(),  0.0f);
        std::fill (synFreq.begin(), synFreq.end(), 0.0f);
        for (int k = 0; k < bins; ++k)
        {
            const float shifted = trueFreq[(size_t) k] * pitchFactor;  // target frequency [bins]
            // v2.6.0: 「範囲内なら通す」と書く。以前の「範囲外なら弾く」形だと
            // NaN は全ての比較が false になるため素通りし、(int) NaN が
            // INT_MIN になって配列外を書きに行っていた(実測クラッシュ)。
            if (! (shifted >= 0.0f && shifted < (float) (bins - 1))) continue;
            const int   j0 = (int) shifted;
            const float fr = shifted - (float) j0;
            const float e  = exc[(size_t) k];
            // Spread energy over the two neighbouring bins; record the frequency on both.
            synMag[(size_t) j0]       += e * (1.0f - fr);
            synFreq[(size_t) j0]       = shifted;
            if (j0 + 1 < bins)
            {
                synMag[(size_t)(j0 + 1)] += e * fr;
                synFreq[(size_t)(j0 + 1)] = shifted;
            }
        }
        // v1.9.3: bound how much the formant filter may change any single bin.
        // On very pure material (whistle, flute, a sine-like synth) the envelope is
        // one narrow bump, so shifting the excitation out from under it dropped the
        // level by ~30 dB - the sound effectively vanished. Real voices have broad
        // formants and never hit this, but the limit costs nothing and stops it.
        const float kFormantGainMax = 4.0f;      // +/-12 dB
        for (int k = 0; k < bins; ++k)
        {
            const float src = (float) k / formantFactor; float e;
            if (src <= 0.0f)                 e = env[0];
            else if (src >= (float)(bins-1)) e = env[(size_t)(bins - 1)];
            else { const int i0 = (int) src; const float fr = src - (float) i0;
                   e = env[(size_t) i0] + (env[(size_t)(i0 + 1)] - env[(size_t) i0]) * fr; }
            const float here  = env[(size_t) k];
            const float ratio = e / (here > 0.0f ? here : 1.0e-12f);
            const float lim   = (ratio > kFormantGainMax) ? kFormantGainMax
                              : (ratio < 1.0f / kFormantGainMax) ? 1.0f / kFormantGainMax : ratio;
            finalMag[(size_t) k] = synMag[(size_t) k] * here * lim;
        }
        {   // v1.9.0 safety valve: never emit more than 4x the input magnitude,
            double inSum = 0.0, outSum = 0.0;
            for (int k = 0; k < bins; ++k) { inSum += mag[(size_t) k]; outSum += finalMag[(size_t) k]; }
            // 上限: 何かが破綻しても入力の4倍を超える音は出さない
            // 下限: 純音のようにフォルマント構造が無い素材だと、励起が包絡の
            //       山から外れて -30dB まで落ちることがある。フレーム全体で
            //       -12dB を下回らないようにして「音が消える」のを防ぐ。
            double g = 1.0;
            if      (outSum > 4.00 * inSum + 1.0e-9) g = (4.00 * inSum) / outSum;
            else if (outSum > 1.0e-9 && outSum < 0.25 * inSum) g = (0.25 * inSum) / outSum;
            if (g != 1.0)
                for (int k = 0; k < bins; ++k) finalMag[(size_t) k] *= (float) g;
        }
        for (int k = 0; k < bins; ++k)
        {
            sumPhase[(size_t) k] += 2.0f * PI * (float) hop * synFreq[(size_t) k] / (float) N;
            // Wrap the accumulated phase into [-2pi,2pi]. Left alone it overflows the
            // float mantissa and the sound turns grainy after a few minutes.
            sumPhase[(size_t) k] = std::fmod (sumPhase[(size_t) k], 2.0f * PI);
            // v2.6.0: 位相の積算はフレームをまたいで残る唯一の値。ここが一度でも
            // 壊れると以降ずっと壊れたままなので、壊れていたら0へ戻す(自己回復)。
            if (! std::isfinite (sumPhase[(size_t) k])) sumPhase[(size_t) k] = 0.0f;
            float fm = finalMag[(size_t) k];
            if (! std::isfinite (fm)) fm = 0.0f;
            frame[(size_t) k] = cf (fm * std::cos (sumPhase[(size_t) k]),
                                    fm * std::sin (sumPhase[(size_t) k]));
        }
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

    static void smooth (std::vector<float>& v, float a)
    {
        const int n = (int) v.size(); const float b = 1.0f - a;
        for (int k = 1; k < n; ++k)    v[(size_t) k] = a * v[(size_t)(k-1)] + b * v[(size_t) k];
        for (int k = n - 2; k >= 0; --k) v[(size_t) k] = a * v[(size_t)(k+1)] + b * v[(size_t) k];
    }

    int N = 512, hop = 128, bins = 257;
    std::vector<float> win; float winNorm = 1.0f;
    std::vector<float> hist, ola, dryLine;
    int histPos = 0, olaHead = 0, dryPos = 0, samplesSinceFrame = 0, dryDelay = 384;
    int maxN = 1024, ovl = 4;                 // v1.9.6: 低遅延モード用
    std::vector<cf> frame;
    std::vector<float> mag, lastPhase, sumPhase, trueFreq, env, exc, synMag, synFreq, finalMag;
    float pitchFactor = 1.0f, formantFactor = 1.0f, mix = 1.0f;
};

} // namespace gz
