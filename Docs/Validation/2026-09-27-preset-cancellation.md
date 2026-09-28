# Preset state and Analyze cancellation, 2026-09-27

**UE 5.8.3 editor build passed; full DatasmithHISM suite passed 30/30, exit 0, zero automation-controller errors.** This completes the bounded source-fixes-and-automation assignment. Native UI, representative large-source, rendered and release acceptance remain open. [Machine-readable evidence](2026-09-27-preset-cancellation-evidence.json) records all 55 source hashes, test names, logs and boundaries.

## Delivered behavior

- Typing a source/destination, choosing a source file and loading a preset use the same input-application path. Old and incoming paths are compared before member assignment. Source/destination changes clear old inspection/selection, material-review rows, report and save association; source changes also reset translator-specific knowledge.
- A panel owns at most one material-review window. Invalidated windows are disabled immediately and requested to close, preventing stale approval actions while Slate defers destruction. A new service result also closes the previous review.
- Same-identity presets preserve the imported-result association and require new analysis of their settings. Invalid presets leave input/result associations unchanged. Restoring settings performs no import, rebuild or save; session restore retains the last executed settings.
- One operation-local cancellation latch covers source/sidecar reads, translation return, actor/light traversal, materials, texture reads, dependency checks and group planning. Texture fingerprints stream 1 MiB chunks and retain the original MD5 bytes and missing-evidence semantics. Import uses the same pre-mutation helpers, including correct cancellation classification during sidecar reads.
- Cancellation returns no partial plan ID, counts, inspection, appearance or material/light decision collections. Ownership, commit, replacement and rollback remain centralized in the service.
- Progress observers are private editor-service callbacks with named phases and work counts. They are not saved to presets/manifests and do not enter plan identity. No schema, runtime or cooked-data format changed.

## Executed validation

All paths below are under host `Saved/Phase2Acceptance/20260927-PresetCancellation` unless stated otherwise.

| Evidence | Result |
|---|---|
| `before-work.zip` | 124 tracked/untracked workspace files plus Git status, patches, HEAD and hashes; all 54 prior source hashes matched before edits |
| `01-build.txt` | Initial build failed on an additional material-fingerprint call site, reference arguments in a test and helper declaration order; corrected before the final build |
| `02-build.txt`, `02-build-exit.txt` | UE editor build succeeded, exit 0 |
| `03-full-automation.txt`, `03-full-automation-exit.txt` | 30 successes, zero failures/controller errors, process and final test-complete exit 0 |
| `PanelInputStateInvalidation` | Actual preset files, source-selection application and Slate text-change callbacks clear stale state; same-identity and invalid presets preserve the appropriate association; no world/asset changes or executed-session preset overwrite |
| `AnalysisPhaseCancellation` | Nine Analyze and eight import cases cancel at named phases; source/mesh/texture hashes, world object states/dirty flag and asset inventories remain unchanged; no attempted-session output or partial plan survives |
| `AnalysisProgressPreservesEvidence` | Repeated analysis keeps plan identity, counts, material decisions and sidecar fingerprint; streamed texture MD5 equals the original engine hash; observer is not persisted; missing dependencies still fail before mutation |

The texture fixture is a generated valid 1024 x 1024 24-bit BMP, 3,145,782 bytes, crossing multiple 1 MiB reads. Cancellation targets are source hash, sidecar hash, translator return, source actors/lights, materials, texture hash, dependencies and group planning; Analyze additionally covers report preparation. Translator-return injection proves the service boundary, not an actual user click during a blocked translator.

The panel regression imports a real generated scene, then drives the panel's application methods and text delegates. Its material-review closure check uses a non-native `SWindow` and destruction delegate. It does not establish visible window behavior, focus, rendering, frame timing or cancellation responsiveness.

### Exact successful commands

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat' AdvancedHISMEditor Win64 Development '-Project=D:/Unreal/Sandbox/AdvancedHISM/AdvancedHISM.uproject' -WaitMutex

& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'D:/Unreal/Sandbox/AdvancedHISM/AdvancedHISM.uproject' '-ExecCmds=Automation RunTests DatasmithHISM; Quit' -unattended -nopause -nosplash -NullRHI -log '-abslog=D:/Unreal/Sandbox/AdvancedHISM/Saved/Phase2Acceptance/20260927-PresetCancellation/03-full-automation.txt'
```

Build output was captured in `02-build.txt`; automation console output was captured separately in `03-full-automation-console.txt`. Both ran with the user's editor closed. The full suite retains its separately invoked interruption fixtures outside the normal 30 tests; no new actual process-interruption run occurred here.

## Remaining acceptance

Follow the [live checklist](2026-09-26-phase2-ui.md), including the explicit preset-switching reproduction added for these fixes. Native interaction tools were unavailable in this session; the user selected fixes and automation with live UI pending.

The full structural export still has eight missing texture references, and HVAC has one. Their source files remain unchanged; use them as dependency-failure cases, not successful representative-source evidence. Corrected joist geometry, physical Revit light calibration and material appearance approval remain separate source-author gates. A complete large export and a suitable blocking translator workload are still needed.

Reference maps and retained recovery artifacts were preserved. Existing uncommitted work remains uncommitted. The runtime module was unchanged and no cook/package/rendered/performance run was made. Earlier [persistence/recovery](2026-09-26-phase2.md) and [package](2026-09-26.md) evidence retain their original dates and limits.
