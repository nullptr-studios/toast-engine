<# PowerShell equivalent of tools/ci/clang_tidy.sh. #>
param([string]$BuildDir = "build/clang-tidy")

$ErrorActionPreference = "Stop"
Set-Location (Resolve-Path (Join-Path $PSScriptRoot "..\.."))

if (-not (Test-Path (Join-Path $BuildDir "compile_commands.json"))) {
    Write-Error "compile_commands.json not found in '$BuildDir'. Pass the build directory as the first argument."
    exit 1
}

$files = Get-ChildItem engine -Recurse -Filter *.cpp -File | Where-Object {
    $_.FullName -notmatch "[\\/]engine[\\/](generated|external|ffi)([\\/]|$)"
} | Sort-Object FullName

if ($files.Count -eq 0) { Write-Output "No files matched."; exit 0 }

Write-Output "Checking $($files.Count) files in one clang-tidy invocation..."
$clangTidy = (Get-Command clang-tidy -ErrorAction Stop).Source
& $clangTidy -p $BuildDir '--warnings-as-errors=*' '--header-filter=engine/(src|include)/.*' @($files.FullName)
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "Style check passed!"
