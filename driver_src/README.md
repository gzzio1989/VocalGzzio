# VocalGzzioAudio — 100%自社実装 Windows 仮想オーディオドライバ (`driver_src/`)

このディレクトリには、他社製コード・バイナリを一切使用しない **100%独自実装の Windows 10 / 11 (x64 & ARM64) カーネルモード仮想オーディオドライバ（`VocalGzzioAudio.sys`）** のソースコード一式と、Microsoft Hardware Dashboard（Attestation Signing：構成証明署名）提出用の自動パッケージングスクリプトが含まれています。

事業届出（開業届）から EV コード署名証明書・Microsoft Partner Center 登録までの事務手続きチェックリストは、ローカル管理の `販売/独自仮想ドライバ_EV証明書_Microsoft登録手順書.md` を参照してください。

---

## 設計の特徴

1. **PortCls + WaveRT + イベント駆動通知（`IMiniportWaveRTStreamNotification`）**
   - Windows 標準オーディオクラスドライバ（`portcls.sys`）の **WaveRT** アーキテクチャで実装されており、OBS Studio・Discord・VRChat・cluster・DAW などの WASAPI（共有／イベント駆動）と超低遅延で連携します。
   - **32-bit IEEE Float**・**24-bit PCM**・**32-bit PCM**・**16-bit PCM**（44.1 / 48 / 88.2 / 96 kHz、2ch ステレオ）にネイティブ対応しています。
2. **FPU不使用の整数 Q31 内部バス ＆ 自動サンプルレート変換（[`include/VocalGzzioLoopbackCore.h`](include/VocalGzzioLoopbackCore.h)）**
   - カーネルの `DISPATCH_LEVEL`（DPC）でも浮動小数点演算を一切使わない純粋整数ビット演算で IEEE-754 Float32 ↔ Q31 変換と 32.32 固定小数点線形リサンプルを行います。
   - 送信側（VocalGzzio: 例 48kHz Float32）と受信側（通話アプリ等: 例 44.1kHz PCM16）の形式が食い違っていても音割れやピッチ狂いが起きません。
   - 送信停止時には残留バッファの「ブーー」というループ発振音や停止クリック音を出さず、数ミリ秒で滑らかに無音へ減衰します。
3. **Windows 11 HVCI（コア分離・メモリ整合性）＆ Universal INF 完全準拠**
   - `POOL_NX_OPTIN=1`（`NonPagedPoolNx`）、`/kernel /guard:cf /INTEGRITYCHECK /NXCOMPAT` でビルドされ、Windows 11 のメモリ整合性（HVCI）が有効なゲーミングPCでもブロックされません。
   - [`inf/VocalGzzioAudio.inf`](inf/VocalGzzioAudio.inf) は Microsoft 公式の `infverif.exe /v /u`（Universal INF 検査）および `Inf2Cat.exe` をエラー・警告ゼロで通過確認済みです。
   - `PKEY_AudioDevice_NeverSetAsDefaultEndpoint` を INF に明記しているため、導入時にユーザーの普段使いスピーカーやマイク（既定のデバイス）を勝手に奪いません。

---

## ディレクトリ構成

| ファイル | 役割 |
| :--- | :--- |
| [`include/VocalGzzioLoopbackCore.h`](include/VocalGzzioLoopbackCore.h) | カーネル／ユーザーモード共通の整数 Q31 ループバック・リサンプラ（CTest で単体検証） |
| [`sys/VocalGzzioAudio.h`](sys/VocalGzzioAudio.h) | PortCls WaveRT / Topology 共通定義・HVCI NX プール宣言 |
| [`sys/Adapter.cpp`](sys/Adapter.cpp) | `DriverEntry` / `AddDevice` / `StartDevice` / スピンロック保護共有バス |
| [`sys/MiniportTopology.cpp`](sys/MiniportTopology.cpp) | `ライン (VocalGzzio Driver (WDM))` 再生・録音エンドポイント＆常時ジャック接続応答 |
| [`sys/MiniportWaveRT.cpp`](sys/MiniportWaveRT.cpp) | QPC 剰余累積クロック駆動 WaveRT ストリーム＆イベント通知 |
| [`sys/VocalGzzioAudio.rc`](sys/VocalGzzioAudio.rc) | `Gzzio` 製造元バージョンリソース |
| [`inf/VocalGzzioAudio.inf`](inf/VocalGzzioAudio.inf) | Windows 10 (1809+) / Windows 11 (x64 & ARM64) 対応 Universal INF |
| [`build_driver.ps1`](build_driver.ps1) | 公式 WDK NuGet 自動取得・コンパイル・`InfVerif /u`・`Inf2Cat` 一括実行 |
| [`pack_attestation_cab.ps1`](pack_attestation_cab.ps1) | Microsoft Hardware Dashboard 提出用 `.cab` 生成（`-Sign` で EV 署名実行） |
| [`apply_signed_driver.ps1`](apply_signed_driver.ps1) | Microsoft 署名済み ZIP のカーネル署名検証（`/kp`）と `driver/` への安全な反映 |

---

## ビルドと署名の手順（EV 証明書取得後）

### 1. コンパイル＆検査（WDK の手動インストール不要）
```powershell
powershell -ExecutionPolicy Bypass -File .\driver_src\build_driver.ps1
```
Visual Studio（C++）と Windows SDK があれば、公式 `Microsoft.Windows.WDK.x64` NuGet パッケージを `.wdk_cache/`（Git除外）へ自動取得して `out\x64\VocalGzzioAudio\VocalGzzioAudio.sys` を生成します。

### 2. Microsoft 提出用 `.cab` の作成と EV 署名
```powershell
# 未署名 CAB の生成テストのみ行う場合:
powershell -ExecutionPolicy Bypass -File .\driver_src\pack_attestation_cab.ps1

# EV 証明書トークンを挿して本番提出用の署名付き CAB を作る場合:
powershell -ExecutionPolicy Bypass -File .\driver_src\pack_attestation_cab.ps1 -Sign
```
生成された `driver_src\out\VocalGzzioAudio_Attestation_x64.cab` を Microsoft Partner Center（Hardware Dashboard）へ提出します。

### 3. Microsoft 公式署名済みドライバの組み込み
Microsoft Hardware Dashboard から返ってきた署名済み ZIP を展開し、以下を実行します：
```powershell
powershell -ExecutionPolicy Bypass -File .\driver_src\apply_signed_driver.ps1 -SignedDir "C:\展開先フォルダ" -RemoveLegacySyncroomFallback
```
`signtool verify /kp` で Microsoft のカーネルモード署名が確認された場合のみ `driver\VocalGzzioAudio.{inf,cat,sys}` へ配置されます。あとは `配布物をつくる.bat` を実行するだけで、[`VocalGzzioWdmInstaller.exe`](../tools/VocalGzzioWdmInstaller.cpp)・[`Installer.iss`](../Installer.iss)・[`pack_booth.py`](../tools/pack_booth.py) のすべてが自動的に新ドライバへ切り替わります。
