[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist'),
    [string]$GameExecutableFixture = $env:MINIMAP_GAME_FIXTURE,
    [string]$GlslangPath = $env:MINIMAP_GLSLANG,
    [string]$MSBuildPath = 'MSBuild',
    [string]$PythonPath = 'python'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $inventory = & "$PSScriptRoot/verify-source.ps1"
    if ($inventory.version -cne '0.5.15') { throw 'Pipeline is pinned to Minimap 0.5.15.' }
    $metadata = Get-Content -LiteralPath "$root/assets/mod.json" -Raw | ConvertFrom-Json
    if ($metadata.version -cne $inventory.version) { throw 'Manifest and source inventory versions differ.' }

    $fixture = $null
    if ($GameExecutableFixture) {
        $fixture = Get-Item -LiteralPath $GameExecutableFixture -ErrorAction Stop
        $expected = $inventory.fixtures | Where-Object id -CEQ 'client-1-0-executable'
        if (-not $expected -or $fixture.Length -ne $expected.lengthBytes -or
            (Get-FileHash -LiteralPath $fixture.FullName -Algorithm SHA256).Hash -ine $expected.sha256) {
            throw 'Offline game fixture identity mismatch.'
        }
    }

    $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
    if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output directory already exists; use a fresh path.' }
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null

    if (-not $GlslangPath) { throw 'Supply the pinned MINIMAP_GLSLANG compiler.' }
    $GlslangPath = (Get-Item -LiteralPath $GlslangPath -ErrorAction Stop).FullName
    if ((Get-FileHash -LiteralPath $GlslangPath -Algorithm SHA256).Hash -ine
        '9643709c02c83e26773f96b6a87f197ab2e84f78be0879ad8fb3619418b5d9ad') {
        throw 'Expected the pinned Khronos glslang 16.6.0 executable.'
    }
    foreach ($kind in @('vert', 'frag')) {
        $shader = "embervale_minimap_terrain.$kind"
        $generated = Join-Path $OutputDirectory "$shader.spv"
        & $GlslangPath -V --target-env vulkan1.0 "assets/$shader" -o $generated
        if ($LASTEXITCODE) { throw "Shader compilation failed: $shader" }
        if ((Get-FileHash -LiteralPath $generated).Hash -cne (Get-FileHash -LiteralPath "assets/$shader.spv").Hash) {
            throw "Fresh shader differs from the checked-in runtime asset: $shader"
        }
    }

    & $PythonPath "$root/tests/prepare_marker_summary_test.py"
    if ($LASTEXITCODE) { throw 'Could not regenerate the focused marker-summary test extraction.' }
    $expectedExtraction = @($inventory.files | Where-Object path -CEQ 'tests/marker_summary_extracted.inc')
    if ($expectedExtraction.Count -ne 1 -or
        (Get-FileHash -LiteralPath "$root/tests/marker_summary_extracted.inc" -Algorithm SHA256).Hash -ine $expectedExtraction[0].sha256) {
        throw 'Generated marker-summary test extraction differs from the reviewed source input.'
    }

    $common = @('/t:Rebuild', '/p:Configuration=Release', '/p:Platform=x64', '/p:PlatformToolset=v143', '/m', '/nologo')
    & $MSBuildPath ./minimap_mod.vcxproj @common
    if ($LASTEXITCODE) { throw 'Minimap production build failed.' }

    $projects = @(
        'tracking_test', 'renderer_test', 'markers_test', 'map_features_test',
        'client_1_0_test', 'frame_failure_test', 'hdr_test', 'patch_profile_test',
        'menu_visibility_test', 'compat_copy_test', 'marker_summary_test',
        'public_creative_compat_test'
    )
    $fixtureTests = @('client_1_0_test', 'patch_profile_test', 'menu_visibility_test', 'public_creative_compat_test')
    $results = @()
    foreach ($name in $projects) {
        $project = Join-Path $root "tests/$name.vcxproj"
        if (-not (Test-Path -LiteralPath $project)) { throw "Required test project missing: $name" }
        & $MSBuildPath $project @common
        if ($LASTEXITCODE) { throw "Test build failed: $name" }
        if ($name -in $fixtureTests -and -not $fixture) {
            $results += [ordered]@{ name = $name; status = 'SKIPPED'; reason = 'pinned offline game executable fixture unavailable' }
            continue
        }
        $test = Get-ChildItem -LiteralPath "$root/build" -Recurse -File -Filter "$name.exe" | Select-Object -First 1
        if (-not $test) { throw "Missing test executable: $name" }
        $arguments = if ($name -in $fixtureTests) { @($fixture.FullName) } else { @() }
        & $test.FullName @arguments
        if ($LASTEXITCODE) { throw "Test failed: $name" }
        $results += [ordered]@{ name = $name; status = 'PASSED' }
    }

    $dll = Join-Path $root 'build/x64/Release/mods/minimap_mod.dll'
    if (-not (Test-Path -LiteralPath $dll)) { throw 'Production DLL output missing.' }
    $dllHash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
    $commit = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE) { throw 'Cannot determine source commit.' }
    $dirty = @(& git status --porcelain --untracked-files=normal)
    if ($LASTEXITCODE -or $dirty.Count) { throw 'Refusing to package a dirty source checkout.' }

    $receipt = [ordered]@{
        schema = 'shroudweave-minimap-actions-build-v1'
        version = $inventory.version
        repository = 'https://github.com/Aerox912/Enshrouded-minimap'
        sourceCommit = $commit
        sourceInventorySha256 = (Get-FileHash -LiteralPath "$root/ci/source-inputs.json" -Algorithm SHA256).Hash.ToLowerInvariant()
        nativeSourceTreeSha256 = $inventory.acceptedNativeSourceTreeSha256
        runtimeDllSha256 = $dllHash
        runtimeDllBytes = (Get-Item -LiteralPath $dll).Length
        fixtureTestsComplete = [bool]$fixture
        tests = $results
        publication = 'NOT PERFORMED; Actions artifact only'
    }
    $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'BUILD-RECEIPT.json') -Encoding utf8
    & "$root/package-release.ps1" -DllPath $dll -OutputDirectory $OutputDirectory
    if ($LASTEXITCODE) { throw 'Versioned runtime package failed.' }
    $sumLines = foreach ($file in Get-ChildItem -LiteralPath $OutputDirectory -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object Name) {
        '{0}  {1}' -f (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $file.Name
    }
    $sumLines | Set-Content -LiteralPath (Join-Path $OutputDirectory 'SHA256SUMS.txt') -Encoding ascii
} finally { Pop-Location }
