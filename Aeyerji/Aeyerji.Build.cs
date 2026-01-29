// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Aeyerji : ModuleRules
{
	public Aeyerji(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "AIModule", "Niagara", "EnhancedInput", "GameplayAbilities", "GameplayTags", "GameplayTasks", "StateTreeModule", "GameplayStateTreeModule", "NavigationSystem", "OnlineSubsystem", "OnlineSubsystemUtils", "UMG", "SlateCore", "DeveloperSettings", "NetCore", "PhysicsCore" });
        PrivateDependencyModuleNames.AddRange(new string[] { "AITestSuite" });
	}
}
