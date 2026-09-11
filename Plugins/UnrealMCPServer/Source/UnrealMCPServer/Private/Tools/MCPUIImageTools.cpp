// Copyright StraySpark 2026 All Rights Reserved.

#include "Tools/MCPUIImageTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPSettings.h"

#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "IPythonScriptPlugin.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace MCPUIImageTools
{

static FString GetGeneratedImagesDir()
{
	FString Dir = FPaths::ProjectSavedDir() / TEXT("MCPGeneratedImages");
	IFileManager::Get().MakeDirectory(*Dir, true);
	return Dir;
}

// Build Python code that uses runpy to properly import fal_image_gen and call a function.
// This avoids nested exec() scope issues that break function visibility.
static FString BuildPythonCall(const FString& ScriptPath, const FString& FunctionCall)
{
	FString EscapedPath = ScriptPath;
	EscapedPath.ReplaceInline(TEXT("\\"), TEXT("/"));

	return FString::Printf(TEXT(
		"import runpy, os, json, unreal\n"
		"_script = '%s'\n"
		"if not os.path.exists(_script):\n"
		"    unreal.log_error(f'fal_image_gen.py not found at: {_script}')\n"
		"else:\n"
		"    _mod = runpy.run_path(_script)\n"
		"    _fn_generate = _mod.get('generate')\n"
		"    _fn_remove_bg = _mod.get('remove_bg')\n"
		"    _result = %s\n"
		"    unreal.log(f'MCP_FAL_RESULT:{json.dumps(_result)}')\n"
	), *EscapedPath, *FunctionCall);
}

// Escape a string for safe embedding in a Python single-quoted string literal
static FString PyEscape(const FString& S)
{
	FString Out = S;
	Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Out.ReplaceInline(TEXT("'"), TEXT("\\'"));
	Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	return Out;
}

static FString FindScriptPath()
{
	// Try plugin directory first
	FString Path = FPaths::ProjectPluginsDir() / TEXT("UnrealMCPServer/Content/Python/fal_image_gen.py");
	if (FPaths::FileExists(Path)) return Path;

	// Try engine plugins
	Path = FPaths::EnginePluginsDir() / TEXT("Marketplace/UnrealMCPServer/Content/Python/fal_image_gen.py");
	if (FPaths::FileExists(Path)) return Path;

	// Try project plugins marketplace
	Path = FPaths::ProjectPluginsDir() / TEXT("Marketplace/UnrealMCPServer/Content/Python/fal_image_gen.py");
	if (FPaths::FileExists(Path)) return Path;

	// Return default and let Python handle the error
	return FPaths::ProjectPluginsDir() / TEXT("UnrealMCPServer/Content/Python/fal_image_gen.py");
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// generate_ui_image - Generate AI image via fal.ai and import
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("prompt"), TEXT("Image description prompt (e.g., 'sci-fi health bar icon, glowing cyan')"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("destination_path"), TEXT("Content path destination for imported texture (default: '/Game/UI/Generated/')"));
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("model"), TEXT("fal.ai model to use (default: flux-2-flash). flux-2-flash is fastest/cheapest with transparency support. nano-banana-2 for concept art. flux-pro for highest quality."),
			{ TEXT("flux-2-flash"), TEXT("nano-banana-2"), TEXT("nano-banana"), TEXT("flux-dev"), TEXT("flux-pro") });
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("style_preset"), TEXT("Style preset that appends style keywords to the prompt"),
			{ TEXT("ui_icon"), TEXT("ui_background"), TEXT("ui_button"), TEXT("ui_frame"), TEXT("ui_portrait"), TEXT("custom") });
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("image_size"), TEXT("Output image aspect ratio"),
			{ TEXT("square"), TEXT("square_hd"), TEXT("landscape_4_3"), TEXT("landscape_16_9"), TEXT("portrait_4_3"), TEXT("portrait_16_9") });
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("remove_background"), TEXT("Remove background using fal-ai/birefnet/v2 after generation (default: false)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("image_name"), TEXT("Override the generated texture asset name (auto-generated if omitted)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("generate_ui_image");
		Def.Description = TEXT("Generate an AI image using fal.ai and import it into the UE content browser as a texture. Models: flux-2-flash (fastest, cheapest, supports transparency), nano-banana-2 (concept art), flux-pro (highest quality). Supports style presets and optional background removal via birefnet/v2. Requires fal.ai API key in Project Settings and PythonScriptPlugin.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			const UMCPSettings* Settings = UMCPSettings::Get();

			FString ApiKey = Settings->FalAIApiKey;
			if (ApiKey.IsEmpty())
			{
				return FMCPToolResult::Error(TEXT("fal.ai API key is not configured. Set it in Project Settings > Plugins > Unreal MCP Server > AI Image Generation."));
			}

			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
			{
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is not loaded. Enable it in Edit > Plugins."));
			}

			FString Prompt;
			if (!Args->TryGetStringField(TEXT("prompt"), Prompt))
				return FMCPToolResult::Error(TEXT("prompt is required"));

			FString DestPath = TEXT("/Game/UI/Generated/");
			Args->TryGetStringField(TEXT("destination_path"), DestPath);

			FString Model = Settings->DefaultFalModel.IsEmpty() ? TEXT("flux-2-flash") : Settings->DefaultFalModel;
			Args->TryGetStringField(TEXT("model"), Model);

			FString StylePreset = TEXT("custom");
			Args->TryGetStringField(TEXT("style_preset"), StylePreset);

			FString ImageSize = TEXT("square_hd");
			Args->TryGetStringField(TEXT("image_size"), ImageSize);

			bool bRemoveBG = false;
			Args->TryGetBoolField(TEXT("remove_background"), bRemoveBG);

			FString ImageName;
			Args->TryGetStringField(TEXT("image_name"), ImageName);

			FString OutputDir = GetGeneratedImagesDir();
			FString ScriptPath = FindScriptPath();

			// Build the Python function call string
			FString FnCall = FString::Printf(TEXT(
				"_fn_generate(\n"
				"        prompt='%s',\n"
				"        api_key='%s',\n"
				"        output_dir='%s',\n"
				"        model='%s',\n"
				"        style_preset='%s',\n"
				"        image_size='%s',\n"
				"        image_name=%s,\n"
				"        remove_background=%s,\n"
				"    )"
			),
				*PyEscape(Prompt),
				*PyEscape(ApiKey),
				*PyEscape(OutputDir),
				*Model,
				*StylePreset,
				*ImageSize,
				ImageName.IsEmpty() ? TEXT("None") : *FString::Printf(TEXT("'%s'"), *PyEscape(ImageName)),
				bRemoveBG ? TEXT("True") : TEXT("False")
			);

			FString PythonCode = BuildPythonCall(ScriptPath, FnCall);
			bool bSuccess = PythonPlugin->ExecPythonCommand(*PythonCode);

			if (!bSuccess)
			{
				return FMCPToolResult::Error(TEXT("Python execution failed. Check Output Log for details. Ensure fal_image_gen.py exists and PythonScriptPlugin is enabled."));
			}

			// Find the most recently modified PNG in output dir
			TArray<FString> GeneratedFiles;
			IFileManager::Get().FindFiles(GeneratedFiles, *(OutputDir / TEXT("*.png")), true, false);

			if (GeneratedFiles.Num() == 0)
			{
				return FMCPToolResult::Error(TEXT("Image generation may have failed - no PNG files found in output directory. Check Output Log for fal.ai API errors."));
			}

			FString LatestFile;
			FDateTime LatestTime = FDateTime::MinValue();
			for (const FString& File : GeneratedFiles)
			{
				FString FullPath = OutputDir / File;
				FDateTime ModTime = IFileManager::Get().GetTimeStamp(*FullPath);
				if (ModTime > LatestTime)
				{
					LatestTime = ModTime;
					LatestFile = FullPath;
				}
			}

			if (LatestFile.IsEmpty())
			{
				return FMCPToolResult::Error(TEXT("Could not find generated image file"));
			}

			// Import into UE content browser
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			TArray<FString> FilesToImport;
			FilesToImport.Add(LatestFile);

			TArray<UObject*> ImportedAssets = AssetTools.ImportAssets(FilesToImport, DestPath);

			if (ImportedAssets.Num() == 0)
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to import generated image from: %s"), *LatestFile));
			}

			UObject* ImportedAsset = ImportedAssets[0];
			FString ContentPath = ImportedAsset->GetPathName();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Generated and imported AI image. Texture: %s (model: %s, style: %s%s)"),
				*ContentPath, *Model, *StylePreset,
				bRemoveBG ? TEXT(", background removed") : TEXT("")));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// remove_background - Remove background from image using birefnet
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("image_url"), TEXT("URL of the image to remove background from (must be publicly accessible)"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("destination_path"), TEXT("Content path destination for imported texture (default: '/Game/UI/Generated/')"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("image_name"), TEXT("Name for the output texture asset"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("remove_background");
		Def.Description = TEXT("Remove background from an image using fal-ai/birefnet/v2. Provide a publicly accessible image URL. The result transparent PNG is imported into the UE content browser. Requires fal.ai API key and PythonScriptPlugin.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			const UMCPSettings* Settings = UMCPSettings::Get();

			FString ApiKey = Settings->FalAIApiKey;
			if (ApiKey.IsEmpty())
			{
				return FMCPToolResult::Error(TEXT("fal.ai API key is not configured. Set it in Project Settings > Plugins > Unreal MCP Server > AI Image Generation."));
			}

			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
			{
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is not loaded. Enable it in Edit > Plugins."));
			}

			FString ImageUrl;
			if (!Args->TryGetStringField(TEXT("image_url"), ImageUrl))
				return FMCPToolResult::Error(TEXT("image_url is required"));

			FString DestPath = TEXT("/Game/UI/Generated/");
			Args->TryGetStringField(TEXT("destination_path"), DestPath);

			FString ImageName;
			Args->TryGetStringField(TEXT("image_name"), ImageName);

			FString OutputDir = GetGeneratedImagesDir();
			FString ScriptPath = FindScriptPath();

			FString FnCall = FString::Printf(TEXT(
				"_fn_remove_bg(\n"
				"        image_url='%s',\n"
				"        api_key='%s',\n"
				"        output_dir='%s',\n"
				"        image_name=%s,\n"
				"    )"
			),
				*PyEscape(ImageUrl),
				*PyEscape(ApiKey),
				*PyEscape(OutputDir),
				ImageName.IsEmpty() ? TEXT("None") : *FString::Printf(TEXT("'%s'"), *PyEscape(ImageName))
			);

			FString PythonCode = BuildPythonCall(ScriptPath, FnCall);
			bool bSuccess = PythonPlugin->ExecPythonCommand(*PythonCode);

			if (!bSuccess)
			{
				return FMCPToolResult::Error(TEXT("Python execution failed. Check Output Log for details."));
			}

			// Find most recently created file
			TArray<FString> GeneratedFiles;
			IFileManager::Get().FindFiles(GeneratedFiles, *(OutputDir / TEXT("*.png")), true, false);

			FString LatestFile;
			FDateTime LatestTime = FDateTime::MinValue();
			for (const FString& File : GeneratedFiles)
			{
				FString FullPath = OutputDir / File;
				FDateTime ModTime = IFileManager::Get().GetTimeStamp(*FullPath);
				if (ModTime > LatestTime)
				{
					LatestTime = ModTime;
					LatestFile = FullPath;
				}
			}

			if (LatestFile.IsEmpty())
			{
				return FMCPToolResult::Error(TEXT("Background removal may have failed - no output file found. Check Output Log."));
			}

			// Import
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			TArray<FString> FilesToImport;
			FilesToImport.Add(LatestFile);

			TArray<UObject*> ImportedAssets = AssetTools.ImportAssets(FilesToImport, DestPath);

			if (ImportedAssets.Num() == 0)
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to import result image from: %s"), *LatestFile));
			}

			FString ContentPath = ImportedAssets[0]->GetPathName();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Background removed. Transparent texture imported: %s"), *ContentPath));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPUIImageTools
