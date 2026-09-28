# ADR 0008: Keep implementation evidence separate from release acceptance

Date: 2026-09-26. Status: **Accepted**. Scope: the current consolidated roadmap; validation status is separate.

## Context

Earlier docs incorrectly described automation as blocked and later treated safe-looking code as sufficient proof. Source fixtures can also be incomplete, even when import preserves them exactly.

## Decision

Record engine/plugin/source/exporter/fixture versions, exact settings, commands, exit codes, logs and limitations. Separate build, automation, editor interaction, persistence, rendered-scene, packaged-runtime and performance evidence. Critical identity/ownership guards need fault-sensitive behavioral tests through real dispatch where relevant.

Keep negative fixtures and add corrected positive versions. Do not redefine missing source geometry, unresolved light calibration or unreviewed catalog identities as passing acceptance. The six-phase ledger records remaining gates; Phase 6 remains deferred until earlier acceptance and a separate feature decision.

## Alternatives and consequences

A single green badge would hide real-data and rendering gaps. Repeating tests without a new change or unresolved concern provides little additional evidence. Dated records and source hashes make the conclusion assessable while keeping documentation-only refreshes distinct from new builds.

## Evidence and follow-up

The latest executed suite passed 30/30 tests on 2026-09-27 after [preset/cancellation fixes](../Validation/2026-09-27-preset-cancellation.md). Named-copy refusal, real and simulated save failures, saved drift and one actual interruption/restart checkpoint retain their [earlier Phase 2 evidence](../Validation/2026-09-26-phase2-evidence.json). Earlier packaged results remain separate. These results do not close the structural/lighting/catalog/live-UI/broader-recovery/rendered/performance gates. The earlier [2026-09-27 closeout](../Validation/2026-09-27-closeout.md) remains a documentation-only record of its own source snapshot and existing logs.
