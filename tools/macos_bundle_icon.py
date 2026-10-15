"""Macアプリのアイコン参照と実データを配布前・導入後に確認する。"""
from pathlib import Path
import struct


def verify_application_icon(bundle, info):
    if bundle.suffix != ".app":
        return
    name = info.get("CFBundleIconFile")
    if (not isinstance(name, str) or not name.strip()
            or name in (".", "..") or "/" in name or "\\" in name):
        raise ValueError("単体アプリのアイコン参照がありません、または不正です。")
    if not name.endswith(".icns"):
        name += ".icns"
    path = bundle / "Contents" / "Resources" / name
    if not path.is_file():
        raise ValueError("単体アプリに参照先のアイコンが同梱されていません。")
    data = path.read_bytes()
    if len(data) < 8 or data[:4] != b"icns" or struct.unpack(">I", data[4:8])[0] != len(data):
        raise ValueError("単体アプリのアイコンデータが破損しています。")
    # ICNSの外枠だけでは成功扱いしない。全エントリーがファイル内に収まることを確認する。
    offset = 8
    images = set()
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError("単体アプリのアイコン項目が途中で切れています。")
        kind = data[offset:offset + 4]
        size = struct.unpack(">I", data[offset + 4:offset + 8])[0]
        if size <= 8 or offset + size > len(data):
            raise ValueError("単体アプリのアイコン項目の長さが不正です。")
        if kind in (b"ic07", b"ic08", b"ic09", b"ic10", b"ic11", b"ic12", b"ic13", b"ic14"):
            images.add(kind)
        offset += size
    if not images:
        raise ValueError("単体アプリに高解像度のアイコン画像がありません。")
    return path
