# DatasmithHISM — Work Journal

Chronological record of decisions, discoveries, and changes. Each entry covers one work session. For the current task list see PLAN.md; for user-facing documentation see README.md.

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

**Planning first:** [IMPORT_PANEL_PLAN.md](IMPORT_PANEL_PLAN.md) now defines five gated phases, the panel controls and states, atomic work units, file ownership, and acceptance checks. [IMPORT_PANEL_VALIDATION.md](IMPORT_PANEL_VALIDATION.md) freezes the immutable-plan, metadata-manifest, rollback, verification, and optimized-reimport contracts.

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

Updated the project-level [PLAN.md](PLAN.md), [README.md](README.md), and optimized-import plan so their status matches the implementation evidence: the UE 5.8.3 editor target builds, the generated-fixture test compiles, runtime automation remains blocked before editor startup by optional-platform SDK validation, and real Revit/Datasmith acceptance still requires a representative export and sidecar assets.

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
