#include "TestPaths.h"
// dsp_usemode.cpp — v3.1「使いかた4種」（設計書§3）の検証
//
//  v2.10.0 の「音の種類」は うた/アコギ/しゃべり の3つだった。
//  v3.1 で **声とギター**（弾き語り＝1本のマイクに声とギターが同時に入る）を足して4つにする。
//  ※名前を「弾き語り」にしなかったのは、ヘッダのシーン切替に同名のボタンが既にあるため。
//
//  確かめること:
//   [1] うた(0)の設定表が1つも変わっていない（既存の曲の音を守る「凍結」検査）
//   [2] 選択肢が4つあり、末尾に足されている（並べ替えていない＝古い曲がずれない）
//   [3] 弾き語りの「こもり」は 300Hz と 220Hz の**2点**きく
//   [4] 弾き語りはピッチ系を通さない → 申告遅延が 0 のまま
//   [5] 弾き語りは「ことば・艶」は生きている（アコギだけとの違い）
//   [6] 弾き語りの「サ行おさえ」は 3.5kHz の山も下げる（フレット/ピックの音）
//       …そして、うたでは 3.5kHz を下げない（今までどおり）
//   [7] ノイズ除去の効きが使いかたで変わる（しゃべり>うた>アコギだけ・声とギター）
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <vector>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

// ★ダイナミクス系を全部切る。切らないと EQ の差が圧縮で潰れて測れない
//  （dsp_srcmode.cpp で一度この誤 FAIL を出している）。
static void flatten (VocalGzzioProcessor& p)
{
    for (const char* id : { "comp1", "comp2", "ride_amt", "res_amt", "deess",
                            "seq_amount", "drive", "sustain", "cons_amt", "br_amt",
                            "ring", "hum_amt", "denoise", "makeup" })
        setP (p, id, 0.0f);
    setP (p, "mud", 0.0f);       // 既定は -3dB。基準を平らにしておく
    setP (p, "harsh", 0.0f);     // 既定は -1.5dB
    setP (p, "air", 0.0f);
    setP (p, "presence", 0.0f);
    setP (p, "seq_on", 0.0f);
    setP (p, "gate", -80.0f);
    setP (p, "dn_on", 0.0f);
    setP (p, "dn_relearn", 0.0f);
    setP (p, "revon", 0.0f); setP (p, "dly_on", 0.0f);
    setP (p, "prox_amt", 0.0f); setP (p, "doubler", 0.0f); setP (p, "width", 0.0f);
    setP (p, "crush_on", 0.0f);
}

// その周波数の正弦波を流して、出てきた大きさ(dB)を直交検波で測る
static float toneLevelDb (VocalGzzioProcessor& p, double hz, double amp = 0.25,
                          double seconds = 1.2)
{
    const double sr = 48000.0; const int block = 128;
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    const int total = (int) (sr * seconds);
    const int skip  = (int) (sr * 0.6);           // 立ち上がりは捨てる
    double ph = 0.0, sumI = 0.0, sumQ = 0.0; int cnt = 0;
    for (int done = 0; done < total; done += block)
    {
        auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
        for (int n = 0; n < block; ++n)
        {
            const float s = (float) (amp * std::sin (ph));
            ph += 2.0 * juce::MathConstants<double>::pi * hz / sr;
            if (ph > 2.0 * juce::MathConstants<double>::pi) ph -= 2.0 * juce::MathConstants<double>::pi;
            L[n] = s; R[n] = s;
        }
        p.processBlock (buf, midi);
        for (int n = 0; n < block; ++n)
        {
            if (done + n < skip) continue;
            const double t = 2.0 * juce::MathConstants<double>::pi * hz * (double) (done + n) / sr;
            const double y = buf.getReadPointer (0)[n];
            sumI += y * std::cos (t); sumQ += y * std::sin (t); ++cnt;
        }
    }
    if (cnt == 0) return -120.0f;
    const double mag = 2.0 * std::sqrt (sumI * sumI + sumQ * sumQ) / (double) cnt;
    return (float) (20.0 * std::log10 (mag + 1e-12));
}

// 「こもり」を -8dB にしたときの、その周波数での下がり方(dB, 正 = 下がった)
static float mudCutAt (int mode, double hz)
{
    float flat = 0.0f, cut = 0.0f;
    {   VocalGzzioProcessor p; flatten (p); setP (p, "src_mode", (float) mode);
        p.prepareToPlay (48000.0, 128); flat = toneLevelDb (p, hz); }
    {   VocalGzzioProcessor p; flatten (p); setP (p, "src_mode", (float) mode);
        setP (p, "mud", -8.0f);         // ツマミの範囲は -12..0 dB。マイナスが「削る」
        p.prepareToPlay (48000.0, 128); cut = toneLevelDb (p, hz); }
    return flat - cut;
}

// サ行おさえ(deess)を強くかけたときの、その周波数での下がり方(dB, 正 = 下がった)
//  ★測る音は「その帯域だけが大きい音」にする。ディエッサーのしきい値は
//   全体包絡の割合でも決まるので、目当ての帯域だけを鳴らさないと反応しない。
static float deessCutAt (int mode, double hz)
{
    float flat = 0.0f, cut = 0.0f;
    {   VocalGzzioProcessor p; flatten (p); setP (p, "src_mode", (float) mode);
        setP (p, "ds_on", 1.0f); setP (p, "deess", 0.0f);
        p.prepareToPlay (48000.0, 128); flat = toneLevelDb (p, hz, 0.35, 2.5); }
    {   VocalGzzioProcessor p; flatten (p); setP (p, "src_mode", (float) mode);
        setP (p, "ds_on", 1.0f); setP (p, "deess", 90.0f);
        p.prepareToPlay (48000.0, 128); cut = toneLevelDb (p, hz, 0.35, 2.5); }
    return flat - cut;
}

// ノイズだけを流して、除去がどれだけ削ったか(dB, 正 = 削れた)
//  ★LEARN で床を先に決める。自力の床づくりは 60 秒かかるので、
//   それを待つ検査にすると 4 つの使いかた × 2 回で何分もかかる
//   （最初これを 6 秒で測って「どの使いかたも 0.07dB」という嘘の FAIL を出した）。
static float noiseCutFor (int mode)
{
    const double sr = 44100.0; const int bs = 128;
    auto run = [&] (bool on) -> float
    {
        VocalGzzioProcessor p; flatten (p);
        setP (p, "src_mode", (float) mode);
        setP (p, "dn_on", on ? 1.0f : 0.0f);
        setP (p, "denoise", on ? 60.0f : 0.0f);
        p.prepareToPlay (sr, bs);
        juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer midi;
        juce::Random rng (12345);                       // 種を固定＝毎回同じノイズ
        auto fill = [&] { auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
            for (int n = 0; n < bs; ++n)
            { const float s = (rng.nextFloat() * 2.0f - 1.0f) * 0.004f;   // 約 -48dBFS
              L[n] = s; R[n] = s; } };

        // 1秒鳴らして包絡を落ち着かせてから LEARN（部屋の床を決める）
        for (int b = 0; b < (int) (sr * 1.0 / bs); ++b) { fill(); p.processBlock (buf, midi); }
        p.requestDenoiseLearn();
        while (p.isDenoiseLearning()) { fill(); p.processBlock (buf, midi); }

        double sum = 0.0; int cnt = 0;
        const int total = (int) (sr * 3.0);
        const int from  = (int) (sr * 1.5);             // ゲートが閉じきってから測る
        for (int done = 0; done < total; done += bs)
        {
            fill();
            p.processBlock (buf, midi);
            if (done >= from)
                for (int n = 0; n < bs; ++n)
                { const double y = buf.getReadPointer (0)[n]; sum += y * y; ++cnt; }
        }
        return (float) (20.0 * std::log10 (std::sqrt (sum / juce::jmax (1, cnt)) + 1e-12));
    };
    return run (false) - run (true);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // autosave.xml が次のブロックへ漏れると、比べているものが変わる
    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_usemode");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } restore { autosave, backup };
    autosave.deleteFile();

    std::printf ("使いかた4種（うた / アコギだけ / しゃべり / 声とギター）の検証\n\n");

    // ---------------------------------------------------------------- [1]
    std::printf ("[1] うた(0)の設定表が凍っているか（既存の曲の音を守る）\n");
    {
        const auto u = sourceProfile (0);
        CHECK (u.mudHz == 300.0f && u.mudQ == 1.0f,          "こもり 300Hz / Q1.0");
        CHECK (u.harshHz == 3200.0f && u.harshQ == 1.2f,     "かたさ 3200Hz / Q1.2");
        CHECK (u.presHz == 4200.0f && u.presQ == 0.9f,       "ぬけ   4200Hz / Q0.9");
        CHECK (u.airHz == 11000.0f,                          "きらめき 11kHz");
        CHECK (u.dsDetectHz == 5200.0f && u.dsShelfHz == 6500.0f, "サ行 5.2k を見て 6.5k を下げる");
        CHECK (u.resLoHz == 900.0 && u.resHiHz == 9000.0,    "なめらか 900Hz-9kHz");
        CHECK (u.voiceOnly && u.breathOk && u.spaceOk,       "声の処理は全部あり");
        // ★v3.1 で足した3つが、うたでは1つも効かないこと
        CHECK (u.pitchOk,                                    "ピッチ系あり");
        CHECK (u.dnScale == 1.0f,                            "ノイズ除去の効きは ×1.0（今までどおり）");
        CHECK (u.mudHz2 == 0.0f,                             "2点目のこもりは使わない");
        CHECK (u.dsDetectHz2 == 0.0f,                        "2点目のサ行は使わない");
    }

    // ---------------------------------------------------------------- [2]
    std::printf ("\n[2] 選択肢が4つ・末尾に足されているか（並べ替えていない）\n");
    {
        VocalGzzioProcessor p;
        auto* prm = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("src_mode"));
        CHECK (prm != nullptr, "src_mode は選択肢のパラメータ");
        if (prm != nullptr)
        {
            const auto& ch = prm->choices;
            CHECK (ch.size() == 4, "選択肢は4つ (%d)", ch.size());
            CHECK (ch[0] == juce::String::fromUTF8 ("\xe3\x81\x86\xe3\x81\x9f"), "0 = うた（動かしていない）");
            CHECK (ch[1].startsWith (juce::String::fromUTF8 ("\xe3\x82\xa2\xe3\x82\xb3\xe3\x82\xae")),
                   "1 = アコギ… （番号はそのまま。名前だけ「アコギだけ」）");
            CHECK (ch[2] == juce::String::fromUTF8 ("\xe3\x81\x97\xe3\x82\x83\xe3\x81\xb9\xe3\x82\x8a"),
                   "2 = しゃべり（動かしていない）");
            //  ★名前は「声とギター」。ヘッダのシーン切替の1つ目がすでに
            //   「弾き語り」で、あちらは画面の見せかたを変えるもの。取り違え防止。
            CHECK (ch[3] == juce::String::fromUTF8 ("\xe5\xa3\xb0\xe3\x81\xa8\xe3\x82\xae\xe3\x82\xbf\xe3\x83\xbc"),
                   "3 = 声とギター（末尾に追加。弾き語り用）");
        }
    }

    // ---------------------------------------------------------------- [3]
    std::printf ("\n[3]「声とギター」のこもりが 300Hz と 220Hz の2点きくか\n");
    {
        const float uta300 = mudCutAt (0, 300.0), uta220 = mudCutAt (0, 220.0);
        const float hik300 = mudCutAt (3, 300.0), hik220 = mudCutAt (3, 220.0);
        std::printf ("  うた      300Hz %.2f dB / 220Hz %.2f dB\n", uta300, uta220);
        std::printf ("  弾き語り  300Hz %.2f dB / 220Hz %.2f dB\n", hik300, hik220);
        CHECK (hik300 > 5.0f,  "弾き語りは 300Hz(声)を下げる (%.2f dB)", hik300);
        CHECK (hik220 > 5.0f,  "弾き語りは 220Hz(胴鳴り)も下げる (%.2f dB)", hik220);
        CHECK (hik220 > uta220 + 2.0f,
               "220Hz は うたより深く下がる (%.2f → %.2f dB)", uta220, hik220);
    }

    // ---------------------------------------------------------------- [4]
    std::printf ("\n[4]「声とギター」はピッチ系を通さない → 申告遅延が 0 か\n");
    {
        auto latencyFor = [] (int mode) -> int
        {
            VocalGzzioProcessor p; flatten (p);
            setP (p, "src_mode", (float) mode);
            setP (p, "at_on", 1.0f);          // ピッチ補正 ON
            setP (p, "session", 0.0f);
            setP (p, gz::ModuleChain::paramId (gz::ModuleChain::Henshin), 1.0f);
            p.prepareToPlay (48000.0, 128);
            juce::AudioBuffer<float> buf (2, 128); juce::MidiBuffer m;
            for (int i = 0; i < 20; ++i) { buf.clear(); p.processBlock (buf, m); }
            return p.getLatencySamples();
        };
        const int lUta = latencyFor (0), lHik = latencyFor (3), lGita = latencyFor (1);
        const int lSha = latencyFor (2);
        std::printf ("  申告遅延: うた %d / アコギだけ %d / しゃべり %d / 弾き語り %d サンプル\n",
                     lUta, lGita, lSha, lHik);
        CHECK (lUta > 0,     "うたはピッチ補正が通る＝遅延を申告する (%d)", lUta);
        CHECK (lGita == 0,   "アコギだけは通さない＝0 (%d)", lGita);
        CHECK (lHik  == 0,   "弾き語りも通さない＝0 (%d)", lHik);
        //  2026-08-20 相談で決定: しゃべりもピッチ系を音の側で止める。
        //  申告と実際が食い違うと DAW がトラックを前に引っ張るので、ここも見る。
        CHECK (lSha  == 0,   "しゃべりも通さない＝0（配信の口パクずれが減る） (%d)", lSha);
    }

    // ---------------------------------------------------------------- [5]
    std::printf ("\n[5]「声とギター」は ことば・艶 が生きているか（アコギだけとの違い）\n");
    {
        const auto g = sourceProfile (1), h = sourceProfile (3);
        CHECK (! g.voiceOnly && ! g.pitchOk, "アコギだけ: 声の処理もピッチ系も切る");
        CHECK (h.voiceOnly,                  "弾き語り: ことば・艶は使う（声はあるので）");
        CHECK (! h.pitchOk,                  "弾き語り: ピッチ系は切る（和音で誤動作するので）");
        CHECK (h.breathOk && h.spaceOk,      "弾き語り: 息・ひびきは使う");
        const auto sh = sourceProfile (2);
        CHECK (sh.voiceOnly,                 "しゃべり: ことばは使う（聞き取りやすさ優先）");
        CHECK (! sh.pitchOk,                 "しゃべり: ピッチ系は切る（語尾が階段状になるので）");
    }

    // ---------------------------------------------------------------- [6]
    std::printf ("\n[6]「声とギター」のサ行おさえが 3.5kHz(フレット)も下げるか\n");
    {
        const float uta35 = deessCutAt (0, 3500.0), hik35 = deessCutAt (3, 3500.0);
        const float uta62 = deessCutAt (0, 6500.0), hik62 = deessCutAt (3, 6500.0);
        std::printf ("  3.5kHz: うた %.2f dB / 弾き語り %.2f dB\n", uta35, hik35);
        std::printf ("  6.5kHz: うた %.2f dB / 弾き語り %.2f dB\n", uta62, hik62);
        CHECK (hik35 > uta35 + 1.5f,
               "弾き語りだけ 3.5kHz を下げる (うた %.2f → 弾き語り %.2f dB)", uta35, hik35);
        CHECK (uta35 < 1.5f,
               "うたは 3.5kHz を下げない＝今までどおり (%.2f dB)", uta35);
        CHECK (hik62 > 3.0f,
               "弾き語りでも 6.5kHz(サ行)はちゃんと下がる (%.2f dB)", hik62);
    }

    // ---------------------------------------------------------------- [7]
    std::printf ("\n[7] ノイズ除去の効きが使いかたで変わるか（設計書§3）\n");
    {
        const float uta   = noiseCutFor (0);
        const float gita  = noiseCutFor (1);
        const float shabe = noiseCutFor (2);
        const float hiki  = noiseCutFor (3);
        std::printf ("  同じ 60%% での削れ: うた %.2f / アコギだけ %.2f / しゃべり %.2f / 声とギター %.2f dB\n",
                     uta, gita, shabe, hiki);
        CHECK (shabe > uta + 0.5f,  "しゃべりは強め (うた %.2f → %.2f dB)", uta, shabe);
        CHECK (uta > gita + 0.5f,   "アコギだけはひかえめ (うた %.2f → %.2f dB)", uta, gita);
        CHECK (uta > hiki + 0.5f,   "声とギターもひかえめ (うた %.2f → %.2f dB)", uta, hiki);
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
