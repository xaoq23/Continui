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
5. **Verify** — per risk tier (Section 6): independent audit for high-risk changes, independent task review for normal functional changes, regular checks for low-risk changes.
6. **Report** — what changed, why, files affected, test results, verification result, remaining risks.

A task is not complete merely because the code compiles.

## 6. Verification by Risk Tier

Process exists to reduce total workload, not to add ceremony. Verification effort scales with the risk a change carries: pick the tier before review starts, and pick the lowest tier that honestly covers the risk.

### 6.1 Tiers

| Tier | Applies to | Required verification |
|---|---|---|
| **High risk** | Core runtime, memory lifetime, thread safety, rendering backend, platform interfaces, public API | Independent sub-agent audit (6.2–6.6) |
| **Normal** | Ordinary functional changes | Independent review of the whole task, grounded in tests and code evidence (6.7) |
| **Low risk** | Formatting, documentation fixes, local naming | Regular checks: self-review plus executed tests (6.8) |

Tier is judged by what a change *could break*, not by its size: a small diff inside the arena, channel, invalidation scheduler, renderer, platform layer, or a public header is high risk; a large but isolated example or test addition is not.

### 6.2 High-risk tier: principle

The implementer must not be the sole judge of high-risk work. An implementation agent accumulates assumptions and self-justifying reasoning (context contamination) — the same thinking that produced a defect may prevent recognizing it. Therefore a **separate audit sub-agent** with a fresh context audits every high-risk change; it must not inherit the implementer's private reasoning or narrative, and must derive findings from the requirements and repository evidence itself.

If sub-agent delegation is unavailable on a high-risk task, report the task as **AUDIT BLOCKED**. Self-review may supplement, but never substitutes for the audit.

### 6.3 Audit input

Give the auditor: the original task, relevant architectural constraints, the diff / changed-file list, relevant interfaces and call sites, actual test commands and results, and any owner-approved design decisions. Do not feed it a long implementation narrative as its primary source of truth.

### 6.4 Audit scope

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

### 6.5 Audit report format

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

### 6.6 Remediation and completion

The implementer classifies each finding as accepted / rejected / owner-decision; accepted findings are corrected, tests re-run, important corrections independently re-verified. Rejections need evidence-backed technical explanation. Disagreements touching public API, architecture, ownership, or threading escalate to the owner.

A high-risk task is **DONE** only when: implementation complete, tests executed, audit completed, no unresolved CRITICAL/MAJOR findings, corrections verified, remaining warnings documented.

Do not pressure the auditor toward a positive result or weaken findings for a green status. A useful audit may conclude the implementation is wrong or should be redesigned — that is the point.

### 6.7 Normal tier: independent task review

A fresh-context reviewer (sub-agent when available) examines the **whole** feature task, not fragments: requirement, full diff, call sites, and the executed tests. The review is grounded in code and test evidence; a short note suffices — what was checked, verdict, findings — no fixed template. Findings follow the same remediation path as 6.6.

### 6.8 Low tier: regular checks

Self-review plus executed tests/builds. No sub-agent, no report format. Honest records still apply (Section 12).

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

**Allowed:** reading/analyzing code, inspecting build config, adding internal helpers, adding/updating tests, fixing clearly identified bugs, improving docs within scope, running builds and tests, delegating independent audits and reviews.

**Approval required:** changing public interfaces, adding dependencies, reorganizing modules, changing core architecture, destructive Git operations, creating commits, pushing, merging, removing features.

**Never fabricate progress:** never claim a build/test/audit/platform support/performance result without actually producing it. Report limitations honestly.

## 13. Task States

```
TODO → IN_PROGRESS → REVIEW → Verification by tier (Section 6)
                                  ├─ High risk      → Independent Audit   → Findings → IN_PROGRESS · Passed → DONE
                                  ├─ Normal         → Independent Review  → Findings → IN_PROGRESS · Passed → DONE
                                  ├─ Low risk       → Regular Checks      → DONE
                                  └─ Owner Decision needed → BLOCKED
```

States: `TODO` · `IN_PROGRESS` · `BLOCKED` · `REVIEW` · `AUDIT_FAILED` · `DONE`.
Pick the verification tier up front; do not inflate a low-risk change into ceremony, and do not downgrade a high-risk one.

## 14. Pull Requests

Every substantial PR includes: summary, motivation, architectural impact, API changes, build/test results, platform verification, performance impact (if applicable), known limitations. PRs containing high-risk changes additionally include the independent audit report (Section 6.5); normal-tier PRs include the review note (Section 6.7).

A PR must not claim completion while blocking audit findings remain unresolved.

## 15. Definition of Done

- [ ] Requirements understood.
- [ ] Implementation completed; existing architecture preserved.
- [ ] Public API changes and dependencies approved.
- [ ] Relevant tests added or updated and executed.
- [ ] No known critical defects remain.
- [ ] Verification completed per risk tier (Section 6); high-risk findings resolved and verified.
- [ ] Documentation updated; remaining risks reported.
- [ ] Owner informed of the result.

## 16. Final Engineering Principles

1. Preserve the architecture before optimizing implementation details.
2. Prefer explicit behavior over hidden mechanisms.
3. Prefer evidence over assumptions.
4. Prefer simple interfaces over clever abstractions.
5. Ask the owner before making architectural decisions.
6. Never fabricate test results or implementation progress.
7. Never treat self-review alone as verification for high-risk or normal-tier changes.
8. Match verification effort to risk (Section 6); the independent audit is reserved for high-risk changes.
9. Treat audit findings as engineering evidence, not personal criticism.
10. Never mark a high-risk implementation complete without its audit.
11. Every rule in this file must reduce total workload; a rule that adds ceremony without reducing risk should be proposed for removal.

---

The implementation agent builds the change. The independent audit agent challenges it. The project owner holds final architectural authority.
