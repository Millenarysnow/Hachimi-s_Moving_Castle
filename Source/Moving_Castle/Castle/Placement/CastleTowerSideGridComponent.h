// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CastleGameplayTypes.h"
#include "Components/ActorComponent.h"
#include "CastleTowerSideGridComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UTextRenderComponent;

/** 塔身坐标和朝外的面共同确定一个侧格；同层的四个面各有独立属性。 */
USTRUCT()
struct FCastleTowerSideCell
{
	GENERATED_BODY()

	FIntVector TowerCell = FIntVector::ZeroValue;
	ECastleGridFace Face = ECastleGridFace::NegativeX;
	ECastleTileColor Color = ECastleTileColor::Gray;
	int32 Number = 0;

	/** 面板本身是唯一命中来源；数字不参与射线或导航。 */
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Panel;
	UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> Label;
};

/** 随主塔生存的侧格数据与表现，不负责房间占用、放置或合成。 */
UCLASS()
class UCastleTowerSideGridComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UCastleTowerSideGridComponent();

	/** 以塔底为局部原点生成四面；失败时调用方不提交主塔及本批侧格。 */
	bool Initialize(const FIntVector& BaseCell, int32 LayerCount, float CellWidth, float LayerHeight,
		float SurfaceOffset, int32 Seed, FText& OutReason);

	/** 仅追加新开放的塔层，完整生成之后才加入原侧格索引。 */
	bool ExpandToLayerCount(int32 TargetLayerCount, FText& OutReason);

	/** 不依赖可见状态；导演在奖励阶段仍需要检查隐藏中的侧格是否完整。 */
	bool ValidatePresentation(FText& OutReason) const;
	bool SetPresentationHidden(bool bHidden, FText& OutReason);
	bool IsPresentationHidden() const { return bPresentationHidden; }

	/** 数据按本局生成顺序保留，原型背包从真实侧格取样，不另外造牌色。 */
	const TArray<FCastleTowerSideCell>& GetCells() const { return Cells; }
	const FCastleTowerSideCell* FindCell(const FIntVector& TowerCell, ECastleGridFace Face) const;

	/** 只允许外侧正面命中；侧格背面、薄边和主塔其它网格都不是可选格。 */
	bool TryResolveHit(const FHitResult& Hit, FCastleGridCursorTarget& OutTarget) const;

private:
	/** 使用与旧地块同一正式几何和调色材质，不在缺失时生成替代物。 */
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> PanelMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> PanelMaterial;
	UPROPERTY(Transient) TArray<FCastleTowerSideCell> Cells;

	bool bInitialized = false;
	bool bPresentationHidden = false;
	/** 按本局初始配置采样后续层，记录的是棋盘坐标而非可见楼体的包围盒。 */
	FIntVector TowerBaseCell = FIntVector::ZeroValue;
	int32 GeneratedLayerCount = 0;
	int32 RunSeed = 0;
	float TowerCellWidth = 0.0f;
	float TowerLayerHeight = 0.0f;
	float TowerSurfaceOffset = 0.0f;

	bool AppendLayers(int32 TargetLayerCount, FText& OutReason);
};
