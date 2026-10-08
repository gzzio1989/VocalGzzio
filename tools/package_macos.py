#!/usr/bin/env python3
"""Mac上で、版を検証してからインストーラーと配布ZIPを作る。"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
ARCHITECTURES = {"arm64", "x86_64"}
LICENSES = ("使用許諾契約書.txt", "THIRD-PARTY-NOTICES.txt",
            "JUCE-8-licence.txt", "VST3-SDK-MIT.txt", "MochiyPopOne-OFL-1.1.txt")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def run(*arguments):
    return subprocess.check_output([str(a) for a in arguments], text=True).strip()


def verify_marker(data, edition):
    markers = set(re.findall(rb"VOCALGZZIO-EDITION:[A-Z]+", data))
    expected = b"VOCALGZZIO-EDITION:" + edition.upper().encode("ascii")
    require(markers == {expected}, "バイナリの版が指定と違うか、版の目印がありません。")


def verify_cache(build, edition):
    text = (build / "CMakeCache.txt").read_text(encoding="utf-8-sig")
    values = dict(re.findall(r"^([^#/\s][^:=]*):[^=]+=(.*)$", text, re.MULTILINE))
    truth = {"1", "ON", "TRUE", "YES"}
    require(values.get("VOCALGZZIO_BUILD_TESTS", "").upper() not in truth,
            "検査用バイナリは配布できません。検査を無効にした別のビルドを使ってください。")
    require(values.get("VOCALGZZIO_LITE", "").upper() not in truth,
            "機能制限版は製品版・体験版として配布できません。")
    require((values.get("VOCALGZZIO_TRIAL", "").upper() in truth) == (edition == "trial"),
            "ビルド設定と指定した版が一致しません。")


def verify_bundle(bundle, edition, version):
    require(bundle.is_dir(), f"見つかりません: {bundle}")
    with (bundle / "Contents" / "Info.plist").open("rb") as source:
        info = plistlib.load(source)
    require(info.get("CFBundleShortVersionString") == version,
            f"{bundle.name} の版番号が {version} と一致しません。")
    executable = info.get("CFBundleExecutable", "")
    require(executable and Path(executable).name == executable,
            f"{bundle.name} の実行ファイル名が不正です。")
    binary = bundle / "Contents" / "MacOS" / executable
    require(binary.is_file(), f"実行ファイルがありません: {binary}")
    require(set(run("lipo", "-archs", binary).split()) == ARCHITECTURES,
            f"{bundle.name} にApple Silicon版とIntel版の両方が必要です。")
    with tempfile.TemporaryDirectory(prefix="vocalgzzio-verify-") as temporary:
        for architecture in sorted(ARCHITECTURES):
            thin = Path(temporary) / architecture
            run("lipo", binary, "-thin", architecture, "-output", thin)
            try:
                verify_marker(thin.read_bytes(), edition)
            except ValueError as error:
                raise ValueError(f"{bundle.name} / {architecture}: {error}") from error
    return binary


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=Path("release/macos"))
    parser.add_argument("--edition", choices=("product", "trial"), required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--public-artifact", action="store_true")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    require(sys.platform == "darwin", "Mac用の配布物はMac上で作成してください。")
    require(re.fullmatch(r"\d+\.\d+\.\d+", args.version), "版番号の形式が不正です。")
    require(not args.public_artifact or args.edition == "trial",
            "製品版は公開リポジトリの成果物に含められません。")
    verify_cache(args.build, args.edition)
    source_version = re.search(r"project\(VocalGzzio\s+VERSION\s+(\d+\.\d+\.\d+)",
                               (ROOT / "CMakeLists.txt").read_text(encoding="utf-8-sig")).group(1)
    require(source_version == args.version, "ソースの版番号と指定した版番号が違います。")
    stem = "VocalGzzio Trial" if args.edition == "trial" else "VocalGzzio"
    label = "体験版" if args.edition == "trial" else "製品版"
    artefacts = args.build / "VocalGzzio_artefacts" / "Release"
    bundles = ((artefacts / "VST3" / f"{stem}.vst3", "VST3", "Library/Audio/Plug-Ins/VST3"),
               (artefacts / "AU" / f"{stem}.component", "AU", "Library/Audio/Plug-Ins/Components"),
               (artefacts / "Standalone" / f"{stem}.app", "単体起動", "Applications"))
    for bundle, _, _ in bundles:
        verify_bundle(bundle, args.edition, args.version)
    if args.verify_only:
        print(f"{args.version} {label}: 3形式・2種類のCPUの版を確認しました。")
        return
    args.out.mkdir(parents=True, exist_ok=True)
    basename = f"VocalGzzio_v{args.version}_Mac_{label}"
    destination = args.out / f"{basename}.zip"
    require(not destination.exists(), f"既存の配布物を上書きできません: {destination}")
    with tempfile.TemporaryDirectory(prefix="vocalgzzio-package-") as temporary:
        work = Path(temporary)
        package = work / basename
        payload = work / "payload"
        package.mkdir()
        for bundle, folder, install_path in bundles:
            manual = package / "手動インストール" / folder / bundle.name
            manual.parent.mkdir(parents=True)
            run("ditto", bundle, manual)
            # アドホック署名はAppleの開発者署名・公証とは異なる。
            run("codesign", "--force", "--deep", "--sign", "-", manual)
            run("codesign", "--verify", "--deep", "--strict", manual)
            target = payload / install_path / bundle.name
            target.parent.mkdir(parents=True, exist_ok=True)
            run("ditto", manual, target)
        documents = package / "説明書"
        documents.mkdir()
        shutil.copy2(ROOT / "配布物" / "Macでの導入手順.md", package / "はじめにお読みください.md")
        shutil.copy2(ROOT / "docs" / "manual.html", documents / "説明書.html")
        shutil.copy2(ROOT / "docs" / "style.css", documents / "style.css")
        license_dir = package / "ライセンス"
        license_dir.mkdir()
        for name in LICENSES:
            shutil.copy2(ROOT / "配布物" / "ライセンス" / name, license_dir / name)
        details = {"version": args.version, "edition": args.edition,
                   "architectures": sorted(ARCHITECTURES), "minimum_macos": "11.0",
                   "formats": ["VST3", "AU", "Standalone"],
                   "developer_id_signed": False, "notarized": False}
        (package / "配布情報.json").write_text(json.dumps(details, ensure_ascii=False, indent=2) + "\n",
                                                encoding="utf-8")
        installed_docs = payload / "Library" / "Application Support" / "Gzzio" / stem
        installed_docs.mkdir(parents=True)
        for document in ("説明書", "ライセンス", "はじめにお読みください.md", "配布情報.json"):
            run("ditto", package / document, installed_docs / document)
        components = work / "components.plist"
        run("pkgbuild", "--analyze", "--root", payload, components)
        with components.open("rb") as source:
            component_data = plistlib.load(source)
        for component in component_data:
            component["BundleIsRelocatable"] = False
            component["BundleOverwriteAction"] = "upgrade"
        with components.open("wb") as target:
            plistlib.dump(component_data, target)
        identifier = "com.gzzio.vocalgzzio" + (".trial" if args.edition == "trial" else "") + ".installer"
        installer = package / f"{basename}_未公証.pkg"
        run("pkgbuild", "--root", payload, "--component-plist", components,
            "--identifier", identifier, "--version", args.version,
            "--install-location", "/", "--ownership", "recommended", installer)
        run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", package, destination)
    checksum = sha256(destination)
    destination.with_suffix(".zip.sha256").write_text(f"{checksum}  {destination.name}\n", encoding="utf-8")
    print(f"作成しました: {destination}\nSHA-256: {checksum}")
    print("Apple開発者署名・公証は未実施です。実機での起動・録音ソフトへの読込み確認が必要です。")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"中止: {error}", file=sys.stderr)
        sys.exit(1)
