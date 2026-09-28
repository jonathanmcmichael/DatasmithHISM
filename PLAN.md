# DatasmithHISM accepted development plan

Accepted scope: the consolidated six-phase Revit-to-Unreal plan. Updated 2026-09-26. **Phases 1-5 and release acceptance remain open**, even where implementation is present. The [execution ledger](ROADMAP_EXECUTION.md) records evidence; this document defines scope and sequencing. The former five-phase legacy backlog is preserved in [history](Docs/History/2026-09-26/PLAN.md).

Execution checkpoint, 2026-09-27: the bounded persistence/recovery work has passing evidence, and the next independent assignment is live UI acceptance in [HANDOFF.md](HANDOFF.md). Existing small fixtures suffice for those interactions; corrected structural data remains required for its own geometry gate. This does not change the accepted scope or close an entire phase.

## Release decisions

- Support Unreal Editor import and already imported output in packaged Windows applications.
- Keep tracked import primary and preserve separate guarantees for legacy selection conversion.
- Preserve source geometry, alignment, identity, hierarchy, and meaningful scene-component roots.
- Warn about tracked manual changes and require explicit replacement authorization. Keep material approvals and mesh exceptions as persistent settings.
- Preserve failed-verification acceptance as an explicit whole-session decision that quarantines degraded output.
- Keep unknown ownership/future schemas blocked, stock reimport refused, and automatic cleanup deferred.

## Phases, ownership, and gates

Ownership below identifies the responsible work area, not a request to run parallel agents. Complete dependencies and record evidence before closing a gate.

| Phase | Work and owner | Dependencies | Acceptance gate |
|---|---|---|---|
| 1. Geometry fidelity and fixtures | Source/geometry owner: isolate joists and thin members; compare ordinary/ISM/HISM with independent Nanite assets; inspect payload, sections, normals, bounds, visibility, and rendering. Record versions, known dimensions, elevations, nested links, mirrored placement, and distant coordinates across structural/HVAC sources. | Corrected joist export and complete sidecars for positive acceptance | Structural details and source alignment survive supported paths; retain negative and corrected regression fixtures. |
| 2. Integrity, progress, recovery | Service/UI owner: whole-mesh accounting; unchanged-output drift; replace/cancel manual edits; explicit rebuild; Analyze stages and cancellation; dependency/translator/destination/write diagnostics; separate verification/save/degraded states; attempt checkpoints; independent legacy offset/settings coverage. | Phase 1 fixtures; existing ownership/rollback contract | Failure, cancellation, edits, saves, and restart recovery never silently lose user work or claim partial success. |
| 3. Nanite, lighting, presets | Processing owner: independent Nanite policies for all owned meshes including zero groups; compatibility/compilation; persistent mesh exceptions; enabled-light inventory; advisory threshold 100; MegaLights navigation; calibrated units/intensity/falloff/IES; named and session presets. | Phase 2 ownership and source accounting; calibrated light references | Policy is independent of grouping; lights match references or remain explicitly unresolved; presets reproduce output choices. |
| 4. Catalog and reviewed materials | Materials owner: linked appearance/mapping DataTables with CSV; provenance and aliases; separate catalog/replacement coverage; evidence-based candidates; exact variant approvals; review UI; apply to defaults/overrides before Nanite; preserve original assignments and external target ownership. | Phases 2-3 execution order; reviewable appearance evidence | Stock/custom/ambiguous/unmapped/changed variants behave predictably; slots and approved choices survive rebuild and save/reopen. |
| 5. Inspection and release | UI/runtime/validation owner: searchable source results and focus; group/material/light/edit previews; distinct object counts; runtime identity; Windows cook; retained-version sizes; import/build/memory and camera-path CPU/GPU baselines. | Earlier gates plus representative scene/camera references | Traceability, safe rebuild/save, rendered fidelity, and packaged operation have separate passing evidence. |
| 6. Later extensions | Future feature owner: source relinking with identity preview; optional Unreal material pack; reference-aware cleanup; broader deduplication; richer instance visualization/extraction. | Phases 1-5 accepted; feature-specific design/rights review | Separate deliverables; not required to claim this release's earlier gates. |

## Shared requirements

PlanId includes normalized output-changing settings, exact source exceptions, and approved mapping revisions. Warning thresholds and observed renderer settings stay advisory. Sidecar changes remain warning-only until explicit rebuild.

New manifests use schema 2, tracked-state version 1, and source-inventory version 1. Older records cannot claim checks they never stored. Preserve the previous active session until the verified replacement reaches the safe replacement boundary; never infer ownership from a folder name alone.

The intended execution order is load/analyze, ownership/conflict preflight, import, approved materials, mesh/light policy processing, verification, then commit. Focused helpers implement processing/review/persistence; lifecycle decisions remain in the service.

Sources stay read-only. Uninterruptible translator/build calls honor cancellation at their next safe return. Automated runs show no dialogs. Diagnostics must distinguish geometry, mesh build, light, material, persistence, and recovery outcomes. Saving or opening a project must not silently approve edits, upgrade manifests, or rebuild an import.

## Release evidence

Use the [validation matrix](Docs/VALIDATION.md) for required scenarios. A real editor build and automation run are necessary but do not establish full rendered or packaged acceptance. Preserve failing fixtures and prove that critical guards detect deliberately introduced faults. Log known limitations and data blockers in [NEXT_STEPS.md](NEXT_STEPS.md).
