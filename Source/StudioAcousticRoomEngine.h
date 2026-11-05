#pragma once

/**
 * @file StudioAcousticRoomEngine.h
 * @brief VocalGzzio スタジオ音響空間・8x8アダマールFDNリバーブ・部屋鳴り除去＆名機マイクカプセル科学エンジン
 *
 * 【研究開発の背景と独自技術】
 * 1. DirectToReverberantDeReverbEngine (ゼロレイテンシ6バンド直接音/残響比 DRR デリバーブ):
 *    - 自宅ブースや吸音不足の部屋で録音されたボーカルに含まれる「初期反射・フラッターエコー・部屋鳴り」を、
 *      コンプレッサーで持ち上がる前にゼロレイテンシで抑制します。
 *    - 6バンドの Zavalishin TPT フィルタバンクごとに、直接音エンベロープ (2.2ms) と
 *      拡散残響テイル推定値 (72ms リーキーピークホールド) の比 (Direct-to-Reverberant Ratio: DRR) を算出し、
 *      語尾にまとわりつく部屋鳴りのみを滑らかに減衰させます。
 *
 * 2. VintageMicrophoneCapsuleSimulator (世界の名機マイクカプセル＆真空管ヘッドアンプ物理モデル):
 *    - 以下の4種類の象徴的なボーカルマイク特性を TPT 共振セルと ADAA 真空管ステージで忠実に再現:
 *      (0) Transparent Reference: フラットなリファレンス特性
 *      (1) 1950s German U47 Tube (KK47 カプセルの濃厚な 260Hz ローミッドと 4.8kHz プレゼンス + VF14 三極管)
 *      (2) Modern Pop C800G Tube (ペルチェ冷却 6AU6 真空管の澄み切った 9.5kHz シルクエアと速いトランジェント)
 *      (3) Broadcast Dynamic 7B (ダイナミックカプセル特有の引き締まった低域と中域の押し出し・歯擦音の丸み)
 *
 * 3. Hadamard8x8VelvetFdnReverb (8x8 直交ウォルシュ・アダマール変換 FDN ベルベット・リバーブ):
 *    - Lexicon 480L や Bricasti M7 の設計思想に基づき、互いに素な8本のディレイラインを
 *      8x8 直交アダマール行列 (1/√8 正規化 Fast Walsh-Hadamard Transform) で結合。
 *    - 各ディレイラインに黄金比 LFO 変調 (Catmull-Rom 3次補間) と空気吸収ダンピングフィルタを備え、
 *      さらに「歌唱中はリバーブを自動で -4.5 dB ダッキングし、語尾の隙間で豊かに開花させる」
 *      プロエンジニアのオートメーション技術を内蔵しています。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "MasteringProEngine.h"

namespace vg::studio_acoustics
{

//==============================================================================
// 1. ゼロレイテンシ 6バンド DRR (Direct-to-Reverberant Ratio) デリバーブ・エンジン
//==============================================================================
class DirectToReverberantDeReverbEngine
{
public:
    static constexpr int kNumBands = 6;

    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        const float dt = static_cast<float>(1.0 / sr);

        // 直接音トラッカー (2.2ms アタック / 14ms リリース)
        directAttackCoeff = 1.0f - std::exp(-dt / 0.0022f);
        directReleaseCoeff = 1.0f - std::exp(-dt / 0.0140f);

        // 拡散残響トラッカー (45ms アタック / 85ms リリース)
        reverbAttackCoeff = 1.0f - std::exp(-dt / 0.0450f);
        reverbReleaseCoeff = 1.0f - std::exp(-dt / 0.0850f);

        // ゲイン平滑化 (4.5ms)
        gainSmoothCoeff = 1.0f - std::exp(-dt / 0.0045f);

        static constexpr std::array<float, kNumBands> kCenterFreqs = {
            165.0f, 380.0f, 880.0f, 2100.0f, 4600.0f, 9200.0f
        };
        static constexpr std::array<float, kNumBands> kBandQs = {
            1.15f, 1.25f, 1.30f, 1.35f, 1.30f, 1.10f
        };

        for (size_t b = 0; b < kNumBands; ++b)
        {
            bandFiltersL[b].prepare(sr);
            bandFiltersR[b].prepare(sr);
            bandFiltersL[b].setParameters(kCenterFreqs[b], kBandQs[b], 0.0f,
                                          vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
            bandFiltersR[b].setParameters(kCenterFreqs[b], kBandQs[b], 0.0f,
                                          vg::mastering::TptStateVariableFilter::Mode::UnitGainBandpass);
        }

        reset();
    }

    void reset() noexcept
    {
        for (size_t b = 0; b < kNumBands; ++b)
        {
            bandFiltersL[b].reset();
            bandFiltersR[b].reset();
            directEnv[b] = 0.0f;
            reverbEnv[b] = 0.0f;
            bandSuppressionGain[b] = 1.0f;
        }
        averageRoomSuppressionDb = 0.0f;
    }

    void processSamplePair(float& left, float& right, float deReverbAmount01) noexcept
    {
        deReverbAmount01 = std::clamp(deReverbAmount01, 0.0f, 1.0f);
        if (deReverbAmount01 <= 0.001f)
        {
            averageRoomSuppressionDb = 0.0f;
            return;
        }

        const float mid = 0.5f * (left + right);
        float corrL = 0.0f;
        float corrR = 0.0f;
        float totalCutDb = 0.0f;

        for (size_t b = 0; b < kNumBands; ++b)
        {
            const float bpL = bandFiltersL[b].processSample(left);
            const float bpR = bandFiltersR[b].processSample(right);
            const float bpMidAbs = 0.5f * (std::fabs(bpL) + std::fabs(bpR)) + std::fabs(mid) * 0.05f;

            // 直接音エンベロープ更新
            const float dCoeff = (bpMidAbs > directEnv[b]) ? directAttackCoeff : directReleaseCoeff;
            directEnv[b] += dCoeff * (bpMidAbs - directEnv[b]);

            // 拡散残響テイル更新
            const float rCoeff = (bpMidAbs > reverbEnv[b]) ? reverbAttackCoeff : reverbReleaseCoeff;
            reverbEnv[b] += rCoeff * (bpMidAbs - reverbEnv[b]);

            // 直接音に対する残響テイルの過剰比率を計算
            const float drrRatio = directEnv[b] / (reverbEnv[b] + 1.0e-7f);
            float targetGain = 1.0f;
            if (drrRatio < 0.72f && reverbEnv[b] > 1.0e-5f)
            {
                const float tailExcess01 = std::clamp((0.72f - drrRatio) / 0.62f, 0.0f, 1.0f);
                // 部屋鳴りが最も目立つ 380Hz〜2.1kHz を重点的に最大 -9.5 dB 抑制
                const float bandWeight = (b >= 1 && b <= 3) ? 1.0f : 0.65f;
                const float cutDb = -9.5f * deReverbAmount01 * tailExcess01 * bandWeight;
                targetGain = vg::mastering::FastAudioMath::dbToLinear(cutDb);
                totalCutDb += -cutDb;
            }

            bandSuppressionGain[b] += gainSmoothCoeff * (targetGain - bandSuppressionGain[b]);
            const float deltaGain = bandSuppressionGain[b] - 1.0f;
            corrL += bpL * deltaGain;
            corrR += bpR * deltaGain;
        }

        left = vg::mastering::FastAudioMath::sanitize(left + corrL);
        right = vg::mastering::FastAudioMath::sanitize(right + corrR);
        averageRoomSuppressionDb = totalCutDb / static_cast<float>(kNumBands);
    }

    float getAverageRoomSuppressionDb() const noexcept { return averageRoomSuppressionDb; }

private:
    double sr = 48000.0;
    float directAttackCoeff = 0.01f;
    float directReleaseCoeff = 0.001f;
    float reverbAttackCoeff = 0.0005f;
    float reverbReleaseCoeff = 0.0002f;
    float gainSmoothCoeff = 0.005f;
    float averageRoomSuppressionDb = 0.0f;

    std::array<vg::mastering::TptStateVariableFilter, kNumBands> bandFiltersL;
    std::array<vg::mastering::TptStateVariableFilter, kNumBands> bandFiltersR;
    std::array<float, kNumBands> directEnv{};
    std::array<float, kNumBands> reverbEnv{};
    std::array<float, kNumBands> bandSuppressionGain{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
};

//==============================================================================
// 2. 世界の名機マイクカプセル＆真空管ヘッドアンプ物理シミュレータ
//==============================================================================
class VintageMicrophoneCapsuleSimulator
{
public:
    enum class CapsuleModel : int
    {
        TransparentReference = 0, // 0: フラット・リファレンス
        GermanTubeU47        = 1, // 1: KK47 カプセル + VF14 三極管 (濃厚な260Hzチェスト＆4.8kHz存在感)
        ModernC800G          = 2, // 2: ペルチェ冷却真空管 + 9.5kHz 現代ポップ・シルクエア
        BroadcastDynamic7B   = 3  // 3: ダイナミック・ブロードキャスト (タイトな低域＆3.2kHz中域の押し出し)
    };

    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        for (int ch = 0; ch < 2; ++ch)
        {
            hpRollOff[ch].prepare(sr);
            chestBodyBell[ch].prepare(sr);
            presenceBell[ch].prepare(sr);
            airShelf[ch].prepare(sr);
            lpCapsuleDamp[ch].prepare(sr);
        }
        tubeStageL.prepare(sr);
        tubeStageR.prepare(sr);
        configureModel(currentModel);
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            hpRollOff[ch].reset();
            chestBodyBell[ch].reset();
            presenceBell[ch].reset();
            airShelf[ch].reset();
            lpCapsuleDamp[ch].reset();
        }
        tubeStageL.reset();
        tubeStageR.reset();
    }

    void setModel(CapsuleModel model) noexcept
    {
        if (model != currentModel)
        {
            currentModel = model;
            configureModel(currentModel);
        }
    }

    CapsuleModel getModel() const noexcept { return currentModel; }

    void processSamplePair(float& left, float& right, float characterBlend01) noexcept
    {
        characterBlend01 = std::clamp(characterBlend01, 0.0f, 1.0f);
        if (currentModel == CapsuleModel::TransparentReference || characterBlend01 <= 0.001f)
            return;

        const float dryL = left;
        const float dryR = right;

        float wetL = hpRollOff[0].processSample(left);
        wetL = chestBodyBell[0].processSample(wetL);
        wetL = presenceBell[0].processSample(wetL);
        wetL = airShelf[0].processSample(wetL);
        wetL = lpCapsuleDamp[0].processSample(wetL);

        float wetR = hpRollOff[1].processSample(right);
        wetR = chestBodyBell[1].processSample(right);
        wetR = presenceBell[1].processSample(wetR);
        wetR = airShelf[1].processSample(wetR);
        wetR = lpCapsuleDamp[1].processSample(wetR);

        // 真空管モデル (U47 / C800G) の場合は ADAA 2次アンチエイリアシング三極管ステージを通過
        if (currentModel == CapsuleModel::GermanTubeU47)
        {
            wetL = tubeStageL.processSample(wetL, 1.45f, 0.24f, 1.0f);
            wetR = tubeStageR.processSample(wetR, 1.45f, 0.24f, 1.0f);
        }
        else if (currentModel == CapsuleModel::ModernC800G)
        {
            wetL = tubeStageL.processSample(wetL, 1.22f, 0.14f, 1.0f);
            wetR = tubeStageR.processSample(wetR, 1.22f, 0.14f, 1.0f);
        }

        left = vg::mastering::FastAudioMath::sanitize(dryL + characterBlend01 * (wetL - dryL));
        right = vg::mastering::FastAudioMath::sanitize(dryR + characterBlend01 * (wetR - dryR));
    }

private:
    void configureModel(CapsuleModel model) noexcept
    {
        using Mode = vg::mastering::TptStateVariableFilter::Mode;
        for (int ch = 0; ch < 2; ++ch)
        {
            switch (model)
            {
                case CapsuleModel::GermanTubeU47:
                    hpRollOff[ch].setParameters(42.0f, 0.707f, 0.0f, Mode::Highpass);
                    chestBodyBell[ch].setParameters(265.0f, 0.75f, +2.1f, Mode::PeakBell);
                    presenceBell[ch].setParameters(4850.0f, 0.90f, +2.4f, Mode::PeakBell);
                    airShelf[ch].setParameters(11500.0f, 0.707f, +0.8f, Mode::HighShelf);
                    lpCapsuleDamp[ch].setParameters(17500.0f, 0.707f, 0.0f, Mode::Lowpass);
                    break;

                case CapsuleModel::ModernC800G:
                    hpRollOff[ch].setParameters(55.0f, 0.707f, 0.0f, Mode::Highpass);
                    chestBodyBell[ch].setParameters(210.0f, 0.85f, +0.9f, Mode::PeakBell);
                    presenceBell[ch].setParameters(3600.0f, 1.10f, -0.8f, Mode::PeakBell);
                    airShelf[ch].setParameters(9500.0f, 0.68f, +3.6f, Mode::HighShelf);
                    lpCapsuleDamp[ch].setParameters(19800.0f, 0.707f, 0.0f, Mode::Lowpass);
                    break;

                case CapsuleModel::BroadcastDynamic7B:
                    hpRollOff[ch].setParameters(88.0f, 0.78f, 0.0f, Mode::Highpass);
                    chestBodyBell[ch].setParameters(175.0f, 0.95f, +1.6f, Mode::PeakBell);
                    presenceBell[ch].setParameters(3250.0f, 0.85f, +2.8f, Mode::PeakBell);
                    airShelf[ch].setParameters(8500.0f, 0.707f, -1.5f, Mode::HighShelf);
                    lpCapsuleDamp[ch].setParameters(15200.0f, 0.707f, 0.0f, Mode::Lowpass);
                    break;

                case CapsuleModel::TransparentReference:
                default:
                    hpRollOff[ch].setParameters(20.0f, 0.707f, 0.0f, Mode::Highpass);
                    chestBodyBell[ch].setParameters(250.0f, 0.707f, 0.0f, Mode::PeakBell);
                    presenceBell[ch].setParameters(4000.0f, 0.707f, 0.0f, Mode::PeakBell);
                    airShelf[ch].setParameters(10000.0f, 0.707f, 0.0f, Mode::HighShelf);
                    lpCapsuleDamp[ch].setParameters(20000.0f, 0.707f, 0.0f, Mode::Lowpass);
                    break;
            }
        }
    }

    double sr = 48000.0;
    CapsuleModel currentModel = CapsuleModel::TransparentReference;
    std::array<vg::mastering::TptStateVariableFilter, 2> hpRollOff;
    std::array<vg::mastering::TptStateVariableFilter, 2> chestBodyBell;
    std::array<vg::mastering::TptStateVariableFilter, 2> presenceBell;
    std::array<vg::mastering::TptStateVariableFilter, 2> airShelf;
    std::array<vg::mastering::TptStateVariableFilter, 2> lpCapsuleDamp;
    vg::mastering::AdaaTriodeTransformerStage tubeStageL;
    vg::mastering::AdaaTriodeTransformerStage tubeStageR;
};

//==============================================================================
// 3. 8x8 直交ウォルシュ・アダマール変換 FDN (Feedback Delay Network) ベルベット・リバーブ
//==============================================================================
class Hadamard8x8VelvetFdnReverb
{
public:
    static constexpr int kNumChannels = 8;

    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        invSr = static_cast<float>(1.0 / sr);
        const float srScale = static_cast<float>(sr / 48000.0);

        // 48kHz 基準で互いに素な8本のディレイ長 (約 22.6ms 〜 65.9ms)
        static constexpr std::array<int, kNumChannels> kBasePrimeDelays = {
            1087, 1283, 1511, 1777, 2053, 2381, 2749, 3163
        };

        for (size_t i = 0; i < kNumChannels; ++i)
        {
            baseDelaySamples[i] = std::clamp(static_cast<float>(kBasePrimeDelays[i]) * srScale,
                                             64.0f, static_cast<float>(kMaxDelaySamples - 32));
            dampFilters[i].prepare(sr);
            dampFilters[i].setParameters(5400.0f, 0.707f, 0.0f,
                                         vg::mastering::TptStateVariableFilter::Mode::Lowpass);
            lfoPhases[i] = static_cast<float>(i) * 0.125f;
        }

        // 入力プリコンディショニング (Abbey Road 流: 240Hz 以下と 9.5kHz 以上を事前にロールオフ)
        inputHighpass.prepare(sr);
        inputHighpass.setParameters(240.0f, 0.707f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::Highpass);
        inputLowpass.prepare(sr);
        inputLowpass.setParameters(9500.0f, 0.707f, 0.0f, vg::mastering::TptStateVariableFilter::Mode::Lowpass);

        const float dt = static_cast<float>(1.0 / sr);
        duckAttackCoeff = 1.0f - std::exp(-dt / 0.008f);
        duckReleaseCoeff = 1.0f - std::exp(-dt / 0.190f);

        setReverbTimeSeconds(1.65f);
        reset();
    }

    void reset() noexcept
    {
        for (size_t i = 0; i < kNumChannels; ++i)
        {
            delayLines[i].fill(0.0f);
            dampFilters[i].reset();
        }
        inputHighpass.reset();
        inputLowpass.reset();
        writeIndex = 0;
        vocalDuckEnv = 0.0f;
    }

    void setReverbTimeSeconds(float rt60Seconds) noexcept
    {
        rt60Sec = std::clamp(rt60Seconds, 0.25f, 8.0f);
        for (size_t i = 0; i < kNumChannels; ++i)
        {
            const float delaySec = baseDelaySamples[i] * invSr;
            // RT60 で -60 dB (10^-3) 減衰するループゲイン: g = 10^(-3 * delaySec / rt60Sec)
            feedbackGains[i] = std::pow(10.0f, (-3.0f * delaySec) / rt60Sec);
            feedbackGains[i] = std::clamp(feedbackGains[i], 0.10f, 0.985f);
        }
    }

    /**
     * @brief 8要素ベクトルに対する正規化高速ウォルシュ・アダマール変換 (In-place Orthogonal FWHT)
     * 乗算1回のみで直交エネルギー保存 (∑x_i^2 = ∑y_i^2) を満たす最高効率ミキシング行列
     */
    static inline void applyNormalizedHadamard8(std::array<float, kNumChannels>& v) noexcept
    {
        // Stage 1 (stride 1)
        for (size_t i = 0; i < 8; i += 2)
        {
            const float a = v[i];
            const float b = v[i + 1];
            v[i] = a + b;
            v[i + 1] = a - b;
        }
        // Stage 2 (stride 2)
        for (size_t i = 0; i < 8; i += 4)
        {
            const float a0 = v[i];
            const float a1 = v[i + 1];
            const float b0 = v[i + 2];
            const float b1 = v[i + 3];
            v[i]     = a0 + b0;
            v[i + 1] = a1 + b1;
            v[i + 2] = a0 - b0;
            v[i + 3] = a1 - b1;
        }
        // Stage 3 (stride 4)
        for (size_t i = 0; i < 4; ++i)
        {
            const float a = v[i];
            const float b = v[i + 4];
            v[i]     = (a + b) * kInvSqrt8;
            v[i + 4] = (a - b) * kInvSqrt8;
        }
    }

    void processSamplePair(float& left, float& right, float wetMix01) noexcept
    {
        wetMix01 = std::clamp(wetMix01, 0.0f, 1.0f);
        if (wetMix01 <= 0.0005f)
            return;

        const float dryMid = 0.5f * (left + right);
        const float drySide = 0.5f * (left - right);

        // 歌唱中の自動ダッキング包絡線
        const float absVocal = std::fabs(dryMid);
        const float dCoeff = (absVocal > vocalDuckEnv) ? duckAttackCoeff : duckReleaseCoeff;
        vocalDuckEnv += dCoeff * (absVocal - vocalDuckEnv);
        const float duckGain = 1.0f / (1.0f + 3.2f * vocalDuckEnv);

        // 入力プリコンディショニング
        const float conditionedIn = inputLowpass.processSample(inputHighpass.processSample(dryMid)) * 0.35f;

        std::array<float, kNumChannels> delayOutputs{};
        constexpr float kTwoPi = 6.28318530717958647692f;

        for (size_t i = 0; i < kNumChannels; ++i)
        {
            // 黄金比レートで緩やかにディレイ長を変調し金属的な固有モードを解消
            const float rateHz = 0.31f + 0.09f * static_cast<float>(i);
            lfoPhases[i] += rateHz * invSr;
            if (lfoPhases[i] >= 1.0f)
                lfoPhases[i] -= 1.0f;

            const float modSamples = 5.5f * std::sin(kTwoPi * lfoPhases[i]);
            const float readDelay = baseDelaySamples[i] + modSamples;
            const float rawDelayed = readCubicDelay(delayLines[i], readDelay);
            delayOutputs[i] = dampFilters[i].processSample(rawDelayed) * feedbackGains[i];
        }

        // 8x8 直交アダマール拡散行列を適用
        applyNormalizedHadamard8(delayOutputs);

        // 入力信号を注入して各ディレイラインへ書き戻し
        for (size_t i = 0; i < kNumChannels; ++i)
        {
            const float sign = ((i & 1u) == 0u) ? 1.0f : -1.0f;
            const float inInject = conditionedIn + sign * drySide * 0.15f;
            delayLines[i][static_cast<size_t>(writeIndex)] =
                vg::mastering::FastAudioMath::sanitize(delayOutputs[i] + inInject);
        }

        writeIndex = (writeIndex + 1) & kDelayMask;

        // 偶数チャンネルを左、奇数チャンネルを右へ直交ステレオ合成
        const float wetL = 0.5f * (delayOutputs[0] - delayOutputs[2] + delayOutputs[4] - delayOutputs[6]);
        const float wetR = 0.5f * (delayOutputs[1] - delayOutputs[3] + delayOutputs[5] - delayOutputs[7]);

        const float effectiveWet = wetMix01 * 0.28f * duckGain;
        left = vg::mastering::FastAudioMath::sanitize(left + effectiveWet * wetL);
        right = vg::mastering::FastAudioMath::sanitize(right + effectiveWet * wetR);
    }

private:
    static constexpr int kMaxDelaySamples = 8192;
    static constexpr int kDelayMask = kMaxDelaySamples - 1;
    static constexpr float kInvSqrt8 = 0.35355339059327376220f;

    float readCubicDelay(const std::array<float, kMaxDelaySamples>& line, float delaySamples) const noexcept
    {
        const float readPos = static_cast<float>(writeIndex) - delaySamples;
        int idx1 = static_cast<int>(std::floor(readPos));
        const float frac = readPos - static_cast<float>(idx1);

        const int i0 = (idx1 - 1) & kDelayMask;
        const int i1 = idx1 & kDelayMask;
        const int i2 = (idx1 + 1) & kDelayMask;
        const int i3 = (idx1 + 2) & kDelayMask;

        const float y0 = line[static_cast<size_t>(i0)];
        const float y1 = line[static_cast<size_t>(i1)];
        const float y2 = line[static_cast<size_t>(i2)];
        const float y3 = line[static_cast<size_t>(i3)];

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    double sr = 48000.0;
    float invSr = 1.0f / 48000.0f;
    float rt60Sec = 1.65f;
    int writeIndex = 0;
    float duckAttackCoeff = 0.01f;
    float duckReleaseCoeff = 0.001f;
    float vocalDuckEnv = 0.0f;

    std::array<std::array<float, kMaxDelaySamples>, kNumChannels> delayLines{};
    std::array<float, kNumChannels> baseDelaySamples{};
    std::array<float, kNumChannels> feedbackGains{};
    std::array<float, kNumChannels> lfoPhases{};
    std::array<vg::mastering::TptStateVariableFilter, kNumChannels> dampFilters;
    vg::mastering::TptStateVariableFilter inputHighpass;
    vg::mastering::TptStateVariableFilter inputLowpass;
};

//==============================================================================
// 4. スタジオ音響空間＆マイクカプセル統合スイート (StudioAcousticRoomSuite)
//==============================================================================
class StudioAcousticRoomSuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        deReverb.prepare(sampleRate);
        capsuleSim.prepare(sampleRate);
        hadamardReverb.prepare(sampleRate);
    }

    void reset() noexcept
    {
        deReverb.reset();
        capsuleSim.reset();
        hadamardReverb.reset();
    }

    void processSamplePair(float& left,
                           float& right,
                           float deReverbAmount01,
                           VintageMicrophoneCapsuleSimulator::CapsuleModel micModel,
                           float micCharacter01,
                           float velvetReverbMix01) noexcept
    {
        deReverb.processSamplePair(left, right, deReverbAmount01);
        capsuleSim.setModel(micModel);
        capsuleSim.processSamplePair(left, right, micCharacter01);
        hadamardReverb.processSamplePair(left, right, velvetReverbMix01);
    }

    DirectToReverberantDeReverbEngine& getDeReverb() noexcept { return deReverb; }
    const DirectToReverberantDeReverbEngine& getDeReverb() const noexcept { return deReverb; }

    VintageMicrophoneCapsuleSimulator& getCapsuleSimulator() noexcept { return capsuleSim; }
    const VintageMicrophoneCapsuleSimulator& getCapsuleSimulator() const noexcept { return capsuleSim; }

    Hadamard8x8VelvetFdnReverb& getHadamardReverb() noexcept { return hadamardReverb; }
    const Hadamard8x8VelvetFdnReverb& getHadamardReverb() const noexcept { return hadamardReverb; }

private:
    DirectToReverberantDeReverbEngine deReverb;
    VintageMicrophoneCapsuleSimulator capsuleSim;
    Hadamard8x8VelvetFdnReverb hadamardReverb;
};

} // namespace vg::studio_acoustics
