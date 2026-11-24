// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CastleMainTowerBalloonComponent.generated.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** 同一个作者挂点上的拉索和大气球随生命同步显隐。固定挂环已烘焙在塔身网格中。 */
USTRUCT()
struct FCastleMainTowerLiftParts
{
	GENERATED_BODY()

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Tether;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Balloon;
};

/** 以 560 cm 套件的数据同时切换整塔模型和吊挂，视觉颗数由真实生命比例驱动。 */
UCLASS()
class UCastleMainTowerBalloonComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UCastleMainTowerBalloonComponent();

	/** 层数到作者套件的索引；不支持的层数返回 INDEX_NONE。 */
	static int32 FindTierIndex(int32 LayerCount);

	/** 在开局校验全套资产，并登记整塔网格与两个原有命名的运行时锚点。 */
	bool Initialize(UStaticMeshComponent* InTowerMesh, USceneComponent* InCrownAnchor,
		USceneComponent* InBalloonAnchor, FText& OutReason);

	/** 先注册隐藏的吊挂组；侧格扩层失败时只销毁这批未发布的组件。 */
	bool PrepareTier(int32 LayerCount, FText& OutReason);
	void DiscardPreparedTier();

	/** 侧格成功追加后一次换上整塔网格和吊挂，用同一时刻的主塔生命刷新显隐。 */
	void CommitPreparedTier(float CurrentHealth, float MaxHealth);
	/** 校验生命状态后刷新显隐，非法输入时保留上一帧的吊挂状态。 */
	bool UpdateHealth(float CurrentHealth, float MaxHealth);

private:
	/** 类默认对象持有硬引用，使运行时未使用的预留等级及其材质也进入 Cook。 */
	UPROPERTY() TArray<TObjectPtr<UStaticMesh>> TowerAssets;
	UPROPERTY() TArray<TObjectPtr<UStaticMesh>> LiftAssets;
	UPROPERTY() TObjectPtr<UMaterialInterface> DarkMaterial;

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> TowerMesh;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> CrownAnchor;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> BalloonAnchor;
	UPROPERTY(Transient) TArray<FCastleMainTowerLiftParts> LiveParts;
	UPROPERTY(Transient) TArray<FCastleMainTowerLiftParts> PendingParts;
	int32 PendingTierIndex = INDEX_NONE;
	int32 LiveCount = 0;
};
