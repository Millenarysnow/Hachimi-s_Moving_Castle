// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CastleMainTowerBalloonComponent.generated.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** 占位气球及球绳；数量由生命组件驱动，整体随棋盘的塔顶锚点移动。 */
UCLASS()
class UCastleMainTowerBalloonComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UCastleMainTowerBalloonComponent();

	/** 校验资产并登记塔顶锚点；气球与绳子始终成对生成。 */
	bool Initialize(USceneComponent* InAnchor, FText& OutReason);

	/** 预备本等级的表现容量；失败时保留之前的所有实例。 */
	bool SetCapacity(int32 Capacity, FText& OutReason);

	/** 升级事务失败时销毁尚未发布的隐藏资源，保留旧等级全部气球。 */
	void DiscardCapacityAbove(int32 Capacity);

	/** 只切换受影响的气球和球绳，序号在一次游戏内保持稳定。 */
	void SetLiveCount(int32 Count);

private:
	UPROPERTY(Transient) TObjectPtr<USceneComponent> Anchor;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> BalloonMesh;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> StringMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BalloonMaterial;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Balloons;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Strings;
	int32 LiveCount = 0;
};
