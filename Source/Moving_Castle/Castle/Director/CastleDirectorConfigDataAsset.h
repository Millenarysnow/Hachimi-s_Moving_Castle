// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputCoreTypes.h"
#include "Castle/SkyIslands/CastleSkyIslandTypes.h"
#include "CastleDirectorConfigDataAsset.generated.h"

class UMassEntityConfigAsset;
class UStaticMesh;
class UCastleDirectorHUDWidget;
class UCastleRewardChoiceWidget;
class UCastleGameOverWidget;
class UCastleDirectorNodeLabelWidget;
class UCastleCombatConfigDataAsset;
class UCastleTowerDispatchConfigDataAsset;
class UCastleTowerUpgradeRewardWidget;
class UCastleWildcardRoomPickerWidget;

/** 单个主塔等级的固定成长数据；赠送数量仅在本次跨入对应等级时生成。 */
USTRUCT(BlueprintType)
struct FCastleTowerLevelConfig
{
	GENERATED_BODY()

	/** 用于检查数据资产行顺序，防止误将两级对调。 */
	UPROPERTY(EditAnywhere, Category = "成长") int32 Level = 1;
	UPROPERTY(EditAnywhere, Category = "成长") int32 MaxShopNumber = 3;
	UPROPERTY(EditAnywhere, Category = "成长") int32 UnlockedLayers = 3;
	UPROPERTY(EditAnywhere, Category = "成长") int32 RoomLimit = 150;
	UPROPERTY(EditAnywhere, Category = "成长") int32 BalloonCount = 5;
	/** 基础最大生命，当前已持有徽章倍率由主塔组件另行乘算。 */
	UPROPERTY(EditAnywhere, Category = "成长") float BaseMaxHealth = 1000.0f;
	UPROPERTY(EditAnywhere, Category = "赠送") int32 PathPointReward = 0;
	UPROPERTY(EditAnywhere, Category = "赠送") int32 CustomRoomReward = 0;
	UPROPERTY(EditAnywhere, Category = "赠送") int32 NPCReward = 0;
};

/** 导演系统单局配置；资产只保存可调参数，流程行为由 C++ 子系统执行。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleDirectorConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UCastleDirectorConfigDataAsset();
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

	/** 普通步首个节点固定战斗，其余节点用以下权重抽样。 */
	UPROPERTY(EditAnywhere, Category = "导演|节点") int32 NormalNodeWeight = 40;
	UPROPERTY(EditAnywhere, Category = "导演|节点") int32 RewardNodeWeight = 40;
	UPROPERTY(EditAnywhere, Category = "导演|节点") int32 RestNodeWeight = 20;

	/** 工厂只接受显式登记的玩法类，波次 2 正式资产保留空映射。 */
	UPROPERTY(EditAnywhere, Category = "导演|空岛") TMap<ECastleSkyIslandType, TSubclassOf<ACastleSkyIslandBase>> SkyIslandClasses;
	UPROPERTY(EditAnywhere, Category = "导演|空岛") float SkyIslandSpawnDistance = 9500.0f;
	UPROPERTY(EditAnywhere, Category = "导演|空岛") float SkyIslandHeightOffset = 1200.0f;
	UPROPERTY(EditAnywhere, Category = "导演|空岛") FKey SkyIslandInteractKey = EKeys::E;

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

	/** 正式出兵策略和 NPC 模板分别显式指定，配置校验在单局启动时完成。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|NPC")
	TObjectPtr<UMassEntityConfigAsset> NPCConfiguration;

	UPROPERTY(EditAnywhere, Category = "导演|NPC") TObjectPtr<UCastleTowerDispatchConfigDataAsset> DispatchConfig;
	UPROPERTY(EditAnywhere, Category = "导演|NPC") int32 InitialNPCCount = 8;
	UPROPERTY(EditAnywhere, Category = "导演|成长") int32 InitialPathPointLimit = 2;

	/** 通关奖励与成长参数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|成长", meta = (ClampMin = "1"))
	int32 RewardExp = 60;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|成长", meta = (ClampMin = "1"))
	int32 ExpPerLevel = 100;

	/** 恰好四行，商店、建造面和主塔共用这份等级数据。 */
	UPROPERTY(EditAnywhere, Category = "导演|成长") TArray<FCastleTowerLevelConfig> TowerLevels;

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

	/** 样式由资产提供；运行时必须验证完整 BindWidget。 */
	UPROPERTY(EditAnywhere, Category = "导演|界面") TSubclassOf<UCastleTowerUpgradeRewardWidget> TowerUpgradeRewardWidgetClass;
	UPROPERTY(EditAnywhere, Category = "导演|界面") TSubclassOf<UCastleWildcardRoomPickerWidget> WildcardRoomPickerClass;

	/** 战斗子系统的正式配置；缺失时整局拒绝启动，不创建替代战斗。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "导演|战斗")
	TObjectPtr<UCastleCombatConfigDataAsset> CombatConfig;

	/** 检查导演全部必需配置；失败时写明资产名称、字段和值，不修正配置。 */
	bool ValidateConfig(FText& OutFailureReason) const;
	bool ValidateTowerLevels(FText& OutFailureReason) const;
};
