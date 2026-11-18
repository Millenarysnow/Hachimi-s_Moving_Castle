using UnrealBuildTool;

public class AerialTerrain : ModuleRules
{
    public AerialTerrain(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // 对外只有原生 Actor，插件可以放进任何能渲染静态网格的项目。
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        // 在加载材质之前把插件里的源文件映射到着色器虚拟目录。
        PrivateDependencyModuleNames.AddRange(new[] { "Projects", "RenderCore", "RHI" });
    }
}
