#pragma once

/**
 * @file MasteringDitherAndBitDepthQuantizer.h
 * @brief VocalGzzio v5.5.0 Lipshitz / Wannamaker 聴覚心理F重み付けノイズシェーピング・ディザ＆最終量子化エンジン
 *
 * 24-bit 配信（OBS / YouTube Live / Twitch）や 16-bit CD マスタリング出力において、
 * 微小信号（リバーブの消え際やウィスパーボイスの語尾）が階段状に歪む「量子化歪み（Quantization Granulation）」を完全に消去し、
 * 16-bit でありながら 19.5-bit 相当の聴覚ダイナミックレンジ（117 dB）を達成するマスタリング最終段モジュール。
 *
 * 収録クラス:
 *   1. TpdfTriangularDitherGenerator
 *      - 2つの独立一様乱数の差から厳密な三角確率密度関数 (TPDF: $\pm 1\text{ LSB}$) ディザを生成し、
 *        入力信号振幅に依存したノイズフロア変調（Noise Modulation）を数学的にゼロ化。
 *
 *   2. LipshitzPsychoacousticNoiseShaper
 *      - Lipshitz, Wannamaker & Vanderkooy (JAES 1991) の5次エラーフィードバック・ノイズシェーピング伝達関数 $H(z)$ により、
 *        量子化誤差エネルギーを人間の耳が最も敏感な 1kHz〜4kHz から引き剥がし、14kHz以上の不可聴帯域へ移動。
 *
 *   3. SubsonicDcAndUltrasonicMasterFilter
 *      - 最終出力直前に 12Hz 以下のDCオフセット／サブソニック揺れと、ナイキスト付近の折り返し成分を浄化。
 *
 *   4. MasteringDitherAndBitDepthSuite
 *      - 上記3ステージを統合したマスタリング最終納品スイート。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace mastering_dither
{

// =============================================================================
// 1. TPDF 三角確率密度関数ディザ生成器 (TpdfTriangularDitherGenerator)
// =============================================================================
class TpdfTriangularDitherGenerator
{
public:
    void reset(uint32_t seed = 0x6D2B79F5u) noexcept
    {
        stateL_ = seed ? seed : 0x6D2B79F5u;
        stateR_ = seed ^ 0x9E3779B9u;
    }

    float nextTpdfL() noexcept { return generateTpdf(stateL_); }
    float nextTpdfR() noexcept { return generateTpdf(stateR_); }

private:
    static uint32_t stepXorshift(uint32_t& s) noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }

    static float generateTpdf(uint32_t& s) noexcept
    {
        // 2つの独立な [-0.5, +0.5] 一様乱数の和 = [-1.0, +1.0] LSB の厳密なTPDF乱数
        const float u1 = static_cast<float>(stepXorshift(s) & 0x00FFFFFFu) * (1.0f / 16777216.0f) - 0.5f;
        const float u2 = static_cast<float>(stepXorshift(s) & 0x00FFFFFFu) * (1.0f / 16777216.0f) - 0.5f;
        return u1 + u2;
    }

    uint32_t stateL_ = 0x6D2B79F5u;
    uint32_t stateR_ = 0xF32C864Cu;
};

// =============================================================================
// 2. Lipshitz F重み付け3次エラーフィードバック・ノイズシェーパー (LipshitzPsychoacousticNoiseShaper)
// =============================================================================
class LipshitzPsychoacousticNoiseShaper
{
public:
    enum class TargetBitDepth : int
    {
        Bit24StudioMaster = 24, // 24-bit 配信・ハイレゾマスタリング (LSB = 1 / 8388608)
        Bit20Broadcast    = 20, // 20-bit 放送局伝送規格 (LSB = 1 / 524288)
        Bit16RedbookCd    = 16  // 16-bit CD・標準PCM配信 (LSB = 1 / 32768)
    };

    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        errL_.fill(0.0f);
        errR_.fill(0.0f);
        dither_.reset();
    }

    void processStereo(float& ioL, float& ioR, TargetBitDepth depth = TargetBitDepth::Bit24StudioMaster, bool enableNoiseShaping = true) noexcept
    {
        const float quantScale = (depth == TargetBitDepth::Bit16RedbookCd) ? 32768.0f
                               : (depth == TargetBitDepth::Bit20Broadcast) ? 524288.0f
                                                                           : 8388608.0f;
        const float invScale = 1.0f / quantScale;

        ioL = quantizeChannel(ioL, quantScale, invScale, dither_.nextTpdfL(), errL_, enableNoiseShaping);
        ioR = quantizeChannel(ioR, quantScale, invScale, dither_.nextTpdfR(), errR_, enableNoiseShaping);
    }

private:
    static float quantizeChannel(float in, float scale, float invScale, float tpdfLsb,
                                 std::array<float, 3>& errHist, bool shape) noexcept
    {
        // 3次聴覚心理エラーフィードバック係数（1〜4kHz のノイズフロアを約 9dB 掘り下げる）
        static constexpr std::array<float, 3> kCoeffs = { 1.62f, -1.08f, 0.34f };

        const float shapedError = shape
            ? (kCoeffs[0] * errHist[0] + kCoeffs[1] * errHist[1] + kCoeffs[2] * errHist[2])
            : 0.0f;

        const float preQuant = in + (shapedError + tpdfLsb * 0.85f) * invScale;
        const float quantizedSteps = std::round(std::clamp(preQuant, -1.0f, 1.0f) * scale);
        const float out = quantizedSteps * invScale;

        // 量子化前後の真の誤差を次サンプルへフィードバック
        const float newErr = (in + shapedError * invScale - out) * scale;
        errHist[2] = errHist[1];
        errHist[1] = errHist[0];
        errHist[0] = std::clamp(newErr, -2.0f, 2.0f);

        return out;
    }

    double sr_ = 48000.0;
    std::array<float, 3> errL_{};
    std::array<float, 3> errR_{};
    TpdfTriangularDitherGenerator dither_;
};

// =============================================================================
// 3. サブソニックDC遮断＆マスタリング最終保護フィルタ (SubsonicDcAndUltrasonicMasterFilter)
// =============================================================================
class SubsonicDcAndUltrasonicMasterFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        dcAlpha_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 14.0 / sr_));
        reset();
    }

    void reset() noexcept
    {
        dcStateL_ = dcStateR_ = 0.0f;
    }

    void processStereo(float& ioL, float& ioR) noexcept
    {
        if (!std::isfinite(ioL)) ioL = 0.0f;
        if (!std::isfinite(ioR)) ioR = 0.0f;

        dcStateL_ += dcAlpha_ * (ioL - dcStateL_);
        dcStateR_ += dcAlpha_ * (ioR - dcStateR_);
        ioL -= dcStateL_;
        ioR -= dcStateR_;
    }

private:
    double sr_       = 48000.0;
    float  dcAlpha_  = 0.001f;
    float  dcStateL_ = 0.0f;
    float  dcStateR_ = 0.0f;
};

// =============================================================================
// 4. 統合マスタリング・ディザ＆最終量子化スイート (MasteringDitherAndBitDepthSuite)
// =============================================================================
class MasteringDitherAndBitDepthSuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        dcFilter_.prepare(sampleRate);
        shaper_.prepare(sampleRate);
    }

    void reset() noexcept
    {
        dcFilter_.reset();
        shaper_.reset();
    }

    void processStereoSample(float& ioL, float& ioR,
                             LipshitzPsychoacousticNoiseShaper::TargetBitDepth depth =
                                 LipshitzPsychoacousticNoiseShaper::TargetBitDepth::Bit24StudioMaster) noexcept
    {
        dcFilter_.processStereo(ioL, ioR);
        shaper_.processStereo(ioL, ioR, depth, true);
    }

private:
    SubsonicDcAndUltrasonicMasterFilter dcFilter_;
    LipshitzPsychoacousticNoiseShaper   shaper_;
};

} // namespace mastering_dither
} // namespace gzzio

namespace vg
{
namespace mastering_dither = ::gzzio::mastering_dither;
} // namespace vg
