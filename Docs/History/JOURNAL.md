# DatasmithHISM — Work Journal

Chronological record of decisions, discoveries, and changes. Earlier entries preserve the understanding at that time, including claims later corrected. For current status use the [handoff](../../HANDOFF.md#current-status), [roadmap](../../ROADMAP.md), and [documentation index](../README.md). Historical task assignments are not current instructions.

---

## 2026-09-21 · Session 1 — Core ISM conversion + geometry grouping

**Goal:** Convert ~10,000 Herman Miller Aeron chair actors from an IFC/Datasmith import into a minimal set of ISM components.

**Root cause identified:** The original plugin used mesh pointer equality as the grouping key. IFC imports produce one `UStaticMesh` asset per placed instance even when geometry is identical, so pointer equality never matched and the "optimized" result was still 10,000 ISMs — one per source actor.

**Solution:** Replaced the mesh pointer key with a geometry signature — an MD5 hash of the LOD0 triangle data normalized to be centroid-relative (position-independent) and sorted (order-independent). The hash also covers normals, UVs, and material slot names. Two meshes with identical rendered geometry produce the same hash regardless of asset name or path.

**Grouping key after change:** `(FamilyTypeActor, GeometrySignature, MaterialSignature)`

**Key decisions:**
- ISM over HISM throughout. HISM per-cluster culling adds overhead that is redundant and harmful for Nanite meshes. The plugin was originally named for HISM; that is now a historical artifact.
- Geometry signature falls back to asset path for meshes that cannot be hashed (no source models, no triangles), so those actors still group with others referencing the exact same broken asset.
- Canonical mesh = alphabetically first asset path in the group, giving a deterministic result across reruns without sorting.
- Switched `ConVerseBatchHISMLibrary` to also use `UInstancedStaticMeshComponent`.

**Files changed:** `ConVerseHISMUtils.cpp`, `ConVerseStaticMeshConsolidationUtils.cpp/.h`, `ConVerseBatchHISMLibrary.cpp`, `DatasmithHISM.cpp`, `.gitignore`, `README.md`

---

## 2026-09-21 · Session 2 — Production hardening + rename

**Goal:** Make the plugin safe to use on real scenes before the first real-data test.

**Changes:**
- Wrapped `CreateHISMsFromSelection` in `FScopedTransaction` so the entire operation is one undo step. Placed at the library layer, not inside `BuildManagedHISMs`, so the utility stays transaction-neutral for Dataprep use.
- Added two-phase `FScopedSlowTask` progress dialog (both phases cancellable via `MakeDialog(true)`). At this point the cancel button was wired to the dialog UI but `ShouldCancel()` was not checked in the loops — the button appeared but did nothing.
- Added single-instance filter: groups with fewer than 2 actors skip ISM creation (a 1-instance ISM costs more than the source component).
- Added `FMessageDialog::Open(YesNo)` confirmation before `ObjectTools::DeleteObjects` in Dedupe Meshes. Suppressed headlessly when `IsRunningCommandlet()` so Dataprep pipelines don't hang.
- Renamed all public API fields and categories from `HISM` to `ISM` terminology.

**Files changed:** `ConVerseHISMLibrary.h/.cpp`, `ConVerseHISMUtils.cpp`, `ConVerseStaticMeshConsolidationLibrary.cpp`, `ConVerseCreateHISMOperation.cpp`, `ConVerseStaticMeshConsolidationWidget.h`, `DatasmithHISM.cpp`, `README.md`, `PLAN.md`

---

## 2026-09-21 · Session 3 — ISM/HISM creation-time toggle + AGENTS.md

**Goal:** Allow callers to choose HISM instead of ISM for non-Nanite workflows, and document the codebase for AI agents.

**Context:** An external review pointed out that while ISM is correct for Nanite, HISM's hierarchical culling is genuinely beneficial for large non-Nanite populations (vegetation, structural members without Nanite, etc.). The plugin should support both without requiring code changes.

**Solution:** Added `bool bUseHISM = false` parameter through the full call chain. At the `NewObject` call in `BuildManagedHISMs`, the component class is chosen at runtime. The stored pointer remains `UInstancedStaticMeshComponent*` (valid because HISM is a subclass), so all downstream code — property copying, instance adding, tagging, cleanup — is unchanged. `ClearManagedHISMComponents` already catches both types via subclass query.

**AGENTS.md created:** Documents architecture, conventions (ISM-not-HISM rule, transaction placement, progress dialog structure, tag name preservation, `RF_Transactional`, `LOCTEXT_NAMESPACE`), file responsibilities, what to avoid, build instructions, and domain context for AI coding agents.

**Files changed:** `ConVerseHISMUtils.h/.cpp`, `ConVerseHISMLibrary.h/.cpp`, `ConVerseCreateHISMOperation.h/.cpp`, `AGENTS.md` (new)

---

## 2026-09-21 · Session 3b — External review incorporation

**Context:** An external AI review of the problem statement and architecture identified several issues.

**Confirmed correct (no code change needed):**
- `MaterialSignature` already uses `Component->GetMaterial(Index)` — resolves per-component overrides, not mesh defaults.
- `FamilyTypeActor` in the group key is the managed actor (found by `(CleanupBoundary, FamilyLabel)`), not the source wrapper actor — instances of the same Revit family type under the same boundary correctly share one managed actor.
- Geometry hash already covers normals, UVs, and material slot names — not positions only.

**Corrections made to README and PLAN.md:**
- Problem statement reframed: actor proliferation is the primary problem (even with shared assets); mesh-asset proliferation is the secondary/additional case AdvancedHISM handles beyond Unreal's built-ins.
- "Each actor is a separate draw call" removed — Nanite and dynamic instancing make this too absolute. Replaced with actor/UObject/component/primitive overhead language.
- Batch ISMs no longer described as "failing" — it works for shared assets; AdvancedHISM adds equivalence detection for non-shared assets.
- "10,000 actors → 3 draw calls" replaced with "3 instanced components representing 10,000 transforms" — draw call count depends on materials, passes, and rendering path.
- Cleanup boundary inconsistency between two README sections fixed — both now define family wrapper and cleanup boundary as distinct concepts.
- Geometry signature description updated to accurately state it includes normals, UVs, and material slot names.

**New tasks added to PLAN.md:**
- A07: Pivot/anchor compensation (potential placement bug if Datasmith doesn't normalize all mesh pivots — needs T01 real-data verification)
- A08: Mesh-pointer fast path (already mostly handled by signature cache; noted for completeness)
- A09: Auto ISM/HISM based on Nanite status
- A10: BIM metadata preservation per instance
- A11: Grouping mode (BIM hierarchy vs maximum optimization)

---

## 2026-09-21 · Session 4 — Cancel, threshold, auto ISM/HISM, rename, LOD1 fallback

**Tasks:** A01, A02, A09, E01, B02, D01

### A01 — Cancel wired up

`ShouldCancel()` is now checked at the top of both the grouping loop and the build loop. When triggered, `bWasCancelled = true` is set and the loop breaks.

**Orphan prevention:** Managed family-type actors are created during Phase 1 (grouping). If the user cancels mid-Phase 1, some of these actors exist but have no ISM components yet — empty shells in the level. After the build loop, any managed family-type actor with zero `ConVerseManagedHISM`-tagged components is explicitly destroyed.

**Transaction behavior:** Cancel commits the partial result to the undo history. The partial output (whatever was built before cancel) is valid. The user can Ctrl+Z to undo the entire partial result in one step. Full transaction rollback on cancel was not implemented — `FScopedTransaction` RAII makes this non-trivial without switching to the raw `GEditor->BeginTransaction/CancelTransaction` API. Deferred.

`bWasCancelled` added to `FConVerseHISMCreationResult` so Blueprint callers can detect cancellation.

### A02 — Configurable minimum instance threshold

Hardcoded `< 2` replaced by `< MinInstanceCount` (default 2). Parameter added to `BuildManagedHISMs`, `CreateISMsFromSelection`, and the Dataprep operation panel (`ClampMin = 1`).

### A09 — Auto ISM/HISM detection

`bool bAutoDetectFromNanite = false` added. When true, checks `CanonicalMesh->NaniteSettings.bEnabled` at group-build time per group. ISM for Nanite meshes, HISM for non-Nanite. Overrides `bUseHISM` when enabled. Default false preserves all prior behavior.

The tri-state enum (Auto/ForceISM/ForceHISM) described in the plan was deferred in favor of two booleans for now. The enum refactor can happen without breaking callers once the API stabilizes.

### E01 — Blueprint API rename

`CreateISMsFromSelection(Prefix, bUseHISM, MinInstanceCount, bAutoDetectFromNanite)` is now the canonical function. `CreateHISMsFromSelection` is deprecated and delegates to it. Toolbar `RunCreateHISMs` updated to call the new name.

### B02 — LOD1 fallback

`BuildMeshSignature` now tries `GetMeshDescription(1)` if LOD0 is null or empty (affects some Datasmith imports that omit LOD0 source data). `UsedLODIndex` is hashed into the signature so a LOD0-sourced signature and a LOD1-sourced signature for geometrically identical meshes never collide.

### D01 — Resolved as part of A02

`UConVerseCreateHISMOperation` now surfaces `MinInstanceCount` and `bAutoDetectFromNanite` as `EditAnywhere` UPROPERTYs, visible in the Dataprep pipeline panel.

**Files changed:** `ConVerseHISMUtils.h/.cpp`, `ConVerseHISMLibrary.h/.cpp`, `ConVerseCreateHISMOperation.h/.cpp`, `ConVerseStaticMeshConsolidationUtils.cpp`, `DatasmithHISM.cpp`, `README.md`, `PLAN.md`

---

## 2026-09-21 · Session 5 — Grouping mode, analyze/dry-run, Enable Nanite, tag migration, dedupe dry-run

**Tasks:** A11, C01, C02, F01, F02

### A11 — Grouping mode

`EConVerseGroupingMode` UENUM added to `ConVerseHISMLibrary.h`:
- `PreserveBIMHierarchy` (default) — existing behavior; family label is part of the group key so different Revit/IFC families never collapse together even if their geometry is identical
- `MaximumOptimization` — family label is dropped from the group key; actors with identical geometry and materials under the same cleanup boundary collapse into one ISM regardless of family identity; produces the smallest possible component count

Wired through the full call chain: `CreateISMsFromSelection`, `BuildManagedHISMs`, `Dataprep ConVerseCreateHISMOperation`.

**Implementation note:** In `MaximumOptimization`, `FindOrCreateManagedFamilyTypeActor` is still called but with a fixed label (`ComponentNamePrefix` or `"ISM"`) rather than the per-family label. All groups on the same boundary therefore share one managed actor, and the `FHISMGroupKey.FamilyTypeActor` pointer will be the same for all of them on that boundary.

### C01 — Analyze / dry-run

New `AnalyzeManagedHISMCandidates` function in `ConVerseHISMUtils.cpp` that runs Phase 1 grouping logic without creating, modifying, or deleting any actors. Uses a local `FAnalysisKey { CleanupBoundary, FamilyLabel, GeometrySignature, MaterialSignature }` struct (with TMap-compatible `GetTypeHash`) instead of managed actor pointers.

New `AnalyzeISMCandidatesInSelection` Blueprint function in `ConVerseHISMLibrary` (same parameters as `CreateISMsFromSelection`).

New "Analyze ISMs" toolbar button and Tools menu entry — runs analysis on selection and shows result in a message dialog. No transaction, no world changes.

New `FConVerseHISMAnalysisResult` USTRUCT: `ActorsConsidered`, `GroupsAboveThreshold`, `GroupsBelowThreshold`, `ActorsWouldBeConverted`, `ActorsInSmallGroups`, `SkippedActors`, `Summary`.

### C02 — Enable Nanite

New `EnableNaniteOnSelection` Blueprint function. Collects all `UStaticMesh` assets from selection (via `ConVerseStaticMeshConsolidation::CollectStaticMeshes`), checks `NaniteSettings.bEnabled`, and for any mesh where it's false: calls `Modify()`, sets flag, calls `PostEditChange()` to queue async rebuild. Wrapped in `FScopedTransaction`.

New `FConVerseEnableNaniteResult` USTRUCT: `MeshesConsidered`, `MeshesAlreadyEnabled`, `MeshesEnabled`, `Summary`.

New "Enable Nanite" toolbar button and Tools menu entry.

### F01 — Tag migration

New `MigrateTagsInCurrentLevel(OldTagName, NewTagName)` Blueprint function. Iterates all actors in the current editor world via `TActorIterator<AActor>`. For each actor: replaces `OldTag` with `NewTag` in `Actor->Tags`. For each component: replaces `OldTag` with `NewTag` in `Component->ComponentTags`. Wrapped in `FScopedTransaction`. Returns count of replaced tag instances.

### F02 — Dedupe dry-run

Added `bDryRun = false` parameter to both `ConsolidateSimilarStaticMeshesInSelection` and `ConsolidateSimilarStaticMeshes`. In dry-run mode: skips the replace-references pass and the `ObjectTools::DeleteObjects` call, returns a `[Dry run]` prefixed summary showing what would have happened. No confirmation dialog in dry-run mode.

**Files changed:** `ConVerseHISMLibrary.h/.cpp`, `ConVerseHISMUtils.h/.cpp`, `ConVerseCreateHISMOperation.h/.cpp`, `ConVerseStaticMeshConsolidationLibrary.h/.cpp`, `DatasmithHISM.h/.cpp`, `DatasmithHISMStyle.cpp`, `PLAN.md`, `JOURNAL.md`

---

## 2026-09-21 · Session 6 — Explode ISMs, one-click pipeline, dry-run Dataprep op

**Tasks:** C03, C04, D03

### C03 — Explode ISMs

New `ExplodeISMsFromSelection` Blueprint function and `FConVerseISMExplodeResult` struct. The implementation:
1. Collects all actors from selection subtrees via `CollectActorsFromRoots`
2. Identifies actors tagged `ConVerseManagedFamilyType` within those subtrees
3. For each managed family-type actor, iterates `UInstancedStaticMeshComponent` children tagged `ConVerseManagedHISM`
4. For each instance: calls `GetInstanceTransform(i, Transform, /*bWorldSpace=*/true)`, spawns `AActor` + `UStaticMeshComponent` at that transform, copies mesh and per-slot materials and rendering flags
5. Destroys each processed ISM component
6. Destroys managed family-type actors that have no remaining managed ISM components
7. Wrapped in `FScopedTransaction` — one Ctrl+Z undoes everything

Exploded actors are attached to the same parent actor that the managed family-type actor was attached to (the cleanup boundary), preserving the level hierarchy structure.

**Implementation note:** Spawned actors are generic `AActor` with `UStaticMeshComponent` (not `AStaticMeshActor`), matching the structure of the original Datasmith-imported source actors.

New "Explode ISMs" toolbar button and Tools menu entry.

### C04 — One-click pipeline

New `RunOneClickPipeline` handler. Calls `ConsolidateSimilarStaticMeshesInSelection(true)` then `CreateISMsFromSelection(TEXT("ISM"))` sequentially. Shows a combined summary dialog with both results. The Dedupe confirmation dialog is preserved; the ISM pass runs unconditionally after Dedupe returns (regardless of whether the user confirmed the deletion). The two operations each use their own transaction — Ctrl+Z applied twice undoes them separately.

New "Dedupe + ISMs" toolbar button and Tools menu entry.

### D03 — Dry-run Dataprep operation

New `UConVerseAnalyzeHISMOperation` class (`ConVerseAnalyzeHISMOperation.h/.cpp`). Runs `AnalyzeManagedHISMCandidates` on the Dataprep context actors and logs:
- The full summary string
- A broken-down log line: groups above threshold / actors that would convert, groups below threshold / actors left in place, skipped actors

Same UPROPERTYs as `UConVerseCreateHISMOperation`: `NewActorLabelPrefix`, `bUseHISM`, `bAutoDetectFromNanite`, `MinInstanceCount`, `GroupingMode`. Makes parameter matching between the analyze and create operations straightforward.

**Files changed:** `ConVerseHISMLibrary.h/.cpp`, `DatasmithHISM.h/.cpp`, `DatasmithHISMStyle.cpp`, `ConVerseAnalyzeHISMOperation.h/.cpp` (new), `README.md`, `PLAN.md`, `JOURNAL.md`

---

## 2026-09-21 · Session 7 — ISM/HISM split counts, category Dataprep op, HISM toolbar toggle

**Tasks:** E02, D02, C05

### E02 — Result struct expansion

Added `ISMOnlyComponentsCreated` and `HISMOnlyComponentsCreated` to `FConVerseHISMCreationResult`. The existing `ISMComponentsCreated` remains as the sum of both (backward compat).

In `BuildManagedHISMs`, after incrementing `ISMComponentsCreated`, also increments `ISMOnlyComponentsCreated` or `HISMOnlyComponentsCreated` based on `bResolvedUseHISM`.

`FinalizeSummary` now shows `"X ISM + Y HISM component(s)"` in the summary string when both counts are non-zero (i.e., when `bAutoDetectFromNanite` produces a mix). When only one type is present, shows the plain count as before.

### D02 — Category-aware Dataprep operation

New `UConVerseCategoryGroupHISMOperation` class. `CategoryFilter` UPROPERTY is a case-insensitive substring matched against each actor's `GetActorLabel()`. Empty filter → all actors pass (equivalent to the standard Create ISMs operation).

Matching is applied only to root actors from the Dataprep context. Once filtered roots are identified, `CollectActorsFromRoots` expands to include their full subtrees. This means a root actor with label "Furniture_Aeron" will include all its children regardless of child labels.

Same ISM parameters as `UConVerseCreateHISMOperation` (`NewActorLabelPrefix`, `bUseHISM`, `bAutoDetectFromNanite`, `MinInstanceCount`, `GroupingMode`).

### C05 — ISM/HISM toggle in toolbar

New `bool bToolbarUseHISM` member on `FDatasmithHISMModule`. Loaded from `GEditorPerProjectIni` at `StartupModule()` via `GConfig->GetBool`. Saved on toggle via `GConfig->SetBool`.

New "Use HISM" toggle button in the toolbar (`EUserInterfaceActionType::ToggleButton`). `IsUseHISMEnabled()` is the checked-state callback. The toggle affects `RunCreateHISMs`, `RunAnalyzeISMs`, and `RunOneClickPipeline` — all pass `bToolbarUseHISM` as `bUseHISM` to the library functions.

**Note:** The toggle does not affect `bAutoDetectFromNanite` (which is only configurable via Blueprint/Dataprep API, not the toolbar, to keep the toolbar simple). For Nanite scenes, leave the toggle off and rely on `bAutoDetectFromNanite` via Blueprint.

**Files changed:** `ConVerseHISMLibrary.h`, `ConVerseHISMUtils.cpp`, `DatasmithHISM.h/.cpp`, `DatasmithHISMStyle.cpp`, `ConVerseCategoryGroupHISMOperation.h/.cpp` (new), `PLAN.md`, `JOURNAL.md`

---

## 2026-09-21 · Session 8 — IFC storey-boundary grouping

**Task:** A06

### A06 — IFC spatial hierarchy awareness

**Problem:** In multi-storey IFC models (IfcBuildingStorey hierarchy), `FindCleanupBoundaryActor` was walking up to the first non-mesh ancestor and stopping there. In a large office building this might be an individual room or space actor, producing one managed family-type actor per space rather than per storey. For models with hundreds of storeys and many rooms per storey, this results in far more managed actors than necessary — the per-storey grouping is both more useful semantically and cheaper to render.

**Solution:** Two-phase boundary walk in `FindCleanupBoundaryActor`.

Phase 1 (unchanged): walk up to the first non-mesh ancestor — this is always the baseline / fallback boundary.

Phase 2 (new, only when `Cache.StoreyBoundaryPatterns` is non-empty): starting at the Phase 1 result, walk further up the parent chain looking for the first ancestor whose label contains any of the patterns (case-insensitive `FString::Contains`). If found, that ancestor becomes the cleanup boundary. If no match is found, falls back to the Phase 1 result — original behavior preserved.

**Cache field:** `FFamilyTypeLookupCache::StoreyBoundaryPatterns TArray<FString>` — set once when the cache is constructed at the top of `BuildManagedHISMs` / `AnalyzeManagedHISMCandidates`, then read by `FindCleanupBoundaryActor` on every call.

**Example patterns:** `{"Level", "Floor", "Story"}` — case-insensitive, so "Level 1", "FLOOR 3", "Ground Story" all match.

**Empty array = original behavior:** The phase 2 block is guarded by `Cache.StoreyBoundaryPatterns.IsEmpty()`, so all existing callers that don't set the field are unaffected.

**Propagation:**
- `BuildManagedHISMs` and `AnalyzeManagedHISMCandidates` in `ConVerseHISMUtils.h/.cpp` gain `const TArray<FString>& StoreyBoundaryPatterns = TArray<FString>()` as trailing parameter.
- `UConVerseCreateHISMOperation`, `UConVerseAnalyzeHISMOperation`, `UConVerseCategoryGroupHISMOperation` headers each gain `UPROPERTY(EditAnywhere, Category = "ISM") TArray<FString> StoreyBoundaryPatterns;`
- All three operation CPPs pass `StoreyBoundaryPatterns` through to the utility functions.

**Not exposed as UFUNCTION parameter:** `TArray<FString>` default values are not supported by UHT in UFUNCTION declarations. Blueprint users can call `BuildManagedHISMs` indirectly through the library, but storey patterns are only configurable via Dataprep operations or direct C++ API.

**Files changed:** `ConVerseHISMUtils.h/.cpp`, `ConVerseCreateHISMOperation.h/.cpp`, `ConVerseAnalyzeHISMOperation.h/.cpp`, `ConVerseCategoryGroupHISMOperation.h/.cpp`, `PLAN.md`, `JOURNAL.md`

### B03 — Normalization audit for IFC coordinates (analytical)

**Question:** Does `GetMeshGeometrySignature` correctly cancel IFC survey-point offsets so that identical geometry placed at different world positions hashes the same?

**Analysis:** `PositionOrigin = PositionBounds.GetCenter()` is computed from mesh-description-local vertex positions (not world positions). Two cases:

1. **Survey point in actor transform (Datasmith standard):** mesh description vertices are in local space near the origin. `PositionOrigin ≈ (0,0,0)`. Normalization is a near-no-op. Two identical meshes placed 1km apart have the same mesh description → identical hashes. ✓

2. **Survey point baked into vertex positions (non-standard):** vertices are offset by the survey point in mesh description space. `PositionBounds.GetCenter()` captures that offset. `vertex - centroid` produces centroid-relative positions that are identical for geometrically equivalent meshes regardless of their absolute position. ✓

**Conclusion:** Normalization is correct in both cases. No code change needed. T01 real-data testing should provide empirical confirmation.

---

## Open questions / blockers

**T01 — Real-data test not yet run.** All implementation work is based on reasoning about expected Datasmith/IFC import behavior. The following assumptions need validation with actual data:

1. **Pivot normalization (A07):** The code uses `Actor->GetActorTransform()` as the instance transform, assuming Datasmith places mesh geometry at the local origin of each actor. If IFC imports produce meshes with geometry at non-origin local positions, instances will be offset by the difference between the canonical mesh's local origin and each source mesh's local origin. This could produce visually wrong results. Verify during T01 by checking that converted instances sit exactly where source actors were.

2. **Hash performance (B01):** Unknown how long `BuildMeshSignature` takes on real-world IFC meshes at the scales we care about. Profile during T01.

3. **LOD1 fallback (B02):** Untested against actual imports that omit LOD0. Verify that the fallback triggers and produces correct grouping.

4. **Single-instance threshold:** With real data, check whether `MinInstanceCount = 2` is the right default or whether some categories produce many 2-instance groups that would be better left unconverted.

---

## 2026-09-22 · Optimized Datasmith import panel planning and delegation

**Goal:** Add a dockable panel that parses `.udatasmith`, offers explicit ISM or HISM output, optimizes eligible source actors before they populate the current level, and verifies the imported result.

**Planning first:** [IMPORT_PANEL_PLAN.md](2026-09-26/IMPORT_PANEL_PLAN.md) now defines five gated phases, the panel controls and states, atomic work units, file ownership, and acceptance checks. [IMPORT_PANEL_VALIDATION.md](../../IMPORT_PANEL_VALIDATION.md) freezes the immutable-plan, metadata-manifest, rollback, verification, and optimized-reimport contracts.

**Baseline evidence:** UE 5.8.3, changelist 58210709. The first integrated UBT attempt reached the new scaffold and failed on two confirmed compile issues: `FSpawnTabArgs` was forward-declared as a struct, and the service used unavailable `FMD5Hash::HashBytes` and `FMD5Hash::ToString` members. No `.udatasmith`, `_Assets`, or `.ifc` fixture exists under this project, so real Revit acceptance remains pending.

**Independent API audit:** UE 5.8 supports the proposed `FExternalSource` to mutable `IDatasmithScene` to `UDatasmithImportFactory` seam, and native Datasmith HISM elements import as `UHierarchicalInstancedStaticMeshComponent`. The audit also found release-blocking gaps in the partial scaffold: source metadata loss, no rollback, partial HISM-to-ISM failure, unsafe ordinary/automatic reimport, name-based mesh/material verification, repeated-import ambiguity, and unsupported negative-scale HISM instances.

**Contract decisions:**

- Analyze must be read-only and return a deterministic, pointer-free plan tied to the source hash.
- Grouping initially uses exact Datasmith mesh references within one immediate parent boundary. Cross-asset geometry equivalence stays in the existing post-import converter until payload and pivot equivalence are proven.
- ISM is the default. HISM remains explicit.
- Negative-determinant instance transforms are skipped and retained as ordinary actors in the first release because UE 5.8 warns that negative instance scale is unsupported for native Datasmith HISM import.
- Verification uses imported object mappings from `UDatasmithScene`, exact component classes, ordered instance transforms, material slots, shared settings, and complete group/session accounting.
- Converted Revit/Datasmith identity must persist in a plugin manifest. Missing identity is recorded rather than inferred.
- Cancellation or verification failure cannot be reported complete until the attempted session is removed. Ordinary Datasmith reimport must not bypass the optimizer.

**Delegation:** Separate agents own the read-only API audit, validation contract, isolated Slate panel, core service, persistent manifest types, and module/tab integration. The root integrator owns cross-file reconciliation, builds, tests, and project documentation.

---

## 2026-09-22 · Optimized Datasmith import implementation and validation

**Implemented:** The five-phase panel plan is now represented by the plugin code: a Tools-menu nomad panel, immutable analysis plan, native Datasmith scene transformation, requested HISM output or session-scoped HISM-to-ISM conversion, manifest persistence, strict component/material/transform verification, rollback accounting, and standard-reimport/repeated-import protection.

**Automation fixture:** Added `DatasmithHISM.OptimizedImport.GeneratedFixtureEndToEnd`. It exports a temporary Datasmith scene containing two compatible instances and one mirrored instance, then exercises Analyze and both ISM/HISM import paths. Its assertions cover deterministic planning, no Analyze mutation, expected group/instance counts, negative-scale rejection, source-file immutability, persistent metadata and identity records, component class, ownership marker, and cleanup.

**UE 5.8.3 build:** The following command succeeded after adding the direct `DatasmithExporter` module dependency and using the supported `LexToString(FMD5Hash)` helper in the automation test:

`UnrealBuildTool.exe AdvancedHISMEditor Win64 Development -Project="D:\Unreal\Sandbox\AdvancedHISM\AdvancedHISM.uproject" -WaitMutex -NoHotReloadFromIDE`

**Runtime test limitation:** The packaged `UnrealEditor-Cmd.exe` launcher did not reach the test runner. Before creating the requested log it invokes all-platform SDK validation, which reports missing LinuxArm64 and VisionOS `MainVersion` SDK settings despite a valid Win64 SDK. This is an environment configuration failure, not a passing or failing test result. Run the generated fixture test once the editor launcher can start, then validate against a representative Revit `.udatasmith` file and its `_Assets` folder.

---

## 2026-09-22 · Importer documentation reconciliation

Updated the project-level [PLAN.md](2026-09-26/PLAN.md), [README.md](../../README.md), and optimized-import plan so their status matches the implementation evidence: the UE 5.8.3 editor target builds, the generated-fixture test compiles, runtime automation remains blocked before editor startup by optional-platform SDK validation, and real Revit/Datasmith acceptance still requires a representative export and sidecar assets.

---

## 2026-09-22 · Dedupe cancellation and pipeline safety

**P1.2:** Added `EConVerseStaticMeshConsolidationOutcome` to the Blueprint result contract. The interactive Dedupe confirmation now occurs before mesh references are repointed, so a No response reports `Cancelled` and leaves both references and assets unchanged. A partial deletion is reported as `Failed`. Headless Dataprep behavior remains non-interactive.

**P1.3:** The **Dedupe + ISMs** toolbar pipeline now stops before Managed ISMs if Dedupe is cancelled or fails, and its result dialog states that the ISM phase did not run.

**Build evidence:** `AdvancedHISMEditor Win64 Development` succeeded with UHT after both changes. Manual editor-path verification on disposable content remains pending because the packaged command-line editor launcher is still blocked before startup by optional-platform SDK validation.

---

## 2026-09-22 · Explode failure safety

**P1.4:** Explode now stages and validates every replacement actor and static-mesh component before it removes the source ISM/HISM component. If any replacement fails, staged actors are destroyed, the source component remains intact, and the failure count is retained in the result. The existing transaction and managed-tag behavior are preserved.

**Build evidence:** `AdvancedHISMEditor Win64 Development -Project="D:\Unreal\Sandbox\AdvancedHISM\AdvancedHISM.uproject"` succeeded. Forced spawn failure, normal success, undo/redo, and attachment behavior still require manual editor validation.

---

## 2026-09-22 · Dedupe reference-scope safety

**P1.5:** Dedupe now audits every candidate duplicate mesh against loaded `UStaticMeshComponent` references and Asset Registry on-disk package referencers. It only replaces and deletes a duplicate when all detected references are inside the toolbar selection or Dataprep context. Any external or uncertain reference skips the duplicate conservatively. The Blueprint result exposes the skip report, and Dataprep logs unsafe skips.

**Build evidence:** `AdvancedHISMEditor Win64 Development` succeeded. Manual validation remains for selected-only references, unselected actor references, asset-only selection, unloaded-map referencers, dry run, declined confirmation, and Dataprep logging.

---

## 2026-09-22 · Planning review — five-phase handoff

**Scope:** Reviewed `README.md`, `PLAN.md`, `JOURNAL.md`, `AGENTS.md`, and the current conversion, Dedupe, toolbar, and Explode paths. This was a documentation and planning pass; no C++ implementation was changed and no build or editor test was run. The plugin Git worktree already contains uncommitted source and documentation changes. No `.ifc`, `.udatasmith`, or `.umap` test input was found in the current project Content tree.

**Findings that set the order:**

1. `ConsolidateSimilarStaticMeshes` calls `ReplaceStaticMeshReferencesInObjects` before the interactive `FMessageDialog`. A No response cancels deletion after references have already changed. `RunOneClickPipeline` runs Managed ISMs even when that Dedupe result reports cancellation.
   `ReplaceStaticMeshReferencesInObjects` only walks supplied objects; the subsequent asset deletion may encounter references outside that selection. Phase 1 now requires a reference-scope check before deletion.
2. `ExplodeISMsFromSelection` counts individual spawn failures but still removes the source ISM component after the loop. This can remove instances that were not recreated.
3. Managed grouping keys on family, geometry, and material, then copies collision/rendering/mobility properties from the first source component. Different effective settings can therefore share one ISM. `GetSourceWorldTransform` currently returns `Actor->GetActorTransform()`; component-relative offsets and canonical pivot differences are unverified.
4. Existing managed output is cleared when a new eligible actor is encountered on a boundary. A reimport or partial source set needs an explicit affected-import contract before the cleanup/rebuild behavior can be called safe.
5. `AGENTS.md` still described several Session 4–8 features as open; `README.md` counted three Dataprep operations though four exist. Both documents were corrected for this handoff.

**Decision:** `PLAN.md` now has five gated phases: baseline/destructive-tool safety, placement and setting equivalence, reimport lifecycle and source identity, multi-component coverage and measured performance, then real IFC acceptance. Old task rows remain as an idea inventory. The 10,000-chair result is a target case, not a verified outcome.

### Follow-up review — UE 5.8 documentation and conversion code

Epic's ISM/HISM and Datasmith guidance is summarized with links in `Info.md`. The documentation distinguishes Datasmith Scene Asset reimport from level-actor synchronization, and notes that deleted level actors are not respawned by default. This makes the plugin's source-actor deletion a material reimport design issue.

The conversion code also has direct correctness gaps: `BuildMaterialSignature` reads effective component materials but the new ISM never receives those overrides; `AddInstance` results are ignored before source actors are queued for deletion; eligibility does not reject actors with additional non-mesh payload; and below-threshold groups can leave empty managed family actors. `PLAN.md` Phase 2 now has separate work units and fixture checks for those findings. This review did not change C++ or run a build/editor test.

---

## Session — Tessellation options and the `ComputePlanId` fix

**Goal:** Expose Datasmith tessellation settings (chord tolerance, max edge length, normal angle, stitching) through the import panel and the headless commandlet.

**The load-bearing part was not the feature.** Surfacing the options was routine. The real defect was that `ComputePlanId` did not include them. Tessellation changes the geometry the importer *generates*, so a user could change chord tolerance, reimport, and get the old mesh back with a cheerful `AlreadyCurrent` — a silent wrong answer with no error. An input that changes the output was missing from the identity hash.

**Verification that mattered:** `TessellationAffectsPlanIdentity` was mutation-tested rather than trusted for passing. Options are clamped to sane ranges, and Amendment 4 was added to the contract in the same change as the behavior, per the project rule.

---

## Session — Sidecar hashing, warn-first

**Goal:** Close the last known correctness gap: only the primary source file was hashed, never the `_Assets` sidecar folder where Revit and CAD sources actually keep their geometry. Re-export with changed meshes and the primary file can stay byte-identical, so the importer reported success while serving stale geometry — the same bug class as the tessellation omission above.

**Decision made by measurement, not by asking again.** The user was undecided between a metadata fast path, full content hashing, and a hybrid. Rather than re-present the trade-off, MD5 throughput was benchmarked at ~250 MB/s (~4s/GB). That is noise beside CAD/Revit tessellation measured in minutes, and it **reversed the standing recommendation** from the hybrid to plain full content hashing. Timestamp heuristics are unreliable across copies, network shares, and VCS checkouts, which is a poor guard for a destructive operation.

**The user chose warn-first, and that choice constrains the design.** A sidecar-only change keeps `AlreadyCurrent`, stays read-only, creates no session, and reports through `bSidecarChanged`, the summary, a log warning, and the panel's warning-toned status.

**Critical consequence:** the sidecar hash is deliberately **excluded** from `ComputePlanId` — the exact inverse of the tessellation fix, and intentionally so. Folding it in would change the PlanId, bypass the `AlreadyCurrent` branch entirely, and produce the automatic destructive reimport the user had just declined. Identity hashes drive action; advisory hashes drive reporting. Tessellation belongs in the first category, the sidecar in the second.

**Three smaller decisions, all load-bearing:**
- Relative paths are sorted before folding. Directory enumeration order is not guaranteed stable, and an unsorted fold would produce a fresh hash every run and warn constantly.
- An empty recorded hash means "not recorded", never "changed", so manifests predating the field do not warn spuriously. `ContractVersion` was *not* bumped, because it feeds `ComputePlanId` and would have invalidated every committed session.
- An absent sidecar folder is legitimate; one that exists but cannot be read is a failure that names the offending file, so a permissions error cannot masquerade as a self-contained source.

**Verification:** clean build, 9/9 automation tests, and `SidecarChangeWarnsWithoutReimport` mutation-tested — forcing `bSidecarChanged` to false failed exactly the detection and summary assertions and nothing else. The test also asserts the primary file's size and timestamp are untouched, so it cannot pass for the wrong reason. Amendment 5 landed with the behavior.

**State at close:** no known correctness bugs remain in the optimized import path. The highest-risk outstanding item is that rollback has **never once executed** — untested destructive code whose entire purpose is protecting user data.

---

## 2026-09-24 · Rollback run — forcing the failure path to execute

**Scope:** Closed the top two items from `NEXT_STEPS.md`: rollback had never run, and the toolbar gave no indication that its two paths carry different guarantees.

**The seam.** Rollback is unreachable without a failure, so `FConVerseOptimizedImportOptions::FailureInjection` was added to abort at checkpoints that *already* route through `RollBackAttempt` — no new failure plumbing, just a way to reach the existing one. `AfterDatasmithImport` aborts while the attempt owns assets and actors; `BeforeManifestCommit` aborts after verification has passed, the latest point that still rolls back.

**The decision worth recording:** `ObstructRollback` does not fake a failed return value. It suppresses only the destruction pass, and the existing verification sweep then finds the actors still present and reports failure on its own terms. A seam that hardcoded `bRollbackSucceeded = false` would have tested nothing but the seam. It also skips the asset force-delete while obstructing, because force-deleting assets still referenced by surviving actors produces a *worse* state than the one under test.

**Second inversion of the PlanId rule.** The injection knob is deliberately **excluded** from `ComputePlanId`, and for a different reason than the sidecar hash. The sidecar is excluded because including it would trigger destructive action. This is excluded because it is incapable of producing committed output at all — every non-`None` value aborts before commit, so no committed session can ever exist whose identity depended on it. Documented as Amendment 3.

**Verification — the part that actually matters.** Both rollback tests were mutation-tested, separately, because one mutation could not catch both:
- Suppressing actor destruction in `RollBackAttempt` failed `InducedFailureRollsBackCleanly` at both depths (4 remaining objects, world actor count 149 vs 145) — but left `ObstructedRollbackDegradesToRollbackFailed` passing, since not destroying actors is that test's own premise.
- Hardcoding `bRollbackSucceeded = true` failed the obstruction test on status, flag, and stage.

`InducedFailureRollsBackCleanly` also asserts `CreatedObjectCount > 0`, so a rollback that never had anything to undo cannot pass it, and re-imports cleanly afterwards to prove the rollback did not poison its own destination.

**Drift check promoted.** "Delete an actor by hand, reimport, expect a warning not a success" was a manual-only item. It is now `DeletedActorReportsDriftNotSuccess`, which first proves an untouched reimport verifies clean so a drift report cannot pass for the wrong reason.

**UI disclosure.** The panel now separates **"Tracked import — verified, reversible"** from **"Selection tools — in-place, not reversible"**, with the legacy tooltips stating "no manifest, no verification, no rollback". `REVIEW.md` recommended disclosure over rewriting the legacy path; the asymmetry is unchanged, but it is no longer silent.

**Process note.** `Build.bat` first refused with "Unable to build while Live Coding is active" — the documented condition, not a new blocker. Rather than assume, the docs were updated to a prominent NOT COMPILE-VERIFIED state and the build retried; it then succeeded, and the banners were removed. The banner existed for roughly ten minutes, which is the correct lifetime for one.

**Verification:** clean build, **12/12** automation tests, both new rollback tests mutation-verified, Amendment 3 and the panel disclosure requirement landed with the behavior.

**State at close:** rollback is no longer untested destructive code, and the two paths are honestly labelled. The highest-value outstanding item is now exercising the importer against a real Revit, IFC, or CAD model — the one thing automation cannot substitute for.

## 2026-09-25 · Import log — making failures visible and durable

**Request:** "implement an import log ... so you can see when an import fails."

**What I found before writing anything.** Per-attempt JSON reports already existed, already covered
every failure path, and were already funnelled through a single `SaveAndLogReport`. So the honest
framing was not "imports aren't logged" but "why is a logged failure still hard to see?" Three
concrete answers, two of which were latent bugs rather than missing features:

1. **Everything logged at `Display`.** Filtering the Output Log to warnings and errors — the first
   thing anyone does when hunting a problem — showed nothing at all. Even `RollbackFailed`, the
   worst state this system can report, was indistinguishable from a clean success.

2. **Repeated failures destroyed their own history.** The report filename fell back to `PlanId` when
   no session existed. `PlanId` is deterministic for a given source and options, so an import that
   failed ten times left exactly one artifact — the tenth. The precise scenario where you most want
   history is the one where it was being erased.

3. **No chronological view.** Answering "when did this start failing?" meant opening N JSON files.

The third is the feature that was asked for; the first two are why it would not have been enough on
its own.

**Decisions worth recording.**

*`AlreadyCurrent` is not automatically a success.* It was tempting to treat it as one. But an
`AlreadyCurrent` that found drift means committed output no longer matches its manifest — exactly
the silent failure this log exists to surface. It counts as success only when re-verification passed
and the sidecar is unchanged. Getting this wrong would have logged the most interesting failure mode
as `Info`.

*Logging must never be able to fail an import.* If the CSV is open in Excel and locked, the write
fails. An import that genuinely succeeded must not be reported as failed because a log file was
busy. One warning, outcome untouched.

*Unbounded retention.* Rotation was considered and rejected: silently discarding failure history
defeats the purpose. Rows are ~300 bytes; the cost is negligible and the policy is documented rather
than implemented as a silent truncation.

*Test scoping.* The log is append-only and shared across the whole automation run, so asserting on
absolute row counts would have been order-dependent and flaky. The test tags its own destination
path with a per-run GUID and counts only rows containing it. It also asserts the *second* consecutive
failure produces a second row and a second surviving artifact — that assertion is the one that would
have caught defect 2.

**Verification:** clean build, **13/13** automation tests.

Two compile errors first, both `C4459: declaration of 'LogPath' hides global declaration` — the
engine has a global `LogPath`, and this project builds warnings-as-errors. Renamed to
`ImportLogFile` in both files.

Then two *test* failures, and both were real defects rather than bad tests:

1. **`ObstructedRollbackDegradesToRollbackFailed` regressed.** It had passed for two sessions. My
   change promoted `RollbackFailed` to `Error`, and the automation framework fails any test that
   logs an unexpected error. The test deliberately induces `RollbackFailed`, so the fix was
   `AddExpectedError`, not backing off the severity — the severity is the point of the feature.
   Worth noting this is a permanent constraint now: promoting a status to `Error` breaks any test
   that induces it on purpose.

2. **Three imports produced four log rows.** I had assumed a miscount in my test. Reading the actual
   CSV showed two `Verified` rows for one import. `FinishResult` is called twice on the success
   path: once at the `Verified` checkpoint before commit, so a report survives a later commit
   failure, and once at the real exit. Sound design, and completely invisible while each report
   overwrote its own file — but it double-counts against an append-only log. Added a `bTerminal`
   flag; only terminal calls append a row.

The second one is the interesting result of the session. The double-finish predates this work and
was undetectable by design; adding an append-only log is what made it observable. The log found a
bug on its first run.

**Then mutation testing found a third defect — in my own fix.** Reverting the timestamped report id
should have failed the new test. It *passed*. That meant the test was wrong, not that the code was
safe, and it was worth chasing rather than shrugging at.

Cause: failed imports are assigned a `SessionId`, so they take the session branch and never touch
the `PlanId` fallback the fix targets. My assertion was passing for free and proving nothing about
durability. The sessionless path — `Analyze`, pre-session failures — is the one actually at risk.

Rewriting the assertion around two identical `Analyze` calls then failed **unmutated**: second-
resolution timestamps collide when two attempts land in the same second, which under automation is
the norm rather than the exception. So the durability fix as originally written was broken for
exactly the case it existed to protect. Added milliseconds plus a process-lifetime atomic counter;
the counter is what guarantees uniqueness, the timestamp is only for readability. The mutation then
failed correctly, and was reverted.

Three defects found, and the two most interesting were found by tooling I added for a different
reason. A green mutation is a failed mutation test.

**State at close:** the import log is built, verified, and documented as Amendment 4. Confirmed by
direct inspection of the CSV rather than green assertions alone: one row per attempt, and a severity
spread across the full run of 26 Info / 7 Warning / 1 Error — the single Error being the intentional
`RollbackFailed`. 13/13 tests pass with all mutations reverted. The highest-value outstanding item
remains exercising the importer against a real Revit, IFC, or CAD model.

---

## 2026-09-26 · Accepting a failed verification — weakening a guarantee on purpose

**Ask:** "verify and import won't import if it fails. Can we have a way to accept and continue even
with failed imports?"

**The finding that shaped everything.** The obvious design is "keep the groups that passed, leave
the rest alone." It cannot be built. `ConvertGroups` destroys each source HISM at line ~1432, and
`VerifySession` does not run until line ~2980. Conversion is internally atomic — every target is
built and preflighted before any source dies — but that atomicity ends *before* verification. By the
time a group is known to be bad, the original it replaced no longer exists. Partial recovery would
mean re-creating geometry from snapshots: a second unverified mutation stacked on an already-failed
import. Rejected.

So acceptance is whole-session. That is a real limitation, not a shortcut, and it is recorded as an
open question in NEXT_STEPS.md because it may not be what was actually wanted.

**Design.** Every previous session in this project *strengthened* a guarantee. This one weakens the
central one: committed output is no longer necessarily verified. Three decisions keep that honest.

1. *No pre-armed flag.* `bDeferRollbackOnVerificationFailure` only parks the attempt; keeping the
   output takes a separate explicit call. A standing "ignore failures" setting would quietly rot the
   guarantee across every future import — exactly how safety systems die.
2. *Accepting never rewrites history.* `bVerificationSucceeded` stays false and the log records
   `Warning`. The log answers "did the checks pass", not "did the user mean it".
3. *Quarantine.* This is the part that makes the feature defensible. An accepted manifest is marked
   degraded and optimized reimport is refused against it, because superseding destroys actors using
   the manifest's own records — the records verification just proved untrustworthy. Without this,
   accepting once would arm a destructive operation later.

Parked attempts live in memory only. Resuming a half-finished mutation across an editor restart,
against a world that may have changed underneath it, is worse than losing the ability to accept. The
panel resolves the decision in a modal inside the same click handler, defaulting to discard, so a
parked attempt cannot outlive the operation.

For tests, `CorruptBeforeVerification` removes one real instance from one real component. The
failure is genuine — `VerifySession` reports a count mismatch on its own terms — rather than
injected verdicts, which would test nothing.

**State at close: NOT COMPILE-VERIFIED.** Live Coding held the build for the entire session (editor
"Crusoe", PID 10576) and I did not force-kill it. Nothing here has been compiled or run. Flagged at
the top of HANDOFF.md and NEXT_STEPS.md with the specific mutations that should be tried against the
two new tests once a build is possible.

---

## 2026-09-25 - Multi-material verification was broken from the start

**Ask:** a paste of real import output - many `FAIL ... material pointer mismatch at slot 1` lines on
`Furniture_Chair-Breuer` ISM components, alongside `PASS` on mullions and railings - and the
question "why did these items fail on import?"

**It was the verifier, not the import.** The components were built correctly. `ResolveExpectedAssets`
mapped each imported `UStaticMesh` material slot back to its Datasmith slot id with
`LexFromString(SlotId, *MaterialSlotName)`. That could never work:

- `FDatasmithStaticMeshImporter::ApplyMaterialsToStaticMesh` renames every slot to the **imported
  material asset's name** (`DatasmithStaticMeshImporter.cpp:767`).
- `FDatasmithStaticMaterialTemplate::Apply` copies that same name into `ImportedMaterialSlotName`
  (`DatasmithStaticMeshTemplate.cpp:84`).

So neither `FStaticMaterial` name field retains the numeric id by the time verification reads it.
`LexFromString(int32&, ...)` is `FCString::Atoi`, which **writes `0` for a non-numeric string rather
than failing** - silently defeating both the `INDEX_NONE` initializer and the positional fallback
sitting right underneath it. Every slot resolved to Datasmith slot 0. Slot 0 matched by coincidence;
slot 1 and beyond were compared against slot 0's material.

That explains the selective symptom exactly: single-slot meshes (mullions, railings) pass, every
multi-material mesh fails. And because a failed verification triggers rollback, **no multi-material
mesh could ever be optimized.** This shipped as a silent ceiling on what the tool could do.

**Fix.** The numeric id survives in exactly one place: the LOD0 `FMeshDescription` polygon-group
material slot names. `UDatasmithStaticMeshTemplate::Apply` reorders `StaticMaterials` to match
polygon-group order (`DatasmithStaticMeshTemplate.cpp:259-286`), so index *i* of `StaticMaterials`
corresponds to polygon group *i*. `BuildImportedSlotIds` reads the ids from there, falling back to
parsing `ImportedMaterialSlotName`, then to positional index. `ParseDatasmithSlotId` now rejects any
string that is not all digits instead of trusting `Atoi`.

The identical bug existed in the manifest-recording loop that populates `GroupRecord.MaterialSlots`,
so the persisted record of which material sat in which slot was wrong too. Both call sites now share
the helpers.

**Key decisions:**
- *Read the id from the mesh description, not from a name.* The alternative - matching by material
  asset identity - would work only until two slots referenced the same material, which is common.
  Position within the polygon groups is the actual invariant Datasmith maintains.
- *Three-tier fallback rather than a hard failure.* A mesh whose polygon-group count no longer
  matches its slot count is not necessarily corrupt, and verification should not reject output it
  cannot map when the positional order is still meaningful.
- *No contract amendment.* `IMPORT_PANEL_VALIDATION.md` line 283 and scenario S10 already required
  exact per-slot material matching. The contract was right; the code was not meeting it.

**Mutation-verified.** The new `MultiMaterialSlotsVerify` test was written *after* the fix, so it had
proved nothing yet. Forcing every slot id back to `0` reproduced `material pointer mismatch at slot 1`
on the synthetic two-material fixture - the user's exact symptom. Worth noting the first mutation
attempt failed to compile: unreachable code is an error under warnings-as-errors, so the mutation had
to be hidden behind an opaque condition. Reverted, suite re-run green.

**Amendment 6 executed for the first time, and failed immediately.** Accepting a failed verification
had been written in a prior session but never compiled - Live Coding held the build. Running it now
surfaced a real defect on the first try: `CommitManifestAndOwnership` hardcoded
`Verification.State = Passed` and `FailedGroupCount = 0`, and `AcceptFailedVerification` set only
`bAcceptedWithFailedVerification`. An accepted-with-failures manifest therefore **still claimed it
verified.** Commit now derives both fields from the result it is given.

Contract clause 4 of Amendment 6 said "accepting never claims the checks passed" but only enumerated
the in-memory `bVerificationSucceeded`. Extended to cover the persisted manifest as well, since that
is the record a future supersede would consult.

This is the project's own lesson landing twice in one session: *write the test even when the code
looks safe*, and *a green mutation is a failed mutation test*.

**Verification:** clean build, **16/16** automation tests, `MultiMaterialSlotsVerify`
mutation-verified, all mutations reverted.

**State at close:** `HANDOFF.md` no longer carries the "NOT COMPILE-VERIFIED" banner - Amendment 6 is
built, run, and corrected. The highest-value outstanding item is unchanged and now sharper: the
multi-material fix was proven against a synthetic fixture, **not** against the user's real Revit
dataset that produced the original failures. Re-running that import is the real confirmation.

**Confirmed on real data (2026-09-25).** The user re-ran the original Revit import in-editor: the
`Furniture_Chair-Breuer` groups that produced every `material pointer mismatch at slot 1` line now
verify and stay optimized. The synthetic fixture predicted the production result correctly, and the
close-out caveat above is resolved for the multi-material path. Sidecar-change warnings and
changed-source reimport are still proven only against generated fixtures.


## 2026-09-26 - Consolidated roadmap implementation and evidence

Implemented the editor workflow across Analyze progress/cancellation, source mesh accounting,
tracked manual-edit conflicts and explicit rebuild, independent Nanite policies and exact mesh
exceptions, light/IES source checks and MegaLights advice, named/session presets, reviewed
material catalog/mapping tables, inspection, persistence/recovery diagnostics, and a separate
runtime source-metadata module. Ownership, replacement, commit, and rollback remain centralized.
Legacy conversion now preserves component world placement and groups compatible descriptors.

Added ordinary/instanced geometry fixtures and extracted one source light/IES fixture. The
structural 16K6 joist payload has 88 vertices and 160 triangles, with no web diagonals. All six
independent ordinary/ISM/HISM and Nanite comparisons match that payload. Revit elements
610662/610663 need a corrected source export; no diagonal geometry was fabricated. Full
structural/HVAC preflight also found missing textures. All 1,033 HVAC lights declare Unitless;
physical Revit calibration is unresolved and those values were preserved.

The DataTable/review infrastructure is implemented, with 245 observed appearances recorded.
These are not verified Autodesk stock identities or approved Unreal equivalents. Source
appearance changes invalidate exact approvals; external replacement assets remain outside
rollback ownership. Rebuild previews record group changes, per-material and per-light old/new
values, and tracked edits; older inventories explicitly report missing comparison evidence.

Validation: UE 5.8.3 editor build succeeded; all 24 automation tests passed with exit 0 and no
controller errors. Save/reopen passed. A material expression GUID populated by PostLoad had
caused false drift; canonical comparison now ignores generated expression IDs while tests
prove actual parameter edits remain detectable. A real read-only validation-map save failed
with exit 1 and an unchanged map hash; its original attributes were restored.

Win64 Development build/cook/archive passed. NullRHI and DX12 packaged runs passed with three
source records, one mesh component, one light, one IES profile, and one successful collision
trace. Final preview/progress/timing editor refinements were built/tested afterward; packaged
runtime code did not change. No rendered-scene equivalence or representative performance
claim follows from these smoke tests.

Release acceptance remains incomplete. See ROADMAP_EXECUTION.md and
Docs/Validation/2026-09-26.md for current source-data, calibration, catalog, UI, recovery,
rendered, and performance gates. Phase 6 stays deferred. Earlier uncommitted changes were
preserved; no commit or publication was performed.


## 2026-09-26 - Documentation and ADR consolidation

Reconciled READMEs, active plans, agent guidance, review, handoff, baseline and engine-reference
notes with the implemented workflow and recorded 24-test/save/package evidence. Removed active
claims that automation was blocked, rollback had never executed, component offsets/material
overrides remained unfixed, all functionality was editor-only, or UE 5.5+ compatibility had been
established. Clarified ordinary zero-group imports and verification versus disk persistence in
the contract without changing accepted guarantees.

Added focused architecture, material, legacy, runtime/packaging and validation guides, plus nine
accepted ADRs covering lifecycle, identity, geometry/Nanite, lights, materials, persistence,
runtime boundaries, evidence and legacy tools. Preserved prior plans/reviews as clearly marked
historical snapshots with relocated links. Added project/plugin documentation entry points.

This was documentation-only: no C++ behavior or fixture source data changed and no Unreal build
or automation run was repeated. Local link/anchor checks, stale-guidance searches, and comparison
to the existing source hashes/logs validate the refresh. Release gates remain open as recorded
in ROADMAP_EXECUTION.md; ADR acceptance is not release acceptance.

## 2026-09-26 - Phase 2 persistence and recovery acceptance

Reproduced named-level Save As reporting success while a fresh-process reopen of the copied
map verified 0/1 groups. UE duplicates an already-saved world and changes actor GUIDs.
The commandlet now refuses that operation before import/copy; explicit save of a native copy
explains failed ownership proof and names the original owning map. No GUID or session guard
was weakened. Recovery diagnostics now include recorded destination/report/object paths
without claiming ownership or deleting objects.

Added three meaningful regression tests. The final UE 5.8.3 build and full 27-test suite pass,
exit 0 with zero controller errors. Real read-only-asset failure, bounded simulated map write
capacity failure, retry and save-with-manual-drift pass. A specifically launched disposable
process was terminated at a verified-but-uncommitted checkpoint; a fresh process reported
all 7 observed paths and left 4 saved evidence files byte-for-byte unchanged. A final original-map
reopen re-verified 1 group/2 instances without a new session; a fresh editor runtime-lookup
check found 2 source records and zero errors. Sources match their initial/provenance hashes.

Evidence and reproduction: Docs/Validation/2026-09-26-phase2.md and its JSON record. Native
Slate tools were unavailable, so live progress/cancellation and inspection remain explicitly
pending in the UI checklist. Content Browser rename/move, other crash windows and actual
full-volume behavior remain broader acceptance work. Runtime code was unchanged and no
new cook or rendered acceptance is claimed. Existing uncommitted work was preserved;
no commit or publication was performed. HANDOFF.md now points to live UI acceptance.

## 2026-09-27 - Documentation reconciliation and conversation closeout

Updated the handoff with completed code changes, exact evidence paths, retained interrupted
sessions and the next live UI assignment. Reconciled active host/plugin READMEs, plans,
architecture, ADR evidence sections, workflow/validation guides, source notes and fixture
cleanup guidance. Removed stale current 24-test and untested-interruption claims while
preserving historical execution records and original ADR dates.

The last executed baseline remains the 2026-09-26 build and 27/27 passing tests. Closeout
checks confirmed all 54 source hashes still match, the existing automation log has zero
controller errors, and all four retained interruption evidence files remain unchanged.
Checked local file links and Markdown anchors across host/plugin documentation, including
historical snapshots. Exact results are in
[closeout verification](../Validation/2026-09-27-docs.json).

This turn changed documentation and added a local documentation audit script only. No C++
or fixture data changed, no Unreal execution was repeated, and no commit/push/release was
made. Live UI, source-data, broader recovery, cooked/rendered and performance gates remain
explicitly open. See the [closeout record](../Validation/2026-09-27-closeout.md).

## 2026-09-27: preset state and Analyze cancellation

Completed the authorized fixes-and-automation pass. Shared input application invalidates old inspection/material-review/save targets for preset, browser and destination changes. Shared cooperative pre-mutation progress/cancellation includes streamed texture hashes and correct import sidecar cancellation status. Three new regressions pass; UE 5.8.3 build succeeded and full suite passed 30/30, exit 0, zero controller errors. [Evidence](../Validation/2026-09-27-preset-cancellation.md) records the 124-file checkpoint and current 55 source hashes. No commit, push, cook or live UI acceptance was performed. Native UI and representative-source gates remain pending.

## 2026-09-27: live native UI acceptance

Drove a disposable Unreal Editor with real mouse/text input through the guarded `NativeUI.ps1` helper (Slate accepts `SendInput`; `SendKeys` accelerators do not register). Small-source Analyze/import, per-instance and light focus, typed-path and preset invalidation, review-window reuse and closure, missing-target feedback, group-removal rebuild preview, and early cancellation on the 90 MB HVAC export passed. Translator-boundary cancellation is honored after return but cannot show feedback while Slate is frozen.

Found two defects. (1) Naming an untitled level through the editor's Save As also saved the manifest; UE's asset-path redirection rewrote its soft paths but left tracked-state text on `/Temp/`, so the explicit save reported false drift and a rebuild would have demanded manual-edit authorization. (2) Session restore set the effective source but left the source field empty; Analyze then read the hidden source. Fixed both (`RebaseTemporaryWorld` under GUID/session-tag proof; initial source text and restored status), clarified contract Amendment 7 and ADR 0006, and added `UnnamedMapFirstSaveStaysVerified` and `PanelSessionRestoreShowsInputs`. Both failed before the fixes, the map-and-manifest variant specifically with the rebase disabled. UE 5.8.3 build succeeded; full suite 32/32, exit 0, zero controller errors. The fixes have not been re-exercised live. No commit, push, cook or package. [Evidence](../Validation/2026-09-27-live-ui.md).

## 2026-09-27: texture search folders and missing-texture prompt

The user's Revit 2025 export (`ARCH.udatasmith`, SDK 5.3.0) failed Analyze because two referenced textures were not copied beside it. Added ordered texture search folders (default: Autodesk shared material library `Textures/1`, `2`, `3` `/Mats`, matching the tier-1 images Revit exports), resolved by case-insensitive file name onto the in-memory Datasmith element only. Resolutions enter plan identity; the folder list does not, so existing plan IDs are unchanged. Remaining missing textures get a panel Yes/No prompt (exact acceptance, cleared on source change) or the headless `-AllowMissingTextures`; missing meshes still fail. Contract Amendment 8. New test `MissingTexturesRequireExplicitAcceptance`. Build succeeded; 33/33, exit 0, zero controller errors. A headless analyze of `ARCH.udatasmith` resolved the bump map from the library and still stopped on `Window Keystone01.jpg` (absent from the machine). Not yet exercised in the live panel. No commit.

## 2026-09-28: ARCH live-import findings and verification fixes

A live panel import of `ARCH.udatasmith` in HISM mode confirmed HISM output: 298 groups (4,884 instances) all exact `HierarchicalInstancedStaticMeshComponent`, verified exact mesh, materials, counts, order, transforms, placement, settings. Import session failed verification outside of grouping and was accepted as degraded. Evidence: `Saved/DatasmithHISM/ImportReports/f266185c4bbb368035316d8b1fc7cf66.json`.

**Root causes identified and fixed in automation (38/38 tests, 37/37 Batch 1 + 38/38 Batch 2):**
- **Zero-byte dependencies (Amendment 9):** The 0-byte IES file `ARCH_Assets/generic` in ARCH_Assets caused 10 light verification failures. `IFileManager::FileSize<=0` now detects empty files; entries marked `" (empty file)"`; unresolved empty textures require explicit acceptance; empty meshes fail. Test `ZeroByteDependenciesAreMissing`.
- **Light verification field naming (Amendment 9):** Differing light fields (intensity, visibility, units, IES, brightness flag/scale) now named in diagnostic; lights with accepted-missing IES show outcome with WARN instead of FAIL. Test `LightVerificationNamesField`.
- **Source accounting exclusion (Amendment 11):** Unaccounted components now named in diagnostics; editor visualization meshes without Datasmith id excluded. Root cause of "actual=5506 accounted=5505": ARCH's single `<Camera>` becomes `ACineCameraActor`; its `UCameraComponent::OnRegister` creates `UCameraProxyMeshComponent` only in non-commandlet editors. Test `CameraProxyMeshIsNotSourceContent`.
- **Failed-verification summary (Amendment 6):** Summary now names every failing category (groups, lights, source accounting) at park/accept/rollback. Test `FailedVerificationSummaryNamesChecks`.
- **Nanite policy HISM coverage (Amendment 10):** "Converted ISM meshes only" policy now covers converted HISM groups; display name "Converted ISM/HISM Groups Only"; `ProcessMeshes` uses `IsA<UInstancedStaticMeshComponent>`; PlanId salt added only for HISM + ConvertedISMOnly so existing sessions are not `AlreadyCurrent` with stale output. Test `ConvertedGroupNaniteCoversHISM`.
- **Translator stage messaging (Amendment 2 clarification):** Status shows "Translating source; the editor may be unresponsive. Cancel takes effect when translation returns." with `FSlowTask::ForceRefresh` before blocking call. Suite passes; painting not observed live.

**Still needing live re-check (none done on 2026-09-28):** both 2026-09-27 fixes (false drift; hidden source); missing-texture prompt; empty-file prompt; accepted-missing-IES warning; pre-translation message painting; ARCH accounting with camera proxy recognized; remaining Phase 2 UI rows; cosmetic items.

Build: `Saved/Phase2Acceptance/20260928-Verification/08-full-suite-after-fix.txt` (37/37), then `20260928-NaniteTranslator/06-test-full-suite.log` (38/38). No commit, no live editor session.

## 2026-09-29: remediation Batches A, C and D

- **Batch A:** failed-verification acceptance now rolls the new attempt back when the previous session cannot be removed, and supersede path fallback requires the previous session's exact actor tag. Two real-entry regressions cover predecessor-removal failure and unrelated actor path reuse.
- **Batch C:** recursive texture-library discovery has its own cancellation phase, deterministic cooperative traversal, streamed hashes, and per-folder bounds of 250,000 files and 100,000 directories. Import-time approved-material fingerprinting uses the same progress object after mutation; cancellation routes through attempt rollback.
- **Batch D:** a selected texture-library resolution contributes its streamed content hash and byte size to `PlanId`. The folder list stays excluded. A same-path byte change crosses the real `ImportAndVerify` path and replaces the active result instead of reporting `AlreadyCurrent`.

UE 5.8.3 build and 36/36 automation passed. A Win64 cook and `ConVerse.ValidateImportedScene 3 RequireCollision RequireIES Exit` passed ([packaging evidence](../Validation/2026-09-29-packaging.md)).

## 2026-09-30: ARCH rollback timing and Amendment 12

A headless ARCH HISM run took 165.0 s and ended `ImportedWithFailuresRolledBack`. Log timestamps put about 136 s in rollback's single `ObjectTools::ForceDeleteObjects` over 3,298 packages; a CPU trace showed 6,596 `GatherObjectReferencersForDeletion` calls totalling 110.7 s. Amendment 12 replaced it with one batch reference check and `DeleteObjectsUnchecked`, falling back to `ForceDeleteObjects` on any outside referencer. The same run then took 43.5 s, with verify plus rollback at 7.1 s. 45/45 automation passed, including `RollbackExternalReferencerUsesCheckedDelete`. Datasmith 5.8 `FinalizeAssets` calls `BatchBuild` with no pre-build hook, so Nanite cannot be set before the first build through a supported seam. Committed as `c8ac53c`.

## 2026-10-01: Nanite crash, instrumentation and advisory

The Crusoe Mech import hit UE 5.8.3's fatal Nanite root-page ceiling (49,152 pages / 2,048 MB). Added per-step timings, `ConVerse_` Insights scopes, a per-operation progress log, per-mesh Nanite transition records (Amendment 13), and a projected Nanite mesh advisory above 16,384 (Amendment 14). Build and 45/45 automation passed; see [profiling evidence](../Validation/2026-10-01-import-profiling.md).

## 2026-10-01: documentation consolidation

Status now lives only in `HANDOFF.md#current-status`. `PLAN.md`, `NEXT_STEPS.md`, `ROADMAP_EXECUTION.md` and `REVIEW.md` were merged into `ROADMAP.md`; their dated checkpoints are the entries above. `IMPORT_PANEL_PLAN.md`, `IMPORT_RENDERING_PLAN.md` and `VALIDATION_BASELINE.md` were removed after their tables moved into the workflow and architecture docs. `Info.md` became `Docs/ENGINE_NOTES.md`, and this journal moved to `Docs/History/`. Earlier versions remain in Git history.

Resolved findings recorded in `REVIEW.md` at removal, each with regression coverage:

| Finding | Resolution |
|---|---|
| Preset/file-picker changes kept previous inspection and save state | Shared input application clears old associations and closes stale review windows |
| Analysis loops lacked cancellation; sidecar cancellation reported load failure | Shared cooperative checkpoints and streamed fingerprints; 17 cancellation cases |
| Material slot IDs lost after Datasmith sanitization | Numeric IDs recovered from mesh-description polygon groups |
| Failed-verification acceptance recorded Passed | Commit persists degraded/quarantined state |
| Rollback had never executed | Injected and obstructed rollback exercised |
| Automation supposedly blocked by optional SDKs | Disproved |
| Named-map Save As reported success for an unverifiable copy | Commandlet refusal before import/copy; explicit-save ownership proof |
| Recovery diagnostics omitted recorded object paths | Paths shown as unproven observations |
| Legacy overrides omitted; failed instance insertion deleted its source | Effective materials copied; failed insertion keeps the actor |
| Legacy behavior payload, below-threshold cleanup, component offsets, shared settings | Conservative eligibility, cleanup, world transforms, descriptor grouping |
| Nanite omitted ordinary/zero-group meshes | Owned-mesh policy independent of instancing |
| Manual edits could be silently replaced | Tracked comparison and explicit authorization |
| Generated material expression IDs caused false drift | Canonical comparison ignores expression IDs, detects value changes |
| Multiple active owners made replacement ambiguous | Real import dispatch blocks before mutation |

## 2026-10-01: opt-in import trace capture

Added the default-off **Profile next import** control. It captures one full-file Unreal Insights trace for Import/Rebuild, stops automatically, and records the path in the report; Analyze, presets, restored settings, and `PlanId` are unaffected. Existing active traces are left unchanged, and capture failures do not block import. The focused repeat-capture test and full 47-test suite passed after a UE 5.8.3 editor build. See [trace validation](../Validation/2026-10-01-trace-capture.md). Live Slate interaction and large-source capture remain unverified.
