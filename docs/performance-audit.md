# Whole-codebase performance audit

The initial audit was performed on 20 July 2026 and reviewed against the current
implementation on 30 July 2026. This document records decisions and remaining
opportunities; reproducible commands, budgets, and measurements belong in
[performance.md](performance.md), while ownership and cache mechanics belong in
[rendering-pipeline.md](rendering-pipeline.md).

The audited runtime was the pinned GNOME 50 SDK: GLib 2.88.2, GTK 4.22.4,
libadwaita 1.9.2, GStreamer 1.26.11, librsvg 2.62.3, Cairo 1.18.4, Pango 1.57.1,
and GtkSourceView 5.20.0.

## Conclusions

The parser and transition paths are linear in the amount of source or visible
content and do not contain accidental quadratic loops. The important repeated
costs were resource decoding, speaker-preview regeneration, PDF raster
retention, and avoidable idle callbacks. Those issues have been addressed.

| Area | Current decision |
| --- | --- |
| Speaker view | Reuses stage rendering and schedules timing updates only while timing is active. |
| Asset resolution | Caches source-relative resolution and detected content types per presentation. |
| Raster and SVG assets | Shares resolved assets and decoded data; raster textures use a 64 MiB least-recently-used budget. |
| PDF export | Keeps stable slide IDs and one current raster surface, supports cancellation, and bounds raster dimensions. |
| Media | Uses GStreamer and Glycin asynchronous paths rather than decoding on the GTK thread. |
| Text | Uses GTK/Pango layout through normal snapshot rendering; no private text cache was justified. |
| GtkSvg | Remains unused because the optional dependency does not replace the shared librsvg path or improve format coverage. |
| Page curl | Keeps the straightforward mesh implementation until profiling justifies additional invalidation complexity. |

## Copy and ownership map

The remaining copies are intentional and bounded:

- Source text is read once, normalized once, and tokenized into owned parser
  storage so reloads cannot invalidate live slides.
- Raster images are decoded asynchronously and cached as shared textures.
- SVG source bytes and handles are shared across consumers; view-specific
  rasterization is generated at the required output size.
- Text strings remain presentation-owned and Pango creates transient layout
  state while snapshotting.
- GStreamer owns decoded video and camera frames; Pinpoint receives paintable
  updates.
- PDF export owns only the current raster surface and releases it before the
  next slide.
- Page-curl vertices are rebuilt for animated frames because their geometry
  changes with progress.

## Remaining opportunity

Retained slide composition could reduce repeated snapshot-time work for static
slides. It would also introduce invalidation rules across live reload, media,
transitions, scaling, and speaker view. It should therefore be adopted only
after a prototype demonstrates a material gain on representative decks.

The page-curl mesh is a smaller related opportunity. Cache it only if profiles
show geometry construction is material; otherwise the current simple ownership
model is preferable.

Physical testing should continue across mixed-scale displays and different GPU
drivers. These items are tracked in the central [backlog](../TODO.md).

## Guardrails

- Do not replace toolkit snapshot rendering with a parallel renderer without a
  measured bottleneck and a fidelity plan.
- Do not keep unbounded decoded assets or PDF slide surfaces alive.
- Do not move GTK objects or snapshot work off the GTK thread.
- Do not add caches without explicit invalidation and ownership rules.
- Do not weaken live editing, source-relative asset behavior, or legacy format
  compatibility for a synthetic benchmark.
