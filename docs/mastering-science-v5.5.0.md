# VocalGzzio v5.5.0 — 世界最先端ボーカル音響科学・グラミーマスタリング工学＆スイス高級時計工芸UI 技術白書

## 1. 研究開発の背景と設計思想

`VocalGzzio v5.5.0` では、単なる「エフェクトの詰め合わせ」や「AI生成風の均質なUI」から完全に脱却し、以下の3つの柱を軸にゼロから数理モデル・DSPアーキテクチャ・工芸UIを再構築しました。

1. **世界のトップチャートを手掛けるマスタリング＆ボーカルエンジニアの実践手法の数理モデル化**
   - Serban Ghenea（並列FET/Optoバス＋Oxford知覚インフレーター）、Manny Marroquin（4帯域位相整合クロスオーバー＋ドルビーA方式エアー）、Jaycen Joshua（動的咽頭レゾナンス鎮静化）、Andrew Scheps（1176リアバス並列圧縮）、Bob Katz（K-14ラウドネス＆クレストファクター管理）など、グラミー賞受賞エンジニアが実機コンソールで構築している複合シグナルチェインを、単一モジュール・ゼロ遅延で再現しました。
2. **最新音響音声学（Vocal Tract Acoustics）・心理音響学（Psychoacoustics）・数値解析DSPの統合**
   - Vadim Zavalishin の **TPT/ZDF（Topology-Preserving Transform / Zero-Delay Feedback）状態変数フィルタ**、Parker らの **2次 ADAA（Antiderivative Antialiasing）連続時間逆微分飽和器**、Johan Sundberg の **シンガーズ・フォルマント（Epilarynx 2.82kHz/3.28kHz 二重共鳴）**、Eberhard Zwicker の **16臨界帯域バーク尺度（Bark Scale）マスキング解析**、および **純正律（Just Intonation 5:4 / 6:5 / 3:2）動的マイクロチューニング** をすべて固定長スタック配列（RTヒープ確保ゼロ）で実装しました。
3. **人間による工芸設計（Swiss Haute Horlogerie × 宵祭ランタン光学ボケ × 物理キネティック操作）**
   - スイスの高級機械式時計工芸（Breguet ギョーシェ彫り、A. Lange & Söhne 面取りアングラージュ、トゥールビヨン脱進機、サファイアクリスタル無反射ドーム）と、日本の宵祭（深い夜空の藍 `#040B1E`、琥珀色の提灯 `#FFAA2B` / `#FFD875`、花火コーラル `#FF6E8E`、星明かりシアン `#3CE8D5`）の色彩体系を融合し、慣性モーメントと磁気デテント吸着を備えた「触っているだけでワクワクする楽器・計器」として設計しました。

---

## 2. コアDSPモジュール体系と数理仕様

### 2.1 `Source/MasteringProEngine.h` — Zavalishin TPT/ZDF・2次ADAA・Hilbert包絡・Serban並列バス

#### (1) Zavalishin TPT/ZDF 状態変数フィルタ (`TptStateVariableFilter`)
従来の双二次フィルタ（Direct Form I/II Biquad）は、ナイキスト周波数近傍でのゲイン・位相の圧縮（Frequency Cramping）や、カットオフ周波数を高速変調した際の内部状態発散・クリックノイズという構造的弱点を持っていました。
`VocalGzzio` ではアナログ状態変数フィルタの積分器を台形積分（Bilinear Transform）でゼロ遅延解法した TPT トポロジーを採用しています。

- **プリワーピング係数**:
  $$g = \tan\left(\frac{\pi f_c}{f_s}\right), \quad k = \frac{1}{Q}$$
- **ゼロ遅延フィードバック連立方程式の解析解**:
  $$a_1 = \frac{1}{1 + g(g + k)}, \quad a_2 = g \cdot a_1, \quad a_3 = g \cdot a_2$$
  $$v_3 = x[n] - s_2, \quad v_1 = a_1 s_1 + a_2 v_3, \quad v_2 = s_2 + a_2 s_1 + a_3 v_3$$
  $$s_1 \leftarrow 2v_1 - s_1, \quad s_2 \leftarrow 2v_2 - s_2$$
- **完全再構成恒等式（Multi-Output Identity）**:
  同一の内部状態から同時に取り出される Lowpass ($y_{\text{LP}} = v_2$)、Bandpass ($y_{\text{BP}} = v_1$)、Highpass ($y_{\text{HP}} = x[n] - k v_1 - v_2$) は、任意のサンプルにおいて厳密に以下の恒等式を満たします：
  $$y_{\text{LP}}[n] + k \cdot y_{\text{BP}}[n] + y_{\text{HP}}[n] \equiv x[n]$$
  この性質により、マルチバンド分割・再合成時に位相キャンセリング（コムフィルタリング）が一切発生しません。

#### (2) 2次 ADAA（Antiderivative Antialiasing）三極管＆トランスフォーマー飽和器 (`AdaaTriodeTransformerStage`)
非線形飽和関数 $f(x) = \tanh(x)$ を離散サンプル列に直接適用すると、高次倍音がナイキスト周波数で折り返し、金属的なエイリアシング雑音が発生します。
本エンジンでは 1次逆微分 $F_1(x) = \int f(u)\,du = \ln(\cosh(x))$ および 2次逆微分 $F_2(x) = \int F_1(u)\,du$ を用いた 2次 ADAA 差分商によって連続時間波形を近似評価します：

$$y[n] = \frac{2}{x_0 - x_2} \left( \frac{F_2(x_0) - F_2(x_1)}{x_0 - x_1} - \frac{F_2(x_1) - F_2(x_2)}{x_1 - x_2} \right)$$

さらに低域（`< 240 Hz`）の磁気コア磁束積分 $\Phi[n]$ によるトランスフォーマー偶数次倍音変調と、14 Hz TPT DCブロッカーを組み合わせ、温かく澄み切ったアナログ飽和を実現しています。

#### (3) 4次ポリフェーズIIR ヒルベルト変換・解析信号包絡線検波器 (`HilbertAnalyticDetector`)
通常のコンプレッサー（$|x[n]|$ 検波）では、85 Hz〜130 Hz の男性ボーカル基音に対して半周期ごとの整流リップルが発生し、高速アタック時に波形そのものを変調してしまう「粒子感歪み（Intermodulation Distortion）」が生じます。
本検波器は $0^\circ$ 経路と $90^\circ$ 経路の4次オールパスフィルタ対から直交解析信号 $z[n] = I[n] + j Q[n]$ を構成し、瞬時振幅を算出します：

$$A[n] = \sqrt{I[n]^2 + Q[n]^2}$$

単一正弦波 $\sin(\omega t)$ に対して $\sin^2(\omega t) + \cos^2(\omega t) \equiv 1$ となるため、低音ボーカルでもリップルが `< 0.35 dB` に抑えられ、歪みゼロの超高速コンプレッションが可能になります。

---

### 2.2 `Source/VocalScienceLab.h` — 音響音声学・バーク尺度マスキング解析・無事故ガーディアン

| クラス名 | 役割とアルゴリズム詳細 |
| :--- | :--- |
| `ZeroLagPhonemeClassifier` | ゼロ交差率（ZCR）と高域/低域エネルギー傾斜（Spectral Tilt）から、ゼロ遅延で各サンプルの **母音確率 (`voicedProb01`)**・**歯擦音確率 (`sibilantProb01`)**・**ブレス確率 (`breathProb01`)** を連続出力します。 |
| `SingersFormantAndChestBridge` | Johan Sundberg 教授の喉頭蓋管（Epilarynx Tube）共鳴理論に基づき、母音発声中のみ `2820 Hz` と `3280 Hz` の二重共鳴クラスタをリフトしてオーケストラや高密度ミックスを突き抜ける輝きを与え、歯擦音区間では瞬時にダッキングします。 |
| `BarkMaskingAnalyzer` | Eberhard Zwicker の **16臨界帯域（Critical Bands: 100 Hz 〜 13.5 kHz）** フィルタバンクと上方マスキング広がり関数（`+25 dB/Bark` / `-10 dB/Bark`）を計算し、カラオケ伴奏に対する歌声の聴こえやすさを **Vocal Audibility Index (VAI: 0〜100%)** としてリアルタイム数値化します。 |
| `AudioStabilityGuardian` | `NaN` / `±Inf` / 非正規化数（Denormal）のゼロ化＋`2.5 ms` スムーズ復帰ランプ、`11.5 Hz` TPT DCオフセット除去、USBオーディオインターフェースの1サンプル突発スパイク（ポップノイズ）を捕捉するスルーレート・アレスタを統合した最終安全弁です。 |
| `AdaptivePlosiveSuppressor` | マイク吹かれ（`/p/`, `/b/` 破裂音やスタンド振動）に特有の「`95 Hz` 以下が急増しているのに第2・第3倍音が存在しない」瞬間だけを検知し、`24 dB/oct` TPT ハイパスを `0.6 ms` で `32 Hz` から最大 `158 Hz` へ動的スイープします。 |

---

### 2.3 `Source/GrammyVocalChainRack.h` — 歴史的名機アウトボードの物理・位相モデリング

1. **`PultecPassiveInductorEq` (Pultec EQP-1A & MEQ-5 パッシブLCインダクタ曲線)**
   - `60 Hz / 100 Hz` のブーストとアッテネートを同時にかけることで生まれる伝説の **「Pultec Low-End Trick（サブ基音を太くしながら 145 Hz のブーミーな濁りを抉る）」** と、`12.5 kHz` LCインダクタのベル型シルキーエアーを再現。
2. **`H3000MicroPitchStereoDoubler` (Eventide H3000 Style デュアル・マイクロピッチシフト)**
   - 左チャンネルに `+8.5 cents` / `11.2 ms`、右チャンネルに `-8.5 cents` / `17.8 ms` の逆方向微細ピッチデチューンを施し、歌唱中は Mid 成分をダッキングしてセンター定位の芯を100%守ります。
3. **`DifferentialVelvetTransientShaper` (SPL Transient Designer 方式・レベル非依存エンベロープ微分器)**
   - 入力音量に依存せず、速い包絡と遅い包絡の正規化差分 $(E_{\text{fast}} - E_{\text{slow}}) / (E_{\text{slow}} + \epsilon)$ から子音のアタック感と語尾の余韻を独立制御します。
4. **`MarinairClassAPreampStage` (Neve 1073 Marinair LO1166 出力トランス＆A級プリアンプ)**
   - トランスコアの低域位相回転（オールパス分散）と偶数次倍音により、細いデジタル録音にアナログコンソール特有の「重心の低さと前に出る押し出し」を与えます。
5. **`AbbeyRoadSteedChamberConditioner` (Abbey Road Studios STEED テープ・プリディレイ＆チェンバー)**
   - `28 ms` のテープ・プリディレイに `600 Hz〜4.8 kHz` のバンドパスと空気吸収ロールオフを施し、ボーカルの明瞭度を一切損なわない奥行き空間を作ります。

---

### 2.4 `Source/SpatialMasteringConsole.h` — M/S空間造形＆4相ポリフェーズFIRトゥルーピーク天井制御

1. **`EllipticLowEndPhaseAligner` (カッティング旋盤式エリプティック・ローエンド・モノメーカー)**
   - Side 信号の `135 Hz` 以下を TPT ハイパスでモノラルへ収束させつつ、Mid 信号にも同一カットオフの1次オールパスを通して左右の位相回転を完全一致させます。さらにリアルタイム相互相関係数（Stereo Correlation `-1.0 .. +1.0`）を常時監視します。
2. **`DynamicMidSidePocketSculptor` (動的M/Sボーカルポケット＆語尾ステレオブルーム)**
   - 歌唱中は Mid の `1.2 kHz〜3.4 kHz` をわずかにリフトしてセンターの存在感を際立たせ、フレーズの切れ目で Side の `6.5 kHz` エアーをふわっと広げて余韻を包み込みます。
3. **`PolyphaseFirTruePeakCeiling` (ITU-R BS.1770-4 準拠 6タップ×4相ポリフェーズFIRインターサンプル・トゥルーピークリミッター)**
   - ブラックマン・ハリス窓付き Sinc 補間フィルタ（位相 $t = 0.00, 0.25, 0.50, 0.75$）により、D/A変換後のアナログ波形ピーク（Inter-Sample Peak）を正確に捕捉し、設定天井（`-1.0 dBTP` または `-0.3 dBTP`）を絶対に超えないよう2段リリース（`1.2 ms` マイクロ復帰 ＋ `55 ms` プログラムリリース）で制御します。

---

### 2.5 `Source/VocalArticulationPolish.h` & `Source/BroadcastStreamGuard.h` — 日本語母音整合＆配信ガード

- **`JapaneseVowelMoraBalancer`（日本語5母音 `/a, i, u, e, o/` モーラF1/F2自動均一化）**:
  日本語の歌唱で「イ段・エ段（前舌母音）だけ `2.3 kHz` が耳に刺さり、ウ段・オ段（後舌母音）がこもって歌詞が聴き取りにくくなる」現象を、F1/F2 エネルギー比からリアルタイム判定して自動補正します。
- **`AdaptiveFeedbackHowlingSuppressor`（6帯域適応ノッチ・ゼロ遅延ハウリングサプレッサー）**:
  スピーカーモニター環境で発生する純音発振（ハウリング）を、クレストファクターと持続時間から検出し、`Q = 14.0` の超狭帯域 TPT ノッチフィルタで声質を変えずにピンポイント除去します。
- **`MechanicalClickAndImpactBlanker`（キーボード打鍵・マウスクリック瞬時ブランカー）**:
  歌枠や雑談配信中のメカニカルキーボード打鍵音・マウスクリック音を、1次差分スルーレートから検知してサブミリ秒で滑らかに抑制します。
- **`BinauralBoneConductionCueEnhancer`（骨伝導逆補正＋280μs バウアー・バイノーラルクロスフィード）**:
  頭蓋骨の骨伝導で過剰に聴こえる `250 Hz` 付近をすっきり整理し、密閉型ヘッドホンでの長時間歌唱時の耳疲れとピッチの上ずりを防ぎます。

---

### 2.6 `Source/PitchAndHarmonyScience.h` — 純正律マイクロチューニング＆輪状甲状筋キネマティクス

1. **`JustIntonationChordTuner` (純正律 5:4 / 6:5 / 3:2 ダイナミック・マイクロチューニング)**
   - 12平均律（12-TET）の長3度（純粋な `5:4` より `+13.686 cents` 高い）や短3度（純粋な `6:5` より `-15.641 cents` 狭い）が生む「うなり（Acoustic Beating）」を解消するため、和音が `75 ms` 以上ホールドされた区間で純粋整数比へ滑らかにロックインします。
2. **`KinematicVibratoAndPortamentoModel` (輪状甲状筋ビブラート＆最小躍度ポルタメント)**
   - ノート発声から `180〜220 ms` のストレートトーン保持後に開花する自然ビブラートと、人間の運動制御における最小躍度軌道（Minimum-Jerk Trajectory: $s(t) = 10t^3 - 15t^4 + 6t^5$）による滑らかなポルタメントを生成します。
3. **`SubSampleViterbiPitchRefiner` (放物線サブサンプルラグ補間＆3状態ビタビ・オクターブジャンプ防止)**
   - ボーカルフライやファルセット移行時に起きやすい「1オクターブ誤検出」を、状態遷移コスト関数によって防ぎます。

---

### 2.7 `Source/StudioAcousticRoomEngine.h` & `Source/HarmonicExciterAndTapeDeck.h` & `Source/IntelligentMixGlueEngine.h`

- **`DirectToReverberantDeReverbEngine`（6帯域 DRR 直接音/残響比ゼロ遅延デリバーブ）**:
  直接音包絡（`2.2 ms`）と拡散残響テイル（`85 ms`）の比（DRR）から、自宅部屋のフラッターエコーだけをコンプ前段で引き算します。
- **`VintageMicrophoneCapsuleSimulator`（U47 Tube / C800G / 7B Dynamic カプセル物理モデル）**:
  4種類の象徴的なマイクカプセル共振と真空管ヘッドアンプ特性を再現します。
- **`Hadamard8x8VelvetFdnReverb`（8x8 直交ウォルシュ・アダマール変換 FDN リバーブ）**:
  互いに素な8本のディレイラインを $1/\sqrt{8}$ 正規化高速ウォルシュ・アダマール変換（FWHT）で直交結合し、歌唱中は自動ダッキング、語尾で豊かに開花する最高品位リバーブを提供します。
- **`MagneticHysteresisTapeCell` & `Studer30IpsHeadBumpFilter`**:
  Jiles-Atherton 磁区ピン止めヒステリシス近似と、`30 IPS`（`68 Hz +1.65 dB`）/ `15 IPS`（`48 Hz +2.20 dB`）の再生ヘッドバンプ特性を再現します。
- **`WeissPrecisionSplitDeEsser`（Weiss DS1-MK3 式・相対レベル検出デュアルバンド・ディエッサー）**:
  母音基準エネルギーに対する歯擦音帯域（`5.35 kHz` & `8.45 kHz`）の相対比で動作するため、ささやき声から全力のサビまで入力音量に依存せず一定の音楽的ディエッシングを行います。

---

### 2.8 `Source/DynamicFormantAndRegisterEngine.h` & `Source/PsychoacousticClarityEngine.h` & `Source/MasteringLoudnessAndDelivery.h`

- **`PassaggioRegisterBridge`（パッサージョ声区転換点ブリッジ）**:
  地声からミックスボイス・ハイトーンへ移る `260 Hz〜520 Hz` 帯域での声の細りを検知し、第1倍音ボディと咽頭共鳴を自動架橋します。
- **`MissingFundamentalVirtualPitchEngine`（チェビシェフ多項式 $T_2, T_3$ 欠落基音バーチャルピッチ）**:
  $T_2(x) = 2x^2 - 1$ および $T_3(x) = 4x^3 - 3x$ により基音に位相同期した第2・第3倍音を生成し、小型スマホスピーカーでも豊かなチェスト低域を知覚させます。
- **`StreamingLoudnessTargetOptimizer` & `LossyCodecDistortionGuard` & `PsychoacousticNoiseShapedDither`**:
  YouTube / Spotify (`-14 LUFS`)、Apple Music (`-16 LUFS`)、Shorts / TikTok (`-12 LUFS`) のラウドネス規定管理、AAC/Opus ロッシー変換時のギブス現象オーバーシュート予防、および Wannamaker F重み付け 2次ノイズシェーピング 24-bit TPDF ディザを提供します。

---

## 3. 自動検証テストスイート（CTest 59項目）

すべての新規DSP・音響科学・時計工芸UIレンダリング基盤は、以下の専用テストバイナリによって自動検証されています。

1. **`tools/dsp_mastering_pro_suite.cpp`**:
   - Zavalishin TPT 全9モードの周波数応答・高速カットオフ変調安定性・`LP + k*BP + HP == Input` 完全再構成検査
   - 2次 ADAA 飽和器のエイリアシング抑制＆DCオフセット除去検査
   - ヒルベルト変換包絡検波器の 85 Hz 低音リップル免疫検査（`< 0.35 dB`）
   - Sundberg シンガーズフォルマント＆ゼロ遅延音素識別器（母音 vs 歯擦音）検査
   - Zwicker 16帯域バーク尺度マスキング解析＆ VAI 指標検査
   - `AudioStabilityGuardian` への `NaN` / `+Inf` / `-Inf` / `Denormal` / `+50.0f` 突発スパイク注入耐久検査
2. **`tools/ui_horology_and_science_audit.cpp`**:
   - 純正律マイクロチューニング（`5:4` 長3度 `-13.69 cents`、`3:2` 完全5度 `+1.96 cents`）のうなり低減検査
   - 輪状甲状筋ビブラート開花遅延＆最小躍度ポルタメント軌道検査
   - 8x8 直交アダマール行列のエネルギー保存則（$\sum x_i^2 \equiv \sum y_i^2$）検査
   - 24大シグネチャー・コンソールカタログの双線形モーフィング補間＆ジャンル自動推薦検査
   - スイス高級時計ギョーシェ彫り・トゥールビヨン脱進機・宵祭ランタン粒子物理のオフスクリーン ARGB レンダリング検査
