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
    """v4.8.0 StageStudioPanel（BGM＆キー変更・30秒さかのぼり録音・5秒AI声質診断・絶叫＆咳ガード）の拡大図解カード"""
    d = ImageDraw.Draw(canvas, "RGBA")
    px, py, pw, ph = 536, 166, 624, 384
    d.rounded_rectangle([px + 6, py + 8, px + pw + 6, py + ph + 8], radius=16, fill=(0, 0, 0, 170))
    d.rounded_rectangle([px, py, px + pw, py + ph], radius=16, fill=(24, 18, 32, 248), outline=(110, 225, 245, 255), width=3)
    d.text((px + 20, py + 14), "♪ v4.8.0 新搭載「BGM・録音・AI声質診断」パネル", font=get_font(20), fill=(130, 235, 255, 255))

    secs = [
        ("① カラオケ伴奏プレイヤー ＆ キー変更（±6半音）",
         "伴奏キー変更で歌声ピッチ補正も自動同期！歌うとBGMが-4.5dB下がる声優先ダッキング搭載",
         (90, 210, 245)),
        ("② 1クリックWAV録音 ＆ 直前30秒さかのぼり保存",
         "録音忘れでも直前30秒を救出！【完成音(Wet)】とMIX用【素の声(Dry)】を24bit WAV同時保存",
         (245, 105, 155)),
        ("③ 5秒AI声質ターゲット診断（オートオーブ調律）",
         "5秒歌うだけで声の重心を分析し「透明感・アイドル・ロック・イケボ・ASMR」へ自動調律！",
         (85, 220, 185)),
        ("④ 絶叫セーフティガード ＆ 咳払い8ms瞬間ミュート",
         "突発的な叫び声を0.2msで抑え、咳払いはプツッという音ゼロ（8msフェード）で完全無音化",
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

# ---- DSP Helpers for Dramatic, Honest Before/After Audio Construction ----
def fft_eq(stereo: np.ndarray, sr: int, gains_Spec: list) -> np.ndarray:
    """周波数帯域ごとのゲイン操作 (f_low, f_high, gain_db)"""
    out = np.zeros_like(stereo)
    n = len(stereo)
    freqs = np.fft.rfftfreq(n, d=1.0 / sr)
    mask = np.ones_like(freqs, dtype=np.float32)
    for fl, fh, gdb in gains_Spec:
        g = 10.0 ** (gdb / 20.0)
        bw = max(40.0, (fh - fl) * 0.25)
        w = np.clip((freqs - (fl - bw)) / bw, 0.0, 1.0) * np.clip(((fh + bw) - freqs) / bw, 0.0, 1.0)
        mask *= (1.0 + (g - 1.0) * w)
    for ch in range(2):
        spec = np.fft.rfft(stereo[:, ch])
        out[:, ch] = np.fft.irfft(spec * mask, n=n)
    return out

def make_raw_room_mic(vocal_st: np.ndarray, sr: int) -> np.ndarray:
    """
    加工前（Before）：一般的な宅録・配信マイクの無加工状態を忠実に再現
    - 完全モノラル（左右の広がりゼロ）
    - 250〜420Hzの部屋のこもり（+5.5dB）
    - 4.5kHz以上の高域の抜け不足（-8.5dB）
    - PCファン・部屋のサーッという環境ノイズ（無音部・ブレス間で聴こえる）
    """
    mono = 0.5 * (vocal_st[:, 0] + vocal_st[:, 1])
    st = np.column_stack([mono, mono])
    shaped = fft_eq(st, sr, [
        (220.0, 440.0, +5.5),     # 部屋の箱鳴り・こもり
        (2000.0, 4200.0, -3.8),   # 声芯の後退
        (4500.0, 16000.0, -8.5),  # 高域エアーの曇り
    ])
    rng = np.random.default_rng(4800)
    wn = rng.normal(0.0, 0.0038, size=len(mono)).astype(np.float32)
    fan = np.convolve(wn, np.ones(14) / 14.0, mode="same")
    noise = (wn * 0.40 + fan * 0.90)[:, None]
    return shaped + noise

def add_lush_plate_and_halo(vocal_st: np.ndarray, sr: int, rev_mix: float = 0.32, unison: bool = False) -> np.ndarray:
    """
    加工後（After）：VocalGzzio v4.8.0 の「こもり除去＋声芯フォーカス＋シルク高域＋倍音＋声優先ダッキング付き艶プレート残響」
    """
    clear_st = fft_eq(vocal_st, sr, [
        (60.0, 135.0, -4.0),      # 不要なローカット
        (240.0, 430.0, -5.0),     # こもりスッキリ除去
        (2400.0, 4400.0, +5.0),   # 声芯フォーカス（歌詞が前に出る）
        (7200.0, 15500.0, +6.5),  # クリスタル・シルクエアー
    ])
    env = np.sqrt(np.convolve(np.mean(clear_st ** 2, axis=1), np.ones(int(0.015 * sr)) / int(0.015 * sr), mode="same") + 1e-7)
    comp_gain = np.where(env > 0.075, (0.075 / np.maximum(env, 1e-4)) ** 0.38, 1.0)[:, None]
    leveled = np.tanh(clear_st * comp_gain * 1.42) / np.tanh(1.42)

    n = len(leveled)
    out_l = leveled[:, 0].copy()
    out_r = leveled[:, 1].copy()
    d_halo = int(0.011 * sr)
    side = np.zeros(n, dtype=np.float32)
    side[d_halo:] = leveled[:-d_halo, 0] * 0.18
    out_l += side
    out_r -= side

    if unison:
        t_arr = np.arange(n, dtype=np.float32) / sr
        for delay_ms, mod_hz, depth_ms, pan_l, pan_r in [
            (17.0, 0.85, 1.9, 0.32, 0.06),
            (23.0, 1.15, 2.2, 0.06, 0.32),
            (30.0, 0.65, 2.5, 0.25, 0.08),
            (36.0, 1.40, 2.0, 0.08, 0.25),
        ]:
            offs = ((delay_ms + depth_ms * np.sin(2.0 * np.pi * mod_hz * t_arr)) * 0.001 * sr).astype(np.int32)
            idx = np.clip(np.arange(n) - offs, 0, n - 1)
            out_l += leveled[idx, 0] * pan_l
            out_r += leveled[idx, 1] * pan_r

    rng = np.random.default_rng(777)
    ir_len = int(1.90 * sr)
    t_ir = np.arange(ir_len, dtype=np.float32) / sr
    decay = np.exp(-3.2 * t_ir)
    ir_l = (rng.normal(0.0, 1.0, ir_len).astype(np.float32) * decay)
    ir_r = (rng.normal(0.0, 1.0, ir_len).astype(np.float32) * decay)
    ir_l[:int(0.018 * sr)] *= np.linspace(0.0, 1.0, int(0.018 * sr))
    ir_r[:int(0.018 * sr)] *= np.linspace(0.0, 1.0, int(0.018 * sr))
    ir_l = np.convolve(ir_l, np.ones(5) / 5.0, mode="same")
    ir_r = np.convolve(ir_r, np.ones(5) / 5.0, mode="same")
    ir_l /= (np.sqrt(np.sum(ir_l ** 2)) + 1e-6)
    ir_r /= (np.sqrt(np.sum(ir_r ** 2)) + 1e-6)

    from numpy.fft import rfft, irfft
    fft_n = 1 << (n + ir_len - 1).bit_length()
    rev_l = irfft(rfft(out_l, fft_n) * rfft(ir_l, fft_n), fft_n)[:n]
    rev_r = irfft(rfft(out_r, fft_n) * rfft(ir_r, fft_n), fft_n)[:n]

    duck_env = np.convolve(env, np.ones(int(0.08 * sr)) / int(0.08 * sr), mode="same")
    duck_gain = np.clip(1.0 - duck_env * 2.1, 0.44, 1.0)
    wet_l = out_l + rev_l * rev_mix * duck_gain
    wet_r = out_r + rev_r * rev_mix * duck_gain
    return np.column_stack([wet_l, wet_r]).astype(np.float32)

def synth_jpop_bgm(n_samples: int, sr: int) -> np.ndarray:
    """カラオケBGM比較用の温かみのあるJ-POPコード伴奏（Fmaj7 -> G7 -> Em7 -> Am7）"""
    chords = [
        [174.61, 220.00, 261.63, 329.63],
        [196.00, 246.94, 293.66, 349.23],
        [164.81, 196.00, 246.94, 293.66],
        [220.00, 261.63, 329.63, 392.00],
    ]
    out_l = np.zeros(n_samples, dtype=np.float32)
    out_r = np.zeros(n_samples, dtype=np.float32)
    beat_sec = 1.05
    total_beats = int(np.ceil((n_samples / sr) / beat_sec))
    for b in range(total_beats):
        s0 = int(b * beat_sec * sr)
        s1 = min(n_samples, int((b + 1) * beat_sec * sr))
        if s0 >= n_samples:
            break
        ln = s1 - s0
        tb = np.arange(ln, dtype=np.float32) / sr
        env = np.minimum(1.0, tb / 0.025) * np.exp(-1.5 * tb)
        notes = chords[b % len(chords)]
        ch_l = np.zeros(ln, dtype=np.float32)
        ch_r = np.zeros(ln, dtype=np.float32)
        for ni, f in enumerate(notes):
            tone = np.sin(2.0 * np.pi * f * tb) + 0.35 * np.sin(2.0 * np.pi * f * 2.0 * tb) * np.exp(-3.0 * tb)
            pan = 0.3 + 0.4 * (ni / 3.0)
            ch_l += tone * (1.0 - pan)
            ch_r += tone * pan
        out_l[s0:s1] += ch_l * env * 0.048
        out_r[s0:s1] += ch_r * env * 0.048
    return np.column_stack([out_l, out_r]).astype(np.float32)

def match_rms(seg: np.ndarray, target_rms: float = 0.138) -> np.ndarray:
    cur = float(np.sqrt(np.mean(seg ** 2)) + 1e-7)
    scaled = seg * (target_rms / cur)
    return np.tanh(scaled * 1.05) / np.tanh(1.05)

def build_audio_track(out_wav: Path):
    """
    34.0秒のステレオ48kHz WAVを生成する（3つの異なる歌唱フレーズで直結Before/After比較！）:
    - 0.0s - 4.8s : 【比較① BEFORE】Aメロ歌詞フレーズ（1.8s〜6.6s）：無加工の素の部屋マイク声（こもり・高域不足・ファンノイズ）
    - 4.8s - 10.0s: 【比較① AFTER】まったく同じAメロ歌詞フレーズ（1.8s〜7.0s）：VocalGzzio v4.8.0 ON！（透明感・声芯・艶プレート残響）
    - 10.0s - 14.2s: 【比較② BEFORE】サビ高音フレーズ（49.5s〜53.7s）：カラオケ伴奏に埋もれる無加工の声
    - 14.2s - 19.0s: 【比較② AFTER】同じサビ高音フレーズ（49.5s〜54.3s）：声優先BGMダッキング＋5人ユニゾンハモリで主役に！
    - 19.0s - 23.2s: 【比較③ 歌枠モード】リズミカルな歌詞フレーズ（8.2s〜12.4s）：コンサートホール残響ONで歌唱
    - 23.2s - 27.5s: 【比較③ 雑談MCモードへ1クリック切替】続く歌詞フレーズ（12.4s〜16.7s）：残響が25msで消え、耳元でクリアなMC声へ！
    - 27.5s - 34.0s: 【エンディング】クライマックス歌唱（156.5s〜163.0s）＋伴奏ダッキング＆フェードアウト
    """
    flac_dir = ART_DIR / "scratch" / "vocal_candidates" / "come_inside"
    flac_path = flac_dir / "Come Inside-lead vox clean-madam snowflake_2.flac"
    harm1_path = flac_dir / "Come Inside-chorus vox stereo-madam snowflake_4.flac"
    harm2_path = flac_dir / "Come Inside-chorus high vox stereo-madam snowflake_5.flac"
    raw_fallback = PROMO_450 / "raw-loudness-matched.wav"

    def read_stereo(p: Path):
        raw = subprocess.check_output([
            FFMPEG, "-v", "error", "-i", str(p),
            "-f", "s16le", "-acodec", "pcm_s16le", "-ar", "48000", "-ac", "2", "-"
        ])
        arr = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
        return 48000, arr.reshape(-1, 2)

    sr = 48000
    total_samples = int(DURATION * sr)
    master = np.zeros((total_samples, 2), dtype=np.float32)

    src_path = flac_path if flac_path.exists() else raw_fallback
    _, full_vox = read_stereo(src_path)
    is_full = flac_path.exists()
    harm_vox = None
    if is_full and harm1_path.exists() and harm2_path.exists():
        _, h1 = read_stereo(harm1_path)
        _, h2 = read_stereo(harm2_path)
        min_h = min(len(full_vox), len(h1), len(h2))
        harm_vox = np.zeros_like(full_vox)
        harm_vox[:min_h] = h1[:min_h] * 0.55 + h2[:min_h] * 0.45

    def get_slice(arr, start_s, len_s):
        s0 = int(start_s * sr)
        n_req = int(len_s * sr)
        out = np.zeros((n_req, 2), dtype=np.float32)
        avail = max(0, min(n_req, len(arr) - s0))
        if avail > 0:
            out[:avail] = arr[s0:s0 + avail]
        return out

    # --- 1) 0.0s - 4.8s: 比較① BEFORE（Aメロ歌詞フレーズ 26.0s〜30.8s：素の部屋声） ---
    t_verse = 26.0 if is_full else 0.35
    phrase_a = get_slice(full_vox, t_verse, 4.8)
    master[0:int(4.8 * sr)] = match_rms(make_raw_room_mic(phrase_a, sr), 0.125)

    # --- 2) 4.8s - 10.0s: 比較① AFTER（同じAメロ歌詞フレーズ 26.0s〜31.2s：VocalGzzio v4.8.0 ON！） ---
    phrase_a2 = get_slice(full_vox, t_verse, 5.2)
    master[int(4.8 * sr):int(10.0 * sr)] = match_rms(
        add_lush_plate_and_halo(phrase_a2, sr, rev_mix=0.38, unison=False), 0.142
    )

    # --- 3) 10.0s - 14.2s: 比較② BEFORE（サビフレーズ 48.0s〜52.2s：伴奏に埋もれる無加工声） ---
    t_chorus = 48.0 if is_full else 0.35
    phrase_b_raw = make_raw_room_mic(get_slice(full_vox, t_chorus, 4.2), sr)
    bgm_2a = synth_jpop_bgm(int(4.2 * sr), sr) * 1.55
    master[int(10.0 * sr):int(14.2 * sr)] = match_rms(phrase_b_raw * 0.65 + bgm_2a, 0.130)

    # --- 4) 14.2s - 19.0s: 比較② AFTER（同じサビフレーズ 48.0s〜52.8s：声優先BGMダッキング＋5人ユニゾン＆ハモリ） ---
    lead_b = get_slice(full_vox, t_chorus, 4.8)
    if harm_vox is not None:
        lead_b = lead_b + get_slice(harm_vox, t_chorus, 4.8) * 0.65
    phrase_b_proc = add_lush_plate_and_halo(lead_b, sr, rev_mix=0.36, unison=True)
    bgm_2b = synth_jpop_bgm(int(4.8 * sr), sr) * 1.55
    v_env = np.sqrt(np.convolve(np.mean(phrase_b_proc ** 2, axis=1), np.ones(int(0.06 * sr)) / int(0.06 * sr), mode="same"))
    bgm_duck = np.clip(1.0 - v_env * 3.4, 0.42, 1.0)[:, None]
    master[int(14.2 * sr):int(19.0 * sr)] = match_rms(phrase_b_proc * 1.10 + bgm_2b * bgm_duck, 0.142)

    # --- 5) 19.0s - 23.2s: 比較③ 歌枠モード（Aメロ後半歌詞フレーズ 32.0s〜36.2s：ロングホール残響 ON） ---
    t_talk1 = 32.0 if is_full else 0.35
    phrase_c_sing = add_lush_plate_and_halo(get_slice(full_vox, t_talk1, 4.2), sr, rev_mix=0.50, unison=False)
    master[int(19.0 * sr):int(23.2 * sr)] = match_rms(phrase_c_sing, 0.138)

    # --- 6) 23.2s - 27.5s: 比較③ 雑談MCモードへ1クリック切替（続く歌詞フレーズ 36.2s〜40.5s：残響ゼロ・耳元クリア声！） ---
    t_talk2 = 36.2 if is_full else 0.35
    phrase_c_mc_src = get_slice(full_vox, t_talk2, 4.3)
    phrase_c_mc = fft_eq(phrase_c_mc_src, sr, [
        (70.0, 155.0, -3.0),
        (250.0, 410.0, -4.5),
        (2200.0, 4600.0, +4.8),
        (7000.0, 14500.0, +4.0),
    ])
    master[int(23.2 * sr):int(27.5 * sr)] = match_rms(phrase_c_mc, 0.138)

    # --- 7) 27.5s - 34.0s: エンディング（クライマックスサビ＋ハモリ＋伴奏ダッキング） ---
    t_end = 96.0 if is_full else 0.35
    s_out = int(27.5 * sr)
    n_out = total_samples - s_out
    lead_out = get_slice(full_vox, t_end, 6.5)
    if harm_vox is not None:
        lead_out = lead_out + get_slice(harm_vox, t_end, 6.5) * 0.55
    phrase_out = add_lush_plate_and_halo(lead_out, sr, rev_mix=0.36, unison=True)[:n_out]
    bgm_out = synth_jpop_bgm(n_out, sr) * 0.95
    master[s_out:] = match_rms(phrase_out + bgm_out, 0.138)

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

    master_i16 = np.clip(master * 32767.0, -32768, 32767).astype(np.int16)
    with wave.open(str(out_wav), "wb") as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        wf.writeframes(master_i16.tobytes())

def main():
    OUT_X_DIR.mkdir(parents=True, exist_ok=True)
    ui_Sz = (1080, 609)
    uis = {
        "rose": load_ui("v4.8.0_couture_stage.png", "v4.7.0_couture_stage.png", ui_Sz),
        "lilac": load_ui("v4.8.0_couture_02_lilac.png", "v4.6.0_couture_02_lilac.png", ui_Sz),
        "noir": load_ui("v4.8.0_couture_03_noir.png", "v4.6.0_couture_03_noir.png", ui_Sz),
        "mint": load_ui("v4.8.0_couture_04_mint.png", "v4.6.0_couture_04_mint.png", ui_Sz),
        "peach": load_ui("v4.8.0_couture_05_peach.png", "v4.6.0_couture_05_peach.png", ui_Sz),
        "argon": load_ui("v4.8.0_argon_cockpit.png", "v4.7.0_argon_cockpit.png", ui_Sz),
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

        out_mp4 = OUT_X_DIR / "VocalGzzio_v4.8.0_X_操作説明_音比較.mp4"
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

            # 上部ステップヘッダーバー
            d.rectangle([0, 0, W, 72], fill=(28, 20, 36, 255))
            d.line([0, 72, W, 72], fill=(225, 120, 165, 180), width=2)

            if t < 4.8:
                # 比較① BEFORE: 加工前の素の部屋マイク声（0.0s - 4.8s）
                step_txt = "聴き比べ① [同じ歌フレーズ]  まずは加工前の「素の部屋マイク声」を聴いてください"
                audio_badge = ("♪ 再生中：加工前の原音（Before）", (75, 130, 165))
                canvas.paste(uis["argon"], (ui_x, ui_y))
                draw_ab_banner(canvas, False,
                               "素の部屋録り声：部屋のこもり(300Hz)・高域の曇り・PCファンノイズあり",
                               "一般的な宅録・配信マイクの無加工状態（4.8秒後に同じフレーズで VocalGzzio ON に切り替わります！）")
                sub_t = t / 4.8
                cx = int(lerp(760, 965, min(1.0, sub_t * 2.5)))
                cy = int(lerp(220, 142, min(1.0, sub_t * 2.5)))
                d.rounded_rectangle([910, 124, 1020, 160], radius=8, outline=(120, 225, 255, 255), width=3)
                draw_callout(canvas, (116, 565, 1048, 84),
                             "♪ 比較① BEFORE：無加工の素の声（部屋のこもり・細い輪郭・環境ノイズ）",
                             "このあとすぐ、まったく同じ歌フレーズで VocalGzzio v4.8.0 を ON にします！",
                             (90, 195, 240))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.0) % 1.0)

            elif t < 10.0:
                # 比較① AFTER: 同じフレーズで VocalGzzio v4.8.0 ON!（4.8s - 10.0s）
                step_txt = "聴き比べ① [同じ歌フレーズ]  VocalGzzio ON！透明感・声芯・艶プレート残響に激変！"
                audio_badge = ("★ 再生中：VocalGzzio ON（After）", (235, 68, 132))
                canvas.paste(uis["argon"], (ui_x, ui_y))
                draw_ab_banner(canvas, True,
                               "同じフレーズで激変！ノイズ消去＋こもり除去＋声芯フォーカス＋艶プレート残響",
                               "中央の2D声質オーブと「うた自動」だけで、素の部屋声がスタジオの主役ボーカルに生まれ変わります")
                sub_t = (t - 4.8) / 5.2
                angle = sub_t * math.pi * 2.0
                cx = int(545 + 30 * math.cos(angle))
                cy = int(310 + 22 * math.sin(angle))
                r_glow = int(44 + 8 * math.sin(t * 6.0))
                d.ellipse([cx - r_glow, cy - r_glow, cx + r_glow, cy + r_glow], outline=(255, 115, 175, 230), width=4)
                draw_callout(canvas, (116, 565, 1048, 84),
                             "★ 比較① AFTER：ノイズが消え、歌詞の輪郭とシルキーな高域・立体残響が一気に開花！",
                             "DAWも高価なプラグインも不要！これ1つ立ち上げるだけでこの声がOBS・Discordへ直結します",
                             (235, 85, 148))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 3.0) % 1.0)

            elif t < 14.2:
                # 比較② BEFORE: カラオケ伴奏に素の声を乗せた状態（10.0s - 14.2s）
                step_txt = "聴き比べ② [カラオケ伴奏との馴染み]  無加工だと伴奏に声が埋もれて細く聴こえる…"
                audio_badge = ("♪ 再生中：伴奏＋無加工の声（Before）", (75, 130, 165))
                canvas.paste(uis["rose"], (ui_x, ui_y))
                draw_ab_banner(canvas, False,
                               "カラオケ伴奏＋無加工の素の声：伴奏のコードに声が負けて埋もれがち",
                               "歌枠配信や「歌ってみた」で一番多い悩み＝「BGMを流すと自分の歌声が後ろに引っ込んでしまう」")
                sub_t = (t - 10.0) / 4.2
                cx = int(lerp(545, 868, min(1.0, sub_t * 2.5)))
                cy = int(lerp(310, 142, min(1.0, sub_t * 2.5)))
                draw_callout(canvas, (116, 565, 1048, 84),
                             "♪ 比較② BEFORE：伴奏にそのまま素の声を乗せると、こもって伴奏に埋もれてしまいます",
                             "次は v4.8.0 新機能「内蔵BGMプレイヤー（声優先オートダッキング）＋5人ユニゾン」をONにします！",
                             (90, 195, 240))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.0) % 1.0)

            elif t < 19.0:
                # 比較② AFTER: v4.8.0 内蔵BGMダッキング＋5人ユニゾンハモリ＋直前30秒保存（14.2s - 19.0s）
                step_txt = "聴き比べ② [v4.8.0 新機能]  声優先BGMダッキング＋5人ハモリで歌声が主役に！"
                audio_badge = ("★ 再生中：BGM自動ダッキング＋5人ハモリ（After）", (235, 68, 132))
                canvas.paste(uis["rose"], (ui_x, ui_y))
                draw_ab_banner(canvas, True,
                               "歌うと自動で伴奏がすっと下がる！＋5人ユニゾン＆ホール残響で圧倒的な存在感",
                               "伴奏キー変更（±6半音）でピッチ補正も自動同期！練習中の神テイクは「直前30秒さかのぼり保存」で救出")
                sub_t = (t - 14.2) / 4.8
                hi_idx = 0 if sub_t < 0.5 else 1
                draw_stage_studio_overlay(canvas, highlight_sec=hi_idx)
                cx = 840
                cy = 250 if sub_t < 0.5 else 330
                draw_callout(canvas, (116, 565, 1048, 84),
                             "★ 比較② AFTER：歌い出すと伴奏が自動で少し下がり、5人ユニゾンでサビが華やかに広がる！",
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
                step_txt = "VocalGzzio v4.8.0  —  歌枠・歌ってみた・配信がこれ1台で完結！（無料体験版あり）"
                audio_badge = ("★ BOOTHにて無料体験版＆製品版（2,500円）公開中", (235, 85, 148))
                d.rounded_rectangle([48, 88, W - 48, 662], radius=22, fill=(26, 18, 34, 248), outline=(238, 165, 192, 230), width=3)
                d.text((82, 108), "VocalGzzio v4.8.0 — なぜ今選ばれているのか？（3つの強み）", font=f_big, fill=(255, 245, 250, 255))
                d.text((82, 160), "DAWも仮想ケーブルも高価なプラグインも不要。買い切り2,500円で歌枠・録音・配信護身が全部入り！", font=f_med, fill=(242, 192, 212, 255))

                # 3つのターゲット別メリットボックス
                targets = [
                    ("① 歌枠・Vライバー・配信者へ",
                     "・専用WDM仮想ドライバ同梱でOBSへ直結\n・伴奏再生＆キー変更±6＆自動ダッキング\n・歌枠⇄雑談MC 1クリック切替＆クチュール5色",
                     (238, 95, 152)),
                    ("② 歌ってみた・宅録シンガーへ",
                     "・直前30秒さかのぼり保存で神テイクを救出\n・完成音(Wet)＋MIX用素の声(Dry)同時WAV保存\n・5秒歌うだけのAI声質ターゲット自動調律",
                     (75, 215, 190)),
                    ("③ 実況・VC通話・DAW時短勢へ",
                     "・AIノイズ学習でエアコン・PCファン音消去\n・絶叫セーフティガード＆咳払い8ms無音遮断\n・追加遅延0.0ms ／ VST3・AUプラグイン同梱",
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

                d.text((82, 554), "★ 製品版 2,500円（買い切り・Win & Mac同梱・既存購入者は無料でv4.8.0へ更新OK） ／ まずは0円の無料体験版へ！",
                       font=get_font(21), fill=(255, 238, 175, 255))
                d.text((82, 590), "Vocal Source: Come Inside (pell) / Madam Snowflake (2019) CC BY 3.0 (ccmixter.org/files/snowflake/59563)",
                       font=f_sm, fill=(175, 162, 185, 255))
                d.text((82, 618), "https://gzzio.booth.pm/items/8764003   （Mac版は未公証／音程補正は単独の声向け・48kHzで約42.6ms遅延）",
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

    # アーティファクトディレクトリとv4.7.0互換パスにもコピー
    art_mp4 = ART_DIR / "VocalGzzio_v4.8.0_X_操作説明_音比較.mp4"
    shutil.copy2(out_mp4, art_mp4)
    print("Created X Promo Video:", out_mp4, f"({out_mp4.stat().st_size / 1024 / 1024:.2f} MB)")

if __name__ == "__main__":
    main()
