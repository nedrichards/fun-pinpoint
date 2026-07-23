# Repository Guidelines

## Project Structure & Module Organization

Pinpoint is a GTK 4/libadwaita rebuild of the legacy presentation tool. Keep
application code in `src/`: the `pp-*.c`/`.h` pairs group a subsystem (for
example, `pp-presentation` or `pp-pdf`), while `main.c` owns application setup.
`data/` contains desktop metadata, resources, syntax highlighting, and the
bundled introduction. Put regression fixtures in `tests/fixtures/` and their
GLib tests in `tests/test-*.c`. `docs/` records the `.pin` format, compatibility
contract, rendering choices, and release-quality policies; update it when a
user-visible contract changes. Flatpak manifests live in `flatpak/`.

## Build, Test, and Development Commands

Use the pinned GNOME 50 SDK, not the host toolchain:

```sh
flatpak run --user --filesystem="$PWD" --command=meson org.gnome.Sdk//50 setup "$PWD/_build"
flatpak run --user --filesystem="$PWD" --command=meson org.gnome.Sdk//50 compile -C "$PWD/_build"
flatpak run --user --filesystem="$PWD" --device=dri --talk-name=org.freedesktop.Flatpak --socket=wayland --socket=fallback-x11 --command=meson org.gnome.Sdk//50 test -C "$PWD/_build" --print-errorlogs --wrapper="$PWD/tests/run-in-devel-flatpak.sh"
```

Install the development app with `flatpak-builder --user --install --force-clean
build-dir flatpak/com.nedrichards.pinpoint.Devel.json` before the test suite so
Glycin has Pinpoint's Flatpak identity. Run `tests/run-gcc-analysis.sh`,
`tests/run-leak-checks.sh`, or `tests/run-coverage.sh` for the corresponding
host-session quality gates. Use `_build/src/pinpoint talk.pin` for local runs.

## Coding Style & Naming Conventions

Write C17 with two-space indentation, braces on the same line as control
statements, and GLib conventions (`g_autoptr`, `g_return_*`, `g_assert_*`). Name
subsystem files `pp-<feature>.c` and `pp-<feature>.h`; use `pp_` for exported
symbols and `static` for private helpers. Keep headers and Meson source lists
in sync. Warnings are errors; avoid introducing GCC analyzer findings.

## Testing Guidelines

Add a focused GLib test with `g_test_add_func()` for each behavioural change;
use slash-separated paths such as `/parser/invalid-source`. Add minimal `.pin`,
JSON, SVG, or media fixtures when reproducing compatibility cases. Run the
Meson suite, and run the relevant display, leak, performance, or coverage gate
when touching rendering, media, or lifetime-sensitive code.

## Commits & Pull Requests

Use concise Conventional Commit-style subjects seen in history, such as
`fix: reload presentations after external saves` or `docs: document the command-line contract`.
Keep commits single-purpose. PRs should explain behaviour and compatibility
impact, list validation commands, link the issue when applicable, and include
screenshots or recordings for visible GTK changes.
