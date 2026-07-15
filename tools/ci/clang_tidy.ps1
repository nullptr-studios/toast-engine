<#
.SYNOPSIS
    PowerShell port of tools/ci/clang_tidy.sh for local use on Windows.
.PARAMETER BuildDir
    Build directory containing compile_commands.json (default: build/clang-tidy).
#>
param(
    [string]$BuildDir = "build/clang-tidy"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $repoRoot

$compileCommands = Join-Path $BuildDir "compile_commands.json"
if (-not (Test-Path $compileCommands)) {
    Write-Error "compile_commands.json not found in '$BuildDir'. Pass the build directory as the first argument."
    exit 1
}

Write-Output "Running clang-tidy check..."

$excludeDirs = @(
    (Join-Path "engine" "external"),
    (Join-Path "engine" "generated"),
    (Join-Path "engine" "ffi")
)

$files = Get-ChildItem -Path "engine" -Recurse -Filter "*.cpp" -File | Where-Object {
    $rel = (Resolve-Path $_.FullName -Relative)
    -not ($excludeDirs | Where-Object { $rel -like "*\$_\*" })
} | ForEach-Object { $_.FullName }

if ($files.Count -eq 0) {
    Write-Output "No files matched."
    exit 0
}

$maxParallel = [Environment]::ProcessorCount
Write-Output "Checking $($files.Count) files with up to $maxParallel parallel workers..."

$clangTidy = (Get-Command clang-tidy -ErrorAction Stop).Source
$tempDir = [System.IO.Path]::GetTempPath()

$pending = [System.Collections.Generic.Queue[string]]::new()
$files | ForEach-Object { $pending.Enqueue($_) }

# {Process, File, LogFile}
$running = @()
$failedFiles = @()

function Start-ClangTidy($file) {
    # Redirect via cmd.exe to a real file instead of a .NET pipe: clang-tidy's combined
    # stdout+stderr for a noisy file can exceed the OS pipe buffer, and since we only drain
    # the pipe after polling HasExited, an unread full pipe would deadlock the child process.
    $logFile = Join-Path $tempDir ("clang-tidy-{0}.log" -f ([guid]::NewGuid()))
    # Every token that could contain shell-significant characters (spaces, parens) must be
    # individually quoted, and the whole thing wrapped in one more quote pair so cmd's /c
    # special-cases stripping it before parsing the trailing redirection.
    $cmdArgs = "/c `"`"$clangTidy`" -p $BuildDir `"$file`" `"--warnings-as-errors=*`" `"--header-filter=engine/(src|include)/.*`" > `"$logFile`" 2>&1`""
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = "cmd.exe"
    $psi.Arguments = $cmdArgs
    $psi.UseShellExecute = $false
    $proc = [System.Diagnostics.Process]::Start($psi)
    return [PSCustomObject]@{ Process = $proc; File = $file; LogFile = $logFile }
}

while ($pending.Count -gt 0 -or $running.Count -gt 0) {
    while ($running.Count -lt $maxParallel -and $pending.Count -gt 0) {
        $running += Start-ClangTidy $pending.Dequeue()
    }

    Start-Sleep -Milliseconds 100
    $stillRunning = @()
    foreach ($job in $running) {
        if ($job.Process.HasExited) {
            if ($job.Process.ExitCode -ne 0) {
                Write-Output "----- $($job.File) -----"
                if (Test-Path $job.LogFile) { Get-Content $job.LogFile | Write-Output }
                $failedFiles += $job.File
            }
            $job.Process.Dispose()
            Remove-Item $job.LogFile -ErrorAction SilentlyContinue
        } else {
            $stillRunning += $job
        }
    }
    $running = $stillRunning
}

if ($failedFiles.Count -gt 0) {
    Write-Output ""
    Write-Error "clang-tidy found issues in $($failedFiles.Count) file(s):`n$($failedFiles -join "`n")"
    exit 1
}

Write-Output "Style check passed!"
