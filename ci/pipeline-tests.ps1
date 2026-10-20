$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$inventory = & "$PSScriptRoot/verify-source.ps1"
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
        schemaVersion = 1; version = '0.5.13'; nativePaths = @('probe.txt')
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
    if (-not $rejected) { throw 'Tampered source was accepted.' }
    $probeInventory.files[0].sha256 = (Get-FileHash -LiteralPath "$work/probe.txt").Hash.ToLowerInvariant()
    $probeInventory | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$work/ci/source-inputs.json" -Encoding utf8
    $rejected = $false
    try { $null = & "$PSScriptRoot/verify-source.ps1" -Root $work }
    catch {
        if ($_.Exception.Message -notlike 'Native source tree digest differs*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Stale aggregate source identity was accepted.' }

    [IO.File]::WriteAllText("$work/minimap_mod.dll", 'test fixture, never loaded')
    @{
        runtimeDllSha256 = (Get-FileHash -LiteralPath "$work/minimap_mod.dll").Hash.ToLowerInvariant()
        nativeSourceTreeSha256 = $inventory.acceptedNativeSourceTreeSha256
        fixtureTestsComplete = $true
        tests = @(@{ name = 'tracking_test'; status = 'PASSED' })
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$work/BUILD-RECEIPT.json" -Encoding utf8
    $rejected = $false
    try { & "$root/package-release.ps1" -DllPath "$work/minimap_mod.dll" -OutputDirectory $work }
    catch {
        if ($_.Exception.Message -notlike 'Build receipt does not account*') { throw }
        $rejected = $true
    }
    if (-not $rejected -or @(Get-ChildItem -LiteralPath $work -Filter '*.zip').Count) {
        throw 'Incomplete test evidence produced an archive.'
    }
    $allTests = @(Get-ChildItem -LiteralPath "$root/tests" -Filter '*.vcxproj' -File | ForEach-Object { @{ name = $_.BaseName; status = 'PASSED' } })
    foreach ($skipOne in @($false, $true)) {
        $allTests[0].status = $(if ($skipOne) { 'SKIPPED' } else { 'PASSED' })
        @{
            runtimeDllSha256 = (Get-FileHash -LiteralPath "$work/minimap_mod.dll").Hash.ToLowerInvariant()
            nativeSourceTreeSha256 = $inventory.acceptedNativeSourceTreeSha256
            fixtureTestsComplete = $skipOne
            tests = $allTests
        } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$work/BUILD-RECEIPT.json" -Encoding utf8
        $rejected = $false
        try { & "$root/package-release.ps1" -DllPath "$work/minimap_mod.dll" -OutputDirectory $work }
        catch {
            if ($_.Exception.Message -notlike 'Full release packaging requires*') { throw }
            $rejected = $true
        }
        if (-not $rejected -or @(Get-ChildItem -LiteralPath $work -Filter '*.zip').Count) { throw 'Incomplete fixture/skipped-test evidence produced an archive.' }
    }
    foreach ($badGame in @($null, "$work/probe.txt")) {
        $rejected = $false
        try { & "$PSScriptRoot/build.ps1" -GameExecutableFixture $badGame -CreativeZipFixture '' }
        catch {
            if ($_.Exception.Message -notlike 'Supply -GameExecutableFixture*' -and $_.Exception.Message -notlike 'Offline game fixture identity mismatch*') { throw }
            $rejected = $true
        }
        if (-not $rejected) { throw 'Missing or wrong game fixture reached the build.' }
    }
    # Small synthetic packaging tree; its DLL and project are never loaded or built.
    $fixture = Join-Path $work 'package-fixture'
    $fixtureFiles = @{
        'assets/mod.json' = '{"version":"0.5.13"}'
        'src/probe.cpp' = '// fixture'
        'tests/tracking_test.vcxproj' = '<Project />'
        'releases/0.5.13/package/mods/minimap_mod/mod.json' = '{"version":"0.5.13"}'
        'releases/0.5.13/RUNTIME-README.txt' = 'fixture readme'
        'releases/0.5.13/BUILD-MANIFEST.json' = '{"tests":[{"project":"fixture_only"}]}'
    }
    foreach ($kind in @('vert', 'frag', 'vert.spv', 'frag.spv')) {
        $fixtureFiles["assets/embervale_minimap_terrain.$kind"] = 'fixture shader'
    }
    $fixtureRecords = @()
    foreach ($relative in $fixtureFiles.Keys) {
        $target = Join-Path $fixture $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        [IO.File]::WriteAllText($target, $fixtureFiles[$relative])
        $fixtureRecords += @{
            path = $relative; lengthBytes = (Get-Item -LiteralPath $target).Length
            sha256 = (Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant()
        }
    }
    $nativeHash = Get-TreeHash "src/probe.cpp`t$((Get-FileHash -LiteralPath "$fixture/src/probe.cpp").Hash.ToLowerInvariant())"
    New-Item -ItemType Directory -Path "$fixture/ci", "$fixture/.github/workflows" -Force | Out-Null
    foreach ($name in @('package-release.ps1', 'build-release.ps1', 'README.md', 'BUILD-TEST.md', '.gitattributes')) {
        Copy-Item -LiteralPath "$root/$name" -Destination $fixture
    }
    Copy-Item -LiteralPath "$root/.github/workflows/release.yml" -Destination "$fixture/.github/workflows/release.yml"
    foreach ($name in @('build.ps1', 'verify-source.ps1', 'pipeline-tests.ps1', 'prepare-shader-compiler.ps1')) {
        Copy-Item -LiteralPath "$PSScriptRoot/$name" -Destination "$fixture/ci/$name"
    }
    @{
        schemaVersion = 1; version = '0.5.13'; nativePaths = @('src/probe.cpp')
        acceptedNativeSourceTreeSha256 = $nativeHash; files = $fixtureRecords
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$fixture/ci/source-inputs.json" -Encoding utf8
    [IO.File]::WriteAllText("$fixture/releases/0.5.13/package/mods/minimap_mod/unlisted.log", 'must not ship')
    [IO.File]::WriteAllText("$fixture/ci/unrelated.log", 'must not ship')
    $output = Join-Path $work 'package-output'
    New-Item -ItemType Directory -Path $output | Out-Null
    @{
        runtimeDllSha256 = (Get-FileHash -LiteralPath "$work/minimap_mod.dll").Hash.ToLowerInvariant()
        nativeSourceTreeSha256 = $nativeHash
        fixtureTestsComplete = $true
        tests = @(@{ name = 'tracking_test'; status = 'PASSED' })
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$output/BUILD-RECEIPT.json" -Encoding utf8
    & "$fixture/package-release.ps1" -DllPath "$work/minimap_mod.dll" -OutputDirectory $output
    foreach ($zipPath in Get-ChildItem -LiteralPath $output -Filter '*.zip' -File) {
        $zip = [IO.Compression.ZipFile]::OpenRead($zipPath.FullName)
        try {
            if (@($zip.Entries | Where-Object { $_.FullName -match '(unlisted|unrelated)\.log$' }).Count) {
                throw "Unpinned extra file shipped in $($zipPath.Name)"
            }
        } finally { $zip.Dispose() }
    }
    Write-Output 'PASS: source/tamper/stale-tree checks, missing/wrong fixture and incomplete/skipped-test receipt rejection, and unpinned runtime/source-file exclusion.'
} finally {
    $expectedPrefix = [IO.Path]::GetFullPath((Join-Path $root 'build')) + [IO.Path]::DirectorySeparatorChar
    if (-not [IO.Path]::GetFullPath($work).StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing cleanup outside the test build directory.'
    }
    Remove-Item -LiteralPath $work -Recurse -Force
}
