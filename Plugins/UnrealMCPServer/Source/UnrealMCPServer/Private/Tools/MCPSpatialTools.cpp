// Copyright StraySpark 2026 All Rights Reserved.

#include "Tools/MCPSpatialTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Engine/Engine.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

namespace MCPSpatialTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

static AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == Label)
		{
			return *It;
		}
	}
	return nullptr;
}

static TSharedPtr<FJsonObject> MakeVectorJson(const FVector& V)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("x"), V.X);
	Obj->SetNumberField(TEXT("y"), V.Y);
	Obj->SetNumberField(TEXT("z"), V.Z);
	return Obj;
}

static ECollisionChannel ParseTraceChannel(const FString& ChannelName)
{
	if (ChannelName == TEXT("Visibility")) return ECC_Visibility;
	if (ChannelName == TEXT("Camera")) return ECC_Camera;
	if (ChannelName == TEXT("WorldStatic")) return ECC_WorldStatic;
	if (ChannelName == TEXT("WorldDynamic")) return ECC_WorldDynamic;
	if (ChannelName == TEXT("Pawn")) return ECC_Pawn;
	if (ChannelName == TEXT("PhysicsBody")) return ECC_PhysicsBody;
	return ECC_Visibility;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// get_actor_bounds - Get world-space bounding box of an actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor to get bounds for"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_actor_bounds");
		Def.Description = TEXT("Get the world-space bounding box of an actor. Returns origin, extent, min/max corners, size, and center. The fundamental tool for understanding actor dimensions.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			FVector Origin, BoxExtent;
			Actor->GetActorBounds(false, Origin, BoxExtent);

			FVector Min = Origin - BoxExtent;
			FVector Max = Origin + BoxExtent;
			FVector Size = BoxExtent * 2.0;

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actor"), ActorName);
			Result->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
			Result->SetObjectField(TEXT("origin"), MakeVectorJson(Origin));
			Result->SetObjectField(TEXT("extent"), MakeVectorJson(BoxExtent));
			Result->SetObjectField(TEXT("min"), MakeVectorJson(Min));
			Result->SetObjectField(TEXT("max"), MakeVectorJson(Max));
			Result->SetObjectField(TEXT("size"), MakeVectorJson(Size));
			Result->SetObjectField(TEXT("center"), MakeVectorJson(Origin));

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_mesh_asset_bounds - Get bounding box of a StaticMesh asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("mesh_path"), TEXT("Content path of the static mesh asset (e.g., '/Game/Meshes/SM_Wall')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_mesh_asset_bounds");
		Def.Description = TEXT("Get the bounding box of a StaticMesh ASSET (before placing it in the level). Critical for calculating how many pieces span a distance or how things fit together.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString MeshPath;
			if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath))
				return FMCPToolResult::Error(TEXT("mesh_path is required"));

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!Mesh) return FMCPToolResult::Error(FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));

			FBox BoundingBox = Mesh->GetBoundingBox();
			FVector BoundsMin = BoundingBox.Min;
			FVector BoundsMax = BoundingBox.Max;
			FVector BoundsSize = BoundsMax - BoundsMin;
			double SphereRadius = Mesh->GetBounds().SphereRadius;

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("mesh_path"), MeshPath);
			Result->SetStringField(TEXT("mesh_name"), Mesh->GetName());
			Result->SetObjectField(TEXT("bounds_min"), MakeVectorJson(BoundsMin));
			Result->SetObjectField(TEXT("bounds_max"), MakeVectorJson(BoundsMax));
			Result->SetObjectField(TEXT("bounds_size"), MakeVectorJson(BoundsSize));
			Result->SetNumberField(TEXT("bounding_sphere_radius"), SphereRadius);

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// line_trace - Cast a ray and report what it hits
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("start_x"), TEXT("Start X position"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("start_y"), TEXT("Start Y position"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("start_z"), TEXT("Start Z position"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("end_x"), TEXT("End X position"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("end_y"), TEXT("End Y position"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("end_z"), TEXT("End Z position"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("trace_channel"), TEXT("Collision channel (default: 'Visibility'). Options: Visibility, Camera, WorldStatic, WorldDynamic, Pawn, PhysicsBody"));
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("ignore_actors"), TEXT("Array of actor labels to ignore during the trace"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("line_trace");
		Def.Description = TEXT("Cast a ray from A to B and report what it hits. Critical for finding ground level, checking line of sight, and placing on surfaces.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FVector Start(
				Args->GetNumberField(TEXT("start_x")),
				Args->GetNumberField(TEXT("start_y")),
				Args->GetNumberField(TEXT("start_z"))
			);
			FVector End(
				Args->GetNumberField(TEXT("end_x")),
				Args->GetNumberField(TEXT("end_y")),
				Args->GetNumberField(TEXT("end_z"))
			);

			FString ChannelStr = TEXT("Visibility");
			Args->TryGetStringField(TEXT("trace_channel"), ChannelStr);
			ECollisionChannel Channel = ParseTraceChannel(ChannelStr);

			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MCPLineTrace), true);

			// Add actors to ignore
			if (Args->HasField(TEXT("ignore_actors")))
			{
				TArray<TSharedPtr<FJsonValue>> IgnoreNames = Args->GetArrayField(TEXT("ignore_actors"));
				for (const auto& Val : IgnoreNames)
				{
					FString Name;
					if (Val->TryGetString(Name))
					{
						AActor* IgnoreActor = FindActorByLabel(World, Name);
						if (IgnoreActor)
						{
							QueryParams.AddIgnoredActor(IgnoreActor);
						}
					}
				}
			}

			FHitResult HitResult;
			bool bHit = World->LineTraceSingleByChannel(HitResult, Start, End, Channel, QueryParams);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("hit"), bHit);

			if (bHit)
			{
				Result->SetObjectField(TEXT("hit_location"), MakeVectorJson(HitResult.Location));
				Result->SetObjectField(TEXT("hit_normal"), MakeVectorJson(HitResult.Normal));
				Result->SetNumberField(TEXT("hit_distance"), HitResult.Distance);

				if (HitResult.GetActor())
				{
					Result->SetStringField(TEXT("hit_actor"), HitResult.GetActor()->GetActorLabel());
					Result->SetStringField(TEXT("hit_actor_class"), HitResult.GetActor()->GetClass()->GetName());
				}
				if (HitResult.GetComponent())
				{
					Result->SetStringField(TEXT("hit_component"), HitResult.GetComponent()->GetName());
				}

				// Try to get physical material name
				if (HitResult.PhysMaterial.IsValid())
				{
					Result->SetStringField(TEXT("hit_material"), HitResult.PhysMaterial->GetName());
				}
			}

			Result->SetObjectField(TEXT("trace_start"), MakeVectorJson(Start));
			Result->SetObjectField(TEXT("trace_end"), MakeVectorJson(End));
			Result->SetNumberField(TEXT("trace_length"), FVector::Dist(Start, End));

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// overlap_test - Check if something overlaps at a position
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Actor to test overlap for (uses actor's current bounds). Mutually exclusive with test_x/y/z + test_extent_x/y/z."));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("test_x"), TEXT("X position of test box center (use with test_extent)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("test_y"), TEXT("Y position of test box center"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("test_z"), TEXT("Z position of test box center"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("test_extent_x"), TEXT("Half-extent X of test box (default: 50)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("test_extent_y"), TEXT("Half-extent Y of test box (default: 50)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("test_extent_z"), TEXT("Half-extent Z of test box (default: 50)"));
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("ignore_actors"), TEXT("Array of actor labels to ignore"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("overlap_test");
		Def.Description = TEXT("Check if an actor overlaps with anything at its current position, or test a hypothetical box at a position. Critical for collision-free placement.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FVector TestCenter;
			FVector TestExtent(50.0, 50.0, 50.0);
			AActor* TestActor = nullptr;

			FString ActorName;
			if (Args->TryGetStringField(TEXT("actor_name"), ActorName))
			{
				TestActor = FindActorByLabel(World, ActorName);
				if (!TestActor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

				FVector Origin, BoxExtent;
				TestActor->GetActorBounds(false, Origin, BoxExtent);
				TestCenter = Origin;
				TestExtent = BoxExtent;
			}
			else if (Args->HasField(TEXT("test_x")))
			{
				TestCenter.X = Args->GetNumberField(TEXT("test_x"));
				TestCenter.Y = Args->HasField(TEXT("test_y")) ? Args->GetNumberField(TEXT("test_y")) : 0.0;
				TestCenter.Z = Args->HasField(TEXT("test_z")) ? Args->GetNumberField(TEXT("test_z")) : 0.0;

				if (Args->HasField(TEXT("test_extent_x"))) TestExtent.X = Args->GetNumberField(TEXT("test_extent_x"));
				if (Args->HasField(TEXT("test_extent_y"))) TestExtent.Y = Args->GetNumberField(TEXT("test_extent_y"));
				if (Args->HasField(TEXT("test_extent_z"))) TestExtent.Z = Args->GetNumberField(TEXT("test_extent_z"));
			}
			else
			{
				return FMCPToolResult::Error(TEXT("Provide either actor_name or test_x/y/z position"));
			}

			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MCPOverlapTest), true);
			if (TestActor)
			{
				QueryParams.AddIgnoredActor(TestActor);
			}

			// Add actors to ignore
			if (Args->HasField(TEXT("ignore_actors")))
			{
				TArray<TSharedPtr<FJsonValue>> IgnoreNames = Args->GetArrayField(TEXT("ignore_actors"));
				for (const auto& Val : IgnoreNames)
				{
					FString Name;
					if (Val->TryGetString(Name))
					{
						AActor* IgnoreActor = FindActorByLabel(World, Name);
						if (IgnoreActor) QueryParams.AddIgnoredActor(IgnoreActor);
					}
				}
			}

			FCollisionShape Shape = FCollisionShape::MakeBox(TestExtent);
			TArray<FOverlapResult> Overlaps;
			bool bHasOverlap = World->OverlapMultiByChannel(Overlaps, TestCenter, FQuat::Identity, ECC_WorldStatic, Shape, QueryParams);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("has_overlap"), bHasOverlap);
			Result->SetObjectField(TEXT("test_center"), MakeVectorJson(TestCenter));
			Result->SetObjectField(TEXT("test_extent"), MakeVectorJson(TestExtent));

			TArray<TSharedPtr<FJsonValue>> OverlapArray;
			TSet<AActor*> SeenActors;
			for (const FOverlapResult& Overlap : Overlaps)
			{
				AActor* OverlapActor = Overlap.GetActor();
				if (!OverlapActor || SeenActors.Contains(OverlapActor)) continue;
				SeenActors.Add(OverlapActor);

				TSharedPtr<FJsonObject> OverlapObj = MakeShared<FJsonObject>();
				OverlapObj->SetStringField(TEXT("name"), OverlapActor->GetActorLabel());
				OverlapObj->SetStringField(TEXT("class"), OverlapActor->GetClass()->GetName());

				// Compute approximate overlap distance
				FVector OtherOrigin, OtherExtent;
				OverlapActor->GetActorBounds(false, OtherOrigin, OtherExtent);
				double Dist = FVector::Dist(TestCenter, OtherOrigin);
				OverlapObj->SetNumberField(TEXT("distance_to_center"), Dist);

				OverlapArray.Add(MakeShared<FJsonValueObject>(OverlapObj));
			}
			Result->SetArrayField(TEXT("overlapping_actors"), OverlapArray);
			Result->SetNumberField(TEXT("overlap_count"), OverlapArray.Num());

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// place_actor_on_ground - Drop an actor to sit on the surface below
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor to place on ground"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("offset_z"), TEXT("Extra height above the ground surface (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("place_actor_on_ground");
		Def.Description = TEXT("Move an actor down (or up) to sit on the ground/surface below it. Traces straight down to find the surface, then positions the actor so its bottom sits on that surface.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			double OffsetZ = 0.0;
			if (Args->HasField(TEXT("offset_z"))) OffsetZ = Args->GetNumberField(TEXT("offset_z"));

			// Get actor bounds
			FVector Origin, BoxExtent;
			Actor->GetActorBounds(false, Origin, BoxExtent);

			// Trace from actor center straight down
			FVector TraceStart = Origin;
			FVector TraceEnd = Origin - FVector(0, 0, 100000.0); // 1km down

			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MCPPlaceOnGround), true);
			QueryParams.AddIgnoredActor(Actor);

			FHitResult HitResult;
			bool bHit = World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams);

			if (!bHit)
			{
				return FMCPToolResult::Error(TEXT("No ground surface found below the actor"));
			}

			// Calculate new position: hit point + half bounds height + offset
			FVector ActorLocation = Actor->GetActorLocation();
			double ActorBottomOffset = Origin.Z - BoxExtent.Z - ActorLocation.Z;
			FVector NewLocation = ActorLocation;
			NewLocation.Z = HitResult.Location.Z - ActorBottomOffset + OffsetZ;

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Place Actor On Ground")));
			Actor->Modify();
			Actor->SetActorLocation(NewLocation);
			GEditor->EndTransaction();

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actor"), ActorName);
			Result->SetObjectField(TEXT("new_position"), MakeVectorJson(NewLocation));
			Result->SetObjectField(TEXT("ground_hit_point"), MakeVectorJson(HitResult.Location));
			Result->SetObjectField(TEXT("ground_normal"), MakeVectorJson(HitResult.Normal));

			if (HitResult.GetActor())
			{
				Result->SetStringField(TEXT("surface_actor"), HitResult.GetActor()->GetActorLabel());
			}
			if (HitResult.PhysMaterial.IsValid())
			{
				Result->SetStringField(TEXT("surface_material"), HitResult.PhysMaterial->GetName());
			}

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// align_actors - Align/snap actors relative to each other or grid
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("actor_names"), TEXT("Array of actor labels to align"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("align_mode"), TEXT("Alignment mode"),
			{ TEXT("min_x"), TEXT("max_x"), TEXT("center_x"),
			  TEXT("min_y"), TEXT("max_y"), TEXT("center_y"),
			  TEXT("min_z"), TEXT("max_z"), TEXT("center_z"),
			  TEXT("grid") }, true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("grid_size"), TEXT("Grid cell size for 'grid' mode (default: 100)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("align_actors");
		Def.Description = TEXT("Align/snap actors relative to each other (min/max/center on any axis) or snap them all to a grid.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			TArray<TSharedPtr<FJsonValue>> Names = Args->GetArrayField(TEXT("actor_names"));
			if (Names.Num() < 1) return FMCPToolResult::Error(TEXT("At least one actor name required"));

			FString AlignMode;
			if (!Args->TryGetStringField(TEXT("align_mode"), AlignMode))
				return FMCPToolResult::Error(TEXT("align_mode is required"));

			double GridSize = 100.0;
			if (Args->HasField(TEXT("grid_size"))) GridSize = Args->GetNumberField(TEXT("grid_size"));
			if (GridSize <= 0) GridSize = 100.0;

			// Find all actors
			TArray<AActor*> Actors;
			for (const auto& Val : Names)
			{
				FString Name;
				if (Val->TryGetString(Name))
				{
					AActor* Actor = FindActorByLabel(World, Name);
					if (Actor) Actors.Add(Actor);
				}
			}

			if (Actors.Num() == 0) return FMCPToolResult::Error(TEXT("No valid actors found"));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Align Actors")));

			if (AlignMode == TEXT("grid"))
			{
				// Snap each actor to nearest grid point
				for (AActor* Actor : Actors)
				{
					Actor->Modify();
					FVector Loc = Actor->GetActorLocation();
					Loc.X = FMath::RoundToDouble(Loc.X / GridSize) * GridSize;
					Loc.Y = FMath::RoundToDouble(Loc.Y / GridSize) * GridSize;
					Loc.Z = FMath::RoundToDouble(Loc.Z / GridSize) * GridSize;
					Actor->SetActorLocation(Loc);
				}
			}
			else
			{
				// Calculate alignment target from first actor
				// For min/max modes, find the extreme value among all actors
				double AlignTarget = 0.0;

				if (AlignMode == TEXT("min_x") || AlignMode == TEXT("max_x") || AlignMode == TEXT("center_x") ||
					AlignMode == TEXT("min_y") || AlignMode == TEXT("max_y") || AlignMode == TEXT("center_y") ||
					AlignMode == TEXT("min_z") || AlignMode == TEXT("max_z") || AlignMode == TEXT("center_z"))
				{
					bool bFirst = true;
					for (AActor* Actor : Actors)
					{
						FVector Origin, BoxExtent;
						Actor->GetActorBounds(false, Origin, BoxExtent);

						double Val = 0.0;
						if (AlignMode == TEXT("min_x")) Val = Origin.X - BoxExtent.X;
						else if (AlignMode == TEXT("max_x")) Val = Origin.X + BoxExtent.X;
						else if (AlignMode == TEXT("center_x")) Val = Origin.X;
						else if (AlignMode == TEXT("min_y")) Val = Origin.Y - BoxExtent.Y;
						else if (AlignMode == TEXT("max_y")) Val = Origin.Y + BoxExtent.Y;
						else if (AlignMode == TEXT("center_y")) Val = Origin.Y;
						else if (AlignMode == TEXT("min_z")) Val = Origin.Z - BoxExtent.Z;
						else if (AlignMode == TEXT("max_z")) Val = Origin.Z + BoxExtent.Z;
						else if (AlignMode == TEXT("center_z")) Val = Origin.Z;

						if (bFirst)
						{
							AlignTarget = Val;
							bFirst = false;
						}
						else
						{
							if (AlignMode.StartsWith(TEXT("min"))) AlignTarget = FMath::Min(AlignTarget, Val);
							else if (AlignMode.StartsWith(TEXT("max"))) AlignTarget = FMath::Max(AlignTarget, Val);
							else AlignTarget = (AlignTarget + Val) / 2.0; // average for center
						}
					}

					// For center mode, compute true average
					if (AlignMode.StartsWith(TEXT("center")))
					{
						double Sum = 0.0;
						for (AActor* Actor : Actors)
						{
							FVector Origin, BoxExtent;
							Actor->GetActorBounds(false, Origin, BoxExtent);
							if (AlignMode == TEXT("center_x")) Sum += Origin.X;
							else if (AlignMode == TEXT("center_y")) Sum += Origin.Y;
							else if (AlignMode == TEXT("center_z")) Sum += Origin.Z;
						}
						AlignTarget = Sum / Actors.Num();
					}

					// Apply alignment
					for (AActor* Actor : Actors)
					{
						Actor->Modify();
						FVector Loc = Actor->GetActorLocation();
						FVector Origin, BoxExtent;
						Actor->GetActorBounds(false, Origin, BoxExtent);

						double CurrentBoundsCenter = 0.0;
						double CurrentBoundsEdge = 0.0;

						if (AlignMode == TEXT("min_x"))
						{
							double CurrentMin = Origin.X - BoxExtent.X;
							Loc.X += AlignTarget - CurrentMin;
						}
						else if (AlignMode == TEXT("max_x"))
						{
							double CurrentMax = Origin.X + BoxExtent.X;
							Loc.X += AlignTarget - CurrentMax;
						}
						else if (AlignMode == TEXT("center_x"))
						{
							Loc.X += AlignTarget - Origin.X;
						}
						else if (AlignMode == TEXT("min_y"))
						{
							double CurrentMin = Origin.Y - BoxExtent.Y;
							Loc.Y += AlignTarget - CurrentMin;
						}
						else if (AlignMode == TEXT("max_y"))
						{
							double CurrentMax = Origin.Y + BoxExtent.Y;
							Loc.Y += AlignTarget - CurrentMax;
						}
						else if (AlignMode == TEXT("center_y"))
						{
							Loc.Y += AlignTarget - Origin.Y;
						}
						else if (AlignMode == TEXT("min_z"))
						{
							double CurrentMin = Origin.Z - BoxExtent.Z;
							Loc.Z += AlignTarget - CurrentMin;
						}
						else if (AlignMode == TEXT("max_z"))
						{
							double CurrentMax = Origin.Z + BoxExtent.Z;
							Loc.Z += AlignTarget - CurrentMax;
						}
						else if (AlignMode == TEXT("center_z"))
						{
							Loc.Z += AlignTarget - Origin.Z;
						}

						Actor->SetActorLocation(Loc);
					}
				}
			}

			GEditor->EndTransaction();

			// Build result with new positions
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("align_mode"), AlignMode);
			Result->SetNumberField(TEXT("actor_count"), Actors.Num());

			TArray<TSharedPtr<FJsonValue>> PositionsArray;
			for (AActor* Actor : Actors)
			{
				TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
				PosObj->SetStringField(TEXT("name"), Actor->GetActorLabel());
				PosObj->SetObjectField(TEXT("position"), MakeVectorJson(Actor->GetActorLocation()));
				PositionsArray.Add(MakeShared<FJsonValueObject>(PosObj));
			}
			Result->SetArrayField(TEXT("new_positions"), PositionsArray);

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// stack_actors - Stack actors along an axis with proper spacing
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddStringArray(Schema, TEXT("actor_names"), TEXT("Array of actor labels to stack, in order"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("direction"), TEXT("Stacking direction"),
			{ TEXT("up"), TEXT("right"), TEXT("forward") }, true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("gap"), TEXT("Gap between stacked actors in units (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("stack_actors");
		Def.Description = TEXT("Stack actors on top of / next to each other with proper spacing based on their bounds. Critical for building walls, stacking crates, assembling modular pieces.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			TArray<TSharedPtr<FJsonValue>> Names = Args->GetArrayField(TEXT("actor_names"));
			if (Names.Num() < 2) return FMCPToolResult::Error(TEXT("At least two actor names required for stacking"));

			FString Direction;
			if (!Args->TryGetStringField(TEXT("direction"), Direction))
				return FMCPToolResult::Error(TEXT("direction is required"));

			double Gap = 0.0;
			if (Args->HasField(TEXT("gap"))) Gap = Args->GetNumberField(TEXT("gap"));

			// Find actors in order
			TArray<AActor*> Actors;
			for (const auto& Val : Names)
			{
				FString Name;
				if (Val->TryGetString(Name))
				{
					AActor* Actor = FindActorByLabel(World, Name);
					if (Actor) Actors.Add(Actor);
				}
			}

			if (Actors.Num() < 2) return FMCPToolResult::Error(TEXT("Need at least 2 valid actors to stack"));

			// Determine axis: up=Z, right=Y, forward=X
			int32 Axis = 2; // Z by default (up)
			if (Direction == TEXT("right")) Axis = 1; // Y
			else if (Direction == TEXT("forward")) Axis = 0; // X

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Stack Actors")));

			// First actor stays in place. Each subsequent actor is placed after the previous one.
			for (int32 i = 1; i < Actors.Num(); i++)
			{
				AActor* PrevActor = Actors[i - 1];
				AActor* CurrActor = Actors[i];

				FVector PrevOrigin, PrevExtent;
				PrevActor->GetActorBounds(false, PrevOrigin, PrevExtent);

				FVector CurrOrigin, CurrExtent;
				CurrActor->GetActorBounds(false, CurrOrigin, CurrExtent);

				// Calculate where the current actor's bounds edge should start
				// (after the previous actor's bounds edge + gap)
				double PrevMax = 0.0;
				double CurrMin = 0.0;
				double CurrCenter = 0.0;

				if (Axis == 0) // X (forward)
				{
					PrevMax = PrevOrigin.X + PrevExtent.X;
					CurrMin = CurrOrigin.X - CurrExtent.X;
					CurrCenter = CurrOrigin.X;
				}
				else if (Axis == 1) // Y (right)
				{
					PrevMax = PrevOrigin.Y + PrevExtent.Y;
					CurrMin = CurrOrigin.Y - CurrExtent.Y;
					CurrCenter = CurrOrigin.Y;
				}
				else // Z (up)
				{
					PrevMax = PrevOrigin.Z + PrevExtent.Z;
					CurrMin = CurrOrigin.Z - CurrExtent.Z;
					CurrCenter = CurrOrigin.Z;
				}

				double TargetMin = PrevMax + Gap;
				double Offset = TargetMin - CurrMin;

				CurrActor->Modify();
				FVector NewLoc = CurrActor->GetActorLocation();
				if (Axis == 0) NewLoc.X += Offset;
				else if (Axis == 1) NewLoc.Y += Offset;
				else NewLoc.Z += Offset;

				CurrActor->SetActorLocation(NewLoc);
			}

			GEditor->EndTransaction();

			// Build result
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("direction"), Direction);
			Result->SetNumberField(TEXT("gap"), Gap);
			Result->SetNumberField(TEXT("actor_count"), Actors.Num());

			TArray<TSharedPtr<FJsonValue>> PositionsArray;
			for (AActor* Actor : Actors)
			{
				TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
				PosObj->SetStringField(TEXT("name"), Actor->GetActorLabel());
				PosObj->SetObjectField(TEXT("position"), MakeVectorJson(Actor->GetActorLocation()));

				FVector Origin, Extent;
				Actor->GetActorBounds(false, Origin, Extent);
				PosObj->SetObjectField(TEXT("bounds_size"), MakeVectorJson(Extent * 2.0));

				PositionsArray.Add(MakeShared<FJsonValueObject>(PosObj));
			}
			Result->SetArrayField(TEXT("new_positions"), PositionsArray);

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// measure_distance - Measure distance between two points or actors
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("from_actor"), TEXT("Start actor label (alternative to from_x/y/z)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("from_x"), TEXT("Start X (alternative to from_actor)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("from_y"), TEXT("Start Y"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("from_z"), TEXT("Start Z"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("to_actor"), TEXT("End actor label (alternative to to_x/y/z)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("to_x"), TEXT("End X (alternative to to_actor)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("to_y"), TEXT("End Y"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("to_z"), TEXT("End Z"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("measure_distance");
		Def.Description = TEXT("Measure distance between two actors or two points. Returns total distance, per-axis distances, and direction vector. Critical for spacing verification.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FVector From, To;

			// Resolve "from"
			FString FromActor;
			if (Args->TryGetStringField(TEXT("from_actor"), FromActor))
			{
				AActor* Actor = FindActorByLabel(World, FromActor);
				if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("From actor not found: %s"), *FromActor));
				From = Actor->GetActorLocation();
			}
			else if (Args->HasField(TEXT("from_x")))
			{
				From.X = Args->GetNumberField(TEXT("from_x"));
				From.Y = Args->HasField(TEXT("from_y")) ? Args->GetNumberField(TEXT("from_y")) : 0.0;
				From.Z = Args->HasField(TEXT("from_z")) ? Args->GetNumberField(TEXT("from_z")) : 0.0;
			}
			else
			{
				return FMCPToolResult::Error(TEXT("Provide either from_actor or from_x/y/z"));
			}

			// Resolve "to"
			FString ToActor;
			if (Args->TryGetStringField(TEXT("to_actor"), ToActor))
			{
				AActor* Actor = FindActorByLabel(World, ToActor);
				if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("To actor not found: %s"), *ToActor));
				To = Actor->GetActorLocation();
			}
			else if (Args->HasField(TEXT("to_x")))
			{
				To.X = Args->GetNumberField(TEXT("to_x"));
				To.Y = Args->HasField(TEXT("to_y")) ? Args->GetNumberField(TEXT("to_y")) : 0.0;
				To.Z = Args->HasField(TEXT("to_z")) ? Args->GetNumberField(TEXT("to_z")) : 0.0;
			}
			else
			{
				return FMCPToolResult::Error(TEXT("Provide either to_actor or to_x/y/z"));
			}

			FVector Delta = To - From;
			double Distance = FVector::Dist(From, To);
			FVector Dir = Delta.GetSafeNormal();

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("distance"), Distance);
			Result->SetNumberField(TEXT("distance_x"), FMath::Abs(Delta.X));
			Result->SetNumberField(TEXT("distance_y"), FMath::Abs(Delta.Y));
			Result->SetNumberField(TEXT("distance_z"), FMath::Abs(Delta.Z));
			Result->SetObjectField(TEXT("delta"), MakeVectorJson(Delta));
			Result->SetObjectField(TEXT("direction"), MakeVectorJson(Dir));
			Result->SetObjectField(TEXT("from"), MakeVectorJson(From));
			Result->SetObjectField(TEXT("to"), MakeVectorJson(To));

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_spatial_context - High-level spatial analysis of the scene
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("center_x"), TEXT("Center X of analysis region (default: scene center)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("center_y"), TEXT("Center Y of analysis region"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("center_z"), TEXT("Center Z of analysis region"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("radius"), TEXT("Radius of analysis region in units (default: 5000)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_spatial_context");
		Def.Description = TEXT("High-level spatial analysis of the current scene. Returns scene bounds, actor density, ground level, nearest actors, density map by quadrant, and empty spaces. Gives the AI a bird's-eye understanding of the scene layout.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			double Radius = 5000.0;
			if (Args->HasField(TEXT("radius"))) Radius = Args->GetNumberField(TEXT("radius"));

			// Gather all actors and compute scene bounds
			FVector SceneMin(MAX_dbl, MAX_dbl, MAX_dbl);
			FVector SceneMax(-MAX_dbl, -MAX_dbl, -MAX_dbl);
			TArray<AActor*> AllActors;
			double SmallestSize = MAX_dbl;
			double LargestSize = 0.0;

			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!IsValid(Actor)) continue;
				// Skip transient / editor-only actors
				if (Actor->IsA(ABrush::StaticClass())) continue;
				if (Actor->IsHidden()) continue;

				FVector Origin, BoxExtent;
				Actor->GetActorBounds(false, Origin, BoxExtent);

				// Skip actors with zero bounds (world settings, etc)
				if (BoxExtent.IsNearlyZero()) continue;

				AllActors.Add(Actor);

				FVector ActorMin = Origin - BoxExtent;
				FVector ActorMax = Origin + BoxExtent;
				SceneMin = SceneMin.ComponentMin(ActorMin);
				SceneMax = SceneMax.ComponentMax(ActorMax);

				double Size = BoxExtent.Size();
				SmallestSize = FMath::Min(SmallestSize, Size);
				LargestSize = FMath::Max(LargestSize, Size);
			}

			if (AllActors.Num() == 0)
			{
				return FMCPToolResult::Success(TEXT("{\"actor_count\": 0, \"message\": \"No actors in scene\"}"));
			}

			FVector SceneCenter = (SceneMin + SceneMax) * 0.5;
			FVector SceneSize = SceneMax - SceneMin;

			// Use provided center or scene center
			FVector AnalysisCenter = SceneCenter;
			if (Args->HasField(TEXT("center_x")))
			{
				AnalysisCenter.X = Args->GetNumberField(TEXT("center_x"));
				AnalysisCenter.Y = Args->HasField(TEXT("center_y")) ? Args->GetNumberField(TEXT("center_y")) : SceneCenter.Y;
				AnalysisCenter.Z = Args->HasField(TEXT("center_z")) ? Args->GetNumberField(TEXT("center_z")) : SceneCenter.Z;
			}

			// Filter actors within radius and find nearest
			struct FActorDist
			{
				AActor* Actor;
				double Distance;
			};
			TArray<FActorDist> ActorsInRadius;

			for (AActor* Actor : AllActors)
			{
				double Dist = FVector::Dist(Actor->GetActorLocation(), AnalysisCenter);
				if (Dist <= Radius)
				{
					ActorsInRadius.Add({Actor, Dist});
				}
			}

			// Sort by distance
			ActorsInRadius.Sort([](const FActorDist& A, const FActorDist& B) { return A.Distance < B.Distance; });

			// Nearest actors (top 10)
			TArray<TSharedPtr<FJsonValue>> NearestArray;
			int32 MaxNearest = FMath::Min(ActorsInRadius.Num(), 10);
			for (int32 i = 0; i < MaxNearest; i++)
			{
				AActor* Actor = ActorsInRadius[i].Actor;
				FVector Origin, BoxExtent;
				Actor->GetActorBounds(false, Origin, BoxExtent);

				TSharedPtr<FJsonObject> NearObj = MakeShared<FJsonObject>();
				NearObj->SetStringField(TEXT("name"), Actor->GetActorLabel());
				NearObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
				NearObj->SetNumberField(TEXT("distance"), ActorsInRadius[i].Distance);
				NearObj->SetObjectField(TEXT("position"), MakeVectorJson(Actor->GetActorLocation()));
				NearObj->SetObjectField(TEXT("bounds_size"), MakeVectorJson(BoxExtent * 2.0));
				NearestArray.Add(MakeShared<FJsonValueObject>(NearObj));
			}

			// Density map (quadrants based on X/Y relative to center)
			int32 CountNW = 0, CountNE = 0, CountSW = 0, CountSE = 0;
			for (const FActorDist& AD : ActorsInRadius)
			{
				FVector Loc = AD.Actor->GetActorLocation();
				bool bEast = Loc.X >= AnalysisCenter.X;
				bool bNorth = Loc.Y >= AnalysisCenter.Y;
				if (bNorth && bEast) CountNE++;
				else if (bNorth && !bEast) CountNW++;
				else if (!bNorth && bEast) CountSE++;
				else CountSW++;
			}

			TSharedPtr<FJsonObject> DensityMap = MakeShared<FJsonObject>();
			DensityMap->SetNumberField(TEXT("NE_count"), CountNE);
			DensityMap->SetNumberField(TEXT("NW_count"), CountNW);
			DensityMap->SetNumberField(TEXT("SE_count"), CountSE);
			DensityMap->SetNumberField(TEXT("SW_count"), CountSW);
			DensityMap->SetStringField(TEXT("densest_quadrant"),
				(CountNE >= CountNW && CountNE >= CountSE && CountNE >= CountSW) ? TEXT("NE") :
				(CountNW >= CountNE && CountNW >= CountSE && CountNW >= CountSW) ? TEXT("NW") :
				(CountSE >= CountNE && CountSE >= CountNW && CountSE >= CountSW) ? TEXT("SE") : TEXT("SW"));
			DensityMap->SetStringField(TEXT("emptiest_quadrant"),
				(CountNE <= CountNW && CountNE <= CountSE && CountNE <= CountSW) ? TEXT("NE") :
				(CountNW <= CountNE && CountNW <= CountSE && CountNW <= CountSW) ? TEXT("NW") :
				(CountSE <= CountNE && CountSE <= CountNW && CountSE <= CountSW) ? TEXT("SE") : TEXT("SW"));

			// Ground level estimation: trace down from center at several points
			double GroundSum = 0.0;
			int32 GroundHits = 0;
			FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(MCPGroundTrace), true);
			TArray<FVector> SamplePoints = {
				AnalysisCenter,
				AnalysisCenter + FVector(Radius * 0.5, 0, 0),
				AnalysisCenter + FVector(-Radius * 0.5, 0, 0),
				AnalysisCenter + FVector(0, Radius * 0.5, 0),
				AnalysisCenter + FVector(0, -Radius * 0.5, 0)
			};

			for (const FVector& Pt : SamplePoints)
			{
				FVector TraceStart(Pt.X, Pt.Y, SceneMax.Z + 1000.0);
				FVector TraceEnd(Pt.X, Pt.Y, SceneMin.Z - 1000.0);
				FHitResult Hit;
				if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, GroundParams))
				{
					GroundSum += Hit.Location.Z;
					GroundHits++;
				}
			}

			// Empty spaces analysis: check grid cells for no actors
			TArray<TSharedPtr<FJsonValue>> EmptySpaces;
			double CellSize = Radius * 0.5;
			for (double dx = -1; dx <= 1; dx += 1.0)
			{
				for (double dy = -1; dy <= 1; dy += 1.0)
				{
					FVector CellCenter = AnalysisCenter + FVector(dx * CellSize, dy * CellSize, 0);
					bool bOccupied = false;
					for (const FActorDist& AD : ActorsInRadius)
					{
						FVector Loc = AD.Actor->GetActorLocation();
						if (FMath::Abs(Loc.X - CellCenter.X) < CellSize * 0.5 &&
							FMath::Abs(Loc.Y - CellCenter.Y) < CellSize * 0.5)
						{
							bOccupied = true;
							break;
						}
					}
					if (!bOccupied)
					{
						TSharedPtr<FJsonObject> SpaceObj = MakeShared<FJsonObject>();
						SpaceObj->SetObjectField(TEXT("center"), MakeVectorJson(CellCenter));
						SpaceObj->SetNumberField(TEXT("approximate_size"), CellSize);
						EmptySpaces.Add(MakeShared<FJsonValueObject>(SpaceObj));
					}
				}
			}

			// Build comprehensive result
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

			// Scene bounds
			TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
			BoundsObj->SetObjectField(TEXT("min"), MakeVectorJson(SceneMin));
			BoundsObj->SetObjectField(TEXT("max"), MakeVectorJson(SceneMax));
			BoundsObj->SetObjectField(TEXT("size"), MakeVectorJson(SceneSize));
			BoundsObj->SetObjectField(TEXT("center"), MakeVectorJson(SceneCenter));
			Result->SetObjectField(TEXT("scene_bounds"), BoundsObj);

			Result->SetNumberField(TEXT("total_actor_count"), AllActors.Num());
			Result->SetNumberField(TEXT("actors_in_radius"), ActorsInRadius.Num());
			Result->SetObjectField(TEXT("analysis_center"), MakeVectorJson(AnalysisCenter));
			Result->SetNumberField(TEXT("analysis_radius"), Radius);

			if (GroundHits > 0)
			{
				Result->SetNumberField(TEXT("ground_level_z"), GroundSum / GroundHits);
			}

			Result->SetArrayField(TEXT("nearest_actors"), NearestArray);
			Result->SetObjectField(TEXT("density_map"), DensityMap);
			Result->SetArrayField(TEXT("empty_spaces"), EmptySpaces);

			// Bounding summary
			TSharedPtr<FJsonObject> BoundingSummary = MakeShared<FJsonObject>();
			BoundingSummary->SetNumberField(TEXT("smallest_actor_extent"), SmallestSize);
			BoundingSummary->SetNumberField(TEXT("largest_actor_extent"), LargestSize);

			// Calculate average spacing between nearest actor pairs
			if (ActorsInRadius.Num() >= 2)
			{
				double SpacingSum = 0.0;
				int32 SpacingCount = 0;
				int32 MaxPairs = FMath::Min(ActorsInRadius.Num(), 20);
				for (int32 i = 0; i < MaxPairs; i++)
				{
					double MinDist = MAX_dbl;
					for (int32 j = 0; j < ActorsInRadius.Num(); j++)
					{
						if (i == j) continue;
						double D = FVector::Dist(ActorsInRadius[i].Actor->GetActorLocation(),
							ActorsInRadius[j].Actor->GetActorLocation());
						MinDist = FMath::Min(MinDist, D);
					}
					if (MinDist < MAX_dbl)
					{
						SpacingSum += MinDist;
						SpacingCount++;
					}
				}
				if (SpacingCount > 0)
				{
					BoundingSummary->SetNumberField(TEXT("average_nearest_spacing"), SpacingSum / SpacingCount);
				}
			}

			Result->SetObjectField(TEXT("bounding_summary"), BoundingSummary);

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// find_placement_position - Find a clear position to place something
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("near_x"), TEXT("Desired X position"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("near_y"), TEXT("Desired Y position"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("near_z"), TEXT("Desired Z position"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("required_size_x"), TEXT("Required size X (full width of object to place)"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("required_size_y"), TEXT("Required size Y (full depth)"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("required_size_z"), TEXT("Required size Z (full height)"), true);
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("on_ground"), TEXT("Trace to ground and place on surface (default: true)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("min_distance_from_actors"), TEXT("Minimum distance from any existing actor (default: 0)"));
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("prefer_direction"), TEXT("Preferred direction to search if desired position is blocked"),
			{ TEXT("any"), TEXT("north"), TEXT("south"), TEXT("east"), TEXT("west") });

		FMCPToolDefinition Def;
		Def.Name = TEXT("find_placement_position");
		Def.Description = TEXT("AI-friendly tool: find a good position to place something. Tries the desired position, checks overlap, and spirals outward to find clear space if blocked. Optionally snaps to ground.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FVector DesiredPos(
				Args->GetNumberField(TEXT("near_x")),
				Args->GetNumberField(TEXT("near_y")),
				Args->GetNumberField(TEXT("near_z"))
			);

			FVector RequiredSize(
				Args->GetNumberField(TEXT("required_size_x")),
				Args->GetNumberField(TEXT("required_size_y")),
				Args->GetNumberField(TEXT("required_size_z"))
			);

			FVector HalfExtent = RequiredSize * 0.5;

			bool bOnGround = true;
			if (Args->HasField(TEXT("on_ground"))) Args->TryGetBoolField(TEXT("on_ground"), bOnGround);

			double MinDist = 0.0;
			if (Args->HasField(TEXT("min_distance_from_actors"))) MinDist = Args->GetNumberField(TEXT("min_distance_from_actors"));

			FString PreferDir = TEXT("any");
			Args->TryGetStringField(TEXT("prefer_direction"), PreferDir);

			// Lambda to test if a position is clear
			auto TestPosition = [&](const FVector& Pos) -> bool
			{
				FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MCPFindPlacement), true);
				FCollisionShape Shape = FCollisionShape::MakeBox(HalfExtent + FVector(MinDist));
				TArray<FOverlapResult> Overlaps;
				return !World->OverlapMultiByChannel(Overlaps, Pos, FQuat::Identity, ECC_WorldStatic, Shape, QueryParams) || Overlaps.Num() == 0;
			};

			FVector FinalPos = DesiredPos;
			bool bExact = true;
			FString AdjustedReason;

			if (!TestPosition(DesiredPos))
			{
				bExact = false;
				bool bFound = false;

				// Determine search directions based on preference
				TArray<FVector> SearchDirs;
				if (PreferDir == TEXT("north"))
				{
					SearchDirs.Add(FVector(0, 1, 0)); // +Y first
					SearchDirs.Add(FVector(1, 0, 0));
					SearchDirs.Add(FVector(0, -1, 0));
					SearchDirs.Add(FVector(-1, 0, 0));
				}
				else if (PreferDir == TEXT("south"))
				{
					SearchDirs.Add(FVector(0, -1, 0));
					SearchDirs.Add(FVector(1, 0, 0));
					SearchDirs.Add(FVector(0, 1, 0));
					SearchDirs.Add(FVector(-1, 0, 0));
				}
				else if (PreferDir == TEXT("east"))
				{
					SearchDirs.Add(FVector(1, 0, 0)); // +X first
					SearchDirs.Add(FVector(0, 1, 0));
					SearchDirs.Add(FVector(-1, 0, 0));
					SearchDirs.Add(FVector(0, -1, 0));
				}
				else if (PreferDir == TEXT("west"))
				{
					SearchDirs.Add(FVector(-1, 0, 0));
					SearchDirs.Add(FVector(0, 1, 0));
					SearchDirs.Add(FVector(1, 0, 0));
					SearchDirs.Add(FVector(0, -1, 0));
				}
				else // "any" - spiral outward
				{
					SearchDirs.Add(FVector(1, 0, 0));
					SearchDirs.Add(FVector(0, 1, 0));
					SearchDirs.Add(FVector(-1, 0, 0));
					SearchDirs.Add(FVector(0, -1, 0));
					SearchDirs.Add(FVector(1, 1, 0).GetSafeNormal());
					SearchDirs.Add(FVector(-1, 1, 0).GetSafeNormal());
					SearchDirs.Add(FVector(1, -1, 0).GetSafeNormal());
					SearchDirs.Add(FVector(-1, -1, 0).GetSafeNormal());
				}

				// Search at increasing distances
				double StepSize = FMath::Max(RequiredSize.GetMax(), 100.0);
				for (int32 Ring = 1; Ring <= 20 && !bFound; Ring++)
				{
					double Dist = StepSize * Ring;
					for (const FVector& Dir : SearchDirs)
					{
						FVector TestPos = DesiredPos + Dir * Dist;
						if (TestPosition(TestPos))
						{
							FinalPos = TestPos;
							bFound = true;
							AdjustedReason = FString::Printf(TEXT("Moved %.0f units %s to avoid overlap"),
								Dist,
								Dir.X > 0.5 ? TEXT("east") :
								Dir.X < -0.5 ? TEXT("west") :
								Dir.Y > 0.5 ? TEXT("north") :
								TEXT("south"));
							break;
						}
					}
				}

				if (!bFound)
				{
					AdjustedReason = TEXT("Could not find clear space within search range, using desired position");
					FinalPos = DesiredPos;
				}
			}

			// Trace to ground if requested
			if (bOnGround)
			{
				FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(MCPPlacementGround), true);
				FVector TraceStart(FinalPos.X, FinalPos.Y, FinalPos.Z + 10000.0);
				FVector TraceEnd(FinalPos.X, FinalPos.Y, FinalPos.Z - 100000.0);
				FHitResult Hit;
				if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, GroundParams))
				{
					FinalPos.Z = Hit.Location.Z + HalfExtent.Z;
				}
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetObjectField(TEXT("position"), MakeVectorJson(FinalPos));
			Result->SetBoolField(TEXT("is_exact"), bExact);
			Result->SetObjectField(TEXT("desired_position"), MakeVectorJson(DesiredPos));
			Result->SetObjectField(TEXT("required_size"), MakeVectorJson(RequiredSize));

			if (!AdjustedReason.IsEmpty())
			{
				Result->SetStringField(TEXT("adjusted_reason"), AdjustedReason);
			}

			Result->SetNumberField(TEXT("distance_from_desired"), FVector::Dist(DesiredPos, FinalPos));

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}

} // void RegisterAll

} // namespace MCPSpatialTools
