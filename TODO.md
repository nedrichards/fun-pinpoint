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
  host plugins. H.264 is supplied by the runtime-declared, automatically
  installed codec extension; AV1 software fallback is verified against the
  base runtime's dav1d decoder.
- [x] Audit the media and rendering pipeline for Wayland-first hardware
  acceleration and output quality. Video now exposes its negotiated caps and
  selected elements, the camera can negotiate PipeWire directly with the GTK
  sink, raster images use explicit minification filtering, SVGs remain cached
  vector nodes, text shaping is cached without application-side glyph
  rasterisation, and page curl captures at physical HiDPI resolution. The
  documented one-time page-curl readback/upload remains a hardware-profiling
  question; X11 remains only a compatibility fallback.
- [x] Validate historical presentations against the installed 0.1.8 Flatpak.
  Real presentations have been exercised during the port, and every known
  discrepancy that can be shared repeatably is represented by the compatibility,
  rendering, media, transition, lifecycle, or multi-monitor fixtures. Treat new
  real-presentation findings as regressions and add a focused fixture when they
  appear rather than maintaining a duplicate private corpus in the repository.
- [x] Audit keyboard and assistive-technology behaviour. The native setup view
  retains GTK/libadwaita semantics; the custom stage now exposes slide position,
  audience text, blank state, and shortcut help; speaker previews and timing
  controls have contextual names; and disabling desktop animations eliminates
  slide motion. The historical format's lack of backward-compatible visual
  alternative text remains explicitly documented as an authoring limitation.
- [x] State the project's goal in the About dialog: help hackers give excellent
  presentations with concise plain-text files.
- [ ] Resume hardware performance validation after the pending local update
  unblocks profiling. Capture page-curl frame pacing on representative
  integrated and discrete GPUs and run a long presentation on battery power;
  investigate persistent mapped GL buffers only if those traces show that the
  remaining curved-page buffer update is material.
- [ ] Enable host-session GNOME automation for repeatable interactive UI tests
  where practical. Deterministic selection tests now cover one, two, and three
  displays plus unplug/replug recalculation, and `tests/run-host-display-test.sh`
  covers real GNOME fullscreen placement, display swapping, and fullscreen
  restoration without changing Flatpak permissions. Run and ratchet the host
  gate with two connected displays in extended-desktop mode after GNOME Shell
  unsafe mode is re-enabled. This live two-screen run is the remaining
  validation requirement; physical connector events remain represented by the
  tested production selection policy rather than a synthetic compositor device.
- [x] Establish the remote-control foundation independently of any transport.
  Audience keys, speaker keys and toolbar controls, mouse buttons, standard HID
  clickers, and delivered media-navigation keys now use one stateful action
  model. Presentation display mode owns exactly one idle inhibitor whether
  windowed or fullscreen; the live GNOME gate verifies flag `8`, stable
  ownership, and release on exit.
- [x] Build isolated remote-control prototypes without selecting a production
  adapter. Per-instance D-Bus actions and conservative MPRIS both drive the
  shared state model and retain independent simultaneous processes. A bounded
  Varlink interface parses and generates typed bindings; `librebonjour` builds
  in the GNOME 50 SDK, while `varlink-glib` currently requires bundling libdex
  1.2.beta beyond the SDK's 1.1.0. The P2P design keeps authenticated encrypted
  pairing separate from DNS-SD discovery and proposes a companion process as a
  way to keep network permissions out of the presenter.
- [ ] Choose and productionise remote control only after real-client proof.
  Test the D-Bus and MPRIS prototypes with GNOME accessibility/automation tools,
  `playerctl`, and Valent or GSConnect plus a phone; repeat inside the production
  Flatpak; then compare usefulness, discoverability, instance selection, and
  required bus permissions. If a bespoke peer remains justified, prove its
  ephemeral QR/phrase pairing, encrypted transport, explicit lifecycle and
  revocation before granting network access. Keep MPRIS play/pause unavailable
  unless it truthfully controls a presentation timer.
- [ ] Explore an optional GTK-native composition environment for authoring
  presentations. Keep `.pin` as the portable plain-text source format and
  external editors fully supported, while considering a separate experience
  based on GtkSourceView and inspired by GNOME Text Editor and Builder:
  syntax-aware editing, diagnostics, completion for settings and assets, slide
  navigation, and a live quality-accurate preview. Keep its dependencies and
  runtime cost out of the lean presentation path when the editor is not
  installed or used.
