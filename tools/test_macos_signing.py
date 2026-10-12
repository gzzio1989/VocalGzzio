"""配布を許可する条件の検査。Macの実署名・実公証の代わりではない。"""
import hashlib
import json
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import macos_signing as signing


TEAM = "AB12CD34EF"
OTHER_TEAM = "ZZ12CD34EF"
SUBMISSION = "12345678-abcd-4321-9876-aabbccddeeff"


def completed(stdout="", stderr="", returncode=0):
    return subprocess.CompletedProcess([], returncode, stdout, stderr)


def signature(team=TEAM):
    return (f"CodeDirectory v=20500 size=123 flags=0x10000(runtime) hashes=3+7\n"
            f"Authority=Developer ID Application: Example ({team})\n"
            "Authority=Developer ID Certification Authority\n"
            f"TeamIdentifier={team}\nTimestamp=11 Oct 2026 at 12:00:00\n")


class SigningTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def bundle(self, suffix=".app"):
        bundle = self.root / ("VocalGzzio" + suffix)
        binary = bundle / "Contents" / "MacOS" / "VocalGzzio"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(bytes.fromhex("cafebabe") + b"test-only")
        info = {"CFBundleExecutable": "VocalGzzio", "NSMicrophoneUsageDescription": "録音に使用します。"}
        (bundle / "Contents" / "Info.plist").write_bytes(plistlib.dumps(info))
        return bundle

    def signature_runner(self, *, display=None, entitlements=None, architectures="arm64 x86_64"):
        def run(*args, **kwargs):
            if args[0] == "lipo":
                return completed(architectures)
            if "--entitlements" in args:
                rights = {signing.AUDIO_INPUT: True} if entitlements is None else entitlements
                return completed(plistlib.dumps(rights).decode())
            if "--display" in args:
                return completed(stderr=signature() if display is None else display)
            return completed()
        return run

    def test_identity_requires_correct_certificate_kind_and_team(self):
        signing.validate_identity(f"Developer ID Application: Example ({TEAM})", TEAM)
        signing.validate_identity("a" * 40, TEAM)
        for identity, team, kind in [("-", TEAM, "Application"),
                (f"Developer ID Application: Example ({OTHER_TEAM})", TEAM, "Application"),
                (f"Developer ID Installer: Example ({TEAM})", TEAM, "Application"),
                ("a" * 40, "short", "Application"), ("a" * 40, TEAM, "Unknown")]:
            with self.subTest(identity=identity, team=team, kind=kind):
                with self.assertRaises(signing.SigningError):
                    signing.validate_identity(identity, team, kind)

    def test_universal_application_signature_and_minimal_permission(self):
        with patch.object(signing, "_run", side_effect=self.signature_runner()) as run:
            result = signing.verify_bundle_signature(self.bundle(), TEAM)
        self.assertTrue(result["developer_id_signed"])
        self.assertEqual(set(result["architectures"]), {"arm64", "x86_64"})
        self.assertTrue(any("--all-architectures" in call.args for call in run.call_args_list))

    def test_plugin_has_no_application_entitlement(self):
        bundle = self.bundle(".component")
        with patch.object(signing, "_run", side_effect=self.signature_runner(entitlements={})):
            self.assertTrue(signing.verify_bundle_signature(bundle, TEAM)["developer_id_signed"])
        with patch.object(signing, "_run", side_effect=self.signature_runner()):
            with self.assertRaises(signing.SigningError):
                signing.verify_bundle_signature(bundle, TEAM)

    def test_wrong_team_adhoc_missing_runtime_and_timestamp_are_rejected(self):
        bundle = self.bundle()
        invalid = [signature(OTHER_TEAM), "Signature=adhoc\nTeamIdentifier=not set\n",
                   signature().replace("0x10000", "0x0"),
                   signature().replace("Timestamp=11 Oct 2026 at 12:00:00", "Timestamp=none"),
                   signature() + f"TeamIdentifier={TEAM}\n"]
        for display in invalid:
            with self.subTest(display=display):
                with patch.object(signing, "_run", side_effect=self.signature_runner(display=display)):
                    with self.assertRaises(signing.SigningError):
                        signing.verify_bundle_signature(bundle, TEAM)

    def test_extra_missing_and_non_boolean_entitlements_are_rejected(self):
        bundle = self.bundle()
        for rights in [{}, {signing.AUDIO_INPUT: 1},
                       {signing.AUDIO_INPUT: True, "com.apple.security.cs.disable-library-validation": True}]:
            with self.subTest(rights=rights):
                with patch.object(signing, "_run", side_effect=self.signature_runner(entitlements=rights)):
                    with self.assertRaises(signing.SigningError):
                        signing.verify_bundle_signature(bundle, TEAM)

    def test_missing_architecture_is_rejected(self):
        with patch.object(signing, "_run", side_effect=self.signature_runner(architectures="arm64")):
            with self.assertRaises(signing.SigningError):
                signing.verify_bundle_signature(self.bundle(), TEAM)

    def test_nested_code_is_rejected_before_signing(self):
        bundle = self.bundle()
        (bundle / "Contents" / "helper").write_bytes(b"#!/bin/sh\n")
        with patch.object(signing, "_run") as run:
            with self.assertRaises(signing.SigningError):
                signing.sign_bundle(bundle, f"Developer ID Application: Example ({TEAM})", TEAM)
        run.assert_not_called()

    def test_command_timeout_never_reports_success(self):
        with patch.object(signing.sys, "platform", "darwin"), patch.object(signing.subprocess, "run",
                side_effect=subprocess.TimeoutExpired("codesign", 1, output=b"partial")):
            with self.assertRaises(signing.CommandError) as context:
                signing._run("codesign", timeout=1)
        self.assertEqual(context.exception.stdout, "partial")

    def test_installer_signature_requires_expected_team_and_timestamp(self):
        installer = self.root / "VocalGzzio.pkg"
        installer.write_bytes(b"test")
        output = ("Status: signed by a developer certificate issued by Apple for distribution\n"
                  "Signed with a trusted timestamp\n"
                  f"    1. Developer ID Installer: Example ({TEAM})\n")
        with patch.object(signing, "_run", return_value=completed(output)):
            self.assertTrue(signing.verify_installer_signature(installer, TEAM)["developer_id_signed"])
        for invalid in [output.replace(TEAM, OTHER_TEAM), output.replace("trusted timestamp", "no timestamp")]:
            with patch.object(signing, "_run", return_value=completed(invalid)):
                with self.assertRaises(signing.SigningError):
                    signing.verify_installer_signature(installer, TEAM)

    def test_gatekeeper_assessment_requires_notarized_source(self):
        installer = self.root / "VocalGzzio.pkg"
        installer.write_bytes(b"test")
        with patch.object(signing, "_run", return_value=completed(stderr="source=Notarized Developer ID\n")):
            signing.assess_installer(installer)
        with patch.object(signing, "_run", return_value=completed(stderr="source=Developer ID\n")):
            with self.assertRaises(signing.SigningError):
                signing.assess_installer(installer)


class NotarizationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.archive = self.root / "VocalGzzio.zip"
        self.archive.write_bytes(b"test archive")
        self.checksum = hashlib.sha256(self.archive.read_bytes()).hexdigest()

    def runner(self, *, submission_status="Accepted", report_status="Accepted", report_id=SUBMISSION.upper(),
               report_checksum=None, returncode=0, change_archive=False, write_log=True):
        def run(*args, **kwargs):
            if args[2] == "submit":
                return completed(json.dumps({"id": SUBMISSION, "status": submission_status}), returncode=returncode)
            if args[2] == "log":
                if write_log:
                    Path(args[-1]).write_text(json.dumps({"jobId": report_id, "status": report_status,
                        "sha256": self.checksum if report_checksum is None else report_checksum}), encoding="utf-8")
                if change_archive:
                    self.archive.write_bytes(b"changed")
                return completed()
            raise AssertionError(args)
        return run

    def notarize(self):
        return signing.notarize(self.archive, "existing-profile", self.root / "logs", timeout_seconds=1)

    def test_accepted_log_matches_case_insensitive_uuid_and_checksum(self):
        with patch.object(signing, "_run", side_effect=self.runner(report_checksum=self.checksum.upper())):
            result = self.notarize()
        self.assertEqual(result["status"], "Accepted")
        self.assertEqual(result["sha256"], self.checksum)
        self.assertTrue(Path(result["log"]).is_file())

    def test_unaccepted_submission_report_or_command_failure_is_rejected(self):
        for options in [{"submission_status": "In Progress"}, {"submission_status": "Invalid"},
                        {"report_status": "Invalid"}, {"returncode": 1}]:
            with self.subTest(options=options):
                with patch.object(signing, "_run", side_effect=self.runner(**options)):
                    with self.assertRaises(signing.SigningError):
                        self.notarize()

    def test_other_request_invalid_uuid_or_checksum_is_rejected(self):
        for options in [{"report_id": "12345678-abcd-4321-9876-aabbccddeefa"},
                        {"report_id": "invalid"}, {"report_id": None},
                        {"report_checksum": "0" * 64}, {"write_log": False}]:
            with self.subTest(options=options):
                with patch.object(signing, "_run", side_effect=self.runner(**options)):
                    with self.assertRaises(signing.SigningError):
                        self.notarize()

    def test_archive_changed_during_submission_is_rejected(self):
        with patch.object(signing, "_run", side_effect=self.runner(change_archive=True)):
            with self.assertRaises(signing.SigningError):
                self.notarize()

    def test_timeout_is_failure_and_partial_output_is_preserved(self):
        with patch.object(signing, "_run", side_effect=signing.CommandError("timeout", "partial", "waiting")):
            with self.assertRaises(signing.SigningError):
                self.notarize()
        logs = list((self.root / "logs").glob("notary-*/submission.stdout.txt"))
        self.assertEqual(len(logs), 1)
        self.assertEqual(logs[0].read_text(encoding="utf-8"), "partial")


if __name__ == "__main__":
    unittest.main()
