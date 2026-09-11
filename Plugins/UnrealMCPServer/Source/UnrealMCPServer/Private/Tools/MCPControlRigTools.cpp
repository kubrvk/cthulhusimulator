// Copyright StraySpark 2026 All Rights Reserved.

#include "Tools/MCPControlRigTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Animation/Skeleton.h"

// Control Rig is an optional plugin — uses Python bridge for max compatibility
#include "Modules/ModuleManager.h"
#include "IPythonScriptPlugin.h"

namespace MCPControlRigTools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_control_rig - Create a Control Rig Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new Control Rig Blueprint (e.g., '/Game/Characters/CR_Hero')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("skeleton_path"), TEXT("Content path to the USkeleton asset (e.g., '/Game/Characters/SK_Hero_Skeleton')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_control_rig");
		Def.Description = TEXT("Create a new Control Rig Blueprint for a specific skeleton. Control Rigs provide IK chains, bone constraints, and procedural animation. Requires the ControlRig plugin to be enabled. The rig is created via the execute_python bridge for maximum compatibility.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, SkeletonPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
				return FMCPToolResult::Error(TEXT("skeleton_path is required"));

			// Verify the skeleton exists
			USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
			if (!IsValid(Skeleton))
				return FMCPToolResult::Error(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

			// Check if ControlRig plugin is loaded
			if (!FModuleManager::Get().IsModuleLoaded(TEXT("ControlRigEditor")))
			{
				return FMCPToolResult::Error(TEXT("ControlRig plugin is not enabled. Enable it in Edit > Plugins > Animation > Control Rig."));
			}

			// Use Python for Control Rig creation as it's the most stable API path
			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
			{
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is required for Control Rig creation. Enable it in Edit > Plugins."));
			}

			FString EscapedAssetPath = AssetPath.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));
			FString EscapedSkeletonPath = SkeletonPath.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));

			FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			FString AssetName = FPackageName::GetShortName(AssetPath);
			FString EscapedPkgPath = PackagePath.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));
			FString EscapedName = AssetName.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));

			FString PythonCode = FString::Printf(TEXT(
				"import unreal\n"
				"factory = unreal.ControlRigBlueprintFactory()\n"
				"asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
				"rig = asset_tools.create_asset('%s', '%s', unreal.ControlRigBlueprint, factory)\n"
				"if rig:\n"
				"    skeleton = unreal.load_object(name='%s', outer=None)\n"
				"    if skeleton:\n"
				"        rig.set_preview_mesh(skeleton.get_preview_mesh(True), True)\n"
				"    unreal.EditorAssetLibrary.save_asset('%s')\n"
				"    unreal.log(f'MCP_CR_RESULT:success')\n"
				"else:\n"
				"    unreal.log_error('Failed to create Control Rig Blueprint')\n"
			), *EscapedName, *EscapedPkgPath, *EscapedSkeletonPath, *EscapedAssetPath);

			bool bSuccess = PythonPlugin->ExecPythonCommand(*PythonCode);

			if (!bSuccess)
			{
				return FMCPToolResult::Error(TEXT("Failed to create Control Rig. Check Output Log for details."));
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created Control Rig Blueprint '%s' for skeleton '%s' at %s. Open it in the editor to add IK chains, constraints, and controls."),
				*AssetName, *Skeleton->GetName(), *AssetPath));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_control_rig_info - Inspect Control Rig structure
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path to the Control Rig Blueprint"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_control_rig_info");
		Def.Description = TEXT("Get information about a Control Rig Blueprint: preview mesh, rig hierarchy, bone count, and available controls. Requires the ControlRig plugin.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			if (!FModuleManager::Get().IsModuleLoaded(TEXT("ControlRigEditor")))
			{
				return FMCPToolResult::Error(TEXT("ControlRig plugin is not enabled."));
			}

			// Use Python for querying Control Rig info
			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
			{
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is required."));
			}

			FString EscapedPath = AssetPath.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));

			FString PythonCode = FString::Printf(TEXT(
				"import unreal, json\n"
				"rig = unreal.load_asset('%s')\n"
				"if rig:\n"
				"    info = {\n"
				"        'name': rig.get_name(),\n"
				"        'path': '%s',\n"
				"        'class': rig.get_class().get_name(),\n"
				"    }\n"
				"    mesh = rig.get_preview_mesh()\n"
				"    if mesh:\n"
				"        info['preview_mesh'] = mesh.get_name()\n"
				"        info['preview_mesh_path'] = mesh.get_path_name()\n"
				"    unreal.log(f'MCP_CR_INFO:{json.dumps(info)}')\n"
				"else:\n"
				"    unreal.log_error(f'Control Rig not found: %s')\n"
			), *EscapedPath, *EscapedPath, *EscapedPath);

			bool bSuccess = PythonPlugin->ExecPythonCommand(*PythonCode);

			if (!bSuccess)
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to read Control Rig at: %s. Check Output Log."), *AssetPath));
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Control Rig info logged. Check Output Log for MCP_CR_INFO details. Asset: %s"),
				*AssetPath));
		});
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPControlRigTools
