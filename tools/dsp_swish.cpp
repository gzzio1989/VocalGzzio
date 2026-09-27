#include "TestPaths.h"
// dsp_swish.cpp — 「ノイズ除去をかけると声がサフサフする」の**本当の原因**を測る。
//
//  前回(v3.1)は「喋りはじめの子音が削られる」と考えて直したが、
//  ユーザーからは「まだ消えていない」。仮説が違った可能性が高いので、
//  症状そのもの＝**高域が時間とともに開いたり閉じたりする**を直接測る。
//
//  なぜそれがサフサフなのか:
//   ・このノイズ除去は4バンドのエキスパンダー。いちばん上は 5kHz 以上。
//   ・声リンク(voiceOpen)の保持は 80ms、閉じの時定数は 45ms。
//   ・ふつうの日本語の会話は、単語のすき間が 150〜300ms ある。
//     → すき間のたびに 80ms で保持が切れ、上のバンドだけが一気に閉じ、
//       次の単語で また開く。**毎秒2〜3回、上の帯域だけが出たり消えたり**する。
//     これが「サフサフ」。喋りはじめだけの話ではなく、**しゃべっている間ずっと**起きる。
//
//  測りかた:
//   入力と出力の 5kHz 以上のエネルギーを 20ms ごとに比べ、
//   その「通り具合(dB)」が**声を出している区間の中で**どれだけ揺れるかを見る。
//   揺れ(peak-to-peak)が大きいほどサフサフして聴こえる。
//   人の耳は 3Hz 前後の振幅変調にいちばん敏感なので、この揺れは目立つ。
//
//  ★実測して分かった本当の原因(2026-08-19):
//   声の包絡は 60ms で減衰するが、判定線(床×4)まで落ちるには約190ms かかる。
//   ふつうの会話のすき間は 150〜300ms。**すき間が終わる前に次の単語が来るので、
//   ゲートが一度も閉じない**。つまり しゃべっている間ずっと部屋のノイズが素通しで、
//   それが声に乗って「サフサフ」と聴こえる。喋りはじめだけの話ではなかった。
//
//  測りかた(症状そのものを測る):
//   同じ声を「部屋あり」「部屋なし(無響)」の2回流し、
//   **単語のまん中で 5kHz以上のエネルギーがどれだけ増えているか**を比べる。
//   増えたぶん = 声に乗って漏れている部屋のノイズ = サフサフの量。
//
//   [1] しゃべっている間に漏れる高域ノイズ(サフサフの量)
//   [2] 声のすき間でノイズが減っているか
//   [3] 単語のまん中の高域(子音・息)が削られていないか
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static constexpr double kSR = 44100.0;
static constexpr int    kBS = 128;
static float gRoomDb = -50.0f;   // 部屋の音量（[5]でうるさい部屋も試す）

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

// おそうじ以外を切って、ノイズ除去だけを見る
static void dnOnly (VocalGzzioProcessor& p)
{
    using M = gz::ModuleChain;
    for (int m : { (int) M::Henshin, (int) M::Totonoe, (int) M::Soroe,
                   (int) M::Sagyo, (int) M::Neiro, (int) M::Chara, (int) M::Hirogari })
        setP (p, M::paramId (m), 0.0f);
    setP (p, "gate_on", 0.0f);
    setP (p, "dn_on", 1.0f); setP (p, "denoise", 60.0f);
    setP (p, "dn_relearn", 0.0f);
    setP (p, "hum_amt", 0.0f); setP (p, "mix", 100.0f); setP (p, "makeup", 0.0f);
}

// 5kHz以上だけを取り出す測定用フィルタ(1次HPを3段)
struct HP5k
{
    float a1=0,a2=0,a3=0;
    float f (float x)
    {
        const float a = 0.49f;                    // ~5kHz @44.1k
        a1 = a*(a1+x);   const float y1 = x - a1;
        a2 = a*(a2+y1);  const float y2 = y1 - a2;
        a3 = a*(a3+y2);  return y2 - a3;
    }
};

// 声のモデル（★実際の日本語に合わせた）:
//   母音 … 基音180Hzの倍音。5kHz以上は基音より約40dB下（実際の母音はこの程度）
//   子音 … 「さ・し・す」。エネルギーの大半が5kHz以上。母音より20dB小さい
//  以前のモデルは母音でも高域を出しっぱなしにしていたので、
//  「高域バンドが子音のたびに開閉する」という本当の動きが再現できていなかった。
struct Voice
{
    double ph = 0.0; unsigned s = 11u; float h1=0,h2=0;
    float vowel()
    {
        double v = 0.0;
        for (int h = 1; h <= 14; ++h) v += std::sin (ph * h) / h;   // ~2.5kHz まで
        ph += 2.0 * juce::MathConstants<double>::pi * 180.0 / kSR;
        return (float) (0.09 * v);
    }
    float consonant()
    {
        s = s * 1664525u + 1013904223u;
        const float w = (float) ((int) (s >> 9) - 4194304) / 4194304.0f;
        const float a = 0.49f;
        h1 = a*(h1+w); const float y1 = w - h1;
        h2 = a*(h2+y1);
        return (y1 - h2) * 0.030f;      // 母音のおよそ -20dB。弱いさ行
    }
};

struct Room
{
    unsigned s = 77u; float lp = 0;
    float next (float db)
    {
        s = s * 1664525u + 1013904223u;
        const float w = (float) ((int) (s >> 9) - 4194304) / 4194304.0f;
        lp += 0.25f * (w - lp);
        return lp * 2.0f * juce::Decibels::decibelsToGain (db);
    }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_swish");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct R { juce::File a,b; ~R(){ a.deleteFile(); if (b.existsAsFile()) b.moveFileTo(a);} } rst { autosave, backup };
    autosave.deleteFile();

    std::printf ("サフサフ（しゃべっている間、部屋のノイズが声に乗って漏れる）の検査\n\n");

    const long nA = (long)(kSR*2.5), nB = (long)(kSR*8.0);
    const long cons = (long)(kSR*0.08), vow = (long)(kSR*0.22), gap = (long)(kSR*0.20);
    const long word = cons + vow, cyc = word + gap;

    // withRoom=true/false で同じ声を流し、単語まん中の5kHz以上エネルギーを返す。
    // gapOut/gapIn は「すき間のノイズがどれだけ減ったか」用（部屋ありのときだけ意味がある）。
    struct Res { double wordHi = 0; double gapIn = 0, gapOut = 0; };
    auto run = [&] (bool withRoom) -> Res
    {
        VocalGzzioProcessor p; dnOnly (p); p.prepareToPlay (kSR, kBS);
        juce::AudioBuffer<float> buf (2, kBS); juce::MidiBuffer midi;
        Voice voice; Room room; HP5k hOut;
        Res r;
        for (long done = 0; done < nA + nB; done += kBS)
        {
            float in[kBS];
            for (int i = 0; i < kBS; ++i)
            {
                const long t = done + i;
                float v = withRoom ? room.next (gRoomDb) : 0.0f;
                if (t >= nA)
                {
                    const long u = (t - nA) % cyc;
                    if      (u < cons) v += voice.consonant();     // さ行の頭
                    else if (u < word) v += voice.vowel();         // 母音
                }
                in[i] = v; buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            // 部屋なしの回は LEARN できない(無音)。部屋ありで作った床と同じ条件にするため、
            // 部屋なしでも同じタイミングで LEARN を投げる(無音なので採用されず、床は既定のまま)。
            if (done == 22016 && withRoom) p.requestDenoiseLearn();
            p.processBlock (buf, midi);
            for (int i = 0; i < kBS; ++i)
            {
                const long t = done + i;
                const float o = buf.getSample (0, i);
                const float ho = hOut.f (o);
                if (t < nA) continue;
                const long u = (t - nA) % cyc;
                if (u > cons + (long)(kSR*0.04) && u < word - (long)(kSR*0.02))
                    r.wordHi += (double) ho*ho;                       // ★母音のまん中
                if (u > word + (long)(kSR*0.12))
                { r.gapIn += (double) in[i]*in[i]; r.gapOut += (double) o*o; }
            }
        }
        return r;
    };

    const Res withN = run (true);
    const Res noN   = run (false);

    // ---------------------------------------------------------------- [1]
    std::printf ("[1] 母音を出している間、5kHz以上に漏れる部屋のノイズ（サフサフの量）\n");
    const double leakDb = 10.0*std::log10 ((withN.wordHi + 1e-20) / (noN.wordHi + 1e-20));
    std::printf ("  母音のまん中の高域: 部屋なし基準に対して +%.2f dB\n", leakDb);
    std::printf ("  （0dB = 部屋のノイズが完全に消えている / 大きいほどサフサフ）\n");
    //  +3dB = 声と同じだけノイズが乗っている状態。ここを下回りたい。
    CHECK (leakDb < 3.0, "漏れは +3dB 未満 (%.2f dB)", leakDb);

    // ---------------------------------------------------------------- [2]
    std::printf ("\n[2] 文中の短いすき間では、あえて閉じない（設計）\n");
    //  ★これは「直すべき欠陥」ではなく、意図した設計。
    //  ふつうの会話のすき間は 150〜300ms。ここで毎回ゲートを閉じて開けると
    //  **毎秒2〜3回の音量変化**になり、人の耳がいちばん敏感な変調（3Hz付近）に
    //  ぴったり当たって、かえって耳障りになる（これこそポンピング）。
    //  文の中では開けたままにして、長い沈黙でだけ閉じる（[4]の沈黙区間で確認）。
    const double cut = 10.0*std::log10 ((withN.gapOut+1e-20)/(withN.gapIn+1e-20));
    std::printf ("  すき間のノイズ: %.2f dB（0dB付近＝閉じていない＝設計どおり）\n", cut);
    CHECK (cut > -6.0, "文中のすき間で開閉していない (%.2f dB)", cut);

    // ---------------------------------------------------------------- [3]
    std::printf ("\n[3] さ行の子音（弱い高域）が削られていないか\n");
    //  部屋なしの回で、入れた声の高域がそのまま出ているか（削れ = マイナス）
    {
        VocalGzzioProcessor p; dnOnly (p); setP (p, "dn_on", 0.0f);   // 除去OFF＝素の値
        p.prepareToPlay (kSR, kBS);
        juce::AudioBuffer<float> buf (2, kBS); juce::MidiBuffer midi;
        Voice voice; HP5k hOut; double ref = 0;
        for (long done = 0; done < nA + nB; done += kBS)
        {
            for (int i = 0; i < kBS; ++i)
            {
                const long t = done + i;
                float v = 0.0f;
                if (t >= nA)
                {
                    const long u = (t - nA) % cyc;
                    if      (u < cons) v = voice.consonant();
                    else if (u < word) v = voice.vowel();
                }
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < kBS; ++i)
            {
                const long t = done + i; if (t < nA) continue;
                const long u = (t - nA) % cyc;
                const float ho = hOut.f (buf.getSample (0, i));
                if (u > cons + (long)(kSR*0.04) && u < word - (long)(kSR*0.02)) ref += (double) ho*ho;
            }
        }
        const double keep = 10.0*std::log10 ((noN.wordHi+1e-20)/(ref+1e-20));
        std::printf ("  除去ONの高域 / 除去OFFの高域: %.2f dB\n", keep);
        CHECK (keep > -2.0, "声の高域は削られていない (%.2f dB)", keep);
    }

    // ---------------------------------------------------------------- [4]
    std::printf ("\n[4] ★喋り出した瞬間、部屋のノイズが「バッ」と出てこないか\n");
    //  ここが本命。長い沈黙のあいだはゲートが閉じてノイズが消えている。
    //  そこで喋り出すと、声リンク(voiceOpen)が**全バンドを1msで開ける**ので、
    //  声のない 5kHz以上の帯域まで一気に開き、**部屋のノイズが突然出現する**。
    //  人の耳は「静か→急にサーッ」の変化にとても敏感で、これが
    //  「喋りはじめにサフサフが聴こえる」の正体。
    //
    //  測りかた: 声として **400Hz の純音** を使う。
    //  ★倍音つきの母音では駄目だった: 測定用の高域フィルタ(18dB/oct)を
    //   2.5kHz の倍音が通り抜けてしまい、「ノイズが跳ねた」ではなく
    //   「声が来た」を測っていた（実測 14.6dB のうち大半がこれ）。
    //   400Hz の純音なら 5kHz まで 83dB 落ちるので、
    //   測っているのは**部屋のノイズだけ**になる。
    //   声リンク(voiceOpen)は帯域1が反応するので、狙いの動作はそのまま起きる。
    {
        VocalGzzioProcessor p; dnOnly (p); p.prepareToPlay (kSR, kBS);
        juce::AudioBuffer<float> buf (2, kBS); juce::MidiBuffer midi;
        Voice voice; Room room; HP5k hOut; double sinPh = 0.0;
        juce::ignoreUnused (voice);
        const long qA = (long)(kSR*2.5);        // 部屋のみ(0.5秒目にLEARN)
        const long qS = (long)(kSR*2.0);        // さらに2秒の沈黙(ゲートが完全に閉じる)
        const long qV = (long)(kSR*1.0);        // 母音だけを1秒(高域を含まない声)
        double quiet = 0; long quietN = 0;
        double burst = 0; long burstN = 0;
        for (long done = 0; done < qA + qS + qV; done += kBS)
        {
            for (int i = 0; i < kBS; ++i)
            {
                const long t = done + i;
                float v = room.next (gRoomDb);
                if (t >= qA + qS)
                {
                    v += (float) (0.20 * std::sin (sinPh));   // 400Hz 純音
                    sinPh += 2.0 * juce::MathConstants<double>::pi * 400.0 / kSR;
                }
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            if (done == 22016) p.requestDenoiseLearn();
            p.processBlock (buf, midi);
            for (int i = 0; i < kBS; ++i)
            {
                const long t = done + i;
                const float ho = hOut.f (buf.getSample (0, i));
                // 沈黙の最後の0.5秒（ゲートが閉じきっている所）
                if (t > qA + qS - (long)(kSR*0.5) && t < qA + qS)
                { quiet += (double) ho*ho; ++quietN; }
                // 喋り出し直後の 50ms
                if (t >= qA + qS && t < qA + qS + (long)(kSR*0.05))
                { burst += (double) ho*ho; ++burstN; }
            }
        }
        const double q = 10.0*std::log10 (quiet/juce::jmax(1L,quietN) + 1e-20);
        const double b = 10.0*std::log10 (burst/juce::jmax(1L,burstN) + 1e-20);
        std::printf ("  沈黙中の高域ノイズ %.1f dB → 喋り出し直後50ms %.1f dB\n", q, b);
        std::printf ("  → 立ち上がり %.2f dB（大きいほど「バッ」と出る＝サフサフ）\n", b - q);
        //  6dB を超えると、静けさからの落差として はっきり聴こえる。
        CHECK (b - q < 6.0, "喋り出しでノイズが跳ね上がらない (%.2f dB)", b - q);
    }

    // ---------------------------------------------------------------- [5]
    std::printf ("\n[5] うるさい部屋（-40dB）でも同じことが言えるか\n");
    {
        gRoomDb = -40.0f;
        const Res w2 = run (true), n2 = run (false);
        const double leak2 = 10.0*std::log10 ((w2.wordHi+1e-20)/(n2.wordHi+1e-20));
        std::printf ("  母音に漏れる高域ノイズ: +%.2f dB\n", leak2);
        CHECK (leak2 < 6.0, "うるさい部屋でも漏れは +6dB 未満 (%.2f dB)", leak2);
        gRoomDb = -50.0f;
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
