# Remote-control prototypes

These programs and peer-interface definitions remain experiments rather than
stable public APIs. They build only when Meson's `remote_prototypes` option is
enabled. The MPRIS implementation itself is shared with production Pinpoint so
the prototype exercises the shipped protocol adapter.

```sh
meson setup _build-prototypes -Dremote_prototypes=true
meson compile -C _build-prototypes
prototypes/run-prototype-tests.sh
```

`pinpoint-remote-prototype` simulates a presentation while exposing the same
`PpControl` actions through two local transports:

- standard `org.gtk.Actions` on a per-process D-Bus name; and
- the production conservative MPRIS player with `Next` and `Previous`, slide
  metadata, and fullscreen state.

MPRIS reports `Stopped` and rejects play, pause, seek, and volume operations.
Those media concepts are not truthful presentation concepts until Pinpoint has
a timer or autoadvance feature that they can actually control. The protocol
test also starts two instances, verifies that both D-Bus and MPRIS names remain
distinct, and sends an action to the second instance to prove that selection
does not leak into the first.

To run the same gate inside an installed Pinpoint Flatpak while retaining the
repository-built prototype binary:

```sh
PINPOINT_PROTOTYPE_FLATPAK_ID=com.nedrichards.pinpoint.Devel \
  prototypes/run-prototype-tests.sh
```

The runner mounts the repository read-only into the selected app sandbox. The
prototype derives its bus namespace from `FLATPAK_ID` and uses its unique D-Bus
connection name for the instance suffix. A process ID is not sufficient because
separate Flatpak PID namespaces can give simultaneous processes the same PID.

The Varlink file is the equivalent peer-facing command contract. It exposes no
file, URI, arbitrary command, or slide-jump surface. It deliberately does not
specify a socket or network address: `varlink-glib` accepts an already-connected
`GIOStream`, so the application must establish an authenticated and encrypted
stream before attaching this API. Run `validate-varlink-schema.sh` with an
upstream `varlink-codegen` binary to parse the interface and generate temporary
C bindings.

## Results on the GNOME 50 SDK

- The D-Bus and MPRIS executable builds against the existing Pinpoint
  dependencies and passes automated host and installed-Flatpak session-bus
  round trips. Flatpak's default filtered bus policy permits the app's own
  namespace and matching MPRIS names, so no broader session-bus permission is
  needed.
- A general-purpose GNOME desktop MPRIS client discovered and selected two
  simultaneous sandboxed instances independently. `Next` and `Previous`
  changed only the selected presentation, while `PlayPause` surfaced the
  adapter's deliberate `NotSupported` response.
- `librebonjour` 1.0.alpha configures and builds against GLib 2.88 and the
  SDK's libdex 1.1.0. It can provide DNS-SD discovery, but discovery proves
  neither peer identity nor possession of a pairing secret.
- `varlink-glib` 1.0.alpha requires libdex 1.2. The SDK provides 1.1.0, so it
  fails a no-fallback configure. Bundling current libdex 1.2.beta makes it build
  and permits the schema code-generation check, but that is too much young
  infrastructure to add to the production Flatpak before a real client proves
  the value.

No GSConnect, Valent, or `playerctl` client is installed. The local desktop gate
is nevertheless complete through the GNOME MPRIS client and the filtered
Flatpak session bus. The central [Pinpoint backlog](../TODO.md) keeps the
separate real-phone comparison outstanding; this prototype note records results
rather than maintaining another work list.
