[CmdletBinding()]
param(
    [ValidateSet("debug", "releasedbg")]
    [string]$Mode = "releasedbg",

    [switch]$Analyze,

    [switch]$Quiet
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $PSScriptRoot "Build.ps1"
$pluginTarget = "NativeEditorIDFix"
$testTarget = "NativeEditorIDFixTests"

& $buildScript -Mode $Mode -Analyze:$Analyze -ConfigureOnly -Quiet:$Quiet

Push-Location $repoRoot
try {
    if (-not $Quiet) {
        Write-Host "Building tests" -ForegroundColor Cyan
        Write-Host "  plugin: $pluginTarget"
        Write-Host "  target: $testTarget"
        Write-Host "  mode:   $Mode"
        Write-Host "  analyze: $($Analyze.IsPresent)"
        Write-Host ""
    }

    & xmake build $pluginTarget
    if ($LASTEXITCODE -ne 0) {
        throw "xmake build $pluginTarget failed."
    }

    & xmake build $testTarget
    if ($LASTEXITCODE -ne 0) {
        throw "xmake build $testTarget failed."
    }

    & xmake run $testTarget
    if ($LASTEXITCODE -ne 0) {
        throw "xmake run $testTarget failed."
    }
} finally {
    Pop-Location
}
