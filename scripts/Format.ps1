[CmdletBinding()]
param(
    [string[]]$Path = @("src", "tests"),

    [switch]$Check,

    [switch]$Quiet
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$enterDevShell = Join-Path $PSScriptRoot "Enter-DevShell.ps1"

if (-not (Get-Command clang-format -ErrorAction SilentlyContinue)) {
    & $enterDevShell -Quiet
}

$extensions = @("*.cpp", "*.cc", "*.cxx", "*.c", "*.h", "*.hh", "*.hpp", "*.hxx", "*.ipp", "*.inl")
$files = New-Object System.Collections.Generic.List[string]

Push-Location $repoRoot
try {
    foreach ($relativePath in $Path) {
        $targetPath = if ([System.IO.Path]::IsPathRooted($relativePath)) {
            $relativePath
        } else {
            Join-Path $repoRoot $relativePath
        }

        if (-not (Test-Path -LiteralPath $targetPath)) {
            continue
        }

        Get-ChildItem -LiteralPath $targetPath -Recurse -File -Include $extensions |
            Sort-Object FullName |
            ForEach-Object { [void]$files.Add($_.FullName) }
    }

    if ($files.Count -eq 0) {
        if (-not $Quiet) {
            Write-Host "No C/C++ source files matched the requested paths." -ForegroundColor Yellow
        }
        return
    }

    if (-not $Quiet) {
        Write-Host ($(if ($Check) { "Checking format" } else { "Formatting files" })) -ForegroundColor Cyan
        Write-Host "  files: $($files.Count)"
        Write-Host ""
    }

    $clangFormat = (Get-Command clang-format).Source
    foreach ($file in $files) {
        $quotedFile = '"' + $file.Replace('"', '\"') + '"'
        $argumentLine = if ($Check) {
            "--dry-run --Werror $quotedFile"
        } else {
            "-i $quotedFile"
        }

        $process = Start-Process -FilePath $clangFormat -ArgumentList $argumentLine -NoNewWindow -Wait -PassThru
        if ($process.ExitCode -ne 0) {
            throw "clang-format failed."
        }
    }
} finally {
    Pop-Location
}
