#include "TestPaths.h"
// ui_usemode.cpp — v3.1「使いかた4種」の画面側（設計書§3）の検証
//
//  使いかたで **そもそも音が通らない** ツマミを画面から出さない、という作り。
//  ここで確かめるのは3つ:
//   [1] 使いかたごとに、出るツマミ／出ないツマミが表のとおりか
//   [2] 出さなくなった席が**詰まる**か（穴が開いたまま＝壊れて見える）
//   [3] 「ぜんぶ表示」を入れれば、ぜんぶ出るか
//
//  ★エディタは使いかたごとに作り直す。パラメータを後から動かすと
//   ComboBoxAttachment の反映がメッセージループ待ちになり、検査が
//   「まだ変わっていない画面」を見て通ってしまう（＝嘘のPASS）。
//   作り直す形なら、プロジェクトを開き直したときの実際の道すじと同じになる。
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
#include <vector>
#include <string>

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

// 見えている Label の文字を全部あつめる（ツマミの名札はここに出る）
static std::vector<juce::String> visibleLabels (juce::Component& root)
{
    std::vector<juce::Component*> all;
    collect (root, all);
    std::vector<juce::String> out;
    for (auto* c : all)
        if (auto* l = dynamic_cast<juce::Label*> (c))
            if (l->getText().isNotEmpty())
                out.push_back (l->getText());
    return out;
}

static bool has (const std::vector<juce::String>& v, const char* utf8)
{
    const auto t = juce::String::fromUTF8 (utf8);
    for (auto& s : v) if (s == t) return true;
    return false;
}

// 見えているツマミ（回すやつ）の枠を集める
static std::vector<juce::Rectangle<int>> visibleKnobBoxes (juce::Component& root)
{
    std::vector<juce::Component*> all;
    collect (root, all);
    std::vector<juce::Rectangle<int>> out;
    for (auto* c : all)
        if (auto* s = dynamic_cast<juce::Slider*> (c))
            if (s->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag
                && s->getWidth() > 0 && s->getHeight() > 0)
                out.push_back (c->getScreenBounds());
    return out;
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

struct Shot
{
    std::vector<juce::String>          labels;
    std::vector<juce::Rectangle<int>>  knobs;
};

// 使いかた mode / ぜんぶ表示 showAll でエディタを作り、見えているものを写し取る
//  fxTab=true にすると「エフェクト」タブを**実際に押して**から写す。
//  へんしん（ピッチ・フォルマント・ユニゾン）はそちら側にあるので、
//  既定の EQ タブのままだと、どの使いかたでも見えない＝
//  「出ていないこと」の検査がいつでもPASSしてしまう（実際そうなった）。
static Shot shoot (int mode, bool showAll, bool fxTab = false)
{
    VocalGzzioProcessor p;
    if (auto* prm = p.apvts.getParameter ("src_mode"))
        prm->setValueNotifyingHost (p.apvts.getParameterRange ("src_mode").convertTo0to1 ((float) mode));
    p.apvts.state.setProperty ("ui_show_all", showAll, nullptr);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    if (ed == nullptr) { std::printf ("  FAIL: エディタが作れない\n"); ++gFail; return {}; }
    ed->setSize (ed->getWidth(), ed->getHeight());   // resized() を確実に走らせる

    //  ★タブは**毎回はっきり押す**。設定ファイル任せにすると、前に何が
    //   書かれていたかで見えるツマミが変わる。
    {
        std::vector<juce::Component*> pre;
        collect (*ed, pre);
        juce::Button* tb = nullptr;
        const juce::String want = fxTab ? "tabFx" : "tabEq";
        for (auto* c : pre)
            if (c->getComponentID() == want)
                if (auto* b = dynamic_cast<juce::Button*> (c)) { tb = b; break; }
        if (tb == nullptr) { std::printf ("  FAIL: タブ(%s)が見つからない\n", want.toRawUTF8()); ++gFail; }
        else
        {
            //  ★triggerClick() はメッセージを積むだけ。検査にはメッセージループが
            //   無いので何も起きない（実際それで空振りした）。onClick を直接呼ぶ。
            if (tb->onClick) tb->onClick();
            ed->setSize (ed->getWidth(), ed->getHeight());
        }
    }

    Shot s;
    s.labels = visibleLabels (*ed);
    s.knobs  = visibleKnobBoxes (*ed);
    return s;
}

// 同じ行に並んだツマミどうしが重なっていないか（席詰めの失敗を見つける）
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

    // autosave が前の実行から漏れると、見ている画面が変わる
    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_uimode");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } restore { autosave, backup };
    autosave.deleteFile();

    std::printf ("使いかた4種の画面（出すツマミ・出さないツマミ）の検証\n\n");

    const char* KOTOBA = "\xe3\x81\x93\xe3\x81\xa8\xe3\x81\xb0";                 // ことば
    const char* TSUYA  = "\xe8\x89\xb6";                                        // 艶
    // この検査は詳細画面を開くため、詳細用の短い名称で確認する。
    const char* HIBIKI = "ひびき";
    const char* YAMABI = "やまびこ";
    const char* HEYA = "部屋の広さ";
    const char* PICK   = "\xe3\x83\x94\xe3\x83\x83\xe3\x82\xaf\xe3\x81\x8a\xe3\x81\x95\xe3\x81\x88";       // ピックおさえ
    static const char* PITCH  = "\xe3\x83\x94\xe3\x83\x83\xe3\x83\x81";   // ピッチ（ボイス変換）
    static const char* UNISON = "\xe3\x83\xa6\xe3\x83\x8b\xe3\x82\xbe\xe3\x83\xb3";   // ユニゾン（ハモリ量）

    // ---------------------------------------------------------------- [1]
    std::printf ("[1] うた(0) — 何も減らない（今までどおり）\n");
    const auto uta = shoot (0, false);
    CHECK (has (uta.labels, KOTOBA), "ことば が出ている");
    CHECK (has (uta.labels, TSUYA),  "艶 が出ている");
    CHECK (has (uta.labels, HIBIKI), "ひびき が出ている");
    CHECK (has (uta.labels, HEYA),   "部屋の広さ が出ている");
    CHECK (! has (uta.labels, PICK), "ピックおさえ を出していない（うたでは通らない）");
    CHECK (! uta.knobs.empty(),      "ツマミが並んでいる (%d 個)", (int) uta.knobs.size());
    CHECK (overlapCount (uta.knobs) == 0, "ツマミどうしが重なっていない");

    // ---------------------------------------------------------------- [2]
    std::printf ("\n[2] アコギだけ(1) — 声のためのツマミを出さない\n");
    const auto gita = shoot (1, false);
    CHECK (! has (gita.labels, KOTOBA), "ことば を出していない（声用なので通らない）");
    CHECK (! has (gita.labels, TSUYA),  "艶 を出していない");
    CHECK (has (gita.labels, HIBIKI),   "ひびき は出ている（アコギでも使う）");
    CHECK (has (gita.labels, PICK),     "ピックおさえ が出ている（この使いかたにだけある）");
    //  ことば・艶 が消えて(-2)、ピックおさえ が出る(+1) ＝ 差し引き -1
    CHECK ((int) gita.knobs.size() == (int) uta.knobs.size() - 1,
           "うたより 1 個だけ少ない（-2して+1）(%d → %d)", (int) uta.knobs.size(), (int) gita.knobs.size());
    CHECK (overlapCount (gita.knobs) == 0, "席を詰めても重なっていない");

    // ---------------------------------------------------------------- [3]
    std::printf ("\n[3] しゃべり(2) — 残響は利用者が入／切できる\n");
    const auto shabe = shoot (2, false);
    CHECK (has (shabe.labels, KOTOBA),   "ことば は出ている（聞き取りやすさが最優先）");
    CHECK (has (shabe.labels, HIBIKI), "残響の量を選べる");
    CHECK (has (shabe.labels, HEYA),   "残響の長さを選べる");
    CHECK (has (shabe.labels, YAMABI), "やまびこを選べる");
    CHECK (! has (shabe.labels, PICK),   "ピックおさえ を出していない");
    //  2026-08-20 相談で決定: しゃべりは へんしん・ハモリ を音の側で止めた。
    //  止めた物は画面にも出さない（押せるのに効かない物を作らない）。
    //  ★へんしんは「エフェクト」タブの中なので、そちらを押してから見る。
    //   ★そして「うたでは出ている」ことを**必ず一緒に**見る。
    //    出ない物を数えるだけの検査は、名前を書き間違えても、
    //    そもそも見えない場所を見ていても、いつでもPASSする（2回ともやった）。
    const auto shabeFx = shoot (2, false, true);
    const auto utaFx   = shoot (0, false, true);
    CHECK (has (utaFx.labels, PITCH),      "うたでは ピッチ が出ている（検査が正しい場所を見ている証拠）");
    CHECK (has (utaFx.labels, UNISON),     "うたでは ユニゾン が出ている");
    CHECK (! has (shabeFx.labels, PITCH),  "しゃべりでは ピッチ を出していない（ピッチ系は通していない）");
    CHECK (! has (shabeFx.labels, UNISON), "しゃべりでは ユニゾン を出していない（ハモリも止めた）");
    CHECK (overlapCount (shabe.knobs) == 0, "席を詰めても重なっていない");

    // ---------------------------------------------------------------- [4]
    std::printf ("\n[4] 声とギター(3) — 声のツマミは残る（アコギだけとの違い）\n");
    const auto hiki = shoot (3, false);
    CHECK (has (hiki.labels, KOTOBA), "ことば が出ている（声はあるので）");
    CHECK (has (hiki.labels, TSUYA),  "艶 が出ている");
    CHECK (has (hiki.labels, HIBIKI), "ひびき が出ている");
    CHECK (! has (hiki.labels, PICK), "ピックおさえ は出ていない（アコギだけ専用）");
    CHECK (overlapCount (hiki.knobs) == 0, "ツマミどうしが重なっていない");

    // ---------------------------------------------------------------- [5]
    std::printf ("\n[5]「ぜんぶ表示」を入れれば、どの使いかたでも全部出る\n");
    const auto gitaAll  = shoot (1, true);
    const auto shabeAll = shoot (2, true);
    CHECK (has (gitaAll.labels, KOTOBA),  "アコギだけ+ぜんぶ表示: ことば が出る");
    CHECK (has (gitaAll.labels, TSUYA),   "アコギだけ+ぜんぶ表示: 艶 が出る");
    CHECK (has (shabeAll.labels, HIBIKI), "しゃべり+ぜんぶ表示: ひびき が出る");
    CHECK (has (gitaAll.labels, PICK), "アコギだけ+ぜんぶ表示: ピックおさえ も出る");
    //  ぜんぶ表示 = うたの全部 + ピックおさえ（うたでは通らないので、ふだんは出ない）
    CHECK ((int) gitaAll.knobs.size() == (int) uta.knobs.size() + 1,
           "うたの全部 + ピックおさえ の数になる (%d)", (int) gitaAll.knobs.size());
    CHECK (overlapCount (gitaAll.knobs) == 0, "全部出しても重なっていない");

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
