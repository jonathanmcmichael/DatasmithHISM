# Geometry survey and mirrored-instance engine check, 2026-10-29

Follow-up to the [grouping breakdown](2026-10-29-grouping-breakdown.md): are the "unique" meshes really unique, and can mirrored instances share a group? Read-only; UE 5.6.1; same three Warrenton exports.

## Geometry survey

New read-only commandlet mode `-run=ConVerseOptimizedImport -GeometrySurvey=<scene.udatasmith> -SurveyFile=<out.json> [-MinInstances=2]`. It parses the scene XML, loads every placed mesh payload (LOD0, non-collision), and hashes three signatures per mesh element, each including the sorted material slots:

- **Exact**: positions as stored (1 mm quantum), triangle corners, UV0 and (from the second build) normals. Equal means the elements are the same mesh under different names; sharing needs only a reference change.
- **ShapeAndUV**: as Exact, but positions relative to the vertex centroid. Equal means the copies differ only by a baked translation; sharing also needs the centroid offset added to each instance transform.
- **Shape**: sorted centroid-relative positions and triangle count only. Upper bound: also counts copies whose UVs differ (likely world-mapped), which cannot share without moving texture placement.

Raw output: host `Saved/Benchmarks/GeometrySurvey/{HKS_A,MBC_CW,Arch3}.json`; logs `Saved/Logs/GeometrySurvey_*.txt`. Exit code 1 is only the known UE 5.8 template-asset errors; no payload was unreadable.

"Actors groupable" counts actors whose signature has at least 2 placements, ignoring parent, settings and skip rules, so it is an upper bound for each signature kind. The mesh-element baseline tracks the importer's real plan closely (3,938 vs 3,928 planned instances on MBC_CW).

| | HKS_A | MBC_CW | Arch3 |
|---|---:|---:|---:|
| Mesh actors / placed mesh elements | 4,049 / 1,765 | 8,154 / 4,245 | 10,408 / 4,840 |
| Groupable by mesh element (today) | 2,464 | 3,938 | 6,341 |
| Exact (rename only) | 2,801 (+337) | 3,945 (+7) | 7,052 (+711) |
| **ShapeAndUV (translation removed)** | **2,846 (+382)** | **7,921 (+3,983)** | **7,714 (+1,373)** |
| Shape (upper bound) | 3,247 | 7,973 | 8,249 |
| Distinct ShapeAndUV signatures | 1,469 | **477** | 3,559 |

Largest new curtain-wall classes: `Piles_TEST_CW1` 883 meshes, 883 actors; `Footing-Spread_F8-10` 237 meshes, 237 actors; a further `Piles_TEST_CW1` class of 204. Each is one Revit family type exported as one mesh per placement, with the placement baked into the geometry (every actor shares the same transform).

Cross-check: a separate XML-only estimate (label, size, area, materials) predicted 4,061 rescuable curtain-wall meshes; nine sampled pairs exported with `-InspectMesh` matched in 7 cases once translation was removed (2 already identical).

Not compared in the table: normals (added to Exact and ShapeAndUV afterwards; see below), tangents, UV channels above 0, LODs above 0, collision meshes, and actor material overrides. A real implementation must prove equivalence on these or fail closed, as Phase 3 already requires.

## Mirrored instances (engine source, UE 5.6)

- **Nanite**: `NaniteRasterizer.usf` `ReverseWindingOrder` flips the winding per instance from `INSTANCE_SCENE_DATA_FLAG_DETERMINANT_SIGN`, and flips again if the primitive sets reverse culling. Mixed-sign instances in one Nanite component render correctly.
- **Non-Nanite ISM/HISM**: `InstancedStaticMesh.cpp` sets one `MeshBatch.ReverseCulling` per component. Mirrored instances in a positive-determinant group would show back faces (unless two-sided).
- Non-Nanite culling follows the component's own determinant: `FStaticMeshSceneProxy::ShouldRenderBackFaces` returns `bReverseCulling != IsLocalToWorldDeterminantNegative()`.
- Design consequence ([ADR 0010](../ADR/0010-equivalent-meshes-and-mirrored-groups.md), proposed): give a mirrored-only group a mirrored component transform and positive-determinant instance transforms, so both paths flip correctly without `bReverseCulling` and the later Nanite step needs no special case. Shadows, tangent-space normals and lighting still need a check on a real RHI. NullRHI tests cannot show culling.

## Automation and the normals re-run

`DatasmithHISM.OptimizedImport.GeometrySurveyFindsTranslatedCopies` passes on UE 5.6.1 (host `Saved/Logs/GeometrySurveyTest-UE56.txt`). Its fixture has a renamed copy, a baked-translation copy, a baked copy with shifted UVs, an unrelated size and one mesh placed twice. It asserts each class's signatures, groupable actors and rescued meshes, and that a missing scene fails. Normals were then added to the Exact and ShapeAndUV signatures (the test still passes; its quads share one normal). The three exports were then re-run with normals included: HKS_A and Arch3 are unchanged, and MBC_CW ShapeAndUV moves by one mesh (7,920 actors groupable, 478 signatures, 3,982 rescued). Normals therefore do not split any meaningful class; the JSON files hold the re-run values.

## Conclusion

Phase 0's "unique mesh" class is mostly an artefact of the Revit export. On the curtain wall, sharing translated copies would roughly double instanced actors (3,938 to 7,921) and cut placed meshes from 4,245 to 477. This replaces payload-hash dedupe as Phase 3's method and makes it the largest lever measured so far. Mirrored grouping is feasible in a split-by-sign form.
