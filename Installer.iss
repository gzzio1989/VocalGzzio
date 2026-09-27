; ============================================================
;  VocalGzzio — Windows インストーラー (Inno Setup 6)
; ------------------------------------------------------------
;  作り方（2026-08-13 更新）:
;   1. build.bat      … ビルドする
;   2. installer.bat  … インストーラーを作る（開発版。名前に -dev が付く）
;      installer.bat release … リリース用（-dev が付かない）
;
;   installer.bat が version_generated.iss を書き出し、このファイルがそれを読みます。
;   **バージョンの正本は CMakeLists.txt** です。ここには書きません。
;   Inno Setup を入れていない場合は https://jrsoftware.org/isdl.php から。
;
;  v1.9.5 での変更:
;   - すでに入っているときは【更新 / 修正 / 追加・変更 / 削除】を選べます
;   - VST3 と単体起動版をコンポーネントとして選べます
;   - このファイルは UTF-8(BOM付き) で保存すること。BOM が無いと
;     Inno Setup が日本語を化けさせます。
; ============================================================

#define MyAppName "VocalGzzio"

; ------------------------------------------------------------
;  バージョンは **CMakeLists.txt が正本**。ここには書きません。
;  installer.bat が CMakeLists.txt から読んで、
;  version_generated.iss に書き出して渡してきます（下の #ifexist）。
;
;  ★VerSuffix があると、出来上がりは
;      VocalGzzio_Setup_v3.0.0-dev.exe
;    になります。開発中のビルドが、**リリース済みのインストーラーを
;    黙って上書きしてしまう事故**を防ぐためです。
;    （2026-08-13 に実際に起きました。v3.0開発中のビルドが
;      installer_output\VocalGzzio_Setup_v2.12.0.exe を潰しました）
;
;  Inno Setup の GUI から直接 Compile したときは、下の既定値が使われます。
;  そのときも -dev が付くので、リリース版と混ざりません。
; ------------------------------------------------------------
#ifndef ExternalBuild
  #ifexist "version_generated.iss"
    #include "version_generated.iss"
  #endif
#endif
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef VerSuffix
  #define VerSuffix "-dev"
#endif

#define MyAppPublisher "Gzzio"
#define MyAppURL "https://github.com/gzzio1989/VocalGzzio"

; ------------------------------------------------------------
;  体験版（installer.bat trial → version_generated.iss が TrialBuild を定義）。
;  体験版は**別の製品**として入る:
;   ・名前 = VocalGzzio Trial（.vst3 も exe も別名 → 製品版と共存できる）
;   ・AppId も別 → 追加/削除に別々に並び、消すときも混ざらない
;   ・中身のビルドは build_trial\（60秒ごとのディップ・保存不可）
; ------------------------------------------------------------
#ifdef TrialBuild
  #define MyAppName2      "VocalGzzio Trial"
  #ifndef MyBuildDir
    #define MyBuildDir      "build_trial"
  #endif
  #define MyVst3Name      "VocalGzzio Trial.vst3"
  #define MyExeName       "VocalGzzio Trial.exe"
  #define MyOutPrefix     "VocalGzzio_Trial_Setup_v"
  #define MyAppIdStr      "{7A2C4E83-1B5D-4A96-9C3E-2D4F6A8B0C1E}"
  #define MyStandaloneDir "{commonpf64}\VocalGzzio Trial"
#else
  #define MyAppName2      "VocalGzzio"
  #ifndef MyBuildDir
    #define MyBuildDir      "build"
  #endif
  #define MyVst3Name      "VocalGzzio.vst3"
  #define MyExeName       "VocalGzzio.exe"
  #define MyOutPrefix     "VocalGzzio_Setup_v"
  #define MyAppIdStr      "{5D7E3B21-9A4C-4F80-B6D2-8C1E2F3A4B5C}"
  #define MyStandaloneDir "{commonpf64}\VocalGzzio"
#endif

[Setup]
; AppId はこのアプリ固有の識別子。変更しないでください（アンインストール紐付けに使用）
AppId={{#MyAppIdStr}
AppName={#MyAppName2}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}/releases
VersionInfoVersion={#MyAppVersion}
DefaultDirName={commoncf64}\VST3
DisableDirPage=no
DirExistsWarning=no
DisableProgramGroupPage=yes
; 「すでに入っています」の選択画面をようこそ画面の直後に挟むため、
; ようこそ画面を有効にしておく
DisableWelcomePage=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=installer_output
OutputBaseFilename={#MyOutPrefix}{#MyAppVersion}{#VerSuffix}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName2} {#MyAppVersion}{#VerSuffix}

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english";  MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "{cm:TypeFull}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "vst3";       Description: "{cm:CompVst3}";       Types: full custom
Name: "standalone"; Description: "{cm:CompStandalone}"; Types: full

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Components: standalone; Flags: unchecked

[Files]
; 使用許諾と、使用している第三者ライブラリ・フォントのライセンス文書。
Source: "LICENSE";                                DestDir: "{app}\{#MyVst3Name}"; DestName: "LICENSE.txt"; Components: vst3; Flags: ignoreversion
Source: "Resources\kawaii_font_LICENSE_OFL.txt"; DestDir: "{app}\{#MyVst3Name}"; Components: vst3; Flags: ignoreversion
Source: "LICENSE";                                DestDir: "{#MyStandaloneDir}"; DestName: "LICENSE.txt"; Components: standalone; Flags: ignoreversion
Source: "Resources\kawaii_font_LICENSE_OFL.txt"; DestDir: "{#MyStandaloneDir}"; Components: standalone; Flags: ignoreversion
Source: "配布物\ライセンス\*"; DestDir: "{app}\{#MyVst3Name}\ライセンス"; Components: vst3; Flags: ignoreversion
Source: "配布物\ライセンス\*"; DestDir: "{#MyStandaloneDir}\ライセンス"; Components: standalone; Flags: ignoreversion
; 取扱説明書（オフラインで開けるHTML 1枚。買った人が最初に読む物）
Source: "docs\manual.html"; DestDir: "{#MyStandaloneDir}"; DestName: "取扱説明書.html"; Components: standalone; Flags: ignoreversion
Source: "docs\manual.html"; DestDir: "{app}\{#MyVst3Name}"; DestName: "取扱説明書.html"; Components: vst3; Flags: ignoreversion
Source: "docs\style.css"; DestDir: "{#MyStandaloneDir}"; Components: standalone; Flags: ignoreversion
Source: "docs\style.css"; DestDir: "{app}\{#MyVst3Name}"; Components: vst3; Flags: ignoreversion
; VST3 バンドル（フォルダごと）
Source: "{#MyBuildDir}\VocalGzzio_artefacts\Release\VST3\{#MyVst3Name}\*"; \
    DestDir: "{app}\{#MyVst3Name}"; Components: vst3; \
    Flags: recursesubdirs createallsubdirs ignoreversion
; 単体起動版
Source: "{#MyBuildDir}\VocalGzzio_artefacts\Release\Standalone\{#MyExeName}"; \
    DestDir: "{#MyStandaloneDir}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{commonprograms}\{#MyAppName2}"; Filename: "{#MyStandaloneDir}\{#MyExeName}"; Components: standalone
Name: "{commonprograms}\{#MyAppName2} 取扱説明書"; Filename: "{#MyStandaloneDir}\取扱説明書.html"; Components: standalone
Name: "{commondesktop}\{#MyAppName2}";  Filename: "{#MyStandaloneDir}\{#MyExeName}"; Components: standalone; Tasks: desktopicon

[InstallDelete]
; v2.8.1: 上書きインストールの前に古いVST3バンドルを丸ごと消す。
; 中身のファイル構成が変わった場合に新旧が混ざるのを防ぐ
; (「新しい版を入れたのにDAWの表示が古いまま」の一因になり得る)。
Type: filesandordirs; Name: "{app}\{#MyVst3Name}"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\{#MyVst3Name}"
Type: filesandordirs; Name: "{#MyStandaloneDir}"

[CustomMessages]
japanese.TypeFull=標準（VST3 と単体起動版の両方）
english.TypeFull=Full installation (plug-in and standalone app)
japanese.TypeCustom=カスタム（入れるものを選ぶ）
english.TypeCustom=Custom (choose what to install)
japanese.CompVst3=VST3 プラグイン ― DAW で使う
english.CompVst3=VST3 plug-in - for your DAW
japanese.CompStandalone=単体起動アプリ ― DAW なしで使う
english.CompStandalone=Standalone app - run it without a DAW
japanese.MaintCaption=VocalGzzio はすでに入っています
english.MaintCaption=VocalGzzio is already installed
japanese.MaintDesc=この後の操作を選んでください。
english.MaintDesc=Choose what you want to do.
japanese.MaintSub=インストール済み: バージョン %1
english.MaintSub=Installed version: %1
japanese.MaintUpdate=更新 ― 新しいバージョン {#MyAppVersion} に入れ替える
english.MaintUpdate=Update - replace it with version {#MyAppVersion}
japanese.MaintRepair=修正 ― 消えた・壊れたファイルを入れ直す
english.MaintRepair=Repair - reinstall the files as they should be
japanese.MaintModify=追加・変更 ― 入れるもの（VST3 / 単体起動版）を選び直す
english.MaintModify=Add or change - pick which parts are installed
japanese.MaintRemove=削除 ― パソコンから取り除く
english.MaintRemove=Remove - uninstall it from this PC
japanese.RemoveDone=VocalGzzio を削除しました。
english.RemoveDone=VocalGzzio has been removed.
japanese.RemoveFailed=アンインストーラーを起動できませんでした。「アプリと機能」から削除してください。
english.RemoveFailed=Could not start the uninstaller. Please remove it from Apps & features instead.
japanese.NeedOne=VST3 プラグインか単体起動アプリの、どちらかは選んでください。
english.NeedOne=Select at least one part to install: the VST3 plug-in or the standalone app.

[Messages]
japanese.FinishedLabel=VocalGzzio のインストールが完了しました。%n%nDAW を再起動し、FX 追加で「VocalGzzio」を検索してください（出てこない場合はプラグインの再スキャンを実行）。
english.FinishedLabel=VocalGzzio has been installed.%n%nRestart your DAW and add "VocalGzzio" from the plug-in browser (run a plug-in re-scan if it does not appear).

[Code]
var
  MaintPage: TInputOptionWizardPage;
  CancelWithoutPrompt: Boolean;

const
  MAINT_UPDATE = 0;
  MAINT_REPAIR = 1;
  MAINT_MODIFY = 2;
  MAINT_REMOVE = 3;

function UninstallRegKey(): String;
begin
  Result := ExpandConstant('Software\Microsoft\Windows\CurrentVersion\Uninstall\{#SetupSetting("AppId")}_is1');
end;

function GetUninstallString(): String;
var
  S: String;
begin
  S := '';
  if not RegQueryStringValue(HKLM, UninstallRegKey(), 'UninstallString', S) then
    RegQueryStringValue(HKCU, UninstallRegKey(), 'UninstallString', S);
  Result := S;
end;

function InstalledVersion(): String;
var
  S: String;
begin
  S := '';
  if not RegQueryStringValue(HKLM, UninstallRegKey(), 'DisplayVersion', S) then
    RegQueryStringValue(HKCU, UninstallRegKey(), 'DisplayVersion', S);
  if S = '' then
    S := '?';
  Result := S;
end;

function IsInstalled(): Boolean;
begin
  Result := GetUninstallString() <> '';
end;

// 選んだ保守メニュー（未インストールなら「更新」扱い＝通常インストール）
function MaintChoice(): Integer;
begin
  if IsInstalled() and (MaintPage <> nil) then
    Result := MaintPage.SelectedValueIndex
  else
    Result := MAINT_UPDATE;
end;

procedure InitializeWizard();
var
  Sub: String;
begin
  CancelWithoutPrompt := False;
  if not IsInstalled() then
    Exit;

  Sub := FmtMessage(ExpandConstant('{cm:MaintSub}'), [InstalledVersion()]);
  MaintPage := CreateInputOptionPage(wpWelcome,
    ExpandConstant('{cm:MaintCaption}'),
    ExpandConstant('{cm:MaintDesc}'),
    Sub, True, False);
  MaintPage.Add(ExpandConstant('{cm:MaintUpdate}'));
  MaintPage.Add(ExpandConstant('{cm:MaintRepair}'));
  MaintPage.Add(ExpandConstant('{cm:MaintModify}'));
  MaintPage.Add(ExpandConstant('{cm:MaintRemove}'));
  MaintPage.SelectedValueIndex := MAINT_UPDATE;
end;

// 更新・修正のときは場所と構成をそのまま引き継ぐので、その2ページは出さない
function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if not IsInstalled() then
    Exit;
  if PageID = wpSelectDir then
    Result := True;
  if (PageID = wpSelectComponents) and (MaintChoice() <> MAINT_MODIFY) then
    Result := True;
end;

function RunUninstaller(): Boolean;
var
  Cmd: String;
  Code: Integer;
begin
  Cmd := RemoveQuotes(GetUninstallString());
  Result := (Cmd <> '') and
            Exec(Cmd, '/SILENT /NORESTART /SUPPRESSMSGBOXES', '', SW_SHOW, ewWaitUntilTerminated, Code);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;

  if IsInstalled() and (MaintPage <> nil) and (CurPageID = MaintPage.ID) then
  begin
    if MaintChoice() = MAINT_REMOVE then
    begin
      if RunUninstaller() then
        MsgBox(ExpandConstant('{cm:RemoveDone}'), mbInformation, MB_OK)
      else
        MsgBox(ExpandConstant('{cm:RemoveFailed}'), mbError, MB_OK);
      CancelWithoutPrompt := True;
      WizardForm.Close;
      Result := False;
    end;
    Exit;
  end;

  if CurPageID = wpSelectComponents then
  begin
    if not (WizardIsComponentSelected('vst3') or WizardIsComponentSelected('standalone')) then
    begin
      MsgBox(ExpandConstant('{cm:NeedOne}'), mbError, MB_OK);
      Result := False;
    end;
  end;
end;

procedure CancelButtonClick(CurPageID: Integer; var Cancel, Confirm: Boolean);
begin
  // 「削除」を選んで閉じるときは、終了確認を出さない
  if CancelWithoutPrompt then
    Confirm := False;
end;

// 「追加・変更」で外した構成は、実ファイルも片付ける
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then
    Exit;

  if not WizardIsComponentSelected('standalone') then
  begin
    DelTree(ExpandConstant('{#MyStandaloneDir}'), True, True, True);
    DeleteFile(ExpandConstant('{commonprograms}\{#MyAppName2}.lnk'));
    DeleteFile(ExpandConstant('{commondesktop}\{#MyAppName2}.lnk'));
  end;

  if not WizardIsComponentSelected('vst3') then
    DelTree(ExpandConstant('{app}\{#MyVst3Name}'), True, True, True);
end;
