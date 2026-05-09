[CmdletBinding()]
param(
    [ValidateSet("debug", "releasedbg")]
    [string]$Mode = "releasedbg",

    [switch]$Quiet
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$formatScript = Join-Path $PSScriptRoot "Format.ps1"
$buildScript = Join-Path $PSScriptRoot "Build.ps1"
$testScript = Join-Path $PSScriptRoot "Test.ps1"

& $formatScript -Check -Quiet:$Quiet
& $buildScript -Mode $Mode -Analyze -Quiet:$Quiet
& $testScript -Mode $Mode -Analyze -Quiet:$Quiet
