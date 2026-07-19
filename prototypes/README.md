# Remote-control prototypes

These programs and interface definitions are experiments, not installed
Pinpoint features or stable public APIs. They build only when Meson's
`remote_prototypes` option is enabled.

```sh
meson setup _build-prototypes -Dremote_prototypes=true
meson compile -C _build-prototypes
prototypes/run-prototype-tests.sh
```

`pinpoint-remote-prototype` simulates a presentation while exposing the same
`PpControl` actions through two local transports:

- standard `org.gtk.Actions` on a per-process D-Bus name; and
- an intentionally conservative MPRIS player with `Next` and `Previous`, slide
  metadata, and fullscreen state.

MPRIS reports `Stopped` and rejects play, pause, seek, and volume operations.
Those media concepts are not truthful presentation concepts until Pinpoint has
a timer or autoadvance feature that they can actually control. The protocol
test also starts two instances to verify that their bus names remain distinct.

The Varlink file is the equivalent peer-facing command contract. It exposes no
file, URI, arbitrary command, or slide-jump surface. It deliberately does not
specify a socket or network address: `varlink-glib` accepts an already-connected
`GIOStream`, so the application must establish an authenticated and encrypted
stream before attaching this API. Run `validate-varlink-schema.sh` with an
upstream `varlink-codegen` binary to parse the interface and generate temporary
C bindings.

## Results on the GNOME 50 SDK

- The D-Bus and MPRIS executable builds against the existing Pinpoint
  dependencies and passes automated session-bus round trips.
- `librebonjour` 1.0.alpha configures and builds against GLib 2.88 and the
  SDK's libdex 1.1.0. It can provide DNS-SD discovery, but discovery proves
  neither peer identity nor possession of a pairing secret.
- `varlink-glib` 1.0.alpha requires libdex 1.2. The SDK provides 1.1.0, so it
  fails a no-fallback configure. Bundling current libdex 1.2.beta makes it build
  and permits the schema code-generation check, but that is too much young
  infrastructure to add to the production Flatpak before a real client proves
  the value.

No GSConnect, Valent, or `playerctl` client was available during the spike.
Those real-client checks remain necessary before choosing a production adapter.
