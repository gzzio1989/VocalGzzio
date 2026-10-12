"""Mac配布用の署名・公証。既存キーチェーンを使い、安全設定は変更しない。

現在のVocalGzzioは各バンドルに実行ファイルが一つだけある構成を想定する。
入れ子のコードが増えた場合は黙って再帰署名せず、署名順序の設計をやり直す。
このモジュールの検査成功は、録音ソフトや実機での動作確認を代替しない。

Appleの一次資料（2026-10-11確認）:
https://developer.apple.com/documentation/xcode/creating-distribution-signed-code-for-the-mac
https://developer.apple.com/documentation/security/customizing-the-notarization-workflow
https://developer.apple.com/documentation/security/resolving-common-notarization-issues
https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.device.audio-input
"""
import hashlib
import json
import math
import os
from pathlib import Path
import plistlib
import re
import subprocess
import sys
import tempfile
import uuid


AUDIO_INPUT = "com.apple.security.device.audio-input"
MACHO_MAGIC = {bytes.fromhex(value) for value in (
    "feedface", "cefaedfe", "feedfacf", "cffaedfe",
    "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}


class SigningError(ValueError):
    """署名・公証が確実に確認できなかった。配布を中止する。"""


class CommandError(SigningError):
    def __init__(self, message, stdout="", stderr=""):
        super().__init__(message)
        self.stdout = stdout
        self.stderr = stderr


def _require(condition, message):
    if not condition:
        raise SigningError(message)


def _text(value):
    return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value or ""


def _run(*arguments, check=True, timeout=180):
    _require(sys.platform == "darwin", "署名・公証の実処理はMac上で実行してください。")
    environment = dict(os.environ, LC_ALL="C", LANG="C")
    try:
        result = subprocess.run([str(value) for value in arguments], capture_output=True,
                                text=True, encoding="utf-8", errors="replace",
                                env=environment, timeout=timeout, check=False)
    except subprocess.TimeoutExpired as error:
        raise CommandError("処理が時間内に完了しませんでした。成功とは扱いません。",
                           _text(error.stdout), _text(error.stderr)) from error
    except OSError as error:
        raise CommandError("必要なMac用ツールを実行できませんでした。", stderr=str(error)) from error
    if check and result.returncode:
        raise CommandError(f"{arguments[0]} の検査・処理が失敗しました。",
                           result.stdout, result.stderr)
    return result


def validate_identity(identity, team_id, kind="Application"):
    """完全な証明書名かSHA-1識別子のみ。実際の証明書は署名後も検査する。"""
    _require(re.fullmatch(r"[A-Z0-9]{10}", team_id or ""), "Team IDは英大文字・数字10文字が必要です。")
    _require(kind in ("Application", "Installer"), "証明書の種類が不正です。")
    named = re.fullmatch(r"Developer ID " + kind + r": [^\r\n]+ \(" + team_id + r"\)", identity or "")
    fingerprint = re.fullmatch(r"[0-9a-fA-F]{40}", identity or "")
    _require(named or fingerprint, f"Developer ID {kind}の完全な証明書名かSHA-1識別子が必要です。")


def _single_executable(bundle):
    bundle = Path(bundle).absolute()
    _require(bundle.suffix in (".app", ".component", ".vst3") and bundle.is_dir()
             and not bundle.is_symlink(), "署名対象のバンドルが不正です。")
    for path in bundle.rglob("*"):
        _require(not path.is_symlink(), "リンクを含むバンドルは署名構成の確認が必要です。")
    try:
        info = plistlib.loads((bundle / "Contents" / "Info.plist").read_bytes())
    except (OSError, ValueError, plistlib.InvalidFileException) as error:
        raise SigningError("バンドルの情報ファイルを読めません。") from error
    _require(isinstance(info, dict), "バンドルの情報ファイルが辞書形式ではありません。")
    executable = info.get("CFBundleExecutable", "")
    _require(isinstance(executable, str) and executable not in ("", ".", "..")
             and "/" not in executable and "\\" not in executable, "実行ファイル名が不正です。")
    binary = bundle / "Contents" / "MacOS" / executable
    _require(binary.is_file(), "本体の実行ファイルがありません。")
    with binary.open("rb") as source:
        _require(source.read(4) in MACHO_MAGIC, "本体がMac用の実行形式ではありません。")
    for path in bundle.rglob("*"):
        if path.is_file() and path != binary:
            with path.open("rb") as source:
                prefix = source.read(4)
            _require(prefix not in MACHO_MAGIC and not prefix.startswith(b"#!"),
                     f"入れ子の実行コードは明示的な署名設計が必要です: {path.relative_to(bundle)}")
    if bundle.suffix == ".app":
        _require(isinstance(info.get("NSMicrophoneUsageDescription"), str)
                 and info["NSMicrophoneUsageDescription"].strip(), "単体アプリにマイク使用の説明が必要です。")
    return bundle, binary


def _signature_details(display, team_id):
    display = display.replace("\r\n", "\n")
    authorities = re.findall(r"^Authority=(.+)$", display, re.MULTILINE)
    _require(authorities and re.fullmatch(
        r"Developer ID Application: .+ \(" + re.escape(team_id) + r"\)", authorities[0]),
        "開発元の証明書がDeveloper ID Applicationではないか、Team IDが一致しません。")
    teams = re.findall(r"^TeamIdentifier=(.+)$", display, re.MULTILINE)
    _require(teams == [team_id], "署名のTeam IDが一致しません。")
    flags = re.search(r"\bflags=0x([0-9a-fA-F]+)\b", display)
    _require(flags is not None and int(flags.group(1), 16) & 0x10000,
             "保護された実行環境が有効になっていません。")
    timestamp = re.search(r"^Timestamp=(.+)$", display, re.MULTILINE)
    _require(timestamp is not None and timestamp.group(1).strip()
             and timestamp.group(1).strip().lower() not in ("none", "not set"),
             "安全なタイムスタンプを確認できません。")
    return {"authority": authorities[0], "team_id": team_id, "timestamp": timestamp.group(1)}


def verify_bundle_signature(bundle, team_id):
    """両CPUごとに署名元・Team ID・実行保護・権限を確認する。"""
    validate_identity("0" * 40, team_id)
    bundle, binary = _single_executable(bundle)
    _run("codesign", "--verify", "--deep", "--strict", "--all-architectures", "--verbose=2", bundle)
    architectures = _run("lipo", "-archs", binary).stdout.split()
    _require(set(architectures) == {"arm64", "x86_64"} and len(architectures) == 2,
             "Apple SiliconとIntelの両方の署名確認が必要です。")
    expected = {AUDIO_INPUT: True} if bundle.suffix == ".app" else {}
    details = {}
    for architecture in sorted(architectures):
        output = _run("codesign", "--display", "--verbose=4", "--arch", architecture, bundle)
        details[architecture] = _signature_details(output.stdout + "\n" + output.stderr, team_id)
        output = _run("codesign", "--display", "--entitlements", "-", "--xml", "--arch", architecture, bundle)
        try:
            entitlements = plistlib.loads(output.stdout.encode("utf-8")) if output.stdout.strip() else {}
        except (ValueError, plistlib.InvalidFileException) as error:
            raise SigningError("署名された権限情報を確認できません。") from error
        # --entitlements - --xml は標準出力にXMLを出す。標準エラーの診断文を
        # 権限として解析しないが、出力先の異常を「権限なし」と誤認もしない。
        _require(output.stdout.strip() or ("<plist" not in output.stderr and "<dict>" not in output.stderr),
                 "署名された権限情報の出力先を確認できません。")
        _require(isinstance(entitlements, dict) and entitlements == expected
                 and all(value is True for value in entitlements.values()),
                 "署名の権限がVocalGzzioに必要なものと一致しません。")
    return {"developer_id_signed": True, "team_id": team_id, "architectures": details}


def sign_bundle(bundle, identity, team_id):
    """Developer IDで本体を署名する。証明書・秘密鍵は既存キーチェーン内に置く。"""
    validate_identity(identity, team_id)
    bundle, _ = _single_executable(bundle)
    with tempfile.TemporaryDirectory(prefix="vocalgzzio-entitlements-") as temporary:
        arguments = ["codesign", "--force", "--sign", identity, "--options", "runtime", "--timestamp"]
        if bundle.suffix == ".app":
            entitlements = Path(temporary) / "audio-input.plist"
            entitlements.write_bytes(plistlib.dumps({AUDIO_INPUT: True}, fmt=plistlib.FMT_XML))
            arguments += ["--entitlements", entitlements]
        _run(*arguments, bundle)
    return verify_bundle_signature(bundle, team_id)


def _sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _save_command_log(directory, stem, result):
    (directory / (stem + ".stdout.txt")).write_text(_text(result.stdout), encoding="utf-8")
    (directory / (stem + ".stderr.txt")).write_text(_text(result.stderr), encoding="utf-8")


def notarize(archive, keychain_profile, log_directory, *, timeout_seconds=3600):
    """Acceptedと対応する公証ログの取得を確認した場合だけ結果を返す。

    再試行でも過去の結果を流用しない。各要求の記録を別フォルダーに保存する。
    パスワード・秘密鍵は引数に受け取らず、保存済みのプロファイル名だけを使う。
    """
    archive = Path(archive).absolute()
    _require(archive.suffix in (".zip", ".pkg") and archive.is_file()
             and not archive.is_symlink(), "公証対象のZIPまたはPKGがありません。")
    _require(isinstance(keychain_profile, str) and keychain_profile.strip()
             and not keychain_profile.startswith("-") and not re.search(r"[\x00-\x1f]", keychain_profile),
             "保存済みキーチェーンのプロファイル名が必要です。")
    _require(isinstance(timeout_seconds, (int, float)) and not isinstance(timeout_seconds, bool)
             and math.isfinite(timeout_seconds) and timeout_seconds > 0, "待機時間が不正です。")
    directory = Path(log_directory).absolute() / ("notary-" + uuid.uuid4().hex)
    directory.mkdir(parents=True, exist_ok=False)
    checksum = _sha256(archive)
    (directory / "request.json").write_text(json.dumps(
        {"archive": archive.name, "sha256": checksum}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    try:
        result = _run("xcrun", "notarytool", "submit", archive, "--keychain-profile", keychain_profile,
                      "--wait", "--output-format", "json", check=False, timeout=timeout_seconds)
    except CommandError as error:
        _save_command_log(directory, "submission", error)
        raise SigningError(f"公証の完了を確認できません。記録: {directory}") from error
    _save_command_log(directory, "submission", result)
    try:
        submission = json.loads(result.stdout)
    except (ValueError, TypeError) as error:
        raise SigningError(f"公証結果の形式が不正です。記録: {directory}") from error
    _require(isinstance(submission, dict), f"公証結果の形式が不正です。記録: {directory}")
    submission_id = submission.get("id", "")
    _require(isinstance(submission_id, str) and re.fullmatch(
        r"[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}", submission_id),
        f"公証の要求番号を確認できません。記録: {directory}")
    log_path = directory / "notary-log.json"
    try:
        log_result = _run("xcrun", "notarytool", "log", submission_id,
                          "--keychain-profile", keychain_profile, log_path, check=False)
    except CommandError as error:
        _save_command_log(directory, "log-download", error)
        raise SigningError(f"公証ログを取得できません。記録: {directory}") from error
    _save_command_log(directory, "log-download", log_result)
    _require(result.returncode == 0 and submission.get("status") == "Accepted",
             f"公証は承認済みではありません。記録: {directory}")
    _require(log_result.returncode == 0 and log_path.is_file(), f"公証ログを保存できません。記録: {directory}")
    try:
        report = json.loads(log_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise SigningError(f"公証ログを確認できません。記録: {directory}") from error
    # Appleの出力では、同じUUIDでも送信結果とログで文字の大小が異なる。
    # IDとダイジェストを正規化して照合し、別の要求・別のファイルは拒否する。
    _require(isinstance(report, dict), f"公証ログの形式が不正です。記録: {directory}")
    report_id = report.get("jobId")
    report_checksum = report.get("sha256")
    try:
        same_request = isinstance(report_id, str) and uuid.UUID(report_id) == uuid.UUID(submission_id)
    except (ValueError, AttributeError):
        same_request = False
    _require(report.get("status") == "Accepted"
             and same_request
             and isinstance(report_checksum, str) and report_checksum.lower() == checksum,
             f"公証ログが対象ファイルの承認を証明していません。記録: {directory}")
    _require(_sha256(archive) == checksum, "公証の処理中に対象ファイルが変更されました。")
    return {"status": "Accepted", "submission_id": submission_id,
            "sha256": checksum, "log": str(log_path)}


def staple_and_validate(path):
    """公証チケットを添付して検査する。ZIPやプラグインへの添付は行わない。"""
    path = Path(path).absolute()
    _require(not path.is_symlink() and ((path.suffix == ".app" and path.is_dir())
             or (path.suffix == ".pkg" and path.is_file())), "チケット添付の対象はアプリとPKGだけです。")
    _run("xcrun", "stapler", "staple", path)
    _run("xcrun", "stapler", "validate", path)


def verify_installer_signature(installer, team_id):
    """インストーラー証明書の発行元、Team ID、時刻を確認する。"""
    validate_identity("0" * 40, team_id, "Installer")
    installer = Path(installer).absolute()
    _require(installer.suffix == ".pkg" and installer.is_file() and not installer.is_symlink(),
             "署名済みインストーラーがありません。")
    result = _run("pkgutil", "--check-signature", installer)
    output = result.stdout + "\n" + result.stderr
    certificate = re.search(r"^\s*1\. (Developer ID Installer: .+ \(" + re.escape(team_id) + r"\))\s*$",
                            output, re.MULTILINE)
    _require(certificate is not None
             and "Status: signed by a developer certificate issued by Apple for distribution" in output
             and "Signed with a trusted timestamp" in output,
             "インストーラーの証明書・Team ID・タイムスタンプを確認できません。")
    return {"authority": certificate.group(1), "team_id": team_id, "developer_id_signed": True}


def assess_installer(installer):
    """インストール許可の評価のみ。Gatekeeperの設定は一切変更しない。"""
    installer = Path(installer).absolute()
    _require(installer.suffix == ".pkg" and installer.is_file() and not installer.is_symlink(),
             "評価対象のインストーラーがありません。")
    result = _run("spctl", "--assess", "--type", "install", "--verbose=2", installer)
    _require(re.search(r"^source=Notarized Developer ID\s*$", result.stdout + "\n" + result.stderr,
                       re.MULTILINE), "公証済みの開発元としてインストール可能か確認できません。")
