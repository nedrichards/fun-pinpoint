# Pinpoint backlog

This is the single status and sequencing list for Pinpoint. Supporting documents
may explain constraints, validation procedures, or design results, but open work
belongs here.

## Open work

- [ ] Evaluate phone remote control once a real paired-device setup is
  available. Test Valent or GSConnect plus a phone before selecting an adapter.
  If those paths cannot provide a reliable presentation remote, prove the
  bespoke peer design's ephemeral QR/phrase pairing, encrypted transport,
  explicit lifecycle, revocation, and Flatpak permissions before granting
  network access. `varlink-glib` also needs a dependency recheck because the
  GNOME 50 SDK currently has libdex 1.1 while the alpha requires 1.2.
- [ ] Investigate building a custom android remote app, it would need to work
  with wear os on a pixel watch 2 and android on a Pixel 9.
- [ ] **Blocked on the runtime providing GTK 4.24:** Integrate GTK-managed
  desktop session save and restore once the pinned SDK exposes the new
  application-state API. Follow `docs/session-restore.md`: normal launches must
  stay clean; clean session restore may reopen a deck and slide; crash recovery
  must remain windowed and paused without replaying commands, media, camera, or
  rehearsal. GTK owns instance storage and window geometry; Pinpoint supplies
  the shared presentation state and per-window audience/speaker roles.
- [ ] Recover the historical renderer's retained-scene CPU efficiency without
  changing transition semantics. Three controlled installed-0.1.8 runs used
  far fewer CPU samples than the GTK4 GL path, even after allowing for the old
  renderer's lower frame rate and sampling variance. The old Clutter code keeps
  slide actors alive and animates properties; the GTK4 stage reconstructs both
  slide compositions each frame. A native crossfade reduced CPU but changed the
  historical opacity curve, while static whole-slide node reuse added large
  stalls and more samples, so both experiments were reverted. Treat 0.1.8 as a
  performance reference and pursue finer retained composition only with pixel,
  frame-pacing, media-offload, and CPU-capture proof. See
  `docs/performance.md`.
- [ ] Finish the remaining physical hardware performance matrix. Sysprof is now
  working and repeatable GNOME 50 workloads cover page curl, the full bundled
  introduction, its fade-heavy range, and its video range on the available
  Tiger Lake Iris Xe and internal 2x eDP panel. GTK's Vulkan path held fades to
  roughly 32 fps; GL reached roughly 59 fps with fewer than half the CPU
  samples and preserved pixels and DMA-BUF video. Pinpoint now selects GL only
  for the exact tested `8086:9a49` device, leaving other GPUs and explicit
  overrides untouched. Page curl remains a trade-off: GL improved average frame
  pacing but used 11% more CPU samples and showed setup stalls. Still run the
  matrix on a representative discrete GPU, inspect colour on an external
  display, and run a long representative presentation on battery; the current
  RAPL captures were made on wall power. Only then generalize renderer policy
  or consider persistent mapped buffers or deeper resource sharing. See
  `docs/performance.md` for the commands and results.
- [ ] Explore an optional GTK-native composition environment after the external
  editor integration is established. Keep `.pin` as the portable plain-text
  source format and external editors fully supported, while considering a
  separate GtkSourceView-based experience inspired by GNOME Text Editor and
  Builder: diagnostics, completion for settings and assets, slide navigation,
  and a live quality-accurate preview. Keep its dependencies and runtime cost
  out of the lean presentation path when the editor is not installed or used.
- [ ] Contribute the Pinpoint language definition to GtkSourceView so Flatpak
  editors receive it from their runtime. Native packages install it directly;
  until the upstream definition reaches the GNOME runtime, sandboxed GNOME Text
  Editor uses the documented per-user installation path because one Flatpak
  cannot modify another application's `/app`.

## Completed work

- [x] Replace the inherited Pinpoint 0.1.8 AppStream screenshot before a
  supported release. Repository-hosted captures now show the GTK 4 launch,
  audience, and speaker experiences at representative sizes, with captions
  describing the plain-text presentation workflow.

- [x] Finish PDF export memory bounds and cancellation. Interactive exports now
  show slide progress with an explicit Cancel action; CLI exports use the same
  cancellable worker, turn Ctrl+C into a clean exit, and show progress on a TTY.
  Raster and video backgrounds are capped at a 144-DPI-equivalent export bound,
  only the most recent decoded surface is retained, and the destination is
  replaced only after a complete successful render.

- [x] Compare GTK 4.22's `GtkSvg` paintable with librsvg. On GTK 4.22.4 the
  existing pixel fixture is visually equivalent (mean channel difference
  0.006/255, with 0.007% of pixels outside the existing tolerance), and
  `GtkSvg` records its 320x240 node a few microseconds faster. Cold parsing is
  the same order of magnitude, while `GtkSvg` reports unsupported `<style>`,
  `<textPath>`, and `<feTurbulence>` content that librsvg accepts. Because
  Pinpoint already caches each size-specific node, PDF export still needs
  librsvg's Cairo rendering, and the dependency therefore cannot be removed,
  keep the shared librsvg source as the sole production path rather than paying
  for a dual-parser fast path. `test-svg-renderers` preserves the compatibility,
  pixel-difference, fallback-signal, and indicative-cost comparison.

- [x] Add backward-compatible visual descriptions for informative slide
  backgrounds. `#@alt:` uses a specialised first-column speaker-note line, so
  current Pinpoint exposes it through the stage accessibility description while
  Pinpoint 0.1.8 leaves the audience rendering unchanged and shows it as a
  normal speaker note. The parser, round-trip serialization, GTK stage, editor
  definition, documentation, and bundled introduction cover the directive.

- [x] Audit and harden the command-line contract without breaking valid 0.1.8
  invocations. PDF export now rejects its source file and path aliases to it;
  Ctrl+C and SIGTERM cancel atomically with conventional 130/143 statuses;
  a repeated termination signal force-quits; and interrupted rehearsal leaves
  its source untouched. Ambiguous multiple presentations fail explicitly.
  Additive `--version` and non-interactive `--check` modes support scripts;
  checking covers UTF-8 parsing, relative backgrounds, and custom legacy
  transition JSON without executing commands or decoding media. Application
  tests cover successful and failing checks, signal paths, destination
  preservation, source aliases, and subprocess teardown. See
  `docs/command-line.md`.

- [x] Improve the external-editor experience without making an editor part of
  the presentation runtime. The installed GtkSourceView 5 definition associates
  `.pin` and `application/x-pinpoint`, and highlights recommended default lines,
  slide separators, settings, audience text, speaker notes, commands, Pango
  markup, entities, and escapes. The pinned SDK validates its schema, MIME/glob
  discovery, and semantic contexts automatically; the installed GNOME Text
  Editor and GNOME Builder Flatpaks also discover it through their real
  GtkSourceView runtimes. The editor workflow, sandbox boundary, native and
  per-Flatpak installation paths, and generic GtkSourceView recipe are
  documented in `docs/external-editors.md`.

- [x] Complete local desktop remote-control evaluation. A general-purpose GNOME
  MPRIS client selected and controlled two simultaneous instances inside the
  installed Flatpak; per-instance `org.gtk.Actions` calls remained isolated;
  and Flatpak required no extra session-bus permission. Instance names now use
  the unique D-Bus connection rather than sandbox-local PIDs. The conservative
  MPRIS adapter is now shipped for media-key and specialist-client navigation;
  retain the action group for future exact local/companion control, and keep
  play/pause unavailable until it has truthful timer semantics.

- [x] Investigate GTK session saving and storage for the desktop's future
  restore model. GNOME 50 currently has GTK 4.22.4 and reports
  `RestoreSupported=false`; the application integration is blocked until the
  pinned runtime supplies GTK 4.24 or a stable successor. The GTK-owned
  lifecycle, split application/window state contract, restore-reason policy,
  portal-file behaviour, safety exclusions, and validation plan are recorded
  in `docs/session-restore.md` rather than adding a competing private restore
  file.

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
- [x] Review and define the acceptable video-format set. The production-runtime
  contract covers WebM VP8/VP9/AV1 Main, MP4 AV1 Main, MP4/MOV H.264 Constrained
  Baseline/Main/High, Ogg Theora, animated GIF, Opus/Vorbis/AAC-LC, and
  conventional 8-bit 4:2:0 BT.601/BT.709 SDR. Synthetic accepted fixtures and
  a corrupt MP4 rejection fixture are retained and tested independently of host
  plugins. H.264 comes from the automatically installed codec extension; AV1
  software fallback is verified against the base runtime's dav1d decoder.
- [x] Audit the media and rendering pipeline for Wayland-first hardware
  acceleration and output quality. Video exposes negotiated caps and selected
  elements, the camera can negotiate PipeWire directly with the GTK sink,
  raster images use explicit minification filtering, SVGs remain cached vector
  nodes, text shaping is cached without application-side glyph rasterisation,
  and page curl captures at physical HiDPI resolution.
- [x] Audit the whole codebase for obvious performance issues, redundant copies,
  zero-copy gaps, and GNOME 50-era GTK/GLib/GStreamer improvements. The review
  confirmed the zero-copy-eligible media path and identified speaker cache
  resets and idle wakeups, repeated/unbounded asset work, synchronous and
  copy-heavy PDF export, optional compositor video offload and GTK-native SVG
  fast paths, and the page-curl readback boundary. Findings, constraints, and
  implementation order are recorded in `docs/performance-audit.md`.
- [x] Remove avoidable speaker-view work. Preview stages now retain their
  presentation and shared raster cache while selecting adjacent slides without
  transitions. The 50 ms timing source exists only while the timer is running
  and is removed while idle or paused, with source-lifecycle regression tests.
- [x] Establish watched, shared, bounded asset caching. Each unique background
  is classified and resolved once per presentation, audience and preview stages
  share an initial eight-texture LRU working set, unchanged textures survive
  text-only reloads, and every referenced image, SVG, video, or legacy
  transition file is monitored for targeted live invalidation. Atomic
  replacement, cross-stage reuse, and redraw after invalidation are covered by
  tests.
- [x] Finish the presentation asset store. Current and adjacent raster images
  decode through cancellable `GTask` workers, reload and final-stage shutdown
  discard pending results, and audience/speaker stages share both textures and
  one parsed librsvg source per file. A 64 MiB RGBA-equivalent decoded-texture
  LRU replaces the eight-image limit; tests cover deduplication, invalidation,
  cancellation, differently sized SVG consumers, and byte-bounded eviction.
- [x] Prototype GTK graphics offload without weakening compatibility. Stable
  video and camera backgrounds use a dedicated `GtkGraphicsOffload` child,
  while overlays, clipping, transitions, unsupported platforms, formats, and
  colour states retain GTK's normal GSK fallback. Bounds are aligned to physical
  pixels at fractional scales. A live Wayland run confirmed NV12 DMA-BUF input,
  subsurface creation, and the compositor's explicit fallback reasons. GTK
  4.22's reduced-motion preference and the older animations switch both suppress
  transitions, with one informational message explaining the resulting jump.
- [x] Remove the first interactive PDF stalls and redundant thumbnail copies.
  Launch-screen export now runs through a tested `GTask` while CLI export stays
  synchronous. Video candidates are scored directly in mapped RGBA buffers and
  only the selected sample is copied into the PDF image path.
- [x] Validate historical presentations against the installed 0.1.8 Flatpak.
  Real presentations have been exercised during the port, and every known
  discrepancy that can be shared repeatably is represented by the compatibility,
  rendering, media, transition, lifecycle, or multi-monitor fixtures. Treat new
  real-presentation findings as regressions and add a focused fixture.
- [x] Audit keyboard and assistive-technology behaviour. Native setup retains
  GTK/libadwaita semantics; the custom stage exposes slide position, audience
  text, blank state, and shortcut help; speaker previews and timing controls
  have contextual names; and disabling desktop animations eliminates motion.
- [x] Put excellent presentations for hackers at the centre of the product.
  One shared tagline anchors the launch screen, About dialog, command-line help,
  desktop entry, and AppStream listing. Supporting copy follows the bundled
  introduction's core-ideas, plain-text, live-tuning, and less-text philosophy.
- [x] Establish the remote-control foundation independently of transport.
  Audience and speaker input, toolbar controls, mouse buttons, standard HID
  clickers, and delivered media-navigation keys use one stateful action model.
  Presentation display mode owns exactly one idle inhibitor whether windowed or
  fullscreen; the live GNOME gate verifies flag `8`, stable ownership, and
  release on exit.
- [x] Validate the host display automation runner with two displays in extended
  desktop mode. The live GNOME run passed on 20 July 2026: audience and speaker
  windows opened fullscreen on distinct displays, `S` swapped them, and `F11`
  left and restored the two-display fullscreen arrangement. Physical connector
  events remain covered by the deterministic production selection policy
  because window automation cannot synthesize them reliably.
- [x] Build isolated remote-control prototypes without selecting a production
  adapter. Per-instance D-Bus actions and conservative MPRIS drive the shared
  state model and preserve independent processes. A bounded Varlink interface
  generates typed bindings, `librebonjour` builds in GNOME 50, and the P2P
  design separates authenticated encrypted pairing from DNS-SD discovery.
