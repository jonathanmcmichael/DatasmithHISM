# ADR 0005: Separate appearance recognition from replacement approval

Date: 2026-09-26. Status: **Accepted**. Scope: the current consolidated roadmap; validation status is separate.

## Context

Autodesk material names can be renamed or customized while retaining familiar labels. Catalog coverage does not imply that an equivalent Unreal material exists or that replacing a particular appearance is approved.

## Decision

Use linked appearance-catalog and replacement-mapping DataTables with CSV workflows. Names/aliases suggest candidates. Exact source appearance evidence, including available properties and texture/UV evidence, binds a reviewed approval to a variant. Imported materials remain the default for unapproved, ambiguous or changed appearances.

Apply approved mappings to owned mesh defaults and component overrides before Nanite processing. Record original assignments. Reject missing approved targets and conflicting approvals. Keep project replacement assets outside rollback ownership. Include active mapping identity/revision in PlanId; catalog-only edits do not rebuild output.

## Alternatives and consequences

Name-only replacement is convenient but cannot distinguish customized variants. Bundling a replacement pack before identity review would confuse recognition with appearance equivalence. Exact evidence is conservative and can require renewed review, especially when non-Revit graphs include broader texture evidence.

## Evidence and follow-up

Approval application, absent target, changed appearance, preview and preset tests pass. The 245 observed entries are not verified stock-library coverage. Library/version curation, physical scale, visual equivalence and the optional pack remain separate work. See [materials guide](../MATERIALS.md) and [row types](../../Source/DatasmithHISM/Public/ConVerseImportRecipe.h).
