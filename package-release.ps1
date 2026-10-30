param(
  [Parameter(Mandatory = $true)][string]$DllPath,
  [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifest = Get-Content -LiteralPath (Join-Path $root 'assets\mod.json') -Raw | ConvertFrom-Json
$version = $manifest.version
$semVerPattern = '^(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*))*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$'
if ($version -notmatch $semVerPattern) { throw 'Expected a valid SemVer version.' }
if ($version -cnotin @('0.5.15', '0.5.16', '0.5.16-gpu-reporter-test.2', '0.5.16-startup-fix-test.1', '0.5.16-marker-route-fix-test.1', '0.5.16-perf-glow-test.1', '0.5.17')) { throw 'Expected an approved Minimap pipeline version.' }
$dll = Get-Item -LiteralPath $DllPath -ErrorAction Stop
if ($dll.PSIsContainer -or $dll.Name -ne 'minimap_mod.dll') { throw 'Expected the built minimap_mod.dll.' }
$commit = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source commit.' }
$dirty = & git -C $root status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'Commit tracked source changes before packaging.' }

$output = [System.IO.Path]::GetFullPath($OutputDirectory)
$receiptPath = Join-Path $output 'BUILD-RECEIPT.json'
if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) {
  throw 'Build receipt does not account for this package.'
}
$receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
$inventory = Get-Content -LiteralPath (Join-Path $root 'ci/source-inputs.json') -Raw | ConvertFrom-Json
$dllHash = (Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
if ($receipt.runtimeDllSha256 -cne $dllHash -or
    $receipt.nativeSourceTreeSha256 -cne $inventory.acceptedNativeSourceTreeSha256) {
  throw 'Build receipt does not account for this package.'
}
if ($version -cin @('0.5.15', '0.5.16', '0.5.16-gpu-reporter-test.2', '0.5.16-startup-fix-test.1', '0.5.16-marker-route-fix-test.1', '0.5.16-perf-glow-test.1', '0.5.17')) {
  $expectedTests = @(
    'tracking_test', 'renderer_test', 'markers_test', 'map_features_test',
    'client_1_0_test', 'frame_failure_test', 'hdr_test', 'patch_profile_test',
    'menu_visibility_test', 'compat_copy_test', 'marker_summary_test',
    'public_creative_compat_test'
  )
  if ($version -cin @('0.5.16', '0.5.16-startup-fix-test.1', '0.5.16-marker-route-fix-test.1', '0.5.16-perf-glow-test.1', '0.5.17')) {
    $expectedTests += 'swapchain_startup_test'
  }
  $actualTests = @($receipt.tests | ForEach-Object { $_.name })
  $testStatusesValid = @($receipt.tests | Where-Object { $_.status -notin @('PASSED', 'SKIPPED') }).Count -eq 0
  $fixtureTests = @('client_1_0_test', 'patch_profile_test', 'menu_visibility_test', 'public_creative_compat_test')
  $fixtureStatusesValid = if ($receipt.fixtureTestsComplete) {
    @($receipt.tests | Where-Object { $_.name -in $fixtureTests -and $_.status -ne 'PASSED' }).Count -eq 0
  } else {
    @($receipt.tests | Where-Object { $_.name -in $fixtureTests -and $_.status -ne 'SKIPPED' }).Count -eq 0
  }
  $nonFixtureStatusesValid = @($receipt.tests | Where-Object { $_.name -notin $fixtureTests -and $_.status -ne 'PASSED' }).Count -eq 0
  $compilerPropertiesValid = $receipt.PSObject.Properties.Name -contains 'compilerProperties' -and
    $receipt.compilerProperties.EnableMinimapBackgroundDiagnostics -eq $false -and
    $receipt.compilerProperties.EnableMinimapPresentDecisionDiagnostics -eq $false
  if ($inventory.version -cne $version -or $receipt.version -cne $version -or $receipt.sourceCommit -cne $commit -or
      $receipt.sourceInventorySha256 -cne (Get-FileHash -LiteralPath (Join-Path $root 'ci/source-inputs.json') -Algorithm SHA256).Hash.ToLowerInvariant() -or
      $receipt.nativeSourceTreeSha256 -cne $inventory.acceptedNativeSourceTreeSha256 -or
      $receipt.runtimeDllBytes -ne $dll.Length -or
      -not $compilerPropertiesValid -or
      $receipt.fixtureTestsComplete -notin @($true, $false) -or
      ($actualTests.Count -ne $expectedTests.Count) -or
      @(Compare-Object -ReferenceObject $expectedTests -DifferenceObject $actualTests).Count -ne 0 -or
      -not $testStatusesValid -or -not $fixtureStatusesValid -or -not $nonFixtureStatusesValid) {
    throw 'Build receipt does not account for this package.'
  }
}
$stage = Join-Path $output "stage-v$version"
$zipPath = Join-Path $output "enshrouded-minimap-v$version.zip"
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $zipPath)) {
  throw 'Release staging/output already exists. Use a new output directory.'
}
$modDir = Join-Path $stage 'mods\minimap_mod'
New-Item -ItemType Directory -Path $modDir -Force | Out-Null
Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $modDir 'minimap_mod.dll')
Get-ChildItem -LiteralPath (Join-Path $root 'assets') | ForEach-Object {
  Copy-Item -LiteralPath $_.FullName -Destination $modDir -Recurse
}
foreach ($name in @('README.md', 'README-INSTALL.txt', 'CREDITS.md', 'LICENSE')) {
  Copy-Item -LiteralPath (Join-Path $root $name) -Destination $stage
}
Copy-Item -LiteralPath (Join-Path $root 'docs') -Destination $stage -Recurse
New-Item -ItemType Directory -Path (Join-Path $stage 'tests') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'tests\README.md') -Destination (Join-Path $stage 'tests\README.md')
Copy-Item -LiteralPath $receiptPath -Destination $stage
$buildInfo = [ordered]@{
  version = $version
  repository = 'https://github.com/Aerox912/Enshrouded-minimap'
  commit = $commit
  platform = 'Windows x64'
  runtimeDllSha256 = $dllHash
  sourceInventorySha256 = $receipt.sourceInventorySha256
  nativeSourceTreeSha256 = $receipt.nativeSourceTreeSha256
  fixtureTestsComplete = [bool]$receipt.fixtureTestsComplete
  tests = $receipt.tests
}
$buildInfo | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'build-info.json') -Encoding utf8
$checksums = Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName | ForEach-Object {
  $relative = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
  '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
}
$checksums | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ascii
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zipPath)
$archiveHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
('{0}  {1}' -f $archiveHash, [System.IO.Path]::GetFileName($zipPath)) |
  Set-Content -LiteralPath ($zipPath + '.sha256') -Encoding ascii
Get-Item -LiteralPath $zipPath | Select-Object FullName, Length
Write-Output "SHA256: $archiveHash"
