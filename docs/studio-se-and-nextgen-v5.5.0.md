# VocalGzzio v5.5.0 — 4大スタジオライブSE音源刷新 ＆ 次世代マスタリング・声道物理・複雑時計機構 技術仕様書

## 1. 4大ライブSEパッド（`拍手` / `キラ` / `ドラム` / `ベル`）の物理モデリング＆フォーリー全面刷新

従来の `VocalGzzioProcessor::ensureLiveSeBuilt` では、各SEが単純なサイン波の重ね合わせで作られており、さらに最終ピークが `-19.1 dBFS`（`0.11f`）に抑えられていたため、歌やBGMの背後で音質・音量ともに貧弱に聞こえる課題がありました。

`v5.5.0` では、`Source/StudioLiveSoundEffectsEngine.h` および `Source/StudioFoleyAndStageAmbienceBank.h` を新設し、世界最高峰の物理モデリング・統計集団音響合成と **8×8 直交アダマールFDN コンサートホール残響**、さらに **`-6.8 dBFS`（`0.42f〜0.48f`）スタジオ・マスタリング正規化** によって、放送局・劇場クオリティの4大音源へ完全刷新しました。

---

### ① `♪拍手`（SE 0: コンサートホール220名集団拍手 ＋ 歓声母音フォルマント共鳴 — `3.0秒`）
- **物理モデル**: Peltola, Välimäki & Erkut (*IEEE/ACM Trans. Audio, Speech, and Language Processing*, "Synthesis of Hand Clapping Sounds") の統計的パームクラップ物理モデルを3層構造で実装。
- **第1層（前列28名の近接リアルクラップ）**:
  - 人間の左右の手のひらが衝突する際の親指側・指先側の微小時間差（二重打撃 $1.2\sim 2.6\text{ ms}$）を再現。
  - 手のひらのカップ形状による **ヘルムホルツ空洞共振 $F_1 \in [740, 1380]\text{ Hz}$** と、平坦な指先スラップによる **高域スラップ共振 $F_2 \in [1980, 3330]\text{ Hz}$** をTPT状態変数フィルタで個別に励振。
  - 1人ずつ異なる拍手テンポ（$4.4\sim 6.7\text{ Hz}$）とヒューマンタイミング揺らぎ、Haas 効果（両耳間時間差 ITD 最大 $0.55\text{ ms}$ ＆ ILD）で180°のステージ前方に配置。
- **第2層・第3層（中列・バルコニー席のアンサンブル層 ＆ 温かい母音歓声フォルマント層）**:
  - 104発のホール拡散拍手粒子に加え、`EncoreBravosAndWarmHallCheerSynthesizer` による観客の温かい母音フォルマント共鳴レイヤーをブレンドし、「サーッ」というホワイトノイズ感が一切ない劇場の拍手喝采を実現。

---

### ② `✦キラ`（SE 1: Mustel チェレスタ × コンサートハープ × 36本真鍮ウィンドチャイム × グラスハーモニカ — `2.5秒`）
- **第1層（Mustel チェレスタ 16音リディアン9th・カスケード）**:
  - Euler-Bernoulli 自由端鋼鉄バーの非整数次横振動モード比 $1.000 : 2.756 : 5.404$ と木製ヘルムホルツ共鳴箱、フェルトハンマーのソフト打撃包絡を合成。
- **第2層（オーケストラ・グランドハープ 8弦和音共鳴）**:
  - C4〜E6 のハープ開放弦プラックとスプルーストップ響板の温かい胴鳴り（第2・第3倍音）をレイヤー。
- **第3層（36本ソリッドブラス・ウィンドチャイム / Mark Tree）**:
  - $2.95\text{ kHz}\sim 11.2\text{ kHz}$ に指数配置された36本の真鍮円筒チューブが、指で撫でたように順次衝突し、円筒楕円非対称性による美しいスピンうなり（$1.0038\times f_0$）を伴って左右へきらめき広がる粒子群を合成。
- **第4層（クリスタル・グラスハーモニカ ＆ 8×8 FDN クリスタルホール残響）**:
  - `GlassHarmonicaAndBisbigliandoShimmerSynthesizer` によるクリスタルガラスボウルの摩擦共鳴（$1.000 : 2.320$）を重ね、気品ある透明な余韻を実現。

---

### ③ `●ドラム`（SE 2: マルチバウンス・コンサートスネア・プレスロール ＆ ティンパニ → 青銅クラッシュ ＆ 大太鼓・ドラ・ファンファーレ — `2.7秒`）
- **前半 $0.00\text{ s}\sim 1.24\text{ s}$（本物のオーケストラ・プレスロール / Buzz Roll ＆ 32インチ・ティンパニ・クレッシェンド）**:
  - 左右スティックの26ストロークごとに、ヘッド上でスティックが4回跳ね返る **多重バウンス物理（計104発のマイクロ打撃）** を再現。
  - 円形膜のベッセル関数モード比 $J_{mn}$ ($1.000 : 1.593 : 2.135 : 2.295 : 2.653 : 2.917$) ＋ メイプル胴共鳴 ($315\text{ Hz}$) ＋ 裏面20本コイル鋼線（Snare Wires: $2.35\text{ kHz} / 4.85\text{ kHz}$）がヘッド振幅に連動してパリッと鳴り、ピアニッシモからフォルティッシモへ劇的にクレッシェンド。
  - さらに裏で32インチ銅製ティンパニ（$G_2 = 98\text{ Hz} \to C_3 = 130.8\text{ Hz}$）がロールで迫り上がる重厚な構成。
- **後半 $1.26\text{ s}\sim 2.70\text{ s}$（クライマックス「ジャン！！」フィナーレ）**:
  - スネア・リムショット ＋ ティンパニ強打 ＋ **40モード Kirchhoff-Love 青銅クラッシュシンバル** ＋ **40インチ・グランカッサ大太鼓（$52\text{ Hz}$）** ＋ **38インチ青銅タムタム（非線形モードブルーム）** ＋ **7声シンフォニック・ブラス $C\text{maj}9$ ファンファーレ和音** が一斉に炸裂。

---

### ④ `◆ベル`（SE 3: Musser ヴィブラフォン × オーケストラ・チューブラーベル × 5倍音カリヨン大鐘 — `2.3秒`）
- **3ステップ・ラグジュアリー放送チャイム＆ $D\text{maj}9$ 解決カデンツ**:
  - Musser ヴィブラフォンのアーチ削り出しアルミ音板（基音 : 第4倍音 $= 1.000 : 4.000$）と、$5.4\text{ Hz}$ 回転バタフライ・モーターレゾネーターによる L/R 直交トレモロ。
  - オーケストラ・チューブラーベルのクラウン打撃非整数倍音（$1.000 : 2.756 : 5.404$）。
  - さらに `FivePartialCarillonAndHandbellChoirSynthesizer` によるヨーロッパ伝統のメジャーサード・カリヨン5部分音調律（Hum $0.500$ : Prime $1.000$ : Tierce $1.2599$ : Quint $1.4983$ : Nominal $2.000$）とイングリッシュ・ハンドベルの12度倍音（$3.000$）を重ね、Bricasti M7 風ホール残響で仕上げました。

---

## 2. 次世代マスタリング・声道物理・スイス複雑時計機構モジュール一覧（全23新規ヘッダ）

| # | ヘッダファイル | 主要クラス・物理アルゴリズム |
|---|---|---|
| 1 | `StudioLiveSoundEffectsEngine.h` | 4大ライブSE物理モデリング合成器 ＆ 8×8 アダマールFDN コンサートホール残響 |
| 2 | `StudioFoleyAndStageAmbienceBank.h` | 歓声母音フォルマント、グラスハーモニカ、40インチ大太鼓＆タムタム、5倍音カリヨン鐘 |
| 3 | `NeuralFormantAndGlottalSynth.h` | Fant LF声門パルスモデル、14次Levinson-Durbin LPC声道包絡、胸腔サブ倍音共振 |
| 4 | `MultibandLinearPhaseCrossoverEngine.h` | Linkwitz-Riley 4次 (24 dB/oct) オールパス位相補償TPT 4バンドクロスオーバー＆M/S制御 |
| 5 | `AnalogTubeAndOptoMasteringChain.h` | Fairchild 670 Vari-Mu 真空管、Teletronix LA-2A T4B オプト、Manley Massive Passive EQ |
| 6 | `AccompanimentDuckingAndStemSculptor.h` | Trackspacer型8バンドM/S Mid スペクトル彫刻ダッカー＆ゼロクリック先読みサイドチェイン |
| 7 | `LiveStageAndVtuberDirectorEngine.h` | 自動アンコール演出ディレクター、5母音 (`/a,i,u,e,o/`) リップシンクViseme推定、Camelotキー遷移 |
| 8 | `PrecisionPitchAndVibratoSynthesizer.h` | TD-PSOLA ピッチ同期合成、日本の歌唱表現（しゃくり・こぶし・フォール）生成、12声クワイア |
| 9 | `MicrophoneAndPreampPhysicalTwin.h` | U87風静電コンデンサーカプセル非線形容量、リボンマイク近接効果、2520 DOA＋Cinemagトランス |
| 10 | `MasteringLimiterAndTruePeakEngine.h` | 8倍ポリフェーズHermite ISP True-Peak検出器、2段IRCマスタリングリミッター、EBU R128 LUFS/LRA計 |
| 11 | `DynamicDePlosiveAndDeClickEngine.h` | 高調波コム保護型ポップノイズ除去、AR(2) LMS自己回帰リップクリック修復、ブレス保護ゲート |
| 12 | `BinauralHeadphoneAndRoomCalibration.h` | 球体頭部回折ITD/ILDクロスフィード、耳介Pinna前方定位ノッチ、Harmanターゲットヘッドホン較正 |
| 13 | `VintageDigitalReverbAndDelayRack.h` | Lexicon 224 オールパスタンク、AMS RMX16 NonLin2 ゲート空間、Roland Dimension D 4モードコーラス |
| 14 | `AdaptiveAutoEqAndResonanceSuppressor.h` | Soothe型16バンド相対閾値・動的レゾナンス抑制、Gullfoss型ピンクノイズ・スペクトルチルト自動サーボ |
| 15 | `VocalDoublerAndMicroShiftArray.h` | Abbey Road Studer J37 ヴァリスピードADT、Eventide H3000風 ±8.5 cents デュアル窓マイクロピッチ |
| 16 | `HarmonicSeriesSculptorAndSubBass.h` | レベル不変チェビシェフ多項式 ($T_2\dots T_5$) 倍音造形器、ゼロ交差2分周位相同期サブオクターブ合成 |
| 17 | `MasteringStereoFieldAndPhaseDoctor.h` | ピアソン位相相関＆逆相自動レスキュー、モノラル合成時キャンセレーション完全ゼロ4段オールパスワイドナー |
| 18 | `StreamingAudioRouterAndSafetyMatrix.h` | 5次エルミート8.5msゼロクリック咳払いミュート、ハウリング自動検知＆高Qノッチ遮断、OBS/イヤモニ2系統バス |
| 19 | `PsychoacousticTransientAndSustainEngine.h` | SPL型レベル不変・差動包絡線トランジェント＆サステイン造形器、日本語子音オンセット28ms明瞭化 |
| 20 | `BroadcastMasteringPresetVault.h` | 全12種プロマスタリング＆ライブSE統合ファクトリープリセット・スナップショット＆モーフィング補間 |
| 21 | `VocalEmotionAndProsodyAnalyzer.h` | Russell円環感情モデル（Valence × Arousal）リアルタイム歌唱感情分類、フレーズ抑揚アーチ美解析 |
| 22 | `AcousticImpedanceAndVocalTractTwin.h` | Sundberg シンガーズ・フォルマント（喉頭蓋管2.8〜3.5kHz密集共鳴）、Kelly-Lochbaum 6区間声道音響管 |
| 23 | `MasteringDitherAndBitDepthQuantizer.h` | TPDF 三角確率密度ディザ、Lipshitz 3次聴覚心理エラーフィードバック・ノイズシェーピング量子化器 |
| 24 | `HorologyCelestialVisualizerSuite.h` | 天文星座盤プラニスフィア音場マップ、4大SE専用オートマタ紋章、Rattrapante スプリットセコンドLUFS計 |
| 25 | `HorologyGrandComplicationHud.h` | 均時差キドニーカム機構、レトログラード16バンド倍音扇形針、アベンチュリン声区ムーンフェイズ天体窓 |
