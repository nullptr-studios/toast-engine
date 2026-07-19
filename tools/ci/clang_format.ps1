<# PowerShell equivalent of tools/ci/clang_format.sh. #>
param()

$ErrorActionPreference = "Stop"
Set-Location (Resolve-Path (Join-Path $PSScriptRoot "..\.."))

$files = Get-ChildItem engine -Recurse -File | Where-Object {
    $_.Extension -in @(".cpp", ".hpp", ".h", ".inl") -and
    $_.FullName -notmatch "[\\/]engine[\\/](generated|external|ffi)([\\/]|$)"
} | Sort-Object FullName

if ($files.Count -eq 0) {
    Write-Output "No engine files matched formatting filters."
    exit 0
}

Write-Output "Formatting $($files.Count) files..."
$clangFormat = (Get-Command clang-format -ErrorAction Stop).Source
& $clangFormat -i --style=file:.clang-format @($files.FullName)
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$diff = git diff --quiet -- @($files.FullName)
if ($LASTEXITCODE -ne 0) {
    Write-Error "Formatting changes detected. Run clang-format locally and push the results."
    exit 1
}
Write-Output "Formatting check passed."
