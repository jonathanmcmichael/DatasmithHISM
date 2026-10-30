# UE 5.6/5.8 engine compatibility and packaging, 2026-10-06

## Result

The Win64 `AdvancedHISMEditor Development` target builds successfully with UE 5.6.1 and UE 5.8.3. The plugin descriptor remains unpinned (`EngineVersion` is not set), so this adds a verified source-build version without excluding the established 5.8 build. The plugin remains Win64-only.

The host project's `EngineAssociation` is still `5.8`. Select UE 5.6 explicitly when opening the host project for a 5.6 trial; this compatibility work did not change the host project association.

## Compatibility changes

- `ConVerseNaniteSettings.h` accesses `UStaticMesh::NaniteSettings` before UE 5.7 and uses the accessor API in UE 5.7 and later.
- Package object enumeration uses the boolean overload before UE 5.8 and `EGetObjectsFlags` on UE 5.8 and later.
- Mesh-description drift identity uses `GetIdString()` before UE 5.8 and `GetUnversionedIdString()` on UE 5.8 and later.
- Import trace capture supplies missing channel names through `FTraceAuxiliary::Start`, supported by both engines, while retaining active-trace refusal and channel restoration behavior.

## Validation

| Engine | Check | Result | Evidence |
| --- | --- | --- | --- |
| UE 5.6.1 | `AdvancedHISMEditor Win64 Development` build | Passed, exit 0 | Host `Saved/Logs/UE56CompatBuild-20261006.txt` |
| UE 5.8.3 | `AdvancedHISMEditor Win64 Development` build | Passed, exit 0 | Host `Saved/Logs/UE58CompatBuild-20261006.txt` |
| UE 5.6.1 | `DatasmithHISM.OptimizedImport.TraceCaptureAndReportPath` | Failed, exit 1 | Host `Saved/Logs/UE56TraceCapture.txt` |

The focused 5.6 test started the trace and its fixture import reached `Verified` with one group and two instances. Datasmith import also emitted a handled ensure from UE 5.6's `FPrimitiveInstanceBoundsCache::GetValue` (`InstancedStaticMeshComponent.h:128`), which caused the automation test to fail. This result does not establish a plugin crash or a clean 5.6 runtime test; the full 5.6 automation suite was not run.

The cross-version build sequence initially reused generated files from the preceding engine. Removing only the host editor-target and plugin `Intermediate` products allowed each engine to regenerate its own UHT output; subsequent 5.8 and final 5.6 builds both passed. No source-generated or user-authored files were removed.

The editor target builds on both versions, and the UE 5.6.1 Win64 plugin package succeeded. Full UE 5.6 automation, interactive editor workflows, and runtime behavior remain unverified.

## UE 5.8 package attempt

Two `BuildPlugin` attempts were run with UE 5.8.3 for Win64, targeting `C:/Unreal/Packages/DatasmithHISM-UE5.8-Win64` and then the fresh `C:/Unreal/Packages/DatasmithHISM-UE5.8-Win64-Retry`. Both UAT attempts exited 6 before compiling the plugin: UBT could not open the shared `C:/Users/jonathanmc/AppData/Local/UnrealEngine/Intermediate/Build/BuildRules/MarketplaceRules.dll` because another process had it open while UBT was regenerating rules from UE 5.6.1 to 5.8.3.

This is an environment/file-lock failure, not a plugin compiler diagnostic. Both destinations may contain partial UAT output and are not usable UE 5.8 packages. The failing UBT logs are preserved at host `Saved/Logs/UE58BuildPlugin-20261006-failed.txt` and `Saved/Logs/UE58BuildPlugin-20261006-retry-failed.txt`. No process was stopped; do not retry until the process releases the shared rules DLL, using a fresh output directory if necessary.
