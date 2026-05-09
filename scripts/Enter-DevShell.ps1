[CmdletBinding()]
param(
    [ValidateSet("amd64")]
    [string]$Arch = "amd64",

    [ValidateSet("amd64")]
    [string]$HostArch = "amd64",

    [switch]$Quiet
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-VsWherePath
{
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "Could not locate vswhere.exe."
}

function Get-VsInstallationPath
{
    $vswhere = Get-VsWherePath
    $installationPath = & $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath

    if (-not $installationPath) {
        throw "vswhere.exe did not return a Visual Studio installation path."
    }

    return $installationPath.Trim()
}

function Add-PathEntry
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$PathEntry
    )

    if (-not (Test-Path -LiteralPath $PathEntry)) {
        return
    }

    $pathEntries = @($env:Path -split ";")
    if ($pathEntries -notcontains $PathEntry) {
        $env:Path = "$PathEntry;$env:Path"
    }
}

$installationPath = Get-VsInstallationPath
$devShellModulePath = Join-Path $installationPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
$legacyDevShellModulePath = Join-Path $installationPath "Common7\Tools\vsdevshell\Microsoft.VisualStudio.DevShell.dll"

if (-not (Test-Path -LiteralPath $devShellModulePath)) {
    if (Test-Path -LiteralPath $legacyDevShellModulePath) {
        $devShellModulePath = $legacyDevShellModulePath
    } else {
        throw "Could not locate Microsoft.VisualStudio.DevShell.dll under '$installationPath'."
    }
}

Import-Module $devShellModulePath
Enter-VsDevShell -VsInstallPath $installationPath -SkipAutomaticLocation -Arch $Arch -HostArch $HostArch | Out-Null

$llvmBin = Join-Path $installationPath "VC\Tools\Llvm\x64\bin"
Add-PathEntry -PathEntry $llvmBin

if (-not $Quiet) {
    Write-Host "Activated Visual Studio developer shell:" -ForegroundColor Cyan
    Write-Host "  installation: $installationPath"
    Write-Host "  dev shell:    $devShellModulePath"
    Write-Host "  llvm bin:     $llvmBin"
    Write-Host "  note:         run 'xmake global --clean' after changing Visual Studio or Windows SDK installs"
    Write-Host ""

    Get-Command cl, clang, clang-cl, clang-format, xmake -ErrorAction SilentlyContinue |
        Select-Object Name, Source |
        Format-Table -AutoSize
}
