> Historical snapshot before the 2026-09-26 documentation consolidation. Claims and task status below are historical, not current guidance. See the [current documentation index](../../README.md). Relative links have been adjusted for this archive.

# Customizing the Datasmith Import Process - Key Points

Condensed from [Epic's "Customizing the Datasmith Import Process"](https://dev.epicgames.com/documentation/unreal-engine/customizing-the-datasmith-import-process-in-unreal-engine).

> **Why this page matters here.** Unlike the [Export SDK guidelines](DatasmithExportSDKGuidelines.md), which are written for exporter authors, this page describes the **importer** side and documents the exact two-stage pattern `FConVerseDatasmithImportService` implements. It is the closest thing Epic publishes to an official sanction for what this plugin does, and it also names a real hazard for our reimport path.

---

## The two-stage import process

Every Datasmith import, from `.udatasmith` or CAD/Revit/IFC alike, is internally two stages:

1. **Translate** - the importer reads the source file into an in-memory data structure, the **Datasmith Scene** (`IDatasmithScene`). This holds the 3D objects, their relationships, and every property Datasmith could extract.
2. **Finalize** - the scene elements become Unreal Assets in the Content Browser. The Datasmith Scene Asset is then spawned into the current Level, which in turn spawns its children: Actors, Static Mesh Actors, Lights, Cameras.

Scripted imports may **de-construct these two stages and insert processing between them**. That seam is the entire basis of this plugin's optimization.

## The sanctioned customization sequence

Epic's documented order of operations:

1. Construct an in-memory Datasmith Scene from a `.udatasmith`, CAD, or other supported file on disk.
2. Modify the scene to change how it becomes Unreal Assets.
3. Set up import options, equivalent to the editor UI settings (destination path, which object types to create, tessellation for parametric CAD, and so on).
4. Finalize the scene into Unreal Assets.
5. **Destroy the scene** when no longer needed, to release its memory.
6. Post-process the generated Assets (collision, LODs, etc.).

> **How this maps to our code.** `LoadFreshSource` performs step 1, `ApplyResolvedPlan` performs step 2 at the element layer, and `CreateFromExternalSource` performs step 4. Our HISM-to-ISM conversion and verification are step 6.

Metadata is the recommended way to identify which objects to change. See [Using Datasmith Metadata](https://dev.epicgames.com/documentation/unreal-engine/using-datasmith-metadata-in-unreal-engine).

---

## Pre-import modification breaks ordinary reimport

**This is the most important warning on the page, and it directly validates this plugin's manifest design.**

Epic states plainly:

> Customizing the import process is very likely to have an effect on the re-import process. For example, if you use a script to remove elements such as meshes or lights from the Datasmith Scene before you complete the import process, then you re-import the Datasmith Scene Asset, your pre-processing script is bypassed during the re-import. The result is that the objects you originally filtered out from the scene are detected as newly added, and are added to your Project or Level.

In other words: **a stock Datasmith reimport does not know your pre-import transform happened.** It re-reads the original file, sees the elements your script collapsed or removed, and re-adds them as new. For this plugin, an ordinary reimport of an optimized scene would resurrect the individual mesh actors that were collapsed into ISM/HISM components, silently undoing the optimization and duplicating geometry.

This is precisely why the plugin:

- attaches a `UConVerseOptimizedImportManifest` recording the source identity and every created actor and asset;
- guards the ordinary Datasmith reimport path on optimized scenes;
- routes reimport through `ImportAndVerify`, which supersedes the prior session deliberately rather than letting Datasmith diff against an unmodified source.

Epic's own recommendation is conservative:

> For now, we recommend doing most modifications **after** import... Modify the Datasmith Scene during import only if you have a particular need that you can't fulfill by modifying Assets and Actors after you finalize the import, such as **preventing the creation of certain Assets**.

Our case is the stated exception. Collapsing thousands of redundant Static Mesh Actors into instanced components is specifically about *preventing the creation* of those actors and assets; doing it after finalize would mean paying the full import cost first, then deleting the result. The tradeoff is accepted knowingly, and the manifest exists to pay for it.

---

## Import options are per-format

The API is identical across source formats; only the **options class** differs:

| Source | Options class |
|---|---|
| All Datasmith-supported files | `DatasmithImportOptions` (destination asset/path, what to import, lightmap resolution) |
| CAD | `DatasmithCommonTessellationOptions` (tessellation settings) and `DatasmithCADImportOptions` |
| Cinema 4D | `DatasmithC4DImportOptions` |

Key behaviors:

- Requesting an options class that does not apply to the constructed source returns **null**. Test for it.
- `get_all_options()` returns every options class the scene element owns, as a map keyed by class. **Use this when the source format is not known ahead of time** - which is the normal case for a general importer like this one.

> **Relevance.** This reinforces the multi-format decision already made here: never gate on file extension. Resolve a translator, then read whatever options the resulting scene element actually exposes.

---

## Datasmith Scene contents

The scene is a container of elements, each becoming either a Content Browser Asset or a Level Actor.

**Asset elements:**

- **Meshes** - a block of 3D geometry; becomes a Static Mesh under `Geometry`. Each has material slots associated **by name** with material elements.
- **Materials** - a distinct surface type; becomes a Material under `Materials`.
- **Textures** - a single 2D image used by at least one material; becomes a Texture under `Textures`.

**Actor elements:**

- **Mesh actors** - an instance of a mesh geometry; becomes a Static Mesh Actor in the World Outliner.
- **Light actors** - becomes a Point Light, Spot Light, or a custom Datasmith Actor simulating an Area light. Intensity, color, and IES profiles are get/settable.
- **Camera actors** - becomes a `CineCameraActor`.

> **Relevance.** "Mesh actor element = one instance of a mesh geometry" is the exact unit this plugin groups. Many mesh actors referencing one mesh element is the redundancy the ISM/HISM optimization collapses. Note also that material slots bind **by name**, consistent with the rule elsewhere in this repo that identity comes from element *names*, never labels.

The in-memory scene mirrors the `.udatasmith` XML closely, so an exported file is a readable reference for how the scene object is structured.

## Working with the scene

- Retrieve element lists via `DatasmithSceneElementBase` (Python) or the `Datasmith > Scene` nodes (Blueprint).
- Iterate to a specific element, then use its element API (for example `DatasmithMeshActorElement`) to get and set properties.
- Actor elements expose **child actor elements**, allowing downward traversal of the hierarchy.
- Elements can be **removed and added**, including re-parenting actors by removing and re-adding them under different parents.

> **Relevance.** This is the element-layer API surface `ApplyResolvedPlan` operates on. It also confirms a correction recorded in `HANDOFF.md`: this layer exposes geometry, hierarchy, and material bindings, but **no collision API**. Collision is necessarily a post-finalize concern on the created components, which is where this plugin already handles it.

---

## Relevance to DatasmithHISM - summary

| Epic guidance | Effect here |
|---|---|
| Import is two stages with a scriptable seam between them | Sanctions the whole optimized-import architecture |
| Pre-import modification is bypassed on reimport, re-adding filtered elements | **Confirms the manifest and reimport guard are mandatory, not defensive extras** |
| Prefer post-import modification except to prevent asset creation | Our use case is the documented exception; tradeoff accepted |
| Options classes vary by format; use `get_all_options` when unknown | Reinforces translator-driven format handling over extension allowlists |
| Material slots bind by name | Matches the element-name identity rule |
| Element layer exposes geometry and hierarchy only | Collision belongs after finalize, as implemented |
| Destroy the in-memory scene when done | Watch lifetime of scenes held across the import transaction |
