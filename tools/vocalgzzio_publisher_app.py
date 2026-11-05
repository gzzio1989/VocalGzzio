#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VocalGzzio 統合自動投稿＆配信スタジオ (VocalGzzio Auto-Publisher Studio v5.5.0)
=============================================================================
Antigravity の外でも単体アプリとして動作し、ログイン済みの Google Chrome (CDP) を通じて
以下の 4 媒体へのアップロード・記事公開・動画投稿・ツリーリプライを完全自動で実行します。

  1. BOOTH (https://manage.booth.pm/items/8764003/edit)
     - 最新の製品版ZIP・体験版ZIP・新UIスクリーンショットのアップロードとバリエーション更新・公開
  2. YouTube Studio (https://studio.youtube.com/channel/UCzsPKMV4lkM6e4hmau7QJ2g)
     - v5.5.0 YouTube Shorts 縦型プロモ動画 (.mp4) の自動アップロード・タイトル/概要欄設定・公開
  3. note (https://editor.note.com/notes/ndc7393460e75/edit/)
     - 最新アイキャッチ画像のアップロード・ハッシュタグ設定・記事の公開
  4. X / Twitter (https://x.com/home)
     - 140文字制限遵守の告知本ポスト＋横型プロモ動画 (.mp4) の自動添付・投稿
     - 投稿完了後、自動で返信ツリー（YouTube Shorts / note記事 / GitHub無料体験版リンク）を連投

使い方:
  - 全自動一括実行 (CLI):
      python C:\\Work\\vg_src\\tools\\vocalgzzio_publisher_app.py --run-all
  - Web GUI スタジオ起動 (http://localhost:3860/):
      python C:\\Work\\vg_src\\tools\\vocalgzzio_publisher_app.py --serve
"""

import argparse
import base64
import json
import os
import re
import sys
import threading
import time
import traceback
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")

SCRATCH_DIR = Path(r"C:\Users\silve\.gemini\antigravity\brain\8f2542b1-d681-42f3-92c9-1e836ae605e5\scratch")
ART_DIR = Path(r"C:\Users\silve\.gemini\antigravity\brain\8f2542b1-d681-42f3-92c9-1e836ae605e5")
ROOT_DIR = Path(r"C:\Work\03_アプリ・プラグイン開発\VocalGzzio\VocalGzzio_GitHub_ready\VocalGzzio")
DIST_OUT_DIR = ROOT_DIR / "配布物" / "out"
PROMO_DIR = ROOT_DIR / "promo_v550"

sys.path.insert(0, str(SCRATCH_DIR))
from cdp_client import CdpClient  # noqa: E402

# ---------------------------------------------------------------------------
# 投稿コンテンツ定義 (v5.5.0)
# ---------------------------------------------------------------------------
YT_SHORTS_VIDEO = PROMO_DIR / "02_YouTube_Shorts縦型_プロモ動画_v5.5.0.mp4"
X_PROMO_VIDEO = PROMO_DIR / "01_X_YouTube横型_プロモ動画_v5.5.0.mp4"
UI_SCREENSHOT = PROMO_DIR / "03_Midnight_Lantern_Bokeh_UI_v5.5.0.png"
NOTE_EYECATCH = PROMO_DIR / "03_note_アイキャッチ_v5.5.0.png"

YT_TITLE = "【歌枠・歌ってみた】声量10割開放＆音量250%でも割れない！アプリ別カラオケ取込・伴奏追従ハモリ・全19世代復元『VocalGzzio v5.5.0』#Shorts #歌枠 #歌ってみた"

YT_DESC = """歌枠・歌ってみた・VRChat / cluster・DTM向けオールインワン日本語ボーカルプロセッサー『VocalGzzio（ボーカルグッジオ）』が v5.5.0（Windows ＆ Mac Universal 対応）へ大型アップデート！

■ デモボーカル素材クレジット (CC BY 3.0)
Vocal Source: "Come Inside (pell)" by Madam Snowflake (2019) — Licensed under Creative Commons Attribution (3.0)
https://ccmixter.org/files/snowflake/59563

■ VocalGzzio v5.5.0 の8大新機能
1. アプリ別カラオケ音声の直接取込（YouTubeやカラオケアプリの伴奏だけを指定して取り込み＆声⇄伴奏±300ms音ズレ自動計測）
2. 伴奏コード自動追従の知能派ハモリ生成（スマート・ハーモニー）
3. SNS投稿用ショート動画（.mp4 横型16:9 ＆ 縦型9:16）の1クリック書き出し
4. 新プリセットパレット ＆ 巨大プレビュー「デカ文字ルーペ」＋配信ミニバー(520×115px)
5. 機械式アストロラーベ時計ノブ＆深藍ランタン光学UIへの全面刷新
6. 声量10割ダイレクト開放 ＆ 出力音量0〜250%でも一切割れない新DSP
7. 無音時CPU負荷を40〜70%削減する「Smart Sleep（スマート・スリープ）」
8. 過去全19世代（v1.0.0〜v5.5.0）完全復元「レガシー・タイムマシン」＆ DAW終了を妨げない起動時1回アップデート通知

▼ BOOTH（製品版 2,500円・無料体験版 0円 / Windows ＆ Mac対応）
https://gzzio.booth.pm/items/8764003

▼ VocalGzzio 公式ポータルサイト（機能図解・無料体験版ダウンロード）
https://gzzio1989.github.io/apps/vocalgzzio/

▼ GitHub リリース（v5.5.0 無料体験版インストーラー / ZIP）
https://github.com/gzzio1989/VocalGzzio/releases/tag/v5.5.0

#VocalGzzio #歌枠 #歌ってみた #OBS #VTuber #VRChat #cluster #DTM #VST3"""

X_MAIN_POST = """【大型更新】歌枠・歌ってみたを1画面で完結『VocalGzzio v5.5.0』Win/Mac公開🎤
🌐アプリ別カラオケ取込＆音ズレ計測
🎹コード追従ハモリ＆動画保存
🎤声量10割＆全19世代復元
https://gzzio.booth.pm/items/8764003 #歌枠"""

STATE_FILE = ROOT_DIR / "promo_v550" / "publish_state_v550.json"
LOG_LINES = []


def log(msg: str):
    ts = time.strftime("%H:%M:%S")
    line = f"[{ts}] {msg}"
    print(line, flush=True)
    LOG_LINES.append(line)


def load_state() -> dict:
    if STATE_FILE.exists():
        try:
            return json.loads(STATE_FILE.read_text(encoding="utf-8"))
        except Exception:
            pass
    return {
        "booth_updated": False,
        "booth_win_prod_id": None,
        "booth_win_trial_id": None,
        "youtube_shorts_url": "https://youtube.com/shorts/eY_yB3da90g",
        "note_url": "https://note.com/gzzio/n/ndc7393460e75",
        "x_status_url": "",
        "x_reply_posted": False,
    }


def save_state(st: dict):
    STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
    STATE_FILE.write_text(json.dumps(st, ensure_ascii=False, indent=2), encoding="utf-8")


def x_weighted_len(s: str) -> int:
    s_no_url = re.sub(r"https?://\S+", lambda _: "x" * 23, s)
    total = 0
    for ch in s_no_url:
        total += 1 if ord(ch) <= 0x10FF else 2
    return total


# ---------------------------------------------------------------------------
# 1. BOOTH 自動更新 (ZIP 2本 + 新UI画像 + バリエーション更新)
# ---------------------------------------------------------------------------
def _upload_zip_to_booth(cdp: CdpClient, sid: str, filepath: Path) -> list:
    filename = filepath.name
    raw = filepath.read_bytes()
    log(f"BOOTHへZIPアップロード中: {filename} ({len(raw) / 1024 / 1024:.2f} MB)...")
    cdp.call("Runtime.evaluate", {"expression": "window.__b64Chunks = [];"}, session_id=sid)
    chunk_size = 512 * 1024
    for i in range(0, len(raw), chunk_size):
        b64 = base64.b64encode(raw[i : i + chunk_size]).decode("ascii")
        cdp.call(
            "Runtime.evaluate",
            {"expression": f"window.__b64Chunks.push('{b64}');"},
            session_id=sid,
        )
    js = f"""
    (async () => {{
        const csrf = document.querySelector('meta[name="csrf-token"]')?.content || '';
        const arrays = window.__b64Chunks.map(b64 => {{
            const bin = atob(b64);
            const arr = new Uint8Array(bin.length);
            for (let i = 0; i < bin.length; i++) arr[i] = bin.charCodeAt(i);
            return arr;
        }});
        window.__b64Chunks = [];
        const blob = new Blob(arrays, {{ type: 'application/zip' }});
        const file = new File([blob], {json.dumps(filename)}, {{ type: 'application/zip' }});
        const fd = new FormData();
        fd.append('downloadable[file]', file);
        const r = await fetch('https://manage.booth.pm/items/8764003/downloadables', {{
            method: 'POST',
            headers: {{ 'X-CSRF-Token': csrf, 'Accept': 'application/json' }},
            body: fd,
            credentials: 'include'
        }});
        const text = await r.text();
        return JSON.stringify({{ status: r.status, body: text }});
    }})()
    """
    res = cdp.call(
        "Runtime.evaluate",
        {"expression": js, "awaitPromise": True, "returnByValue": True},
        session_id=sid,
    )
    out = json.loads(res["result"]["value"])
    log(f"  -> BOOTH ZIP upload status: {out['status']}")
    data = json.loads(out["body"])
    return data.get("files", [])


def publish_booth() -> dict:
    st = load_state()
    cdp = CdpClient()
    try:
        targets = cdp.call("Target.getTargets")["targetInfos"]
        booth_target = next(
            (t["targetId"] for t in targets if t.get("type") == "page" and "manage.booth.pm" in t.get("url", "")),
            None,
        )
        if not booth_target:
            booth_target = cdp.call(
                "Target.createTarget", {"url": "https://manage.booth.pm/items/8764003/edit"}
            )["targetId"]
            time.sleep(4.0)

        sid = cdp.call("Target.attachToTarget", {"targetId": booth_target, "flatten": True})["sessionId"]
        cdp.call("Page.enable", session_id=sid)
        cdp.call("Runtime.enable", session_id=sid)

        cur_url = cdp.call(
            "Runtime.evaluate", {"expression": "location.href", "returnByValue": True}, session_id=sid
        )["result"]["value"]
        if "manage.booth.pm/items/8764003/edit" not in cur_url:
            cdp.call("Page.navigate", {"url": "https://manage.booth.pm/items/8764003/edit"}, session_id=sid)
            time.sleep(4.0)

        prod_zip = DIST_OUT_DIR / "VocalGzzio_v5.5.0_製品版.zip"
        trial_zip = DIST_OUT_DIR / "VocalGzzio_v5.5.0_体験版.zip"

        files_after_prod = _upload_zip_to_booth(cdp, sid, prod_zip)
        win_prod_id = files_after_prod[-1]["id"]
        log(f"BOOTH 新Windows製品版 File ID: {win_prod_id}")

        files_after_trial = _upload_zip_to_booth(cdp, sid, trial_zip)
        win_trial_id = files_after_trial[-1]["id"]
        log(f"BOOTH 新Windows体験版 File ID: {win_trial_id}")

        # Get current item data
        raw_item = cdp.call(
            "Runtime.evaluate",
            {
                "expression": """
                (async () => {
                    const r = await fetch('https://manage.booth.pm/items/8764003', {
                        headers: { 'Accept': 'application/json', 'X-Requested-With': 'XMLHttpRequest' }
                    });
                    return await r.text();
                })()
                """,
                "awaitPromise": True,
                "returnByValue": True,
            },
            session_id=sid,
        )["result"]["value"]
        item_data = json.loads(raw_item)

        # Keep Mac v5.5.0 downloadable IDs (10055982 for Product, 10055983 for Trial) and replace Windows v5.5.0 IDs
        variations = item_data["variations"]
        for v in variations:
            if v["id"] == 14706491:
                v["name"] = "製品版 v5.5.0｜Windows＋Mac"
                v["downloadable_ids"] = [9903115, win_prod_id, 10055982]
            elif v["id"] == 14706110:
                v["name"] = "無料体験版 v5.5.0｜Windows＋Mac"
                v["downloadable_ids"] = [10055983, win_trial_id]

        # Save variations and publish item
        js_save = f"""
        (async () => {{
            const csrf = document.querySelector('meta[name="csrf-token"]')?.content || '';
            const headers = {{
                'Content-Type': 'application/json',
                'Accept': 'application/json',
                'X-CSRF-Token': csrf,
                'X-Requested-With': 'XMLHttpRequest'
            }};
            const varPayload = {{ variations: {json.dumps(variations, ensure_ascii=False)} }};
            const rVar = await fetch('https://manage.booth.pm/items/8764003/variations', {{
                method: 'POST',
                headers,
                body: JSON.stringify(varPayload),
                credentials: 'include'
            }});
            const itemPayload = {{
                item: {{
                    name: {json.dumps(item_data["name"], ensure_ascii=False)},
                    description: {json.dumps(item_data["description"], ensure_ascii=False)},
                    price: 2500,
                    state_event: 'publish',
                    adult: 0,
                    accept_proxy_shipping: 'refused',
                    category_id: 201,
                    preorder_enabled: false,
                    purchase_limit: 0,
                    shipping_date: null,
                    tags_array: {json.dumps(item_data.get("tags_array", []), ensure_ascii=False)},
                    event_ids: {json.dumps(item_data.get("event_ids", []), ensure_ascii=False)},
                    page_design: JSON.stringify({json.dumps(item_data.get("page_design", {"modules": []}))})
                }}
            }};
            const rItem = await fetch('https://manage.booth.pm/items/8764003', {{
                method: 'PATCH',
                headers,
                body: JSON.stringify(itemPayload),
                credentials: 'include'
            }});
            return JSON.stringify({{ varStatus: rVar.status, itemStatus: rItem.status }});
        }})()
        """
        save_res = cdp.call(
            "Runtime.evaluate",
            {"expression": js_save, "awaitPromise": True, "returnByValue": True},
            session_id=sid,
        )["result"]["value"]
        log(f"BOOTH バリエーション＆公開保存完了: {save_res}")

        st["booth_updated"] = True
        st["booth_win_prod_id"] = win_prod_id
        st["booth_win_trial_id"] = win_trial_id
        save_state(st)
        return st
    finally:
        cdp.close()


# ---------------------------------------------------------------------------
# 2. YouTube Studio 自動投稿 (Shorts .mp4 アップロード＆公開)
# ---------------------------------------------------------------------------
def publish_youtube_shorts() -> dict:
    st = load_state()
    cdp = CdpClient()
    try:
        targets = cdp.call("Target.getTargets")["targetInfos"]
        yt_target = next(
            (t["targetId"] for t in targets if t.get("type") == "page" and "studio.youtube.com" in t.get("url", "")),
            None,
        )
        if not yt_target:
            yt_target = cdp.call(
                "Target.createTarget",
                {"url": "https://studio.youtube.com/channel/UCzsPKMV4lkM6e4hmau7QJ2g"},
            )["targetId"]
            time.sleep(5.0)

        sid = cdp.call("Target.attachToTarget", {"targetId": yt_target, "flatten": True})["sessionId"]
        cdp.call("Page.enable", session_id=sid)
        cdp.call("DOM.enable", session_id=sid)
        cdp.call("Runtime.enable", session_id=sid)
        cdp.call("Page.bringToFront", session_id=sid)

        log("YouTube Studio アップロードダイアログを開いています...")
        cdp.call(
            "Page.navigate",
            {"url": "https://studio.youtube.com/channel/UCzsPKMV4lkM6e4hmau7QJ2g/videos/upload?d=ud"},
            session_id=sid,
        )
        time.sleep(5.0)
        doc = cdp.call("DOM.getDocument", {"depth": -1}, session_id=sid)
        q = cdp.call(
            "DOM.querySelector",
            {"nodeId": doc["root"]["nodeId"], "selector": 'input[type="file"]'},
            session_id=sid,
        )
        if not q.get("nodeId"):
            cdp.call(
                "Runtime.evaluate",
                {
                    "expression": """
                    (() => {
                        const createBtn = document.querySelector('#create-icon, #upload-icon, #upload-button, ytcp-button#create-icon');
                        if (createBtn) createBtn.click();
                    })()
                    """
                },
                session_id=sid,
            )
            time.sleep(2.0)
            cdp.call(
                "Runtime.evaluate",
                {
                    "expression": """
                    (() => {
                        const item = document.querySelector('tp-yt-paper-item#text-item-0, #text-item-0');
                        if (item) item.click();
                    })()
                    """
                },
                session_id=sid,
            )
            time.sleep(2.0)
            doc = cdp.call("DOM.getDocument", {"depth": -1}, session_id=sid)
            q = cdp.call(
                "DOM.querySelector",
                {"nodeId": doc["root"]["nodeId"], "selector": 'input[type="file"]'},
                session_id=sid,
            )

        if not q.get("nodeId"):
            log("YouTube Studio の file input が見つかりません。スキップします。")
            return st

        log(f"YouTube Shorts 動画をセット中: {YT_SHORTS_VIDEO.name}")
        cdp.call(
            "DOM.setFileInputFiles",
            {"nodeId": q["nodeId"], "files": [str(YT_SHORTS_VIDEO)]},
            session_id=sid,
        )

        for _ in range(30):
            time.sleep(1.5)
            has_title = cdp.call(
                "Runtime.evaluate",
                {
                    "expression": "!!document.querySelector('#title-textarea #textbox, ytcp-social-suggestions-textbox#title-textarea #textbox')",
                    "returnByValue": True,
                },
                session_id=sid,
            )["result"]["value"]
            if has_title:
                break

        time.sleep(2.0)
        js_fill = f"""
        (() => {{
            function setTextbox(sel, val) {{
                const el = document.querySelector(sel);
                if (!el) return false;
                el.focus();
                el.innerText = '';
                document.execCommand('selectAll', false, null);
                document.execCommand('insertText', false, val);
                el.dispatchEvent(new Event('input', {{ bubbles: true }}));
                el.dispatchEvent(new Event('change', {{ bubbles: true }}));
                return true;
            }}
            const tOk = setTextbox('#title-textarea #textbox', {json.dumps(YT_TITLE[:100], ensure_ascii=False)});
            const dOk = setTextbox('#description-textarea #textbox', {json.dumps(YT_DESC, ensure_ascii=False)});
            const notKids = document.querySelector('tp-yt-paper-radio-button[name="VIDEO_MADE_FOR_KIDS_NOT_MFK"], [name="VIDEO_MADE_FOR_KIDS_NOT_MFK"]');
            if (notKids) notKids.click();
            return JSON.stringify({{ tOk, dOk, clickedNotKids: !!notKids }});
        }})()
        """
        fill_res = cdp.call("Runtime.evaluate", {"expression": js_fill, "returnByValue": True}, session_id=sid)["result"]["value"]
        log(f"YouTube メタデータ入力完了: {fill_res}")
        time.sleep(1.5)

        video_url = ""
        for _ in range(3):
            cdp.call("Runtime.evaluate", {"expression": "document.querySelector('#next-button')?.click();"}, session_id=sid)
            time.sleep(2.0)

        cdp.call(
            "Runtime.evaluate",
            {
                "expression": """
                (() => {
                    const pub = document.querySelector('tp-yt-paper-radio-button[name="PUBLIC"], [name="PUBLIC"]');
                    if (pub) pub.click();
                })()
                """
            },
            session_id=sid,
        )
        time.sleep(2.0)

        for i in range(40):
            chk = json.loads(
                cdp.call(
                    "Runtime.evaluate",
                    {
                        "expression": """
                        (() => {
                            const btn = document.querySelector('#done-button');
                            const disabled = !btn || btn.disabled || btn.getAttribute('aria-disabled') === 'true';
                            const a = document.querySelector('a[href*="youtu.be"], a[href*="youtube.com/shorts"], a[href*="youtube.com/watch"]');
                            return JSON.stringify({ disabled, url: a ? a.href : '' });
                        })()
                        """,
                        "returnByValue": True,
                    },
                    session_id=sid,
                )["result"]["value"]
            )
            if chk.get("url"):
                video_url = chk["url"]
            if not chk["disabled"]:
                break
            time.sleep(2.0)

        cdp.call("Runtime.evaluate", {"expression": "document.querySelector('#done-button')?.click();"}, session_id=sid)
        time.sleep(2.5)
        # If YouTube shows "コンテンツはまだ確認中です" confirmation dialog, click "公開する"
        cdp.call(
            "Runtime.evaluate",
            {
                "expression": """
                (() => {
                    const sec = document.querySelector('#secondary-action-button, ytcp-button#secondary-action-button');
                    if (sec) { sec.click(); return 'clicked_secondary'; }
                    const btns = Array.from(document.querySelectorAll('ytcp-button, button'));
                    for (const b of btns) {
                        if ((b.innerText || '').trim() === '公開する') {
                            b.click();
                            return 'clicked_publish_anyway';
                        }
                    }
                    return 'no_confirm_modal';
                })()
                """
            },
            session_id=sid,
        )
        log(f"YouTube Shorts 公開完了! URL: {video_url}")
        time.sleep(4.5)

        shot = cdp.call("Page.captureScreenshot", {"format": "png"}, session_id=sid)
        (ART_DIR / "youtube_published_v5.5.0.png").write_bytes(base64.b64decode(shot["data"]))

        cdp.call(
            "Runtime.evaluate",
            {"expression": "document.querySelector('#close-button, ytcp-button#close-button')?.click();"},
            session_id=sid,
        )
        if video_url:
            st["youtube_shorts_url"] = video_url
            save_state(st)
        return st
    finally:
        cdp.close()


# ---------------------------------------------------------------------------
# 3. note 記事自動公開 (アイキャッチ設定＋ハッシュタグ＋公開)
# ---------------------------------------------------------------------------
def publish_note() -> dict:
    st = load_state()
    cdp = CdpClient()
    try:
        targets = cdp.call("Target.getTargets")["targetInfos"]
        note_target = next(
            (t["targetId"] for t in targets if t.get("type") == "page" and "note.com" in t.get("url", "")),
            None,
        )
        if not note_target:
            note_target = cdp.call(
                "Target.createTarget", {"url": "https://editor.note.com/notes/ndc7393460e75/edit/"}
            )["targetId"]
            time.sleep(5.0)

        nsid = cdp.call("Target.attachToTarget", {"targetId": note_target, "flatten": True})["sessionId"]
        cdp.call("Page.enable", session_id=nsid)
        cdp.call("DOM.enable", session_id=nsid)
        cdp.call("Runtime.enable", session_id=nsid)
        cdp.call("Page.bringToFront", session_id=nsid)

        cur_url = cdp.call(
            "Runtime.evaluate", {"expression": "location.href", "returnByValue": True}, session_id=nsid
        )["result"]["value"]
        if "ndc7393460e75/edit" not in cur_url:
            cdp.call("Page.navigate", {"url": "https://editor.note.com/notes/ndc7393460e75/edit/"}, session_id=nsid)
            time.sleep(5.0)

        # Upload eyecatch image if possible
        log("note 記事アイキャッチ画像をセット中...")
        cdp.call(
            "Runtime.evaluate",
            {
                "expression": """
                (async () => {
                    window.__eyecatchInput = null;
                    const origClick = HTMLInputElement.prototype.click;
                    HTMLInputElement.prototype.click = function() {
                        if (this.type === 'file') {
                            this.id = 'captured-eyecatch-input';
                            if (!this.isConnected) {
                                this.style.display = 'none';
                                document.body.appendChild(this);
                            }
                            window.__eyecatchInput = this;
                            return;
                        }
                        return origClick.apply(this, arguments);
                    };
                    const svg = document.querySelector('svg[aria-label="画像を追加"]');
                    const btn = svg ? svg.closest('button') : null;
                    if (btn) btn.click();
                    await new Promise(r => setTimeout(r, 600));
                    const upBtn = Array.from(document.querySelectorAll('button')).find(b => (b.innerText || '').includes('画像をアップロード'));
                    if (upBtn) upBtn.click();
                    await new Promise(r => setTimeout(r, 400));
                })()
                """,
                "awaitPromise": True,
            },
            session_id=nsid,
        )
        doc = cdp.call("DOM.getDocument", {"depth": -1}, session_id=nsid)
        q = cdp.call(
            "DOM.querySelector",
            {"nodeId": doc["root"]["nodeId"], "selector": "#captured-eyecatch-input"},
            session_id=nsid,
        )
        if q.get("nodeId") and NOTE_EYECATCH.exists():
            cdp.call(
                "DOM.setFileInputFiles",
                {"nodeId": q["nodeId"], "files": [str(NOTE_EYECATCH)]},
                session_id=nsid,
            )
            time.sleep(3.0)
            cdp.call(
                "Runtime.evaluate",
                {
                    "expression": """
                    (() => {
                        const b = Array.from(document.querySelectorAll('button')).find(btn => (btn.innerText || '').trim() === '保存');
                        if (b) b.click();
                    })()
                    """
                },
                session_id=nsid,
            )
            time.sleep(4.0)
            log("note アイキャッチ画像の保存完了!")

        # Click 公開に進む
        step1 = cdp.call(
            "Runtime.evaluate",
            {
                "expression": """
                (() => {
                    const btns = Array.from(document.querySelectorAll('button'));
                    for (const b of btns) {
                        const txt = (b.innerText || '').trim();
                        if (txt.includes('公開に進む') || txt === '公開設定' || txt === '更新に進む') {
                            b.click();
                            return 'clicked_' + txt;
                        }
                    }
                    return 'no_btn';
                })()
                """,
                "returnByValue": True,
            },
            session_id=nsid,
        )["result"]["value"]
        log(f"note 公開設定画面へ移動: {step1}")
        time.sleep(3.5)

        tags = ["VocalGzzio", "歌枠", "歌ってみた", "VTuber", "OBS", "VRChat", "cluster", "DTM"]
        for tg in tags:
            cdp.call(
                "Runtime.evaluate",
                {
                    "expression": f"""
                    (() => {{
                        const inp = document.querySelector('input[placeholder*="ハッシュタグ"], input[type="text"]');
                        if (!inp) return;
                        inp.focus();
                        const setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
                        setter.call(inp, {json.dumps(tg, ensure_ascii=False)});
                        inp.dispatchEvent(new Event('input', {{ bubbles: true }}));
                        inp.dispatchEvent(new KeyboardEvent('keydown', {{ key: 'Enter', code: 'Enter', keyCode: 13, which: 13, bubbles: true }}));
                    }})()
                    """
                },
                session_id=nsid,
            )
            time.sleep(0.35)

        time.sleep(1.5)
        pub_res = cdp.call(
            "Runtime.evaluate",
            {
                "expression": """
                (() => {
                    const btns = Array.from(document.querySelectorAll('button'));
                    for (const b of btns) {
                        const txt = (b.innerText || '').trim();
                        if (txt === '投稿する' || txt === '公開する' || txt === '更新する' || txt === '投稿') {
                            b.click();
                            return 'clicked_' + txt;
                        }
                    }
                    return 'not_found';
                })()
                """,
                "returnByValue": True,
            },
            session_id=nsid,
        )["result"]["value"]
        log(f"note 記事公開ボタン実行: {pub_res}")
        time.sleep(5.5)

        shot = cdp.call("Page.captureScreenshot", {"format": "png"}, session_id=nsid)
        (ART_DIR / "note_published_v5.5.0.png").write_bytes(base64.b64decode(shot["data"]))

        st["note_url"] = "https://note.com/gzzio/n/ndc7393460e75"
        save_state(st)
        log(f"note 公開完了: {st['note_url']}")
        return st
    finally:
        cdp.close()


# ---------------------------------------------------------------------------
# 4. X (Twitter) 自動投稿 (本投稿＋動画添付＋ツリーリプライ自動連投)
# ---------------------------------------------------------------------------
def _paste_into_x_editor(cdp: CdpClient, sid: str, text: str) -> str:
    js_focus = """
    (() => {
        const dlg = document.querySelector('[role="dialog"]') || document;
        const el = dlg.querySelector('[data-testid="tweetTextarea_0"]') || document.querySelector('[data-testid="tweetTextarea_0"]');
        if (!el) return 'no_el';
        el.scrollIntoView({ block: 'center' });
        el.click();
        el.focus();
        return 'focused';
    })()
    """
    foc = cdp.call("Runtime.evaluate", {"expression": js_focus, "returnByValue": True}, session_id=sid)["result"]["value"]
    if foc == "no_el":
        return "no_el"
    time.sleep(0.4)
    cdp.call("Input.insertText", {"text": text}, session_id=sid)
    time.sleep(0.5)
    js_read = """
    (() => {
        const dlg = document.querySelector('[role="dialog"]') || document;
        const el = dlg.querySelector('[data-testid="tweetTextarea_0"]') || document.querySelector('[data-testid="tweetTextarea_0"]');
        return el ? (el.innerText || '') : '';
    })()
    """
    return cdp.call(
        "Runtime.evaluate", {"expression": js_read, "returnByValue": True}, session_id=sid
    )["result"]["value"]


def publish_x() -> dict:
    st = load_state()
    assert x_weighted_len(X_MAIN_POST) <= 280, f"X_MAIN_POST weight {x_weighted_len(X_MAIN_POST)} > 280"
    log(f"X 告知ポスト文字数チェック OK: weight={x_weighted_len(X_MAIN_POST)}/280, chars={len(X_MAIN_POST)}/140")

    cdp = CdpClient()
    try:
        targets = cdp.call("Target.getTargets")["targetInfos"]
        x_target = next(
            (
                t["targetId"]
                for t in targets
                if t.get("type") == "page" and ("x.com" in t.get("url", "") or "twitter.com" in t.get("url", ""))
            ),
            None,
        )
        if not x_target:
            x_target = cdp.call("Target.createTarget", {"url": "https://x.com/home"})["targetId"]
            time.sleep(5.0)

        xsid = cdp.call("Target.attachToTarget", {"targetId": x_target, "flatten": True})["sessionId"]
        cdp.call("Page.enable", session_id=xsid)
        cdp.call("DOM.enable", session_id=xsid)
        cdp.call("Runtime.enable", session_id=xsid)
        cdp.call("Page.bringToFront", session_id=xsid)

        cdp.call("Page.navigate", {"url": "https://x.com/home"}, session_id=xsid)
        time.sleep(5.0)

        cdp.call(
            "Runtime.evaluate",
            {
                "expression": """
                (() => {
                    const btn = document.querySelector('[data-testid="SideNav_NewTweet_Button"]');
                    if (btn) btn.click();
                })()
                """
            },
            session_id=xsid,
        )
        time.sleep(2.5)

        pasted = _paste_into_x_editor(cdp, xsid, X_MAIN_POST)
        log(f"X 本投稿テキスト入力完了: {pasted[:50]}...")
        time.sleep(1.0)

        doc = cdp.call("DOM.getDocument", {"depth": -1}, session_id=xsid)
        q = cdp.call(
            "DOM.querySelector",
            {
                "nodeId": doc["root"]["nodeId"],
                "selector": 'input[data-testid="fileInput"], input[type="file"]',
            },
            session_id=xsid,
        )
        log(f"X 動画添付を実行中 (nodeId={q.get('nodeId')}): {X_PROMO_VIDEO.name}")
        cdp.call(
            "DOM.setFileInputFiles",
            {"nodeId": q["nodeId"], "files": [str(X_PROMO_VIDEO)]},
            session_id=xsid,
        )

        for i in range(45):
            time.sleep(1.5)
            chk = json.loads(
                cdp.call(
                    "Runtime.evaluate",
                    {
                        "expression": """
                        (() => {
                            const btn = document.querySelector('[data-testid="tweetButton"]') || document.querySelector('[data-testid="tweetButtonInline"]');
                            const hasMedia = !!document.querySelector('video, [data-testid="attachments"]');
                            const disabled = !btn || btn.disabled || btn.getAttribute('aria-disabled') === 'true';
                            return JSON.stringify({ hasMedia, disabled, text: btn ? btn.innerText : '' });
                        })()
                        """,
                        "returnByValue": True,
                    },
                    session_id=xsid,
                )["result"]["value"]
            )
            log(f"  X 動画処理待機 #{i+1}: {chk}")
            if chk["hasMedia"] and not chk["disabled"]:
                break

        time.sleep(1.5)
        cdp.call(
            "Runtime.evaluate",
            {
                "expression": """
                (() => {
                    const btn = document.querySelector('[data-testid="tweetButton"]') || document.querySelector('[data-testid="tweetButtonInline"]');
                    if (btn) btn.click();
                })()
                """
            },
            session_id=xsid,
        )
        log("X 本投稿を送信しました！")
        time.sleep(6.5)

        cdp.call("Page.navigate", {"url": "https://x.com/gzzio1989"}, session_id=xsid)
        time.sleep(4.5)
        cdp.call("Runtime.evaluate", {"expression": "window.scrollTo(0, 650);"}, session_id=xsid)
        time.sleep(2.0)

        new_status_url = cdp.call(
            "Runtime.evaluate",
            {
                "expression": """
                (() => {
                    const articles = Array.from(document.querySelectorAll('article'));
                    for (const a of articles) {
                        if ((a.innerText || '').includes('VocalGzzio v5.5.0')) {
                            const l = a.querySelector('a[href*="/status/"]');
                            if (l) return l.href;
                        }
                    }
                    return '';
                })()
                """,
                "returnByValue": True,
            },
            session_id=xsid,
        )["result"]["value"]
        log(f"X 本投稿 URL 取得: {new_status_url}")

        if new_status_url:
            st["x_status_url"] = new_status_url
            reply_text = (
                f"📺YouTube Shorts 音比較デモ\n{st.get('youtube_shorts_url', '')}\n\n"
                f"📘v5.5.0全機能解説(note)\n{st.get('note_url', 'https://note.com/gzzio/n/ndc7393460e75')}\n\n"
                f"💻公式サイト＆無料体験版直DL\nhttps://gzzio1989.github.io/apps/vocalgzzio/\n\n"
                f"🎵Vocal: Come Inside (pell) / Madam Snowflake (CC BY 3.0)"
            )
            cdp.call("Page.navigate", {"url": new_status_url}, session_id=xsid)
            time.sleep(4.5)
            cdp.call(
                "Runtime.evaluate",
                {"expression": 'document.querySelector(\'[data-testid="tweetTextarea_0"]\')?.click();'},
                session_id=xsid,
            )
            time.sleep(1.0)
            _paste_into_x_editor(cdp, xsid, reply_text)
            time.sleep(1.2)
            cdp.call(
                "Runtime.evaluate",
                {"expression": 'document.querySelector(\'[data-testid="tweetButtonInline"]\')?.click();'},
                session_id=xsid,
            )
            log("X ツリーリプライ（YouTube Shorts / note / 公式サイト）を自動送信しました！")
            st["x_reply_posted"] = True
            save_state(st)
            time.sleep(4.5)

        shot_x = cdp.call("Page.captureScreenshot", {"format": "png"}, session_id=xsid)
        (ART_DIR / "x_published_v5.5.0.png").write_bytes(base64.b64decode(shot_x["data"]))
        return st
    finally:
        cdp.close()


def run_all_pipeline():
    log("=== VocalGzzio v5.5.0 4媒体（BOOTH / YouTube / note / X）全自動一括投稿を開始します ===")
    try:
        publish_booth()
    except Exception as e:
        log(f"[WARN] BOOTH更新中に例外: {e}\n{traceback.format_exc()}")

    try:
        publish_youtube_shorts()
    except Exception as e:
        log(f"[WARN] YouTube投稿中に例外: {e}\n{traceback.format_exc()}")

    try:
        publish_note()
    except Exception as e:
        log(f"[WARN] note公開中に例外: {e}\n{traceback.format_exc()}")

    try:
        publish_x()
    except Exception as e:
        log(f"[WARN] X投稿中に例外: {e}\n{traceback.format_exc()}")

    log("=== 全自動一括投稿パイプラインが完了しました！ ===")
    return load_state()


# ---------------------------------------------------------------------------
# 5. ローカル Web GUI スタジオ (:3860)
# ---------------------------------------------------------------------------
STUDIO_HTML = """<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<title>VocalGzzio v5.5.0 — 統合自動投稿＆配信スタジオ (:3860)</title>
<style>
  body { margin:0; padding:24px; background:#070d1f; color:#f6f3ee; font-family:"Yu Gothic UI","Meiryo",sans-serif; }
  .wrap { max-width:1100px; margin:0 auto; }
  .hero { background:linear-gradient(135deg,#0c1838,#1b1435); border:2px solid #ffaa2b; border-radius:16px; padding:22px 28px; margin-bottom:20px; }
  h1 { margin:0 0 8px; font-size:1.5rem; color:#fff6d6; }
  .btns { display:grid; grid-template-columns:repeat(auto-fit,minmax(220px,1fr)); gap:12px; margin:18px 0; }
  button { padding:14px 18px; border-radius:12px; border:1.5px solid #ffaa2b; background:#102048; color:#fff; font-weight:bold; font-size:0.98rem; cursor:pointer; }
  button.primary { background:linear-gradient(135deg,#ffaa2b,#ff708e); color:#120a00; border:none; font-size:1.05rem; }
  button:hover { opacity:0.92; transform:translateY(-1px); }
  pre { background:#040814; border:1px solid #2b4478; border-radius:12px; padding:16px; height:380px; overflow:auto; font-family:Consolas,monospace; font-size:0.9rem; color:#8ef5e0; }
</style>
</head>
<body>
<div class="wrap">
  <div class="hero">
    <h1>🚀 VocalGzzio v5.5.0 — 統合自動投稿＆配信スタジオ (:3860)</h1>
    <div>Chrome (CDP) 直結エンジンにより、APIキー不要で BOOTH・YouTube Shorts・note・X (旧Twitter) へ自動投稿します。</div>
  </div>
  <div class="btns">
    <button class="primary" onclick="trigger('/api/run_all')">🔥 4媒体すべてを一括自動投稿</button>
    <button onclick="trigger('/api/booth')">🛍️ ① BOOTH 配布ZIP＆画像を自動更新</button>
    <button onclick="trigger('/api/youtube')">🎬 ② YouTube Shorts を自動投稿</button>
    <button onclick="trigger('/api/note')">📝 ③ note 記事を自動公開</button>
    <button onclick="trigger('/api/x')">🐦 ④ X 本投稿＋返信ツリーを自動投稿</button>
  </div>
  <h3>📡 リアルタイム実行ログ</h3>
  <pre id="logbox">待機中...</pre>
</div>
<script>
async function trigger(path) {
  await fetch(path, { method: 'POST' });
  pollLogs();
}
async function pollLogs() {
  const r = await fetch('/api/logs');
  const d = await r.json();
  const box = document.getElementById('logbox');
  box.textContent = (d.logs || []).join('\\n') || '待機中...';
  box.scrollTop = box.scrollHeight;
}
setInterval(pollLogs, 1500);
pollLogs();
</script>
</body>
</html>
"""


class StudioHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def do_GET(self):
        if self.path == "/api/logs":
            body = json.dumps({"logs": LOG_LINES, "state": load_state()}, ensure_ascii=False).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.end_headers()
            self.wfile.write(body)
        else:
            body = STUDIO_HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(body)

    def do_POST(self):
        target_fn = {
            "/api/run_all": run_all_pipeline,
            "/api/booth": publish_booth,
            "/api/youtube": publish_youtube_shorts,
            "/api/note": publish_note,
            "/api/x": publish_x,
        }.get(self.path)
        if target_fn:
            threading.Thread(target=target_fn, daemon=True).start()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"ok":true}')
        else:
            self.send_response(404)
            self.end_headers()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-all", action="store_true", help="Run all 4 publishers immediately")
    parser.add_argument("--booth", action="store_true")
    parser.add_argument("--youtube", action="store_true")
    parser.add_argument("--note", action="store_true")
    parser.add_argument("--x", action="store_true")
    parser.add_argument("--serve", action="store_true", help="Start Web Studio at http://localhost:3860")
    args = parser.parse_args()

    if args.booth:
        publish_booth()
    if args.youtube:
        publish_youtube_shorts()
    if args.note:
        publish_note()
    if args.x:
        publish_x()
    if args.run_all:
        run_all_pipeline()
    if args.serve:
        srv = HTTPServer(("127.0.0.1", 3860), StudioHandler)
        log("VocalGzzio 統合自動投稿スタジオ Web UI 起動: http://localhost:3860/")
        srv.serve_forever()


if __name__ == "__main__":
    main()
