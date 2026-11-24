#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HachimisV4/CastleHachimisV4Layout.h"
#include "CastlePCGAttachmentKit.generated.h"

/** 作者在统一挂点坐标中声明的网格套件；玩法碰撞由规则另行声明。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastlePCGAttachmentKit : public UDataAsset
{
	GENERATED_BODY()
public:
	/** 每件网格都按套件锚点保存局部变换，不从网格尺寸推测用途。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	TArray<FCastleV4Part> Parts;
};
