[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/validation',
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [string]$JuceSource = '',
    [string]$PythonExecutable = '',
    [string]$TestPattern = '',
    [int]$Jobs = 4,
    [switch]$SkipBuild,
    [switch]$Trial
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $repo $BuildDirectory
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$logDirectory = Join-Path $BuildDirectory ('logs/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null

# Visual Studio 同梱の CMake / CTest を自動検出する
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmake = if ($cmakeCommand) { $cmakeCommand.Source } else { $null }
if (-not $cmake) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $vs = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vs) {
            $candidate = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
            if (Test-Path -LiteralPath $candidate) { $cmake = $candidate }
        }
    }
}
if (-not $cmake) { throw "CMake and C++ build tools not found. Please check Visual Studio C++ workload." }
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest)) { throw "ctest.exe not found in CMake directory." }

function Invoke-Logged {
    param([string]$Executable, [string[]]$Arguments, [string]$LogName)
    $logFile = Join-Path $logDirectory $LogName
    # PowerShell 5.1 の NativeCommandError を避けつつ全出力を保存する
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $Executable @Arguments 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $logFile
    $result = $LASTEXITCODE
    $ErrorActionPreference = $savedPreference
    if ($result -ne 0) { throw "Validation failed with exit code $result. Log: $logFile" }
}

Write-Host "Build directory: $BuildDirectory"
Write-Host "Log directory: $logDirectory"
if (-not $SkipBuild) {
    $configure = @('-S', $repo, '-B', $BuildDirectory,
        '-DVOCALGZZIO_BUILD_TESTS=ON', ('-DCMAKE_BUILD_TYPE=' + $Configuration))
    if ($Trial) { $configure += '-DVOCALGZZIO_TRIAL=ON' }
    else { $configure += '-DVOCALGZZIO_TRIAL=OFF' }
    if (-not $JuceSource) {
        $cachedJuce = Join-Path $repo 'build/_deps/juce-src'
        if (Test-Path -LiteralPath (Join-Path $cachedJuce 'CMakeLists.txt')) { $JuceSource = $cachedJuce }
    }
    if ($JuceSource) {
        $configure += '-DFETCHCONTENT_SOURCE_DIR_JUCE=' + [IO.Path]::GetFullPath($JuceSource)
    }
    if ($PythonExecutable) {
        $configure += '-DPython3_EXECUTABLE=' + [IO.Path]::GetFullPath($PythonExecutable)
    }
    Invoke-Logged $cmake $configure 'configure.log'
    Invoke-Logged $cmake @('--build', $BuildDirectory, '--config', $Configuration, '--parallel', "$Jobs") 'build.log'
}
$testArguments = @('--test-dir', $BuildDirectory, '-C', $Configuration,
    '--output-on-failure', '--no-tests=error', '--output-junit', (Join-Path $logDirectory 'results.xml'))
if ($TestPattern) { $testArguments += @('-R', $TestPattern) }
Invoke-Logged $ctest $testArguments 'tests.log'
Write-Host "Validation completed. Log: $logDirectory"
