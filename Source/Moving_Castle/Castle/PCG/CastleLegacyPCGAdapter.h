#pragma once

#include "HachimisV4/CastleHachimisV4PCG.h"

/** 非 V4 占位建筑的旧资产契约，正式 V4 装配不依赖旧模板。 */
namespace CastleLegacyPCG
{
	struct FVariants
	{
		TArray<FCastlePCGMeshTemplate> LV1;
		TArray<FCastlePCGMeshTemplate> LV2;
	};

	bool ReadVariants(UWorld& World, const UCastlePCGRuleSetDataAsset& Rules, FVariants& Out, FString& Error);
	bool Extract(ACastleBuildingBase& Building, const ACastleBoard& Board, TArray<FCastlePCGPartAnnotation>& OutParts, FString& Error);
	FCastleV4Hatch LadderHatch(double LayerHeight);
	bool Prepare(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
		const FVariants& Variants, const FCastlePCGSemanticField& Field, const CastleV4PCG::FStairs& Stairs,
		TConstArrayView<FCastlePCGPartAnnotation> OriginalParts, FCastlePCGApplyPlan& Plan, FString& Error);
	/** 只在含旧占位建筑的棋盘调用：为 V4 一侧补旧地板件和高差过渡。 */
	void PrepareMixed(const ACastleBoard& Board, const ACastleBuildingBase& Building, const UCastlePCGRuleSetDataAsset& Rules,
		const FVariants& Variants, const FCastlePCGSemanticField& Field, const CastleV4PCG::FStairs& Stairs,
		FCastlePCGApplyPlan& Plan);
}
