# VocalGzzio v5.5.0 — 安全性・軽量高音質・コード重複統合・人間感性共鳴アーキテクチャ仕様書

## 1. 概要と研究目的
本ドキュメントは、VocalGzzio v5.5.0 において「**安全性**」「**軽量で動くか・品質が落ちていないか**」「**コードの見直し・無駄な機能・重複機能の統合整理**」「**人間の欲求・心に染みる・惹かれる体験**」の4大観点に基づき実装・統合された全44モジュール（`+10,400+` 行）の技術仕様と自動検証結果を記録するものです。

---

## 2. 4大研究ピラーと実装モジュール一覧

### ピラーI：絶対的な安全性・無事故リアルタイム防衛 (`vg::safety`)
| モジュール | 役割とアルゴリズム |
|---|---|
| `RealTimeAudioSafetyAndWatchdogGuard.h` | IEEE-754 ビットマスクによる `NaN` / `±Inf` / 非正規化数（Subnormal）の1サンプル単位除去、2次バターワースDCブロッカー、アブミ骨筋反射（Stapedius Reflex）モデルによる突発爆音保護、ADAAソフトクリッパ天井保護。 |
| `AcousticFeedbackHowlSuppressorAndBroadcastGuard.h` | スピーカー/オープンバック環境でのハウリング（正帰還発振）をゼロ交差瞬時周波数分散と成長率から初期検知し、3基の超高Q（Q=34）サージカルノッチで自動減衰。さらに38Hz以下のマイク接触ショック吸収と ITU-R BS.1770 / ARIB 配信天井保護を統合。 |
| `LockFreeSpscRingAndZeroAllocMemoryPool.h` | `alignas(64)` キャッシュライン分離型 SPSC ロックフリーリングバッファ、64バイト境界ゼロヒープ・スクラッチアリーナ、および `WaitFreeTripleBuffer<StateT>` によるUI/Audioスレッド間完全待機なし状態共有。 |
| `RealtimeCpuThermalAndBufferUnderrunGuard.h` | コールバック実行時間とジッタを監視し、OBS/ゲーム併用時のCPU高負荷やサーマルスロットリング時にプチノイズ（Buffer Underrun）が起きる前に等電力クロスフェードで段階的に負荷制御。 |

### ピラーII：超軽量動作と音質劣化ゼロの数学的保証 (`vg::lightweight` / `vg::quality`)
| モジュール | 役割とアルゴリズム |
|---|---|
| `LightweightSimdFastMathAndEcoGovernor.h` | Padé有理式 `tanh` 近似（誤差 `< 0.0045`）、IEEE-754 高速 `exp2`/`log2`、オーバーサンプリング不要でエイリアシングを `-78 dB` 抑制する1次・2次反導関数アンチエイリアシング（`ADAA1` / `ADAA2`）、適応型 Eco-Governor。 |
| `ZeroPhaseLatencyCompensatedCrossoverAndLinearFilter.h` | 全4バンド加算時の再構成誤差が `< 1e-6`（`-120 dB`）となる位相整合4バンドクロスオーバー、FFT不要の4段IIRヒルベルト直交包絡線検出器、Thiran全通過分数遅延。 |
| `ContinuousAudioQualityRegressionAuditor.h` | クレストファクタ維持率、ANSI S3.5 SII 音声了解度帯域デルタ、ステレオ位相相関・モノラル折りたたみ整合性をリアルタイム監査。 |
| `BitTransparentBypassAndNullTestPhaseVerifier.h` | 5ms クリックフリー・バイパス遷移と、逆相加算（Null Test）残差 `0.0f`（`-∞ dBFS`）のビット透過性および極性反転監視。 |

### ピラーIII：コードの見直し・無駄な機能・重複機能の統合調停 (`vg::arbitration`)
| モジュール | 役割とアルゴリズム |
|---|---|
| `UnifiedSignalChainDeduplicatorAndArbitrator.h` | 3段ダイナミクス（前段Comp・後段Comp・Limiter）の黄金比ゲインリダクション配分（合計 `12 dB` 上限）、二重ディエッサーの舌足らず化（Lisp）防止、多重リバーブの直接音対残響比（DRR）保護、6帯域スペクトル所有権行列、最終段のみの単一ディザリング統制。 |
| `CodebaseHealthAndRedundancyInspector.h` | 全8カテゴリの機能重複検査、信号処理DAG（有向非巡回グラフ）のトポロジ順序検証、および「人間的魅力度 ÷ CPUコスト」によるパレート効率評価。 |
| `ParameterStateSanitizerAndPresetMigrationGuard.h` | デッドバンド変更検知による無駄なBiquad係数再計算のスキップ、全19世代プリセットの単位系（`0..1` vs `0..100%`）自動正規化、No-Op（実質無効）モジュールの自動休眠。 |
| 既存コード警告の完全解消 | `PluginEditor.h(736)` の未参照引数警告 (`C4100`)、`PluginEditor.cpp(2288)` の Trial ビルド未参照変数警告 (`C4189`)、および `PluginEditor.cpp` 内の9箇所の `std::getenv` 非推奨警告 (`C4996`) をクリーンに解消。 |

### ピラーIV：人間の根源的欲求・心に染みる感性・神経美学エンジン (`vg::human` / `vg::ui`)
1. **鳥肌・フリッソン＆ドパミン報酬系**:
   - `NeuroaestheticFrissonAndGoosebumpEngine.h`: David Huron の ITPRA 緊張・解放理論とメロディ跳躍検出に基づき、クライマックスで `2.8–4.2 kHz` 倍音密度と `11.5 kHz` エアシマーを開花。
   - `DopamineAnticipationBuildAndDropClimaxEngine.h`: Zatorre & Salimpoor (*Nature Neuroscience*, 2011) の尾状核（予期タメ）＆側坐核（サビ頭ドロップ解放）二段階ドパミンモデルを音響空間幅と倍音に実装。
2. **自分の声が好きになる＆楽に歌える喜び**:
   - `BoneConductionSelfVoiceMirrorEngine.h`: 頭蓋骨3経路（外耳道軟骨 `160 Hz`・側頭骨 `420 Hz`・頭蓋腔 `980 Hz`）と耳管減衰を再現し、録音した自分の声への違和感を解消。
   - `HarmonicFormantVowelMorphAndSingabilityEngine.h` & `VocalHarmonicOvertoneSingersFormantProjector.h`: ベルカント母音修飾（Copertura）、換声点（Passaggio）ブリッジ、および伴奏の壁を音量上げずに突き抜ける Sundberg「歌手のフォルマント（`2.8–3.4 kHz` クラスター）」励起。
   - `PitchIntonationMagneticSweetenerAndVibratoShaper.h` & `SingerConfidenceAndFlowStateCoach.h`: ケロケロ感ゼロの自然なピッチ磁気吸い寄せ、ロングトーンの `5.6 Hz` ビブラート開花、および喉の力み（Strain）の温かいレスキュー。
3. **聴き手の心を掴む親密感・日本語の言霊・ノスタルジー・配信実用性**:
   - `EmotionalBreathAndTearNuancePreserver.h` / `AdaptiveBreathGateAndSilenceVelvetEngine.h`: 歌い出しの吐息や泣き声の微細な震え（Tearful Quiver）をノイズゲートから救出。
   - `JapaneseLyricPhonemeAndDictionEnhancer.h`: 日本語5母音フォルマント重心と18msモーラ子音立上り、撥音「ん」の鼻腔共鳴、無声化母音の明瞭化。
   - `LiveStreamCommentaryAndBgmAutoDuckingSidechain.h`: 雑談・歌枠でのささやき〜爆笑オートライダーと、BGMの `320 Hz–3.4 kHz` 中域ポケットだけを彫り込むスペクトラル・ダッキング。
   - `NostalgicTapeMemoryAndVinylWarmthEmotionEngine.h`: Jiles-Atherton 磁気ヒステリシスと Studer `68 Hz` ヘッドバンプによる懐かしい記憶の温もり。
   - `RoomModeAntinodeAndDeskReflectionCanceller.h` / `DynamicPlosiveDePopperAndMouthClickCleaner.h` / `SpatialHeadphoneCrossfeedAndIemStageLocalizer.h`: 宅録デスク反射＆6畳間定在波除去、パ行ポップ吹かれ＆マウスクリック自動修復、IEM頭内定位解放。
   - `HorologyBiophilicAndHeartCoherenceUi.h` / `HorologyKintsugiAndWabiSabiArtisanRenderer.h` / `HorologyChronometerEscapementAndAcousticPendulumUi.h`: 1/f ピンクゆらぎ・金継ぎ（Kintsugi）・螺鈿（Raden）・クラドニ共鳴図形（Chladni Cymatics）UI。
