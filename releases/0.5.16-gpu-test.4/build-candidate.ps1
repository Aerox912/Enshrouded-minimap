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
$oldExportRows = @($oldExports | Where-Object { $_ -match '^\s+\d+\s+\d+\s+[0-9A-Fa-f]+\s+' } | ForEach-Object { $_.Trim() })
$newExportRows = @($newExports | Where-Object { $_ -match '^\s+\d+\s+\d+\s+[0-9A-Fa-f]+\s+' } | ForEach-Object { $_.Trim() })
if (($oldExportRows -join "`n") -cne ($newExportRows -join "`n")) { throw 'Rebuilt Minimap exports differ from the frozen candidate.' }
$oldDependencies = Invoke-Dumpbin -DumpArgs @('/dependents',$historicalDll) -Output (Join-Path $out 'historical-dependents.txt')
$newDependencies = Invoke-Dumpbin -DumpArgs @('/dependents',$dll) -Output (Join-Path $out 'rebuilt-dependents.txt')
$oldDlls = @($oldDependencies | Where-Object { $_.Trim() -match '\.dll$' } | ForEach-Object { $_.Trim().ToLowerInvariant() })
$newDlls = @($newDependencies | Where-Object { $_.Trim() -match '\.dll$' } | ForEach-Object { $_.Trim().ToLowerInvariant() })
if (($oldDlls -join "`n") -cne ($newDlls -join "`n")) { throw 'Rebuilt Minimap dependencies differ from the frozen candidate.' }
$sha = { param([string]$p) (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant() }
$dist = Join-Path $base 'dist'
$receipts = Join-Path $dist 'receipts'
New-Item -ItemType Directory -Force -Path $dist,$receipts | Out-Null
Copy-Item -LiteralPath $dll -Destination (Join-Path $dist 'minimap_mod.dll') -Force
foreach ($file in @($log,$binlog,(Join-Path $out 'historical-headers.txt'),(Join-Path $out 'rebuilt-headers.txt'),(Join-Path $out 'historical-exports.txt'),(Join-Path $out 'rebuilt-exports.txt'),(Join-Path $out 'historical-dependents.txt'),(Join-Path $out 'rebuilt-dependents.txt'))) { Copy-Item -LiteralPath $file -Destination $receipts -Force }
$receipt = [ordered]@{
    schemaVersion = 2; status = 'CI_BUILD_PASS_NEW_RUNTIME_CANDIDATE'; product = $request.component; version = $request.version; managerCandidate = $request.managerCandidate; channel = $request.channel
    sourceRepository = 'Aerox912/Enshrouded-minimap'; sourceCommit = $env:GITHUB_SHA; workflowRunId = [long]$env:GITHUB_RUN_ID; workflowRunAttempt = [int]$env:GITHUB_RUN_ATTEMPT
    sourceManifestSha256 = & $sha (Join-Path $base 'SOURCE-MANIFEST.json'); sourceFileCount = $request.sourceFileCount; sourcePatchSha256 = $request.sourcePatchSha256
    historicalInput = [ordered]@{ archivePath = 'package/frozen-candidate.zip'; archiveSha256 = $request.candidateArchiveSha256; archiveBytes = $request.candidateArchiveBytes; archiveMembers = $request.candidateArchiveMembers; dllSha256 = $request.historicalCandidateDllSha256; dllBytes = $request.historicalCandidateDllBytes; unchangedAssetInputsVerified = $true }
    build = [ordered]@{ platform = 'Windows x64'; configuration = 'Release'; toolset = 'v143'; msvcToolsVersion = $toolVersion; compilerHost = 'HostX86'; windowsSdk = $sdk; compilerSha256 = & $sha (Join-Path $toolBin 'cl.exe'); linkerSha256 = & $sha (Join-Path $toolBin 'link.exe'); dumpbinSha256 = & $sha $dumpbin; msbuildSha256 = & $sha $msbuild; logSha256 = & $sha $log; binlogSha256 = & $sha $binlog; runtimeDllSha256 = & $sha $dll; runtimeDllBytes = (Get-Item $dll).Length; x64 = $true; exportsMatchHistoricalCandidate = $true; dependenciesMatchHistoricalCandidate = $true; historicalRuntimeHashReproduced = $false; byteIdenticalReproductionExpected = $false }
    defaults = $request.defaults; focusedDefaultsHarness = 'verify_focused_defaults.py'; runtimeGameplayAccepted = $false; releasePublished = $false
}
$receipt | ConvertTo-Json -Depth 10 | Set-Content -Encoding utf8 (Join-Path $dist 'BUILD-RECEIPT.json')
