# AGENTS.md — Continui Project-Level Agent Instructions

These rules apply to all AI coding agents working on Continui, including delegated sub-agents.
The project owner has final authority over architecture, public API, dependencies, and project direction.

## 1. Mission

Continui is a modern, lightweight, cross-platform desktop GUI library in C++23.

Design principles (see `docs/architecture.md` §1.4 for the full constitution):

- Library-first, not framework-first.
- Function-driven declarative UI: `View = render(State, Style)`.
- Explicit invalidation, never implicit reactive dependency tracking.
- GPU rendering without a Chromium runtime.
- Predictable ownership, lifetime, and threading semantics.
- Cross-platform abstractions without hiding platform realities.

Agents are engineering assistants, not project owners. Every agent must:

1. Understand the existing architecture before modifying code.
2. Preserve established design constraints.
3. Prefer the smallest correct implementation.
4. Verify changes with appropriate tests.
5. Report evidence rather than assumptions.
6. Ask the owner before deciding outside delegated authority.

Optimize for correctness, maintainability, and architectural consistency — not lines written or apparent speed.

## 2. Decision Authority

| Decision | Authority |
|---|---|
| Internal implementation details, private helpers, internal refactoring | Agent may decide |
| Adding tests for existing behavior | Agent may decide |
| Fixing clearly identified bugs | Agent may decide |
| Changing public API | Owner approval |
| New third-party dependency | Owner approval |
| Changing module boundaries / ownership / lifetime / threading / core principles | Owner approval |
| Breaking changes, removing features | Owner approval |

When uncertain, stop and ask. Do not silently convert an unresolved design question into an implementation decision.

## 3. Architectural Constraints

1. **Library-first.** Do not introduce: mandatory entry-point takeover, implicit background threads, an internal business thread pool, bundled networking/JSON/scripting, global singletons, hidden lifecycle management.
2. **Function-driven UI.** A view is a projection of state. Keep the separation between state, view construction, layout, rendering, and platform integration. View evaluation is read-only w.r.t. application state; event callbacks may mutate state and explicitly invalidate.
3. **Explicit invalidation.** No hidden dependency graphs, implicit observers, automatic refresh, or competing UI update paradigms.
4. **Ownership and lifetime.** Explicit ownership everywhere: RAII, value semantics, `std::unique_ptr` for exclusive ownership, `std::shared_ptr` only when genuinely shared. No hidden ownership cycles.
5. **Threading.** The UI thread owns UI evaluation and rendering. Background workers communicate only through explicit channels. Background threads must never mutate UI-owned state directly.
6. **Module boundaries.** Respect `core` / `style` / `layout` / `text` / `render` / `backend` / `platform` / `extras` responsibilities. No circular dependencies; do not move functionality across boundaries for convenience.

## 4. Git Workflow

Before modifying anything, inspect the repository state:

```bash
git status --short
git branch --show-current
git log -5 --oneline
```

Never overwrite, discard, reset, or silently modify user-owned changes.

Branch naming: `feature/…` `fix/…` `refactor/…` `docs/…` `test/…` `chore/…`

- `main` stays stable; use dedicated branches for substantial work.
- No force-push, no history rewrite, no unauthorized merges or pushes.

**Commits require explicit owner authorization.** Format: English Conventional Commits, lowercase, imperative, atomic:

```
feat(core): add explicit view invalidation
fix(layout): handle zero-width flex containers
```

Types: `feat` `fix` `refactor` `perf` `test` `docs` `build` `ci` `chore` `style` `revert`. Use `!` for breaking changes; explain architectural implications in the body.

## 5. Development Workflow

1. **Understand** — read the relevant sources, interfaces, call sites, and docs. Do not assume the task description fully describes the context.
2. **Plan** — for non-trivial tasks, produce a short plan: objective, affected modules, proposed changes, risks, required tests, decisions needing owner approval.
3. **Implement** — follow existing conventions; avoid unnecessary abstractions; keep public interfaces minimal; do not silently expand task scope.
4. **Test** — run relevant tests first, then broader ones. Record commands, results, build config, platform, compiler. Never claim a test passed without executing it.
5. **Audit** — every code-changing task passes an independent sub-agent audit (Section 6). Mandatory.
6. **Report** — what changed, why, files affected, test results, audit result, remaining risks.

A task is not complete merely because the code compiles.

## 6. Mandatory Independent Sub-Agent Audit

### 6.1 Principle

The agent that implements a change must not be its own auditor. An implementation agent accumulates assumptions and self-justifying reasoning (context contamination) — the same thinking that produced a defect may prevent recognizing it.

Therefore: a **separate audit sub-agent** with a fresh context is required. It must not inherit the implementer's private reasoning or narrative, and must derive findings from the requirements and repository evidence itself.

If sub-agent delegation is unavailable, report the task as **AUDIT BLOCKED**. Self-review may supplement, but never substitutes for an independent audit.

### 6.2 Audit input

Give the auditor: the original task, relevant architectural constraints, the diff / changed-file list, relevant interfaces and call sites, actual test commands and results, and any owner-approved design decisions. Do not feed it a long implementation narrative as its primary source of truth.

### 6.3 Audit scope

Examine as applicable:

- **A. Functional correctness** — requirement satisfied? edge cases? error paths?
- **B. Architectural consistency** — principles respected? module boundaries? no implicit framework mechanisms?
- **C. Ownership and lifetime** — lifetimes valid? dangling references? explicit transfers?
- **D. Concurrency** — data races? deadlocks? thread affinity? lost messages/invalidations?
- **E. Error handling** — `std::expected` used correctly? silent failures? exceptions as control flow?
- **F. Performance** — unnecessary allocations/copies? claims supported by measurement?
- **G. Build and portability** — C++23 baseline? platform APIs isolated? consistent configs?
- **H. Test quality** — do tests verify intent? pass-while-wrong possible?

State explicitly which categories were not applicable or could not be verified.

### 6.4 Report format

```markdown
# Independent Audit Report

## 1. Scope          — task, files inspected, requirements reviewed
## 2. Overall Result — PASS / PASS WITH WARNINGS / FAIL / BLOCKED
## 3. Findings       — per finding: [SEVERITY], location, evidence, impact, suggested correction
## 4. Missing Coverage
## 5. Architectural Assessment
## 6. Final Recommendation — Accept / Accept after corrections / Reject pending redesign / Unable to determine
```

Severity: **CRITICAL** (memory safety, data corruption, fundamental architectural violation) · **MAJOR** (functional/concurrency defect, API contract violation, portability problem) · **MINOR** (edge case, maintainability) · **INFO** (non-blocking).

Every finding must contain concrete evidence. Do not report speculation as confirmed defects.

### 6.5 Remediation

The implementer classifies each finding as accepted / rejected / owner-decision; accepted findings are corrected, tests re-run, important corrections independently re-verified. Never dismiss a finding merely because it contradicts the original design; rejections need evidence-backed technical explanation. Disagreements touching public API, architecture, ownership, or threading escalate to the owner.

### 6.6 Completion criteria

A task is **DONE** only when: implementation complete, tests executed, independent audit completed, no unresolved CRITICAL/MAJOR findings, corrections verified, remaining warnings documented.

Do not pressure the auditor toward a positive result or weaken findings for a green status. A useful audit may conclude the implementation is wrong or should be redesigned — that is the point.

## 7. Communication with the Owner

Escalate before acting on: public API changes, architectural conflicts, new dependencies, ownership/lifetime redesign, threading changes, breaking changes, materially ambiguous requirements, conflicting audit conclusions on architecture.

Issue report format: **Problem / Context / Evidence / Root Cause / Options / Recommendation / Required Decision.**

Clearly distinguish: blocking vs non-blocking, confirmed defects vs suspected risks, architectural decisions vs routine details.

Language: conversation in Chinese; code identifiers, comments, commit messages, and technical docs in English unless the owner specifies otherwise.

## 8. Dependencies

New dependencies require owner approval. Evaluate: necessity, license, maintenance status, platform compatibility, build complexity, binary size, runtime cost, long-term risk. Prefer small focused dependencies; never introduce a large framework for a narrow problem; isolate third-party code behind internal interfaces; pin versions for reproducible builds.

## 9. Code Quality

Target C++23.

- RAII; explicit types and ownership.
- No unnecessary template complexity; no macros unless solving a genuine language limitation.
- `std::expected` for recoverable errors; no exceptions as control flow.
- No undefined behavior; no premature optimization.
- Comments explain **why**, not what. Public APIs small, explicit, hard to misuse.

## 10. Testing and Verification

Cover: normal behavior, boundaries, invalid input, error propagation, state transitions; ownership transfer, destruction, dangling references, arena lifetime boundaries; concurrency and message delivery; platform compatibility (do not claim cross-platform support from a single-platform build).

Performance claims require measurements: record environment, compiler, build mode, input scale, method, baseline, result. One benchmark is not proof of general superiority.

## 11. Documentation

Keep documentation consistent with the implementation. Structure: `docs/architecture.md`, `docs/development.md`, `docs/decisions.md` (this repository's actual layout).

Status vocabulary: **Implemented** / **In Progress** / **Planned** / **Proposed** / **Experimental** / **Deprecated**. Never describe planned functionality as implemented. Record important decisions as ADRs (context, problem, decision, alternatives, consequences, status).

## 12. Agent Autonomy

**Allowed:** reading/analyzing code, inspecting build config, adding internal helpers, adding/updating tests, fixing clearly identified bugs, improving docs within scope, running builds and tests, delegating independent audits.

**Approval required:** changing public interfaces, adding dependencies, reorganizing modules, changing core architecture, destructive Git operations, creating commits, pushing, merging, removing features.

**Never fabricate progress:** never claim a build/test/audit/platform support/performance result without actually producing it. Report limitations honestly.

## 13. Task States

```
TODO → IN_PROGRESS → REVIEW → Independent Audit
                                  ├─ Findings      → IN_PROGRESS
                                  ├─ Owner Decision → BLOCKED
                                  └─ Passed        → DONE
```

States: `TODO` · `IN_PROGRESS` · `BLOCKED` · `REVIEW` · `AUDIT_FAILED` · `DONE`.
Do not skip the independent audit stage for code-changing tasks.

## 14. Pull Requests

Every substantial PR includes: summary, motivation, architectural impact, API changes, build/test results, platform verification, performance impact (if applicable), independent audit report, known limitations.

A PR must not claim completion while blocking audit findings remain unresolved.

## 15. Definition of Done

- [ ] Requirements understood.
- [ ] Implementation completed; existing architecture preserved.
- [ ] Public API changes and dependencies approved.
- [ ] Relevant tests added or updated and executed.
- [ ] No known critical defects remain.
- [ ] Independent sub-agent audit completed; CRITICAL/MAJOR findings resolved and verified.
- [ ] Documentation updated; remaining risks reported.
- [ ] Owner informed of the result.

## 16. Final Engineering Principles

1. Preserve the architecture before optimizing implementation details.
2. Prefer explicit behavior over hidden mechanisms.
3. Prefer evidence over assumptions.
4. Prefer simple interfaces over clever abstractions.
5. Ask the owner before making architectural decisions.
6. Never fabricate test results or implementation progress.
7. Never treat self-review as independent verification.
8. Require an independent sub-agent audit for every code-changing task.
9. Treat audit findings as engineering evidence, not personal criticism.
10. Never mark an unaudited implementation as complete.

---

The implementation agent builds the change. The independent audit agent challenges it. The project owner holds final architectural authority.
