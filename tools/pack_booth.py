#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BOOTH に上げる前に「版を取り違えていないか」を機械で確かめる。

v3.1 では 配布物をつくる.bat が Inno Setup でインストーラーを作るので、
ふだんは **--verify-only** だけ使えば足りる:

    python tools/pack_booth.py --edition product --version 4.0.0 --verify-only
    python tools/pack_booth.py --edition trial   --version 4.0.0 --verify-only

--verify-only を付けなければ、zip も作る（インストーラーを使わない配り方の場合）。

いちばん大事な仕事は「圧縮」ではなく **取り違えを止めること**。
出来上がったバイナリに埋めた合い言葉
    VOCALGZZIO-EDITION:PRODUCT / :TRIAL / :LITE
を読んで、注文と食い違ったら zip を作らずに止まる。

使い方（プロジェクト直下で）:
    python tools/pack_booth.py --edition product --version 4.0.0
    python tools/pack_booth.py --edition trial   --version 4.0.0

    --build  ビルド先（既定 build / 体験版は build_trial）
    --out    出力先（既定 配布物/out）
"""
import argparse, os, re, sys, zipfile, shutil, hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

MARK = b"VOCALGZZIO-EDITION:"
WANT = {"product": b"VOCALGZZIO-EDITION:PRODUCT",
        "trial":   b"VOCALGZZIO-EDITION:TRIAL"}

# 配らないもの（うっかり混入を機械で止める）
FORBIDDEN_TEXT = [
    (b"GNU AFFERO GENERAL PUBLIC LICENSE",
     "AGPLv3 の全文が入っています。有償配布物に入れてはいけません。"),
]

LICENSE_FILES = [
    "使用許諾契約書.txt",
    "THIRD-PARTY-NOTICES.txt",
    "JUCE-8-licence.txt",
    "VST3-SDK-MIT.txt",
    "MochiyPopOne-OFL-1.1.txt",
]

def die(msg):
    print("\n!! 中止: " + msg + "\n", file=sys.stderr)
    sys.exit(1)

def note(msg):  print("   " + msg)
def ok(msg):    print("  OK  " + msg)

# ---------------------------------------------------------------- 目印を読む
def read_markers(path: Path):
    """バイナリの中の VOCALGZZIO-EDITION:XXXX をぜんぶ拾う。"""
    data = path.read_bytes()
    found = set()
    for m in re.finditer(re.escape(MARK) + rb"[A-Z]+", data):
        found.add(m.group(0))
    return found

def check_edition(path: Path, edition: str):
    found = read_markers(path)
    if not found:
        die(f"{path.name} に版の目印がありません。\n"
            f"   v4.0.0 より前のバイナリか、ビルドし直しが必要です。\n"
            f"   （Source/PluginProcessor.cpp の VocalGzzioEditionMarker）")
    want = WANT[edition]
    if found != {want}:
        got = ", ".join(sorted(x.decode() for x in found))
        die(f"{path.name} の版が注文と違います。\n"
            f"   ほしい版 : {want.decode()}\n"
            f"   入っていた: {got}\n"
            f"   → build\\ と build_trial\\ を取り違えていないか見てください。\n"
            f"     作り直すなら 配布物をつくる.bat（両方を順に作ります）")
    ok(f"{path.name}: {want.decode()}")

# ---------------------------------------------------------------- 中身を検める
def check_forbidden(files):
    for src, arc in files:
        if src.suffix.lower() not in (".txt", ".md", ".html", ".rtf"):
            continue
        data = src.read_bytes()
        for needle, why in FORBIDDEN_TEXT:
            if needle in data:
                die(f"{arc} … {why}")
    ok("配ってはいけない文面は入っていません")

# ---------------------------------------------------------------- 袋詰め
def add(files, src: Path, arc: str):
    if not src.exists():
        die(f"見つかりません: {src}")
    files.append((src, arc))

def build_zip(files, out: Path):
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for src, arc in sorted(files, key=lambda t: t[1]):
            zi = zipfile.ZipInfo(arc)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.flag_bits |= 0x800          # 日本語ファイル名を UTF-8 と明示
            zi.external_attr = 0o644 << 16
            z.writestr(zi, src.read_bytes())
    return out

def sha256(p: Path):
    h = hashlib.sha256()
    with p.open("rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()

# ---------------------------------------------------------------- 本体
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--edition", required=True, choices=["product", "trial"])
    ap.add_argument("--version", required=True)
    ap.add_argument("--build",  default=None)
    ap.add_argument("--out",    default=str(ROOT / "配布物" / "out"))
    # ★v3.1 は 配布物をつくる.bat が Inno Setup で .exe インストーラーを作る。
    #   zip を作る必要はない。「版が合っているか」だけ見たいときはこれ。
    ap.add_argument("--verify-only", action="store_true",
                    help="zip を作らず、版の照合だけする")
    a = ap.parse_args()

    ed, ver = a.edition, a.version
    jp = "製品版" if ed == "product" else "体験版"
    build = Path(a.build) if a.build else ROOT / ("build" if ed == "product" else "build_trial")
    if not build.is_absolute():
        build = ROOT / build

    print(f"\n===== VocalGzzio v{ver} {jp} を袋詰めします =====")
    note(f"ビルド先: {build}")

    # --- 1. 出来上がったものを探す ---------------------------------
    # ★体験版は CMakeLists が PRODUCT_NAME を "VocalGzzio Trial" に変える
    #   （PLUGIN_CODE も Vcgt。だから製品版と同じPCに共存できる）。
    #   ここで "VocalGzzio.vst3" だけを探すと、体験版のビルドを見つけられないか、
    #   もっと悪いことに**隣に残っている製品版のビルドを拾ってしまう**。
    stem = "VocalGzzio Trial" if ed == "trial" else "VocalGzzio"
    note(f"さがす名前: {stem}")

    vst3_dirs = sorted([p for p in build.rglob(f"{stem}.vst3") if p.is_dir() and "validation" not in p.parts],
                       key=lambda p: p.stat().st_mtime, reverse=True)
    vst3_files = sorted([p for p in build.rglob(f"{stem}.vst3") if p.is_file() and "validation" not in p.parts],
                        key=lambda p: p.stat().st_mtime, reverse=True)
    if not vst3_dirs and not vst3_files:
        die(f"{build} に {stem}.vst3 がありません。\n"
            f"   ビルドは 配布物をつくる.bat（ダブルクリック）が両方作ります。\n"
            f"   個別に作るなら: build.bat clean auto / build.bat trial clean auto")
    vst3 = vst3_dirs[0] if vst3_dirs else vst3_files[0]
    dll = vst3_files[0] if vst3_files else None
    if dll is None:
        die("VST3 の中の実体（DLL）が見つかりません。")

    exes = sorted([p for p in build.rglob(f"{stem}.exe") if p.is_file() and "validation" not in p.parts],
                  key=lambda p: p.stat().st_mtime, reverse=True)
    standalone = exes[0] if exes else None

    note(f"VST3 : {dll.relative_to(build)}")
    note(f"単体 : {standalone.relative_to(build) if standalone else '(なし)'}")

    # --- 2. 版が合っているか（いちばん大事） ------------------------
    print("\n-- 版の照合 --")
    check_edition(dll, ed)
    if standalone:
        check_edition(standalone, ed)
    else:
        note("単体起動版が無いので照合をとばしました（本番では必ず両方入れること）")

    if a.verify_only:
        print("\n===== 版の照合だけ済ませました（zip は作っていません）=====")
        print(f"  {jp}のビルドは正しく {WANT[ed].decode()} です。")
        print("  インストーラーは 配布物をつくる.bat が作ります。\n")
        return

    # --- 3. 中身を組む（公式インストーラー ＋ 手動配置用一式 ＋ 仮想ドライバ同梱） ---
    top = f"VocalGzzio_v{ver}_{jp}"
    out_dir = Path(a.out)
    files = []

    # 3-a. 公式インストーラー（VocalGzzio_Setup_vX.Y.Z.exe / VocalGzzio_Trial_Setup_vX.Y.Z.exe）を必須同梱
    inst_prefix = "VocalGzzio_Trial_Setup_v" if ed == "trial" else "VocalGzzio_Setup_v"
    inst_name = f"{inst_prefix}{ver}.exe"
    inst_candidates = [
        out_dir / inst_name,
        ROOT / "installer_output" / inst_name,
        ROOT / "release" / "windows" / inst_name,
    ]
    installer_exe = next((p for p in inst_candidates if p.is_file()), None)
    if installer_exe is None:
        die(f"公式インストーラー {inst_name} が見つかりません。\n"
            f"   先に tools/package_windows.ps1 または installer.bat release でインストーラーを作成してください。\n"
            f"   （インストーラー無しのZIP配布を防止するため中止しました）")
    inst_sha_path = Path(str(installer_exe) + ".sha256")
    if inst_sha_path.is_file():
        recorded = inst_sha_path.read_text(encoding="utf-8").split()[0].lower()
        actual = sha256(installer_exe).lower()
        if recorded != actual:
            die(f"{inst_name} の SHA-256 が {inst_sha_path.name} と一致しません。")
    add(files, installer_exe, f"{top}/{inst_name}")
    ok(f"公式インストーラー同梱: {inst_name}")

    # 3-b. VST3 プラグイン（手動配置用）
    if vst3.is_dir():
        for p in vst3.rglob("*"):
            if p.is_file():
                add(files, p, f"{top}/VST3プラグイン/{stem}.vst3/{p.relative_to(vst3).as_posix()}")
    else:
        add(files, vst3, f"{top}/VST3プラグイン/{stem}.vst3")

    # 3-c. 単体起動アプリ ＋ 専用仮想ドライバ導入ツール（手動起動でもドライバ導入可能にする）
    if not standalone:
        die("単体起動版 (.exe) が見つかりません。本番配布ZIPには単体起動版が必須です。")
    add(files, standalone, f"{top}/単体起動アプリ/{stem}.exe")

    wdm_candidates = [
        standalone.parent / "VocalGzzioWdmInstaller.exe",
        build / "Release" / "VocalGzzioWdmInstaller.exe",
        ROOT / "VocalGzzioWdmInstaller.exe",
    ]
    wdm_installer = next((p for p in wdm_candidates if p.is_file()), None)
    if wdm_installer is None:
        die("VocalGzzioWdmInstaller.exe が見つかりません。仮想ドライバ導入ツールの同梱が必須です。")
    add(files, wdm_installer, f"{top}/単体起動アプリ/VocalGzzioWdmInstaller.exe")

    drv_dir = ROOT / "driver"
    for drv_file in ("syncroom.inf", "syncroom.cat", "syncroom.sys"):
        add(files, drv_dir / drv_file, f"{top}/単体起動アプリ/driver/{drv_file}")
    ok("単体起動アプリ用 VocalGzzioWdmInstaller.exe ＋ driver/ 一式同梱")

    # 3-d. ライセンス・説明書・はじめにお読みください
    lic = ROOT / "配布物" / "ライセンス"
    for name in LICENSE_FILES:
        add(files, lic / name, f"{top}/ライセンス/{name}")

    # 最新の docs/manual.html & docs/style.css を優先しつつ、配布物/説明書 内の英語ガイド等もすべて同梱
    manual_html = ROOT / "docs" / "manual.html"
    manual_css = ROOT / "docs" / "style.css"
    if manual_html.is_file():
        add(files, manual_html, f"{top}/説明書/取扱説明書.html")
    if manual_css.is_file():
        add(files, manual_css, f"{top}/説明書/style.css")
    doc = ROOT / "配布物" / "説明書"
    if doc.is_dir():
        already_doc = {arc for _, arc in files}
        for p in doc.rglob("*"):
            if p.is_file():
                arc_path = f"{top}/説明書/{p.relative_to(doc).as_posix()}"
                if arc_path not in already_doc:
                    add(files, p, arc_path)

    readme = ROOT / "配布物" / f"はじめにお読みください_{jp}.txt"
    if readme.exists():
        add(files, readme, f"{top}/はじめにお読みください.txt")
    else:
        die(f"{readme.name} がありません。")

    print("\n-- 中身の検め --")
    check_forbidden(files)
    have = {arc.split("/")[-1] for _, arc in files}
    missing = [n for n in LICENSE_FILES if n not in have]
    if missing:
        die("ライセンス文が足りません: " + ", ".join(missing))
    ok(f"ライセンス文 {len(LICENSE_FILES)} 本すべて同梱")
    ok(f"ファイル数 {len(files)}")

    # --- 4. 書き出し（BOOTH用の別名ZIPと .sha256 も一括同期） ----------------
    out = out_dir / f"{top}.zip"
    build_zip(files, out)
    digest = sha256(out)
    Path(str(out) + ".sha256").write_text(f"{digest}  {out.name}\n", encoding="utf-8")

    alias_names = [f"VocalGzzio_v{ver}_Windows_{jp}.zip"]
    if ed == "trial":
        alias_names.append(f"VocalGzzio_v{ver}_Windows_Trial.zip")
    for alias in alias_names:
        alias_path = out_dir / alias
        shutil.copy2(out, alias_path)
        Path(str(alias_path) + ".sha256").write_text(f"{digest}  {alias_path.name}\n", encoding="utf-8")
    mb = out.stat().st_size / 1024 / 1024
    print(f"\n===== できました =====")
    print(f"  {out}")
    print(f"  {mb:.2f} MB   sha256 {sha256(out)[:16]}…")
    if ed == "trial":
        print("\n  ※ 体験版です。60秒ごとに0.6秒、音量が下がります。")
    else:
        print("\n  ※ 製品版です。無料の場所に置かないでください。")
    print()

if __name__ == "__main__":
    main()
