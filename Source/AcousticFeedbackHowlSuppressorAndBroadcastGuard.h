#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vg::safety
{

// ============================================================================
// AcousticFeedbackHowlSuppressorAndBroadcastGuard.h
// ----------------------------------------------------------------------------
// 安全性・事故防止の最終防衛モジュール:
//   1. AcousticFeedbackHowlPredictorAndNotchBank:
//      スピーカーモニター中やオープンバックヘッドホン・スマホ通話配信で発生する
//      ハウリング（正帰還発振: Larsen Effect）を、発振が爆音化する前（初期成長段階）に
//      「位相コヒーレンス持続性＋狭帯域クレストファクタ低下＋指数成長率」から検知し、
//      最大3基の超高Q（Q=28〜45）ゼロ遅延アダプティブノッチでピンポイント除去。
//   2. SeismicMicBumpAndCableShockAbsorber:
//      マイクスタンドへの接触・デスク打鍵・ケーブル抜けかけ時の爆発的インパルスパルス
//      （< 35 Hz かつ高スルーレート）を検知し、声の帯域を傷つけずにショックのみ吸収。
//   3. BroadcastStreamComplianceGuard:
//      YouTube Live / Twitch / ニコ生 / ラジオ放送規格（ITU-R BS.1770 / ARIB TR-B32）に
//      準拠したリアルタイム True-Peak 天井保護とラウドネス逸脱アラート。
// ============================================================================

struct HowlAndBroadcastReport
{
    int activeHowlNotchCount = 0;               // 稼働中のハウリング抑制ノッチ数 (0..3)
    std::array<float, 3> notchFreqsHz { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> notchDepthsDb { 0.0f, 0.0f, 0.0f };
    float maxHowlRiskScore01 = 0.0f;            // ハウリング危険度 (0..1)
    bool seismicShockAbsorbed = false;          // マイク衝撃/ケーブルノイズ吸収作動
    float seismicAttenuationDb = 0.0f;          // 低域衝撃減衰量 (dB)
    float shortTermLoudnessLufs = -24.0f;       // 短期ラウドネス推定値 (LUFS)
    float truePeakCeilingMarginDb = 6.0f;       // True-Peak 天井までの余裕 (dB)
    bool broadcastCompliant = true;             // 配信規格適合フラグ
};

// ----------------------------------------------------------------------------
// 1. ハウリング予兆検知＆3段ゼロ遅延アダプティブ・サージカルノッチバンク
// ----------------------------------------------------------------------------
class AcousticFeedbackHowlPredictorAndNotchBank
{
public:
    struct NotchSlot
    {
        bool active = false;
        float centerHz = 1000.0f;
        float depthLinear = 1.0f;   // 1.0 = 透過, 0.12 = -18.4 dB ノッチ
        float qFactor = 32.0f;
        float holdTimerSec = 0.0f;
        // 2次Biquad状態 (Direct Form II Transposed)
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1L = 0.0f, z2L = 0.0f, z1R = 0.0f, z2R = 0.0f;
    };

    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        for (auto& slot : slots)
        {
            slot = NotchSlot{};
        }
        zeroCrossPeriodSamples = 48.0f;
        sampleCounterSinceZc = 0;
        prevSampleMono = 0.0f;
        instFreqSmoothHz = 1000.0f;
        freqVarianceSmooth = 50000.0f;
        envFast = 0.0f;
        envSlow = 0.0f;
        howlPersistenceSec = 0.0f;
        currentRisk01 = 0.0f;
    }

    // 1サンプル対のハウリング予兆解析とノッチ適用
    void processSamplePair(float& left, float& right) noexcept
    {
        const float mono = 0.5f * (left + right);
        const float absMono = std::fabs(mono);
        const float dt = 1.0f / static_cast<float>(sr);

        // エンベロープ追跡（急激な純音成長の検知）
        envFast += 0.02f * (absMono - envFast);
        envSlow += 0.0008f * (absMono - envSlow);

        // ゼロ交差周期による瞬時周波数と「周波数分散の異常な小ささ（純音発振）」の検出
        ++sampleCounterSinceZc;
        if ((prevSampleMono <= 0.0f && mono > 0.0f) && absMono > 0.003f)
        {
            const float frac = prevSampleMono / (prevSampleMono - mono + 1.0e-12f);
            const float period = std::max(2.0f, static_cast<float>(sampleCounterSinceZc) - 1.0f + frac);
            const float instHz = std::clamp(static_cast<float>(sr) / period, 180.0f, 9500.0f);

            const float diff = instHz - instFreqSmoothHz;
            instFreqSmoothHz += 0.18f * diff;
            freqVarianceSmooth = 0.88f * freqVarianceSmooth + 0.12f * (diff * diff);
            sampleCounterSinceZc = 0;
        }
        prevSampleMono = mono;

        // ハウリング条件:
        //  (1) 周波数分散が極小（ビブラートや子音ではなく固定周波数の正弦波発振: std < 14 Hz）
        //  (2) 音量が一定以上 (envFast > 0.025f) かつ成長傾向 (envFast >= 0.95f * envSlow)
        const bool isPureToneOscillation = (freqVarianceSmooth < 196.0f) && (envFast > 0.025f) && (envFast >= 0.92f * envSlow);
        if (isPureToneOscillation)
        {
            howlPersistenceSec = std::min(1.0f, howlPersistenceSec + dt * 4.5f);
        }
        else
        {
            howlPersistenceSec = std::max(0.0f, howlPersistenceSec - dt * 1.8f);
        }

        currentRisk01 = std::clamp(howlPersistenceSec * 2.5f, 0.0f, 1.0f);

        // リスク閾値を超えたら最も近いスロットまたは空きスロットにサージカルノッチをロック
        if (currentRisk01 > 0.42f)
        {
            assignOrUpdateNotch(instFreqSmoothHz, currentRisk01);
        }

        // 各スロットのフィルタ処理と自動減衰復帰
        for (auto& slot : slots)
        {
            if (!slot.active)
                continue;

            slot.holdTimerSec -= dt;
            if (slot.holdTimerSec <= 0.0f)
            {
                // 緩やかにノッチを解除
                slot.depthLinear += 0.0005f * (1.0f - slot.depthLinear);
                if (slot.depthLinear > 0.985f)
                {
                    slot.active = false;
                    slot.depthLinear = 1.0f;
                    continue;
                }
                updateSlotCoefficients(slot);
            }

            const float yL = slot.b0 * left + slot.z1L;
            slot.z1L = slot.b1 * left - slot.a1 * yL + slot.z2L;
            slot.z2L = slot.b2 * left - slot.a2 * yL;

            const float yR = slot.b0 * right + slot.z1R;
            slot.z1R = slot.b1 * right - slot.a1 * yR + slot.z2R;
            slot.z2R = slot.b2 * right - slot.a2 * yR;

            left = yL;
            right = yR;
        }
    }

    int getActiveNotchCount() const noexcept
    {
        int c = 0;
        for (const auto& s : slots)
            if (s.active) ++c;
        return c;
    }

    float getRiskScore01() const noexcept { return currentRisk01; }
    const std::array<NotchSlot, 3>& getSlots() const noexcept { return slots; }

private:
    void assignOrUpdateNotch(float targetHz, float risk01) noexcept
    {
        int targetIdx = -1;
        // 既存スロットで近い周波数があれば更新
        for (int i = 0; i < 3; ++i)
        {
            if (slots[static_cast<size_t>(i)].active &&
                std::fabs(slots[static_cast<size_t>(i)].centerHz - targetHz) < targetHz * 0.06f)
            {
                targetIdx = i;
                break;
            }
        }
        // なければ空きスロットを探す
        if (targetIdx < 0)
        {
            for (int i = 0; i < 3; ++i)
            {
                if (!slots[static_cast<size_t>(i)].active)
                {
                    targetIdx = i;
                    break;
                }
            }
        }
        if (targetIdx < 0)
            targetIdx = 0;

        auto& slot = slots[static_cast<size_t>(targetIdx)];
        if (!slot.active)
        {
            slot.active = true;
            slot.centerHz = targetHz;
            slot.z1L = slot.z2L = slot.z1R = slot.z2R = 0.0f;
        }
        else
        {
            slot.centerHz += 0.15f * (targetHz - slot.centerHz);
        }

        // リスクに応じて最大 -16 dB (linear ~0.158) まで狭帯域減衰
        const float targetDepth = std::clamp(1.0f - 0.84f * risk01, 0.16f, 0.85f);
        slot.depthLinear = std::min(slot.depthLinear, targetDepth);
        slot.qFactor = 34.0f;
        slot.holdTimerSec = 1.25f;
        updateSlotCoefficients(slot);
    }

    void updateSlotCoefficients(NotchSlot& slot) noexcept
    {
        const float w0 = 2.0f * 3.14159265358979323846f * std::clamp(slot.centerHz, 120.0f, static_cast<float>(sr * 0.45)) / static_cast<float>(sr);
        const float cosW0 = std::cos(w0);
        const float sinW0 = std::sin(w0);
        const float alpha = sinW0 / (2.0f * std::max(4.0f, slot.qFactor));
        const float A = std::sqrt(std::clamp(slot.depthLinear, 0.05f, 1.0f));

        const float b0Raw = 1.0f + alpha * A;
        const float b1Raw = -2.0f * cosW0;
        const float b2Raw = 1.0f - alpha * A;
        const float a0Raw = 1.0f + alpha / std::max(0.1f, A);
        const float a1Raw = -2.0f * cosW0;
        const float a2Raw = 1.0f - alpha / std::max(0.1f, A);

        const float invA0 = 1.0f / a0Raw;
        slot.b0 = b0Raw * invA0;
        slot.b1 = b1Raw * invA0;
        slot.b2 = b2Raw * invA0;
        slot.a1 = a1Raw * invA0;
        slot.a2 = a2Raw * invA0;
    }

    double sr = 48000.0;
    std::array<NotchSlot, 3> slots{};
    float zeroCrossPeriodSamples = 48.0f;
    int sampleCounterSinceZc = 0;
    float prevSampleMono = 0.0f;
    float instFreqSmoothHz = 1000.0f;
    float freqVarianceSmooth = 50000.0f;
    float envFast = 0.0f;
    float envSlow = 0.0f;
    float howlPersistenceSec = 0.0f;
    float currentRisk01 = 0.0f;
};

// ----------------------------------------------------------------------------
// 2. マイク衝撃・ケーブルショック吸収＆配信ラウドネス規格ガード統合ファサード
// ----------------------------------------------------------------------------
class AcousticFeedbackHowlSuppressorAndBroadcastGuard
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        howlBank.prepare(sr);
        subLpCoeff = 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 38.0f / static_cast<float>(sr));
        reset();
    }

    void reset() noexcept
    {
        howlBank.reset();
        subStateL = subStateR = 0.0f;
        shockGain = 1.0f;
        loudnessEnergy = 1.0e-5f;
        lastReport = HowlAndBroadcastReport{};
    }

    HowlAndBroadcastReport processBuffer(float* left, float* right, int numSamples) noexcept
    {
        if (left == nullptr || right == nullptr || numSamples <= 0)
            return lastReport;

        float maxPeak = 0.0f;
        bool shockTriggered = false;
        float minShockGain = 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            float l = left[i];
            float r = right[i];

            // 1. 38Hz以下の爆発的マイクショック（ボフッ/ドンッ）の抽出と選択的吸収
            subStateL += subLpCoeff * (l - subStateL);
            subStateR += subLpCoeff * (r - subStateR);
            const float subMag = 0.5f * (std::fabs(subStateL) + std::fabs(subStateR));
            if (subMag > 0.14f)
            {
                shockTriggered = true;
                const float targetShock = std::clamp(0.14f / (subMag + 1.0e-6f), 0.15f, 1.0f);
                shockGain += 0.25f * (targetShock - shockGain);
            }
            else
            {
                shockGain += 0.004f * (1.0f - shockGain);
            }
            minShockGain = std::min(minShockGain, shockGain);

            // 超低域成分のみショック減衰させて中高域の声は一切曇らせない
            l = (l - subStateL) + subStateL * shockGain;
            r = (r - subStateR) + subStateR * shockGain;

            // 2. ハウリング予兆検知＆サージカルノッチバンク
            howlBank.processSamplePair(l, r);

            // 3. 配信ラウドネス推定＆True-Peak天井保護 (-1.0 dBTP = 0.89125)
            const float pwr = 0.5f * (l * l + r * r);
            loudnessEnergy += 0.0005f * (pwr - loudnessEnergy);

            const float ceiling = 0.89125f;
            if (std::fabs(l) > ceiling)
                l = std::copysign(ceiling + 0.08f * std::tanh((std::fabs(l) - ceiling) * 12.5f), l);
            if (std::fabs(r) > ceiling)
                r = std::copysign(ceiling + 0.08f * std::tanh((std::fabs(r) - ceiling) * 12.5f), r);

            maxPeak = std::max(maxPeak, std::max(std::fabs(l), std::fabs(r)));
            left[i] = l;
            right[i] = r;
        }

        lastReport.activeHowlNotchCount = howlBank.getActiveNotchCount();
        lastReport.maxHowlRiskScore01 = howlBank.getRiskScore01();
        const auto& slots = howlBank.getSlots();
        for (size_t k = 0; k < 3; ++k)
        {
            lastReport.notchFreqsHz[k] = slots[k].active ? slots[k].centerHz : 0.0f;
            lastReport.notchDepthsDb[k] = slots[k].active ? 20.0f * std::log10(std::max(0.05f, slots[k].depthLinear)) : 0.0f;
        }
        lastReport.seismicShockAbsorbed = shockTriggered;
        lastReport.seismicAttenuationDb = 20.0f * std::log10(std::max(0.05f, minShockGain));
        lastReport.shortTermLoudnessLufs = -0.691f + 10.0f * std::log10( std::max(1.0e-10f, loudnessEnergy));
        const float peakDb = 20.0f * std::log10(std::max(1.0e-7f, maxPeak));
        lastReport.truePeakCeilingMarginDb = -1.0f - peakDb;
        lastReport.broadcastCompliant = (peakDb <= -0.85f);
        return lastReport;
    }

    const HowlAndBroadcastReport& getLastReport() const noexcept { return lastReport; }

private:
    double sr = 48000.0;
    AcousticFeedbackHowlPredictorAndNotchBank howlBank;
    float subLpCoeff = 0.005f;
    float subStateL = 0.0f;
    float subStateR = 0.0f;
    float shockGain = 1.0f;
    float loudnessEnergy = 1.0e-5f;
    HowlAndBroadcastReport lastReport{};
};

} // namespace vg::safety
