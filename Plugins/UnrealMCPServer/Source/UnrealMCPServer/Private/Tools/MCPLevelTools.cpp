// Copyright StraySpark 2026 All Rights Reserved.

#include "Tools/MCPLevelTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "EditorLevelUtils.h"
#include "Misc/PackageName.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "Engine/LevelStreaming.h"

namespace MCPLevelTools
{

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// get_level_info - Current level metadata
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_level_info");
		Def.Description = TEXT("Get information about the currently loaded level: name, path, actor count, world settings, and streaming levels.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("name"), World->GetName());
			Info->SetStringField(TEXT("mapName"), World->GetMapName());

			if (World->GetOutermost())
			{
				Info->SetStringField(TEXT("package"), World->GetOutermost()->GetName());
			}

			// Actor count
			int32 ActorCount = 0;
			for (TActorIterator<AActor> It(World); It; ++It) ActorCount++;
			Info->SetNumberField(TEXT("actorCount"), ActorCount);

			// World settings
			AWorldSettings* Settings = World->GetWorldSettings();
			if (Settings)
			{
				TSharedPtr<FJsonObject> WorldSettings = MakeShared<FJsonObject>();
				WorldSettings->SetBoolField(TEXT("enableWorldBoundsChecks"), Settings->bEnableWorldBoundsChecks);
				WorldSettings->SetStringField(TEXT("gameMode"), Settings->DefaultGameMode ? Settings->DefaultGameMode->GetName() : TEXT("None"));
				Info->SetObjectField(TEXT("worldSettings"), WorldSettings);
			}

			// Streaming levels
			TArray<TSharedPtr<FJsonValue>> StreamingArray;
			for (ULevelStreaming* StreamLevel : World->GetStreamingLevels())
			{
				if (!StreamLevel) continue;
				TSharedPtr<FJsonObject> StreamObj = MakeShared<FJsonObject>();
				StreamObj->SetStringField(TEXT("name"), StreamLevel->GetWorldAssetPackageName());
				StreamObj->SetBoolField(TEXT("visible"), StreamLevel->GetShouldBeVisibleInEditor());
				StreamObj->SetBoolField(TEXT("loaded"), StreamLevel->IsLevelLoaded());
				StreamingArray.Add(MakeShared<FJsonValueObject>(StreamObj));
			}
			Info->SetArrayField(TEXT("streamingLevels"), StreamingArray);

			// Level bounds
			FBox WorldBounds(ForceInit);
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (*It)
				{
					FVector Origin, Extent;
					(*It)->GetActorBounds(false, Origin, Extent);
					if (Extent.SizeSquared() > 0)
					{
						WorldBounds += FBox(Origin - Extent, Origin + Extent);
					}
				}
			}
			if (WorldBounds.IsValid)
			{
				TSharedPtr<FJsonObject> Bounds = MakeShared<FJsonObject>();
				Bounds->SetNumberField(TEXT("minX"), WorldBounds.Min.X);
				Bounds->SetNumberField(TEXT("minY"), WorldBounds.Min.Y);
				Bounds->SetNumberField(TEXT("minZ"), WorldBounds.Min.Z);
				Bounds->SetNumberField(TEXT("maxX"), WorldBounds.Max.X);
				Bounds->SetNumberField(TEXT("maxY"), WorldBounds.Max.Y);
				Bounds->SetNumberField(TEXT("maxZ"), WorldBounds.Max.Z);
				Info->SetObjectField(TEXT("bounds"), Bounds);
			}

			return FMCPToolResult::Success(JsonToString(Info));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// new_level - Create a new empty level
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("level_path"), TEXT("Content path for the new level (e.g., '/Game/Maps/NewLevel')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("new_level");
		Def.Description = TEXT("Create and open a new empty level at the specified content path.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString LevelPath;
			if (!Args->TryGetStringField(TEXT("level_path"), LevelPath))
				return FMCPToolResult::Error(TEXT("level_path is required"));

			// Save current if dirty
			if (GEditor->GetEditorWorldContext().World()->GetOutermost()->IsDirty())
			{
				FEditorFileUtils::SaveDirtyPackages(true, true, true);
			}

			UWorld* NewWorld = GEditor->NewMap();
			if (!NewWorld) return FMCPToolResult::Error(TEXT("Failed to create new level"));

			// Save as specified path
			FString PackageFilename = FPackageName::LongPackageNameToFilename(LevelPath, FPackageName::GetMapPackageExtension());
			bool bSaved = FEditorFileUtils::SaveLevel(NewWorld->PersistentLevel, PackageFilename);

			if (!bSaved)
			{
				return FMCPToolResult::Error(TEXT("Created level but failed to save it"));
			}

			return FMCPToolResult::Success(FString::Printf(TEXT("Created and opened new level: %s"), *LevelPath));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// open_level - Load an existing level
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("level_path"), TEXT("Content path of the level to open (e.g., '/Game/Maps/MyLevel')"), true);
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("save_current"), TEXT("Save the current level before opening (default: true)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("open_level");
		Def.Description = TEXT("Open an existing level in the editor.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString LevelPath;
			if (!Args->TryGetStringField(TEXT("level_path"), LevelPath))
				return FMCPToolResult::Error(TEXT("level_path is required"));

			bool bSaveCurrent = true;
			Args->TryGetBoolField(TEXT("save_current"), bSaveCurrent);

			if (bSaveCurrent)
			{
				FEditorFileUtils::SaveDirtyPackages(true, true, true);
			}

			FString MapFilename;
			if (!FPackageName::TryConvertLongPackageNameToFilename(LevelPath, MapFilename, FPackageName::GetMapPackageExtension()))
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Invalid level path: %s"), *LevelPath));
			}

			if (!FPaths::FileExists(MapFilename))
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Level not found: %s"), *LevelPath));
			}

			FEditorFileUtils::LoadMap(LevelPath);

			return FMCPToolResult::Success(FString::Printf(TEXT("Opened level: %s"), *LevelPath));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// save_level - Save the current level
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("save_all"), TEXT("Save all dirty packages, not just the level (default: false)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("save_level");
		Def.Description = TEXT("Save the current level and optionally all dirty packages.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			bool bSaveAll = false;
			Args->TryGetBoolField(TEXT("save_all"), bSaveAll);

			if (bSaveAll)
			{
				bool bResult = FEditorFileUtils::SaveDirtyPackages(true, true, true);
				return FMCPToolResult::Success(bResult ? TEXT("Saved all dirty packages") : TEXT("Save completed with some failures"));
			}

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

			ULevel* Level = World->PersistentLevel;
			if (!Level) return FMCPToolResult::Error(TEXT("No persistent level"));

			bool bSaved = FEditorFileUtils::SaveLevel(Level);

			return bSaved
				? FMCPToolResult::Success(FString::Printf(TEXT("Saved level: %s"), *World->GetMapName()))
				: FMCPToolResult::Error(TEXT("Failed to save level"));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_world_settings - Read world settings
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_world_settings");
		Def.Description = TEXT("Read current world settings: gravity, kill-Z height, default game mode, navigation system class, and world bounds.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			AWorldSettings* WS = World->GetWorldSettings();
			if (!WS) return FMCPToolResult::Error(TEXT("No WorldSettings actor found"));

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("level_name"), World->GetMapName());

			// Gravity
			Info->SetNumberField(TEXT("gravity_z"), WS->GetGravityZ());

			// Kill Z
			Info->SetNumberField(TEXT("kill_z"), WS->KillZ);

			// Default game mode
			if (WS->DefaultGameMode)
			{
				Info->SetStringField(TEXT("default_game_mode"), WS->DefaultGameMode->GetName());
			}
			else
			{
				Info->SetStringField(TEXT("default_game_mode"), TEXT("None (project default)"));
			}

			// World bounds
			Info->SetBoolField(TEXT("enable_world_bounds_checks"), WS->bEnableWorldBoundsChecks);

			// Navigation
			Info->SetBoolField(TEXT("enable_navigation_system"), WS->IsNavigationSystemEnabled());

			return FMCPToolResult::Success(JsonToString(Info));
		});
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_world_settings - Modify world settings
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("gravity_z"), TEXT("World gravity on Z axis (default: -980). Set to -490 for moon gravity, 0 for zero-G."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("kill_z"), TEXT("Kill Z height - actors below this Z are destroyed (default: -10000)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("game_mode_class"), TEXT("Content path to Game Mode Blueprint to set as default (e.g., '/Game/BP_GameMode')"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("enable_world_bounds_checks"), TEXT("Enable world bounds checking"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_world_settings");
		Def.Description = TEXT("Modify world settings for the current level. Set gravity, kill-Z height, default game mode, and world bounds checks. Only provided fields are changed.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			AWorldSettings* WS = World->GetWorldSettings();
			if (!WS) return FMCPToolResult::Error(TEXT("No WorldSettings actor found"));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set World Settings")));
			WS->Modify();

			TArray<FString> Changed;

			if (Args->HasField(TEXT("gravity_z")))
			{
				float Gravity = (float)Args->GetNumberField(TEXT("gravity_z"));
				WS->bGlobalGravitySet = true;
				WS->GlobalGravityZ = Gravity;
				Changed.Add(FString::Printf(TEXT("GravityZ=%.1f"), Gravity));
			}

			if (Args->HasField(TEXT("kill_z")))
			{
				float KillZ = (float)Args->GetNumberField(TEXT("kill_z"));
				WS->KillZ = KillZ;
				Changed.Add(FString::Printf(TEXT("KillZ=%.1f"), KillZ));
			}

			FString GameModeStr;
			if (Args->TryGetStringField(TEXT("game_mode_class"), GameModeStr) && !GameModeStr.IsEmpty())
			{
				UBlueprint* GMBP = LoadObject<UBlueprint>(nullptr, *GameModeStr);
				UClass* GMClass = GMBP ? GMBP->GeneratedClass.Get() : nullptr;
				if (!GMClass)
				{
					// Try _C suffix
					GMClass = LoadObject<UClass>(nullptr, *(GameModeStr + TEXT("_C")));
				}
				if (GMClass && GMClass->IsChildOf(AGameModeBase::StaticClass()))
				{
					WS->DefaultGameMode = GMClass;
					Changed.Add(FString::Printf(TEXT("GameMode=%s"), *GMClass->GetName()));
				}
				else
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(FString::Printf(TEXT("Game mode class not found or invalid: %s"), *GameModeStr));
				}
			}

			bool bBoundsCheck;
			if (Args->TryGetBoolField(TEXT("enable_world_bounds_checks"), bBoundsCheck))
			{
				WS->bEnableWorldBoundsChecks = bBoundsCheck;
				Changed.Add(FString::Printf(TEXT("WorldBoundsChecks=%s"), bBoundsCheck ? TEXT("true") : TEXT("false")));
			}

			GEditor->EndTransaction();

			if (Changed.Num() == 0)
			{
				return FMCPToolResult::Success(TEXT("No world settings changed. Provide at least one parameter."));
			}

			return FMCPToolResult::Success(FString::Printf(TEXT("Updated world settings: %s"),
				*FString::Join(Changed, TEXT(", "))));
		});
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPLevelTools
