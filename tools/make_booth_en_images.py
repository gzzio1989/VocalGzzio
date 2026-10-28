#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Generate English Storefront & Visual UI Guide Images for VocalGzzio v5.4.0:
1. docs/assets/overview-en-map.png (+ BOOTH_EN_1_Visual_UI_Map.png):
   Annotated visual map of the Japanese UI with numbered callouts (1-10) and an English legend below.
2. BOOTH商品画像_英語版/BOOTH_EN_0_Thumbnail_Simple.png (1200x1200):
   English counterpart of the high-converting "G'zzio Simple Cream" thumbnail.
3. BOOTH商品画像_英語版/BOOTH_EN_2_Features_v540.png (1200x1200):
   3-Card Feature Overview covering v5.4.0 Vocal Expression, v5.3.0 Sweet Spot & Cue Monitor, and v5.2.0 Spectral Unmasker.
4. BOOTH商品画像_英語版/BOOTH_EN_3_Specs_and_Trial.png (1200x1200):
   System Requirements, Commercial Streaming License, and $0 Free Trial vs Full Version guide.
"""
import os
import shutil
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "BOOTH商品画像_英語版"
DOCS_ASSETS = ROOT / "docs" / "assets"
PORTAL_ASSETS = Path(r"C:\Users\silve\.gemini\antigravity\scratch\gzzio1989.github.io\apps\vocalgzzio\assets")
ART_DIR = Path(r"C:\Users\silve\.gemini\antigravity\brain\8628be81-113b-4def-9d88-27b577e549be")
MASCOT_PATH = ROOT / "Resources" / "puniguji_kawaii.png"
MOCHI_FONT_PATH = DOCS_ASSETS / "MochiyPopOne.ttf"
OVERVIEW_PNG = DOCS_ASSETS / "overview.png"

FONT_BOLD_CANDIDATES = [
    r"C:\Windows\Fonts\segoeui.ttf",
    r"C:\Windows\Fonts\segoeuib.ttf",
    r"C:\Windows\Fonts\arialbd.ttf",
    r"C:\Windows\Fonts\YuGothB.ttc",
]
FONT_SEGOE_BOLD = [
    r"C:\Windows\Fonts\YuGothB.ttc",
    r"C:\Windows\Fonts\meiryob.ttc",
    r"C:\Windows\Fonts\BIZ-UDGothicB.ttc",
]
FONT_SEGOE_REG = [
    r"C:\Windows\Fonts\YuGothM.ttc",
    r"C:\Windows\Fonts\meiryo.ttc",
    r"C:\Windows\Fonts\BIZ-UDGothicR.ttc",
]

CREAM = (251, 246, 236)
CREAM_CARD = (255, 253, 249)
CARD_BORDER = (224, 213, 196)
INK = (62, 49, 42)
SUB = (112, 95, 84)
ORANGE_ACCENT = (196, 102, 64)
PEACH_BG = (252, 236, 229)
PEACH_BORDER = (228, 152, 130)
TEAL_ACCENT = (42, 130, 114)
TEAL_BG = (224, 245, 238)
TEAL_BORDER = (138, 206, 188)
PINK_ACCENT = (194, 68, 112)
PINK_BG = (253, 232, 239)
PINK_BORDER = (234, 166, 186)


def get_mochi(size: int) -> ImageFont.FreeTypeFont:
    if MOCHI_FONT_PATH.exists():
        return ImageFont.truetype(str(MOCHI_FONT_PATH), size)
    return get_sans(size, bold=True)


def get_sans(size: int, bold: bool = True) -> ImageFont.FreeTypeFont:
    cands = FONT_SEGOE_BOLD if bold else FONT_SEGOE_REG
    for p in cands:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


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
              bg_color, border_color, text_color, font_size=22, use_mochi=False):
    d.rounded_rectangle([x, y, x + w, y + h], radius=h // 2, fill=bg_color, outline=border_color, width=3)
    f = get_mochi(font_size) if use_mochi else get_sans(font_size, bold=True)
    bb = d.textbbox((0, 0), text, font=f)
    tw, th = bb[2] - bb[0], bb[3] - bb[1]
    d.text((x + (w - tw) / 2 - bb[0], y + (h - th) / 2 - bb[1]), text, font=f, fill=text_color)


def draw_footer(d: ImageDraw.ImageDraw, w: int, h: int, right_text: str):
    y_line = h - 78
    d.line([56, y_line, w - 56, y_line], fill=(216, 204, 188), width=2)
    f = get_sans(19, bold=True)
    d.text((56, h - 52), "G'zzio  |  VocalGzzio v5.4.0", font=f, fill=(142, 126, 112))
    bb = d.textbbox((0, 0), right_text, font=f)
    tw = bb[2] - bb[0]
    d.text((w - 56 - tw, h - 52), right_text, font=f, fill=SUB)


# ---------------------------------------------------------------------------
# 1. Visual English UI Map (1600 x 1440)
# ---------------------------------------------------------------------------
def make_visual_ui_map():
    w, h = 1600, 1440
    im = make_cream_canvas(w, h, mascot_pos=None)
    d = ImageDraw.Draw(im)

    # Top Title Bar
    d.rounded_rectangle([48, 24, 380, 78], radius=27, fill=PINK_ACCENT)
    d.text((74, 34), "10-Second UI Map", font=get_mochi(26), fill=(255, 255, 255))
    d.text((404, 28), "VocalGzzio — Visual English Cheat Sheet", font=get_mochi(34), fill=INK)
    d.text((406, 74), "Even with Japanese labels on screen, these 10 numbered zones control everything you need.", font=get_sans(21, True), fill=SUB)

    # Paste overview.png at (48, 112) scaled to 1504 x 848
    sx, sy, sw, sh = 48, 112, 1504, 848
    if OVERVIEW_PNG.exists():
        src = Image.open(OVERVIEW_PNG).convert("RGBA").resize((sw, sh), Image.LANCZOS)
        mask = Image.new("L", (sw, sh), 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, sw - 1, sh - 1], radius=18, fill=255)
        im.paste(src, (sx, sy), mask)
        d.rounded_rectangle([sx, sy, sx + sw - 1, sy + sh - 1], radius=18, outline=(90, 180, 210), width=3)

    # Overlay numbered callout badges + short English tags right next to each badge
    # Coordinates relative to screenshot (0..1)
    callouts = [
        ("1", 0.418, 0.098, "Auto-Setup (Sing/Talk)", TEAL_ACCENT),
        ("2", 0.758, 0.098, "BGM & 3-Stem Rec", PINK_ACCENT),
        ("3", 0.865, 0.052, "Hold = Dry Compare", ORANGE_ACCENT),
        ("4", 0.022, 0.165, "Pitch & Key Meter", TEAL_ACCENT),
        ("5", 0.022, 0.432, "01 Clean & De-Reverb", TEAL_ACCENT),
        ("6", 0.324, 0.165, "3D Tone & Air Sphere", PINK_ACCENT),
        ("7", 0.324, 0.562, "02 Shape, Pitch & Unison", PINK_ACCENT),
        ("8", 0.698, 0.165, "I/O Meters & Gain", ORANGE_ACCENT),
        ("9", 0.698, 0.432, "03 Space & Room IRs", TEAL_ACCENT),
        ("10", 0.672, 0.945, "Chorus Boost & SE Pads", PINK_ACCENT),
    ]

    for num, rx, ry, tag, col in callouts:
        cx = int(sx + rx * sw)
        cy = int(sy + ry * sh)
        r = 20
        # Outer white halo + colored circle
        d.ellipse([cx - r - 3, cy - r - 3, cx + r + 3, cy + r + 3], fill=(255, 255, 255, 245))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col, outline=(255, 255, 255), width=2)
        fn = get_sans(20, bold=True)
        bb = d.textbbox((0, 0), num, font=fn)
        tw, th = bb[2] - bb[0], bb[3] - bb[1]
        d.text((cx - tw / 2 - bb[0], cy - th / 2 - bb[1]), num, font=fn, fill=(255, 255, 255))

        # Small high-contrast label pill next to badge
        ft = get_sans(15, bold=True)
        tbb = d.textbbox((0, 0), tag, font=ft)
        ttw = tbb[2] - tbb[0]
        px = cx + r + 6
        py = cy - 15
        if px + ttw + 20 > sx + sw - 8:
            px = cx - r - ttw - 24
        d.rounded_rectangle([px, py, px + ttw + 18, py + 30], radius=8, fill=(20, 18, 24, 230), outline=col, width=2)
        d.text((px + 9, py + 6), tag, font=ft, fill=(255, 250, 240))

    # Bottom Legend Grid (2 columns x 5 rows) from y=980 to y=1350
    legend_items = [
        ("1", "うた自動 / トーク自動  (1-Click Auto-Setup)",
         "Start here! Sing or speak for 5s to auto-set EQ, compression & de-esser.", TEAL_ACCENT, TEAL_BG),
        ("2", "♪ BGM・録音 & 歌枠/MC  (Karaoke & Recorder)",
         "Load backing tracks, 1-click Vocal Remover, Key Shift (±6) & 3-Stem WAV Rec.", PINK_ACCENT, PINK_BG),
        ("3", "押して原音と比較 / 初期化  (Hold Dry/Wet & Reset)",
         "Hold to compare raw microphone input vs. processed sound; 1-click Reset.", ORANGE_ACCENT, PEACH_BG),
        ("4", "歌声・音程 / 音域チェック  (Live Pitch & Range)",
         "Real-time note tuner (cents), pitch stability history & vocal range checker.", TEAL_ACCENT, TEAL_BG),
        ("5", "01 整える  (Zone 01: Clean Up & Room De-Reverb)",
         "1-click Noise Learn (じどう学習), Gate, Low-Cut, De-Mud & 0ms Room De-Reverb.", TEAL_ACCENT, TEAL_BG),
        ("6", "ARGON 3D  (3D Tone Sphere & 4 Quick Presets)",
         "Drag 1 orb for Velvet warmth, Core focus, Silk air & 3D Halo (+ 4 style buttons).", PINK_ACCENT, PINK_BG),
        ("7", "02 声をつくる  (Zone 02: Shape, Pitch & Expression)",
         "Leveler, De-Esser, Brightness, 0.08ms Pitch, 5-Voice Unison & v5.4 Expression.", PINK_ACCENT, PINK_BG),
        ("8", "計器・入出力  (Meters, Auto-Mono & Gain)",
         "Auto-centers Ch.2 XLR mics (Scarlett Solo etc.) + Input/Output Gain knobs.", ORANGE_ACCENT, PEACH_BG),
        ("9", "03 空間をつくる  (Zone 03: Reverb & Space)",
         "Reverb Amount/Length, Echo, Stereo Width, Tempo Fit, Zero-Lag & Custom IRs.", TEAL_ACCENT, TEAL_BG),
        ("10", "★サビON! & SE Pads  (Chorus Boost & 8 Sound Pads)",
         "1-click/Auto Chorus Boost + 4 built-in SEs (Applause/Sparkle/Drum/Bell) + 4 Custom SEs.", PINK_ACCENT, PINK_BG),
    ]

    col_w = 740
    row_h = 72
    start_y = 978
    for idx, (num, title, desc, col, bg) in enumerate(legend_items):
        c = idx // 5
        r = idx % 5
        lx = 48 + c * (col_w + 24)
        ly = start_y + r * (row_h + 8)
        d.rounded_rectangle([lx, ly, lx + col_w, ly + row_h], radius=14, fill=CREAM_CARD, outline=CARD_BORDER, width=2)
        d.ellipse([lx + 12, ly + 14, lx + 56, ly + 58], fill=bg, outline=col, width=2)
        fn = get_sans(21, True)
        bb = d.textbbox((0, 0), num, font=fn)
        d.text((lx + 34 - (bb[2] - bb[0]) / 2 - bb[0], ly + 36 - (bb[3] - bb[1]) / 2 - bb[1]), num, font=fn, fill=col)
        d.text((lx + 68, ly + 9), title, font=get_sans(19, True), fill=INK)
        d.text((lx + 68, ly + 39), desc, font=get_sans(16, False), fill=SUB)

    draw_footer(d, w, h, "Full interactive guide: https://gzzio1989.github.io/apps/vocalgzzio/en.html")

    out_docs = DOCS_ASSETS / "overview-en-map.png"
    out_booth = OUT_DIR / "BOOTH_EN_1_Visual_UI_Map.png"
    im.convert("RGB").save(out_docs, quality=95)
    im.convert("RGB").save(out_booth, quality=95)
    if PORTAL_ASSETS.exists():
        im.convert("RGB").save(PORTAL_ASSETS / "overview-en-map.png", quality=95)
    if ART_DIR.exists():
        im.convert("RGB").save(ART_DIR / "BOOTH_EN_1_Visual_UI_Map.png", quality=95)


# ---------------------------------------------------------------------------
# 2. BOOTH_EN_0_Thumbnail_Simple.png (1200 x 1200)
# ---------------------------------------------------------------------------
def make_simple_en_thumb():
    w, h = 1200, 1200
    im = make_cream_canvas(w, h, mascot_pos=(48, 170), mascot_size=(370, 410))
    d = ImageDraw.Draw(im)

    # Top pill badges
    d.rounded_rectangle([56, 48, 565, 124], radius=38, fill=(194, 75, 114))
    d.text((88, 66), "0.08ms Zero-Lag · No DAW", font=get_mochi(28), fill=(255, 255, 255))

    d.rounded_rectangle([588, 48, 1144, 124], radius=38, fill=TEAL_BG, outline=TEAL_BORDER, width=3)
    d.text((620, 66), "Standalone App + VST3 / AU", font=get_mochi(27), fill=TEAL_ACCENT)

    # Main Title
    d.text((456, 178), "VocalGzzio", font=get_mochi(82), fill=INK)
    d.text((460, 294), "Put Your Voice Up Front.", font=get_mochi(42), fill=(194, 75, 114))
    d.text((464, 376), "All-in-One Vocal Studio for Karaoke Streams (OBS),", font=get_sans(27, True), fill=SUB)
    d.text((464, 418), "VTubers, Covers & VRChat.  (v5.4.0 / Win & Mac)", font=get_sans(27, True), fill=SUB)

    # Quick highlight pill under title
    draw_pill(d, 464, 474, 680, 56, "★ Includes Visual English Cheat Sheet & WDM Driver",
              CREAM_CARD, CARD_BORDER, ORANGE_ACCENT, font_size=22, use_mochi=False)

    # 3 Big Numbered Cards
    cards = [
        ("1", "Never Get Buried in the Mix",
         "Carves a vocal pocket in the backing track & auto-rides your volume in choruses",
         TEAL_ACCENT, TEAL_BG),
        ("2", "Erase Bedroom Reverb (0ms Lag)",
         "Subtracts boxy wall reflections, tames loud breaths & smooths vowel balance",
         (194, 75, 114), PINK_BG),
        ("3", "1-Click Karaoke & 3-Stem WAV Rec",
         "Strip vocals from any song, shift key (±6) & export -14 LUFS Master + Wet + Dry",
         ORANGE_ACCENT, PEACH_BG),
    ]
    cy = 576
    for num, title, sub, col, bg in cards:
        d.rounded_rectangle([56, cy, w - 56, cy + 146], radius=24, fill=CREAM_CARD, outline=CARD_BORDER, width=3)
        d.ellipse([84, cy + 29, 84 + 88, cy + 29 + 88], fill=bg, outline=col, width=3)
        d.text((112, cy + 39), num, font=get_mochi(42), fill=col)
        d.text((196, cy + 24), title, font=get_mochi(34), fill=INK)
        d.text((196, cy + 88), sub, font=get_sans(23, True), fill=SUB)
        cy += 164

    # Bottom Call-to-Action Bar
    d.rounded_rectangle([56, 1084, w - 56, 1160], radius=38, fill=(52, 128, 110))
    d.text((98, 1103), "Try the $0 Free Trial First!  (Full Version: ¥2,500 / ~$17 One-Time)", font=get_sans(27, True), fill=(255, 255, 255))

    out = OUT_DIR / "BOOTH_EN_0_Thumbnail_Simple.png"
    im.convert("RGB").save(out, quality=95)
    if ART_DIR.exists():
        im.convert("RGB").save(ART_DIR / "BOOTH_EN_0_Thumbnail_Simple.png", quality=95)


# ---------------------------------------------------------------------------
# 3. BOOTH_EN_2_Features_v540.png (1200 x 1200)
# ---------------------------------------------------------------------------
def make_features_en():
    w, h = 1200, 1200
    im = make_cream_canvas(w, h, mascot_pos=(48, 44), mascot_size=(235, 235))
    d = ImageDraw.Draw(im)

    d.text((310, 48), "What's Inside VocalGzzio v5.4.0", font=get_sans(24, True), fill=ORANGE_ACCENT)
    d.text((310, 88), "Pro Vocal Expression & Mix", font=get_mochi(44), fill=INK)
    d.text((310, 154), "Without a DAW or Booth.", font=get_mochi(44), fill= PINK_ACCENT)
    d.text((310, 224), "Every DSP module runs at 0.0ms–0.08ms latency for live monitoring.", font=get_sans(22, True), fill=SUB)

    cards = [
        ("★ NEW v5.4 & v5.3: Vocal Expression, Sweet Spot & Cue Monitor",
         "• Tail Vibrato & Vowel EQ: Natural 5.5Hz vibrato on long notes + uniform vowel body.\n"
         "• Upward Whisper Lifter & Dolby-A Air: Lifts quiet details +4.5dB; silky vowel-only air.\n"
         "• Pitch-Tracked Sweet Spot & Bone-Conduction Cue Monitor: Easy high notes in your cans.",
         PINK_BG, PINK_BORDER, PINK_ACCENT),
        ("♪ Spectral Unmasker, BGM Vocal Rider & 1-Click Karaoke Maker",
         "• Spectral Unmasker: Carves up to -5dB in the track's Mid channel only while you sing.\n"
         "• BGM Vocal Rider: Automatically rides your vocal fader (-2.5 to +3.5dB) into the chorus.\n"
         "• Built-in BGM Player: 1-click Vocal Remover, Key Shift (±6), 1s Key/BPM Auto-Detect.",
         TEAL_BG, TEAL_BORDER, TEAL_ACCENT),
        ("◎ 0ms Room De-Reverb, 0.08ms Pitch & 3-Stem WAV Export",
         "• Zero-Lag Room De-Reverb & Breath Leveler: Subtracts bedroom slapback & 460Hz boxiness.\n"
         "• 4 Studio Mic Models (800G / U87 / SM7B / ASMR) + Mouth De-Click + 5-Voice Harmonies.\n"
         "• Records 3 WAV stems at once: (-14 LUFS Full Mix + Wet Vocal + Raw Dry Vocal) + 8 SE Pads.",
         PEACH_BG, PEACH_BORDER, ORANGE_ACCENT),
    ]

    cy = 284
    for title, body, bg_c, bd_c, fg_c in cards:
        d.rounded_rectangle([56, cy, w - 56, cy + 212], radius=22, fill=CREAM_CARD, outline=bd_c, width=3)
        d.rounded_rectangle([76, cy + 16, w - 76, cy + 64], radius=14, fill=bg_c, outline=bd_c, width=2)
        d.text((94, cy + 25), title, font=get_sans(22, True), fill=fg_c)
        for idx, line in enumerate(body.split("\n")):
            d.text((84, cy + 80 + idx * 40), line, font=get_sans(20, True), fill=INK)
        cy += 232

    draw_pill(d, 56, 994, 348, 60, "♪ 1-Click Vocal Remover & Key ±6", PINK_BG, PINK_BORDER, PINK_ACCENT, 20)
    draw_pill(d, 422, 994, 356, 60, "★ -14 LUFS 3-Stem WAV Export", TEAL_BG, TEAL_BORDER, TEAL_ACCENT, 20)
    draw_pill(d, 796, 994, 348, 60, "◎ 4 Built-in + 4 Custom SE Pads", CREAM_CARD, CARD_BORDER, INK, 20)

    draw_footer(d, w, h, "Includes WHQL-signed WDM Virtual Driver for direct OBS / Discord / VRChat routing")
    out = OUT_DIR / "BOOTH_EN_2_Features_v540.png"
    im.convert("RGB").save(out, quality=95)
    if ART_DIR.exists():
        im.convert("RGB").save(ART_DIR / "BOOTH_EN_2_Features_v540.png", quality=95)


# ---------------------------------------------------------------------------
# 4. BOOTH_EN_3_Specs_and_Trial.png (1200 x 1200)
# ---------------------------------------------------------------------------
def make_specs_en():
    w, h = 1200, 1200
    im = make_cream_canvas(w, h, mascot_pos=(48, 44), mascot_size=(235, 235))
    d = ImageDraw.Draw(im)

    d.text((310, 48), "System Requirements, License & Free Trial", font=get_sans(24, True), fill=ORANGE_ACCENT)
    d.text((310, 88), "Before You Buy:", font=get_mochi(46), fill=INK)
    d.text((310, 154), "Everything You Need to Know", font=get_mochi(38), fill=TEAL_ACCENT)
    d.text((310, 222), "One purchase includes both Windows & macOS builds + free updates.", font=get_sans(22, True), fill=SUB)

    d.rounded_rectangle([56, 274, w - 56, 650], radius=22, fill=CREAM_CARD, outline=CARD_BORDER, width=2)
    rows = [
        ("Windows", "Windows 10 / 11 (64-bit) — Standalone (.exe) + VST3 + WHQL WDM Driver", INK),
        ("macOS", "macOS 11+ (Apple Silicon & Intel Universal) — Standalone + AU + VST3", TEAL_ACCENT),
        ("Price", "¥2,500 JPY (~$17 USD) One-Time Purchase — No subscription, free updates", ORANGE_ACCENT),
        ("Commercial", "100% OK for monetized streams, SuperChats, covers & sales (No credit needed)", PINK_ACCENT),
        ("Devices", "Install on up to 3 computers owned by you (Win & Mac both included)", SUB),
    ]
    ry = 296
    for k, v, col in rows:
        d.text((84, ry), k, font=get_sans(24, True), fill=INK)
        d.text((264, ry), v, font=get_sans(22, True), fill=col)
        if ry < 560:
            d.line([84, ry + 54, w - 84, ry + 54], fill=(232, 224, 212), width=2)
        ry += 72

    # Free Trial vs Full Version Box
    d.rounded_rectangle([56, 674, w - 56, 920], radius=20, fill=TEAL_BG, outline=TEAL_BORDER, width=2)
    d.text((84, 694), "★ Free Trial ($0) vs. Full Version (¥2,500 JPY)", font=get_sans(26, True), fill=TEAL_ACCENT)
    d.text((84, 738), "• Identical audio engine, 19 themes, 160+ supported interfaces & WDM virtual driver.", font=get_sans(21, True), fill=INK)
    d.text((84, 778), "• Trial Limitation 1: Volume dips briefly for 0.6s once every 60 seconds.", font=get_sans(21, True), fill=INK)
    d.text((84, 818), "• Trial Limitation 2: Settings reset on app close (Full Version auto-saves everything).", font=get_sans(21, True), fill=INK)
    d.text((84, 858), "• Trial is installed separately as 'VocalGzzio Trial' so it never conflicts with Full.", font=get_sans(21, True), fill=SUB)

    # First-launch note + Overseas checkout note
    d.rounded_rectangle([56, 942, w - 56, 1088], radius=20, fill=PEACH_BG, outline=PEACH_BORDER, width=2)
    d.text((84, 960), "International Checkout & First-Launch OS Note", font=get_sans(24, True), fill=(180, 72, 52))
    d.text((84, 1000), "• Checkout via PayPal or Credit Card on BOOTH (English UI toggle at top-right of page).", font=get_sans(20, True), fill=INK)
    d.text((84, 1036), "• Indie build: On first launch, click 'More info -> Run anyway' (Win) or see macOS guide.", font=get_sans(20, True), fill=INK)

    draw_footer(d, w, h, "Mascot art is AI-generated; all C++ DSP audio code is 100% hand-written.")
    out = OUT_DIR / "BOOTH_EN_3_Specs_and_Trial.png"
    im.convert("RGB").save(out, quality=95)
    if ART_DIR.exists():
        im.convert("RGB").save(ART_DIR / "BOOTH_EN_3_Specs_and_Trial.png", quality=95)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    make_visual_ui_map()
    make_simple_en_thumb()
    make_features_en()
    make_specs_en()
    print("Generated all English storefront & UI map images in:", OUT_DIR)


if __name__ == "__main__":
    main()
