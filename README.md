# ConVerse HISM Plugin

This repository contains an Unreal Engine 5.7 editor plugin for converting Datasmith-style actor hierarchies into managed hierarchical instanced static mesh outputs.

The plugin is intended to reduce actor count in imported scenes while preserving grouping by family, mesh, and materials.

## What It Does

The plugin provides editor tooling for:

- creating HISMs from the current editor selection
- batching selected static mesh components into HISMs
- running HISM creation inside Dataprep workflows
- consolidating similar static meshes before instancing

## Main Workflow

The primary tool is `Create HISMs From Selection`.

It:

1. Walks the current editor selection and attached child hierarchy.
2. Finds actors that represent eligible static mesh instances.
3. Groups them by logical family boundary, static mesh, and material set.
4. Creates managed `HierarchicalInstancedStaticMeshComponent` outputs.
5. Removes converted source actors and redundant hierarchy shells when safe.

This workflow is designed for Datasmith and Revit-style imported scene hierarchies.

## Plugin Layout

- `Plugins/ConVerseEditor/ConVerseEditor.uplugin`
- `Plugins/ConVerseEditor/Source/ConVerseEditor`
- `Plugins/ConVerseEditor/Source/ConVerseEditor/Public`
- `Plugins/ConVerseEditor/Source/ConVerseEditor/Private`

## Requirements

- Unreal Engine `5.7`
- Windows editor environment
- Dataprep-enabled Unreal installation for the Dataprep operations

## Installation

1. Copy `Plugins/ConVerseEditor` into your Unreal project's `Plugins/` folder.
2. Open the project in Unreal Engine 5.7.
3. Enable the plugin if Unreal prompts for it.
4. Regenerate project files if you are building from source.

## Usage

After enabling the plugin, open the Unreal Editor and use the ConVerse toolbar or Tools menu entries:

- `Create HISMs`
- `Batch To HISMs`
- `Consolidate Meshes`

The plugin also exposes Blueprint-callable editor utilities and Dataprep operations.

## Managed Output

Managed objects are identified with these tags:

- `ConVerseManagedHISM`
- `ConVerseManagedFamilyType`

Re-running the tool on the same boundary replaces older managed ConVerse HISM outputs for that boundary.

## Notes

- The plugin is editor-only.
- It assumes each convertible source actor effectively represents one mesh instance.
- Actors with multiple eligible static mesh components are skipped by the main HISM creation path.
- The detailed HISM behavior is further documented in `Plugins/ConVerseEditor/Source/ConVerseEditor/Public/README.md`.
