# DatasmithHISM

Tracked Datasmith import and ISM/HISM optimization for **Unreal Engine 5.8.3**. Import runs in the editor; imported scenes and source metadata can be cooked into Windows applications.

**Status, 2026-09-28:** UE 5.8.3 editor build and **38/38 automation tests passed** after [verification fixes (Amendments 9-11)](Docs/Validation/2026-09-28-verification.md). Each new test failed before its fix; nothing has been re-checked live. Earlier [live UI acceptance (32/32)](Docs/Validation/2026-09-27-live-ui.md) found two defects, fixed and tested but not re-verified live. [Preset and cancellation evidence](Docs/Validation/2026-09-27-preset-cancellation.md) and [Phase 2 persistence/recovery evidence](Docs/Validation/2026-09-26-phase2.md) retain their records. Windows cook and packaged checks remain separate. Release acceptance is incomplete; [the handoff](HANDOFF.md) lists the live re-check items and remaining UI rows as the next task.

## Start here

1. Open **Tools > Optimized Datasmith Import**.
2. Choose a source readable by an enabled Datasmith translator and a `/Game/...` destination. Keep `.udatasmith` exports with their complete `_Assets` sidecars.
3. Choose ISM or HISM, the minimum group size, and tessellation where the translator supports it. ISM and a minimum of two are the defaults.
4. Review **Mesh, lighting, and material settings**. Nanite defaults to **All supported imported meshes**, including retained ordinary meshes. Material replacement is off until reviewed mappings are enabled.
5. Run **Analyze** to inspect grouping, exclusions, lights, appearances, dependencies, and proposed rebuild changes. It creates no scene assets or actors.
6. Run **Import and Verify**. A verified result is initially unsaved. Give its owning level a permanent name and use **Save imported result**.

[Full workflow and commandlet options](Docs/IMPORT_WORKFLOW.md) cover exceptions, named presets, rebuilds, material review, and inspection.

## Two workflows with different guarantees

| | Tracked import, primary workflow | Legacy selection tools |
|---|---|---|
| Input | Source file and translator | Actors/assets already in the project |
| Grouping | Exact source mesh reference, same parent, compatible source settings | Geometry signature, hierarchy boundary, effective materials and component descriptor |
| Recovery | Attempt ownership, verification, rollback, guarded replacement | No tracked manifest or session rollback; editor transactions where supported |
| Reimport | Stock reimport blocked; explicit optimizer-aware rebuild | No equivalent lifecycle guarantee |
| Runtime identity | Per-source records for ordinary meshes, instances and lights | No equivalent tracked source-record contract |

The primary importer does not merge separate mesh assets because their family names, labels, or bounds resemble one another. Cross-asset deduplication remains deferred for tracked imports. Scene-component roots remain where they carry transforms or attachments. See [architecture](Docs/ARCHITECTURE.md) and [legacy tools](Docs/LEGACY_TOOLS.md).

## Import policies

- **Nanite:** all supported imported meshes, converted ISM/HISM group meshes only (Amendment 10; originally "converted ISM meshes only", but HISM groups were always meant to be covered), or preserve imported settings. Exact source-mesh exceptions can retain ordinary actors or disable Nanite. Effective material checks precede compilation and verification.
- **Lights:** warn at 100 enabled local lights by default. Report project MegaLights configuration and offer settings navigation without changing global rendering settings. Verify exported units/intensity/IES evidence; ambiguous Unitless data stays unresolved.
- **Materials:** recognize appearances separately from approving Unreal replacements. Approval binds to exact appearance evidence and is reconsidered when it changes. External replacement materials are outside import rollback ownership.
- **Rebuild:** detect tracked manual changes before replacement; require Replace with source or Cancel. Headless execution requires `-ReplaceManualEdits` to discard those changes.
- **Identity:** output-changing options and approved mapping revisions participate in PlanId. Light advisories and sidecar-only changes do not silently trigger replacement. Use explicit rebuild for changed sidecars.
- **Persistence:** verification and successful saving are separate. Incomplete saves and unfinished attempts remain diagnostic states; uncertain objects are not automatically deleted.
- **Named-map copies:** Save As of an already-saved level changes actor GUIDs and is refused by the commandlet before import/copy. Continue with the owning map, or import independent output into a fresh map and destination. Native copies do not inherit manifest ownership from copied tags.

The initial appearance inventory contains 245 observed variants, not verified coverage of Autodesk's default library. See [material catalog and approval](Docs/MATERIALS.md).

## Known source findings

The sampled 16K6 joist for Revit elements 610662/610663 has no web diagonals in its original exported payload. Its 88 vertices / 160 triangles are preserved by all six ordinary/ISM/HISM and Nanite comparisons. The full structural/HVAC exports also have missing texture references.

All 1,033 lights in the supplied HVAC export explicitly declare Unitless intensity. Preserving those exported values is tested; matching physical Revit brightness requires authoritative source values and calibration. These findings are documented with fixtures and evidence in the [validation record](Docs/Validation/2026-09-26.md).

## Install, build, and package

Place this folder under a project's `Plugins/DatasmithHISM` directory. Build against the tested engine version with the editor closed. DatasmithImporter and DataprepEditor dependencies are declared by the plugin; additional source formats depend on enabled translators. Compatibility with other engine versions is not established by this baseline.

- [Build and automation commands](Docs/VALIDATION.md)
- [Runtime source lookup and Windows packaging](Docs/RUNTIME_AND_PACKAGING.md)
- [Architecture decisions](Docs/ADR/README.md)
- [Documentation index](Docs/README.md)

The historical `DatasmithHISM` name and `ConVerse` C++ prefix remain for compatibility. ISM is the default; HISM remains an explicit supported choice. Performance depends on the scene and target and requires measurement.

## License

MIT. See [LICENSE](LICENSE). The fixture/catalog evidence does not grant distribution rights to Autodesk library assets or a future material pack.
