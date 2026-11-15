#include "CastleFarmEffectComponent.h"

#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleEconomyComponent.h"
#include "CastleMassUnitSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Moving_Castle.h"

bool UCastleFarmEffectComponent::ExecuteEffect(const FCastleBuildingTriggerContext& Context)
{
	ACastleBuildingBase* Farm = CastChecked<ACastleBuildingBase>(GetOwner());
	const int32 Level = Farm->GetBuildingLevel();
	if (Level < 1 || Level > 5 || Farm->GetBuildingNumber() <= 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("农田 %s 结算失败：等级或自身数字无效。"), *Farm->GetPathName());
		return false;
	}
	int64 Total = Farm->GetBuildingNumber();
	if (Level > 1)
	{
		ACastleBoard* Board = ACastleBoard::FindBoardForBuilding(Farm);
		if (!Board)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("农田 %s 无所属棋盘，无法查询同色连通农田。"), *Farm->GetPathName());
			return false;
		}
		TArray<ACastleBuildingBase*> Candidates;
		if (Level >= 4) { Candidates = Board->GetPlacedBuildings(); }
		else if (!Board->GetBuildingsInExpandedBounds(Farm, Level == 2 ? FIntVector(1, 1, 0) : FIntVector(1, 1, 1), Candidates)) { return false; }
		// D12 要求在等级范围内的同色农田连通；普通房间、异色农田均不能充当传递桥梁。
		Candidates = Candidates.FilterByPredicate([Farm](const ACastleBuildingBase* Other)
		{
			return Other->GetBuildingType() == ECastleBuildingType::Farm && Other->GetBuildingColor() == Farm->GetBuildingColor();
		});
		TArray<ACastleBuildingBase*> Connected;
		if (!Board->GetConnectedBuildings(Farm, Candidates, Connected)) { return false; }
		Total = 0;
		for (const ACastleBuildingBase* Other : Connected) { Total += Other->GetBuildingNumber(); }
	}
	if (Total <= 0 || Total > MAX_int32)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("农田 %s 产金数字超出金币整数范围：%lld。"), *Farm->GetPathName(), Total);
		return false;
	}
	UCastleEconomyComponent* Economy = EconomyOverride;
	if (EconomyOverride == nullptr)
	{
		APlayerController* Player = GetWorld()->GetFirstPlayerController();
		Economy = Player ? Player->FindComponentByClass<UCastleEconomyComponent>() : nullptr;
	}
	if (!IsValid(Economy))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("农田 %s 缺少有效金币账户：请配置本世界玩家控制器的 Economy 或显式绑定 EconomyOverride。"), *Farm->GetPathName());
		return false;
	}
	FText Reason;
	const int32 Gold = static_cast<int32>(Total);
	if (!Economy->CanAddGold(Gold, Reason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("农田 %s 无法入账，账户=%s，原因=%s。"), *Farm->GetPathName(), *Economy->GetPathName(), *Reason.ToString());
		return false;
	}
	UCastleMassUnitSubsystem* Units = nullptr;
	const bool bGrantBuff = Level == 5 && Context.Source == ECastleBuildingTriggerSource::NPCEntry;
	const float BonusDelta = 0.1f * Farm->GetBuildingNumber();
	if (bGrantBuff)
	{
		Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
		float PreviousBonus = 0.0f;
		if (!Units || !Units->GetNPCFarmBuff(Context.NPC, PreviousBonus) || !FMath::IsFinite(PreviousBonus + BonusDelta))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("五级农田 %s 无法向有效 NPC 叠加有限农田增益。"), *Farm->GetPathName());
			return false;
		}
	}
	if (!Economy->TryAddGold(Gold, Reason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("农田 %s 提交金币失败，账户=%s，原因=%s。"), *Farm->GetPathName(), *Economy->GetPathName(), *Reason.ToString());
		return false;
	}
	if (bGrantBuff && !Units->AddNPCFarmBuff(Context.NPC, BonusDelta)) { return false; }
	LastGoldGranted = Gold;
	UE_LOG(LogMoving_Castle, Display, TEXT("农田 %s Lv%d 入账 %d 金币%s。"), *Farm->GetName(), Level, Gold,
		bGrantBuff ? TEXT("，NPC 已叠加本座数字 10% 的攻击增益") : TEXT(""));
	return true;
}
