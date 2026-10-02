using UnrealBuildTool;

public class DatasmithHISM : ModuleRules
{
	public DatasmithHISM(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"DatasmithHISMRuntime",
				"DataprepCore",
				"Engine"
			});

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"AssetRegistry",
				"AssetTools",
				"Blutility",
				"DatasmithContent",
				"DatasmithCore",
				"DatasmithExporter",
				"DatasmithImporter",
				"DatasmithTranslator",
				"DesktopPlatform",
				"EditorFramework",
				"ExternalSource",
				"InputCore",
				"Json",
				"JsonUtilities",
				"PropertyEditor",
				"Settings",
				"MaterialEditor",
				"MeshDescription",
				"MeshMergeUtilities",
				"Projects",
				"Slate",
				"SlateCore",
				"StaticMeshDescription",
				"TraceLog",
				"ToolMenus",
				"UMG",
				"UnrealEd"
			});
	}
}
