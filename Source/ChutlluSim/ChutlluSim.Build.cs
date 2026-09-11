// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class ChutlluSim : ModuleRules
{
	public ChutlluSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Niagara", "UMG" });

		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Living-city (Mass/Traffic/Crowd) — Phase 3 ZoneGraph generation + Phase 5 crowd consume.
		PrivateDependencyModuleNames.AddRange(new string[] { "ZoneGraph", "MassEntity", "MassCommon", "MassCrowd" });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
