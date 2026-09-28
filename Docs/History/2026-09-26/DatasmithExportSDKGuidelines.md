> Historical snapshot before the 2026-09-26 documentation consolidation. Claims and task status below are historical, not current guidance. See the [current documentation index](../../README.md). Relative links have been adjusted for this archive.

# Datasmith Export SDK Guidelines - Key Points

Condensed from [Epic's Datasmith Export SDK Guidelines](https://dev.epicgames.com/documentation/unreal-engine/datasmith-export-sdk-guidelines?lang=en-US).

> **Scope note.** The source page is written for authors of *exporter* plugins that run inside a design application (Revit, 3ds Max, SketchUp) and write `.udatasmith` files. This project is on the *importer* side. Sections are annotated with how they apply here; see [Relevance to DatasmithHISM](#relevance-to-datasmithhism) for the parts that directly constrain our work.
>
> For the importer-side counterpart, see [Customizing the Datasmith Import Process](DatasmithImportCustomizationGuidelines.md), which documents the two-stage translate/finalize seam this plugin is built on.

---

## The two-step model

1. Parse the design application and build an `IDatasmithScene` using the **DatasmithCore** API.
2. Write the scene to disk using the **DatasmithExporter** API.

Datasmith exists to carry things generic formats (FBX, OBJ) lose: LODs, collision, lights, object hierarchy, metadata, and large meshes, plus texture reformatting to power-of-2 and Unreal-supported formats.

## Unreal data model

```
Level -> Actors (position/rotation/scale, layers, visibility)
		   -> Components: Static Meshes, Lights, Cameras, ...
				-> Static Mesh -> Material / Material Instance -> Texture
```

One Static Mesh may be referenced by many Actors. That reuse is **geometry instantiation**, and it is the foundation this plugin's ISM/HISM optimization builds on.

## Plugin topologies

- **Exporter + importer pair** - a plugin inside the design app writes `.udatasmith`; Unreal's Datasmith File Importer reads it. Used by Revit, 3ds Max, SketchUp.
- **Direct importer** - Unreal reads the application's native format directly. Used by Rhino, Solidworks, Cinema4D.

Both converge on the same translator/importer machinery inside Unreal, which is why a well-written importer should not care which format produced the data.

---

## Design principles

### Export granular, optimize on import

Epic's stated split: **export everything object-by-object, and defer merging, polygon reduction, and other data preparation to Unreal.**

> "It's best to have the least amount of options (or none at all) exposed in the Datasmith exporter, and let the Unreal Engine user make most of the decisions during the import."

This is the architectural justification for an import-side optimizer: instance merging is explicitly named as import-side work, not exporter work.

### Reimport requires stable identity

Datasmith's reimport workflow preserves in-editor work across source changes. Two requirements follow:

1. **Entities need a persistent unique identifier.** Names are explicitly called out as a bad identity strategy, because multiple objects can share a name.
2. **Entities are saved with a hash value** so reimport can compare cheaply instead of doing expensive element-by-element comparison.

The hash is derived from object data (vertex/face/UV counts, for example), so comparing two numbers replaces comparing two meshes.

### Leave artistic decisions to the engine

Export geometry, materials, lights, and metadata. Cameras, environments, and backgrounds are generally better decided by the Unreal or Twinmotion user after import.

---

## UX guidelines

### Exporter UI

- Favor **WYSIWYG**: rely on the host app's existing view and filter state (Revit exports the active View; SketchUp exports what's on screen). Do not invent a parallel selection UX.
- Favor **no export options at all**; if options are unavoidable, keep them minimal.
- **Avoid** exposing data-prep and optimization options (geometry detail, object type filtering, UV channels) at export time.

### Progress, cancellation, and errors

This section is normative and applies equally to long-running import-side operations:

- **Progress information must be presented** during the operation.
- **Users must be able to cancel** the process.
- **An error log should be displayed** covering unsupported objects, missing textures, and similar issues.
- **Avoid successive modal dialogs.** Do not interrupt the operation with an OK/Cancel window per warning; collect into a log.
- Batch processing and scripting support is called out as valuable, since users script exports through the host application's native scripting language.

Relevant APIs: `IDatasmithProgressManager`, `FDatasmithLogger`.

---

## File and folder structure

A Datasmith "file" is **two things**:

- `[filename].udatasmith` - an XML data structure.
- `[filename]_Assets/` - a **sidecar folder** holding every associated asset.

**Must have**

- Exactly one `.udatasmith` file and one matching `_Assets` folder.
- All assets inside that `_Assets` folder.
- Assets referenced by **relative** paths.

**Avoid**

- Absolute asset paths.
- Extra folders or subfolders holding assets outside the `_Assets` folder.

> This is why source identity is not a single file. Any integrity or change-detection scheme that hashes only the `.udatasmith` file ignores everything in `_Assets`.

## File header

The header carries provenance, and Epic collects telemetry on file type and source:

```xml
<DatasmithUnrealScene>
	<Version>0.24</Version>
	<SDKVersion>4.25</SDKVersion>
	<Host>Revit</Host>
	<Application Vendor="Autodesk Inc." ProductName="Revit" ProductVersion="2018"/>
	<User ID="..." OS="Windows 10 (Release 1709)"/>
```

Useful for recording exporter and host versions when diagnosing an import.

---

## Element guidelines

### Static Mesh Assets (`IDatasmithMeshElement`)

**Must have**

- Correct smoothing, normals, and tangents.

**Useful**

- Additional LODs, collision meshes, and a lightmap UV channel (unwrap).

**Avoid**

- Mesh names that are not unique and repeatable across exports. **Do not use user-specified object names.**
- Baking unit rescaling into Actor transforms.
- Leaving pivots at the origin.
- Exporting thousands of mesh Actors that should be welded together (a box is one 6-face mesh, not six 1-face meshes).

APIs: `IDatasmithMeshElement`, `FDatasmithMesh`, `FDatasmithUtils::SanitizeObjectName`.

### Static Mesh Actors (`IDatasmithMeshActorElement`)

Mesh Actors carry no geometry; they point at a mesh asset. Several Actors may reference one mesh.

**Must have**

- **Unique, stable Actor names** that do not change between exports. Required for reimport tracking.
- **Sanitized, human-readable labels.**
- **Mesh assets reused across Actors where applicable** (true instancing).
- Scale and coordinate conversion **baked into the mesh**, not applied to Actor transforms.

**Useful**

- Layer assignment, tags, and metadata.

> Note the deliberate split: **name = stable identity, label = display text.** Labels are sanitized on import (spaces become underscores), so labels are not a safe identity key.

### Empty Actors

Actors with no components. Use them to represent null/helper objects, custom origins (Revit site locations), hierarchy aids (Rhino layers or block origins, Revit levels), or the head of a compound object with no geometry of its own (Revit curtain walls).

### Tags

- **Prefix tags with the source application**, e.g. `Revit.TagName`, `Max.TagName`.
- Use tags for *technical* information about source structure; use **metadata** for user-defined data.

APIs: `IDatasmithActorElement::AddTag`, `IDatasmithActorElement::SetIsAComponent`.

### Metadata

Key/value pairs for BIM or other custom data.

**Limitations**

- **Values are strings only.** Floats and units must be baked into the string, e.g. `"10 mm"`.
- **No hierarchical properties.** Flatten with an underscore separator, as Revit does for Element and Type properties.

APIs: `IDatasmithMetaDataElement`, `SetAssociatedElement`, `FDatasmithSceneFactory::CreateKeyValueProperty`.

### Camera Actors

Unreal cameras are physically based; set sensor width, aspect ratio, exposure, white point, and depth of field at export time. Post-process effects are optional.

**Limitation:** Unreal does not support skewed / 2-point-perspective cameras, so Revit "cropped" views cannot be represented.

### Texture Assets

Textures must declare **intended use** (`texturemode`): Diffuse, Specular, Normal, NormalGreenInv, Displace, Bump, Ies, Other. Color space (gamma / sRGB) must also be specified, as it directly affects lighting and shading.

---

## Reference material

- Datasmith SDK: `\Engine\Source\Programs\Enterprise\Datasmith\DatasmithSDK\`
- Reference exporters: `\Engine\Source\Programs\Enterprise\Datasmith\`
  - SketchUp (scene, components, progress): `DatasmithSketchUpRubyExporter/Private/`
  - 3ds Max (metadata, cameras): `DatasmithMaxExporter/Private/`
- API docs: [DatasmithCore](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/DatasmithCore), [DatasmithExporter](https://dev.epicgames.com/documentation/unreal-engine/API/Developer/DatasmithExporter)

---

## Relevance to DatasmithHISM

This project is an **import-side optimizer**, so the exporter checklists are mostly background. Four points bear directly on current work:

1. **Import-side optimization is the sanctioned design.** Epic explicitly assigns object merging and data preparation to the import side. Optimizing repeated mesh actors into ISM/HISM is consistent with the intended division of labor, not a workaround.

2. **Sidecar files are part of source identity.** A Datasmith source is a `.udatasmith` file *plus* its `_Assets` folder. Hashing only the primary file cannot detect changed geometry or textures, which matters most for Revit and CAD sources. This drove the sidecar hashing now implemented under Amendment 5 in `IMPORT_PANEL_VALIDATION.md`: the folder is hashed as an aggregate fingerprint, and a sidecar-only change warns rather than triggering a destructive reimport.

3. **Progress, cancellation, and a non-modal error log are Epic's stated expectations** for long-running Datasmith operations, matching the panel contract's existing requirements. Per-warning modal dialogs are explicitly discouraged; aggregate into a report.

4. **Identity comes from element names, not labels.** Names are contractually stable across exports; labels are display text and are sanitized on import. Grouping, verification, and manifest matching should key off names and mesh element identity.

Also worth noting: Epic's own reimport design uses **content-derived hashes** to avoid expensive comparison, the same strategy used by this plugin's manifest.
