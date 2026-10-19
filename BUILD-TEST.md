# Build and package 0.5.12

Use PowerShell 7, Visual Studio 2022 C++ Build Tools with MSVC v143 and MSBuild
on PATH. Prepare the pinned Khronos glslang 16.6.0 compiler using
[its provenance and hashes](releases/0.5.12/GLSLANG-PROVENANCE.md).

```powershell
./ci/build.ps1 -GlslangPath '<verified glslang.exe>' -GameExecutableFixture '<offline enshrouded.exe>' -OutputDirectory '<new output directory>'
```

`MINIMAP_GLSLANG` and `MINIMAP_GAME_FIXTURE` can supply those paths. The game
executable is read as offline PE data only, never executed or bundled. Its exact
hash and size are pinned in [BUILD-MANIFEST.json](releases/0.5.12/BUILD-MANIFEST.json).
Do not upload the proprietary executable into this public repository.

For public GitHub-hosted CI, pass `-AllowMissingFixture`. All twelve direct
native test projects are compiled. Eight environment-independent tests run;
`client_1_0_test`, `patch_profile_test`, `menu_visibility_test`, and
`renderer_ownership_adapter_test` are explicitly SKIPPED when no fixture is
provided. A supplied wrong fixture fails, even in this mode. Without the switch,
a missing fixture also fails. The nested offscreen GPU verifier is retained
but is not run by public CI; it requires a Vulkan-capable device. Neither route
claims gameplay, physical HDR display or dedicated-server acceptance.

The pipeline verifies every pinned source/package input, compiles both terrain
shaders with the pinned compiler, compares their SPIR-V with the accepted assets,
rebuilds the DLL, compiles all twelve test projects and runs the applicable tests.
It emits runtime and source candidate ZIPs, SHA256SUMS.txt and BUILD-RECEIPT.json.
The source ZIP contains the actual root sources and pipeline plus retained
package inputs and provenance; SOURCE-FILES.json identifies its exact inputs.
Archive entries use a fixed timestamp and are checked against staging.

The receipt records individual PASSED/SKIPPED test states, source identity, the
base Git commit and dirty state, and the freshly built DLL hash. Compiler/linker
version, paths and debug data may change binary bytes; exact DLL reproduction
is not presumed. If the DLL differs from the accepted published DLL, stop before
replacing or publishing it. The GitHub tag path also rejects that mismatch.
No tag or release is created by a main-branch or pull-request build.

Source/package validation regressions can be checked with `./ci/pipeline-tests.ps1`.
`./build-release.ps1` remains a build-only helper for the existing install script;
it defaults to v143 and does not replace the build/test/package gate above.
The retained historical instructions under releases/0.5.12 describe their old
snapshot; the actual current counts are twelve projects and four fixture tests.
