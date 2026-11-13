<#
.SYNOPSIS
Read-only assessment of hidden MilkDrop presets using the current DX11 compiler path.
.DESCRIPTION
Scans the current preset-blacklist.txt by default. Produces an HTML report,
CSV/JSON evidence, compiler logs and a live-test shortlist under TestResults.
Does not change presets, settings, exclusions or the installed component.
Foobar2000 may remain open. Compilation does not prove correct visual output.
Requires Python 3.10+; building a missing/stale helper requires MSVC C++ tools.
.EXAMPLE
pwsh -NoProfile -File .\tools\Test-MilkDropRecovery.ps1
.EXAMPLE
pwsh -NoProfile -File .\tools\Test-MilkDropRecovery.ps1 -PresetList C:\Temp\presets-to-check.txt
#>
[CmdletBinding()]
param(
    [string]$MilkdropRoot = (Join-Path $env:APPDATA 'foobar2000-v2\milkdrop2'),
    [string]$PresetList,
    [string]$OutputDirectory,
    [ValidateRange(1, 4)][int]$Workers = 2,
    [ValidateRange(1, 120)][int]$ShaderTimeoutSeconds = 25
)

$ErrorActionPreference = 'Stop'
try {
    $repoPath = Split-Path -Parent $PSScriptRoot
    $pythonPath = (Get-Command python -ErrorAction Stop).Source
    & $pythonPath -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)'
    if ($LASTEXITCODE -ne 0) { throw 'Python 3.10 or later is required.' }

    $rootPath = (Resolve-Path -LiteralPath $MilkdropRoot).Path
    if (-not $PresetList) { $PresetList = Join-Path $rootPath 'preset-blacklist.txt' }
    $listPath = (Resolve-Path -LiteralPath $PresetList).Path
    if (-not $OutputDirectory) {
        $runName = 'recovery-scan-{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), $PID
        $OutputDirectory = Join-Path $repoPath (Join-Path 'TestResults' $runName)
    }

    $includePath = Join-Path (Split-Path -Parent $rootPath) 'user-components-x64\foo_vis_milk2\data\include.fx'
    if (-not (Test-Path -LiteralPath $includePath -PathType Leaf)) {
        $includePath = Join-Path $repoPath 'external\winamp\data\include.fx'
        Write-Host 'Installed shader include not found; using the include from this checkout.'
    }
    if (-not (Test-Path -LiteralPath $includePath -PathType Leaf)) {
        throw "Shader include not found: $includePath"
    }

    Write-Host 'Checking the current DX11 compiler helper...'
    $builder = Join-Path $PSScriptRoot 'Build-MilkDropRecoveryCompiler.ps1'
    $compilerDirectory = Join-Path $repoPath 'TestResults\recovery-compiler'
    & $builder -RepoRoot $repoPath -OutputDirectory $compilerDirectory | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'Preparing the DX11 compiler helper failed.' }

    $scanner = Join-Path $PSScriptRoot 'milkdrop_recovery_scan.py'
    $scanArguments = @(
        $scanner,
        '--milkdrop-root', $rootPath,
        '--preset-list', $listPath,
        '--output-directory', $OutputDirectory,
        '--compiler', (Join-Path $compilerDirectory 'compile.exe'),
        '--include-file', $includePath,
        '--workers', $Workers.ToString(),
        '--timeout', $ShaderTimeoutSeconds.ToString()
    )
    & $pythonPath @scanArguments
    exit $LASTEXITCODE
}
catch {
    Write-Error -Message $_ -ErrorAction Continue
    exit 2
}
