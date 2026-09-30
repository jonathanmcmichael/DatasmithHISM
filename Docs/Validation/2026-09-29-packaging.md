# 2026-09-29 Runtime Smoke Test Evidence

The packaged runtime smoke tests were executed successfully following the Batch A/C/D updates (36-test baseline).

## Execution

1. Generated a fresh runtime smoke test map `RuntimeSmokeTestMap` using headless optimized imports:
   - Imported `Joist16K6.udatasmith`
   - Imported `RevitLightExport.udatasmith`
2. Ran Unreal Automation Tool (`BuildCookRun`) to package a Win64 build of the `AdvancedHISM` project using the `RuntimeSmokeTestMap`.
3. Executed the packaged binary with the `ConVerse.ValidateImportedScene 3 RequireCollision RequireIES Exit` smoke command.

## Result

The diagnostic validated all 3 required source records and completed without errors:

```text
LogConVerseRuntimeValidation: Display: PASS cooked import: sources=3 meshComponents=1 lights=1 queryCollisionComponents=1 collisionTraceHits=1 IESProfiles=1 errors=0. Rendered fidelity requires separate acceptance.
```

No editor-only dependencies leaked into the packaged build, and runtime identity structures (`DatasmithHISMRuntime`) verified properly in the cooked environment.
