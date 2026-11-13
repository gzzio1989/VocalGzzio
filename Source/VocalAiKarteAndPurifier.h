#pragma once

// =============================================================================
//  VocalAiKarteAndPurifier.h — VocalGzzio v5.7.0
//  1. JPopVocalKarteAnalyzer:
//     J-Pop学・歌ってみた・音声学(Vocology: Sundberg Singer's Formant / H1-H2 / HNR)に基づく
//     無音免疫12帯域スペクトル＆倍音構造解析・12タイプ声質カルテ診断エンジン
//  2. AiVoicePurifierAndNoiseClassifier:
//     6種ノイズパターン指紋学習 ＋ 歌唱中も倍音の隙間ノイズだけを抜き出す
//     ゼロ遅延・ピッチ同期ハーモニック・コム声抽出 ＆ 語尾・吐息・子音保護エンジン
//  3. JPopAutoVocalPolishEngine:
//     新機能「J-Pop 完パケ艶＆喉締めオートリリーフ」
//     (1.08kHz鼻声イキミ＆3.88kHz喉締め痛覚の動的緩和 ＋ 有声母音限定2.95kHz/11.5kHz艶付与)
// =============================================================================

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace gz::v570
{

// -----------------------------------------------------------------------------
//  高速・リアルタイム安全なゼロ遅延 TPT 2次状態変数フィルタ (SVF)
//  カットオフ変調時も発散せず、ヒープ確保を一切行わない軽量実装
// -----------------------------------------------------------------------------
struct FastTptSvf
{
    double ic1eq = 0.0;
    double ic2eq = 0.0;
    double g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double m0 = 1.0, m1 = 0.0, m2 = 0.0;

    void reset() noexcept
    {
        ic1eq = 0.0;
        ic2eq = 0.0;
    }

    static double safeFreq (double fc, double sr) noexcept
    {
        const double maxF = std::max (200.0, sr * 0.44);
        return std::clamp (fc, 20.0, maxF);
    }

    void setBandPass (double sr, double fc, double Q) noexcept
    {
        const double safeSr = std::max (8000.0, sr);
        const double f = safeFreq (fc, safeSr);
        const double qSafe = std::clamp (Q, 0.25, 16.0);
        g  = std::tan (3.14159265358979323846 * f / safeSr);
        k  = 1.0 / qSafe;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        // 正規化バンドパス出力 (中心周波数ゲイン = 0 dB)
        m0 = 0.0;
        m1 = k;
        m2 = 0.0;
    }

    void setPeakBell (double sr, double fc, double Q, double gainDb) noexcept
    {
        const double safeSr = std::max (8000.0, sr);
        const double f = safeFreq (fc, safeSr);
        const double qSafe = std::clamp (Q, 0.25, 16.0);
        const double A = std::pow (10.0, std::clamp (gainDb, -24.0, 24.0) / 40.0);
        g  = std::tan (3.14159265358979323846 * f / safeSr);
        k  = 1.0 / (qSafe * A);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        m0 = 1.0;
        m1 = k * (A * A - 1.0);
        m2 = 0.0;
    }

    void setHighShelf (double sr, double fc, double Q, double gainDb) noexcept
    {
        const double safeSr = std::max (8000.0, sr);
        const double f = safeFreq (fc, safeSr);
        const double qSafe = std::clamp (Q, 0.25, 8.0);
        const double A = std::pow (10.0, std::clamp (gainDb, -18.0, 18.0) / 40.0);
        g  = std::tan (3.14159265358979323846 * f / safeSr) * std::sqrt (A);
        k  = 1.0 / qSafe;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        m0 = A * A;
        m1 = k * (1.0 - A) * A;
        m2 = 1.0 - A * A;
    }

    inline float processSample (float x) noexcept
    {
        const double v0 = (double) x;
        const double v3 = v0 - ic2eq;
        const double v1 = a1 * ic1eq + a2 * v3;
        const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0 * v1 - ic1eq;
        ic2eq = 2.0 * v2 - ic2eq;
        if (! std::isfinite (ic1eq) || std::abs (ic1eq) < 1.0e-20) ic1eq = 0.0;
        if (! std::isfinite (ic2eq) || std::abs (ic2eq) < 1.0e-20) ic2eq = 0.0;
        return (float) (m0 * v0 + m1 * v1 + m2 * v2);
    }
};

// =============================================================================
//  1. JPopVocalKarteAnalyzer — 12帯域×音声学特徴量 ＆ 12タイプ声質カルテ解析器
// =============================================================================
class JPopVocalKarteAnalyzer
{
public:
    static constexpr int kNumFineBands = 12;

    // J-Pop / 歌ってみた 12種類の声質カルテ・アーキタイプ
    enum Archetype : int
    {
        None                 = 0,
        WhisperTransparent   = 1,  // 01 透明ウィスパー・ASMR系
        ClearStandardJPop    = 2,  // 02 王道J-Popクリア・ストレート
        HighToneKawaii       = 3,  // 03 ハイトーン・カワボ／アイドル系
        HuskySmokyEmo        = 4,  // 04 ハスキー・スモーキーエモ系
        PowerfulBelting      = 5,  // 05 パワフル・ベルティング／アニソン系
        SilkyMixVoice        = 6,  // 06 シルキー・ミックスボイス系
        WarmLowBaritone      = 7,  // 07 ウォーム・イケボ／低音チェスト系
        FastSyllableVocalo   = 8,  // 08 ボカロカバー・高速譜割り系
        DynamicBallad        = 9,  // 09 ダイナミック・バラード表現系
        AcousticNatural      = 10, // 10 アコースティック・ナチュラル系
        HomeBoothBoxyFix     = 11, // 11 宅録ボックス鳴り・こもり集中補正
        HighPressureRock     = 12  // 12 ハイプレッシャー・ロック／シャウト系
    };

    struct KarteReport
    {
        int   archetype            = ClearStandardJPop;
        float meanF0Hz             = 220.0f;
        float pitchSpanSemitones   = 6.0f;
        float h1MinusH2Db          = 0.0f;   // 正: 息多め(開放的) / 負: 声門閉鎖強め(張り)
        float hnr01                = 0.70f;  // 1.0: 純粋倍音クリア / 低: ハスキー気流成分豊富
        float singerFormantRatioDb = 0.0f;   // 2.98kHz(抜け) vs 4.1kHz(刺さり) バランス
        float nasalIkimiExcessDb   = 0.0f;   // 1.12kHz 鼻声・喉締めイキミ突出量
        float boxinessExcessDb     = 0.0f;   // 400Hz 宅録箱鳴り突出量
        float sibilanceRatio       = 0.08f;  // 6.4kHz 歯擦音エネルギー比
        float breathAirRatio       = 0.05f;  // 10.2kHz+ 吐息エアー比
        float activeRmsDb          = -20.0f; // 無音・息継ぎを除いた純粋歌唱RMS
        float activeDynSdDb        = 3.0f;   // 歌唱フレーズ間の抑揚SD
        float transientDensity01   = 0.35f;  // 子音アタック密度

        // UI表示用 6軸カルテスコア (0.0 .. 1.0)
        float scoreClarity01       = 0.65f;  // 透明感・抜け
        float scoreWarmth01        = 0.55f;  // 温かみ・太さ
        float scoreBreathGloss01   = 0.50f;  // 吐息・エアリー艶
        float scoreCorePower01     = 0.60f;  // 声芯・音圧感
        float scoreSmoothness01    = 0.65f;  // なめらかさ・耳優しさ
        float scoreExpressive01    = 0.55f;  // 抑揚・表現レンジ

        // 自動最適化モジュール推奨フラグ
        bool  recUpwardAir         = false;
        bool  recBreathCtrl        = true;
        bool  recPitchSweet        = false;
        bool  recMixBridge         = false;
        bool  recVowelAlign        = false;
        bool  recOnsetGroove       = false;
        bool  recRoomDereverb      = false;
        bool  recCrushGuard        = false;
        int   recJpopPolishMode    = 1;      // 0:OFF, 1:歌枠ナチュラル艶, 2:J-Pop完パケ極艶
    };

    void prepare (double sampleRate) noexcept
    {
        sr = std::max (16000.0, sampleRate);
        // 音声学・J-Popミックスの急所をピンポイント分解する12帯域中心周波数
        static constexpr double kCentersHz[kNumFineBands] = {
            55.0,    // B0: <80Hz ランブル
            130.0,   // B1: 80-180Hz チェスト基音
            240.0,   // B2: 180-310Hz 近接ローミッド
            400.0,   // B3: 310-520Hz 宅録ボックス鳴り
            680.0,   // B4: 520-880Hz 日本語母音F1コア
            1120.0,  // B5: 880-1450Hz 鼻声・イキミ帯域
            1950.0,  // B6: 1450-2550Hz 言葉の子音輪郭F2
            2980.0,  // B7: 2550-3450Hz Sundberg シンガーズ・フォルマント
            4100.0,  // B8: 3450-4850Hz 外耳道共鳴・喉締めキンキン痛覚帯域
            6400.0,  // B9: 4850-8200Hz 日本語サ行・ツ行歯擦音
            10200.0, // B10: 8200-12800Hz シルク吐息ブレス
            14500.0  // B11: 12800Hz+ クリスタルエアー
        };
        static constexpr double kQs[kNumFineBands] = {
            1.1, 1.35, 1.45, 1.55, 1.45, 1.65, 1.45, 1.75, 1.85, 1.45, 1.35, 1.15
        };
        for (int b = 0; b < kNumFineBands; ++b)
        {
            bp[b].setBandPass (sr, kCentersHz[b], kQs[b]);
            bp[b].reset();
        }
        resetAccumulators();
        prepared = true;
    }

    void resetAccumulators() noexcept
    {
        for (auto& f : bp) f.reset();
        allBandEnergy.fill (0.0);
        voicedBandEnergy.fill (0.0);
        voicedSamples = 0;
        allSamples    = 0;
        voicedSumSq   = 0.0;
        voicedDbSum   = 0.0;
        voicedDbSqSum = 0.0;
        voicedBlocks  = 0;
        f0WeightedSum = 0.0;
        f0WeightTotal = 0.0;
        f0MinHz       = 2000.0f;
        f0MaxHz       = 0.0f;
        hnrSum        = 0.0;
        hnrCount      = 0;
        onsetCount    = 0;
        prevBlockDb   = -90.0f;
    }

    // オーディオスレッドから呼ばれる軽量・ゼロ確保の1ブロック特徴量蓄積
    void pushBlock (const float* ch0, const float* ch1, int numSamples,
                    float f0Hz, float f0Conf, float noiseFloorLinear) noexcept
    {
        if (! prepared || ch0 == nullptr || numSamples <= 0)
            return;

        double blockSq = 0.0;
        for (int n = 0; n < numSamples; ++n)
        {
            const float s0 = ch0[n];
            const float s1 = (ch1 != nullptr) ? ch1[n] : s0;
            // 片chのみ入力されているインターフェースでも正確に拾うためエネルギーの大きい方を評価
            const double e = std::max ((double) s0 * (double) s0, (double) s1 * (double) s1);
            blockSq += e;
        }
        const double meanSq = blockSq / (double) numSamples;
        const float blockDb = (float) (10.0 * std::log10 (meanSq + 1.0e-12));
        const float floorDb = (float) (20.0 * std::log10 (std::max ((double) noiseFloorLinear, 1.0e-6)));

        // 無音・ブレス休止区間を除外するためのアクティブ歌唱フレーム判定:
        // ノイズ床より +9dB 以上高いか、-48dBFS 以上で有声ピッチ信頼度があるフレームを「歌唱中」とする
        const bool isActiveVoice = (blockDb > std::max (-50.0f, floorDb + 9.0f))
                                || (blockDb > -54.0f && f0Hz >= 65.0f && f0Conf >= 0.45f);

        // アタック密度検出（ブロック間で +4.2 dB 以上の鋭い立ち上がりがあれば子音/音節オンセット）
        if (isActiveVoice && (blockDb - prevBlockDb) > 4.2f)
            ++onsetCount;
        prevBlockDb = blockDb;

        double localBand[kNumFineBands] {};
        // サンプルごとにモノラル代表信号（逆相打ち消しを防ぐため振幅絶対値の大きい側を選択）で12帯域フィルタを更新
        for (int n = 0; n < numSamples; ++n)
        {
            const float s0 = ch0[n];
            const float s1 = (ch1 != nullptr) ? ch1[n] : s0;
            const float x  = (std::abs (s1) > std::abs (s0)) ? s1 : s0;
            for (int b = 0; b < kNumFineBands; ++b)
            {
                const float y = bp[b].processSample (x);
                localBand[b] += (double) y * (double) y;
            }
        }

        for (int b = 0; b < kNumFineBands; ++b)
            allBandEnergy[(size_t) b] += localBand[b];
        allSamples += numSamples;

        if (isActiveVoice)
        {
            for (int b = 0; b < kNumFineBands; ++b)
                voicedBandEnergy[(size_t) b] += localBand[b];
            voicedSamples += numSamples;
            voicedSumSq   += blockSq;
            voicedDbSum   += (double) blockDb;
            voicedDbSqSum += (double) blockDb * (double) blockDb;
            ++voicedBlocks;

            if (f0Hz >= 65.0f && f0Hz <= 1150.0f && f0Conf >= 0.40f)
            {
                const double w = (double) f0Conf * std::sqrt (meanSq + 1.0e-9);
                f0WeightedSum += (double) f0Hz * w;
                f0WeightTotal += w;
                if (f0Conf >= 0.58f)
                {
                    f0MinHz = std::min (f0MinHz, f0Hz);
                    f0MaxHz = std::max (f0MaxHz, f0Hz);
                }
            }
            if (f0Conf > 0.05f)
            {
                hnrSum += (double) std::clamp (f0Conf, 0.0f, 1.0f);
                ++hnrCount;
            }
        }
    }

    // 蓄積された統計から12タイプ J-Pop 声質カルテを生成（メッセージスレッド・オーディオスレッド双方で安全）
    KarteReport buildReport (float fallbackRmsDb, float fallbackDynSdDb, bool isTalkMode) const noexcept
    {
        KarteReport r;
        const bool useVoiced = (voicedSamples >= (long long) (0.25 * sr));
        const auto& E = useVoiced ? voicedBandEnergy : allBandEnergy;
        const long long smp = useVoiced ? voicedSamples : std::max (1LL, allSamples);

        double totalE = 1.0e-12;
        for (int b = 1; b < kNumFineBands; ++b) // B0(<80Hzランブル)を除いた純粋声帯域合計
            totalE += E[(size_t) b];

        auto share = [&] (int b) noexcept -> float
        {
            return (float) (E[(size_t) b] / totalE);
        };
        auto bandDb = [&] (int b) noexcept -> float
        {
            return (float) (10.0 * std::log10 (E[(size_t) b] / (double) smp + 1.0e-15));
        };

        if (useVoiced && voicedBlocks >= 2)
        {
            r.activeRmsDb = (float) (10.0 * std::log10 (voicedSumSq / (double) voicedSamples + 1.0e-12));
            const double meanB = voicedDbSum / (double) voicedBlocks;
            const double varB  = std::max (0.0, voicedDbSqSum / (double) voicedBlocks - meanB * meanB);
            r.activeDynSdDb = (float) std::sqrt (varB);
        }
        else
        {
            r.activeRmsDb   = fallbackRmsDb;
            r.activeDynSdDb = fallbackDynSdDb;
        }

        // 基本周波数 F0 と音域幅
        if (f0WeightTotal > 1.0e-9)
        {
            r.meanF0Hz = (float) std::clamp (f0WeightedSum / f0WeightTotal, 75.0, 950.0);
        }
        else
        {
            // ピッチトラッカ未稼働時（単体正弦波・複合波テスト等）は低域基音帯域重心から推定
            const double lowWeight = E[1] * 130.0 + E[2] * 240.0 + E[3] * 380.0 + 1.0e-12;
            const double lowDenom  = E[1] + E[2] + E[3] + 1.0e-12;
            r.meanF0Hz = (float) std::clamp (lowWeight / lowDenom, 95.0, 520.0);
        }

        if (f0MaxHz > f0MinHz * 1.03f && f0MinHz < 1500.0f)
            r.pitchSpanSemitones = std::clamp (12.0f * std::log2 (f0MaxHz / f0MinHz), 0.5f, 36.0f);
        else
            r.pitchSpanSemitones = std::clamp (r.activeDynSdDb * 1.4f, 2.0f, 18.0f);

        // H1-H2 声門開放率指標: 基音帯域(B1+B2) と 倍音コア帯域(B4+B5) の相対バランス
        const float fundDb = (float) (10.0 * std::log10 ((E[1] + E[2]) / (double) smp + 1.0e-15));
        const float coreDb = (float) (10.0 * std::log10 ((E[4] + E[5]) / (double) smp + 1.0e-15));
        r.h1MinusH2Db = std::clamp (fundDb - coreDb, -18.0f, 18.0f);

        // 2.98kHz シンガーズ・フォルマント vs 4.1kHz 外耳道痛覚帯域
        const float sfDb    = bandDb (7);
        const float harshDb = bandDb (8);
        r.singerFormantRatioDb = std::clamp (sfDb - harshDb, -15.0f, 15.0f);

        // 1.12kHz 日本語特有の鼻声・喉締めイキミ突出度
        const float adjMidDb = 0.5f * (bandDb (4) + bandDb (6));
        r.nasalIkimiExcessDb = std::clamp (bandDb (5) - adjMidDb, -12.0f, 15.0f);

        // 400Hz 宅録ボックス鳴り（部屋のこもり・箱共鳴）突出度
        const float adjBoxDb = 0.5f * (bandDb (2) + bandDb (4));
        r.boxinessExcessDb = std::clamp (bandDb (3) - adjBoxDb, -12.0f, 15.0f);

        r.sibilanceRatio = share (9);
        r.breathAirRatio = share (10) + share (11);

        // HNR (クリア度 vs ハスキー気流成分)
        const float hfBreathRatio = (share (9) * 0.5f + share (10) + share (11))
                                  / std::max (0.02f, share (4) + share (6) + share (7));
        const float spectralHnr = std::clamp (1.0f - hfBreathRatio * 1.65f, 0.15f, 0.96f);
        r.hnr01 = (hnrCount >= 4)
            ? std::clamp (0.55f * (float) (hnrSum / (double) hnrCount) + 0.45f * spectralHnr, 0.12f, 0.98f)
            : spectralHnr;

        const double activeSec = std::max (0.4, (double) smp / sr);
        const float onsetsPerSec = (float) ((double) onsetCount / activeSec);
        r.transientDensity01 = std::clamp (onsetsPerSec / 6.5f, 0.0f, 1.0f);

        // 6軸UIカルテスコア算出
        const float highShare = share (6) + share (7) + share (8);
        const float lowShare  = share (1) + share (2) + share (3);
        r.scoreClarity01     = std::clamp (0.42f + (highShare - lowShare * 0.45f) * 1.35f + r.singerFormantRatioDb * 0.02f, 0.12f, 0.98f);
        r.scoreWarmth01      = std::clamp (0.35f + lowShare * 1.45f - share (8) * 0.8f, 0.12f, 0.98f);
        r.scoreBreathGloss01 = std::clamp (0.28f + r.breathAirRatio * 4.8f + std::max (0.0f, r.h1MinusH2Db) * 0.025f, 0.10f, 0.98f);
        r.scoreCorePower01   = std::clamp ((r.activeRmsDb + 38.0f) / 26.0f, 0.10f, 0.98f);
        r.scoreSmoothness01  = std::clamp (0.78f - std::max (0.0f, -r.singerFormantRatioDb) * 0.04f
                                                 - std::max (0.0f, r.nasalIkimiExcessDb) * 0.03f
                                                 - r.sibilanceRatio * 1.2f, 0.12f, 0.98f);
        r.scoreExpressive01  = std::clamp (0.25f + r.activeDynSdDb * 0.085f + r.pitchSpanSemitones * 0.022f, 0.12f, 0.98f);

        // ---- 12タイプ J-Pop 声質カルテ精密分類 ----
        // ※400Hz箱鳴り判定は 10kHz吐息エアーが少ない（息多めウィスパー基音240Hzと誤認しない）条件を厳密適用
        if ((share (3) > 0.44f || (r.boxinessExcessDb > 4.0f && highShare < 0.16f)) && r.breathAirRatio < 0.08f)
        {
            r.archetype = HomeBoothBoxyFix;
        }
        else if (r.breathAirRatio > 0.11f && (r.h1MinusH2Db > 2.2f || r.activeRmsDb < -25.0f))
        {
            r.archetype = WhisperTransparent;
        }
        else if (r.hnr01 < 0.46f && (share (8) + share (9) + share (10)) > 0.18f && r.activeRmsDb >= -28.0f)
        {
            r.archetype = HuskySmokyEmo;
        }
        else if (r.activeRmsDb > -15.5f && (share (5) + share (8)) > 0.28f)
        {
            r.archetype = HighPressureRock;
        }
        else if (r.activeRmsDb > -18.5f && (r.h1MinusH2Db < -1.5f || r.nasalIkimiExcessDb > 2.4f))
        {
            r.archetype = PowerfulBelting;
        }
        else if (r.meanF0Hz >= 275.0f && highShare > 0.085f && r.hnr01 >= 0.48f)
        {
            r.archetype = HighToneKawaii;
        }
        else if (r.meanF0Hz <= 165.0f && (share (1) + share (2)) > 0.30f)
        {
            r.archetype = WarmLowBaritone;
        }
        else if (r.transientDensity01 >= 0.68f && ! isTalkMode)
        {
            r.archetype = FastSyllableVocalo;
        }
        else if (r.activeDynSdDb >= 5.2f)
        {
            r.archetype = DynamicBallad;
        }
        else if (r.pitchSpanSemitones >= 9.5f && r.scoreSmoothness01 >= 0.58f)
        {
            r.archetype = SilkyMixVoice;
        }
        else if (lowShare > 0.38f && r.scoreSmoothness01 >= 0.62f && r.activeRmsDb < -20.0f)
        {
            r.archetype = AcousticNatural;
        }
        else
        {
            r.archetype = ClearStandardJPop;
        }

        // ---- アーキタイプ別の補助DSPモジュール自動処方 ----
        r.recBreathCtrl     = true;
        r.recUpwardAir      = (r.archetype == WhisperTransparent || r.archetype == DynamicBallad || r.activeRmsDb < -26.0f);
        r.recPitchSweet     = (r.archetype == HighToneKawaii || r.archetype == PowerfulBelting || r.archetype == HighPressureRock || r.meanF0Hz >= 250.0f);
        r.recMixBridge      = (r.archetype == SilkyMixVoice || r.archetype == PowerfulBelting || r.pitchSpanSemitones >= 8.5f);
        r.recVowelAlign     = (r.archetype == ClearStandardJPop || r.archetype == HuskySmokyEmo || r.archetype == SilkyMixVoice);
        r.recOnsetGroove    = (r.archetype == FastSyllableVocalo || r.archetype == HighPressureRock);
        r.recRoomDereverb   = (r.archetype == HomeBoothBoxyFix && r.boxinessExcessDb > 3.5f);
        r.recCrushGuard     = (r.archetype == PowerfulBelting || r.archetype == DynamicBallad || r.archetype == HighPressureRock || r.activeDynSdDb > 4.5f);
        r.recJpopPolishMode = isTalkMode ? 0
                            : (r.archetype == PowerfulBelting || r.archetype == HighPressureRock || r.archetype == HighToneKawaii
                               || r.nasalIkimiExcessDb > 2.5f || r.singerFormantRatioDb < -1.5f) ? 2 : 1;
        return r;
    }

    static const char* archetypeNameJa (int arch) noexcept
    {
        switch (arch)
        {
            case WhisperTransparent: return "透明ウィスパー系";
            case ClearStandardJPop:  return "王道J-Popクリア";
            case HighToneKawaii:     return "ハイトーン・カワボ";
            case HuskySmokyEmo:      return "ハスキー・エモ声";
            case PowerfulBelting:    return "パワフル・ベルティング";
            case SilkyMixVoice:      return "シルキー・ミックス";
            case WarmLowBaritone:    return "ウォーム低音・イケボ";
            case FastSyllableVocalo: return "ボカロ・高速譜割り";
            case DynamicBallad:      return "ダイナミック・バラード";
            case AcousticNatural:    return "アコースティック自然派";
            case HomeBoothBoxyFix:   return "宅録こもり・箱鳴り補正";
            case HighPressureRock:   return "ハイプレッシャー・ロック";
            default:                 return "未診断";
        }
    }

    static const char* archetypeNameEn (int arch) noexcept
    {
        switch (arch)
        {
            case WhisperTransparent: return "Crystal Whisper";
            case ClearStandardJPop:  return "Clear J-Pop Lead";
            case HighToneKawaii:     return "High-Tone Bright";
            case HuskySmokyEmo:      return "Smoky Husky";
            case PowerfulBelting:    return "Power Belting";
            case SilkyMixVoice:      return "Silky Mix Voice";
            case WarmLowBaritone:    return "Warm Baritone";
            case FastSyllableVocalo: return "Fast Articulation";
            case DynamicBallad:      return "Dynamic Ballad";
            case AcousticNatural:    return "Acoustic Natural";
            case HomeBoothBoxyFix:   return "Booth De-Boxy";
            case HighPressureRock:   return "Rock Presence";
            default:                 return "Ready";
        }
    }

    static const char* archetypeRecipeJa (int arch) noexcept
    {
        switch (arch)
        {
            case WhisperTransparent: return "息のシルク艶を保護し、消え際まで前に浮上";
            case ClearStandardJPop:  return "2.95kHzの抜けと2段コンプで王道メジャー質感";
            case HighToneKawaii:     return "高域の耳痛帯域を抑え、偶数次倍音で太く可愛く";
            case HuskySmokyEmo:      return "美味しい掠れ倍音を残し、ザラつく痛覚帯のみ整音";
            case PowerfulBelting:    return "サビの張り潰れを防ぎ、1.1kHz/3.9kHzの力みを緩和";
            case SilkyMixVoice:      return "地声〜裏声の換声点を滑らかに繋ぎ立体感を付与";
            case WarmLowBaritone:    return "近接ボワつきを締め、胸の深みと3kHzの輪郭を両立";
            case FastSyllableVocalo: return "高速譜割りの子音アタックを立てて歌詞を鮮明化";
            case DynamicBallad:      return "Aメロの吐息からラスサビの伸びまで抑揚を完全維持";
            case AcousticNatural:    return "過度な圧縮を避け、木の温もりと自然な息遣いを保持";
            case HomeBoothBoxyFix:   return "400Hz箱鳴りと部屋反響を引き算しスタジオ録音化";
            case HighPressureRock:   return "Rear-Bus芯圧でオケ負けを防ぎ、刺さりを動的鎮静";
            default:                 return "5秒間歌うと12タイプの声質カルテで自動最適化します";
        }
    }

    static const char* archetypeRecipeEn (int arch) noexcept
    {
        switch (arch)
        {
            case WhisperTransparent: return "Silk air lift + soft tail protection";
            case ClearStandardJPop:  return "2.95kHz Singer's Formant + dual LA-2A/1176";
            case HighToneKawaii:     return "3.9kHz harshness relief + warm velvet body";
            case HuskySmokyEmo:      return "Keeps smoky air while taming 4kHz bite";
            case PowerfulBelting:    return "Belt headroom guard + 1.1k/3.9k strain relief";
            case SilkyMixVoice:      return "Seamless passagio bridge + 3D stage halo";
            case WarmLowBaritone:    return "Tightens 220Hz proximity + 3kHz focus";
            case FastSyllableVocalo: return "Crisp consonant transients for fast lyrics";
            case DynamicBallad:      return "Whisper upward lift + climax crest guard";
            case AcousticNatural:    return "Gentle opto leveling + natural room warmth";
            case HomeBoothBoxyFix:   return "400Hz de-box + room reflection cleanup";
            case HighPressureRock:   return "Parallel rear-bus punch + dynamic soothe";
            default:                 return "Sing for 5s to diagnose 12 J-Pop vocal profiles";
        }
    }

private:
    double sr = 48000.0;
    bool   prepared = false;
    std::array<FastTptSvf, kNumFineBands> bp {};
    std::array<double, kNumFineBands> allBandEnergy {};
    std::array<double, kNumFineBands> voicedBandEnergy {};
    long long voicedSamples = 0;
    long long allSamples    = 0;
    double voicedSumSq      = 0.0;
    double voicedDbSum      = 0.0;
    double voicedDbSqSum    = 0.0;
    int    voicedBlocks     = 0;
    double f0WeightedSum    = 0.0;
    double f0WeightTotal    = 0.0;
    float  f0MinHz          = 2000.0f;
    float  f0MaxHz          = 0.0f;
    double hnrSum           = 0.0;
    int    hnrCount         = 0;
    int    onsetCount       = 0;
    float  prevBlockDb      = -90.0f;
};

// =============================================================================
//  2. AiVoicePurifierAndNoiseClassifier — AIノイズパターン識別 ＆ ピッチ同期声抽出器
// =============================================================================
class AiVoicePurifierAndNoiseClassifier
{
public:
    enum NoisePattern : int
    {
        StudioQuiet = 0, // ✨ 静音クリア
        HvacFan     = 1, // 🌀 空調・PCファン騒音 (<1.2kHz 低〜中低域定常ノイズ)
        PowerHum    = 2, // ⚡ 電源ハム・USBジー音 (<250Hz 狭帯域集中ノイズ)
        PreampHiss  = 3, // 📻 マイクサー音・ヒス (>1.2kHz 高域熱雑音)
        KeyClick    = 4, // ⌨️ 打鍵・マウス物音 (高クレスト突発ノイズ)
        ComplexRoom = 5  // 🏠 複合環境ノイズ (広帯域室内騒音)
    };

    static constexpr int kCombRingSize = 4096; // 192kHz / 65Hz = 2953 samples まで余裕で収容
    static constexpr int kCombRingMask = kCombRingSize - 1;

    void prepare (double sampleRate) noexcept
    {
        sr = std::clamp (sampleRate, 8000.0, 192000.0);
        const float srF = (float) sr;
        combAlphaSlew = 1.0f - std::exp (-1.0f / (0.008f * srF)); // 8ms 滑らか遷移
        f0Slew        = 1.0f - std::exp (-1.0f / (0.004f * srF)); // 4ms ピッチ追従
        crossfadeStep = 1.0f / std::max (32.0f, 0.0065f * srF);   // v5.8.0: 6.5ms 2タップ静止クロスフェード
        clickFastAtk  = 1.0f - std::exp (-1.0f / (0.0004f * srF));
        clickFastRel  = 1.0f - std::exp (-1.0f / (0.015f * srF));
        clickSlowCoef = 1.0f - std::exp (-1.0f / (0.250f * srF));
        reset();
    }

    void reset() noexcept
    {
        for (int b = 0; b < 3; ++b)
            for (int ch = 0; ch < 2; ++ch)
                combRing[b][ch].fill (0.0f);
        ringWrite = 0;
        smoothedPeriod = (float) (sr / 220.0);
        tapPeriodA     = smoothedPeriod;
        tapPeriodB     = smoothedPeriod;
        tapCrossfade   = 0.0f;
        tapCrossfading = false;
        prevBlockF0Hz  = 0.0f;
        combMixAlpha   = 0.0f;
        fastPeakEnv    = 0.0f;
        slowRmsEnv     = 1.0e-5f;
        clickSpikeScore = 0.0f;
        currentPattern = StudioQuiet;
        bandWeights = { 1.0f, 1.0f, 1.0f, 1.0f };
        lastHarmonicCleanDb = 0.0f;
    }

    // 1サンプルごとの突発スパイク（キーボード打鍵・マウスクリック）観測
    inline void observeInputSample (float s0, float s1, bool voiceActive) noexcept
    {
        const float pk = std::max (std::abs (s0), std::abs (s1));
        fastPeakEnv += (pk > fastPeakEnv ? clickFastAtk : clickFastRel) * (pk - fastPeakEnv);
        if (! voiceActive)
            slowRmsEnv += clickSlowCoef * (pk - slowRmsEnv);

        if (! voiceActive && slowRmsEnv > 1.5e-5f)
        {
            const float crest = fastPeakEnv / (slowRmsEnv + 1.0e-6f);
            const float target = (crest > 6.5f && fastPeakEnv > 1.2e-3f) ? 1.0f : 0.0f;
            clickSpikeScore += (target > clickSpikeScore ? 0.04f : 0.00008f) * (target - clickSpikeScore);
        }
    }

    // 4帯域ノイズ床プロファイル (LP250, 250-1200, 1200-5000, HP5000) からノイズ種類を識別し、
    // 声の美味しい成分を守りながら雑音帯域だけを重点カットする帯域重みを更新する
    int classifyProfile (const float dnFloor[4]) noexcept
    {
        const float f0 = std::max (dnFloor[0], 1.0e-6f);
        const float f1 = std::max (dnFloor[1], 1.0e-6f);
        const float f2 = std::max (dnFloor[2], 1.0e-6f);
        const float f3 = std::max (dnFloor[3], 1.0e-6f);
        const float maxF = std::max ({ f0, f1, f2, f3 });
        const float sumF = f0 + f1 + f2 + f3 + 1.0e-9f;

        // -74 dBFS (約 2.0e-4) 未満の極静音環境
        if (maxF < 2.0e-4f && clickSpikeScore < 0.35f)
        {
            currentPattern = StudioQuiet;
            bandWeights = { 1.0f, 1.0f, 1.0f, 1.0f };
            return currentPattern;
        }

        if (clickSpikeScore >= 0.42f && maxF < 6.0e-3f)
        {
            currentPattern = KeyClick;
            // 物音・クリック時は中高域の素早い閉鎖を助けつつ声の基本帯域は自然維持
            bandWeights = { 1.00f, 1.05f, 1.12f, 1.10f };
            return currentPattern;
        }

        const float lowDom  = f0 / sumF;
        const float fanDom  = (f0 + f1) / sumF;
        const float hissDom = (f2 * 0.65f + f3 * 1.35f) / sumF;

        if (lowDom > 0.56f && f0 > f2 * 3.2f)
        {
            currentPattern = PowerHum;
            // 電源ハム・ジー音: <250Hz を強力に沈め、高域は自然な空気感を保つ
            bandWeights = { 1.28f, 1.08f, 0.98f, 0.90f };
        }
        else if (fanDom > 0.66f && (f0 + f1) > (f2 + f3) * 1.9f)
        {
            currentPattern = HvacFan;
            // 空調・PCファン: ゴォーという低〜中低域を重点抑制し、高域の静寂との落差を滑らかに保つ
            bandWeights = { 1.22f, 1.18f, 0.98f, 0.90f };
        }
        else if (hissDom > 0.52f || f3 > f0 * 1.45f)
        {
            currentPattern = PreampHiss;
            // マイクサー音(ホワイトヒス): 高域の非子音ノイズを緻密に抑え、低域も学習基準減衰力(1.00)を維持
            bandWeights = { 1.00f, 1.00f, 1.14f, 1.20f };
        }
        else
        {
            currentPattern = ComplexRoom;
            // 複合室内ノイズ: 全帯域バランス良く純化
            bandWeights = { 1.08f, 1.06f, 1.05f, 1.05f };
        }
        return currentPattern;
    }

    inline float bandWeight (int b) const noexcept
    {
        return bandWeights[(size_t) std::clamp (b, 0, 3)];
    }

    int pattern() const noexcept { return currentPattern; }
    float harmonicCleanDb() const noexcept { return lastHarmonicCleanDb; }

    // -------------------------------------------------------------------------
    //  ピアニッシモ歌唱フェードアウト保護ブリッジ:
    //  歌い終わりの小さなロングトーンが従来の閾値(vFloor*4)を下回っても、
    //  倍音の周期性(isPeriodic または f0Conf)が確認されている間は語尾をチョップせず滑らかに通す！
    //  無音になった瞬間は 0.0 になるため、無音時のノイズ減衰力は 100% そのまま維持される。
    // -------------------------------------------------------------------------
    static inline float computeHarmonicTailBridge01 (bool isPeriodic, float f0Conf,
                                                     float midEnv, float midFloor) noexcept
    {
        const bool harmonicPresent = isPeriodic || (f0Conf >= 0.52f);
        if (! harmonicPresent)
            return 0.0f;
        const float safeFloor = std::max (midFloor, 1.0e-6f);
        // 床の +3.8dB (×1.55) から +12dB (×4.0) のピアニッシモ領域を滑らかに橋渡しする
        return std::clamp ((midEnv - safeFloor * 1.55f) / std::max (safeFloor * 2.45f, 1.0e-6f), 0.0f, 1.0f);
    }

    // -------------------------------------------------------------------------
    //  歌唱中もうたを殺さず倍音の隙間ノイズだけを抜き出す
    //  「ゼロ遅延・ピッチ同期ハーモニック・コム声抽出器」
    //  ・band1 (250-1200Hz), band2 (1200-5000Hz), band3 (>5000Hz) に適用
    //  ・整数次倍音 k*F0 のゲイン = (1 - a) + a*(+1) = 1.000 (0.00 dB 損失！)
    //  ・倍音間ノイズ (k+0.5)*F0 のゲイン = 1 - 2a (最大 -10.5 dB 純化！)
    //  ・ビブラート急変時やサ行子音オンセット時は瞬時に a -> 0 となり音質劣化ゼロ！
    // -------------------------------------------------------------------------
    void processHarmonicVoiceExtractor (float* b1L, float* b1R,
                                        float* b2L, float* b2R,
                                        float* b3L, float* b3R,
                                        int numSamples, int numCh,
                                        float f0Hz, float f0Conf,
                                        float denoiseAmount01,
                                        const float dnFloor[4],
                                        const float dnEnv[4],
                                        bool hfConsonantOnset) noexcept
    {
        if (numSamples <= 0 || b1L == nullptr || b2L == nullptr || b3L == nullptr)
            return;

        const bool validPitch = (f0Hz >= 72.0f && f0Hz <= 950.0f && f0Conf >= 0.58f);
        // ビブラートやピッチグライドの速さを測定し、速いピッチ変化中はコム干渉を防ぐため自動で緩める
        float glideGuard = 1.0f;
        if (validPitch && prevBlockF0Hz > 50.0f)
        {
            const float relDelta = std::abs (f0Hz - prevBlockF0Hz) / prevBlockF0Hz;
            // 1ブロックあたり 1.8% (~30 cents) 以上の急速グライド時は滑らかに解放
            glideGuard = std::clamp (1.0f - (relDelta - 0.006f) / 0.018f, 0.0f, 1.0f);
        }
        if (validPitch)
            prevBlockF0Hz = f0Hz;
        else
            prevBlockF0Hz = 0.0f;

        // 背景ノイズ床が実際に存在するときだけ歌唱中コム純化を働かせる
        // （ノイズゼロのクリーンなスタジオ録音には一切余計な処理を掛けない！）
        const float midHighFloor = dnFloor[1] * 0.35f + dnFloor[2] * 0.40f + dnFloor[3] * 0.25f;
        const float midHighEnv   = dnEnv[1]   * 0.35f + dnEnv[2]   * 0.40f + dnEnv[3]   * 0.25f + 1.0e-7f;
        // ノイズ床が -72 dBFS (2.5e-4) 以上あり、かつ現在の信号に対してノイズが無視できない比率のとき稼働
        const float floorPresence = std::clamp ((midHighFloor - 2.2e-4f) / 1.8e-3f, 0.0f, 1.0f);
        const float snrNeed       = std::clamp ((midHighFloor * 28.0f) / midHighEnv, 0.0f, 1.0f);

        float wantAlpha = 0.0f;
        float targetPeriod = tapPeriodA;
        if (validPitch && ! hfConsonantOnset && denoiseAmount01 > 0.02f && floorPresence > 0.01f)
        {
            targetPeriod = std::clamp ((float) (sr / (double) f0Hz), 8.0f, (float) (kCombRingSize - 8));
            const float confFactor = std::clamp ((f0Conf - 0.58f) / 0.32f, 0.0f, 1.0f);
            wantAlpha = std::clamp (0.35f * denoiseAmount01 * confFactor * glideGuard * floorPresence * (0.35f + 0.65f * snrNeed),
                                    0.0f, 0.35f);

            // v5.8.0: 2タップ位相ロック静止クロスフェード方式
            // 読み出しタップをサンプルごとに連続スイープさせると dd/dn != 0 のドップラー・ジッパーノイズが出るため、
            // 無稼働からの立ち上がり時はフェードイン前に目標周期へ完全静止スナップし、
            // 有声中のピッチ変化時は「完全静止したAタップ」と「目標周期に固定したBタップ」を約6.5msで滑らかにクロスフェードする。
            if (combMixAlpha < 0.0008f)
            {
                tapPeriodA     = targetPeriod;
                tapPeriodB     = targetPeriod;
                smoothedPeriod = targetPeriod;
                tapCrossfade   = 0.0f;
                tapCrossfading = false;
            }
            else if (! tapCrossfading && std::abs (targetPeriod - tapPeriodA) > 0.25f)
            {
                tapPeriodB     = targetPeriod;
                tapCrossfade   = 0.0f;
                tapCrossfading = true;
            }
        }

        // ラグランジュ3次分数遅延係数をブロック先頭で1回だけ事前計算（dd/dn == 0 厳守＆CPU 3倍高速化）
        auto makeLagrange = [] (float period, int& dIntOut, float& c0, float& c1, float& c2, float& c3) noexcept
        {
            const float d = std::clamp (period, 4.0f, (float) (kCombRingSize - 6));
            dIntOut = (int) d;
            const float mu = d - (float) dIntOut;
            c0 = -mu * (mu - 1.0f) * (mu - 2.0f) * (1.0f / 6.0f);
            c1 =  (mu + 1.0f) * (mu - 1.0f) * (mu - 2.0f) * 0.5f;
            c2 = -(mu + 1.0f) * mu * (mu - 2.0f) * 0.5f;
            c3 =  (mu + 1.0f) * mu * (mu - 1.0f) * (1.0f / 6.0f);
        };

        int dIntA = 0, dIntB = 0;
        float c0A = 0.0f, c1A = 1.0f, c2A = 0.0f, c3A = 0.0f;
        float c0B = 0.0f, c1B = 1.0f, c2B = 0.0f, c3B = 0.0f;
        makeLagrange (tapPeriodA, dIntA, c0A, c1A, c2A, c3A);
        if (tapCrossfading)
            makeLagrange (tapPeriodB, dIntB, c0B, c1B, c2B, c3B);

        float* bandsL[3] = { b1L, b2L, b3L };
        float* bandsR[3] = { b1R, b2R, b3R };
        // 高域(>5kHz)はピッチ揺らぎへの感度が高いためコム深度を控えめ(0.45倍)にして息の自然さを100%保つ
        static constexpr float kBandCombScale[3] = { 0.80f, 1.00f, 0.45f };

        const int chCount = std::min (numCh, 2);
        for (int n = 0; n < numSamples; ++n)
        {
            combMixAlpha += combAlphaSlew * (wantAlpha - combMixAlpha);

            float xfadeMix = 0.0f;
            if (tapCrossfading)
            {
                tapCrossfade += crossfadeStep;
                if (tapCrossfade >= 1.0f)
                {
                    tapCrossfade   = 0.0f;
                    tapCrossfading = false;
                    tapPeriodA     = tapPeriodB;
                    smoothedPeriod = tapPeriodA;
                    dIntA = dIntB;
                    c0A = c0B; c1A = c1B; c2A = c2B; c3A = c3B;
                }
                else
                {
                    // C1連続 Hermite smoothstep で切替時の傾き不連続もゼロにする
                    xfadeMix = tapCrossfade * tapCrossfade * (3.0f - 2.0f * tapCrossfade);
                }
            }

            const int w = ringWrite;
            if (combMixAlpha > 0.0008f)
            {
                const int idxA0 = (w - dIntA + 1 + kCombRingSize) & kCombRingMask;
                const int idxA1 = (w - dIntA     + kCombRingSize) & kCombRingMask;
                const int idxA2 = (w - dIntA - 1 + kCombRingSize) & kCombRingMask;
                const int idxA3 = (w - dIntA - 2 + kCombRingSize) & kCombRingMask;

                int idxB0 = 0, idxB1 = 0, idxB2 = 0, idxB3 = 0;
                if (tapCrossfading)
                {
                    idxB0 = (w - dIntB + 1 + kCombRingSize) & kCombRingMask;
                    idxB1 = (w - dIntB     + kCombRingSize) & kCombRingMask;
                    idxB2 = (w - dIntB - 1 + kCombRingSize) & kCombRingMask;
                    idxB3 = (w - dIntB - 2 + kCombRingSize) & kCombRingMask;
                }

                for (int b = 0; b < 3; ++b)
                {
                    const float a = combMixAlpha * kBandCombScale[b];
                    for (int ch = 0; ch < chCount; ++ch)
                    {
                        float* ptr = (ch == 0) ? bandsL[b] : bandsR[b];
                        if (ptr == nullptr) continue;
                        const float x = ptr[n];
                        auto& ring = combRing[b][ch];
                        ring[(size_t) w] = x;
                        float delayed = c0A * ring[(size_t) idxA0]
                                      + c1A * ring[(size_t) idxA1]
                                      + c2A * ring[(size_t) idxA2]
                                      + c3A * ring[(size_t) idxA3];
                        if (tapCrossfading)
                        {
                            const float delayedB = c0B * ring[(size_t) idxB0]
                                                 + c1B * ring[(size_t) idxB1]
                                                 + c2B * ring[(size_t) idxB2]
                                                 + c3B * ring[(size_t) idxB3];
                            delayed += xfadeMix * (delayedB - delayed);
                        }
                        ptr[n] = (1.0f - a) * x + a * delayed;
                    }
                }
            }
            else
            {
                // 待機中もリング履歴だけ軽量更新して有声開始瞬間のクリックをゼロにする
                for (int b = 0; b < 3; ++b)
                {
                    combRing[b][0][(size_t) w] = bandsL[b][n];
                    if (chCount > 1 && bandsR[b] != nullptr)
                        combRing[b][1][(size_t) w] = bandsR[b][n];
                }
            }

            ringWrite = (w + 1) & kCombRingMask;
        }

        // 倍音間の雑音が何dB引き算されたかをテレメトリ保存 (1 - 2*alpha)
        const float notchGain = std::clamp (1.0f - 2.0f * combMixAlpha, 0.25f, 1.0f);
        lastHarmonicCleanDb = (combMixAlpha > 0.005f) ? (-20.0f * std::log10 (notchGain)) : 0.0f;
    }

    static const char* patternBadgeJa (int pat, bool extractingVoiceNow) noexcept
    {
        if (extractingVoiceNow)
            return "✨AI声抽出中";
        switch (pat)
        {
            case HvacFan:     return "🌀空調ファン純化";
            case PowerHum:    return "⚡ハム・ジー純化";
            case PreampHiss:  return "📻サー音ヒス純化";
            case KeyClick:    return "⌨️物音パルス抑制";
            case ComplexRoom: return "🏠環境ノイズ純化";
            default:          return "✨AI静音監視";
        }
    }

    static const char* patternBadgeEn (int pat, bool extractingVoiceNow) noexcept
    {
        if (extractingVoiceNow)
            return "AI Voice Extract";
        switch (pat)
        {
            case HvacFan:     return "Fan/AC Clean";
            case PowerHum:    return "Hum/Buzz Clean";
            case PreampHiss:  return "Hiss Purifier";
            case KeyClick:    return "Click Guard";
            case ComplexRoom: return "Room AI Clean";
            default:          return "AI Pure Idle";
        }
    }

private:
    double sr = 48000.0;
    std::array<float, kCombRingSize> combRing[3][2] {};
    int   ringWrite        = 0;
    float smoothedPeriod   = 218.0f;
    float tapPeriodA       = 218.0f;
    float tapPeriodB       = 218.0f;
    float tapCrossfade     = 0.0f;
    bool  tapCrossfading   = false;
    float crossfadeStep    = 0.004f;
    float prevBlockF0Hz    = 0.0f;
    float combMixAlpha     = 0.0f;
    float combAlphaSlew    = 0.002f;
    float f0Slew           = 0.005f;
    float clickFastAtk     = 0.05f;
    float clickFastRel     = 0.001f;
    float clickSlowCoef    = 0.0001f;
    float fastPeakEnv      = 0.0f;
    float slowRmsEnv       = 1.0e-5f;
    float clickSpikeScore  = 0.0f;
    int   currentPattern   = StudioQuiet;
    std::array<float, 4> bandWeights { 1.0f, 1.0f, 1.0f, 1.0f };
    float lastHarmonicCleanDb = 0.0f;
};

// =============================================================================
//  3. JPopAutoVocalPolishEngine — 新機能「J-Pop 完パケ艶＆喉締めオートリリーフ」
//     1) 1.08kHz 日本語鼻づまり・イキミ共鳴の動的引き算リリーフ
//     2) 3.88kHz ハイトーン喉締め・外耳道痛覚共鳴の動的引き算リリーフ
//     3) 2.95kHz シンガーズ・フォルマント ＆ 11.5kHz シルクエアーの有声母音連動リフト
// =============================================================================
class JPopAutoVocalPolishEngine
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = std::clamp (sampleRate, 16000.0, 192000.0);
        detRefVowel.setBandPass (sr, 680.0, 1.15);
        detIkimi.setBandPass    (sr, 1080.0, 1.85);
        detHarsh.setBandPass    (sr, 3880.0, 2.05);

        const float srF = (float) sr;
        envAtk = 1.0f - std::exp (-1.0f / (0.0035f * srF)); // 3.5ms 素早い反応
        envRel = 1.0f - std::exp (-1.0f / (0.0650f * srF)); // 65ms 自然な復帰
        reset();
    }

    void reset() noexcept
    {
        detRefVowel.reset();
        detIkimi.reset();
        detHarsh.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            eqIkimi[ch].reset();
            eqHarsh[ch].reset();
            eqFocus[ch].reset();
            eqSilk[ch].reset();
        }
        refEnv = ikimiEnv = harshEnv = 0.0f;
        curIkimiCutDb = curHarshCutDb = curFocusBoostDb = curSilkBoostDb = 0.0f;
        appliedIkimiDb = appliedHarshDb = appliedFocusDb = appliedSilkDb = -99.0f;
        lastReliefDb = 0.0f;
    }

    // mode: 0 = OFF, 1 = 歌枠ナチュラル艶, 2 = J-Pop完パケ極艶
    void processBlock (float* L, float* R, int numSamples, int mode,
                       float voicedRatio01, float f0Hz) noexcept
    {
        if (mode <= 0 || L == nullptr || numSamples <= 0)
        {
            if (appliedIkimiDb != -99.0f)
                reset();
            return;
        }

        const bool isMaxPolish = (mode >= 2);
        const float maxIkimiCut = isMaxPolish ? 4.4f : 2.8f;
        const float maxHarshCut = isMaxPolish ? 4.8f : 3.2f;
        const float targetFocus = (isMaxPolish ? 2.3f : 1.4f) * std::clamp (voicedRatio01, 0.0f, 1.0f);
        const float targetSilk  = (isMaxPolish ? 2.1f : 1.2f) * std::clamp (voicedRatio01, 0.0f, 1.0f);

        // ハイトーン歌唱時 (F0 >= 260Hz) は喉締め痛覚帯域(3.88kHz)への感度を高める
        const float highNoteSens = (f0Hz >= 240.0f)
            ? std::clamp (1.0f + (f0Hz - 240.0f) / 380.0f, 1.0f, 1.45f)
            : 1.0f;

        float peakRelief = 0.0f;
        for (int n = 0; n < numSamples; ++n)
        {
            const float mono = (R != nullptr) ? 0.5f * (L[n] + R[n]) : L[n];
            const float aRef = std::abs (detRefVowel.processSample (mono));
            const float aIki = std::abs (detIkimi.processSample (mono));
            const float aHar = std::abs (detHarsh.processSample (mono)) * highNoteSens;

            refEnv   += (aRef > refEnv   ? envAtk : envRel) * (aRef - refEnv);
            ikimiEnv += (aIki > ikimiEnv ? envAtk : envRel) * (aIki - ikimiEnv);
            harshEnv += (aHar > harshEnv ? envAtk : envRel) * (aHar - harshEnv);

            if ((n & 31) == 0)
            {
                float wantIkimiCut = 0.0f;
                float wantHarshCut = 0.0f;
                if (refEnv > 8.0e-4f || ikimiEnv > 1.2e-3f)
                {
                    const float ikiRatio = ikimiEnv / (refEnv * 0.82f + 5.0e-4f);
                    if (ikiRatio > 1.0f)
                        wantIkimiCut = std::min (maxIkimiCut, (ikiRatio - 1.0f) * 3.6f);

                    const float harRatio = harshEnv / (refEnv * 0.52f + 4.0e-4f);
                    if (harRatio > 1.0f)
                        wantHarshCut = std::min (maxHarshCut, (harRatio - 1.0f) * 3.8f);
                }

                curIkimiCutDb   += 0.22f * (wantIkimiCut - curIkimiCutDb);
                curHarshCutDb   += 0.25f * (wantHarshCut - curHarshCutDb);
                curFocusBoostDb += 0.12f * (targetFocus  - curFocusBoostDb);
                curSilkBoostDb  += 0.12f * (targetSilk   - curSilkBoostDb);

                if (std::abs (curIkimiCutDb - appliedIkimiDb) > 0.06f)
                {
                    appliedIkimiDb = curIkimiCutDb;
                    for (int ch = 0; ch < 2; ++ch)
                        eqIkimi[ch].setPeakBell (sr, 1080.0, 1.95, -(double) appliedIkimiDb);
                }
                if (std::abs (curHarshCutDb - appliedHarshDb) > 0.06f)
                {
                    appliedHarshDb = curHarshCutDb;
                    for (int ch = 0; ch < 2; ++ch)
                        eqHarsh[ch].setPeakBell (sr, 3880.0, 2.15, -(double) appliedHarshDb);
                }
                if (std::abs (curFocusBoostDb - appliedFocusDb) > 0.06f)
                {
                    appliedFocusDb = curFocusBoostDb;
                    for (int ch = 0; ch < 2; ++ch)
                        eqFocus[ch].setPeakBell (sr, 2950.0, 1.35, (double) appliedFocusDb);
                }
                if (std::abs (curSilkBoostDb - appliedSilkDb) > 0.06f)
                {
                    appliedSilkDb = curSilkBoostDb;
                    for (int ch = 0; ch < 2; ++ch)
                        eqSilk[ch].setHighShelf (sr, 11500.0, 0.72, (double) appliedSilkDb);
                }
            }

            peakRelief = std::max (peakRelief, std::max (curIkimiCutDb, curHarshCutDb));

            float yL = eqIkimi[0].processSample (L[n]);
            yL = eqHarsh[0].processSample (yL);
            yL = eqFocus[0].processSample (yL);
            L[n] = eqSilk[0].processSample (yL);

            if (R != nullptr)
            {
                float yR = eqIkimi[1].processSample (R[n]);
                yR = eqHarsh[1].processSample (yR);
                yR = eqFocus[1].processSample (yR);
                R[n] = eqSilk[1].processSample (yR);
            }
        }
        lastReliefDb = peakRelief;
    }

    float reliefDb() const noexcept { return lastReliefDb; }

private:
    double sr = 48000.0;
    FastTptSvf detRefVowel, detIkimi, detHarsh;
    FastTptSvf eqIkimi[2], eqHarsh[2], eqFocus[2], eqSilk[2];
    float envAtk = 0.01f, envRel = 0.001f;
    float refEnv = 0.0f, ikimiEnv = 0.0f, harshEnv = 0.0f;
    float curIkimiCutDb = 0.0f, curHarshCutDb = 0.0f;
    float curFocusBoostDb = 0.0f, curSilkBoostDb = 0.0f;
    float appliedIkimiDb = -99.0f, appliedHarshDb = -99.0f;
    float appliedFocusDb = -99.0f, appliedSilkDb  = -99.0f;
    float lastReliefDb = 0.0f;
};

} // namespace gz::v570

namespace gz::v580
{

// =============================================================================
//  DawRealtimeHealthMonitor — REAPER / DAW リアルタイム負荷＆音途切れ切り分け診断器
//  ・ロックフリー・ゼロ確保（1コールバックあたり約15nsの高精度タイマー計測のみ）
//  ・プラグイン自身のDSP処理時間比率（平均% / ピーク% / 80%超過回数）と、
//    ホスト側コールバック間隔の異常遅延（プラグイン自身は35%未満で軽快に終わったのに
//    次のコールバック到着が予算の2.5倍以上遅れた＝他トラックの重いプラグインやASIO競合）を
//    リアルタイムで明確に切り分ける。
// =============================================================================
class DawRealtimeHealthMonitor
{
public:
    void prepare (double sampleRate, int blockSize) noexcept
    {
        sr = std::clamp (sampleRate, 8000.0, 192000.0);
        lastBlockSizeAtom.store (std::max (1, blockSize), std::memory_order_relaxed);
        const double invFreq = 1.0e6 / std::max (1.0, (double) juce::Time::getHighResolutionTicksPerSecond());
        ticksToMicros = invFreq;
        prevEndTicks  = 0;
        prevPluginLoadPct = 0.0f;
        smoothedLoadPct   = 0.0f;
        decayingPeakPct   = 0.0f;
    }

    inline void beginCallback (int numSamples, double sampleRate) noexcept
    {
        const juce::int64 nowTicks = juce::Time::getHighResolutionTicks();
        startTicks = nowTicks;
        curSamples = std::max (1, numSamples);
        const double useSr = (sampleRate > 1000.0) ? sampleRate : sr;
        curBudgetUs = 1.0e6 * (double) curSamples / useSr;

        if (prevEndTicks > 0 && curSamples >= 32)
        {
            const double gapUs = (double) (nowTicks - prevEndTicks) * ticksToMicros;
            // 再生停止・一時停止(>250ms)は除外し、プラグイン負荷が35%未満と軽いのに
            // ホスト側のコールバック間隔が予算の2.5倍+1.8msを超えた場合は「DAW他トラック/ASIO遅延」として記録
            const double stallThreshUs = std::max (curBudgetUs * 2.5 + 1800.0, 6500.0);
            if (gapUs > stallThreshUs && gapUs < 250000.0 && prevPluginLoadPct < 35.0f)
                hostGapDropoutsAtom.fetch_add (1, std::memory_order_relaxed);
        }
    }

    inline void endCallback() noexcept
    {
        const juce::int64 endTicks = juce::Time::getHighResolutionTicks();
        prevEndTicks = endTicks;
        const double elapsedUs = std::max (0.0, (double) (endTicks - startTicks) * ticksToMicros);
        const float loadPct = (float) std::clamp (100.0 * elapsedUs / std::max (curBudgetUs, 50.0), 0.0, 999.0);
        prevPluginLoadPct = loadPct;

        if (curSamples >= 32)
        {
            const float alpha = (loadPct > smoothedLoadPct) ? 0.22f : 0.045f;
            smoothedLoadPct += alpha * (loadPct - smoothedLoadPct);
            decayingPeakPct  = std::max (loadPct, decayingPeakPct * 0.992f);

            if (loadPct >= 80.0f)
                pluginOverrunsAtom.fetch_add (1, std::memory_order_relaxed);

            avgLoadPctAtom.store     (smoothedLoadPct, std::memory_order_relaxed);
            peakLoadPctAtom.store    (decayingPeakPct, std::memory_order_relaxed);
            lastElapsedUsAtom.store  ((float) elapsedUs, std::memory_order_relaxed);
            lastBudgetUsAtom.store   ((float) curBudgetUs, std::memory_order_relaxed);
            lastBlockSizeAtom.store  (curSamples, std::memory_order_relaxed);
        }
    }

    void resetCounters() noexcept
    {
        pluginOverrunsAtom.store  (0, std::memory_order_relaxed);
        hostGapDropoutsAtom.store (0, std::memory_order_relaxed);
        decayingPeakPct = smoothedLoadPct;
        peakLoadPctAtom.store (decayingPeakPct, std::memory_order_relaxed);
    }

    float avgLoadPct() const noexcept      { return avgLoadPctAtom.load     (std::memory_order_relaxed); }
    float peakLoadPct() const noexcept     { return peakLoadPctAtom.load    (std::memory_order_relaxed); }
    float lastElapsedUs() const noexcept   { return lastElapsedUsAtom.load  (std::memory_order_relaxed); }
    float lastBudgetUs() const noexcept    { return lastBudgetUsAtom.load   (std::memory_order_relaxed); }
    int   lastBlockSize() const noexcept   { return lastBlockSizeAtom.load  (std::memory_order_relaxed); }
    int   pluginOverruns() const noexcept  { return pluginOverrunsAtom.load (std::memory_order_relaxed); }
    int   hostGapDropouts() const noexcept { return hostGapDropoutsAtom.load(std::memory_order_relaxed); }

private:
    double sr = 48000.0;
    double ticksToMicros = 0.1;
    juce::int64 startTicks   = 0;
    juce::int64 prevEndTicks = 0;
    int    curSamples  = 128;
    double curBudgetUs = 2666.7;
    float  prevPluginLoadPct = 0.0f;
    float  smoothedLoadPct   = 0.0f;
    float  decayingPeakPct   = 0.0f;

    std::atomic<float> avgLoadPctAtom      { 0.0f };
    std::atomic<float> peakLoadPctAtom     { 0.0f };
    std::atomic<float> lastElapsedUsAtom   { 0.0f };
    std::atomic<float> lastBudgetUsAtom    { 2666.7f };
    std::atomic<int>   lastBlockSizeAtom   { 128 };
    std::atomic<int>   pluginOverrunsAtom  { 0 };
    std::atomic<int>   hostGapDropoutsAtom { 0 };
};

} // namespace gz::v580
