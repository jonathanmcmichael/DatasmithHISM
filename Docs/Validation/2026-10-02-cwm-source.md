# CWM source: first real large-source runs, 2026-10-02

Source: `CE-CWM_BD01_A_MDL_HKS_R25_DM.udatasmith` (Revit export, 42 MB, `_Assets` sidecar of 2,998 files / 117 MB). 7,101 mesh actors, 2,970 mesh assets, 371 lights, 7 textures missing from the sidecar. Headless commandlet, UE 5.8.3, NullRHI, HISM, `-AllowMissingTextures`, fresh destinations. Raw metrics and logs are in the host project's `Saved/Benchmarks` and `Saved/Logs` (`CWM_20261002*.json/.txt`, `CWM_Nanite_Analysis.csv`). This is import-side evidence only: no rendering, no frame times, and a single machine and source.

## What the runs showed

| Run | Result |
|---|---|
| 1. Default | Stopped at dependency preflight in 4 s: 7 distinct textures absent from the sidecar (22 uses of one Hush Stack PNG). Expected, and accepted from run 2 on. |
| 2. Accepting missing textures | Geometry verified (379/379 groups, 2,561/2,561 instances) but **67 of 371 lights failed verification and the import rolled back** (92.7 s, mostly verification plus rollback). Cause: the sidecar's `Generic` IES file has no extension, so Datasmith's texture factory builds nothing. Fixed by [Amendment 18](../../IMPORT_PANEL_VALIDATION.md), first as an accept-to-proceed dependency and then, by the user's decision, as a plain warning: a missing, empty or extensionless IES profile no longer blocks or needs acceptance (runs 3 and 4 below ran before that change and still passed `-AllowMissingTextures` for the seven image textures). |
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
- The engine process exits 1 even though the commandlet reports success, because Datasmith logs an Error ("Texture import failed") for the extensionless IES file. The commandlet's own result is 0. The documented "exits 0 only on full success" therefore does not hold when an accepted unimportable IES is present; by the engine source, an accepted absent IES file logs the same kind of Error (not run). Not changed.
- Rendered metrics, runtime cost of Nanite on small meshes, and the missing textures' effect on materials are not measured. The import was not saved, so no map or package was written.
