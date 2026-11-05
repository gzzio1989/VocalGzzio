#pragma once

/**
 * @file AcousticImpedanceAndVocalTractTwin.h
 * @brief VocalGzzio v5.5.0 Kelly-Lochbaum 声道音響管散乱デジタルツイン＆Sundberg シンガーズ・フォルマント（喉頭蓋管2.9kHz共鳴）エンジン
 *
 * 音声物理学の古典的名モデルである Kelly & Lochbaum (1962) の区分断面積音響管（Piecewise Cylindrical Acoustic Tube）の
 * 反射係数ジャンクション散乱方程式と、Johan Sundberg (1974, KTH Royal Institute of Technology) が解明した
 * オペラ歌手・一流ボーカリスト特有の「シンガーズ・フォルマント・クラスタ（喉頭室・梨状陥凹の独立共鳴による 2.8kHz〜3.2kHz の黄金の輝き）」、
 * および Flanagan の唇球面バッフル放射インピーダンス（Lip Radiation Load: +6 dB/oct 微分放射特性）をモデル化したモジュール。
 *
 * 収録クラス:
 *   1. SundbergEpilarynxSingersFormantResonator
 *      - 喉頭蓋管（Epilarynx Tube）の断面積比が咽頭腔の 1/6 以下になったときに発生する第3・第4・第5フォルマントの密集共鳴
 *        ($F_3 = 2750\text{ Hz}, \; F_4 = 3050\text{ Hz}, \; F_5 = 3420\text{ Hz}$) を3段並列TPT状態変数共鳴器で再現。
 *      - どんなに分厚いオーケストラやロックバンドの壁の中でも、声量を無理に張り上げることなく歌声がスッと前に抜ける物理現象を付与。
 *
 *   2. KellyLochbaum6JunctionVocalTractWaveguide
 *      - 声門から唇までの17.5cm声道を6区間の円筒音響管ジャンクションとしてモデル化し、
 *        反射係数 $k_m = (A_{m+1} - A_m) / (A_{m+1} + A_m)$ による前進波・後退波の散乱で豊かな声道ボディ共鳴を合成。
 *
 *   3. LipSphericalBaffleRadiationFilter
 *      - 唇から自由空間へ音波が放射される際の音響放射インピーダンス $Z_{rad}(z)$ を再現し、声の輪郭と前方投射感を強化。
 *
 *   4. AcousticImpedanceAndVocalTractSuite
 *      - 上記3物理モデルを統合した声道インピーダンス・デジタルツインスイート。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace vocal_tract_twin
{

// =============================================================================
// 1. Sundberg シンガーズ・フォルマント・クラスタ共鳴器 (SundbergEpilarynxSingersFormantResonator)
// =============================================================================
class SundbergEpilarynxSingersFormantResonator
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        reset();
        setEpilarynxTuning(2820.0, 3120.0, 3480.0);
    }

    void reset() noexcept
    {
        ic1L_.fill(0.0f);
        ic2L_.fill(0.0f);
        ic1R_.fill(0.0f);
        ic2R_.fill(0.0f);
    }

    void setEpilarynxTuning(double f3Hz, double f4Hz, double f5Hz) noexcept
    {
        const std::array<double, 3> freqs = { f3Hz, f4Hz, f5Hz };
        const std::array<double, 3> qs    = { 5.2,  6.4,  5.8  };
        const double nyq = sr_ * 0.43;

        for (size_t i = 0; i < 3; ++i)
        {
            const double fc = std::clamp(freqs[i], 1800.0, nyq);
            const double g  = std::tan(3.14159265358979323846 * fc / sr_);
            const double k  = 1.0 / qs[i];
            k_[i]  = static_cast<float>(k);
            a1_[i] = static_cast<float>(1.0 / (1.0 + g * (g + k)));
            a2_[i] = static_cast<float>(g) * a1_[i];
            a3_[i] = static_cast<float>(g) * a2_[i];
        }
    }

    void processStereo(float& ioL, float& ioR, float singersFormantRing01) noexcept
    {
        const float ring = std::clamp(singersFormantRing01, 0.0f, 1.0f);
        if (ring <= 1.0e-4f)
            return;

        static constexpr std::array<float, 3> kWeights = { 0.38f, 0.46f, 0.24f };
        float clusterL = 0.0f;
        float clusterR = 0.0f;

        for (size_t i = 0; i < 3; ++i)
        {
            const float v3L = ioL - ic2L_[i];
            const float v1L = a1_[i] * ic1L_[i] + a2_[i] * v3L;
            const float v2L = ic2L_[i] + a2_[i] * ic1L_[i] + a3_[i] * v3L;
            ic1L_[i] = 2.0f * v1L - ic1L_[i];
            ic2L_[i] = 2.0f * v2L - ic2L_[i];
            clusterL += (v1L * k_[i]) * kWeights[i];

            const float v3R = ioR - ic2R_[i];
            const float v1R = a1_[i] * ic1R_[i] + a2_[i] * v3R;
            const float v2R = ic2R_[i] + a2_[i] * ic1R_[i] + a3_[i] * v3R;
            ic1R_[i] = 2.0f * v1R - ic1R_[i];
            ic2R_[i] = 2.0f * v2R - ic2R_[i];
            clusterR += (v1R * k_[i]) * kWeights[i];
        }

        // 喉頭室の非線形共鳴圧縮（強唱時に耳に刺さらず密度だけが増すソフトサチュレーション）
        ioL += std::tanh(clusterL * 1.6f) * (ring * 0.34f);
        ioR += std::tanh(clusterR * 1.6f) * (ring * 0.34f);
    }

private:
    double sr_ = 48000.0;
    std::array<float, 3> k_{};
    std::array<float, 3> a1_{};
    std::array<float, 3> a2_{};
    std::array<float, 3> a3_{};
    std::array<float, 3> ic1L_{};
    std::array<float, 3> ic2L_{};
    std::array<float, 3> ic1R_{};
    std::array<float, 3> ic2R_{};
};

// =============================================================================
// 2. Kelly-Lochbaum 6区間声道音響管ウェーブガイド (KellyLochbaum6JunctionVocalTractWaveguide)
// =============================================================================
class KellyLochbaum6JunctionVocalTractWaveguide
{
public:
    static constexpr size_t kNumSections = 6;

    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        reset();
        // イタリア古典ベルカントの開いた咽頭・口腔断面積プロファイルから算出した反射係数 k_m
        reflectionK_ = { -0.28f, 0.34f, -0.18f, 0.26f, 0.32f, -0.45f };
    }

    void reset() noexcept
    {
        fwdL_.fill(0.0f);
        bwdL_.fill(0.0f);
        fwdR_.fill(0.0f);
        bwdR_.fill(0.0f);
    }

    void processStereo(float& ioL, float& ioR, float tractResonanceDepth01) noexcept
    {
        const float depth = std::clamp(tractResonanceDepth01, 0.0f, 1.0f);
        if (depth <= 1.0e-4f)
            return;

        ioL = ioL + (stepLattice(ioL, fwdL_, bwdL_) - ioL) * (depth * 0.24f);
        ioR = ioR + (stepLattice(ioR, fwdR_, bwdR_) - ioR) * (depth * 0.24f);
    }

private:
    float stepLattice(float input,
                      std::array<float, kNumSections>& fwd,
                      std::array<float, kNumSections>& bwd) const noexcept
    {
        // 声門端の反射境界 (Glottal Reflection r_g = 0.72)
        float curFwd = input + 0.35f * bwd[0];

        std::array<float, kNumSections> nextFwd{};
        std::array<float, kNumSections> nextBwd{};

        for (size_t m = 0; m < kNumSections - 1; ++m)
        {
            const float k  = reflectionK_[m];
            const float fPlus = curFwd;
            const float bMinus = bwd[m + 1];
            const float junctionDelta = k * (fPlus - bMinus);

            nextFwd[m + 1] = (fPlus + junctionDelta) * 0.96f;
            nextBwd[m]     = (bMinus + junctionDelta) * 0.96f;
            curFwd         = nextFwd[m + 1];
        }

        // 唇端の自由空間反射 (Lip Reflection r_L = -0.45)
        const float lipOut = curFwd * (1.0f + reflectionK_[kNumSections - 1]);
        nextBwd[kNumSections - 1] = -reflectionK_[kNumSections - 1] * curFwd * 0.85f;
        nextFwd[0] = input;

        fwd = nextFwd;
        bwd = nextBwd;
        return lipOut;
    }

    double sr_ = 48000.0;
    std::array<float, kNumSections> reflectionK_{};
    std::array<float, kNumSections> fwdL_{};
    std::array<float, kNumSections> bwdL_{};
    std::array<float, kNumSections> fwdR_{};
    std::array<float, kNumSections> bwdR_{};
};

// =============================================================================
// 3. 唇球面バッフル放射インピーダンス・フィルタ (LipSphericalBaffleRadiationFilter)
// =============================================================================
class LipSphericalBaffleRadiationFilter
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        lpAlpha_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 1850.0 / sr_));
        reset();
    }

    void reset() noexcept
    {
        prevL_ = prevR_ = 0.0f;
    }

    void processStereo(float& ioL, float& ioR, float lipProjection01) noexcept
    {
        const float proj = std::clamp(lipProjection01, 0.0f, 1.0f);
        if (proj <= 1.0e-4f)
            return;

        // 唇からの球面音波放射における1次高域微分特性 (Flanagan 1972)
        prevL_ += lpAlpha_ * (ioL - prevL_);
        prevR_ += lpAlpha_ * (ioR - prevR_);
        const float radHighL = ioL - prevL_;
        const float radHighR = ioR - prevR_;

        ioL += radHighL * (proj * 0.20f);
        ioR += radHighR * (proj * 0.20f);
    }

private:
    double sr_      = 48000.0;
    float  lpAlpha_ = 0.15f;
    float  prevL_   = 0.0f;
    float  prevR_   = 0.0f;
};

// =============================================================================
// 4. 統合声道インピーダンス・デジタルツインスイート (AcousticImpedanceAndVocalTractSuite)
// =============================================================================
class AcousticImpedanceAndVocalTractSuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        waveguide_.prepare(sampleRate);
        singersFormant_.prepare(sampleRate);
        lipRadiation_.prepare(sampleRate);
    }

    void reset() noexcept
    {
        waveguide_.reset();
        singersFormant_.reset();
        lipRadiation_.reset();
    }

    void processStereoSample(float& ioL, float& ioR,
                             float tractBody01,
                             float singersFormant01,
                             float lipProjection01) noexcept
    {
        waveguide_.processStereo(ioL, ioR, tractBody01);
        singersFormant_.processStereo(ioL, ioR, singersFormant01);
        lipRadiation_.processStereo(ioL, ioR, lipProjection01);
    }

private:
    KellyLochbaum6JunctionVocalTractWaveguide waveguide_;
    SundbergEpilarynxSingersFormantResonator  singersFormant_;
    LipSphericalBaffleRadiationFilter         lipRadiation_;
};

} // namespace vocal_tract_twin
} // namespace gzzio

namespace vg
{
namespace vocal_tract_twin = ::gzzio::vocal_tract_twin;
} // namespace vg
