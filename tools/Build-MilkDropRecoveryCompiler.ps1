[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$OutputDirectory,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$repoPath = (Resolve-Path -LiteralPath $RepoRoot).Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repoPath 'TestResults\recovery-compiler'
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
$helperPath = Join-Path $repoPath 'tools\milkdrop_recovery_compiler.py'
$pythonPath = (Get-Command python -ErrorAction Stop).Source
$commonArguments = @('--repo-root', $repoPath, '--output-dir', $outputPath)

# --check never creates files. Exit 3 means missing/stale, not a build failure.
$checkOutput = & $pythonPath $helperPath --check @commonArguments
$checkExit = $LASTEXITCODE
if ($checkExit -ne 0 -and $checkExit -ne 3) {
    throw 'Recovery compiler freshness check failed.'
}
if ($checkExit -eq 0 -and -not $Force) {
    $checkOutput
    exit 0
}

$generated = & $pythonPath $helperPath --generate @commonArguments
if ($LASTEXITCODE -ne 0) { throw 'Generating the current production compiler wrapper failed.' }

$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
    throw 'Visual Studio Installer/vswhere.exe was not found; install the MSVC C++ build tools.'
}
$installationPath = (& $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $installationPath) { throw 'No installed MSVC x64 C++ toolchain was found.' }
$vcvarsPath = Join-Path $installationPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvarsPath -PathType Leaf)) {
    throw "MSVC environment script not found: $vcvarsPath"
}

# These paths become quoted batch literals. Reject characters that could alter
# batch expansion instead of interpolating untrusted shell syntax.
foreach ($path in @($vcvarsPath, $outputPath)) {
    if ($path -match '[%"\r\n]') { throw "Unsupported character in compiler build path: $path" }
}
$buildPath = Join-Path $outputPath 'build.cmd'
$stdoutPath = Join-Path $outputPath 'build.stdout.log'
$stderrPath = Join-Path $outputPath 'build.stderr.log'
$buildText = @"
@echo off
setlocal DisableDelayedExpansion
call "$vcvarsPath" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /MD /std:c++17 /Fe:compile.exe /Fo:compile.obj compile.cpp
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($buildPath, $buildText, [Text.UTF8Encoding]::new($false))
$buildProcess = Start-Process -FilePath $env:ComSpec -ArgumentList @('/d', '/s', '/c', "`"$buildPath`"") `
    -WorkingDirectory $outputPath -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
if (-not $buildProcess.WaitForExit(60000)) {
    $buildProcess.Kill($true)
    throw 'Recovery compiler build exceeded 60 seconds; its build process was stopped.'
}
$buildProcess.WaitForExit()
if ($buildProcess.ExitCode -ne 0) {
    $details = @(Get-Content -LiteralPath $stdoutPath -ErrorAction SilentlyContinue) +
               @(Get-Content -LiteralPath $stderrPath -ErrorAction SilentlyContinue)
    throw "Recovery compiler build failed ($($buildProcess.ExitCode)).`n$($details -join [Environment]::NewLine)"
}

$manifest = & $pythonPath $helperPath --record-build @commonArguments --toolchain $installationPath
if ($LASTEXITCODE -ne 0) { throw 'Compiler built, but its source/executable manifest could not be verified.' }
$manifest
exit 0
