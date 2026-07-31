# Development workflow

Pinpoint uses the pinned GNOME 50 Flatpak SDK as its build and test environment.
Do not rely on host library versions: the runtime contract is the one declared
by the manifests in `flatpak/`.

## Build and test

Install the development app first. This gives Glycin and other portal services
Pinpoint's Flatpak identity during display-backed tests:

```sh
flatpak-builder --user --install --force-clean build-dir \
  flatpak/com.nedrichards.pinpoint.Devel.json
```

Configure, compile, and run the complete suite in the GNOME 50 SDK:

```sh
flatpak run --user --filesystem="$PWD" --command=meson \
  org.gnome.Sdk//50 setup "$PWD/_build"
flatpak run --user --filesystem="$PWD" --command=meson \
  org.gnome.Sdk//50 compile -C "$PWD/_build"
flatpak run --user --filesystem="$PWD" --device=dri \
  --talk-name=org.freedesktop.Flatpak --socket=wayland --socket=fallback-x11 \
  --command=meson org.gnome.Sdk//50 test -C "$PWD/_build" \
  --print-errorlogs --wrapper="$PWD/tests/run-in-devel-flatpak.sh"
```

Meson treats warnings as errors. Reconfigure an existing build directory after
changing options or dependencies.

## Run locally

Run the uninstalled binary against a presentation:

```sh
_build/src/pinpoint talk.pin
_build/src/pinpoint --edit talk.pin
```

The composition editor is an optional module. Meson's `editor` option defaults
to `auto`; the Flatpak manifests enable it explicitly and supply GtkSourceView.
Use `-Deditor=disabled` only to verify the presentation-only build. The
`remote_prototypes` option is off by default and builds the experimental
remote-control prototype when explicitly enabled.

## Focused quality gates

Run the normal Meson suite for every change. Add the relevant focused gate when
the affected subsystem warrants it:

```sh
tests/run-gcc-analysis.sh
tests/run-leak-checks.sh
tests/run-coverage.sh
```

- Use the GCC analyzer for ownership, cleanup, or control-flow changes.
- Use leak checks for rendering, media, window, and other lifetime-sensitive
  work.
- Use the coverage gate when changing tested behavior or its exclusions; see
  [coverage.md](coverage.md).
- Use the performance procedure for render-loop, caching, media, or PDF changes;
  see [performance.md](performance.md).
- Use the host display procedure for fullscreen, speaker-view, monitor-routing,
  or inhibition changes; see
  [host-display-automation.md](host-display-automation.md).

Add focused GLib tests under `tests/` for behavioral changes and minimal
fixtures under `tests/fixtures/`. User-visible format or compatibility changes
must update their authoritative document as part of the same change.

## Flatpak manifests

- `flatpak/com.nedrichards.pinpoint.Devel.json` builds the current checkout and
  installs the development application ID.
- `flatpak/com.nedrichards.pinpoint.json` is the production-shaped manifest.

Keep dependencies and build options aligned unless a difference is explicitly
development-only. Validate release metadata with the Meson suite and
`appstreamcli validate --no-net _build/data/com.nedrichards.pinpoint.metainfo.xml`.
