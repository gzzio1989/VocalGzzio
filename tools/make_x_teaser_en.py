#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
make_x_teaser_en.py — 海外X（Twitter）＆ Reddit 投稿用の英語ティザー画像（5枚）を作る。
make_x_teaser.py と同じ寸法・クリーム地・ぷにぐじ・MochiyPopOne書体で英語版を出力する。
右下のぷにぐじ（左端 x=913）に箇条書きやフッターが絶対に重ならないよう自動幅制御を行う。
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, 'docs', 'assets', 'MochiyPopOne.ttf')
MASCOT = os.path.join(ROOT, 'Resources', 'puniguji_kawaii.png')

W, H = 1200, 675

CREAM = (253, 250, 243)
PINK_BG = (252, 241, 241)
MINT_BG = (238, 247, 239)
ACCENT = (194, 75, 114)
INK = (74, 64, 56)
SUB = (123, 112, 98)
FOOT = (139, 128, 114)

X0 = 70
HEAD_SIZE, HEAD_TOP, HEAD_MAXW = 50, 145, 1050
SUB_SIZE, SUB_TOP, SUB_PITCH, SUB_MAXW = 26, 228, 43, 1050
BUL_SIZE, BUL_TOP, BUL_PITCH = 23, 344, 49.5
BUL_TEXT_X, BUL_DOT_CX, BUL_DOT_R = 112, 83, 7.5
BUL_MAXW = 780               # 112 + 780 = 892 < ぷにぐじ左端 913
FOOT_SIZE, FOOT_TOP, FOOT_TRACK = 24, 600, 0.0
BADGE_Y, BADGE_H, BADGE_SIZE, BADGE_PAD = 66, 48, 23, 24
MASCOT_RIGHT, MASCOT_BOTTOM, MASCOT_W, MASCOT_H = 1127, 633, 214, 251

SS = 2


def background():
    im = Image.new('RGB', (W, H), CREAM)
    px = im.load()

    def smoothstep(t):
        t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
        return t * t * (3.0 - 2.0 * t)

    for y in range(H):
        fy_p = max(0.0, (270.0 - y) / 270.0)
        fy_m = max(0.0, (330.0 - y) / 330.0)
        if fy_p <= 0.0 and fy_m <= 0.0:
            continue
        for x in range(W):
            ap = smoothstep((720.0 - x) / 600.0) * fy_p
            am = smoothstep((x - 680.0) / 380.0) * fy_m
            if ap <= 0.0 and am <= 0.0:
                continue
            px[x, y] = tuple(
                int(round(CREAM[c] + ap * (PINK_BG[c] - CREAM[c])
                                   + am * (MINT_BG[c] - CREAM[c])))
                for c in range(3))
    return im


def font(size):
    return ImageFont.truetype(FONT, int(round(size * SS)))


def draw_ink_top(d, x, ink_top, text, f, fill, track=0.0):
    if track:
        x_cur, x0i, y0i = 0.0, None, None
        for ch in text:
            b = f.getbbox(ch)
            if b[2] > b[0]:
                if x0i is None:
                    x0i = x_cur + b[0]
                y0i = b[1] if y0i is None else min(y0i, b[1])
            x_cur += f.getlength(ch) + track
        px = x * SS - (x0i or 0.0)
        py = ink_top * SS - (y0i or 0.0)
        for ch in text:
            d.text((px, py), ch, font=f, fill=fill)
            px += f.getlength(ch) + track
        return
    b = f.getbbox(text)
    d.text((x * SS - b[0], ink_top * SS - b[1]), text, font=f, fill=fill)


def ink_width(text, f, track=0.0):
    if not track:
        b = f.getbbox(text)
        return (b[2] - b[0]) / SS
    x_cur, lo, hi = 0.0, None, None
    for ch in text:
        b = f.getbbox(ch)
        if b[2] > b[0]:
            lo = x_cur + b[0] if lo is None else lo
            hi = x_cur + b[2]
        x_cur += f.getlength(ch) + track
    return ((hi or 0.0) - (lo or 0.0)) / SS


def draw_segments(d, x, ink_top, segs, f, track=0.0):
    whole = ''.join(t for t, _ in segs)
    b = f.getbbox(whole)
    px = x * SS - b[0]
    py = ink_top * SS - b[1]
    for text, fill in segs:
        for ch in text:
            d.text((px, py), ch, font=f, fill=fill)
            px += f.getlength(ch) + track


def make(card, out_path):
    im = background().resize((W * SS, H * SS), Image.NEAREST)
    d = ImageDraw.Draw(im)

    # Badge
    fb = font(BADGE_SIZE)
    bw = ink_width(card['badge'], fb)
    pill_w = bw + BADGE_PAD * 2
    d.rounded_rectangle(
        [X0 * SS, BADGE_Y * SS, (X0 + pill_w) * SS, (BADGE_Y + BADGE_H) * SS],
        radius=(BADGE_H / 2) * SS, fill=ACCENT)
    bb = fb.getbbox(card['badge'])
    d.text(((X0 + pill_w / 2) * SS - (bb[0] + bb[2]) / 2,
            (BADGE_Y + BADGE_H / 2) * SS - (bb[1] + bb[3]) / 2),
           card['badge'], font=fb, fill=(255, 255, 255))

    # Heading
    size = HEAD_SIZE
    while size > 30 and ink_width(card['head'], font(size)) > HEAD_MAXW:
        size -= 1
    draw_ink_top(d, X0, HEAD_TOP, card['head'], font(size), INK)

    # Subtitle
    sub_size = SUB_SIZE
    while sub_size > 18 and max(ink_width(line, font(sub_size)) for line in card['sub']) > SUB_MAXW:
        sub_size -= 1
    fs = font(sub_size)
    for i, line in enumerate(card['sub']):
        draw_ink_top(d, X0, SUB_TOP + SUB_PITCH * i, line, fs, SUB)

    # Bullets (ぷにぐじ左端 913 に重ならないよう BUL_MAXW=780 に収める)
    bul_size = BUL_SIZE
    while bul_size > 16 and max(ink_width(line, font(bul_size)) for line in card['bullets']) > BUL_MAXW:
        bul_size -= 0.5
    fu = font(bul_size)
    for i, line in enumerate(card['bullets']):
        top = BUL_TOP + BUL_PITCH * i
        cy = (top + bul_size * 0.46) * SS
        d.ellipse([BUL_DOT_CX * SS - BUL_DOT_R * SS, cy - BUL_DOT_R * SS,
                   BUL_DOT_CX * SS + BUL_DOT_R * SS, cy + BUL_DOT_R * SS], fill=ACCENT)
        draw_ink_top(d, BUL_TEXT_X, top, line, fu, SUB)

    # Mascot
    m = Image.open(MASCOT).convert('RGBA')
    m = m.crop(m.getchannel('A').getbbox())
    m = m.resize((MASCOT_W * SS, MASCOT_H * SS), Image.LANCZOS)
    im.paste(m, ((MASCOT_RIGHT - MASCOT_W) * SS, (MASCOT_BOTTOM - MASCOT_H) * SS), m)

    # Footer (ぷにぐじ左端 913 に重ならない長さ)
    ff = font(FOOT_SIZE)
    draw_segments(d, X0, FOOT_TOP, [
        (card['version'] + ' / ', FOOT),
        ('Free Trial Available', ACCENT),
        (' / Win & Mac', FOOT),
    ], ff, FOOT_TRACK)

    im.resize((W, H), Image.LANCZOS).save(out_path, optimize=True)
    return out_path


VER = 'VocalGzzio v5.4.0'

CARDS = [
    dict(
        file='X_EN_v5.2.0_1_VocalPocket.png',
        badge='NEW', version=VER,
        head='Bring Vocals Forward Without Turning Down the Track.',
        sub=['Turning down the whole backing track makes the chorus lose its power.',
             'Instead, VocalGzzio carves a tiny pocket just for your voice.'],
        bullets=['Carves competing Mid frequencies (-5dB) only while you sing',
                 'Leaves sub-bass, kick drum, and stereo guitars 100% untouched',
                 'BGM-Linked Rider gently lifts your voice (+3.5dB) in choruses'],
    ),
    dict(
        file='X_EN_v5.2.0_2_RoomDeReverb.png',
        badge='NEW', version=VER,
        head='Remove Untreated Bedroom Reflections in 0.0 ms.',
        sub=['Singing in a normal room adds boxy wall reflections to your mic,',
             'making studio reverb sound muddy when layered on top.'],
        bullets=['Subtracts late room reflections while keeping vocal core intact',
                 'Absorbs 460Hz boxy bedroom resonance automatically',
                 '0.0 ms added latency - monitor live in headphones with zero lag'],
    ),
    dict(
        file='X_EN_v5.2.0_3_BreathAndSibilance.png',
        badge='NEW', version=VER,
        head='Separate Breath Inhales and Harsh Sibilance.',
        sub=['Heavy compression makes loud breath gasps and sharp "S / Sh" sounds',
             'jump out unnaturally in headphones.'],
        bullets=['Detects breath inhales and tames them (-6.5dB) before compression',
                 'Splits vowels and consonants in real time (Velvet Split)',
                 'Cushions 6.8kHz harshness while keeping silky vocal air'],
    ),
    dict(
        file='X_EN_v5.2.0_4_ZeroLagPitchAndKey.png',
        badge='NEW', version=VER,
        head='0.08 ms Zero-Lag Pitch Correction & Auto-Key.',
        sub=['Traditional pitch plugins add 20-40ms of delay in your headphones.',
             'We reduced pitch processing latency down to 0.08 ms (4 samples).'],
        bullets=['Detects backing track Key (24 scales) and BPM in 1 second',
                 'Preserves natural vibrato while supporting long note endings',
                 'Includes 4 Studio Mic Models (800G / U87 / SM7B / ASMR)'],
    ),
    dict(
        file='X_EN_v5.2.0_5_KaraokeAnd3StemWav.png',
        badge='NEW', version=VER,
        head='1-Click Karaoke Maker & 3-Stem WAV Recording.',
        sub=['Practice or stream even when you do not have an instrumental track,',
             'and export 3 WAV files at once every time you sing.'],
        bullets=['Subtracts center vocals while keeping bass and stereo width',
                 'Simultaneously saves Wet Vocal, Dry Vocal, and -14 LUFS Mix',
                 '"Save Last 30s" rescues your best take even if Record was off'],
    ),
    dict(
        file='X_EN_v5.4.0_6_ExpressionAndCueMonitor.png',
        badge='NEW v5.4', version=VER,
        head='Vowel Balance, Tail Vibrato & Cue Monitor.',
        sub=['High notes on "ee/eh" vowels often sound thin, and skull bone',
             'conduction makes your own voice sound muddy in headphones.'],
        bullets=['Balances vowel formants & adds natural 5.5Hz tail vibrato',
                 'Mix-Voice Bridge fills missing harmonics on high register breaks',
                 'Bone-Conduction Cue Monitor clarifies pitch in your cans only'],
    ),
]


if __name__ == '__main__':
    out_dir = os.path.join(ROOT, '記録', 'X素材_英語版')
    os.makedirs(out_dir, exist_ok=True)
    art_dir = r'C:\Users\silve\.gemini\antigravity\brain\8628be81-113b-4def-9d88-27b577e549be'
    for c in CARDS:
        out = os.path.join(out_dir, c['file'])
        make(c, out)
        if os.path.isdir(art_dir):
            make(c, os.path.join(art_dir, c['file']))
        print(f'{c["file"]}: {os.path.getsize(out) // 1024} KB')
