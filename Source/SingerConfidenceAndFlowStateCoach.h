#pragma once
// ==============================================================================
//  SingerConfidenceAndFlowStateCoach.h
//  VocalGzzio — 喉の力み（喉締め・仮声帯過収縮）自動レスキュー・声帯疲労ドーズ監視 ＆ 歌手フロー状態（ゾーン突入）支援エンジン
//
//  【歌い手の欲求：「高音で喉が締まって苦しい時でも、楽に伸びやかに歌いたい」】
//   長時間配信や高音サビで歌い手が「もっと声を出さなきゃ」と力む（Laryngeal Hyperfunction / 喉締め）と、
//   仮声帯の収縮によって 1.65 kHz 〜 2.15 kHz に特有の硬い狭窄ピーク（Pressed Phonation Peak）が発生し、
//   逆に 180 Hz 〜 320 Hz の胸声の基礎共鳴（Fundamental Chest Support）が抜けて細く苦しそうな声になる。
//
//   本エンジンは:
//   1. **1.88 kHz 狭窄エネルギー 対 250 Hz 胸声基礎エネルギーの比（Pressed-to-Flow Ratio）検出器**:
//      「流暢な発声（Flow Phonation）」と「力んだ発声（Pressed Phonation）」をリアルタイム識別する。
//   2. **声帯接触衝撃ドーズ（Vocal Fold Collision Dose）トラッカー (`VocalHealthDosimeter`)**:
//      長時間歌唱における喉の累積負担を追跡し、力みが続く場合は自動で上向きコンプレッション支援（Upward Effort Relief）を強める。
//   3. 力みが検知された瞬間、1.88 kHz の苦しそうな硬さを動的にクッション緩和しつつ、
//      抜けてしまった 250 Hz の支えと 3.1 kHz の倍音の伸びを自動で補填する。
// ==============================================================================

#include "LightweightSimdFastMathAndEcoGovernor.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace vg::human
{

// ==============================================================================
//  1. 声帯接触疲労ドーズ（Vocal Fold Collision & Stamina Dosimeter）監視クラス
// ==============================================================================
class VocalStaminaAndHealthDosimeter
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double safeFs = std::max (8000.0, sampleRate);
        // 長期疲労蓄積時定数（4.0秒）と休息回復時定数（1.5秒）
        accumCoeff   = static_cast<float> (std::exp (-1.0 / (4.000 * safeFs)));
        recoverCoeff = static_cast<float> (std::exp (-1.0 / (1.500 * safeFs)));
        reset();
    }

    void reset() noexcept
    {
        cumulativeStrainDose01 = 0.0f;
    }

    inline float updateStaminaDose (float instantStrain01, bool isSingingNow) noexcept
    {
        if (isSingingNow && instantStrain01 > 0.25f)
            cumulativeStrainDose01 = accumCoeff * cumulativeStrainDose01 + (1.0f - accumCoeff) * instantStrain01;
        else
            cumulativeStrainDose01 = recoverCoeff * cumulativeStrainDose01;

        return std::clamp (cumulativeStrainDose01, 0.0f, 1.0f);
    }

    float getVocalStaminaRemainingPct() const noexcept
    {
        return std::clamp (100.0f - cumulativeStrainDose01 * 45.0f, 50.0f, 100.0f);
    }

private:
    float accumCoeff   = 0.99999f;
    float recoverCoeff = 0.9999f;
    float cumulativeStrainDose01 = 0.0f;
};

// ==============================================================================
//  2. 喉締め自動レスキュー ＆ 歌手フロー状態（ゾーン突入）支援統合エンジン
// ==============================================================================
struct FlowStateCoachTelemetry
{
    float throatStrainIndex01         = 0.0f;  // 0.0 = 完全リラックス（美声フロー）, 1.0 = 強い力み検出
    float autoEffortReliefDb          = 0.0f;  // 喉締め緩和・支え補填量 (dB)
    float vocalStaminaRemainingPct    = 98.5f; // 喉のスタミナ残量指標 (0..100%)
    float singerFlowConfidencePct     = 95.0f; // フロー状態・歌唱快適度スコア (0..100%)
    const char* producerEncouragementJa = "息の通りが抜群です！そのままリラックスして歌いましょう";
};

class SingerConfidenceAndFlowStateCoach
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRateHz = std::max (8000.0, sampleRate);

        // 1. 喉締め狭窄検出フィルタ (f0 = 1880 Hz, Q = 2.5)
        setupBandpass (strainBand, 1880.0, 2.5, sampleRateHz);

        // 2. 胸声・声帯閉鎖の基礎支えフィルタ (f0 = 250 Hz, Q = 1.3)
        setupBandpass (supportBand, 250.0, 1.3, sampleRateHz);

        // 3. 声門フロー倍音解放フィルタ (f0 = 3120 Hz, Q = 2.2: 力を抜いても前に飛ぶ帯域)
        setupBandpass (projectionBand, 3120.0, 2.2, sampleRateHz);

        staminaDosimeter.prepare (sampleRateHz);
        envCoeff = static_cast<float> (std::exp (-1.0 / (0.045 * sampleRateHz)));
        reset();
    }

    void reset() noexcept
    {
        strainBand.reset();
        supportBand.reset();
        projectionBand.reset();
        staminaDosimeter.reset();
        supportSatL.reset();
        supportSatR.reset();
        strainEnv  = 0.0001f;
        supportEnv = 0.0005f;
        smoothedStrain01 = 0.0f;
    }

    inline void processStereo (float& left, float& right, float reliefAssist01 = 0.50f) noexcept
    {
        if (reliefAssist01 <= 0.001f)
            return;

        const float assist = std::clamp (reliefAssist01, 0.0f, 1.0f);

        const float stL = strainBand.processL (left);
        const float stR = strainBand.processR (right);
        const float spL = supportBand.processL (left);
        const float spR = supportBand.processR (right);
        const float prL = projectionBand.processL (left);
        const float prR = projectionBand.processR (right);

        const float stAbs = 0.5f * (std::fabs (stL) + std::fabs (stR));
        const float spAbs = 0.5f * (std::fabs (spL) + std::fabs (spR));

        strainEnv  = envCoeff * strainEnv  + (1.0f - envCoeff) * stAbs;
        supportEnv = envCoeff * supportEnv + (1.0f - envCoeff) * spAbs;

        // 胸の支えに対して 1.88kHz の狭窄成分が突出しているほど「高音で喉が締まっている」と判定
        float rawStrain = 0.0f;
        const bool isSinging = (strainEnv + supportEnv) > 0.004f;
        if (isSinging)
        {
            const float ratio = strainEnv / std::max (0.001f, supportEnv);
            rawStrain = std::clamp ((ratio - 0.85f) * 0.55f, 0.0f, 1.0f);
        }

        smoothedStrain01 = 0.995f * smoothedStrain01 + 0.005f * rawStrain;
        staminaDosimeter.updateStaminaDose (smoothedStrain01, isSinging);

        // 250Hz 基礎支え成分にADAA温感倍音を付与して、喉に力を入れなくても太い芯を作る
        const float warmSupportL = supportSatL.processSample (spL, 1.28f);
        const float warmSupportR = supportSatR.processSample (spR, 1.28f);

        // 力み検知時：1.88kHz の硬い苦しさを引き算し、250Hz の温かい支え＋3.12kHz の遠達性を補填する
        const float cushionCut = assist * smoothedStrain01 * 0.34f;
        const float supportAdd = assist * smoothedStrain01 * 0.28f;
        const float projAdd    = assist * smoothedStrain01 * 0.14f;

        left  = left  - cushionCut * stL + supportAdd * warmSupportL + projAdd * prL;
        right = right - cushionCut * stR + supportAdd * warmSupportR + projAdd * prR;
    }

    FlowStateCoachTelemetry getTelemetry (float reliefAssist01 = 0.50f) const noexcept
    {
        FlowStateCoachTelemetry t;
        t.throatStrainIndex01      = smoothedStrain01;
        t.autoEffortReliefDb       = reliefAssist01 * smoothedStrain01 * 3.1f;
        t.vocalStaminaRemainingPct = staminaDosimeter.getVocalStaminaRemainingPct();
        t.singerFlowConfidencePct  = std::clamp (100.0f - smoothedStrain01 * (1.0f - 0.65f * reliefAssist01) * 35.0f, 60.0f, 100.0f);

        if (smoothedStrain01 < 0.20f)
            t.producerEncouragementJa = "息の通りが抜群です！そのままリラックスして歌いましょう";
        else if (smoothedStrain01 < 0.50f)
            t.producerEncouragementJa = "高音の伸びが美しいです！胸の支えを自動アシスト中";
        else
            t.producerEncouragementJa = "高音レスキュー発動中：力を抜いても声が前に飛ぶよう支えています";

        return t;
    }

private:
    struct StereoBP
    {
        float b0 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1L = 0.0f, z2L = 0.0f, z1R = 0.0f, z2R = 0.0f;
        void reset() noexcept { z1L = z2L = z1R = z2R = 0.0f; }

        inline float processL (float x) noexcept
        {
            const float y = b0 * x + z1L;
            z1L = -a1 * y + z2L;
            z2L = b2 * x - a2 * y;
            return y;
        }
        inline float processR (float x) noexcept
        {
            const float y = b0 * x + z1R;
            z1R = -a1 * y + z2R;
            z2R = b2 * x - a2 * y;
            return y;
        }
    };

    static void setupBandpass (StereoBP& bq, double freqHz, double q, double fs) noexcept
    {
        const double safeF = std::clamp (freqHz, 20.0, fs * 0.45);
        const double w0    = 2.0 * 3.14159265358979323846 * safeF / fs;
        const double alpha = std::sin (w0) / (2.0 * std::max (0.2, q));
        const double a0    = 1.0 + alpha;
        bq.b0 = static_cast<float> ( alpha / a0);
        bq.b2 = static_cast<float> (-alpha / a0);
        bq.a1 = static_cast<float> ((-2.0 * std::cos (w0)) / a0);
        bq.a2 = static_cast<float> ((1.0 - alpha) / a0);
    }

    double sampleRateHz = 48000.0;
    StereoBP strainBand;
    StereoBP supportBand;
    StereoBP projectionBand;
    VocalStaminaAndHealthDosimeter staminaDosimeter;
    vg::lightweight::Adaa1TanhSaturator supportSatL;
    vg::lightweight::Adaa1TanhSaturator supportSatR;

    float envCoeff = 0.999f;
    float strainEnv = 0.0001f;
    float supportEnv = 0.0005f;
    float smoothedStrain01 = 0.0f;
};

} // namespace vg::human
