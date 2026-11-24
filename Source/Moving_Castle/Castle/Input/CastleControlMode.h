// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "CastleControlMode.generated.h"

class APlayerController;

/** 玩家本地操作预设。存档使用稳定名称，显示名称由设置界面提供。 */
UENUM(BlueprintType)
enum class ECastleControlMode : uint8
{
	Classic,
	Design
};

/** 世界输入所用的语义动作。玩法只读取动作对应的按键。 */
enum class ECastleWorldInputAction : uint8
{
	PrimaryInteract,
	Demolish,
	HoldCameraLook,
	Count
};

/** 单套预设的动作到物理按键表。经典模式不需要按住键即可转动视角。 */
struct FCastleControlBindings
{
	FKey Keys[static_cast<int32>(ECastleWorldInputAction::Count)];

	const FKey& Get(ECastleWorldInputAction Action) const { return Keys[static_cast<int32>(Action)]; }
};

namespace CastleControlInput
{
	const FCastleControlBindings& GetBindings(ECastleControlMode Mode);
	TArray<FKey> GetPointerKeys();
	FText GetKeyDisplayText(const FKey& Key);
	/** 经典玩家和通用控制器巡逻演示共用窗口中心坐标。 */
	bool TryGetClassicAimScreenPosition(const APlayerController& Controller, float& OutX, float& OutY);
	bool LoadMode(ECastleControlMode& OutMode);
	void SaveMode(ECastleControlMode Mode);
}
