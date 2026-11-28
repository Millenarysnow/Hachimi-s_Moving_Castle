#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "CastleStylizedMaterialTools.generated.h"

class UMaterial;
class UMaterialExpressionMaterialFunctionCall;
class UMaterialInstanceConstant;
class UMaterialInterface;
class UToonProfile;

/** 离线材质工具。给编辑器 Python 提供 UE 5.8 未公开的曲线和材质输入访问。 */
UCLASS()
class MOVING_CASTLE_API UCastleStylizedMaterialTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 把暗部颜色与可选的金属高光写入 Profile 自带的曲线。 */
	UFUNCTION(BlueprintCallable, Category="Castle|Stylized Rendering", meta=(DevelopmentOnly))
	static bool ConfigureToonProfile(UToonProfile* Profile, FLinearColor ShadowColor, FLinearColor HighlightColor);

	/** 读取 V4 原材质的线性固有色与自发光常量，拒绝已有输入图的源材质。 */
	UFUNCTION(BlueprintCallable, Category="Castle|Stylized Rendering", meta=(DevelopmentOnly))
	static bool ReadV4Colors(UMaterial* Material, FLinearColor& BaseColor, FLinearColor& EmissiveColor);

	/** 保留原材质图的输出索引和通道掩码，将固有色、发光、法线接入共享表面。 */
	UFUNCTION(BlueprintCallable, Category="Castle|Stylized Rendering", meta=(DevelopmentOnly))
	static bool ConnectLegacySurface(UMaterial* Material, UMaterialExpressionMaterialFunctionCall* Call);

	/** 检查实例本地保存的全部参数类型与名称，返回无法从新母材质解析的项目。 */
	UFUNCTION(BlueprintCallable, Category="Castle|Stylized Rendering", meta=(DevelopmentOnly))
	static TArray<FString> GetUnsupportedOverrides(const UMaterialInstanceConstant* Instance, const UMaterialInterface* Parent);
};
