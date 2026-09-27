#pragma once
// =============================================================================
//  VocalGzzio v2.10.0 —「距離ならし」近接効果の自動補正
//
//  宅録でいちばん多い事故は「マイクとの距離が一定しない」ことです。
//  近づけば低音が持ち上がってモコモコになり(近接効果)、離れれば痩せて
//  部屋の響きが増える。歌っている本人は動いている自覚がありません。
//
//  ● 近接効果とは
//    単一指向性マイクは、音源が近いほど低域が持ち上がります。数cmの差でも
//    100Hz 付近で数dB動きます。だから「同じ声なのにフレーズごとに太さが違う」。
//
//  ● どうやって距離を推し量るか
//    距離そのものは測れません。そこで **低域と中域のエネルギー比** を見ます。
//      低域(〜200Hz) / 中域(500Hz〜3kHz)
//    近づくと低域だけが増えるので、この比が上がります。離れると下がります。
//    声の高さが変わっても中域と低域は一緒に動くので、比は距離に反応して
//    音程には反応しにくい ── これがこの指標を選んだ理由です。
//
//  ● 何を基準にするか
//    「正しい距離」は人と機材で違うので、固定の目標値は持ちません。
//    **その人のいつもの比**をとてもゆっくり(数十秒)学習し、そこからの
//    ずれだけを打ち消します。だから誰の環境でも「いつもの太さ」に寄ります。
//
//  ● なぜゼロ遅延なのか
//    使うのは一次のローシェルフと包絡追従だけで、先読みも FFT もありません。
//    係数は ArrayCoefficients 相当を自前で持ち、実行中にヒープを触りません。
//
//  JUCE に依存しません。tools/dsp_proximity.cpp から同じコードを直接テストします。
// =============================================================================

#include <cmath>
#include <algorithm>

namespace gz::prox
{

// ---- 一次ローシェルフ（係数は毎ブロック作り直すが、確保は起きない） ----------
// RBJ Audio EQ Cookbook の low-shelf(S=1) を一次で近似したもの。
// 中身は float 6個だけ。new は一切しない。
struct LowShelf
{
    float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;

    void reset() noexcept { x1 = y1 = 0.0f; }

    // gainDb: シェルフの持ち上げ/下げ量、fc: 曲がり始める周波数
    //
    //   H(s) = (s + G) / (s + 1)     … 直流で G 倍、高域で 1 倍（s は fc で正規化）
    //   双一次変換 s = (1 - z^-1) / (K (1 + z^-1)) 、K = tan(pi fc / sr) を入れて
    //       b0 = (1 + G K)/(1 + K)、b1 = (G K - 1)/(1 + K)、a1 = (K - 1)/(1 + K)
    //   直流で G、ナイキストで 1 になることは手計算で確認済み
    //   (tools/dsp_proximity.cpp でも数値で確かめている)。
    void set (double sr, float fc, float gainDb) noexcept
    {
        const float G = std::pow (10.0f, gainDb / 20.0f);          // 直流での倍率
        const float K = std::tan (3.14159265358979f * fc / (float) sr);
        const float d = 1.0f + K;
        if (d < 1.0e-9f) { b0 = 1.0f; b1 = a1 = 0.0f; return; }
        b0 = (1.0f + G * K) / d;
        b1 = (G * K - 1.0f) / d;
        a1 = (K - 1.0f) / d;
    }

    inline float tick (float x) noexcept
    {
        const float y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x; y1 = y;
        return y;
    }
};

// ---- 一次のバンド抽出（包絡を取るためだけの、粗くて軽いもの） ----------------
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (double sr, float fc) noexcept
    {
        a = std::exp (-2.0f * 3.14159265358979f * fc / (float) sr);
    }
    void  reset() noexcept { z = 0.0f; }
    inline float lp (float x) noexcept { z = x + a * (z - x); return z; }
};

class Evener
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;

        // 一次を重ねて分離を鋭くする。ここが緩いと低域と中域が互いに漏れ合い、
        // 実際は 3dB 動いているのに 1.5dB しか動いていないように見えて、
        // 補正が半分しか効かない（最初の実装がまさにそれだった）。
        lowA.setCutoff (sr, 200.0f);   lowB.setCutoff (sr, 200.0f);
        lowC.setCutoff (sr, 200.0f);
        midHpA.setCutoff (sr, 400.0f); midHpB.setCutoff (sr, 400.0f);
        midLpA.setCutoff (sr, 2500.0f); midLpB.setCutoff (sr, 2500.0f);

        // 包絡: 50ms。音節より遅く、フレーズより速い。
        env.setCutoff (sr, 1.0f / (2.0f * 3.14159265358979f * 0.050f));
        // 補正の追従: 250ms。口の動きの速さ。ここを速くすると
        // 「一音ごとに太さが変わる」不自然さが出るので、あえて鈍くしてある。
        smooth = std::exp (-1.0f / (float) (sr * 0.250));
        // 基準の学習: 40秒。歌い出しの数フレーズでは動かない。
        learn  = std::exp (-1.0f / (float) (sr * 40.0));

        reset();
        shelfL.set (sr, kShelfHz, 0.0f);
        shelfR.set (sr, kShelfHz, 0.0f);
    }

    void reset() noexcept
    {
        lowA.reset(); lowB.reset(); lowC.reset();
        midHpA.reset(); midHpB.reset(); midLpA.reset(); midLpB.reset(); env.reset();
        shelfL.reset(); shelfR.reset();
        lowEnv = midEnv = 1.0e-6f;
        ratioNow = 0.0f;
        refRatio = 0.0f;
        refReady = false;
        refSamples = 0;
        corrDb = 0.0f;
        gainNow = 1.0f;
    }

    // amount: 0..1（0 でこの処理は完全に素通し）
    void setAmount (float a) noexcept { amount = std::clamp (a, 0.0f, 1.0f); }

    // 画面表示用: いま何dB補正しているか（＋なら痩せを補い、−ならモコモコを削っている）
    float currentCorrectionDb() const noexcept { return corrDb; }
    // 画面表示用: 基準からのずれ（＋で近い／−で遠い）
    float currentOffsetDb()     const noexcept { return refReady ? (ratioNow - refRatio) : 0.0f; }
    bool  isReady()             const noexcept { return refReady; }

    // ステレオ（R は無くてもよい）。ゲート後ではなく**入力の直後**で呼ぶこと。
    void process (float* L, float* R, int num, bool voiced) noexcept
    {
        if (amount <= 0.0f)
        {
            // 完全に素通し。係数も動かさない（設定0%なら音は1ビットも変わらない）
            corrDb = 0.0f;
            return;
        }

        for (int n = 0; n < num; ++n)
        {
            const float x = R ? 0.5f * (L[n] + R[n]) : L[n];

            // 低域: 200Hz を3回通す（18dB/oct 相当）
            const float lo = lowC.lp (lowB.lp (lowA.lp (x)));
            // 中域: 400Hz より上を残し（2回）、2.5kHz より上を落とす（2回）
            const float h1 = x  - midHpA.lp (x);
            const float h2 = h1 - midHpB.lp (h1);
            const float md = midLpB.lp (midLpA.lp (h2));

            // 包絡（二乗の平均→あとで dB へ）
            lowEnv = lowEnv + (1.0f - env.a) * (lo * lo - lowEnv);
            midEnv = midEnv + (1.0f - env.a) * (md * md - midEnv);

            // 声が出ているときだけ学習・追従する。無音や息だけの区間で
            // 比を更新すると、部屋のノイズの比を「その人のいつも」と
            // 覚えてしまう（ゲートONでノイズ除去が効かなくなった v2.8.0 の
            // 失敗と同じ形なので、ここは最初から声の有無で門を作る）。
            const bool loud = (midEnv > kFloor) && voiced;

            if (loud)
            {
                const float r = 10.0f * std::log10 ((lowEnv + 1.0e-12f) / (midEnv + 1.0e-12f));
                ratioNow = r;

                if (! refReady)
                {
                    // 立ち上がり: 最初の数秒は素直に平均へ寄せる
                    refRatio += (r - refRatio) * 0.0004f;
                    if (++refSamples > (long) (sr * 3.0)) refReady = true;
                }
                else
                {
                    refRatio = refRatio + (1.0f - learn) * (r - refRatio);
                }

                // 基準からのずれを打ち消す向きへ。効き過ぎないよう ±6dB で頭打ち。
                const float want = std::clamp (-(r - refRatio) * amount, -6.0f, 6.0f);
                corrDb = corrDb + (1.0f - smooth) * (want - corrDb);
            }
            else
            {
                // 声が無い間は補正をゆっくり 0 へ戻す（無音区間で固まらせない）
                corrDb = corrDb + (1.0f - smooth) * (0.0f - corrDb);
            }
        }

        // 係数はブロックに1回だけ更新する。ヒープは触らない。
        shelfL.set (sr, kShelfHz, corrDb);
        if (R) shelfR.set (sr, kShelfHz, corrDb);

        // 距離が離れたぶんの「音量の痩せ」も、控えめに補う。
        // 低域補正の 1/3 だけ全体ゲインへ回す（掛け過ぎるとゲートと喧嘩する）。
        const float gTarget = std::pow (10.0f, (corrDb / 3.0f) / 20.0f);

        for (int n = 0; n < num; ++n)
        {
            gainNow += (gTarget - gainNow) * 0.001f;
            L[n] = shelfL.tick (L[n]) * gainNow;
            if (R) R[n] = shelfR.tick (R[n]) * gainNow;
        }
    }

private:
    static constexpr float kShelfHz = 220.0f;   // 近接効果が出る帯域の肩
    static constexpr float kFloor   = 1.0e-7f;  // これ以下は「声が無い」とみなす

    double sr = 48000.0;
    float  amount = 0.0f;

    OnePole lowA, lowB, lowC, midHpA, midHpB, midLpA, midLpB, env;
    LowShelf shelfL, shelfR;

    float lowEnv = 1.0e-6f, midEnv = 1.0e-6f;
    float ratioNow = 0.0f, refRatio = 0.0f;
    float corrDb = 0.0f, gainNow = 1.0f;
    float smooth = 0.0f, learn = 0.0f;
    bool  refReady = false;
    long  refSamples = 0;
};

} // namespace gz::prox
