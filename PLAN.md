# DatasmithHISM accepted development plan

Accepted scope: the consolidated six-phase Revit-to-Unreal plan. Updated 2026-09-29 after the repository-wide safety, legacy/runtime, and evidence review. **Phases 1-5 and release acceptance remain open**, even where implementation is present. The [execution ledger](ROADMAP_EXECUTION.md) records evidence; this document defines scope and sequencing. The former five-phase legacy backlog is preserved in [history](Docs/History/2026-09-26/PLAN.md).

Execution checkpoint, 2026-09-29: the bounded persistence/recovery work retains its dated evidence, but a repository review found unresolved destructive-safety, unattended Dataprep, cancellation, identity, and evidence-portability gaps. The remediation program below is the immediate queue. Live UI re-checks follow its safety-critical batches. Corrected structural data remains required only for its own geometry gate. This does not change the accepted release scope or close an entire phase.

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

## Immediate remediation program

This program converts the 2026-09-29 review into bounded assignments for subsequent agents. Execute the batches in order unless an assignment explicitly says it may run in parallel. An agent owns only its named paths and tests; preserve unrelated work. Before editing, read `AGENTS.md`, `HANDOFF.md`, `NEXT_STEPS.md`, `ROADMAP_EXECUTION.md`, this plan, and the applicable contract/ADR. Do not run Unreal builds while the editor is open and do not kill the user's editor.

Each implementation batch is complete only when its regression first demonstrates the fault or otherwise proves the guard through the real entry path, the fix passes the targeted test and full `DatasmithHISM` suite, and the contract, ADR when consequential, ledger, handoff, and dated evidence agree. Automation does not substitute for live UI, packaged, rendered, or performance evidence.

### Batch A - supersede and quarantine safety

**Owner:** tracked-import lifecycle agent. **Priority:** blocking; complete before any live destructive acceptance.

1. Repair `AcceptFailedVerification` so a failed removal of the previous active session cannot return `AcceptedWithFailedVerification`, expose two active manifests, or leave duplicate active geometry. Preserve the previous active session unless the replacement reaches the contractual safe boundary.
2. Add the missing S25 regression for failure to remove the old session during failed-verification acceptance. Exercise a reimport with a real `PreviousManifest`; the existing first-import acceptance test is insufficient.
3. Harden supersede actor resolution. A path fallback must prove ownership with the recorded session tag or fail closed; `bReplaceManualEdits` must not authorize deletion of an unrelated actor that reused a recorded path.
4. Add an adversarial path-reuse/session-tag regression through the real supersede preflight.

**Primary paths:** `ConVerseDatasmithImportService.cpp`, `ConVerseOptimizedImportAutomation.cpp`, `IMPORT_PANEL_VALIDATION.md`, and ADR 0001 or 0006 as appropriate.

**Gate:** injected old-session removal failure and path reuse both preserve unrelated/previous output, produce a non-success status with actionable diagnostics, and leave ownership unambiguous after restart.

### Batch B - unattended legacy and Dataprep correctness

**Owner:** legacy/Dataprep agent. **Priority:** blocking for unattended acceptance. May begin after Batch A tests are defined, but must not overlap edits to shared legacy files with another agent.

1. Make progress-dialog presentation caller-controlled. Interactive library entry points may present cancellable UI; Dataprep and headless entry points must not call `MakeDialog`.
2. Add coverage through both actual Dataprep operation wrappers proving dialog-free execution rather than testing only `BuildManagedHISMs` directly.
3. Correct `UConVerseCreateHISMOperation` deletion reporting to use the successful deletion count and retain explicit partial-failure diagnostics.
4. Add deletion-failure reporting coverage. Preserve transaction ownership at the library boundary and existing Blueprint compatibility, including the deprecated two-parameter alias.

**Primary paths:** `ConVerseHISMUtils.cpp`, both HISM Dataprep operation implementations, `ConVerseHISMLegacyAutomation.cpp`, `Docs/LEGACY_TOOLS.md`, and ADR 0009.

**Gate:** Dataprep runs without interactive dialogs, partial deletion is never reported as complete, and all existing placement/settings/compatibility tests continue to pass.

### Batch C - cancellable dependency and material processing

**Owner:** processing/progress agent. **Dependency:** Batch A, because this work crosses the post-mutation failure path.

**Implementation checkpoint, 2026-09-29:** implemented in the shared working tree with targeted
recursive-search and post-import hashing regressions. The implementation is **unbuilt and
unverified** because an Unreal Editor process was open; the batch gate remains open pending a safe
build plus focused and full automation.

1. Carry `FConVerseImportProgress` and cancellation through import-time material appearance fingerprinting. Avoid the blocking whole-file hashing branch after mutation.
2. Make recursive texture-library discovery bounded, observable, and cooperatively cancellable. Preserve deterministic first-folder and sorted-path resolution.
3. Define and test the exact cancellation outcome before and after the mutation boundary. Incomplete hashes or search results must not be retained or reported as successful analysis.
4. If pre-indexing or caching is introduced, specify invalidation, lifetime, ordering, and memory bounds before implementation; do not add a global mutable cache implicitly.

**Primary paths:** `ConVerseImportProcessing.{h,cpp}`, `ConVerseImportProgress.h`, service call sites, panel status handling, and optimized-import automation.

**Gate:** cancellation tests cover search discovery and post-import material hashing with large synthetic inputs; rollback/status semantics remain contract-correct and the UI never claims partial success.

### Batch D - resolved-texture plan identity

**Owner:** identity/material agent. **Dependency:** Batch C's chosen hashing interface.

1. Include the resolved texture's content identity, not only its element/path diagnostic string, in `PlanId` whenever external search resolution changes imported output.
2. Preserve the Amendment 8 rule that the configured search-folder list itself does not alter identity when it resolves nothing.
3. Add regressions for an in-place content change at the same resolved path, unchanged content at the same or equivalent resolution, no-match folder changes, and deterministic folder precedence.
4. Decide whether the clarification changes a guarantee. If so, revise Amendment 8 and the identity ADR in the same change; do not change manifest schema unless stored verification coverage actually changes.

**Gate:** changed resolved bytes cannot return `AlreadyCurrent`; irrelevant folder-list changes retain the previous plan identity; sources and library files remain read-only.

### Batch E - evidence and execution-document reconciliation

**Owner:** documentation/evidence agent. **Dependency:** Batches A-D final source state. This agent must not claim execution it did not perform.

1. Reconcile the current baseline: distinguish the historical 30-test and 32-test runs from the expected/current 33-test suite in `AGENTS.md`, `HANDOFF.md`, `README.md`, `NEXT_STEPS.md`, `ROADMAP_EXECUTION.md`, `Docs/README.md`, and `Docs/VALIDATION.md`.
2. Replace stale `D:/Unreal/Sandbox/AdvancedHISM` operational commands with the current project path or parameterized examples. Preserve historical paths inside dated evidence as historical facts.
3. Correct current claims that the clean committed checkout is uncommitted. Record the reviewed commit and working-tree state in the new dated evidence.
4. Restore the required project-level `../../AGENTS.md` or remove/fix the mandatory reference at its owning scope. Do not invent project-level policy inside plugin history.
5. Check in a dated machine-readable evidence record for the post-remediation source hashes, exact test list/count, commands, exits, and log locations. If raw external logs are unavailable, say so explicitly.
6. Reorder `NEXT_STEPS.md` so executable safety/UI work precedes the explicitly deprioritized, source-author-blocked joist correction.

**Gate:** one unambiguous current baseline, runnable commands for this checkout, no false workspace-state claims, valid governance links, and portable evidence sufficient to distinguish source verification from executed Unreal results.

### Batch F - live acceptance continuation

**Owner:** live UI/validation agent. **Dependencies:** successful build and full automation after Batches A-D; Batch E may finalize alongside evidence capture. Use disposable maps and the guarded native interaction helper. Do not use the saved reference map as a scratch target.

Execute in this order:

1. Re-check untitled-level first-save rebasing and restored-source visibility in the live panel.
2. Exercise the missing-texture Yes/No prompt live, including refusal, exact acceptance, source-change clearing, and the separate missing-mesh refusal.
3. Decide and document the synchronous translator-boundary feedback limitation or implement pre-translation messaging; do not claim that a cancellation state paints while Slate is blocked.
4. Complete copied-fixture material/light rebuild previews, reviewed-target approval/revocation, and native named-map copy refusal.
5. Broaden Content Browser rename/move and additional interruption checkpoints.
6. Leave representative complete-model, rendered, packaged breadth, and performance gates pending when their required inputs are unavailable.

**Gate:** every attempted checklist row has a reproducible result and artifact; demonstrated defects receive built/tested fixes; blocked rows name the missing input without being converted into passes.

## Agent coordination rules

- The lifecycle agent owns commit/supersede/rollback ordering. Processing, UI, and test agents must not duplicate those decisions in helpers.
- Batches A and B may be developed independently because their primary implementation files do not overlap. Batches C and D are sequential. Batch E follows the final source state. Batch F follows a green build and suite.
- An agent finding a new destructive-safety or ownership defect stops its batch from being called complete, records the reproducer, and routes the decision back to the lifecycle owner.
- Do not opportunistically begin Phase 6, clean retained interrupted sessions, change manifest schema, or broaden legacy tools into tracked lifecycle behavior.
- Commit or hand off one bounded batch at a time with exact changed files, tests run, failures, artifacts, and remaining limitations. Preserve user changes and avoid mixing cosmetic cleanup with safety fixes.

## Shared requirements

PlanId includes normalized output-changing settings, exact source exceptions, and approved mapping revisions. Warning thresholds and observed renderer settings stay advisory. Sidecar changes remain warning-only until explicit rebuild.

New manifests use schema 2, tracked-state version 1, and source-inventory version 1. Older records cannot claim checks they never stored. Preserve the previous active session until the verified replacement reaches the safe replacement boundary; never infer ownership from a folder name alone.

The intended execution order is load/analyze, ownership/conflict preflight, import, approved materials, mesh/light policy processing, verification, then commit. Focused helpers implement processing/review/persistence; lifecycle decisions remain in the service.

Sources stay read-only. Uninterruptible translator/build calls honor cancellation at their next safe return. Automated runs show no dialogs. Diagnostics must distinguish geometry, mesh build, light, material, persistence, and recovery outcomes. Saving or opening a project must not silently approve edits, upgrade manifests, or rebuild an import.

## Release evidence

Use the [validation matrix](Docs/VALIDATION.md) for required scenarios. A real editor build and automation run are necessary but do not establish full rendered or packaged acceptance. Preserve failing fixtures and prove that critical guards detect deliberately introduced faults. Log known limitations and data blockers in [NEXT_STEPS.md](NEXT_STEPS.md).
