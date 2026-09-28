# ADR 0007: Cook source identity without editor import services

Date: 2026-09-26. Status: **Accepted**. Scope: the current consolidated roadmap; validation status is separate.

## Context

The accepted release must support packaged Windows applications, while Datasmith orchestration and recovery depend on editor-only facilities.

## Decision

Keep import, material review, manifests, reimport guards, rollback and persistence in `DatasmithHISM` (Editor). Store cookable source records and lookup in `DatasmithHISMRuntime` with Core/CoreUObject/Engine dependencies only. Mark editor manifest/ownership data editor-only for cook stripping.

Record component identity and instance index alongside source element/document/metadata. Use INDEX_NONE for ordinary meshes and lights. Provide Blueprint lookup and a Development-only opt-in runtime validation command. Packaged applications consume prepared content; runtime source-file import is not part of this release.

## Alternatives and consequences

Keeping identity only in editor user data loses runtime traceability. Shipping editor dependencies is not an appropriate packaging boundary. A small runtime component preserves useful lookup but requires application code to maintain mappings if it changes the committed instance layout.

## Evidence and follow-up

Windows Development cook/archive and NullRHI/DX12 runtime smoke checks pass on the fixture map. Texture/UV breadth, rendered acceptance, interaction-level lookup, broader collision/navigation, and representative performance remain open. See [runtime guide](../RUNTIME_AND_PACKAGING.md) and [runtime source](../../Source/DatasmithHISMRuntime/Public/ConVerseSourceMetadata.h).

The later [Phase 2 lookup check](../Validation/2026-09-26-phase2.md) ran in an editor process and does not supersede the package evidence. Runtime source remained unchanged after the recorded cook; the editor-only persistence fixes were not recooked.
