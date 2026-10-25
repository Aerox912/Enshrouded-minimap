param(
  [string]$OutputDirectory = 'E:\Build\minimap-gpu-arrow-glow-20261025\final',
  [string]$Dumpbin = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx86\x64\dumpbin.exe',
  [string]$BaselineDll = 'E:\Build\minimap-gpu-test4-build\minimap_mod.dll',
  [string]$FocusedTestReceipt = 'E:\Scratch\minimap-hollow-proof-arrow-glow-20261025\task-1-support\arrow-glow-full-focused-accepted-final.json',
  [string]$FocusedTestCandidateManifest = 'E:\Scratch\minimap-hollow-proof-arrow-glow-20261025\task-1-support\arrow-glow-full-focused-candidate-final.json',
  [string]$PinnedShaderReceipt = 'E:\Scratch\minimap-hollow-proof-arrow-glow-20261025\task-1-support\arrow-glow-pinned-shader-accepted-clean-2.json',
  [string]$PinnedShaderEvidence = 'E:\Scratch\minimap-hollow-proof-arrow-glow-20261025\task-1-support\arrow-glow-pinned-shader-evidence-clean-2.json',
  [string]$PinnedShaderCandidateManifest = 'E:\Scratch\minimap-hollow-proof-arrow-glow-20261025\task-1-support\arrow-glow-pinned-shader-candidate.json',
  [string]$PinnedShaderDependencies = 'E:\Scratch\minimap-hollow-proof-arrow-glow-20261025\task-1-support\arrow-glow-pinned-shader-dependencies.json',
  [string]$PinnedShaderOutputDirectory = 'E:\Build\minimap-gpu-arrow-glow-20261025\shader-pinned-clean-2',
  [switch]$ValidateDependencyParserOnly
)

$ErrorActionPreference = 'Stop'
$TaskId = 'MM-GPU-ARROW-GLOW-IMPL-20261025'
$ExpectedVersion = '0.5.16-gpu-arrow-glow-test.1'
$ExpectedGlslangSha256 = '9643709c02c83e26773f96b6a87f197ab2e84f78be0879ad8fb3619418b5d9ad'
$ExpectedVCToolsVersion = '14.44.35207'
$ExpectedWindowsSdkVersion = '10.0.26100.0'
$ExpectedMsBuildVersion = '17.14.51.32402'
$ExpectedBaseManifestSha256 = '282b8c3611b17f1b807afa948c9419d60f053db118ca31c7fd22705927ad2c05'
$LatestParserFix = 'https://github.com/Aerox912/Enshrouded-minimap/commit/92d96f459f1d6a05a64c21c4dfa2ce2cfc62caa8'
$Root = Split-Path -Parent $PSScriptRoot
$DependencyPattern = '^\s+([A-Za-z0-9_.-]+\.dll)\s*$'

function Get-ImageDependencyNames {
  param([Parameter(Mandatory = $true)][string[]]$Lines)

  $names = foreach ($line in $Lines) {
    if ($line -match $DependencyPattern) {
      $Matches[1].ToLowerInvariant()
    }
  }
  @($names | Sort-Object -Unique)
}

function Test-DependencyParserFixture {
  $fixture = @(
    'Dump of file minimap_mod.dll',
    'File Type: DLL',
    '  Image has the following dependencies:',
    '    KERNEL32.dll',
    '    USER32.dll',
    '    kernel32.dll',
    '  Summary'
  )
  $actual = @(Get-ImageDependencyNames -Lines $fixture)
  $expected = @('kernel32.dll', 'user32.dll')
  if (@(Compare-Object -ReferenceObject $expected -DifferenceObject $actual).Count -ne 0) {
    throw "Dependency parser fixture failed. Expected [$($expected -join ', ')], got [$($actual -join ', ')]."
  }
  Write-Output 'Dependency parser fixture: PASS (headers excluded; imports lowercased, sorted, unique).'
}

function Invoke-MSBuild {
  param(
    [Parameter(Mandatory = $true)][string]$Project,
    [Parameter(Mandatory = $true)][string]$OutDir,
    [Parameter(Mandatory = $true)][string]$IntDir,
    [Parameter(Mandatory = $true)][string]$LogPath
  )

  $vsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
  $msbuild = Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe'
  $vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvarsall.bat'
  if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) { throw "Missing vcvarsall.bat: $vcvars" }
  if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) { throw "Missing pinned x86 MSBuild: $msbuild" }

  $versionOutput = (& $msbuild -version -nologo 2>&1 | Out-String).Trim()
  if ($LASTEXITCODE -ne 0 -or $versionOutput -notmatch [regex]::Escape($ExpectedMsBuildVersion)) {
    throw "Expected x86 MSBuild $ExpectedMsBuildVersion; got '$versionOutput'."
  }

  $OutDir = $OutDir.TrimEnd('\')
  $IntDir = $IntDir.TrimEnd('\')
  $command = 'call "' + $vcvars + '" x64 -vcvars_ver=' + $ExpectedVCToolsVersion +
    ' >nul && "' + $msbuild + '" "' + $Project + '" /m /nologo' +
    ' /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143' +
    ' /p:VCToolsVersion=' + $ExpectedVCToolsVersion +
    ' /p:WindowsTargetPlatformVersion=' + $ExpectedWindowsSdkVersion +
    ' /p:OutDir=' + $OutDir + '\ /p:IntDir=' + $IntDir + '\ > "' + $LogPath + '" 2>&1'
  & $env:ComSpec /d /s /c $command
  $exitCode = $LASTEXITCODE
  if (Test-Path -LiteralPath $LogPath -PathType Leaf) { Get-Content -LiteralPath $LogPath }
  if ($exitCode -ne 0) { throw "MSBuild failed with exit $exitCode. See $LogPath" }
}

Test-DependencyParserFixture
if ($ValidateDependencyParserOnly) { exit 0 }

$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
if ($OutputDirectory -ne 'E:\Build\minimap-gpu-arrow-glow-20261025\final') {
  throw 'The private freeze output must be E:\Build\minimap-gpu-arrow-glow-20261025\final.'
}
if (Test-Path -LiteralPath $OutputDirectory) {
  throw "Freeze output already exists; use the retained result or select an approved new output path: $OutputDirectory"
}

$descriptorPath = Join-Path $Root 'assets\mod.json'
$descriptor = Get-Content -LiteralPath $descriptorPath -Raw | ConvertFrom-Json
if ($descriptor.version -cne $ExpectedVersion) { throw "Expected private descriptor $ExpectedVersion, got $($descriptor.version)." }
if (-not (Test-Path -LiteralPath (Join-Path $Root 'assets\embervale_minimap_arrow_glow.vert.spv') -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $Root 'assets\embervale_minimap_arrow_glow.frag.spv') -PathType Leaf)) {
  throw 'Both pinned arrow-glow SPV outputs must exist in assets before the final build.'
}

$churnCli = 'C:\Users\herks\.codex\plugins\cache\openai-curated-remote\agent-churn-control\0.1.4\scripts\churn_control.py'
$pinnedShaderReceipt = Get-Content -LiteralPath $PinnedShaderReceipt -Raw | ConvertFrom-Json
$pinnedShaderEvidence = Get-Content -LiteralPath $PinnedShaderEvidence -Raw | ConvertFrom-Json
$pinnedShaderManifest = Get-Content -LiteralPath $PinnedShaderCandidateManifest -Raw | ConvertFrom-Json
$pinnedShaderDependencies = Get-Content -LiteralPath $PinnedShaderDependencies -Raw | ConvertFrom-Json
$pinnedEvidenceHash = (Get-FileHash -LiteralPath $PinnedShaderEvidence -Algorithm SHA256).Hash.ToLowerInvariant()
$pinnedCandidateHash = (Get-FileHash -LiteralPath $PinnedShaderCandidateManifest -Algorithm SHA256).Hash.ToLowerInvariant()
$pinnedDependencyHash = (Get-FileHash -LiteralPath $PinnedShaderDependencies -Algorithm SHA256).Hash.ToLowerInvariant()
if ($pinnedShaderReceipt.outcome -cne 'pass' -or $pinnedShaderReceipt.evidence_accepted -ne $true -or
    $pinnedShaderReceipt.candidate_digest -cne "sha256:$pinnedCandidateHash" -or
    $pinnedShaderReceipt.dependency_digest -cne "sha256:$pinnedDependencyHash" -or
    $pinnedShaderReceipt.evidence_digest -cne "sha256:$pinnedEvidenceHash" -or
    $pinnedShaderEvidence.compilerSha256 -cne $ExpectedGlslangSha256 -or
    $pinnedShaderEvidence.compilerVersion -notlike '*16.6.0*' -or
    $pinnedShaderDependencies.compilerSha256 -cne $ExpectedGlslangSha256) {
  throw 'The pinned shader receipt, compiler, evidence, and candidate manifest do not match.'
}
$pinnedCompiler = (Resolve-Path -LiteralPath $pinnedShaderDependencies.compilerPath -ErrorAction Stop).Path
if ((Get-FileHash -LiteralPath $pinnedCompiler -Algorithm SHA256).Hash.ToLowerInvariant() -cne $ExpectedGlslangSha256) {
  throw 'The pinned shader compiler binary no longer matches its accepted hash.'
}
if ($pinnedShaderEvidence.shaders.Count -ne 2) { throw 'Pinned shader evidence must have exactly two stage records.' }
foreach ($entry in $pinnedShaderManifest) {
  $path = Join-Path $Root ($entry.path.Replace('/', '\'))
  if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
      (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.sha256) {
    throw "The pinned shader candidate input changed: $($entry.path)"
  }
}
foreach ($shader in $pinnedShaderEvidence.shaders) {
  if ($shader.stage -notin @('vert', 'frag') -or $shader.exitCode -ne 0 -or $shader.magic -ne '0x07230203') {
    throw 'Pinned shader evidence contains an invalid stage result.'
  }
  $spvName = "embervale_minimap_arrow_glow.$($shader.stage).spv"
  $asset = Join-Path $Root "assets\$spvName"
  $compiled = Join-Path $PinnedShaderOutputDirectory $spvName
  foreach ($path in @($asset, $compiled)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $shader.sha256) {
      throw "Pinned $($shader.stage) SPIR-V output hash mismatch: $path"
    }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -ne $shader.bytes -or $bytes.Length % 4 -ne 0 -or
        $bytes[0] -ne 0x03 -or $bytes[1] -ne 0x02 -or $bytes[2] -ne 0x23 -or $bytes[3] -ne 0x07) {
      throw "Pinned $($shader.stage) SPIR-V bytes or magic changed: $path"
    }
  }
}
$pinnedVerifyOutput = (& python $churnCli verify --receipt $PinnedShaderReceipt 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw "Pinned shader receipt verification failed: $pinnedVerifyOutput" }

New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$logs = Join-Path $OutputDirectory 'logs'
New-Item -ItemType Directory -Path $logs | Out-Null
$shaderChecks = @($pinnedShaderEvidence.shaders | ForEach-Object {
  [ordered]@{ stage = $_.stage; bytes = $_.bytes; sha256 = $_.sha256; compilerVersion = $pinnedShaderEvidence.compilerVersion }
})

$focusedReceipt = Get-Content -LiteralPath $FocusedTestReceipt -Raw | ConvertFrom-Json
$focusedManifest = Get-Content -LiteralPath $FocusedTestCandidateManifest -Raw | ConvertFrom-Json
$focusedManifestHash = (Get-FileHash -LiteralPath $FocusedTestCandidateManifest -Algorithm SHA256).Hash.ToLowerInvariant()
if ($focusedReceipt.outcome -cne 'pass' -or $focusedReceipt.evidence_accepted -ne $true -or
    $focusedReceipt.candidate_digest -cne "sha256:$focusedManifestHash") {
  throw 'The accepted focused-test receipt does not match the supplied tested-candidate manifest.'
}
foreach ($entry in $focusedManifest) {
  $path = Join-Path $Root ($entry.path.Replace('/', '\'))
  if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
      (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.sha256) {
    throw "The accepted focused-test candidate input changed: $($entry.path)"
  }
}
$verifyOutput = (& python $churnCli verify --receipt $FocusedTestReceipt 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw "Focused-test receipt verification failed: $verifyOutput" }

$releaseOut = Join-Path $OutputDirectory 'native\Release\x64\mods\'
$releaseInt = Join-Path $OutputDirectory 'native\obj\Release\x64\'
$releaseProject = Join-Path $Root 'minimap_mod.vcxproj'
$nativeLog = Join-Path $logs 'arrow-glow-native-release-x64.log'
Invoke-MSBuild -Project $releaseProject -OutDir $releaseOut -IntDir $releaseInt -LogPath $nativeLog
$dll = Join-Path $releaseOut 'minimap_mod.dll'
if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) { throw 'Release x64 DLL was not produced.' }

if (-not (Test-Path -LiteralPath $Dumpbin -PathType Leaf)) { throw "Missing pinned dumpbin.exe: $Dumpbin" }
if (-not (Test-Path -LiteralPath $BaselineDll -PathType Leaf)) { throw "Missing accepted .16 GPU-test.4 baseline DLL: $BaselineDll" }
$baselineDependencyLog = Join-Path $OutputDirectory 'dumpbin-baseline-dependents.txt'
$baselineDependencyOutput = (& $Dumpbin /DEPENDENTS $BaselineDll 2>&1 | Tee-Object -FilePath $baselineDependencyLog)
if ($LASTEXITCODE -ne 0) { throw 'dumpbin /DEPENDENTS failed for the .16 baseline.' }
$baselineDependencies = @(Get-ImageDependencyNames -Lines @($baselineDependencyOutput | ForEach-Object { [string]$_ }))
$dependencyLog = Join-Path $OutputDirectory 'dumpbin-candidate-dependents.txt'
$dependencyOutput = (& $Dumpbin /DEPENDENTS $dll 2>&1 | Tee-Object -FilePath $dependencyLog)
if ($LASTEXITCODE -ne 0) { throw 'dumpbin /DEPENDENTS failed for the new candidate.' }
$dependencies = @(Get-ImageDependencyNames -Lines @($dependencyOutput | ForEach-Object { [string]$_ }))
$dependenciesAdded = @($dependencies | Where-Object { $_ -notin $baselineDependencies })
$dependenciesRemoved = @($baselineDependencies | Where-Object { $_ -notin $dependencies })

$zipRoot = Join-Path $OutputDirectory 'private-update-stage'
$modStage = Join-Path $zipRoot 'mods\minimap_mod'
$zipPath = Join-Path $OutputDirectory 'Shroudweave-Minimap-0.5.16-gpu-arrow-glow-test.1-private-update.zip'
if ((Test-Path -LiteralPath $zipRoot) -or (Test-Path -LiteralPath $zipPath)) { throw 'Private ZIP staging already exists.' }
New-Item -ItemType Directory -Path $modStage -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $modStage 'minimap_mod.dll')
Copy-Item -LiteralPath $descriptorPath -Destination (Join-Path $modStage 'mod.json')
$packageFiles = @(
  'embervale_minimap_arrow_glow.vert.spv',
  'embervale_minimap_arrow_glow.frag.spv',
  'embervale_minimap_frame.vert.spv',
  'embervale_minimap_frame.frag.spv',
  'embervale_minimap_terrain.vert.spv',
  'embervale_minimap_terrain.frag.spv'
)
foreach ($name in $packageFiles) {
  Copy-Item -LiteralPath (Join-Path $Root "assets\$name") -Destination (Join-Path $modStage $name)
}
foreach ($name in @('CREDITS.md', 'LICENSE')) {
  Copy-Item -LiteralPath (Join-Path $Root $name) -Destination (Join-Path $modStage $name)
}
$updateReadme = @'
Shroudweave Minimap 0.5.16-gpu-arrow-glow-test.1 private update

This is a local update overlay, not a standalone mod. It requires the complete
optimized Shroudweave Minimap 0.5.16-gpu-test.4 setup and its matching assets.
Extract this ZIP over that existing setup, keeping the existing base map image,
player-arrow image, frame artwork, configuration and all other .16 assets.

The new player_arrow_glow setting defaults to false. Set it to true in the
existing minimap_mod entry in shroudtopia.json. player_arrow_glow_style accepts
same-colour (default) or contrasting. Keep the existing map settings and map
images; this update does not contain large HD map images.

Both local and other-player arrows retain their existing bodies. The GPU halo
is optional and failure to prepare it leaves the legacy arrow rendering path.
'@
Set-Content -LiteralPath (Join-Path $zipRoot 'README-PRIVATE-UPDATE.txt') -Value $updateReadme -Encoding utf8

$members = @(Get-ChildItem -LiteralPath $zipRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
  $_.FullName.Substring($zipRoot.Length + 1).Replace('\', '/')
})
if (@($members | Where-Object { $_ -match 'realmap|\.pdb$|\.log$|shroudtopia\.json|creative' }).Count -gt 0) {
  throw 'The private ZIP stage contains a prohibited map, PDB, log, user config or Creative file.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($zipRoot, $zipPath)
$archiveHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()

$sourceManifest = Get-ChildItem -LiteralPath $Root -File -Recurse | Sort-Object FullName | ForEach-Object {
  [ordered]@{
    path = $_.FullName.Substring($Root.Length + 1).Replace('\', '/')
    bytes = $_.Length
    sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  }
}
$sourceManifestPath = Join-Path $OutputDirectory 'candidate-source-manifest.json'
$sourceManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $sourceManifestPath -Encoding utf8
$sourceManifestHash = (Get-FileHash -LiteralPath $sourceManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$receipt = [ordered]@{
  taskId = $TaskId
  status = 'PRIVATE_BUILD_AND_FREEZE_COMPLETE'
  version = $ExpectedVersion
  baselineManifestSha256 = $ExpectedBaseManifestSha256
  latestParserFixUrl = $LatestParserFix
  glslang = [ordered]@{ path = $pinnedCompiler; version = $pinnedShaderEvidence.compilerVersion; sha256 = $ExpectedGlslangSha256; receiptId = $pinnedShaderReceipt.receipt_id; evidenceDigest = $pinnedShaderReceipt.evidence_digest; verifier = $pinnedVerifyOutput }
  nativeToolchain = [ordered]@{ msbuildVersion = $ExpectedMsBuildVersion; vcToolsVersion = $ExpectedVCToolsVersion; windowsSdkVersion = $ExpectedWindowsSdkVersion; platformToolset = 'v143'; configuration = 'Release'; platform = 'x64' }
  shaders = $shaderChecks
  focusedTest = [ordered]@{ status = 'REUSED_ACCEPTED_RECEIPT'; receiptId = $focusedReceipt.receipt_id; candidateDigest = $focusedReceipt.candidate_digest; evidenceDigest = $focusedReceipt.evidence_digest; verifier = $verifyOutput }
  runtimeDll = [ordered]@{ path = $dll; bytes = (Get-Item -LiteralPath $dll).Length; sha256 = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant() }
  dependencies = [ordered]@{ baseline = $baselineDependencies; candidate = $dependencies; added = $dependenciesAdded; removed = $dependenciesRemoved }
  baselineDll = [ordered]@{ path = $BaselineDll; sha256 = (Get-FileHash -LiteralPath $BaselineDll -Algorithm SHA256).Hash.ToLowerInvariant() }
  dependencyParserPattern = $DependencyPattern
  dependencyParserFixture = 'PASS'
  sourceManifest = [ordered]@{ path = $sourceManifestPath; sha256 = $sourceManifestHash; files = @($sourceManifest).Count }
  package = [ordered]@{ path = $zipPath; bytes = (Get-Item -LiteralPath $zipPath).Length; sha256 = $archiveHash; members = $members }
  packageBase = 'Complete optimized 0.5.16-gpu-test.4 assets and setup; this archive is an overlay only.'
}
$receiptPath = Join-Path $OutputDirectory 'PRIVATE-BUILD-RECEIPT.json'
$receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding utf8
Write-Output "Private DLL: $dll"
Write-Output "Private ZIP: $zipPath"
Write-Output "ZIP SHA256: $archiveHash"
Write-Output "Receipt: $receiptPath"
