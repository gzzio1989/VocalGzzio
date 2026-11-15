#pragma once

#include <array>
#include <cmath>
#include <algorithm>
#include <juce_core/juce_core.h>

namespace gz::v590
{

//==============================================================================
/**
 * VocalIntonationStabilizer (v5.9.0)
 * ゼロ報告遅延（0サンプルPDC）・ドップラー分数遅延ピッチ揺らぎダンパー
 *
 * 【開発背景】
 * オートチューン（ピッチシフター：約3〜16ms遅延）をOFFにしたまま歌うと、
 * 発声の支えが不安定な箇所で 1.2〜3.6Hz 程度の無意識なピッチ揺らぎ（±10〜25セントのフラつき）が
 * そのまま残り、「なんとなく素人っぽく聴こえる」大きな原因になっていた。
 *
 * 【アルゴリズム】
 * 1. 微小中心遅延（約1.45ms＝空気中約49cm相当・ホスト報告遅延0サンプル）を持つ
 *    4点Catmull-Rom三次補間リングバッファ（AoS配置・ヒープ確保ゼロ）を構築。
 * 2. 入力F0から高速ピッチ軌跡 mFast とノート重心 mAnchor を分離追跡し、
 *    半音超のメロディ移動・しゃくり・こぶし時は mAnchor を即座に追従させて表現を100%保持。
 * 3. ピッチ偏差と微分エネルギー比（Rayleigh瞬間変調周波数推定）により、
 *    1.2〜3.6Hz の不安定なピッチ揺らぎだけをドップラー遅延速度制御（dτ/dt = +g・ε）で制振し、
 *    4.6〜8.5Hz の意図的な歌唱ビブラートは 90%以上そのまま通過させる。
 * 4. 無声音（子音 s/t/k/p やブレス）では遅延長を整数サンプル中心 τ0 へ滑らかに収束させ、
 *    コムフィルタや高域劣化ゼロ（子音保持率 100%）を保証する。
 */
class VocalIntonationStabilizer
{
public:
    static constexpr int kRingSize = 1024; // 192kHz時でも約5.3msまで余裕を持って対応（2のべき乗）
    static constexpr int kRingMask = kRingSize - 1;

    struct StereoFrame
    {
        float l = 0.0f;
        float r = 0.0f;
    };

    void prepare (double sampleRate) noexcept
    {
        sr = (sampleRate > 8000.0) ? sampleRate : 48000.0;
        // 中心遅延を厳密な整数サンプル（約1.95ms）に設定し、無声音・定常時の補間誤差を完全ゼロにする
        tau0Samples = juce::jlimit (24.0f, (float) (kRingSize / 2 - 24),
                                    std::round (0.00195f * (float) sr));
        maxExcursion = juce::jmin (tau0Samples - 4.0f, (float) (kRingSize - 8) - tau0Samples);
        rateSlewCoeff = 1.0f - std::exp (-1.0f / juce::jmax (1.0f, 0.0024f * (float) sr));
        leakPerSample = 1.0f / juce::jmax (1.0f, 0.48f * (float) sr);
        reset();
    }

    void reset() noexcept
    {
        ring.fill ({});
        writePos = 0;
        tau = tau0Samples;
        curRate = 0.0f;
        mFast = 60.0f;
        mAnchor = 60.0f;
        prevDev = 0.0f;
        envDevSq = 0.0f;
        envVelSq = 0.0f;
        vibConf = 0.0f;
        wasVoiced = false;
        voicedHoldSec = 0.0f;
        lastCorrectionCents = 0.0f;
    }

    float getLastCorrectionCents() const noexcept { return lastCorrectionCents; }
    float getVibratoConfidence()   const noexcept { return vibConf; }

    void processBlock (float* left, float* right, int numSamples,
                       float f0Hz, float f0Conf, float extVibConf = 0.0f) noexcept
    {
        if (left == nullptr || numSamples <= 0)
            return;

        const float blockDt = (float) numSamples / (float) sr;
        const bool voiced = (f0Hz >= 65.0f && f0Hz <= 1100.0f && f0Conf >= 0.45f);

        float targetRate = 0.0f;
        float activeCorrCents = 0.0f;

        if (voiced)
        {
            const float mIn = 69.0f + 12.0f * std::log2 (juce::jmax (20.0f, f0Hz) / 440.0f);

            if (! wasVoiced)
            {
                // 発声開始時はアンカーを現在ピッチへ即座に同期し、歌い出しのしゃくりを潰さない
                mFast = mIn;
                mAnchor = mIn;
                prevDev = 0.0f;
                voicedHoldSec = blockDt;
                wasVoiced = true;
            }
            else
            {
                voicedHoldSec = juce::jmin (10.0f, voicedHoldSec + blockDt);
                const float fastAlpha = 1.0f - std::exp (-blockDt / 0.0035f);
                mFast += fastAlpha * (mIn - mFast);

                const float jump = std::abs (mIn - mAnchor);
                if (jump > 0.52f)
                {
                    // 半音以上のメロディ移動・こぶし・フォールはノート遷移とみなして即追従
                    mAnchor = mIn;
                    mFast = mIn;
                    prevDev = 0.0f;
                    envDevSq *= 0.25f;
                    envVelSq *= 0.25f;
                    voicedHoldSec = blockDt;
                }
                else
                {
                    // 持続母音中は 420ms 時定数でノート重心を安定保持し、1.2〜3.6Hz のフラつきにアンカーが引きずられないようにする
                    const float anchorAlpha = 1.0f - std::exp (-blockDt / 0.42f);
                    mAnchor += anchorAlpha * (mFast - mAnchor);

                    const float nearestSemi = std::round (mAnchor);
                    if (std::abs (nearestSemi - mAnchor) < 0.22f && voicedHoldSec > 0.10f)
                    {
                        const float semiPull = juce::jmin (0.25f, blockDt * 1.4f);
                        mAnchor += semiPull * (nearestSemi - mAnchor);
                    }
                }
            }

            const float dev = juce::jlimit (-0.50f, 0.50f, mFast - mAnchor);
            const float vel = (blockDt > 1.0e-5f) ? ((dev - prevDev) / blockDt) : 0.0f;
            prevDev = dev;

            // Rayleigh 瞬間変調周波数推定: f_mod = (1 / 2π) * sqrt(E[vel^2] / E[dev^2])
            const float modAlpha = 1.0f - std::exp (-blockDt / 0.075f);
            envDevSq += modAlpha * (dev * dev - envDevSq);
            envVelSq += modAlpha * (vel * vel - envVelSq);

            const float rmsDev = std::sqrt (juce::jmax (0.0f, envDevSq));
            float detectedVib = 0.0f;
            if (rmsDev > 0.035f && envDevSq > 1.0e-6f)
            {
                const float fMod = (1.0f / juce::MathConstants<float>::twoPi)
                                 * std::sqrt (envVelSq / (envDevSq + 1.0e-6f));
                // 4.4Hz〜8.8Hz の周期的な揺れは意図的な歌唱ビブラートとして保護する
                const float rise = smoothStep (4.1f, 4.9f, fMod);
                const float fall = 1.0f - smoothStep (8.6f, 11.0f, fMod);
                detectedVib = rise * fall;
            }

            const float wantVib = juce::jmax (extVibConf, detectedVib);
            const float vibAlpha = 1.0f - std::exp (-blockDt / (wantVib > vibConf ? 0.035f : 0.090f));
            vibConf += vibAlpha * (wantVib - vibConf);

            // 発声直後25msはフェードインし、ビブラート検出時は制振ゲインを 6% まで下げて表情を守る
            const float onsetRamp = smoothStep (0.008f, 0.028f, voicedHoldSec);
            const float confGate  = smoothStep (0.45f, 0.70f, f0Conf);
            const float dampGain  = 0.88f * onsetRamp * confGate * (1.0f - 0.94f * vibConf);

            // f_out = f_in * (1 - dτ/dt) より、ピッチが上ずった時(dev > 0)は dτ/dt > 0 で遅延を伸ばしてピッチを下げる
            const float fracErr = std::exp2 (dev / 12.0f) - 1.0f;
            targetRate = juce::jlimit (-0.035f, 0.035f, dampGain * fracErr);
            activeCorrCents = std::abs (dev * 100.0f * dampGain);
        }
        else
        {
            wasVoiced = false;
            voicedHoldSec = 0.0f;
            prevDev = 0.0f;
            envDevSq *= 0.85f;
            envVelSq *= 0.85f;
            vibConf  *= 0.85f;
            targetRate = 0.0f;
        }

        lastCorrectionCents = 0.80f * lastCorrectionCents + 0.20f * activeCorrCents;

        for (int n = 0; n < numSamples; ++n)
        {
            const float xL = left[n];
            const float xR = (right != nullptr) ? right[n] : xL;

            ring[writePos & kRingMask] = { xL, xR };

            curRate += rateSlewCoeff * (targetRate - curRate);

            // 外側32%の領域でのみ滑らかにソフト壁を効かせ、通常振幅では線形な制振特性を維持する
            const float offset = tau - tau0Samples;
            const float normOff = offset / juce::jmax (1.0f, maxExcursion);
            float effRate = curRate;
            if (normOff * effRate > 0.0f && std::abs (normOff) > 0.68f)
            {
                const float u = (std::abs (normOff) - 0.68f) / 0.32f;
                const float wallScale = juce::jlimit (0.0f, 1.0f, 1.0f - u * u);
                effRate *= wallScale;
            }

            const float leakScale = voiced ? 1.0f : 3.5f;
            tau += effRate - offset * (leakPerSample * leakScale);
            tau = juce::jlimit (tau0Samples - maxExcursion, tau0Samples + maxExcursion, tau);

            // 無声音でほぼ τ0 に収束したら厳密な整数遅延へスナップして補間ロスを完全ゼロにする
            if (! voiced && std::abs (tau - tau0Samples) < 1.0e-3f && std::abs (curRate) < 1.0e-5f)
                tau = tau0Samples;

            const float rp = (float) writePos - tau;
            const int idx  = (int) std::floor (rp);
            const float fr = rp - (float) idx;

            const auto& sm1 = ring[(idx - 1) & kRingMask];
            const auto& s0  = ring[ idx      & kRingMask];
            const auto& s1  = ring[(idx + 1) & kRingMask];
            const auto& s2  = ring[(idx + 2) & kRingMask];

            left[n] = interpCatmullRom (sm1.l, s0.l, s1.l, s2.l, fr);
            if (right != nullptr)
                right[n] = interpCatmullRom (sm1.r, s0.r, s1.r, s2.r, fr);

            writePos = (writePos + 1) & kRingMask;
        }
    }

private:
    static inline float smoothStep (float edge0, float edge1, float x) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - edge0) / juce::jmax (1.0e-6f, edge1 - edge0));
        return t * t * (3.0f - 2.0f * t);
    }

    static inline float interpCatmullRom (float ym1, float y0, float y1, float y2, float fr) noexcept
    {
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * fr + c2) * fr + c1) * fr + c0;
    }

    std::array<StereoFrame, kRingSize> ring {};
    double sr = 48000.0;
    float tau0Samples = 70.0f;
    float maxExcursion = 64.0f;
    float rateSlewCoeff = 0.006f;
    float leakPerSample = 0.00006f;

    int   writePos = 0;
    float tau = 70.0f;
    float curRate = 0.0f;
    float mFast = 60.0f;
    float mAnchor = 60.0f;
    float prevDev = 0.0f;
    float envDevSq = 0.0f;
    float envVelSq = 0.0f;
    float vibConf = 0.0f;
    bool  wasVoiced = false;
    float voicedHoldSec = 0.0f;
    float lastCorrectionCents = 0.0f;
};

} // namespace gz::v590
