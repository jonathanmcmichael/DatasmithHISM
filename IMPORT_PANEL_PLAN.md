# Optimized Datasmith Import Panel: Five-Phase Implementation Plan

Status: implementation complete on 2026-09-22. The UE 5.8.3 editor target builds successfully. The generated-fixture automation test compiles, but this machine's packaged editor launcher currently aborts before it starts a test because all-platform SDK validation reports missing LinuxArm64 and VisionOS `MainVersion` settings. Real Revit/Datasmith acceptance remains pending a representative export and sidecar assets.

## Execution status

| Work unit | Status | Evidence |
|---|---|---|
| IP1.1 baseline | Complete | UE 5.8.3, changelist 58210709. The original scaffold failures were corrected. `AdvancedHISMEditor Win64 Development -Project="D:\Unreal\Sandbox\AdvancedHISM\AdvancedHISM.uproject" -WaitMutex -NoHotReloadFromIDE` now succeeds. No representative `.udatasmith`, `_Assets`, or `.ifc` fixture exists under the project. |
| IP1.2 API audit | Complete | UE 5.8 integration seam is valid. Audit found P0 gaps in metadata, rollback, ordinary/automatic reimport, partial ISM conversion, mirrored transforms, exact asset verification, and repeated imports. |
| IP1.3 contract | Complete | [IMPORT_PANEL_VALIDATION.md](IMPORT_PANEL_VALIDATION.md) freezes immutable planning, status, manifest, rollback, verification, and first-release reimport behavior. |
| IP2 core service | Complete, build verified | Immutable planning, native Datasmith scene mutation, HISM import, optional ISM replacement, exact verification, rollback accounting, manifests, and repeated-import/reimport protection are implemented. |
| IP3.1 panel shell | Complete, integrated and build verified | The Slate panel is registered under **Tools > Optimized Datasmith Import** and as a nomad tab. |
| IP4 manifest and reimport guard | Complete, build verified | Persistent source-identity manifest and ownership markers are written only after verification; ordinary Datasmith reimport is blocked for committed optimized output. |
| IP5 generated-fixture automation | Implemented and compile verified | The test covers Analyze, ISM/HISM import, metadata, negative-scale rejection, source immutability, verification, and cleanup. Runtime execution is blocked before editor startup by this machine's optional-platform SDK validation. |

## Goal

Add a dockable editor panel that accepts a `.udatasmith` file, lets the user choose ISM or HISM output, optimizes eligible repeated mesh actors in memory before they populate the level, imports the transformed scene through Unreal's Datasmith importer, and reports whether the imported result matches the pre-import plan.

The source `.udatasmith` file and its `_Assets` sidecar remain unchanged. The first accepted release must distinguish these states:

1. Analysis succeeded.
2. Import succeeded.
3. Optimization created the expected components.
4. Verification passed or failed.
5. Import was cancelled or rolled back.

An import is not reported as verified merely because `UDatasmithImportFactory` returned an object.

## Architecture to freeze in Phase 1

1. Resolve and parse the file with Datasmith's `FExternalSource` and native translator.
2. Traverse `IDatasmithScene` and create an immutable optimization plan.
3. Group only leaf static-mesh actors that have the same hierarchy boundary, exact mesh reference, ordered material overrides, layer, visibility, shadow, mobility, and component status. The minimum group size defaults to two.
4. Replace each accepted group in the in-memory scene with an `IDatasmithHierarchicalInstancedStaticMeshActorElement`. Datasmith natively imports this element as `UHierarchicalInstancedStaticMeshComponent`.
5. For HISM output, retain that component. For ISM output, replace only components tagged for the current import session with `UInstancedStaticMeshComponent`, copying the mesh, effective materials, settings, and instance transforms.
6. Retain an expected-result manifest for verification and source identity. Removing source actor elements without a manifest is not acceptable for metadata-bearing Revit data.
7. Verify the current import session in the editor world. Existing ISM/HISM components in the map are outside the verification scope.

The initial grouping contract deliberately uses exact Datasmith mesh references. Geometry-equivalence grouping across separate mesh elements is a later extension because it requires payload comparison and pivot compensation before it is safe.

## UI panel specification

The panel is registered as **Optimized Datasmith Import** under the Level Editor Tools menu and as a nomad tab.

### Inputs

| Control | Behavior |
|---|---|
| Source file | Editable path plus Browse button. Accept `.udatasmith` only and verify the file exists. |
| Destination content folder | Unreal long package path, default `/Game/DatasmithOptimized`. Validate before import. |
| Output component | Explicit ISM/HISM selector. Default ISM, consistent with this plugin's Nanite-first convention. |
| Minimum instances | Integer, minimum 2, default 2. |
| Analyze | Parse and plan without creating assets or actors. |
| Import and Verify | Import the optimized in-memory scene into the current editor world, then run verification. |

### Output

The report area shows source mesh-actor count, eligible actor count, group count, converted instance count, below-threshold count, skipped reasons, import result, and one PASS/FAIL row per optimized group. Each group row includes component type, mesh, material result, expected/actual instance count, and transform result. Limit the visible group list for large models and provide the full report through the Output Log or a saved text artifact.

### States

- **Idle:** no valid source.
- **Ready:** source and destination inputs validate.
- **Analyzed:** a current plan exists for the selected file and options.
- **Working:** controls are disabled; progress and cancellation are available.
- **Verified:** import and every required check passed.
- **Imported with failures:** the Datasmith import returned an asset, but one or more optimization checks failed.
- **Cancelled / failed:** show the stage and leave no partially converted session output.

## Agent operating rules

- One agent owns each file while it is editing. Shared service tasks run sequentially.
- Agents do not edit `PLAN.md`, `README.md`, `JOURNAL.md`, or `AGENTS.md`; the integrator owns project documentation.
- Agents report changed files, exact build/test commands, results, and unverified behavior.
- No agent may claim real Revit/Datasmith validation without a `.udatasmith` fixture and its sidecar assets.
- Preserve existing uncommitted work and the tags `ConVerseManagedHISM` and `ConVerseManagedFamilyType`.
- The new importer uses separate session tags and must not rewrite existing managed output.

## Phase 1: Baseline, API audit, and contract freeze

| ID | Atomic action | Owner files | Acceptance check | Depends on |
|---|---|---|---|---|
| IP1.1 | Record Git status, UE version, exact UBT command, current build result, and available `.udatasmith`, `_Assets`, `.ifc`, and `.umap` fixtures. | Validation report only | Evidence includes command output and clearly separates build success from editor/data validation. | None |
| IP1.2 | Audit the partial service against UE 5.8 public APIs: external-source lifetime, scene mutation, actor attachment/removal, import-factory behavior, HISM actor import, and component replacement. Do not edit code. | Review report only | Every compile-risk and lifecycle risk names the relevant API and source line. | IP1.1 may run in parallel |
| IP1.3 | Freeze `FConVerseOptimizedImportOptions` and `FConVerseOptimizedImportResult`, group eligibility, result states, transform tolerances, session tags, metadata manifest fields, rollback behavior, and reimport policy. | This plan or a focused design note | UI and service agents can implement without inventing new public behavior. | IP1.2 |
| IP1.4 | Decide the first-release reimport behavior. Either implement optimizer-aware reimport or clearly block standard Datasmith reimport for optimized scene assets and expose an explicit optimized reimport action. | Design note only | Two consecutive imports cannot silently create raw and optimized duplicates. | IP1.2 |

**Phase 1 gate:** The contracts are written, the current scaffold has an API audit, fixture availability is known, and no implementation agent needs to guess how cancellation, metadata, or reimport should behave.

## Phase 2: Core pre-import optimizer

These tasks share `ConVerseDatasmithImportService.h/.cpp` and therefore run sequentially under one service owner.

| ID | Atomic action | Owner files | Acceptance check | Depends on |
|---|---|---|---|---|
| IP2.1 | Implement option validation and fresh external-source loading for every Analyze or Import request. | `Private/ConVerseDatasmithImportService.h/.cpp` | Missing file, wrong extension, unsupported translator, and invalid destination each produce a distinct non-crashing result. | Phase 1 gate |
| IP2.2 | Implement read-only scene traversal and deterministic plan construction. Record candidates, group IDs, source actor identity, expected world transforms, mesh, materials, settings, and skip reasons. | Same | Repeated analysis yields identical group IDs and counts and creates no packages or world actors. | IP2.1 |
| IP2.3 | Apply the plan to the in-memory scene using Datasmith HISM actor elements. Preserve parent-relative placement and leave rejected actors untouched. | Same | The on-disk `.udatasmith` hash is unchanged; accepted source actors are absent only from the transformed in-memory scene. | IP2.2 |
| IP2.4 | Import through `UDatasmithImportFactory`, tag the session, and implement the HISM-to-ISM replacement path for ISM selection. | Same | HISM selection yields exact HISM components. ISM selection yields exact non-HISM ISM components with the same mesh, materials, settings, and instances. | IP2.3 |
| IP2.5 | Add rollback for import failure, conversion failure, and cancellation. Scope cleanup to the current session. | Same | A forced failure leaves no session-tagged actors/components or partially registered asset from this operation. | IP2.4 |

**Phase 2 gate:** The service compiles, Analyze is read-only, both output modes work on a synthetic scene, source files remain unchanged, and failure cleanup is session-scoped.

## Phase 3: Dockable editor UI

UI files can be implemented in parallel with late Phase 2 work after the Phase 1 interface contract is frozen. Module integration runs after the panel compiles in isolation.

| ID | Atomic action | Owner files | Acceptance check | Depends on |
|---|---|---|---|---|
| IP3.1 | Build the Slate panel shell and controls defined above. | New `Private/ConVerseDatasmithImportPanel.h/.cpp` | Panel compiles and exposes options without changing service types. | Phase 1 gate |
| IP3.2 | Wire Browse, input validation, ISM/HISM selection, minimum count, Analyze, and Import and Verify. | Same | Invalid fields disable actions or produce inline errors; Analyze never imports. | IP3.1, IP2.1 |
| IP3.3 | Add state handling, progress/cancel behavior, and a scalable report view. | Same | Controls cannot start overlapping imports; the final state distinguishes verified, imported-with-failures, cancelled, and failed. | IP3.2, IP2.5 |
| IP3.4 | Register/unregister the nomad tab and add one Tools menu entry and optional toolbar entry. | `Public/DatasmithHISM.h`, `Private/DatasmithHISM.cpp`, style files only if an icon is added | Repeated module reload does not duplicate menu entries; shutdown leaves no registered tab spawner. | IP3.1 |
| IP3.5 | Add only the required module/plugin dependencies. | `DatasmithHISM.Build.cs`, `DatasmithHISM.uplugin` | UBT succeeds without unused private dependencies introduced by this feature. | IP2.4, IP3.4 |

**Phase 3 gate:** A user can open the panel, select a file and output type, analyze without mutation, run one import at a time, cancel safely, and read a clear result.

## Phase 4: Verification, metadata, and lifecycle safety

| ID | Atomic action | Owner files | Acceptance check | Depends on |
|---|---|---|---|---|
| IP4.1 | Implement group-level verification for exact component class, mesh, every effective material slot, instance count, and ordered world transforms. | Import service files | A controlled mismatch fails the specific check while a matching result passes. | Phase 2 gate |
| IP4.2 | Verify coverage: every planned group appears once, no extra session group appears, rejected actors remain, and optimized raw source actors do not appear as duplicate geometry. | Import service files | Totals reconcile to the pre-import plan. | IP4.1 |
| IP4.3 | Persist a source identity manifest containing source scene identity, source actor/element name, available Revit/Datasmith identity, group ID, instance index, and missing-ID state. | New manifest UObject files plus import service | Save/reopen preserves index alignment and a query returns the source identity for a tested instance. | IP4.1; Phase 1 metadata contract |
| IP4.4 | Implement the selected reimport policy and test it twice. | Import service/module files as specified in IP1.4 | Two consecutive optimized imports or reimports produce no duplicate instances and retain unrelated map content. | IP4.3 |
| IP4.5 | Add editor automation around a generated or checked-in fixture for plan determinism, ISM/HISM type, transforms, materials, rollback, and metadata alignment. | New test files only | Focused automation passes from a clean editor invocation. | IP4.1 through IP4.4 |

**Phase 4 gate:** Verification proves the requested conversion, metadata remains traceable, failure paths are non-destructive, save/reopen works, and repeated import behavior follows the documented policy.

## Phase 5: Integrated and real-data validation

| ID | Atomic action | Owner files | Acceptance check | Depends on |
|---|---|---|---|---|
| IP5.1 | Run UBT on the integrated working tree and record warnings/errors. | Build output and validation report | `AdvancedHISMEditor Win64 Development` succeeds. | Phases 2 through 4 |
| IP5.2 | Run focused automation and a manual synthetic UI smoke test for both ISM and HISM. | Validation report only | Analyze counts equal imported verified counts; cancellation and forced failure leave no session output. | IP5.1 |
| IP5.3 | Run a representative Revit `.udatasmith` export with repeated families, material variants, nested parents, metadata, and sidecar meshes. | Disposable level and validation report only | Record before/after actors, assets, groups, instances, transforms, materials, metadata lookup, time, memory, save/reopen, and repeated import. | IP5.2; real fixture available |
| IP5.4 | Update README, agent guidance, plan status, and journal from recorded evidence. | `README.md`, `AGENTS.md`, `PLAN.md`, `JOURNAL.md`, `Info.md` | Documentation names actual UE version and test data, and labels any missing real-data check as pending. | IP5.3 or an explicit pending-data decision |
| IP5.5 | Make the release decision. | Plan and journal only | Release-ready requires integrated build, automation, UI smoke, and real-data pass. Otherwise state the exact blocked gate. | IP5.4 |

**Phase 5 gate:** Release claims are supported by build output and an actual Revit/Datasmith import. Synthetic tests remain evidence for code behavior but do not replace the real-data pass.

## Initial delegation after this plan

| Agent | First assignment | File ownership |
|---|---|---|
| API audit agent | IP1.2: audit the partial import service and UE 5.8 lifecycle/API assumptions; report only. | Read-only |
| UI agent | IP3.1: implement the panel shell against the frozen option/result types; no module registration. | New panel `.h/.cpp` only |
| Validation agent | Draft IP1.3 metadata, rollback, reimport, and verification contract plus a fixture matrix. | New focused design/validation note only |
| Integrator | Own this plan, reconcile agent findings, freeze interfaces, integrate module wiring, run UBT, and resolve cross-file issues. | Shared integration and project docs |

The service owner starts Phase 2 edits only after the API audit and contract note are integrated. The UI shell may proceed because it consumes the current private option/result types without changing them.
