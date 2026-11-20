#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CastleHachimisV4Layout.generated.h"

class UStaticMesh;

/** 一个 FBX 网格节点的局部原点、方向和语义组。重复几何可共用资源，节点身份仍独立。 */
USTRUCT()
struct FCastleV4Part
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="V4")
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FName Node;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FName Group;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FTransform Local;

	/** 屋顶、连接件和底盘的覆盖格。坐标相对未旋转造型的最低占格。 */
	UPROPERTY(VisibleAnywhere, Category="V4")
	TArray<FIntVector> Cells;
};

/** 同系列、等级、安装跨度的作者墙在两种状态下的独立节点。 */
UCLASS()
class MOVING_CASTLE_API UCastleV4WallKit : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category="V4")
	FName WallKey;

	UPROPERTY(VisibleAnywhere, Category="V4")
	double Span = 0.0;

	UPROPERTY(VisibleAnywhere, Category="V4")
	double Opening = 0.0;

	UPROPERTY(VisibleAnywhere, Category="V4")
	TArray<FCastleV4Part> Open;

	UPROPERTY(VisibleAnywhere, Category="V4")
	TArray<FCastleV4Part> Closed;
};

/** 上落脚面、登梯点及梯井均以作者底部锚点为零。 */
USTRUCT()
struct FCastleV4Hatch
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="V4")
	FVector BottomStand = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FVector TopStand = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FVector LadderBottom = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FVector LadderTop = FVector::ZeroVector;

	/** XY 梯井范围；Z 记录梯板标高。坐标包含作者的左右偏置。 */
	UPROPERTY(VisibleAnywhere, Category="V4")
	FBox Opening = FBox(ForceInit);
};

/** 单格的通梯楼板套件，保持每片梯级和铺面网格独立。 */
UCLASS()
class MOVING_CASTLE_API UCastleV4LadderKit : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category="V4")
	FName Family;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FCastleV4Hatch Hatch;

	UPROPERTY(VisibleAnywhere, Category="V4")
	TArray<FCastleV4Part> Parts;
};

/** 真实单格方位由作者墙位映射，建筑旋转后再换算为棋盘坐标。 */
USTRUCT()
struct FCastleV4WallSlot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="V4")
	FName Node;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FIntVector Cell = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FIntVector Facing = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FTransform Mount;

	UPROPERTY(VisibleAnywhere, Category="V4")
	TObjectPtr<UCastleV4WallKit> Kit = nullptr;
};

/** 持久化 FBX 与蓝图之间的一对一装配记录。运行时只引用已烹饪的 UE 资源。 */
UCLASS()
class MOVING_CASTLE_API UCastleHachimisV4Layout : public UDataAsset
{
	GENERATED_BODY()
public:
	static constexpr double CellPitch = 515.0;
	static constexpr double StoryRise = 560.0;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FString SourceFile;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FString BlueprintPath;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FName Family;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FName Form;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FIntVector Grid = FIntVector::ZeroValue;

	UPROPERTY(VisibleAnywhere, Category="V4")
	double StandingHeight = 0.0;

	UPROPERTY(VisibleAnywhere, Category="V4")
	bool bChassisOnly = false;

	UPROPERTY(VisibleAnywhere, Category="V4")
	TObjectPtr<UCastleV4LadderKit> Ladder = nullptr;

	UPROPERTY(VisibleAnywhere, Category="V4")
	FCastleV4Hatch InternalHatch;

	UPROPERTY(VisibleAnywhere, Category="V4")
	TArray<FCastleV4Part> Parts;

	/** 底盘的跨格零件在选中格开洞时按这些预先裁切的片段恢复其余格。 */
	UPROPERTY(VisibleAnywhere, Category="V4")
	TArray<FCastleV4Part> FoundationSlices;

	UPROPERTY(VisibleAnywhere, Category="V4")
	TArray<FCastleV4WallSlot> Walls;

	/** 部件与面锚点共用棋盘节距，移除屋顶和定位接驳使用同一局部中心。 */
	FVector CellBottom(const FIntVector& Cell) const
	{
		return FVector((Cell.X - (Grid.X - 1) * 0.5) * CellPitch,
			(Cell.Y - (Grid.Y - 1) * 0.5) * CellPitch, Cell.Z * StoryRise);
	}
};
