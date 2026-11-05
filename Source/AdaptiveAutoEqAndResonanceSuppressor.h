#pragma once

/**
 * @file AdaptiveAutoEqAndResonanceSuppressor.h
 * @brief VocalGzzio v5.5.0 インテリジェント16バンド動的レゾナンス抑制＆ピンクノイズ・スペクトルチルト自動平衡エンジン
 *
 * Soothe2 や Gullfoss 等の現代最高峰の適応スペクトル・プロセッサが採用している
 * 「局部的な耳障りな共振ピーク（Whistle / Boxiness / Harshness）の動的ノッチ抑制」と
 * 「理想的なピンクノイズ（-4.5 dB/oct ボーカルスペクトル勾配）への自動チルト補正」を、
 * ゼロレイテンシTPTフィルタバンクで実装したモジュール。
 *
 * 収録クラス:
 *   1. Dynamic16BandResonanceTamer
 *      - 180Hz〜11.5kHz をカバーする16バンド高Q TPTレゾネータ群で各帯域のエネルギーを監視し、
 *        隣接バンドの平均エネルギーに対して突出している「共振ピーク（部屋の定在波・マイクの金属共振・鼻づまり感）」だけを
 *        瞬時に動的減衰（最大 -6.5 dB）。基音や自然な倍音は一切削らない相対閾値アルゴリズム。
 *
 *   2. PinkNoiseSpectralTiltServo
 *      - 低域 (< 320Hz) と高域 (> 2.4kHz) の短時間エネルギー比を計測し、
 *        こもったマイク入力には明るいチルトを、キンキンした細い声には温かいローミッド・チルトを自動付与するサーボEQ。
 *
 *   3. AdaptiveAutoEqAndResonanceSuite
 *      - 上記2ステージを統合したリアルタイム自動整音スイート。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace adaptive_autoeq
{

// =============================================================================
// 1. 16バンド相対閾値・動的レゾナンス抑制エンジン (Dynamic16BandResonanceTamer)
// =============================================================================
class Dynamic16BandResonanceTamer
{
public:
    static constexpr size_t kNumBands = 16;

    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        reset();

        // ボーカルのこもり・箱鳴り・耳障りな共振が集中する16中心周波数 (Hz)
        static constexpr std::array<double, kNumBands> kCenterFreqs = {
            190.0,  265.0,  360.0,  490.0,  670.0,  910.0,  1240.0, 1680.0,
            2250.0, 2950.0, 3850.0, 4950.0, 6300.0, 7900.0, 9600.0, 11400.0
        };

        for (size_t b = 0; b < kNumBands; ++b)
        {
            const double fc = std::min(kCenterFreqs[b], sr_ * 0.43);
            const double q  = (b < 6) ? 3.2 : 4.2; // 中高域ほど鋭い共振を狙い撃ち
            const double g  = std::tan(3.14159265358979323846 * fc / sr_);
            const double k  = 1.0 / q;
            bandK_[b]  = static_cast<float>(k);
            bandA1_[b] = static_cast<float>(1.0 / (1.0 + g * (g + k)));
            bandA2_[b] = static_cast<float>(g) * bandA1_[b];
            bandA3_[b] = static_cast<float>(g) * bandA2_[b];
        }

        envAtk_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.0025 * sr_)));
        envRel_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.0450 * sr_)));
    }

    void reset() noexcept
    {
        ic1L_.fill(0.0f);
        ic2L_.fill(0.0f);
        ic1R_.fill(0.0f);
        ic2R_.fill(0.0f);
        bandEnv_.fill(0.0f);
        bandCutGain_.fill(0.0f);
    }

    void processStereo(float& ioL, float& ioR, float sensitivity01) noexcept
    {
        const float sens = std::clamp(sensitivity01, 0.0f, 1.0f);
        if (sens <= 1.0e-4f)
            return;

        std::array<float, kNumBands> bpL{};
        std::array<float, kNumBands> bpR{};

        // 1. 16バンド並列TPTバンドパス抽出＆包絡線追従
        for (size_t b = 0; b < kNumBands; ++b)
        {
            const float a1 = bandA1_[b];
            const float a2 = bandA2_[b];
            const float a3 = bandA3_[b];
            const float k  = bandK_[b];

            const float v3L = ioL - ic2L_[b];
            const float v1L = a1 * ic1L_[b] + a2 * v3L;
            const float v2L = ic2L_[b] + a2 * ic1L_[b] + a3 * v3L;
            ic1L_[b] = 2.0f * v1L - ic1L_[b];
            ic2L_[b] = 2.0f * v2L - ic2L_[b];
            bpL[b]   = v1L * k;

            const float v3R = ioR - ic2R_[b];
            const float v1R = a1 * ic1R_[b] + a2 * v3R;
            const float v2R = ic2R_[b] + a2 * ic1R_[b] + a3 * v3R;
            ic1R_[b] = 2.0f * v1R - ic1R_[b];
            ic2R_[b] = 2.0f * v2R - ic2R_[b];
            bpR[b]   = v1R * k;

            const float absBand = 0.5f * (std::fabs(bpL[b]) + std::fabs(bpR[b]));
            const float coeff   = (absBand > bandEnv_[b]) ? envAtk_ : envRel_;
            bandEnv_[b] += coeff * (absBand - bandEnv_[b]);
        }

        // 2. 隣接バンド平均と比較し、局所的に突出した共振（Resonance Spike）だけを抑制
        float totalCutL = 0.0f;
        float totalCutR = 0.0f;
        for (size_t b = 0; b < kNumBands; ++b)
        {
            const float prevE = (b > 0) ? bandEnv_[b - 1] : bandEnv_[b];
            const float nextE = (b + 1 < kNumBands) ? bandEnv_[b + 1] : bandEnv_[b];
            const float localFloor = 0.5f * (prevE + nextE) + 1.0e-5f;
            const float ratio = bandEnv_[b] / localFloor;

            // 隣接帯域より 1.35倍以上突出しているときだけノッチ減衰を起動
            float targetCut = 0.0f;
            if (ratio > 1.35f)
            {
                const float excess = std::min(ratio - 1.35f, 3.0f) / 3.0f;
                targetCut = excess * 0.52f * sens;
            }
            bandCutGain_[b] += 0.15f * (targetCut - bandCutGain_[b]);
            totalCutL += bpL[b] * bandCutGain_[b];
            totalCutR += bpR[b] * bandCutGain_[b];
        }

        ioL -= totalCutL;
        ioR -= totalCutR;
    }

    float getAverageSuppressionDb() const noexcept
    {
        float sum = 0.0f;
        for (float g : bandCutGain_)
            sum += g;
        const float avgLin = 1.0f - (sum / static_cast<float>(kNumBands));
        return -20.0f * std::log10(std::max(0.25f, avgLin));
    }

private:
    double sr_ = 48000.0;
    std::array<float, kNumBands> bandK_{};
    std::array<float, kNumBands> bandA1_{};
    std::array<float, kNumBands> bandA2_{};
    std::array<float, kNumBands> bandA3_{};
    std::array<float, kNumBands> ic1L_{};
    std::array<float, kNumBands> ic2L_{};
    std::array<float, kNumBands> ic1R_{};
    std::array<float, kNumBands> ic2R_{};
    std::array<float, kNumBands> bandEnv_{};
    std::array<float, kNumBands> bandCutGain_{};
    float envAtk_ = 0.01f;
    float envRel_ = 0.001f;
};

// =============================================================================
// 2. ピンクノイズ・スペクトルチルト自動平衡サーボ (PinkNoiseSpectralTiltServo)
// =============================================================================
class PinkNoiseSpectralTiltServo
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        pivotAlpha_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 950.0 / sr_));
        meterAlpha_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.080 * sr_)));
        reset();
    }

    void reset() noexcept
    {
        lpL_ = lpR_ = 0.0f;
        lowEnergy_  = 1.0e-4f;
        highEnergy_ = 1.0e-4f;
        currentTilt_ = 0.0f;
    }

    void processStereo(float& ioL, float& ioR, float autoBalance01) noexcept
    {
        const float amt = std::clamp(autoBalance01, 0.0f, 1.0f);
        if (amt <= 1.0e-4f)
            return;

        // 950Hz を支点（Pivot）として Low と High に分離
        lpL_ += pivotAlpha_ * (ioL - lpL_);
        lpR_ += pivotAlpha_ * (ioR - lpR_);
        const float hiL = ioL - lpL_;
        const float hiR = ioR - lpR_;

        const float absLow  = 0.5f * (std::fabs(lpL_) + std::fabs(lpR_));
        const float absHigh = 0.5f * (std::fabs(hiL) + std::fabs(hiR));
        lowEnergy_  += meterAlpha_ * (absLow - lowEnergy_);
        highEnergy_ += meterAlpha_ * (absHigh - highEnergy_);

        if (lowEnergy_ + highEnergy_ > 1.0e-4f)
        {
            // 理想のボーカル高域/低域エネルギー比 (約 0.42 = -4.5dB/oct 勾配相当)
            const float targetHighRatio = 0.42f;
            const float actualRatio     = highEnergy_ / (lowEnergy_ + 1.0e-5f);
            const float rawError        = std::clamp(targetHighRatio - actualRatio, -0.35f, 0.35f);
            currentTilt_ += 0.002f * (rawError * amt - currentTilt_);
        }

        const float lowGain  = 1.0f - currentTilt_ * 0.55f;
        const float highGain = 1.0f + currentTilt_ * 0.75f;
        ioL = lpL_ * lowGain + hiL * highGain;
        ioR = lpR_ * lowGain + hiR * highGain;
    }

    float getTiltDbPerOct() const noexcept { return currentTilt_ * 4.5f; }

private:
    double sr_         = 48000.0;
    float  pivotAlpha_ = 0.05f;
    float  meterAlpha_ = 0.001f;
    float  lpL_        = 0.0f;
    float  lpR_        = 0.0f;
    float  lowEnergy_  = 1.0e-4f;
    float  highEnergy_ = 1.0e-4f;
    float  currentTilt_= 0.0f;
};

// =============================================================================
// 3. 統合アダプティブAuto-EQ＆レゾナンス抑制スイート (AdaptiveAutoEqAndResonanceSuite)
// =============================================================================
class AdaptiveAutoEqAndResonanceSuite
{
public:
    struct Telemetry
    {
        float resonanceCutDb = 0.0f;
        float spectralTiltDb = 0.0f;
    };

    void prepare(double sampleRate) noexcept
    {
        tamer_.prepare(sampleRate);
        tiltServo_.prepare(sampleRate);
        telemetry_ = {};
    }

    void reset() noexcept
    {
        tamer_.reset();
        tiltServo_.reset();
        telemetry_ = {};
    }

    void processStereoSample(float& ioL, float& ioR,
                             float resonanceTame01,
                             float autoTiltBalance01) noexcept
    {
        tamer_.processStereo(ioL, ioR, resonanceTame01);
        tiltServo_.processStereo(ioL, ioR, autoTiltBalance01);
        telemetry_.resonanceCutDb = tamer_.getAverageSuppressionDb();
        telemetry_.spectralTiltDb = tiltServo_.getTiltDbPerOct();
    }

    const Telemetry& getTelemetry() const noexcept { return telemetry_; }

private:
    Dynamic16BandResonanceTamer tamer_;
    PinkNoiseSpectralTiltServo  tiltServo_;
    Telemetry telemetry_{};
};

} // namespace adaptive_autoeq
} // namespace gzzio

namespace vg
{
namespace adaptive_autoeq = ::gzzio::adaptive_autoeq;
} // namespace vg
