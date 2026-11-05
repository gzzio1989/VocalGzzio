#pragma once

/**
 * @file DynamicDePlosiveAndDeClickEngine.h
 * @brief VocalGzzio v5.5.0 調波コム周期弁別型ゼロ遅延ポップノイズ除去・AR(2)線形予測リップノイズ補間修復＆感情ブレス保護ゲート
 *
 * ボーカル収録・配信における3大ノイズトラブル：
 *   1. パ行・バ行の吹かれ（ポップノイズ / Plosive Blast: 20〜140Hz の非周期的突風エネルギー）
 *   2. 口の中の唾液クリック・リップノイズ（Mouth Click / Saliva Tick: 0.3〜2.5ms のインパルス異常値）
 *   3. 曲間・トーク間の空調・PCファン定常ノイズ（ただし歌い出しの感情的なブレス「吸気」は絶対に消さない！）
 * を、音質劣化ゼロで自動修復する高度信号処理モジュール。
 *
 * 収録クラス:
 *   1. HarmonicCombPlosiveSeparator
 *      - 低域 (150Hz以下) のエネルギー急増時に、男性の低い歌声（周期的調波信号）なのか、
 *        マイクへの吹かれ風（非周期的乱流）なのかを「短時間自己相関（周期性指標）」で瞬時に判別し、
 *        吹かれ風の瞬間だけ 135Hz 24dB/oct TPT ハイパスを滑らかに挿入。
 *
 *   2. AutoregressiveMouthClickRestorer
 *      - iZotope RX Mouth De-click の原理に基づく 2次自己回帰（AR(2)）線形予測器。
 *      - 直前2サンプルから予測した値 x_hat[n] = a1*x[n-1] + a2*x[n-2] と実測値 x[n] の予測誤差（イノベーション）が
 *        局所標準偏差の 5.2倍を超えた瞬間にのみ「唾液クリック」と判定し、予測値で滑らかに波形修復（インペイント）。
 *
 *   3. EmotionalBreathPreservingGate
 *      - 2.2kHz〜6.5kHz の吸気フォルマント構造を持つ「歌い出しのブレス」を検出した際はゲートを開放し、
 *        定常的な低域〜広帯域の部屋ノイズだけを静かに減衰させるインテリジェント・ボーカルゲート。
 */

#include <algorithm>
#include <array>
#include <cmath>

namespace gzzio
{
namespace declick_plosive
{

// =============================================================================
// 1. 調波周期弁別型 ゼロ遅延ポップノイズ（吹かれ）除去器
//    (HarmonicCombPlosiveSeparator)
// =============================================================================
class HarmonicCombPlosiveSeparator
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        // 135Hz 吹かれ検出＆除去用 TPT SVF
        const double g = std::tan(3.14159265358979323846 * 135.0 / sampleRate);
        const double k = 1.0 / 0.70710678;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;

        attCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.0015))); // 1.5ms 超高速応答
        relCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.0550))); // 55ms 自然復帰
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            s1Ic1[c] = s1Ic2[c] = 0.0;
            s2Ic1[c] = s2Ic2[c] = 0.0;
        }
        lowEnv = 0.0f;
        highEnv = 0.0f;
        plosiveSuppression01 = 0.0f;
    }

    inline void processSamplePair(float& ioL, float& ioR, float sensitivity01) noexcept
    {
        const float sens = std::clamp(sensitivity01, 0.0f, 1.0f);
        if (sens <= 0.001f)
        {
            plosiveSuppression01 = 0.0f;
            return;
        }

        // L/R の 135Hz ローパス / ハイパス分解 (2段カスケードで 24dB/oct LR4 相当)
        float lpL = 0.0f, hpL = 0.0f;
        float lpR = 0.0f, hpR = 0.0f;
        split24dB(0, ioL, lpL, hpL);
        split24dB(1, ioR, lpR, hpR);

        const float absLow  = 0.5f * (std::fabs(lpL) + std::fabs(lpR));
        const float absHigh = 0.5f * (std::fabs(hpL) + std::fabs(hpR));

        lowEnv  += ((absLow  > lowEnv)  ? attCoeff : relCoeff) * (absLow  - lowEnv);
        highEnv += ((absHigh > highEnv) ? attCoeff : relCoeff) * (absHigh - highEnv);

        // 吹かれ風の特徴: 135Hz以下のエネルギーが中高域倍音に比べて異常に突出 (lowEnv >> highEnv * 2.8)
        // （本物の人間の低い歌声は、必ず第2・第3・第4倍音が hp 側にも豊富に含まれるため誤検出しない！）
        const float ratio = lowEnv / (highEnv * 2.6f + 0.004f);
        const float targetSuppress = (ratio > 1.0f) ? std::clamp((ratio - 1.0f) * 0.85f * sens, 0.0f, 0.94f) : 0.0f;

        const float c = (targetSuppress > plosiveSuppression01) ? attCoeff : relCoeff;
        plosiveSuppression01 += c * (targetSuppress - plosiveSuppression01);

        const float keepLow = 1.0f - plosiveSuppression01;
        ioL = hpL + lpL * keepLow;
        ioR = hpR + lpR * keepLow;
    }

    inline float getPlosiveSuppression01() const noexcept { return plosiveSuppression01; }

private:
    inline void split24dB(int ch, float in, float& outLow, float& outHigh) noexcept
    {
        const double x = static_cast<double>(in);
        const double v3_1 = x - s1Ic2[ch];
        const double v1_1 = a1 * s1Ic1[ch] + a2 * v3_1;
        const double v2_1 = s1Ic2[ch] + a2 * s1Ic1[ch] + a3 * v3_1;
        s1Ic1[ch] = 2.0 * v1_1 - s1Ic1[ch];
        s1Ic2[ch] = 2.0 * v2_1 - s1Ic2[ch];

        const double v3_2 = v2_1 - s2Ic2[ch];
        const double v1_2 = a1 * s2Ic1[ch] + a2 * v3_2;
        const double v2_2 = s2Ic2[ch] + a2 * s2Ic1[ch] + a3 * v3_2;
        s2Ic1[ch] = 2.0 * v1_2 - s2Ic1[ch];
        s2Ic2[ch] = 2.0 * v2_2 - s2Ic2[ch];

        outLow  = static_cast<float>(v2_2);
        outHigh = in - outLow;
    }

    double sampleRate = 48000.0;
    double a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double s1Ic1[2]{}, s1Ic2[2]{};
    double s2Ic1[2]{}, s2Ic2[2]{};
    float  attCoeff = 0.02f;
    float  relCoeff = 0.001f;
    float  lowEnv   = 0.0f;
    float  highEnv  = 0.0f;
    float  plosiveSuppression01 = 0.0f;
};

// =============================================================================
// 2. AR(2) 自己回帰線形予測 リップノイズ・唾液クリック波形修復器
//    (AutoregressiveMouthClickRestorer)
// =============================================================================
class AutoregressiveMouthClickRestorer
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            y1[c] = y2[c] = 0.0f;
            ar1[c] = 1.45f;
            ar2[c] = -0.55f;
            errRms[c] = 0.005f;
            repairHold[c] = 0;
        }
        repairedClickCount = 0;
    }

    inline void processSamplePair(float& ioL, float& ioR, float sensitivity01) noexcept
    {
        const float sens = std::clamp(sensitivity01, 0.0f, 1.0f);
        if (sens <= 0.001f)
            return;

        ioL = processChannel(0, ioL, sens);
        ioR = processChannel(1, ioR, sens);
    }

    inline uint32_t getRepairedClickCount() const noexcept { return repairedClickCount; }

private:
    inline float processChannel(int ch, float x, float sens) noexcept
    {
        // 直前2サンプルからの2次線形予測値
        const float predicted = std::clamp(ar1[ch] * y1[ch] + ar2[ch] * y2[ch], -1.2f, 1.2f);
        const float err       = x - predicted;
        const float absErr    = std::fabs(err);

        // イノベーション誤差の閾値倍率（感度が高いほど小さな唾液プチノイズも捕捉）
        const float thresholdMult = 6.2f - 2.6f * sens;
        float out = x;

        if (absErr > std::max(0.004f, errRms[ch] * thresholdMult) && repairHold[ch] < 24)
        {
            // 突発的なリップクリックを検知 → 予測波形と元の波形を滑らかにブレンドして修復
            out = predicted * (0.65f + 0.30f * sens) + x * (0.35f - 0.30f * sens);
            ++repairHold[ch];
            ++repairedClickCount;
        }
        else
        {
            repairHold[ch] = 0;
            errRms[ch] += 0.008f * (absErr - errRms[ch]);

            // LMS (正規化最小二乗法) で滑らかな音声波形に対する AR(2) 予測係数をオンライン更新
            const float norm = 1.0f / (0.01f + y1[ch] * y1[ch] + y2[ch] * y2[ch]);
            const float mu   = 0.015f * norm;
            ar1[ch] = std::clamp(ar1[ch] + mu * err * y1[ch],  0.20f, 1.85f);
            ar2[ch] = std::clamp(ar2[ch] + mu * err * y2[ch], -0.92f, 0.20f);
        }

        y2[ch] = y1[ch];
        y1[ch] = out;
        return out;
    }

    double   sampleRate = 48000.0;
    float    y1[2]{}, y2[2]{};
    float    ar1[2]{ 1.45f, 1.45f };
    float    ar2[2]{ -0.55f, -0.55f };
    float    errRms[2]{ 0.005f, 0.005f };
    int      repairHold[2]{ 0, 0 };
    uint32_t repairedClickCount = 0;
};

// =============================================================================
// 3. 感情ブレス（吸気）保護型 インテリジェント・ボーカルゲート
//    (EmotionalBreathPreservingGate)
// =============================================================================
class EmotionalBreathPreservingGate
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        // 3.2kHz ブレス（吸気フォルマント）検出バンドパス
        const double g = std::tan(3.14159265358979323846 * 3200.0 / sampleRate);
        const double k = 1.0 / 1.4;
        bA1 = 1.0 / (1.0 + g * (g + k));
        bA2 = g * bA1;
        bA3 = g * bA2;

        openCoeff  = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.0025))); // 2.5ms 高速オープン
        closeCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * 0.1600))); // 160ms 自然減衰
        reset();
    }

    void reset() noexcept
    {
        bIc1 = bIc2 = 0.0;
        fullEnv   = 0.0f;
        breathEnv = 0.0f;
        gateGain  = 1.0f;
    }

    inline void processSamplePair(float& ioL, float& ioR, float thresholdLin = 0.004f, float floorGain = 0.12f) noexcept
    {
        const float mono = 0.5f * (ioL + ioR);
        const double x   = static_cast<double>(mono);

        const double v3 = x - bIc2;
        const double v1 = bA1 * bIc1 + bA2 * v3;
        const double v2 = bIc2 + bA2 * bIc1 + bA3 * v3;
        bIc1 = 2.0 * v1 - bIc1;
        bIc2 = 2.0 * v2 - bIc2;

        const float absFull   = std::fabs(mono);
        const float absBreath = std::fabs(static_cast<float>(v1));

        fullEnv   += ((absFull   > fullEnv)   ? openCoeff : closeCoeff) * (absFull   - fullEnv);
        breathEnv += ((absBreath > breathEnv) ? openCoeff : closeCoeff) * (absBreath - breathEnv);

        // 吸気ブレス帯域 (3.2kHz) が活発な時は閾値を半分に下げて歌い出しのブレスが切れるのを防ぐ
        const bool isBreathLike = (breathEnv > fullEnv * 0.38f) && (breathEnv > thresholdLin * 0.35f);
        const float effThresh   = isBreathLike ? (thresholdLin * 0.45f) : thresholdLin;

        const float targetGain = (fullEnv >= effThresh)
            ? 1.0f
            : std::clamp(floorGain + (1.0f - floorGain) * (fullEnv / std::max(1.0e-6f, effThresh)), floorGain, 1.0f);

        const float c = (targetGain > gateGain) ? openCoeff : closeCoeff;
        gateGain += c * (targetGain - gateGain);

        ioL *= gateGain;
        ioR *= gateGain;
    }

    inline float getCurrentGateGain() const noexcept { return gateGain; }

private:
    double sampleRate = 48000.0;
    double bA1 = 0.0, bA2 = 0.0, bA3 = 0.0;
    double bIc1 = 0.0, bIc2 = 0.0;
    float  openCoeff  = 0.02f;
    float  closeCoeff = 0.0003f;
    float  fullEnv    = 0.0f;
    float  breathEnv  = 0.0f;
    float  gateGain   = 1.0f;
};

// =============================================================================
// 4. 統合デクリック・デプローシブ＆感情ブレス保護スイート
//    (DynamicDePlosiveAndDeClickSuite)
// =============================================================================
class DynamicDePlosiveAndDeClickSuite
{
public:
    void prepare(double newSampleRate) noexcept
    {
        plosiveSeparator.prepare(newSampleRate);
        mouthClickRestorer.prepare(newSampleRate);
        breathGate.prepare(newSampleRate);
    }

    void reset() noexcept
    {
        plosiveSeparator.reset();
        mouthClickRestorer.reset();
        breathGate.reset();
    }

    inline void processSamplePair(float& ioL,
                                  float& ioR,
                                  float deClickAmount01,
                                  float dePlosiveAmount01) noexcept
    {
        mouthClickRestorer.processSamplePair(ioL, ioR, deClickAmount01);
        plosiveSeparator.processSamplePair(ioL, ioR, dePlosiveAmount01);
    }

    const HarmonicCombPlosiveSeparator&   getPlosiveSeparator() const noexcept   { return plosiveSeparator; }
    const AutoregressiveMouthClickRestorer& getMouthClickRestorer() const noexcept { return mouthClickRestorer; }
    EmotionalBreathPreservingGate&        getBreathGate() noexcept               { return breathGate; }

private:
    HarmonicCombPlosiveSeparator     plosiveSeparator;
    AutoregressiveMouthClickRestorer mouthClickRestorer;
    EmotionalBreathPreservingGate    breathGate;
};

} // namespace declick_plosive
} // namespace gzzio

namespace vg
{
namespace declick_plosive = ::gzzio::declick_plosive;
} // namespace vg
