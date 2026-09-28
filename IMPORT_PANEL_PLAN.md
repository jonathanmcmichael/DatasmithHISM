# Optimized import panel design

Current implementation summary, reconciled 2026-09-27. The original five-phase panel implementation plan is [archived](Docs/History/2026-09-26/IMPORT_PANEL_PLAN.md). Current scope is the [six-phase plan](PLAN.md); required behavior remains in the [import contract](IMPORT_PANEL_VALIDATION.md).

## Workflow and controls

The panel opens through **Tools > Optimized Datasmith Import**. Source support comes from enabled translators, not an extension allowlist. Controls include destination, ISM/HISM, minimum group size, translator tessellation, Nanite/light/material settings, Analyze, Import and Verify, Rebuild from source, Save imported result, named presets, Rendering settings, and Review materials.

Analyze presents source reading, supporting-file checks, translation, grouping, and report preparation. It creates no scene actors/assets. Measurable loops report items; blocking translator work reports its stage. Cancellation becomes final only after controllable work stops and any attempted output is accounted for.

Zero-group analysis can still import retained ordinary geometry or lights. Structured inspection distinguishes source elements, mesh assets, actors, components, and instances. Search includes source/Revit identity; focus/open actions resolve ordinary meshes, instances, and lights where recorded.

## State and decisions

| Situation | Required presentation |
|---|---|
| Verified output in memory | Verified, unsaved |
| Explicit save succeeds | Saved; tracked drift or accepted failures remain degraded/unverified |
| Save fails | Incomplete save with affected package/map; do not imply all output is durable |
| Native named-map copy cannot prove ownership | Refuse rebinding, name the original owning map and give recovery instructions |
| Tracked edits before replacement | List changes and require Replace with source or Cancel |
| AlreadyCurrent | Reverify existing output; report drift and sidecar warnings separately |
| Failed verification | Default rollback; optional explicit whole-session acceptance remains degraded/quarantined |
| Rollback failure or interrupted attempt | Actionable paths and recovery diagnostics; no deletion of uncertain objects |
| Older comparison data | Explain which fields were not recorded; do not silently upgrade |

Rebuild previews show group additions/removals, ordinary output counts, material/light changes, and tracked edits. Advisory renderer settings never change global configuration. Restoring session settings never starts an import.

## Validation

The 2026-09-26 editor build and all 27 automation tests passed. Actual interruption/restart at one recorded checkpoint, native-copy refusal and partial-save behavior have [Phase 2 evidence](Docs/Validation/2026-09-26-phase2.md). Live Slate progress/cancellation, large-file responsiveness, material review, source/instance/light focus, presets, rebuild previews and visible recovery feedback remain in the [interaction checklist](Docs/Validation/2026-09-26-phase2-ui.md). Service tests do not establish those interactions.
