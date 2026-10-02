# DatasmithHISM handoff

## Current status

This section is the single source of current status. Other documents link here instead of restating test counts.

| | As of 2026-10-01 |
|---|---|
| Build | UE 5.8.3 `AdvancedHISMEditor Win64 Development` passed on the current working tree, editor closed. |
| Automation | Latest full suite: **53/53, zero failures, exit 0** (host `Saved/Logs/IesWarnSuite.txt`; previous 53/53 run `CwmSuite.txt`; earlier: 52/52 `AnalysisSuite.txt`, 51/51 `PanelNaniteSuite.txt`, 50/50 `ApplyNaniteSuiteFinal2.txt`, 49/49 `NaniteBudgetSuite.txt`, 48/48 `BenchSuiteFinal.txt`, 47/47 `TraceFeatureFinalSuite.txt`). The two interruption/probe fixtures in `ConVersePersistenceAutomation.cpp` remain separately invoked. Focused trace test: 1/1, exit 0 (host `Saved/Logs/TraceCaptureLatentRetry.txt`); focused Batch B test: 1/1, exit 0 (host `Saved/Logs/DataprepDeletionFocused.txt`). |
| Source control | Uncommitted changes on top of `c8ac53c`, including progress instrumentation, Amendments 13-14, and opt-in one-shot import trace capture. |
| Win64 plugin export | `C:/Unreal/Packages/DatasmithHISM-2026-10-01-Win64/` predates the progress log, Nanite records and Nanite advisory. |
| Live UI | Nothing since 2026-09-27 has been re-checked live. See the ordered list below. |
| Release acceptance | Incomplete. Gates and remaining work are in the [roadmap](ROADMAP.md). |

Evidence: [current-tree machine-readable results and source hashes](Docs/Validation/2026-10-01-current-tree-evidence.json), [import profiling and Nanite advisory](Docs/Validation/2026-10-01-import-profiling.md), [packaging](Docs/Validation/2026-09-29-packaging.md), [verification fixes](Docs/Validation/2026-09-28-verification.md), [live UI](Docs/Validation/2026-09-27-live-ui.md), [preset/cancellation](Docs/Validation/2026-09-27-preset-cancellation.md), [persistence/recovery](Docs/Validation/2026-09-26-phase2.md). Each record keeps the test count of its own run.

## Recent changes

- **2026-10-02 first real large-source runs and unavailable IES profiles as warnings (Amendment 18):** the CWM Revit export (2,970 meshes, 7,101 mesh actors, 371 lights) first rolled back because 67 lights referenced an extensionless IES file (`Generic`) that Datasmith cannot import and the dependency check passed. **Per the user, a missing IES profile is now only a warning.** An IES-mode texture that is absent, empty or has no `.ies` extension goes to a new `UnavailableIesProfiles` list: it never blocks, is not in the missing-texture prompt, needs no acceptance, and its lights verify with "IES profile unavailable (warning)". Ordinary image textures keep their accept prompt. `ExtensionlessIesIsReportedUpFront` (extensionless and absent profiles, real Datasmith importer) now covers it. **Verified:** build exit 0; full suite **53/53, zero failures, exit 0** (host `Saved/Logs/IesWarnSuite.txt`); the new routing was confirmed to fail both IES tests when IES profiles were sent back to the blocking list. **CWM rerun (host `Saved/Logs/CWM_20261002f.txt`):** `-AnalyzeOnly` without `-AllowMissingTextures` blocks only on the 7 image textures; with them accepted and no IES acceptance, the import verifies (379/379 groups, 2,561/2,561 instances), 67 lights carry "IES profile unavailable (warning)", none fail, the analysis recommends the same 98 meshes, and the whole headless run takes 19 s. The editor process still exits 1 (engine error line for the unusable profile). The commandlet gained `-AnalyzeNanite`, `-NaniteAnalysisFile=<csv>`, `-NaniteCoverage`, `-NaniteMinTriangles` and `-ApplyNanite=Recommended`. Build exit 0; full suite **53/53, zero failures, exit 0** (host `Saved/Logs/CwmSuite.txt`). Evidence and numbers: [CWM source record](Docs/Validation/2026-10-02-cwm-source.md). Headlines: the import verifies in about 19-31 s; Nanite on all 2,920 eligible meshes takes 5.4 s and on the 98 recommended takes 0.94 s; 98 meshes carry 89.7% of the scene's placed triangles. **Open:** the runtime value of skipping Nanite on the ~2,300 tiny meshes is unmeasured; the analysis thresholds are still untuned; the engine process exits 1 for a successful run that accepted an unimportable IES (Datasmith logs an Error), which breaks the headless "exit 0 on full success" rule; nothing was saved or rendered.

- **2026-10-02 Nanite analysis and selection (Amendment 17):** new read-only `AnalyzeNanite` ranks a committed import's meshes by placed triangles and recommends the fewest covering a chosen share (default 95%, minimum 1,000 triangles; both heuristics, not measured). The Apply step gained `OnlyMeshElements` (listed meshes on, everything else off, converges on repeat). Panel: **Analyze Nanite** button with Cover % and Min triangles entries, and a **Recommended meshes** scope; an empty recommendation is refused rather than treated as "all". New test `DatasmithHISM.OptimizedImport.NaniteAnalysis` plus extended `PanelApplyNanite`; each failed with its logic disabled (coverage cut-off, selection, empty-recommendation guard) and passed after the revert. Build exit 0; full suite **52/52, zero failures, exit 0** (host `Saved/Logs/AnalysisSuite.txt`). **Not done:** no large synthetic source was built, so the analysis is verified only on a two-mesh fixture with known counts and the thresholds are untuned; the new panel row is unseen live (extend live check 6b).

- **2026-10-02 panel Apply Nanite control:** new **Nanite (separate step)** row under the existing buttons: scope (all supported meshes / converted ISM/HISM groups only), max-meshes budget (default 16,384, 0 = unlimited) and an **Apply Nanite** button that acts on the last import, uses the inspection list's Disable-Nanite exceptions, shows a cancellable progress dialog when attended (`FConVerseNaniteApplyOptions::bAutomated` skips it), shows refusals and their reasons in the report, and leaves the import's status alone. The settings are panel-local and not saved in presets, and changing them does not invalidate the import. New test `DatasmithHISM.OptimizedImport.PanelApplyNanite` drives the real click handler; it failed with the exception list ignored and with the status overwritten, then passed after the revert. Build exit 0; full suite **51/51, zero failures, exit 0** (host `Saved/Logs/PanelNaniteSuite.txt`). **Not looked at live:** layout, wrapping at narrow widths, the progress dialog and its Cancel button are unverified in a real editor; add them to the live re-check list below. Still open: analysis of which meshes need Nanite.

- **2026-10-02 Nanite moved to a separate Apply step (Amendment 16):** imports now default to `PreserveImported`, so Datasmith builds each mesh once. New `FConVerseDatasmithImportService::ApplyNanite` (in `ConVerseNaniteApply.cpp`) enables Nanite on a committed import: fail-closed on drift/degraded/wrong level, reuses the Amendment 15 budget and ranking, restores every touched mesh on failure or cancel, recaptures the mesh tracked state, and records `NaniteApplySettingsJson` / `NaniteApplyEnabledMeshes` on the manifest. Commandlet: `-ApplyNanite=All|ISM`, with `ApplyNanite*` metrics. New test `DatasmithHISM.OptimizedImport.ApplyNaniteStep` passed and failed against a build with the drift refusal and the restore disabled. `NanitePolicyAndZeroGroupImport` now sets its inline policy explicitly (it relied on the old default). Build exit 0; full suite **50/50, zero failures, exit 0** (host `Saved/Logs/ApplyNaniteSuiteFinal2.txt`). **Not done: no panel control (service and commandlet only), nothing run on a large source, no live editor check, and total time is not reduced** (the benefit is failure isolation and a usable scene sooner). An existing import made under the old default is a different plan and rebuilds on its next optimized reimport. Next: the panel's Apply Nanite control, then a way to analyze which meshes need Nanite.

- **2026-10-01 Nanite mesh budget (Amendment 15, roadmap workstream 2, first step):** new `MaxNaniteMeshes` setting (default 16,384, 0 = unlimited, commandlet `-NaniteBudget=`). Over budget, the most-placed meshes keep Nanite and the rest are set to Nanite-off. It enters PlanId only when it binds, so existing under-budget imports keep their PlanId. New test `DatasmithHISM.OptimizedImport.NaniteMeshBudget` passed and failed against a build with the cap disabled. Build exit 0; full suite **49/49, zero failures, exit 0** (host `Saved/Logs/NaniteBudgetSuite.txt`). **Not exercised on a large source** (generated two-mesh fixture only), the panel UI was not looked at, and no live editor check was done. The double mesh build and the no-instancing-opportunity policy are still open.

- **2026-10-01 benchmark harness (roadmap workstream 1, started):** the commandlet gained `-MetricsFile` and `-Budget` ([usage](Docs/VALIDATION.md#benchmark-metrics-and-budgets)). The editor target built (exit 0). Covered by the new automation test `DatasmithHISM.OptimizedImport.BenchmarkMetricsAndBudgets`. It passed, and it failed (exit 1 on the exceeded-budget case) against a temporary build with the comparison disabled, then passed again after the revert. `-Budget` without `-MetricsFile` is now rejected before any import work. Full suite after the change: **48/48, zero failures, exit 0** (host `Saved/Logs/BenchSuiteFinal.txt`; build log `BenchBuildFinal.txt`). No baselines or budgets for the three real exports exist yet; that needs the ARCH, HVAC and a large unique-mesh source run on this machine. Rendered metrics are not implemented.

- **2026-10-01 Nanite advisory (Amendment 14):** Analyze and Import report `ProjectedNaniteMeshes` and warn above 16,384. Advisory only; no PlanId change.
- **2026-10-01 per-mesh Nanite records (Amendment 13):** flushed started/returned progress-log records around `PostEditChange()` and the `ConVerse_NanitePostEditChange` Insights scope. These identify the last callback entered, not crash causality.
- **2026-10-01 progress and trace capture:** per-operation stage records under host `Saved/DatasmithHISM/ImportProgress`, Insights bookmarks, `ConVerse_DatasmithImport` scope, and a default-off **Profile next import** option. It captures one full `.utrace` for Import/Rebuild, stops automatically, and reports the path without changing plan identity or presets. Build and 47/47 automation passed; details in the [trace evidence](Docs/Validation/2026-10-01-trace-capture.md).
- **2026-10-01 Batch B closeout:** Dataprep deletion reporting and deletion-obstruction coverage passed the focused real-wrapper automation; see the [legacy tools evidence](Docs/LEGACY_TOOLS.md) and [roadmap](ROADMAP.md).
- **2026-09-29:** Batches A/C/D and the runtime packaging smoke test.

## Next agent assignment

**Next task: one batched live session, then the remaining checklist rows.** Live sessions take over mouse and keyboard; **only run one when the user explicitly frees the desktop.** Do not restart implementation or begin Phase 6.

**Native interaction works.** `Saved/Phase2Acceptance/20260927-LiveUIB/NativeUI.ps1` drives Slate with `SendInput` clicks and Unicode text, guarded to the launched editor's foreground process. `SendKeys` accelerators such as Ctrl+N do not register; use menus.

The 2026-09-27 drift and session-restore fixes, and the 2026-09-28 ARCH fixes ([verification record](Docs/Validation/2026-09-28-verification.md)), have automation coverage only.

**Live re-check, in order:**

| Order | Item | What to observe |
|---|---|---|
| 1 | 2026-09-27 drift fix | Open `/Game/ConVerseValidation/LiveUI_20260927B_Joist` (saved with pre-fix `/Temp/` state); it should re-verify without drift. Import joist into a fresh untitled level, name it, save: expect green "Saved imported result and owning level." |
| 2 | 2026-09-27 session restore fix | Close and reopen the panel: the restored source path is visible and status is Ready. |
| 3 | Missing/empty dependency prompt (Amendments 8, 9) | Analyze ARCH: the Yes/No prompt lists `Window Keystone01.jpg` and `generic` (empty file). Declining stops; accepting proceeds. |
| 5 | Pre-translation warning (Amendment 2) | Analyze the 90 MB HVAC export: the "Translating source; the editor may be unresponsive…" text is visible before the freeze. |
| 6 | Failure summary wording | If any verification failure occurs, the dialog names the failing categories. Automated coverage exists; observe only if it arises naturally. |
| 6b | Apply Nanite control (2026-10-02) | Import a source with the default settings (meshes build once, Nanite off), set a scope and budget, click Apply Nanite: the cancellable dialog shows, the status line and report say what changed, the row fits the panel at normal and narrow widths, Cancel restores the meshes, Save imported result keeps the result. Also edit a mesh by hand and confirm the refusal text. Then Analyze Nanite: the ranked list and its marks are readable, the Cover % and Min triangles entries fit, and the Recommended meshes scope applies exactly the marked meshes. |
| 7 | Remaining checklist rows | Copied-fixture material/light rebuild previews; reviewed-target approval/revocation (needs a disposable reviewed target); native named-map copy refusal; Content Browser rename/move. Representative large-source success still needs a complete export. |

Cosmetic items seen live on 2026-09-27 and not fixed: the per-file "bytes 0 / N" progress label; stale report summary after save; `SelectInstance` bits never cleared between focuses; `…_ISM_0` component naming.

Use fresh map and destination names for destructive/failure tests. The current saved validation map is reference evidence, not a scratch target. Keep ownership/replacement/rollback decisions in the service. The next assignment is done when every live UI checklist row has a reproducible result and artifact, demonstrated defects have built/tested fixes, and unavailable interactions or missing inputs remain explicit. Broader source-data and release gates stay separate; see the [roadmap](ROADMAP.md).

## Ready-to-use workspace and validation paths

| Resource | Location |
|---|---|
| Working directory / Git root | `C:/Unreal/Projects/AdvancedHISM/Plugins/DatasmithHISM` |
| Host project | `C:/Unreal/Projects/AdvancedHISM/AdvancedHISM.uproject` |
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

- Git repository: this plugin directory. The host project root, generated validation maps, logs and package archives are outside it.
- Latest automation log: host `Saved/Logs/NaniteProjectionSuite.txt`. Earlier build/test logs are under host `Saved/Phase2Acceptance/<date>/`, named in each dated validation record.
- Live UI: `Saved/Phase2Acceptance/20260927-LiveUIB/` holds `editor.log`, `interaction.jsonl` and the numbered captures referenced by the [live UI results](Docs/Validation/2026-09-27-live-ui.md).
- Persistence: original named map reopens and re-verifies in a fresh process. Identity-changing named-map copies are refused with recovery instructions; initial unnamed save-as retains its previous behavior. Real read-only-asset and simulated map write-capacity failures report incomplete saves; saving drift never accepts a new baseline.
- Recovery: a disposable process was terminated at a recorded pre-commit checkpoint. Restart reports 7 observed paths and preserves all 4 saved evidence files unchanged.
- Fresh-process checks: `11-final-reopen.txt` re-verifies 1 group/2 instances with no new session; `12-runtime-lookup.txt` reports 2 source records and zero errors in an editor process. Its `cooked import` log label does not make this a packaged run.
- Package: clean Windows Development cook/archive and NullRHI/DX12 runtime smoke passed with three source records, one mesh component, one light, one IES profile and one successful collision trace.

All `Saved` paths refer to the host project. See the [earlier persistence evidence](Docs/Validation/2026-09-26-phase2.md) and [source hashes/results](Docs/Validation/2026-09-26-phase2-evidence.json). The [earlier evidence](Docs/Validation/2026-09-26.md) remains authoritative for the package run. Runtime code was unchanged; this persistence update was not recooked.

## Read before editing

[Contract](IMPORT_PANEL_VALIDATION.md), [roadmap](ROADMAP.md), [ADRs](Docs/ADR/README.md), [architecture](Docs/ARCHITECTURE.md), and [validation commands](Docs/VALIDATION.md). Historical [journal](Docs/History/JOURNAL.md) entries may contain superseded status claims; re-test suspected blockers against current evidence.
