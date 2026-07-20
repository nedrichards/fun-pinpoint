# Whole-codebase performance audit

Audit date: 20 July 2026

This audit covers the presentation parser and live reload, asset lookup,
audience and speaker rendering, media and camera pipelines, transitions, PDF
export, and shutdown. It complements the hardware Sysprof run in the central
[backlog](../TODO.md); it does not claim measurements that still require that
run.

The GNOME 50 SDK used for the audit contains GLib 2.88.2, GTK 4.22.4,
libadwaita 1.9.2, GStreamer 1.26.11, librsvg 2.62.3, Cairo 1.18.4, and Pango
1.57.1. The normal 14-test SDK suite and the GCC 15 `-fanalyzer`/strict-warning
gate pass. The existing page-curl benchmark remains comfortably bounded at
about 0.153 ms of CPU time per generated curved mesh on this machine; that
number does not include texture readback, upload, composition, or display
timing.

## Outcome

The ordinary GTK/GSK and live-media paths are fundamentally sound. Static text
and SVG render nodes are cached, ordinary transitions remain in GSK, and file
video and camera frames can stay in DMA-BUF or GL memory through
`gtk4paintablesink`. Pinpoint should not add its own DMA-BUF importer or force a
hardware decoder.

The main opportunities are around that rendering core:

1. Speaker previews discard useful caches on every navigation and the speaker
   timer wakes every 50 ms even before timing has started.
2. Asset paths, URIs, content types, decoded textures, and SVG sources are
   resolved or loaded repeatedly. The caches are stage-local, unbounded, and
   discarded on every presentation reload, even when the asset did not change.
3. Interactive PDF export blocks the GTK main thread. Video thumbnail selection
   forces full-size RGBA system-memory frames and copies every candidate before
   copying the winner again into Cairo form; decoded PDF images are retained
   until the whole export finishes.
4. Page curl deliberately performs a GSK texture to CPU to OpenGL round trip.
   Current public GTK APIs can import externally owned GL textures and DMA-BUFs,
   but do not expose a portable zero-copy import of an arbitrary
   `gsk_renderer_render_texture()` result into a `GtkGLArea`. This remains a
   measured hardware problem, not an invitation to use renderer internals.

## Implementation status

The first production batch landed with this audit. Speaker previews now select
slides without resetting their presentation, and the 50 ms timing source is
owned only while timing is active. The follow-up asset-store batch decodes and
prefetches current/adjacent raster images through cancellable `GTask` workers.
Audience and previews share a 64 MiB RGBA-equivalent decoded-texture LRU and one parsed librsvg
handle per source while retaining size-specific vector nodes. Completed entries
survive text-only reloads; obsolete task results are discarded on reload or
final stage shutdown. Every referenced image, SVG, video, or legacy transition
file has a `GFileMonitor`, so replacement invalidates the matching render/media
state without reparsing the `.pin` file.

Interactive PDF export now runs outside GTK's main thread, while CLI export
remains synchronous. Video thumbnail candidates are scored directly in their
mapped `GstVideoFrame`; only the selected `GstSample` is converted to a pixbuf.
The remaining PDF work is export cancellation in the UI, an export-resolution
bound, and bounded decoded-image retention. Those remain in the central backlog.

## Copy and acceleration map

| Path | Current behaviour | Assessment |
| --- | --- | --- |
| Presentation source | `g_file_load_contents()` allocates the input and the parser duplicates it into the retained source. Defaults strings are copied into each slide. | Real copies, but normally small and not a priority. |
| Asset classification | Up to 4 KiB is opened and read for every slide background during every parse, including repeated references to the same file. | Redundant I/O; classify each unique resolved asset once. |
| Asset identity in snapshots | Image and video snapshots repeatedly create a `GFile`, allocate its URI, and hash that URI, including once per video frame. | Avoidable main-thread allocation in a hot path. Store resolved asset identity in a presentation asset record. |
| Raster image display | Cancellable `GTask` workers deduplicate and prefetch current/adjacent `GdkTexture` objects into a shared 64 MiB RGBA-equivalent decoded-size LRU; GSK scales them without an application-side per-frame copy. | Keep. The bundled raster set is about 7.15 MiB by the same estimate, while eight 4K RGBA images would be about 253 MiB, validating the byte rather than item bound. |
| SVG display | The shared store parses one librsvg handle per URI; stages record size-specific Cairo/GSK nodes from that immutable source. | Keep. This preserves vector output quality while avoiding repeated parsing across audience and previews. |
| Text display | Pango shaping and the GSK text node are cached per slide, size, scale, and Pango context serial. | Keep. There is no useful application-side glyph rasterisation to add. |
| Ordinary transitions | GSK transform and opacity nodes wrap the cached foreground components. | Keep on GSK; no CPU readback is present. A whole-static-slide node cache is only worth considering after traces show wrapper construction matters. |
| File video | `playbin3` feeds `gtk4paintablesink`; no RGB filter or application map/copy is inserted. | Already zero-copy eligible. The GNOME 50 sink advertises DMA-BUF, GLMemory, native YUV system memory, and RGB fallbacks. |
| Camera | `pipewiresrc` links directly to `gtk4paintablesink`. | Already zero-copy eligible where PipeWire, driver, sink, and compositor modifiers agree. Keep system-memory fallback. |
| Speaker previews | Three independent `PpStage` objects call `pp_stage_set_presentation()` on every slide change, clearing all three texture, text, SVG, media, and transition caches. | Highest-confidence redundant work. Retain the presentation and move each preview to a slide without resetting it; share immutable asset data. |
| Speaker timing | A 50 ms GLib timeout formats a new string, sets a label, and queues a Cairo progress redraw for the lifetime of the speaker object, including idle and hidden states. | Unnecessary 20 Hz wakeups. Run only while timing/autoadvance requires it, and stop while paused or idle. |
| PDF still images | GdkPixbuf decode is followed by a full straight-alpha/RGB to premultiplied Cairo copy. JPEG source bytes are also retained so Cairo can embed the original JPEG. | The format conversion is legitimate for Cairo/PDF; unbounded all-export retention and main-thread execution are not. |
| PDF video thumbnails | Appsink requires RGBA system memory. Each of up to four full-resolution candidates is copied out of its mapped `GstBuffer`; the selected frame is copied and premultiplied again for Cairo. | GPU zero copy is the wrong goal for CPU/PDF output. Score mapped frames in place, retain only the winning sample, scale to an export-appropriate bound, and convert the winner once. |
| Page curl | Two GSK slide nodes are rendered to textures, downloaded into CPU bytes, uploaded with `glTexImage2D()`, and then composed back into GTK through `GtkGLArea`. Source and uploaded textures coexist for the transition. | The only definite interactive GPU round trip. Preserve the current compatible path until hardware traces justify a PBO or architectural experiment. |

## Recommended implementation sequence

### 1. Remove speaker idle work and cache resets

Add a no-transition slide-selection operation for preview stages. When the
presentation pointer has not changed, navigation must not clear any cache.
Create the timing source when timing begins and remove it when stopped or
paused; keep sufficient precision for autoadvance without repainting unchanged
labels. Add tests that assert idle speaker mode owns no repeating source and
that navigating previews does not reload an unchanged texture.

This is low risk, does not change output, and should be the first performance
implementation.

### 2. Introduce a watched, bounded presentation asset store

Implemented on 20 July 2026 with cancellable asynchronous raster prefetch,
shared parsed SVG sources, targeted re-prefetch after invalidation, and a
64 MiB RGBA-equivalent decoded-texture LRU.

Resolve every unique background to a `GFile` and stable key once. Store content
classification and immutable decoded sources there, and let the audience and
speaker stages share them. Monitor the presentation directory with
`GFileMonitor`; use individual monitors only for assets outside it. A completed
change should invalidate the matching image, SVG, or media entry and queue one
redraw. This both avoids polling/re-resolution and makes edits to an image or
SVG visible without touching the `.pin` file.

Preserve unchanged asset entries across text-only presentation reloads. Use a
small current/adjacent working set or a byte budget rather than retaining every
large texture ever visited. Load and prefetch large `GdkTexture` objects with a
`GTask`: GTK explicitly documents `gdk_texture_new_from_file()` as threadsafe,
and `GdkTexture` is immutable and safe to return to the main thread.

Tests should cover one decode for repeated references, cache reuse across the
audience and three previews, invalidation after replacement/rename, bounded
eviction, cancellation on reload/shutdown, and fallback when monitoring is not
available.

### 3. Make interactive PDF export asynchronous and memory-bounded

Keep the command-line export synchronous, but run launch-screen export work in
a `GTask` with cancellation and return only completion/error to GTK. Do not
touch widgets from the worker.

For thumbnails, score directly from each mapped `GstVideoFrame`, retain the
best `GstSample`, and convert only the winner. Negotiate or scale to a documented
export-quality bound instead of always retaining a source-resolution RGBA
frame. Release image surfaces after their final slide or use a bounded cache so
a long image-led deck does not retain every decoded background simultaneously.
The PDF path needs CPU/vector data; forcing DMA-BUF or hardware decode here
would usually add a readback rather than remove one.

### 4. Prototype modern GTK acceleration behind evidence gates

Stable video and camera backgrounds now use a dedicated paintable child inside
`GtkGraphicsOffload`. GTK can pass a compatible DMA-BUF directly to the Wayland
compositor and automatically falls back to GSK when a format, colour state,
clip, transform, opacity, overlay, transition, or platform prevents offload.
Pinpoint's text and shading remain in their existing GSK path, transitions
explicitly disable the offload child, and physical-pixel alignment avoids an
otherwise common fractional-scale rejection.

A live `GDK_DEBUG=offload,dmabuf` run confirmed a Wayland subsurface and NV12
DMA-BUF negotiation from `vavp9dec` through `gtk4paintablesink`. It also made the
limits concrete: window chrome and slide overlays lower the candidate, and the
current compositor rejected the unobscured fullscreen frame because it lacked
colour management for the video's non-default colour state. The implementation
therefore remains opportunistic and must not be described as universal direct
scanout. Sysprof display timings and battery measurements remain part of the
separate hardware-validation task.

GTK 4.22's `GtkSvg` is a GTK-native SVG paintable and is worth a focused
prototype for self-contained static SVGs. It implements a documented subset of
SVG, while Pinpoint currently relies on librsvg for broad historical
compatibility and external-resource handling. Use it only if the compatibility
and pixel suites can select a reliable fast path and retain librsvg fallback.

The renderer now uses GTK 4.22's `gtk-interface-reduced-motion` preference in
addition to the older all-animations switch when deciding whether to suppress
slide motion. It emits one informational message per process when that path is
first used, making the intentional immediate slide change visible in bug
reports. This is primarily an accessibility modernization, with the incidental
benefit of eliminating transition work for users who request reduced motion.

### 5. Keep page-curl experiments tied to hardware traces

The current download is explicitly expensive in GTK's API contract. A
`GdkGLTextureBuilder` or `GdkDmabufTextureBuilder` constructs a GDK texture from
an externally owned resource; neither extracts a portable GL name or DMA-BUF
from the texture returned by GSK. The deprecated `GskGLShader` path is also not
an option because GTK's Vulkan-focused renderer no longer supports it.

During the deferred hardware run, measure readback/upload time, peak texture
residency, frame misses, and colour consistency at 1x and 2x. If the round trip
is material, first test releasing the source GSK textures after successful GL
upload and downloading into a reusable or persistently mapped pixel buffer.
Move deformation to a vertex shader or attempt deeper resource sharing only if
those smaller changes are insufficient and the software-compatible fallback
remains intact.

## Deliberate non-changes

- Do not force a VA-API decoder, RGB conversion, GLMemory, or DMA-BUF. GStreamer
  negotiation has more information about the decoder, modifiers, and devices.
- Do not replace `gtk4paintablesink` with hand-written GDK DMA-BUF import code.
- Do not rasterise text or SVGs into application-owned textures merely to call
  the result "GPU accelerated".
- Do not move PDF generation to the GPU; PDF export ultimately requires CPU
  data and vector output.
- Do not adopt deprecated GSK GL shaders or private renderer APIs for page curl.
- Do not retain an unbounded texture cache to make revisiting every slide
  instant. Presentation decks can contain enough 4K images to exhaust memory.

## Primary API references

- [GTK threading and immutable render objects](https://docs.gtk.org/gtk4/section-threading.html)
- [`GdkTexture`](https://docs.gtk.org/gdk4/class.Texture.html)
- [`GtkGraphicsOffload`](https://docs.gtk.org/gtk4/class.GraphicsOffload.html)
- [`GtkSvg`](https://docs.gtk.org/gtk4/class.Svg.html)
- [`GtkSettings:gtk-interface-reduced-motion`](https://docs.gtk.org/gtk4/property.Settings.gtk-interface-reduced-motion.html)
- [`GdkTextureDownloader`](https://docs.gtk.org/gdk4/struct.TextureDownloader.html)
- [`GdkGLTextureBuilder`](https://docs.gtk.org/gdk4/class.GLTextureBuilder.html)
- [`GdkDmabufTextureBuilder`](https://docs.gtk.org/gdk4/class.DmabufTextureBuilder.html)
- [`GtkGLArea`](https://docs.gtk.org/gtk4/class.GLArea.html)
- [`gtk4paintablesink`](https://gstreamer.freedesktop.org/documentation/gtk4/)
- [`GFileMonitor`](https://docs.gtk.org/gio/class.FileMonitor.html)
