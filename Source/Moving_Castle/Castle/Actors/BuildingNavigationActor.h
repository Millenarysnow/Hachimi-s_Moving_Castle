// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildingNavigationActor.generated.h"

/**
 * 所有建筑模型共用的导航边界，不添加组件、建造状态或动画逻辑。
 * 棋盘建筑通过 CastleBuildingBase 继承本类；仅用于展示的主塔等模型也可直接继承。
 * 只有 Building Gameplay Root 功能分支具备导航参与资格，普通美术和辅助组件一律排除。
 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ABuildingNavigationActor : public AActor
{
	GENERATED_BODY()

public:
	/**
	 * UE 在组件注册、导航八叉树更新及整座 Actor 更新时调用的原生资格检查。
	 * 即使分支外组件勾选 Can Ever Affect Navigation 或在运行时新建，也不能导出导航几何。
	 * 功能分支仍需自身允许影响导航并提供有效碰撞；本接口不会强行开启触发器的导航。
	 */
	virtual bool IsComponentRelevantForNavigation(UActorComponent* Component) const override;
};
