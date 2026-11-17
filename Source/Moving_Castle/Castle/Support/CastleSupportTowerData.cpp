#include "CastleSupportTowerData.h"

#include "CastleMassTraits.h"
#include "MassEntityConfigAsset.h"
#include "Materials/MaterialInterface.h"

bool UCastleSupportTowerData::ValidateFor(const ECastleBuildingType Type, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	auto Fail = [this, &OutReason](const TCHAR* Reason)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("支援塔资产 %s：%s"), *GetPathName(), Reason));
		return false;
	};
	auto Positive = [](const float Value) { return FMath::IsFinite(Value) && Value > 0.0f; };
	if (!IsValid(FlightMaterial)) { return Fail(TEXT("必须指定飞行表现材质。")); }
	if (Type == ECastleBuildingType::BeeHive)
	{
		return (Positive(BeeDamage) && Positive(BeeFlightSpeed)) || Fail(TEXT("每只蜂伤害及飞行速度必须为有限正数。"));
	}
	if (Type != ECastleBuildingType::BalloonTower) { return Fail(TEXT("建筑类别不是支援塔。")); }
	if (!Positive(BalloonSpeed) || !Positive(BalloonHealth) || !Positive(AttractionRadius)
		|| !Positive(LandingRadius) || !Positive(CargoFallSpeed) || !Positive(MaxDropDepth))
	{
		return Fail(TEXT("气球生命、航速、仇恨范围、落地范围、货箱落速和最大下落距离必须为有限正数。"));
	}
	if (!IsValid(NPCConfiguration) || !NPCConfiguration->FindTrait(UCastleMassNPCTrait::StaticClass()))
	{
		return Fail(TEXT("蓝色空投必须配置带 CastleMassNPCTrait 的人员资产。"));
	}
	if (BalloonPath.Num() < 2) { return Fail(TEXT("气球路径至少需要两个航点。")); }
	for (int32 Index = 0; Index < BalloonPath.Num(); ++Index)
	{
		if (BalloonPath[Index].ContainsNaN() || BalloonPath[Index].Z <= 0.0
			|| (Index > 0 && BalloonPath[Index].Equals(BalloonPath[Index - 1], 0.01)))
		{
			return Fail(TEXT("气球航点必须有限、位于塔顶格心上方，且相邻航点不得重合。"));
		}
	}
	return true;
}
