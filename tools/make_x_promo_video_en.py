#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VocalGzzio v5.4.0 — English Promotional Before/After Audio & Video Demo Generator
Generates:
1. 記録/X素材_英語版/VocalGzzio_v5.4.0_X_Promo_EN_BeforeAfter.mp4
   - 1280x720 (16:9 HD), 24 fps, 34.0s (H.264 yuv420p + AAC 48kHz Stereo 192kbps)
   - 3 side-by-side Before/After vocal comparisons:
     (1) Raw Untreated Bedroom Mic vs. VocalGzzio v5.4.0 (0ms Room De-Reverb + Focus + Lush Plate)
     (2) Buried in Backing Track vs. Spectral Unmasker + BGM Vocal Rider + 5-Voice Unison
     (3) Concert Hall Singing Mode vs. 1-Click Dry MC Talk Toggle + Cough Mute
     (4) Ending Summary Card (Built-in Full English UI Mode, Free Trial $0, Win & Mac)
2. 記録/X素材_英語版/YouTube_English_Captions_v5.4.0.srt
   - Ready-to-upload English subtitle file (.srt) for YouTube Studio / X video captions.
"""
import math
import os
import shutil
import subprocess
import tempfile
import wave
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SRC_ART_DIR = Path(r"C:\Users\silve\.gemini\antigravity\brain\8f2542b1-d681-42f3-92c9-1e836ae605e5")
CUR_ART_DIR = Path(r"C:\Users\silve\.gemini\antigravity\brain\8628be81-113b-4def-9d88-27b577e549be")
PROMO_450 = ROOT / "review" / "promotion-4.5.0" / "comparison-video"
OUT_EN_DIR = ROOT / "記録" / "X素材_英語版"
FFMPEG = r"C:\Users\silve\AppData\Local\Microsoft\WinGet\Links\ffmpeg.EXE"

W, H = 1280, 720
FPS = 24
DURATION = 34.0
TOTAL_FRAMES = int(DURATION * FPS)

FONT_BOLD_CANDS = [
    r"C:\Windows\Fonts\YuGothB.ttc",
    r"C:\Windows\Fonts\meiryob.ttc",
    r"C:\Windows\Fonts\BIZ-UDGothicB.ttc",
]


def get_font(size: int) -> ImageFont.FreeTypeFont:
    for p in FONT_BOLD_CANDS:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def load_ui(preferred: str, fallback: str, target_size=(1080, 609)) -> Image.Image:
    p = SRC_ART_DIR / preferred
    if not (p.exists() and p.stat().st_size > 10000):
        p = SRC_ART_DIR / fallback
    if not p.exists():
        p = ROOT / "docs" / "assets" / "overview.png"
    im = Image.open(p).convert("RGBA")
    return im.resize(target_size, Image.LANCZOS)


def ease_in_out(t: float) -> float:
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * ease_in_out(t)


def draw_cursor(canvas: Image.Image, cx: int, cy: int, clicking: bool = False, ripple_prog: float = -1.0):
    d = ImageDraw.Draw(canvas, "RGBA")
    if 0.0 <= ripple_prog <= 1.0:
        r = int(12 + 44 * ripple_prog)
        alpha = int(230 * (1.0 - ripple_prog))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 225, 120, alpha), width=4)
        r2 = int(6 + 26 * ripple_prog)
        d.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], fill=(255, 105, 165, int(140 * (1.0 - ripple_prog))))

    scale = 0.88 if clicking else 1.0
    pts = [
        (0, 0),
        (0, int(34 * scale)),
        (int(9 * scale), int(26 * scale)),
        (int(16 * scale), int(40 * scale)),
        (int(22 * scale), int(37 * scale)),
        (int(15 * scale), int(23 * scale)),
        (int(26 * scale), int(23 * scale)),
    ]
    abs_pts = [(cx + px, cy + py) for px, py in pts]
    shadow_pts = [(cx + px + 3, cy + py + 4) for px, py in pts]
    d.polygon(shadow_pts, fill=(0, 0, 0, 175))
    d.polygon(abs_pts, fill=(255, 255, 255, 255), outline=(24, 16, 28, 255))
    d.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], fill=(255, 90, 150, 240))


def draw_callout(canvas: Image.Image, box: tuple, title: str, subtitle: str, accent_col=(255, 105, 160)):
    d = ImageDraw.Draw(canvas, "RGBA")
    x, y, w, h = box
    d.rounded_rectangle([x + 4, y + 6, x + w + 4, y + h + 6], radius=16, fill=(0, 0, 0, 155))
    d.rounded_rectangle([x, y, x + w, y + h], radius=16, fill=(20, 14, 26, 242), outline=accent_col + (255,), width=3)
    d.rounded_rectangle([x + 14, y + 12, x + 22, y + h - 12], radius=4, fill=accent_col + (255,))
    f_t = get_font(21)
    f_s = get_font(16)
    d.text((x + 34, y + 13), title, font=f_t, fill=(255, 248, 252, 255))
    d.text((x + 34, y + 46), subtitle, font=f_s, fill=(225, 215, 232, 255))


def draw_ab_banner(canvas: Image.Image, is_after: bool, label_main: str, label_sub: str):
    d = ImageDraw.Draw(canvas, "RGBA")
    x, y, w, h = 116, 94, 1048, 64
    if not is_after:
        bg = (36, 46, 58, 235)
        bd = (120, 175, 210, 255)
        tag_bg = (80, 125, 155, 255)
        tag_txt = "BEFORE (Raw Dry Mic)"
    else:
        bg = (52, 18, 38, 240)
        bd = (255, 110, 168, 255)
        tag_bg = (235, 68, 132, 255)
        tag_txt = "AFTER (VocalGzzio ON!)"
    d.rounded_rectangle([x, y, x + w, y + h], radius=14, fill=bg, outline=bd, width=3)
    d.rounded_rectangle([x + 10, y + 10, x + 285, y + h - 10], radius=10, fill=tag_bg)
    d.text((x + 22, y + 20), tag_txt, font=get_font(19), fill=(255, 255, 255, 255))
    d.text((x + 302, y + 11), label_main, font=get_font(19), fill=(255, 248, 252, 255))
    d.text((x + 302, y + 37), label_sub, font=get_font(15), fill=(220, 208, 228, 255))


def draw_stage_studio_overlay(canvas: Image.Image, highlight_sec: int = 0):
    d = ImageDraw.Draw(canvas, "RGBA")
    px, py, pw, ph = 520, 166, 640, 384
    d.rounded_rectangle([px + 6, py + 8, px + pw + 6, py + ph + 8], radius=16, fill=(0, 0, 0, 170))
    d.rounded_rectangle([px, py, px + pw, py + ph], radius=16, fill=(24, 18, 32, 248), outline=(110, 225, 245, 255), width=3)
    d.text((px + 20, py + 14), "♪ VocalGzzio v5.4.0 — Stage, Studio & Expression Suite", font=get_font(19), fill=(130, 235, 255, 255))

    secs = [
        ("1. Spectral Unmasker, BGM Rider & 1-Click Vocal Remover",
         "Carves a -5dB pocket in the track's Mid channel + Key Shift (±6) & Auto-Key Sync",
         (90, 210, 245)),
        ("2. -14 LUFS 3-Stem WAV Export & Save Last 30 Seconds",
         "Simultaneously exports -14 LUFS Master + Wet Vocal + Raw Dry Vocal (24-bit WAV)",
         (245, 105, 155)),
        ("3. v5.4 Tail Vibrato, Vowel Formant EQ & Cue Monitor",
         "Smooth 5.5Hz tail vibrato, uniform vowel warmth & bone-conduction headphone cue",
         (85, 220, 185)),
        ("4. 1-Click Sing/MC Toggle, Cough Mute & Scream Guard",
         "Instant dry voice for chatting between songs + 8ms pop-free Cough Mute & 8 SE Pads",
         (245, 190, 95)),
    ]
    sy = py + 52
    for idx, (stitle, sdesc, scol) in enumerate(secs):
        is_hi = (idx == highlight_sec)
        bg = (46, 32, 58, 245) if is_hi else (32, 24, 42, 220)
        bw = 3 if is_hi else 1
        d.rounded_rectangle([px + 14, sy, px + pw - 14, sy + 72], radius=10, fill=bg, outline=scol + (255 if is_hi else 140,), width=bw)
        d.text((px + 24, sy + 10), stitle, font=get_font(17), fill=scol + (255,))
        d.text((px + 24, sy + 39), sdesc, font=get_font(13), fill=(235, 228, 242, 255))
        sy += 80


# Reuse audio synthesis from make_x_promo_video_v460
from make_x_promo_video_v460 import build_audio_track


def write_english_srt(srt_path: Path):
    srt_content = """1
00:00:00,000 --> 00:00:04,800
[Comparison 1: BEFORE] Raw untreated bedroom mic (300Hz room boxiness, dull highs & PC fan noise).

2
00:00:04,800 --> 00:00:10,000
[Comparison 1: AFTER] Exact same take with VocalGzzio ON! Zero-lag room de-reverb, vocal focus, silky air & lush plate reverb.

3
00:00:10,000 --> 00:00:14,200
[Comparison 2: BEFORE] Raw vocal over a backing track — the voice gets buried under the instruments.

4
00:00:14,200 --> 00:00:19,000
[Comparison 2: AFTER] Spectral Unmasker carves a vocal pocket in the track + BGM Vocal Rider & 5-Voice Unison Harmonies!

5
00:00:19,000 --> 00:00:23,200
[Comparison 3: Singing Mode] Singing with lush concert hall reverb and pitch-tracked sweet spot EQ.

6
00:00:23,200 --> 00:00:27,500
[Comparison 3: 1-Click Talk/MC Mode] Click [Sing/MC] to instantly mute reverb for crystal-clear chatting between songs!

7
00:00:27,500 --> 00:00:34,000
VocalGzzio v5.4.0 (Win & Mac) — Built-in Full English UI Mode ([文字・見た目] -> [ブランド]). Try the $0 Free Trial on BOOTH!
"""
    srt_path.write_text(srt_content, encoding="utf-8")


def main():
    OUT_EN_DIR.mkdir(parents=True, exist_ok=True)
    write_english_srt(OUT_EN_DIR / "YouTube_English_Captions_v5.4.0.srt")

    ui_Sz = (1080, 609)
    uis = {
        "rose": load_ui("v4.8.0_couture_stage.png", "v4.7.0_couture_stage.png", ui_Sz),
        "lilac": load_ui("v4.8.0_couture_02_lilac.png", "v4.6.0_couture_02_lilac.png", ui_Sz),
        "noir": load_ui("v4.8.0_couture_03_noir.png", "v4.6.0_couture_03_noir.png", ui_Sz),
        "mint": load_ui("v4.8.0_couture_04_mint.png", "v4.6.0_couture_04_mint.png", ui_Sz),
        "peach": load_ui("v4.8.0_couture_05_peach.png", "v4.6.0_couture_05_peach.png", ui_Sz),
        "argon": load_ui("v4.8.0_argon_cockpit.png", "v4.7.0_argon_cockpit.png", ui_Sz),
    }

    mini_sz = (224, 126)
    minis = [
        ("01 Rose", uis["rose"].resize(mini_sz, Image.LANCZOS), (236, 105, 148)),
        ("02 Lilac", uis["lilac"].resize(mini_sz, Image.LANCZOS), (175, 145, 248)),
        ("03 Noir", uis["noir"].resize(mini_sz, Image.LANCZOS), (225, 82, 132)),
        ("04 Mint", uis["mint"].resize(mini_sz, Image.LANCZOS), (82, 212, 190)),
        ("05 Peach", uis["peach"].resize(mini_sz, Image.LANCZOS), (245, 152, 112)),
    ]

    f_top = get_font(19)
    f_badge = get_font(17)
    f_big = get_font(34)
    f_med = get_font(20)
    f_sm = get_font(16)

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        wav_path = tmp / "audio.wav"
        build_audio_track(wav_path)

        out_mp4 = OUT_EN_DIR / "VocalGzzio_v5.4.0_X_Promo_EN_BeforeAfter.mp4"
        cmd = [
            FFMPEG, "-y",
            "-f", "rawvideo",
            "-vcodec", "rawvideo",
            "-s", f"{W}x{H}",
            "-pix_fmt", "rgb24",
            "-r", str(FPS),
            "-i", "-",
            "-i", str(wav_path),
            "-c:v", "libx264",
            "-preset", "fast",
            "-crf", "18",
            "-pix_fmt", "yuv420p",
            "-c:a", "aac",
            "-b:a", "192k",
            "-shortest",
            "-movflags", "+faststart",
            str(out_mp4),
        ]
        err_file = open(tmp / "ffmpeg_err.log", "w", encoding="utf-8")
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=err_file)

        ui_x, ui_y = 100, 86

        for frame_idx in range(TOTAL_FRAMES):
            t = frame_idx / FPS
            canvas = Image.new("RGBA", (W, H), (18, 14, 24, 255))
            d = ImageDraw.Draw(canvas, "RGBA")

            d.rectangle([0, 0, W, 72], fill=(28, 20, 36, 255))
            d.line([0, 72, W, 72], fill=(225, 120, 165, 180), width=2)

            if t < 4.8:
                step_txt = "Test 1 [Same Vocal Take]  First, listen to the raw untreated bedroom mic..."
                audio_badge = ("♪ Playing: BEFORE (Raw Mic)", (75, 130, 165))
                canvas.paste(uis["argon"], (ui_x, ui_y))
                draw_ab_banner(canvas, False,
                               "Raw Bedroom Mic: Boxy room resonance (300-460Hz), dull highs & PC fan hiss",
                               "Standard untreated home mic — switches to VocalGzzio ON at 4.8s on the exact same phrase!")
                sub_t = t / 4.8
                cx = int(lerp(760, 965, min(1.0, sub_t * 2.5)))
                cy = int(lerp(220, 142, min(1.0, sub_t * 2.5)))
                d.rounded_rectangle([910, 124, 1020, 160], radius=8, outline=(120, 225, 255, 255), width=3)
                draw_callout(canvas, (116, 565, 1048, 84),
                             "♪ Test 1 BEFORE: Untreated bedroom vocal (boxy reflections, thin core & background hiss)",
                             "In a moment, hear the exact same vocal phrase with VocalGzzio v5.4.0 turned ON!",
                             (90, 195, 240))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.0) % 1.0)

            elif t < 10.0:
                step_txt = "Test 1 [Same Vocal Take]  VocalGzzio ON! 0ms Room De-Reverb + Focus + Lush Reverb"
                audio_badge = ("★ Playing: AFTER (VocalGzzio ON)", (235, 68, 132))
                canvas.paste(uis["argon"], (ui_x, ui_y))
                draw_ab_banner(canvas, True,
                               "Same Take, Night & Day! Denoise + 0ms Room De-Reverb + Core Focus + Lush Plate",
                               "Just click 1-Click Auto-Setup (Sing) or drag the 3D Tone Orb — routes straight to OBS & VRChat")
                sub_t = (t - 4.8) / 5.2
                angle = sub_t * math.pi * 2.0
                cx = int(545 + 30 * math.cos(angle))
                cy = int(310 + 22 * math.sin(angle))
                r_glow = int(44 + 8 * math.sin(t * 6.0))
                d.ellipse([cx - r_glow, cy - r_glow, cx + r_glow, cy + r_glow], outline=(255, 115, 175, 230), width=4)
                draw_callout(canvas, (116, 565, 1048, 84),
                             "★ Test 1 AFTER: Room mud & fan noise vanish; silky Dolby-A air and studio reverb bloom!",
                             "No DAW or Voicemeeter required — includes WHQL-signed WDM Virtual Driver for OBS & Discord",
                             (235, 85, 148))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 3.0) % 1.0)

            elif t < 14.2:
                step_txt = "Test 2 [Singing Over a Track]  Without processing, the vocal gets buried in the mix..."
                audio_badge = ("♪ Playing: BEFORE (Track + Raw Mic)", (75, 130, 165))
                canvas.paste(uis["rose"], (ui_x, ui_y))
                draw_ab_banner(canvas, False,
                               "Backing Track + Raw Vocal: Competing Mid frequencies bury the voice in the chorus",
                               "The #1 struggle in karaoke streams: turning down the track makes the song feel weak and flat")
                sub_t = (t - 10.0) / 4.2
                cx = int(lerp(545, 868, min(1.0, sub_t * 2.5)))
                cy = int(lerp(310, 142, min(1.0, sub_t * 2.5)))
                draw_callout(canvas, (116, 565, 1048, 84),
                             "♪ Test 2 BEFORE: Raw vocal fights with piano/guitars in the center of the backing track",
                             "Next: Turn ON Spectral Unmasker (Track Pocket) + BGM Vocal Rider + 5-Voice Unison!",
                             (90, 195, 240))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.0) % 1.0)

            elif t < 19.0:
                step_txt = "Test 2 [Spectral Unmasker + Unison]  Carves a vocal pocket & rides chorus volume!"
                audio_badge = ("★ Playing: AFTER (Pocket + 5-Voice Unison)", (235, 68, 132))
                canvas.paste(uis["rose"], (ui_x, ui_y))
                draw_ab_banner(canvas, True,
                               "Spectral Unmasker Carves a -5dB Mid Pocket + 5-Voice Unison Harmonies!",
                               "Includes 1-Click Vocal Remover, Key Shift (±6) with Auto-Key Sync, and 3-Stem WAV Export")
                sub_t = (t - 14.2) / 4.8
                hi_idx = 0 if sub_t < 0.5 else 1
                draw_stage_studio_overlay(canvas, highlight_sec=hi_idx)
                cx = 840
                cy = 250 if sub_t < 0.5 else 330
                draw_callout(canvas, (116, 565, 1048, 84),
                             "★ Test 2 AFTER: Your voice sits right in the front pocket without losing backing track power!",
                             "Plus: 'Save Last 30s' simultaneously exports -14 LUFS Master + Wet Vocal + Raw Dry Vocal WAVs",
                             (235, 85, 148))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.5) % 1.0)

            elif t < 23.2:
                step_txt = "Test 3 [Singing <-> Talk MC Toggle]  During a song: Lush Concert Hall Reverb ON"
                audio_badge = ("♪ Playing: Singing Mode (Reverb ON)", (62, 195, 175))
                canvas.paste(uis["lilac"], (ui_x, ui_y))
                draw_ab_banner(canvas, True,
                               "🎤 Singing Mode: Lush Hall Reverb + Tail Vibrato (5.5Hz) + Vowel Formant EQ",
                               "But when the song ends and you chat with viewers, heavy reverb sounds like a bathroom echo...")
                sub_t = (t - 19.0) / 4.2
                draw_stage_studio_overlay(canvas, highlight_sec=2)
                cx = int(lerp(840, 725, min(1.0, sub_t * 2.0)))
                cy = int(lerp(330, 142, min(1.0, sub_t * 2.0)))
                draw_callout(canvas, (116, 565, 1048, 84),
                             "🎤 Singing with lush reverb + v5.4 Natural Tail Vibrato & Bone-Conduction Cue Monitor",
                             "Watch what happens at 23.2s when we click [Sing/MC] to talk to chat between songs!",
                             (68, 205, 182))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.0) % 1.0)

            elif t < 27.5:
                step_txt = "Test 3 [1-Click Talk/MC Mode!]  Reverb mutes in 25ms for crisp, up-close voice chat!"
                audio_badge = ("★ Playing: Talk/MC Mode (Dry & Crisp)", (235, 142, 68))
                canvas.paste(uis["mint"], (ui_x, ui_y))
                draw_ab_banner(canvas, True,
                               "💬 1-Click Talk/MC Toggle! Reverb vanishes in 25ms while keeping Denoise & EQ active",
                               "Click once more when your next song starts to restore 100% of your singing reverb & harmonies")
                sub_t = (t - 23.2) / 4.3
                draw_stage_studio_overlay(canvas, highlight_sec=3)
                cx = int(lerp(725, 815, min(1.0, sub_t * 2.2)))
                cy = 142
                draw_callout(canvas, (116, 565, 1048, 84),
                             "💬 Zero bathroom echo when talking to chat! Plus 8ms pop-free Cough Mute & Scream Guard",
                             "Also includes 4 built-in SE pads (Applause/Sparkle/Drum/Bell) + 4 Custom User Audio SE Pads!",
                             (240, 155, 75))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.5) % 1.0)

            else:
                step_txt = "VocalGzzio v5.4.0 — All-in-One Vocal Studio for Win & Mac ($0 Free Trial Available)"
                audio_badge = ("★ Built-in Full English UI Mode Included!", (235, 85, 148))
                d.rounded_rectangle([48, 88, W - 48, 662], radius=22, fill=(26, 18, 34, 248), outline=(238, 165, 192, 230), width=3)
                d.text((78, 108), "VocalGzzio v5.4.0 — One Screen. Zero Routing Headaches.", font=f_big, fill=(255, 245, 250, 255))
                d.text((78, 158), "Built-in 100% English UI Mode: Click [文字・見た目] -> [ブランド (Brand)] at top-right!", font=f_med, fill=(130, 235, 215, 255))

                targets = [
                    ("1. Karaoke Streamers & VTubers",
                     "• Direct OBS/VRChat WDM Driver\n• Spectral Unmasker & BGM Rider\n• 1-Click Sing/MC & 8 SE Pads",
                     (238, 95, 152)),
                    ("2. Cover Vocalists & Home Studio",
                     "• 0ms Room De-Reverb & 4 Mics\n• 0.08ms Pitch & 5-Voice Unison\n• -14 LUFS 3-Stem WAV Recorder",
                     (75, 215, 190)),
                    ("3. v5.4 Expression & Cue Monitor",
                     "• 5.5Hz Tail Vibrato & Vowel EQ\n• Mix-Voice Harmonic Bridge\n• Bone-Conduction Headphone Cue",
                     (120, 195, 250)),
                ]
                tx = 78
                ty = 204
                tw_card = 362
                for t_head, t_body, t_col in targets:
                    d.rounded_rectangle([tx, ty, tx + tw_card, ty + 188], radius=14, fill=(38, 26, 48, 240), outline=t_col + (255,), width=2)
                    d.text((tx + 16, ty + 14), t_head, font=get_font(19), fill=t_col + (255,))
                    for li, line in enumerate(t_body.split("\n")):
                        d.text((tx + 16, ty + 52 + li * 40), line, font=get_font(16), fill=(248, 242, 252, 255))
                    tx += tw_card + 18

                mx = 78
                my = 410
                for m_title, m_im, m_col in minis:
                    canvas.paste(m_im, (mx, my))
                    d.rounded_rectangle([mx, my, mx + mini_sz[0], my + mini_sz[1]], radius=10, outline=m_col + (255,), width=3)
                    d.rounded_rectangle([mx + 8, my + 92, mx + mini_sz[0] - 8, my + 120], radius=6, fill=(18, 12, 22, 220))
                    d.text((mx + 68, my + 96), m_title, font=f_badge, fill=m_col + (255,))
                    mx += mini_sz[0] + 12

                d.text((78, 554), "★ Try the $0 Free Trial First!  |  Full Version: ¥2,500 JPY (~$17 USD One-Time, Win & Mac Included)",
                       font=get_font(19), fill=(255, 238, 175, 255))
                d.text((78, 588), "Vocal Source: Come Inside (pell) / Madam Snowflake (2019) CC BY 3.0 (ccmixter.org/files/snowflake/59563)",
                       font=f_sm, fill=(175, 162, 185, 255))
                d.text((78, 616), "English Guide: https://gzzio1989.github.io/apps/vocalgzzio/en.html   |   BOOTH: https://gzzio.booth.pm/items/8764003",
                       font=f_sm, fill=(235, 205, 165, 255))

            d.text((24, 22), step_txt, font=f_top, fill=(255, 248, 252, 255))
            b_txt, b_col = audio_badge
            bb = d.textbbox((0, 0), b_txt, font=f_badge)
            bw = bb[2] - bb[0] + 32
            bx = W - bw - 20
            d.rounded_rectangle([bx, 14, bx + bw, 58], radius=22, fill=b_col + (255,), outline=(255, 255, 255, 210), width=2)
            d.text((bx + 16, 25), b_txt, font=f_badge, fill=(255, 255, 255, 255))

            prog_w = int(W * (frame_idx / max(1, TOTAL_FRAMES - 1)))
            d.rectangle([0, H - 6, W, H], fill=(40, 30, 50, 255))
            d.rectangle([0, H - 6, prog_w, H], fill=(235, 95, 155, 255))

            proc.stdin.write(canvas.convert("RGB").tobytes())

        proc.stdin.close()
        ret = proc.wait()
        err_file.close()
        if ret != 0:
            stderr_out = (tmp / "ffmpeg_err.log").read_text(encoding="utf-8", errors="replace")
            raise RuntimeError(f"FFmpeg failed ({ret}):\n{stderr_out}")

    if CUR_ART_DIR.exists():
        shutil.copy2(out_mp4, CUR_ART_DIR / "VocalGzzio_v5.4.0_X_Promo_EN_BeforeAfter.mp4")
    print("Created English Promo Video:", out_mp4, f"({out_mp4.stat().st_size / 1024 / 1024:.2f} MB)")


if __name__ == "__main__":
    main()
