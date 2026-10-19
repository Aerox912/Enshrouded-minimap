[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist'),
    [string]$GameExecutableFixture = $env:MINIMAP_GAME_FIXTURE,
    [string]$GlslangPath = $env:MINIMAP_GLSLANG,
    [string]$MSBuildPath = 'MSBuild',
    [switch]$AllowMissingFixture
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$inventory = & "$PSScriptRoot/verify-source.ps1"
$fixtureManifest = Get-Content -LiteralPath "$root/releases/0.5.12/BUILD-MANIFEST.json" -Raw | ConvertFrom-Json
$fixtureTests = @($fixtureManifest.tests.project)
$fixture = $null
if ($GameExecutableFixture) {
    $fixture = Get-Item -LiteralPath $GameExecutableFixture -ErrorAction Stop
    $expected = $fixtureManifest.fixtures[0]
    if ($fixture.Length -ne $expected.lengthBytes -or
        (Get-FileHash -LiteralPath $fixture.FullName -Algorithm SHA256).Hash -ine $expected.sha256) {
        throw 'Offline game fixture identity mismatch.'
    }
} elseif (-not $AllowMissingFixture) {
    throw 'Supply -GameExecutableFixture, or explicitly use -AllowMissingFixture for partial CI verification.'
}
if (-not $GlslangPath) { $GlslangPath = "$root/toolchain/glslang-16.6.0/bin/glslang.exe" }
$GlslangPath = (Get-Item -LiteralPath $GlslangPath -ErrorAction Stop).FullName
if ((Get-FileHash -LiteralPath $GlslangPath -Algorithm SHA256).Hash -ine
    '9643709c02c83e26773f96b6a87f197ab2e84f78be0879ad8fb3619418b5d9ad') {
    throw 'Expected the pinned Khronos glslang 16.6.0 executable.'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output directory already exists; choose a new build output.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$testResults = @()
$common = @('/t:Rebuild', '/p:Configuration=Release', '/p:Platform=x64', '/p:PlatformToolset=v143', '/m', '/nologo')
Push-Location $root
try {
    foreach ($kind in @('vert', 'frag')) {
        $shader = "embervale_minimap_terrain.$kind"
        $generated = Join-Path $OutputDirectory "$shader.spv"
        & $GlslangPath -V --target-env vulkan1.0 "assets/$shader" -o $generated
        if ($LASTEXITCODE) { throw "Shader compilation failed: $shader" }
        if ((Get-FileHash -LiteralPath $generated).Hash -cne (Get-FileHash -LiteralPath "assets/$shader.spv").Hash) {
            throw "Fresh shader differs from accepted SPIR-V: $shader"
        }
    }
    & $MSBuildPath ./minimap_mod.vcxproj @common
    if ($LASTEXITCODE) { throw 'Minimap build failed.' }
    foreach ($project in Get-ChildItem -LiteralPath "$root/tests" -Filter '*.vcxproj' -File | Sort-Object Name) {
        & $MSBuildPath $project.FullName @common
        if ($LASTEXITCODE) { throw "Test build failed: $($project.BaseName)" }
        $arguments = @()
        if ($project.BaseName -in $fixtureTests) {
            if (-not $fixture) {
                Write-Warning "SKIPPED $($project.BaseName): offline game fixture unavailable."
                $testResults += [ordered]@{ name = $project.BaseName; status = 'SKIPPED'; reason = 'offline game fixture unavailable' }
                continue
            }
            $arguments = @($fixture.FullName)
        }
        & "$root/build/tests/$($project.BaseName).exe" @arguments
        if ($LASTEXITCODE) { throw "Test failed: $($project.BaseName)" }
        $testResults += [ordered]@{ name = $project.BaseName; status = 'PASSED' }
    }
    $dll = "$root/build/x64/Release/mods/minimap_mod.dll"
    $dllHash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
    $sourceCommit = $null
    $workingTreeDirty = $null
    if (Test-Path -LiteralPath "$root/.git") {
        $sourceCommit = (& git rev-parse HEAD).Trim()
        if ($LASTEXITCODE) { throw 'Cannot determine source commit.' }
        $workingTreeDirty = [bool]@(& git status --porcelain --untracked-files=normal)
        if ($LASTEXITCODE) { throw 'Cannot determine source working-tree state.' }
    }
    $receipt = [ordered]@{
        version = $inventory.version
        sourceCommit = $sourceCommit
        workingTreeDirty = $workingTreeDirty
        sourceInventorySha256 = (Get-FileHash -LiteralPath "$root/ci/source-inputs.json").Hash.ToLowerInvariant()
        nativeSourceTreeSha256 = $inventory.acceptedNativeSourceTreeSha256
        tests = $testResults
        fixtureTestsComplete = [bool]$fixture
        runtimeDllSha256 = $dllHash
        acceptedRuntimeDllSha256 = $inventory.acceptedRuntimeDllSha256
        matchesAcceptedRuntime = ($dllHash -ceq $inventory.acceptedRuntimeDllSha256)
        offscreenGpuTest = 'NOT RUN: requires a graphics-capable Vulkan device'
        gameplay = 'NOT RUN'
        publication = 'NOT PERFORMED'
    }
    $receipt | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath "$OutputDirectory/BUILD-RECEIPT.json" -Encoding utf8
    & "$root/package-release.ps1" -DllPath $dll -OutputDirectory $OutputDirectory
    if (-not $receipt.matchesAcceptedRuntime) {
        Write-Warning 'Built DLL differs from the accepted published DLL. Artifacts are candidates; do not replace or publish them.'
    }
} finally { Pop-Location }
