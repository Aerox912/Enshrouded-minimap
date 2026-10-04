# World clock

Version 0.5.2 adds a 24-hour world-time clock below the minimap. Its bronze
plate, cream digits and cyan end jewels follow the compass frame's palette.
Daylight uses a gold sun; night uses a blue-white crescent. The panel stays fixed
while the map rotates, and bottom-right placement reserves room for the footer.
`show_clock` is enabled by default and can be changed while playing.

## Time source

On Steam client data revision 1076226, `player_daytime_ui` (registry RVA
`0x1D437D0`, function `0x266980`) obtains a 0x28-byte iterator record. Slot +0x10
is the `g38_daytime::Daytime` object. The game's `isDaytime` routine at `0xCC3710`
compares current solar time at object +0x48 against sunrise +0x28 and sunset +0x30.
These are nanoseconds within a 24-hour day. The constructor at `0xC9CCF0` uses
06:00 sunrise, 18:00 sunset and 12:00 initial time; time conversion at `0xCAA880`
uses 86,400,000,000,000 nanoseconds per solar day. Current time is written at
`0xCE519A`. Real day/night durations at +0x38/+0x40 are not displayed as solar time.

The named UI system, named client time updater, iterator call and record slot,
daylight comparisons, day-length constant and time write must all match before
the hook is enabled. A mismatch disables only the clock. Its entry trampoline
copies 17 bytes spanning four whole instructions with no relative operands and preserves
the incoming arguments and stack. The callback initializes a private iterator
context, reads 40 bounded bytes at +0x28 and retains only the decoded minute and
daylight flag. No game-owned pointer reaches the Vulkan renderer.

Samples are captured at most ten times a second; time is never extrapolated
from Windows time. Invalid values are hidden, missing data expires after two
seconds, and the session generation prevents stale time surviving world exit.
The sun/moon transition uses the world's actual sunrise and sunset thresholds.

## Validation

The focused map-feature tests execute the production reader, callback and real
entry trampoline against synthetic data, and validate the layout against the
installed executable. They cover midnight, sunrise/sunset, changed daylight
thresholds, sleep/server time jumps, invalid pointers and times, stale data,
tick wrap, world exit, configuration toggles and changed executable layouts.
The production drawing commands are checked for a bounded command count,
sun and crescent pixels, all supported placements, and 480p through 2160p heights.
CPU previews use the bundled compass frame and actual clock renderer.

All 249 focused checks pass (17 tracking, 27 renderer, 58 live-marker and 147
map-feature checks, including previews and the installed executable). Screenshots
provided by the player on 4 October 2026 show 04:18 with the moon, 06:16 with the
sun, and later 06:39 with the sun. They confirm in-game placement, both icon
states and advancing displayed time. Sleep acceleration and re-entry are covered
by focused tests; those specific scenarios were not separately confirmed in game.
