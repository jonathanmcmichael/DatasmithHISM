# DatasmithHISM Handoff

> ## Validation status: CLEAN
>
> The working tree **builds and passes all 9 automation tests** as of the latest session
>
> Verification commands:
>
> 1. `& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" AdvancedHISMEditor Win64 Development -Project="D:\Unreal\Sandbox\AdvancedHISM\AdvancedHISM.uproject" -WaitMutex`
> 2. `& "...\UnrealEditor-Cmd.exe" "...\AdvancedHISM.uproject" -ExecCmds="Automation RunTests DatasmithHISM; Quit" -unattended -nopause -nosplash -NullRHI -log -abslog="...\Saved\Logs\Run.txt"`
>
> `get_errors` / IntelliSense is **not** a substitute: it cannot resolve engine headers in this project and reports ~45 false errors on untouched code. Only `Build.bat` validates.
>
> **Outstanding work is consolidated in `NEXT_STEPS.md`.** The per-topic "Remaining work" sections below are kept for historical context; `NEXT_STEPS.md` is the current list.

## Current state

The plugin builds successfully with Unreal Engine 5.8.3. The user manually confirmed these editor workflows work:

- Optimized Datasmith import of the toilet fixture after the crash fix.
- The new dockable **DatasmithHISM Tools** panel and its toolbar launcher.
- Automatic Nanite enablement for plain ISM output.

The workspace root is not a Git working tree, so no Git status is available.

Agent-facing repository guidance lives in `AGENTS.md` at the workspace root. Condensed Datasmith reference documentation, and how it constrains this plugin, lives in `Docs/`:

- `Docs/DatasmithExportSDKGuidelines.md` - the exporter-side SDK guidelines.
- `Docs/DatasmithImportCustomizationGuidelines.md` - the importer-side customization process. This one documents the two-stage translate/finalize seam this plugin is built on.

Epic's own SDK guidelines independently corroborate the sidecar definition that drove the hashing work recorded below: a Datasmith "file" is a primary `.udatasmith` file **plus** its `[filename]_Assets` sidecar folder, with all assets referenced by relative paths inside that folder. That folder is now hashed as part of source identity.

Epic's import-customization page independently corroborates the reimport guard: pre-import scene modification is **bypassed during an ordinary reimport**, so elements removed or collapsed before finalize are detected as newly added and re-created. For this plugin that would resurrect the mesh actors collapsed into ISM/HISM components. The manifest and the reimport guard are therefore load-bearing, not defensive extras.


## Completed work

### SHA-256 import crash

**Cause:** The packaged UE 5.8 Windows editor has no SHA-256 platform implementation. Calling `FPlatformMisc::GetSHA256Signature` asserted during optimized import analysis.

**Fix:** `ConVerseDatasmithImportService.cpp` now uses portable Unreal Core MD5 helpers (`FMD5` and `FMD5Hash`) for deterministic source, group, and plan fingerprints. `IMPORT_PANEL_VALIDATION.md` was updated from SHA-256 to MD5 terminology.

### Validation baseline

`VALIDATION_BASELINE.md` records the engine/build environment, fixture inventory, smoke-test checklist, and known runtime test limitations.

### Toolbar consolidation

The Level Editor toolbar now contains one **Datasmith Tools** launcher. It opens a dockable **DatasmithHISM Tools** panel containing the former actions plus an Optimized Datasmith Import launcher.

Key files:

- `Private/ConVerseDatasmithToolsPanel.h/.cpp`
- `Private/DatasmithHISM.cpp`
- `Public/DatasmithHISM.h`

### Automatic Nanite for ISM

Plain ISM creation now enables Nanite on its canonical mesh by default. HISM creation does not automatically alter Nanite settings.

This applies to both:

- Managed ISMs created from selection in `ConVerseHISMUtils.cpp`
- Optimized Datasmith Import conversion in `ConVerseDatasmithImportService.cpp`

`ConVerseHISM::EnableNaniteIfNeeded` centralizes the mesh mutation and is also used by the manual **Enable Nanite** action.

### Optimizer-aware reimport

`ImportAndVerify` no longer hard-stops with `OptimizedReimportBlocked` when an active manifest owns the same normalized source and destination. It now runs a full reimport transaction.

Flow:

1. `FindActiveManifest` result is captured as `PreviousManifest`, rooted in a `TStrongObjectPtr` because the Datasmith import can trigger garbage collection.
2. An unchanged `PlanId` returns `AlreadyCurrent` with no session and no world mutation.
3. A changed plan imports and verifies a new session in its own `<Destination>/<Asset>_ConVerse_<SessionId>` attempt folder while the old session stays intact.
4. `PreflightSupersedeSession` resolves the prior `CreatedActors` before any destruction and fails if the owning world is not the current editor world. Actors a user already deleted are logged, not treated as errors.
5. `CommitManifestAndOwnership` takes a `PreviousManifest` parameter and records `PreviousManifestId`, `PreviousManifestObjectPath`, and `PreviousSessionId`.
6. `RemoveSupersededSession` destroys prior-session actors deepest-child-first.
7. `MarkManifestSuperseded` sets the old manifest and its matching asset markers to `Superseded`.

Failure at step 4, 5, or 6 rolls back the new attempt with the existing `RollBackAttempt` and leaves the old session active, per the documented rollback policy.

Superseded asset packages are intentionally retained. They may be externally referenced, so this first implementation never deletes them.

`FConVerseOptimizedReimportHandler` is unchanged and still blocks ordinary Content Browser reimport.

New result fields on `FConVerseOptimizedImportResult`: `bWasReimport`, `PreviousManifestId`, `PreviousSessionId`, `RemovedPreviousActorCount`.

The panel calls the new `FConVerseDatasmithImportService::HasActiveOptimizedImport` probe. The result is cached in `bActiveImportExists` and refreshed only from `MarkInputsChanged` and `SetResult`, never during paint. When set, the button reads **Optimized Reimport and Verify**.

The stale `Source SHA-256` report label was corrected to `Source MD5`, matching the actual hashing.

## Build evidence

The following target built successfully after the optimizer-aware reimport changes:

`AdvancedHISMEditor Win64 Development`

The project editor may be running with Live Coding enabled. Disable Live Coding with **Ctrl+Alt+F11** or close the editor before invoking an external UBT build.

## Manual verification evidence

Confirmed in-editor: an optimized import producing **ISM** components, followed by a reimport with the instance type changed to **HISM**, works. The changed component type produces a different PlanId, so the reimport takes the supersede path rather than `AlreadyCurrent`: a new session is imported and verified in its own attempt folder, the prior session's actors are removed, the old manifest is marked `Superseded`, and the new manifest becomes `Active`.

This exercises the highest-risk sequence end to end: `PreflightSupersedeSession` -> `CommitManifestAndOwnership` -> `RemoveSupersededSession` -> `MarkManifestSuperseded`.

Also confirmed in-editor: reimporting an **unchanged** source takes the `AlreadyCurrent` path. This exercises `ReverifyCommittedSession` in its passing case, so the manifest-based re-verification resolves components, classes, instance counts, meshes, material slots, and created actors correctly against a real committed session.

Still unexercised at runtime:

- the **failing** side of the drift check, for example hand-deleting an optimized actor and then reimporting the unchanged source, which should report drift rather than success;
- every rollback path, since no failure has been observed yet.

## Remaining work

1. Verify the drift case: delete one optimized actor by hand, then reimport the unchanged source. Expect a drift warning, not a success.
2. Confirm a changed-source reimport leaves no duplicate world geometry and exactly one active session.
3. Consider a follow-up policy for deleting or garbage-collecting superseded asset packages once external-reference safety can be determined.

## `AlreadyCurrent` runs verification only

`IMPORT_PANEL_VALIDATION.md` requires that a reimport with an unchanged source hash, options, and PlanId "runs verification only" (reimport policy item 9, and the status table entry for `AlreadyCurrent`).

`ReverifyCommittedSession` implements this. It re-checks the existing active session against its own manifest and is strictly read-only: it resolves nothing new and mutates neither the world nor any package.

Per manifest group record it checks:

- the `OutputComponentPath` still resolves to a valid component;
- the component class exactly matches `RequestedComponentType`;
- the instance count matches `ExpectedInstanceCount`;
- the static mesh matches `ImportedStaticMeshPath`;
- every `MaterialSlots` entry is in range and matches `ImportedMaterialPath`.

It then confirms every `CreatedActors` record still resolves.

This makes `AlreadyCurrent` a real drift check. If a user hand-deletes an optimized actor or edits an instance count, an unchanged-source reimport now reports the drift instead of silently claiming the output is current. The panel shows the drift case as a warning, not a success, and still does not modify the world.

## Automation results

Automation **does** run from the command line. An earlier note in this file claimed the packaged launcher could not start automation because of LinuxArm64 and VisionOS SDK validation. That was wrong. Those messages come from a `-Mode=ValidatePlatforms` UnrealBuildTool subprocess that the editor spawns at startup; it is unrelated noise and does not abort the run.

Working command:

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "D:\Unreal\Sandbox\AdvancedHISM\AdvancedHISM.uproject" -ExecCmds="Automation RunTests DatasmithHISM; Quit" -unattended -nopause -nosplash -NullRHI -log -abslog="D:\Unreal\Sandbox\AdvancedHISM\Saved\Logs\AutomationAll.txt"
```

Results: all six tests pass.

- `DatasmithHISM.OptimizedImport.OptimizerAwareReimport` - **Success**, including the `AlreadyCurrent` re-verification assertions.
- `DatasmithHISM.OptimizedImport.GeneratedFixtureEndToEnd` - **Success** after the label fix below.
- `DatasmithHISM.OptimizedImport.FutureSchemaManifestIsRefused` - **Success**. See "Manifest schema versioning".
- `DatasmithHISM.LegacyConversion.BehaviorPayloadIsNotDestroyed` - **Success**.
- `DatasmithHISM.LegacyConversion.MaterialOverrideSurvivesConversion` - **Success**.
- `DatasmithHISM.LegacyConversion.BelowThresholdLeavesNoEmptyActors` - **Success**.

## Resolved: GeneratedFixtureEndToEnd label mismatch

This test previously failed in both ISM and HISM modes:

```
Expected 'ISM rejected mirrored actor remains imported' to be not null.
ConVerseOptimizedImportAutomation.cpp(442)
```

Diagnostic logging of every `CreatedActors` record showed the actor was present and correctly rejected, but its label was `Mirrored_Negative_Instance`. Datasmith sanitizes spaces to underscores on import, while the fixture sets the source label to `Mirrored Negative Instance`. The lookup compared against the unsanitized form and never matched.

This was purely a test bug. `FindCreatedActorByLabel` now normalizes spaces to underscores before comparing.

Two things this ruled out, both of which had been raised as open questions:

- `CreatedActors` semantics are **not** ambiguous. `CommitManifestAndOwnership` populates it by diffing every world actor against the pre-import `Inventory.ExistingActors` snapshot, so it means "every actor this session brought into the world", rejected actors included.
- Because rejected actors are already in `CreatedActors`, `PreflightSupersedeSession` and `RemoveSupersededSession` already clean them up. Superseding a session does **not** orphan rejected geometry, and the earlier concern about the ISM to HISM reimport leaving a stray mirrored actor was unfounded.

## Legacy conversion path hardening

All four long-standing correctness gaps in `ConVerseHISMUtils.cpp` are now fixed, following the priority order in `REVIEW.md`.

1. **`AddInstance` deletion gap.** The return index was discarded, so a failed instance add still deleted the source actor. It is now checked; on `INDEX_NONE` the actor is kept and not queued for deletion, `FailedInstanceAdditions` is incremented, and a warning naming the actor is logged. A component that ends up with zero instances is removed and destroyed.
2. **Material overrides.** `CopyRelevantComponentProperties` now copies every effective material slot after `SetStaticMesh`. The grouping key already guarantees group members share a material set.
3. **Non-mesh payload.** New `HasBehaviorPayload` helper plus `EActorEligibilityFailureReason::HasBehaviorPayload` and an `ActorsWithBehaviorPayload` counter. An actor is rejected when it has a Blueprint-added component, a component whose class is anything other than a bare `USceneComponent`, or asset user data.
   **Assumption:** this policy decision was escalated but the user was unavailable, so the conservative "preserve data" default was taken. A bare `USceneComponent` is deliberately treated as transform scaffolding and does not block conversion, since rejecting it would exclude ordinary Datasmith hierarchy actors. Revisit if it proves too strict on real BIM data.
4. **Empty family actors.** Cleanup no longer runs only on cancellation. It now runs on every path and skips actors with attached children, so a pre-existing actor reused as a family parent is never destroyed.

Two new `FConVerseHISMCreationResult` counters, `FailedInstanceAdditions` and `ActorsWithBehaviorPayload`, are reported through `FinalizeSummary`, so existing panel and Dataprep summaries pick them up automatically.

Coverage lives in `Private/Tests/ConVerseHISMLegacyAutomation.cpp`. The tests call `ConVerseHISM::BuildManagedHISMs` directly, because `UConVerseHISMLibrary::CreateISMsFromSelection` is selection-based and needs `GEditor` selection state that is awkward to stage headlessly. When asserting on the created component, the tests snapshot pre-existing managed components first and diff, since the shared editor world persists between tests.

## Remaining work

1. Consider a follow-up policy for deleting or garbage-collecting superseded asset packages once external-reference safety can be determined.
2. Rollback paths remain unexercised, since they only trigger on failure and no failure has been observed.
3. The legacy path's BIM-hierarchy grouping mode, storey boundary patterns, and Nanite auto-detection are still unverified. The new tests use `MaximumOptimization` and cover safety properties, not grouping breadth.
4. The legacy path still has no manifest and no rollback. That is an architectural gap, not a defect, and is unchanged by this work.

The reimport test re-exports the fixture over the same canonical source path with one extra instance, so `CreateFixture` now accepts `ExtraInstances` and an `ExistingRoot`.

## Multi-format source support (CAD / Revit / IFC)

**Status: complete and verified.** This section originally read "code-complete but NOT COMPILE-VERIFIED" because Live Coding held the binaries during that session. That banner is now stale: the target has since built clean and the full suite has passed repeatedly, including after the tessellation work. Retained here as an example of exactly the doc drift this project keeps producing - a blocker recorded once and left standing after it was resolved.

The importer previously rejected anything that was not `.udatasmith`. That restriction was artificial: `LoadFreshSource` already resolved sources through `IExternalSourceModule::GetOrCreateExternalSource`, and the import already ran through `UDatasmithImportFactory::CreateFromExternalSource`. Both were format-agnostic from the start, so only the validation gate needed removing.

- `ValidateOptions` no longer checks the extension. Translator resolution in `LoadFreshSource` is now the authoritative format gate, and its failure message names the extension and points at a possibly disabled plugin.
- The panel derives both `ValidateSource` and the file-dialog filter from `FDatasmithTranslatorManager::GetSupportedFormats()`, which returns `"ext;description"` entries. Enabling a format plugin therefore widens the importer with no code change. A `.udatasmith` + all-files fallback covers the case where no translators resolve.
- Added the `DatasmithTranslator` module to `DatasmithHISM.Build.cs`. **This dependency is the single most likely thing to fail the first build.**
- Generalized user-facing strings in `ConVerseDatasmithImportPanel.cpp`, `ConVerseDatasmithToolsPanel.cpp`, and `DatasmithHISM.cpp`. The fallback filter and the automation fixture strings intentionally still say `.udatasmith`, because that fixture genuinely is one.
- `IMPORT_PANEL_VALIDATION.md` gained "Amendment 1", widening the frozen contract first, as the contract itself requires.

## Manifest schema versioning

`FindActiveManifest` fails closed: a manifest whose `ManifestSchemaVersion` exceeds `ContractVersion` will never be treated as active. Supersede destroys actors, so matching a manifest written by a newer build risks destroying output this build cannot interpret.

**An earlier version of this gate was wrong, and the test caught it.** The first implementation simply `continue`d past the future-schema manifest, and this file claimed that was safe because the destination guard would then return `OptimizedReimportBlocked`. That reasoning was false. The imported scene asset lives in the per-session attempt folder (`DestinationPath/AssetName_ConVerse_<sessionId>`), **not** at `ConventionalObjectPath`, so `GetAssetByObjectPath(ConventionalPath)` is invalid and that guard never fires. `DatasmithHISM.OptimizedImport.FutureSchemaManifestIsRefused` failed on its first run with a full duplicate import that added 4 actors (151 -> 155).

The corrected design reports the refusal explicitly. `FindActiveManifest` takes an optional `OutUnreadableManifestError`; a manifest that matches source and destination but carries a future schema sets that string and returns `nullptr`. `ImportAndVerify` checks it first and returns `OptimizedReimportBlocked` with a message naming the schema version, before any mutation. Identity checks were also moved ahead of the version check so only genuinely matching manifests can trigger the refusal.

The test asserts the status, that the actor count is unchanged, that the newer manifest stays `Active`, and that every actor it owns still resolves.

## Correction: per-instance settings were never transform-only

An earlier audit claimed collision and other per-instance settings were lost during optimization. **That claim is false.** Reading the code shows collision is captured at `:1040`, verified at `:1080`, and transferred at `:1180-1183` via `SetCollisionProfileName`, `SetCollisionEnabled`, `SetGenerateOverlapEvents`, and `SetCullDistances`, alongside mobility, visibility, hidden-in-game, cast shadow, component tags, and per-slot materials at `:1173-1188`.

`FConVerseOptimizedComponentSettings` records group-identity settings; it is not the transfer mechanism. `ApplyResolvedPlan` works at the Datasmith element layer, which exposes no collision API at all.

Planned work to "add collision fidelity" was dropped as busywork. This was the second false claim from that same audit, the first being an `O(n^2)` scalability claim that was also disproved by reading the code (`ExistingActors` and `ClaimedActors` are both `TSet`). **Any remaining unverified audit claim should be checked against the source before acting on it.**

## Tessellation options (done)

The panel exposes chord tolerance, normal tolerance, max edge length, and stitching in a collapsed "Tessellation (CAD, Revit, IFC)" section; the commandlet mirrors them as `-ChordTolerance`, `-NormalTolerance`, `-MaxEdgeLength`, `-Stitching`. `ApplyTessellationOptions` pushes them into the translator before `TryLoad`, following Epic's `SetDefaultTranslatorOptions` pattern. See Amendment 4 in the contract.

Implementing this surfaced a real defect that was fixed in the same change: **`ComputePlanId` did not include tessellation**, so a tessellation-only change hashed identically, matched the active manifest, took the `AlreadyCurrent` branch, and would have silently discarded the user's settings while reporting success. `TessellationAffectsPlanIdentity` covers it and was mutation-verified - removing chord tolerance from the hash makes the test fail with the expected message.

Formats that are already tessellated, notably `.udatasmith`, expose no tessellation options. The panel reports applicability as unknown until a run actually observes it rather than guessing from the extension, and `Result.bTessellationApplied` carries that answer.

## Sidecar hashing (done)

The contract required source *and sidecar* hashes, but `HashFile` only ever hashed the primary file, so a source whose sidecars changed while the primary file did not was misreported as `AlreadyCurrent` and silently retained stale geometry. This was the last known correctness gap in the optimized path.

`HashDirectory` now folds the entire `<BaseName>_Assets` folder into one aggregate fingerprint, persisted on the manifest as `SidecarHash` / `SidecarTotalSize` / `SidecarFileCount`. Relative paths are sorted before folding, because directory enumeration order is not guaranteed stable and an unsorted fold would warn on every run.

**The behavior is warn-first, chosen by the user.** A sidecar-only change keeps the `AlreadyCurrent` status, stays read-only, creates no session, and reports the change through `bSidecarChanged`, the summary, a log warning, and the panel's warning-toned status. Superseding is destructive, so rebuilding is the user's call.

**The sidecar hash is deliberately excluded from `ComputePlanId`** - the inverse of the tessellation rule, and intentionally so. Folding it in would change the PlanId, bypass the `AlreadyCurrent` branch, and produce exactly the automatic destructive reimport that was declined.

An empty recorded hash means "not recorded", never "changed", so manifests predating the field do not warn spuriously. Covered by `DatasmithHISM.OptimizedImport.SidecarChangeWarnsWithoutReimport`, mutation-verified: forcing `bSidecarChanged` to false fails the test on exactly the detection and summary assertions.

Cost was measured before choosing full content hashing over a metadata fast path: MD5 runs at ~250 MB/s, about 4s/GB, which is negligible beside CAD/Revit tessellation measured in minutes. A metadata fast path would have traded that for timestamp logic that is unreliable across copies, network shares, and VCS checkouts.

## Resolved: progress and cancellation

This was an unmet existing contract requirement (lines 305, 307, 361, 362), now implemented.

`ImportAndVerify` runs under an `FScopedSlowTask` with nine progress frames: hash, load, plan, preflight, transform, import, convert, verify, commit. `MakeDialog(true)` is only called when `!bAutomated`, so automation and the commandlet stay headless.

Cancellation is tiered by what the operation owns:

- **Before the first mutation** (after hash, load, plan, preflight): the `WasCancelled` helper returns `CancelledRolledBack` immediately. Nothing was changed, so no rollback is needed.
- **After the Datasmith import**: the attempt owns real assets and actors, so cancellation unwinds through `RollBackAttempt`, degrading to `RollbackFailed` if the unwind itself fails.
- **After verification**: cancellation is deliberately no longer offered. The session is committing ownership and superseding must be atomic.

The pre-existing `bCancelled` out-param from `CreateFromExternalSource` is unchanged and still maps to `CancelledRolledBack`.

On the panel, `EPanelStatus` gained `Cancelled` and `Blocked`. `SetResult` now reports `CancelledRolledBack` and `OptimizedReimportBlocked` as distinct, recoverable outcomes in warning color, instead of collapsing both into the generic "Import failed or was cancelled" message. `Working` is retained and documented: the service's modal slow-task dialog owns on-screen progress for the synchronous window, so `Working` is only observable if an operation returns without reporting a result.

## Headless commandlet

`UConVerseOptimizedImportCommandlet` drives the optimized import for CI and batch conversion:

```
UnrealEditor-Cmd.exe <Project> -run=ConVerseOptimizedImport
    -Source="C:/Path/Scene.udatasmith"
    [-Destination=/Game/DatasmithOptimized]
    [-InstanceType=ISM|HISM]
    [-MinInstances=2]
    [-AnalyzeOnly]
```

It forces `bAutomated = true` so it never prompts. The exit policy is deliberately strict: only `Verified`, or `AlreadyCurrent` **with** re-verification passing, returns 0. Rolled-back failures, cancellation, and blocked reimports all return 1 so a build step fails loudly.

Verified end-to-end against a real fixture: exit code 0 with `Status=Verified Groups=1/1 Instances=2/2`. A missing source returns exit code 1.

