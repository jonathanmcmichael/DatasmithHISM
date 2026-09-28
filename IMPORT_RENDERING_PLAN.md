# Import rendering policy and acceptance

Updated 2026-09-26. The earlier proposal is [archived](Docs/History/2026-09-26/IMPORT_RENDERING_PLAN.md). These features are implemented in the current development build; calibrated/rendered acceptance remains open.

At the 2026-09-27 closeout, the later [Phase 2 evidence](Docs/Validation/2026-09-26-phase2.md) adds persistence/recovery checks and an editor-process lookup check. It adds no rendered, calibrated-light or performance acceptance; the remaining gates below still apply.

| Area | Current behavior | Remaining acceptance |
|---|---|---|
| Nanite | All supported imported meshes by default; converted ISM only and preserve options; exact mesh exceptions; effective blend-mode checks; finish compilation and verify Nanite data | Broader material/platform compatibility, real compiler failure, rendered thin/mirrored geometry, LOD/culling |
| Lights | Inventory enabled local lights and units/IES; threshold 100 advisory; observe MegaLights project setting; preserve and verify exported values | Revit numerical references, physical falloff/exposure, all supported light types, IES on/off |
| Materials | Reviewed exact-fingerprint replacement of mesh defaults and component overrides before Nanite processing | Stock-library identity coverage, visual/physical scale checks, reviewed targets |
| Geometry | Owned ordinary and instanced meshes are accounted for | Corrected joist payload and complete aligned models; missing diagonals cannot be reconstructed from absent source data |
| Performance | Import duration, mesh-policy/build interval, compilation wait, process-lifetime peak memory | Fixed baseline scenes/camera paths and CPU/GPU frame-time comparison |

Output-changing rendering choices participate in PlanId. Advisory thresholds and observed global rendering settings do not. Missing/incompatible inputs receive diagnostics; approval cannot be inferred from a material name or light unit label. Headless and panel execution use the same service policy.

See [Nanite ADR](Docs/ADR/0003-geometry-and-nanite.md), [lighting ADR](Docs/ADR/0004-lighting-provenance.md), [materials ADR](Docs/ADR/0005-reviewed-material-matching.md), and [validation procedure](Docs/VALIDATION.md).
