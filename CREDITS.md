# Credits and license

## Original minimap

**elxokker (xoker)** created the Enshrouded Minimap on which this fork is based.
The minimap renderer, map assets, frame, original icon atlas and original mod
structure come from that project.

- [Original source](https://github.com/elxokker/Enshrouded-minimap)
- [Original Nexus mod](https://www.nexusmods.com/enshrouded/mods/97)

This is an independent community-maintained fork by **Aerox912**, not an official
update from elxokker. It includes the changes submitted upstream as
[PR #5](https://github.com/elxokker/Enshrouded-minimap/pull/5) and
[PR #6](https://github.com/elxokker/Enshrouded-minimap/pull/6).
Please report problems with this build to the fork's issue tracker.

## Other acknowledgements

- **s0T7x / Miguel Oppermann**: [Shroudtopia](https://github.com/s0t7x/shroudtopia),
  the required native mod loader and API.
- **o-shabashov**: marker-layout findings and display/graphics ideas in
  [upstream PR #4](https://github.com/elxokker/Enshrouded-minimap/pull/4).
  This fork retains its own named-camera reader and nonblocking GPU fences.
- **Keen Games**: Enshrouded and the underlying game map/icon artwork.
- Players who tested movement, rotation, multiplayer markers, pings, waypoints,
  map icons and custom pins, including the original-icon display option.

## License and provenance

This release is built from the GitHub source repository, which is published
under the MIT license. The unmodified [LICENSE](LICENSE), including its existing
copyright notice for Miguel Oppermann, is included in the source and binary ZIP.
The upstream license can be inspected at
[the source revision used by this fork](https://github.com/elxokker/Enshrouded-minimap/blob/ca881bcff534699ee7d2a068c2c351d1124f06f0/LICENSE).

The release is compiled from source rather than copied from the original Nexus
download. Third-party game artwork and trademarks remain the property of their
respective owners; this fork does not claim ownership of them. Shroudtopia and
Microsoft runtime installers are linked as requirements and are not bundled.
