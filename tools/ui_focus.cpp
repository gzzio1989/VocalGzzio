#include "TestPaths.h"
// ui_focus.cpp — v3.1 §4「1つずつ」（画面案A の中央＝選んだモジュールの詳細）の検証
//
//  この画面は「席の取り合い」が本体なので、目で見るだけでは足りない。
//  ★実際、作っている途中で3回ぶつけた:
//    ① ツマミの席が説明の箱に乗った（下限を jmax で守らせたぶんが食い込んだ）
//    ② スイッチ帯が説明の箱に乗った（帯の高さを席の割り当てより後に決めていた）
//    ③ しあげ の数値が窓の下で切れた（合計が窓を越えた）
//  どれも「重なっていないか」「窓に収まっているか」を数えれば見つかる。
//
//  確かめること（8つの箱ぜんぶで）:
//   [1] 選んだ箱のツマミが出ている（空の箱を作らない）
//   [2] 見えている部品どうしが重なっていない
//   [3] 見えている部品が画面の外へはみ出していない
//   [4] 「入」「出」の固定カード（案A）が選べて、中身が出る
//   [5] 「しあげ」は箱の中に**出さない**（出のカードにあるので二重にしない）
//   [6] 「1つずつ」を切れば、元の画面に戻る
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
#include <vector>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static void collect (juce::Component& c, std::vector<juce::Component*>& out)
{
    for (auto* k : c.getChildren())
    {
        if (! k->isVisible()) continue;
        out.push_back (k);
        collect (*k, out);
    }
}

// ── 検査の環境を固定する ────────────────────────────────────────────
//  ★UIの設定（くわしい/かんたん・EQ/エフェクトのタブ・文字%）は、
//   利用者ごとの設定ファイルに**残ります**。前に走らせた検査やアプリが
//   書いた値を引きずると、同じ検査が日によって別の数を出します。
//   （実際、別の検査でタブを切り替えたら、次の検査でツマミが 33→45 個に増えた）
//   なので、検査のあいだだけ**決め打ちの設定**に置き換え、終わったら戻す。
struct PinnedPrefs
{
    juce::File f, bak;
    PinnedPrefs()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "VocalGzzio";
        o.filenameSuffix  = ".settings";
        o.folderName      = "VocalGzzio";
        o.storageFormat   = juce::PropertiesFile::storeAsXML;
        f   = gz::dataDirectory().getChildFile ("VocalGzzio.settings");
        bak = f.getSiblingFile (f.getFileName() + ".bak_test");
        f.getParentDirectory().createDirectory();
        if (f.existsAsFile()) { bak.deleteFile(); f.copyFileTo (bak); }
        f.replaceWithText (
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PROPERTIES>\n"
            "  <VALUE name=\"ui_advanced\" val=\"1\"/>\n"
            "  <VALUE name=\"ui_layout_revision\" val=\"2\"/>\n"
            "  <VALUE name=\"ui_overview\" val=\"0\"/>\n"
            "  <VALUE name=\"ui_focus\" val=\"0\"/>\n"
            "  <VALUE name=\"ui_theme\" val=\"0\"/>\n"
            "  <VALUE name=\"ui_tab\" val=\"0\"/>\n"
            "  <VALUE name=\"ui_font\" val=\"1.0\"/>\n"
            "  <VALUE name=\"ui_zoom\" val=\"1.0\"/>\n</PROPERTIES>\n");
    }
    ~PinnedPrefs()
    {
        f.deleteFile();
        if (bak.existsAsFile()) { bak.copyFileTo (f); bak.deleteFile(); }
    }
};

struct View
{
    std::vector<juce::Rectangle<int>> knobs;     // 回すツマミ（席の取り合いの主役）
    std::vector<juce::String>         labels;
    juce::Rectangle<int>              bounds;
    int overflow = 0;                            // 画面からはみ出した部品の数
    bool sizeSliders = false;                    // v3.1「ぜんたい」の大きさスライダー2本
    int  sizeSliderH = 0;                        // その高さ（設計書§4-0: 26px以上）
    bool emptyNote = false;                      // v3.1「この使いかたでは使いません」
};

static View shoot (bool focus, int module, int useMode = 0)
{
    PinnedPrefs casePrefs;
    VocalGzzioProcessor p;
    if (auto* prm = p.apvts.getParameter ("src_mode"))
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) useMode));
    //  「1つずつ」は保存しない決まりなので、状態に書いても戻らない。
    //  ★実際に**列のボタンを押して**切り替える（ユーザーと同じ道を通す）。
    p.apvts.state.setProperty ("ui_focus_mod", module, nullptr);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    View v;
    if (ed == nullptr) { std::printf ("  FAIL: エディタが作れない\n"); ++gFail; return v; }
    ed->setSize (ed->getWidth(), ed->getHeight());
    v.bounds = ed->getLocalBounds();

    if (focus)
    {
        std::vector<juce::Component*> pre;
        collect (*ed, pre);
        juce::Button* fb = nullptr;
        for (auto* c : pre)
            if (c->getComponentID() == "focusBtn")
                if (auto* b = dynamic_cast<juce::Button*> (c)) { fb = b; break; }
        if (fb == nullptr)
        { std::printf ("  FAIL: 「1つずつ」のボタンが見つからない\n"); ++gFail; return v; }
        fb->setToggleState (true, juce::sendNotificationSync);
    }

    std::vector<juce::Component*> all;
    collect (*ed, all);
    for (auto* c : all)
    {
        if (auto* s = dynamic_cast<juce::Slider*> (c))
            if (s->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag)
                v.knobs.push_back (c->getScreenBounds());
        if (auto* l = dynamic_cast<juce::Label*> (c))
            if (l->getText().isNotEmpty()) v.labels.push_back (l->getText());
        if (c->getComponentID() == "uiFontSize" || c->getComponentID() == "uiZoom")
        { v.sizeSliders = true; v.sizeSliderH = juce::jmax (v.sizeSliderH, c->getHeight()); }
        if (c->getComponentID() == "focusEmptyNote" && c->isVisible()) v.emptyNote = true;
        //  ★はみ出しは「画面のいちばん下より下に**はみ出した**か」で数える。
        //   スクロールする列(Viewport)の中身は外にあって正しいので、直接の子だけ見る。
        if (c->getParentComponent() == ed.get())
            if (c->getBottom() > ed->getHeight() || c->getRight() > ed->getWidth())
                ++v.overflow;
    }
    return v;
}

static bool has (const std::vector<juce::String>& v, const char* utf8)
{
    const auto t = juce::String::fromUTF8 (utf8);
    for (auto& s : v) if (s == t) return true;
    return false;
}

static int overlapCount (const std::vector<juce::Rectangle<int>>& r)
{
    int n = 0;
    for (size_t i = 0; i < r.size(); ++i)
        for (size_t j = i + 1; j < r.size(); ++j)
            if (r[i].intersects (r[j])) ++n;
    return n;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    PinnedPrefs pinnedPrefs;   // 検査のあいだだけ UI 設定を決め打ちにする

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_uifocus");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct R { juce::File a, b;
        ~R() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } rst { autosave, backup };
    autosave.deleteFile();

    std::printf ("「1つずつ」画面（選んだモジュールの詳細）の検証\n\n");

    const char* SHIAGE = "出力音量";  // 仕上げ音量
    const char* FXAMT  = "\xe3\x82\xa8\xe3\x83\x95\xe3\x82\xa7\xe3\x82\xaf\xe3\x83\x88\xe9\x87\x8f"; // エフェクト量
    const char* MICVOL = "入力音量";              // マイク音量
    const char* JIION  = "\xe3\x82\xb8\xe3\x83\xbc\xe9\x9f\xb3";                                          // ジー音

    for (int m = 0; m < gz::ModuleChain::Count; ++m)
    {
        const auto v = shoot (true, m);
        std::printf ("[%d] %s\n", m + 1, gz::ModuleChain::jpName (m));
        CHECK ((int) v.knobs.size() >= 1, "ツマミが出ている (%d 個)", (int) v.knobs.size());
        CHECK (overlapCount (v.knobs) == 0, "ツマミどうしが重なっていない");
        CHECK (v.overflow == 0, "画面からはみ出した部品が無い (%d 個)", v.overflow);
        //  ★「しあげ」は箱の中ではなく、列の「出」のカードにある（第23歩）。
        //   箱の画面に出ていないことも見ておく（同じ2本を二度出したくない）。
        CHECK (! has (v.labels, SHIAGE),
               "「しあげ」は箱の中に出さない（出のカードにある）");
        autosave.deleteFile();
    }

    std::printf ("\n[9] 入（マイクから）— 案A の固定カード\n");
    {
        const auto v = shoot (true, 8);
        CHECK (has (v.labels, MICVOL), "マイク音量 が出ている");
        CHECK (has (v.labels, JIION),  "ジー音 が出ている");
        CHECK (! has (v.labels, SHIAGE), "しあげ の物は出ていない");
        CHECK (overlapCount (v.knobs) == 0, "重なっていない");
        CHECK (v.overflow == 0, "はみ出していない (%d)", v.overflow);
        autosave.deleteFile();
    }

    std::printf ("\n[10] 出（しあげ）— 案A の固定カード\n");
    {
        const auto v = shoot (true, 9);
        CHECK (has (v.labels, SHIAGE), "仕上げ音量 が出ている");
        CHECK (has (v.labels, FXAMT),  "エフェクト量 が出ている");
        CHECK (! has (v.labels, MICVOL), "入 の物は出ていない");
        CHECK (overlapCount (v.knobs) == 0, "重なっていない");
        CHECK (v.overflow == 0, "はみ出していない (%d)", v.overflow);
        autosave.deleteFile();
    }

    std::printf ("\n[11] 右1/4「ぜんたい」（設計書§4）\n");
    {
        const auto on  = shoot (true,  0);
        const auto off = shoot (false, 0);
        CHECK (on.sizeSliders,
               "1つずつ: 文字/画面の大きさが常設されている（高さ %d px）", on.sizeSliderH);
        CHECK (on.sizeSliderH >= 26,
               "設計書§4-0 のとおり 26px 以上ある (%d px)", on.sizeSliderH);
        CHECK (! off.sizeSliders,
               "ふつうの画面では出さない（「文字・見た目」の吹き出しの中だけ）");
    }

    std::printf ("\n[12]「1つずつ」を切れば元の画面に戻る\n");
    {
        const auto off = shoot (false, 0);
        const auto on  = shoot (true,  0);
        std::printf ("  ツマミの数: ふつう %d 個 → 1つずつ %d 個\n",
                     (int) off.knobs.size(), (int) on.knobs.size());
        CHECK ((int) off.knobs.size() > (int) on.knobs.size() + 8,
               "ふつうの画面のほうがずっと多い (%d vs %d)",
               (int) off.knobs.size(), (int) on.knobs.size());
        CHECK (overlapCount (off.knobs) == 0, "ふつうの画面も重なっていない");
        CHECK (off.overflow == 0, "ふつうの画面もはみ出していない (%d)", off.overflow);
    }

    std::printf ("\n[13] 使いかたで中身が消える箱は、空白ではなく理由を出す\n");
    {
        //  しゃべり(2) は へんしん(ピッチ・ハモリ) を音の側で止めている。
        //  「1つずつ」でその箱を開くと、席が空になる。空白のままだと壊れて見える。
        const auto v = shoot (true, gz::ModuleChain::Henshin, 2);
        std::printf ("  しゃべり × へんしん のツマミ: %d 個\n", (int) v.knobs.size());
        CHECK ((int) v.knobs.size() == 0, "しゃべりでは へんしん のツマミが出ない");
        CHECK (v.emptyNote, "「この使いかたでは使いません」の席が用意されている");
        CHECK (v.overflow == 0, "はみ出していない (%d)", v.overflow);
        const auto u = shoot (true, gz::ModuleChain::Henshin, 0);
        CHECK ((int) u.knobs.size() > 0, "うたでは へんしん のツマミが出る (%d 個)",
               (int) u.knobs.size());
        CHECK (! u.emptyNote, "うたでは言い訳を出さない");
        autosave.deleteFile();
    }

    std::printf ("\n[14] 初期設定では総合画面から操作できる\n");
    {
        //  v4.2: 画面設定が未保存なら「総合画面」が既定。
        //  プロジェクト側に古い「全体表示」があっても初期値を上書きしない。
        pinnedPrefs.f.replaceWithText (
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PROPERTIES>\n"
            "  <VALUE name=\"ui_advanced\" val=\"1\"/>\n"
            "  <VALUE name=\"ui_theme\" val=\"0\"/>\n"
            "  <VALUE name=\"ui_font\" val=\"1.0\"/>\n"
            "  <VALUE name=\"ui_zoom\" val=\"1.0\"/>\n</PROPERTIES>\n");
        VocalGzzioProcessor p;
        p.apvts.state.setProperty ("ui_focus", false, nullptr);
        p.apvts.state.setProperty ("ui_focus_mod", 9, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        if (ed == nullptr) { std::printf ("  FAIL: エディタが作れない\n"); ++gFail; }
        else
        {
            ed->setSize (ed->getWidth(), ed->getHeight());
            std::vector<juce::Component*> all;
            collect (*ed, all);
            int knobs = 0; bool sizeSliders = false;
            for (auto* c : all)
            {
                if (auto* sl = dynamic_cast<juce::Slider*> (c))
                    if (sl->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag)
                        ++knobs;
                if (c->getComponentID() == "uiFontSize" || c->getComponentID() == "uiZoom")
                    sizeSliders = true;
            }
            std::printf ("  開いたときのツマミの数: %d 個\n", knobs);
            CHECK (knobs == 14, "総合画面の主要つまみ14個を表示している (%d 個)", knobs);
            CHECK (! sizeSliders, "文字・画面サイズは専用ボタンから開く");
        }
        autosave.deleteFile();
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
