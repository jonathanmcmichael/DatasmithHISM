# Unreal and Datasmith source notes

External guidance reviewed 2026-09-26 against project source and Epic's UE 5.8 documentation; project evidence reconciled 2026-09-27. External guidance below is separate from implementation decisions and executed evidence. Earlier findings are [archived](History/2026-09-26/Info.md).

## Instancing and Nanite

An ISM references one mesh asset and shares material/collision/render settings across instances. ISM supports per-instance LOD; HISM adds a hierarchy whose benefit depends on the scene. Epic recommends ISM for a Nanite-only workflow and considers HISM for appropriate fallback/non-Nanite cases. Both types can reference Nanite meshes. [Epic: ISM and HISM](https://dev.epicgames.com/documentation/en-us/unreal-engine/instanced-static-mesh-component-in-unreal-engine)

Nanite's documented material support includes Opaque and Masked; unsupported material types and platform/render-path limitations require checking. Asset enablement alone does not prove correct appearance. [Epic: Nanite](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine)

**Project decision:** default tracked imports to ISM and independent import-wide Nanite processing, with exceptions and verification. Do not describe HISM as categorically harmful. The legacy selection Enable Nanite utility has a narrower validation contract than the tracked processing pass. See [ADR 0003](ADR/0003-geometry-and-nanite.md).

## Revit export and source evidence

Revit's active 3D view determines visible exported geometry; section boxes can cut it. Revit supplies mesh tessellation. Exported hierarchy and metadata can include levels, hosts, base points and survey points. Epic documents preservation of Revit light intensity units. [Epic: Datasmith with Revit](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-datasmith-with-revit-in-unreal-engine)

**Observed in this project:** the joist's original payload lacks diagonals and the HVAC source explicitly declares Unitless lighting. These observations do not establish which Revit setting caused either condition. Retain the original payload, compare a corrected export and obtain authoritative photometric values. Never infer physical lumens from a label or an IES filename. See [dated evidence](Validation/2026-09-26.md).

Use complete `.udatasmith` exchanges and sidecars for reproducible tests. Record source/exporter versions, source IDs, dimensions and coordinate references. Do not change a coordinated source origin merely to make an isolated fixture appear aligned. The extracted fixtures omit parents and are not alignment evidence.

## Import customization and identity

Datasmith permits processing a translated scene before finalizing Unreal content. Custom pre-import filtering is not replayed by ordinary reimport, so lifecycle behavior needs deliberate handling. [Epic: import customization](https://dev.epicgames.com/documentation/unreal-engine/customizing-the-datasmith-import-process-in-unreal-engine)

**Project decision:** use the native translator/import seam, exact source-element identity, owned attempt folders, a versioned manifest, verified replacement and a real stock-reimport guard. Labels are display data. The tracked workflow is primary; Dataprep/legacy selection conversion remains available with separate guarantees. See [architecture](ARCHITECTURE.md) and [ADR 0001](ADR/0001-tracked-import-lifecycle.md).

Direct Link ingestion is not part of this plugin's file-import contract. Source relinking and broader deduplication also remain deferred; they cannot inherit safety guarantees simply by calling an existing legacy tool.

**Observed persistence boundary:** UE 5.8 `FileHelpers.cpp` duplicates a world when Save As targets a new name and the original map already exists on disk. The Phase 2 regression confirms changed actor GUIDs. The commandlet refuses this unsupported ownership migration before import/copy; explicit save of a native copy refuses when ownership proof fails. This installed-source/test finding is recorded in [Phase 2 evidence](Validation/2026-09-26-phase2.md), separately from the external import-customization guidance.

## MegaLights

Epic exposes MegaLights through **Project Settings > Rendering > Direct Lighting**, with hardware ray tracing guidance and per-light/volume/scalability controls. A project checkbox is not proof of effective support in every viewport or target. [Epic: MegaLights](https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine)

**Project decision:** the 100-enabled-local-light threshold is an advisory, not a hardware capacity claim. Observe the project flag, navigate to settings, and leave global changes to the user. The warning is excluded from PlanId. See [ADR 0004](ADR/0004-lighting-provenance.md).

## Further project references

- [Export SDK guidance](History/2026-09-26/DatasmithExportSDKGuidelines.md)
- [Import customization guidance](History/2026-09-26/DatasmithImportCustomizationGuidelines.md)
- [Validation matrix](VALIDATION.md) and [roadmap, risks and remaining gates](../ROADMAP.md)

The old offset/material/instance-insertion findings are resolved in current source and tests; their historical descriptions are not instructions to reimplement those fixes. Broader real-data and rendered acceptance is still required.
