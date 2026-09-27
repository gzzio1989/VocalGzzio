"""復号一致・改ざん拒否・既存ファイル保護を確認する。"""
import os
from pathlib import Path
import tempfile
import unittest
from cryptography.exceptions import InvalidTag
from release_crypto import generate, encrypt, decrypt


class EnvelopeTest(unittest.TestCase):
    def test_roundtrip_tamper_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            private, public = root / 'key.private.pem', root / 'key.public.pem'
            generate(private, public)
            source, sealed, result = root / 'source.zip', root / 'sealed.vgzenc', root / 'result.zip'
            source.write_bytes(os.urandom(2 * 1024 * 1024 + 137))
            encrypt(source, sealed, public)
            decrypt(sealed, result, private)
            self.assertEqual(source.read_bytes(), result.read_bytes())
            with self.assertRaises(ValueError):
                decrypt(sealed, result, private)
            damaged = bytearray(sealed.read_bytes())
            damaged[-30] ^= 1
            corrupt = root / 'corrupt.vgzenc'
            corrupt.write_bytes(damaged)
            refused = root / 'refused.zip'
            with self.assertRaises(InvalidTag):
                decrypt(corrupt, refused, private)
            self.assertFalse(refused.exists())
            self.assertEqual([], list(root.glob('*.part')))


if __name__ == '__main__':
    unittest.main()
