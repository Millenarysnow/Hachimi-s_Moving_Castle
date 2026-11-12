// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastleGameplayTypes.h"
#include "CastleGridCursor.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/**
 * 屏幕中心射线的网格面指示器：浅蓝表示当前卡牌可放，红色表示不满足放置条件。
 *
 * 该 Actor 只负责显示棋盘已经解析好的变换，不参与射线查询，也不承担任何
 * 放置规则。这样控制器、棋盘和可视化之间保持单向数据流，后续替换正式美术
 * 资源时不需要重写坐标吸附逻辑。
 */
UCLASS()
class MOVING_CASTLE_API ACastleGridCursor : public AActor
{
	GENERATED_BODY()

public:
	/** 使用 UE 自带 Cube 创建一个可旋转到六个面的薄平面。 */
	ACastleGridCursor();

	/** 应用棋盘解析出的变换与校验结果；可视化自身不重复判断规则。 */
	void ApplyTarget(const FCastleGridCursorTarget& Target, bool bCanPlace);

	/** 隐藏当前指示器，表示屏幕中心没有指向有效棋盘体积。 */
	void HideCursor();

protected:
	/** 薄立方体显示当前网格面，颜色由放置校验结果决定。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "网格指针")
	TObjectPtr<UStaticMeshComponent> CursorMesh;

	/** 运行时创建的高亮材质实例。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CursorMaterial;
};
