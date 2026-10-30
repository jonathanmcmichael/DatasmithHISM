# Grouping breakdown, 2026-10-29

Phase 0 of the [grouping plan](../GROUPING_PLAN.md) ([contract Amendment 20](../../IMPORT_PANEL_VALIDATION.md)). Read-only Analyze on three Warrenton Revit exports, UE 5.6.1 commandlet, NullRHI, `-AnalyzeOnly -AllowMissingTextures`, default minimum of 2. The 2026-10-02 CWM source (`CE-CWM_BD01_A_MDL_HKS_R25_DM`) and UE 5.8 are not on this machine, so these are different sources and engine from the earlier CWM records.

Raw metrics: host `Saved/Benchmarks/GroupingBreakdown/{HKS_A,MBC_CW,Arch3}.json`; logs `Saved/Logs/GroupingBreakdown_*.txt`. Each commandlet exited 1 only because UE 5.6 cannot load the host project's UE 5.8 `Content/LevelPrototyping` template assets (15 asset-registry errors); every analysis reported `AnalysisSucceeded`.

## Sources

| Tag | Export (`Desktop/Warrenton Revit Exports`) |
|---|---|
| HKS_A | `CE-WA_BLDG_02_GC_MDL_DPR_R25-3DView-CE-CWM_BLDG_02_A_MDL_HKS_DM` (architecture) |
| MBC_CW | `CE-WA_BLDG_02_GC_MDL_DPR_R25-3DView-CE-CWM_BLDG_02_CW_MDL_MBC_CM` (curtain wall) |
| Arch3 | `Arch 3` (largest architecture export, 69 MB) |

## Results

| | HKS_A | MBC_CW | Arch3 |
|---|---:|---:|---:|
| Source mesh actors / mesh assets | 4,049 / 1,765 | 8,154 / 4,245 | 10,408 / 4,840 |
| Planned groups / instances | 253 / 1,414 (35%) | 40 / 3,928 (48%) | 521 / 3,820 (37%) |
| Below minimum | 1,531 | 4,200 | 4,511 |
| - unique mesh | **1,257** | **4,190** | **3,511** |
| - parent split | 273 | 10 | 996 |
| - settings split / payload duplicate / no single lever | 0 / 0 / 1 | 0 / 0 / 0 | 0 / 0 / 4 |
| `UnsupportedNegativeScale` (could group) | 736 (**685**) | 0 | 921 (**870**) |
| `HasChildren` (could group) | 368 (12) | 26 (0) | 1,156 (554) |
| Group spread: median / widest | 5.0 m / 404 m | 2.2 m / 422 m | 4.9 m / 404 m |
| Groups spanning half the scene | 0 (scene 2.7 km) | **10** (scene 495 m) | 0 (scene 4.1 km) |

Every mesh element declared a payload hash, and all hashes were distinct. A separate read-only check of the curtain-wall `.udsmesh` files found 3,732 of 4,245 sharing a byte size with another file, but with mesh names removed only 2 files matched byte-for-byte. Byte-identical duplicate payloads are therefore effectively absent. Geometry that is equal only after normalization (centroid-relative, as the legacy signature does) was not measured.

## Findings

1. **Unique meshes dominate the leftovers** (78%, 99.8%, 78% of below-minimum actors). No instancing rule can group them. Reducing their cost needs merging or HLOD (plan Phase 4), not looser grouping.
2. **Mirrored instances are the largest instancing lever on architecture models**: 685 and 870 actors would join groups if negative-determinant instances were supported. This lever is not in the original plan.
3. **Parent split is moderate**: 996 actors on Arch3, 273 on HKS_A, 10 on the curtain wall (Phase 2).
4. **Settings split and byte-identical payload duplicates are zero** on all three sources. Phase 3 as written (payload hash) has nothing to act on here.
5. **Curtain wall groups are scene-sized**: 10 groups span at least half the 495 m scene, including one of 2,904 instances across 422 m. This is the case Phase 1 cell splitting targets. On the architecture models the scene spread is inflated by distant outlier actors (2.7-4.1 km), so the half-scene test undercounts there.

## Limits

Counts are single-rule counterfactuals, not proof that a relaxed grouping is safe or faster. No import, rendering or runtime measurement was made. UE 5.6 only.
