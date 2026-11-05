#pragma once

/**
 * @file VocalDoublerAndMicroShiftArray.h
 * @brief VocalGzzio v5.5.0 アビーロード式ADT（自動ダブルトラッキング）＆ H3000風デュアル・マイクロピッチシフト配列
 *
 * 1966年に Abbey Road Studios の Ken Townsend が John Lennon のボーカルのために発明した
 * Studer J37 テープ・ヴァリスピード ADT（Artificial Double Tracking）と、
 * Eventide H910 / H3000 の名プリセット「Micro-Pitch Shift (+9 cents Left / -9 cents Right)」を融合したモジュール。
 *
 * 収録クラス:
 *   1. AbbeyRoadVarispeedTapeAdtEngine
 *      - 18ms〜34ms のテープ遅延ラインに対し、人間が手でヴァリスピード・オシレータを揺らしたような
 *        非周期・二周波合成ドリフト (0.37Hz + 0.91Hz) と、テープヘッドの高域ギャップ損失・ソフト飽和を付加。
 *
 *   2. DualWindowedMicroPitchShifter
 *      - ハニング窓クロスフェード付きデュアル・フェーザー回転遅延ラインにより、
 *        左チャンネルに -8.5 cents (11.5ms遅延)、右チャンネルに +8.5 cents (16.2ms遅延) の
 *        グリッチフリー・デチューンボイスを生成。モノラル折りたたみ時にもコムフィルタの深いノッチを作らない設計。
 *
 *   3. VocalDoublerAndMicroShiftSuite
 *      - ADT と Micro-Pitch Shift をシームレスにブレンドするボーカル・ダブリングスイート。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace vocal_doubler
{

// =============================================================================
// 1. Abbey Road J37 ヴァリスピード・テープADTエンジン (AbbeyRoadVarispeedTapeAdtEngine)
// =============================================================================
class AbbeyRoadVarispeedTapeAdtEngine
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        tapeAlpha_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 7800.0 / sr_));
        hpAlpha_   = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 145.0 / sr_));
        reset();
    }

    void reset() noexcept
    {
        bufL_.fill(0.0f);
        bufR_.fill(0.0f);
        writePos_ = 0;
        phase1_   = 0.0;
        phase2_   = 0.0;
        lpL_ = lpR_ = 0.0f;
        hpL_ = hpR_ = 0.0f;
    }

    void processStereo(float inL, float inR, float adtAmount01, float& outL, float& outR) noexcept
    {
        const float amt = std::clamp(adtAmount01, 0.0f, 1.0f);
        if (amt <= 1.0e-4f)
        {
            outL = inL;
            outR = inR;
            return;
        }

        bufL_[static_cast<size_t>(writePos_)] = inL;
        bufR_[static_cast<size_t>(writePos_)] = inR;

        // 非周期2トーン・ヴァリスピード揺らぎ (0.37Hz + 0.91Hz)
        phase1_ += 6.283185307179586 * 0.37 / sr_;
        phase2_ += 6.283185307179586 * 0.91 / sr_;
        if (phase1_ >= 6.283185307179586) phase1_ -= 6.283185307179586;
        if (phase2_ >= 6.283185307179586) phase2_ -= 6.283185307179586;

        const float driftL = 0.65f * static_cast<float>(std::sin(phase1_))
                           + 0.35f * static_cast<float>(std::cos(phase2_));
        const float driftR = 0.65f * static_cast<float>(std::cos(phase1_))
                           - 0.35f * static_cast<float>(std::sin(phase2_));

        // 中心遅延: L = 23.5ms, R = 28.2ms (±3.2ms ヴァリスピード変調)
        const float delaySamplesL = std::clamp(static_cast<float>((23.5f + 3.2f * driftL) * 0.001 * sr_),
                                               2.0f, static_cast<float>(kBufSize - 4));
        const float delaySamplesR = std::clamp(static_cast<float>((28.2f + 3.2f * driftR) * 0.001 * sr_),
                                               2.0f, static_cast<float>(kBufSize - 4));

        const float rawL = readHermite(bufL_, writePos_, delaySamplesL);
        const float rawR = readHermite(bufR_, writePos_, delaySamplesR);
        writePos_ = (writePos_ + 1) % kBufSize;

        // J37 テープの 145Hz ヘッドバンプ・ハイパス＆ 7.8kHz ロールオフ＋ソフト飽和
        hpL_ += hpAlpha_ * (rawL - hpL_);
        hpR_ += hpAlpha_ * (rawR - hpR_);
        const float bandL = rawL - hpL_;
        const float bandR = rawR - hpR_;

        lpL_ += tapeAlpha_ * (bandL - lpL_);
        lpR_ += tapeAlpha_ * (bandR - lpR_);

        const float tapeWetL = std::tanh(lpL_ * 1.25f) * 0.82f;
        const float tapeWetR = std::tanh(lpR_ * 1.25f) * 0.82f;

        outL = inL + tapeWetL * (amt * 0.38f);
        outR = inR + tapeWetR * (amt * 0.38f);
    }

private:
    static constexpr int kBufSize = 4096;

    static float readHermite(const std::array<float, kBufSize>& buf, int writePos, float delaySamples) noexcept
    {
        const float rF = static_cast<float>(writePos) - delaySamples;
        int i0 = static_cast<int>(std::floor(rF));
        const float frac = rF - static_cast<float>(i0);
        auto at = [&](int idx) noexcept -> float {
            const int wrapped = (idx % kBufSize + kBufSize) % kBufSize;
            return buf[static_cast<size_t>(wrapped)];
        };
        const float xm1 = at(i0 - 1);
        const float x0  = at(i0);
        const float x1  = at(i0 + 1);
        const float x2  = at(i0 + 2);
        const float c0  = x0;
        const float c1  = 0.5f * (x1 - xm1);
        const float c2  = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3  = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    double sr_ = 48000.0;
    std::array<float, kBufSize> bufL_{};
    std::array<float, kBufSize> bufR_{};
    int writePos_ = 0;
    double phase1_ = 0.0, phase2_ = 0.0;
    float tapeAlpha_ = 0.3f, hpAlpha_ = 0.02f;
    float lpL_ = 0.0f, lpR_ = 0.0f;
    float hpL_ = 0.0f, hpR_ = 0.0f;
};

// =============================================================================
// 2. H3000 スタイル・デュアル窓付きマイクロピッチシフター (DualWindowedMicroPitchShifter)
// =============================================================================
class DualWindowedMicroPitchShifter
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        delayBuf_.fill(0.0f);
        writePos_  = 0;
        phasorDown_ = 0.0;
        phasorUp_   = 0.25;
    }

    void processStereo(float inL, float inR, float detuneCents, float mix01, float& outL, float& outR) noexcept
    {
        const float mix = std::clamp(mix01, 0.0f, 1.0f);
        if (mix <= 1.0e-4f)
        {
            outL = inL;
            outR = inR;
            return;
        }

        const float monoIn = 0.5f * (inL + inR);
        delayBuf_[static_cast<size_t>(writePos_)] = monoIn;

        const float cents = std::clamp(detuneCents, 1.0f, 25.0f);
        const double ratioUp   = std::pow(2.0,  static_cast<double>(cents) / 1200.0);
        const double ratioDown = std::pow(2.0, -static_cast<double>(cents) / 1200.0);

        // 28ms のハニング窓クロスフェード・グレイン長
        const double windowSec = 0.028;
        const float  windowSamples = static_cast<float>(windowSec * sr_);
        const double rateDown = (1.0 - ratioDown) / windowSec; // 正方向の遅延増加 = ピッチ低下
        const double rateUp   = (1.0 - ratioUp)   / windowSec; // 負方向の遅延減少 = ピッチ上昇

        phasorDown_ += rateDown / sr_;
        if (phasorDown_ >= 1.0) phasorDown_ -= 1.0;
        if (phasorDown_ <  0.0) phasorDown_ += 1.0;

        phasorUp_ += rateUp / sr_;
        if (phasorUp_ >= 1.0) phasorUp_ -= 1.0;
        if (phasorUp_ <  0.0) phasorUp_ += 1.0;

        const float baseDelayL = static_cast<float>(0.0115 * sr_);
        const float baseDelayR = static_cast<float>(0.0162 * sr_);

        const float shiftedDownL = sampleDualGrain(writePos_, baseDelayL, windowSamples, static_cast<float>(phasorDown_));
        const float shiftedUpR   = sampleDualGrain(writePos_, baseDelayR, windowSamples, static_cast<float>(phasorUp_));

        writePos_ = (writePos_ + 1) % kBufSize;

        outL = inL + shiftedDownL * (mix * 0.36f);
        outR = inR + shiftedUpR   * (mix * 0.36f);
    }

private:
    static constexpr int kBufSize = 4096;

    float sampleDualGrain(int writePos, float baseDelay, float winSamples, float phase01) const noexcept
    {
        const float pA = phase01;
        const float pB = (phase01 + 0.5f >= 1.0f) ? (phase01 - 0.5f) : (phase01 + 0.5f);

        // sin^2 + cos^2 = 1.0 の完全エネルギー保存クロスフェード窓
        const float wA = std::sin(3.14159265358979323846f * pA);
        const float wB = std::sin(3.14159265358979323846f * pB);

        const float dA = std::clamp(baseDelay + pA * winSamples, 2.0f, static_cast<float>(kBufSize - 4));
        const float dB = std::clamp(baseDelay + pB * winSamples, 2.0f, static_cast<float>(kBufSize - 4));

        return readLinear(writePos, dA) * (wA * wA) + readLinear(writePos, dB) * (wB * wB);
    }

    float readLinear(int writePos, float delaySamples) const noexcept
    {
        const float rF = static_cast<float>(writePos) - delaySamples;
        int i0 = static_cast<int>(std::floor(rF));
        const float frac = rF - static_cast<float>(i0);
        i0 = (i0 % kBufSize + kBufSize) % kBufSize;
        const int i1 = (i0 + 1) % kBufSize;
        return delayBuf_[static_cast<size_t>(i0)] * (1.0f - frac)
             + delayBuf_[static_cast<size_t>(i1)] * frac;
    }

    double sr_ = 48000.0;
    std::array<float, kBufSize> delayBuf_{};
    int writePos_ = 0;
    double phasorDown_ = 0.0;
    double phasorUp_   = 0.25;
};

// =============================================================================
// 3. 統合ボーカルダブラー＆マイクロシフト・スイート (VocalDoublerAndMicroShiftSuite)
// =============================================================================
class VocalDoublerAndMicroShiftSuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        adt_.prepare(sampleRate);
        microShift_.prepare(sampleRate);
    }

    void reset() noexcept
    {
        adt_.reset();
        microShift_.reset();
    }

    void processStereoSample(float& ioL, float& ioR,
                             float adtDoubler01,
                             float microShiftWidth01) noexcept
    {
        float midL = ioL, midR = ioR;
        adt_.processStereo(ioL, ioR, adtDoubler01, midL, midR);
        microShift_.processStereo(midL, midR, 8.5f, microShiftWidth01, ioL, ioR);
    }

private:
    AbbeyRoadVarispeedTapeAdtEngine adt_;
    DualWindowedMicroPitchShifter   microShift_;
};

} // namespace vocal_doubler
} // namespace gzzio

namespace vg
{
namespace vocal_doubler = ::gzzio::vocal_doubler;
} // namespace vg
