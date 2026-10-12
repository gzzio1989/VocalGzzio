#!/usr/bin/env python3
"""Mac上で、版を検証してからインストーラーと配布ZIPを作る。"""
import argparse
import hashlib
import html
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

import macos_signing as signing
from validate_macos_au import validate_installer

ROOT = Path(__file__).resolve().parent.parent
ARCHITECTURES = {"arm64", "x86_64"}
LICENSES = ("使用許諾契約書.txt", "THIRD-PARTY-NOTICES.txt",
            "JUCE-8-licence.txt", "VST3-SDK-MIT.txt", "MochiyPopOne-OFL-1.1.txt")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def run(*arguments):
    return subprocess.check_output([str(a) for a in arguments], text=True,
                                   encoding="utf-8", timeout=300).strip()


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
    if sys.platform == "darwin":
        require(binary.stat().st_mode & 0o111, f"実行権限がありません: {binary}")
    if bundle.suffix == ".app":
        require(isinstance(info.get("NSMicrophoneUsageDescription"), str)
                and info["NSMicrophoneUsageDescription"].strip(),
                "単体アプリにマイクの使用許可説明がありません。")
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


def signing_configuration(args):
    """指定が一部だけの場合も中断し、未公証の方式へ切り替えない。"""
    names = ("signing_identity", "installer_identity", "team_id", "notary_profile")
    values = [getattr(args, name, None) for name in names]
    if all(value is None for value in values):
        return None
    require(all(isinstance(value, str) and value.strip() for value in values),
            "正式配布には署名用証明書、インストーラー用証明書、Team ID、公証用プロファイルの全指定が必要です。")
    signing.validate_identity(args.signing_identity, args.team_id)
    signing.validate_identity(args.installer_identity, args.team_id, "Installer")
    require(not args.notary_profile.startswith("-")
            and not re.search(r"[\x00-\x1f]", args.notary_profile),
            "公証用の保存済みプロファイル名が不正です。")
    return dict(zip(names, values))


def write_json(path, details):
    path.write_text(json.dumps(details, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def signed_readme(version, label, installer_name):
    # 現在の未公証版向け説明を、将来の正式配布物に混ぜない。
    return f"""# VocalGzzio {version} Mac {label}

## インストール

1. 録音ソフトとVocalGzzioの単体アプリを終了してください。
2. 同梱の「{installer_name}」を開いて、画面の案内に従ってください。
3. Logic Proなどの録音ソフトを起動して、VocalGzzioを選んでください。
   単体で使う場合は、アプリケーションフォルダーのVocalGzzioを起動します。

`.component` と `.vst3` は録音ソフトが読み込む部品です。直接開く必要はありません。
本体は Apple Silicon と Intel の両方に対応し、macOS 11以降を対象とします。
AUは `/Library/Audio/Plug-Ins/Components`、VST3は
`/Library/Audio/Plug-Ins/VST3`、単体アプリは `/Applications` に入ります。

## 署名と公証の確認

この配布物は、本体の開発元署名と公証、インストーラーの開発元署名と公証、
公証チケットの添付、およびMacのインストール許可の検査を通過した場合だけ作成されます。
結果は配布ZIPの「配布情報.json」に記録しています。
これらは各録音ソフトやお使いのMacでの動作を保証する検査ではありません。

## 読み込めない場合

インストール後もLogic Proに出ない場合はMacを再起動し、プラグインマネージャで
VocalGzzioだけを選んで再スキャンしてください。
警告が出る場合は無理に開かず、警告の全文、macOS・録音ソフト・本体の版をお知らせください。
Mac全体のセキュリティ設定を無効にする必要はありません。

機能の説明は「説明書/説明書.html」を開いてください。
体験版では約60秒ごとに約0.6秒音量が下がり、設定の保存・復元を制限しています。
製品の配布元: https://gzzio.booth.pm/items/8764003
"""


def signed_install_html(version, installer_name):
    """未公証版の説明を流用せず、この配布物に合う導入ページを作る。"""
    version = html.escape(version)
    installer_name = html.escape(installer_name)
    return f"""<!doctype html>
<html lang="ja"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>VocalGzzio {version}・Macへの導入</title>
<style>body{{font:18px/1.8 system-ui,sans-serif;max-width:48rem;margin:3rem auto;padding:0 1.5rem;color:#222}}a{{color:#075ba5}}li{{margin:.8rem 0}}code{{overflow-wrap:anywhere}}</style>
<main><h1>VocalGzzio {version}をMacで使う</h1>
<ol><li>録音ソフトとVocalGzzioの単体アプリを終了します。</li>
<li>配布ZIPの「<strong>{installer_name}</strong>」を開き、案内に従います。</li>
<li>Logic Proなどの録音ソフトを起動し、VocalGzzioを選びます。
単体で使う場合はアプリケーションフォルダーのVocalGzzioを起動します。</li></ol>
<p><code>.component</code> と <code>.vst3</code> は録音ソフトが読み込む部品です。直接開く必要はありません。</p>
<h2>読み込めないとき</h2><p>Macを再起動し、Logic ProのプラグインマネージャでVocalGzzioだけを選んで再スキャンしてください。
警告が出る場合は無理に開かず、警告の全文、macOSと録音ソフトの版をお知らせください。Mac全体のセキュリティ設定を無効にする必要はありません。</p>
<h2>この配布物の検査</h2><p>開発元署名、公証、公証チケットの添付、インストール許可の検査を通過した配布物です。
各録音ソフトやお使いのMacでの動作確認とは別の検査です。結果は配布ZIPの「配布情報.json」に記録しています。</p>
<p>Apple Silicon・Intelの両方、macOS 11以降が対象です。
体験版は約60秒ごとに約0.6秒音量が下がり、設定の保存・復元を制限しています。</p>
<p><a href="説明書.html">機能の説明に戻る</a> ／ <a href="https://gzzio.booth.pm/items/8764003">配布元・お問い合わせ</a></p></main></html>
"""


def unsigned_readme(version, label, installer_name):
    return f"""# VocalGzzio {version} Mac {label}

まず「ここから始める.html」を開いてください。

1. 録音ソフトとVocalGzzioを終了します。
2. Finderで「{installer_name}」を開き、画面に従って導入します。
3. 単体で使う場合はアプリケーションフォルダーのVocalGzzioを起動します。
   Logic Proなどで使う場合は、そのソフト内でVocalGzzioを選びます。

この配布物はAppleの開発元署名・公証を受けていません。初回にMacが拒否する場合があります。
警告の扱いと導入後の確認は「ここから始める.html」をご覧ください。
.component と .vst3 は録音ソフト用の部品です。直接開く必要はありません。

Apple SiliconとIntelの両方、macOS 11以降が対象です。
単体アプリは /Applications、AUは /Library/Audio/Plug-Ins/Components、
VST3は /Library/Audio/Plug-Ins/VST3 に導入します。
機能の説明は「説明書/説明書.html」にあります。
製品の配布元: https://gzzio.booth.pm/items/8764003
"""


def unsigned_install_html(version, installer_name, stem):
    version, installer_name, stem = map(html.escape, (version, installer_name, stem))
    return f"""<!doctype html>
<html lang="ja"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>VocalGzzio {version}・Macへの導入</title>
<style>body{{font:18px/1.8 system-ui,sans-serif;max-width:48rem;margin:3rem auto;padding:0 1.5rem;color:#222}}a{{color:#075ba5}}li{{margin:.8rem 0}}code,strong{{overflow-wrap:anywhere}}aside{{padding:1rem 1.3rem;background:#fff4db;border-left:5px solid #a66b00}}</style>
<main><h1>VocalGzzio {version}をMacで使う</h1>
<aside>この版はAppleの開発元署名・公証を受けていません。インストーラーを用意していますが、初回の警告が出なくなる修正は未完了です。</aside>
<h2>1. インストーラーを開く</h2><ol>
<li>録音ソフトとVocalGzzioを終了します。</li>
<li>Finderで、同じフォルダーの「<strong>{installer_name}</strong>」を開きます。</li>
<li>画面の案内に従ってインストールします。<code>.component</code> や <code>.vst3</code> を直接開く操作は不要です。</li></ol>
<h2>初回の警告で止まった場合</h2>
<p>BOOTHの正規配布物であることを確認し、ご自身で起動を許可する場合だけ、
インストーラーを開こうとした後に「システム設定」→「プライバシーとセキュリティ」から対象名を確認して「このまま開く」を選びます。
表示が違う、または対象を確認できない場合は、警告画面を添えて配布元へお問い合わせください。
「コンピュータに損害を与えます」「壊れています」という警告は、この手順で進めずお問い合わせください。</p>
<p><a href="https://support.apple.com/ja-jp/102445">Appleによる警告と起動許可の説明</a></p>
<h2>2. 使い方に合わせて起動する</h2>
<p><strong>単体アプリ：</strong>アプリケーションフォルダーの「{stem}」を開きます。
マイクの使用を求められたら許可し、音声設定で使う機器の入力・出力を選びます。
許可しなかった場合は「プライバシーとセキュリティ」→「マイク」で、このアプリを確認してください。</p>
<p><strong>Logic Pro：</strong>オーディオトラックのエフェクトから、Audio Units → Gzzio → {stem} を選びます。
表示されない場合はMacを再起動し、プラグインマネージャでこの製品を選び、リセットして再スキャンします。
失敗する場合は警告全文を添えてお問い合わせください。</p>
<p><a href="https://support.apple.com/ja-jp/122179">Appleによるプラグイン読み込みの案内</a></p>
<h2>3. 音が入ることを確認する</h2>
<p>入力メーターが動くか確認してください。動かない場合は機器の入力選択とマイク許可を確認します。
単体アプリで聞く場合は、出力先も指定します。</p>
<p>Apple Silicon・Intelの両方、macOS 11以降が対象です。体験版には周期的な音量低下と保存制限があります。</p>
<p><a href="説明書.html">機能の説明</a> ／ <a href="https://gzzio.booth.pm/items/8764003">配布元・お問い合わせ</a></p></main></html>
"""


def publish_archive(staged_archive, destination):
    """完成したZIPだけを排他的に作成する。既存ZIP・検証値を上書きしない。"""
    checksum_path = destination.with_suffix(".zip.sha256")
    require(staged_archive.is_file() and staged_archive.stat().st_size > 0,
            "完成した配布ZIPがありません。")
    for target in (destination, checksum_path):
        require(not target.exists() and not target.is_symlink(),
                f"既存の配布物を上書きできません: {target}")
    checksum = sha256(staged_archive)
    owned = []
    try:
        with checksum_path.open("x", encoding="utf-8") as target:
            owned.append(checksum_path)
            target.write(f"{checksum}  {destination.name}\n")
        with destination.open("xb") as target:
            owned.append(destination)
            with staged_archive.open("rb") as source:
                shutil.copyfileobj(source, target)
    except BaseException:
        # この呼出しで作成した不完全なファイルだけを取り除く。
        for target in reversed(owned):
            target.unlink(missing_ok=True)
        raise
    return checksum


def previous_install_html(stem, version):
    stem, version = html.escape(stem), html.escape(version)
    return f"""<h2>以前に手動で導入した方へ</h2>
<p>録音ソフトとVocalGzzioを終了し、ホームフォルダー内の
<code>~/Library/Audio/Plug-Ins/Components</code>、<code>~/Library/Audio/Plug-Ins/VST3</code>、
<code>~/Applications</code> に「{stem}」の旧版が残っていないか確認してください。
残っている場合は、その製品の該当項目だけをデスクトップ等の「VocalGzzio旧版保管」フォルダーへ移動して保管し、
今回のインストーラーを実行します。製品版と体験版は共存できるため、違う版種やほかのプラグイン・設定ファイルは移動しません。</p>
<p>起動後に、本体の版番号が <strong>{version}</strong> であることを確認してください。</p>"""


def package_distribution(args, bundles, stem, label):
    config = signing_configuration(args)
    args.out.mkdir(parents=True, exist_ok=True)
    basename = f"VocalGzzio_v{args.version}_Mac_{label}"
    destination = args.out / f"{basename}.zip"
    for target in (destination, destination.with_suffix(".zip.sha256")):
        require(not target.exists() and not target.is_symlink(),
                f"既存の配布物を上書きできません: {target}")
    with tempfile.TemporaryDirectory(prefix="vocalgzzio-package-") as temporary:
        work = Path(temporary)
        package = work / basename
        payload = work / "payload"
        package.mkdir()
        verified_bundles = []
        # 部品を直接開いてしまわないよう、署名の有無にかかわらずPKGで導入する。
        bundle_root = work / "bundles"
        for bundle, folder, install_path in bundles:
            copied = bundle_root / folder / bundle.name
            copied.parent.mkdir(parents=True)
            run("ditto", bundle, copied)
            if config:
                proof = signing.sign_bundle(copied, config["signing_identity"], config["team_id"])
                require(proof.get("developer_id_signed") is True, "本体の署名確認が完了していません。")
                verified_bundles.append({"name": bundle.name, **proof})
            else:
                # アドホック署名はAppleの開発者署名・公証とは異なる。
                run("codesign", "--force", "--deep", "--sign", "-", copied)
                run("codesign", "--verify", "--deep", "--strict", copied)

        details = {"version": args.version, "edition": args.edition,
                   "architectures": sorted(ARCHITECTURES), "minimum_macos": "11.0",
                   "formats": ["VST3", "AU", "Standalone"],
                   "developer_id_signed": False, "notarized": False,
                   "distribution": "installer_only"}
        log_directory = getattr(args, "notary_log_dir", None) or args.out / "公証記録"
        content_proof = None
        if config:
            submission = work / "signed-bundles.zip"
            run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", bundle_root, submission)
            content_proof = signing.notarize(submission, config["notary_profile"], log_directory)
            require(content_proof.get("status") == "Accepted", "本体の公証が承認されていません。")
            for bundle, folder, _ in bundles:
                copied = bundle_root / folder / bundle.name
                if bundle.suffix == ".app":
                    signing.staple_and_validate(copied)
                signing.verify_bundle_signature(copied, config["team_id"])

        for bundle, folder, install_path in bundles:
            target = payload / install_path / bundle.name
            target.parent.mkdir(parents=True, exist_ok=True)
            run("ditto", bundle_root / folder / bundle.name, target)
            if config:
                signing.verify_bundle_signature(target, config["team_id"])

        documents = package / "説明書"
        documents.mkdir()
        installer_name = f"{basename}" + ("_公証済み.pkg" if config else "_未公証.pkg")
        if config:
            readme = signed_readme(args.version, label, installer_name)
            guide = signed_install_html(args.version, installer_name)
        else:
            readme = unsigned_readme(args.version, label, installer_name)
            guide = unsigned_install_html(args.version, installer_name, stem)
        guide = guide.replace("</main>", previous_install_html(stem, args.version) + "</main>")
        readme += "\n以前に手動導入した方は「ここから始める.html」の旧版についての案内もご確認ください。\n"
        (package / "はじめにお読みください.md").write_text(readme, encoding="utf-8")
        (documents / "mac-install.html").write_text(guide, encoding="utf-8")
        (package / "ここから始める.html").write_text(
            guide.replace('href="説明書.html"', 'href="説明書/説明書.html"'), encoding="utf-8")
        manual = (ROOT / "docs" / "manual.html").read_text(encoding="utf-8").replace(
            'href="https://gzzio1989.github.io/apps/vocalgzzio/mac-install.html"', 'href="mac-install.html"')
        (documents / "説明書.html").write_text(manual, encoding="utf-8")
        shutil.copy2(ROOT / "docs" / "style.css", documents / "style.css")
        license_dir = package / "ライセンス"
        license_dir.mkdir()
        for name in LICENSES:
            shutil.copy2(ROOT / "配布物" / "ライセンス" / name, license_dir / name)
        installed_docs = payload / "Library" / "Application Support" / "Gzzio" / stem
        installed_docs.mkdir(parents=True)
        for document in ("説明書", "ライセンス", "はじめにお読みください.md", "ここから始める.html"):
            run("ditto", package / document, installed_docs / document)
        if config:
            # ここで確認済みなのは本体の公証まで。最終PKGの成功情報を先取りしない。
            code_details = {**details, "scope": "application_and_plugins",
                            "developer_id_signed": True, "notarized": True,
                            "team_id": config["team_id"], "bundles": verified_bundles,
                            "notarization": {key: content_proof[key] for key in
                                             ("status", "submission_id", "sha256")}}
            write_json(installed_docs / "本体検証情報.json", code_details)
        else:
            write_json(installed_docs / "配布情報.json", details)

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
        installer = package / installer_name
        arguments = ["pkgbuild", "--root", payload, "--component-plist", components,
                     "--identifier", identifier, "--version", args.version,
                     "--install-location", "/", "--ownership", "recommended"]
        if config:
            arguments += ["--sign", config["installer_identity"], "--timestamp"]
        run(*arguments, installer)
        if config:
            installer_signature = signing.verify_installer_signature(installer, config["team_id"])
            require(installer_signature.get("developer_id_signed") is True,
                    "インストーラーの署名確認が完了していません。")
            installer_proof = signing.notarize(installer, config["notary_profile"], log_directory)
            require(installer_proof.get("status") == "Accepted", "インストーラーの公証が承認されていません。")
            signing.staple_and_validate(installer)
            signing.verify_installer_signature(installer, config["team_id"])
            signing.assess_installer(installer)
            details.update(developer_id_signed=True, notarized=True, team_id=config["team_id"],
                           distribution="installer_only", bundles=verified_bundles,
                           installer={"name": installer.name, "sha256": sha256(installer),
                                      "signature": installer_signature, "ticket_validated": True,
                                      "gatekeeper_install_assessment": "accepted",
                                      "notarization": {key: installer_proof[key] for key in
                                                       ("status", "submission_id", "sha256")}},
                           content_notarization={key: content_proof[key] for key in
                                                 ("status", "submission_id", "sha256")})
        if getattr(args, "validate_install", False):
            details["ci_install_validation"] = validate_installer(installer, args.edition, args.version)
        write_json(package / "配布情報.json", details)
        staged_archive = work / "distribution.zip"
        run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", package, staged_archive)
        checksum = publish_archive(staged_archive, destination)
    print(f"作成しました: {destination}\nSHA-256: {checksum}")
    if config:
        print("開発元署名・公証・公証チケット・インストール許可の検査を通過しました。実機と録音ソフトの動作確認は別に必要です。")
    else:
        print("Apple開発者署名・公証は未実施です。実機での起動・録音ソフトへの読込み確認が必要です。")
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=Path("release/macos"))
    parser.add_argument("--edition", choices=("product", "trial"), required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--public-artifact", action="store_true")
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--validate-install", action="store_true", help="使い捨てGitHub Mac実行機でPKG導入とAU読込みを実行")
    parser.add_argument("--signing-identity", help="Developer ID Applicationの完全な証明書名またはSHA-1識別子")
    parser.add_argument("--installer-identity", help="Developer ID Installerの完全な証明書名またはSHA-1識別子")
    parser.add_argument("--team-id", help="開発元のTeam ID")
    parser.add_argument("--notary-profile", help="既存キーチェーンに保存済みの公証用プロファイル名")
    parser.add_argument("--notary-log-dir", type=Path, help="公証記録の保存先。既定は出力先の公証記録フォルダー")
    args = parser.parse_args()
    require(sys.platform == "darwin", "Mac用の配布物はMac上で作成してください。")
    require(re.fullmatch(r"\d+\.\d+\.\d+", args.version), "版番号の形式が不正です。")
    require(not args.public_artifact or args.edition == "trial",
            "製品版は公開リポジトリの成果物に含められません。")
    signing_configuration(args)
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
    package_distribution(args, bundles, stem, label)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"中止: {error}", file=sys.stderr)
        sys.exit(1)
