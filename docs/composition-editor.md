# Composition mode

Composition mode is Pinpoint's focused `.pin` editing environment. Open it
with **New Presentation**, **Edit** on a selected deck, or:

```sh
pinpoint --edit talk.pin
```

It is intentionally smaller than Builder, but opens at a practical composition
size. One window owns one deck: a compact slide outline, GtkSourceView source
buffer, presentation-quality preview, and the actions needed to save, present,
and rehearse. The editor follows
libadwaita's light or dark appearance and changes its GtkSourceView Pinpoint
scheme and diagnostic colours immediately when the desktop appearance changes.

## Editing and preview

The outline uses the first audience-text line as each slide title. It presents
a compact numbered list with left-aligned, wrapping titles and keeps the
current slide selected and scrolled into view. Selecting a slide with a pointer
or touch scrolls its source into view, places the insertion cursor at the first
body line, and updates the preview. Moving the cursor updates the outline and
preview without moving the insertion point to the start of the slide. The
buffer is parsed after a 200 ms idle delay. Valid edits replace the preview
without writing the file or disturbing the cursor. If an incomplete edit
cannot be parsed, Pinpoint keeps the last valid preview and labels it paused
until the source is valid again.

Warnings underline malformed brackets, unknown keyed settings, invalid enum
values, and invalid numeric values. Ctrl+Space offers settings, enumerated
values, files beside the presentation, slide separators, speaker notes, and
visual-description directives. The status line summarizes slides and problems;
hover it for the first diagnostic.

Syntax colour reinforces the format without turning slide prose into code:
slide separators and commands are blue, settings and defaults are orange, Pango
markup is distinct from its attribute values, and notes are quietly italic.
Audience text retains the normal editor foreground so it remains the dominant
thing to read.

The preview uses the production `PpStage` renderer for text, images, SVG,
video, and transitions. It remains safe while typing: audio is muted and slide
commands and cameras are not activated. **Present** or Ctrl+Enter explicitly
enters the normal presentation path from the current slide. **Rehearse** or
Ctrl+Shift+R keeps the composition window open and starts the speaker view
alongside it, so notes and the live preview remain available during timing.
The rehearsal presentation starts from the source at that moment; edits made
while rehearsing update the editor preview and are included if you apply the
recorded timings, but do not rewrite the running rehearsal mid-slide.

## Saving and external changes

Previewing never saves implicitly. Ctrl+S saves atomically; Ctrl+Shift+S saves
under a new name. Pinpoint tracks the file revision so it cannot silently
overwrite an external change. A clean buffer reloads external saves
automatically. If both copies changed, choose whether to keep editing, reload,
or save the buffer under another name. Closing a modified deck offers Save,
Discard, and Cancel.

Rehearsal operates on the current in-memory buffer, including unsaved edits.
After the final slide, **Apply Timings** inserts or replaces only the slides'
`duration=` values as one undoable buffer action. It does not save. Discarding
leaves the source untouched. Standalone rehearsal uses the same exact source
patcher and refuses to overwrite a deck changed by another process.

## Optional module boundary

The editor is built with `-Deditor=enabled` in Pinpoint's Flatpaks. Native and
special-purpose builds may use `-Deditor=disabled`; `auto` builds it when
GtkSourceView 5.20 is available. The main `pinpoint` executable loads the
private editor module only after an Edit action or `--edit`, keeping
GtkSourceView out of ordinary presentation startup and memory use. External
editors remain a first-class workflow.
