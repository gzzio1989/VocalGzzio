[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [string]$OutputDirectory = 'release/windows',
    [switch]$Trial
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
function Resolve-RepoPath([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $repo $Path }
    return [IO.Path]::GetFullPath($Path)
}
$BuildDirectory = Resolve-RepoPath $BuildDirectory
$OutputDirectory = Resolve-RepoPath $OutputDirectory
$source = Get-Content -LiteralPath (Join-Path $repo 'CMakeLists.txt') -Raw
if ($source -notmatch 'project\(VocalGzzio\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    throw '正本のバージョン番号が見つかりません。'
}
$version = $Matches[1]
$cacheFile = Join-Path $BuildDirectory 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cacheFile)) { throw '指定先に構成情報がありません。先に配布用ビルドを作成してください。' }
$cache = Get-Content -LiteralPath $cacheFile -Raw
function Assert-Cache([string]$Pattern, [string]$Message) {
    if ($cache -notmatch $Pattern) { throw $Message }
}
Assert-Cache '(?m)^VOCALGZZIO_BUILD_TESTS:BOOL=OFF\r?$' '検査用ビルドは配布できません。VOCALGZZIO_BUILD_TESTS=OFF で別フォルダーへ構成してください。'
Assert-Cache '(?m)^VOCALGZZIO_LITE:BOOL=OFF\r?$' '機能制限版はこの配布手順では作成できません。'
$trialValue = if ($Trial) { 'ON' } else { 'OFF' }
Assert-Cache ('(?m)^VOCALGZZIO_TRIAL:BOOL=' + $trialValue + '\r?$') '体験版指定がビルドと一致しません。'
Assert-Cache ('(?m)^CMAKE_PROJECT_VERSION:STATIC=' + [regex]::Escape($version) + '\r?$') 'ビルドのバージョンが正本と一致しません。'
$name = if ($Trial) { 'VocalGzzio Trial' } else { 'VocalGzzio' }
$prefix = if ($Trial) { 'VocalGzzio_Trial_Setup_v' } else { 'VocalGzzio_Setup_v' }
$artefacts = Join-Path $BuildDirectory 'VocalGzzio_artefacts/Release'
$standalone = Join-Path $artefacts ('Standalone/' + $name + '.exe')
$plugin = Join-Path $artefacts ('VST3/' + $name + '.vst3/Contents/x86_64-win/' + $name + '.vst3')
foreach ($binary in @($standalone, $plugin)) {
    if (-not (Test-Path -LiteralPath $binary)) { throw "必要な配布ファイルがありません: $binary" }
    $versionInfo = (Get-Item -LiteralPath $binary).VersionInfo
    $binaryVersion = $versionInfo.ProductVersion
    if ($binaryVersion -ne $version) { throw "バイナリの版が一致しません: $binaryVersion / $binary" }
    if ($versionInfo.ProductName -ne $name) { throw "製品名が一致しません: $binary" }
    $binaryText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($binary))
    $edition = if ($Trial) { 'TRIAL' } else { 'PRODUCT' }
    $markers = @([regex]::Matches($binaryText, 'VOCALGZZIO-EDITION:[A-Z]+') | ForEach-Object Value | Select-Object -Unique)
    if ($markers.Count -ne 1 -or $markers[0] -ne ('VOCALGZZIO-EDITION:' + $edition)) {
        throw "バイナリの製品版・体験版の目印が一致しません: $binary"
    }
    if ($binaryText.Contains('VOCALGZZIO_TEST_DATA_DIR')) {
        throw "検査用の保存先切替を含むバイナリは配布できません: $binary"
    }
}
$iscc = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 6/ISCC.exe')
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 が見つかりません。' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$installer = Join-Path $OutputDirectory ($prefix + $version + '.exe')
if (Test-Path -LiteralPath $installer) { throw "既存の配布物は上書きしません。別の出力先を指定してください: $installer" }
$arguments = @('/DExternalBuild=1', ('/DMyAppVersion=' + $version), '/DVerSuffix=',
    ('/DMyBuildDir=' + $BuildDirectory), ('/O' + $OutputDirectory))
if ($Trial) { $arguments += '/DTrialBuild=1' }
$arguments += Join-Path $repo 'Installer.iss'
$log = Join-Path $OutputDirectory ($prefix + $version + '.log')
$savedPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $iscc @arguments 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log
$result = $LASTEXITCODE
$ErrorActionPreference = $savedPreference
if ($result -ne 0 -or -not (Test-Path -LiteralPath $installer)) { throw "インストーラー作成に失敗しました。記録: $log" }
$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($installer + '.sha256', $hash + '  ' + [IO.Path]::GetFileName($installer) + "`n", [Text.UTF8Encoding]::new($false))
Write-Host "作成完了: $installer"
Write-Host '電子署名は付与していません。インストール操作は実行していません。'
