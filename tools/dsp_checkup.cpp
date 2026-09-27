// dsp_checkup.cpp — v2.10.0「点検」の土台（#75 使われ方 / #76 名前ごとの設定）の検証
//
//  ここは音を作らない部品なので、プラグイン本体を巻き込まずに単体で確かめる。
//  確かめること:
//   1. 使われ方: 足した秒が貯まる／保存して読み直しても残る／起動回数が増える
//   2. 名前ごとの設定: 覚えた設定を同じ名前で取り出せる（往復して中身が同じ）
//   3. 同じ名前で覚え直したら上書きされる（増えていかない）
//   4. 知らない名前を引いたら nullptr（勝手に何か返さない）
//   5. 名前が空のときは覚えない（無名で上書きし合う事故を防ぐ）
#include "../Source/Checkup.h"
#include <cstdio>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    // 検証版は試験専用フォルダーへ保存する。同じ検査を再実行できるように、
    // その中に残った前回の試験データだけを退避して、終わったら戻す。
    auto uf = gz::checkup::Usage::file();
    auto rf = gz::checkup::Recall::file();
   #if VOCALGZZIO_TESTING
    const auto configuredPath = juce::SystemStats::getEnvironmentVariable (
        "VOCALGZZIO_TEST_DATA_DIR", {});
    const auto expectedDirectory = juce::File::getCurrentWorkingDirectory().getChildFile (
        configuredPath.isNotEmpty() ? configuredPath : juce::String (".vocalgzzio-test-data"));
    CHECK (uf.getParentDirectory() == expectedDirectory, "利用記録を試験用保存先へ隔離");
    CHECK (rf.getParentDirectory() == expectedDirectory, "名前ごとの設定を試験用保存先へ隔離");
    if (gFail != 0) return 1; // 隔離に失敗した場合、ファイルを書き換えない。
   #endif
    auto ub = uf.getSiblingFile ("usage.bak_test");
    auto rb = rf.getSiblingFile ("recall.bak_test");
    uf.getParentDirectory().createDirectory();
    if (uf.existsAsFile()) uf.moveFileTo (ub);
    if (rf.existsAsFile()) rf.moveFileTo (rb);

    std::printf ("点検の土台（使われ方・名前ごとの設定）の検証\n\n");

    // ---- 1. 使われ方 ----
    std::printf ("[1] 使われ方（手元に貯めるだけ・送信しない）\n");
    {
        gz::checkup::Usage u;
        u.load();
        const int firstLaunches = u.getLaunches();
        u.add ("prox", 12.5);
        u.add ("prox", 7.5);
        u.add ("session", 60.0);
        CHECK (std::abs (u.get ("prox") - 20.0) < 1e-9, "足した秒が貯まる (%.1f)", u.get ("prox"));
        u.save();

        gz::checkup::Usage u2;
        u2.load();
        CHECK (std::abs (u2.get ("prox") - 20.0) < 1e-6,
               "保存して読み直しても残る (%.1f)", u2.get ("prox"));
        CHECK (u2.getLaunches() == firstLaunches + 1,
               "起動回数が増える (%d → %d)", firstLaunches, u2.getLaunches());
        CHECK (u2.getFirstSeen().isNotEmpty(), "はじめて使った日が入る");
    }
    std::printf ("\n");

    // ---- 2〜5. 名前ごとの設定 ----
    std::printf ("[2] 名前ごとの設定\n");
    {
        juce::XmlElement state ("PARAMS");
        state.setAttribute ("prox_amt", 45.0);
        state.setAttribute ("hum_amt", 100.0);

        CHECK (gz::checkup::Recall::store (juce::String::fromUTF8 ("\xe3\x81\x86\xe3\x81\x9f\x31"), state),
               "覚えられる");

        auto got = gz::checkup::Recall::fetch (juce::String::fromUTF8 ("\xe3\x81\x86\xe3\x81\x9f\x31"));
        CHECK (got != nullptr, "同じ名前で取り出せる");
        if (got != nullptr)
        {
            CHECK (std::abs (got->getDoubleAttribute ("prox_amt") - 45.0) < 1e-9,
                   "中身が同じ prox_amt=%.1f", got->getDoubleAttribute ("prox_amt"));
            CHECK (std::abs (got->getDoubleAttribute ("hum_amt") - 100.0) < 1e-9,
                   "中身が同じ hum_amt=%.1f", got->getDoubleAttribute ("hum_amt"));
        }

        // 上書き（増えない）
        juce::XmlElement state2 ("PARAMS");
        state2.setAttribute ("prox_amt", 10.0);
        gz::checkup::Recall::store (juce::String::fromUTF8 ("\xe3\x81\x86\xe3\x81\x9f\x31"), state2);
        auto got2 = gz::checkup::Recall::fetch (juce::String::fromUTF8 ("\xe3\x81\x86\xe3\x81\x9f\x31"));
        CHECK (got2 != nullptr && std::abs (got2->getDoubleAttribute ("prox_amt") - 10.0) < 1e-9,
               "同じ名前で覚え直すと上書きされる");
        CHECK (gz::checkup::Recall::names().size() == 1,
               "名前が増えていかない (%d 件)", gz::checkup::Recall::names().size());

        // 2つめの名前
        gz::checkup::Recall::store (juce::String::fromUTF8 ("\xe3\x81\x86\xe3\x81\x9f\x32"), state);
        CHECK (gz::checkup::Recall::names().size() == 2,
               "別の名前は別に貯まる (%d 件)", gz::checkup::Recall::names().size());

        // 知らない名前・空の名前
        CHECK (gz::checkup::Recall::fetch ("nothing-here") == nullptr,
               "知らない名前は何も返さない");
        CHECK (! gz::checkup::Recall::store ("", state),
               "名前が空なら覚えない");
    }
    std::printf ("\n");

    // 後片付け（元のファイルを戻す）
    uf.deleteFile(); rf.deleteFile();
    if (ub.existsAsFile()) ub.moveFileTo (uf);
    if (rb.existsAsFile()) rb.moveFileTo (rf);

    std::printf (gFail ? "== %d 件 FAIL ==\n" : "== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
