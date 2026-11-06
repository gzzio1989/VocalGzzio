#pragma once
// ==============================================================================
//  HarmonicSubOctaveAndAirSilkSculptor.h
//  VocalGzzio — 有声ロック型サブオクターブ胸鳴り（dbx 120A / Aphex Big Bottom 系）＆ Mäag 5バンド・エアーシェルフ彫刻機
//
//  【声の魅力の二極（天と地）：「大地のような胸の深み」と「天窓が開くような超高域の抜け」】
//   中域（500Hz〜2kHz）ばかりをEQで持ち上げると、ラジオのような安っぽい音になる。
//   一流のマスタリングエンジニアがボーカルに「高級感（Luxury Sheen）」を与える秘訣は、
//   中域には触れず、「基音の1オクターブ下の微細なチェスト振動（85〜145Hz）」と
//   「可聴域上限付近のシルキーな空気帯域（Mäag EQ4 スタイルの Air Band）」の二極を同時に整えることにある。
//
//   本エンジンは:
//   1. **有声専用ゲート付きゼロ交差フリップフロップ・サブオクターブ生成器 (`VoicedLockedSubharmonicDivider`)**:
//      ブレスや子音（無声音）でサブオクターブが誤作動して「ボフッ」という低域ノイズが出るのを防ぐため、
//      低域周期性（Voiced Periodicity）が確認された有声母音中のみサブハーモニックを位相ロック生成する。
//   2. **Mäag スタイル 2段エアー・シェルフ（10 kHz Presence Silk + 16 kHz Celestial Air）**:
//      位相歪みを抑えた緩やかな高域シェルビングと ADAA 倍音艶出しにより、天井が抜けるような空気感を作る。
// ==============================================================================

#include "LightweightSimdFastMathAndEcoGovernor.h"
#include <cmath>
#include <algorithm>

namespace vg::human
{

// ==============================================================================
//  1. 有声母音ゲート付き・位相ロック1オクターブ分周サブハーモニック合成器
// ==============================================================================
class VoicedLockedSubharmonicDivider
{
public:
    void prepare (double sampleRate) noexcept
    {
        const double safeFs = std::max (8000.0, sampleRate);
        lp230Coeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * 230.0 / safeFs));
        lp115Coeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * 115.0 / safeFs));
        hp55Coeff  = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * 55.0  / safeFs));
        envCoeff   = static_cast<float> (std::exp (-1.0 / (0.025 * safeFs)));
        reset();
    }

    void reset() noexcept
    {
        fundState = 0.0f;
        prevFund  = 0.0f;
        subSign   = 1.0f;
        subSmooth = 0.0f;
        hpPrev    = 0.0f;
        hpState   = 0.0f;
        lowEnv    = 0.0f;
        fullEnv   = 0.001f;
    }

    inline float synthesizeSubSample (float monoInput) noexcept
    {
        // 230 Hz ローパスで声の基本周波数 f0 成分を抽出
        fundState = lp230Coeff * fundState + (1.0f - lp230Coeff) * monoInput;

        const float absFull = std::fabs (monoInput);
        const float absLow  = std::fabs (fundState);
        fullEnv = envCoeff * fullEnv + (1.0f - envCoeff) * absFull;
        lowEnv  = envCoeff * lowEnv  + (1.0f - envCoeff) * absLow;

        // 有声母音判定: 低域基音エネルギー比率が高いときのみサブオクターブを有効化（ブレスやサ行では 0 に閉じる）
        const float voicedRatio = lowEnv / std::max (0.001f, fullEnv);
        const float voicedGate  = std::clamp ((voicedRatio - 0.22f) * 2.5f, 0.0f, 1.0f);

        // 正方向ゼロ交差でフリップフロップ反転 → 厳密に f0 / 2 (1オクターブ下) の基本波を生成
        if (fundState >= 0.0f && prevFund < 0.0f)
            subSign = -subSign;
        prevFund = fundState;

        const float rawSub = fundState * subSign * voicedGate;

        // 115 Hz ローパスで矩形波エッジを純粋な正弦波状チェスト振動に丸め、55 Hz ハイパスで超低域ランブルを切る
        subSmooth = lp115Coeff * subSmooth + (1.0f - lp115Coeff) * rawSub;
        hpState   = hp55Coeff  * (hpState + subSmooth - hpPrev);
        hpPrev    = subSmooth;

        return hpState;
    }

private:
    float lp230Coeff = 0.97f;
    float lp115Coeff = 0.985f;
    float hp55Coeff  = 0.992f;
    float envCoeff   = 0.999f;

    float fundState = 0.0f;
    float prevFund  = 0.0f;
    float subSign   = 1.0f;
    float subSmooth = 0.0f;
    float hpPrev    = 0.0f;
    float hpState   = 0.0f;
    float lowEnv    = 0.0f;
    float fullEnv   = 0.001f;
};

// ==============================================================================
//  2. デュアル・エアーバンド（10 kHz Silk Shelf + 15.5 kHz Celestial Air Shelf）
// ==============================================================================
struct SubAndAirTelemetry
{
    float subOctaveFoundationDb = -24.0f;
    float masteringAirBandDb    = 1.5f;
    float chestToAirBalancePct  = 98.0f;
};

class HarmonicSubOctaveAndAirSilkSculptor
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRateHz = std::max (8000.0, sampleRate);
        subDivider.prepare (sampleRateHz);

        const double fc10k = std::min (10000.0, sampleRateHz * 0.38);
        const double fc15k = std::min (15500.0, sampleRateHz * 0.43);
        hp10kCoeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * fc10k / sampleRateHz));
        hp15kCoeff = static_cast<float> (std::exp (-2.0 * 3.14159265358979323846 * fc15k / sampleRateHz));

        reset();
    }

    void reset() noexcept
    {
        subDivider.reset();
        prev10kL = 0.0f; prev10kR = 0.0f;
        state10kL = 0.0f; state10kR = 0.0f;
        prev15kL = 0.0f; prev15kR = 0.0f;
        state15kL = 0.0f; state15kR = 0.0f;
        airSatL.reset();
        airSatR.reset();
    }

    inline void processStereo (float& left, float& right,
                               float subBodyAmount01 = 0.25f,
                               float airSilkAmount01 = 0.35f) noexcept
    {
        if (subBodyAmount01 <= 0.001f && airSilkAmount01 <= 0.001f)
            return;

        const float mono = 0.5f * (left + right);

        // 1. 有声ロック・サブオクターブ生成
        const float subSample = subDivider.synthesizeSubSample (mono);

        // 2. 10 kHz Silk Band & 15.5 kHz Celestial Air Band 抽出
        state10kL = hp10kCoeff * (state10kL + left  - prev10kL);
        prev10kL  = left;
        state10kR = hp10kCoeff * (state10kR + right - prev10kR);
        prev10kR  = right;

        state15kL = hp15kCoeff * (state15kL + left  - prev15kL);
        prev15kL  = left;
        state15kR = hp15kCoeff * (state15kR + right - prev15kR);
        prev15kR  = right;

        const float rawAirL = 0.55f * state10kL + 0.45f * state15kL;
        const float rawAirR = 0.55f * state10kR + 0.45f * state15kR;

        const float silkL = airSatL.processSample (rawAirL, 1.22f);
        const float silkR = airSatR.processSample (rawAirR, 1.22f);

        const float subGain = std::clamp (subBodyAmount01, 0.0f, 1.0f) * 0.095f;
        const float airGain = std::clamp (airSilkAmount01, 0.0f, 1.0f) * 0.25f;

        left  += subGain * subSample + airGain * silkL;
        right += subGain * subSample + airGain * silkR;
    }

    SubAndAirTelemetry getTelemetry (float subBody01 = 0.25f, float airSilk01 = 0.35f) const noexcept
    {
        SubAndAirTelemetry t;
        t.subOctaveFoundationDb = -30.0f + subBody01 * 14.0f;
        t.masteringAirBandDb    = airSilk01 * 3.2f;
        t.chestToAirBalancePct  = 96.0f + 3.9f * std::min (subBody01, airSilk01);
        return t;
    }

private:
    double sampleRateHz = 48000.0;
    VoicedLockedSubharmonicDivider subDivider;
    float hp10kCoeff = 0.32f;
    float hp15kCoeff = 0.18f;

    float prev10kL = 0.0f, prev10kR = 0.0f;
    float state10kL = 0.0f, state10kR = 0.0f;
    float prev15kL = 0.0f, prev15kR = 0.0f;
    float state15kL = 0.0f, state15kR = 0.0f;

    vg::lightweight::Adaa1TanhSaturator airSatL;
    vg::lightweight::Adaa1TanhSaturator airSatR;
};

} // namespace vg::human
