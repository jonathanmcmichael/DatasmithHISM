# Packaged CWM runtime comparison, 2026-10-15

**A preliminary comparison is recorded; the runtime performance release gate is not passed.** Nine fresh-process packaged runs completed with exit 0: ordinary, ISM and HISM, each repeated three times at three identical fixed views. All 27 measured windows contain exactly 2,000 frames with validated stationary camera position and direction (54,000 measured frames). CPU/GPU timings, draw calls, process/GPU memory, 27 screenshots and nine raw Insights traces are retained.

Instancing reduced exterior draw calls modestly but increased them in the interior view. Frame-time repeat ranges overlap substantially on this actively used shared machine, so these measurements do **not** establish a reliable speedup or a supported source-size budget. HISM also has a visible interior lighting/shadow difference that needs diagnosis before rendered fidelity can pass. No importer, runtime module, saved map, material or Nanite setting was changed.

## Inputs and controls

Used the existing UE **5.8.3-58210709** Win64 Development archive and three maps from the [2026-10-02 preparation](2026-10-02-runtime-comparison.md), not a new cook of the current dirty source tree. Source inventory and import-side counts remain those in that record: 7,101 source mesh actors, 2,970 mesh assets, 371 source lights; ISM/HISM contain 379 groups / 2,561 instances and 4,540 ordinary elements. The seven accepted missing image textures remain a limitation. The original source file is no longer at its recorded Desktop path; no source was reimported or reconstructed.

| Control | Recorded value |
|---|---|
| Hardware | Dell Pro Max 16 Plus MB16250; Intel Core Ultra 9 285HX, 24 logical processors; about 127.5 GiB RAM |
| Rendering GPU / driver | NVIDIA RTX PRO 5000 Blackwell Generation Laptop GPU, 24,137 MiB dedicated VRAM reported by UE; driver 597.16 |
| Rendering | DX12 / SM6, 1280 x 720 windowed, screen percentage 100, scalability groups Epic (3), horizontal FOV 60 |
| Frame policy | VSync off, `t.MaxFPS=0`, dynamic resolution off, `t.IdleWhenNotForeground=0`; no fixed timestep |
| Existing scene settings | Lumen, virtual shadow maps and ray tracing retained; no Apply Nanite step |
| Lighting control | One transient default `DirectionalLight` spawned at the initial distant-view pawn transform, identically in every process; source lights retained |
| Input control | Native `ke PlayerController SetIgnoreLookInput true` and `SetIgnoreMoveInput true`; each call reports one successful instance |
| Run order | Ordinary / ISM / HISM; HISM / Ordinary / ISM; ISM / HISM / Ordinary |
| Window policy | Each view lasts 4,500 frames: first 2,000 warm up; frames [2,000, 4,000) measured; screenshot at 4,200 |
| Trace policy | Native `Trace.File <unique-name>.utrace default` at CSV frame 200; measurement bookmarks; `Trace.Stop` at 13,400 |
| Power / competing work | Windows Balanced scheme, clocks not locked; another UE editor and desktop applications remained active; foreground changed during runs |

Positions are Unreal centimetres; rotations are pitch/yaw/roll in degrees. `ViewSelf`, `BugItGo` and `fov 60` establish the views. Native input locking prevents desktop mouse motion from changing the camera. Analysis checks **every measured frame**, not just the requested transform or a screenshot.

| View | Position (X, Y, Z) | Rotation (P, Y, R) |
|---|---|---|
| Distant exterior | 75000, -90000, 40000 | -30, -135, 0 |
| Near exterior | 60000, -110000, 16000 | -15, -135, 0 |
| Interior | 35000, -135000, 3300 | 0, 180, 0 |

The imported camera was not a useful lit comparison view: it looked toward a small, effectively black model without exterior illumination. A diagnostic lighting-off run located the model; **lighting-off captures are excluded**. The final comparison uses the same transient directional light and lit rendering in all variants. Its interior is still dark. These are benchmark lighting controls, not a claim of Revit photometric equivalence.

Warm-up windows lasted **12.62-25.89 s** and measured windows **12.30-28.59 s**, depending on actual frame rate. There were **zero graphics/compute PSO misses in measured windows**. Process responsiveness, foreground process and other-editor CPU/memory were sampled once per second. No editor was closed or terminated and no Windows Security prompt was interacted with.

## Results

Values below are the **median of the three per-run medians**, not pooled frame averages. Bracketed ranges are the minimum and maximum per-run median. P95 is the median of the three per-run frame-time P95s. Thread/GPU times are native CSV values in milliseconds, not import-stage timings.

| View | Variant | Frame ms [repeat range] | P95 ms | Game ms | Render ms | RHI ms | GPU ms |
|---|---|---:|---:|---:|---:|---:|---:|
| Distant | Ordinary | 6.848 [6.519-7.583] | 7.788 | 1.505 | 6.839 | 3.130 | 2.564 |
| Distant | ISM | 6.872 [6.245-6.943] | 8.471 | 1.500 | 6.868 | 3.217 | 2.761 |
| Distant | HISM | 6.541 [6.089-7.314] | 7.963 | 1.549 | 6.527 | 3.023 | 2.735 |
| Near | Ordinary | 8.141 [8.030-12.084] | 10.489 | 1.606 | 8.131 | 3.354 | 3.497 |
| Near | ISM | 10.933 [7.279-11.240] | 14.662 | 1.904 | 10.917 | 4.348 | 3.768 |
| Near | HISM | 7.925 [6.992-13.402] | 9.292 | 1.626 | 7.914 | 3.296 | 3.607 |
| Interior | Ordinary | 8.769 [6.126-9.611] | 10.179 | 1.988 | 8.760 | 4.520 | 3.115 |
| Interior | ISM | 9.156 [6.316-10.203] | 10.675 | 1.855 | 9.140 | 4.337 | 3.216 |
| Interior | HISM | 6.453 [6.141-9.378] | 7.229 | 1.541 | 6.447 | 3.302 | 3.123 |

Memory is resident process physical memory (`PhysicalUsedMB`) and RHI-reported local GPU memory (`GPUMem/LocalUsedMB`), in MiB. It is not isolated mesh/instance storage. The raw results also retain process virtual memory, primitive counts, critical-path thread times and render-thread subscopes.

| View | Variant | RHI draws/frame | Draw change vs ordinary | Process physical MiB | GPU local MiB |
|---|---|---:|---:|---:|---:|
| Distant | Ordinary | 5726.5 | baseline | 2280.6 | 2619.8 |
| Distant | ISM | 5329.5 | -6.93% | 2444.1 | 2619.5 |
| Distant | HISM | 5438.5 | -5.03% | 2452.1 | 2618.6 |
| Near | Ordinary | 6756.0 | baseline | 2310.2 | 2668.8 |
| Near | ISM | 6562.5 | -2.86% | 2470.3 | 2637.1 |
| Near | HISM | 6709.5 | -0.69% | 2471.1 | 2635.9 |
| Interior | Ordinary | 1965.0 | baseline | 2319.3 | 2757.6 |
| Interior | ISM | 2062.0 | +4.94% | 2487.0 | 2758.7 |
| Interior | HISM | 2135.0 | +8.65% | 2472.6 | 2725.4 |

All measured windows report **7,106 GPU-scene instances** in every variant (source geometry plus common runtime content). The actor-count difference remains 541, matching the import-side reduction; common runtime/transient actors explain the higher absolute counts and a one-actor change during some runs. This does not substitute for source identity, collision or full-model geometry verification.

### Interpretation

- **Fewer actors did not produce a universal rendering win.** Exterior draw reductions are much smaller than the import-side primitive-component reduction. Interior draws increase in both instanced variants. GPU medians are slightly higher for both candidates in these views.
- **Do not turn the frame-time medians into a speedup claim.** For example HISM near-view medians span 6.992-13.402 ms, overlapping ordinary's 8.030-12.084 ms. The apparent HISM interior advantage is also within broad, overlapping repeat ranges. Active shared-machine work and unlocked clocks prevent a clean causal comparison.
- **The observed critical path is render-thread dominated, not game-thread actor ticking or GPU saturation.** Render critical-path times track frame times while game and GPU times are smaller; measured render-thread idle medians are zero. This narrows the next investigation to render submission, visibility/culling and lighting/shadow costs, not an automatic grouping redesign.
- **No memory-saving claim is supported.** The instanced processes did not have lower measured physical memory, GPU residency is similar, and shared-machine working-set variation is substantial.

## Rendered comparison and trace limitations

The 27 screenshots and a three-by-three first-repeat contact sheet are retained. Exterior framing and visible silhouettes align closely. Mean absolute RGB differences (0-255 scale) across all ordinary/candidate repeat pairs are 0.010-0.012 distant and 0.021-0.030 near, comparable to temporal repeat noise.

**The HISM interior is visibly different:** some roof-member lighting/shadow bands are darker. Ordinary/HISM interior mean absolute RGB differences are **1.918-1.943**, with up to **3.802%** of pixels differing by more than 16 in a colour channel. Ordinary/ISM is **0.124-0.149**, comparable to ordinary/ordinary repeats (**0.126-0.134**). A dark background makes whole-image averages insensitive, so neither these numbers nor the contact sheet proves complete fidelity. Preserve and diagnose the HISM difference; do not classify it as geometry loss or dismiss it as harmless without isolating culling, shadows and Lumen.

All nine traces were opened and decoded with UE 5.8 Unreal Insights in `-NoUI -AutoQuit` mode; CPU and GPU providers completed and thread/timer/statistics/counter exports were produced, exit 0. **Analysis is not warning/error-free:** Memory Insights reports invalid/missing memory-tag tracker data, and GPU analysis reports small event-order corrections. The diagnostics and full logs are retained. Do not use these traces for allocation attribution or microsecond GPU-scope comparisons; memory values in the tables come from independent native CSV/OS/RHI counters, not Memory Insights. Whole-trace timer statistics include warm-up and transitions; use the measurement bookmarks and CSV windows for per-view comparisons.

## Artifacts and reproducibility

All paths below are relative to the host project, currently `C:\Users\jonathanmc\Documents\Unreal Projects\AdvancedHISM`.

Root: `Saved\Benchmarks\CWMRuntime_20261015\FinalCaptures`.

| Artifact | Contents |
|---|---|
| `settings.json`, `runs.json` | Hardware, normalized controls, executable SHA-256, run order, wall-clock times and exit codes |
| `results.json`, `summary.csv` | All 27 validated windows, repeat ranges, timings/counters and artifact SHA-256 hashes |
| `01_Ordinary_R1` through `09_Ordinary_R3` | Per-run `arguments.txt`, `runtime.log`, `runtime.csv`, `runtime.utrace`, `process-samples.csv`, `Distant.png`, `Near.png`, `Interior.png` |
| Per-run `export.rsp`, `insights-export.log`, `threads.csv`, `timers.csv`, `timer-statistics.csv`, `counters.csv` | Native Insights decode and export evidence |
| `insights-diagnostics.json` | Explicit memory-analysis errors and other analyzer warnings for each trace |
| `image-comparisons.csv`, `comparison-sheet.png` | 108 screenshot pair comparisons and first-repeat contact sheet |

The nine traces total **4,988,320,947 bytes** and are local artifacts, not checked-in binaries. A compact [machine-readable record](2026-10-15-runtime-comparison.json) preserves settings, per-view summaries, per-run frame-time medians, diagnostics and raw-trace fingerprints in the plugin repository.

Reproduction scripts are preserved one level above the capture root: `Capture-CWMRuntime.ps1`, `Analyze-CWMRuntime.py`, `Export-CWMInsights.ps1` and `Compare-CWMImages.ps1`. The capture script uses **native packaged Unreal console commands and the native CSV profiler**, not a substitute importer or shell performance simulator. It refuses output collisions and validates existing successful captures before resuming. Use a fresh output directory and unique native artifact prefix for a new collection. Analysis requires no added Python packages; it uses the engine-bundled Python.

Excluded preparation attempts remain in `Saved\Benchmarks\CWMRuntime_20261015`: startup `-trace=` probes exited **777003** after logged shutdown; stopping the trace first and changing CSV exit timing did not resolve that startup-trace failure. Post-startup native `Trace.File ... default` completed normally and was used for all final runs. Earlier `NativeCaptures` had mouse-driven orientation differences and were rejected; the first `LockedCaptures` retry hit an existing native trace filename and was also rejected. A CSV parser initially counted the numeric metadata footer as an extra frame; it now stops at the native repeated end-header and validates exactly 13,500 data frames per run. None of these excluded attempts contributes to the tables.

## Remaining gate

This fulfills the **first measured packaged comparison**, not release acceptance. Stop collecting more samples under the same busy-machine conditions. Next, diagnose the preserved HISM interior difference and increased interior draw count; then repeat the same fixed views on an idle, explicitly available desktop with controlled power/foreground conditions before setting budgets or claiming a performance gain. Keep full-model fidelity, live-panel checks, complete ARCH/HVAC inputs, unique-mesh coverage and the later Nanite comparison pending.
