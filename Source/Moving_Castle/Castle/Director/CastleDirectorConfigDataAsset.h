// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CastleDirectorConfigDataAsset.generated.h"

class UMassEntityConfigAsset;
class UStaticMesh;
class UCastleDirectorHUDWidget;
class UCastleRewardChoiceWidget;
class UCastleGameOverWidget;
class UCastleDirectorNodeLabelWidget;

/** 导演系统单局配置；资产只保存可调参数，流程行为由 C++ 子系统执行。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleDirectorConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 节点数量的确定性随机种子。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|节点")
	int32 RunSeed = 20261116;

	/** 每一步节点数量范围；校验阶段约束为 2 至 3。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|节点", meta = (ClampMin = "2", ClampMax = "3"))
	int32 NodeCountMin = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|节点", meta = (ClampMin = "2", ClampMax = "3"))
	int32 NodeCountMax = 3;

	/** 每 N 步强制生成精英节点。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|节点", meta = (ClampMin = "2"))
	int32 EliteEveryNSteps = 5;

	/** 节点与主塔目标点的水平半径，单位厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|节点", meta = (Units = "cm"))
	float NodeRingRadius = 7500.0f;

	/** 节点相对主塔 Actor 位置的高度，单位厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|节点", meta = (Units = "cm"))
	float NodeHeightOffset = 1200.0f;

	/** 节点圆弧的总角度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|节点", meta = (Units = "degrees"))
	float NodeArcDegrees = 60.0f;

	/** 玩家触发球半径，单位厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|节点", meta = (Units = "cm"))
	float NodeTriggerRadius = 300.0f;

	/** 节点视觉网格与缩放。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|节点")
	TObjectPtr<UStaticMesh> NodeMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|节点", meta = (ClampMin = "0.01"))
	float NodeMeshScale = 4.0f;

	/** 阶段 2 用于沿共享路线生成 NPC 的配置与数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|NPC")
	TObjectPtr<UMassEntityConfigAsset> NPCConfiguration;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|NPC", meta = (ClampMin = "1", ClampMax = "128"))
	int32 NPCCount = 8;

	/** 通关奖励与成长参数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|成长", meta = (ClampMin = "1"))
	int32 RewardExp = 60;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|成长", meta = (ClampMin = "1"))
	int32 ExpPerLevel = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|成长", meta = (ClampMin = "1"))
	int32 BaseRoomLimit = 15;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|成长", meta = (ClampMin = "0"))
	int32 RoomLimitPerLevel = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|成长", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float HealFraction = 0.3f;

	/** 四类界面均使用 WBP 子类，仅绑定 C++ 基类控件，不包含蓝图逻辑。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|界面")
	TSubclassOf<UCastleDirectorHUDWidget> HUDWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|界面")
	TSubclassOf<UCastleRewardChoiceWidget> RewardWidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|界面")
	TSubclassOf<UCastleGameOverWidget> GameOverWidgetClass;

	/** 节点屏幕空间中文标签，必须绑定 LabelText；不会回退到离线字体。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|界面")
	TSubclassOf<UCastleDirectorNodeLabelWidget> NodeLabelWidgetClass;

	/** 检查所有阶段 1 配置；失败时写明资产名称、字段和值，不修正配置。 */
	bool ValidateConfig(FText& OutFailureReason) const;
};
