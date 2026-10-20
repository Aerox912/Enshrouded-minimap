# Build and package 0.5.13 locally

Nick explicitly waived GitHub Actions provenance for this release. The current
source must still be recorded in Aerox912/Enshrouded-minimap. Keep exact source
pins, compiler identity, local DLL/package hashes and archive readback. The
existing private fixture release stays unchanged; no private Actions job or
credential access is needed for this release.

Use PowerShell 7, Visual Studio 2022 C++ Build Tools/MSVC v143, MSBuild 17.8 or
later, Windows SDK 10.0.26100.0 and Python 3.12. Supply the existing hash-verified
Khronos glslang16.6.0 compiler; the wrapper retains shader/SPIR-V parity checks.

From a fresh E:\Build source copy matching the source inventory:

    ./ci/pipeline-tests.ps1
    ./ci/build.ps1 -GlslangPath '<verified glslang.exe>' -GameExecutableFixture '<absolute offline enshrouded.exe>' -CreativeZipFixture '<absolute original Creative 2.1.4 ZIP>' -OutputDirectory '<new output directory>'

The game and Creative files are inert fixture inputs, never executed, committed,
printed, cached or redistributed. The game identity is pinned in the release
BUILD-MANIFEST.json; the unchanged fixture preparer pins the Creative ZIP/member.
Generated good/wrong module data is private and excluded from archives.

The full local gate builds Debug/Release DLLs and all thirteen direct native test
projects, including the focused81-case suite. Four companion suites require the
game; focused compatibility uses game/good/wrong module data. Terrain uses
--cpu-only; the nested offscreen GPU verifier is outside this non-GPU scope.

Public CI retains source/shader/pipeline regression checks and uses
-AllowMissingFixture: it builds all thirteen direct native projects and runs
eight fixture-free suites, without release packaging or private fixture access.
Public CI is optional evidence for this local release, not a publication gate.

Retained local regression tests reject source tampering, stale aggregate hashes,
incomplete native receipts, skipped required tests and missing/wrong fixture
inputs, and verify unlisted runtime/source files stay outside both archives.
The packager preserves the accepted runtime membership and reopens both ZIPs to
verify exact entries and hashes. Releaser and Tester use the exact resulting
local bytes; do not substitute an automatic tag rebuild. Original Creative is
separate; broader gameplay, loader, multiplayer or GPU performance support is
not inferred from these checks.