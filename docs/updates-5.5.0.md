# VocalGzzio 5.5.0 変更記録（2026年10月31日）

**VocalGzzio 5.5.0（Smart Sleep Ultra-Light & Live Streamer Cockpit Edition）** では、音質劣化ゼロのまま CPU・UI/GPU 負荷を 40〜70% 削減する **「⚡ Smart Sleep 超軽量エンジン ＆ 1回共通FFTマルチボイス・ハーモナイザー」** を土台に、OBS や歌詞・コメント欄の隅に常駐できる **「📌 配信ミニバー（520×115 px）」**、仮想ドライバ不要で YouTube やブラウザの音だけを取り込める **「🌐 アプリ別カラオケ音声取込 ＆ ⚡音ズレ1クリック自動計測（±300 ms）」**、裏画面や全角かな入力中でも誤爆しない **「⌨️ 裏画面・全角かな無敵グローバルホットキー」**、J-POP・アニソンの借用和音や転調でも濁らない **「🎹 伴奏コード追従スマートハモリ」**、歌ってみた投稿を外部ソフト不要で完結させる **「🎬 SNS投稿用ビジュアライザ動画（.mp4 / 16:9 & 9:16 / Before➔After比較）1クリック保存」**、および **OBS 標準 VST3・ループバック・外部仮想ケーブル連携ガイドの刷新** を行いました。

---

## 1. ⚡ Smart Sleep 超軽量化エンジン ＆ 1回共通FFTハーモナイザー（音質劣化ゼロで CPU / UI 負荷を 40〜70% 削減）

- **① UIスレッド重複 YIN ピッチ解析の完全廃止（ロックフリー F0 共有）**：
  これまで画面のチューナー表示（`VocalTuner`）が UI スレッド上で毎秒 30 回・最大 32,768 サンプルの YIN 自己相関解析を重複実行していた構造を刷新し、うた／しゃべりモード時は音声スレッドの `PitchDetector` が検出した基本周波数（$f_0$）と信頼度をロックフリー（`std::atomic<float>`）で O(1) 共有する方式へ移行しました（ギター・ベースの多弦チューニング表示中のみ長窓 YIN 解析が稼働します）。
- **② エディタ描画タイマー＆マウス走査の省電力スロットリング**：
  ウィンドウの最小化時・背面非表示時（`!isShowing()` / `isMinimised()`）や配信ミニバー切替中は、メイン画面および各種ポップアップのタイマー再描画と `getComponentUnderMouse()` 走査を完全スキップし、裏画面での配信・ゲームプレイ中の UI / GPU 負荷を **実質 0%** に抑えました。さらに通常表示中もマウス静止時はヒットテスト走査を休止します。
- **③ 1回共通FFT解析・複数声部合成（`MultiVoiceHarmonyShifter`）＆ 純ユニゾン声部ゼロFFT化**：
  3声コーラスや4声ゴスペルなど複数ハモリ声部の稼働時、これまで最大4回個別に行っていた前進 STFT・位相微分・真包絡（True-Envelope）・ピーク所有権解析を **1回だけ共通実行** する新設計へ刷新しました。さらに度数シフトのない純ユニゾン声部はゼロ FFT の分数遅延モジュレーション ADT へ最適化し、0.5 秒無音スリープ（発声 1 サンプル目から遅延なく即復帰）と合わせて重い 3D アバター＋OBS 同時起動時の負荷を大幅に引き下げました。

---

## 2. 📌 ワンクリック『配信ミニバー』モード（520×115 px・最前面ピン留め対応）＆ 画面スッキリ整理

- **『📌 ミニバー』ワンクリック切替**：
  総合画面ヘッダー（および「🎵 BGM・録音」ステージハブ上部）に新設された **`📌 ミニバー`** ボタンを押すと、OBS・YouTube 歌詞画面・配信コメント欄・ゲーム画面の隅に邪魔にならず置ける **`520 × 115 px` のコンパクト常駐バー**へ瞬時に切り替わります。
  - ミニバー搭載機能：リアルタイム `IN` / `OUT` レベルメーター、音ズレ補正・裏ホットキー稼働バッジ、**`🎤 歌枠モード ⇄ 💬 雑談MC中`**、**`😷 咳ミュート`**、**`★ サビ盛`**、**`👏 SE`**（左クリックで拍手即ポン出し／右クリックまたは Shift+クリックで内蔵4種＋自作4枠の全8種SEメニュー）、伴奏キー **`♭-1` / `伴奏 ±0` / `♯+1`**、**`⏪ 30秒保存`**、**`📌 最前面`**（Standalone 時の Always-on-Top ピン留め）、**`↗ 通常画面`**（元のウィンドウ倍率・サイズへ完全復帰）。
- **『✨ こだわり詳細』折りたたみ整理**：
  「🎵 BGM・録音（ステージ＆スタジオ・ハブ）」内の細かなこだわりスイッチ群（声ポケット掘り・伴奏追従ライド・ささやき浮上・音程追従・骨伝導モニター・歌唱表現4エンジン・ペチャ音/部屋鳴り/息保護・自作SE登録枠）を、既定でスッキリ収納しつつ稼働中エンジン数をバッジ表示し、ワンクリックでいつでも展開・調整できる折りたたみ構造に整理しました。

---

## 3. 🌐 アプリ別カラオケ音声取込（仮想ドライバ不要）＆ ⚡ 声⇄伴奏「音ズレ」1クリック自動計測（±300 ms）

- **🌐 アプリ別カラオケ音声取込（Windows 10/11 Process Loopback Capture）**：
  仮想オーディオドライバを一切インストールすることなく、Windows 上で起動中のブラウザ（Chrome / Edge 上の YouTube カラオケ音源等）や音楽・カラオケアプリの **「指定した 1 アプリの音だけ」** を直接 VocalGzzio へ取り込めます（Discord の通話相手の声や VocalGzzio 自身の出力は回り込みません）。
  - 取り込んだアプリ音声にも、内蔵 BGM と同じく **✨ 原曲 1 クリック伴奏ボーカル除去**・**キー変更（±6半音）**・**🎹 伴奏コード追従ハモリ**・**🕳️ 声ポケット自動掘り**・**声優先ダッキング**・**WAV / SNS 動画同時録音** がすべて適用されます。
  - Windows 上で既に鳴っているアプリ音と二重聴こえ（フランジャー現象）を起こさないよう、既定では自分のヘッドホンへの二重加算を自動抑止し、録音・配信出力（StreamOut）にだけ同期合成します（キー変更・ボーカル除去後の音を自分の耳でも聴きたいときはメニュー内チェック1つで返しONにできます）。
- **⚡ 声⇄伴奏「音ズレ」1クリック自動計測 ＆ ミリ秒補正スライダー（`-300 ms` 〜 `+300 ms`）**：
  - **`⚡ 自動計測` ボタン**：ヘッドホンやスピーカーにマイクを近づけて押すと、「コンッ」という短いウッドブロック・クリック音を 1 発鳴らしてオーディオ IF の往復遅延＋エフェクト処理遅延を自動実測し、最適な音ズレ補正値（ms）をワンクリックで自動セットします。
  - 手動スライダーでも **1ms 単位（`-300 ms` 〜 `+300 ms`）** で微調整可能です（`+`側：マイク歌声を遅延 ／ `-`側：伴奏を遅延 ／ `0ms` ボタンでリセット）。

---

## 4. ⌨️ 裏画面・日本語 IME（全角かな）無敵のグローバルホットキー（Windows Standalone 版）

- OBS・ブラウザ（歌詞や配信コメント欄）・ゲーム画面が最前面にある裏画面状態や、日本語 IME が「全角かな入力中（`VK_PROCESSKEY`）」の状態でも文字入力と誤爆せず確実に反応する Win32 直接キー監視ホットキーを単体起動版（Standalone）に搭載しました（「🎵 BGM・録音」内の **`⌨️ 裏キー: ON/OFF`** でいつでも切替可能。DAW プラグイン版ではホスト DAW のショートカットを妨げないよう自動的に無効化されます）。
  - **`Ctrl + Shift + M`** ： 😷 咳払いミュート ON / OFF トグル
  - **`F9` キー長押し** ： 😷 押している間だけ一時咳ミュート（離すと即復帰する Push-to-Mute）
  - **`Ctrl + Shift + T`** ： 🎤 歌枠モード ⇄ 💬 雑談MCモード 切替
  - **`Ctrl + Shift + S`** ： ★ サビ盛り上げ（Chorus Boost）ON / OFF 切替
  - **`Ctrl + Shift + R`** ： ⏪ 直前30秒さかのぼり WAV 保存

---

## 5. 🎹 伴奏コード追従スマートハモリ（借用和音・ラスサビ半音転調の自動和音吸着）

- 内蔵 BGM や「🌐 アプリ音取込」で鳴っている伴奏から 36 ビン複素フェーザバンクでリアルタイムに現在の和音（`[C]` / `[Fm]` / `[E7]` 等）を検出し、ボイチェン＆18種ハモリパネルの **`🎹 伴奏コード追従スマートハモリ`** が ON（既定 ON）のとき、J-POP やアニソンで頻出する **借用和音（`III7` の `G#` やサブドミナントマイナー `IVm` の `Ab` など）** や **ラスサビの半音・全音転調** で固定スケール度数が伴奏と半音衝突する瞬間を検知し、主旋律と協和する伴奏和音の構成音へスマート吸着します。

---

## 6. 🎬 SNS投稿用ビジュアライザ動画（`.mp4` / `16:9` & `9:16` / `Before➔After比較`）1クリック保存

- 歌い終わった直後の WAV 録音テイク、または録音ボタンを押していなくても常時蓄えられている **直前 30 秒さかのぼりバッファ** から、動画編集ソフトや外部 FFmpeg を一切使わずに **H.264 映像 ＋ AAC 192kbps 音声（`-14 LUFS` 自動音圧マスタリング済）の `.mp4` 動画** をワンクリックで書き出せます（「🎵 BGM・録音」内 **`🎬 SNS動画▼`**）。
  - **📺 `16:9` 横型（`1280×720`）**：X（旧 Twitter）・YouTube 投稿にそのまま使えるワイド動画
  - **📱 `9:16` 縦型（`720×1280`）**：YouTube Shorts・TikTok・Instagram Reels にそのまま投稿できる縦型ショート動画
  - **✨ `素の声(Before) ➔ 加工後(After)` 聴き比べモード**：前半 50% を加工前の素の声（Dry）、後半 50% を VocalGzzio 艶加工後（`-14 LUFS` Wet）へ滑らかにクロスフェード切り替えし、視覚バッジとフラッシュリング演出付きで音質変化を一発アピールできる宣伝動画を自動生成します（右下の `✦ VocalGzzio` 透かしロゴ表示も任意で ON/OFF 切替可能）。

---

## 7. 🎤 声量幅（ダイナミクス）3モード切替・直感『最終音量（0%〜250%）』・大音量時の音質刷新 ＆ UI/バックナンバー/起動時更新チェック改善

- **🎤 声量幅3モード切替（`🎵声量幅:標準バランス` ⇄ `🎤声量幅:10割!` ⇄ `📻声量幅:一定キープ`）**：
  「10割の声量で全力歌唱しても7割くらいに頭打ちして感じられる」というご要望にお応えし、直列コンプレッサー比率と自動音量ライドの引き下げ量を歌唱向けに抜本緩和したうえで、総合画面の出力セクションからワンクリックで **`🎤声量幅:10割!`（サビの全力発声が潰れずダイレクトに伸びるモード）**・**`🎵標準バランス`**・**`📻声量幅:一定キープ`（配信トーク向け均一音量）** を切り替えられるようにしました。
- **🔊 初心者にも直感的な『最終音量（`0%` 〜 `250%`）』＆ 適正ゾーンガイドメーター**：
  従来の `+dB` 増幅のみの表記から、**`100% ＝ 標準（等倍）` / `0〜99% ＝ 音量を下げる` / `101〜250% ＝ クリアに音量を上げる`** のパーセントノブと、初心者でも一目で分かる **`小さい / ◎適正ゾーン / 注意`** メーターガイドへ刷新しました。
- **💎 音量アップ時の歪み・音質劣化の根絶（最終段クリーン線形増幅 ＆ 滑らかピークホールド保護）**：
  出力増幅（`makeup` / `最終音量%`）をすべての倍音飽和回路（Warmth / Sustain / Argon）の **完全後段** へ移動し、さらに絶叫セーフティガードに `15 ms` ピークホールド＋`120 ms` スムース復帰を導入したことで、音量を大きく上げた際の天井飽和歪み（奇数次高調波歪み・ビリつき）を根絶しました。
- **🎨 ノブ内部装飾デザインの ON / OFF 切替 ＆ スライドスイッチ視認性修正**：
  「文字・見た目」パネルに **`🎨 ノブ内デザイン: ON / OFF`** を新設し、テーマごとの装飾イラスト入りノブとスッキリ見やすいシンプルノブをいつでも切り替え可能にしました。また、トグルスイッチ内部に二重で小さなスイッチが描画されていた表示不具合を修正し、大きく押しやすいピル型スライドスイッチへ統一しました。
- **🕰️ 歴代バージョン呼び出し（世代タイムマシン）全 19 世代完全対応**：
  欠落していた **`v5.0.0` / `v5.1.0` / `v5.2.0` / `v5.3.0` / `v5.4.0` / `v5.4.1`** のバックナンバーをすべて復元し、初代 `v1.0.0` 〜 最新 `v5.5.0` までの **全 19 世代** を 3 列一覧ポップアップから自在に呼び出せるようにしました。
- **🆙 Cubase 等の DAW 終了ハングを完全回避した『起動時1回アップデート確認』機能**：
  プラグインプロセス内での通信スレッド待機を一切行わず、起動時に **1回だけ** 完全分離された別プロセス（ハンドル継承なし・即時クローズ・3秒タイムアウト）で GitHub 最新リリースを非同期確認する安全設計を採用しました。Cubase 等の DAW 終了を1ミリ秒も妨げず、新しいバージョンがある時だけ控えめな **`🆙 更新 vX.Y.Z`** バッジでお知らせします（「文字・見た目」から自動確認の ON/OFF や手動チェックも可能）。

---

## 8. 🛡️ 独自仮想ドライバ同梱の見送りと OBS・配信アプリ連携ガイドについて

- Windows 10/11 の最新カーネル保護および Microsoft ハードウェア署名審査スケジュールへの適合に万全を期すため、**独自仮想ドライバの同梱は次期以降のロードマップへ一旦先送り**いたしました（以前の告知でお待ちいただいていた皆さまへ明確にご案内いたします）。
- その代わり、仮想ドライバを一切導入しなくてもブラウザ・カラオケ音源を直接取り込める上記 **「🌐 アプリ別カラオケ音声取込」** を新搭載したほか、OBS・通話アプリへの接続には安定実績の高い以下の 3 通りのゼロ遅延連携をご案内しております（アプリ内「配信出力」パネルの **`📖 OBS・配信連携ガイド`** でも図解手順を確認できます）：
  1. **OBS 標準 VST3 プラグイン直接読込**（OBS のマイクフィルタから `VocalGzzio` を選ぶだけ・仮想ケーブル不要）
  2. **オーディオインターフェース内蔵ループバック**（AG03/AG06・UR22C・MOTU M2/M4 等・ハウリング自動遮断ガード内蔵）
  3. **定番の外部仮想ケーブル連携**（`VB-CABLE` / 公式 `Yamaha SYNCROOM` 等を「配信出力」が自動検出してブリッジ送出）

---

## 🌐 English Release Summary (VocalGzzio v5.5.0)

1. **⚡ Smart Sleep Ultra-Light Engine & Single-FFT Multi-Voice Harmonizer (-40% to -70% CPU/UI Load)**: Lock-free audio-thread $f_0$ sharing eliminates duplicate 30Hz UI YIN analysis, minimized/hidden UI & GPU polling drops to ~0%, `MultiVoiceHarmonyShifter` shares one forward STFT analysis across up to 4 harmony voices while pure unison voices use zero-FFT modulated ADT, and a 0.5s silence sleep gate wakes on sample 1.
2. **📌 1-Click Streaming Mini-Bar (`520×115 px`, Always-on-Top Pin) & UI Decluttering**: Dockable compact overlay with live `IN`/`OUT` meters, Singing ⇄ Talk MC, Cough Mute, Chorus Boost, 8-pad SE trigger, BGM Key `±`, 30s Retroactive Save, and `✨ Fine Details` collapsible drawer.
3. **🌐 App-Specific Karaoke Audio Capture & ⚡ 1-Click Auto-Sync Calibration (`±300 ms`)**: Captures audio directly from Chrome/YouTube/Edge/Karaoke via Windows 10/11 Process Loopback without any virtual driver or Discord feedback, plus 1-click woodblock click round-trip latency measurement and 1ms manual sync slider.
4. **⌨️ Background IME-Immune Global Hotkeys (Windows Standalone)**: `Ctrl+Shift+M` (Cough Mute toggle), Hold `F9` (Push-to-Cough-Mute), `Ctrl+Shift+T` (Singing ⇄ Talk MC), `Ctrl+Shift+S` (Chorus Boost), and `Ctrl+Shift+R` (Save Last 30s WAV).
5. **🎹 Live Chord-Following Smart Harmony**: Real-time 36-bin accompaniment chord chroma tracking snaps harmony voices cleanly onto borrowed chords (`III7`, `IVm`) and final-chorus key modulations.
6. **🎬 1-Click SNS Visualizer Video Export (`.mp4` / H.264+AAC / `16:9` & `9:16` / `Before➔After`)**: Exports `-14 LUFS` mastered landscape or vertical Shorts/TikTok MP4 videos directly via Windows Media Foundation with no external FFmpeg required.
7. **🎤 Full 100% Vocal Dynamics Mode, Distortion-Free `0-250%` Output Volume, Knob Inner Artwork Toggle, Full 19-Era Time Machine (`v1.0.0`–`v5.5.0`) & DAW-Safe Startup Update Check**: Unlocks full 100% singing expression without ceiling squash, moves final volume completely post-saturation with a `15 ms` hold / `120 ms` smooth safety guard, adds a knob inner art ON/OFF toggle and full-size slide switches, restores `v5.0.0`–`v5.4.1` back-numbers, and checks for updates once at startup in a detached process with zero Cubase/DAW shutdown blocking.
8. **🛡️ Virtual Driver Roadmap Notice & OBS Routing**: To ensure full compliance with Windows 10/11 kernel security and Microsoft hardware signature certification scheduling, bundling of a proprietary virtual driver has been postponed to a future roadmap release; v5.5.0 provides driverless App Audio Capture alongside rock-solid OBS native VST3 direct loading, built-in interface loopback, and external virtual cable (`VB-CABLE` / official `Yamaha SYNCROOM`) integration.
