#include "TestPaths.h"
// dsp_usememo.cpp — v3.1「使いかたごとにツマミの値を別に覚える」（設計書§3）の検証
//
//  確かめること:
//   [1] 使いかたを変えて戻すと、その使いかたの値が戻ってくる
//   [2] はじめて行く使いかたでは、いまの音のまま（勝手に飛ばない）
//   [3] 覚えない物（曲そのものの設定・互換スイッチ）は動かない
//   [4] 保存して開き直しても、使いかたごとの記録が残っている
//   [5] ★開き直した直後に上書きされない（いちばん危ない所）
//   [6] 同じ使いかたのまま何度呼んでも何も起きない
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}
static float getP (VocalGzzioProcessor& p, const char* id)
{
    return p.apvts.getRawParameterValue (id)->load();
}
// 使いかたを変える＝画面のコンボと同じ道すじ（パラメータを動かしてから記憶を回す）
static void pickUseMode (VocalGzzioProcessor& p, int mode)
{
    setP (p, "src_mode", (float) mode);
    p.applyUseModeMemoryIfChanged();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_usememo");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } rst { autosave, backup };
    autosave.deleteFile();

    std::printf ("使いかたごとにツマミの値を別に覚える、の検証\n\n");

    // ---------------------------------------------------------------- [1]
    std::printf ("[1] 使いかたを変えて戻すと、その使いかたの値が戻るか\n");
    {
        VocalGzzioProcessor p;
        p.applyUseModeMemoryIfChanged();          // 起動直後の基準取り（空振り）

        // うた(0) で、こもりを -9dB、ひびきを 40% にする
        setP (p, "src_mode", 0.0f); p.applyUseModeMemoryIfChanged();
        setP (p, "mud", -9.0f);
        setP (p, "revmix", 40.0f);

        pickUseMode (p, 2);                        // しゃべりへ
        setP (p, "mud", -2.0f);                    // しゃべりでは浅く
        setP (p, "revmix", 0.0f);
        CHECK (std::abs (getP (p, "mud") + 2.0f) < 0.05f, "しゃべり側で こもり=-2dB にした (%.2f)", getP (p, "mud"));

        pickUseMode (p, 0);                        // うたへ戻る
        CHECK (std::abs (getP (p, "mud") + 9.0f) < 0.05f,
               "うたへ戻ると こもり が -9dB に戻る (%.2f dB)", getP (p, "mud"));
        CHECK (std::abs (getP (p, "revmix") - 40.0f) < 0.5f,
               "うたへ戻ると ひびき が 40%% に戻る (%.1f %%)", getP (p, "revmix"));

        pickUseMode (p, 2);                        // もう一度しゃべりへ
        CHECK (std::abs (getP (p, "mud") + 2.0f) < 0.05f,
               "しゃべりへ行くと こもり が -2dB に戻る (%.2f dB)", getP (p, "mud"));
        CHECK (std::abs (getP (p, "revmix")) < 0.5f,
               "しゃべりへ行くと ひびき が 0%% に戻る (%.1f %%)", getP (p, "revmix"));
    }

    // ---------------------------------------------------------------- [2]
    std::printf ("\n[2] はじめて行く使いかたでは、いまの音のままか（勝手に飛ばない）\n");
    {
        VocalGzzioProcessor p;
        p.applyUseModeMemoryIfChanged();
        setP (p, "src_mode", 0.0f); p.applyUseModeMemoryIfChanged();
        setP (p, "mud", -7.5f);
        pickUseMode (p, 3);                        // 声とギターは初めて
        CHECK (std::abs (getP (p, "mud") + 7.5f) < 0.05f,
               "初めての使いかたでは、こもり はそのまま (%.2f dB)", getP (p, "mud"));
    }

    // ---------------------------------------------------------------- [3]
    std::printf ("\n[3] 覚えない物は動かないか（曲の設定・互換スイッチ）\n");
    {
        CHECK (VocalGzzioProcessor::useModeMemorySkips ("src_mode"),    "src_mode は覚えない");
        CHECK (VocalGzzioProcessor::useModeMemorySkips ("session"),     "session は覚えない");
        CHECK (VocalGzzioProcessor::useModeMemorySkips ("lift_legacy"), "lift_legacy は覚えない");
        CHECK (VocalGzzioProcessor::useModeMemorySkips ("bpm"),         "bpm は覚えない");
        CHECK (VocalGzzioProcessor::useModeMemorySkips ("refpitch"),    "refpitch は覚えない");
        CHECK (! VocalGzzioProcessor::useModeMemorySkips ("mud"),       "こもり は覚える");

        VocalGzzioProcessor p;
        p.applyUseModeMemoryIfChanged();
        setP (p, "src_mode", 0.0f); p.applyUseModeMemoryIfChanged();
        setP (p, "bpm", 100.0f);
        setP (p, "lift_legacy", 1.0f);
        pickUseMode (p, 2);
        setP (p, "bpm", 160.0f);
        setP (p, "lift_legacy", 0.0f);
        pickUseMode (p, 0);                        // うたへ戻る
        CHECK (std::abs (getP (p, "bpm") - 160.0f) < 0.5f,
               "使いかたを戻しても BPM は動かない (%.0f)", getP (p, "bpm"));
        CHECK (getP (p, "lift_legacy") < 0.5f,
               "使いかたを戻しても 互換スイッチ は動かない (%.0f)", getP (p, "lift_legacy"));
    }

    // ---------------------------------------------------------------- [4][5]
    std::printf ("\n[4] 保存して開き直しても記録が残るか／[5] 開いた直後に上書きされないか\n");
    {
        juce::MemoryBlock mb;
        {
            VocalGzzioProcessor p;
            p.applyUseModeMemoryIfChanged();
            setP (p, "src_mode", 0.0f); p.applyUseModeMemoryIfChanged();
            setP (p, "mud", -11.0f);          // うた = -11dB
            pickUseMode (p, 2);
            setP (p, "mud", -1.0f);           // しゃべり = -1dB
            pickUseMode (p, 0);               // うたで保存する（-11dB のはず）
            CHECK (std::abs (getP (p, "mud") + 11.0f) < 0.05f,
                   "保存前: うたで -11dB (%.2f)", getP (p, "mud"));
            p.getStateInformation (mb);
        }
        {
            VocalGzzioProcessor q;
            // ★開き直す前に、わざと別の値を入れておく。
            //  「読み込む前の値で、読み込んだばかりの記録を上書きする」不具合を
            //  ここで捕まえる（それが起きると、しゃべりの記録が -5dB になる）。
            setP (q, "src_mode", 2.0f);
            setP (q, "mud", -5.0f);
            q.applyUseModeMemoryIfChanged();

            q.setStateInformation (mb.getData(), (int) mb.getSize());
            q.applyUseModeMemoryIfChanged();          // 開き直した直後の1回（空振りするはず）

            CHECK (std::abs (getP (q, "mud") + 11.0f) < 0.05f,
                   "[5] 開き直した直後、うたの値のまま (%.2f dB)", getP (q, "mud"));

            pickUseMode (q, 2);
            CHECK (std::abs (getP (q, "mud") + 1.0f) < 0.05f,
                   "[4] しゃべりへ行くと、保存されていた -1dB が戻る (%.2f dB)", getP (q, "mud"));
            pickUseMode (q, 0);
            CHECK (std::abs (getP (q, "mud") + 11.0f) < 0.05f,
                   "[4] うたへ戻ると -11dB が戻る (%.2f dB)", getP (q, "mud"));
        }
    }

    // ---------------------------------------------------------------- [6]
    std::printf ("\n[6] 同じ使いかたのまま何度呼んでも何も起きないか\n");
    {
        VocalGzzioProcessor p;
        p.applyUseModeMemoryIfChanged();
        setP (p, "src_mode", 0.0f); p.applyUseModeMemoryIfChanged();
        setP (p, "mud", -6.0f);
        for (int i = 0; i < 50; ++i) p.applyUseModeMemoryIfChanged();
        CHECK (std::abs (getP (p, "mud") + 6.0f) < 0.05f,
               "50回呼んでも こもり は -6dB のまま (%.2f dB)", getP (p, "mud"));
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
