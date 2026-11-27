// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastleGameplayTypes.h"
#include "CastleGridCursor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

/**
 * 屏幕中心射线的网格全息指示器：青蓝表示可放，暖红斜纹表示当前条件不满足。
 *
 * 该 Actor 只负责显示棋盘已经解析好的变换，不参与射线查询，也不承担任何
 * 放置规则。这样控制器、棋盘和可视化之间保持单向数据流，后续替换正式美术
 * 资源时不需要重写坐标吸附逻辑。目标变换由棋盘或空岛计算，Actor 不自行推断落点。
 */
UCLASS()
class MOVING_CASTLE_API ACastleGridCursor : public AActor
{
	GENERATED_BODY()
	friend class FCastlePlacementHoloBoxAutomationTest;

public:
	/** 使用具有六面 0–1 UV 的 100 cm 立方体显示完整的三维占位体积。 */
	ACastleGridCursor();

	/** 应用棋盘解析出的变换与校验结果；可视化自身不重复判断规则。 */
	void ApplyTarget(const FCastleGridCursorTarget& Target, bool bCanPlace);

	/** 隐藏当前指示器，表示屏幕中心没有指向有效棋盘体积。 */
	void HideCursor();

protected:
	/** 每个已初始化的指示器单独创建动态材质，避免在默认对象上保存运行时参数。 */
	virtual void PostInitializeComponents() override;

	/** 完整网格显示当前目标单格空间，透明全息参数由放置校验结果决定。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "网格指针")
	TObjectPtr<UStaticMeshComponent> CursorMesh;

	/** 构造时硬引用生成的材质，运行时以它为父材质创建实例。 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> CursorMaterialTemplate;

	/** 当前指示器自己的动态材质；必需参数索引在初始化后复用。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CursorMaterial;

	/** 保存材质必需向量参数的实例索引，避免运行时按未核对的名称写值。 */
	int32 HoloColorParameterIndex = INDEX_NONE;
	int32 SupportNormalParameterIndex = INDEX_NONE;

	/** 保存材质必需标量参数的实例索引，避免运行时按未核对的名称写值。 */
	int32 InvalidModeParameterIndex = INDEX_NONE;
};
