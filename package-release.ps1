param(
    [Parameter(Mandatory = $true)][string]$DllPath,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = $PSScriptRoot
$inventory = & "$root/ci/verify-source.ps1"
$receipt = Get-Content -LiteralPath (Join-Path $OutputDirectory 'BUILD-RECEIPT.json') -Raw | ConvertFrom-Json
$dll = Get-Item -LiteralPath $DllPath -ErrorAction Stop
$dllHash = (Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
if ($dll.Name -cne 'minimap_mod.dll' -or $dllHash -cne $receipt.runtimeDllSha256 -or
    $receipt.nativeSourceTreeSha256 -cne $inventory.acceptedNativeSourceTreeSha256) {
    throw 'Built DLL/source identity does not match the build receipt.'
}
$projects = @(Get-ChildItem -LiteralPath "$root/tests" -Filter '*.vcxproj' -File | ForEach-Object BaseName | Sort-Object)
$reported = @($receipt.tests.name | Sort-Object)
if (($projects -join ',') -cne ($reported -join ',') -or @($receipt.tests | Where-Object status -notin @('PASSED', 'SKIPPED')).Count) {
    throw 'Build receipt does not account for every native test project.'
}
$fixtureTests = @((Get-Content -LiteralPath "$root/releases/0.5.12/BUILD-MANIFEST.json" -Raw | ConvertFrom-Json).tests.project)
if (@($receipt.tests | Where-Object { $_.status -eq 'SKIPPED' -and $_.name -notin $fixtureTests }).Count) {
    throw 'Environment-independent native tests cannot be skipped.'
}
$version = $inventory.version
$metadata = Get-Content -LiteralPath "$root/assets/mod.json" -Raw | ConvertFrom-Json
if ($metadata.version -cne $version) { throw 'Package/source versions differ.' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$runtime = Join-Path $output 'runtime-stage'
$source = Join-Path $output 'source-stage'
$runtimeZip = Join-Path $output "Sparkweave-Minimap-$version.zip"
$sourceZip = Join-Path $output "Sparkweave-Minimap-$version-source.zip"
foreach ($path in @($runtime, $source, $runtimeZip, $sourceZip)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to replace existing staging/output: $path" }
}
New-Item -ItemType Directory -Path $runtime, $source | Out-Null
$runtimeExpected = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
$packagePrefix = 'releases/0.5.12/package/'
foreach ($entry in $inventory.files | Where-Object { $_.path.StartsWith("${packagePrefix}mods/", [StringComparison]::Ordinal) }) {
    $relative = $entry.path.Substring($packagePrefix.Length)
    $target = Join-Path $runtime $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $root $entry.path) -Destination $target
    $runtimeExpected.Add($relative, $entry.sha256)
}
Copy-Item -LiteralPath "$root/releases/0.5.12/RUNTIME-README.txt" -Destination "$runtime/README.txt"
$runtimeExpected.Add('README.txt', (Get-FileHash -LiteralPath "$root/releases/0.5.12/RUNTIME-README.txt").Hash.ToLowerInvariant())
$runtimeMod = Join-Path $runtime 'mods/minimap_mod'
Copy-Item -LiteralPath $dll.FullName -Destination "$runtimeMod/minimap_mod.dll"
$runtimeExpected.Add('mods/minimap_mod/minimap_mod.dll', $dllHash)
foreach ($name in @('embervale_minimap_terrain.vert', 'embervale_minimap_terrain.frag', 'embervale_minimap_terrain.vert.spv', 'embervale_minimap_terrain.frag.spv')) {
    Copy-Item -LiteralPath "$root/assets/$name" -Destination $runtimeMod
    $runtimeExpected.Add("mods/minimap_mod/$name", (Get-FileHash -LiteralPath "$root/assets/$name").Hash.ToLowerInvariant())
}
$runtimeFiles = @(Get-ChildItem -LiteralPath $runtime -Recurse -File)
if ($runtimeFiles.Count -ne $runtimeExpected.Count) { throw 'Runtime staging membership differs from permitted inputs.' }
foreach ($file in $runtimeFiles) {
    $relative = $file.FullName.Substring($runtime.Length + 1).Replace('\', '/')
    if (-not $runtimeExpected.ContainsKey($relative) -or
        (Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant() -cne $runtimeExpected[$relative]) {
        throw "Runtime staging contains an unverified entry: $relative"
    }
}
$sourcePaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($entry in $inventory.files) { [void]$sourcePaths.Add($entry.path) }
foreach ($relative in @('README.md', 'BUILD-TEST.md', 'build-release.ps1', 'package-release.ps1', '.gitattributes', '.github/workflows/release.yml')) {
    [void]$sourcePaths.Add($relative)
}
foreach ($name in @('build.ps1', 'verify-source.ps1', 'pipeline-tests.ps1', 'source-inputs.json')) { [void]$sourcePaths.Add("ci/$name") }
$sourceRecords = @()
foreach ($relative in $sourcePaths | Sort-Object -CaseSensitive) {
    $target = Join-Path $source $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $target
    $sourceRecords += [ordered]@{ path = $relative; sha256 = (Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() }
}
$sourceRecords | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$source/SOURCE-FILES.json" -Encoding utf8
Copy-Item -LiteralPath "$output/BUILD-RECEIPT.json" -Destination $source

function New-VerifiedZip([string]$Tree, [string]$ArchivePath) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $files = @(Get-ChildItem -LiteralPath $Tree -Recurse -File | Sort-Object FullName -CaseSensitive)
    $zip = [IO.Compression.ZipFile]::Open($ArchivePath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($Tree.Length + 1).Replace('\', '/')
            $entry = $zip.CreateEntry($relative, [IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = [DateTimeOffset]::new(2020, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
            $stream = $entry.Open()
            try { $bytes = [IO.File]::ReadAllBytes($file.FullName); $stream.Write($bytes, 0, $bytes.Length) }
            finally { $stream.Dispose() }
        }
    } finally { $zip.Dispose() }
    $zip = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        if ($zip.Entries.Count -ne $files.Count) { throw 'Archive membership differs from staging.' }
        foreach ($entry in $zip.Entries) {
            $stream = $entry.Open()
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $hash = [Convert]::ToHexString($sha.ComputeHash($stream)).ToLowerInvariant() }
            finally { $sha.Dispose(); $stream.Dispose() }
            if ($hash -cne (Get-FileHash -LiteralPath (Join-Path $Tree $entry.FullName)).Hash.ToLowerInvariant()) {
                throw "Archive entry differs from staging: $($entry.FullName)"
            }
        }
    } finally { $zip.Dispose() }
}
New-VerifiedZip $runtime $runtimeZip
New-VerifiedZip $source $sourceZip
$hashLines = foreach ($path in @($runtimeZip, $sourceZip, "$output/BUILD-RECEIPT.json")) {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant(), (Split-Path -Leaf $path)
}
$hashLines | Set-Content -LiteralPath "$output/SHA256SUMS.txt" -Encoding ascii
Write-Output "Candidate archives: $runtimeZip ; $sourceZip"
