# ============================================================================
# build_driver.ps1
# 100%自社実装 Windows 仮想オーディオドライバ (VocalGzzioAudio.sys) 自動ビルドスクリプト
# ・ローカルに WDK が未導入でも Microsoft 公式 WDK NuGet を自動キャッシュしてコンパイル
# ・Windows 11 HVCI (メモリ整合性 / コア分離) 準拠 (/kernel /guard:cf /INTEGRITYCHECK)
# ・Microsoft Hardware Dashboard 提出要件の Universal INF 検査 (infverif /v /u) を自動実行
# ============================================================================
param(
    [ValidateSet("x64", "ARM64")]
    [string]$Arch = "x64",
    [string]$WdkNuGetVersion = "10.0.26100.6584",
    [string]$WdkCacheRoot = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if ([string]::IsNullOrWhiteSpace($WdkCacheRoot)) {
    $WdkCacheRoot = Join-Path $ScriptDir ".wdk_cache"
}

Write-Host "=== [1/5] Locating Visual Studio MSVC ($Arch) & Windows SDK ===" -ForegroundColor Cyan
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Please install Visual Studio with C++ Desktop workload."
}
$vsPath = (& $vswhere -latest -products * -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($vsPath)) {
    throw "Visual Studio installation not found."
}

$msvcVerDir = Get-ChildItem "$vsPath\VC\Tools\MSVC" | Sort-Object Name -Descending | Select-Object -First 1
if (-not $msvcVerDir) {
    throw "MSVC compiler toolset not found under $vsPath\VC\Tools\MSVC"
}

$targetBinDir = Join-Path $msvcVerDir.FullName "bin\Hostx64\$($Arch.ToLower())"
$clExe   = Join-Path $targetBinDir "cl.exe"
$linkExe = Join-Path $targetBinDir "link.exe"
if (-not (Test-Path $clExe)) {
    throw "MSVC compiler for $Arch not found at: $clExe (Install MSVC $Arch build tools in Visual Studio Installer if needed)"
}

# System Windows SDK (shared / ucrt headers & rc.exe)
$winKitsRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
$sdkIncVer = Get-ChildItem "$winKitsRoot\Include" | Where-Object { Test-Path "$($_.FullName)\shared\ksmedia.h" } | Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdkIncVer) {
    throw "Windows 10/11 SDK (ksmedia.h) not found under $winKitsRoot\Include"
}
$sdkVerName = $sdkIncVer.Name
$sdkIncBase = $sdkIncVer.FullName

$rcExe = Get-ChildItem "$winKitsRoot\bin" -Recurse -Filter "rc.exe" | Where-Object { $_.FullName -match "\\x64\\" } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $rcExe) {
    throw "rc.exe not found under $winKitsRoot\bin"
}

Write-Host "  MSVC : $clExe"
Write-Host "  SDK  : $sdkIncBase"
Write-Host "  RC   : $($rcExe.FullName)"

Write-Host "=== [2/5] Locating or Fetching Official Microsoft WDK ($Arch) ===" -ForegroundColor Cyan
$wdkCBase = $null
$sysWdkVer = Get-ChildItem "$winKitsRoot\Include" -ErrorAction SilentlyContinue | Where-Object {
    (Test-Path "$($_.FullName)\km\portcls.h") -and (Test-Path "$winKitsRoot\Lib\$($_.Name)\km\$($Arch.ToLower())\portcls.lib")
} | Sort-Object Name -Descending | Select-Object -First 1

if ($sysWdkVer) {
    $wdkIncKm     = Join-Path $sysWdkVer.FullName "km"
    $wdkIncCrt    = Join-Path $sysWdkVer.FullName "km\crt"
    $wdkIncShared = Join-Path $sysWdkVer.FullName "shared"
    $wdkLibKm     = Join-Path $winKitsRoot "Lib\$($sysWdkVer.Name)\km\$($Arch.ToLower())"
    $infVerifExe  = Get-ChildItem "$winKitsRoot\Tools" -Recurse -Filter "infverif.exe" -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match "\\x64\\" } | Select-Object -First 1
    $inf2CatExe   = Get-ChildItem "$winKitsRoot\bin" -Recurse -Filter "Inf2Cat.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    Write-Host "  Using system-installed WDK ($($sysWdkVer.Name))"
} else {
    $pkgId = if ($Arch -eq "ARM64") { "microsoft.windows.wdk.arm64" } else { "microsoft.windows.wdk.x64" }
    $pkgExtract = Join-Path $WdkCacheRoot "${pkgId}_${WdkNuGetVersion}\extracted"
    if (-not (Test-Path "$pkgExtract\c\Include")) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $pkgExtract) | Out-Null
        $nupkgZip = Join-Path (Split-Path -Parent $pkgExtract) "wdk.zip"
        $url = "https://api.nuget.org/v3-flatcontainer/$pkgId/$WdkNuGetVersion/$pkgId.$WdkNuGetVersion.nupkg"
        Write-Host "  Downloading official Microsoft WDK NuGet: $url"
        Invoke-WebRequest -Uri $url -OutFile $nupkgZip -UseBasicParsing
        Expand-Archive -Path $nupkgZip -DestinationPath $pkgExtract -Force
    }
    $wdkVerDir = Get-ChildItem "$pkgExtract\c\Include" | Where-Object { Test-Path "$($_.FullName)\km\ntddk.h" } | Sort-Object Name -Descending | Select-Object -First 1
    if (-not $wdkVerDir) {
        throw "WDK Include directory missing inside NuGet cache: $pkgExtract"
    }
    $wdkIncKm     = Join-Path $wdkVerDir.FullName "km"
    $wdkIncCrt    = Join-Path $wdkVerDir.FullName "km\crt"
    $wdkIncShared = Join-Path $wdkVerDir.FullName "shared"
    $wdkLibKm     = Join-Path "$pkgExtract\c\Lib\$($wdkVerDir.Name)\km" $Arch.ToLower()
    $infVerifExe  = Get-ChildItem "$pkgExtract\c\tools" -Recurse -Filter "infverif.exe" -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match "\\x64\\" } | Select-Object -First 1
    $inf2CatExe   = Get-ChildItem "$pkgExtract\c\bin" -Recurse -Filter "Inf2Cat.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    Write-Host "  Using cached WDK NuGet ($($wdkVerDir.Name))"
}

$outRoot   = Join-Path $ScriptDir "out\$Arch"
$objDir    = Join-Path $outRoot "obj"
$pkgOutDir = Join-Path $outRoot "VocalGzzioAudio"
New-Item -ItemType Directory -Force -Path $objDir | Out-Null
New-Item -ItemType Directory -Force -Path $pkgOutDir | Out-Null

Write-Host "=== [3/5] Compiling Kernel Driver Sources (/kernel /guard:cf /W4 /WX) ===" -ForegroundColor Cyan
$archDefines = if ($Arch -eq "ARM64") {
    @("/D_WIN64", "/D_ARM64_", "/DARM64")
} else {
    @("/D_WIN64", "/D_AMD64_", "/DAMD64")
}

$incArgs = @(
    "/I`"$ScriptDir\include`"",
    "/I`"$ScriptDir\sys`"",
    "/I`"$wdkIncKm`"",
    "/I`"$wdkIncCrt`"",
    "/I`"$wdkIncShared`"",
    "/I`"$sdkIncBase\shared`""
)

$commonClArgs = @(
    "/c", "/nologo", "/utf-8", "/Zp8", "/Gy",
    "/Zc:wchar_t-", "/Zc:forScope", "/Zc:inline",
    "/GR-", "/EHs-c-", "/GF",
    "/W4", "/WX", "/O2", "/Oi", "/Oy-", "/Zi",
    "/GS", "/guard:cf", "/kernel", "/std:c++17",
    "/DNTDDI_VERSION=0x0A000006",
    "/D_WIN32_WINNT=0x0A00",
    "/DWINVER=0x0A00",
    "/DWINNT=1",
    "/DUNICODE", "/D_UNICODE",
    "/DPOOL_NX_OPTIN=1",
    "/DNDEBUG",
    "/Fd`"$objDir\vc_kernel.pdb`""
) + $archDefines + $incArgs

$cppFiles = @("Adapter.cpp", "MiniportTopology.cpp", "MiniportWaveRT.cpp")
$objFiles = @()

foreach ($src in $cppFiles) {
    $srcPath = Join-Path "$ScriptDir\sys" $src
    $objPath = Join-Path $objDir ([System.IO.Path]::ChangeExtension($src, ".obj"))
    Write-Host "  [CL] $src"
    & $clExe @commonClArgs "/Fo`"$objPath`"" "`"$srcPath`""
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed for $src (exit code $LASTEXITCODE)"
    }
    $objFiles += "`"$objPath`""
}

$rcPath  = Join-Path "$ScriptDir\sys" "VocalGzzioAudio.rc"
$resPath = Join-Path $objDir "VocalGzzioAudio.res"
Write-Host "  [RC] VocalGzzioAudio.rc"
& $rcExe.FullName /nologo "/I`"$sdkIncBase\um`"" "/I`"$sdkIncBase\shared`"" "/fo`"$resPath`"" "`"$rcPath`""
if ($LASTEXITCODE -ne 0) {
    throw "Resource compilation failed (exit code $LASTEXITCODE)"
}
$objFiles += "`"$resPath`""

Write-Host "=== [4/5] Linking VocalGzzioAudio.sys (HVCI /INTEGRITYCHECK /GUARD:CF) ===" -ForegroundColor Cyan
$sysOut = Join-Path $pkgOutDir "VocalGzzioAudio.sys"
$pdbOut = Join-Path $outRoot "VocalGzzioAudio.pdb"
$machineArg = if ($Arch -eq "ARM64") { "/MACHINE:ARM64" } else { "/MACHINE:X64" }

$linkArgs = @(
    "/NOLOGO",
    "/DRIVER",
    "/SUBSYSTEM:NATIVE,10.00",
    $machineArg,
    "/NODEFAULTLIB",
    "/ENTRY:GsDriverEntry",
    "/RELEASE",
    "/INCREMENTAL:NO",
    "/OPT:REF", "/OPT:ICF",
    "/DEBUG",
    "/PDB:`"$pdbOut`"",
    "/GUARD:CF",
    "/INTEGRITYCHECK",
    "/NXCOMPAT",
    "/DYNAMICBASE",
    "/SECTION:INIT,d",
    "/MERGE:_TEXT=.text;_PAGE=PAGE",
    "/LIBPATH:`"$wdkLibKm`"",
    "/LIBPATH:`"$($msvcVerDir.FullName)\lib\$($Arch.ToLower())`"",
    "/OUT:`"$sysOut`""
) + $objFiles + @(
    "BufferOverflowFastFailK.lib",
    "ntoskrnl.lib",
    "hal.lib",
    "wmilib.lib",
    "portcls.lib",
    "stdunk.lib",
    "ksguid.lib"
)

& $linkExe @linkArgs
if ($LASTEXITCODE -ne 0) {
    throw "Linking VocalGzzioAudio.sys failed (exit code $LASTEXITCODE)"
}

$infSrc = Join-Path "$ScriptDir\inf" "VocalGzzioAudio.inf"
$infDst = Join-Path $pkgOutDir "VocalGzzioAudio.inf"
Copy-Item -Path $infSrc -Destination $infDst -Force

Write-Host "=== [5/5] Verifying Universal INF (InfVerif /u) & Generating Catalog ===" -ForegroundColor Cyan
if ($infVerifExe -and (Test-Path $infVerifExe.FullName)) {
    Write-Host "  [InfVerif] $($infVerifExe.FullName) /v /u `"$infDst`""
    & $infVerifExe.FullName /v /u $infDst
    if ($LASTEXITCODE -ne 0) {
        throw "InfVerif Universal INF check failed (exit code $LASTEXITCODE)"
    }
    Write-Host "  [OK] InfVerif Universal INF validation passed!" -ForegroundColor Green
} else {
    Write-Warning "infverif.exe not found in WDK tools; skipping INF lint."
}

if ($inf2CatExe -and (Test-Path $inf2CatExe.FullName)) {
    $osTag = if ($Arch -eq "ARM64") { "10_VB_ARM64" } else { "10_X64" }
    Write-Host "  [Inf2Cat] Generating VocalGzzioAudio.cat ($osTag)"
    & $inf2CatExe.FullName "/driver:$pkgOutDir" "/os:$osTag" /uselocaltime
    if ($LASTEXITCODE -ne 0) {
        throw "Inf2Cat failed (exit code $LASTEXITCODE)"
    }
}

$sysInfo = Get-Item $sysOut
Write-Host ""
Write-Host "SUCCESS: Built $sysOut ($($sysInfo.Length) bytes)" -ForegroundColor Green
Write-Host "Package staged at: $pkgOutDir" -ForegroundColor Green
