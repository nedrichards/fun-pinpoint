# Documentation

This directory holds Pinpoint's durable contracts and implementation notes.
Start with the document that matches the question; each subject has one
authoritative home.

## Using and authoring presentations

- [Presentation format](presentation-format.md) — complete `.pin` syntax and
  asset-resolution reference.
- [Composition editor](composition-editor.md) — source-first editing, preview,
  saving, diagnostics, and integrated rehearsal.
- [External editors](external-editors.md) — live reload and GtkSourceView
  language support outside Pinpoint.
- [Command line](command-line.md) — stable options, exit statuses, PDF export,
  and invalid combinations.
- [Media formats](media-formats.md) — supported image, SVG, audio, video, and
  camera behavior.
- [Remote control](remote-control.md) — shared command model, MPRIS contract,
  prototype results, and future peer architecture.

## Product contracts

- [Compatibility](compatibility.md) — implemented legacy behavior and deliberate
  differences from the original Pinpoint.
- [Accessibility](accessibility.md) — keyboard, assistive-technology, contrast,
  and motion expectations.
- [Session restoration](session-restore.md) — deferred GTK 4.24 design and
  privacy boundary.

## Architecture and performance

- [Rendering pipeline](rendering-pipeline.md) — content ownership, scaling,
  caching, transitions, media, and export architecture.
- [Performance](performance.md) — measurement procedures, budgets, and recorded
  results.
- [Performance audit](performance-audit.md) — audit decisions, completed
  optimizations, and remaining opportunities.

## Development and validation

- [Development](development.md) — authoritative GNOME SDK build, test, analysis,
  and local-run workflow.
- [Coverage](coverage.md) — coverage gate and justified exclusions.
- [Host display automation](host-display-automation.md) — real two-display and
  fullscreen-inhibition validation.

Experimental remote clients live under [prototypes](../prototypes/README.md).
Open work is tracked only in the top-level [backlog](../TODO.md).
The project licence is [LGPL 2.1 or later](../COPYING), and bundled introduction
media provenance is recorded in [its origin file](../data/introduction/ORIGIN.md).
