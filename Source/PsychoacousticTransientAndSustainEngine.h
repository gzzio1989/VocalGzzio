#pragma once

/**
 * @file PsychoacousticTransientAndSustainEngine.h
 * @brief VocalGzzio v5.5.0 レベル独立型ディファレンシャル・トランジェント造形器＆日本語子音オンセット明瞭化エンジン
 *
 * SPL Transient Designer の名原理である「レベル独立型・差動包絡線検出（Differential Envelope Technology）」を
 * ボーカルの「日本語・英語歌詞の子音立ち上がり（Consonant Attack）」と「語尾の余韻・ロングトーンの伸び（Vocal Sustain）」に
 * 特化チューニングしたモジュール。
 *
 * 収録クラス:
 *   1. DifferentialEnvelopeTransientDesigner
 *      - 高速アタック包絡と低速アタック包絡の比から「音の立ち上がり瞬間（Attack Phase）」を、
 *        高速リリース包絡と超低速リリース包絡の差から「余韻の減衰区間（Sustain Phase）」を抽出し、
 *        スレッショルド設定不要（ささやき声でも大声でも同じ効き方）で子音のキレとロングトーンの伸びを自在に制御。
 *
 *   2. ConsonantOnsetClarityDeMasker
 *      - 語頭の子音（k, t, p, s, sh, ch）が発음された最初の 28ms だけ 2.8kHz〜6.5kHz の明瞭度帯域を
 *        瞬時に +2.5dB リフトし、母音に入った瞬間にフラットへ戻すことで「耳に痛くないのに歌詞が一言一句ハッキリ聴こえる」処理を実現。
 *
 *   3. PsychoacousticTransientAndSustainSuite
 *      - 上記2モジュールを統合したトランジェント＆サステイン造形スイート。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace transient_sustain
{

// =============================================================================
// 1. 差動包絡線トランジェント＆サステイン・デザイナー (DifferentialEnvelopeTransientDesigner)
// =============================================================================
class DifferentialEnvelopeTransientDesigner
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        atkFast_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.0008 * sr_)));
        atkSlow_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.0180 * sr_)));
        relFast_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.0450 * sr_)));
        relSlow_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.3200 * sr_)));
        reset();
    }

    void reset() noexcept
    {
        envFastAtk_ = 1.0e-5f;
        envSlowAtk_ = 1.0e-5f;
        envFastRel_ = 1.0e-5f;
        envSlowRel_ = 1.0e-5f;
        lastGain_   = 1.0f;
    }

    // attackAmount: -1.0 .. +1.0, sustainAmount: -1.0 .. +1.0
    void processStereo(float& ioL, float& ioR, float attackAmount, float sustainAmount) noexcept
    {
        const float atkParam = std::clamp(attackAmount, -1.0f, 1.0f);
        const float susParam = std::clamp(sustainAmount, -1.0f, 1.0f);

        const float absIn = 0.5f * (std::fabs(ioL) + std::fabs(ioR));

        // 1. Attack 差動検出用デュアル包絡線
        envFastAtk_ += ((absIn > envFastAtk_) ? atkFast_ : relFast_) * (absIn - envFastAtk_);
        envSlowAtk_ += ((absIn > envSlowAtk_) ? atkSlow_ : relFast_) * (absIn - envSlowAtk_);

        // 2. Sustain 差動検出用デュアル包絡線
        envFastRel_ += ((absIn > envFastRel_) ? atkFast_ : relFast_) * (absIn - envFastRel_);
        envSlowRel_ += ((absIn > envSlowRel_) ? atkFast_ : relSlow_) * (absIn - envSlowRel_);

        // レベル不変の正規化差動シグナル（スレッショルド不要）
        const float atkDiff = std::max(0.0f, (envFastAtk_ - envSlowAtk_) / (envFastAtk_ + 1.0e-4f));
        const float susDiff = std::max(0.0f, (envSlowRel_ - envFastRel_) / (envSlowRel_ + 1.0e-4f));

        const float gainDb = (atkDiff * atkParam * 7.5f) + (susDiff * susParam * 9.0f);
        const float targetGain = std::pow(10.0f, std::clamp(gainDb, -12.0f, 12.0f) * 0.05f);
        lastGain_ += 0.15f * (targetGain - lastGain_);

        ioL *= lastGain_;
        ioR *= lastGain_;
    }

    float getCurrentGainDb() const noexcept
    {
        return 20.0f * std::log10(std::max(0.05f, lastGain_));
    }

private:
    double sr_         = 48000.0;
    float  atkFast_    = 0.1f;
    float  atkSlow_    = 0.01f;
    float  relFast_    = 0.005f;
    float  relSlow_    = 0.0005f;
    float  envFastAtk_ = 1.0e-5f;
    float  envSlowAtk_ = 1.0e-5f;
    float  envFastRel_ = 1.0e-5f;
    float  envSlowRel_ = 1.0e-5f;
    float  lastGain_   = 1.0f;
};

// =============================================================================
// 2. 子音オンセット明瞭化デマスカー (ConsonantOnsetClarityDeMasker)
// =============================================================================
class ConsonantOnsetClarityDeMasker
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        const double fc = std::min(4200.0, sr_ * 0.43);
        const double g  = std::tan(3.14159265358979323846 * fc / sr_);
        k_  = 1.0f / 1.25f;
        a1_ = static_cast<float>(1.0 / (1.0 + g * (g + k_)));
        a2_ = static_cast<float>(g) * a1_;
        a3_ = static_cast<float>(g) * a2_;

        fastAlpha_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.0012 * sr_)));
        slowAlpha_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.0280 * sr_)));
        reset();
    }

    void reset() noexcept
    {
        ic1L_ = ic2L_ = ic1R_ = ic2R_ = 0.0f;
        fastEnv_ = slowEnv_ = 0.0f;
    }

    void processStereo(float& ioL, float& ioR, float articulationBoost01) noexcept
    {
        const float amt = std::clamp(articulationBoost01, 0.0f, 1.0f);
        if (amt <= 1.0e-4f)
            return;

        // 4.2kHz 子音帯域の抽出
        const float v3L = ioL - ic2L_;
        const float v1L = a1_ * ic1L_ + a2_ * v3L;
        const float v2L = ic2L_ + a2_ * ic1L_ + a3_ * v3L;
        ic1L_ = 2.0f * v1L - ic1L_;
        ic2L_ = 2.0f * v2L - ic2L_;
        const float bpL = v1L * k_;

        const float v3R = ioR - ic2R_;
        const float v1R = a1_ * ic1R_ + a2_ * v3R;
        const float v2R = ic2R_ + a2_ * ic1R_ + a3_ * v3R;
        ic1R_ = 2.0f * v1R - ic1R_;
        ic2R_ = 2.0f * v2R - ic2R_;
        const float bpR = v1R * k_;

        const float bandAbs = 0.5f * (std::fabs(bpL) + std::fabs(bpR));
        fastEnv_ += fastAlpha_ * (bandAbs - fastEnv_);
        slowEnv_ += slowAlpha_ * (bandAbs - slowEnv_);

        // 子音の立ち上がり最初の 28ms だけ正の差分が生じる
        const float onsetRatio = std::clamp((fastEnv_ - slowEnv_) / (fastEnv_ + 1.0e-4f), 0.0f, 1.0f);
        const float lift = onsetRatio * amt * 0.48f;

        ioL += bpL * lift;
        ioR += bpR * lift;
    }

private:
    double sr_        = 48000.0;
    float  k_         = 0.8f;
    float  a1_        = 0.5f;
    float  a2_        = 0.1f;
    float  a3_        = 0.02f;
    float  fastAlpha_ = 0.1f;
    float  slowAlpha_ = 0.01f;
    float  ic1L_ = 0.0f, ic2L_ = 0.0f, ic1R_ = 0.0f, ic2R_ = 0.0f;
    float  fastEnv_ = 0.0f, slowEnv_ = 0.0f;
};

// =============================================================================
// 3. 統合トランジェント＆サステイン造形スイート (PsychoacousticTransientAndSustainSuite)
// =============================================================================
class PsychoacousticTransientAndSustainSuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        designer_.prepare(sampleRate);
        deMasker_.prepare(sampleRate);
    }

    void reset() noexcept
    {
        designer_.reset();
        deMasker_.reset();
    }

    void processStereoSample(float& ioL, float& ioR,
                             float attackPunch01,
                             float sustainBloom01,
                             float consonantClarity01) noexcept
    {
        designer_.processStereo(ioL, ioR, attackPunch01 * 0.5f, sustainBloom01 * 0.6f);
        deMasker_.processStereo(ioL, ioR, consonantClarity01);
    }

private:
    DifferentialEnvelopeTransientDesigner designer_;
    ConsonantOnsetClarityDeMasker         deMasker_;
};

} // namespace transient_sustain
} // namespace gzzio

namespace vg
{
namespace transient_sustain = ::gzzio::transient_sustain;
} // namespace vg
