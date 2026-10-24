[CmdletBinding()]
param(
    [switch]$ValidateOnly,
    [switch]$ManifestOnly,
    [switch]$PackageOnly,
    [switch]$SealApprovedCandidate,
    [string]$GameExecutableFixture,
    [string]$ExpectedSourceManifestSha256,
    [string]$ExpectedTestedDllSha256,
    [string]$ApprovedRunRoot,
    [string]$ExpectedAcceptedBuildScriptSha256
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (($ValidateOnly -and $ManifestOnly) -or (($ValidateOnly -or $ManifestOnly) -and $PackageOnly)) {
    throw 'Choose only one of -ValidateOnly, -ManifestOnly, or -PackageOnly.'
}
if ($SealApprovedCandidate -and ($ValidateOnly -or $ManifestOnly)) {
    throw 'Sealing is available only after a full build/test run or an approved package-only reuse run.'
}
if ($PackageOnly -and -not [string]::IsNullOrWhiteSpace($GameExecutableFixture)) {
    throw '-PackageOnly reuses an existing verified test run; omit -GameExecutableFixture.'
}

$CandidateRoot = [IO.Path]::GetFullPath((Split-Path -Parent $MyInvocation.MyCommand.Path))
$BuildScriptPath = [IO.Path]::GetFullPath($MyInvocation.MyCommand.Path)
$SourceRoot = Join-Path $CandidateRoot 'source'
$PackageRoot = Join-Path $CandidateRoot 'package'
$OutputRoot = 'E:\Build\minimap-creative-compat-20261018'
$RuntimeReadmePath = Join-Path $CandidateRoot 'RUNTIME-README.txt'
$TestResultsPath = Join-Path $OutputRoot 'TEST-RESULTS.md'
$Version = '0.5.12-local-r4'
$ModId = 'minimap_mod'
$DisplayName = 'Sparkweave Minimap'
$ExpectedSettingsHash = '8b5480d94a090e81cb162e2a2c2d76d1cdb1bd6e3316b766bd695e3d91d18ffe'
$ExpectedInheritedPackageDllHash = '9b2f5646a5332798cf8d20098d73b41fcc3341822008ac9a962a23f2ecfb0b73'
$ExpectedGlslangHash = '9643709c02c83e26773f96b6a87f197ab2e84f78be0879ad8fb3619418b5d9ad'
$ExpectedLegacyEsmLogoHash = '320ed017d51c4cc3a63de62bd6d9dcb66d1125c7a28ad5f477a53133030807f4'
$ExpectedBrandKitVersion = '2.0'
$FixtureManifestPath = Join-Path $CandidateRoot 'BUILD-MANIFEST.json'
$FixtureManifest = Get-Content -LiteralPath $FixtureManifestPath -Raw | ConvertFrom-Json
if ($FixtureManifest.schemaVersion -ne 1) {
    throw 'Unsupported BUILD-MANIFEST.json schema version.'
}
$FixtureDefinitions = @($FixtureManifest.fixtures | Where-Object { $_.parameter -eq 'GameExecutableFixture' })
if ($FixtureDefinitions.Count -ne 1) {
    throw 'BUILD-MANIFEST.json must define exactly one GameExecutableFixture input.'
}
$ExpectedGameExecutableFixtureHash = [string]$FixtureDefinitions[0].sha256
$FixtureTestSpecs = @($FixtureManifest.tests)
$FixtureTestNames = @($FixtureTestSpecs | ForEach-Object { [string]$_.project })
if ($ExpectedGameExecutableFixtureHash -notmatch '^[0-9a-fA-F]{64}$' -or $FixtureTestNames.Count -lt 1) {
    throw 'BUILD-MANIFEST.json has an unpinned fixture hash or no fixture-bound tests.'
}
$script:VSInstallPath = $null
$TerrainShaders = @(
    'embervale_minimap_terrain.vert',
    'embervale_minimap_terrain.frag'
)
$SourceManifestPath = Join-Path $CandidateRoot 'SOURCE-INPUT-MANIFEST.json'
$RequiredBaselineAssets = @(
    'embervale_fowr.bin',
    'embervale_pois.bin',
    'embervale_minimap_frame.vert.spv',
    'embervale_minimap_frame.frag.spv'
)

function Get-HashHex {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file does not exist: $Path"
    }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-CanonicalSettingsHash {
    param([Parameter(Mandatory = $true)]$Settings)
    $canonical = ConvertTo-Json -InputObject $Settings -Depth 100 -Compress
    $bytes = [Text.Encoding]::UTF8.GetBytes($canonical)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function Assert-Equal {
    param(
        [Parameter(Mandatory = $true)]$Expected,
        [Parameter(Mandatory = $true)]$Actual,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ($Expected -cne $Actual) {
        throw "$Description mismatch. Expected '$Expected'; got '$Actual'."
    }
}

function Resolve-GameExecutableFixture {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "The full build/test gate requires -GameExecutableFixture for $($FixtureTestNames -join ' and '). No tests were skipped or claimed passed; the game executable is not bundled or executed."
    }
    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "The supplied offline game executable fixture does not exist: $resolved"
    }
    $fixtureFile = Get-Item -LiteralPath $resolved
    if ($fixtureFile.Name -ine [string]$FixtureDefinitions[0].fileName) {
        throw "The supplied fixture filename must be '$($FixtureDefinitions[0].fileName)'."
    }
    if ($fixtureFile.Length -ne [long]$FixtureDefinitions[0].lengthBytes) {
        throw "The supplied offline fixture has unexpected size $($fixtureFile.Length) bytes."
    }
    $candidatePrefix = $CandidateRoot.TrimEnd('\') + '\'
    if ($resolved.StartsWith($candidatePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The game executable fixture must remain outside the candidate and will not be packaged.'
    }
    Assert-Equal -Expected $ExpectedGameExecutableFixtureHash -Actual (Get-HashHex -Path $resolved) -Description 'Offline game executable fixture SHA256'
    return $resolved
}

function Test-IsExcludedVerificationArtifact {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $normalized = $RelativePath.Replace('\', '/')
    $segments = $normalized.Split('/')
    $leaf = [IO.Path]::GetFileName($normalized)
    if ($segments -contains '.agent-churn-control') {
        return $true
    }
    if ($leaf -match '(?i)^churn-.*\.json(?:\.lock)?$') {
        return $true
    }
    return $false
}

function Test-IsExcludedSourceArchiveOutput {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $normalized = $RelativePath.Replace('\', '/')
    $segments = $normalized.Split('/')
    $leaf = [IO.Path]::GetFileName($normalized)
    if ($segments -contains '.agent-churn-control') {
        return $true
    }
    if ($leaf -match '(?i)^churn-.*\.(?:json|ndjson)(?:\.lock)?$' -or $leaf -match '(?i)^receipts\.ndjson(?:\.lock)?$') {
        return $true
    }
    if ($normalized -match '(?i)(?:^|/)tests/perf_verify/' -and
        $leaf -match '(?i)^gpu[-_].*\.(?:json|ndjson|log|csv|tsv|txt|out)$') {
        return $true
    }
    return $false
}

function Get-ExcludedSourceFile {
    param([Parameter(Mandatory = $true)][IO.FileInfo]$File)
    $relative = $File.FullName.Substring($SourceRoot.Length).TrimStart('\')
    if (Test-IsExcludedVerificationArtifact -RelativePath $relative) {
        return $true
    }
    $segments = $relative.Split('\')
    foreach ($segment in $segments) {
        if ($segment -in @('build', 'obj', 'bin', '.vs', '.git', 'logs', 'verification')) {
            return $true
        }
    }
    if ($File.Extension.ToLowerInvariant() -in @('.dll', '.exe', '.pdb', '.ilk', '.exp', '.lib', '.obj', '.tlog', '.recipe', '.log', '.pch', '.idb', '.ipch', '.lastbuildstate')) {
        return $true
    }
    return $false
}

function Copy-FilteredTree {
    param(
        [Parameter(Mandatory = $true)][string]$From,
        [Parameter(Mandatory = $true)][string]$To,
        [switch]$IsSourceTree,
        [switch]$ExcludeBaselineDll
    )
    New-Item -ItemType Directory -Force -Path $To | Out-Null
    $base = [IO.Path]::GetFullPath($From).TrimEnd('\')
    foreach ($file in Get-ChildItem -LiteralPath $base -Recurse -File) {
        $relative = $file.FullName.Substring($base.Length).TrimStart('\')
        if ($IsSourceTree -and ((Get-ExcludedSourceFile -File $file) -or (Test-IsExcludedSourceArchiveOutput -RelativePath $relative))) {
            continue
        }
        if ($ExcludeBaselineDll -and $relative -ieq 'mods\minimap_mod\minimap_mod.dll') {
            continue
        }
        if ($file.Extension.ToLowerInvariant() -in @('.exe', '.pdb', '.ilk', '.exp', '.obj', '.tlog', '.recipe', '.log')) {
            continue
        }
        $target = Join-Path $To $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
}

function Get-SourceInputManifest {
    $inputsByPath = [System.Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    foreach ($file in Get-ChildItem -LiteralPath $SourceRoot -Recurse -File) {
        if (Get-ExcludedSourceFile -File $file) {
            continue
        }
        $relative = $file.FullName.Substring($SourceRoot.Length).TrimStart('\').Replace('\', '/')
        $path = "source/$relative"
        $inputsByPath.Add($path, [ordered]@{
            path = $path
            lengthBytes = [long]$file.Length
            sha256 = Get-HashHex -Path $file.FullName
        })
    }

    foreach ($file in Get-ChildItem -LiteralPath $PackageRoot -Recurse -File) {
        $relative = $file.FullName.Substring($PackageRoot.Length).TrimStart('\')
        if ($relative -ieq 'mods\minimap_mod\minimap_mod.dll') {
            continue
        }
        if ($file.Extension.ToLowerInvariant() -in @('.exe', '.pdb', '.ilk', '.exp', '.obj', '.tlog', '.recipe', '.log')) {
            continue
        }
        $path = "package/$($relative.Replace('\', '/'))"
        $inputsByPath.Add($path, [ordered]@{
            path = $path
            lengthBytes = [long]$file.Length
            sha256 = Get-HashHex -Path $file.FullName
        })
    }

    $candidateInputs = @('BUILD-MANIFEST.json')
    foreach ($relative in $candidateInputs) {
        $filePath = Join-Path $CandidateRoot $relative
        $path = $relative.Replace('\', '/')
        $inputsByPath.Add($path, [ordered]@{
            path = $path
            lengthBytes = [long](Get-Item -LiteralPath $filePath).Length
            sha256 = Get-HashHex -Path $filePath
        })
    }

    $paths = [string[]]@($inputsByPath.Keys)
    [Array]::Sort($paths, [StringComparer]::Ordinal)
    $records = @()
    foreach ($path in $paths) {
        $records += $inputsByPath[$path]
    }

    $shaderRecords = @()
    foreach ($shaderName in $TerrainShaders) {
        $sourcePath = "source/assets/$shaderName"
        $spirvPath = "$sourcePath.spv"
        $sourceInput = $inputsByPath[$sourcePath]
        $spirvInput = $inputsByPath[$spirvPath]
        if (-not $sourceInput -or -not $spirvInput) {
            throw "Source input manifest is missing terrain shader input '$sourcePath' or '$spirvPath'."
        }
        $shaderRecords += [ordered]@{
            source = $sourcePath
            sourceSha256 = $sourceInput.sha256
            spirv = $spirvPath
            spirvSha256 = $spirvInput.sha256
        }
    }

    $document = [ordered]@{
        schemaVersion = 1
        version = $Version
        inputs = [object[]]$records
        shaders = [object[]]$shaderRecords
    }
    $json = ConvertTo-Json -InputObject $document -Depth 8 -Compress
    $bytes = [Text.Encoding]::UTF8.GetBytes($json)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $digest = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
    return [pscustomobject]@{ document = $document; json = $json; bytes = $bytes; sha256 = $digest }
}

function Write-SourceInputManifest {
    param([Parameter(Mandatory = $true)]$Manifest)
    [IO.File]::WriteAllBytes($SourceManifestPath, $Manifest.bytes)
}

function Assert-RunFileHash {
    param(
        [Parameter(Mandatory = $true)][string]$RunRoot,
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$ExpectedHash,
        [Parameter(Mandatory = $true)][string]$Description
    )
    if ([IO.Path]::IsPathRooted($RelativePath) -or $RelativePath -match '(^|[/\\])\.\.([/\\]|$)') {
        throw "$Description contains a path outside its approved run: $RelativePath"
    }
    $runBase = [IO.Path]::GetFullPath($RunRoot).TrimEnd('\') + '\'
    $normalizedPath = $RelativePath.Replace('/', '\')
    $path = [IO.Path]::GetFullPath((Join-Path $RunRoot $normalizedPath))
    if (-not $path.StartsWith($runBase, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description resolves outside its approved run: $RelativePath"
    }
    $file = Get-Item -LiteralPath $path
    if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description resolves to a reparse point: $RelativePath"
    }
    Assert-Equal -Expected $ExpectedHash -Actual (Get-HashHex -Path $path) -Description $Description
}

function Assert-ApprovedRun {
    param(
        [Parameter(Mandatory = $true)][string]$RunRoot,
        [Parameter(Mandatory = $true)][string]$SourceManifestSha256,
        [Parameter(Mandatory = $true)][string]$TestedDllSha256,
        [Parameter(Mandatory = $true)][string]$AcceptedBuildScriptSha256,
        [Parameter(Mandatory = $true)][string[]]$ExpectedTestNames,
        [Parameter(Mandatory = $true)][string]$FixtureSha256,
        [Parameter(Mandatory = $true)]$SourceManifest
    )
    $resolvedRoot = [IO.Path]::GetFullPath($RunRoot).TrimEnd('\')
    $outputBase = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\') + '\'
    if (-not $resolvedRoot.StartsWith($outputBase, [StringComparison]::OrdinalIgnoreCase)) {
        throw "The approved run must be inside '$OutputRoot'."
    }
    $runDirectory = Get-Item -LiteralPath $resolvedRoot
    if (($runDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'The approved run directory must not be a reparse point.'
    }
    $receiptPath = Join-Path $resolvedRoot 'BUILD-RECEIPT.json'
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) {
        throw "The approved run is missing BUILD-RECEIPT.json: $receiptPath"
    }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    Assert-Equal -Expected 'minimap-creative-compat-20261018-local-r4' -Actual $receipt.task -Description 'Approved run task ID'
    Assert-Equal -Expected $Version -Actual $receipt.version -Description 'Approved run candidate version'
    Assert-Equal -Expected $AcceptedBuildScriptSha256 -Actual $receipt.buildScriptSha256 -Description 'Accepted test harness SHA256'
    Assert-Equal -Expected $SourceManifestSha256 -Actual $receipt.sourceInputManifestSha256 -Description 'Accepted test run source input manifest SHA256'
    Assert-Equal -Expected $TestedDllSha256 -Actual $receipt.runtimeDllSha256 -Description 'Approved run DLL SHA256'
    Assert-Equal -Expected $ExpectedSettingsHash -Actual $receipt.settingsDefaultsSha256 -Description 'Approved run settings defaults SHA256'
    Assert-Equal -Expected $ExpectedGlslangHash -Actual $receipt.glslang.executableSha256 -Description 'Approved run glslang executable SHA256'
    Assert-Equal -Expected '16.6.0' -Actual $receipt.glslang.version -Description 'Approved run glslang version'
    Assert-RunFileHash -RunRoot $resolvedRoot -RelativePath 'SOURCE-INPUT-MANIFEST.json' -ExpectedHash $SourceManifestSha256 -Description 'Accepted run source input manifest file SHA256'

    $expectedTests = @($ExpectedTestNames | Sort-Object -CaseSensitive)
    $actualTests = @($receipt.nativeTests | ForEach-Object { [string]$_.name } | Sort-Object -CaseSensitive)
    if ($actualTests.Count -ne $expectedTests.Count) {
        throw "Approved run test count mismatch. Expected $($expectedTests.Count), got $($actualTests.Count)."
    }
    for ($index = 0; $index -lt $expectedTests.Count; $index++) {
        Assert-Equal -Expected $expectedTests[$index] -Actual $actualTests[$index] -Description 'Approved run native test project set'
    }
    foreach ($test in $receipt.nativeTests) {
        Assert-Equal -Expected 0 -Actual ([int]$test.exitCode) -Description "Approved native test '$($test.name)' exit code"
        if ($test.name -in $FixtureTestNames) {
            Assert-Equal -Expected $FixtureSha256 -Actual $test.fixtureSha256 -Description "Approved native test '$($test.name)' fixture SHA256"
        }
        Assert-RunFileHash -RunRoot $resolvedRoot -RelativePath ([string]$test.stdout) -ExpectedHash ([string]$test.stdoutSha256) -Description "Approved native test '$($test.name)' stdout SHA256"
        Assert-RunFileHash -RunRoot $resolvedRoot -RelativePath ([string]$test.stderr) -ExpectedHash ([string]$test.stderrSha256) -Description "Approved native test '$($test.name)' stderr SHA256"
    }

    $expectedBuildProjects = @('minimap_mod') + $ExpectedTestNames
    $actualBuildProjects = @($receipt.projectBuilds | ForEach-Object { [string]$_.project } | Sort-Object -CaseSensitive)
    $expectedBuildProjects = @($expectedBuildProjects | Sort-Object -CaseSensitive)
    if ($actualBuildProjects.Count -ne $expectedBuildProjects.Count) {
        throw "Approved run project build count mismatch. Expected $($expectedBuildProjects.Count), got $($actualBuildProjects.Count)."
    }
    for ($index = 0; $index -lt $expectedBuildProjects.Count; $index++) {
        Assert-Equal -Expected $expectedBuildProjects[$index] -Actual $actualBuildProjects[$index] -Description 'Approved run project build set'
    }
    foreach ($build in $receipt.projectBuilds) {
        Assert-Equal -Expected 0 -Actual ([int]$build.exitCode) -Description "Approved project '$($build.project)' build exit code"
        Assert-RunFileHash -RunRoot $resolvedRoot -RelativePath ([string]$build.log) -ExpectedHash ([string]$build.logSha256) -Description "Approved project '$($build.project)' build log SHA256"
    }

    $expectedShaders = @($TerrainShaders | Sort-Object -CaseSensitive)
    $actualShaders = @($receipt.glslang.shaderResults | ForEach-Object { [string]$_.source } | Sort-Object -CaseSensitive)
    if ($actualShaders.Count -ne $expectedShaders.Count) {
        throw "Approved run shader result count mismatch. Expected $($expectedShaders.Count), got $($actualShaders.Count)."
    }
    for ($index = 0; $index -lt $expectedShaders.Count; $index++) {
        Assert-Equal -Expected $expectedShaders[$index] -Actual $actualShaders[$index] -Description 'Approved run terrain shader set'
    }
    foreach ($shader in $receipt.glslang.shaderResults) {
        $expectedSourcePath = "source/assets/$($shader.source)"
        $inputShader = @($SourceManifest.document.shaders | Where-Object { $_.source -eq $expectedSourcePath })
        if ($inputShader.Count -ne 1) {
            throw "Approved run references unknown shader '$($shader.source)'."
        }
        Assert-Equal -Expected $inputShader[0].spirvSha256 -Actual $shader.sha256 -Description "Approved shader output '$($shader.source)' SHA256"
    }
    if ($receipt.glslang.shaderResults.Count -ne $TerrainShaders.Count) {
        throw 'Approved run does not contain verification results for every terrain shader.'
    }
    $perfVerifyPath = Join-Path $SourceRoot 'tests\perf_verify.ps1'
    if (Test-Path -LiteralPath $perfVerifyPath -PathType Leaf) {
        if ($receipt.perfVerify -is [string]) {
            throw 'Approved run is missing the perf_verify result required by the current candidate.'
        }
        Assert-Equal -Expected 0 -Actual ([int]$receipt.perfVerify.exitCode) -Description 'Approved perf_verify exit code'
        Assert-Equal -Expected (Get-HashHex -Path $perfVerifyPath) -Actual ([string]$receipt.perfVerify.scriptSha256) -Description 'Approved perf_verify source SHA256'
        Assert-RunFileHash -RunRoot $resolvedRoot -RelativePath ([string]$receipt.perfVerify.stdout) -ExpectedHash ([string]$receipt.perfVerify.stdoutSha256) -Description 'Approved perf_verify stdout SHA256'
        Assert-RunFileHash -RunRoot $resolvedRoot -RelativePath ([string]$receipt.perfVerify.stderr) -ExpectedHash ([string]$receipt.perfVerify.stderrSha256) -Description 'Approved perf_verify stderr SHA256'
    }
    elseif ($receipt.perfVerify -isnot [string] -or [string]$receipt.perfVerify -ne 'not present') {
        throw 'Approved run perf_verify result does not match the current candidate.'
    }
    return $receipt
}

function Assert-BaselineFilesPreserved {
    param(
        [Parameter(Mandatory = $true)][string]$Baseline,
        [Parameter(Mandatory = $true)][string]$Copy,
        [Parameter(Mandatory = $true)][string[]]$AllowedChanges
    )
    $base = [IO.Path]::GetFullPath($Baseline).TrimEnd('\')
    foreach ($file in Get-ChildItem -LiteralPath $base -Recurse -File) {
        $relative = $file.FullName.Substring($base.Length).TrimStart('\')
        if ($relative -in $AllowedChanges) {
            continue
        }
        $copyFile = Join-Path $Copy $relative
        if (-not (Test-Path -LiteralPath $copyFile -PathType Leaf)) {
            throw "Staging omitted baseline package file '$relative'."
        }
        Assert-Equal -Expected (Get-HashHex -Path $file.FullName) -Actual (Get-HashHex -Path $copyFile) -Description "Preserved baseline package file '$relative'"
    }
}

function Get-MSBuildPath {
    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    $installPath = $null
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $found = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
        if ($LASTEXITCODE -eq 0 -and $found) {
            $installPath = [string]$found
            $script:VSInstallPath = $installPath
        }
    }

    $candidates = @()
    if ($installPath) {
        $candidates += (Join-Path $installPath 'MSBuild\Current\Bin\MSBuild.exe')
    }
    $candidates += (Join-Path $programFilesX86 'Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe')
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            if (-not $script:VSInstallPath) {
                $script:VSInstallPath = Join-Path $programFilesX86 'Microsoft Visual Studio\2022\BuildTools'
            }
            return $candidate
        }
    }
    throw 'MSBuild for Visual Studio 2022 was not found. Install/use the existing C++ v143 Build Tools; this script does not install dependencies.'
}

function Invoke-MSBuild {
    param(
        [Parameter(Mandatory = $true)][string]$Project,
        [Parameter(Mandatory = $true)][string]$LogPath
    )
    $projectName = [IO.Path]::GetFileNameWithoutExtension($Project)
    if ($projectName -eq 'minimap_mod') {
        $outputDirectory = Join-Path $buildRoot 'x64\Release\mods'
        $intermediateDirectory = Join-Path $buildRoot 'obj\x64\Release'
    }
    else {
        $outputDirectory = Join-Path $buildRoot 'tests'
        $intermediateDirectory = Join-Path (Join-Path $buildRoot 'obj\tests') $projectName
    }
    New-Item -ItemType Directory -Force -Path $outputDirectory, $intermediateDirectory | Out-Null
    $arguments = @(
        $Project,
        '/nologo',
        '/m:1',
        '/t:Rebuild',
        '/p:Configuration=Release',
        '/p:Platform=x64',
        '/p:PlatformToolset=v143',
        "/p:OutDir=$outputDirectory\",
        "/p:IntDir=$intermediateDirectory\",
        "/flp:logfile=$LogPath;verbosity=minimal"
    )
    & $script:MSBuildPath @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE for '$Project'. See '$LogPath'."
    }
}

function Invoke-TestExecutable {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$LogDirectory,
        [Parameter(Mandatory = $true)][string]$Name,
        [string[]]$Arguments = @(),
        [string]$FixtureHash
    )
    $stdout = Join-Path $LogDirectory "$Name.stdout.log"
    $stderr = Join-Path $LogDirectory "$Name.stderr.log"
    $startParameters = @{
        FilePath = $Executable
        WorkingDirectory = $SourceRoot
        Wait = $true
        PassThru = $true
        NoNewWindow = $true
        RedirectStandardOutput = $stdout
        RedirectStandardError = $stderr
    }
    if ($Arguments.Count -gt 0) {
        $startParameters.ArgumentList = $Arguments
    }
    $process = Start-Process @startParameters
    if ($process.ExitCode -ne 0) {
        throw "Native test '$Name' failed with exit code $($process.ExitCode). See '$stdout' and '$stderr'."
    }
    return [ordered]@{
        name = $Name
        exitCode = $process.ExitCode
        fixtureSha256 = $FixtureHash
        stdout = "logs/$(Split-Path -Leaf $stdout)"
        stdoutSha256 = Get-HashHex -Path $stdout
        stderr = "logs/$(Split-Path -Leaf $stderr)"
        stderrSha256 = Get-HashHex -Path $stderr
    }
}

function Get-ProjectBuildEvidence {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectName,
        [Parameter(Mandatory = $true)][string]$LogPath
    )
    return [ordered]@{
        project = $ProjectName
        exitCode = 0
        log = "logs/$(Split-Path -Leaf $LogPath)"
        logSha256 = Get-HashHex -Path $LogPath
    }
}

function Assert-ZipHasEntries {
    param(
        [Parameter(Mandatory = $true)][string]$ZipPath,
        [Parameter(Mandatory = $true)][string[]]$RequiredEntries,
        [switch]$SourceArchive
    )
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $entries = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        foreach ($required in $RequiredEntries) {
            if ($required -notin $entries) {
                throw "Archive '$ZipPath' is missing '$required'."
            }
        }
        if ($SourceArchive) {
            if ($entries -contains 'package/mods/minimap_mod/minimap_mod.dll') {
                throw 'The source archive unexpectedly contains the inherited package DLL.'
            }
            $forbiddenEntries = @($entries | Where-Object {
                $_ -match '^(toolchain/|source/(build|obj|bin|\.vs|\.git|logs|verification)/)' -or
                $_ -match '\.(dll|exe|pdb|ilk|exp|lib|obj|tlog|recipe|log|pch|idb|ipch|lastbuildstate)$' -or
                ($_ -match '^source/' -and (Test-IsExcludedSourceArchiveOutput -RelativePath $_.Substring(7)))
            })
            if ($forbiddenEntries.Count -gt 0) {
                throw "The source archive contains excluded toolchain, build, or diagnostic files: $($forbiddenEntries -join ', ')"
            }
        }
    }
    finally {
        $archive.Dispose()
    }
}

function Assert-ZipEntryHash {
    param(
        [Parameter(Mandatory = $true)][string]$ZipPath,
        [Parameter(Mandatory = $true)][string]$EntryPath,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256
    )
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $matches = @($archive.Entries | Where-Object { $_.FullName.Replace('\', '/') -ceq $EntryPath })
        if ($matches.Count -ne 1) {
            throw "Archive '$ZipPath' must contain exactly one '$EntryPath' entry."
        }
        $stream = $matches[0].Open()
        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            $actualSha256 = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
        }
        finally {
            $sha.Dispose()
            $stream.Dispose()
        }
        Assert-Equal -Expected $ExpectedSha256.ToLowerInvariant() -Actual $actualSha256 -Description "Archive entry '$EntryPath' SHA256"
    }
    finally {
        $archive.Dispose()
    }
}

function Assert-ZipMatchesTree {
    param(
        [Parameter(Mandatory = $true)][string]$ZipPath,
        [Parameter(Mandatory = $true)][string]$TreePath
    )
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    $actual = [System.Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        foreach ($entry in $archive.Entries) {
            if ($entry.FullName.EndsWith('/')) {
                continue
            }
            $entryPath = $entry.FullName.Replace('\', '/')
            $stream = $entry.Open()
            try {
                $entryHash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
            }
            finally {
                $stream.Dispose()
            }
            $actual.Add($entryPath, $entryHash)
        }
    }
    finally {
        $sha.Dispose()
        $archive.Dispose()
    }
    $base = [IO.Path]::GetFullPath($TreePath).TrimEnd('\')
    $expectedByPath = [System.Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    foreach ($file in Get-ChildItem -LiteralPath $base -Recurse -File) {
        $relative = $file.FullName.Substring($base.Length).TrimStart('\').Replace('\', '/')
        $expectedByPath.Add($relative, (Get-HashHex -Path $file.FullName))
    }
    if ($actual.Count -ne $expectedByPath.Count) {
        throw "Archive entry count mismatch for '$ZipPath'. Expected $($expectedByPath.Count), got $($actual.Count)."
    }
    foreach ($entryPath in $expectedByPath.Keys) {
        if (-not $actual.ContainsKey($entryPath)) {
            throw "Archive '$ZipPath' is missing staged file '$entryPath'."
        }
        if ($actual[$entryPath] -cne $expectedByPath[$entryPath]) {
            throw "Archive content hash mismatch for '$ZipPath' at '$entryPath'."
        }
    }
}

function New-DeterministicZip {
    param(
        [Parameter(Mandatory = $true)][string]$TreePath,
        [Parameter(Mandatory = $true)][string]$ZipPath
    )
    Add-Type -AssemblyName System.IO.Compression
    $base = [IO.Path]::GetFullPath($TreePath).TrimEnd('\')
    $filesByPath = [System.Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    foreach ($file in Get-ChildItem -LiteralPath $base -Recurse -File) {
        $relative = $file.FullName.Substring($base.Length).TrimStart('\').Replace('\', '/')
        $filesByPath.Add($relative, $file.FullName)
    }
    $paths = [string[]]@($filesByPath.Keys)
    [Array]::Sort($paths, [StringComparer]::Ordinal)

    $output = [IO.File]::Open($ZipPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $archive = $null
    try {
        $archive = [IO.Compression.ZipArchive]::new($output, [IO.Compression.ZipArchiveMode]::Create, $false)
        $fixedTimestamp = [DateTimeOffset]::new(2000, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
        foreach ($relative in $paths) {
            $entry = $archive.CreateEntry($relative, [IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $fixedTimestamp
            $entry.ExternalAttributes = 0
            $input = [IO.File]::OpenRead($filesByPath[$relative])
            $entryStream = $entry.Open()
            try {
                $input.CopyTo($entryStream)
            }
            finally {
                $entryStream.Dispose()
                $input.Dispose()
            }
        }
    }
    finally {
        if ($archive) {
            $archive.Dispose()
        }
        $output.Dispose()
    }
}

Write-Host "Candidate: $CandidateRoot"
Write-Host "Source:    $SourceRoot"

if (-not (Test-Path -LiteralPath (Join-Path $SourceRoot 'minimap_mod.vcxproj') -PathType Leaf)) {
    throw 'Candidate source project is missing.'
}
if (-not (Test-Path -LiteralPath $PackageRoot -PathType Container)) {
    throw 'Candidate baseline package is missing.'
}

$metadataPath = Join-Path $PackageRoot 'mods\minimap_mod\mod.json'
$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
Assert-Equal -Expected $ModId -Actual $metadata.id -Description 'Loader package ID'
Assert-Equal -Expected $Version -Actual $metadata.version -Description 'Package version'
Assert-Equal -Expected $DisplayName -Actual $metadata.name -Description 'Display name'
Assert-Equal -Expected $ExpectedSettingsHash -Actual (Get-CanonicalSettingsHash -Settings $metadata.shroudtopia.default) -Description 'Settings defaults SHA256'

$baselineDll = Join-Path $PackageRoot 'mods\minimap_mod\minimap_mod.dll'
if (Test-Path -LiteralPath $baselineDll -PathType Leaf) {
    Assert-Equal -Expected $ExpectedInheritedPackageDllHash -Actual (Get-HashHex -Path $baselineDll) -Description 'Inherited package DLL identity SHA256 (stale input excluded from staging)'
}
else {
    Write-Host 'No inherited package DLL is present; the source build does not require one.'
}
foreach ($asset in $RequiredBaselineAssets) {
    $assetPath = Join-Path $PackageRoot "mods\minimap_mod\$asset"
    if (-not (Test-Path -LiteralPath $assetPath -PathType Leaf)) {
        throw "Required baseline runtime asset is missing: $assetPath"
    }
}

$shaderCompiler = Join-Path $CandidateRoot 'toolchain\glslang-16.6.0\bin\glslang.exe'
$shaderArchive = Join-Path $CandidateRoot 'toolchain\glslang-16.6.0-windows-x86_64-release.zip'
$shaderLicense = Join-Path $CandidateRoot 'toolchain\glslang-16.6.0\LICENSE.txt'
if (-not (Test-Path -LiteralPath $shaderCompiler -PathType Leaf) -or -not (Test-Path -LiteralPath $shaderLicense -PathType Leaf)) {
    throw "The pinned glslang 16.6.0 build tool is missing. See '$CandidateRoot\GLSLANG-PROVENANCE.md' for the official release, hashes, and local extraction path. The build script does not download or install tools."
}
Assert-Equal -Expected $ExpectedGlslangHash -Actual (Get-HashHex -Path $shaderCompiler) -Description 'Pinned glslang executable SHA256'
if (Test-Path -LiteralPath $shaderArchive -PathType Leaf) {
    Assert-Equal -Expected '82bf434e69b9bb4829de7e2b4bc2c5e7a7861e53d66cf75e5cc70f5f694a8d9b' -Actual (Get-HashHex -Path $shaderArchive) -Description 'Official glslang release archive SHA256'
}
Assert-Equal -Expected '17e70c676e1521ff3e4686f04a2053d93a7e28a33be8de7ec37ab0ff72feb677' -Actual (Get-HashHex -Path $shaderLicense) -Description 'glslang license SHA256'
$brandManifestPath = Join-Path $CandidateRoot 'branding\assets\manifest.json'
$brandManifest = Get-Content -LiteralPath $brandManifestPath -Raw | ConvertFrom-Json
Assert-Equal -Expected $ExpectedBrandKitVersion -Actual ([string]$brandManifest.kitVersion) -Description 'Sparkweave branding kit version'
$expectedBrandAssets = @(
    [ordered]@{ file = 'sparkweave-logo-dark-v1.png'; sha256 = '37078CFCD68ECA989683C667AE5A8838FBC89D501CC145DFADC3A0612AF6ECDA' },
    [ordered]@{ file = 'sparkweave-mods-banner-dark-v1.png'; sha256 = '504DD475FB7694D7C04612D4DA8810BE8FAF840AD0C246128A4B90F404D770A1' }
)
Assert-Equal -Expected $expectedBrandAssets.Count -Actual @($brandManifest.assets).Count -Description 'Sparkweave branded asset count'
foreach ($expectedAsset in $expectedBrandAssets) {
    $assetRecords = @($brandManifest.assets | Where-Object { $_.file -ceq $expectedAsset.file })
    if ($assetRecords.Count -ne 1) {
        throw "Brand asset manifest must contain exactly one '$($expectedAsset.file)' record."
    }
    Assert-Equal -Expected $expectedAsset.sha256 -Actual ([string]$assetRecords[0].sha256).ToUpperInvariant() -Description "Brand asset manifest SHA256 for $($expectedAsset.file)"
    $assetPath = Join-Path (Join-Path $CandidateRoot 'branding\assets') $expectedAsset.file
    Assert-Equal -Expected $expectedAsset.sha256 -Actual (Get-HashHex -Path $assetPath).ToUpperInvariant() -Description "Sparkweave branded asset SHA256 for $($expectedAsset.file)"
}
$brandProvenancePath = Join-Path $CandidateRoot 'branding\provenance.json'
$brandProvenance = Get-Content -LiteralPath $brandProvenancePath -Raw | ConvertFrom-Json
Assert-Equal -Expected $ExpectedBrandKitVersion -Actual ([string]$brandProvenance.kitVersion) -Description 'Sparkweave provenance kit version'
Assert-Equal -Expected 'assets/manifest.json' -Actual ([string]$brandProvenance.currentAssetsManifest) -Description 'Sparkweave asset provenance manifest path'
Assert-Equal -Expected $ExpectedLegacyEsmLogoHash -Actual (Get-HashHex -Path (Join-Path $CandidateRoot 'branding\esm-logo-dark-v1.png')) -Description 'Historical ESM logo SHA256'

$compilerVersion = (& $shaderCompiler --version 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or $compilerVersion -notmatch '16\.6\.0') {
    throw 'The pinned glslang executable did not report version 16.6.0.'
}

$testProjects = @(Get-ChildItem -LiteralPath (Join-Path $SourceRoot 'tests') -Filter '*.vcxproj' -File | Sort-Object Name)
if ($testProjects.Count -lt 8) {
    throw "Only $($testProjects.Count) native test projects were discovered; expected the complete current suite."
}
Write-Host "Discovered native test projects: $($testProjects.Count)"
foreach ($shaderName in $TerrainShaders) {
    $shaderSource = Join-Path $SourceRoot "assets\$shaderName"
    $checkedInSpv = "$shaderSource.spv"
    if (-not (Test-Path -LiteralPath $shaderSource -PathType Leaf) -or -not (Test-Path -LiteralPath $checkedInSpv -PathType Leaf)) {
        throw "Required terrain source/SPIR-V pair is missing for '$shaderName'."
    }
    $shaderBytes = [IO.File]::ReadAllBytes($checkedInSpv)
    if ($shaderBytes.Length -lt 20 -or [BitConverter]::ToUInt32($shaderBytes, 0) -ne 0x07230203) {
        throw "Checked-in shader asset is not a valid SPIR-V module: $checkedInSpv"
    }
}

if (-not $PackageOnly) {
    $msbuildPath = Get-MSBuildPath
    $msvcRoot = Join-Path $script:VSInstallPath 'VC\Tools\MSVC'
    $clCandidates = @(
        Get-ChildItem -LiteralPath $msvcRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object { [Version]$_.Name } -Descending |
            ForEach-Object { Join-Path $_.FullName 'bin\HostX64\x64\cl.exe' } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }
    )
    if ($clCandidates.Count -eq 0) {
        throw 'MSVC x64 compiler was not found under the selected Visual Studio installation.'
    }
    $script:MSBuildPath = $msbuildPath
    Write-Host "MSBuild:   $msbuildPath"
    Write-Host "MSVC v143: $($clCandidates[0])"
}

if ($ValidateOnly) {
    if (-not [string]::IsNullOrWhiteSpace($GameExecutableFixture)) {
        $GameExecutableFixture = Resolve-GameExecutableFixture -Path $GameExecutableFixture
        Write-Host "Offline test fixture validated by hash: $ExpectedGameExecutableFixtureHash"
    }
    Write-Host 'Candidate structure, IDs/defaults, baseline assets, pinned tool hashes, compiler version, and toolchain presence validated.'
    return
}

$availableFixtureTests = @($testProjects | Where-Object { [IO.Path]::GetFileNameWithoutExtension($_.Name) -in $FixtureTestNames })
if ($availableFixtureTests.Count -ne $FixtureTestNames.Count) {
    throw "The complete required fixture test set is not present. Expected: $($FixtureTestNames -join ', ')."
}
$sourceManifest = Get-SourceInputManifest
if ($ManifestOnly) {
    Write-SourceInputManifest -Manifest $sourceManifest
    Write-Host "Source input manifest: $SourceManifestPath"
    Write-Host "Source input manifest SHA256: $($sourceManifest.sha256)"
    return
}

$buildRoot = Join-Path $OutputRoot 'native-build'
$testNames = @($testProjects | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_.Name) })
$testResults = @()
$projectBuilds = @()
$shaderResults = @()
$perfVerifyResult = 'not present'
$reusedRunRoot = $null
$acceptedRunBuildScriptSha256 = $null
$acceptedRunReceiptSha256 = $null
$acceptedRunSourceManifestSha256 = $null

if ($PackageOnly) {
    if ($ExpectedSourceManifestSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw '-PackageOnly requires -ExpectedSourceManifestSha256 with 64 hexadecimal characters.'
    }
    if ($ExpectedTestedDllSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw '-PackageOnly requires -ExpectedTestedDllSha256 with 64 hexadecimal characters.'
    }
    if ([string]::IsNullOrWhiteSpace($ApprovedRunRoot)) {
        throw '-PackageOnly requires -ApprovedRunRoot pointing at the run containing the accepted build/test receipt and logs.'
    }
    if ($ExpectedAcceptedBuildScriptSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw '-PackageOnly requires -ExpectedAcceptedBuildScriptSha256 from the accepted build/test receipt.'
    }
    Assert-Equal -Expected $ExpectedSourceManifestSha256.ToLowerInvariant() -Actual $sourceManifest.sha256 -Description 'Current source input manifest SHA256'
    if (-not (Test-Path -LiteralPath $SourceManifestPath -PathType Leaf)) {
        throw "-PackageOnly requires the saved source input manifest created by the approved build: $SourceManifestPath"
    }
    Assert-Equal -Expected $ExpectedSourceManifestSha256.ToLowerInvariant() -Actual (Get-HashHex -Path $SourceManifestPath) -Description 'Saved source input manifest SHA256'
    $receipt = Assert-ApprovedRun -RunRoot $ApprovedRunRoot -SourceManifestSha256 $ExpectedSourceManifestSha256.ToLowerInvariant() -TestedDllSha256 $ExpectedTestedDllSha256.ToLowerInvariant() -AcceptedBuildScriptSha256 $ExpectedAcceptedBuildScriptSha256.ToLowerInvariant() -ExpectedTestNames $testNames -FixtureSha256 $ExpectedGameExecutableFixtureHash -SourceManifest $sourceManifest
    $acceptedRunBuildScriptSha256 = [string]$receipt.buildScriptSha256
    $acceptedRunSourceManifestSha256 = [string]$receipt.sourceInputManifestSha256
    $acceptedRunReceiptSha256 = Get-HashHex -Path (Join-Path $ApprovedRunRoot 'BUILD-RECEIPT.json')
    $builtDll = Join-Path $buildRoot 'x64\Release\mods\minimap_mod.dll'
    Assert-Equal -Expected $ExpectedTestedDllSha256.ToLowerInvariant() -Actual (Get-HashHex -Path $builtDll) -Description 'Reused tested DLL SHA256'
    $testResults = @($receipt.nativeTests)
    $projectBuilds = @($receipt.projectBuilds)
    $shaderResults = @($receipt.glslang.shaderResults)
    $perfVerifyResult = $receipt.perfVerify
    $msbuildPath = [string]$receipt.msbuild
    $clCandidates = @([string]$receipt.msvc)
    $reusedRunRoot = [IO.Path]::GetFullPath($ApprovedRunRoot)
    Write-Host "Reusing accepted build/test evidence: $reusedRunRoot"
}
else {
    Write-SourceInputManifest -Manifest $sourceManifest
    $GameExecutableFixture = Resolve-GameExecutableFixture -Path $GameExecutableFixture
    $script:GameExecutableFixtureHash = $ExpectedGameExecutableFixtureHash

    $outputParent = Split-Path -Parent $OutputRoot
    New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
    New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
    $runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $runRoot = Join-Path $OutputRoot "run-$runId"
    New-Item -ItemType Directory -Path $runRoot | Out-Null
    $logRoot = Join-Path $runRoot 'logs'
    New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
    Copy-Item -LiteralPath $SourceManifestPath -Destination (Join-Path $runRoot 'SOURCE-INPUT-MANIFEST.json')

    $generatedShaderRoot = Join-Path $buildRoot 'shader-outputs'
    New-Item -ItemType Directory -Force -Path $generatedShaderRoot | Out-Null
    foreach ($shaderName in $TerrainShaders) {
        $shaderSource = Join-Path $SourceRoot "assets\$shaderName"
        $generatedSpv = Join-Path $generatedShaderRoot "$shaderName.spv"
        $checkedInSpv = "$shaderSource.spv"
        $shaderStdout = Join-Path $generatedShaderRoot "$shaderName.compiler.stdout.log"
        $shaderStderr = Join-Path $generatedShaderRoot "$shaderName.compiler.stderr.log"
        if (-not (Test-Path -LiteralPath $shaderSource -PathType Leaf)) {
            throw "Terrain shader source is missing: $shaderSource"
        }
        $shaderArgs = @('-V', '--target-env', 'vulkan1.0', $shaderSource, '-o', $generatedSpv)
        $shaderProcess = Start-Process -FilePath $shaderCompiler -ArgumentList $shaderArgs -WorkingDirectory $SourceRoot -Wait -PassThru -NoNewWindow -RedirectStandardOutput $shaderStdout -RedirectStandardError $shaderStderr
        if ($shaderProcess.ExitCode -ne 0) {
            throw "glslang failed for '$shaderName' with exit code $($shaderProcess.ExitCode). See '$shaderStdout' and '$shaderStderr'."
        }
        $spvBytes = [IO.File]::ReadAllBytes($generatedSpv)
        if ($spvBytes.Length -lt 20 -or [BitConverter]::ToUInt32($spvBytes, 0) -ne 0x07230203) {
            throw "Generated shader is not a valid SPIR-V module: $generatedSpv"
        }
        Assert-Equal -Expected (Get-HashHex -Path $generatedSpv) -Actual (Get-HashHex -Path $checkedInSpv) -Description "Checked-in SPIR-V output for $shaderName"
        $shaderResults += [ordered]@{ source = $shaderName; spirv = "$shaderName.spv"; sha256 = (Get-HashHex -Path $generatedSpv) }
    }

    $buildLog = Join-Path $logRoot 'minimap_mod.msbuild.log'
    Invoke-MSBuild -Project (Join-Path $SourceRoot 'minimap_mod.vcxproj') -LogPath $buildLog
    $projectBuilds += Get-ProjectBuildEvidence -ProjectName 'minimap_mod' -LogPath $buildLog
    $builtDll = Join-Path $buildRoot 'x64\Release\mods\minimap_mod.dll'
    if (-not (Test-Path -LiteralPath $builtDll -PathType Leaf)) {
        throw "Native build succeeded but the expected mod DLL was not produced: $builtDll"
    }
    if ((Get-HashHex -Path $builtDll) -eq $ExpectedInheritedPackageDllHash) {
        throw 'Built DLL unexpectedly matches the stale inherited package DLL identity; refuse to package it.'
    }

    foreach ($project in $testProjects) {
        $name = [IO.Path]::GetFileNameWithoutExtension($project.Name)
        $projectLog = Join-Path $logRoot "$name.msbuild.log"
        Invoke-MSBuild -Project $project.FullName -LogPath $projectLog
        $projectBuilds += Get-ProjectBuildEvidence -ProjectName $name -LogPath $projectLog
        $testExecutable = Join-Path $buildRoot "tests\$name.exe"
        if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
            throw "Test project '$name' built without its expected executable: $testExecutable"
        }
        $testArguments = @()
        $fixtureHash = $null
        if ($name -in $FixtureTestNames) {
            $testSpec = @($FixtureTestSpecs | Where-Object { $_.project -eq $name })[0]
            $fixtureArgs = @()
            foreach ($argumentSpec in $testSpec.arguments) {
                $fixture = @($FixtureDefinitions | Where-Object { $_.id -eq $argumentSpec.fixture })
                if ($fixture.Count -ne 1) {
                    throw "Test manifest references unknown fixture '$($argumentSpec.fixture)' for '$name'."
                }
                if ($fixture[0].parameter -ne 'GameExecutableFixture') {
                    throw "Unsupported fixture parameter '$($fixture[0].parameter)' for '$name'."
                }
                $fixtureArgs += "`"$GameExecutableFixture`""
            }
            $testArguments = $fixtureArgs
            $fixtureHash = $script:GameExecutableFixtureHash
        }
        $testResults += Invoke-TestExecutable -Executable $testExecutable -LogDirectory $logRoot -Name $name -Arguments $testArguments -FixtureHash $fixtureHash
    }

    $perfVerify = Join-Path $SourceRoot 'tests\perf_verify.ps1'
    if (Test-Path -LiteralPath $perfVerify -PathType Leaf) {
        $perfStdout = Join-Path $logRoot 'perf_verify.stdout.log'
        $perfStderr = Join-Path $logRoot 'perf_verify.stderr.log'
        $powershellExe = (Get-Process -Id $PID).Path
        $perfProcess = Start-Process -FilePath $powershellExe -ArgumentList @('-NoProfile', '-File', "`"$perfVerify`"") -WorkingDirectory $SourceRoot -Wait -PassThru -NoNewWindow -RedirectStandardOutput $perfStdout -RedirectStandardError $perfStderr
        if ($perfProcess.ExitCode -ne 0) {
            throw "Independent perf_verify harness failed with exit code $($perfProcess.ExitCode). See '$perfStdout' and '$perfStderr'."
        }
        $perfVerifyResult = [ordered]@{
            path = 'source/tests/perf_verify.ps1'
            scriptSha256 = Get-HashHex -Path $perfVerify
            exitCode = $perfProcess.ExitCode
            stdout = "logs/$(Split-Path -Leaf $perfStdout)"
            stdoutSha256 = Get-HashHex -Path $perfStdout
            stderr = "logs/$(Split-Path -Leaf $perfStderr)"
            stderrSha256 = Get-HashHex -Path $perfStderr
        }
    }
    $finalSourceManifest = Get-SourceInputManifest
    Assert-Equal -Expected $sourceManifest.sha256 -Actual $finalSourceManifest.sha256 -Description 'Source input manifest unchanged during build and tests'
}

if ($PackageOnly) {
    $outputParent = Split-Path -Parent $OutputRoot
    New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
    New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
    $runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $runRoot = Join-Path $OutputRoot "run-package-$runId"
    New-Item -ItemType Directory -Path $runRoot | Out-Null
    $logRoot = Join-Path $runRoot 'logs'
    New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
    Copy-Item -LiteralPath (Join-Path $reusedRunRoot 'SOURCE-INPUT-MANIFEST.json') -Destination (Join-Path $runRoot 'SOURCE-INPUT-MANIFEST.json')
    foreach ($test in $testResults) {
        foreach ($logPath in @([string]$test.stdout, [string]$test.stderr)) {
            $absoluteSource = Join-Path $reusedRunRoot $logPath.Replace('/', '\')
            Copy-Item -LiteralPath $absoluteSource -Destination $logRoot
        }
    }
    foreach ($build in $projectBuilds) {
        $absoluteSource = Join-Path $reusedRunRoot ([string]$build.log).Replace('/', '\')
        Copy-Item -LiteralPath $absoluteSource -Destination $logRoot
    }
    if ($perfVerifyResult -isnot [string]) {
        foreach ($logPath in @([string]$perfVerifyResult.stdout, [string]$perfVerifyResult.stderr)) {
            $absoluteSource = Join-Path $reusedRunRoot $logPath.Replace('/', '\')
            Copy-Item -LiteralPath $absoluteSource -Destination $logRoot
        }
    }
}

$runtimeStage = Join-Path $runRoot 'runtime-package'
Copy-FilteredTree -From $PackageRoot -To $runtimeStage -ExcludeBaselineDll
$runtimeReadmeFile = Get-Item -LiteralPath $RuntimeReadmePath
if (($runtimeReadmeFile.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'The output runtime README must not be a reparse point.'
}
Copy-Item -LiteralPath $RuntimeReadmePath -Destination (Join-Path $runtimeStage 'README.txt') -Force
Assert-BaselineFilesPreserved -Baseline $PackageRoot -Copy $runtimeStage -AllowedChanges @(
    'README.txt',
    'mods\minimap_mod\mod.json',
    'mods\minimap_mod\CREDITS.md',
    'mods\minimap_mod\minimap_mod.dll'
)
$runtimeMod = Join-Path $runtimeStage "mods\$ModId"
Copy-Item -LiteralPath $builtDll -Destination (Join-Path $runtimeMod "$ModId.dll")
foreach ($shaderName in $TerrainShaders) {
    Copy-Item -LiteralPath (Join-Path $SourceRoot "assets\$shaderName") -Destination $runtimeMod
    Copy-Item -LiteralPath (Join-Path $SourceRoot "assets\$shaderName.spv") -Destination $runtimeMod
}

foreach ($asset in $RequiredBaselineAssets) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtimeMod $asset) -PathType Leaf)) {
        throw "Staged package is missing required baseline asset '$asset'."
    }
}
foreach ($shaderName in $TerrainShaders) {
    foreach ($extension in @('', '.spv')) {
        $shaderAsset = "$shaderName$extension"
        if (-not (Test-Path -LiteralPath (Join-Path $runtimeMod $shaderAsset) -PathType Leaf)) {
            throw "Staged package is missing terrain shader asset '$shaderAsset'."
        }
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $runtimeMod 'LICENSE') -PathType Leaf) -or -not (Test-Path -LiteralPath (Join-Path $runtimeMod 'CREDITS.md') -PathType Leaf)) {
    throw 'Staged package is missing upstream license or credits.'
}
$stagedMetadata = Get-Content -LiteralPath (Join-Path $runtimeMod 'mod.json') -Raw | ConvertFrom-Json
Assert-Equal -Expected $ModId -Actual $stagedMetadata.id -Description 'Staged loader package ID'
Assert-Equal -Expected $Version -Actual $stagedMetadata.version -Description 'Staged package version'
Assert-Equal -Expected $DisplayName -Actual $stagedMetadata.name -Description 'Staged display name'
Assert-Equal -Expected $ExpectedSettingsHash -Actual (Get-CanonicalSettingsHash -Settings $stagedMetadata.shroudtopia.default) -Description 'Staged settings defaults SHA256'
Assert-Equal -Expected (Get-HashHex -Path $builtDll) -Actual (Get-HashHex -Path (Join-Path $runtimeMod "$ModId.dll")) -Description 'Staged runtime DLL SHA256'

$sourceStage = Join-Path $runRoot 'source-archive'
New-Item -ItemType Directory -Force -Path $sourceStage | Out-Null
Copy-FilteredTree -From $SourceRoot -To (Join-Path $sourceStage 'source') -IsSourceTree
Copy-FilteredTree -From $PackageRoot -To (Join-Path $sourceStage 'package') -ExcludeBaselineDll
Assert-BaselineFilesPreserved -Baseline $PackageRoot -Copy (Join-Path $sourceStage 'package') -AllowedChanges @(
    'README.txt',
    'mods\minimap_mod\mod.json',
    'mods\minimap_mod\CREDITS.md',
    'mods\minimap_mod\minimap_mod.dll'
)
foreach ($fileName in @('README.md', 'BUILD-TEST.md', 'BUILD-MANIFEST.json', 'SOURCE-INPUT-MANIFEST.json', 'BUILD.ps1')) {
    Copy-Item -LiteralPath (Join-Path $CandidateRoot $fileName) -Destination (Join-Path $sourceStage $fileName)
}
Copy-FilteredTree -From (Join-Path $CandidateRoot 'branding') -To (Join-Path $sourceStage 'branding')
Copy-Item -LiteralPath (Join-Path $CandidateRoot 'GLSLANG-PROVENANCE.md') -Destination (Join-Path $sourceStage 'GLSLANG-PROVENANCE.md')
Copy-Item -LiteralPath $RuntimeReadmePath -Destination (Join-Path $sourceStage 'RUNTIME-README.txt')

$receipt = [ordered]@{
    task = 'minimap-creative-compat-20261018-local-r4'
    version = $Version
    candidate = $CandidateRoot
    generatedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    buildScriptSha256 = Get-HashHex -Path $BuildScriptPath
    acceptedTestHarnessSha256 = $acceptedRunBuildScriptSha256
    acceptedBuildReceiptSha256 = $acceptedRunReceiptSha256
    acceptedTestSourceManifestSha256 = $acceptedRunSourceManifestSha256
    packagingNote = 'The source ZIP omits generated anti-churn and GPU result artifacts. The accepted build/test receipt, complete source input manifest, and tested DLL remain unchanged.'
    msbuild = $MSBuildPath
    msvc = $clCandidates[0]
    glslang = [ordered]@{ version = '16.6.0'; executableSha256 = $ExpectedGlslangHash; shaderResults = $shaderResults }
    sourceInputManifestSha256 = $sourceManifest.sha256
    sourceInputManifest = 'SOURCE-INPUT-MANIFEST.json'
    settingsDefaultsSha256 = $ExpectedSettingsHash
    nativeTestProjects = @($testProjects | ForEach-Object { $_.Name })
    projectBuilds = $projectBuilds
    nativeTests = $testResults
    perfVerify = $perfVerifyResult
    runtimeDllSha256 = (Get-HashHex -Path $builtDll)
    reusedApprovedRun = $reusedRunRoot
    runtimeAssets = @(Get-ChildItem -LiteralPath $runtimeStage -Recurse -File | ForEach-Object { $_.FullName.Substring($runtimeStage.Length).TrimStart('\') })
    liveGameAcceptance = 'not performed'
    fpsClaim = 'none'
    sealed = [bool]$SealApprovedCandidate
}
$receipt | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath (Join-Path $runRoot 'BUILD-RECEIPT.json') -Encoding utf8

if ($SealApprovedCandidate) {
    $packageZip = Join-Path $OutputRoot "Sparkweave-Minimap-$Version.zip"
    $sourceZip = Join-Path $OutputRoot "Sparkweave-Minimap-$Version-source.zip"
    $hashFile = Join-Path $OutputRoot 'SHA256SUMS.txt'
    foreach ($finalPath in @($packageZip, $sourceZip, $hashFile)) {
        if (Test-Path -LiteralPath $finalPath) {
            throw "Refusing to overwrite existing sealed artifact: $finalPath"
        }
    }
    if (-not (Test-Path -LiteralPath $TestResultsPath -PathType Leaf)) {
        throw "Sealing requires the final output-only test summary: $TestResultsPath"
    }
    New-DeterministicZip -TreePath $runtimeStage -ZipPath $packageZip
    New-DeterministicZip -TreePath $sourceStage -ZipPath $sourceZip
    Assert-ZipMatchesTree -ZipPath $packageZip -TreePath $runtimeStage
    Assert-ZipMatchesTree -ZipPath $sourceZip -TreePath $sourceStage
    Assert-ZipHasEntries -ZipPath $packageZip -RequiredEntries @(
        'mods/minimap_mod/minimap_mod.dll',
        'mods/minimap_mod/mod.json',
        'mods/minimap_mod/CREDITS.md',
        'mods/minimap_mod/LICENSE',
        'mods/minimap_mod/embervale_fowr.bin',
        'mods/minimap_mod/embervale_pois.bin',
        'mods/minimap_mod/embervale_minimap_terrain.vert',
        'mods/minimap_mod/embervale_minimap_terrain.frag',
        'mods/minimap_mod/embervale_minimap_terrain.vert.spv',
        'mods/minimap_mod/embervale_minimap_terrain.frag.spv'
    )
    Assert-ZipHasEntries -ZipPath $sourceZip -RequiredEntries @(
        'BUILD.ps1', 'BUILD-TEST.md', 'BUILD-MANIFEST.json', 'SOURCE-INPUT-MANIFEST.json', 'RUNTIME-README.txt', 'source/minimap_mod.vcxproj',
        'package/mods/minimap_mod/CREDITS.md', 'package/mods/minimap_mod/LICENSE',
        'branding/esm-logo-dark-v1.png', 'branding/provenance.json', 'branding/ASSET-PROVENANCE.md',
        'branding/assets/manifest.json', 'branding/assets/sparkweave-logo-dark-v1.png',
        'branding/assets/sparkweave-mods-banner-dark-v1.png', 'GLSLANG-PROVENANCE.md',
        'source/assets/embervale_minimap_terrain.vert.spv',
        'source/assets/embervale_minimap_terrain.frag.spv'
    ) -SourceArchive
    Assert-ZipEntryHash -ZipPath $packageZip -EntryPath 'mods/minimap_mod/minimap_mod.dll' -ExpectedSha256 (Get-HashHex -Path $builtDll)
    Copy-Item -LiteralPath (Join-Path $CandidateRoot 'BUILD-TEST.md') -Destination (Join-Path $OutputRoot 'BUILD-TEST.md')
    $hashLines = @(
        "$(Get-HashHex -Path $packageZip)  $(Split-Path -Leaf $packageZip)",
        "$(Get-HashHex -Path $sourceZip)  $(Split-Path -Leaf $sourceZip)",
        "$(Get-HashHex -Path $TestResultsPath)  $(Split-Path -Leaf $TestResultsPath)"
    )
    $hashLines | Set-Content -LiteralPath $hashFile -Encoding ascii
    Write-Host "Sealed local artifacts: $packageZip ; $sourceZip"
}
else {
    Write-Host 'Build, tests, and staging completed. No ZIPs were created; pass -SealApprovedCandidate only after explicit authorization for the frozen candidate.'
}

Write-Host "Run evidence: $runRoot"
