# ADR 0002: Output identity and explicit manifest evolution

Date: 2026-09-26. Status: **Accepted**. Scope: the current consolidated roadmap; validation status is separate.

## Context

AlreadyCurrent is valid only when the settings that generate output have not changed. Sidecars also change, but automatically replacing output on a sidecar warning would bypass the user's rebuild decision.

## Decision

Plan identity includes the primary source hash, contract version, normalized instancing/tessellation settings, Nanite choices, exact source-mesh exceptions, active approved mapping identity/revisions, and each selected texture-library resolution with its streamed content hash and byte size. Canonical paths are case-folded on Windows. The configured texture search-folder list is excluded: it changes identity only when it selects a different resolution. This makes same-path library-file edits invalidate `AlreadyCurrent` while unchanged/equivalent resolution, Windows case aliases, and no-match folder changes remain stable. Sidecar content is fingerprinted separately and warns without changing PlanId. Light thresholds, observed renderer settings, and catalog-only changes are advisory.

Use source element names for joins and preserve available authoring-tool identity separately from display labels. New records use manifest schema 2, tracked-state version 1, and source-inventory version 1. Older records remain unverified for missing fields; future schemas block replacement. Opening a project never silently migrates/rebuilds its sessions.

## Alternatives and consequences

Timestamp-only checks can miss changed content. Including every advisory in PlanId would cause unnecessary replacement. Inferring missing baseline fields would claim checks never performed. Explicit rebuild and schema-aware diagnostics add workflow steps but preserve user control.

## Evidence and follow-up

Tessellation, Nanite/exception/advisory identity, resolved-texture content identity and deterministic precedence, sidecar warnings, future-schema refusal, changed appearance and older preview inventory have behavioral coverage. The resolved-texture additions are unbuilt and unexecuted as of 2026-09-29. Source relinking remains deferred. See [manifest](../../Source/DatasmithHISM/Public/ConVerseOptimizedImportManifest.h) and [contract amendments](../../IMPORT_PANEL_VALIDATION.md).

The [Phase 2 tests](../Validation/2026-09-26-phase2.md) prove that a native named-map copy changes the owner actor GUID and cannot acquire ownership from its copied tags. Commandlet refusal and explicit-save rebinding checks retain the original manifest/session. Content Browser rename/move remains a separate untested path.
