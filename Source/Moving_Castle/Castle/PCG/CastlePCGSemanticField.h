#pragma once

#include "CoreMinimal.h"
#include "CastlePCGTypes.h"

class ACastleBoard;
class UCastleHachimisV4Layout;
class UCastlePCGAttachmentKit;
class UCastlePCGRuleSetDataAsset;
class UCastleV4LadderKit;

/** 单格的放置快照。建筑身份用占格字典序首格表示，规则不用读取 Actor。 */
struct FCastlePCGBoardCell
{
	FIntVector BuildingRoot = FIntVector::ZeroValue;
	const UCastleHachimisV4Layout* Layout = nullptr;
	uint8 Color = 0;
	int32 Number = 0;
	double BoardYaw = 0.0;
	bool bRoom = false;
	/** 格底棋盘朝向在建筑作者坐标中的变换，供外梯与内梯洞进行同空间比较。 */
	FTransform CellInBuilding = FTransform::Identity;
	/** 建筑正式占格数。规则用它区分单格建筑和跨格造型。 */
	int32 BuildingCellCount = 1;
};

/** 同一作者样例被拆给多个单格房拥有，每格记录自己的局部索引。 */
struct FCastlePCGRoomSample
{
	const UCastleHachimisV4Layout* Layout = nullptr;
	FIntVector Cell = FIntVector::ZeroValue;
	FIntVector Origin = FIntVector::ZeroValue;
	bool bTurn = false;
};

enum class ECastlePCGRoof : uint8 { Keep, Hidden, Override };
enum class ECastlePCGDeck : uint8 { Solid, Hatch, Merged };
enum class ECastlePCGSide : uint8 { Closed, OpenDoor, MergedOpen, MainTowerDoor };
enum class ECastlePCGVertical : uint8 { None, Closed, Ladder };

/** 挂点变换相对键引用的格底中心，功能盒也用这一坐标系。 */
struct FCastlePCGAttachmentSpec
{
	const UCastlePCGAttachmentKit* Kit = nullptr;
	FTransform AnchorTransform = FTransform::Identity;
	FVector NavigationExtent = FVector::ZeroVector;
	bool bSelectable = false;
	bool bAffectsNavigation = false;

	bool Validate(FString& Error) const;
};

struct FCastlePCGCellSpec
{
	ECastlePCGRoof Roof = ECastlePCGRoof::Keep;
	ECastlePCGDeck Deck = ECastlePCGDeck::Solid;
	FCastlePCGAttachmentSpec RoofOverride;
	/** 整体屋面替换时，原装接缝和无单格覆盖标记的屋面件也随屋面隐藏。 */
	bool bOverrideWholeRoof = false;
	/** 底盘视觉与物理梯洞分别表达，替换网格不会改变 Deck 或层界。 */
	TOptional<FCastlePCGAttachmentSpec> DeckOverride;
	FCastlePCGRoomSample RoomDeck;
	FCastlePCGRoomSample RoomFrame;
};

struct FCastlePCGSideFaceKey
{
	FIntVector Cell;
	ECastlePCGFace Face;

	bool operator==(const FCastlePCGSideFaceKey&) const = default;
	friend uint32 GetTypeHash(const FCastlePCGSideFaceKey& Key)
	{
		return HashCombine(GetTypeHash(Key.Cell), GetTypeHash(Key.Face));
	}
};

/** 墙是否由本格持有、是否接壤旧占位建筑均由规则明确写入。 */
struct FCastlePCGSideFaceSpec
{
	ECastlePCGSide State = ECastlePCGSide::Closed;
	FName Style;
	bool bHasNeighbor = false;
	bool bNeighborLegacy = false;
	bool bOwnsWall = true;

	bool IsDoor() const { return State == ECastlePCGSide::OpenDoor || State == ECastlePCGSide::MainTowerDoor; }
	bool IsConnected() const { return IsDoor() || State == ECastlePCGSide::MergedOpen; }
};

/** 层界键是下格；套件和旋转供上下两侧共同装配。 */
struct FCastlePCGVerticalSpec
{
	ECastlePCGVertical State = ECastlePCGVertical::None;
	const UCastleV4LadderKit* Kit = nullptr;
	double BoardYaw = 0.0;
};

/** 同一空间格可以有多个挂载件，命名锚点与槽位共同构成唯一身份。 */
struct FCastlePCGAttachmentKey
{
	FIntVector OwnerCell;
	FName Anchor;
	FName SlotTag;

	bool operator==(const FCastlePCGAttachmentKey&) const = default;
	friend uint32 GetTypeHash(const FCastlePCGAttachmentKey& Key)
	{
		return HashCombine(GetTypeHash(Key.OwnerCell), HashCombine(GetTypeHash(Key.Anchor), GetTypeHash(Key.SlotTag)));
	}
};

/** 棋盘求解阶段可写，装配阶段以 const 读取的四个空间域。 */
struct MOVING_CASTLE_API FCastlePCGSemanticField
{
	TMap<FIntVector, FCastlePCGCellSpec> Cells;
	TMap<FCastlePCGSideFaceKey, FCastlePCGSideFaceSpec> SideFaces;
	TMap<FIntVector, FCastlePCGVerticalSpec> VerticalInterfaces;

	/** 挂件必经插入接口，重复键与无效网格在求解期间返回错误。 */
	bool AddAttachment(const FCastlePCGAttachmentKey& Key, const FCastlePCGAttachmentSpec& Spec, FString& Error);
	const TMap<FCastlePCGAttachmentKey, FCastlePCGAttachmentSpec>& GetAttachments() const { return Attachments; }

private:
	TMap<FCastlePCGAttachmentKey, FCastlePCGAttachmentSpec> Attachments;
};

/** 规则只访问已完成占格快照、棋盘几何和资源配置。 */
class MOVING_CASTLE_API FCastlePCGBoardQuery
{
public:
	inline static const FIntVector Up{0, 0, 1};
	inline static constexpr ECastlePCGFace Sides[] = {
		ECastlePCGFace::PositiveX, ECastlePCGFace::NegativeX, ECastlePCGFace::PositiveY, ECastlePCGFace::NegativeY};

	FCastlePCGBoardQuery(const ACastleBoard& Board, const TMap<FIntVector, ACastleBuildingBase*>& Occupancy, UObject* InNoise);
	/** 无世界规则测试可直接构造同一套纯数据查询。 */
	FCastlePCGBoardQuery(TMap<FIntVector, FCastlePCGBoardCell> InCells, const UCastlePCGRuleSetDataAsset& InRules,
		UObject* InNoise, int32 InSeed, int32 InGroundLayer, TOptional<FIntVector> InTowerColumn = {});

	const TArray<FIntVector>& OrderedCells() const { return SortedCells; }
	const FCastlePCGBoardCell* Find(const FIntVector& Cell) const { return Cells.Find(Cell); }
	const FCastlePCGBoardCell* FindOffset(const FIntVector& Cell, const FIntVector& Offset) const { return Find(Cell + Offset); }
	bool IsSkyExposed(const FIntVector& Cell) const;
	int32 GetOverhangDepth(const FIntVector& Cell) const;
	bool IsOverhanging(const FIntVector& Cell) const { return GetOverhangDepth(Cell) > 0; }
	bool IsMainTowerDoorFace(const FIntVector& Cell, const FIntVector& Side) const;
	/** 主塔侧建模式下，塔柱在任意楼层的 XY 投影都可承托上层构件。 */
	bool IsMainTowerColumn(const FIntVector& Cell) const;
	bool SampleNoise(const FIntVector& Cell, FName Channel, float& Value, FString& Error) const;
	/** 水平连通分量按已求出的侧面计算，根节点是每个分量的字典序首格。 */
	TMap<FIntVector, FIntVector> HorizontalComponents(const FCastlePCGSemanticField& Field, bool bRoomsOnly) const;
	const UCastlePCGRuleSetDataAsset& Config() const { return *Rules; }
	int32 GetGroundLayer() const { return GroundLayer; }
	double GetCellPitch() const { return Pitch; }
	double GetLayerHeight() const { return LayerHeight; }
	FVector CellBottom(const FIntVector& Cell) const;

private:
	void SortCells();
	TMap<FIntVector, FCastlePCGBoardCell> Cells;
	TArray<FIntVector> SortedCells;
	const UCastlePCGRuleSetDataAsset* Rules;
	UObject* Noise;
	int32 Seed;
	int32 GroundLayer;
	double Pitch = 515.0;
	double LayerHeight = 560.0;
	bool bTowerSides = false;
	FIntVector TowerCell = FIntVector::ZeroValue;
};
