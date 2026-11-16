// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CastlePCGTypes.generated.h"

class ACastleBuildingBase;
class UPrimitiveComponent;

/** 建筑美术零件参与语义规则时使用的稳定角色，不以组件名或创建顺序作为身份。 */
UENUM(BlueprintType)
enum class ECastlePCGPartRole : uint8
{
	None UMETA(DisplayName="未标注"),
	Roof UMETA(DisplayName="屋顶"),
	TopCeilingSlab UMETA(DisplayName="顶平楼板"),
	PerimeterRailing UMETA(DisplayName="周围护栏"),
	Structural UMETA(DisplayName="柱梁结构")
};

/** 以棋盘局部坐标表示建筑零件所朝向的外侧方位面。 */
UENUM(BlueprintType)
enum class ECastlePCGFace : uint8
{
	None UMETA(DisplayName="无"),
	PositiveX UMETA(DisplayName="棋盘 +X"),
	NegativeX UMETA(DisplayName="棋盘 -X"),
	PositiveY UMETA(DisplayName="棋盘 +Y"),
	NegativeY UMETA(DisplayName="棋盘 -Y"),
	PositiveZ UMETA(DisplayName="棋盘 +Z"),
	NegativeZ UMETA(DisplayName="棋盘 -Z")
};

/** 单个原始模型组件的语义快照，局部占格偏移以建筑占格包围盒最小坐标为原点。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastlePCGPartAnnotation
{
	GENERATED_BODY()

	/** 原始美术组件引用；后续规则只控制显隐，不以动态变体取代此引用。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG")
	TObjectPtr<UPrimitiveComponent> Component = nullptr;

	/** 显式标签或几何提取得到的角色，用于声明式规则谓词。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG")
	ECastlePCGPartRole Role = ECastlePCGPartRole::None;

	/** 相对于建筑最小占格的三维偏移，不是 Actor 局部空间中的厘米坐标。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG")
	FIntVector LocalCellOffset = FIntVector::ZeroValue;

	/** 已计入建筑旋转的棋盘方位，避免沿 Y 放置时仍匹配原始模型 X 方位。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG")
	ECastlePCGFace BoardFacing = ECastlePCGFace::None;

	/** 缓存作者初始可见性；邻居移除时必须恢复此值而非统一显示全部组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG")
	bool bOriginalVisible = true;
};

/** 两个不同建筑在单个棋盘占格上形成的六向语义邻接。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastlePCGCellAdjacency
{
	GENERATED_BODY()

	/** 邻接描述符的起始建筑，必须是已完成正式放置的棋盘建筑。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG")
	TObjectPtr<ACastleBuildingBase> FirstBuilding = nullptr;

	/** 邻接面的另一座建筑；同一建筑内部相邻格不生成跨建筑替换。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG")
	TObjectPtr<ACastleBuildingBase> SecondBuilding = nullptr;

	/** 起始建筑在本次邻接中的绝对棋盘占格。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG")
	FIntVector FirstCell = FIntVector::ZeroValue;

	/** 终点建筑在本次邻接中的绝对棋盘占格。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG")
	FIntVector SecondCell = FIntVector::ZeroValue;

	/** 从 FirstCell 指向 SecondCell 的六邻单位偏移。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG")
	FIntVector Offset = FIntVector::ZeroValue;

	/** 同色或同数字为可通行；仅表示语义关系，不等同于导航路径已完成构建。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG")
	bool bCanTraverse = false;
};

/** 当前棋盘由变体 LV2 激活的一处上下梯子连接，供导航系统查询其端点与方向。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleActiveLadderConnection
{
	GENERATED_BODY()

	/** 拥有变体 LV2 美术与功能组件的下层建筑。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG|梯子")
	TObjectPtr<ACastleBuildingBase> Building = nullptr;

	/** 唯一梯子所在的下层绝对棋盘占格。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG|梯子")
	FIntVector LowerCell = FIntVector::ZeroValue;

	/** 下层占格正上方、属于另一座建筑的绝对占格。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG|梯子")
	FIntVector UpperCell = FIntVector::ZeroValue;

	/** 梯子底端的精确世界坐标，包含棋盘与建筑的空间变换。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG|梯子")
	FVector BottomWorldLocation = FVector::ZeroVector;

	/** 梯子顶端的精确世界坐标，不能用世界 Z 固定加层高代替旋转变换。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG|梯子")
	FVector TopWorldLocation = FVector::ZeroVector;

	/** 梯面的世界朝向，供导航消费者校准接驳与攀爬面向。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="建筑PCG|梯子")
	FRotator WorldFacing = FRotator::ZeroRotator;
};
