# Remaining release work

Updated 2026-09-28 after [verification fixes (Amendments 9-11)](Docs/Validation/2026-09-28-verification.md): UE 5.8.3 editor build and **38/38 automation tests passed**. Each new test failed before its fix; nothing has been re-checked live. Earlier [live UI acceptance (32/32)](Docs/Validation/2026-09-27-live-ui.md) found two defects with fixes and tests (not re-verified live). [Phase 2 persistence/recovery evidence](Docs/Validation/2026-09-26-phase2.md) covers named-copy refusal, partial saves, manual drift and one actual interrupted-process checkpoint. Windows packaged smoke evidence remains separate. The [execution ledger](ROADMAP_EXECUTION.md) distinguishes implementation from incomplete gates.

**Next agent:** start with [HANDOFF.md](HANDOFF.md#next-agent-assignment) (ordered live re-check checklist for the user). The user must free the desktop for interactive sessions. Source corrections below remain the first dependency for full geometry acceptance; they do not prevent small-source UI acceptance.

| Order | Work and owner | Dependency | Done when |
|---|---|---|---|
| 1 | Source author + geometry validation: correct the joist export and restore structural/HVAC textures | Revit source and complete sidecars | Corrected payload contains known diagonals; keep negative fixture; normal Datasmith/ordinary/ISM/HISM and independent Nanite comparisons retain them. |
| 2 | Geometry validation: complete structural/HVAC alignment and fidelity | Complete exports from step 1 | Known dimensions/elevations/reference points, nested links, mirrors, distant coordinates, sections/normals/visibility and rendered thin members agree. |
| 3 | Lighting validation: obtain authoritative Revit values and calibrate | Source-family photometrics, IES and reference exposure | Units/intensity/falloff and IES on/off agree for relevant light types, or unresolved cases remain explicit. |
| 4 | Materials owner: curate identities and approved replacements | Library/version evidence and candidate Unreal targets | Stock/custom/renamed/ambiguous variants, texture scale/orientation, missing targets and changed evidence are reviewed; distinguish recognized coverage from replacement coverage. |
| 5 | UI/persistence owner: exercise live controls and broader faults | Built plugin and disposable maps | Analyze/cancel, material review, source focus, previews and preset restore have live evidence. Broaden Content Browser rename/move and interruption stages; actual full-volume behavior remains separate from the passing bounded simulation. |
| 6 | Runtime/performance owner: broaden packaged/rendered acceptance | Earlier source/material/light gates and fixed scenes/camera paths | Textures/UVs, supported lights, collision/navigation/LOD/culling and lookup work in a cooked application; baseline/candidate import time, memory and CPU/GPU frame time are recorded. |
| 7 | Release owner: reconcile evidence and decide release | All required preceding gates | Contract scenarios and [acceptance matrix](Docs/VALIDATION.md) have explicit results, with no hidden source-data or verification failures. |

Source-data gaps block the affected checks, not independent synthetic or UI work. Keep results in dated validation records and update the ledger after a real run. The sampled joist failure is upstream of conversion/Nanite; the 1,033 Unitless lights must not be relabeled as a shortcut.

## Legacy acceptance

Separately validate Dedupe confirmation/dry-run/external references, Explode staged failure/success/undo, Dataprep without dialogs, BIM hierarchy/storey grouping, partial-selection reruns, and Nanite auto-detection. Four legacy safety/placement tests already pass. Giving legacy tools tracked lifecycle guarantees is a separate scope decision.

## Deferred Phase 6

Source relinking with identity preview, reference-aware cleanup, cross-asset deduplication in tracked imports, richer instance visualization/extraction, and the optional Unreal material pack remain later deliverables. Pack distribution needs separately reviewed rights and appearance evidence. Retained superseded assets are reported; they are not automatically deleted.
