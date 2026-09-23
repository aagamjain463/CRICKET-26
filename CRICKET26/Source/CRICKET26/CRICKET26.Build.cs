// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CRICKET26 : ModuleRules
{
	public CRICKET26(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "AudioMixer", "ApplicationCore", "RenderCore", "RHI", "AnimationCore", "MeshDescription", "StaticMeshDescription" });

		PublicIncludePaths.AddRange(new string[] {
			"CRICKET26",
			"CRICKET26/Cricket",
			"CRICKET26/Variant_Platforming",
			"CRICKET26/Variant_Platforming/Animation",
			"CRICKET26/Variant_Combat",
			"CRICKET26/Variant_Combat/AI",
			"CRICKET26/Variant_Combat/Animation",
			"CRICKET26/Variant_Combat/Gameplay",
			"CRICKET26/Variant_Combat/Interfaces",
			"CRICKET26/Variant_Combat/UI",
			"CRICKET26/Variant_SideScrolling",
			"CRICKET26/Variant_SideScrolling/AI",
			"CRICKET26/Variant_SideScrolling/Gameplay",
			"CRICKET26/Variant_SideScrolling/Interfaces",
			"CRICKET26/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
