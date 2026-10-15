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
from test_macos_bundle_icon import icon_bytes


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
                     None, subprocess.CompletedProcess([], 0,
                         stdout="aumf Vcgj Gzzo - Gzzio: VocalGzzio\n", stderr=""),
                     subprocess.CalledProcessError(1, "auval")]) as run, \
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
                                if suffix == "app":
                                    (contents / "Resources").mkdir()
                                    (contents / "Resources/Icon.icns").write_bytes(icon_bytes())
                                    info["CFBundleIconFile"] = "Icon.icns"
                                if failure == "version":
                                    info["CFBundleShortVersionString"] = "4.2.1"
                                (contents / "Info.plist").write_bytes(plistlib.dumps(info))
                        elif command == ["/usr/bin/auval", "-a"]:
                            subtype = "Vcgt" if trial else "Vcgj"
                            return subprocess.CompletedProcess(command, 0,
                                stdout=f"aumf {subtype} Gzzo - Gzzio: {stem}\n", stderr="")
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
                            self.assertEqual(run.call_count, 3)
                            run.reset_mock()
                            with self.assertRaisesRegex(ValueError, "上書き"):
                                validation.validate_installer(Path("example.pkg"), edition, "4.3.1")
                            run.assert_not_called()


class AudioUnitRegistrationTest(unittest.TestCase):
    identity = ("aumf", "Vcgj", "Gzzo")

    def listing(self, output, error=""):
        return subprocess.CompletedProcess(["/usr/bin/auval", "-a"], 0,
                                           stdout=output, stderr=error)

    def test_delayed_registration_then_real_validation_runs_once(self):
        now = [0.0]
        def sleep(seconds):
            now[0] += seconds
        with patch.object(validation, "require_ci_mac"), \
             patch.object(validation.time, "monotonic", side_effect=lambda: now[0]), \
             patch.object(validation.time, "sleep", side_effect=sleep) as pause, \
             patch.object(validation.subprocess, "run", side_effect=[
                 self.listing("aufx appl test - Apple\n"),
                 self.listing("  aumf Vcgj Gzzo - Gzzio: VocalGzzio\n"),
                 subprocess.CompletedProcess([], 0)]) as run:
            validation.validate_registered_au(self.identity)
            self.assertEqual(now[0], 1.0)
            pause.assert_called_once_with(1.0)
            self.assertEqual([call.args[0] for call in run.call_args_list], [
                ["/usr/bin/auval", "-a"], ["/usr/bin/auval", "-a"],
                ["/usr/bin/auval", "-v", *self.identity]])
            self.assertTrue(all(call.kwargs["check"] for call in run.call_args_list))

    def test_timeout_rejects_other_edition_type_case_and_codes_only_in_name(self):
        output = ("aumf Vcgt Gzzo - Gzzio: VocalGzzio Trial\n"
                  "aufx Vcgj Gzzo - Gzzio: VocalGzzio\n"
                  "aumf vcgj Gzzo - Gzzio: VocalGzzio\n"
                  "aumf Test Gzzo - Gzzio: aumf Vcgj Gzzo\n")
        now = [0.0]
        def enumerate_au(command, **kwargs):
            self.assertEqual(command, ["/usr/bin/auval", "-a"])
            self.assertLessEqual(kwargs["timeout"], 45.0 - now[0])
            now[0] += 0.25  # 一覧を取得している時間も上限に含める。
            return self.listing(output)
        def sleep(seconds):
            now[0] += seconds
        with patch.object(validation, "require_ci_mac"), \
             patch.object(validation.time, "monotonic", side_effect=lambda: now[0]), \
             patch.object(validation.time, "sleep", side_effect=sleep), \
             patch.object(validation.subprocess, "run", side_effect=enumerate_au) as run:
            with self.assertRaisesRegex(ValueError, "45秒") as caught:
                validation.validate_registered_au(self.identity)
            self.assertEqual(now[0], 45.0)
            self.assertIn(output, str(caught.exception))
            self.assertGreater(run.call_count, 1)

    def test_failed_enumeration_cannot_pass_even_if_output_contains_expected_codes(self):
        output = "aumf Vcgj Gzzo - Gzzio: VocalGzzio"
        for failure in (subprocess.CalledProcessError(2, "auval", output=output,
                                                    stderr="一覧失敗の詳細"),
                        subprocess.TimeoutExpired("auval", 15, output=output,
                                                  stderr="一覧失敗の詳細")):
            with self.subTest(failure=type(failure).__name__), \
                 patch.object(validation, "require_ci_mac"), \
                 patch.object(validation.subprocess, "run", side_effect=failure) as run, \
                 patch.object(validation.time, "sleep") as pause:
                with self.assertRaises(ValueError) as caught:
                    validation.validate_registered_au(self.identity)
                self.assertIn(output, str(caught.exception))
                self.assertIn("一覧失敗の詳細", str(caught.exception))
                run.assert_called_once()
                pause.assert_not_called()

    def test_validation_failure_after_registration_is_not_retried(self):
        with patch.object(validation, "require_ci_mac"), \
             patch.object(validation.subprocess, "run", side_effect=[
                 self.listing("aumf Vcgj Gzzo - Gzzio: VocalGzzio\n"),
                 subprocess.CalledProcessError(1, "auval")]) as run, \
             patch.object(validation.time, "sleep") as pause:
            with self.assertRaises(subprocess.CalledProcessError):
                validation.validate_registered_au(self.identity)
            self.assertEqual(run.call_count, 2)
            pause.assert_not_called()

    def test_registration_wait_is_for_ci_mac_only(self):
        for platform, github_actions in (("win32", "true"), ("darwin", "false")):
            with self.subTest(platform=platform, github_actions=github_actions), \
                 patch.dict(os.environ, {"GITHUB_ACTIONS": github_actions}), \
                 patch.object(validation.sys, "platform", platform), \
                 patch.object(validation.subprocess, "run") as run:
                with self.assertRaises(ValueError):
                    validation.validate_registered_au(self.identity)
                run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
