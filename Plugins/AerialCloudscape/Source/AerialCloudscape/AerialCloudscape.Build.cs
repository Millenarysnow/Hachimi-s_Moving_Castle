using UnrealBuildTool;

public class AerialCloudscape : ModuleRules
{
    public AerialCloudscape(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        // 航程只在驱动实现里读取，调用方不需了解地貌数据或移动城堡的玩法模块。
        PrivateDependencyModuleNames.AddRange(new[] { "AerialTerrain", "Projects", "RenderCore", "RHI" });
    }
}
