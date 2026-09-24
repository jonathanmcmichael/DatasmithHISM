# DatasmithHISM — Development Plan

Living document. Update after each work session.

---

## Scope

This plugin handles **any Datasmith or IFC/Revit import** where the source authoring tool emitted one `UStaticMesh` asset per placed instance instead of per unique geometry. The canonical test case is Herman Miller Aeron chairs (10,000 actors, ~4 geometry variants, ~8 material variants), but the same pattern appears across:

- **Revit MEP** — pipe fittings, valves, diffusers, lighting fixtures; each family type may have dozens of instance parameter variants but identical geometry
- **Revit Structural** — columns, beams, braces; large buildings can have thousands of structurally identical members
- **Revit Interiors** — chairs, desks, workstations, panel systems; often the highest-count category in an office building
- **IFC from any authoring tool** — IfcProduct hierarchies translated by Datasmith into actor trees; geometry dedup is not guaranteed
- **Civil/landscape** — trees, bollards, site furniture; potentially hundreds of thousands of instances

The plugin must perform well at any of these scales without code changes. Thresholds and grouping heuristics should be configurable, not hardcoded.

---

## Current state

**Status as of 2026-09-22:** Sessions 1–8 and the optimized Datasmith import implementation are recorded. The UE 5.8.3 editor target builds successfully, but the project has not been exercised against a real IFC/Datasmith map. The generated-fixture optimized-import automation test compiles, but this machine's packaged editor launcher aborts before starting it because optional LinuxArm64 and VisionOS SDK settings are missing. The five phases below remain the execution order for the existing post-import converter. Historical task rows later in this file are an inventory, not an instruction to implement every idea.

The dockable pre-import UI described in [IMPORT_PANEL_PLAN.md](IMPORT_PANEL_PLAN.md) is implemented and build-verified. Its immutable-plan, metadata, rollback, verification, and reimport contracts are in [IMPORT_PANEL_VALIDATION.md](IMPORT_PANEL_VALIDATION.md). Before treating the optimized importer as runtime-verified, run `DatasmithHISM.OptimizedImport.GeneratedFixtureEndToEnd` in an editor environment that can launch commandlet automation, then validate against a representative Revit `.udatasmith` export and its `_Assets` folder.

---

## Next work: five phases

The first deliverable is a reproducible baseline and destructive-operation safety fixes. P1.2 and P1.3 now ensure a declined Dedupe operation performs no reference replacement and stops the combined pipeline before Managed ISMs run. P1.4 now stages Explode output and retains the source component if any actor recreation fails. P1.5 now audits loaded components and on-disk package referencers, skipping any duplicate mesh with a reference outside the supplied context. The remaining Phase 1 work is editor-fixture validation of these paths. The current grouping key covers family, geometry, and materials, but not other component settings copied from the first source. The placement path uses the actor transform, not the source mesh component transform. These are code findings, not validated failures on a real import.

### How to hand off each work unit

- Assign one model per ID. Give it the ID, listed files, dependency, expected behavior, and acceptance check. An implementer must inspect the current code before editing; the file list is an ownership boundary, not a substitute for reading callers.
- Independent IDs can run in parallel only when their edit files do not overlap. One integrator owns `PLAN.md`, `README.md`, `JOURNAL.md`, and `AGENTS.md`; implementation models send evidence and changed-file lists to that integrator. Coordinate UnrealBuildTool and editor runs because all models share one project and build output.
- Preserve the current uncommitted work. Each model reports the diff it made, build or test command and result, and anything unverified. A completed code task is not a completed phase until its gate passes. Use a small synthetic scene when real IFC data is unavailable; record real-data checks as pending.
- Keep existing Blueprint names and fields, `ConVerseManagedHISM` and `ConVerseManagedFamilyType` tags, and the default ISM behavior unless a work unit explicitly changes them. Do not turn a read-only analysis into a mutating operation.

### Phase 1 — Establish a baseline and make existing destructive tools safe

| ID | Owner files | Work and acceptance | Depends on |
|---|---|---|---|
| P1.1 | Project build output and a validation note; no source edits | Record UE version, plugin Git status, exact UBT command and result. Inventory available `.ifc`, `.udatasmith`, and `.umap` test data. Record a minimal editor smoke-test checklist for Analyze, Managed, Dedupe dry-run, Dedupe confirmation, Explode, undo, and Dataprep. Do not label the current tree built until UBT actually succeeds. | None |
| P1.2 | `Public/ConVerseStaticMeshConsolidationLibrary.h`, `Private/ConVerseStaticMeshConsolidationLibrary.cpp` | **Implemented, build verified.** `EConVerseStaticMeshConsolidationOutcome` reports completed, cancelled, or failed. Interactive confirmation now occurs before `ReplaceStaticMeshReferencesInObjects`; a No response leaves references and assets unchanged. Headless Dataprep behavior is retained. Verify Yes and dry-run paths separately on disposable assets. | P1.1 |
| P1.3 | `Private/DatasmithHISM.cpp` | **Implemented, build verified.** Dedupe + ISMs stops before Managed ISMs when Dedupe returns cancelled or failed. Verify no ISM conversion follows a declined delete. | P1.2 |
| P1.4 | `Private/ConVerseHISMLibrary.cpp` | **Implemented, build verified.** Explode stages and validates every replacement actor/component. On failure, staged actors are destroyed, the original ISM/HISM component remains intact, and failures are counted. Source components are removed only after complete success. Test a forced spawn failure plus normal success and undo. | P1.1 |
| P1.5 | `Private/ConVerseStaticMeshConsolidationUtils.cpp/.h`, `Private/ConVerseStaticMeshConsolidationLibrary.cpp` | **Implemented, build verified.** Audits loaded `UStaticMeshComponent` references and Asset Registry on-disk package referencers. It replaces/deletes only duplicates whose detected references are all inside the supplied context; external or uncertain references skip the duplicate and are reported to Blueprint and Dataprep. Test a selected actor, an unselected actor, and an asset selected without any actors; none may gain a broken reference. | P1.2; coordinate with P1.3 on the result contract |

**Gate:** UBT succeeds on the integrated tree. On disposable assets, declining Dedupe changes nothing, dry-run changes nothing, out-of-selection references remain valid, Explode failure retains all source instances, and successful Explode is undoable. If no imported map exists, record that limitation without blocking synthetic checks.

### Phase 2 — Make conversion preserve placement and component behavior

| ID | Owner files | Work and acceptance | Depends on |
|---|---|---|---|
| P2.1 | New fixture or editor automation files only | Specify and create a small scene with shared and separate-but-equivalent meshes, nonzero component-relative transforms, differing mesh pivots, mesh-default and component-override material variants, and differences in collision, mobility, visibility, shadow, and navigation settings. Capture expected world transforms, effective materials, and group counts before conversion. | Phase 1 gate |
| P2.2 | `Private/ConVerseHISMUtils.cpp/.h`, `Private/ConVerseStaticMeshConsolidationUtils.cpp/.h` | Define the mesh-origin contract, then use the source component world transform with any proven canonical-pivot correction. Exact geometry and material variants must retain the fixture's world-space placement. Leave meshes unconverted when compensation cannot be established safely. | P2.1 |
| P2.3 | `Private/ConVerseHISMUtils.cpp/.h` | Add only the effective component settings that one ISM must share to grouping eligibility/key; copy those settings, including every effective source material slot, to the new ISM before source deletion. Sources with incompatible collision, mobility, visibility, shadow, navigation, or other proven render settings must form separate groups or remain untouched. Verify component material overrides keep their appearance. | P2.2 |
| P2.4 | `Private/ConVerseHISMUtils.cpp/.h` | Make Analyze and Build use the same eligibility, boundary, and grouping decisions. On the fixture, Analyze's predicted groups and conversion count must equal a successful Build's actual counts. Analyze must cause no world or asset changes. | P2.3 |
| P2.5 | `Private/ConVerseHISMUtils.cpp/.h` | Check each `AddInstance` result and final instance count before queuing source actors for deletion. If a group cannot be built completely, keep every source actor for that group and remove staged output. Verify failure reporting with a controlled failure case. | P2.4 |
| P2.6 | `Private/ConVerseHISMUtils.cpp/.h` | Audit non-mesh components, actor tags, user data, and child relationships before treating an actor as disposable. Preserve or skip any actor whose non-mesh payload cannot be represented by the managed result. Test a one-mesh actor with an extra behavior-bearing component. | P2.5 |
| P2.7 | `Private/ConVerseHISMUtils.cpp/.h` | Avoid creating managed family actors for groups below `MinInstanceCount`, or remove empty family actors on every successful and failure path. Verify that a selection containing only below-threshold groups leaves the level unchanged. | P2.6 |

**Gate:** The synthetic fixture preserves transforms, effective materials, and other per-component behavior; failed instance adds and non-mesh actor payloads do not lead to source deletion; below-threshold groups leave no output; and Analyze predicts Build. Verify undo and rerun on the fixture. A real import still needs separate validation.

### Phase 3 — Define and protect the Datasmith reimport lifecycle

| ID | Owner files | Work and acceptance | Depends on |
|---|---|---|---|
| P3.1 | Validation note or fixture only | Trace a saved-level reimport: identify which actors/assets survive, which are replaced, and how managed outputs can be associated with the imported scene. Write the exact affected-boundary and source-identity contract before choosing a `FReimportManager` callback or an explicit rebuild command. Include a no-data fallback fixture. | Phase 2 gate |
| P3.2 | `Private/ConVerseHISMUtils.cpp/.h`, `Private/DatasmithHISM.cpp`, `Public/DatasmithHISM.h` as needed | Implement the P3.1 contract so a reimport neither doubles instances nor clears old managed output using only a partial set of new source actors. Bound cleanup to the affected import; preserve unrelated manual actors and managed groups. Test two consecutive reimports. | P3.1 |
| P3.3 | `Public/ConVerseHISMLibrary.h`, `Private/ConVerseHISMLibrary.cpp`, then `Private/ConVerseHISMUtils.cpp/.h` | First freeze an optional source-identity schema (source actor name and Datasmith element identifier when available; explicit missing-ID behavior). Then store a per-component, per-instance mapping that survives save/load and rerun. Keep default behavior and existing Blueprint fields compatible. Test index alignment after conversion and reimport. | P3.1; coordinate file ownership with P3.2 |
| P3.4 | `Private/Dataprep/*.cpp`, `Public/Dataprep/*.h` | Apply the settled lifecycle and metadata contracts to Create, Analyze, and category operations without interactive dialogs. Verify Dataprep Analyze remains read-only and reports the same group counts as Create for the same inputs. | P3.2, P3.3 |

**Gate:** A saved fixture survives two imports without duplicate or missing instances, unrelated content survives, and source identity can be queried where present. If the required import identity is unavailable, document the limitation and hold reimport automation rather than guessing from labels.

### Phase 4 — Expand coverage only where evidence supports it

| ID | Owner files | Work and acceptance | Depends on |
|---|---|---|---|
| P4.1 | Fixture files and a short design note | Define atomic handling of actors with multiple eligible mesh components. Specify which components may be converted together, when the actor can be deleted, and how partial failure is reported. Include a fixture with one eligible and one ineligible component. | Phase 2 gate |
| P4.2 | `Private/ConVerseHISMUtils.cpp/.h` | Implement P4.1 without deleting an actor while a source component remains. Analyze and Build must count components/actors consistently; undo restores the original actor. | P4.1; schedule after P3.2/P3.3 edits to these files |
| P4.3 | Benchmark fixture/log only, then `Private/ConVerseStaticMeshConsolidationUtils.cpp/.h` if justified | Profile exact signature computation and memory on representative large meshes; include the 10k import when available. Optimize caching or exact hashing only when measured. Never use a first-N-triangle hash as an equivalence decision because it can merge different meshes. | Phase 2 gate |
| P4.4 | `Public/ConVerseHISMLibrary.h`, `Private/ConVerseHISMLibrary.cpp` | Add per-category reporting only after a stable category source is identified in P3.1/P3.3. Define unknown-category behavior and verify totals equal converted instances. Skip this unit if no reliable category metadata exists. | P3.3 |

**Gate:** Multi-component actors either convert completely or remain intact, with accurate results. A measured profile supports any performance change, and equivalence remains exact by default.

### Phase 5 — Validate a real import and prepare a release decision

| ID | Owner files | Work and acceptance | Depends on |
|---|---|---|---|
| P5.1 | Validation report and disposable imported level only | Run Analyze then Managed ISMs on a representative Aeron/IFC import; record source actor count, distinct source assets, geometry/material groups, converted/retained/skipped counts, instance totals, time, memory, and before/after placement samples. The 10k-chair figures in this plan are a target case, not an observed result. | Phases 1–4 gates; real data available |
| P5.2 | Validation report only | Exercise both toolbar and Dataprep paths: Dedupe decline/accept/dry-run, Analyze, ISM/HISM selection, cancellation, undo/redo, save/reopen, Explode failure and success, and two reimports. Record exact pass/fail and log locations. | P5.1 |
| P5.3 | `README.md`, `AGENTS.md`, `PLAN.md`, `JOURNAL.md` | Update user-facing behavior and agent instructions to match tested code. Mark each legacy task done, deferred with reason, or still open. State Unreal versions actually built and known data limitations. One documentation owner edits these files after implementation evidence is integrated. | P5.2 |

**Gate:** Release claims require a successful integrated build, fixture checks, editor/Dataprep smoke checks, and a real imported map. If real data is unavailable, label the release decision pending; the earlier synthetic passes remain useful but do not substitute for P5.1.

### Legacy backlog disposition

The open rows below remain as source ideas. P2.2 covers A07; P3.2 covers A04; P3.3 covers A10; P4.2 covers A03; P4.3 covers B01; P4.4 covers E03; P5.1 covers T01. Defer A05 until exact-match behavior is validated on real data and measured drift creates a clear need. Defer A08 because the existing per-mesh signature cache already avoids repeat hashing of shared assets; a path-only shortcut can break grouping with separate equivalent assets. Defer Z01–Z04 until the five phase gates identify a concrete need. The proposed partial-triangle B01 shortcut must not be used as a final equivalence key.

---

## Session log

### Session 1 — Core ISM conversion + geometry grouping

**Problem solved:** IFC import creates one `UStaticMesh` per placed chair instance even when geometry is identical. The old pointer-equality grouping key meant 10,000 chairs → 10,000 ISMs, not 1.

**Changes:**
- `ConVerseHISMUtils.cpp` — replaced `FHISMGroupKey` mesh pointer with `GeometrySignature + MaterialSignature`; added `FHISMGroupData.CanonicalMesh` (alphabetically first asset path); switched all component creation from `UHierarchicalInstancedStaticMeshComponent` → `UInstancedStaticMeshComponent`; added `GetCachedMeshSignature()` with per-run cache to avoid rehashing the same asset multiple times; added `BuildMaterialSignature()` for per-component material variant detection
- `ConVerseStaticMeshConsolidationUtils.cpp/.h` — added public `GetMeshGeometrySignature()` (MD5 of sorted LOD0 triangle hashes: centroid-relative positions, normals, UVs, material slot names)
- `ConVerseBatchHISMLibrary.cpp` — `Settings.ISMComponentToUse = UInstancedStaticMeshComponent::StaticClass()`
- `DatasmithHISM.cpp` — toolbar/tooltip labels updated to "Managed ISMs" / "Batch ISMs"
- `.gitignore` — added (was missing)
- `README.md` — initial draft
- GitHub `jonathanmcmichael/DatasmithHISM` — force-pushed to replace blank template with real plugin source

**Key decisions:**
- ISM over HISM: HISM per-cluster culling adds overhead that is redundant and harmful with Nanite meshes
- Geometry signature falls back to asset path for unhashable meshes so those actors still group with others referencing the same broken asset
- Canonical mesh = alphabetically first asset path → deterministic across reruns without sorting the group every time

---

### Session 2 — Production hardening + cosmetic rename

**Changes:**
- `ConVerseHISMLibrary.cpp` — wrapped `CreateHISMsFromSelection` in `FScopedTransaction("Build Managed ISMs")`; added `#include "ScopedTransaction.h"` and `LOCTEXT_NAMESPACE`
- `ConVerseHISMUtils.cpp` — added two-phase `FScopedSlowTask`: Phase 1 (actor grouping, one `EnterProgressFrame` per actor with actor label), Phase 2 (ISM building, one frame per group); both `MakeDialog(true)` (cancellable); added single-instance filter (`Num < 2` → skip, increment `ActorsInSingleActorGroups`); added `#include "Misc/ScopedSlowTask.h"` and `LOCTEXT_NAMESPACE`
- `ConVerseStaticMeshConsolidationLibrary.cpp` — added `FMessageDialog::Open(YesNo)` before `ObjectTools::DeleteObjects`; suppressed in Dataprep/commandlet via `GIsEditor && !IsRunningCommandlet()`; added `#include "Misc/MessageDialog.h"`
- `ConVerseHISMLibrary.h` — `HISMActorsCreated` → `ISMComponentsCreated`; `FailedHISMActorCreations` → `FailedISMComponentCreations`; all `Category = "HISM"` → `"ISM"`; updated `CreateHISMsFromSelection` tooltip and default prefix to `TEXT("ISM")`
- `ConVerseCreateHISMOperation.cpp` — default prefix `"HISM"` → `"ISM"`; updated log string; fixed `HISMActorsCreated` reference
- `ConVerseStaticMeshConsolidationWidget.h` — `DisplayName` updated to "Datasmith ISM Static Mesh Consolidation Widget"
- `ConVerseHISMUtils.cpp::FinalizeSummary` — now includes `ActorsInSingleActorGroups` in summary string; all log/summary strings updated to "ISM"
- `DatasmithHISM.cpp` — fixed `RunCreateHISMs` passing `TEXT("HISM")` prefix (now `TEXT("ISM")`)
- `README.md` — full rewrite: ISM terminology throughout, geometry grouping explained, new features documented, result fields table
- `PLAN.md` — converted from one-shot checklist to living session log

**Key decisions:**
- `FScopedTransaction` placed at `CreateHISMsFromSelection` (library layer), not inside `BuildManagedHISMs` — keeps the utility function transaction-neutral so Dataprep can wrap it separately
- Single-instance actors not added to `ConvertedActorsAndCleanupBoundaries` — they survive the operation completely untouched
- Dedupe dialog suppressed headlessly so existing Dataprep pipelines don't break

---

### Session 3 — ISM/HISM creation-time toggle

**Changes:**
- `ConVerseHISMUtils.h/.cpp` — `BuildManagedHISMs` gains `bool bUseHISM = false`; `NewObject` call picks component class at runtime; stored pointer stays `UInstancedStaticMeshComponent*` (valid since HISM is a subclass)
- `ConVerseHISMLibrary.h/.cpp` — `CreateHISMsFromSelection` gains `bool bUseHISM = false`; passed through to `BuildManagedHISMs`
- `ConVerseCreateHISMOperation.h/.cpp` — `bUseHISM` UPROPERTY added (visible in Dataprep panel); wired through
- `AGENTS.md` — created; documents architecture, conventions, what to avoid, build instructions, domain context

**Key decisions:**
- `bUseHISM = false` default preserves existing Nanite-first behavior; opt-in for non-Nanite workflows
- Cleanup (`ClearManagedHISMComponents`) already catches both types via subclass query — no change needed there

---

### Session 4 — Cancel, min-threshold, auto ISM/HISM, rename, LOD1 fallback

**Tasks completed:** A01, A02, A09, E01, B02

**Changes:**
- `ConVerseHISMLibrary.h` — `FConVerseHISMCreationResult` gains `bool bWasCancelled`; new canonical `CreateISMsFromSelection(Prefix, bUseHISM, MinInstanceCount, bAutoDetectFromNanite)`; `CreateHISMsFromSelection` deprecated, now delegates to `CreateISMsFromSelection`
- `ConVerseHISMUtils.h` — `BuildManagedHISMs` gains `int32 MinInstanceCount = 2` and `bool bAutoDetectFromNanite = false`
- `ConVerseHISMUtils.cpp` — cancel: `ShouldCancel()` checked at the top of both the grouping and build loops; sets `bWasCancelled`; post-cancel cleanup destroys managed family-type actors that have no ISM components (orphan prevention); min-threshold: `< 2` replaced by `< MinInstanceCount`; auto ISM/HISM: `bResolvedUseHISM` checks `CanonicalMesh->NaniteSettings.bEnabled` when `bAutoDetectFromNanite` is true
- `ConVerseHISMLibrary.cpp` — `CreateISMsFromSelection` is the implementation; `CreateHISMsFromSelection` delegates to it
- `ConVerseCreateHISMOperation.h/.cpp` — `bAutoDetectFromNanite` and `MinInstanceCount` UPROPERTYs added; wired through to `BuildManagedHISMs`
- `DatasmithHISM.cpp` — toolbar `RunCreateHISMs` updated to call `CreateISMsFromSelection`
- `ConVerseStaticMeshConsolidationUtils.cpp` — LOD1 fallback: if LOD0 mesh description is null or empty, tries LOD1 before failing; hashes `UsedLODIndex` to prevent cross-LOD false matches

**Key decisions:**
- Cancel commits the partial result (what was built before cancel is kept); the transaction is not rolled back — user can Ctrl+Z to undo the full partial result if unwanted
- Orphaned managed family-type actors (created in Phase 1 but not populated in Phase 2 due to cancel) are explicitly destroyed to leave no empty shells
- `bAutoDetectFromNanite` overrides `bUseHISM` when enabled; default false preserves existing behavior
- `MinInstanceCount` defaults to 2 (existing behavior); exposed in Dataprep operation panel
- `UsedLODIndex` is included in the geometry hash so LOD1-sourced signatures never collide with LOD0-sourced signatures for the same geometry

---

## Historical agent orchestration (reference)

The five-phase work units above supersede the model-tier assignments and task order in this older section. Its file-stream map is still useful when assigning non-overlapping edits.

This section defines how multiple AI agents can work on this plugin in parallel without stepping on each other. The work is partitioned by file ownership; no two agents should edit the same file at the same time.

### Model tier assignment

| Tier | Model | Role |
|---|---|---|
| Architect | Opus / high-Sonnet | Feature design, interface definition, cross-cutting review, PLAN.md updates, final sign-off |
| Implementer | Sonnet | Implementing well-specified tasks, writing validation scripts, updating README |
| Worker | Haiku | Rote changes: string updates, boilerplate wrappers, config edits, comment-only changes |

**Rule:** An Architect agent produces a work unit specification (files to touch, exact interface changes, acceptance criteria). An Implementer or Worker agent executes that specification. No agent should both design and implement a non-trivial feature in the same session — the design pass catches interface conflicts before code is written.

### Work streams (parallel, non-overlapping)

Each stream owns its files exclusively during a work session. Sessions across streams can overlap.

```
Stream A  ConVerseHISMUtils.cpp/.h          Core grouping algorithm
Stream B  ConVerseStaticMeshConsolidationUtils.cpp/.h   Geometry hashing
Stream C  DatasmithHISM.cpp + new UI files  Toolbar, buttons, editor widgets
Stream D  Dataprep/*.cpp/.h                 Dataprep operation wrappers
Stream E  ConVerseHISMLibrary.h/.cpp        Blueprint API surface
Stream F  ConVerseStaticMeshConsolidationLibrary.h/.cpp  Dedupe API surface
Stream G  README.md / PLAN.md / AGENTS.md   Documentation only
```

Streams A and B interact at `GetMeshGeometrySignature` — if both are active, the Architect must freeze the signature function's interface before either stream begins.

Streams C and E interact at result display — if C adds a new toolbar button that calls a new E function, E's interface must be specified first.

Stream D depends on whatever A/E expose but never edits those files directly.

### Work unit format

Every work unit handed to an agent must include:

```
Task: <one-line description>
Stream: <letter>
Files: <exhaustive list of files to read and edit>
Interface contract: <function signatures, struct fields, or UPROPERTY names that must not change>
Acceptance criteria: <what "done" looks like — compiles, specific behavior, result fields populated>
Must not: <explicit list of things the agent must not do>
Agent tier: <Architect | Implementer | Worker>
Depends on: <task IDs that must be complete first, or "none">
```

---

## Historical backlog (superseded by five phases)

These rows record earlier ideas and completions. Use the phase IDs and legacy-backlog disposition above for the next work.

Tasks are grouped by stream. Hardness ratings: **E** (easy, <1h), **M** (medium, 1–4h), **H** (hard, 4–8h), **VH** (very hard, >8h or requires deep domain research).

### Must-do before shipping

| ID | Task | Stream | Hardness | Agent tier | Depends on |
|---|---|---|---|---|---|
| T01 | Build + test against real Aeron chair data. Validate 10,000 actors → small number of ISMs per material variant. Check result fields. | — | E | Human | — |

---

### Stream A — Core algorithm

| ID | Task | Hardness | Agent tier | Depends on |
|---|---|---|---|---|
| ~~A01~~ | ~~**Wire up cancel**~~ — done (Session 4) | M | — | — |
| ~~A02~~ | ~~**Configurable minimum instance threshold**~~ — done (Session 4) | E | — | — |
| A03 | **Multi-component actor support** — actors with multiple eligible SMCs are currently skipped. Define a grouping strategy: either split into separate entries (one per SMC, same family actor) or treat as a compound group. Architect must specify the strategy before implementation. | H | Architect → Implementer | — |
| A04 | **Reimport handling** — when Datasmith reimport fires, new source actors appear alongside existing managed ISMs, doubling instance counts. Options: (a) register a `FReimportManager` post-reimport callback that clears managed outputs on the affected boundary and rebuilds, or (b) detect pre-existing managed outputs at conversion time and skip source actors that are already instanced. Architect to evaluate. | H | Architect → Implementer | — |
| A05 | **Geometry tolerance parameter** — add a float tolerance (default 0) to `GetMeshGeometrySignature` comparisons so that meshes with floating-point drift from independent IFC imports (slightly different vertex positions) can still group. Must not break exact-match behavior when tolerance = 0. | VH | Architect → Implementer | B02 |
| ~~A06~~ | ~~**IFC spatial hierarchy awareness**~~ — done (Session 8). `StoreyBoundaryPatterns TArray<FString>` in `FFamilyTypeLookupCache`. `FindCleanupBoundaryActor` two-phase walk: find first non-mesh ancestor, then optionally continue upward to the first ancestor whose label matches any pattern (case-insensitive). Exposed as `UPROPERTY` on all three Dataprep operations. Empty array = original behavior. | H | — | — |
| A07 | **Pivot/anchor compensation investigation** — The geometry hash normalizes vertex positions by subtracting the mesh bounding-box center (`PositionOrigin`). Two meshes with identical geometry but different local origins (e.g., one centered at local (0,0,0), another at (100,0,0)) hash identically. If the canonical mesh and a non-canonical source mesh have different local origins, substituting the canonical mesh and using the source actor's transform via `Actor->GetActorTransform()` may offset instances by the difference in origins. The code assumes Datasmith flattens all mesh pivots to the actor origin — verify this holds for real IFC imports during T01. If not: store `PositionOrigin` per mesh during the hashing pass and apply a corrective local-space translation when calling `AddInstance`. | H | Implementer | T01 |
| A08 | **Mesh-pointer fast path** — Before computing the geometry hash for a source component, check whether its `UStaticMesh*` already appears in the signature cache with a known signature. If multiple actors share the exact same asset (i.e., Datasmith did reuse it), they will all produce the same cache hit with zero rehashing. This is already handled by `GetCachedMeshSignature`. However, also add a pre-pass shortcut: if a group's entire candidate set shares one `UStaticMesh*`, skip hashing entirely and use the asset path as the group signature directly. Avoids hashing overhead for the common case where Datasmith correctly shared the asset. | E | Worker | — |
| ~~A09~~ | ~~**Auto ISM/HISM selection based on Nanite status**~~ — done (Session 4) as `bAutoDetectFromNanite`; tri-state enum deferred | M | — | — |
| A10 | **BIM metadata preservation** — When source actors are destroyed, any Datasmith metadata (element GUIDs, Revit parameter values) on those actors is lost. Add an optional `FManagedInstanceSourceMap` stored on the managed family-type actor: maps each ISM instance index to the source actor name and Datasmith element GUID. Allows "which Revit element is this instance?" to be answered after conversion. Gated by a `bPreserveSourceMetadata` flag (default false — no overhead if not needed). | H | Implementer | — |
| ~~A11~~ | ~~**Grouping mode**~~ — done (Session 5). `EConVerseGroupingMode::PreserveBIMHierarchy` (default) and `MaximumOptimization`. Exposed in Blueprint API and Dataprep operation. | M | — | — |

---

### Stream B — Geometry hashing

| ID | Task | Hardness | Agent tier | Depends on |
|---|---|---|---|---|
| B01 | **Hash performance for large meshes** — `GetMeshGeometrySignature` reprocesses every triangle in LOD0. For meshes with >100k triangles, profile whether hashing dominates runtime. If so, add an early-exit: hash only the first N triangles (configurable), accepting a small false-positive risk in exchange for speed. | M | Implementer | T01 |
| ~~B02~~ | ~~**LOD1 fallback hash**~~ — done (Session 4) | E | — | — |
| ~~B03~~ | ~~**Normalization audit for IFC coordinates**~~ — done (Session 8, analytical). `PositionOrigin = PositionBounds.GetCenter()` operates on mesh-description-local vertices. Regardless of whether Datasmith bakes IFC survey-point offsets into actor transforms (bounds near origin, normalization is a near-no-op) or into vertex positions (centroid subtraction cancels the absolute offset), centroid-relative per-triangle positions are identical for geometrically equivalent meshes. T01 should provide empirical confirmation. | M | — | — |

---

### Stream C — UI / toolbar

| ID | Task | Hardness | Agent tier | Depends on |
|---|---|---|---|---|
| ~~C01~~ | ~~**Analyze / dry-run button**~~ — done (Session 5). "Analyze ISMs" toolbar button + `AnalyzeISMCandidatesInSelection` Blueprint function. Runs grouping phase only, no changes. Returns `FConVerseHISMAnalysisResult`. | M | — | — |
| ~~C02~~ | ~~**Enable Nanite button**~~ — done (Session 5). "Enable Nanite" toolbar button + `EnableNaniteOnSelection` Blueprint function. Enables Nanite on all mesh assets in selection and queues async rebuilds. Returns `FConVerseEnableNaniteResult`. | M | — | — |
| ~~C03~~ | ~~**Explode ISMs button**~~ — done (Session 6). "Explode ISMs" toolbar button + `ExplodeISMsFromSelection` Blueprint function. Spawns one `AActor` + `UStaticMeshComponent` per ISM instance, destroys ISM components and empty family-type actors. Returns `FConVerseISMExplodeResult`. | H | — | — |
| ~~C04~~ | ~~**One-click pipeline button**~~ — done (Session 6). "Dedupe + ISMs" toolbar button runs `ConsolidateSimilarStaticMeshesInSelection` then `CreateISMsFromSelection`, shows combined result dialog. | E | — | — |
| ~~C05~~ | ~~**ISM/HISM toggle in toolbar**~~ — done (Session 7). "Use HISM" toggle button next to Managed ISMs. `bToolbarUseHISM` member on the module, persisted to `GEditorPerProjectIni` via `GConfig`. Affects Managed ISMs, Analyze ISMs, and Dedupe + ISMs. | M | — | — |

---

### Stream D — Dataprep

| ID | Task | Hardness | Agent tier | Depends on |
|---|---|---|---|---|
| ~~D01~~ | ~~**Expose minimum instance threshold in Dataprep operation**~~ — done (Session 4) | E | — | — |
| ~~D02~~ | ~~**Category-aware Dataprep operation**~~ — done (Session 7). `UConVerseCategoryGroupHISMOperation` with `CategoryFilter` (case-insensitive label substring). Empty filter = all actors. Same ISM parameters as Create ISMs operation. | M | — | — |
| ~~D03~~ | ~~**Dry-run Dataprep operation**~~ — done (Session 6). `UConVerseAnalyzeHISMOperation` runs `AnalyzeManagedHISMCandidates` and logs the result breakdown. Same parameters as `UConVerseCreateHISMOperation`. | E | — | — |

---

### Stream E — Blueprint API surface

| ID | Task | Hardness | Agent tier | Depends on |
|---|---|---|---|---|
| ~~E01~~ | ~~**`CreateHISMsFromSelection` rename**~~ — done (Session 4) | E | — | — |
| ~~E02~~ | ~~**Result struct expansion**~~ — done (Session 7). Added `ISMOnlyComponentsCreated` and `HISMOnlyComponentsCreated` to `FConVerseHISMCreationResult`. `ISMComponentsCreated` stays as total for backward compat. `FinalizeSummary` shows "X ISM + Y HISM" breakdown when `bAutoDetectFromNanite` produces both types. | M | — | — |
| E03 | **Per-category result breakdown** — `FConVerseHISMCreationResult` gets a `TMap<FString, int32> InstancesPerCategory` field. Populated from actor tags or Datasmith metadata if available. No-op (empty map) if metadata is absent. | M | Implementer | — |

---

### Stream F — Dedupe API surface

| ID | Task | Hardness | Agent tier | Depends on |
|---|---|---|---|---|
| ~~F01~~ | ~~**Tag migration utility**~~ — done (Session 5). `MigrateTagsInCurrentLevel(OldTagName, NewTagName)` Blueprint function. Replaces actor and component tags level-wide, wrapped in `FScopedTransaction`. | E | — | — |
| ~~F02~~ | ~~**Dedupe dry-run mode**~~ — done (Session 5). `bDryRun = false` parameter on both `ConsolidateSimilarStaticMeshes` overloads. Dry-run skips replace+delete and returns a "[Dry run]" summary. | M | — | — |

---

### Low priority / deferred

| ID | Task | Hardness | Depends on |
|---|---|---|---|
| Z01 | Linked Revit model handling — linked models arrive as separate actor subtrees with their own cleanup boundaries; the current single-pass operation handles them correctly but produces separate managed actors per link; evaluate whether cross-link grouping is desirable | VH | — |
| Z02 | Per-instance material override detection — Revit instance parameters can override material per placed instance; these should produce separate ISM groups even when geometry and default material match | H | A03 |
| Z03 | LOD generation trigger after Nanite enable — after C02, optionally trigger LOD generation for meshes where Nanite is not suitable (transparent, masked) | M | C02 |
| Z04 | Commandlet support — headless `UConVerseHISMCommandlet` for CI/pipeline use; no dialogs, writes result to stdout as JSON | H | — |

---

## Architecture reference

```
ConVerseHISMLibrary.cpp              ← Blueprint/toolbar entry; FScopedTransaction here
  └─ ConVerseHISMUtils.cpp           ← BuildManagedHISMs; grouping + ISM creation; FScopedSlowTask here
       └─ ConVerseStaticMeshConsolidationUtils.cpp  ← GetMeshGeometrySignature (LOD0 MD5)

ConVerseStaticMeshConsolidationLibrary.cpp  ← Dedupe Meshes entry; confirmation dialog here
  └─ ConVerseStaticMeshConsolidationUtils.cpp       ← BuildMeshSignature, AnalyzeMeshes

ConVerseBatchHISMLibrary.cpp         ← Batch ISMs (Unreal built-in MergeComponentsToInstances)

Dataprep/ConVerseCreateHISMOperation.cpp             ← Dataprep wrapper for BuildManagedHISMs
Dataprep/ConVerseConsolidateSimilarMeshesOperation.cpp  ← Dataprep wrapper for Dedupe Meshes
```

**Grouping key:** `FHISMGroupKey { FamilyTypeActor, GeometrySignature, MaterialSignature }`
- `GeometrySignature` — MD5 of sorted LOD0 (or LOD1 fallback) triangle hashes (centroid-relative position, normal, UVs, material slot names); falls back to asset path on failure
- `MaterialSignature` — ordered `GetMaterial(index)->GetPathName()` per slot; separates colour/finish variants into distinct ISMs
- `CanonicalMesh` — alphabetically first asset path in the group; deterministic across reruns

**Managed output tags** (preserved for backward compat with existing levels):
- `ConVerseManagedHISM` — on ISM components
- `ConVerseManagedFamilyType` — on managed family-type actors

**ISM vs HISM:**
- `bUseHISM = false, bAutoDetectFromNanite = false` (default) — always `UInstancedStaticMeshComponent`
- `bUseHISM = true` — always `UHierarchicalInstancedStaticMeshComponent`
- `bAutoDetectFromNanite = true` — ISM if `CanonicalMesh->NaniteSettings.bEnabled`, HISM otherwise; overrides `bUseHISM`
- `ClearManagedHISMComponents` handles both types (HISM is a subclass of ISM)

**Blueprint API (current):**
- `CreateISMsFromSelection(Prefix, bUseHISM, MinInstanceCount, bAutoDetectFromNanite, GroupingMode)` — canonical (StoreyBoundaryPatterns only via Dataprep/C++ API; not a UFUNCTION param — UHT limitation)
- `AnalyzeISMCandidatesInSelection(Prefix, bUseHISM, MinInstanceCount, bAutoDetectFromNanite, GroupingMode)` — dry-run, no changes
- `ExplodeISMsFromSelection()` — reverse: spawns individual actors from ISM instances
- `EnableNaniteOnSelection()` — enables Nanite on all mesh assets in selection
- `MigrateTagsInCurrentLevel(OldTagName, NewTagName)` — level-wide tag migration
- `CreateHISMsFromSelection(Prefix, bUseHISM)` — deprecated, delegates to above

**Dataprep operations (current):**
- `UConVerseCreateHISMOperation` — Managed ISMs (Prefix, bUseHISM, bAutoDetectFromNanite, MinInstanceCount, GroupingMode, StoreyBoundaryPatterns)
- `UConVerseAnalyzeHISMOperation` — dry-run counterpart; same parameters; no changes
- `UConVerseCategoryGroupHISMOperation` — Managed ISMs with label-substring category filter (CategoryFilter + all Create ISMs params incl. StoreyBoundaryPatterns)
- `UConVerseConsolidateSimilarMeshesOperation` — Dedupe Meshes (bRequireMatchingMaterials)
