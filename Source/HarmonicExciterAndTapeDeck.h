#pragma once

/**
 * @file HarmonicExciterAndTapeDeck.h
 * @brief VocalGzzio Studer A800 / Ampex ATR-102 30 IPS マスタリングテープヒステリシス・ヘッドバンプ・Aphexオーラルエキサイター＆サブハーモニック合成エンジン
 *
 * 【研究開発の背景と独自技術】
 * 1. MagneticHysteresisTapeCell (Jiles-Atherton 磁区ピン止めヒステリシス＆高域自己消磁コンプレッサー):
 *    - 磁性体酸化鉄粒子の非線形磁化曲線 Man(H) = tanh(H / a) と、磁壁移動の不可逆ヒステリシス (Pinning Loss) を
 *      ゼロ遅延微分方程式でリアルタイム近似。
 *    - テープ録音特有の「強いトランジェントや刺さる高域だけが自然に丸まり、中低域の密度が濃くなる」
 *      高域自己消磁 (Self-Erasure / High-Frequency Compression) と IEC/NAB 再生イコライゼーションを統合しています。
 *
 * 2. Studer30IpsHeadBumpFilter (30 IPS / 15 IPS 再生ヘッド・コンター効果 & サブロー引き締めフィルタ):
 *    - Studer A800 (30 IPS: 68Hz +1.65dB ヘッドバンプ / 31Hz ロールオフ) と
 *      Ampex ATR-102 (15 IPS: 48Hz +2.20dB ウォームバンプ) の磁気ヘッド波長干渉特性を Zavalishin TPT で再現。
 *
 * 3. AphexPhaseHarmonicExciter (1975 Aphex Aural Exciter 方式・位相回転＋偶奇倍音ジェネレータ):
 *    - 通常のEQブーストでは得られない「ピーク音量を上げずに声の空気感と明瞭度だけを浮き彫りにする」ため、
 *      2.6kHz 以上の高域側鎖に TPT オールパス位相回転と ADAA 2次アンチエイリアシング非対称倍音生成を施してブレンドします。
 *
 * 4. SubHarmonicChestSynthesizer (dbx 120A 方式・ゼロ交差フリップフロップ 1オクターブ下チェストシンセサイザー):
 *    - 110Hz〜260Hz のボーカル基音帯域からゼロ交差で正確に半周期分周波 (Sub-Octave: 55Hz〜130Hz) を生成し、
 *      入力エンベロープに完全追従させて細くなりがちなハイトーンやスマホマイク録音に圧倒的な胸鳴りを付与します。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "MasteringProEngine.h"

namespace vg::tape_and_harmonics
{

//==============================================================================
// 1. Jiles-Atherton 簡易リアルタイム磁気ヒステリシス＆テープサチュレーション・セル
//==============================================================================
class MagneticHysteresisTapeCell
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        // プリエンファシス (録音時高域ブースト -> 磁気飽和 -> デエンファシスで高域の刺さりを自然に圧縮)
        preEmphasis.prepare(sr);
        deEmphasis.prepare(sr);
        dcBlocker.prepare(sr);

        preEmphasis.setParameters(3800.0f, 0.707f, +4.2f, vg::mastering::TptStateVariableFilter::Mode::HighShelf);
        deEmphasis.setParameters(3800.0f, 0.707f, -3.9f, vg::mastering::TptStateVariableFilter::Mode::HighShelf);
        dcBlocker.setParameters(12.0f, 0.707f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::Highpass);
        reset();
    }

    void reset() noexcept
    {
        lastH = 0.0f;
        lastM = 0.0f;
        adaaStage.reset();
        preEmphasis.reset();
        deEmphasis.reset();
        dcBlocker.reset();
    }

    float processSample(float input, float tapeDrive01, float hysteresisCoercivity01 = 0.35f) noexcept
    {
        tapeDrive01 = std::clamp(tapeDrive01, 0.0f, 1.0f);
        if (tapeDrive01 <= 0.0005f)
            return input;

        const float pre = preEmphasis.processSample(input);
        const float driveGain = 1.0f + tapeDrive01 * 2.1f;
        const float H = pre * driveGain;
        const float dH = H - lastH;

        // 非履歴磁化曲線 Man(H) とヒステリシス・ピン止め項の合成
        const float kPin = 0.12f * std::clamp(hysteresisCoercivity01, 0.0f, 1.0f);
        const float hEffective = H - kPin * (H - lastM) * (dH >= 0.0f ? 1.0f : -1.0f);

        // ADAA 2次アンチエイリアシング飽和ステージでエイリアシングゼロ磁化 M を算出
        const float M = adaaStage.processSample(hEffective, 1.15f, 0.08f, 1.0f);

        lastH = H;
        lastM = M;

        const float de = deEmphasis.processSample(M);
        const float cleanOut = dcBlocker.processSample(de);
        return vg::mastering::FastAudioMath::sanitize(input + tapeDrive01 * (cleanOut - input));
    }

private:
    double sr = 48000.0;
    float lastH = 0.0f;
    float lastM = 0.0f;
    vg::mastering::AdaaTriodeTransformerStage adaaStage;
    vg::mastering::TptStateVariableFilter preEmphasis;
    vg::mastering::TptStateVariableFilter deEmphasis;
    vg::mastering::TptStateVariableFilter dcBlocker;
};

//==============================================================================
// 2. Studer A800 (30 IPS) / Ampex ATR-102 (15 IPS) ヘッドバンプ＆サブタイトナー
//==============================================================================
class Studer30IpsHeadBumpFilter
{
public:
    enum class TapeSpeedMode : int
    {
        Speed30IpsModern = 0, // 68 Hz +1.65 dB Tight Punch (Pop / Rock / Modern Vocal)
        Speed15IpsWarm   = 1  // 48 Hz +2.20 dB Deep Warmth (Ballad / R&B / Jazz)
    };

    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        for (int ch = 0; ch < 2; ++ch)
        {
            subCutHp[ch].prepare(sr);
            headBumpBell[ch].prepare(sr);
            headDipBell[ch].prepare(sr);
        }
        setSpeedMode(currentSpeed);
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            subCutHp[ch].reset();
            headBumpBell[ch].reset();
            headDipBell[ch].reset();
        }
    }

    void setSpeedMode(TapeSpeedMode mode) noexcept
    {
        currentSpeed = mode;
        using Mode = vg::mastering::TptStateVariableFilter::Mode;
        for (int ch = 0; ch < 2; ++ch)
        {
            if (mode == TapeSpeedMode::Speed30IpsModern)
            {
                subCutHp[ch].setParameters(31.0f, 0.75f, 0.0f, Mode::Highpass);
                headBumpBell[ch].setParameters(68.0f, 1.35f, +1.65f, Mode::PeakBell);
                headDipBell[ch].setParameters(132.0f, 1.80f, -0.55f, Mode::PeakBell);
            }
            else
            {
                subCutHp[ch].setParameters(25.0f, 0.72f, 0.0f, Mode::Highpass);
                headBumpBell[ch].setParameters(48.0f, 1.25f, +2.20f, Mode::PeakBell);
                headDipBell[ch].setParameters(102.0f, 1.70f, -0.65f, Mode::PeakBell);
            }
        }
    }

    void processSamplePair(float& left, float& right, float amount01) noexcept
    {
        amount01 = std::clamp(amount01, 0.0f, 1.0f);
        if (amount01 <= 0.001f)
            return;

        float wetL = headDipBell[0].processSample(headBumpBell[0].processSample(subCutHp[0].processSample(left)));
        float wetR = headDipBell[1].processSample(headBumpBell[1].processSample(subCutHp[1].processSample(right)));

        left  = vg::mastering::FastAudioMath::sanitize(left + amount01 * (wetL - left));
        right = vg::mastering::FastAudioMath::sanitize(right + amount01 * (wetR - right));
    }

private:
    double sr = 48000.0;
    TapeSpeedMode currentSpeed = TapeSpeedMode::Speed30IpsModern;
    std::array<vg::mastering::TptStateVariableFilter, 2> subCutHp;
    std::array<vg::mastering::TptStateVariableFilter, 2> headBumpBell;
    std::array<vg::mastering::TptStateVariableFilter, 2> headDipBell;
};

//==============================================================================
// 3. Aphex Aural Exciter 方式・位相分散＆偶奇倍音ジェネレータ
//==============================================================================
class AphexPhaseHarmonicExciter
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        for (int ch = 0; ch < 2; ++ch)
        {
            sidechainHp[ch].prepare(sr);
            phaseRotatorAp[ch].prepare(sr);
            harmonicTamerLp[ch].prepare(sr);

            sidechainHp[ch].setParameters(2650.0f, 0.82f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::Highpass);
            phaseRotatorAp[ch].setParameters(3400.0f, 0.95f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::Allpass);
            harmonicTamerLp[ch].setParameters(15800.0f, 0.707f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::Lowpass);
            adaaExciter[ch].prepare(sr);
        }
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            sidechainHp[ch].reset();
            phaseRotatorAp[ch].reset();
            harmonicTamerLp[ch].reset();
            adaaExciter[ch].reset();
        }
    }

    void processSamplePair(float& left, float& right, float exciterMix01) noexcept
    {
        exciterMix01 = std::clamp(exciterMix01, 0.0f, 1.0f);
        if (exciterMix01 <= 0.0005f)
            return;

        // Left
        const float hpL = sidechainHp[0].processSample(left);
        const float rotL = phaseRotatorAp[0].processSample(hpL);
        const float harmL = harmonicTamerLp[0].processSample(adaaExciter[0].processSample(rotL, 2.35f, 0.32f, 1.0f));

        // Right
        const float hpR = sidechainHp[1].processSample(right);
        const float rotR = phaseRotatorAp[1].processSample(hpR);
        const float harmR = harmonicTamerLp[1].processSample(adaaExciter[1].processSample(rotR, 2.35f, 0.32f, 1.0f));

        const float injectGain = exciterMix01 * 0.22f;
        left  = vg::mastering::FastAudioMath::sanitize(left + injectGain * harmL);
        right = vg::mastering::FastAudioMath::sanitize(right + injectGain * harmR);
    }

private:
    double sr = 48000.0;
    std::array<vg::mastering::TptStateVariableFilter, 2> sidechainHp;
    std::array<vg::mastering::TptStateVariableFilter, 2> phaseRotatorAp;
    std::array<vg::mastering::TptStateVariableFilter, 2> harmonicTamerLp;
    std::array<vg::mastering::AdaaTriodeTransformerStage, 2> adaaExciter;
};

//==============================================================================
// 4. dbx 120A 方式・ゼロ交差 1オクターブ下サブハーモニック・チェストシンセサイザー
//==============================================================================
class SubHarmonicChestSynthesizer
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        const float dt = static_cast<float>(1.0 / sr);
        envAttack = 1.0f - std::exp(-dt / 0.004f);
        envRelease = 1.0f - std::exp(-dt / 0.035f);

        fundamentalExtractBp.prepare(sr);
        subResonatorBp.prepare(sr);

        // 120 Hz 〜 250 Hz のボーカル基音抽出
        fundamentalExtractBp.setParameters(175.0f, 1.45f, 0.0f,
                                           vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        // 半周期分周された 85 Hz チェストサブ基音を滑らかな正弦波に整形
        subResonatorBp.setParameters(88.0f, 1.65f, 0.0f,
                                     vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        reset();
    }

    void reset() noexcept
    {
        fundamentalExtractBp.reset();
        subResonatorBp.reset();
        lastFundSample = 0.0f;
        flipFlopSign = 1.0f;
        fundEnvelope = 0.0f;
    }

    void processSamplePair(float& left, float& right, float subChestAmount01) noexcept
    {
        subChestAmount01 = std::clamp(subChestAmount01, 0.0f, 1.0f);
        if (subChestAmount01 <= 0.0005f)
            return;

        const float mono = 0.5f * (left + right);
        const float fund = fundamentalExtractBp.processSample(mono);

        // 正方向ゼロ交差ごとにフリップフロップの極性を反転 (正確に 1/2 周波数のサブオクターブを生成)
        if (lastFundSample <= 0.0f && fund > 0.0f)
        {
            flipFlopSign = -flipFlopSign;
        }
        lastFundSample = fund;

        const float absFund = std::fabs(fund);
        const float coeff = (absFund > fundEnvelope) ? envAttack : envRelease;
        fundEnvelope += coeff * (absFund - fundEnvelope);

        const float rawSubWave = flipFlopSign * fundEnvelope;
        const float shapedSub = subResonatorBp.processSample(rawSubWave);

        // サブハーモニックはモノラル (Mid) 中心に注入して位相を完全ロック
        const float subInject = shapedSub * subChestAmount01 * 0.25f;
        left  = vg::mastering::FastAudioMath::sanitize(left + subInject);
        right = vg::mastering::FastAudioMath::sanitize(right + subInject);
    }

private:
    double sr = 48000.0;
    float envAttack = 0.01f;
    float envRelease = 0.001f;
    vg::mastering::TptStateVariableFilter fundamentalExtractBp;
    vg::mastering::TptStateVariableFilter subResonatorBp;
    float lastFundSample = 0.0f;
    float flipFlopSign = 1.0f;
    float fundEnvelope = 0.0f;
};

//==============================================================================
// 5. アナログテープ＆ハーモニック合成統合ラック (HarmonicExciterAndTapeSuite)
//==============================================================================
class HarmonicExciterAndTapeSuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        tapeL.prepare(sampleRate);
        tapeR.prepare(sampleRate);
        headBump.prepare(sampleRate);
        aphexExciter.prepare(sampleRate);
        subChestSynth.prepare(sampleRate);
    }

    void reset() noexcept
    {
        tapeL.reset();
        tapeR.reset();
        headBump.reset();
        aphexExciter.reset();
        subChestSynth.reset();
    }

    void processSamplePair(float& left,
                           float& right,
                           float tapeDrive01,
                           float headBumpAmount01,
                           float aphexExciter01,
                           float subChest01) noexcept
    {
        left  = tapeL.processSample(left, tapeDrive01, 0.35f);
        right = tapeR.processSample(right, tapeDrive01, 0.35f);
        headBump.processSamplePair(left, right, headBumpAmount01);
        aphexExciter.processSamplePair(left, right, aphexExciter01);
        subChestSynth.processSamplePair(left, right, subChest01);
    }

    MagneticHysteresisTapeCell& getTapeLeft() noexcept { return tapeL; }
    MagneticHysteresisTapeCell& getTapeRight() noexcept { return tapeR; }
    Studer30IpsHeadBumpFilter& getHeadBump() noexcept { return headBump; }
    AphexPhaseHarmonicExciter& getAphexExciter() noexcept { return aphexExciter; }
    SubHarmonicChestSynthesizer& getSubChestSynth() noexcept { return subChestSynth; }

private:
    MagneticHysteresisTapeCell tapeL;
    MagneticHysteresisTapeCell tapeR;
    Studer30IpsHeadBumpFilter headBump;
    AphexPhaseHarmonicExciter aphexExciter;
    SubHarmonicChestSynthesizer subChestSynth;
};

} // namespace vg::tape_and_harmonics
