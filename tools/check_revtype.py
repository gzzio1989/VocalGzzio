#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
リバーブ種別の数が3か所で食い違っていないかを見る。

へや(畳み込み)を1部屋足すと、直さないといけない場所が3つある:
  1. Source/Heya.h        kNumRooms
  2. Source/PluginProcessor.cpp  rev_type の選択肢
  3. Source/PluginEditor.cpp     英語ラベルの表

どれか1つ忘れると、
  - コンボに出てこない
  - 英語UIだけ日本語のまま
  - 選ぶと別の部屋が鳴る
のどれかになる。目で数えるのはやめて機械に数えさせる。
"""
import re, sys, io
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
bad = []

def read(p):
    return io.open(ROOT / p, encoding='utf-8').read()

# --- 1. Heya.h の部屋数 ---
h = read('Source/Heya.h')
m = re.search(r'kNumRooms\s*=\s*(\d+)', h)
if not m:
    print('!! Heya.h に kNumRooms が見つかりません'); sys.exit(1)
num_rooms = int(m.group(1))

# RoomSpec の実体の数（nameEn に "IR" が付いている行を数える）
specs = len(re.findall(r'"[^"]*IR"', h))
print(f'  Heya.h        kNumRooms = {num_rooms} / RoomSpec の実体 {specs} 個')
if specs != num_rooms:
    bad.append(f'kNumRooms({num_rooms}) と RoomSpec の実体({specs})が違う')

# --- 2. PluginProcessor.h の定数 ---
ph = read('Source/PluginProcessor.h')
m = re.search(r'kHeyaFirst\s*=\s*(\d+)', ph)
if not m:
    print('!! PluginProcessor.h に kHeyaFirst がありません'); sys.exit(1)
heya_first = int(m.group(1))
expect = heya_first + num_rooms
print(f'  PluginProcessor.h  kHeyaFirst = {heya_first} → 種別は全部で {expect} 個のはず')

# --- 3. rev_type の選択肢の数 ---
pp = read('Source/PluginProcessor.cpp')
i = pp.index('pid ("rev_type")')
blk = pp[i : pp.index('}, 0));', i)]
choices = len(re.findall(r'juce::String::fromUTF8\s*\(', blk))
print(f'  PluginProcessor.cpp  rev_type の選択肢 {choices} 個')
if choices != expect:
    bad.append(f'rev_type の選択肢が {choices} 個。{expect} 個あるはず')

# --- 4. 英語ラベルの数 ---
pe = read('Source/PluginEditor.cpp')
j = pe.index('if (paramID == "rev_type")')
eblk = pe[j : pe.index('};', j)]
en = len(re.findall(r'"[^"]+"', eblk)) - 1     # "rev_type" 自身を引く
print(f'  PluginEditor.cpp     英語ラベル {en} 個')
if en != expect:
    bad.append(f'英語ラベルが {en} 個。{expect} 個あるはず（足りないと英語UIで日本語が出る）')

# --- 5. 説明文が部屋数ぶんあるか ---
tt = read('Source/Tooltips.h')
if 'rev_desc_heya' not in tt:
    bad.append('Tooltips.h に rev_desc_heya がない')
else:
    k = tt.index('rev_desc_heya')
    dblk = tt[k : tt.index('return {};', k)]
    cases = len(re.findall(r'case\s+\d+\s*:', dblk))
    print(f'  Tooltips.h    rev_desc_heya の case {cases} 個')
    if cases != num_rooms:
        bad.append(f'rev_desc_heya の case が {cases} 個。{num_rooms} 個あるはず')

print()
if bad:
    for b in bad: print('  !! ' + b)
    sys.exit(1)
print('  PASS: リバーブ種別の数が Heya.h / Processor / Editor / Tooltips で一致')
sys.exit(0)
