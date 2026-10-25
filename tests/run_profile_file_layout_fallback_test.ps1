param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [switch]$ExpectedRed,
    [switch]$FollowupControlsOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$sourceRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputRoot) {
    throw "Refusing to reuse an existing test output directory: $outputRoot"
}
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

$fixtureDirectory = Join-Path $outputRoot 'fixtures'
$binaryDirectory = Join-Path $outputRoot 'bin'
$intermediateDirectory = Join-Path $outputRoot 'obj'
$fixtureLog = Join-Path $outputRoot 'fixture-generation.log'
$buildLog = Join-Path $outputRoot 'test-build.log'
$runLog = Join-Path $outputRoot 'test-run.log'
$fixtureScript = Join-Path $PSScriptRoot 'profile_file_layout_fixtures.py'
$testProject = Join-Path $PSScriptRoot 'profile_file_layout_fallback_test.vcxproj'
$testExecutable = Join-Path $binaryDirectory 'profile_file_layout_fallback_test.exe'
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'

$fixtureArguments = @($sourceRoot, $fixtureDirectory, '--check-signature-header', (Join-Path $sourceRoot 'src\client_1_0_file_signatures.h'))
if ($FollowupControlsOnly) {
    if ($ExpectedRed) {
        throw 'Follow-up controls cannot use the initial ExpectedRed mode.'
    }
    $fixtureArguments += '--followup-controls'
}

& python $fixtureScript @fixtureArguments 2>&1 | Tee-Object -LiteralPath $fixtureLog
$fixtureExit = $LASTEXITCODE
if ($fixtureExit -ne 0) {
    throw "Synthetic fixture generation failed with exit code $fixtureExit. See $fixtureLog"
}

& $msbuild $testProject /t:Rebuild /p:Configuration=Release /p:Platform=x64 "/p:OutDir=$binaryDirectory\" "/p:IntDir=$intermediateDirectory\" /nologo /verbosity:minimal /m:1 2>&1 | Tee-Object -LiteralPath $buildLog
$buildExit = $LASTEXITCODE
if ($buildExit -ne 0) {
    throw "Focused test build failed with exit code $buildExit. See $buildLog"
}

& $testExecutable (Join-Path $fixtureDirectory 'cases.txt') 2>&1 | Tee-Object -LiteralPath $runLog
$testExit = $LASTEXITCODE
$runText = Get-Content -LiteralPath $runLog -Raw

if ($ExpectedRed) {
    $expectedFailures = @(
        'FAIL synthetic-current-layout.pe expected=current actual=unsupported',
        'FAIL synthetic-unrelated-patcher-edits.pe expected=current actual=unsupported'
    )
    foreach ($failure in $expectedFailures) {
        if ($runText -notmatch [regex]::Escape($failure)) {
            throw "Expected RED evidence was absent: $failure. See $runLog"
        }
    }
    if ($runText -notmatch 'profile_file_layout_cases=81 failures=2' -or $testExit -ne 1) {
        throw "The initial fallback regression did not fail only on the two required acceptances. See $runLog"
    }
    if (@(Select-String -LiteralPath $runLog -Pattern '^FAIL ' -AllMatches).Count -ne 2) {
        throw "The initial fallback regression had unexpected failures. See $runLog"
    }
    Write-Output "RED confirmed: actual IdentifyFile rejects both synthetic latest-layout acceptance cases before the fallback is implemented."
    Write-Output "Logs: $fixtureLog ; $buildLog ; $runLog"
    exit 0
}

$expectedCaseCount = if ($FollowupControlsOnly) { 11 } else { 81 }
if ($testExit -ne 0 -or $runText -notmatch "profile_file_layout_cases=$expectedCaseCount failures=0" -or
    $runText -match '(?m)^FAIL ') {
    throw "Focused profile file-layout regression failed. See $runLog"
}
Write-Output "GREEN confirmed: all $expectedCaseCount focused synthetic profile file-layout cases passed."
Write-Output "Logs: $fixtureLog ; $buildLog ; $runLog"
