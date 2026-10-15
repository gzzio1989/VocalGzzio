"""実際に起きた『汎用アイコンになる配布』を検査で拒否する。"""
from pathlib import Path
import struct
import tempfile
import unittest

from macos_bundle_icon import verify_application_icon


def icon_bytes():
    # ICNS構造の検査用。画像の描画確認はMacの本物の生成物で別途行う。
    entry = b"ic09" + struct.pack(">I", 12) + b"test"
    return b"icns" + struct.pack(">I", 8 + len(entry)) + entry


class MacApplicationIconTests(unittest.TestCase):
    def test_missing_empty_and_unsafe_reference_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = Path(temporary) / "VocalGzzio.app"
            for name in (None, "", " ", "../Icon", "/tmp/Icon", "..", "dir\\Icon"):
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, "参照"):
                    verify_application_icon(bundle, {"CFBundleIconFile": name})

    def test_reference_must_have_a_real_valid_icon_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = Path(temporary) / "VocalGzzio.app"
            resources = bundle / "Contents/Resources"
            resources.mkdir(parents=True)
            with self.assertRaisesRegex(ValueError, "同梱"):
                verify_application_icon(bundle, {"CFBundleIconFile": "Icon.icns"})
            for data in (b"", b"not-an-icon", b"icns\0\0\0\x08", b"icns\0\0\0\x15" + b"ic09\0\0\0\x0ctest"):
                (resources / "Icon.icns").write_bytes(data)
                with self.subTest(data=data), self.assertRaises(ValueError):
                    verify_application_icon(bundle, {"CFBundleIconFile": "Icon.icns"})
            (resources / "Icon.icns").write_bytes(icon_bytes())
            for name in ("Icon", "Icon.icns"):
                verify_application_icon(bundle, {"CFBundleIconFile": name})

    def test_broken_inner_entry_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            bundle = Path(temporary) / "VocalGzzio.app"
            icon = bundle / "Contents/Resources/Icon.icns"
            icon.parent.mkdir(parents=True)
            for entry in (b"ic09", b"ic09\0\0\0\0", b"ic09\0\0\0\x20test"):
                icon.write_bytes(b"icns" + struct.pack(">I", 8 + len(entry)) + entry)
                with self.subTest(entry=entry), self.assertRaises(ValueError):
                    verify_application_icon(bundle, {"CFBundleIconFile": "Icon"})

    def test_plugin_bundles_do_not_require_an_application_icon(self):
        verify_application_icon(Path("VocalGzzio.component"), {})
        verify_application_icon(Path("VocalGzzio.vst3"), {})


if __name__ == "__main__":
    unittest.main()
