> Historical snapshot before the 2026-09-26 documentation consolidation. Claims and task status below are historical, not current guidance. See the [current documentation index](../../README.md). Relative links have been adjusted for this archive.

# DatasmithHISM - next steps

## Current release gates, 2026-09-26

The editor build, 24/24 automation tests, save/reopen, Windows cook/archive, and packaged runtime smoke checks pass. Release acceptance remains open. [ROADMAP_EXECUTION.md](../../../ROADMAP_EXECUTION.md) is the detailed current ledger; [validation evidence](../../Validation/2026-09-26.md) records the limits.

1. Correct the joist export: webbing is absent from the original payload, including Revit element 610662. Keep the negative fixture and add a corrected positive fixture.
2. Restore missing structural/HVAC textures, then validate complete scenes, reference points, dimensions, hierarchy and rendered geometry.
3. Obtain known Revit photometric values and IES settings. All 1,033 exported HVAC lights are Unitless; preservation is verified, conversion is unresolved.
4. Curate Autodesk appearance identities and approved Unreal mappings using the new review/DataTable workflow. The 245-row observed inventory is a starting point, not verified stock-library coverage.
5. Complete live Slate/cancellation, save failure/restart/rename, rendered texture/UV, broader collision/navigation/LOD, and camera-path performance acceptance. Exercise the implemented group/material/light rebuild previews on representative complete scenes.
6. Keep source relinking, automatic cleanup, broader deduplication, selective extraction and the optional material pack deferred.

Whole-session failed-verification acceptance is explicitly retained by the accepted plan. The older design question below is resolved on that point. Earlier findings and baseline counts below are retained as history.


> ## Amendment 6 is built, tested, and green
>
> Accepting a failed verification has now been **compiled and run** for the first time. Doing so
> immediately falsified it: `FailedVerificationCanBeAcceptedAndIsQuarantined` failed on its first
> execution because `CommitManifestAndOwnership` hardcoded
> `Verification.State = Passed` and `FailedGroupCount = 0`. `AcceptFailedVerification` set only
> `bAcceptedWithFailedVerification`, so an accepted-with-failures manifest still claimed it passed.
> Commit now derives both fields from the result it is given. The suite is **16/16**.
>
> Another instance of this project's own lesson: *write the test even when the code looks safe.*
>
> **Open design question for the user.** Acceptance was implemented as whole-session because
> `ConvertGroups` destroys the source HISMs before `VerifySession` runs, making per-group recovery
> impossible without re-importing. If what was actually wanted is "keep the groups that passed and
> leave the rest as ordinary actors", that needs a different design: verification would have to move
> before source destruction, or the conversion would need to be re-runnable per group.

> ## Multi-material verification defect - fixed
>
> Verification mapped each imported `UStaticMesh` material slot back to its Datasmith slot id with
> `LexFromString(MaterialSlotName)`. That was never going to work: `FDatasmithStaticMeshImporter::ApplyMaterialsToStaticMesh`
> renames every slot to the imported material asset's name, and `FDatasmithStaticMaterialTemplate::Apply`
> copies that name into `ImportedMaterialSlotName` too, so **neither name field retains the numeric id**.
> `LexFromString` is `Atoi`, which writes `0` for any non-numeric string rather than failing - so every
> slot resolved to Datasmith slot 0, defeating the positional fallback that was already there. Slot 0
> matched by coincidence; slot 1 and beyond compared against slot 0's material and failed.
>
> Every multi-material mesh therefore failed verification and was rolled back. Single-slot meshes were
> unaffected, which is why the symptom looked selective.
>
> The id now comes from the LOD0 `FMeshDescription` polygon-group slot names - the only place it
> survives - with `ImportedMaterialSlotName` and positional index as ordered fallbacks. The same bug
> existed verbatim in the manifest-recording path; both call sites now share the fix.
> `MultiMaterialSlotsVerify` covers it and is **mutation-verified**: forcing every slot id back to 0
> reproduces `material pointer mismatch at slot 1` exactly.
>
> No contract amendment was needed - `IMPORT_PANEL_VALIDATION.md` line 283 and scenario S10 already
> required exact per-slot material matching. The code simply was not meeting it.


> The import log (Amendment 4) is **built, tested and verified**: 13/13 automation tests pass and
> the log was inspected directly to confirm one row per attempt and correct severities.
>
> Implementing it surfaced two genuine defects, both fixed:
>
> 1. **`FinishResult` ran twice on the success path.** The `Verified` checkpoint reports before
>    commit so a report survives a later commit failure, then the real exit reports again. Harmless
>    while output was write-once-per-file, but it double-counted the moment an append-only log
>    existed. `FinishResult` now takes `bTerminal`; only terminal calls append a row.
> 2. **`RollbackFailed` logged at `Display`.** Now `Error`. This is also why
>    `ObstructedRollbackDegradesToRollbackFailed` needed `AddExpectedError` — it induces that state
>    deliberately, and the automation framework fails any test that logs an unexpected error.
> 3. **The durability fix was itself broken.** Second-resolution timestamps still collided for two
>    attempts in the same second. Found only because the first mutation test *passed* when it should
>    have failed — the original assertion targeted failed imports, which get a unique `SessionId`
>    and so were never exposed to the collision. Strengthened to assert on repeated `Analyze` calls,
>    which are genuinely sessionless; the mutation then failed correctly.
>
> **Lesson worth keeping:** a mutation that does not fail the test means the *test* is wrong, not
> that the code is safe. Both mutations were reverted and the suite re-run green.

> Written at the close of the tessellation run, updated at the close of the sidecar run, and again at the close of the rollback run. Verified at UE 5.8.3, editor target `AdvancedHISMEditor Win64 Development` building clean, **12/12 automation tests passing**, both new rollback tests mutation-verified.
>
> This project has a documented history of stale docs (a false "automation is blocked by SDK validation" claim, a stale `SHA-256` label after a switch to MD5, and a "NOT COMPILE-VERIFIED" banner that outlived its own fix). **Re-verify anything here before acting on it.** Every status below was checked against the source or a live run at the time of writing, not copied forward.

## Where the run ended

The optimized import path is manifest-owned, verified, rollback-safe, reimport-guarded, format-agnostic, and now tessellation-aware. The legacy in-place conversion path has had its four correctness defects fixed and has automation covering its safety properties, but it remains architecturally weaker - no manifest, no rollback.

**That asymmetry is still the most important fact about this project.** Both paths are reachable from the same toolbar and a user cannot tell from the UI which guarantees apply.

### Verified state

| Item | Status |
| --- | --- |
| `AdvancedHISMEditor Win64 Development` | Builds clean |
| Automation suite (`DatasmithHISM`) | 16/16 passing |
| Multi-material slot verification | Fixed, mutation-verified test (`MultiMaterialSlotsVerify`) |
| Accept-with-failed-verification (Amendment 6) | Compiled, run, defect found and fixed |
| Multi-format support (CAD/Revit/IFC) | Compile-verified and suite-green |
| Tessellation options | Complete, contract amended, mutation-verified test |
| Sidecar hashing | Complete, warn-first, contract amended, mutation-verified test |
| Optimized import manual verification | ISM import, ISM→HISM reimport, unchanged-source reimport confirmed in-editor |
| Rollback paths | Exercised at two depths, `RollbackFailed` degradation exercised, both tests mutation-verified |
| Legacy-vs-optimized UI disclosure | Implemented in the tools panel, contract amended |

### Current file sizes

| File | Lines |
| --- | --- |
| `ConVerseDatasmithImportService.cpp` | 2694 |
| `ConVerseHISMUtils.cpp` | 1293 |
| `ConVerseDatasmithImportPanel.cpp` | 1058 |
| `ConVerseOptimizedImportAutomation.cpp` | 944 |

---

## What is left

### Resolved since this file was written: sidecar hashing

This was the top item and the only known correctness gap. It is now closed.

`HashDirectory` folds the `<BaseName>_Assets` folder into one aggregate fingerprint persisted on the manifest. A sidecar-only change is detected and **warned about, not acted on**: status stays `AlreadyCurrent`, the world is untouched, no session is created, and the change surfaces in the summary, the log, and the panel's warning-toned status. The sidecar hash is deliberately excluded from `ComputePlanId`, because including it would bypass the `AlreadyCurrent` branch and cause the automatic destructive reimport that was explicitly declined.

Full content hashing was chosen over a metadata fast path on measured evidence: MD5 runs at ~250 MB/s, roughly 4s/GB, negligible beside CAD/Revit tessellation measured in minutes. Covered by `SidecarChangeWarnsWithoutReimport`, mutation-verified.

### 1. Rollback paths - closed

Was the highest-value open item. A test-only failure-injection seam (`FConVerseOptimizedImportOptions::FailureInjection`) now aborts the import at checkpoints that already route through `RollBackAttempt`, and three tests were added:

| Test | Covers |
| --- | --- |
| `InducedFailureRollsBackCleanly` | Successful rollback at two depths: post-import and post-verification pre-commit. Asserts the world actor count and dirty flag are restored, the destination holds no assets, no manifest is claimed, `CreatedObjectCount > 0` so a no-op rollback cannot pass, and the destination remains importable afterwards. |
| `ObstructedRollbackDegradesToRollbackFailed` | The `RollbackFailed` degradation. The obstruction suppresses only the destruction pass; the verification sweep then genuinely finds remaining objects and fails on its own terms. Self-cleaning. |
| `DeletedActorReportsDriftNotSuccess` | A hand-deleted optimized actor produces `AlreadyCurrent` with verification failure and a drift summary, and the branch stays read-only. Converts a manual-only check into a regression test. |

Both rollback tests were **mutation-verified**, which matters more than usual here because they guard the most destructive code in the plugin:

- Suppressing actor destruction inside `RollBackAttempt` made `InducedFailureRollsBackCleanly` fail at both depths, reporting 4 remaining objects and an unrestored world actor count.
- Hardcoding `bRollbackSucceeded = true` made `ObstructedRollbackDegradesToRollbackFailed` fail on the status, the success flag, and the stage.

Both mutations were reverted and the suite is green at 12/12.

### 2. Legacy path grouping breadth - test gap

The three legacy tests use `MaximumOptimization` and cover safety properties, not grouping breadth. Still unverified: BIM-hierarchy grouping mode, storey boundary patterns, Nanite auto-detection.

### 3. Legacy path has no manifest and no rollback - disclosed, still architectural

The architecture is unchanged: the legacy path still has no manifest and no rollback. Per `REVIEW.md`, the safest near-term step was to **make the difference visible** rather than rewrite it, and that is now done. The panel groups the two paths under **"Tracked import - verified, reversible"** and **"Selection tools - in-place, not reversible"**, with body text and tooltips stating plainly that the legacy operations record no manifest, perform no verification, and have no rollback.

The underlying asymmetry remains and is still worth eventually closing. It is just no longer a silent footgun.

### 4. Split `ConVerseDatasmithImportService.cpp` - maintainability

Now ~2850 lines holding planning, import, verification, manifest commit, supersede, rollback, and reporting. Largest maintenance risk by size. `REVIEW.md` item 6 says do this **only when the file next needs substantial change** - splitting it for its own sake is churn against a file that currently works.

### 5. Superseded asset package policy - deferred by design

Superseded packages are retained because external-reference safety cannot currently be determined. Deliberate, not an oversight. Revisit only when that safety can actually be established.

### 6. Manual verifications - one automated, one outstanding

- ~~Delete one optimized actor by hand, then reimport the unchanged source. **Expect a drift warning, not a success.**~~ Now covered by `DeletedActorReportsDriftNotSuccess`.
- Confirm a changed-source reimport leaves no duplicate world geometry and exactly one active session. **Still outstanding.** Partially exercised by the existing reimport test, but never confirmed against a real model.

Still genuinely manual, and unchanged by this run: exercising the importer against an actual Revit, IFC, or CAD export. That run is also the only way to sanity-check item 7 and to see sidecar warnings fire on real data.

### 7. Revisit the behavior-payload eligibility rule

`HasBehaviorPayload` currently rejects any actor with a Blueprint-added component, a component that is not a bare `USceneComponent`, or asset user data. This was a conservative default taken when the policy question was escalated and you were unavailable - preserve data rather than silently destroy it. **It may prove too strict on real BIM data.** Revisit against an actual Revit or IFC model.

---

## What is next - suggested order

With rollback exercised and the path guarantees disclosed, **there are no known correctness bugs and no untested destructive code left in the optimized import path.** What remains is breadth of coverage against real data, one architectural wart, and deferred policy.

1. **Exercise the importer against a real Revit, IFC, or CAD model.** **Partially done (2026-09-25).** A real Revit dataset that previously failed verification on every multi-material mesh now imports and verifies cleanly after the slot-id fix, confirmed by the user in-editor. That closes the multi-material question on genuine data. Still unconfirmed on a real model: the behavior-payload rule in item 7, sidecar-change warnings, and the changed-source reimport check in item 6.
2. **Close the legacy grouping test gap** (item 2): BIM-hierarchy mode, storey boundaries, Nanite auto-detection.
3. **Consider giving the legacy path a manifest** (item 3). It is now honestly labelled, but labelling is not a guarantee.
4. Leave the service split until that file needs substantial change anyway.

## Working agreements to carry forward

These are recorded in `AGENTS.md` and earned the hard way this run:

- **Any new option that changes generated output must be added to `ComputePlanId`.** Plan identity drives the `AlreadyCurrent` short-circuit; an input missing from the hash makes the importer report success while discarding the setting.
- **Mutation-test identity and guard tests.** The tessellation test was proven to fail when the fix was reverted. A guard test that has never been seen to fail proves nothing.
- **Amend `IMPORT_PANEL_VALIDATION.md` in the same change as the behavior**, not after.
- **Check for an existing implementation before declaring a defect.** A reimport-guard "defect" was claimed and later disproved by finding `ConVerseOptimizedReimportHandler.cpp`.
- **Re-test documented blockers before trusting them.** Multiple have turned out to be false.
