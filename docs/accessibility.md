# Accessibility

Pinpoint keeps its setup workflow in native GTK and libadwaita controls so
keyboard focus, control roles, switch state, menu structure, and platform text
scaling come from the desktop toolkit. Icon-only setup actions also have
explicit accessible names rather than relying on their icons or tooltips.

The presentation stage is custom-rendered, so Pinpoint supplies the semantics
which GTK cannot infer from child widgets. The focused stage exposes:

- its role as a grouped presentation surface;
- the current position, such as “Presentation slide 2 of 12”;
- the visible audience text with Pango markup removed;
- the blank-screen state; and
- the non-standard presentation keyboard controls as shortcut and help text.

Slide changes update those properties while focus remains on the stage. The
speaker view identifies its three rendered previews as previous, current, and
next slides. Speaker notes, remaining time, and the visual timing indicator
have contextual names. Every toolbar operation remains a focusable text button;
the display-swap and fullscreen buttons also publish their `S` and `F11`
shortcuts.

Pinpoint follows GTK's `gtk-enable-animations` setting. When desktop animations
are disabled, slide transitions complete immediately rather than running a
reduced but still moving effect.

## Author responsibility

Audience text is available to assistive technology, but the historical `.pin`
format has no backward-compatible field for alternative descriptions of image,
video, SVG, or camera backgrounds. Unknown bracketed settings deliberately mean
“background”, so introducing an `[alt=…]` setting would make the same file render
incorrectly in historical Pinpoint. Authors should therefore avoid relying on
an uncaptioned visual as the only source of essential information. The central
[Pinpoint backlog](../TODO.md) owns the work to design visual descriptions
without violating that historical parser rule.

Colour contrast inside a slide is likewise selected by the presentation
author. Pinpoint retains the historical shading controls and defaults to a
dark, partially opaque text backdrop, but it does not rewrite authored colours.

## Regression coverage

`tests/test-lifecycle.c` checks the stage role, initial and changing slide
labels, audience text, blank state, contextual preview naming, and the
reduced-motion path. The normal GTK lifecycle test remains display-backed so
these assertions use GTK's real accessible context rather than a parallel model.
