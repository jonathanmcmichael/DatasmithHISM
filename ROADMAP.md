# DatasmithHISM roadmap

Accepted scope, priorities, gates and risks. Current build, test and live-check status is in the [handoff](HANDOFF.md#current-status). Required behavior is in the [import contract](IMPORT_PANEL_VALIDATION.md); decision rationale is in the [ADRs](Docs/ADR/README.md). The dated history is in the [journal](Docs/History/JOURNAL.md).

## Core goal

Import Revit Datasmith exports into the **fastest, most performant Unreal scene**, with **materials matched** to curated Unreal targets, without losing data or weakening the safety contract.

Everything below is ordered by that goal. Work that does not serve speed, material matching, or no-data-loss safety is deferred.

## Core vs deferred

| Core (in scope now) | Deferred (needs a scope decision first) |
|---|---|
| Measured import and runtime performance | Phase 6 extensions (relinking, cleanup, cross-asset dedupe, visualization, material pack) |
| Nanite and instancing policy that cannot hit engine limits | New persistence/recovery stages beyond the proven ones |
| Curated material matching | Tracked lifecycle for legacy tools |
| The existing safety contract and its real-dispatch tests | Named-copy ownership migration |
| Fidelity of what is imported (alignment, identity) | Automatic cleanup of retained interrupted sessions |

## Release decisions

- Support Unreal Editor import and already imported output in packaged Windows applications.
- Keep tracked import primary and preserve separate guarantees for legacy selection conversion.
- Preserve source geometry, alignment, identity, hierarchy, and meaningful scene-component roots.
- Warn about tracked manual changes and require explicit replacement authorization. Keep material approvals and mesh exceptions as persistent settings.
- Preserve failed-verification acceptance as an explicit whole-session decision that quarantines degraded output.
- Keep unknown ownership/future schemas blocked, stock reimport refused, and automatic cleanup deferred.

## Priorities

| # | Workstream | Status | Done when |
|---|---|---|---|
| 1 | **Benchmark harness** | Mechanism done 2026-10-01: commandlet `-MetricsFile` / `-Budget` (import-side metrics) with automation coverage. No baselines yet. Status in the [handoff](HANDOFF.md#current-status). | Three representative Revit exports (ARCH, HVAC, one large unique-mesh model) have recorded baselines and budgets; a regression fails the headless run. Rendered metrics (draw calls, frame time on a fixed camera path) are a separate follow-up and are not implied by import-side budgets. |
| 2 | **Budget-aware Nanite policy** | Partly done: mesh-count budget by placements (Amendment 15) and Nanite as a separate Apply step on a committed import (Amendment 16, 2026-10-02; service, commandlet and automation; imports no longer apply Nanite by default). Not yet run on a large source. The panel's Apply Nanite control and a read-only Nanite analysis with a recommended selection (Amendment 17) are built and automation-covered, not yet seen live. First real source run 2026-10-02 ([record](Docs/Validation/2026-10-02-cwm-source.md)): 2,970 meshes, Nanite apply 5.4 s for all and 0.94 s for the 98 recommended; the budget did not bind. Open: tune the analysis thresholds against rendered results, say what Nanite actually costs or saves per mesh at runtime, and try a source with tens of thousands of unique meshes to prove the budget. The double build itself cannot be removed (Datasmith constraint); the step isolates it instead. | Default policy stays under the pool ceiling on the large source by capping or skipping low-value meshes; avoidable double mesh builds are removed or shown to be unavoidable; scenes with no instancing opportunity have an explicit documented policy. Any option that changes output enters `ComputePlanId`. |
| 3 | **Material matching** | Review/approve/revoke machinery and 245 observed entries exist. No curated coverage. | The most-used materials (ranked by surface area and instance count, first 20-30) map to parameter-driven Unreal master materials with texture scale and orientation, reviewed and approved. Observed entries are not stock coverage. |
| 4 | **Fidelity on real models** | Fixtures pass; full-model alignment unproven while textures are missing. | Dimensions, elevations, reference points, nested links, mirrors and thin members agree on the structural/HVAC exports. |
| 5 | **Packaged and rendered acceptance** | Windows cook and runtime smoke pass. | Textures/UVs, lights, collision/navigation/LOD/culling and lookup work in a cooked build; camera-path CPU/GPU baselines recorded against the workstream 1 budgets. |
| 6 | **Release decision** | Open. | Contract scenarios and the [acceptance matrix](Docs/VALIDATION.md) have explicit results. |

Live editor checks (the ordered list in the handoff) run as a batched session when the user frees the desktop. Move checks that do not need a person into automation.

Source-data gaps block only the affected checks. Light calibration needs authoritative Revit photometrics, IES and reference exposure; the 1,033 Unitless HVAC lights must not be relabeled as a shortcut. Structural/HVAC fidelity needs the complete texture dependencies and shared reference-point information.

## Remediation batches (from the 2026-09-29 review)

| Batch | Status |
|---|---|
| A. Supersede and quarantine safety | Done: predecessor-removal failure and actor path reuse regressions pass. |
| B. Unattended legacy and Dataprep | Done: dialog-free wrappers and actual deletion counts with explicit partial-failure diagnostics; real-wrapper deletion-obstruction automation passes (`DataprepWrappersAreDialogFree`). |
| C. Cancellable dependency and material processing | Done. |
| D. Resolved-texture plan identity | Done. |
| E. Evidence and document reconciliation | Done 2026-10-01: [current-tree machine-readable evidence](Docs/Validation/2026-10-01-current-tree-evidence.json) records source hashes, the test list, commands, exits, and logs. |

## Phase status (implemented vs remaining)

| Phase | Implemented | Remaining gate |
|---|---|---|
| 1. Geometry fidelity | Source-derived joist and light/IES fixtures; version provenance; direct payload inspection; six ordinary/ISM/HISM and Nanite comparisons | Workstream 4. |
| 2. Integrity and recovery | Source accounting; tracked edits and replace/cancel; explicit rebuild; Analyze progress and cancellation; dependency preflight; preset/destination invalidation; save/reopen; save failure handling; named-map copy refusal; one actual interruption/restart; rollback batch delete (Amendment 12) | Other interruption stages and full-volume behavior stay deferred unless a scope decision pulls them in. |
| 3. Nanite, lighting, presets | Import-wide Nanite policy; converted ISM/HISM groups option (Amendment 10); exact mesh exceptions; per-mesh Nanite records and projected-count advisory (Amendments 13, 14); light/IES source verification; presets | Workstream 2; calibrated Revit photometrics; broader material compatibility. |
| 4. Materials | Linked DataTables with CSV; review/approve/revoke UI; exact appearance fingerprints; replacement before Nanite | Workstream 3. |
| 5. Inspection and release | Search, source identities, focus, rebuild previews, runtime lookup, Windows cook and runtime smoke, stage/step timing and peak memory | Workstreams 1 and 5. |
| 6. Later extensions | Not started | Deferred. |

### Legacy acceptance

Separately validate Dedupe confirmation/dry-run/external references, Explode staged failure/success/undo, BIM hierarchy/storey grouping, partial-selection reruns, and Nanite auto-detection. Giving legacy tools tracked lifecycle guarantees is a separate scope decision.

## Known risks and limits

- **Nanite budget:** applying Nanite to all meshes can exhaust UE 5.8.3's fatal root-page pool on large unique-mesh sources. Nanite is set after Datasmith's first build, so each changed mesh builds twice. Checked against the engine source 2026-10-01: Datasmith has no Nanite handling, and creates, finalizes and builds meshes inside one stock factory call (`FinalizeAssets` then `BatchBuild`), so no supported pre-build hook exists. `UStaticMesh::OnPreMeshBuild()` fires before each build but is a per-instance delegate and the meshes are not reachable before the build. The only seams found are unsupported: temporarily setting the `UStaticMesh` class-default Nanite flag (global, and it would also enable Nanite on meshes the budget or exceptions exclude, causing rebuilds to turn it off), or an object-creation listener. **The cost of the extra build is unmeasured**; the first build has no Nanite data, so it may be small next to the Nanite rebuild. Measure it on a heavy synthetic source before adopting either workaround.
- **Source fidelity:** full-model alignment is unproven while textures are missing.
- **Photometrics:** source preservation is tested; physical equivalence with Revit is uncalibrated.
- **Persistence/recovery:** named-map copies are refused rather than migrated. Journals diagnose known state; they do not resume or delete output.
- **Runtime/rendering:** cooked lookup, references and a collision trace pass. Visual quality, texture/UV breadth, navigation/LOD/culling and performance are unproven.
- **Maintenance:** the import service is about 4,000 lines. Keep lifecycle decisions centralized and test real dispatch before refactoring.
- **Legacy boundary:** descriptor grouping and origin checks do not prove separate assets match in every collision/LOD/rendering attribute. Legacy tools have no session rollback.

## Agent rules

- The lifecycle owner decides commit/supersede/rollback ordering; helpers must not duplicate those decisions.
- A new destructive-safety or ownership defect blocks its batch from being called complete; record the reproducer.
- Do not begin Phase 6, clean retained interrupted sessions, change manifest schema, or extend legacy tools into tracked lifecycle behavior without a scope decision.
- Hand off one bounded change at a time with changed files, tests run, failures, artifacts and limits. Do not mix cosmetic cleanup with safety fixes.
