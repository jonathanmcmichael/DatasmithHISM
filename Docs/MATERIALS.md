# Material catalog and reviewed matching

Current implementation, 2026-09-26. The catalog recognizes appearances; the mapping table authorizes specific replacements. Recognition alone leaves the imported material in use. See [ADR 0005](ADR/0005-reviewed-material-matching.md).

## Tables

Create the linked tables through **Review materials > Create project tables**. New project tables are created under `/Game/DatasmithImportSettings`, outside attempt rollback ownership. Existing selected tables are preserved. Native DataTable editors provide CSV import/export; save both table assets after editing.

| Appearance catalog field | Meaning |
|---|---|
| `CatalogId` | Nonempty unique catalog key used by mappings |
| `LibraryVersion` | Source/library version provenance; distinguish observed export version from verified Autodesk library identity |
| `AppearanceName`, `Aliases` | Display and candidate-search text, not sufficient approval evidence |
| `SourceIdentity` | Available source identifier/provenance |
| `ReferenceFingerprint` | Reviewed reference variant, if established |
| `Evidence` | Appearance/property/provenance notes and unresolved evidence |

Use row structure `FConVerseAppearanceCatalogRow` (`ConVerseAppearanceCatalogRow` in the editor).

| Replacement mapping field | Meaning |
|---|---|
| `CatalogId` | Link to a known catalog entry |
| `SourceFingerprint` | Exact appearance variant being approved |
| `Replacement` | Soft reference to the project material or material instance |
| `bApproved` | Explicit approval state |
| `Revision` | Revision participating in active mapping identity |

Use row structure `FConVerseMaterialMappingRow`. Approved rows require a valid target and unique source fingerprint. Missing catalog identity, duplicate approved fingerprints, or missing targets block application with a diagnostic.

## Review and apply

1. Analyze the source and open **Review materials**. Search source appearances and inspect exported evidence, suggested catalog entries, usage, and approval status.
2. Import [ObservedAppearanceCatalog.csv](../Tests/Fixtures/ObservedAppearanceCatalog.csv) if useful, or add the selected observed appearance to the catalog. Neither action approves a replacement.
3. Establish one catalog identity and a suitable target. Source and target material assets can be opened in their native editors when available. Source imported-material assets become available after import; Analyze alone does not create them.
4. Approve the exact variant, then save both tables. Enable **Apply Approved Materials** in the import settings and save the preset.
5. Analyze the proposed changes, then rebuild explicitly. Mappings apply to owned mesh defaults and component overrides before Nanite compatibility checks. Original assignments remain recorded.
6. Revoke or revise an approval in the review/table workflow when needed. Changing the table does not automatically alter a previously imported scene.

CSV `Name` is the DataTable row key. Prefer exporting a table from Unreal to establish its exact array/soft-reference syntax before hand-editing CSV. The observed inventory contains **245 appearances** from available project exports. It deliberately leaves reference fingerprints blank and does not claim Autodesk stock-library coverage.

## Matching evidence and invalidation

Names and aliases suggest candidates. Revit instance appearance fingerprints use available typed properties, exported UV transforms, and texture-content evidence. Missing texture evidence cannot establish an exact approval. Non-Revit material graph fingerprints conservatively include scene texture content, so an unrelated texture change may require renewed review.

Changed appearance evidence cannot inherit approval solely because the source material keeps its name. Recognized but unmapped, customized, and ambiguous variants keep their imported appearance. An approved entry whose appearance is absent/changed is diagnosed. Conflicting approved targets for a shared imported material are rejected.

Target assets stay outside attempt cleanup and are not rewritten by rollback. A preset stores table references, not a portable copy of those tables or their materials; transferring a preset requires transferring its referenced project assets.

## Remaining acceptance and material pack

Automation covers approved application, missing targets, preset round-trip, changed-appearance invalidation, and material rebuild preview. Stock-library identity/version review, physical texture scale/orientation, rendered comparison, broader live review-UI acceptance, and approved replacement coverage remain open.

The future Unreal material pack should use these catalog identifiers and add separately reviewed equivalents. Appearance fidelity, rendering compatibility, texture provenance, and distribution rights must be established before publishing that pack. No pack or majority-stock coverage is delivered by the observed inventory alone.
