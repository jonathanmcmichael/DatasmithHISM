# Tracked Revit import workflow

This development version targets UE 5.8.3. Import, recovery, and material review run in the editor. Imported output and `UConVerseSourceMetadata` can be cooked into Windows applications. Release acceptance remains open; see [the roadmap](../ROADMAP.md).

## Import and inspect

1. Keep the export beside its complete `_Assets` folder. Sources remain read-only. For Revit models with pipes, set the relevant view's **Detail Level** to **Fine** before exporting; **Low** detail can omit pipes from the Datasmith source, and the importer cannot recreate omitted geometry.
2. Choose the source, destination, ISM/HISM mode, minimum group size, and translator tessellation settings. **Profile next import** is optional and off by default; when checked, it is consumed by the next Import/Rebuild only.
3. Expand **Mesh, lighting, and material settings**. Imports preserve imported Nanite settings by default, so Datasmith builds each mesh once. Apply Nanite afterwards with the separate step (Amendment 16): use the panel's **Nanite (separate step)** row, or `-ApplyNanite=All|ISM` headless. Its scope choices are all supported meshes, **Converted ISM/HISM Groups Only** (Amendment 10), or **Recommended meshes** from the read-only **Analyze Nanite** ranking (Amendment 17); choosing one inline at import still works but rebuilds each changed mesh inside the import. Each Nanite mesh uses at least one streaming root page, and UE 5.8.3 crashes when its pool (49,152 pages, shared with loaded content) is exhausted. Analyze and Import report `Projected Nanite meshes (upper bound)` and warn above 16,384. Sources with many unique meshes, such as fabrication steel or MEP exports, should usually use converted groups only or Nanite exceptions.
4. Optionally run **Analyze** to preview the source and plan without creating assets or actors. The progress dialog opens before hashing and translation; cancellable item progress covers actors/lights, materials, textures and dependencies. Translators that cannot interrupt safely honor cancellation after returning. Analyze is not required before Import; running both repeats source and sidecar checks, which can take time on large exports.
5. If you analyzed, inspect source IDs, grouping results, ordinary meshes, material appearances, light diagnostics, and any rebuild preview. Search includes source/Revit identity. After import, selection can focus an individual instance or light and open mesh assets.
6. Run **Import and Verify**. It independently checks the source and dependencies before mutation; missing dependencies block import. Approved materials apply before Nanite checks. Mesh compilation finishes before verification. Every planned source mesh must resolve to an instance or ordinary mesh; missing/duplicate output fails verification.
7. **Verified, unsaved** means the result passed in memory. Save the owning level, then use **Save imported result** to save its assets, manifest, and level. Save failures remain explicit. Saving edited output does not accept a new verification baseline.

### Result states

| Situation | Presentation |
|---|---|
| Verified output in memory | Verified, unsaved |
| Explicit save succeeds | Saved; tracked drift or accepted failures remain degraded/unverified |
| Save fails | Incomplete save naming the affected package/map |
| Native named-map copy cannot prove ownership | Rebinding refused, with the original owning map and recovery instructions |
| Tracked edits before replacement | Changes listed; requires Replace with source or Cancel |
| AlreadyCurrent | Existing output reverified; drift and sidecar warnings reported separately |
| Failed verification | Rollback by default; explicit whole-session acceptance stays degraded/quarantined |
| Rollback failure or interrupted attempt | Actionable paths and recovery diagnostics; uncertain objects are not deleted |
| Older comparison data | Unrecorded fields are named; the manifest is not silently upgraded |

### Choosing ISM or HISM

Both output modes combine repeated uses of the same mesh into an instanced component. They reduce the number of separate mesh actors/components compared with keeping every source actor separate. They do not merge different meshes or materials into one component, and instancing does not guarantee a lower frame time: rendering cost still depends on visible instances, mesh sections/material slots, shadows, Nanite, and the view.

| Output | How it works | Usually a good fit | Tradeoffs |
| --- | --- | --- | --- |
| **ISM** (Instanced Static Mesh) | Keeps instances in a flat collection on an instanced component. It does not build HISM's spatial cluster hierarchy. | Smaller or tightly grouped sets; sets whose instances change often; cases where HISM's extra hierarchy does not help the camera views. | Does not get HISM's hierarchical cluster-culling benefit. The renderer still performs its normal instance visibility and rendering work. |
| **HISM** (Hierarchical Instanced Static Mesh) | Organizes instances into a spatial hierarchy so groups of instances can be culled together. | Large, mostly static sets spread through a scene, where camera views can reject many spatial clusters. Repetitive building elements are a reasonable candidate. | Building and maintaining the hierarchy costs time and memory. That overhead may not pay off for small/tight groups, views where nearly every instance is visible, or frequently changing sets. |

Both modes instance the geometry; HISM does not inherently mean fewer draw calls than ISM for the same mesh/material grouping. The practical difference is whether hierarchical culling saves more work than its tree's build, memory, and update costs. Nanite also performs its own fine-grained culling, so it can change how much benefit HISM adds; benchmark the actual combination rather than assuming either mode wins.

For a large static building, try HISM first when repeated elements are spatially distributed, but compare it with ISM using the same source, settings, and camera paths. Include close, distant, inside, and exterior views. Compare CPU and GPU frame time and memory, and account for import/build time separately from runtime performance. If the scene is compact or most instances remain visible, ISM may be as fast or faster with less hierarchy overhead.

In this plugin, both choices currently use Datasmith HISM scene elements as the import representation. When ISM is selected, the plugin validates and replaces the imported HISM components with ISM components during the initial import; when HISM is selected, they remain HISM. That conversion is an import-time cost, separate from which component type renders faster afterward.

There is not yet a representative ISM-versus-HISM runtime benchmark for this plugin's building-sized workloads. See the [roadmap](../ROADMAP.md) for the open camera-path CPU/GPU performance gate.

**Rebuild from source** bypasses the unchanged-plan shortcut. Detected tracked changes require an explicit replace-or-cancel decision. Headless runs require `-ReplaceManualEdits`. Settings that change generated output, approved mapping revisions, and mesh exceptions participate in plan identity. Light warning thresholds and catalog-only changes do not. Sidecar changes remain warning-only until explicit rebuild.

Import reports list wall time per stage and finer `Step` timings for sidecar enumeration/sizing/hashing, component conversion and mesh policy; compilation wait is reported separately. A `Step` time on a failed attempt is elapsed work, not proof that the step completed. Start an Unreal Insights CPU trace before importing to see the corresponding `ConVerse_` scopes; tracing cannot recover work that finished before capture began. A crash may leave only an incomplete attempt journal and the last `Import step started` log entry, with no final timing report. Preserve the journal and uncertain output for recovery inspection instead of deleting them.

To capture the next import, check **Profile next import** before Import or Rebuild. It writes a complete `.utrace` under the host project's `Saved/Profiling`, includes CPU, frame, bookmark, and log events, stops at operation completion, and adds the trace path to the result report. The trace path is not part of plan identity or saved settings. If another trace is already active, it is left unchanged; capture failures do not block the import. Alternatively, start a manual trace in the Unreal Editor console with `Trace.Send 127.0.0.1 cpu,frame,bookmark,log`; stop it afterward with `Trace.Stop` and open the session in Unreal Insights. Each stage start and completion is appended immediately to a per-operation file under `Saved/DatasmithHISM/ImportProgress`; the final report names that path. During mesh policy processing, an owned mesh whose Nanite setting changes also gets a flushed `Nanite setting change started` record before `PostEditChange()`, followed by a `returned` record and the callback duration. The `ConVerse_NanitePostEditChange` CPU scope isolates that callback in Insights. These records are emitted only for actual state changes and use one append handle for the mesh pass. A missing `returned` entry narrows the last unrecorded callback but does not prove it caused a crash; compilation may be asynchronous and the later batch compilation wait may fail after all callbacks returned. Progress-file failure does not change import verification or ownership.

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
| `-AllowMissingTextures` | Accept missing or empty textures (Amendments 8, 9); missing meshes still fail |
| `-TextureSearchFolders="C:/A;C:/B"` | Ordered texture search folders; an empty value disables the default Autodesk library search |
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
