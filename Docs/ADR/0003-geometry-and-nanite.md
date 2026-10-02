# ADR 0003: Preserve geometry and apply Nanite independently

Date: 2026-09-26. Status: **Accepted**. Scope: the current consolidated roadmap; validation status is separate.

## Context

Nanite tied only to ISM conversion omits unique and retained meshes. More aggressive grouping also risks hiding thin geometry or placement errors unless source fidelity is established first.

## Decision

Group tracked imports by exact source mesh identity and compatible source settings under the same parent. Keep unsupported/mirrored/parent-bearing cases ordinary when they cannot be safely grouped. Preserve transform and attachment roots. Do not infer equivalence from family labels or bounds.

Since 2026-10-02 (Amendment 16) imports default to preserve-imported and Nanite is applied by a separate step to all supported owned meshes or converted ISM/HISM groups only, still with exact keep-ordinary and disable-Nanite mesh exceptions. Process each unique owned asset, inspect effective defaults/overrides, finish compilation, and verify built data. Approved material mappings run before compatibility checks.

## Alternatives and consequences

Conversion-only enablement misses zero-group imports. Blind bulk enablement ignores material compatibility. Asset-level Nanite settings apply to all uses of a shared asset, so a disable exception affects those uses and must be visible in reporting. Broad cross-asset deduplication stays outside tracked import scope.

Applying Nanite to all meshes can exhaust UE 5.8.3's fatal Nanite root-page pool on sources with tens of thousands of unique meshes (observed 2026-10-01). 2026-10-01 added a mesh budget ([Amendment 15](../../IMPORT_PANEL_VALIDATION.md)): at most 16,384 meshes get Nanite by default, chosen by how often each is placed, and the budget enters plan identity only when it binds. The projected-count advisory ([Amendment 14](../../IMPORT_PANEL_VALIDATION.md)) remains and judges the capped count. The budget bounds mesh count only, was exercised on a generated two-mesh fixture, and is not proven against a real large source; ranking by placements ignores triangle count until that is measured.

## Evidence and follow-up

Zero-group/policy/exception automation and six independent joist import combinations pass. The negative joist payload itself lacks diagonals; no import policy can recover absent geometry. Corrected source geometry, rendered thin-member fidelity, broader compatibility and real compilation failure remain open. See [fixture evidence](../Validation/2026-09-26.md) and [processing helper](../../Source/DatasmithHISM/Private/ConVerseImportProcessing.cpp).
