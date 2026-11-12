// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BuildingNavigationActor.h"
#include "CastleGameplayTypes.h"
#include "Components/Building/Effects/CastleBuildingTriggerInterface.h"
#include "CastleBuildingBase.generated.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class UBuildingMergeComponent;
class UAssemblyFlightComponent;
class UBuildingGameplayRootComponent;
class UCastleBuildingEffectComponent;

/**
 * 所有可放置建筑的共同 Actor 基类。
 *
 * 这个 Actor 同时承载建筑逻辑和该等级蓝图中的直接模型组件。等级蓝图直接
 * 继承对应 C++ 类，必须显式提供美术模型；缺少模型时拒绝配置和放置。
 * 不再通过独立模型载体或 Child Actor 转发模型和变换。
 */
UCLASS(Abstract, Blueprintable)
class MOVING_CASTLE_API ACastleBuildingBase : public ABuildingNavigationActor, public ICastleBuildingTriggerInterface
{
	GENERATED_BODY()

public:
	/** 创建逻辑根节点、功能分组和通用建筑标签，不创建替代美术模型。 */
	ACastleBuildingBase();

	/** 所有可放置建筑原生携带同一个效果入口；每个实例独立保存状态。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑|效果")
	TObjectPtr<UCastleBuildingEffectComponent> BuildingEffect;

	/** 高频调用入口只转发给自身组件，不扫描 NPC、不内置冷却。 */
	virtual bool TriggerBuildingEffect(const FCastleBuildingTriggerContext& Context) override;

	/** 预览与建造中间状态不能作为正式触发目标。 */
	bool CanReceiveBuildingTrigger() const { return !bBuildingPreview && bHasCompletedPlacement; }

	/**
	 * 放置隐藏碰撞、导航表面和触发区域的公共父节点；所有深度的后代均不参与两种建筑动画。
	 * 初始化模型时保留这些组件的碰撞和导航配置，功能组件也不参与模型检测和预览取景。
	 * 分支外所有组件均由原生 Actor 导航接口拒绝，直接摆入关卡和运行时新增组件也遵循此规则。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑|功能组件")
	TObjectPtr<UBuildingGameplayRootComponent> GameplayRoot;

	/** 所有建筑原生携带的零件合并组件；仅在棋盘合并时启动，卡片预览和普通放置不播放。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑|动画")
	TObjectPtr<UBuildingMergeComponent> BuildingMerge;

	/** 实际放置由棋盘在模型配置完后启动；预览、合并结果和关卡编辑不会自动飞入。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑|动画")
	TObjectPtr<UAssemblyFlightComponent> AssemblyFlight;

	/** 未完成放置或播放中止的建筑不能参与合并；合并结果不再重复播放放置动画。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="建筑|动画")
	bool bHasCompletedPlacement = true;

	/** 棋盘切换放置状态，并让标签只在建筑完整组装后出现。 */
	void SetPlacementComplete(bool bComplete);

	/**
	 * 用背包描述和占用格子初始化一个新建筑。
	 *
	 * 棋盘负责在调用前检查坐标是否可用；该函数仍会校验数据，避免其他调用者
	 * 绕过棋盘写入道路颜色、数字或重复坐标。
	 */
	bool InitializeBuilding(const FCastleBuildingDescriptor& Descriptor, const TArray<FIntVector>& InOccupiedCoordinates);

	/** 返回当前建筑的类别。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	ECastleBuildingType GetBuildingType() const
	{
		return BuildingType;
	}

	/** 返回当前建筑的颜色。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	ECastleTileColor GetBuildingColor() const
	{
		return Color;
	}

	/** 返回当前建筑的数字。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	int32 GetBuildingNumber() const
	{
		return Number;
	}

	/** 返回当前建筑的等级。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	int32 GetBuildingLevel() const
	{
		return BuildingLevel;
	}

	/** 返回建筑所占用的全部网格坐标副本，避免外部直接修改棋盘索引。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	TArray<FIntVector> GetOccupiedCoordinates() const
	{
		return OccupiedCoordinates;
	}

	/** 返回建筑描述，删除建筑或放回背包时使用。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	FCastleBuildingDescriptor GetBuildingDescriptor() const;

	/** 返回卡牌、场上建筑和升级结果共享的同一份建筑定义。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	UCastleBuildingDefinition* GetBuildingDefinition() const { return BuildingDefinition; }

	/** 根据当前 Actor 自身的美术组件配置视觉与标签；缺少美术模型时返回明确错误。 */
	bool ConfigureBuildingVisual(float InCellSize, float InLayerHeight, FText& OutFailureReason);

	/** 返回当前 Actor 直接拥有的可见模型包围盒，供标签和卡片预览取景使用。 */
	FBox GetVisualBounds() const;

	/** 预览模式下只允许 SceneCapture 看见模型，且关闭碰撞和阴影。 */
	void PrepareForPreview();

	/** 判断当前建筑是否属于道路。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|建筑")
	bool IsRoad() const
	{
		return BuildingType == ECastleBuildingType::Road;
	}

	/**
	 * 返回指定等级对应的具体 Actor 类。
	 *
	 * 等级映射只来自本建筑的 Data Asset，不再在各个建筑族中维护另一份表。
	 */
	TSubclassOf<ACastleBuildingBase> GetClassForLevel(int32 InLevel) const;

	/** 判断当前建筑是否还有可合成的下一级，功能建筑默认最多自动合成到 4 级。 */
	bool CanMergeToNextLevel() const;

	/** 判断两个建筑是否满足同类别、同等级、同颜色和同数字的基础合成条件。 */
	bool CanMergeWith(const ACastleBuildingBase* Other) const;

	/** 返回当前等级合成所需的单个建筑占用格子数。 */
	static int32 GetExpectedFootprintCellCount(int32 InLevel);

protected:
	/** 建筑可被关卡流送或直接销毁，必须撤销本实例的触发登记。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 所有模型组件和通用标签的公共根节点。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑模型")
	TObjectPtr<USceneComponent> VisualRoot;

	/** 仅供数字标签底牌使用的引擎资源，不得用于替代缺失的建筑模型。 */
	UPROPERTY()
	TObjectPtr<UStaticMesh> LabelPlateMesh;

	/** 数字标签底牌的基础材质；每座建筑单独创建深色动态实例。 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> LabelPlateMaterial;

	/** 卡片捕获器也会创建建筑实例，必须显式排除它们的效果调用。 */
	UPROPERTY(Transient)
	bool bBuildingPreview = false;

	/** 当前建筑的共享定义；初始化后始终随回收和合成描述一起传递。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "建筑")
	TObjectPtr<UCastleBuildingDefinition> BuildingDefinition;

	/** 建筑类别由具体建筑族构造函数设置，初始化时会再次校验描述。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "建筑")
	ECastleBuildingType BuildingType = ECastleBuildingType::Road;

	/** 建筑颜色由放置卡牌提供。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "建筑")
	ECastleTileColor Color = ECastleTileColor::Gray;

	/** 建筑数字由放置卡牌提供。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "建筑")
	int32 Number = 0;

	/** 建筑等级由具体等级子类的构造函数设置。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "建筑")
	int32 BuildingLevel = 1;

	/** 该 Actor 当前占用的全部网格坐标，合成后的 Actor 会拥有多个坐标。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "建筑")
	TArray<FIntVector> OccupiedCoordinates;

	/** 在模型配置成功后显示颜色、数字和等级，以便判断放置规则。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑标签")
	TObjectPtr<class UTextRenderComponent> PrototypeLabel;

	/** 建筑标签的深色底牌，确保数字在各种建筑颜色上都能读取。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑标签")
	TObjectPtr<class UStaticMeshComponent> PrototypeLabelPlate;
};
