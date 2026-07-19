# Host display automation

`tests/run-host-display-test.sh` exercises the compositor-visible part of a
two-screen presentation in a real GNOME session. It is intentionally separate
from Meson's sandbox-safe suite because it inspects and focuses host windows and
injects the same keyboard shortcuts a presenter uses.

Before running it:

1. Connect at least two displays.
2. Compile the normal `_build` tree.
3. Open Looking Glass with **Alt+F2**, enter `lg`, and evaluate:

   ```js
   global.context.unsafe_mode = true
   ```

Then run:

```sh
tests/run-host-display-test.sh
```

The runner launches `tests/fixtures/multi-monitor.pin` with fullscreen speaker
mode, then verifies through GNOME Shell that:

- the audience and speaker windows both become fullscreen;
- they occupy two distinct monitors;
- the speaker view's `S` action exchanges their monitor assignments; and
- `F11` leaves and restores the two-display fullscreen arrangement.

The runner exits with status 77 when unsafe mode or a second monitor is absent,
and fails for an incorrect window state. It does not change Pinpoint's Flatpak
permissions or leave a testing API in the application. Disable Shell evaluation
again afterwards if desired:

```js
global.context.unsafe_mode = false
```

Physical connector removal cannot be generated reliably by a window-automation
process. The production selection policy is therefore isolated in
`pp-display-selection.c` and covered separately with deterministic one-, two-,
and three-display cases, removal of either assigned display, and replugging.
The host runner retains responsibility for the actual GTK-to-Mutter fullscreen
and swap boundary. Its outstanding physical validation status lives in the
central [Pinpoint backlog](../TODO.md), not in this procedure.
