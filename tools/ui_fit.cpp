#include "TestPaths.h"
// ui_fit.cpp — v2.12.0「文字が省略されるのは論外」を機械で検査する。
//
//  目視だけに頼っていたので、v2.0.1 で手測りした固定幅(74/70/50)が
//  v2.12.0 まで残り、「トー…」「Ba…」「RES…」「ボイス…」になっていた。
//  ここから先は**納品前に必ずこれを通す**。
//
//  やること: エディタを実際に作り、文字サイズを 80/100/125/150% と変えて
//  resized() を走らせ、すべてのボタン・ラベルについて
//      「描画に使う実フォントでの文字幅」 > 「箱の幅」
//  になっていないかを測る。1件でもあれば FAIL。
//
//  ついでに**重なり**も見る（Band がテーマ帯の下敷きになっていた件）。
//  同じ親を持つ見えている子どうしが交差していたら FAIL。
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
#include <vector>
#include <string>
#include <map>
#include <cmath>

static int gFail = 0;

#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

// 文字の一部に「…」を最初から含める設計のもの（プレースホルダ等）は除外する。
static bool intentionalEllipsis (const juce::String& t)
{
    return t.endsWith (juce::String::fromUTF8 ("\xe2\x80\xa6"))     // …
        || t.endsWith ("...");
}

static void collect (juce::Component& c, std::vector<juce::Component*>& out)
{
    for (auto* k : c.getChildren())
    {
        if (! k->isVisible()) continue;
        out.push_back (k);
        collect (*k, out);
    }
}

struct Offender { std::string what, text; int need, have; };

// JUCE の drawFittedText は、入らないとき **まず横に潰し**、
// minimumHorizontalScale まで潰しても入らないときに初めて「…」にする。
//  - need * minScale > have  → 「…」が出る＝**論外**（FAIL）
//  - need > have             → 潰れて細くなる（読める。記録だけ残す＝注意）
// この2段構えにしないと、実際には「…」になっていない所まで FAIL になり、
// 本当に危ない所が埋もれる（初版がそうなった）。
static void checkFit (juce::Component& root, std::vector<Offender>& bad,
                      std::vector<Offender>& squeezed, std::vector<Offender>& tiny)
{
    std::vector<juce::Component*> all;
    collect (root, all);

    for (auto* c : all)
    {
        auto& lf = c->getLookAndFeel();
        std::string kind; juce::String t; int need = 0, have = 0; float minScale = 1.0f;

        // ★描画と**同じ** GzzioLnF::fitFont を通してから測る。
        //   ここを別ロジックで書くと、製品と検査がずれて意味が無くなる。
        if (auto* b = dynamic_cast<juce::TextButton*> (c))
        {
            t = b->getButtonText();
            if (t.isEmpty() || intentionalEllipsis (t)) continue;
            // GzzioLnF::drawButtonText は 左右4px、トグルならさらに左に 20px のランプ
            have = b->getWidth() - 8 - (b->getClickingTogglesState() ? 20 : 0);
            const auto f = GzzioLnF::fitFont (lf.getTextButtonFont (*b, b->getHeight()), t, have);
            need = juce::GlyphArrangement::getStringWidthInt (f, t);
            minScale = 0.92f;                    // drawButtonText で渡している値
            kind = "ボタン";
        }
        else if (auto* l = dynamic_cast<juce::Label*> (c))
        {
            t = l->getText();
            if (t.isEmpty() || intentionalEllipsis (t)) continue;
            // スライダー／コンボの中の数値・選択文字は中身が変わるので対象外
            if (dynamic_cast<juce::Slider*>   (l->getParentComponent()) != nullptr) continue;
            if (dynamic_cast<juce::ComboBox*> (l->getParentComponent()) != nullptr) continue;
            have = l->getWidth() - 4;
            const auto f = GzzioLnF::fitFont (lf.getLabelFont (*l), t, have);
            need = juce::GlyphArrangement::getStringWidthInt (f, t);
            minScale = l->getMinimumHorizontalScale();
            if (minScale <= 0.0f) minScale = 0.7f;
            kind = "ラベル";
        }
        else continue;

        // fitFont が 12px の底に当たっている＝箱が狭すぎる合図。
        // 「…」にはならないが、文字サイズを上げた人にとっては裏切りなので残す。
        {
            const auto raw = (kind == "ボタン")
                ? lf.getTextButtonFont (*dynamic_cast<juce::TextButton*> (c), c->getHeight())
                : lf.getLabelFont (*dynamic_cast<juce::Label*> (c));
            const auto fit = GzzioLnF::fitFont (raw, t, have);
            if (fit.getHeight() <= GzzioLnF::kFitMinPx + 0.01f && raw.getHeight() > 14.0f)
                tiny.push_back ({ kind, t.toStdString(),
                                  (int) raw.getHeight(), (int) fit.getHeight() });
        }

        if ((float) need * minScale > (float) have)
            bad.push_back ({ kind, t.toStdString(), need, have });     // 「…」が出る = 論外
        else if (need > have)
            squeezed.push_back ({ kind, t.toStdString(), need, have }); // 横に潰れる = 注意
    }
}

// 同じ親のもとで、見えている「押せる物」どうしが重なっていないか
static void checkOverlap (juce::Component& root, std::vector<Offender>& bad)
{
    std::vector<juce::Component*> all;
    collect (root, all);
    for (size_t i = 0; i < all.size(); ++i)
        for (size_t j = i + 1; j < all.size(); ++j)
        {
            auto* a = all[i]; auto* b = all[j];
            if (a->getParentComponent() != b->getParentComponent()) continue;
            const bool ai = dynamic_cast<juce::Button*> (a) || dynamic_cast<juce::ComboBox*> (a);
            const bool bi = dynamic_cast<juce::Button*> (b) || dynamic_cast<juce::ComboBox*> (b);
            if (! ai || ! bi) continue;
            auto ra = a->getBounds(), rb = b->getBounds();
            if (! ra.intersects (rb)) continue;
            const auto ov = ra.getIntersection (rb);
            if (ov.getWidth() < 3 || ov.getHeight() < 3) continue;   // 1-2px の接触は許す
            std::string an = dynamic_cast<juce::TextButton*> (a)
                           ? dynamic_cast<juce::TextButton*> (a)->getButtonText().toStdString() : "?";
            std::string bn = dynamic_cast<juce::TextButton*> (b)
                           ? dynamic_cast<juce::TextButton*> (b)->getButtonText().toStdString() : "?";
            bad.push_back ({ "重なり", an + " x " + bn, ov.getWidth(), 0 });
        }
}

// ★エディタが実際に読む場所を、同じ Options から求める。
//  最初は userApplicationDataDirectory(~/.config) に書いていたが、
//  PropertiesFile が使うのは ~/VocalGzzio/ で、4段階すべて既定値のまま
//  同じ結果が出ていた（＝検査が空振りしていた）。推測せず同じ道具で出す。
static juce::File prefsFile()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "VocalGzzio";
    o.filenameSuffix  = ".settings";
    o.folderName      = "VocalGzzio";
    o.storageFormat   = juce::PropertiesFile::storeAsXML;
    return gz::dataDirectory().getChildFile ("VocalGzzio.settings");
}

static void writePrefs (double font, bool advanced, int theme)
{
    auto f = prefsFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::String ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PROPERTIES>\n"
                                     "  <VALUE name=\"ui_advanced\" val=\"")
                       + juce::String (advanced ? 1 : 0) + "\"/>\n"
                       + "  <VALUE name=\"ui_layout_revision\" val=\"2\"/>\n"
            "  <VALUE name=\"ui_overview\" val=\"0\"/>\n"
            "  <VALUE name=\"ui_focus\" val=\"0\"/>\n"
                       + "  <VALUE name=\"ui_theme\" val=\"" + juce::String (theme) + "\"/>\n"
                       + "  <VALUE name=\"ui_font\" val=\"" + juce::String (font) + "\"/>\n"
                       + "  <VALUE name=\"ui_zoom\" val=\"1.0\"/>\n</PROPERTIES>\n");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto prefs  = prefsFile();
    auto backup = prefs.getSiblingFile ("VocalGzzio.bak_uifit");
    prefs.getParentDirectory().createDirectory();
    if (prefs.existsAsFile()) prefs.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } r { prefs, backup };

    std::printf ("画面の文字が箱に入りきっているかの検査（省略「…」の作り込み防止）\n\n");

    // ★検査していたのは「くわしい画面・日本語」だけだった。
    //  かんたんモードは一度も通しておらず、実際そこに重なりが残っていた
    //  （「おまかせ設定」の上に「トーク自動」）。英語(ブランド)も文字幅が違うので、
    //  4サイズ × 3通り の合計12回まわす。
    struct Mode { const char* name; bool advanced; int theme; };
    const Mode modes[] = {
        { "くわしい/日本語", true,  1 },   // 1 = ゆるふわ(既定)
        { "かんたん/日本語", false, 1 },
        { "くわしい/英語",   true,  3 },   // 3 = ブランド = 全UI英語
    };
    const double sizes[] = { 0.80, 1.00, 1.25, 1.50 };

    for (auto& mo : modes)
    {
        std::printf ("---- %s ----\n", mo.name);
        float lastProbeH = 0.0f;
        for (double s : sizes)
        {
            writePrefs (s, mo.advanced, mo.theme);
            VocalGzzioProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            if (ed == nullptr) { std::printf ("  FAIL: エディタが作れない\n"); return 1; }
            ed->setSize (ed->getWidth(), ed->getHeight());   // resized() を確実に走らせる

            std::vector<Offender> bad, squeezed, tiny;
            checkFit (*ed, bad, squeezed, tiny);
            checkOverlap (*ed, bad);

            // 検査が「文字サイズを変えたつもり」で終わっていないかの自己点検。
            // (実際に一度これで空振りした: 4段階すべて同じ数字が出ていた)
            float probeH = 0.0f;
            {
                juce::Label probe; probe.setText ("A", juce::dontSendNotification);
                ed->addAndMakeVisible (probe);
                probeH = ed->getLookAndFeel().getLabelFont (probe).getHeight();
                ed->removeChildComponent (&probe);
            }
            if (probeH <= lastProbeH + 0.5f && lastProbeH > 0.0f)
            {
                std::printf ("  FAIL: 文字サイズが効いていない(%.1f px のまま)。検査が空振りしています\n",
                             probeH);
                ++gFail;
            }
            lastProbeH = probeH;

            std::printf ("[文字 %3d%% / 実測 %.1f px] ", (int) (s * 100.0 + 0.5), probeH);
            if (bad.empty()) std::printf ("PASS  (横に潰れている物 %d 件)\n", (int) squeezed.size());
            else
            {
                std::printf ("FAIL: 「…」になる物 %d 件\n", (int) bad.size());
                for (auto& o : bad)
                    std::printf ("    %s \"%s\"  必要 %d px / 使える %d px\n",
                                 o.what.c_str(), o.text.c_str(), o.need, o.have);
                ++gFail;
            }
            for (auto& o : squeezed)
                std::printf ("    (注意) %s \"%s\"  %d px を %d px に潰して表示\n",
                             o.what.c_str(), o.text.c_str(), o.need, o.have);
            for (auto& o : tiny)
                std::printf ("    (小さすぎ) %s \"%s\"  %d px の字を %d px まで縮めている\n",
                             o.what.c_str(), o.text.c_str(), o.need, o.have);
        }
        std::printf ("\n");
    }

    // ================= 設計書§4「文字の規律」 =================
    //  26 / 20 / 17 / 14px の4段だけを使い、14px未満は全面禁止（100%表示時）。
    //  ★見て回るのでは絶対に抜ける。実際に**描かせて**、使われた文字の
    //   大きさを1つ残らず記録する。fitFont で縮んだ結果もここに出る。
    //  ★★1画面だけ測って「0件」と言わない。画面を変えれば別の文字が出る。
    //    （実際、EQ画面だけ測っていたときは チューナー と EQグラフ の
    //      8.5〜13px を1件も見つけられていなかった）
    std::printf ("\n---- 文字の大きさの実測（§4: 26/20/17/14px。14px未満は禁止）----\n");
    {
        struct Screen { const char* name; bool advanced; const char* press; };
        const Screen screens[] = {
            { "くわしい・EQ",       true,  nullptr    },
            { "くわしい・エフェクト", true,  "tabFx"    },
            { "かんたん",           false, nullptr    },
            { "1つずつ",            true,  "focusBtn" },
        };
        int grand = 0, grandUnder = 0;
        std::map<int,int> all;
        for (auto& sc : screens)
        {
            writePrefs (1.00, sc.advanced, 1);
            VocalGzzioProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            if (ed == nullptr) { std::printf ("  FAIL: エディタが作れない\n"); ++gFail; continue; }
            ed->setSize (ed->getWidth(), ed->getHeight());
            if (sc.press != nullptr)
            {
                std::vector<juce::Component*> all2; collect (*ed, all2);
                juce::Button* b = nullptr;
                for (auto* c : all2)
                    if (c->getComponentID() == sc.press)
                        if (auto* bb = dynamic_cast<juce::Button*> (c)) { b = bb; break; }
                if (b == nullptr) { std::printf ("  FAIL: %s のボタンが無い\n", sc.press); ++gFail; continue; }
                if (b->getClickingTogglesState()) b->setToggleState (true, juce::sendNotificationSync);
                else if (b->onClick) b->onClick();
                ed->setSize (ed->getWidth(), ed->getHeight());
            }

            FontProbe::seen().clear();
            FontProbe::on() = true;
            {
                juce::Image img (juce::Image::ARGB,
                                 juce::jmax (1, ed->getWidth()), juce::jmax (1, ed->getHeight()), true);
                juce::Graphics g (img);
                ed->paintEntireComponent (g, true);
            }
            FontProbe::on() = false;

            std::map<int,int> hist;
            for (float h : FontProbe::seen()) { hist[(int) std::lround (h * 2.0f)]++; all[(int) std::lround (h * 2.0f)]++; }
            int total = 0, under = 0;
            for (auto& kv : hist) { total += kv.second; if (kv.first < 28) under += kv.second; }
            grand += total; grandUnder += under;
            std::printf ("  [%s] 描いた文字 %d 回 / %d とおり / 14px未満 %d 回\n",
                         sc.name, total, (int) hist.size(), under);
            for (auto& kv : hist)
                if (kv.first < 28)
                    std::printf ("      ★%.1f px : %d 回\n", kv.first / 2.0, kv.second);
            CHECK (total > 20, "[%s] ちゃんと描けている（%d 回）", sc.name, total);
        }
        std::printf ("\n  ぜんぶの画面の合計: %d 回 / %d とおりの大きさ\n", grand, (int) all.size());
        for (auto& kv : all)
            std::printf ("    %5.1f px : %4d 回%s\n", kv.first / 2.0, kv.second,
                         kv.first < 28 ? "   ★14px未満" : "");
        CHECK (grandUnder == 0, "どの画面にも14px未満の文字が無い（%d 回）", grandUnder);
    }

    // ---- §4-0「押す物は36px以上」の実測 ----
    //  指で押す物（ボタン・コンボ・横スライダー）の高さを測る。
    //  回すツマミは自前描画で当たり判定が広いので、ここでは数えない。
    std::printf ("\n---- 押す物の高さ（§4-0: 36px以上）----\n");
    {
        writePrefs (1.00, true, 1);
        VocalGzzioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        if (ed == nullptr) { std::printf ("  FAIL: エディタが作れない\n"); ++gFail; }
        else
        {
            ed->setSize (ed->getWidth(), ed->getHeight());
            std::vector<juce::Component*> all; collect (*ed, all);
            std::vector<std::pair<std::string,int>> small;
            int counted = 0;
            for (auto* c : all)
            {
                const bool isBtn = (dynamic_cast<juce::Button*> (c) != nullptr);
                const bool isBox = (dynamic_cast<juce::ComboBox*> (c) != nullptr);
                bool isBar = false;
                if (auto* sl = dynamic_cast<juce::Slider*> (c))
                    isBar = (sl->getSliderStyle() != juce::Slider::RotaryHorizontalVerticalDrag);
                if (! (isBtn || isBox || isBar)) continue;
                if (c->getWidth() <= 0 || c->getHeight() <= 0) continue;
                ++counted;
                if (c->getHeight() < 36)
                {
                    std::string nm = c->getName().toStdString();
                    if (nm.empty()) nm = isBtn ? "(ボタン)" : isBox ? "(コンボ)" : "(横スライダー)";
                    small.push_back ({ nm, c->getHeight() });
                }
            }
            std::printf ("  押す物 %d 個 / 36px未満 %d 個\n", counted, (int) small.size());
            int shown = 0;
            for (auto& o : small)
                if (shown++ < 25) std::printf ("      %-28s %d px\n", o.first.c_str(), o.second);
            if ((int) small.size() > shown) std::printf ("      …ほか %d 個\n", (int) small.size() - shown);
            CHECK (counted > 20, "押す物をちゃんと数えている（%d 個）", counted);
        }
    }

    std::printf (gFail ? "== %d 段階で FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
