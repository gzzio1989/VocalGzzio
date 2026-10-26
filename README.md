# VocalGzzio 5.3.0

歌・話し声・アコースティックギター・VTuber配信・メタバース通話（cluster / VRChat / Discord / OBS / SYNCROOM）の音を整えるプラグイン＆単体起動アプリです。
ノイズ除去（**💧口元のペチャ音・リップノイズ瞬間除去**・**🏠ゼロ遅延・部屋鳴り（反響・風呂場感）除去**・**🌬️息継ぎ（ブレス）自動整え＆母音/子音セパレート（Velvet Split）**搭載）、音量そろえ（**🪶ささやき浮上・上向きコンプレッサー**）、音色（**💎倍音ドルビーAエアー**・**🎯音程追従オートスイートスポット**・2D声質重力場オーブ＆4軸成形）、**🎙️ゼロ遅延・4大名機マイクモデリング（✨800G真空管／👑U87王道／📻SM7B配信／💎ASMR極艶）**、音程（**🎯0.08ms知能ピッチ補正**）、空間IR残響、**🎧骨伝導補正・歌い手専用モニターエンハンサー（配信・録音は変えずに自分の耳への返しだけ歌いやすく補正）**、歌枠⇄雑談MCのワンクリック瞬間切替、5秒試聴ルーパー、カラオケ・BGMプレイヤー内蔵（**🕳️伴奏「声ポケット」自動掘り（Spectral Unmasker）**・**🎢伴奏追従ボーカルライダー**・**🎼伴奏キー＆BPM 1秒自動解析＆ワンクリック同期**・**📋歌枠セットリスト一括記憶**・**✨1クリック伴奏ボーカル除去**・キー変更±6＆自動ダッキング）、1クリックWAV録音＆直前30秒さかのぼり保存（**💿-14 LUFS自動音圧マスタリング＆伴奏込み完パケFullMix 3ファイル同時書き出し**）、**🔥サビ1クリック華やか演出 ＆ 🤖サビ自動検知（Auto-Chorus Director）**、**👏4種ライブSEポン出しパッド（拍手・キラ・ジャン・ピンポン）**、5秒声質ターゲット診断、絶叫セーフティガード＆咳払い瞬間ミュート、**「星空・海・黄昏」5大風景コレクション＆4つの表示・操作スタイル切替（標準デザイン／◉くっきり大文字／★かんたん3ステップ／●マット・具体表記）**、そして**あらゆるオーディオインターフェース（サウンドハウス・Amazon主要60ブランド・160機種以上）に対応する自動クロック同期・IN1〜IN4マルチ入力中央化・外部配信ブリッジ**を総合画面から調整できます。

> **更新状況（2026年10月27日）**：アップデート **VocalGzzio 5.3.0（Upward Air Lifter, Pitch-Tracked Sweet Spot & Singer's Cue Monitor Edition）** を公開しました。大きな声のピークを潰さずにウィスパーボイスや語尾の余韻だけを下から最大+4.5dBすくい上げ、母音にだけ4帯域マルチバンド倍音エアーを付加する **「🪶 ささやき浮上・上向きコンプレッサー ＆ 💎 倍音ドルビーAエアー」**、歌っている音程（f0）をリアルタイム追跡して高音では胸の響き（260Hz）を足しつつ3.2kHzの張り上げ痛みを抑え、低音では220Hzのこもりを引いて言葉の輪郭を出す **「🎯 音程追従・オートスイートスポット」**、OBS配信やWAV録音の音は一切変えずに自分のヘッドホン返しの骨伝導ダブり（240Hz）を引き算してピッチセンター倍音を際立たせる **「🎧 骨伝導補正・歌い手専用モニターエンハンサー」** を新搭載しました。実際に入手できるファイルは[BOOTHの商品ページ](https://gzzio.booth.pm/items/8764003)で確認してください。Windows版とMac版は同じ購入に含まれ、既存購入者も追加購入なしで無償アップデートできます。

[商品と体験版](https://gzzio.booth.pm/items/8764003) · [使い方](docs/manual.html) · [5.3.0の変更記録](docs/updates-5.3.0.md) · [5.2.0の変更記録](docs/updates-5.2.0.md) · [5.1.0の変更記録](docs/updates-5.1.0.md) · [5.0.0の変更記録](docs/updates-5.0.0.md) · [4.9.0の変更記録](docs/updates-4.9.0.md) · [4.8.0の変更記録](docs/updates-4.8.0.md) · [4.7.0の変更記録](docs/updates-4.7.0.md) · [過去の画面](docs/archive-ui-history.md) · [Macの導入と起動警告](https://gzzio1989.github.io/apps/vocalgzzio/mac-install.html)

---

## 💡 無料体験版（0円）と製品版（2,500円・買い切り）の違い

無料体験版でも、音声処理エンジン・超低遅延モード・全8モジュール・v5.0の4大新機能（伴奏ボーカル除去／-14 LUFS録音／サビ演出／4種ライブSE）・OBS直結仮想ドライバ（`VocalGzzio Driver (WDM)`）・全19テーマ＆4表示スタイルまで、**機能や音質は一切削っていません**。

| 機能・項目 | 無料体験版（0円） | 製品版（2,500円・税込買い切り） |
| :--- | :--- | :--- |
| **音声エンジン・全19テーマ・4表示スタイル・v5.0全機能** | ○ すべて製品版と同じ | **◎ すべて制限なし** |
| **連続使用時の試聴制限** | 60秒ごとに約0.6秒だけ音量が低下 | **◎ 音量低下なし（何時間の歌枠配信・録音でも途切れず使用可能）** |
| **設定・ノイズ学習・お気に入りテーマの自動保存＆読込（A/B比較）** | ×（終了すると初期状態へリセット） | **◎ 完全対応（毎回起動0秒でいつもの自分の声で歌える）** |
| **商用利用・収益化配信・動画投稿・OS対応** | 機材相性・音質確認用（公開・配信利用不可） | **◎ 収益化配信・スパチャ歌枠・案件・歌ってみた投稿・同人販売すべて自由（報告・クレジット不要）／Win＆Mac同梱（PC3台まで・無償アップデート対象）** |

---

## 🎙️ 対応オーディオインターフェース・配信ミキサー・USBマイク・VR機器 完全一覧表
**（サウンドハウス・Amazon 主要60ブランド / 160機種以上対応）**

お手持ちの機材がどのメーカー・どの端子構成であっても、VocalGzzio 単体起動版（Standalone）＋内蔵の専用仮想ドライバ **`ライン (VocalGzzio Driver (WDM))`** でそのままお使いいただけます。

| カテゴリ / メーカー | 主な対応機種（サウンドハウス・Amazon 定番＆最新モデル） | ハードウェア仕様・端子特徴 | VocalGzzio v5.0 自動対応機能 |
| :--- | :--- | :--- | :--- |
| **① 配信・実況・ゲームミキサー**<br>YAMAHA / Roland / audio-technica | ・**YAMAHA**: `AG03MK2` / `AG06MK2` / `AG08` / `ZG01` / `ZG02` / `AG03` / `AG06`<br>・**audio-technica**: `AT-UMX3`（Amazonベストセラー1位）<br>・**Roland**: `BRIDGE CAST` / `BRIDGE CAST X` / `BRIDGE CAST ONE` / `VT-4` / `E-4` / `GO:MIXER PRO-X` | ・`IN 1` マイク入力＋本体 `LOOPBACK` スイッチ搭載<br>・`GAME` / `CHAT` / `VOICE` / `STREAM` 等複数のWindows仮想WDMバスを搭載<br>・`AT-UMX3` はドライバレス最大192kHz対応 | **◎ 完全対応（推奨）**<br>・複数WDMバスのサンプルレート（48kHz等）を一括自動統一し、`cluster` 等の音消えを防止<br>・本体 `LOOPBACK` ON時のハウリング発振を0.35秒で自動検知し耳への返しを自動ミュート |
| **② DTM・歌枠・歌ってみた超定番**<br>Steinberg / Focusrite / Roland | ・**Steinberg**: `UR22C` / `UR12` / `UR24C` / `UR44C` / `UR816C` / `IXO12` / `IXO22` / `URX22C` / `URX44C` / `UR-RT2` / `UR-RT4`<br>・**Focusrite**: `Scarlett Solo` / `2i2` / `4i4` / `8i6` / `18i8` / `18i20`（第3〜第4世代） / `Vocaster One` / `Two` / `Clarett+`<br>・**Roland**: `Rubix22` / `Rubix24` / `Rubix44` | ・**`Scarlett Solo (第4世代)` はXLRマイク端子が右（`IN 2`）**<br>・**`Rubix44` / `UR44C` / `Scarlett 4i4` はマイク入力が `IN 1〜4`、サブヘッドホンが `OUT 3-4`** | **◎ 完全対応（推奨）**<br>・`IN 1〜IN 4` のどこにマイクを挿してもAC音声帯域検知で自動的に両耳中央（`L=R`）へ配置（手動固定も可）<br>・多ch機の `IN 1〜4` ＆ `OUT 1〜4`（2系統目ヘッドホン含む）を自動有効化 |
| **③ 高音質スタジオ定番**<br>SSL / MOTU / Universal Audio / Audient | ・**Solid State Logic**: `SSL 2` / `SSL 2+` / `SSL 2 MKII` / `SSL 2+ MKII` / `SSL 12`<br>・**MOTU**: `M2` / `M4` / `M6` / `UltraLite-mk5` / `828`<br>・**Universal Audio**: `Volt 1` / `Volt 2` / `Volt 4` / `Volt 176` / `Volt 276` / `Volt 476` / `Volt 476P` / `Apollo Twin` / `Apollo Solo`<br>・**Audient**: `iD4mkII` / `iD14mkII` / `iD24` / `iD44mkII` / `EVO 4` / `EVO 8` / `EVO 16` | ・**`MOTU M2/M4` や `Audient EVO 4/8` は `IN 3-4` が内部ステレオループバック端子**<br>・44.1kHz〜192kHzの高解像度ASIO駆動 | **◎ 完全対応（推奨）**<br>・`IN 3-4` にステレオのPCループバック音が流れていても誤吸い込みせず、単一マイク端子（`IN 1/2`）だけを正確に追従<br>・最大192kHzリサンプリング対応 |
| **④ 32bit Float・新世代機**<br>ZOOM / TASCAM / Lewitt / Topping | ・**ZOOM**: `UAC-232` / `UAC-432` / `UAC-2` / `AMS-22` / `AMS-24` / `AMS-44` / `PodTrak P4` / `P8` / `LiveTrak`<br>・**TASCAM**: `US-1x2HR` / `US-2x2HR` / `US-4x4HR` / `MiNiSTUDIO US-32W` / `US-42W` / `Series 102i` / `208i` / `Portacapture` / `FR-AV2`<br>・**Lewitt**: `CONNECT 2` / `CONNECT 6`<br>・**Topping**: `E2x2` / `E2x2 OTG` / `E4x4` | ・**`ZOOM UAC-232 / 432` や `URX` 等は32bit FloatデュアルAD搭載**（大声時に `+0dBFS` / `\|x\| > 1.0f` を超える浮動小数点信号が入力される） | **◎ 完全対応（推奨）**<br>・**32bit Float自動ヘッドルーム制御**を搭載。`+0dBFS` を超える突発的な大声入力も内部で自動パッド制御し、ピッチ補正やEQが一切クリップしません |
| **⑤ ハイエンドプロ機・統合配信コンソール**<br>RME / Neumann / Elgato / RODE / TC Helicon / Antelope / Apogee | ・**RME**: `Babyface Pro FS` / `Fireface UCX II` / `Fireface UFX II` / `UFX III` / `Digiface`<br>・**Neumann**: `MT 48` ・**Antelope Audio**: `Zen Go` / `Zen Quadro` / `Discrete 4/8`<br>・**Apogee**: `Boom` / `Duet 3` / `Symphony Desktop`<br>・**RODE**: `RODECaster Pro II` / `RODECaster Duo` / `Streamer X` / `AI-1`<br>・**Elgato**: `Wave XLR` / `Wave:3`（Wave Link対応）<br>・**TC Helicon**: `GoXLR` / `GoXLR Mini` / `Perform-V` | ・TotalMix FX / Wave Link / GoXLR / RODECaster 等のマルチバス・仮想ルーティング搭載 | **◎ 完全対応（推奨）**<br>・`VocalGzzio Driver (WDM)` ＋ `Yamaha SYNCROOM Driver` ＋ `VB-CABLE` の**最大3系統同時ミラー送出**に対応し、どの仮想バスへも遅延30msで直結可能 |
| **⑥ コスパDTM・ミキサー・ギター系**<br>PreSonus / Arturia / NI / Behringer / M-Audio / Mackie / Blackstar / IK | ・**PreSonus**: `AudioBox GO` / `USB 96` / `Studio 24c` / `26c` / `68c` / `Revelator io24` / `io44` / `Quantum ES 2` / `ES 4`<br>・**Arturia**: `MiniFuse 1` / `2` / `4` / `AudioFuse`<br>・**Native Instruments**: `Komplete Audio 1` / `2` / `6`<br>・**Behringer**: `UMC22` / `UM2` / `UMC202HD` / `204HD` / `404HD` / `FLOW 8`<br>・**M-Audio**: `M-Track Solo` / `Duo` / `AIR 192\|4` / `6` / `8`<br>・**Mackie**: `Onyx` / `ProFX` / `M-Caster` / `MainStream` ・**Blackstar**: `Polar 2` / `4` ・**IK Multimedia**: `AXE I/O` / `iRig Pro` / `iRig Stream` | ・専用ASIOドライバ搭載機、および `ASIO4ALL v2` 等の排他ラッパーを使用するエントリー機 | **◎ 完全対応**<br>・`ASIO4ALL` 等でWindowsの他アプリの音がロックされる機種でも、ワンクリック **「🌐 全アプリ共存・共有モード（WASAPI共有）」** ボタンで `cluster` やブラウザと完全共存 |
| **⑦ USBマイク・ゲーミングDAC・VR機器**<br>Shure / HyperX / Blue / FIFINE / Maono / Sound Blaster / SteelSeries / Meta Quest | ・**USBマイク**: `Shure MV7+` / `MV7` / `MVX2U`、`HyperX QuadCast S` / `2` / `SoloCast`、`Blue Yeti` / `Nano`、`audio-technica AT2020USB-X` / `AT2040USB`、`Razer Seiren`<br>・**配信ミキサー/DAC**: `FIFINE AmpliGame SC3`、`Maono AME2` / `G1 NEO`、`Razer Audio Mixer`、`Creative Sound Blaster GC7` / `X4` / `G6`、`SteelSeries GameDAC Gen 2` / `Arctis Nova`、`Astro MixAmp`、`Sony INZONE`<br>・**VR機器**: `Meta Quest 3 / 3S / 2 (Oculus Virtual Audio)`、`SteamVR (Index / Bigscreen Beyond)`、`VIVE`、`PICO 4` | ・入力（USBマイクやVRマイク）と出力（ヘッドホンやVRスピーカー）が**別々のデバイス**に分かれる構成 | **◎ 完全対応**<br>・**「🌐 全アプリ共存・共有モード」** を押すだけで、別々のUSBマイク入力とヘッドホン／VRスピーカー出力を自動で束ね、`VocalGzzio Driver (WDM)` へ加工後の声を送出します |

---

## 最初の3ステップ

1. 入力の種類を選び、いちばん強く歌うところで入力が歪まないよう音声機器の音量を合わせます（マイクが `IN 1〜IN 4` のどこに挿さっていても、自動で両耳中央 `L=R` に揃えます）。
2. 「うた自動」の自動調整か「仕上がり」、または「🎯 ステージ＆スタジオ」の **5秒声質ターゲット診断** を選びます。中央のオーブや4本のバーで、温かみ（倍音）・声の輪郭（声芯）・空気感（空気）・広がり（立体）を調整できます。
3. `🔄5秒試聴` で自分の声を5秒録ってループ再生しながら、「押して原音と比較」で聴き比べ、出力音量と残響を仕上げます。`cluster`・`VRChat`・`Discord`・`OBS` で使うときは、相手アプリのマイクに **`ライン (VocalGzzio Driver (WDM))`** を選ぶだけです。
