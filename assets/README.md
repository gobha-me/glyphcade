# Art assets

Committed PNGs are the build input. Their authoring prompt, tool, dimensions and
SHA-256 live in `manifest.json`; a manifest alone is never enough, because image
generation is neither offline nor reproducible across model versions.

`glyphcade_embed_asset()` turns a PNG into target-private byte storage at build
time. `glyphcade::assets::decode_png()` decodes that span once into a TermForge
`Image`; rendering code retains the image and never decodes or allocates per
frame. There is deliberately no committed `.rgba` copy: it is larger, opaque to
review, and derivable from the PNG.

The cell presentation remains the specification. Every asset-backed widget
authors an information-complete Baseline in `draw()` and adds the image through
TermForge's pixel-region path. Kitty receives native graphics, ANSI truecolour
receives half-block raster, and Baseline keeps the authored cells.

This first slice defines built-in storage only. A custom card-pack format,
filesystem discovery, installation, deck selection and RasterForge integration
are deferred; RasterForge may later author the same format but will not become a
runtime or build dependency.
