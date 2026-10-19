param([string]$Root = (Split-Path -Parent $PSScriptRoot))

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = [IO.Path]::GetFullPath($Root)
$manifest = Get-Content -LiteralPath (Join-Path $Root 'ci/source-inputs.json') -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1) { throw 'Unsupported source inventory.' }
foreach ($entry in $manifest.files) {
    $path = Join-Path $Root $entry.path
    $file = Get-Item -LiteralPath $path -ErrorAction Stop
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($file.Length -ne $entry.lengthBytes -or $hash -cne $entry.sha256) {
        throw "Source input differs from accepted 0.5.12: $($entry.path)"
    }
}
$nativePaths = [string[]]@($manifest.nativePaths)
[Array]::Sort($nativePaths, [StringComparer]::OrdinalIgnoreCase)
$nativeRows = foreach ($relative in $nativePaths) {
    $entry = @($manifest.files | Where-Object path -CEQ $relative)
    if ($entry.Count -ne 1) { throw "Native path must have exactly one verified input: $relative" }
    "$relative`t$($entry[0].sha256)"
}
$sha = [Security.Cryptography.SHA256]::Create()
try {
    $nativeHash = [Convert]::ToHexString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($nativeRows -join "`n"))).ToLowerInvariant()
} finally { $sha.Dispose() }
if ($nativeHash -cne $manifest.acceptedNativeSourceTreeSha256) { throw 'Native source tree digest differs from accepted inventory.' }
$manifest
