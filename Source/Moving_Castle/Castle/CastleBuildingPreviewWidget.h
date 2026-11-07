// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CastleGameplayTypes.h"
#include "CastleBuildingPreviewWidget.generated.h"

/**
 * 背包卡片中的建筑三维线框预览。
 *
 * 当前阶段没有正式建筑模型，因此这里直接根据建筑描述生成一个小型的
 * 三维占用轮廓，再把它投影到 UMG 的本地画布上。预览只负责表现，不会生成
 * 棋盘建筑 Actor，也不会参与棋盘占用、碰撞或放置判定。
 */
UCLASS()
class MOVING_CASTLE_API UCastleBuildingPreviewWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 三维线段，端点使用预览空间坐标。 */
	struct FPreviewSegment
	{
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
	};

	/** 设置当前卡片要显示的建筑描述。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包预览")
	void SetBuildingDescriptor(const FCastleBuildingDescriptor& InDescriptor);

	/** 清空当前预览；空槽位卡片不会绘制线框。 */
	UFUNCTION(BlueprintCallable, Category = "移动城堡|背包预览")
	void ClearPreview();

	/** 返回当前是否已经配置了有效建筑描述。 */
	UFUNCTION(BlueprintPure, Category = "移动城堡|背包预览")
	bool HasPreviewDescriptor() const
	{
		return bHasDescriptor;
	}

protected:
	/** 每帧轻微旋转预览，使卡片中的线框具有明确的三维深度。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 在 UMG 本地画布中绘制半透明三维线框。 */
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	/** 根据等级生成标准合成占用形状。 */
	void BuildOccupiedCells(TArray<FIntVector>& OutCells) const;

	/** 根据建筑类别为占用形状补充可识别的轮廓细节。 */
	void BuildPreviewSegments(TArray<FPreviewSegment>& OutSegments) const;

	/** 为一个标准网格单元追加 12 条边。 */
	static void AddCellWireframe(const FVector& CellMinimum, const FVector& CellMaximum, TArray<FPreviewSegment>& OutSegments);

	/** 追加箭塔顶部的三角形塔尖。 */
	static void AddArcherTowerDetail(float TopHeight, float CellSize, TArray<FPreviewSegment>& OutSegments);

	/** 追加炮塔顶部的短炮管。 */
	static void AddCannonTowerDetail(float TopHeight, float CellSize, TArray<FPreviewSegment>& OutSegments);

	/** 追加农田顶部的田垄线。 */
	static void AddFarmDetail(float TopHeight, float CellSize, TArray<FPreviewSegment>& OutSegments);

	/** 追加道路的十字通行标记。 */
	static void AddRoadDetail(float TopHeight, float CellSize, TArray<FPreviewSegment>& OutSegments);

	/** 把三维点按固定三分之四视角投影为二维点。 */
	FVector2D ProjectPoint(const FVector& Point) const;

	/** 返回建筑颜色在预览中的高亮色。 */
	FLinearColor GetPreviewColor() const;

	/** 当前卡片的建筑描述。 */
	UPROPERTY(Transient)
	FCastleBuildingDescriptor Descriptor;

	/** 是否已经设置描述；默认空槽位不绘制预览。 */
	UPROPERTY(Transient)
	bool bHasDescriptor = false;

	/** 预览持续旋转角度，单位为度。 */
	UPROPERTY(Transient)
	float RotationDegrees = -32.0f;
};
