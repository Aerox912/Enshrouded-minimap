$ErrorActionPreference = 'Stop'
$base = $PSScriptRoot
$source = Join-Path $base 'source'
$request = Get-Content -Raw -LiteralPath (Join-Path $base 'BUILD-REQUEST.json') | ConvertFrom-Json
python (Join-Path $base 'verify_candidate.py')
if ($LASTEXITCODE -ne 0) { throw 'Frozen source and historical candidate verification failed.' }
python (Join-Path $base 'verify_focused_defaults.py')
if ($LASTEXITCODE -ne 0) { throw 'Focused top-right/north-up defaults contract failed.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe is missing.' }
$install = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $install) { throw 'Visual Studio with MSBuild is missing.' }
$toolRoot = Join-Path $install 'VC\Tools\MSVC'
$tool = Get-ChildItem -LiteralPath $toolRoot -Directory | Where-Object { $_.Name -eq $request.build.msvcToolsVersion } | Select-Object -First 1
if (-not $tool) { throw "Reviewed MSVC $($request.build.msvcToolsVersion) toolset is not installed." }
$toolVersion = $tool.Name
$toolBin = Join-Path $tool.FullName 'bin\HostX86\x64'
foreach ($name in @('cl.exe','link.exe','dumpbin.exe')) { if (-not (Test-Path (Join-Path $toolBin $name))) { throw "Required HostX86\x64 tool missing: $name" } }
$vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvarsall.bat'
$msbuild = Join-Path $install 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $vcvars) -or -not (Test-Path $msbuild)) { throw 'Pinned build tools are incomplete.' }
$sdk = [string]$request.build.windowsSdk
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include\$sdk\ucrt"
if (-not (Test-Path (Join-Path $sdkRoot 'stdlib.h'))) { throw "Windows SDK $sdk is missing." }
$out = Join-Path $env:RUNNER_TEMP 'minimap-gpu-test4-build'
$obj = Join-Path $out 'obj'
New-Item -ItemType Directory -Force -Path $out,$obj | Out-Null
$dll = Join-Path $out 'minimap_mod.dll'
$log = Join-Path $out 'build.log'
$binlog = Join-Path $out 'build.binlog'
$map = Join-Path $out 'minimap_mod.map'
$project = Join-Path $source 'minimap_mod.vcxproj'
$arguments = "`"$vcvars`" x64 -vcvars_ver=$($request.build.msvcMajorMinor) && `"$msbuild`" `"$project`" /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /p:VCToolsVersion=$toolVersion /p:WindowsTargetPlatformVersion=$sdk /p:OutDir=$out\ /p:IntDir=$obj\ /p:GenerateMapFile=true /p:MapFileName=`"$map`" /nologo /m /bl:`"$binlog`" /flp:logfile=`"$log`";verbosity=normal"
& cmd.exe /d /c $arguments
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed: $LASTEXITCODE" }
if (-not (Select-String -LiteralPath $log -Pattern '0 Warning\(s\)' -Quiet) -or -not (Select-String -LiteralPath $log -Pattern '0 Error\(s\)' -Quiet)) { throw 'Build log reports warnings or errors.' }
$dist = Join-Path $base 'dist'
$receipts = Join-Path $dist 'receipts'
New-Item -ItemType Directory -Force -Path $dist,$receipts | Out-Null
$harnessRoot = Join-Path $base 'tests\focused-default'
python (Join-Path $base 'prepare_focused_harness.py')
if ($LASTEXITCODE -ne 0) { throw 'Frozen focused-default harness preparation failed.' }
$harnessProject = Join-Path $harnessRoot 'top-right-default-test.vcxproj'
$harnessOut = Join-Path $harnessRoot 'focused-bin'
$harnessObj = Join-Path $harnessRoot 'focused-obj'
$harnessBuildLog = Join-Path $out 'focused-default-build.log'
$harnessBinLog = Join-Path $out 'focused-default-build.binlog'
$harnessArguments = "`"$vcvars`" x64 -vcvars_ver=$($request.build.msvcMajorMinor) && `"$msbuild`" `"$harnessProject`" /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /p:VCToolsVersion=$toolVersion /p:WindowsTargetPlatformVersion=$sdk /p:OutDir=$harnessOut\ /p:IntDir=$harnessObj\ /nologo /m /bl:`"$harnessBinLog`" /flp:logfile=`"$harnessBuildLog`";verbosity=normal"
& cmd.exe /d /c $harnessArguments
if ($LASTEXITCODE -ne 0) { throw "Focused harness MSBuild failed: $LASTEXITCODE" }
if (-not (Select-String -LiteralPath $harnessBuildLog -Pattern '0 Warning\(s\)' -Quiet) -or -not (Select-String -LiteralPath $harnessBuildLog -Pattern '0 Error\(s\)' -Quiet)) { throw 'Focused harness build log reports warnings or errors.' }
$harnessExe = Join-Path $harnessOut 'top-right-default-test.exe'
$harnessFixture = Join-Path $harnessRoot 'provenance\position-config.json'
$harnessOutput = & $harnessExe $harnessFixture 2>&1
$harnessExit = $LASTEXITCODE
$harnessText = $harnessOutput -join "`n"
$harnessText | Set-Content -LiteralPath (Join-Path $out 'focused-default-test.log') -Encoding utf8
if ($harnessExit -ne 0 -or $harnessText -notmatch 'FOCUSED_RESULT=PASS') { throw 'Frozen focused-default native harness failed.' }
$harnessManifestPath = Join-Path $receipts 'focused-default-harness-input.json'
$harnessManifest = Get-Content -Raw -LiteralPath $harnessManifestPath | ConvertFrom-Json
$sha = { param([string]$p) (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant() }
$harnessManifest | Add-Member -NotePropertyName status -NotePropertyValue 'FOCUSED_NATIVE_DEFAULTS_PASS' -Force
$harnessManifest | Add-Member -NotePropertyName harnessBuildLogSha256 -NotePropertyValue (& $sha $harnessBuildLog) -Force
$harnessManifest | Add-Member -NotePropertyName harnessBinLogSha256 -NotePropertyValue (& $sha $harnessBinLog) -Force
$harnessManifest | Add-Member -NotePropertyName harnessExecutableSha256 -NotePropertyValue (& $sha $harnessExe) -Force
$harnessManifest | Add-Member -NotePropertyName harnessTestLogSha256 -NotePropertyValue (& $sha (Join-Path $out 'focused-default-test.log')) -Force
$harnessManifest | Add-Member -NotePropertyName compilerToolsVersion -NotePropertyValue $toolVersion -Force
$harnessManifest | Add-Member -NotePropertyName windowsSdk -NotePropertyValue $sdk -Force
$harnessManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $harnessManifestPath -Encoding utf8
$dumpbin = Join-Path $toolBin 'dumpbin.exe'
$originalArchive = Join-Path $base 'package\frozen-candidate.zip'
$extract = Join-Path $out 'historical-package'
Expand-Archive -LiteralPath $originalArchive -DestinationPath $extract -Force
$historicalDll = Join-Path $extract 'mods\minimap_mod\minimap_mod.dll'
function Invoke-Dumpbin([string[]]$DumpArgs, [string]$Output) {
    $text = & $dumpbin @DumpArgs 2>&1
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed: $($DumpArgs -join ' ')" }
    $text | Set-Content -LiteralPath $Output -Encoding utf8
    return @($text)
}
$oldHeaders = Invoke-Dumpbin -DumpArgs @('/headers',$historicalDll) -Output (Join-Path $out 'historical-headers.txt')
$newHeaders = Invoke-Dumpbin -DumpArgs @('/headers',$dll) -Output (Join-Path $out 'rebuilt-headers.txt')
if ((($oldHeaders -join "`n") -notmatch '8664 machine \(x64\)') -or (($newHeaders -join "`n") -notmatch '8664 machine \(x64\)')) { throw 'Historical or rebuilt Minimap binary is not x64.' }
$oldExports = Invoke-Dumpbin -DumpArgs @('/exports',$historicalDll) -Output (Join-Path $out 'historical-exports.txt')
$newExports = Invoke-Dumpbin -DumpArgs @('/exports',$dll) -Output (Join-Path $out 'rebuilt-exports.txt')
function Get-ExportSymbols([string[]]$DumpOutput) {
    $symbols = @()
    foreach ($line in $DumpOutput) {
        if ($line -match '^\s+(\d+)\s+(\d+)\s+[0-9A-Fa-f]+\s+(\S+)') { $symbols += "$($Matches[1]) $($Matches[2]) $($Matches[3])" }
    }
    return $symbols
}
$oldExportSymbols = @(Get-ExportSymbols $oldExports)
$newExportSymbols = @(Get-ExportSymbols $newExports)
if (($oldExportSymbols -notcontains '1 0 CreateModInstance') -or ($newExportSymbols -notcontains '1 0 CreateModInstance')) { throw 'Historical or rebuilt Minimap DLL lacks the CreateModInstance export.' }
if (($oldExportSymbols -join "`n") -cne ($newExportSymbols -join "`n")) { throw 'Rebuilt Minimap export names or ordinals differ from the frozen candidate.' }
$oldDependencies = Invoke-Dumpbin -DumpArgs @('/dependents',$historicalDll) -Output (Join-Path $out 'historical-dependents.txt')
$newDependencies = Invoke-Dumpbin -DumpArgs @('/dependents',$dll) -Output (Join-Path $out 'rebuilt-dependents.txt')
$oldDlls = @($oldDependencies | Where-Object { $_.Trim() -match '\.dll$' } | ForEach-Object { $_.Trim().ToLowerInvariant() } | Sort-Object -Unique)
$newDlls = @($newDependencies | Where-Object { $_.Trim() -match '\.dll$' } | ForEach-Object { $_.Trim().ToLowerInvariant() } | Sort-Object -Unique)
if (($oldDlls -join "`n") -cne ($newDlls -join "`n")) { Write-Host "Historical dependencies: $($oldDlls -join ', ')"; Write-Host "Rebuilt dependencies: $($newDlls -join ', ')"; throw 'Rebuilt Minimap dependencies differ from the frozen candidate.' }
$sha = { param([string]$p) (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant() }
Copy-Item -LiteralPath $dll -Destination (Join-Path $dist 'minimap_mod.dll') -Force
foreach ($file in @($log,$binlog,$harnessBuildLog,$harnessBinLog,(Join-Path $out 'focused-default-test.log'),(Join-Path $out 'historical-headers.txt'),(Join-Path $out 'rebuilt-headers.txt'),(Join-Path $out 'historical-exports.txt'),(Join-Path $out 'rebuilt-exports.txt'),(Join-Path $out 'historical-dependents.txt'),(Join-Path $out 'rebuilt-dependents.txt'))) { Copy-Item -LiteralPath $file -Destination $receipts -Force }
$receipt = [ordered]@{
    schemaVersion = 2; status = 'CI_BUILD_PASS_NEW_RUNTIME_CANDIDATE'; product = $request.component; version = $request.version; managerCandidate = $request.managerCandidate; channel = $request.channel
    sourceRepository = 'Aerox912/Enshrouded-minimap'; sourceCommit = $env:GITHUB_SHA; workflowRunId = [long]$env:GITHUB_RUN_ID; workflowRunAttempt = [int]$env:GITHUB_RUN_ATTEMPT
    sourceManifestSha256 = & $sha (Join-Path $base 'SOURCE-MANIFEST.json'); sourceFileCount = $request.sourceFileCount; sourcePatchSha256 = $request.sourcePatchSha256
    historicalInput = [ordered]@{ archivePath = 'package/frozen-candidate.zip'; archiveSha256 = $request.candidateArchiveSha256; archiveBytes = $request.candidateArchiveBytes; archiveMembers = $request.candidateArchiveMembers; dllSha256 = $request.historicalCandidateDllSha256; dllBytes = $request.historicalCandidateDllBytes; unchangedAssetInputsVerified = $true }
    build = [ordered]@{ platform = 'Windows x64'; configuration = 'Release'; toolset = 'v143'; msvcToolsVersion = $toolVersion; compilerHost = 'HostX86'; windowsSdk = $sdk; compilerSha256 = & $sha (Join-Path $toolBin 'cl.exe'); linkerSha256 = & $sha (Join-Path $toolBin 'link.exe'); dumpbinSha256 = & $sha $dumpbin; msbuildSha256 = & $sha $msbuild; logSha256 = & $sha $log; binlogSha256 = & $sha $binlog; runtimeDllSha256 = & $sha $dll; runtimeDllBytes = (Get-Item $dll).Length; x64 = $true; exportsMatchHistoricalCandidate = $true; dependenciesMatchHistoricalCandidate = $true; historicalRuntimeHashReproduced = $false; byteIdenticalReproductionExpected = $false }
    defaults = $request.defaults; focusedDefaultsHarness = $harnessManifest; runtimeGameplayAccepted = $false; releasePublished = $false
}
$receipt | ConvertTo-Json -Depth 10 | Set-Content -Encoding utf8 (Join-Path $dist 'BUILD-RECEIPT.json')
