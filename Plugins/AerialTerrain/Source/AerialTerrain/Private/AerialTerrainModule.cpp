#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FAerialTerrainModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        // 材质 Custom 节点以虚拟路径引用 .ush，启动阶段先于材质加载注册路径。
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AerialTerrain"));
        checkf(Plugin.IsValid(), TEXT("AerialTerrain: 无法定位插件，不能注册地表着色器目录。"));
        AddShaderSourceDirectoryMapping(TEXT("/Plugin/AerialTerrain"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
    }
};

IMPLEMENT_MODULE(FAerialTerrainModule, AerialTerrain)
