#include "BuildingGameplayRootComponent.h"

bool UBuildingGameplayRootComponent::IsInGameplayHierarchy(const USceneComponent* Component)
{
	// 从自身向上查询，既覆盖直接子组件，也覆盖夹有任意多个普通 SceneComponent 的分组。
	for (const USceneComponent* Ancestor = Component; Ancestor; Ancestor = Ancestor->GetAttachParent())
	{
		if (Ancestor->IsA<UBuildingGameplayRootComponent>())
		{
			return true;
		}
	}
	return false;
}
