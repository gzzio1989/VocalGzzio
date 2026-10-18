#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VocalGzzio v4.8.0 (Stage & Studio Complete Edition) 用の BOOTH商品画像（全8枚＋特設サムネ1枚）を生成するスクリプト。
「ぷにぐじ」マスコットとクリーム背景の親しみやすい世界観を継承しつつ、
v4.8.0 の4大新機能（カラオケBGM再生＆キー変更±6＆声優先ダッキング、1クリックWAV録音＆直前30秒さかのぼりWet+Dry保存、
5秒AI声質ターゲット診断、絶叫セーフティガード＆咳払い8ms瞬間ミュート）およびターゲット別の利点を一目で伝える。
"""
import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
ART_DIR = Path(r"C:\Users\silve\.gemini\antigravity\brain\8f2542b1-d681-42f3-92c9-1e836ae605e5")
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
    d.text((64, h - 58), "G'zzio  |  VocalGzzio v4.8.0", font=f, fill=(148, 132, 118))
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
    d.text((945, 114), "VocalGzzio v4.8.0", font=get_font(44, True), fill=(126, 108, 94))
    d.text((405, 188), "伴奏再生＆キー変更・直前30秒さかのぼり録音・5秒AI声質診断・絶叫＆咳ガード搭載！", font=get_font(26, True), fill=INK)

    pills = [
        ("♪伴奏再生＆キー変更±6", PINK_BG, PINK_BORDER, PINK_ACCENT, 290),
        ("★30秒さかのぼりWAV録音", TEAL_BG, TEAL_BORDER, TEAL_ACCENT, 295),
        ("2,500円・買い切り", CREAM_CARD, CARD_BORDER, INK, 225),
        ("無料の体験版あり", CREAM_CARD, CARD_BORDER, INK, 225),
    ]
    px = 405
    for label, bg, bd, fg, pw in pills:
        draw_pill(d, px, 252, pw, 58, label, bg, bd, fg, font_size=21)
        px += pw + 14

    im.convert("RGB").save(OUT_DIR / "BOOTH_1_バナー.png", quality=95)

# 2. BOOTH_2_商品画像.png (1200 x 1200)
def make_main_product():
    w, h = 1200, 1200
    im = make_cream_canvas(w, h, mascot_pos=(54, 42), mascot_size=(260, 260))
    d = ImageDraw.Draw(im)
    d.text((880, 52), "VocalGzzio v4.8.0", font=get_font(26, True), fill=ORANGE_ACCENT)
    d.text((340, 88), "歌枠も、歌ってみた録音も、", font=get_font(52, True), fill=INK)
    d.text((340, 158), "DAWなしでこれ1つで完結。", font=get_font(52, True), fill=INK)
    d.text((340, 238), "♪カラオケ伴奏＆キー変更・★30秒さかのぼりWet+Dry録音・◎5秒AI声質診断", font=get_font(24, True), fill=SUB)
    d.text((340, 276), "🛡絶叫ガード・😷咳瞬間ミュート・歌枠⇄雑談MC切替・クチュール5色搭載。", font=get_font(24, True), fill=PINK_ACCENT)

    rose_img = resolve_art("v4.8.0_couture_stage.png", "v4.7.0_couture_stage.png")
    paste_rounded_screenshot(im, rose_img, (56, 336, 1088, 613), radius=18, border_col=(215, 165, 180))

    swatches = [
        ("01 ロゼ・クチュール", resolve_art("v4.8.0_couture_stage.png", "v4.7.0_couture_stage.png"), (232, 92, 138)),
        ("02 アイシーラベンダー", resolve_art("v4.8.0_couture_02_lilac.png", "v4.6.0_couture_02_lilac.png"), (168, 138, 240)),
        ("03 ノワール・ロリータ", resolve_art("v4.8.0_couture_03_noir.png", "v4.6.0_couture_03_noir.png"), (218, 78, 125)),
        ("04 ティファニーミント", resolve_art("v4.8.0_couture_04_mint.png", "v4.6.0_couture_04_mint.png"), (72, 198, 178)),
        ("05 ハニーピーチ", resolve_art("v4.8.0_couture_05_peach.png", "v4.6.0_couture_05_peach.png"), (238, 138, 102)),
    ]
    cw = 204
    gap = 17
    sx = 56
    sy = 968
    for title, p, col in swatches:
        paste_rounded_screenshot(im, p, (sx, sy, cw, 115), radius=10, border_col=col)
        d.rounded_rectangle([sx + 6, sy + 83, sx + cw - 6, sy + 111], radius=8, fill=(24, 18, 24, 215))
        f = get_font(16, True)
        bb = d.textbbox((0, 0), title, font=f)
        tw = bb[2] - bb[0]
        d.text((sx + (cw - tw) / 2, sy + 88), title, font=f, fill=(255, 248, 252))
        sx += cw + gap

    draw_footer(d, w, h, "Windows & Mac 両対応 ／ 無料体験版でまず耳でお確かめください。")
    im.convert("RGB").save(OUT_DIR / "BOOTH_2_商品画像.png", quality=95)

# 3. BOOTH_3_遅延0.png (1200 x 1200) — v4.8.0 4大ステージ＆スタジオ機能＋配信対策
def make_card_stream():
    w, h = 1200, 1200
    im = make_cream_canvas(w, h, mascot_pos=(54, 48), mascot_size=(250, 250))
    d = ImageDraw.Draw(im)
    d.text((545, 52), "1 / 歌枠・歌ってみた・配信がこれ1台で完結（v4.8.0）", font=get_font(22, True), fill=ORANGE_ACCENT)
    d.text((330, 96), "伴奏キー変更も、神テイク救出も、", font=get_font(48, True), fill=INK)
    d.text((330, 166), "AI声質診断も、ぜんぶ内蔵。", font=get_font(48, True), fill=INK)

    cards = [
        ("♪ 内蔵カラオケ・BGMプレイヤー（キー変更±6半音 ＆ 声優先オートダッキング）",
         "伴奏（WAV/MP3/FLAC）を読み込んでキー変更（±6半音）すると、歌声のピッチ補正・ハモリも自動同期！\nさらに歌い出すと伴奏が自動で少し下がり（オートダッキング）、歌声が伴奏に埋もれません。",
         PINK_BG, PINK_BORDER, PINK_ACCENT),
        ("★ 1クリックWAV録音 ＆ 直前30秒さかのぼり保存（Wet完成音＋Dry素の声 同時保存）",
         "録音ボタンを押していなくても「今のテイク最高だった！」と思ったら【直前30秒を保存】で後から救出！\nエフェクト込みの【完成音(Wet)】とMIX師にそのまま渡せる【無加工の素の声(Dry)】を24bit WAVで同時保存。",
         TEAL_BG, TEAL_BORDER, TEAL_ACCENT),
        ("◎ 5秒AI声質ターゲット診断 ＆ 歌枠⇄MC即切替 ＆ 絶叫ガード・咳払い8ms瞬間ミュート",
         "5秒歌うだけでAIが理想の歌声（透明感・アイドル・ロック等）へ自動調律！曲間トークは1クリックでMCへ。\n突発的な叫び声を0.2msで抑える【絶叫ガード】と、プツッという音ゼロで遮断する【咳ミュート】も完備。",
         CREAM_CARD, CARD_BORDER, ORANGE_ACCENT),
    ]
    cy = 290
    for title, desc, bg_c, bd_c, fg_c in cards:
        d.rounded_rectangle([64, cy, w - 64, cy + 205], radius=22, fill=CREAM_CARD, outline=bd_c, width=3)
        d.rounded_rectangle([88, cy + 20, 1110, cy + 70], radius=14, fill=bg_c, outline=bd_c, width=2)
        d.text((104, cy + 31), title, font=get_font(22, True), fill=fg_c)
        lines = desc.split("\n")
        for idx, line in enumerate(lines):
            d.text((92, cy + 94 + idx * 44), line, font=get_font(20, True), fill=INK)
        cy += 230

    draw_pill(d, 64, 995, 340, 62, "♪伴奏再生＆キー変更±6", PINK_BG, PINK_BORDER, PINK_ACCENT, 22)
    draw_pill(d, 424, 995, 350, 62, "★30秒さかのぼりWet+Dry", TEAL_BG, TEAL_BORDER, TEAL_ACCENT, 22)
    draw_pill(d, 794, 995, 342, 62, "◎5秒AI診断＆絶叫・咳ガード", CREAM_CARD, CARD_BORDER, INK, 21)

    draw_footer(d, w, h, "専用WDM仮想ドライバ同梱でOBS・Discordへ直結！")
    im.convert("RGB").save(OUT_DIR / "BOOTH_3_遅延0.png", quality=95)
    im.convert("RGB").save(ART_DIR / "BOOTH_3_配信対策_v4.8.0.png", quality=95)

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
        ("台数・更新", "購入者ご本人の機器 3台まで ／ v4系のアップデートは無償", INK),
        ("音程処理", "単独の歌声向け（48kHzで約42.6msの遅延 ／ 低遅延モードでは音程系停止）", SUB),
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
    d.text((96, 1016), "既に購入済みの方は、追加料金なしで購入履歴から v4.8.0（Win/Mac両方）をダウンロードできます。", font=get_font(22, True), fill=INK)

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
    d.text((96, 882), "音声エンジン・カラオケBGM再生・30秒さかのぼり録音・AI診断は製品版と同じです", font=get_font(26, True), fill=PINK_ACCENT)
    d.text((96, 932), "・体験版でも「伴奏再生＆キー変更」「直前30秒保存」「5秒AI声質診断」「絶叫・咳ガード」を試せます。", font=get_font(21, True), fill=INK)
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
    d.text((810, 326), "v4.8.0", font=get_font(64, True), fill=INK)

    if is_trial:
        d.text((890, 412), "0円", font=get_font(96, True), fill=ORANGE_ACCENT)
        d.text((650, 526), "60秒ごとに0.6秒だけ音量が下がります", font=get_font(26, True), fill=INK)
        zip_label = "VocalGzzio_v4.8.0_Windows_体験版.zip  （＋Mac体験版）"
    else:
        d.text((685, 412), "2,500円", font=get_font(96, True), fill=TEAL_ACCENT)
        d.text((665, 526), "買い切り・税込 ／ 同時に3台まで（Win & Mac）", font=get_font(25, True), fill=INK)
        zip_label = "VocalGzzio_v4.8.0_Windows_製品版.zip  （＋Mac製品版）"

    d.rounded_rectangle([64, 570, 820, 642], radius=16, fill=CREAM_CARD, outline=CARD_BORDER, width=2)
    d.text((92, 591), zip_label, font=get_font(24, True), fill=INK)

    d.rounded_rectangle([64, 668, w - 64, 960], radius=22, fill=CREAM_CARD, outline=CARD_BORDER, width=2)
    d.text((100, 696), "ダウンロードに含まれるもの（v4.8.0 Stage & Studio Complete Edition）", font=get_font(25, True), fill=INK)
    items = [
        "・インストーラー（VST3・単体起動アプリ・専用仮想ドライバ VocalGzzio Driver (WDM) 同梱）",
        "・♪カラオケBGMプレイヤー（キー変更±6・声優先ダッキング）＆ ★直前30秒さかのぼりWet+Dry録音",
        "・◎5秒AI声質ターゲット診断 ＆ 🛡絶叫セーフティガード・😷咳瞬間ミュート ＆ 歌枠⇄MC切替",
        "・クチュール5色（01ロゼ〜05白桃）＋ オフラインで読める「説明書.html」＋ ライセンス表記",
    ]
    iy = 748
    for it in items:
        d.text((100, iy), it, font=get_font(22, True), fill=INK)
        iy += 48

    draw_pill(d, 64, 992, 340, 62, "♪伴奏再生＆キー変更±6", PINK_BG, PINK_BORDER, PINK_ACCENT, 22)
    draw_pill(d, 424, 992, 360, 62, "★30秒さかのぼりWet+Dry", TEAL_BG, TEAL_BORDER, TEAL_ACCENT, 22)
    draw_pill(d, 804, 992, 332, 62, "Windows & Mac 両対応", CREAM_CARD, CARD_BORDER, INK, 23)

    footer_r = "まずご自身の声とマイクで、音と動作をお確かめください。" if is_trial else "製品版で作った音は商用も配信も自由。報告もクレジット表記も不要です。"
    draw_footer(d, w, h, footer_r)
    out_name = "BOOTH_8_ジャケット_体験版.png" if is_trial else "BOOTH_7_ジャケット_製品版.png"
    im.convert("RGB").save(OUT_DIR / out_name, quality=95)

# 9. BOOTH_0_サムネ_クチュール5色_v4.8.0.png (1200 x 1200)
def make_special_thumbnail():
    w, h = 1200, 1200
    im = Image.new("RGBA", (w, h), (22, 15, 24, 255))
    overlay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    od = ImageDraw.Draw(overlay)
    od.ellipse([-150, -100, 700, 650], fill=(218, 72, 128, 65))
    od.ellipse([550, -100, 1350, 650], fill=(120, 90, 220, 55))
    od.ellipse([200, 600, 1100, 1300], fill=(64, 190, 175, 45))
    overlay = overlay.filter(ImageFilter.GaussianBlur(75))
    im = Image.alpha_composite(im, overlay)
    d = ImageDraw.Draw(im)

    draw_pill(d, 56, 44, 490, 54, "★ VocalGzzio v4.8.0 大型アップデート", (52, 26, 42), (235, 165, 188), (255, 220, 232), 23)
    draw_pill(d, 566, 44, 330, 54, "Windows & Mac 両対応", (28, 44, 52), (110, 210, 195), (175, 248, 235), 23)
    draw_pill(d, 916, 44, 228, 54, "無料体験版あり", (54, 42, 28), (238, 195, 128), (255, 236, 185), 23)

    d.text((56, 116), "歌枠も宅録も、これ1つで完結。", font=get_font(64, True), fill=(255, 248, 252))
    d.text((56, 202), "♪伴奏再生＆キー変更・★30秒さかのぼり録音・◎5秒AI声質診断・絶叫＆咳ガード", font=get_font(28, True), fill=(238, 190, 208))

    argon_img = resolve_art("v4.8.0_argon_cockpit.png", "v4.7.0_argon_cockpit.png")
    rose_img = resolve_art("v4.8.0_couture_stage.png", "v4.7.0_couture_stage.png")
    paste_rounded_screenshot(im, argon_img, (460, 268, 684, 385), radius=16, border_col=(110, 215, 245))
    paste_rounded_screenshot(im, rose_img, (56, 300, 740, 416), radius=18, border_col=(242, 182, 198))

    d.text((56, 738), "▼ クチュール・コスメティック 5色 ＆ 5つのペルソナ画面に1クリック切替", font=get_font(25, True), fill=(255, 225, 236))
    swatches = [
        ("01 ロゼ", resolve_art("v4.8.0_couture_stage.png", "v4.7.0_couture_stage.png"), (236, 112, 152)),
        ("02 氷紫", resolve_art("v4.8.0_couture_02_lilac.png", "v4.6.0_couture_02_lilac.png"), (175, 145, 248)),
        ("03 黒姫", resolve_art("v4.8.0_couture_03_noir.png", "v4.6.0_couture_03_noir.png"), (225, 82, 132)),
        ("04 翡翠", resolve_art("v4.8.0_couture_04_mint.png", "v4.6.0_couture_04_mint.png"), (82, 212, 190)),
        ("05 白桃", resolve_art("v4.8.0_couture_05_peach.png", "v4.6.0_couture_05_peach.png"), (245, 152, 112)),
    ]
    cw = 204
    gap = 17
    sx = 56
    sy = 782
    for title, p, col in swatches:
        paste_rounded_screenshot(im, p, (sx, sy, cw, 115), radius=12, border_col=col)
        d.rounded_rectangle([sx + 8, sy + 81, sx + cw - 8, sy + 110], radius=8, fill=(18, 12, 20, 225))
        f = get_font(19, True)
        bb = d.textbbox((0, 0), title, font=f)
        d.text((sx + (cw - (bb[2] - bb[0])) / 2, sy + 85), title, font=f, fill=col)
        sx += cw + gap

    d.rounded_rectangle([56, 922, w - 56, 1102], radius=20, fill=(34, 24, 38, 235), outline=(218, 165, 185), width=2)
    feats = [
        ("♪ 伴奏再生＆キー変更±6", "歌うと自動でBGMが下がるダッキング"),
        ("★ 30秒さかのぼりWet+Dry録音", "完成音とMIX用素の声を同時WAV保存"),
        ("◎ 5秒AI声質診断＆絶叫・咳ガード", "OBS直結の専用仮想ドライバも同梱"),
    ]
    fx = 74
    for head, sub in feats:
        d.text((fx, 948), head, font=get_font(21, True), fill=(255, 232, 240))
        d.text((fx, 986), sub, font=get_font(18, True), fill=(205, 188, 200))
        fx += 358

    d.line([82, 1032, w - 82, 1032], fill=(82, 62, 86), width=2)
    d.text((82, 1048), "製品版 2,500円（買い切り・Win/Mac同梱） ／ まずは0円の無料体験版でお試し！", font=get_font(25, True), fill=(255, 242, 195))

    d.text((56, 1132), "G'zzio  |  VocalGzzio v4.8.0 Stage & Studio Complete Edition", font=get_font(22, True), fill=(168, 148, 165))
    im.convert("RGB").save(OUT_DIR / "BOOTH_0_サムネ_クチュール5色_v4.8.0.png", quality=95)
    im.convert("RGB").save(OUT_DIR / "BOOTH_0_サムネ_クチュール5色_v4.7.0.png", quality=95)
    im.convert("RGB").save(OUT_DIR / "BOOTH_0_サムネ_クチュール5色_v4.6.0.png", quality=95)
    im.convert("RGB").save(ART_DIR / "BOOTH_0_サムネ_クチュール5色_v4.8.0.png", quality=95)

def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    make_banner()
    make_main_product()
    make_card_stream()
    make_card_heya()
    make_card_env()
    make_card_license()
    make_jacket(is_trial=False)
    make_jacket(is_trial=True)
    make_special_thumbnail()
    print("Generated all 9 BOOTH v4.8.0 images in:", OUT_DIR)

if __name__ == "__main__":
    main()
