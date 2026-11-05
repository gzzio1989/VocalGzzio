#pragma once

/**
 * @file NeuralFormantAndGlottalSynth.h
 * @brief VocalGzzio v5.5.0 声門パルス物理モデル (Liljencrants-Fant)・14次Levinson-Durbin LPC真包絡・気門下胸腔共鳴＆声門開大ブレス励振エンジン
 *
 * ボイスチェンジャーや極端なピッチシフト（±12半音）、ウィスパー歌唱、サブオクターブ補強において、
 * 「ロボット声・ケロケロ感・プラスチック感」が生じる根本原因である声門音源の劣化とフォルマント崩壊を
 * 音声生成物理学（Gunnar Fant, Dennis Klatt, Hideki Kawahara TANDEM-STRAIGHT/WORLD 理論）に基づいて解決するモジュール。
 *
 * 収録クラス:
 *   1. LiljencrantsFantGlottalPulseModel (LF声門体積流微分モデル)
 *      - Gunnar Fant (1985) の4パラメータ LFモデル (Tp, Te, Ta, Rd) をリアルタイム解析形式で実装。
 *      - 発声強度に応じた Rd パラメータ（0.5=力強いベルティング 〜 2.5=柔らかいファルセット・ウィスパー）の連続モーフィング。
 *
 *   2. TrueEnvelopeCepstralLpcTracker (14次自己相関＋Levinson-Durbin LPC＋ケプストラム・リフタリング声道包絡追跡器)
 *      - オーディオスレッドのヒープ確保ゼロで14次線形予測係数（LPC）を Levinson-Durbin 再帰アルゴリズムで解き、
 *        ピッチ調波の微細リップルを除去した滑らかな声道フォルマント包絡（F1〜F5）を抽出・再適用。
 *
 *   3. SubGlottalChestResonanceInjector (気管・胸骨下気門共鳴インジェクター)
 *      - 声門閉鎖瞬間（Glottal Closure Instant: GCI）に駆動される気管（Trachea: 約550Hz, 1450Hz）および
 *        胸骨腔（Sternum Chest Wall: 125Hz〜210Hz）の身体共鳴を物理合成し、細い声に圧倒的な体幹の説得力を付与。
 *
 *   4. BreathyGlottalChinkExciter (声門開大期同期・Klatt 気息アスピレーション励振器)
 *      - 単なる定常ホワイトノイズの付加ではなく、声門が開いている位相区間（Open Quotient: OQ）にのみ同期して
 *        乱流気息（2.2kHz〜9.5kHz フォルマント変調ブレス）を注入する Dennis Klatt (1990) 型の生体ブレス合成器。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace glottal_synth
{

// =============================================================================
// 1. Liljencrants-Fant (LF) 声門体積流微分パルスモデル
// =============================================================================
class LiljencrantsFantGlottalPulseModel
{
public:
    struct LfParameters
    {
        float rdShape        = 1.10f; // Fant の波形形状指標 Rd (0.5: 強い閉鎖 〜 2.7: 柔らかい息漏れ声)
        float openQuotient   = 0.62f; // 声門開大率 Te / T0 (0.38〜0.88)
        float speedQuotient  = 2.15f; // 非対称速度比 αm (1.5〜3.2)
        float returnPhaseTa  = 0.045f;// 閉鎖復帰時定数比 Ta / T0 (0.01〜0.18: 高域スペクトル傾斜を決定)
        float subHarmonicMix = 0.18f; // 1オクターブ下の声門倍音ブレンド量
    };

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        reset();
    }

    void reset() noexcept
    {
        phase01      = 0.0;
        subPhase01   = 0.0;
        smoothF0Hz   = 180.0f;
        spectralTilt = 0.0f;
        dcStateIn    = 0.0f;
        dcStateOut   = 0.0f;
        lastOpenMask = 0.0f;
    }

    // Fant (1995) の Rd 単一パラメータから OQ(Te), Tp, Ta を自動導出
    static LfParameters deriveFromRd(float rd, float subMix = 0.15f) noexcept
    {
        LfParameters p{};
        const float r = std::clamp(rd, 0.45f, 2.80f);
        p.rdShape = r;
        // Fant (1995) "The LF-model revisited: transformations and frequency domain analysis"
        const float rap = (-1.0f + 4.8f * r) * 0.01f;
        const float rkp = (22.4f + 11.8f * r) * 0.01f;
        const float rgp = rkp / (0.5f / (0.11f * r / (0.5f + 1.2f * rkp) - rap) * 4.0f);
        const float tp  = 1.0f / std::max(1.1f, 2.0f * rgp);
        const float te  = tp * (1.0f + rkp);

        p.openQuotient  = std::clamp(te, 0.36f, 0.88f);
        p.speedQuotient = std::clamp(1.0f / std::max(0.15f, rkp), 1.4f, 3.4f);
        p.returnPhaseTa = std::clamp(rap, 0.008f, 0.22f);
        p.subHarmonicMix = std::clamp(subMix, 0.0f, 1.0f);
        return p;
    }

    // 1サンプル分の LF 声門微分パルス E(t) を生成し、同時に現在の声門開大マスク [0..1] を更新
    inline float synthesizeSample(float targetF0Hz, bool isVoiced, const LfParameters& params) noexcept
    {
        if (!isVoiced || targetF0Hz < 45.0f || targetF0Hz > 1400.0f)
        {
            lastOpenMask *= 0.992f;
            return 0.0f;
        }

        smoothF0Hz += 0.015f * (targetF0Hz - smoothF0Hz);
        const double inc = static_cast<double>(smoothF0Hz) / sampleRate;
        phase01 += inc;
        if (phase01 >= 1.0)
            phase01 -= std::floor(phase01);

        subPhase01 += inc * 0.5;
        if (subPhase01 >= 1.0)
            subPhase01 -= std::floor(subPhase01);

        const float pulseFund = evaluateLfWaveform(static_cast<float>(phase01), params, lastOpenMask);
        float dummyMask = 0.0f;
        const float pulseSub  = evaluateLfWaveform(static_cast<float>(subPhase01), params, dummyMask);

        const float raw = pulseFund + params.subHarmonicMix * pulseSub;

        // 1次ハイパス DC ブロッカー (18Hz)
        const float dcRemoved = raw - dcStateIn + 0.9975f * dcStateOut;
        dcStateIn  = raw;
        dcStateOut = dcRemoved;

        // Rd に応じた緩やかなスペクトル傾斜（息漏れ声ほど高次倍音を柔らかくロールオフ）
        const float tiltAlpha = std::clamp(0.65f - 0.14f * (params.rdShape - 0.5f), 0.18f, 0.85f);
        spectralTilt += tiltAlpha * (dcRemoved - spectralTilt);
        return std::isfinite(spectralTilt) ? spectralTilt : 0.0f;
    }

    inline float getGlottalOpenPhaseMask01() const noexcept { return lastOpenMask; }
    inline float getCurrentPhase01() const noexcept { return static_cast<float>(phase01); }

private:
    static inline float evaluateLfWaveform(float t01, const LfParameters& p, float& outOpenMask) noexcept
    {
        const float te = std::clamp(p.openQuotient, 0.35f, 0.88f);
        const float tp = std::clamp(te / (1.0f + 1.0f / std::max(1.2f, p.speedQuotient)), 0.22f, te - 0.04f);
        const float ta = std::clamp(p.returnPhaseTa, 0.008f, 0.24f);

        if (t01 < te)
        {
            // 開大期〜閉鎖瞬間 (0 <= t < Te): E(t) = E0 * exp(α * t) * sin(π * t / Tp)
            const float u = t01 / te;
            outOpenMask = std::sin(3.1415926535f * u);
            const float alpha = 1.65f / std::max(0.2f, tp);
            const float growth = std::exp(alpha * (t01 - tp));
            const float wave   = std::sin(3.1415926535f * (t01 / tp));
            return growth * wave;
        }
        else
        {
            // 閉鎖復帰期 (Te <= t < 1.0): E(t) = - (1 / (ε * Ta)) * (exp(-ε*(t - Te)) - exp(-ε*(1 - Te)))
            outOpenMask = 0.0f;
            const float dt = t01 - te;
            const float rem = std::max(0.05f, 1.0f - te);
            const float eps = 1.0f / std::max(0.006f, ta);
            const float endExp = std::exp(-eps * rem);
            const float curExp = std::exp(-eps * dt);
            // Te での負のピーク（声門閉鎖インパルス GCI）から 0 へ指数復帰
            const float negPeak = -std::exp(1.65f * (te - tp) / std::max(0.2f, tp))
                                * std::fabs(std::sin(3.1415926535f * (te / tp)));
            const float normDenom = std::max(1.0e-4f, 1.0f - endExp);
            return negPeak * ((curExp - endExp) / normDenom);
        }
    }

    double sampleRate   = 48000.0;
    double phase01      = 0.0;
    double subPhase01   = 0.0;
    float  smoothF0Hz   = 180.0f;
    float  spectralTilt = 0.0f;
    float  dcStateIn    = 0.0f;
    float  dcStateOut   = 0.0f;
    float  lastOpenMask = 0.0f;
};

// =============================================================================
// 2. 14次 Levinson-Durbin LPC ＋ ケプストラム声道フォルマント包絡トラッカー
// =============================================================================
class TrueEnvelopeCepstralLpcTracker
{
public:
    static constexpr size_t kLpcOrder  = 14;
    static constexpr size_t kFrameSize = 256;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        for (size_t i = 0; i < kFrameSize; ++i)
        {
            const double u = static_cast<double>(i) / static_cast<double>(kFrameSize - 1);
            hannWindow[i] = static_cast<float>(0.5 - 0.5 * std::cos(6.283185307179586 * u));
        }
        reset();
    }

    void reset() noexcept
    {
        ringBuffer.fill(0.0f);
        autocorr.fill(0.0);
        lpcCoeffs.fill(0.0f);
        lpcCoeffs[0] = 1.0f;
        cepstrum.fill(0.0f);
        synthHistory.fill(0.0f);
        writePos      = 0;
        hopCounter    = 0;
        preEmphPrevIn = 0.0f;
        deEmphPrevOut = 0.0f;
        estimatedTiltDbPerOct = -6.0f;
    }

    // 入力音声サンプルを蓄積し、64サンプルごとに14次Levinson-Durbin再帰で声道フィルタ係数を更新
    inline void pushAnalysisSample(float inputSample) noexcept
    {
        // プリエンファシス (1 - 0.94 z^-1) で声門の -12dB/oct 傾斜を平坦化してから声道共鳴を抽出
        const float pre = inputSample - 0.94f * preEmphPrevIn;
        preEmphPrevIn = inputSample;

        ringBuffer[writePos] = pre;
        writePos = (writePos + 1) & (kFrameSize - 1);

        if (++hopCounter >= 64)
        {
            hopCounter = 0;
            computeLpcAndCepstrum();
        }
    }

    // 任意の励振信号（ピッチシフト後信号やLF声門パルス）に、抽出した元の声道フォルマント包絡を適用
    inline float applyTrackedVocalTractFilter(float excitationSample, float formantPreserveAmount01) noexcept
    {
        const float amt = std::clamp(formantPreserveAmount01, 0.0f, 1.0f);
        if (amt <= 0.001f)
            return excitationSample;

        // 全極フィルタ H(z) = G / (1 + ∑ a_k z^-k)
        double acc = static_cast<double>(excitationSample) * static_cast<double>(frameGain);
        for (size_t k = 1; k <= kLpcOrder; ++k)
            acc -= static_cast<double>(lpcCoeffs[k]) * static_cast<double>(synthHistory[k - 1]);

        // 安定性クランプ
        float out = static_cast<float>(std::clamp(acc, -4.0, 4.0));
        for (size_t k = kLpcOrder; k > 1; --k)
            synthHistory[k - 1] = synthHistory[k - 2];
        synthHistory[0] = out;

        // ディエンファシス (1 / (1 - 0.94 z^-1))
        deEmphPrevOut = out + 0.92f * deEmphPrevOut;
        if (!std::isfinite(deEmphPrevOut))
            deEmphPrevOut = 0.0f;

        return excitationSample * (1.0f - amt) + deEmphPrevOut * amt;
    }

    // 指定周波数 (Hz) におけるケプストラム平滑化対数スペクトル包絡 (dB) を評価
    float evaluateSmoothedEnvelopeDb(float freqHz) const noexcept
    {
        const float omega = 6.28318530718f * std::clamp(freqHz, 20.0f, static_cast<float>(sampleRate) * 0.48f)
                          / static_cast<float>(sampleRate);
        float logMag = cepstrum[0];
        for (size_t m = 1; m <= kLpcOrder; ++m)
            logMag += 2.0f * cepstrum[m] * std::cos(omega * static_cast<float>(m));
        return std::clamp(logMag * 8.6858896f, -60.0f, 36.0f);
    }

    inline const std::array<float, kLpcOrder + 1>& getLpcCoefficients() const noexcept { return lpcCoeffs; }
    inline float getEstimatedSpectralTiltDb() const noexcept { return estimatedTiltDbPerOct; }

private:
    void computeLpcAndCepstrum() noexcept
    {
        std::array<float, kFrameSize> windowed{};
        for (size_t i = 0; i < kFrameSize; ++i)
        {
            const size_t idx = (writePos + i) & (kFrameSize - 1);
            windowed[i] = ringBuffer[idx] * hannWindow[i];
        }

        // 自己相関関数 R[0..kLpcOrder] の計算
        for (size_t lag = 0; lag <= kLpcOrder; ++lag)
        {
            double sum = 0.0;
            const size_t limit = kFrameSize - lag;
            for (size_t n = 0; n < limit; ++n)
                sum += static_cast<double>(windowed[n]) * static_cast<double>(windowed[n + lag]);
            autocorr[lag] = sum;
        }

        // ホワイトノイズ・ラグ窓（対角荷重）で極が単位円上に張り付く発散を100%防止
        autocorr[0] = std::max(1.0e-7, autocorr[0] * 1.002);
        if (autocorr[0] <= 1.0e-6)
            return;

        estimatedTiltDbPerOct = static_cast<float>(6.0 * (autocorr[1] / autocorr[0] - 1.0));

        // Levinson-Durbin 再帰アルゴリズム (O(P^2), ヒープ確保ゼロ)
        std::array<double, kLpcOrder + 1> a{};
        std::array<double, kLpcOrder + 1> aPrev{};
        a[0] = 1.0;
        double err = autocorr[0];

        for (size_t m = 1; m <= kLpcOrder; ++m)
        {
            double lambda = 0.0;
            for (size_t j = 1; j < m; ++j)
                lambda += a[j] * autocorr[m - j];
            lambda = -(autocorr[m] + lambda) / std::max(1.0e-9, err);

            // 反射係数（PARCOR係数）を [-0.96, +0.96] に制限し全極フィルタの絶対安定性を保証
            lambda = std::clamp(lambda, -0.96, 0.96);

            aPrev = a;
            a[m]  = lambda;
            for (size_t j = 1; j < m; ++j)
                a[j] = aPrev[j] + lambda * aPrev[m - j];

            err *= (1.0 - lambda * lambda);
        }

        // 帯域幅拡張 (Bandwidth Expansion: γ = 0.988) で鋭すぎる共振ピークを音楽的に緩和
        double gammaPow = 1.0;
        for (size_t k = 0; k <= kLpcOrder; ++k)
        {
            lpcCoeffs[k] = static_cast<float>(a[k] * gammaPow);
            gammaPow *= 0.988;
        }
        frameGain = std::clamp(static_cast<float>(std::sqrt(std::max(1.0e-7, err) / static_cast<double>(kFrameSize))), 0.02f, 0.85f);

        // LPC係数からLPCケプストラム係数 c[m] への再帰変換（Markel & Gray 漸化式）
        cepstrum[0] = std::log(std::max(1.0e-4f, frameGain));
        for (size_t m = 1; m <= kLpcOrder; ++m)
        {
            double sum = 0.0;
            for (size_t k = 1; k < m; ++k)
                sum += static_cast<double>(m - k) * static_cast<double>(cepstrum[m - k]) * static_cast<double>(lpcCoeffs[k]);
            cepstrum[m] = static_cast<float>(-static_cast<double>(lpcCoeffs[m]) - sum / static_cast<double>(m));
        }
    }

    double sampleRate = 48000.0;
    std::array<float, kFrameSize> hannWindow{};
    std::array<float, kFrameSize> ringBuffer{};
    std::array<double, kLpcOrder + 1> autocorr{};
    std::array<float, kLpcOrder + 1>  lpcCoeffs{};
    std::array<float, kLpcOrder + 1>  cepstrum{};
    std::array<float, kLpcOrder>      synthHistory{};
    size_t writePos      = 0;
    int    hopCounter    = 0;
    float  preEmphPrevIn = 0.0f;
    float  deEmphPrevOut = 0.0f;
    float  frameGain     = 0.25f;
    float  estimatedTiltDbPerOct = -6.0f;
};

// =============================================================================
// 3. 気管・胸骨下気門共鳴インジェクター (SubGlottalChestResonanceInjector)
// =============================================================================
class SubGlottalChestResonanceInjector
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        updateResonators(155.0f, 540.0f);
        reset();
    }

    void reset() noexcept
    {
        chestIc1 = chestIc2 = 0.0;
        trachIc1 = trachIc2 = 0.0;
        lastChestEnergy = 0.0f;
    }

    void updateResonators(float sternumFreqHz, float trachealFreqHz) noexcept
    {
        // 第1共鳴: 胸骨・胸郭壁共鳴 (125Hz〜210Hz, Q=2.8)
        const double g1 = std::tan(3.141592653589793 * std::clamp(static_cast<double>(sternumFreqHz), 90.0, 280.0) / sampleRate);
        const double k1 = 1.0 / 2.8;
        cA1 = 1.0 / (1.0 + g1 * (g1 + k1));
        cA2 = g1 * cA1;
        cA3 = g1 * cA2;

        // 第2共鳴: 声門下気管支第1フォルマント Sub-F1 (510Hz〜620Hz, Q=3.5)
        const double g2 = std::tan(3.141592653589793 * std::clamp(static_cast<double>(trachealFreqHz), 420.0, 750.0) / sampleRate);
        const double k2 = 1.0 / 3.5;
        tA1 = 1.0 / (1.0 + g2 * (g2 + k2));
        tA2 = g2 * tA1;
        tA3 = g2 * tA2;
    }

    inline float processSample(float vocalInput, float glottalPulseSample, float chestDepth01) noexcept
    {
        const float depth = std::clamp(chestDepth01, 0.0f, 1.0f);
        if (depth <= 0.001f)
            return vocalInput;

        const double drive = static_cast<double>(vocalInput * 0.70f + glottalPulseSample * 0.30f);

        // 胸郭共鳴バンドパス
        const double cv3 = drive - chestIc2;
        const double cv1 = cA1 * chestIc1 + cA2 * cv3;
        const double cv2 = chestIc2 + cA2 * chestIc1 + cA3 * cv3;
        chestIc1 = 2.0 * cv1 - chestIc1;
        chestIc2 = 2.0 * cv2 - chestIc2;

        // 気管支共鳴バンドパス
        const double tv3 = drive - trachIc2;
        const double tv1 = tA1 * trachIc1 + tA2 * tv3;
        const double tv2 = trachIc2 + tA2 * trachIc1 + tA3 * tv3;
        trachIc1 = 2.0 * tv1 - trachIc1;
        trachIc2 = 2.0 * tv2 - trachIc2;

        const float subRes = static_cast<float>(cv1 * 0.72 + tv1 * 0.28);
        const float saturated = std::tanh(subRes * 1.4f);
        lastChestEnergy = 0.995f * lastChestEnergy + 0.005f * std::fabs(saturated);

        return vocalInput + depth * 0.28f * saturated;
    }

    inline float getChestResonanceEnergy() const noexcept { return lastChestEnergy; }

private:
    double sampleRate = 48000.0;
    double cA1 = 0.0, cA2 = 0.0, cA3 = 0.0, chestIc1 = 0.0, chestIc2 = 0.0;
    double tA1 = 0.0, tA2 = 0.0, tA3 = 0.0, trachIc1 = 0.0, trachIc2 = 0.0;
    float  lastChestEnergy = 0.0f;
};

// =============================================================================
// 4. 声門開大期同期・Klatt 生体気息アスピレーション励振器 (BreathyGlottalChinkExciter)
// =============================================================================
class BreathyGlottalChinkExciter
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        // F3/F4 帯域のベルベット・気息フォルマントフィルタ (2850Hz & 6400Hz)
        const double g1 = std::tan(3.141592653589793 * 2850.0 / sampleRate);
        const double k1 = 1.0 / 1.8;
        b1A1 = 1.0 / (1.0 + g1 * (g1 + k1));
        b1A2 = g1 * b1A1;
        b1A3 = g1 * b1A2;

        const double g2 = std::tan(3.141592653589793 * std::min(6400.0, sampleRate * 0.42) / sampleRate);
        const double k2 = 1.0 / 1.4;
        b2A1 = 1.0 / (1.0 + g2 * (g2 + k2));
        b2A2 = g2 * b2A1;
        b2A3 = g2 * b2A2;
        reset();
    }

    void reset() noexcept
    {
        b1Ic1 = b1Ic2 = 0.0;
        b2Ic1 = b2Ic2 = 0.0;
        rngState = 0x7F4A7C15u;
        envFollower = 0.0f;
    }

    inline float processSample(float vocalInput, float glottalOpenMask01, float breathAmount01) noexcept
    {
        const float amt = std::clamp(breathAmount01, 0.0f, 1.0f);
        const float absIn = std::fabs(vocalInput);
        envFollower += (absIn > envFollower ? 0.04f : 0.002f) * (absIn - envFollower);

        if (amt <= 0.001f || envFollower < 1.0e-4f)
            return vocalInput;

        // Xorshift 乱数で微細な気流乱流を生成
        uint32_t x = rngState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rngState = x;
        const float white = static_cast<float>(x & 0x00FFFFFFu) * (2.0f / 16777216.0f) - 1.0f;

        // Klatt モデル: 声門開大期マスク (35% 定常声門間隙 Chink + 65% 開大期パルス変調)
        const float pitchSyncMod = (0.35f + 0.65f * std::clamp(glottalOpenMask01, 0.0f, 1.0f)) * envFollower;
        const double drivenNoise = static_cast<double>(white * pitchSyncMod);

        const double v1_3 = drivenNoise - b1Ic2;
        const double v1_1 = b1A1 * b1Ic1 + b1A2 * v1_3;
        const double v1_2 = b1Ic2 + b1A2 * b1Ic1 + b1A3 * v1_3;
        b1Ic1 = 2.0 * v1_1 - b1Ic1;
        b1Ic2 = 2.0 * v1_2 - b1Ic2;

        const double v2_3 = drivenNoise - b2Ic2;
        const double v2_1 = b2A1 * b2Ic1 + b2A2 * v2_3;
        const double v2_2 = b2Ic2 + b2A2 * b2Ic1 + b2A3 * v2_3;
        b2Ic1 = 2.0 * v2_1 - b2Ic1;
        b2Ic2 = 2.0 * v2_2 - b2Ic2;

        const float velvetAspiration = static_cast<float>(v1_1 * 0.45 + v2_1 * 0.55);
        return vocalInput + amt * 0.16f * velvetAspiration;
    }

private:
    double sampleRate = 48000.0;
    double b1A1 = 0.0, b1A2 = 0.0, b1A3 = 0.0, b1Ic1 = 0.0, b1Ic2 = 0.0;
    double b2A1 = 0.0, b2A2 = 0.0, b2A3 = 0.0, b2Ic1 = 0.0, b2Ic2 = 0.0;
    uint32_t rngState = 0x7F4A7C15u;
    float envFollower = 0.0f;
};

// =============================================================================
// 5. 統合声門物理・真包絡フォルマント・スイート (NeuralFormantAndGlottalSuite)
// =============================================================================
class NeuralFormantAndGlottalSuite
{
public:
    void prepare(double newSampleRate) noexcept
    {
        lfGlottal.prepare(newSampleRate);
        lpcTrackerL.prepare(newSampleRate);
        lpcTrackerR.prepare(newSampleRate);
        chestInjectorL.prepare(newSampleRate);
        chestInjectorR.prepare(newSampleRate);
        breathExciterL.prepare(newSampleRate);
        breathExciterR.prepare(newSampleRate);
    }

    void reset() noexcept
    {
        lfGlottal.reset();
        lpcTrackerL.reset();
        lpcTrackerR.reset();
        chestInjectorL.reset();
        chestInjectorR.reset();
        breathExciterL.reset();
        breathExciterR.reset();
    }

    inline void processSamplePair(float& ioL,
                                  float& ioR,
                                  float detectedF0Hz,
                                  bool isVoiced,
                                  float rdShape,
                                  float formantLock01,
                                  float chestAuthority01,
                                  float glottalBreath01) noexcept
    {
        lpcTrackerL.pushAnalysisSample(ioL);
        lpcTrackerR.pushAnalysisSample(ioR);

        const auto lfParams = LiljencrantsFantGlottalPulseModel::deriveFromRd(rdShape, 0.16f);
        const float glottalPulse = lfGlottal.synthesizeSample(detectedF0Hz, isVoiced, lfParams);
        const float openMask     = lfGlottal.getGlottalOpenPhaseMask01();

        float l = lpcTrackerL.applyTrackedVocalTractFilter(ioL, formantLock01 * 0.25f);
        float r = lpcTrackerR.applyTrackedVocalTractFilter(ioR, formantLock01 * 0.25f);

        l = chestInjectorL.processSample(l, glottalPulse, chestAuthority01);
        r = chestInjectorR.processSample(r, glottalPulse, chestAuthority01);

        l = breathExciterL.processSample(l, openMask, glottalBreath01);
        r = breathExciterR.processSample(r, openMask, glottalBreath01);

        ioL = std::isfinite(l) ? l : 0.0f;
        ioR = std::isfinite(r) ? r : 0.0f;
    }

    const LiljencrantsFantGlottalPulseModel& getLfGlottalModel() const noexcept { return lfGlottal; }
    LiljencrantsFantGlottalPulseModel&       getLfGlottalModel() noexcept       { return lfGlottal; }
    const TrueEnvelopeCepstralLpcTracker&    getLpcTrackerL() const noexcept    { return lpcTrackerL; }
    const SubGlottalChestResonanceInjector&  getChestInjectorL() const noexcept { return chestInjectorL; }

private:
    LiljencrantsFantGlottalPulseModel lfGlottal;
    TrueEnvelopeCepstralLpcTracker    lpcTrackerL, lpcTrackerR;
    SubGlottalChestResonanceInjector  chestInjectorL, chestInjectorR;
    BreathyGlottalChinkExciter        breathExciterL, breathExciterR;
};

} // namespace glottal_synth
} // namespace gzzio

namespace vg
{
namespace glottal_synth = ::gzzio::glottal_synth;
} // namespace vg
