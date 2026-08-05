#pragma once
// =============================================================================
//  VocalGzzio v2.7.0 —「こぶし」しゃくり・こぶし保護
//
//  海外製のピッチ補正は「まっすぐ伸ばす歌」を前提に作られています。だから
//  J-POP のしゃくり上げ、演歌のこぶし、語尾を落とす表現を「音痴」と見なして
//  平らに潰してしまう。日本の歌い手がピッチ補正を強くかけられない最大の理由が
//  これです。**海外の大手が作る動機を持たない機能**なので、ここを直せば
//  「補正を強くかけても、自分の歌い回しが残る」状態が作れます。
//
//  ● 考え方: 音そのものではなく「音程の動き方」を見る
//    音高 F0 はすでに取れているので、その**時間微分（1秒あたり何半音動いたか）**
//    と**折り返しの回数**だけを見ます。音を触らないので当然ゼロ遅延です。
//
//      しゃくり … 歌い出しの直後に、下から目標音へ一気に駆け上がる
//                 （80〜200ms で 1〜4半音）→ 出だしの速さで見分ける
//      こぶし   … 短い上下動が1〜3回。振幅が大きく（0.7半音以上）、すぐ収まる
//                 → 折り返しの「振幅」と「間隔」で見分ける
//
//  ● ビブラートと間違えないための仕掛け
//    ビブラートも上下動なので、素朴に作るとこぶしと区別できません。違いは
//    **規則正しく続くかどうか**です。ビブラートは同じ間隔（4〜8Hz）の折り返しが
//    延々と続きます。こぶしは1〜3回で着地します。そこで「一定の間隔の折り返しが
//    4回以上続いたらビブラート」と判定し、そのあいだは保護しません
//    （ビブラートは既存の「速さ」ツマミ側で守られる作りになっています）。
//
//  ● 出口
//    process() は **補正をどれだけ残すか（0〜1）** を返します。
//    1 = そのまま補正 / 0 = この瞬間は補正しない。掛け算1つで効きます。
//
//  JUCE に依存しません。tools/dsp_ornament.cpp から同じコードを直接テストします。
// =============================================================================

#include <cmath>
#include <algorithm>

namespace gz::orn
{

class Guard
{
public:
    void prepare (double /*sampleRate*/) { reset(); }

    void reset() noexcept
    {
        pPrev = pExtremum = -1.0f;
        velSm = 0.0f; sinceOnset = 9.9f; sinceRev = 9.9f;
        protect = 0.0f; hold = 0.0f; vibRun = 0; kind = 0; voiced = false;
    }

    // amount 0..1（0 = 保護しない＝従来と完全に同じ）
    void setAmount (float a) noexcept { amount = std::min (1.0f, std::max (0.0f, a)); }
    float getAmount() const noexcept  { return amount; }

    // pitchSemi: MIDI番号相当の音高。無声のときは 0 以下を渡す。
    // dt: このブロックの長さ（秒）。
    // 戻り値: 補正を残す割合 0..1（1 = フル補正、0 = 補正なし）
    // v2.8.0: 1回の呼び出しが長すぎるときは、10ms 刻みに分けて回す。
    // 以前は dt を 50ms で頭打ちにしていたので、バッファを 4096 サンプルに
    // すると実際の 85ms を 50ms として数えてしまい、**同じ歌・同じ設定でも
    // バッファサイズで効きが変わる**（保護が1.7倍長く残る／ビブラート判定が
    // ずれる）状態だった。時間の数え方を実時間に合わせる。
    float process (float pitchSemi, float dt) noexcept
    {
        if (amount <= 0.0005f) { return 1.0f; }
        dt = std::max (1.0e-4f, dt);
        if (dt > kMaxStep)
        {
            const int   steps = std::min (64, (int) std::ceil (dt / kMaxStep));
            const float sub   = dt / (float) steps;
            float last = 1.0f;
            for (int i = 0; i < steps; ++i) last = step (pitchSemi, sub);
            return last;
        }
        return step (pitchSemi, dt);
    }

    float lastProtect() const noexcept { return protect; }   // 0..1（UI表示用）
    int   lastKind()    const noexcept { return kind; }       // 0=なし 1=しゃくり 2=こぶし

private:
    static constexpr float kMaxStep = 0.010f;   // 分割の刻み(秒)

    float step (float pitchSemi, float dt) noexcept
    {

        const bool nowVoiced = (pitchSemi > 0.0f);
        if (! nowVoiced)
        {
            // 無声（息継ぎ・子音）。次の歌い出しに備えて出だし計測を戻す。
            voiced = false; pPrev = -1.0f; velSm = 0.0f; sinceOnset = 9.9f;
            decay (dt);
            kind = 0;
            return 1.0f - protect * amount;
        }

        // --- 歌い出し / 音の乗り換えを検出して「出だしからの時間」を計り直す ---
        const bool jumped = voiced && pPrev > 0.0f && std::fabs (pitchSemi - pPrev) > kJumpSemi;
        if (! voiced || jumped)
        {
            sinceOnset = 0.0f; sinceRev = 0.0f;
            pExtremum = pitchSemi; velSm = 0.0f; vibRun = 0;
        }
        else sinceOnset += dt;
        sinceRev += dt;
        voiced = true;

        // --- 速さ（半音／秒）。生の微分は暴れるので 30ms でならす ---
        if (pPrev > 0.0f)
        {
            const float velRaw = (pitchSemi - pPrev) / dt;
            const float a = 1.0f - std::exp (-dt / 0.030f);
            velSm += a * (velRaw - velSm);
        }
        const float pNow = pitchSemi;
        const float velPrevSign = velSm;

        float want = 0.0f; int k = 0;

        // ビブラートが止まって少し経ったら「規則的だった」記憶を捨てる。
        // 捨てないと、ビブラートのあとに来たこぶしを守れなくなる。
        if (sinceRev > kVibForget) vibRun = 0;

        // ---- しゃくり: 出だし直後の駆け上がり／駆け下がり ----
        if (sinceOnset < kScoopWin && std::fabs (velSm) > kScoopVel)
        {
            want = 1.0f; k = 1;
        }

        // ---- 音から音への渡り(レガートのスライド・語尾の落とし) ----
        // 出だしに限らず、速く一方向へ動いている間は補正を引っ込める。
        // ビブラートも瞬間の速さは大きいので、**規則的な折り返しが続いている
        // 間は除外**する(vibRun)。この一行が無いとビブラートを渡りと誤認する。
        if (std::fabs (velSm) > kGlideVel && vibRun < kVibRunToLock)
        {
            want = 1.0f; if (k == 0) k = 1;
        }

        // ---- こぶし: 折り返しの振幅と間隔で見る ----
        // 折り返し = 速さの符号が変わった瞬間。細かすぎる反転は無視（40ms のデバウンス）。
        const bool reversed = (velPrevSign * velLast < 0.0f) && sinceRev > kRevDebounce;
        if (reversed)
        {
            const float ampl = std::fabs (pNow - pExtremum);   // 前の折り返しからの振れ幅
            const float half = sinceRev;                       // 折り返しの間隔（半周期）

            // ビブラートらしい折り返し（一定間隔・振幅ひかえめ）が続いているか数える
            const bool vibLike = (half > kVibHalfLo && half < kVibHalfHi && ampl < kVibAmpMax);
            vibRun = vibLike ? (vibRun + 1) : 0;

            // こぶしと認めるのは「大きく振れて、すぐ収まる」動きだけ。
            // ビブラートが続いている間は認めない（vibRun で判定）。
            if (ampl >= kKobuAmp && half <= kKobuHalfMax && vibRun < kVibRunToLock)
            {
                want = 1.0f; k = 2;
                hold = kKobuHold;      // 1回の折り返しで少し保護を維持する
            }
            pExtremum = pNow; sinceRev = 0.0f;
        }
        // 折り返しから離れても、山の高さは更新しておく（次の振幅を正しく測るため）
        if ((velSm > 0.0f && pNow > pExtremum) || (velSm < 0.0f && pNow < pExtremum))
            { /* 進行中: pExtremum は前の折り返し点のまま保持する */ }

        if (hold > 0.0f) { hold -= dt; want = std::max (want, 1.0f); if (k == 0) k = 2; }

        // --- 保護量: 素早く出て、ゆっくり戻る（戻り際に補正が急に効かない） ---
        const float aUp = 1.0f - std::exp (-dt / 0.020f);
        const float aDn = 1.0f - std::exp (-dt / 0.150f);
        protect += ((want > protect) ? aUp : aDn) * (want - protect);

        velLast = velSm;
        pPrev = pitchSemi;
        kind = (protect > 0.15f) ? (k != 0 ? k : kind) : 0;
        return 1.0f - protect * amount;
    }

    void decay (float dt) noexcept
    {
        const float aDn = 1.0f - std::exp (-dt / 0.150f);
        protect += aDn * (0.0f - protect);
        hold = std::max (0.0f, hold - dt);
    }

    // --- しゃくり ---
    static constexpr float kScoopWin  = 0.22f;   // 出だしから何秒を「出だし」とみなすか
    static constexpr float kScoopVel  = 4.0f;    // 半音/秒。1半音を0.25秒で駆け上がる速さ
    static constexpr float kJumpSemi  = 1.5f;    // これ以上飛んだら「別の音に乗り換えた」
    static constexpr float kGlideVel  = 8.0f;    // 半音/秒。これ以上速い一方向の動きは「渡り」
    // --- こぶし ---
    static constexpr float kKobuAmp     = 0.70f; // 折り返しの振幅（半音）これ以上
    static constexpr float kKobuHalfMax = 0.28f; // 折り返しの間隔（秒）これ以下
    static constexpr float kKobuHold    = 0.18f; // 1回見つけたら維持する時間
    static constexpr float kRevDebounce = 0.040f;
    // --- ビブラート判定（こぶしと切り分けるため） ---
    static constexpr float kVibHalfLo   = 0.055f; // 半周期 55ms〜（＝9Hz以下）
    static constexpr float kVibHalfHi   = 0.150f; // 〜150ms（＝3.3Hz以上）
    static constexpr float kVibAmpMax   = 1.60f;  // この範囲の振幅なら規則性だけで判断
    static constexpr int   kVibRunToLock = 3;     // 規則的な折り返しが3回続いたらビブラート
    static constexpr float kVibForget    = 0.30f; // 折り返しが途絶えたら記憶を捨てる(秒)

    float amount = 0.0f;
    float pPrev = -1.0f, pExtremum = -1.0f;
    float velSm = 0.0f, velLast = 0.0f;
    float sinceOnset = 9.9f, sinceRev = 9.9f;
    float protect = 0.0f, hold = 0.0f;
    int   vibRun = 0, kind = 0;
    bool  voiced = false;
};

} // namespace gz::orn
