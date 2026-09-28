# Versioned import evidence

| Fixture | Provenance and purpose |
|---|---|
| [Joist16K6](Joist16K6/fixture.json) | Original mesh bytes from the structural export, Revit elements 610662/610663. Known incomplete geometry: 88 vertices, 160 triangles, no diagonal web members. This preserves the failing case; it is not a positive structural-fidelity acceptance fixture. |
| [RevitLightExport](RevitLightExport/fixture.json) | One point light and its original IES profile from the HVAC export. Exported intensity 100, Unitless. Validates preservation of source values; original Revit photometric calibration remains unavailable. |
| [ObservedAppearanceCatalog.csv](ObservedAppearanceCatalog.csv) | 245 observed appearances from three available exports. Stock Autodesk library identity and replacement coverage remain unverified. Import with `ConVerseAppearanceCatalogRow`. |

Each extracted scene has a `fixture.json` recording its origin and limitations. Parent hierarchies are omitted in the small fixtures, so they must not establish cross-discipline alignment. Source files on the Desktop were not edited.

The automation suite additionally generates fresh source fixtures for repeated and mirrored meshes, nested placement, multiple material slots, customized appearances, physical light units, cancellation, rollback, reimport, manual edits, and ownership conflicts. Those source generators clean up their own temporary inputs. Persistence tests instead import the checked-in negative joist fixture into uniquely named disposable maps/destinations and retain generated saved output. Do not assume all automation output was deleted.

The explicit [interruption runner](../Invoke-InterruptedRecovery.ps1) intentionally retains uncertain partial packages and a nonterminal journal as evidence. The final recorded session is `0a40897b43b1aa24c01bccb3e8bc1d0b`; diagnostics for it are expected. See [Phase 2 results](../../Docs/Validation/2026-09-26-phase2.md) before classifying or cleaning any generated artifacts.

Keep a corrected joist export as a separate version with known Revit dimensions and a rendered reference when it becomes available. Do not replace the negative fixture or redefine its missing webbing as a passing fidelity gate.


## Reproducing and extending the evidence

Use the [validation commands/matrix](../../Docs/VALIDATION.md) and [dated geometry/light results](../../Docs/Validation/2026-09-26.md). The structural fixture records Revit 2027 / exporter SDK 5.6.1; the HVAC light fixture records Revit 2025 / SDK 5.3.0-24761556. Their version difference is provenance, not a demonstrated explanation of the missing geometry or Unitless export.

Add positive fixtures with source/exporter/engine/plugin versions, stable element IDs, hashes, expected numerical values and rendered references. Ordinary/ISM/HISM and Nanite comparisons must use independent imported assets so an asset-level flag change does not contaminate another case. Generated maps, logs, OBJ exports and cooked archives live under the host project, outside the plugin repository.

Importing the observed CSV creates catalog evidence only. See [materials](../../Docs/MATERIALS.md) before approving a replacement. Preserve third-party provenance; these fixtures do not establish rights to distribute a future material pack.

The 2026-09-27 cancellation regressions generate a valid 3,145,782-byte BMP alongside repeated meshes, multiple materials and lights. Nine Analyze and eight pre-mutation import cases request cancellation at named progress phases. These generated fixtures and automated panel-state checks do not establish native UI responsiveness or representative full-source acceptance; see [the executed record](../../Docs/Validation/2026-09-27-preset-cancellation.md).
