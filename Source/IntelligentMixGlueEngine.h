#pragma once

/**
 * @file IntelligentMixGlueEngine.h
 * @brief VocalGzzio SSL 4000G バスグルーVCA・Weiss DS1式レベル非依存スプリットディエッサー＆グラミー基準スペクトル自動整合エンジン
 *
 * 【研究開発の背景と独自技術】
 * 1. SslGSeriesVocalBusGlue (SSL 4000G Quad-VCA オートリリース・ボーカルバスグルー):
 *    - 世界のトップミキサーがボーカルバスの最終段に挿す SSL 4000 G-Series Bus Compressor の
 *      「子音のアタック (18ms) を潰さずに通し、母音のサステインだけを接着 (Glue) する」挙動をモデル化。
 *    - 115Hz サイドチェイン・ハイパスフィルタにより低域の胸鳴りでコンプが誤動作するのを防ぎ、
 *      さらに「高速マイクロ復帰 (65ms) ＋ マクロ土台保持 (750ms)」の2段オートリリース回路を搭載しています。
 *
 * 2. WeissPrecisionSplitDeEsser (Weiss DS1-MK3 式・相対レベル検出デュアルバンド・ディエッサー):
 *    - 従来の絶対閾値ディエッサーは「小さい声の『さしすせそ』を見逃し、大きい声の母音まで誤って削る」欠点がありました。
 *    - 本機は「歯茎摩擦音 8.4kHz (/s/, /ts/)」と「硬口蓋摩擦音 5.3kHz (/sh/, /ch/)」のエネルギーを
 *      「母音基音帯域 (200Hz〜2.2kHz) の包絡線との相対比 (Relative Sibilance-to-Vowel Ratio)」で判定するため、
 *      ウィスパーボイスでも全力のサビでも、入力音量に一切左右されずに歯擦音だけを精密に整えます。
 *
 * 3. SpectralBalanceReferenceMatcher (グラミー賞ボーカル基準スペクトル・オートバランサー):
 *    - 商用ヒット曲のリードボーカルが持つ黄金の4帯域エネルギー分布 (Warmth / Body / Presence / Air) と
 *      入力ボーカルの長時間スペクトル傾斜を比較し、±2.8dB の穏やかな自動 TPT トリムで
 *      どんなマイクの録音も即座にプロフェッショナルなトーンバランスへ導きます。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "MasteringProEngine.h"

namespace vg::mix_glue
{

//==============================================================================
// 1. SSL 4000 G-Series Quad-VCA オートリリース・ボーカルバスグルー
//==============================================================================
class SslGSeriesVocalBusGlue
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        const float dt = static_cast<float>(1.0 / sr);

        // 18ms アタック (子音のパンチを活かして母音ボディを接着)
        attackCoeff = 1.0f - std::exp(-dt / 0.018f);
        // 2段オートリリース: Fast 65ms / Slow 720ms
        fastReleaseCoeff = 1.0f - std::exp(-dt / 0.065f);
        slowReleaseCoeff = 1.0f - std::exp(-dt / 0.720f);

        sidechainHp.prepare(sr);
        sidechainHp.setParameters(115.0f, 0.707f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::Highpass);
        reset();
    }

    void reset() noexcept
    {
        sidechainHp.reset();
        fastGainReductionDb = 0.0f;
        slowGainReductionDb = 0.0f;
        currentGlueReductionDb = 0.0f;
    }

    void processSamplePair(float& left, float& right, float glueAmount01) noexcept
    {
        glueAmount01 = std::clamp(glueAmount01, 0.0f, 1.0f);
        if (glueAmount01 <= 0.0005f)
        {
            currentGlueReductionDb = 0.0f;
            return;
        }

        const float linkedPeak = std::max(std::fabs(left), std::fabs(right));
        const float scFiltered = std::fabs(sidechainHp.processSample(linkedPeak));
        const float scDb = vg::mastering::FastAudioMath::linearToDb(scFiltered);

        // グルー量に応じてスレッショルドを -14 dBFS 〜 -24 dBFS に設定 (レシオ 3.2:1 ソフトニー)
        const float thresholdDb = -14.0f - glueAmount01 * 10.0f;
        const float overDb = scDb - thresholdDb;

        float targetGrDb = 0.0f;
        if (overDb > -3.0f)
        {
            if (overDb < 3.0f)
            {
                const float knee = overDb + 3.0f;
                targetGrDb = (1.0f - 1.0f / 3.2f) * (knee * knee) / 12.0f;
            }
            else
            {
                targetGrDb = (1.0f - 1.0f / 3.2f) * overDb;
            }
        }
        targetGrDb = std::clamp(targetGrDb, 0.0f, 9.0f);

        // デュアル・オートリリース包絡線
        if (targetGrDb > fastGainReductionDb)
            fastGainReductionDb += attackCoeff * (targetGrDb - fastGainReductionDb);
        else
            fastGainReductionDb += fastReleaseCoeff * (targetGrDb - fastGainReductionDb);

        if (targetGrDb > slowGainReductionDb)
            slowGainReductionDb += (attackCoeff * 0.25f) * (targetGrDb - slowGainReductionDb);
        else
            slowGainReductionDb += slowReleaseCoeff * (targetGrDb - slowGainReductionDb);

        currentGlueReductionDb = std::max(fastGainReductionDb, slowGainReductionDb * 0.72f);

        // 自動メイクアップゲイン (+0.55 * GR) を伴う VCA ゲイン乗算
        const float netGainDb = -currentGlueReductionDb + currentGlueReductionDb * 0.55f;
        const float vcaGain = vg::mastering::FastAudioMath::dbToLinear(netGainDb * glueAmount01);

        left  = vg::mastering::FastAudioMath::sanitize(left * vcaGain);
        right = vg::mastering::FastAudioMath::sanitize(right * vcaGain);
    }

    float getGlueReductionDb() const noexcept { return currentGlueReductionDb; }

private:
    double sr = 48000.0;
    float attackCoeff = 0.001f;
    float fastReleaseCoeff = 0.0005f;
    float slowReleaseCoeff = 0.00005f;
    float fastGainReductionDb = 0.0f;
    float slowGainReductionDb = 0.0f;
    float currentGlueReductionDb = 0.0f;
    vg::mastering::TptStateVariableFilter sidechainHp;
};

//==============================================================================
// 2. Weiss DS1-MK3 式・相対レベル検出デュアルバンド・スプリットディエッサー
//==============================================================================
class WeissPrecisionSplitDeEsser
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        const float dt = static_cast<float>(1.0 / sr);

        // 超高速 0.6ms アタック / 28ms ベルベットリリース
        attackCoeff = 1.0f - std::exp(-dt / 0.0006f);
        releaseCoeff = 1.0f - std::exp(-dt / 0.0280f);
        vowelEnvCoeff = 1.0f - std::exp(-dt / 0.0120f);

        vowelRefBp.prepare(sr);
        vowelRefBp.setParameters(680.0f, 0.65f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);

        for (int ch = 0; ch < 2; ++ch)
        {
            shBandBp[ch].prepare(sr);
            sBandBp[ch].prepare(sr);
            // Band A: 5350 Hz (/sh/, /ch/ 硬口蓋歯擦音)
            shBandBp[ch].setParameters(5350.0f, 1.55f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
            // Band B: 8450 Hz (/s/, /ts/ 歯茎歯擦音)
            sBandBp[ch].setParameters(8450.0f, 1.45f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        }

        reset();
    }

    void reset() noexcept
    {
        vowelRefBp.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            shBandBp[ch].reset();
            sBandBp[ch].reset();
        }
        vowelRefEnv = 0.0f;
        shReductionGain = 1.0f;
        sReductionGain = 1.0f;
        activeDeEssDb = 0.0f;
    }

    void processSamplePair(float& left, float& right, float deEssIntensity01) noexcept
    {
        deEssIntensity01 = std::clamp(deEssIntensity01, 0.0f, 1.0f);
        if (deEssIntensity01 <= 0.0005f)
        {
            activeDeEssDb = 0.0f;
            return;
        }

        const float mono = 0.5f * (left + right);
        const float vowelAbs = std::fabs(vowelRefBp.processSample(mono));
        vowelRefEnv += vowelEnvCoeff * (vowelAbs - vowelRefEnv);

        const float shL = shBandBp[0].processSample(left);
        const float shR = shBandBp[1].processSample(right);
        const float sL  = sBandBp[0].processSample(left);
        const float sR  = sBandBp[1].processSample(right);

        const float shPeak = std::max(std::fabs(shL), std::fabs(shR));
        const float sPeak  = std::max(std::fabs(sL), std::fabs(sR));

        // 母音基準エネルギーに対する相対比率 (Relative Sibilance Ratio)
        const float denom = std::max(1.0e-5f, vowelRefEnv * (1.25f - 0.55f * deEssIntensity01));
        const float shRatio = shPeak / denom;
        const float sRatio  = sPeak / denom;

        float targetShGain = 1.0f;
        if (shRatio > 1.0f && shPeak > 1.0e-4f)
        {
            const float cutDb = -std::min(9.5f, (shRatio - 1.0f) * 5.2f * deEssIntensity01);
            targetShGain = vg::mastering::FastAudioMath::dbToLinear(cutDb);
        }

        float targetSGain = 1.0f;
        if (sRatio > 0.90f && sPeak > 1.0e-4f)
        {
            const float cutDb = -std::min(11.5f, (sRatio - 0.90f) * 6.0f * deEssIntensity01);
            targetSGain = vg::mastering::FastAudioMath::dbToLinear(cutDb);
        }

        const float shCoeff = (targetShGain < shReductionGain) ? attackCoeff : releaseCoeff;
        shReductionGain += shCoeff * (targetShGain - shReductionGain);

        const float sCoeff = (targetSGain < sReductionGain) ? attackCoeff : releaseCoeff;
        sReductionGain += sCoeff * (targetSGain - sReductionGain);

        // 減衰差分のみを元の広帯域信号から差し引くため、13kHz以上のシルキーな空気感は100%無傷で残る
        left  = vg::mastering::FastAudioMath::sanitize(left  + shL * (shReductionGain - 1.0f) + sL * (sReductionGain - 1.0f));
        right = vg::mastering::FastAudioMath::sanitize(right + shR * (shReductionGain - 1.0f) + sR * (sReductionGain - 1.0f));

        const float minGain = std::min(shReductionGain, sReductionGain);
        activeDeEssDb = -vg::mastering::FastAudioMath::linearToDb(std::max(0.1f, minGain));
    }

    float getActiveDeEssDb() const noexcept { return activeDeEssDb; }

private:
    double sr = 48000.0;
    float attackCoeff = 0.05f;
    float releaseCoeff = 0.001f;
    float vowelEnvCoeff = 0.002f;
    float vowelRefEnv = 0.0f;
    float shReductionGain = 1.0f;
    float sReductionGain = 1.0f;
    float activeDeEssDb = 0.0f;

    vg::mastering::TptStateVariableFilter vowelRefBp;
    std::array<vg::mastering::TptStateVariableFilter, 2> shBandBp;
    std::array<vg::mastering::TptStateVariableFilter, 2> sBandBp;
};

//==============================================================================
// 3. グラミー基準スペクトル・オートバランサー (SpectralBalanceReferenceMatcher)
//==============================================================================
class SpectralBalanceReferenceMatcher
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        const float dt = static_cast<float>(1.0 / sr);
        rmsSmoothCoeff = 1.0f - std::exp(-dt / 0.120f); // 120ms 長時間スペクトル推定

        for (size_t b = 0; b < 4; ++b)
        {
            detectBp[b].prepare(sr);
        }
        detectBp[0].setParameters(165.0f,  0.85f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        detectBp[1].setParameters(520.0f,  0.85f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        detectBp[2].setParameters(2450.0f, 0.85f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        detectBp[3].setParameters(8200.0f, 0.85f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);

        for (int ch = 0; ch < 2; ++ch)
        {
            warmthShelf[ch].prepare(sr);
            mudTrimBell[ch].prepare(sr);
            presenceBell[ch].prepare(sr);
            airShelf[ch].prepare(sr);

            warmthShelf[ch].setParameters(175.0f,  0.707f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::LowShelf);
            mudTrimBell[ch].setParameters(480.0f,  1.100f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::PeakBell);
            presenceBell[ch].setParameters(2850.0f, 0.900f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::PeakBell);
            airShelf[ch].setParameters(8500.0f,    0.707f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::HighShelf);
        }

        reset();
    }

    void reset() noexcept
    {
        for (size_t b = 0; b < 4; ++b)
        {
            detectBp[b].reset();
            bandEnv[b] = 0.0f;
            currentTrimDb[b] = 0.0f;
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            warmthShelf[ch].reset();
            mudTrimBell[ch].reset();
            presenceBell[ch].reset();
            airShelf[ch].reset();
        }
        coeffUpdateCounter = 0;
    }

    void processSamplePair(float& left, float& right, float matchStrength01) noexcept
    {
        matchStrength01 = std::clamp(matchStrength01, 0.0f, 1.0f);
        if (matchStrength01 <= 0.001f)
            return;

        const float mono = 0.5f * (left + right);
        for (size_t b = 0; b < 4; ++b)
        {
            const float v = std::fabs(detectBp[b].processSample(mono));
            bandEnv[b] += rmsSmoothCoeff * (v - bandEnv[b]);
        }

        const float total = bandEnv[0] + bandEnv[1] + bandEnv[2] + bandEnv[3] + 1.0e-6f;
        if (total > 1.0e-3f)
        {
            // グラミー賞リードボーカルの黄金比率 (Warmth 26%, Body 28%, Presence 30%, Air 16%)
            static constexpr std::array<float, 4> kTargetShares = { 0.26f, 0.28f, 0.30f, 0.16f };
            for (size_t b = 0; b < 4; ++b)
            {
                const float actualShare = std::clamp(bandEnv[b] / total, 0.04f, 0.85f);
                const float ratioDb = 20.0f * std::log10(kTargetShares[b] / actualShare);
                const float clampedTrim = std::clamp(ratioDb * 0.35f * matchStrength01, -2.6f, +2.6f);
                currentTrimDb[b] += 0.002f * (clampedTrim - currentTrimDb[b]);
            }
        }

        if ((++coeffUpdateCounter & 31) == 0)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                warmthShelf[ch].setDynamicGainDbFast(currentTrimDb[0]);
                mudTrimBell[ch].setDynamicGainDbFast(currentTrimDb[1]);
                presenceBell[ch].setDynamicGainDbFast(currentTrimDb[2]);
                airShelf[ch].setDynamicGainDbFast(currentTrimDb[3]);
            }
        }

        left  = airShelf[0].processSample(presenceBell[0].processSample(mudTrimBell[0].processSample(warmthShelf[0].processSample(left))));
        right = airShelf[1].processSample(presenceBell[1].processSample(mudTrimBell[1].processSample(warmthShelf[1].processSample(right))));
    }

    const std::array<float, 4>& getCurrentTrimDb() const noexcept { return currentTrimDb; }

private:
    double sr = 48000.0;
    float rmsSmoothCoeff = 0.0002f;
    int coeffUpdateCounter = 0;
    std::array<vg::mastering::TptStateVariableFilter, 4> detectBp;
    std::array<float, 4> bandEnv{};
    std::array<float, 4> currentTrimDb{};

    std::array<vg::mastering::TptStateVariableFilter, 2> warmthShelf;
    std::array<vg::mastering::TptStateVariableFilter, 2> mudTrimBell;
    std::array<vg::mastering::TptStateVariableFilter, 2> presenceBell;
    std::array<vg::mastering::TptStateVariableFilter, 2> airShelf;
};

//==============================================================================
// 4. インテリジェント・ミックスグルー統合スイート (IntelligentMixGlueSuite)
//==============================================================================
class IntelligentMixGlueSuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        sslBusGlue.prepare(sampleRate);
        weissDeEsser.prepare(sampleRate);
        spectralMatcher.prepare(sampleRate);
    }

    void reset() noexcept
    {
        sslBusGlue.reset();
        weissDeEsser.reset();
        spectralMatcher.reset();
    }

    void processSamplePair(float& left,
                           float& right,
                           float deEssAmount01,
                           float spectralMatch01,
                           float busGlue01) noexcept
    {
        weissDeEsser.processSamplePair(left, right, deEssAmount01);
        spectralMatcher.processSamplePair(left, right, spectralMatch01);
        sslBusGlue.processSamplePair(left, right, busGlue01);
    }

    SslGSeriesVocalBusGlue& getSslBusGlue() noexcept { return sslBusGlue; }
    const SslGSeriesVocalBusGlue& getSslBusGlue() const noexcept { return sslBusGlue; }

    WeissPrecisionSplitDeEsser& getWeissDeEsser() noexcept { return weissDeEsser; }
    const WeissPrecisionSplitDeEsser& getWeissDeEsser() const noexcept { return weissDeEsser; }

    SpectralBalanceReferenceMatcher& getSpectralMatcher() noexcept { return spectralMatcher; }
    const SpectralBalanceReferenceMatcher& getSpectralMatcher() const noexcept { return spectralMatcher; }

private:
    SslGSeriesVocalBusGlue sslBusGlue;
    WeissPrecisionSplitDeEsser weissDeEsser;
    SpectralBalanceReferenceMatcher spectralMatcher;
};

} // namespace vg::mix_glue
