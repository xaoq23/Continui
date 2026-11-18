# ADR-010 — HarfBuzz + FreeType Text System

Status: **Accepted** (owner decision, 2026-11-18)

## Decision

Text rendering is built on **HarfBuzz** (shaping) and **FreeType** (font loading
and glyph rasterization), vendored through FetchContent at pinned release tags:

- FreeType `VER-2-15-0` (tag, 2026-10; ClearType rendering fixes; trims:
  PNG / Brotli / Bzip2 / bundled HarfBuzz disabled — shaping is our own
  HarfBuzz, never FreeType's internal copy).
- HarfBuzz `15.1.0` (2026-11; trims: subset / ICU / glib / FreeType reverse
  dependency / tests / utils disabled).

Both follow the Dawn vendoring pattern (ADR-009): pin + trim, no bundled
copies, no build-system surprises.

## The shape of the pipeline (Phase 0.5)

```
UTF-8 string + FontFace
  → HarfBuzz shaping (glyph ids + advances + offsets, latin single-run)
  → GlyphRun (plain value type)
  → per-glyph rasterization cached in a strike keyed by
      (face, pixel size, x-subpixel bucket [quarters], no y bucket)
  → glyphs packed into one RGBA white-mask atlas (straight alpha,
      encoded (255, 255, 255, c))
  → Painter::text() records one quad per glyph; same-atlas quads merge
      into a single batch → few draw calls
```

The white-mask encoding is the load-bearing choice: a glyph coverage value
`c` becomes a straight-alpha texel `(255, 255, 255, c)`. The Phase 0.4
pipeline already converts straight→premultiplied in the fragment stage and
multiplies by premultiplied tint, so `(255,255,255,c) → (c,c,c,c) × tint`
renders correctly colored text with **zero shader or blend-state changes**.

## Decided details (owner-approved)

1. **Atlas strategy**: dynamic append via a shelf packer; when full, grow by
   rebuilding into a larger atlas (Flutter's strategy, including height
   adjustment). Phase 0.5 has **no eviction** — glyphs accumulate for the
   renderer's lifetime, with a capacity cap as protection. LRU and memory
   budgets arrive when profiled.
2. **Strike key**: `(face, pixel size, x-subpixel bucket)`. X positioning
   uses 4 quarter-pixel buckets (the Chromium/Flutter common floor); no
   Y bucket (GUI text baselines are effectively fixed).
3. **Gamma/contrast correction is in Phase 0.5**, not deferred: GPU text
   rendered in linear space makes light-on-dark text look too thin. The
   fragment stage applies an exponent correction interpolated by ink-color
   luminance (Flutter's approach; Windows reference coefficient 1.6
   matching DirectWrite/ClearType visual weight).
4. **No SDF glyphs.** Alpha masks at native size; Skia itself restricts SDF
   to the ~32–162 px range and loses small-size stroke detail. (The Phase
   0.4 shape SDF is a different technique and unaffected.)
5. **Explicitly out of scope for 0.5** (each is its own later phase):
   LCD subpixel antialiasing, color glyphs (COLR/emoji), bidirectional text,
   complex scripts, CJK line breaking. A per-character **font fallback
   chain is required by the 0.7 IME phase** — single-face 0.5 shows
   `.notdef` for missing glyphs.

## Dependency direction

`text → core` (no GPU types); `render` consumes `GlyphRun`; `backend` consumes
atlas bytes. Matches architecture.md §3: no cycles, no GPU knowledge in text.

## Alternatives considered

- **DirectWrite rasterization (Windows)**: higher visual ceiling on Windows
  (ClearType hinting), and what Flutter uses there. Kept as a possible later
  FontFace backend behind the same interface; FreeType gives the
  cross-platform baseline first.
- **stb_truetype**: smaller, but no shaping story and weaker hinting;
  rejected.
- **SDF glyph atlas**: rejected per detail 4 — Skia limits it to a mid-size
  band precisely because small sizes lose stroke detail.
- **Deferred gamma correction**: rejected — the "our text looks subtly
  wrong" class of bug is expensive to diagnose after the fact, and the fix
  is two lines in the fragment stage.

## Costs / constraints

- Two vendored dependencies; both industry-standard, permissive
  (FreeType License / MIT-like), cross-platform, CMake-friendly.
- RGBA white-mask atlas costs 4× memory vs a single-channel atlas; recorded
  as the first optimization when the R8 path is added (requires a shader
  variant, hence deferred).
- Sub-pixel buckets multiply cache entries ×4 in x; accepted as the
  industry floor.
