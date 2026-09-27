# Camera tracking regression tests

From a Visual Studio x64 Native Tools prompt at the repository root:

```powershell
MSBuild tests\tracking_test.vcxproj /p:Configuration=Release /p:Platform=x64
.\build\tests\tracking_test.exe
```

The project defaults to v142, matching the mod. For Visual Studio 2022 with
v143 installed, add `/p:PlatformToolset=v143` to the build command. Build output
can be redirected with the usual `OutDir` and `IntDir` MSBuild properties.

The executable includes the production reader and supplies synthetic ECS records.
It exits with a nonzero status on failure, including in Release builds. It checks:

- Named system identity and the registry's string length convention.
- Advancing the copied iterator, filtering the local entity, and preserving the
  original cursor.
- Updating player position and camera heading independently.
- Rejecting stale fallback samples, invalid transforms, and unavailable iterators.
- Preserving legacy publication when the named camera path is unavailable.

These tests do not load or hook the game and do not validate Vulkan rendering.

## Client 1076226 layout

The new path is limited to the observed Steam client layout. It requires matching
registry names and function pointers for `player_camera_transform` and
`player_waypoints_ui`, the expected iterator targets, and the camera entry bytes.
All addresses in the implementation are image-relative RVAs.

The camera iterator's 80-byte record provides `RenderTransform` at +0x10,
`LocalPlayerData` at +0x18, and `LocalPlayerCameraTransform` at +0x30. Position uses
the player's 32.32 fixed-point coordinates; heading uses the camera quaternion.
The local entity is selected by comparing the iterator's current entity ID with
`LocalPlayerData` at +4, matching the game's camera system. The iterator advances
its context at +8, so the hook operates on a private 16-byte context copy.

The same client moves the waypoint array to +0x305550. Its named registry entry
avoids a different system with an identical short prologue that the nearest-pattern
search can otherwise select.

## Runtime acceptance

On Steam client data revision 1076226, a local prototype containing this camera
reader and the broader community PR #4 changes was tested with Shroudtopia 0.1.1.
The user confirmed that movement and rotation both updated; diagnostic samples
also showed changing coordinates and heading over about 30 seconds.

This PR ports the camera reader onto upstream main and does not include PR #4's
other changes. The exact standalone PR build still needs an in-game test. Enter a
world, walk, turn the camera without moving, and check that the map follows both.
Restart and reload the world to check that tracking resumes. Older client layouts
and long sessions have not been runtime-tested with this patch.
