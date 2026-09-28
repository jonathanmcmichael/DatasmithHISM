# DatasmithHISM agent guidance

Updated 2026-09-27 after preset/cancellation implementation and automation. Read the project-level [AGENTS.md](../../AGENTS.md), [handoff](HANDOFF.md), [current next steps](NEXT_STEPS.md), and [execution ledger](ROADMAP_EXECUTION.md) before new work. This is a C++ UE **5.8.3** plugin, with editor and runtime modules; it is not a .NET application.

## Scope and authority

The primary tracked importer is governed by [IMPORT_PANEL_VALIDATION.md](IMPORT_PANEL_VALIDATION.md), including its numbered amendments. Read it before changing that path. Revise the contract in the same change when a guarantee changes. [ADRs](Docs/ADR/README.md) explain decisions; [PLAN.md](PLAN.md) defines accepted scope; historical plans are not an active queue.

| Path | Purpose |
|---|---|
| `Source/DatasmithHISM/Private/ConVerseDatasmithImportService.cpp` | Tracked ownership, planning, import, verification, replacement, rollback, and commit |
| `Source/DatasmithHISM/Private/ConVerseImportProcessing.cpp` | Mesh/light/material policies and tracked state |
| `Source/DatasmithHISM/Private/ConVerseImportPersistence.cpp` | Presets, explicit save, attempt recovery diagnostics |
| `Source/DatasmithHISM/Private/ConVerseMaterialReview.cpp` | Review and exact appearance approvals |
| `Source/DatasmithHISM/Private/ConVerseHISMUtils.cpp` | Separate legacy selection conversion |
| `Source/DatasmithHISMRuntime/` | Cookable identity/lookup; no editor dependencies |

## Invariants

- Resolve source support through enabled translators; do not add an extension allowlist.
- Sources stay read-only. Sidecar fingerprints warn and stay outside PlanId; normalized output settings, tessellation, exact exceptions, and active approved mappings belong in it.
- Apply translator tessellation before loading. Assign its fields individually so engine-owned defaults survive. Clamp in the service, preserving zero max-edge-length as unconstrained.
- Keep ownership/commit/supersede/rollback decisions centralized. Ambiguous ownership, future schemas, and quarantined sessions block replacement.
- Keep stock reimport refused by the handler registered at default factory priority plus 100. Its real `FReimportManager` dispatch test must stay real-dispatch coverage.
- Older manifests never implicitly acquire new verification coverage. New records use schema 2, tracked-state version 1 and source-inventory version 1.
- Modifying a warning threshold or opening/restoring settings must not rebuild. Explicit rebuild does not itself authorize discarding tracked edits.
- Apply approved material mappings before mesh compatibility checks. External project targets never become attempt-owned. Unitless lighting remains unresolved without authoritative calibration.
- Preserve attachment/transform roots and component world placement. Source element names and metadata carry identity; sanitized labels do not.
- Already-saved map Save As changes actor GUIDs and is refused before commandlet import/copy. Copied tags do not transfer ownership. Keep the native-copy service refusal and initial unnamed-map rebinding proof distinct.

## Legacy boundary

Legacy tools have no tracked manifest or session rollback. Preserve `ConVerseManagedHISM`, `ConVerseManagedFamilyType`, and existing Blueprint compatibility. `CreateISMsFromSelection` is canonical; the old `CreateHISMsFromSelection` alias remains deprecated.

Managed grouping now includes component descriptor settings and a mesh-origin discriminator; material overrides and component transforms are copied. Keep behavior-payload rejection, instance-add result checks, and below-threshold cleanup. Do not reintroduce the resolved gaps from historical reviews.

Transactions belong at the library boundary so Dataprep can own its transaction. Keep Dataprep/headless paths free of interactive dialogs. Dedupe confirmation precedes reference replacement, and external/uncertain references skip deletion. See [legacy tools](Docs/LEGACY_TOOLS.md).

## Build and evidence

Run the [real build and automation commands](Docs/VALIDATION.md) with the editor closed. Do not kill the user's editor. The current recorded baseline is 33 passing automation tests, including rollback, manual edits, material invalidation, light thresholds, legacy placement/settings, persistence/recovery, first naming of an untitled level, and panel session restore. The explicit process-interruption fixture runs separately and may terminate only its own specifically launched disposable process. Optional-platform SDK noise did not block automation. IntelliSense cannot reliably resolve this Unreal project and is not build evidence.

Preserve existing uncommitted work. The Git root is this plugin directory; the host project root is not a Git checkout. All execution logs are under the host project's `Saved`, not this plugin's `Saved`.

The latest executed build and 32-test suite are dated 2026-09-27; [current evidence](Docs/Validation/2026-09-27-live-ui.md) records live UI results and all 55 source hashes. The earlier closeout, preset/cancellation and persistence snapshots retain their dates. Native interaction works through the guarded `NativeUI.ps1` helper (see the handoff). The next task is a live re-check of the two automation-verified fixes, then the remaining checklist rows; keep pending status where interaction or inputs are unavailable. Do not repeat successful service tests as a substitute for UI evidence. Generated interrupted sessions are retained deliberately; review the handoff before cleanup.

When adding world-scanning tests, snapshot/diff pre-existing objects because the world persists between tests. Prove safety guards can detect faults through their real entry paths. Update handoff with exact evidence and flag unbuilt/unverified code. Builds and synthetic tests never establish rendered, full-model, or packaged acceptance by themselves.

## Style and documentation

Follow Unreal types/prefixes, tabs in C++, `TEXT()` and `LOCTEXT`, and the `LogConVerseOptimizedImport` category. Keep runtime dependencies limited to runtime modules. Prefer symbol links over fragile line counts in architecture docs.

Update the [documentation index](Docs/README.md), relevant ADR/contract, [ledger](ROADMAP_EXECUTION.md), and dated evidence when behavior changes. The [journal](JOURNAL.md) is chronological history; its old claims do not override newer evidence. Do not edit user memory as part of repository documentation work.
