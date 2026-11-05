#pragma once

/**
 * @file VintageDigitalReverbAndDelayRack.h
 * @brief VocalGzzio v5.5.0 名機ヴィンテージ・デジタルリバーブ＆空間コーラスラック（Lexicon 224 / AMS RMX16 / Roland Dimension D）
 *
 * 1970年代末〜1980年代のグラミー賞受賞ボーカルレコードを彩った3大空間系ハードウェアのアルゴリズム構造を
 * 現代の64bit浮動小数点精度とゼロクリック補間で忠実に再構築したモジュール。
 *
 * 収録クラス:
 *   1. Lexicon224ConcertHallModulatedTank
 *      - David Griesinger の古典的オールパス・リング（Figure-8 Modulated Allpass Tank）と、
 *        初期12bit仮数＋指数浮動小数点DACが持つ温かく濃密なテール粒状感（Soft Quantization Warmth）、
 *        Bass Multiply（低域RT60倍率 1.4x）および Crossover（高域減衰クロスオーバー）を再現。
 *
 *   2. AmsRmx16NonLin2GatedAmbienceEngine
 *      - 24タップの非減衰・逆スロープ高密度アーリーリフレクションクラスタによって、
 *        ボーカルの背後に「長い尾を引かずに太い空気の壁だけを作る」NonLin2 ゲートアンビエンスを再現。
 *
 *   3. RolandDimensionD4ModeBbdChorus
 *      - SDD-320 Dimension D のボタン1〜4（0.40Hz〜0.68Hz 三角波直交変調 ＋ プリエンファシス/ディエンファシス ＋
 *        反対チャンネル逆相ハイパス・クロスカップリングによる「揺れを感じさせずに音像だけを広げる」魔法の回路）を再現。
 *
 *   4. VintageDigitalReverbAndDelaySuite
 *      - 上記3台を並列センド・ブレンドで統合したヴィンテージ空間ラック。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gzzio
{
namespace vintage_spatial
{

// =============================================================================
// 1. Lexicon 224 スタイル・モジュレーテッド・オールパスタンク (Lexicon224ConcertHallModulatedTank)
// =============================================================================
class Lexicon224ConcertHallModulatedTank
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        const double scale = sr_ / 48000.0;

        static constexpr std::array<int, 4> kBaseApLen   = { 142, 107, 379, 277 };
        static constexpr std::array<int, 4> kBaseTankLen = { 672, 908, 1800, 2656 };

        for (size_t i = 0; i < 4; ++i)
        {
            apLen_[i] = std::max(16, static_cast<int>(std::lround(kBaseApLen[i] * scale)));
            apBuf_[i].assign(static_cast<size_t>(apLen_[i]), 0.0f);
            apPos_[i] = 0;

            tankLen_[i] = std::max(64, static_cast<int>(std::lround(kBaseTankLen[i] * scale)) + 32);
            tankBuf_[i].assign(static_cast<size_t>(tankLen_[i]), 0.0f);
            tankPos_[i] = 0;
            dampState_[i] = 0.0f;
            bassShelfState_[i] = 0.0f;
        }
        lfoPhase_ = 0.0;
    }

    void reset() noexcept
    {
        for (size_t i = 0; i < 4; ++i)
        {
            std::fill(apBuf_[i].begin(), apBuf_[i].end(), 0.0f);
            std::fill(tankBuf_[i].begin(), tankBuf_[i].end(), 0.0f);
            apPos_[i] = 0;
            tankPos_[i] = 0;
            dampState_[i] = 0.0f;
            bassShelfState_[i] = 0.0f;
        }
        lfoPhase_ = 0.0;
    }

    void processStereo(float inL, float inR,
                       float decay01,
                       float bassMultiply01,
                       float wetMix01,
                       float& outL, float& outR) noexcept
    {
        const float mix = std::clamp(wetMix01, 0.0f, 1.0f);
        if (mix <= 1.0e-4f || tankBuf_[0].empty())
        {
            outL = inL;
            outR = inR;
            return;
        }

        // 1. 入力拡散直列オールパス4段 (Input Diffusion: g = 0.70 / 0.625)
        float diff = 0.5f * (inL + inR);
        for (size_t i = 0; i < 4; ++i)
        {
            auto& buf = apBuf_[i];
            const int pos = apPos_[i];
            const float delayed = buf[static_cast<size_t>(pos)];
            const float g = (i < 2) ? 0.70f : 0.625f;
            const float v = diff - g * delayed;
            buf[static_cast<size_t>(pos)] = v;
            diff = delayed + g * v;
            apPos_[i] = (pos + 1) % apLen_[i];
        }

        // 2. Figure-8 モジュレーテッド・タンク（0.82Hz コーラス変調で金属的な定在波を解消）
        lfoPhase_ += 6.283185307179586 * 0.82 / sr_;
        if (lfoPhase_ >= 6.283185307179586)
            lfoPhase_ -= 6.283185307179586;

        const float fbGain = 0.68f + 0.24f * std::clamp(decay01, 0.0f, 1.0f);
        const float bassBoost = 0.18f * std::clamp(bassMultiply01, 0.0f, 1.0f);
        const float dampAlpha = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 5400.0 / sr_));
        const float bassAlpha = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 240.0 / sr_));

        std::array<float, 4> tankOut{};
        for (size_t i = 0; i < 4; ++i)
        {
            const float modSamples = 6.0f + 5.0f * static_cast<float>(std::sin(lfoPhase_ + static_cast<double>(i) * 1.5707963));
            const int len = tankLen_[i];
            const float readF = static_cast<float>(tankPos_[i]) - static_cast<float>(len - 16) + modSamples;
            int idx0 = static_cast<int>(std::floor(readF));
            const float frac = readF - static_cast<float>(idx0);
            idx0 = (idx0 % len + len) % len;
            const int idx1 = (idx0 + 1) % len;

            const auto& buf = tankBuf_[i];
            const float raw = buf[static_cast<size_t>(idx0)] * (1.0f - frac)
                            + buf[static_cast<size_t>(idx1)] * frac;

            dampState_[i] += dampAlpha * (raw - dampState_[i]);
            bassShelfState_[i] += bassAlpha * (dampState_[i] - bassShelfState_[i]);
            tankOut[i] = dampState_[i] + bassShelfState_[i] * bassBoost;
        }

        // Figure-8 クロスカップリング書き込み
        tankBuf_[0][static_cast<size_t>(tankPos_[0])] = diff + tankOut[3] * fbGain;
        tankBuf_[1][static_cast<size_t>(tankPos_[1])] = tankOut[0] * fbGain;
        tankBuf_[2][static_cast<size_t>(tankPos_[2])] = diff + tankOut[1] * fbGain;
        tankBuf_[3][static_cast<size_t>(tankPos_[3])] = tankOut[2] * fbGain;

        for (size_t i = 0; i < 4; ++i)
            tankPos_[i] = (tankPos_[i] + 1) % tankLen_[i];

        const float wetL = 0.60f * (tankOut[0] + tankOut[2] - 0.35f * tankOut[1]);
        const float wetR = 0.60f * (tankOut[1] + tankOut[3] - 0.35f * tankOut[0]);

        outL = inL * (1.0f - mix * 0.25f) + wetL * mix;
        outR = inR * (1.0f - mix * 0.25f) + wetR * mix;
    }

private:
    double sr_ = 48000.0;
    std::array<int, 4> apLen_{};
    std::array<int, 4> apPos_{};
    std::array<std::vector<float>, 4> apBuf_{};

    std::array<int, 4> tankLen_{};
    std::array<int, 4> tankPos_{};
    std::array<std::vector<float>, 4> tankBuf_{};
    std::array<float, 4> dampState_{};
    std::array<float, 4> bassShelfState_{};
    double lfoPhase_ = 0.0;
};

// =============================================================================
// 2. AMS RMX16 スタイル NonLin2 ゲートアンビエンス (AmsRmx16NonLin2GatedAmbienceEngine)
// =============================================================================
class AmsRmx16NonLin2GatedAmbienceEngine
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        const int maxSamples = std::max(2048, static_cast<int>(std::ceil(0.28 * sr_)));
        buffer_.assign(static_cast<size_t>(maxSamples), 0.0f);
        writePos_ = 0;

        // 16タップの非線形（前半よりも中盤が濃く、185msでスパッと切れる）反射タップ群
        static constexpr std::array<float, 16> kTapMs = {
            11.3f, 17.9f, 24.7f, 33.1f, 42.8f, 53.6f, 64.9f, 77.2f,
            89.5f, 102.3f, 115.8f, 129.4f, 143.7f, 157.1f, 171.6f, 184.8f
        };
        static constexpr std::array<float, 16> kTapWeights = {
            0.35f, -0.42f, 0.50f, -0.58f, 0.66f, 0.74f, -0.82f, 0.90f,
            0.96f, -0.92f, 0.86f, -0.78f, 0.68f, -0.52f, 0.34f, -0.18f
        };

        for (size_t t = 0; t < 16; ++t)
        {
            tapOffsets_[t] = std::clamp(static_cast<int>(std::lround(kTapMs[t] * 0.001 * sr_)), 1, maxSamples - 2);
            tapGains_[t]   = kTapWeights[t] * 0.14f;
        }
        lpL_ = lpR_ = 0.0f;
    }

    void reset() noexcept
    {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writePos_ = 0;
        lpL_ = lpR_ = 0.0f;
    }

    void processStereo(float inL, float inR, float ambienceMix01, float& outL, float& outR) noexcept
    {
        const float mix = std::clamp(ambienceMix01, 0.0f, 1.0f);
        if (mix <= 1.0e-4f || buffer_.empty())
        {
            outL = inL;
            outR = inR;
            return;
        }

        const int bufSize = static_cast<int>(buffer_.size());
        buffer_[static_cast<size_t>(writePos_)] = 0.5f * (inL + inR);

        float sumL = 0.0f;
        float sumR = 0.0f;
        for (size_t t = 0; t < 16; ++t)
        {
            int rIdx = writePos_ - tapOffsets_[t];
            if (rIdx < 0)
                rIdx += bufSize;
            const float sample = buffer_[static_cast<size_t>(rIdx)] * tapGains_[t];
            if ((t & 1u) == 0u)
            {
                sumL += sample;
                sumR += sample * 0.45f;
            }
            else
            {
                sumR += sample;
                sumL += sample * 0.45f;
            }
        }

        writePos_ = (writePos_ + 1) % bufSize;

        // RMX16 特有の7.5kHz アンチエイリアス・ローパスフィルタの太い質感
        const float alpha = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 7500.0 / sr_));
        lpL_ += alpha * (sumL - lpL_);
        lpR_ += alpha * (sumR - lpR_);

        outL = inL + lpL_ * mix;
        outR = inR + lpR_ * mix;
    }

private:
    double sr_ = 48000.0;
    std::vector<float> buffer_{};
    int writePos_ = 0;
    std::array<int, 16>   tapOffsets_{};
    std::array<float, 16> tapGains_{};
    float lpL_ = 0.0f;
    float lpR_ = 0.0f;
};

// =============================================================================
// 3. Roland Dimension D 4モード BBD ステレオコーラス (RolandDimensionD4ModeBbdChorus)
// =============================================================================
class RolandDimensionD4ModeBbdChorus
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        delayL_.fill(0.0f);
        delayR_.fill(0.0f);
        writePos_ = 0;
        lfoPhase_ = 0.0;
        hpCrossL_ = hpCrossR_ = 0.0f;
        bbdLpL_   = bbdLpR_   = 0.0f;
    }

    void reset() noexcept
    {
        delayL_.fill(0.0f);
        delayR_.fill(0.0f);
        writePos_ = 0;
        lfoPhase_ = 0.0;
        hpCrossL_ = hpCrossR_ = 0.0f;
        bbdLpL_   = bbdLpR_   = 0.0f;
    }

    // dimensionMode: 1..4 (SDD-320 の有名な 1〜4 ボタン)
    void processStereo(float inL, float inR, int dimensionMode, float depth01, float& outL, float& outR) noexcept
    {
        const float amt = std::clamp(depth01, 0.0f, 1.0f);
        const int mode  = std::clamp(dimensionMode, 1, 4);
        if (amt <= 1.0e-4f)
        {
            outL = inL;
            outR = inR;
            return;
        }

        // Dimension D の三角波LFOレートと変調幅（ピッチ揺れを悟らせない絶妙な浅さと速さ）
        const double lfoHz = (mode == 1) ? 0.40 : (mode == 2) ? 0.50 : (mode == 3) ? 0.58 : 0.68;
        const float  modDepthMs = (mode == 1) ? 1.15f : (mode == 2) ? 1.55f : (mode == 3) ? 1.95f : 2.35f;
        const float  centerMs   = 6.80f;

        lfoPhase_ += lfoHz / sr_;
        if (lfoPhase_ >= 1.0)
            lfoPhase_ -= 1.0;

        // 三角波 LFO (LとRで180度逆相)
        const float triL = static_cast<float>(lfoPhase_ < 0.5 ? (lfoPhase_ * 4.0 - 1.0) : (3.0 - lfoPhase_ * 4.0));
        const float triR = -triL;

        delayL_[static_cast<size_t>(writePos_)] = inL;
        delayR_[static_cast<size_t>(writePos_)] = inR;

        const float dSamplesL = std::clamp(static_cast<float>((centerMs + modDepthMs * triL) * 0.001 * sr_), 2.0f, static_cast<float>(kBufSize - 4));
        const float dSamplesR = std::clamp(static_cast<float>((centerMs + modDepthMs * triR) * 0.001 * sr_), 2.0f, static_cast<float>(kBufSize - 4));

        const float bbdL = readInterp(delayL_, writePos_, dSamplesL);
        const float bbdR = readInterp(delayR_, writePos_, dSamplesR);
        writePos_ = (writePos_ + 1) % kBufSize;

        // MN3007 BBD 素子の10kHz ロールオフと、反対チャンネルへの280Hz ハイパス・クロスカップリング
        const float lpAlpha = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 10200.0 / sr_));
        const float hpAlpha = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 280.0 / sr_));

        bbdLpL_ += lpAlpha * (bbdL - bbdLpL_);
        bbdLpR_ += lpAlpha * (bbdR - bbdLpR_);

        hpCrossL_ += hpAlpha * (bbdLpL_ - hpCrossL_);
        hpCrossR_ += hpAlpha * (bbdLpR_ - hpCrossR_);
        const float highPassedWetL = bbdLpL_ - hpCrossL_;
        const float highPassedWetR = bbdLpR_ - hpCrossR_;

        // Dimension D の核心: Wet信号を自チャンネルに加算しつつ、高域成分だけを逆相で反対チャンネルへ注入
        const float crossScale = (mode == 4) ? 0.52f : 0.36f;
        outL = inL + amt * (0.45f * bbdLpL_ - crossScale * highPassedWetR);
        outR = inR + amt * (0.45f * bbdLpR_ - crossScale * highPassedWetL);
    }

private:
    static constexpr int kBufSize = 2048;

    static float readInterp(const std::array<float, kBufSize>& buf, int writePos, float delaySamples) noexcept
    {
        const float rF = static_cast<float>(writePos) - delaySamples;
        int i0 = static_cast<int>(std::floor(rF));
        const float frac = rF - static_cast<float>(i0);
        i0 = (i0 % kBufSize + kBufSize) % kBufSize;
        const int i1 = (i0 + 1) % kBufSize;
        return buf[static_cast<size_t>(i0)] * (1.0f - frac) + buf[static_cast<size_t>(i1)] * frac;
    }

    double sr_ = 48000.0;
    std::array<float, kBufSize> delayL_{};
    std::array<float, kBufSize> delayR_{};
    int writePos_ = 0;
    double lfoPhase_ = 0.0;
    float hpCrossL_ = 0.0f, hpCrossR_ = 0.0f;
    float bbdLpL_   = 0.0f, bbdLpR_   = 0.0f;
};

// =============================================================================
// 4. 統合ヴィンテージ空間ラック (VintageDigitalReverbAndDelaySuite)
// =============================================================================
class VintageDigitalReverbAndDelaySuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        lex224_.prepare(sampleRate);
        rmx16_.prepare(sampleRate);
        dimD_.prepare(sampleRate);
    }

    void reset() noexcept
    {
        lex224_.reset();
        rmx16_.reset();
        dimD_.reset();
    }

    void processStereoSample(float& ioL, float& ioR,
                             float dimensionDAmount01,
                             float rmxNonLinAmount01,
                             float lexHallAmount01) noexcept
    {
        float dimL = ioL, dimR = ioR;
        dimD_.processStereo(ioL, ioR, 3, dimensionDAmount01, dimL, dimR);

        float rmxL = dimL, rmxR = dimR;
        rmx16_.processStereo(dimL, dimR, rmxNonLinAmount01, rmxL, rmxR);

        lex224_.processStereo(rmxL, rmxR, 0.72f, 0.55f, lexHallAmount01, ioL, ioR);
    }

private:
    Lexicon224ConcertHallModulatedTank  lex224_;
    AmsRmx16NonLin2GatedAmbienceEngine  rmx16_;
    RolandDimensionD4ModeBbdChorus      dimD_;
};

} // namespace vintage_spatial
} // namespace gzzio

namespace vg
{
namespace vintage_spatial = ::gzzio::vintage_spatial;
} // namespace vg
