#pragma once

/**
 * @file DynamicFormantAndRegisterEngine.h
 * @brief VocalGzzio パッサージョ声区ブリッジ・鼻腔ホンク動的ノッチ＆調波対雑音比 (HNR) 明瞭度ポリッシャー
 *
 * 【研究開発の背景と独自技術】
 * 1. PassaggioRegisterBridge (声区転換点・チェスト〜ヘッドボイス継目ゼロ・音響インピーダンス補間器):
 *    - 人間の歌声は Primo Passaggio (約 290Hz〜350Hz) および Secondo Passaggio (約 415Hz〜550Hz) を境に
 *      声帯振動モードが胸声 (甲状披裂筋主体) から頭声 (輪状甲状筋主体) へ切り替わり、
 *      急に声が細くなったり裏返りやすくなります。
 *    - 本モジュールは基本周波数 (F0) がパッサージョ帯域へ入った瞬間を検知し、
 *      失われやすい第1倍音ボディ (260Hz) と咽頭共鳴 (2.85kHz) をミリ秒単位で自動補強して
 *      地声からハイトーンまで継ぎ目のない「黄金のミックスボイス」を実現します。
 *
 * 2. DynamicNasalHonkEliminator (1040Hz 鼻腔反共振・メガホンっぽさ動的サプレッサー):
 *    - マイクに近づきすぎた時や鼻腔に声が抜けた際に発生する 960Hz〜1180Hz の「鼻づまり感・メガホン感 (Nasal Honk)」を、
 *      上下の隣接帯域 (620Hz / 1750Hz) とのエネルギー比からリアルタイム検出し、
 *      必要な瞬間だけ Q=3.8 の TPT ベルフィルタで最大 -4.8 dB 引き算します。
 *
 * 3. HarmonicToNoiseClarityPolisher (HNR 調波対雑音比ガイド付き・声門閉鎖ポリッシャー):
 *    - 声帯の閉鎖が甘く息漏れが多い箇所と、芯のある母音箇所のバランスを自動的に整え、
 *      かすれ声でもフロアノイズを増やさずに声の輪郭だけを際立たせます。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "MasteringProEngine.h"

namespace vg::register_science
{

//==============================================================================
// 1. パッサージョ声区ブリッジ (PassaggioRegisterBridge)
//==============================================================================
class PassaggioRegisterBridge
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        const float dt = static_cast<float>(1.0 / sr);
        bridgeSmoothCoeff = 1.0f - std::exp(-dt / 0.022f);

        for (int ch = 0; ch < 2; ++ch)
        {
            chestAnchorBell[ch].prepare(sr);
            passaggioWarmthBell[ch].prepare(sr);
            headRingBell[ch].prepare(sr);

            chestAnchorBell[ch].setParameters(255.0f, 0.90f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::PeakBell);
            passaggioWarmthBell[ch].setParameters(540.0f, 1.15f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::PeakBell);
            headRingBell[ch].setParameters(2880.0f, 1.25f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::PeakBell);
        }
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            chestAnchorBell[ch].reset();
            passaggioWarmthBell[ch].reset();
            headRingBell[ch].reset();
        }
        bridgeActivation01 = 0.0f;
        updateCounter = 0;
    }

    void processSamplePair(float& left,
                           float& right,
                           float trackedF0Hz,
                           bool isVoiced,
                           float bridgeAmount01) noexcept
    {
        bridgeAmount01 = std::clamp(bridgeAmount01, 0.0f, 1.0f);
        if (bridgeAmount01 <= 0.001f)
        {
            bridgeActivation01 = 0.0f;
            return;
        }

        float targetAct = 0.0f;
        if (isVoiced && trackedF0Hz >= 240.0f && trackedF0Hz <= 880.0f)
        {
            // 260Hz (C4) から立ち上がり、330Hz〜520Hz のパッサージョ〜ハイトーン領域で最大活性化
            targetAct = std::clamp((trackedF0Hz - 240.0f) / 220.0f, 0.0f, 1.0f);
        }

        bridgeActivation01 += bridgeSmoothCoeff * (targetAct - bridgeActivation01);
        const float activeBoost = bridgeActivation01 * bridgeAmount01;

        if ((++updateCounter & 15) == 0)
        {
            const float chestDb = +2.4f * activeBoost;
            const float warmDb  = +1.5f * activeBoost;
            const float ringDb  = +2.1f * activeBoost;
            for (int ch = 0; ch < 2; ++ch)
            {
                chestAnchorBell[ch].setDynamicGainDbFast(chestDb);
                passaggioWarmthBell[ch].setDynamicGainDbFast(warmDb);
                headRingBell[ch].setDynamicGainDbFast(ringDb);
            }
        }

        left  = headRingBell[0].processSample(passaggioWarmthBell[0].processSample(chestAnchorBell[0].processSample(left)));
        right = headRingBell[1].processSample(passaggioWarmthBell[1].processSample(chestAnchorBell[1].processSample(right)));
    }

    float getBridgeActivation01() const noexcept { return bridgeActivation01; }

private:
    double sr = 48000.0;
    float bridgeSmoothCoeff = 0.001f;
    float bridgeActivation01 = 0.0f;
    int updateCounter = 0;
    std::array<vg::mastering::TptStateVariableFilter, 2> chestAnchorBell;
    std::array<vg::mastering::TptStateVariableFilter, 2> passaggioWarmthBell;
    std::array<vg::mastering::TptStateVariableFilter, 2> headRingBell;
};

//==============================================================================
// 2. 1040 Hz 鼻腔反共振・メガホン感動的サプレッサー (DynamicNasalHonkEliminator)
//==============================================================================
class DynamicNasalHonkEliminator
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        const float dt = static_cast<float>(1.0 / sr);
        envAttack = 1.0f - std::exp(-dt / 0.0035f);
        envRelease = 1.0f - std::exp(-dt / 0.0420f);

        lowRefBp.prepare(sr);
        nasalTargetBp.prepare(sr);
        highRefBp.prepare(sr);

        lowRefBp.setParameters(610.0f, 1.3f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        nasalTargetBp.setParameters(1045.0f, 2.8f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        highRefBp.setParameters(1780.0f, 1.3f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);

        for (int ch = 0; ch < 2; ++ch)
        {
            nasalCutBell[ch].prepare(sr);
            nasalCutBell[ch].setParameters(1045.0f, 3.4f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::PeakBell);
        }
        reset();
    }

    void reset() noexcept
    {
        lowRefBp.reset();
        nasalTargetBp.reset();
        highRefBp.reset();
        for (int ch = 0; ch < 2; ++ch)
            nasalCutBell[ch].reset();

        eLow = 0.0f;
        eNasal = 0.0f;
        eHigh = 0.0f;
        currentCutDb = 0.0f;
        updateCounter = 0;
    }

    void processSamplePair(float& left, float& right, float sensitivity01) noexcept
    {
        sensitivity01 = std::clamp(sensitivity01, 0.0f, 1.0f);
        if (sensitivity01 <= 0.001f)
        {
            currentCutDb = 0.0f;
            return;
        }

        const float mono = 0.5f * (left + right);
        const float vLow = std::fabs(lowRefBp.processSample(mono));
        const float vNasal = std::fabs(nasalTargetBp.processSample(mono));
        const float vHigh = std::fabs(highRefBp.processSample(mono));

        eLow   += envRelease * (vLow - eLow);
        eNasal += ((vNasal > eNasal) ? envAttack : envRelease) * (vNasal - eNasal);
        eHigh  += envRelease * (vHigh - eHigh);

        const float flankAvg = 0.5f * (eLow + eHigh) + 1.0e-6f;
        const float honkRatio = eNasal / flankAvg;

        float targetCutDb = 0.0f;
        if (honkRatio > 1.28f && eNasal > 1.0e-4f)
        {
            targetCutDb = -std::min(4.8f, (honkRatio - 1.28f) * 3.8f * sensitivity01);
        }

        currentCutDb += 0.015f * (targetCutDb - currentCutDb);
        if ((++updateCounter & 15) == 0)
        {
            nasalCutBell[0].setDynamicGainDbFast(currentCutDb);
            nasalCutBell[1].setDynamicGainDbFast(currentCutDb);
        }

        left  = nasalCutBell[0].processSample(left);
        right = nasalCutBell[1].processSample(right);
    }

    float getCurrentCutDb() const noexcept { return currentCutDb; }

private:
    double sr = 48000.0;
    float envAttack = 0.01f;
    float envRelease = 0.001f;
    float eLow = 0.0f;
    float eNasal = 0.0f;
    float eHigh = 0.0f;
    float currentCutDb = 0.0f;
    int updateCounter = 0;

    vg::mastering::TptStateVariableFilter lowRefBp;
    vg::mastering::TptStateVariableFilter nasalTargetBp;
    vg::mastering::TptStateVariableFilter highRefBp;
    std::array<vg::mastering::TptStateVariableFilter, 2> nasalCutBell;
};

//==============================================================================
// 3. 調波対雑音比 (HNR) ガイド付き・声門閉鎖ポリッシャー
//==============================================================================
class HarmonicToNoiseClarityPolisher
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        harmonicCoreBp.prepare(sr);
        noiseFloorHp.prepare(sr);
        harmonicCoreBp.setParameters(1450.0f, 0.75f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        noiseFloorHp.setParameters(6800.0f, 0.707f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::Highpass);

        for (int ch = 0; ch < 2; ++ch)
        {
            clarityFocusBell[ch].prepare(sr);
            clarityFocusBell[ch].setParameters(2150.0f, 1.15f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::PeakBell);
        }
        reset();
    }

    void reset() noexcept
    {
        harmonicCoreBp.reset();
        noiseFloorHp.reset();
        for (int ch = 0; ch < 2; ++ch)
            clarityFocusBell[ch].reset();
        harmEnv = 0.0f;
        noiseEnv = 0.0f;
        estimatedHnrDb = 18.0f;
        updateCounter = 0;
    }

    void processSamplePair(float& left, float& right, float polishAmount01) noexcept
    {
        polishAmount01 = std::clamp(polishAmount01, 0.0f, 1.0f);
        const float mono = 0.5f * (left + right);
        const float hAbs = std::fabs(harmonicCoreBp.processSample(mono));
        const float nAbs = std::fabs(noiseFloorHp.processSample(mono));

        harmEnv  += 0.002f * (hAbs - harmEnv);
        noiseEnv += 0.002f * (nAbs - noiseEnv);

        if (harmEnv > 1.0e-5f)
        {
            const float instHnrDb = 20.0f * std::log10((harmEnv + 1.0e-6f) / (noiseEnv + 1.0e-6f));
            estimatedHnrDb += 0.005f * (std::clamp(instHnrDb, -6.0f, 36.0f) - estimatedHnrDb);
        }

        if (polishAmount01 <= 0.001f)
            return;

        // 調波成分がしっかり存在する有声区間でのみ 2.15kHz の声門閉鎖フォーカスを +1.8dB リフト
        const float voicedFocus01 = std::clamp((estimatedHnrDb - 4.0f) / 18.0f, 0.0f, 1.0f);
        const float boostDb = 1.85f * polishAmount01 * voicedFocus01;

        if ((++updateCounter & 15) == 0)
        {
            clarityFocusBell[0].setDynamicGainDbFast(boostDb);
            clarityFocusBell[1].setDynamicGainDbFast(boostDb);
        }

        left  = clarityFocusBell[0].processSample(left);
        right = clarityFocusBell[1].processSample(right);
    }

    float getEstimatedHnrDb() const noexcept { return estimatedHnrDb; }

private:
    double sr = 48000.0;
    float harmEnv = 0.0f;
    float noiseEnv = 0.0f;
    float estimatedHnrDb = 18.0f;
    int updateCounter = 0;
    vg::mastering::TptStateVariableFilter harmonicCoreBp;
    vg::mastering::TptStateVariableFilter noiseFloorHp;
    std::array<vg::mastering::TptStateVariableFilter, 2> clarityFocusBell;
};

//==============================================================================
// 4. パッサージョ＆フォルマント科学統合スイート (DynamicFormantAndRegisterSuite)
//==============================================================================
class DynamicFormantAndRegisterSuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        passaggioBridge.prepare(sampleRate);
        nasalEliminator.prepare(sampleRate);
        hnrPolisher.prepare(sampleRate);
    }

    void reset() noexcept
    {
        passaggioBridge.reset();
        nasalEliminator.reset();
        hnrPolisher.reset();
    }

    void processSamplePair(float& left,
                           float& right,
                           float trackedF0Hz,
                           bool isVoiced,
                           float bridgeAmount01,
                           float nasalTame01,
                           float hnrPolish01) noexcept
    {
        nasalEliminator.processSamplePair(left, right, nasalTame01);
        passaggioBridge.processSamplePair(left, right, trackedF0Hz, isVoiced, bridgeAmount01);
        hnrPolisher.processSamplePair(left, right, hnrPolish01);
    }

    PassaggioRegisterBridge& getPassaggioBridge() noexcept { return passaggioBridge; }
    const PassaggioRegisterBridge& getPassaggioBridge() const noexcept { return passaggioBridge; }

    DynamicNasalHonkEliminator& getNasalEliminator() noexcept { return nasalEliminator; }
    const DynamicNasalHonkEliminator& getNasalEliminator() const noexcept { return nasalEliminator; }

    HarmonicToNoiseClarityPolisher& getHnrPolisher() noexcept { return hnrPolisher; }
    const HarmonicToNoiseClarityPolisher& getHnrPolisher() const noexcept { return hnrPolisher; }

private:
    PassaggioRegisterBridge passaggioBridge;
    DynamicNasalHonkEliminator nasalEliminator;
    HarmonicToNoiseClarityPolisher hnrPolisher;
};

} // namespace vg::register_science
