# AGENTS.md — DatasmithHISM Plugin

Guidance for AI coding agents working in this repository.

---

## Project purpose

Editor-only UE5 plugin. Converts Datasmith/IFC-imported actor hierarchies into `UInstancedStaticMeshComponent` (ISM) components to reduce actor count and draw calls in AEC/BIM scenes. Nanite-compatible.

Core conversion operations:

| Button | Entry point | What it does |
|---|---|---|
| Dedupe Meshes | `UConVerseStaticMeshConsolidationLibrary::ConsolidateSimilarStaticMeshes` | Repoints components from duplicate mesh assets to one canonical asset per geometry group, then deletes duplicates |
| Managed ISMs | `UConVerseHISMLibrary::CreateHISMsFromSelection` | Groups actors by family boundary + geometry signature + material set; creates one ISM component per group under a managed family-type actor |
| Batch ISMs | `UConVerseBatchHISMLibrary::BatchSelectionToHISMs` | Thin wrapper around Unreal's built-in `MergeComponentsToInstances`; no family-hierarchy awareness |

The toolbar also exposes Analyze ISMs, Enable Nanite, Explode ISMs, Dedupe + ISMs, and a Use HISM toggle. Four Dataprep actions exist: Create, Analyze, Create By Category, and Consolidate Similar Meshes. Check the current source before extending these paths.

---

## Architecture

```
DatasmithHISM.cpp                    ← Module startup; toolbar/menu wiring; RunXxx() handlers
  ├─ ConVerseHISMLibrary.cpp         ← Blueprint entry for Managed ISMs; FScopedTransaction here
  │    └─ ConVerseHISMUtils.cpp      ← BuildManagedHISMs(); grouping + ISM creation; FScopedSlowTask here
  │         └─ ConVerseStaticMeshConsolidationUtils.cpp  ← GetMeshGeometrySignature() (LOD0 MD5)
  ├─ ConVerseStaticMeshConsolidationLibrary.cpp  ← Dedupe Meshes entry; confirmation dialog here
  │    └─ ConVerseStaticMeshConsolidationUtils.cpp
  ├─ ConVerseBatchHISMLibrary.cpp    ← Batch ISMs entry
  └─ Dataprep/
       ├─ ConVerseCreateHISMOperation.cpp         ← Dataprep wrapper for BuildManagedHISMs
       └─ ConVerseConsolidateSimilarMeshesOperation.cpp  ← Dataprep wrapper for Dedupe Meshes
```

**Grouping key** (`FHISMGroupKey` in `ConVerseHISMUtils.cpp`):
- `FamilyTypeActor` — the managed actor that will own the ISM component
- `GeometrySignature` — MD5 of sorted LOD0 triangle hashes (centroid-relative position, normal, UVs, material slot names); falls back to asset path for unhashable meshes
- `MaterialSignature` — ordered `GetMaterial(index)->GetPathName()` per slot; keeps colour/finish variants as separate ISMs

The key does not yet include other component settings copied from the first source. Phase 2 of `PLAN.md` covers this correctness gap.

**Canonical mesh** — alphabetically first asset path among all geometrically equivalent meshes in a group; deterministic across reruns.

---

## Code conventions

- **Class prefix** — all plugin C++ classes use `ConVerse` (project namespace). Do not drop it or rename to a different prefix.
- **ISM default, HISM optional** — use `UInstancedStaticMeshComponent` by default for Nanite. `bUseHISM` explicitly selects `UHierarchicalInstancedStaticMeshComponent`; `bAutoDetectFromNanite` selects per canonical mesh. Keep both supported and keep the default ISM behavior.
- **Transaction placement** — `FScopedTransaction` lives at the library layer (`ConVerseHISMLibrary.cpp`), not inside the utility functions. `BuildManagedHISMs` is transaction-neutral so Dataprep can wrap it differently.
- **Progress dialogs** — `FScopedSlowTask` lives inside `ConVerseHISMUtils.cpp::BuildManagedHISMs`. Actor grouping and ISM building both check `ShouldCancel()`. Cancellation keeps the partial result in the undo transaction; test this in-editor before changing it.
- **Dedupe dialog suppression** — `FMessageDialog::Open(YesNo)` before `ObjectTools::DeleteObjects` is suppressed when `IsRunningCommandlet()` is true, so Dataprep pipelines don't hang.
- **Managed output tags** — `ConVerseManagedHISM` (on ISM components) and `ConVerseManagedFamilyType` (on managed actors). These names are preserved for backward compatibility with existing levels. Do not change them.
- **Minimum instance count** — groups smaller than `MinInstanceCount` are skipped; the default is two. Their actors are counted in `ActorsInSingleActorGroups` and left untouched.
- **`LOCTEXT_NAMESPACE`** — every `.cpp` that uses `LOCTEXT()` defines and undefines the namespace at the top/bottom. Follow this pattern for any new `.cpp` files.
- **`RF_Transactional`** — all `NewObject` calls for components that should participate in undo use this flag. Don't omit it.

---

## Key files and their responsibilities

| File | Responsibility |
|---|---|
| `Private/ConVerseHISMUtils.cpp` | Core grouping and ISM creation algorithm. All structural changes to how actors are grouped or ISMs are built go here. |
| `Private/ConVerseHISMUtils.h` | Declares `BuildManagedHISMs`, `CollectActorsFromRoots`, `FinalizeSummary`. Keep the public surface minimal. |
| `Public/ConVerseHISMLibrary.h` | Declares `FConVerseHISMCreationResult` (Blueprint-visible struct) and `UConVerseHISMLibrary`. Field renames here are Blueprint-breaking changes. |
| `Private/ConVerseHISMLibrary.cpp` | Thin entry: collects selection, calls `BuildManagedHISMs`, logs, shows result dialog. `FScopedTransaction` is here. |
| `Private/ConVerseStaticMeshConsolidationUtils.cpp` | `GetMeshGeometrySignature()` — the LOD0 MD5 hash. Changes here affect both Dedupe Meshes and Managed ISMs grouping. |
| `Private/DatasmithHISM.cpp` | Module init + toolbar/menu registration. Only touches this for new toolbar buttons or menu items. |
| `Public/Dataprep/ConVerseCreateHISMOperation.h/.cpp` | Dataprep `UDataprepOperation` wrapper. Must not show any interactive dialogs. |

---

## What to avoid

- Do not change the default to HISM or remove the explicit HISM and auto-detect options without an API migration.
- Do not add confirmation dialogs inside Dataprep operation classes (`ConVerseCreateHISMOperation`, `ConVerseConsolidateSimilarMeshesOperation`). Use `GIsEditor && !IsRunningCommandlet()` guards when adding dialogs anywhere.
- Do not move the `FScopedTransaction` into `ConVerseHISMUtils.cpp`. It must stay at the library layer so the utility is usable headlessly.
- Do not change the `ConVerseManagedHISM` or `ConVerseManagedFamilyType` tag strings. Existing levels depend on them for rerun detection.
- Do not rename `CreateHISMsFromSelection` on the Blueprint API without adding a deprecated redirect — it is a public, Blueprint-callable function name.
- Do not add `bRequireMatchingMaterials = false` as a default behavior in `ConsolidateSimilarStaticMeshes` — the current default (`true`) is intentional to avoid merging intentionally different-material meshes.

---

## Build

This is a UE5 plugin (`.uplugin` + `Build.cs`). Build via:

```
UnrealBuildTool AdvancedHISMEditor Win64 Development -Project="D:\Unreal\Sandbox\AdvancedHISM\AdvancedHISM.uproject"
```

Or open the `.uproject` in the editor and let UE rebuild on load. There are no standalone unit tests; validation is done by running the operations in-editor against real Datasmith/IFC imports.

**Module dependencies** (from `DatasmithHISM.Build.cs`):

| Scope | Modules |
|---|---|
| Public | `Core`, `CoreUObject`, `DataprepCore`, `Engine` |
| Private | `AssetTools`, `Blutility`, `EditorFramework`, `InputCore`, `MaterialEditor`, `MeshDescription`, `MeshMergeUtilities`, `Projects`, `Slate`, `SlateCore`, `StaticMeshDescription`, `ToolMenus`, `UMG`, `UnrealEd` |

---

## Next work (from PLAN.md)

Follow the five-phase execution plan in `PLAN.md`. Analyze, Enable Nanite, Explode, cancellation, and HISM selection are implemented but not validated against a real IFC/Datasmith map. Current high-priority gaps are Dedupe's reference changes before confirmation, Explode's removal of an ISM after a spawn failure, component placement and setting equivalence, and safe reimport handling. Do not infer a successful build or real-data pass from the journal's implementation entries.

---

## Domain context

- **IFC/Datasmith imports** often produce one `UStaticMesh` asset per placed instance even when geometry is identical (e.g., 10,000 Herman Miller Aeron chair actors each with their own mesh asset). The geometry signature hash is the mechanism that collapses these into a small number of ISMs regardless of asset count.
- **Family hierarchy** — Revit/IFC wrapper families are non-geometry ancestors (no eligible SMC) that group their geometry children. `FindHostedFamilyWrapperActor` and `FindCleanupBoundaryActor` walk the attach-parent chain to detect this structure.
- **Cleanup boundary** — the first non-geometry ancestor above a geometry actor (or above its hosted-family wrapper). Prior managed outputs are cleared at this level before rebuilding, so re-running on the same selection replaces stale results cleanly.
