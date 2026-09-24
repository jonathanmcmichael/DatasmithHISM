# DatasmithHISM project review

Reviewed at UE 5.8.3, editor target `AdvancedHISMEditor Win64 Development` building clean and all automation tests passing.

> **Update.** Recommendations 1 through 5 have since been implemented. The legacy path's four correctness gaps are fixed and it now has automation coverage. Sections below are annotated with resolutions.

## Summary

The plugin contains two subsystems that solve overlapping problems at very different quality levels.

The **optimized import** path (`ConVerseDatasmithImportService.cpp`) is in good shape: manifest-owned, verified, rollback-safe, reimport-aware, and covered by automation. Recent work closed the reimport gap and the `AlreadyCurrent` contract deviation.

The **legacy in-place conversion** path (`ConVerseHISMUtils.cpp` and its Dataprep/toolbar callers) is weaker. It still performs destructive actor deletion with no manifest and no rollback. The four correctness gaps recorded in `Info.md` and `JOURNAL.md` have now been fixed, and the path has gained automation coverage of its safety properties, but it remains architecturally less safe than the optimized path.

This asymmetry is still the single most important fact about the project. The manifest-backed path and the destructive path are both reachable from the same toolbar.

## Verified state

| Item | Status |
| --- | --- |
| `AdvancedHISMEditor Win64 Development` | Builds clean |
| `DatasmithHISM.OptimizedImport.OptimizerAwareReimport` | Pass |
| `DatasmithHISM.OptimizedImport.GeneratedFixtureEndToEnd` | Pass |
| `DatasmithHISM.LegacyConversion.BehaviorPayloadIsNotDestroyed` | Pass |
| `DatasmithHISM.LegacyConversion.MaterialOverrideSurvivesConversion` | Pass |
| `DatasmithHISM.LegacyConversion.BelowThresholdLeavesNoEmptyActors` | Pass |
| Optimized import manual verification | ISM import, ISM to HISM reimport, unchanged-source reimport all confirmed in-editor |
| Legacy conversion path | Safety properties covered by automation; grouping breadth still unverified |

## Size

| File | Lines |
| --- | --- |
| `ConVerseDatasmithImportService.cpp` | 2438 |
| `ConVerseHISMUtils.cpp` | 1291 |
| `ConVerseDatasmithImportPanel.cpp` | 677 |
| `ConVerseOptimizedImportAutomation.cpp` | 643 |
| `ConVerseStaticMeshConsolidationUtils.cpp` | 547 |
| `ConVerseHISMLibrary.cpp` | 479 |

`ConVerseDatasmithImportService.cpp` at 2438 lines is the largest maintenance risk by size. It holds planning, import, verification, manifest commit, supersede, rollback, and report generation in one translation unit.

## Confirmed defects in the legacy conversion path

All four were previously recorded as findings. **All four have since been fixed and are covered by automation**; the original analysis is retained below for context, with resolutions noted.

### 1. Material overrides are grouped on but never applied

`BuildMaterialSignature` (`ConVerseHISMUtils.cpp:491-504`) builds the grouping key from `Component->GetMaterial(Index)`, which returns the **effective** material including per-component overrides. `CopyRelevantComponentProperties` (`:531-547`) copies mobility, collision, shadow, decal, navigation, and cull-distance settings, but never assigns any material to the target component.

`ConVerseHISMUtils.cpp:1095` sets only the static mesh, so the created ISM renders with the mesh's default materials.

Consequence: two actors that differ **only** by material override are correctly placed in separate groups, then both render identically with default materials. The override is silently lost, and the grouping work that detected the difference is wasted.

**Resolved.** `CopyRelevantComponentProperties` now copies every effective material slot to the target component after `SetStaticMesh`. Safe because the grouping key guarantees all members of a group share the same material set.

### 2. `AddInstance` failure does not stop deletion

At `ConVerseHISMUtils.cpp:1108-1113`:

```cpp
for (const FSourceActorData& SourceActor : SourceActors)
{
	ISMComponent->AddInstance(SourceActor.WorldTransform, true);
	ConvertedActorsAndCleanupBoundaries.Emplace(SourceActor.Actor.Get(), SourceActor.CleanupBoundaryActor.Get());
	++Output.Result.SourceActorsConverted;
}
```

`AddInstance` returns the new instance index, or `INDEX_NONE` on failure. The return value is discarded. The source actor is queued for deletion and counted as converted on the next two lines regardless of outcome.

Consequence: if `AddInstance` fails, the original geometry is deleted and no instance replaces it. This is silent data loss, and `SourceActorsConverted` overreports success.

Note the contrast with component creation directly above at `:1100-1106`, which *does* validate and back out cleanly. The per-instance path simply lacks the equivalent check.

**Resolved.** The index is now checked; on `INDEX_NONE` the actor is kept, `FailedInstanceAdditions` is incremented, and a warning is logged. A component left with zero instances is destroyed.

### 3. Eligibility ignores non-mesh payload

Eligibility (around `ConVerseHISMUtils.cpp:165-220`) counts suitable static mesh components. It does not reject an actor carrying audio components, particle systems, scripted or Blueprint behavior, child actor components, or user data. Converted actors then become unconditional delete candidates.

Consequence: an actor whose mesh is instanceable but which also carries behavior is destroyed, and only its geometry survives as an instance.

**Resolved.** `HasBehaviorPayload` now rejects such actors, counted in `ActorsWithBehaviorPayload`. See the assumption recorded under the recommendations for the exact scope.

### 4. Below-threshold groups can leave empty family actors

Filtering groups below the instance threshold can leave managed family-type actors with no remaining children. Cleanup runs on cancellation but not on the normal path (`:1141-1156`).

Consequence: empty actors accumulate in the outliner after a successful run.

**Resolved.** Cleanup now runs on every path, and skips any actor with attached children so pre-existing parents are never destroyed.

## Architectural observations

**Two conversion systems, one toolbar.** The optimized import path enforces manifest ownership, pre-import inventory snapshots, verification, and rollback. The legacy path enforces none of these. A user cannot tell from the UI which guarantees apply. The safest near-term step is to make the difference explicit in the UI rather than to rewrite the legacy path.

**`CreatedActors` semantics are sound.** `CommitManifestAndOwnership` populates it by diffing all world actors against the pre-import snapshot, so it captures everything a session introduced, including optimizer-rejected actors. Supersede cleanup therefore removes rejected geometry correctly. This was verified during the reimport work.

**Documentation has drifted from reality more than once.** `HANDOFF.md` previously asserted that automation could not run because of LinuxArm64 and VisionOS SDK validation. That was false: the messages come from a `-Mode=ValidatePlatforms` subprocess and never blocked anything. A stale `Source SHA-256` report label survived a hashing change to MD5. Documented blockers in this project should be re-tested before being trusted.

## Recommendations, in priority order

1. ~~**Fix the `AddInstance` deletion gap (`:1108-1113`).**~~ **RESOLVED.** The return index is now checked; on `INDEX_NONE` the source actor is not queued for deletion, `FailedInstanceAdditions` is incremented, and a warning naming the actor is logged. A component that ends up with zero instances is destroyed rather than left inert.
2. ~~**Apply material overrides in `CopyRelevantComponentProperties`.**~~ **RESOLVED.** Effective materials are copied per slot after `SetStaticMesh`. Covered by `DatasmithHISM.LegacyConversion.MaterialOverrideSurvivesConversion`.
3. ~~**Decide the eligibility contract for non-mesh payload.**~~ **RESOLVED — see assumption below.** Actors carrying behavior are now rejected via `HasBehaviorPayload` and counted in `ActorsWithBehaviorPayload`.
4. ~~**Add automation coverage for the legacy path.**~~ **RESOLVED.** `Private/Tests/ConVerseHISMLegacyAutomation.cpp` adds three tests under `DatasmithHISM.LegacyConversion`. They call `ConVerseHISM::BuildManagedHISMs` directly, because the Blueprint entry points are selection-based and need `GEditor` selection state.
5. ~~**Clean up empty family actors on the success path.**~~ **RESOLVED.** The cleanup no longer runs only on cancellation. It skips actors with attached children so a pre-existing actor reused as a family parent is never destroyed.
6. **Consider splitting `ConVerseDatasmithImportService.cpp`.** Planning, verification, and manifest/supersede handling are separable. Only worth doing when that file next needs substantial change. **Still open.**
7. **Superseded asset package policy.** Still deferred by design. Superseded packages are retained because external reference safety cannot currently be determined.

### Assumption recorded for item 3

The policy question was escalated but the user was unavailable, so the conservative default was taken: preserve data rather than silently destroy it. An actor is treated as behavior-bearing when it has a Blueprint-added (non-native) component, a component whose class is anything other than a bare `USceneComponent`, or asset user data. A bare `USceneComponent` is deliberately treated as transform scaffolding and does **not** block conversion, since rejecting it would exclude ordinary Datasmith hierarchy actors. Revisit if this proves too strict on real BIM data.

## Not yet exercised

- Rollback paths in the optimized import. They only trigger on failure, and no failure has been induced. Reaching them requires deliberately forcing an error, for example by locking the attempt folder mid-import.
- The legacy path's BIM-hierarchy grouping mode, storey boundary patterns, and Nanite auto-detection. The new tests use `MaximumOptimization` and cover the safety properties, not grouping breadth.
