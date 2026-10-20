[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist'),
    [string]$GameExecutableFixture = $env:MINIMAP_GAME_FIXTURE,
    [string]$CreativeZipFixture = $env:MINIMAP_CREATIVE_FIXTURE,
    [string]$GlslangPath = $env:MINIMAP_GLSLANG,
    [string]$MSBuildPath = 'MSBuild',
    [switch]$AllowMissingFixture
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$inventory = & "$PSScriptRoot/verify-source.ps1"
$fixtureManifest = Get-Content -LiteralPath "$root/releases/0.5.13/BUILD-MANIFEST.json" -Raw | ConvertFrom-Json
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
$creativeData = $null
if ($CreativeZipFixture) {
    if (-not $fixture) { throw 'Creative validation also requires the pinned game fixture.' }
    $creativeData = Join-Path (Split-Path -Parent $CreativeZipFixture) 'generated'
    & python "$root/tests/prepare_public_creative_fixture.py" --zip $CreativeZipFixture --out $creativeData
    if ($LASTEXITCODE) { throw 'Pinned Creative fixture preparation failed.' }
} elseif (-not $AllowMissingFixture) {
    throw 'Full release validation requires the pinned Creative ZIP fixture.'
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
$common = @('/t:Rebuild', '/p:Configuration=Release', '/p:Platform=x64', '/p:PlatformToolset=v143', '/p:WindowsTargetPlatformVersion=10.0.26100.0', '/m', '/nologo')
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
    & $MSBuildPath ./minimap_mod.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0.26100.0 /m /nologo
    if ($LASTEXITCODE) { throw 'Debug Minimap build failed.' }
    & $MSBuildPath ./minimap_mod.vcxproj @common
    if ($LASTEXITCODE) { throw 'Minimap build failed.' }
    foreach ($project in Get-ChildItem -LiteralPath "$root/tests" -Filter '*.vcxproj' -File | Sort-Object Name) {
        & $MSBuildPath $project.FullName @common "/p:OutDir=$root/build/tests/" "/p:IntDir=$root/build/obj/$($project.BaseName)/"
        if ($LASTEXITCODE) { throw "Test build failed: $($project.BaseName)" }
        $arguments = @()
        if ($project.BaseName -eq 'public_creative_compat_test') {
            if (-not $fixture -or -not $creativeData) {
                $testResults += [ordered]@{ name = $project.BaseName; status = 'SKIPPED'; reason = 'private fixtures unavailable; public checks only' }
                continue
            }
            $arguments = @($fixture.FullName, (Join-Path $creativeData 'public-module-data.bin'), (Join-Path $creativeData 'wrong-module-data.bin'))
        } elseif ($project.BaseName -in $fixtureTests) {
            if (-not $fixture) {
                Write-Warning "SKIPPED $($project.BaseName): offline game fixture unavailable."
                $testResults += [ordered]@{ name = $project.BaseName; status = 'SKIPPED'; reason = 'offline game fixture unavailable' }
                continue
            }
            $arguments = @($fixture.FullName)
        } elseif ($project.BaseName -eq 'terrain_test') {
            $arguments = @('--cpu-only')
        }
        & "$root/build/tests/$($project.BaseName).exe" @arguments
        if ($LASTEXITCODE) { throw "Test failed: $($project.BaseName)" }
        $testResults += [ordered]@{ name = $project.BaseName; status = 'PASSED' }
    }
    $dll = "$root/build/x64/Release/mods/minimap_mod.dll"
    $dllHash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
    $toolchainText = & $MSBuildPath ./minimap_mod.vcxproj /nologo /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 '-getProperty:VCToolsVersion,VCToolsInstallDir'
    if ($LASTEXITCODE) { throw 'Cannot record the actual compiler toolchain.' }
    $toolchain = ($toolchainText -join "`n") | ConvertFrom-Json
    $compiler = Get-Item -LiteralPath (Join-Path $toolchain.Properties.VCToolsInstallDir 'bin/Hostx64/x64/cl.exe')
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
        fixtureTestsComplete = [bool]($fixture -and $creativeData)
        runtimeDllSha256 = $dllHash
        acceptedRuntimeDllSha256 = $inventory.acceptedRuntimeDllSha256
        matchesAcceptedRuntime = ($dllHash -ceq $inventory.acceptedRuntimeDllSha256)
        offscreenGpuTest = 'NOT RUN: requires a graphics-capable Vulkan device'
        gameplay = 'NOT RUN'
        publication = 'NOT PERFORMED'
        repository = $env:GITHUB_REPOSITORY
        sourceRef = $env:GITHUB_REF
        workflowRun = $(if ($env:GITHUB_ACTIONS -eq 'true') { "https://github.com/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID" } else { $null })
        workflowRunAttempt = $env:GITHUB_RUN_ATTEMPT
        buildOrigin = $(if ($env:GITHUB_ACTIONS -eq 'true') { 'github-actions' } else { 'local-exact-source' })
        runnerImageVersion = $env:ImageVersion
        compilerFileVersion = $compiler.VersionInfo.FileVersion
        vcToolsVersion = $toolchain.Properties.VCToolsVersion
        windowsSdk = '10.0.26100.0'
    }
    $receipt | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath "$OutputDirectory/BUILD-RECEIPT.json" -Encoding utf8
    if (-not $receipt.fixtureTestsComplete) {
        Write-Warning 'Public checks only. Missing private fixture tests prevent all release packaging.'
        return
    }
    & "$root/package-release.ps1" -DllPath $dll -OutputDirectory $OutputDirectory
    if (-not $receipt.matchesAcceptedRuntime) {
        Write-Warning 'Built DLL differs from the accepted private predecessor. Artifacts require matching review; do not publish them.'
    }
} finally { Pop-Location }
