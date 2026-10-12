"""製品と体験版、AUの別形式を取り違えた検査を成功扱いしない。"""
import unittest
import os
from pathlib import Path
import plistlib
import subprocess
import tempfile
from unittest.mock import patch
import validate_macos_au as validation
from validate_macos_au import validate_identity


class AudioUnitIdentityTest(unittest.TestCase):
    def info(self, trial=False):
        return {"CFBundleName": "VocalGzzio Trial" if trial else "VocalGzzio",
                "CFBundleIdentifier": "com.gzzio.vocalgzzio" + (".trial" if trial else ""),
                "CFBundleShortVersionString": "4.3.1",
                "AudioComponents": [{"type": "aumf", "subtype": "Vcgt" if trial else "Vcgj",
                                     "manufacturer": "Gzzo"}]}

    def test_product_and_trial_use_distinct_au_codes(self):
        for trial in (False, True):
            result = validate_identity(self.info(trial), "trial" if trial else "product", "4.3.1")
            self.assertEqual(result, ("aumf", "Vcgt" if trial else "Vcgj", "Gzzo"))

    def test_wrong_edition_or_version_is_rejected(self):
        for edition, version in (("trial", "4.3.1"), ("product", "4.2.1")):
            with self.assertRaises(ValueError):
                validate_identity(self.info(), edition, version)

    def test_wrong_type_or_manufacturer_is_rejected(self):
        for key, value in (("type", "aufx"), ("subtype", "Vcgt"), ("manufacturer", "TEST")):
            candidate = self.info()
            candidate["AudioComponents"][0][key] = value
            with self.assertRaises(ValueError):
                validate_identity(candidate, "product", "4.3.1")

    def test_missing_or_multiple_components_are_rejected(self):
        for components in ([], [{}, {}], None, {}, [None]):
            candidate = self.info()
            candidate["AudioComponents"] = components
            with self.assertRaises(ValueError):
                validate_identity(candidate, "product", "4.3.1")

    def test_local_machine_is_not_modified(self):
        with patch.dict(os.environ, {"GITHUB_ACTIONS": "false"}), \
             patch.object(validation.sys, "platform", "darwin"), \
             patch.object(validation.sys, "argv", ["validate", "--bundle", "VocalGzzio.component",
                 "--edition", "product", "--version", "4.3.1"]), \
             patch.object(validation.subprocess, "run") as run:
            with self.assertRaises(ValueError):
                validation.main()
            run.assert_not_called()

    def test_host_failure_is_not_reported_as_success_and_existing_au_is_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bundle = root / "build" / "VocalGzzio.component"
            (bundle / "Contents").mkdir(parents=True)
            (bundle / "Contents" / "Info.plist").write_bytes(plistlib.dumps(self.info()))
            target = root / "Library" / "Audio" / "Plug-Ins" / "Components" / bundle.name
            with patch.dict(os.environ, {"GITHUB_ACTIONS": "true"}), \
                 patch.object(validation.sys, "platform", "darwin"), \
                 patch.object(validation.sys, "argv", ["validate", "--bundle", str(bundle),
                     "--edition", "product", "--version", "4.3.1"]), \
                 patch.object(validation.Path, "home", return_value=root), \
                 patch.object(validation.subprocess, "run", side_effect=[
                     None, subprocess.CalledProcessError(1, "auval")]) as run, \
                 patch("builtins.print") as report:
                with self.assertRaises(subprocess.CalledProcessError):
                    validation.main()
                self.assertEqual(run.call_args_list[-1].args[0],
                                 ["/usr/bin/auval", "-v", "aumf", "Vcgj", "Gzzo"])
                self.assertTrue(run.call_args.kwargs["check"])
                report.assert_not_called()
                target.mkdir()
                run.reset_mock()
                with self.assertRaises(ValueError):
                    validation.main()
                run.assert_not_called()

    def test_installer_checks_actual_destinations_and_fails_on_bad_payload_or_au(self):
        for trial in (False, True):
            for failure in (None, "installer", "version", "auval"):
                with self.subTest(trial=trial, failure=failure), tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    stem = "VocalGzzio Trial" if trial else "VocalGzzio"
                    edition = "trial" if trial else "product"
                    def execute(command, **kwargs):
                        self.assertTrue(kwargs["check"])
                        if command[0] == "sudo":
                            if failure == "installer":
                                raise subprocess.CalledProcessError(1, command)
                            self.assertEqual(command[1:4], ["-n", "/usr/sbin/installer", "-pkg"])
                            for folder, suffix in (("Applications", "app"),
                                    ("Library/Audio/Plug-Ins/Components", "component"),
                                    ("Library/Audio/Plug-Ins/VST3", "vst3")):
                                contents = root / folder / f"{stem}.{suffix}" / "Contents"
                                (contents / "MacOS").mkdir(parents=True)
                                binary = contents / "MacOS" / stem
                                binary.write_bytes(b"test-binary")
                                binary.chmod(0o755)
                                info = {**self.info(trial), "CFBundleExecutable": stem}
                                if failure == "version":
                                    info["CFBundleShortVersionString"] = "4.2.1"
                                (contents / "Info.plist").write_bytes(plistlib.dumps(info))
                        else:
                            self.assertEqual(command, ["/usr/bin/auval", "-v", "aumf",
                                                       "Vcgt" if trial else "Vcgj", "Gzzo"])
                            if failure == "auval":
                                raise subprocess.CalledProcessError(1, command)
                    with patch.object(validation, "require_ci_mac"), \
                         patch.object(validation, "INSTALL_ROOT", root), \
                         patch.object(validation.Path, "home", return_value=root / "user"), \
                         patch.object(validation.subprocess, "run", side_effect=execute) as run, \
                         patch("builtins.print") as report:
                        if failure:
                            with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                                validation.validate_installer(Path("example.pkg"), edition, "4.3.1")
                            report.assert_not_called()
                        else:
                            proof = validation.validate_installer(Path("example.pkg"), edition, "4.3.1")
                            self.assertTrue(proof["installer_succeeded"])
                            self.assertFalse(proof["gatekeeper_download_tested"])
                            self.assertEqual(run.call_count, 2)
                            run.reset_mock()
                            with self.assertRaisesRegex(ValueError, "上書き"):
                                validation.validate_installer(Path("example.pkg"), edition, "4.3.1")
                            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
