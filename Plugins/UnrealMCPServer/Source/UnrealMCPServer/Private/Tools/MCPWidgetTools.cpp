// Copyright StraySpark 2026 All Rights Reserved.

#include "Tools/MCPWidgetTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/UserWidget.h"
#include "UObject/SavePackage.h"

// UMG Widget Blueprint editing
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetToolsModule.h"
#include "Misc/PackageName.h"
#include "Engine/Texture2D.h"

// Blueprint event graph (for bind_widget_event)
#include "K2Node_ComponentBoundEvent.h"
#include "EdGraphSchema_K2.h"

// UMG Widget types
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/GridPanel.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/Border.h"
#include "Components/WrapBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/EditableTextBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Spacer.h"
#include "Components/RichTextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/PanelWidget.h"

namespace MCPWidgetTools
{

// ============================================================================
// Helper functions
// ============================================================================

static UWorld* GetEditorWorld()
{
	if (GEditor) return GEditor->GetEditorWorldContext().World();
	return nullptr;
}

static AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == Label) return *It;
	}
	return nullptr;
}

static UWidgetBlueprint* FindWidgetBlueprint(const FString& AssetPath)
{
	return LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
}

static UWidget* FindWidgetByName(UWidgetBlueprint* WBP, const FString& WidgetName)
{
	if (!WBP || !WBP->WidgetTree) return nullptr;
	return WBP->WidgetTree->FindWidget(FName(*WidgetName));
}

static UClass* WidgetClassFromTypeName(const FString& TypeName)
{
	static TMap<FString, UClass*> ClassMap;
	if (ClassMap.Num() == 0)
	{
		ClassMap.Add(TEXT("CanvasPanel"), UCanvasPanel::StaticClass());
		ClassMap.Add(TEXT("VerticalBox"), UVerticalBox::StaticClass());
		ClassMap.Add(TEXT("HorizontalBox"), UHorizontalBox::StaticClass());
		ClassMap.Add(TEXT("GridPanel"), UGridPanel::StaticClass());
		ClassMap.Add(TEXT("Overlay"), UOverlay::StaticClass());
		ClassMap.Add(TEXT("SizeBox"), USizeBox::StaticClass());
		ClassMap.Add(TEXT("ScaleBox"), UScaleBox::StaticClass());
		ClassMap.Add(TEXT("Border"), UBorder::StaticClass());
		ClassMap.Add(TEXT("WrapBox"), UWrapBox::StaticClass());
		ClassMap.Add(TEXT("UniformGridPanel"), UUniformGridPanel::StaticClass());
		ClassMap.Add(TEXT("ScrollBox"), UScrollBox::StaticClass());
		ClassMap.Add(TEXT("Button"), UButton::StaticClass());
		ClassMap.Add(TEXT("TextBlock"), UTextBlock::StaticClass());
		ClassMap.Add(TEXT("Image"), UImage::StaticClass());
		ClassMap.Add(TEXT("EditableTextBox"), UEditableTextBox::StaticClass());
		ClassMap.Add(TEXT("Slider"), USlider::StaticClass());
		ClassMap.Add(TEXT("ProgressBar"), UProgressBar::StaticClass());
		ClassMap.Add(TEXT("CheckBox"), UCheckBox::StaticClass());
		ClassMap.Add(TEXT("ComboBoxString"), UComboBoxString::StaticClass());
		ClassMap.Add(TEXT("Spacer"), USpacer::StaticClass());
		ClassMap.Add(TEXT("RichTextBlock"), URichTextBlock::StaticClass());
	}
	if (UClass** Found = ClassMap.Find(TypeName))
	{
		return *Found;
	}
	return nullptr;
}

static void SaveWidgetBlueprint(UWidgetBlueprint* WBP)
{
	FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
	FKismetEditorUtilities::CompileBlueprint(WBP);

	FAssetRegistryModule::AssetCreated(WBP);
	WBP->GetPackage()->MarkPackageDirty();

	FString PackageFilename = FPackageName::LongPackageNameToFilename(
		WBP->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(WBP->GetPackage(), WBP, *PackageFilename, SaveArgs);
}

static TSharedPtr<FJsonObject> SerializeWidget(UWidget* Widget, bool bIncludeProperties)
{
	if (!Widget) return nullptr;

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("name"), Widget->GetName());
	Obj->SetStringField(TEXT("type"), Widget->GetClass()->GetName());

	if (bIncludeProperties)
	{
		Obj->SetStringField(TEXT("visibility"), UEnum::GetValueAsString(Widget->GetVisibility()));
		Obj->SetBoolField(TEXT("is_enabled"), Widget->GetIsEnabled());
		Obj->SetNumberField(TEXT("render_opacity"), Widget->GetRenderOpacity());

		if (UTextBlock* TB = Cast<UTextBlock>(Widget))
		{
			Obj->SetStringField(TEXT("text"), TB->GetText().ToString());
		}
		else if (UProgressBar* PB = Cast<UProgressBar>(Widget))
		{
			Obj->SetNumberField(TEXT("percent"), PB->GetPercent());
		}

		// Include slot info if widget has a parent
		if (Widget->Slot)
		{
			if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
				SlotObj->SetStringField(TEXT("type"), TEXT("CanvasPanelSlot"));
				FAnchors Anchors = CS->GetAnchors();
				SlotObj->SetNumberField(TEXT("anchor_min_x"), Anchors.Minimum.X);
				SlotObj->SetNumberField(TEXT("anchor_min_y"), Anchors.Minimum.Y);
				SlotObj->SetNumberField(TEXT("anchor_max_x"), Anchors.Maximum.X);
				SlotObj->SetNumberField(TEXT("anchor_max_y"), Anchors.Maximum.Y);
				FMargin Offsets = CS->GetOffsets();
				SlotObj->SetNumberField(TEXT("offset_left"), Offsets.Left);
				SlotObj->SetNumberField(TEXT("offset_top"), Offsets.Top);
				SlotObj->SetNumberField(TEXT("offset_right"), Offsets.Right);
				SlotObj->SetNumberField(TEXT("offset_bottom"), Offsets.Bottom);
				Obj->SetObjectField(TEXT("slot"), SlotObj);
			}
		}
	}

	// Recurse into children if this is a panel widget
	UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
	if (Panel)
	{
		TArray<TSharedPtr<FJsonValue>> ChildArray;
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			UWidget* Child = Panel->GetChildAt(i);
			TSharedPtr<FJsonObject> ChildObj = SerializeWidget(Child, bIncludeProperties);
			if (ChildObj)
			{
				ChildArray.Add(MakeShared<FJsonValueObject>(ChildObj));
			}
		}
		if (ChildArray.Num() > 0)
		{
			Obj->SetArrayField(TEXT("children"), ChildArray);
		}
	}

	return Obj;
}

// Note: JsonToString() is already defined in MCPProtocol.h and available via ADL

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// list_widget_blueprints - List all Widget Blueprint assets
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("path"), TEXT("Content path to search (e.g., '/Game/'). Default: '/Game/'"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("name_filter"), TEXT("Filter by asset name (substring match)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("limit"), TEXT("Maximum number of results (default: 100)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_widget_blueprints");
		Def.Description = TEXT("List all Widget Blueprint (UMG) assets in the project. Returns asset name, content path, and parent class for each widget.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 5000);
			}

			FTopLevelAssetPath WidgetBPClassPath(TEXT("/Script/UMGEditor"), TEXT("WidgetBlueprint"));
			TArray<FAssetData> Assets;
			AssetRegistry.GetAssetsByClass(WidgetBPClassPath, Assets, true);

			TArray<FString> Results;
			int32 TotalMatching = 0;

			for (const FAssetData& Asset : Assets)
			{
				if (!Asset.PackageName.ToString().StartsWith(Path)) continue;
				if (!NameFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(NameFilter)) continue;

				TotalMatching++;
				if (Results.Num() < Limit)
				{
					FString ParentClass = TEXT("UserWidget");
					FAssetTagValueRef ParentClassTag = Asset.TagsAndValues.FindTag(TEXT("ParentClass"));
					if (ParentClassTag.IsSet())
					{
						ParentClass = ParentClassTag.AsString();
					}

					TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
					Obj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
					Obj->SetStringField(TEXT("package"), Asset.PackageName.ToString());
					Obj->SetStringField(TEXT("parent_class"), ParentClass);

					Results.Add(JsonToString(Obj));
				}
			}

			if (Results.Num() == 0)
			{
				return FMCPToolResult::Success(FString::Printf(
					TEXT("No Widget Blueprints found in '%s'%s"),
					*Path,
					NameFilter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" matching '%s'"), *NameFilter)));
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Found %d Widget Blueprint(s) (showing %d):\n[%s]"),
				TotalMatching, Results.Num(), *FString::Join(Results, TEXT(",\n"))));
		});
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// spawn_widget_component - Add a WidgetComponent to an existing actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the target actor to add the WidgetComponent to"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("widget_class_path"), TEXT("Optional content path to a Widget Blueprint asset (e.g., '/Game/UI/WBP_HUD.WBP_HUD')"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("draw_size_x"), TEXT("Width of the widget in world units (default: 500)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("draw_size_y"), TEXT("Height of the widget in world units (default: 500)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("relative_x"), TEXT("X offset from actor root (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("relative_y"), TEXT("Y offset from actor root (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("relative_z"), TEXT("Z offset from actor root (default: 0)"));
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("space"), TEXT("Render space for the widget. 'World' renders in 3D world space, 'Screen' renders as a screen-space overlay. Default: 'World'"),
			{ TEXT("World"), TEXT("Screen") });

		FMCPToolDefinition Def;
		Def.Name = TEXT("spawn_widget_component");
		Def.Description = TEXT("Add a WidgetComponent to an existing actor for in-world UI display. The component can render a Widget Blueprint in 3D world space or screen space. Returns the component name on success.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			float DrawSizeX = 500.0f;
			float DrawSizeY = 500.0f;
			if (Args->HasField(TEXT("draw_size_x"))) DrawSizeX = (float)Args->GetNumberField(TEXT("draw_size_x"));
			if (Args->HasField(TEXT("draw_size_y"))) DrawSizeY = (float)Args->GetNumberField(TEXT("draw_size_y"));

			FVector RelativeOffset(
				Args->HasField(TEXT("relative_x")) ? Args->GetNumberField(TEXT("relative_x")) : 0.0,
				Args->HasField(TEXT("relative_y")) ? Args->GetNumberField(TEXT("relative_y")) : 0.0,
				Args->HasField(TEXT("relative_z")) ? Args->GetNumberField(TEXT("relative_z")) : 0.0
			);

			FString SpaceStr;
			Args->TryGetStringField(TEXT("space"), SpaceStr);
			EWidgetSpace WidgetSpace = EWidgetSpace::World;
			if (SpaceStr == TEXT("Screen")) WidgetSpace = EWidgetSpace::Screen;

			TSubclassOf<UUserWidget> WidgetClass = nullptr;
			FString WidgetClassPath;
			if (Args->TryGetStringField(TEXT("widget_class_path"), WidgetClassPath) && !WidgetClassPath.IsEmpty())
			{
				UBlueprint* WidgetBP = LoadObject<UBlueprint>(nullptr, *WidgetClassPath);
				UClass* SpawnGenClass = WidgetBP ? WidgetBP->GeneratedClass.Get() : nullptr;
				if (SpawnGenClass && SpawnGenClass->IsChildOf(UUserWidget::StaticClass()))
				{
					WidgetClass = TSubclassOf<UUserWidget>(SpawnGenClass);
				}
				else
				{
					FString GeneratedClassPath = WidgetClassPath + TEXT("_C");
					UClass* LoadedClass = LoadObject<UClass>(nullptr, *GeneratedClassPath);
					if (LoadedClass && LoadedClass->IsChildOf(UUserWidget::StaticClass()))
					{
						WidgetClass = LoadedClass;
					}
					else
					{
						return FMCPToolResult::Error(FString::Printf(
							TEXT("Could not load Widget Blueprint class from: %s"), *WidgetClassPath));
					}
				}
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Spawn Widget Component")));
			Actor->Modify();

			UWidgetComponent* WidgetComp = NewObject<UWidgetComponent>(Actor, UWidgetComponent::StaticClass(),
				MakeUniqueObjectName(Actor, UWidgetComponent::StaticClass(), TEXT("WidgetComponent")),
				RF_Transactional);

			if (!WidgetComp)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to create WidgetComponent"));
			}

			WidgetComp->SetWidgetSpace(WidgetSpace);
			WidgetComp->SetDrawSize(FVector2D(DrawSizeX, DrawSizeY));

			if (WidgetClass)
			{
				WidgetComp->SetWidgetClass(WidgetClass);
			}

			WidgetComp->RegisterComponent();
			WidgetComp->AttachToComponent(
				Actor->GetRootComponent(),
				FAttachmentTransformRules::KeepRelativeTransform);
			WidgetComp->SetRelativeLocation(RelativeOffset);

			Actor->AddInstanceComponent(WidgetComp);
			Actor->RerunConstructionScripts();

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Added WidgetComponent '%s' to actor '%s'. DrawSize=(%.0f x %.0f), Space=%s%s"),
				*WidgetComp->GetName(),
				*ActorName,
				DrawSizeX,
				DrawSizeY,
				WidgetSpace == EWidgetSpace::World ? TEXT("World") : TEXT("Screen"),
				WidgetClass ? *FString::Printf(TEXT(", WidgetClass=%s"), *WidgetClass->GetName()) : TEXT("")));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_widget_component_property - Set properties on a WidgetComponent
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor that owns the WidgetComponent"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("draw_size_x"), TEXT("New draw width in world units"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("draw_size_y"), TEXT("New draw height in world units"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("widget_class_path"), TEXT("Content path to a Widget Blueprint to assign as the widget class (e.g., '/Game/UI/WBP_HUD.WBP_HUD')"));
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("space"), TEXT("Render space: 'World' for 3D world space, 'Screen' for screen-space overlay"),
			{ TEXT("World"), TEXT("Screen") });
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("tint_r"), TEXT("Tint color red channel (0.0 - 1.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("tint_g"), TEXT("Tint color green channel (0.0 - 1.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("tint_b"), TEXT("Tint color blue channel (0.0 - 1.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("tint_a"), TEXT("Tint color alpha channel (0.0 - 1.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("max_interaction_distance"), TEXT("Maximum distance at which the player can interact with this widget (in world units)"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("is_two_sided"), TEXT("Whether the widget geometry is rendered two-sided"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_widget_component_property");
		Def.Description = TEXT("Set one or more properties on the first WidgetComponent found on an actor. Only provided fields are modified. Supports draw size, widget class, render space, tint color, interaction distance, and two-sided rendering.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			UWidgetComponent* WidgetComp = Actor->FindComponentByClass<UWidgetComponent>();
			if (!WidgetComp)
				return FMCPToolResult::Error(FString::Printf(
					TEXT("No WidgetComponent found on actor '%s'. Use spawn_widget_component to add one first."), *ActorName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Widget Component Property")));
			WidgetComp->Modify();

			TArray<FString> ChangedProps;

			bool bHasDrawX = Args->HasField(TEXT("draw_size_x"));
			bool bHasDrawY = Args->HasField(TEXT("draw_size_y"));
			if (bHasDrawX || bHasDrawY)
			{
				FVector2D CurrentSize = WidgetComp->GetDrawSize();
				float NewX = bHasDrawX ? (float)Args->GetNumberField(TEXT("draw_size_x")) : CurrentSize.X;
				float NewY = bHasDrawY ? (float)Args->GetNumberField(TEXT("draw_size_y")) : CurrentSize.Y;
				WidgetComp->SetDrawSize(FVector2D(NewX, NewY));
				ChangedProps.Add(FString::Printf(TEXT("DrawSize=(%.0f x %.0f)"), NewX, NewY));
			}

			FString WidgetClassPath;
			if (Args->TryGetStringField(TEXT("widget_class_path"), WidgetClassPath) && !WidgetClassPath.IsEmpty())
			{
				UBlueprint* WidgetBP = LoadObject<UBlueprint>(nullptr, *WidgetClassPath);
				UClass* GenClass = WidgetBP ? WidgetBP->GeneratedClass.Get() : nullptr;
				if (GenClass && GenClass->IsChildOf(UUserWidget::StaticClass()))
				{
					WidgetComp->SetWidgetClass(TSubclassOf<UUserWidget>(GenClass));
					ChangedProps.Add(FString::Printf(TEXT("WidgetClass=%s"), *WidgetBP->GetName()));
				}
				else
				{
					FString GeneratedClassPath = WidgetClassPath + TEXT("_C");
					UClass* LoadedClass = LoadObject<UClass>(nullptr, *GeneratedClassPath);
					if (LoadedClass && LoadedClass->IsChildOf(UUserWidget::StaticClass()))
					{
						TSubclassOf<UUserWidget> WC = LoadedClass;
						WidgetComp->SetWidgetClass(WC);
						ChangedProps.Add(FString::Printf(TEXT("WidgetClass=%s"), *LoadedClass->GetName()));
					}
					else
					{
						GEditor->EndTransaction();
						return FMCPToolResult::Error(FString::Printf(
							TEXT("Could not load Widget Blueprint class from: %s"), *WidgetClassPath));
					}
				}
			}

			FString SpaceStr;
			if (Args->TryGetStringField(TEXT("space"), SpaceStr))
			{
				EWidgetSpace NewSpace = (SpaceStr == TEXT("Screen")) ? EWidgetSpace::Screen : EWidgetSpace::World;
				WidgetComp->SetWidgetSpace(NewSpace);
				ChangedProps.Add(FString::Printf(TEXT("Space=%s"), *SpaceStr));
			}

			bool bHasR = Args->HasField(TEXT("tint_r"));
			bool bHasG = Args->HasField(TEXT("tint_g"));
			bool bHasB = Args->HasField(TEXT("tint_b"));
			bool bHasA = Args->HasField(TEXT("tint_a"));
			if (bHasR || bHasG || bHasB || bHasA)
			{
				FLinearColor NewTint(
					bHasR ? (float)Args->GetNumberField(TEXT("tint_r")) : 1.0f,
					bHasG ? (float)Args->GetNumberField(TEXT("tint_g")) : 1.0f,
					bHasB ? (float)Args->GetNumberField(TEXT("tint_b")) : 1.0f,
					bHasA ? (float)Args->GetNumberField(TEXT("tint_a")) : 1.0f
				);
				WidgetComp->SetTintColorAndOpacity(NewTint);
				ChangedProps.Add(FString::Printf(TEXT("Tint=(R=%.2f G=%.2f B=%.2f A=%.2f)"),
					NewTint.R, NewTint.G, NewTint.B, NewTint.A));
			}

			bool bTwoSided = false;
			if (Args->TryGetBoolField(TEXT("is_two_sided"), bTwoSided))
			{
				WidgetComp->SetTwoSided(bTwoSided);
				ChangedProps.Add(FString::Printf(TEXT("TwoSided=%s"), bTwoSided ? TEXT("true") : TEXT("false")));
			}

			GEditor->EndTransaction();

			if (ChangedProps.Num() == 0)
			{
				return FMCPToolResult::Success(FString::Printf(
					TEXT("No properties changed on WidgetComponent of '%s'. Provide at least one optional property to modify."),
					*ActorName));
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Updated WidgetComponent on '%s': %s"),
				*ActorName,
				*FString::Join(ChangedProps, TEXT(", "))));
		});
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_widget_blueprint - Create a new Widget Blueprint
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path for the new Widget Blueprint (e.g., '/Game/UI/WBP_MainMenu')"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("root_widget_type"), TEXT("Root panel widget type (default: CanvasPanel)"),
			{ TEXT("CanvasPanel"), TEXT("VerticalBox"), TEXT("HorizontalBox"), TEXT("Overlay"), TEXT("GridPanel") });

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_widget_blueprint");
		Def.Description = TEXT("Create a new Widget Blueprint (UMG) asset with a specified root panel type. The Widget Blueprint is saved and ready for editing with add_widget, set_widget_properties, etc.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString RootType = TEXT("CanvasPanel");
			Args->TryGetStringField(TEXT("root_widget_type"), RootType);

			UClass* RootClass = WidgetClassFromTypeName(RootType);
			if (!RootClass)
				return FMCPToolResult::Error(FString::Printf(TEXT("Unknown root widget type: %s"), *RootType));

			FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
			FString AssetName = FPackageName::GetShortName(AssetPath);

			UPackage* Package = CreatePackage(*PackagePath);
			if (!Package)
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package: %s"), *PackagePath));

			UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();

			UWidgetBlueprint* NewWBP = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(
				UWidgetBlueprint::StaticClass(), Package, FName(*AssetName),
				RF_Public | RF_Standalone, nullptr, GWarn));

			if (!NewWBP)
				return FMCPToolResult::Error(TEXT("Failed to create Widget Blueprint"));

			// Set the root widget
			if (NewWBP->WidgetTree)
			{
				UWidget* RootWidget = NewWBP->WidgetTree->ConstructWidget<UWidget>(RootClass, FName(TEXT("RootPanel")));
				if (RootWidget)
				{
					NewWBP->WidgetTree->RootWidget = RootWidget;
				}
			}

			SaveWidgetBlueprint(NewWBP);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created Widget Blueprint '%s' at %s with root widget type '%s'"),
				*AssetName, *AssetPath, *RootType));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_widget_tree - Read widget hierarchy
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true);
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("include_properties"), TEXT("Include basic properties like text, color, anchors (default: false)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_widget_tree");
		Def.Description = TEXT("Read the widget hierarchy of a Widget Blueprint. Returns a JSON tree showing all widgets, their types, names, children, and optionally their properties and slot configuration.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			if (!WBP->WidgetTree) return FMCPToolResult::Error(TEXT("Widget Blueprint has no WidgetTree"));

			bool bIncludeProperties = false;
			Args->TryGetBoolField(TEXT("include_properties"), bIncludeProperties);

			UWidget* Root = WBP->WidgetTree->RootWidget;
			if (!Root) return FMCPToolResult::Success(TEXT("Widget Blueprint has no root widget (empty tree)"));

			TSharedPtr<FJsonObject> Tree = SerializeWidget(Root, bIncludeProperties);
			return FMCPToolResult::Success(JsonToString(Tree));
		});
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// add_widget - Add a widget to the widget tree
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("widget_type"), TEXT("Type of widget to add"),
			{ TEXT("CanvasPanel"), TEXT("VerticalBox"), TEXT("HorizontalBox"), TEXT("GridPanel"),
			  TEXT("Overlay"), TEXT("SizeBox"), TEXT("ScaleBox"), TEXT("Border"), TEXT("WrapBox"),
			  TEXT("UniformGridPanel"), TEXT("ScrollBox"), TEXT("Button"), TEXT("TextBlock"),
			  TEXT("Image"), TEXT("EditableTextBox"), TEXT("Slider"), TEXT("ProgressBar"),
			  TEXT("CheckBox"), TEXT("ComboBoxString"), TEXT("Spacer"), TEXT("RichTextBlock") }, true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("widget_name"), TEXT("Custom name for the widget (auto-generated if omitted)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("parent_widget_name"), TEXT("Name of parent widget. If omitted, adds to root widget."));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("index"), TEXT("Insertion index among siblings (appends to end if omitted)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("add_widget");
		Def.Description = TEXT("Add a widget to a Widget Blueprint's widget tree. Supports all standard UMG widgets including layout panels (CanvasPanel, VerticalBox, HorizontalBox, etc.) and leaf widgets (Button, TextBlock, Image, etc.). Returns the created widget's name.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetType;
			if (!Args->TryGetStringField(TEXT("widget_type"), WidgetType))
				return FMCPToolResult::Error(TEXT("widget_type is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			if (!WBP->WidgetTree) return FMCPToolResult::Error(TEXT("Widget Blueprint has no WidgetTree"));

			UClass* WidgetClass = WidgetClassFromTypeName(WidgetType);
			if (!WidgetClass)
				return FMCPToolResult::Error(FString::Printf(TEXT("Unknown widget type: %s"), *WidgetType));

			// Find parent
			UPanelWidget* ParentPanel = nullptr;
			FString ParentName;
			if (Args->TryGetStringField(TEXT("parent_widget_name"), ParentName) && !ParentName.IsEmpty())
			{
				UWidget* ParentWidget = FindWidgetByName(WBP, ParentName);
				if (!ParentWidget)
					return FMCPToolResult::Error(FString::Printf(TEXT("Parent widget not found: %s"), *ParentName));
				ParentPanel = Cast<UPanelWidget>(ParentWidget);
				if (!ParentPanel)
					return FMCPToolResult::Error(FString::Printf(TEXT("Parent widget '%s' is not a panel (cannot have children)"), *ParentName));
			}
			else
			{
				// Default to root
				ParentPanel = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget);
				if (!ParentPanel)
					return FMCPToolResult::Error(TEXT("Root widget is not a panel or doesn't exist. Create one with create_widget_blueprint first."));
			}

			// Determine widget name
			FString WidgetName;
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName) || WidgetName.IsEmpty())
			{
				WidgetName = WidgetType;
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Add Widget")));
			WBP->Modify();

			UWidget* NewWidget = WBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*WidgetName));
			if (!NewWidget)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to construct widget of type: %s"), *WidgetType));
			}

			// Add to parent
			UPanelSlot* Slot = ParentPanel->AddChild(NewWidget);
			if (!Slot)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to add widget to parent '%s'. Parent may be full or incompatible."), *ParentPanel->GetName()));
			}

			GEditor->EndTransaction();

			SaveWidgetBlueprint(WBP);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Added %s widget '%s' to parent '%s' in %s"),
				*WidgetType, *NewWidget->GetName(), *ParentPanel->GetName(), *AssetPath));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// remove_widget - Remove a widget from the tree
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("widget_name"), TEXT("Name of the widget to remove"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("remove_widget");
		Def.Description = TEXT("Remove a widget from a Widget Blueprint's widget tree by name. Also removes all children of that widget.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetName;
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName))
				return FMCPToolResult::Error(TEXT("widget_name is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			if (Widget == WBP->WidgetTree->RootWidget)
				return FMCPToolResult::Error(TEXT("Cannot remove the root widget. Use create_widget_blueprint to create a new one."));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Remove Widget")));
			WBP->Modify();

			// Remove from parent
			UPanelWidget* Parent = Widget->GetParent();
			if (Parent)
			{
				Parent->RemoveChild(Widget);
			}
			WBP->WidgetTree->RemoveWidget(Widget);

			GEditor->EndTransaction();
			SaveWidgetBlueprint(WBP);

			return FMCPToolResult::Success(FString::Printf(TEXT("Removed widget '%s' from %s"), *WidgetName, *AssetPath));
		});
		Def.bDestructiveHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_widget_properties - Set properties on any widget
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("widget_name"), TEXT("Name of the widget to modify"), true);
		// Common properties
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("visibility"), TEXT("Widget visibility"),
			{ TEXT("Visible"), TEXT("Collapsed"), TEXT("Hidden"), TEXT("HitTestInvisible"), TEXT("SelfHitTestInvisible") });
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("is_enabled"), TEXT("Whether the widget is interactive"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("tooltip_text"), TEXT("Tooltip text"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("render_opacity"), TEXT("Render opacity (0.0 - 1.0)"));
		// TextBlock
		FMCPSchemaBuilder::AddString(Schema, TEXT("text"), TEXT("Text content (for TextBlock/RichTextBlock)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("font_size"), TEXT("Font size in points (for TextBlock)"));
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("justification"), TEXT("Text justification (for TextBlock)"),
			{ TEXT("Left"), TEXT("Center"), TEXT("Right") });
		// Color (applies to TextBlock color, Image tint, Button bg color)
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("color_r"), TEXT("Color red channel (0.0 - 1.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("color_g"), TEXT("Color green channel (0.0 - 1.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("color_b"), TEXT("Color blue channel (0.0 - 1.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("color_a"), TEXT("Color alpha channel (0.0 - 1.0)"));
		// ProgressBar
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("percent"), TEXT("Progress bar fill percent (0.0 - 1.0)"));
		// SizeBox
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("width_override"), TEXT("Width override for SizeBox (0 to clear)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("height_override"), TEXT("Height override for SizeBox (0 to clear)"));
		// Border/SizeBox padding
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("padding_left"), TEXT("Left padding"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("padding_top"), TEXT("Top padding"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("padding_right"), TEXT("Right padding"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("padding_bottom"), TEXT("Bottom padding"));
		// Image brush size
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("brush_size_x"), TEXT("Image brush width"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("brush_size_y"), TEXT("Image brush height"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_widget_properties");
		Def.Description = TEXT("Set properties on any widget in a Widget Blueprint. Supports common properties (visibility, opacity, tooltip) and type-specific properties (text/font for TextBlock, color for TextBlock/Button/Image, percent for ProgressBar, size overrides for SizeBox, padding for Border). Only provided fields are modified.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetName;
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName))
				return FMCPToolResult::Error(TEXT("widget_name is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Widget Properties")));
			WBP->Modify();

			TArray<FString> Changed;

			// Common: Visibility
			FString VisStr;
			if (Args->TryGetStringField(TEXT("visibility"), VisStr))
			{
				if (VisStr == TEXT("Visible")) Widget->SetVisibility(ESlateVisibility::Visible);
				else if (VisStr == TEXT("Collapsed")) Widget->SetVisibility(ESlateVisibility::Collapsed);
				else if (VisStr == TEXT("Hidden")) Widget->SetVisibility(ESlateVisibility::Hidden);
				else if (VisStr == TEXT("HitTestInvisible")) Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
				else if (VisStr == TEXT("SelfHitTestInvisible")) Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				Changed.Add(FString::Printf(TEXT("Visibility=%s"), *VisStr));
			}

			// Common: IsEnabled
			bool bEnabled;
			if (Args->TryGetBoolField(TEXT("is_enabled"), bEnabled))
			{
				Widget->SetIsEnabled(bEnabled);
				Changed.Add(FString::Printf(TEXT("IsEnabled=%s"), bEnabled ? TEXT("true") : TEXT("false")));
			}

			// Common: RenderOpacity
			if (Args->HasField(TEXT("render_opacity")))
			{
				float Opacity = (float)Args->GetNumberField(TEXT("render_opacity"));
				Widget->SetRenderOpacity(Opacity);
				Changed.Add(FString::Printf(TEXT("RenderOpacity=%.2f"), Opacity));
			}

			// Common: ToolTipText
			FString TooltipStr;
			if (Args->TryGetStringField(TEXT("tooltip_text"), TooltipStr))
			{
				Widget->SetToolTipText(FText::FromString(TooltipStr));
				Changed.Add(FString::Printf(TEXT("ToolTip=%s"), *TooltipStr));
			}

			// Build color from provided channels
			bool bHasColorR = Args->HasField(TEXT("color_r"));
			bool bHasColorG = Args->HasField(TEXT("color_g"));
			bool bHasColorB = Args->HasField(TEXT("color_b"));
			bool bHasColorA = Args->HasField(TEXT("color_a"));
			bool bHasColor = bHasColorR || bHasColorG || bHasColorB || bHasColorA;
			FLinearColor Color(
				bHasColorR ? (float)Args->GetNumberField(TEXT("color_r")) : 1.0f,
				bHasColorG ? (float)Args->GetNumberField(TEXT("color_g")) : 1.0f,
				bHasColorB ? (float)Args->GetNumberField(TEXT("color_b")) : 1.0f,
				bHasColorA ? (float)Args->GetNumberField(TEXT("color_a")) : 1.0f
			);

			// TextBlock specific
			if (UTextBlock* TB = Cast<UTextBlock>(Widget))
			{
				FString Text;
				if (Args->TryGetStringField(TEXT("text"), Text))
				{
					TB->SetText(FText::FromString(Text));
					Changed.Add(FString::Printf(TEXT("Text=%s"), *Text));
				}

				if (Args->HasField(TEXT("font_size")))
				{
					int32 FontSize = (int32)Args->GetNumberField(TEXT("font_size"));
					FSlateFontInfo FontInfo = TB->GetFont();
					FontInfo.Size = FontSize;
					TB->SetFont(FontInfo);
					Changed.Add(FString::Printf(TEXT("FontSize=%d"), FontSize));
				}

				FString JustStr;
				if (Args->TryGetStringField(TEXT("justification"), JustStr))
				{
					if (JustStr == TEXT("Left")) TB->SetJustification(ETextJustify::Left);
					else if (JustStr == TEXT("Center")) TB->SetJustification(ETextJustify::Center);
					else if (JustStr == TEXT("Right")) TB->SetJustification(ETextJustify::Right);
					Changed.Add(FString::Printf(TEXT("Justification=%s"), *JustStr));
				}

				if (bHasColor)
				{
					TB->SetColorAndOpacity(FSlateColor(Color));
					Changed.Add(TEXT("TextColor updated"));
				}
			}
			// Button specific
			else if (UButton* Btn = Cast<UButton>(Widget))
			{
				if (bHasColor)
				{
					Btn->SetBackgroundColor(Color);
					Changed.Add(TEXT("BackgroundColor updated"));
				}
			}
			// Image specific
			else if (UImage* Img = Cast<UImage>(Widget))
			{
				if (bHasColor)
				{
					Img->SetColorAndOpacity(Color);
					Changed.Add(TEXT("ImageTint updated"));
				}

				bool bHasBrushX = Args->HasField(TEXT("brush_size_x"));
				bool bHasBrushY = Args->HasField(TEXT("brush_size_y"));
				if (bHasBrushX || bHasBrushY)
				{
					FVector2D CurrentSize = Img->GetBrush().ImageSize;
					float BX = bHasBrushX ? (float)Args->GetNumberField(TEXT("brush_size_x")) : CurrentSize.X;
					float BY = bHasBrushY ? (float)Args->GetNumberField(TEXT("brush_size_y")) : CurrentSize.Y;
					Img->SetDesiredSizeOverride(FVector2D(BX, BY));
					Changed.Add(FString::Printf(TEXT("BrushSize=(%.0f, %.0f)"), BX, BY));
				}
			}
			// ProgressBar specific
			else if (UProgressBar* PB = Cast<UProgressBar>(Widget))
			{
				if (Args->HasField(TEXT("percent")))
				{
					float Pct = (float)Args->GetNumberField(TEXT("percent"));
					PB->SetPercent(Pct);
					Changed.Add(FString::Printf(TEXT("Percent=%.2f"), Pct));
				}
				if (bHasColor)
				{
					PB->SetFillColorAndOpacity(Color);
					Changed.Add(TEXT("FillColor updated"));
				}
			}
			// SizeBox specific
			else if (USizeBox* SB = Cast<USizeBox>(Widget))
			{
				if (Args->HasField(TEXT("width_override")))
				{
					float W = (float)Args->GetNumberField(TEXT("width_override"));
					SB->SetWidthOverride(W);
					Changed.Add(FString::Printf(TEXT("WidthOverride=%.0f"), W));
				}
				if (Args->HasField(TEXT("height_override")))
				{
					float H = (float)Args->GetNumberField(TEXT("height_override"));
					SB->SetHeightOverride(H);
					Changed.Add(FString::Printf(TEXT("HeightOverride=%.0f"), H));
				}
			}
			// Border specific
			else if (UBorder* Brd = Cast<UBorder>(Widget))
			{
				if (bHasColor)
				{
					Brd->SetBrushColor(Color);
					Changed.Add(TEXT("BorderColor updated"));
				}
				bool bHasPadding = Args->HasField(TEXT("padding_left")) || Args->HasField(TEXT("padding_top"))
					|| Args->HasField(TEXT("padding_right")) || Args->HasField(TEXT("padding_bottom"));
				if (bHasPadding)
				{
					FMargin Padding;
					Padding.Left = Args->HasField(TEXT("padding_left")) ? (float)Args->GetNumberField(TEXT("padding_left")) : 0.0f;
					Padding.Top = Args->HasField(TEXT("padding_top")) ? (float)Args->GetNumberField(TEXT("padding_top")) : 0.0f;
					Padding.Right = Args->HasField(TEXT("padding_right")) ? (float)Args->GetNumberField(TEXT("padding_right")) : 0.0f;
					Padding.Bottom = Args->HasField(TEXT("padding_bottom")) ? (float)Args->GetNumberField(TEXT("padding_bottom")) : 0.0f;
					Brd->SetPadding(Padding);
					Changed.Add(TEXT("Padding updated"));
				}
			}

			GEditor->EndTransaction();

			if (Changed.Num() == 0)
			{
				return FMCPToolResult::Success(FString::Printf(
					TEXT("No applicable properties changed on widget '%s' (type: %s). Check parameter compatibility with widget type."),
					*WidgetName, *Widget->GetClass()->GetName()));
			}

			SaveWidgetBlueprint(WBP);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Updated widget '%s': %s"),
				*WidgetName, *FString::Join(Changed, TEXT(", "))));
		});
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_widget_properties - Inspect widget properties
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("widget_name"), TEXT("Name of the widget to inspect"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_widget_properties");
		Def.Description = TEXT("Inspect a widget's current properties including type, visibility, opacity, and type-specific properties (text, colors, sizes). Also reports slot/layout information from the parent container.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetName;
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName))
				return FMCPToolResult::Error(TEXT("widget_name is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("name"), Widget->GetName());
			Info->SetStringField(TEXT("type"), Widget->GetClass()->GetName());
			Info->SetStringField(TEXT("visibility"), UEnum::GetValueAsString(Widget->GetVisibility()));
			Info->SetBoolField(TEXT("is_enabled"), Widget->GetIsEnabled());
			Info->SetNumberField(TEXT("render_opacity"), Widget->GetRenderOpacity());

			UPanelWidget* Parent = Widget->GetParent();
			if (Parent)
			{
				Info->SetStringField(TEXT("parent"), Parent->GetName());
			}

			bool bIsPanel = Cast<UPanelWidget>(Widget) != nullptr;
			Info->SetBoolField(TEXT("is_panel"), bIsPanel);
			if (bIsPanel)
			{
				Info->SetNumberField(TEXT("child_count"), Cast<UPanelWidget>(Widget)->GetChildrenCount());
			}

			// Type-specific properties
			if (UTextBlock* TB = Cast<UTextBlock>(Widget))
			{
				Info->SetStringField(TEXT("text"), TB->GetText().ToString());
				Info->SetNumberField(TEXT("font_size"), TB->GetFont().Size);
			}
			else if (UProgressBar* PB = Cast<UProgressBar>(Widget))
			{
				Info->SetNumberField(TEXT("percent"), PB->GetPercent());
			}
			else if (USizeBox* SB = Cast<USizeBox>(Widget))
			{
				Info->SetBoolField(TEXT("has_width_override"), SB->GetWidthOverride() > 0);
				Info->SetBoolField(TEXT("has_height_override"), SB->GetHeightOverride() > 0);
			}

			// Slot info
			if (Widget->Slot)
			{
				TSharedPtr<FJsonObject> SlotInfo = MakeShared<FJsonObject>();

				if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Widget->Slot))
				{
					SlotInfo->SetStringField(TEXT("slot_type"), TEXT("CanvasPanelSlot"));
					FAnchors A = CS->GetAnchors();
					SlotInfo->SetNumberField(TEXT("anchor_min_x"), A.Minimum.X);
					SlotInfo->SetNumberField(TEXT("anchor_min_y"), A.Minimum.Y);
					SlotInfo->SetNumberField(TEXT("anchor_max_x"), A.Maximum.X);
					SlotInfo->SetNumberField(TEXT("anchor_max_y"), A.Maximum.Y);
					FMargin O = CS->GetOffsets();
					SlotInfo->SetNumberField(TEXT("offset_left"), O.Left);
					SlotInfo->SetNumberField(TEXT("offset_top"), O.Top);
					SlotInfo->SetNumberField(TEXT("offset_right"), O.Right);
					SlotInfo->SetNumberField(TEXT("offset_bottom"), O.Bottom);
					FVector2D Alignment = CS->GetAlignment();
					SlotInfo->SetNumberField(TEXT("alignment_x"), Alignment.X);
					SlotInfo->SetNumberField(TEXT("alignment_y"), Alignment.Y);
					SlotInfo->SetNumberField(TEXT("z_order"), CS->GetZOrder());
					SlotInfo->SetBoolField(TEXT("auto_size"), CS->GetAutoSize());
				}
				else if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Widget->Slot))
				{
					SlotInfo->SetStringField(TEXT("slot_type"), TEXT("VerticalBoxSlot"));
					FSlateChildSize SizeRule = VS->GetSize();
					SlotInfo->SetStringField(TEXT("size_rule"), SizeRule.SizeRule == ESlateSizeRule::Automatic ? TEXT("Auto") : TEXT("Fill"));
					SlotInfo->SetStringField(TEXT("halign"), UEnum::GetValueAsString(VS->GetHorizontalAlignment()));
					SlotInfo->SetStringField(TEXT("valign"), UEnum::GetValueAsString(VS->GetVerticalAlignment()));
				}
				else if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Widget->Slot))
				{
					SlotInfo->SetStringField(TEXT("slot_type"), TEXT("HorizontalBoxSlot"));
					FSlateChildSize SizeRule = HS->GetSize();
					SlotInfo->SetStringField(TEXT("size_rule"), SizeRule.SizeRule == ESlateSizeRule::Automatic ? TEXT("Auto") : TEXT("Fill"));
					SlotInfo->SetStringField(TEXT("halign"), UEnum::GetValueAsString(HS->GetHorizontalAlignment()));
					SlotInfo->SetStringField(TEXT("valign"), UEnum::GetValueAsString(HS->GetVerticalAlignment()));
				}
				else if (UOverlaySlot* OS = Cast<UOverlaySlot>(Widget->Slot))
				{
					SlotInfo->SetStringField(TEXT("slot_type"), TEXT("OverlaySlot"));
					SlotInfo->SetStringField(TEXT("halign"), UEnum::GetValueAsString(OS->GetHorizontalAlignment()));
					SlotInfo->SetStringField(TEXT("valign"), UEnum::GetValueAsString(OS->GetVerticalAlignment()));
				}

				Info->SetObjectField(TEXT("slot"), SlotInfo);
			}

			return FMCPToolResult::Success(JsonToString(Info));
		});
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_widget_slot - Configure slot/layout properties
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("widget_name"), TEXT("Name of the widget to configure slot for"), true);
		// CanvasPanel slot
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("anchor_min_x"), TEXT("Anchor minimum X (0-1). CanvasPanel slot."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("anchor_min_y"), TEXT("Anchor minimum Y (0-1). CanvasPanel slot."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("anchor_max_x"), TEXT("Anchor maximum X (0-1). CanvasPanel slot."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("anchor_max_y"), TEXT("Anchor maximum Y (0-1). CanvasPanel slot."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("offset_left"), TEXT("Left offset in pixels. CanvasPanel slot."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("offset_top"), TEXT("Top offset in pixels. CanvasPanel slot."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("offset_right"), TEXT("Right offset/width. CanvasPanel slot."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("offset_bottom"), TEXT("Bottom offset/height. CanvasPanel slot."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("alignment_x"), TEXT("Alignment X (0-1). CanvasPanel slot."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("alignment_y"), TEXT("Alignment Y (0-1). CanvasPanel slot."));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("auto_size"), TEXT("Auto size to content. CanvasPanel slot."));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("z_order"), TEXT("Z-order for rendering. CanvasPanel slot."));
		// Box slot (Vertical/Horizontal)
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("size_rule"), TEXT("Size rule for box slots"),
			{ TEXT("Auto"), TEXT("Fill") });
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("fill_weight"), TEXT("Fill weight when size_rule is Fill (default: 1.0)"));
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("halign"), TEXT("Horizontal alignment"),
			{ TEXT("Fill"), TEXT("Left"), TEXT("Center"), TEXT("Right") });
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("valign"), TEXT("Vertical alignment"),
			{ TEXT("Fill"), TEXT("Top"), TEXT("Center"), TEXT("Bottom") });
		// Padding (for box/overlay slots)
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("padding_left"), TEXT("Left padding"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("padding_top"), TEXT("Top padding"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("padding_right"), TEXT("Right padding"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("padding_bottom"), TEXT("Bottom padding"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_widget_slot");
		Def.Description = TEXT("Configure a widget's slot properties (positioning within its parent container). For CanvasPanel parents: set anchors, position offsets, alignment, z-order. For VerticalBox/HorizontalBox: set size rule (Auto/Fill), alignment, padding. For Overlay: set alignment, padding. Only provided fields are modified.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetName;
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName))
				return FMCPToolResult::Error(TEXT("widget_name is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			if (!Widget->Slot)
				return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' has no slot (it may be the root widget)"), *WidgetName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Widget Slot")));
			WBP->Modify();

			TArray<FString> Changed;

			// Helper lambda for alignment enum parsing
			auto ParseHAlign = [](const FString& S) -> EHorizontalAlignment
			{
				if (S == TEXT("Left")) return EHorizontalAlignment::HAlign_Left;
				if (S == TEXT("Center")) return EHorizontalAlignment::HAlign_Center;
				if (S == TEXT("Right")) return EHorizontalAlignment::HAlign_Right;
				return EHorizontalAlignment::HAlign_Fill;
			};
			auto ParseVAlign = [](const FString& S) -> EVerticalAlignment
			{
				if (S == TEXT("Top")) return EVerticalAlignment::VAlign_Top;
				if (S == TEXT("Center")) return EVerticalAlignment::VAlign_Center;
				if (S == TEXT("Bottom")) return EVerticalAlignment::VAlign_Bottom;
				return EVerticalAlignment::VAlign_Fill;
			};

			if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				// Anchors
				bool bHasAnchor = Args->HasField(TEXT("anchor_min_x")) || Args->HasField(TEXT("anchor_min_y"))
					|| Args->HasField(TEXT("anchor_max_x")) || Args->HasField(TEXT("anchor_max_y"));
				if (bHasAnchor)
				{
					FAnchors Anchors = CS->GetAnchors();
					if (Args->HasField(TEXT("anchor_min_x"))) Anchors.Minimum.X = (float)Args->GetNumberField(TEXT("anchor_min_x"));
					if (Args->HasField(TEXT("anchor_min_y"))) Anchors.Minimum.Y = (float)Args->GetNumberField(TEXT("anchor_min_y"));
					if (Args->HasField(TEXT("anchor_max_x"))) Anchors.Maximum.X = (float)Args->GetNumberField(TEXT("anchor_max_x"));
					if (Args->HasField(TEXT("anchor_max_y"))) Anchors.Maximum.Y = (float)Args->GetNumberField(TEXT("anchor_max_y"));
					CS->SetAnchors(Anchors);
					Changed.Add(FString::Printf(TEXT("Anchors=(%.2f,%.2f)-(%.2f,%.2f)"),
						Anchors.Minimum.X, Anchors.Minimum.Y, Anchors.Maximum.X, Anchors.Maximum.Y));
				}

				// Offsets
				bool bHasOffset = Args->HasField(TEXT("offset_left")) || Args->HasField(TEXT("offset_top"))
					|| Args->HasField(TEXT("offset_right")) || Args->HasField(TEXT("offset_bottom"));
				if (bHasOffset)
				{
					FMargin Offsets = CS->GetOffsets();
					if (Args->HasField(TEXT("offset_left"))) Offsets.Left = (float)Args->GetNumberField(TEXT("offset_left"));
					if (Args->HasField(TEXT("offset_top"))) Offsets.Top = (float)Args->GetNumberField(TEXT("offset_top"));
					if (Args->HasField(TEXT("offset_right"))) Offsets.Right = (float)Args->GetNumberField(TEXT("offset_right"));
					if (Args->HasField(TEXT("offset_bottom"))) Offsets.Bottom = (float)Args->GetNumberField(TEXT("offset_bottom"));
					CS->SetOffsets(Offsets);
					Changed.Add(FString::Printf(TEXT("Offsets=(L=%.0f T=%.0f R=%.0f B=%.0f)"),
						Offsets.Left, Offsets.Top, Offsets.Right, Offsets.Bottom));
				}

				// Alignment
				bool bHasAlignX = Args->HasField(TEXT("alignment_x"));
				bool bHasAlignY = Args->HasField(TEXT("alignment_y"));
				if (bHasAlignX || bHasAlignY)
				{
					FVector2D Alignment = CS->GetAlignment();
					if (bHasAlignX) Alignment.X = (float)Args->GetNumberField(TEXT("alignment_x"));
					if (bHasAlignY) Alignment.Y = (float)Args->GetNumberField(TEXT("alignment_y"));
					CS->SetAlignment(Alignment);
					Changed.Add(FString::Printf(TEXT("Alignment=(%.2f, %.2f)"), Alignment.X, Alignment.Y));
				}

				// Auto size
				bool bAutoSize;
				if (Args->TryGetBoolField(TEXT("auto_size"), bAutoSize))
				{
					CS->SetAutoSize(bAutoSize);
					Changed.Add(FString::Printf(TEXT("AutoSize=%s"), bAutoSize ? TEXT("true") : TEXT("false")));
				}

				// Z-order
				if (Args->HasField(TEXT("z_order")))
				{
					int32 ZOrder = (int32)Args->GetNumberField(TEXT("z_order"));
					CS->SetZOrder(ZOrder);
					Changed.Add(FString::Printf(TEXT("ZOrder=%d"), ZOrder));
				}
			}
			else if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Widget->Slot))
			{
				FString SizeRule;
				if (Args->TryGetStringField(TEXT("size_rule"), SizeRule))
				{
					FSlateChildSize Size;
					Size.SizeRule = (SizeRule == TEXT("Fill")) ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic;
					if (Args->HasField(TEXT("fill_weight"))) Size.Value = (float)Args->GetNumberField(TEXT("fill_weight"));
					VS->SetSize(Size);
					Changed.Add(FString::Printf(TEXT("SizeRule=%s"), *SizeRule));
				}

				FString HAlignStr;
				if (Args->TryGetStringField(TEXT("halign"), HAlignStr))
				{
					VS->SetHorizontalAlignment(ParseHAlign(HAlignStr));
					Changed.Add(FString::Printf(TEXT("HAlign=%s"), *HAlignStr));
				}

				FString VAlignStr;
				if (Args->TryGetStringField(TEXT("valign"), VAlignStr))
				{
					VS->SetVerticalAlignment(ParseVAlign(VAlignStr));
					Changed.Add(FString::Printf(TEXT("VAlign=%s"), *VAlignStr));
				}

				bool bHasPad = Args->HasField(TEXT("padding_left")) || Args->HasField(TEXT("padding_top"))
					|| Args->HasField(TEXT("padding_right")) || Args->HasField(TEXT("padding_bottom"));
				if (bHasPad)
				{
					FMargin Pad;
					Pad.Left = Args->HasField(TEXT("padding_left")) ? (float)Args->GetNumberField(TEXT("padding_left")) : 0.0f;
					Pad.Top = Args->HasField(TEXT("padding_top")) ? (float)Args->GetNumberField(TEXT("padding_top")) : 0.0f;
					Pad.Right = Args->HasField(TEXT("padding_right")) ? (float)Args->GetNumberField(TEXT("padding_right")) : 0.0f;
					Pad.Bottom = Args->HasField(TEXT("padding_bottom")) ? (float)Args->GetNumberField(TEXT("padding_bottom")) : 0.0f;
					VS->SetPadding(Pad);
					Changed.Add(TEXT("Padding updated"));
				}
			}
			else if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Widget->Slot))
			{
				FString SizeRule;
				if (Args->TryGetStringField(TEXT("size_rule"), SizeRule))
				{
					FSlateChildSize Size;
					Size.SizeRule = (SizeRule == TEXT("Fill")) ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic;
					if (Args->HasField(TEXT("fill_weight"))) Size.Value = (float)Args->GetNumberField(TEXT("fill_weight"));
					HS->SetSize(Size);
					Changed.Add(FString::Printf(TEXT("SizeRule=%s"), *SizeRule));
				}

				FString HAlignStr;
				if (Args->TryGetStringField(TEXT("halign"), HAlignStr))
				{
					HS->SetHorizontalAlignment(ParseHAlign(HAlignStr));
					Changed.Add(FString::Printf(TEXT("HAlign=%s"), *HAlignStr));
				}

				FString VAlignStr;
				if (Args->TryGetStringField(TEXT("valign"), VAlignStr))
				{
					HS->SetVerticalAlignment(ParseVAlign(VAlignStr));
					Changed.Add(FString::Printf(TEXT("VAlign=%s"), *VAlignStr));
				}

				bool bHasPad = Args->HasField(TEXT("padding_left")) || Args->HasField(TEXT("padding_top"))
					|| Args->HasField(TEXT("padding_right")) || Args->HasField(TEXT("padding_bottom"));
				if (bHasPad)
				{
					FMargin Pad;
					Pad.Left = Args->HasField(TEXT("padding_left")) ? (float)Args->GetNumberField(TEXT("padding_left")) : 0.0f;
					Pad.Top = Args->HasField(TEXT("padding_top")) ? (float)Args->GetNumberField(TEXT("padding_top")) : 0.0f;
					Pad.Right = Args->HasField(TEXT("padding_right")) ? (float)Args->GetNumberField(TEXT("padding_right")) : 0.0f;
					Pad.Bottom = Args->HasField(TEXT("padding_bottom")) ? (float)Args->GetNumberField(TEXT("padding_bottom")) : 0.0f;
					HS->SetPadding(Pad);
					Changed.Add(TEXT("Padding updated"));
				}
			}
			else if (UOverlaySlot* OS = Cast<UOverlaySlot>(Widget->Slot))
			{
				FString HAlignStr;
				if (Args->TryGetStringField(TEXT("halign"), HAlignStr))
				{
					OS->SetHorizontalAlignment(ParseHAlign(HAlignStr));
					Changed.Add(FString::Printf(TEXT("HAlign=%s"), *HAlignStr));
				}

				FString VAlignStr;
				if (Args->TryGetStringField(TEXT("valign"), VAlignStr))
				{
					OS->SetVerticalAlignment(ParseVAlign(VAlignStr));
					Changed.Add(FString::Printf(TEXT("VAlign=%s"), *VAlignStr));
				}

				bool bHasPad = Args->HasField(TEXT("padding_left")) || Args->HasField(TEXT("padding_top"))
					|| Args->HasField(TEXT("padding_right")) || Args->HasField(TEXT("padding_bottom"));
				if (bHasPad)
				{
					FMargin Pad;
					Pad.Left = Args->HasField(TEXT("padding_left")) ? (float)Args->GetNumberField(TEXT("padding_left")) : 0.0f;
					Pad.Top = Args->HasField(TEXT("padding_top")) ? (float)Args->GetNumberField(TEXT("padding_top")) : 0.0f;
					Pad.Right = Args->HasField(TEXT("padding_right")) ? (float)Args->GetNumberField(TEXT("padding_right")) : 0.0f;
					Pad.Bottom = Args->HasField(TEXT("padding_bottom")) ? (float)Args->GetNumberField(TEXT("padding_bottom")) : 0.0f;
					OS->SetPadding(Pad);
					Changed.Add(TEXT("Padding updated"));
				}
			}
			else
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(FString::Printf(
					TEXT("Unsupported slot type for widget '%s'. Slot type: %s"),
					*WidgetName, *Widget->Slot->GetClass()->GetName()));
			}

			GEditor->EndTransaction();

			if (Changed.Num() == 0)
			{
				return FMCPToolResult::Success(FString::Printf(
					TEXT("No slot properties changed on widget '%s'. Check that parameters match the parent container type."),
					*WidgetName));
			}

			SaveWidgetBlueprint(WBP);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Updated slot for widget '%s': %s"),
				*WidgetName, *FString::Join(Changed, TEXT(", "))));
		});
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// move_widget - Move/reparent a widget
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("widget_name"), TEXT("Name of the widget to move"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("new_parent_name"), TEXT("Name of the new parent widget (must be a panel)"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("index"), TEXT("Position among new parent's children (appends to end if omitted)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("move_widget");
		Def.Description = TEXT("Move a widget to a different parent in the widget tree, or reorder within the same parent.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetName, NewParentName;
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName))
				return FMCPToolResult::Error(TEXT("widget_name is required"));
			if (!Args->TryGetStringField(TEXT("new_parent_name"), NewParentName))
				return FMCPToolResult::Error(TEXT("new_parent_name is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			UWidget* NewParentWidget = FindWidgetByName(WBP, NewParentName);
			if (!NewParentWidget) return FMCPToolResult::Error(FString::Printf(TEXT("New parent not found: %s"), *NewParentName));

			UPanelWidget* NewParent = Cast<UPanelWidget>(NewParentWidget);
			if (!NewParent)
				return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a panel widget (cannot have children)"), *NewParentName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Move Widget")));
			WBP->Modify();

			// Remove from current parent
			UPanelWidget* OldParent = Widget->GetParent();
			if (OldParent)
			{
				OldParent->RemoveChild(Widget);
			}

			// Add to new parent
			NewParent->AddChild(Widget);

			GEditor->EndTransaction();
			SaveWidgetBlueprint(WBP);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Moved widget '%s' to new parent '%s'"), *WidgetName, *NewParentName));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_widget_image - Set texture on an Image widget
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("widget_name"), TEXT("Name of the UImage widget"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("texture_path"), TEXT("Content path to a Texture2D asset (e.g., '/Game/Textures/T_MyIcon')"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("tint_r"), TEXT("Tint red (0-1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("tint_g"), TEXT("Tint green (0-1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("tint_b"), TEXT("Tint blue (0-1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("tint_a"), TEXT("Tint alpha (0-1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("size_x"), TEXT("Override image display width"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("size_y"), TEXT("Override image display height"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_widget_image");
		Def.Description = TEXT("Set a texture on a UImage widget in a Widget Blueprint. Loads a Texture2D asset and assigns it as the image brush. Optionally set tint color and display size.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetName;
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName))
				return FMCPToolResult::Error(TEXT("widget_name is required"));

			FString TexturePath;
			if (!Args->TryGetStringField(TEXT("texture_path"), TexturePath))
				return FMCPToolResult::Error(TEXT("texture_path is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			UImage* ImageWidget = Cast<UImage>(Widget);
			if (!ImageWidget)
				return FMCPToolResult::Error(FString::Printf(TEXT("Widget '%s' is not an Image widget (type: %s)"),
					*WidgetName, *Widget->GetClass()->GetName()));

			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *TexturePath);
			if (!Texture)
				return FMCPToolResult::Error(FString::Printf(TEXT("Texture not found: %s"), *TexturePath));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Widget Image")));
			WBP->Modify();

			ImageWidget->SetBrushFromTexture(Texture);

			// Tint
			bool bHasR = Args->HasField(TEXT("tint_r"));
			bool bHasG = Args->HasField(TEXT("tint_g"));
			bool bHasB = Args->HasField(TEXT("tint_b"));
			bool bHasA = Args->HasField(TEXT("tint_a"));
			if (bHasR || bHasG || bHasB || bHasA)
			{
				FLinearColor Tint(
					bHasR ? (float)Args->GetNumberField(TEXT("tint_r")) : 1.0f,
					bHasG ? (float)Args->GetNumberField(TEXT("tint_g")) : 1.0f,
					bHasB ? (float)Args->GetNumberField(TEXT("tint_b")) : 1.0f,
					bHasA ? (float)Args->GetNumberField(TEXT("tint_a")) : 1.0f
				);
				ImageWidget->SetColorAndOpacity(Tint);
			}

			// Size override
			bool bHasSizeX = Args->HasField(TEXT("size_x"));
			bool bHasSizeY = Args->HasField(TEXT("size_y"));
			if (bHasSizeX || bHasSizeY)
			{
				float SX = bHasSizeX ? (float)Args->GetNumberField(TEXT("size_x")) : 0.0f;
				float SY = bHasSizeY ? (float)Args->GetNumberField(TEXT("size_y")) : 0.0f;
				ImageWidget->SetDesiredSizeOverride(FVector2D(SX, SY));
			}

			GEditor->EndTransaction();
			SaveWidgetBlueprint(WBP);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Set texture '%s' on Image widget '%s' in %s"),
				*Texture->GetName(), *WidgetName, *AssetPath));
		});
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// bind_widget_event - Bind widget event to a Blueprint function
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("widget_name"), TEXT("Name of the widget to bind event on (e.g., 'Btn_Start')"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("event_name"), TEXT("Widget event to bind"),
			{ TEXT("OnClicked"), TEXT("OnPressed"), TEXT("OnReleased"), TEXT("OnHovered"), TEXT("OnUnhovered"),
			  TEXT("OnValueChanged"), TEXT("OnCheckStateChanged"), TEXT("OnTextChanged"), TEXT("OnTextCommitted") }, true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("function_name"), TEXT("Name of the Blueprint function or custom event to call when the event fires. If it doesn't exist, a new custom event will be created in the event graph."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("bind_widget_event");
		Def.Description = TEXT("Create an event binding for a widget in a Widget Blueprint's event graph. Adds a K2Node_ComponentBoundEvent that fires when the specified widget event occurs (e.g., Button OnClicked, Slider OnValueChanged). The event node is placed in the event graph and can be wired to other nodes using Blueprint tools.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetName;
			if (!Args->TryGetStringField(TEXT("widget_name"), WidgetName))
				return FMCPToolResult::Error(TEXT("widget_name is required"));

			FString EventName;
			if (!Args->TryGetStringField(TEXT("event_name"), EventName))
				return FMCPToolResult::Error(TEXT("event_name is required"));

			FString FunctionName;
			Args->TryGetStringField(TEXT("function_name"), FunctionName);

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			UWidget* Widget = FindWidgetByName(WBP, WidgetName);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

			// Map event name to the actual multicast delegate property name on the widget class
			// UE convention: OnClicked maps to "OnClicked" delegate, etc.
			FName DelegateName = FName(*EventName);

			// Verify the delegate property exists on this widget type
			FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(
				Widget->GetClass(), DelegateName);

			if (!DelegateProp)
			{
				// Try common alternative naming patterns
				FString AltName = FString::Printf(TEXT("On%s"), *EventName);
				DelegateProp = FindFProperty<FMulticastDelegateProperty>(Widget->GetClass(), FName(*AltName));

				if (!DelegateProp)
				{
					// List available delegates for the error message
					TArray<FString> AvailableDelegates;
					for (TFieldIterator<FMulticastDelegateProperty> It(Widget->GetClass()); It; ++It)
					{
						AvailableDelegates.Add(It->GetName());
					}

					return FMCPToolResult::Error(FString::Printf(
						TEXT("Event '%s' not found on widget '%s' (type: %s). Available events: %s"),
						*EventName, *WidgetName, *Widget->GetClass()->GetName(),
						AvailableDelegates.Num() > 0 ? *FString::Join(AvailableDelegates, TEXT(", ")) : TEXT("none")));
				}
				DelegateName = FName(*AltName);
			}

			// Find the event graph
			UEdGraph* EventGraph = nullptr;
			for (UEdGraph* Graph : WBP->UbergraphPages)
			{
				if (Graph && Graph->GetName() == TEXT("EventGraph"))
				{
					EventGraph = Graph;
					break;
				}
			}

			if (!EventGraph)
			{
				return FMCPToolResult::Error(TEXT("Could not find EventGraph in Widget Blueprint"));
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Bind Widget Event")));
			WBP->Modify();
			EventGraph->Modify();

			// Create the K2Node_ComponentBoundEvent
			UK2Node_ComponentBoundEvent* EventNode = NewObject<UK2Node_ComponentBoundEvent>(EventGraph);
			EventNode->DelegatePropertyName = DelegateName;
			EventNode->DelegateOwnerClass = Widget->GetClass();
			EventNode->ComponentPropertyName = FName(*WidgetName);

			// Position the node
			EventNode->NodePosX = 0;
			EventNode->NodePosY = EventGraph->Nodes.Num() * 200; // Stagger vertically

			EventGraph->AddNode(EventNode, false, false);
			EventNode->CreateNewGuid();
			EventNode->PostPlacedNewNode();
			EventNode->AllocateDefaultPins();

			FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);

			GEditor->EndTransaction();
			SaveWidgetBlueprint(WBP);

			// Build result message with node info
			FString NodeId = EventNode->NodeGuid.ToString();
			TArray<FString> OutputPins;
			for (UEdGraphPin* Pin : EventNode->Pins)
			{
				if (Pin->Direction == EGPD_Output)
				{
					OutputPins.Add(FString::Printf(TEXT("%s (%s)"),
						*Pin->PinName.ToString(),
						*Pin->PinType.PinCategory.ToString()));
				}
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created event binding for '%s.%s' in %s.\nNode ID: %s\nOutput pins: %s\n\nUse Blueprint tools (connect_pins, add_function_call_node) to wire this event to game logic."),
				*WidgetName, *EventName, *AssetPath,
				*NodeId,
				*FString::Join(OutputPins, TEXT(", "))));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// batch_add_widgets - Add multiple widgets in one call
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("widgets_json"), TEXT("JSON array of widget definitions: [{\"type\":\"TextBlock\",\"name\":\"Txt_Title\",\"parent\":\"RootPanel\"}, ...]"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("batch_add_widgets");
		Def.Description = TEXT("Add multiple widgets to a Widget Blueprint in one call. Provide a JSON array of widget definitions, each with 'type' (required), 'name' (optional), and 'parent' (optional, defaults to root). Much faster than individual add_widget calls for building complex UIs.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString WidgetsJson;
			if (!Args->TryGetStringField(TEXT("widgets_json"), WidgetsJson))
				return FMCPToolResult::Error(TEXT("widgets_json is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			if (!WBP->WidgetTree) return FMCPToolResult::Error(TEXT("No WidgetTree"));

			// Parse the JSON array
			TArray<TSharedPtr<FJsonValue>> WidgetDefs;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(WidgetsJson);
			if (!FJsonSerializer::Deserialize(Reader, WidgetDefs))
				return FMCPToolResult::Error(TEXT("Failed to parse widgets_json. Expected JSON array of objects."));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Batch Add Widgets")));
			WBP->Modify();

			TArray<FString> Added;
			TArray<FString> Failed;

			for (const TSharedPtr<FJsonValue>& WidgetVal : WidgetDefs)
			{
				TSharedPtr<FJsonObject> WidgetDef = WidgetVal->AsObject();
				if (!WidgetDef) { Failed.Add(TEXT("invalid entry")); continue; }

				FString WidgetType;
				if (!WidgetDef->TryGetStringField(TEXT("type"), WidgetType)) { Failed.Add(TEXT("missing type")); continue; }

				UClass* WidgetClass = WidgetClassFromTypeName(WidgetType);
				if (!WidgetClass) { Failed.Add(FString::Printf(TEXT("unknown type: %s"), *WidgetType)); continue; }

				FString WidgetName = WidgetType;
				WidgetDef->TryGetStringField(TEXT("name"), WidgetName);

				// Find parent
				UPanelWidget* ParentPanel = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget);
				FString ParentName;
				if (WidgetDef->TryGetStringField(TEXT("parent"), ParentName) && !ParentName.IsEmpty())
				{
					UWidget* ParentWidget = FindWidgetByName(WBP, ParentName);
					if (ParentWidget)
					{
						UPanelWidget* PP = Cast<UPanelWidget>(ParentWidget);
						if (PP) ParentPanel = PP;
					}
				}

				if (!ParentPanel) { Failed.Add(FString::Printf(TEXT("%s: no parent panel"), *WidgetName)); continue; }

				UWidget* NewWidget = WBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*WidgetName));
				if (!NewWidget) { Failed.Add(FString::Printf(TEXT("%s: construct failed"), *WidgetName)); continue; }

				UPanelSlot* Slot = ParentPanel->AddChild(NewWidget);
				if (!Slot) { Failed.Add(FString::Printf(TEXT("%s: add child failed"), *WidgetName)); continue; }

				Added.Add(FString::Printf(TEXT("%s (%s)"), *NewWidget->GetName(), *WidgetType));
			}

			GEditor->EndTransaction();
			SaveWidgetBlueprint(WBP);

			FString Result = FString::Printf(TEXT("Added %d widget(s)"), Added.Num());
			if (Added.Num() > 0) Result += FString::Printf(TEXT(": %s"), *FString::Join(Added, TEXT(", ")));
			if (Failed.Num() > 0) Result += FString::Printf(TEXT(". Failed %d: %s"), Failed.Num(), *FString::Join(Failed, TEXT(", ")));

			return FMCPToolResult::Success(Result);
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// batch_set_widget_properties - Set properties on multiple widgets
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("operations_json"), TEXT("JSON array of operations: [{\"widget\":\"Txt_Title\",\"text\":\"Hello\",\"font_size\":24}, {\"widget\":\"Img_Icon\",\"render_opacity\":0.5}]"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("batch_set_widget_properties");
		Def.Description = TEXT("Set properties on multiple widgets in one call. Each operation specifies a widget name and the properties to set (same as set_widget_properties: text, font_size, visibility, render_opacity, is_enabled, tooltip_text, color_r/g/b/a, percent, width_override, height_override, justification). Much faster than individual calls.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString OpsJson;
			if (!Args->TryGetStringField(TEXT("operations_json"), OpsJson))
				return FMCPToolResult::Error(TEXT("operations_json is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
			if (!WBP->WidgetTree) return FMCPToolResult::Error(TEXT("No WidgetTree"));

			TArray<TSharedPtr<FJsonValue>> Operations;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OpsJson);
			if (!FJsonSerializer::Deserialize(Reader, Operations))
				return FMCPToolResult::Error(TEXT("Failed to parse operations_json. Expected JSON array of objects."));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Batch Set Widget Properties")));
			WBP->Modify();

			TArray<FString> Succeeded;
			TArray<FString> Failed;

			for (const TSharedPtr<FJsonValue>& OpVal : Operations)
			{
				TSharedPtr<FJsonObject> Op = OpVal->AsObject();
				if (!Op) { Failed.Add(TEXT("invalid entry")); continue; }

				FString WidgetName;
				if (!Op->TryGetStringField(TEXT("widget"), WidgetName))
				{
					Failed.Add(TEXT("missing 'widget' field"));
					continue;
				}

				UWidget* Widget = FindWidgetByName(WBP, WidgetName);
				if (!Widget)
				{
					Failed.Add(FString::Printf(TEXT("%s: not found"), *WidgetName));
					continue;
				}

				TArray<FString> Changed;

				// Common: Visibility
				FString VisStr;
				if (Op->TryGetStringField(TEXT("visibility"), VisStr))
				{
					if (VisStr == TEXT("Visible")) Widget->SetVisibility(ESlateVisibility::Visible);
					else if (VisStr == TEXT("Collapsed")) Widget->SetVisibility(ESlateVisibility::Collapsed);
					else if (VisStr == TEXT("Hidden")) Widget->SetVisibility(ESlateVisibility::Hidden);
					else if (VisStr == TEXT("HitTestInvisible")) Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
					else if (VisStr == TEXT("SelfHitTestInvisible")) Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
					Changed.Add(TEXT("visibility"));
				}

				// Common: IsEnabled
				bool bEnabled;
				if (Op->TryGetBoolField(TEXT("is_enabled"), bEnabled))
				{
					Widget->SetIsEnabled(bEnabled);
					Changed.Add(TEXT("is_enabled"));
				}

				// Common: RenderOpacity
				if (Op->HasField(TEXT("render_opacity")))
				{
					Widget->SetRenderOpacity((float)Op->GetNumberField(TEXT("render_opacity")));
					Changed.Add(TEXT("render_opacity"));
				}

				// Common: ToolTipText
				FString TooltipStr;
				if (Op->TryGetStringField(TEXT("tooltip_text"), TooltipStr))
				{
					Widget->SetToolTipText(FText::FromString(TooltipStr));
					Changed.Add(TEXT("tooltip"));
				}

				// Color
				bool bHasColor = Op->HasField(TEXT("color_r")) || Op->HasField(TEXT("color_g"))
					|| Op->HasField(TEXT("color_b")) || Op->HasField(TEXT("color_a"));
				FLinearColor Color(
					Op->HasField(TEXT("color_r")) ? (float)Op->GetNumberField(TEXT("color_r")) : 1.0f,
					Op->HasField(TEXT("color_g")) ? (float)Op->GetNumberField(TEXT("color_g")) : 1.0f,
					Op->HasField(TEXT("color_b")) ? (float)Op->GetNumberField(TEXT("color_b")) : 1.0f,
					Op->HasField(TEXT("color_a")) ? (float)Op->GetNumberField(TEXT("color_a")) : 1.0f
				);

				// TextBlock
				if (UTextBlock* TB = Cast<UTextBlock>(Widget))
				{
					FString Text;
					if (Op->TryGetStringField(TEXT("text"), Text))
					{
						TB->SetText(FText::FromString(Text));
						Changed.Add(TEXT("text"));
					}
					if (Op->HasField(TEXT("font_size")))
					{
						FSlateFontInfo FontInfo = TB->GetFont();
						FontInfo.Size = (int32)Op->GetNumberField(TEXT("font_size"));
						TB->SetFont(FontInfo);
						Changed.Add(TEXT("font_size"));
					}
					FString JustStr;
					if (Op->TryGetStringField(TEXT("justification"), JustStr))
					{
						if (JustStr == TEXT("Left")) TB->SetJustification(ETextJustify::Left);
						else if (JustStr == TEXT("Center")) TB->SetJustification(ETextJustify::Center);
						else if (JustStr == TEXT("Right")) TB->SetJustification(ETextJustify::Right);
						Changed.Add(TEXT("justification"));
					}
					if (bHasColor) { TB->SetColorAndOpacity(FSlateColor(Color)); Changed.Add(TEXT("color")); }
				}
				else if (UButton* Btn = Cast<UButton>(Widget))
				{
					if (bHasColor) { Btn->SetBackgroundColor(Color); Changed.Add(TEXT("color")); }
				}
				else if (UImage* Img = Cast<UImage>(Widget))
				{
					if (bHasColor) { Img->SetColorAndOpacity(Color); Changed.Add(TEXT("color")); }
				}
				else if (UProgressBar* PB = Cast<UProgressBar>(Widget))
				{
					if (Op->HasField(TEXT("percent")))
					{
						PB->SetPercent((float)Op->GetNumberField(TEXT("percent")));
						Changed.Add(TEXT("percent"));
					}
					if (bHasColor) { PB->SetFillColorAndOpacity(Color); Changed.Add(TEXT("color")); }
				}
				else if (USizeBox* SB = Cast<USizeBox>(Widget))
				{
					if (Op->HasField(TEXT("width_override")))
					{
						SB->SetWidthOverride((float)Op->GetNumberField(TEXT("width_override")));
						Changed.Add(TEXT("width"));
					}
					if (Op->HasField(TEXT("height_override")))
					{
						SB->SetHeightOverride((float)Op->GetNumberField(TEXT("height_override")));
						Changed.Add(TEXT("height"));
					}
				}

				if (Changed.Num() > 0)
					Succeeded.Add(FString::Printf(TEXT("%s: %s"), *WidgetName, *FString::Join(Changed, TEXT(","))));
				else
					Succeeded.Add(FString::Printf(TEXT("%s: no applicable changes"), *WidgetName));
			}

			GEditor->EndTransaction();
			SaveWidgetBlueprint(WBP);

			FString Result = FString::Printf(TEXT("Updated %d widget(s)"), Succeeded.Num());
			if (Succeeded.Num() > 0) Result += FString::Printf(TEXT(": %s"), *FString::Join(Succeeded, TEXT("; ")));
			if (Failed.Num() > 0) Result += FString::Printf(TEXT(". Failed %d: %s"), Failed.Num(), *FString::Join(Failed, TEXT(", ")));

			return FMCPToolResult::Success(Result);
		});
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPWidgetTools
