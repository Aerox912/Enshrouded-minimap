# Shroudweave Minimap 0.5.16

A client-side minimap for Enshrouded, with an HD map, live fog of war, player and location markers, and a weather clock. Draws inside the game's Vulkan frame. No server mod is needed.

Community-maintained by Aerox912, based on [elxokker's original Minimap](https://github.com/elxokker/Enshrouded-minimap). Original credits and MIT license are preserved. See [CREDITS.md](CREDITS.md).

## What's new

- Optional square map with a matching gold frame. Round north-up remains the default.
- Configurable glow for player arrows, pings and waypoints, plus a dark backplate and red rim for death markers. All glow options are enabled by default.
- Top-right is now the default for new configurations. Existing configured positions are retained.
- Renderer startup and frame-upload fixes, including the square terrain flag correction.
- GPU terrain and icon drawing, cached clock/weather geometry, fewer repeated config/cache operations, and controlled retries for unresolved marker positions.

These performance changes may help on affected systems. Hopefully they improve your FPS, but a gain is not guaranteed. [Full 0.5.16 notes](docs/releases/v0.5.16.md).

## Install or update

1. Close Enshrouded. Back up `mods/minimap_mod` somewhere outside `mods`, and back up `shroudtopia.json`.
2. Install the maintained [Shroudtopia Shroudweave loader](https://www.nexusmods.com/enshrouded/mods/216), following its instructions. It is a separate dependency, shared by several mods, so it has its own download and update page.
3. Download the complete Minimap 0.5.16 package from [Nexus](https://www.nexusmods.com/enshrouded/mods/143?tab=files) or [GitHub Releases](https://github.com/Aerox912/Enshrouded-minimap/releases). Extract it into the game folder so the DLL and assets are in `mods/minimap_mod`. Overwrite matching Minimap files when updating. The complete package includes the required images and shaders.
4. Keep your existing `shroudtopia.json` and other mods/settings. Edit the existing `mods.minimap_mod` section when changing options; do not add a second section with the same name.

This uses the Shroudtopia loader, not a native ShroudForge port. Creative Mode 3.0.0 compatibility guards remain in place. Do not replace a working multi-loader setup just to change Minimap settings.

## Appearance and placement

| Setting | Options |
|---|---|
| `map_shape` | `round` (default), `square` |
| `map_orientation` | `north-up` (default), `rotating`; square stays north-up |
| `position` | `top-right` (default), `middle-right`, `bottom-right`, `top-left`, `middle-left`, `bottom-left` |
| `offset` | Vertical adjustment relative to the chosen position. Default `0`; positive moves up, negative moves down. Scaled from 1080p and clamped to the screen. |
| `map_style` | `default` for the HD map, `original` for the original texture |
| `icon_style` | `original` for plain markers, `world-map` for gold frames and brighter backplates |
| `scale_percent` | `50`, `75`, `100` (default), `125`, `150`, `175`, `200` |

Square may need a small offset to clear the Journal or other HUD elements. `offset: -16` moved it down enough in one tested setup; adjust it for your display. The default remains `0`.

`player_arrow_glow`, `ping_glow`, `waypoint_glow` and `death_marker_glow` can each be set to `false`. For the first three, the corresponding `_style` setting accepts `same-colour` (default) or `contrasting`. Death markers use the dark backplate with a red rim. Existing arrow bodies and headings are retained. Check the Nexus Images tab for impressions of the map and icon styles.

## Controls

| Action | Default |
|---|---|
| Show/hide | F10 or numpad * |
| Zoom in/out | + / - or numpad + / - |
| Smaller/larger | F9 / F11 |

Change the key settings below in `shroudtopia.json`. Readable names such as `F8`, `Insert`, `Delete`, `numpadplus` and letters are supported, along with hexadecimal key values. Use `none` to disable a binding. Choose keys that do not conflict with your other mods.

## Default settings

This is the complete default Minimap section for 0.5.16. Merge it into an existing configuration rather than replacing other mods' settings. Existing configured values take precedence over the defaults.

```json
{
  "mods": {
    "minimap_mod": {
      "active": true,
      "position": "top-right",
      "offset": 0,
      "map_shape": "round",
      "map_orientation": "north-up",
      "toggle_key": "F10",
      "render_camera_fallback": true,
      "debug_logging": false,
      "show_other_players": true,
      "show_pings": true,
      "show_waypoints": true,
      "show_world_markers": true,
      "show_death_markers": true,
      "map_light": 55,
      "map_style": "default",
      "icon_style": "original",
      "heading_smoothing_ms": 55,
      "player_arrow_glow": true,
      "player_arrow_glow_style": "same-colour",
      "ping_glow": true,
      "ping_glow_style": "same-colour",
      "waypoint_glow": true,
      "waypoint_glow_style": "same-colour",
      "death_marker_glow": true,
      "map_sample_step": 2,
      "max_icons": 64,
      "show_clock": true,
      "show_weather": true,
      "show_fog_of_war": true,
      "toggle_key_secondary": "0x6A",
      "zoom_in_key": "0xBB",
      "zoom_in_key_secondary": "0x6B",
      "zoom_out_key": "0xBD",
      "zoom_out_key_secondary": "0x6D",
      "scale_percent": 100,
      "size_decrease_key": "F9",
      "size_increase_key": "F11"
    }
  }
}
```

## Current limits and troubleshooting

Native world-map POI filter selections are not yet mirrored completely on the minimap. Current-location/region labels and interior Hollow Halls maps are not included. Linux/Proton support and a universal FPS improvement are not established by this release.

If the map or frame is missing, check that the full package was extracted, including its images and shaders. Send the full `shroudtopia.log` from startup and a screenshot when reporting it. For performance reports, record FPS in the same spot with the map visible and hidden, after the toggle banner disappears.

The game-profile, HUD-visibility and Creative ownership guards remain enabled. Unsupported or unverified state can leave the minimap hidden.

## Credits

Original Minimap: elxokker (xoker). Community maintenance: Aerox912. Shroudtopia loader/API: s0T7x / Miguel Oppermann. Thanks to o-shabashov for marker-layout findings and display ideas, and to everyone testing the builds. Enshrouded and its game artwork belong to Keen Games. See [LICENSE](LICENSE) and [CREDITS.md](CREDITS.md).
