// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Moving_Castle : ModuleRules
{
	public Moving_Castle(ReadOnlyTargetRules Target) : base(Target)
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

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Moving_Castle",
			"Moving_Castle/Castle", // 城堡基础逻辑的公共头文件路径，供生成器、棋盘和建筑类共用。
			"Moving_Castle/Variant_Platforming",
			"Moving_Castle/Variant_Platforming/Animation",
			"Moving_Castle/Variant_Combat",
			"Moving_Castle/Variant_Combat/AI",
			"Moving_Castle/Variant_Combat/Animation",
			"Moving_Castle/Variant_Combat/Gameplay",
			"Moving_Castle/Variant_Combat/Interfaces",
			"Moving_Castle/Variant_Combat/UI",
			"Moving_Castle/Variant_SideScrolling",
			"Moving_Castle/Variant_SideScrolling/AI",
			"Moving_Castle/Variant_SideScrolling/Gameplay",
			"Moving_Castle/Variant_SideScrolling/Interfaces",
			"Moving_Castle/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
