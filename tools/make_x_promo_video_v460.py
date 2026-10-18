#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VocalGzzio v4.6.0 — X（Twitter）プロモーション用「操作説明＋音量統一Before/Afterデモ動画」生成スクリプト。
- 解像度: 1280 x 720 (16:9 HD), 24 fps, 約32秒 (H.264 yuv420p + AAC 48kHz Stereo)
- タイムライン最初の0.2秒からカーソルとUIが動き、ミュート再生でも操作手順と魅力が直感的に伝わる設計。
- 音声は review/promotion-4.5.0/comparison-video の -18.0 LUFS ラウドネス統一済みボーカル（原音→処理後）を使用。
"""
import math
import os
import subprocess
import tempfile
import wave
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
ART_DIR = Path(r"C:\Users\silve\.gemini\antigravity\brain\8f2542b1-d681-42f3-92c9-1e836ae605e5")
PROMO_450 = ROOT / "review" / "promotion-4.5.0" / "comparison-video"
OUT_X_DIR = ROOT / "記録" / "X素材"
FFMPEG = r"C:\Users\silve\AppData\Local\Microsoft\WinGet\Links\ffmpeg.EXE"

W, H = 1280, 720
FPS = 24
DURATION = 32.0
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

def load_ui(name: str, target_size=(1060, 598)) -> Image.Image:
    p = ART_DIR / name
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
        r = int(12 + 42 * ripple_prog)
        alpha = int(220 * (1.0 - ripple_prog))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 220, 120, alpha), width=4)
        r2 = int(6 + 24 * ripple_prog)
        d.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], fill=(255, 110, 165, int(130 * (1.0 - ripple_prog))))

    # カーソル矢印ポリゴン（見やすい白塗り＋濃紺フチ＋ピンクグロー）
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
    d.polygon(shadow_pts, fill=(0, 0, 0, 160))
    d.polygon(abs_pts, fill=(255, 255, 255, 255), outline=(24, 16, 28, 255))
    d.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], fill=(255, 90, 150, 240))

def draw_callout(canvas: Image.Image, box: tuple, title: str, subtitle: str, accent_col=(255, 105, 160)):
    d = ImageDraw.Draw(canvas, "RGBA")
    x, y, w, h = box
    d.rounded_rectangle([x + 4, y + 6, x + w + 4, y + h + 6], radius=16, fill=(0, 0, 0, 140))
    d.rounded_rectangle([x, y, x + w, y + h], radius=16, fill=(22, 16, 28, 238), outline=accent_col + (255,), width=3)
    d.rounded_rectangle([x + 14, y + 12, x + 22, y + h - 12], radius=4, fill=accent_col + (255,))
    f_t = get_font(23)
    f_s = get_font(18)
    d.text((x + 34, y + 12), title, font=f_t, fill=(255, 248, 252, 255))
    d.text((x + 34, y + 44), subtitle, font=f_s, fill=(220, 208, 225, 255))

def build_audio_track(out_wav: Path):
    """
    32秒のステレオ48kHz WAVを生成する:
    - 0.0s - 4.0s : 処理後ボーカルのフェードイン導入（華やかなサウンドで引き込む）
    - 4.0s - 9.5s : 加工前（原音・-18 LUFS統一）
    - 9.5s - 28.5s: 加工後（VocalGzzio処理後・-18 LUFS統一）
    - 28.5s - 32.0s: 余韻＆フェードアウト
    """
    raw_path = PROMO_450 / "raw-loudness-matched.wav"
    proc_path = PROMO_450 / "processed-loudness-matched.wav"

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

    if raw_path.exists() and proc_path.exists():
        _, raw_arr = read_stereo(raw_path)
        _, proc_arr = read_stereo(proc_path)

        def get_slice(arr, start_s, len_s):
            s0 = int(start_s * sr)
            n_req = int(len_s * sr)
            out = np.zeros((n_req, 2), dtype=np.float32)
            pos = 0
            while pos < n_req:
                rem = n_req - pos
                avail = len(arr) - (s0 if pos == 0 else 0)
                offset = s0 if pos == 0 else 0
                take = min(rem, avail)
                out[pos:pos + take] = arr[offset:offset + take]
                pos += take
            return out

        # 0.0 - 4.0s: イントロ（控えめな音量で処理後フレーズ）
        seg_intro = get_slice(proc_arr, 0.0, 4.0) * 0.75
        fade_in = np.linspace(0.0, 1.0, int(0.4 * sr))[:, None]
        seg_intro[:len(fade_in)] *= fade_in
        master[0:int(4.0 * sr)] = seg_intro

        # 4.0 - 9.5s: 原音（Before）
        s_raw_0 = int(4.0 * sr)
        s_raw_1 = int(9.5 * sr)
        seg_raw = get_slice(raw_arr, 0.5, 5.5)
        master[s_raw_0:s_raw_1] = seg_raw

        # 9.5 - 32.0s: 処理後（After）— 同じフレーズの続き＋ループで違いが明確に分かるように
        s_proc_0 = int(9.5 * sr)
        seg_proc1 = get_slice(proc_arr, 0.5, 17.0)
        seg_proc2 = get_slice(proc_arr, 0.5, 5.5)
        seg_proc = np.vstack([seg_proc1, seg_proc2])[:total_samples - s_proc_0]
        master[s_proc_0:s_proc_0 + len(seg_proc)] = seg_proc

        # 境界のクロスフェード（クリック防止 20ms）
        for boundary_s in [4.0, 9.5]:
            b_idx = int(boundary_s * sr)
            w_len = int(0.02 * sr)
            if b_idx - w_len >= 0 and b_idx + w_len < total_samples:
                env = np.linspace(0.0, 1.0, w_len * 2)[:, None]
                master[b_idx - w_len:b_idx + w_len] *= np.abs(env - 0.5) * 2.0

        # 末尾 1.5s フェードアウト
        fo_len = int(1.5 * sr)
        master[-fo_len:] *= np.linspace(1.0, 0.0, fo_len)[:, None]

    master_i16 = np.clip(master * 32767.0, -32768, 32767).astype(np.int16)
    with wave.open(str(out_wav), "wb") as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        wf.writeframes(master_i16.tobytes())

def main():
    OUT_X_DIR.mkdir(parents=True, exist_ok=True)
    # UIスクリーンショットの事前読み込み（v4.7.0 新UIを優先）
    ui_Sz = (1080, 609)
    rose_name = "v4.7.0_couture_stage.png" if (ART_DIR / "v4.7.0_couture_stage.png").exists() else "v4.6.0_couture_01_rose.png"
    argon_name = "v4.7.0_argon_cockpit.png" if (ART_DIR / "v4.7.0_argon_cockpit.png").exists() else "v4.6.0_persona_argon_scifi.png"
    uis = {
        "rose": load_ui(rose_name, ui_Sz),
        "lilac": load_ui("v4.6.0_couture_02_lilac.png", ui_Sz),
        "noir": load_ui("v4.6.0_couture_03_noir.png", ui_Sz),
        "mint": load_ui("v4.6.0_couture_04_mint.png", ui_Sz),
        "peach": load_ui("v4.6.0_couture_05_peach.png", ui_Sz),
        "argon": load_ui(argon_name, ui_Sz),
        "yurufuwa": load_ui("v4.6.0_persona_yurufuwa_cafe.png", ui_Sz),
        "night": load_ui("v4.6.0_persona_yurufuwa_night.png", ui_Sz),
        "studio": load_ui("v4.6.0_persona_studio_pro.png", ui_Sz),
    }

    # 小サムネ（アウトロ用）
    mini_sz = (224, 126)
    minis = [
        ("01 ロゼ", uis["rose"].resize(mini_sz, Image.LANCZOS), (236, 105, 148)),
        ("02 氷紫", uis["lilac"].resize(mini_sz, Image.LANCZOS), (175, 145, 248)),
        ("03 黒姫", uis["noir"].resize(mini_sz, Image.LANCZOS), (225, 82, 132)),
        ("04 翡翠", uis["mint"].resize(mini_sz, Image.LANCZOS), (82, 212, 190)),
        ("05 白桃", uis["peach"].resize(mini_sz, Image.LANCZOS), (245, 152, 112)),
    ]

    f_top = get_font(24)
    f_badge = get_font(19)
    f_big = get_font(44)
    f_med = get_font(26)
    f_sm = get_font(18)

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        wav_path = tmp / "audio.wav"
        build_audio_track(wav_path)

        # FFmpeg rawvideo パイプで高速・高画質エンコード
        out_mp4 = OUT_X_DIR / "VocalGzzio_v4.7.0_X_操作説明_音比較.mp4"
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

        ui_x, ui_y = 100, 86  # 1080x609 を中央配置

        for frame_idx in range(TOTAL_FRAMES):
            t = frame_idx / FPS
            canvas = Image.new("RGBA", (W, H), (18, 14, 24, 255))
            d = ImageDraw.Draw(canvas, "RGBA")

            # 上部ステップヘッダーバー
            d.rectangle([0, 0, W, 72], fill=(28, 20, 36, 255))
            d.line([0, 72, W, 72], fill=(225, 120, 165, 180), width=2)

            if t < 4.5:
                # STEP 1: クチュール・コスメティック5色のワンクリック着せ替え
                step_txt = "STEP 1 / 4  「クチュール・コスメ5色」＆新搭載【♪歌枠⇄MC】【◎5秒試聴】"
                audio_badge = ("★ VocalGzzio v4.7.0 デモ再生中", (225, 82, 138))
                # 0.0 -> 4.5s で 5色を順番にクリック
                swatches_order = [
                    ("rose", 455, 108, "01 ロゼ・クチュール (Rose Gold & Ruby)"),
                    ("lilac", 502, 108, "02 アイシーラベンダー (Glacier Lilac)"),
                    ("noir", 548, 108, "03 ノワール・ロリータ (Noir Ruban)"),
                    ("mint", 595, 108, "04 ティファニーミント (Celadon Bijou)"),
                    ("peach", 642, 108, "05 ハニーピーチ (Apricot Miel)"),
                ]
                seg = min(4, int(t / 0.9))
                sub_t = (t - seg * 0.9) / 0.9
                cur_key, tx, ty, cname = swatches_order[seg]
                prev_key, px, py, _ = swatches_order[max(0, seg - 1)]
                canvas.paste(uis[cur_key], (ui_x, ui_y))
                cx = int(lerp(px if seg > 0 else 380, tx, min(1.0, sub_t * 2.5)))
                cy = int(lerp(py if seg > 0 else 180, ty, min(1.0, sub_t * 2.5)))
                draw_callout(canvas, (130, 565, 680, 82),
                             f"♥ {cname}",
                             "上部の [01ロゼ]〜[05白桃] ピル（または ♥LIVER 再クリック）で即切替！",
                             (236, 105, 152))
                draw_cursor(canvas, cx, cy, clicking=(sub_t < 0.35), ripple_prog=min(1.0, sub_t * 1.6))

            elif t < 9.5:
                # STEP 2A: 加工前の原音（Before）を聴く
                step_txt = "STEP 2 / 4  「押して原音と比較」で加工前の声をチェック（-18 LUFS 音量統一）"
                audio_badge = ("♪ いま再生中：加工前の原音（Before / -18 LUFS）", (90, 145, 175))
                canvas.paste(uis["argon"], (ui_x, ui_y))
                sub_t = (t - 4.5) / 5.0
                # 「押して原音と比較」ボタン付近 (x=925, y=142)
                cx = int(lerp(640, 925, min(1.0, sub_t * 3.0)))
                cy = int(lerp(110, 142, min(1.0, sub_t * 3.0)))
                # ハイライト枠
                d.rounded_rectangle([850, 124, 1018, 160], radius=8, outline=(120, 225, 255, 255), width=3)
                draw_callout(canvas, (130, 565, 760, 82),
                             "♪ 比較①：まずは加工前の原音（Before）",
                             "「押して原音と比較」を押している間だけ原音を聴き比べられます（音量差なしで比較）",
                             (90, 195, 240))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.0) % 1.0)

            elif t < 16.0:
                # STEP 2B: 加工後（After）＆ 2D声質重力場オーブと4軸バーの操作
                step_txt = "STEP 2 / 4  中央の「2D声質オーブ」と4本のバーで声の温かみ・声芯・空気・立体を成形"
                audio_badge = ("★ いま再生中：VocalGzzio 加工後（After / -18 LUFS）", (235, 85, 145))
                canvas.paste(uis["argon"], (ui_x, ui_y))
                sub_t = (t - 9.5) / 6.5
                # 中央オーブ (x=545, y=310) から4軸バー (x=685, y=335) へドラッグ演出
                angle = sub_t * math.pi * 2.0
                if sub_t < 0.6:
                    cx = int(545 + 32 * math.cos(angle))
                    cy = int(310 + 24 * math.sin(angle))
                else:
                    cx = int(lerp(545, 685, (sub_t - 0.6) / 0.4))
                    cy = int(lerp(310, 338, (sub_t - 0.6) / 0.4))
                # オーブ強調リング
                r_glow = int(42 + 8 * math.sin(t * 6.0))
                d.ellipse([cx - r_glow, cy - r_glow, cx + r_glow, cy + r_glow], outline=(110, 235, 255, 210), width=3)
                draw_callout(canvas, (130, 565, 820, 82),
                             "★ 比較②：加工後の声（倍音22%・声芯40%・プレート残響12%）",
                             "中央のオーブや4本のバーをドラッグするだけ！言葉の輪郭が前に出て艶やかな声に",
                             (235, 95, 155))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 3.0) % 1.0)

            elif t < 22.0:
                # STEP 3: v4.7.0 新機能 ♪歌枠⇄雑談MC 1クリック切替 ＆ ◎5秒試聴ルーパー
                step_txt = "STEP 3 / 4  v4.7.0新機能「♪歌枠⇄雑談MC 瞬間切替」＆「◎5秒試聴ルーパー」"
                audio_badge = ("★ いま再生中：VocalGzzio 加工後（After / -18 LUFS）", (68, 198, 175))
                canvas.paste(uis["rose"], (ui_x, ui_y))
                sub_t = (t - 16.0) / 6.0
                if sub_t < 0.5:
                    # ♪歌枠 / 雑談MC ピル付近 (x=592, y=108)
                    p = sub_t / 0.5
                    cx = int(lerp(680, 592, min(1.0, p * 2.2)))
                    cy = int(lerp(310, 108, min(1.0, p * 2.2)))
                    d.rounded_rectangle([545, 92, 636, 126], radius=10, outline=(255, 220, 110, 255), width=4)
                    draw_callout(canvas, (130, 565, 850, 82),
                                 "♪ 歌枠 ⇄ 雑談MC ワンクリック瞬間切替（v4.7.0 新機能）",
                                 "曲が終わってトークに入る瞬間、1クリックでリバーブ・ディレイ・ピッチをなめらかに退避！",
                                 (235, 95, 155))
                else:
                    # ◎5秒試聴ルーパー ピル付近 (x=668, y=108)
                    p = (sub_t - 0.5) / 0.5
                    cx = int(lerp(592, 668, min(1.0, p * 2.2)))
                    cy = int(lerp(108, 108, min(1.0, p * 2.2)))
                    d.rounded_rectangle([634, 92, 712, 126], radius=10, outline=(110, 235, 255, 255), width=4)
                    draw_callout(canvas, (130, 565, 850, 82),
                                 "◎ 5秒試聴ルーパー（手放しセルフ試聴 ／ v4.7.0 新機能）",
                                 "マイクに5秒歌うだけで自動ループ再生！発声の骨伝導に邪魔されず両手でじっくり音作り",
                                 (72, 210, 185))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 2.0) % 1.0)

            elif t < 27.5:
                # STEP 4: 5つのペルソナ画面切替 ＆ ◆声優先ダッキング直結＆空間IR波形
                step_txt = "STEP 4 / 4  5つのペルソナ画面 ＆ ◆声優先ダッキング可視化・カスタムIR読込"
                audio_badge = ("★ いま再生中：VocalGzzio 加工後（After / -18 LUFS）", (218, 158, 75))
                sub_t = (t - 22.0) / 5.5
                if sub_t < 0.34:
                    canvas.paste(uis["yurufuwa"], (ui_x, ui_y))
                    cx, cy = 415, 108
                    title_c = "● ゆるふわカフェ ＆ 夜モード（目に優しいボタニカル配色）"
                    sub_c = "長時間の配信や深夜の宅録でも目が疲れにくいウォームリネン＆ダークカフェ"
                elif sub_t < 0.67:
                    canvas.paste(uis["studio"], (ui_x, ui_y))
                    cx, cy = 465, 108
                    title_c = "■ STUDIO コンソール（マホガニー木目＆真鍮・縦列ストリップ）"
                    sub_c = "録音エンジニア馴染みの縦列信号フローで、整える→声をつくる→空間を一覧操作"
                else:
                    canvas.paste(uis["argon"], (ui_x, ui_y))
                    cx, cy = 960, 515
                    d.rounded_rectangle([795, 420, 1145, 555], radius=12, outline=(185, 150, 255, 255), width=4)
                    title_c = "◆ 声優先ダッキング直結＆可視化 ＋ 手持ちのカスタムIR（.wav）読込"
                    sub_c = "歌唱中は残響を自動ダッキングして歌詞くっきり！IRカード上で減衰量(-dB)も一目瞭然"
                draw_callout(canvas, (130, 565, 850, 82), title_c, sub_c, (185, 140, 245))
                draw_cursor(canvas, cx, cy, clicking=True, ripple_prog=(sub_t * 3.0) % 1.0)

            else:
                # OUTRO (27.5s - 32.0s): 全5色クチュール一覧＋BOOTH案内＋音源クレジット
                step_txt = "VocalGzzio v4.7.0  —  Windows & Mac 両対応 ／ まずは0円の無料体験版へ！"
                audio_badge = ("♪ BOOTHにて無料体験版＆製品版（2,500円）公開中", (235, 95, 155))
                # 背景カード
                d.rounded_rectangle([60, 96, W - 60, 650], radius=24, fill=(28, 20, 36, 245), outline=(235, 165, 190, 220), width=3)
                d.text((100, 122), "VocalGzzio v4.7.0（Live Talk & Looper Edition）", font=f_big, fill=(255, 245, 250, 255))
                d.text((100, 184), "歌枠・雑談配信・SYNCROOM・歌ってみたの音づくりを、1つの画面で。", font=f_med, fill=(238, 190, 210, 255))

                # 5色ミニカード横並び
                mx = 82
                my = 240
                for m_title, m_im, m_col in minis:
                    canvas.paste(m_im, (mx, my))
                    d.rounded_rectangle([mx, my, mx + mini_sz[0], my + mini_sz[1]], radius=10, outline=m_col + (255,), width=3)
                    d.rounded_rectangle([mx + 8, my + 92, mx + mini_sz[0] - 8, my + 120], radius=6, fill=(18, 12, 22, 220))
                    d.text((mx + 68, my + 96), m_title, font=f_badge, fill=m_col + (255,))
                    mx += mini_sz[0] + 14

                # 特徴まとめ
                bullets = [
                    "♪ 歌枠⇄雑談MC 1クリック切替 ＆ ◎5秒試聴ルーパー ＆ ◆声優先ダッキング可視化",
                    "★ クチュール5色（01ロゼ／02氷紫／03黒姫／04翡翠／05白桃）＆ 片耳中央化・配信出力直結",
                    "◆ 製品版 2,500円（買い切り・Windows & Mac 同梱・既存購入者は無料で4.7.0へ更新可能）",
                    "♥ 無料体験版あり（60秒ごとに0.6秒音量低下・保存不可 ／ 音質と全機能をお試し可能）",
                ]
                by = 392
                for b in bullets:
                    d.text((100, by), b, font=get_font(22), fill=(250, 245, 252, 255))
                    by += 42

                d.text((100, 582), "Vocal Source: Coin of the Realm / Mr_Yesterday (Lyrics D A Ayer, 2020) CC BY 3.0 — Loudness matched at -18.0 LUFS",
                       font=f_sm, fill=(175, 162, 185, 255))
                d.text((100, 610), "https://gzzio.booth.pm/items/8764003   （Mac版は未公証／音程補正は単独の声向け・48kHzで約42.6ms遅延）",
                       font=f_sm, fill=(245, 215, 155, 255))

            # 上部バーのテキストと右上の音声状態バッジを描画
            d.text((28, 21), step_txt, font=f_top, fill=(255, 248, 252, 255))
            b_txt, b_col = audio_badge
            bb = d.textbbox((0, 0), b_txt, font=f_badge)
            bw = bb[2] - bb[0] + 32
            bx = W - bw - 24
            d.rounded_rectangle([bx, 14, bx + bw, 58], radius=22, fill=b_col + (255,), outline=(255, 255, 255, 200), width=2)
            d.text((bx + 16, 24), b_txt, font=f_badge, fill=(255, 255, 255, 255))

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

    print("Created X Promo Video:", out_mp4, f"({out_mp4.stat().st_size / 1024 / 1024:.2f} MB)")

if __name__ == "__main__":
    main()
