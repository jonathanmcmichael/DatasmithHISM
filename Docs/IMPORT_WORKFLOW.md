# Tracked Revit import workflow

This development version targets UE 5.8.3. Import, recovery, and material review run in the editor. Imported output and `UConVerseSourceMetadata` can be cooked into Windows applications. Release acceptance remains open; see [the execution ledger](../ROADMAP_EXECUTION.md).

## Import and inspect

1. Keep the export beside its complete `_Assets` folder. Sources remain read-only.
2. Choose the source, destination, ISM/HISM mode, minimum group size, and translator tessellation settings.
3. Expand **Mesh, lighting, and material settings**. Nanite defaults to all supported imported mesh assets, including ordinary meshes and zero-group imports. Choose **Converted ISM/HISM Groups Only** (Amendment 10) or preserve imported settings when needed.
4. Run **Analyze**. The progress dialog opens before hashing and translation. It shows controllable item progress and named stages, including actor/light analysis, materials, texture fingerprinting and dependency checks; translators that cannot interrupt safely honor cancellation after returning. Missing dependencies block import. No assets or actors are created by Analyze.
5. Inspect source IDs, grouping results, ordinary meshes, material appearances, light diagnostics, and any rebuild preview. Search includes source/Revit identity. After import, selection can focus an individual instance or light and open mesh assets.
6. Run **Import and Verify**. Approved materials apply before Nanite checks. Mesh compilation finishes before verification. Every planned source mesh must resolve to an instance or ordinary mesh; missing/duplicate output fails verification.
7. **Verified, unsaved** means the result passed in memory. Save the owning level, then use **Save imported result** to save its assets, manifest, and level. Save failures remain explicit. Saving edited output does not accept a new verification baseline.

**Rebuild from source** bypasses the unchanged-plan shortcut. Detected tracked changes require an explicit replace-or-cancel decision. Headless runs require `-ReplaceManualEdits`. Settings that change generated output, approved mapping revisions, and mesh exceptions participate in plan identity. Light warning thresholds and catalog-only changes do not. Sidecar changes remain warning-only until explicit rebuild.

**Keep as ordinary** and **Disable Nanite** target exact source mesh element IDs. Shared uses of an owned mesh asset share its Nanite state. Unmatched exceptions are reported when the source changes. Save a named JSON preset to retain complete settings; the panel also restores its last executed session settings without importing automatically. Switching source or destination through typing, browsing or a preset clears previous result/save associations and closes the old material-review window. A same-source/destination preset retains the imported-result association but requires Analyze for its new settings.

Scene-component roots remain where they carry attachment or transform relationships. They do not render geometry. Removing them indiscriminately can change placements, including linked/nested elements and lights.

## Material catalog and approval

Use **Review materials** after Analyze or import. The review shows appearance fingerprints, exported properties, catalog candidates, usage, and approval state. Imported source materials and candidate replacement materials can be opened in their native material editors.

- **Create project tables** creates separate appearance and approval DataTables under `/Game/DatasmithImportSettings`, outside attempt rollback ownership. Existing selected tables are preserved.
- Import [ObservedAppearanceCatalog.csv](../Tests/Fixtures/ObservedAppearanceCatalog.csv) into a `ConVerseAppearanceCatalogRow` DataTable. It contains 245 observed variants from the available exports, with source and version provenance. It is not a verified Autodesk stock-library catalog; its fingerprints are deliberately blank until reviewed against available appearance evidence.
- A catalog entry does not approve a replacement. Names/aliases only suggest candidates. **Add observed appearance to catalog** records evidence without claiming stock-library identity.
- Choose one catalog ID and a replacement material, then **Approve this variant**. Approval binds to the exact appearance fingerprint. Changed appearances and missing texture evidence cannot inherit an approval by name. A missing approved target blocks application.
- Save both DataTable assets. Enable **Apply Approved Materials**, save the import preset, Analyze, and explicitly rebuild. **Revoke approval** changes the mapping table; it does not silently edit an existing import.
- DataTable editors provide CSV import/export. A replacement table uses `ConVerseMaterialMappingRow` and links to catalog entries through `CatalogId`.

Revit instance fingerprints include typed appearance properties and texture content evidence. This includes exported UV transforms. Non-Revit material graphs conservatively include all scene texture content; unrelated texture changes can require renewed approval. Imported assignments and original material paths remain recorded for inspection and rebuilding.

## Lighting

The initial advisory threshold is 100 enabled local lights. The report includes source light counts, Unitless data, IES usage, and observed project MegaLights configuration. **Rendering settings** navigates to the project settings; the importer does not change rendering configuration.

Physical units, intensity, enabled state, and IES settings are checked against Datasmith source values. Unitless values remain Unitless and receive an unresolved calibration diagnostic. This validates source preservation, not physical agreement with an unavailable Revit photometric reference.

## Automation and runtime

Use `-run=ConVerseOptimizedImport` with the normal source/destination/instancing/tessellation arguments. Additional switches:

| Switch | Behavior |
|---|---|
| `-Nanite=All\|ISM\|Preserve` | Independent mesh policy |
| `-KeepOrdinary=MeshIdA,MeshIdB` | Exact source mesh exceptions |
| `-DisableNanite=MeshIdA,MeshIdB` | Disable Nanite for those owned assets |
| `-ManyLightThreshold=100` | Advisory only |
| `-Preset="path.json"` | Load saved settings; explicit CLI values override them |
| `-Rebuild` | Rebuild even when primary source and plan are unchanged |
| `-ReplaceManualEdits` | Explicitly allow replacing detected tracked edits |
| `-LoadMap=/Game/Maps/Scene` | Load the owning map before verification/rebuild |
| `-NewMap=/Game/Maps/NewScene` | Create a new map before import; refuses existing paths |
| `-SaveAsMap=/Game/Maps/NewScene` | Initially name an unsaved map after import; refuses existing paths and already-saved map copies before import |
| `-Save` | Save the imported result and owning map; fail on incomplete saves |
| `-GeometryDirectory="path"` | Export imported LOD0 triangle/vertex OBJ evidence and inventory |
| `-InspectMesh="file.udsmesh" -GeometryDirectory="path"` | Inspect source payload without import; no `-Source` required |

`-Source` remains required with `-Preset`. Analysis with zero instancing groups can succeed. Automated runs show no dialogs; degraded, cancelled, blocked, failed, or drifted results do not return success.

UE 5.8 Save As of a level already saved on disk creates a new world with changed actor GUIDs. The commandlet refuses this operation before importing or copying; continue with the original `-LoadMap` and `-Save`. For independent output, choose a fresh `-NewMap` and destination. A copy created through native editor controls also cannot silently acquire the original manifest's ownership; explicit save explains the refusal when GUID/session proof fails.

Blueprint/runtime callers can use `UConVerseSourceMetadata::FindSource(Component, InstanceIndex, Record)`. Use the instance index for ISM/HISM; use `INDEX_NONE` for ordinary meshes and lights. Records include document identity, source identity, labels, and exported metadata.

Development packaged builds expose `ConVerse.ValidateImportedScene <expected-source-count> [RequireCollision] [RequireIES] [Exit]`. It checks runtime lookup, mesh/material references, optional collision traces, and optional light/IES presence. It does not replace rendered appearance or representative performance acceptance.

Attempt checkpoints live in `Saved/DatasmithHISM/Attempts`. Interrupted/unreadable attempts are reported with known destination, report and observed object paths, without loading or deleting unknown objects. Recorded paths are diagnostic observations, not proof of current ownership. Reports and import logs remain under `Saved/DatasmithHISM`. Analyze lists retained superseded versions and saved package/bulk sizes; no automatic cleanup runs.


## Related references and evidence limits

See the [material schema/review guide](MATERIALS.md), [legacy tool boundary](LEGACY_TOOLS.md), [runtime and packaging guide](RUNTIME_AND_PACKAGING.md), [validation procedure](VALIDATION.md), and [ADRs](ADR/README.md). Zero-group import and source-preserving light checks are implemented; physical Revit calibration and broad live UI/rendered acceptance remain open.

MegaLights reporting observes `r.MegaLights.EnableForProject`; it does not establish effective platform, volume, scalability, or per-light support. Presets retain table references, so moving a preset alone does not transfer its catalog, mapping or target assets. Save success does not silently accept a new tracked baseline.
