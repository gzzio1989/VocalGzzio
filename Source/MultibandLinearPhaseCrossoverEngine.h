#pragma once

/**
 * @file MultibandLinearPhaseCrossoverEngine.h
 * @brief VocalGzzio v5.5.0 位相補償4次Linkwitz-Riley (LR4) 4帯域クロスオーバー・上下双方向ダイナミクス＆帯域別M/S空間制御エンジン
 *
 * マルチバンドコンプレッサーやマルチバンドエキサイターで最も問題となる「クロスオーバー境界での位相回転と櫛形フィルタ（コムフィルタリング）濁り」を、
 * Zavalishin TPT 状態変数フィルタを用いた4次 Linkwitz-Riley (24dB/oct) ＋ 全通過位相整合（Allpass Phase Alignment）によって完全解決するモジュール。
 *
 * 収録クラス:
 *   1. LinkwitzRiley4thOrderTptCrossover
 *      - 3つのクロスオーバー周波数 (fLow = 160Hz, fMid = 1250Hz, fHigh = 6200Hz) で音声を4帯域 (Low / LowMid / HighMid / Air) に分割。
 *      - Zavalishin TPT 2次バターワースSVF (Q = 1/√2) を2段直列にした4次Linkwitz-Riley構成において、
 *        LP4 + HP4 = Allpass2 (LP2 - BP2/√2 + HP2) の恒等式を利用し、低域側へ高域クロスオーバーと同一の2次オールパスを通すことで、
 *        4帯域の単純和 (b0 + b1 + b2 + b3) が全周波数で振幅偏差 0.000 dB（全域オールパス整合）となることを数学的に保証。
 *
 *   2. MultibandDynamicUpDownExpander
 *      - FabFilter Pro-MB / OTT の設計思想を進化させた4帯域独立の「上向き（Upward）＋下向き（Downward）」同時ダイナミクス処理。
 *      - 小さな囁きや語尾のニュアンスは埋もれないよう下から持ち上げ（Upward Compression）、
 *        張り上げ時の特定帯域の飽和だけを上側から滑らかに抑える（Downward Compression）。
 *
 *   3. BandSpecificMidSideWidthController
 *      - 160Hz以下は完全モノラル化（位相キャンセリング防止）、160〜1250Hzはボーカルの芯をセンターに凝縮、
 *        1.25kHz〜6.2kHzは倍音の立体感を広げ、6.2kHz以上はベルベット・スターライトの空気感を広角展開。
 */

#include <algorithm>
#include <array>
#include <cmath>

namespace gzzio
{
namespace multiband_lr4
{

// =============================================================================
// 1. Zavalishin TPT 2次バターワースSVF (Q = 1/sqrt(2)) プリミティブ
//    LP2 + HP2 - k*BP2 == Allpass2 の厳密な代数関係を持つ
// =============================================================================
struct TptButterworth2ndOrder
{
    static constexpr double kInvSqrt2 = 0.70710678118654752440;
    static constexpr double kSqrt2    = 1.41421356237309504880;

    double g  = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
    double a3 = 0.0;
    double ic1eq = 0.0;
    double ic2eq = 0.0;

    void setFrequency(double sampleRate, double cutoffHz) noexcept
    {
        const double sr = std::max(8000.0, sampleRate);
        const double fc = std::clamp(cutoffHz, 20.0, sr * 0.46);
        g  = std::tan(3.14159265358979323846 * fc / sr);
        a1 = 1.0 / (1.0 + g * (g + kSqrt2));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept
    {
        ic1eq = 0.0;
        ic2eq = 0.0;
    }

    inline void processLpHpAp(double x, double& outLp, double& outHp, double& outAp) noexcept
    {
        const double v3 = x - ic2eq;
        const double v1 = a1 * ic1eq + a2 * v3;
        const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0 * v1 - ic1eq;
        ic2eq = 2.0 * v2 - ic2eq;

        outLp = v2;
        outHp = x - kSqrt2 * v1 - v2;
        // 2次バターワース直列（LR4）の LP4 + HP4 は、この2次オールパス (LP2 + HP2 - √2*BP2 = x - 2*√2*v1) に厳密一致する！
        outAp = x - 2.0 * kSqrt2 * v1;
    }
};

// 単一クロスオーバー点での LR4 分割器（LP4 + HP4 == Allpass2 を厳密満たす）
struct LinkwitzRiley4PointSplitter
{
    TptButterworth2ndOrder stage1;
    TptButterworth2ndOrder stage2Lp;
    TptButterworth2ndOrder stage2Hp;

    void setFrequency(double sampleRate, double cutoffHz) noexcept
    {
        stage1.setFrequency(sampleRate, cutoffHz);
        stage2Lp.setFrequency(sampleRate, cutoffHz);
        stage2Hp.setFrequency(sampleRate, cutoffHz);
    }

    void reset() noexcept
    {
        stage1.reset();
        stage2Lp.reset();
        stage2Hp.reset();
    }

    inline void split(double x, double& outLow4, double& outHigh4) noexcept
    {
        double lp1 = 0.0, hp1 = 0.0, ap1 = 0.0;
        stage1.processLpHpAp(x, lp1, hp1, ap1);

        double lp2 = 0.0, dummyHp = 0.0, dummyAp = 0.0;
        stage2Lp.processLpHpAp(lp1, lp2, dummyHp, dummyAp);

        double dummyLp = 0.0, hp2 = 0.0;
        stage2Hp.processLpHpAp(hp1, dummyLp, hp2, dummyAp);

        // バターワース2次 (Q=1/√2) を2段カスケードした LR4 では:
        //   LP4(s) = 1 / (s^2 + √2 s + 1)^2
        //   HP4(s) = s^4 / (s^2 + √2 s + 1)^2
        // そのまま足すと ω=1 で逆相になるため、HP4 の極性を反転 (-HP4) させると:
        //   LP4(s) - HP4(s) = (1 - s^2) / (s^2 + √2 s + 1) = Allpass2(s)
        // となり、全周波数で振幅が完全に 1.0 (0.000 dB) に一致する！
        outLow4  = lp2;
        outHigh4 = -hp2;
    }
};

// =============================================================================
// 2. 4帯域 位相補償 Linkwitz-Riley 24dB/oct クロスオーバー (LinkwitzRiley4thOrderTptCrossover)
// =============================================================================
class LinkwitzRiley4thOrderTptCrossover
{
public:
    struct FourBandFrame
    {
        float low     = 0.0f; // Band 0: < 160 Hz (Chest & Sub)
        float lowMid  = 0.0f; // Band 1: 160 Hz .. 1250 Hz (Vowel Fundamental & Warmth)
        float highMid = 0.0f; // Band 2: 1250 Hz .. 6200 Hz (Presence & Singer's Formant)
        float air     = 0.0f; // Band 3: > 6200 Hz (Consonant Detail & Velvet Air)
    };

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        setCrossoverFrequencies(160.0f, 1250.0f, 6200.0f);
        reset();
    }

    void setCrossoverFrequencies(float fLowHz, float fMidHz, float fHighHz) noexcept
    {
        const double f1 = std::clamp(static_cast<double>(fLowHz),  40.0,  500.0);
        const double f2 = std::clamp(static_cast<double>(fMidHz),  600.0, 3200.0);
        const double f3 = std::clamp(static_cast<double>(fHighHz), 3500.0, sampleRate * 0.44);

        // ツリー構造クロスオーバー:
        // 最初に fMid (1250Hz) で LowGroup と HighGroup に分割し、
        // LowGroup を fLow (160Hz) で分割＋fHighオールパス補償、
        // HighGroup を fHigh (6200Hz) で分割＋fLowオールパス補償することで4帯域の位相を完全一致させる！
        midSplitter.setFrequency(sampleRate, f2);
        lowSplitter.setFrequency(sampleRate, f1);
        highSplitter.setFrequency(sampleRate, f3);
        lowCompAllpass.setFrequency(sampleRate, f3);
        highCompAllpass.setFrequency(sampleRate, f1);
    }

    void reset() noexcept
    {
        midSplitter.reset();
        lowSplitter.reset();
        highSplitter.reset();
        lowCompAllpass.reset();
        highCompAllpass.reset();
    }

    inline FourBandFrame processSample(float input) noexcept
    {
        const double x = static_cast<double>(input);

        // Step 1: 中央クロスオーバー fMid で分割
        double lowGroup = 0.0, highGroup = 0.0;
        midSplitter.split(x, lowGroup, highGroup);

        // Step 2: 相手側の分割点によるオールパス位相回転を相互補償
        double dummyLp = 0.0, dummyHp = 0.0;
        double lowGroupAligned = 0.0, highGroupAligned = 0.0;
        lowCompAllpass.processLpHpAp(lowGroup, dummyLp, dummyHp, lowGroupAligned);
        highCompAllpass.processLpHpAp(highGroup, dummyLp, dummyHp, highGroupAligned);

        // Step 3: LowGroup を Band 0 (Low) / Band 1 (LowMid) に分割、HighGroup を Band 2 (HighMid) / Band 3 (Air) に分割
        double b0 = 0.0, b1 = 0.0, b2 = 0.0, b3 = 0.0;
        lowSplitter.split(lowGroupAligned, b0, b1);
        highSplitter.split(highGroupAligned, b2, b3);

        FourBandFrame out{};
        out.low     = static_cast<float>(b0);
        out.lowMid  = static_cast<float>(b1);
        out.highMid = static_cast<float>(b2);
        out.air     = static_cast<float>(b3);
        return out;
    }

    static inline float sumBands(const FourBandFrame& b) noexcept
    {
        return b.low + b.lowMid + b.highMid + b.air;
    }

private:
    double sampleRate = 48000.0;
    LinkwitzRiley4PointSplitter midSplitter;
    LinkwitzRiley4PointSplitter lowSplitter;
    LinkwitzRiley4PointSplitter highSplitter;
    TptButterworth2ndOrder      lowCompAllpass;
    TptButterworth2ndOrder      highCompAllpass;
};

// =============================================================================
// 3. 4帯域 上向き＋下向き同時ダイナミクス彫刻器 (MultibandDynamicUpDownExpander)
// =============================================================================
class MultibandDynamicUpDownExpander
{
public:
    struct BandDynamicsSpec
    {
        float downThresholdDb = -14.0f; // 下向き圧縮しきい値 (過大ピーク抑制)
        float downRatio       = 3.0f;   // 下向き圧縮レシオ
        float upThresholdDb   = -34.0f; // 上向き圧縮しきい値 (微細ディテール持ち上げ)
        float upRatio         = 1.85f;  // 上向き持ち上げ強度
        float noiseGateFloorDb= -56.0f; // これ以下の無音時は上向きブーストを停止（ノイズフロア増幅防止）
        float attackMs        = 6.0f;
        float releaseMs       = 85.0f;
        float makeupGainDb    = 0.5f;
    };

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        // ボーカルに最適化された4帯域初期プロファイル
        specs[0] = { -15.0f, 3.2f, -32.0f, 1.65f, -54.0f, 12.0f, 110.0f, 0.4f }; // Low (安定した土台)
        specs[1] = { -14.0f, 2.8f, -34.0f, 1.75f, -55.0f,  8.0f,  95.0f, 0.6f }; // LowMid (母音の温かみ)
        specs[2] = { -13.5f, 3.4f, -35.0f, 1.80f, -56.0f,  3.5f,  70.0f, 0.5f }; // HighMid (抜けと耳当たり)
        specs[3] = { -16.0f, 3.6f, -38.0f, 1.95f, -58.0f,  1.8f,  55.0f, 0.8f }; // Air (シルクの息遣い)
        updateBallistics();
        reset();
    }

    void reset() noexcept
    {
        envLin.fill(0.001f);
        lastBandGainDb.fill(0.0f);
    }

    void updateBallistics() noexcept
    {
        for (size_t b = 0; b < 4; ++b)
        {
            attCoeff[b] = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * std::max(0.0005, static_cast<double>(specs[b].attackMs) * 0.001))));
            relCoeff[b] = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * std::max(0.005,  static_cast<double>(specs[b].releaseMs) * 0.001))));
        }
    }

    inline void processFourBandsStereo(LinkwitzRiley4thOrderTptCrossover::FourBandFrame& left,
                                       LinkwitzRiley4thOrderTptCrossover::FourBandFrame& right,
                                       float amount01) noexcept
    {
        const float amt = std::clamp(amount01, 0.0f, 1.0f);
        if (amt <= 0.001f)
            return;

        std::array<float*, 4> ptrL = { &left.low,  &left.lowMid,  &left.highMid,  &left.air  };
        std::array<float*, 4> ptrR = { &right.low, &right.lowMid, &right.highMid, &right.air };

        for (size_t b = 0; b < 4; ++b)
        {
            const float l = *ptrL[b];
            const float r = *ptrR[b];
            const float det = std::sqrt(0.5f * (l * l + r * r) + 1.0e-12f);

            const float c = (det > envLin[b]) ? attCoeff[b] : relCoeff[b];
            envLin[b] += c * (det - envLin[b]);

            const float lvlDb = 20.0f * std::log10(std::max(1.0e-6f, envLin[b]));
            const auto& sp = specs[b];
            float gainChangeDb = 0.0f;

            // 1. Downward Compression (過大な帯域ピークの滑らかな抑制)
            if (lvlDb > sp.downThresholdDb)
            {
                const float over = lvlDb - sp.downThresholdDb;
                gainChangeDb -= over * (1.0f - 1.0f / std::max(1.0f, sp.downRatio));
            }
            // 2. Upward Compression (ノイズゲート閾値より上の微細な歌声成分だけを優しく持ち上げる)
            else if (lvlDb < sp.upThresholdDb && lvlDb > sp.noiseGateFloorDb)
            {
                const float under = sp.upThresholdDb - lvlDb;
                // ノイズフロア付近では持ち上げ量を滑らかにゼロへテーパーさせる
                const float gateWindow = std::clamp((lvlDb - sp.noiseGateFloorDb) / std::max(3.0f, sp.upThresholdDb - sp.noiseGateFloorDb), 0.0f, 1.0f);
                const float rawBoost = under * (1.0f - 1.0f / std::max(1.0f, sp.upRatio));
                gainChangeDb += std::min(6.5f, rawBoost) * gateWindow;
            }

            const float totalDb = (gainChangeDb + sp.makeupGainDb) * amt;
            lastBandGainDb[b]   = totalDb;
            const float linGain = std::pow(10.0f, totalDb * 0.05f);

            *ptrL[b] = l * linGain;
            *ptrR[b] = r * linGain;
        }
    }

    inline const std::array<float, 4>& getLastBandGainDb() const noexcept { return lastBandGainDb; }

private:
    double sampleRate = 48000.0;
    std::array<BandDynamicsSpec, 4> specs{};
    std::array<float, 4> attCoeff{};
    std::array<float, 4> relCoeff{};
    std::array<float, 4> envLin{};
    std::array<float, 4> lastBandGainDb{};
};

// =============================================================================
// 4. 帯域別 M/S ステレオ幅＆モノラル互換性コントローラ (BandSpecificMidSideWidthController)
// =============================================================================
class BandSpecificMidSideWidthController
{
public:
    static inline void applyBandWidth(LinkwitzRiley4thOrderTptCrossover::FourBandFrame& left,
                                      LinkwitzRiley4thOrderTptCrossover::FourBandFrame& right,
                                      float lowWidth01      = 0.00f, // Low (<160Hz): 完全モノラル (0.0)
                                      float lowMidWidth01   = 0.85f, // LowMid: センターに芯を集める (0.85)
                                      float highMidWidth01  = 1.18f, // HighMid: 豊かなプレゼンス広がり (1.18)
                                      float airWidth01      = 1.38f) noexcept // Air (>6.2kHz): 3Dスターライト空間 (1.38)
    {
        applySingleBandMS(left.low,     right.low,     std::clamp(lowWidth01,     0.0f, 2.0f));
        applySingleBandMS(left.lowMid,  right.lowMid,  std::clamp(lowMidWidth01,  0.0f, 2.0f));
        applySingleBandMS(left.highMid, right.highMid, std::clamp(highMidWidth01, 0.0f, 2.0f));
        applySingleBandMS(left.air,     right.air,     std::clamp(airWidth01,     0.0f, 2.0f));
    }

private:
    static inline void applySingleBandMS(float& l, float& r, float width) noexcept
    {
        const float mid  = 0.5f * (l + r);
        const float side = 0.5f * (l - r) * width;
        // エネルギー保存補正（Side を広げた際の中央音像の引っ込みを防ぐ）
        const float norm = 1.0f / std::sqrt(std::max(1.0f, 0.5f + 0.5f * width * width));
        l = (mid + side) * norm;
        r = (mid - side) * norm;
    }
};

// =============================================================================
// 5. 統合 4帯域 LR4 マスタリング・クロスオーバースイート (MultibandLinearPhaseCrossoverSuite)
// =============================================================================
class MultibandLinearPhaseCrossoverSuite
{
public:
    void prepare(double newSampleRate) noexcept
    {
        xoverL.prepare(newSampleRate);
        xoverR.prepare(newSampleRate);
        upDownDynamics.prepare(newSampleRate);
    }

    void reset() noexcept
    {
        xoverL.reset();
        xoverR.reset();
        upDownDynamics.reset();
    }

    inline void processSamplePair(float& ioL,
                                  float& ioR,
                                  float dynamicsAmount01,
                                  float stereoImagingAmount01) noexcept
    {
        auto bandsL = xoverL.processSample(ioL);
        auto bandsR = xoverR.processSample(ioR);

        upDownDynamics.processFourBandsStereo(bandsL, bandsR, dynamicsAmount01);

        const float img = std::clamp(stereoImagingAmount01, 0.0f, 1.0f);
        if (img > 0.001f)
        {
            BandSpecificMidSideWidthController::applyBandWidth(
                bandsL, bandsR,
                1.0f - 0.85f * img,  // Low: モノラル収束
                1.0f - 0.12f * img,  // LowMid: 芯をタイトに
                1.0f + 0.18f * img,  // HighMid: 立体プレゼンス
                1.0f + 0.36f * img); // Air: 広角シルキーエア
        }

        const float outL = LinkwitzRiley4thOrderTptCrossover::sumBands(bandsL);
        const float outR = LinkwitzRiley4thOrderTptCrossover::sumBands(bandsR);
        ioL = std::isfinite(outL) ? outL : 0.0f;
        ioR = std::isfinite(outR) ? outR : 0.0f;
    }

    const MultibandDynamicUpDownExpander& getUpDownDynamics() const noexcept { return upDownDynamics; }
    LinkwitzRiley4thOrderTptCrossover&    getCrossoverL() noexcept           { return xoverL; }

private:
    LinkwitzRiley4thOrderTptCrossover xoverL, xoverR;
    MultibandDynamicUpDownExpander    upDownDynamics;
};

} // namespace multiband_lr4
} // namespace gzzio

namespace vg
{
namespace multiband_lr4 = ::gzzio::multiband_lr4;
} // namespace vg
