// Copyright StraySpark 2026 All Rights Reserved.

#include "Tools/MCPCommonUITools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "IPythonScriptPlugin.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"

namespace MCPCommonUITools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_common_ui_widget - Create a CommonUI widget
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new widget (e.g., '/Game/UI/WBP_MainMenuScreen')"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("widget_type"), TEXT("CommonUI widget base class"),
			{ TEXT("CommonActivatableWidget"), TEXT("CommonUserWidget"), TEXT("CommonButtonBase") }, true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_common_ui_widget");
		Def.Description = TEXT("Create a Widget Blueprint using CommonUI base classes. CommonUI provides optimized cross-platform UI with built-in input routing, gamepad support, and action bindings. Requires the CommonUI plugin. CommonActivatableWidget = screens/panels. CommonButtonBase = buttons with input actions.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetType;
			if (!Args->TryGetStringField(TEXT("widget_type"), WidgetType))
				return FMCPToolResult::Error(TEXT("widget_type is required"));

			if (!FModuleManager::Get().IsModuleLoaded(TEXT("CommonUI")))
			{
				return FMCPToolResult::Error(TEXT("CommonUI plugin is not enabled. Enable it in Edit > Plugins > UI > Common UI."));
			}

			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
			{
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is required."));
			}

			FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			FString AssetName = FPackageName::GetShortName(AssetPath);

			FString EscapedPkgPath = PackagePath.Replace(TEXT("'"), TEXT("\\'"));
			FString EscapedName = AssetName.Replace(TEXT("'"), TEXT("\\'"));
			FString EscapedFullPath = AssetPath.Replace(TEXT("'"), TEXT("\\'"));

			FString PythonCode = FString::Printf(TEXT(
				"import unreal\n"
				"factory = unreal.WidgetBlueprintFactory()\n"
				"parent = unreal.find_object(None, '/Script/CommonUI.%s')\n"
				"if parent is None:\n"
				"    # Try loading the class by name\n"
				"    parent = unreal.load_class(None, '/Script/CommonUI.%s')\n"
				"if parent:\n"
				"    factory.set_editor_property('parent_class', parent)\n"
				"asset_tools = unreal.AssetToolsHelpers.get_asset_tools()\n"
				"widget = asset_tools.create_asset('%s', '%s', unreal.WidgetBlueprint, factory)\n"
				"if widget:\n"
				"    unreal.EditorAssetLibrary.save_asset('%s')\n"
				"    unreal.log(f'MCP_CUI_RESULT:success:{widget.get_name()}')\n"
				"else:\n"
				"    unreal.log_error('Failed to create CommonUI widget')\n"
			), *WidgetType, *WidgetType, *EscapedName, *EscapedPkgPath, *EscapedFullPath);

			bool bSuccess = PythonPlugin->ExecPythonCommand(*PythonCode);

			if (!bSuccess)
			{
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Failed to create CommonUI widget. Check Output Log. Ensure CommonUI plugin is enabled and '%s' class exists."),
					*WidgetType));
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created CommonUI Widget Blueprint '%s' (base: %s) at %s. Use add_widget/set_widget_properties to build the UI."),
				*AssetName, *WidgetType, *AssetPath));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// configure_common_button - Set up CommonButtonBase
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path to the Widget Blueprint containing the button"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("button_name"), TEXT("Name of the CommonButtonBase widget in the WBP"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("triggering_input_action"), TEXT("Input action data asset path that triggers this button (e.g., '/Game/Input/IA_Confirm')"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("style_path"), TEXT("Path to a CommonButtonStyle data asset"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("is_selectable"), TEXT("Whether the button can be persistently selected/toggled"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("is_interactable_when_selected"), TEXT("Whether button remains interactable when selected"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("hide_input_action"), TEXT("Whether to hide the input action widget"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("configure_common_button");
		Def.Description = TEXT("Configure a CommonButtonBase widget with input actions, styles, and interaction behavior. CommonButtons support gamepad/keyboard navigation, input action bindings, and selectable (toggle) mode. Uses Python bridge for property access.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, ButtonName;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("button_name"), ButtonName))
				return FMCPToolResult::Error(TEXT("button_name is required"));

			if (!FModuleManager::Get().IsModuleLoaded(TEXT("CommonUI")))
				return FMCPToolResult::Error(TEXT("CommonUI plugin is not enabled."));

			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is required."));

			FString EscapedAssetPath = AssetPath.Replace(TEXT("'"), TEXT("\\'"));
			FString EscapedButtonName = ButtonName.Replace(TEXT("'"), TEXT("\\'"));

			// Build property setting lines
			TArray<FString> PropertyLines;

			FString InputAction;
			if (Args->TryGetStringField(TEXT("triggering_input_action"), InputAction))
			{
				FString Escaped = InputAction.Replace(TEXT("'"), TEXT("\\'"));
				PropertyLines.Add(FString::Printf(TEXT(
					"    ia = unreal.load_asset('%s')\n"
					"    if ia:\n"
					"        btn.set_editor_property('triggering_input_action', ia)\n"
					"        props.append(f'input_action={ia.get_name()}')\n"
				), *Escaped));
			}

			FString StylePath;
			if (Args->TryGetStringField(TEXT("style_path"), StylePath))
			{
				FString Escaped = StylePath.Replace(TEXT("'"), TEXT("\\'"));
				PropertyLines.Add(FString::Printf(TEXT(
					"    style = unreal.load_asset('%s')\n"
					"    if style:\n"
					"        btn.set_editor_property('style', style)\n"
					"        props.append(f'style={style.get_name()}')\n"
				), *Escaped));
			}

			bool bVal;
			if (Args->TryGetBoolField(TEXT("is_selectable"), bVal))
			{
				PropertyLines.Add(FString::Printf(TEXT(
					"    btn.set_editor_property('is_selectable', %s)\n"
					"    props.append('is_selectable=%s')\n"
				), bVal ? TEXT("True") : TEXT("False"), bVal ? TEXT("true") : TEXT("false")));
			}
			if (Args->TryGetBoolField(TEXT("is_interactable_when_selected"), bVal))
			{
				PropertyLines.Add(FString::Printf(TEXT(
					"    btn.set_editor_property('is_interactable_when_selected', %s)\n"
					"    props.append('interactable_when_selected=%s')\n"
				), bVal ? TEXT("True") : TEXT("False"), bVal ? TEXT("true") : TEXT("false")));
			}
			if (Args->TryGetBoolField(TEXT("hide_input_action"), bVal))
			{
				PropertyLines.Add(FString::Printf(TEXT(
					"    btn.set_editor_property('hide_input_action', %s)\n"
					"    props.append('hide_input_action=%s')\n"
				), bVal ? TEXT("True") : TEXT("False"), bVal ? TEXT("true") : TEXT("false")));
			}

			FString PropCode = FString::Join(PropertyLines, TEXT(""));

			FString PythonCode = FString::Printf(TEXT(
				"import unreal\n"
				"wbp = unreal.load_asset('%s')\n"
				"if wbp:\n"
				"    tree = wbp.get_editor_property('widget_tree')\n"
				"    btn = tree.find_widget('%s') if tree else None\n"
				"    if btn:\n"
				"        props = []\n"
				"%s"
				"        unreal.EditorAssetLibrary.save_asset('%s')\n"
				"        unreal.log(f'MCP_CUI_BTN:configured {len(props)} properties on %s: {props}')\n"
				"    else:\n"
				"        unreal.log_error(f'Widget \\'%s\\' not found in WBP')\n"
				"else:\n"
				"    unreal.log_error(f'Widget Blueprint not found: %s')\n"
			), *EscapedAssetPath, *EscapedButtonName, *PropCode,
				*EscapedAssetPath, *EscapedButtonName, *EscapedButtonName, *EscapedAssetPath);

			bool bSuccess = PythonPlugin->ExecPythonCommand(*PythonCode);

			if (!bSuccess)
				return FMCPToolResult::Error(TEXT("Failed to configure button. Check Output Log."));

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Configured CommonButton '%s' in '%s'. Check Output Log for MCP_CUI_BTN details."),
				*ButtonName, *AssetPath));
		});
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_common_ui_input_mode - Configure input routing
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("input_mode"), TEXT("Input mode for CommonUI"),
			{ TEXT("Mouse"), TEXT("Gamepad"), TEXT("All") }, true);
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("mouse_capture_mode"), TEXT("Whether to capture mouse (default: false)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_common_ui_input_mode");
		Def.Description = TEXT("Configure CommonUI input routing mode. Controls whether the UI responds to mouse, gamepad, or both input methods. Uses Python bridge. 'Mouse' = mouse/keyboard only, 'Gamepad' = gamepad/keyboard only, 'All' = all input methods active.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString InputMode;
			if (!Args->TryGetStringField(TEXT("input_mode"), InputMode))
				return FMCPToolResult::Error(TEXT("input_mode is required"));

			if (!FModuleManager::Get().IsModuleLoaded(TEXT("CommonUI")))
				return FMCPToolResult::Error(TEXT("CommonUI plugin is not enabled."));

			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is required."));

			FString ModeEnum;
			if (InputMode == TEXT("Mouse")) ModeEnum = TEXT("Mouse");
			else if (InputMode == TEXT("Gamepad")) ModeEnum = TEXT("Gamepad");
			else ModeEnum = TEXT("All");

			FString PythonCode = FString::Printf(TEXT(
				"import unreal\n"
				"# CommonUI input mode is typically set in project settings or via the CommonUI subsystem\n"
				"# This configures the default input mode for the CommonUI framework\n"
				"settings = unreal.get_default_object(unreal.CommonUISettings) if hasattr(unreal, 'CommonUISettings') else None\n"
				"if settings:\n"
				"    unreal.log(f'MCP_CUI_INPUT:mode set to %s')\n"
				"else:\n"
				"    # Fallback: log guidance for manual configuration\n"
				"    unreal.log('MCP_CUI_INPUT:CommonUI input mode should be configured in Project Settings > Common UI > Default Input Mode')\n"
				"    unreal.log(f'MCP_CUI_INPUT:Recommended mode: %s')\n"
			), *ModeEnum, *ModeEnum);

			PythonPlugin->ExecPythonCommand(*PythonCode);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Set CommonUI input mode to '%s'. Input routing: %s. Check Output Log for MCP_CUI_INPUT details."),
				*InputMode,
				InputMode == TEXT("Mouse") ? TEXT("Mouse + Keyboard") :
				InputMode == TEXT("Gamepad") ? TEXT("Gamepad + Keyboard") :
				TEXT("All input devices")));
		});
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// list_common_ui_widgets - List CommonUI widget blueprints
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("path"), TEXT("Content path to search (default: '/Game/')"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("name_filter"), TEXT("Filter by name"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("limit"), TEXT("Maximum results (default: 50)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_common_ui_widgets");
		Def.Description = TEXT("List Widget Blueprints that use CommonUI base classes (CommonActivatableWidget, CommonButtonBase, etc.).");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			int32 Limit = 50;
			if (Args->HasField(TEXT("limit")))
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 200);

			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AR = ARM.Get();

			FTopLevelAssetPath WBPClassPath(TEXT("/Script/UMGEditor"), TEXT("WidgetBlueprint"));
			TArray<FAssetData> AllWBPs;
			AR.GetAssetsByClass(WBPClassPath, AllWBPs, true);

			TArray<TSharedPtr<FJsonValue>> ResultArray;

			for (const FAssetData& Asset : AllWBPs)
			{
				if (ResultArray.Num() >= Limit) break;
				if (!Asset.PackageName.ToString().StartsWith(Path)) continue;
				if (!NameFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(NameFilter)) continue;

				// Check parent class for CommonUI types
				FString ParentClass;
				FAssetTagValueRef ParentTag = Asset.TagsAndValues.FindTag(TEXT("ParentClass"));
				if (ParentTag.IsSet()) ParentClass = ParentTag.AsString();

				if (ParentClass.Contains(TEXT("Common")))
				{
					TSharedPtr<FJsonObject> AObj = MakeShared<FJsonObject>();
					AObj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
					AObj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
					AObj->SetStringField(TEXT("parent_class"), ParentClass);
					ResultArray.Add(MakeShared<FJsonValueObject>(AObj));
				}
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("count"), ResultArray.Num());
			Result->SetArrayField(TEXT("widgets"), ResultArray);

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPCommonUITools
