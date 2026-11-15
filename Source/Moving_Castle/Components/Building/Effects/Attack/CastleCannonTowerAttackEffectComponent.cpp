#include "CastleCannonTowerAttackEffectComponent.h"

#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "Moving_Castle.h"

bool UCastleCannonTowerAttackEffectComponent::GetAmmunitionBuildings(TArray<ACastleBuildingBase*>& OutBuildings) const
{
	OutBuildings.Reset();
	const ACastleBuildingBase* Cannon = Cast<ACastleBuildingBase>(GetOwner());
	if (!IsValid(Cannon) || Cannon->GetBuildingLevel() < 1 || Cannon->GetBuildingLevel() > 5 || Cannon->GetBuildingNumber() <= 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("炮塔 %s 查询弹药库失败：拥有者、等级或数字无效。"), *GetPathNameSafe(Cannon));
		return false;
	}
	const int32 Level = Cannon->GetBuildingLevel();
	// D9 明确一级没有弹药库，独立生成的一级炮塔无需棋盘空间查询。
	if (Level == 1) { return true; }
	ACastleBoard* Board = ACastleBoard::FindBoardForBuilding(Cannon);
	if (!Board)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("炮塔 %s 无所属棋盘，无法收录弹药库。"), *Cannon->GetPathName());
		return false;
	}
	const FIntVector Expansion = Level == 2 ? FIntVector(1, 1, 0) : Level == 3 ? FIntVector(1, 1, 1) : FIntVector(2, 2, 2);
	TArray<ACastleBuildingBase*> Candidates;
	if (!Board->GetBuildingsInExpandedBounds(Cannon, Expansion, Candidates)) { return false; }
	for (ACastleBuildingBase* Other : Candidates)
	{
		// D9 允许任意类别，仅用自身排除、建造状态和同数字定义弹药库集合。
		if (Other != Cannon && Other->GetBuildingNumber() == Cannon->GetBuildingNumber()) { OutBuildings.Add(Other); }
	}
	return true;
}

bool UCastleCannonTowerAttackEffectComponent::ExecuteEffect(const FCastleBuildingTriggerContext& Context)
{
	const UCastleTowerAttackDataAsset* Data = nullptr;
	const FCastleTowerAttackLevelData* LevelData = nullptr;
	if (!ResolveAttackConfiguration(Data, LevelData)) { return false; }
	const ACastleBuildingBase* Cannon = CastChecked<ACastleBuildingBase>(GetOwner());
	TArray<ACastleBuildingBase*> Ammunition;
	if (!GetAmmunitionBuildings(Ammunition)) { return false; }
	int64 Numbers = Cannon->GetBuildingNumber();
	for (const ACastleBuildingBase* Other : Ammunition) { Numbers += Other->GetBuildingNumber() + (Cannon->GetBuildingLevel() == 5 ? 1 : 0); }
	float FarmBonus = 0.0f;
	if (!GetTriggerFarmBonus(Context, FarmBonus)) { return false; }
	const double Damage = (static_cast<double>(Numbers) + FarmBonus) * LevelData->ProjectileDamage;
	if (!FMath::IsFinite(Damage) || Damage <= 0.0 || Damage > MAX_flt || PendingShotDamages.Num() == MAX_int32)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("炮塔 %s 炮击伤害或队列溢出，攻击资产=%s。"), *Cannon->GetPathName(), *Data->GetPathName());
		return false;
	}
	if (!QueueShots(1)) { return false; }
	PendingShotDamages.Add(static_cast<float>(Damage));
	UE_LOG(LogMoving_Castle, Display, TEXT("炮塔 %s 入队一发追踪炮弹，伤害 %.2f，弹药库 %d 座。"), *Cannon->GetName(), Damage, Ammunition.Num());
	return true;
}

bool UCastleCannonTowerAttackEffectComponent::FireOneShot(const UCastleTowerAttackDataAsset& DataAsset,
	const FCastleTowerAttackLevelData& LevelData)
{
	if (PendingShotDamages.IsEmpty())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("炮塔 %s 待射数量与逐发伤害队列不一致。"), *GetNameSafe(GetOwner()));
		return false;
	}
	if (!FireProjectile(DataAsset, LevelData, PendingShotDamages[0])) { return false; }
	// D11 无目标同样消费这一发；Mass 错误返回 false 时保留伤害，不能与后续 NPC 的炮击串位。
	PendingShotDamages.RemoveAt(0, EAllowShrinking::No);
	return true;
}

void UCastleCannonTowerAttackEffectComponent::OnBoardRegistered(ACastleBoard& Board)
{
	check(!BoardTriggerHandle.IsValid());
	RegisteredBoard = &Board;
	BoardTriggerHandle = Board.OnBuildingTriggered.AddUObject(this, &UCastleCannonTowerAttackEffectComponent::HandleBoardBuildingTriggered);
}

void UCastleCannonTowerAttackEffectComponent::HandleBoardBuildingTriggered(ACastleBuildingBase* Source,
	const FCastleBuildingTriggerContext& Context)
{
	// D10 已确认：本体主动调用由建筑入口正常处理，附属弹药库只有真实 NPC 进入才联动。
	// 建造中的炮塔不属于可触发集合；自身事件已经入队，不能再把自己当作弹药库重复结算。
	if (Context.Source != ECastleBuildingTriggerSource::NPCEntry || Source == GetOwner()) { return; }
	const ACastleBuildingBase* Cannon = CastChecked<ACastleBuildingBase>(GetOwner());
	if (!Cannon->CanReceiveBuildingTrigger()) { return; }
	TArray<ACastleBuildingBase*> Ammunition;
	if (!GetAmmunitionBuildings(Ammunition)) { return; }
	if (Ammunition.Contains(Source))
	{
		// 刻意调用组件统一入口而不是建筑入口：保留校验、计数与伤害结算，但不再次广播棋盘事件。
		if (!Trigger(Context))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("炮塔 %s 的弹药库 %s 进入联动失败。"), *Cannon->GetPathName(), *GetPathNameSafe(Source));
		}
	}
}

void UCastleCannonTowerAttackEffectComponent::OnBoardUnregistered(ACastleBoard& Board)
{
	// 多格棋盘在 teardown 中可能重复通知，句柄解除是幂等生命周期操作。
	if (RegisteredBoard.Get() == &Board)
	{
		Board.OnBuildingTriggered.Remove(BoardTriggerHandle);
		BoardTriggerHandle.Reset();
		RegisteredBoard.Reset();
	}
}

void UCastleCannonTowerAttackEffectComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACastleBoard* Board = RegisteredBoard.Get()) { OnBoardUnregistered(*Board); }
	PendingShotDamages.Reset();
	Super::EndPlay(EndPlayReason);
}
