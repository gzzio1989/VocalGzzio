#pragma once
// =============================================================================
//  CarbonVocalEngine.h — VocalGzzio v6.0.0 (Carbon Studio Monolith Edition)
// =============================================================================
//  Carbon の設計思想に基づく 5 大スタジオ音響＆リアルタイム計測コア（完全ゼロヒープ確保・0.0ms追加遅延）:
//    1. CarbonAdaaSaturator         : 1次反導関数アンチエイリアシング(ADAA)非線形カーネル（高域倍音折り返し歪み根絶）
//    2. CarbonSpectralBudgetArbitrator : 多段EQ・アップワード圧縮の競合と位相濁りを防ぐ統合バジェット調停器
//    3. CarbonAirPreservingDeEsser  : サ行制動中も >11.2kHz の最高域シルクエアーを無傷保護するスプリット復元器
//    4. CarbonMsReverbPocket        : 歌唱中は残響のMid成分だけを譲り、語尾で解放するM/S残響ボーカルポケット
//    5. CarbonLiveTelemetry         : ITU-R BS.1770-4 K重み付け LUFS / dBTP / 真のGR / 64点推移リング計測器
// =============================================================================

#include <cmath>
#include <algorithm>
#include <atomic>
#include <array>

namespace gz
{

// -----------------------------------------------------------------------------
// 1. 1次 ADAA (Antiderivative Anti-Aliasing) 非線形カーネル
// -----------------------------------------------------------------------------
struct CarbonAdaaSaturator
{
    float prevX { 0.0f };
    float prevF { 0.0f };

    void reset() noexcept
    {
        prevX = 0.0f;
        prevF = 0.0f;
    }

    // f(x) = tanh(x) の反導関数 F(x) = ln(cosh(x)) = |x| + ln(1 + e^{-2|x|}) - ln(2)
    static inline float antiderivTanh (float x) noexcept
    {
        const float ax = std::abs (x);
        return ax + std::log1pf (std::exp (-2.0f * ax)) - 0.69314718f;
    }

    inline float processTanh (float x, float drive = 1.0f) noexcept
    {
        const float u = x * drive;
        const float fu = antiderivTanh (u);
        const float du = u - prevX;
        float y = 0.0f;
        if (std::abs (du) > 1.0e-4f)
            y = (fu - prevF) / du;
        else
            y = std::tanh (0.5f * (u + prevX));
        prevX = u;
        prevF = fu;
        return (drive > 1.001f) ? (y / std::tanh (drive)) : y;
    }

    // f(x) = x / (1 + |x|) の反導関数 F(x) = |x| - ln(1 + |x|)（超高域エアー励起用）
    static inline float antiderivSoftAir (float x) noexcept
    {
        const float ax = std::abs (x);
        return ax - std::log1pf (ax);
    }

    inline float processSoftAir (float x) noexcept
    {
        const float fx = antiderivSoftAir (x);
        const float dx = x - prevX;
        float y = 0.0f;
        if (std::abs (dx) > 1.0e-4f)
            y = (fx - prevF) / dx;
        else
        {
            const float xm = 0.5f * (x + prevX);
            y = xm / (1.0f + std::abs (xm));
        }
        prevX = x;
        prevF = fx;
        return y;
    }
};

// -----------------------------------------------------------------------------
// 2. Carbon 多段スペクトル＆音圧バジェット統合調停器
// -----------------------------------------------------------------------------
struct CarbonSpectralBudgetArbitrator
{
    struct BudgetScalars
    {
        float presenceScale { 1.0f }; // 2.5kHz〜4.2kHz 過剰重畳の抑制係数 (0.55..1.0)
        float airScale      { 1.0f }; // >5kHz 倍音エアー重畳の抑制係数 (0.58..1.0)
        float upwardScale   { 1.0f }; // 小音量持ち上げ重畳の抑制係数 (0.50..1.0)
    };

    float smoothPresence { 1.0f };
    float smoothAir      { 1.0f };
    float smoothUpward   { 1.0f };

    void reset() noexcept
    {
        smoothPresence = 1.0f;
        smoothAir      = 1.0f;
        smoothUpward   = 1.0f;
    }

    // ブロック先頭で現在の有効モジュール群の要求ゲイン総量（dB相当）から最適配分係数を算出する
    inline BudgetScalars computeBlockBudget (float presenceDb,
                                             float ringPct,
                                             float argonFocusPct,
                                             bool  sweetSpotOn,
                                             int   polishMode,
                                             float airDb,
                                             float argonSilkPct,
                                             bool  upwardAirOn,
                                             float sustainPct,
                                             float ridePct,
                                             int   prismMode) noexcept
    {
        // ① 3kHz帯プレゼンス要求総量（上限目安: +6.2 dB 相当を超えたら滑らかに協調配分）
        const float presDemand = std::max (0.0f, presenceDb)
                               + 0.035f * ringPct
                               + 0.042f * argonFocusPct
                               + (sweetSpotOn ? 1.4f : 0.0f)
                               + (polishMode > 0 ? (polishMode == 2 ? 2.2f : 1.5f) : 0.0f);
        const float wantPres = (presDemand > 6.2f)
                             ? std::clamp (6.2f / presDemand, 0.58f, 1.0f)
                             : 1.0f;

        // ② 超高域エアー要求総量（上限目安: +6.8 dB 相当）
        const float airDemand = std::max (0.0f, airDb)
                              + 0.048f * argonSilkPct
                              + (upwardAirOn ? 2.2f : 0.0f)
                              + (polishMode > 0 ? 1.6f : 0.0f);
        const float wantAir = (airDemand > 6.8f)
                            ? std::clamp (6.8f / airDemand, 0.60f, 1.0f)
                            : 1.0f;

        // ③ アップワード持ち上げ要求総量（上限目安: +7.5 dB 相当、リップノイズ・息継ぎ過大浮上を防止）
        const float upDemand = (upwardAirOn ? 3.8f : 0.0f)
                             + 0.065f * sustainPct
                             + 0.055f * ridePct
                             + 0.030f * argonFocusPct
                             + (prismMode > 0 ? (1.8f + 0.45f * (float) prismMode) : 0.0f);
        const float wantUp = (upDemand > 7.5f)
                           ? std::clamp (7.5f / upDemand, 0.52f, 1.0f)
                           : 1.0f;

        smoothPresence += 0.20f * (wantPres - smoothPresence);
        smoothAir      += 0.20f * (wantAir  - smoothAir);
        smoothUpward   += 0.20f * (wantUp   - smoothUpward);

        return { smoothPresence, smoothAir, smoothUpward };
    }
};

// -----------------------------------------------------------------------------
// 3. サ行制動時の >11.5kHz シルクエアー無傷復元器（Split-Band Air Guard）
// -----------------------------------------------------------------------------
struct CarbonAirPreservingDeEsser
{
    float hpL { 0.0f }, hpR { 0.0f };
    float lpNyqL { 0.0f }, lpNyqR { 0.0f };
    float hpCoef { 0.35f };
    float nyqCoef { 0.82f };
    float gainSmooth { 0.0f };

    void prepare (double sampleRate) noexcept
    {
        const float sr = (float) std::max (16000.0, sampleRate);
        const float fcAir = std::min (11800.0f, sr * 0.40f);
        const float fcNyq = std::min (18500.0f, sr * 0.44f);
        hpCoef  = 1.0f - std::exp (-2.0f * 3.14159265f * fcAir / sr);
        nyqCoef = 1.0f - std::exp (-2.0f * 3.14159265f * fcNyq / sr);
        reset();
    }

    void reset() noexcept
    {
        hpL = hpR = 0.0f;
        lpNyqL = lpNyqR = 0.0f;
        gainSmooth = 0.0f;
    }

    // 6.5kHz High-Shelf ディエッサーが削った >11.8kHz の無害な息の煌めきのみを最大72%位相整合復元する
    inline void restoreTopAirBlock (const float* preDsL, const float* preDsR,
                                    float* postDsL, float* postDsR,
                                    int numSamples, float dsReductionDb) noexcept
    {
        if (numSamples <= 0 || preDsL == nullptr || postDsL == nullptr)
            return;

        // dsReductionDb は負値（例: -4.5 dB）
        const float absRedDb = std::clamp (-dsReductionDb, 0.0f, 9.0f);
        const float targetRestore = (absRedDb > 0.25f)
                                  ? std::clamp (absRedDb / 9.0f, 0.0f, 0.72f)
                                  : 0.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            gainSmooth += 0.015f * (targetRestore - gainSmooth);

            const float inL = preDsL[n];
            hpL += hpCoef * (inL - hpL);
            const float topL = inL - hpL;
            lpNyqL += nyqCoef * (topL - lpNyqL);

            if (gainSmooth > 1.0e-3f)
            {
                // サ行の刺さり帯(5〜8.5kHz)を含まない >11.8kHz 帯の純粋なエアー差分だけを戻す
                postDsL[n] += lpNyqL * (gainSmooth * 0.55f);
            }

            if (preDsR != nullptr && postDsR != nullptr)
            {
                const float inR = preDsR[n];
                hpR += hpCoef * (inR - hpR);
                const float topR = inR - hpR;
                lpNyqR += nyqCoef * (topR - lpNyqR);
                if (gainSmooth > 1.0e-3f)
                    postDsR[n] += lpNyqR * (gainSmooth * 0.55f);
            }
        }
    }
};

// -----------------------------------------------------------------------------
// 4. Carbon M/S 残響ボーカルポケット（歌唱中 Mid ダッキング ＆ 語尾ブルーム）
// -----------------------------------------------------------------------------
struct CarbonMsReverbPocket
{
    float vocBpState { 0.0f };
    float vocLpState { 0.0f };
    float vocEnv     { 0.0f };
    float midDuckEnv { 0.0f };
    float hpCoef     { 0.04f };
    float lpCoef     { 0.35f };
    float atkCoef    { 0.003f };
    float relCoef    { 0.0002f };

    void prepare (double sampleRate) noexcept
    {
        const float sr = (float) std::max (16000.0, sampleRate);
        hpCoef  = 1.0f - std::exp (-2.0f * 3.14159265f * 320.0f  / sr);
        lpCoef  = 1.0f - std::exp (-2.0f * 3.14159265f * 3400.0f / sr);
        atkCoef = 1.0f - std::exp (-1.0f / (0.012f * sr)); // 12ms 素早いポケット形成
        relCoef = 1.0f - std::exp (-1.0f / (0.145f * sr)); // 145ms 自然な語尾残響の開花
        reset();
    }

    void reset() noexcept
    {
        vocBpState = 0.0f;
        vocLpState = 0.0f;
        vocEnv     = 0.0f;
        midDuckEnv = 0.0f;
    }

    // 残響 Wet バッファに対して、主旋律が存在する瞬間だけ Mid を最大 -3.6dB 譲り、Side を +8% 広げる。
    // 戻り値: 現在のポケット深さ（正の dB 値、テレメトリ表示用）
    inline float processReverbWetPocket (const float* dryVoxL, const float* dryVoxR,
                                         float* revWetL, float* revWetR,
                                         int numSamples, bool monoOut) noexcept
    {
        if (numSamples <= 0 || dryVoxL == nullptr || revWetL == nullptr || revWetR == nullptr || monoOut)
        {
            midDuckEnv *= 0.85f;
            return 0.0f;
        }

        float maxPocketDb = 0.0f;
        for (int n = 0; n < numSamples; ++n)
        {
            const float vox = (dryVoxR != nullptr) ? 0.5f * (dryVoxL[n] + dryVoxR[n]) : dryVoxL[n];
            vocBpState += hpCoef * (vox - vocBpState);
            const float hp = vox - vocBpState;
            vocLpState += lpCoef * (hp - vocLpState);
            const float absMidVox = std::abs (vocLpState);

            vocEnv += ((absMidVox > vocEnv) ? atkCoef : relCoef) * (absMidVox - vocEnv);

            // -38 dBFS (0.0126) 以上で発声中と判定し、最大 0.34 (-3.6 dB) Mid を譲る
            const float wantDuck = std::clamp ((vocEnv - 0.012f) * 3.8f, 0.0f, 0.34f);
            midDuckEnv += ((wantDuck > midDuckEnv) ? atkCoef : relCoef) * (wantDuck - midDuckEnv);

            const float wL = revWetL[n];
            const float wR = revWetR[n];
            const float mid  = 0.5f * (wL + wR) * (1.0f - midDuckEnv);
            const float side = 0.5f * (wL - wR) * (1.0f + 0.24f * midDuckEnv);
            revWetL[n] = mid + side;
            revWetR[n] = mid - side;

            const float pDb = -20.0f * std::log10 (std::max (0.5f, 1.0f - midDuckEnv));
            if (pDb > maxPocketDb) maxPocketDb = pDb;
        }
        return maxPocketDb;
    }
};

// -----------------------------------------------------------------------------
// 5. Carbon ITU-R BS.1770-4 リアルタイムラウドネス＆テレメトリエンジン
// -----------------------------------------------------------------------------
class CarbonLiveTelemetry
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double sr = std::max (16000.0, sampleRate);
        // ITU-R BS.1770-4 Stage 1: 1.5kHz +4dB 頭部音響ハイシェルフ（双一次変換）
        {
            const double f0 = 1681.974450955533;
            const double G  = 3.999843853973347;
            const double Q  = 0.7071752369554196;
            const double K  = std::tan (3.141592653589793 * f0 / sr);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            hsB0 = (float) ((Vh + Vb * K / Q + K * K) / a0);
            hsB1 = (float) (2.0 * (K * K - Vh) / a0);
            hsB2 = (float) ((Vh - Vb * K / Q + K * K) / a0);
            hsA1 = (float) (2.0 * (K * K - 1.0) / a0);
            hsA2 = (float) ((1.0 - K / Q + K * K) / a0);
        }
        // ITU-R BS.1770-4 Stage 2: 38Hz RLB ハイパスフィルタ
        {
            const double f0 = 38.13547087602444;
            const double Q  = 0.5003270373238773;
            const double K  = std::tan (3.141592653589793 * f0 / sr);
            const double a0 = 1.0 + K / Q + K * K;
            hpB0 = (float) (1.0 / a0);
            hpB1 = (float) (-2.0 / a0);
            hpB2 = (float) (1.0 / a0);
            hpA1 = (float) (2.0 * (K * K - 1.0) / a0);
            hpA2 = (float) ((1.0 - K / Q + K * K) / a0);
        }
        momAlpha = 1.0f - std::exp (-1.0f / (0.400f * (float) sr));
        stAlpha  = 1.0f - std::exp (-1.0f / (3.000f * (float) sr));
        tpDecay  = std::exp (-1.0f / (0.350f * (float) sr));
        reset();
    }

    void reset() noexcept
    {
        hsZ1[0] = hsZ1[1] = hsZ2[0] = hsZ2[1] = 0.0f;
        hpZ1[0] = hpZ1[1] = hpZ2[0] = hpZ2[1] = 0.0f;
        tpHistL[0] = tpHistL[1] = tpHistL[2] = 0.0f;
        tpHistR[0] = tpHistR[1] = tpHistR[2] = 0.0f;
        momMeanSq = 0.0f;
        stMeanSq  = 0.0f;
        tpHoldLin = 0.0f;
        momentaryLufs.store (-70.0f, std::memory_order_relaxed);
        shortTermLufs.store (-70.0f, std::memory_order_relaxed);
        truePeakDbtp.store  (-90.0f, std::memory_order_relaxed);
        reverbPocketDb.store (0.0f,  std::memory_order_relaxed);
        deEssGrDb.store      (0.0f,  std::memory_order_relaxed);
        histSampleAcc = 0;
    }

    inline void processOutputBlock (const float* L, const float* R, int numSamples,
                                    float pitchCents, float compGrDb,
                                    float deEssDb, float pocketDb) noexcept
    {
        if (numSamples <= 0 || L == nullptr)
            return;

        for (int n = 0; n < numSamples; ++n)
        {
            const float xL = L[n];
            const float xR = (R != nullptr) ? R[n] : xL;

            // 4点エルミート3次補間（係数総和 = 1.0000 厳密一致）による正確な 4x Inter-Sample True-Peak (dBTP) 計測
            const float midL25 = -0.0703125f * tpHistL[0] + 0.8671875f * tpHistL[1] + 0.2265625f * tpHistL[2] - 0.0234375f * xL;
            const float midL50 = -0.0625000f * tpHistL[0] + 0.5625000f * tpHistL[1] + 0.5625000f * tpHistL[2] - 0.0625000f * xL;
            const float midL75 = -0.0234375f * tpHistL[0] + 0.2265625f * tpHistL[1] + 0.8671875f * tpHistL[2] - 0.0703125f * xL;
            tpHistL[0] = tpHistL[1]; tpHistL[1] = tpHistL[2]; tpHistL[2] = xL;

            const float midR25 = -0.0703125f * tpHistR[0] + 0.8671875f * tpHistR[1] + 0.2265625f * tpHistR[2] - 0.0234375f * xR;
            const float midR50 = -0.0625000f * tpHistR[0] + 0.5625000f * tpHistR[1] + 0.5625000f * tpHistR[2] - 0.0625000f * xR;
            const float midR75 = -0.0234375f * tpHistR[0] + 0.2265625f * tpHistR[1] + 0.8671875f * tpHistR[2] - 0.0703125f * xR;
            tpHistR[0] = tpHistR[1]; tpHistR[1] = tpHistR[2]; tpHistR[2] = xR;

            const float pkL = std::max (std::abs (xL), std::max (std::abs (midL50), std::max (std::abs (midL25), std::abs (midL75))));
            const float pkR = std::max (std::abs (xR), std::max (std::abs (midR50), std::max (std::abs (midR25), std::abs (midR75))));
            const float pk  = std::max (pkL, pkR);
            tpHoldLin = std::max (pk, tpHoldLin * tpDecay);

            // K重み付け 2段DF2Tフィルタ
            const float s0L = hsB0 * xL + hsZ1[0];
            hsZ1[0] = hsB1 * xL - hsA1 * s0L + hsZ2[0];
            hsZ2[0] = hsB2 * xL - hsA2 * s0L;
            const float kL  = hpB0 * s0L + hpZ1[0];
            hpZ1[0] = hpB1 * s0L - hpA1 * kL + hpZ2[0];
            hpZ2[0] = hpB2 * s0L - hpA2 * kL;

            float kSq = kL * kL;
            if (R != nullptr)
            {
                const float s0R = hsB0 * xR + hsZ1[1];
                hsZ1[1] = hsB1 * xR - hsA1 * s0R + hsZ2[1];
                hsZ2[1] = hsB2 * xR - hsA2 * s0R;
                const float kR  = hpB0 * s0R + hpZ1[1];
                hpZ1[1] = hpB1 * s0R - hpA1 * kR + hpZ2[1];
                hpZ2[1] = hpB2 * s0R - hpA2 * kR;
                kSq = 0.5f * (kL * kL + kR * kR) * 2.0f; // BS.1770 L+R sum of channel powers
            }
            else
            {
                kSq = 2.0f * kL * kL;
            }

            momMeanSq += momAlpha * (kSq - momMeanSq);
            stMeanSq  += stAlpha  * (kSq - stMeanSq);
        }

        const float mLufs = (momMeanSq > 1.0e-10f) ? (-0.691f + 10.0f * std::log10 (momMeanSq)) : -70.0f;
        const float sLufs = (stMeanSq  > 1.0e-10f) ? (-0.691f + 10.0f * std::log10 (stMeanSq))  : -70.0f;
        const float dbtp  = (tpHoldLin > 1.0e-6f)  ? (20.0f * std::log10 (tpHoldLin))            : -90.0f;

        momentaryLufs.store  (std::clamp (mLufs, -70.0f, 6.0f),  std::memory_order_relaxed);
        shortTermLufs.store  (std::clamp (sLufs, -70.0f, 6.0f),  std::memory_order_relaxed);
        truePeakDbtp.store   (std::clamp (dbtp,  -90.0f, 6.0f),  std::memory_order_relaxed);
        deEssGrDb.store      (std::clamp (deEssDb, 0.0f, 24.0f), std::memory_order_relaxed);
        reverbPocketDb.store (std::clamp (pocketDb, 0.0f, 12.0f), std::memory_order_relaxed);

        // 約 40ms ごとに 64 点ロックフリー履歴リングを更新（UI波形トレース用）
        histSampleAcc += numSamples;
        if (histSampleAcc >= 1920)
        {
            histSampleAcc = 0;
            const uint32_t idx = histWriteIdx.load (std::memory_order_relaxed) & 63u;
            centHistory[idx].store (std::clamp (pitchCents, -50.0f, 50.0f), std::memory_order_relaxed);
            grHistory[idx].store   (std::clamp (compGrDb,     0.0f, 18.0f), std::memory_order_relaxed);
            histWriteIdx.store ((idx + 1u) & 63u, std::memory_order_relaxed);
        }
    }

    float getMomentaryLufs()  const noexcept { return momentaryLufs.load  (std::memory_order_relaxed); }
    float getShortTermLufs()  const noexcept { return shortTermLufs.load  (std::memory_order_relaxed); }
    float getTruePeakDbtp()   const noexcept { return truePeakDbtp.load   (std::memory_order_relaxed); }
    float getDeEssGrDb()      const noexcept { return deEssGrDb.load      (std::memory_order_relaxed); }
    float getReverbPocketDb() const noexcept { return reverbPocketDb.load (std::memory_order_relaxed); }

    void copyTelemetryHistory (std::array<float, 64>& outCents, std::array<float, 64>& outGr) const noexcept
    {
        const uint32_t w = histWriteIdx.load (std::memory_order_relaxed) & 63u;
        for (uint32_t i = 0; i < 64u; ++i)
        {
            const uint32_t p = (w + i) & 63u;
            outCents[i] = centHistory[p].load (std::memory_order_relaxed);
            outGr[i]    = grHistory[p].load   (std::memory_order_relaxed);
        }
    }

private:
    float hsB0 { 1.0f }, hsB1 { 0.0f }, hsB2 { 0.0f }, hsA1 { 0.0f }, hsA2 { 0.0f };
    float hpB0 { 1.0f }, hpB1 { 0.0f }, hpB2 { 0.0f }, hpA1 { 0.0f }, hpA2 { 0.0f };
    float hsZ1[2] { 0.0f, 0.0f }, hsZ2[2] { 0.0f, 0.0f };
    float hpZ1[2] { 0.0f, 0.0f }, hpZ2[2] { 0.0f, 0.0f };
    float tpHistL[3] { 0.0f, 0.0f, 0.0f }, tpHistR[3] { 0.0f, 0.0f, 0.0f };
    float momAlpha { 0.001f }, stAlpha { 0.0001f }, tpDecay { 0.9999f };
    float momMeanSq { 0.0f }, stMeanSq { 0.0f }, tpHoldLin { 0.0f };

    std::atomic<float> momentaryLufs  { -70.0f };
    std::atomic<float> shortTermLufs  { -70.0f };
    std::atomic<float> truePeakDbtp   { -90.0f };
    std::atomic<float> deEssGrDb      { 0.0f };
    std::atomic<float> reverbPocketDb { 0.0f };

    int histSampleAcc { 0 };
    std::atomic<uint32_t> histWriteIdx { 0 };
    std::array<std::atomic<float>, 64> centHistory {};
    std::array<std::atomic<float>, 64> grHistory   {};
};

} // namespace gz
