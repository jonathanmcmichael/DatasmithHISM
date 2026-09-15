# DatasmithHISM Plugin

Unreal Engine 5 editor plugin for converting Datasmith-imported actor hierarchies into Instanced Static Mesh (ISM) components.

Reduces actor and draw-call count in imported scenes while preserving grouping by family, mesh, and materials. Nanite-compatible.

## Tools

Three toolbar buttons are added to the Level Editor:

### Dedupe Meshes
Scans the selected actors for geometrically identical static mesh assets, repoints all referencing components to a single canonical mesh per group, and deletes the duplicate assets. Does not change actor layout or create instances. Run this first on IFC/Datasmith imports where each placed instance imported as a separate mesh asset.

### Managed ISMs
Groups selected actors by family boundary, static mesh, and material set. Creates one `InstancedStaticMeshComponent` per group under a managed family-type actor, then removes the converted source actors and any empty hierarchy shells. Designed for Datasmith and Revit-style imported scenes.

**Recommended workflow for IFC imports with duplicate mesh assets:**
1. Select all — run **Dedupe Meshes**
2. Select all — run **Managed ISMs**

### Batch ISMs (Unreal)
Runs Unreal's built-in `MergeComponentsToInstances` path on the selection using `UInstancedStaticMeshComponent`. Faster and simpler than Managed ISMs but not family-hierarchy-aware.

## Dataprep Support

Both the HISM creation and mesh consolidation operations are exposed as Dataprep actions:

- `ConVerse Create ISM Operation`
- `ConVerse Consolidate Similar Meshes Operation`

## Blueprint API

All major operations are exposed as Blueprint-callable editor utilities:

- `UConVerseHISMLibrary::CreateHISMsFromSelection`
- `UConVerseBatchHISMLibrary::BatchSelectionToHISMs`
- `UConVerseStaticMeshConsolidationLibrary::ConsolidateSimilarStaticMeshes`
- `UConVersePowdercoatMaterialLibrary`

## Managed Output Tags

Objects created by Managed ISMs are identified with:

- `ConVerseManagedHISM` — on the ISM component
- `ConVerseManagedFamilyType` — on the family-type actor

Re-running on the same boundary replaces prior managed outputs for that boundary.

## Requirements

- Unreal Engine 5.5+
- Windows editor environment
- Dataprep plugin enabled for Dataprep operations

## Installation

Copy the `DatasmithHISM` folder into your project's `Plugins/` directory and rebuild.

## Notes

- Editor-only plugin.
- Each convertible source actor must represent exactly one mesh instance (actors with multiple eligible static mesh components are skipped by the managed path).
- `Dedupe Meshes` permanently deletes duplicate mesh assets — run it on a saved level or ensure source control is active.
