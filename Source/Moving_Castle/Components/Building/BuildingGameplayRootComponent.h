#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "BuildingGameplayRootComponent.generated.h"

/**
 * 建筑功能组件的分组根节点。隐藏地板、简化楼梯、碰撞盒和触发区域应挂在本节点下面。
 * 本节点及其任意深度的后代都不属于建筑美术零件；动画、取景和离线零件收集共用此边界。
 * 只声明组件用途，不自动隐藏、禁用碰撞或开启导航，具体设置仍由各功能组件的作者决定。
 */
UCLASS(ClassGroup=(Castle), BlueprintType, meta=(BlueprintSpawnableComponent, DisplayName="Building Gameplay Root"))
class MOVING_CASTLE_API UBuildingGameplayRootComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/**
	 * 检查组件自身及完整附着父级链，识别功能根节点与其所有后代。
	 * 使用原生组件类型而非名称或标签，避免重命名失效，也不要求后代逐件复制标签。
	 * 暴露给 UE Python，让离线工具与运行时使用同一条排除规则。
	 */
	UFUNCTION(BlueprintPure, Category="建筑|功能组件")
	static bool IsInGameplayHierarchy(const USceneComponent* Component);
};
