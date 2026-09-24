# Optimized Datasmith Import Contract and Validation Matrix

Status: Phase 1 contract, version 1, frozen for the first implementation pass on 2026-09-22.

> **Amendment 1 - source formats.** The scope is widened from `.udatasmith` only to every format the enabled Datasmith translators accept, including CAD, Revit, and IFC. Rationale: `LoadFreshSource` already resolves sources through `IExternalSourceModule::GetOrCreateExternalSource`, and the import already runs through `UDatasmithImportFactory::CreateFromExternalSource`, so both paths were format-agnostic from the start; the `.udatasmith` restriction was an artificial validation gate rather than an architectural limit. Translator resolution is now the authoritative format check. This widens capability without weakening any verification, rollback, or ownership guarantee. Sections below that name `.udatasmith` should be read as "the source file" unless they specifically concern the Datasmith exporter format.

This note defines the service, manifest, lifecycle, verification, rollback, reimport, and acceptance behavior for the **Optimized Datasmith Import** panel in [IMPORT_PANEL_PLAN.md](IMPORT_PANEL_PLAN.md). Implementation agents may add private helpers, but they must not weaken these guarantees without revising this contract first.

## Scope and first-release boundaries

The first release accepts a local source file in any format an enabled Datasmith translator can read, builds a read-only plan, rewrites eligible repeated mesh actors in a fresh in-memory Datasmith scene, imports through Unreal's Datasmith importer, produces the requested ISM or HISM components, persists source identity, and verifies the result.

The importer must not maintain its own extension allowlist. Whether a file is supported is decided by translator resolution, so enabling or disabling a format plugin changes the accepted set with no code change.

The first release groups only actors that reference the same Datasmith mesh element. It does not infer geometry equivalence between separate mesh elements. That later feature needs payload comparison, pivot compensation, and a wider mesh-settings equivalence check.

The on-disk source file and every sidecar file are read-only inputs. Their hashes must be unchanged after Analyze, Import and Verify, cancellation, failure, and optimized reimport.

> **Resolved (Amendment 5).** Sidecar files are now hashed. `HashDirectory` folds the entire `<BaseName>_Assets` folder into one aggregate fingerprint recorded on the manifest, so a source whose sidecars changed while the primary file did not is detected rather than misreported as silently current. See Amendment 5 for the reporting policy.

## Confirmed UE 5.8 API assumptions

These facts were checked in the installed UE 5.8 source and are part of the implementation contract:

- `UDatasmithImportFactory::CreateFromExternalSource` accepts an `FExternalSource` and returns cancellation through `bOutOperationCanceled`.
- `IDatasmithScene` exposes scene exporter fields and associated metadata through `GetMetaData(Element)`.
- `IDatasmithHierarchicalInstancedStaticMeshActorElement` is imported as a `UHierarchicalInstancedStaticMeshComponent`. Datasmith adds its instance transforms as component-local transforms.
- Datasmith applies an actor element's relative transform, mobility, visibility, cast-shadow setting, and tags when it sets up the imported scene component.
- `UDatasmithScene` implements `IInterface_AssetUserData` and can own a persistent manifest object.
- `UDatasmithScene::StaticMeshes` and `UDatasmithScene::Materials` map Datasmith element names to imported assets. Verification must use these mappings instead of comparing sanitized display names.
- `FReimportManager::OnPreReimport()` is a notification delegate and cannot cancel a reimport. Blocking normal reimport requires a higher-priority `FReimportHandler` for assets owned by this importer.

Relevant installed source:

- `Engine/Plugins/Enterprise/DatasmithImporter/Source/DatasmithImporter/Public/DatasmithImportFactory.h`
- `Engine/Plugins/Enterprise/DatasmithImporter/Source/DatasmithImporter/Private/DatasmithActorImporter.cpp`
- `Engine/Plugins/Enterprise/DatasmithContent/Source/DatasmithContent/Public/DatasmithScene.h`
- `Engine/Source/Runtime/Datasmith/DatasmithCore/Public/IDatasmithSceneElements.h`
- `Engine/Source/Editor/UnrealEd/Public/EditorReimportHandler.h`

## Immutable option snapshot

`FConVerseOptimizedImportOptions` remains a value type with these fields:

| Field | Type | Rule |
|---|---|---|
| `FilePath` | `FString` | Required existing local source file in a translator-supported format. Normalize to a canonical absolute path before planning. |
| `DestinationPath` | `FString` | Required valid long package folder under `/Game`. Normalize separators and remove the trailing slash. Default `/Game/DatasmithOptimized`. |
| `InstanceType` | `EConVerseOptimizedInstanceType` | `ISM` or `HISM`. Default `ISM`. |
| `MinimumInstanceCount` | `int32` | Inclusive minimum, at least 2. Default 2. |

The service copies and normalizes the options at operation start. UI changes after that point cannot alter an active operation.

The following values are fixed policy in contract version 1 and are not UI options:

- exact Datasmith mesh-reference grouping;
- same immediate parent boundary;
- leaf mesh actors only;
- automatic rollback after cancellation, import failure, conversion failure, or verification failure;
- ordered transform verification;
- full source metadata capture for converted instances;
- ordinary Datasmith reimport and scene synchronization blocked for committed optimized output.

Changing Source file, Destination content folder, Output component, or Minimum instances invalidates the current analyzed plan and moves the panel back to **Ready**.

## Immutable optimization plan

Analyze returns an immutable `FConVerseOptimizedImportPlan`. Import and Verify must either consume the exact current plan or rebuild it from a fresh parse and show that it did so. The plan must not retain mutable `IDatasmithScene` element pointers after Analyze returns.

### Plan header fields

| Field | Required content |
|---|---|
| `ContractVersion` | Integer schema version. Starts at 1. |
| `PlanId` | Full hexadecimal MD5 of contract version, source fingerprint, normalized options, ordered groups, and ordered candidates. |
| `SourceUri` | Canonical file URI. |
| `SourceFilePath` | Canonical absolute path used for diagnostics. |
| `SourceFileHash` | MD5 of the primary source file bytes. |
| `SourceFileSize` | Byte size captured with the hash. |
| `SourceModifiedUtc` | Informational timestamp. The hash remains authoritative. |
| `SceneName` | `IDatasmithScene::GetName()`. |
| `Host`, `Vendor`, `ProductName`, `ProductVersion` | Scene exporter identity. Empty source values remain empty. |
| `ExporterVersion`, `ExporterSdkVersion` | Scene format and SDK versions. |
| `Options` | The normalized option snapshot. |
| `Groups` | Accepted groups sorted by deterministic group key. |
| `SkipCounts` | Count by stable skip-reason enum. |
| `TotalMeshActors` | All non-instanced Datasmith static-mesh actors visited. |
| `EligibleLeafActors` | Leaf mesh actors with a resolvable mesh and material binding before thresholding. |

The import path recomputes the source hash immediately before applying the plan. A changed hash produces `SourceChangedAfterAnalysis`; no package or actor is created.

### Candidate identity and ordering

Each candidate record is a pure value and contains:

- source element name and label;
- deterministic hierarchy path made from ancestor element names and sibling ordinals;
- deterministic source ordinal from the parsed scene traversal;
- immediate parent hierarchy path;
- source world transform;
- source relative transform;
- exact mesh element name/path;
- raw actor tags in original order;
- all associated Datasmith metadata key/value pairs;
- source-identity status and any identity candidates;
- skip reason when rejected.

Candidates inside a group are sorted by hierarchy path, then source ordinal. That order becomes the instance index order in the HISM element, the final ISM/HISM component, and the persisted manifest. Verification is index-to-index. It does not use nearest-transform matching.

### Group eligibility

A candidate may enter a group only when all these conditions pass:

1. The element is a Datasmith static-mesh actor and is not already a Datasmith HISM actor.
2. It is a leaf actor. Any child causes `HasChildren`.
3. Its static mesh reference is non-empty and resolves to one scene mesh element. Otherwise use `MissingMeshReference` or `UnresolvedMeshReference`.
4. Its material bindings can be expanded to an effective material for every mesh slot. Otherwise use `UnresolvedMaterialBinding`.
5. Its transform is finite. Any NaN or infinite value causes `NonFiniteTransform`.
6. Its component-local instance transform has a non-negative determinant. UE 5.8's native Datasmith HISM importer warns that negative instance scale produces unsupported inverted meshes, so mirrored instances receive `UnsupportedNegativeScale` and remain ordinary actors in contract version 1.
7. Its immediate parent boundary has a deterministic hierarchy path.

An accepted group contains at least `MinimumInstanceCount` candidates with an exact match on:

- immediate parent hierarchy path;
- exact Datasmith mesh element reference;
- ordered actor material-override pairs, including override slot IDs;
- layer string;
- visibility;
- cast shadow;
- mobility;
- `IsAComponent` status.

Source tags, labels, and per-instance metadata are not grouping fields because they commonly carry distinct Revit instance identity. They are preserved in the manifest.

Candidates in smaller groups receive `BelowMinimumInstanceCount` and remain normal Datasmith actors. Rejected actors and their hierarchy remain unchanged in the transformed scene.

### Deterministic IDs

- `GroupId` is the 32 lowercase hexadecimal characters of MD5 over `ContractVersion` plus the complete group key.
- `PlanId` uses the full 32-character MD5 value.
- `SessionId` is a new 32-character hexadecimal GUID for each mutating attempt. Analyze does not create a session ID.
- Hash input uses UTF-8, explicit field separators, invariant number formatting, and sorted keys. Pointer values and Unreal-generated object names are prohibited hash inputs.

### In-memory apply rules

- Load or parse a fresh scene for each Analyze and each Import and Verify operation. Do not mutate a scene instance that can be returned from a shared external-source cache to another caller.
- Resolve plan candidates in that fresh scene by hierarchy path, ordinal, and source element name. Any mismatch invalidates the plan before mutation.
- Create one Datasmith HISM actor element per group. Its world transform equals the parent world transform, or identity at scene root. Each instance transform is `SourceWorld.GetRelativeTransform(GroupWorld)`.
- Attach the new element to the same immediate parent using world-preserving attachment.
- Copy mesh reference, material overrides, layer, visibility, cast shadow, mobility, and component status from the frozen group record.
- Remove only the accepted leaf source elements after every instance has been added to the new element successfully.
- Keep associated source metadata in the immutable plan and manifest even though the source elements are removed from the transformed scene.

## Session tags

The importer uses these tags and does not reuse or rename `ConVerseManagedHISM` or `ConVerseManagedFamilyType`:

| Tag | Target | Purpose |
|---|---|---|
| `ConVerseOptimizedImport` | Optimized component and committed scene root | Identifies this feature. |
| `ConVerseImportSession=<SessionId>` | Every object that supports tags and was created by the attempt | Scopes verification and rollback. |
| `ConVerseOptimizedGroup=<GroupId>` | The one output component for a group | Joins runtime output to the plan and manifest. |
| `ConVerseOptimizedPlan=<PlanId>` | Optimized component and scene root | Detects stale output. |
| `ConVerseRequestedMode=ISM|HISM` | Optimized component | Records requested output. |
| `ConVerseExpectedInstances=<N>` | Optimized component | Diagnostic value only. Manifest count is authoritative. |

Tags are locators, not proof. Verification still checks the manifest and actual component state.

## Persistent source identity manifest

Create `UConVerseOptimizedImportManifest` as `UAssetUserData` and attach it to the imported `UDatasmithScene`. Every UObject reference inside it must be a `UPROPERTY` value or a soft object path so save, garbage collection, reopen, and rename behavior are explicit.

### Manifest header

The manifest stores:

- manifest schema version and plugin version;
- engine version;
- source URI, canonical source path, file hash, size, and timestamp;
- scene/exporter identity fields from the plan;
- normalized options and `PlanId`;
- committed `SessionId`;
- imported Datasmith scene asset soft path;
- destination content folder;
- world and level soft paths;
- committed Datasmith scene actor GUID and soft path, when present;
- exact list of actors created by the session;
- exact list of assets/packages created by the session;
- previous manifest/session ID for optimized reimport;
- commit state: `Active`, `Superseded`, or `RolledBack`;
- final verification summary and full report artifact path.

### Group record

Each group record stores:

- group ID and complete serialized group key;
- output component soft path and owner actor GUID/path;
- requested component type;
- exact Datasmith mesh element reference;
- imported `UStaticMesh` soft path resolved through `UDatasmithScene::StaticMeshes`;
- expected effective material element name and imported material soft path for every slot;
- copied component settings;
- parent hierarchy path and group world transform;
- expected instance count;
- contiguous range into the manifest instance array.

### Instance record

Each instance record stores:

- group ID and zero-based instance index;
- source element name, label, hierarchy path, and traversal ordinal;
- source world and relative transforms;
- raw source actor tags;
- all associated Datasmith metadata key/value pairs without dropping unknown fields;
- `SourceIdentityStatus`: `Explicit`, `Ambiguous`, or `Missing`;
- selected source identity key/value when status is `Explicit`;
- every conflicting candidate when status is `Ambiguous`.

Contract version 1 recognizes these source-ID keys case-insensitively: `UniqueId`, `ElementId`, `Element ID`, `Revit.Element.Id`, `Revit Element ID`, `IfcGUID`, `GlobalId`, and `GUID`. Empty values are ignored. One distinct non-empty value is `Explicit`; conflicting distinct values are `Ambiguous`; no recognized value is `Missing`. The Datasmith element name is always stored but is not promoted to an authoritative Revit or IFC ID.

Save/reopen acceptance requires that `GroupId + InstanceIndex` still returns the same source record after reopening the project.

## Service result contract

Replace ambiguous success inference with one authoritative status enum. UI state is derived from this enum, not from combinations of booleans.

### Required final statuses

| Status | Meaning | World/content mutation at return |
|---|---|---|
| `InvalidOptions` | File, destination, type, or minimum count failed validation, or no enabled translator accepts the file. | None |
| `SourceLoadFailed` | No translator accepted the file or parsing failed. | None |
| `SourceChangedAfterAnalysis` | Source hash changed before import. | None |
| `AnalysisSucceeded` | A deterministic plan with one or more groups was produced. | None |
| `AnalysisNoEligibleGroups` | Analysis succeeded but no group met the contract. | None |
| `AlreadyCurrent` | Active committed output has the same source hash, options, and PlanId; verification was rerun. | None beyond report/manifest verification data |
| `Verified` | Import, conversion, persistence, and every required verification passed. | One committed active session |
| `ImportedWithFailuresRolledBack` | Datasmith returned output but conversion or verification failed; cleanup succeeded. | None from the attempted session |
| `CancelledRolledBack` | Cancellation was observed and cleanup succeeded. | None from the attempted session |
| `FailedRolledBack` | A failure occurred after mutation began and cleanup succeeded. | None from the attempted session |
| `OptimizedReimportBlocked` | Existing output or asset ownership made a safe replacement impossible. | Existing active session unchanged |
| `RollbackFailed` | Cleanup could not restore the pre-operation inventory. | Unknown partial state, detailed inventory required |

`AnalysisNoEligibleGroups` is not an import success. Import and Verify stops before creating assets or actors when the plan contains zero groups.

### Required result fields

`FConVerseOptimizedImportResult` contains at least:

- final status and last completed stage;
- `PlanId` and optional `SessionId`;
- source hash;
- planned group and instance counts;
- verified group and instance counts;
- total source, eligible, below-threshold, and skipped counts;
- count by skip reason;
- one structured verification result per planned group;
- import asset soft path and manifest soft path when committed;
- `bRollbackAttempted` and `bRollbackSucceeded`;
- created and remaining object counts for failed cleanup;
- short summary for the panel;
- full report text and saved report path.

Booleans such as `bImportSucceeded` may remain as diagnostic facts during migration, but the UI must not use them as the final state.

## Verification contract

Verification is scoped to the current SessionId and uses the immutable plan plus imported scene asset mappings.

### Required group checks

Every planned group must produce exactly one component. No extra current-session group component may exist.

| Check | Pass rule |
|---|---|
| Component type | ISM requires exact `UInstancedStaticMeshComponent` and rejects HISM subclasses. HISM requires exact `UHierarchicalInstancedStaticMeshComponent`. |
| Mesh | Component mesh pointer equals the asset resolved from the planned Datasmith mesh element through `UDatasmithScene::StaticMeshes`. Name-only comparison is prohibited. |
| Material slot count | Actual effective slot count equals the planned mesh slot count. |
| Materials | Every `GetMaterial(Slot)` equals the imported material resolved from the planned effective Datasmith material element. An all-slot override is expanded before comparison. Null is valid only when the planned effective material is also null. |
| Instance count | Exact equality with the planned candidate count. |
| Instance order | Actual instance index maps to the same planned source instance index. |
| Translation | Each world-space axis differs by at most `0.1 cm`. |
| Rotation | Normalized quaternion angular distance is at most `0.1 degrees`; `q` and `-q` are equivalent. |
| Scale | Each scale axis differs by at most `0.001`; determinant sign must match. |
| Finite values | Expected and actual transforms contain no NaN or infinite values. |
| Component placement | Attachment parent, component relative transform, and owner level match the planned hierarchy boundary. |
| Shared settings | Mobility, visibility, hidden-in-game state, cast shadow, collision enabled/profile, overlap generation, cull distances, and required tags match. |

For ISM output, capture the imported HISM precursor before replacement. The ISM must match the precursor's mesh, effective materials, attachment, relative transform, shared settings, and every local instance transform before the HISM is destroyed.

### Required coverage checks

- Every planned GroupId appears exactly once.
- Session component count equals planned group count.
- Session instance total equals planned converted candidate total.
- Accepted source actor elements do not appear as separate imported geometry.
- Rejected and below-threshold actors remain present once.
- Source accounting reconciles: `TotalMeshActors = ConvertedCandidates + BelowThreshold + SkippedByReason` for mutually exclusive terminal categories.
- Existing actors, components, and assets recorded before the operation are unchanged.
- The persisted manifest contains one instance record for every output instance with matching indexes.
- The source file and sidecar hashes are unchanged.

Any required check failure prevents `Verified` and triggers rollback for the attempted session.

## Cancellation and rollback

### Cancellation checkpoints

Check cancellation during source hashing, traversal, group planning, in-memory application, post-import inventory, HISM-to-ISM conversion, manifest construction, group verification, and report generation.

`CreateFromExternalSource` is synchronous. If the user requests cancellation during a portion that cannot be interrupted safely, show **Cancel requested**, allow the engine operation to return, then roll back before returning control as complete. Do not report cancellation while imported output is still present.

> **Amendment 2 - cancellation implementation.** Implemented via `FScopedSlowTask` in `ImportAndVerify` with nine progress frames. Cancellation is tiered: before the first mutation it returns `CancelledRolledBack` with no rollback needed; after the Datasmith import it unwinds through `RollBackAttempt` and degrades to `RollbackFailed` on an unwind failure; after verification it is deliberately no longer offered, because the session is committing ownership and superseding must be atomic. This satisfies the requirement that cancellation is never reported while imported output is still present. The panel surfaces the outcome through the new `EPanelStatus::Cancelled`. `MakeDialog` is suppressed when `bAutomated` so headless runs are unaffected.

### Mutation boundary

Before calling the factory, capture:

- world actor and component inventory;
- destination-folder asset and package inventory;
- dirty flags for pre-existing packages;
- active optimized manifests for the same logical source;
- the source and sidecar hashes.

The first release must not overwrite an unrelated existing asset. A destination collision without an owned matching manifest returns `OptimizedReimportBlocked` before mutation.

> **Amendment 3 - ownership blocking is manifest-driven, not path-driven.** Imported assets are written to a per-session attempt folder (`DestinationPath/AssetName_ConVerse_<sessionId>`), so the conventional-path collision check cannot be relied on to detect a prior optimized session; it only catches genuinely unrelated assets sitting at the conventional path. Blocking an unreadable prior session is therefore driven by `FindActiveManifest`, which reports a matching-but-future-schema manifest explicitly so `ImportAndVerify` can return `OptimizedReimportBlocked` before any mutation. Covered by `DatasmithHISM.OptimizedImport.FutureSchemaManifestIsRefused`.

> **Amendment 4 - tessellation options.** The panel exposes chord tolerance, normal tolerance, max edge length, and stitching technique in a collapsed "Tessellation (CAD, Revit, IFC)" section; the commandlet exposes the same four as `-ChordTolerance`, `-NormalTolerance`, `-MaxEdgeLength`, and `-Stitching`. These are forwarded to the resolved translator via `GetSceneImportOptions` / `SetSceneImportOptions` **before** `TryLoad`, following the sequence Epic uses in `DatasmithBlueprintLibrary.cpp`. Three consequences are contractual:
>
> 1. **Tessellation is part of plan identity.** It changes generated geometry without changing the source file, so `ComputePlanId` includes all four values. Without this, a tessellation-only change would hash identically, match the active manifest, take the `AlreadyCurrent` branch, and silently discard the user's settings while reporting success. Covered by `DatasmithHISM.OptimizedImport.TessellationAffectsPlanIdentity`, which was mutation-verified to fail when the fields are removed from the hash.
> 2. **Clamping lives in the service, not the widget.** `NormalizeAndValidateOptions` rejects non-finite values and clamps chord to >= 0.005, normal to 5-90, and max edge length to 0 (unconstrained) or >= 1.0, because the commandlet and automation bypass the panel entirely. The panel clamps identically so a displayed value never differs from the value that would run.
> 3. **Applicability is reported, never inferred.** Formats that are already tessellated, notably `.udatasmith`, expose no tessellation options. The panel states this as unknown until a run with `bSourceLoaded` reports `bTessellationApplied`, rather than guessing from the file extension.
>
> Stitching may merge or split objects before tessellation, which can change how instances group. This is expected and is reflected in the plan, since the plan is built from the post-load scene.

> **Amendment 5 - sidecar hashing is warn-first.** A Datasmith source is a primary file plus a `<BaseName>_Assets` sidecar folder. `HashDirectory` folds that folder into one aggregate fingerprint, which is recorded on the manifest as `SidecarHash`, `SidecarTotalSize`, and `SidecarFileCount`. Four points are contractual:
>
> 1. **The sidecar hash is deliberately NOT part of `PlanId`.** This is the opposite of the tessellation rule in Amendment 4, and intentionally so. Tessellation changes what the importer *generates*, so it belongs in plan identity. A sidecar change is something to *report*: folding it into `PlanId` would bypass the `AlreadyCurrent` branch and trigger an automatic destructive reimport. Superseding destroys committed output, so rebuilding is the user's decision.
> 2. **A sidecar-only change reports but does not mutate.** The status stays `AlreadyCurrent`, re-verification still runs, the world is untouched, and no session is created. The change is surfaced as `bSidecarChanged`, appended to the summary, logged as a warning, and shown in the panel under the warning-toned status rather than the green verified state. Covered by `DatasmithHISM.OptimizedImport.SidecarChangeWarnsWithoutReimport`, mutation-verified.
> 3. **An empty recorded hash means "not recorded", never "changed".** Manifests written before this field existed, and genuinely self-contained sources, must not produce a spurious warning on their first reimport.
> 4. **Aggregation must be order-stable and absence must be distinguishable from failure.** Relative paths are sorted before folding, because directory enumeration order is not guaranteed stable and an unsorted fold would warn on every run. An absent sidecar folder is legitimate and is not an error; a sidecar that exists but cannot be read is a failure and names the offending file, so a permissions error cannot masquerade as a self-contained source.

### Rollback algorithm

Rollback is explicit and does not rely on editor Undo:

1. Stop further conversion and verification.
2. Destroy components and actors created by the attempted SessionId, children before parents.
3. Remove newly created asset objects and packages under the attempt's versioned destination. Never delete a path present in the pre-operation inventory.
4. Restore dirty flags where the operation dirtied a previously clean package without changing its contents.
5. Remove an uncommitted manifest and unregister its asset-registry entries.
6. Force an asset-registry and world inventory check.
7. Confirm that no attempted-session tags, actor GUIDs, assets, packages, or manifest rows remain.
8. Confirm that the previously active optimized session and unrelated world content still match the pre-operation inventory.

If any check fails, return `RollbackFailed` and list every remaining object path. Never collapse this state into `Cancelled` or `Failed`.

Analyze performs no package creation, world mutation, asset-registry notification, or transaction.

## Safe first-release reimport policy

Normal Datasmith reimport and normal Datasmith Scene Actor synchronization are unsafe for optimized output because the persisted Datasmith scene contains native HISM elements while ISM mode performs a post-import replacement. The first release uses this policy:

1. Attach the manifest to the `UDatasmithScene` and mark every owned imported asset path in the manifest.
2. Register a higher-priority `FReimportHandler` that recognizes the manifest-bearing scene asset and its owned assets. A normal Content Browser reimport returns `Cancelled` with a message directing the user to **Optimized Datasmith Import**. `OnPreReimport` is not used as a blocker.
3. After commit, prevent the committed `ADatasmithSceneActor` from using the ordinary Synchronize action. The implementation may clear its normal scene link after recording it in the manifest or provide an equivalent tested hard guard. A warning alone is insufficient.
4. The panel's primary button changes from **Import and Verify** to **Optimized Reimport and Verify** when it finds one active manifest for the same canonical source and destination. No second button is required.
5. Reimport parses the source again, creates a new PlanId, and imports the attempt into a unique versioned package path. It does not overwrite the active version.
6. Verify the new session completely while the old session remains the recovery copy.
7. After verification, remove the old session's world actors. If old-session removal fails, roll back the new session and keep the old session active.
8. Mark the new manifest `Active` and the old manifest `Superseded`. Retain superseded asset packages in the first release so external references are never silently deleted. The report lists them for optional later cleanup.
9. A reimport with the same source hash, normalized options, and PlanId runs verification only and returns `AlreadyCurrent`.

Final world state must contain one active session for the logical source. Two consecutive optimized reimports may leave explicitly named superseded asset versions in Content, but they may not leave duplicate world geometry.

## UI behavior mapped to the contract

| Panel state | Service condition | Controls and report |
|---|---|---|
| Idle | No valid source path. | Analyze and import disabled. |
| Ready | Local validation passes and no current plan matches all four inputs. | Analyze enabled. Import may plan internally, but must show the same validation errors as Analyze. |
| Analyzed | `AnalysisSucceeded` or `AnalysisNoEligibleGroups`. | Show plan ID and counts. Disable Import and Verify for zero groups. |
| Working | Service operation active. | Disable all inputs and both start actions. Show stage, progress, and Cancel. |
| Cancel requested | Cancel requested during a non-interruptible stage. | Keep controls disabled until rollback result. |
| Verified | `Verified` or `AlreadyCurrent` with verification pass. | Green summary plus structured counts and report path. |
| Imported with failures | `ImportedWithFailuresRolledBack`. | Red group rows, state that cleanup succeeded, and preserve report. |
| Cancelled / failed | `CancelledRolledBack`, `FailedRolledBack`, or pre-mutation failure. | Show failing stage and cleanup result. |
| Rollback failed | `RollbackFailed`. | Persistent critical error with remaining object paths. |

The visible group list is capped at 50 rows. Counts always cover the full plan. The complete structured report is written to the Output Log and to `Saved/DatasmithHISM/ImportReports/<SessionId>.json`; the manifest stores the report path and final summary.

## Synthetic fixture matrix

The generated fixture and automation must use real Datasmith scene elements and imported Unreal assets. A unit test that only repeats the planner's string-key logic is insufficient.

| ID | Fixture | Action | Required result |
|---|---|---|---|
| S01 | Two leaf actors, same parent, mesh, materials, and settings | Analyze twice | One deterministic group, two candidates, identical PlanId, no created packages or actors. |
| S02 | One eligible actor with minimum 2 | Analyze, then attempt import | `AnalysisNoEligibleGroups`; no import output. |
| S03 | Same mesh under two different immediate parents | Analyze | Separate group keys; neither crosses the parent boundary. |
| S04 | Same mesh and parent with one material override variant | Analyze and import | Separate material groups; effective slots on both outputs match source variants. |
| S05 | Same mesh with visibility, cast-shadow, mobility, layer, or component-status differences | Analyze | One split per differing shared setting. |
| S06 | Mesh actor with a child | Analyze and import other valid group | Parent actor has `HasChildren`, remains unchanged, and valid group verifies. |
| S07 | Empty and unresolved mesh references | Analyze | Stable skip reasons, no crash, counts reconcile. |
| S08 | Nested rotated parent, translated child, non-uniform parent scale, shifted pivot | Import both modes | Ordered world transforms pass the frozen tolerances. |
| S09 | Mirrored instance with negative determinant beside a normal repeated set | Analyze and import both modes | Mirrored source receives `UnsupportedNegativeScale` and remains once as a normal actor; the supported set verifies. |
| S10 | Multi-slot mesh with per-slot override and negative all-slot override ID | Import both modes | Every effective slot resolves and matches through the scene asset material map. |
| S11 | Valid two-group scene, ISM selected | Import and Verify | Exact ISM class, zero session HISM components, all groups verified. |
| S12 | Same scene, HISM selected | Import and Verify | Exact HISM class, all groups verified. |
| S13 | Three instances with explicit, ambiguous, and missing source IDs | Import, save, reopen, query | Three aligned manifest rows and all three identity states survive reopen. |
| S14 | Change source bytes after Analyze | Import and Verify | `SourceChangedAfterAnalysis`; no mutation. |
| S15 | Pre-existing unrelated actors and assets in world/destination parent | Import and Verify | Unrelated inventory is byte/path stable and no object is retagged. |
| S16 | Force factory failure after at least one asset/actor is created | Import and Verify | `FailedRolledBack`; no attempted-session output remains. |
| S17 | Force ISM replacement failure on the second group | Import and Verify | `ImportedWithFailuresRolledBack`; no partial first group remains. |
| S18 | Cancel during planning, apply, factory import, conversion, and verification in separate runs | Cancel | Every run ends before mutation or as `CancelledRolledBack`; inventories match baseline. |
| S19 | Tamper mesh, one material slot, count, type, and transform in separate verification injections | Verify | Each injection fails its named check and causes rollback. |
| S20 | Existing unrelated asset at the exact initial output path | Import and Verify | `OptimizedReimportBlocked`; unrelated asset unchanged. |
| S21 | Ordinary reimport of a manifest-owned scene and mesh asset | Content Browser Reimport | Custom handler cancels both and directs the user to the panel. |
| S22 | Committed scene actor ordinary Synchronize attempt | Details panel action | Synchronization is unavailable or hard-blocked; output does not change. |
| S23 | Optimized reimport with unchanged source | Panel reimport | `AlreadyCurrent`; one active world session. |
| S24 | Optimized reimport after add, delete, move, material change, and parent change | Panel reimport twice | New plan verifies, old world session is removed, one active session remains, superseded assets are listed. |
| S25 | Forced failure while removing the old session after new verification | Optimized reimport | New session rolls back, old session stays active and verified. |
| S26 | Save and reopen ISM and HISM levels | Reopen and verify | Type, mesh, materials, counts, transforms, tags, and manifest queries still pass. |

## Real Revit acceptance matrix

Use a dedicated Revit 3D view exported to `.udatasmith` with its `_Assets` folder beside it. Record Revit version, exporter version, UE version, view name, source hash, export warnings, and import options. Keep the raw baseline import in a disposable comparison level.

| ID | Source content | Evidence to record | Pass criterion |
|---|---|---|---|
| R01 | At least two repeated family types with identical parameters | Raw actor count, distinct Datasmith mesh references, planned groups, converted instances | Every exact-reference repeated set above threshold groups once within its parent boundary; totals reconcile. |
| R02 | Same family with instance or type material variants | Revit material intent, Datasmith override pairs, final asset paths per slot | Variants are split where needed and every effective material slot matches. |
| R03 | Hosted and nested families across two storeys | Raw hierarchy paths, parent boundaries, final attachments | No group crosses the frozen immediate-parent boundary; accepted and rejected hierarchy is accounted for. |
| R04 | Mirrored and rotated family instances | Source and output world transforms, determinant signs, and skip reasons | Supported rotations pass all transform checks. Negative-determinant instances remain normal actors with `UnsupportedNegativeScale`; they are never silently inverted or deleted. |
| R05 | Linked-model element | Link identity, hierarchy path, metadata, transform | Element is either safely grouped within its parent or skipped with a stable reason; identity is queryable. |
| R06 | Metadata-rich elements plus one element with filtered/missing metadata | Raw metadata/tags and manifest identity state | All raw key/value pairs for converted instances persist; missing data is `Missing`, never invented. |
| R07 | Section-box cut element and a normal repeated element | Mesh references and skip/group outcome | Cut geometry is never grouped merely by label; only exact mesh references group. |
| R08 | Project-base-point and survey-coordinate cases used by the project | Coordinate setup and maximum position magnitude | Transforms pass fixed tolerances or the case is rejected before deletion with explicit evidence. |
| R09 | ISM output | Component classes and verification report | Every planned group is exact ISM, no session HISM remains, and all required checks pass. |
| R10 | HISM output from the same export | Component classes and verification report | Every planned group is exact HISM and all required checks pass. |
| R11 | Save, close editor, reopen level | Manifest queries and full re-verification | Type, mesh, materials, counts, transforms, hierarchy, and source lookup still pass. |
| R12 | Re-export after adding, deleting, moving, reparenting, and changing a material | Old/new PlanIds, active/superseded sessions, actor totals | Optimized reimport leaves one active world session, the new result verifies, and unrelated content is unchanged. |
| R13 | Ordinary Reimport and Synchronize attempts on committed output | UI message and before/after inventory | Both paths are blocked and create no raw or HISM duplicates. |
| R14 | User cancellation during a representative import | Before/after world and asset inventory | Attempt ends as `CancelledRolledBack`; baseline inventory is restored. |
| R15 | Representative production-size view | Parse, import, conversion, verification time; peak memory; actor/component/instance counts | Operation completes without crash or rollback failure. Measurements are recorded without inventing a performance target. |

Real-data acceptance fails if the source file is present without its referenced sidecar files, if the exporter/version evidence is missing, or if only a synthetic scene was tested.

Because the importer now accepts every translator-supported format, real-data acceptance should cover at least one sidecar-dependent source such as Revit, IFC, or a CAD format, in addition to a `.udatasmith` export. A format is not considered validated on the evidence of `.udatasmith` runs alone.

## Phase gates for later agents

### Phase 2 gate

- Service types implement this status and immutable-plan contract.
- Analyze is read-only and deterministic.
- Both output modes pass S01 through S12.
- All mutation failures roll back by session.

### Phase 3 gate

- The panel implements the state mapping above.
- All four input controls invalidate stale analysis correctly.
- It allows one operation at a time and exposes pending cancellation honestly.
- The primary button selects import, optimized reimport, or verification-only behavior from the active manifest state.

### Phase 4 gate

- Manifest, exact material/mesh verification, save/reopen, reimport blocking, optimized reimport, and rollback pass S13 through S26.
- A source identity query works by GroupId and InstanceIndex after reopen.

### Phase 5 gate

- UBT, focused automation, manual panel smoke tests, and R01 through R15 are recorded.
- Release-ready requires both ISM and HISM real-export passes.
- Missing real Revit data remains an explicit blocked gate and cannot be replaced by synthetic success.
