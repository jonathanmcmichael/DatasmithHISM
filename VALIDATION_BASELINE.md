# DatasmithHISM Validation Baseline

Recorded: 2026-09-23

## Environment

- Project: `D:\Unreal\Sandbox\AdvancedHISM\AdvancedHISM.uproject`
- Engine: Unreal Engine 5.8.3 (`++UE5+Release-5.8-CL-58210709`)
- Plugin: `Plugins/DatasmithHISM`
- Source control: unavailable; `D:\Unreal\Sandbox\AdvancedHISM` is not a Git working tree.

## Build evidence

The following command completed successfully on 2026-09-23:

`C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat AdvancedHISMEditor Win64 Development D:\Unreal\Sandbox\AdvancedHISM\AdvancedHISM.uproject -WaitMutex`

UnrealBuildTool reported the target was up to date and returned `Result: Succeeded`.

## Available fixtures

- Two generated automation fixtures are present under `Saved/DatasmithHISM/Automation`, each with a `.udatasmith` file and `.udsmesh` sidecar.
- `Saved/Autosaves/Temp/Untitled_1_Auto1.umap` is an editor autosave only.
- No representative IFC input, external Datasmith export, or imported validation map is present in the workspace.

## Editor smoke-test checklist

Run these checks only against disposable content:

1. Open **Tools > Optimized Datasmith Import**, select a `.udatasmith` source and a new `/Game/...` destination, then run **Analyze**. Confirm no assets or actors are created.
2. Run **Import and Verify** with ISM output. Confirm the report succeeds and owned output is created only in the selected destination.
3. Repeat with HISM output. Confirm the requested component class and instance counts.
4. Run **Dedupe Meshes** as a dry run. Confirm no asset references or assets change.
5. Run **Dedupe Meshes**, decline confirmation, and confirm no references, assets, or Managed ISMs change.
6. Run **Dedupe Meshes**, accept confirmation, then use Undo. Confirm references and source assets are restored.
7. Run **Explode** on a managed ISM/HISM component, validate replacement actors, then use Undo.
8. Execute the equivalent Dataprep actions and confirm no interactive dialog blocks the pipeline.

## Known validation gaps

- `DatasmithHISM.OptimizedImport.GeneratedFixtureEndToEnd` compiles but cannot currently run from the packaged command-line editor because optional LinuxArm64 and VisionOS SDK validation aborts before the test runner starts.
- Validate the importer with a representative Revit or IFC `.udatasmith` file and its `_Assets` folder when one is available.
- Optimizer-aware reimport is not implemented. Test changed sources by importing into a new destination folder.
