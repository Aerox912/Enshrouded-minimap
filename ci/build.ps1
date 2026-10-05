$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Set-Location $root
$common=@('/p:Configuration=Release','/p:Platform=x64','/p:PlatformToolset=v143','/m','/nologo')
& MSBuild ./minimap_mod.vcxproj @common
if($LASTEXITCODE) { throw 'Minimap build failed' }
foreach($name in @('tracking','renderer','markers','map_features')) {
  & MSBuild "tests/$($name)_test.vcxproj" @common
  if($LASTEXITCODE) { throw "Test build failed: $name" }
  $test=Get-ChildItem ./build -Recurse -Filter "$($name)_test.exe" | Select-Object -First 1
  if(!$test) { throw "Missing test: $name" }
  & $test.FullName
  if($LASTEXITCODE) { throw "Test failed: $name" }
}
./package-release.ps1 -DllPath ./build/x64/Release/mods/minimap_mod.dll -OutputDirectory ./dist
if($LASTEXITCODE) { throw 'Packaging failed' }
