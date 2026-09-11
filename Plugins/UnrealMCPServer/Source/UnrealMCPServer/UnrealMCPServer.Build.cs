// Copyright StraySpark 2026 All Rights Reserved.

using UnrealBuildTool;

public class UnrealMCPServer : ModuleRules
{
	public UnrealMCPServer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Json",
			"JsonUtilities",
			"HTTPServer",
			"HTTP",
			"Slate",
			"SlateCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor core
			"UnrealEd",
			"EditorSubsystem",
			"LevelEditor",
			"EditorFramework",
			"ToolMenus",
			"DeveloperSettings",
			"Projects",

			// Asset management
			"AssetTools",
			"AssetRegistry",
			"ContentBrowser",
			"ContentBrowserData",

			// Blueprint & graph
			"Kismet",
			"KismetCompiler",
			"BlueprintGraph",
			"GraphEditor",

			// Rendering & viewport
			"RenderCore",
			"RHI",
			"ImageWrapper",

			// Platform
			"DesktopPlatform",
			"ApplicationCore",

			// Settings
			"EngineSettings",

			// Niagara particle system (required plugin dependency)
			"Niagara",

			// Landscape terrain system
			"Landscape",
			"LandscapeEditor",

			// Foliage system
			"Foliage",

			// Sequencer / MovieScene tools
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
			"SequencerCore",

			// Navigation system
			"NavigationSystem",

			// Physics (PhysicalMaterial class)
			"PhysicsCore",

			// UMG / Widget tools
			"UMG",
			"UMGEditor",

			// PCG - Procedural Content Generation (required plugin dependency)
			"PCG",

			// Enhanced Input System (required plugin dependency)
			"EnhancedInput",
			"InputBlueprintNodes",

			// Gameplay Tags
			"GameplayTags",

			// AI / Behavior Trees
			"AIModule",
			"GameplayTasks",

			// AnimGraph tools (anim blueprint factories, blend spaces, state machine graphs)
			"AnimGraphRuntime",
			"AnimGraph",

			// Note: World Partition classes are part of the Engine module.
			// Note: GameplayAbilities is optional — GAS tools use dynamic class lookup.
			// Note: MetaSound tools use dynamic class loading — no compile-time dep needed.
			// Note: Networking/Replication uses Engine module classes — no extra dep needed.
			// Note: Material Graph tools use Engine module material expression classes.
		});

		// Python scripting - soft dependency via interface header
		PrivateIncludePathModuleNames.Add("PythonScriptPlugin");

		// UMGEditor private headers (K2Node_CreateWidget is in private/Nodes/)
		string UMGEditorPrivate = System.IO.Path.Combine(EngineDirectory, "Source", "Editor", "UMGEditor", "Private");
		if (System.IO.Directory.Exists(UMGEditorPrivate))
		{
			PrivateIncludePaths.Add(UMGEditorPrivate);
		}

		bEnableExceptions = false;
		bUseUnity = true;

		PublicDefinitions.Add("WITH_UNREAL_MCP_SERVER=1");
	}
}
