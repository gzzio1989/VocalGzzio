#pragma once

// =============================================================================
//  VocalArticulationPolish.h — VocalGzzio v5.5.0
//  日本語モーラ母音（あいうえお）F1/F2自動バランサー・知能派ブレス音ベルベット整形・
//  マイク距離（近接効果）自動補正 ＆ 超高域シルク・シーン統合モジュール
// =============================================================================
//  【研究開発・設計背景】
//  日本のボーカルミックス（J-Pop / 歌ってみた / VTuber歌枠 / アニソン）および英語歌唱に
//  おいて、熟練のボーカルディレクターやミキシングエンジニアが手作業のフェーダーオートメーションと
//  ダイナミックEQで時間をかけて直している「4大歌唱課題」をリアルタイム自動化：
//
//  1. JapaneseVowelMoraBalancer (日本語母音 F1/F2 トライアングル自動均一化)
//     - 日本語の閉母音「い(/i/)」「う(/u/)」は開口面積が小さいため、開母音「あ(/a/)」「お(/o/)」
//       に比べて音響放射エネルギーが 3〜5dB 低く、伴奏の中で歌詞が聴き取りづらくなります。
//     - 一方で高音の「い(/i/)」「え(/e/)」は第2フォルマント (F2: 2.1kHz〜2.9kHz) が鋭く張り出し、
//       耳に刺さりやすい特性があります。
//     - 本クラスは F1 帯域 (320〜820Hz) と F2 帯域 (950〜2700Hz) のエネルギー重心から
//       開母音／閉母音／前舌母音をゼロ遅延で推定し、「い・う」の厚み補強と高音「い・え」の
//       角取りを全自動で行います。
//  2. VelvetBreathSculptor (息継ぎ・ブレス音のベルベット音楽的整形)
//     - コンプレッサーを強くかけると、フレーズ前の息継ぎ（ブレス）が不自然に大きく張り付き、
//       逆にノイズゲートを強くするとブレスがブツ切りになって不自然な機械声になります。
//     - 本クラスは息継ぎ区間だけを検出し、耳障りな 3.6kHz の気流摩擦ノイズを丸めつつ
//       音量を適正な音楽的バランスへソフトに着地させます。
//  3. DynamicProximityCompensator (マイク距離・近接効果リアルタイム自動スタビライザー)
//     - 配信や歌枠で頭が動いてマイクに近づきすぎた時の「150Hz以下の近接ボワつき急増」と、
//       サビでマイクから離れた時の「声の腰抜け」を、低域／中域スペクトル比の短期偏差から
//       自動補償します。
//  4. CondenserSilkSheenSynthesizer (ダイナミックマイクを高級真空管マイクの空気感へ変える超高域シーン)
//     - SM58 や SM7B 等のダイナミックマイクで不足しがちな 12kHz〜18kHz の空気感を、
//       歯擦音帯域（5〜8kHz）を一切刺激せずに倍音変調で補完します。
// =============================================================================

#include "MasteringProEngine.h"
#include "VocalScienceLab.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace gzzio
{
namespace articulation
{

using gzzio::mastering::FastAudioMath;
using gzzio::mastering::TptStateVariableFilter;

// =============================================================================
//  1. 日本語モーラ母音（あ・い・う・え・お）F1/F2 自動バランサー
// =============================================================================
class JapaneseVowelMoraBalancer
{
public:
    JapaneseVowelMoraBalancer() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);

        // 検出用フィルタ:
        // - ClosedLowF1 (280-440Hz: 「い」「う」の低い第1フォルマント)
        // - OpenHighF1  (620-900Hz: 「あ」「お」の高い第1フォルマント)
        // - FrontHighF2 (2100-2850Hz: 「い」「え」の前舌第2フォルマント)
        detectClosedF1.prepare(sampleRate);
        detectOpenF1.prepare(sampleRate);
        detectFrontF2.prepare(sampleRate);

        detectClosedF1.setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, 350.0f, 1.8f);
        detectOpenF1.setParameters(TptStateVariableFilter::Mode::UnitGainBandpass,   740.0f, 1.8f);
        detectFrontF2.setParameters(TptStateVariableFilter::Mode::UnitGainBandpass,  2450.0f, 2.2f);

        // 補正用ゼロ遅延 TPT Bell:
        // - 360Hz Body Lift (閉母音「い・う」で声が細くなるのを支える)
        // - 750Hz Open Tame (開母音「あ」で急に音量が飛び出すのを整える)
        // - 2500Hz Palatal Smooth (前舌母音「い・え」のキンつきを丸める)
        bodyLiftBell.prepare(sampleRate);
        openTameBell.prepare(sampleRate);
        palatalSmoothBell.prepare(sampleRate);

        bodyLiftBell.setParameters(TptStateVariableFilter::Mode::PeakBell,      360.0f,  1.25f, 0.0f);
        openTameBell.setParameters(TptStateVariableFilter::Mode::PeakBell,      750.0f,  1.35f, 0.0f);
        palatalSmoothBell.setParameters(TptStateVariableFilter::Mode::PeakBell, 2500.0f, 2.10f, 0.0f);

        envAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 4.5f);
        envRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 38.0f);
        reset();
    }

    void reset() noexcept
    {
        detectClosedF1.reset();
        detectOpenF1.reset();
        detectFrontF2.reset();
        bodyLiftBell.reset();
        openTameBell.reset();
        palatalSmoothBell.reset();

        closedF1Env = 1.0e-5f;
        openF1Env   = 1.0e-5f;
        frontF2Env  = 1.0e-5f;
        curBodyDb   = 0.0f;
        curOpenDb   = 0.0f;
        curPalatalDb= 0.0f;
        decimCounter = 0;
    }

    void setAmount(float amount01) noexcept
    {
        amount = std::clamp(amount01, 0.0f, 1.0f);
    }

    inline float processSample(float input) noexcept
    {
        if (amount <= 0.001f)
            return input;

        const float cAbs = std::fabs(detectClosedF1.processSample(input));
        const float oAbs = std::fabs(detectOpenF1.processSample(input));
        const float fAbs = std::fabs(detectFrontF2.processSample(input));

        closedF1Env += ((cAbs > closedF1Env) ? envAtt : envRel) * (cAbs - closedF1Env);
        openF1Env   += ((oAbs > openF1Env)   ? envAtt : envRel) * (oAbs - openF1Env);
        frontF2Env  += ((fAbs > frontF2Env)  ? envAtt : envRel) * (fAbs - frontF2Env);

        if ((++decimCounter & 15u) == 0u)
        {
            const float totalVowel = closedF1Env + openF1Env + 1.0e-5f;
            float targetBody = 0.0f;
            float targetOpen = 0.0f;
            float targetPal  = 0.0f;

            if (totalVowel > 0.003f)
            {
                // 閉母音比率が高い（「い」「う」発声中）-> 360Hz を最大 +2.2dB 補強
                const float closedDominance = std::clamp((closedF1Env - openF1Env * 0.8f) / totalVowel, 0.0f, 1.0f);
                targetBody = closedDominance * 2.2f * amount;

                // 開母音比率が極端に高い（「あ」強唱中）-> 750Hz の飛び出しを最大 -1.8dB 整える
                const float openDominance = std::clamp((openF1Env - closedF1Env * 1.1f) / totalVowel, 0.0f, 1.0f);
                targetOpen = -openDominance * 1.8f * amount;

                // 前舌母音 F2 が突出（「い」「え」高音）-> 2.5kHz を最大 -2.5dB 滑らかに抑制
                const float palatalRatio = frontF2Env / totalVowel;
                if (palatalRatio > 0.55f)
                {
                    const float ex = std::min(1.5f, palatalRatio - 0.55f);
                    targetPal = -ex * 1.7f * amount;
                }
            }

            curBodyDb    += 0.20f * (targetBody - curBodyDb);
            curOpenDb    += 0.20f * (targetOpen - curOpenDb);
            curPalatalDb += 0.20f * (targetPal  - curPalatalDb);

            bodyLiftBell.setDynamicGainDbFast(curBodyDb);
            openTameBell.setDynamicGainDbFast(curOpenDb);
            palatalSmoothBell.setDynamicGainDbFast(curPalatalDb);
        }

        float y = bodyLiftBell.processSample(input);
        y = openTameBell.processSample(y);
        y = palatalSmoothBell.processSample(y);
        return FastAudioMath::sanitize(y);
    }

private:
    double sampleRate = 48000.0;
    float amount = 0.45f;

    TptStateVariableFilter detectClosedF1;
    TptStateVariableFilter detectOpenF1;
    TptStateVariableFilter detectFrontF2;

    TptStateVariableFilter bodyLiftBell;
    TptStateVariableFilter openTameBell;
    TptStateVariableFilter palatalSmoothBell;

    float closedF1Env = 1.0e-5f;
    float openF1Env   = 1.0e-5f;
    float frontF2Env  = 1.0e-5f;

    float curBodyDb    = 0.0f;
    float curOpenDb    = 0.0f;
    float curPalatalDb = 0.0f;
    float envAtt = 0.02f;
    float envRel = 0.003f;
    uint32_t decimCounter = 0;
};

// =============================================================================
//  2. 知能派ブレス（息継ぎ）ベルベット整形器 (Velvet Breath Sculptor)
// =============================================================================
class VelvetBreathSculptor
{
public:
    VelvetBreathSculptor() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        breathTurbulenceBell.prepare(sampleRate);
        breathTurbulenceBell.setParameters(TptStateVariableFilter::Mode::PeakBell, 3650.0f, 1.6f, 0.0f);
        smoothCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, 10.0f);
        reset();
    }

    void reset() noexcept
    {
        breathTurbulenceBell.reset();
        smoothedBreathProb = 0.0f;
        decimCounter = 0;
    }

    void setControlAmount(float amount01) noexcept
    {
        amount = std::clamp(amount01, 0.0f, 1.0f);
    }

    inline float processSample(float input, float breathProbability01) noexcept
    {
        if (amount <= 0.001f)
            return input;

        smoothedBreathProb += smoothCoeff * (std::clamp(breathProbability01, 0.0f, 1.0f) - smoothedBreathProb);

        if ((++decimCounter & 15u) == 0u)
        {
            // ブレス区間では気流の擦過音（3.65kHz）を最大 -3.5dB 丸めてベルベットのような息遣いにする
            const float dipDb = -smoothedBreathProb * 3.5f * amount;
            breathTurbulenceBell.setDynamicGainDbFast(dipDb);
        }

        const float filtered = breathTurbulenceBell.processSample(input);
        const float levelScale = 1.0f - smoothedBreathProb * 0.28f * amount;
        return FastAudioMath::sanitize(filtered * levelScale);
    }

private:
    double sampleRate = 48000.0;
    float amount = 0.45f;
    TptStateVariableFilter breathTurbulenceBell;
    float smoothedBreathProb = 0.0f;
    float smoothCoeff = 0.01f;
    uint32_t decimCounter = 0;
};

// =============================================================================
//  3. マイク距離変動・近接効果リアルタイム自動スタビライザー
// =============================================================================
class DynamicProximityCompensator
{
public:
    DynamicProximityCompensator() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        proximityBassLp.prepare(sampleRate);
        vocalRefMidBp.prepare(sampleRate);
        correctiveLowShelf.prepare(sampleRate);

        proximityBassLp.setParameters(TptStateVariableFilter::Mode::Lowpass, 155.0f, 0.7071f);
        vocalRefMidBp.setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, 680.0f, 0.85f);
        correctiveLowShelf.setParameters(TptStateVariableFilter::Mode::LowShelf, 175.0f, 0.65f, 0.0f);

        envAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 18.0f);
        envRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 120.0f);
        reset();
    }

    void reset() noexcept
    {
        proximityBassLp.reset();
        vocalRefMidBp.reset();
        correctiveLowShelf.reset();
        bassEnv = 1.0e-5f;
        midEnv  = 1.0e-5f;
        currentShelfDb = 0.0f;
        decimCounter = 0;
    }

    void setStabilizerAmount(float amount01) noexcept
    {
        amount = std::clamp(amount01, 0.0f, 1.0f);
    }

    inline float processSample(float input) noexcept
    {
        if (amount <= 0.001f)
            return input;

        const float bAbs = std::fabs(proximityBassLp.processSample(input));
        const float mAbs = std::fabs(vocalRefMidBp.processSample(input));

        bassEnv += ((bAbs > bassEnv) ? envAtt : envRel) * (bAbs - bassEnv);
        midEnv  += ((mAbs > midEnv)  ? envAtt : envRel) * (mAbs - midEnv);

        if ((++decimCounter & 15u) == 0u)
        {
            float targetShelfDb = 0.0f;
            if (bassEnv + midEnv > 0.004f)
            {
                // 標準的な低域/中域バランス比 (0.85) からのズレを算出
                const float ratioDb = FastAudioMath::linearToDb((bassEnv + 1.0e-5f) / (midEnv * 0.85f + 1.0e-5f));
                // 近接しすぎ (ratioDb > 0) なら最大 -4.5dB カット、離れすぎ (ratioDb < 0) なら最大 +2.4dB 補強
                targetShelfDb = std::clamp(-ratioDb * 0.42f * amount, -4.5f, 2.4f);
            }
            currentShelfDb += 0.16f * (targetShelfDb - currentShelfDb);
            correctiveLowShelf.setDynamicGainDbFast(currentShelfDb);
        }

        return FastAudioMath::sanitize(correctiveLowShelf.processSample(input));
    }

    inline float getProximityCorrectionDb() const noexcept { return currentShelfDb; }

private:
    double sampleRate = 48000.0;
    float amount = 0.45f;
    TptStateVariableFilter proximityBassLp;
    TptStateVariableFilter vocalRefMidBp;
    TptStateVariableFilter correctiveLowShelf;

    float bassEnv = 1.0e-5f;
    float midEnv  = 1.0e-5f;
    float currentShelfDb = 0.0f;
    float envAtt = 0.01f;
    float envRel = 0.001f;
    uint32_t decimCounter = 0;
};

// =============================================================================
//  4. 超高域コンデンサー・シルク・シーン生成器 (Condenser Silk Sheen Synthesizer)
// =============================================================================
class CondenserSilkSheenSynthesizer
{
public:
    CondenserSilkSheenSynthesizer() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        sourceBandBp.prepare(sampleRate);
        sheenHighpass.prepare(sampleRate);

        // 歯擦音(5-8kHz)を避け、2.8kHz〜4.5kHz の母音高次倍音から 12kHz 以上の絹のような空気感を生成
        sourceBandBp.setParameters(TptStateVariableFilter::Mode::UnitGainBandpass, 3600.0f, 1.35f);
        sheenHighpass.setParameters(TptStateVariableFilter::Mode::Highpass, 11800.0f, 0.7071f);
        reset();
    }

    void reset() noexcept
    {
        sourceBandBp.reset();
        sheenHighpass.reset();
    }

    void setSheenAmount(float amount01) noexcept
    {
        sheenAmount = std::clamp(amount01, 0.0f, 1.0f);
    }

    inline float processSample(float input, float sibilantProb01 = 0.0f) noexcept
    {
        if (sheenAmount <= 0.001f)
            return input;

        const float src = sourceBandBp.processSample(input);
        // ソフト2次・3次倍音変調で超高域成分を生成し、11.8kHz ハイパスで可聴中域への干渉を遮断
        const double x = static_cast<double>(src) * 2.4;
        const float excited = static_cast<float>(FastAudioMath::smoothTanh(x) - 0.35 * x);
        const float airySheen = sheenHighpass.processSample(excited);

        const float sibilantDuck = 1.0f - 0.85f * std::clamp(sibilantProb01, 0.0f, 1.0f);
        return FastAudioMath::sanitize(input + airySheen * (sheenAmount * 0.24f * sibilantDuck));
    }

private:
    double sampleRate = 48000.0;
    float sheenAmount = 0.35f;
    TptStateVariableFilter sourceBandBp;
    TptStateVariableFilter sheenHighpass;
};

// =============================================================================
//  5. VocalArticulationPolishSuite — 歌唱アーティキュレーション統合ポリッシャー
// =============================================================================
class VocalArticulationPolishSuite
{
public:
    VocalArticulationPolishSuite() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            proximityComp[ch].prepare(sampleRate);
            moraBalancer[ch].prepare(sampleRate);
            breathSculptor[ch].prepare(sampleRate);
            silkSheen[ch].prepare(sampleRate);
        }
    }

    void reset() noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            proximityComp[ch].reset();
            moraBalancer[ch].reset();
            breathSculptor[ch].reset();
            silkSheen[ch].reset();
        }
    }

    void setPolishParameters(float proximityStabilize01,
                             float moraVowelBalance01,
                             float velvetBreath01,
                             float condenserSheen01) noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            proximityComp[ch].setStabilizerAmount(proximityStabilize01);
            moraBalancer[ch].setAmount(moraVowelBalance01);
            breathSculptor[ch].setControlAmount(velvetBreath01);
            silkSheen[ch].setSheenAmount(condenserSheen01);
        }
    }

    inline void processStereoBlock(float* left,
                                   float* right,
                                   int numSamples,
                                   float breathProb01 = 0.0f,
                                   float sibilantProb01 = 0.0f) noexcept
    {
        if (numSamples <= 0 || left == nullptr)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            float l = proximityComp[0].processSample(left[i]);
            l = moraBalancer[0].processSample(l);
            l = breathSculptor[0].processSample(l, breathProb01);
            l = silkSheen[0].processSample(l, sibilantProb01);
            left[i] = l;

            if (right != nullptr)
            {
                float r = proximityComp[1].processSample(right[i]);
                r = moraBalancer[1].processSample(r);
                r = breathSculptor[1].processSample(r, breathProb01);
                r = silkSheen[1].processSample(r, sibilantProb01);
                right[i] = r;
            }
        }
    }

private:
    double sampleRate = 48000.0;
    std::array<DynamicProximityCompensator, 2> proximityComp;
    std::array<JapaneseVowelMoraBalancer, 2> moraBalancer;
    std::array<VelvetBreathSculptor, 2> breathSculptor;
    std::array<CondenserSilkSheenSynthesizer, 2> silkSheen;
};

} // namespace articulation
} // namespace gzzio
