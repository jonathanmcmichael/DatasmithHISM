# CWM runtime comparison preparation, 2026-10-02

**Status: maps and Win64 package prepared; runtime profiling is pending.** No packaged map was launched for capture because the user did not free the shared desktop. No frame-time, GPU, draw-call, or runtime-memory result is claimed here.

## Purpose and controls

Prepare a same-source comparison of ordinary meshes, ISM, and HISM on the CWM Revit export. The three fresh maps use the same source, accepted missing-image decision, engine/project settings, and Nanite-preserving default. Nanite was not applied; all metrics report zero Nanite-enabled and zero rebuilt meshes. Each import verified and saved its result and owning level.

The ordinary control used `-MinInstances=7102`, above all 7,101 source mesh actors; it produced zero optimized groups. ISM and HISM used the default minimum of 2. The source has 2,970 mesh assets and 371 lights. Seven image textures remain missing and were accepted for all three imports. Their visual and streaming effect is not assessed.

## Import results

| Variant | Groups / instances | Ordinary elements | World actors | Primitive components | Instanced components / instances | Import time | Sidecar hash time | Peak process memory |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Ordinary (`-MinInstances=7102`) | 0 / 0 | 7,101 | 3,079 | 7,621 | 0 / 0 | 43.21 s | 22.29 s | 6,497 MB |
| ISM | 379 / 2,561 | 4,540 | 2,538 | 5,439 | 379 / 2,561 | 20.12 s | 1.10 s | 6,469 MB |
| HISM | 379 / 2,561 | 4,540 | 2,538 | 5,439 | 379 / 2,561 | 20.73 s | 1.59 s | 6,446 MB |

All three commandlets exited 0 and reported `Verified`. Unreal summarized zero errors, with metadata-reference warnings (3,288 on ordinary; 3,666 on ISM/HISM). Import timings are not runtime measurements, and the ordinary run had a substantially colder sidecar hash than the two instanced runs.

## Maps and package

Maps saved under `/Game/ConVerseValidation/`:

- `CWM_Runtime_OrdinaryMap`
- `CWM_Runtime_ISMMap`
- `CWM_Runtime_HISMMap`

The Win64 Development archive is `Saved/CWMRuntimeComparison/Windows/AdvancedHISM.exe`. `Manifest_UFSFiles_Win64.txt` includes all three maps; the archive contains the cooked Pak and IoStore containers.

The source `.udatasmith` contains one camera element, but its imported map actor, suitability as a representative view, and behavior in the packaged runtime have not yet been checked.

## Evidence

Host project artifacts:

| Variant | Log | Metrics |
|---|---|---|
| Ordinary | `Saved/Logs/CWM_20261002_Runtime_Ordinary.txt` | `Saved/Benchmarks/CWM_20261002_Runtime_Ordinary.json` |
| ISM | `Saved/Logs/CWM_20261002_Runtime_ISM.txt` | `Saved/Benchmarks/CWM_20261002_Runtime_ISM.json` |
| HISM | `Saved/Logs/CWM_20261002_Runtime_HISM.txt` | `Saved/Benchmarks/CWM_20261002_Runtime_HISM.json` |

Package command: UE 5.8 `RunUAT.bat BuildCookRun`, Win64 Development, `-build -cook -stage -pak -archive`, with the three maps above in one `+`-separated `-map` argument. The archive directory was the new `Saved/CWMRuntimeComparison`.

## Pending runtime capture

After the shared desktop is explicitly freed, launch each packaged map under identical display resolution, graphics settings, power mode, and camera path. Confirm the imported CWM camera is usable; otherwise establish a repeatable camera route before recording data. Capture comparable warm and steady-state windows with Unreal Insights and runtime stats. Keep CPU/game/render/RHI/GPU frame times, draw calls, and memory separate from import/build measurements. Repeat each variant enough to distinguish run-to-run noise and record the raw traces and exact settings.

These maps and the package establish preparation only. The CWM missing textures limit visual/material conclusions, and a fixed-camera runtime comparison on this one machine does not establish full-model fidelity or shipping performance across target hardware.
