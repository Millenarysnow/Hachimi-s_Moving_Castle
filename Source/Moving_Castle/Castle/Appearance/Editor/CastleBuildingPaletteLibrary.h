#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "CastleBuildingPaletteLibrary.generated.h"

class UMaterial;
class UTexture2D;

/** 从项目已导入的作者固有色制作玩法色阶，编辑器可从 Python 重建；成品随建筑材质一起烘焙。 */
UCLASS()
class UCastleBuildingPaletteLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, Category="建筑|美术制作")
	static bool RebuildBuildingPalettes();

	/** 同一套接线也用于 FBX 再导入，防止更新模型时清掉属性色。 */
	static bool BindSolid(UMaterial& Material);
	static bool BindAtlas(UMaterial& Material, UTexture2D& Source, bool bShield, TArray<UObject*>& Assets);
#endif
};
