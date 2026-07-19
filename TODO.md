# Pinpoint backlog

- [x] Prepare a safe first public checkpoint. Exclude local build products,
  profiling captures, Python bytecode, crash dumps, and agent metadata; audit
  authored files for credentials and machine-specific paths; include the full
  LGPL-2.1-or-later licence; and keep the active-reconstruction status and
  remaining manual hardware gates explicit.

- [x] Expose more PDF export options in the launch-screen workflow. The focused
  libadwaita setup step now covers paper size, orientation, speaker-note pages,
  and comment notes, with equivalent CLI controls and legacy-compatible
  defaults.
- [x] Write complete user-facing documentation for the `.pin` presentation
  format. The format reference covers presentation defaults, slide separators,
  text and background settings, transitions, speaker notes, commands, media,
  escaping, PDF export, and retained historical compatibility rules.
- [x] Package the historical `introduction.pin` and its assets, with an obvious
  launch-screen entry point that works inside Flatpak. The bundled copy opens
  read-only, can be saved with all of its assets for editing, and uses an
  attributed CC BY 3.0 Big Buck Bunny excerpt encoded as VP9/Opus WebM.
- [x] Improve PDF video-thumbnail selection beyond the deterministic one-third
  frame. PDF export samples at most four candidates under fixed decode
  timeouts, rejects black or nearly uniform frames, scores contrast and visual
  detail, retains a safe fallback, and caches one result per video URI.
- [x] Review and define the acceptable video-format set. Test the containers,
  video codecs, audio codecs, colour formats, and common profile variants that
  the GNOME runtime can reliably decode; distinguish formats Pinpoint supports
  deliberately from those that merely happen to work through locally installed
  GStreamer plugins. The production-runtime contract now covers WebM
  VP8/VP9/AV1 Main, MP4 AV1 Main, MP4/MOV H.264 Constrained
  Baseline/Main/High, Ogg Theora, animated GIF, Opus/Vorbis/AAC-LC, and
  conventional 8-bit 4:2:0 BT.601/BT.709 SDR. Synthetic accepted fixtures and
  a corrupt MP4 rejection fixture are retained and tested independently of
  host plugins and optional Flatpak codec extensions.
- [x] Audit the media and rendering pipeline for Wayland-first hardware
  acceleration and output quality. Video now exposes its negotiated caps and
  selected elements, the camera can negotiate PipeWire directly with the GTK
  sink, raster images use explicit minification filtering, SVGs remain cached
  vector nodes, text shaping is cached without application-side glyph
  rasterisation, and page curl captures at physical HiDPI resolution. The
  documented one-time page-curl readback/upload remains a hardware-profiling
  question; X11 remains only a compatibility fallback.
- [ ] Resume hardware performance validation after the pending local update
  unblocks profiling. Capture page-curl frame pacing on representative
  integrated and discrete GPUs and run a long presentation on battery power;
  investigate persistent mapped GL buffers only if those traces show that the
  remaining curved-page buffer update is material.
- [ ] Enable host-session GNOME automation for repeatable interactive UI tests
  where practical. Keep Pinpoint's Flatpak permissions unchanged: use `gdbus`
  from the outer test environment for ordinary session-bus inspection, and run
  window and input automation in an explicitly enabled host GNOME Shell context
  when tests need to exercise fullscreen, speaker-window, navigation, or
  display-hotplug behaviour.
- [ ] Explore an optional GTK-native composition environment for authoring
  presentations. Keep `.pin` as the portable plain-text source format and
  external editors fully supported, while considering a separate experience
  based on GtkSourceView and inspired by GNOME Text Editor and Builder:
  syntax-aware editing, diagnostics, completion for settings and assets, slide
  navigation, and a live quality-accurate preview. Keep its dependencies and
  runtime cost out of the lean presentation path when the editor is not
  installed or used.
