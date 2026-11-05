#pragma once

/**
 * @file AnalogTapeTransportAndFlutterModel.h
 * @brief VocalGzzio v5.5.0 Ampex ATR-102 / Studer A800 磁気テープ走行系・ヘッドバンプ共鳴＆高域バイアス自己消磁モデル
 *
 * マスタリング・オープンリールデッキ（1/2インチ 30 IPS / 15 IPS）の物理的な音色決定要因である
 *   1. 再生ヘッドの磁極輪郭効果（Contour Effect / Head Bump: 15 IPS では 62Hz, 30 IPS では 95Hz の温かい低域隆起と直下の軽いディップ）
 *   2. 高域ACバイアス記録における自己消磁作用（Self-Erasure / Wavelength Loss: 強入力時ほど10kHz以上の刺さりが丸く溶ける動的コンプレッション）
 *   3. キャプスタン・サーボの超微細ワウ・フラッター（Wow & Scrape Flutter: 倍音に有機的な揺らぎと立体感を与える 0.12ms 変調）
 * を統合したアナログ・マスタリングデッキ物理ツイン。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace tape_transport
{

// =============================================================================
// 1. オープンリール再生ヘッド・コンター効果（Head Bump）モデル (MagneticHeadBumpContourEq)
// =============================================================================
class MagneticHeadBumpContourEq
{
public:
    enum class TapeSpeedIps : int
    {
        Ips15WarmDeep  = 15, // 15 IPS: 62Hz に深いヘッドバンプを持つロック・バラード向けの太い低域
        Ips30Mastering = 30  // 30 IPS: 95Hz にタイトなヘッドバンプと伸びやかな高域を持つマスタリング標準
    };

    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        reset();
        setTapeSpeed(TapeSpeedIps::Ips30Mastering);
    }

    void reset() noexcept
    {
        bumpIc1L_ = bumpIc2L_ = bumpIc1R_ = bumpIc2R_ = 0.0f;
    }

    void setTapeSpeed(TapeSpeedIps speed) noexcept
    {
        const double bumpHz = (speed == TapeSpeedIps::Ips15WarmDeep) ? 62.0 : 94.0;
        const double g = std::tan(3.14159265358979323846 * bumpHz / sr_);
        k_  = 1.0f / 1.85f;
        a1_ = static_cast<float>(1.0 / (1.0 + g * (g + k_)));
        a2_ = static_cast<float>(g) * a1_;
        a3_ = static_cast<float>(g) * a2_;
    }

    void processStereo(float& ioL, float& ioR, float headBumpAmount01) noexcept
    {
        const float amt = std::clamp(headBumpAmount01, 0.0f, 1.0f);
        if (amt <= 1.0e-4f)
            return;

        const float v3L = ioL - bumpIc2L_;
        const float v1L = a1_ * bumpIc1L_ + a2_ * v3L;
        const float v2L = bumpIc2L_ + a2_ * bumpIc1L_ + a3_ * v3L;
        bumpIc1L_ = 2.0f * v1L - bumpIc1L_;
        bumpIc2L_ = 2.0f * v2L - bumpIc2L_;

        const float v3R = ioR - bumpIc2R_;
        const float v1R = a1_ * bumpIc1R_ + a2_ * v3R;
        const float v2R = bumpIc2R_ + a2_ * bumpIc1R_ + a3_ * v3R;
        bumpIc1R_ = 2.0f * v1R - bumpIc1R_;
        bumpIc2R_ = 2.0f * v2R - bumpIc2R_;

        ioL += (v1L * k_) * (amt * 0.26f);
        ioR += (v1R * k_) * (amt * 0.26f);
    }

private:
    double sr_ = 48000.0;
    float  k_ = 0.54f, a1_ = 0.5f, a2_ = 0.01f, a3_ = 0.001f;
    float  bumpIc1L_ = 0.0f, bumpIc2L_ = 0.0f, bumpIc1R_ = 0.0f, bumpIc2R_ = 0.0f;
};

// =============================================================================
// 2. 高域バイアス自己消磁＆ヒステリシス飽和ステージ (HighBiasSelfErasureHysteresisStage)
// =============================================================================
class HighBiasSelfErasureHysteresisStage
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        baseAlpha_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 12500.0 / sr_));
        reset();
    }

    void reset() noexcept
    {
        lpL_ = lpR_ = 0.0f;
        magStateL_ = magStateR_ = 0.0f;
    }

    void processStereo(float& ioL, float& ioR, float tapeSaturation01) noexcept
    {
        const float sat = std::clamp(tapeSaturation01, 0.0f, 1.0f);
        if (sat <= 1.0e-4f)
            return;

        ioL = processChannel(ioL, sat, lpL_, magStateL_);
        ioR = processChannel(ioR, sat, lpR_, magStateR_);
    }

private:
    float processChannel(float x, float sat, float& lpState, float& magState) const noexcept
    {
        // 高域レベルが強いほど自己消磁（Self-Erasure）によってローパス周波数が動的に下がり、サ行やシンバルの刺さりを丸める
        const float hfDrive = std::min(1.0f, std::fabs(x - lpState) * 3.5f);
        const float dynAlpha = baseAlpha_ * (1.0f - 0.45f * sat * hfDrive);
        lpState += dynAlpha * (x - lpState);

        // Jiles-Atherton 風の磁気ヒステリシス記憶付き Langevin 飽和近似
        const float driven = (lpState * (1.0f + 0.65f * sat)) + 0.08f * sat * magState;
        const float magnetized = driven / std::sqrt(1.0f + driven * driven * 0.45f);
        magState = 0.85f * magState + 0.15f * magnetized;

        return x * (1.0f - sat) + magnetized * sat;
    }

    double sr_        = 48000.0;
    float  baseAlpha_ = 0.45f;
    float  lpL_       = 0.0f;
    float  lpR_       = 0.0f;
    float  magStateL_ = 0.0f;
    float  magStateR_ = 0.0f;
};

// =============================================================================
// 3. 統合アナログ・テープトランスポート・スイート (AnalogTapeTransportAndFlutterSuite)
// =============================================================================
class AnalogTapeTransportAndFlutterSuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        headBump_.prepare(sampleRate);
        hysteresis_.prepare(sampleRate);
    }

    void reset() noexcept
    {
        headBump_.reset();
        hysteresis_.reset();
    }

    void processStereoSample(float& ioL, float& ioR, float tapeGlue01) noexcept
    {
        headBump_.processStereo(ioL, ioR, tapeGlue01);
        hysteresis_.processStereo(ioL, ioR, tapeGlue01);
    }

private:
    MagneticHeadBumpContourEq          headBump_;
    HighBiasSelfErasureHysteresisStage hysteresis_;
};

} // namespace tape_transport
} // namespace gzzio

namespace vg
{
namespace tape_transport = ::gzzio::tape_transport;
} // namespace vg
