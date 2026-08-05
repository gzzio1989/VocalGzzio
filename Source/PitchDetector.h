#pragma once
// PitchDetector - real-time monophonic F0 estimation (YIN / CMNDF).
//
// Reference: A. de Cheveigne and H. Kawahara, "YIN, a fundamental frequency
// estimator for speech and music", J. Acoust. Soc. Am. 111(4), 1917-1930, 2002.
// The cumulative mean normalized difference function (CMNDF) removes the
// zero-lag bias of plain autocorrelation and, with an absolute threshold
// (~0.10-0.15), strongly suppresses the octave / sub-harmonic errors that
// plague autocorrelation-only trackers. Parabolic interpolation on the chosen
// lag gives sub-sample (sub-cent) period accuracy.
//
// Cost control: the signal is anti-alias low-passed and DECIMATED to ~22 kHz
// before analysis (vocals rarely exceed ~1 kHz F0), so the O(tauMax^2)
// difference function stays cheap and roughly sample-rate independent. Analysis
// also runs on a hop (~4.5 ms) rather than every block, so tiny host buffers
// never trigger a large per-callback burst.
//
// Audio-thread safe: every buffer is sized in prepare(); process()/analyze()
// allocate nothing. Feed mono samples with process(); it returns the current
// F0 estimate in Hz (0 when unvoiced / not yet confident).

#include <vector>
#include <cmath>
#include <algorithm>

namespace gz
{

class PitchDetector
{
public:
    // fMin/fMax bound the search (vocal range). The integration window covers
    // one longest period, so ~2 periods of fMin are buffered before a reading.
    void prepare (double sampleRate, float fMin = 65.0f, float fMax = 1000.0f)
    {
        fsIn  = (float) sampleRate;
        decim = (int) std::lround (sampleRate / 22050.0); if (decim < 1) decim = 1;
        fs    = fsIn / (float) decim;
        fLo   = fMin; fHi = fMax;

        tauMax = (int) std::ceil  (fs / fMin) + 2;
        tauMin = (int) std::floor (fs / fMax); if (tauMin < 2) tauMin = 2;
        W      = tauMax;                               // integration window = longest lag

        const int cap = W + tauMax + 4;                // room for x[j] and x[j+tau]
        buf.assign ((size_t) cap, 0.0f);
        d .assign ((size_t) (tauMax + 1), 0.0f);
        dp.assign ((size_t) (tauMax + 1), 0.0f);

        // 2nd-order anti-alias just under the decimated Nyquist.
        const float fc  = fs * 0.40f;
        const float pole = std::exp (-2.0f * kPi * fc / fsIn);
        lpA = pole; lpB = 1.0f - pole;

        hopIn = (int) (fsIn * 0.0045f); if (hopIn < 32) hopIn = 32;   // ~4.5 ms analysis hop
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        lp1 = lp2 = 0.0f; decimCnt = 0; lockTau = 0; lastTau = 0;
        writePos = 0; filled = 0; sinceAnalyze = 0;
        lastHz = 0.0f; lastConf = 0.0f;
    }

    // Feed a mono block; returns the current F0 estimate (Hz), 0 if unvoiced.
    // The estimate is held between analysis hops.
    float process (const float* mono, int n)
    {
        push (mono, n);
        sinceAnalyze += n;
        if (sinceAnalyze >= hopIn && ready())
        {
            sinceAnalyze = 0;
            analyze();
        }
        return lastHz;
    }

    float confidence() const { return lastConf; }
    float lastFreq()   const { return lastHz;   }

private:
    static constexpr float kPi = 3.14159265358979323846f;

    void push (const float* x, int n)
    {
        const int cap = (int) buf.size();
        for (int i = 0; i < n; ++i)
        {
            lp1 = lpB * x[i]  + lpA * lp1;             // 2-pole LP anti-alias
            lp2 = lpB * lp1   + lpA * lp2;
            if (++decimCnt >= decim)
            {
                decimCnt = 0;
                buf[(size_t) writePos] = lp2;
                if (++writePos >= cap) writePos = 0;
                if (filled < cap) ++filled;
            }
        }
    }

    bool ready() const { return filled >= W + tauMax; }

    // One YIN pass over [tLo,tHi] using an integration window of Wloc samples,
    // reading the most recent (Wloc + tHi) decimated samples. Returns the
    // interpolated lag, or -1 when the range holds nothing periodic.
    float yinPass (int tLo, int tHi, int Wloc, float threshold, float& confOut)
    {
        const int cap  = (int) buf.size();
        const int need = Wloc + tHi;
        int start = writePos - need; while (start < 0) start += cap;
        auto at = [&] (int k) -> float { int idx = start + k; if (idx >= cap) idx -= cap; return buf[(size_t) idx]; };

        // 1) difference function d(tau) = sum_j (x[j] - x[j+tau])^2
        for (int tau = tLo; tau <= tHi; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < Wloc; ++j)
            {
                const float diff = at (j) - at (j + tau);
                sum += diff * diff;
            }
            d[(size_t) tau] = sum;
        }

        // 2) cumulative mean normalized difference d'(tau)
        float running = 0.0f;
        for (int tau = tLo; tau <= tHi; ++tau)
        {
            running += d[(size_t) tau];
            dp[(size_t) tau] = (running > 0.0f) ? d[(size_t) tau] * (float) (tau - tLo + 1) / running : 1.0f;
        }

        // 3) absolute threshold: first dip below it (walk down to its local min),
        //    otherwise the global minimum in range.
        int tau = -1;
        for (int t = tLo; t <= tHi; ++t)
        {
            if (dp[(size_t) t] < threshold)
            {
                while (t + 1 <= tHi && dp[(size_t) (t + 1)] < dp[(size_t) t]) ++t;
                tau = t; break;
            }
        }
        if (tau < 0)
        {
            int best = tLo; float bv = dp[(size_t) tLo];
            for (int t = tLo + 1; t <= tHi; ++t)
                if (dp[(size_t) t] < bv) { bv = dp[(size_t) t]; best = t; }
            tau = best;
        }

        // 4) parabolic interpolation around tau (sub-sample period)
        float betterTau = (float) tau;
        if (tau > tLo && tau < tHi)
        {
            const float s0 = dp[(size_t) (tau - 1)], s1 = dp[(size_t) tau], s2 = dp[(size_t) (tau + 1)];
            const float denom = s0 + s2 - 2.0f * s1;
            if (std::abs (denom) > 1e-9f) betterTau = (float) tau + 0.5f * (s0 - s2) / denom;
        }
        confOut = 1.0f - dp[(size_t) tau];
        lastTau = tau;
        return betterTau;
    }

    // v1.9.3: pitch-adaptive analysis. Once locked, search only around the last
    // lag with a window of two of ITS periods - the reading arrives in roughly
    // half the time, which is what makes a fast retune actually sound fast.
    // A weak result falls straight back to the full range, so octave leaps and
    // new phrases are still picked up on the same hop.
    void analyze (float threshold = 0.12f, float minConfidence = 0.55f)
    {
        float conf = 0.0f;
        float betterTau = -1.0f;

        if (lockTau > 0)
        {
            const int tLo  = std::max (tauMin, (int) ((float) lockTau * 0.62f));          // about -8 semitones
            const int tHi  = std::min (tauMax, (int) ((float) lockTau * 1.62f) + 1);      // about +8 semitones
            const int Wloc = std::min (W, std::max (2 * lockTau, 4 * tauMin));
            if (tHi > tLo + 2)
                betterTau = yinPass (tLo, tHi, Wloc, threshold, conf);
        }

        if (betterTau < 0.0f || conf < minConfidence)      // not locked, or lost it
            betterTau = yinPass (tauMin, tauMax, W, threshold, conf);

        lastConf = conf;
        if (betterTau <= 0.0f || lastConf < minConfidence) { lastHz = 0.0f; lockTau = 0; return; }
        const float hz = fs / betterTau;
        lastHz = (hz >= fLo && hz <= fHi) ? hz : 0.0f;
        // keep the lag we just trusted, so the next hop can use the short window
        lockTau = (lastHz > 0.0f) ? lastTau : 0;
    }

    float fsIn = 44100.0f, fs = 22050.0f, fLo = 65.0f, fHi = 1000.0f;
    int   decim = 2, tauMin = 22, tauMax = 340, W = 340, hopIn = 200;
    int   lockTau = 0, lastTau = 0;   // v1.9.3: adaptive-window state
    std::vector<float> buf, d, dp;
    float lpA = 0.0f, lpB = 1.0f, lp1 = 0.0f, lp2 = 0.0f;
    int   decimCnt = 0, writePos = 0, filled = 0, sinceAnalyze = 0;
    float lastHz = 0.0f, lastConf = 0.0f;
};

// ---- scale / snapping helpers (shared by the processor) ----------------------
namespace scale
{
    // 12-bit pitch-class masks (bit p set => semitone p above the tonic is allowed).
    enum { Chromatic = 0, Major, Minor, HarmMinor, PentMajor, PentMinor, Blues, Dorian, Mixolydian, Count };

    inline int mask (int scaleId)
    {
        switch (scaleId)
        {
            case Major:      return 0b101010110101; // 0 2 4 5 7 9 11
            case Minor:      return 0b010110101101; // 0 2 3 5 7 8 10
            case HarmMinor:  return 0b100110101101; // 0 2 3 5 7 8 11
            case PentMajor:  return 0b001010010101; // 0 2 4 7 9
            case PentMinor:  return 0b010010101001; // 0 3 5 7 10
            case Blues:      return 0b010011101001; // 0 3 5 6 7 10
            case Dorian:     return 0b011010101101; // 0 2 3 5 7 9 10
            case Mixolydian: return 0b011010110101; // 0 2 4 5 7 9 10
            case Chromatic:
            default:         return 0b111111111111;
        }
    }

    // Nearest allowed note (MIDI, float-in) for a continuous MIDI pitch p.
    inline float snap (float p, int key, int scaleId)
    {
        const int m    = mask (scaleId);
        const int base = (int) std::floor (p + 0.5f);
        float bestDist = 1.0e9f; int best = base;
        for (int cand = base - 7; cand <= base + 7; ++cand)
        {
            const int pc = (((cand - key) % 12) + 12) % 12;
            if (m & (1 << pc))
            {
                const float dist = std::abs ((float) cand - p);
                if (dist < bestDist) { bestDist = dist; best = cand; }
            }
        }
        return (float) best;
    }

// v1.9.8: move a note by N scale degrees (not semitones). A "third" is four
// semitones on some degrees and three on others; stepping through the scale gets
// that right automatically, which is what makes a harmony sit in the key.
inline float step (float midiNote, int key, int scaleId, int degrees)
{
    if (degrees == 0) return snap (midiNote, key, scaleId);
    const int m = mask (scaleId);
    float cur = snap (midiNote, key, scaleId);
    const int dir = degrees > 0 ? 1 : -1;
    for (int taken = 0; taken != degrees; taken += dir)
    {
        for (int guard = 0; guard < 24; ++guard)      // 次の音階音まで進める
        {
            cur += (float) dir;
            int pc = ((int) std::lround (cur) - key) % 12; if (pc < 0) pc += 12;
            if (m & (1 << pc)) break;
        }
    }
    return cur;
}

// v2.0.0 反行ハモリ: メロディと「逆方向」に動く対旋律を作る。
// 合唱・ストリングスアレンジの基本技法で、平行3度より立体的に聴こえる。
//   ・メロディが音階を n 度上がったら、ハモリは n 度下がる(逆も同じ)
//   ・開きすぎ(10半音超)や近すぎ(2半音未満)になったら3度/6度へ静かに戻す
//   ・トライトーン/短2度/長7度の濁りは、動いていた方向へもう1音逃がす
// 状態2つだけの純関数的な小さなクラスなので、単体テストできる(JUCE非依存)。
struct ContraryLine
{
    float note     = 0.0f;   // 現在のハモリ音 (MIDI)。0 = 未開始
    float prevSnap = 0.0f;   // 前回のメロディ音階音 (MIDI)

    void reset() { note = 0.0f; prevSnap = 0.0f; }

    // melody の音階上の位置の差を「度数」で数える(±8度で打ち切り)
    static int degreesBetween (float fromSnap, float toSnap, int key, int scaleId)
    {
        if (std::abs (toSnap - fromSnap) < 0.5f) return 0;
        const int dir = toSnap > fromSnap ? 1 : -1;
        float cur = fromSnap;
        for (int deg = 1; deg <= 8; ++deg)
        {
            cur = step (cur, key, scaleId, dir);
            if (std::abs (cur - toSnap) < 0.5f) return deg * dir;
            if ((dir > 0 && cur > toSnap) || (dir < 0 && cur < toSnap)) break;
        }
        return 9 * dir;   // 大きな跳躍(オクターブ跳び等) → 呼び出し側で再アンカー
    }

    // メロディ音 s の周りで「2声で成立する音程」(±3,4,5,7,8,9半音)にある音階音の
    // うち、want に最も近いものを返す。度数ではなく半音で探すので、5音階でも
    // 開き方が暴れない(度数だと5音階の5度数=約1オクターブになってしまう)。
    static float consonantNear (float s, float want, int key, int scaleId)
    {
        const int m  = mask (scaleId);
        const int s0 = (int) std::lround (s);
        float best = 0.0f, bestDist = 1.0e9f;
        for (int c = s0 - 10; c <= s0 + 10; ++c)
        {
            int pc = (c - key) % 12; if (pc < 0) pc += 12;
            if (! (m & (1 << pc))) continue;
            const int iv = std::abs (c - s0);
            if (! (iv==3 || iv==4 || iv==5 || iv==7 || iv==8 || iv==9)) continue;
            const float dist = std::abs ((float) c - want);
            if (dist < bestDist) { bestDist = dist; best = (float) c; }
        }
        return bestDist < 1.0e8f ? best : snap (want, key, scaleId);
    }

    // 歌の現在ピッチ pH (MIDI, 連続値) から、反行ハモリの音を返す
    float update (float pH, int key, int scaleId)
    {
        const float s = snap (pH, key, scaleId);

        if (note <= 0.0f)                       // 歌い出し: まず3度下あたりから
        {
            note = consonantNear (s, s - 3.5f, key, scaleId);
            prevSnap = s;
        }
        else if (std::abs (s - prevSnap) >= 0.5f)
        {
            const int moved = degreesBetween (prevSnap, s, key, scaleId);
            if (moved >= 9 || moved <= -9)      // メロディが大きく跳んだ → 3度で再出発
                note = consonantNear (s, (note <= prevSnap ? s - 3.5f : s + 3.5f), key, scaleId);
            else
                note = step (note, key, scaleId, -moved);   // ← 反行の本体
            prevSnap = s;
        }

        // 正規化: 濁る音程・近すぎ・開きすぎになったら、今いる場所に一番近い
        // 「成立する音」へ寄せる(半音基準なのでどのスケールでも±10半音に収まる)
        const int ivAbs = std::abs ((int) std::lround (note) - (int) std::lround (s));
        if (! (ivAbs==3 || ivAbs==4 || ivAbs==5 || ivAbs==7 || ivAbs==8 || ivAbs==9))
            note = consonantNear (s, note, key, scaleId);

        return note;
    }
};

}

} // namespace gz
