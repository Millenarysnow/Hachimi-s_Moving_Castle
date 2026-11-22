// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputCoreTypes.h"
#include "CastleTowerDispatchConfigDataAsset.generated.h"

class UMassEntityConfigAsset;

/** 正式关卡显式绑定的出兵设置，玩家输入和调度时钟使用同一份资产。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleTowerDispatchConfigDataAsset final : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 由关卡或波次导演用作开局资源数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "主塔出兵")
	int32 InitialNPCCount = 8;

	/** 左键持续瞄准主塔时，两次派出之间的秒数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "主塔出兵", meta = (Units = "s"))
	float HoldDispatchInterval = 0.15f;

	/** 自动模式下两次派出之间的秒数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "主塔出兵", meta = (Units = "s"))
	float AutoDispatchInterval = 0.5f;

	/** 局部玩家的自动派兵开关键，由配置资产决定。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "主塔出兵")
	FKey ToggleAutoDispatchKey = EKeys::G;

	/** MainMap 手动启动和未来波次接口使用的 NPC 模板资产。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "主塔出兵")
	TObjectPtr<UMassEntityConfigAsset> NPCConfiguration;

	/** 在开始会话前检查原始值，配置失效时整次启动拒绝。 */
	bool ValidateConfiguration(FText& OutReason) const;
};
