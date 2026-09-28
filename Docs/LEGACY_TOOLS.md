# Legacy selection tools

These tools operate on actors/assets already in the editor. They do not acquire the tracked importer's manifest, verification, guarded replacement, or rollback guarantees. Transaction support is operation-specific and is not a durable recovery system. [ADR 0009](ADR/0009-legacy-selection-boundary.md) records this boundary.

## Tools and behavior

| Tool | Current behavior |
|---|---|
| Analyze ISMs | Read-only grouping analysis for a selection, using the managed-conversion eligibility rules |
| Managed ISMs | Groups eligible actors, creates ISM/HISM components, copies effective materials/settings, then deletes successfully converted sources |
| Dedupe Meshes | Audits references, replaces eligible duplicate asset references, then attempts permanent deletion; dry-run available |
| Dedupe + ISMs | Runs managed conversion only if dedupe did not cancel or fail |
| Explode ISMs | Stages replacement actors; keeps the original component if recreation fails; removes it only after successful staging |
| Enable Nanite | Enables Nanite on selected referenced mesh assets; this selection utility does not use the tracked importer's compatibility/ownership/verification pass |
| Batch ISMs | Uses Unreal's `MergeComponentsToInstances`; no family-aware or geometry-signature equivalence contract |

Dedupe's interactive confirmation now precedes reference changes. Declining does not partially repoint the selection. The reference audit checks loaded components and on-disk package referencers, skipping external/uncertain use. Permanent asset deletion should not be described as guaranteed recoverable through Undo. Dataprep runs suppress dialogs.

## Managed conversion

Eligible sources have one supported mesh component and no unsupported behavior payload. Bare native scene-component roots can carry transform scaffolding; Blueprint-added components, other behavior-bearing components, and relevant user data prevent destructive simplification.

Grouping includes the family/cleanup boundary, geometry signature, effective material signature, and `FISMComponentDescriptorBase` settings. A mesh bounds-center discriminator avoids a known shifted-origin grouping case. It is not permission to group by bounds alone or proof of full equivalence across every LOD/collision/rendering attribute of separate assets.

`PreserveBIMHierarchy` respects family boundaries. `MaximumOptimization` relaxes family organization within the cleanup boundary, while retaining the other compatibility checks. Canonical assets are selected deterministically. Component world transforms preserve offsets below actor roots. Effective material overrides and descriptor settings are copied to output.

The minimum count defaults to two. Below-threshold actors remain; empty managed actors are cleaned up. Successful instance insertion is checked before its source is queued for deletion. Partial failure retains the affected source. Managed conversion cancellation may leave a partial result within the editor transaction; this differs from tracked import's attempt rollback.

Keep selection scope and existing managed results under review when rerunning. Broad BIM hierarchy/storey, partial-selection rerun, and legacy reimport behavior have not acquired the tracked lifecycle guarantees.

## Editor Blueprint and Dataprep API

Primary Blueprint entry points remain in `UConVerseHISMLibrary`: `CreateISMsFromSelection`, `AnalyzeISMCandidatesInSelection`, `ExplodeISMsFromSelection`, `EnableNaniteOnSelection`, and `MigrateTagsInCurrentLevel`. The older `CreateHISMsFromSelection` name is deprecated and delegates to the canonical entry point.

Other editor APIs include `UConVerseBatchHISMLibrary::BatchSelectionToHISMs`, `UConVerseStaticMeshConsolidationLibrary::ConsolidateSimilarStaticMeshes`, the consolidation widget base class, and `UConVersePowdercoatMaterialLibrary::CreatePowdercoatSubstrateMaterial`. These editor utilities are not runtime import APIs.

Dataprep exposes Create, Analyze, Create by Category, and Consolidate Similar Meshes operations. Their display names may retain historical HISM wording; `bUseHISM` and `bAutoDetectFromNanite` determine the actual component type. Category label filtering is a selection convenience, not source identity or geometric equivalence.

Preserve tags `ConVerseManagedHISM` and `ConVerseManagedFamilyType` and public Blueprint compatibility. Source files: [library](../Source/DatasmithHISM/Public/ConVerseHISMLibrary.h), [conversion helper](../Source/DatasmithHISM/Private/ConVerseHISMUtils.cpp), [dedupe library](../Source/DatasmithHISM/Private/ConVerseStaticMeshConsolidationLibrary.cpp).

## Evidence

Four legacy automation tests pass: behavior payload retention, below-threshold cleanup, effective material overrides, and component offsets/settings. Dedupe external-reference/confirmation and Explode failure/undo workflows still need the broader interactive acceptance listed in [validation](VALIDATION.md). A passing legacy safety test does not establish full BIM grouping coverage.
