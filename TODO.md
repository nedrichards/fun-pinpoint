# Pinpoint backlog

This file contains only open or intentionally deferred work. Completed behavior
is recorded in [docs/compatibility.md](docs/compatibility.md), with detailed
history in Git.

## Remote-control device evaluation

Status: desktop MPRIS behavior and the bundled browser prototype are complete;
real-device phone and watch interaction still needs evaluation.

- Test GNOME Remote Desktop and KDE Connect presentation/media controls from a
  Pixel 9 and Pixel Watch 2.
- Record which required actions map cleanly to standard MPRIS controls and which
  need Pinpoint-specific semantics.
- Decide whether the existing integrations are sufficient. Build a dedicated
  Android/Wear companion only if the device study demonstrates a concrete gap.
- Keep any custom protocol compatible with the architecture and threat model in
  [docs/remote-control.md](docs/remote-control.md).

## Session restoration

Status: design complete; implementation is intentionally blocked on GTK 4.24,
whose restore-token API is not available in the pinned GNOME 50 runtime.

- Recheck the runtime when GTK 4.24 lands.
- Implement the documented design without persisting presentation paths or
  stage state.

See [docs/session-restore.md](docs/session-restore.md).

## Retained slide composition

Status: profiling identified repeated snapshot-time composition as the main
remaining CPU opportunity. The current implementation is correct and keeps
rendering state simple.

- Prototype retained slide composition without changing texture ownership,
  transition behavior, live editing, or media playback.
- Compare idle, transition, and animated-slide profiles before adopting it.
- Keep the change only if measured gains justify the additional invalidation
  and lifetime complexity.

See [docs/performance.md](docs/performance.md) and
[docs/rendering-pipeline.md](docs/rendering-pipeline.md).

## Physical performance matrix

Status: automated and initial physical validation are complete. Broader hardware
coverage remains useful for release-quality confidence.

- Repeat representative decks on integrated and discrete GPUs, mixed-scale
  displays, and two-monitor speaker-view setups.
- Record frame pacing, CPU use, memory growth, and any driver-specific faults.
- Re-run the host display procedure after material stage or monitor-routing
  changes.

See [docs/performance.md](docs/performance.md) and
[docs/host-display-automation.md](docs/host-display-automation.md).

## GtkSourceView language definition

Status: Pinpoint ships its own `pinpoint.lang` definition.

- Contribute the language definition upstream once the syntax and scope names
  have remained stable through wider use.
- Keep the bundled copy until the minimum supported GtkSourceView reliably
  contains the upstream version.
