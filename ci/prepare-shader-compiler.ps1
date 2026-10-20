$ErrorActionPreference = 'Stop'
$toolRoot = Join-Path $env:RUNNER_TEMP 'minimap-glslang-16.6.0'
$archive = "$toolRoot.zip"
Invoke-WebRequest 'https://github.com/KhronosGroup/glslang/releases/download/16.6.0/glslang-16.6.0-windows-x86_64-release.zip' -OutFile $archive
if ((Get-FileHash -LiteralPath $archive).Hash -ine '82bf434e69b9bb4829de7e2b4bc2c5e7a7861e53d66cf75e5cc70f5f694a8d9b') { throw 'Shader compiler archive hash mismatch' }
Expand-Archive -LiteralPath $archive -DestinationPath $toolRoot
$compiler = @(Get-ChildItem -LiteralPath $toolRoot -Recurse -File -Filter glslang.exe)
if ($compiler.Count -ne 1) { throw 'Expected one shader compiler' }
"MINIMAP_GLSLANG=$($compiler[0].FullName)" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
