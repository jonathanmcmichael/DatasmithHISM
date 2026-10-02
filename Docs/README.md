# Documentation index

Current build, test and live-check status is kept only in [HANDOFF.md](../HANDOFF.md#current-status). Import orchestration targets UE 5.8.3 Editor; runtime output targets packaged Windows applications.

## Using the plugin

| Document | Purpose |
|---|---|
| [Plugin README](../README.md) | Quick start, capabilities, and current limitations |
| [Import workflow](IMPORT_WORKFLOW.md) | Analyze, import, rebuild, save, presets, inspection, and commandlet switches |
| [Material catalog and matching](MATERIALS.md) | DataTable schemas, CSV, review, approval, and invalidation |
| [Legacy selection tools](LEGACY_TOOLS.md) | Managed/Batch ISMs, Dedupe, Explode, Dataprep, and editor Blueprint API |
| [Runtime and packaging](RUNTIME_AND_PACKAGING.md) | Cookable metadata, lookup, Windows build, smoke checks, and limitations |

## Engineering and release

| Document | Authority |
|---|---|
| [Import contract](../IMPORT_PANEL_VALIDATION.md) | Required behavior; numbered amendments override earlier wording in their scope |
| [Architecture](ARCHITECTURE.md) | Current module/helper boundaries and execution sequence |
| [ADRs](ADR/README.md) | Accepted decision rationale, alternatives, consequences, and evidence |
| [Roadmap](../ROADMAP.md) | Accepted scope, phase gates, remaining work in order, known risks |
| [Validation procedure](VALIDATION.md) | Repeatable build/test/package commands and acceptance matrix |
| [Import profiling and Win64 export, 2026-10-01](Validation/2026-10-01-import-profiling.md) | Nanite crash boundary, per-step diagnostics, Nanite advisory, earlier 45-test runs, and packaged plugin |
| [CWM source, 2026-10-02](Validation/2026-10-02-cwm-source.md) | First real large-source runs: the unimportable-IES finding (Amendment 18), import and Nanite apply timings, and the Nanite analysis of 2,970 meshes |
| [Opt-in trace capture, 2026-10-01](Validation/2026-10-01-trace-capture.md) | One-shot full-file import tracing, repeat capture, and current 47-test suite evidence |
| [Current-tree machine evidence, 2026-10-01](Validation/2026-10-01-current-tree-evidence.json) | Source SHA-256 hashes, build and automation commands/results, and all 47 suite test paths |
| [Packaging, 2026-09-29](Validation/2026-09-29-packaging.md) | Win64 cook and runtime smoke check for the 36-test tree |
| [Earlier evidence, 2026-09-26](Validation/2026-09-26.md) | Geometry/light/package results and earlier 24-test baseline |
| [Earlier machine-readable evidence](Validation/2026-09-26-evidence.json) | Original implementation snapshot; superseded for current source hashes and test count |
| [Phase 2 follow-up evidence](Validation/2026-09-26-phase2.md) | Named-copy refusal, partial saves, manual drift, actual interruption/restart, 27 passing tests |
| [Phase 2 source hashes/results](Validation/2026-09-26-phase2-evidence.json) | Earlier build/test and persistence evidence |
| [Verification evidence, 2026-09-28](Validation/2026-09-28-verification.md) | Build, 38 tests, Amendments 9-11, five new regression tests |
| [Earlier source hashes](Validation/2026-09-28-source-sha256.json) | 55 source files from the 2026-09-28 build |
| [Live UI evidence](Validation/2026-09-27-live-ui.md) | Earlier build, 32 tests, native interaction results and two fixes |
| [Live UI source hashes](Validation/2026-09-27-live-ui-evidence.json) | 55 source hashes from that build |
| [Preset/cancellation evidence](Validation/2026-09-27-preset-cancellation.md) | Earlier build, 30 tests, preset/cancellation fixes |
| [Preset/cancellation source hashes](Validation/2026-09-27-preset-cancellation-evidence.json) | Earlier 55-file snapshot |
| [Pending live UI checklist](Validation/2026-09-26-phase2-ui.md) | Required progress/cancellation and inspection interactions with explicit pending status |
| [Live UI preflight](Validation/2026-09-27-live-ui-preflight.md) | Unchanged source snapshot, Windows capture/activation, desktop availability dependency; no checklist pass |
| [Conversation closeout, 2026-09-27](Validation/2026-09-27-closeout.md) | Final state, retained artifacts, next task and documentation-only verification |
| [Closeout verification](Validation/2026-09-27-docs.json) | Local link/anchor checks, source-hash and existing-log verification; no new Unreal execution |
| [Fixture index](../Tests/Fixtures/README.md) | Provenance and limits of extracted/generated fixtures |
| [Handoff](../HANDOFF.md) | Next live UI assignment, completed persistence/recovery evidence, commands and inputs |
| [Engine/source notes](ENGINE_NOTES.md) | External guidance separated from project decisions |
| [Journal](History/JOURNAL.md) | Chronological history; older status claims are not current gates |

The original panel, rendering and baseline plans are archived in [History](History/README.md). Their current content lives in the [workflow](IMPORT_WORKFLOW.md), [architecture](ARCHITECTURE.md) and [validation](VALIDATION.md) documents.

## Maintaining these documents

Use source and executed evidence to establish current behavior. Keep implementation, automation, editor UI, rendered-scene, cooked-runtime, and performance status separate. A passing test is not a completed release gate unless it covers that gate's required data and behavior.

Change the contract when a guarantee changes, add or supersede an ADR for a consequential decision, update the execution ledger, and record new evidence without overwriting earlier runs. ADR acceptance records a decision, not successful release acceptance.

[Historical snapshots](History/README.md) preserve earlier plans and reviews; dated evidence preserves the results of its own run. Current handoff/roadmap guidance supersedes their old backlog claims. The earlier 2026-09-27 closeout changed documentation only and verified the 54-file Phase 2 snapshot. The later preset/cancellation implementation has its own 55-file snapshot and executed build/test evidence. Build, automation, package and interaction evidence retain their original execution dates.
