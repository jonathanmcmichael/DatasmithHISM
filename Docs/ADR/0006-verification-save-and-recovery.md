# ADR 0006: Separate verification, persistence and recovery

Date: 2026-09-26. Status: **Accepted**. Scope: the current consolidated roadmap; validation status is separate.

## Context

An imported scene can pass checks in memory and still fail to save. Manual changes may also be intentional, and an interrupted attempt may leave uncertain ownership.

## Decision

Record a committed tracked baseline and compare it before replacement. Require Replace with source or Cancel when edits are detected; headless execution requires explicit authorization. Rebuild bypasses AlreadyCurrent only, not conflict or ownership checks.

Report Verified, unsaved; Saved; and degraded/unverified states separately. Explicit save includes owned assets, manifest and owning level, reports incomplete saves, and never accepts a new baseline just because edited output was saved. Guard initial save-as path rebinding by actor GUID/component path and session ownership. When saving the manifest with the newly named level lets UE redirect its soft paths first, map tracked-state references to the former `/Temp/` world onto the owning world only under the same GUID and session-tag proof (clarified 2026-09-27). Ignore generated material parameter expression GUIDs in canonical comparison while retaining actual parameter values.

Write attempt checkpoints and report unfinished/uncertain attempts without automatic deletion. Explicit acceptance of a failed verification remains whole-session, degraded and quarantined against optimized replacement.

UE 5.8 Save As of an already-saved level creates a duplicate with changed actor identities. Refuse that commandlet operation before import/copying; a loaded native copy cannot acquire ownership merely from copied session tags. Refusal identifies the original owning map. Recovery diagnostics display recorded paths as unproven observations, never as deletion authorization.

## Alternatives and consequences

Treating import as save success hides persistence failure. Automatically accepting manual edits during save weakens source verification. Automatic cleanup based only on paths can destroy unrelated objects. Journals aid diagnosis but are not a crash-resumable transactional import system.

## Evidence and follow-up

The [Phase 2 follow-up](../Validation/2026-09-26-phase2.md) adds passing named-copy refusal, actual read-only-asset failure, bounded simulated write-capacity failure, save-with-drift and actual interruption/restart evidence. All 27 automation tests pass. Live UI, Content Browser rename/move, other interruption windows and actual full-volume behavior remain open. See [persistence helper](../../Source/DatasmithHISM/Private/ConVerseImportPersistence.cpp) and [validation](../VALIDATION.md).

The [2026-09-27 preset/cancellation follow-up](../Validation/2026-09-27-preset-cancellation.md) adds shared panel input invalidation and closes stale material-review windows. Restoring settings remains passive, and same-source/destination presets retain the imported association. The current full suite passes 30 tests; this does not close native UI acceptance.
