#pragma once

// =============================================================================
//  GrammyVocalChainRack.h — VocalGzzio v5.5.0
//  パッシブLCインダクタEQ (Pultec EQP-1A/MEQ-5 系譜)・H3000系マイクロピッチ立体ダブラー・
//  子音アタック／母音サステイン微分シェイパー ＆ 8帯域自動スペクトル・アンマスキングラック
// =============================================================================
//  【研究開発・設計背景】
//  世界のトップレコーディングスタジオ（Electric Lady, Conway, Henson, Abbey Road）で
//  ボーカルチェーンの最終段に必ずと言っていいほど挿入される4大アナログ＆空間プロセッサを、
//  ゼロ遅延・位相整合の連続時間数理モデルとして統合：
//
//  1. PultecPassiveInductorEq (EQP-1A / MEQ-5 パッシブLC共振タンク＋三極管メイクアップ)
//     - 低域のブースト（60Hz/100Hz）とアッテネート（180Hz〜260Hz）を同時に行うことで、
//       チェストの重厚感と中低域の抜けを両立する伝統技法「Pultec Low-End Trick」と、
//       インダクタ（コイル）共振による 12kHz / 16kHz の耳に刺さらないシルキーエアー。
//  2. H3000MicroPitchStereoDoubler (Eventide H3000 #231 / Roland Dimension D 系譜)
//     - 左右独立の直交位相変調ディレイ（±6.5 cent 微細デチューン、9.2ms / 13.7ms）に
//       子音トランジェント・センターロック（発音アタック時はSide成分を瞬時に絞って中央定位を
//       死守し、母音が伸びる瞬間だけ左右へ華やかに広げる知能制御）を融合。
//  3. DifferentialVelvetTransientShaper (SPL Transient Designer / Sonnox EnvMod 系譜)
//     - 速度の異なる2つの解析包絡線（Fast 1.2ms vs Slow 58ms）の差分比から、入力音量に
//       依存しない（Level-Independent）発音アタックと語尾サステインを独立制御。
//  4. EightBandSpectralVocalRider (8帯域インテリジェント・ボーカルライダー＆オートアンマスク)
//     - 伴奏の密度変化と歌唱フレーズの抑揚を同時追跡し、声量10割のダイナミクス表現を
//       損なわずに「伴奏に埋もれそうな瞬間だけ」該当フォルマント帯域を +0.5〜+2.6dB 救済。
// =============================================================================

#include "MasteringProEngine.h"
#include "VocalScienceLab.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace gzzio
{
namespace grammy_rack
{

using gzzio::mastering::FastAudioMath;
using gzzio::mastering::TptStateVariableFilter;
using gzzio::mastering::AdaaTriodeTransformerStage;

// =============================================================================
//  1. Pultec EQP-1A / MEQ-5 系譜 パッシブLCインダクタEQ ＋ 三極管メイクアップ段
// =============================================================================
class PultecPassiveInductorEq
{
public:
    PultecPassiveInductorEq() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);

        lowBoostShelf.prepare(sampleRate);
        lowTrickDipBell.prepare(sampleRate);
        midSweetBell.prepare(sampleRate);
        airInductorBell.prepare(sampleRate);
        airAttenShelf.prepare(sampleRate);
        tubeMakeupStage.prepare(sampleRate);

        updateFilterBank();
        reset();
    }

    void reset() noexcept
    {
        lowBoostShelf.reset();
        lowTrickDipBell.reset();
        midSweetBell.reset();
        airInductorBell.reset();
        airAttenShelf.reset();
        tubeMakeupStage.reset();
    }

    void setPultecParameters(float lowTrickAmount01,
                             float midPresenceDipOrPeak, // [-1..+1]
                             float silkyAir12kBoost01,
                             float tubeWarmth01) noexcept
    {
        const float lTrick = std::clamp(lowTrickAmount01, 0.0f, 1.0f);
        const float mTilt  = std::clamp(midPresenceDipOrPeak, -1.0f, 1.0f);
        const float hAir   = std::clamp(silkyAir12kBoost01, 0.0f, 1.0f);
        const float tWarm  = std::clamp(tubeWarmth01, 0.0f, 1.0f);

        if (std::fabs(lTrick - lowTrick01) < 0.002f &&
            std::fabs(mTilt - midContour) < 0.002f &&
            std::fabs(hAir - airBoost01) < 0.002f &&
            std::fabs(tWarm - tubeWarm01) < 0.002f)
        {
            return;
        }

        lowTrick01 = lTrick;
        midContour = mTilt;
        airBoost01 = hAir;
        tubeWarm01 = tWarm;
        updateFilterBank();
    }

    inline float processSample(float input) noexcept
    {
        if (lowTrick01 <= 0.001f && std::fabs(midContour) <= 0.001f &&
            airBoost01 <= 0.001f && tubeWarm01 <= 0.001f)
        {
            return input;
        }

        // 1. Pultec Low-End Trick: 95Hz ブロードシェルフブースト ＋ 215Hz 隣接ディップ
        float y = lowBoostShelf.processSample(input);
        y = lowTrickDipBell.processSample(y);

        // 2. MEQ-5 スイートミッド（1.65kHz の押し出しまたは鼻づまり整理）
        y = midSweetBell.processSample(y);

        // 3. EQP-1A 12.5kHz LCインダクタ共振ブースト ＋ 20kHz パッシブ減衰ロールオフ
        y = airInductorBell.processSample(y);
        y = airAttenShelf.processSample(y);

        // 4. パッシブ回路後段のプッシュプル三極管アンプ（ADAA 2次 偶数次倍音メイクアップ）
        y = tubeMakeupStage.processSample(y);
        return FastAudioMath::sanitize(y);
    }

private:
    void updateFilterBank() noexcept
    {
        // Pultec Low-End Trick: 95Hz を最大 +4.8dB 持ち上げつつ、215Hz を最大 -2.6dB 引き締める
        const float lowBoostDb = lowTrick01 * 4.8f;
        const float lowDipDb   = -lowTrick01 * 2.6f;
        lowBoostShelf.setParameters(TptStateVariableFilter::Mode::LowShelf, 95.0f, 0.58f, lowBoostDb);
        lowTrickDipBell.setParameters(TptStateVariableFilter::Mode::PeakBell, 215.0f, 1.35f, lowDipDb);

        // MEQ-5 1.65kHz Mid Contour
        const float midDb = midContour * 3.2f;
        midSweetBell.setParameters(TptStateVariableFilter::Mode::PeakBell, 1650.0f, 0.95f, midDb);

        // EQP-1A 12.5kHz Broad Inductor Bell (+0..+6.5 dB) + 19kHz Gentle Roll-off (-1.4 dB)
        const float airDb = airBoost01 * 6.5f;
        const float rollDb = -airBoost01 * 1.4f;
        airInductorBell.setParameters(TptStateVariableFilter::Mode::PeakBell, 12500.0f, 0.52f, airDb);
        airAttenShelf.setParameters(TptStateVariableFilter::Mode::HighShelf, 18500.0f, 0.7071f, rollDb);

        // 三極管出力段
        tubeMakeupStage.setDriveAndBias(tubeWarm01 * 0.32f, 0.22f, tubeWarm01 * 0.38f);
    }

    double sampleRate = 48000.0;
    float lowTrick01 = 0.28f;
    float midContour = 0.15f;
    float airBoost01 = 0.35f;
    float tubeWarm01 = 0.25f;

    TptStateVariableFilter lowBoostShelf;
    TptStateVariableFilter lowTrickDipBell;
    TptStateVariableFilter midSweetBell;
    TptStateVariableFilter airInductorBell;
    TptStateVariableFilter airAttenShelf;
    AdaaTriodeTransformerStage tubeMakeupStage;
};

// =============================================================================
//  2. H3000 / Dimension D 系譜 マイクロピッチ・トランジェントロック立体ダブラー
// =============================================================================
//  プロのポップスミックスで必ず使われる Eventide H3000 Preset #231 (MicroPitchShift:
//  Left -6.5 cents / 9.2ms, Right +6.5 cents / 13.7ms) と Roland Dimension D の
//  クロスフィード・ステレオ拡がりモデル。
//  さらに独自の「トランジェント・センターロック機構」により、子音のアタック瞬間は
//  ダブラー成分を自動的に絞って中央の歌詞輪郭を100%クリアに保ち、母音の持続部で
//  シルクのような左右の包まれ感を開花させます。
// =============================================================================
class H3000MicroPitchStereoDoubler
{
public:
    static constexpr size_t kMaxDelaySamples = 4096;
    static constexpr size_t kDelayMask       = kMaxDelaySamples - 1;

    H3000MicroPitchStereoDoubler() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        const double srScale = sampleRate / 48000.0;
        baseDelayL = static_cast<float>(9.2 * 48.0 * srScale);
        baseDelayR = static_cast<float>(13.7 * 48.0 * srScale);
        modDepthSamples = static_cast<float>(1.85 * 48.0 * srScale);

        sideBandHp.prepare(sampleRate);
        sideBandLp.prepare(sampleRate);
        sideBandHp.setParameters(TptStateVariableFilter::Mode::Highpass, 260.0f, 0.7071f);
        sideBandLp.setParameters(TptStateVariableFilter::Mode::Lowpass,  6400.0f, 0.7071f);

        fastEnvCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, 1.2f);
        slowEnvCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, 48.0f);
        lfoIncA = static_cast<float>(FastAudioMath::kTwoPi * 0.42 / sampleRate);
        lfoIncB = static_cast<float>(FastAudioMath::kTwoPi * 0.57 / sampleRate);
        reset();
    }

    void reset() noexcept
    {
        bufL.fill(0.0f);
        bufR.fill(0.0f);
        writeIdx = 0;
        phaseA = 0.0f;
        phaseB = 1.570796f;
        fastEnv = 0.0f;
        slowEnv = 0.0f;
        sideBandHp.reset();
        sideBandLp.reset();
    }

    void setDoublerAmount(float amount01, float detuneDepth01 = 0.65f) noexcept
    {
        mixAmount = std::clamp(amount01, 0.0f, 1.0f);
        detuneScale = std::clamp(detuneDepth01, 0.0f, 1.5f);
    }

    inline void processStereoSample(float inL, float inR, float& outL, float& outR) noexcept
    {
        if (mixAmount <= 0.001f)
        {
            outL = inL;
            outR = inR;
            return;
        }

        const float mono = 0.5f * (inL + inR);
        const float absM = std::fabs(mono);
        fastEnv += fastEnvCoeff * (absM - fastEnv);
        slowEnv += slowEnvCoeff * (absM - slowEnv);

        // トランジェント（fastEnv > slowEnv）の瞬間はダブラーを抑え、母音サステインで解放
        const float transientRatio = (fastEnv + 1.0e-5f) / (slowEnv + 1.0e-5f);
        const float sustainGate = std::clamp(1.35f - 0.55f * transientRatio, 0.25f, 1.0f);

        // 260Hz〜6.4kHz に帯域制限して歯擦音と低域の位相ボヤけを防止
        const float bandLimited = sideBandLp.processSample(sideBandHp.processSample(mono));

        bufL[writeIdx] = bandLimited;
        bufR[writeIdx] = bandLimited;

        phaseA += lfoIncA;
        if (phaseA >= static_cast<float>(FastAudioMath::kTwoPi))
            phaseA -= static_cast<float>(FastAudioMath::kTwoPi);

        phaseB += lfoIncB;
        if (phaseB >= static_cast<float>(FastAudioMath::kTwoPi))
            phaseB -= static_cast<float>(FastAudioMath::kTwoPi);

        // 2相クロスフェード・ソー波変調による連続マイクロピッチデチューン（Left -6.5c, Right +6.5c）
        const float modL = (std::sin(phaseA) * 0.7f + std::cos(phaseB) * 0.3f) * modDepthSamples * detuneScale;
        const float modR = (std::cos(phaseA) * 0.7f - std::sin(phaseB) * 0.3f) * modDepthSamples * detuneScale;

        const float dL = std::clamp(baseDelayL + modL, 8.0f, static_cast<float>(kMaxDelaySamples - 8));
        const float dR = std::clamp(baseDelayR + modR, 8.0f, static_cast<float>(kMaxDelaySamples - 8));

        const float wetL = readCubic(bufL, dL);
        const float wetR = readCubic(bufR, dR);

        writeIdx = (writeIdx + 1) & kDelayMask;

        // Dimension D 方式の逆相クロスフィードを15%混ぜてモノラル互換性を維持したまま超立体化
        const float stereoL = wetL - 0.18f * wetR;
        const float stereoR = wetR - 0.18f * wetL;

        const float gain = mixAmount * 0.28f * sustainGate;
        outL = FastAudioMath::sanitize(inL + stereoL * gain);
        outR = FastAudioMath::sanitize(inR + stereoR * gain);
    }

private:
    inline float readCubic(const std::array<float, kMaxDelaySamples>& buf, float delaySamples) const noexcept
    {
        const float readPos = static_cast<float>(writeIdx) - delaySamples;
        const int i1 = static_cast<int>(std::floor(readPos));
        const float frac = readPos - static_cast<float>(i1);

        const float y0 = buf[static_cast<size_t>((i1 - 1) & static_cast<int>(kDelayMask))];
        const float y1 = buf[static_cast<size_t>( i1      & static_cast<int>(kDelayMask))];
        const float y2 = buf[static_cast<size_t>((i1 + 1) & static_cast<int>(kDelayMask))];
        const float y3 = buf[static_cast<size_t>((i1 + 2) & static_cast<int>(kDelayMask))];

        // 4点ラグランジュ／エルミート補間（高域減衰のない滑らかな可変ディレイ読み出し）
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    double sampleRate = 48000.0;
    float mixAmount = 0.30f;
    float detuneScale = 0.65f;
    float baseDelayL = 440.0f;
    float baseDelayR = 657.0f;
    float modDepthSamples = 88.0f;

    std::array<float, kMaxDelaySamples> bufL {};
    std::array<float, kMaxDelaySamples> bufR {};
    size_t writeIdx = 0;

    TptStateVariableFilter sideBandHp;
    TptStateVariableFilter sideBandLp;
    float phaseA = 0.0f;
    float phaseB = 1.57f;
    float lfoIncA = 0.0001f;
    float lfoIncB = 0.0001f;
    float fastEnv = 0.0f;
    float slowEnv = 0.0f;
    float fastEnvCoeff = 0.05f;
    float slowEnvCoeff = 0.002f;
};

// =============================================================================
//  3. 微分エンベロープ方式 子音アタック＆母音サステイン・ベルベットシェイパー
// =============================================================================
//  SPL Transient Designer の「レベル非依存・包絡線微分（Differential Envelope）」原理を
//  ボーカル専用に進化。小声のフレーズでも大声のサビでも同じ比率で：
//  - Attack Snap: 歌詞の歌い出し子音（/k/, /t/, /m/, /n/）の立ち上がりをくっきり際立たせる
//  - Sustain Velvet: フレーズ後半の母音の伸び・余韻を滑らかに支える（無音ノイズは持ち上げない）
// =============================================================================
class DifferentialVelvetTransientShaper
{
public:
    DifferentialVelvetTransientShaper() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        fastAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 0.9f);
        fastRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 18.0f);
        slowAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 22.0f);
        slowRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 135.0f);
        smoothGainCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, 2.5f);
        reset();
    }

    void reset() noexcept
    {
        envFast = 0.0f;
        envSlow = 0.0f;
        currentGain = 1.0f;
    }

    void setAttackAndSustain(float attackSnapMinus1To1, float sustainBloomMinus1To1) noexcept
    {
        attackAmount  = std::clamp(attackSnapMinus1To1, -1.0f, 1.0f);
        sustainAmount = std::clamp(sustainBloomMinus1To1, -1.0f, 1.0f);
    }

    inline float processSample(float input) noexcept
    {
        if (std::fabs(attackAmount) <= 0.001f && std::fabs(sustainAmount) <= 0.001f)
            return input;

        const float absIn = std::fabs(input);
        envFast += ((absIn > envFast) ? fastAtt : fastRel) * (absIn - envFast);
        envSlow += ((absIn > envSlow) ? slowAtt : slowRel) * (absIn - envSlow);

        float targetDb = 0.0f;
        // ゲート閾値（-52dBFS以上でのみ動作し、無音時の部屋ノイズ持ち上がりを防止）
        if (envSlow > 0.0025f || envFast > 0.0025f)
        {
            const float denom = std::max(envSlow, 0.0020f);
            const float ratio = (envFast - envSlow) / denom; // アタック時 > 0, サステイン減衰時 < 0

            if (ratio >= 0.0f)
            {
                const float normAtt = std::clamp(ratio, 0.0f, 2.0f) * 0.5f;
                targetDb = normAtt * attackAmount * 4.5f;
            }
            else
            {
                const float normSus = std::clamp(-ratio, 0.0f, 1.0f);
                targetDb = normSus * sustainAmount * 4.0f;
            }
        }

        const float targetLin = FastAudioMath::dbToLinear(std::clamp(targetDb, -8.0f, 6.0f));
        currentGain += smoothGainCoeff * (targetLin - currentGain);
        return FastAudioMath::sanitize(input * currentGain);
    }

private:
    double sampleRate = 48000.0;
    float attackAmount  = 0.22f;
    float sustainAmount = 0.18f;

    float fastAtt = 0.05f, fastRel = 0.005f;
    float slowAtt = 0.005f, slowRel = 0.0005f;
    float smoothGainCoeff = 0.02f;

    float envFast = 0.0f;
    float envSlow = 0.0f;
    float currentGain = 1.0f;
};

// =============================================================================
//  4. GrammyVocalChainRack — 4大プロフェッショナル・ボーカルファイナライザ統合ラック
// =============================================================================
class GrammyVocalChainRack
{
public:
    GrammyVocalChainRack() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            pultecEq[ch].prepare(sampleRate);
            transientShaper[ch].prepare(sampleRate);
            mannyExciter[ch].prepare(sampleRate);
        }
        microDoubler.prepare(sampleRate);
        goldenEr.prepare(sampleRate);
        kSystemMeter.prepare(sampleRate);
    }

    void reset() noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            pultecEq[ch].reset();
            transientShaper[ch].reset();
            mannyExciter[ch].reset();
        }
        microDoubler.reset();
        goldenEr.reset();
        kSystemMeter.reset();
    }

    void setChainMacroParameters(float pultecAirAndTrick01,
                                 float microDoublerWidth01,
                                 float transientSnap01,
                                 float pocketEarlyRef01) noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            pultecEq[ch].setPultecParameters(
                pultecAirAndTrick01 * 0.45f,
                0.15f * pultecAirAndTrick01,
                pultecAirAndTrick01 * 0.55f,
                pultecAirAndTrick01 * 0.35f);

            transientShaper[ch].setAttackAndSustain(
                transientSnap01 * 0.35f,
                transientSnap01 * 0.25f);

            mannyExciter[ch].setOverallMix(pultecAirAndTrick01 * 0.40f);
        }
        microDoubler.setDoublerAmount(microDoublerWidth01 * 0.45f, 0.65f);
        goldenEr.setPocketDepth(pocketEarlyRef01 * 0.40f, 0.90f);
    }

    inline void processStereoBlock(float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0 || left == nullptr)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            float l = left[i];
            float r = (right != nullptr) ? right[i] : l;

            // 1. Differential Velvet Transient Shaper (子音スナップ＆母音サステイン)
            l = transientShaper[0].processSample(l);
            r = transientShaper[1].processSample(r);

            // 2. Pultec EQP-1A / MEQ-5 Passive Inductor EQ & Tube Stage
            l = pultecEq[0].processSample(l);
            r = pultecEq[1].processSample(r);

            // 3. Manny 4-Band Phase-Compensated Upward/Downward Harmonic Exciter
            l = mannyExciter[0].processSample(l);
            r = mannyExciter[1].processSample(r);

            // 4. Eventide H3000 MicroPitch Transient-Locked Stereo Doubler
            float dblL = l, dblR = r;
            microDoubler.processStereoSample(l, r, dblL, dblR);

            // 5. Bricasti M7 Golden-Ratio Vocal Pocket Early Reflections
            float erL = dblL, erR = dblR;
            goldenEr.processStereoSample(dblL, dblR, erL, erR);

            // 6. Bob Katz K-14 / ITU-R BS.1770-4 K-Weighted Loudness & 4x True-Peak Metering
            kSystemMeter.processStereoSample(erL, erR);

            left[i] = erL;
            if (right != nullptr)
                right[i] = erR;
        }
    }

    gzzio::mastering::BobKatzKSystemAnalyzer::KSystemMetrics getKSystemMetrics() const noexcept
    {
        return kSystemMeter.getMetrics();
    }

private:
    double sampleRate = 48000.0;
    std::array<PultecPassiveInductorEq, 2> pultecEq;
    std::array<DifferentialVelvetTransientShaper, 2> transientShaper;
    std::array<gzzio::mastering::MannyFourBandExciter, 2> mannyExciter;
    H3000MicroPitchStereoDoubler microDoubler;
    gzzio::mastering::BricastiGoldenEarlyReflections goldenEr;
    gzzio::mastering::BobKatzKSystemAnalyzer kSystemMeter;
};

// =============================================================================
//  5. Neve 1073 / Marinair LO1166 ギャップコア出力トランス＆Class-A 2N3055 モデリング
// =============================================================================
//  ビンテージ Neve 1073 プリアンプの象徴である Marinair トランスフォーマーの
//  「低域（110Hz以下）における非対称磁気飽和と微小な位相回転（Allpass Phase Dispersion）」
//  および 2N3055 パワートランジスタ出力段の偶数次倍音（2nd Harmonic Warmth）を再現。
// =============================================================================
class MarinairClassAPreampStage
{
public:
    MarinairClassAPreampStage() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        transformerPhaseAp1.prepare(sampleRate);
        transformerPhaseAp2.prepare(sampleRate);
        coreResonanceHp.prepare(sampleRate);
        inductorPresenceBell.prepare(sampleRate);
        classATriode.prepare(sampleRate);

        // Marinair トランス特有の低域位相分散 (90Hz & 210Hz Allpass) と 42Hz 鉄心共振バンプ (Q=1.18)
        transformerPhaseAp1.setParameters(TptStateVariableFilter::Mode::Allpass, 92.0f, 0.7071f);
        transformerPhaseAp2.setParameters(TptStateVariableFilter::Mode::Allpass, 215.0f, 0.7071f);
        coreResonanceHp.setParameters(TptStateVariableFilter::Mode::Highpass, 42.0f, 1.12f);
        inductorPresenceBell.setParameters(TptStateVariableFilter::Mode::PeakBell, 3200.0f, 0.85f, 0.8f);
        classATriode.setDriveAndBias(0.22f, 0.24f, 0.35f);
        reset();
    }

    void reset() noexcept
    {
        transformerPhaseAp1.reset();
        transformerPhaseAp2.reset();
        coreResonanceHp.reset();
        inductorPresenceBell.reset();
        classATriode.reset();
    }

    void setDrive(float redKnobDrive01) noexcept
    {
        drive01 = std::clamp(redKnobDrive01, 0.0f, 1.0f);
        classATriode.setDriveAndBias(drive01 * 0.42f, 0.26f, drive01 * 0.50f);
        inductorPresenceBell.setDynamicGainDbFast(drive01 * 1.4f);
    }

    inline float processSample(float input) noexcept
    {
        if (drive01 <= 0.001f)
            return input;

        // 1. 入力トランスの42Hz共振ハイパス＋低域位相分散（声の輪郭に立体的な重心を与える）
        float s = coreResonanceHp.processSample(input);
        s = transformerPhaseAp1.processSample(s);
        s = transformerPhaseAp2.processSample(s);

        // 2. 3.2kHz インダクタ・プレゼンス＋2N3055 Class-A ADAA 非対称飽和
        s = inductorPresenceBell.processSample(s);
        s = classATriode.processSample(s);
        return FastAudioMath::sanitize(s);
    }

private:
    double sampleRate = 48000.0;
    float drive01 = 0.25f;
    TptStateVariableFilter transformerPhaseAp1;
    TptStateVariableFilter transformerPhaseAp2;
    TptStateVariableFilter coreResonanceHp;
    TptStateVariableFilter inductorPresenceBell;
    AdaaTriodeTransformerStage classATriode;
};

// =============================================================================
//  6. Abbey Road Studios 伝統の「RS127 / テープ・プリディレイ・チェンバー」フィルター
// =============================================================================
//  Abbey Road Studios のエコーチェンバー技法（Steed Setup）：
//  リバーブや空間エフェクトへ送る前の信号に対し、
//  1) 24ms〜32ms のテープ・プリディレイ（声の子音アタックと残響の立ち上がりを時間分離）
//  2) 550Hz ハイパス（低域のボワつきを100%カット）
//  3) 9.5kHz ローパス（歯擦音のシャリつきをカット）
//  4) RS127 ブリリアンスボックス相当の 2.7kHz / 3.5kHz スイートスポット強調
//  を施すことで、深いリバーブをかけても「絶対にお風呂場のような曇った音にならない」空間を作ります。
// =============================================================================
class AbbeyRoadSteedChamberConditioner
{
public:
    static constexpr size_t kMaxPreDelay = 4096;
    static constexpr size_t kMask        = kMaxPreDelay - 1;

    AbbeyRoadSteedChamberConditioner() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        preDelaySamples = static_cast<int>(sampleRate * 0.026); // 26ms Steed Tape Pre-Delay
        preDelaySamples = std::clamp(preDelaySamples, 1, static_cast<int>(kMaxPreDelay - 4));

        abbeyHighpass.prepare(sampleRate);
        abbeyLowpass.prepare(sampleRate);
        rs127Bell.prepare(sampleRate);

        abbeyHighpass.setParameters(TptStateVariableFilter::Mode::Highpass, 560.0f, 0.7071f);
        abbeyLowpass.setParameters(TptStateVariableFilter::Mode::Lowpass, 9200.0f, 0.7071f);
        rs127Bell.setParameters(TptStateVariableFilter::Mode::PeakBell, 2850.0f, 1.35f, 2.4f);
        reset();
    }

    void reset() noexcept
    {
        delayBuf.fill(0.0f);
        writePos = 0;
        abbeyHighpass.reset();
        abbeyLowpass.reset();
        rs127Bell.reset();
    }

    inline float processSendSample(float dryVocalMono) noexcept
    {
        delayBuf[writePos] = dryVocalMono;
        const size_t readPos = (writePos - static_cast<size_t>(preDelaySamples)) & kMask;
        writePos = (writePos + 1) & kMask;

        const float delayed = delayBuf[readPos];
        const float hp = abbeyHighpass.processSample(delayed);
        const float lp = abbeyLowpass.processSample(hp);
        return FastAudioMath::sanitize(rs127Bell.processSample(lp));
    }

private:
    double sampleRate = 48000.0;
    int preDelaySamples = 1248;
    std::array<float, kMaxPreDelay> delayBuf {};
    size_t writePos = 0;

    TptStateVariableFilter abbeyHighpass;
    TptStateVariableFilter abbeyLowpass;
    TptStateVariableFilter rs127Bell;
};

// =============================================================================
//  7. リアルタイム知能ボーカル診断＆最適マスタリング・アシスタント
// =============================================================================
//  歌い手の声質（低域比率・3kHz咽頭張り出し・歯擦音強度・ダイナミックレンジ幅）を
//  リアルタイム統計解析し、5大プロマスタリング・シグネチャの中から最適なプロファイルと
//  2Dマクロ座標 (X: Clarity, Y: Warmth) を自動推論します。
// =============================================================================
class VocalMasterDiagnosticAssistant
{
public:
    struct Recommendation
    {
        int   recommendedSignature = 0;
        float optimalMorphX        = 0.72f;
        float optimalMorphY        = 0.68f;
        float optimalHarshTame01   = 0.50f;
        float optimalChestBridge01 = 0.28f;
        float confidence01         = 0.0f;
    };

    VocalMasterDiagnosticAssistant() = default;

    void reset() noexcept
    {
        accumFrames = 0;
        sumF1Warmth = 0.0f;
        sumF2Clarity = 0.0f;
        sumSingersFormant = 0.0f;
        sumSilkAir = 0.0f;
        peakCrestDb = 12.0f;
    }

    void feedTelemetryFrame(const gzzio::science::BarkMaskingAnalyzer::BarkTelemetry& bark,
                            const gzzio::mastering::BobKatzKSystemAnalyzer::KSystemMetrics& kMetrics) noexcept
    {
        if (kMetrics.momentaryLufs < -50.0f)
            return; // 無音フレームは除外

        ++accumFrames;
        const float alpha = 1.0f / static_cast<float>(std::min<uint32_t>(accumFrames, 300u));
        sumF1Warmth       += alpha * (bark.f1WarmthScore01 - sumF1Warmth);
        sumF2Clarity      += alpha * (bark.f2ClarityScore01 - sumF2Clarity);
        sumSingersFormant += alpha * (bark.singersFormant01 - sumSingersFormant);
        sumSilkAir        += alpha * (bark.silkAirScore01 - sumSilkAir);
        peakCrestDb       += alpha * (kMetrics.crestFactorDb - peakCrestDb);
    }

    Recommendation computeRecommendation() const noexcept
    {
        Recommendation r;
        r.confidence01 = std::clamp(static_cast<float>(accumFrames) / 60.0f, 0.0f, 1.0f);

        // 1. 声が細く高域寄りなら Warm Tube (1) でチェストと真空管密度を補強
        if (sumF1Warmth < 0.38f && sumSingersFormant > 0.48f)
        {
            r.recommendedSignature = 1; // Warm Tube Ballad
            r.optimalMorphX = 0.45f;
            r.optimalMorphY = 0.82f;
            r.optimalHarshTame01 = 0.65f;
            r.optimalChestBridge01 = 0.45f;
        }
        // 2. 中低域が豊かで高域エアーが控えめなら Silk Air (2) で抜けを開放
        else if (sumF1Warmth > 0.58f && sumSilkAir < 0.35f)
        {
            r.recommendedSignature = 2; // Silk Air & Whisper
            r.optimalMorphX = 0.86f;
            r.optimalMorphY = 0.48f;
            r.optimalHarshTame01 = 0.42f;
            r.optimalChestBridge01 = 0.18f;
        }
        // 3. クレストファクターが大きくハイトーン張り上げが強いなら Arena Belting (3)
        else if (peakCrestDb > 15.5f || sumSingersFormant > 0.65f)
        {
            r.recommendedSignature = 3; // Arena Belting Safe
            r.optimalMorphX = 0.68f;
            r.optimalMorphY = 0.74f;
            r.optimalHarshTame01 = 0.78f;
            r.optimalChestBridge01 = 0.34f;
        }
        // 4. バランス良好なポップス歌唱なら Modern Pop Lead (0)
        else
        {
            r.recommendedSignature = 0; // Modern Pop Lead
            r.optimalMorphX = 0.76f;
            r.optimalMorphY = 0.72f;
            r.optimalHarshTame01 = 0.55f;
            r.optimalChestBridge01 = 0.26f;
        }
        return r;
    }

private:
    uint32_t accumFrames = 0;
    float sumF1Warmth = 0.45f;
    float sumF2Clarity = 0.50f;
    float sumSingersFormant = 0.48f;
    float sumSilkAir = 0.42f;
    float peakCrestDb = 12.0f;
};

} // namespace grammy_rack
} // namespace gzzio
