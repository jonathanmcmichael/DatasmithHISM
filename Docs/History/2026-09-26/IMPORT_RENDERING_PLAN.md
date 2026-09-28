> Historical snapshot before the 2026-09-26 documentation consolidation. Claims and task status below are historical, not current guidance. See the [current documentation index](../../README.md). Relative links have been adjusted for this archive.

# DatasmithHISM project review and rendering import plan

Reviewed 2026-09-25 against the current project source, installed UE 5.8 source, saved automation/import evidence, and the Revit export used by the latest successful import. This is a proposed implementation plan. No feature code or project settings were changed during this review.

**Project summary**

AdvancedHISM is the UE 5.8 host for the editor-only DatasmithHISM plugin. Its primary workflow analyzes a fresh Datasmith scene, groups eligible repeated mesh actors, imports the transformed scene, produces ISM or HISM output, verifies it, and records source identity and ownership in a persistent manifest. Failed attempts have explicit rollback. Ordinary Datasmith reimport is blocked for active owned output; optimized reimport creates and verifies a replacement before superseding the previous session. Explicit acceptance of failed verification creates a quarantined session that cannot be automatically superseded.

The separate selection tools perform in-place conversion, deduplication, analysis, explosion, and manual Nanite enablement. They have editor Undo but no optimized-import manifest or rollback. The tools panel now discloses this difference. These new import features belong in the tracked import service.

Optimized grouping currently requires the same Datasmith mesh reference, immediate parent, material bindings, and shared settings. It does not deduplicate separately exported equivalent mesh assets. Singleton, parent-bearing, and unsupported mirrored actors can remain ordinary actors. Nanite operates on mesh assets and should not depend on whether an actor qualifies for grouping.

**Evidence and limits**

| Area | Current evidence |
| --- | --- |
| Automation | `Saved/Logs/Automation.txt` contains 16 successful test completions and no `LogAutomationController: Error:` entries. This log was inspected; tests were not rerun for this planning review. |
| Build | `HANDOFF.md` records a successful UE 5.8.3 build. No fresh build was run. The editor is open. |
| Real model | The latest saved import entry records 944 verified groups and 9,106 verified instances from 16,598 source mesh actors. |
| Actors outside grouping | The report records 5,765 below-threshold actors, 1,506 actors with children, and 221 unsupported negative-scale actors: 7,492 total. Some can share an asset with a converted group, so this is not a count of non-Nanite assets. |
| Nanite | `ConvertHISMsToISMsTwoStage` calls `EnableNaniteIfNeeded` only for meshes used by converted ISM groups. There is no import-wide Nanite policy or Nanite verification. |
| MegaLights | `Config/DefaultEngine.ini` already sets `r.MegaLights.EnableForProject=True`, with DX12/SM6 and ray tracing configured. Configured support does not establish the effective viewport state. |
| Lighting | The service has no light inventory, high-count warning, or photometric verification. Light import is delegated to Datasmith. |
| Maintenance | `ConVerseDatasmithImportService.cpp` is 3,368 lines. New mesh/light helpers should be separated as they are introduced, while retaining the existing ownership and rollback orchestration. |

Some guidance is stale: root `AGENTS.md` still describes nine tests and unexecuted rollback; the plugin's older guidance says there are no automated tests. Current source, the saved 16-test log, and the updated `NEXT_STEPS.md` show that rollback coverage exists. Documentation reconciliation belongs in implementation phase 1.

The inspected source is `C:/Users/jonathanmc/Desktop/Snowdon_Towers_Sample_HVAC-3DView-{3D}.udatasmith`. Its header records Revit 2025 and Datasmith SDK `5.3.0-24761556`; these are exported header values, not a claim about the currently installed exporter. The successful report is `Saved/DatasmithHISM/ImportReports/ad0c947f4492b27227d90bb274d1ddf6.json`.

**Lighting finding that changes the implementation approach**

The source contains 1,033 enabled `PointLight` elements. All 1,033 explicitly specify `IntensityUnits=Unitless`. Of these, 801 reference IES textures and specify `IESbrightness scale=-1`; 232 have no IES texture reference. The UE 5.8 XML reader interprets that negative scale as IES brightness disabled and normalizes the scale to 1. An IES distribution alone therefore does not establish the intended Revit luminous output.

The inventory was obtained by extracting all complete `Light` blocks and parsing each independently, with zero light-block parsing errors. A strict Python parse of the whole export rejected an invalid token elsewhere in the file; this inventory is not a whole-file XML validation or an Unreal import test. The source was not changed.

The installed `DatasmithLightImporter.cpp` copies the source intensity and maps explicit Candelas/Lumens units to the corresponding Unreal units, defaulting to Unitless otherwise. The observed Unitless state is already present upstream of this plugin. Epic documents preservation of Revit physical intensity units, but this particular export does not encode them as lumens or candelas. The reason must be established using representative Revit fixtures and the export path. [Epic: Datasmith with Revit](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-datasmith-with-revit-in-unreal-engine)

A targeted scan of exported metadata found photometric web filenames and wattage comments, but no explicit lumen, candela, or initial-intensity field sufficient to establish a conversion. This does not prove Revit lacks those values. They may require a different export or explicit extraction from the source model.

Changing only `IntensityUnits` would reinterpret the existing number and change the emitted light. A conversion that preserves Unreal's current brightness is also not proof of fidelity to Revit. Light type, distribution, falloff, IES settings, and intensity must be validated together. [Epic: physical lighting units](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-physical-lighting-units-in-unreal-engine)

**Proposed behavior**

| Feature | Proposed user experience | Scope and safety |
| --- | --- | --- |
| Automatic Nanite | Default new imports to **All supported imported meshes**. Offer **Converted ISM meshes only** as the existing behavior. Summarize enabled, already enabled, unsupported, and failed assets. | Process unique mesh assets created by this import, including assets used only by ordinary actors. Apply independently of ISM/HISM choice. Keep incompatible materials intact and report exceptions. |
| Many-light warning | Analyze shows total lights, enabled local lights, light types, IES usage, and unit counts. Initially warn at 100 enabled local lights, configurable after profiling. | 100 is a proposed advisory threshold, not an Epic limit. Warn before mutation, including direct Import without a prior Analyze. Respect whether light import is selected. |
| MegaLights advice | If many lights are planned and the project setting is off, recommend enabling MegaLights and offer navigation to the setting. If configured on, report that fact and any known overrides. | Advisory only. Do not automatically change global rendering settings. If runtime support cannot be established, report it as unknown rather than claiming MegaLights is active. |
| Revit light fidelity | Preserve and verify valid physical units. For ambiguous Unitless data, report the missing provenance. Apply a correction only after the Revit/exporter mapping has been established. | Record original and applied units, intensity, IES state, and the reason for any conversion. Unknown data stays explicitly unresolved. |

Nanite supports ordinary static meshes as well as ISM and HISM components, but has material restrictions. In particular, enabling it on incompatible translucent content can substitute a default material. The all-mesh policy needs explicit exceptions for these cases, including component material overrides on shared assets. [Epic: Nanite support](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine)

MegaLights can be controlled by project settings, post-process volumes, per-light settings, and scalability/device settings. Counts should emphasize enabled local lights, with directional/environment lights listed separately. Light overlap and attenuation affect the practical result; the count is an early advisory, not a performance guarantee. [Epic: MegaLights](https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine)

**Implementation phases**

| Phase | Work | Completion gate |
| --- | --- | --- |
| 1. Baseline and contract | Reconcile stale guidance. Add a contract amendment covering mesh-wide processing, lighting inventory, verification scope, and option persistence. Create a small Revit comparison fixture with known photometric values and both IES and non-IES lights. Record Revit/exporter versions and compare raw Datasmith import with optimized import. | A field-by-field source/export/Unreal comparison establishes where unit or value information is lost. Unresolved exporter behavior is recorded without blocking independent Nanite/advisory work. |
| 2. All-imported-mesh Nanite | Introduce the Nanite policy in the panel, service, commandlet, plan identity, and manifest. Add an asset pass after Datasmith import and before final verification/commit. Remove the conversion-only call as the policy authority. | Unique, repeated, below-threshold, mirrored, and child-bearing mesh cases are covered in ISM and HISM modes. Unrelated assets remain untouched. Builds finish before success; exceptions are named. |
| 3. Light inventory and MegaLights warning | Add recursive source light inventory, pre-import advice, project/effective-state diagnostics, and report/log fields. Reconcile planned lights with actual import options and imported objects. | Boundary tests below/at/above the threshold, disabled/nested lights, mixed light types, light import disabled, MegaLights configured on/off, and commandlet operation pass. No project settings change. |
| 4. Revit photometric fidelity | Implement only the mapping established in phase 1. Preserve valid physical values and handle supported corrections with type-aware logic. Add light identity, source/applied values, IES state, and verification results to the manifest. | Known Revit fixtures reproduce the expected units and effective output; ambiguous fixtures produce an explicit diagnostic. Unit-only relabeling cannot pass. Reimport and save/reopen preserve the recorded behavior. |
| 5. Integrated acceptance | Run the real build and automation with the editor closed, then validate the real Revit model in the editor. Exercise save/reopen, unchanged-source verification, changed-source supersede, cancellation, and injected failure after the new mesh/light work. | One active session, no duplicate geometry or lights, restored inventories after failure, correct material appearance and light output, and recorded import/build/GPU measurements. Headless success and viewport acceptance are reported separately. |

Phase 2 can proceed after the contract decisions in phase 1 even if exporter calibration remains open. Phase 3 can ship independently of photometric correction. Phase 4 must wait for trustworthy Revit reference values.

**Implementation details that must survive review**

1. **Cover imports with no repeated meshes.** The current service returns `AnalysisNoEligibleGroups` and the panel disables import when the group list is empty. To make the new Nanite policy useful for a unique-mesh scene, amend this contract and support a tracked import with zero groups when import-wide processing is requested. Test ownership, verification, rollback, and reimport on that path. Retain the old no-work behavior when no import-wide work is selected.

2. **Use the imported asset inventory.** Start with `UDatasmithScene::StaticMeshes`, deduplicate resolved assets, and intersect with the attempt's newly created inventory before mutation. Account for any additional imported static meshes. Do not discover targets by scanning every mesh in the project or only the converted groups. Unexpected external/shared ownership must be reported, not silently modified.

3. **Wait for actual mesh work.** `EnableNaniteIfNeeded` currently sets the flag and calls `PostEditChange`; its boolean does not prove a successful Nanite build. Batch unique meshes, show progress, handle cancellation, wait for relevant mesh compilation, and verify build output as well as settings. Unsupported assets should retain their appearance with reported exceptions. A supported mesh that fails its required build must fail verification and enter the existing rollback path.

4. **Persist output-affecting choices.** Nanite policy and any light conversion policy/version belong in `ComputePlanId` and the manifest option snapshot. Changing either must invalidate analysis and prevent an incorrect `AlreadyCurrent`. Warning thresholds and observed MegaLights settings are diagnostic and must not trigger destructive reimport. Keep sidecar changes warn-only, including IES-only changes.

5. **Handle old manifests deliberately.** Missing new fields mean legacy/unrecorded behavior, not verified all-mesh Nanite or verified physical lighting. Plan schema compatibility and an explicit upgrade/reimport path. Merely opening an old session or changing an advisory must not rebuild it. Continue refusing unknown future schemas and quarantined sessions.

6. **Extend verification beyond groups.** Add per-mesh and per-light records and aggregate results without pretending a light failure is a failed ISM group. Reverification must detect changed Nanite flags and light units/intensity/IES settings. Integrate these failures with the existing rollback and explicit acceptance/quarantine contract; headless runs remain failures when required checks fail.

7. **Match source identity, not labels.** Revit fixture geometry can have child light elements. Preserve that hierarchy and use element names/identity to join source lights to imported output. Tests must include these parent-bearing fixtures and avoid cross-test world contamination.

8. **Keep photometric claims testable.** Use a fixed-exposure comparison scene and record numerical source and engine values. Cover point, spot, and area/rect lights where present, valid lumens/candelas, Unitless, falloff, IES brightness enabled/disabled, missing profiles, and invalid intensities. Do not infer luminous output from electrical wattage or from the presence of an IES filename.

9. **Prove the new guards can fail.** Mutation-check plan identity by temporarily omitting each output-affecting policy from the hash, and verification by bypassing the Nanite/light checks. The corresponding behavioral tests must fail; restore the implementation and rerun the suite afterward.

**Code areas**

| Area | Planned responsibility |
| --- | --- |
| `Private/ConVerseDatasmithImportService.h/.cpp` | Immutable options and inventories, processing order, plan identity, session verification, rollback integration. |
| New private mesh/light helper files | Nanite processing, light inventory, rendering diagnostics, validated photometric mapping. Keep ownership decisions in the service. |
| `Private/ConVerseDatasmithImportPanel.h/.cpp` | Policy controls, warning display, analysis invalidation, zero-group import affordance, summary. |
| `Private/ConVerseOptimizedImportCommandlet.cpp` | Equivalent options and structured diagnostics without dialogs. |
| `Public/ConVerseOptimizedImportManifest.h` | Versioned option snapshot and mesh/light evidence, with backward compatibility. |
| `Private/Tests/` | Behavioral fixtures for coverage, identity, isolation, rollback, reimport, and lighting provenance. |
| `IMPORT_PANEL_VALIDATION.md`, `NEXT_STEPS.md`, `HANDOFF.md` | Revised guarantees, active work list, and separate build/automation/editor acceptance evidence. |

**Remaining project work outside these features**

Keep the existing real-model changed-source reimport and sidecar-warning checks in phase 5. Legacy BIM hierarchy, storey boundary, and Nanite auto-detection coverage remain useful follow-up work. Giving the legacy tools a manifest is a separate project. Retaining superseded asset packages remains the deliberate policy until external-reference safety can be established. A broad service rewrite and cross-asset geometry deduplication are not prerequisites for these three additions.

The next implementation step is phase 1's contract and fixture work, followed by the all-imported-mesh Nanite pass. The existing Snowdon export provides a substantial geometry/light inventory, but Revit's authoritative photometric values are still needed before promising corrected brightness.
