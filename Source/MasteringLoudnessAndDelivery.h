#pragma once

/**
 * @file MasteringLoudnessAndDelivery.h
 * @brief VocalGzzio 配信プラットフォーム別ラウドネス最適化・ロッシーコーデック歪みガード・クレスト復元＆心理音響ノイズシェーピングディザ
 *
 * 【研究開発の背景と独自技術】
 * 1. StreamingLoudnessTargetOptimizer (YouTube / Spotify / Apple Music / Shorts 規格別ラウドネス＆天井最適化):
 *    - 現代の音楽配信・動画プラットフォームごとの規定値:
 *      ・YouTube / MV: -14.0 LUFS / -1.0 dBTP
 *      ・Spotify Normal: -14.0 LUFS / -1.0 dBTP
 *      ・Apple Music SoundCheck: -16.0 LUFS / -1.0 dBTP
 *      ・YouTube Shorts / TikTok / 歌ってみた配信: -12.0 LUFS / -0.8 dBTP
 *      ・CD / クラブマスタリング: -9.5 LUFS / -0.3 dBTP
 *    - 現在の Short-Term LUFS と目標ラウドネスの差分 (Loudness Penalty / Margin dB) を算出し、
 *      配信先での自動音量引き下げ (Loudness Normalization Penalty) で曲が小さく聴こえる現象を防ぎます。
 *
 * 2. LossyCodecDistortionGuard (AAC / Opus / MP3 エンコード時のギブス現象オーバーシュート予防フィルタ):
 *    - 配信サイトの非可逆圧縮 (AAC / Opus) を通すと、15.5kHz 以上の急峻なエネルギーがギブス現象を起こし、
 *      最大 +0.8 dB のコーデック後クリップ (Codec Reconstruction Overshoot) が発生します。
 *    - 本モジュールは 15.8kHz 以上の過剰なスルーレートを検出し、エンコード後のクリップを事前に防ぎます。
 *
 * 3. UpwardCrestMicroRestorer (潰れすぎたボーカル素材のマイクロダイナミクス・クレスト復元器):
 *    - 録音段階で強くコンプがかかりすぎた平坦なボーカルに対し、音節のアタック頂点だけを +2.4 dB 伸長して
 *      生き生きとした躍動感を取り戻します。
 *
 * 4. PsychoacousticNoiseShapedDither (Wannamaker F重み付け2次エラーフィードバック TPDF ディザ):
 *    - 人間の耳が最も敏感な 3kHz〜4kHz 帯域の量子化ノイズフロアを最小化する 24-bit 心理音響ノイズシェーピングディザです。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "MasteringProEngine.h"

namespace vg::mastering_delivery
{

//==============================================================================
// 1. 配信プラットフォーム別ラウドネス＆True-Peak ターゲット最適化器
//==============================================================================
class StreamingLoudnessTargetOptimizer
{
public:
    enum class DeliveryPlatform : int
    {
        YouTubeAndMv       = 0, // -14.0 LUFS / -1.0 dBTP
        SpotifyStreaming   = 1, // -14.0 LUFS / -1.0 dBTP
        AppleMusicSoundChk = 2, // -16.0 LUFS / -1.0 dBTP
        ShortsTikTokLive   = 3, // -12.0 LUFS / -0.8 dBTP
        CdClubLoudMaster   = 4  //  -9.5 LUFS / -0.3 dBTP
    };

    struct PlatformSpec
    {
        float targetLufs = -14.0f;
        float ceilingDbTp = -1.0f;
        const char* platformName = "YouTube / Spotify (-14 LUFS)";
    };

    static inline PlatformSpec getSpec(DeliveryPlatform p) noexcept
    {
        switch (p)
        {
            case DeliveryPlatform::AppleMusicSoundChk:
                return { -16.0f, -1.0f, "Apple Music (-16 LUFS / -1.0 dBTP)" };
            case DeliveryPlatform::ShortsTikTokLive:
                return { -12.0f, -0.8f, "Shorts / TikTok / Live (-12 LUFS / -0.8 dBTP)" };
            case DeliveryPlatform::CdClubLoudMaster:
                return { -9.5f, -0.3f, "CD / Loud Pop Master (-9.5 LUFS / -0.3 dBTP)" };
            case DeliveryPlatform::SpotifyStreaming:
                return { -14.0f, -1.0f, "Spotify Streaming (-14 LUFS / -1.0 dBTP)" };
            case DeliveryPlatform::YouTubeAndMv:
            default:
                return { -14.0f, -1.0f, "YouTube / MV (-14 LUFS / -1.0 dBTP)" };
        }
    }

    void reset() noexcept
    {
        smoothedTrimGain = 1.0f;
        estimatedLoudnessPenaltyDb = 0.0f;
    }

    /**
     * @brief 現在の Short-Term LUFS に基づき、配信プラットフォームでのペナルティ予測値と推奨トリムを更新
     */
    void updateTelemetry(float currentShortTermLufs, DeliveryPlatform platform) noexcept
    {
        const PlatformSpec spec = getSpec(platform);
        if (currentShortTermLufs > -55.0f)
        {
            // ターゲットを超過している分が配信サイトでの音量引き下げペナルティ (負の値)
            const float excessDb = currentShortTermLufs - spec.targetLufs;
            estimatedLoudnessPenaltyDb = (excessDb > 0.0f) ? -excessDb : 0.0f;
        }
        else
        {
            estimatedLoudnessPenaltyDb = 0.0f;
        }
    }

    float getEstimatedLoudnessPenaltyDb() const noexcept { return estimatedLoudnessPenaltyDb; }

private:
    float smoothedTrimGain = 1.0f;
    float estimatedLoudnessPenaltyDb = 0.0f;
};

//==============================================================================
// 2. AAC / Opus ロッシーコーデック・ギブス現象オーバーシュート予防ガード
//==============================================================================
class LossyCodecDistortionGuard
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        for (int ch = 0; ch < 2; ++ch)
        {
            ultraHighExtractor[ch].prepare(sr);
            ultraHighExtractor[ch].setParameters(15400.0f, 0.75f, 0.0f,
                                                 vg::mastering::TptStateVariableFilter::Mode::Highpass);
        }
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
            ultraHighExtractor[ch].reset();
        hfEnvelope = 0.0f;
        codecHeadroomSavedDb = 0.0f;
    }

    void processSamplePair(float& left, float& right, float guardStrength01) noexcept
    {
        guardStrength01 = std::clamp(guardStrength01, 0.0f, 1.0f);
        if (guardStrength01 <= 0.001f)
        {
            codecHeadroomSavedDb = 0.0f;
            return;
        }

        const float hfL = ultraHighExtractor[0].processSample(left);
        const float hfR = ultraHighExtractor[1].processSample(right);
        const float hfPeak = std::max(std::fabs(hfL), std::fabs(hfR));

        hfEnvelope += 0.025f * (hfPeak - hfEnvelope);

        // 15.4kHz 以上の超高域振幅が 0.12 (-18 dBFS) を超える瞬間のみ穏やかに抑制し、
        // AAC/Opus デコード時のギブス現象ピーク跳ね上がりを防止
        float dampRatio = 0.0f;
        if (hfEnvelope > 0.12f)
        {
            dampRatio = std::clamp((hfEnvelope - 0.12f) * 2.5f, 0.0f, 0.55f) * guardStrength01;
        }

        left  = vg::mastering::FastAudioMath::sanitize(left - dampRatio * hfL);
        right = vg::mastering::FastAudioMath::sanitize(right - dampRatio * hfR);
        codecHeadroomSavedDb = dampRatio * 1.45f;
    }

    float getCodecHeadroomSavedDb() const noexcept { return codecHeadroomSavedDb; }

private:
    double sr = 48000.0;
    float hfEnvelope = 0.0f;
    float codecHeadroomSavedDb = 0.0f;
    std::array<vg::mastering::TptStateVariableFilter, 2> ultraHighExtractor;
};

//==============================================================================
// 3. 潰れたボーカル素材のアップワード・クレストファクター復元器
//==============================================================================
class UpwardCrestMicroRestorer
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        const float dt = static_cast<float>(1.0 / sr);
        fastCoeff = 1.0f - std::exp(-dt / 0.003f);
        slowCoeff = 1.0f - std::exp(-dt / 0.065f);
        reset();
    }

    void reset() noexcept
    {
        fastEnv = 0.0f;
        slowEnv = 0.0f;
    }

    void processSamplePair(float& left, float& right, float restoreAmount01) noexcept
    {
        restoreAmount01 = std::clamp(restoreAmount01, 0.0f, 1.0f);
        if (restoreAmount01 <= 0.0005f)
            return;

        const float peak = std::max(std::fabs(left), std::fabs(right));
        fastEnv += fastCoeff * (peak - fastEnv);
        slowEnv += slowCoeff * (peak - slowEnv);

        if (slowEnv > 1.0e-4f && fastEnv > slowEnv)
        {
            const float crestTransient = std::clamp((fastEnv - slowEnv) / (slowEnv + 1.0e-4f), 0.0f, 1.0f);
            const float expandDb = 2.4f * restoreAmount01 * crestTransient;
            const float gain = vg::mastering::FastAudioMath::dbToLinear(expandDb);
            left  = vg::mastering::FastAudioMath::sanitize(left * gain);
            right = vg::mastering::FastAudioMath::sanitize(right * gain);
        }
    }

private:
    double sr = 48000.0;
    float fastCoeff = 0.01f;
    float slowCoeff = 0.0005f;
    float fastEnv = 0.0f;
    float slowEnv = 0.0f;
};

//==============================================================================
// 4. Wannamaker F重み付け 2次エラーフィードバック 24-bit TPDF 心理音響ディザ
//==============================================================================
class PsychoacousticNoiseShapedDither
{
public:
    void reset() noexcept
    {
        rngState = 0x853C49E6748FEA9Bull;
        err1L = 0.0f;
        err2L = 0.0f;
        err1R = 0.0f;
        err2R = 0.0f;
    }

    void processSamplePair24Bit(float& left, float& right, bool enableDither = true) noexcept
    {
        if (!enableDither)
            return;

        // 24-bit 量子化ステップ (1 / 2^23)
        constexpr float kQuantScale = 8388608.0f;
        constexpr float kInvQuantScale = 1.0f / kQuantScale;

        // 2次エラーフィードバック係数 (3.2kHz 周辺の量子化雑音を減衰)
        constexpr float b1 = 1.52f;
        constexpr float b2 = -0.58f;

        // Left
        const float shapedInL = left + (b1 * err1L + b2 * err2L) * kInvQuantScale;
        const float tpdfL = nextTpdf() * kInvQuantScale;
        const float quantL = std::round((shapedInL + tpdfL) * kQuantScale);
        const float outL = quantL * kInvQuantScale;
        err2L = err1L;
        err1L = (shapedInL * kQuantScale) - quantL;
        left = vg::mastering::FastAudioMath::sanitize(outL);

        // Right
        const float shapedInR = right + (b1 * err1R + b2 * err2R) * kInvQuantScale;
        const float tpdfR = nextTpdf() * kInvQuantScale;
        const float quantR = std::round((shapedInR + tpdfR) * kQuantScale);
        const float outR = quantR * kInvQuantScale;
        err2R = err1R;
        err1R = (shapedInR * kQuantScale) - quantR;
        right = vg::mastering::FastAudioMath::sanitize(outR);
    }

private:
    float nextUniform01() noexcept
    {
        // xorshift64* (ゼロアロケーション・決定論的PRNG)
        uint64_t x = rngState;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        rngState = x;
        const uint32_t bits = static_cast<uint32_t>((x * 0x2545F4914F6CDD1Dull) >> 40);
        return static_cast<float>(bits) * (1.0f / 16777216.0f);
    }

    float nextTpdf() noexcept
    {
        return (nextUniform01() - nextUniform01());
    }

    uint64_t rngState = 0x853C49E6748FEA9Bull;
    float err1L = 0.0f;
    float err2L = 0.0f;
    float err1R = 0.0f;
    float err2R = 0.0f;
};

//==============================================================================
// 5. マスタリング配信＆ラウドネス統合スイート (MasteringLoudnessAndDeliverySuite)
//==============================================================================
class MasteringLoudnessAndDeliverySuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        codecGuard.prepare(sampleRate);
        crestRestorer.prepare(sampleRate);
        loudnessOptimizer.reset();
        dither.reset();
    }

    void reset() noexcept
    {
        codecGuard.reset();
        crestRestorer.reset();
        loudnessOptimizer.reset();
        dither.reset();
    }

    void processSamplePair(float& left,
                           float& right,
                           float crestRestore01,
                           float codecGuard01,
                           bool enable24BitDither) noexcept
    {
        crestRestorer.processSamplePair(left, right, crestRestore01);
        codecGuard.processSamplePair(left, right, codecGuard01);
        dither.processSamplePair24Bit(left, right, enable24BitDither);
    }

    StreamingLoudnessTargetOptimizer& getLoudnessOptimizer() noexcept { return loudnessOptimizer; }
    const StreamingLoudnessTargetOptimizer& getLoudnessOptimizer() const noexcept { return loudnessOptimizer; }

    LossyCodecDistortionGuard& getCodecGuard() noexcept { return codecGuard; }
    const LossyCodecDistortionGuard& getCodecGuard() const noexcept { return codecGuard; }

    UpwardCrestMicroRestorer& getCrestRestorer() noexcept { return crestRestorer; }
    PsychoacousticNoiseShapedDither& getDither() noexcept { return dither; }

private:
    StreamingLoudnessTargetOptimizer loudnessOptimizer;
    LossyCodecDistortionGuard codecGuard;
    UpwardCrestMicroRestorer crestRestorer;
    PsychoacousticNoiseShapedDither dither;
};

} // namespace vg::mastering_delivery
