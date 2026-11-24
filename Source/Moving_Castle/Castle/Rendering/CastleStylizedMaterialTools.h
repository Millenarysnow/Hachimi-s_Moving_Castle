#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "CastleStylizedMaterialTools.generated.h"

class UMaterial;
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
};
