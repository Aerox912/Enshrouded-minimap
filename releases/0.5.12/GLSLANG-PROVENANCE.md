# Pinned GLSL compiler provenance

The build uses the official Khronos glslang 16.6.0 Windows x64 release to
compile the terrain shaders for Vulkan 1.0. This compiler is a local build
tool. It is not included in the source archive or the runtime package, and the
build script does not download or install it.

Official release: <https://github.com/KhronosGroup/glslang/releases/tag/16.6.0>

| File | SHA-256 |
| --- | --- |
| `glslang-16.6.0-windows-x86_64-release.zip` | `82bf434e69b9bb4829de7e2b4bc2c5e7a7861e53d66cf75e5cc70f5f694a8d9b` |
| `bin/glslang.exe` | `9643709c02c83e26773f96b6a87f197ab2e84f78be0879ad8fb3619418b5d9ad` |
| `LICENSE.txt` | `17e70c676e1521ff3e4686f04a2053d93a7e28a33be8de7ec37ab0ff72feb677` |

Download the named asset from the official release page and verify its archive
hash before extracting it. Place the executable at
`toolchain/glslang-16.6.0/bin/glslang.exe` and its license at
`toolchain/glslang-16.6.0/LICENSE.txt`. Keeping the verified archive at
`toolchain/glslang-16.6.0-windows-x86_64-release.zip` lets `BUILD.ps1` check
that hash too. The script always checks the executable, license, and reported
compiler version before a full build.

The compiler and downloaded archive are excluded from both candidate ZIPs.
