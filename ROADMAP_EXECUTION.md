# Consolidated roadmap execution

Status reconciled 2026-09-29: **UE 5.8.3 editor build and 36/36 tests passed; release gates remain incomplete.** [Live UI evidence](Docs/Validation/2026-09-27-live-ui.md) covers native interaction results, two fixed defects and two new regressions; the fixes still need a live re-check. [Preset-state and cancellation evidence](Docs/Validation/2026-09-27-preset-cancellation.md) covers the previous changes. [Earlier Phase 2 evidence](Docs/Validation/2026-09-26-phase2.md) covers persistence/recovery safety; [Windows packaged smoke evidence](Docs/Validation/2026-09-29-packaging.md) confirms the runtime components build and load successfully against the 36-test baseline. A live re-check of the fixes and the remaining UI rows are the next independent assignment.

Separate 2026-09-28 verification snapshot (not validation of the combined tree): **UE 5.8.3 editor build and 38/38 tests passed** for the verification fixes. [Evidence](Docs/Validation/2026-09-28-verification.md) covers Amendments 9-11 and clarifications to Amendments 2 and 6, with five regression tests that each failed before their fix. The combined remote-plus-stashed tree remains unbuilt and unverified; the live re-check is pending.

Scope and rationale: [accepted plan](PLAN.md), [ADRs](Docs/ADR/README.md), and [validation matrix](Docs/VALIDATION.md).

2026-09-29 Batch A implementation checkpoint: failed-verification acceptance now rolls the new
attempt back when the previous session cannot be removed, and supersede path fallback requires the
previous session's exact actor tag. Two real-entry automation regressions cover predecessor-removal
failure and unrelated actor path reuse. These changes have been **built and verified**; they pass the 36-test suite.

2026-09-29 Batch C implementation checkpoint: recursive texture-library discovery now has its own
observable cancellation phase, deterministic cooperative traversal, streamed hashes, and per-folder
safety bounds of 250,000 files and 100,000 normalized directories. Import-time approved-material fingerprinting uses the same
streaming progress object after mutation; cancellation routes through attempt rollback. Focused
regressions exercise cancellation during a synthetic recursive search and during post-import
texture hashing, including inventory restoration and removal of partial result data. This code is
now **built and verified** successfully, passing the complete 36-test automation suite.

2026-09-29 Batch D implementation checkpoint: a selected texture-library resolution now contributes
its streamed content hash and byte size to `PlanId`, in addition to its canonical resolution
evidence. The configured folder list remains excluded, so an unchanged winner and no-match folder
changes preserve identity, while an in-place byte change or a different deterministic winner changes
it. The same-path byte-change regression crosses the real committed `ImportAndVerify` path and proves
the active result is replaced rather than reported `AlreadyCurrent`; Windows path-case aliases,
unchanged/equivalent resolution, no-match folders, deterministic precedence, cleanup, and read-only
source/library behavior are also covered.
This code has now been **built and executed** successfully, establishing the new 36/36 passing baseline.

The [2026-09-27 live UI preflight](Docs/Validation/2026-09-27-live-ui-preflight.md) confirmed that all 55 source hashes still match and Windows capture/window activation are available. Shared-desktop foreground availability prevented plugin interaction; the editor then closed normally. No live acceptance gate changed, and no new source build or automation run was needed.

Read [workflow and commandlet usage](Docs/IMPORT_WORKFLOW.md), [current source hashes/results](Docs/Validation/2026-09-27-preset-cancellation-evidence.json), [earlier geometry/light/package evidence](Docs/Validation/2026-09-26.md), and the [documentation closeout](Docs/Validation/2026-09-27-closeout.md).

| Phase | Implemented / demonstrated | Remaining gate |
|---|---|---|
| 1: geometry | Source-derived joist and light/IES fixtures; version provenance; direct payload inspection; six independent ordinary/ISM/HISM and Nanite comparisons | Webbing is absent from the supplied payload. Obtain corrected geometry and complete sidecars; verify full-scene alignment, source dimensions, stock-toolbar and rendered comparisons. |
| 2: integrity and recovery | Preset/file-picker/destination invalidation; cooperative analysis checkpoints and chunked texture fingerprints; 17 pre-mutation cancellation cases; source accounting and tracked edits; rebuild; Analyze progress/cancellation; dependency preflight; save/reopen; real read-only map/asset failures; bounded simulated write-capacity failure; save-with-drift; named-map copy refusal; actual pre-commit interruption/restart with path diagnostics and unchanged files; legacy fixes | Live progress/cancellation and inspection; Content Browser rename/move; other interruption stages and actual full-volume behavior; broader scenes. Named-copy ownership migration is not implemented. |
| 3: Nanite and lighting | Import-wide policy including zero groups; exact mesh exceptions; effective blend-mode exclusions; compilation completion/data checks; light/IES source verification; advisory threshold 100; MegaLights navigation; named presets and session recovery | Calibrated Revit photometrics, additional light types, broader material compatibility, and a real mesh-build failure. Unitless sources remain explicitly unresolved. |
| 4: materials | Linked DataTable schemas, CSV workflow, review/approve/revoke UI, exact appearance fingerprints, missing-target rejection, pre-Nanite replacement, original assignments, custom-appearance invalidation; 245 observed catalog entries | Curated Autodesk stock identities/versions, approved replacement coverage, visual matching and physical texture-scale validation. Observed entries do not establish stock coverage. |
| 5: inspection and packaging | Search, Revit/source identities, instance/light focus, open assets, group/material/light rebuild previews, runtime lookup module, editor-only orchestration/manifest stripping, Windows cook and runtime checks, retained-version disk sizes, duration/mesh-processing/compilation-wait/process-peak reporting | Full rendered/UI acceptance; normal texture/UV coverage; broader collision/navigation/LOD cases; representative camera-path CPU/GPU baselines. |
| 6: later work | Explicitly retained in the backlog | Source relinking, reference-aware deletion, cross-asset deduplication, richer visualization/extraction, optional Unreal material pack and its distribution review. |

## Verified findings

- 2026-09-28 live ARCH imports: the 10 failing lights all reference IES file `ARCH_Assets/generic`, which is 0 bytes; preflight treated it as present. Empty dependency files are now missing (Amendment 9).
- The same imports' `actual=5506 accounted=5505` mismatch was the editor-only `UCameraProxyMeshComponent` that `UCameraComponent::OnRegister` adds to the imported `ACineCameraActor` outside commandlets; the mesh accounting itself was exact (10,389 = 904 + 221 + 4,380 + 4,884). Visualization components without Datasmith identity are now excluded (Amendment 11).
- Nanite "converted meshes only" enabled nothing in HISM mode because converted meshes were collected by exact ISM class. It now covers HISM groups, with a PlanId salt for that combination only (Amendment 10).
- The 16K6 joist used by Revit elements 610662 and 610663 has 88 vertices / 160 triangles in the original `.udsmesh`. Web diagonals are already absent. All six imported LOD0 geometry exports match the payload exactly. The importer cannot reconstruct missing source geometry reliably.
- Structural preflight reports eight missing texture references (seven filenames); HVAC reports one missing bump texture. Both full-source imports stop before creating output.
- The HVAC file contains 1,033 point lights, all explicitly Unitless. The isolated light retains intensity 100 and its IES configuration. Original Revit reference values are still needed for physical calibration.
- Save-as testing found an expression-GUID-only material difference on reopen. Comparison now ignores those generated IDs while still detecting actual parameter edits; this has a regression test.
- Both ownership ambiguity and stock reimport are tested through the real import/reimport dispatch paths, rather than by testing only a guard function.
- Already-saved map Save As was found to create an unverifiable copy while reporting success. The commandlet now refuses before import/copy; native-copy explicit save explains failed ownership proof. The original map re-verifies in a fresh process.
- Actual interruption at a verified-but-uncommitted checkpoint produced restart diagnostics for 7 observed paths and left 4 recorded files unchanged. This does not establish every crash window or automatic recovery.

## Remaining implementation and acceptance scope

The core paths are present, but acceptance coverage is not a release guarantee. Rebuild previews report group changes, planned ordinary output, per-material/per-light before-and-after decisions, and tracked edits. Older manifests without source inventories explicitly report that their previews are unverified. Named progress stages are shared with the commandlet log. Timing distinguishes overall import, mesh policy processing including builds, and compilation wait; process peak memory is a process-lifetime measurement. Representative CPU/GPU profiling remains open. Material preview uses the native source/target asset editors; automatic visual similarity matching is not implemented.

Keep ownership, commit, replacement, and rollback decisions centralized in the import service. Processing, material review, and persistence are separate helpers. Sources stay read-only; warning-only sidecars remain outside PlanId; unknown future manifests and quarantined failed sessions remain blocked. Existing uncommitted work has been preserved.

The documentation consolidation preserves earlier plans in [history](Docs/History/README.md). It does not add build or release evidence; current active docs supersede historical status claims.
