# ADR 0004: Preserve photometric evidence and keep renderer advice advisory

Date: 2026-09-26. Status: **Accepted**. Scope: the current consolidated roadmap; validation status is separate.

## Context

The supplied HVAC source declares 1,033 point lights as Unitless. Replacing that label with Lumens or Candelas would change interpretation without establishing the original physical quantity.

## Decision

Inventory source light types, enabled local count, units, intensity and IES evidence. Verify light object presence and non-IES source values. Treat IES profile attachment and profile-brightness differences as best-effort warnings; they must not roll back otherwise verified scene objects. Keep ambiguous Unitless values and issue an unresolved calibration diagnostic until authoritative Revit values support a tested conversion, including falloff and exposure.

Warn at 100 enabled local lights by default; make the threshold configurable. Report the observed project MegaLights setting and provide navigation. Never change global rendering settings automatically, and exclude advisory thresholds/settings from destructive plan identity.

## Alternatives and consequences

Relabeling units alone is not a brightness correction. Auto-enabling a renderer feature would alter the wider project based on one import. A preserved but unresolved source is more honest than an uncalibrated physical claim. The threshold is a project advisory, not a measured hardware limit.

## Evidence and follow-up

Generated physical-unit/threshold checks and the exported point-light fixture pass. The CWM import verifies its objects when 67 unavailable IES profiles are omitted; it exits 0 without Datasmith IES errors. This does not establish photometric equivalence: calibrated Revit values and rendered comparison remain open. See [source evidence](../Validation/2026-10-02-cwm-source.md), [earlier light evidence](../Validation/2026-09-26.md), and [rendering policy](../ARCHITECTURE.md#rendering-policy-and-open-acceptance). MegaLights navigation follows [Epic's project settings guidance](https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine).
