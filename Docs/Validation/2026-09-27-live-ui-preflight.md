# Live UI preflight, 2026-09-27

Status: **preflight only; live interaction acceptance remains pending**. No Analyze, import, preset, focus, material-review, rebuild, or save interaction was executed. No C++ source changed and no new build or automation run is claimed.

## Observed results

- UE version is 5.8.3, changelist 58210709. All 55 source hashes match `2026-09-27-preset-cancellation-evidence.json`.
- The existing dirty working tree was recorded and preserved. No Unreal editor was running initially.
- A disposable interactive editor was launched at 2026-09-27 11:21:25 UTC with `/Engine/Maps/Entry`, process 61936. The native editor and DatasmithHISM Tools window appeared.
- Windows desktop capture and native window activation worked through PowerShell/.NET/Win32. The previous absence of a dedicated native-desktop connector is therefore not sufficient to declare all native access unavailable. Actual button/keyboard interaction remains unverified.
- Foreground ownership returned to another application. The helper refused input whenever the foreground process differed from the launched editor. No click or text input reached another application. Desktop availability was requested from the user; no response had arrived when this preflight record was written.
- The editor subsequently exited through `QUIT_EDITOR` / `UUnrealEdEngine::CloseEditor()` at 11:25:50 UTC. The log ends with normal editor shutdown and `Log file closed` at 11:25:52 UTC. The agent did not send a quit command or terminate the process. The initiating actor is not established by the log.
- All six checked-in joist/light fixture files remain unchanged. Disposable copies were prepared, but no test map, imported output, or import report was created by this run. Existing reference maps and retained interrupted attempts were not used as scratch targets.

## Local artifacts

All paths below are relative to the host project's `Saved/Phase2Acceptance/20260927-LiveUI/`, outside the plugin Git root.

| Artifact | Evidence or purpose |
|---|---|
| `editor.log` | Real interactive editor startup and normal shutdown |
| `source-check.json` | Expected and current SHA-256 values for all 55 source files |
| `git-status-before.txt` | Existing working-tree changes before this run |
| `fixtures-before.json` | Original fixture hashes and sizes; comparison at closeout found no changes |
| `reports-before.json`, `journals-before.json` | Preflight report/journal inventory |
| `preflight.json`, `preflight-result.json` | Planned disposable paths and final preflight result |
| `NativeUI.ps1`, `interaction.jsonl` | Guarded native-input helper and successful capture/window action trace; not a completed UI test harness |
| `Sources/Joist16K6/`, `Sources/RevitLightExport/` | Disposable copies reserved for controlled source changes |

Exploratory whole-desktop captures were removed because they included unrelated applications. No retained screenshot is offered as checklist evidence.

## Remaining acceptance and resume point

Every row of the [live checklist](2026-09-26-phase2-ui.md) and all six detailed preset/cancellation reproductions remain pending. No product defect or pass was established by this preflight.

Once the shared desktop is available, recheck process state and launch or identify the intended editor. Never reuse process 61936 without rechecking its identity. Use a fresh disposable map such as `/Game/ConVerseValidation/LiveUI_20260927/Joist` and destination `/Game/ConVerseValidation/LiveUI_20260927/JoistOutput`, checking that neither already exists. The engine Entry map was only a startup location; do not import into or save over that engine map.

Begin with the original two-instance joist fixture, then the light fixture. Capture only the editor and relevant dialogs, record the source/settings and report path, and demonstrate actual input before claiming native UI support. Progress/cancellation, source/instance/light focus, presets, material review, rebuild previews, and save/refusal feedback still require their own observed results. Complete representative sources, suitable translator workloads, and reviewed material targets remain separate dependencies.

The recorded 30/30 automation result remains the earlier baseline. Do not rerun service tests to substitute for live acceptance, and do not start Phase 6.
