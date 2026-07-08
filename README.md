# ボーカルグッジオ (VocalGzzio) — Vocal Channel Strip VST3

by **G'zzio**

歌・トーク配信用のリアルタイム・ボーカルチャンネルストリップです。
SYNCROOM や配信でそのまま使える **遅延0サンプル設計**。

## 主な機能
- **プリセット**: 声質10種(女性5・男性5)× マイク10種(SM58 / SM7B / RE20 / e935 / MD421 / BETA 87A / U87 / C414 / AT2020 / NT1)× シーン3種(弾き語り / トーク配信 / Band)を自由に掛け合わせ
- **ノイズ除去(DE-NOISE + LEARN)**: 4帯域の適応型。LEARN ボタンで部屋のノイズを約1.5秒学習(遅延ゼロ)
- **2段コンプ**(ピーク+平準化)、**ディエッサー**、ノイズゲート
- EQ(ローカット・こもり・キンキン・Presence・Air)、Warmth、**のび**(伸びる倍音)
- Width / ダブラー / ディレイ / リバーブ
- チューナー(基準ピッチ可変)、IN / OUT / GR / DS / DN メーター
- A/B比較、プリセット保存/読込、「文字を大きく」ボタン
- 全ツマミに【効果】と【伝わり方】の2段説明(カーソルを合わせると表示)

## 動作環境
- Windows 10 / 11 (64bit)
- VST3 対応 DAW(REAPER 等)。単体起動できる Standalone 版も同梱
- **Mac は非対応です**

## インストール
**方法A(推奨): インストーラー**
1. Releases から `VocalGzzio_Setup_vX.X.X.exe` をダウンロードして実行
2. 初回起動時に SmartScreen の青い警告が出た場合は
   **「詳細情報」→「実行」** で進めてください(未署名のためで、異常ではありません)
3. DAW を再起動し、FX 追加で「VocalGzzio」を検索

**方法B: 手動コピー**
`VocalGzzio.vst3` フォルダを `C:\Program Files\Common Files\VST3\` にコピー → DAW を再起動。

## 使い方のコツ
1. 上のコンボで「声質」と「マイク」を選ぶ(下のシーンも)
2. 黙った状態で **LEARN** を押して1.5秒待つ → DE-NOISE を上げるとノイズが消えます
3. 歌うならTONEの「のび」を上げるとロングトーンが伸びて聴こえます

## ソースからビルドする場合
Visual Studio 2022/2026(C++ デスクトップ開発)+ CMake + Git が必要です。
```
cmake -B build -A x64
cmake --build build --config Release
```
出力: `build\VocalGzzio_artefacts\Release\VST3\VocalGzzio.vst3`

## ライセンス
本ソフトウェアは **GNU AGPLv3** で公開されています(LICENSE 参照)。
[JUCE](https://juce.com) フレームワーク(AGPLv3 デュアルライセンス)を使用しています。
VST は Steinberg Media Technologies GmbH の商標です。

## 商標について
記載のマイク名(Shure, Sennheiser, Electro-Voice, Neumann, AKG, Audio-Technica, RODE 等)は
各社の商標です。本プラグインは非公式であり、各メーカーとは一切関係ありません。
マイクプリセットは一般に知られる傾向をもとにした補正の目安です。

## 免責
本ソフトウェアは無保証で提供されます。自己責任でご利用ください。
