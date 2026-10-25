# S1 Classic Gold square frame provenance

Task: MM-SQUARE-S1-IMPL-20261025
Disposition: main accepted this S1 artwork as immutable. No image pixels were changed or regenerated.

The accepted 1254x1254 PNG source has SHA-256
0560bcbd67bc90f1ca4bd468c91bc5413160f4f2d4849e0c735f7a69b4485176.
Its headerless RGBA8 row-major conversion, shipped as
mods/minimap_mod/embervale_minimap_square_frame.rgba, is 6,290,064 bytes and has
SHA-256 6d0da23c7910f00de966735bb37bcfac2201cb1e19adf85d2d1cebec81281d76.
Conversion reordered decoded BGRA memory channels to RGBA without resampling,
recoloring, or pixel cleanup.

Built-in ImageGen used the left S1 Classic Gold frame from the supplied concept
sheet as a style reference. The design retains the antique-gold rim, dark inner
keyline, corner diamonds and top N. It contains no terrain or map marker imagery.

Calibration found alpha-nonzero bounds [65,39]..[1189,1219]. At alpha>=128,
the solid silhouette is [77,66]..[1177,1163], or 1101x1098 pixels. The measured
clear aperture is 995x984 pixels; implementation uses the shorter calibrated
984-pixel aperture for a square map interior. The immutable art includes a
small asymmetry and 135 alpha-one pixels in its central 50 percent. The raw
file remains unchanged; the GPU upload copy alone discards alpha<=1. A square-only fragment shader discards UVs outside the calibrated solid rectangle (77,66) to (1178,1164), preserving the round fragment and common vertex shader.

The square renderer targets a 259x259 outer frame at 100 percent, aligned to
the measured inclusive clock/weather ornament and shadow bounds. At 50 and
200 percent the geometry is 130x130 and 518x518 pixels. Terrain and clipping
use explicit square math rather than inferring geometry from bitmap alpha.