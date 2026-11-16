#include "CastleBuildingPCGRule.h"
#include "CastlePCGSemantics.h"
#include "CastleDeterministicPCGNoise.h"
#include "Engine/StaticMesh.h"

FString FCastlePCGTarget::StableKey() const
{
	// 显式排序消除 TMap、棋盘插入顺序和 UObject 地址的影响。
	TArray<FString> Keys;
	for (const auto& Pair : Ceilings) { Keys.Add(FString::Printf(TEXT("Z:%d,%d,%d:%d"), Pair.Key.X, Pair.Key.Y, Pair.Key.Z, Pair.Value)); }
	for (const auto& Face : Faces) { Keys.Add(FString::Printf(TEXT("F:%d,%d,%d:%d:%d"), Face.Cell.X, Face.Cell.Y, Face.Cell.Z, static_cast<int32>(Face.Face), Face.bConnected)); }
	Keys.Sort();
	return FString::Join(Keys, TEXT(";"));
}

bool UCastlePCGVerticalRule::Evaluate(const FCastlePCGRuleContext& Context, FCastlePCGTarget& Out, FString& Error) const
{
	TMap<ACastleBuildingBase*, TArray<FIntVector>> ConnectedPairs;
	for (const auto& Adj : Context.Adjacencies)
	{
		if (Adj.Offset != FIntVector(0, 0, 1)) { continue; }
		Out.Ceilings.Add(Adj.FirstCell, 1);
		if (Adj.bCanTraverse) { ConnectedPairs.FindOrAdd(Adj.SecondBuilding).Add(Adj.FirstCell); }
	}
	for (auto& Pair : ConnectedPairs)
	{
		Pair.Value.Sort(CastlePCGSemantics::CellLess);
		FIntVector Selected = Pair.Value[0];
		if (Context.bUseLadderNoise)
		{
			if (!Context.NoiseProvider || !Context.NoiseProvider->Implements<UCastlePCGNoiseProvider>() || Context.LadderChannel.IsNone())
			{ Error = TEXT("梯子择格缺少有效噪声接口或通道。"); return false; }
			float Best = -1.0f;
			for (const auto& Cell : Pair.Value)
			{
				const float Noise = ICastlePCGNoiseProvider::Execute_SampleNoise(Context.NoiseProvider, Context.Seed, Cell, Context.LadderChannel);
				if (!FMath::IsFinite(Noise) || Noise < 0.0f || Noise >= 1.0f)
				{ Error = TEXT("梯子噪声接口返回了 [0,1) 之外的值。"); return false; }
				if (Noise > Best) { Best = Noise; Selected = Cell; }
			}
		}
		Out.Ceilings[Selected] = 2;
	}
	return true;
}

bool UCastlePCGPlanarRule::Evaluate(const FCastlePCGRuleContext& Context, FCastlePCGTarget& Out, FString& Error) const
{
	for (const auto& Adj : Context.Adjacencies)
	{
		if (Adj.Offset.Z != 0) { continue; }
		ECastlePCGFace Face = Adj.Offset.X > 0 ? ECastlePCGFace::PositiveX : ECastlePCGFace::NegativeX;
		if (Adj.Offset.Y != 0) { Face = Adj.Offset.Y > 0 ? ECastlePCGFace::PositiveY : ECastlePCGFace::NegativeY; }
		Out.Faces.Add({Adj.FirstCell, Face, Adj.bCanTraverse});
	}
	return true;
}

bool UCastlePCGRuleSetDataAsset::ValidateConfiguration(FString& Error) const
{
	if (!VariantLV1ActorClass || !VariantLV2ActorClass || !FlatFloorSlabMesh)
	{ Error = FString::Printf(TEXT("规则集 %s 缺少 LV1/LV2 变体类或封闭平楼板。"), *GetPathName()); return false; }
	if (LadderSelectionNoiseChannel.IsNone() || !FMath::IsFinite(BridgeWidth) || BridgeWidth <= 70.0 || BridgeWidth > 500.0
		|| !FMath::IsFinite(FloorThickness) || FloorThickness <= 0.0 || !FMath::IsFinite(BarrierHeight) || BarrierHeight < 100.0)
	{ Error = FString::Printf(TEXT("规则集 %s 的通道、桥宽、厚度或阻隔高度非法。"), *GetPathName()); return false; }
	if (Rules.IsEmpty()) { Error = FString::Printf(TEXT("规则集 %s 未配置语义规则。"), *GetPathName()); return false; }
	for (const UCastleBuildingPCGRule* Rule : Rules) { if (!Rule) { Error = FString::Printf(TEXT("规则集 %s 含空规则。"), *GetPathName()); return false; } }
	return true;
}