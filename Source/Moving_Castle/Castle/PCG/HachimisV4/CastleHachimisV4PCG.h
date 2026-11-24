#pragma once

#include "CoreMinimal.h"
#include "Castle/PCG/CastleBuildingPCGComponent.h"
#include "Castle/PCG/CastlePCGSemanticField.h"
#include "CastleHachimisV4Layout.h"

class ACastleBuildingBase;
class UCastlePCGRuleSetDataAsset;

namespace CastleV4PCG
{
	/** 求解后的层界在此解析为共享装配变换，上下 Actor 各自拥有本格零件。 */
	struct FStairInterface
	{
		const UCastleV4LadderKit* Kit = nullptr;
		FTransform WorldFrame;
		FCastleV4Hatch Hatch;
	};

	using FOccupancy = TMap<FIntVector, ACastleBuildingBase*>;
	using FStairs = TMap<FIntVector, FStairInterface>;

	FTransform CellFrame(const ACastleBoard& Board, const FIntVector& Cell);
	bool MapCells(const ACastleBuildingBase& Building, const ACastleBoard& Board, TMap<FIntVector, FIntVector>& Out, FString& Error);
	bool Extract(ACastleBuildingBase& Building, const ACastleBoard& Board, TArray<FCastlePCGPartAnnotation>& Out, FString& Error);
	/** 把求解器给出的套件、格位、朝向解析为两侧共用世界坐标。 */
	bool BuildStairs(const ACastleBoard& Board, const FOccupancy& Occupancy, const FCastlePCGSemanticField& Field, FStairs& Out, FString& Error);
	/** 上层功能板的原件由具体资产适配器提供，四片补板按同一井口生成。 */
	bool CutDeck(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FIntVector& Cell, double Walk,
		UPrimitiveComponent& Floor, const FStairInterface& Interface, FCastlePCGApplyPlan& Plan, FString& Error);
	void AddLink(FCastlePCGApplyPlan& Plan, const FIntVector& Lower, const FTransform& Frame, const FCastleV4Hatch& Hatch);
	/** 将指定组直接落到对应 Actor，保持同一井口框架。 */
	bool AddStairGroup(const ACastleBuildingBase& Building, const FStairInterface& Stair, FName Group,
		FCastlePCGApplyPlan& Plan, FString& Error);
	/** 屋顶与挂件共享同一种锚定套件，功能盒按声明独立进入玩法计划。 */
	bool AddAttachments(const ACastleBoard& Board, const ACastleBuildingBase& Building, const FCastlePCGSemanticField& Field,
		FCastlePCGApplyPlan& Plan, FString& Error);
	bool PrepareRoom(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
		const FCastlePCGSemanticField& Field, TConstArrayView<FCastlePCGPartAnnotation> Original,
		FCastlePCGApplyPlan& Plan, FString& Error);
	bool Prepare(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
		const FCastlePCGSemanticField& Field, const FStairs& Stairs,
		TConstArrayView<FCastlePCGPartAnnotation> Original, FCastlePCGApplyPlan& Plan, FString& Error);
}
