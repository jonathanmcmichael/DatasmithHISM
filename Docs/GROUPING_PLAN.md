# Grouping compression plan

Date: 2026-10-29. Status: **Phase 0 implemented (contract Amendment 20) and run on three Warrenton exports ([evidence](Validation/2026-10-29-grouping-breakdown.md)); awaiting the lever decision.** Phases 1-4 not started. A follow-up [geometry survey](Validation/2026-10-29-geometry-survey.md) found that most "unique" meshes are translated copies, and checked mirrored-instance support in the engine source; see the revised order below.

**Phase 0 result.** Unique meshes are 78-99.8% of below-minimum actors; mirrored instances are the largest instancing lever on architecture models (685 and 870 actors); parent split is moderate (10-996); settings split and byte-identical payload duplicates are zero; curtain-wall groups span the scene. Revised order, for decision: Phase 1 (cells, curtain wall), a new **mirrored-instance phase** (needs an engine check of negative-determinant ISM/HISM instances in UE 5.6/5.8 before any design), then Phase 2. Phase 3 is deprioritized unless a normalized-geometry signature finds duplicates. Phase 4 (HLOD first) is the only lever for the unique majority.

**Geometry survey result (supersedes the Phase 3 and Phase 4 ranking above).** A centroid-relative signature with UVs and material slots finds that most placed-once meshes are copies with a baked translation. Groupable actors (upper bound, before parent and skip rules): curtain wall 3,938 to 7,921, Arch3 6,341 to 7,714, HKS_A 2,464 to 2,846. Placed curtain-wall meshes drop from 4,245 to 477 signatures. Engine source shows Nanite flips culling per instance for mirrored transforms, while non-Nanite ISM/HISM uses one cull mode per component. Revised order for decision: **Phase 3 as geometry dedupe** (largest lever, see below), mirrored grouping with a mirrored component transform (both proposed in [ADR 0010](ADR/0010-equivalent-meshes-and-mirrored-groups.md)), Phase 1 cells, then Phase 2. Phase 4 only for what is still unique afterwards. The panel's **Advanced import** tab shows each result's converted / could-group / stays-ordinary split and the further steps that apply; later phases should add their controls there. This plan expands [PLAN.md workstream 2](../PLAN.md#2-explain-ordinary-leftovers-before-changing-grouping) into ordered phases. It does not amend the [import contract](../IMPORT_PANEL_VALIDATION.md) or authorize any output change by itself: each output-changing phase stops for a scope decision, a contract amendment and an ADR before code.

## Problem

On CWM, 2,561 of 7,101 source mesh actors (36%) become instances in 379 groups; 4,540 stay ordinary ([evidence](Validation/2026-10-02-cwm-source.md)). The [packaged comparison](Validation/2026-10-15-runtime-comparison.md) shows exterior draws down 0.69-6.93% and interior draws **up** 4.94-8.65%. So there are two separate questions: why so many actors stay ordinary, and whether the groups we do make are too spatially large to cull well.

Current grouping ([`BuildSerializedGroupKey`](../Source/DatasmithHISM/Private/ConVerseDatasmithImportService.cpp) / `GatherPlanCandidates`) keys on immediate parent, exact mesh reference, overrides, layer, component flag, visibility, shadow and mobility. Mesh actors with children, explicit keep-ordinary meshes, unresolved mesh/material references, non-finite transforms and negative determinants are skipped. Today `HasChildren` also counts explicit keep-ordinary exceptions, and every non-instance row reads "Ordinary: not eligible or below threshold", so the report cannot say which rule costs the most.

## Phase 0 - Ordinary-leftover breakdown (read-only, no PlanId change)

Goal: account for every source mesh actor exactly once and estimate what each lever could save.

1. Split `EConVerseOptimizedSkipReason::HasChildren` from a new `ExplicitKeepOrdinary` reason. Give each inspection row its specific reason instead of the shared text.
2. For eligible actors that end up below the minimum, add counterfactual classes computed from the existing buckets:
   - **Unique mesh reference**: the mesh element is placed once in the whole scene. No instancing lever applies; only merging (Phase 4) or deduplication (Phase 3) could help.
   - **Parent split**: the same group key minus `Parent` would reach the minimum. Lever: Phase 2.
   - **Settings split**: same mesh and parent, differing only in overrides/layer/visibility/shadow/mobility. Report which field splits.
   - **Payload duplicate**: a different mesh element with an identical payload hash would reach the minimum. Lever: Phase 3. Confirm first which hash the UE 5.8 Datasmith mesh element exposes, and whether it is stable across element names.
3. Report how many `HasChildren` and `UnsupportedNegativeScale` actors would otherwise group, so their cost is visible.
4. Add per-group spatial metrics: world bounds extent, instance count and instances per cubic metre. Flag groups whose bounds cover most of the scene.
5. Emit everything in the Analyze result and `-MetricsFile`, with a short panel summary.

Tests (automation, editor closed): the classes partition `TotalMeshActors`; Analyze creates no scene assets/actors; generated fixtures for each class (parent split, settings split, unique, payload duplicate) land in the right class; totals match the import's group/ordinary inventory.

Build: full C++ build (new enum values, result fields; check for reflected types).

**Gate:** a dated CWM evidence note with the breakdown and the spatial metrics, ranking levers by ordinary actors and primitive components they could remove. Then choose which of Phases 1-4 to pursue; skip phases whose lever is small.

## Phase 1 - Spatial cell splitting of large groups (output change)

Most likely fix for the interior draw regression, and lowest risk because it only splits existing groups, never merges new ones.

- New option `GroupCellSize` (cm; 0 = off, default off). When set, add the cell index of each instance's bounds centre to the bucket key, so one group becomes several per-cell groups under the same parent.
- Normalize/clamp in `NormalizeAndValidateOptions`; add to `ComputePlanId` with a `CellSizeAffectsPlanIdentity` test.
- Verification is unchanged in kind (counts, order, transforms, placement per group).
- Contract amendment: groups may be split by cell; identity and ordering rules per group.
- Measure: repeat the packaged ordinary/ISM/HISM comparison with cells off vs 1-2 cell sizes on the same views.

**Gate:** interior draws/GPU time no worse than ordinary on the controlled machine, without losing the exterior gain.

## Phase 2 - Cross-parent grouping within a cell (only if parent splits dominate)

- Option `GroupingScope = Parent | Cell` (default `Parent`). `Cell` drops `Parent` from the key and requires a nonzero `GroupCellSize`. The group's component lives on a new owned cell actor in world space.
- Keep every parent scene component as today; only the leaf mesh moves into the instance. Consequence for the contract: moving a parent later no longer moves those instances. Tracked-edit detection must treat the parent transform as part of the instance baseline.
- Verify world transforms (not parent-relative) and source lookup per instance. Lights and other children under the old parent are unaffected.
- Needs an ADR, a contract amendment, PlanId coverage and rollback/supersede tests for the new cell actors.

## Phase 3 - Payload deduplication (only if payload duplicates dominate)

Supersedes part of ADR 0003 ("broad cross-asset deduplication stays outside tracked import"), so it needs a new ADR first.

- Pre-import: treat mesh elements with an equal geometry signature (the survey's `ShapeAndUV`: centroid-relative positions, triangle corners, UV0 and material slots; not the Datasmith payload hash, which is distinct for every Revit mesh) and compatible effective materials as candidates. Rewrite their actors to one canonical mesh element in the Datasmith scene, before import. For translated copies, fold the centroid offset (rotated and scaled by the actor transform) into the actor's transform; verification then checks world-space placement, not the source actor transform. The `Exact` class needs no transform change.
- Post-import: prove equivalence on the imported canonical vs. one representative duplicate (vertices, indices, UVs, sections, material slots, collision, LODs). Fail closed: on mismatch, roll back to separate meshes.
- Record the original mesh element name per instance so source identity survives.
- Optional follow-on: fingerprint-identical materials with different names, using the material review fingerprints.

## Phase 4 - Merging unique leftovers (separate scope; evaluate HLOD first)

For actors placed once, instancing cannot help.

1. First measure World Partition HLODs on the CWM map. They reduce distant primitives without changing tracked ownership.
2. Only if near-field primitive count is the measured bottleneck: merge ordinary meshes per cell and material into one owned static mesh, with a section/triangle-range to source-element map in runtime metadata. Costs to accept explicitly: per-object culling and selection, editability, and rollback/verification of merged assets.

## Order and decision points

1. Phase 0 now (read-only, fits the current roadmap).
2. Phase 1 next; it also serves [roadmap decision 1](../ROADMAP.md#next-decisions-in-order) by diagnosing the interior regression.
3. Phase 2 or 3, whichever Phase 0 ranks higher, after a scope decision.
4. Phase 4 only after the runtime comparison shows ordinary primitives are the bottleneck.

Each output-changing phase: contract amendment and ADR before code, PlanId test, focused automation, full suite, CWM import verified in both modes, dated evidence note, handoff status update after real runs. Do not loosen eligibility just to raise the instance count.
