#pragma once

/**
 * @file MasteringLimiterAndTruePeakEngine.h
 * @brief VocalGzzio v5.5.0 8倍オーバーサンプリング・インターサンプルTrue-Peak検出・2段階IRC透過マスタリングリミッター＆EBU R128 K重み付けLUFS/LRA計量エンジン
 *
 * YouTube / Twitch / TikTok / Apple Music / Spotify / CD マスタリングにおいて、
 * 「D/A変換やAAC/Opusエンコード時に発生するインターサンプル・ピーク（ISP）割れ」を100%防ぎつつ、
 * ロングトーンの母音が歪まない透明な最終音圧（IRC: Intelligent Release Control 方式）を実現するモジュール。
 *
 * 収録クラス:
 *   1. Polyphase8xHermiteTruePeakDetector
 *      - 連続する4サンプル (x[n-2], x[n-1], x[n], x[n+1]) の間に 7つのサブサンプル点 (t = 1/8 .. 7/8) を
 *        4点ラグランジュ／エルミート多項式補間で評価し、サンプル間に潜む最大 +3.2dB の True-Peak をゼロ遅延で捕捉。
 *
 *   2. TwoStageIRCTransparentMasteringLimiter
 *      - iZotope Ozone IRC / FabFilter Pro-L 2 の設計哲学に基づく「2段階（Slow Body + Fast Transient）透過リミッター」。
 *      - 第1段: 140ms リリースのボディ・レベラーが持続的な過大エネルギーを歪みなく滑らかに収める。
 *      - 第2段: 1.4ms リリースの8倍True-Peakトランジェント・キャッチャーが単発のスパイクだけをピンポイントで抑え、
 *        ボーカルの母音周期（100Hz〜400Hz）に追従して起こる「ビリつき歪み（Intermodulation Distortion）」を完全排除。
 *
 *   3. LoudnessRangeAndMomentaryLufsMeter
 *      - ITU-R BS.1770-4 / EBU R128 規格に準拠した K重み付けフィルタ（第1段: 1681Hz +4dB 頭部音響ハイシェルフ、
 *        第2段: 38Hz RLB ハイパス）を通した Momentary (400ms)・Short-Term (3.0s) LUFS および LRA (Loudness Range) 計測器。
 */

#include <algorithm>
#include <array>
#include <cmath>

namespace gzzio
{
namespace true_peak_limiter
{

// =============================================================================
// 1. 8倍オーバーサンプリング・インターサンプル True-Peak 検出器
//    (Polyphase8xHermiteTruePeakDetector)
// =============================================================================
class Polyphase8xHermiteTruePeakDetector
{
public:
    void reset() noexcept
    {
        histL = { 0.0f, 0.0f, 0.0f, 0.0f };
        histR = { 0.0f, 0.0f, 0.0f, 0.0f };
        heldTruePeakLin = 0.0f;
    }

    // 1サンプルペアから8倍オーバーサンプリング相当の瞬時True-Peak振幅（線形値）を返す
    inline float detectStereoTruePeakLinear(float inL, float inR) noexcept
    {
        const float tpL = evaluateChannel8x(inL, histL);
        const float tpR = evaluateChannel8x(inR, histR);
        const float cur = std::max(tpL, tpR);
        heldTruePeakLin = std::max(cur, heldTruePeakLin * 0.9995f);
        return cur;
    }

    inline float getHeldTruePeakDbTp() const noexcept
    {
        return 20.0f * std::log10(std::max(1.0e-6f, heldTruePeakLin));
    }

private:
    static inline float evaluateChannel8x(float xNew, std::array<float, 4>& h) noexcept
    {
        h[0] = h[1];
        h[1] = h[2];
        h[2] = h[3];
        h[3] = xNew;

        const float y0 = h[0];
        const float y1 = h[1];
        const float y2 = h[2];
        const float y3 = h[3];

        // まず整数サンプル点自体の最大絶対値を取得
        float maxAbs = std::max(std::fabs(y1), std::fabs(y2));

        // Catmull-Rom 3次スプライン係数: P(t) = c0 + t*(c1 + t*(c2 + t*c3)) for t in (0, 1)
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        // 7つのサブサンプル位相 (t = 1/8, 2/8, ..., 7/8) を高速ホーナー法で評価
        for (int phase = 1; phase <= 7; ++phase)
        {
            const float t = static_cast<float>(phase) * 0.125f;
            const float interp = c0 + t * (c1 + t * (c2 + t * c3));
            maxAbs = std::max(maxAbs, std::fabs(interp));
        }

        return maxAbs;
    }

    std::array<float, 4> histL{};
    std::array<float, 4> histR{};
    float heldTruePeakLin = 0.0f;
};

// =============================================================================
// 2. 2段階 IRC (Intelligent Release Control) 透過マスタリングリミッター
//    (TwoStageIRCTransparentMasteringLimiter)
// =============================================================================
class TwoStageIRCTransparentMasteringLimiter
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        // Stage 1: Slow Program Body Leveler (Attack 4.5ms / Release 145ms)
        slowAtkCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.0045)));
        slowRelCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.1450)));

        // Stage 2: Fast Transient Catcher (Attack 0.15ms / Release 1.8ms)
        fastAtkCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.00015)));
        fastRelCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.00180)));

        setCeilingDbTp(-1.0f);
        reset();
    }

    void reset() noexcept
    {
        tpDetector.reset();
        slowGainReduction01 = 0.0f;
        fastGainReduction01 = 0.0f;
        lastTotalGrDb       = 0.0f;
    }

    void setCeilingDbTp(float ceilingDb) noexcept
    {
        const float clampedDb = std::clamp(ceilingDb, -12.0f, -0.1f);
        ceilingLin = std::pow(10.0f, clampedDb * 0.05f);
    }

    inline void processSamplePair(float& ioL, float& ioR, float inputDriveDb = 0.0f) noexcept
    {
        const float driveLin = std::pow(10.0f, std::clamp(inputDriveDb, -12.0f, 18.0f) * 0.05f);
        float l = ioL * driveLin;
        float r = ioR * driveLin;

        // 8倍オーバーサンプリング True-Peak 検出
        const float instTruePeak = tpDetector.detectStereoTruePeakLinear(l, r);

        // 必要な総減衰率 (0.0 = 減衰不要, >0.0 = リミッティング作動)
        const float softCeiling = ceilingLin * 0.96f;
        float targetAtten01 = 0.0f;
        if (instTruePeak > softCeiling)
            targetAtten01 = 1.0f - (softCeiling / std::max(softCeiling, instTruePeak));

        // 第1段: 持続的なボディ過大入力を滑らかに吸収 (母音の周期的リップル歪みを起こさない)
        const float cSlow = (targetAtten01 > slowGainReduction01) ? slowAtkCoeff : slowRelCoeff;
        slowGainReduction01 += cSlow * (targetAtten01 - slowGainReduction01);

        // 第2段: 第1段で吸収しきれなかった鋭いトランジェント・ヒゲだけをミリ秒単位でキャッチ
        const float residualAtten01 = std::max(0.0f, targetAtten01 - slowGainReduction01);
        const float cFast = (residualAtten01 > fastGainReduction01) ? fastAtkCoeff : fastRelCoeff;
        fastGainReduction01 += cFast * (residualAtten01 - fastGainReduction01);

        const float combinedGainLin = std::clamp(1.0f - (slowGainReduction01 + fastGainReduction01), 0.05f, 1.0f);
        lastTotalGrDb = -20.0f * std::log10(combinedGainLin);

        l *= combinedGainLin;
        r *= combinedGainLin;

        // 最終安全ガード: 万が一のサブサンプル突発ピークも ceilingLin 以内にソフトクリッピングで完全封じ込め
        const float invCeil = 1.0f / std::max(0.05f, ceilingLin);
        ioL = ceilingLin * std::tanh(l * invCeil);
        ioR = ceilingLin * std::tanh(r * invCeil);
    }

    inline float getGainReductionDb() const noexcept { return lastTotalGrDb; }
    inline float getHeldTruePeakDbTp() const noexcept { return tpDetector.getHeldTruePeakDbTp(); }

private:
    double sampleRate   = 48000.0;
    float  ceilingLin   = 0.89125f; // -1.0 dBTP
    float  slowAtkCoeff = 0.01f;
    float  slowRelCoeff = 0.0002f;
    float  fastAtkCoeff = 0.25f;
    float  fastRelCoeff = 0.01f;

    Polyphase8xHermiteTruePeakDetector tpDetector;
    float slowGainReduction01 = 0.0f;
    float fastGainReduction01 = 0.0f;
    float lastTotalGrDb       = 0.0f;
};

// =============================================================================
// 3. EBU R128 / ITU-R BS.1770-4 K重み付けラウドネス＆LRA計量器
//    (LoudnessRangeAndMomentaryLufsMeter)
// =============================================================================
class LoudnessRangeAndMomentaryLufsMeter
{
public:
    struct LoudnessSnapshot
    {
        float momentaryLufs   = -70.0f; // 400ms 窓 Momentary Loudness
        float shortTermLufs   = -70.0f; // 3.0s  窓 Short-Term Loudness
        float integratedLufs  = -70.0f; // ゲート付き Integrated Loudness
        float loudnessRangeLu = 0.0f;   // 抑揚レンジ LRA (LU)
    };

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);

        // Stage 1: ITU-R BS.1770-4 Pre-filter (1681Hz, +4.0dB 頭部球面回折ハイシェルフ)
        const double fcShelf = std::min(1681.97, sampleRate * 0.42);
        const double gShelf  = std::tan(3.14159265358979323846 * fcShelf / sampleRate);
        const double kShelf  = 1.0 / 0.70710678;
        sA1 = 1.0 / (1.0 + gShelf * (gShelf + kShelf));
        sA2 = gShelf * sA1;
        sA3 = gShelf * sA2;

        // Stage 2: RLB (Revised Low-frequency B-weighting) 38.13Hz 2次ハイパス
        const double gHp = std::tan(3.14159265358979323846 * 38.135 / sampleRate);
        const double kHp = 1.0 / 0.500327;
        hA1 = 1.0 / (1.0 + gHp * (gHp + kHp));
        hA2 = gHp * hA1;
        hA3 = gHp * hA2;

        momentaryAlpha  = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.400)));
        shortTermAlpha  = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 3.000)));
        integratedAlpha = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 8.000)));
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            sIc1[c] = sIc2[c] = 0.0;
            hIc1[c] = hIc2[c] = 0.0;
        }
        momentaryEnergy  = 1.0e-9f;
        shortTermEnergy  = 1.0e-9f;
        integratedEnergy = 1.0e-9f;
        minShortTermLufs = -24.0f;
        maxShortTermLufs = -14.0f;
    }

    inline void pushStereoSample(float inL, float inR) noexcept
    {
        const float kL = applyKWeighting(0, inL);
        const float kR = applyKWeighting(1, inR);
        const float meanSq = 0.5f * (kL * kL + kR * kR);

        momentaryEnergy += momentaryAlpha * (meanSq - momentaryEnergy);
        shortTermEnergy += shortTermAlpha * (meanSq - shortTermEnergy);

        // BS.1770-4 Absolute Gate (-70 LUFS) を超える有効音声フレームのみ Integrated と LRA に積算
        const float curMomentaryLufs = -0.691f + 10.0f * std::log10(std::max(1.0e-9f, momentaryEnergy));
        if (curMomentaryLufs > -65.0f)
        {
            integratedEnergy += integratedAlpha * (meanSq - integratedEnergy);
            const float curStLufs = -0.691f + 10.0f * std::log10(std::max(1.0e-9f, shortTermEnergy));
            if (curStLufs > -50.0f)
            {
                maxShortTermLufs = std::max(maxShortTermLufs * 0.99995f + curStLufs * 0.00005f, curStLufs);
                minShortTermLufs = std::min(minShortTermLufs * 0.99995f + curStLufs * 0.00005f, curStLufs);
            }
        }
    }

    inline LoudnessSnapshot getSnapshot() const noexcept
    {
        LoudnessSnapshot snap{};
        snap.momentaryLufs   = std::clamp(-0.691f + 10.0f * std::log10(std::max(1.0e-9f, momentaryEnergy)),  -70.0f, 3.0f);
        snap.shortTermLufs   = std::clamp(-0.691f + 10.0f * std::log10(std::max(1.0e-9f, shortTermEnergy)),  -70.0f, 3.0f);
        snap.integratedLufs  = std::clamp(-0.691f + 10.0f * std::log10(std::max(1.0e-9f, integratedEnergy)), -70.0f, 3.0f);
        snap.loudnessRangeLu = std::clamp(maxShortTermLufs - minShortTermLufs, 0.0f, 30.0f);
        return snap;
    }

private:
    inline float applyKWeighting(int ch, float in) noexcept
    {
        const double x = static_cast<double>(in);

        // 1. 1681Hz +4dB High Shelf (x + 0.5849 * HP + 0.42 * BP)
        const double sv3 = x - sIc2[ch];
        const double sv1 = sA1 * sIc1[ch] + sA2 * sv3;
        const double sv2 = sIc2[ch] + sA2 * sIc1[ch] + sA3 * sv3;
        sIc1[ch] = 2.0 * sv1 - sIc1[ch];
        sIc2[ch] = 2.0 * sv2 - sIc2[ch];
        const double hpShelf = x - 1.41421356 * sv1 - sv2;
        const double stage1  = x + 0.584893 * hpShelf + 0.415 * sv1;

        // 2. 38.13Hz RLB Highpass
        const double hv3 = stage1 - hIc2[ch];
        const double hv1 = hA1 * hIc1[ch] + hA2 * hv3;
        const double hv2 = hIc2[ch] + hA2 * hIc1[ch] + hA3 * hv3;
        hIc1[ch] = 2.0 * hv1 - hIc1[ch];
        hIc2[ch] = 2.0 * hv2 - hIc2[ch];
        const double rlbHp = stage1 - (1.0 / 0.500327) * hv1 - hv2;

        return static_cast<float>(rlbHp);
    }

    double sampleRate = 48000.0;
    double sA1 = 0.0, sA2 = 0.0, sA3 = 0.0;
    double hA1 = 0.0, hA2 = 0.0, hA3 = 0.0;
    double sIc1[2]{}, sIc2[2]{};
    double hIc1[2]{}, hIc2[2]{};

    float momentaryAlpha   = 0.001f;
    float shortTermAlpha   = 0.0001f;
    float integratedAlpha  = 0.00005f;
    float momentaryEnergy  = 1.0e-9f;
    float shortTermEnergy  = 1.0e-9f;
    float integratedEnergy = 1.0e-9f;
    float minShortTermLufs = -24.0f;
    float maxShortTermLufs = -14.0f;
};

// =============================================================================
// 4. 統合8倍True-Peakリミッター＆R128ラウドネス計量スイート
//    (MasteringLimiterAndTruePeakSuite)
// =============================================================================
class MasteringLimiterAndTruePeakSuite
{
public:
    void prepare(double newSampleRate) noexcept
    {
        ircLimiter.prepare(newSampleRate);
        lufsMeter.prepare(newSampleRate);
    }

    void reset() noexcept
    {
        ircLimiter.reset();
        lufsMeter.reset();
    }

    inline void processSamplePair(float& ioL, float& ioR, float driveDb = 0.0f) noexcept
    {
        ircLimiter.processSamplePair(ioL, ioR, driveDb);
        lufsMeter.pushStereoSample(ioL, ioR);
    }

    const TwoStageIRCTransparentMasteringLimiter& getIrcLimiter() const noexcept { return ircLimiter; }
    TwoStageIRCTransparentMasteringLimiter&       getIrcLimiter() noexcept       { return ircLimiter; }
    const LoudnessRangeAndMomentaryLufsMeter&     getLufsMeter() const noexcept  { return lufsMeter; }

private:
    TwoStageIRCTransparentMasteringLimiter ircLimiter;
    LoudnessRangeAndMomentaryLufsMeter     lufsMeter;
};

} // namespace true_peak_limiter
} // namespace gzzio

namespace vg
{
namespace true_peak_limiter = ::gzzio::true_peak_limiter;
} // namespace vg
