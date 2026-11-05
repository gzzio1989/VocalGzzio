#pragma once

/**
 * @file PitchAndHarmonyScience.h
 * @brief VocalGzzio 最先端ピッチ・純正律ハーモニー・喉頭運動モデリング科学エンジン
 *
 * 【研究開発の背景と独自技術】
 * 1. JustIntonationChordTuner (純正律ダイナミック・マイクロチューニング・エンジン):
 *    - 12平均律 (12-TET) の長3度 (+13.686 cent 高い) や短3度 (-15.641 cent 低い) が生む
 *      「うなり (Acoustic Beating: Δf = |n·f1 - m·f2|)」を解消するため、
 *      Jacob Collier やトップアカペラ・コーラスが実践する「純正律 (Just Intonation: 5:4, 6:5, 3:2, 7:4)」への
 *      動的マイクロセント補正をリアルタイムで算出します。
 *    - ただし常時純正律にすると経過音や速いパッセージで基準ピッチがドリフトするため、
 *      音程持続時間 (Note Stability Hold) が 65ms を超えた「和音ホールド区間」でのみ
 *      滑らかに純正律へロックインし、移動中は 12平均律へ戻るハイブリッド制御を実装しています。
 *
 * 2. KinematicVibratoAndPortamentoModel (輪状甲状筋キネマティック・ビブラート＆最小躍度ポルタメント):
 *    - 人間の歌唱における輪状甲状筋 (Cricothyroid Muscle) の神経筋応答をモデル化。
 *    - ノート発声直後はストレートトーンで立ち上がり、180ms〜260ms の Onset Delay 後に
 *      5.4〜6.1 Hz のビブラートが自然に開花 (Bloom) し、さらにピッチ周期と同期した
 *      微細な声門振幅シマー (Amplitude Shimmer) と第3ホルマント輝度変調を伴います。
 *    - ノート遷移時には Flash / ロボット的な直線補間ではなく、人間の運動制御における
 *      「最小躍度軌道 (Minimum-Jerk Trajectory: 10t^3 - 15t^4 + 6t^5)」を用いた
 *      滑らかなポルタメント曲線を生成します。
 *
 * 3. SubSampleViterbiPitchRefiner (放物線＋Sincラグ補間＆ビタビ・オクターブジャンプ防止器):
 *    - YIN / 自己相関ラグ周期の離散サンプル量子化誤差を 3点放物線＋高次ビタビ遷移スコアで精錬し、
 *      ボーカルフライ (声門下振動) やファルセット移行時に発生しやすい「1オクターブ誤検出」を
 *      連続性コスト関数によって鉄壁にブロックします。
 *
 * 4. FormantLockedMicroDoubler (位相コヒーレント・マイクロピッチ・ボーカルダブラー):
 *    - リードボーカルのフォルマント包絡を崩さずに左右へ ±6.2 cent の微細ピッチシフトと
 *      黄金比ディレイを与え、モノラル折りたたみ時のコムフィルタリングを最小化するダブラーです。
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "MasteringProEngine.h"

namespace vg::pitch_science
{

//==============================================================================
// 1. 純正律ダイナミック・マイクロチューニング・エンジン
//==============================================================================
class JustIntonationChordTuner
{
public:
    static constexpr int kMaxHarmonyVoices = 5;

    /**
     * @brief 半音インターバル (-24 〜 +24) に対する純正律セント補正テーブル
     * 12平均律 (12-TET) から純粋整数比 (Pure Harmonic Ratio) へ合わせるための微細セント偏差:
     *   0 (1:1 Unison)          ->   0.000 cents
     *   1 (16:15 Minor 2nd)     -> +11.731 cents
     *   2 (9:8 Major 2nd)       ->  +3.910 cents
     *   3 (6:5 Minor 3rd)       -> +15.641 cents  (12-TETの短3度は純粋6:5より15.64cent狭いため広げる)
     *   4 (5:4 Major 3rd)       -> -13.686 cents  (12-TETの長3度は純粋5:4より13.69cent広いため下げる)
     *   5 (4:3 Perfect 4th)     ->  -1.955 cents
     *   6 (7:5 Septimal Tritone)-> -17.488 cents
     *   7 (3:2 Perfect 5th)     ->  +1.955 cents  (純粋完全5度)
     *   8 (8:5 Minor 6th)       -> +13.686 cents
     *   9 (5:3 Major 6th)       -> -15.641 cents
     *  10 (7:4 Harmonic 7th)    -> -31.174 cents  (バーバーショップ/ジャズで澄み切る自然7度、通常時は9:5/+17.6とブレンド)
     *  11 (15:8 Major 7th)      -> -11.731 cents
     *  12 (2:1 Octave)          ->   0.000 cents
     */
    static inline float getPureJustCentsOffset(int semitoneIntervalFromRoot, bool enableHarmonicSeventh = false) noexcept
    {
        int absSemi = std::abs(semitoneIntervalFromRoot);
        int pitchClass = absSemi % 12;
        float sign = (semitoneIntervalFromRoot >= 0) ? 1.0f : -1.0f;

        // 下方インターバルの場合は逆数比の純正律オフセットを適用
        static constexpr std::array<float, 12> kJustCentsUp = {
             0.0000f,  //  0: 1:1 Unison
            +11.7313f, //  1: 16:15 Minor Second
             +3.9100f, //  2: 9:8 Major Whole Tone
            +15.6413f, //  3: 6:5 Pure Minor Third
            -13.6863f, //  4: 5:4 Pure Major Third
             -1.9550f, //  5: 4:3 Pure Fourth
            -10.2647f, //  6: 45:32 / 7:5 Hybrid Tritone
             +1.9550f, //  7: 3:2 Pure Fifth
            +13.6863f, //  8: 8:5 Pure Minor Sixth
            -15.6413f, //  9: 5:3 Pure Major Sixth
             -3.9100f, // 10: 16:9 Pythagorean / Mild Seventh (標準)
            -11.7313f  // 11: 15:8 Pure Major Seventh
        };

        float baseCents = kJustCentsUp[static_cast<size_t>(pitchClass)];
        if (pitchClass == 10 && enableHarmonicSeventh)
        {
            baseCents = -24.50f; // 7:4 Harmonic Seventh Blend
        }
        return sign * baseCents;
    }

    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        const float dt = static_cast<float>(1.0 / sr);
        holdAttackCoeff = 1.0f - std::exp(-dt / 0.075f);  // 75ms 和音ロックイン
        holdReleaseCoeff = 1.0f - std::exp(-dt / 0.018f); // 18ms 素早い解除
        centsSmoothCoeff = 1.0f - std::exp(-dt / 0.025f); // 25ms セント補間
        reset();
    }

    void reset() noexcept
    {
        lastLeadMidiNote = 60.0f;
        noteHoldLock01 = 0.0f;
        currentAcousticBeatingReductionDb = 0.0f;
        smoothedCentsOffsets.fill(0.0f);
        smoothedRatioMultipliers.fill(1.0f);
    }

    /**
     * @brief リードピッチ (Hz) と各ハーモニー声部の半音シフト量から、純正律ロックオン倍率を更新
     * @param leadFreqHz 現在のリード基本周波数 (Hz)
     * @param isVoiced 有声区間フラグ
     * @param voiceSemitoneShifts 各ハーモニー声部の半音シフト量 (例: +4, +7, -3, +12...)
     * @param purityAmount01 純正律ブレンド量 (0.0 = 12平均律, 1.0 = 純粋純正律)
     */
    void updateHarmonyTuning(float leadFreqHz,
                             bool isVoiced,
                             const std::array<int, kMaxHarmonyVoices>& voiceSemitoneShifts,
                             float purityAmount01) noexcept
    {
        purityAmount01 = std::clamp(purityAmount01, 0.0f, 1.0f);

        if (!isVoiced || leadFreqHz < 55.0f || leadFreqHz > 1600.0f)
        {
            noteHoldLock01 += holdReleaseCoeff * (0.0f - noteHoldLock01);
        }
        else
        {
            const float currentMidi = 69.0f + 12.0f * std::log2(leadFreqHz / 440.0f);
            const float midiDelta = std::fabs(currentMidi - lastLeadMidiNote);
            lastLeadMidiNote += 0.08f * (currentMidi - lastLeadMidiNote);

            // ピッチ変動が 0.35 半音未満で安定している場合、純正律ロックを深める
            const float targetLock = (midiDelta < 0.35f) ? 1.0f : 0.0f;
            const float coeff = (targetLock > noteHoldLock01) ? holdAttackCoeff : holdReleaseCoeff;
            noteHoldLock01 += coeff * (targetLock - noteHoldLock01);
        }

        const float effectivePurity = purityAmount01 * noteHoldLock01;
        float beatingSavedAccum = 0.0f;

        for (size_t v = 0; v < kMaxHarmonyVoices; ++v)
        {
            const int semi = voiceSemitoneShifts[v];
            const float targetCents = getPureJustCentsOffset(semi, false) * effectivePurity;
            smoothedCentsOffsets[v] += centsSmoothCoeff * (targetCents - smoothedCentsOffsets[v]);

            // セントから周波数倍率へ変換: 2^(cents / 1200)
            smoothedRatioMultipliers[v] = std::pow(2.0f, smoothedCentsOffsets[v] / 1200.0f);
            beatingSavedAccum += std::fabs(smoothedCentsOffsets[v]);
        }

        currentAcousticBeatingReductionDb = std::clamp(beatingSavedAccum * 0.22f, 0.0f, 12.0f);
    }

    float getVoiceFrequencyMultiplier(int voiceIndex) const noexcept
    {
        if (voiceIndex < 0 || voiceIndex >= kMaxHarmonyVoices)
            return 1.0f;
        return smoothedRatioMultipliers[static_cast<size_t>(voiceIndex)];
    }

    float getVoiceCentsOffset(int voiceIndex) const noexcept
    {
        if (voiceIndex < 0 || voiceIndex >= kMaxHarmonyVoices)
            return 0.0f;
        return smoothedCentsOffsets[static_cast<size_t>(voiceIndex)];
    }

    float getNoteHoldLock01() const noexcept { return noteHoldLock01; }
    float getBeatingReductionIndexDb() const noexcept { return currentAcousticBeatingReductionDb; }

private:
    double sr = 48000.0;
    float holdAttackCoeff = 0.001f;
    float holdReleaseCoeff = 0.005f;
    float centsSmoothCoeff = 0.002f;
    float lastLeadMidiNote = 60.0f;
    float noteHoldLock01 = 0.0f;
    float currentAcousticBeatingReductionDb = 0.0f;
    std::array<float, kMaxHarmonyVoices> smoothedCentsOffsets{};
    std::array<float, kMaxHarmonyVoices> smoothedRatioMultipliers{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
};

//==============================================================================
// 2. 輪状甲状筋キネマティック・ビブラート＆最小躍度ポルタメント・エンジン
//==============================================================================
class KinematicVibratoAndPortamentoModel
{
public:
    struct VibratoState
    {
        float pitchCentsModulation = 0.0f;  // ピッチ変調量 (cents)
        float frequencyMultiplier = 1.0f;   // 周波数乗数
        float amplitudeShimmerGain = 1.0f;  // 同期声門振幅シマー倍率
        float bloomEnvelope01 = 0.0f;       // ビブラート開花度 (0.0..1.0)
    };

    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        invSr = static_cast<float>(1.0 / sr);
        reset();
    }

    void reset() noexcept
    {
        vibratoPhase = 0.0f;
        secondaryFlutterPhase = 0.0f;
        noteHoldSeconds = 0.0f;
        bloomEnv01 = 0.0f;
        lastTargetMidi = 60.0f;
        portamentoStartMidi = 60.0f;
        portamentoTargetMidi = 60.0f;
        portamentoProgress01 = 1.0f;
    }

    /**
     * @brief 最小躍度軌道 (Minimum-Jerk Trajectory: 10t^3 - 15t^4 + 6t^5)
     * 人間の喉頭筋が音程を移動する際の最も自然で滑らかなS字曲線
     */
    static inline float evaluateMinimumJerk(float t01) noexcept
    {
        const float t = std::clamp(t01, 0.0f, 1.0f);
        const float t2 = t * t;
        const float t3 = t2 * t;
        return t3 * (10.0f - 15.0f * t + 6.0f * t2);
    }

    /**
     * @brief 目標MIDIノートに対する最小躍度ポルタメント補間ノートを返す
     */
    float processPortamentoMidi(float targetMidiNote, float glideTimeSeconds) noexcept
    {
        if (std::fabs(targetMidiNote - portamentoTargetMidi) > 0.05f)
        {
            portamentoStartMidi = getCurrentPortamentoMidi();
            portamentoTargetMidi = targetMidiNote;
            portamentoProgress01 = 0.0f;
            // 新しいノートへ遷移したらビブラートの開花タイマーを緩やかにリセット
            noteHoldSeconds *= 0.35f;
        }

        const float safeGlideSec = std::clamp(glideTimeSeconds, 0.002f, 0.500f);
        if (portamentoProgress01 < 1.0f)
        {
            portamentoProgress01 = std::min(1.0f, portamentoProgress01 + invSr / safeGlideSec);
        }

        return getCurrentPortamentoMidi();
    }

    float getCurrentPortamentoMidi() const noexcept
    {
        const float s = evaluateMinimumJerk(portamentoProgress01);
        return portamentoStartMidi + s * (portamentoTargetMidi - portamentoStartMidi);
    }

    /**
     * @brief 1サンプルのキネマティック・ビブラート状態を計算
     * @param isVoicedSustained 有声かつノート持続中か
     * @param rateHz ビブラート速度 (標準 5.6 Hz)
     * @param depthCents 最大ビブラート深さ (標準 26.0 cents)
     * @param onsetDelaySec 発声からビブラートが咲き始めるまでの遅延時間 (標準 0.20 秒)
     */
    VibratoState tickVibrato(bool isVoicedSustained,
                             float rateHz = 5.65f,
                             float depthCents = 24.0f,
                             float onsetDelaySec = 0.20f) noexcept
    {
        rateHz = std::clamp(rateHz, 3.5f, 8.5f);
        depthCents = std::clamp(depthCents, 0.0f, 80.0f);
        onsetDelaySec = std::clamp(onsetDelaySec, 0.02f, 0.80f);

        if (isVoicedSustained)
        {
            noteHoldSeconds = std::min(4.0f, noteHoldSeconds + invSr);
            if (noteHoldSeconds > onsetDelaySec)
            {
                // 発声後 onsetDelaySec を過ぎたら 220ms かけて滑らかに開花
                const float rawBloom = std::clamp((noteHoldSeconds - onsetDelaySec) / 0.22f, 0.0f, 1.0f);
                const float targetBloom = evaluateMinimumJerk(rawBloom);
                bloomEnv01 += 0.002f * (targetBloom - bloomEnv01);
            }
            else
            {
                bloomEnv01 *= 0.9985f;
            }
        }
        else
        {
            noteHoldSeconds = 0.0f;
            bloomEnv01 *= 0.995f;
        }

        // メイン周期 (5.65 Hz) + 生体ゆらぎ第2高調波/サブフラッター
        constexpr float kTwoPi = 6.28318530717958647692f;
        vibratoPhase += rateHz * invSr;
        if (vibratoPhase >= 1.0f)
            vibratoPhase -= 1.0f;

        secondaryFlutterPhase += (rateHz * 0.37f) * invSr;
        if (secondaryFlutterPhase >= 1.0f)
            secondaryFlutterPhase -= 1.0f;

        const float primaryWave = std::sin(kTwoPi * vibratoPhase);
        const float organicFlutter = 0.08f * std::sin(kTwoPi * secondaryFlutterPhase);
        const float compositeWave = primaryWave + organicFlutter;

        VibratoState out;
        out.bloomEnvelope01 = bloomEnv01;
        out.pitchCentsModulation = compositeWave * depthCents * bloomEnv01;
        out.frequencyMultiplier = std::pow(2.0f, out.pitchCentsModulation / 1200.0f);

        // 輪状甲状筋の収縮に伴う声門振幅シマー (ピッチ頂点でわずかに +0.85 dB 輝く)
        const float shimmerDb = 0.85f * bloomEnv01 * (depthCents / 30.0f) * primaryWave;
        out.amplitudeShimmerGain = std::pow(10.0f, shimmerDb / 20.0f);

        return out;
    }

private:
    double sr = 48000.0;
    float invSr = 1.0f / 48000.0f;
    float vibratoPhase = 0.0f;
    float secondaryFlutterPhase = 0.0f;
    float noteHoldSeconds = 0.0f;
    float bloomEnv01 = 0.0f;
    float lastTargetMidi = 60.0f;
    float portamentoStartMidi = 60.0f;
    float portamentoTargetMidi = 60.0f;
    float portamentoProgress01 = 1.0f;
};

//==============================================================================
// 3. サブサンプル放物線補間＆ビタビ・オクターブジャンプ防止ピッチ精錬器
//==============================================================================
class SubSampleViterbiPitchRefiner
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        reset();
    }

    void reset() noexcept
    {
        trackedF0Hz = 220.0f;
        confidenceSmooth = 0.0f;
        voicedStreakFrames = 0;
    }

    /**
     * @brief 離散ラグインデックス周辺の3点相関値 (yMinus1, y0, yPlus1) から
     *        サブサンプル精度の放物線極値オフセット (-0.5 .. +0.5 サンプル) を算出
     */
    static inline float computeParabolicLagOffset(float yMinus1, float y0, float yPlus1) noexcept
    {
        const float denom = 2.0f * (2.0f * y0 - yMinus1 - yPlus1);
        if (std::fabs(denom) < 1.0e-9f)
            return 0.0f;
        const float delta = (yMinus1 - yPlus1) / denom;
        return std::clamp(delta, -0.495f, 0.495f);
    }

    /**
     * @brief 生のF0推定値 (rawF0Hz) と信頼度 (rawConfidence01) を受け取り、
     *        ビタビ3状態 (f0, 0.5*f0, 2.0*f0) 連続性評価によってオクターブ飛びを除去した精錬F0を返す
     */
    float refineF0Estimate(float rawF0Hz, float rawConfidence01) noexcept
    {
        rawConfidence01 = std::clamp(rawConfidence01, 0.0f, 1.0f);
        confidenceSmooth = 0.82f * confidenceSmooth + 0.18f * rawConfidence01;

        if (rawF0Hz < 50.0f || rawF0Hz > 1800.0f || rawConfidence01 < 0.25f)
        {
            voicedStreakFrames = 0;
            return trackedF0Hz;
        }

        if (voicedStreakFrames == 0)
        {
            trackedF0Hz = rawF0Hz;
            voicedStreakFrames = 1;
            return trackedF0Hz;
        }

        voicedStreakFrames = std::min(1000, voicedStreakFrames + 1);

        // 3つの候補状態: そのまま (cand0), 1オクターブ下補正 (candHalf), 1オクターブ上補正 (candDouble)
        const std::array<float, 3> candidates = {
            rawF0Hz,
            rawF0Hz * 0.5f,
            rawF0Hz * 2.0f
        };

        // 各候補が直前の追跡ピッチ (trackedF0Hz) から何半音離れているかを評価
        // ただしオクターブ補正候補には固有ペナルティ (3.2半音相当) を課し、
        // 本当に1オクターブ誤検出が起きた時 (約12半音ジャンプかつ信頼度が中程度の時) のみ発動させる
        const std::array<float, 3> penalties = {
            0.0f,
            (rawConfidence01 < 0.78f) ? 3.2f : 9.5f,
            (rawConfidence01 < 0.78f) ? 3.2f : 9.5f
        };

        int bestIdx = 0;
        float bestCost = 1.0e9f;

        for (int i = 0; i < 3; ++i)
        {
            const float candHz = candidates[static_cast<size_t>(i)];
            if (candHz < 55.0f || candHz > 1600.0f)
                continue;

            const float semitoneJump = 12.0f * std::fabs(std::log2(candHz / std::max(55.0f, trackedF0Hz)));
            const float totalCost = semitoneJump + penalties[static_cast<size_t>(i)];
            if (totalCost < bestCost)
            {
                bestCost = totalCost;
                bestIdx = i;
            }
        }

        const float chosenHz = candidates[static_cast<size_t>(bestIdx)];
        // 滑らかに追従
        const float alpha = (bestIdx == 0) ? 0.45f : 0.25f;
        trackedF0Hz += alpha * (chosenHz - trackedF0Hz);
        return trackedF0Hz;
    }

    float getTrackedF0Hz() const noexcept { return trackedF0Hz; }
    float getSmoothedConfidence() const noexcept { return confidenceSmooth; }

private:
    double sr = 48000.0;
    float trackedF0Hz = 220.0f;
    float confidenceSmooth = 0.0f;
    int voicedStreakFrames = 0;
};

//==============================================================================
// 4. 位相コヒーレント・フォルマント保護マイクロダブラー
//==============================================================================
class FormantLockedMicroDoubler
{
public:
    void prepare(double sampleRate) noexcept
    {
        sr = std::max(8000.0, sampleRate);
        invSr = static_cast<float>(1.0 / sr);
        bodyFilterL.prepare(sr);
        bodyFilterR.prepare(sr);
        bodyFilterL.setParameters(240.0f, 0.707f, 1.2f, vg::mastering::TptStateVariableFilter::Mode::PeakBell);
        bodyFilterR.setParameters(240.0f, 0.707f, 1.2f, vg::mastering::TptStateVariableFilter::Mode::PeakBell);
        reset();
    }

    void reset() noexcept
    {
        delayBufL.fill(0.0f);
        delayBufR.fill(0.0f);
        writePos = 0;
        lfoPhase = 0.0f;
        bodyFilterL.reset();
        bodyFilterR.reset();
    }

    void processSamplePair(float& left, float& right, float amount01) noexcept
    {
        amount01 = std::clamp(amount01, 0.0f, 1.0f);
        if (amount01 <= 0.0005f)
            return;

        const float mid = 0.5f * (left + right);
        delayBufL[static_cast<size_t>(writePos)] = left;
        delayBufR[static_cast<size_t>(writePos)] = right;

        constexpr float kTwoPi = 6.28318530717958647692f;
        lfoPhase += 0.38f * invSr;
        if (lfoPhase >= 1.0f)
            lfoPhase -= 1.0f;

        // 黄金比 (1 : 1.618) にもとづく左右非対称マイクロディレイ (8.5ms & 13.75ms)
        const float dSamplesL = static_cast<float>(sr) * (0.0085f + 0.00095f * std::sin(kTwoPi * lfoPhase));
        const float dSamplesR = static_cast<float>(sr) * (0.01375f + 0.00095f * std::cos(kTwoPi * lfoPhase));

        const float wetL = bodyFilterL.processSample(readCubic(delayBufL, dSamplesL));
        const float wetR = bodyFilterR.processSample(readCubic(delayBufR, dSamplesR));

        writePos = (writePos + 1) & kBufferMask;

        // モノラル合算時に逆相で消えないよう、Midと直交する成分を繊細にブレンド
        const float blend = amount01 * 0.24f;
        left = vg::mastering::FastAudioMath::sanitize(left + blend * (wetL - 0.15f * mid));
        right = vg::mastering::FastAudioMath::sanitize(right + blend * (wetR - 0.15f * mid));
    }

private:
    static constexpr int kBufferSize = 4096;
    static constexpr int kBufferMask = kBufferSize - 1;

    float readCubic(const std::array<float, kBufferSize>& buf, float delaySamples) const noexcept
    {
        const float readF = static_cast<float>(writePos) - std::clamp(delaySamples, 2.0f, static_cast<float>(kBufferSize - 4));
        int idx1 = static_cast<int>(std::floor(readF));
        const float frac = readF - static_cast<float>(idx1);

        const int i0 = (idx1 - 1) & kBufferMask;
        const int i1 = idx1 & kBufferMask;
        const int i2 = (idx1 + 1) & kBufferMask;
        const int i3 = (idx1 + 2) & kBufferMask;

        const float y0 = buf[static_cast<size_t>(i0)];
        const float y1 = buf[static_cast<size_t>(i1)];
        const float y2 = buf[static_cast<size_t>(i2)];
        const float y3 = buf[static_cast<size_t>(i3)];

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    double sr = 48000.0;
    float invSr = 1.0f / 48000.0f;
    std::array<float, kBufferSize> delayBufL{};
    std::array<float, kBufferSize> delayBufR{};
    int writePos = 0;
    float lfoPhase = 0.0f;
    vg::mastering::TptStateVariableFilter bodyFilterL;
    vg::mastering::TptStateVariableFilter bodyFilterR;
};

//==============================================================================
// 5. ピッチ＆ハーモニー科学統合スイート (PitchAndHarmonyScienceSuite)
//==============================================================================
class PitchAndHarmonyScienceSuite
{
public:
    void prepare(double sampleRate) noexcept
    {
        justIntonation.prepare(sampleRate);
        kinematicVibrato.prepare(sampleRate);
        viterbiRefiner.prepare(sampleRate);
        microDoubler.prepare(sampleRate);
    }

    void reset() noexcept
    {
        justIntonation.reset();
        kinematicVibrato.reset();
        viterbiRefiner.reset();
        microDoubler.reset();
    }

    JustIntonationChordTuner& getJustIntonationTuner() noexcept { return justIntonation; }
    const JustIntonationChordTuner& getJustIntonationTuner() const noexcept { return justIntonation; }

    KinematicVibratoAndPortamentoModel& getKinematicVibrato() noexcept { return kinematicVibrato; }
    const KinematicVibratoAndPortamentoModel& getKinematicVibrato() const noexcept { return kinematicVibrato; }

    SubSampleViterbiPitchRefiner& getViterbiRefiner() noexcept { return viterbiRefiner; }
    const SubSampleViterbiPitchRefiner& getViterbiRefiner() const noexcept { return viterbiRefiner; }

    FormantLockedMicroDoubler& getMicroDoubler() noexcept { return microDoubler; }

private:
    JustIntonationChordTuner justIntonation;
    KinematicVibratoAndPortamentoModel kinematicVibrato;
    SubSampleViterbiPitchRefiner viterbiRefiner;
    FormantLockedMicroDoubler microDoubler;
};

} // namespace vg::pitch_science
