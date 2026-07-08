; ============================================================
;  VocalGzzio VST3 — Windows Installer script (Inno Setup)
; ------------------------------------------------------------
;  使い方:
;   1. 先にプラグインをビルドして VocalGzzio.vst3 を生成しておく
;        cmake --build build --config Release
;   2. Inno Setup をインストール (https://jrsoftware.org/isdl.php)
;   3. この Installer.iss を VocalGzzio フォルダ直下
;      (CMakeLists.txt と同じ場所) に置き、
;      ダブルクリックで開いて「Compile」を押す
;   4. installer_output\VocalGzzio_Setup_v1.0.0.exe が完成
; ============================================================

#define MyAppName "VocalGzzio"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Gzzio"

[Setup]
; AppId はこのアプリ固有の識別子。変更しないでください（アンインストール紐付けに使用）
; ※アコグッジオとは別IDなので、両方入れても共存できます
AppId={{5D7E3B21-9A4C-4F80-B6D2-8C1E2F3A4B5C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=installer_output
OutputBaseFilename=VocalGzzio_Setup_v{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName} (VST3)

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english";  MessagesFile: "compiler:Default.isl"

[Files]
; ビルドで出来た .vst3 バンドル（フォルダ）を丸ごとコピー
Source: "build\VocalGzzio_artefacts\Release\VST3\VocalGzzio.vst3\*"; \
    DestDir: "{commoncf64}\VST3\VocalGzzio.vst3"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\VocalGzzio.vst3"

[Messages]
japanese.FinishedLabel=VocalGzzio のインストールが完了しました。%n%nReaper を再起動し、FX 追加で「VocalGzzio」を検索してください（必要なら設定でプラグインの再スキャンを実行）。
english.FinishedLabel=VocalGzzio has been installed.%n%nRestart Reaper and add "VocalGzzio" from the FX browser (re-scan plug-ins if needed).
