[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Target,

    [ValidateSet("debug", "releasedbg")]
    [string]$Mode = "releasedbg",

    [switch]$IncludePdb,

    [switch]$OverwriteIni,

    [switch]$SkipBuild,

    [switch]$Reconfigure,

    [switch]$Quiet
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $PSScriptRoot "Build.ps1"
$pluginName = "NativeEditorIDFix"
$pluginFile = "$pluginName.dll"
$pdbFile = "$pluginName.pdb"
$iniFile = "$pluginName.ini"
$buildRoot = Join-Path $repoRoot "build/windows/x64/$Mode"
$distRoot = Join-Path $repoRoot "dist"
$sourceDll = Join-Path $buildRoot $pluginFile
$sourcePdb = Join-Path $buildRoot $pdbFile
$sourceIni = Join-Path $distRoot $iniFile
$targetRootPath = [System.IO.Path]::GetFullPath($Target)
$pluginTargetRoot = Join-Path $targetRootPath "SKSE/Plugins"
$targetDll = Join-Path $pluginTargetRoot $pluginFile
$targetPdb = Join-Path $pluginTargetRoot $pdbFile
$targetIni = Join-Path $pluginTargetRoot $iniFile
$targetIniExisted = Test-Path -LiteralPath $targetIni

if (-not (Test-Path -LiteralPath $sourceIni)) {
    throw "Missing tracked config template at '$sourceIni'."
}

if (-not $SkipBuild) {
    & $buildScript -Mode $Mode -Quiet:$Quiet -Reconfigure:$Reconfigure
    if ($LASTEXITCODE -ne 0) {
        throw "Build.ps1 failed."
    }
}

if (-not (Test-Path -LiteralPath $sourceDll)) {
    throw "Built plugin not found at '$sourceDll'."
}

New-Item -ItemType Directory -Path $pluginTargetRoot -Force | Out-Null

Copy-Item -LiteralPath $sourceDll -Destination $targetDll -Force

if ($IncludePdb) {
    if (-not (Test-Path -LiteralPath $sourcePdb)) {
        throw "Requested PDB deploy, but '$sourcePdb' does not exist."
    }

    Copy-Item -LiteralPath $sourcePdb -Destination $targetPdb -Force
}

if ($OverwriteIni -or -not $targetIniExisted) {
    Copy-Item -LiteralPath $sourceIni -Destination $targetIni -Force
}

if (-not $Quiet) {
    Write-Host "Deployed Native EditorID Fix VR:" -ForegroundColor Cyan
    Write-Host "  target:       $targetRootPath"
    Write-Host "  mode:         $Mode"
    Write-Host "  dll:          $targetDll"
    if ($IncludePdb) {
        Write-Host "  pdb:          $targetPdb"
    }
    Write-Host "  ini:          $targetIni"
    Write-Host "  ini updated:  $($OverwriteIni -or -not $targetIniExisted)"
}
