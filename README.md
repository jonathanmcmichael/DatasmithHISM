# ConVerse

ConVerse is an Unreal Engine 5.7 project.

The repository contains the main `ConVerse` runtime project module and local editor-focused plugins for authoring and content-processing workflows.

## Project Layout

- `ConVerse.uproject`: Unreal project descriptor.
- `Source/ConVerse`: primary runtime game module.
- `Plugins/ConVerseEditor`: local editor plugin for mesh consolidation, HISM generation, Dataprep operations, and material tooling.
- `Plugins/DatasmithHISM`: additional local plugin content.
- `Content`: Unreal assets for the project.
- `Config`: project configuration.
- `Build`: build metadata and platform-specific files.

## Requirements

- Unreal Engine `5.7`
- Windows development environment
- Visual Studio or Rider with Unreal support

## Opening The Project

1. Open `ConVerse.uproject` in Unreal Engine 5.7.
2. If project files are stale, regenerate them from the `.uproject`.
3. Open the generated solution in Rider or Visual Studio.

## Plugins

### ConVerseEditor

`ConVerseEditor` is a local editor plugin under `Plugins/ConVerseEditor`.

It provides editor utilities for:

- consolidating similar static meshes
- creating hierarchical instanced static mesh groups from selections
- running Dataprep editor operations
- generating powdercoat substrate materials

Additional implementation notes for the HISM workflow are documented in:

- `Plugins/ConVerseEditor/Source/ConVerseEditor/Public/README.md`

## Source Control Notes

If this project is moved to GitHub, only source assets and configuration should be committed. Generated and machine-local Unreal folders should be ignored, including:

- `Binaries/`
- `DerivedDataCache/`
- `Intermediate/`
- `Saved/`
- `.vs/`
- `.idea/`

Large cache or database files should also stay out of Git, including:

- `cesium-request-cache.sqlite`

## Current State

- `ConVerse` is the active runtime module.
- `ConVerseEditor` is enabled as a plugin in `ConVerse.uproject`.
- The project still contains `Source/ConVerseEditor.Target.cs`; if plugin-only editor builds are desired, that target may need review later.
