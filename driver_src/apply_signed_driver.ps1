# ============================================================================
# apply_signed_driver.ps1
# Microsoft Partner Center (Hardware Dashboard) からダウンロード・解凍した
# 公式署名済み VocalGzzioAudio ドライバ一式を検証 (/kp カーネルポリシー) し、
# 正当な署名がある場合のみリポジトリの driver\ フォルダへ反映するスクリプト。
# 未署名・テスト署名ドライバが誤って製品配布物に混入する事故を防止する。
# ============================================================================
param(
    [Parameter(Mandatory = $true)]
    [string]$SignedDir,
    [switch]$RemoveLegacySyncroomFallback,
    [switch]$AllowTestSignedForLocalVmOnly
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = Split-Path -Parent $ScriptDir
$TargetDriverDir = Join-Path $RepoRoot "driver"

if (-not (Test-Path -LiteralPath $SignedDir)) {
    throw "SignedDir does not exist: $SignedDir"
}

$infFile = Get-ChildItem -LiteralPath $SignedDir -Recurse -Filter "VocalGzzioAudio.inf" | Select-Object -First 1
$sysFile = Get-ChildItem -LiteralPath $SignedDir -Recurse -Filter "VocalGzzioAudio.sys" | Select-Object -First 1
$catFile = Get-ChildItem -LiteralPath $SignedDir -Recurse -Filter "VocalGzzioAudio.cat" | Select-Object -First 1

if (-not $infFile -or -not $sysFile -or -not $catFile) {
    throw "Required files (VocalGzzioAudio.inf / .sys / .cat) were not all found inside: $SignedDir"
}

$winKits  = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
$signTool = Get-ChildItem $winKits -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\x64\\" } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
if (-not $signTool) {
    throw "signtool.exe not found under $winKits"
}

if (-not $AllowTestSignedForLocalVmOnly) {
    Write-Host "=== Verifying Microsoft Kernel-Mode Signature (/kp) ===" -ForegroundColor Cyan
    & $signTool.FullName verify /kp /v /c $catFile.FullName $sysFile.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "Kernel-mode signature verification (/kp) FAILED. Do NOT deploy unsigned or non-Microsoft-signed drivers to production."
    }
    & $signTool.FullName verify /kp /v $catFile.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "Catalog kernel-mode signature verification (/kp) FAILED."
    }
    Write-Host "  [OK] Microsoft Kernel-Mode Driver signature verified!" -ForegroundColor Green
} else {
    Write-Warning "Skipping /kp check because -AllowTestSignedForLocalVmOnly was explicitly specified."
}

New-Item -ItemType Directory -Force -Path $TargetDriverDir | Out-Null
Copy-Item -LiteralPath $infFile.FullName -Destination (Join-Path $TargetDriverDir "VocalGzzioAudio.inf") -Force
Copy-Item -LiteralPath $sysFile.FullName -Destination (Join-Path $TargetDriverDir "VocalGzzioAudio.sys") -Force
Copy-Item -LiteralPath $catFile.FullName -Destination (Join-Path $TargetDriverDir "VocalGzzioAudio.cat") -Force

if ($RemoveLegacySyncroomFallback) {
    foreach ($legacy in @("syncroom.inf", "syncroom.cat", "syncroom.sys")) {
        $p = Join-Path $TargetDriverDir $legacy
        if (Test-Path -LiteralPath $p) {
            Remove-Item -LiteralPath $p -Force
            Write-Host "  Removed legacy fallback file: $p"
        }
    }
}

Write-Host ""
Write-Host "SUCCESS: Deployed Microsoft-signed VocalGzzioAudio.{inf,cat,sys} to $TargetDriverDir" -ForegroundColor Green
Write-Host "Next step: Run 配布物をつくる.bat to build the updated installer and BOOTH ZIPs." -ForegroundColor Cyan
