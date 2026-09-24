# DatasmithHISM - next steps

> Written at the close of the tessellation run and updated at the close of the sidecar run. Verified at UE 5.8.3, editor target `AdvancedHISMEditor Win64 Development` building clean, **9/9 automation tests passing**.
>
> This project has a documented history of stale docs (a false "automation is blocked by SDK validation" claim, a stale `SHA-256` label after a switch to MD5, and a "NOT COMPILE-VERIFIED" banner that outlived its own fix). **Re-verify anything here before acting on it.** Every status below was checked against the source or a live run at the time of writing, not copied forward.

## Where the run ended

The optimized import path is manifest-owned, verified, rollback-safe, reimport-guarded, format-agnostic, and now tessellation-aware. The legacy in-place conversion path has had its four correctness defects fixed and has automation covering its safety properties, but it remains architecturally weaker - no manifest, no rollback.

**That asymmetry is still the most important fact about this project.** Both paths are reachable from the same toolbar and a user cannot tell from the UI which guarantees apply.

### Verified state

| Item | Status |
| --- | --- |
| `AdvancedHISMEditor Win64 Development` | Builds clean |
| Automation suite (`DatasmithHISM`) | 9/9 passing |
| Multi-format support (CAD/Revit/IFC) | Compile-verified and suite-green (supersedes the stale "NOT COMPILE-VERIFIED" note in `HANDOFF.md`) |
| Tessellation options | Complete, contract amended, mutation-verified test |
| Sidecar hashing | Complete, warn-first, contract amended, mutation-verified test |
| Optimized import manual verification | ISM import, ISM→HISM reimport, unchanged-source reimport confirmed in-editor |

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

### 1. Rollback paths are unexercised - test gap

**Now the highest-value open item.** Rollback only triggers on failure, and no failure has ever been induced, so `RollBackAttempt` and the `RollbackFailed` degradation have **never actually run**. This is untested destructive code on the path that exists specifically to protect user data.

Reaching it requires deliberately forcing an error - for example locking the attempt folder mid-import, or injecting a failure behind a test-only hook. Worth doing precisely because the consequences of a broken rollback are worst-case.

### 2. Legacy path grouping breadth - test gap

The three legacy tests use `MaximumOptimization` and cover safety properties, not grouping breadth. Still unverified: BIM-hierarchy grouping mode, storey boundary patterns, Nanite auto-detection.

### 3. Legacy path has no manifest and no rollback - architectural

Unchanged by any work so far. `REVIEW.md` recommends the safest near-term step is to **make the difference visible in the UI** rather than rewrite the legacy path. That is a small, high-value change: a user currently cannot tell which set of guarantees they are getting.

### 4. Split `ConVerseDatasmithImportService.cpp` - maintainability

Now ~2850 lines holding planning, import, verification, manifest commit, supersede, rollback, and reporting. Largest maintenance risk by size. `REVIEW.md` item 6 says do this **only when the file next needs substantial change** - splitting it for its own sake is churn against a file that currently works.

### 5. Superseded asset package policy - deferred by design

Superseded packages are retained because external-reference safety cannot currently be determined. Deliberate, not an oversight. Revisit only when that safety can actually be established.

### 6. Two manual verifications never performed

From the original handoff, still outstanding:
- Delete one optimized actor by hand, then reimport the unchanged source. **Expect a drift warning, not a success.**
- Confirm a changed-source reimport leaves no duplicate world geometry and exactly one active session.

### 7. Revisit the behavior-payload eligibility rule

`HasBehaviorPayload` currently rejects any actor with a Blueprint-added component, a component that is not a bare `USceneComponent`, or asset user data. This was a conservative default taken when the policy question was escalated and you were unavailable - preserve data rather than silently destroy it. **It may prove too strict on real BIM data.** Revisit against an actual Revit or IFC model.

---

## What is next - suggested order

With the sidecar gap closed, **there are no known correctness bugs left in the optimized import path.** What remains is test coverage, one architectural wart, and deferred policy.

1. **Force a rollback and watch it run.** Untested destructive code is now the biggest risk in the plugin.
2. **Make the legacy-vs-optimized distinction visible in the UI.** Small, cheap, and removes a real footgun.
3. **Exercise the two manual reimport scenarios** against a real model - the same run can sanity-check the behavior-payload rule from item 7 and confirm sidecar warnings behave sensibly on genuine Revit or CAD data.
4. Leave the service split until that file needs substantial change anyway.

## Working agreements to carry forward

These are recorded in `AGENTS.md` and earned the hard way this run:

- **Any new option that changes generated output must be added to `ComputePlanId`.** Plan identity drives the `AlreadyCurrent` short-circuit; an input missing from the hash makes the importer report success while discarding the setting.
- **Mutation-test identity and guard tests.** The tessellation test was proven to fail when the fix was reverted. A guard test that has never been seen to fail proves nothing.
- **Amend `IMPORT_PANEL_VALIDATION.md` in the same change as the behavior**, not after.
- **Check for an existing implementation before declaring a defect.** A reimport-guard "defect" was claimed and later disproved by finding `ConVerseOptimizedReimportHandler.cpp`.
- **Re-test documented blockers before trusting them.** Multiple have turned out to be false.
