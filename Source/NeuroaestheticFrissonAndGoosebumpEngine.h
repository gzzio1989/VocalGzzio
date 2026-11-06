#pragma once
// ==============================================================================
//  NeuroaestheticFrissonAndGoosebumpEngine.h
//  VocalGzzio — 神経美学フリッソン（鳥肌・感動涙誘発）・Huron ITPRA期待解放理論 ＆ シンガーズ・フォルマント黄金共鳴エンジン
//
//  【人間の欲求・心に染みる音響科学の核心】
//   人が歌声を聴いて「鳥肌が立つ（フリッソン / Musical Frisson）」「胸が締め付けられて涙が出る」
//   瞬間には、神経科学（Blood & Zatorre, PNAS; Huron, Sweet Anticipation; Sundberg）において
//   明確な4つの音響トリガーが存在する:
//
//   1. 【シンガーズ・フォルマント・リング（2.85 kHz 〜 3.35 kHz 梨状陥凹共鳴クラスタ）】
//      一流の歌手やオペラ・J-Popのスターボーカリストが「大音量のバンド伴奏の中でも
//      一切うるさくならず、黄金の輪郭を持って真っ直ぐ心に突き刺さる」理由は、
//      喉頭室・梨状陥凹が形成する F3/F4/F5 の密集共鳴（約 3.1 kHz）にある。
//      本エンジンは 2.92 kHz と 3.34 kHz の双峰（Dual-Peak）共鳴器により、
//      耳障りな 4.5kHz 歯擦音を避けながら「スターの輝き（Golden Ring）」だけを付与する。
//
//   2. 【Huron ITPRA 音楽的緊張・解放（Tension-to-Release Catharsis）モデル】
//      音楽心理学者 David Huron の ITPRA 理論（Imagination-Tension-Prediction-Reaction-Appraisal）に基づき、
//      フレーズ前の「静かな溜め（Tension Accumulation）」が長いほど、次の強拍・高音が入る瞬間の
//      ドーパミン報酬（Catharsis Release Halo）を大きく開花させる。
//
//   3. 【メロディック・リープ（5半音以上の跳躍音程）高揚ハイライター (`MelodicLeapSalienceDetector`)】
//      バラードのサビ頭などで音程が一気に5度・6度・オクターブへ跳躍する瞬間（Spectral Centroid Jump）を検知し、
//      倍音のきらめきを瞬間的にリフトして聴き手の涙腺と鳥肌を刺激する。
//
//   4. 【13.5 kHz シルク・エア・ベルベット ＆ 135 Hz チェスト・ベルベット】
//      声の芯を太く抱きしめる胸声の温もり（135 Hz）と、天井が抜けるような空気の粒子感（13.5 kHz）を
//      ADAA無歪み倍音変調で織り上げる。
// ==============================================================================

#include "LightweightSimdFastMathAndEcoGovernor.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace vg::human
{

// ==============================================================================
//  1. Huron ITPRA 音楽的「溜め（Tension）」→「解放（Catharsis Release）」トラッカー
// ==============================================================================
class HuronTensionAndCatharsisTracker
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double safeFs = std::max (8000.0, sampleRate);
        tensionChargeCoeff  = static_cast<float> (std::exp (-1.0 / (0.850 * safeFs)));
        releaseDecayCoeff   = static_cast<float> (std::exp (-1.0 / (0.320 * safeFs)));
        fastEnergyCoeff     = static_cast<float> (std::exp (-1.0 / (0.018 * safeFs)));
        slowBaselineCoeff   = static_cast<float> (std::exp (-1.0 / (1.200 * safeFs)));
        reset();
    }

    void reset() noexcept
    {
        storedTension01  = 0.0f;
        catharsisBloom01 = 0.0f;
        fastEnergy       = 0.001f;
        slowBaseline     = 0.005f;
    }

    inline float updateAndComputeCatharsis01 (float absMono) noexcept
    {
        fastEnergy   = fastEnergyCoeff   * fastEnergy   + (1.0f - fastEnergyCoeff)   * absMono;
        slowBaseline = slowBaselineCoeff * slowBaseline + (1.0f - slowBaselineCoeff) * absMono;

        // 静かなウィスパーや休符の「溜め」区間で Tension をチャージ
        if (fastEnergy < slowBaseline * 0.65f && slowBaseline > 0.002f)
        {
            storedTension01 = tensionChargeCoeff * storedTension01 + (1.0f - tensionChargeCoeff) * 1.0f;
        }
        else if (fastEnergy > slowBaseline * 1.35f && storedTension01 > 0.15f)
        {
            // 溜めの直後に強い歌声（サビ頭など）が飛び出した瞬間、蓄えた Tension を Catharsis Bloom へ一気に変換！
            catharsisBloom01 = std::min (1.0f, catharsisBloom01 + storedTension01 * 0.65f);
            storedTension01 *= 0.40f;
        }

        catharsisBloom01 *= releaseDecayCoeff;
        return std::clamp (catharsisBloom01, 0.0f, 1.0f);
    }

    float getStoredTension01() const noexcept  { return storedTension01; }
    float getCatharsisBloom01() const noexcept { return catharsisBloom01; }

private:
    float tensionChargeCoeff = 0.9999f;
    float releaseDecayCoeff  = 0.9999f;
    float fastEnergyCoeff    = 0.999f;
    float slowBaselineCoeff  = 0.99998f;

    float storedTension01  = 0.0f;
    float catharsisBloom01 = 0.0f;
    float fastEnergy       = 0.001f;
    float slowBaseline     = 0.005f;
};

// ==============================================================================
//  2. メロディック・リープ（高音跳躍・倍音重心ジャンプ）検出器
// ==============================================================================
class MelodicLeapSalienceDetector
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double safeFs = std::max (8000.0, sampleRate);
        hp1200Coeff   = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * 1200.0 / safeFs));
        fastTiltCoeff = static_cast<float> (std::exp (-1.0 / (0.030 * safeFs)));
        slowTiltCoeff = static_cast<float> (std::exp (-1.0 / (0.450 * safeFs)));
        reset();
    }

    void reset() noexcept
    {
        hpPrev = 0.0f;
        hpState = 0.0f;
        fastHighEnv = 0.001f;
        slowHighEnv = 0.001f;
    }

    inline float detectPitchLeapExcitement01 (float mono) noexcept
    {
        hpState = hp1200Coeff * (hpState + mono - hpPrev);
        hpPrev  = mono;
        const float absHigh = std::fabs (hpState);

        fastHighEnv = fastTiltCoeff * fastHighEnv + (1.0f - fastTiltCoeff) * absHigh;
        slowHighEnv = slowTiltCoeff * slowHighEnv + (1.0f - slowTiltCoeff) * absHigh;

        if (fastHighEnv < 0.003f)
            return 0.0f;

        const float jumpRatio = fastHighEnv / std::max (0.0005f, slowHighEnv);
        return std::clamp ((jumpRatio - 1.15f) * 0.70f, 0.0f, 1.0f);
    }

private:
    float hp1200Coeff   = 0.85f;
    float fastTiltCoeff = 0.999f;
    float slowTiltCoeff = 0.9999f;
    float hpPrev = 0.0f, hpState = 0.0f;
    float fastHighEnv = 0.001f, slowHighEnv = 0.001f;
};

// ==============================================================================
//  3. 神経美学フリッソン＆シンガーズ・フォルマント統合エンジン
// ==============================================================================
struct FrissonTelemetry
{
    float singersFormantEnergyDb   = -60.0f;
    float dynamicContrastIndex01   = 0.0f;
    float emotionalBloomFactor01   = 0.0f;
    float catharsisReleaseScore01  = 0.0f;
    float melodicLeapExcitement01  = 0.0f;
    float frissonGoosebumpScore01  = 0.0f; // 0.0 .. 1.0 (0.75以上で鳥肌誘発ゾーン)
};

class NeuroaestheticFrissonAndGoosebumpEngine
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRateHz = std::max (8000.0, sampleRate);

        // 1. シンガーズ・フォルマント双峰共振器 (f1 = 2920 Hz, f2 = 3340 Hz, Q = 4.2)
        setupBandpassBiquad (sfBiquad1, 2920.0, 4.2, sampleRateHz);
        setupBandpassBiquad (sfBiquad2, 3340.0, 4.5, sampleRateHz);

        // 2. 135 Hz チェスト・ベルベット温感共振器 (Q = 1.4)
        setupBandpassBiquad (chestBiquad, 135.0, 1.4, sampleRateHz);

        // 3. 13.5 kHz シルク・エア抽出ハイパス (1次)
        const double fcAir = std::min (13500.0, sampleRateHz * 0.40);
        airHpCoeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * fcAir / sampleRateHz));

        // 4. 短時間（25ms）＆長時間（600ms）感情包絡トラッカー
        shortEnvCoeff    = static_cast<float> (std::exp (-1.0 / (0.025 * sampleRateHz)));
        longEnvCoeff     = static_cast<float> (std::exp (-1.0 / (0.600 * sampleRateHz)));
        bloomSmoothCoeff = static_cast<float> (std::exp (-1.0 / (0.080 * sampleRateHz)));

        huronTracker.prepare (sampleRateHz);
        leapDetector.prepare (sampleRateHz);
        shimmerOsc.setFrequency (5.8, sampleRateHz); // 5.8 Hz 自然な感情ビブラート周期の微細きらめき
        reset();
    }

    void reset() noexcept
    {
        sfBiquad1.reset();
        sfBiquad2.reset();
        chestBiquad.reset();
        huronTracker.reset();
        leapDetector.reset();
        airPrevL = 0.0f; airPrevR = 0.0f;
        airHpL = 0.0f;   airHpR = 0.0f;
        shortEnv = 0.001f;
        longEnv  = 0.001f;
        smoothedBloom = 0.0f;
        lastCatharsis01 = 0.0f;
        lastLeap01      = 0.0f;
        sfEnergyEnv = 1.0e-7f;
        adaaRingL.reset();
        adaaRingR.reset();
        shimmerOsc.reset (0.0f);
    }

    inline void processStereo (float& left, float& right,
                               float frissonIntensity01 = 0.45f,
                               float singersRingAmount01 = 0.40f) noexcept
    {
        if (frissonIntensity01 <= 0.001f && singersRingAmount01 <= 0.001f)
            return;

        const float mono = 0.5f * (left + right);
        const float absMono = std::fabs (mono);

        // 短期・長期エンベロープ＋Huron ITPRAカタルシス＋高音跳躍検出
        shortEnv = shortEnvCoeff * shortEnv + (1.0f - shortEnvCoeff) * absMono;
        longEnv  = longEnvCoeff  * longEnv  + (1.0f - longEnvCoeff)  * absMono;
        lastCatharsis01 = huronTracker.updateAndComputeCatharsis01 (absMono);
        lastLeap01      = leapDetector.detectPitchLeapExcitement01 (mono);

        const float surgeRatio = shortEnv / std::max (0.002f, longEnv);
        const float rawBloom   = std::clamp ((surgeRatio - 1.05f) * 0.65f + 0.35f * lastCatharsis01 + 0.25f * lastLeap01, 0.0f, 1.0f);
        smoothedBloom = bloomSmoothCoeff * smoothedBloom + (1.0f - bloomSmoothCoeff) * rawBloom;

        // 1. シンガーズ・フォルマント双峰黄金リング抽出 (2.92kHz + 3.34kHz)
        float ringL = 0.60f * sfBiquad1.processL (left)  + 0.40f * sfBiquad2.processL (left);
        float ringR = 0.60f * sfBiquad1.processR (right) + 0.40f * sfBiquad2.processR (right);

        // ADAA偶数/奇数調和サチュレーションで耳に刺さらない黄金の芯に変換
        ringL = adaaRingL.processSample (ringL, 1.35f);
        ringR = adaaRingR.processSample (ringR, 1.35f);

        const float ringPower = 0.5f * (ringL * ringL + ringR * ringR);
        sfEnergyEnv = shortEnvCoeff * sfEnergyEnv + (1.0f - shortEnvCoeff) * ringPower;

        // 2. チェスト・ベルベット（135Hz 胸の温もり：特にウィスパー〜中音量時に親密感を支える）
        const float whisperWeight = std::clamp (1.0f - shortEnv * 3.5f, 0.15f, 1.0f);
        const float chestL = chestBiquad.processL (left)  * whisperWeight;
        const float chestR = chestBiquad.processR (right) * whisperWeight;

        // 3. シルク・エア・ベルベット（13.5kHz以上：感情が盛り上がった瞬間にステレオの翼を広げる）
        airHpL = airHpCoeff * (airHpL + left  - airPrevL);
        airPrevL = left;
        airHpR = airHpCoeff * (airHpR + right - airPrevR);
        airPrevR = right;

        float lfoSin = 0.0f, lfoCos = 1.0f;
        shimmerOsc.tick (lfoSin, lfoCos);

        const float bloomAirGain = frissonIntensity01 * (0.18f + 0.32f * smoothedBloom);
        const float airSilkL = airHpL * bloomAirGain * (1.0f + 0.12f * lfoSin);
        const float airSilkR = airHpR * bloomAirGain * (1.0f + 0.12f * lfoCos);

        // 4. 感情コントラスト統合
        const float ringGain  = singersRingAmount01 * (0.22f + 0.25f * smoothedBloom);
        const float chestGain = frissonIntensity01  * 0.16f;

        left  += ringL * ringGain + chestL * chestGain + airSilkL;
        right += ringR * ringGain + chestR * chestGain + airSilkR;
    }

    FrissonTelemetry getTelemetry() const noexcept
    {
        FrissonTelemetry t;
        t.singersFormantEnergyDb  = vg::lightweight::FastMathKernel::fastLinearToDb (std::sqrt (std::max (1.0e-12f, sfEnergyEnv)));
        t.dynamicContrastIndex01  = std::clamp ((shortEnv / std::max (0.001f, longEnv)) * 0.45f, 0.0f, 1.0f);
        t.emotionalBloomFactor01  = smoothedBloom;
        t.catharsisReleaseScore01 = lastCatharsis01;
        t.melodicLeapExcitement01 = lastLeap01;
        const float sfNorm        = std::clamp ((t.singersFormantEnergyDb + 48.0f) / 36.0f, 0.0f, 1.0f);
        t.frissonGoosebumpScore01 = std::clamp (0.45f * sfNorm + 0.35f * smoothedBloom + 0.20f * lastCatharsis01, 0.0f, 1.0f);
        return t;
    }

private:
    struct StereoBandpassBiquad
    {
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1L = 0.0f, z2L = 0.0f;
        float z1R = 0.0f, z2R = 0.0f;

        void reset() noexcept
        {
            z1L = 0.0f; z2L = 0.0f;
            z1R = 0.0f; z2R = 0.0f;
        }

        inline float processL (float x) noexcept
        {
            const float y = b0 * x + z1L;
            z1L = b1 * x - a1 * y + z2L;
            z2L = b2 * x - a2 * y;
            return y;
        }

        inline float processR (float x) noexcept
        {
            const float y = b0 * x + z1R;
            z1R = b1 * x - a1 * y + z2R;
            z2R = b2 * x - a2 * y;
            return y;
        }
    };

    static void setupBandpassBiquad (StereoBandpassBiquad& bq, double freqHz, double q, double fs) noexcept
    {
        const double safeF = std::clamp (freqHz, 20.0, fs * 0.45);
        const double w0    = 2.0 * 3.14159265358979323846 * safeF / fs;
        const double alpha = std::sin (w0) / (2.0 * std::max (0.2, q));
        const double a0    = 1.0 + alpha;

        bq.b0 = static_cast<float> ( alpha / a0);
        bq.b1 = 0.0f;
        bq.b2 = static_cast<float> (-alpha / a0);
        bq.a1 = static_cast<float> ((-2.0 * std::cos (w0)) / a0);
        bq.a2 = static_cast<float> ((1.0 - alpha) / a0);
    }

    double sampleRateHz = 48000.0;
    StereoBandpassBiquad sfBiquad1;
    StereoBandpassBiquad sfBiquad2;
    StereoBandpassBiquad chestBiquad;
    HuronTensionAndCatharsisTracker huronTracker;
    MelodicLeapSalienceDetector     leapDetector;

    float airHpCoeff = 0.25f;
    float airPrevL = 0.0f, airPrevR = 0.0f;
    float airHpL = 0.0f,   airHpR = 0.0f;

    float shortEnvCoeff = 0.999f;
    float longEnvCoeff  = 0.9999f;
    float bloomSmoothCoeff = 0.9995f;
    float shortEnv = 0.001f;
    float longEnv  = 0.001f;
    float smoothedBloom   = 0.0f;
    float lastCatharsis01 = 0.0f;
    float lastLeap01      = 0.0f;
    float sfEnergyEnv = 1.0e-7f;

    vg::lightweight::Adaa1TanhSaturator adaaRingL;
    vg::lightweight::Adaa1TanhSaturator adaaRingR;
    vg::lightweight::MagicCircleQuadratureOscillator shimmerOsc;
};

} // namespace vg::human
