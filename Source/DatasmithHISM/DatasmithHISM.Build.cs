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
				"MaterialEditor",
				"MeshDescription",
				"MeshMergeUtilities",
				"Projects",
				"Slate",
				"SlateCore",
				"StaticMeshDescription",
				"ToolMenus",
				"UMG",
				"UnrealEd"
			});
	}
}
