# Wayland-first rendering pipeline

Wayland is Pinpoint's primary presentation platform. GTK's Wayland backend and
the compositor own presentation and display timing; X11 remains available
through `fallback-x11` for compatibility, but it must not constrain the
preferred media formats or rendering path.

The pipeline has two equally important goals:

- preserve presentation quality, including sharp text at fractional and HiDPI
  scales, vector SVG output, suitable image filtering, and correct colour;
- avoid unnecessary decoding, colour conversion, CPU readback, rasterisation,
  texture upload, and redraw work.

## Current paths

### Video and camera

File video uses GStreamer's `playbin3` with `gtk4paintablesink`. `playbin3`
selects demuxers and decoders from the installed runtime, including ranked
hardware decoders. The GNOME 50 runtime's sink accepts DMA-BUF, GL textures,
native YUV formats, high-bit-depth YUV, and RGB system memory. Pinpoint does not
force an RGB caps filter or a software decoder.

The camera portal supplies a PipeWire remote to `pipewiresrc`, which now links
directly to `gtk4paintablesink`. This lets PipeWire and the sink negotiate a
shared DMA-BUF or native YUV format where the driver and compositor support it;
an unconditional `videoconvert` no longer forces an intermediate processing
stage. PipeWire's buffer pool remains enabled and its `always-copy` option
remains disabled by default.

Hardware decoding is opportunistic rather than mandatory. It requires the
appropriate runtime plugin, a decoder with a sufficient GStreamer rank, a
compatible stream, and access to the render device. The production Flatpak's
`--device=dri` permission provides that device access.

The current Intel/Wayland validation machine negotiated `vavp9dec` output as
NV12 DMA-BUF directly into `gtk4paintablesink`, with `GdkWaylandDisplay` and
`GskVulkanRenderer` at 2× scale. An intentional `GDK_DISABLE=dmabuf` comparison
kept VA-API decoding but negotiated system-memory NV12 through
`GskGLRenderer`. These observations prove both paths on that machine; they are
not hard-coded requirements for other drivers.

### Still images and SVG

Raster images are loaded once as immutable `GdkTexture` objects and cached for
the presentation. GDK retains their colour-state metadata. Rendering uses an
explicit linear filter when enlarging and a trilinear filter when reducing an
image, avoiding the aliasing produced by single-level minification.

SVG backgrounds are loaded through librsvg and cached as `GskCairoNode` vector
recordings for each slide and stage size. They are not decoded to a texture at
the SVG's nominal width and then enlarged. The renderer can therefore rasterise
the vector content for the actual output scale while repeated video-frame
snapshots reuse the same node.

### Text and ordinary transitions

Text remains a Pango layout represented by GTK/GSK text render nodes. Pinpoint
does not draw glyphs into an application-owned bitmap. Each slide and stage
size now shapes and measures its text once, caches the resulting node and
geometry, and reuses it for shading and drawing. This removes duplicate Pango
work and prevents a video background from reshaping static foreground text for
every frame.

Ordinary transitions apply GSK transform and opacity nodes around the live
background, shading, and text nodes. Text therefore remains text until the GSK
renderer composites the frame.

### Page curl

Page curl is the deliberate exception. It flattens each participating slide to
a `GdkTexture` once at transition start, downloads that texture, and uploads it
to the `GtkGLArea` used by the curl mesh. The textures are now captured at the
widget's physical scale factor, so text and SVG content remain sharp on HiDPI
outputs, but the one-time GPU-to-CPU-to-GPU transfer remains.

Removing that transfer would require a renderer-specific GL, Vulkan, or
DMA-BUF import path, or replacing the custom GL area with a GSK-native effect.
That change should be guided by the deferred hardware profiles because it has
substantially more portability and synchronization risk than the ordinary GSK
pipeline.

## Observing the negotiated path

Set the `pinpoint-media` debug domain to report the active GDK backend, GSK
renderer, output scale, negotiated video caps and memory features, and the
elements selected by `playbin3`:

```sh
G_MESSAGES_DEBUG=pinpoint-media _build/src/pinpoint presentation.pin
```

For a Flatpak build:

```sh
flatpak run --env=G_MESSAGES_DEBUG=pinpoint-media \
  com.nedrichards.pinpoint presentation.pin
```

A preferred video path reports `memory:DMABuf` or `memory:GLMemory` at the GTK
sink and a hardware decoder such as a `va*dec` element. System-memory caps are
valid fallbacks, not proof of a bug: codec, driver, modifier, renderer, and
cross-device compatibility can all require a copy.

Useful GTK diagnostics include `GSK_DEBUG=renderer,fallback,cache` and
`GDK_DISABLE=dmabuf` for an intentional comparison run. Do not ship either as
an application default.

## Release checks

On representative Wayland hardware:

1. Confirm the log names `GdkWaylandDisplay` and the expected Vulkan or GL GSK
   renderer.
2. Exercise VP9/Opus, H.264/AAC, camera, large JPEG/PNG, SVG, marked-up text,
   and page curl at 1× and 2× scale.
3. Record negotiated caps and selected decoder elements; compare DMA-BUF or GL
   paths with the deliberate system-memory fallback.
4. Check text edges, SVG curves, image minification, colour and video levels on
   the actual projector or external display, not only a screenshot.
5. Use Sysprof to investigate frame misses or copies before adding
   renderer-specific resource sharing.

Primary API references:

- [GTK on Wayland](https://docs.gtk.org/gtk4/wayland.html)
- [`gtk4paintablesink`](https://gstreamer.freedesktop.org/documentation/gtk4/)
- [GStreamer hardware decoding](https://gstreamer.freedesktop.org/documentation/tutorials/playback/hardware-accelerated-video-decoding.html)
- [GStreamer DMA-BUF negotiation](https://gstreamer.freedesktop.org/documentation/additional/design/dmabuf.html)
- [`GdkTexture`](https://docs.gtk.org/gdk4/class.Texture.html)
- [`gtk_snapshot_append_scaled_texture()`](https://docs.gtk.org/gtk4/method.Snapshot.append_scaled_texture.html)
- [`gtk_snapshot_append_layout()`](https://docs.gtk.org/gtk4/method.Snapshot.append_layout.html)
