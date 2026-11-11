// Copyright Epic Games, Inc. All Rights Reserved.

#include "BuildingNavigationActor.h"

#include "Components/Building/BuildingGameplayRootComponent.h"

bool ABuildingNavigationActor::IsComponentRelevantForNavigation(UActorComponent* Component) const
{
	// 必须属于当前建筑且位于功能分支；未附着的组件及没有场景层级的导航修改器也排除。
	// 每次沿当前父级链判断，不缓存结果，支持嵌套分组、根节点重命名和组件重新挂接。
	const USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
	return SceneComponent && SceneComponent->GetOwner() == this
		&& UBuildingGameplayRootComponent::IsInGameplayHierarchy(SceneComponent)
		&& Super::IsComponentRelevantForNavigation(Component);
}
