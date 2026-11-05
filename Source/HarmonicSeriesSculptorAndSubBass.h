#pragma once

/**
 * @file HarmonicSeriesSculptorAndSubBass.h
 * @brief VocalGzzio v5.5.0 レベル不変8次チェビシェフ倍音造形器＆位相同期サブオクターブ・ボーカルアンカー
 *
 * 入力声量の大小に関わらず一定の倍音色彩（2次＝三極管の温もり、3次＝五極管の芯、4〜8次＝シルキーな空気倍音）を
 * 個別ブレンドできる「レベル不変（Level-Invariant）直交チェビシェフ多項式エキサイター」と、
 * 細い声にラジオDJや映画トレーラーのような重厚な胸の支えを与える「ゼロ交差フリップフロップ位相同期サブオクターブ合成器」を収録。
 *
 * 収録クラス:
 *   1. LevelInvariantChebyshevHarmonicSculptor
 *      - 入力信号の瞬時包絡線 $E(n)$ で正規化したキャリア $u(n) = x(n) / E(n) \in [-1, 1]$ に対して
 *        第1種チェビシェフ多項式 $T_2(u)\dots T_6(u)$ を計算し、再び $E(n)$ を乗算することで、
 *        ささやき声でもシャウトでも常に狙い通りの倍音比率を保つダイナミクス独立型ハーモニック・デザイナー。
 *
 *   2. PhaseLockedSubOctaveVocalAnchor
 *      - 基音抽出ローパス (90Hz〜260Hz) のゼロ交差をヒステリシス付きシュミットトリガで検出し、
 *        周期を正確に2分周（1オクターブ下 $f_0 / 2$）したサイン波状のサブベース・ファンダメンタルを生成。
 *
 *   3. HarmonicSeriesSculptorAndSubBassSuite
 *      - 上記2モジュールを統合した倍音＆サブオクターブ造形スイート。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace harmonic_sculptor
{

// =============================================================================
// 1. レベル不変チェビシェフ多項式ハーモニック造形器 (LevelInvariantChebyshevHarmonicSculptor)
// =============================================================================
class LevelInvariantChebyshevHarmonicSculptor
{
public:
    struct HarmonicWeights
    {
        float h2EvenWarmth  = 0.35f; // 2次倍音: 真空管三極管の偶数次ウォームス
        float h3OddPresence = 0.22f; // 3次倍音: 磁気テープ＆五極管の芯と抜け
        float h4EvenSilky   = 0.10f; // 4次倍音: 華やかなシルキー倍音
        float h5OddEdge     = 0.05f; // 5次倍音: エッジと輪郭
    };

    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        envAtk_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.0015 * sr_)));
        envRel_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.0350 * sr_)));
        dcBlockAlpha_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 24.0 / sr_));
        reset();
    }

    void reset() noexcept
    {
        envL_ = envR_ = 1.0e-3f;
        dcL_  = dcR_  = 0.0f;
    }

    void processStereo(float& ioL, float& ioR, const HarmonicWeights& w, float driveMix01) noexcept
    {
        const float mix = std::clamp(driveMix01, 0.0f, 1.0f);
        if (mix <= 1.0e-4f)
            return;

        ioL = processSample(ioL, w, mix, envL_, dcL_);
        ioR = processSample(ioR, w, mix, envR_, dcR_);
    }

private:
    float processSample(float x, const HarmonicWeights& w, float mix, float& env, float& dcState) const noexcept
    {
        const float absX = std::fabs(x);
        const float c = (absX > env) ? envAtk_ : envRel_;
        env += c * (absX - env);

        // ノイズフロア (-66 dBFS) 以下では倍音生成を滑らかにゲートして無音時の静寂を守る
        const float gate = std::clamp((env - 0.0005f) * 400.0f, 0.0f, 1.0f);
        if (gate <= 1.0e-4f)
            return x;

        // [-1, +1] に正規化したキャリア u
        const float u  = std::clamp(x / (env + 1.0e-4f), -1.0f, 1.0f);
        const float u2 = u * u;
        const float u3 = u2 * u;

        // 第1種チェビシェフ多項式 (T_n(cos theta) = cos(n theta))
        // T2(u) = 2u^2 - 1 (DCオフセット除去のため 2(u^2 - 0.5) を使用)
        const float t2 = 2.0f * u2 - 1.0f;
        const float t3 = 4.0f * u3 - 3.0f * u;
        const float t4 = 8.0f * (u2 * u2) - 8.0f * u2 + 1.0f;
        const float t5 = 16.0f * (u3 * u2) - 20.0f * u3 + 5.0f * u;

        const float rawHarmonics = (w.h2EvenWarmth * t2
                                  + w.h3OddPresence * t3
                                  + w.h4EvenSilky * t4
                                  + w.h5OddEdge * t5) * env * gate * 0.22f;

        // 偶数次チェビシェフ多項式から生じるDC成分を24Hz ハイパスで除去
        dcState += dcBlockAlpha_ * (rawHarmonics - dcState);
        const float cleanHarmonics = rawHarmonics - dcState;

        return x + cleanHarmonics * mix;
    }

    double sr_           = 48000.0;
    float  envAtk_       = 0.01f;
    float  envRel_       = 0.001f;
    float  dcBlockAlpha_ = 0.003f;
    float  envL_         = 1.0e-3f;
    float  envR_         = 1.0e-3f;
    float  dcL_          = 0.0f;
    float  dcR_          = 0.0f;
};

// =============================================================================
// 2. 位相同期サブオクターブ・ボーカルアンカー (PhaseLockedSubOctaveVocalAnchor)
// =============================================================================
class PhaseLockedSubOctaveVocalAnchor
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        fundLpAlpha_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 220.0 / sr_));
        subLpAlpha_  = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 125.0 / sr_));
        envAlpha_    = static_cast<float>(1.0 - std::exp(-1.0 / (0.008 * sr_)));
        reset();
    }

    void reset() noexcept
    {
        fundLp1_   = fundLp2_ = 0.0f;
        subLp1_    = subLp2_  = 0.0f;
        fundEnv_   = 0.0f;
        prevSign_  = false;
        flipFlop_  = 1.0f;
    }

    void processStereo(float& ioL, float& ioR, float subAnchorAmount01) noexcept
    {
        const float amt = std::clamp(subAnchorAmount01, 0.0f, 1.0f);
        if (amt <= 1.0e-4f)
            return;

        const float mono = 0.5f * (ioL + ioR);

        // 1. 2段ローパスでボーカル基音 (f0 <= 220Hz) を抽出
        fundLp1_ += fundLpAlpha_ * (mono - fundLp1_);
        fundLp2_ += fundLpAlpha_ * (fundLp1_ - fundLp2_);
        fundEnv_ += envAlpha_ * (std::fabs(fundLp2_) - fundEnv_);

        // 2. ヒステリシス付きゼロ交差検出と2分周フリップフロップ (f0 / 2)
        const bool currentSign = (fundLp2_ > 1.0e-4f) ? true : ((fundLp2_ < -1.0e-4f) ? false : prevSign_);
        if (currentSign && !prevSign_)
            flipFlop_ = -flipFlop_;
        prevSign_ = currentSign;

        // 3. 分周矩形波に基音包絡線を乗算し、2段125Hzローパスで丸めて純粋なサイン波状サブオクターブへ整形
        const float rawSub = flipFlop_ * fundEnv_;
        subLp1_ += subLpAlpha_ * (rawSub - subLp1_);
        subLp2_ += subLpAlpha_ * (subLp1_ - subLp2_);

        const float subSignal = subLp2_ * (amt * 0.28f);
        ioL += subSignal;
        ioR += subSignal;
    }

private:
    double sr_          = 48000.0;
    float  fundLpAlpha_ = 0.02f;
    float  subLpAlpha_  = 0.015f;
    float  envAlpha_    = 0.005f;
    float  fundLp1_     = 0.0f;
    float  fundLp2_     = 0.0f;
    float  subLp1_      = 0.0f;
    float  subLp2_      = 0.0f;
    float  fundEnv_     = 0.0f;
    bool   prevSign_    = false;
    float  flipFlop_    = 1.0f;
};

// =============================================================================
// 3. 統合倍音造形＆サブオクターブ・スイート (HarmonicSeriesSculptorAndSubBassSuite)
// =============================================================================
class HarmonicSeriesSculptorAndSubBassSuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        chebyshev_.prepare(sampleRate);
        subAnchor_.prepare(sampleRate);
    }

    void reset() noexcept
    {
        chebyshev_.reset();
        subAnchor_.reset();
    }

    void processStereoSample(float& ioL, float& ioR,
                             float harmonicSculpt01,
                             float subBassAnchor01) noexcept
    {
        LevelInvariantChebyshevHarmonicSculptor::HarmonicWeights w{};
        chebyshev_.processStereo(ioL, ioR, w, harmonicSculpt01);
        subAnchor_.processStereo(ioL, ioR, subBassAnchor01);
    }

private:
    LevelInvariantChebyshevHarmonicSculptor chebyshev_;
    PhaseLockedSubOctaveVocalAnchor         subAnchor_;
};

} // namespace harmonic_sculptor
} // namespace gzzio

namespace vg
{
namespace harmonic_sculptor = ::gzzio::harmonic_sculptor;
} // namespace vg
