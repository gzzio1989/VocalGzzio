#pragma once
// ==============================================================================
//  IntelligentGainStagingAndLoudnessNormalizer.h
//  VocalGzzio — 0 VU (-18 dBFS RMS) 自動スイートスポット・ゲインステージング・吹かれ耐性重み付け ＆ ゼロ交差補間エンジン
//
//  【コード見直し・初心者のつまずき解消：「マイク入力が小さすぎ／大きすぎても常に最高の音質を引き出す」】
//   自宅配信者や宅録ユーザーの多くは、オーディオインターフェースのゲイン設定が最適値から外れている
//   （例: ダイナミックマイクで -34 dBFS しか入っていない、逆にコンデンサーで -2 dBFS まで突っ込んでいる）。
//   アナログモデリング（真空管・光学コンプ・テープ）は「0 VU = -18 dBFS RMS」を基準に設計されているため、
//   入力が小さすぎるとエフェクトが全く掛からず、大きすぎると歪んでしまう。
//
//   本エンジンは3つの高度なサブシステムで構成される:
//   1. **吹かれ（Plosive）耐性付き K加重デュアルウィンドウ RMS 検出器 (`PlosiveImmuneDualWindowDetector`)**:
//      パ行・バ行の突発的な低域風圧（< 90Hz）に騙されてゲインが急락するのを防ぐため、
//      検出サイドチェーンに 110Hz ハイパスと 1.5kHz K加重シェルフを通して「人間の耳が感じる声の芯の音量」だけを測定する。
//   2. **無音時完全フリーズ機構 (`SilenceFreezeHysteresis`)**:
//      -50 dBFS 以下の無音・ブレス待機中はゲインライダーを完全ホールド（Freeze）し、
//      無音時に部屋のサーッというホワイトノイズやPCファン音が勝手に持ち上がる事故を 100% 阻止する。
//   3. **ゼロ交差（Zero-Crossing）協調型スルーレート補間器 (`ZeroCrossingSmoothGainStage`)**:
//      波形がゼロ点を横切る瞬間に優先してゲインステップを更新し、振幅変化に伴う位相変調歪みを極小化する。
// ==============================================================================

#include "LightweightSimdFastMathAndEcoGovernor.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace vg::human
{

// ==============================================================================
//  1. 吹かれ耐性付き K加重デュアルウィンドウ（短期400ms / 中期2.5秒）レベル検出器
// ==============================================================================
class PlosiveImmuneDualWindowDetector
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double safeFs = std::max (8000.0, sampleRate);
        // サイドチェーン用 115Hz ハイパス（マイクの吹かれ風圧でゲインが下がるのを防ぐ）
        scHp115Coeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * 115.0 / safeFs));
        // 1.6kHz 聴覚感度プレゼンスLPF（ハイシェルフ差分用）
        scLp1600Coeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * 1600.0 / safeFs));

        momentary400msCoeff = static_cast<float> (std::exp (-1.0 / (0.400 * safeFs)));
        shortTerm2500msCoeff = static_cast<float> (std::exp (-1.0 / (2.500 * safeFs)));
        reset();
    }

    void reset() noexcept
    {
        hpPrev = 0.0f;
        hpState = 0.0f;
        lpState = 0.0f;
        momentaryPower = 0.01585f; // -18.0 dBFS 相当
        shortTermPower = 0.01585f;
        voicedGateOpen = false;
    }

    inline float observeAndEstimateSweetSpotPower (float left, float right) noexcept
    {
        const float mono = 0.5f * (left + right);

        // 115Hz ハイパスでポップノイズ風圧を除去
        hpState = scHp115Coeff * (hpState + mono - hpPrev);
        hpPrev  = mono;

        // 1.6kHz 以上の声の明瞭度帯域に +3.5dB 重み付け
        lpState = scLp1600Coeff * lpState + (1.0f - scLp1600Coeff) * hpState;
        const float highPart = hpState - lpState;
        const float weighted = lpState + 1.48f * highPart;

        const float weightedSq = weighted * weighted;

        // シュミットトリガ型有声判定（開閾値 -48dBFS, 閉閾値 -53dBFS）
        if (! voicedGateOpen && weightedSq > 1.58e-5f)
            voicedGateOpen = true;
        else if (voicedGateOpen && weightedSq < 5.01e-6f)
            voicedGateOpen = false;

        if (voicedGateOpen)
        {
            momentaryPower = momentary400msCoeff  * momentaryPower + (1.0f - momentary400msCoeff)  * weightedSq;
            shortTermPower = shortTerm2500msCoeff * shortTermPower + (1.0f - shortTerm2500msCoeff) * weightedSq;
        }

        // 短期（40%）と中期（60%）のブレンドで自然な抑揚を残しつつ基準レベルを安定化
        return 0.40f * momentaryPower + 0.60f * shortTermPower;
    }

    bool  isVoicedActive() const noexcept { return voicedGateOpen; }
    float getMomentaryPower() const noexcept { return momentaryPower; }
    float getShortTermPower() const noexcept { return shortTermPower; }

private:
    float scHp115Coeff         = 0.985f;
    float scLp1600Coeff        = 0.81f;
    float momentary400msCoeff  = 0.9999f;
    float shortTerm2500msCoeff = 0.99998f;

    float hpPrev  = 0.0f;
    float hpState = 0.0f;
    float lpState = 0.0f;
    float momentaryPower = 0.01585f;
    float shortTermPower = 0.01585f;
    bool  voicedGateOpen = false;
};

// ==============================================================================
//  2. ゼロ交差（Zero-Crossing）協調型スルーレート・ゲイン補間器
// ==============================================================================
class ZeroCrossingSmoothGainStage
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double safeFs = std::max (8000.0, sampleRate);
        normalSmoothCoeff = static_cast<float> (std::exp (-1.0 / (0.090 * safeFs)));
        zcFastSmoothCoeff = static_cast<float> (std::exp (-1.0 / (0.012 * safeFs)));
        reset();
    }

    void reset() noexcept
    {
        currentGain = 1.0f;
        prevMono    = 0.0f;
    }

    inline float computeSampleGain (float left, float right, float targetGain) noexcept
    {
        const float mono = 0.5f * (left + right);
        const bool isZeroCrossing = ((mono >= 0.0f && prevMono < 0.0f) || (mono < 0.0f && prevMono >= 0.0f));
        prevMono = mono;

        // ゼロ交差瞬間は歪みが発生しないため 12ms 係数で素早く追従、非ゼロ交差時は 90ms で超滑らかに補間
        const float coeff = isZeroCrossing ? zcFastSmoothCoeff : normalSmoothCoeff;
        currentGain = coeff * currentGain + (1.0f - coeff) * targetGain;
        return currentGain;
    }

    float getCurrentGainLinear() const noexcept { return currentGain; }

private:
    float normalSmoothCoeff = 0.9995f;
    float zcFastSmoothCoeff = 0.996f;
    float currentGain       = 1.0f;
    float prevMono          = 0.0f;
};

// ==============================================================================
//  3. 統合 0 VU (-18 dBFS RMS) 自動スイートスポット・ゲインステージング＆ラウドネス正規化器
// ==============================================================================
struct GainStagingTelemetry
{
    float measuredVocalRmsDb  = -24.0f;
    float targetSweetSpotDb   = -18.0f;
    float appliedAutoGainDb   = 0.0f;
    bool  isFrozenOnSilence   = true;
};

class IntelligentGainStagingAndLoudnessNormalizer
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRateHz = std::max (8000.0, sampleRate);
        detector.prepare (sampleRateHz);
        gainInterpolator.prepare (sampleRateHz);
        reset();
    }

    void reset() noexcept
    {
        detector.reset();
        gainInterpolator.reset();
        gatedRmsSq = 0.01585f;
        currentGainLinear = 1.0f;
        frozenOnSilence = true;
    }

    inline void processStereo (float& left, float& right, float autoGainStrength01 = 0.50f) noexcept
    {
        if (autoGainStrength01 <= 0.001f)
            return;

        const float strength = std::clamp (autoGainStrength01, 0.0f, 1.0f);
        gatedRmsSq      = detector.observeAndEstimateSweetSpotPower (left, right);
        frozenOnSilence = ! detector.isVoicedActive();

        // 目標スイートスポット: -18.0 dBFS RMS (linear = 0.12589254f, sq = 0.015849f)
        constexpr float kTargetRmsLinear = 0.12589254f;
        const float currentRmsLinear = std::sqrt (std::max (1.0e-7f, gatedRmsSq));

        // 最大 ±8.5 dB (0.375x .. 2.66x) の安全範囲で目標補正ゲインを計算
        const float rawRatio = std::clamp (kTargetRmsLinear / currentRmsLinear, 0.375f, 2.660f);
        const float targetGain = 1.0f + strength * (rawRatio - 1.0f);

        currentGainLinear = gainInterpolator.computeSampleGain (left, right, targetGain);
        left  *= currentGainLinear;
        right *= currentGainLinear;
    }

    GainStagingTelemetry getTelemetry() const noexcept
    {
        GainStagingTelemetry t;
        t.measuredVocalRmsDb = vg::lightweight::FastMathKernel::fastLinearToDb (std::sqrt (std::max (1.0e-9f, gatedRmsSq)));
        t.targetSweetSpotDb  = -18.0f;
        t.appliedAutoGainDb  = vg::lightweight::FastMathKernel::fastLinearToDb (currentGainLinear);
        t.isFrozenOnSilence  = frozenOnSilence;
        return t;
    }

private:
    double sampleRateHz = 48000.0;
    PlosiveImmuneDualWindowDetector detector;
    ZeroCrossingSmoothGainStage     gainInterpolator;
    float gatedRmsSq = 0.01585f;
    float currentGainLinear = 1.0f;
    bool frozenOnSilence = true;
};

} // namespace vg::human
