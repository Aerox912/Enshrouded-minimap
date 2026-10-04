param(
  [Parameter(Mandatory = $true)][string]$DllPath,
  [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifest = Get-Content -LiteralPath (Join-Path $root 'assets\mod.json') -Raw | ConvertFrom-Json
$version = $manifest.version
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Expected a numeric release version.' }
$dll = Get-Item -LiteralPath $DllPath -ErrorAction Stop
if ($dll.PSIsContainer -or $dll.Name -ne 'minimap_mod.dll') { throw 'Expected the built minimap_mod.dll.' }
$commit = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source commit.' }
$dirty = & git -C $root status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'Commit tracked source changes before packaging.' }

$output = [System.IO.Path]::GetFullPath($OutputDirectory)
$stage = Join-Path $output "stage-v$version"
$zipPath = Join-Path $output "enshrouded-minimap-community-v$version.zip"
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $zipPath)) {
  throw 'Release staging/output already exists. Use a new output directory.'
}
$modDir = Join-Path $stage 'mods\minimap_mod'
New-Item -ItemType Directory -Path $modDir -Force | Out-Null
Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $modDir 'minimap_mod.dll')
Get-ChildItem -LiteralPath (Join-Path $root 'assets') | ForEach-Object {
  Copy-Item -LiteralPath $_.FullName -Destination $modDir -Recurse
}
foreach ($name in @('README.md', 'CREDITS.md', 'LICENSE')) {
  Copy-Item -LiteralPath (Join-Path $root $name) -Destination $stage
}
Copy-Item -LiteralPath (Join-Path $root 'docs') -Destination $stage -Recurse
New-Item -ItemType Directory -Path (Join-Path $stage 'tests') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'tests\README.md') -Destination (Join-Path $stage 'tests\README.md')
$buildInfo = [ordered]@{
  version = $version
  repository = 'https://github.com/Aerox912/Enshrouded-minimap'
  commit = $commit
  platform = 'Windows x64'
  testedClientDataRevision = 1076226
  dllSha256 = (Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash
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
