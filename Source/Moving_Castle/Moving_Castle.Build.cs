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
			"MassEntity",
			"MassCore",
			"MassCommon",
			"MassActors",
			"MassSpawner",
			"MassSimulation",
			"MassMovement",
			"MassLOD",
			"MassRepresentation",
			"UMG",
			"Slate",
			"SlateCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
		// 几何身份在编辑器离线生成；正式游戏只读取目录，不依赖 MeshDescription 或 CPU 顶点。
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "MeshDescription", "StaticMeshDescription" });
		}

		PublicIncludePaths.AddRange(new string[] {
			"Moving_Castle",
			"Moving_Castle/Units/Mass",
			"Moving_Castle/Castle", // 城堡模块根路径，保留旧工程代码的公共头文件兼容性。
			"Moving_Castle/Gameplay/Characters",
			"Moving_Castle/Gameplay/Controllers",
			"Moving_Castle/Gameplay/GameModes",
			"Moving_Castle/Castle/Actors",
			"Moving_Castle/Castle/Components",
			"Moving_Castle/Castle/Data",
			"Moving_Castle/Castle/GameModes",
			"Moving_Castle/Castle/Controllers",
			"Moving_Castle/Castle/UI",
			"Moving_Castle/Castle/Automation",
			"Moving_Castle/Components/Animation",
			"Moving_Castle/Variant_Platforming",
			"Moving_Castle/Variant_Platforming/Animation",
			"Moving_Castle/Variant_Platforming/Characters",
			"Moving_Castle/Variant_Platforming/Controllers",
			"Moving_Castle/Variant_Platforming/GameModes",
			"Moving_Castle/Variant_Combat",
			"Moving_Castle/Variant_Combat/AI",
			"Moving_Castle/Variant_Combat/Animation",
			"Moving_Castle/Variant_Combat/Gameplay",
			"Moving_Castle/Variant_Combat/Interfaces",
			"Moving_Castle/Variant_Combat/UI",
			"Moving_Castle/Variant_Combat/Characters",
			"Moving_Castle/Variant_Combat/Controllers",
			"Moving_Castle/Variant_Combat/GameModes",
			"Moving_Castle/Variant_SideScrolling",
			"Moving_Castle/Variant_SideScrolling/AI",
			"Moving_Castle/Variant_SideScrolling/Gameplay",
			"Moving_Castle/Variant_SideScrolling/Interfaces",
			"Moving_Castle/Variant_SideScrolling/UI",
			"Moving_Castle/Variant_SideScrolling/Camera",
			"Moving_Castle/Variant_SideScrolling/Characters",
			"Moving_Castle/Variant_SideScrolling/Controllers",
			"Moving_Castle/Variant_SideScrolling/GameModes"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
