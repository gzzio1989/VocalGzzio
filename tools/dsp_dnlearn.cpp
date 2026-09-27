#include "TestPaths.h"
// dsp_dnlearn.cpp — v2.10.0「ノイズ床の学習」を汚れたまま固定させない、の検証
//
//  背景（2026-08-05 の音質調査）:
//   ユーザー報告「シャリつく・痩せた・小さい・上げると潰れる」の正体は、
//   **声が入っている最中に学習してしまったノイズ床が、永久に固定されていた**こと。
//   実測: 歌の最中に学習 → 低域 -10.5dB / 空気 +2.7dB / 山谷 +4.0dB。
//   本当の無音で学習した場合は ±0.00dB（＝機能自体は正しい）。
//   さらに dnLearned を false へ戻すコードがどこにも無く、状態XMLに保存されるので
//   再起動しても版を上げても直らなかった。
//
//  ここで確かめること:
//   1. 本当の無音で学習 → 採用される（結果=1）。歌は削られない
//   2. 歌いながら学習   → 採用されない（結果=2 か 3）。床は前のまま
//   3. 学習を消せる     → clearDenoiseLearn() で自動追従に戻る
//   4. 採用しなかったとき、音が変わっていない（＝拒否は本当に無害）
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <memory>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

struct Rnd { unsigned int s = 4242u;
    float next() noexcept { s = s * 1664525u + 1013904223u;
                            return (float) ((int) (s >> 9) - 4194304) / 4194304.0f; } };

// 部屋のノイズ(-58dBFS 相当)＋ 指定があれば歌
struct Src
{
    Rnd r; double ph[8] {}; float pink[3] {}; double sr = 48000.0;
    float noise() noexcept
    {
        const float w = r.next();
        pink[0] = 0.99765f * pink[0] + w * 0.0990460f;
        pink[1] = 0.96300f * pink[1] + w * 0.2965164f;
        pink[2] = 0.57000f * pink[2] + w * 1.0526913f;
        return (pink[0] + pink[1] + pink[2] + w * 0.1848f) * 0.2f;
    }
    float next (bool singing) noexcept
    {
        double s = 0.0;
        if (singing)
        {
            for (int h = 0; h < 8; ++h)
            {
                s += std::sin (ph[h]) / (h + 1);
                ph[h] += 2.0 * juce::MathConstants<double>::pi * 196.0 * (h + 1) / sr;
                if (ph[h] > 2 * juce::MathConstants<double>::pi) ph[h] -= 2 * juce::MathConstants<double>::pi;
            }
            s = 0.20 * s / 1.7;
        }
        return (float) (s + noise() * 0.00126);
    }
};

// dur 秒ぶん流す。singing=true なら歌を混ぜる。out!=nullptr なら結果を貯める
static void run (VocalGzzioProcessor& p, Src& src, double dur, bool singing,
                 std::vector<float>* out = nullptr)
{
    const int block = 128;
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    const int total = (int) (48000.0 * dur);
    for (int done = 0; done < total; done += block)
    {
        auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
        for (int n = 0; n < block; ++n) { const float s = src.next (singing); L[n] = s; R[n] = s; }
        p.processBlock (buf, midi);
        if (out) for (int n = 0; n < block; ++n) out->push_back (buf.getReadPointer (0)[n]);
    }
}

static float rmsDb (const std::vector<float>& v, size_t from = 0)
{
    double a = 0.0; size_t n = 0;
    for (size_t i = from; i < v.size(); ++i) { a += (double) v[i] * v[i]; ++n; }
    return n ? (float) (10.0 * std::log10 (a / (double) n + 1e-30)) : -120.0f;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // ★このテストは VocalGzzioProcessor をそのまま作る。コンストラクタは
    //   autosave.xml を読むので、**前のテストで覚えた床がそのまま次に漏れる**。
    //   （最初に書いたときこれで [2] が誤って FAIL した。ベンチと同じ落とし穴。）
    //   走らせるあいだだけ退避し、終わったら必ず戻す。
    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_dnlearn");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore {
        juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); }
    } restore { autosave, backup };
    // ブロックごとに消す。markStateDirty() が走ると autosave.xml が生まれ、
    // 次に作った Processor がそれを読み直してしまうため。
    auto fresh = [&] { autosave.deleteFile(); };

    std::printf ("ノイズ床の学習ガードの検証\n\n");

    // ---- 1. 本当の無音で学習 → 採用される ----
    std::printf ("[1] 本当の無音で学習した（正しい使い方）\n");
    {
        fresh();
        auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage; Src src;
        setP (p, "dn_on", 1); setP (p, "denoise", 60);
        p.prepareToPlay (48000.0, 128);
        run (p, src, 0.5, false);            // 落ち着かせる
        p.requestDenoiseLearn();
        run (p, src, 2.0, false);            // 学習の1.5秒は無音のまま
        CHECK (p.getDenoiseLearnResult() == 1, "採用される (結果=%d)", p.getDenoiseLearnResult());
        CHECK (p.isDenoiseLearned(), "学習ずみになる");
        CHECK (p.getDenoiseLearnLevelDb() < -45.0f,
               "測った部屋の大きさが妥当 (%.1f dB)", p.getDenoiseLearnLevelDb());

        std::vector<float> sung; run (p, src, 2.0, true, &sung);
        CHECK (rmsDb (sung, 48000) > -30.0f,
               "そのあと歌っても削られない (%.1f dB)", rmsDb (sung, 48000));
    }
    std::printf ("\n");

    // ---- 2. 歌いながら学習 → 採用しない ----
    std::printf ("[2] 歌っている最中に学習した（事故）\n");
    {
        fresh();
        auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage; Src src;
        setP (p, "dn_on", 1); setP (p, "denoise", 60);
        p.prepareToPlay (48000.0, 128);
        run (p, src, 0.5, true);
        p.requestDenoiseLearn();
        run (p, src, 2.0, true);             // 学習の1.5秒に声が入っている
        const int res = p.getDenoiseLearnResult();
        CHECK (res == 2 || res == 3, "採用しない (結果=%d: 2=大きすぎ 3=静かでない)", res);
        CHECK (! p.isDenoiseLearned(), "学習ずみにならない（床は前のまま）");
        CHECK (p.getDenoiseLearnLevelDb() > -45.0f,
               "大きすぎると分かっている (%.1f dB)", p.getDenoiseLearnLevelDb());
    }
    std::printf ("\n");

    // ---- 3. 拒否されたとき、音は変わっていない ----
    std::printf ("[3] 採用しなかったとき、音が変わっていないか\n");
    {
        std::vector<float> a, b;
        {   fresh(); auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage; Src src;
            setP (p, "dn_on", 1); setP (p, "denoise", 60);
            p.prepareToPlay (48000.0, 128);
            run (p, src, 0.5, true);
            p.requestDenoiseLearn();          // 押す（歌の最中＝拒否される）
            run (p, src, 3.0, true, &a); }
        {   fresh(); auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage; Src src;
            setP (p, "dn_on", 1); setP (p, "denoise", 60);
            p.prepareToPlay (48000.0, 128);
            run (p, src, 0.5, true);
            run (p, src, 3.0, true, &b); }    // 押さない
        double maxd = 0.0;
        const size_t n = juce::jmin (a.size(), b.size());
        for (size_t i = 0; i < n; ++i) maxd = juce::jmax (maxd, (double) std::abs (a[i] - b[i]));
        CHECK (maxd < 1.0e-6, "押しても押さなくても同じ音 (最大差 %.3e)", maxd);
    }
    std::printf ("\n");

    // ---- 4. 学習を消して自動追従に戻せる ----
    std::printf ("[4] 覚えた床を消せる（これまで戻す道が無かった）\n");
    {
        fresh();
        auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage; Src src;
        setP (p, "dn_on", 1); setP (p, "denoise", 60);
        p.prepareToPlay (48000.0, 128);
        run (p, src, 0.5, false);
        p.requestDenoiseLearn();
        run (p, src, 2.0, false);
        CHECK (p.isDenoiseLearned(), "いったん学習ずみにする");

        p.clearDenoiseLearn();
        run (p, src, 0.5, false);             // 音声側が拾うまで1ブロック回す
        CHECK (! p.isDenoiseLearned(), "消せる（自動追従にもどる）");
        CHECK (p.getDenoiseLearnResult() == 0, "結果の表示もクリアされる");

        // 消したあと、もう一度学習できる
        p.requestDenoiseLearn();
        run (p, src, 2.0, false);
        CHECK (p.getDenoiseLearnResult() == 1, "消したあと、もう一度学習できる");
    }
    std::printf ("\n");

    // ---- 5. 保存・復元しても消えた状態が保たれる ----
    std::printf ("[5] 消した状態が保存・復元される\n");
    {
        fresh();
        auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage; Src src;
        setP (p, "dn_on", 1); setP (p, "denoise", 60);
        p.prepareToPlay (48000.0, 128);
        run (p, src, 0.5, false);
        p.requestDenoiseLearn(); run (p, src, 2.0, false);
        juce::MemoryBlock learnedState; p.getStateInformation (learnedState);

        p.clearDenoiseLearn(); run (p, src, 0.5, false);
        juce::MemoryBlock clearedState; p.getStateInformation (clearedState);

        fresh();
        auto qStorage = std::make_unique<VocalGzzioProcessor>(); auto& q = *qStorage;
        q.prepareToPlay (48000.0, 128);
        q.setStateInformation (clearedState.getData(), (int) clearedState.getSize());
        CHECK (! q.isDenoiseLearned(), "消した状態を読み直しても消えている");

        fresh();
        auto rStorage = std::make_unique<VocalGzzioProcessor>(); auto& r = *rStorage;
        r.prepareToPlay (48000.0, 128);
        r.setStateInformation (learnedState.getData(), (int) learnedState.getSize());
        CHECK (r.isDenoiseLearned(), "覚えた状態は今までどおり残る");
    }
    std::printf ("\n");

    // ---- 6. すでに汚れている設定を読んだら、捨てて自動追従に戻す ----
    //   v2.9.0 以前で汚れた床を保存してしまった人は、更新しても直らないままになる。
    //   読み込みの時点で見つけて捨てる。
    std::printf ("[6] 汚れた床が保存された設定を読んだとき\n");
    {
        fresh();
        // 手で「有り得ない大きさの床」を書いた状態XMLを作る
        auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage; p.prepareToPlay (48000.0, 128);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        CHECK (xml != nullptr, "状態XMLを取り出せる");
        if (xml != nullptr)
        {
            auto* d = xml->getChildByName ("DENOISE");
            if (d == nullptr) d = xml->createNewChildElement ("DENOISE");
            d->setAttribute ("learned", true);
            for (int b = 0; b < 4; ++b) d->setAttribute ("f" + juce::String (b), 0.05);  // -26 dBFS
            juce::MemoryBlock bad; juce::AudioProcessor::copyXmlToBinary (*xml, bad);

            fresh();
            auto qStorage = std::make_unique<VocalGzzioProcessor>(); auto& q = *qStorage; q.prepareToPlay (48000.0, 128);
            q.setStateInformation (bad.getData(), (int) bad.getSize());
            CHECK (! q.isDenoiseLearned(), "有り得ない大きさの床は読まずに捨てる");

            // まともな床は今までどおり読む
            for (int b = 0; b < 4; ++b) d->setAttribute ("f" + juce::String (b), 0.0006); // -64 dBFS
            juce::MemoryBlock ok; juce::AudioProcessor::copyXmlToBinary (*xml, ok);
            fresh();
            auto r2Storage = std::make_unique<VocalGzzioProcessor>(); auto& r2 = *r2Storage; r2.prepareToPlay (48000.0, 128);
            r2.setStateInformation (ok.getData(), (int) ok.getSize());
            CHECK (r2.isDenoiseLearned(), "まともな床は今までどおり読む");
        }
    }
    std::printf ("\n");

    std::printf (gFail ? "== %d 件 FAIL ==\n" : "== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
