> Historical snapshot before the 2026-09-26 documentation consolidation. Claims and task status below are historical, not current guidance. See the [current documentation index](../../README.md). Relative links have been adjusted for this archive.

# DatasmithHISM Plugin

Unreal Engine 5.8.3 plugin for tracked Datasmith import and ISM/HISM optimization. Import orchestration runs in the editor; imported scenes and source metadata can be cooked into Windows applications.

Reduces actor, component, and primitive overhead in AEC/BIM scenes. The primary workflow groups exact source mesh references with compatible settings under the same parent; family names and labels do not establish geometry equivalence. Legacy selection conversion remains a separate workflow.

Current development workflow: [import, materials, lighting, presets, and runtime usage](../../IMPORT_WORKFLOW.md). Implementation and remaining release gates: [execution ledger](../../../ROADMAP.md) and [validation evidence](../../Validation/2026-09-26.md).

> **Name note:** The plugin is named `DatasmithHISM` for historical reasons. It defaults to plain `UInstancedStaticMeshComponent` (ISM) — not `UHierarchicalInstancedStaticMeshComponent` (HISM) — because HISM's per-cluster occlusion culling is redundant and harmful for Nanite meshes. HISM is available via `bUseHISM = true` or auto-detected per-mesh with `bAutoDetectFromNanite = true`. The `ConVerse` prefix on all C++ classes is a project namespace.

## Optimized Datasmith import panel

Open **Tools > Optimized Datasmith Import** in the Unreal Editor. The panel accepts any source an enabled Datasmith translator can read — `.udatasmith`, plus CAD, Revit, and IFC formats — builds a read-only optimization plan, lets you choose ISM or HISM output, imports the transformed in-memory scene, and verifies every planned group before reporting success.

1. Keep the source file beside its complete exported `_Assets` sidecar folder. Both are fingerprinted, but sidecar changes are warning-only and do not alter plan identity or automatically trigger replacement.
2. Select the source file and a `/Game/...` destination folder.
3. Choose **ISM** for the default Nanite-oriented path or **HISM** when hierarchical instance culling is required.
4. Set the minimum number of matching actors per instance group. The default is 2.
5. For CAD-family sources, set the tessellation options (chord tolerance, normal tolerance, max edge length, stitching). These change the generated geometry and are part of the plan's identity.
6. Run **Analyze** to inspect counts and skip reasons without creating actors or assets.
7. Run **Import and Verify** to create a versioned import attempt, convert eligible groups, verify class, mesh, per-slot materials, settings, instance order, transforms, and coverage, then commit its ownership manifest.

If verification fails, the import is rolled back by default. You can opt in to reviewing the failure and accepting the output anyway; an accepted session is recorded as failed, marked degraded, and optimized reimport is refused against it.

Reports are written under `Saved/DatasmithHISM/ImportReports`, and one row per attempt is appended to an import log. A successful import stores source identity and instance alignment in asset user data, detaches the level actor from ordinary Datasmith synchronization, and marks owned assets so standard reimport is blocked. Re-importing the same source into the same destination is handled by the optimizer-aware path: an unchanged source short-circuits as already current, and a changed source supersedes the previous session.

The first implementation groups exact Datasmith mesh references under the same immediate parent. It leaves actors with children, unresolved references, invalid transforms, mirrored transforms, and groups below the selected threshold as ordinary Datasmith actors.

### Validation status

The UE 5.8.3 editor target builds clean and the full editor automation suite passes **24/24**
(`Automation RunTests DatasmithHISM`). Coverage includes end-to-end import against a generated
Datasmith fixture, rollback at two depths, `RollbackFailed` degradation, stock-reimport refusal,
optimizer-aware reimport, sidecar-change warnings, tessellation plan identity, schema refusal,
accept/discard of a failed verification, multi-material slot verification, import-wide Nanite,
Analyze cancellation, manual-edit conflicts, material approval invalidation, light units/advisories,
rebuild previews, legacy component placement/settings, and ambiguous active ownership.

An earlier note in this file claimed automation was blocked by the editor launcher's optional-platform
SDK validation. **That was wrong** - those LinuxArm64/VisionOS messages are harmless noise from a
`-Mode=ValidatePlatforms` subprocess. The suite runs.

Both the build and the suite require the Unreal Editor to be **closed**; Live Coding blocks external
builds. The latest automation run exits 0; inspect both the process result and the log's test results
and controller errors rather than inferring success from either one alone.

Still pending: broader validation against representative Revit, IFC, and CAD exports. The
multi-material path **has** now been confirmed on a real Revit dataset (2026-09-25): geometry that
previously failed verification on every multi-slot mesh imports and verifies cleanly. Sidecar-change
warnings and changed-source reimport remain proven only against generated fixtures. The current
roadmap also has passing save/reopen and Windows cook/runtime smoke evidence. Release acceptance
remains incomplete: the source joist payload lacks webbing, full exports have missing textures,
and calibrated lighting, reviewed catalog coverage, live UI, rendered and performance gates remain
open. See the linked execution ledger for the current scope and evidence limits.

## How it works

### The problem

Datasmith represents individually placed BIM elements as Unreal Actors. A building with 10,000 chairs produces thousands of Static Mesh Actors and components even when many of those objects share identical geometry. This creates substantial Actor, UObject, component, primitive, and scene-management overhead — and prevents Unreal's normal same-mesh batching and dynamic instancing from consolidating the geometry into instances.

On top of actor proliferation, some Datasmith and IFC imports go further: geometrically identical elements arrive as **separate `UStaticMesh` assets** rather than shared ones. This happens across MEP pipe fittings, structural members, lighting fixtures, site furniture, and chairs — any category where the authoring tool emitted one asset per placed instance rather than one asset per unique type.

Unreal's built-in Batch ISMs handles the first case partially — it can consolidate actors that already reference the **same asset**. It cannot help when identical geometry lives in separate assets, because it groups by mesh pointer equality, not by shape.

### The solution

Group by **geometry signature** instead of mesh pointer.

The signature is an MD5 hash of the LOD0 triangle data covering vertex positions (centroid-relative, so position-independent), normals, UVs (all channels), and material slot names — sorted by triangle hash so the result is order-independent. Two mesh assets with identical rendered geometry produce the same hash regardless of asset name, path, or import order.

**Grouping key** = `(FamilyTypeActor, GeometrySignature, MaterialSignature)`

- Same shape + same materials → one ISM, N instances
- Same shape + different effective materials → separate ISM groups. Current code does not apply source component material overrides to the new ISM; see [Info.md](Info.md).
- Unique shape with no duplicates → source actor left in place; a 1-instance ISM has higher overhead than the source component. The current grouping pass can still create an empty managed actor.

### What runs inside Managed ISMs

**Phase 1 — Grouping** (one pass over all selected actors):

For each actor, the plugin:
1. Finds its single eligible `UStaticMeshComponent` (skips actors with zero or multiple)
2. Walks the attach-parent chain to identify the **family wrapper** (nearest non-geometry ancestor — a Revit wrapper actor with no mesh of its own) and the **cleanup boundary** (the first non-geometry ancestor above the wrapper, or the immediate parent if there is no wrapper). The managed family label comes from the wrapper's name, or the mesh name if no wrapper exists.
3. Clears prior managed outputs on that boundary before building new groups. Reimport and partial-selection behavior still need validation; see [the development plan](PLAN.md).
4. Computes `GeometrySignature` (cached per mesh asset) and `MaterialSignature`
5. Accumulates into a group map keyed by `(FamilyTypeActor, GeometrySignature, MaterialSignature)`

**Phase 2 — Building** (one pass over the groups):

For each group meeting the minimum instance threshold (default 2):
1. Spawns or finds a managed family-type actor (tagged `ConVerseManagedFamilyType`) under the cleanup boundary
2. Creates a `UInstancedStaticMeshComponent` or `UHierarchicalInstancedStaticMeshComponent` (depending on `bUseHISM` / `bAutoDetectFromNanite`) tagged `ConVerseManagedHISM`
3. Uses the **canonical mesh** (alphabetically first asset path in the group — deterministic across reruns) as the ISM's mesh
4. Adds one instance per source actor at its world transform
5. Queues source actors and empty hierarchy shells for deletion

The entire operation is wrapped in a single `FScopedTransaction` — one Ctrl+Z undoes everything.

### The Revit family hierarchy

Revit families have a two-level structure:

```
[Family wrapper actor]       ← no geometry; identifies the family type
  └─ [Geometry actor]        ← has the UStaticMeshComponent
  └─ [Geometry actor]
  └─ ...
```

The plugin detects this by walking the attach-parent chain. Geometry actors at the bottom are grouped under a managed actor named after their wrapper. The **cleanup boundary** is the first non-geometry ancestor above that wrapper — the scope within which prior managed outputs are cleared on a rerun. Re-running after Datasmith reimport has not been validated yet.

### What you end up with

```
[Cleanup boundary actor]
  └─ [Managed family-type actor]  (tag: ConVerseManagedFamilyType)
       ├─ ISM_ChairBase_001        (tag: ConVerseManagedHISM, 847 instances, black finish)
       ├─ ISM_ChairBase_002        (tag: ConVerseManagedHISM, 312 instances, white finish)
       └─ ISM_ChairArm_003         (tag: ConVerseManagedHISM, 1159 instances)
```

An import with 10,000 source actors and three compatible geometry/material groups could become three instanced components representing 10,000 transforms. This is an illustrative target, not a measured result for the Aeron import. For Nanite scenes the expected gain is reduced Actor, UObject, component, and primitive overhead; actual rendering and memory gains require profiling on the imported scene.

ISM (not HISM) is the default because HISM's per-cluster culling adds overhead that Nanite already handles. Pass `bUseHISM = true` via the Blueprint API or Dataprep operation for non-Nanite meshes where hierarchical culling is beneficial.

### What Dedupe Meshes does

Dedupe Meshes is an optional pre-pass. It finds duplicate mesh assets by geometry signature, repoints references in the supplied objects to a canonical asset per group, then attempts to **permanently delete** the duplicates from the Content Browser. Review the current limitations below before using it.

You don't need to run it before Managed ISMs — geometry-signature grouping handles separate-asset duplicates at conversion time. Dedupe Meshes is useful when you want a clean Content Browser, or when you want to reduce asset count before other operations.

## Tools

The Level Editor toolbar provides the following tools:

### Dedupe Meshes

Scans the selected actors for geometrically identical static mesh assets, repoints components in the supplied selection to a canonical mesh per group, then attempts to delete the duplicate assets.

- Does not change actor layout or create instances
- Uses a stable MD5 hash of LOD0 source geometry for comparison — position-independent (centroid-relative), order-independent (sorted triangle hashes). Falls back to LOD1 if LOD0 source data is absent.
- Prompts for confirmation before permanently deleting assets
- Current implementation repoints component references before that confirmation. Declining deletion does not restore them. Use dry-run or disposable content until Phase 1 of [the development plan](PLAN.md) is verified.
- References outside the supplied selection are not repointed by this path. Avoid deleting assets used by other actors or maps until the Phase 1 reference-scope check is complete.

### Managed ISMs

Groups selected actors by family boundary, mesh geometry signature, and material set. Creates one `UInstancedStaticMeshComponent` per group under a managed family-type actor, then removes the converted source actors and any empty hierarchy shells.

Key behaviors:
- **Geometry-based grouping** — actors referencing separate mesh assets with identical geometry (same shape, same material slots, same UVs) are collapsed into one ISM. No pre-deduplication required.
- **Material variants** — actors with the same geometry but different effective materials produce separate groups. The current build path does not copy component material overrides to its new ISM, so appearance needs verification before converting source actors.
- **Grouping mode** — two modes via the `GroupingMode` parameter:
  - `PreserveBIMHierarchy` (default) — groups within Revit/IFC family-type boundaries; different families never collapse even if geometry is identical. Preserves BIM organization.
  - `MaximumOptimization` — ignores family identity; any actors under the same cleanup boundary with identical geometry and materials collapse into one ISM. Produces the smallest possible component count.
- **Minimum instance threshold** — groups below the threshold (default 2, configurable) are left as plain static mesh actors. A small-count ISM has higher overhead than the source components.
- **Family hierarchy** — Revit/IFC wrapper families are detected and respected. Multi-part families group under a managed actor named after the wrapper.
- **Reruns** — existing managed ISM outputs on a boundary are cleared before rebuilding. Use a complete source selection; reimport and partial-selection behavior need validation.
- **Undo** — the entire operation is wrapped in a single `FScopedTransaction`. Ctrl+Z restores all source actors and removes all created components in one step.
- **Cancel** — the progress dialog cancel button stops the operation cleanly. Any ISMs built before cancellation are committed; Ctrl+Z undoes the partial result if unwanted. No orphaned empty actors are left behind.

**Current workflow for IFC imports with duplicate mesh assets:** Select the intended source hierarchy, run **Analyze ISMs**, review its counts, then run **Managed ISMs** on a disposable copy of the level. Geometry-based grouping does not require the Dedupe pre-pass. Dedupe now skips and reports duplicate assets with loaded-component or on-disk package references outside the supplied context; validate the workflow on disposable content before using it in production.

### Analyze ISMs

Dry-run counterpart to Managed ISMs. Runs the grouping phase on the selection and reports how many ISM groups would be created, how many actors would be converted, and how many would be left in place — without making any changes to the level.

Use this before running Managed ISMs to understand the expected reduction before committing.

### Explode ISMs

Reverse of Managed ISMs. Finds all `ConVerseManagedHISM`-tagged components in the selection and its descendant hierarchy. For each ISM component, spawns one `AActor` with a `UStaticMeshComponent` per instance at the stored world transform, copying the mesh and all material assignments. Destroys the ISM components and any now-empty managed family-type actors.

The operation stages and validates every actor before it removes the source component. If any actor cannot be recreated, it destroys the staged actors and retains the source ISM/HISM component.

The entire operation is wrapped in a single `FScopedTransaction` — one Ctrl+Z undoes everything.

### Dedupe + ISMs

One-click pipeline. Runs Dedupe Meshes then Managed ISMs in sequence on the selection. Dedupe prompts before it changes mesh references or deletes assets. Declining the prompt leaves the selection unchanged, and a declined or failed Dedupe operation stops the pipeline before Managed ISMs run.

Before deletion, Dedupe audits loaded `UStaticMeshComponent` references and on-disk Asset Registry package referencers. It only replaces and deletes a duplicate when every detected reference is inside the supplied toolbar or Dataprep context; otherwise it skips and reports the duplicate. Manual editor validation of selected-only, external-reference, unloaded-package, and asset-only cases remains pending.

### Enable Nanite

Enables Nanite on all static mesh assets referenced by the current selection. Sets `NaniteSettings.bEnabled = true` on each asset and queues asynchronous rebuilds. Wrapped in a transaction.

Use this to prepare non-Nanite Datasmith imports before running Managed ISMs, so the auto-ISM/HISM detection (`bAutoDetectFromNanite`) correctly selects ISM for all converted meshes.

### Batch ISMs (Unreal)

Runs Unreal's built-in `MergeComponentsToInstances` path on the selection using `UInstancedStaticMeshComponent`. Faster and simpler than Managed ISMs but not family-hierarchy-aware or geometry-signature-aware.

## Dataprep Support

Four operations are exposed as Dataprep actions:

- `ConVerse Create ISM Operation` — runs Managed ISMs logic in a Dataprep pipeline (Prefix, bUseHISM, bAutoDetectFromNanite, MinInstanceCount, GroupingMode)
- `ConVerse Analyze ISM Candidates` — dry-run counterpart; same parameters as Create ISM; logs a grouping report without making any changes; useful as a pipeline pre-flight step
- `ConVerse Create ISMs By Category` — filters root actors by a case-insensitive label substring, then processes their subtrees
- `ConVerse Consolidate Similar Meshes Operation` — runs Dedupe Meshes logic in a Dataprep pipeline (no confirmation dialog in Dataprep context)

## Blueprint API

All major operations are Blueprint-callable editor utilities:

- `UConVerseHISMLibrary::CreateISMsFromSelection(Prefix, bUseHISM, MinInstanceCount, bAutoDetectFromNanite, GroupingMode)` — Managed ISMs (canonical)
- `UConVerseHISMLibrary::AnalyzeISMCandidatesInSelection(Prefix, bUseHISM, MinInstanceCount, bAutoDetectFromNanite, GroupingMode)` — dry-run; same parameters; no changes
- `UConVerseHISMLibrary::ExplodeISMsFromSelection` — reverse of Managed ISMs; spawns individual actors from ISM instances
- `UConVerseHISMLibrary::EnableNaniteOnSelection` — enables Nanite on all mesh assets referenced by selection
- `UConVerseHISMLibrary::MigrateTagsInCurrentLevel(OldTagName, NewTagName)` — level-wide tag replacement
- `UConVerseHISMLibrary::CreateHISMsFromSelection` — **deprecated**; delegates to `CreateISMsFromSelection`
- `UConVerseBatchHISMLibrary::BatchSelectionToHISMs` — Batch ISMs
- `UConVerseStaticMeshConsolidationLibrary::ConsolidateSimilarStaticMeshes(Objects, bRequireMatchingMaterials, bDryRun)` — Dedupe Meshes; `bDryRun = true` reports what would happen without deleting
- `UConVerseStaticMeshConsolidationWidget` — Editor Utility Widget base class; subclass in UMG to build custom UI around the Dedupe Meshes operation
- `UConVersePowdercoatMaterialLibrary::CreatePowdercoatSubstrateMaterial` — procedurally creates a [Substrate](https://dev.epicgames.com/documentation/en-us/unreal-engine/substrate-materials-in-unreal-engine)-based powdercoat material asset with configurable color, orange-peel amount/scale, clearcoat, and thickness

## Managed Output Tags

Objects created by Managed ISMs are identified with component/actor tags:

- `ConVerseManagedHISM` — on the ISM component (tag name preserved from original HISM release for backward compatibility with existing levels)
- `ConVerseManagedFamilyType` — on the managed family-type actor

Re-running on the same boundary detects these tags and replaces prior managed outputs before rebuilding.

## Result Fields

`FConVerseHISMCreationResult` (returned by Blueprint API and logged to Output Log):

| Field | Description |
|---|---|
| `ActorsConsidered` | Total actors passed in |
| `ISMComponentsCreated` | Number of ISM/HISM components created |
| `ISMOnlyComponentsCreated` / `HISMOnlyComponentsCreated` | Count by actual component class |
| `SourceActorsConverted` | Source actors folded into ISMs |
| `ActorsInSingleActorGroups` | Actors left in place (below minimum instance threshold) |
| `SkippedActors` | Actors with no or multiple eligible mesh components |
| `FailedISMComponentCreations` | ISM creation failures |
| `FailedSourceActorDeletes` | Source actor delete failures |
| `bWasCancelled` | True if the user cancelled via the progress dialog |
| `Summary` | Human-readable summary string |

## Requirements

- Unreal Engine 5.5+
- Windows editor environment
- Dataprep plugin enabled for Dataprep operations

## Installation

Copy the `DatasmithHISM` folder into your project's `Plugins/` directory and rebuild.

## Notes

- Editor-only plugin.
- Each convertible source actor must represent exactly one mesh instance — actors with multiple eligible static mesh components are skipped by the Managed ISMs path.
- **Dedupe Meshes permanently deletes duplicate mesh assets.** Run it on a saved level or with source control active. A confirmation dialog is shown before any deletion.
- ISM is the default for Nanite compatibility. HISM is available via `bUseHISM = true` or auto-selected per mesh via `bAutoDetectFromNanite = true` (ISM for Nanite meshes, HISM for non-Nanite). HISM's per-cluster occlusion culling is redundant and harmful with Nanite but beneficial for large non-Nanite populations.

## License

MIT — see [LICENSE](../../../LICENSE).
