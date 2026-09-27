#!/usr/bin/env python3
"""公開ビルド環境から製品版を暗号化して受け取る。秘密鍵はローカルにのみ保管する。"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

MAGIC = b"VGZENC01"
LABEL = b"VocalGzzio release envelope v1"
CHUNK = 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def file_hash(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(CHUNK), b""):
            digest.update(chunk)
    return digest.hexdigest()


def public_id(key):
    der = key.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    return hashlib.sha256(der).hexdigest()


def oaep():
    return padding.OAEP(mgf=padding.MGF1(hashes.SHA256()), algorithm=hashes.SHA256(), label=LABEL)


def generate(private_path, public_path):
    require(not private_path.exists() and not public_path.exists(), "既存の鍵は上書きできません。")
    key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    private_path.parent.mkdir(parents=True, exist_ok=True)
    public_path.parent.mkdir(parents=True, exist_ok=True)
    # 生成直後からUnixでは所有者のみ。Windows側では保存先のアクセス権を引き継ぐ。
    descriptor = os.open(private_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as out:
        out.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                    serialization.NoEncryption()))
    with public_path.open("xb") as out:
        out.write(key.public_key().public_bytes(serialization.Encoding.PEM,
                                               serialization.PublicFormat.SubjectPublicKeyInfo))
    return public_id(key.public_key())


def encrypt(source, destination, public_path):
    require(source.is_file(), "暗号化する配布物がありません。")
    require(not destination.exists(), "出力先はすでに存在します。")
    key = serialization.load_pem_public_key(public_path.read_bytes())
    require(isinstance(key, rsa.RSAPublicKey) and key.key_size >= 3072, "RSA 3072ビット以上の公開鍵が必要です。")
    aes_key, nonce = os.urandom(32), os.urandom(12)
    header = {"version": 1, "algorithm": "AES-256-GCM+RSA-OAEP-SHA256",
              "name": source.name, "size": source.stat().st_size, "sha256": file_hash(source),
              "key_id": public_id(key), "nonce": base64.b64encode(nonce).decode("ascii"),
              "key": base64.b64encode(key.encrypt(aes_key, oaep())).decode("ascii")}
    raw = json.dumps(header, ensure_ascii=False, separators=(",", ":"), sort_keys=True).encode("utf-8")
    prefix = MAGIC + struct.pack(">I", len(raw)) + raw
    encryptor = Cipher(algorithms.AES(aes_key), modes.GCM(nonce)).encryptor()
    encryptor.authenticate_additional_data(prefix)
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        with source.open("rb") as inp, destination.open("xb") as out:
            out.write(prefix)
            for chunk in iter(lambda: inp.read(CHUNK), b""):
                out.write(encryptor.update(chunk))
            out.write(encryptor.finalize())
            out.write(encryptor.tag)
    except Exception:
        destination.unlink(missing_ok=True)
        raise
    return header


def decrypt(source, destination, private_path):
    require(not destination.exists(), "復号先はすでに存在します。")
    private = serialization.load_pem_private_key(private_path.read_bytes(), password=None)
    require(isinstance(private, rsa.RSAPrivateKey) and private.key_size >= 3072, "対応するRSA秘密鍵が必要です。")
    with source.open("rb") as inp:
        prefix = inp.read(12)
        require(len(prefix) == 12 and prefix[:8] == MAGIC, "暗号化ファイルの形式が違います。")
        header_length = struct.unpack(">I", prefix[8:])[0]
        require(0 < header_length <= 65536, "暗号化ファイルの先頭情報が不正です。")
        raw = inp.read(header_length)
        require(len(raw) == header_length, "暗号化ファイルが途中で切れています。")
        header = json.loads(raw)
        require(header.get("version") == 1 and header.get("algorithm") == "AES-256-GCM+RSA-OAEP-SHA256",
                "対応していない暗号化形式です。")
        require(header.get("key_id") == public_id(private.public_key()), "秘密鍵が配布物の宛先と一致しません。")
        size = header.get("size")
        require(type(size) is int and size >= 0, "元ファイルの長さが不正です。")
        require(source.stat().st_size == 12 + header_length + size + 16, "暗号化ファイルの長さが違います。")
        nonce = base64.b64decode(header["nonce"], validate=True)
        require(len(nonce) == 12, "暗号化情報が不正です。")
        aes_key = private.decrypt(base64.b64decode(header["key"], validate=True), oaep())
        require(len(aes_key) == 32, "暗号化情報が不正です。")
        inp.seek(-16, 2)
        tag = inp.read(16)
        inp.seek(12 + header_length)
        decryptor = Cipher(algorithms.AES(aes_key), modes.GCM(nonce, tag)).decryptor()
        decryptor.authenticate_additional_data(prefix + raw)
        destination.parent.mkdir(parents=True, exist_ok=True)
        # 認証前の平文は一時ファイルに隔離し、認証とハッシュ照合後だけ確定する。
        fd, temporary = tempfile.mkstemp(prefix=".vocalgzzio-", suffix=".part", dir=destination.parent)
        temporary = Path(temporary)
        try:
            digest = hashlib.sha256()
            with os.fdopen(fd, "wb") as out:
                remaining = size
                while remaining:
                    chunk = inp.read(min(CHUNK, remaining))
                    require(bool(chunk), "暗号化ファイルが途中で切れています。")
                    remaining -= len(chunk)
                    plain = decryptor.update(chunk)
                    digest.update(plain)
                    out.write(plain)
                tail = decryptor.finalize()
                digest.update(tail)
                out.write(tail)
            require(digest.hexdigest() == header.get("sha256"), "復号した配布物の照合に失敗しました。")
            # ハードリンクの作成は既存ファイルを置換しない。
            os.link(temporary, destination)
        finally:
            temporary.unlink(missing_ok=True)
    return header


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="operation", required=True)
    keygen = sub.add_parser("keygen")
    keygen.add_argument("--private", type=Path, required=True)
    keygen.add_argument("--public", type=Path, required=True)
    for operation, key in (("encrypt", "public"), ("decrypt", "private")):
        command = sub.add_parser(operation)
        command.add_argument("--input", type=Path, required=True)
        command.add_argument("--output", type=Path, required=True)
        command.add_argument("--" + key, type=Path, required=True)
    args = parser.parse_args()
    if args.operation == "keygen":
        identifier = generate(args.private, args.public)
        print(f"鍵を生成しました。公開鍵の識別値: {identifier}")
    else:
        operation = encrypt if args.operation == "encrypt" else decrypt
        key = args.public if args.operation == "encrypt" else args.private
        result = operation(args.input, args.output, key)
        print(f"完了: {args.output}\n元ファイルのSHA-256: {result['sha256']}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, InvalidTag) as error:
        print("中止: " + (str(error) or "暗号化ファイルの認証に失敗しました。"), file=sys.stderr)
        sys.exit(1)
