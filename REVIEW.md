# Current engineering review

Source/evidence reconciliation, 2026-09-27. The detailed earlier defect report is [archived](Docs/History/2026-09-26/REVIEW.md). The latest [live UI evidence](Docs/Validation/2026-09-27-live-ui.md) records two defects found by native interaction, their fixes, the passing build, 32 tests and the current 55-file source snapshot. Earlier persistence evidence retains its date and scope. Release acceptance remains incomplete.

## Resolved findings

| Finding | Current resolution / evidence |
|---|---|
| Preset/file-picker changes retained previous inspection and save state | Shared input application compares paths before assignment, clears old associations and disables/closes stale review windows; panel callback regression passes. |
| Later analysis loops lacked cancellation, and import sidecar cancellation reported load failure | Shared cooperative checkpoints and streamed texture fingerprints; 17 phase/entry-point cancellation cases preserve inventories and source bytes. Native interaction remains unverified. |
| Material slot IDs lost after Datasmith sanitization/renaming | Recover numeric IDs from mesh-description polygon groups with guarded fallbacks; multi-slot verification passes and prior user real-data confirmation is recorded in the journal. |
| Failed-verification acceptance falsely recorded Passed | Commit derives actual result and persists degraded/quarantined state; accept/discard tests pass. |
| Rollback had never executed | Injected failures and obstructed rollback are exercised; cleanup and RollbackFailed cases pass. |
| Automation supposedly blocked by optional SDKs | Disproved; current full suite passes 32/32 with exit 0. |
| Named-map Save As falsely reported success for an unverifiable copy | Real commandlet refusal before import/copy; native copy's changed actor GUID fails explicit-save ownership proof; original remains verifiable. |
| Recovery diagnostics omitted recorded object paths | Recorded destination/report/object paths are displayed as unproven observations; actual interrupted-process restart preserves saved files. |
| Legacy overrides were omitted | Effective source materials copied and tested. |
| Legacy instance failure could delete its source | Failed insertion retains the affected actor; no empty output component is retained. |
| Behavior payload and below-threshold cleanup gaps | Conservative eligibility and cleanup have regression coverage. |
| Component offset / shared-setting grouping | Component world transforms and descriptor-based grouping/copying tested. |
| Nanite omitted ordinary/zero-group meshes | Owned-mesh policy independent of instancing; zero-group coverage passes. |
| Manual edits could be silently replaced | Tracked comparison and explicit replacement authorization; real entry-path coverage passes. |
| Generated material expression IDs caused false drift after reopen | Canonical comparison ignores expression IDs but detects value changes; save/reopen and regression pass. |
| Multiple active owners could make replacement ambiguous | Real import dispatch blocks ambiguous ownership before mutation. |

## Remaining risks and limits

- **Source fidelity:** the negative joist fixture lacks webbing before import. Full model alignment is not established while referenced textures are missing.
- **Photometrics:** source preservation is tested; physical equivalence with Revit remains uncalibrated for the Unitless source data.
- **Materials:** exact approvals and infrastructure exist; stock catalog curation, physical scale and rendered equivalence do not follow from the observed inventory.
- **Persistence/recovery:** named-map copies are safely refused rather than migrated. Real read-only map/asset failure, bounded write-capacity simulation, saving drift and actual pre-commit interruption/restart are exercised. Live UI, Content Browser rename/move, other crash windows and actual full-volume behavior remain open. Journals diagnose known state rather than automatically resuming or deleting output.
- **Runtime/rendering:** cooked lookup, mesh/material/light/IES references and a collision trace pass. This does not establish visual quality, normal texture/UV breadth, navigation/LOD/culling or representative performance.
- **Maintenance:** helper extraction reduces service responsibilities, but lifecycle code remains substantial. Preserve centralized decisions and test real dispatch before further refactoring.
- **Legacy boundary:** descriptor grouping and origin checks are improvements, not complete proof that separate assets match in every collision/LOD/rendering attribute. Legacy tools retain their independent acceptance work and lack session rollback.

Avoid unstable line-count tables and old source line numbers as correctness evidence. Use the [architecture map](Docs/ARCHITECTURE.md), [ADRs](Docs/ADR/README.md), and [remaining work](NEXT_STEPS.md) to guide changes.
