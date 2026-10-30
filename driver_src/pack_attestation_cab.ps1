# ============================================================================
# pack_attestation_cab.ps1
# Microsoft Partner Center (Hardware Dashboard) の Attestation Signing (構成証明署名)
# へ提出するためのドライバ .cab パッケージ (VocalGzzioAudio_Attestation.cab) を生成する。
# -Sign 指定時は EV コード署名証明書 (USBトークン / eSigner 等) で自動署名まで行う。
# ============================================================================
param(
    [ValidateSet("x64", "ARM64")]
    [string]$Arch = "x64",
    [switch]$Sign,
    [string]$CertThumbprint = "",
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$pkgDir    = Join-Path $ScriptDir "out\$Arch\VocalGzzioAudio"

$infFile = Join-Path $pkgDir "VocalGzzioAudio.inf"
$sysFile = Join-Path $pkgDir "VocalGzzioAudio.sys"
$catFile = Get-ChildItem $pkgDir -Filter "*.cat" -ErrorAction SilentlyContinue | Select-Object -First 1

if (-not (Test-Path $infFile) -or -not (Test-Path $sysFile)) {
    Write-Host "Staged driver not found. Running build_driver.ps1 first..." -ForegroundColor Yellow
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ScriptDir "build_driver.ps1") -Arch $Arch
    if ($LASTEXITCODE -ne 0) {
        throw "build_driver.ps1 failed."
    }
    $catFile = Get-ChildItem $pkgDir -Filter "*.cat" -ErrorAction SilentlyContinue | Select-Object -First 1
}

function Find-SignTool {
    $winKits = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
    $st = Get-ChildItem $winKits -Recurse -Filter "signtool.exe" -ErrorAction SilentlyContinue |
          Where-Object { $_.FullName -match "\\x64\\" } |
          Sort-Object FullName -Descending |
          Select-Object -First 1
    if ($st) { return $st.FullName }
    $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

if ($Sign) {
    $signTool = Find-SignTool
    if (-not $signTool) {
        throw "signtool.exe not found in Windows Kits."
    }
    $certArgs = if ([string]::IsNullOrWhiteSpace($CertThumbprint)) { @("/a") } else { @("/sha1", $CertThumbprint) }
    Write-Host "=== Signing VocalGzzioAudio.sys (pre-submission EV signature) ===" -ForegroundColor Cyan
    & $signTool sign /fd sha256 /tr $TimestampUrl /td sha256 @certArgs $sysFile
    if ($LASTEXITCODE -ne 0) {
        throw "EV signing of VocalGzzioAudio.sys failed (exit code $LASTEXITCODE)"
    }
    if ($catFile) {
        Write-Host "=== Signing $($catFile.Name) ===" -ForegroundColor Cyan
        & $signTool sign /fd sha256 /tr $TimestampUrl /td sha256 @certArgs $catFile.FullName
        if ($LASTEXITCODE -ne 0) {
            throw "EV signing of $($catFile.Name) failed (exit code $LASTEXITCODE)"
        }
    }
}

$outRoot = Join-Path $ScriptDir "out"
$cabName = "VocalGzzioAudio_Attestation_$Arch.cab"
$cabPath = Join-Path $outRoot $cabName
$ddfPath = Join-Path $outRoot "VocalGzzioAudio_Attestation_$Arch.ddf"

$ddfLines = @(
    ".OPTION EXPLICIT",
    ".Set CabinetFileCountThreshold=0",
    ".Set FolderFileCountThreshold=0",
    ".Set FolderSizeThreshold=0",
    ".Set MaxCabinetSize=0",
    ".Set MaxDiskFileCount=0",
    ".Set MaxDiskSize=0",
    ".Set CompressionType=MSZIP",
    ".Set Cabinet=on",
    ".Set Compress=on",
    ".Set CabinetNameTemplate=$cabName",
    ".Set DiskDirectory1=`"$outRoot`"",
    ".Set DestinationDir=VocalGzzioAudio",
    "`"$infFile`"",
    "`"$sysFile`""
)
if ($catFile) {
    $ddfLines += "`"$($catFile.FullName)`""
}

[System.IO.File]::WriteAllLines($ddfPath, $ddfLines, [System.Text.Encoding]::ASCII)

Write-Host "=== Packing Microsoft Hardware Dashboard Submission CAB ===" -ForegroundColor Cyan
Push-Location $outRoot
try {
    & makecab.exe /F $ddfPath | Out-Host
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $cabPath)) {
        throw "makecab.exe failed to produce $cabPath"
    }
} finally {
    Pop-Location
}

if ($Sign) {
    $signTool = Find-SignTool
    $certArgs = if ([string]::IsNullOrWhiteSpace($CertThumbprint)) { @("/a") } else { @("/sha1", $CertThumbprint) }
    Write-Host "=== Signing Submission CAB with EV Code Signing Certificate ===" -ForegroundColor Cyan
    & $signTool sign /fd sha256 /tr $TimestampUrl /td sha256 @certArgs $cabPath
    if ($LASTEXITCODE -ne 0) {
        throw "EV signing of $cabPath failed (exit code $LASTEXITCODE)"
    }
    Write-Host ""
    Write-Host "READY TO UPLOAD TO MICROSOFT PARTNER CENTER HARDWARE DASHBOARD:" -ForegroundColor Green
    Write-Host "  $cabPath" -ForegroundColor Green
} else {
    $cabItem = Get-Item $cabPath
    Write-Host ""
    Write-Host "SUCCESS: Created unsigned submission CAB ($($cabItem.Length) bytes):" -ForegroundColor Green
    Write-Host "  $cabPath" -ForegroundColor Green
    Write-Host ""
    Write-Host "Once your EV Code Signing Certificate is ready, sign it with:" -ForegroundColor Yellow
    Write-Host "  powershell -ExecutionPolicy Bypass -File .\driver_src\pack_attestation_cab.ps1 -Sign" -ForegroundColor Yellow
}
