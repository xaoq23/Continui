# ADR-009 — Dawn as the First Graphics Backend

Status: **Accepted** (owner decision, 2026-11-18)

## Decision

Use Dawn as the first GPU backend. Do not wrap raw D3D12/Metal/Vulkan by hand.

## Rationale

- A raw-API wrapper is a second project (memory allocators, synchronization,
  per-backend shader dialects) that delays any visible result. Dawn provides a
  Chromium-hardened, spec-stable WebGPU implementation maintained upstream.
- Reused from the WebGPU ecosystem: mature resource/synchronization model, a
  single shader language (WGSL → per-backend translation inside Dawn/Tint),
  and known shader-level techniques (SDF text, MSAA) rather than feature
  drop-ins. Font shaping/loading stays FreeType + HarfBuzz (ADR-010); path
  rendering starts as CPU-flattened batches (Phase 0.4).
- `gfx::Device` stays the only surface business code sees; native backends
  remain possible later and are decided by profiler data (ADR-014).

## Alternatives considered

- **wgpu-native**: Rust-ecosystem library; smaller build, but the C++ binding
  story is weaker and the project sits outside the Chromium maintenance orbit.
- **Hand-written D3D12 wrapper**: full control, prohibitive early cost.
- **Deferred GPU entirely**: blocks Phase 0.3 acceptance; rejected.

## Costs / constraints

- Large vendored dependency; pinned release tag required. Cold build is
  minutes-scale; build prerequisites: Python 3 + jinja2.
- Static-link footprint in the MB range (accepted in the README positioning).
- First backend is Windows/D3D12; other adapters follow with platform phases.
