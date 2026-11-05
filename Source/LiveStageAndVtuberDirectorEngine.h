#pragma once

/**
 * @file LiveStageAndVtuberDirectorEngine.h
 * @brief VocalGzzio v5.5.0 歌枠・VTuber・ライブステージ自動演出ディレクター＆5母音リップシンク・ビゼーム解析＆Camelot調性遷移エンジン
 *
 * 歌枠配信・VTuberライブ・オンライン歌合戦において、ワンオペ配信者でも専属のPA＆演出ディレクターがついているかのように
 * ステージ演出を自動制御・アシストするインテリジェント・ライブ演出モジュール。
 *
 * 収録クラス:
 *   1. AutonomousLiveEncoreDirector
 *      - 歌唱継続時間・フレーズ終止のロングトーン安定度・その後の静寂移行を多段階ステートマシンで監視し、
 *        曲の歌い終わり（アウトロ解決）を誤爆ゼロで検知してスタジオ拍手SEやアンコール演出トリガーを発火。
 *      - サビ突入時の高音域（F0上昇）＋声量エネルギー急増を検知して「サビ自動ブースト（Auto Chorus Boost）」を滑らかに駆動。
 *
 *   2. LipSyncVisemeAndSnsAvatarAnimator
 *      - ボーカル信号の F1 (顎の開き: 250〜950Hz)・F2 (舌の前後と唇の丸め: 800〜2600Hz)・瞬時RMSから、
 *        日本語5母音ビゼーム (/a/, /i/, /u/, /e/, /o/) のブレンド重み、口の縦開き率、横幅率、頬のチーク紅潮度を
 *        60fps以上の滑らかさでリアルタイム算出。
 *
 *   3. SetlistTempoAndKeyTransitionAssistant
 *      - セットリストの連続する2曲間で、五度圏（Camelot Wheel）に基づく調性相性スコア (0〜100点)、
 *        自然につながる推奨移調キー（±半音）、およびBPMクロスフェード・ランプ時間を自動計算。
 */

#include <algorithm>
#include <array>
#include <cmath>

namespace gzzio
{
namespace stage_director
{

// =============================================================================
// 1. 自動アンコール拍手＆サビ演出ディレクター (AutonomousLiveEncoreDirector)
// =============================================================================
class AutonomousLiveEncoreDirector
{
public:
    enum class StagePhase : int
    {
        IdleWaiting      = 0, // 待機中・MCトーク中
        VerseBuilding    = 1, // Aメロ・Bメロ歌唱中
        ChorusClimax     = 2, // サビ最高潮（Chorus Boost 稼働）
        OutroCadenceHold = 3, // ロングトーン終止・余韻判定中
        EncoreApplause   = 4  // 歌い終わり拍手発火！
    };

    struct DirectorDecision
    {
        StagePhase currentPhase       = StagePhase::IdleWaiting;
        float      chorusBoostLevel01 = 0.0f; // サビ華やか演出の現在強度 (0..1)
        float      sungAccumulatedSec = 0.0f; // 現在の曲の累積歌唱秒数
        bool       triggerApplauseNow = false;// このフレームで拍手SEを発火すべきか
    };

    void reset() noexcept
    {
        phase              = StagePhase::IdleWaiting;
        accumulatedSongSec = 0.0f;
        quietAfterSongSec  = 0.0f;
        cooldownSec        = 0.0f;
        baselineF0Hz       = 220.0f;
        baselineEnergy     = 0.05f;
        chorusEnv01        = 0.0f;
    }

    // フレーム単位（例: dtSec = 0.010s〜0.020s）で歌唱統計を更新し、演出判定を返す
    DirectorDecision updateFrame(float dtSec,
                                 float vocalRmsLin,
                                 float vocalF0Hz,
                                 bool isVoiced,
                                 bool autoApplauseEnabled,
                                 bool autoChorusEnabled) noexcept
    {
        const float dt = std::clamp(dtSec, 0.0001f, 0.25f);
        DirectorDecision dec{};

        if (cooldownSec > 0.0f)
            cooldownSec = std::max(0.0f, cooldownSec - dt);

        const bool singingNow = isVoiced && (vocalRmsLin > 0.012f) && (vocalF0Hz > 65.0f);

        if (singingNow)
        {
            accumulatedSongSec += dt;
            quietAfterSongSec = 0.0f;

            // 曲前半の平均ピッチと平均声量をゆっくり学習し、サビでの相対的な盛り上がりを正確に捉える
            baselineF0Hz   += 0.008f * dt * (vocalF0Hz - baselineF0Hz);
            baselineEnergy += 0.012f * dt * (vocalRmsLin - baselineEnergy);

            const float pitchRiseRatio = vocalF0Hz / std::max(80.0f, baselineF0Hz);
            const float energyRatio    = vocalRmsLin / std::max(0.015f, baselineEnergy);

            // サビ判定: 平均より約3.5半音以上高い (ratio > 1.22) または声量が1.65倍以上で3秒以上歌っている場合
            const bool isChorusMoment = autoChorusEnabled
                                     && (accumulatedSongSec > 3.0f)
                                     && ((pitchRiseRatio > 1.20f && energyRatio > 1.15f) || energyRatio > 1.65f);

            const float targetChorus = isChorusMoment ? 1.0f : 0.0f;
            chorusEnv01 += (targetChorus > chorusEnv01 ? (dt * 1.8f) : (dt * 0.45f)) * (targetChorus - chorusEnv01);
            chorusEnv01  = std::clamp(chorusEnv01, 0.0f, 1.0f);

            phase = (chorusEnv01 > 0.45f) ? StagePhase::ChorusClimax : StagePhase::VerseBuilding;
        }
        else
        {
            chorusEnv01 = std::max(0.0f, chorusEnv01 - dt * 0.65f);
            if (accumulatedSongSec >= 8.0f)
            {
                quietAfterSongSec += dt;
                phase = StagePhase::OutroCadenceHold;

                // 8秒以上しっかり歌った後、1.35秒の静寂（歌い終わり余韻）が訪れたら自動で拍手を発火！
                if (autoApplauseEnabled && quietAfterSongSec >= 1.35f && cooldownSec <= 0.0f)
                {
                    dec.triggerApplauseNow = true;
                    phase                  = StagePhase::EncoreApplause;
                    accumulatedSongSec     = 0.0f;
                    quietAfterSongSec      = 0.0f;
                    cooldownSec            = 12.0f; // 連続誤発火防止クールダウン
                }
            }
            else
            {
                quietAfterSongSec += dt;
                if (quietAfterSongSec > 3.5f)
                {
                    accumulatedSongSec = 0.0f;
                    phase              = StagePhase::IdleWaiting;
                }
            }
        }

        dec.currentPhase       = phase;
        dec.chorusBoostLevel01 = chorusEnv01;
        dec.sungAccumulatedSec = accumulatedSongSec;
        return dec;
    }

private:
    StagePhase phase              = StagePhase::IdleWaiting;
    float      accumulatedSongSec = 0.0f;
    float      quietAfterSongSec  = 0.0f;
    float      cooldownSec        = 0.0f;
    float      baselineF0Hz       = 220.0f;
    float      baselineEnergy     = 0.05f;
    float      chorusEnv01        = 0.0f;
};

// =============================================================================
// 2. 5母音ビゼーム・リップシンク＆アバター表情アニメーター (LipSyncVisemeAndSnsAvatarAnimator)
// =============================================================================
class LipSyncVisemeAndSnsAvatarAnimator
{
public:
    struct AvatarVisemeFrame
    {
        float mouthOpenHeight01  = 0.0f; // 口の縦開き量 (0=閉口, 1=最大開口 /a/)
        float mouthWidthSpread01 = 0.5f; // 口の横広がり (0=すぼめ /u/, /o/ 〜 1=横引き /i/, /e/)
        float cheekBlushGlow01   = 0.0f; // 歌唱熱量に連動する頬の紅潮・オーラ輝度
        float headBounceOffset   = 0.0f; // リズムに連動する自然な立ち絵バウンス (-1..+1)
        std::array<float, 5> vowelWeights01{}; // [0]=/a/, [1]=/i/, [2]=/u/, [3]=/e/, [4]=/o/
        int   dominantVowelIndex = -1;   // -1=閉口, 0=あ, 1=い, 2=う, 3=え, 4=お
    };

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = std::max(8000.0, newSampleRate);
        reset();
    }

    void reset() noexcept
    {
        currentFrame = {};
        bouncePhase  = 0.0f;
    }

    // 推定フォルマント (F1, F2) と音量包絡からアバターの口形・表情フレームを更新
    AvatarVisemeFrame updateViseme(float dtSec,
                                   float rmsEnvelope,
                                   float f1Hz,
                                   float f2Hz,
                                   bool isVoiced) noexcept
    {
        const float dt = std::clamp(dtSec, 0.001f, 0.1f);
        const float activeGate = (rmsEnvelope > 0.008f) ? std::clamp((rmsEnvelope - 0.008f) / 0.18f, 0.0f, 1.0f) : 0.0f;

        // F1 (250〜900Hz) は顎の縦開きに直結
        const float targetOpen = isVoiced
            ? std::clamp((f1Hz - 240.0f) / 620.0f, 0.12f, 1.0f) * activeGate
            : (activeGate * 0.25f);

        // F2 (800〜2500Hz) は唇の横引き (/i/, /e/) と丸め (/u/, /o/) に直結
        const float targetWidth = isVoiced
            ? std::clamp((f2Hz - 850.0f) / 1550.0f, 0.10f, 0.95f)
            : 0.50f;

        const float openAlpha = (targetOpen > currentFrame.mouthOpenHeight01) ? std::min(1.0f, dt * 32.0f) : std::min(1.0f, dt * 16.0f);
        currentFrame.mouthOpenHeight01  += openAlpha * (targetOpen - currentFrame.mouthOpenHeight01);
        currentFrame.mouthWidthSpread01 += std::min(1.0f, dt * 20.0f) * (targetWidth - currentFrame.mouthWidthSpread01);
        currentFrame.cheekBlushGlow01   += std::min(1.0f, dt * 4.0f)  * (activeGate - currentFrame.cheekBlushGlow01);

        bouncePhase += dt * 11.5f;
        currentFrame.headBounceOffset = std::sin(bouncePhase) * currentFrame.mouthOpenHeight01 * 0.45f;

        // 日本語5母音プロトタイプ (F1, F2) とのガウス近接度から母音ブレンド重みを計算
        static constexpr std::array<std::pair<float, float>, 5> kPrototypes = {{
            { 780.0f, 1280.0f }, // 0: /a/ (あ)
            { 310.0f, 2280.0f }, // 1: /i/ (い)
            { 350.0f, 1320.0f }, // 2: /u/ (う)
            { 510.0f, 1920.0f }, // 3: /e/ (え)
            { 490.0f,  920.0f }  // 4: /o/ (お)
        }};

        if (activeGate < 0.05f)
        {
            currentFrame.vowelWeights01.fill(0.0f);
            currentFrame.dominantVowelIndex = -1;
        }
        else
        {
            float sumW = 1.0e-6f;
            int bestIdx = 0;
            float bestW = -1.0f;
            for (size_t v = 0; v < 5; ++v)
            {
                const float df1 = (f1Hz - kPrototypes[v].first)  / 190.0f;
                const float df2 = (f2Hz - kPrototypes[v].second) / 360.0f;
                const float w   = std::exp(-0.5f * (df1 * df1 + df2 * df2)) * activeGate;
                currentFrame.vowelWeights01[v] = w;
                sumW += w;
                if (w > bestW)
                {
                    bestW   = w;
                    bestIdx = static_cast<int>(v);
                }
            }
            for (float& w : currentFrame.vowelWeights01)
                w /= sumW;
            currentFrame.dominantVowelIndex = bestIdx;
        }

        return currentFrame;
    }

    inline const AvatarVisemeFrame& getCurrentVisemeFrame() const noexcept { return currentFrame; }

private:
    double sampleRate = 48000.0;
    AvatarVisemeFrame currentFrame{};
    float bouncePhase = 0.0f;
};

// =============================================================================
// 3. 五度圏 (Camelot Wheel) セットリスト調性＆BPMトランジション診断器
// =============================================================================
class SetlistTempoAndKeyTransitionAssistant
{
public:
    struct TransitionAdvice
    {
        float compatibilityScore100 = 100.0f; // 調性＆テンポの相性スコア (0〜100)
        int   circleOfFifthsDistance= 0;      // 五度圏上の距離 (0〜6)
        int   recommendedTransposeSt= 0;      // 次曲を最も滑らかに繋ぐ推奨キー変更 (-2..+2 半音)
        float recommendedCrossfadeSec = 2.5f; // 推奨曲間クロスフェード秒数
    };

    // pitchClass: 0=C, 1=C#, ..., 11=B
    static TransitionAdvice evaluateTransition(int currentKey0To11,
                                               bool currentIsMinor,
                                               int currentBpm,
                                               int nextKey0To11,
                                               bool nextIsMinor,
                                               int nextBpm) noexcept
    {
        TransitionAdvice adv{};

        // 平行調（短調は +3半音の長調と同じ五度圏ポジション）に正規化
        const int curMajorEquiv  = ((currentKey0To11 + (currentIsMinor ? 3 : 0)) % 12 + 12) % 12;
        const int nextMajorEquiv = ((nextKey0To11    + (nextIsMinor    ? 3 : 0)) % 12 + 12) % 12;

        // 五度圏インデックス (C=0, G=1, D=2, A=3, E=4, B=5, F#=6, Db=7, Ab=8, Eb=9, Bb=10, F=11)
        const int curCircle  = (curMajorEquiv  * 7) % 12;
        const int nextCircle = (nextMajorEquiv * 7) % 12;
        const int rawDiff    = std::abs(curCircle - nextCircle);
        const int fifthDist  = std::min(rawDiff, 12 - rawDiff);
        adv.circleOfFifthsDistance = fifthDist;

        // 推奨移調（±1 または ±2 半音で五度圏距離が 0〜1 になるなら提案）
        int bestShift = 0;
        int bestDist  = fifthDist;
        for (int shift : { -1, 1, -2, 2 })
        {
            const int shiftedMajor = ((nextMajorEquiv + shift) % 12 + 12) % 12;
            const int shiftedCirc  = (shiftedMajor * 7) % 12;
            const int d            = std::min(std::abs(curCircle - shiftedCirc), 12 - std::abs(curCircle - shiftedCirc));
            if (d < bestDist)
            {
                bestDist  = d;
                bestShift = shift;
            }
        }
        adv.recommendedTransposeSt = bestShift;

        // BPM差の評価（ハーフタイム・ダブルタイムも考慮）
        const float cBpm = std::clamp(static_cast<float>(currentBpm), 50.0f, 240.0f);
        const float nBpm = std::clamp(static_cast<float>(nextBpm),    50.0f, 240.0f);
        const float diffDirect = std::fabs(cBpm - nBpm);
        const float diffDouble = std::fabs(cBpm * 2.0f - nBpm);
        const float diffHalf   = std::fabs(cBpm * 0.5f - nBpm);
        const float minBpmDiff = std::min({ diffDirect, diffDouble, diffHalf });

        const float harmonicScore = 100.0f - static_cast<float>(fifthDist) * 11.5f;
        const float tempoPenalty  = std::min(35.0f, minBpmDiff * 0.55f);
        adv.compatibilityScore100 = std::clamp(harmonicScore - tempoPenalty, 25.0f, 100.0f);
        adv.recommendedCrossfadeSec = std::clamp(60.0f / cBpm * 4.0f, 1.5f, 5.0f);
        return adv;
    }
};

// =============================================================================
// 4. 統合ライブステージ＆VTuber演出スイート (LiveStageAndVtuberDirectorSuite)
// =============================================================================
class LiveStageAndVtuberDirectorSuite
{
public:
    void prepare(double newSampleRate) noexcept
    {
        encoreDirector.reset();
        visemeAnimator.prepare(newSampleRate);
    }

    void reset() noexcept
    {
        encoreDirector.reset();
        visemeAnimator.reset();
    }

    AutonomousLiveEncoreDirector&        getEncoreDirector() noexcept       { return encoreDirector; }
    const AutonomousLiveEncoreDirector&  getEncoreDirector() const noexcept { return encoreDirector; }
    LipSyncVisemeAndSnsAvatarAnimator&   getVisemeAnimator() noexcept       { return visemeAnimator; }
    const LipSyncVisemeAndSnsAvatarAnimator& getVisemeAnimator() const noexcept { return visemeAnimator; }

private:
    AutonomousLiveEncoreDirector      encoreDirector;
    LipSyncVisemeAndSnsAvatarAnimator visemeAnimator;
};

} // namespace stage_director
} // namespace gzzio

namespace vg
{
namespace stage_director = ::gzzio::stage_director;
} // namespace vg
