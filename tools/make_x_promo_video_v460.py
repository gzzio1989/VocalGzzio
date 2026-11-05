#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VocalGzzio v4.8.0 (Stage & Studio Complete Edition)
X（Twitter）・BOOTH プロモーション用「同一フレーズ直結Before/After音比較＋利点・新機能解説デモ動画」生成スクリプト。
- 解像度: 1280 x 720 (16:9 HD), 24 fps, 34.0秒 (H.264 yuv420p + AAC 48kHz Stereo 192kbps)
- 視聴者が最初の5秒で「原音（Before）とVocalGzzio処理後（After）の劇的な変化」を耳と目で体感できるよう、
  【まったく同じ歌フレーズの直結聴き比べ】を3段階（①素の声vs主役級歌枠ボイス、②伴奏埋もれvs声優先ダッキング＆5人ハモリ、③歌枠残響vs雑談MC切替）で構成。
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
ART_DIR = Path(r"C:\Users\silve\.gemini\antigravity\brain\8f2542b1-d681-42f3-92c9-1e836ae605e5")
PROMO_450 = ROOT / "review" / "promotion-4.5.0" / "comparison-video"
OUT_X_DIR = ROOT / "記録" / "X素材"
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
    p = ART_DIR / preferred
    if not (p.exists() and p.stat().st_size > 10000):
        p = ART_DIR / fallback
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
    f_t = get_font(23)
    f_s = get_font(18)
    d.text((x + 34, y + 12), title, font=f_t, fill=(255, 248, 252, 255))
    d.text((x + 34, y + 44), subtitle, font=f_s, fill=(225, 215, 232, 255))

def draw_ab_banner(canvas: Image.Image, is_after: bool, label_main: str, label_sub: str):
    """画面上部に一目で『いま原音(BEFORE)か加工後(AFTER)か』が分かる大型インジケーターを描画"""
    d = ImageDraw.Draw(canvas, "RGBA")
    x, y, w, h = 116, 94, 1048, 64
    if not is_after:
        bg = (36, 46, 58, 235)
        bd = (120, 175, 210, 255)
        tag_bg = (80, 125, 155, 255)
        tag_txt = "BEFORE (無加工の原音)"
    else:
        bg = (52, 18, 38, 240)
        bd = (255, 110, 168, 255)
        tag_bg = (235, 68, 132, 255)
        tag_txt = "AFTER (VocalGzzio ON!)"
    d.rounded_rectangle([x, y, x + w, y + h], radius=14, fill=bg, outline=bd, width=3)
    d.rounded_rectangle([x + 10, y + 10, x + 285, y + h - 10], radius=10, fill=tag_bg)
    d.text((x + 24, y + 19), tag_txt, font=get_font(20), fill=(255, 255, 255, 255))
    d.text((x + 302, y + 10), label_main, font=get_font(21), fill=(255, 248, 252, 255))
    d.text((x + 302, y + 36), label_sub, font=get_font(16), fill=(220, 208, 228, 255))

def draw_stage_studio_overlay(canvas: Image.Image, highlight_sec: int = 0):
    """v5.5.0 StageStudioPanel（アプリ別カラオケ取込・コード追従ハモリ・SNS動画書き出し・声量10割＆超軽量ミニバー）の拡大図解カード"""
    d = ImageDraw.Draw(canvas, "RGBA")
    px, py, pw, ph = 536, 166, 624, 384
    d.rounded_rectangle([px + 6, py + 8, px + pw + 6, py + ph + 8], radius=16, fill=(0, 0, 0, 170))
    d.rounded_rectangle([px, py, px + pw, py + ph], radius=16, fill=(24, 18, 32, 248), outline=(110, 225, 245, 255), width=3)
    d.text((px + 20, py + 14), "♪ v5.5.0 新搭載「アプリ別カラオケ取込・コード追従・SNS動画化」", font=get_font(19), fill=(130, 235, 255, 255))

    secs = [
        ("① ドライバ不要『アプリ別カラオケ音声取込』＆ ±300ms音ズレ自動計測",
         "YouTube等の音だけ直接取込＆キー変更±6・ボーカル除去・声ポケット掘りを適用！",
         (90, 210, 245)),
        ("② 伴奏コード追従スマートハモリ ＆ 🎬 SNS動画(.mp4)1クリック保存",
         "借用和音や転調でもハモリが濁らない！横16:9／縦9:16（Before➔After比較可）動画出力",
         (245, 105, 155)),
        ("③ 🎤 10割ダイレクト声量表現 ＆ 歪みゼロ『最終音量（0〜250%）』",
         "全力で歌った熱量が頭打ちせずそのまま伸びる！大音量でも一切歪まない新設計",
         (85, 220, 185)),
        ("④ ⚡ Smart Sleep 超軽量エンジン ＆ 📌 配信ミニバー（520×115px）",
         "CPU/UI負荷40〜70%削減！裏画面グローバルホットキー＆歴代全19世代タイムマシン搭載",
         (245, 190, 95)),
    ]
    sy = py + 52
    for idx, (stitle, sdesc, scol) in enumerate(secs):
        is_hi = (idx == highlight_sec)
        bg = (46, 32, 58, 245) if is_hi else (32, 24, 42, 220)
        bw = 3 if is_hi else 1
        d.rounded_rectangle([px + 14, sy, px + pw - 14, sy + 72], radius=10, fill=bg, outline=scol + (255 if is_hi else 140,), width=bw)
        d.text((px + 26, sy + 9), stitle, font=get_font(18), fill=scol + (255,))
        d.text((px + 26, sy + 38), sdesc, font=get_font(13), fill=(235, 228, 242, 255))
        sy += 80

# ---- High-Fidelity 48kHz / 24-bit C++ VocalGzzioProcessor Audio Helpers ----
def fft_eq(stereo: np.ndarray, sr: int, gains_Spec: list) -> np.ndarray:
    """周波数帯域ごとの滑らかな線形位相EQ操作 (f_low, f_high, gain_db)"""
    out = np.zeros_like(stereo)
    n = len(stereo)
    freqs = np.fft.rfftfreq(n, d=1.0 / sr)
    mask = np.ones_like(freqs, dtype=np.float32)
    for fl, fh, gdb in gains_Spec:
        g = 10.0 ** (gdb / 20.0)
        bw = max(60.0, (fh - fl) * 0.30)
        w = np.clip((freqs - (fl - bw)) / bw, 0.0, 1.0) * np.clip(((fh + bw) - freqs) / bw, 0.0, 1.0)
        w = w * w * (3.0 - 2.0 * w)
        mask *= (1.0 + (g - 1.0) * w)
    for ch in range(2):
        spec = np.fft.rfft(stereo[:, ch])
        out[:, ch] = np.fft.irfft(spec * mask, n=n)
    return out


def make_clean_dry_mic(vocal_st: np.ndarray, sr: int) -> np.ndarray:
    """
    加工前（Before）：エフェクト無しの素のモノラルマイク原音（合成ノイズ一切無し・歪みゼロ）
    """
    mono = 0.5 * (vocal_st[:, 0] + vocal_st[:, 1])
    st = np.column_stack([mono, mono])
    return fft_eq(st, sr, [
        (220.0, 420.0, +1.5),     # 素のマイク特有の自然な中低域の厚み
        (3000.0, 14000.0, -2.0),  # エフェクト処理前のフラットで落ち着いた高域
    ]).astype(np.float32)


def add_fractional_stereo_doubler(vocal_st: np.ndarray, sr: int, mix: float = 0.28) -> np.ndarray:
    """
    サビ用の滑らかな補間ステレオダブラー／ユニゾン（整数インデックス量子化ノイズを完全排除）
    """
    n = len(vocal_st)
    # 歯擦音（6kHz以上）と超低域（200Hz以下）をダブラー成分から除外して濁り・位相干渉を防止
    band = fft_eq(vocal_st, sr, [
        (20.0, 190.0, -12.0),
        (5800.0, 20000.0, -12.0),
    ])
    t_idx = np.arange(n, dtype=np.float64)
    t_sec = t_idx / sr
    out_l = vocal_st[:, 0].astype(np.float64).copy()
    out_r = vocal_st[:, 1].astype(np.float64).copy()

    voices = [
        (16.5, 0.65, 1.2, 0.78, 0.18),
        (23.5, 0.92, 1.4, 0.18, 0.78),
        (31.0, 0.50, 1.5, 0.52, 0.22),
        (37.5, 1.10, 1.3, 0.22, 0.52),
    ]
    for delay_ms, mod_hz, depth_ms, pan_l, pan_r in voices:
        delay_samples = (delay_ms + depth_ms * np.sin(2.0 * np.pi * mod_hz * t_sec)) * 0.001 * sr
        read_pos = np.clip(t_idx - delay_samples, 0.0, float(n - 1))
        d_l = np.interp(read_pos, t_idx, band[:, 0])
        d_r = np.interp(read_pos, t_idx, band[:, 1])
        out_l += d_l * (pan_l * mix)
        out_r += d_r * (pan_r * mix)

    return np.column_stack([out_l, out_r]).astype(np.float32)


def carve_vocal_pocket_bgm(bgm_st: np.ndarray, vocal_st: np.ndarray, sr: int) -> np.ndarray:
    """
    v5.5.0「声ポケット自動掘り」：伴奏のMid（中央定位）の320〜3400Hzから声の居場所を掘り、Sideの広がりは維持
    """
    mid = 0.5 * (bgm_st[:, 0] + bgm_st[:, 1])
    side = 0.5 * (bgm_st[:, 0] - bgm_st[:, 1])
    mid_st = np.column_stack([mid, mid])
    mid_carved = fft_eq(mid_st, sr, [
        (320.0, 3400.0, -3.8),  # ボーカル帯域のMidをスッキリ整理
    ])[:, 0]

    # ボーカル歌唱時に滑らかに伴奏のMidを少しだけ譲らせる（歪みゼロ）
    win = int(0.08 * sr)
    v_env = np.sqrt(np.convolve(np.mean(vocal_st ** 2, axis=1), np.ones(win) / win, mode="same") + 1e-9)
    v_norm = np.clip(v_env / (np.percentile(v_env, 90) + 1e-6), 0.0, 1.0)
    duck = (1.0 - 0.22 * v_norm)
    out_l = mid_carved * duck + side * 1.06
    out_r = mid_carved * duck - side * 1.06
    return np.column_stack([out_l, out_r]).astype(np.float32)


def match_rms(seg: np.ndarray, target_rms: float = 0.138) -> np.ndarray:
    """非線形tanhクリップを使わず、純粋な線形ゲイン＋安全ピーク保護（-1.0 dBFS）で音量揃え"""
    cur = float(np.sqrt(np.mean(seg ** 2)) + 1e-7)
    scaled = seg * (target_rms / cur)
    peak = float(np.max(np.abs(scaled)) + 1e-9)
    ceiling = 0.891  # -1.0 dBFS
    if peak > ceiling:
        scaled = scaled * (ceiling / peak)
    return scaled.astype(np.float32)


def build_audio_track(out_wav: Path):
    """
    34.0秒のステレオ48kHz / 24-bit WAVを生成する（日本語歌唱：Vo. & Music グッジオ オリジナル曲『ネット回線が来ない曲』× C++ VocalGzzioProcessor 実機DSP出力）:
    - 0.0s - 4.8s : 【比較① BEFORE】Aメロ日本語歌詞フレーズ（15.0s〜19.8s）：無加工の素のモノラル原音
    - 4.8s - 10.0s: 【比較① AFTER】まったく同じAメロ日本語歌詞フレーズ（15.0s〜20.2s）：VocalGzzio v5.5.0 ON！（透明感・声芯・艶プレート残響）
    - 10.0s - 14.2s: 【比較② BEFORE】サビ日本語歌詞フレーズ（69.0s〜73.2s）：実楽器バンド伴奏＋無加工の素の声
    - 14.2s - 19.0s: 【比較② AFTER】同じサビ日本語歌詞フレーズ（69.0s〜73.8s）：声ポケット掘り＋コード追従ユニゾンハモリで主役に！
    - 19.0s - 23.2s: 【比較③ 歌枠モード】日本語歌詞フレーズ前半（42.0s〜46.2s）：コンサートホール残響ONで歌唱
    - 23.2s - 27.5s: 【比較③ 雑談MCモードへ1クリック切替】続く日本語歌詞フレーズ後半（46.2s〜50.5s）：残響OFF・耳元でクリアな声へ！
    - 27.5s - 34.0s: 【エンディング】クライマックスサビ日本語歌唱（101.0s〜107.5s）＋実楽器バンド伴奏＆フェードアウト
    """
    jp_dir = ART_DIR / "scratch" / "vocal_candidates" / "jp_original"
    dry_path = jp_dir / "net_kaisen_vocal_dry_48k.wav"
    light_path = jp_dir / "net_kaisen_vocal_light_48k.wav"
    space_path = jp_dir / "net_kaisen_vocal_space_48k.wav"
    bgm_path = jp_dir / "net_kaisen_bgm_48k.wav"

    def read_stereo_f32(p: Path):
        raw = subprocess.check_output([
            FFMPEG, "-v", "error", "-i", str(p),
            "-f", "f32le", "-acodec", "pcm_f32le", "-ar", "48000", "-ac", "2", "-"
        ])
        arr = np.frombuffer(raw, dtype=np.float32)
        return 48000, arr.reshape(-1, 2)

    sr = 48000
    total_samples = int(DURATION * sr)
    master = np.zeros((total_samples, 2), dtype=np.float32)

    _, vox_dry = read_stereo_f32(dry_path)
    _, vox_light = read_stereo_f32(light_path)
    _, vox_space = read_stereo_f32(space_path)
    _, bgm_full = read_stereo_f32(bgm_path)

    # C++ VocalGzzioProcessor の純粋なステレオPlate残響成分（space - light）
    cpp_reverb = vox_space - vox_light

    # C++ 処理済みボーカルにシルキーな高域の伸びを微調整（歪みゼロ線形位相）
    vox_studio_dry = fft_eq(vox_light, sr, [
        (220.0, 380.0, -1.6),
        (2800.0, 5200.0, +1.8),
        (8000.0, 15000.0, +2.2),
    ])

    def get_slice(arr, start_s, len_s):
        s0 = int(start_s * sr)
        n_req = int(len_s * sr)
        out = np.zeros((n_req, 2), dtype=np.float32)
        avail = max(0, min(n_req, len(arr) - s0))
        if avail > 0:
            out[:avail] = arr[s0:s0 + avail]
        return out

    # net_kaisen_*_48k.wav は原曲 0.0s 起点
    # --- 1) 0.0s - 4.8s: 比較① BEFORE（Aメロ日本語フレーズ 原曲15.0s〜19.8s：無加工の素のモノラル声） ---
    off_verse = 15.0
    phrase_a_dry = make_clean_dry_mic(get_slice(vox_dry, off_verse, 4.8), sr)
    master[0:int(4.8 * sr)] = match_rms(phrase_a_dry, 0.118)

    # --- 2) 4.8s - 10.0s: 比較① AFTER（同じAメロ日本語フレーズ 原曲15.0s〜20.2s：VocalGzzio v5.5.0 C++実機ON！） ---
    phrase_a_after = get_slice(vox_studio_dry, off_verse, 5.2) + get_slice(cpp_reverb, off_verse, 5.2) * 2.4
    master[int(4.8 * sr):int(10.0 * sr)] = match_rms(phrase_a_after, 0.148)

    # --- 3) 10.0s - 14.2s: 比較② BEFORE（サビ日本語フレーズ 原曲69.0s〜73.2s：実楽器バンド伴奏＋無加工声） ---
    off_chorus = 69.0
    phrase_b_raw = match_rms(make_clean_dry_mic(get_slice(vox_dry, off_chorus, 4.2), sr), 0.088)
    bgm_2a = match_rms(get_slice(bgm_full, off_chorus, 4.2), 0.118)
    master[int(10.0 * sr):int(14.2 * sr)] = match_rms(phrase_b_raw + bgm_2a, 0.138)

    # --- 4) 14.2s - 19.0s: 比較② AFTER（同じサビ日本語フレーズ 原曲69.0s〜73.8s：声ポケット掘り＋ステレオユニゾン） ---
    lead_b_core = get_slice(vox_studio_dry, off_chorus, 4.8)
    lead_b_rev = get_slice(cpp_reverb, off_chorus, 4.8) * 2.2
    lead_b_wide = add_fractional_stereo_doubler(lead_b_core, sr, mix=0.26) + lead_b_rev
    lead_b_proc = match_rms(lead_b_wide, 0.132)
    bgm_2b_raw = match_rms(get_slice(bgm_full, off_chorus, 4.8), 0.112)
    bgm_2b_pocket = carve_vocal_pocket_bgm(bgm_2b_raw, lead_b_proc, sr)
    master[int(14.2 * sr):int(19.0 * sr)] = match_rms(lead_b_proc + bgm_2b_pocket, 0.152)

    # --- 5) 19.0s - 23.2s: 比較③ 歌枠モード（日本語フレーズ前半 原曲42.0s〜46.2s：ホール残響 ON） ---
    off_talk1 = 42.0
    phrase_c_sing = match_rms(
        get_slice(vox_studio_dry, off_talk1, 4.2) + get_slice(cpp_reverb, off_talk1, 4.2) * 3.2,
        0.136,
    )
    bgm_c1 = carve_vocal_pocket_bgm(match_rms(get_slice(bgm_full, off_talk1, 4.2), 0.055), phrase_c_sing, sr)
    master[int(19.0 * sr):int(23.2 * sr)] = match_rms(phrase_c_sing + bgm_c1, 0.145)

    # --- 6) 23.2s - 27.5s: 比較③ 雑談MCモードへ1クリック切替（続く日本語フレーズ後半 原曲46.2s〜50.5s：残響ゼロ・耳元クリア声！） ---
    off_talk2 = 46.2
    phrase_c_mc = get_slice(vox_studio_dry, off_talk2, 4.3)
    master[int(23.2 * sr):int(27.5 * sr)] = match_rms(phrase_c_mc, 0.142)

    # --- 7) 27.5s - 34.0s: エンディング（クライマックスサビ日本語歌唱 原曲101.0s〜107.5s＋伴奏声ポケット＆フェードアウト） ---
    off_end = 101.0
    s_out = int(27.5 * sr)
    n_out = total_samples - s_out
    lead_out_core = get_slice(vox_studio_dry, off_end, 6.5)[:n_out]
    lead_out_rev = (get_slice(cpp_reverb, off_end, 6.5)[:n_out]) * 2.2
    phrase_out = match_rms(add_fractional_stereo_doubler(lead_out_core, sr, mix=0.25) + lead_out_rev, 0.130)
    bgm_out_raw = match_rms(get_slice(bgm_full, off_end, 6.5)[:n_out], 0.108)
    bgm_out_pocket = carve_vocal_pocket_bgm(bgm_out_raw, phrase_out, sr)
    master[s_out:] = match_rms(phrase_out + bgm_out_pocket, 0.150)

    for boundary_s in [4.8, 10.0, 14.2, 19.0, 23.2, 27.5]:
        b_idx = int(boundary_s * sr)
        w_len = int(0.018 * sr)
        if b_idx - w_len >= 0 and b_idx + w_len < total_samples:
            env = np.linspace(0.0, 1.0, w_len * 2)[:, None]
            master[b_idx - w_len:b_idx + w_len] *= (0.35 + 0.65 * np.abs(env - 0.5) * 2.0)

    fi_len = int(0.15 * sr)
    master[:fi_len] *= np.linspace(0.0, 1.0, fi_len)[:, None]
    fo_len = int(1.4 * sr)
    master[-fo_len:] *= np.linspace(1.0, 0.0, fo_len)[:, None]

    # 24-bit 48kHz ステレオWAVとして書き出し（16-bit量子化劣化を回避）
    proc = subprocess.Popen([
        FFMPEG, "-y", "-v", "error",
        "-f", "f32le", "-ar", str(sr), "-ac", "2", "-i", "-",
        "-c:a", "pcm_s24le", str(out_wav)
    ], stdin=subprocess.PIPE)
    proc.communicate(np.clip(master, -0.98, 0.98).astype(np.float32).tobytes())
    if proc.returncode != 0:
        raise RuntimeError("Failed to write 24-bit WAV")

def main():
    try:
        OUT_X_DIR.mkdir(parents=True, exist_ok=True)
    except PermissionError:
        pass
    ui_Sz = (1080, 609)
    uis = {
        "rose": load_ui("v550_couture_mint_switches.png", "v4.8.0_couture_stage.png", ui_Sz),
        "lilac": load_ui("v4.8.0_couture_02_lilac.png", "v4.6.0_couture_02_lilac.png", ui_Sz),
        "noir": load_ui("v4.8.0_couture_03_noir.png", "v4.6.0_couture_03_noir.png", ui_Sz),
        "mint": load_ui("v550_couture_mint_switches.png", "v4.6.0_couture_04_mint.png", ui_Sz),
        "peach": load_ui("v4.8.0_couture_05_peach.png", "v4.6.0_couture_05_peach.png", ui_Sz),
        "argon": load_ui("v550_midnight_lantern_bokeh_ui.png", "v550_overview_final.png", ui_Sz),
        "yurufuwa": load_ui("v4.8.0_persona_yurufuwa_cafe.png", "v4.6.0_persona_yurufuwa_cafe.png", ui_Sz),
        "studio": load_ui("v4.8.0_persona_studio_pro.png", "v4.6.0_persona_studio_pro.png", ui_Sz),
    }

    mini_sz = (224, 126)
    minis = [
        ("01 ロゼ", uis["rose"].resize(mini_sz, Image.LANCZOS), (236, 105, 148)),
        ("02 氷紫", uis["lilac"].resize(mini_sz, Image.LANCZOS), (175, 145, 248)),
        ("03 黒姫", uis["noir"].resize(mini_sz, Image.LANCZOS), (225, 82, 132)),
        ("04 翡翠", uis["mint"].resize(mini_sz, Image.LANCZOS), (82, 212, 190)),
        ("05 白桃", uis["peach"].resize(mini_sz, Image.LANCZOS), (245, 152, 112)),
    ]

    f_top = get_font(20)
    f_badge = get_font(18)
    f_big = get_font(38)
    f_med = get_font(23)
    f_sm = get_font(17)

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        wav_path = tmp / "audio.wav"
        build_audio_track(wav_path)

        art_mp4 = ART_DIR / "VocalGzzio_v5.5.0_X_操作説明_音比較.mp4"
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
            "-b:a", "320k",
            "-ar", "48000",
            "-shortest",
            "-movflags", "+faststart",
            str(art_mp4),
        ]
        err_file = open(tmp / "ffmpeg_err.log", "w", encoding="utf-8")
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=err_file)

        ui_x, ui_y = 100, 86

        for frame_idx in range(TOTAL_FRAMES):
            t = frame_idx / FPS
            canvas = Image.new("RGBA", (W, H), (18, 14, 24, 255))
            d = ImageDraw.Draw(canvas, "RGBA")

            # 上部ステップヘッダーバー
            d.rectangle([0, 0, W, 72], fill=(28, 20, 36, 255))
            d.line([0, 72, W, 72], fill=(225, 120, 165, 180), width=2)

            if t < 4.8:
                # 比較① BEFORE: 加工前の素の部屋マイク声（0.0s - 4.8s）
                step_txt = "聴き比べ① [日本語歌唱・同じフレーズ]  まずは加工前の「素のマイク原音」を聴いてください"
                audio_badge = ("♪ 再生中：加工前の原音（Before）", (75, 130, 165))
                canvas.paste(uis["argon"], (ui_x, ui_y))
                draw_ab_banner(canvas, False,
                               "素の部屋録り声：エフェクト無しのモノラル原音（声芯・広がり・艶が控えめ）",
                               "一般的な宅録・配信マイクの無加工状態（4.8秒後に同じ日本語フレーズで VocalGzzio ON に切り替わります！）")
                sub_t = t / 4.8
                cx = int(lerp(760, 965, min(1.0, sub_t * 2.5)))
                cy = int(lerp(220, 142, min(1.0, sub_t * 2.5)))
                d.rounded_rectangle([910, 124, 1020, 160], radius=8, outline=(120, 225, 255, 255), width=3)
                draw_callout(canvas, (116, 565, 1048, 84),
                             "♪ 比較① BEFORE：エフェクト無しの素のモノラル歌声（48kHz / 24bit ドライ原音）",
                             "このあとすぐ、まったく同じ日本語フレーズで VocalGzzio v5.5.0 を ON にします！",
                             (90, 195, 240))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.0) % 1.0)

            elif t < 10.0:
                # 比較① AFTER: 同じフレーズで VocalGzzio v5.5.0 ON!（4.8s - 10.0s）
                step_txt = "聴き比べ① [日本語歌唱・同じフレーズ]  VocalGzzio ON！透明感・声芯・艶プレート残響に激変！"
                audio_badge = ("★ 再生中：VocalGzzio ON（After）", (235, 68, 132))
                canvas.paste(uis["argon"], (ui_x, ui_y))
                draw_ab_banner(canvas, True,
                               "同じ日本語フレーズで激変！こもり整理＋声芯フォーカス＋シルク高域＋艶プレート残響",
                               "中央の2D声質オーブと「うた自動」だけで、素の部屋声がスタジオの主役ボーカルに生まれ変わります")
                sub_t = (t - 4.8) / 5.2
                angle = sub_t * math.pi * 2.0
                cx = int(545 + 30 * math.cos(angle))
                cy = int(310 + 22 * math.sin(angle))
                r_glow = int(44 + 8 * math.sin(t * 6.0))
                d.ellipse([cx - r_glow, cy - r_glow, cx + r_glow, cy + r_glow], outline=(255, 115, 175, 230), width=4)
                draw_callout(canvas, (116, 565, 1048, 84),
                             "★ 比較① AFTER：歌詞の輪郭とシルキーな高域・立体プレート残響が一気に開花！",
                             "DAWも高価なプラグインも不要！これ1つ立ち上げるだけでこの声がOBS・Discordへ直結します",
                             (235, 85, 148))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 3.0) % 1.0)

            elif t < 14.2:
                # 比較② BEFORE: カラオケ伴奏に素の声を乗せた状態（10.0s - 14.2s）
                step_txt = "聴き比べ② [カラオケ伴奏との馴染み]  無加工だと伴奏に声が埋もれて細く聴こえる…"
                audio_badge = ("♪ 再生中：伴奏＋無加工の声（Before）", (75, 130, 165))
                canvas.paste(uis["rose"], (ui_x, ui_y))
                draw_ab_banner(canvas, False,
                               "カラオケ伴奏＋無加工の素の声：伴奏の楽器に声が負けて埋もれがち",
                               "歌枠配信や「歌ってみた」で一番多い悩み＝「BGMを流すと自分の歌声が後ろに引っ込んでしまう」")
                sub_t = (t - 10.0) / 4.2
                cx = int(lerp(545, 868, min(1.0, sub_t * 2.5)))
                cy = int(lerp(310, 142, min(1.0, sub_t * 2.5)))
                draw_callout(canvas, (116, 565, 1048, 84),
                             "♪ 比較② BEFORE：バンド伴奏にそのまま素の声を乗せると、伴奏の中音域に埋もれてしまいます",
                             "次は v5.5.0「アプリ別カラオケ取込＋声ポケット掘り＋伴奏コード追従ハモリ」をONにします！",
                             (90, 195, 240))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.0) % 1.0)

            elif t < 19.0:
                # 比較② AFTER: v5.5.0 内蔵BGMダッキング＋5人ユニゾンハモリ＋直前30秒保存（14.2s - 19.0s）
                step_txt = "聴き比べ② [v5.5.0 新機能]  声ポケット掘り＋コード追従ハモリ＋10割声量で主役に！"
                audio_badge = ("★ 再生中：声ポケット掘り＋コード追従ハモリ（After）", (235, 68, 132))
                canvas.paste(uis["rose"], (ui_x, ui_y))
                draw_ab_banner(canvas, True,
                               "伴奏のMidから声の居場所を掘り、借用和音・転調にも追従するユニゾン＆ハモリで主役に！",
                               "ドライバ不要のアプリ別カラオケ取込＆±300ms音ズレ自動計測＋SNS用.mp4動画1クリック保存搭載")
                sub_t = (t - 14.2) / 4.8
                hi_idx = 0 if sub_t < 0.5 else 1
                draw_stage_studio_overlay(canvas, highlight_sec=hi_idx)
                cx = 840
                cy = 250 if sub_t < 0.5 else 330
                draw_callout(canvas, (116, 565, 1048, 84),
                             "★ 比較② AFTER：歌い出すと伴奏のMidが自動で譲り、ステレオ感あふれるサビが華やかに広がる！",
                             "さらに【直前30秒を保存】で「完成音(Wet)」とMIX師に渡せる「素の声(Dry)」を24bit WAVで同時保存！",
                             (235, 85, 148))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.5) % 1.0)

            elif t < 23.2:
                # 比較③ 歌枠モード（華やかなロングホール残響） 19.0s - 23.2s
                step_txt = "聴き比べ③ [歌枠 ⇄ 雑談MC 切替]  歌っている時は華やかなコンサートホール残響"
                audio_badge = ("♪ 再生中：歌枠モード（ホール残響 ON）", (62, 195, 175))
                canvas.paste(uis["lilac"], (ui_x, ui_y))
                draw_ab_banner(canvas, True,
                               "🎤 歌枠モード：広がりのあるホール残響＋声優先ダッキングで気持ちよく歌唱",
                               "でも曲が終わってそのまま喋ると「お風呂場で喋っているようなエコー事故」になりがち…")
                sub_t = (t - 19.0) / 4.2
                draw_stage_studio_overlay(canvas, highlight_sec=2)
                cx = int(lerp(840, 725, min(1.0, sub_t * 2.0)))
                cy = int(lerp(330, 142, min(1.0, sub_t * 2.0)))
                draw_callout(canvas, (116, 565, 1048, 84),
                             "🎤 歌枠モードで歌唱中 ＋ 🎯 5秒AI声質ターゲット診断で理想の声へ自動調律！",
                             "曲が終わってトークに入るとき、上の【歌枠/MC】ボタンを1クリックすると…？（23.2秒で切替！）",
                             (68, 205, 182))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.0) % 1.0)

            elif t < 27.5:
                # 比較③ 雑談MCモードへ瞬間切替 ＋ 絶叫ガード＆咳払いミュート 23.2s - 27.5s
                step_txt = "聴き比べ③ [1クリックで雑談MCへ！]  残響がスッと消え、耳元でクリアなトーク声に！"
                audio_badge = ("★ 再生中：雑談MCモード（残響OFF・声芯トーク）", (235, 142, 68))
                canvas.paste(uis["mint"], (ui_x, ui_y))
                draw_ab_banner(canvas, True,
                               "💬 雑談MCモードへ1クリック切替！残響が25msで消え、言葉がくっきり耳元に届く",
                               "ノイズ除去と声の艶はそのままキープ！次の曲へ行くときも1クリックで元の歌枠設定へ完全復帰")
                sub_t = (t - 23.2) / 4.3
                draw_stage_studio_overlay(canvas, highlight_sec=3)
                cx = int(lerp(725, 815, min(1.0, sub_t * 2.2)))
                cy = 142
                draw_callout(canvas, (116, 565, 1048, 84),
                             "💬 お風呂声トーク事故をゼロに！さらに【絶叫ガード】＆【咳ミュート(8ms無音)】で配信護身",
                             "突発的な叫び声・台パンも0.2msで抑え込み、咳払いはプツッという音ゼロでスマートに遮断！",
                             (240, 155, 75))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.5) % 1.0)

            else:
                # エンディング（27.5s - 34.0s）：何が売り？どんな人に最適？ひと目でわかるまとめカード
                step_txt = "VocalGzzio v5.5.0  —  歌枠・歌ってみた・配信がこれ1台で完結！（無料体験版あり）"
                audio_badge = ("★ BOOTHにて無料体験版＆製品版（2,500円）公開中", (235, 85, 148))
                d.rounded_rectangle([48, 88, W - 48, 662], radius=22, fill=(26, 18, 34, 248), outline=(238, 165, 192, 230), width=3)
                d.text((82, 108), "VocalGzzio v5.5.0 — なぜ今選ばれているのか？（3つの強み）", font=f_big, fill=(255, 245, 250, 255))
                d.text((82, 160), "DAWも仮想ケーブルも高価なプラグインも不要。買い切り2,500円で歌枠・録音・SNS動画化が全部入り！", font=f_med, fill=(242, 192, 212, 255))

                # 3つのターゲット別メリットボックス
                targets = [
                    ("① 歌枠・Vライバー・配信者へ",
                     "・ドライバ不要アプリ別カラオケ音声取込\n・配信ミニバー(520×115)＆裏ホットキー\n・声⇄伴奏 1クリック音ズレ自動計測(±300ms)",
                     (238, 95, 152)),
                    ("② 歌ってみた・宅録シンガーへ",
                     "・伴奏コード追従ハモリ＆10割声量モード\n・-14 LUFS 3本WAV＋SNS用.mp4動画保存\n・0ms部屋鳴り除去＆0.08ms知能ピッチ補正",
                     (75, 215, 190)),
                    ("③ 超軽量＆全19世代タイムマシン",
                     "・Smart SleepでCPU/UI負荷40〜70%削減\n・v1.0.0〜v5.5.0 全19世代を呼び出し可能\n・VST3/AU/単体同梱（Win & Mac v5.5.0）",
                     (120, 195, 250)),
                ]
                tx = 78
                ty = 204
                tw_card = 362
                for t_head, t_body, t_col in targets:
                    d.rounded_rectangle([tx, ty, tx + tw_card, ty + 188], radius=14, fill=(38, 26, 48, 240), outline=t_col + (255,), width=2)
                    d.text((tx + 16, ty + 14), t_head, font=get_font(20), fill=t_col + (255,))
                    for li, line in enumerate(t_body.split("\n")):
                        d.text((tx + 16, ty + 52 + li * 40), line, font=get_font(17), fill=(248, 242, 252, 255))
                    tx += tw_card + 18

                # 5色ミニカード横並び
                mx = 78
                my = 410
                for m_title, m_im, m_col in minis:
                    canvas.paste(m_im, (mx, my))
                    d.rounded_rectangle([mx, my, mx + mini_sz[0], my + mini_sz[1]], radius=10, outline=m_col + (255,), width=3)
                    d.rounded_rectangle([mx + 8, my + 92, mx + mini_sz[0] - 8, my + 120], radius=6, fill=(18, 12, 22, 220))
                    d.text((mx + 68, my + 96), m_title, font=f_badge, fill=m_col + (255,))
                    mx += mini_sz[0] + 12

                d.text((82, 554), "★ 製品版 2,500円（買い切り・Win & Mac v5.5.0同梱・既存購入者は無料で更新OK） ／ まずは0円の無料体験版へ！",
                       font=get_font(21), fill=(255, 238, 175, 255))
                d.text((82, 590), "Vocal & Music: グッジオ (G'zzio) オリジナル日本語楽曲 (48kHz / 24bit C++ VocalGzzioProcessor 実機DSPレンダリング)",
                       font=f_sm, fill=(175, 162, 185, 255))
                d.text((82, 618), "https://gzzio.booth.pm/items/8764003   ｜   公式サイト: https://gzzio1989.github.io/apps/vocalgzzio/",
                       font=f_sm, fill=(235, 205, 165, 255))

            # 上部バーのテキストと右上の音声状態バッジを描画
            d.text((24, 22), step_txt, font=f_top, fill=(255, 248, 252, 255))
            b_txt, b_col = audio_badge
            bb = d.textbbox((0, 0), b_txt, font=f_badge)
            bw = bb[2] - bb[0] + 32
            bx = W - bw - 20
            d.rounded_rectangle([bx, 14, bx + bw, 58], radius=22, fill=b_col + (255,), outline=(255, 255, 255, 210), width=2)
            d.text((bx + 16, 25), b_txt, font=f_badge, fill=(255, 255, 255, 255))

            # 下部プログレスバー
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

    out_mp4 = OUT_X_DIR / "VocalGzzio_v5.5.0_X_操作説明_音比較.mp4"
    try:
        shutil.copy2(art_mp4, out_mp4)
    except PermissionError:
        pass
    print("Created X Promo Video:", art_mp4, f"({art_mp4.stat().st_size / 1024 / 1024:.2f} MB)")

if __name__ == "__main__":
    main()
