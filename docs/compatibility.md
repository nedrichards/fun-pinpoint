# Pinpoint 0.1.8 compatibility ledger

The release tarball and tag `0.1.8` are the behavioral baseline. Historical
quirks are covered by tests before deciding whether they should become opt-in
fixes.

## Implemented

- Presentation defaults and per-slide settings
- Legacy leading-hyphen separator behavior
- Escaping, comments, speaker notes, and `--ignore-comments`
- Pango markup and plain-text mode
- Fonts, text colors, alignment, nine text positions, and contrast shading
- Solid-color, image, and SVG backgrounds
- Fit, fill, stretch, unscaled, and background alignment geometry
- Relative presentation assets
- Live source monitoring and first-changed-slide selection
- Keyboard and mouse navigation, blanking, fullscreen, and idle cursor hiding
- Built-in fade, slide, text-slide, spin, sheet, and swing transition families
- Composable native slide, zoom, fade, spin, and 3D flip transitions with
  direction, target-layer, entrance/exit, duration, and easing controls
- External legacy ClutterState JSON transitions resolved beside the
  presentation, with duration, common easing, and actor/background/midground/
  foreground position, scale, opacity, and three-axis rotation compatibility
- GPU-backed page-curl and page-curl-both using the original 32x32 deformation
  mesh and vertex-lighting equation. Mesh depth is reused for triangle order,
  page rotation is precomputed once, and flat pages use a static GPU mesh
- GStreamer video backgrounds using `playbin3` and `gtk4paintablesink`
- A production-runtime media contract covering WebM VP8/VP9/AV1 Main,
  MP4 AV1 Main, MP4/MOV H.264 Baseline/Main/High, Ogg Theora, animated GIF,
  Opus/Vorbis/AAC-LC audio, conventional 8-bit 4:2:0 SDR colour, and
  deterministic corrupt-file rejection
- Bounded best-frame GStreamer video thumbnails in PDF output. Up to four
  candidates are scored for contrast and edge detail, black or flat candidates
  are rejected, and the selected frame is cached per video while retaining the
  same background sizing and placement rules as live slides
- Failed video sources are cached after their first error and media pipelines
  have deterministic floating-reference and NULL-state teardown
- Camera backgrounds through the camera portal, PipeWire, and `gtk4paintablesink`
- Editable embedded commands executed inside the Flatpak sandbox
- Speaker notes, three-slide preview strip, progress, pause, and autoadvance
- Original-proportion speaker-view typography, 4:3 cropped preview strip,
  compact control bar, remaining-time display, and layered timing/warning band
- Clean main/speaker-window shutdown with the speaker timer and stage lifetime
  explicitly coordinated
- Coordinated audience and speaker windows: synchronized fullscreen controls,
  presenter/audience monitor selection, single-monitor fallback, and display
  hotplug handling, plus a speaker-toolbar action that swaps the active pair
  while leaving any additional displays untouched
- Rehearsal timing accumulation and atomic serialization back to the source
- Original command-line option names and short forms, with Ctrl+C rehearsal
  abort semantics retained. Additive version/check and PDF controls do not
  alter valid 0.1.8 presentation invocations.
- PDF output with A4 and US Letter paper sizes, landscape and portrait
  orientation, optional separate speaker-note pages, wrapped note text, and
  equivalent GUI and command-line controls
- Portal-backed PDF export from the launch-screen menu, including relative
  sibling image and SVG assets, with post-export actions to open the PDF or
  reveal it in the file manager
- Desktop, MIME, AppStream, icon, and GNOME 50 Flatpak integration
- Libadwaita launch setup with portal-backed file opening, separate present and
  rehearse paths, fullscreen/speaker/comment options, and audience-display
  selection
- Portal-safe presentation-folder opening that retains access to relative
  sibling assets, automatically opens a sole `.pin`, offers an in-app chooser
  for multiple presentations, and recovers direct file grants with missing
  assets
- A standard libadwaita About dialog that distinguishes Nick Richards'
  copyright in this GTK 4 implementation from the Intel and contributor
  copyright in the original codebase that inspired it, alongside original
  author credits, LGPL 2.1 information, and bundled-media licensing; the launch
  screen, About dialog, CLI, desktop metadata, and AppStream listing share the
  project's “Excellent presentations for hackers” purpose
- Display-backed pixel regression fixtures at 800×600, 1280×720 widescreen,
  and 800×600 with 2× scaling, covering stage size, color, text, and shading
- Automated page-curl CPU, completed-transition idle, executable-size, bundled
  media-size, sanitizer, and media teardown regression gates

## Parity status

No known automated 0.1.8 parser or renderer parity gaps remain. The initial
real-hardware multi-display release gate is complete; repeat it when the GTK,
GNOME, or display-assignment code changes materially.

## Manual hardware validation

Two-display Wayland validation passed on 19 July 2026 with a 2256×1504
built-in panel and a 3840×2560 external panel, both using fractional scaling:

- `--speakermode --fullscreen` automatically placed the audience on the
  external display and the speaker view on the built-in display.
- Navigation from the focused speaker window updated the audience and all
  three speaker previews together.
- F and F11 synchronized fullscreen from both the audience and speaker
  windows; F1 hid the speaker window and restored it on the presenter display.
- Explicitly selecting the built-in display for the audience reversed the
  assignment correctly, with the speaker view fullscreen on the external
  display.
- **Swap Displays** in the speaker toolbar exchanged the fullscreen audience
  and speaker displays in both directions without restarting the presentation.
- Disconnecting the external display while fullscreen kept both windows
  alive, moved the audience fullscreen onto the built-in display, and made the
  speaker view windowed. The one-screen fallback cleared the obsolete
  two-display assignment so reconnecting restored the automatic external
  audience and built-in speaker layout without restarting the presentation.

Hardware profiling and every other open task are tracked in the central
[Pinpoint backlog](../TODO.md), rather than as a second checklist here.

## Deliberately retained historical behavior

- Any unrecognized bracketed setting becomes the slide background.
- Every historical media suffix remains accepted. Existing assets additionally
  use content sniffing, while filename fallback is case-insensitive and covers
  common modern video containers.
- A leading hyphen starts a slide separator unless escaped.
- Background `top` and `bottom` alignment retain the 0.1.8 parser omission;
  corner alignments work as before.
- Text is scaled down to at most 80% of the stage and is never enlarged.
- Alignment margins are 5% of the stage; shading padding is 1% of its width.
- PDF speaker notes are emitted as a separate page after their slide.

## Sandbox boundary

`[command=]` uses `/bin/sh` inside the application sandbox. It intentionally
does not request permission to execute arbitrary host commands. Presentations
that use commands should bundle or otherwise target tools available inside the
Flatpak, or migrate actions to portal-backed file and URI opening.
