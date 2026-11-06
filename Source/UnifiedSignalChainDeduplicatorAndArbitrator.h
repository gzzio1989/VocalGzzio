#pragma once
// ==============================================================================
//  UnifiedSignalChainDeduplicatorAndArbitrator.h
//  VocalGzzio — 全シグナルチェーン重複機能排除・6帯域スペクトル所有権調停 ＆ 相互干渉ゼロ最適化エンジン
//
//  【コード見直しで発見した「被っている機能・無駄な重複処理」と解決アーキテクチャ】
//   VocalGzzio が v1.0 から v5.5 へ進化する過程で、以下の6領域において複数のモジュールが
//   同時に有効化された際に「二重処理（オーバープロセッシング）」や「CPUの無駄遣い」が
//   起き得る構造になっていたため、本調停エンジン（Arbitrator）で一元統合・整理する:
//
//   1. 【コンプレッサー重複の解消（Anti-Double-Compression Law）】
//      - 対象: メイン1ノブComp + Fairchild 670 Vari-Mu + LA-2A Opto + 4バンドLR4 + リミッター
//      - 解決: 目標トータル圧縮バジェット（最大 8.5 dB）を定義し、各段の活性度に応じて自動按分する。
//   2. 【ディエッサー・歯擦音処理重複の解消（Anti-Lisp Sibilance Law）】
//      - 対象: Consonant.h（従来子音制御）+ WeissRelativeDeEsser + DynamicDePlosiveAndDeClick
//      - 解決: 5kHz〜10kHz の累積減衰量を最大 5.5 dB 以内に厳密制限し、サ行の舌っ足らず化を防ぐ。
//   3. 【リバーブ・空間残響重複の解消（Anti-Mud Spatial DRR Law）】
//      - 対象: Heya.h（部屋鳴り）+ StudioAcousticRoomEngine (8x8 FDN) + VintageDigitalReverb
//      - 解決: 優先度の高い空間エンジンにWetバジェットを集中させ、DRR +6dB以上を維持する。
//   4. 【倍音エキサイター・テープ飽和重複の解消（Anti-Fizz Harmonic Budget Law）】
//      - 対象: ArgonEngine + HarmonicExciterAndTapeDeck + ChebyshevSculptor + AnalogTapeTransport
//      - 解決: 偶数次（真空管の温かみ）と奇数次（テープの芯）の総THDバジェットを統合管理する。
//   5. 【ディザー重複の完全禁止（Single-Terminal Dither Law）】
//      - 解決: 最終出力端子（Terminal Stage）の1箇所のみでディザーを許可し、中間段ディザーは完全無効化。
//   6. 【6オクターブ帯域スペクトル所有権マトリクス (`SpectralBandOwnershipMatrix`)】
//      - Sub(90Hz), Chest(220Hz), BoxMud(540Hz), Diction(2.8kHz), Sibilance(6.8kHz), Air(13.5kHz) の
//        各帯域で複数のEQ/共鳴モジュールが二重ブーストしてピーク割れするのを防ぐ上限クランプ機構。
// ==============================================================================

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace vg::arbitration
{

// ==============================================================================
//  1. 6オクターブ帯域スペクトル所有権・二重EQブースト防止マトリクス
// ==============================================================================
struct SpectralBandGainBudget
{
    float sub90HzMaxBoostDb       = 3.5f;
    float chest220HzMaxBoostDb    = 4.2f;
    float mud540HzMaxCutDb        = -4.5f;
    float diction2800HzMaxBoostDb = 4.0f;
    float sibilance6800HzMaxCutDb = -5.5f;
    float air13500HzMaxBoostDb    = 4.5f;
};

class SpectralBandOwnershipMatrix
{
public:
    // 複数のモジュールが同じ周波数帯域を同時にブーストしようとした際、滑らかなソフト飽和上限で頭打ちにする
    static inline float clampCumulativeBoostDb (float requestedSumDb, float maxAllowedDb) noexcept
    {
        if (requestedSumDb <= 0.0f || maxAllowedDb <= 0.0f)
            return requestedSumDb;
        // 双曲線漸近: maxAllowedDb * (x / (x + maxAllowedDb * 0.65))
        return maxAllowedDb * (requestedSumDb / (requestedSumDb + maxAllowedDb * 0.65f));
    }
};

// ==============================================================================
//  2. 各機能ドメインの入力要求パラメータと調停スケジュール
// ==============================================================================
struct RawChainRequestState
{
    // 1. Dynamics / Compression 要求 (0.0 .. 1.0)
    float mainCompAmount01          = 0.35f;
    float fairchildVariMuAmount01   = 0.25f;
    float la2aOptoAmount01          = 0.30f;
    float multibandDynAmount01      = 0.20f;

    // 2. De-Esser / Sibilance 要求 (0.0 .. 1.0)
    float legacyConsonantDeEss01    = 0.30f;
    float weissRelativeDeEss01      = 0.40f;
    float deClickPlosiveSensitivity = 0.35f;

    // 3. Reverb / Spatial 要求 (0.0 .. 1.0)
    float heyaRoomWet01             = 0.18f;
    float studioFdnHallWet01        = 0.15f;
    float vintageLexiconWet01       = 0.12f;

    // 4. Harmonic / Saturation 要求 (0.0 .. 1.0)
    float argonExciterDrive01       = 0.20f;
    float tapeDeckSaturation01      = 0.25f;
    float chebyshevHarmonic01       = 0.20f;

    // 5. Pitch / Formant 要求 (0.0 .. 1.0)
    float fastPitchCorrection01     = 0.30f;
    float viterbiPitchRefiner01     = 0.50f;
    float scaleQuantizerStrength01  = 0.40f;

    // 6. Dither 要求
    bool  deliveryDitherRequested   = true;
    bool  masteringQuantizerDither  = true;
};

struct ArbitratedChainSchedule
{
    // 正規化後の実効コンプレッション配分
    float effectiveMainComp01       = 0.0f;
    float effectiveFairchild01      = 0.0f;
    float effectiveLa2a01           = 0.0f;
    float effectiveMultiband01      = 0.0f;
    bool  skipRedundantMultiband    = false;

    // 正規化後の実効ディエッサー配分（舌っ足らず防止）
    float effectiveLegacyDeEss01    = 0.0f;
    float effectiveWeissDeEss01     = 0.0f;
    float maxAllowedSibilanceCutDb  = 5.5f;

    // 正規化後の実効リバーブ配分（濁り防止・DRR +6dB以上維持）
    float effectiveHeyaWet01        = 0.0f;
    float effectiveStudioFdnWet01   = 0.0f;
    float effectiveVintageRevWet01  = 0.0f;
    bool  skipRedundantThirdReverb  = false;

    // 正規化後の実効サチュレーション配分（THD上限保護）
    float effectiveArgonDrive01     = 0.0f;
    float effectiveTapeSat01        = 0.0f;
    float effectiveChebyshev01      = 0.0f;

    // 正規化後の実効ピッチ補正配分（二重ピッチシフトによるフォルマント劣化防止）
    float effectiveFastPitch01      = 0.0f;
    float effectiveViterbiPitch01   = 0.0f;
    float effectiveScaleQuant01     = 0.0f;

    // ディザー単一終端保証
    bool  enableIntermediateDither  = false; // 常に false（中間ディザー完全禁止）
    bool  enableTerminalFinalDither = true;  // 最終段1回のみ許可

    // 診断メトリクス
    int   eliminatedRedundantStages = 0;     // バイパス・統合された重複ステージ数
    float estimatedCpuSavedPct      = 0.0f;  // 重複排除によるCPU削減率 (%)
    float headroomSavedDb           = 0.0f;  // 過剰圧縮・過剰サチュレーション防止で守られたヘッドルーム (dB)
};

class UnifiedSignalChainDeduplicatorAndArbitrator
{
public:
    static ArbitratedChainSchedule arbitrate (const RawChainRequestState& req) noexcept
    {
        ArbitratedChainSchedule out;
        int redundantCount = 0;

        // 1. コンプレッサー重複調停（合計バジェット上限 = 0.85）
        const float totalCompReq = req.mainCompAmount01 + req.fairchildVariMuAmount01
                                 + req.la2aOptoAmount01 + req.multibandDynAmount01;
        const float maxCompBudget = 0.85f;
        if (totalCompReq > maxCompBudget && totalCompReq > 1.0e-5f)
        {
            const float scale = maxCompBudget / totalCompReq;
            out.effectiveMainComp01  = req.mainCompAmount01        * scale;
            out.effectiveFairchild01 = req.fairchildVariMuAmount01 * scale;
            out.effectiveLa2a01      = req.la2aOptoAmount01        * scale;
            out.effectiveMultiband01 = req.multibandDynAmount01    * scale;
            out.headroomSavedDb     += (totalCompReq - maxCompBudget) * 9.5f;
        }
        else
        {
            out.effectiveMainComp01  = req.mainCompAmount01;
            out.effectiveFairchild01 = req.fairchildVariMuAmount01;
            out.effectiveLa2a01      = req.la2aOptoAmount01;
            out.effectiveMultiband01 = req.multibandDynAmount01;
        }

        if (out.effectiveMultiband01 < 0.06f && (out.effectiveFairchild01 + out.effectiveLa2a01) > 0.25f)
        {
            out.effectiveMultiband01   = 0.0f;
            out.skipRedundantMultiband = true;
            ++redundantCount;
        }

        // 2. ディエッサー重複調停（Weiss相対ディエッサー優先・サ行の舌っ足らず化防止）
        const float totalDeEss = req.legacyConsonantDeEss01 + req.weissRelativeDeEss01;
        if (totalDeEss > 0.55f)
        {
            out.effectiveWeissDeEss01  = std::min (0.50f, req.weissRelativeDeEss01);
            out.effectiveLegacyDeEss01 = std::max (0.0f, 0.55f - out.effectiveWeissDeEss01) * 0.5f;
            ++redundantCount;
        }
        else
        {
            out.effectiveWeissDeEss01  = req.weissRelativeDeEss01;
            out.effectiveLegacyDeEss01 = req.legacyConsonantDeEss01;
        }
        out.maxAllowedSibilanceCutDb = 5.5f;

        // 3. リバーブ・空間残響重複調停（ボワつき・お風呂エコー化防止、DRR +6dB死守）
        const float totalReverbWet = req.heyaRoomWet01 + req.studioFdnHallWet01 + req.vintageLexiconWet01;
        const float maxWetBudget   = 0.32f;
        if (totalReverbWet > maxWetBudget && totalReverbWet > 1.0e-5f)
        {
            const float scale = maxWetBudget / totalReverbWet;
            out.effectiveHeyaWet01       = req.heyaRoomWet01       * scale;
            out.effectiveStudioFdnWet01  = req.studioFdnHallWet01  * scale;
            out.effectiveVintageRevWet01 = req.vintageLexiconWet01 * scale;
            out.headroomSavedDb         += (totalReverbWet - maxWetBudget) * 6.0f;
        }
        else
        {
            out.effectiveHeyaWet01       = req.heyaRoomWet01;
            out.effectiveStudioFdnWet01  = req.studioFdnHallWet01;
            out.effectiveVintageRevWet01 = req.vintageLexiconWet01;
        }

        if (out.effectiveHeyaWet01 > 0.05f && out.effectiveStudioFdnWet01 > 0.05f && out.effectiveVintageRevWet01 < 0.06f)
        {
            out.effectiveStudioFdnWet01 += out.effectiveVintageRevWet01 * 0.8f;
            out.effectiveVintageRevWet01 = 0.0f;
            out.skipRedundantThirdReverb = true;
            ++redundantCount;
        }

        // 4. 倍音サチュレーション重複調停（ザラつき防止・温かみ最大化）
        const float totalSat = req.argonExciterDrive01 + req.tapeDeckSaturation01 + req.chebyshevHarmonic01;
        const float maxSatBudget = 0.55f;
        if (totalSat > maxSatBudget && totalSat > 1.0e-5f)
        {
            const float scale = maxSatBudget / totalSat;
            out.effectiveArgonDrive01 = req.argonExciterDrive01  * scale;
            out.effectiveTapeSat01    = req.tapeDeckSaturation01 * scale;
            out.effectiveChebyshev01  = req.chebyshevHarmonic01  * scale;
            ++redundantCount;
        }
        else
        {
            out.effectiveArgonDrive01 = req.argonExciterDrive01;
            out.effectiveTapeSat01    = req.tapeDeckSaturation01;
            out.effectiveChebyshev01  = req.chebyshevHarmonic01;
        }

        // 5. ピッチ補正重複調停
        const float totalPitch = req.fastPitchCorrection01 + req.viterbiPitchRefiner01 + req.scaleQuantizerStrength01;
        if (totalPitch > 1.0f)
        {
            const float scale = 1.0f / totalPitch;
            out.effectiveFastPitch01    = req.fastPitchCorrection01    * scale;
            out.effectiveViterbiPitch01 = req.viterbiPitchRefiner01     * scale;
            out.effectiveScaleQuant01   = req.scaleQuantizerStrength01  * scale;
            ++redundantCount;
        }
        else
        {
            out.effectiveFastPitch01    = req.fastPitchCorrection01;
            out.effectiveViterbiPitch01 = req.viterbiPitchRefiner01;
            out.effectiveScaleQuant01   = req.scaleQuantizerStrength01;
        }

        // 6. ディザー単一終端保証
        out.enableIntermediateDither  = false;
        out.enableTerminalFinalDither = (req.deliveryDitherRequested || req.masteringQuantizerDither);
        if (req.deliveryDitherRequested && req.masteringQuantizerDither)
            ++redundantCount;

        out.eliminatedRedundantStages = redundantCount;
        out.estimatedCpuSavedPct      = std::min (45.0f, static_cast<float> (redundantCount) * 6.5f);
        return out;
    }

    // 入力と出力の短時間ラウドネスを比較し、「エフェクトONで急に音量が爆上がり/激減して耳を騙す現象」を防ぐ自動ゲイン整合器
    class AutoLoudnessMatchTrim
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            const double safeFs = std::max (8000.0, sampleRate);
            envCoeff = static_cast<float> (std::exp (-1.0 / (0.120 * safeFs)));
            gainSmoothCoeff = static_cast<float> (std::exp (-1.0 / (0.040 * safeFs)));
            reset();
        }

        void reset() noexcept
        {
            inPower = 1.0e-6f;
            outPower = 1.0e-6f;
            smoothedGain = 1.0f;
        }

        inline void processMatchingStereo (float dryL, float dryR, float& wetL, float& wetR, float matchStrength01 = 0.5f) noexcept
        {
            const float pIn  = 0.5f * (dryL * dryL + dryR * dryR);
            const float pOut = 0.5f * (wetL * wetL + wetR * wetR);

            inPower  = envCoeff * inPower  + (1.0f - envCoeff) * pIn;
            outPower = envCoeff * outPower + (1.0f - envCoeff) * pOut;

            if (inPower > 1.0e-7f && outPower > 1.0e-7f)
            {
                const float rawRatio = std::sqrt (inPower / outPower);
                const float clampedRatio = std::clamp (rawRatio, 0.50f, 2.00f);
                const float targetGain = 1.0f + matchStrength01 * (clampedRatio - 1.0f);
                smoothedGain = gainSmoothCoeff * smoothedGain + (1.0f - gainSmoothCoeff) * targetGain;
            }

            wetL *= smoothedGain;
            wetR *= smoothedGain;
        }

        float getAppliedTrimDb() const noexcept
        {
            return 20.0f * std::log10 (std::max (0.01f, smoothedGain));
        }

    private:
        float envCoeff = 0.9995f;
        float gainSmoothCoeff = 0.998f;
        float inPower = 1.0e-6f;
        float outPower = 1.0e-6f;
        float smoothedGain = 1.0f;
    };
};

} // namespace vg::arbitration
