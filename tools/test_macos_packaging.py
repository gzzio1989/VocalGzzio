"""実際の署名・公証とは分離して、配布工程の中断条件と内容を検査する。"""
import argparse
import contextlib
import io
import json
from pathlib import Path
import plistlib
import shutil
import tempfile
import unittest
from unittest import mock
import zipfile

import package_macos as packaging


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.args = argparse.Namespace(out=self.root / "release", version="4.3.1", edition="product",
            signing_identity=None, installer_identity=None, team_id=None, notary_profile=None,
            notary_log_dir=None)
        source = self.root / "sources"
        self.bundles = []
        for suffix, folder, location in (("vst3", "VST3", "Library/Audio/Plug-Ins/VST3"),
                ("component", "AU", "Library/Audio/Plug-Ins/Components"), ("app", "単体起動", "Applications")):
            bundle = source / f"VocalGzzio.{suffix}"
            bundle.mkdir(parents=True)
            (bundle / "example").write_text("音声本体", encoding="utf-8")
            self.bundles.append((bundle, folder, location))
        self.archive_snapshots = []
        self.installer_snapshots = []

    def signed(self):
        self.args.signing_identity = "Developer ID Application: Example (ABCDEFGHIJ)"
        self.args.installer_identity = "Developer ID Installer: Example (ABCDEFGHIJ)"
        self.args.team_id = "ABCDEFGHIJ"
        self.args.notary_profile = "vocalgzzio-notary"

    def fake_run(self, *arguments):
        command = str(arguments[0])
        if command == "ditto":
            source, destination = map(Path, arguments[-2:])
            if "-c" in arguments:
                names = {str(p.relative_to(source)): p.read_bytes() for p in source.rglob("*") if p.is_file()}
                self.archive_snapshots.append(names)
                with zipfile.ZipFile(destination, "w") as archive:
                    for name, data in names.items():
                        archive.writestr(source.name + "/" + name.replace("\\", "/"), data)
            elif source.is_dir():
                shutil.copytree(source, destination, dirs_exist_ok=True)
            else:
                shutil.copy2(source, destination)
        elif command == "pkgbuild":
            if "--analyze" in arguments:
                Path(arguments[-1]).write_bytes(plistlib.dumps([{"BundleIsRelocatable": True}]))
            else:
                payload = Path(arguments[arguments.index("--root") + 1])
                self.installer_snapshots.append({str(p.relative_to(payload)): p.read_bytes()
                    for p in payload.rglob("*") if p.is_file()})
                Path(arguments[-1]).write_bytes(b"mock signed installer")
        elif command != "codesign":
            raise AssertionError(f"想定外のコマンド: {arguments}")
        return ""

    @contextlib.contextmanager
    def mocked_tools(self, status="Accepted", fail_at=None):
        proof = {"status": status, "submission_id": "00000000-0000-0000-0000-000000000001", "sha256": "a" * 64}
        with contextlib.ExitStack() as stack:
            stack.enter_context(mock.patch.object(packaging, "run", side_effect=self.fake_run))
            methods = {}
            for name in ("sign_bundle", "verify_bundle_signature", "verify_installer_signature", "notarize",
                         "staple_and_validate", "assess_installer"):
                result = proof if name == "notarize" else {"developer_id_signed": True, "team_id": "ABCDEFGHIJ"}
                methods[name] = stack.enter_context(mock.patch.object(packaging.signing, name, return_value=result))
            if fail_at:
                methods[fail_at].side_effect = packaging.signing.SigningError("検査失敗")
            stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
            yield methods

    def package(self):
        return packaging.package_distribution(self.args, self.bundles, "VocalGzzio", "製品版")

    def assert_no_release(self):
        self.assertFalse(list(self.args.out.glob("*.zip")))
        self.assertFalse(list(self.args.out.glob("*.sha256")))

    def test_each_missing_signing_option_fails(self):
        self.signed()
        for name in ("signing_identity", "installer_identity", "team_id", "notary_profile"):
            with self.subTest(name=name):
                old = getattr(self.args, name)
                setattr(self.args, name, None)
                with self.assertRaises(ValueError):
                    packaging.signing_configuration(self.args)
                setattr(self.args, name, old)
        self.assert_no_release()

    def test_empty_and_unsafe_profile_fail(self):
        self.signed()
        for value in ("", "   ", "--password", "profile\nother"):
            with self.subTest(value=value):
                self.args.notary_profile = value
                with self.assertRaises(ValueError):
                    packaging.signing_configuration(self.args)

    def test_unnotarized_metadata_and_guide(self):
        with self.mocked_tools() as methods:
            destination = self.package()
        for tool in methods.values():
            tool.assert_not_called()
        self.assertTrue(destination.is_file())
        with zipfile.ZipFile(destination) as archive:
            root = "VocalGzzio_v4.3.1_Mac_製品版/"
            metadata = json.loads(archive.read(root + "配布情報.json"))
            self.assertIs(metadata["developer_id_signed"], False)
            self.assertIs(metadata["notarized"], False)
            guide = archive.read(root + "説明書/mac-install.html").decode("utf-8")
            self.assertIn("Appleの開発元署名・公証を受けていません", guide)
            self.assertIn("VocalGzzio_v4.3.1_Mac_製品版_未公証.pkg", guide)
            self.assertIn('href="説明書.html"', guide)
            entry = archive.read(root + "ここから始める.html").decode("utf-8")
            self.assertIn('href="説明書/説明書.html"', entry)
            self.assertFalse(any("手動インストール/" in name for name in archive.namelist()))
            self.assertFalse(any(".component/" in name or ".app/" in name for name in archive.namelist()))
            self.assertIn(root + "VocalGzzio_v4.3.1_Mac_製品版_未公証.pkg", archive.namelist())

    def test_notarization_rejection_never_publishes_zip(self):
        self.signed()
        with self.mocked_tools(status="Invalid") as methods:
            with self.assertRaises(ValueError):
                self.package()
        methods["assess_installer"].assert_not_called()
        self.assert_no_release()

    def test_actual_install_failure_never_publishes_zip(self):
        self.args.validate_install = True
        with self.mocked_tools(), mock.patch.object(packaging, "validate_installer",
                side_effect=ValueError("導入またはAU読み込み失敗")) as validate:
            with self.assertRaises(ValueError):
                self.package()
            validate.assert_called_once()
        self.assert_no_release()

    def test_installer_rejection_never_publishes_zip(self):
        self.signed()
        with self.mocked_tools() as methods:
            accepted = methods["notarize"].return_value
            methods["notarize"].side_effect = [accepted, {**accepted, "status": "Invalid"}]
            with self.assertRaises(ValueError):
                self.package()
        self.assert_no_release()

    def test_every_external_verification_failure_blocks_release(self):
        self.signed()
        for name in ("sign_bundle", "verify_bundle_signature", "verify_installer_signature",
                     "staple_and_validate", "assess_installer"):
            with self.subTest(tool=name), self.mocked_tools(fail_at=name):
                with self.assertRaises(ValueError):
                    self.package()
                self.assert_no_release()

    def test_formal_distribution_has_matching_guide_and_only_installer(self):
        self.signed()
        with self.mocked_tools() as methods:
            destination = self.package()
        self.assertEqual(methods["notarize"].call_count, 2)
        self.assertEqual(methods["staple_and_validate"].call_count, 2)
        methods["assess_installer"].assert_called_once()
        with zipfile.ZipFile(destination) as archive:
            root = "VocalGzzio_v4.3.1_Mac_製品版/"
            metadata = json.loads(archive.read(root + "配布情報.json"))
            self.assertIs(metadata["developer_id_signed"], True)
            self.assertIs(metadata["notarized"], True)
            self.assertEqual(metadata["distribution"], "installer_only")
            self.assertFalse(any("手動インストール" in name or "signed-bundles" in name for name in archive.namelist()))
            guide = archive.read(root + "説明書/mac-install.html").decode("utf-8")
            self.assertIn("_公証済み.pkg", guide)
            self.assertNotIn("未公証", guide)
            self.assertIn('href="説明書.html"', guide)
            self.assertIn(root + "VocalGzzio_v4.3.1_Mac_製品版_公証済み.pkg", archive.namelist())
        installed = self.installer_snapshots[0]
        self.assertTrue(any(name.replace("\\", "/").endswith("説明書/mac-install.html") for name in installed))
        # インストール済み文書に最終PKGの未確認の成功情報を先取りしない。
        self.assertFalse(any(name.endswith("配布情報.json") for name in installed))
        self.assertTrue(any(name.endswith("本体検証情報.json") for name in installed))

    def test_existing_zip_or_checksum_blocks_packaging_before_tools(self):
        self.args.out.mkdir()
        for suffix in (".zip", ".zip.sha256"):
            with self.subTest(suffix=suffix):
                target = self.args.out / ("VocalGzzio_v4.3.1_Mac_製品版" + suffix)
                target.write_bytes(b"original")
                with mock.patch.object(packaging, "run") as run:
                    with self.assertRaises(ValueError):
                        self.package()
                    run.assert_not_called()
                self.assertEqual(target.read_bytes(), b"original")
                target.unlink()

    def test_publish_archive_refuses_overwrite_and_preserves_original(self):
        source = self.root / "staged.zip"
        source.write_bytes(b"new")
        destination = self.root / "distribution.zip"
        for target in (destination, destination.with_suffix(".zip.sha256")):
            with self.subTest(target=target.name):
                target.write_bytes(b"original")
                with self.assertRaises(ValueError):
                    packaging.publish_archive(source, destination)
                self.assertEqual(target.read_bytes(), b"original")
                target.unlink()

    def test_publish_archive_cleans_only_its_files_after_copy_failure(self):
        source = self.root / "staged.zip"
        source.write_bytes(b"new")
        destination = self.root / "distribution.zip"
        with mock.patch.object(packaging.shutil, "copyfileobj", side_effect=OSError("容量不足")):
            with self.assertRaises(OSError):
                packaging.publish_archive(source, destination)
        self.assertFalse(destination.exists())
        self.assertFalse(destination.with_suffix(".zip.sha256").exists())
        self.assertEqual(source.read_bytes(), b"new")


if __name__ == "__main__":
    unittest.main()
