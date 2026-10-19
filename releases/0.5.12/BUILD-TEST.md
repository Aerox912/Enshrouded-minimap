# Build and test instructions

This document describes local candidate verification only. It does not approve
installation, release, or publication.

## Prerequisites

- PowerShell 7.x (`pwsh.exe`), or Windows PowerShell 5.1 when its existing
  local execution policy permits this candidate script. The script does not
  change execution policy.
- Visual Studio 2022 Build Tools with the C++ workload and MSVC v143 x64 tools.
- The separately acquired Khronos glslang 16.6.0 compiler at
  `toolchain/glslang-16.6.0/bin/glslang.exe`. Verify and prepare it as described
  in [GLSLANG-PROVENANCE.md](GLSLANG-PROVENANCE.md); the compiler is not included
  in either candidate ZIP, and the build script does not download it.

No Enshrouded installation or inherited `minimap_mod.dll` is a build input.
When the complete local package baseline contains its original DLL, the script
checks its hash and excludes it from source archives; the build never links or
loads it. The staged runtime package receives only the freshly built DLL.

Three profile tests read a separately supplied client executable fixture as
`argv[1]`. The expected fixture hash and the test-to-input mapping are recorded
in `BUILD-MANIFEST.json`. The fixture is read as PE data by those tests, never
executed, copied, or included in either archive. If it is absent or the hash
does not match, the full test/package gate fails clearly before running tests.
The currently authorized local fixture is
`E:\Build\esm-client-25988036-20261016\enshrouded.exe` (35,327,560 bytes,
SHA-256 `82e5020a8736348cd0374f004b2f5e5ef456b5bd27aa5420df34c7dd241cfeeb`).
Its path and hash describe a host-local test input; the executable itself is
not copied into this candidate or its archives.

## Command

From this directory, run:

```powershell
pwsh.exe -NoProfile -File .\BUILD.ps1 -GameExecutableFixture 'E:\Build\esm-client-25988036-20261016\enshrouded.exe'
```

The command does not change PowerShell's execution policy. It builds the native
mod in `Release|x64` with MSVC v143, compiles the terrain vertex and fragment
shaders for Vulkan 1.0, and compares the fresh SPIR-V output with the checked-in
shader assets. It discovers every `source/tests/*.vcxproj` at run time, builds
each in `Release|x64`, and runs every resulting test executable with the source
directory as its working directory. This keeps `frame_failure_test` able to
locate its source assets. The manifest passes the offline fixture to
`client_1_0_test`, `patch_profile_test`, and `menu_visibility_test`. If `source/tests/perf_verify.ps1`
exists, it is run as an additional independent verification step.

The current candidate has eleven native test project files directly under
`source/tests`. The script discovers those projects at run time; an additional
top-level project is automatically included. The nested offscreen GPU verifier
is separate and is described below. Any build or native test failure stops
staging and returns a non-zero exit code.

`control_test` includes a UI-hook lifecycle regression: it verifies one initial
callsite activation, capture/snapshot shutdown without deactivating or freeing
the installed relay, forwarding of late calls after deactivation/unload, and
reactivation without a second patch. It also verifies that post-CAS instruction-
cache finalization failure retains the relay for forwarding but never enables
capture on retry.

## Offscreen GPU parity

The offscreen Vulkan readback verifier is separate from `BUILD.ps1`. Build it
from the candidate root, then run it with the source directory as its working
directory so it can find the checked-in shader assets. With `MSBuild.exe` on
`PATH`, use:

```powershell
MSBuild.exe .\source\tests\perf_verify\perf_verify_gpu_test.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143
Push-Location .\source
try { .\build\tests\perf_verify\perf_verify_gpu_test.exe } finally { Pop-Location }
```

This requires a Vulkan loader and a graphics-capable device with RGBA8 and
RGBA16F color-attachment support. The candidate verifier retains orientation
coverage and adds all-size terrain coverage. It does not use the game or a
display. The integrated candidate has not yet completed final source review or
the acceptance gate.

## Input identity and package-only reuse

To emit or refresh `SOURCE-INPUT-MANIFEST.json` without a build or tests, run:

```powershell
pwsh.exe -NoProfile -File .\BUILD.ps1 -ManifestOnly
```

The manifest has stable path ordering and records SHA-256 and byte length for
the production source, tests/projects, shader assets, package baseline, and
`BUILD-MANIFEST.json`. It excludes build output, verification logs, generated
native binaries, the inherited package DLL, documentation, and local compiler
files. The vertex and fragment GLSL/SPIR-V hashes are also listed explicitly. A
full build stores the same manifest in its run folder and records its digest,
the fresh DLL hash, the build-script hash, and hashes for all native build/test
logs and any `perf_verify` logs.

After the full build/test run has passed, the parent may authorize local review
packaging while independent review is still pending. `-PackageOnly` verifies
that the candidate still matches the manifest, the tested DLL still matches its
expected hash, the prior receipt names the complete current test set with zero
exit codes, and every retained log still matches its recorded hash. It then
stages a new runtime package and source archive from the exact tested DLL. This
mode does not rebuild, recompile shaders, or rerun tests. Pass the exact accepted
run directory from under the output root:

```powershell
pwsh.exe -NoProfile -File .\BUILD.ps1 -PackageOnly `
  -ExpectedSourceManifestSha256 '<accepted-source-manifest-sha256>' `
  -ExpectedTestedDllSha256 '<accepted-tested-dll-sha256>' `
   -ApprovedRunRoot 'E:\Build\minimap-creative-compat-20261018\run-<accepted-run-id>' `
  -ExpectedAcceptedBuildScriptSha256 '<accepted-build-script-sha256>'
```

The script stages outputs but does not create ZIPs unless
`-SealApprovedCandidate` is added after the parent agent explicitly authorizes
local review archives. Those archives remain pending independent review and
live-game acceptance. ZIP entries are written in ordinal path order with a fixed
timestamp, verified byte-for-byte against the staged trees, and checksummed.

The package-only invocation also requires the exact accepted test-harness SHA-256 from its run receipt. The new package-only receipt records the current packaging-script hash separately and leaves the accepted test receipt unchanged.

## Outputs

Each full build invocation creates a timestamped run directory under
 `E:\Build\minimap-creative-compat-20261018` with build logs, test logs, a staged
runtime package, a source-archive tree, and a machine-readable local build
receipt. A package-only invocation creates a separate `run-package-*` directory
and retains copies of the exact accepted logs in that folder. Intermediate
outputs are retained for review; the script does not clean pre-existing runs.
`-ValidateOnly` creates no output directory and does not build or execute tests.

The script creates the versioned runtime ZIP, source ZIP, and `SHA256SUMS.txt`
only when invoked with `-SealApprovedCandidate`. Use that switch only after the
implementation is frozen and the parent agent has explicitly authorized final
sealing. The ZIPs are local test artifacts and do not establish in-game or
multiplayer acceptance.

The source archive contains source, tests, package assets, the runtime README, these instructions,
`BUILD-MANIFEST.json`, `SOURCE-INPUT-MANIFEST.json`, Sparkweave branding assets
and provenance, and `GLSLANG-PROVENANCE.md`. The source archive records the
shared kit version and checksums for its copied logo and mod-family banner. It
excludes the compiler and its download archive,
build output, logs, generated churn-control state, offscreen GPU result files,
native test binaries, the inherited baseline DLL, and the client executable
fixture. The runtime package contains the freshly
built DLL, baseline credits/license and required assets, plus the GLSL and
SPIR-V terrain shaders. The compiler is not included in either archive.

## Scope of verification

Successful native tests and packaging verify local source, shader, metadata,
and archive preparation. The separate offscreen Vulkan checks exercise terrain
readback and cache behavior on the available device; they do not verify output
on a physical HDR display or the game's presentation path. No FPS or frame-time
result is claimed. Map alignment in the running game, process memory, multiplayer
behavior, device-loss recovery, and compatibility with a live game remain
unverified.

