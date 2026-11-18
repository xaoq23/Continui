# ADR-020 — Primary Toolchain: clang++ Targeting the MSVC ABI

Status: **Accepted** (owner decision, 2026-11-18)

## Decision

clang++ (target `x86_64-pc-windows-msvc`, linking the MSVC STL/runtime) is the
primary development toolchain. MSVC `cl.exe` remains a periodic portability
cross-check; MinGW (GNU ABI) is not supported for Continui on Windows.

## Rationale

- The MSVC ABI keeps Dawn (ADR-009) on its best-supported Windows path.
- Clang diagnostics (`-Wall -Wextra -Wpedantic`, the project-wide warning
  policy) apply verbatim with the GNU-style driver through Ninja.
- The alternative — MinGW ABI clang/gcc — conflicts with the backend decision:
  Dawn support there is weak and unofficial.

## Consequences

- Build presets grow a Ninja + clang++ variant (single-config; no
  `configuration` field needed).
- Portability across MSVC/MinGW consumers is still verified by the periodic
  cl.exe / MSYS2 builds performed during audits, not by daily development.
