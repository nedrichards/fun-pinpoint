# Remote-control architecture

Pinpoint treats remote control as a set of adapters around one presentation
command model. Keyboard and mouse input, the speaker toolbar, local automation,
desktop media controls, and any future peer connection must not grow separate
navigation logic.

## Shared command model

`PpControl` installs these application actions:

- `presentation-next`, `presentation-previous`, and `presentation-first`;
- stateful `presentation-blank`, `presentation-fullscreen`, and
  `presentation-speaker`; and
- `presentation-swap-displays` when a fullscreen audience/speaker pair exists.

Actions are disabled outside presentation display mode. Previous and first are
also disabled on the first slide. The internal next action remains available on
the final slide because one further advance completes historical rehearsal
timing; protocol adapters must use `pp_control_can_go_next()` when advertising
whether another slide exists.

Both presentation windows use the same key mapper. Conventional USB and
Bluetooth clickers continue to work through arrow and Page Up/Page Down keys.
Forward/back and audio next/previous keys are also accepted if the compositor
delivers them to the application. Mouse buttons and speaker-toolbar controls
activate the same actions.

Pinpoint currently uses `G_APPLICATION_NON_UNIQUE` so that separate
presentations can run in separate processes. The actions are an internal API at
this checkpoint, not a promised public D-Bus interface.

## Blanking and locking inhibition

The idle inhibitor follows presentation display mode rather than fullscreen.
It is acquired after a presentation loads successfully, remains unchanged
through fullscreen toggles, speaker visibility, monitor changes, and display
swaps, and is released when presentation display ends or the process exits.
Reaching the final slide does not release it because that slide is still being
shown.

Only `GTK_APPLICATION_INHIBIT_IDLE` is requested. This prevents the session
from becoming idle and potentially blanking or locking without overriding a
deliberate suspend or lid-close request.

Run the live GNOME check with:

```sh
tests/run-host-inhibit-test.sh
```

The runner checks windowed and fullscreen starts against GNOME's inhibitor
registry. The portal backend represents the request as a Mutter `idle-inhibit`
object, so the test identifies the new object relative to the pre-launch set,
checks flag `8`, verifies that it is not duplicated, and waits for its removal
after Pinpoint exits.

Pass `PINPOINT_FLATPAK_ID=com.nedrichards.pinpoint.Devel` to run the same gate
against an installed development Flatpak instead of the SDK-built host binary.

## Adapters to prototype

The shared actions deliberately do not choose a transport. The next stage will
evaluate each adapter independently:

1. A per-process local D-Bus action group for trusted scripts and accessibility
   tools. Its instance identity must preserve Pinpoint's multi-process model.
2. MPRIS `Next` and `Previous`, with slide position as metadata. `PlayPause`
   must remain unavailable unless it can truthfully represent the rehearsal or
   autoadvance timer; it must never mean blank screen.
3. A peer Varlink API using the same commands. Varlink describes typed calls
   over a supplied `GIOStream`; discovery, authentication, encryption, and
   pairing remain separate transport policy.
4. An opt-in local peer session suitable for a phone. A credible design needs
   an ephemeral pairing secret, an explicit start/stop lifecycle, no file or
   command surface, and revocation when presenting ends.

Current GNOME-community experiments make the third and fourth options worth a
bounded spike. `varlink-glib` provides generated asynchronous C APIs over an
application-supplied stream, while `librebonjour` abstracts local DNS-SD/mDNS
discovery over Avahi or systemd-resolved. Both are young, separately maintained
projects rather than platform guarantees, and service advertisement still has
host-policy and Flatpak implications:

- https://blogs.gnome.org/chergert/2026/05/#asynchronous-varlink-with-varlink-glib
- https://blogs.gnome.org/chergert/2026/05/#librebonjour
- https://valent.andyholmes.ca/documentation/protocol.html

Valent and the KDE Connect protocol are also important comparison points: they
already combine paired devices, presentation input, and MPRIS bridging. A
Pinpoint-specific peer protocol should only be pursued if those existing paths
cannot provide a reliable, understandable presentation remote.

## Decision gates

An adapter is suitable for production only if it:

- works in the production Flatpak and a Wayland GNOME session;
- preserves independent simultaneous Pinpoint processes;
- has truthful, slide-oriented semantics and observable enabled state;
- requires no broader permission than its user value justifies;
- remains offline-capable with predictable latency;
- fails closed when presentation display mode ends; and
- can be exercised by automated protocol tests plus a real client.
