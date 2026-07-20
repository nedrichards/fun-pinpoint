# Pinpoint backlog

This is the single status and sequencing list for Pinpoint. Supporting documents
may explain constraints, validation procedures, or design results, but open work
belongs here.

## Open work

- [ ] Improve the external-editor experience without making an editor part of
  the presentation runtime. Start with an installed GtkSourceView 5 language
  definition for `.pin`: highlight defaults and slide separators, settings,
  audience text, comments/speaker notes, commands, and Pango markup; associate
  it with Pinpoint's MIME type; validate the definition automatically and in
  GNOME Text Editor. This is the next implementation task.
- [ ] Replace the inherited Pinpoint 0.1.8 AppStream screenshot before a
  supported release. Capture the current GTK 4 launch, audience, and speaker
  experiences at representative sizes, keep the images in a stable public
  location, and make the captions express the plain-text hacker workflow.
- [ ] Design a backward-compatible way to describe visual backgrounds to
  assistive technology. The historical parser treats every unknown bracketed
  setting as a background, so a new `[alt=…]` field would break the same file in
  Pinpoint 0.1.8. Evaluate a comment convention, sidecar metadata, or a versioned
  format extension; then expose the chosen description on the stage and add
  compatibility and accessibility fixtures.
- [ ] Evaluate phone remote control once a real paired-device setup is
  available. Test Valent or GSConnect plus a phone before selecting an adapter.
  If those paths cannot provide a reliable presentation remote, prove the
  bespoke peer design's ephemeral QR/phrase pairing, encrypted transport,
  explicit lifecycle, revocation, and Flatpak permissions before granting
  network access. `varlink-glib` also needs a dependency recheck because the
  GNOME 50 SDK currently has libdex 1.1 while the alpha requires 1.2.
- [ ] Investigate building a custom android remote app, it would need to work
  with wear os on a pixel watch 2 and android on a Pixel 9.
- [ ] investigate the CLI and make sure it is robust against cancellation and
  interruption
- [ ] **Blocked on the runtime providing GTK 4.24:** Integrate GTK-managed
  desktop session save and restore once the pinned SDK exposes the new
  application-state API. Follow `docs/session-restore.md`: normal launches must
  stay clean; clean session restore may reopen a deck and slide; crash recovery
  must remain windowed and paused without replaying commands, media, camera, or
  rehearsal. GTK owns instance storage and window geometry; Pinpoint supplies
  the shared presentation state and per-window audience/speaker roles.
- [x] Finish PDF export memory bounds and cancellation. Interactive exports now
  show slide progress with an explicit Cancel action; CLI exports use the same
  cancellable worker, turn Ctrl+C into a clean exit, and show progress on a TTY.
  Raster and video backgrounds are capped at a 144-DPI-equivalent export bound,
  only the most recent decoded surface is retained, and the destination is
  replaced only after a complete successful render.
- [ ] Compare GTK 4.22's `GtkSvg` paintable with librsvg on the SVG
  compatibility and pixel suites, retaining the existing shared librsvg source
  as the fallback for unsupported SVG features.
- [ ] Resume hardware performance validation after the pending local update
  unblocks Sysprof. Capture page-curl frame pacing, its one-time GSK-to-CPU-to-GL
  transfer, peak texture residency, and colour consistency on representative
  integrated and discrete GPUs; record negotiated video paths and offload
  decisions on Wayland; check an external display; and run a long presentation
  on battery. Test releasing uploaded page-curl source textures or persistent
  mapped buffers only if those traces show material cost.
- [ ] Explore an optional GTK-native composition environment after the external
  editor integration is established. Keep `.pin` as the portable plain-text
  source format and external editors fully supported, while considering a
  separate GtkSourceView-based experience inspired by GNOME Text Editor and
  Builder: diagnostics, completion for settings and assets, slide navigation,
  and a live quality-accurate preview. Keep its dependencies and runtime cost
  out of the lean presentation path when the editor is not installed or used.

## Completed work

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
