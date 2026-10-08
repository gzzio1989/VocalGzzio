#Requires -Version 5.1
<#
.SYNOPSIS
インストール済みの製品版 VST3 を、照合済みバックアップ付きで更新します。
.DESCRIPTION
既定は検査のみです。実際の更新には -Apply と、検査結果の -ExpectedSHA256 が必要です。
-Apply -WhatIf でも変更しません。管理者への自動昇格や、使用中ソフトの終了は行いません。
単体起動版、体験版、設定・プリセット、インストーラーの登録情報は変更しません。
この検査は音質や動作の合格を意味しません。更新元を別途検証してから適用してください。
.EXAMPLE
./tools/update_installed_vst3.ps1 -BuildDirectory build
.EXAMPLE
./tools/update_installed_vst3.ps1 -BuildDirectory build -Apply -ExpectedSHA256 <検査結果のSHA256> -WhatIf
#>
[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [string]$BuildDirectory = 'build',
    [switch]$Apply,
    [ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ExpectedSHA256
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
# 違う製品や別ユーザーの場所を誤って操作しないため、この端末の更新先を固定する。
$installedBundle = 'C:\Program Files\Common Files\VST3\VocalGzzio.vst3'
$backupRoot = 'C:\Users\silve\VocalGzzio_Backups'
$installedParent = Split-Path -Parent $installedBundle

function Assert-ChildPath([string]$Path, [string]$Parent) {
    $full = [IO.Path]::GetFullPath($Path)
    $prefix = [IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "予定した範囲外のパスです: $full"
    }
}

function Assert-NoLink([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "リンクまたは接合点は操作しません: $current"
            }
        }
        $current = Split-Path -Parent $current
    }
}

function Get-TreeManifest([string]$Root) {
    Assert-NoLink $Root
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { throw "フォルダーがありません: $Root" }
    $prefix = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($Root)
    $entries = @(
        while ($pending.Count -gt 0) {
            foreach ($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
                Assert-ChildPath $item.FullName $Root
                if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                    throw "リンクまたは接合点を含むフォルダーは操作しません: $($item.FullName)"
                }
                $directory = $item.PSIsContainer
                [pscustomobject][ordered]@{
                    Path = $item.FullName.Substring($prefix.Length)
                    Directory = $directory
                    Length = $(if ($directory) { 0L } else { $item.Length })
                    SHA256 = $(if ($directory) { '' } else { (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash })
                }
                if ($directory) { $pending.Push($item.FullName) }
            }
        }
    )
    $entries | Sort-Object Path
}

function Assert-Manifest([string]$Root, [object[]]$Expected) {
    $actual = @(Get-TreeManifest $Root)
    $expectedJson = ConvertTo-Json -InputObject @($Expected | Sort-Object Path) -Depth 4 -Compress
    $actualJson = ConvertTo-Json -InputObject $actual -Depth 4 -Compress
    if ($expectedJson -cne $actualJson) { throw "ファイル数・内容の照合に失敗しました: $Root" }
}

function Copy-TreeChecked([string]$Source, [string]$Destination, [object[]]$Manifest) {
    Assert-NoLink $Source
    Assert-NoLink $Destination
    if (Test-Path -LiteralPath $Destination) { throw "既存の保存先は上書きしません: $Destination" }
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    foreach ($entry in $Manifest) {
        $from = Join-Path $Source $entry.Path
        $to = Join-Path $Destination $entry.Path
        Assert-ChildPath $from $Source
        Assert-ChildPath $to $Destination
        Assert-NoLink $from
        Assert-NoLink $to
        if ($entry.Directory) { [IO.Directory]::CreateDirectory($to) | Out-Null }
        else {
            [IO.Directory]::CreateDirectory((Split-Path -Parent $to)) | Out-Null
            Copy-Item -LiteralPath $from -Destination $to
        }
    }
    Assert-Manifest $Destination $Manifest
    Assert-Manifest $Source $Manifest
}

function Assert-Unlocked([string]$Root, [object[]]$Manifest) {
    foreach ($entry in $Manifest | Where-Object { -not $_.Directory }) {
        $file = Join-Path $Root $entry.Path
        Assert-ChildPath $file $Root
        Assert-NoLink $file
        $handle = $null
        try {
            # 書き込まず、排他で開けることだけを調べる。使用中・権限不足はいずれも停止する。
            $handle = [IO.File]::Open($file, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        }
        catch { throw "使用中、または更新権限がありません。録音・配信ソフトを閉じて確認してください: $file`n$($_.Exception.Message)" }
        finally { if ($null -ne $handle) { $handle.Dispose() } }
    }
}

function Assert-ProductBinary([string]$Path, [string]$Version) {
    Assert-NoLink $Path
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "製品版 VST3 がありません: $Path" }
    $info = (Get-Item -LiteralPath $Path).VersionInfo
    if ($info.ProductVersion -ne $Version -or $info.ProductName -ne 'VocalGzzio') {
        throw "バイナリの製品名・バージョンが正本と一致しません: $Path"
    }
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 128 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw 'Windows バイナリではありません。' }
    $pe = [BitConverter]::ToInt32($bytes, 60)
    if ($pe -lt 0 -or $pe -gt $bytes.Length - 6 -or
        [BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664) {
        throw '64 ビット Windows 用のバイナリではありません。'
    }
    $text = [Text.Encoding]::ASCII.GetString($bytes)
    $markers = @([regex]::Matches($text, 'VOCALGZZIO-EDITION:[A-Z]+') | ForEach-Object { $_.Value } | Select-Object -Unique)
    if ($markers.Count -ne 1 -or $markers[0] -ne 'VOCALGZZIO-EDITION:PRODUCT') { throw '製品版の目印を確認できません。' }
    if ($text.Contains('VOCALGZZIO_TEST_DATA_DIR')) { throw '検査用の保存先切替を含むバイナリは導入できません。' }
}

if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $repo $BuildDirectory }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
Assert-ChildPath $BuildDirectory $repo
Assert-NoLink $BuildDirectory
Assert-NoLink $installedBundle
Assert-NoLink $backupRoot
$cmake = Get-Content -LiteralPath (Join-Path $repo 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(VocalGzzio\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') { throw '正本のバージョンが見つかりません。' }
$version = $Matches[1]
$cache = Get-Content -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -Raw
foreach ($flag in @('VOCALGZZIO_BUILD_TESTS', 'VOCALGZZIO_LITE', 'VOCALGZZIO_TRIAL')) {
    if ($cache -notmatch ('(?m)^' + $flag + ':BOOL=OFF\r?$')) { throw "製品版の配布構成ではありません: $flag" }
}
if ($cache -notmatch ('(?m)^CMAKE_PROJECT_VERSION:STATIC=' + [regex]::Escape($version) + '\r?$')) { throw 'ビルドの版が正本と一致しません。' }
$sourceBundle = Join-Path $BuildDirectory 'VocalGzzio_artefacts\Release\VST3\VocalGzzio.vst3'
$binaryRelative = 'Contents\x86_64-win\VocalGzzio.vst3'
$sourceBinary = Join-Path $sourceBundle $binaryRelative
Assert-ProductBinary $sourceBinary $version
$sourceManifest = @(Get-TreeManifest $sourceBundle)
$sourceHash = (Get-FileHash -LiteralPath $sourceBinary -Algorithm SHA256).Hash
if ($ExpectedSHA256 -and $sourceHash -ne $ExpectedSHA256) { throw '更新元が指定した SHA256 と一致しません。適用を中止しました。' }
$originalManifest = @(Get-TreeManifest $installedBundle)
if (-not (Test-Path -LiteralPath (Join-Path $installedBundle $binaryRelative) -PathType Leaf)) { throw '既存の製品版バンドルを確認できません。新規インストールには対応していません。' }
Assert-Unlocked $installedBundle $originalManifest

# インストーラーと同じ説明書・ライセンスを同梱する。既存の追加文書もバックアップして維持する。
$documents = @(
    @{ Source = 'LICENSE'; Destination = 'LICENSE.txt' },
    @{ Source = 'Resources\kawaii_font_LICENSE_OFL.txt'; Destination = 'kawaii_font_LICENSE_OFL.txt' },
    @{ Source = 'docs\manual.html'; Destination = '取扱説明書.html' },
    @{ Source = 'docs\style.css'; Destination = 'style.css' }
)
$licenseRoot = Join-Path $repo '配布物\ライセンス'
$licenseManifest = @(Get-TreeManifest $licenseRoot)
if (@($licenseManifest | Where-Object { -not $_.Directory }).Count -eq 0) { throw '配布用ライセンスが空です。' }
foreach ($entry in $licenseManifest | Where-Object { -not $_.Directory }) {
    $documents += @{ Source = ('配布物\ライセンス\' + $entry.Path); Destination = ('ライセンス\' + $entry.Path) }
}
foreach ($document in $documents) {
    $path = Join-Path $repo $document.Source
    Assert-NoLink $path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "同梱文書がありません: $path" }
    $document.SHA256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}

$result = [ordered]@{
    状態 = '検査完了・変更なし'
    バージョン = $version
    更新元 = $sourceBundle
    更新先 = $installedBundle
    更新元SHA256 = $sourceHash
    保存先 = $backupRoot
    既存ファイル数 = @($originalManifest | Where-Object { -not $_.Directory }).Count
}
if (-not $Apply) { [pscustomobject]$result; return }
if (-not $ExpectedSHA256) { throw '実際の更新には、検査結果の -ExpectedSHA256 が必要です。' }
if (-not $PSCmdlet.ShouldProcess($installedBundle, 'バンドル全体を別フォルダーへ保存・照合してから製品版を更新')) { [pscustomobject]$result; return }

$id = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$sessionBackup = Join-Path $backupRoot ('VST3更新_' + $id)
$originalBackup = Join-Path $sessionBackup 'VocalGzzio.vst3'
$workDirectory = Join-Path $installedParent ('.VocalGzzio-update-' + $id)
$stagedBundle = Join-Path $workDirectory 'NewBundle'
$previousBundle = Join-Path $workDirectory 'PreviousBundle'
$rejectedBundle = Join-Path $workDirectory 'RejectedBundle'
Assert-ChildPath $sessionBackup $backupRoot
Assert-ChildPath $workDirectory $installedParent
Assert-ChildPath $originalBackup $sessionBackup
foreach ($path in @($stagedBundle, $previousBundle, $rejectedBundle)) { Assert-ChildPath $path $workDirectory }
foreach ($path in @($sessionBackup, $workDirectory)) {
    Assert-NoLink $path
    if (Test-Path -LiteralPath $path) { throw "作業先が既に存在しています: $path" }
}
$oldMoved = $false
$newMoved = $false
$updateVerified = $false
try {
    [IO.Directory]::CreateDirectory($sessionBackup) | Out-Null
    Copy-TreeChecked $installedBundle $originalBackup $originalManifest
    $originalManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $sessionBackup '更新前の照合記録.json') -Encoding UTF8
    [IO.File]::WriteAllText((Join-Path $sessionBackup '復元について.txt'), @"
元のバンドル全体: $originalBackup
元の配置先: $installedBundle
更新元の版: $version
更新元 SHA256: $sourceHash

復元時は録音・配信ソフトを終了してください。
現在のバンドルを別の場所へ保存し、この保存済み VocalGzzio.vst3 フォルダー全体を元の配置先へ戻します。
中のファイルだけを重ね書きしないでください。照合記録には更新前の各ファイルの SHA256 が入っています。
使用許諾、説明書、第三者ライセンスも含めて保存しています。
この更新は単体起動版、体験版、設定・プリセット、インストーラーの登録情報には触れていません。
"@, [Text.UTF8Encoding]::new($true))
    [IO.Directory]::CreateDirectory($workDirectory) | Out-Null
    Copy-TreeChecked $sourceBundle $stagedBundle $sourceManifest
    # 古い実行部分を混ぜない。Contents 以外の追加文書だけを、衝突しない範囲で維持する。
    foreach ($entry in $originalManifest | Where-Object { $_.Path -ne 'Contents' -and -not $_.Path.StartsWith('Contents\', [StringComparison]::OrdinalIgnoreCase) }) {
        $to = Join-Path $stagedBundle $entry.Path
        Assert-ChildPath $to $stagedBundle
        if (-not (Test-Path -LiteralPath $to)) {
            if ($entry.Directory) { [IO.Directory]::CreateDirectory($to) | Out-Null }
            else {
                [IO.Directory]::CreateDirectory((Split-Path -Parent $to)) | Out-Null
                Copy-Item -LiteralPath (Join-Path $originalBackup $entry.Path) -Destination $to
                if ((Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash -ne $entry.SHA256) { throw '追加文書の保存照合に失敗しました。' }
            }
        }
    }
    foreach ($document in $documents) {
        $from = Join-Path $repo $document.Source
        $to = Join-Path $stagedBundle $document.Destination
        Assert-NoLink $from
        Assert-NoLink $to
        Assert-ChildPath $to $stagedBundle
        [IO.Directory]::CreateDirectory((Split-Path -Parent $to)) | Out-Null
        Copy-Item -LiteralPath $from -Destination $to -Force
        if ((Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash -ne $document.SHA256) { throw '同梱文書が検査後に変わりました。' }
    }
    Assert-ProductBinary (Join-Path $stagedBundle $binaryRelative) $version
    if ((Get-FileHash -LiteralPath (Join-Path $stagedBundle $binaryRelative) -Algorithm SHA256).Hash -ne $ExpectedSHA256) { throw '配置準備後のバイナリ照合に失敗しました。' }
    $stagedManifest = @(Get-TreeManifest $stagedBundle)
    $stagedManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $sessionBackup '更新後の照合記録.json') -Encoding UTF8
    Assert-Manifest $installedBundle $originalManifest
    Assert-Manifest $originalBackup $originalManifest
    Assert-Unlocked $installedBundle $originalManifest
    foreach ($path in @($installedBundle, $stagedBundle, $previousBundle)) { Assert-NoLink $path }
    Assert-ChildPath $installedBundle $installedParent
    Assert-ChildPath $stagedBundle $workDirectory
    Assert-ChildPath $previousBundle $workDirectory
    Move-Item -LiteralPath $installedBundle -Destination $previousBundle
    $oldMoved = $true
    Move-Item -LiteralPath $stagedBundle -Destination $installedBundle
    $newMoved = $true
    Assert-Manifest $installedBundle $stagedManifest
    Assert-Manifest $previousBundle $originalManifest
    $updateVerified = $true
}
catch {
    $failure = $_.Exception.Message
    if ($oldMoved) {
        try {
            foreach ($path in @($installedBundle, $previousBundle, $rejectedBundle)) { Assert-NoLink $path }
            Assert-ChildPath $installedBundle $installedParent
            Assert-ChildPath $previousBundle $workDirectory
            Assert-ChildPath $rejectedBundle $workDirectory
            Assert-Manifest $previousBundle $originalManifest
            if ($newMoved) {
                if (Test-Path -LiteralPath $rejectedBundle) { throw '復元用の退避先が既に存在しています。' }
                Move-Item -LiteralPath $installedBundle -Destination $rejectedBundle
            }
            if (Test-Path -LiteralPath $installedBundle) { throw '復元先に別のバンドルがあります。上書きしません。' }
            Move-Item -LiteralPath $previousBundle -Destination $installedBundle
            Assert-Manifest $installedBundle $originalManifest
        }
        catch { throw "更新失敗: $failure`n自動復元も停止しました: $($_.Exception.Message)`n元の完全なバックアップ: $originalBackup`n作業先: $workDirectory" }
        throw "更新を中止し、元のバンドルへ復元しました: $failure`nバックアップ: $originalBackup`n確認用の作業先: $workDirectory"
    }
    throw "更新を中止しました。導入済みバンドルは変更していません: $failure`n保存先: $sessionBackup`n作業先: $workDirectory"
}

if ($updateVerified) {
    # 復元用の永久バックアップを再照合してから、この実行専用の一時フォルダーだけ片付ける。
    Assert-Manifest $originalBackup $originalManifest
    Assert-ChildPath $workDirectory $installedParent
    Assert-NoLink $workDirectory
    $null = @(Get-TreeManifest $workDirectory)
    try { Remove-Item -LiteralPath $workDirectory -Recurse -Force }
    catch { Write-Warning "更新は完了しました。一時フォルダーは使用中などの理由で残っています: $workDirectory" }
    $result.状態 = '更新完了・全ファイル照合済み'
    $result.保存先 = $originalBackup
    $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $sessionBackup '更新結果.json') -Encoding UTF8
    [pscustomobject]$result
}
