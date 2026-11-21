#pragma once

#include "CoreMinimal.h"
#include "Castle/PCG/CastleBuildingPCGComponent.h"
#include "CastleHachimisV4Layout.h"

class ACastleBuildingBase;

namespace CastleV4PCG
{
	/** 垂直接口的数据由下层唯一拥有，上层用同一坐标切换地板和功能碰撞。 */
	struct FStairInterface
	{
		const UCastleV4LadderKit* Kit = nullptr;
		FTransform WorldFrame;
		FCastleV4Hatch Hatch;
	};

	/** 普通房样例的一个格。样例以棋盘底面中心放置，每个房间仅持有自己的部分。 */
	struct FRoomSample
	{
		const UCastleHachimisV4Layout* Layout = nullptr;
		FIntVector Cell = FIntVector::ZeroValue;
		FIntVector Origin = FIntVector::ZeroValue;
		bool bTurn = false;
	};

	struct FRoomCell
	{
		FRoomSample Deck;
		FRoomSample Frame;
	};

	using FOccupancy = TMap<FIntVector, ACastleBuildingBase*>;
	using FTargets = TMap<ACastleBuildingBase*, FCastlePCGTarget>;
	using FRooms = TMap<FIntVector, FRoomCell>;
	using FStairs = TMap<FIntVector, FStairInterface>;

	bool CanTraverse(const ACastleBuildingBase& A, const ACastleBuildingBase& B);
	bool IsRoom(const ACastleBuildingBase* Building);
	FTransform CellFrame(const ACastleBoard& Board, const FIntVector& Cell);
	bool MapCells(const ACastleBuildingBase& Building, const ACastleBoard& Board, TMap<FIntVector, FIntVector>& Out, FString& Error);
	bool Extract(ACastleBuildingBase& Building, const ACastleBoard& Board, TArray<FCastlePCGPartAnnotation>& Out, FString& Error);
	bool BuildRooms(const UCastlePCGRuleSetDataAsset& Rules, const FOccupancy& Occupancy, FTargets& Targets, FRooms& Rooms, FString& Error);
	/** 共享楼梯几何来自下层所属资产；旧造型只在与 V4 混接时加入此计划。 */
	bool BuildStairs(const ACastleBoard& Board, const FOccupancy& Occupancy, const FTargets& Targets, const FRooms& Rooms, FStairs& Out, FString& Error);
	/** 在原梯格中选择满足上下房作者家具净空的楼板梯方向，不改家具或共享接口资格。 */
	bool OrientRoomStair(const ACastleBoard& Board, const ACastleBuildingBase& Lower, const ACastleBuildingBase& Upper,
		const FRoomSample& LowerFrame, const FRoomSample& UpperFrame, FStairInterface& Stair, FString& Error);
	bool CutDeck(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FIntVector& Cell, const FIntVector& SourceCell,
		const FStairInterface& Interface, FCastlePCGApplyPlan& Plan, FString& Error);
	void AddLink(FCastlePCGApplyPlan& Plan, const FIntVector& Lower, const FTransform& Frame, const FCastleV4Hatch& Hatch);
	void FinalizeKey(const FCastlePCGTarget& Target, FCastlePCGApplyPlan& Plan);
	bool PrepareRoom(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
		const FOccupancy& Occupancy, const FRooms& Rooms, const FStairs& Stairs, TConstArrayView<FCastlePCGPartAnnotation> Original,
		FCastlePCGApplyPlan& Plan, FString& Error);
	bool Prepare(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
		const FOccupancy& Occupancy, const FRooms& Rooms, const FStairs& Stairs, const FCastlePCGTarget& Target,
		TConstArrayView<FCastlePCGPartAnnotation> Original, FCastlePCGApplyPlan& Plan, FString& Error);
}
