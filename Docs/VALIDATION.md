# Build, tests, and release acceptance

Current build and test status is kept in the [handoff](../HANDOFF.md#current-status). Dated records under [Validation](Validation/) keep the results of their own runs; the [documentation index](README.md) lists them.

## Build and automation

Close Unreal Editor before either command. If Live Coding blocks the build, close the editor or disable Live Coding through its normal workflow; do not kill an editor holding user work. IntelliSense error output does not substitute for this build. The host project is `C:\Unreal\Projects\AdvancedHISM`; adjust paths for another checkout.

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  AdvancedHISMEditor Win64 Development `
  -Project='C:\Unreal\Projects\AdvancedHISM\AdvancedHISM.uproject' -WaitMutex

& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'C:\Unreal\Projects\AdvancedHISM\AdvancedHISM.uproject' `
  '-ExecCmds=Automation RunTests DatasmithHISM; Quit' `
  -unattended -nopause -nosplash -NullRHI `
  '-abslog=C:\Unreal\Projects\AdvancedHISM\Saved\Logs\Automation.txt'
```

Capture each process exit code. Inspect `Test Completed`, `LogAutomationController: Error:`, and the final test-complete exit marker. Optional LinuxArm64/VisionOS validation messages do not block runs.

## Fixture-driven imports

Use a disposable destination and a complete source plus sidecars. `-AnalyzeOnly` runs analysis. To exercise saved output, use a new `/Game` map via `-NewMap`, then `-Save`. On later invocations load the same map with `-LoadMap`. `-NewMap` and `-SaveAsMap` refuse existing map paths.

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'C:\Unreal\Projects\AdvancedHISM\AdvancedHISM.uproject' -run=ConVerseOptimizedImport `
  '-Source=C:\Unreal\Projects\AdvancedHISM\Plugins\DatasmithHISM\Tests\Fixtures\Joist16K6\Joist16K6.udatasmith' `
  -Destination=/Game/ConVerseValidation/JoistReview -InstanceType=ISM -Nanite=All `
  -NewMap=/Game/ConVerseValidation/JoistReviewMap -Save `
  -unattended -nopause -nosplash -NullRHI
```

`-AnalyzeNanite` logs the read-only Nanite analysis of the import it just made (Amendment 17), `-NaniteAnalysisFile=<csv>` also writes every mesh row, `-NaniteCoverage=<percent>` and `-NaniteMinTriangles=<n>` set its heuristic thresholds (defaults 95 and 1000), and `-ApplyNanite=Recommended` applies exactly the recommendation (it fails if there is none). The metrics then include `AnalysisEligibleMeshes`, `AnalysisCandidateMeshes`, `AnalysisRecommendedMeshes`, `AnalysisTotalPlacedTriangles` and `AnalysisRecommendedPlacedTriangles`. A successful run that accepted an unimportable or absent IES file can still make the engine process exit 1, because Datasmith logs an Error for it; read the commandlet's own "import succeeded" line and the metrics file.

`-ApplyNanite=All|ISM` runs the separate Apply Nanite step on the import it just made (Amendment 16); `-NaniteBudget` and `-DisableNanite` feed it, and a failed step exits 1. The metrics then include `ApplyNaniteSeconds`, `ApplyNaniteMeshBuildSeconds`, `ApplyNaniteEnabledMeshes`, `ApplyNaniteRebuiltMeshes` and `ApplyNaniteBudgetSkippedMeshes`. An import without it builds meshes once and leaves Nanite as imported. `-Nanite=` still applies Nanite inline during the import.

`-NaniteBudget=<n>` sets the Nanite mesh budget for a run (0 = unlimited; omitted uses the recipe or preset value, default 16,384). The metrics file records `NaniteBudgetSkippedMeshes`.

### Benchmark metrics and budgets

Add `-MetricsFile=<path>.json` to write one run's import-side metrics (durations per stage, mesh build/processing time, process peak physical MB, source/planned/Nanite counts, and after an import the world's actor, primitive-component, instanced-component and instance counts). The file is written for failed runs too. Add `-Budget=<budget>.json` to fail the run (exit 1) when a metric exceeds its limit. Budget keys are `Max<Metric>`, for example `{ "MaxDurationSeconds": 120, "MaxActorCount": 500, "MaxPeakPhysicalMB": 8000 }`. An unknown or non-numeric key fails the run, so a typo cannot become an unchecked budget. `-Budget` requires `-MetricsFile`.

`PeakPhysicalMB` is the whole process lifetime including editor startup, so compare it only against the same command on the same machine. These are import-side numbers only: draw calls and frame time need a rendered camera-path run and are not measured here, so a passing budget never implies rendered speed. Use a fresh `-Destination` per run or the second run returns `AlreadyCurrent`. Covered by `DatasmithHISM.OptimizedImport.BenchmarkMetricsAndBudgets`, which drives the commandlet entry point: a satisfiable budget exits 0 with metrics matching the real world, and an exceeded budget, typo'd key, metric the run never recorded, non-numeric limit, unreadable budget file, and `-Budget` without `-MetricsFile` each exit 1.

This negative joist fixture proves source preservation, not restored diagonals. Generated automation fixtures are isolated per test; cleanup must diff pre-existing world objects because the editor world persists across tests. Use [fixture provenance](../Tests/Fixtures/README.md) and the [complete commandlet options](IMPORT_WORKFLOW.md#automation-and-runtime).

## Required acceptance matrix

| Area | Required scenarios | Current evidence and remaining work |
|---|---|---|
| Geometry | Joist diagonals, thin/ordinary/instanced output, sections/normals, nested/mirrored transforms, alignment and known dimensions | Six source-payload comparisons pass; original joist lacks diagonals. Corrected positive fixture, complete models, stock-toolbar and rendered comparison remain. |
| Nanite | On/off, incompatible defaults/overrides, exceptions, zero groups, compiler failure | Policy/zero-group/identity automation passes; broader materials/platforms and real compiler failure remain. |
| Instancing | ISM/HISM placement, bounds, LOD/culling, settings, collision, navigation, identity | Generated placement/settings and runtime collision evidence pass; broader navigation/LOD/BIM grouping remains. |
| Analyze/UI | Small/large files, early visible progress, stage/item updates, cancellation, translation failure, no partial success | Service cancellation/analysis checks pass; live Slate and representative large-file interaction remain. |
| Lighting | Known Revit values, physical/Unitless, IES on/off/missing, mixed types, warning boundaries | Generated units/threshold tests and one exported IES point light pass; physical Revit calibration and broader light types remain. |
| Materials | Stock/custom, ambiguous/renamed variants, textures/UV scale, missing target, approval invalidation | Exact approval, missing target, changed-appearance and preview tests pass; stock identities, visual matching and physical scale remain. |
| Lifecycle | Unchanged drift, changed source, manual edits, sidecars, stock refusal, injected failure, no duplicate active geometry | Generated lifecycle/rollback/ownership tests pass; complete real-model replacement and sidecar scenarios remain. |
| Persistence | Save/reopen, failed saves, named-level rename, interrupted attempt, uncertain ownership, source immutability | Save/reopen, named-copy refusal, save-with-drift, real read-only asset/map failures, bounded capacity simulation and actual pre-commit interruption/restart pass. Live UI, Content Browser rename/move, other crash windows and actual full-volume behavior remain. |
| Packaging | Clean Windows cook, geometry/materials/textures/lights, collision, metadata | Development cook and NullRHI/DX12 runtime smoke pass; ordinary texture/UV and rendered breadth remain. |
| Performance | Fixed baseline/candidate source, import/build time, memory, CPU/GPU camera paths | Timing and process peak fields implemented; representative comparative benchmark remains. |

## Interactive and fault checks

Use disposable maps and assets. For manual-edit conflicts, first show that the untouched result verifies, then change one tracked property and prove replacement blocks until explicitly authorized. For a destructive guard, exercise the real dispatch path and introduce a fault that the guard must detect; testing only a helper is insufficient.

Exercise Analyze cancellation before/during/after translation, material approval/revocation, source/instance/light focus, preset restore, and save status. A cancelled analysis must not appear completed. A saved result with drift remains unverified/degraded.

The read-only tests change only generated map/asset attributes, capture hashes, request save, confirm failure and unchanged bytes, then restore the attributes. Do not apply this procedure to user work. A bounded platform-file quota fixture exercises partial save failure without filling the normal disk; label it simulated. Actual interrupted-process recovery can be reproduced with `Tests/Invoke-InterruptedRecovery.ps1` after closing the editor. It starts and terminates only its own disposable process, then verifies recovery in a fresh process and compares saved files. See the [executed evidence](Validation/2026-09-26-phase2.md).

Legacy acceptance additionally includes Dedupe dry-run/decline/accept, external and unloaded references, Explode staged failure/success/undo, Dataprep with no dialogs, partial-selection reruns, hierarchy/storey cases, and Nanite auto-detection. Legacy asset deletion has no tracked rollback guarantee.

## Evidence and release rules

Record engine/plugin/source/exporter/fixture versions, source fingerprints, exact normalized settings, test/command, exit code, log, and observed limitations. Keep editor, automation, persistence, package, rendered and performance evidence distinct. Use [Windows packaging](RUNTIME_AND_PACKAGING.md) for cook and runtime checks.

Missing source data blocks the relevant acceptance gate rather than converting it into a pass. Keep negative fixtures. Do not claim the 245-row observed appearance list is verified stock coverage or that Unitless preservation establishes photometric correctness. The [roadmap](../ROADMAP.md) is the release-status authority alongside this procedure and dated evidence.
