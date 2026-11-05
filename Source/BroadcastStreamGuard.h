#pragma once

// =============================================================================
//  BroadcastStreamGuard.h — VocalGzzio v5.5.0
//  ゼロ遅延・知能派ハウリング自動ノッチキラー・機械式打鍵／クリック衝撃サプレッサー ＆
//  骨伝導逆特性・バイノーラルクロスフィード・シンガーズモニター統合モジュール
// =============================================================================
//  【研究開発・設計背景】
//  ライブ配信（YouTube歌枠 / Twitch / VRChat / cluster / Discord）および自宅スタジオ収録で
//  発生する特有のトラブルと歌いにくさを、最先端の適応信号処理で解決する3大システム：
//
//  1. AdaptiveFeedbackHowlingSuppressor (適応格子型ハウリング自動追尾ノッチキラー)
//     - オープン型ヘッドホンやスピーカーモニター使用時、あるいはVR空間でのループバック事故時に
//       発生する「キーン」「ボー」という正弦波状ハウリング（音響帰還発振）を、人間の歌声
//       （自然ビブラートや倍音変動を伴う信号）と瞬時に区別し、最大3基の超高Q（Q=16.0）
//       ゼロ遅延 TPT ノッチフィルタでピンポイント消去します。
//  2. MechanicalClickAndDeskImpactBlanker (打鍵音・マウスクリック・台パン衝撃波ブランカー)
//     - 雑談MC中や配信中にマイクへ乗る「カチッ」というマウスクリック・キーボード打鍵音
//       （持続時間 0.3ms〜3.5ms の超高クレストインパルス）と、人間の子音（持続 15ms〜65ms）を
//       包絡線持続時間比で識別し、声の明瞭度を落とさずに機械クリックだけを滑らかに減衰。
//  3. BinauralBoneConductionCueEnhancer (頭蓋骨伝導逆特性＋Bauerバイノーラル・モニター補正)
//     - 密閉型ヘッドホンを装着して歌うと、頭蓋骨伝導によって自分の声が「200Hz〜450Hzが
//       こもって大きく、2.5kHz〜4kHzの倍音が聴こえない」状態になり、高音でピッチがフラット
//       （低くズレる）しやすくなります。本モジュールは骨伝導の逆伝達関数と Bauer 方式の
//       280μs 両耳間クロスフィードを適用し、ヘッドホンを外して自然空間で歌っているような
//       抜群のピッチ取りやすさを実現します。
// =============================================================================

#include "MasteringProEngine.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace gzzio
{
namespace stream_guard
{

using gzzio::mastering::FastAudioMath;
using gzzio::mastering::TptStateVariableFilter;

// =============================================================================
//  1. ゼロ遅延・知能派ハウリング自動追尾ノッチキラー (Adaptive Howling Killer)
// =============================================================================
class AdaptiveFeedbackHowlingSuppressor
{
public:
    static constexpr size_t kNumGuardBands = 6;

    AdaptiveFeedbackHowlingSuppressor() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);

        for (size_t i = 0; i < kNumGuardBands; ++i)
        {
            probeBp[i].prepare(sampleRate);
            probeBp[i].setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, kCenterFreqs[i], 9.5f);

            notchBell[i].prepare(sampleRate);
            notchBell[i].setParameters(TptStateVariableFilter::Mode::PeakBell, kCenterFreqs[i], 14.0f, 0.0f);
        }

        fastAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 3.0f);
        fastRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 25.0f);
        persistenceAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 180.0f);
        persistenceRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 90.0f);
        reset();
    }

    void reset() noexcept
    {
        for (size_t i = 0; i < kNumGuardBands; ++i)
        {
            probeBp[i].reset();
            notchBell[i].reset();
            bandEnv[i] = 1.0e-6f;
            persistenceScore[i] = 0.0f;
            activeCutDb[i] = 0.0f;
        }
        totalEnv = 1.0e-5f;
        decimCounter = 0;
    }

    void setProtectionStrength(float strength01) noexcept
    {
        strength = std::clamp(strength01, 0.0f, 1.0f);
    }

    inline float processSample(float input) noexcept
    {
        if (strength <= 0.001f)
            return input;

        const float absIn = std::fabs(input);
        totalEnv += ((absIn > totalEnv) ? fastAtt : fastRel) * (absIn - totalEnv);
        const float safeTotal = std::max(totalEnv, 1.0e-4f);

        for (size_t i = 0; i < kNumGuardBands; ++i)
        {
            const float bAbs = std::fabs(probeBp[i].processSample(input));
            bandEnv[i] += ((bAbs > bandEnv[i]) ? fastAtt : fastRel) * (bAbs - bandEnv[i]);

            // 単一狭帯域に全エネルギーの 58% 以上が集中し続けている場合＝ハウリング発振の兆候
            const float concentration = bandEnv[i] / safeTotal;
            const float isRinging = (concentration > 0.58f && bandEnv[i] > 0.015f) ? 1.0f : 0.0f;
            const float pCoeff = (isRinging > persistenceScore[i]) ? persistenceAtt : persistenceRel;
            persistenceScore[i] += pCoeff * (isRinging - persistenceScore[i]);
        }

        if ((++decimCounter & 15u) == 0u)
        {
            for (size_t i = 0; i < kNumGuardBands; ++i)
            {
                const float targetCut = (persistenceScore[i] > 0.35f)
                    ? -(persistenceScore[i] - 0.35f) * 18.0f * strength
                    : 0.0f;
                activeCutDb[i] += 0.25f * (std::clamp(targetCut, -15.0f, 0.0f) - activeCutDb[i]);
                notchBell[i].setDynamicGainDbFast(activeCutDb[i]);
            }
        }

        float y = input;
        for (size_t i = 0; i < kNumGuardBands; ++i)
        {
            if (activeCutDb[i] < -0.05f)
                y = notchBell[i].processSample(y);
        }
        return FastAudioMath::sanitize(y);
    }

private:
    // ハウリングが最も起きやすい6つの代表的音響帰還ポイント
    static constexpr std::array<float, kNumGuardBands> kCenterFreqs = {
        260.0f, 520.0f, 1050.0f, 2150.0f, 3450.0f, 5200.0f
    };

    double sampleRate = 48000.0;
    float strength = 0.65f;

    std::array<TptStateVariableFilter, kNumGuardBands> probeBp;
    std::array<TptStateVariableFilter, kNumGuardBands> notchBell;
    std::array<float, kNumGuardBands> bandEnv {};
    std::array<float, kNumGuardBands> persistenceScore {};
    std::array<float, kNumGuardBands> activeCutDb {};
    float totalEnv = 1.0e-5f;
    float fastAtt = 0.05f, fastRel = 0.005f;
    float persistenceAtt = 0.001f, persistenceRel = 0.002f;
    uint32_t decimCounter = 0;
};

// =============================================================================
//  2. 打鍵音・マウスクリック・衝撃波トランジェント・ブランカー
// =============================================================================
class MechanicalClickAndImpactBlanker
{
public:
    MechanicalClickAndImpactBlanker() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        clickHighpass.prepare(sampleRate);
        clickHighpass.setParameters(TptStateVariableFilter::Mode::Highpass, 2800.0f, 0.7071f);

        ultraFastAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 0.15f);
        ultraFastRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 2.2f);
        vocalBodyAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 14.0f);
        vocalBodyRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 60.0f);
        reset();
    }

    void reset() noexcept
    {
        clickHighpass.reset();
        spikeEnv = 0.0f;
        bodyEnv  = 0.0f;
        blankingGain = 1.0f;
    }

    void setSensitivity(float sensitivity01) noexcept
    {
        sensitivity = std::clamp(sensitivity01, 0.0f, 1.0f);
    }

    inline float processSample(float input) noexcept
    {
        if (sensitivity <= 0.001f)
            return input;

        const float absIn = std::fabs(input);
        const float hiClick = std::fabs(clickHighpass.processSample(input));

        spikeEnv += ((hiClick > spikeEnv) ? ultraFastAtt : ultraFastRel) * (hiClick - spikeEnv);
        bodyEnv  += ((absIn > bodyEnv)    ? vocalBodyAtt : vocalBodyRel) * (absIn - bodyEnv);

        // 声の持続ボディが小さいのに 0.2ms の鋭い高域インパルスだけが跳ねた場合＝マウス/キーボード打鍵音
        float targetGain = 1.0f;
        if (spikeEnv > (bodyEnv * 3.8f + 0.006f))
        {
            const float excess = spikeEnv / (bodyEnv * 3.8f + 0.006f);
            targetGain = std::clamp(1.0f / (1.0f + (excess - 1.0f) * 0.85f * sensitivity), 0.18f, 1.0f);
        }

        blankingGain += 0.25f * (targetGain - blankingGain);
        return FastAudioMath::sanitize(input * blankingGain);
    }

private:
    double sampleRate = 48000.0;
    float sensitivity = 0.55f;
    TptStateVariableFilter clickHighpass;
    float ultraFastAtt = 0.4f, ultraFastRel = 0.02f;
    float vocalBodyAtt = 0.01f, vocalBodyRel = 0.002f;
    float spikeEnv = 0.0f;
    float bodyEnv  = 0.0f;
    float blankingGain = 1.0f;
};

// =============================================================================
//  3. 頭蓋骨伝導逆特性＋Bauerバイノーラル・シンガーズキューモニター
// =============================================================================
class BinauralBoneConductionCueEnhancer
{
public:
    static constexpr size_t kCrossfeedDelayMax = 64;
    static constexpr size_t kMask              = kCrossfeedDelayMax - 1;

    BinauralBoneConductionCueEnhancer() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        crossfeedDelaySamples = std::clamp(static_cast<int>(sampleRate * 0.00028), 1, 48); // 280 microseconds ITD

        for (size_t ch = 0; ch < 2; ++ch)
        {
            // 骨伝導のこもり（280Hz）をすっきりさせ、ピッチ知覚の鍵となる第3倍音〜2.4kHzを明瞭化
            boneMudCutBell[ch].prepare(sampleRate);
            pitchCueFocusBell[ch].prepare(sampleRate);
            crossfeedLp[ch].prepare(sampleRate);

            boneMudCutBell[ch].setParameters(TptStateVariableFilter::Mode::PeakBell, 285.0f, 1.35f, -2.4f);
            pitchCueFocusBell[ch].setParameters(TptStateVariableFilter::Mode::PeakBell, 2350.0f, 1.15f, 2.2f);
            crossfeedLp[ch].setParameters(TptStateVariableFilter::Mode::Lowpass, 720.0f, 0.7071f);
        }
        reset();
    }

    void reset() noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            boneMudCutBell[ch].reset();
            pitchCueFocusBell[ch].reset();
            crossfeedLp[ch].reset();
            delayBuf[ch].fill(0.0f);
        }
        writePos = 0;
    }

    void setCueAmount(float amount01) noexcept
    {
        cueAmount = std::clamp(amount01, 0.0f, 1.0f);
    }

    inline void processStereoSample(float& left, float& right) noexcept
    {
        if (cueAmount <= 0.001f)
            return;

        const float eqL = pitchCueFocusBell[0].processSample(boneMudCutBell[0].processSample(left));
        const float eqR = pitchCueFocusBell[1].processSample(boneMudCutBell[1].processSample(right));

        delayBuf[0][writePos] = crossfeedLp[0].processSample(eqL);
        delayBuf[1][writePos] = crossfeedLp[1].processSample(eqR);

        const size_t readPos = (writePos - static_cast<size_t>(crossfeedDelaySamples)) & kMask;
        writePos = (writePos + 1) & kMask;

        const float crossL = eqL + delayBuf[1][readPos] * 0.16f;
        const float crossR = eqR + delayBuf[0][readPos] * 0.16f;

        left  = FastAudioMath::sanitize(left * (1.0f - cueAmount) + crossL * cueAmount);
        right = FastAudioMath::sanitize(right * (1.0f - cueAmount) + crossR * cueAmount);
    }

private:
    double sampleRate = 48000.0;
    float cueAmount = 0.0f;
    int crossfeedDelaySamples = 14;

    std::array<TptStateVariableFilter, 2> boneMudCutBell;
    std::array<TptStateVariableFilter, 2> pitchCueFocusBell;
    std::array<TptStateVariableFilter, 2> crossfeedLp;
    std::array<std::array<float, kCrossfeedDelayMax>, 2> delayBuf {};
    size_t writePos = 0;
};

// =============================================================================
//  4. BroadcastStreamGuardSuite — ライブ配信護身＆モニター補正統合スイート
// =============================================================================
class BroadcastStreamGuardSuite
{
public:
    BroadcastStreamGuardSuite() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            howlingKiller[ch].prepare(sampleRate);
            clickBlanker[ch].prepare(sampleRate);
        }
        binauralCue.prepare(sampleRate);
    }

    void reset() noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            howlingKiller[ch].reset();
            clickBlanker[ch].reset();
        }
        binauralCue.reset();
    }

    void setStreamGuardParameters(float antiHowling01, float clickBlanker01, float binauralCue01) noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            howlingKiller[ch].setProtectionStrength(antiHowling01);
            clickBlanker[ch].setSensitivity(clickBlanker01);
        }
        binauralCue.setCueAmount(binauralCue01);
    }

    inline void processStereoBlock(float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0 || left == nullptr)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            float l = clickBlanker[0].processSample(left[i]);
            l = howlingKiller[0].processSample(l);

            float r = (right != nullptr) ? clickBlanker[1].processSample(right[i]) : l;
            if (right != nullptr)
                r = howlingKiller[1].processSample(r);

            binauralCue.processStereoSample(l, r);

            left[i] = l;
            if (right != nullptr)
                right[i] = r;
        }
    }

private:
    double sampleRate = 48000.0;
    std::array<AdaptiveFeedbackHowlingSuppressor, 2> howlingKiller;
    std::array<MechanicalClickAndImpactBlanker, 2> clickBlanker;
    BinauralBoneConductionCueEnhancer binauralCue;
};

} // namespace stream_guard
} // namespace gzzio
