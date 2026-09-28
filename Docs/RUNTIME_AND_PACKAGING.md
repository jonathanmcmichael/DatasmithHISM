# Runtime metadata and Windows packaging

The release targets Unreal Editor import and **packaged Windows applications containing the resulting scene**. It does not provide packaged source-file import. Editor orchestration, manifests, material review, and recovery remain in `DatasmithHISM`; cookable identity is in `DatasmithHISMRuntime`. See [ADR 0007](ADR/0007-editor-runtime-split.md).

## Source lookup

Imported mesh/light owners carry `UConVerseSourceMetadata` with `FConVerseSourceRecord` entries. Records hold component name, instance index, source element and label, source mesh, source document fingerprint, and flattened metadata.

Call Blueprint-pure `UConVerseSourceMetadata::FindSource(Component, InstanceIndex, Record)`:

- For ISM/HISM, pass the instance index, such as a hit result's Item index.
- For an ordinary mesh or light, pass `INDEX_NONE` (`-1`).
- Treat a false return as no matching recorded source, rather than inventing identity from a display label.

The mapping describes the committed instance layout. Runtime code that removes/reorders instances must maintain a corresponding mapping itself; selective extraction and dynamic remapping are deferred. An exported element name is stored even when authoritative Revit/IFC metadata is missing or ambiguous.

## Build a validation package

Save the imported result and its owning map first. Run from the project workspace with the editor closed. The map below is local generated validation content, not an assumed checked-in production map; import the fixtures and create an equivalent map if it is absent.

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat' BuildCookRun `
  -project='D:\Unreal\Sandbox\AdvancedHISM\AdvancedHISM.uproject' `
  -noP4 -platform=Win64 -clientconfig=Development -build -cook `
  -map=/Game/ConVerseValidation/SaveAsAcceptance -stage -pak -archive `
  -archivedirectory='D:\Unreal\Sandbox\AdvancedHISM\Saved\ConVersePackagedValidationFinal' `
  -unattended -utf8output
```

The recorded archive is `Saved/ConVersePackagedValidationFinal/Windows`. Include the appropriate map(s) and all referenced target materials/textures in the cook. A successful editor import alone does not establish packaged content availability.

## Runtime smoke command

Development builds expose an opt-in console command:

```text
ConVerse.ValidateImportedScene 3 RequireCollision RequireIES Exit
```

The first argument is the expected count of source records. `RequireCollision` requires a successful query-collision trace and `RequireIES` requires IES evidence. `Exit` requests completion after the check. This diagnostic is not a shipping runtime import service.

The recorded fixture map contains two joist instances and one source light: three source records. Both NullRHI and DX12 runs passed with one mesh component, one light, one IES profile, one successful collision trace, and zero reported validation errors. See [logs and caveats](Validation/2026-09-26.md#packaged-output).

DX12 startup and reference/trace checks do not prove rendered equivalence. Remaining acceptance includes ordinary textures/UVs, wider light types, navigation/LOD/culling, source lookup in application interactions, and representative camera-path CPU/GPU performance. Editor-only preview/progress and later Phase 2 persistence changes were built/tested after the recorded cook; the runtime module did not change and no new cook was claimed.

The Phase 2 follow-up also ran `ConVerse.ValidateImportedScene 2 Exit` in a fresh editor process against its two-instance map and found zero errors. The command's log text says `cooked import` even in that context. Record the actual executable/environment: that run is editor lookup evidence, not another packaged result. See [Phase 2 scope and logs](Validation/2026-09-26-phase2.md).
