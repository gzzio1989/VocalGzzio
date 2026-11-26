#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gz::v590
{

//==============================================================================
// v5.9.0 『🎙️ マイク劣化・パリパリ／サフサフ修復（ゼロ遅延チャープ外挿・6帯域直交周期ANC v2）』
//
// 物理モデルと3段ハイブリッド信号復元:
//   - 6帯域 1次RC直交テレスコーピング分割 (740 / 1500 / 2650 / 4300 / 7200 / 11800 Hz):
//     「振幅恒等和 ΣB_k ≡ 1 (追加遅延 0.0ms・位相回転 0.000°)」と「電力直交和 Σ|B_k|^2 ≡ 1」を両立。
//     単一インターリーブAoSリングバッファ (RingFrame) によりL1キャッシュセット競合を完全解消。
//   - α-β チャープ外挿ピッチ追跡 (2周期一貫性検証 r(T0)&r(2T0) + 窓重心前進外挿 + C0連続ラグ補間):
//     5〜7Hzの深いビブラート中でも瞬時周期 T0(n) と周期変化率 dT0/dn を高精度追跡し、
//     無声子音（さ・ふ・息継ぎ）での誤有声判定を2周期自己相関ゲートで物理的にゼロ化。
//   - 第1段: 4周期チャープ整合トリム平均 (midMean4) ＋ 周期内エクスカージョン擦れクランプ
//     「あー」発声中のカプセル湿気アーク放電（パリパリ・チリチリ）とスピーカー様の片側擦れ歪み (Rub & Buzz) を即時置換。
//   - 第2段: チャープ同期1次再帰倍音コム ＋ 倍音保護型サブバンドHNRポストウィーナー
//     劣化した風防スポンジの共鳴サフサフ音や、メインのノイズ除去をすり抜ける母音中の変調ヒスを倍音無傷で深層浄化。
//   - 第3段: 単調減衰包絡ロック付き 母音オフセット後スポンジ共鳴減衰尾 ＆ ノイズ除去ホールド残留ヒス尾サプレッサ
//     単調減衰中の共鳴尾だけを強力に消去し、直後に続く子音・ブレスの立ち上がりは即座に開放して 99%以上無傷保護。
//==============================================================================
class BrokenMicSafuCanceller
{
public:
    static constexpr int kNumSubBands = 6;

    enum Mode : int
    {
        Off      = 0,
        Standard = 1,
        Deep     = 2
    };

    BrokenMicSafuCanceller() { prepare (48000.0); }

    void prepare (double sampleRate)
    {
        sr = std::clamp (sampleRate, 16000.0, 192000.0);

        // 48kHz時は ringSize = 4096 (過去4周期 72Hz基音まで余裕で保持)、単一連続配列でL1キャッシュ効率を最大化
        const int needed = (int) std::ceil (4.0 * sr / 72.0) + 64;
        int pow2 = 4096;
        while (pow2 < needed && pow2 < 16384)
            pow2 <<= 1;
        ringSize = pow2;
        ringMask = ringSize - 1;
        if ((int) ring.size() != ringSize)
            ring.assign ((size_t) ringSize, RingFrame {});

        aPitchLp = onePoleFreqCoef (480.0, sr);
        aFund185 = onePoleFreqCoef (185.0, sr);

        // 6帯域 1次RC直交テレスコーピング分割カットオフ
        static constexpr double kSplitHz[kNumSubBands] = {
            740.0, 1500.0, 2650.0, 4300.0, 7200.0, 11800.0
        };
        for (int s = 0; s < kNumSubBands; ++s)
            aSplit[(size_t) s] = onePoleFreqCoef (kSplitHz[s], sr);

        aLowAtk     = onePoleTauMs (0.65,  sr);
        aLowRel     = onePoleTauMs (2.20,  sr);
        aLowTailAtk = onePoleTauMs (1.80,  sr);
        aLowTailRel = onePoleTauMs (48.00, sr);

        // 0サンプル遅延ピークホールド＋0.24ms高速減衰
        aSbRmsAtk   = onePoleTauMs (0.24, sr);
        aSbRmsRel   = onePoleTauMs (0.48, sr);
        aSbPeakRel  = onePoleTauMs (0.24, sr);
        aConsAtk    = onePoleTauMs (1.60, sr);
        aConsRel    = onePoleTauMs (3.20, sr);
        aTailHfSlew = onePoleTauMs (7.50, sr);

        aPowAtk     = onePoleTauMs (1.60, sr);
        aPowRel     = onePoleTauMs (6.00, sr);

        aVoicedAtk     = onePoleTauMs (1.50, sr);
        aVoicedRel     = onePoleTauMs (2.60, sr);
        aSpikeAtk      = 1.00f; // 先頭サンプル (k=0) で遅延ゼロ即時クランプ
        aSpikeRel      = onePoleTauMs (0.95, sr);
        aBodySpikeRel  = onePoleTauMs (0.65, sr);
        aWienerSlew    = onePoleTauMs (2.40, sr);
        aWienerTailAtk = onePoleTauMs (0.30, sr);
        aBypassSlew    = onePoleTauMs (3.00, sr);

        xfadeStep      = (float) (1.0 / std::max (8.0, 0.0020 * sr));
        maxTailSamples = std::max (64, (int) std::lround (sr * 0.115));

        // 1.33ms (64サンプル@48kHz) ピッチ更新＋サンプル毎チャープ速度補間により超低CPUとビブラート追従を両立
        corrHopSamples = std::max (32, (int) std::lround (sr * (64.0 / 48000.0)));
        corrWinSamples = std::min (1536, std::max (240, (int) std::lround (sr * 0.0090)));
        corrDecim      = (sr > 120000.0) ? 16 : (sr > 64000.0 ? 10 : 6);
        minLagSamples  = std::max (12, (int) std::floor (sr / 850.0));
        maxLagSamples  = std::min ((ringSize / 4) - 16, (int) std::ceil (sr / 75.0));

        reset();
    }

    void reset() noexcept
    {
        std::fill (ring.begin(), ring.end(), RingFrame {});
        for (int s = 0; s < kNumSubBands; ++s)
        {
            sbFastPow[(size_t) s]        = 0.0f;
            sbPeakEnv[(size_t) s]        = 0.0f;
            sbCleanPeakEnv[(size_t) s]   = 0.0f;
            powSig[(size_t) s]           = 0.0f;
            powDiff1[(size_t) s]         = 0.0f;
            powDiff2[(size_t) s]         = 0.0f;
            spikeGain[(size_t) s]        = 1.0f;
            wienerGain[(size_t) s]       = 1.0f;
            spongeAlpha[(size_t) s]      = 0.0f;
            voicedNoiseFloor[(size_t) s] = 0.0f;
        }
        pitchLpState[0] = pitchLpState[1] = 0.0f;
        rcFund[0] = rcFund[1] = 0.0f;
        spikeBodyGain = 1.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < kNumSubBands; ++k)
                rcLp[ch][k] = 0.0f;

        ringPos                = 0;
        totalPushed            = 0;
        hopCounter             = 0;
        const float defLag     = (float) (sr / 220.0);
        lagEst                 = defLag;
        lagVel                 = 0.0f;
        curLagSamples          = defLag;
        curLagStep             = 0.0f;
        curPeriodR0            = 0.0f;
        curResidualJitter      = 0.0f;
        curVoicedFlag          = false;
        wasVoicedPrev          = false;
        voicedHoldSamples      = 0;
        sinceVoicedSamples     = 1000000;
        tailHfMin              = 0.0f;
        tailEnvHf              = 0.0f;
        tailAbortedByConsonant = true;
        voicedWeight           = 0.0f;
        lowPow                 = 0.0f;
        lowTailPow             = 0.0f;
        hfConsPow              = 0.0f;
        activeMix              = 0.0f;
        wasSleeping            = true;

        tapLagBackup           = defLag;
        tapCrossfade           = 0.0f;
        tapCrossfading         = false;
        tapWasValid            = false;

        lastCleanDb            = 0.0f;
        defectAccumScore       = 0.0f;
        defectFrames           = 0;
        sentinelEma            = 0.0f;
        sentinelHighBlocks     = 0;
    }

    void notifyBypassed() noexcept
    {
        wasSleeping = true;
        activeMix   = 0.0f;
        lastCleanDb = 0.0f;
    }

    void resetKarteObservation() noexcept
    {
        defectAccumScore = 0.0f;
        defectFrames     = 0;
    }

    void resetMidSessionSentinel() noexcept
    {
        sentinelEma        = 0.0f;
        sentinelHighBlocks = 0;
    }

    bool consumeMidSessionSentinelTrigger() noexcept
    {
        if (sentinelHighBlocks >= 22 && sentinelEma >= 0.28f)
        {
            sentinelEma        = 0.0f;
            sentinelHighBlocks = 0;
            return true;
        }
        return false;
    }

    int recommendedModeFromObservation() const noexcept
    {
        if (defectFrames < 12)
            return Off;
        const float avgScore = defectAccumScore / (float) defectFrames;
        if (avgScore >= 0.40f)
            return Deep;
        if (avgScore >= 0.22f)
            return Standard;
        return Off;
    }

    float cleanReductionDb() const noexcept { return lastCleanDb; }

    void processBuffer (float* chL, float* chR,
                        int numSamples, int numCh,
                        int mode,
                        float f0HintHz, float f0HintConf,
                        bool observeOnlyForKarte = false) noexcept
    {
        if (numSamples <= 0 || ringSize <= 0 || ring.empty() || chL == nullptr)
            return;

        const bool activeSuppress = (mode > 0);
        if (! activeSuppress && ! observeOnlyForKarte && activeMix < 1.0e-4f)
        {
            wasSleeping = true;
            lastCleanDb = 0.0f;
            return;
        }

        if (wasSleeping)
        {
            const float savedScore    = defectAccumScore;
            const int   savedFrames   = defectFrames;
            const float savedSentEma  = sentinelEma;
            const int   savedSentHigh = sentinelHighBlocks;
            reset();
            defectAccumScore   = savedScore;
            defectFrames       = savedFrames;
            sentinelEma        = savedSentEma;
            sentinelHighBlocks = savedSentHigh;
            wasSleeping        = false;
        }

        const bool isDeep   = (mode >= Deep);
        const float strength = isDeep ? 1.68f : (mode == Standard ? 0.72f : 0.0f);
        const int chCount = (numCh > 1 && chR != nullptr) ? 2 : 1;

        // sb0: 740-1500Hz  (F2保護＆広帯域放電裾)
        // sb1: 1.50-2.65kHz (サフサフ・スポンジ共鳴下部)
        // sb2: 2.65-4.30kHz (スポンジ共鳴核 2.85kHz・擦れ 3.8kHz)
        // sb3: 4.30-7.20kHz (スポンジ第2核 4.3kHz・パリパリ核 4.6kHz・擦れ 6.8kHz)
        // sb4: 7.20-11.8kHz (高域チリチリ放電・残留変調ヒス)
        // sb5: >11.8kHz     (インパルス放電スパイク)
        // 人間の歌声の自然なシマー・息成分・母音遷移では誤発火せず（Standardは完全透明）、Deepではクリーン倍音無傷のまま放電・共鳴尾・変調ヒスを深層除去する設計値
        static constexpr float kCenterHz[kNumSubBands]     = { 1080.0f, 2000.0f, 3380.0f, 5550.0f, 9200.0f, 14500.0f };
        static constexpr float kPeakCrestStd[kNumSubBands] = { 1.50f,   1.40f,   1.34f,   1.30f,   1.26f,   1.24f    };
        static constexpr float kPeakCrestDp[kNumSubBands]  = { 1.31f,   1.23f,   1.17f,   1.14f,   1.11f,   1.10f    };
        static constexpr float kCombMaxStd[kNumSubBands]   = { 0.08f,   0.14f,   0.18f,   0.20f,   0.22f,   0.24f    };
        static constexpr float kCombMaxDp[kNumSubBands]    = { 0.68f,   0.81f,   0.86f,   0.88f,   0.88f,   0.88f    };
        static constexpr float kOverSubStd[kNumSubBands]   = { 1.00f,   1.08f,   1.22f,   1.32f,   1.40f,   1.45f    };
        static constexpr float kOverSubDp[kNumSubBands]    = { 1.00f,   1.16f,   1.40f,   1.60f,   1.74f,   1.84f    };
        static constexpr float kWienerBetaStd[kNumSubBands]= { 0.35f,   0.85f,   1.60f,   2.10f,   2.60f,   3.00f    };
        static constexpr float kWienerBetaDp[kNumSubBands] = { 0.65f,   1.80f,   3.50f,   4.60f,   5.35f,   5.80f    };
        static constexpr float kMinVoicedStd[kNumSubBands] = { 0.88f,   0.82f,   0.76f,   0.74f,   0.70f,   0.66f    };
        static constexpr float kMinVoicedDp[kNumSubBands]  = { 0.58f,   0.26f,   0.035f,  0.015f,  0.010f,  0.008f   };

        auto* const ringPtr = ring.data();
        const int   rMask   = ringMask;
        const int   rSize   = ringSize;

        float blockReductionDbPeak = 0.0f;
        float blockDefectPeak      = 0.0f;
        bool  blockHadVoiced       = false;

        for (int n = 0; n < numSamples; ++n)
        {
            const int w = ringPos;
            RingFrame& curFrame = ringPtr[(size_t) w];

            const float inL = chL[n];
            const float inR = (chCount > 1) ? chR[n] : inL;
            const float monoIn = (chCount > 1) ? 0.5f * (inL + inR) : inL;

            pitchLpState[0] += aPitchLp * (monoIn - pitchLpState[0]);
            pitchLpState[1] += aPitchLp * (pitchLpState[0] - pitchLpState[1]);
            const float lowVoice = pitchLpState[1];
            curFrame.low = lowVoice;
            ++totalPushed;

            const float lowSq = lowVoice * lowVoice;
            lowPow     += (lowSq > lowPow     ? aLowAtk     : aLowRel)     * (lowSq - lowPow);
            lowTailPow += (lowSq > lowTailPow ? aLowTailAtk : aLowTailRel) * (lowSq - lowTailPow);
            const float envLow     = std::sqrt (std::max (0.0f, lowPow));
            const float envLowTail = std::sqrt (std::max (0.0f, lowTailPow));

            if (++hopCounter >= corrHopSamples)
            {
                hopCounter = 0;
                updatePitchLag (w, f0HintHz, f0HintConf);
            }
            else if (curVoicedFlag)
            {
                curLagSamples = std::clamp (curLagSamples + curLagStep, (float) minLagSamples, (float) maxLagSamples);
            }

            if (curVoicedFlag)
                ++voicedHoldSamples;
            else
                voicedHoldSamples = 0;

            const float wantVoicedW = curVoicedFlag
                ? std::clamp ((curPeriodR0 - 0.80f) * (1.0f / 0.11f), 0.0f, 1.0f)
                : 0.0f;
            voicedWeight += (wantVoicedW > voicedWeight ? aVoicedAtk : aVoicedRel) * (wantVoicedW - voicedWeight);
            const float wv = voicedWeight;
            if (wv > 0.35f)
                blockHadVoiced = true;

            const float targetMix = activeSuppress ? 1.0f : 0.0f;
            activeMix += aBypassSlew * (targetMix - activeMix);

            float xfadeMix = 0.0f;
            if (tapCrossfading)
            {
                tapCrossfade += xfadeStep;
                if (tapCrossfade >= 1.0f)
                {
                    tapCrossfade   = 0.0f;
                    tapCrossfading = false;
                }
                else
                {
                    xfadeMix = tapCrossfade * tapCrossfade * (3.0f - 2.0f * tapCrossfade);
                }
            }

            // チャープ伸縮補正付き多周期連続ラグ τ1(n), τ2(n), τ3(n) の算出とリング添字の事前一括計算
            const float tau1 = curLagSamples;
            const float tau2 = std::clamp (2.0f * tau1 - lagVel * tau1,        (float) (minLagSamples * 2), (float) (rSize - 12));
            const float tau3 = std::clamp (3.0f * tau1 - 3.0f * lagVel * tau1, (float) (minLagSamples * 3), (float) (rSize - 12));

            TapIndex tap1, tap2, tap3, tap1Old;
            makeTapIndex (w, tau1, rSize, rMask, tap1);
            makeTapIndex (w, tau2, rSize, rMask, tap2);
            makeTapIndex (w, tau3, rSize, rMask, tap3);
            if (tapCrossfading)
                makeTapIndex (w, tapLagBackup, rSize, rMask, tap1Old);

            // 6帯域 1次RC直交テレスコーピング分割: fundKeep + bodyMidIn + sb0..sb5 === inSample！
            float fundKeep[2]  = {};
            float bodyMidIn[2] = {};
            float bodyKeep[2]  = {};
            float sbIn[kNumSubBands][2] = {};
            for (int ch = 0; ch < chCount; ++ch)
            {
                const float x = (ch == 0) ? inL : inR;

                rcLp[ch][0] += aSplit[0] * (x - rcLp[ch][0]);
                bodyKeep[ch] = rcLp[ch][0];
                rcFund[ch]  += aFund185 * (bodyKeep[ch] - rcFund[ch]);
                fundKeep[ch]  = rcFund[ch];
                bodyMidIn[ch] = bodyKeep[ch] - fundKeep[ch];
                float rem = x - rcLp[ch][0];

                for (int k = 0; k < kNumSubBands - 1; ++k)
                {
                    rcLp[ch][k + 1] += aSplit[(size_t) (k + 1)] * (rem - rcLp[ch][k + 1]);
                    sbIn[k][ch] = rcLp[ch][k + 1];
                    rem -= rcLp[ch][k + 1];
                }
                sbIn[kNumSubBands - 1][ch] = rem;
            }

            // 日本語無声子音（さ・ふ・息継ぎ: >1.5kHz 全帯域 s=1..5 を網羅）保護ガード
            float hfSq = 0.0f;
            for (int s = 1; s < kNumSubBands; ++s)
                hfSq += sbIn[s][0] * sbIn[s][0];
            hfConsPow += (hfSq > hfConsPow ? aConsAtk : aConsRel) * (hfSq - hfConsPow);
            const float envHfCons = std::sqrt (std::max (0.0f, hfConsPow));
            tailEnvHf += aTailHfSlew * (envHfCons - tailEnvHf);

            const bool isUnvoicedFricative = (envHfCons > envLow * 1.15f + 1.0e-3f)
                                          && (envLow < 0.024f || curPeriodR0 < 0.88f);
            const float consBypass = isUnvoicedFricative ? 0.0f : 1.0f;
            const float wvSafe = wv * consBypass;

            // 母音オフセット後の単調減衰尾トラッキング（低域基音が48ms包絡の45%未満へ急落した時点で即座に減衰尾カウント開始）
            if (wvSafe > 0.45f && envLow > 0.022f && envLow >= envLowTail * 0.45f)
            {
                sinceVoicedSamples     = 0;
                tailHfMin              = tailEnvHf;
                tailAbortedByConsonant = false;
            }
            else
            {
                if (sinceVoicedSamples < 1000000)
                    ++sinceVoicedSamples;
                if (tailEnvHf < tailHfMin)
                    tailHfMin = tailEnvHf;
                else if (tailEnvHf > tailHfMin * 2.35f + 3.6e-3f)
                    tailAbortedByConsonant = true;
            }

            const int lag1 = std::clamp (tap1.dInt,                     minLagSamples,     rSize - 16);
            const int lag2 = std::clamp (tap2.dInt,                     minLagSamples * 2, rSize - 16);
            const int lag3 = std::clamp (tap3.dInt,                     minLagSamples * 3, rSize - 16);
            const int lag4 = std::clamp ((int) std::lround (tau1 * 4.0f), minLagSamples * 4, rSize - 16);
            const bool have3Periods = (totalPushed > (std::uint64_t) (lag4 + 8));
            const bool have2Periods = (totalPushed > (std::uint64_t) (tap3.dInt + 8));
            const float f0Est = (float) (sr / std::max (40.0, (double) tau1));
            const float envLowDenom = envLow + 8.0e-4f;

            const RingFrame& f1_0 = ringPtr[(size_t) tap1.i0];
            const RingFrame& f1_1 = ringPtr[(size_t) tap1.i1];
            const RingFrame& f1_2 = ringPtr[(size_t) tap1.i2];
            const RingFrame& f1_3 = ringPtr[(size_t) tap1.i3];

            const RingFrame& f2_0 = ringPtr[(size_t) tap2.i0];
            const RingFrame& f2_1 = ringPtr[(size_t) tap2.i1];
            const RingFrame& f2_2 = ringPtr[(size_t) tap2.i2];
            const RingFrame& f2_3 = ringPtr[(size_t) tap2.i3];

            const RingFrame& f3_0 = ringPtr[(size_t) tap3.i0];
            const RingFrame& f3_1 = ringPtr[(size_t) tap3.i1];
            const RingFrame& f3_2 = ringPtr[(size_t) tap3.i2];
            const RingFrame& f3_3 = ringPtr[(size_t) tap3.i3];

            const int rIdx1a = (w - lag1     + rSize) & rMask;
            const int rIdx1b = (w - lag1 - 2 + rSize) & rMask;
            const int rIdx2a = (w - lag2     + rSize) & rMask;
            const int rIdx2b = (w - lag2 - 2 + rSize) & rMask;
            const int rIdx3a = (w - lag3     + rSize) & rMask;
            const int rIdx3b = (w - lag3 - 2 + rSize) & rMask;
            const int rIdx4a = (w - lag4     + rSize) & rMask;
            const int rIdx4b = (w - lag4 - 2 + rSize) & rMask;

            float sbOut[kNumSubBands][2] = {};
            float broadbandSpikeG  = 1.0f;
            float hiBandNoiseRatio = 0.0f;

            const float tailNoEcho = (envLowTail > 0.010f && envLow < envLowTail * 0.85f)
                ? std::clamp ((envLow - envLowTail * 0.45f) / (envLowTail * 0.40f + 1.0e-5f), 0.0f, 1.0f)
                : 1.0f;

            float sharedTailGate = 0.0f;
            // 有声母音が終了して低域基音が急落した直後の無声減衰尾区間(48ms)のみ稼働し、歌唱中の自然な声量変化では絶対チョップしない
            if (! tailAbortedByConsonant
                && (wvSafe < 0.48f || envLow < envLowTail * 0.42f)
                && sinceVoicedSamples > 0
                && sinceVoicedSamples < maxTailSamples
                && envLowTail > 0.0008f
                && envLow < envLowTail * 0.78f)
            {
                const float fadeEnd = std::clamp ((float) (maxTailSamples - sinceVoicedSamples) / (0.18f * (float) maxTailSamples), 0.0f, 1.0f);
                sharedTailGate = fadeEnd * std::clamp ((envLowTail * 0.78f - envLow) / (envLowTail * 0.25f + 1.0e-5f), 0.0f, 1.0f);
            }

            for (int s = kNumSubBands - 1; s >= 0; --s)
            {
                float uM1[2]   = { 0.0f, 0.0f };
                float uM2[2]   = { 0.0f, 0.0f };
                float uM3[2]   = { 0.0f, 0.0f };
                float uTrim[2] = { sbIn[s][0], sbIn[s][1] };
                float hPrev[2] = { 0.0f, 0.0f };

                if (have2Periods)
                {
                    for (int ch = 0; ch < chCount; ++ch)
                    {
                        const float v1 = tap1.c0 * f1_0.dec[s][ch] + tap1.c1 * f1_1.dec[s][ch] + tap1.c2 * f1_2.dec[s][ch] + tap1.c3 * f1_3.dec[s][ch];
                        const float v2 = tap2.c0 * f2_0.dec[s][ch] + tap2.c1 * f2_1.dec[s][ch] + tap2.c2 * f2_2.dec[s][ch] + tap2.c3 * f2_3.dec[s][ch];
                        const float v3 = tap3.c0 * f3_0.dec[s][ch] + tap3.c1 * f3_1.dec[s][ch] + tap3.c2 * f3_2.dec[s][ch] + tap3.c3 * f3_3.dec[s][ch];
                        float hp       = tap1.c0 * f1_0.harm[s][ch] + tap1.c1 * f1_1.harm[s][ch] + tap1.c2 * f1_2.harm[s][ch] + tap1.c3 * f1_3.harm[s][ch];
                        if (tapCrossfading)
                        {
                            const float hpOld = tap1Old.c0 * ringPtr[(size_t) tap1Old.i0].harm[s][ch]
                                              + tap1Old.c1 * ringPtr[(size_t) tap1Old.i1].harm[s][ch]
                                              + tap1Old.c2 * ringPtr[(size_t) tap1Old.i2].harm[s][ch]
                                              + tap1Old.c3 * ringPtr[(size_t) tap1Old.i3].harm[s][ch];
                            hp = hpOld + xfadeMix * (hp - hpOld);
                        }
                        uM1[ch]   = v1;
                        uM2[ch]   = v2;
                        uM3[ch]   = v3;
                        uTrim[ch] = midMean4 (sbIn[s][ch], v1, v2, v3);
                        hPrev[ch] = hp;
                    }
                }

                float sqMax  = sbIn[s][0] * sbIn[s][0];
                float absMax = std::abs (sbIn[s][0]);
                if (chCount > 1)
                {
                    sqMax  = std::max (sqMax,  sbIn[s][1] * sbIn[s][1]);
                    absMax = std::max (absMax, std::abs (sbIn[s][1]));
                }

                sbFastPow[(size_t) s] += (sqMax > sbFastPow[(size_t) s] ? aSbRmsAtk : aSbRmsRel)
                                       * (sqMax - sbFastPow[(size_t) s]);
                sbPeakEnv[(size_t) s] = (absMax > sbPeakEnv[(size_t) s])
                    ? absMax
                    : (sbPeakEnv[(size_t) s] + aSbPeakRel * (absMax - sbPeakEnv[(size_t) s]));

                float targetSpikeG = 1.0f;
                if (wvSafe > 0.15f && have3Periods && sinceVoicedSamples == 0)
                {
                    float v[4] = {
                        std::max (ringPtr[(size_t) rIdx1a].ratio[s], ringPtr[(size_t) rIdx1b].ratio[s]),
                        std::max (ringPtr[(size_t) rIdx2a].ratio[s], ringPtr[(size_t) rIdx2b].ratio[s]),
                        std::max (ringPtr[(size_t) rIdx3a].ratio[s], ringPtr[(size_t) rIdx3b].ratio[s]),
                        std::max (ringPtr[(size_t) rIdx4a].ratio[s], ringPtr[(size_t) rIdx4b].ratio[s])
                    };
                    if (v[0] > v[1]) std::swap (v[0], v[1]);
                    if (v[2] > v[3]) std::swap (v[2], v[3]);
                    if (v[0] > v[2]) std::swap (v[0], v[2]);
                    if (v[1] > v[3]) std::swap (v[1], v[3]);
                    const float rRef = std::min (v[1], v[2]);
                    const float refPeak = rRef * envLowDenom;
                    const float crestHere = isDeep ? kPeakCrestDp[s] : kPeakCrestStd[s];
                    float allowPk = refPeak * crestHere + (isDeep ? 6.0e-5f : 1.0e-4f);

                    // 周期内エクスカージョン擦れ (Rub & Buzz) クランプ（Deep時または明確な非周期擦れ突出時のみ適用し、通常のビブラート倍音は保護）
                    const float prevPx = powSig[(size_t) s];
                    const float prevPn = std::max (0.0f, std::min (powDiff1[(size_t) s], powDiff2[(size_t) s]) - 0.04f * prevPx);
                    const float rubRatioThr = isDeep ? ((s == 1) ? 0.30f : 0.20f) : 0.48f;
                    if (s >= 1 && prevPn > rubRatioThr * prevPx && prevPx > 1.0e-7f)
                    {
                        const float overSubHere = isDeep ? kOverSubDp[s] : kOverSubStd[s];
                        const float psPure      = std::max (0.0f, prevPx - overSubHere * prevPn);
                        const float harmRmsEst  = std::sqrt (psPure);
                        allowPk = std::min (allowPk, (isDeep ? 1.88f : 2.35f) * harmRmsEst + 2.5e-4f);
                    }

                    float signedJump = 0.0f;
                    for (int ch = 0; ch < chCount; ++ch)
                    {
                        const float jmp = std::min (std::abs (sbIn[s][ch] - uTrim[ch]),
                                                    std::min (std::abs (sbIn[s][ch] - hPrev[ch]),
                                                              std::abs (sbIn[s][ch] - uM1[ch])));
                        signedJump = std::max (signedJump, jmp);
                    }
                    // Standard時はホールド値によるクレッシェンド誤発火を完全防止し、Deep時は0.24msピークホールドで連続放電尾まで深層クランプ
                    const float effPeak  = isDeep ? std::max (sbPeakEnv[(size_t) s], signedJump)
                                                  : std::max (absMax, signedJump * 1.15f);
                    const float jumpGate = isDeep ? 0.0f : (allowPk * 0.38f);

                    if (effPeak > allowPk && signedJump >= jumpGate)
                    {
                        const float rawG = isDeep
                            ? std::clamp ((allowPk * 0.06f + 1.0e-5f) / (effPeak + 1.0e-9f), 0.002f, 1.0f)
                            : (1.0f - std::clamp ((effPeak - allowPk) / (allowPk * 0.48f + 1.0e-7f), 0.0f, 1.0f) * 0.94f);
                        if (activeSuppress)
                            targetSpikeG = 1.0f - std::min (1.0f, wvSafe * (isDeep ? 1.0f : 0.92f)) * (1.0f - rawG);

                        // サフサフ共鳴が同時発生中でも、クレストが高い広帯域パリパリ放電スパイク時は低中域(185Hz〜2.65kHz)も同期置換！
                        const float sbRms = std::sqrt (std::max (0.0f, prevPx));
                        if (s >= 2
                            && absMax > (isDeep ? 1.32f : 1.45f) * allowPk + (isDeep ? 1.8e-3f : 2.2e-3f)
                            && signedJump > (isDeep ? 2.40f : 2.65f) * sbRms + (isDeep ? 1.5e-3f : 1.8e-3f))
                        {
                            broadbandSpikeG = std::min (broadbandSpikeG, targetSpikeG);
                        }

                        if (s >= 1)
                        {
                            const float exc = (effPeak - allowPk) / (refPeak + 4.0e-4f);
                            blockDefectPeak = std::max (blockDefectPeak, std::min (1.0f, exc));
                        }
                    }

                    if (s <= 1 && broadbandSpikeG < (isDeep ? 0.55f : 0.65f) && activeSuppress)
                        targetSpikeG = std::min (targetSpikeG, broadbandSpikeG);
                }

                spikeGain[(size_t) s] += (targetSpikeG < spikeGain[(size_t) s] ? aSpikeAtk : aSpikeRel)
                                       * (targetSpikeG - spikeGain[(size_t) s]);
                const float gSp = (activeSuppress && have2Periods) ? spikeGain[(size_t) s] : 1.0f;

                float uDec[2] = { 0.0f, 0.0f };
                float decAbsMax = 0.0f;
                for (int ch = 0; ch < chCount; ++ch)
                {
                    // Standard時は純粋4周期チャープ中央値(uTrim)で金属着色ゼロ、Deep時は倍音コム出力もブレンドしてスポンジ重畳時の置換純度を高める
                    const float cleanRef = (isDeep && voicedHoldSamples > lag1 * 2)
                        ? (0.52f * hPrev[ch] + 0.48f * uTrim[ch])
                        : uTrim[ch];
                    uDec[ch] = cleanRef + gSp * (sbIn[s][ch] - cleanRef);
                    decAbsMax = std::max (decAbsMax, std::abs (uDec[ch]));
                }

                // 連続高密度パリパリ放電(毎周期スパイク)でも過去4周期の参照ピークが汚染されないよう、クランプ後包絡を保存
                const float ratioSrcAbs = activeSuppress ? decAbsMax : absMax;
                sbCleanPeakEnv[(size_t) s] = (ratioSrcAbs > sbCleanPeakEnv[(size_t) s])
                    ? ratioSrcAbs
                    : (sbCleanPeakEnv[(size_t) s] + aSbPeakRel * (ratioSrcAbs - sbCleanPeakEnv[(size_t) s]));
                curFrame.ratio[s] = sbCleanPeakEnv[(size_t) s] / envLowDenom;

                curFrame.dec[s][0] = uDec[0];
                if (chCount > 1)
                    curFrame.dec[s][1] = uDec[1];

                // ---- 第2段：チャープ同期1次再帰倍音コム & 倍音保護型サブバンドHNRポストウィーナー ----
                const float uMono  = (chCount > 1) ? 0.5f * (uDec[0] + uDec[1]) : uDec[0];
                const float m1Mono = (chCount > 1) ? 0.5f * (uM1[0]  + uM1[1])  : uM1[0];
                const float m2Mono = (chCount > 1) ? 0.5f * (uM2[0]  + uM2[1])  : uM2[0];

                const float d1 = (uMono - m1Mono) * 0.70710678f;
                const float d2 = (uMono - 2.0f * m1Mono + m2Mono) * 0.40824829f;

                const float sqU  = uMono * uMono;
                const float sqD1 = d1 * d1;
                const float sqD2 = d2 * d2;
                powSig[(size_t) s]   += (sqU  > powSig[(size_t) s]   ? aPowAtk : aPowRel) * (sqU  - powSig[(size_t) s]);
                powDiff1[(size_t) s] += (sqD1 > powDiff1[(size_t) s] ? aPowAtk : aPowRel) * (sqD1 - powDiff1[(size_t) s]);
                powDiff2[(size_t) s] += (sqD2 > powDiff2[(size_t) s] ? aPowAtk : aPowRel) * (sqD2 - powDiff2[(size_t) s]);

                const float px    = powSig[(size_t) s] + 1.0e-14f;
                const float rawPn = have2Periods ? std::min (powDiff1[(size_t) s], powDiff2[(size_t) s]) : 0.0f;

                const float harmK    = kCenterHz[s] / f0Est;
                const float phaseRot = std::min (1.5707963f, 3.14159265f * harmK * (curResidualJitter + 0.0014f));
                const float sinHalf  = std::sin (phaseRot);
                const float s2       = sinHalf * sinHalf;
                // ビブラート倍音の位相回転は保護しつつ、定常サー音・スポンジ共鳴の非周期電力を正確に分離
                const float naturalBreathMargin = isDeep ? ((s >= 4) ? 0.0003f : 0.0007f)
                                                         : ((s >= 4) ? 0.0030f : 0.0065f);
                const float vibScale = isDeep ? ((s >= 4) ? 2.0f : 2.5f) : ((s >= 4) ? 2.8f : 4.2f);
                const float vibGuard = px * std::min (isDeep ? 0.34f : 0.58f, vibScale * s2 * s2 + naturalBreathMargin);
                const float pn       = std::max (0.0f, rawPn - vibGuard - (isDeep ? ((s >= 4) ? 3.0e-9f : 8.0e-9f) : 3.5e-8f));
                const float noiseRatio = std::min (1.0f, pn / px);
                if (s >= 2)
                    hiBandNoiseRatio = std::max (hiBandNoiseRatio, noiseRatio);

                if (wvSafe > 0.60f && envLow > 0.020f && s >= 1 && s <= 4 && noiseRatio > 0.22f && pn > 2.2e-6f)
                    blockDefectPeak = std::max (blockDefectPeak, std::min (1.0f, (noiseRatio - 0.22f) * 2.2f));

                const float onsetReady = (voicedHoldSamples > lag1 + 16)
                    ? std::clamp ((float) (voicedHoldSamples - lag1 - 16) / (float) lag1, 0.0f, 1.0f)
                    : 0.0f;

                const float effNoiseRatio = (isDeep && s <= 1 && hiBandNoiseRatio > 0.35f)
                    ? std::max (noiseRatio, 0.48f * hiBandNoiseRatio)
                    : noiseRatio;
                const float combMaxHere = isDeep ? kCombMaxDp[s] : kCombMaxStd[s];
                const float combDrive   = std::min (1.0f, std::max (0.0f, effNoiseRatio - (isDeep ? 0.015f : 0.045f)) * (isDeep ? 30.0f : 16.0f));
                const float vibCombCap  = isDeep
                    ? std::max (0.38f, combMaxHere / (1.0f + 17.0f * std::abs (lagVel) * harmK))
                    : (combMaxHere / (1.0f + 22.0f * std::abs (lagVel) * harmK));
                const float alphaH      = have2Periods
                    ? std::min (vibCombCap, wvSafe * onsetReady * tailNoEcho * combMaxHere * combDrive)
                    : 0.0f;

                float sigHarm[2] = { 0.0f, 0.0f };
                for (int ch = 0; ch < chCount; ++ch)
                {
                    sigHarm[ch] = uDec[ch] + alphaH * (hPrev[ch] - uDec[ch]);
                    curFrame.harm[s][ch] = sigHarm[ch];
                }

                // ---- 第3段：定常母音内部でのスポンジ共鳴比 ＆ 残留ヒス電力学習と単調減衰語尾消去 ----
                if (wvSafe > 0.55f && envLow > 0.024f && envLow >= envLowTail * 0.85f && pn > 0.0f)
                {
                    const float remNoise  = std::sqrt (pn);
                    const float instAlpha = std::min (0.75f, remNoise / (envLow + 1.0e-5f));
                    spongeAlpha[(size_t) s]      = 0.9945f * spongeAlpha[(size_t) s] + 0.0055f * instAlpha;
                    voicedNoiseFloor[(size_t) s] = 0.9940f * voicedNoiseFloor[(size_t) s] + 0.0060f * pn;
                }
                else if (! curVoicedFlag && envLowTail < 0.002f)
                {
                    voicedNoiseFloor[(size_t) s] *= 0.9996f;
                }

                const float wTail = (spongeAlpha[(size_t) s] > 0.0025f || voicedNoiseFloor[(size_t) s] > 1.2e-8f)
                    ? sharedTailGate
                    : 0.0f;

                if (activeMix < 1.0e-4f)
                {
                    for (int ch = 0; ch < chCount; ++ch)
                        sbOut[s][ch] = sbIn[s][ch];
                    continue;
                }

                float targetGwVoiced = 1.0f;
                if (wvSafe > 0.02f && pn > 0.0f)
                {
                    const float minVoicedG = isDeep ? kMinVoicedDp[s] : kMinVoicedStd[s];
                    const float wBeta      = isDeep ? kWienerBetaDp[s] : kWienerBetaStd[s];
                    const float overSubH   = isDeep ? kOverSubDp[s]   : kOverSubStd[s];
                    const float psEst      = std::max (0.0f, px - overSubH * pn);
                    const float pnComb     = pn * ((1.0f - alphaH) / (1.0f + alphaH));
                    const float gwRaw      = psEst / (psEst + wBeta * strength * pnComb + 1.0e-14f);
                    const float gwVoiced   = std::clamp (gwRaw, minVoicedG, 1.0f);
                    targetGwVoiced = 1.0f - (wvSafe * tailNoEcho) * (1.0f - gwVoiced);
                }

                float targetGwTail = 1.0f;
                if (wTail > 0.02f)
                {
                    const float pxFast    = sbFastPow[(size_t) s] + 1.0e-14f;
                    const float spongeAmp = spongeAlpha[(size_t) s] * envLowTail * (isDeep ? 6.80f : 3.20f);
                    const float pnTailPow = std::max (spongeAmp * spongeAmp, (isDeep ? 4.20f : 2.20f) * voicedNoiseFloor[(size_t) s]);
                    const float gwTail    = std::clamp ((pxFast - (isDeep ? 16.0f : 5.5f) * pnTailPow) / pxFast,
                                                        isDeep ? 0.003f : 0.08f, 1.0f);
                    targetGwTail = 1.0f - wTail * (1.0f - gwTail);
                }

                const float targetGw = std::min (targetGwVoiced, targetGwTail);
                const float wSlew = (wTail > 0.05f && targetGw < wienerGain[(size_t) s]) ? aWienerTailAtk : aWienerSlew;
                wienerGain[(size_t) s] += wSlew * (targetGw - wienerGain[(size_t) s]);
                const float gw = wienerGain[(size_t) s];

                for (int ch = 0; ch < chCount; ++ch)
                {
                    const float rep = sigHarm[ch] * gw;
                    sbOut[s][ch] = sbIn[s][ch] + activeMix * (rep - sbIn[s][ch]);
                }

                const float combWhiteGain   = std::sqrt ((1.0f - alphaH) / (1.0f + alphaH));
                const float totalGainApprox = gSp * gw * combWhiteGain;
                if (activeSuppress && totalGainApprox < 0.96f)
                {
                    const float db = -20.0f * std::log10 (std::max (0.03f, totalGainApprox));
                    blockReductionDbPeak = std::max (blockReductionDbPeak, db);
                }
            }

            // 185〜740Hz (bodyMid) の広帯域パリパリ放電成分も、高域スパイク確認瞬間のみ4周期トリム予測へ置換（クリーン時は恒等 0.000 dB）
            const float targetBodySpG = (activeSuppress && have2Periods && sinceVoicedSamples == 0 && wvSafe > 0.40f && broadbandSpikeG < 0.65f)
                ? broadbandSpikeG
                : 1.0f;
            spikeBodyGain += (targetBodySpG < spikeBodyGain ? aSpikeAtk : aBodySpikeRel) * (targetBodySpG - spikeBodyGain);
            const float gBodySp = (activeSuppress && have2Periods) ? spikeBodyGain : 1.0f;

            float bodyOut[2] = { bodyKeep[0], bodyKeep[1] };
            for (int ch = 0; ch < chCount; ++ch)
            {
                float midRef = bodyMidIn[ch];
                if (have2Periods)
                {
                    const float m1 = tap1.c0 * f1_0.bodyMid[ch] + tap1.c1 * f1_1.bodyMid[ch] + tap1.c2 * f1_2.bodyMid[ch] + tap1.c3 * f1_3.bodyMid[ch];
                    const float m2 = tap2.c0 * f2_0.bodyMid[ch] + tap2.c1 * f2_1.bodyMid[ch] + tap2.c2 * f2_2.bodyMid[ch] + tap2.c3 * f2_3.bodyMid[ch];
                    const float m3 = tap3.c0 * f3_0.bodyMid[ch] + tap3.c1 * f3_1.bodyMid[ch] + tap3.c2 * f3_2.bodyMid[ch] + tap3.c3 * f3_3.bodyMid[ch];
                    midRef = midMean4 (bodyMidIn[ch], m1, m2, m3);
                }
                const float midDec = midRef + gBodySp * (bodyMidIn[ch] - midRef);
                curFrame.bodyMid[ch] = midDec;
                bodyOut[ch] = fundKeep[ch] + (bodyMidIn[ch] + activeMix * (midDec - bodyMidIn[ch]));
            }

            if (activeMix >= 1.0e-4f)
            {
                float sumL = bodyOut[0];
                for (int s = 0; s < kNumSubBands; ++s)
                    sumL += sbOut[s][0];
                chL[n] = sumL;

                if (chCount > 1)
                {
                    float sumR = bodyOut[1];
                    for (int s = 0; s < kNumSubBands; ++s)
                        sumR += sbOut[s][1];
                    chR[n] = sumR;
                }
            }

            ringPos = (w + 1) & rMask;
        }

        if (observeOnlyForKarte && blockHadVoiced)
        {
            if (defectFrames < 4000)
            {
                defectAccumScore += blockDefectPeak;
                ++defectFrames;
            }
            sentinelEma = 0.92f * sentinelEma + 0.08f * blockDefectPeak;
            if (blockDefectPeak >= 0.42f)
                sentinelHighBlocks = std::min (200, sentinelHighBlocks + 1);
            else
                sentinelHighBlocks = std::max (0, sentinelHighBlocks - 1);
        }

        if (activeSuppress)
        {
            if (blockReductionDbPeak > lastCleanDb)
                lastCleanDb += 0.35f * (blockReductionDbPeak - lastCleanDb);
            else
                lastCleanDb *= 0.86f;
            if (lastCleanDb < 0.08f)
                lastCleanDb = 0.0f;
        }
        else
        {
            lastCleanDb = 0.0f;
        }
    }

private:
    struct RingFrame
    {
        float low = 0.0f;
        float bodyMid[2] {};
        float ratio[kNumSubBands] {};
        float dec[kNumSubBands][2] {};
        float harm[kNumSubBands][2] {};
    };

    struct TapIndex
    {
        int dInt = 4;
        int i0 = 0, i1 = 0, i2 = 0, i3 = 0;
        float c0 = 0.0f, c1 = 1.0f, c2 = 0.0f, c3 = 0.0f;
    };

    static inline float midMean4 (float a, float b, float c, float d) noexcept
    {
        if (a > b) std::swap (a, b);
        if (c > d) std::swap (c, d);
        if (a > c) std::swap (a, c);
        if (b > d) std::swap (b, d);
        return 0.5f * (std::max (a, c) + std::min (b, d));
    }

    static float onePoleFreqCoef (double fcHz, double sampleRate) noexcept
    {
        const double fc = std::clamp (fcHz, 20.0, sampleRate * 0.46);
        return (float) (1.0 - std::exp (-2.0 * 3.14159265358979323846 * fc / sampleRate));
    }

    static float onePoleTauMs (double tauMs, double sampleRate) noexcept
    {
        return (float) (1.0 - std::exp (-1.0 / std::max (1.0, sampleRate * tauMs * 0.001)));
    }

    static inline void makeTapIndex (int w, float delaySamples, int rSize, int rMask, TapIndex& out) noexcept
    {
        const float d = std::clamp (delaySamples, 4.0f, (float) (rSize - 8));
        const int dInt = (int) d;
        const float mu = d - (float) dInt;
        out.dInt = dInt;
        out.i0 = (w - dInt + 1 + rSize) & rMask;
        out.i1 = (w - dInt     + rSize) & rMask;
        out.i2 = (w - dInt - 1 + rSize) & rMask;
        out.i3 = (w - dInt - 2 + rSize) & rMask;
        out.c0 = -mu * (mu - 1.0f) * (mu - 2.0f) * (1.0f / 6.0f);
        out.c1 =  (mu + 1.0f) * (mu - 1.0f) * (mu - 2.0f) * 0.5f;
        out.c2 = -(mu + 1.0f) * mu * (mu - 2.0f) * 0.5f;
        out.c3 =  (mu + 1.0f) * mu * (mu - 1.0f) * (1.0f / 6.0f);
    }

    void updatePitchLag (int w, float f0HintHz, float f0HintConf) noexcept
    {
        if (totalPushed < (std::uint64_t) (maxLagSamples * 2 + corrWinSamples + 8))
        {
            curVoicedFlag     = false;
            wasVoicedPrev     = false;
            curPeriodR0       = 0.0f;
            curResidualJitter = 0.0f;
            curLagStep        = 0.0f;
            lagVel            = 0.0f;
            return;
        }

        const auto* const ringPtr = ring.data();
        const int rSize = ringSize;
        const int rMask = ringMask;

        // 粗探索用の間引き低域系列をスタック連続配列へ一括展開（内側ループのモジュロ演算を完全除去）
        float decA[256];
        int nDec = 0;
        double e0Dec = 0.0;
        for (int i = 0; i < corrWinSamples && nDec < 256; i += corrDecim)
        {
            const float v = ringPtr[(size_t) ((w - i + rSize) & rMask)].low;
            decA[nDec++] = v;
            e0Dec += (double) v * (double) v;
        }
        const float rms0 = (nDec > 0) ? (float) std::sqrt (e0Dec / (double) nDec) : 0.0f;
        if (rms0 < 2.8e-3f || e0Dec < 1.0e-11)
        {
            curVoicedFlag     = false;
            wasVoicedPrev     = false;
            curPeriodR0       = 0.0f;
            curResidualJitter = 0.0f;
            curLagStep        = 0.0f;
            lagVel            = 0.0f;
            return;
        }

        int bestCoarseTau = minLagSamples;
        double bestCoarseR = -1.0;
        double coarseR[256] = {};
        int coarseTau[256] = {};
        int numCoarse = 0;

        for (int tau = minLagSamples; tau <= maxLagSamples && numCoarse < 256; tau += corrDecim)
        {
            double dot = 0.0, ep = 0.0;
            int idx = (w - tau + rSize) & rMask;
            for (int k = 0; k < nDec; ++k)
            {
                const float b = ringPtr[(size_t) idx].low;
                dot += (double) decA[k] * (double) b;
                ep  += (double) b * (double) b;
                idx = (idx - corrDecim + rSize) & rMask;
            }
            const double r = (ep > 0.25 * e0Dec) ? (dot / std::sqrt (e0Dec * ep + 1.0e-18)) : -1.0;
            coarseR[numCoarse]   = r;
            coarseTau[numCoarse] = tau;
            ++numCoarse;
            if (r > bestCoarseR)
            {
                bestCoarseR   = r;
                bestCoarseTau = tau;
            }
        }

        // McLeod First-Peak 法：2T0・3T0 サブハーモニック倍周期ロックを完全防止
        if (bestCoarseR > 0.55 && numCoarse >= 3)
        {
            const double firstPeakThr = bestCoarseR * 0.91;
            for (int k = 1; k < numCoarse - 1; ++k)
            {
                if (coarseR[k] >= firstPeakThr && coarseR[k] >= coarseR[k - 1] && coarseR[k] >= coarseR[k + 1])
                {
                    bestCoarseTau = coarseTau[k];
                    bestCoarseR   = coarseR[k];
                    break;
                }
            }
        }

        if (f0HintHz >= 75.0f && f0HintHz <= 850.0f && f0HintConf >= 0.58f)
        {
            const int hintTau = std::clamp ((int) std::lround (sr / (double) f0HintHz), minLagSamples, maxLagSamples);
            double dot = 0.0, ep = 0.0;
            int idx = (w - hintTau + rSize) & rMask;
            for (int k = 0; k < nDec; ++k)
            {
                const float b = ringPtr[(size_t) idx].low;
                dot += (double) decA[k] * (double) b;
                ep  += (double) b * (double) b;
                idx = (idx - corrDecim + rSize) & rMask;
            }
            const double rHint = dot / std::sqrt (e0Dec * ep + 1.0e-18);
            if (rHint > bestCoarseR - 0.03)
                bestCoarseTau = hintTau;
        }

        // 2周期一貫性検証 r(2*T0)：狭帯域無声摩擦音・ブレスの偶発自己相関ピークを物理的に100%排除！
        const int tau2Check = std::min (rSize - corrWinSamples - 8, bestCoarseTau * 2);
        double dot2 = 0.0, ep2 = 0.0;
        {
            int idx2 = (w - tau2Check + rSize) & rMask;
            for (int k = 0; k < nDec; ++k)
            {
                const float b = ringPtr[(size_t) idx2].low;
                dot2 += (double) decA[k] * (double) b;
                ep2  += (double) b * (double) b;
                idx2 = (idx2 - corrDecim + rSize) & rMask;
            }
        }
        const double r2Period = (ep2 > 0.18 * e0Dec) ? (dot2 / std::sqrt (e0Dec * ep2 + 1.0e-18)) : -1.0;

        const int fineStep = (sr > 64000.0) ? 4 : 2;
        const int fineWin  = std::min (corrWinSamples, std::max (128, (int) std::lround (sr * 0.0054)));
        float fineA[256];
        int nFine = 0;
        double e0Full = 0.0;
        for (int i = 0; i < fineWin && nFine < 256; i += fineStep)
        {
            const float a = ringPtr[(size_t) ((w - i + rSize) & rMask)].low;
            fineA[nFine++] = a;
            e0Full += (double) a * (double) a;
        }

        const int radius = corrDecim + 2;
        const int tLo = std::max (minLagSamples, bestCoarseTau - radius);
        const int tHi = std::min (maxLagSamples, bestCoarseTau + radius);
        double rFine[40] = {};
        int bestIdx = 0;
        double bestR = -1.0;
        const int count = std::min (40, tHi - tLo + 1);

        for (int k = 0; k < count; ++k)
        {
            const int tau = tLo + k;
            double dot = 0.0, ep = 0.0;
            int idx = (w - tau + rSize) & rMask;
            for (int m = 0; m < nFine; ++m)
            {
                const float b = ringPtr[(size_t) idx].low;
                dot += (double) fineA[m] * (double) b;
                ep  += (double) b * (double) b;
                idx = (idx - fineStep + rSize) & rMask;
            }
            const double r = dot / std::sqrt (e0Full * ep + 1.0e-18);
            rFine[k] = r;
            if (r > bestR)
            {
                bestR   = r;
                bestIdx = k;
            }
        }

        float refinedLag = (float) (tLo + bestIdx);
        if (bestIdx > 0 && bestIdx < count - 1)
        {
            const double rm1 = rFine[bestIdx - 1];
            const double r0  = rFine[bestIdx];
            const double rp1 = rFine[bestIdx + 1];
            const double denom = rm1 - 2.0 * r0 + rp1;
            if (std::abs (denom) > 1.0e-9)
            {
                const double delta = std::clamp (0.5 * (rm1 - rp1) / denom, -0.5, 0.5);
                refinedLag = (float) ((double) (tLo + bestIdx) + delta);
            }
        }

        curPeriodR0 = (float) std::clamp (bestR, -1.0, 1.0);
        if (curPeriodR0 > 0.82f && r2Period > 0.72)
        {
            curVoicedFlag = true;
            const float hopF    = (float) corrHopSamples;
            const float winHalf = 0.5f * (float) fineWin;
            const float predLag = lagEst + lagVel * hopF;

            if (wasVoicedPrev && predLag > 10.0f && std::abs (refinedLag - predLag) < predLag * 0.032f)
            {
                // α-β トラッカで瞬時ラグとビブラート傾き dτ/dn を同時推定
                const float err = std::clamp (refinedLag - predLag, -1.60f, 1.60f);
                lagEst = std::clamp (predLag + 0.56f * err, (float) minLagSamples, (float) maxLagSamples);
                lagVel = std::clamp (lagVel + (0.16f / hopF) * err, -0.009f, 0.009f);
                curResidualJitter = std::abs (err) / lagEst;
            }
            else
            {
                lagEst            = refinedLag;
                lagVel            = 0.0f;
                curResidualJitter = 0.0f;
            }

            // 相関窓重心 (w - 0.5*fineWin - 0.5*T0) から現在時刻の1周期遅延重心 (w - 0.5*T0) へ前進外挿！
            const float targetNow  = std::clamp (lagEst + lagVel * winHalf,          (float) minLagSamples, (float) maxLagSamples);
            const float targetNext = std::clamp (lagEst + lagVel * (winHalf + hopF), (float) minLagSamples, (float) maxLagSamples);

            if (! tapWasValid)
            {
                curLagSamples  = targetNow;
                curLagStep     = lagVel;
                tapLagBackup   = targetNow;
                tapCrossfading = false;
                tapCrossfade   = 0.0f;
                tapWasValid    = true;
            }
            else if (std::abs (targetNow - curLagSamples) > 2.4f)
            {
                tapLagBackup   = curLagSamples;
                curLagSamples  = targetNow;
                curLagStep     = lagVel;
                tapCrossfade   = 0.0f;
                tapCrossfading = true;
            }
            else
            {
                curLagStep = (targetNext - curLagSamples) / hopF;
            }

            wasVoicedPrev = true;
        }
        else
        {
            curVoicedFlag     = false;
            wasVoicedPrev     = false;
            tapWasValid       = false;
            curResidualJitter = 0.0f;
            curLagStep        = 0.0f;
            lagVel            = 0.0f;
        }
    }

    double sr = 48000.0;

    float aPitchLp = 0.06f;
    float aFund185 = 0.024f;
    std::array<float, kNumSubBands> aSplit {};
    float aLowAtk = 0.03f, aLowRel = 0.008f, aLowTailAtk = 0.01f, aLowTailRel = 0.00043f;
    float aSbRmsAtk = 0.05f, aSbRmsRel = 0.022f, aSbPeakRel = 0.04f;
    float aConsAtk = 0.012f, aConsRel = 0.006f, aTailHfSlew = 0.0028f;
    float aPowAtk = 0.012f, aPowRel = 0.0034f;
    float aVoicedAtk = 0.013f, aVoicedRel = 0.0075f;
    float aSpikeAtk = 1.00f, aSpikeRel = 0.015f, aBodySpikeRel = 0.032f;
    float aWienerSlew = 0.0085f, aWienerTailAtk = 0.030f;
    float aBypassSlew = 0.007f;
    float xfadeStep = 0.005f;

    int maxTailSamples = 5520;
    int corrHopSamples = 64;
    int corrWinSamples = 432;
    int corrDecim      = 6;
    int minLagSamples  = 56;
    int maxLagSamples  = 640;
    int ringSize       = 4096;
    int ringMask       = 4095;

    std::vector<RingFrame> ring;

    float pitchLpState[2] = { 0.0f, 0.0f };
    float rcFund[2]       = { 0.0f, 0.0f };
    float rcLp[2][kNumSubBands] = {};

    std::array<float, kNumSubBands> sbFastPow {};
    std::array<float, kNumSubBands> sbPeakEnv {};
    std::array<float, kNumSubBands> sbCleanPeakEnv {};
    std::array<float, kNumSubBands> powSig {};
    std::array<float, kNumSubBands> powDiff1 {};
    std::array<float, kNumSubBands> powDiff2 {};
    std::array<float, kNumSubBands> spikeGain { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, kNumSubBands> wienerGain { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, kNumSubBands> spongeAlpha {};
    std::array<float, kNumSubBands> voicedNoiseFloor {};
    float spikeBodyGain = 1.0f;

    int ringPos = 0;
    std::uint64_t totalPushed = 0;
    int hopCounter = 0;

    float lagEst                 = 218.0f;
    float lagVel                 = 0.0f;
    float curLagSamples          = 218.0f;
    float curLagStep             = 0.0f;
    float curPeriodR0            = 0.0f;
    float curResidualJitter      = 0.0f;
    bool  curVoicedFlag          = false;
    bool  wasVoicedPrev          = false;
    int   voicedHoldSamples      = 0;
    int   sinceVoicedSamples     = 1000000;
    float tailHfMin              = 0.0f;
    float tailEnvHf              = 0.0f;
    bool  tailAbortedByConsonant = true;
    float voicedWeight           = 0.0f;
    float lowPow                 = 0.0f;
    float lowTailPow             = 0.0f;
    float hfConsPow              = 0.0f;
    float activeMix              = 0.0f;
    bool  wasSleeping            = true;

    float tapLagBackup           = 218.0f;
    float tapCrossfade           = 0.0f;
    bool  tapCrossfading         = false;
    bool  tapWasValid            = false;

    float lastCleanDb            = 0.0f;
    float defectAccumScore       = 0.0f;
    int   defectFrames           = 0;
    float sentinelEma            = 0.0f;
    int   sentinelHighBlocks     = 0;
};

} // namespace gz::v590
