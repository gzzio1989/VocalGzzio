#!/usr/bin/env python3
"""使い捨てのGitHub Mac実行機で、配布用AUをAppleの検証ホストへ読み込む。"""
import argparse
import os
from pathlib import Path
import plistlib
import subprocess
import sys

INSTALL_ROOT = Path("/")


def require_ci_mac():
    if sys.platform != "darwin" or os.environ.get("GITHUB_ACTIONS") != "true":
        raise ValueError("この検査はGitHubの使い捨てMac実行機専用です。")


def validate_installer(installer, edition, version):
    """実際のPKGを導入し、導入先のAUをAppleのホストに読み込ませる。"""
    require_ci_mac()
    stem = "VocalGzzio Trial" if edition == "trial" else "VocalGzzio"
    targets = [INSTALL_ROOT / "Applications" / (stem + ".app"),
               INSTALL_ROOT / "Library/Audio/Plug-Ins/Components" / (stem + ".component"),
               INSTALL_ROOT / "Library/Audio/Plug-Ins/VST3" / (stem + ".vst3")]
    # 既存製品やユーザー側AUに検査がすり替わらないようにする。
    user_au = Path.home() / "Library/Audio/Plug-Ins/Components" / (stem + ".component")
    if any(path.exists() or path.is_symlink() for path in [*targets, user_au]):
        raise ValueError("検査用の導入先に同名の本体があるため、上書きを中止しました。")
    subprocess.run(["sudo", "-n", "/usr/sbin/installer", "-pkg", str(installer), "-target", "/"],
                   check=True, timeout=180)
    identity = None
    for target in targets:
        with (target / "Contents/Info.plist").open("rb") as source:
            info = plistlib.load(source)
        expected_id = "com.gzzio.vocalgzzio" + (".trial" if edition == "trial" else "")
        if (info.get("CFBundleName") != stem or info.get("CFBundleIdentifier") != expected_id
                or info.get("CFBundleShortVersionString") != version):
            raise ValueError("導入された本体の名前・識別子・版番号が違います。")
        executable = info.get("CFBundleExecutable", "")
        if not executable or Path(executable).name != executable:
            raise ValueError("導入された本体の実行ファイル名が不正です。")
        binary = target / "Contents/MacOS" / executable
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise ValueError("導入された本体の実行ファイル・実行権限がありません。")
        if target.suffix == ".component":
            identity = validate_identity(info, edition, version)
    subprocess.run(["/usr/bin/auval", "-v", *identity], check=True, timeout=180)
    print("インストーラーによる3形式の導入と、導入されたAUの読み込み検査に合格しました。")
    return {"installer_succeeded": True, "installed_formats": ["Standalone", "AU", "VST3"],
            "auval_native_architecture": "passed", "gatekeeper_download_tested": False,
            "logic_pro_tested": False, "microphone_hardware_tested": False}


def validate_identity(info, edition, version):
    if not isinstance(info, dict):
        raise ValueError("AUの情報ファイルが辞書形式ではありません。")
    expected_name = "VocalGzzio Trial" if edition == "trial" else "VocalGzzio"
    expected_bundle = "com.gzzio.vocalgzzio" + (".trial" if edition == "trial" else "")
    components = info.get("AudioComponents", [])
    if (info.get("CFBundleName") != expected_name
            or info.get("CFBundleIdentifier") != expected_bundle
            or info.get("CFBundleShortVersionString") != version
            or not isinstance(components, list) or len(components) != 1
            or not isinstance(components[0], dict)):
        raise ValueError("AUの名前・版番号・識別子が配布指定と一致しません。")
    component = components[0]
    expected = ("aumf", "Vcgt" if edition == "trial" else "Vcgj", "Gzzo")
    actual = tuple(component.get(key) for key in ("type", "subtype", "manufacturer"))
    if actual != expected:
        raise ValueError("AUの形式・版・開発元コードが一致しません。")
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--edition", choices=("product", "trial"), required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    # ローカルの利用者のAUフォルダーには書き込まない。
    require_ci_mac()
    bundle = args.bundle.resolve(strict=True)
    if bundle.suffix != ".component":
        raise ValueError("AUバンドルを指定してください。")
    with (bundle / "Contents" / "Info.plist").open("rb") as source:
        identity = validate_identity(plistlib.load(source), args.edition, args.version)
    folder = Path.home() / "Library" / "Audio" / "Plug-Ins" / "Components"
    target = folder / bundle.name
    if target.exists() or target.is_symlink():
        raise ValueError("検査先に同名AUがあるため上書きを中止しました。")
    folder.mkdir(parents=True, exist_ok=True)
    subprocess.run(["ditto", str(bundle), str(target)], check=True, timeout=60)
    subprocess.run(["/usr/bin/auval", "-v", *identity], check=True, timeout=180)
    print("AUをAppleの検証ホストへ読み込み、検査に合格しました。")
    print("検査対象はこの実行機のCPUです。公証・利用者環境・Logic Proでの試奏確認とは別です。")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"AU読み込み検査を中止: {error}", file=sys.stderr)
        sys.exit(1)
