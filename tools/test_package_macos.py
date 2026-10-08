"""両CPU・版の取り違え・検査用ビルドを梱包時に拒否することを確認する。"""
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch

import package_macos as package


class MacPackageTest(unittest.TestCase):
    def test_matching_product_and_trial_markers(self):
        for edition in ("product", "trial"):
            package.verify_marker(b"\0VOCALGZZIO-EDITION:" + edition.upper().encode() + b"\0", edition)

    def test_missing_wrong_and_mixed_markers_are_refused(self):
        for data in (b"", b"VOCALGZZIO-EDITION:TRIAL\0", b"VOCALGZZIO-EDITION:LITE\0",
                     b"VOCALGZZIO-EDITION:PRODUCT\0VOCALGZZIO-EDITION:TRIAL\0"):
            with self.subTest(data=data), self.assertRaises(ValueError):
                package.verify_marker(data, "product")

    def make_bundle(self, root):
        bundle = root / "VocalGzzio.vst3"
        contents = bundle / "Contents"
        (contents / "MacOS").mkdir(parents=True)
        (contents / "MacOS" / "VocalGzzio").write_bytes(b"universal-binary-placeholder")
        (contents / "Info.plist").write_bytes(plistlib.dumps({
            "CFBundleShortVersionString": "4.2.0", "CFBundleExecutable": "VocalGzzio"}))
        return bundle

    def fake_lipo(self, slices, visited):
        def run(*args):
            self.assertEqual(args[0], "lipo")
            if args[1] == "-archs":
                return " ".join(slices)
            self.assertEqual(args[2], "-thin")
            architecture = args[3]
            visited.append(architecture)
            Path(args[5]).write_bytes(slices[architecture])
            return ""
        return run

    def test_both_architectures_are_individually_verified(self):
        for edition in ("product", "trial"):
            with self.subTest(edition=edition), tempfile.TemporaryDirectory() as temporary:
                bundle = self.make_bundle(Path(temporary))
                marker = b"VOCALGZZIO-EDITION:" + edition.upper().encode() + b"\0"
                visited = []
                with patch.object(package, "run", self.fake_lipo({
                        "arm64": marker, "x86_64": marker}, visited)):
                    package.verify_bundle(bundle, edition, "4.2.0")
                self.assertEqual(visited, ["arm64", "x86_64"])

    def test_wrong_or_missing_marker_in_either_slice_is_refused(self):
        for architecture in ("arm64", "x86_64"):
            for bad_marker in (b"", b"VOCALGZZIO-EDITION:TRIAL\0"):
                with self.subTest(architecture=architecture, bad_marker=bad_marker):
                    with tempfile.TemporaryDirectory() as temporary:
                        bundle = self.make_bundle(Path(temporary))
                        slices = {"arm64": b"VOCALGZZIO-EDITION:PRODUCT\0",
                                  "x86_64": b"VOCALGZZIO-EDITION:PRODUCT\0"}
                        slices[architecture] = bad_marker
                        with patch.object(package, "run", self.fake_lipo(slices, [])):
                            with self.assertRaisesRegex(ValueError, architecture):
                                package.verify_bundle(bundle, "product", "4.2.0")

    def test_single_architecture_is_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = self.make_bundle(Path(temporary))
            with patch.object(package, "run", return_value="arm64"):
                with self.assertRaises(ValueError):
                    package.verify_bundle(bundle, "product", "4.2.0")

    def test_old_version_is_refused_before_lipo(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = self.make_bundle(Path(temporary))
            with patch.object(package, "run") as run:
                with self.assertRaises(ValueError):
                    package.verify_bundle(bundle, "product", "4.1.0")
                run.assert_not_called()

    def test_unsafe_build_settings_are_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            flags = {"VOCALGZZIO_BUILD_TESTS": "OFF", "VOCALGZZIO_LITE": "OFF",
                     "VOCALGZZIO_TRIAL": "OFF"}
            for changed in (*flags, None):
                candidate = dict(flags)
                if changed:
                    candidate[changed] = "ON"
                (build / "CMakeCache.txt").write_text("\n".join(
                    f"{key}:BOOL={value}" for key, value in candidate.items()), encoding="utf-8")
                with self.subTest(changed=changed):
                    if changed:
                        with self.assertRaises(ValueError):
                            package.verify_cache(build, "product")
                    else:
                        package.verify_cache(build, "product")


if __name__ == "__main__":
    unittest.main()
