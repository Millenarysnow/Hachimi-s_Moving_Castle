// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastleGameplayTypes.h"
#include "CastleBuildingModelActor.generated.h"

class UChildActorComponent;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 放置建筑与卡片预览共用的模型载体。
 * 本类只显示模型，不保存棋盘坐标，不处理背包、合成或战斗规则。
 * 逻辑建筑继承本类，预览则直接创建本类，因此两个入口不会分别解释模型配置。
 */
UCLASS()
class MOVING_CASTLE_API ACastleBuildingModelActor : public AActor
{
	GENERATED_BODY()

public:
	/** 创建独立根节点、静态网格和可容纳多零件蓝图的模型组件。 */
	ACastleBuildingModelActor();

	/** 按准确等级显示模型；只有模型引用全部留空时才启用立方体占位。 */
	bool ConfigureModel(const UCastleBuildingDefinition* Definition, int32 Level, ECastleTileColor Color,
		const FIntVector& FootprintDimensions, float CellSize, float LayerHeight, FText& OutFailureReason);

	/** 返回可见模型的世界包围盒，供建筑标签和预览相机取景使用。 */
	FBox GetVisualBounds() const;

	/** 预览模型只进入 SceneCapture，不显示在主视口，也不参与任何碰撞或导航。 */
	void PrepareForPreview();

	/** 暴露实际表现状态，方便编辑器检查与自动化验证。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑模型")
	bool IsUsingPlaceholder() const { return bUsingPlaceholder; }

protected:
	/** 逻辑建筑的标签也挂在此根节点，避免继承模型的缩放。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑模型")
	TObjectPtr<USceneComponent> VisualRoot;

	/** 二级建筑沿 Y 轴合成时只旋转模型，不改变逻辑占用坐标。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑模型")
	TObjectPtr<USceneComponent> ModelPivot;

	/** 单网格模型和原型立方体共用此组件；使用蓝图模型时清空该网格。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑模型")
	TObjectPtr<UStaticMeshComponent> PrototypeMesh;

	/** 现有多零件模型保持为一个子 Actor，不修改原模型资产的组件层级。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑模型")
	TObjectPtr<UChildActorComponent> ModelBlueprint;

	/** 引擎自带立方体与材质只用于用户允许的“未配置模型”分支。 */
	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaceholderCube;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PlaceholderMaterial;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "建筑模型")
	bool bUsingPlaceholder = true;
};
