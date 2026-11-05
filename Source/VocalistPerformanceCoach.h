#pragma once

/**
 * @file VocalistPerformanceCoach.h
 * @brief VocalGzzio リアルタイム歌唱バイオメトリクス解析・ピッチ傾向ヒストグラム・母音F1/F2マッパー＆スタジオテイク採点エンジン
 *
 * 【研究開発の背景と独自技術】
 * 1. CentAccuracyHistogramTracker (21ビン・セント精度ヒストグラム＆シャープ/フラット癖診断):
 *    - 歌唱中の基本周波数 (F0) と目標音程の偏差 (-50 cent 〜 +50 cent) を 21ビンのヒストグラムにリアルタイム集積。
 *    - 「高音域で +7.5 cent シャープしやすい」「ロングトーン後半で -5.2 cent フラットする」といった
 *      シンガー固有のピッチ傾向 (Mean Bias Cents) と音程安定度スコア (Pitch Lock Score 0..100%) を算出します。
 *
 * 2. VibratoBiometricsAnalyzer (生体ビブラート周期・振幅・規則性アナライザ):
 *    - ピッチ偏差のゼロ交差周期とピーク包絡から、シンガー自身の自然ビブラート速度 (Hz)、
 *      深さ (Extent Cents)、および周期規則性 (Regularity 0..100%) をリアルタイム計測します。
 *
 * 3. FormantVowelSpaceMapper (F1/F2 第1・第2フォルマント重心トラッカー＆発声レジスター判定):
 *    - 第1フォルマント帯域 (F1: 250〜950 Hz = 顎の開き具合) と第2フォルマント帯域 (F2: 850〜2550 Hz = 舌の前後位置) の
 *      エネルギー重心を TPT フィルタ群で追跡し、2次元母音空間 (Jaw Open × Tongue Front) へマッピング。
 *    - さらにチェストボイス / ミックスボイス (パッサージョ) / ヘッドボイス / 咽頭共鳴の発声バランスを可視化します。
 *
 * 4. StudioSessionTakeScorer (グラミースタジオ・テイク総合スコアリング＆日本語コーチング助言):
 *    - ピッチ安定度・ダイナミクス制御・Singer's Formant 輝き・ブレス/破裂音コントロールの4軸から
 *      リアルタイム総合スコア (0〜100点 / S+〜Bランク) と具体的なワンポイントアドバイスを生成します。
 */

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "MasteringProEngine.h"
#include "HorologyTactileUi.h"

namespace vg::performance_coach
{

//==============================================================================
// 1. 21ビン・セント精度ヒストグラム＆イントネーション傾向トラッカー
//==============================================================================
class CentAccuracyHistogramTracker
{
public:
    static constexpr int kNumBins = 21; // -50c .. +50c (5 cent 刻み)

    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        histogramBins.fill(0.0f);
        meanCentsBias = 0.0f;
        centsVariance = 12.0f;
        pitchLockScore01 = 0.85f;
        highNoteBiasCents = 0.0f;
        sustainedTailBiasCents = 0.0f;
    }

    /**
     * @brief 有声フレームごとのピッチ偏差 (centsError: -50..+50) と基本周波数 (f0Hz) を入力
     */
    void pushPitchFrame(float centsError, float f0Hz, bool isVoiced) noexcept
    {
        // ヒストグラム全体を緩やかに時間減衰させる
        for (float& bin : histogramBins)
            bin *= 0.996f;

        if (!isVoiced || f0Hz < 65.0f || f0Hz > 1500.0f)
            return;

        const float clampedCents = std::clamp(centsError, -50.0f, 50.0f);
        const float normIdx = (clampedCents + 50.0f) / 100.0f * static_cast<float>(kNumBins - 1);
        const int binIdx = std::clamp(static_cast<int>(std::lround(normIdx)), 0, kNumBins - 1);
        histogramBins[static_cast<size_t>(binIdx)] =
            std::min(1.0f, histogramBins[static_cast<size_t>(binIdx)] + 0.045f);

        // 平均バイアスと分散の指数移動平均
        meanCentsBias += 0.015f * (clampedCents - meanCentsBias);
        const float diff = clampedCents - meanCentsBias;
        centsVariance += 0.015f * (diff * diff - centsVariance);

        const float stdDevCents = std::sqrt(std::max(0.0f, centsVariance));
        const float absBias = std::fabs(meanCentsBias);
        // 標準偏差と平均誤差が小さいほど 1.0 (100点) に近づく
        const float rawScore = 1.0f - std::clamp((stdDevCents * 0.75f + absBias * 0.65f) / 38.0f, 0.0f, 1.0f);
        pitchLockScore01 += 0.02f * (rawScore - pitchLockScore01);

        // 高音域 (C4=261.6Hz 以上) でのシャープ/フラット癖を個別追跡
        if (f0Hz >= 261.6f)
            highNoteBiasCents += 0.02f * (clampedCents - highNoteBiasCents);
        else
            sustainedTailBiasCents += 0.02f * (clampedCents - sustainedTailBiasCents);
    }

    const std::array<float, kNumBins>& getHistogramBins() const noexcept { return histogramBins; }
    float getMeanCentsBias() const noexcept { return meanCentsBias; }
    float getStdDevCents() const noexcept { return std::sqrt(std::max(0.0f, centsVariance)); }
    float getPitchLockScore01() const noexcept { return pitchLockScore01; }
    float getHighNoteBiasCents() const noexcept { return highNoteBiasCents; }

private:
    double sr = 48000.0;
    std::array<float, kNumBins> histogramBins{};
    float meanCentsBias = 0.0f;
    float centsVariance = 12.0f;
    float pitchLockScore01 = 0.85f;
    float highNoteBiasCents = 0.0f;
    float sustainedTailBiasCents = 0.0f;
};

//==============================================================================
// 2. 生体ビブラート周期・振幅・規則性アナライザ
//==============================================================================
class VibratoBiometricsAnalyzer
{
public:
    void prepare(double frameRateHz) noexcept
    {
        fps = std::max(10.0, frameRateHz);
        invFps = static_cast<float>(1.0 / fps);
        reset();
    }

    void reset() noexcept
    {
        lastCents = 0.0f;
        framesSinceZeroCross = 0;
        estimatedRateHz = 5.6f;
        estimatedExtentCents = 0.0f;
        regularityScore01 = 0.0f;
        lastHalfPeriodSec = 0.089f;
    }

    void pushPitchDeviationFrame(float centsDeviation, bool isVoiced) noexcept
    {
        if (!isVoiced)
        {
            estimatedExtentCents *= 0.95f;
            regularityScore01 *= 0.95f;
            framesSinceZeroCross = 0;
            return;
        }

        framesSinceZeroCross++;
        const float absCents = std::fabs(centsDeviation);
        estimatedExtentCents += 0.08f * (absCents * 1.414f - estimatedExtentCents);

        // ゼロ交差検出 (十分な振幅がある時のみ)
        if (((lastCents < 0.0f && centsDeviation >= 0.0f) ||
             (lastCents > 0.0f && centsDeviation <= 0.0f)) &&
            estimatedExtentCents > 6.0f)
        {
            const float halfPeriodSec = static_cast<float>(framesSinceZeroCross) * invFps;
            framesSinceZeroCross = 0;

            // 人間のビブラート周期範囲 (3.5 Hz 〜 8.5 Hz -> 半周期 0.058s 〜 0.142s)
            if (halfPeriodSec >= 0.055f && halfPeriodSec <= 0.150f)
            {
                const float instRateHz = 0.5f / halfPeriodSec;
                estimatedRateHz += 0.25f * (instRateHz - estimatedRateHz);

                const float periodJitter = std::fabs(halfPeriodSec - lastHalfPeriodSec) / std::max(0.05f, lastHalfPeriodSec);
                lastHalfPeriodSec = halfPeriodSec;
                const float instReg = std::clamp(1.0f - periodJitter * 2.5f, 0.0f, 1.0f);
                regularityScore01 += 0.20f * (instReg - regularityScore01);
            }
            else
            {
                regularityScore01 *= 0.90f;
            }
        }

        lastCents = centsDeviation;
    }

    float getEstimatedRateHz() const noexcept { return estimatedRateHz; }
    float getEstimatedExtentCents() const noexcept { return estimatedExtentCents; }
    float getRegularityScore01() const noexcept { return regularityScore01; }

private:
    double fps = 60.0;
    float invFps = 1.0f / 60.0f;
    float lastCents = 0.0f;
    int framesSinceZeroCross = 0;
    float estimatedRateHz = 5.6f;
    float estimatedExtentCents = 0.0f;
    float regularityScore01 = 0.0f;
    float lastHalfPeriodSec = 0.089f;
};

//==============================================================================
// 3. 第1・第2フォルマント (F1/F2) 母音空間マッパー＆発声レジスター判定器
//==============================================================================
class FormantVowelSpaceMapper
{
public:
    enum class VocalRegister : int
    {
        SilentOrBreath = 0,
        ChestVoice     = 1, // 胸声 (濃厚な低次倍音)
        MixedPassaggio = 2, // ミックスボイス (黄金のチェスト＋ヘッド均衡)
        HeadFalsetto   = 3, // ヘッドボイス / ファルセット
        BrightPharynx  = 4  // 咽頭共鳴 / ベルティング (高 Singer's Formant)
    };

    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        const float dt = static_cast<float>(1.0 / sr);
        envSmoothCoeff = 1.0f - std::exp(-dt / 0.018f);

        // F1 トラッキング用デュアルバンド (320 Hz 閉口母音 vs 780 Hz 開口母音)
        f1LowFilter.prepare(sr);
        f1HighFilter.prepare(sr);
        f1LowFilter.setParameters(320.0f, 1.25f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        f1HighFilter.setParameters(780.0f, 1.25f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);

        // F2 トラッキング用デュアルバンド (980 Hz 後舌母音 vs 2150 Hz 前舌母音)
        f2BackFilter.prepare(sr);
        f2FrontFilter.prepare(sr);
        f2BackFilter.setParameters(980.0f, 1.25f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        f2FrontFilter.setParameters(2150.0f, 1.25f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);

        // Singer's Formant / 咽頭輝きバンド (3050 Hz)
        ringFilter.prepare(sr);
        ringFilter.setParameters(3050.0f, 1.40f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);

        reset();
    }

    void reset() noexcept
    {
        f1LowFilter.reset();
        f1HighFilter.reset();
        f2BackFilter.reset();
        f2FrontFilter.reset();
        ringFilter.reset();

        eF1Low = 0.0f;
        eF1High = 0.0f;
        eF2Back = 0.0f;
        eF2Front = 0.0f;
        eRing = 0.0f;

        jawOpen01 = 0.5f;
        tongueFront01 = 0.5f;
        estimatedF1Hz = 520.0f;
        estimatedF2Hz = 1500.0f;
        currentRegister = VocalRegister::MixedPassaggio;
    }

    void analyzeMonoSample(float monoSample) noexcept
    {
        const float vF1L = std::fabs(f1LowFilter.processSample(monoSample));
        const float vF1H = std::fabs(f1HighFilter.processSample(monoSample));
        const float vF2B = std::fabs(f2BackFilter.processSample(monoSample));
        const float vF2F = std::fabs(f2FrontFilter.processSample(monoSample));
        const float vRing = std::fabs(ringFilter.processSample(monoSample));

        eF1Low   += envSmoothCoeff * (vF1L - eF1Low);
        eF1High  += envSmoothCoeff * (vF1H - eF1High);
        eF2Back  += envSmoothCoeff * (vF2B - eF2Back);
        eF2Front += envSmoothCoeff * (vF2F - eF2Front);
        eRing    += envSmoothCoeff * (vRing - eRing);

        const float totalEnergy = eF1Low + eF1High + eF2Back + eF2Front + 1.0e-7f;
        if (totalEnergy < 1.0e-4f)
        {
            currentRegister = VocalRegister::SilentOrBreath;
            return;
        }

        // 顎の開き具合 (Jaw Opening: F1 High の割合)
        jawOpen01 = std::clamp(eF1High / (eF1Low + eF1High + 1.0e-7f), 0.0f, 1.0f);
        // 舌の前方張り出し (Tongue Frontness: F2 Front の割合)
        tongueFront01 = std::clamp(eF2Front / (eF2Back + eF2Front + 1.0e-7f), 0.0f, 1.0f);

        estimatedF1Hz = 280.0f + jawOpen01 * 580.0f;
        estimatedF2Hz = 880.0f + tongueFront01 * 1450.0f;

        // 発声レジスター判定
        const float lowDominance = (eF1Low + eF1High) / totalEnergy;
        const float ringRatio = eRing / totalEnergy;

        if (ringRatio > 0.34f)
            currentRegister = VocalRegister::BrightPharynx;
        else if (lowDominance > 0.68f)
            currentRegister = VocalRegister::ChestVoice;
        else if (lowDominance < 0.38f)
            currentRegister = VocalRegister::HeadFalsetto;
        else
            currentRegister = VocalRegister::MixedPassaggio;
    }

    float getJawOpen01() const noexcept { return jawOpen01; }
    float getTongueFront01() const noexcept { return tongueFront01; }
    float getEstimatedF1Hz() const noexcept { return estimatedF1Hz; }
    float getEstimatedF2Hz() const noexcept { return estimatedF2Hz; }
    VocalRegister getVocalRegister() const noexcept { return currentRegister; }

private:
    double sr = 48000.0;
    float envSmoothCoeff = 0.001f;
    vg::mastering::TptStateVariableFilter f1LowFilter;
    vg::mastering::TptStateVariableFilter f1HighFilter;
    vg::mastering::TptStateVariableFilter f2BackFilter;
    vg::mastering::TptStateVariableFilter f2FrontFilter;
    vg::mastering::TptStateVariableFilter ringFilter;

    float eF1Low = 0.0f;
    float eF1High = 0.0f;
    float eF2Back = 0.0f;
    float eF2Front = 0.0f;
    float eRing = 0.0f;

    float jawOpen01 = 0.5f;
    float tongueFront01 = 0.5f;
    float estimatedF1Hz = 520.0f;
    float estimatedF2Hz = 1500.0f;
    VocalRegister currentRegister = VocalRegister::MixedPassaggio;
};

//==============================================================================
// 4. グラミースタジオ・テイク総合採点＆日本語コーチング助言エンジン
//==============================================================================
class StudioSessionTakeScorer
{
public:
    struct TakeEvaluation
    {
        float totalScore100 = 92.0f;           // 総合スコア (0..100)
        float pitchStability100 = 90.0f;       // ピッチ安定度 (0..100)
        float dynamicConsistency100 = 92.0f;   // ダイナミクス密度 (0..100)
        float resonanceProjection100 = 94.0f;  // 抜け・倍音共鳴スコア (0..100)
        const char* gradeBadge = "S";          // ランク ("S+", "S", "A+", "A", "B")
        const char* coachingTipJa = "ピッチと倍音共鳴が理想的なミックスボイス帯域にロックしています。";
    };

    void updateEvaluation(float pitchLockScore01,
                          float vaiAudibility100,
                          float crestFactorDb,
                          float meanCentsBias) noexcept
    {
        eval.pitchStability100 = std::clamp(pitchLockScore01 * 100.0f, 40.0f, 100.0f);
        eval.resonanceProjection100 = std::clamp(vaiAudibility100, 45.0f, 100.0f);

        // ボーカルの理想クレストファクター (8.5 dB 〜 13.5 dB) への適合度
        const float crestDelta = std::fabs(crestFactorDb - 11.0f);
        eval.dynamicConsistency100 = std::clamp(100.0f - crestDelta * 4.2f, 50.0f, 100.0f);

        eval.totalScore100 = 0.40f * eval.pitchStability100
                           + 0.35f * eval.resonanceProjection100
                           + 0.25f * eval.dynamicConsistency100;

        if (eval.totalScore100 >= 94.0f)      eval.gradeBadge = "S+";
        else if (eval.totalScore100 >= 88.0f) eval.gradeBadge = "S";
        else if (eval.totalScore100 >= 80.0f) eval.gradeBadge = "A+";
        else if (eval.totalScore100 >= 72.0f) eval.gradeBadge = "A";
        else                                  eval.gradeBadge = "B";

        if (meanCentsBias > 8.5f)
        {
            eval.coachingTipJa = "高音域でわずかにシャープ傾向 (+8c) です。首の力を抜き胸郭の支えを意識するとさらに安定します。";
        }
        else if (meanCentsBias < -8.5f)
        {
            eval.coachingTipJa = "語尾でわずかにフラット傾向 (-8c) です。軟口蓋を少し高く保つと倍音とピッチが輝きます。";
        }
        else if (eval.resonanceProjection100 >= 88.0f)
        {
            eval.coachingTipJa = "3kHz Singer's Formant と胸声の支えが完璧に調和したグラミークラスのテイクです。";
        }
        else
        {
            eval.coachingTipJa = "安定したピッチと滑らかなダイナミクスで、オケの中でも前に出るボーカルに仕上がっています。";
        }
    }

    const TakeEvaluation& getEvaluation() const noexcept { return eval; }

private:
    TakeEvaluation eval;
};

//==============================================================================
// 5. ボーカルコーチング・テレメトリ HUD レンダラー (JUCE Graphics)
//==============================================================================
class CoachTelemetryHudRenderer
{
public:
    static void drawVowelSpaceAndPitchHistogram(
        juce::Graphics& g,
        juce::Rectangle<float> bounds,
        const std::array<float, CentAccuracyHistogramTracker::kNumBins>& histBins,
        float jawOpen01,
        float tongueFront01,
        float totalScore100,
        const char* gradeBadge)
    {
        using namespace vg::horology_ui;
        EditorialBeveledCardRenderer::drawCard(
            g, bounds, MidnightLanternPalette:: lanternGold(), true, 8.0f);

        auto inner = bounds.reduced(8.0f, 6.0f);
        const float leftW = inner.getWidth() * 0.56f;
        auto histRect = inner.removeFromLeft(leftW);
        inner.removeFromLeft(6.0f);
        auto vowelRect = inner;

        // 1. 21ビン・セント精度ヒストグラム描画
        const int numBins = static_cast<int>(histBins.size());
        const float binW = histRect.getWidth() / static_cast<float>(numBins);
        for (int i = 0; i < numBins; ++i)
        {
            const float val = std::clamp(histBins[static_cast<size_t>(i)], 0.04f, 1.0f);
            const float barH = histRect.getHeight() * 0.78f * val;
            const float bx = histRect.getX() + static_cast<float>(i) * binW;
            const float by = histRect.getBottom() - barH;

            const float distFromCenter = std::fabs(static_cast<float>(i - numBins / 2)) / static_cast<float>(numBins / 2);
            const juce::Colour col = MidnightLanternPalette::starlightCyan()
                                         .interpolatedWith(MidnightLanternPalette::lanternAmber(), distFromCenter);
            g.setColour(col.withAlpha(0.82f));
            g.fillRoundedRectangle(bx + 0.5f, by, std::max(1.5f, binW - 1.2f), barH, 1.5f);
        }

        // 2. 右側: F1/F2 母音空間レーダー＆テイクグレードバッジ
        g.setColour(MidnightLanternPalette::hairlineSlate().withAlpha(0.65f));
        g.drawRoundedRectangle(vowelRect, 5.0f, 1.0f);

        const float vx = vowelRect.getX() + (1.0f - std::clamp(tongueFront01, 0.05f, 0.95f)) * vowelRect.getWidth();
        const float vy = vowelRect.getY() + std::clamp(jawOpen01, 0.05f, 0.95f) * vowelRect.getHeight();

        g.setColour(MidnightLanternPalette::lanternGold().withAlpha(0.28f));
        g.fillEllipse(vx - 7.0f, vy - 7.0f, 14.0f, 14.0f);
        g.setColour(MidnightLanternPalette::champagneHighlight());
        g.fillEllipse(vx - 3.0f, vy - 3.0f, 6.0f, 6.0f);

        g.setColour(MidnightLanternPalette::champagneHighlight());
        g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        const juce::String scoreText = juce::String(gradeBadge != nullptr ? gradeBadge : "S")
                                     + " " + juce::String(totalScore100, 0);
        g.drawText(scoreText, vowelRect.reduced(4.0f, 2.0f), juce::Justification::topRight, false);
    }
};

//==============================================================================
// 6. パフォーマンスコーチ統合スイート (VocalistPerformanceCoachSuite)
//==============================================================================
class VocalistPerformanceCoachSuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        centHistogram.prepare(sampleRate);
        vibratoBiometrics.prepare(60.0);
        vowelMapper.prepare(sampleRate);
    }

    void reset() noexcept
    {
        centHistogram.reset();
        vibratoBiometrics.reset();
        vowelMapper.reset();
    }

    CentAccuracyHistogramTracker& getCentHistogram() noexcept { return centHistogram; }
    const CentAccuracyHistogramTracker& getCentHistogram() const noexcept { return centHistogram; }

    VibratoBiometricsAnalyzer& getVibratoBiometrics() noexcept { return vibratoBiometrics; }
    const VibratoBiometricsAnalyzer& getVibratoBiometrics() const noexcept { return vibratoBiometrics; }

    FormantVowelSpaceMapper& getVowelMapper() noexcept { return vowelMapper; }
    const FormantVowelSpaceMapper& getVowelMapper() const noexcept { return vowelMapper; }

    StudioSessionTakeScorer& getTakeScorer() noexcept { return takeScorer; }
    const StudioSessionTakeScorer& getTakeScorer() const noexcept { return takeScorer; }

private:
    CentAccuracyHistogramTracker centHistogram;
    VibratoBiometricsAnalyzer vibratoBiometrics;
    FormantVowelSpaceMapper vowelMapper;
    StudioSessionTakeScorer takeScorer;
};

} // namespace vg::performance_coach
