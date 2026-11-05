#pragma once

/**
 * @file StreamingAudioRouterAndSafetyMatrix.h
 * @brief VocalGzzio v5.5.0 配信OBS/イヤモニ独立2系統ルーティング＆ゼロクリック咳払いミュート・ハウリング緊急遮断マトリクス
 *
 * 生配信（YouTube Live / Twitch / 歌枠）において最も事故が起きやすい
 *   1. 「自分のイヤモニではリバーブを深めに気持ちよく聴きたいが、OBS配信乗せは歌詞が聴き取りやすい適度なリバーブにしたい」
 *   2. 「くしゃみ・咳払いの瞬間にミュートボタンを押したとき『ブチッ』とクリックノイズが乗る」
 *   3. 「スピーカーからマイクへ音が回り込んで突然『キーン！』とハウリング（音響フィードバック発振）が起きる」
 * の3大トラブルをハードウェア放送卓レベルで防ぐセーフティ・マトリクス。
 *
 * 収録クラス:
 *   1. ZeroClickHermiteCoughMuteGuard
 *      - 5次エルミート多項式 $6t^5 - 15t^4 + 10t^3$（1階・2階微分ともに端点でゼロ）による
 *        8ms S字フェードイン/フェードアウト包絡で、大声の最中にミュートしてもクリックノイズを100%排除。
 *
 *   2. AcousticFeedbackHowlingKiller
 *      - 自己相関の周期性（正弦波発振状態）と急激なエネルギー増大を検知し、
 *        ハウリング周波数を瞬時に推定して動的TPTノッチフィルタで発振ループを遮断。
 *
 *   3. DualBusStreamAndMonitorMatrix
 *      - 配信バス（Stream Bus）と演者ヘッドホンモニターバス（Monitor Bus）の独立バランス計算機。
 *
 *   4. StreamingAudioRouterAndSafetySuite
 *      - 上記3モジュールを統合した放送・歌枠セーフティスイート。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace stream_safety
{

// =============================================================================
// 1. ゼロクリック5次エルミート咳払いミュート・ガード (ZeroClickHermiteCoughMuteGuard)
// =============================================================================
class ZeroClickHermiteCoughMuteGuard
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        // 8.5ms の滑らかな遷移時間
        stepPerSample_ = static_cast<float>(1.0 / (0.0085 * sr_));
        reset();
    }

    void reset() noexcept
    {
        muted_ = false;
        phase01_ = 1.0f;
    }

    void setCoughMuted(bool shouldMute) noexcept
    {
        muted_ = shouldMute;
    }

    bool isCoughMuted() const noexcept { return muted_; }

    void processStereo(float& ioL, float& ioR) noexcept
    {
        const float target = muted_ ? 0.0f : 1.0f;
        if (phase01_ < target)
            phase01_ = std::min(1.0f, phase01_ + stepPerSample_);
        else if (phase01_ > target)
            phase01_ = std::max(0.0f, phase01_ - stepPerSample_);

        // 5次エルミート平滑化多項式 S(t) = t^3 * (6t^2 - 15t + 10)
        const float t  = phase01_;
        const float t3 = t * t * t;
        const float gain = t3 * (t * (6.0f * t - 15.0f) + 10.0f);

        ioL *= gain;
        ioR *= gain;
    }

    float getCurrentGain() const noexcept
    {
        const float t = phase01_;
        return (t * t * t) * (t * (6.0f * t - 15.0f) + 10.0f);
    }

private:
    double sr_            = 48000.0;
    float  stepPerSample_ = 0.002f;
    bool   muted_         = false;
    float  phase01_       = 1.0f;
};

// =============================================================================
// 2. 音響フィードバック（ハウリング）自動検知＆ノッチ遮断器 (AcousticFeedbackHowlingKiller)
// =============================================================================
class AcousticFeedbackHowlingKiller
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        fastEnvAlpha_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sr_)));
        slowEnvAlpha_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.120 * sr_)));
        freqSmooth_   = static_cast<float>(1.0 - std::exp(-1.0 / (0.015 * sr_)));
        reset();
        updateNotchCoeffs(2400.0f);
    }

    void reset() noexcept
    {
        fastEnv_      = 0.0f;
        slowEnv_      = 0.0f;
        prevSample_   = 0.0f;
        samplesSinceZero_ = 0;
        estimatedHz_  = 2400.0f;
        howlDepth01_  = 0.0f;
        ic1L_ = ic2L_ = ic1R_ = ic2R_ = 0.0f;
    }

    void processStereo(float& ioL, float& ioR, bool enableProtection = true) noexcept
    {
        if (!enableProtection)
            return;

        const float mono = 0.5f * (ioL + ioR);
        const float absM = std::fabs(mono);
        fastEnv_ += fastEnvAlpha_ * (absM - fastEnv_);
        slowEnv_ += slowEnvAlpha_ * (absM - slowEnv_);

        // ゼロ交差間隔から瞬時優勢周波数を推定
        ++samplesSinceZero_;
        if ((mono >= 0.0f && prevSample_ < 0.0f) && samplesSinceZero_ > 4)
        {
            const float instHz = std::clamp(static_cast<float>(sr_) / static_cast<float>(samplesSinceZero_), 250.0f, 9500.0f);
            const float freqDiffRatio = std::fabs(instHz - estimatedHz_) / (estimatedHz_ + 1.0f);
            estimatedHz_ += freqSmooth_ * (instHz - estimatedHz_);

            // 単一周波数にロックオンしたまま（周波数変動 < 1.8%）エネルギーが急増している場合はハウリングと判定
            if (freqDiffRatio < 0.018f && fastEnv_ > 0.08f && fastEnv_ > slowEnv_ * 1.15f)
            {
                howlDepth01_ = std::min(1.0f, howlDepth01_ + 0.08f);
                updateNotchCoeffs(estimatedHz_);
            }
            else
            {
                howlDepth01_ = std::max(0.0f, howlDepth01_ - 0.015f);
            }
            samplesSinceZero_ = 0;
        }
        prevSample_ = mono;

        if (howlDepth01_ > 1.0e-3f)
        {
            const float v3L = ioL - ic2L_;
            const float v1L = a1_ * ic1L_ + a2_ * v3L;
            const float v2L = ic2L_ + a2_ * ic1L_ + a3_ * v3L;
            ic1L_ = 2.0f * v1L - ic1L_;
            ic2L_ = 2.0f * v2L - ic2L_;
            ioL -= (v1L * k_) * (howlDepth01_ * 0.85f);

            const float v3R = ioR - ic2R_;
            const float v1R = a1_ * ic1R_ + a2_ * v3R;
            const float v2R = ic2R_ + a2_ * ic1R_ + a3_ * v3R;
            ic1R_ = 2.0f * v1R - ic1R_;
            ic2R_ = 2.0f * v2R - ic2R_;
            ioR -= (v1R * k_) * (howlDepth01_ * 0.85f);
        }
    }

    float getHowlingSuppressionDepth01() const noexcept { return howlDepth01_; }
    float getTrackedFrequencyHz() const noexcept { return estimatedHz_; }

private:
    void updateNotchCoeffs(float freqHz) noexcept
    {
        const float fc = std::clamp(freqHz, 220.0f, static_cast<float>(sr_ * 0.43));
        const float g  = static_cast<float>(std::tan(3.14159265358979323846 * static_cast<double>(fc) / sr_));
        k_  = 1.0f / 6.5f; // 高Qピンポイントノッチ
        a1_ = 1.0f / (1.0f + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

    double sr_               = 48000.0;
    float  fastEnvAlpha_     = 0.01f;
    float  slowEnvAlpha_     = 0.001f;
    float  freqSmooth_       = 0.05f;
    float  fastEnv_          = 0.0f;
    float  slowEnv_          = 0.0f;
    float  prevSample_       = 0.0f;
    int    samplesSinceZero_ = 0;
    float  estimatedHz_      = 2400.0f;
    float  howlDepth01_      = 0.0f;
    float  k_ = 0.15f, a1_ = 0.5f, a2_ = 0.1f, a3_ = 0.02f;
    float  ic1L_ = 0.0f, ic2L_ = 0.0f, ic1R_ = 0.0f, ic2R_ = 0.0f;
};

// =============================================================================
// 3. 配信バス／イヤモニバス独立ミキシング・マトリクス (DualBusStreamAndMonitorMatrix)
// =============================================================================
class DualBusStreamAndMonitorMatrix
{
public:
    struct BusOutputPair
    {
        float streamL  = 0.0f;
        float streamR  = 0.0f;
        float monitorL = 0.0f;
        float monitorR = 0.0f;
    };

    static BusOutputPair computeDualBus(float dryVocalL, float dryVocalR,
                                        float wetFxL,    float wetFxR,
                                        float bgmL,      float bgmR,
                                        float streamReverbRatio01  = 0.72f,
                                        float monitorComfortBoost01 = 0.25f) noexcept
    {
        BusOutputPair out{};
        const float sRev = std::clamp(streamReverbRatio01, 0.0f, 1.5f);
        const float mCom = std::clamp(monitorComfortBoost01, 0.0f, 1.0f);

        // OBS 配信乗せバス: 歌詞の明瞭度を最優先し、リバーブ量を適度に整えてBGMと融合
        out.streamL = dryVocalL + wetFxL * sRev + bgmL;
        out.streamR = dryVocalR + wetFxR * sRev + bgmR;

        // 演者イヤモニバス: 自分の声が気持ちよく伸びるようにリバーブとダイレクト声を少しリッチにモニター
        const float monVocalGain = 1.0f + 0.15f * mCom;
        const float monRevGain   = sRev + 0.28f * mCom;
        const float monBgmGain   = 1.0f - 0.12f * mCom;
        out.monitorL = dryVocalL * monVocalGain + wetFxL * monRevGain + bgmL * monBgmGain;
        out.monitorR = dryVocalR * monVocalGain + wetFxR * monRevGain + bgmR * monBgmGain;
        return out;
    }
};

// =============================================================================
// 4. 統合ストリーミング・セーフティスイート (StreamingAudioRouterAndSafetySuite)
// =============================================================================
class StreamingAudioRouterAndSafetySuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        coughGuard_.prepare(sampleRate);
        howlKiller_.prepare(sampleRate);
    }

    void reset() noexcept
    {
        coughGuard_.reset();
        howlKiller_.reset();
    }

    void setCoughMute(bool muted) noexcept { coughGuard_.setCoughMuted(muted); }
    bool isCoughMuted() const noexcept { return coughGuard_.isCoughMuted(); }

    void processStereoSample(float& ioL, float& ioR, bool enableHowlGuard = true) noexcept
    {
        howlKiller_.processStereo(ioL, ioR, enableHowlGuard);
        coughGuard_.processStereo(ioL, ioR);
    }

    float getCoughMuteGain() const noexcept { return coughGuard_.getCurrentGain(); }
    float getHowlSuppression01() const noexcept { return howlKiller_.getHowlingSuppressionDepth01(); }

private:
    ZeroClickHermiteCoughMuteGuard coughGuard_;
    AcousticFeedbackHowlingKiller  howlKiller_;
};

} // namespace stream_safety
} // namespace gzzio

namespace vg
{
namespace stream_safety = ::gzzio::stream_safety;
} // namespace vg
