# ADR 0009: Preserve a separate contract for legacy selection conversion

Date: 2026-09-26. Status: **Accepted**. Scope: the current consolidated roadmap; validation status is separate.

## Context

Legacy selection tools process already-placed actors and may delete actors or duplicate mesh assets. Their hierarchy/geometry grouping and editor transactions do not establish tracked session ownership.

## Decision

Maintain the legacy API and tags without representing those operations as verified, rollback-safe tracked import. Keep library-layer transactions and headless Dataprep operation. Confirm dedupe before reference changes, audit external/uncertain references, and stage Explode replacements before source removal.

Managed grouping includes effective materials, component descriptor settings and a mesh-origin discriminator. Copy component world transforms and effective properties. Preserve behavior-bearing actors, failed instance sources and below-threshold groups. A bare native scene-component root can remain transform scaffolding.

## Alternatives and consequences

Routing tracked sessions through legacy conversion would bypass their manifest lifecycle. Rebuilding legacy tools around a new manifest is a separate project. Geometry signatures are not a proof of every separate asset's LOD/collision/rendering equivalence; broad grouping still needs real-data acceptance.

## Evidence and follow-up

Four legacy safety/placement/material tests pass. Dedupe/Explode interactive recovery, BIM hierarchy/storey, partial selection, Dataprep breadth and auto-detection remain separate checks. See [legacy guide](../LEGACY_TOOLS.md) and [conversion source](../../Source/DatasmithHISM/Private/ConVerseHISMUtils.cpp).
