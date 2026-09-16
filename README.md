# DatasmithHISM Plugin

Unreal Engine 5 editor plugin for converting Datasmith-imported actor hierarchies into Instanced Static Mesh (ISM) components.

Reduces actor and draw-call count in AEC/BIM scenes while preserving grouping by family, mesh geometry, and materials. Nanite-compatible.

> **Name note:** The plugin is named `DatasmithHISM` for historical reasons. It uses plain `UInstancedStaticMeshComponent` (ISM) throughout — not `UHierarchicalInstancedStaticMeshComponent` (HISM) — because HISM's per-cluster occlusion culling is redundant and harmful for Nanite meshes. The `ConVerse` prefix on all C++ classes is a project namespace.

## Tools

Three toolbar buttons are added to the Level Editor:

### Dedupe Meshes

Scans the selected actors for geometrically identical static mesh assets, repoints all referencing components to a single canonical mesh per group, then deletes the duplicate assets.

- Does not change actor layout or create instances
- Uses a stable MD5 hash of LOD0 source geometry for comparison — position-independent (centroid-relative), order-independent (sorted triangle hashes)
- Prompts for confirmation before permanently deleting assets
- Run this first on IFC/Datasmith imports where each placed instance imported as a separate mesh asset, to reduce Content Browser clutter before running Managed ISMs

### Managed ISMs

Groups selected actors by family boundary, mesh geometry signature, and material set. Creates one `UInstancedStaticMeshComponent` per group under a managed family-type actor, then removes the converted source actors and any empty hierarchy shells.

Key behaviors:
- **Geometry-based grouping** — actors referencing separate mesh assets with identical geometry (same shape, same material slots, same UVs) are collapsed into one ISM. No pre-deduplication required.
- **Material variants** — actors with the same geometry but different assigned materials produce separate ISM components, preserving each colour/finish variant.
- **Single-instance actors** — actors whose geometry is unique (no duplicates in the selection) are left in place as plain static mesh actors. A 1-instance ISM has higher overhead than the source component.
- **Family hierarchy** — Revit/IFC wrapper families are detected and respected. Multi-part families group under a managed actor named after the wrapper.
- **Rerun-safe** — existing managed ISM outputs on a boundary are cleared before rebuilding, so re-running on the same selection replaces prior results cleanly.
- **Undo** — the entire operation is wrapped in a single `FScopedTransaction`. Ctrl+Z restores all source actors and removes all created components in one step.

**Recommended workflow for IFC imports with duplicate mesh assets:**
1. Select all — run **Dedupe Meshes** (optional; cleans up Content Browser)
2. Select all — run **Managed ISMs**

Or just run **Managed ISMs** directly — geometry-based grouping handles duplicates at conversion time without needing a separate dedup pass.

### Batch ISMs (Unreal)

Runs Unreal's built-in `MergeComponentsToInstances` path on the selection using `UInstancedStaticMeshComponent`. Faster and simpler than Managed ISMs but not family-hierarchy-aware or geometry-signature-aware.

## Dataprep Support

Both operations are exposed as Dataprep actions:

- `ConVerse Create ISM Operation` — runs Managed ISMs logic in a Dataprep pipeline
- `ConVerse Consolidate Similar Meshes Operation` — runs Dedupe Meshes logic in a Dataprep pipeline (no confirmation dialog in Dataprep context)

## Blueprint API

All major operations are Blueprint-callable editor utilities:

- `UConVerseHISMLibrary::CreateHISMsFromSelection` — Managed ISMs
- `UConVerseBatchHISMLibrary::BatchSelectionToHISMs` — Batch ISMs
- `UConVerseStaticMeshConsolidationLibrary::ConsolidateSimilarStaticMeshes` — Dedupe Meshes
- `UConVersePowdercoatMaterialLibrary` — material utility functions

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
| `ISMComponentsCreated` | Number of ISM components created |
| `SourceActorsConverted` | Source actors folded into ISMs |
| `ActorsInSingleActorGroups` | Actors left in place (unique geometry, no instancing benefit) |
| `SkippedActors` | Actors with no or multiple eligible mesh components |
| `FailedISMComponentCreations` | ISM creation failures |
| `FailedSourceActorDeletes` | Source actor delete failures |
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
- ISM (not HISM) is used throughout for Nanite compatibility. HISM adds per-cluster occlusion culling overhead that is redundant and harmful with Nanite meshes.

## License

MIT — see [LICENSE](LICENSE).
