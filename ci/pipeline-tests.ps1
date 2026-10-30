$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$inventory = & "$PSScriptRoot/verify-source.ps1"
if ($inventory.version -cnotin @('0.5.15', '0.5.16', '0.5.16-gpu-reporter-test.2', '0.5.16-startup-fix-test.1', '0.5.16-marker-route-fix-test.1', '0.5.16-perf-glow-test.1', '0.5.17')) { throw 'Expected an approved Minimap pipeline version.' }
$version = $inventory.version

$metadata = Get-Content -LiteralPath "$root/assets/mod.json" -Raw | ConvertFrom-Json
if ($metadata.version -cne $version) { throw 'Mod metadata and source inventory versions differ.' }
if (-not (Select-String -LiteralPath "$root/src/dllmain.cpp" -Pattern ('"' + [regex]::Escape($version) + '"') -Quiet)) {
    throw 'Native ModInfo and source inventory versions differ.'
}

$workflow = Get-Content -LiteralPath "$root/.github/workflows/release.yml" -Raw
if ($workflow -notmatch '(?m)^\s+push:\s*$' -or
    $workflow -notmatch '(?m)^\s+branches:\s*\[main\]\s*$' -or
    $workflow -notmatch '(?m)^\s+pull_request:\s*$' -or
    $workflow -match '(?m)^\s+tags?:\s*') {
    throw 'GitHub Actions must build main and pull requests without a tag-triggered release.'
}
if ($workflow -notmatch 'actions/upload-artifact@v4' -or
    $workflow -notmatch 'dist/\*\.zip' -or
    $workflow -notmatch 'dist/BUILD-RECEIPT\.json') {
    throw 'GitHub Actions must upload the complete versioned ZIP and build receipt.'
}

$work = Join-Path $root "build/pipeline-check-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path "$work/ci" -Force | Out-Null
function Get-TreeHash([string]$Row) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { [Convert]::ToHexString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Row))).ToLowerInvariant() }
    finally { $sha.Dispose() }
}
try {
    [IO.File]::WriteAllText("$work/probe.txt", 'accepted')
    $probeHash = (Get-FileHash -LiteralPath "$work/probe.txt").Hash.ToLowerInvariant()
    $probeInventory = @{
        schemaVersion = 1
        nativePaths = @('probe.txt')
        acceptedNativeSourceTreeSha256 = Get-TreeHash "probe.txt`t$probeHash"
        files = @(@{ path = 'probe.txt'; lengthBytes = 8; sha256 = $probeHash })
    }
    $probeInventory | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$work/ci/source-inputs.json" -Encoding utf8
    $null = & "$PSScriptRoot/verify-source.ps1" -Root $work
    [IO.File]::WriteAllText("$work/probe.txt", 'tampered')
    $rejected = $false
    try { $null = & "$PSScriptRoot/verify-source.ps1" -Root $work }
    catch {
        if ($_.Exception.Message -notlike 'Source input differs*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Tampered source input was accepted.' }

    $probeInventory.files[0].sha256 = (Get-FileHash -LiteralPath "$work/probe.txt").Hash.ToLowerInvariant()
    $probeInventory | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$work/ci/source-inputs.json" -Encoding utf8
    $rejected = $false
    try { $null = & "$PSScriptRoot/verify-source.ps1" -Root $work }
    catch {
        if ($_.Exception.Message -notlike 'Native source tree digest differs*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'A stale aggregate source identity was accepted.' }

    # Package an isolated fixture to check archive layout without loading the fixture DLL.
    $fixture = Join-Path $work 'package-fixture'
    $fixtureFiles = @{
        'assets/mod.json' = (@{id='minimap_mod';version=$version} | ConvertTo-Json -Compress)
        'src/probe.cpp' = '// fixture only'
        'README.md' = "# Shroudweave Minimap $version"
        'README-INSTALL.txt' = 'fixture install instructions'
        'CREDITS.md' = 'fixture credits'
        'LICENSE' = 'fixture license'
        "docs/releases/v$version.md" = "# Shroudweave Minimap $version"
        'tests/README.md' = 'fixture test notes'
        'ci/unrelated.log' = 'must not ship'
    }
    $fixtureRecords = @()
    foreach ($relative in $fixtureFiles.Keys) {
        $target = Join-Path $fixture $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        [IO.File]::WriteAllText($target, $fixtureFiles[$relative])
        $fixtureRecords += @{
            path = $relative
            lengthBytes = (Get-Item -LiteralPath $target).Length
            sha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    $nativeHash = Get-TreeHash "src/probe.cpp`t$((Get-FileHash -LiteralPath "$fixture/src/probe.cpp").Hash.ToLowerInvariant())"
    New-Item -ItemType Directory -Path "$fixture/ci" -Force | Out-Null
    Copy-Item -LiteralPath "$root/package-release.ps1" -Destination $fixture
    $fixtureInventory = @{
        schemaVersion = 1
        version = $version
        nativePaths = @('src/probe.cpp')
        acceptedNativeSourceTreeSha256 = $nativeHash
        files = $fixtureRecords
    }
    $fixtureInventory | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$fixture/ci/source-inputs.json" -Encoding utf8
    $fixtureDll = Join-Path $work 'minimap_mod.dll'
    [IO.File]::WriteAllText($fixtureDll, 'fixture DLL; never loaded')

    & git -C $fixture init --quiet
    if ($LASTEXITCODE) { throw 'Could not initialize isolated package fixture repository.' }
    & git -C $fixture config user.name 'Pipeline Fixture'
    & git -C $fixture config user.email 'pipeline-fixture@example.invalid'
    & git -C $fixture add --all
    if ($LASTEXITCODE) { throw 'Could not stage isolated package fixture.' }
    & git -C $fixture commit --quiet -m 'Create isolated package fixture'
    if ($LASTEXITCODE) { throw 'Could not commit isolated package fixture.' }

    $incompleteOutput = Join-Path $work 'incomplete-package-output'
    New-Item -ItemType Directory -Path $incompleteOutput | Out-Null
    @{
        version = $version
        sourceCommit = (& git -C $fixture rev-parse HEAD).Trim()
        sourceInventorySha256 = (Get-FileHash -LiteralPath "$fixture/ci/source-inputs.json" -Algorithm SHA256).Hash.ToLowerInvariant()
        nativeSourceTreeSha256 = $nativeHash
        runtimeDllSha256 = (Get-FileHash -LiteralPath $fixtureDll -Algorithm SHA256).Hash.ToLowerInvariant()
        runtimeDllBytes = (Get-Item -LiteralPath $fixtureDll).Length
        fixtureTestsComplete = $false
        tests = @()
    } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $incompleteOutput 'BUILD-RECEIPT.json') -Encoding utf8
    $rejected = $false
    try { & "$fixture/package-release.ps1" -DllPath $fixtureDll -OutputDirectory $incompleteOutput }
    catch {
        if ($_.Exception.Message -notlike 'Build receipt does not account*') { throw }
        $rejected = $true
    }
    if (-not $rejected -or @(Get-ChildItem -LiteralPath $incompleteOutput -Filter '*.zip').Count) {
        throw 'An incomplete build receipt produced a package.'
    }

    $output = Join-Path $work 'package-output'
    New-Item -ItemType Directory -Path $output | Out-Null
    $fixtureDllHash = (Get-FileHash -LiteralPath $fixtureDll -Algorithm SHA256).Hash.ToLowerInvariant()
    $fixtureTests = @(
        @{ name = 'tracking_test'; status = 'PASSED' },
        @{ name = 'renderer_test'; status = 'PASSED' },
        @{ name = 'markers_test'; status = 'PASSED' },
        @{ name = 'map_features_test'; status = 'PASSED' },
        @{ name = 'client_1_0_test'; status = 'SKIPPED' },
        @{ name = 'frame_failure_test'; status = 'PASSED' },
        @{ name = 'hdr_test'; status = 'PASSED' },
        @{ name = 'patch_profile_test'; status = 'SKIPPED' },
        @{ name = 'menu_visibility_test'; status = 'SKIPPED' },
        @{ name = 'compat_copy_test'; status = 'PASSED' },
        @{ name = 'marker_summary_test'; status = 'PASSED' },
        @{ name = 'public_creative_compat_test'; status = 'SKIPPED' }
    )
    if ($version -cin @('0.5.16', '0.5.16-startup-fix-test.1', '0.5.16-marker-route-fix-test.1', '0.5.16-perf-glow-test.1', '0.5.17')) {
        $fixtureTests += @{ name = 'swapchain_startup_test'; status = 'PASSED' }
    }
    @{
        schema = 'shroudweave-minimap-actions-build-v1'
        version = $version
        sourceCommit = (& git -C $fixture rev-parse HEAD).Trim()
        sourceInventorySha256 = (Get-FileHash -LiteralPath "$fixture/ci/source-inputs.json" -Algorithm SHA256).Hash.ToLowerInvariant()
        nativeSourceTreeSha256 = $nativeHash
        runtimeDllSha256 = $fixtureDllHash
        runtimeDllBytes = (Get-Item -LiteralPath $fixtureDll).Length
        compilerProperties = @{ EnableMinimapBackgroundDiagnostics = $false; EnableMinimapPresentDecisionDiagnostics = $false }
        fixtureTestsComplete = $false
        tests = $fixtureTests
        publication = 'NOT PERFORMED; Actions artifact only'
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'BUILD-RECEIPT.json') -Encoding utf8
    & "$fixture/package-release.ps1" -DllPath $fixtureDll -OutputDirectory $output
    $zipPath = Join-Path $output "enshrouded-minimap-v$version.zip"
    if (-not (Test-Path -LiteralPath $zipPath -PathType Leaf)) { throw 'Versioned package archive was not created.' }
    $zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $entries = @($zip.Entries | ForEach-Object FullName)
        foreach ($required in @(
            'mods/minimap_mod/minimap_mod.dll',
            'mods/minimap_mod/mod.json',
            'README-INSTALL.txt',
            'CREDITS.md',
            'LICENSE',
            "docs/releases/v$version.md",
            'tests/README.md'
        )) {
            if ($required -notin $entries) { throw "Package is missing required entry: $required" }
        }
        if ($entries -contains 'ci/unrelated.log') { throw 'Unrelated source file was included in the package.' }
        $entry = $zip.GetEntry('mods/minimap_mod/mod.json')
        $reader = [IO.StreamReader]::new($entry.Open())
        try { $packagedMetadata = $reader.ReadToEnd() | ConvertFrom-Json }
        finally { $reader.Dispose() }
        if ($packagedMetadata.version -cne $version) { throw 'Packaged metadata version differs from source inventory.' }
    } finally { $zip.Dispose() }

    Write-Output "PASS: pinned source identity, tamper rejection, $version pipeline configuration, and complete standalone package layout."
} finally {
    $expectedPrefix = [IO.Path]::GetFullPath((Join-Path $root 'build')) + [IO.Path]::DirectorySeparatorChar
    if (-not [IO.Path]::GetFullPath($work).StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing cleanup outside the test build directory.'
    }
    Remove-Item -LiteralPath $work -Recurse -Force
}
