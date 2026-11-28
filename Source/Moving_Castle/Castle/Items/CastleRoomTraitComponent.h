#pragma once

#include "CastleGameplayTypes.h"
#include "Components/StaticMeshComponent.h"
#include "CastleRoomTraitComponent.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** 房间特质挂在 GameplayRoot 下。只有传送门实际使用静态网格与准星碰撞。 */
UCLASS(ClassGroup = (Castle))
class MOVING_CASTLE_API UCastleRoomTraitComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UCastleRoomTraitComponent();

	/** 从已校验的卡牌携带特质数据，完成组件准备后才允许放置提交。 */
	bool TryInitializeTrait(const FCastleBuildingDescriptor& Descriptor, FText& OutReason);

	/** 门体位于单格内部，使用与房间放置一致的底面中心和棋盘尺寸。 */
	void ConfigurePortalTransform(float CellSize, float LayerHeight);

	/** 飞入或摄影期间禁用 Visibility 命中；建造完成后才显示并允许交互。 */
	void SetPortalAvailable(bool bAvailable);
	bool IsPortalAvailable() const;

	/** 传送成功后由房间清除特质；失败时建筑与导演状态保持原状。 */
	bool TryInteractWithPortal(FText& OutReason);

private:
	/** 硬引用让 Cook 保留复用的离岛门资源。缺失时拒绝初始化房间。 */
	UPROPERTY()
	TObjectPtr<UStaticMesh> DeparturePortalMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> DeparturePortalMaterial;
};
