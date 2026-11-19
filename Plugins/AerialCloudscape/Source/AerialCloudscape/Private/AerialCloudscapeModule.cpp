#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FAerialCloudscapeModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        // 材质引用插件虚拟路径，必须在关卡加载与材质编译之前注册。
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AerialCloudscape"));
        checkf(Plugin.IsValid(), TEXT("AerialCloudscape: 无法定位插件，不能注册体积云着色器目录。"));
        AddShaderSourceDirectoryMapping(TEXT("/Plugin/AerialCloudscape"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
    }
};

IMPLEMENT_MODULE(FAerialCloudscapeModule, AerialCloudscape)
