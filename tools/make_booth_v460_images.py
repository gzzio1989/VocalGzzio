#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VocalGzzio v4.9.0 (Starry Sky & Universal Style Edition) 用の BOOTH商品画像（全8枚＋特設サムネ1枚）を生成するスクリプト。
「ぷにぐじ」マスコットとクリーム背景の親しみやすい世界観を継承しつつ、
v4.9.0 の「星空・海・黄昏 5大風景コレクション（01銀河・02碧海・03朝焼・04晩秋・05蒼劇）」「4つの表示・操作スタイル（標準／◉くっきり大文字／★かんたん3ステップ／●マット・具体表記）」、
およびカラオケBGM再生＆キー変更±6、直前30秒さかのぼりWet+Dry録音、5秒AI声質診断、cluster音消え一発修復を一目で伝える。
"""
import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
ART_DIR = Path(r"C:\Users\silve\.gemini\antigravity\brain\b8363e04-cff5-4472-8e7e-30436a3c6d81")
OUT_DIR = ROOT / "BOOTH商品画像"
MASCOT_PATH = ROOT / "Resources" / "puniguji_kawaii.png"

FONT_CANDIDATES_BOLD = [
    r"C:\Windows\Fonts\YuGothB.ttc",
    r"C:\Windows\Fonts\meiryob.ttc",
    r"C:\Windows\Fonts\BIZ-UDGothicB.ttc",
]
FONT_CANDIDATES_REG = [
    r"C:\Windows\Fonts\YuGothM.ttc",
    r"C:\Windows\Fonts\meiryo.ttc",
    r"C:\Windows\Fonts\BIZ-UDGothicR.ttc",
]

def get_font(size: int, bold: bool = True) -> ImageFont.FreeTypeFont:
    cands = FONT_CANDIDATES_BOLD if bold else FONT_CANDIDATES_REG
    for p in cands:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()

CREAM = (251, 246, 236)
CREAM_CARD = (255, 253, 249)
CARD_BORDER = (224, 213, 196)
INK = (66, 53, 46)
SUB = (115, 98, 86)
ORANGE_ACCENT = (196, 108, 72)
TEAL_ACCENT = (48, 134, 118)
TEAL_BG = (224, 245, 238)
TEAL_BORDER = (142, 208, 190)
PINK_ACCENT = (198, 72, 116)
PINK_BG = (253, 232, 239)
PINK_BORDER = (234, 168, 188)
PEACH_BG = (251, 235, 230)
PEACH_BORDER = (226, 146, 126)

def resolve_art(preferred: str, fallback: str) -> Path:
    p = ART_DIR / preferred
    if p.exists() and p.stat().st_size > 10000:
        return p
    return ART_DIR / fallback

def make_cream_canvas(w: int, h: int, mascot_pos=None, mascot_size=(340, 380)) -> Image.Image:
    im = Image.new("RGBA", (w, h), CREAM + (255,))
    overlay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    od.ellipse([-120, -120, int(w * 0.55), int(h * 0.55)], fill=(255, 228, 235, 70))
    od.ellipse([int(w * 0.55), -100, w + 120, int(h * 0.5)], fill=(220, 245, 236, 70))
    overlay = overlay.filter(ImageFilter.GaussianBlur(60))
    im = Image.alpha_composite(im, overlay)

    if mascot_pos and MASCOT_PATH.exists():
        m = Image.open(MASCOT_PATH).convert("RGBA")
        bbox = m.getchannel("A").getbbox()
        if bbox:
            m = m.crop(bbox)
        m.thumbnail(mascot_size, Image.LANCZOS)
        im.paste(m, mascot_pos, m)
    return im

def draw_pill(d: ImageDraw.ImageDraw, x: int, y: int, w: int, h: int, text: str,
              bg_color, border_color, text_color, font_size=24):
    d.rounded_rectangle([x, y, x + w, y + h], radius=h // 2, fill=bg_color, outline=border_color, width=2)
    f = get_font(font_size, bold=True)
    bb = d.textbbox((0, 0), text, font=f)
    tw, th = bb[2] - bb[0], bb[3] - bb[1]
    d.text((x + (w - tw) / 2 - bb[0], y + (h - th) / 2 - bb[1]), text, font=f, fill=text_color)

def draw_footer(d: ImageDraw.ImageDraw, w: int, h: int, right_text: str):
    y_line = h - 82
    d.line([64, y_line, w - 64, y_line], fill=(216, 204, 188), width=2)
    f = get_font(22, bold=True)
    d.text((64, h - 58), "G'zzio  |  VocalGzzio v5.2.0", font=f, fill=(148, 132, 118))
    bb = d.textbbox((0, 0), right_text, font=f)
    tw = bb[2] - bb[0]
    d.text((w - 64 - tw, h - 58), right_text, font=f, fill=SUB)

def paste_rounded_screenshot(canvas: Image.Image, img_path: Path, box: tuple, radius: int = 18, border_col=None):
    x, y, w, h = box
    if not img_path.exists():
        return
    src = Image.open(img_path).convert("RGBA")
    src = src.resize((w, h), Image.LANCZOS)
    mask = Image.new("L", (w, h), 0)
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle([0, 0, w - 1, h - 1], radius=radius, fill=255)
    shadow = Image.new("RGBA", (w + 32, h + 32), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    sd.rounded_rectangle([16, 20, 16 + w, 20 + h], radius=radius, fill=(30, 22, 28, 75))
    shadow = shadow.filter(ImageFilter.GaussianBlur(10))
    canvas.alpha_composite(shadow, (x - 16, y - 16))
    canvas.paste(src, (x, y), mask)
    if border_col:
        d = ImageDraw.Draw(canvas)
        d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=radius, outline=border_col, width=3)

# 1. BOOTH_1_バナー.png (1500 x 360)
def make_banner():
    w, h = 1500, 360
    im = make_cream_canvas(w, h, mascot_pos=(55, 22), mascot_size=(290, 320))
    d = ImageDraw.Draw(im)
    d.text((405, 48), "Windows & Mac  ／  VST3・AU・単体起動アプリ・専用WDM仮想ドライバ同梱", font=get_font(23, True), fill=ORANGE_ACCENT)
    d.text((400, 92), "ボーカルグッジオ", font=get_font(66, True), fill=INK)
    d.text((945, 114), "VocalGzzio v5.2.0", font=get_font(44, True), fill=(126, 108, 94))
    d.text((405, 188), "伴奏「声ポケット」自動あけ・部屋鳴り除去・0.08ms音程・原曲カラオケ化・完パケ録音搭載！", font=get_font(22, True), fill=INK)

    pills = [
        ("★声ポケット＆部屋鳴り除去", PINK_BG, PINK_BORDER, PINK_ACCENT, 315),
        ("♪原曲カラオケ化＆完パケ録音", TEAL_BG, TEAL_BORDER, TEAL_ACCENT, 295),
        ("2,500円・買い切り", CREAM_CARD, CARD_BORDER, INK, 210),
        ("無料の体験版あり", CREAM_CARD, CARD_BORDER, INK, 210),
    ]
    px = 405
    for label, bg, bd, fg, pw in pills:
        draw_pill(d, px, 252, pw, 58, label, bg, bd, fg, font_size=21)
        px += pw + 14

    im.convert("RGB").save(OUT_DIR / "BOOTH_1_バナー.png", quality=95)

# 2. BOOTH_2_商品画像.png (1200 x 1200) — 4つの表示・操作スタイル＆星空5景
def make_main_product():
    w, h = 1200, 1200
    im = make_cream_canvas(w, h, mascot_pos=(54, 42), mascot_size=(260, 260))
    d = ImageDraw.Draw(im)
    d.text((880, 52), "VocalGzzio v5.2.0", font=get_font(26, True), fill=ORANGE_ACCENT)
    d.text((340, 88), "伴奏の声ポケットも、部屋鳴りも、", font=get_font(48, True), fill=INK)
    d.text((340, 158), "見やすさも1クリックで完結。", font=get_font(48, True), fill=INK)
    d.text((340, 238), "◉くっきり大文字 ／ ★かんたん3ステップ ／ ●マット・具体表記に即切替！", font=get_font(22, True), fill=TEAL_ACCENT)
    d.text((340, 276), "星空・海・黄昏5大風景＆クチュール5色（全19テーマ）・文字150%でも見切れゼロ", font=get_font(21, True), fill=PINK_ACCENT)

    gal_img = resolve_art("v5_overview_galaxy_live.png", "theme_14_galactic_milkyway.png")
    paste_rounded_screenshot(im, gal_img, (56, 330, 710, 400), radius=16, border_col=(110, 195, 255))

    styles = [
        ("◉ くっきり大文字（高コントラスト）", resolve_art("v5_style1_high_contrast.png", "style_1_high_contrast.png"), (255, 215, 60)),
        ("★ かんたん3ステップ（迷わない）", resolve_art("v5_style2_3step_guide.png", "style_2_3step_guide.png"), (80, 210, 175)),
        ("● マット・具体表記（点滅ゼロ）", resolve_art("v5_style3_matte_concrete.png", "style_3_matte_concrete.png"), (160, 185, 210)),
    ]
    sy = 330
    for stitle, spath, scol in styles:
        paste_rounded_screenshot(im, spath, (786, sy, 358, 124), radius=12, border_col=scol)
        d.rounded_rectangle([792, sy + 92, 786 + 352, sy + 120], radius=7, fill=(18, 16, 22, 225))
        d.text((804, sy + 95), stitle, font=get_font(16, True), fill=scol)
        sy += 138

    d.text((56, 752), "▼ 「星空・海・黄昏」5大風景コレクション（＋クチュール5色・全19テーマ収録）", font=get_font(23, True), fill=INK)
    swatches = [
        ("01 銀河 MilkyWay", resolve_art("v5_overview_galaxy_live.png", "theme_14_galactic_milkyway.png"), (110, 195, 255)),
        ("02 碧海 AquaSea", resolve_art("theme_15_lush_aqua_sea.png", "v4.8.0_couture_04_mint.png"), (64, 220, 205)),
        ("03 朝焼 Dawn", resolve_art("theme_16_radiant_dawn.png", "v4.8.0_couture_05_peach.png"), (255, 160, 110)),
        ("04 晩秋 Twilight", resolve_art("theme_17_autumn_winter.png", "v4.8.0_couture_02_lilac.png"), (235, 145, 125)),
        ("05 蒼劇 Sapphire", resolve_art("theme_18_opera_sapphire.png", "v4.8.0_couture_03_noir.png"), (125, 170, 255)),
    ]
    cw = 204
    gap = 17
    sx = 56
    sy = 792
    for title, p, col in swatches:
        paste_rounded_screenshot(im, p, (sx, sy, cw, 115), radius=10, border_col=col)
        d.rounded_rectangle([sx + 6, sy + 83, sx + cw - 6, sy + 111], radius=8, fill=(18, 16, 24, 225))
        f = get_font(16, True)
        bb = d.textbbox((0, 0), title, font=f)
        tw = bb[2] - bb[0]
        d.text((sx + (cw - tw) / 2, sy + 88), title, font=f, fill=(255, 248, 252))
        sx += cw + gap

    d.rounded_rectangle([56, 932, w - 56, 1085], radius=18, fill=TEAL_BG, outline=TEAL_BORDER, width=2)
    d.text((80, 952), "★ なぜ 0円の無料体験版から「製品版（2,500円・買い切り）」へアップグレードするの？", font=get_font(23, True), fill=TEAL_ACCENT)
    d.text((80, 994), "・無料体験版は60秒ごとに0.6秒だけ音量が下がり、アプリを閉じると設定が初期状態に戻ります。", font=get_font(20, True), fill=INK)
    d.text((80, 1032), "・製品版なら、あなたの声に合わせた設定・ノイズ学習・テーマを保存し、毎回0秒で本番配信＆完パケ録音！", font=get_font(20, True), fill=PINK_ACCENT)

    draw_footer(d, w, h, "Windows & Mac 両対応 ／ 無料体験版でまず耳でお確かめください。")
    im.convert("RGB").save(OUT_DIR / "BOOTH_2_商品画像.png", quality=95)

# 3. BOOTH_3_遅延0.png (1200 x 1200) — v5.0.0 4大ステージ＆スタジオ機能＋配信対策
def make_card_stream():
    w, h = 1200, 1200
    im = make_cream_canvas(w, h, mascot_pos=(54, 48), mascot_size=(250, 250))
    d = ImageDraw.Draw(im)
    d.text((545, 52), "1 / 歌枠・歌ってみた・配信がこれ1台で完結（v5.2.0）", font=get_font(22, True), fill=ORANGE_ACCENT)
    d.text((330, 96), "伴奏を下げずに声を出す「声ポケット」と", font=get_font(42, True), fill=INK)
    d.text((330, 166), "遅れゼロの「部屋鳴り消し」を新搭載。", font=get_font(42, True), fill=INK)

    cards = [
        ("♪ 伴奏「声ポケット」自動あけ ＆ 伴奏追従ライダー ＆ 1クリック原曲カラオケ化",
         "歌っている瞬間だけ伴奏の真ん中から声と重なる帯域だけを最大-5dBあけ、伴奏の迫力を保って声を前へ！\nAメロは馴染ませサビは最大+3.5dB自動リフト。原曲ボーカル除去・キー変更±6・1秒キー解析も内蔵。",
         PINK_BG, PINK_BORDER, PINK_ACCENT),
        ("★ ゼロ遅延・部屋鳴り（反響）除去 ＆ 息継ぎ自動整え・サ行セパレート ＆ 0.08ms音程補正",
         "普通の部屋の壁の跳ね返り（お風呂感・460Hz箱鳴り）を追加遅延0.0msで引き算し、防音室のような声に！\nコンプ前の大きな息継ぎを自然に抑え、母音とサ行子音を分離して高域の刺さりを保護。4大マイク質感も搭載。",
         TEAL_BG, TEAL_BORDER, TEAL_ACCENT),
        ("◎ -14 LUFS 完パケ3ファイル同時保存（30秒さかのぼり） ＆ サビ盛・4種SE・音消え自動保護",
         "1回歌うと【完パケMIX(-14LUFS)】【完成声(Wet)】【素の声(Dry)】の3本を同時保存（直前30秒保存も対応）。\nさらにサビ自動演出、拍手など4種SE、WDMドライバ導入時の既定スピーカー保護＆cluster共存も完備。",
         CREAM_CARD, CARD_BORDER, ORANGE_ACCENT),
    ]
    cy = 290
    for title, desc, bg_c, bd_c, fg_c in cards:
        d.rounded_rectangle([64, cy, w - 64, cy + 205], radius=22, fill=CREAM_CARD, outline=bd_c, width=3)
        d.rounded_rectangle([88, cy + 20, 1110, cy + 70], radius=14, fill=bg_c, outline=bd_c, width=2)
        d.text((104, cy + 31), title, font=get_font(21, True), fill=fg_c)
        lines = desc.split("\n")
        for idx, line in enumerate(lines):
            d.text((92, cy + 94 + idx * 44), line, font=get_font(20, True), fill=INK)
        cy += 230

    draw_pill(d, 64, 995, 340, 62, "♪原曲ボーカル除去＆キー±6", PINK_BG, PINK_BORDER, PINK_ACCENT, 21)
    draw_pill(d, 424, 995, 350, 62, "★-14LUFS完パケ3種保存", TEAL_BG, TEAL_BORDER, TEAL_ACCENT, 21)
    draw_pill(d, 794, 995, 342, 62, "◎サビ演出＆◆4種ライブSE", CREAM_CARD, CARD_BORDER, INK, 21)

    draw_footer(d, w, h, "専用WDM仮想ドライバ同梱でOBS・Discord・clusterへ直結！")
    im.convert("RGB").save(OUT_DIR / "BOOTH_3_遅延0.png", quality=95)

# 4. BOOTH_4_へや.png (1200 x 1200)
def make_card_heya():
    w, h = 1200, 1200
    im = make_cream_canvas(w, h, mascot_pos=(54, 48), mascot_size=(250, 250))
    d = ImageDraw.Draw(im)
    d.text((760, 52), "2 / 空間IR「へや」＆ 声優先ダッキング", font=get_font(24, True), fill=ORANGE_ACCENT)
    d.text((330, 96), "6つの「へや」と声優先ダッキング。", font=get_font(50, True), fill=INK)
    d.text((330, 166), "手持ちのIR（.wav）も読込OK。", font=get_font(50, True), fill=INK)

    d.rounded_rectangle([64, 275, w - 64, 715], radius=22, fill=CREAM_CARD, outline=CARD_BORDER, width=2)
    rooms = [
        ("へや：おふろ", "2.1 × 1.7 × 2.3 m", "1.29 秒"),
        ("へや：カラオケ箱", "3.4 × 2.6 × 2.4 m", "0.45 秒"),
        ("へや：録音スタジオ", "2.0 × 1.6 × 2.2 m", "0.24 秒"),
        ("へや：ライブハウス", "13 × 9 × 4.2 m", "0.95 秒"),
        ("へや：コンサートホール", "30 × 21 × 13 m", "2.07 秒"),
        ("へや：プレート", "スタジオ鉄板残響", "1.87 秒"),
    ]
    ry = 298
    for name, dim, rt in rooms:
        d.text((100, ry), name, font=get_font(28, True), fill=INK)
        d.text((540, ry + 2), dim, font=get_font(26, False), fill=SUB)
        d.text((950, ry), rt, font=get_font(29, True), fill=TEAL_ACCENT)
        if ry < 620:
            d.line([96, ry + 54, w - 96, ry + 54], fill=(232, 224, 212), width=2)
        ry += 68

    d.rounded_rectangle([64, 745, w - 64, 965], radius=22, fill=PINK_BG, outline=PINK_BORDER, width=2)
    d.text((96, 772), "★ 声優先ダッキング直結＆可視化 ＋ カスタムIR読込 ＋ 歌枠⇄MC切替", font=get_font(28, True), fill=PINK_ACCENT)
    d.text((96, 826), "・歌っている最中は残響もカラオケBGMも自動で控えめに抑え、歌詞がくっきり前に出ます。", font=get_font(23, True), fill=INK)
    d.text((96, 870), "・IR減衰波形カード上から直接「声優先ダッキング」をON/OFFでき、減衰量（-dB）もリアルタイム表示！", font=get_font(23, True), fill=INK)
    d.text((96, 914), "・「IR読込」から手持ちの .wav / .aiff IR読込もOK。曲間トークは「♪歌枠⇄雑談MC」で即座にドライへ。", font=get_font(23, True), fill=INK)

    draw_pill(d, 64, 995, 360, 62, "◆声優先ダッキング可視化", TEAL_BG, TEAL_BORDER, TEAL_ACCENT, 23)
    draw_pill(d, 444, 995, 350, 62, ".wav / .aiff カスタムIR対応", PINK_BG, PINK_BORDER, PINK_ACCENT, 23)
    draw_pill(d, 814, 995, 322, 62, "トーク時は1クリックでMCへ", CREAM_CARD, CARD_BORDER, INK, 23)

    draw_footer(d, w, h, "内蔵IRは計算生成データです。実在の部屋の録音物ではありません。")
    im.convert("RGB").save(OUT_DIR / "BOOTH_4_へや.png", quality=95)

# 5. BOOTH_5_動作環境と導入.png (1200 x 1200)
def make_card_env():
    w, h = 1200, 1200
    im = make_cream_canvas(w, h, mascot_pos=(54, 48), mascot_size=(250, 250))
    d = ImageDraw.Draw(im)
    d.text((860, 52), "3 / 動作環境と導入", font=get_font(24, True), fill=ORANGE_ACCENT)
    d.text((330, 96), "買う前に、", font=get_font(54, True), fill=INK)
    d.text((330, 168), "ここだけ見てください。", font=get_font(54, True), fill=INK)

    d.rounded_rectangle([64, 270, w - 64, 690], radius=22, fill=CREAM_CARD, outline=CARD_BORDER, width=2)
    rows = [
        ("Windows", "Windows 10 / 11（64bit） — VST3 ／ 単体起動アプリ ／ 専用WDMドライバ同梱", INK),
        ("Mac", "macOS 11以降（Apple Silicon / Intel） — AU ／ VST3 ／ 単体起動アプリ", TEAL_ACCENT),
        ("価格", "2,500円（買い切り・税込 ／ Windows版とMac版の両方を同梱）", ORANGE_ACCENT),
        ("台数・更新", "購入者ご本人の機器 3台まで ／ メジャー・マイナーアップデート無償", INK),
        ("音程処理", "0.08ms（4サンプル）ゼロ遅延ピッチ補正対応（ハモリ併用時は約42.6ms）", SUB),
    ]
    ry = 298
    for k, v, col in rows:
        d.text((98, ry), k, font=get_font(27, True), fill=INK)
        d.text((310, ry), v, font=get_font(24, True), fill=col)
        if ry < 590:
            d.line([96, ry + 62, w - 96, ry + 62], fill=(232, 224, 212), width=2)
        ry += 80

    d.rounded_rectangle([64, 720, w - 64, 885], radius=18, fill=PEACH_BG, outline=PEACH_BORDER, width=2)
    d.text((96, 742), "初回起動時のセキュリティ確認（未署名・未公証）について", font=get_font(27, True), fill=(180, 72, 52))
    d.text((96, 788), "個人開発のためインストーラー本体の署名・Apple公証は未付与です（同梱WDMドライバはWHQL署名済）。", font=get_font(21, True), fill=INK)
    d.text((96, 826), "Windowsでは「詳細情報」→「実行」、Macでは同梱の案内手順に沿って導入してください。", font=get_font(22, True), fill=INK)

    d.rounded_rectangle([64, 910, w - 64, 1075], radius=18, fill=TEAL_BG, outline=TEAL_BORDER, width=2)
    d.text((96, 932), "購入前に「無料の体験版」でご自身の声・機材との相性をお確かめください", font=get_font(27, True), fill=TEAL_ACCENT)
    d.text((96, 978), "体験版（VocalGzzio Trial）は製品版と別名で導入されるため、同じPCに両方入れても混ざりません。", font=get_font(22, True), fill=INK)
    d.text((96, 1016), "既に購入済みの方は、追加料金なしで購入履歴から v5.2.0（Win/Mac両方）をダウンロードできます。", font=get_font(22, True), fill=INK)

    draw_footer(d, w, h, "体験版は同じBOOTH商品ページから0円でダウンロードできます。")
    im.convert("RGB").save(OUT_DIR / "BOOTH_5_動作環境と導入.png", quality=95)

# 6. BOOTH_6_ライセンスと体験版.png (1200 x 1200)
def make_card_license():
    w, h = 1200, 1200
    im = make_cream_canvas(w, h, mascot_pos=(54, 48), mascot_size=(250, 250))
    d = ImageDraw.Draw(im)
    d.text((790, 52), "4 / 使ってよい範囲と体験版", font=get_font(24, True), fill=ORANGE_ACCENT)
    d.text((330, 96), "製品版で作った音は、", font=get_font(52, True), fill=INK)
    d.text((330, 168), "商用も配信も自由です。", font=get_font(52, True), fill=INK)

    d.rounded_rectangle([64, 270, w - 64, 455], radius=22, fill=CREAM_CARD, outline=CARD_BORDER, width=2)
    d.text((100, 304), "できます", font=get_font(28, True), fill=TEAL_ACCENT)
    d.text((320, 304), "製品版で作った音の商用利用・収益化配信・動画投稿・CD/音源販売（報告・表記不要）", font=get_font(23, True), fill=INK)
    d.line([96, 362, w - 96, 362], fill=(232, 224, 212), width=2)
    d.text((100, 386), "できません", font=get_font(28, True), fill=(184, 72, 52))
    d.text((320, 386), "ソフト本体の再配布・共有・転売・改変・リバースエンジニアリング", font=get_font(23, True), fill=INK)

    d.text((64, 492), "無料の体験版（VocalGzzio Trial） ── 製品版との違い", font=get_font(31, True), fill=INK)
    d.rounded_rectangle([64, 548, w - 64, 825], radius=22, fill=CREAM_CARD, outline=CARD_BORDER, width=2)
    limits = [
        ("1", "60秒ごとに 0.6秒 だけ音量がなめらかに下がります（-18dBまで）"),
        ("2", "設定の保存・読込・次回起動時の復元、および A/B 比較は使えません"),
        ("3", "体験版で作った音を公開・配信・販売物に使うことはできません（相性確認用です）"),
    ]
    ly = 576
    for num, txt in limits:
        d.text((104, ly), num, font=get_font(30, True), fill=TEAL_ACCENT)
        d.text((168, ly + 2), txt, font=get_font(25, True), fill=INK)
        if ly < 720:
            d.line([96, ly + 62, w - 96, ly + 62], fill=(232, 224, 212), width=2)
        ly += 84

    d.rounded_rectangle([64, 855, w - 64, 1075], radius=18, fill=PINK_BG, outline=PINK_BORDER, width=2)
    d.text((96, 882), "音声エンジン・全19テーマ・原曲カラオケ化・完パケ録音・ライブSEは製品版と同じです", font=get_font(24, True), fill=PINK_ACCENT)
    d.text((96, 932), "・体験版でも「♪原曲ボーカル除去」「★完パケ3種保存」「◎サビ演出」「◆ライブSE」をすべて試せます。", font=get_font(20, True), fill=INK)
    d.text((96, 974), "・気に入った設定を保存して毎日の歌枠や録音でそのまま使いたくなったら、製品版をご検討ください。", font=get_font(22, True), fill=INK)
    d.text((96, 1016), "・商品画像のマスコットはAI生成、プラグイン本体のC++音声プログラムはすべて手書き実装です。", font=get_font(22, True), fill=SUB)

    draw_footer(d, w, h, "くわしくは同梱の使用許諾契約書および取扱説明書をご覧ください。")
    im.convert("RGB").save(OUT_DIR / "BOOTH_6_ライセンスと体験版.png", quality=95)

# 7 & 8. ジャケット画像（製品版 / 体験版）
def make_jacket(is_trial: bool):
    w, h = 1200, 1200
    im = make_cream_canvas(w, h, mascot_pos=(64, 54), mascot_size=(340, 360))
    d = ImageDraw.Draw(im)

    badge_col = (212, 118, 92) if is_trial else (52, 128, 110)
    badge_txt = "無料体験版" if is_trial else "製品版"
    d.rounded_rectangle([830, 56, 1136, 162], radius=53, fill=badge_col)
    f_b = get_font(42, True)
    bb = d.textbbox((0, 0), badge_txt, font=f_b)
    d.text((830 + (306 - (bb[2] - bb[0])) / 2, 56 + (106 - (bb[3] - bb[1])) / 2 - 4), badge_txt, font=f_b, fill=(255, 255, 255))

    d.text((880, 192), "ボーカルグッジオ", font=get_font(28, True), fill=ORANGE_ACCENT)
    d.text((640, 232), "VocalGzzio", font=get_font(82, True), fill=INK)
    d.text((810, 326), "v5.2.0", font=get_font(64, True), fill=INK)

    if is_trial:
        d.text((890, 412), "0円", font=get_font(96, True), fill=ORANGE_ACCENT)
        d.text((635, 526), "60秒ごとに0.6秒だけ音量が下がります", font=get_font(25, True), fill=INK)
        zip_label = "VocalGzzio_v5.2.0_Windows_体験版.zip  （＋Mac体験版）"
    else:
        d.text((685, 412), "2,500円", font=get_font(96, True), fill=TEAL_ACCENT)
        d.text((535, 526), "買い切り・税込 ／ 同時に3台まで（Win & Mac）", font=get_font(25, True), fill=INK)
        zip_label = "VocalGzzio_v5.2.0_Windows_製品版.zip  （＋Mac製品版）"

    d.rounded_rectangle([64, 570, 820, 642], radius=16, fill=CREAM_CARD, outline=CARD_BORDER, width=2)
    d.text((92, 591), zip_label, font=get_font(24, True), fill=INK)

    d.rounded_rectangle([64, 668, w - 64, 960], radius=22, fill=CREAM_CARD, outline=CARD_BORDER, width=2)
    d.text((100, 696), "ダウンロードに含まれるもの（v5.2.0 Spectral Unmasker & Room De-Reverb Suite）", font=get_font(22, True), fill=INK)
    items = [
        "・インストーラー（VST3・単体起動アプリ・専用仮想ドライバ VocalGzzio Driver (WDM) 同梱）",
        "・♪伴奏「声ポケット」自動あけ ＆ 伴奏追従ライダー ＆ 原曲カラオケ化（キー変更±6・自動解析）",
        "・★ゼロ遅延・部屋鳴り除去 ＆ 息継ぎ/サ行セパレート ＆ 0.08ms音程補正 ＆ -14LUFS完パケ3本録音",
        "・◎星空・海・黄昏5景＋4種の見やすさ切替 ＆ 4大名機マイクモデリング ＆ cluster音消え自動保護",
    ]
    iy = 748
    for it in items:
        d.text((100, iy), it, font=get_font(21, True), fill=INK)
        iy += 48

    draw_pill(d, 64, 992, 340, 62, "♪原曲カラオケ化＆完パケ録音", PINK_BG, PINK_BORDER, PINK_ACCENT, 21)
    draw_pill(d, 424, 992, 360, 62, "◎サビ演出＆◆4種ライブSE", TEAL_BG, TEAL_BORDER, TEAL_ACCENT, 21)
    draw_pill(d, 804, 992, 332, 62, "Windows & Mac 両対応", CREAM_CARD, CARD_BORDER, INK, 23)

    footer_r = "まずご自身の声とマイクで、音と動作をお確かめください。" if is_trial else "製品版で作った音は商用も配信も自由。報告もクレジット表記も不要です。"
    draw_footer(d, w, h, footer_r)
    out_name = "BOOTH_8_ジャケット_体験版.png" if is_trial else "BOOTH_7_ジャケット_製品版.png"
    im.convert("RGB").save(OUT_DIR / out_name, quality=95)

def make_mobile_hero_thumb():
    """
    BOOTH検索一覧（スマホの150x150px表示）で0.3秒で読めてクリックされる
    「引き算・特大文字・ぷにぐじ＋1画面ドアップ」のメインサムネイル（1200 x 1200）。
    ※ YuGothB.ttc で豆腐(☒)にならないよう BMP記号（★・♪・◎・◆）のみを使用する。
    """
    w, h = 1200, 1200
    im = make_cream_canvas(w, h, mascot_pos=(36, 124), mascot_size=(336, 366))
    d = ImageDraw.Draw(im)

    # 最上部：誰向けの何のソフトか一目でわかる特大ピル
    d.rounded_rectangle([42, 32, 835, 108], radius=38, fill=(44, 34, 30), outline=(218, 175, 110), width=3)
    d.text((68, 47), "★ DAW不要！歌枠・歌ってみた・VC通話アプリ", font=get_font(33, True), fill=(255, 246, 220))

    d.rounded_rectangle([855, 32, 1158, 108], radius=38, fill=(214, 72, 112), outline=(255, 220, 230), width=3)
    d.text((888, 47), "無料体験版 0円", font=get_font(34, True), fill=(255, 255, 255))

    # メインコピー：スマホ縮小でも100%読める超特大ゴシック
    d.rounded_rectangle([382, 186, 1148, 224], radius=10, fill=(255, 230, 135))
    d.rounded_rectangle([382, 298, 1164, 336], radius=10, fill=(255, 212, 224))

    d.text((386, 128), "部屋のマイク声が、", font=get_font(82, True), fill=INK)
    d.text((386, 238), "1クリックで主役級。", font=get_font(80, True), fill=(188, 44, 88))

    # サブコピー：3つの最強フック
    d.rounded_rectangle([386, 358, 1158, 476], radius=20, fill=CREAM_CARD, outline=CARD_BORDER, width=3)
    d.text((412, 372), "♪ 手持ちの原曲を1クリックでカラオケ伴奏化（キー変更±6）", font=get_font(25, True), fill=TEAL_ACCENT)
    d.text((412, 420), "★ 歌＋伴奏の「完パケ音源」同時録音 ＆ サビ盛・拍手SE内蔵", font=get_font(25, True), fill=PINK_ACCENT)

    # 中段3連ピル（スクリーンショットのヘッダーやフッターを隠さずに魅力を明示）
    draw_pill(d, 44, 492, 358, 52, "◎ 3軸パレット＆5秒AI声質診断", TEAL_BG, TEAL_BORDER, TEAL_ACCENT, 21)
    draw_pill(d, 418, 492, 364, 52, "◆ サビ1クリック演出＆4種SE", PINK_BG, PINK_BORDER, PINK_ACCENT, 21)
    draw_pill(d, 798, 492, 358, 52, "★ 歴代13世代再現＆くっきり文字", CREAM_CARD, CARD_BORDER, ORANGE_ACCENT, 21)

    # 下半分：1枚の美しい総合画面を大きく配置（文字やボタンを一切隠さない）
    gal_img = resolve_art("v5_overview_galaxy_live.png", "theme_14_galactic_milkyway.png")
    paste_rounded_screenshot(im, gal_img, (44, 558, 1112, 522), radius=20, border_col=(90, 180, 245))

    # 最下部フッター帯：対応機材・OSの安心感
    d.rounded_rectangle([42, 1098, 1158, 1170], radius=20, fill=(44, 122, 108), outline=(142, 218, 200), width=2)
    d.text((76, 1118), "Win & Mac 両対応 ／ OBS・AG03・Scarlett・USBマイク・cluster 直結OK！", font=get_font(28, True), fill=(255, 255, 255))

    out1 = OUT_DIR / "BOOTH_0_サムネ_完パケ歌枠スイート_v5.2.0.png"
    out2 = ROOT / "BOOTH_0_サムネ_完パケ歌枠スイート_v5.2.0.png"
    im.convert("RGB").save(out1, quality=95)
    im.convert("RGB").save(out2, quality=95)
    im.convert("RGB").save(ART_DIR / "BOOTH_0_サムネ_完パケ歌枠スイート_v5.2.0.png", quality=95)

def make_simple_guzzio_thumb():
    """
    10月初旬に8本売れた時の「MochiyPopOne × クリーム地 × ぷにぐじ × 細かいスクショ無し」の
    スマホ一覧（150x150px）で圧倒的に読みやすく親しみやすい引き算サムネイル（1200 x 1200）。
    """
    w, h = 1200, 1200
    im = make_cream_canvas(w, h, mascot_pos=(56, 175), mascot_size=(380, 420))
    d = ImageDraw.Draw(im)

    mochi_path = ROOT / "docs" / "assets" / "MochiyPopOne.ttf"
    def mfont(sz):
        if mochi_path.exists():
            return ImageFont.truetype(str(mochi_path), sz)
        return get_font(sz, True)

    # 上部ピルバッジ
    d.rounded_rectangle([64, 54, 560, 130], radius=38, fill=(194, 75, 114))
    d.text((104, 68), "ぜんぶ日本語・遅延 0", font=mfont(36), fill=(255, 255, 255))

    d.rounded_rectangle([584, 54, 1136, 130], radius=38, fill=TEAL_BG, outline=TEAL_BORDER, width=3)
    d.text((622, 68), "VST3 ＆ 単体アプリ同梱", font=mfont(34), fill=TEAL_ACCENT)

    # メイン見出し（MochiyPopOne 特大）
    d.text((468, 188), "ボーカルグッジオ", font=mfont(76), fill=INK)
    d.text((472, 294), "あなたの声を、主役に。", font=mfont(56), fill=(194, 75, 114))
    d.text((476, 384), "歌ってみた・歌枠配信・弾き語り・VC通話の声を", font=get_font(29, True), fill=SUB)
    d.text((476, 430), "これ1つで整えるソフトです。（v5.2.0 / Win & Mac）", font=get_font(29, True), fill=SUB)

    # 中段：3つの大きな白カード（スマホでも読める特大文字）
    cards = [
        ("1", "伴奏を下げずに、声を前へ出す", "歌う瞬間だけ伴奏の真ん中に「声のすきま」をあけ、サビで声が埋もれません", TEAL_ACCENT, TEAL_BG),
        ("2", "普通の部屋の、はね返りを消す", "壁の反響（お風呂感）や息継ぎ・サ行の刺さりを、追加の遅れ 0ms で整えます", (194, 75, 114), PINK_BG),
        ("3", "原曲カラオケ化 ＆ 3本同時録音", "手持ちの曲からボーカルを引き算。キー変更（±6）と完パケ録音も1画面で完結", ORANGE_ACCENT, PEACH_BG),
    ]
    cy = 590
    for num, title, sub, col, bg in cards:
        d.rounded_rectangle([64, cy, w - 64, cy + 142], radius=24, fill=CREAM_CARD, outline=CARD_BORDER, width=3)
        d.ellipse([92, cy + 26, 92 + 88, cy + 26 + 88], fill=bg, outline=col, width=3)
        d.text((120, cy + 36), num, font=mfont(44), fill=col)
        d.text((208, cy + 24), title, font=mfont(36), fill=INK)
        d.text((208, cy + 86), sub, font=get_font(24, True), fill=SUB)
        cy += 162

    # 下部：価格と無料体験版の案内バー
    d.rounded_rectangle([64, 1086, w - 64, 1162], radius=38, fill=(52, 128, 110))
    d.text((116, 1102), "まずは 0円の「無料体験版」でお試しください（製品版 2,500円・買い切り）", font=get_font(28, True), fill=(255, 255, 255))

    out1 = OUT_DIR / "BOOTH_0_サムネ_グッジオ節_シンプル_v5.2.0.png"
    im.convert("RGB").save(out1, quality=95)
    im.convert("RGB").save(ART_DIR / "BOOTH_0_サムネ_グッジオ節_シンプル_v5.2.0.png", quality=95)

def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    make_simple_guzzio_thumb()
    make_mobile_hero_thumb()
    make_banner()
    make_main_product()
    make_card_stream()
    make_card_heya()
    make_card_env()
    make_card_license()
    make_jacket(is_trial=False)
    make_jacket(is_trial=True)
    print("Generated all BOOTH v5.2.0 images in:", OUT_DIR)

if __name__ == "__main__":
    main()
