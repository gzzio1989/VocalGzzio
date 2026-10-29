#pragma once
#include <cstdlib>   // GZ_JN test hook

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <map>
#include <atomic>
#include <mutex>
#include <array>
#include "PluginProcessor.h"
#include "Tooltips.h"

//==============================================================================
// Dark palette (v1.3.0): blue-grey base with 3 elevation levels; accents are
// saturated versions of the mascot colours so they read well on dark bg.
namespace Palette
{
    // v1.6.1 "watchmaker" look: dark brushed gunmetal stage (like a premium
    // audio unit / skeleton watch), butter-brass + fresh mint accents kept.
    // v1.7.0: the palette is now MUTABLE so the theme switch (cross-switch)
    // can recolour every card/text/hairline at once. Defaults below are the
    // neutral "watchmaker" dark look; applyTheme() rewrites them per mode.
    inline juce::Colour bgTop    { 0xff111719 };   // bg0 (brushed dark steel)
    inline juce::Colour bgBot    { 0xff090d0f };
    inline juce::Colour panel    { 0xff171e21 };   // bg1 (cards, steel plate)
    inline juce::Colour panel2   { 0xff20292d };   // bg2 (raised surfaces)
    inline juce::Colour panelLn  { 0xff344247 };   // hairlines / grid
    inline juce::Colour ink      { 0xfff0f3ee };   // primary text
    inline juce::Colour inkSoft  { 0xffa3b1b3 };   // secondary text
    inline juce::Colour yellow   { 0xffd4f87b };   // butter / polished brass
    inline juce::Colour yellowDk { 0xffb5d45f };
    inline juce::Colour green    { 0xff85dcc4 };   // accent G
    inline juce::Colour salmon   { 0xffff8b99 };   // accent R (ruby bearings)
    inline juce::Colour blue     { 0xffb6aff8 };   // accent B
    inline juce::Colour ice      { 0xffd4f87b };   // mint: knob arcs / highlights
    inline juce::Colour track    { 0xff303c40 };   // knob/meter track
    inline juce::Colour badge    { 0xfffdfcf7 };   // paper badge for the mascot
    // クチュール・コスメ金具（ローズゴールド／プラチナ銀／黒銀／シャンパン金／ハニーゴールド）＆コンパクト鏡面色
    inline juce::Colour metalHi   { 0xffffe6d4 };
    inline juce::Colour metalMid  { 0xffd4967a };
    inline juce::Colour metalDark { 0xff8c523c };
    inline juce::Colour mirrorTop { 0xffde7da2 };
    inline juce::Colour mirrorBot { 0xff4f1830 };
    inline int          currentTheme { 0 };

    inline bool isCoutureTheme (int m) noexcept { return m == 5 || (m >= 10 && m <= 13); }
    inline int  coutureIndex   (int m) noexcept { return (m >= 10 && m <= 13) ? (m - 9) : 0; }
    inline int  themeOfCouture (int idx) noexcept
    {
        constexpr int kModes[5] = { 5, 10, 11, 12, 13 };
        return kModes[juce::jlimit (0, 4, idx)];
    }

    // 🌌🌊🌅🍁👑 風景・季節コレクション 5選 (14=天の川銀河 / 15=瑞々しい碧海 / 16=朝焼けの海 / 17=晩秋〜初冬の哀愁 / 18=蒼海オペラ座)
    inline bool isOceanStarTheme (int m) noexcept { return m >= 14 && m <= 18; }
    inline int  oceanStarIndex   (int m) noexcept { return (m >= 14 && m <= 18) ? (m - 14) : 0; }
    inline int  themeOfOceanStar (int idx) noexcept
    {
        constexpr int kModes[5] = { 14, 15, 16, 17, 18 };
        return kModes[juce::jlimit (0, 4, idx)];
    }

    // mode: 1 = yuru-kawa (soft pastel, light cards), else neutral (dark)
    // v1.9.0: 下地の明るさに応じて必ず読める文字色を返す。
    // 明るいアクセント(黄・ピンク)の上に淡い色を置いてしまい、
    // ゆるかわ/自然モードで選択中の項目が読めなくなっていた対策。
    inline juce::Colour readableOn (juce::Colour bg)
    {
        return bg.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff2b2419)
                                                   : juce::Colour (0xfff7f3ec);
    }

    // v2.0.0: アクセント色をそのまま使うと下地によっては読めない(例: 明るいモードの
    // クリーム地に黄色の見出し)。WCAGのコントラスト比が4.5:1を超えるまで、下地が
    // 明るければ暗く・暗ければ明るく寄せる。届いていればそのままの色を返す。
    inline float wcagLum (juce::Colour c)
    {
        auto ch = [] (float u) { return u <= 0.03928f ? u / 12.92f
                                                      : std::pow ((u + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * ch (c.getFloatRed()) + 0.7152f * ch (c.getFloatGreen())
             + 0.0722f * ch (c.getFloatBlue());
    }
    inline float wcagContrast (juce::Colour a, juce::Colour b)
    {
        const float la = wcagLum (a) + 0.05f, lb = wcagLum (b) + 0.05f;
        return la > lb ? la / lb : lb / la;
    }
    inline juce::Colour accentOn (juce::Colour accent, juce::Colour bg, float minRatio = 4.5f)
    {
        juce::Colour c = accent;
        const bool darkBg = wcagLum (bg) < 0.30f;
        for (int i = 0; i < 14 && wcagContrast (c, bg) < minRatio; ++i)
            c = darkBg ? c.brighter (0.16f) : c.darker (0.16f);
        return c;
    }

    inline void applyTheme (int mode)
    {
        currentTheme = mode;
        // デフォルトのクチュール金具・鏡面色（各モードで上書き）
        metalHi   = juce::Colour (0xffffe6d4);
        metalMid  = juce::Colour (0xffd4967a);
        metalDark = juce::Colour (0xff8c523c);
        mirrorTop = juce::Colour (0xffde7da2);
        mirrorBot = juce::Colour (0xff4f1830);

        if (mode == 1)   // ゆるふわ (Salon de Thé): 焦がしキャラメル＆バニラカスタード＆フランボワーズのパティスリー配色
        {
            bgTop    = juce::Colour (0xfff6e7d8);   // ミルクティー＆ビスケット
            bgBot    = juce::Colour (0xffebd5c0);   // ベイクド・フィナンシェ
            panel    = juce::Colour (0xfffff8f0);   // マスカルポーネ・クリーム下地
            panel2   = juce::Colour (0xfff6dfc8);   // キャラメル・スポンジファンデーション
            panelLn  = juce::Colour (0xffcca482);   // アンティーク・キャラメルゴールド枠
            ink      = juce::Colour (0xff422a1e);   // 深煎りエスプレッソ文字
            inkSoft  = juce::Colour (0xff7c5947);   // シナモンモカ副文字
            yellow   = juce::Colour (0xffe08a38);   // キャラメルハニー主アクセント
            yellowDk = juce::Colour (0xffbf6e22);
            green    = juce::Colour (0xff5ca678);   // ピスタチオ・マカロン
            salmon   = juce::Colour (0xffe25d78);   // フランボワーズ・ベリー
            blue     = juce::Colour (0xff768cd4);   // ブルーベリー・タルト
            ice      = juce::Colour (0xff6db592);   // ミント・グラッセ
            track    = juce::Colour (0xffe5cbb4);   // カフェラテ・トラック溝
            badge    = juce::Colour (0xfffffbf6);
            metalHi   = juce::Colour (0xfffff0d8);
            metalMid  = juce::Colour (0xffc89468);
            metalDark = juce::Colour (0xff825232);
        }
        else if (mode == 8)   // v2.11.0 ゆるふわ・よる: 夜でも目が痛くならない暗いパステル
        {
            // 「ふつう(0)」の青灰ダークとは別物。ゆるふわの暖かさを残したまま
            // 明度だけ落とした「月あかりの子ども部屋」。差し色は昼より彩度を
            // 落としてある(暗い下地では同じ彩度でも眩しく感じるため)。
            // 文字色のコントラストは全ペア 5.9:1 以上で実測ずみ(WCAG AA)。
            bgTop    = juce::Colour (0xff221d2e);   // 夕闇のインディゴ
            bgBot    = juce::Colour (0xff15111f);
            panel    = juce::Colour (0xff2b2439);   // 暗いプラムのカード
            panel2   = juce::Colour (0xff342a45);   // ひとつ上の面(ヒーロー帯)
            panelLn  = juce::Colour (0xff4a3f60);
            ink      = juce::Colour (0xfff2ece1);   // 月あかりのクリーム(暖色寄り)
            inkSoft  = juce::Colour (0xffb3a8bd);
            yellow   = juce::Colour (0xfff0d38a);   // 昼より落とした月バター
            yellowDk = juce::Colour (0xffd9b96a);
            green    = juce::Colour (0xff86d9bd);
            salmon   = juce::Colour (0xfff0a1b4);
            blue     = juce::Colour (0xffa7bdf0);
            ice      = juce::Colour (0xff8fdccb);
            track    = juce::Colour (0xff3a3049);
            badge    = juce::Colour (0xfff6f1ea);
        }
        else if (mode == 2)   // 自然（四季・癒し）: 明るい緑/土のやすらぎ配色
        {
            bgTop    = juce::Colour (0xffdfeee4);
            bgBot    = juce::Colour (0xffeef2e2);
            panel    = juce::Colour (0xfff6faf2);
            panel2   = juce::Colour (0xffe8f0e2);
            panelLn  = juce::Colour (0xffcdd9c4);
            ink      = juce::Colour (0xff3a4a38);
            inkSoft  = juce::Colour (0xff7c8a76);
            yellow   = juce::Colour (0xffe6b34a);
            yellowDk = juce::Colour (0xffcf9a33);
            green    = juce::Colour (0xff5aa86a);
            salmon   = juce::Colour (0xffe98f7a);
            blue     = juce::Colour (0xff7bafd0);
            ice      = juce::Colour (0xff5aa86a);
            track    = juce::Colour (0xffdde7d5);
            badge    = juce::Colour (0xfffbfef8);
        }
        else if (mode == 3)   // ブランド: ダークなレザー・ラグジュアリー（金の差し色）
        {
            // v1.9.0: 商用化に向けて茶×金(既存ブランド想起)から
            //          自社色「深いプラム × シャンパン」へ変更
            bgTop    = juce::Colour (0xff2a2038);
            bgBot    = juce::Colour (0xff140e1d);
            panel    = juce::Colour (0xff332742);
            panel2   = juce::Colour (0xff3d3050);
            panelLn  = juce::Colour (0xff574468);
            ink      = juce::Colour (0xfff3ebf8);
            inkSoft  = juce::Colour (0xffb9a9c8);
            yellow   = juce::Colour (0xffe0c48c);
            yellowDk = juce::Colour (0xffc7a86d);
            green    = juce::Colour (0xff9dc0a8);
            salmon   = juce::Colour (0xffd08fa8);
            blue     = juce::Colour (0xff8f9ed8);
            ice      = juce::Colour (0xffe0c48c);
            track    = juce::Colour (0xff453757);
            badge    = juce::Colour (0xfff3ebf8);
        }
        else if (mode == 4)   // オーロラ / ライブ会場: 暗い舞台＋鮮やかなオーロラ照明の差し色
        {
            bgTop    = juce::Colour (0xff17131f);
            bgBot    = juce::Colour (0xff0a0810);
            panel    = juce::Colour (0xff201a2b);
            panel2   = juce::Colour (0xff2a2338);
            panelLn  = juce::Colour (0xff453a5a);
            ink      = juce::Colour (0xfff2eefa);
            inkSoft  = juce::Colour (0xffa39ab8);
            yellow   = juce::Colour (0xfff2c14a);
            yellowDk = juce::Colour (0xffd8a733);
            green    = juce::Colour (0xff43d492);
            salmon   = juce::Colour (0xffff5c8a);
            blue     = juce::Colour (0xff4bc8e8);
            ice      = juce::Colour (0xff4bc8e8);
            track    = juce::Colour (0xff342b45);
            badge    = juce::Colour (0xfff2eefa);
        }
        else if (mode == 5)   // 01 ロゼ・クチュール (Rose Gold & Ruby): ポーセリン・クッションファンデ＆ローズゴールド配色
        {
            bgTop    = juce::Colour (0xfffbe4ec);   // シルキー・ローズブラッシュ下地
            bgBot    = juce::Colour (0xfff0cfe0);   // ピオニー・モーヴベルベット
            panel    = juce::Colour (0xfffff3f6);   // ポーセリン・クッションファンデーション
            panel2   = juce::Colour (0xfff8d5e2);   // プレスト・サテンチーク内面
            panelLn  = juce::Colour (0xffd496a8);   // シャンパン・ローズゴールド縁
            ink      = juce::Colour (0xff3b1828);   // ディープ・ベリーカカオ（アイライナー黒茶）
            inkSoft  = juce::Colour (0xff7c485e);   // モーヴ・トープ
            yellow   = juce::Colour (0xffe44d7e);   // クチュール・ルビーローズ主アクセント
            yellowDk = juce::Colour (0xffc23262);
            green    = juce::Colour (0xffe06e68);   // コーラル・ピーチチーク（01区画＆ONスイッチ）
            salmon   = juce::Colour (0xffe8456e);   // ベリー・リップティント（03区画）
            blue     = juce::Colour (0xffa85ca6);   // アメジスト・モーヴシャドウ（02区画）
            ice      = juce::Colour (0xffdc7692);   // ローズシャンパン・ハイライター
            track    = juce::Colour (0xffebc0d2);   // フェイスパウダー・ローズ溝
            badge    = juce::Colour (0xfffff6f9);
            metalHi   = juce::Colour (0xffffe6d4);  // シャンパン・ローズゴールド
            metalMid  = juce::Colour (0xffd4967a);
            metalDark = juce::Colour (0xff8c523c);
            mirrorTop = juce::Colour (0xffde7da2);  // ローズクォーツ鏡面
            mirrorBot = juce::Colour (0xff4f1830);
        }
        else if (mode == 10)  // 02 ブルベ透明感・アイシーラベンダー＆プラチナシルバー (Glacier Lilac & Platinum Silver)
        {
            bgTop    = juce::Colour (0xffe8e6fa);   // アイシー・ライラックシルク下地
            bgBot    = juce::Colour (0xffd5d2f2);   // ペリウィンクル・ベルベット
            panel    = juce::Colour (0xfff6f5ff);   // トーンアップ・ラベンダーファンデーション
            panel2   = juce::Colour (0xffe2dff8);   // プレスト・ライラックサテンハイライター
            panelLn  = juce::Colour (0xff9ea4cc);   // プラチナ・スターリングシルバー枠
            ink      = juce::Colour (0xff1e1a3a);   // ミッドナイト・ネイビーバイオレット文字
            inkSoft  = juce::Colour (0xff56507c);   // ライラック・スレート副文字
            yellow   = juce::Colour (0xff7654d8);   // クリスタル・アイリス主アクセント
            yellowDk = juce::Colour (0xff5838b8);
            green    = juce::Colour (0xff468ed8);   // アイスサファイア・プリズム（01区画＆ONスイッチ）
            salmon   = juce::Colour (0xffd04a96);   // クール・フューシャベリー（03区画）
            blue     = juce::Colour (0xff8456d2);   // ロイヤル・アメジストシャドウ（02区画）
            ice      = juce::Colour (0xff5298e6);   // ダイヤモンド・ブルーシマー
            track    = juce::Colour (0xffcecaec);   // ラベンダーパウダー溝
            badge    = juce::Colour (0xfffaf9ff);
            metalHi   = juce::Colour (0xfff6f9ff);  // プラチナ・スターリングシルバー（銀金具）
            metalMid  = juce::Colour (0xff9aa6c6);
            metalDark = juce::Colour (0xff4e5876);
            mirrorTop = juce::Colour (0xff788ee8);  // スターライト・サファイア鏡面
            mirrorBot = juce::Colour (0xff181a42);
        }
        else if (mode == 11)  // 03 小悪魔ゴシック・ノワールロリータ (Noir Ruban & Velvet Gothic)
        {
            bgTop    = juce::Colour (0xff1c131e);   // ノワール・ベルベットレース下地
            bgBot    = juce::Colour (0xff0f0912);   // オブシディアン・プラム
            panel    = juce::Colour (0xff271a2a);   // 漆黒ベルベット・コンパクトケース
            panel2   = juce::Colour (0xff36233a);   // スモーキー・モーヴサテン内面
            panelLn  = juce::Colour (0xff8c5674);   // アンティーク・ローズピューター枠
            ink      = juce::Colour (0xfffcecf5);   // ドール・ポーセリンホワイトピンク文字
            inkSoft  = juce::Colour (0xffc8a0b8);   // ダスティ・ロゼ副文字
            yellow   = juce::Colour (0xfff0588c);   // 小悪魔ロゼ・ルビー主アクセント
            yellowDk = juce::Colour (0xffc83268);
            green    = juce::Colour (0xffee72a2);   // くすみベビーピンク・リボン（01区画＆ONスイッチ）
            salmon   = juce::Colour (0xfff43868);   // 深紅フランボワーズ・ティント（03区画）
            blue     = juce::Colour (0xffb86ce8);   // ゴシック・オーキッドバイオレット（02区画）
            ice      = juce::Colour (0xfff48ab0);   // ムーンライト・ロゼハイライター
            track    = juce::Colour (0xff452c4a);   // ノワール・ベルベット溝
            badge    = juce::Colour (0xfffcecf5);
            metalHi   = juce::Colour (0xfff5dce8);  // アンティーク・ブラックシルバー＆ローズ金具
            metalMid  = juce::Colour (0xffa66c88);
            metalDark = juce::Colour (0xff442034);
            mirrorTop = juce::Colour (0xffc83668);  // ガーネット・ノワール鏡面
            mirrorBot = juce::Colour (0xff180510);
        }
        else if (mode == 12)  // 04 ティファニーミント＆パールホワイト・ジュエリー (Celadon Bijou & Champagne Gold)
        {
            bgTop    = juce::Colour (0xffdcf4ed);   // ティファニー・ミントシルク下地
            bgBot    = juce::Colour (0xffc2e6dc);   // セラドン・サテンベルベット
            panel    = juce::Colour (0xfff2fcf9);   // パールアイボリー・ミントファンデーション
            panel2   = juce::Colour (0xffd4efe6);   // プレスト・セラドンミントパウダー
            panelLn  = juce::Colour (0xff7ebcae);   // ミント・シャンパンゴールド枠
            ink      = juce::Colour (0xff143630);   // ディープ・フォレストティール文字
            inkSoft  = juce::Colour (0xff447268);   // セージ・ティール副文字
            yellow   = juce::Colour (0xff209882);   // ロイヤル・エメラルドミント主アクセント
            yellowDk = juce::Colour (0xff147664);
            green    = juce::Colour (0xff26a48c);   // マカロン・ミントグリーン（01区画＆ONスイッチ）
            salmon   = juce::Colour (0xffe05e78);   // カメリア・コーラルローズ差し色（03区画）
            blue     = juce::Colour (0xff328cb8);   // アクアマリン・ラグーンビジュー（02区画）
            ice      = juce::Colour (0xff30b09a);   // パール・アクアハイライター
            track    = juce::Colour (0xffbae0d4);   // ミントパウダー溝
            badge    = juce::Colour (0xfff7fefa);
            metalHi   = juce::Colour (0xfffff2d6);  // シャンパン・イエローゴールド金具
            metalMid  = juce::Colour (0xffcfa262);
            metalDark = juce::Colour (0xff7c5624);
            mirrorTop = juce::Colour (0xff3eb8a0);  // エメラルド・ラグーン鏡面
            mirrorBot = juce::Colour (0xff0d362e);
        }
        else if (mode == 13)  // 05 多幸感ハニーピーチ＆アプリコット・グロウ (Apricot Miel & Warm Gold)
        {
            bgTop    = juce::Colour (0xffffefe0);   // ハニーピーチ・シルククリーム下地
            bgBot    = juce::Colour (0xfff8d8be);   // アプリコット・サテン
            panel    = juce::Colour (0xfffff8f1);   // ウォームバニラ・クッションファンデーション
            panel2   = juce::Colour (0xfffce0c8);   // プレスト・アプリコットコーラルチーク
            panelLn  = juce::Colour (0xffdca076);   // ハニー・イエローゴールド枠
            ink      = juce::Colour (0xff401e12);   // ロースト・カカオブラウン文字
            inkSoft  = juce::Colour (0xff804e3a);   // シナモン・ピーチ副文字
            yellow   = juce::Colour (0xffe46434);   // 完熟アプリコット・コーラル主アクセント
            yellowDk = juce::Colour (0xffc04a1e);
            green    = juce::Colour (0xffe67834);   // フレッシュ・マンゴーオレンジ（01区画＆ONスイッチ）
            salmon   = juce::Colour (0xffea5446);   // ジューシー・白桃コーラルティント（03区画）
            blue     = juce::Colour (0xffc65864);   // テラコッタ・フィグローズ（02区画）
            ice      = juce::Colour (0xffe4844a);   // ハニーシャンパン・グロウ
            track    = juce::Colour (0xfff0cab0);   // ピーチパウダー溝
            badge    = juce::Colour (0xfffffaf5);
            metalHi   = juce::Colour (0xfffff4d0);  // 24K ハニー・イエローゴールド金具
            metalMid  = juce::Colour (0xffda9c48);
            metalDark = juce::Colour (0xff885616);
            mirrorTop = juce::Colour (0xffea8458);  // サンセット・パパラチア鏡面
            mirrorBot = juce::Colour (0xff4a1a0c);
        }
        else if (mode == 6)   // ネオン (Cyber Neon): 配信・ゲーミング向け サイバーネオン配色
        {
            bgTop    = juce::Colour (0xff0b0c18);
            bgBot    = juce::Colour (0xff05060c);
            panel    = juce::Colour (0xff121528);
            panel2   = juce::Colour (0xff191d36);
            panelLn  = juce::Colour (0xff313b68);
            ink      = juce::Colour (0xffeef6ff);
            inkSoft  = juce::Colour (0xff8ea2c8);
            yellow   = juce::Colour (0xff00f0ff);   // サイバーシアン主アクセント
            yellowDk = juce::Colour (0xff00bdd0);
            green    = juce::Colour (0xff00ff9d);   // ネオンミント
            salmon   = juce::Colour (0xffff2a85);   // ネオンマゼンタ
            blue     = juce::Colour (0xffa855f7);   // エレクトリックバイオレット
            ice      = juce::Colour (0xff38bdf8);
            track    = juce::Colour (0xff23294a);
            badge    = juce::Colour (0xffeef6ff);
        }
        else if (mode == 9)   // スタジオ (Studio Pro): プロミュージシャン・エンジニア向け アナログコンソール配色
        {
            bgTop    = juce::Colour (0xff191715);
            bgBot    = juce::Colour (0xff100e0c);
            panel    = juce::Colour (0xff24201d);
            panel2   = juce::Colour (0xff2e2925);
            panelLn  = juce::Colour (0xff4c433b);
            ink      = juce::Colour (0xfff4ece1);   // ウォームシルク刻印文字
            inkSoft  = juce::Colour (0xffb2a596);
            yellow   = juce::Colour (0xffe8a948);   // VUメーター・アンバーブラス
            yellowDk = juce::Colour (0xffc6882c);
            green    = juce::Colour (0xff64b878);   // コンソールシグナルグリーン
            salmon   = juce::Colour (0xffe0634e);   // ピークインジケータ・バーミリオン
            blue     = juce::Colour (0xff6299b8);   // Neve/API スチールブルー
            ice      = juce::Colour (0xffdfa44e);
            track    = juce::Colour (0xff3a332d);
            badge    = juce::Colour (0xfff4ece1);
        }
        else if (mode == 7)   // 色覚サポート: Okabe-Ito カラーユニバーサル配色 (高コントラスト)
        {
            bgTop    = juce::Colour (0xff11161b);
            bgBot    = juce::Colour (0xff0a0e12);
            panel    = juce::Colour (0xff1b232b);
            panel2   = juce::Colour (0xff242e38);
            panelLn  = juce::Colour (0xff45535f);
            ink      = juce::Colour (0xfff4f7f9);
            inkSoft  = juce::Colour (0xffaebac4);
            yellow   = juce::Colour (0xffe69f00);   // CUD orange
            yellowDk = juce::Colour (0xffc98700);
            green    = juce::Colour (0xff009e73);   // CUD bluish green
            salmon   = juce::Colour (0xffd55e00);   // CUD vermillion
            blue     = juce::Colour (0xff0072b2);   // CUD blue
            ice      = juce::Colour (0xff56b4e9);   // CUD sky blue
            track    = juce::Colour (0xff2c3944);
            badge    = juce::Colour (0xfff4f7f9);
        }
        else if (mode == 14)  // 🌌 01 星降る天の川銀河と蛍火の夜 (Galactic Milky Way & Firefly Night)
        {
            bgTop    = juce::Colour (0xff05081d);   // 深宇宙のコズミック・ミッドナイト
            bgBot    = juce::Colour (0xff101c42);   // 湖畔の森陰と星霧の藍色
            panel    = juce::Colour (0xff0b1332);   // 天の川を透かすアストラル・サファイアガラス
            panel2   = juce::Colour (0xff162352);   // 星雲のネビュラ・インディゴ内面
            panelLn  = juce::Colour (0xff7282e0);   // 天の川アメジスト＆スターライト枠
            ink      = juce::Colour (0xfffffaed);   // 温かな星あかりのアイボリー文字
            inkSoft  = juce::Colour (0xffc2d4f8);   // 星雲のミルキーブルー副文字
            yellow   = juce::Colour (0xfffde047);   // 黄金の蛍火＆一等星アンバー（主アクセント）
            yellowDk = juce::Colour (0xffd97706);
            green    = juce::Colour (0xff2dd4bf);   // バイオルミネセンス・星霧ティール（01区画＆ON）
            salmon   = juce::Colour (0xfff472b6);   // ばら星雲のロゼット・マゼンタピンク（03区画）
            blue     = juce::Colour (0xffa855f7);   // アンドロメダ銀河のネビュラ・バイオレット（02区画）
            ice      = juce::Colour (0xff38bdf8);   // ベガ＆流星のスターライト・シアン
            track    = juce::Colour (0xff1f2f68);
            badge    = juce::Colour (0xfffffaed);
            metalHi   = juce::Colour (0xfffef08a);  // 天球儀のスターライト・シャンパンゴールド金具
            metalMid  = juce::Colour (0xfff59e0b);
            metalDark = juce::Colour (0xff78350f);
            mirrorTop = juce::Colour (0xff3b2d88);  // 銀河ネビュラ天球ドーム
            mirrorBot = juce::Colour (0xff06091e);
        }
        else if (mode == 15)  // 🌊 02 瑞々しい碧海と水紋・アクアラグーン (Lush Aqua Sea & Caustic Ripples)
        {
            bgTop    = juce::Colour (0xff043b52);   // 陽光が降り注ぐ透明なターコイズ水面
            bgBot    = juce::Colour (0xff041f36);   // 瑞々しいアクアマリン・ラグーンの深み
            panel    = juce::Colour (0xff07344a);   // 水滴が弾けるウォータードロップ・クリスタルガラス
            panel2   = juce::Colour (0xff0d4d68);   // 水紋コースティクスが揺らめくアクア内面
            panelLn  = juce::Colour (0xff38e2f8);   // 煌めく水沫と水滴のブリリアント・アクアリム
            ink      = juce::Colour (0xfff2feff);   // 澄み切ったウォーター・クリスタルホワイト文字
            inkSoft  = juce::Colour (0xffa6f4ff);   // 瑞々しいシーフォーム・アクア副文字
            yellow   = juce::Colour (0xffffe47a);   // 水面に反射するサンライト・パールゴールド（主アクセント）
            yellowDk = juce::Colour (0xffd99b26);
            green    = juce::Colour (0xff00f5d4);   // 瑞々しいルミナス・アクアミント（01区画＆ON）
            salmon   = juce::Colour (0xffff7597);   // 珊瑚礁と水泡のコーラル・シェルピンク（03区画）
            blue     = juce::Colour (0xff00bbf9);   // 透明感あふれるクリスタル・ラグーンブルー（02区画）
            ice      = juce::Colour (0xff48cae4);   // 水紋コースティクスのピュア・アクアシアン
            track    = juce::Colour (0xff125875);
            badge    = juce::Colour (0xfff2feff);
            metalHi   = juce::Colour (0xffe0fbfc);  // 水滴プラチナ＆アクアクリスタル金具
            metalMid  = juce::Colour (0xff00d2e6);
            metalDark = juce::Colour (0xff0a5c75);
            mirrorTop = juce::Colour (0xff00b4d8);  // 瑞々しい水球アクア・オーブ鏡面
            mirrorBot = juce::Colour (0xff032238);
        }
        else if (mode == 16)  // 🌅 03 朝焼けの海と暁の水平線 (Radiant Dawn & Sunrise Sea)
        {
            bgTop    = juce::Colour (0xff14163d);   // 暁の一番星が残るモーニング・インディゴ空
            bgBot    = juce::Colour (0xff3a1c33);   // 朝焼けの彩雲と黄金の朝日を映す海面
            panel    = juce::Colour (0xff211a3a);   // 朝焼けの光を透かすドーン・プリズムガラス
            panel2   = juce::Colour (0xff382246);   // 暁のローズ・バイオレット内面
            panelLn  = juce::Colour (0xfff79d65);   // 昇る朝日のサンライズ・コーラルゴールド枠
            ink      = juce::Colour (0xfffff8f2);   // 朝露に輝くパール・アイボリー文字
            inkSoft  = juce::Colour (0xfff7c6b8);   // 朝焼け雲のアプリコット・ロゼ副文字
            yellow   = juce::Colour (0xffffbe0b);   // 水平線から昇る朝日のサンライズ・ゴールド（主アクセント）
            yellowDk = juce::Colour (0xffe85d04);
            green    = juce::Colour (0xff2ec4b6);   // 朝凪のクリア・エメラルドシー（01区画＆ON）
            salmon   = juce::Colour (0xffff6b8b);   // 暁に染まる彩雲のロゼ・マゼンタコーラル（03区画）
            blue     = juce::Colour (0xffa78bfa);   // 夜明け空のモーニング・ライラック（02区画）
            ice      = juce::Colour (0xffff9e64);   // 朝日の光芒ゴールデン・アプリコット
            track    = juce::Colour (0xff48294c);
            badge    = juce::Colour (0xfffff8f2);
            metalHi   = juce::Colour (0xffffebb3);  // サンライズ・シャンパンゴールド金具
            metalMid  = juce::Colour (0xfff48c42);
            metalDark = juce::Colour (0xff7c2d12);
            mirrorTop = juce::Colour (0xffe76f51);  // 朝焼けの太陽コロナ鏡面
            mirrorBot = juce::Colour (0xff241538);
        }
        else if (mode == 17)  // 🍁 04 晩秋から初冬へ・茜空と初雪の水鏡 (Late Autumn to Early Winter Melancholy)
        {
            bgTop    = juce::Colour (0xff121a2d);   // 初冬の冷気が降りてくるウィンター・スレートインディゴ空
            bgBot    = juce::Colour (0xff21171f);   // 散り紅葉と初雪が溶け合う静寂の湖面水鏡
            panel    = juce::Colour (0xff1a2132);   // 夕茜と霜の結晶を宿すフロスト・アンバーガラス
            panel2   = juce::Colour (0xff2c212c);   // 哀愁の茜色と冬霞が交わるサーフェス
            panelLn  = juce::Colour (0xffd97757);   // 紅葉の唐紅（からくれない）＆霜銀リム
            ink      = juce::Colour (0xfff9f3ec);   // 初雪と和紙のウォーム・フロストホワイト文字
            inkSoft  = juce::Colour (0xffd8c2b2);   // 枯野と川霧のセピア・トープ副文字
            yellow   = juce::Colour (0xfff59e0b);   // 散りゆく銀杏と夕茜の琥珀ゴールド（主アクセント）
            yellowDk = juce::Colour (0xffb45309);
            green    = juce::Colour (0xff67c7d4);   // 初冬の氷晶・フロストアイスブルー（01区画＆ON）
            salmon   = juce::Colour (0xffe04f44);   // 晩秋のもみじ・唐紅クリムゾン（03区画）
            blue     = juce::Colour (0xff8ea4d2);   // 遠山を包む冬霞のスレート・ペリウィンクル（02区画）
            ice      = juce::Colour (0xffbae6fd);   // 舞い降りる初雪のクリスタル・スノーホワイト
            track    = juce::Colour (0xff3b2c37);
            badge    = juce::Colour (0xfff9f3ec);
            metalHi   = juce::Colour (0xfffde6c4);  // 琥珀金と氷晶シルバーのアンティーク金具
            metalMid  = juce::Colour (0xffd97742);
            metalDark = juce::Colour (0xff6b2d1a);
            mirrorTop = juce::Colour (0xffa83e36);  // 茜空と冬霞を映す水鏡ドーム
            mirrorBot = juce::Colour (0xff111929);
        }
        else if (mode == 18)  // 👑 05 蒼海オペラ座と黄金シャンデリア (Grand Opera Sapphire & Chandelier)
        {
            bgTop    = juce::Colour (0xff040f2d);   // 深海ロイヤル・ウルトラマリン
            bgBot    = juce::Colour (0xff0b2058);   // 蒼海オペラ座のベルベットドレープ
            panel    = juce::Colour (0xff091a45);   // ロイヤル・サファイアベルベット筐体
            panel2   = juce::Colour (0xff122b6b);   // 深いコバルト・サテン内面
            panelLn  = juce::Colour (0xffd4af37);   // 24K アンティーク・シャンデリア金枠
            ink      = juce::Colour (0xfffff8eb);   // シャンパン・アイボリー文字
            inkSoft  = juce::Colour (0xffb8cae8);   // ロイヤル・シルバーブルー副文字
            yellow   = juce::Colour (0xfff2c94c);   // シャンデリア・アンティークゴールド（主アクセント）
            yellowDk = juce::Colour (0xffb4833e);
            green    = juce::Colour (0xff2ec4b6);   // アビサル・エメラルドラグーン（01区画＆ON）
            salmon   = juce::Colour (0xffe07a5f);   // オペラ劇場のローズアンバー＆テラコッタ（03区画）
            blue     = juce::Colour (0xff3b82f6);   // エレクトリック・アビサルサファイア（02区画）
            ice      = juce::Colour (0xff60a5fa);   // 蒼海のスポットライト・アズール
            track    = juce::Colour (0xff19357a);
            badge    = juce::Colour (0xfffff8eb);
            metalHi   = juce::Colour (0xfffbe6a2);  // アンティーク・オペラゴールド金具
            metalMid  = juce::Colour (0xffd4af37);
            metalDark = juce::Colour (0xff6e4f1a);
            mirrorTop = juce::Colour (0xff1d4ed8);  // 蒼海サファイア＆シャンデリア鏡面
            mirrorBot = juce::Colour (0xff050e29);
        }
        else if (mode == 19)    // 🕰️ 初代 v1.0.0 クリーム・フラット (v1.0.0_ui.png 完全再現パレット)
        {
            bgTop    = juce::Colour (0xfffaf8f2);
            bgBot    = juce::Colour (0xfff5f1e6);
            panel    = juce::Colour (0xfffffefa);
            panel2   = juce::Colour (0xfff3efe4);
            panelLn  = juce::Colour (0xffddd6c7);
            ink      = juce::Colour (0xff263b54);
            inkSoft  = juce::Colour (0xff5a6d82);
            yellow   = juce::Colour (0xffdac438);
            yellowDk = juce::Colour (0xffb8a224);
            green    = juce::Colour (0xff62a888);
            salmon   = juce::Colour (0xffee8b72);
            blue     = juce::Colour (0xff4a709c);
            ice      = juce::Colour (0xffdac438);
            track    = juce::Colour (0xffe3dfd3);
            badge    = juce::Colour (0xfffffefa);
        }
        else if (mode == 20)    // 🕰️ v1.3.0 ダークネイビー 2列スタジオ (v1.3.0_ui.png 完全再現パレット)
        {
            bgTop    = juce::Colour (0xff121622);
            bgBot    = juce::Colour (0xff0e121c);
            panel    = juce::Colour (0xff1a2030);
            panel2   = juce::Colour (0xff232a3d);
            panelLn  = juce::Colour (0xff2e374f);
            ink      = juce::Colour (0xfff0f4f8);
            inkSoft  = juce::Colour (0xff94a0b8);
            yellow   = juce::Colour (0xfff0c445);
            yellowDk = juce::Colour (0xffd4a628);
            green    = juce::Colour (0xff62c898);
            salmon   = juce::Colour (0xffec6868);
            blue     = juce::Colour (0xff6088d8);
            ice      = juce::Colour (0xfff0c445);
            track    = juce::Colour (0xff283044);
            badge    = juce::Colour (0xfffaf8f2);
        }
        else
        {
            bgTop    = juce::Colour (0xff111719);
            bgBot    = juce::Colour (0xff090d0f);
            panel    = juce::Colour (0xff171e21);
            panel2   = juce::Colour (0xff20292d);
            panelLn  = juce::Colour (0xff344247);
            ink      = juce::Colour (0xfff0f3ee);
            inkSoft  = juce::Colour (0xffa3b1b3);
            yellow   = juce::Colour (0xffd4f87b);
            yellowDk = juce::Colour (0xffb5d45f);
            green    = juce::Colour (0xff85dcc4);
            salmon   = juce::Colour (0xffff8b99);
            blue     = juce::Colour (0xffb6aff8);
            ice      = juce::Colour (0xffd4f87b);
            track    = juce::Colour (0xff303c40);
            badge    = juce::Colour (0xfffdfcf7);
        }
    }

    // ★ 表示スタイル別パレット上書き:
    //   1 = ◉ くっきり大文字 (WCAG AAA 12:1 超高コントラスト・漆黒マット下地・純白＆黄金文字)
    //   2 = ★ かんたん3ステップ (視覚ノイズを抑えたディープスレート下地・明確な3色ステップ配色)
    //   3 = ● マット・具体表記 (眩しさ・発光・高彩度を排したウォームスレート＆セージグリーン配色)
    inline void applyAccessOverride (int accessMode) noexcept
    {
        if (accessMode == 1)        // ◉ くっきり大文字スタイル (WCAG AAA High-Clarity)
        {
            bgTop     = juce::Colour (0xff070b10);
            bgBot     = juce::Colour (0xff05080c);
            panel     = juce::Colour (0xff0e1520);
            panel2    = juce::Colour (0xff172234);
            panelLn   = juce::Colour (0xffb0c8e4);
            ink       = juce::Colour (0xffffffff);
            inkSoft   = juce::Colour (0xffe6f0fa);
            yellow    = juce::Colour (0xffffd600);
            yellowDk  = juce::Colour (0xffe6b800);
            green     = juce::Colour (0xff00e699);
            salmon    = juce::Colour (0xffff6b8b);
            blue      = juce::Colour (0xff58b8ff);
            ice       = juce::Colour (0xff38bdf8);
            track     = juce::Colour (0xff28384d);
            badge     = juce::Colour (0xffffffff);
            metalHi   = juce::Colour (0xffffea80);
            metalMid  = juce::Colour (0xffffc400);
            metalDark = juce::Colour (0xff8c6d1f);
            mirrorTop = juce::Colour (0xff1a2b45);
            mirrorBot = juce::Colour (0xff080d16);
        }
        else if (accessMode == 2)   // ★ かんたん3ステップスタイル (選択中のテーマ配色・世界観デザインをそのまま活かす)
        {
            // テーマ本来のパレット(Palette::applyTheme)を100%維持し、上書きしない
        }
        else if (accessMode == 3)   // ● マット・具体表記スタイル (Zero-Glare Matte Sage & Slate)
        {
            bgTop     = juce::Colour (0xff1c2226);
            bgBot     = juce::Colour (0xff171c20);
            panel     = juce::Colour (0xff242c31);
            panel2    = juce::Colour (0xff2d373d);
            panelLn   = juce::Colour (0xff5c6d75);
            ink       = juce::Colour (0xffeceae2);
            inkSoft   = juce::Colour (0xffbec0b9);
            green     = juce::Colour (0xff7eb39b);
            blue      = juce::Colour (0xff80a6c2);
            yellow    = juce::Colour (0xffd1b582);
            yellowDk  = juce::Colour (0xffa88d5c);
            salmon    = juce::Colour (0xffc99090);
            ice       = juce::Colour (0xff86b3b8);
            track     = juce::Colour (0xff38444a);
            badge     = juce::Colour (0xffeceae2);
            metalHi   = juce::Colour (0xffcfd6d9);
            metalMid  = juce::Colour (0xff8b99a1);
            metalDark = juce::Colour (0xff4c585e);
            mirrorTop = juce::Colour (0xff324047);
            mirrorBot = juce::Colour (0xff1d2529);
        }
    }
}

//==============================================================================
// ============================================================================
//  文字の大きさを「実際に描いたときに」測るための覗き窓（設計書§4 文字の規律）
//  ------------------------------------------------------------------------
//  ★検査のときだけ有効にする作りにはしない。検査と本体で関数の中身が変わると、
//   同じ名前で別物が2つある状態（ODR違反）になって、嘘の結果が出る。
//   常に入れておき、on() が false のときは atomic の読み1回で終わる。
//   フォントを作るのは描画のときだけなので、音には一切関係しない。
struct FontProbe
{
    static std::atomic<bool>&    on()   { static std::atomic<bool> b { false }; return b; }
    static std::mutex&           mtx()  { static std::mutex m; return m; }
    static std::vector<float>&   seen() { static std::vector<float> v; return v; }
    static void note (float h)
    {
        if (! on().load (std::memory_order_relaxed)) return;
        const std::lock_guard<std::mutex> lock (mtx());
        seen().push_back (h);
    }
};

class GzzioLnF : public juce::LookAndFeel_V4
{
public:
    // v3.1 §4-0「押す物は36px以上・スライダーつまみ42px」。
    //  JUCE の既定は高さの半分ぶんしかないので、横スライダーのつまみが
    //  32pxの器で16pxになる。指でもマウスでも掴みにくいので、ここで広げる。
    //  縦横どちらの線形スライダーにも効く（回すツマミは自前描画なので無関係）。
    int getSliderThumbRadius (juce::Slider& s) override
    {
        if (s.isBar()) return juce::LookAndFeel_V4::getSliderThumbRadius (s);
        const int box = s.isHorizontal() ? s.getHeight() : s.getWidth();
        return juce::jlimit (7, 21, juce::roundToInt (box * 0.62f));
    }

    GzzioLnF()
    {
        // v1.6.1: unit gear silhouettes for the watch-movement knob faces.
        // Built once; every knob draws them scaled+rotated (two path fills).
        auto makeUnitGear = [] (int teeth, float hubR, float holeR)
        {
            juce::Path g2;
            const float rTip = 1.0f, rRoot = 0.80f;
            const float step = juce::MathConstants<float>::twoPi / (float) teeth;
            for (int t = 0; t < teeth; ++t)
            {
                const float a0 = (float) t * step;
                auto pt = [] (float r, float a) { return juce::Point<float> (r * std::sin (a), -r * std::cos (a)); };
                if (t == 0) g2.startNewSubPath (pt (rRoot, a0)); else g2.lineTo (pt (rRoot, a0));
                g2.lineTo (pt (rTip,  a0 + step * 0.22f));
                g2.lineTo (pt (rTip,  a0 + step * 0.50f));
                g2.lineTo (pt (rRoot, a0 + step * 0.72f));
            }
            g2.closeSubPath();
            g2.setUsingNonZeroWinding (false);
            g2.addEllipse (-hubR, -hubR, hubR * 2.0f, hubR * 2.0f);
            if (holeR > 0.0f)
                for (int h = 0; h < 3; ++h)
                {
                    const float ha = (float) h * juce::MathConstants<float>::twoPi / 3.0f + 0.5f;
                    const float hr = (0.80f + hubR) * 0.52f;
                    g2.addEllipse (hr * std::sin (ha) - holeR, -hr * std::cos (ha) - holeR,
                                   holeR * 2.0f, holeR * 2.0f);
                }
            return g2;
        };
        uGearBig   = makeUnitGear (12, 0.22f, 0.185f);
        uGearSmall = makeUnitGear (8,  0.26f, 0.0f);

        ++s_instances;
        refreshPaletteColours();
    }

    // v2.3.0 重要な修正(Cubase 13が終了できない問題):
    //   埋め込みフォントを static(s_kawaiiFace)に入れっぱなしにしていたため、
    //   参照がプラグインの寿命を超えて残り、DAW終了時のモジュール解放中
    //   (Windowsのローダーロック下)にフォントの解放処理が走って止まっていた。
    //   最後のLookAndFeelが消えた時点で必ず手放す。
    ~GzzioLnF() override
    {
        if (--s_instances <= 0)
        {
            s_kawaiiFace = nullptr;
            s_useKawaii  = false;
            s_knobStyle  = 0;
        }
    }


    // v1.9.0: 以前はコンストラクタで一度だけ色を入れていたため、テーマを
    //         切り替えてもポップアップ・ツールチップ・ボタンの色が前のまま
    //         残っていた。テーマ変更のたびにここを呼び直す。
    void refreshPaletteColours()
    {
        faceCache.clear();
        setColour (juce::Label::textColourId, Palette::ink);
        setColour (juce::Slider::textBoxTextColourId, Palette::ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TooltipWindow::backgroundColourId, Palette::panel2);
        setColour (juce::TooltipWindow::textColourId, Palette::ink);
        setColour (juce::TooltipWindow::outlineColourId, Palette::panelLn);
        setColour (juce::TextButton::buttonColourId, Palette::panel2);
        setColour (juce::TextButton::textColourOffId, Palette::ink);
        setColour (juce::TextButton::textColourOnId, Palette::readableOn (Palette::yellow));
        {
            // 明るいテーマ（V-Liver / ゆるふわ / 自然）では黒いコンボボックスを廃止し、
            // クチュール・コスメやカフェの世界観に調和するシルキーなファンデーション箱＋濃色エスプレッソ文字にする。
            const bool lightTheme = Palette::panel2.getPerceivedBrightness() > 0.55f;
            const juce::Colour comboBg  = lightTheme ? Palette::panel2.brighter (0.04f) : Palette::panel2;
            const juce::Colour comboTxt = lightTheme ? Palette::ink.darker (0.45f)      : Palette::readableOn (comboBg);
            setColour (juce::ComboBox::backgroundColourId, comboBg);
            setColour (juce::ComboBox::textColourId, comboTxt);
            setColour (juce::ComboBox::outlineColourId, Palette::panelLn);
            setColour (juce::ComboBox::arrowColourId, lightTheme ? Palette::yellowDk : comboTxt.withMultipliedAlpha (0.78f));
        }
        setColour (juce::PopupMenu::backgroundColourId, Palette::panel2);
        setColour (juce::PopupMenu::textColourId, Palette::ink);
        setColour (juce::PopupMenu::headerTextColourId, Palette::inkSoft);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::yellow);
        setColour (juce::PopupMenu::highlightedTextColourId, Palette::readableOn (Palette::yellow));
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                       juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.8f);
        const float rad = s_useKawaii ? r.getHeight() * 0.48f : 7.5f;
        const bool lightTheme = Palette::panel2.getPerceivedBrightness() > 0.55f;

        if (s_knobStyle >= 7)
        {
            // 🕰 歴代タイムマシン (v1.0.0〜v4.3.3): ジュエル装飾なしのフラット／クリーンなクラシックセレクター
            const float legacyRad = (s_knobStyle == 8 || s_knobStyle == 9) ? 5.0f : 6.0f;
            g.setColour (isButtonDown ? Palette::panel2.darker (0.06f) : Palette::panel2);
            g.fillRoundedRectangle (r, legacyRad);
            g.setColour (Palette::panelLn);
            g.drawRoundedRectangle (r, legacyRad, 1.2f);

            const float bx = r.getRight() - 14.0f;
            const float by = r.getCentreY();
            juce::Path chev;
            chev.startNewSubPath (bx - 4.0f, by - 1.8f);
            chev.lineTo          (bx,        by + 2.2f);
            chev.lineTo          (bx + 4.0f, by - 1.8f);
            g.setColour (Palette::inkSoft);
            g.strokePath (chev, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }

        if (s_useKawaii || lightTheme)
        {
            // ♥ クチュール・エナメル＆宝飾金具・セレクターカプセル（黒い箱を完全追放）
            g.setColour (Palette::panelLn.withAlpha (0.28f));
            g.fillRoundedRectangle (r.translated (0.0f, isButtonDown ? 0.5f : 2.0f), rad);

            const juce::Colour topC = isButtonDown ? Palette::panel2 : Palette::panel.brighter (0.04f);
            const juce::Colour botC = isButtonDown ? Palette::panel2.darker (0.08f) : Palette::panel2.interpolatedWith (Palette::panel, 0.35f);
            g.setGradientFill (juce::ColourGradient (topC, r.getX(), r.getY(), botC, r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, rad);

            // 上部のツヤ・ハイライト帯（リップグロス／クッションパクトの光沢）
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (lightTheme ? 0.78f : 0.22f), r.getX(), r.getY() + 1.0f,
                                                     juce::Colours::transparentWhite, r.getX(), r.getY() + r.getHeight() * 0.45f, false));
            g.fillRoundedRectangle (r.reduced (2.0f, 1.2f).withHeight (r.getHeight() * 0.44f), rad * 0.85f);

            // 二重のクチュール宝飾メタルフレーム（ロゼゴールド／プラチナシルバー／ノワールシルバー／シャンパン／24Kハニー）
            const juce::Colour goldRim = (s_knobStyle == 2) ? Palette::metalMid : Palette::panelLn.darker (0.12f);
            g.setColour (goldRim);
            g.drawRoundedRectangle (r, rad, 1.6f);
            g.setColour (lightTheme ? juce::Colours::white.withAlpha (0.82f) : Palette::metalHi.withAlpha (0.35f));
            g.drawRoundedRectangle (r.reduced (1.8f), juce::jmax (2.0f, rad - 1.8f), 1.0f);

            // 右端のジュエル・メダリオン＆シェブロン矢印
            const float badgeR = juce::jmin (11.0f, r.getHeight() * 0.32f);
            const float bx = r.getRight() - badgeR - 8.0f;
            const float by = r.getCentreY();
            g.setGradientFill (juce::ColourGradient (Palette::yellow.brighter (0.18f), bx - badgeR, by - badgeR,
                                                     Palette::yellowDk, bx + badgeR, by + badgeR, false));
            g.fillEllipse (bx - badgeR, by - badgeR, badgeR * 2.0f, badgeR * 2.0f);
            g.setColour (goldRim.brighter (0.25f));
            g.drawEllipse (bx - badgeR, by - badgeR, badgeR * 2.0f, badgeR * 2.0f, 1.2f);

            juce::Path chev;
            chev.startNewSubPath (bx - 4.0f, by - 1.6f);
            chev.lineTo          (bx,        by + 2.6f);
            chev.lineTo          (bx + 4.0f, by - 1.6f);
            g.setColour (juce::Colours::white.withAlpha (0.95f));
            g.strokePath (chev, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (s_knobStyle == 6)
        {
            // 🌌🌊🌅🍁👑 風景・季節コレクション: 背景の銀河・碧海・朝焼け・紅葉と初雪が透けるクリスタルガラス・カプセル
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillRoundedRectangle (r.translated (0.0f, 1.8f), rad);
            g.setGradientFill (juce::ColourGradient (Palette::panel2.withAlpha (0.52f), r.getX(), r.getY(),
                                                     Palette::panel.withAlpha  (0.62f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, rad);
            // 上半分の水滴・クリスタルガラス反射ハイライト
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.24f), r.getX(), r.getY() + 1.0f,
                                                     juce::Colours::transparentWhite,        r.getX(), r.getY() + r.getHeight() * 0.48f, false));
            g.fillRoundedRectangle (r.reduced (2.0f, 1.0f).withHeight (r.getHeight() * 0.45f), rad * 0.85f);
            g.setColour (Palette::metalMid.withAlpha (isButtonDown ? 0.92f : 0.68f));
            g.drawRoundedRectangle (r, rad, 1.3f);

            const float bx = r.getRight() - 15.0f;
            const float by = r.getCentreY();
            const float badgeR = juce::jmin (8.5f, r.getHeight() * 0.34f);
            g.setGradientFill (juce::ColourGradient (Palette::metalHi.withAlpha (0.40f), bx - badgeR, by - badgeR,
                                                     Palette::metalDark.withAlpha (0.55f), bx + badgeR, by + badgeR, false));
            g.fillEllipse (bx - badgeR, by - badgeR, badgeR * 2.0f, badgeR * 2.0f);
            g.setColour (Palette::metalHi.withAlpha (0.78f));
            g.drawEllipse (bx - badgeR, by - badgeR, badgeR * 2.0f, badgeR * 2.0f, 1.0f);

            juce::Path chev;
            chev.startNewSubPath (bx - 3.8f, by - 1.5f);
            chev.lineTo          (bx,        by + 2.4f);
            chev.lineTo          (bx + 3.8f, by - 1.5f);
            g.setColour (Palette::ink);
            g.strokePath (chev, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            // ダーク系（Argon / Studio / Neon）: 3Dリセスト・ハードウェアセレクター
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (r.translated (0.0f, 1.8f), rad);
            g.setGradientFill (juce::ColourGradient (Palette::panel2.brighter (0.08f), r.getX(), r.getY(),
                                                     Palette::panel.darker (0.18f),    r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, rad);
            g.setColour (Palette::panelLn.brighter (isButtonDown ? 0.25f : 0.05f));
            g.drawRoundedRectangle (r, rad, 1.3f);

            const float bx = r.getRight() - 16.0f;
            const float by = r.getCentreY();
            juce::Path chev;
            chev.startNewSubPath (bx - 4.5f, by - 2.0f);
            chev.lineTo          (bx,        by + 2.5f);
            chev.lineTo          (bx + 4.5f, by - 2.0f);
            g.setColour (box.findColour (juce::ComboBox::arrowColourId));
            g.strokePath (chev, juce::PathStrokeType (1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // v2.1.0: MIDI設定パネル(CallOutBox)もテーマに合わせる。JUCE既定の描画は
    // 配色スキーム固定なので、どのテーマでも同じダーク箱で開いて浮いてしまう。
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g,
                                   const juce::Path& path, juce::Image&) override
    {
        g.setColour (Palette::panel);
        g.fillPath (path);
        g.setColour (Palette::panelLn);
        g.strokePath (path, juce::PathStrokeType (1.4f));
    }

    void setFontScale (float s) { fontScale = juce::jmax (0.5f, s); }

    // v1.7.0: theme accent for knob arcs / pinion / jewel cap (per-theme mint)
    void setThemeArc (juce::Colour c) { themeArc = c; }

    // v1.7.0 yuru-kawa: swap in the embedded rounded font (Mochiy Pop One subset).
    // Static so uiFont() (which is static and used everywhere) can pick it up.
    static void setKawaiiTypeface (juce::Typeface::Ptr tf) { s_kawaiiFace = tf; }
    static void setUseKawaiiFont  (bool b)                 { s_useKawaii  = b; }
    static void setKnobStyle      (int style)              { s_knobStyle  = style; }
    static int  getKnobStyle      () noexcept              { return s_knobStyle; }
    static void setAccessMode     (int m) noexcept         { s_accessMode = juce::jlimit (0, 3, m); }
    static int  getAccessMode     () noexcept              { return s_accessMode; }
    static void bumpJuicePhase    (float d)                { s_juicePhase = std::fmod (s_juicePhase + d, 6283.1853f); }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        if (s_useKawaii && s_kawaiiFace != nullptr && (s_accessMode == 0 || s_accessMode == 2))
            return s_kawaiiFace;
        return juce::LookAndFeel_V4::getTypefaceForFont (f);
    }
    float getFontScale() const  { return fontScale; }

    // Prefer a clean, highly legible UI face. On Windows "Yu Gothic UI" / "Meiryo"
    // render Japanese crisply; fall back gracefully elsewhere.
    static juce::Font uiFont (float height, bool bold)
    {
        FontProbe::note (height);
        if (s_useKawaii && s_kawaiiFace != nullptr && (s_accessMode == 0 || s_accessMode == 2))
            return juce::Font (s_kawaiiFace).withHeight (height);  // rounded font (single weight)

        juce::FontOptions o;
       #if JUCE_WINDOWS
        o = juce::FontOptions ("Yu Gothic UI", height, bold ? juce::Font::bold : juce::Font::plain);
       #elif JUCE_MAC
        o = juce::FontOptions ("Hiragino Sans", height, bold ? juce::Font::bold : juce::Font::plain);
       #else
        o = juce::FontOptions().withHeight (height).withStyle (bold ? "Bold" : "Regular");
       #endif
        return juce::Font (o);
    }

    juce::Font getLabelFont (juce::Label& l) override
    {
        float base = (float) l.getProperties().getWithDefault ("fontH", 14.0);
        if (dynamic_cast<juce::ComboBox*> (l.getParentComponent()) != nullptr)
            base = 15.5f;
        bool bold = (bool) l.getProperties().getWithDefault ("bold", false);
        if (s_accessMode == 1)  // ◉ くっきり大文字モード: 全ラベルを太字＋大型化
        {
            base = juce::jmax (16.2f, base * 1.15f);
            bold = true;
        }
        else if (s_accessMode == 2 || s_accessMode == 3)
        {
            base = juce::jmax (15.0f, base * 1.06f);
            bold = true;
        }
        float wantH = base * fontScale;
        if (l.getHeight() >= 12)
            wantH = juce::jmin (wantH, juce::jmax (14.0f, (float) l.getHeight() * 0.84f));
        return uiFont (wantH, bold);
    }

    static constexpr float kFitMinPx = 12.0f;
    static int stringWidth (const juce::Font& f, const juce::String& t)
    {
        if (t.isEmpty()) return 0;
        struct WidthCacheEntry
        {
            juce::int64 hash { 0 };
            int hKey { 0 };
            int width { 0 };
        };
        static WidthCacheEntry s_cache[1024];
        const juce::int64 h64 = t.hashCode64();
        const int hKey = juce::roundToInt (f.getHeight() * 20.0f) ^ (f.getStyleFlags() << 16) ^ (s_useKawaii ? 0x400000 : 0);
        const size_t slot = (size_t) ((juce::uint64) (h64 ^ hKey) & 1023u);
        if (s_cache[slot].hash == h64 && s_cache[slot].hKey == hKey && s_cache[slot].width > 0)
            return s_cache[slot].width;
        const int need = juce::GlyphArrangement::getStringWidthInt (f, t);
        s_cache[slot] = { h64, hKey, need };
        return need;
    }

    static juce::Font fitFont (juce::Font f, const juce::String& t, int w)
    {
        if (t.isEmpty() || w <= 0) return f;
        const int need = stringWidth (f, t);
        if (need <= w) return f;
        const float shrunk = juce::jmax (kFitMinPx, f.getHeight() * (float) w / (float) need);
        FontProbe::note (shrunk);
        return f.withHeight (shrunk);
    }

    // ★ 各ツマミが「いま音をどう変えているか（効き目の強さ・方向・感覚ワード）」を判定する共通ヘルパー
    //    outIntensity: 0.0 (待機/変化なし) .. 1.0 (強く効いている)
    //    outDir      : -1 (不要な音を削る・抑える・整える) / +1 (輝き・厚み・空間・音量を足す) / 0 (変化なし)
    static void queryKnobEffect (const juce::Slider& sl, float pos, float& outIntensity, int& outDir,
                                 juce::String& outTagJa, juce::String& outTagEn)
    {
        outIntensity = 0.0f;
        outDir = 0;
        outTagJa.clear();
        outTagEn.clear();
        if (! sl.isEnabled()) return;

        const juce::String id = sl.getProperties().getWithDefault ("paramID", "").toString();
        const double val = sl.getValue();
        const float liveMeter = (float) sl.getProperties().getWithDefault ("liveMeter", 0.0);
        const bool literalMode = (s_accessMode == 3); // ● マット・具体表記モード: 比喩・絵文字を排し具体的・機能的な言葉で統一

        if (id == "denoise")
        {
            outIntensity = val > 1.0 ? juce::jlimit (0.22f, 1.0f, (float) val / 60.0f + liveMeter * 0.35f) : 0.0f;
            outDir = -1;
            outTagJa = juce::String::fromUTF8 (literalMode ? "背景ノイズ除去: 動作中" : "🧹 雑音カット");
            outTagEn = "DENOISE";
        }
        else if (id == "gate")
        {
            outIntensity = val > -75.0 ? juce::jlimit (0.22f, 1.0f, (float) (val + 80.0) / 50.0f) : 0.0f;
            outDir = -1;
            outTagJa = juce::String::fromUTF8 (literalMode ? "無音時ミュート: 動作中" : "🔇 無音遮断");
            outTagEn = "GATE";
        }
        else if (id == "lowcut")
        {
            outIntensity = val > 25.0 ? juce::jlimit (0.22f, 1.0f, (float) (val - 20.0) / 150.0f) : 0.0f;
            outDir = -1;
            outTagJa = juce::String::fromUTF8 (literalMode ? "超低域カット: 動作中" : "✂ 低音スッキリ");
            outTagEn = "LOW CUT";
        }
        else if (id == "mud")
        {
            outIntensity = std::abs (val) > 0.2 ? juce::jlimit (0.24f, 1.0f, (float) std::abs (val) / 6.0f) : 0.0f;
            outDir = (val < 0.0 ? -1 : 1);
            outTagJa = juce::String::fromUTF8 (literalMode ? (val < 0.0 ? "250Hz帯 減衰中" : "250Hz帯 増幅中")
                                                           : (val < 0.0 ? "💎 こもり解消" : "🔥 中低音UP"));
            outTagEn = val < 0.0 ? "DE-MUD" : "LOW-MID+";
        }
        else if (id == "harsh")
        {
            outIntensity = std::abs (val) > 0.2 ? juce::jlimit (0.22f, 1.0f, (float) std::abs (val) / 6.0f) : 0.0f;
            outDir = (val < 0.0 ? -1 : 1);
            outTagJa = juce::String::fromUTF8 (literalMode ? "3.5kHz帯 減衰中" : (val < 0.0 ? "🌿 耳ざわり減" : "✨ 高域強調"));
            outTagEn = "HARSH";
        }
        else if (id == "comp2" || id == "comp1")
        {
            outIntensity = val > 1.0 ? juce::jlimit (0.25f, 1.0f, (float) val / 70.0f + liveMeter * 0.35f) : 0.0f;
            outDir = -1;
            outTagJa = juce::String::fromUTF8 (literalMode ? "音量均一化: 動作中" : "⚖ 声量ピタッ");
            outTagEn = "LEVELING";
        }
        else if (id == "deess")
        {
            outIntensity = val > 1.0 ? juce::jlimit (0.24f, 1.0f, (float) val / 65.0f + liveMeter * 0.35f) : 0.0f;
            outDir = -1;
            outTagJa = juce::String::fromUTF8 (literalMode ? "歯擦音(サ行)抑制: 動作中" : "🛡 サ行おさえ");
            outTagEn = "DE-ESS";
        }
        else if (id == "presence")
        {
            outIntensity = std::abs (val) > 0.2 ? juce::jlimit (0.24f, 1.0f, (float) std::abs (val) / 6.0f) : 0.0f;
            outDir = (val >= 0.0 ? 1 : -1);
            outTagJa = juce::String::fromUTF8 (literalMode ? "中高域(明瞭度): 動作中" : (val >= 0.0 ? "✨ 輪郭くっきり" : "🌙 優しい輪郭"));
            outTagEn = "PRESENCE";
        }
        else if (id == "air")
        {
            outIntensity = std::abs (val) > 0.2 ? juce::jlimit (0.24f, 1.0f, (float) std::abs (val) / 10.0f) : 0.0f;
            outDir = (val >= 0.0 ? 1 : -1);
            outTagJa = juce::String::fromUTF8 (literalMode ? "10kHz高域: 動作中" : (val >= 0.0 ? "✨ 空気ツヤUP" : "🌙 高音落ち着き"));
            outTagEn = "AIR";
        }
        else if (id == "drive")
        {
            outIntensity = val > 1.0 ? juce::jlimit (0.22f, 1.0f, (float) val / 40.0f) : 0.0f;
            outDir = 1;
            outTagJa = juce::String::fromUTF8 (literalMode ? "倍音付加: 動作中" : "🔥 温かい厚み");
            outTagEn = "WARMTH";
        }
        else if (id == "revmix")
        {
            outIntensity = val > 1.0 ? juce::jlimit (0.25f, 1.0f, (float) val / 55.0f) : 0.0f;
            outDir = 1;
            outTagJa = juce::String::fromUTF8 (literalMode ? "残響ブレンド: 動作中" : "🏛 残響の余韻");
            outTagEn = "REVERB";
        }
        else if (id == "revsize")
        {
            outIntensity = val > 1.0 ? juce::jlimit (0.22f, 1.0f, (float) val / 85.0f) : 0.0f;
            outDir = 1;
            outTagJa = juce::String::fromUTF8 (literalMode ? "残響空間サイズ: 設定中" : "🌌 空間の広さ");
            outTagEn = "SPACE SIZE";
        }
        else if (id == "delay")
        {
            outIntensity = val > 1.0 ? juce::jlimit (0.25f, 1.0f, (float) val / 60.0f) : 0.0f;
            outDir = 1;
            outTagJa = juce::String::fromUTF8 (literalMode ? "反響ディレイ: 動作中" : "🔊 やまびこ");
            outTagEn = "ECHO";
        }
        else if (id == "width")
        {
            outIntensity = std::abs (val) > 1.0 ? juce::jlimit (0.25f, 1.0f, (float) std::abs (val) / 75.0f) : 0.0f;
            outDir = 1;
            outTagJa = juce::String::fromUTF8 (literalMode ? "左右ステレオ幅: 動作中" : "↔ 左右パノラマ");
            outTagEn = "STEREO";
        }
        else if (id == "in_gain")
        {
            outIntensity = std::abs (val) > 0.2 ? juce::jlimit (0.22f, 1.0f, (float) std::abs (val) / 12.0f) : 0.0f;
            outDir = (val >= 0.0 ? 1 : -1);
            outTagJa = juce::String::fromUTF8 (literalMode ? "入力ゲイン調整中" : (val >= 0.0 ? "🎤 入力増幅" : "🎤 入力減衰"));
            outTagEn = "IN GAIN";
        }
        else if (id == "makeup")
        {
            outIntensity = std::abs (val) > 0.2 ? juce::jlimit (0.25f, 1.0f, (float) std::abs (val) / 8.0f) : 0.0f;
            outDir = (val >= 0.0 ? 1 : -1);
            outTagJa = juce::String::fromUTF8 (literalMode ? "出力ゲイン調整中" : (val >= 0.0 ? "🔊 音量UP中" : "🔉 音量抑制"));
            outTagEn = "OUT GAIN";
        }
        else if (id == "hum")
        {
            outIntensity = val > 1.0 ? juce::jlimit (0.22f, 1.0f, (float) val / 50.0f) : 0.0f;
            outDir = -1;
            outTagJa = juce::String::fromUTF8 (literalMode ? "電源ハム除去: 動作中" : "⚡ ジー音除去");
            outTagEn = "DE-HUM";
        }
        else if (id == "attack" || id == "release" || id == "mix")
        {
            outIntensity = 0.28f;
            outDir = 0;
        }
        else
        {
            outIntensity = std::abs (val) > 0.5 ? juce::jlimit (0.20f, 1.0f, pos) : 0.0f;
            outDir = 1;
            outTagJa = juce::String::fromUTF8 (literalMode ? "動作中" : "✦ 効き目ON");
            outTagEn = "ACTIVE";
        }
    }

    void drawLabel (juce::Graphics& g, juce::Label& l) override
    {
        if (l.isBeingEdited()) return;
        const auto area = l.getBorderSize().subtractedFrom (l.getLocalBounds());
        int leftLedPad = 0;

        // ★ ロータリーノブの数値ボックス描画（標準＝OLEDカプセル / ◉くっきり＝超高コントラスト＋形状記号 / ★3ステップ＝待機減光 / ●マット＝無発光箱）
        //    ※ s_knobStyle >= 7 (v1.0.0〜v4.3.3 歴代モード) は当時のUI通りカプセル枠なしのプレーンテキスト数値表示にする
        if (auto* parentSlider = dynamic_cast<juce::Slider*> (l.getParentComponent()))
        {
            if (s_knobStyle != 5 && s_knobStyle < 7 && area.getWidth() >= 36 && area.getHeight() >= 14)
            {
                const auto rf = l.getLocalBounds().toFloat().reduced (1.5f, 0.8f);
                const float rad = (s_accessMode == 1 || s_accessMode == 3) ? 6.0f : (s_useKawaii ? rf.getHeight() * 0.48f : 5.5f);
                const bool lightPanel = Palette::panel.getPerceivedBrightness() > 0.55f;
                const int group = (int) parentSlider->getProperties().getWithDefault ("signalGroup", 2);
                const juce::Colour acc = ! parentSlider->isEnabled() ? Palette::inkSoft
                                       : (group == 0 ? Palette::green : group == 1 ? Palette::blue : themeArc);
                const float prop = (float) juce::jlimit (0.0, 1.0, parentSlider->valueToProportionOfLength (parentSlider->getValue()));

                float effInt = 0.0f; int effDir = 0; juce::String tagJa, tagEn;
                queryKnobEffect (*parentSlider, prop, effInt, effDir, tagJa, tagEn);
                const bool isWorking = (effInt > 0.02f);
                const juce::Colour effCol = (effDir < 0) ? Palette::green.interpolatedWith (Palette::ice, 0.38f)
                                          : (effDir > 0) ? Palette::yellow.interpolatedWith (Palette::salmon, 0.35f)
                                                         : acc;

                if (s_accessMode == 1)  // ◉ くっきり大文字モード: 漆黒ソリッド下地＋太枠＋形状記号 (▼削る / ▲足す / ○待機)
                {
                    g.setColour (juce::Colour (0xff04070b));
                    g.fillRoundedRectangle (rf, rad);
                    g.setColour (isWorking ? (effDir < 0 ? Palette::green : Palette::yellow) : Palette::panelLn.withAlpha (0.75f));
                    g.drawRoundedRectangle (rf.reduced (0.5f), rad, isWorking ? 2.2f : 1.4f);

                    const bool hasSym = (rf.getWidth() >= 68.0f);
                    if (hasSym)
                    {
                        const float sx = rf.getX() + 9.0f;
                        const float sy = rf.getCentreY();
                        if (isWorking)
                        {
                            juce::Path sym;
                            if (effDir < 0) // ▼ 削る・抑える
                            {
                                sym.addTriangle (sx - 4.5f, sy - 3.5f, sx + 4.5f, sy - 3.5f, sx, sy + 4.5f);
                                g.setColour (Palette::green);
                            }
                            else            // ▲ 足す・広げる
                            {
                                sym.addTriangle (sx - 4.5f, sy + 4.0f, sx + 4.5f, sy + 4.0f, sx, sy - 4.2f);
                                g.setColour (Palette::yellow);
                            }
                            g.fillPath (sym);
                        }
                        else
                        {
                            g.setColour (Palette::panelLn.withAlpha (0.65f));
                            g.drawEllipse (sx - 3.2f, sy - 3.2f, 6.4f, 6.4f, 1.5f);
                        }
                    }

                    const auto textRect = area.withTrimmedLeft (hasSym ? 15 : 2).withTrimmedRight (2);
                    const float maxH = juce::jmin (rf.getHeight() * 0.80f, 15.5f * fontScale);
                    const auto f = fitFont (uiFont (maxH, true), l.getText(), textRect.getWidth());
                    g.setFont (f);
                    g.setColour (isWorking ? juce::Colours::white : Palette::inkSoft);
                    g.drawFittedText (l.getText(), textRect, juce::Justification::centred, 1, 0.85f);
                    return;
                }
                else if (s_accessMode == 3) // ● マット・具体表記モード: 発光・反射・グラデーション完全ゼロの静穏マットカプセル
                {
                    g.setColour (Palette::bgBot);
                    g.fillRoundedRectangle (rf, 5.0f);
                    g.setColour (isWorking ? acc : Palette::panelLn.withAlpha (0.65f));
                    g.drawRoundedRectangle (rf.reduced (0.5f), 5.0f, isWorking ? 1.6f : 1.0f);
                    const bool hasSq = (isWorking && rf.getWidth() >= 68.0f);
                    if (hasSq)
                    {
                        g.setColour (acc);
                        g.fillRoundedRectangle (rf.getX() + 5.5f, rf.getCentreY() - 3.0f, 6.0f, 6.0f, 1.5f);
                    }
                    const auto textRect = area.withTrimmedLeft (hasSq ? 12 : 2).withTrimmedRight (2);
                    const auto f = fitFont (getLabelFont (l), l.getText(), textRect.getWidth());
                    g.setFont (f);
                    g.setColour (isWorking ? Palette::ink : Palette::inkSoft.withAlpha (0.80f));
                    g.drawFittedText (l.getText(), textRect, juce::Justification::centred, 1, 0.85f);
                    return;
                }

                // 標準 または ★ かんたん3ステップスタイル (3ステップ時は待機カプセルを減光して作動中だけに視線を集中させる)
                const float stepDim = (s_accessMode == 2 && ! isWorking) ? 0.38f : 1.0f;

                // 0. 効いているツマミのOLEDカプセル背面オーラ
                if (isWorking)
                {
                    g.setColour (effCol.withAlpha (0.18f + 0.18f * effInt));
                    g.fillRoundedRectangle (rf.expanded (1.6f, 1.2f), rad + 1.2f);
                }

                // 1. ディスプレイ深層キャビティ背景
                if (lightPanel)
                {
                    g.setGradientFill (juce::ColourGradient (Palette::panel2.darker (0.04f).withAlpha (0.90f * stepDim), rf.getX(), rf.getY(),
                                                             Palette::panel.brighter (0.04f).withAlpha (0.94f * stepDim), rf.getX(), rf.getBottom(), false));
                }
                else
                {
                    g.setGradientFill (juce::ColourGradient (Palette::bgTop.darker (isWorking ? 0.38f : 0.55f).withAlpha (0.90f * stepDim), rf.getX(), rf.getY(),
                                                             Palette::panel2.darker (isWorking ? 0.16f : 0.32f).withAlpha (0.92f * stepDim), rf.getX(), rf.getBottom(), false));
                }
                g.fillRoundedRectangle (rf, rad);

                // 2. 上部ガラス反射マイクロハイライト
                g.setColour (juce::Colours::white.withAlpha ((lightPanel ? 0.55f : (isWorking ? 0.22f : 0.08f)) * stepDim));
                g.drawHorizontalLine (juce::roundToInt (rf.getY() + 1.0f), rf.getX() + rad, rf.getRight() - rad);

                // 3. 下辺のライブ・オプティカル進捗ライトパイプバー (効いている時のみ鮮やかに発光)
                const float barMaxW = juce::jmax (8.0f, rf.getWidth() - 10.0f);
                const float barX    = rf.getX() + 5.0f;
                const float barY    = rf.getBottom() - 2.6f;
                g.setColour (Palette::track.withAlpha (0.45f * stepDim));
                g.fillRoundedRectangle (barX, barY, barMaxW, 1.6f, 0.8f);
                if (isWorking && prop > 0.01f)
                {
                    const float fillW = juce::jmax (4.0f, barMaxW * prop);
                    g.setGradientFill (juce::ColourGradient (acc.withAlpha (0.90f), barX, barY,
                                                             effCol.brighter (0.25f), barX + fillW, barY, false));
                    g.fillRoundedRectangle (barX, barY, fillW, 2.0f, 1.0f);
                }

                // 4. 左端の「稼働中ジュエルLEDドット (●)」(幅に余裕がある総合画面カプセルで点灯)
                if (isWorking && rf.getWidth() >= 68.0f)
                {
                    const float lx = rf.getX() + 6.8f;
                    const float ly = rf.getCentreY() - 0.5f;
                    g.setColour (effCol.withAlpha (0.45f));
                    g.fillEllipse (lx - 3.6f, ly - 3.6f, 7.2f, 7.2f);
                    g.setColour (effCol.brighter (0.25f));
                    g.fillEllipse (lx - 2.2f, ly - 2.2f, 4.4f, 4.4f);
                    g.setColour (juce::Colours::white);
                    g.fillEllipse (lx - 0.9f, ly - 0.9f, 1.8f, 1.8f);
                    leftLedPad = 11;
                }

                // 5. 精密ベゼルリム（効いているツマミは発光リム、待機中ツマミは静かなダークリム）
                g.setColour (isWorking ? effCol.withAlpha (0.85f)
                           : s_knobStyle == 6 ? Palette::metalMid.withAlpha (0.32f * stepDim)
                                              : Palette::panelLn.withAlpha (0.38f * stepDim));
                g.drawRoundedRectangle (rf.reduced (0.4f), rad, isWorking ? 1.35f : 0.85f);
            }
        }

        const auto textArea = (leftLedPad > 0) ? area.withTrimmedLeft (leftLedPad).withTrimmedRight (2) : area;
        const auto f = fitFont (getLabelFont (l), l.getText(), textArea.getWidth());
        juce::Colour textCol = l.findColour (juce::Label::textColourId);
        if (auto* parentCombo = dynamic_cast<juce::ComboBox*> (l.getParentComponent()))
            textCol = parentCombo->findColour (juce::ComboBox::textColourId);
        float textAlpha = l.isEnabled() ? 1.0f : 0.5f;
        if (s_accessMode == 2)
        {
            if (auto* parentSlider = dynamic_cast<juce::Slider*> (l.getParentComponent()))
            {
                float effInt = 0.0f; int effDir = 0; juce::String tj, te;
                const float prop = (float) juce::jlimit (0.0, 1.0, parentSlider->valueToProportionOfLength (parentSlider->getValue()));
                queryKnobEffect (*parentSlider, prop, effInt, effDir, tj, te);
                if (effInt <= 0.02f) textAlpha *= 0.42f;
            }
        }
        g.setColour (textCol.withMultipliedAlpha (textAlpha));
        g.setFont (f);
        const int maxLines = juce::jmax (1, (int) ((float) textArea.getHeight() / f.getHeight()));
        if (maxLines <= 1)
            g.drawText (l.getText(), textArea.withTrimmedBottom (1), l.getJustificationType(), false);
        else
            g.drawFittedText (l.getText(), textArea.withTrimmedBottom (1), l.getJustificationType(),
                              maxLines, l.getMinimumHorizontalScale());
    }

    juce::Font getTextButtonFont (juce::TextButton& b, int h) override
    {
        const float want = (float) b.getProperties().getWithDefault ("fontH", 0.0);
        const float boost = (s_accessMode == 1) ? 1.12f : 1.0f;
        const float baseH = (want > 0.0f) ? (want * fontScale * boost)
                                          : ((s_accessMode == 1 ? 15.5f : 14.5f) * fontScale);
        const float maxH = (h > 0) ? juce::jmax (14.0f, (float) h * 0.72f) : baseH;
        return uiFont (juce::jmin (maxH, baseH), true);
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        const float wantH = (s_accessMode == 1 ? 16.0f : 14.0f) * fontScale;
        const float maxH  = (box.getHeight() > 0) ? juce::jmax (14.0f, (float) box.getHeight() * 0.72f) : wantH;
        const auto f = uiFont (juce::jmin (maxH, wantH), s_accessMode == 1);
        const auto txt = box.getText().isNotEmpty() ? box.getText() : box.getTextWhenNothingSelected();
        if (box.getWidth() > 40 && txt.isNotEmpty())
            return fitFont (f, txt, box.getWidth() - 36);
        return f;
    }

    juce::Font getPopupMenuFont() override
    {
        return uiFont ((s_accessMode == 1 ? 16.5f : 15.0f) * fontScale, s_accessMode == 1);
    }

private:
    // ---- v1.6.1 watch-movement knob internals -------------------------------
    juce::Path uGearBig, uGearSmall;        // unit-radius gear silhouettes
    std::map<int, juce::Image> faceCache;   // static dial plate, rendered once per size

    const juce::Image& getMovementFace (int px)
    {
        auto it = faceCache.find (px);
        if (it != faceCache.end())
            return it->second;

        juce::Image img (juce::Image::ARGB, juce::jmax (8, px), juce::jmax (8, px), true);
        juce::Graphics fg (img);
        const float P = (float) juce::jmax (8, px);
        const float R = P * 0.5f;

        // steel dial plate with a soft top-left sheen
        {
            juce::ColourGradient gr (Palette::panel2.brighter (0.22f), R * 0.62f, R * 0.42f,
                                     Palette::panel.darker (0.30f),   R * 1.55f, R * 1.65f, true);
            fg.setGradientFill (gr);
            fg.fillEllipse (0.0f, 0.0f, P, P);
        }
        // circular brushing: three faint rings
        fg.setColour (Palette::ink.withAlpha (0.05f));
        for (float rr : { 0.34f, 0.52f, 0.70f })
            fg.drawEllipse (R - R * rr, R - R * rr, R * rr * 2.0f, R * rr * 2.0f, 1.0f);
        // bridge plate (upper) with two screws
        fg.setColour (Palette::panel.darker (0.42f));
        fg.fillRoundedRectangle (P * 0.14f, P * 0.12f, P * 0.72f, P * 0.24f, P * 0.10f);
        fg.setColour (Palette::panelLn);
        fg.drawRoundedRectangle (P * 0.14f, P * 0.12f, P * 0.72f, P * 0.24f, P * 0.10f, 1.0f);
        fg.setColour (Palette::ink.withAlpha (0.55f));
        fg.fillEllipse (P * 0.205f, P * 0.195f, P * 0.055f, P * 0.055f);
        fg.fillEllipse (P * 0.740f, P * 0.195f, P * 0.055f, P * 0.055f);
        // ruby bearings peeking out from under the gears
        auto ruby = [&fg, P] (float cx, float cy, float r)
        {
            fg.setColour (Palette::salmon.darker (0.45f));
            fg.fillEllipse (P * cx - r - 1.2f, P * cy - r - 1.2f, (r + 1.2f) * 2.0f, (r + 1.2f) * 2.0f);
            fg.setColour (Palette::salmon);
            fg.fillEllipse (P * cx - r, P * cy - r, r * 2.0f, r * 2.0f);
        };
        ruby (0.26f, 0.72f, P * 0.045f);
        ruby (0.77f, 0.64f, P * 0.038f);
        // polished brass rim (the metal bezel of the movement)
        fg.setColour (Palette::yellowDk.withAlpha (0.95f));
        fg.drawEllipse (P * 0.025f, P * 0.025f, P * 0.95f, P * 0.95f, juce::jmax (1.6f, P * 0.05f));
        fg.setColour (Palette::yellow.withAlpha (0.35f));
        fg.drawEllipse (P * 0.10f, P * 0.10f, P * 0.80f, P * 0.80f, 1.0f);

        return faceCache.emplace (juce::jmax (8, px), std::move (img)).first->second;
    }

public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float pos, float startAngle, float endAngle, juce::Slider& sl) override
    {
        if (s_knobStyle == 1 && (s_accessMode == 0 || s_accessMode == 2))   // yuru-kawa: draw a juice glass (fill = value)
        {
            drawJuiceKnob (g, juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height),
                           pos, (int) sl.getProperties().getWithDefault ("juice", 0),
                           (bool) sl.getProperties().getWithDefault ("wideGlass", false));
            return;
        }
        const bool isHero = (bool) sl.getProperties().getWithDefault ("heroKnob", false);
        const float dialInset = (s_accessMode == 1) ? 12.2f : (isHero ? 11.2f : 9.4f);
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (dialInset);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto  c      = bounds.getCentre();
        const float angle  = startAngle + pos * (endAngle - startAngle);
        const float ring   = juce::jmax (isHero ? 4.8f : 3.4f, radius * (isHero ? 0.082f : 0.068f));
        const int group = (int) sl.getProperties().getWithDefault ("signalGroup", 2);
        const auto groupAccent = group == 0 ? Palette::green : group == 1 ? Palette::blue : themeArc;

        float effInt = 0.0f;
        int   effDir = 1;
        juce::String effTagJa, effTagEn;
        queryKnobEffect (sl, pos, effInt, effDir, effTagJa, effTagEn);
        const bool isWorking = sl.isEnabled() && (effInt > 0.02f);
        const juce::Colour dirTint = (effDir < 0) ? Palette::ice : Palette::yellow;
        const auto accent = ! sl.isEnabled() ? Palette::inkSoft
                          : isWorking        ? groupAccent.interpolatedWith (dirTint, 0.24f * effInt).brighter (0.14f * effInt)
                                             : groupAccent.interpolatedWith (Palette::inkSoft, 0.52f).withAlpha (0.58f);

        // =========================================================================
        // ★ 表示スタイル専用ロータリーダイヤル描画 (s_accessMode == 1, 3)
        //    (s_accessMode == 2「★かんたん3ステップ」は選択中テーマ本来のデザイン描画へ進む)
        // =========================================================================
        if (s_accessMode == 1)  // ◉ くっきり大文字スタイル (WCAG AAA 12:1・極太8目盛り・巨大白金ポインター針)
        {
            const float thickRing = ring * 1.38f;
            const juce::Colour hiCol = isWorking ? (effDir < 0 ? Palette::green : (group == 1 ? Palette::blue : Palette::yellow))
                                                 : Palette::panelLn.withAlpha (0.55f);

            // 1. 外周8分割の極太・高コントラスト目盛り（32本の細かい目盛りを廃止し、一目で位置がわかる大目盛り）
            for (int tick = 0; tick <= 8; ++tick)
            {
                const float tNorm = (float) tick / 8.0f;
                const float a = startAngle + (endAngle - startAngle) * tNorm;
                const bool major = (tick == 0 || tick == 4 || tick == 8);
                const bool passed = isWorking && (tNorm <= pos + 0.01f);
                const float rIn  = radius + 4.2f;
                const float rOut = radius + (major ? 12.0f : 8.5f);
                g.setColour (passed ? Palette::yellow : (major ? juce::Colours::white.withAlpha (0.90f) : Palette::panelLn.withAlpha (0.75f)));
                g.drawLine (c.x + rIn * std::sin (a), c.y - rIn * std::cos (a),
                            c.x + rOut * std::sin (a), c.y - rOut * std::cos (a),
                            major ? 3.4f : 2.2f);
            }

            // 2. 極太トラック溝＆高輝度ソリッド値アーク
            juce::Path track;
            track.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
            g.setColour (juce::Colour (0xff03060a));
            g.strokePath (track, juce::PathStrokeType (thickRing + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (Palette::track);
            g.strokePath (track, juce::PathStrokeType (thickRing, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (isWorking)
            {
                juce::Path valArc;
                valArc.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, angle, true);
                g.setColour (hiCol);
                g.strokePath (valArc, juce::PathStrokeType (thickRing, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // 3. 漆黒マット文字盤（内部の歯車や反射グロスを完全排除し、針の視認性を極限まで高める）
            const float kr = radius - thickRing * 0.75f - 3.5f;
            if (kr >= 6.0f)
            {
                g.setColour (juce::Colour (0xff060a10));
                g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
                g.setColour (isWorking ? hiCol : Palette::panelLn);
                g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, isHero ? 2.6f : 2.0f);

                // 4. 超大型・極太5.2px 純白＆黄金くさびポインター針（先端に大きな三角マーカー付き）
                const float pIn  = kr * 0.14f;
                const float pOut = kr * 0.88f;
                const float sinA = std::sin (angle), cosA = std::cos (angle);
                g.setColour (juce::Colours::black.withAlpha (0.85f));
                g.drawLine (c.x + pIn * sinA, c.y - pIn * cosA, c.x + pOut * sinA, c.y - pOut * cosA, isHero ? 7.5f : 6.2f);
                g.setColour (isWorking ? juce::Colours::white : Palette::inkSoft);
                g.drawLine (c.x + pIn * sinA, c.y - pIn * cosA, c.x + pOut * sinA, c.y - pOut * cosA, isHero ? 5.0f : 4.2f);

                // 先端の黄金デルタマーカー
                const float tipX = c.x + (kr * 0.90f) * sinA;
                const float tipY = c.y - (kr * 0.90f) * cosA;
                g.setColour (isWorking ? Palette::yellow : Palette::inkSoft);
                g.fillEllipse (tipX - 4.5f, tipY - 4.5f, 9.0f, 9.0f);
                g.setColour (juce::Colours::white);
                g.fillEllipse (c.x - 4.2f, c.y - 4.2f, 8.4f, 8.4f);
            }
            return;
        }
        else if (s_accessMode == 3) // ● マット・具体表記スタイル (発光ゼロ・反射ゼロ・グラデーションゼロの完全マット設計)
        {
            for (int tick = 0; tick <= 8; ++tick)
            {
                const float tNorm = (float) tick / 8.0f;
                const float a = startAngle + (endAngle - startAngle) * tNorm;
                const bool major = (tick % 2 == 0);
                const float rIn  = radius + 3.5f;
                const float rOut = radius + (major ? 8.0f : 5.5f);
                g.setColour (Palette::inkSoft.withAlpha (major ? 0.55f : 0.30f));
                g.drawLine (c.x + rIn * std::sin (a), c.y - rIn * std::cos (a),
                            c.x + rOut * std::sin (a), c.y - rOut * std::cos (a), major ? 1.8f : 1.2f);
            }
            juce::Path track;
            track.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
            g.setColour (Palette::track);
            g.strokePath (track, juce::PathStrokeType (ring * 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (isWorking)
            {
                juce::Path valArc;
                valArc.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, angle, true);
                g.setColour (groupAccent);
                g.strokePath (valArc, juce::PathStrokeType (ring * 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            const float kr = radius - ring - 3.2f;
            if (kr >= 6.0f)
            {
                g.setColour (Palette::panel2);
                g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
                g.setColour (isWorking ? groupAccent : Palette::panelLn);
                g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.6f);

                const float sinA = std::sin (angle), cosA = std::cos (angle);
                g.setColour (isWorking ? Palette::ink : Palette::inkSoft);
                g.drawLine (c.x + kr * 0.22f * sinA, c.y - kr * 0.22f * cosA,
                            c.x + kr * 0.84f * sinA, c.y - kr * 0.84f * cosA, 3.2f);
                g.setColour (isWorking ? groupAccent : Palette::panelLn);
                g.fillEllipse (c.x - 3.2f, c.y - 3.2f, 6.4f, 6.4f);
            }
            return;
        }

        // ⚡ 軽量モード (s_knobStyle == 5): グラデーション・影・画像転写を一切使わない超高速フラットベクター描画
        if (s_knobStyle == 5)
        {
            juce::Path track;
            track.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
            g.setColour (Palette::track);
            g.strokePath (track, juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path value;
            value.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, angle, true);
            g.setColour (accent);
            g.strokePath (value, juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const float kr = radius - ring - 3.0f;
            if (kr >= 5.0f)
            {
                g.setColour (Palette::panel2);
                g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
                g.setColour (Palette::panelLn);
                g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.2f);
                g.setColour (accent);
                g.drawLine (c.x + kr * 0.35f * std::sin (angle), c.y - kr * 0.35f * std::cos (angle),
                            c.x + kr * 0.84f * std::sin (angle), c.y - kr * 0.84f * std::cos (angle),
                            juce::jmax (2.4f, kr * 0.06f));
            }
            return;
        }

        // 🕰 歴代タイムマシン専用ノブ描画 (s_knobStyle == 7..10)
        if (s_knobStyle >= 7 && s_knobStyle <= 10)
        {
            if (s_knobStyle == 8) // v1.0.0: クリーム背景フラット・リングノブ (外周オリーブ黄アーク＋紺枠サークル＋紺ポインター)
            {
                const float r8 = juce::jmax (4.8f, radius * 0.16f);
                juce::Path trk;
                trk.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
                g.setColour (juce::Colour (0xffe0dbd0));
                g.strokePath (trk, juce::PathStrokeType (r8, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                juce::Path val;
                val.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, angle, true);
                g.setColour (juce::Colour (0xffd4c83a));
                g.strokePath (val, juce::PathStrokeType (r8, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                const float kr = radius - r8 * 0.88f;
                if (kr >= 5.0f)
                {
                    g.setColour (juce::Colour (0xffece8dc));
                    g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
                    g.setColour (juce::Colour (0xff2a4158));
                    g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 2.0f);
                    g.drawLine (c.x + kr * 0.20f * std::sin (angle), c.y - kr * 0.20f * std::cos (angle),
                                c.x + kr * 0.82f * std::sin (angle), c.y - kr * 0.82f * std::cos (angle), 2.4f);
                }
                return;
            }
            if (s_knobStyle == 9) // v1.3.0: ダークネイビー・フラットサークルノブ (外周アンバー黄アーク＋濃紺円盤＋白ポインター)
            {
                const float r9 = juce::jmax (4.4f, radius * 0.15f);
                juce::Path trk;
                trk.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
                g.setColour (juce::Colour (0xff262d3d));
                g.strokePath (trk, juce::PathStrokeType (r9, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                juce::Path val;
                val.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, angle, true);
                g.setColour (juce::Colour (0xfff0c445));
                g.strokePath (val, juce::PathStrokeType (r9, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                const float kr = radius - r9 * 0.88f;
                if (kr >= 5.0f)
                {
                    g.setColour (juce::Colour (0xff1e2430));
                    g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
                    g.setColour (juce::Colour (0xff3a4356));
                    g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.5f);
                    g.setColour (juce::Colour (0xfff0f4f8));
                    g.drawLine (c.x + kr * 0.20f * std::sin (angle), c.y - kr * 0.20f * std::cos (angle),
                                c.x + kr * 0.82f * std::sin (angle), c.y - kr * 0.82f * std::cos (angle), 2.3f);
                }
                return;
            }
            if (s_knobStyle == 7) // v1.7.0〜v3.1.0: 機械式デュアルギア・ムーブメントノブ (金の大歯車＋ミントの小歯車＋ルビー軸受＋白針)
            {
                const float r7 = juce::jmax (4.2f, radius * 0.135f);
                juce::Path trk;
                trk.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
                g.setColour (Palette::track);
                g.strokePath (trk, juce::PathStrokeType (r7, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                juce::Path val;
                val.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, angle, true);
                g.setColour (Palette::green);
                g.strokePath (val, juce::PathStrokeType (r7, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                const float kr = radius - r7 - 2.2f;
                if (kr >= 5.0f)
                {
                    const int dia = juce::jmax (10, juce::roundToInt (kr * 2.0f));
                    g.setColour (juce::Colours::black.withAlpha (0.35f));
                    g.fillEllipse (c.x - kr, c.y - kr + 2.2f, kr * 2.0f, kr * 2.0f);
                    g.drawImage (getMovementFace (dia),
                                 juce::Rectangle<float> (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f));

                    // 金色の大歯車 (12歯) ＆ ミントの小歯車 (8歯)
                    g.setColour (juce::Colour (0xffe5c868).withAlpha (0.88f));
                    g.fillPath (uGearBig, juce::AffineTransform::rotation (angle * 0.8f)
                                              .scaled (kr * 0.52f, kr * 0.52f)
                                              .translated (c.x - kr * 0.12f, c.y + kr * 0.10f));
                    g.setColour (juce::Colour (0xff7be0c0).withAlpha (0.85f));
                    g.fillPath (uGearSmall, juce::AffineTransform::rotation (-angle * 1.4f)
                                                .scaled (kr * 0.34f, kr * 0.34f)
                                                .translated (c.x + kr * 0.28f, c.y - kr * 0.12f));
                    // ルビー軸受
                    g.setColour (juce::Colour (0xfff0788c));
                    g.fillEllipse (c.x - kr * 0.12f - 2.4f, c.y + kr * 0.10f - 2.4f, 4.8f, 4.8f);
                    // 白ポインター針
                    g.setColour (juce::Colours::white);
                    g.drawLine (c.x + kr * 0.18f * std::sin (angle), c.y - kr * 0.18f * std::cos (angle),
                                c.x + kr * 0.86f * std::sin (angle), c.y - kr * 0.86f * std::cos (angle),
                                juce::jmax (2.4f, kr * 0.08f));
                }
                return;
            }
            if (s_knobStyle == 10) // v4.0.0〜v4.3.3: 3ステップ・クラシックスタジオノブ (ミント/ラベンダー/ライムのアーク＋ダークスタジオキャップ)
            {
                const float r10 = juce::jmax (4.2f, radius * 0.13f);
                juce::Path trk;
                trk.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
                g.setColour (juce::Colour (0xff242e34));
                g.strokePath (trk, juce::PathStrokeType (r10, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                juce::Path val;
                val.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, angle, true);
                g.setColour (groupAccent);
                g.strokePath (val, juce::PathStrokeType (r10, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                const float kr = radius - r10 - 2.8f;
                if (kr >= 5.0f)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.42f));
                    g.fillEllipse (c.x - kr, c.y - kr + 2.4f, kr * 2.0f, kr * 2.0f);
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222c32), c.x - kr * 0.4f, c.y - kr * 0.5f,
                                                             juce::Colour (0xff12181c), c.x + kr * 0.5f, c.y + kr * 0.6f, false));
                    g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
                    g.setColour (juce::Colour (0xff313f47));
                    g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.4f);
                    // センターリング
                    g.setColour (juce::Colour (0xff29353c));
                    g.drawEllipse (c.x - kr * 0.42f, c.y - kr * 0.42f, kr * 0.84f, kr * 0.84f, 1.0f);
                    // ポインター針
                    g.setColour (groupAccent.brighter (0.15f));
                    g.drawLine (c.x + kr * 0.25f * std::sin (angle), c.y - kr * 0.25f * std::cos (angle),
                                c.x + kr * 0.84f * std::sin (angle), c.y - kr * 0.84f * std::cos (angle),
                                juce::jmax (2.5f, kr * 0.08f));
                }
                return;
            }
        }

        // ★ アクティブ音響コロナ (Active Sonic Corona):
        //   ツマミが効いている時だけ、効き具合 (effInt) に比例して外周に多層光輪ハローと
        //   音の方向性リング (カット・整音＝氷晶シアン / ブースト・空間・艶＝琥珀ゴールド) が浮かび上がる！
        if (isWorking)
        {
            const float coronaR = radius + (isHero ? 10.0f : 8.0f);
            const juce::Colour glowCol = groupAccent.interpolatedWith (dirTint, 0.38f);
            g.setGradientFill (juce::ColourGradient (glowCol.withAlpha ((0.16f + 0.22f * effInt) * (isHero ? 1.1f : 1.0f)), c.x, c.y,
                                                     glowCol.withAlpha (0.0f), c.x + coronaR, c.y, true));
            g.fillEllipse (c.x - coronaR, c.y - coronaR, coronaR * 2.0f, coronaR * 2.0f);
        }

        // ★ 主役ノブ(heroKnob)専用: V-Liverクチュール・ティアラハロー
        if (isHero && radius > 34.0f && (s_knobStyle == 2 || s_useKawaii))
        {
            const float outerR = radius + 6.5f;
            const juce::Colour goldCol = Palette::metalMid;
            g.setColour (goldCol.withAlpha (isWorking ? 0.68f : 0.28f));
            g.drawEllipse (c.x - outerR, c.y - outerR, outerR * 2.0f, outerR * 2.0f, 2.0f);
            for (int q = 0; q < 4; ++q)
            {
                const float qa = (float) q * juce::MathConstants<float>::halfPi + juce::MathConstants<float>::pi * 0.25f;
                const float gx = c.x + (outerR + 0.5f) * std::sin (qa);
                const float gy = c.y - (outerR + 0.5f) * std::cos (qa);
                g.setColour (goldCol.withAlpha (isWorking ? 1.0f : 0.45f));
                g.fillEllipse (gx - 4.4f, gy - 4.4f, 8.8f, 8.8f);
                g.setColour (accent.withAlpha (isWorking ? 0.95f : 0.45f));
                g.fillEllipse (gx - 3.0f, gy - 3.0f, 6.0f, 6.0f);
                if (isWorking)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.95f));
                    g.fillEllipse (gx - 1.3f, gy - 1.3f, 2.6f, 2.6f);
                }
            }
        }

        // ★ すっきり見やすい外周目盛り（主役ノブ12分割・標準ノブ8分割）
        const int numTicks = isHero ? 12 : 8;
        for (int tick = 0; tick <= numTicks; ++tick)
        {
            const float tNorm = (float) tick / (float) numTicks;
            const float a = startAngle + (endAngle - startAngle) * tNorm;
            const bool activeTick = isWorking && (tNorm <= pos + 0.005f);
            const bool majorTick  = (tick % (isHero ? 3 : 2) == 0);
            const juce::Colour tickAccent = accent.interpolatedWith (dirTint, tNorm * 0.48f);
            if (s_knobStyle == 2)   // V-Liver: クチュール宝飾メタル台座＋ジュエル・ドット目盛り
            {
                const float dotR = radius + 6.2f;
                const float sz = majorTick ? 4.2f : 2.5f;
                const float dx = c.x + dotR * std::sin (a);
                const float dy = c.y - dotR * std::cos (a);
                g.setColour (activeTick ? tickAccent.withAlpha (0.95f)
                                        : Palette::metalMid.withAlpha (isWorking ? (majorTick ? 0.52f : 0.26f) : (majorTick ? 0.28f : 0.14f)));
                g.fillEllipse (dx - sz * 0.5f, dy - sz * 0.5f, sz, sz);
            }
            else
            {
                const float inner = radius + (isHero ? 4.2f : 3.6f);
                const float outer = radius + (majorTick ? (isHero ? 9.5f : 8.0f) : (isHero ? 6.8f : 5.6f));
                if (activeTick)
                {
                    g.setColour (tickAccent.withAlpha (majorTick ? 0.95f : 0.78f));
                    g.drawLine (c.x + inner * std::sin (a), c.y - inner * std::cos (a),
                                c.x + outer * std::sin (a), c.y - outer * std::cos (a),
                                majorTick ? (isHero ? 2.2f : 1.8f) : 1.3f);
                }
                else
                {
                    g.setColour ((s_knobStyle == 6 ? Palette::metalHi : Palette::inkSoft)
                                   .withAlpha (isWorking ? (majorTick ? 0.42f : 0.20f) : (majorTick ? 0.22f : 0.10f)));
                    g.drawLine (c.x + inner * std::sin (a), c.y - inner * std::cos (a),
                                c.x + outer * std::sin (a), c.y - outer * std::cos (a),
                                majorTick ? (isHero ? 1.5f : 1.2f) : 0.9f);
                }
            }
        }

        // 彫り込みトラック溝＋デュアルトーン・オプティカルライトパイプ発光アーク
        juce::Path track;
        track.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour ((s_knobStyle == 2)
                     ? Palette::metalMid.withAlpha (isWorking ? 0.55f : 0.28f)
                     : (s_useKawaii ? accent.withAlpha (0.24f) : Palette::bgTop.darker (0.48f).withAlpha (0.84f)));
        g.strokePath (track, juce::PathStrokeType (ring + 2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (Palette::track.withAlpha (isWorking ? 1.0f : 0.62f));
        g.strokePath (track, juce::PathStrokeType (ring, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (isWorking)
        {
            juce::Path value;
            value.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, angle, true);
            const float glowAlpha = ((s_knobStyle == 4 || s_knobStyle == 6) ? 0.42f : (isHero ? 0.36f : 0.28f)) * (0.55f + 0.45f * effInt);
            g.setColour (accent.withAlpha (glowAlpha));
            g.strokePath (value, juce::PathStrokeType (ring * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float tipX0 = c.x + radius * std::sin (startAngle), tipY0 = c.y - radius * std::cos (startAngle);
            const float tipX1 = c.x + radius * std::sin (angle),      tipY1 = c.y - radius * std::cos (angle);
            g.setGradientFill (juce::ColourGradient (groupAccent, tipX0, tipY0,
                                                     accent.interpolatedWith (dirTint, 0.50f).brighter (0.28f), tipX1, tipY1, false));
            g.strokePath (value, juce::PathStrokeType (ring * 1.12f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (juce::Colours::white.withAlpha (0.55f + 0.30f * effInt));
            g.strokePath (value, juce::PathStrokeType (ring * 0.42f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // アーク先端の3Dジュエルコロナ・レーザービーコン（効いているツマミだけが鮮烈に輝く）
            const float tipX = c.x + radius * std::sin (angle), tipY = c.y - radius * std::cos (angle);
            const float beaconScale = 1.0f + 0.35f * effInt;
            g.setColour (accent.withAlpha (0.55f));
            g.fillEllipse (tipX - ring * 1.6f * beaconScale, tipY - ring * 1.6f * beaconScale, ring * 3.2f * beaconScale, ring * 3.2f * beaconScale);
            g.setColour (accent.brighter (0.48f));
            g.fillEllipse (tipX - ring * 0.88f * beaconScale, tipY - ring * 0.88f * beaconScale, ring * 1.76f * beaconScale, ring * 1.76f * beaconScale);
            g.setColour (juce::Colours::white.withAlpha (0.98f));
            g.fillEllipse (tipX - ring * 0.42f * beaconScale, tipY - ring * 0.42f * beaconScale, ring * 0.84f * beaconScale, ring * 0.84f * beaconScale);
        }
        else
        {
            // 待機中（0% / 0.0dB / -80dB）はアーク先端に控えめなスリープドットのみを表示し、効いているツマミを際立たせる
            const float tipX = c.x + radius * std::sin (angle), tipY = c.y - radius * std::cos (angle);
            g.setColour (Palette::inkSoft.withAlpha (0.42f));
            g.fillEllipse (tipX - ring * 0.48f, tipY - ring * 0.48f, ring * 0.96f, ring * 0.96f);
        }

        const float kr = radius - ring - (isHero ? 3.8f : 3.0f);
        if (kr < 5.0f)
            return;

        // 待機中（音を変えていない）ツマミは最後に静かなダークベールを掛け、効いているツマミは発光リングを重ねる共通フィニッシャー
        auto finishKnobStateOverlay = [&]()
        {
            if (! isWorking)
            {
                const bool darkBg = Palette::panel.getPerceivedBrightness() < 0.45f;
                g.setColour ((darkBg ? Palette::bgTop.darker (0.55f) : Palette::panel2.darker (0.18f)).withAlpha (0.42f));
                g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
            }
            else if (effInt > 0.15f)
            {
                g.setColour (accent.withAlpha (0.28f + 0.32f * effInt));
                g.drawEllipse (c.x - kr + 0.8f, c.y - kr + 0.8f, (kr - 0.8f) * 2.0f, (kr - 0.8f) * 2.0f, isHero ? 1.8f : 1.3f);
            }
        };

        if (s_knobStyle == 2)   // ライバー (V-Liver: クチュール・プレストチーク＆香水瓶クリスタルカット＆宝飾メタル・ダイヤル)
        {
            // 1. 立体ドロップシャドウ（白飛びせずくっきり浮き上がる下地影）
            g.setColour (Palette::metalDark.darker (0.35f).withAlpha (0.32f));
            g.fillEllipse (c.x - kr, c.y - kr + 3.6f, kr * 2.0f, kr * 2.0f);

            // 2. 外周 24弁フルーテッド・クチュール金属ベゼル（ローズゴールド／プラチナシルバー／ノワール／シャンパン／24Kハニー）
            g.setGradientFill (juce::ColourGradient (Palette::metalHi, c.x - kr * 0.6f, c.y - kr * 0.6f,
                                                     Palette::metalMid.darker (0.14f), c.x + kr * 0.6f, c.y + kr * 0.6f, false));
            g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
            for (int p = 0; p < 24; ++p)
            {
                const float pa = (float) p * juce::MathConstants<float>::twoPi / 24.0f + angle * 0.25f;
                g.setColour ((p % 2 == 0) ? Palette::metalHi.brighter (0.20f).withAlpha (0.78f)
                                          : Palette::metalDark.withAlpha (0.55f));
                g.drawLine (c.x + kr * 0.84f * std::sin (pa), c.y - kr * 0.84f * std::cos (pa),
                            c.x + kr * 0.99f * std::sin (pa), c.y - kr * 0.99f * std::cos (pa), 1.4f);
            }
            g.setColour (Palette::metalDark.brighter (0.12f));
            g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.3f);

            // 3. プレスト・サテンチーク＆ファンデーションの濃厚ピグメントコア（カラーバリエーション完全連動）
            const float blushR = kr * 0.84f;
            const bool darkCouture = Palette::panel.getPerceivedBrightness() < 0.35f;
            const juce::Colour blushTop = darkCouture
                ? accent.interpolatedWith (Palette::panel2.brighter (0.18f), 0.42f)
                : accent.interpolatedWith (Palette::panel, 0.56f);
            const juce::Colour blushMid = darkCouture
                ? accent.interpolatedWith (Palette::panel, 0.48f)
                : accent.interpolatedWith (Palette::panel2, 0.38f);
            const juce::Colour blushBot = darkCouture
                ? accent.darker (0.45f).interpolatedWith (Palette::bgBot, 0.45f)
                : accent.darker (0.18f).interpolatedWith (Palette::panelLn, 0.42f);
            juce::ColourGradient blushGrad (blushTop, c.x - blushR * 0.4f, c.y - blushR * 0.55f,
                                            blushBot, c.x + blushR * 0.45f, c.y + blushR * 0.65f, false);
            blushGrad.addColour (0.52, blushMid);
            g.setGradientFill (blushGrad);
            g.fillEllipse (c.x - blushR, c.y - blushR, blushR * 2.0f, blushR * 2.0f);

            // 4. クリスタル香水瓶の12面ブリリアント・ファセットカット光線（角度に連動して煌めく）
            for (int f = 0; f < 12; ++f)
            {
                const float fa1 = angle * 0.35f + (float) f * juce::MathConstants<float>::twoPi / 12.0f;
                const float fa2 = fa1 + juce::MathConstants<float>::twoPi / 24.0f;
                juce::Path facet;
                facet.startNewSubPath (c.x, c.y);
                facet.lineTo (c.x + blushR * std::sin (fa1), c.y - blushR * std::cos (fa1));
                facet.lineTo (c.x + blushR * std::sin (fa2), c.y - blushR * std::cos (fa2));
                facet.closeSubPath();
                g.setColour ((f % 2 == 0) ? juce::Colours::white.withAlpha (0.20f)
                                          : accent.darker (0.28f).withAlpha (0.14f));
                g.fillPath (facet);
            }

            // 5. 内周のクチュール宝飾・カメオリング＆12粒のパールカボション
            const float cameoR = kr * 0.66f;
            g.setColour (Palette::metalHi.darker (0.08f).withAlpha (0.90f));
            g.drawEllipse (c.x - cameoR, c.y - cameoR, cameoR * 2.0f, cameoR * 2.0f, isHero ? 1.8f : 1.3f);
            for (int p = 0; p < 12; ++p)
            {
                const float pa = (float) p * juce::MathConstants<float>::twoPi / 12.0f;
                const float px = c.x + cameoR * std::sin (pa);
                const float py = c.y - cameoR * std::cos (pa);
                g.setColour (Palette::metalDark.withAlpha (0.75f));
                g.fillEllipse (px - 2.3f, py - 2.3f, 4.6f, 4.6f);
                g.setColour (juce::Colour (0xfffffdf9));
                g.fillEllipse (px - 1.6f, py - 1.6f, 3.2f, 3.2f);
            }

            // 6. 中心の3Dハート宝石＆純白エンジェルウィング（濃いチーク下地の上だから白羽と宝石が鮮烈に映える！）
            {
                const float hs = juce::jmax (5.0f, kr * (isHero ? 0.34f : 0.28f));
                if (isHero && kr > 20.0f)
                {
                    // 左右の純白エンジェルウィング（クチュール金具縁取り付き立体羽）
                    for (int side : { -1, 1 })
                    {
                        const float s = (float) side;
                        juce::Path wing;
                        wing.startNewSubPath (c.x + s * hs * 0.55f, c.y - hs * 0.10f);
                        wing.cubicTo (c.x + s * hs * 1.45f, c.y - hs * 0.85f,
                                      c.x + s * hs * 2.15f, c.y - hs * 0.35f,
                                      c.x + s * hs * 1.75f, c.y + hs * 0.18f);
                        wing.cubicTo (c.x + s * hs * 1.40f, c.y + hs * 0.48f,
                                      c.x + s * hs * 0.90f, c.y + hs * 0.35f,
                                      c.x + s * hs * 0.55f, c.y + hs * 0.15f);
                        wing.closeSubPath();
                        g.setColour (juce::Colours::white.withAlpha (0.95f));
                        g.fillPath (wing);
                        g.setColour (Palette::metalMid);
                        g.strokePath (wing, juce::PathStrokeType (1.2f));
                    }
                }
                juce::Path hp;
                hp.startNewSubPath (c.x, c.y + hs * 0.62f);
                hp.cubicTo (c.x - hs * 1.15f, c.y - hs * 0.10f, c.x - hs * 0.60f, c.y - hs * 0.95f, c.x, c.y - hs * 0.28f);
                hp.cubicTo (c.x + hs * 0.60f, c.y - hs * 0.95f, c.x + hs * 1.15f, c.y - hs * 0.10f, c.x, c.y + hs * 0.62f);
                hp.closeSubPath();
                g.setGradientFill (juce::ColourGradient (accent.brighter (0.30f), c.x - hs * 0.4f, c.y - hs * 0.5f,
                                                         accent.darker (0.35f),   c.x + hs * 0.4f, c.y + hs * 0.5f, false));
                g.fillPath (hp);
                g.setColour (Palette::metalHi);
                g.strokePath (hp, juce::PathStrokeType (1.5f));
                // ハート宝石のハイライト粒
                g.setColour (juce::Colours::white.withAlpha (0.92f));
                g.fillEllipse (c.x - hs * 0.42f, c.y - hs * 0.42f, hs * 0.28f, hs * 0.22f);
            }

            // 7. クチュール金具の魔法杖ポインター＋先端の4芒星ジュエル
            const float px = c.x + kr * 0.64f * std::sin (angle);
            const float py = c.y - kr * 0.64f * std::cos (angle);
            g.setColour (Palette::metalHi);
            g.drawLine (c.x + kr * 0.24f * std::sin (angle), c.y - kr * 0.24f * std::cos (angle),
                        px, py, isHero ? 2.6f : 2.0f);
            const float sr = juce::jmax (4.8f, kr * 0.28f);
            juce::Path star;
            for (int i = 0; i < 8; ++i)
            {
                const float sa = angle + (float) i * juce::MathConstants<float>::pi * 0.25f;
                const float rr = (i % 2 == 0) ? sr : sr * 0.38f;
                const float sx = px + rr * std::sin (sa), sy = py - rr * std::cos (sa);
                if (i == 0) star.startNewSubPath (sx, sy); else star.lineTo (sx, sy);
            }
            star.closeSubPath();
            g.setColour (Palette::metalDark);
            g.strokePath (star, juce::PathStrokeType (1.8f));
            g.setColour (juce::Colours::white);
            g.fillPath (star);
            g.setColour (accent);
            g.fillEllipse (px - 2.2f, py - 2.2f, 4.4f, 4.4f);

            // 8. ガラスドーム上部のグロス・ハイライト
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.62f), c.x, c.y - blushR,
                                                     juce::Colours::transparentWhite, c.x, c.y - blushR * 0.15f, false));
            g.fillEllipse (c.x - blushR * 0.72f, c.y - blushR * 0.88f, blushR * 1.44f, blushR * 0.68f);
            finishKnobStateOverlay();
            return;
        }
        if (s_knobStyle == 3)   // スタジオ (Studio Pro): 真鍮ベゼル＆ローレット刻み＆精密ムーブメントダイヤル
        {
            const int dia = juce::jmax (10, juce::roundToInt (kr * 2.0f));
            g.setColour (juce::Colours::black.withAlpha (0.52f));
            g.fillEllipse (c.x - kr, c.y - kr + 3.5f, kr * 2.0f, kr * 2.0f);
            g.drawImage (getMovementFace (dia),
                         juce::Rectangle<float> (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f));
            // 外周の金属ローレット（36刻みのハードウェア・ナーリング）
            g.setColour (Palette::yellow.withAlpha (0.38f));
            for (int k = 0; k < 36; ++k)
            {
                const float ka = (float) k * juce::MathConstants<float>::twoPi / 36.0f + angle * 0.2f;
                g.drawLine (c.x + (kr - 3.2f) * std::sin (ka), c.y - (kr - 3.2f) * std::cos (ka),
                            c.x + kr * std::sin (ka),          c.y - kr * std::cos (ka), 1.2f);
            }
            // 内部ギアがツマミ角度に連動して精密回転
            g.setColour (Palette::yellow.withAlpha (0.48f));
            g.fillPath (uGearBig, juce::AffineTransform::rotation (angle * 0.8f)
                                      .scaled (kr * 0.44f, kr * 0.44f)
                                      .translated (c.x - kr * 0.16f, c.y + kr * 0.12f));
            g.setColour (Palette::inkSoft.withAlpha (0.48f));
            g.fillPath (uGearSmall, juce::AffineTransform::rotation (-angle * 1.4f)
                                        .scaled (kr * 0.28f, kr * 0.28f)
                                        .translated (c.x + kr * 0.30f, c.y - kr * 0.10f));
            // ルビー・ピボット軸受石 (時計ムーブメント意匠)
            g.setColour (juce::Colour (0xffe03c62).withAlpha (0.85f));
            g.fillEllipse (c.x - kr * 0.16f - 2.4f, c.y + kr * 0.12f - 2.4f, 4.8f, 4.8f);
            // サファイアクリスタル風防の上部レンズ反射ハイライト
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), c.x, c.y - kr * 0.85f,
                                                     juce::Colours::transparentWhite, c.x, c.y, false));
            g.fillEllipse (c.x - kr * 0.78f, c.y - kr * 0.84f, kr * 1.56f, kr * 0.82f);

            const float pOut = kr * 0.86f, pIn = kr * 0.22f;
            juce::Path pointer;
            pointer.startNewSubPath (c.x + pIn  * std::sin (angle), c.y - pIn  * std::cos (angle));
            pointer.lineTo          (c.x + pOut * std::sin (angle), c.y - pOut * std::cos (angle));
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.strokePath (pointer, juce::PathStrokeType (juce::jmax (3.8f, kr * 0.095f),
                                                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (Palette::ink);
            g.strokePath (pointer, juce::PathStrokeType (juce::jmax (2.6f, kr * 0.068f),
                                                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (Palette::yellowDk);
            g.fillEllipse (c.x - 3.6f, c.y - 3.6f, 7.2f, 7.2f);
            g.setColour (Palette::ink);
            g.fillEllipse (c.x - 1.5f, c.y - 1.5f, 3.0f, 3.0f);
            finishKnobStateOverlay();
            return;
        }
        if (s_knobStyle == 6)   // 🌌🌊🌅🍁👑 風景・季節コレクション: 5つの世界観ごとの専用質感オーブダイヤル
        {
            const int tm = Palette::currentTheme;
            // 1. ドロップシャドウ＋世界観オーラ発光
            g.setColour (juce::Colours::black.withAlpha (0.48f));
            g.fillEllipse (c.x - kr, c.y - kr + 3.6f, kr * 2.0f, kr * 2.0f);
            g.setColour (accent.withAlpha (0.26f));
            g.fillEllipse (c.x - kr - 2.0f, c.y - kr - 2.0f, (kr + 2.0f) * 2.0f, (kr + 2.0f) * 2.0f);

            // 2. 外周ベゼル（15=瑞々しい水滴アクアガラスリング、その他=貴金属アストロラーベ・シャンデリア金環）
            if (tm == 15)
            {
                // 🌊 02 碧海: 半透明の瑞々しいウォータードロップ・アクアクリスタル外輪
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe0fbfc), c.x - kr * 0.7f, c.y - kr * 0.7f,
                                                         juce::Colour (0xff0077b6), c.x + kr * 0.7f, c.y + kr * 0.7f, false));
                g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
                g.setColour (juce::Colour (0xff90e0ef).withAlpha (0.85f));
                g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.5f);
            }
            else
            {
                g.setGradientFill (juce::ColourGradient (Palette::metalHi,  c.x - kr * 0.7f, c.y - kr * 0.7f,
                                                         Palette::metalDark, c.x + kr * 0.7f, c.y + kr * 0.7f, false));
                g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
                for (int p = 0; p < 24; ++p)
                {
                    const float pa = (float) p * juce::MathConstants<float>::twoPi / 24.0f + angle * 0.20f;
                    g.setColour ((p % 2 == 0) ? Palette::metalHi.withAlpha (0.76f)
                                              : Palette::metalDark.darker (0.35f).withAlpha (0.65f));
                    g.drawLine (c.x + kr * 0.85f * std::sin (pa), c.y - kr * 0.85f * std::cos (pa),
                                c.x + kr * 0.99f * std::sin (pa), c.y - kr * 0.99f * std::cos (pa), 1.3f);
                }
                g.setColour (Palette::metalMid.brighter (0.20f));
                g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.3f);
            }

            // 3. 球体オーブ内面の世界観別テクスチャ描画
            const float orbR = kr * 0.86f;
            if (tm == 15)
            {
                // 🌊 02 瑞々しい碧海: ぷるんと透き通る水球（ウォータードロップ・オーブ）＋内部の水紋コースティクス＋気泡
                juce::ColourGradient aquaOrb (juce::Colour (0xff00f5d4), c.x - orbR * 0.35f, c.y - orbR * 0.55f,
                                              juce::Colour (0xff023e8a), c.x + orbR * 0.35f, c.y + orbR * 0.65f, true);
                aquaOrb.addColour (0.48, juce::Colour (0xff00b4d8));
                aquaOrb.addColour (0.82, juce::Colour (0xff0077b6));
                g.setGradientFill (aquaOrb);
                g.fillEllipse (c.x - orbR, c.y - orbR, orbR * 2.0f, orbR * 2.0f);

                // 下部の三日月状・水滴内部屈折（エメラルドシアン発光メニスカス）
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xcc90f1ef), c.x, c.y + orbR * 0.92f,
                                                         juce::Colours::transparentBlack, c.x, c.y + orbR * 0.10f, false));
                g.fillEllipse (c.x - orbR * 0.84f, c.y - orbR * 0.60f, orbR * 1.68f, orbR * 1.48f);

                // 水球内部で揺らめく3本の水紋コースティクス波ライン
                for (int wv = 0; wv < 3; ++wv)
                {
                    juce::Path waveP;
                    const float wy0 = c.y - orbR * 0.28f + (float) wv * (orbR * 0.28f);
                    for (int xi = -6; xi <= 6; ++xi)
                    {
                        const float wx = c.x + (float) xi * (orbR * 0.12f);
                        const float wy = wy0 + std::sin ((float) xi * 0.75f + angle + (float) wv * 1.4f) * (orbR * 0.10f);
                        if (xi == -6) waveP.startNewSubPath (wx, wy); else waveP.lineTo (wx, wy);
                    }
                    g.setColour (juce::Colour (0xffcaf0f8).withAlpha (0.46f - (float) wv * 0.08f));
                    g.strokePath (waveP, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }

                // 水球の中に浮かぶ4つの立体気泡（バブル）
                static const float kBubbles[4][3] = {
                    { -0.38f,  0.22f, 0.16f },
                    {  0.34f,  0.30f, 0.13f },
                    { -0.20f,  0.52f, 0.10f },
                    {  0.26f, -0.24f, 0.11f }
                };
                for (int b = 0; b < 4; ++b)
                {
                    const float bx = c.x + kBubbles[b][0] * orbR;
                    const float by = c.y + kBubbles[b][1] * orbR;
                    const float br = juce::jmax (2.0f, kBubbles[b][2] * orbR);
                    g.setColour (juce::Colour (0x55caf0f8));
                    g.fillEllipse (bx - br, by - br, br * 2.0f, br * 2.0f);
                    g.setColour (juce::Colours::white.withAlpha (0.85f));
                    g.drawEllipse (bx - br, by - br, br * 2.0f, br * 2.0f, 1.0f);
                    g.fillEllipse (bx - br * 0.55f, by - br * 0.55f, br * 0.5f, br * 0.5f);
                }
            }
            else if (tm == 14)
            {
                // 🌌 01 星降る天の川銀河: 渦巻銀河（スパイラル・ギャラクシー）とマゼンタ＆シアンの星雲コア
                juce::ColourGradient galOrb (juce::Colour (0xff1e1b4b), c.x - orbR * 0.3f, c.y - orbR * 0.3f,
                                             juce::Colour (0xff050716), c.x + orbR * 0.6f, c.y + orbR * 0.6f, true);
                galOrb.addColour (0.50, juce::Colour (0xff11163b));
                g.setGradientFill (galOrb);
                g.fillEllipse (c.x - orbR, c.y - orbR, orbR * 2.0f, orbR * 2.0f);

                // 2本の対数渦巻腕（Spiral Galaxy Arms）
                for (int arm = 0; arm < 2; ++arm)
                {
                    juce::Path spiral;
                    const float armOffset = (float) arm * juce::MathConstants<float>::pi + angle * 0.5f;
                    for (int st = 0; st <= 18; ++st)
                    {
                        const float t = (float) st / 18.0f;
                        const float sr = orbR * (0.10f + 0.68f * t);
                        const float sa = armOffset + t * 3.6f;
                        const float sx = c.x + sr * std::cos (sa);
                        const float sy = c.y + sr * std::sin (sa);
                        if (st == 0) spiral.startNewSubPath (sx, sy); else spiral.lineTo (sx, sy);
                    }
                    g.setColour ((arm == 0 ? juce::Colour (0xffec4899) : juce::Colour (0xff38bdf8)).withAlpha (0.48f));
                    g.strokePath (spiral, juce::PathStrokeType (orbR * 0.18f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.setColour (juce::Colours::white.withAlpha (0.55f));
                    g.strokePath (spiral, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                // 銀河中心バルジの黄金グロー
                g.setGradientFill (juce::ColourGradient (juce::Colour (0x99fde047), c.x, c.y,
                                                         juce::Colours::transparentBlack, c.x + orbR * 0.55f, c.y, true));
                g.fillEllipse (c.x - orbR * 0.55f, c.y - orbR * 0.55f, orbR * 1.1f, orbR * 1.1f);
            }
            else if (tm == 16)
            {
                // 🌅 03 朝焼けの海と暁の水平線: インディゴ空〜ロゼ〜昇る黄金の太陽と12本の光芒
                juce::ColourGradient dawnOrb (juce::Colour (0xff1d1848), c.x, c.y - orbR,
                                              juce::Colour (0xff3d1832), c.x, c.y + orbR, false);
                dawnOrb.addColour (0.38, juce::Colour (0xff8a2be2).interpolatedWith (juce::Colour (0xffe84a7f), 0.65f));
                dawnOrb.addColour (0.58, juce::Colour (0xffff9e44));
                dawnOrb.addColour (0.72, juce::Colour (0xffc83e5b));
                g.setGradientFill (dawnOrb);
                g.fillEllipse (c.x - orbR, c.y - orbR, orbR * 2.0f, orbR * 2.0f);

                // 水平線から放射する10本の朝日光芒
                const float sunY = c.y + orbR * 0.14f;
                for (int rIdx = 0; rIdx < 10; ++rIdx)
                {
                    const float ra = -1.35f + (float) rIdx * (2.70f / 9.0f);
                    g.setColour (juce::Colour (0xffffea9f).withAlpha (0.32f));
                    g.drawLine (c.x, sunY, c.x + orbR * 0.88f * std::sin (ra), sunY - orbR * 0.88f * std::cos (ra), 1.8f);
                }
                // 昇る朝日コア
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff3b0), c.x, sunY,
                                                         juce::Colour (0x00ff9e00), c.x + orbR * 0.55f, sunY, true));
                g.fillEllipse (c.x - orbR * 0.55f, sunY - orbR * 0.55f, orbR * 1.10f, orbR * 1.10f);
            }
            else if (tm == 17)
            {
                // 🍁 04 晩秋から初冬へ: 初冬のスレート藍空×茜色の水鏡×波紋×氷晶＆紅葉モチーフ
                juce::ColourGradient autOrb (juce::Colour (0xff162138), c.x, c.y - orbR,
                                             juce::Colour (0xff1c141c), c.x, c.y + orbR, false);
                autOrb.addColour (0.45, juce::Colour (0xffa83834));
                autOrb.addColour (0.56, juce::Colour (0xffe07a3c));
                autOrb.addColour (0.72, juce::Colour (0xff5c2832));
                g.setGradientFill (autOrb);
                g.fillEllipse (c.x - orbR, c.y - orbR, orbR * 2.0f, orbR * 2.0f);

                // 水鏡の同心楕円波紋（2重）
                g.setColour (juce::Colour (0xffbae6fd).withAlpha (0.42f));
                g.drawEllipse (c.x - orbR * 0.64f, c.y + orbR * 0.10f, orbR * 1.28f, orbR * 0.36f, 1.1f);
                g.setColour (juce::Colour (0xfff59e0b).withAlpha (0.45f));
                g.drawEllipse (c.x - orbR * 0.42f, c.y + orbR * 0.18f, orbR * 0.84f, orbR * 0.22f, 1.0f);

                // 6角の雪の結晶ライン（上半分）
                const float snowX = c.x - orbR * 0.28f, snowY = c.y - orbR * 0.28f, snowR = orbR * 0.20f;
                g.setColour (juce::Colour (0xffe0f2fe).withAlpha (0.78f));
                for (int k = 0; k < 3; ++k)
                {
                    const float ka = (float) k * juce::MathConstants<float>::pi / 3.0f;
                    g.drawLine (snowX - snowR * std::cos (ka), snowY - snowR * std::sin (ka),
                                snowX + snowR * std::cos (ka), snowY + snowR * std::sin (ka), 1.2f);
                }
            }
            else
            {
                // 👑 05 蒼海オペラ座と黄金シャンデリア: ロイヤルサファイア＆12面シャンデリア・クリスタルカット
                juce::ColourGradient opOrb (juce::Colour (0xff2563eb), c.x - orbR * 0.35f, c.y - orbR * 0.45f,
                                            juce::Colour (0xff050d26), c.x + orbR * 0.45f, c.y + orbR * 0.65f, true);
                opOrb.addColour (0.55, juce::Colour (0xff112868));
                g.setGradientFill (opOrb);
                g.fillEllipse (c.x - orbR, c.y - orbR, orbR * 2.0f, orbR * 2.0f);
                g.setColour (Palette::metalHi.withAlpha (0.28f));
                for (int fc = 0; fc < 6; ++fc)
                {
                    const float fa = (float) fc * juce::MathConstants<float>::pi / 6.0f + angle * 0.25f;
                    g.drawLine (c.x - orbR * 0.78f * std::cos (fa), c.y - orbR * 0.78f * std::sin (fa),
                                c.x + orbR * 0.78f * std::cos (fa), c.y + orbR * 0.78f * std::sin (fa), 1.0f);
                }
            }

            // 4. 内部リング＆方位ドット
            const float ringR = orbR * 0.72f;
            g.setColour (Palette::metalHi.withAlpha (tm == 15 ? 0.55f : 0.42f));
            g.drawEllipse (c.x - ringR, c.y - ringR, ringR * 2.0f, ringR * 2.0f, 1.1f);
            for (int s = 0; s < 8; ++s)
            {
                const float sa = angle * 0.4f + (float) s * juce::MathConstants<float>::pi * 0.25f;
                const float sx = c.x + ringR * std::sin (sa);
                const float sy = c.y - ringR * std::cos (sa);
                const float dSz = (s % 2 == 0) ? 3.2f : 2.0f;
                g.setColour ((s % 2 == 0) ? Palette::metalHi.withAlpha (0.88f) : accent.withAlpha (0.76f));
                g.fillEllipse (sx - dSz * 0.5f, sy - dSz * 0.5f, dSz, dSz);
            }

            // 5. 中心の指針＆先端の4芒星／水滴フレア
            const float pOut = orbR * 0.84f, pIn = orbR * 0.18f;
            g.setColour (accent.withAlpha (0.50f));
            g.drawLine (c.x + pIn * std::sin (angle), c.y - pIn * std::cos (angle),
                        c.x + pOut * std::sin (angle), c.y - pOut * std::cos (angle),
                        isHero ? 5.2f : 4.0f);
            g.setColour (Palette::metalHi);
            g.drawLine (c.x + pIn * std::sin (angle), c.y - pIn * std::cos (angle),
                        c.x + pOut * std::sin (angle), c.y - pOut * std::cos (angle),
                        isHero ? 2.4f : 1.9f);

            const float px = c.x + (orbR * 0.68f) * std::sin (angle);
            const float py = c.y - (orbR * 0.68f) * std::cos (angle);
            const float sr = juce::jmax (4.4f, kr * 0.24f);
            juce::Path star;
            for (int i = 0; i < 8; ++i)
            {
                const float sa = angle + (float) i * juce::MathConstants<float>::pi * 0.25f;
                const float rr = (i % 2 == 0) ? sr : sr * 0.30f;
                const float sx = px + rr * std::sin (sa), sy = py - rr * std::cos (sa);
                if (i == 0) star.startNewSubPath (sx, sy); else star.lineTo (sx, sy);
            }
            star.closeSubPath();
            g.setColour (accent.withAlpha (0.60f));
            g.fillEllipse (px - sr * 0.85f, py - sr * 0.85f, sr * 1.7f, sr * 1.7f);
            g.setColour (Palette::metalHi);
            g.fillPath (star);
            g.setColour (juce::Colours::white);
            g.fillEllipse (px - 1.8f, py - 1.8f, 3.6f, 3.6f);

            // 中心ハブ
            const float hubR = juce::jmax (3.8f, orbR * 0.20f);
            g.setGradientFill (juce::ColourGradient (Palette::metalHi,  c.x - hubR, c.y - hubR,
                                                     Palette::metalMid, c.x + hubR, c.y + hubR, false));
            g.fillEllipse (c.x - hubR, c.y - hubR, hubR * 2.0f, hubR * 2.0f);
            g.setColour (Palette::metalDark);
            g.drawEllipse (c.x - hubR, c.y - hubR, hubR * 2.0f, hubR * 2.0f, 1.1f);

            // 6. 球面ガラス・水球上部のツヤ・三日月スペキュラハイライト
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (tm == 15 ? 0.62f : 0.42f), c.x, c.y - orbR * 0.94f,
                                                     juce::Colours::transparentWhite,                            c.x, c.y - orbR * 0.08f, false));
            g.fillEllipse (c.x - orbR * 0.74f, c.y - orbR * 0.88f, orbR * 1.48f, orbR * 0.72f);
            if (tm == 15)
            {
                // 水滴特有の左上ピンスポット純白ハイライト
                g.setColour (juce::Colours::white.withAlpha (0.88f));
                g.fillEllipse (c.x - orbR * 0.52f, c.y - orbR * 0.68f, orbR * 0.28f, orbR * 0.16f);
            }
            finishKnobStateOverlay();
            return;
        }

        // Standard / Argon Cockpit (0) & Cyber Neon (4):
        // すっきり見やすいマット立体ベゼル＋発光ポインター
        g.setColour (juce::Colours::black.withAlpha (0.40f));
        g.fillEllipse (c.x - kr, c.y - kr + 3.5f, kr * 2.0f, kr * 2.0f);
        g.setGradientFill (juce::ColourGradient (Palette::panel2.brighter (0.24f), c.x - kr * 0.5f, c.y - kr,
                                                 Palette::bgTop.darker (0.42f),    c.x + kr * 0.5f, c.y + kr, false));
        g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
        g.setColour (s_knobStyle == 4 || isHero ? accent.withAlpha (0.68f) : Palette::panelLn.brighter (0.22f));
        g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, isHero ? 1.5f : 1.2f);

        // 内側のキャップ天面（滑らかなダーク凹面＋上縁のソフトハイライト）
        const float capR = kr * (isHero ? 0.78f : 0.80f);
        g.setGradientFill (juce::ColourGradient (Palette::bgTop.darker (0.32f), c.x, c.y - capR,
                                                 Palette::panel.brighter (0.10f), c.x, c.y + capR, false));
        g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
        g.setColour (s_knobStyle == 4 ? Palette::salmon.withAlpha (0.45f) : accent.withAlpha (isHero ? 0.45f : 0.30f));
        g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);

        // 天面ガラスの三日月ハイライト
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.18f), c.x, c.y - capR,
                                                 juce::Colours::transparentWhite,        c.x, c.y, false));
        g.fillEllipse (c.x - capR * 0.80f, c.y - capR * 0.88f, capR * 1.60f, capR * 0.80f);

        // 中心のコアハブ
        const float hubR = kr * (isHero ? 0.20f : 0.18f);
        g.setColour (Palette::bgTop.darker (0.40f));
        g.fillEllipse (c.x - hubR, c.y - hubR, hubR * 2.0f, hubR * 2.0f);
        g.setColour (accent.withAlpha (0.72f));
        g.drawEllipse (c.x - hubR, c.y - hubR, hubR * 2.0f, hubR * 2.0f, 1.2f);

        const float pOut = kr * 0.88f, pIn = kr * 0.26f;
        juce::Path pointer;
        pointer.startNewSubPath (c.x + pIn  * std::sin (angle), c.y - pIn  * std::cos (angle));
        pointer.lineTo          (c.x + pOut * std::sin (angle), c.y - pOut * std::cos (angle));
        g.setColour (accent.withAlpha (0.40f));
        g.strokePath (pointer, juce::PathStrokeType (juce::jmax (5.6f, kr * 0.14f),
                                                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (pointer, juce::PathStrokeType (juce::jmax (3.0f, kr * 0.075f),
                                                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.88f));
        g.strokePath (pointer, juce::PathStrokeType (juce::jmax (1.3f, kr * 0.030f),
                                                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        finishKnobStateOverlay();
    }

    // ================= v1.7.0 yuru-kawa: juice-glass knobs =================
    // fill height = value; each knob a different juice (per-panel fruit theme).
    static juce::Colour juiceColour (int fruit)
    {
        static const juce::uint32 cols[] = {
            0xffffa53c, 0xfff5d24a, 0xff9ccf5a, 0xfffa826e, 0xffe6d95a,  // 0-4  citrus
            0xfff05a72, 0xff7890d8, 0xffe86aa0, 0xff9b78c8, 0xffd23c50,  // 5-9  berry
            0xffffb43c, 0xfff5cd50, 0xfff58c46, 0xfffab9a5,             // 10-13 tropical
            0xffc8e196, 0xfff0505f, 0xffaad796, 0xffa0c86e, 0xffc8e1a0  // 14-18 melon/grape
        };
        const int n = (int) (sizeof (cols) / sizeof (cols[0]));
        return juce::Colour (cols[juce::jlimit (0, n - 1, fruit)]);
    }
    static int garnishType (int fruit)   // 0 citrus slice, 1 melon wedge, 2 berry cluster
    {
        switch (fruit) {
            case 15: case 16: return 1;
            case 5: case 6: case 7: case 8: case 9: case 14: case 17: return 2;
            default: return 0;
        }
    }
    // per-panel fruit assignment (by parameter id)
    static int juiceFruitFor (const juce::String& id)
    {
        if (id=="gate") return 0; if (id=="lowcut") return 1; if (id=="mud") return 2;
        if (id=="harsh") return 3; if (id=="denoise") return 4;                 // CLEAN UP: citrus
        if (id=="comp1") return 5; if (id=="comp2") return 6; if (id=="attack") return 7;
        if (id=="release") return 8; if (id=="deess") return 9;                 // DYNAMICS: berry
        if (id=="presence") return 10; if (id=="air") return 11;
        if (id=="drive") return 12; if (id=="sustain") return 13;               // TONE: tropical
        if (id=="makeup") return 14; if (id=="mix") return 15; if (id=="width") return 16;
        if (id=="doubler") return 17; if (id=="delay") return 18;
        if (id=="revsize") return 14; if (id=="revmix") return 16;              // SPACE: melon/grape
        if (id=="seq_amount") return 0; if (id=="seq_focus") return 5;
        if (id.startsWith ("seq_f")) return 2; if (id.startsWith ("seq_d")) return 10;
        if (id=="duck") return 9; if (id=="cho_amt") return 6; if (id=="bpm") return 1;
        if (id=="dly_ms") return 11; if (id=="dly_fb") return 7; if (id=="dly_hc") return 3;
        if (id=="mega_amt") return 15; if (id=="robo_freq") return 12; if (id=="robo_mix") return 17;
        return (int) (((id.hashCode() % 19) + 19) % 19);
    }

    void drawGarnish (juce::Graphics& g, int type, juce::Colour col, float cx, float cy, float s)
    {
        if (type == 1)   // melon wedge
        {
            juce::Path w;
            w.addPieSegment (cx - s, cy - s, s * 2.0f, s * 2.0f,
                             juce::degreesToRadians (198.0f), juce::degreesToRadians (342.0f), 0.0f);
            g.setColour (col); g.fillPath (w);
            g.setColour (juce::Colour (0xff5faf5f));
            g.strokePath (w, juce::PathStrokeType (s * 0.26f));
            g.setColour (juce::Colour (0xff40332e));
            for (int i = -1; i <= 1; ++i)
                g.fillEllipse (cx + i * s * 0.42f - s * 0.07f, cy - s * 0.30f, s * 0.14f, s * 0.20f);
        }
        else if (type == 2)   // berry cluster
        {
            const float r = s * 0.40f;
            const juce::Point<float> pts[] = { {cx-r,cy-r*0.3f},{cx+r,cy-r*0.3f},{cx,cy-r*1.05f},
                                               {cx-r*0.5f,cy+r*0.7f},{cx+r*0.6f,cy+r*0.7f} };
            for (auto& p : pts) {
                g.setColour (col);              g.fillEllipse (p.x - r, p.y - r, r*2, r*2);
                g.setColour (col.brighter(0.45f)); g.fillEllipse (p.x - r*0.5f, p.y - r*0.6f, r*0.5f, r*0.5f);
            }
        }
        else   // citrus slice
        {
            g.setColour (juce::Colours::white);              g.fillEllipse (cx - s, cy - s, s*2, s*2);
            g.setColour (col);                               g.fillEllipse (cx - s*0.80f, cy - s*0.80f, s*1.6f, s*1.6f);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            for (int a = 0; a < 360; a += 45) {
                const float rd = juce::degreesToRadians ((float) a);
                g.drawLine (cx, cy, cx + std::cos (rd) * s * 0.76f, cy + std::sin (rd) * s * 0.76f, 1.3f);
            }
            g.setColour (col.brighter (0.5f));               g.fillEllipse (cx - s*0.16f, cy - s*0.16f, s*0.32f, s*0.32f);
        }
    }

    // v2.4.0: wide=true でグラスをセル幅いっぱいに近づける。かんたんモードは
    //         7本を横いっぱいに並べるのでセルが広く、0.62 のままだと細長い
    //         試験管みたいに見えてしまう。
    void drawJuiceKnob (juce::Graphics& g, juce::Rectangle<float> area, float pos, int fruit,
                        bool wide = false)
    {
        const float gw = area.getWidth()  * (wide ? 0.90f : 0.64f);
        const float gh = area.getHeight() * 0.88f;
        juce::Rectangle<float> glass (area.getCentreX() - gw * 0.5f,
                                      area.getCentreY() - gh * 0.5f + gh * 0.05f, gw, gh);
        const float corner = gw * 0.18f;
        juce::Rectangle<float> inner = glass.reduced (gw * 0.06f);
        const float fillH = juce::jmax (0.0f, inner.getHeight() * pos);
        const juce::Colour jc = juiceColour (fruit);

        // グラス背面のキャラメルドロップシャドウ＆切子ガラスの厚み下地
        g.setColour (juce::Colour (0x266b4228));
        g.fillRoundedRectangle (glass.translated (0.0f, 2.5f), corner);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xb8fffdf9), glass.getX(), glass.getY(),
                                                 juce::Colour (0x78f2dfce), glass.getRight(), glass.getBottom(), false));
        g.fillRoundedRectangle (glass, corner);
        // グラス底面の重厚なクリスタルコースター台座
        g.setColour (juce::Colour (0x66c89e78));
        g.fillRoundedRectangle (glass.getX() + gw * 0.08f, glass.getBottom() - gh * 0.07f, gw * 0.84f, gh * 0.06f, corner * 0.4f);

        if (fillH > 2.0f)
        {
            juce::Path clip; clip.addRoundedRectangle (inner, corner * 0.8f);
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (clip);

            const float surfY = inner.getBottom() - fillH;
            // 2層グラデーションの濃厚フルーツパルフェ＆シロップ層
            juce::ColourGradient jg (jc.brighter (0.14f), 0, surfY, jc.darker (0.24f), 0, inner.getBottom(), false);
            jg.addColour (0.65, jc);
            g.setGradientFill (jg);
            g.fillRect (juce::Rectangle<float> (inner.getX() - 2, surfY, inner.getWidth() + 4, fillH + 6));

            // 透き通る氷キューブ（2個）
            if (fillH > gh * 0.25f)
            {
                const float iceS = gw * 0.24f;
                g.setColour (juce::Colours::white.withAlpha (0.38f));
                g.fillRoundedRectangle (inner.getX() + gw * 0.14f, surfY + gh * 0.06f, iceS, iceS * 0.85f, 3.0f);
                g.fillRoundedRectangle (inner.getRight() - gw * 0.40f, surfY + gh * 0.14f, iceS * 0.9f, iceS * 0.8f, 3.0f);
            }

            // 液面のホイップフォーム＆メニスカス光沢
            const float wob = std::sin (s_juicePhase * 1.3f + area.getX() * 0.05f) * (gh * 0.012f);
            g.setColour (juce::Colour (0xe6fff9f0));
            g.fillEllipse (inner.getX() - 2, surfY - gh * 0.032f + wob, inner.getWidth() + 4, gh * 0.064f);
            g.setColour (jc.brighter (0.35f));
            g.drawEllipse (inner.getX() - 1, surfY - gh * 0.028f + wob, inner.getWidth() + 2, gh * 0.056f, 1.2f);

            // 立ち昇るシュワシュワ炭酸バブル
            for (int b = 0; b < 5; ++b)
            {
                const float seed = area.getX() * 0.13f + b * 1.7f;
                const float br = gw * (0.030f + 0.018f * std::fmod (seed, 1.0f));
                const float bx = inner.getX() + inner.getWidth() * (0.2f + 0.6f * std::fmod (seed * 0.37f, 1.0f));
                const float span = juce::jmax (1.0f, fillH);
                const float by = inner.getBottom()
                                 - std::fmod (s_juicePhase * (gh * 0.02f) + seed * 11.0f, span);
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.fillEllipse (bx - br, by - br, br * 2, br * 2);
            }
        }

        // 焦がしキャラメル・ゴールドのグラス縁取り＋立体ガラス反射ハイライト
        g.setColour (juce::Colour (0xffb8865c));
        g.drawRoundedRectangle (glass.reduced (0.6f), corner, juce::jmax (1.6f, gw * 0.034f));
        g.setColour (juce::Colours::white.withAlpha (0.65f));
        g.fillRoundedRectangle (juce::Rectangle<float> (glass.getX() + gw * 0.12f, glass.getY() + gh * 0.10f,
                                                        gw * 0.08f, gh * 0.64f), gw * 0.04f);

        // ストライプ・カフェストロー
        {
            const float x1 = area.getCentreX() + gw * 0.12f, y1 = glass.getY() + gh * 0.28f;
            const float x2 = area.getCentreX() + gw * 0.30f, y2 = glass.getY() - gh * 0.18f;
            g.setColour (juce::Colour (0xffe0607e));
            g.drawLine (x1, y1, x2, y2, gw * 0.090f);
            g.setColour (juce::Colours::white.withAlpha (0.72f));
            g.drawLine (x1, y1, x2, y2, gw * 0.028f);
        }

        // リムに添えたフレッシュフルーツ・ガーニッシュ
        drawGarnish (g, garnishType (fruit), jc.brighter (0.05f),
                     glass.getX() + gw * 0.18f, glass.getY() - gh * 0.01f, gw * 0.20f);
    }
    // =======================================================================

    // v2.12.0 ★「オンオフがぱっと見わからない」への直し。
    //
    //  今までは ON/OFF の差が**色の濃さだけ**だった。しかも押すだけのボタン
    //  （点検・SAVE 等）とスイッチ（ボイス変換・ピッチ補正 等）が同じ形なので、
    //  「どれがスイッチなのか」すら見分けられなかった。
    //
    //  直し: スイッチにだけ**ランプ**を付ける。
    //    ON  … 塗りつぶした丸 + 白いふち + 太い枠 + 濃い地
    //    OFF … 中抜きの輪っか + 細い枠 + 素の地
    //  形がちがうので、色の濃淡が分からない人・小さい画面でも判別できる。
    //  押すだけのボタンにはランプが付かない＝「これは切り替えではない」と分かる。
    static constexpr float kLampBox = 20.0f;   // ランプに使う左側の幅

    static float lampWidthFor (const juce::Button& b) noexcept
    {
        return b.getClickingTogglesState() ? kLampBox : 0.0f;
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float rad = s_useKawaii ? r.getHeight() * 0.5f : 7.5f;
        const bool  on  = b.getToggleState();
        const bool  sw  = b.getClickingTogglesState();
        const bool  isHoldTrigger = (b.getComponentID() == "compareOriginal");

        auto col = on ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
        if (on && col.getFloatAlpha() < 0.85f)
            col = col.withAlpha (juce::jmax (0.85f, col.getFloatAlpha()));
        if (down) col = col.brighter (0.14f);
        else if (over) col = col.brighter (0.07f);

        if (s_knobStyle == 5)
        {
            g.setColour (col);
            g.fillRoundedRectangle (r, rad);
        }
        else if (isHoldTrigger)
        {
            if (s_useKawaii)
            {
                // ♥ Kawaii / V-Liver: クチュール・ローズゴールド＆ルビーハート比較ホールドカプセル
                g.setColour (Palette::salmon.withAlpha (0.24f));
                g.fillRoundedRectangle (r.translated (0.0f, down ? 0.5f : 2.2f), rad);
                g.setGradientFill (juce::ColourGradient (
                    down ? Palette::salmon.brighter (0.25f) : Palette::panel.brighter (0.04f), r.getX(), r.getY(),
                    down ? Palette::salmon.withAlpha (0.55f) : Palette::panel2, r.getX(), r.getBottom(), false));
                g.fillRoundedRectangle (r, rad);
                g.setColour (Palette::metalMid);
                g.drawRoundedRectangle (r, rad, down ? 2.2f : 1.6f);
                // 左右の小さなハートチャーム飾り
                for (float hx : { r.getX() + 12.0f, r.getRight() - 12.0f })
                {
                    const float hy = r.getCentreY();
                    const float hs = 4.5f;
                    juce::Path hp;
                    hp.startNewSubPath (hx, hy + hs * 0.55f);
                    hp.cubicTo (hx - hs * 1.05f, hy - hs * 0.10f, hx - hs * 0.55f, hy - hs * 0.90f, hx, hy - hs * 0.28f);
                    hp.cubicTo (hx + hs * 0.55f, hy - hs * 0.90f, hx + hs * 1.05f, hy - hs * 0.10f, hx, hy + hs * 0.55f);
                    hp.closeSubPath();
                    g.setColour (Palette::salmon.withAlpha (down ? 0.95f : 0.78f));
                    g.fillPath (hp);
                }
                return;
            }
            // ★ 長押しホールド・トリガーパドル ("押して原音と比較" / Hold to compare)
            g.setColour (juce::Colours::black.withAlpha (0.32f));
            g.fillRoundedRectangle (r.translated (0.0f, down ? 0.5f : 2.5f), rad);
            const auto holdAccent = down ? Palette::yellow : Palette::salmon;
            g.setGradientFill (juce::ColourGradient (
                down ? Palette::yellow.withAlpha (0.42f) : Palette::panel2.brighter (0.12f), r.getX(), r.getY(),
                down ? Palette::yellowDk.withAlpha (0.28f) : Palette::bgTop.darker (0.28f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, rad);

            // 左右のコックピット・シェブロンストライプ (>>> [ HOLD ] <<<) で「長押しトリガー」を直感化
            g.setColour (holdAccent.withAlpha (down ? 0.90f : 0.55f));
            for (int cIdx = 0; cIdx < 3; ++cIdx)
            {
                const float lx = r.getX() + 10.0f + (float) cIdx * 7.5f;
                const float rx = r.getRight() - 10.0f - (float) cIdx * 7.5f;
                const float cy = r.getCentreY();
                juce::Path chevL, chevR;
                chevL.startNewSubPath (lx, cy - 5.5f); chevL.lineTo (lx + 4.5f, cy); chevL.lineTo (lx, cy + 5.5f);
                chevR.startNewSubPath (rx, cy - 5.5f); chevR.lineTo (rx - 4.5f, cy); chevR.lineTo (rx, cy + 5.5f);
                g.strokePath (chevL, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.strokePath (chevR, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            // 下辺のスプリング・テンションゲージバー
            g.setColour (holdAccent.withAlpha (down ? 0.95f : 0.65f));
            g.fillRoundedRectangle (r.getX() + 14.0f, r.getBottom() - 3.5f, r.getWidth() - 28.0f, 2.2f, 1.1f);
            g.setColour (holdAccent.withAlpha (down ? 0.95f : 0.78f));
            g.drawRoundedRectangle (r, rad, down ? 2.2f : 1.5f);
            return;
        }
        else if (s_knobStyle == 6)
        {
            // 🌌🌊🌅🍁👑 風景・季節コレクション: 水滴や星雲・夕茜の光を通す半透明クリスタル・ジュエルピルボタン
            const bool hasCustomCol = (col != Palette::panel2 && col != Palette::panel);
            g.setColour (juce::Colours::black.withAlpha ((on || hasCustomCol) ? 0.34f : 0.22f));
            g.fillRoundedRectangle (r.translated (0.0f, down ? 0.5f : 1.8f), rad);
            if (on || (hasCustomCol && col.getAlpha() > 200))
            {
                g.setGradientFill (juce::ColourGradient (col.brighter (0.16f).withAlpha (0.88f), r.getX(), r.getY(),
                                                         col.darker   (0.18f).withAlpha (0.88f), r.getX(), r.getBottom(), false));
            }
            else if (hasCustomCol)
            {
                g.setGradientFill (juce::ColourGradient (col.withMultipliedAlpha (over ? 1.35f : 1.10f), r.getX(), r.getY(),
                                                         Palette::panel.withAlpha (over ? 0.72f : 0.58f), r.getX(), r.getBottom(), false));
            }
            else
            {
                g.setGradientFill (juce::ColourGradient (Palette::panel2.withAlpha (over ? 0.62f : 0.46f), r.getX(), r.getY(),
                                                         Palette::panel.withAlpha  (over ? 0.72f : 0.56f), r.getX(), r.getBottom(), false));
            }
            g.fillRoundedRectangle (r, rad);
            // 上部の水滴ガラス・スペキュラ反射帯
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (on ? 0.34f : 0.22f), r.getX(), r.getY() + 1.0f,
                                                     juce::Colours::transparentWhite,                     r.getX(), r.getY() + r.getHeight() * 0.46f, false));
            g.fillRoundedRectangle (r.reduced (2.0f, 1.0f).withHeight (r.getHeight() * 0.44f), rad * 0.85f);
        }
        else
        {
            // 3D立体プッシュパッド＆ロッカーベゼル（Kawaii系はクチュール・エナメル＆ゴールド縁、ハード系は物理ストローク表現）
            g.setColour (s_useKawaii ? Palette::panelLn.withAlpha (on ? 0.42f : 0.28f)
                                     : juce::Colours::black.withAlpha (on ? 0.30f : 0.18f));
            g.fillRoundedRectangle (r.translated (0.0f, down ? 0.5f : 2.2f), rad);
            if (! down && ! s_useKawaii)
            {
                g.setColour (on ? col.darker (0.45f) : Palette::bgTop.darker (0.40f));
                g.fillRoundedRectangle (r.withTrimmedTop (r.getHeight() * 0.55f).translated (0.0f, 1.2f), rad);
            }
            g.setGradientFill (juce::ColourGradient (col.brighter (on ? 0.18f : 0.08f), r.getX(), r.getY(),
                                                     col.darker   (on ? 0.14f : 0.08f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, rad);
            if (s_useKawaii)
            {
                // エナメルボタン上部のツヤ・ハイライト帯（夜間ダークボタン時はソフトなガラス光沢に調整）
                const bool lightBtn = col.getPerceivedBrightness() > 0.55f;
                const float topAlpha = lightBtn ? (on ? 0.42f : 0.72f) : (on ? 0.24f : 0.14f);
                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (topAlpha), r.getX(), r.getY() + 1.0f,
                                                         juce::Colours::transparentWhite, r.getX(), r.getY() + r.getHeight() * 0.45f, false));
                g.fillRoundedRectangle (r.reduced (2.0f, 1.0f).withHeight (r.getHeight() * 0.44f), rad * 0.85f);
            }
            else
            {
                g.setColour (juce::Colours::white.withAlpha (on ? 0.34f : 0.18f));
                g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.0f), r.getX() + rad, r.getRight() - rad);
            }
        }
        const juce::Colour borderCol = s_useKawaii
            ? (on ? Palette::metalDark : ((s_knobStyle == 2) ? Palette::metalMid : Palette::panelLn.darker (0.12f)))
            : (s_knobStyle == 6)
            ? (on ? Palette::metalHi : Palette::metalMid.withAlpha (over ? 0.90f : 0.68f))
            : (on ? col.darker (0.35f) : Palette::panelLn.brighter (over ? 0.18f : 0.0f));
        g.setColour (borderCol);
        g.drawRoundedRectangle (r, rad, on ? 2.0f : 1.4f);

        if (! sw)
        {
            // ★ 押すだけのトリガーパッドには下辺センターにマイクロ・ライトパイプバーを添えて「押すボタン」を表現
            if (s_knobStyle != 5 && r.getWidth() >= 48.0f)
            {
                const float barW = juce::jmin (24.0f, r.getWidth() * 0.30f);
                g.setColour ((s_knobStyle == 6 ? Palette::metalHi : Palette::inkSoft).withAlpha (over ? 0.78f : 0.42f));
                g.fillRoundedRectangle (r.getCentreX() - barW * 0.5f, r.getBottom() - 3.2f, barW, 2.0f, 1.0f);
            }
            return;
        }

        // ---- ★ Red Dot 精密オプティカル・スライドレール＆ジュエルLEDスイッチ (左端 kLampBox = 20px 内) ----
        const float slotW = 16.5f;
        const float slotH = juce::jmin (10.5f, r.getHeight() - 8.0f);
        const float cx    = r.getX() + 3.5f + kLampBox * 0.5f;
        const float cy    = r.getCentreY();
        const juce::Rectangle<float> slot (cx - slotW * 0.5f, cy - slotH * 0.5f, slotW, slotH);

        // スライドレールの彫り込み溝
        if (s_useKawaii)
        {
            g.setColour (on ? col.darker (0.28f).withAlpha (0.88f) : Palette::track.darker (0.06f).withAlpha (0.90f));
            g.fillRoundedRectangle (slot, slotH * 0.5f);
            g.setColour (on ? juce::Colours::white.withAlpha (0.75f) : Palette::panelLn.withAlpha (0.75f));
            g.drawRoundedRectangle (slot, slotH * 0.5f, 1.1f);
        }
        else
        {
            g.setColour (on ? col.darker (0.52f).withAlpha (0.88f) : Palette::bgTop.darker (0.50f).withAlpha (0.88f));
            g.fillRoundedRectangle (slot, slotH * 0.5f);
            g.setColour (on ? Palette::readableOn (col).withAlpha (0.62f)
                            : (s_knobStyle == 6 ? Palette::metalMid.withAlpha (0.65f) : Palette::inkSoft.withAlpha (0.48f)));
            g.drawRoundedRectangle (slot, slotH * 0.5f, 1.1f);
        }

        // スライドノブ（OFF=左端 [●──] スモークジュエルレンズ、ON=右端 [──●] 高輝度発光ジュエルへ物理スライド！）
        const float thumbD = slotH - 2.0f;
        const float thumbX = on ? (slot.getRight() - thumbD - 1.1f) : (slot.getX() + 1.1f);
        const juce::Rectangle<float> thumb (thumbX, cy - thumbD * 0.5f, thumbD, thumbD);
        if (on)
        {
            const auto lampCol = s_useKawaii ? juce::Colours::white : Palette::readableOn (col);
            if (s_knobStyle != 5)
            {
                g.setColour (lampCol.withAlpha (0.38f));
                g.fillEllipse (thumb.expanded (2.4f));
            }
            g.setColour (lampCol);
            g.fillEllipse (thumb);
            if (s_knobStyle != 5)
            {
                g.setColour (juce::Colours::white.withAlpha (0.92f));
                g.fillEllipse (thumb.getX() + thumbD * 0.22f, thumb.getY() + thumbD * 0.18f, thumbD * 0.34f, thumbD * 0.34f);
            }
        }
        else
        {
            const auto offRim = (s_knobStyle == 6) ? Palette::metalHi.withAlpha (0.82f)
                              : s_useKawaii        ? Palette::inkSoft.withAlpha (0.68f)
                                                   : Palette::inkSoft.withAlpha (0.80f);
            g.setColour (offRim);
            g.fillEllipse (thumb);
            g.setColour (s_useKawaii ? juce::Colours::white.withAlpha (0.90f)
                       : (s_knobStyle == 6 ? Palette::mirrorTop.withAlpha (0.90f) : Palette::panel2.brighter (0.15f)));
            g.fillEllipse (thumb.reduced (1.6f));
            g.setColour (juce::Colours::white.withAlpha (0.65f));
            g.fillEllipse (thumb.getX() + thumbD * 0.24f, thumb.getY() + thumbD * 0.20f, thumbD * 0.28f, thumbD * 0.28f);
        }
    }

    // v2.12.0 ランプのぶんだけ文字を右へ寄せる。
    // ★JUCE の既定はボタンの中央に置くので、ランプと重なって読めなくなる。
    void drawButtonText (juce::Graphics& g, juce::TextButton& b,
                         bool /*over*/, bool /*down*/) override
    {
        auto area = b.getLocalBounds()
                     .withTrimmedLeft  (juce::roundToInt (lampWidthFor (b)) + 4)
                     .withTrimmedRight (4);
        // ★縮小許容は 0.92 まで。これ以上詰めると「…」で省略されてしまう。
        //   入り切らないのは箱が狭いということなので、まず箱のほうを直す
        //   (buttonWidthFor で実測する)。それでも狭いときの最後の砦が fitFont で、
        //   文字だけ小さくして「…」にはしない。
        const auto f = fitFont (getTextButtonFont (b, b.getHeight()),
                                b.getButtonText(), area.getWidth());
        g.setFont (f);
        g.setColour ((b.getToggleState() ? Palette::readableOn (b.findColour (juce::TextButton::buttonOnColourId))
                                        : b.findColour (juce::TextButton::textColourOffId))
                       .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText (b.getButtonText(), area, juce::Justification::centred, 1, 0.92f);
    }

    // v2.12.0 「箱が文字に合わせる」ための実測。ボタンの中身が必ず収まる幅を返す。
    //  文字サイズを上げたときに「Ba…」「トー…」と省略されるのを、**寸法の側**で断つ。
    int buttonWidthFor (juce::TextButton& b, int h, int extraPad = 26)
    {
        const int textW = stringWidth (getTextButtonFont (b, h), b.getButtonText());
        return textW + extraPad + (b.getClickingTogglesState() ? (int) kLampBox + 4 : 0);
    }

    // ---- Larger, wrapped tooltips (fixes "description text too small") ----
    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos,
                                           juce::Rectangle<int> parentArea) override
    {
        const float fh = 14.5f * juce::jmax (1.0f, fontScale);
        const int   maxW = juce::jmin (420, parentArea.getWidth() - 24);
        juce::AttributedString s;
        s.append (tip, uiFont (fh, false));
        juce::TextLayout tl;
        tl.createLayout (s, (float) (maxW - tipPad * 2));

        const int w = juce::jmin (maxW, (int) std::ceil (tl.getWidth()) + tipPad * 2);
        const int h = (int) std::ceil (tl.getHeight()) + tipPad * 2;

        const int xLo = parentArea.getX();
        const int xHi = juce::jmax (xLo, parentArea.getRight() - w);
        const int yLo = parentArea.getY();
        const int yHi = juce::jmax (yLo, parentArea.getBottom() - h);
        auto x = juce::jlimit (xLo, xHi, pos.x - w / 2);
        auto y = pos.y > parentArea.getCentreY() ? pos.y - (h + 14) : pos.y + 22;
        y = juce::jlimit (yLo, yHi, y);
        return { x, y, w, h };
    }

    void drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height) override
    {
        juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);
        g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (findColour (juce::TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        const float fh = 14.5f * juce::jmax (1.0f, fontScale);
        juce::AttributedString s;
        s.append (text, uiFont (fh, false), findColour (juce::TooltipWindow::textColourId));
        s.setLineSpacing (2.0f);
        juce::TextLayout tl;
        tl.createLayout (s, (float) (width - tipPad * 2));
        tl.draw (g, bounds.reduced ((float) tipPad).withTrimmedTop (2.0f));
    }

private:
    float fontScale { 1.0f };
    static constexpr int tipPad = 10;
    juce::Colour themeArc { 0xff5ce8bc };   // v1.7.0: current knob-arc accent
    static juce::Typeface::Ptr s_kawaiiFace;   // embedded rounded font (yuru-kawa)
    static std::atomic<int>    s_instances;    // v2.3.0: 最後の1つが消えたらフォントを手放す
    static bool s_useKawaii;
    static int  s_knobStyle;                   // 0=standard, 1=juice, 2=liver, 3=studio, 4=neon
    inline static int s_accessMode = 0;        // 0=標準デザイン, 1=◉くっきり大文字, 2=★かんたん3ステップ, 3=●マット・具体表記
    static float s_juicePhase;                 // juice bubble/wobble animation phase
};

//==============================================================================
// Pitch stability view + meters. Analysis only; audio is never delayed.
// The centrepiece is a scrolling pitch-history graph: instead of a fast-moving
// needle, it plots the last few seconds of cents-error so the singer can SEE
// whether they are holding pitch steadily. Green centre band = in tune.
class VocalTuner : public juce::Component, private juce::Timer
{
    std::shared_ptr<tip::Language> language;
public:
    explicit VocalTuner (VocalGzzioProcessor& p, std::shared_ptr<tip::Language> language = std::make_shared<tip::Language>());

    void paint   (juce::Graphics&) override;
    void resized ()                override;

private:
    void timerCallback() override;
    void analyse();
    void clearPitchDisplay();
    void updatePitchDisplay();
    bool isInstrumentMode() const { return tuningMode != 0; }
    int stringCount() const { return tuningMode == 3 ? 5 : tuningMode == 2 ? 4 : 6; }
    int stringNote (int index) const;
    bool displayInTune() const { return hasPitch && pitchStable && std::abs (dispCents) <= 5.0f && std::abs (cents) <= 6.0f; }
    int controlHeight() const;

    VocalGzzioProcessor& proc;
    std::vector<float>   buffer;
    std::vector<float>   analysisBuffer;
    juce::ComboBox       refPitchBox, tunerModeBox, guitarStringBox;
    bool guitarMode { false }, rangeToolsVisible { false };
    int tuningMode { 0 }; // voice / guitar / four-string bass / five-string bass
    int guitarString { 0 }, detectedString { 0 }, displayMidi { -1 };
    inline static constexpr int guitarNotes[6] { 40, 45, 50, 55, 59, 64 };
    inline static constexpr int bassNotes[5] { 23, 28, 33, 38, 43 };
    std::array<double, 1024> difference {}, normalisedDifference {};
    std::array<float, 5> recentCents {};
    int recentCount { 0 }, recentPosition { 0 }, pendingMidi { -1 }, pendingCount { 0 };
    bool pitchStable { false };

    juce::String noteName { "--" };
    float cents { 0 }, freq { 0 };
    bool  hasPitch { false };
    int   lastMidi { -1 };                 // last detected MIDI note (for note-rail + range)
    float dispCents { 0 }, dispFreq { 0 };
    int   hold { 0 };
    int   activeHistFrames { 0 };
    bool  idlePainted { false };
    float dispIn { 0 }, dispOut { 0 }, dispGR { 0 }, dispDS { 0 }, dispDN { 0 };

    // ---- v1.6.0 gear meter (from the mixer-app motif): a brass + mint gear
    //      pair spins at a speed proportional to the output level. Drawn inside
    //      the meter zone, which already repaints at 30 Hz -> ~zero extra cost.
    juce::Path  gearBig, gearSmall;
    float       gearAngle { 0.0f };          // degrees
    juce::uint32 gearLastMs { 0 };

    // Scrolling pitch history: cents error over time, newest at the right.
    static constexpr int histLen = 240;   // ~8 s at 30 Hz
    float histCents[histLen] = {};
    bool  histValid[histLen] = {};
    int   histMidi[histLen]  = {};        // note-rail: absolute pitch per frame
    int   histPos { 0 };

    // ---- v1.4.0 note rail + vocal-range check ----
    bool  railMode { false };             // false = classic cents strip, true = scale-lane note rail
    int   rangeLo { 127 }, rangeHi { -1 }; // captured min/max MIDI while checking
    bool  rangeChecking { false };
    juce::TextButton railToggle, rangeButton;

    // v1.4.0 P5: YIN pitch confidence (0..1) and held-note gating for range capture.
    // A single glitch frame must never corrupt the min/max, so we only extend the
    // range once the SAME note has been held steadily for a few frames.
    float pitchConf { 0.0f };             // 1 - YIN CMND at the chosen lag
    int   rangeCand { -1 };               // note currently being held as a candidate
    int   rangeCandCount { 0 };           // consecutive stable frames of rangeCand
    // plausible sung range clamp (E2..E6): rejects octave/harmonic detection errors
    static constexpr int kRangeLoMidi = 40;   // E2
    static constexpr int kRangeHiMidi = 88;   // E6

public:
    // MIDI -> singer-community notation (C4 = mid2C, A4 = hiA; A is the zone boundary)
    static juce::String midiToJp (int midi);
    void toggleRail()   { railMode = ! railMode; railToggle.setToggleState (railMode, juce::dontSendNotification); repaint(); }
    void setRailMode (bool r) { railMode = r; railToggle.setToggleState (r, juce::dontSendNotification); repaint(); }
    // v1.9.1: 言語切替でチューナー内のボタン文言も貼り替える（従来は生成時のみで日本語のまま）
    void refreshLanguage();
    bool isRailMode() const   { return railMode; }
    void setGuitarMode (bool enabled);
    bool isGuitarMode() const { return guitarMode; }
    void setTuningMode (int mode);
    int getTuningMode() const { return tuningMode; }
    int getTuningString() const { return guitarString; }
    void setGuitarString (int stringChoice);
   #if VOCALGZZIO_TESTING
    float measureForTest() { analyse(); return hasPitch ? freq : 0.0f; }
    int detectedStringForTest() const { return detectedString; }
    float centsForTest() const { return cents; }
    void advanceDisplayForTest() { updatePitchDisplay(); }
    float displayCentsForTest() const { return dispCents; }
    bool stableForTest() const { return pitchStable; }
    bool inTuneForTest() const { return displayInTune(); }
    int displayNoteForTest() const { return displayMidi; }
   #endif
    void setRangeToolsVisible (bool v)   // note-rail toggle + range check (advanced only)
    {
        rangeToolsVisible = v;
        railToggle .setVisible (v && ! isInstrumentMode());
        rangeButton.setVisible (v && ! isInstrumentMode());
        if (! v) { railMode = false; railToggle.setToggleState (false, juce::dontSendNotification);
                   if (rangeChecking) stopRange(); }
        resized();
    }
    std::function<void (bool)> onRailChange;   // persist rail toggle across editor recreation
    void startRange()   { rangeChecking = true; rangeLo = 127; rangeHi = -1;
                          rangeCand = -1; rangeCandCount = 0; repaint(); }
    void stopRange()    { rangeChecking = false; repaint(); }
    bool isRangeChecking() const { return rangeChecking; }
    juce::String rangeResultText() const;
    void setLiteMode (bool lite) { startTimerHz (lite ? 12 : 30); }

};

//==============================================================================
// EQ graph: log-frequency (20 Hz - 20 kHz) x dB (-18..+18) display.
// Layers: output spectrum (FFT of the analyzer ring) -> auto-cut region ->
// static corrective curve -> live curve (static + current smart-EQ cuts).
// Display only: it reads parameter values / atomics and never touches audio.
class EQGraph : public juce::Component, private juce::Timer
{
    std::shared_ptr<tip::Language> language;
public:
    explicit EQGraph (VocalGzzioProcessor& p, std::shared_ptr<tip::Language> value = std::make_shared<tip::Language>()) : language (std::move (value)), proc (p)
    {
        timeData.resize ((size_t) VocalGzzioProcessor::analyzerSize, 0.0f);
        fftData .resize ((size_t) VocalGzzioProcessor::analyzerSize * 2, 0.0f);
        specDb  .resize ((size_t) VocalGzzioProcessor::analyzerSize / 2 + 1, -120.0f);
        startTimerHz (20);
    }

    void paint (juce::Graphics&) override;
    void resized() override { gridImage = juce::Image(); cachedPx = 0; }   // v1.5.0: invalidate grid & EQ curve cache

    // v1.7.0: run the FFT/analyzer only while actually visible. When the graph is
    // hidden (Effects tab, or the lightweight display modes) the timer stops, so no
    // FFT and no repaints happen — a real CPU saving.
    void visibilityChanged() override
    {
        if (isVisible() && ! liteMode) startTimerHz (20);
        else                           stopTimer();
    }

    void setLiteMode (bool lite)
    {
        liteMode = lite;
        if (liteMode || ! isVisible()) stopTimer();
        else                           startTimerHz (20);
        repaint();
    }
    bool isLiteMode() const noexcept { return liteMode; }

    // ---- v1.4.0 F6-style editing: drag manual bands on the graph ----
    // (freq = horizontal, depth = vertical, mouse wheel / double-tap = Q width)
    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove  (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    int  getDraggingBand() const noexcept { return dragBand; }
    int  hitTestBandPublic (juce::Point<float> p) const { return hitTestBand (p); }
    juce::Point<float> getBandNodePosPublic (int b) const { return bandNodePos (b); }

    // log freq <-> x, dB <-> y helpers (public so bands can reuse them later)
    float freqToX (float hz) const
    {
        const float lo = std::log10 (fMin), hi = std::log10 (fMax);
        return plot.getX() + (std::log10 (juce::jlimit (fMin, fMax, hz)) - lo) / (hi - lo) * plot.getWidth();
    }
    float xToFreq (float x) const
    {
        const float lo = std::log10 (fMin), hi = std::log10 (fMax);
        const float t = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
        return std::pow (10.0f, lo + t * (hi - lo));
    }
    float dbToY (float db) const
    {
        return plot.getCentreY() - (juce::jlimit (-dbMax, dbMax, db) / dbMax) * (plot.getHeight() * 0.5f);
    }
    float yToDb (float y) const
    {
        return juce::jlimit (-dbMax, dbMax,
                             (plot.getCentreY() - y) / juce::jmax (1.0f, plot.getHeight() * 0.5f) * dbMax);
    }

private:
    void timerCallback() override
    {
        if (liteMode || ! isShowing())
            return;
        if (specQuiet && proc.getInputLevel() < 0.0002f && proc.getOutputLevel() < 0.0002f)
            return;
        updateSpectrum();
        repaint();
    }
    void updateSpectrum();

    bool manualEditActive() const;                    // seq_on && manual mode
    int  hitTestBand (juce::Point<float>) const;      // node under the mouse, or -1
    juce::Point<float> bandNodePos (int b) const;     // node = (freq, -depth)
    void cycleBandQ (int b);                          // touch double-tap Q width cycle (1.4 -> 3.0 -> 6.0)
    int dragBand { -1 }, hoverBand { -1 };
    juce::uint32 lastEqTapMs { 0 };
    juce::Point<float> lastEqTapPos;
    bool liteMode { false };
    bool specQuiet { false };

    VocalGzzioProcessor& proc;
    juce::Rectangle<float> plot;         // inner plotting area (set in paint)
    juce::Image gridImage;               // v1.5.0: cached static grid (rebuilt on resize)

    juce::dsp::FFT fft { 12 };           // 2^12 = 4096 = analyzerSize
    juce::dsp::WindowingFunction<float> window { (size_t) VocalGzzioProcessor::analyzerSize,
                                                 juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> timeData, fftData, specDb;
    std::vector<float> cachedStatDb, liveDb;
    int    cachedPx     { 0 };
    double cachedSr     { 0.0 };
    float  cachedLowcut { -999.0f }, cachedMud { -999.0f }, cachedHarsh { -999.0f },
           cachedPres   { -999.0f }, cachedAir { -999.0f };

    static constexpr float fMin  = 20.0f;
    static constexpr float fMax  = 20000.0f;
    static constexpr float dbMax = 18.0f;
};

//==============================================================================
// ★ 指が太い人・タブレットPCタッチ操作対応スライダー (ClickToEditSlider):
//   1. 下から太い指でツマミを掴んだ際、指の腹が下部数値カプセル (textBoxArea) に重なっても
//      文字入力ボックス＆スクリーンキーボードが誤爆ポップアップせず、そのまま滑らかにドラッグ回転できる！
//   2. ドラッグせずに下部数値カプセルの「左端30%」をタップすると -1ステップ微調整、
//      「右端30%」をタップすると +1ステップ微調整、「中央40%」の静止クリック時のみ数値手入力を開く。
//   3. 指離し時のロール・ドリフト補正 (Touch Lift-Off Stabilization):
//      画面から太い指を離す直前(22〜85ms)の非対称な指腹ロールによる微小な値ズレを自動補正。
//   4. 太い指の2タップ許容 (Fat-Finger Double-Tap):
//      1回目と2回目のタップ位置が最大28px離れていても（380ms以内）確実に初期値リセットが発動！
class ClickToEditSlider : public juce::Slider
{
public:
    ClickToEditSlider() = default;

    std::function<void()> onSelect;

    void setTextBoxStyle (TextEntryBoxPosition newPosition, bool isReadOnly,
                          int textEntryBoxWidth, int textEntryBoxHeight)
    {
        if (getTextBoxPosition() == newPosition
            && (! isTextBoxEditable()) == isReadOnly
            && getTextBoxWidth() == textEntryBoxWidth
            && getTextBoxHeight() == textEntryBoxHeight)
            return;
        juce::Slider::setTextBoxStyle (newPosition, isReadOnly, textEntryBoxWidth, textEntryBoxHeight);
    }

    void stepByFineIncrement (int dir)
    {
        const double span = getMaximum() - getMinimum();
        double step = 1.0;
        if (span >= 1000.0)      step = 25.0;  // 周波数 (120..8000 Hz)
        else if (span >= 180.0)  step = 5.0;   // LowCut (20..250 Hz), Delay ms, BPM
        else if (span <= 36.0)   step = 0.5;   // dB ノブ (-12..+12 dB, -18..+18 dB, -6..+6 dB 等)
        else                     step = 2.0;   // % ノブ (0..100%) / ゲート (-80..-20 dB)
        const double nextVal = juce::jlimit (getMinimum(), getMaximum(), getValue() + (double) dir * step);
        setValue (nextVal, juce::sendNotificationSync);
        if (onSelect) onSelect();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onSelect) onSelect();

        const juce::uint32 nowMs = juce::Time::getMillisecondCounter();
        // 太い指のダブルタップ初期値リセット（最大28pxの座標ズレまで許容）
        if (isDoubleClickReturnEnabled()
            && lastTapMs > 0
            && (nowMs - lastTapMs) <= 380
            && e.getPosition().getDistanceFrom (lastTapPos) <= 28)
        {
            lastTapMs = 0;
            setValue (getDoubleClickReturnValue(), juce::sendNotificationSync);
            return;
        }

        downInTextBox = (getTextBoxPosition() != juce::Slider::NoTextBox
                         && textBoxArea().contains (e.getPosition()));
        downTimeMs    = nowMs;
        draggedFar    = false;
        histCount     = 0;
        histHead      = 0;
        pushHistory (nowMs, getValue());

        // ★ mouseDown 時点では showTextBox() を呼ばず、常に Slider::mouseDown へ渡すことで
        //    太い指が下部数値カプセルにかかった状態からのドラッグ回転を100%保証する！
        juce::Slider::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() >= 4)
            draggedFar = true;
        juce::Slider::mouseDrag (e);
        pushHistory (juce::Time::getMillisecondCounter(), getValue());
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const juce::uint32 nowMs = juce::Time::getMillisecondCounter();

        // ★ 指離し時のロール・ドリフト補正 (Touch Lift-Off Stabilization)
        if (draggedFar && (nowMs - downTimeMs) >= 160 && histCount >= 2)
        {
            const double curVal = getValue();
            const double span   = juce::jmax (0.001, getMaximum() - getMinimum());
            for (int i = 0; i < histCount; ++i)
            {
                const auto& h = hist[(size_t) ((histHead - 1 - i + kHistCap) % kHistCap)];
                const juce::uint32 age = nowMs - h.ms;
                if (age >= 22 && age <= 85)
                {
                    const double diff = std::abs (curVal - h.val);
                    if (diff > 0.0 && diff <= span * 0.035)
                        setValue (h.val, juce::sendNotificationSync);
                    break;
                }
            }
        }

        juce::Slider::mouseUp (e);

        // ドラッグせずに静止タップされた場合の処理
        if (! draggedFar && e.getDistanceFromDragStart() < 4 && (nowMs - downTimeMs) <= 340)
        {
            lastTapMs  = nowMs;
            lastTapPos = e.getPosition();

            if (downInTextBox)
            {
                const auto tb    = textBoxArea();
                const int  relX  = e.x - tb.getX();
                const int  sideW = juce::jmax (14, tb.getWidth() * 30 / 100);
                if (relX < sideW)
                    stepByFineIncrement (-1);
                else if (relX >= tb.getWidth() - sideW)
                    stepByFineIncrement (+1);
                else
                    showTextBox();
            }
        }
    }

    juce::Rectangle<int> getTextBoxAreaPublic() const { return textBoxArea(); }

private:
    struct Sample { juce::uint32 ms { 0 }; double val { 0.0 }; };
    static constexpr int kHistCap = 8;
    Sample       hist[kHistCap];
    int          histHead  { 0 };
    int          histCount { 0 };
    bool         downInTextBox { false };
    bool         draggedFar    { false };
    juce::uint32 downTimeMs    { 0 };
    juce::uint32 lastTapMs     { 0 };
    juce::Point<int> lastTapPos;

    void pushHistory (juce::uint32 ms, double val) noexcept
    {
        hist[(size_t) histHead] = { ms, val };
        histHead = (histHead + 1) % kHistCap;
        if (histCount < kHistCap) ++histCount;
    }

    juce::Rectangle<int> textBoxArea() const
    {
        // Mirror the layout for each text-box position.
        switch (getTextBoxPosition())
        {
            case juce::Slider::TextBoxRight: return getLocalBounds().removeFromRight (getTextBoxWidth());
            case juce::Slider::TextBoxLeft:  return getLocalBounds().removeFromLeft  (getTextBoxWidth());
            case juce::Slider::TextBoxAbove: return getLocalBounds().removeFromTop    (getTextBoxHeight());
            default:                         return getLocalBounds().removeFromBottom (getTextBoxHeight());
        }
    }
};

//==============================================================================
// Small red LED placed at the centre of a knob. Lit = module active; click to
// bypass the whole module (lamp goes dark). Wired to a bool parameter.
class LampButton : public juce::Button
{
public:
    LampButton() : juce::Button ({}) { setClickingTogglesState (true); }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        const auto c   = r.getCentre();
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 1.0f;
        const float ir = rad * 0.54f;
        const auto accent = getToggleState() ? Palette::green : Palette::inkSoft;
        g.setColour (Palette::panel);
        g.fillEllipse (r.reduced (0.5f));
        g.setColour (accent.withAlpha (over ? 0.40f : 0.20f));
        g.fillEllipse (r.reduced (0.5f));
        g.setColour (accent);
        juce::Path power;
        power.addCentredArc (c.x, c.y, ir, ir, 0.0f, 0.65f,
                             juce::MathConstants<float>::twoPi - 0.65f, true);
        g.strokePath (power, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        g.drawLine (c.x, c.y - ir - 1.0f, c.x, c.y, 1.6f);
    }
};

//==============================================================================
//==============================================================================
// v1.7.0 theme backdrop. Drawn at the very start of Content::paint (a paint
// helper, NOT a child component) so the header bar and cards render on top of
// it. Yuru-kawa is a fully STATIC image (soft mint gradient + a few bokeh dots
// + the mascot) -> zero extra repaints, safe for a real-time plug-in.
class ThemePainter
{
public:
    void setMascot (const juce::Image& img) { mascot = img; }
    void setBounds (juce::Rectangle<int> b) { area = b; }
    void setMode   (int m, juce::Rectangle<int> b) { mode = m; area = b; }

    // returns true if a themed background was painted (caller then skips its own)
    bool paint (juce::Graphics& g)
    {
        if (mode == 1) { paintYuruKawa  (g); return true; }
        if (mode == 8) { paintYuruNight (g); return true; }   // v2.11.0 ゆるふわ・よる
        if (mode == 2) { paintNature    (g); return true; }
        if (mode == 3) { paintBrand     (g); return true; }
        if (mode == 4) { paintAurora    (g); return true; }
        if (Palette::isCoutureTheme (mode))   { paintVLiver    (g); return true; }   // クチュール・コスメ5色展開
        if (Palette::isOceanStarTheme (mode)) { paintOceanStar (g); return true; }   // 🌌🌊 星空・海・黄昏コレクション5色展開
        if (mode == 6) { paintCyberNeon (g); return true; }   // 配信サイバーネオン
        if (mode == 9) { paintStudioPro (g); return true; }   // プロスタジオ・コンソール
        return false;
    }

private:
    void paintYuruKawa (juce::Graphics& g)
    {
        if (! yuruCache.isValid()
            || yuruCacheMode != 1
            || yuruCache.getWidth()  != area.getWidth()
            || yuruCache.getHeight() != area.getHeight())
        {
            yuruCache = juce::Image (juce::Image::RGB,
                                     juce::jmax (1, area.getWidth()),
                                     juce::jmax (1, area.getHeight()), false);
            yuruCacheMode = 1;
            juce::Graphics cg (yuruCache);
            auto r = area.toFloat().withZeroOrigin();

            // milky cream base
            cg.fillAll (juce::Colour (0xfffbf7f1));

            // subtle WATERCOLOUR blooms: radial gradients fading to transparent, so the
            // colour bleeds softly with no hard edge. Low alpha keeps it light/washy.
            struct Bloom { float x, y, rad; juce::uint32 col; };
            static const Bloom blooms[] = {
                { 0.12f, 0.15f, 0.34f, 0x26afddcb },   // mint
                { 0.82f, 0.10f, 0.30f, 0x24f4b4c2 },   // sakura
                { 0.92f, 0.60f, 0.40f, 0x20aec9e4 },   // sky blue
                { 0.28f, 0.84f, 0.34f, 0x1cf0d27a },   // butter
                { 0.55f, 0.42f, 0.40f, 0x16f4b4c2 },   // soft pink centre
                { 0.04f, 0.58f, 0.30f, 0x1cafddcb }    // mint 2
            };
            const float rad = juce::jmax (r.getWidth(), r.getHeight());
            for (auto& b : blooms)
            {
                const float cx = r.getX() + r.getWidth()  * b.x;
                const float cy = r.getY() + r.getHeight() * b.y;
                const float rr = rad * b.rad;
                juce::Colour c (b.col);
                juce::ColourGradient grad (c, cx, cy, c.withAlpha (0.0f), cx + rr, cy, true);
                cg.setGradientFill (grad);
                cg.fillEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f);
            }
        }
        g.drawImageAt (yuruCache, area.getX(), area.getY());
    }

    // v2.11.0 ゆるふわ・よる。昼版と同じく**完全に静止画**（アニメなし）。
    // 夜中に配信・録音する人が、クリーム地のまぶしさで目をやられないための版。
    // 星は乱数を使わず固定の漸化式で置くので、毎回まったく同じ絵になる
    // （＝再描画のたびに星がチラつかない。確保も一切しない）。
    void paintYuruNight (juce::Graphics& g)
    {
        if (! yuruCache.isValid()
            || yuruCacheMode != 8
            || yuruCache.getWidth()  != area.getWidth()
            || yuruCache.getHeight() != area.getHeight())
        {
            yuruCache = juce::Image (juce::Image::RGB,
                                     juce::jmax (1, area.getWidth()),
                                     juce::jmax (1, area.getHeight()), false);
            yuruCacheMode = 8;
            juce::Graphics cg (yuruCache);
            auto r = area.toFloat().withZeroOrigin();

            // 夜空。上ほど深く、下は街あかりでほんの少し暖かい
            cg.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1729), r.getX(), r.getY(),
                                                      juce::Colour (0xff241d31), r.getX(), r.getBottom(),
                                                      false));
            cg.fillRect (r);

            // 星。奥(小さく暗い)から手前(大きく明るい)へ3層
            {
                juce::uint32 s = 0x9e3779b9u;                     // 固定の種
                auto rnd = [&s] { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 16777216.0f; };
                for (int layer = 0; layer < 3; ++layer)
                {
                    const int   n   = 34 - layer * 8;
                    const float dia = 1.1f + (float) layer * 0.7f;
                    cg.setColour (juce::Colours::white.withAlpha (0.10f + 0.09f * (float) layer));
                    for (int i = 0; i < n; ++i)
                    {
                        const float x = r.getX() + r.getWidth()  * rnd();
                        const float y = r.getY() + r.getHeight() * rnd() * 0.82f;   // 下は空けておく
                        cg.fillEllipse (x, y, dia, dia);
                    }
                }
            }

            // お月さま（右上）。かさ(halo)を先に敷いて、その上に丸を置く
            {
                const float mr = juce::jmax (r.getWidth(), r.getHeight()) * 0.052f;
                const float cx = r.getX() + r.getWidth() * 0.895f;
                const float cy = r.getY() + r.getHeight() * 0.155f;
                const float hr = mr * 4.2f;
                juce::ColourGradient halo (juce::Colour (0x3af3e6b8), cx, cy,
                                           juce::Colour (0x00f3e6b8), cx + hr, cy, true);
                cg.setGradientFill (halo);
                cg.fillEllipse (cx - hr, cy - hr, hr * 2.0f, hr * 2.0f);
                cg.setColour (juce::Colour (0xfff5ead0));
                cg.fillEllipse (cx - mr, cy - mr, mr * 2.0f, mr * 2.0f);
                // うっすらクレーター（べた塗りの円に見えないように）
                cg.setColour (juce::Colour (0x18a08a5a));
                cg.fillEllipse (cx - mr * 0.42f, cy - mr * 0.30f, mr * 0.44f, mr * 0.44f);
                cg.fillEllipse (cx + mr * 0.10f, cy + mr * 0.22f, mr * 0.30f, mr * 0.30f);
            }

            // 昼版と同じ水彩のにじみ。夜なので色を寄せて、alpha もぐっと落とす
            struct Bloom { float x, y, rad; juce::uint32 col; };
            static const Bloom blooms[] = {
                { 0.12f, 0.15f, 0.34f, 0x1c6fb9a2 },   // 夜のミント
                { 0.82f, 0.10f, 0.30f, 0x18c07a92 },   // 夜のさくら
                { 0.92f, 0.60f, 0.40f, 0x186f8ec0 },   // 夜の空色
                { 0.28f, 0.84f, 0.34f, 0x14c0a45a },   // 夜のバター
                { 0.55f, 0.42f, 0.40f, 0x12c07a92 },
                { 0.04f, 0.58f, 0.30f, 0x146fb9a2 }
            };
            const float rad = juce::jmax (r.getWidth(), r.getHeight());
            for (auto& b : blooms)
            {
                const float cx = r.getX() + r.getWidth()  * b.x;
                const float cy = r.getY() + r.getHeight() * b.y;
                const float rr = rad * b.rad;
                juce::Colour c (b.col);
                juce::ColourGradient grad (c, cx, cy, c.withAlpha (0.0f), cx + rr, cy, true);
                cg.setGradientFill (grad);
                cg.fillEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f);
            }
        }
        g.drawImageAt (yuruCache, area.getX(), area.getY());
    }

    // ==================================================================
    // v1.8.1 themed scenery — 「でっかく・分かりやすく」全面刷新
    //  自然(2): 四季40秒サイクル(2秒クロスフェード)。象徴モチーフを大きく透過配置
    //  ブランド(3): モノグラム・キャンバス様式(組みイニシャル+四弁花+星の規則配置)
    //              +ダミエ(市松)帯+大型紋章 — 実在ロゴは商標のため自作紋章
    //  ライブ(4): 大型トラス/ムービングライト/ラインアレイ/サブ/ウェッジ
    //  ペンライトの海は「5人ユニゾンON」の演出として全テーマ共通 (jnActive)
    // ==================================================================
public:
    float animT = 0.0f;
    bool  jnActive = false;
    float level = 0.0f;                      // 平滑入力レベル 0..1 (声連動演出)
    int   mouseX = -9999, mouseY = -9999;    // カーソル(コンテンツ座標)
    bool  freezeTime = false;                // GZ_SEASON固定時: animTを進めない(検証用)

    // ---- パーティクル (花びら/落ち葉/雪): カーソルで押し流せる ----
    struct Pt { float x, y, vx, vy, rot, vr, size; int kind; };
    std::vector<Pt> parts;
    void seedParts (int season)
    {
        parts.clear();
        const int n = season == 3 ? 42 : 26;
        for (int i = 0; i < n; ++i)
        {
            Pt q {};
            q.x = area.getX() + hash01 (i * 7 + 1) * area.getWidth();
            q.y = area.getY() + hash01 (i * 7 + 2) * area.getHeight();
            q.vx = 0; q.vy = 0;
            q.rot = hash01 (i * 7 + 3) * 6.28f;
            q.vr  = (hash01 (i * 7 + 4) - 0.5f) * 2.2f;
            if (season == 0)      { q.kind = 0; q.size = 6 + 6 * hash01 (i); }        // 花びら
            else if (season == 2) { q.kind = (i % 3 == 2) ? 2 : 1;                     // 紅葉/いちょう
                                    q.size = 12 + 9 * hash01 (i); }
            else                  { q.kind = (i % 7 == 0) ? 4 : 3;                     // 雪/結晶
                                    q.size = q.kind == 4 ? 12 + 6 * hash01 (i)
                                                         : 2.5f + 3.5f * hash01 (i); }
            parts.push_back (q);
        }
        partSeason = season;
    }
    int partSeason = -1;
    void update (float dt, int mx, int my, float lv)
    {
        if (! freezeTime) animT += dt;       // GZ_SEASON固定時はanimTを止める
        level += (lv - level) * 0.25f;
        mouseX = mx; mouseY = my;
        if (mode != 2) return;
        const int season = currentSeason();
        if (season == 1) { parts.clear(); partSeason = 1; return; }   // 夏は波のみ
        if (partSeason != season || parts.empty()) seedParts (season);
        const float W = (float) area.getWidth();
        for (auto& q : parts)
        {
            // 基本挙動: ゆっくり落下 + 横揺れ
            const float sway = std::sin (animT * 0.8f + q.rot * 3.0f);
            float ax = sway * (q.kind == 3 ? 3.0f : 8.0f);
            float ay = (q.kind == 0 ? 26.0f : q.kind == 3 ? 22.0f : q.kind == 4 ? 14.0f : 30.0f);
            // カーソルで押し流す (半径120px)
            const float dx = q.x - (float) mouseX, dy = q.y - (float) mouseY;
            const float d2 = dx*dx + dy*dy;
            if (d2 < 120.0f * 120.0f && d2 > 1.0f)
            {
                const float d = std::sqrt (d2), f = (120.0f - d) / 120.0f * 1800.0f / d;
                q.vx += dx * f * dt;  q.vy += dy * f * dt;
            }
            q.vx += (ax - q.vx) * dt * 1.6f;
            q.vy += (ay - q.vy) * dt * 1.6f;
            q.x += q.vx * dt;  q.y += q.vy * dt;
            q.rot += q.vr * dt * (1.0f + std::abs (q.vx) * 0.02f);
            if (q.y > area.getBottom() + 24) { q.y = area.getY() - 20; q.x = area.getX() + hash01 ((int)(q.x*13)) * W; q.vy = 0; }
            if (q.x < area.getX() - 30)  q.x += W + 60;
            if (q.x > area.getRight() + 30) q.x -= W + 60;
        }
    }
    void drawParts (juce::Graphics& g, float a)
    {
        for (auto& q : parts)
        {
            switch (q.kind)
            {
                case 0:
                {
                    g.setColour (juce::Colour (0xffff9fc0).withAlpha (0.95f * a));
                    juce::Path pt; pt.addEllipse (-q.size, -q.size*0.62f, q.size*2, q.size*1.24f);
                    g.fillPath (pt, juce::AffineTransform::rotation (q.rot).translated (q.x, q.y));
                    break;
                }
                case 1: drawMaple (g, q.x, q.y, q.size, q.rot,
                                   (((int) (q.size*10)) % 2 == 0 ? juce::Colour (0xffd9503a)
                                                                 : juce::Colour (0xffe8862e))
                                       .withAlpha (a)); break;
                case 2: drawGinkgo (g, q.x, q.y, q.size, q.rot, a); break;
                case 4: drawFlake  (g, q.x, q.y, q.size, q.rot, 0.95f * a, true); break;
                default:
                    g.setColour (juce::Colour (0xff8aa4c4).withAlpha (0.55f * a));
                    g.fillEllipse (q.x-0.9f, q.y-0.9f, q.size+1.8f, q.size+1.8f);
                    g.setColour (juce::Colours::white.withAlpha (0.9f * a));
                    g.fillEllipse (q.x, q.y, q.size, q.size);
            }
        }
    }

    static float hash01 (int i)
    {
        const float s = std::sin ((float) i * 12.9898f + 4.1414f) * 43758.547f;
        return s - std::floor (s);
    }
    int currentSeason() const { return (int) (std::fmod (animT, 60.0f) / 15.0f) % 4; }

    void paintNature (juce::Graphics& g)
    {
        const float len = 15.0f, xf = 2.0f;   // v1.8.3: 季節サイクル高速化(15秒/季)
        const float t  = std::fmod (animT, len * 4.0f);
        const int   s  = (int) (t / len) % 4;
        const float ph = std::fmod (t, len);
        paintSeason (g, s, 1.0f);
        if (ph > len - xf) paintSeason (g, (s + 1) % 4, (ph - (len - xf)) / xf);
    }
    void paintSeason (juce::Graphics& g, int season, float alpha)
    {
        auto r = area.toFloat();
        const bool layer = alpha < 0.999f;
        if (layer) g.beginTransparencyLayer (alpha);
        switch (season)
        {
            case 0:  paintSpring (g, r); break;
            case 1:  paintSummer (g, r); break;
            case 2:  paintAutumn (g, r); break;
            default: paintWinter (g, r); break;
        }
        if (layer) g.endTransparencyLayer();
    }

    // ---- 共通: 大きな木 ----
    // ---- 春 ----
    void paintSpring (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setGradientFill ({ juce::Colour (0xffffd9e6), r.getCentreX(), r.getY(),
                             juce::Colour (0xfffff6ea), r.getCentreX(), r.getBottom(), false });
        g.fillRect (r);
        g.setColour (juce::Colour (0x4c9ed49e));
        g.fillEllipse (r.getX()-140, r.getBottom()-72, r.getWidth()*0.85f, 170);
        g.fillEllipse (r.getCentreX()-40, r.getBottom()-58, r.getWidth()*0.85f, 160);
    }
    void drawBlossom (juce::Graphics& g, float cx, float cy, float s, float a)
    {
        g.setColour (juce::Colour (0xffff9fc0).withMultipliedAlpha (a));
        for (int k = 0; k < 5; ++k)
        {
            const float an = juce::MathConstants<float>::twoPi * k / 5.0f - 1.5708f;
            g.fillEllipse (cx + std::cos(an)*s*0.55f - s*0.36f,
                           cy + std::sin(an)*s*0.55f - s*0.45f, s*0.72f, s*0.9f);
        }
        g.setColour (juce::Colour (0xffffe08a).withMultipliedAlpha (a));
        g.fillEllipse (cx - s*0.17f, cy - s*0.17f, s*0.34f, s*0.34f);
    }
    // ---- 夏 ----
    void paintSummer (juce::Graphics& g, juce::Rectangle<float> r)
    {
        // v1.8.4: UIパネルが下半分を覆うため、水平線を上部(見える帯)に置く。
        // 空は上26%、海は上26%〜下端まで。これで「空と海」が中段の隙間で明快に見える。
        const float hz = r.getY() + r.getHeight() * 0.26f;
        g.setGradientFill ({ juce::Colour (0xff5fb8f0), r.getCentreX(), r.getY(),
                             juce::Colour (0xffd8f0ff), r.getCentreX(), hz, false });
        g.fillRect (r.withBottom (hz));
        // 海: しっかり濃い青で「海」と分かる彩度に
        g.setGradientFill ({ juce::Colour (0xff0a6bb8), r.getCentreX(), hz,
                             juce::Colour (0xff1f92d8), r.getCentreX(), r.getBottom(), false });
        g.fillRect (r.withTop (hz));
        // 太陽 (空の右上)
        const float sx = r.getX() + r.getWidth() * 0.80f, sy = r.getY() + r.getHeight() * 0.10f;
        juce::Colour sun (0x77ffe9a0);
        g.setGradientFill ({ sun, sx, sy, sun.withAlpha (0.0f), sx + 180, sy, true });
        g.fillRect (r.withBottom (hz));
        g.setColour (juce::Colour (0xffffde6a)); g.fillEllipse (sx - 34, sy - 34, 68, 68);
        // 雲 (空を流れる)
        for (int c = 0; c < 2; ++c)
        {
            const float w = 150 + 60 * c;
            const float cx = std::fmod (animT * (7 + 4 * c) + c * 480.0f, r.getWidth() + w * 2) - w;
            const float cy = r.getY() + 30 + 40 * c;
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillEllipse (cx, cy, w * 0.55f, 34);
            g.fillEllipse (cx + w * 0.26f, cy - 16, w * 0.55f, 42);
            g.fillEllipse (cx + w * 0.52f, cy, w * 0.48f, 30);
        }
        // 帆船シルエット (海と分かる目印・ゆっくり横切る)
        {
            const float bx = std::fmod (animT * 9.0f + 200.0f, r.getWidth() + 220.0f) - 110.0f;
            const float by = hz + 24.0f;
            g.setColour (juce::Colour (0xcc0d3a5a));
            juce::Path hull; hull.startNewSubPath (bx - 34, by);
            hull.lineTo (bx + 34, by); hull.lineTo (bx + 24, by + 14);
            hull.lineTo (bx - 24, by + 14); hull.closeSubPath(); g.fillPath (hull);
            g.drawLine (bx, by, bx, by - 40, 2.5f);
            juce::Path sail; sail.startNewSubPath (bx + 2, by - 38);
            sail.lineTo (bx + 26, by - 6); sail.lineTo (bx + 2, by - 6); sail.closeSubPath();
            g.fillPath (sail);
            juce::Path jib; jib.startNewSubPath (bx - 2, by - 34);
            jib.lineTo (bx - 20, by - 6); jib.lineTo (bx - 2, by - 6); jib.closeSubPath();
            g.fillPath (jib);
        }
        // 海面の波: 濃い青の上に白い波頭を何本も (海だと一目で分かる)
        for (int wv = 0; wv < 9; ++wv)
        {
            const float y = hz + 22 + wv * (r.getHeight() * 0.74f / 9.0f);
            g.setColour (juce::Colours::white.withAlpha (0.55f - wv * 0.03f));
            juce::Path wp; wp.startNewSubPath (r.getX(), y);
            for (int sg = 1; sg <= 12; ++sg)
            {
                const float x1 = r.getX() + r.getWidth() * sg / 12.0f;
                const float ym = y + std::sin (animT * 0.8f + sg * 1.1f + wv * 1.7f) * 5;
                wp.quadraticTo (x1 - r.getWidth() / 24.0f, ym + 7, x1, ym);
            }
            g.strokePath (wp, juce::PathStrokeType (2.6f));
        }
        // 水面のきらめき (海全体)
        for (int i = 0; i < 60; ++i)
        {
            const float x = r.getX() + hash01 (i * 5 + 3) * r.getWidth();
            const float y = hz + 10 + hash01 (i * 5 + 4) * (r.getHeight() - (hz - r.getY()) - 10);
            const float tw = 0.5f + 0.5f * std::sin (animT * 2.2f + i * 2.1f);
            g.setColour (juce::Colours::white.withAlpha (0.45f * tw));
            g.fillEllipse (x, y, 3.0f, 1.6f);
        }
    }
    // ---- 秋 ----
    void paintAutumn (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setGradientFill ({ juce::Colour (0xffffdfae), r.getCentreX(), r.getY(),
                             juce::Colour (0xffffefd6), r.getCentreX(), r.getBottom(), false });
        g.fillRect (r);
        g.setColour (juce::Colour (0x55d9803a));
        g.fillEllipse (r.getX()-120, r.getBottom()-62, r.getWidth()*1.35f, 140);
    }
    void drawMaple (juce::Graphics& g, float cx, float cy, float s, float rot, juce::Colour col)
    {
        juce::Path p;
        for (int k = 0; k < 5; ++k)
        {
            const float an = -juce::MathConstants<float>::halfPi + (k - 2) * 0.62f;
            const float tx = std::cos(an)*s, ty = std::sin(an)*s;
            const float lx = std::cos(an-0.30f)*s*0.45f, ly = std::sin(an-0.30f)*s*0.45f;
            const float rx = std::cos(an+0.30f)*s*0.45f, ry = std::sin(an+0.30f)*s*0.45f;
            juce::Path lobe; lobe.startNewSubPath (0,0);
            lobe.lineTo (lx,ly); lobe.lineTo (tx,ty); lobe.lineTo (rx,ry); lobe.closeSubPath();
            p.addPath (lobe);
        }
        p.addRectangle (-s*0.05f, 0, s*0.1f, s*0.6f);
        g.setColour (col);
        g.fillPath (p, juce::AffineTransform::rotation (rot).translated (cx, cy));
    }
    void drawGinkgo (juce::Graphics& g, float cx, float cy, float s, float rot, float a)
    {
        juce::Path f; f.startNewSubPath (0, s*0.62f);
        f.lineTo (-s*0.08f, 0);
        f.addArc (-s, -s, s*2, s*2, -0.85f, -0.16f, false);
        f.lineTo (-s*0.03f, -s*0.1f);
        f.addArc (-s, -s, s*2, s*2, 0.16f, 0.85f, false);
        f.lineTo (s*0.08f, 0); f.closeSubPath();
        g.setColour (juce::Colour (0xffe8c23a).withMultipliedAlpha (a));
        g.fillPath (f, juce::AffineTransform::rotation (rot).translated (cx, cy));
    }
    // ---- 冬 (空を青灰に: 白い雪が「見える」) ----
    void paintWinter (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setGradientFill ({ juce::Colour (0xff9db6d0), r.getCentreX(), r.getY(),
                             juce::Colour (0xffdfeaf4), r.getCentreX(), r.getBottom(), false });
        g.fillRect (r);
        const float gy = r.getBottom() - r.getHeight()*0.12f;
        g.setColour (juce::Colour (0xfff2f7fc));
        g.fillRoundedRectangle (r.getX()-20, gy, r.getWidth()+40, r.getHeight()*0.14f+20, 18);
        g.setColour (juce::Colour (0x889db8d8));
        g.drawLine (r.getX(), gy+2, r.getRight(), gy+2, 2.5f);
    }
    void drawFlake (juce::Graphics& g, float cx, float cy, float s, float rot,
                    float alpha, bool outlined)
    {
        auto pass = [&] (juce::Colour c, float w)
        {
            g.setColour (c.withAlpha (alpha));
            for (int k = 0; k < 6; ++k)
            {
                const float an = rot + juce::MathConstants<float>::pi * k / 3.0f;
                const float dx = std::cos(an), dy = std::sin(an);
                g.drawLine (cx, cy, cx + dx*s, cy + dy*s, w);
                const float px = -dy, py = dx;
                for (float f : { 0.45f, 0.72f })
                {
                    g.drawLine (cx + dx*s*f, cy + dy*s*f,
                                cx + dx*s*(f+0.2f) + px*s*0.2f,
                                cy + dy*s*(f+0.2f) + py*s*0.2f, w*0.8f);
                    g.drawLine (cx + dx*s*f, cy + dy*s*f,
                                cx + dx*s*(f+0.2f) - px*s*0.2f,
                                cy + dy*s*(f+0.2f) - py*s*0.2f, w*0.8f);
                }
            }
            g.fillEllipse (cx - w, cy - w, w*2, w*2);
        };
        if (outlined) pass (juce::Colour (0xff5c7ca4), s*0.16f);
        pass (juce::Colours::white, s*0.085f);
    }
    // ---- ブランド (モノグラム・キャンバス様式 / 家紋様配置) ----
    void drawGZMono (juce::Graphics& g, float cx, float cy, float s, juce::Colour c, float a)
    {
        g.setColour (c.withMultipliedAlpha (a));
        juce::Path gp; gp.addArc (cx - s, cy - s, s*2, s*2, 0.65f, 5.9f, true);
        g.strokePath (gp, juce::PathStrokeType (s*0.30f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
        g.drawLine (cx + s*0.22f, cy + s*0.06f, cx + s*0.95f, cy + s*0.06f, s*0.30f);
        juce::Path z;
        z.startNewSubPath (cx - s*0.5f, cy - s*0.48f);
        z.lineTo (cx + s*0.5f, cy - s*0.48f); z.lineTo (cx - s*0.5f, cy + s*0.48f);
        z.lineTo (cx + s*0.5f, cy + s*0.48f);
        g.strokePath (z, juce::PathStrokeType (s*0.20f));
    }
    void drawNote8 (juce::Graphics& g, float cx, float cy, float s,
                    juce::Colour c, float a, bool flip)
    {   // 八分音符 (オリジナル・モチーフ)
        const float f = flip ? -1.0f : 1.0f;
        g.setColour (c.withMultipliedAlpha (a));
        juce::Path h; h.addEllipse (-s*0.52f, -s*0.34f, s*1.04f, s*0.68f);
        auto t = juce::AffineTransform::rotation (-0.35f * f)
                     .translated (cx - f*s*0.25f, cy + s*0.55f);
        g.fillPath (h, t);
        g.drawLine (cx + f*s*0.24f, cy + s*0.45f, cx + f*s*0.24f, cy - s*0.85f, s*0.16f);
        juce::Path fl; fl.startNewSubPath (cx + f*s*0.24f, cy - s*0.85f);
        fl.quadraticTo (cx + f*s*0.85f, cy - s*0.62f, cx + f*s*0.62f, cy - s*0.12f);
        g.strokePath (fl, juce::PathStrokeType (s*0.15f, juce::PathStrokeType::curved));
    }
    void drawLozenge (juce::Graphics& g, float cx, float cy, float s,
                      juce::Colour c, float a)
    {   // 菱形+中心点 (和の割り菱をアレンジ)
        juce::Path d;
        d.startNewSubPath (cx, cy - s);      d.lineTo (cx + s*0.72f, cy);
        d.lineTo (cx, cy + s);               d.lineTo (cx - s*0.72f, cy);
        d.closeSubPath();
        g.setColour (c.withMultipliedAlpha (a));
        g.strokePath (d, juce::PathStrokeType (s*0.15f));
        g.fillEllipse (cx - s*0.17f, cy - s*0.17f, s*0.34f, s*0.34f);
    }
    void drawMedallion (juce::Graphics& g, float cx, float cy, float s, float a)
    {   // 小メダリオン (円環+GZ) — 四隅用
        const juce::Colour gold (0xffe0c48c);
        g.setColour (juce::Colour (0xcc241b12).withMultipliedAlpha (a));
        g.fillEllipse (cx - s, cy - s, s*2, s*2);
        g.setColour (gold.withMultipliedAlpha (a));
        g.drawEllipse (cx - s, cy - s, s*2, s*2, s*0.12f);
        g.drawEllipse (cx - s*0.78f, cy - s*0.78f, s*1.56f, s*1.56f, s*0.05f);
        drawGZMono (g, cx, cy, s*0.42f, gold, a);
    }
    void drawMonoCanvas (juce::Graphics& g, juce::Rectangle<float> r, float a)
    {
        const juce::Colour beige (0xffe0c48c);   // v1.9.0: シャンパン
        const float cell = 66.0f;
        int row = 0;
        for (float y = r.getY() + 26; y < r.getBottom() + cell; y += cell*0.86f, ++row)
        {
            const float off = (row % 2) * cell * 0.5f;
            int col = 0;
            for (float x = r.getX() + off + 8; x < r.getRight() + cell; x += cell, ++col)
            {
                switch ((row + col*2) % 4)
                {
                    case 0:  drawGZMono  (g, x, y, 13.0f, beige, a);         break;
                    case 1:  drawNote8   (g, x, y, 11.0f, beige, a, false);  break;
                    case 2:  drawLozenge (g, x, y, 12.0f, beige, a);         break;
                    default: drawNote8   (g, x, y, 11.0f, beige, a, true);   break;
                }
            }
        }
    }
    // v1.9.0: 市松(ダミエ調)は既存ブランドを想起させるため、
    //          自社モチーフの「シェブロン織り」ストライプに置き換え。
    void drawWeaveBand (juce::Graphics& g, juce::Rectangle<float> band, float a)
    {
        const juce::Colour champagne (0xffe0c48c);
        const juce::Colour plum      (0xff2a2038);
        g.setColour (plum.withMultipliedAlpha (a));
        g.fillRect (band);

        const float h = band.getHeight();
        const float step = h * 0.9f;
        g.setColour (champagne.withMultipliedAlpha (0.55f * a));
        for (float x = band.getX() - h; x < band.getRight() + h; x += step)
        {
            juce::Path chev;                       // ＞ 型を並べた織り目
            chev.startNewSubPath (x,              band.getBottom());
            chev.lineTo          (x + h * 0.5f,   band.getCentreY());
            chev.lineTo          (x,              band.getY());
            g.strokePath (chev, juce::PathStrokeType (h * 0.16f));
        }
        g.setColour (champagne.withMultipliedAlpha (0.85f * a));
        g.drawRect (band, 1.2f);
    }
    void drawCrest (juce::Graphics& g, float cx, float cy, float s, float a)
    {
        const juce::Colour gold (0xffe0c48c);
        g.setColour (gold.withMultipliedAlpha (a));
        for (int side = -1; side <= 1; side += 2)
        {
            juce::Path br; br.startNewSubPath (cx + side*s*0.95f, cy + s*0.55f);
            br.quadraticTo (cx + side*s*1.28f, cy, cx + side*s*0.9f, cy - s*0.62f);
            g.strokePath (br, juce::PathStrokeType (s*0.05f));
            for (int L = 0; L < 6; ++L)
            {
                const float t = L / 5.0f;
                const float lx = cx + side*(s*1.02f + s*0.12f*std::sin (t*3.1f));
                const float ly = cy + s*0.55f - t * s*1.15f;
                juce::Path leaf; leaf.addEllipse (-s*0.055f, -s*0.15f, s*0.11f, s*0.3f);
                g.fillPath (leaf, juce::AffineTransform::rotation (side*0.6f).translated (lx, ly));
            }
        }
        juce::Path shield;
        shield.startNewSubPath (cx - s*0.6f, cy - s*0.62f);
        shield.lineTo (cx + s*0.6f, cy - s*0.62f);
        shield.quadraticTo (cx + s*0.62f, cy + s*0.25f, cx, cy + s*0.7f);
        shield.quadraticTo (cx - s*0.62f, cy + s*0.25f, cx - s*0.6f, cy - s*0.62f);
        shield.closeSubPath();
        g.setColour (juce::Colour (0xff1d1428).withMultipliedAlpha (a)); g.fillPath (shield);
        g.setColour (gold.withMultipliedAlpha (a));
        g.strokePath (shield, juce::PathStrokeType (s*0.07f));
        drawGZMono (g, cx, cy - s*0.03f, s*0.30f, gold, a);
        juce::Font ef (juce::FontOptions (s*0.36f));
        g.setFont (ef.boldened());
        g.drawText ("G'ZZIO", (int)(cx - s), (int)(cy + s*0.72f), (int)(s*2), (int)(s*0.44f),
                    juce::Justification::centred);
        juce::Font sf (juce::FontOptions (s*0.17f));
        g.setFont (sf);
        g.setColour (gold.withMultipliedAlpha (0.85f*a));
        g.drawText ("VOCAL MAISON", (int)(cx - s), (int)(cy + s*1.12f), (int)(s*2), (int)(s*0.26f),
                    juce::Justification::centred);
    }
    void paintBrand (juce::Graphics& g)
    {
        if (! brandCache.isValid()
            || brandCache.getWidth()  != area.getWidth()
            || brandCache.getHeight() != area.getHeight())
        {
            brandCache = juce::Image (juce::Image::RGB,
                                      juce::jmax (1, area.getWidth()),
                                      juce::jmax (1, area.getHeight()), true);
            juce::Graphics cg (brandCache);
            auto r = area.toFloat().withZeroOrigin();
            cg.setGradientFill ({ juce::Colour (0xff2a2038), r.getCentreX(), r.getY(),
                                  juce::Colour (0xff140e1d), r.getCentreX(), r.getBottom(), false });
            cg.fillRect (r);
            drawMonoCanvas (cg, r, 0.35f);
            drawCrest (cg, r.getCentreX(), r.getBottom() - 118.0f, 74.0f, 0.9f);   // パネル裏
            drawWeaveBand (cg, { r.getX(), r.getY(),              r.getWidth(), 14.0f }, 0.95f);
            drawWeaveBand (cg, { r.getX(), r.getBottom() - 14.0f, r.getWidth(), 14.0f }, 0.95f);
            auto fr = r.reduced (7.0f);
            cg.setColour (juce::Colour (0xaae0c48c));
            cg.drawRoundedRectangle (fr, 8.0f, 2.4f);
            cg.setColour (juce::Colour (0x8ce0c48c));
            for (float x = fr.getX()+20; x < fr.getRight()-20; x += 13)
            {
                cg.fillRoundedRectangle (x, fr.getY()+18,      7, 2, 1);
                cg.fillRoundedRectangle (x, fr.getBottom()-20,  7, 2, 1);
            }
        }
        g.drawImageAt (brandCache, area.getX(), area.getY());
    }

    // ---- オーロラ (声連動): 星空 + ゆらめくカーテン + 流れ星 ----
    void paintAurora (juce::Graphics& g)
    {
        auto r = area.toFloat();
        // 静的部分(夜空グラデ+基準の星90個)は1回だけ描いてキャッシュ
        if (! auroraCache.isValid()
            || auroraCache.getWidth()  != area.getWidth()
            || auroraCache.getHeight() != area.getHeight())
        {
            auroraCache = juce::Image (juce::Image::RGB,
                                       juce::jmax (1, area.getWidth()),
                                       juce::jmax (1, area.getHeight()), true);
            juce::Graphics cg (auroraCache);
            auto rz = r.withZeroOrigin();
            cg.setGradientFill ({ juce::Colour (0xff0b1026), rz.getCentreX(), rz.getY(),
                                  juce::Colour (0xff03050e), rz.getCentreX(), rz.getBottom(), false });
            cg.fillRect (rz);
            for (int i = 0; i < 90; ++i)
            {
                const float x = rz.getX() + hash01 (i*3+1) * rz.getWidth();
                const float y = rz.getY() + hash01 (i*3+2) * rz.getHeight() * 0.9f;
                const float sz = hash01 (i*3) < 0.12f ? 2.6f : 1.5f;
                cg.setColour (juce::Colours::white.withAlpha (0.30f + 0.35f * hash01 (i+7)));
                cg.fillEllipse (x, y, sz, sz);
            }
        }
        g.drawImageAt (auroraCache, area.getX(), area.getY());
        // 明るい星だけ声連動で瞬く (毎フレームは13個のみ)
        for (int i = 0; i < 90; i += 7)
        {
            const float x = r.getX() + hash01 (i*3+1) * r.getWidth();
            const float y = r.getY() + hash01 (i*3+2) * r.getHeight() * 0.9f;
            const float tw = 0.5f + 0.5f * std::sin (animT * (1.2f + hash01(i)) + i);
            const float aA = tw * (0.35f + 0.65f * juce::jmin (1.0f, 0.35f + level));
            g.setColour (juce::Colours::white.withAlpha (aA));
            g.fillEllipse (x - 0.6f, y - 0.6f, 2.8f, 2.8f);
            if (tw > 0.8f)
            {
                g.setColour (juce::Colours::white.withAlpha (aA * 0.5f));
                g.drawLine (x - 4, y + 1, x + 6, y + 1, 1.0f);
                g.drawLine (x + 1, y - 4, x + 1, y + 6, 1.0f);
            }
        }
        // オーロラ・カーテン 3本 (声量で明るく)
        static const juce::uint32 ac[3] = { 0xff43e8b8, 0xff6fb8ff, 0xffb08cff };
        for (int b2 = 0; b2 < 3; ++b2)
        {
            const float base = r.getY() + r.getHeight() * (0.16f + 0.10f * b2);
            const float amp  = 26.0f + 12.0f * b2;
            juce::Path top, bot;
            for (int sgx = 0; sgx <= 24; ++sgx)
            {
                const float x = r.getX() + r.getWidth() * sgx / 24.0f;
                const float ph = animT * (0.35f + 0.12f * b2) + b2 * 2.1f;
                const float y = base + std::sin (x * 0.006f + ph) * amp
                              + std::sin (x * 0.017f - ph * 1.7f) * amp * 0.35f;
                if (sgx == 0) { top.startNewSubPath (x, y); }
                else            top.lineTo (x, y);
            }
            juce::Path curtain (top);
            curtain.lineTo (r.getRight(), r.getY() + r.getHeight() * 0.62f);
            curtain.lineTo (r.getX(),     r.getY() + r.getHeight() * 0.62f);
            curtain.closeSubPath();
            const float aA = (0.16f + 0.05f * (2 - b2)) * (0.55f + 0.9f * level);
            juce::Colour c (ac[b2]);
            g.setGradientFill ({ c.withAlpha (aA), r.getCentreX(), base - amp,
                                 c.withAlpha (0.0f), r.getCentreX(),
                                 r.getY() + r.getHeight() * 0.62f, false });
            g.fillPath (curtain);
            g.setColour (c.withAlpha (juce::jmin (0.85f, aA * 2.2f)));
            g.strokePath (top, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
        }
        // 流れ星 (約7秒ごと)
        const float cyc = std::fmod (animT, 7.0f);
        if (cyc < 0.7f)
        {
            const int seed = (int) (animT / 7.0f);
            const float p = cyc / 0.7f;
            const float x0 = r.getX() + r.getWidth() * (0.15f + 0.7f * hash01 (seed));
            const float y0 = r.getY() + 30 + 60 * hash01 (seed + 9);
            const float x = x0 + p * 190.0f, y = y0 + p * 70.0f;
            g.setColour (juce::Colours::white.withAlpha ((1.0f - p) * 0.9f));
            g.drawLine (x - 46, y - 17, x, y, 2.2f);
            g.fillEllipse (x - 2, y - 2, 4, 4);
        }
        // 地平のシルエット
        g.setColour (juce::Colour (0xff060812));
        juce::Path hills; hills.startNewSubPath (r.getX(), r.getBottom());
        hills.lineTo (r.getX(), r.getBottom() - 26);
        hills.quadraticTo (r.getX() + r.getWidth()*0.3f, r.getBottom() - 54,
                           r.getCentreX(), r.getBottom() - 30);
        hills.quadraticTo (r.getX() + r.getWidth()*0.75f, r.getBottom() - 10,
                           r.getRight(), r.getBottom() - 34);
        hills.lineTo (r.getRight(), r.getBottom());
        hills.closeSubPath(); g.fillPath (hills);
        // v1.8.3: 下部の背景帯(46px)に湖面のオーロラ反射 (声で明るく)
        static const juce::uint32 rc[3] = { 0xff43e8b8, 0xff6fb8ff, 0xffb08cff };
        for (int i = 0; i < 3; ++i)
        {
            const juce::Colour c ((juce::uint32) rc[i]);
            const float aa = (0.10f + 0.06f * i) * (0.5f + 1.0f * level);
            const float y = r.getBottom() - 40 + i * 12.0f;
            g.setGradientFill ({ c.withAlpha (aa), r.getCentreX(), y,
                                 c.withAlpha (0.0f), r.getCentreX(), y + 18.0f, false });
            g.fillRect (juce::Rectangle<float> (r.getX(), y, r.getWidth(), 18.0f));
        }
    }

    // ---- ライバー (V-Liver: 女性Vライバー向けパステルステージ＆キラキラ演出) ----
    static void drawStar4 (juce::Graphics& g, float cx, float cy, float r, juce::Colour col)
    {
        juce::Path p;
        for (int i = 0; i < 8; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::pi * 0.25f;
            const float rad = (i % 2 == 0) ? r : r * 0.26f;
            const float x = cx + rad * std::sin (a), y = cy - rad * std::cos (a);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        p.closeSubPath();
        g.setColour (col);
        g.fillPath (p);
    }
    static void drawHeart (juce::Graphics& g, float cx, float cy, float sz, juce::Colour col)
    {
        juce::Path p;
        p.startNewSubPath (cx, cy + sz * 0.55f);
        p.cubicTo (cx - sz * 1.05f, cy - sz * 0.10f, cx - sz * 0.55f, cy - sz * 0.90f, cx, cy - sz * 0.30f);
        p.cubicTo (cx + sz * 0.55f, cy - sz * 0.90f, cx + sz * 1.05f, cy - sz * 0.10f, cx, cy + sz * 0.55f);
        p.closeSubPath();
        g.setColour (col);
        g.fillPath (p);
    }
    void paintVLiver (juce::Graphics& g)
    {
        auto r = area.toFloat();
        if (! liverCache.isValid()
            || liverCacheMode          != mode
            || liverCache.getWidth()  != area.getWidth()
            || liverCache.getHeight() != area.getHeight())
        {
            liverCacheMode = mode;
            liverCache = juce::Image (juce::Image::ARGB,
                                      juce::jmax (1, area.getWidth()),
                                      juce::jmax (1, area.getHeight()), true);
            juce::Graphics cg (liverCache);
            auto rz = r.withZeroOrigin();
            const bool darkCouture = (mode == 11);
            // 各カラーバリエーション専用の濃密ファンデーション＆ベルベット下地グラデーション
            cg.setGradientFill (juce::ColourGradient (Palette::bgTop, rz.getX(), rz.getY(),
                                                      Palette::bgBot, rz.getRight(), rz.getBottom(), false));
            cg.fillRect (rz);

            // チーク＆ハイライターパウダーのふんわりグロウボケ（各カラバリの主要色から合成）
            struct SpotPos { float x, y, rad; int colIdx; float alpha; };
            static const SpotPos spots[] = {
                { 0.16f, 0.18f, 0.36f, 0, 0.24f },
                { 0.84f, 0.15f, 0.35f, 1, 0.22f },
                { 0.50f, 0.50f, 0.44f, 2, 0.18f },
                { 0.22f, 0.82f, 0.34f, 3, 0.22f },
                { 0.86f, 0.78f, 0.38f, 0, 0.24f }
            };
            const juce::Colour spotCols[4] = {
                Palette::yellow.brighter (darkCouture ? 0.0f : 0.25f),
                Palette::blue.brighter   (darkCouture ? 0.0f : 0.25f),
                Palette::salmon.brighter (darkCouture ? 0.0f : 0.30f),
                Palette::green.brighter  (darkCouture ? 0.0f : 0.25f)
            };
            const float maxD = juce::jmax (rz.getWidth(), rz.getHeight());
            for (auto& s : spots)
            {
                const float cx = rz.getWidth() * s.x, cy = rz.getHeight() * s.y, rr = maxD * s.rad;
                juce::Colour c = spotCols[s.colIdx].withAlpha (darkCouture ? s.alpha * 0.72f : s.alpha);
                cg.setGradientFill (juce::ColourGradient (c, cx, cy, c.withAlpha (0.0f), cx + rr, cy, true));
                cg.fillEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f);
            }

            // ★ マトラッセ（ダイヤ・キルティング）ステッチ＆クチュール宝飾スタッズ装飾
            const float qStep = 44.0f;
            const juce::Colour stitchDark = darkCouture ? juce::Colours::black.withAlpha (0.38f)
                                                        : Palette::panelLn.darker (0.15f).withAlpha (0.20f);
            const juce::Colour stitchHi   = darkCouture ? Palette::metalMid.withAlpha (0.18f)
                                                        : juce::Colours::white.withAlpha (0.44f);
            for (float x = -rz.getHeight(); x < rz.getWidth() + rz.getHeight(); x += qStep)
            {
                cg.setColour (stitchDark);
                cg.drawLine (x, 0.0f, x + rz.getHeight(), rz.getHeight(), 1.1f);
                cg.drawLine (x, rz.getHeight(), x + rz.getHeight(), 0.0f, 1.1f);
                cg.setColour (stitchHi);
                cg.drawLine (x + 1.2f, 0.0f, x + rz.getHeight() + 1.2f, rz.getHeight(), 0.9f);
                cg.drawLine (x + 1.2f, rz.getHeight(), x + rz.getHeight() + 1.2f, 0.0f, 0.9f);
            }
            // キルティング交点のクチュール金属・パールピン
            for (float y = qStep * 0.5f; y < rz.getHeight(); y += qStep)
            {
                for (float x = qStep * 0.5f; x < rz.getWidth(); x += qStep)
                {
                    cg.setColour (Palette::metalMid.withAlpha (darkCouture ? 0.42f : 0.34f));
                    cg.fillEllipse (x - 2.2f, y - 2.2f, 4.4f, 4.4f);
                    cg.setColour (juce::Colours::white.withAlpha (darkCouture ? 0.55f : 0.78f));
                    cg.fillEllipse (x - 1.0f, y - 1.0f, 2.0f, 2.0f);
                }
            }

            // 上下のクチュール・スカラップレース＆宝飾メタル枠
            cg.setColour (Palette::panelLn.withAlpha (darkCouture ? 0.45f : 0.36f));
            for (float x = 0.0f; x < rz.getWidth(); x += 26.0f)
            {
                cg.fillEllipse (x - 4.0f, -9.0f, 34.0f, 22.0f);
                cg.fillEllipse (x - 4.0f, rz.getBottom() - 13.0f, 34.0f, 22.0f);
            }
            cg.setColour (Palette::metalMid.withAlpha (0.58f));
            cg.drawRoundedRectangle (rz.reduced (4.0f), 10.0f, 1.8f);

            // ハート＆クリスタルきらめきモチーフ
            for (int i = 0; i < 24; ++i)
            {
                const float hx = rz.getWidth()  * hash01 (i * 11 + 3);
                const float hy = rz.getHeight() * hash01 (i * 11 + 7);
                const float hs = 7.5f + 9.0f * hash01 (i * 11 + 9);
                const juce::Colour hc = (i % 3 == 0 ? Palette::salmon : (i % 3 == 1 ? Palette::metalMid : Palette::blue))
                                            .withAlpha (darkCouture ? 0.26f : 0.22f);
                if (i % 2 == 0)
                    drawHeart (cg, hx, hy, hs, hc);
                else
                    drawStar4 (cg, hx, hy, hs * 1.1f, hc);
            }

            // クチュール・ドレッサーアーチもキャッシュに焼き込んで毎フレームのパス描画を省く
            juce::Path arch;
            arch.addCentredArc (rz.getCentreX(), rz.getBottom() + rz.getHeight() * 0.25f,
                                rz.getWidth() * 0.62f, rz.getHeight() * 0.85f, 0.0f, -1.1f, 1.1f, true);
            cg.setColour (Palette::salmon.withAlpha (0.20f));
            cg.strokePath (arch, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved));
        }
        g.drawImageAt (liverCache, area.getX(), area.getY());
    }

    // ---- ネオン (Cyber Neon: シンセウェイブ＆サイバーグリッド演出) ----
    void paintCyberNeon (juce::Graphics& g)
    {
        auto r = area.toFloat();
        if (! neonCache.isValid()
            || neonCache.getWidth()  != area.getWidth()
            || neonCache.getHeight() != area.getHeight())
        {
            neonCache = juce::Image (juce::Image::ARGB,
                                     juce::jmax (1, area.getWidth()),
                                     juce::jmax (1, area.getHeight()), true);
            juce::Graphics cg (neonCache);
            auto rz = r.withZeroOrigin();
            cg.setGradientFill (juce::ColourGradient (juce::Colour (0xff070812), 0.0f, 0.0f,
                                                      juce::Colour (0xff120924), 0.0f, rz.getBottom(), false));
            cg.fillRect (rz);

            // サイバーネオングロー（左上シアン・右下マゼンタ）
            const float maxD = juce::jmax (rz.getWidth(), rz.getHeight());
            cg.setGradientFill (juce::ColourGradient (juce::Colour (0x2a00f0ff), rz.getWidth() * 0.18f, rz.getHeight() * 0.20f,
                                                      juce::Colour (0x0000f0ff), rz.getWidth() * 0.18f + maxD * 0.45f, rz.getHeight() * 0.20f, true));
            cg.fillRect (rz);
            cg.setGradientFill (juce::ColourGradient (juce::Colour (0x2aff2a85), rz.getWidth() * 0.82f, rz.getHeight() * 0.78f,
                                                      juce::Colour (0x00ff2a85), rz.getWidth() * 0.82f + maxD * 0.45f, rz.getHeight() * 0.78f, true));
            cg.fillRect (rz);

            // パースペクティブ・ネオングリッド（下部地平線）
            const float horizon = rz.getHeight() * 0.68f;
            cg.setColour (juce::Colour (0x2400f0ff));
            for (int i = -10; i <= 10; ++i)
            {
                const float xTop = rz.getCentreX() + (float) i * (rz.getWidth() * 0.032f);
                const float xBot = rz.getCentreX() + (float) i * (rz.getWidth() * 0.12f);
                cg.drawLine (xTop, horizon, xBot, rz.getBottom(), 1.2f);
            }
            for (int j = 0; j <= 7; ++j)
            {
                const float t = (float) j / 7.0f;
                const float y = horizon + (rz.getBottom() - horizon) * (t * t);
                cg.setColour (juce::Colour (0xfffe2a85).withAlpha (0.08f + 0.16f * t));
                cg.drawHorizontalLine (juce::roundToInt (y), 0.0f, rz.getRight());
            }
            cg.setColour (juce::Colour (0xff00f0ff).withAlpha (0.30f));
            cg.drawHorizontalLine (68, 14.0f, rz.getRight() - 14.0f);
        }
        g.drawImageAt (neonCache, area.getX(), area.getY());
    }

    // ---- スタジオ (Studio Pro: プロミュージシャン向けヴィンテージ・アナログコンソール筐体) ----
    void paintStudioPro (juce::Graphics& g)
    {
        auto r = area.toFloat();
        if (! studioCache.isValid()
            || studioCache.getWidth()  != area.getWidth()
            || studioCache.getHeight() != area.getHeight())
        {
            studioCache = juce::Image (juce::Image::ARGB,
                                       juce::jmax (1, area.getWidth()),
                                       juce::jmax (1, area.getHeight()), true);
            juce::Graphics cg (studioCache);
            auto rz = r.withZeroOrigin();

            // ヘアライン加工チャコールメタル面板
            cg.setGradientFill (juce::ColourGradient (juce::Colour (0xff23201d), 0.0f, 0.0f,
                                                      juce::Colour (0xff151311), 0.0f, rz.getBottom(), false));
            cg.fillRect (rz);
            for (int y = 0; y < (int) rz.getHeight(); y += 3)
            {
                cg.setColour (juce::Colours::white.withAlpha ((y % 6 == 0) ? 0.016f : 0.008f));
                cg.drawHorizontalLine (y, 0.0f, rz.getRight());
            }

            // 左右のウォルナット（くるみ材）サイドウッドパネル
            const float woodW = 14.0f;
            for (float wx : { 0.0f, rz.getRight() - woodW })
            {
                juce::Rectangle<float> wr (wx, 0.0f, woodW, rz.getHeight());
                cg.setGradientFill (juce::ColourGradient (juce::Colour (0xff5c361e), wx, 0.0f,
                                                          juce::Colour (0xff3b2110), wx + woodW, 0.0f, false));
                cg.fillRect (wr);
                cg.setColour (juce::Colour (0x55d89b56));
                cg.drawRect (wr, 1.0f);
            }

            // 四隅と上下の真鍮ラックマウント・マイナスビス
            auto drawBrassScrew = [&cg] (float cx, float cy)
            {
                const float sr = 5.5f;
                cg.setColour (juce::Colours::black.withAlpha (0.55f));
                cg.fillEllipse (cx - sr, cy - sr + 1.2f, sr * 2.0f, sr * 2.0f);
                cg.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2c46d), cx - sr * 0.5f, cy - sr * 0.5f,
                                                          juce::Colour (0xff8c6222), cx + sr * 0.7f, cy + sr * 0.7f, false));
                cg.fillEllipse (cx - sr, cy - sr, sr * 2.0f, sr * 2.0f);
                cg.setColour (juce::Colour (0xff3d290c));
                cg.drawLine (cx - sr * 0.65f, cy - sr * 0.25f, cx + sr * 0.65f, cy + sr * 0.25f, 1.4f);
            };
            drawBrassScrew (7.0f, 22.0f);
            drawBrassScrew (rz.getRight() - 7.0f, 22.0f);
            drawBrassScrew (7.0f, rz.getCentreY());
            drawBrassScrew (rz.getRight() - 7.0f, rz.getCentreY());
            drawBrassScrew (7.0f, rz.getBottom() - 22.0f);
            drawBrassScrew (rz.getRight() - 7.0f, rz.getBottom() - 22.0f);
        }
        g.drawImageAt (studioCache, area.getX(), area.getY());
    }

    // ---- 🌌🌊🌅🍁👑 風景・季節コレクション 5選 (14=天の川銀河 / 15=瑞々しい碧海 / 16=朝焼けの海 / 17=晩秋〜初冬の哀愁 / 18=蒼海オペラ座) ----
    static void drawLensBokeh (juce::Graphics& g, float cx, float cy, float rad, juce::Colour col, float alpha)
    {
        // 大口径レンズの玉ボケ（ふんわり外周ハロー＋半透明ボケ円盤＋明るいエッジリング）
        g.setGradientFill (juce::ColourGradient (col.withAlpha (alpha * 0.45f), cx, cy,
                                                 col.withAlpha (0.0f), cx + rad * 1.50f, cy, true));
        g.fillEllipse (cx - rad * 1.50f, cy - rad * 1.50f, rad * 3.0f, rad * 3.0f);
        g.setColour (col.withAlpha (alpha * 0.38f));
        g.fillEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
        g.setColour (col.brighter (0.38f).withAlpha (alpha * 0.78f));
        g.drawEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f, juce::jmax (1.0f, rad * 0.08f));
    }

    static void drawStar8 (juce::Graphics& g, float cx, float cy, float rad, juce::Colour col)
    {
        // 天体写真の8芒星クロス回折スパイク（長短の十字＋X字光条＋中心ハロ）
        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.55f), cx, cy,
                                                 col.withAlpha (0.0f), cx + rad * 1.2f, cy, true));
        g.fillEllipse (cx - rad * 1.2f, cy - rad * 1.2f, rad * 2.4f, rad * 2.4f);
        g.setColour (col);
        g.drawLine (cx - rad * 1.45f, cy, cx + rad * 1.45f, cy, 1.5f);
        g.drawLine (cx, cy - rad * 1.45f, cx, cy + rad * 1.45f, 1.5f);
        const float d = rad * 0.72f;
        g.drawLine (cx - d, cy - d, cx + d, cy + d, 1.1f);
        g.drawLine (cx - d, cy + d, cx + d, cy - d, 1.1f);
        g.setColour (juce::Colours::white.withAlpha (col.getFloatAlpha()));
        g.fillEllipse (cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
    }

    static void drawWaterDroplet (juce::Graphics& g, float cx, float cy, float rad, juce::Colour aquaCol, float alpha)
    {
        // ぷるんと瑞々しい3D水滴・気泡（ドロップシャドウ＋透明感グラデ＋右下内部屈折＋左上純白ハイライト）
        g.setColour (juce::Colour (0xff011627).withAlpha (alpha * 0.42f));
        g.fillEllipse (cx - rad + rad * 0.12f, cy - rad + rad * 0.22f, rad * 2.0f, rad * 2.0f);

        juce::ColourGradient dropGrad (aquaCol.brighter (0.45f).withAlpha (alpha * 0.28f), cx - rad * 0.35f, cy - rad * 0.35f,
                                       aquaCol.darker   (0.30f).withAlpha (alpha * 0.62f), cx + rad * 0.55f, cy + rad * 0.55f, true);
        g.setGradientFill (dropGrad);
        g.fillEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);

        // 右下の三日月型・水滴内部反射（メニスカス発光）
        g.setColour (juce::Colour (0xff90f1ef).withAlpha (alpha * 0.75f));
        g.drawEllipse (cx - rad * 0.88f, cy - rad * 0.88f, rad * 1.76f, rad * 1.76f, juce::jmax (1.1f, rad * 0.12f));

        // 外周のクリスタル輪郭
        g.setColour (juce::Colours::white.withAlpha (alpha * 0.72f));
        g.drawEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f, juce::jmax (1.0f, rad * 0.09f));

        // 左上のピンスポット純白スペキュラハイライト（大・小2点）
        const float hlR = juce::jmax (1.4f, rad * 0.28f);
        g.setColour (juce::Colours::white.withAlpha (juce::jmin (0.98f, alpha * 1.15f)));
        g.fillEllipse (cx - rad * 0.52f, cy - rad * 0.54f, hlR * 1.35f, hlR * 0.95f);
        if (rad >= 6.0f)
            g.fillEllipse (cx - rad * 0.22f, cy - rad * 0.64f, hlR * 0.55f, hlR * 0.55f);
    }

    static void drawCausticWaveMesh (juce::Graphics& g, juce::Rectangle<float> box, juce::Colour col, float alpha, int seedOffset = 0)
    {
        // 水面・水底の網目状の光の揺らぎ（コースティクス・水紋メッシュ）
        const int numRows = juce::jlimit (4, 14, (int) (box.getHeight() / 42.0f));
        const int segs = 24;
        for (int row = 0; row < numRows; ++row)
        {
            const float baseY = box.getY() + box.getHeight() * ((float) row + 0.5f) / (float) numRows;
            juce::Path p1, p2;
            for (int s = 0; s <= segs; ++s)
            {
                const float t = (float) s / (float) segs;
                const float x = box.getX() + box.getWidth() * t;
                const float ph1 = t * 12.5f + (float) (row * 1.7f + seedOffset * 0.9f);
                const float ph2 = t * 10.2f - (float) (row * 2.1f + seedOffset * 0.5f);
                const float y1 = baseY + std::sin (ph1) * 14.0f + std::cos (ph2 * 0.7f) * 6.0f;
                const float y2 = baseY + std::cos (ph2) * 14.0f + std::sin (ph1 * 0.8f) * 6.0f;
                if (s == 0)
                {
                    p1.startNewSubPath (x, y1);
                    p2.startNewSubPath (x, y2);
                }
                else
                {
                    p1.lineTo (x, y1);
                    p2.lineTo (x, y2);
                }
            }
            g.setColour (col.withAlpha (alpha * 0.45f));
            g.strokePath (p1, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.strokePath (p2, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (juce::Colours::white.withAlpha (alpha * 0.52f));
            g.strokePath (p1, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    static void drawMapleLeaf (juce::Graphics& g, float cx, float cy, float sz, float rot, juce::Colour col, float alpha)
    {
        // 🍁 日本の5裂カエデ（もみじ）の葉シルエット＋鮮やかな紅〜黄金グラデーション＋金葉脈ハイライト
        juce::Path leaf, veins;
        static const float kAngles[5]  = { -1.5708f, -0.82f, -2.32f, -0.18f, -2.96f };
        static const float kLengths[5] = {  1.12f,    0.96f,  0.96f,  0.72f,  0.72f };
        for (int k = 0; k < 5; ++k)
        {
            const float an = kAngles[k];
            const float len = sz * kLengths[k];
            const float tx = std::cos (an) * len, ty = std::sin (an) * len;
            const float lx = std::cos (an - 0.34f) * len * 0.54f, ly = std::sin (an - 0.34f) * len * 0.54f;
            const float rx = std::cos (an + 0.34f) * len * 0.54f, ry = std::sin (an + 0.34f) * len * 0.54f;
            juce::Path lobe;
            lobe.startNewSubPath (0.0f, sz * 0.12f);
            lobe.quadraticTo (lx * 1.22f, ly * 1.22f, tx, ty);
            lobe.quadraticTo (rx * 1.22f, ry * 1.22f, 0.0f, sz * 0.12f);
            lobe.closeSubPath();
            leaf.addPath (lobe);
            veins.startNewSubPath (0.0f, sz * 0.10f);
            veins.lineTo (tx * 0.78f, ty * 0.78f);
        }
        // 葉柄（茎）
        leaf.addRectangle (-sz * 0.045f, sz * 0.08f, sz * 0.09f, sz * 0.55f);
        auto xf = juce::AffineTransform::rotation (rot).translated (cx, cy);
        g.setColour (juce::Colours::black.withAlpha (alpha * 0.42f));
        g.fillPath (leaf, xf.translated (1.5f, 2.0f));
        g.setGradientFill (juce::ColourGradient (col.brighter (0.28f).withAlpha (alpha), cx, cy - sz,
                                                 juce::Colour (0xfffbbf24).withAlpha (alpha * 0.92f), cx, cy + sz * 0.6f, false));
        g.fillPath (leaf, xf);
        g.setColour (juce::Colour (0xfffff3b0).withAlpha (alpha * 0.75f));
        g.strokePath (leaf, juce::PathStrokeType (0.95f), xf);
        g.setColour (juce::Colour (0xfffde68a).withAlpha (alpha * 0.65f));
        g.strokePath (veins, juce::PathStrokeType (0.85f), xf);
    }

    static void drawSnowCrystal (juce::Graphics& g, float cx, float cy, float sz, float rot, juce::Colour col, float alpha)
    {
        // ❄ 初冬の六花（6角樹枝状の雪の結晶）＋氷晶ハロー
        g.setGradientFill (juce::ColourGradient (col.withAlpha (alpha * 0.38f), cx, cy,
                                                 col.withAlpha (0.0f), cx + sz * 1.35f, cy, true));
        g.fillEllipse (cx - sz * 1.35f, cy - sz * 1.35f, sz * 2.7f, sz * 2.7f);
        g.setColour (col.withAlpha (alpha));
        for (int k = 0; k < 6; ++k)
        {
            const float an = rot + (float) k * juce::MathConstants<float>::pi / 3.0f;
            const float dx = std::cos (an), dy = std::sin (an);
            g.drawLine (cx, cy, cx + dx * sz, cy + dy * sz, 1.4f);
            const float px = -dy, py = dx;
            for (float f : { 0.45f, 0.72f })
            {
                const float bx = cx + dx * sz * f, by = cy + dy * sz * f;
                const float bLen = sz * (f < 0.6f ? 0.30f : 0.22f);
                g.drawLine (bx, by, bx + (dx * 0.7f + px * 0.7f) * bLen, by + (dy * 0.7f + py * 0.7f) * bLen, 1.1f);
                g.drawLine (bx, by, bx + (dx * 0.7f - px * 0.7f) * bLen, by + (dy * 0.7f - py * 0.7f) * bLen, 1.1f);
            }
        }
        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.fillEllipse (cx - 1.8f, cy - 1.8f, 3.6f, 3.6f);
    }

    void paintOceanStar (juce::Graphics& g)
    {
        auto r = area.toFloat();
        if (! oceanStarCache.isValid()
            || oceanStarCacheMode         != mode
            || oceanStarCache.getWidth()  != area.getWidth()
            || oceanStarCache.getHeight() != area.getHeight())
        {
            oceanStarCacheMode = mode;
            oceanStarCache = juce::Image (juce::Image::ARGB,
                                          juce::jmax (1, area.getWidth()),
                                          juce::jmax (1, area.getHeight()), true);
            juce::Graphics cg (oceanStarCache);
            auto rz = r.withZeroOrigin();
            const float W = rz.getWidth(), H = rz.getHeight();
            const float maxD = juce::jmax (W, H);

            if (mode == 14)
            {
                // =========================================================================
                // 🌌 Mode 14: 星降る天の川銀河と蛍火の夜 (Galactic Milky Way & Firefly Night)
                //   画面を斜めに横断する圧倒的な天の川銀河コア（星雲ガス＋暗黒星雲ダストレーン＋420個の星屑＋流星）
                //   ＋湖畔の森シルエットから舞い上がる黄金の蛍火
                // =========================================================================
                juce::ColourGradient sky (juce::Colour (0xff030514), 0.0f, 0.0f,
                                          juce::Colour (0xff0d1b3e), 0.0f, H, false);
                sky.addColour (0.45, juce::Colour (0xff080f2c));
                sky.addColour (0.78, juce::Colour (0xff122452));
                cg.setGradientFill (sky);
                cg.fillRect (rz);

                // 1. 対角線（左下 -> 右上）を貫く天の川銀河の多層星雲クラウド（28層）
                static const juce::uint32 kNebulaCols[5] = {
                    0xff9333ea, 0xffec4899, 0xff38bdf8, 0xff6366f1, 0xfff59e0b
                };
                for (int n = 0; n < 28; ++n)
                {
                    const float t = (float) n / 27.0f;
                    const float baseX = W * (0.04f + 0.92f * t);
                    const float baseY = H * (0.86f - 0.78f * t);
                    const float jitterX = (hash01 (n * 19 + 5) - 0.5f) * (W * 0.22f);
                    const float jitterY = (hash01 (n * 19 + 13) - 0.5f) * (H * 0.22f);
                    const float nx = baseX + jitterX;
                    const float ny = baseY + jitterY;
                    const float nr = maxD * (0.14f + 0.14f * hash01 (n * 19 + 29));
                    const juce::Colour nc (kNebulaCols[n % 5]);
                    const float na = (n % 5 == 4) ? 0.26f : 0.34f;
                    cg.setGradientFill (juce::ColourGradient (nc.withAlpha (na), nx, ny,
                                                              nc.withAlpha (0.0f), nx + nr, ny, true));
                    cg.fillEllipse (nx - nr, ny - nr, nr * 2.0f, nr * 2.0f);
                }

                // 2. 天の川中心部の黄金・ミルキーホワイト銀河バルジ発光帯
                for (int core = 0; core < 12; ++core)
                {
                    const float t = (float) core / 11.0f;
                    const float cx = W * (0.12f + 0.76f * t) + std::sin (t * 6.0f) * 28.0f;
                    const float cy = H * (0.78f - 0.64f * t) + std::cos (t * 5.0f) * 20.0f;
                    const float cr = maxD * (0.09f + 0.05f * std::sin (t * 3.14f));
                    const juce::Colour cc = (core % 2 == 0) ? juce::Colour (0x44fef08a) : juce::Colour (0x3ce0f2fe);
                    cg.setGradientFill (juce::ColourGradient (cc, cx, cy, cc.withAlpha (0.0f), cx + cr, cy, true));
                    cg.fillEllipse (cx - cr, cy - cr, cr * 2.0f, cr * 2.0f);
                }

                // 3. 天の川の立体感を際立たせる暗黒星雲ダストレーン（Dark Rift）
                juce::Path dustRift;
                dustRift.startNewSubPath (W * 0.10f, H * 0.80f);
                dustRift.cubicTo (W * 0.35f, H * 0.62f, W * 0.58f, H * 0.34f, W * 0.88f, H * 0.14f);
                cg.setColour (juce::Colour (0xff04071a).withAlpha (0.58f));
                cg.strokePath (dustRift, juce::PathStrokeType (28.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                // 4. 銀河面に密集する420粒の星屑（Milky Way Star Cloud）
                for (int i = 0; i < 420; ++i)
                {
                    float sx, sy;
                    if (i < 260)
                    {
                        // 天の川ベルト上に密集配置
                        const float t = hash01 (i * 17 + 3);
                        const float spread = (hash01 (i * 17 + 11) - 0.5f) * (hash01 (i * 17 + 19) * 0.36f);
                        sx = W * (0.02f + 0.96f * t + spread * 0.6f);
                        sy = H * (0.88f - 0.80f * t + spread);
                    }
                    else
                    {
                        sx = W * hash01 (i * 13 + 7);
                        sy = H * hash01 (i * 13 + 23) * 0.92f;
                    }
                    const float sr = 0.7f + 2.1f * hash01 (i * 13 + 31);
                    const juce::Colour sc = (i % 6 == 0) ? juce::Colour (0xfffef08a)
                                          : (i % 5 == 0) ? juce::Colour (0xff7dd3fc)
                                          : (i % 4 == 0) ? juce::Colour (0xfff9a8d4)
                                          : (i % 7 == 0) ? juce::Colour (0xffc4b5fd)
                                                         : juce::Colours::white;
                    cg.setColour (sc.withAlpha (0.35f + 0.62f * hash01 (i * 7 + 3)));
                    cg.fillEllipse (sx - sr * 0.5f, sy - sr * 0.5f, sr, sr);
                }

                // 5. 夜空を切り裂く4本の流星（Shooting Stars）
                static const float kMeteors[4][4] = {
                    { 0.24f, 0.12f, 0.12f, 0.24f },
                    { 0.78f, 0.08f, 0.64f, 0.22f },
                    { 0.52f, 0.16f, 0.41f, 0.27f },
                    { 0.88f, 0.34f, 0.76f, 0.46f }
                };
                for (int m = 0; m < 4; ++m)
                {
                    const float x0 = W * kMeteors[m][0], y0 = H * kMeteors[m][1];
                    const float x1 = W * kMeteors[m][2], y1 = H * kMeteors[m][3];
                    cg.setGradientFill (juce::ColourGradient (juce::Colours::transparentWhite, x0, y0,
                                                              juce::Colour (0xffe0f2fe), x1, y1, false));
                    cg.drawLine (x0, y0, x1, y1, 2.2f);
                    drawStar8 (cg, x1, y1, 6.5f, juce::Colour (0xff7dd3fc));
                }

                // 6. 一等星の8芒星クロス回折スパイク（18個）
                for (int s = 0; s < 18; ++s)
                {
                    const float sx = W * (0.05f + 0.90f * hash01 (s * 43 + 9));
                    const float sy = H * (0.06f + 0.78f * hash01 (s * 43 + 17));
                    const float sr = 6.5f + 9.5f * hash01 (s * 43 + 27);
                    const juce::Colour sc = (s % 3 == 0) ? juce::Colour (0xfffef08a)
                                          : (s % 3 == 1) ? juce::Colour (0xff38bdf8)
                                                         : juce::Colour (0xfff472b6);
                    drawStar8 (cg, sx, sy, sr, sc.withAlpha (0.85f));
                }

                // 7. 下辺の湖畔の針葉樹林シルエット＆水面から舞い上がる黄金の蛍火ボケ（26個）
                juce::Path forest;
                forest.startNewSubPath (0.0f, H);
                for (int x = 0; x <= 48; ++x)
                {
                    const float px = W * (float) x / 48.0f;
                    const float treePeak = (x % 2 == 0) ? 26.0f + 18.0f * hash01 (x * 19 + 3) : 12.0f;
                    const float py = H - treePeak - 14.0f * std::sin ((float) x * 0.35f);
                    forest.lineTo (px, py);
                }
                forest.lineTo (W, H);
                forest.closeSubPath();
                cg.setColour (juce::Colour (0xff020510).withAlpha (0.86f));
                cg.fillPath (forest);

                for (int b = 0; b < 26; ++b)
                {
                    const float bx = W * (0.04f + 0.92f * hash01 (b * 29 + 11));
                    const float by = H * (0.42f + 0.54f * hash01 (b * 29 + 23));
                    const float br = 7.0f + 26.0f * hash01 (b * 29 + 41);
                    const juce::Colour bc = (b % 4 == 0) ? juce::Colour (0xff2dd4bf)
                                          : (b % 3 == 0) ? juce::Colour (0xfffde047)
                                                         : juce::Colour (0xfff59e0b);
                    drawLensBokeh (cg, bx, by, br, bc, 0.32f + 0.34f * hash01 (b * 17 + 5));
                }
            }
            else if (mode == 15)
            {
                // =========================================================================
                // 🌊 Mode 15: 瑞々しい碧海と水紋・アクアラグーン (Lush Aqua Sea & Caustic Ripples)
                //   「海なら水々しさ」— 陽光が降り注ぐ透明なエメラルド・ターコイズ海面＋水底コースティクス水紋メッシュ
                //   ＋立体的な水滴＆沸き立つ気泡（48個）＋透き通る波頭と水紋リング
                // =========================================================================
                juce::ColourGradient aquaSea (juce::Colour (0xff0096c7), 0.0f, 0.0f,
                                              juce::Colour (0xff02203c), 0.0f, H, false);
                aquaSea.addColour (0.18, juce::Colour (0xff00b4d8));   // 陽光きらめく浅瀬のアクアターコイズ
                aquaSea.addColour (0.45, juce::Colour (0xff0077b6));   // 澄み切ったマリンブルー
                aquaSea.addColour (0.75, juce::Colour (0xff033e6b));   // 深いサファイアラグーン
                cg.setGradientFill (aquaSea);
                cg.fillRect (rz);

                // 1. 海面中央から降り注ぐ瑞々しい太陽光のハイライトブルーム
                cg.setGradientFill (juce::ColourGradient (juce::Colour (0x8890f1ef), W * 0.50f, H * 0.04f,
                                                          juce::Colour (0x0000b4d8), W * 0.50f + maxD * 0.55f, H * 0.04f, true));
                cg.fillRect (rz);

                // 2. 海中へ斜めに差し込む12本の陽光ゴッドレイ（Sunbeams）
                for (int ray = 0; ray < 12; ++ray)
                {
                    const float topX = W * (0.06f + 0.082f * (float) ray);
                    const float botX = topX + (ray - 5.5f) * (W * 0.045f);
                    const float bwTop = 14.0f + 16.0f * hash01 (ray * 13 + 3);
                    const float bwBot = 48.0f + 42.0f * hash01 (ray * 13 + 7);
                    juce::Path shaft;
                    shaft.startNewSubPath (topX - bwTop, 0.0f);
                    shaft.lineTo (topX + bwTop, 0.0f);
                    shaft.lineTo (botX + bwBot, H);
                    shaft.lineTo (botX - bwBot, H);
                    shaft.closeSubPath();
                    const juce::Colour rc = (ray % 2 == 0) ? juce::Colour (0x38caf0f8) : juce::Colour (0x2c00f5d4);
                    cg.setGradientFill (juce::ColourGradient (rc, topX, 0.0f, rc.withAlpha (0.0f), botX, H * 0.92f, false));
                    cg.fillPath (shaft);
                }

                // 3. 画面全体に広がる網目状の水紋コースティクス（Caustic Light Lace）
                drawCausticWaveMesh (cg, rz, juce::Colour (0xff72efdd), 0.42f, 0);

                // 4. 幾重にも重なる透明な波のうねり（Translucent Rolling Wave Crests）
                for (int wv = 0; wv < 6; ++wv)
                {
                    const float wyBase = H * (0.16f + 0.14f * (float) wv);
                    juce::Path waveFill, waveCrest;
                    waveFill.startNewSubPath (0.0f, H);
                    waveFill.lineTo (0.0f, wyBase);
                    waveCrest.startNewSubPath (0.0f, wyBase);
                    for (int s = 1; s <= 24; ++s)
                    {
                        const float wx = W * (float) s / 24.0f;
                        const float wy = wyBase + std::sin ((float) s * 0.65f + (float) wv * 1.4f) * (10.0f + wv * 2.0f)
                                                + std::cos ((float) s * 1.15f - (float) wv) * 5.0f;
                        waveFill.lineTo (wx, wy);
                        waveCrest.lineTo (wx, wy);
                    }
                    waveFill.lineTo (W, H);
                    waveFill.closeSubPath();
                    cg.setGradientFill (juce::ColourGradient (juce::Colour (0x2448cae4), 0.0f, wyBase,
                                                              juce::Colours::transparentBlack, 0.0f, wyBase + 95.0f, false));
                    cg.fillPath (waveFill);
                    cg.setColour (juce::Colour (0xffcaf0f8).withAlpha (0.58f - (float) wv * 0.06f));
                    cg.strokePath (waveCrest, juce::PathStrokeType (2.0f - (float) wv * 0.15f, juce::PathStrokeType::curved));
                }

                // 5. 水面に広がる同心円の水紋リング（Water Ripples）
                for (int rp = 0; rp < 7; ++rp)
                {
                    const float rx = W * (0.12f + 0.76f * hash01 (rp * 29 + 5));
                    const float ry = H * (0.18f + 0.68f * hash01 (rp * 29 + 17));
                    const float rw = 36.0f + 68.0f * hash01 (rp * 29 + 23);
                    const float rh = rw * 0.34f;
                    cg.setColour (juce::Colour (0xffcaf0f8).withAlpha (0.38f));
                    cg.drawEllipse (rx - rw, ry - rh, rw * 2.0f, rh * 2.0f, 1.3f);
                    cg.setColour (juce::Colour (0xff00f5d4).withAlpha (0.26f));
                    cg.drawEllipse (rx - rw * 0.62f, ry - rh * 0.62f, rw * 1.24f, rh * 1.24f, 1.0f);
                }

                // 6. ぷるんと弾ける立体3D水滴＆立ち昇る気泡ストリーム（48個）
                for (int d = 0; d < 48; ++d)
                {
                    const float dx = W * (0.03f + 0.94f * hash01 (d * 23 + 9));
                    const float dy = H * (0.05f + 0.90f * hash01 (d * 23 + 21));
                    const float dr = (d % 5 == 0) ? (10.0f + 11.0f * hash01 (d * 23 + 33))
                                                  : (2.8f + 6.5f * hash01 (d * 23 + 33));
                    const juce::Colour dc = (d % 3 == 0) ? juce::Colour (0xff00f5d4)
                                          : (d % 3 == 1) ? juce::Colour (0xff48cae4)
                                                         : juce::Colour (0xff90e0ef);
                    drawWaterDroplet (cg, dx, dy, dr, dc, 0.65f + 0.30f * hash01 (d * 11 + 3));
                }

                // 7. 水面の陽光スパークル（18個）
                for (int s = 0; s < 18; ++s)
                {
                    const float sx = W * (0.08f + 0.84f * hash01 (s * 31 + 7));
                    const float sy = H * (0.04f + 0.42f * hash01 (s * 31 + 19));
                    drawStar8 (cg, sx, sy, 6.0f + 7.0f * hash01 (s * 31 + 29), juce::Colour (0xfffff3b0).withAlpha (0.80f));
                }
            }
            else if (mode == 16)
            {
                // =========================================================================
                // 🌅 Mode 16: 朝焼けの海と暁の水平線 (Radiant Dawn & Sunrise Sea)
                //   「朝焼け」— 暁のインディゴ〜ロイヤルパープル〜マゼンタロゼ〜黄金に輝く水平線
                //   ＋昇る朝日と16本の放射光芒（Crepuscular Rays）＋朝焼けの彩雲＋海面の黄金の光の道
                // =========================================================================
                const float horizonY = H * 0.48f;

                // 1. 暁の空〜水平線〜朝焼けの海の7段グラデーション
                juce::ColourGradient dawnSky (juce::Colour (0xff0f1035), 0.0f, 0.0f,
                                              juce::Colour (0xff1e0e24), 0.0f, H, false);
                dawnSky.addColour (0.16, juce::Colour (0xff2e1a54));   // 夜明け前のロイヤル・バイオレット
                dawnSky.addColour (0.30, juce::Colour (0xff882963));   // 暁のクリムゾン・マゼンタロゼ
                dawnSky.addColour (0.41, juce::Colour (0xffe85d56));   // 朝焼けのコーラル・バーミリオン
                dawnSky.addColour (0.48, juce::Colour (0xffffd166));   // 水平線のブリリアント・サンライズゴールド
                dawnSky.addColour (0.56, juce::Colour (0xffd9534f));   // 海面に映る朝焼けロゼ
                dawnSky.addColour (0.76, juce::Colour (0xff4a1f43));   // 朝凪のディープ・ワインバイオレット海面
                cg.setGradientFill (dawnSky);
                cg.fillRect (rz);

                // 2. 水平線から天頂へ向かって扇状に広がる16本の朝日の光芒（Crepuscular Sunbeams）
                const float sunX = W * 0.50f;
                for (int ray = 0; ray < 16; ++ray)
                {
                    const float angle0 = -1.42f + (float) ray * (2.84f / 15.0f) - 0.045f;
                    const float angle1 = angle0 + 0.09f;
                    const float rayLen = maxD * 0.85f;
                    juce::Path beam;
                    beam.startNewSubPath (sunX, horizonY);
                    beam.lineTo (sunX + std::sin (angle0) * rayLen, horizonY - std::cos (angle0) * rayLen);
                    beam.lineTo (sunX + std::sin (angle1) * rayLen, horizonY - std::cos (angle1) * rayLen);
                    beam.closeSubPath();
                    const juce::Colour beamCol = (ray % 2 == 0) ? juce::Colour (0x36ffea9f) : juce::Colour (0x28ff758f);
                    cg.setGradientFill (juce::ColourGradient (beamCol, sunX, horizonY,
                                                              beamCol.withAlpha (0.0f),
                                                              sunX + std::sin (angle0) * rayLen * 0.75f,
                                                              horizonY - std::cos (angle0) * rayLen * 0.75f, false));
                    cg.fillPath (beam);
                }

                // 3. 水平線から昇る黄金の太陽（Rising Sun Disc & Triple Corona）
                cg.setGradientFill (juce::ColourGradient (juce::Colour (0x99ffbe0b), sunX, horizonY,
                                                          juce::Colour (0x00ff6b8b), sunX + W * 0.42f, horizonY, true));
                cg.fillRect (rz);
                {
                    juce::Graphics::ScopedSaveState ss (cg);
                    cg.reduceClipRegion (0, 0, (int) W, (int) horizonY + 2);
                    const float sunR = juce::jmin (W, H) * 0.115f;
                    cg.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff), sunX, horizonY - sunR * 0.25f,
                                                              juce::Colour (0xffff9e00), sunX + sunR, horizonY, true));
                    cg.fillEllipse (sunX - sunR, horizonY - sunR * 0.82f, sunR * 2.0f, sunR * 2.0f);
                }

                // 4. 朝日に下から黄金・ロゼ色に照らされる彩雲（Sunlit Dawn Clouds — 10個）
                for (int cl = 0; cl < 10; ++cl)
                {
                    const float cx = W * (0.06f + 0.88f * hash01 (cl * 23 + 5));
                    const float cy = horizonY * (0.22f + 0.68f * hash01 (cl * 23 + 13));
                    const float cw = 90.0f + 140.0f * hash01 (cl * 23 + 19);
                    const float ch = 16.0f + 22.0f * hash01 (cl * 23 + 29);
                    juce::Path cloud;
                    cloud.addRoundedRectangle (cx - cw * 0.5f, cy - ch * 0.5f, cw, ch, ch * 0.5f);
                    cloud.addEllipse (cx - cw * 0.32f, cy - ch * 0.95f, cw * 0.42f, ch * 1.2f);
                    cloud.addEllipse (cx - cw * 0.02f, cy - ch * 1.15f, cw * 0.38f, ch * 1.4f);
                    cg.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a1f52).withAlpha (0.58f), cx, cy - ch,
                                                              juce::Colour (0xffffb703).withAlpha (0.72f), cx, cy + ch * 0.5f, false));
                    cg.fillPath (cloud);
                }

                // 5. 天頂に残る暁の一番星と星屑（36粒）
                for (int i = 0; i < 36; ++i)
                {
                    const float sx = W * hash01 (i * 17 + 9);
                    const float sy = horizonY * 0.55f * hash01 (i * 17 + 23);
                    const float sr = 1.0f + 1.8f * hash01 (i * 17 + 31);
                    cg.setColour (juce::Colour (0xfffff8f2).withAlpha ((1.0f - sy / (horizonY * 0.6f)) * 0.78f));
                    cg.fillEllipse (sx - sr * 0.5f, sy - sr * 0.5f, sr, sr);
                }
                drawStar8 (cg, W * 0.22f, horizonY * 0.22f, 9.5f, juce::Colour (0xfffff3b0)); // 暁の明星（金星）

                // 6. 海面（horizonY .. H）に輝く朝日の光の道（Golden Sunrise Shimmer Path — 90本）
                for (int rp = 0; rp < 90; ++rp)
                {
                    const float t = (float) rp / 89.0f;
                    const float ry = horizonY + (H - horizonY) * (t * t * 0.80f + t * 0.20f);
                    const float spread = W * (0.08f + 0.58f * t);
                    const float jitter = (hash01 (rp * 13 + 7) - 0.5f) * spread * 0.35f;
                    const float rx0 = sunX + jitter - spread * (0.35f + 0.65f * hash01 (rp * 11 + 3));
                    const float rx1 = sunX + jitter + spread * (0.35f + 0.65f * hash01 (rp * 11 + 9));
                    const juce::Colour rc = (rp % 3 == 0) ? juce::Colour (0xfffff3b0)
                                          : (rp % 3 == 1) ? juce::Colour (0xffffbe0b)
                                                          : juce::Colour (0xffff6b8b);
                    const float ra = (1.0f - t * 0.55f) * 0.58f;
                    const float rh = 1.6f + t * 2.8f;
                    cg.setGradientFill (juce::ColourGradient (rc.withAlpha (0.0f), rx0, ry,
                                                              rc.withAlpha (ra), sunX + jitter, ry, false));
                    cg.fillRect (rx0, ry, (sunX + jitter) - rx0, rh);
                    cg.setGradientFill (juce::ColourGradient (rc.withAlpha (ra), sunX + jitter, ry,
                                                              rc.withAlpha (0.0f), rx1, ry, false));
                    cg.fillRect (sunX + jitter, ry, rx1 - (sunX + jitter), rh);
                }

                // 7. 海面のダイヤモンド・サンライズグリッター（24個）
                for (int sp = 0; sp < 24; ++sp)
                {
                    const float t = hash01 (sp * 19 + 5);
                    const float sx = sunX + (hash01 (sp * 19 + 11) - 0.5f) * W * (0.16f + 0.60f * t);
                    const float sy = horizonY + 8.0f + t * (H - horizonY - 16.0f);
                    drawStar4 (cg, sx, sy, 4.5f + 5.5f * hash01 (sp * 19 + 23), juce::Colour (0xfffff8eb).withAlpha (0.82f));
                }
            }
            else if (mode == 17)
            {
                // =========================================================================
                // 🍁 Mode 17: 晩秋から初冬へ・茜空と初雪の水鏡 (Late Autumn to Early Winter Melancholy)
                //   「これから冬が近づく秋の哀愁ある感じ」
                //   冷たい初冬のスレート藍空と晩秋の夕茜が交わる空＋遠山の霞シルエットと白い川霧
                //   ＋左右上隅から枝垂れる紅葉の枝＋舞い散る5裂カエデ（🍁）と初雪の結晶（❄）＋静寂の湖面波紋
                // =========================================================================
                const float horizonY = H * 0.50f;

                // 1. 初冬の冷たい藍空〜晩秋の茜色〜静寂の湖面水鏡グラデーション
                juce::ColourGradient autumnWinter (juce::Colour (0xff0c1324), 0.0f, 0.0f,
                                                   juce::Colour (0xff120e16), 0.0f, H, false);
                autumnWinter.addColour (0.22, juce::Colour (0xff1e2942));   // 初冬の冷たいウィンター・スレートブルー
                autumnWinter.addColour (0.36, juce::Colour (0xff5e2c3e));   // 暮れゆく晩秋のトワイライト・プラム
                autumnWinter.addColour (0.45, juce::Colour (0xffc84b31));   // 哀愁の夕茜（あかね）バーミリオン
                autumnWinter.addColour (0.50, juce::Colour (0xfff0944d));   // 遠山の端に残る琥珀の残照
                autumnWinter.addColour (0.58, juce::Colour (0xff8c3832));   // 湖面に映る茜色の水鏡
                autumnWinter.addColour (0.78, juce::Colour (0xff241c2c));   // 冬の影が落ちる深淵の湖底
                cg.setGradientFill (autumnWinter);
                cg.fillRect (rz);

                // 2. 遠山の3層シルエット（Misty Distant Mountain Ridges）＋山あいを流れる白い朝靄・川霧
                for (int mt = 0; mt < 3; ++mt)
                {
                    juce::Path ridge;
                    const float baseY = horizonY - (2 - mt) * 18.0f;
                    ridge.startNewSubPath (0.0f, horizonY + 10.0f);
                    for (int x = 0; x <= 36; ++x)
                    {
                        const float px = W * (float) x / 36.0f;
                        const float h1 = std::sin ((float) x * 0.22f + (float) mt * 1.8f) * (28.0f - mt * 6.0f);
                        const float h2 = std::cos ((float) x * 0.55f - (float) mt * 0.9f) * (14.0f - mt * 3.0f);
                        const float py = baseY - std::abs (h1 + h2);
                        ridge.lineTo (px, py);
                    }
                    ridge.lineTo (W, horizonY + 10.0f);
                    ridge.closeSubPath();
                    const juce::Colour mc = (mt == 0) ? juce::Colour (0xff4a2c3d).withAlpha (0.65f)
                                          : (mt == 1) ? juce::Colour (0xff2e1d2d).withAlpha (0.80f)
                                                      : juce::Colour (0xff181220).withAlpha (0.92f);
                    cg.setColour (mc);
                    cg.fillPath (ridge);

                    // 山裾をたなびく白い初冬の川霧・冬霞（Winter River Mist）
                    cg.setGradientFill (juce::ColourGradient (juce::Colour (0x38e2e8f0), 0.0f, baseY - 4.0f,
                                                              juce::Colours::transparentWhite, 0.0f, baseY + 26.0f, false));
                    cg.fillRect (0.0f, baseY - 12.0f, W, 38.0f);
                }

                // 3. 湖面（horizonY .. H）に広がる静寂の波紋と水面に浮かぶ落ち葉
                for (int rp = 0; rp < 8; ++rp)
                {
                    const float rx = W * (0.10f + 0.80f * hash01 (rp * 23 + 7));
                    const float ry = horizonY + 24.0f + (H - horizonY - 36.0f) * hash01 (rp * 23 + 17);
                    const float rw = 42.0f + 85.0f * hash01 (rp * 23 + 29);
                    const float rh = rw * 0.24f;
                    cg.setColour (juce::Colour (0xfffde6c4).withAlpha (0.30f));
                    cg.drawEllipse (rx - rw, ry - rh, rw * 2.0f, rh * 2.0f, 1.2f);
                    cg.setColour (juce::Colour (0xffbae6fd).withAlpha (0.22f));
                    cg.drawEllipse (rx - rw * 0.58f, ry - rh * 0.58f, rw * 1.16f, rh * 1.16f, 0.9f);
                }

                // 4. 左右上隅（ヘッダ直下 H*0.14〜0.38）から画面へ枝垂れる晩秋の紅葉枝シルエット
                for (int side : { 0, 1 })
                {
                    const float sx = (side == 0) ? 0.0f : W;
                    const float dir = (side == 0) ? 1.0f : -1.0f;
                    juce::Path branch;
                    branch.startNewSubPath (sx, H * 0.14f);
                    branch.quadraticTo (sx + dir * W * 0.14f, H * 0.22f, sx + dir * W * 0.26f, H * 0.34f);
                    cg.setColour (juce::Colour (0xff241018).withAlpha (0.85f));
                    cg.strokePath (branch, juce::PathStrokeType (3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    for (int lf = 0; lf < 12; ++lf)
                    {
                        const float t = (float) lf / 11.0f;
                        const float lx = sx + dir * (W * 0.02f + t * W * 0.24f) + (hash01 (lf * 13 + side * 7) - 0.5f) * 28.0f;
                        const float ly = H * (0.14f + t * 0.22f) + (hash01 (lf * 13 + side * 19) - 0.5f) * 26.0f;
                        const juce::Colour lc = (lf % 3 == 0) ? juce::Colour (0xffff2e4c)
                                              : (lf % 3 == 1) ? juce::Colour (0xffff9e00)
                                                              : juce::Colour (0xffef233c);
                        drawMapleLeaf (cg, lx, ly, 11.5f + 6.0f * hash01 (lf * 7), (float) lf * 0.8f, lc, 0.92f);
                    }
                }

                // 5. 秋風に舞い散る紅葉（28枚）× 空から舞い降りる初雪の結晶（26個）の邂逅
                for (int m = 0; m < 28; ++m)
                {
                    const float mx = W * (0.04f + 0.92f * hash01 (m * 31 + 5));
                    const float my = H * (0.14f + 0.80f * hash01 (m * 31 + 19));
                    const float mSz = 10.0f + 9.5f * hash01 (m * 31 + 29);
                    const float mRot = hash01 (m * 31 + 37) * 6.28f;
                    const juce::Colour mc = (m % 3 == 0) ? juce::Colour (0xffff2e4c)   // 深紅のもみじ
                                          : (m % 3 == 1) ? juce::Colour (0xffff7b00)   // 茜オレンジ
                                                         : juce::Colour (0xffffbe0b);  // 黄金の銀杏・琥珀葉
                    drawMapleLeaf (cg, mx, my, mSz, mRot, mc, 0.88f);
                }
                for (int sn = 0; sn < 26; ++sn)
                {
                    const float sx = W * (0.04f + 0.92f * hash01 (sn * 29 + 11));
                    const float sy = H * (0.08f + 0.78f * hash01 (sn * 29 + 23));
                    const float sSz = 6.5f + 8.5f * hash01 (sn * 29 + 31);
                    const float sRot = hash01 (sn * 29 + 43) * 3.14f;
                    drawSnowCrystal (cg, sx, sy, sSz, sRot, juce::Colour (0xffe0f2fe), 0.86f);
                }
            }
            else
            {
                // =========================================================================
                // 👑 Mode 18: 蒼海オペラ座と黄金シャンデリア (Grand Opera Sapphire & Chandelier)
                //   ロイヤルサファイアの深海オペラ舞台＋左右のベルベット幕＆金のタッセルロープ
                //   ＋天頂中央の豪華な24Kゴールド＆クリスタル・シャンデリア＋黄金玉ボケ
                // =========================================================================
                juce::ColourGradient stage (juce::Colour (0xff030a1e), 0.0f, 0.0f,
                                            juce::Colour (0xff061538), 0.0f, H, false);
                stage.addColour (0.50, juce::Colour (0xff0c266e));
                cg.setGradientFill (stage);
                cg.fillRect (rz);

                // 1. 舞台中央のエレクトリック・ロイヤルサファイア背面スポットライト
                cg.setGradientFill (juce::ColourGradient (juce::Colour (0x802563eb), W * 0.50f, H * 0.44f,
                                                          juce::Colour (0x00040b20), W * 0.50f + maxD * 0.48f, H * 0.44f, true));
                cg.fillRect (rz);
                cg.setGradientFill (juce::ColourGradient (juce::Colour (0x55fde68a), W * 0.50f, H * 0.10f,
                                                          juce::Colour (0x00d4af37), W * 0.50f + maxD * 0.32f, H * 0.10f, true));
                cg.fillRect (rz);

                // 2. 天井から降り注ぐシャンデリア＆オペラ座の光条（9本）
                for (int ray = -4; ray <= 4; ++ray)
                {
                    const float topX = W * 0.50f + (float) ray * (W * 0.055f);
                    const float botX = W * 0.50f + (float) ray * (W * 0.16f);
                    juce::Path beam;
                    beam.startNewSubPath (topX - 18.0f, 0.0f);
                    beam.lineTo (topX + 18.0f, 0.0f);
                    beam.lineTo (botX + 64.0f, H);
                    beam.lineTo (botX - 64.0f, H);
                    beam.closeSubPath();
                    const juce::Colour rayCol = (std::abs (ray) <= 1) ? juce::Colour (0x28f2c94c) : juce::Colour (0x203b82f6);
                    cg.setGradientFill (juce::ColourGradient (rayCol, topX, 0.0f, rayCol.withAlpha (0.0f), botX, H * 0.90f, false));
                    cg.fillPath (beam);
                }

                // 3. 左右のロイヤル・サファイアベルベット劇場ドレープ幕＋金のタッセルロープ
                for (int side : { 0, 1 })
                {
                    const float dir = (side == 0) ? 1.0f : -1.0f;
                    const float edgeX = (side == 0) ? 0.0f : W;
                    for (int fold = 0; fold < 5; ++fold)
                    {
                        const float fw = 24.0f + (float) fold * 18.0f;
                        const float fx = (side == 0) ? (float) fold * 16.0f : W - fw - (float) fold * 16.0f;
                        cg.setGradientFill (juce::ColourGradient (
                            juce::Colour (0xff03091c).withAlpha (0.78f - fold * 0.12f), fx, 0.0f,
                            juce::Colour (0xff1d4ed8).withAlpha (0.32f), fx + fw, 0.0f, false));
                        cg.fillRect (fx, 0.0f, fw, H);
                    }
                    // 金のタッセル編みロープ
                    juce::Path rope;
                    rope.startNewSubPath (edgeX, H * 0.36f);
                    rope.quadraticTo (edgeX + dir * 58.0f, H * 0.44f, edgeX + dir * 92.0f, H * 0.38f);
                    cg.setColour (juce::Colour (0xffd4af37).withAlpha (0.85f));
                    cg.strokePath (rope, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }

                // 4. 天頂中央のグラン・クリスタルシャンデリア（3段ガラスガーランド＆キャンドル光輪）
                const float chX = W * 0.50f;
                for (int tier = 0; tier < 3; ++tier)
                {
                    const float tw = (220.0f - tier * 54.0f);
                    const float ty = 12.0f + tier * 24.0f;
                    cg.setColour (juce::Colour (0xffd4af37).withAlpha (0.82f));
                    cg.drawLine (chX - tw * 0.5f, ty, chX + tw * 0.5f, ty, 2.0f);
                    const int drops = 9 - tier * 2;
                    for (int d = 0; d < drops; ++d)
                    {
                        const float dx = chX - tw * 0.5f + tw * (float) d / (float) (drops - 1);
                        cg.setColour (juce::Colour (0xfffef08a).withAlpha (0.85f));
                        cg.fillEllipse (dx - 2.5f, ty + 3.0f, 5.0f, 8.0f);
                        if (d < drops - 1)
                        {
                            const float nx = chX - tw * 0.5f + tw * (float) (d + 1) / (float) (drops - 1);
                            juce::Path swag;
                            swag.startNewSubPath (dx, ty);
                            swag.quadraticTo ((dx + nx) * 0.5f, ty + 14.0f, nx, ty);
                            cg.setColour (juce::Colour (0x99fef08a));
                            cg.strokePath (swag, juce::PathStrokeType (1.2f));
                        }
                    }
                }
                drawStar8 (cg, chX, 44.0f, 14.0f, juce::Colour (0xfffef08a));

                // 5. シャンデリアの黄金ボケ＆深海サファイアジュエルボケ（32個）
                for (int b = 0; b < 32; ++b)
                {
                    const float bx = W * (0.06f + 0.88f * hash01 (b * 31 + 5));
                    const float by = H * (0.05f + 0.88f * hash01 (b * 31 + 19));
                    const float br = 7.0f + 28.0f * hash01 (b * 31 + 37);
                    const juce::Colour bc = (b % 3 == 0) ? juce::Colour (0xfff2c94c)
                                          : (b % 3 == 1) ? juce::Colour (0xff3b82f6)
                                                         : juce::Colour (0xfffbe6a2);
                    drawLensBokeh (cg, bx, by, br, bc, 0.28f + 0.32f * hash01 (b * 13 + 7));
                }
            }

            // 世界観別パーティクル演出も背景キャッシュへ一括描画（毎フレームの再計算・グラデーション生成ゼロ）
            for (int f = 0; f < 10; ++f)
            {
                const float phase = (float) f * 1.7f;
                const float fx = rz.getX() + rz.getWidth()  * (0.08f + 0.84f * hash01 (f * 37 + 13)) + std::sin (phase) * 14.0f;
                const float fy = rz.getY() + rz.getHeight() * (0.12f + 0.76f * hash01 (f * 37 + 29)) + std::cos (phase * 0.8f) * 10.0f;
                const float pulse = 0.65f;
                const float fr = (5.0f + 9.0f * hash01 (f * 19 + 3));
                if (mode == 15)
                {
                    drawWaterDroplet (cg, fx, fy, fr * 0.65f, Palette::ice, 0.40f);
                }
                else if (mode == 17)
                {
                    if (f % 2 == 0)
                        drawMapleLeaf (cg, fx, fy, fr * 0.90f, phase * 0.5f, Palette::salmon, 0.45f);
                    else
                        drawSnowCrystal (cg, fx, fy, fr * 0.75f, phase * 0.3f, Palette::ice, 0.45f);
                }
                else
                {
                    const juce::Colour fc = (f % 2 == 0) ? Palette::yellow : Palette::ice;
                    cg.setGradientFill (juce::ColourGradient (fc.withAlpha (0.20f * pulse), fx, fy,
                                                              fc.withAlpha (0.0f), fx + fr * 2.2f, fy, true));
                    cg.fillEllipse (fx - fr * 2.2f, fy - fr * 2.2f, fr * 4.4f, fr * 4.4f);
                    drawStar8 (cg, fx, fy, fr * 0.85f, fc.brighter (0.3f).withAlpha (0.38f));
                }
            }
        }
        g.drawImageAt (oceanStarCache, area.getX(), area.getY());
    }

    juce::Image yuruCache, brandCache, brandFgCache, auroraCache, liverCache, neonCache, studioCache, oceanStarCache, fgCache;
    int         yuruCacheMode { -1 }, liverCacheMode { -1 }, oceanStarCacheMode { -1 }, fgCacheMode { -1 };

    // ---- ライブ (大型機材) ----
    // ---- ペンライトの海 (5人ユニゾンON演出) ----
    void drawPenlights (juce::Graphics& g, juce::Rectangle<float> r, float a)
    {
        static const juce::uint32 pc[] = { 0xffff6fa0, 0xff53d4f2, 0xffffd75c,
                                           0xffb08cff, 0xff5ce8a8 };
        const float top = r.getBottom() - r.getHeight()*0.20f;
        int idx = 0;
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 18; ++col, ++idx)
            {
                const float x = r.getX() + (0.5f + col) * r.getWidth() / 18.0f
                              + (hash01(idx) - 0.5f) * 18
                              + std::sin (animT*1.6f + idx*0.7f) * 5;
                const float y = top + (0.3f + row) * r.getHeight()*0.20f / 5.0f
                              + (hash01(idx+200) - 0.5f) * 10;
                const juce::Colour c (pc[idx % 5]);
                g.setColour (c.withAlpha (0.16f * a));
                g.fillEllipse (x-7, y-7, 14, 14);
                g.setColour (c.withAlpha (0.9f * a));
                g.fillEllipse (x-2.6f, y-2.6f, 5.2f, 5.2f);
            }
    }

    // ---- 前景: パネルの上に大きく透過配置 ----
    void paintForeground (juce::Graphics& g)
    {
        auto r = area.toFloat();
        if (mode == 2)
        {
            drawParts (g, 0.6f);                 // 小粒パーティクルのみ(カーソルで押せる)
        }
        else if (mode == 4)
        {
            // 上端だけ薄いオーロラのベール + 星の瞬き (操作域は素通し)
            static const juce::uint32 ac[2] = { 0xff43e8b8, 0xff6fb8ff };
            for (int b2 = 0; b2 < 2; ++b2)
            {
                juce::Path top;
                const float base = r.getY() + 26 + 16 * b2;
                for (int sgx = 0; sgx <= 20; ++sgx)
                {
                    const float x = r.getX() + r.getWidth() * sgx / 20.0f;
                    const float y = base + std::sin (x * 0.007f + animT * 0.4f + b2 * 2)
                                        * (12 + 6 * b2);
                    if (sgx == 0) top.startNewSubPath (x, y); else top.lineTo (x, y);
                }
                g.setColour (juce::Colour (ac[b2]).withAlpha (0.12f + 0.10f * level));
                g.strokePath (top, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved));
            }
        }
        else if (mode == 3 || mode == 6 || mode == 8 || Palette::isCoutureTheme (mode) || Palette::isOceanStarTheme (mode))
        {
            // ★ すべての静的前景オーバーレイ（ブランド・よる・クチュール・星空＆海＆季節・ネオン）を1枚の透過画像にキャッシュ！
            //   これで子コンポーネント（ノブ・オーブ・EQ・チューナー）が部分再描画されるたびに
            //   12個の巨大レンズボケや水滴・紅葉・星屑をソフトウェア再計算していた重さを完全にゼロにする。
            if (! fgCache.isValid()
                || fgCacheMode        != mode
                || fgCache.getWidth()  != area.getWidth()
                || fgCache.getHeight() != area.getHeight())
            {
                fgCache = juce::Image (juce::Image::ARGB,
                                       juce::jmax (1, area.getWidth()),
                                       juce::jmax (1, area.getHeight()), true);
                fgCacheMode = mode;
                juce::Graphics cg (fgCache);
                auto rz = area.toFloat().withZeroOrigin();

                if (mode == 3)
                {
                    drawMonoCanvas (cg, rz, 0.08f);
                    drawWeaveBand (cg, { rz.getX(), rz.getY(),              rz.getWidth(), 14.0f }, 0.55f);
                    drawWeaveBand (cg, { rz.getX(), rz.getBottom() - 14.0f, rz.getWidth(), 14.0f }, 0.55f);
                    const float m = 24.0f, sm = 19.0f, hdr = 72.0f;
                    drawMedallion (cg, rz.getX() + m,      rz.getY() + hdr + m, sm, 0.55f);
                    drawMedallion (cg, rz.getRight() - m,  rz.getY() + hdr + m, sm, 0.55f);
                    drawMedallion (cg, rz.getX() + m,      rz.getBottom() - m, sm, 0.55f);
                    drawMedallion (cg, rz.getRight() - m,  rz.getBottom() - m, sm, 0.55f);
                }
                else if (mode == 8)
                {
                    juce::uint32 s = 0x2545f491u;
                    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 16777216.0f; };
                    for (int i = 0; i < 46; ++i)
                    {
                        const float x  = rz.getX() + rz.getWidth()  * rnd();
                        const float y  = rz.getY() + rz.getHeight() * rnd();
                        const float sz = 1.2f + rnd() * 1.5f;
                        cg.setColour (juce::Colours::white.withAlpha (0.05f + rnd() * 0.04f));
                        cg.fillEllipse (x, y, sz, sz);
                    }
                }
                else if (Palette::isCoutureTheme (mode))
                {
                    for (int i = 0; i < 16; ++i)
                    {
                        const float sx = rz.getX() + rz.getWidth()  * hash01 (i * 19 + 5);
                        const float sy = rz.getY() + rz.getHeight() * hash01 (i * 19 + 11);
                        const float pulse = 0.5f + 0.5f * std::sin ((float) i * 1.3f);
                        const float sr = (4.5f + 4.5f * hash01 (i * 7)) * (0.7f + 0.5f * pulse);
                        const juce::Colour sc = (i % 2 == 0) ? Palette::salmon : Palette::blue;
                        drawStar4 (cg, sx, sy, sr, sc.withAlpha (0.14f + 0.14f * pulse));
                    }
                }
                else if (Palette::isOceanStarTheme (mode))
                {
                    const int oIdx = Palette::oceanStarIndex (mode);
                    if (oIdx == 1)
                    {
                        for (int d = 0; d < 18; ++d)
                        {
                            const float dx = rz.getX() + rz.getWidth()  * (0.02f + 0.96f * hash01 (d * 37 + 7));
                            const float dy = rz.getY() + rz.getHeight() * (0.08f + 0.88f * hash01 (d * 37 + 19));
                            const float dr = 3.5f + 7.5f * hash01 (d * 37 + 29);
                            drawWaterDroplet (cg, dx, dy, dr, Palette::ice, 0.36f);
                        }
                    }
                    else if (oIdx == 2)
                    {
                        const float horY = rz.getY() + rz.getHeight() * 0.48f;
                        cg.setGradientFill (juce::ColourGradient (juce::Colour (0x24ffbe0b), rz.getCentreX(), horY,
                                                                  juce::Colours::transparentBlack, rz.getCentreX(), horY - rz.getHeight() * 0.25f, false));
                        cg.fillRect (rz.getX(), horY - rz.getHeight() * 0.25f, rz.getWidth(), rz.getHeight() * 0.25f);
                        cg.setGradientFill (juce::ColourGradient (juce::Colour (0x20ff6b8b), rz.getCentreX(), horY,
                                                                  juce::Colours::transparentBlack, rz.getCentreX(), horY + rz.getHeight() * 0.25f, false));
                        cg.fillRect (rz.getX(), horY, rz.getWidth(), rz.getHeight() * 0.25f);
                    }
                    else if (oIdx == 3)
                    {
                        for (int i = 0; i < 12; ++i)
                        {
                            const float px = rz.getX() + rz.getWidth()  * (0.02f + 0.96f * hash01 (i * 41 + 5));
                            const float py = rz.getY() + rz.getHeight() * (0.08f + 0.88f * hash01 (i * 41 + 17));
                            if (i % 2 == 0)
                                drawMapleLeaf (cg, px, py, 7.5f + 5.0f * hash01 (i * 13), hash01 (i * 29) * 6.28f,
                                               (i % 4 == 0) ? juce::Colour (0xffe11d48) : juce::Colour (0xfff59e0b), 0.34f);
                            else
                                drawSnowCrystal (cg, px, py, 5.5f + 4.5f * hash01 (i * 19), hash01 (i * 31) * 3.14f,
                                                 juce::Colour (0xffe0f2fe), 0.36f);
                        }
                    }
                    else
                    {
                        for (int b = 0; b < 12; ++b)
                        {
                            const float bx = rz.getX() + rz.getWidth()  * (0.06f + 0.88f * hash01 (b * 37 + oIdx * 13 + 5));
                            const float by = rz.getY() + rz.getHeight() * (0.08f + 0.84f * hash01 (b * 37 + oIdx * 13 + 19));
                            const float br = 16.0f + 32.0f * hash01 (b * 37 + 29);
                            const juce::Colour bc = (b % 3 == 0) ? Palette::yellow
                                                  : (b % 3 == 1) ? Palette::ice : Palette::salmon;
                            drawLensBokeh (cg, bx, by, br, bc, 0.08f + 0.04f * (float) (b % 2));
                        }
                    }
                    for (int i = 0; i < 18; ++i)
                    {
                        const float sx = rz.getX() + rz.getWidth()  * hash01 (i * 23 + 7);
                        const float sy = rz.getY() + rz.getHeight() * hash01 (i * 23 + 13);
                        const float pulse = 0.5f + 0.5f * std::sin ((float) i * 1.1f);
                        const float sr = (4.0f + 4.8f * hash01 (i * 9 + 3)) * (0.75f + 0.45f * pulse);
                        const juce::Colour sc = (i % 3 == 0) ? Palette::yellow : (i % 3 == 1 ? Palette::ice : Palette::salmon);
                        drawStar4 (cg, sx, sy, sr, sc.withAlpha (0.14f + 0.12f * pulse));
                    }
                }
                else if (mode == 6)
                {
                    cg.setColour (juce::Colour (0xff00f0ff).withAlpha (0.28f));
                    const float L = 24.0f, M = 6.0f;
                    cg.drawLine (rz.getX() + M, rz.getY() + M, rz.getX() + M + L, rz.getY() + M, 2.0f);
                    cg.drawLine (rz.getX() + M, rz.getY() + M, rz.getX() + M, rz.getY() + M + L, 2.0f);
                    cg.drawLine (rz.getRight() - M - L, rz.getY() + M, rz.getRight() - M, rz.getY() + M, 2.0f);
                    cg.drawLine (rz.getRight() - M, rz.getY() + M, rz.getRight() - M, rz.getY() + M + L, 2.0f);
                    cg.setColour (juce::Colour (0xffff2a85).withAlpha (0.28f));
                    cg.drawLine (rz.getX() + M, rz.getBottom() - M, rz.getX() + M + L, rz.getBottom() - M, 2.0f);
                    cg.drawLine (rz.getX() + M, rz.getBottom() - M - L, rz.getX() + M, rz.getBottom() - M, 2.0f);
                    cg.drawLine (rz.getRight() - M - L, rz.getBottom() - M, rz.getRight() - M, rz.getBottom() - M, 2.0f);
                    cg.drawLine (rz.getRight() - M, rz.getBottom() - M - L, rz.getRight() - M, rz.getBottom() - M, 2.0f);
                }
            }
            g.drawImageAt (fgCache, area.getX(), area.getY());
        }
        if (jnActive)
            drawPenlights (g, r, 0.5f);
    }
    juce::Image          mascot;
    juce::Rectangle<int> area;
    int                  mode { 0 };
};

//==============================================================================
// v3.0-c 「音のとおり道」— 画面の左に**常時**出るカードの列
//
//  設計書 §4「骨格（案A）」の左1/4。前回は吹き出しの中に作って中身だけ先に
//  固めた。今回それを常設の列へ移し、ミニメーターを足す。
//
//  カード1枚 = 番号 / 名前 / ひとこと / はたらき量 / ON-OFF。
//
//  文字の規律（設計書 §4-0）をそのまま持ち込む:
//   * 名前17px・ひとこと14px（100%表示のとき）。**14px未満にしない**
//   * 押す物は高さ30px以上（トグルは 62×30）
//   * 箱が文字に合わせて広がる。入り切らないときは**折り返す**（省略しない）
//   * それでも縦に入り切らないときは列ごと**スクロール**する（Viewport）
// v3.0-c ★メーターの帯は**独立した不透明な子**にする。
//  理由: カードは下地が透けているので、カードに repaint を頼むと
//  その席の背景（テーマの絵・マスコット・ガラス）まで描き直される。
//  実測で 1枚あたり 0.70ms、8枚×20回/秒で **毎秒112ms**（1コアの11%）だった。
//  不透明な子にすると JUCE は親を描き直さないので、実測 0.003ms まで落ちる。
class MeterBar : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        if (isOpaque()) g.fillAll (bg);
        const float mh = (float) getHeight();
        auto r = getLocalBounds().toFloat();
        g.setColour (track);
        g.fillRoundedRectangle (r, mh * 0.5f);
        if (level > 0.005f)
        {
            g.setColour (fill);
            g.fillRoundedRectangle (r.withWidth (juce::jmax (mh, r.getWidth() * level)), mh * 0.5f);
        }
    }
    void setLevel (float v)
    {
        v = juce::jlimit (0.0f, 1.0f, v);
        if (std::abs (v - level) < 0.02f) return;
        level = v; repaint();
    }
    // bg が不透明なときだけ opaque にできる（OFFのカードは半透明なので不可。
    // ただしOFFのときメーターは0で止まっているので、速さは要らない）
    void setColours (juce::Colour background, juce::Colour tr, juce::Colour fl)
    {
        bg = background; track = tr; fill = fl;
        setOpaque (background.isOpaque());
        repaint();
    }
private:
    float level { 0.0f };
    juce::Colour bg { juce::Colours::transparentBlack }, track, fill;
};

class ModuleCard : public juce::Component
{
    std::shared_ptr<tip::Language> language;
public:
    // v3.1 §4 endpoint: 0=ふつうの箱 / 1=入(マイクから) / 2=出(しあげ)
    //  「入」「出」は箱ではないので、スイッチも矢印も持たない。
    ModuleCard (VocalGzzioProcessor&, int index, int endpoint = 0, std::shared_ptr<tip::Language> language = std::make_shared<tip::Language>());

    void paint (juce::Graphics&) override;
    void resized() override;

    void  setUiScale (float s);
    void  refreshText();                       // 日本語/英語の切替
    void  refreshColours();                    // テーマ切替（色は都度取り直す）
    // v3.0-c 順番の分岐。動かせるカードにだけ矢印を出す（0=出さない / -1=上へ / +1=下へ）
    void  setMove (int dir, const juce::String& tip);
    void  setDisplayIndex (int n);             // 表示する番号（実際の順に振り直す）
    std::function<void()> onMove;
    int   preferredHeight (int width) const;   // 折り返し後の高さ
    int   wantedWidth() const;                 // 文字が切れない最小の幅
    bool  isOn() const { return sw.getToggleState(); }
    void  setMeter (float v);                  // 0.0〜1.0（はたらき量）
    // v3.1 §4「1つずつ」: いま見ている1枚。カードの地を押すと選ばれる。
    //  ★スイッチと矢印は子コンポーネントなので、そこを押しても選択は動かない
    //   （ON/OFF したいだけなのに画面が切り替わるのを避ける）。
    void  setSelected (bool s);
    bool  isSelected() const { return selected; }
    std::function<void(int)> onSelect;          // 引数はモジュール番号

    std::function<void()> onToggled;

private:
    juce::String name() const;
    juce::String note() const;
    int  noteLines (int width) const;
    juce::StringArray noteRows (int width) const;   // 日本語は自前で折り返す
    // 幅が足りないとき（文字を大きくしたとき）は、名前とスイッチを**縦に積む**。
    // 名前を縮めたり省略したりはしない。
    bool stacked (int width) const;
    void updateBarColours();

    // ★毎フレームの文字計測をやめるためのキャッシュ。
    //  日本語の折り返しは1文字ずつ幅を測るので、20Hz×8枚で回すと
    //  メッセージスレッドを食い潰す（実際、音が途切れる原因になった）。
    mutable juce::StringArray cachedRows;
    mutable juce::String      cachedNote;
    mutable int   cachedRowsW   { -1 };
    mutable float cachedRowsSc  { -1.0f };
    mutable bool  cachedRowsArrow { false };   // 矢印が出入りしたら折り返しも作り直す
    mutable int   cachedStackW  { -1 };
    mutable float cachedStackSc { -1.0f };
    mutable bool  cachedStacked { false };
    void mouseDown (const juce::MouseEvent&) override;   // v3.1 地を押したら選ぶ
    bool selected { false };
    int  endpoint { 0 };            // v3.1 0=箱 / 1=入 / 2=出
    juce::Rectangle<int> meterRect;   // 帯の席
    MeterBar bar;                     // 不透明な子。親を描き直させない

    VocalGzzioProcessor& proc;
    int   id { 0 };
    int   shown { 0 };          // 画面に出す番号-1（実際の順）
    int   moveDir { 0 };        // 0/-1/+1
    float scale { 1.0f };
    float meter { 0.0f };
    juce::TextButton sw;
    std::unique_ptr<juce::ArrowButton> moveBtn;   // ▲▼ はフォントに無いので図形で描く
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleCard)
};

// 押している「間だけ」効くボタン ＋ タブレットPC向けの「短タップ（チョン押し）ロック切替」。
// Windows タブレットの指長押し（右クリック判定リング）でも途切れず、手放しでも原音比較できるようにする。
class HoldButton : public juce::TextButton
{
public:
    std::function<void()> onDown, onUp;
    void mouseDown (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseDown (e);
        downMs = juce::Time::getMillisecondCounter();
        wasLatchedBeforeDown = latched;
        if (! held)
        {
            held = true;
            if (onDown) onDown();
            repaint();
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseUp (e);
        const juce::uint32 elapsed = juce::Time::getMillisecondCounter() - downMs;
        if (! wasLatchedBeforeDown && elapsed < 240 && getLocalBounds().expanded (12).contains (e.getPosition()))
        {
            latched = true;
            setToggleState (true, juce::dontSendNotification);
            repaint();
            return;
        }
        latched = false;
        setToggleState (false, juce::dontSendNotification);
        if (held) { held = false; if (onUp) onUp(); repaint(); }
    }
    // 押したままウィンドウの外へ出て離された場合の取りこぼしを防ぐ
    void focusLost (FocusChangeType t) override
    {
        juce::TextButton::focusLost (t);
        if (! latched && held) { held = false; if (onUp) onUp(); repaint(); }
    }
    bool isHeld() const noexcept { return held; }
    bool isLatched() const noexcept { return latched; }
private:
    bool held { false };
    bool latched { false };
    bool wasLatchedBeforeDown { false };
    juce::uint32 downMs { 0 };
};

// タブレットPCやタッチ画面で「ツマミを動かさずに説明だけ読みたい」「ダブルタップで確実に初期値へ戻したい」
// という操作をツマミ名ラベルへのタップ／ダブルタップで受け止める Label。
class TouchHintLabel : public juce::Label
{
public:
    std::function<void()> onTap;
    std::function<void()> onDoubleTap;
    void mouseDown (const juce::MouseEvent& e) override
    {
        juce::Label::mouseDown (e);
        if (onTap) onTap();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        juce::Label::mouseDoubleClick (e);
        if (onDoubleTap) onDoubleTap();
    }
};

// クリック時に標準のテキストメニューではなく、カーソルを合わせるだけで
// 選ぶ前に詳しい説明と音作りレシピが見られる「プリセット・パレット」を開く ComboBox。
class PresetPaletteComboBox : public juce::ComboBox
{
public:
    std::function<void()> onShowPalette;
    void showPopup() override
    {
        if (onShowPalette)
        {
            hidePopup();
            onShowPalette();
            return;
        }
        juce::ComboBox::showPopup();
    }
};

class PathRail : public juce::Component
{
    std::shared_ptr<tip::Language> language;
public:
    explicit PathRail (VocalGzzioProcessor&, std::shared_ptr<tip::Language> language = std::make_shared<tip::Language>());
    ~PathRail() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;   // くらべる中の幕
    void resized() override;

    void setUiScale (float s);
    void refreshText();
    void refreshColours();
    void tick();                               // 20Hz: メーターを取り込む
    int  wantedWidth() const;                  // 文字が切れない最小の幅
    int  preferredHeight (int width) const;

private:
    juce::StringArray headerRows (int width) const;
    int  headerHeight (int width) const;
    // v3.0-c いまの実際の順（分岐パラメータから作る）
    std::array<int, gz::ModuleChain::Count> currentOrder() const;
    void applyOrder();
    void updateLegacyVisibility();
public:
    // v3.1 §4「1つずつ」。選ばれているカード（モジュール番号）。
    void setSelected (int module);
    int  getSelected() const { return selectedModule; }
    // v3.1 その箱が「いま何番目を通るか」（順番の分岐を反映した表示上の位置）
    int  displayPosOf (int module) const;
    std::function<void(int)> onSelect;
private:
    int selectedModule { -1 };
private:

    VocalGzzioProcessor& proc;
    std::array<int, gz::ModuleChain::Count> shownOrder { -1 };  // -1 = まだ決めていない
    juce::OwnedArray<ModuleCard> cards;
    // v3.1 §4 案A の固定カード。箱ではないので cards には入れない
    //  （入れると「順番の分岐」や「ぜんぶ入れる/切る」の対象になってしまう）。
    std::unique_ptr<ModuleCard> inCard, outCard;
    juce::TextButton allOn, allOff;
    HoldButton       compareBtn;      // v3.0-c 押している間だけ素通し
public:
    // v3.1 §4「1つずつ」。列の中に置く（カードを見ている所で切り替えたいので）。
    juce::TextButton focusBtn;
    std::function<void(bool)> onFocusToggled;
private:
    // v3.0-c サビリフトの互換スイッチ。**意味があるときだけ**出す
    //  （いつも出しておくと、99%の人には関係のない札が常に居座ることになる）
    juce::TextButton legacyBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> legacyAtt;
    bool legacyShown { false };
    float scale { 1.0f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PathRail)
};

//==============================================================================
class VocalGzzioContent : public juce::Component, private juce::Timer
{
    std::shared_ptr<tip::Language> language { std::make_shared<tip::Language>() };
public:
    explicit VocalGzzioContent (VocalGzzioProcessor&);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;   // cross hover name / lite note (on top)
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // v1.7.0 theme cross-switch & Argon 2D Orb
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;   // cross hover (mode name)
    void mouseExit (const juce::MouseEvent&) override;

    void setFontScale (float s);
    std::function<void (float)> onScaleChange;   // window zoom ratio (geometry)
    std::function<void (float)> onFontChange;    // text size only

    // ---- v1.4.0 UI-state persistence (editor is recreated on window close) ----
    std::function<void (const juce::String&, int)> onUiStateChange;   // key,value -> persist
    void restoreUiState (bool advanced, int tab, bool rail, int theme = 0)
    {
        advancedMode = advanced;
        currentTab   = juce::jlimit (0, 1, tab);
        tuner.setRailMode (rail);
        applyModeVisibility();
        setThemeMode (theme);   // apply AFTER the LnF is attached (correct order)
    
        if (std::getenv ("GZ_JN") != nullptr)              // test hook: 5人ユニゾン強制ON
            if (auto* jp = processor.apvts.getParameter ("jn_on"))
                jp->setValueNotifyingHost (1.0f);
    }
    bool  isAdvanced() const { return advancedMode; }
    void setOverview (bool on);
    bool isOverview() const { return overviewEnabled && advancedMode; }
    void setOverviewLayoutMode (int mode, bool manual = true);
    int  getOverviewLayoutMode() const noexcept { return overviewLayoutMode; }
    void setLiteMode (bool on);
    bool isLiteMode() const noexcept { return liteMode; }
    void setFocusedEditing (bool on)
    {
        focusMode = on;
        pathRail.focusBtn.setToggleState (on, juce::dontSendNotification);
        pathRail.setSelected (on ? focusModule : -1);
        applyKnobVisibility();
    }

    // ---- v1.7.0 theme selector ----
    // 生きている番号: 0 ふつう(既定) / 1 ゆるふわ / 2 自然(四季) /
    //                 3 ブランド(leather,English) / 7 色覚(CUD) /
    //                 8 ゆるふわ・よる(v2.11.0: 1 の暗い版。もう一度押すと切替)
    // 廃止した番号(古い保存データが来たら setThemeMode が寄せる):
    //   4 ライブ / 6 ジャニーズ → 1 へ   (v2.4.0)
    //   5 軽量                  → 0 へ   (v2.12.0)
    void setThemeMode (int m);
    void setAccessMode (int m);                             // 0=標準デザイン, 1=◉くっきり大文字, 2=★かんたん3ステップ, 3=●マット・具体表記
    int  getAccessMode() const noexcept { return accessMode; }
    void refreshLanguage();                                 // v1.8.0: JP <-> EN re-label
    void forceSeason (int s);                               // test hook (GZ_SEASON)
    int  getThemeMode() const { return themeMode; }
    // v2.11.0: よる(8)も「ゆるふわの仲間」。丸文字・ジュースツマミ・ぷにぐじは共通で、
    //          変わるのは配色と背景だけ。ここを 1 だけにしておくと、よるで急に
    //          角ばった文字に戻って別テーマに見えてしまう。
    bool liverTheme()     const { return Palette::isCoutureTheme (themeMode); }
    bool oceanStarTheme() const { return Palette::isOceanStarTheme (themeMode); }
    bool pastelTheme()    const { return themeMode == 1 || themeMode == 8 || liverTheme(); }
    bool juiceTheme()     const { return ! liteMode && (accessMode == 0 || accessMode == 2) && (themeMode == 1 || themeMode == 8); }
    bool nightTheme()     const { return themeMode == 8 || themeMode == 11 || oceanStarTheme(); }   // ゆるふわ・よる / ノワール・ロリータ / 星空・海・黄昏
    void  setAdvanced (bool a) { advancedMode = a; applyModeVisibility(); if (onUiStateChange) onUiStateChange ("ui_advanced", a ? 1 : 0); }
    VocalTuner& getTuner() { return tuner; }

    // v2.12.0: テーマ9枚は「文字・見た目」の吹き出し（3×3グリッド）から選べる。
    //  吹き出し(CallOutBox の中身)から名前・選択状態・切り替えを呼ぶための3つ。
    //  名前を2か所に持つと必ずずれるので、正本はここ1か所にしてある。
    juce::String themeArmName (int i) const;
    bool         themeArmSelected (int i) const;
    void         chooseTheme (int i);

    // v2.12.0 直接描いている文字（ボタンでもラベルでもない見出し・ヒント）用。
    //  ここも「入らなければ小さくして全部出す」。ui_fit は Component しか
    //  見られないので、直描きは必ずこれを通す、という取り決めにする。
    void fitText (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r,
                  juce::Justification j, float h, bool bold) const
    {
        if (! g.clipRegionIntersects (r)) return;
        auto f = cfont (h, bold);
        if (r.getHeight() > 0 && f.getHeight() > (float) r.getHeight() * 0.86f)
            f = f.withHeight (juce::jmax (14.0f, (float) r.getHeight() * 0.86f));
        g.setFont (GzzioLnF::fitFont (f, t, r.getWidth() - 2));
        g.drawText (t, r, j, false);
    }

    bool handleOverviewMouseDown (juce::Point<int> pt);
    bool handleOverviewMouseDoubleClick (juce::Point<int> pt);
    void handleOverviewMouseDrag (juce::Point<int> pt);
    void applyArgonOrbFromXY (float nx, float ny);
    void applyArgonVoiceTarget (int idx);
    void syncArgonOrbFromParams() noexcept;
    juce::String overviewHelpAt (juce::Point<int> pt) const;
    void openPresetPalette (int initialTab, juce::Rectangle<int> localAnchorBounds, bool keepOpenDefault = false);
    juce::Rectangle<int> getFineTargetRect() const noexcept { return fineTargetRect; }
    juce::Rectangle<int> getFineMinusRect()  const noexcept { return fineMinusRect; }
    juce::Rectangle<int> getFinePlusRect()   const noexcept { return finePlusRect; }
    juce::Rectangle<int> getFineResetRect()  const noexcept { return fineResetRect; }
    EQGraph&             getEqGraph()        noexcept       { return eqGraph; }

    // 🕰️ 歴代バージョン呼び出し（世代タイムマシン）カタログ — constexpr テーブルのため起動時ヒープ確保 0 バイト
    struct LegacyVersionSpec
    {
        int         id;             // 0 = v5.0.0 最新, 1..12 = 歴代世代
        int         era;            // 100..510 (DSP省負荷ゲート＆世代別UIレイアウト判定)
        const char* verTag;         // "v5.1.0", "v5.0.0", "v1.0.0" 等
        const char* titleJa;
        const char* titleEn;
        const char* descJa;
        const char* descEn;
        const char* previewPng;     // docs/assets/archive-ui/ 内の実機スクリーンショット名（ポップアップ表示時のみ1枚だけオンデマンド読込）
        int         pageMode;       // 0=総合画面, 1=詳細画面
        bool        advMode;        // true=こだわり詳細, false=かんたん1列
        int         themeId;        // Palette::applyTheme の番号 (19=初代クリーム, 0=ダーク/Argon, 5=クチュール, 9=スタジオ, 14=星雲 等)
        int         ovLayout;       // overviewLayoutMode (0..3)
        int         knobStyle;      // GzzioLnF::s_knobStyle
    };
    static constexpr int kLegacyVersionCount = 14;
    static const LegacyVersionSpec& getLegacyVersionSpec (int id) noexcept;
    void applyLegacyVersion (int versionId, bool keepEcoGate = true);
    void openLegacyVersionPopup (juce::Rectangle<int> anchorScreenBounds);

private:
    friend class PresetPalettePopup;
    friend class LegacyVersionPopup;
    friend class VoiceChangerQuickPanel;
    void initialiseOverview();
    void updateOverviewVisibility();
    void restoreOverviewVisibility();
    void resizedOverview();
    void paintOverview (juce::Graphics&);
    void refreshOverviewText();
    void updateOverviewState();
    void trimOverviewOutputLevel();
    void openRoutePopup (juce::Rectangle<int> anchorScreenBounds);
    void openStreamOutPopup (juce::Rectangle<int> anchorScreenBounds);
    void openStandaloneAudioSettings();
    void openStageStudioPopup (juce::Rectangle<int> anchorScreenBounds);
    void openVoiceChangerPopup (juce::Rectangle<int> anchorScreenBounds);
    void openIrSelectorMenu (juce::Rectangle<int> anchorScreenBounds);
    void openPresetMatrixMenu (juce::Rectangle<int> anchorScreenBounds);
    void browseCustomSeSlot (int slotIndex);
    void openCustomSeSlotMenu (int slotIndex, juce::Rectangle<int> anchorScreenBounds);
    juce::Rectangle<int> detailVerBadgeRect; // 詳細・かんたん画面ヘッダの歴代バージョン切替バッジ領域
    int  lastAppliedLegacyVer { 0 };
    bool lastAppliedLegacyEco { true };
    bool overviewEnabled { true };
    int  overviewLayoutMode { 0 };     // 0=Argon Cockpit, 1=V-Liver Stage, 2=Studio Rack, 3=Classic
    bool overviewLayoutManual { false };
    bool liteMode { false };           // ⚡ 軽量モード (FFT・背景パーティクル・透過重なり休止 / 音声DSPは100%最高品質)
    int  accessMode { 0 };             // ★ 表示スタイル切替: 0=標準デザイン, 1=◉くっきり大文字, 2=★かんたん3ステップ, 3=●マット・具体表記
    std::array<juce::Rectangle<int>, 4> archPillRects;
    std::array<juce::Rectangle<int>, 4> accessPillRects;    // ★ 表示スタイル切替ピル (標準 / ◉大文字 / ★3ステップ / ●マット)
    std::array<juce::Rectangle<int>, 5> coutureSwatchRects;   // 💄 クチュール・コスメ / 🌌 星空・海 5色ワンタッチ切替ピル
    int  lastCoutureTheme { 5 };                              // 最後に選んだクチュール・カラーバリエーション
    int  lastOceanStarTheme { 14 };                           // 最後に選んだ星空・海・黄昏カラーバリエーション
    juce::Rectangle<int> litePillRect;
    juce::Rectangle<int> routePillRect;   // 🎯 中央寄せ・モノラル出力ピル
    juce::Rectangle<int> streamPillRect;  // 📡 外部出力 (SYNCROOM / VB-CABLE 等) ピル
    juce::Rectangle<int> ioInMonoRect;    // 🎤 計器＆入出力カード内の「入力: モノラル/ステレオ」直結ピル
    juce::Rectangle<int> ioOutMonoRect;   // 🔊 計器＆入出力カード内の「出力: モノラル/ステレオ」直結ピル
    juce::Rectangle<int> talkPillRect;    // 🎤⇄💬 歌枠 ⇄ 雑談MC ワンクリック瞬間ライブ切替ピル
    juce::Rectangle<int> loopPillRect;    // 🔄 5秒録って手放し聴き比べ「試聴ルーパー」ピル
    juce::Rectangle<int> coughPillRect;   // 😷 咳払い・くしゃみ瞬間ミュート（8ms無音フェード・残響鳴り残り防止）ピル
    juce::Rectangle<int> stagePillRect;   // 🎵 BGMプレイヤー・1クリックWAV録音・30秒さかのぼり保存・声質ターゲット診断ピル
    juce::Rectangle<int> chorusPillRect;  // 🔥 v5.0: サビ1クリック華やか演出 (Chorus Boost) ピル
    std::array<juce::Rectangle<int>, 4> liveSeRects;   // 👏 v5.0: 歌枠・VC盛り上げ 4種ライブSEポン出しパッド (拍手/キラキラ/ドラム/チャイム)
    std::array<juce::Rectangle<int>, 4> customSeRects; // 🎵 自作音源カスタムSE 4枠ポン出しパッド (自分で用意した .wav/.mp3/.flac/.ogg/.aiff)
    juce::Rectangle<int> fineTargetRect;  // 🎯 指が太い人・タッチ対応：現在選択中のツマミ名＆数値表示ピル（タップで次のツマミへ順送り）
    juce::Rectangle<int> fineMinusRect;   // ➖ 指が太い人・タッチ対応：選択中ツマミを1ステップ下げる大型ボタン
    juce::Rectangle<int> finePlusRect;    // ➕ 指が太い人・タッチ対応：選択中ツマミを1ステップ上げる大型ボタン
    juce::Rectangle<int> fineResetRect;   // ↺ 指が太い人・タッチ対応：選択中ツマミを1タップで初期値へ戻す大型ボタン
    juce::Rectangle<int> irOverviewRect;  // 🌊 IR残響 音響減衰カーブ表示＆外部IR(.wav/.aiff)ローダーカプセル
    juce::Rectangle<int> argonPadRect;
    juce::Rectangle<int> argonResetRect;  // ↺ タブレット・タッチ向け 2Dオーブ＆4軸バー 1タップ0%リセットピル
    std::array<juce::Rectangle<int>, 4> argonBarRects;
    std::array<juce::Rectangle<int>, 4> argonTargetRects;
    juce::Rectangle<int> levelBadgeRect;
    float argonOrbX { 0.46f }, argonOrbY { 0.36f };
    int   argonDragging { -1 };
    int   argonActiveTarget { -1 };
    juce::Point<int> argonDragStartPt;
    int   argonBarStepZone { 0 };
    std::vector<std::pair<juce::Component::SafePointer<juce::Component>, bool>> overviewOldVisibility;
    PresetPaletteComboBox finishPresetBox;
    juce::uint32 lastPaletteOpenMs { 0 };
    int lastPaletteTab { -1 };
    juce::TextButton overviewDetails, overviewReturn, overviewPitch, overviewOutputTrim;
    HoldButton overviewCompare;
    juce::TextButton reverbPower;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverbPowerAttachment;
    std::array<juce::TextButton, 3> overviewMore;
    std::array<juce::TextButton, 3> overviewSectionPower;
    std::array<juce::Rectangle<int>, 3> overviewCards;
    juce::Rectangle<int> overviewMonitor, overviewOutput, overviewHelp;
    juce::String overviewHint;
    juce::Label overviewNoiseStatus;
    int finishPresetIndex { -1 };
    struct Knob
    {
        ClickToEditSlider slider;
        TouchHintLabel    label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    Knob* activeDragKnob  { nullptr };
    Knob* lastTouchedKnob { nullptr };
    mutable juce::Rectangle<int> lastLoupeRect;
    int  lastOutMeterHash { -1 };
    bool argonGestureActive { false };
    void beginArgonGestures (int dragTarget);
    void endArgonGestures();
    void repaintArgonControls (bool includeHelp = false, bool includeVistas = false);
    void repaintKnobInteraction (Knob* k, bool tipChanged = false);
    void paintOverviewStaticBackdrop (juce::Graphics& g);

    struct OverviewBackdropKey
    {
        int w { 0 }, h { 0 };
        int themeMode { -1 }, layoutMode { -1 }, accessMode { -1 };
        int legacyVerId { -1 };
        bool liteMode { false }, english { false };
        bool s0 { false }, s1 { false }, s2 { false };
        bool operator== (const OverviewBackdropKey& o) const noexcept
        {
            return w == o.w && h == o.h && themeMode == o.themeMode
                && layoutMode == o.layoutMode && accessMode == o.accessMode
                && legacyVerId == o.legacyVerId && liteMode == o.liteMode
                && english == o.english && s0 == o.s0 && s1 == o.s1 && s2 == o.s2;
        }
    };
    OverviewBackdropKey overviewBackdropKey;
    juce::Image         overviewBackdropCache;

    void timerCallback() override;   // learn-button state refresh
    void addKnob (Knob& k, const juce::String& paramID, const juce::String& text,
                  const juce::String& tooltip, const juce::String& suffix);
    void placeRow (juce::Rectangle<int> area, std::initializer_list<Knob*> ks);
    // v3.1 §4「1つずつ」は並べる本数が実行時に決まるので、vector 版が要る。
    //  initializer_list 版はこれを呼ぶだけにして、置きかたを1か所に保つ。
    void placeRowVec (juce::Rectangle<int> area, const std::vector<Knob*>& ks);
    void styleButton (juce::TextButton& b);
    juce::Font cfont (float h, bool bold = false) const;
    void applyVoicePreset (int id);
    void applyMicPreset (int id);
    void applyEqPreset (int id);
    void refreshPresetDisplays();   // restore combo selections from apvts.state (display only)
    void applyTempoFit();           // set delay/reverb lengths from current BPM (v1.4.0)
    juce::String keyName (int tonic, bool isMinor);       // v1.4.0 key label
    juce::String suggestChords (int tonic, bool isMinor); // diatonic progression suggestion
    void  applyModeVisibility();    // show/hide advanced-only controls
    // v3.1「使いかた4種」: 見せる/隠すだけを切り出したもの（副作用なし）。
    //  使いかたを変えたときは、おまかせの途中経過などを壊さずにここだけ回す。
    void  applyKnobVisibility();
    // v3.1 使いかたで「そもそも通っていない」ツマミを画面から出さない。
    //  ★ここは**隠すだけ**。出すのは applyKnobVisibility()/updateTabVisibility() の仕事。
    void  applyUseModeMask();
    void applyScene();
    void updateABButtons();
    void savePreset();
    void loadPreset();

    // ---- v1.7.0 theme (cross-switch) ----
    void applyThemeToKnobs();                       // push per-theme arc colour to the LnF
    // v2.12.0: 6=かんたんモードのスイッチだけ。0-5(テーマチップ)は空矩形を返す
    //          （チップは「文字・見た目」の吹き出しへ引っ越した）
    juce::Rectangle<int> crossHit (int idx) const;
    void drawThemeCross (juce::Graphics& g);        // かんたんモードのスイッチ
    // v2.12.0: drawCrossSwitch (旧v1.7.0のアイコン版) は実装・呼び出しとも撤去。
    float rowScale { 1.0f };   // v2.4.0 かんたんモードでツマミ周りの文字/箱を拡大
    bool  heroBig  { false };  // v2.4.0 おまかせ設定の帯を大きく描くか
    // v2.4.0 かんたんモードの10秒おまかせ: 0=待機 1=しずかに(2秒: 0.5s間+1.5sノイズ学習)
    // 2=うた自動(8秒)実行中。タイマー(20Hz)が easyComboTick で進める。
    int   easyComboPhase { 0 }, easyComboTick { 0 };
    // 並び順 -> themeMode:
    //  0=ふつう / 1=ゆるふわ(8=よる) / 2=自然 / 3=ブランド(EN) / 4=オーロラ /
    //  5=ライバー(女性Vライバー向け) / 9=スタジオ(プロ音楽制作向け) / 6=ネオン / 7=色覚(CUD)
    static constexpr int kThemeCount = 9;
    static constexpr int kThemeOfChip[kThemeCount] = { 0, 1, 2, 3, 4, 5, 9, 6, 7 };
    static constexpr int kChipH    = 21;            // テーマチップの高さ
    static constexpr int kSwitchW  = 244;           // かんたんモードのスイッチ(大きめ)
    static constexpr int kSwitchH  = 31;
    void drawJuiceServer (juce::Graphics&);         // v1.7.0 corner juice dispenser (yuru-kawa)

    VocalGzzioProcessor& processor;
    juce::Image          mascot;
    int                  themeMode { 0 };
    bool                 themeInitialised { false };
    // v2.11.0: ゆるふわチップを押したとき、ひる(1)とよる(8)のどちらへ戻すか。
    // 一度よるにした人が別テーマを経由して戻ってきても、よるのままで戻れる。
    bool                 yuruNight { false };
    int                  crossHover { -1 };            // hovered cross cell (-1 none)
    juce::Image          kawaiiMascot;               // yuru-kawa backdrop art
    ThemePainter         themePainter;
    juce::Rectangle<int> crushReadArea;   // v3.0「つぶさない」の数字を出す場所
    juce::Rectangle<int> revTypeLabArea;  // v3.0-c「ひびきの種類」の見出し（空=譲った）
    juce::Rectangle<int> heroTitleArea;   // v3.0-c かんたんモードの「おまかせ設定」の席
    int                  revHeaderLeft { 0 };   // パネル4見出し行で、右側が使っている左端
    juce::Rectangle<int> crossArea;                  // header theme switch bounds
    int                  themeAnimFrame { 0 };       // v1.8.0 anim frame divider
    int                  lastTheme { 1 };             // v1.8.1 center-toggle restore target
    juce::TooltipWindow  tooltipWindow { this, 380 };
    VocalTuner           tuner;
    EQGraph              eqGraph;

    Knob gate, lowCut, mudK, harshK, denoiseK,
         comp1K, comp2K, attackK, releaseK, deessK,
         presenceK, airK, warmthK, sustainK, ringK,
         makeupK, mixK, widthK, doublerK, delayK, revSizeK, revMixK,
         seqAmountK, seqFocusK,                          // Smart EQ auto
         seqF1K, seqD1K, seqF2K, seqD2K, seqF3K, seqD3K; // Smart EQ manual (3 bands)

    // Preset system: voice type x mic model x scene, all combinable
    PresetPaletteComboBox voiceBox, micBox;
    PresetPaletteComboBox eqPresetBox;                     // famous vocal EQ recipes
    juce::String   infoText;                               // preset description shown under the graph
    juce::TextButton sceneSolo, sceneTalk, sceneBand { juce::String::fromUTF8 ("バンド") };
    juce::TextButton tuningPopBtn { juce::String::fromUTF8 ("チューナー") };
    int  currentScene { 1 };   // 0 Solo, 1 Talk, 2 Band
    bool sceneChosen  { false };  // v3.0-c 本人が選んだか（既定のまま＝false）
    // v3.1 「ぜんぶ表示」= 使いかたで使わないツマミも画面に出す（上級者向けの逃げ道）。
    //  apvts.state の "ui_show_all" に入れるので、プロジェクトに残る。
    bool showAllKnobs { false };

    // ---- v3.1 §4「1つずつ」（画面案A の中央＝選んだモジュールの詳細）----
    //  ツマミが60個ならぶ画面をやめて、いま直したい1枚だけを大きく出す。
    //  ★モードの正体は「どのツマミを出すか」と「どこへ置くか」だけ。
    //   ツマミそのものは今までと同じ子コンポーネントを使い回す
    //   （作り直すとアタッチメントが切れて、値が飛ぶ）。
    bool focusMode   { false };      // 全体を見渡す画面を既定にする。
    int  focusModule { 0 };          // いま見ているモジュール（gz::ModuleChain::Id）
    juce::String focusTipText;       // 説明欄に出す文（ツマミに乗ると入れ替わる）
    // v3.1 その使いかたで中身が丸ごと消える箱に出す一文。
    //  ★描くだけでなく**部品**にする。そうしないと検査から見えない
    //   （「出ているつもり」を検査が確かめられない物は作らない）。
    struct FocusEmptyNote : juce::Component
    {
        explicit FocusEmptyNote (std::shared_ptr<tip::Language> value) : language (std::move (value)) {}
        std::shared_ptr<tip::Language> language;
        float scale { 1.0f };
        void paint (juce::Graphics&) override;
    };
    FocusEmptyNote focusEmpty { language };
    juce::Rectangle<int> focusHeadArea, focusTipArea, focusFinishArea;
    // v3.1 §4 右1/4「ぜんたい」。入出力メーターと、文字/画面の大きさの大きなスライダー。
    //  ★「表示をかるくする」は入れない。案A には描いてあるが、その正体だった
    //   「軽量モード」は v2.12.0 で**撤廃した**もの。撤廃した物を設計図に
    //   残っているからという理由で戻すのは、決めたことを忘れるのと同じ。
    juce::Rectangle<int> focusSideArea, focusMeterArea;
    HoldButton focusCompare;         // その1枚だけ くらべる
    // 選んだモジュールに属するツマミを返す（出ていない物も含む。ふるいは別）
    std::vector<Knob*> focusKnobs (int module);
    // ツマミ以外の物（スイッチ・コンボ・横スライダー）。ここも置かないと
    // 「選べるのに操作できない箱」ができる（へんしん がまさにそうなった）。
    std::vector<juce::Component*> focusExtras (int module);
    void applyFocusSelection (int module);

    juce::TextButton learnButton { juce::String::fromUTF8 ("学習") };
    // v3.0 じどう学びなおし（しゃべっていない間にノイズ床を追従させる）
    juce::TextButton relearnBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> relearnAtt;
    // v3.0「つぶさない」（音量が上がっても潰れないモード）。音量そろえの見出しに置く。
    juce::TextButton crushBtn;
    // v3.0 「効果のオンオフ」ボタン → 8モジュールのスイッチを並べた吹き出し。
    //  v3.0-c: くわしい画面では**左の常設列**が本体になったので、この吹き出しは
    //  かんたんモード専用にした（同じ物への入口が2つあるのは、ただの散らかり）。
    juce::TextButton modPopBtn;
    // v3.0-c 左の常設列「音のとおり道」。縦に入り切らないときのために Viewport 越し。
    juce::Viewport   railView;
    PathRail         pathRail;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> crushAttach;
    juce::TextButton autoSetupButton, songSetupButton, tempoFitButton;

    // ---- v2.10.0 ★ノイズ床の学習を、汚れたまま固定させない ----
    //  これまで「覚えた床」は true になるだけで、false へ戻す道がどこにも無く、
    //  しかも状態XMLに保存されていた。声が入っている最中に LEARN(または
    //  10秒おまかせの自動学習)が走ると、低域 -10.5dB / 空気 +2.7dB / 山谷 +4.0dB の
    //  設定ができ上がり、再起動しても版を上げても直らなかった。
    //  → 覚えている間だけ「学習ずみ」と出し、1押しで自動追従へ戻せるようにする。
    juce::TextButton dnClearButton;
    int dnMsgTicks { 0 };                    // 学習結果メッセージの残り(20Hz)
    juce::String dnLearnMsgPending;          // 10秒おまかせ中の学習結果(最後にまとめて出す)

    // ---- v2.8.0 ★かんたんモードの「見えないのに効いている」対策 ----
    // ボイス変換やロボ声は「こだわりモード」のエフェクト欄にしかない。ところが
    // かんたんモードへ切り替えるとその欄が丸ごと消えるだけで、**音には効いたまま**
    // だった。つまり「声が変なままで、直す場所がどこにも無い」状態になり、
    // 全リセット以外に戻す手段が無かった。
    // → 効いているときだけボタンを出し、押せばその場で全部切れるようにする。
    juce::TextButton fxWarnButton;
    bool                 fxHiddenActive { false };   // かんたんモードで効いているか
    juce::Rectangle<int> fxWarnArea;                 // 文の描画位置
    bool  anyHiddenFxActive() const;                 // 判定
    void  clearHiddenFx();                           // まとめて切る
    void  updateHiddenFxWarning();                   // 表示の更新(タイマーから)
    juce::TextButton analyzeButton, keyScaleButton;   // v1.4.0 advanced analysis (Effects tab)
    juce::String     keyScaleMsg, chordMsg;
    int              analyzeMsgTtl { 0 };
    juce::String     autoSetupMsg, tempoFitMsg;
    int              tempoFitMsgTtl { 0 };   // frames left to show the tempo-fit confirmation
    juce::Rectangle<int> lvMeterArea;        // stream-loudness meter (header)
    juce::TextButton resetButton { juce::String::fromUTF8 ("初期化") };
    juce::TextButton abA { "A" }, abB { "B" }, abCopy { "A>B" }, saveButton { juce::String::fromUTF8 ("保存") }, loadButton { juce::String::fromUTF8 ("読込") };

    // ---- v1.4.0: right-column tabs (EQ | Effects) ----
    juce::TextButton tabEqButton { "EQ" }, tabFxButton;
    // v2.4.0: modeButton(かんたん/こだわり)は廃止。ヘッダの大きなスイッチへ統合。
    bool  advancedMode { true };
    int currentTab { 0 };
    void updateTabVisibility();

    // ---- v1.4.0: effect controls (effects tab) ----
    Knob duckK, choAmtK, bpmK, dlyMsK, dlyFbK, dlyHcK, megaAmtK, roboFreqK, roboMixK;
    Knob brK, emoK, liftK;                                 // v2.0.0 エモート(息/エモ/サビリフト)
    Knob popK, lipK;                                       // v2.3.0 ポップ/リップ除去
    Knob resK;                                             // v2.4.0 なめらか(動的レゾナンス抑制)
    Knob pickK;                                            // v3.1 ピックおさえ(アコギだけ)
    Knob inGainK;                                          // v2.4.0 マイク音量(入力トリム)
    Knob rideK;                                            // v2.4.0 音量キープ(自動ゲインライド)
    Knob humK;                                             // v2.6.0 ジー音(電源ハムの自動除去)
    Knob consK;                                            // v2.6.0 ことば(子音エンハンサー)
    int  humShownHz = -1;                                  // v2.6.0 ラベルに出している検出値
    Knob proxK;                                            // v2.10.0 きょり(距離ならし)
    Knob vcPitchK, vcFormK, jnMixK;                        // v1.8.0 voice changer / unison
    juce::TextButton vcOnButton, jnOnButton;
    juce::ComboBox   jnHarmBox;                            // ハモリ (ユニゾン/3度/5度/3&5)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> vcOnAttach, jnOnAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> jnHarmAttach;
    // ---- v1.9.0 auto-tune (pitch correction) UI ----
    juce::TextButton atOnButton;
    juce::TextButton jnSoloButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> jnSoloAttach;
    // v2.9.0: 「低遅延」ボタンを廃止し、ヒーロー帯の「セッション」スイッチへ置き換えた。
    juce::TextButton sessionButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sessionAttach;
    juce::Rectangle<int> latBadgeArea;      // 「追加遅延 +0.0 ms」を描く場所
    int  lastShownLatency = -1;             // 変化したときだけ塗り直す
    bool lastSessionState = false;
    void applySessionLock();                // セッション中は該当ツマミを灰色に
    juce::ComboBox   atKeyBox, atScaleBox;
    juce::Slider     atAmountSlider, atSpeedSlider;
    juce::Slider ornSlider;                                // v2.7.0 こぶし
    juce::Rectangle<int> atLabelOrn, ornStatusArea;         // v2.7.0 ラベルと状態表示
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   atOnAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> atKeyAttach, atScaleAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   atAmountAttach, atSpeedAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ornAttach;   // v2.7.0
    juce::ComboBox revTypeBox, dlySyncBox, megaTypeBox;
    PresetPaletteComboBox charBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        revTypeAttach, dlySyncAttach, megaTypeAttach;
    juce::TextButton tapButton { "TAP" };
    juce::TextButton midiButton;                           // v2.1.0 MIDIスイッチ設定
    juce::TextButton routeButton;                          // v4.7.0 入出力モノラル・マイク中央寄せ設定
    juce::TextButton stageButton;                          // v4.8.0 BGM・WAV録音・30秒さかのぼり・声質ターゲット診断
    juce::TextButton streamButton;                         // v2.2.0 配信出力(単体起動版のみ)
    juce::TextButton checkupButton;                        // v2.10.0 点検
    double lastUsageMs = 0.0;                              // v2.10.0 使われ方の集計時刻
    double lastTapMs { 0.0 };
    float  tapBpm { 0.0f };
    int    tapCount { 0 };
    juce::String fxInfoText;
    juce::Rectangle<int> fxRow1, fxRow2, fxRow3;
    juce::Rectangle<int> analysisArea;   // v1.4.0 P5: framed key/chord detection box
    // v1.9.0: auto-tune band sub-rects (set in resized(), painted in paint())
    juce::Rectangle<int> atColArea, keyColArea, atLabelAmt, atLabelSpd;
    void fillComboFromChoiceParam (juce::ComboBox&, const juce::String& paramID);

    // ---- v1.4.0: red module lamps at knob centres ----
    struct Lamp
    {
        LampButton btn;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attach;
    };
    Lamp lampGate, lampDn, lampDs, lampDbl, lampDly, lampRev, lampMega, lampCho, lampRobo;
    void addLamp (Lamp&, const juce::String& paramID);
    void placeLamp (Lamp&, const Knob&);

    ClickToEditSlider fontSlider;                          // text size only
    juce::Label  fontSliderLabel;
    ClickToEditSlider zoomSlider;                          // window zoom ratio
    juce::Label  zoomSliderLabel;
    // v2.12.0: ヘッダの13pxスライダーは「文字を大きくする操作なのに小さくて
    // 押せない」ため、ボタン→大きなスライダー(高さ34px・つまみも大きい)の
    // 吹き出しに変えた。元の2本は互換のため残すが非表示(値の保存/復元用)。
    juce::TextButton  sizePopBtn;

public:
    // called by the editor when the window is resized by its corner handle
    void syncZoomDisplay (float s) { zoomSlider.setValue (s, juce::dontSendNotification); }

    // set both slider displays without firing callbacks (startup restore)
    void setUiPrefDisplays (float fontVal, float zoomVal)
    {
        fontSlider.setValue (fontVal, juce::dontSendNotification);
        zoomSlider.setValue (zoomVal, juce::dontSendNotification);
    }

private:

    // Smart Dynamic EQ controls
    juce::TextButton seqOnButton;                          // on/off (panel header)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> seqOnAttach;
    juce::ComboBox seqModeBox;                             // 自動 / 手動
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> seqModeAttach;

    // ---- v2.10.0 #77/#78 音源モード ----
    //  「プリセット」ではなく、同じツマミが見るところを持ち替えるスイッチ。
    //  ヘッダ2段目の空き地(x=860〜1220)に置く。1段目はシーンとテーマ帯で埋まっていて、
    //  入れるとシーンボタンがテーマ帯(x=980から)の下敷きになる。
    juce::ComboBox srcModeBox;
    juce::Rectangle<int> srcModeLabelArea;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> srcModeAttach;
    juce::Rectangle<int> seqArea;
    void updateSeqModeVisibility();

    // v2.1.0: A/Bスロットはプロセッサ所有へ移管(MIDIスイッチでエディタ無しでも
    // 切替できるようにするため)。エディタはボタン表示と表示更新だけを持つ。
    int lastAbDirty   { 0 };          // processor.abUiDirty の前回値
    int lastMidiDirty { 0 };          // processor.midiUiDirty の前回値(将来用)
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> cleanArea, dynArea, toneArea, spaceArea;
    juce::Rectangle<int> heroArea;           // v1.5.0 one-press auto-setup band
    juce::Rectangle<int> voiceStageArea;     // かんたん画面の案内とブランド表示
    juce::Rectangle<int> eqGraphArea;   // EQ graph region inside the seq panel

    // update notice (GitHub Releases check, display only)
    juce::HyperlinkButton updateNotice { juce::String(),
        juce::URL ("https://github.com/gzzio1989/VocalGzzio/releases/latest") };
    void startUpdateCheck();

public:
    void showUpdateNotice (const juce::String& versionTag);

private:
    float fontScale { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalGzzioContent)
};

//==============================================================================
class VocalGzzioEditor : public juce::AudioProcessorEditor
{
public:
    explicit VocalGzzioEditor (VocalGzzioProcessor&);
    ~VocalGzzioEditor() override;

    void resized() override;

    // ★ここが**唯一**の既定サイズ。中身は必ずこの寸法で組んでから、窓の大きさに
    //  合わせて拡大縮小される（content.setBounds(0,0,baseW,baseH) + Transform）。
    //  v3.0-c: 左に「音のとおり道」の列(約212px)＋間隔10pxが増えたぶん横を広げ、
    //  カード8枚がスクロールなしで収まるぶん縦を足した。**既存の3列は1pxも
    //  狭めていない**（狭めればツマミが小さくなり視認性が下がるため）。
    static constexpr int baseW = 1582;  // v1.8.3: +80 → v3.0-c: +222(とおり道の列)
    static constexpr int baseH = 892;   // v1.8.3: +24 → v3.0-c: +34(カード8枚)

private:
    static constexpr float kFontRebase = 1.5f;   // new 100 percent = old 150 percent

    void applyScale (float s);

    VocalGzzioProcessor& processor;
    GzzioLnF             lnf;
    VocalGzzioContent    content;
    float                uiScale { 1.00f };   // 1280x790 fits 1080p at 100%
    juce::ApplicationProperties appProps;     // remembers font/zoom across sessions

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalGzzioEditor)
};
