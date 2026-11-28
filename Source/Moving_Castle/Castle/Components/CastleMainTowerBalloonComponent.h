// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CastleMainTowerBalloonComponent.generated.h"

class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class FCastleMenuModelBuilder;
DECLARE_MULTICAST_DELEGATE(FCastleTowerDisplayChanged);

/** 同一个作者挂点上的拉索和大气球随生命同步显隐。固定挂环已烘焙在塔身网格中。 */
USTRUCT()
struct FCastleMainTowerLiftParts
{
	GENERATED_BODY()

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Tether;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Balloon;
};

/** 每级底座和单个中央喷口成组发布，待提交组不渲染也不启动 Niagara。 */
USTRUCT()
struct FCastleMainTowerFlightParts
{
	GENERATED_BODY()

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Base;
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Exhaust;

	/** 先销毁子级喷流，再销毁底座；也用于部分组件注册成功后的撤销。 */
	void Destroy();
};

/** 以 560 cm 套件的数据同时切换塔身、底座与吊挂，气球颗数由真实生命比例驱动。 */
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

	/** 先注册隐藏的吊挂和底座；侧格扩层失败时销毁本次未发布的组件。 */
	bool PrepareTier(int32 LayerCount, FText& OutReason);
	void DiscardPreparedTier();

	/** 侧格成功追加后换上新塔身、喷口和吊挂，用同一时刻的主塔生命刷新气球显隐。 */
	void CommitPreparedTier(float CurrentHealth, float MaxHealth);
	/** 校验生命状态后刷新显隐，非法输入时保留上一帧的吊挂状态。 */
	bool UpdateHealth(float CurrentHealth, float MaxHealth);

	/** 正式局只有换塔或可见吊挂数量变化时才通知菜单记录器。 */
	FCastleTowerDisplayChanged OnDisplayChanged;
	bool CollectMenuTower(FCastleMenuModelBuilder& Builder, FString& Error) const;
	/** 首玩复用正式初始套件的挂点数据，不在菜单实例化生命、派兵或侧格组件。 */
	bool BuildFirstMenuTower(FCastleMenuModelBuilder& Builder, FString& Error) const;

protected:
	/** 塔被销毁时显式撤下活跃喷口和可能尚未提交的底座。 */
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	/** 类默认对象持有硬引用，使运行时未使用的预留等级及其材质也进入 Cook。 */
	UPROPERTY() TArray<TObjectPtr<UStaticMesh>> TowerAssets;
	UPROPERTY() TArray<TObjectPtr<UStaticMesh>> LiftAssets;
	UPROPERTY() TArray<TObjectPtr<UStaticMesh>> FlightBaseAssets;
	UPROPERTY() TObjectPtr<UNiagaraSystem> ThrusterExhaustFX;
	UPROPERTY() TObjectPtr<UMaterialInterface> DarkMaterial;

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> TowerMesh;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> CrownAnchor;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> BalloonAnchor;
	UPROPERTY(Transient) TArray<FCastleMainTowerLiftParts> LiveParts;
	UPROPERTY(Transient) TArray<FCastleMainTowerLiftParts> PendingParts;
	UPROPERTY(Transient) FCastleMainTowerFlightParts LiveFlight;
	UPROPERTY(Transient) FCastleMainTowerFlightParts PendingFlight;
	int32 PendingTierIndex = INDEX_NONE;
	int32 LiveCount = 0;
};
