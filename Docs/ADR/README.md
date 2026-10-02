# Architecture decision records

Recorded 2026-09-26 from the accepted consolidated plan and current implementation. These ADRs document existing decisions; **Accepted** means the design decision is accepted, not that release acceptance is complete. The [contract](../../IMPORT_PANEL_VALIDATION.md) remains authoritative for required behavior, and the [roadmap](../../ROADMAP.md) owns completion status.

Evidence sections were reconciled at the [2026-09-27 closeout](../Validation/2026-09-27-closeout.md): 27 tests passed in that suite, one actual interruption/restart checkpoint is covered, named-map copy ownership remains refused, and live UI is pending. The original decision dates are retained.

| ADR | Decision | Status |
|---|---|---|
| [0001](0001-tracked-import-lifecycle.md) | Tracked import owns lifecycle decisions | Accepted |
| [0002](0002-identity-and-manifest-evolution.md) | Output identity and explicit manifest evolution | Accepted |
| [0003](0003-geometry-and-nanite.md) | Preserve geometry and apply Nanite independently | Accepted |
| [0004](0004-lighting-provenance.md) | Preserve photometric evidence and keep renderer advice advisory | Accepted |
| [0005](0005-reviewed-material-matching.md) | Separate appearance recognition from replacement approval | Accepted |
| [0006](0006-verification-save-and-recovery.md) | Separate verification, persistence and recovery | Accepted |
| [0007](0007-editor-runtime-split.md) | Cook source identity without editor import services | Accepted |
| [0008](0008-evidence-and-release-gates.md) | Keep implementation evidence separate from release acceptance | Accepted |
| [0009](0009-legacy-selection-boundary.md) | Preserve a separate contract for legacy selection conversion | Accepted |

To change a decision, add a new ADR and mark the old record superseded with a link. Update the behavioral contract if a guarantee changes, and add evidence before marking a release gate complete. Source relinking, automatic cleanup, cross-asset deduplication, selective extraction and a material pack require later decisions; these records do not authorize them as current release work.

The subsequent [preset/cancellation follow-up](../Validation/2026-09-27-preset-cancellation.md) passes 30 tests and records the current 55-file source snapshot. It preserves these decisions and leaves native UI acceptance pending.
