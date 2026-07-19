# C coverage policy

Pinpoint measures every authored production C file with GCC 15 and `gcov` from
the pinned GNOME 50 SDK. It does not require LLVM, `gcovr`, `lcov`, or a host C
toolchain.

Run the gate from a graphical host session:

```sh
tests/run-coverage.sh
```

The runner creates `_build-coverage` with Meson's `b_coverage` option, deletes
stale counters, and builds in the SDK. It then executes the parser, rendering,
performance, pixel-profile, lifecycle, and whole-application tests directly on
the host while resolving their libraries and GStreamer helpers from that exact
SDK commit and its automatically installed codec extension. It also resolves
the active EGL, DRI, GBM, and Vulkan paths from the Flatpak GL extension version
declared by the SDK. A missing extension or failed OpenGL 3.3 page-curl context
is a test failure, not a skip or software fallback.
The focused page-curl lifecycle creates textures, compiles and links its GLSL,
uploads both pages, renders both directions, and destroys the GL resources.
This host boundary is necessary for the real Wayland display and is the same
one used by the sanitizer gate. The instrumented executable-size test is
intentionally omitted because coverage instrumentation changes the property it
measures; the normal Meson suite remains its gate.

The complete machine-readable result is
`_build-coverage/meson-logs/coverage.json`. The report includes line, function,
and branch counts for `src/*.c`. Test sources, generated GResource C, system
headers, and dependencies are outside the denominator. Each instrumented object
is collected separately before counts are merged, so a source linked into more
than one test executable cannot overwrite another executable's results.

## Enforcement

`tests/coverage-policy.json` is a ratchet, not a claim that the current suite is
complete. Every production source must have reviewed limits. A run fails when
the number of uncovered lines, functions, or branches exceeds any recorded
limit, and a new C file fails until its policy is reviewed. Limits should only
move down as tests improve.

Deterministic modules use all three limits. Integration-heavy files currently
use a line limit because GTK frame scheduling, process startup, portals,
GStreamer, PipeWire, and graphics drivers can produce compiler-branch variation
without changing application behaviour. Their function and branch figures are
still reported and should guide new scenario tests.

The parser line ratchet retains defensive fallbacks for content types which
GLib cannot map to a MIME type. Normal file and URI guesses do not produce that
state in the test environment, so the guards remain explicitly uncovered.

The current deterministic line gates include:

- `pp-page-curl.c`: 100% executable lines.
- `pp-render.c`: 100% reachable lines after its exhaustive-enum abort arm.
- `pp-file-access.c`: 100% reachable lines after its injected-enumerator-failure
  cleanup.
- `pp-introduction.c`: 100% reachable lines after its injected partial-copy
  rollback.
- `pp-transition.c`: 100% reachable lines after the JSON-GLib wrong-node-type
  critical path.

Each of the six current exclusions applies only to a specific line with a
written reason and an
expected source fragment. The reporter fails if the line moves, changes, becomes
covered, or is no longer executable. Broad file, directory, GUI, or error-path
exclusions are not accepted.

## What coverage does not prove

Executing a line does not establish that its output was correct, visually
accurate, leak-free, fast, or hardware accelerated. Coverage therefore remains
alongside the pixel profiles, ASan/UBSan/LSan gate, GCC analyzer, lifecycle and
file-descriptor tests, performance budgets, and production Flatpak build.

The direct host runner uses the base SDK's library and GStreamer paths plus its
matching active Flatpak GL extensions. It can exercise media correctness and
the real accelerated page-curl path without loading every hardware-codec
extension that the packaged Flatpak runtime exposes. DMA-BUF, negotiated
pixel-format, and hardware-decoder proof belongs to the separate Wayland
media-pipeline and host performance validation rather than this coverage score.
