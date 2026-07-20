# Performance and efficiency

Pinpoint keeps repeatable performance, idle-work, and package-size checks in
the normal GNOME 50 SDK test suite. Synthetic measurements are treated as
regression gates, not as substitutes for battery and GPU measurements on real
hardware.

## Automated gates

- Page-curl mesh generation builds 600 curved 1920×1080 frames within 900 ms
  of CPU time. This allows substantial headroom on slower builders while
  catching accidental extra deformation, sorting, or allocation work.
- Completed transitions must remove their GTK frame-clock callback. A static
  slide therefore has no transition-driven redraw loop.
- An idle or paused speaker view must own no 50 ms timing source. Asset-store
  tests exercise asynchronous current/adjacent raster prefetch, sharing across
  audience/preview stages, reload and shutdown cancellation, one parsed SVG
  source across output sizes, a 64 MiB RGBA-equivalent decoded-texture budget,
  and redraw after
  targeted invalidation. An atomic file-replacement test covers live monitors.
- The SDK-built executable, including embedded presentation assets, must remain
  at or below 3 MiB. The bundled VP9/Opus introduction video has a separate
  1.4 MiB budget.
- Renderer pixel checks cover 800×600, 1280×720 widescreen, and 800×600 at 2×
  scale. They verify stage dimensions, background color, text, shading, and
  cached vector SVG rendering. A separate SVG comparison renders the existing
  fixture through both librsvg and GTK 4.22's `GtkSvg`, checks bounded pixel
  differences, and exercises GTK's structured unsupported-feature errors.
- AddressSanitizer, UndefinedBehaviorSanitizer, and LeakSanitizer cover parser,
  media, transition, page-curl, and shutdown lifecycles. Leak suppressions are
  limited to documented process-lifetime Fontconfig and Mesa EGL display
  configuration caches.

Run the full display-backed suite inside the pinned SDK:

```sh
flatpak run --user --filesystem="$PWD" \
  --device=dri --socket=wayland --socket=fallback-x11 --command=meson \
  org.gnome.Sdk//50 test -C "$PWD/_build" --print-errorlogs
```

To print the page-curl timing and current size figures directly:

```sh
flatpak run --user --filesystem="$PWD" \
  --command="$PWD/_build/tests/test-performance" org.gnome.Sdk//50 --verbose
flatpak run --user --filesystem="$PWD" \
  --command="$PWD/_build/tests/test-size" org.gnome.Sdk//50 \
  "$PWD/_build/src/pinpoint" "$PWD/data/introduction/bunny.webm" --verbose
flatpak run --user --filesystem="$PWD" \
  --device=dri --socket=wayland --socket=fallback-x11 \
  --command="$PWD/_build/tests/test-svg-renderers" org.gnome.Sdk//50 \
  "$PWD/tests/fixtures/svg-quality.svg" --verbose
```

`tests/run-leak-checks.sh` still compiles entirely in the GNOME 50 SDK, then
runs the sanitizer binaries on the host with libraries from that exact SDK so
LeakSanitizer can inspect them.

See [the Wayland-first rendering pipeline](rendering-pipeline.md) for media
caps diagnostics, text and image quality choices, and the remaining page-curl
readback boundary.

The [whole-codebase performance audit](performance-audit.md) maps every known
copy boundary. Its production work removes speaker idle work, asynchronously
prefetches watched and byte-bounded raster assets, shares parsed SVG sources,
moves interactive PDF export off the GTK thread, and scores thumbnail
candidates without copying them. Stable media backgrounds now use GTK's
opportunistic compositor-offload path with automatic GSK fallback. PDF export
now has cancellation and bounded raster memory, and the GTK-native SVG
comparison concluded that the cached librsvg path should remain primary.

## Page-curl work avoided

The renderer deforms each of the 1,089 mesh vertices once and reuses its depth
when ordering triangles. Rotation sine and cosine are calculated once per
page, not once per vertex. A flat page uses a static GPU vertex/index mesh, so
it performs no per-frame deformation, sorting, or buffer upload.

## Hardware validation procedure

The central [Pinpoint backlog](../TODO.md) owns the status and scheduling of the
outstanding hardware run. When performing it, profile page-curl frame pacing on
representative integrated and discrete GPUs and run a long presentation on
battery power. Persistent mapped GL buffers should only be added if those
traces show that the remaining single curved-page buffer update is material.
Multi-display audience/speaker behaviour remains a physical-hardware release
check after material GTK, GNOME, or display-assignment changes.
