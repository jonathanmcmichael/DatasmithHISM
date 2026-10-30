# ADR 0010: Share geometry-equivalent meshes and group mirrored instances

Date: 2026-10-29. Status: **Proposed** (awaiting the user's scope decision). If accepted, it supersedes two sentences of [ADR 0003](0003-geometry-and-nanite.md): "Keep unsupported/mirrored/parent-bearing cases ordinary" (for mirrored cases only) and "Broad cross-asset deduplication stays outside tracked import scope" (for proven-equivalent payloads only). Nothing here is implemented; the [contract](../../IMPORT_PANEL_VALIDATION.md) is unchanged until an amendment is accepted.

## Context

The [geometry survey](../Validation/2026-10-29-geometry-survey.md) shows Revit writes one mesh element per placement for many family types, baking the placement into the vertices. On the Warrenton curtain wall 4,245 mesh elements reduce to 477 signatures once translation is removed, and instanceable actors rise from 3,938 to 7,921 (upper bound, before parent and skip rules). The Datasmith payload hash is distinct for every such mesh, so Amendment 20's payload-duplicate class finds none.

Mirrored actors (negative determinant) are the second lever: 685 and 870 actors on the architecture exports. UE 5.6 source shows Nanite reverses winding per instance, but non-Nanite ISM/HISM picks one cull mode per component (`FStaticMeshSceneProxy::ShouldRenderBackFaces` = `bReverseCulling != IsLocalToWorldDeterminantNegative()`).

ADR 0003 forbids inferring equivalence "from family labels or bounds". This decision keeps that rule: equivalence comes only from comparing payload geometry.

## Decision (proposed)

Products below are in matrix notation: the right-hand factor is applied first, so in Unreal's `FTransform` order `A × B` here is `B * A`.

### 1. Equivalent-mesh sharing, opt-in

- New option `ShareEquivalentMeshes` (default off) in options, presets and `ComputePlanId`.
- **Candidates**: mesh elements with the same survey `ShapeAndUV` signature and the same material slots.
- **Proof, fail closed**: before rewriting, the pair must match on every LOD and the collision mesh. The checks are:
  - triangle count, and triangle corners in order;
  - polygon-group (material slot) assignment;
  - each vertex position, after removing the centroid offset, within `EquivalenceToleranceCm` (default 0.05 cm, above float32 noise at kilometre offsets);
  - normals, tangents and every UV channel within 1e-3.
  A pair that fails any check stays separate and is reported, not shared.
- **Canonical element**: the candidate with the lowest element name (deterministic). The canonical payload is not modified or recentred, so ADR 0003's "preserve geometry" still holds and the pivot stays at the exporter's origin.
- **Rewrite**: a sharing actor's mesh reference becomes the canonical element. Its transform becomes `SourceTransform × Translate(c_self − c_canonical)`, with the offset in mesh-local space. For the `Exact` class the offset is zero.
- **Identity**: the manifest records each instance's original mesh element name and local offset. The group key uses the canonical element, so GroupIds of groups that gain sharers change; PlanId already changes through the option.
- **Verification**: placement is checked in world space against `SourceTransform × offset`, index to index as today. Source lookup must return the original element name.
- **Interaction**: sharing runs before grouping, so shared actors still pass every eligibility rule (parent, settings, children, minimum). Unused original mesh elements are not imported.

### 2. Mirrored groups, opt-in

- New option `GroupMirroredInstances` (default off). It lifts `UnsupportedNegativeScale` for finite, non-zero-scale transforms whose determinant is negative.
- **Key**: append the determinant sign to the group key only when negative, so existing GroupIds are unchanged.
- **Component mirror**: a mirrored group's component gets a fixed mirror in its relative transform (scale X = −1). Each instance stores `Mirror⁻¹ × SourceRelativeTransform`, which has a positive determinant. Every instance's world transform is unchanged. Non-Nanite culling then flips through the component determinant, and Nanite through each instance's world determinant. No `bReverseCulling` flag is set, so a later Nanite step needs no special case.
- **Verification**: compare composed world transforms (component × instance) with the source world transform. The manifest records that the group is mirrored.

## Alternatives

- Mixed-sign groups with `bReverseCulling`: wrong for non-Nanite, and would double-flip after the Nanite step enables Nanite.
- Generating mirrored copies of mesh assets: doubles assets and changes payloads.
- Recentring canonical payloads: better pivots, but changes geometry; it could be a later step.
- Payload-hash dedupe (old Phase 3): finds nothing on Revit exports.

## Consequences and required evidence before acceptance

- Contract amendment (options, keys, manifest fields, verification, reporting) and PlanId tests for both options.
- Automation tests:
  - a baked-translation pair is shared and verified in world space;
  - a near-miss pair (normal, UV or collision difference) stays separate;
  - a mirrored group is verified, survives the Nanite step and rolls back.
- A real-RHI visual check of mirrored groups (Nanite and non-Nanite, shadows and normal maps), because NullRHI cannot show culling.
- A curtain-wall import in both modes with a dated evidence note.
- Risk: shared meshes stay pivoted at the exporter origin, so editor selection and pivot gizmos look odd on them. Bounds are unaffected.
