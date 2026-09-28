# Build, tests, and release acceptance

Current baseline: **2026-09-27, UE 5.8.3 editor build passed, 33/33 automation tests passed** (texture search folders and missing-texture prompt, Amendment 8; logs in host `Saved/Phase2Acceptance/20260927-Textures`). Previously 32/32, exit 0 and zero controller errors. [Executed evidence](Validation/2026-09-27-live-ui.md) and [55 source hashes/results](Validation/2026-09-27-live-ui-evidence.json) cover live native UI results and the two resulting fixes. [Preset/cancellation evidence](Validation/2026-09-27-preset-cancellation.md) covers the previous changes. Earlier [persistence/recovery](Validation/2026-09-26-phase2.md) and [package](Validation/2026-09-26.md) results retain their original dates.

The [earlier documentation closeout](Validation/2026-09-27-closeout.md) records its own 54-file snapshot. Live native UI remains the next independent assignment and is not established by the automated panel-state checks.

## Build and automation

Close Unreal Editor before either command. If Live Coding blocks the build, close the editor or disable Live Coding through its normal workflow; do not kill an editor holding user work. IntelliSense error output does not substitute for this build.

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  AdvancedHISMEditor Win64 Development `
  -Project='D:\Unreal\Sandbox\AdvancedHISM\AdvancedHISM.uproject' -WaitMutex

& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\Unreal\Sandbox\AdvancedHISM\AdvancedHISM.uproject' `
  '-ExecCmds=Automation RunTests DatasmithHISM; Quit' `
  -unattended -nopause -nosplash -NullRHI `
  '-abslog=D:\Unreal\Sandbox\AdvancedHISM\Saved\Logs\Automation.txt'
```

Capture each process exit code. Inspect `Test Completed`, `LogAutomationController: Error:`, and the final test-complete exit marker. The current baseline exits 0 with 30 successes and zero controller errors. Optional LinuxArm64/VisionOS validation messages did not block that run.

## Fixture-driven imports

Use a disposable destination and a complete source plus sidecars. `-AnalyzeOnly` runs analysis. To exercise saved output, use a new `/Game` map via `-NewMap`, then `-Save`. On later invocations load the same map with `-LoadMap`. `-NewMap` and `-SaveAsMap` refuse existing map paths.

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\Unreal\Sandbox\AdvancedHISM\AdvancedHISM.uproject' -run=ConVerseOptimizedImport `
  '-Source=D:\Unreal\Sandbox\AdvancedHISM\Plugins\DatasmithHISM\Tests\Fixtures\Joist16K6\Joist16K6.udatasmith' `
  -Destination=/Game/ConVerseValidation/JoistReview -InstanceType=ISM -Nanite=All `
  -NewMap=/Game/ConVerseValidation/JoistReviewMap -Save `
  -unattended -nopause -nosplash -NullRHI
```

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

Missing source data blocks the relevant acceptance gate rather than converting it into a pass. Keep negative fixtures. Do not claim the 245-row observed appearance list is verified stock coverage or that Unitless preservation establishes photometric correctness. The [execution ledger](../ROADMAP_EXECUTION.md) is the release-status authority alongside this procedure and dated evidence.
