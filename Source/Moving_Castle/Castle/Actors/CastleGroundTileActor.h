// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CastleGameplayTypes.h"
#include "CastleGroundTileActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

/** 单个可独立生成、销毁和检查的棋盘地面格。 */
UCLASS(Blueprintable)
class MOVING_CASTLE_API ACastleGroundTileActor : public AActor
{
	GENERATED_BODY()

public:
	/** 创建地块、数字底牌和文字组件；构造时不创建导航几何。 */
	ACastleGroundTileActor();

	/**
	 * 设置格子数据和视觉尺寸。
	 *
	 * Actor 的世界变换由棋盘在 SpawnActor 时传入；本函数只配置 Actor 自己的局部组件，
	 * 并在赋予网格前设定碰撞与导航资格，避免导航系统看到组件的中间状态。
	 */
	bool InitializeGroundCell(const FCastleGroundCell& InGroundCell, float CellSize, float LayerHeight);

	/** 返回该 Actor 对应的格子数据副本。 */
	UFUNCTION(BlueprintPure, Category="移动城堡|棋盘格")
	FCastleGroundCell GetGroundCell() const { return GroundCell; }

	/** 返回地块网格，供鼠标射线命中身份校验。 */
	UStaticMeshComponent* GetTileMeshComponent() const { return TileMesh; }

	/** 返回无碰撞的数字底牌网格。 */
	UStaticMeshComponent* GetNumberPlateComponent() const { return NumberPlate; }

	/** 返回格子数字文字组件。 */
	UTextRenderComponent* GetNumberLabelComponent() const { return NumberLabel; }

protected:
	/** 地块 Actor 自己拥有的根节点和显示组件，棋盘管理器不再持有这些组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="棋盘格|组件")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="棋盘格|组件")
	TObjectPtr<UStaticMeshComponent> TileMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="棋盘格|组件")
	TObjectPtr<UStaticMeshComponent> NumberPlate;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="棋盘格|组件")
	TObjectPtr<UTextRenderComponent> NumberLabel;

	/** 生成器给出的格子属性由这个独立 Actor 保存。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="棋盘格|数据")
	FCastleGroundCell GroundCell;

private:
	/** 引擎基础方块是占位外观资源，不由棋盘管理器持有。 */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> PrototypeCubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PrototypeMaterial;

	bool bInitialized = false;
};
