#pragma once
#include "PitchDetector.h"

namespace gz
{
// 音程の行き先だけを決める。波形加工やメモリ確保は行わない。
// 自然な補正は一つの音符の中で中心を追い、次の音符には古い中心を持ち越さない。
//
// v5.1.4「ラグ無し＆歌を上手く聴かせる知能ピッチ制御 (`processSkilled`)」:
//   従来のピッチ補正は「ノート間の移り変わり(ポルタメント)」と「ノート内のピッチふらつき制振」を
//   同じ1つの遅いローパスフィルタ(20〜180ms)に通していたため、速さを自然寄りにすると
//   (1) 160ms の中心追従フィルタ(center)の遅れ と (2) 補正値ローパスの遅れ が重なり、
//   歌い出しやピッチの揺れに対して 100〜200ms 遅れて後からピッチが動く「ラグ感・うねり」が出ていた。
//
//   `processSkilled` では以下の4つの音響法則で「遅延ゼロの吸い付き」と「人間らしい自然な歌い回し」を両立する:
//    ① ノート間レガート軌跡(smoothedNote)とノート内即時制振(directCorr)の完全分離:
//       別の音符へ移るときだけ `speed` に応じた自然なポルタメントで目標ノートを滑らかに渡し、
//       同一ノート内の不規則なピッチふらつき(素人の声の揺れ)は位相遅れゼロで瞬時に制振する。
//    ② 規則的ビブラート選択分離:
//       Ornament::Guard が規則的なビブラート(4.5〜7.5Hz)を検出したときだけ `center` 基準へ
//       シームレスに移行し、不規則なふらつきは止めつつ美しいビブラートはそのまま残す。
//    ③ 歌い出し子音マイクロプロソディ保護カーブ:
//       無声→有声の立ち上がり最初の16msだけ子音由来の微小ピッチ跳躍をなだらかに受け止め、
//       大きく外れた入り(±45セント超)は0msで即座にターゲットへ引き込む。
//    ④ ロングトーン漸進重心ロック＆語尾のフラット垂れ(息切れ落ち)自動支え:
//       同一ノートが180ms以上続くロングトーンや語尾では、息切れによるピッチの垂れ下がりを
//       先回りして支え、プロ歌手の伸びやかなロングトーンの安定感を作る。
class PitchCorrection
{
public:
    void reset() noexcept
    {
        center = heldNote = candidate = 0.0f;
        candidateTime = silentTime = 0.0f;
        smoothedNote = skilledCorr = phraseAge = noteAge = 0.0f;
        lastKey = lastScale = -1;
    }

    float target (float midi, float dt, int key, int scaleId, float speed, float amount) noexcept
    {
        if (! std::isfinite (midi) || midi <= 0.0f)
        {
            silentTime = std::fmin (silentTime + dt, 60.0f);
            if (silentTime > 0.030f) reset();
            return 0.0f;
        }
        silentTime = 0.0f;
        const float nearest = scale::snap (midi, key, scaleId);
        const bool hard = speed <= 8.0f;
        const float margin = hard ? 0.03f : 0.09f;
        if (center <= 0.0f || key != lastKey || scaleId != lastScale)
        {
            center = midi;
            heldNote = candidate = nearest;
            candidateTime = 0.0f;
        }
        else if (nearest != heldNote
                 && std::abs (midi - nearest) + margin < std::abs (midi - heldNote))
        {
            if (candidate != nearest) { candidate = nearest; candidateTime = 0.0f; }
            candidateTime = std::fmin (candidateTime + dt, 60.0f);
            if (candidateTime >= (hard ? 0.004f : 0.012f) || std::abs (midi - heldNote) > 2.0f)
            {
                heldNote = nearest;
                center = midi;
                candidateTime = 0.0f;
            }
        }
        else candidateTime = 0.0f;
        lastKey = key; lastScale = scaleId;
        const float alpha = 1.0f - std::exp (-dt / 0.160f);
        center += (midi - center) * alpha;
        const float centerWeight = std::clamp ((speed - 20.0f) / 50.0f, 0.0f, 1.0f);
        const float reference = midi + centerWeight * (center - midi);
        return std::clamp (heldNote - reference, -6.0f, 6.0f) * std::clamp (amount, 0.0f, 1.0f);
    }

    // 歌を上手く聴かせるゼロ遅延軌道エンジン:
    // ornKeep: こぶし・しゃくり保護係数 (1=通常補正, 0=しゃくり/こぶし保護中)
    // vibConf: 規則的ビブラート確信度 (0=非ビブラート/不規則揺れ, 1=規則的ビブラート中)
    float processSkilled (float midi, float dt, int key, int scaleId,
                          float speed, float amount, float ornKeep = 1.0f, float vibConf = 0.0f) noexcept
    {
        dt = std::max (1.0e-4f, dt);
        const float amt = std::clamp (amount, 0.0f, 1.0f);
        if (amt <= 0.0001f)
        {
            target (midi, dt, key, scaleId, speed, 0.0f);
            skilledCorr = 0.0f;
            return 0.0f;
        }
        if (! std::isfinite (midi) || midi <= 0.0f)
        {
            target (midi, dt, key, scaleId, speed, amt);
            skilledCorr *= std::exp (-dt / 0.010f);
            if (std::abs (skilledCorr) < 1.0e-5f) skilledCorr = 0.0f;
            return skilledCorr;
        }

        const float prevHeld  = heldNote;
        const float vibTarget = target (midi, dt, key, scaleId, speed, amt);
        const bool  hard      = (speed <= 8.0f) && (amt >= 0.90f);

        phraseAge = std::fmin (phraseAge + dt, 60.0f);
        if (prevHeld <= 0.0f || std::abs (heldNote - prevHeld) > 0.01f)
            noteAge = 0.0f;
        else
            noteAge = std::fmin (noteAge + dt, 60.0f);

        if (hard)
        {
            smoothedNote = heldNote;
            skilledCorr  = vibTarget * std::clamp (ornKeep, 0.0f, 1.0f);
            return skilledCorr;
        }

        // ① ノート間ポルタメント軌跡: 歌い出しは即時ロック(遅延ゼロ)、ノート乗り換え時だけ滑らかに渡す
        if (smoothedNote <= 0.0f || std::abs (heldNote - smoothedNote) > 7.0f)
        {
            smoothedNote = heldNote;
        }
        else
        {
            const float tauNoteSec = 0.0020f + (speed * speed / 10000.0f) * 0.095f;
            const float aNote = 1.0f - std::exp (-dt / tauNoteSec);
            smoothedNote += (heldNote - smoothedNote) * aNote;
        }

        // ② ノート内即時制振(directCorr) と ビブラート保持補正(centerCorr) の選択ブレンド
        const float centerWeight = std::clamp ((speed - 20.0f) / 50.0f, 0.0f, 1.0f);
        const float reference    = midi + centerWeight * (center - midi);
        const float directCorr   = std::clamp (smoothedNote - midi,      -6.0f, 6.0f) * amt;
        const float centerCorr   = std::clamp (smoothedNote - reference, -6.0f, 6.0f) * amt;

        // ロングトーン(180ms超)や語尾でビブラートでない時は中心ロックを強め、語尾のフラット垂れを支える
        const float speedLoose      = std::clamp ((speed - 45.0f) / 55.0f, 0.0f, 0.60f);
        const float longToneTighten = (noteAge > 0.18f && vibConf < 0.25f)
                                    ? std::clamp ((noteAge - 0.18f) / 0.22f, 0.0f, 1.0f) : 0.0f;
        const float useVibCenter    = std::clamp (std::max (vibConf, speedLoose * (1.0f - 0.80f * longToneTighten)), 0.0f, 1.0f);
        const float rawCorr         = directCorr + useVibCenter * (centerCorr - directCorr);

        // ③ 歌い出し最初の16msの子音マイクロプロソディ保護(大きく外れている時は即座に100%引き込む)
        const float initErr  = std::abs (heldNote - midi);
        const float onsetEnv = (phraseAge < 0.016f && initErr < 0.45f)
                             ? std::clamp (phraseAge / 0.016f, 0.45f, 1.0f) : 1.0f;

        const float wanted = rawCorr * onsetEnv * std::clamp (ornKeep, 0.0f, 1.0f);

        // ④ 超高速デ・ジッター平滑(1.0ms〜3.5ms): ノート内のふらつきに対して位相遅れを作らない
        const float tauFastSec = 0.0010f + (speed / 100.0f) * 0.0025f;
        const float aFast      = 1.0f - std::exp (-dt / tauFastSec);
        skilledCorr += (wanted - skilledCorr) * aFast;
        if (std::abs (wanted - skilledCorr) < 1.0e-6f) skilledCorr = wanted;
        return skilledCorr;
    }

    float held()        const noexcept { return heldNote; }
    float currentNote() const noexcept { return smoothedNote; }

private:
    float center = 0.0f, heldNote = 0.0f, candidate = 0.0f;
    float candidateTime = 0.0f, silentTime = 0.0f;
    float smoothedNote = 0.0f, skilledCorr = 0.0f, phraseAge = 0.0f, noteAge = 0.0f;
    int lastKey = -1, lastScale = -1;
};
}
