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

## Prototype results

The shared actions deliberately do not choose a transport. Isolated prototypes
under `prototypes/` produced these results:

1. A standard `org.gtk.Actions` group works on a per-process D-Bus name and
   exposes the action model without another command implementation. Two
   simultaneous processes retain distinct names. This is the strongest local
   automation candidate, subject to production Flatpak and assistive-client
   proof.
2. MPRIS `Next` and `Previous` can represent slide navigation and slide
   position can be metadata. The prototype truthfully reports `Stopped` and
   rejects `PlayPause`, pause, seek, and volume. This may make some media
   clients hide it, which is preferable to claiming false playback semantics.
3. The peer Varlink schema maps cleanly onto the same state and commands and
   generates typed C bindings. `varlink-glib` describes calls over a supplied
   `GIOStream`; it does not provide pairing, authentication, encryption, or
   network discovery.
4. `librebonjour` builds in the GNOME 50 SDK and could advertise or discover an
   opt-in local service. The current `varlink-glib` alpha does not build against
   the SDK's libdex 1.1 without bundling current libdex 1.2.beta. This is a
   concrete cost against adopting that stack now, not a problem with the
   transport-neutral interface design.

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
already combine paired devices, presentation input, and MPRIS bridging. Neither
Valent, GSConnect, nor a generic MPRIS client was installed for this spike, so a
Pinpoint-specific peer protocol should not be selected until those existing
paths have been tested with real phone and desktop clients.

## P2P architecture boundary

A credible future phone path is a composition, not a single protocol:

1. Presentation mode explicitly creates a short-lived remote session.
2. Pinpoint displays a QR code or short phrase carrying an endpoint identifier,
   an ephemeral secret, and protocol version. The secret must not be placed in
   DNS-SD metadata.
3. The peer proves possession of that secret while establishing an encrypted,
   authenticated stream. DNS-SD can make the endpoint easier to find, but is
   never the trust decision.
4. The Varlink control interface runs over that already-secure `GIOStream`.
   It offers only presentation state and bounded presentation commands.
5. Leaving presentation mode closes the listener and every peer connection;
   starting again creates fresh credentials.

A browser remote would require a browser-compatible secure transport and a
small bridge because browsers cannot connect to an arbitrary raw `GIOStream`.
An Android/Valent client or a separate companion process is a more natural
first peer. A companion could own network discovery and pairing while talking
to Pinpoint through its local per-instance D-Bus API, keeping broad network and
system-bus permissions out of the lean presenter Flatpak. That separation is
worth testing before any manifest permissions are added.

The reproducible build notes and commands are in `prototypes/README.md`.

## Decision gates

An adapter is suitable for production only if it:

- works in the production Flatpak and a Wayland GNOME session;
- preserves independent simultaneous Pinpoint processes;
- has truthful, slide-oriented semantics and observable enabled state;
- requires no broader permission than its user value justifies;
- remains offline-capable with predictable latency;
- fails closed when presentation display mode ends; and
- can be exercised by automated protocol tests plus a real client.

The prototypes satisfy the automated command, state, and multi-instance parts.
They do not yet satisfy real-client, production-Flatpak, pairing, or permission
proof, so no adapter has been selected for production. The local-client and
phone-client gates are sequenced in the central [Pinpoint backlog](../TODO.md).
