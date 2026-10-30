# -*- coding: utf-8 -*-
"""
独自 Windows 仮想オーディオドライバ (VocalGzzioAudio) の INF 構成・同梱解決ロジック検査
"""
import importlib.util
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent


def _load_pack_booth():
    spec = importlib.util.spec_from_file_location("pack_booth", ROOT / "tools" / "pack_booth.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class WindowsDriverPackagingTests(unittest.TestCase):
    def test_universal_inf_meets_microsoft_attestation_rules(self):
        inf_path = ROOT / "driver_src" / "inf" / "VocalGzzioAudio.inf"
        self.assertTrue(inf_path.is_file(), "driver_src/inf/VocalGzzioAudio.inf が存在すること")
        text = inf_path.read_text(encoding="utf-8")

        # Universal INF の必須要件チェック (HKLM 直書き禁止・Dirid 13・PnpLockdown=1・x64/ARM64両対応)
        self.assertIn('Signature   = "$WINDOWS NT$"', text)
        self.assertIn("PnpLockdown = 1", text)
        self.assertIn("CatalogFile = VocalGzzioAudio.cat", text)
        self.assertIn("ROOT\\VocalGzzioAudio", text)
        self.assertIn("NTamd64.10.0...17763", text)
        self.assertIn("NTarm64.10.0...17763", text)
        self.assertIn("ServiceBinary = %13%\\VocalGzzioAudio.sys", text)
        self.assertIn('VocalGzzioAudio.DeviceDesc     = "VocalGzzio Driver (WDM)"', text)
        self.assertIn("PKEY_AudioDevice_NeverSetAsDefaultEndpoint", text)
        self.assertNotIn("HKLM,", text, "Universal INF では HKLM への直接書き込みは禁止 (HKR のみ)")
        self.assertNotIn("Yamaha", text, "自社製ドライバ INF に他社名を含めないこと")

    def test_resolve_bundled_driver_files_modes(self):
        pack_booth = _load_pack_booth()

        # 1. 現行フォールバックのみ存在する場合
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            for f in ("syncroom.inf", "syncroom.cat", "syncroom.sys"):
                (d / f).write_bytes(b"ok")
            self.assertEqual(
                pack_booth.resolve_bundled_driver_files(d),
                ["syncroom.inf", "syncroom.cat", "syncroom.sys"],
            )

        # 2. 自社製署名済みドライバのみに切り替えた場合 (フォールバック削除後)
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            for f in ("VocalGzzioAudio.inf", "VocalGzzioAudio.cat", "VocalGzzioAudio.sys"):
                (d / f).write_bytes(b"ok")
            self.assertEqual(
                pack_booth.resolve_bundled_driver_files(d),
                ["VocalGzzioAudio.inf", "VocalGzzioAudio.cat", "VocalGzzioAudio.sys"],
            )

        # 3. 移行期間中で両方存在する場合 (自社製が先頭で両方同梱)
        with tempfile.TemporaryDirectory() as tmp:
            d = pathlib.Path(tmp)
            for f in (
                "VocalGzzioAudio.inf",
                "VocalGzzioAudio.cat",
                "VocalGzzioAudio.sys",
                "syncroom.inf",
                "syncroom.cat",
                "syncroom.sys",
            ):
                (d / f).write_bytes(b"ok")
            self.assertEqual(
                pack_booth.resolve_bundled_driver_files(d),
                [
                    "VocalGzzioAudio.inf",
                    "VocalGzzioAudio.cat",
                    "VocalGzzioAudio.sys",
                    "syncroom.inf",
                    "syncroom.cat",
                    "syncroom.sys",
                ],
            )


if __name__ == "__main__":
    unittest.main()
