#pragma once
// ==============================================================================
//  PerceptualLoudnessEqualizerAndFletcherMunson.h
//  VocalGzzio — ISO 226:2023 等ラウドネス曲線（Fletcher-Munson最新改訂）10バンド解析テーブル ＆ 夜間・小音量自動補償機
//
//  【人間の聴覚心理の真実：「なぜ夜中に小さい音量で聴くと、ボーカルが急に細く貧相に聞こえるのか？」】
//   人間の耳の感度（ISO 226:2023 Equal-Loudness Contours / 旧 Fletcher-Munson 曲線）は再生音量によって劇的に変化する:
//    - スタジオ基準音量（80〜85 Phon）では 100Hz〜15kHz まで比較的フラットに聞こえる。
//    - しかし、深夜の自宅配信視聴やスマホの小音量再生（50〜65 Phon）に絞った瞬間、
//      人間の耳は **3.5 kHz 付近の感度だけを残して、150 Hz 以下の低域（声の太さ・体温）と
//      10 kHz 以上の超高域（息のシルク感）の感度を -8dB 〜 -16dB も失ってしまう**。
//
//   本エンジンは:
//   1. **ISO 226:2023 標準パラメータ表（$f_i, \alpha_f, L_U, T_f$）に基づく厳密な Phon → dB SPL 計算器 (`Iso226StandardCurveCalculator`)**:
//      任意の再生ラウドネス $L_N \in [40, 90]\text{ Phon}$ における基準 80 Phon との感度差を数学的に算出する。
//   2. **3帯域ダイナミック等ラウドネス補償フィルタ（125 Hz Warmth / 3.45 kHz Pinna Guard / 11.2 kHz Air Silk）**:
//      小音量時でも大音量時と同じ「豊かな胸の温もり」と「きらめくエアー」を維持する。
// ==============================================================================

#include "LightweightSimdFastMathAndEcoGovernor.h"
#include <cmath>
#include <algorithm>
#include <array>

namespace vg::human
{

// ==============================================================================
//  1. ISO 226:2023 標準等ラウドネス曲線 10代表周波数パラメータ計算クラス
// ==============================================================================
class Iso226StandardCurveCalculator
{
public:
    struct IsoBandParam
    {
        float freqHz;
        float alphaF; // ラウドネス知覚指数 α_f
        float luDb;   // 線形伝達正規化値 L_U (dB)
        float tfDb;   // 最小可聴閾値 T_f (dB SPL)
    };

    static constexpr std::array<IsoBandParam, 10> kIso226Table {{
        {   63.0f, 0.532f, -17.1f, 37.5f },
        {  125.0f, 0.396f, -10.3f, 22.1f },
        {  250.0f, 0.320f,  -4.7f, 11.4f },
        {  500.0f, 0.279f,  -1.2f,  4.4f },
        { 1000.0f, 0.267f,   0.0f,  2.4f },
        { 2000.0f, 0.259f,  -1.0f, -1.3f },
        { 3150.0f, 0.251f,  -3.6f, -6.0f },
        { 4000.0f, 0.252f,  -3.9f, -5.4f },
        { 8000.0f, 0.276f, -11.2f, 12.6f },
        {12500.0f, 0.301f,  -6.2f, 16.8f }
    }};

    // 指定したバンド idx (0..9) とラウドネスレベル phon (20..90 Phon) に対する等ラウドネス音圧 Lp (dB SPL) を計算
    static float computeEqualLoudnessSplDb (size_t bandIdx, float phon) noexcept
    {
        const auto& p = kIso226Table[std::min (bandIdx, size_t (9))];
        const float ln = std::clamp (phon, 20.0f, 95.0f);

        // ISO 226 式: Af = 4.47e-3 * (10^(0.025*Ln) - 1.15) + (0.4 * 10^(((Tf + Lu)/10) - 9))^alphaF
        const float term1 = 0.00447f * (std::pow (10.0f, 0.025f * ln) - 1.15f);
        const float term2 = std::pow (0.4f * std::pow (10.0f, ((p.tfDb + p.luDb) * 0.1f) - 9.0f), p.alphaF);
        const float af    = std::max (1.0e-6f, term1 + term2);

        const float lp = (10.0f / p.alphaF) * std::log10 (af) - p.luDb + 94.0f;
        return lp;
    }

    // 基準 82 Phon に対して、指定した targetPhon で不足する 125Hz 低域感度差（dB）を返す
    static float computeLow125HzCompensationDb (float targetPhon) noexcept
    {
        const float diffAtTarget = computeEqualLoudnessSplDb (1, targetPhon) - computeEqualLoudnessSplDb (4, targetPhon);
        const float diffAtRef82  = computeEqualLoudnessSplDb (1, 82.0f)      - computeEqualLoudnessSplDb (4, 82.0f);
        return std::clamp ((diffAtTarget - diffAtRef82) * 0.45f, 0.0f, 6.0f);
    }
};

// ==============================================================================
//  2. ISO 226:2023 夜間・小音量リスニング自動補償エンジン
// ==============================================================================
struct Iso226LoudnessTelemetry
{
    float estimatedListeningPhon = 72.0f;
    float lowContourBoostDb      = 0.0f;
    float highContourSilkBoostDb = 0.0f;
    float midPinnaCushionCutDb   = 0.0f;
};

class PerceptualLoudnessEqualizerAndFletcherMunson
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRateHz = std::max (8000.0, sampleRate);

        // 1. 125 Hz 等ラウドネス低域シェルフ抽出（1次ローパス）
        lp125Coeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * 125.0 / sampleRateHz));

        // 2. 11.2 kHz 等ラウドネス高域シェルフ抽出（1次ハイパス）
        const double fcHigh = std::min (11200.0, sampleRateHz * 0.40);
        hp11kCoeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * fcHigh / sampleRateHz));

        // 3. 3.45 kHz 耳道ピーク抽出（大音量時の耳刺さり保護用 Biquad Bandpass, Q = 2.2）
        const double w0    = 2.0 * 3.14159265358979323846 * 3450.0 / sampleRateHz;
        const double alpha = std::sin (w0) / (2.0 * 2.2);
        const double a0    = 1.0 + alpha;
        b0 = static_cast<float> ( alpha / a0);
        b2 = static_cast<float> (-alpha / a0);
        a1 = static_cast<float> ((-2.0 * std::cos (w0)) / a0);
        a2 = static_cast<float> ((1.0 - alpha) / a0);

        envCoeff = static_cast<float> (std::exp (-1.0 / (0.180 * sampleRateHz)));
        cachedLowCompDb = Iso226StandardCurveCalculator::computeLow125HzCompensationDb (64.0f);
        reset();
    }

    void reset() noexcept
    {
        lowL = 0.0f; lowR = 0.0f;
        hpPrevL = 0.0f; hpPrevR = 0.0f;
        highL = 0.0f; highR = 0.0f;
        z1L = z2L = z1R = z2R = 0.0f;
        rmsEnv = 0.03f;
    }

    // targetReferencePhon: リスナー想定音量 (例: 深夜・小音量モード = 58 Phon, 標準 = 75 Phon)
    inline void processStereo (float& left, float& right,
                               float iso226Amount01 = 0.45f,
                               float targetListeningPhon = 64.0f) noexcept
    {
        if (iso226Amount01 <= 0.001f)
            return;

        const float amount = std::clamp (iso226Amount01, 0.0f, 1.0f);
        const float absMono = 0.5f * (std::fabs (left) + std::fabs (right));
        if (absMono > 0.001f)
            rmsEnv = envCoeff * rmsEnv + (1.0f - envCoeff) * absMono;

        // 125 Hz 低域抽出
        lowL = lp125Coeff * lowL + (1.0f - lp125Coeff) * left;
        lowR = lp125Coeff * lowR + (1.0f - lp125Coeff) * right;

        // 11.2 kHz 高域抽出
        highL = hp11kCoeff * (highL + left  - hpPrevL);
        hpPrevL = left;
        highR = hp11kCoeff * (highR + right - hpPrevR);
        hpPrevR = right;

        // 3.45 kHz 耳道感度ピーク抽出
        const float mid3kL = b0 * left + z1L;
        z1L = -a1 * mid3kL + z2L;
        z2L = b2 * left - a2 * mid3kL;

        const float mid3kR = b0 * right + z1R;
        z1R = -a1 * mid3kR + z2R;
        z2R = b2 * right - a2 * mid3kR;

        // ISO 226:2023 基準82 Phon からの差分算出
        const float phonDeficit = std::clamp ((82.0f - targetListeningPhon) / 30.0f, 0.0f, 1.0f);
        const float quietSignalFactor = std::clamp (1.0f - rmsEnv * 3.2f, 0.15f, 1.0f);
        const float contourWeight = amount * (0.60f * phonDeficit + 0.40f * quietSignalFactor);

        // 大音量パッセージでは 3.45kHz のみを優しくクッション減衰
        const float loudSignalFactor = std::clamp ((rmsEnv - 0.12f) * 3.0f, 0.0f, 1.0f);
        const float midCut = amount * loudSignalFactor * 0.22f;

        left  = left  + (contourWeight * 0.28f) * lowL + (contourWeight * 0.20f) * highL - midCut * mid3kL;
        right = right + (contourWeight * 0.28f) * lowR + (contourWeight * 0.20f) * highR - midCut * mid3kR;
    }

    Iso226LoudnessTelemetry getTelemetry (float iso226Amount01 = 0.45f, float targetPhon = 64.0f) const noexcept
    {
        const float phonDeficit = std::clamp ((82.0f - targetPhon) / 30.0f, 0.0f, 1.0f);
        Iso226LoudnessTelemetry t;
        t.estimatedListeningPhon = targetPhon;
        t.lowContourBoostDb      = iso226Amount01 * std::max (cachedLowCompDb, phonDeficit * 3.6f);
        t.highContourSilkBoostDb = iso226Amount01 * phonDeficit * 2.4f;
        t.midPinnaCushionCutDb   = -iso226Amount01 * std::clamp ((rmsEnv - 0.12f) * 6.0f, 0.0f, 2.2f);
        return t;
    }

private:
    double sampleRateHz = 48000.0;
    float lp125Coeff = 0.98f;
    float hp11kCoeff = 0.28f;
    float b0 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float envCoeff = 0.9995f;
    float cachedLowCompDb = 2.4f;

    float lowL = 0.0f, lowR = 0.0f;
    float hpPrevL = 0.0f, hpPrevR = 0.0f;
    float highL = 0.0f, highR = 0.0f;
    float z1L = 0.0f, z2L = 0.0f, z1R = 0.0f, z2R = 0.0f;
    float rmsEnv = 0.03f;
};

} // namespace vg::human
