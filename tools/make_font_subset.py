#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
make_font_subset.py — kawaii_font.ttf (Mochiy Pop One サブセット) を作り直す。

UIの文字列(ソース内の \xNN エスケープ含む)を全部スキャンして、使われている
文字 + かな全部 + 全角/半角カナ + よく使う記号 をサブセット化する。

▼ いつ実行する?
  Tooltips.h / PresetDefs.h / PluginEditor.cpp などの「画面に出る文字列」に
  新しい漢字を足したとき。実行しないと、ゆるかわ/自然モードでその漢字だけ
  別のフォントで表示される(v1.9.9までのプルダウン漢字混在バグの原因)。

▼ 使い方
  pip install fonttools
  python tools/make_font_subset.py <MochiyPopOne-Regular.ttf のパス>
  → Resources/kawaii_font.ttf と docs/assets/MochiyPopOne.ttf を上書きする。

フル版フォントは https://github.com/google/fonts/tree/main/ofl/mochiypopone
(SIL OFL 1.1。同梱の kawaii_font_LICENSE_OFL.txt を必ず残すこと)
"""
import re, sys, os
from fontTools import subset

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SOURCES = [
    'Source/Tooltips.h', 'Source/PluginEditor.cpp', 'Source/PluginEditor.h',
    'Source/PluginProcessor.cpp', 'Source/PresetDefs.h',
]
WEB_PAGES = ['docs/index.html']

def collect(paths):
    need = set()
    for rel in paths:
        p = os.path.join(ROOT, rel)
        if not os.path.exists(p): continue
        src = open(p, encoding='utf-8').read()
        for ch in src:
            if ord(ch) > 0x7e: need.add(ord(ch))
        for m in re.finditer(r'"((?:\\x[0-9a-fA-F]{2}|[^"\\]|\\[ntr"\\])+)"', src):
            s, by, i = m.group(1), bytearray(), 0
            while i < len(s):
                if s[i] == '\\' and i + 1 < len(s):
                    c = s[i+1]
                    if c == 'x': by += bytes([int(s[i+2:i+4], 16)]); i += 4; continue
                    if c == 'n': by += b'\n'; i += 2; continue
                    i += 2; continue
                by += s[i].encode('utf-8'); i += 1
            for ch in by.decode('utf-8', errors='ignore'):
                if ord(ch) > 0x7e: need.add(ord(ch))
    return {c for c in need if c >= 0xA0}

def base_keep():
    keep = set(range(0x20, 0x7F))
    keep |= set(range(0x3040, 0x3100))          # かな
    keep |= set(range(0xFF01, 0xFFA0))          # 全角英数 + 半角カナ
    keep |= {0x3000,0x3001,0x3002,0x300C,0x300D,0x300E,0x300F,0x3005,0x301C,
             0x2018,0x2019,0x201C,0x201D,0x2026,0x2025,0x2010,0x2012,0x2013,0x2014,0x2015,
             0x00B7,0x00D7,0x00F7,0x00B0,0x00B1,0x2190,0x2191,0x2192,0x2193,0x21D2,0x21D4,
             0x25A0,0x25A1,0x25B2,0x25B3,0x25B6,0x25B7,0x25BC,0x25BD,0x25C6,0x25C7,
             0x25CB,0x25CE,0x25CF,0x2605,0x2606,0x266A,0x266B,0x266D,0x266F,
             0x2713,0x2714,0x26A0,0x203B,0x3006,0x2460,0x2461,0x2462,0x2463,0x2464}
    return keep

def build(full, keepset, out):
    opts = subset.Options()
    opts.layout_features = ['*']; opts.name_IDs = ['*']
    opts.notdef_outline = True; opts.drop_tables += ['DSIG']
    fnt = subset.load_font(full, opts)
    ss = subset.Subsetter(opts); ss.populate(unicodes=keepset); ss.subset(fnt)
    fnt.save(out)
    print(f'{out}: {os.path.getsize(out)//1024} KB, {len(keepset)} codepoints requested')

if __name__ == '__main__':
    full = sys.argv[1] if len(sys.argv) > 1 else '/tmp/gfonts/ofl/mochiypopone/MochiyPopOne-Regular.ttf'
    ui  = base_keep() | collect(SOURCES)
    web = base_keep() | collect(SOURCES) | collect(WEB_PAGES)
    build(full, ui,  os.path.join(ROOT, 'Resources/kawaii_font.ttf'))
    build(full, web, os.path.join(ROOT, 'docs/assets/MochiyPopOne.ttf'))
