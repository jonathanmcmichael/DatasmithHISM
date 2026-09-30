# ARCH live-import verification fixes, 2026-09-28

**Two live panel imports of the user's Revit 2025 export `ARCH.udatasmith` verified every group but failed light and source-accounting checks. Root causes were found, fixed and covered by five new regression tests, each shown failing before its fix. UE 5.8.3 editor build passed; full DatasmithHISM suite passed 38/38, exit 0, zero automation-controller errors. Nothing here has been exercised in a live editor.** [Source hashes](2026-09-28-source-sha256.json) record all 55 source files after the final build.

## Findings from the live imports

Source `C:/Users/jonathanmc/Desktop/ARCH.udatasmith`, HISM, minimum instances 3, Nanite all supported meshes, untitled level. Reports: host `Saved/DatasmithHISM/ImportReports/e2e659384b8a0bbffc4b12b69bb233e2.json` and `f266185c4bbb368035316d8b1fc7cf66.json` (the second accepted as degraded). Both verified 298/298 groups and 4,884 instances as exact `HierarchicalInstancedStaticMeshComponent`.

| Symptom | Root cause | Fix |
|---|---|---|
| 10 of 462 lights failed, with no field named | All 10 use `generic_IES`, whose file `ARCH_Assets/generic` is 0 bytes. `FPaths::FileExists` is true for empty files, so preflight passed it and the imported light had no IES texture. | Empty dependency files are missing (Amendment 9). Light verification names each differing field. |
| `Unaccounted ordinary mesh components: actual=5506 accounted=5505` | ARCH's single `<Camera>` becomes an `ACineCameraActor`. Outside commandlets, `UCameraComponent::OnRegister` registers a `UCameraProxyMeshComponent` (a visualization `UStaticMeshComponent`), which the world scan counted. Mesh accounting itself was exact: 10,389 source mesh actors = 904 + 221 + 4,380 skipped + 4,884 planned. | Visualization components without Datasmith identity are excluded; remaining unaccounted components are named (Amendment 11). |
| Summary "0 of 298 groups failed verification" | The summary was built only from group results although lights and accounting also gate verification. | Summaries and the panel dialog name every failing category (Amendment 6 clause 8). |

The camera-proxy mismatch cannot be reproduced by `-run=ConVerseOptimizedImport`, because commandlets skip the proxy. The automation harness is not a commandlet and reproduces it.

## User-decided changes

- **Accepted missing IES:** a light whose IES file the user explicitly accepted as missing (panel Yes, `AcceptedMissingTextures`, `-AllowMissingTextures`) reports "Light: source values preserved; IES profile missing (accepted)" and a WARN diagnostic instead of failing. An unaccepted missing IES still stops at preflight (Amendment 9).
- **Nanite for HISM groups:** "Converted ISM/HISM Groups Only" now covers HISM group output (`ProcessMeshes` uses `IsA`). `ComputePlanId` already included the output mode and Nanite policy. It gains a salt only for HISM with this policy, so such existing imports are not reported `AlreadyCurrent` with stale output and need an explicit rebuild. All other PlanIds are unchanged (Amendment 10).
- **Translator warning:** both translator entry points show "Translating source; the editor may be unresponsive. Cancel takes effect when translation returns." and call `FSlowTask::ForceRefresh` before the blocking call, bypassing the progress-update throttle. The cancellation guarantee is unchanged (Amendment 2).

## Regression tests

| Test | What it proves | Before fix |
|---|---|---|
| `DatasmithHISM.OptimizedImport.ZeroByteDependenciesAreMissing` | A 0-byte texture is missing; a search folder can resolve it; a 0-byte mesh fails even with missing textures allowed | Failed: 0-byte texture not reported |
| `DatasmithHISM.OptimizedImport.LightVerificationNamesField` | Failing lights name "IES texture"; accepted-missing IES becomes a warning; unaccepted stops preflight | Failed: outcome did not name the field |
| `DatasmithHISM.OptimizedImport.CameraProxyMeshIsNotSourceContent` | A Datasmith camera does not break source accounting | Failed with the Unaccounted diagnostic |
| `DatasmithHISM.OptimizedImport.FailedVerificationSummaryNamesChecks` | Parked and accepted summaries name failing lights while all groups verify (failure injection `CorruptLightBeforeVerification`) | Failed: summary named only groups |
| `DatasmithHISM.OptimizedImport.ConvertedGroupNaniteCoversHISM` | HISM group meshes get Nanite under the converted-groups policy, an ordinary mesh does not, ISM unchanged, identical rerun is `AlreadyCurrent` | Failed: "HISM converted group mesh has Nanite enabled" |

The translator warning is a paint-timing change and has no automated test.

## Executed validation

All paths are under host `Saved/Phase2Acceptance`.

| Evidence | Result |
|---|---|
| `20260928-Baseline/01-build.txt`, `02-full-automation.txt` | Baseline before changes: build up to date, 33/33 |
| `20260928-Verification/03-new-tests-before-fix.txt`, `03b-failedsummary-before-fix-retry.txt` | Four new tests failed as expected against unfixed code (temporary injection scaffold, since discarded) |
| `20260928-Verification/06-build-after-fix-retry.txt`, `07-new-tests-after-fix-retry.txt` | Build succeeded; four new tests pass |
| `20260928-Verification/08-full-suite-after-fix.txt` | 37/37, exit 0, zero controller errors |
| `20260928-Verification/09-arch-analyze.txt` | Headless `-AnalyzeOnly` of ARCH: `SourceLoadFailed`, "generic_IES: C:/Users/jonathanmc/Desktop/ARCH_Assets/generic (empty file)". Source files unchanged. |
| `20260928-NaniteTranslator/02-test-unfixed.log` | New Nanite test failed as expected |
| `20260928-NaniteTranslator/04-build-fixed.log`, `05-test-new-only.log` | Build succeeded; new test passes. (`03-build-fixed.log` is a Live Coding refusal while an editor was open, superseded.) |
| `20260928-NaniteTranslator/06-test-full-suite.log` | **Final: 38/38, exit 0, zero controller errors** |

## Not verified live

No live editor session was run on 2026-09-28. The panel's combined missing/empty dependency prompt on ARCH, the accepted-missing IES warning, a verified live ARCH import without the camera-proxy mismatch, the pre-translation warning painting, and the 2026-09-27 drift and session-restore fixes all await the ordered re-check in [HANDOFF.md](../../HANDOFF.md#next-agent-assignment). No commit, cook or package was made.
