# CWM source: first real large-source runs, 2026-10-02

Source: `CE-CWM_BD01_A_MDL_HKS_R25_DM.udatasmith` (Revit export, 42 MB, `_Assets` sidecar of 2,998 files / 117 MB). 7,101 mesh actors, 2,970 mesh assets, 371 lights, 7 textures missing from the sidecar. Headless commandlet, UE 5.8.3, NullRHI, HISM, `-AllowMissingTextures`, fresh destinations. Raw metrics and logs are in the host project's `Saved/Benchmarks` and `Saved/Logs` (`CWM_20261002*.json/.txt`, `CWM_Nanite_Analysis.csv`). This is import-side evidence only: no rendering, no frame times, and a single machine and source.

## What the runs showed

| Run | Result |
|---|---|
| 1. Default | Stopped at dependency preflight in 4 s: 7 distinct textures absent from the sidecar (22 uses of one Hush Stack PNG). Expected, and accepted from run 2 on. |
| 2. Accepting missing textures | Geometry verified (379/379 groups, 2,561/2,561 instances) but **67 of 371 lights failed verification and the import rolled back** (92.7 s, mostly verification plus rollback). Cause: the sidecar's `Generic` IES file has no extension, so Datasmith's texture factory builds nothing. Amendment 18 first made this warning-only, then the follow-up below added pre-factory filtering to avoid the engine error. Runs 3 and 4 below predate that filtering and still passed `-AllowMissingTextures` for the seven image textures. |
| 3. After the fix, `-ApplyNanite=All` | **Verified**; import 31.4 s (18.2 s of it hashing the sidecar, cold cache), verification 0.26 s. Nanite apply: 2,920 meshes enabled and rebuilt in **5.4 s**, 50 excluded for material compatibility, none left out by the 16,384 budget. Process peak about 6.5 GB. |
| 4. `-AnalyzeNanite`, `-ApplyNanite=Recommended` | Verified; import 18.6 s (sidecar hash warm). Nanite apply of the **98** recommended meshes: **0.94 s**. |

Result scene: 2,537 actors, 5,439 primitive components, 379 instanced components carrying 2,561 instances; 4,540 ordinary meshes.

## Nanite analysis on this source (heuristic thresholds: 95% coverage, 1,000 triangles)

- 2,970 meshes. 2,320 have under 100 triangles, 2,596 under 1,000, 79 have 10,000 or more; 2,352 are placed once.
- One mesh (206,382 triangles, placed 53 times) is about 10.9 M of the scene's 20.8 M placed triangles (53%).
- 368 meshes are candidates (eligible, at least 1,000 triangles). **98 of them cover 95% of the candidates' placed triangles, and 89.7% of all placed triangles**, and hold 2.4 M of the scene's 3.5 M unique triangles.
- Nanite on every eligible mesh costs only 5.4 s here, so import time is not the argument for selecting. Whether Nanite on the 2,300-plus tiny meshes helps or hurts at runtime is **unmeasured**; the case for the selection rests on Nanite's per-mesh overhead on tiny meshes, not on a measurement from this project.

## Observations and limits

- Sidecar hashing is the largest import cost when the file cache is cold (17.9 s of the 18.2 s hashing stage in run 3, against 0.8 s for the same step in run 2 and a 1.4 s whole stage in run 4, both warm). The cold-versus-warm gap is consistent with reading from disk rather than hashing, but that was not isolated.
- Before pre-factory filtering, the engine process exited 1 even though the commandlet reported success, because Datasmith logged an Error ("Texture import failed") for the extensionless IES file. The follow-up run below supersedes that process-exit result.
- Rendered metrics, runtime cost of Nanite on small meshes, and the missing textures' effect on materials are not measured. The import was not saved, so no map or package was written.

## Follow-up: IES best effort, 2026-10-02

After the 2026-10-02 selective-filtering change, a fresh warm-cache CWM import used a new destination, accepted only the seven missing image textures, and did not accept IES profiles. The translated scene omitted unusable IES texture elements before Datasmith import. Result: **Verified**, 379/379 groups, 2,561/2,561 instances, 67 unique lights with IES omission warnings, zero IES import errors, and editor process exit **0**. Metrics: 18.4 s total, 6,419 MB process peak, 2,537 actors, 5,439 primitive components, 379 instanced components, and 4,540 ordinary meshes. The commandlet reported 0 errors; its 3,671 metadata-reference warnings are unrelated to IES and remain visible in the log.

Evidence: host `Saved/Logs/CWM_20261002_IESBestEffort_1610.txt` and `Saved/Benchmarks/CWM_20261002_IESBestEffort_1610.json`. The run was headless with NullRHI and was not saved to a map. It establishes object import and accounting only, not rendered fidelity, photometric equivalence, or the visual effect of the seven missing image textures.

## Repeat import speed check

Two more fresh-destination imports used the same source, HISM settings, `-AllowMissingTextures`, NullRHI, and no Nanite step. Both reported **Verified**, exit 0, 379/379 groups, 2,561/2,561 instances, 2,537 actors, 5,439 primitive components, and 6,413-6,537 MB process peak. Unreal summarized 0 errors and 3,667 warnings, which are missing-actor metadata references; the unusable IES profile caused no texture-import error. Neither map was saved.

| Run | Destination | Import duration | Sidecar hashing | Datasmith import | Peak physical memory |
|---|---|---:|---:|---:|---:|
| Cold-cache retry | `/Game/ConVerseValidation/CWM_20261002_Retry` | 36.39 s | 18.06 s | 12.25 s | 6,537 MB |
| Immediate warm-cache retry | `/Game/ConVerseValidation/CWM_20261002_WarmRetry` | 18.64 s | 1.26 s | 11.22 s | 6,414 MB |

The warm retry is effectively the same speed as the earlier 18.41 s warm run; the cold retry's additional time is almost entirely sidecar hashing. This is consistent with the earlier cache observation, not evidence of a slower import path. The seven absent image textures were accepted for both runs; their visual/material impact remains unmeasured.

Evidence: host `Saved/Logs/CWM_20261002_Retry.txt` and `CWM_20261002_WarmRetry.txt`; metrics in `Saved/Benchmarks/CWM_20261002_Retry.json` and `CWM_20261002_WarmRetry.json`.

## Large-source ISM validation

The same CWM source was imported with `-InstanceType=ISM`, `-AllowMissingTextures`, NullRHI, and a fresh destination `/Game/ConVerseValidation/CWM_20261002_ISM_Geometry`. The commandlet exited **0** and reported **Verified**: 379/379 groups, 2,561/2,561 instances, 2,537 actors, 5,439 primitive components, 379 exact `InstancedStaticMeshComponent` outputs, and 4,540 ordinary mesh elements. Every group passed mesh, material, count, order, transform, placement, and settings checks. This establishes large-source ISM creation and verification; it is not rendered geometry acceptance.

Duration was 40.85 s, including 21.15 s hashing the sidecar, 13.43 s in Datasmith import, and 6,482 MB process peak. It is not directly comparable to the 18.64 s warm-cache HISM retry because this ISM run spent substantially longer hashing. Unreal reported 0 errors and 3,667 missing-actor metadata-reference warnings; no IES texture import error occurred. The seven absent image textures were accepted. No map was saved, and their rendered material impact remains unmeasured.

Evidence: host `Saved/Logs/CWM_20261002_ISM_Geometry.txt`, `Saved/Benchmarks/CWM_20261002_ISM_Geometry.json`, and import report `Saved/DatasmithHISM/ImportReports/bcba8c904c9a870fff4c6ca60ebe8eab.json`.
