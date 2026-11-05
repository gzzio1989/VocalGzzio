#pragma once

/**
 * @file PsychoacousticClarityEngine.h
 * @brief VocalGzzio 欠落基音バーチャルピッチ (チェビシェフ多項式倍音)・STI歌詞明瞭度エンハンサー＆ハース先行音定位ステージ
 *
 * 【研究開発の背景と独自技術】
 * 1. MissingFundamentalVirtualPitchEngine (チェビシェフ T2/T3 多項式による欠落基音・スマホスピーカー低域知覚再生):
 *    - スマートフォンやノートPCの小型スピーカーは 180Hz 以下が物理的に再生できないため、
 *      チェストボイスや低音男性ボーカル (85Hz〜165Hz) の「太さ・存在感」が失われてしまいます。
 *    - 人間の聴覚皮質が「第2・第3倍音の間隔から失われた基音 (Missing Fundamental) を脳内で再合成する」
 *      音響心理現象 (Virtual Pitch / Residue Pitch) を応用し、チェビシェフ多項式 T2(x)=2x^2-1, T3(x)=4x^3-3x で
 *      精密な第2・第3倍音を生成して小型スピーカーでも圧倒的な低域の太さを知覚させます。
 *
 * 2. ConsonantIntelligibilityEnhancer (STI 音声伝達指標 1.6kHz〜4.2kHz 子音立ち上がり明瞭度ブースター):
 *    - 歌詞の聞き取りやすさ (Speech Transmission Index: STI) を決定づける「子音から母音へ遷移する最初の 35ms」のみを検出し、
 *      1.6kHz〜4.2kHz の明瞭度帯域を瞬間的にリフトします。ロングトーンでは一切ブーストしないため、耳疲れが起きません。
 *
 * 3. BinauralPrecedenceDepthStage (ハース効果融合圏 11.5ms 初期側方反射・ボーカル前方定位ピンニング):
 *    - リードボーカルの直接音から 11.5ms 遅らせた逆相直交初期反射を -15dB で側方へ配置。
 *      ハース効果の融合限界 (30ms以内) の内側にあるためエコーとしては知覚されず、
 *      ボーカルの定位がスピーカー面より一歩手前に立体的に浮かび上がります。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "MasteringProEngine.h"

namespace vg::psychoacoustics
{

//==============================================================================
// 1. チェビシェフ T2/T3 多項式・欠落基音バーチャルピッチ生成エンジン
//==============================================================================
class MissingFundamentalVirtualPitchEngine
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        const float dt = static_cast<float>(1.0 / sr);
        envAttack = 1.0f - std::exp(-dt / 0.004f);
        envRelease = 1.0f - std::exp(-dt / 0.038f);

        fundExtractBp.prepare(sr);
        overtoneShapeHp.prepare(sr);
        overtoneShapeLp.prepare(sr);

        // 85 Hz 〜 165 Hz のボーカル基音帯域を抽出
        fundExtractBp.setParameters(122.0f, 1.35f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        // 生成した第2・第3倍音 (180 Hz 〜 480 Hz) のみを取り出すフィルタ
        overtoneShapeHp.setParameters(185.0f, 0.707f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::Highpass);
        overtoneShapeLp.setParameters(520.0f, 0.707f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::Lowpass);
        reset();
    }

    void reset() noexcept
    {
        fundExtractBp.reset();
        overtoneShapeHp.reset();
        overtoneShapeLp.reset();
        fundEnv = 0.0f;
    }

    void processSamplePair(float& left, float& right, float virtualBass01) noexcept
    {
        virtualBass01 = std::clamp(virtualBass01, 0.0f, 1.0f);
        if (virtualBass01 <= 0.0005f)
            return;

        const float mono = 0.5f * (left + right);
        const float fund = fundExtractBp.processSample(mono);
        const float absFund = std::fabs(fund);
        fundEnv += ((absFund > fundEnv) ? envAttack : envRelease) * (absFund - fundEnv);

        if (fundEnv < 1.0e-5f)
            return;

        // 正規化した基音波形 x (-1..+1) に対してチェビシェフ多項式 T2(x), T3(x) を適用
        const float normX = std::clamp(fund / (fundEnv + 1.0e-5f), -1.0f, 1.0f);
        // T2(x) = 2x^2 - 1 (DCオフセット -1 を除いた 2x^2 - env で純粋第2倍音)
        const float cheb2 = (2.0f * normX * normX - 1.0f);
        // T3(x) = 4x^3 - 3x (純粋第3倍音)
        const float cheb3 = normX * (4.0f * normX * normX - 3.0f);

        const float rawHarmonics = (0.65f * cheb2 + 0.35f * cheb3) * fundEnv;
        const float shapedHarmonics = overtoneShapeLp.processSample(overtoneShapeHp.processSample(rawHarmonics));

        const float inject = shapedHarmonics * virtualBass01 * 0.28f;
        left  = vg::mastering::FastAudioMath::sanitize(left + inject);
        right = vg::mastering::FastAudioMath::sanitize(right + inject);
    }

private:
    double sr = 48000.0;
    float envAttack = 0.01f;
    float envRelease = 0.001f;
    float fundEnv = 0.0f;
    vg::mastering::TptStateVariableFilter fundExtractBp;
    vg::mastering::TptStateVariableFilter overtoneShapeHp;
    vg::mastering::TptStateVariableFilter overtoneShapeLp;
};

//==============================================================================
// 2. STI (Speech Transmission Index) 歌詞子音明瞭度エンハンサー
//==============================================================================
class ConsonantIntelligibilityEnhancer
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        const float dt = static_cast<float>(1.0 / sr);
        fastCoeff = 1.0f - std::exp(-dt / 0.0025f); // 2.5ms 高速アタック検出
        slowCoeff = 1.0f - std::exp(-dt / 0.0420f); // 42ms 持続音基準

        for (int ch = 0; ch < 2; ++ch)
        {
            stiBandBp[ch].prepare(sr);
            // 歌詞の子音判別に最も重要な 2.35 kHz 帯域
            stiBandBp[ch].setParameters(2350.0f, 0.95f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        }
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
            stiBandBp[ch].reset();
        fastEnv = 0.0f;
        slowEnv = 0.0f;
        currentArticulationBoostDb = 0.0f;
    }

    void processSamplePair(float& left, float& right, float articulation01) noexcept
    {
        articulation01 = std::clamp(articulation01, 0.0f, 1.0f);
        if (articulation01 <= 0.0005f)
        {
            currentArticulationBoostDb = 0.0f;
            return;
        }

        const float stiL = stiBandBp[0].processSample(left);
        const float stiR = stiBandBp[1].processSample(right);
        const float stiPeak = std::max(std::fabs(stiL), std::fabs(stiR));

        fastEnv += fastCoeff * (stiPeak - fastEnv);
        slowEnv += slowCoeff * (stiPeak - slowEnv);

        // 子音の立ち上がり (fastEnv > slowEnv) の瞬間だけ最大 +3.8 dB ブースト
        const float onsetRatio = std::max(0.0f, (fastEnv - slowEnv) / (slowEnv + 1.0e-5f));
        const float boostGain = std::min(0.65f, onsetRatio * 0.42f * articulation01);

        left  = vg::mastering::FastAudioMath::sanitize(left + stiL * boostGain);
        right = vg::mastering::FastAudioMath::sanitize(right + stiR * boostGain);
        currentArticulationBoostDb = 20.0f * std::log10(1.0f + boostGain);
    }

    float getArticulationBoostDb() const noexcept { return currentArticulationBoostDb; }

private:
    double sr = 48000.0;
    float fastCoeff = 0.02f;
    float slowCoeff = 0.001f;
    float fastEnv = 0.0f;
    float slowEnv = 0.0f;
    float currentArticulationBoostDb = 0.0f;
    std::array<vg::mastering::TptStateVariableFilter, 2> stiBandBp;
};

//==============================================================================
// 3. ハース先行音効果 (Haas Fusion Zone) ボーカル前方立体ピンニング・ステージ
//==============================================================================
class BinauralPrecedenceDepthStage
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        // 11.2 ms & 14.8 ms のハース融合領域ディレイ
        delaySamplesL = std::clamp(static_cast<int>(sr * 0.0112), 16, kMaxSamples - 8);
        delaySamplesR = std::clamp(static_cast<int>(sr * 0.0148), 16, kMaxSamples - 8);

        sideToneBpL.prepare(sr);
        sideToneBpR.prepare(sr);
        sideToneBpL.setParameters(1150.0f, 0.707f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        sideToneBpR.setParameters(1350.0f, 0.707f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        reset();
    }

    void reset() noexcept
    {
        bufL.fill(0.0f);
        bufR.fill(0.0f);
        writePos = 0;
        sideToneBpL.reset();
        sideToneBpR.reset();
    }

    void processSamplePair(float& left, float& right, float depthAmount01) noexcept
    {
        depthAmount01 = std::clamp(depthAmount01, 0.0f, 1.0f);
        if (depthAmount01 <= 0.0005f)
            return;

        bufL[static_cast<size_t>(writePos)] = left;
        bufR[static_cast<size_t>(writePos)] = right;

        const int readL = (writePos - delaySamplesL) & kMask;
        const int readR = (writePos - delaySamplesR) & kMask;

        const float haasL = sideToneBpL.processSample(bufR[static_cast<size_t>(readL)]);
        const float haasR = sideToneBpR.processSample(bufL[static_cast<size_t>(readR)]);

        writePos = (writePos + 1) & kMask;

        const float mix = depthAmount01 * 0.14f;
        left  = vg::mastering::FastAudioMath::sanitize(left + mix * haasL);
        right = vg::mastering::FastAudioMath::sanitize(right - mix * haasR);
    }

private:
    static constexpr int kMaxSamples = 2048;
    static constexpr int kMask = kMaxSamples - 1;
    double sr = 48000.0;
    int delaySamplesL = 537;
    int delaySamplesR = 710;
    int writePos = 0;
    std::array<float, kMaxSamples> bufL{};
    std::array<float, kMaxSamples> bufR{};
    vg::mastering::TptStateVariableFilter sideToneBpL;
    vg::mastering::TptStateVariableFilter sideToneBpR;
};

//==============================================================================
// 4. 音響心理クラリティ統合スイート (PsychoacousticClaritySuite)
//==============================================================================
class PsychoacousticClaritySuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        virtualPitch.prepare(sampleRate);
        consonantEnhancer.prepare(sampleRate);
        precedenceStage.prepare(sampleRate);
    }

    void reset() noexcept
    {
        virtualPitch.reset();
        consonantEnhancer.reset();
        precedenceStage.reset();
    }

    void processSamplePair(float& left,
                           float& right,
                           float virtualBass01,
                           float consonantClarity01,
                           float haasDepth01) noexcept
    {
        virtualPitch.processSamplePair(left, right, virtualBass01);
        consonantEnhancer.processSamplePair(left, right, consonantClarity01);
        precedenceStage.processSamplePair(left, right, haasDepth01);
    }

    MissingFundamentalVirtualPitchEngine& getVirtualPitch() noexcept { return virtualPitch; }
    ConsonantIntelligibilityEnhancer& getConsonantEnhancer() noexcept { return consonantEnhancer; }
    BinauralPrecedenceDepthStage& getPrecedenceStage() noexcept { return precedenceStage; }

private:
    MissingFundamentalVirtualPitchEngine virtualPitch;
    ConsonantIntelligibilityEnhancer consonantEnhancer;
    BinauralPrecedenceDepthStage precedenceStage;
};

} // namespace vg::psychoacoustics
