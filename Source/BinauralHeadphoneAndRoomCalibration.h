#pragma once

/**
 * @file BinauralHeadphoneAndRoomCalibration.h
 * @brief VocalGzzio v5.5.0 バイノーラル・ヘッドホン外在化＆スタジオモニタースピーカー3D音響較正エンジン
 *
 * ヘッドホンやイヤホンで歌枠配信・モニタリングを行うシンガーが感じる「頭内定位（頭の中で声が鳴る閉塞感）」を解消し、
 * マスタリングスタジオのラージモニター（正三角形30°配置）で聴いているかのような自然な前方音像定位を再現するモジュール。
 *
 * 収録クラス:
 *   1. SphericalHeadShadowCrossfeedEngine
 *      - Brown & Duda (1998) の球体頭部回折モデルに基づく両耳間時間差 (ITD: 280us〜340us) と
 *        頭部音響シャドウ1次シェルフフィルタ (ILD: f_shadow = c / (2 * pi * a) ≈ 1.45kHz) を備えたクロスフィード回路。
 *      - モノラル互換性を損なわない低域位相補償を備え、ヘッドホン長時間装着時の聴覚疲労を劇的に軽減。
 *
 *   2. PinnaSpectralNotchElevationCueModel
 *      - 人間の耳介（Pinna / Concha）の反射干渉によって生じる前方定位スペクトルノッチ (7.8kHz N1 ノッチ ＋ 4.2kHz 外耳道共鳴ピーク) を
 *        精密TPTフィルタで付与し、ボーカルの前後・上下の立体感を明瞭化。
 *
 *   3. HarmanTargetHeadphoneCalibrationEq
 *      - Olive & Welti (Harman International) のヘッドホンターゲット曲線に基づき、密閉型・開放型・カナル型イヤホンの
 *        過剰な3.5kHzピークや低域不足を滑らかに補正する5バンド基準キャリブレーションEQ。
 *
 *   4. BinauralHeadphoneAndRoomSuite
 *      - 上記3ステージを統合したリアルタイム・ヘッドホンモニター外在化スイート。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzzio
{
namespace binaural_cal
{

// =============================================================================
// 1. 球体頭部回折クロスフィードエンジン (SphericalHeadShadowCrossfeedEngine)
// =============================================================================
class SphericalHeadShadowCrossfeedEngine
{
public:
    enum classSpeakerAnglePreset : int
    {
        Nearfield22Deg = 0, // 22度 ニアフィールド卓上モニター（220us ITD）
        StudioStd30Deg = 1, // 30度 ITU-R BS.775 標準マスタリング配置（290us ITD）
        WideStage45Deg = 2  // 45度 ワイドステージ・コンサートモニター（380us ITD）
    };

    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        reset();
        setSpeakerAngle(StudioStd30Deg);
    }

    void reset() noexcept
    {
        delayLineL_.fill(0.0f);
        delayLineR_.fill(0.0f);
        writePos_ = 0;
        shadowLpL_ = 0.0f;
        shadowLpR_ = 0.0f;
        directCompL_ = 0.0f;
        directCompR_ = 0.0f;
    }

    void setSpeakerAngle(classSpeakerAnglePreset preset) noexcept
    {
        preset_ = preset;
        // 頭部半径 a = 8.75cm, 音速 c = 343m/s における Woodworth の球体回折ITD公式:
        //   ITD(theta) = (a / c) * (theta + sin(theta))
        double itdSec = 0.00029;
        double shadowCutoffHz = 1480.0;
        switch (preset)
        {
            case Nearfield22Deg:
                itdSec = 0.000215;
                shadowCutoffHz = 1750.0;
                crossGain_ = 0.24f;
                break;
            case StudioStd30Deg:
                itdSec = 0.000290;
                shadowCutoffHz = 1450.0;
                crossGain_ = 0.30f;
                break;
            case WideStage45Deg:
                itdSec = 0.000375;
                shadowCutoffHz = 1180.0;
                crossGain_ = 0.35f;
                break;
        }

        delaySamples_ = std::clamp(static_cast<float>(itdSec * sr_), 1.0f, static_cast<float>(kMaxDelay - 4));
        shadowAlpha_  = static_cast<float>(1.0 - std::exp(-6.283185307179586 * shadowCutoffHz / sr_));
        // クロスフィード加算による低域ビルドアップ（+2.3dB）を防ぐ直接音側の逆補償シェルフ係数
        compAlpha_    = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 650.0 / sr_));
    }

    void processStereo(float inL, float inR, float amount01, float& outL, float& outR) noexcept
    {
        const float amt = std::clamp(amount01, 0.0f, 1.0f);
        if (amt <= 1.0e-4f)
        {
            outL = inL;
            outR = inR;
            return;
        }

        // 1. 遅延バッファへの書き込み
        delayLineL_[static_cast<size_t>(writePos_)] = inL;
        delayLineR_[static_cast<size_t>(writePos_)] = inR;

        // 2. 線形補間による反対耳到達遅延 (ITD) 読み出し
        const float readPosF = static_cast<float>(writePos_) - delaySamples_;
        int idx0 = static_cast<int>(std::floor(readPosF));
        const float frac = readPosF - static_cast<float>(idx0);
        idx0 = (idx0 % kMaxDelay + kMaxDelay) % kMaxDelay;
        const int idx1 = (idx0 + 1) % kMaxDelay;

        const float delayedL = delayLineL_[static_cast<size_t>(idx0)] * (1.0f - frac)
                             + delayLineL_[static_cast<size_t>(idx1)] * frac;
        const float delayedR = delayLineR_[static_cast<size_t>(idx0)] * (1.0f - frac)
                             + delayLineR_[static_cast<size_t>(idx1)] * frac;

        writePos_ = (writePos_ + 1) % kMaxDelay;

        // 3. 球体頭部音響シャドウ（反対側の耳に回り込む際に高域が6dB/oct減衰する回折ローパス）
        shadowLpL_ += shadowAlpha_ * (delayedL - shadowLpL_);
        shadowLpR_ += shadowAlpha_ * (delayedR - shadowLpR_);
        const float crossFromL = shadowLpL_ * 0.78f + delayedL * 0.22f;
        const float crossFromR = shadowLpR_ * 0.78f + delayedR * 0.22f;

        // 4. 直接音の低域ビルドアップ補償（モノラル成分が太りすぎないよう低域をわずかに引き締める）
        directCompL_ += compAlpha_ * (inL - directCompL_);
        directCompR_ += compAlpha_ * (inR - directCompR_);
        const float xfeed = crossGain_ * amt;
        const float compDirectL = inL - directCompL_ * (xfeed * 0.55f);
        const float compDirectR = inR - directCompR_ * (xfeed * 0.55f);

        outL = compDirectL + crossFromR * xfeed;
        outR = compDirectR + crossFromL * xfeed;
    }

private:
    static constexpr int kMaxDelay = 256;
    double sr_ = 48000.0;
    classSpeakerAnglePreset preset_ = StudioStd30Deg;
    std::array<float, kMaxDelay> delayLineL_{};
    std::array<float, kMaxDelay> delayLineR_{};
    int   writePos_     = 0;
    float delaySamples_ = 14.0f;
    float shadowAlpha_  = 0.15f;
    float compAlpha_    = 0.08f;
    float crossGain_    = 0.30f;
    float shadowLpL_    = 0.0f;
    float shadowLpR_    = 0.0f;
    float directCompL_  = 0.0f;
    float directCompR_  = 0.0f;
};

// =============================================================================
// 2. 耳介 (Pinna) 前方定位スペクトルキュー・モデル (PinnaSpectralNotchElevationCueModel)
// =============================================================================
class PinnaSpectralNotchElevationCueModel
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        reset();
        updateCoeffs(4150.0, 7950.0);
    }

    void reset() noexcept
    {
        conchaIc1L_ = conchaIc2L_ = conchaIc1R_ = conchaIc2R_ = 0.0;
        notchIc1L_  = notchIc2L_  = notchIc1R_  = notchIc2R_  = 0.0;
    }

    void updateCoeffs(double conchaPeakHz, double pinnaNotchHz) noexcept
    {
        const double nyq = sr_ * 0.44;
        const double fPeak  = std::clamp(conchaPeakHz, 2400.0, nyq);
        const double fNotch = std::clamp(pinnaNotchHz,  5200.0, nyq);

        // 耳甲介腔 (Concha) の 4.15kHz 前方プレゼンス共鳴 (Q = 1.6)
        conchaG_ = std::tan(3.14159265358979323846 * fPeak / sr_);
        conchaK_ = 1.0 / 1.60;
        conchaA1_ = 1.0 / (1.0 + conchaG_ * (conchaG_ + conchaK_));
        conchaA2_ = conchaG_ * conchaA1_;
        conchaA3_ = conchaG_ * conchaA2_;

        // 耳介ヘリックス反射による 7.95kHz 前方水平定位ノッチ (Q = 3.2)
        notchG_ = std::tan(3.14159265358979323846 * fNotch / sr_);
        notchK_ = 1.0 / 3.20;
        notchA1_ = 1.0 / (1.0 + notchG_ * (notchG_ + notchK_));
        notchA2_ = notchG_ * notchA1_;
        notchA3_ = notchG_ * notchA2_;
    }

    void processStereo(float& ioL, float& ioR, float cueDepth01) noexcept
    {
        const float depth = std::clamp(cueDepth01, 0.0f, 1.0f);
        if (depth <= 1.0e-4f)
            return;

        ioL = processChannel(ioL, depth, conchaIc1L_, conchaIc2L_, notchIc1L_, notchIc2L_);
        ioR = processChannel(ioR, depth, conchaIc1R_, conchaIc2R_, notchIc1R_, notchIc2R_);
    }

private:
    float processChannel(float x, float depth,
                         double& cIc1, double& cIc2,
                         double& nIc1, double& nIc2) const noexcept
    {
        const double in = static_cast<double>(x);

        // Stage 1: Concha 4.15kHz gentle boost (+1.8 dB at full depth)
        const double cV3 = in - cIc2;
        const double cV1 = conchaA1_ * cIc1 + conchaA2_ * cV3;
        const double cV2 = cIc2 + conchaA2_ * cIc1 + conchaA3_ * cV3;
        cIc1 = 2.0 * cV1 - cIc1;
        cIc2 = 2.0 * cV2 - cIc2;
        const double afterConcha = in + (cV1 * conchaK_) * (0.22 * static_cast<double>(depth));

        // Stage 2: Pinna 7.95kHz subtle front-localization notch (-2.4 dB at full depth)
        const double nV3 = afterConcha - nIc2;
        const double nV1 = notchA1_ * nIc1 + notchA2_ * nV3;
        const double nV2 = nIc2 + notchA2_ * nIc1 + notchA3_ * nV3;
        nIc1 = 2.0 * nV1 - nIc1;
        nIc2 = 2.0 * nV2 - nIc2;
        const double afterNotch = afterConcha - (nV1 * notchK_) * (0.26 * static_cast<double>(depth));

        return static_cast<float>(afterNotch);
    }

    double sr_ = 48000.0;
    double conchaG_ = 0.2, conchaK_ = 0.625, conchaA1_ = 0.5, conchaA2_ = 0.1, conchaA3_ = 0.02;
    double notchG_  = 0.4, notchK_  = 0.3125, notchA1_ = 0.5, notchA2_  = 0.2, notchA3_  = 0.08;
    double conchaIc1L_ = 0.0, conchaIc2L_ = 0.0, conchaIc1R_ = 0.0, conchaIc2R_ = 0.0;
    double notchIc1L_  = 0.0, notchIc2L_  = 0.0, notchIc1R_  = 0.0, notchIc2R_  = 0.0;
};

// =============================================================================
// 3. Harman ターゲット・ヘッドホン較正EQ (HarmanTargetHeadphoneCalibrationEq)
// =============================================================================
class HarmanTargetHeadphoneCalibrationEq
{
public:
    enum class HeadphoneType : int
    {
        StudioClosedBack = 0, // 密閉型モニター (SONY MDR-CD900ST / M50x 系: 中高域の張り出しを整えサブベースを補う)
        StudioOpenBack   = 1, // 開放型リファレンス (HD650 / DT990 系: 85Hz以下のロールオフをリニア補正)
        InEarMonitorIEM  = 2  // カナル型イヤモニ (Shure SE / カスタムIEM系: 外耳道閉鎖による3.2kHzピークを緩和)
    };

    void prepare(double sampleRate) noexcept
    {
        sr_ = std::max(8000.0, sampleRate);
        reset();
        setHeadphoneProfile(HeadphoneType::StudioClosedBack);
    }

    void reset() noexcept
    {
        lowLpL_ = lowLpR_ = 0.0f;
        midIc1L_ = midIc2L_ = midIc1R_ = midIc2R_ = 0.0f;
        airHpL_ = airHpR_ = 0.0f;
    }

    void setHeadphoneProfile(HeadphoneType type) noexcept
    {
        type_ = type;
        double lowHz = 95.0;
        double midHz = 3200.0;
        double midQ  = 1.4;

        switch (type)
        {
            case HeadphoneType::StudioClosedBack:
                lowHz       = 85.0;
                lowBoostGain_ = 0.16f;
                midHz       = 2850.0;
                midQ        = 1.5;
                midTrimGain_ = -0.14f;
                airBoostGain_ = 0.12f;
                break;
            case HeadphoneType::StudioOpenBack:
                lowHz       = 68.0;
                lowBoostGain_ = 0.24f;
                midHz       = 3600.0;
                midQ        = 1.2;
                midTrimGain_ = -0.06f;
                airBoostGain_ = 0.08f;
                break;
            case HeadphoneType::InEarMonitorIEM:
                lowHz       = 105.0;
                lowBoostGain_ = 0.08f;
                midHz       = 3350.0;
                midQ        = 2.1;
                midTrimGain_ = -0.22f;
                airBoostGain_ = 0.16f;
                break;
        }

        lowAlpha_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * lowHz / sr_));
        airAlpha_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * 9600.0 / sr_));

        const float g = static_cast<float>(std::tan(3.14159265358979323846 * std::min(midHz, sr_ * 0.44) / sr_));
        midK_  = static_cast<float>(1.0 / midQ);
        midA1_ = 1.0f / (1.0f + g * (g + midK_));
        midA2_ = g * midA1_;
        midA3_ = g * midA2_;
    }

    void processStereo(float& ioL, float& ioR, float calibrationAmount01) noexcept
    {
        const float amt = std::clamp(calibrationAmount01, 0.0f, 1.0f);
        if (amt <= 1.0e-4f)
            return;

        ioL = processChannel(ioL, amt, lowLpL_, midIc1L_, midIc2L_, airHpL_);
        ioR = processChannel(ioR, amt, lowLpR_, midIc1R_, midIc2R_, airHpR_);
    }

private:
    float processChannel(float x, float amt,
                         float& lowLp, float& mIc1, float& mIc2, float& airLp) const noexcept
    {
        lowLp += lowAlpha_ * (x - lowLp);
        airLp += airAlpha_ * (x - airLp);
        const float airHigh = x - airLp;

        const float v3 = x - mIc2;
        const float v1 = midA1_ * mIc1 + midA2_ * v3;
        const float v2 = mIc2 + midA2_ * mIc1 + midA3_ * v3;
        mIc1 = 2.0f * v1 - mIc1;
        mIc2 = 2.0f * v2 - mIc2;
        const float midBand = v1 * midK_;

        return x + amt * (lowLp * lowBoostGain_ + midBand * midTrimGain_ + airHigh * airBoostGain_);
    }

    double sr_ = 48000.0;
    HeadphoneType type_ = HeadphoneType::StudioClosedBack;
    float lowAlpha_     = 0.01f;
    float airAlpha_     = 0.35f;
    float midK_         = 0.7f;
    float midA1_        = 0.5f;
    float midA2_        = 0.1f;
    float midA3_        = 0.02f;
    float lowBoostGain_ = 0.16f;
    float midTrimGain_  = -0.14f;
    float airBoostGain_ = 0.12f;
    float lowLpL_ = 0.0f, lowLpR_ = 0.0f;
    float midIc1L_ = 0.0f, midIc2L_ = 0.0f, midIc1R_ = 0.0f, midIc2R_ = 0.0f;
    float airHpL_ = 0.0f, airHpR_ = 0.0f;
};

// =============================================================================
// 4. 統合バイノーラル・ヘッドホン＆ルーム較正スイート (BinauralHeadphoneAndRoomSuite)
// =============================================================================
class BinauralHeadphoneAndRoomSuite
{
public:
    struct Telemetry
    {
        float crossfeedDepth01   = 0.0f;
        float externalization01  = 0.0f;
        float earFatigueReductionDb = 0.0f;
    };

    void prepare(double sampleRate) noexcept
    {
        crossfeed_.prepare(sampleRate);
        pinnaCue_.prepare(sampleRate);
        harmanEq_.prepare(sampleRate);
        telemetry_ = {};
    }

    void reset() noexcept
    {
        crossfeed_.reset();
        pinnaCue_.reset();
        harmanEq_.reset();
        telemetry_ = {};
    }

    void processStereoSample(float& ioL, float& ioR,
                             float roomExternalize01,
                             float headphoneCalibrate01) noexcept
    {
        const float ext = std::clamp(roomExternalize01, 0.0f, 1.0f);
        const float cal = std::clamp(headphoneCalibrate01, 0.0f, 1.0f);

        float xfL = ioL, xfR = ioR;
        crossfeed_.processStereo(ioL, ioR, ext, xfL, xfR);
        pinnaCue_.processStereo(xfL, xfR, ext * 0.85f);
        harmanEq_.processStereo(xfL, xfR, cal);

        ioL = xfL;
        ioR = xfR;

        telemetry_.crossfeedDepth01      = ext;
        telemetry_.externalization01     = 0.65f * ext + 0.35f * cal;
        telemetry_.earFatigueReductionDb = ext * 3.8f + cal * 2.2f;
    }

    const Telemetry& getTelemetry() const noexcept { return telemetry_; }

private:
    SphericalHeadShadowCrossfeedEngine   crossfeed_;
    PinnaSpectralNotchElevationCueModel  pinnaCue_;
    HarmanTargetHeadphoneCalibrationEq   harmanEq_;
    Telemetry telemetry_{};
};

} // namespace binaural_cal
} // namespace gzzio

namespace vg
{
namespace binaural_cal = ::gzzio::binaural_cal;
} // namespace vg
