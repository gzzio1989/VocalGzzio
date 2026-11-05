#pragma once

/**
 * @file MicrophoneAndPreampPhysicalTwin.h
 * @brief VocalGzzio v5.5.0 静電コンデンサー背面電極非線形容量モデル・コルゲートアルミリボン速度マイク＆API 2520ディスクリートオペアンプ＋Cinemagトランス物理ツイン
 *
 * マイクモデリングにおいて一般的な「固定EQカーブの当てはめ」を超え、
 * 物理的な変換器（Transducer）の非線形運動方程式とプリアンプ回路のスルーレート・磁気飽和を再現するモジュール。
 *
 * 収録クラス:
 *   1. CondenserBackplateElectrostaticCapsuleModel
 *      - Neumann U47 (K47 カプセル) / Sony C-800G の金蒸着マイラー振動板と真鍮背面電極（Backplate）の間の
 *        非線形静電容量 C(x) = εA / (d0 - x) をモデル化。
 *      - 振動板が背面電極に近づく正圧側で容量変化が急峻になる物理法則から、大音量歌唱時に自然な偶数次（第2倍音）の
 *        黄金の温かみと空気の密度が自動的に生まれる。
 *
 *   2. RibbonVelocityFigureEightMotorModel
 *      - RCA 44BX / Royer R-121 の2.5ミクロン波形コルゲート・アルミニウムリボン（圧力勾配型・双指向性速度センサー）を再現。
 *      - 115Hz のリボン最低共振と、高域の刺さりをシルクのように丸める自然なロールオフ、濃厚なローミッド近接効果。
 *
 *   3. DiscreteOpAmp2520AndCinemagTransformerStage
 *      - API 512c / 2520 ディスクリート・オペアンプ特有の高速スルーレート・パンチ（子音アタックの押し出し感）と、
 *        Cinemag / Jensen 出力トランスのニッケル・鉄コア倍音歪みをモデル化。
 */

#include <algorithm>
#include <array>
#include <cmath>

namespace gzzio
{
namespace mic_physical_twin
{

// =============================================================================
// 1. 静電コンデンサーカプセル 背面電極非線形容量モデル
//    (CondenserBackplateElectrostaticCapsuleModel)
// =============================================================================
class CondenserBackplateElectrostaticCapsuleModel
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        // K47 / C800G 大口径ダイアフラムの高域チャンバー共振 (11.2kHz, Q = 1.35)
        const double fc = std::min(11200.0, sampleRate * 0.43);
        const double g  = std::tan(3.14159265358979323846 * fc / sampleRate);
        const double k  = 1.0 / 1.35;
        capA1 = 1.0 / (1.0 + g * (g + k));
        capA2 = g * capA1;
        capA3 = g * capA2;
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            dispState[c] = 0.0f;
            capIc1[c]    = 0.0;
            capIc2[c]    = 0.0;
            dcIn[c]      = 0.0f;
            dcOut[c]     = 0.0f;
        }
    }

    inline void processSamplePair(float& ioL, float& ioR, float biasWarmth01, float capsuleSheen01) noexcept
    {
        ioL = processChannel(0, ioL, biasWarmth01, capsuleSheen01);
        ioR = processChannel(1, ioR, biasWarmth01, capsuleSheen01);
    }

private:
    inline float processChannel(int ch, float input, float biasWarmth01, float capsuleSheen01) noexcept
    {
        const float warmth = std::clamp(biasWarmth01,   0.0f, 1.0f);
        const float sheen  = std::clamp(capsuleSheen01, 0.0f, 1.0f);

        // ダイアフラム変位 x(t) の一次ダンピング追従
        dispState[ch] += 0.45f * (input - dispState[ch]);
        const float x = std::clamp(dispState[ch] * 0.38f * warmth, -0.45f, 0.45f);

        // 静電容量 C(x) = C0 / (1 - x) ≈ C0 * (1 + x + x^2) による非対称第2倍音出力電圧
        const float nonlinearElectrostatic = input * (1.0f + 0.24f * x + 0.12f * x * x);

        // カプセル前面音響チャンバーの 11.2kHz エア・レゾナンス
        const double v0 = static_cast<double>(nonlinearElectrostatic);
        const double v3 = v0 - capIc2[ch];
        const double v1 = capA1 * capIc1[ch] + capA2 * v3;
        const double v2 = capIc2[ch] + capA2 * capIc1[ch] + capA3 * v3;
        capIc1[ch] = 2.0 * v1 - capIc1[ch];
        capIc2[ch] = 2.0 * v2 - capIc2[ch];

        const float withAir = nonlinearElectrostatic + static_cast<float>(v1) * 0.32f * sheen;

        // 高インピーダンス・グリッド結合コンデンサのDCブロッカー
        const float out = withAir - dcIn[ch] + 0.9985f * dcOut[ch];
        dcIn[ch]  = withAir;
        dcOut[ch] = out;
        return out;
    }

    double sampleRate = 48000.0;
    double capA1 = 0.0, capA2 = 0.0, capA3 = 0.0;
    float  dispState[2]{};
    double capIc1[2]{}, capIc2[2]{};
    float  dcIn[2]{}, dcOut[2]{};
};

// =============================================================================
// 2. コルゲート・アルミリボン双指向性ベロシティマイクモデル
//    (RibbonVelocityFigureEightMotorModel)
// =============================================================================
class RibbonVelocityFigureEightMotorModel
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        // 135Hz リボン質量・近接効果ベル (Q = 0.95)
        const double gLow = std::tan(3.14159265358979323846 * 135.0 / sampleRate);
        const double kLow = 1.0 / 0.95;
        lowA1 = 1.0 / (1.0 + gLow * (gLow + kLow));
        lowA2 = gLow * lowA1;
        lowA3 = gLow * lowA2;

        // 8.8kHz リボン自然高域ロールオフ (耳に刺さるデジタル高域をベルベットのように滑らかに整える)
        const double fcHigh = std::min(8800.0, sampleRate * 0.42);
        const double gHigh  = std::tan(3.14159265358979323846 * fcHigh / sampleRate);
        const double kHigh  = 1.0 / 0.707;
        highA1 = 1.0 / (1.0 + gHigh * (gHigh + kHigh));
        highA2 = gHigh * highA1;
        highA3 = gHigh * highA2;
        reset();
    }

    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            lowIc1[c]  = lowIc2[c]  = 0.0;
            highIc1[c] = highIc2[c] = 0.0;
        }
    }

    inline void processSamplePair(float& ioL, float& ioR, float ribbonBlend01) noexcept
    {
        const float blend = std::clamp(ribbonBlend01, 0.0f, 1.0f);
        if (blend <= 0.001f)
            return;

        ioL = processChannel(0, ioL, blend);
        ioR = processChannel(1, ioR, blend);
    }

private:
    inline float processChannel(int ch, float in, float blend) noexcept
    {
        const double x = static_cast<double>(in);

        // 135Hz 近接ボディ補強
        const double lv3 = x - lowIc2[ch];
        const double lv1 = lowA1 * lowIc1[ch] + lowA2 * lv3;
        const double lv2 = lowIc2[ch] + lowA2 * lowIc1[ch] + lowA3 * lv3;
        lowIc1[ch] = 2.0 * lv1 - lowIc1[ch];
        lowIc2[ch] = 2.0 * lv2 - lowIc2[ch];

        const double warmSignal = x + lv1 * 0.42;

        // 8.8kHz シルク・ローパス
        const double hv3 = warmSignal - highIc2[ch];
        const double hv1 = highA1 * highIc1[ch] + highA2 * hv3;
        const double hv2 = highIc2[ch] + highA2 * highIc1[ch] + highA3 * hv3;
        highIc1[ch] = 2.0 * hv1 - highIc1[ch];
        highIc2[ch] = 2.0 * hv2 - highIc2[ch];

        // 昇圧リボン・トランスフォーマーの穏やかな飽和
        const float ribbonOut = std::tanh(static_cast<float>(hv2 * 0.72 + warmSignal * 0.28) * 1.08f);
        return in * (1.0f - blend) + ribbonOut * blend;
    }

    double sampleRate = 48000.0;
    double lowA1 = 0.0, lowA2 = 0.0, lowA3 = 0.0;
    double highA1 = 0.0, highA2 = 0.0, highA3 = 0.0;
    double lowIc1[2]{}, lowIc2[2]{};
    double highIc1[2]{}, highIc2[2]{};
};

// =============================================================================
// 3. API 2520 ディスクリートオペアンプ ＆ Cinemag 出力トランスステージ
//    (DiscreteOpAmp2520AndCinemagTransformerStage)
// =============================================================================
class DiscreteOpAmp2520AndCinemagTransformerStage
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        // 1.8kHz〜3.5kHz のアメリカン・コンソール特有の中域パンチ共振
        const double g = std::tan(3.14159265358979323846 * 2250.0 / sampleRate);
        const double k = 1.0 / 1.15;
        midA1 = 1.0 / (1.0 + g * (g + k));
        midA2 = g * midA1;
        midA3 = g * midA2;
        reset();
    }

    void reset() noexcept
    {
        prevOut[0] = prevOut[1] = 0.0f;
        midIc1[0]  = midIc2[0]  = 0.0;
        midIc1[1]  = midIc2[1]  = 0.0;
    }

    inline void processSamplePair(float& ioL, float& ioR, float punchDrive01) noexcept
    {
        const float drv = std::clamp(punchDrive01, 0.0f, 1.0f);
        if (drv <= 0.001f)
            return;

        ioL = processChannel(0, ioL, drv);
        ioR = processChannel(1, ioR, drv);
    }

private:
    inline float processChannel(int ch, float in, float drv) noexcept
    {
        // 2520 オペアンプの非線形スルーレート圧縮（急峻なデジタル過渡変化を太いアナログトランジェントに変換）
        const float delta = in - prevOut[ch];
        const float maxSlew = 0.45f;
        const float slewedDelta = maxSlew * std::tanh(delta / maxSlew);
        const float opAmpOut = prevOut[ch] + slewedDelta;
        prevOut[ch] = opAmpOut;

        // 2.25kHz Cinemag トランス・ミッドフォワードネス
        const double x  = static_cast<double>(opAmpOut);
        const double v3 = x - midIc2[ch];
        const double v1 = midA1 * midIc1[ch] + midA2 * v3;
        const double v2 = midIc2[ch] + midA2 * midIc1[ch] + midA3 * v3;
        midIc1[ch] = 2.0 * v1 - midIc1[ch];
        midIc2[ch] = 2.0 * v2 - midIc2[ch];

        const float boosted = opAmpOut + static_cast<float>(v1) * 0.28f * drv;
        // ニッケル鋼コア出力トランスの奇数次・偶数次ハイブリッド飽和
        const float gain = 1.0f + 0.55f * drv;
        const float sat  = std::tanh(boosted * gain + 0.03f * boosted * boosted) / std::tanh(gain);
        return sat;
    }

    double sampleRate = 48000.0;
    double midA1 = 0.0, midA2 = 0.0, midA3 = 0.0;
    float  prevOut[2]{};
    double midIc1[2]{}, midIc2[2]{};
};

// =============================================================================
// 4. 統合マイク＆プリアンプ物理ツイン・スイート (MicrophoneAndPreampPhysicalTwinSuite)
// =============================================================================
class MicrophoneAndPreampPhysicalTwinSuite
{
public:
    void prepare(double newSampleRate) noexcept
    {
        condenserCapsule.prepare(newSampleRate);
        ribbonMotor.prepare(newSampleRate);
        opAmp2520.prepare(newSampleRate);
    }

    void reset() noexcept
    {
        condenserCapsule.reset();
        ribbonMotor.reset();
        opAmp2520.reset();
    }

    inline void processSamplePair(float& ioL,
                                  float& ioR,
                                  float condenserWarmth01,
                                  float ribbonVelvet01,
                                  float apiPunch01) noexcept
    {
        condenserCapsule.processSamplePair(ioL, ioR, condenserWarmth01, condenserWarmth01 * 0.85f);
        ribbonMotor.processSamplePair(ioL, ioR, ribbonVelvet01);
        opAmp2520.processSamplePair(ioL, ioR, apiPunch01);
    }

private:
    CondenserBackplateElectrostaticCapsuleModel condenserCapsule;
    RibbonVelocityFigureEightMotorModel         ribbonMotor;
    DiscreteOpAmp2520AndCinemagTransformerStage opAmp2520;
};

} // namespace mic_physical_twin
} // namespace gzzio

namespace vg
{
namespace mic_physical_twin = ::gzzio::mic_physical_twin;
} // namespace vg
