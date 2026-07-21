# Command-line interface

Pinpoint retains the original 0.1.8 presentation options and adds scriptable
checking and PDF controls. A valid historical invocation remains valid:

```sh
pinpoint [--maximized] [--fullscreen] [--speakermode] [--rehearse] \
  [--ignore-comments] [--camera=DEVICE] presentation.pin
```

Exactly one presentation may be supplied. Running without one opens the GTK
setup screen; supplying more than one is an error instead of silently ignoring
the additional paths.

Use `--version` for the installed version and `--help` for the complete option
list.

## Checking a presentation

`--check` provides a non-interactive compatibility check suitable for editor,
CI, and packaging workflows:

```sh
pinpoint --check presentation.pin
```

It verifies that the source is readable UTF-8, parses it with the same
compatibility parser used for presenting, checks referenced relative image,
SVG, and video backgrounds, and loads each custom legacy transition JSON. It
does not execute embedded commands, open a camera, or decode media. Built-in
transition names and remote or absolute assets retain their normal runtime
handling.

A successful check prints the slide count and exits 0. A read, parse, missing
asset, or custom-transition error is written to standard error and exits 1.
`--check` cannot be combined with `--output`.

## PDF export

PDF export is non-interactive when `--output` is present:

```sh
pinpoint --output=talk.pdf --pdf-page-size=a4 \
  --pdf-orientation=landscape --pdf-no-speaker-notes talk.pin
```

Paper size accepts `a4` or `letter`; orientation accepts `landscape` or
`portrait`. `--ignore-comments` excludes comment notes. These PDF options also
set the initial choices in the graphical exporter when `--output` is omitted.

The PDF is rendered to a temporary file beside the destination and moved into
place only after every page succeeds. A failed or cancelled export therefore
preserves an existing destination. Pinpoint also refuses an output that is the
presentation source, including a different path resolving to that same file.

Progress is written to standard error only when it is a terminal, leaving
redirected and scripted exports quiet on success.

## Interruption and exit status

- Normal completion or an ordinary graphical close exits 0.
- Invalid options, ambiguous arguments, failed checks, and failed exports exit
  1 with a `pinpoint:` diagnostic on standard error.
- Ctrl+C cancels work, preserves source and destination files, and exits 130.
- SIGTERM performs the same orderly cancellation and exits 143.
- A second termination signal forces an immediate exit if a decoder, file
  system, or other dependency does not respond to cancellation.

Ctrl+C during rehearsal deliberately discards the in-progress timing changes,
matching the original command-line contract. Timings are written only after a
rehearsal reaches its normal completion path.
