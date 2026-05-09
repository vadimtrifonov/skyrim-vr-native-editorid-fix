[CmdletBinding()]
param(
    [ValidateSet("debug", "releasedbg")]
    [string]$Mode = "releasedbg",

    [string]$Version,

    [switch]$IncludePdb,

    [switch]$SkipBuild,

    [switch]$Reconfigure,

    [switch]$Quiet
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $PSScriptRoot "Build.ps1"
$xmakeFile = Join-Path $repoRoot "xmake.lua"
$pluginName = "NativeEditorIDFix"
$archiveStem = "NativeEditorIDFixVR"
$buildRoot = Join-Path $repoRoot "build/windows/x64/$Mode"
$distRoot = Join-Path $repoRoot "dist"
$artifactsRoot = Join-Path $repoRoot "artifacts"
$pluginFile = "$pluginName.dll"
$pdbFile = "$pluginName.pdb"
$iniFile = "$pluginName.ini"

function Get-ProjectVersion
{
    $match = Select-String -Path $xmakeFile -Pattern 'set_version\("([^"]+)"\)' | Select-Object -First 1
    if (-not $match) {
        throw "Could not read project version from '$xmakeFile'."
    }

    return $match.Matches[0].Groups[1].Value
}

function Get-CommitSuffix
{
    $commit = (& git -C $repoRoot rev-parse --short HEAD | Select-Object -First 1)
    if (-not $commit) {
        return $null
    }

    return $commit.Trim()
}

if (-not $Version) {
    $Version = Get-ProjectVersion
}

$versionIsPlaceholder = $Version -eq "0.0.0"
$commitSuffix = Get-CommitSuffix

$archiveBase = if ($Mode -eq "releasedbg") {
    if ($versionIsPlaceholder -and $commitSuffix) {
        "$archiveStem-$Version-$commitSuffix"
    } else {
        "$archiveStem-$Version"
    }
} else {
    if ($versionIsPlaceholder -and $commitSuffix) {
        "$archiveStem-$Version-$commitSuffix-$Mode"
    } else {
        "$archiveStem-$Version-$Mode"
    }
}

$stageRoot = Join-Path $artifactsRoot $archiveBase
$stageModRoot = Join-Path $stageRoot "mod"
$stagePluginRoot = Join-Path $stageModRoot "SKSE/Plugins"
$archivePath = Join-Path $artifactsRoot "$archiveBase.zip"
$sourceDll = Join-Path $buildRoot $pluginFile
$sourcePdb = Join-Path $buildRoot $pdbFile
$sourceIni = Join-Path $distRoot $iniFile

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

if ($IncludePdb -and -not (Test-Path -LiteralPath $sourcePdb)) {
    throw "Requested PDB packaging, but '$sourcePdb' does not exist."
}

if (Test-Path -LiteralPath $stageRoot) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}

if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

New-Item -ItemType Directory -Path $stagePluginRoot -Force | Out-Null

Copy-Item -LiteralPath $sourceDll -Destination (Join-Path $stagePluginRoot $pluginFile) -Force
Copy-Item -LiteralPath $sourceIni -Destination (Join-Path $stagePluginRoot $iniFile) -Force

if ($IncludePdb) {
    Copy-Item -LiteralPath $sourcePdb -Destination (Join-Path $stagePluginRoot $pdbFile) -Force
}

Compress-Archive -Path (Join-Path $stageModRoot "*") -DestinationPath $archivePath -CompressionLevel Optimal

if (-not $Quiet) {
    Write-Host "Packaged Native EditorID Fix VR:" -ForegroundColor Cyan
    Write-Host "  mode:         $Mode"
    Write-Host "  version:      $Version"
    if ($versionIsPlaceholder -and $commitSuffix) {
        Write-Host "  traceability: $commitSuffix"
    }
    Write-Host "  stage root:   $stageModRoot"
    Write-Host "  archive:      $archivePath"
}
