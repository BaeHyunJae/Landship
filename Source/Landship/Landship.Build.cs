// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Landship : ModuleRules
{
	public Landship(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"Niagara",
			"UMG",
			"Slate",
			"SlateCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Landship",
			"Landship/Combat",
			"Landship/Castle",
			"Landship/Troops",
			"Landship/Controls",
			"Landship/Vision",
			"Landship/Enemies",
			"Landship/Obstacles",
			"Landship/Maps",
			"Landship/Match",
			"Landship/HUD",
			"Landship/Variant_Strategy",
			"Landship/Variant_Strategy/UI",
			"Landship/Variant_TwinStick",
			"Landship/Variant_TwinStick/AI",
			"Landship/Variant_TwinStick/Gameplay",
			"Landship/Variant_TwinStick/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
