# Pinpoint

Pinpoint helps hackers give excellent presentations. Write down the core ideas
as concise plain text in the editor of your choice; Pinpoint turns them into
big, image-led slides and reloads them live while you tune the source. Less
text makes for a happier audience.

This is a GTK 4 and libadwaita rebuild of the original Clutter-based Pinpoint.
It reads the same `.pin` presentation format and retains the original visual
and interaction model while running cleanly on current Wayland desktops and
inside a Flatpak sandbox.

This repository is under active reconstruction. The compatibility parser,
GTK/GSK slide renderer, image and SVG backgrounds, GStreamer video playback,
portal-backed camera slides and file opening, live reload, navigation, speaker
and rehearsal views, embedded sandbox commands, PDF video thumbnails, legacy
ClutterState transition JSON, and PDF export are working.
See [the presentation format reference](docs/presentation-format.md) for the
complete `.pin` syntax, [the compatibility ledger](docs/compatibility.md) for
remaining parity work, and [the product backlog](TODO.md) for planned
user-facing improvements.

## Build

All builds and tests use the GNOME 50 Flatpak SDK. This keeps Meson, GTK,
libadwaita, GStreamer, and the compiler consistent with the packaged app rather
than depending on the host distribution.

```sh
flatpak run --user --filesystem="$PWD" --command=meson \
  org.gnome.Sdk//50 setup "$PWD/_build"
flatpak run --user --filesystem="$PWD" --command=meson \
  org.gnome.Sdk//50 compile -C "$PWD/_build"
flatpak run --user --filesystem="$PWD" --device=dri \
  --socket=wayland --socket=fallback-x11 --command=meson \
  org.gnome.Sdk//50 test -C "$PWD/_build" --print-errorlogs
```

Do not use the host Meson or compiler for release validation. If the SDK is
installed system-wide rather than per-user, omit `--user`.

The suite includes bounded page-curl CPU work, transition-idle checks,
executable and media size budgets, and display-backed pixel profiles. The
display tests run automatically when the SDK has a Wayland or X11 socket; the
page-curl test also requires the explicitly granted GPU device. Without a
display they report a skip. See [performance and efficiency](docs/performance.md)
for the budgets and a display-enabled command, and see the
[Wayland-first rendering pipeline](docs/rendering-pipeline.md) for video,
camera, image, SVG, text, colour, and hardware-acceleration details. The
[media-format policy](docs/media-formats.md) defines the portable video and
audio combinations covered by the production Flatpak and its fixtures. See
[accessibility](docs/accessibility.md) for stage and speaker-view semantics,
keyboard and reduced-motion behaviour, and the remaining authoring limitation.
The [remote-control architecture](docs/remote-control.md) documents the shared
presentation actions, idle-inhibit contract, and transport evaluation plan.

### Sanitizers and leak detection

Run the sanitizer gate from a graphical host session with:

```sh
tests/run-leak-checks.sh
```

The script still compiles with AddressSanitizer and UndefinedBehaviorSanitizer
inside the pinned GNOME SDK. It then runs those binaries directly on the host,
using libraries from that exact SDK commit and graphics drivers from its
declared Flatpak GL extension, because LeakSanitizer cannot inspect a process
inside the Flatpak sandbox. Application leaks, a missing OpenGL 3.3 page-curl
context, a page-curl shader or texture-upload failure, repeated media/camera
file-descriptor growth, and repeated child-process descriptor growth fail the
command. At runtime Pinpoint requires GTK's OpenGL or Vulkan renderer; the Cairo
renderer is deliberately unsupported. The suppressions are limited to documented
process-lifetime Fontconfig and Mesa EGL configuration caches plus a Cairo
strict-string interceptor false positive.

Run GCC's path-sensitive static analyzer and the additional strict C warning
profile inside the same SDK with:

```sh
tests/run-gcc-analysis.sh
```

This gate keeps GCC-specific checks out of the portable default build while
still treating analyzer, format, shadowing, prototype, alignment, undefined
macro, duplicate-condition, null-dereference, and variable-length-array
diagnostics as errors.

Run the GCC line, function, and branch coverage gate from a graphical host
session with:

```sh
tests/run-coverage.sh
```

The gate instruments a separate debug build with Meson's `b_coverage` option,
runs every coverage-relevant test against the pinned SDK libraries in the host
display context, and writes the full report to
`_build-coverage/meson-logs/coverage.json`. It excludes generated and test code,
requires 100% reachable line coverage in the deterministic modules that have
reached it, and prevents every reviewed per-file baseline from regressing. See
[the coverage policy](docs/coverage.md) for the limits and narrowly justified
line exclusions.

Run a presentation with:

```sh
_build/src/pinpoint presentation.pin
```

Running Pinpoint without a file opens a graphical setup screen for presenting
or rehearsing, including fullscreen, speaker-view, comment, and display
options. Choose the folder containing your `.pin` file so the Flatpak portal
also grants Pinpoint access to relative images, videos, SVGs, and asset
subdirectories. A sole presentation opens immediately; if the folder contains
several, Pinpoint asks which one to use. The header menu can export a
presentation to PDF, then open the PDF or show it in Files. It also provides
application, current-implementation, original-project, copyright, and licence
information.

PDF export includes a focused setup step for A4 or US Letter paper, landscape
or portrait orientation, separate speaker-note pages, and whether comment
lines should become notes. The same output controls are available to scripts:

```sh
pinpoint --output=talk.pdf --pdf-page-size=a4 \
  --pdf-orientation=landscape --pdf-no-speaker-notes talk.pin
```

Build the development Flatpak with:

```sh
flatpak-builder --user --force-clean build-dir \
  flatpak/com.nedrichards.pinpoint.Devel.json
```

The production-shaped manifest is `flatpak/com.nedrichards.pinpoint.json`.
Both manifests set the Meson prefix to `/app` explicitly so GNOME Builder's
Flatpak pipeline and command-line `flatpak-builder` builds share the same
installation layout.

## Presentation format

Settings before the first separator are presentation defaults. Settings on a
separator apply to the following slide.

```text
[background.jpg]
[font=Sans 60px]
[bottom]
-- [black] [center]
An image-led presentation
-- [photo.jpg] [fill] [text-align=center]
Small amounts of concise text
#Speaker notes start with # at the beginning of a line
```

The [complete format reference](docs/presentation-format.md) documents every
setting and retained compatibility rule. The historical `introduction.pin` is
bundled as a worked example: choose **View** beside **Introduction
Presentation** on the launch screen, or use its save button to make an editable
copy with all of its assets.

## Controls

- Right, Down, Space, Page Down, or primary click: next slide
- Left, Up, Backspace, Page Up, or secondary click: previous slide
- Forward/back or media next/previous: navigate when delivered to the app
- F or F11: fullscreen
- F1: speaker view
- S in the speaker view: swap the audience and speaker displays
- B: blank the audience screen
- H or Home: first slide
- Enter: run the slide command inside the sandbox
- Tab: edit the slide command
- Escape or Q: quit

The speaker window also provides start/restart, pause, autoadvance, fullscreen,
display swapping, and rehearsal controls. **Swap Displays** exchanges the
audience and speaker displays while presenting fullscreen on two or more
screens. Additional displays are left untouched. Rehearsal timings are written
back only after advancing past the final slide. The real GNOME two-screen path
has a separate [host display automation runner](docs/host-display-automation.md).
While a presentation is displayed, Pinpoint inhibits session idling, screen
blanking, and automatic locking even when it is windowed. The live GNOME
inhibitor lifecycle is checked by `tests/run-host-inhibit-test.sh`.

## License

This GTK 4 implementation is Copyright © 2026 Nick Richards and is licensed
under the GNU Lesser General Public License, version 2.1 or later.

The original Clutter-based Pinpoint codebase, which inspired this independent
implementation and established the presentation format and interaction model,
is Copyright © 2010 Intel Corporation and the original Pinpoint contributors.
It was also distributed under the GNU Lesser General Public License, version
2.1 or later. Original authors are credited in the application's About dialog.

The bundled introduction contains a short, modified excerpt of *Big Buck
Bunny*, Copyright © 2008 Blender Foundation, licensed under Creative Commons
Attribution 3.0. Complete source, attribution, licence, and modification details
are recorded in [`data/introduction/ORIGIN.md`](data/introduction/ORIGIN.md).
