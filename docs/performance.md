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
now has cancellation and bounded raster memory, retains JPEG compression after
downscaling, and deduplicates revisited raster assets without retaining their
decoded surfaces. The GTK-native SVG comparison concluded that the cached
librsvg path should remain primary.

## Page-curl work avoided

The renderer deforms each of the 1,089 mesh vertices once and reuses its depth
when ordering triangles. Rotation sine and cosine are calculated once per
page, not once per vertex. A flat page uses a static GPU vertex/index mesh, so
it performs no per-frame deformation, sorting, or buffer upload.

## Repeatable Sysprof workloads

`test-lifecycle` has two profiling-only entry points. Both maximize the real
stage, keep the GTK main loop uninterrupted during transitions, record frame
clock intervals, and bracket the workload with idle baselines. They are not
part of the normal test run, so profiler overhead cannot make the release gate
flaky.

The page-curl entry point performs two complete forward/backward passes over
the six-slide fixture:

```sh
tests/run-sysprof-profile.sh \
  "$PWD/_build/profiling/page-curl.syscap" \
  --profile-page-curl "$PWD/tests/fixtures/page-curl.pin"
```

The general entry point takes a presentation, forward/backward cycle count,
dwell time, and optional inclusive one-based slide range. This example covers
the introduction's first 21 fade slides once with a 100 ms dwell:

```sh
tests/run-sysprof-profile.sh \
  "$PWD/_build/profiling/introduction-fades.syscap" \
  --profile-presentation "$PWD/data/introduction/introduction.pin" \
  1 100 1 21
```

Set `PINPOINT_PROFILE_GSK_RENDERER=gl` or `vulkan` to compare GSK renderers.
Omit it to exercise GTK's normal choice. The runner enables Sysprof CPU,
scheduler, speedtrack, GNOME Shell, RAPL, and memory providers and launches the
SDK workload separately from the harmless host keepalive. Profiling `flatpak
run` as Sysprof's child records only the short-lived launcher, while Sysprof
49's old `--pid` option is ignored. Select the non-`bwrap` `test-lifecycle`
process root when inspecting the system-wide capture.

The installed 0.1.8 Flatpak can be driven over the same range through AT-SPI:

```sh
tests/run-legacy-sysprof-profile.sh \
  "$PWD/_build/profiling/legacy-introduction-fades.syscap" \
  "$PWD/data/introduction/introduction.pin" 20 0.9
```

Its 20 right and 20 left key presses correspond to slides 1–21 and back. The
0.9-second interval matches the current 800 ms fade plus 100 ms dwell, but the
legacy program has no application-side frame metric. Compare its process CPU
samples and GNOME Shell presentation marks, and allow for AT-SPI/startup noise.
Captures stay under the ignored `_build` directory because they include host
metadata and environment details.

### 21 July 2026 integrated-GPU result

The first repeatable run used the Framework laptop's Intel Iris Xe integrated
GPU, its internal eDP panel, and the GNOME 50 SDK. The maximized stage was
1503×931 logical pixels at 2× scale, so each captured slide texture was
3006×1862 physical pixels. Twenty forward/backward transitions completed
without a runtime or GL error.

The `test-lifecycle` callgraph contained 6,891 samples. The page-curl snapshot
path accounted for 3,559, the GL-area render callback for 2,213, mesh generation
for 1,148, texture upload for 803, and GDK texture download for 470. These are
sample counts for locating cost, not timing budgets. The compositor's trimmed
28.6-second active window averaged 17.38 ms between presentations; 29 intervals
exceeded 20 ms, 14 exceeded 34 ms, and the longest was 100.02 ms. This confirms
that the one-time readback/upload boundary can miss frames at this HiDPI size.

A follow-up experiment retained GL texture storage and used
`glTexSubImage2D()` for later transitions. It did not materially improve the
normalized callgraph: upload moved from 803/6,891 samples to 828/7,380 and
download from 470/6,891 to 494/7,380. The experiment was therefore reverted.
The driver still has to stage each new pair of roughly 21.4 MiB RGBA textures;
reusing the destination allocation does not remove that transfer.

The capture was made on wall power. The system battery provider reported
`discharging` at its configured charge threshold, so that state must not be
used to classify the power source. Whole-system RAPL samples averaged 5.65 W
for the package and 0.78 W for graphics, peaking at 13.61 W and 1.11 W
respectively. These figures establish that the providers work; they are not a
battery-life claim. A long representative presentation on battery remains
necessary.

### Introduction renderer and legacy comparison

The introduction's first 21 slides isolate the default fade while retaining
representative text, images, and the video slide. At 1503x931 logical pixels
and 2x scale, repeated GTK 4.22 Vulkan runs averaged 31.53 ms per application
frame (about 32 fps) and collected 12,226–12,444 process CPU samples. Repeated
GL runs averaged 17.06–17.07 ms (about 59 fps) and collected 4,493–5,224
samples. GL therefore fixes the visible HiDPI throughput problem and more than
halves CPU samples for this workload.

A complete 33-slide introduction pass in each direction confirmed that the
result is not peculiar to the fade-only range: GL produced 3,319 application
frames at a 17.54 ms average and 9,951 CPU samples, versus Vulkan's 2,144
frames at 27.68 ms and 19,724 samples. A three-slide range around the video
negotiated the same 24 fps VP9 `video/x-raw(memory:DMABuf)` NV12 path and
configured `GtkGraphicsOffload` under both renderers. GL delivered 192 frames
versus Vulkan's 113 over that run. Pixel fixtures at 800x600, widescreen, and
2x scale also remained unchanged.

The page-curl trade-off is different. GL's 20-transition run averaged 18.50 ms
but collected 7,674 CPU samples, 11% more than the original 6,891-sample
Vulkan capture, and included several large setup stalls. The exact Tiger Lake
Iris Xe PCI device tested here (`8086:9a49`) therefore defaults Pinpoint to GL,
while every other GPU retains GTK's renderer choice and an explicit
`GSK_RENDERER` always wins. A live launch proved both the automatic GL choice
and an explicit Vulkan override at 2x scale. This is a targeted workaround for
the tested GTK/driver combination, not a claim that GL is universally faster.

The installed Pinpoint 0.1.8 Flatpak is a meaningful CPU reference, not merely
a compatibility oracle. Three controlled passes collected 200–1,070 process
samples, substantially below the GTK4 GL runs even after allowing for its
lower roughly 35–37 fps compositor rate and sampling/startup variance. The
[historical 0.1.8 source](https://download.gnome.org/sources/pinpoint/0.1/pinpoint-0.1.8.tar.xz)
explains the direction of the result: it creates persistent Clutter background
and text actors for each slide and animates their properties, while the GTK4
stage currently reconstructs the outgoing and incoming slide node composition
for every frame. CPU sample totals are not elapsed time and the legacy
automation cannot provide the current harness's
application-side frame metric, so this range must not be presented as an exact
percentage advantage.

Two retained-node experiments did not yet recover that advantage safely. A
cached whole-slide node cut CPU samples by about 27% but GTK's native crossfade
changes the historical independent incoming/outgoing opacity curve. Reusing
static whole-slide nodes with the existing fade instead raised samples and
introduced 300–500 ms stalls. Both were reverted. The remaining goal is a
stable retained composition that preserves the authored transition semantics;
the renderer workaround does not close that architectural gap.

## Hardware validation procedure

The central [Pinpoint backlog](../TODO.md) owns the status and scheduling of the
outstanding hardware run. When performing it, profile page-curl frame pacing on
representative integrated and discrete GPUs and run a long presentation on
battery power. Persistent mapped GL buffers should only be added if those
traces show that the remaining single curved-page buffer update is material.
Multi-display audience/speaker behaviour remains a physical-hardware release
check after material GTK, GNOME, or display-assignment changes.
