#pragma once

/**
 * @file PsychoacousticMaskingAndVocalPocket.h
 * @brief VocalGzzio v5.5.0 Zwicker 臨界帯域（Bark Scale）同時マスキング解析＆ボーカル明瞭度ポケット自動確保エンジン
 *
 * Eberhard Zwicker & Hugo Fastl (*Psychoacoustics: Facts and Models*) の臨界帯域（Critical Band Rate / Bark Scale）および
 * 上方マスキング広がり関数（Upward Spread of Masking: 低域側スロープ $+27\text{ dB/Bark}$, 高域側スロープ $-10\text{ dB/Bark}$）をモデル化し、
 * カラオケ伴奏（BGM）の倍音にボーカルの母音・子音がどれだけ隠されているか（Audibility Margin in dB）を算出して、
 * マスキングされている帯域だけをピンポイントで救出する聴覚心理エンジン。
 *
 * 収録クラス:
 *   1. ZwickerBarkMaskingThresholdCalculator
 *      - 8つの代表Bark帯域（Bark 3〜Bark 21: 300Hz〜7500Hz）の励振レベルから、
 *        他方の帯域へ漏れ出すマスキング閾値曲線をリアルタイム計算。
 *
 *   2. VocalAudibilityMarginBooster
 *      - ボーカルの明瞭度核心帯域（Bark 11〜17 = 1.4kHz〜3.8kHz）が低中域の伴奏エネルギーにマスクされた際、
 *        マスキング深度に応じた最小限のダイナミック・リフト（最大 +3.2 dB）を付与。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace bark_masking
{

// =============================================================================
// 1. Zwicker 臨界帯域（Bark）マスキング閾値計算器 (ZwickerBarkMaskingThresholdCalculator)
// =============================================================================
class ZwickerBarkMaskingThresholdCalculator
{
public:
    static constexpr size_t kNumBarkBands = 8;

    // 周波数 (Hz) から Zwicker Bark 値 (0..24 Bark) への変換公式 (Traunmüller 1990)
    static float hzToBark(float freqHz) noexcept
    {
        const float f = std::max(20.0f, freqHz);
        return (26.81f * f) / (1960.0f + f) - 0.53f;
    }

    // 8代表帯域のエネルギーから、隣接帯域へのマスキング広がり（Upward Spread of Masking）を加味した実効マスキング床を計算
    static std::array<float, kNumBarkBands> computeSpreadMaskingFloor(
        const std::array<float, kNumBarkBands>& bandRms) noexcept
    {
        std::array<float, kNumBarkBands> floorOut{};
        // 低域の強いエネルギーは高域側へ緩やかな勾配 (-10 dB/Bark ≈ 0.316x per band step) でマスキングを広げる
        // 高域のエネルギーは低域側へ急峻な勾配 (+27 dB/Bark ≈ 0.045x per band step) でしか広がらない
        for (size_t target = 0; target < kNumBarkBands; ++target)
        {
            float sumMask = 0.0f;
            for (size_t masker = 0; masker < kNumBarkBands; ++masker)
            {
                if (masker == target)
                {
                    sumMask += bandRms[masker] * 0.50f;
                }
                else if (masker < target)
                {
                    // Upward masking spread (low frequency masking high frequency)
                    const int dist = static_cast<int>(target - masker);
                    sumMask += bandRms[masker] * std::pow(0.34f, static_cast<float>(dist));
                }
                else
                {
                    // Downward masking spread (high frequency masking low frequency)
                    const int dist = static_cast<int>(masker - target);
                    sumMask += bandRms[masker] * std::pow(0.08f, static_cast<float>(dist));
                }
            }
            floorOut[target] = sumMask;
        }
        return floorOut;
    }
};

// =============================================================================
// 2. ボーカル可聴マージン自動救出ブースター (VocalAudibilityMarginBooster)
// =============================================================================
class VocalAudibilityMarginBooster
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        lowMaskAlpha_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 420.0 / sr_));
        envAlpha_     = static_cast<float>(1.0 - std::exp(-1.0 / (0.025 * sr_)));

        const double fc = std::min(2450.0, sr_ * 0.43);
        const double g  = std::tan(3.14159265358979323846 * fc / sr_);
        k_  = 1.0f / 1.15f;
        a1_ = static_cast<float>(1.0 / (1.0 + g * (g + k_)));
        a2_ = static_cast<float>(g) * a1_;
        a3_ = static_cast<float>(g) * a2_;
        reset();
    }

    void reset() noexcept
    {
        lowLp_ = 0.0f;
        lowMaskEnv_ = 1.0e-4f;
        presenceEnv_ = 1.0e-4f;
        ic1L_ = ic2L_ = ic1R_ = ic2R_ = 0.0f;
        currentLiftGain_ = 0.0f;
    }

    void processStereo(float& ioL, float& ioR, float deMaskAmount01) noexcept
    {
        const float amt = std::clamp(deMaskAmount01, 0.0f, 1.0f);
        if (amt <= 1.0e-4f)
            return;

        const float mono = 0.5f * (ioL + ioR);
        lowLp_ += lowMaskAlpha_ * (mono - lowLp_);
        lowMaskEnv_ += envAlpha_ * (std::fabs(lowLp_) - lowMaskEnv_);

        // 2.45kHz ボーカル知覚コア帯域（Bark 14）の抽出
        const float v3L = ioL - ic2L_;
        const float v1L = a1_ * ic1L_ + a2_ * v3L;
        const float v2L = ic2L_ + a2_ * ic1L_ + a3_ * v3L;
        ic1L_ = 2.0f * v1L - ic1L_;
        ic2L_ = 2.0f * v2L - ic2L_;
        const float presL = v1L * k_;

        const float v3R = ioR - ic2R_;
        const float v1R = a1_ * ic1R_ + a2_ * v3R;
        const float v2R = ic2R_ + a2_ * ic1R_ + a3_ * v3R;
        ic1R_ = 2.0f * v1R - ic1R_;
        ic2R_ = 2.0f * v2R - ic2R_;
        const float presR = v1R * k_;

        const float presAbs = 0.5f * (std::fabs(presL) + std::fabs(presR));
        presenceEnv_ += envAlpha_ * (presAbs - presenceEnv_);

        // 低域マスカーが知覚コア帯域の2.5倍を超えているとき、上方マスキングから声を救出するリフトゲインを算出
        const float upwardMask = lowMaskEnv_ * 0.38f;
        float targetLift = 0.0f;
        if (upwardMask > presenceEnv_ && presenceEnv_ > 1.0e-4f)
        {
            const float deficit = std::min(1.0f, (upwardMask - presenceEnv_) / (upwardMask + 1.0e-4f));
            targetLift = deficit * 0.44f * amt;
        }
        currentLiftGain_ += 0.02f * (targetLift - currentLiftGain_);

        ioL += presL * currentLiftGain_;
        ioR += presR * currentLiftGain_;
    }

    float getAudibilityBoostDb() const noexcept
    {
        return 20.0f * std::log10(1.0f + currentLiftGain_);
    }

private:
    double sr_          = 48000.0;
    float  lowMaskAlpha_= 0.04f;
    float  envAlpha_    = 0.002f;
    float  k_ = 0.85f, a1_ = 0.5f, a2_ = 0.1f, a3_ = 0.02f;
    float  lowLp_       = 0.0f;
    float  lowMaskEnv_  = 1.0e-4f;
    float  presenceEnv_ = 1.0e-4f;
    float  ic1L_ = 0.0f, ic2L_ = 0.0f, ic1R_ = 0.0f, ic2R_ = 0.0f;
    float  currentLiftGain_ = 0.0f;
};

} // namespace bark_masking
} // namespace gzzio

namespace vg
{
namespace bark_masking = ::gzzio::bark_masking;
} // namespace vg
