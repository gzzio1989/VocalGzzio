#pragma once

/**
 * @file MasteringStereoFieldAndPhaseDoctor.h
 * @brief VocalGzzio v5.5.0 マスタリング・ステレオ音場位相ドクター＆完全モノラル互換オールパス・ワイドナー
 *
 * スマートフォン・Bluetoothスピーカー等のモノラル再生環境でボーカルやBGMが消えてしまう「位相キャンセレーション事故」を
 * リアルタイムに監視・自動修復するとともに、モノラル折りたたみ時に100%キャンセレーションゼロ（完全相殺で元に戻る）の
 * 4段オールパス直交位相ネットワークによる「Mono-Compatible Pseudo-Stereo Widener」を提供するモジュール。
 *
 * 収録クラス:
 *   1. PearsonPhaseCorrelationAndGoniometerTracker
 *      - L/R チャンネルのピアソン相関係数 $\rho \in [-1, +1]$、Mid/Side エネルギー比、方位角（Azimuth Tilt）を
 *        30ms 窓でリアルタイム計測。$\rho < 0$ の逆相状態を検知すると Side 成分を自動減衰してモノラル崩壊を防止。
 *
 *   2. MonoCompatibleAllpassHilbertWidener
 *      - Mid 信号に4段直列1次オールパスフィルタ（180Hz / 640Hz / 2150Hz / 6800Hz）を通した無相関位相信号 $D(n)$ を生成し、
 *        $L' = L + \alpha D(n), \; R' = R - \alpha D(n)$ として注入。
 *      - モノラル合成 $(L' + R') / 2 = (L + R) / 2$ において $+\alpha D(n)$ と $-\alpha D(n)$ が数学的に完全消滅するため、
 *        スマホ再生でのコムフィルタ劣化が完全にゼロのままステレオの広がりを創出。
 *
 *   3. MasteringStereoFieldAndPhaseSuite
 *      - 位相ドクターと完全モノラル互換ワイドナーを統合したマスタリング・ステレオスイート。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace phase_doctor
{

// =============================================================================
// 1. ピアソン位相相関＆ゴニオメータ・トラッカー (PearsonPhaseCorrelationAndGoniometerTracker)
// =============================================================================
class PearsonPhaseCorrelationAndGoniometerTracker
{
public:
    struct PhaseMetrics
    {
        float correlationNeg1To1 = 1.0f; // +1.0 = 完全同相, 0.0 = 90度直交ワイド, -1.0 = 完全逆相危険領域
        float sideToMidRatio01   = 0.0f; // Side / (Mid + Side)
        float azimuthBalance     = 0.0f; // -1.0 (Left) .. +1.0 (Right)
        float safeSideScale      = 1.0f; // 逆相時に自動適用される Side 保護ゲイン
    };

    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        smoothAlpha_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.035 * sr_)));
        reset();
    }

    void reset() noexcept
    {
        covLR_  = 1.0e-6f;
        varL_   = 1.0e-6f;
        varR_   = 1.0e-6f;
        midPow_ = 1.0e-6f;
        sidePow_= 0.0f;
        safeSideGain_ = 1.0f;
        metrics_ = {};
    }

    void analyzeAndProtectStereo(float& ioL, float& ioR, bool enablePhaseRescue = true) noexcept
    {
        const float l2 = ioL * ioL;
        const float r2 = ioR * ioR;
        const float lr = ioL * ioR;

        varL_  += smoothAlpha_ * (l2 - varL_);
        varR_  += smoothAlpha_ * (r2 - varR_);
        covLR_ += smoothAlpha_ * (lr - covLR_);

        const float denom = std::sqrt(std::max(1.0e-12f, varL_ * varR_));
        const float corr  = std::clamp(covLR_ / denom, -1.0f, 1.0f);

        const float mid  = 0.5f * (ioL + ioR);
        const float side = 0.5f * (ioL - ioR);
        midPow_  += smoothAlpha_ * (mid * mid - midPow_);
        sidePow_ += smoothAlpha_ * (side * side - sidePow_);

        // 相関係数が -0.15 を下回る逆相領域に入った場合、Side成分を滑らかに絞ってモノラル消失を防ぐ
        float targetSideScale = 1.0f;
        if (enablePhaseRescue && corr < -0.15f)
        {
            const float severity = std::clamp((-0.15f - corr) / 0.85f, 0.0f, 1.0f);
            targetSideScale = 1.0f - 0.65f * severity;
        }
        safeSideGain_ += 0.01f * (targetSideScale - safeSideGain_);

        if (enablePhaseRescue && safeSideGain_ < 0.999f)
        {
            const float rescuedSide = side * safeSideGain_;
            ioL = mid + rescuedSide;
            ioR = mid - rescuedSide;
        }

        metrics_.correlationNeg1To1 = corr;
        metrics_.sideToMidRatio01   = sidePow_ / (midPow_ + sidePow_ + 1.0e-9f);
        metrics_.azimuthBalance     = (varR_ - varL_) / (varR_ + varL_ + 1.0e-9f);
        metrics_.safeSideScale      = safeSideGain_;
    }

    const PhaseMetrics& getMetrics() const noexcept { return metrics_; }

private:
    double sr_          = 48000.0;
    float  smoothAlpha_ = 0.001f;
    float  covLR_       = 1.0e-6f;
    float  varL_        = 1.0e-6f;
    float  varR_        = 1.0e-6f;
    float  midPow_      = 1.0e-6f;
    float  sidePow_     = 0.0f;
    float  safeSideGain_= 1.0f;
    PhaseMetrics metrics_{};
};

// =============================================================================
// 2. 完全モノラル互換オールパス・ヒルベルトワイドナー (MonoCompatibleAllpassHilbertWidener)
// =============================================================================
class MonoCompatibleAllpassHilbertWidener
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        static constexpr std::array<double, 4> kPoleFreqs = { 185.0, 640.0, 2150.0, 6800.0 };
        for (size_t i = 0; i < 4; ++i)
        {
            const double t = std::tan(3.14159265358979323846 * std::min(kPoleFreqs[i], sr_ * 0.44) / sr_);
            apCoeff_[i] = static_cast<float>((t - 1.0) / (t + 1.0));
        }
        hpAlpha_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 160.0 / sr_));
        reset();
    }

    void reset() noexcept
    {
        apX1_.fill(0.0f);
        apY1_.fill(0.0f);
        hpState_ = 0.0f;
    }

    void processStereo(float& ioL, float& ioR, float monoSafeWidth01) noexcept
    {
        const float width = std::clamp(monoSafeWidth01, 0.0f, 1.0f);
        if (width <= 1.0e-4f)
            return;

        const float mid = 0.5f * (ioL + ioR);

        // 160Hz 以下の重低域はセンターに固定し、中高域だけを4段オールパスに通して直交Side信号を生成
        hpState_ += hpAlpha_ * (mid - hpState_);
        float x = mid - hpState_;

        for (size_t i = 0; i < 4; ++i)
        {
            const float c = apCoeff_[i];
            const float y = c * x + apX1_[i] - c * apY1_[i];
            apX1_[i] = x;
            apY1_[i] = y;
            x = y;
        }

        const float inject = x * (width * 0.28f);
        // L に +inject, R に -inject を加えるため、モノラル合算 (L+R) では数学的に誤差ゼロで完全相殺！
        ioL += inject;
        ioR -= inject;
    }

private:
    double sr_ = 48000.0;
    std::array<float, 4> apCoeff_{};
    std::array<float, 4> apX1_{};
    std::array<float, 4> apY1_{};
    float hpAlpha_ = 0.02f;
    float hpState_ = 0.0f;
};

// =============================================================================
// 3. 統合マスタリング・ステレオ位相ドクター・スイート (MasteringStereoFieldAndPhaseSuite)
// =============================================================================
class MasteringStereoFieldAndPhaseSuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        widener_.prepare(sampleRate);
        doctor_.prepare(sampleRate);
    }

    void reset() noexcept
    {
        widener_.reset();
        doctor_.reset();
    }

    void processStereoSample(float& ioL, float& ioR, float monoSafeWidener01, bool enablePhaseRescue = true) noexcept
    {
        widener_.processStereo(ioL, ioR, monoSafeWidener01);
        doctor_.analyzeAndProtectStereo(ioL, ioR, enablePhaseRescue);
    }

    const PearsonPhaseCorrelationAndGoniometerTracker::PhaseMetrics& getMetrics() const noexcept
    {
        return doctor_.getMetrics();
    }

private:
    MonoCompatibleAllpassHilbertWidener          widener_;
    PearsonPhaseCorrelationAndGoniometerTracker  doctor_;
};

} // namespace phase_doctor
} // namespace gzzio

namespace vg
{
namespace phase_doctor = ::gzzio::phase_doctor;
} // namespace vg
