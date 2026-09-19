# GNOME 51 runtime review

Pinpoint targets the GNOME 51 Flatpak runtime. The reviewed SDK contains GLib
2.90.0, GTK 4.24.0, libadwaita 1.10.0, GStreamer 1.28.6, librsvg 2.63.0,
Pango 1.58.2, and GtkSourceView 5.21.0.

## Compatibility changes

GTK 4.24 no longer permits direct inclusion of `gdkkeysyms.h`; source and test
code include the supported `gdk.h` umbrella header. The composition editor now
hosts its completion popover in `GtkPopoverBin`, the GTK-provided container for
popover ownership, instead of parenting the popover directly to
`GtkSourceView`.

A build with deprecated GTK, GDK, libadwaita, GdkPixbuf, and GLib APIs hidden
passes. No other source migration is required by the new runtime.

GTK 4.24's Vulkan renderer can report duplicate Wayland presentation feedback
with timestamps one refresh interval apart while the editor test animates its
completion popover. The same class of warning is reproducible in GTK's own demo
and other GNOME applications. The editor lifecycle test therefore selects
Cairo while keeping warnings fatal; the pixel, renderer, lifecycle, and
application suites continue to cover the normal GPU renderer selection,
including Vulkan.

## API decisions

- Keep the cached librsvg slide-background path. `GtkSvgWidget`'s interactive
  SVG support does not fit static presentation assets, and the existing
  renderer comparison still protects fidelity and parse/snapshot cost.
- Use GTK 4.24's renderer cache, frame-delivery, fractional-scale, SVG, and
  reduced-motion improvements through the normal widget and snapshot APIs.
  Pinpoint does not duplicate those internals.
- Keep the exact Tiger Lake GL renderer workaround until the display-backed
  Sysprof workloads are repeated on GNOME 51. The CPU-only comparison below
  cannot justify changing a GPU policy.
- Do not adopt GTK 4.24 application-state restoration yet. The API is present,
  but GTK still labels it unstable. The privacy and lifecycle design remains
  valid and should be implemented after the API stabilizes, unless the project
  deliberately accepts an unstable runtime-only contract.

## Baseline comparison

Twenty consecutive runs of the unchanged page-curl CPU gate produced these
means on the same host:

| SDK | Toolchain | 600 meshes | Per frame |
| --- | --- | ---: | ---: |
| GNOME 50 | GTK 4.22.4, GCC 15.2 | 27.352 ms | 0.046 ms |
| GNOME 51 | GTK 4.24.0, GCC 16.2 | 27.943 ms | 0.047 ms |

The GNOME 51 mean is 2.16% slower, while individual runs overlap substantially
(GNOME 50: 25.23–31.18 ms; GNOME 51: 26.02–30.84 ms). This demonstrates no
material CPU improvement or regression. It is a compiler-and-runtime baseline,
not an isolated GTK benchmark.

GTK 4.24 advertises rendering and frame-delivery improvements, but this
CPU-only gate cannot measure them. Repeat the existing page-curl and
introduction Sysprof workloads on the same display, scale, renderer, and power
state before claiming a GPU, frame-pacing, or energy improvement.
