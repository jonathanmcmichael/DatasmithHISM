# ADR 0001: Tracked import owns lifecycle decisions

Date: 2026-09-26. Status: **Accepted**. Scope: the current consolidated roadmap; validation status is separate.

## Context

Collapsing source mesh actors before finalization changes what native Datasmith imports. Stock reimport rereads the unmodified source and cannot replay this plugin's transformation. A post-import selection conversion has different recovery needs.

## Decision

Keep the optimized importer as the primary workflow. Load a fresh translated scene, build an immutable plan, rewrite exact eligible source groups, import into a unique attempt folder, verify the result, and commit through the centralized service. Ownership, replacement and rollback stay in that service even when processing helpers grow.

Retain the previous active session until the new attempt reaches the safe replacement boundary. Block stock reimport using the higher-priority handler and active asset markers. Ambiguous ownership and unknown future schemas fail closed. Retain superseded asset packages rather than guessing that external references are absent.

## Alternatives and consequences

Stock reimport would bypass the transformation. Legacy conversion would pay for raw actor creation and lacks session recovery. The accepted path needs manifests, explicit rebuild, and stronger verification; it does not make import available in packaged applications.

## Evidence and follow-up

Real-dispatch stock-reimport refusal, optimizer-aware replacement, rollback at multiple depths, rollback obstruction, and duplicate active ownership are covered by automation. The [Phase 2 follow-up](../Validation/2026-09-26-phase2.md) also demonstrates actual interruption/restart at the verified-but-uncommitted checkpoint, with diagnostic paths and unchanged saved files. Full-model replacement, other crash windows and live recovery feedback remain open. See [contract](../../IMPORT_PANEL_VALIDATION.md), [service](../../Source/DatasmithHISM/Private/ConVerseDatasmithImportService.cpp), and [validation](../VALIDATION.md).
