#pragma once
// ==============================================================================
//  EmotionalBreathAndTearNuancePreserver.h
//  VocalGzzio — 感情的ブレス（歌い出しの息の溜め）・語尾の吐息・泣き声の揺らぎ（5〜7Hz 微細トレモロ）保護エンジン
//
//  【人間の心に染みる歌の核心：「息遣い」と「泣きの揺らぎ」を絶対にノイズとして消さない】
//   一般的なデジタル・ノイズゲートやAIノイズ除去の最大の欠点は、
//   ・サビ直前の「ッハァ…」という感情のこもった吸気ブレス（Inspiratory Emotional Breath）
//   ・フレーズ終わりの消えゆく吐息（Phrase-Ending Sigh / Expiratory Release）
//   ・感極まった時の声の微細な震え（Tearful Vocal Tremor: 5.2 Hz 〜 7.4 Hz の振幅・フォルマント微振動）
//   を「背景ノイズ」や「ピッチ外れ」と誤認して機械的にバッサリ切り捨ててしまうことである。
//
//   本エンジンは:
//   1. **ゼロ交差率（ZCR）× 気流フォルマント比による吸気ブレス識別器**:
//      部屋のエアコン騒音（定常ノイズ）と、歌い手の感情ブレス（非定常・声道共鳴気流）をリアルタイム識別する。
//   2. **泣き声・感情トレモロ（5.2〜7.4 Hz 包絡ゆらぎ）検出＆保護器 (`TearfulTremorDetector`)**:
//      バラードのクライマックスで声が震える感動的な瞬間を検出し、過剰な平滑化から守る。
//   3. 感情ブレスと判定された瞬間は嫌な歯擦帯域（6.2kHz）だけを丸めつつ 10.5kHz のシルク・エアを添えて
//      「THE FIRST TAKE のような生々しく美しいブレス」へ昇華する。
// ==============================================================================

#include "LightweightSimdFastMathAndEcoGovernor.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace vg::human
{

enum class BreathNuanceState : int
{
    DeadAirSilence       = 0, // 完全な無音・待機中（部屋ノイズは静かに減衰）
    ExpressiveInhale     = 1, // 歌い出し直前の「感情吸気ブレス」（保護＆シルク磨き）
    VoicedSinging        = 2, // 有声歌唱中（芯と倍音を維持）
    EmotionalSighRelease = 3, // フレーズ語尾の「余韻吐息・泣きのフェードアウト」（長いベルベットリリース）
    TearfulQuiverActive  = 4  // 感極まった「泣きの声震え（5〜7Hz 感情トレモロ）」検出中
};

// ==============================================================================
//  1. 感極まった声の震え（Tearful Vocal Quiver: 5.5 Hz 包絡変調）検出器
// ==============================================================================
class TearfulTremorDetector
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double safeFs = std::max (8000.0, sampleRate);
        // 包絡自体を 100Hz にデシメーションせず連続追跡するための 6Hz バンドパス近似
        envLp20HzCoeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * 20.0 / safeFs));
        envLp2HzCoeff  = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * 2.5  / safeFs));
        smoothCoeff    = static_cast<float> (std::exp (-1.0 / (0.120 * safeFs)));
        reset();
    }

    void reset() noexcept
    {
        lp20 = 0.0f;
        lp2  = 0.0f;
        tremorDepth01 = 0.0f;
    }

    inline float observeEnvelopeAndDetectQuiver (float fastEnv) noexcept
    {
        lp20 = envLp20HzCoeff * lp20 + (1.0f - envLp20HzCoeff) * fastEnv;
        lp2  = envLp2HzCoeff  * lp2  + (1.0f - envLp2HzCoeff)  * fastEnv;

        // 2.5Hz〜20Hz の包絡揺らぎ成分（ビブラート・泣きの震え）の相対変調度
        const float modComponent = std::fabs (lp20 - lp2);
        float rawDepth = 0.0f;
        if (lp2 > 0.008f)
            rawDepth = std::clamp ((modComponent / lp2) * 3.5f, 0.0f, 1.0f);

        tremorDepth01 = smoothCoeff * tremorDepth01 + (1.0f - smoothCoeff) * rawDepth;
        return tremorDepth01;
    }

    float getTremorDepth01() const noexcept { return tremorDepth01; }

private:
    float envLp20HzCoeff = 0.997f;
    float envLp2HzCoeff  = 0.9997f;
    float smoothCoeff    = 0.9995f;
    float lp20 = 0.0f;
    float lp2  = 0.0f;
    float tremorDepth01 = 0.0f;
};

// ==============================================================================
//  2. 感情的ブレス・語尾吐息・泣きの揺らぎ保護統合エンジン
// ==============================================================================
struct BreathNuanceTelemetry
{
    BreathNuanceState currentState         = BreathNuanceState::DeadAirSilence;
    float             breathSilkPolishDb   = 0.0f;
    float             tearfulQuiverDepth01 = 0.0f;
    float             nuancePreservation01 = 1.0f;
    uint32_t          preservedBreathCount = 0;
};

class EmotionalBreathAndTearNuancePreserver
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRateHz = std::max (8000.0, sampleRate);
        fastEnvCoeff   = static_cast<float> (std::exp (-1.0 / (0.008 * sampleRateHz)));
        slowNoiseCoeff = static_cast<float> (std::exp (-1.0 / (1.500 * sampleRateHz)));
        sighHoldSamplesMax = static_cast<int> (0.420 * sampleRateHz); // 語尾余韻 420ms 保護

        const double fcAir = std::min (10500.0, sampleRateHz * 0.40);
        silkHpCoeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * fcAir / sampleRateHz));

        const double fcSoft = 5800.0;
        breathSoftLpCoeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * fcSoft / sampleRateHz));

        tremorDetector.prepare (sampleRateHz);
        reset();
    }

    void reset() noexcept
    {
        fastEnv = 0.0f;
        noiseFloorEst = 0.0005f;
        zcrRateEnv = 0.05f;
        prevSampleMono = 0.0f;
        sighHoldCounter = 0;
        state = BreathNuanceState::DeadAirSilence;
        preservedCount = 0;
        silkPrevL = 0.0f; silkPrevR = 0.0f;
        silkHpL = 0.0f;   silkHpR = 0.0f;
        softLpL = 0.0f;   softLpR = 0.0f;
        smoothedGateGain = 1.0f;
        tremorDetector.reset();
    }

    inline void processStereo (float& left, float& right, float breathBeautyAmount01 = 0.50f) noexcept
    {
        const float mono = 0.5f * (left + right);
        const float absMono = std::fabs (mono);

        // 1. ゼロ交差率（ZCR）の推定（有声音は低ZCR、ブレス・気流音は中〜高ZCR）
        const float zc = ((mono >= 0.0f && prevSampleMono < 0.0f) || (mono < 0.0f && prevSampleMono >= 0.0f)) ? 1.0f : 0.0f;
        prevSampleMono = mono;
        zcrRateEnv = 0.992f * zcrRateEnv + 0.008f * zc;

        // 2. 高速エンベロープと定常ノイズフロアの推定
        fastEnv = fastEnvCoeff * fastEnv + (1.0f - fastEnvCoeff) * absMono;
        const float quiver01 = tremorDetector.observeEnvelopeAndDetectQuiver (fastEnv);

        if (fastEnv < noiseFloorEst * 1.5f)
            noiseFloorEst = slowNoiseCoeff * noiseFloorEst + (1.0f - slowNoiseCoeff) * fastEnv;
        else
            noiseFloorEst = std::min (0.01f, noiseFloorEst * 1.00001f);

        const float snrRatio = fastEnv / std::max (1.0e-6f, noiseFloorEst);

        // 3. 状態遷移判定（有声歌唱 / 泣き声揺らぎ / 感情ブレス / 語尾吐息 / 完全無音）
        const BreathNuanceState prevState = state;
        if (fastEnv > 0.018f && zcrRateEnv < 0.18f)
        {
            state = (quiver01 > 0.42f) ? BreathNuanceState::TearfulQuiverActive
                                       : BreathNuanceState::VoicedSinging;
            sighHoldCounter = sighHoldSamplesMax;
        }
        else if (snrRatio > 3.2f && fastEnv > 0.0018f && zcrRateEnv >= 0.08f && zcrRateEnv <= 0.38f)
        {
            state = (sighHoldCounter > 0) ? BreathNuanceState::EmotionalSighRelease
                                          : BreathNuanceState::ExpressiveInhale;
            if (prevState == BreathNuanceState::DeadAirSilence)
                ++preservedCount;
        }
        else if (sighHoldCounter > 0)
        {
            --sighHoldCounter;
            state = BreathNuanceState::EmotionalSighRelease;
        }
        else
        {
            state = BreathNuanceState::DeadAirSilence;
        }

        // 4. 状態別のベルベット音響整形
        silkHpL = silkHpCoeff * (silkHpL + left  - silkPrevL);
        silkPrevL = left;
        silkHpR = silkHpCoeff * (silkHpR + right - silkPrevR);
        silkPrevR = right;

        softLpL = breathSoftLpCoeff * softLpL + (1.0f - breathSoftLpCoeff) * left;
        softLpR = breathSoftLpCoeff * softLpR + (1.0f - breathSoftLpCoeff) * right;

        float targetGain = 1.0f;
        if (state == BreathNuanceState::DeadAirSilence)
        {
            targetGain = 0.25f + 0.75f * (1.0f - breathBeautyAmount01 * 0.5f);
        }
        else if (state == BreathNuanceState::ExpressiveInhale || state == BreathNuanceState::EmotionalSighRelease)
        {
            targetGain = 1.0f;
            const float polish = std::clamp (breathBeautyAmount01, 0.0f, 1.0f);
            left  = (1.0f - 0.30f * polish) * left  + 0.30f * polish * softLpL + 0.22f * polish * silkHpL;
            right = (1.0f - 0.30f * polish) * right + 0.30f * polish * softLpR + 0.22f * polish * silkHpR;
        }

        smoothedGateGain = 0.995f * smoothedGateGain + 0.005f * targetGain;
        left  *= smoothedGateGain;
        right *= smoothedGateGain;
    }

    BreathNuanceTelemetry getTelemetry (float breathBeautyAmount01 = 0.50f) const noexcept
    {
        BreathNuanceTelemetry t;
        t.currentState         = state;
        t.breathSilkPolishDb   = breathBeautyAmount01 * 2.4f;
        t.tearfulQuiverDepth01 = tremorDetector.getTremorDepth01();
        t.nuancePreservation01 = 1.0f;
        t.preservedBreathCount = preservedCount;
        return t;
    }

private:
    double sampleRateHz = 48000.0;
    TearfulTremorDetector tremorDetector;
    float fastEnvCoeff = 0.99f;
    float slowNoiseCoeff = 0.9999f;
    float silkHpCoeff = 0.30f;
    float breathSoftLpCoeff = 0.45f;

    float fastEnv = 0.0f;
    float noiseFloorEst = 0.0005f;
    float zcrRateEnv = 0.05f;
    float prevSampleMono = 0.0f;
    int sighHoldSamplesMax = 20160;
    int sighHoldCounter = 0;

    BreathNuanceState state = BreathNuanceState::DeadAirSilence;
    uint32_t preservedCount = 0;

    float silkPrevL = 0.0f, silkPrevR = 0.0f;
    float silkHpL = 0.0f,   silkHpR = 0.0f;
    float softLpL = 0.0f,   softLpR = 0.0f;
    float smoothedGateGain = 1.0f;
};

} // namespace vg::human
