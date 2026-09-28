# ADR 0003: Preserve geometry and apply Nanite independently

Date: 2026-09-26. Status: **Accepted**. Scope: the current consolidated roadmap; validation status is separate.

## Context

Nanite tied only to ISM conversion omits unique and retained meshes. More aggressive grouping also risks hiding thin geometry or placement errors unless source fidelity is established first.

## Decision

Group tracked imports by exact source mesh identity and compatible source settings under the same parent. Keep unsupported/mirrored/parent-bearing cases ordinary when they cannot be safely grouped. Preserve transform and attachment roots. Do not infer equivalence from family labels or bounds.

Default new imports to all supported owned meshes. Also offer converted-ISM-only enablement and preserve-imported settings, with exact keep-ordinary and disable-Nanite mesh exceptions. Process each unique owned asset, inspect effective defaults/overrides, finish compilation, and verify built data. Approved material mappings run before compatibility checks.

## Alternatives and consequences

Conversion-only enablement misses zero-group imports. Blind bulk enablement ignores material compatibility. Asset-level Nanite settings apply to all uses of a shared asset, so a disable exception affects those uses and must be visible in reporting. Broad cross-asset deduplication stays outside tracked import scope.

## Evidence and follow-up

Zero-group/policy/exception automation and six independent joist import combinations pass. The negative joist payload itself lacks diagonals; no import policy can recover absent geometry. Corrected source geometry, rendered thin-member fidelity, broader compatibility and real compilation failure remain open. See [fixture evidence](../Validation/2026-09-26.md) and [processing helper](../../Source/DatasmithHISM/Private/ConVerseImportProcessing.cpp).
