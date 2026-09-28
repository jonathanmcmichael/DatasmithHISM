# DatasmithHISM handoff

Updated 2026-09-27 after **live native UI acceptance**. **Real Unreal Editor interaction found two defects; both have built fixes and regression tests. UE 5.8.3 editor build and 32/32 automation tests passed; release acceptance is incomplete.** [Live UI results](Docs/Validation/2026-09-27-live-ui.md) and [all 55 source hashes/results](Docs/Validation/2026-09-27-live-ui-evidence.json) are current. The earlier [preset/cancellation evidence](Docs/Validation/2026-09-27-preset-cancellation.md), [persistence/recovery evidence](Docs/Validation/2026-09-26-phase2.md) and [documentation closeout](Docs/Validation/2026-09-27-closeout.md) retain their dates.

## Next agent assignment

> **Built and automation-tested, not yet live, 2026-09-27:** texture search folders (default: Autodesk shared material library tiers 1/2/3) and the missing-texture Yes/No prompt (contract Amendment 8). Build succeeded; full suite **33/33**, exit 0, zero controller errors (`Saved/Phase2Acceptance/20260927-Textures/01-build.txt`, `02-full-automation.txt`). A headless `-AnalyzeOnly` run on the user's Revit 2025 export `C:/Users/jonathanmc/Desktop/ARCH.udatasmith` (`03-arch-analyze.txt`) resolved `metals.ornamental metals.plate.mesh.1.bump.jpg` from `Textures/1/Mats` and still stopped on `Window Keystone01.jpg`, which is absent from the machine. That stop is expected without `-AllowMissingTextures`. The panel prompt has not yet been seen live. Changed: `ConVerseImportProcessing.{h,cpp}` (`ValidateDependencies`), `ConVerseDatasmithImportService.h` (`AcceptedMissingTextures`, `bAllowMissingTextures`, `MissingTextures`, `MissingMeshFiles`) and `.cpp` (call sites), `ConVerseDatasmithImportPanel.{h,cpp}` (`ConfirmMissingTextures`, `AskYesNo`), `ConVerseOptimizedImportCommandlet.cpp` (`-AllowMissingTextures`), and new test `DatasmithHISM.OptimizedImport.MissingTexturesRequireExplicitAcceptance`. Build and run the full suite (33 tests expected) before relying on it.

> **Unverified live:** the two fixes below passed build and automation only. They have **not** been re-exercised in a live editor.

**Native interaction works.** `Saved/Phase2Acceptance/20260927-LiveUIB/NativeUI.ps1` drives Slate with `SendInput` clicks and Unicode text, guarded to the launched editor's foreground process. `SendKeys` accelerators such as Ctrl+N do not register; use menus. The user must leave the desktop idle while it runs.

**Fixed 2026-09-27 (automation-verified):**

1. **False drift after naming an untitled level.** The editor's Save As also saves the manifest; UE redirects its soft paths but not tracked-state text naming `/Temp/…`. `ConVerseImportProcessing::RebaseTemporaryWorld` now maps those references to the owning world under GUID and session-tag proof, in `CheckState` and on explicit save. Contract Amendment 7 and ADR 0006 carry the clarification. Test: `DatasmithHISM.Persistence.UnnamedMapFirstSaveStaysVerified`.
2. **Session restore hid the effective source.** The panel's source box now shows the restored path and status reflects restored inputs. Test: `DatasmithHISM.OptimizedImport.PanelSessionRestoreShowsInputs`.

**Next task: live re-check, then the remaining checklist rows.** Relaunch the editor with the new build. Open `/Game/ConVerseValidation/LiveUI_20260927B_Joist`: its saved joist manifest still has pre-fix `/Temp/` state and should now re-verify. Then import into a fresh untitled level, name it, save the result (expect green "Saved imported result and owning level."), and reopen the panel (expect the source shown). Continue with the rows still open in the [checklist](Docs/Validation/2026-09-26-phase2-ui.md). Do not restart implementation or begin Phase 6.

| Order | Bounded task | Acceptance and evidence |
|---|---|---|
| 1 | Live re-check of both fixes | Observed as above, with captures |
| 2 | Decide translator-boundary feedback | Slate freezes during the synchronous translator, so no "cancel requested" state can paint; the deferred cancel itself is honest. Accept the limitation in the contract or design pre-translation messaging |
| 3 | Remaining checklist rows | Copied-fixture material/light rebuild previews; reviewed-target approval/revocation; native named-map copy refusal; representative large-source success (needs a complete export) |
| 4 | Broaden native rename and recovery acceptance | Content Browser rename/move, other interruption checkpoints and full-volume behavior remain separate checks |

Cosmetic items seen live and not fixed: the per-file "bytes 0 / N" progress label; stale report summary after save; `SelectInstance` bits never cleared between focuses; `…_ISM_0` component naming.

Use fresh map and destination names for destructive/failure tests. The current saved validation map is reference evidence, not a scratch target. Keep ownership/replacement/rollback decisions in the service. The next assignment is done when every live UI checklist row has a reproducible result and artifact, demonstrated defects have built/tested fixes, and unavailable interactions or missing inputs remain explicit. Broader source-data and release gates stay separate.

**Completed persistence/recovery assignment:** a named-map copy defect and missing recovery-path diagnostics have built/tested fixes. Actual read-only-asset failure, bounded simulated write-capacity failure, save-with-drift, native-copy refusal, and actual interruption/restart have passing evidence. Live interaction was left pending because no native desktop interaction tool was available. This closes only those proven scenarios, not all Phase 2 or release gates.

## Preset/cancellation fixes completed, 2026-09-27

- Shared panel input application fixes stale inspection, material-review and save targets after preset/file-picker/destination changes; invalidated review windows are disabled and closed.
- Shared pre-mutation cancellation covers material/light/dependency work and chunked texture hashes. Cancellation discards incomplete result data; import sidecar cancellation is no longer misreported as load failure.
- Three new regressions pass: panel state, 17 phase/entry-point cancellation cases, and hash/identity/dependency preservation. The complete suite passes 30/30.
- Checkpoint: host `Saved/Phase2Acceptance/20260927-PresetCancellation/before-work.zip`. Successful build/test logs are `02-build.txt` and `03-full-automation.txt` in that directory. Source hash evidence is linked above.
- These are automated editor/service results, not visible Slate interaction evidence. Continue with the expanded live checklist.

## Earlier persistence/recovery changes

- `ConVerseOptimizedImportCommandlet.cpp`: refuses already-saved map Save As before import/copy because native duplication changes actor identity. Ownership migration of those copies is not implemented.
- `ConVerseImportPersistence.cpp`: explains failed ownership rebinding and lists recorded recovery paths without loading or deleting uncertain objects.
- [Persistence automation](Source/DatasmithHISM/Private/Tests/ConVersePersistenceAutomation.cpp): three new regression tests plus two explicitly invoked interruption/probe fixtures outside the normal suite.
- [Interruption runner](Tests/Invoke-InterruptedRecovery.ps1): launches and terminates only its own disposable process, then verifies restart diagnostics and unchanged files.

Existing implementation changes were preserved. The working tree remains uncommitted; no commit, push or release was made. All current C++ edits passed the recorded 2026-09-27 build and suite.

## Ready-to-use workspace and validation paths

| Resource | Location |
|---|---|
| Working directory / Git root | `D:/Unreal/Sandbox/AdvancedHISM/Plugins/DatasmithHISM` |
| Host project | `D:/Unreal/Sandbox/AdvancedHISM/AdvancedHISM.uproject` |
| Engine | `C:/Program Files/Epic Games/UE_5.8` |
| Joist source fixture | `Tests/Fixtures/Joist16K6/Joist16K6.udatasmith` with its adjacent sidecar |
| Light/IES source fixture | `Tests/Fixtures/RevitLightExport/RevitLightExport.udatasmith` with its adjacent sidecar |
| Existing validation map | `/Game/ConVerseValidation/SaveAsAcceptance` |
| Existing joist/light destinations | `/Game/ConVerseValidation/SaveAs` and `/Game/ConVerseValidation/SaveAsLight` |
| Attempt journals | Host project `Saved/DatasmithHISM/Attempts` |
| Reports | Host project `Saved/DatasmithHISM/ImportReports` |
| Existing package | Host project `Saved/ConVersePackagedValidationFinal/Windows` |
| Phase 2 build/test/reopen logs | Host project `Saved/Phase2Acceptance/20260926A` |
| Fresh-process verified original map | `/Game/ConVerseValidation/Phase2_20260926A/Original` |
| Actual interrupted fixture and recovery evidence | Host project `Saved/Phase2Acceptance/20260926A-Interrupted-Final` |

The earlier `SaveAsAcceptance` validation map contains two joist instances and one IES light; the Phase 2 `Original` map contains two joist instances. These are local generated maps, not checked-in fixtures. Test-generated attempt journals may include failure-injection entries whose outputs were subsequently cleaned by their owning tests. The actual interruption fixture intentionally retains journal `0a40897b43b1aa24c01bccb3e8bc1d0b.json` and uncertain partial packages; another earlier interruption run is also retained. Recovery notices for these artifacts are expected. Diagnose their evidence; do not bulk-delete journals or packages to hide warnings.

Exact build, automation and fixture-import commands are in [Docs/VALIDATION.md](Docs/VALIDATION.md). Relevant headless switches are `-LoadMap`, `-NewMap`, `-SaveAsMap`, `-Save`, `-Rebuild` and `-ReplaceManualEdits`; the last flag explicitly authorizes discarding detected tracked changes and should only be used in the intended test. [Workflow reference](Docs/IMPORT_WORKFLOW.md#automation-and-runtime).

## Inputs still needed from the source author

1. **Deprioritized by the user, 2026-09-27: do not work on the joist webbing.** The user attributes the missing diagonals (Revit elements 610662/610663) to the export. That fixture came from Revit 2027's built-in exporter (Datasmith SDK 5.6.1). A new export is coming from Revit 2025 + Twinmotion 2024 (the Revit 2025 exporter writes SDK 5.3.0). Inspect it when supplied; preserve the existing negative fixture.
2. Complete structural/HVAC texture dependencies and shared coordinate/reference-point information. Existing files are `Snowdon_Towers_Sample_Structural-3DView-{3D}.udatasmith` and `Snowdon_Towers_Sample_HVAC-3DView-{3D}.udatasmith` on `C:/Users/jonathanmc/Desktop`.
3. Authoritative Revit light initial intensity/units, relevant loss/falloff/source settings, IES configuration and reference exposure, starting with fixture element **2052881**. All 1,033 exported HVAC point lights are currently Unitless.
4. Autodesk library/version identity evidence and reviewed Unreal target materials for catalog coverage. The observed inventory is only a starting point.

Do not repeat these requests if the user supplies the inputs in the next conversation. Inspect supplied evidence and continue the affected gate.

## Workspace and evidence

- Host project: `D:/Unreal/Sandbox/AdvancedHISM/AdvancedHISM.uproject`; UE 5.8.3.
- Git repository: this plugin directory. The project root, its Docs, generated validation maps, logs and package archives are outside that Git root.
- Existing uncommitted work was preserved. The implementation and documentation are working-tree changes; no publication is implied.
- Current build: `Saved/Phase2Acceptance/20260927-LiveUIB/67-build-final.txt`, passed, exit 0. Earlier: `20260927-PresetCancellation/02-build.txt`, `20260926A/09-build.txt`.
- Current automation: `Saved/Phase2Acceptance/20260927-LiveUIB/68-full-automation.txt`, 32/32 passed, exit 0, zero controller errors. Earlier: `20260927-PresetCancellation/03-full-automation.txt` (30/30), `20260926A/10-full-automation.txt` (27/27).
- Live UI: `Saved/Phase2Acceptance/20260927-LiveUIB/` holds `editor.log`, `interaction.jsonl` and the numbered captures referenced by the [live UI results](Docs/Validation/2026-09-27-live-ui.md).
- Persistence: original named map reopens and re-verifies in a fresh process. Identity-changing named-map copies are refused with recovery instructions; initial unnamed save-as retains its previous behavior. Real read-only-asset and simulated map write-capacity failures report incomplete saves; saving drift never accepts a new baseline.
- Recovery: a disposable process was terminated at a recorded pre-commit checkpoint. Restart reports 7 observed paths and preserves all 4 saved evidence files unchanged.
- Fresh-process checks: `11-final-reopen.txt` re-verifies 1 group/2 instances with no new session; `12-runtime-lookup.txt` reports 2 source records and zero errors in an editor process. Its `cooked import` log label does not make this a packaged run.
- Package: clean Windows Development cook/archive and NullRHI/DX12 runtime smoke passed with three source records, one mesh component, one light, one IES profile and one successful collision trace.

All `Saved` paths refer to the host project. See the [earlier persistence evidence](Docs/Validation/2026-09-26-phase2.md) and [source hashes/results](Docs/Validation/2026-09-26-phase2-evidence.json). The [earlier evidence](Docs/Validation/2026-09-26.md) remains authoritative for the package run. Runtime code was unchanged; this persistence update was not recooked.

## Implemented behavior

Analyze progress/cancellation, ordinary/instanced source accounting, tracked manual-edit replacement guards, explicit rebuild, dependency checks, save/checkpoint diagnostics, independent Nanite policies, exact mesh exceptions, source-light checks and MegaLights advice, named/session presets, reviewed material tables/mappings, searchable inspection, runtime source lookup, and group/material/light rebuild previews are present.

Mesh processing, material review and persistence are focused helpers. Ownership, commit, replacement and rollback remain centralized. New manifests use schema 2, tracked-state version 1 and source-inventory version 1. Material state comparison ignores only generated expression GUIDs; actual parameter edits remain tracked. Stock reimport remains blocked at factory priority plus 100.

Legacy component offsets and incompatible-setting grouping now have fixes and automation. Legacy conversion still has no tracked manifest or rollback. Preserve this distinction in UI/docs.

## Data findings and next gate

The exported 16K6 payload for Revit elements 610662/610663 has 88 vertices and 160 triangles without diagonals. All six ordinary/ISM/HISM/Nanite comparisons preserve it. Obtain a corrected export; do not manufacture geometry. Full structural/HVAC preflight also reports missing textures.

All 1,033 HVAC lights declare Unitless. One exported light/IES fixture preserves intensity and IES state, but physical Revit calibration is unresolved. The 245 observed material appearances are not verified Autodesk stock identities or a replacement library.

Follow [NEXT_STEPS.md](NEXT_STEPS.md) for source correction, full alignment, calibrated lights, catalog curation, live Slate, broader rename/crash/full-volume checks, rendered and performance work. Phase 6 remains deferred. Do not label Phases 1-5 or the release complete.

## Read before editing

[Contract](IMPORT_PANEL_VALIDATION.md), [plan](PLAN.md), [execution ledger](ROADMAP_EXECUTION.md), [ADRs](Docs/ADR/README.md), [architecture](Docs/ARCHITECTURE.md), and [validation commands](Docs/VALIDATION.md). Historical journal entries may contain superseded status claims; re-test suspected blockers against current evidence.
