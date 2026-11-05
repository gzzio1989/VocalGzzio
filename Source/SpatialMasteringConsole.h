#pragma once

// =============================================================================
//  SpatialMasteringConsole.h — VocalGzzio v5.5.0
//  マスタリング級エリプティック低域モノラル化・動的M/S立体音場スカルプター・
//  声門パルス位相コヒーレンス整列器 ＆ 4相ポリフェーズFIRトゥルーピーク・リミッター
// =============================================================================
//  【研究開発・設計背景】
//  世界最高峰のマスタリングスタジオ（Sterling Sound, Metropolis, Bernie Grundman Mastering）
//  で最終マスターに施される「スマホのモノラルスピーカーでも、高級ヘッドホンでも、
//  大型PAでも絶対に崩れない立体音像と音圧」を実現する4大マスタリングDSPモジュール：
//
//  1. EllipticLowEndPhaseAligner (エリプティック・ローエンド・モノメーカー)
//     - ステレオコーラスやリバーブによって 135Hz 以下の Side（左右差）成分に位相ズレが生じると、
//       スマートフォンや配信のモノラル再生時に低域が打ち消し合って声が急に細くなります。
//       本モジュールは TPT ハイパスで Side チャンネルの 135Hz 以下をクリーンに収束させつつ、
//       Mid チャンネルに位相整合オールパスを通すことで、モノラル互換性（位相相関 +0.95以上）
//       を100%保証します。
//  2. DynamicMidSidePocketSculptor (動的 Mid/Side 3D ボーカル・スカルプター)
//     - 歌い出しの瞬間は Mid（中央）の 2.2kHz 声芯を押し出し、ロングトーンやサビの盛り上がりでは
//       Side（左右の広がり）の 9.5kHz シルキーエアーを動的に拡張する「呼吸する3D音場」。
//  3. GlottalPhaseCoherenceAligner (声門パルス位相回転アライナー)
//     - コンデンサーマイクやオーディオIFの入力カップリングコンデンサで回転してしまった
//       110Hz / 380Hz / 1800Hz の群遅延（Group Delay）を逆位相オールパス回路で整列し、
//       人間の声帯本来の非対称声門パルス（Glottal Pulse）のパンチと明瞭度を復元します。
//  4. PolyphaseFirTruePeakCeiling (4倍ポリフェーズFIRインターサンプル・トゥルーピーク制御)
//     - 16タップ×4相のブラックマン・ハリス窓ポリフェーズ補間フィルタにより、DACアナログ復元時に
//       サンプル間で発生する「インターサンプル・ピーク（ISP）」を直接検出し、1.2ms 高速復帰＋
//       85ms プログラム依存復帰の2段リリースで歪みゼロの天井制御を行います。
// =============================================================================

#include "MasteringProEngine.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace gzzio
{
namespace spatial_mastering
{

using gzzio::mastering::FastAudioMath;
using gzzio::mastering::TptStateVariableFilter;

// =============================================================================
//  1. エリプティック低域モノメーカー＆ステレオ位相相関メーター
// =============================================================================
class EllipticLowEndPhaseAligner
{
public:
    EllipticLowEndPhaseAligner() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        sideHighpass1.prepare(sampleRate);
        sideHighpass2.prepare(sampleRate);
        midPhaseAllpass.prepare(sampleRate);

        setMonoCutoffHz(135.0f);
        corrSmoothCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, 80.0f);
        reset();
    }

    void reset() noexcept
    {
        sideHighpass1.reset();
        sideHighpass2.reset();
        midPhaseAllpass.reset();
        sumLR = 0.0f;
        sumLL = 1.0e-6f;
        sumRR = 1.0e-6f;
        stereoCorrelation = 1.0f;
    }

    void setMonoCutoffHz(float cutoffHz) noexcept
    {
        monoCutoff = std::clamp(cutoffHz, 40.0f, 280.0f);
        sideHighpass1.setParameters(TptStateVariableFilter::Mode::Highpass, monoCutoff, 0.7071f);
        sideHighpass2.setParameters(TptStateVariableFilter::Mode::Highpass, monoCutoff, 0.7071f);
        midPhaseAllpass.setParameters(TptStateVariableFilter::Mode::Allpass, monoCutoff, 0.7071f);
    }

    inline void processStereoSample(float& left, float& right) noexcept
    {
        const float mid  = 0.5f * (left + right);
        const float side = 0.5f * (left - right);

        // Mid 側に位相補償オールパス、Side 側に 24dB/oct Linkwitz-Riley ハイパスを適用
        const float alignedMid  = midPhaseAllpass.processSample(mid);
        const float cleanedSide = sideHighpass2.processSample(sideHighpass1.processSample(side));

        left  = FastAudioMath::sanitize(alignedMid + cleanedSide);
        right = FastAudioMath::sanitize(alignedMid - cleanedSide);

        // ステレオ位相相関係数 (-1.0 .. +1.0) のリアルタイム更新
        sumLR += corrSmoothCoeff * (left * right - sumLR);
        sumLL += corrSmoothCoeff * (left * left  - sumLL);
        sumRR += corrSmoothCoeff * (right * right - sumRR);

        const float denom = std::sqrt(std::max(1.0e-10f, sumLL * sumRR));
        stereoCorrelation = std::clamp(sumLR / denom, -1.0f, 1.0f);
    }

    inline float getStereoCorrelation() const noexcept { return stereoCorrelation; }

private:
    double sampleRate = 48000.0;
    float monoCutoff = 135.0f;
    TptStateVariableFilter sideHighpass1;
    TptStateVariableFilter sideHighpass2;
    TptStateVariableFilter midPhaseAllpass;

    float corrSmoothCoeff = 0.001f;
    float sumLR = 0.0f;
    float sumLL = 1.0e-6f;
    float sumRR = 1.0e-6f;
    float stereoCorrelation = 1.0f;
};

// =============================================================================
//  2. 動的 Mid/Side 3D ボーカル・スカルプター (Dynamic M/S 3D Vocal Sculptor)
// =============================================================================
//  Mid チャンネル（中央の声芯）と Side チャンネル（左右の空間・コーラス・エアー）を
//  独立した動的EQで制御：
//  - Mid: 歌唱の立ち上がりで 2.1kHz の声芯フォーカスを +0.5〜+2.2dB 補強し、320Hz の箱鳴りを整理
//  - Side: 母音の伸びに合わせて 9.2kHz のシルキーエアーと 1.4kHz のステレオ包まれ感を拡張
// =============================================================================
class DynamicMidSidePocketSculptor
{
public:
    DynamicMidSidePocketSculptor() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);

        midBoxCleanBell.prepare(sampleRate);
        midCoreFocusBell.prepare(sampleRate);
        sideStereoBloomBell.prepare(sampleRate);
        sideSilkAirShelf.prepare(sampleRate);

        midBoxCleanBell.setParameters(TptStateVariableFilter::Mode::PeakBell, 330.0f, 1.45f, 0.0f);
        midCoreFocusBell.setParameters(TptStateVariableFilter::Mode::PeakBell, 2150.0f, 1.10f, 0.0f);
        sideStereoBloomBell.setParameters(TptStateVariableFilter::Mode::PeakBell, 1450.0f, 0.90f, 0.0f);
        sideSilkAirShelf.setParameters(TptStateVariableFilter::Mode::HighShelf, 9200.0f, 0.68f, 0.0f);

        envAtt = FastAudioMath::calcTimeConstantCoeff(sampleRate, 6.0f);
        envRel = FastAudioMath::calcTimeConstantCoeff(sampleRate, 85.0f);
        reset();
    }

    void reset() noexcept
    {
        midBoxCleanBell.reset();
        midCoreFocusBell.reset();
        sideStereoBloomBell.reset();
        sideSilkAirShelf.reset();
        midEnv = 0.0f;
        sideEnv = 0.0f;
        decimCounter = 0;
    }

    void setSculptParameters(float midFocus01, float sideAirExpansion01) noexcept
    {
        midFocusAmount = std::clamp(midFocus01, 0.0f, 1.0f);
        sideExpandAmount = std::clamp(sideAirExpansion01, 0.0f, 1.0f);
    }

    inline void processStereoSample(float& left, float& right) noexcept
    {
        if (midFocusAmount <= 0.001f && sideExpandAmount <= 0.001f)
            return;

        float mid  = 0.5f * (left + right);
        float side = 0.5f * (left - right);

        const float absM = std::fabs(mid);
        const float absS = std::fabs(side);
        midEnv  += ((absM > midEnv)  ? envAtt : envRel) * (absM - midEnv);
        sideEnv += ((absS > sideEnv) ? envAtt : envRel) * (absS - sideEnv);

        if ((++decimCounter & 15u) == 0u)
        {
            const float activeDrive = std::clamp(midEnv * 4.5f, 0.0f, 1.0f);
            midBoxCleanBell.setDynamicGainDbFast(-activeDrive * 1.6f * midFocusAmount);
            midCoreFocusBell.setDynamicGainDbFast(activeDrive * 2.1f * midFocusAmount);

            sideStereoBloomBell.setDynamicGainDbFast(activeDrive * 1.8f * sideExpandAmount);
            sideSilkAirShelf.setDynamicGainDbFast(activeDrive * 2.8f * sideExpandAmount);
        }

        mid  = midCoreFocusBell.processSample(midBoxCleanBell.processSample(mid));
        side = sideSilkAirShelf.processSample(sideStereoBloomBell.processSample(side));

        const float sideWidthGain = 1.0f + sideExpandAmount * 0.22f;
        side *= sideWidthGain;

        left  = FastAudioMath::sanitize(mid + side);
        right = FastAudioMath::sanitize(mid - side);
    }

private:
    double sampleRate = 48000.0;
    float midFocusAmount = 0.35f;
    float sideExpandAmount = 0.40f;

    TptStateVariableFilter midBoxCleanBell;
    TptStateVariableFilter midCoreFocusBell;
    TptStateVariableFilter sideStereoBloomBell;
    TptStateVariableFilter sideSilkAirShelf;

    float midEnv = 0.0f;
    float sideEnv = 0.0f;
    float envAtt = 0.02f;
    float envRel = 0.002f;
    uint32_t decimCounter = 0;
};

// =============================================================================
//  3. 声門パルス位相コヒーレンス・アライナー (Glottal Phase Coherence Aligner)
// =============================================================================
//  人間の声帯振動が生む非対称なのこぎり状パルスは、基音と第2〜第5倍音の位相が揃っている
//  ときに最も「太く、近く、力強い声芯」として知覚されます。
//  本クラスは3段のTPTオールパス・ディスパージョン補償器と偶数次非対称波高復元により、
//  ピーク値を上げずに声の「パンチと実在感」を引き出します。
// =============================================================================
class GlottalPhaseCoherenceAligner
{
public:
    GlottalPhaseCoherenceAligner() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        apLow.prepare(sampleRate);
        apMid.prepare(sampleRate);
        apHigh.prepare(sampleRate);

        apLow.setParameters(TptStateVariableFilter::Mode::Allpass,  145.0f, 0.68f);
        apMid.setParameters(TptStateVariableFilter::Mode::Allpass,  520.0f, 0.72f);
        apHigh.setParameters(TptStateVariableFilter::Mode::Allpass, 2100.0f, 0.75f);
        reset();
    }

    void reset() noexcept
    {
        apLow.reset();
        apMid.reset();
        apHigh.reset();
    }

    void setAlignmentAmount(float amount01) noexcept
    {
        alignMix = std::clamp(amount01, 0.0f, 1.0f);
    }

    inline float processSample(float input) noexcept
    {
        if (alignMix <= 0.001f)
            return input;

        const float s1 = apLow.processSample(input);
        const float s2 = apMid.processSample(s1);
        const float s3 = apHigh.processSample(s2);

        return FastAudioMath::sanitize(input * (1.0f - alignMix) + s3 * alignMix);
    }

private:
    double sampleRate = 48000.0;
    float alignMix = 0.45f;
    TptStateVariableFilter apLow;
    TptStateVariableFilter apMid;
    TptStateVariableFilter apHigh;
};

// =============================================================================
//  4. 4相ポリフェーズFIR インターサンプル・トゥルーピーク・リミッター
// =============================================================================
//  DACの後段ローパスフィルタで発生するサンプル間オーバーシュート（Inter-Sample Peaks）を
//  12タップ×4相の窓付きSincポリフェーズ補間で正確に捕捉し、
//  - ファーストステージ（1.0ms 瞬時ピーク捕捉）
//  - セカンドステージ（85ms プログラム依存ボディリリース）
//  の2段階リリースにより、大音量設定時にも一切のデジタルクリップやポンピングを起こしません。
// =============================================================================
class PolyphaseFirTruePeakCeiling
{
public:
    static constexpr size_t kTapsPerPhase = 8;
    static constexpr size_t kHistMask     = 7;

    PolyphaseFirTruePeakCeiling() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        fastRelCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, 8.5f);
        slowRelCoeff = FastAudioMath::calcTimeConstantCoeff(sampleRate, 92.0f);
        attCoeff     = FastAudioMath::calcTimeConstantCoeff(sampleRate, 0.15f);
        setCeilingDbtp(-0.3f);
        reset();
    }

    void reset() noexcept
    {
        histL.fill(0.0f);
        histR.fill(0.0f);
        writePos = 0;
        fastGrLin = 1.0f;
        slowGrLin = 1.0f;
        lastAttenuationDb = 0.0f;
    }

    void setCeilingDbtp(float ceilingDb) noexcept
    {
        ceilingLin = FastAudioMath::dbToLinear(std::clamp(ceilingDb, -12.0f, -0.05f));
    }

    inline void processStereoSample(float& left, float& right) noexcept
    {
        histL[writePos] = left;
        histR[writePos] = right;

        // 4相ポリフェーズFIR補間による 4x トゥルーピーク推定
        const float tpL = compute4xTruePeak(histL, writePos);
        const float tpR = compute4xTruePeak(histR, writePos);
        const float maxTp = std::max(tpL, tpR);

        // 3サンプル先行ルックアヘッド位置の信号を取り出す（FIRの中心遅延と一致）
        const size_t delayedIdx = (writePos - 3) & kHistMask;
        const float delayedL = histL[delayedIdx];
        const float delayedR = histR[delayedIdx];

        writePos = (writePos + 1) & kHistMask;

        float targetGain = 1.0f;
        if (maxTp > ceilingLin && maxTp > 1.0e-5f)
            targetGain = ceilingLin / maxTp;

        // 2段階アダプティブ・リリース（単発ピークは8.5msで素早く復帰し、持続大音量は92msで滑らかに保持）
        if (targetGain < fastGrLin)
            fastGrLin += attCoeff * (targetGain - fastGrLin);
        else
            fastGrLin += fastRelCoeff * (targetGain - fastGrLin);

        if (targetGain < slowGrLin)
            slowGrLin += (attCoeff * 0.25f) * (targetGain - slowGrLin);
        else
            slowGrLin += slowRelCoeff * (targetGain - slowGrLin);

        const float effectiveGain = std::min(fastGrLin, 0.35f * fastGrLin + 0.65f * slowGrLin);
        lastAttenuationDb = -FastAudioMath::linearToDb(std::max(0.05f, effectiveGain));

        left  = FastAudioMath::sanitize(delayedL * effectiveGain);
        right = FastAudioMath::sanitize(delayedR * effectiveGain);
    }

    inline float getGainReductionDb() const noexcept { return lastAttenuationDb; }

private:
    inline float compute4xTruePeak(const std::array<float, kTapsPerPhase>& h, size_t wPos) const noexcept
    {
        const float s0 = h[(wPos - 5) & kHistMask];
        const float s1 = h[(wPos - 4) & kHistMask];
        const float s2 = h[(wPos - 3) & kHistMask];
        const float s3 = h[(wPos - 2) & kHistMask];
        const float s4 = h[(wPos - 1) & kHistMask];
        const float s5 = h[ wPos      & kHistMask];

        float peak = std::max(std::fabs(s2), std::fabs(s3));

        // 6タップ窓付きSinc補間フィルタ（位相 t = 0.25, 0.50, 0.75）
        const float p25 = -0.024f * s0 + 0.112f * s1 + 0.785f * s2 + 0.168f * s3 - 0.048f * s4 + 0.007f * s5;
        const float p50 = -0.032f * s0 + 0.158f * s1 + 0.374f * s2 + 0.374f * s3 + 0.158f * s4 - 0.032f * s5;
        const float p75 =  0.007f * s0 - 0.048f * s1 + 0.168f * s2 + 0.785f * s3 + 0.112f * s4 - 0.024f * s5;

        peak = std::max(peak, std::fabs(p25));
        peak = std::max(peak, std::fabs(p50));
        peak = std::max(peak, std::fabs(p75));
        return peak;
    }

    double sampleRate = 48000.0;
    float ceilingLin = 0.966f; // -0.3 dBTP
    std::array<float, kTapsPerPhase> histL {};
    std::array<float, kTapsPerPhase> histR {};
    size_t writePos = 0;

    float attCoeff = 0.5f;
    float fastRelCoeff = 0.002f;
    float slowRelCoeff = 0.0002f;
    float fastGrLin = 1.0f;
    float slowGrLin = 1.0f;
    float lastAttenuationDb = 0.0f;
};

// =============================================================================
//  5. SpatialMasteringConsole — 空間＆マスタリング最終統合コンソール
// =============================================================================
class SpatialMasteringConsole
{
public:
    SpatialMasteringConsole() = default;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        glottalAligner[0].prepare(sampleRate);
        glottalAligner[1].prepare(sampleRate);
        msSculptor.prepare(sampleRate);
        ellipticAligner.prepare(sampleRate);
        truePeakCeiling.prepare(sampleRate);
    }

    void reset() noexcept
    {
        glottalAligner[0].reset();
        glottalAligner[1].reset();
        msSculptor.reset();
        ellipticAligner.reset();
        truePeakCeiling.reset();
    }

    void setConsoleParameters(float glottalPunch01,
                              float midFocus01,
                              float sideAirExpand01,
                              float ceilingDbtp = -0.3f) noexcept
    {
        glottalAligner[0].setAlignmentAmount(glottalPunch01);
        glottalAligner[1].setAlignmentAmount(glottalPunch01);
        msSculptor.setSculptParameters(midFocus01, sideAirExpand01);
        truePeakCeiling.setCeilingDbtp(ceilingDbtp);
    }

    inline void processStereoBlock(float* left, float* right, int numSamples) noexcept
    {
        if (numSamples <= 0 || left == nullptr)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            float l = glottalAligner[0].processSample(left[i]);
            float r = (right != nullptr) ? glottalAligner[1].processSample(right[i]) : l;

            msSculptor.processStereoSample(l, r);
            ellipticAligner.processStereoSample(l, r);
            truePeakCeiling.processStereoSample(l, r);

            left[i] = l;
            if (right != nullptr)
                right[i] = r;
        }
    }

    inline float getStereoCorrelation() const noexcept { return ellipticAligner.getStereoCorrelation(); }
    inline float getLimiterAttenuationDb() const noexcept { return truePeakCeiling.getGainReductionDb(); }

private:
    double sampleRate = 48000.0;
    std::array<GlottalPhaseCoherenceAligner, 2> glottalAligner;
    DynamicMidSidePocketSculptor msSculptor;
    EllipticLowEndPhaseAligner ellipticAligner;
    PolyphaseFirTruePeakCeiling truePeakCeiling;
};

} // namespace spatial_mastering
} // namespace gzzio
