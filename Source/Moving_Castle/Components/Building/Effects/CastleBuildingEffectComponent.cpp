#include "Components/Building/Effects/CastleBuildingEffectComponent.h"

#include "CastleBuildingBase.h"
#include "CastleMassUnitSubsystem.h"
#include "Engine/World.h"
#include "Items/CastleBadgeRuntimeSubsystem.h"
#include "Moving_Castle.h"

UCastleBuildingEffectComponent::UCastleBuildingEffectComponent()
{
	// 效果由离散事件推进，不为所有建筑添加逐帧扫描和无意义 Tick。
	PrimaryComponentTick.bCanEverTick = false;
}

bool UCastleBuildingEffectComponent::Trigger(const FCastleBuildingTriggerContext& Context, const bool bIndirectTrigger)
{
	if (!IsInGameThread())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑效果只能在游戏线程触发。"));
		return false;
	}
	const ACastleBuildingBase* Building = Cast<ACastleBuildingBase>(GetOwner());
	if (!IsValid(Building) || !Building->CanReceiveBuildingTrigger() || Context.EntryLocation.ContainsNaN())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑效果拒绝无效、预览或尚未完成建造的目标：%s。"), *GetNameSafe(GetOwner()));
		return false;
	}
	if (Context.Source == ECastleBuildingTriggerSource::NPCEntry)
	{
		const UCastleMassUnitSubsystem* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
		if (!Units || !Units->IsUnitValid(Context.NPC))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建筑效果的 NPC 来源无效或不属于当前世界：%s。"), *Building->GetName());
			return false;
		}
	}
	else if (Context.Source != ECastleBuildingTriggerSource::Manual)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑效果收到未支持的触发来源。"));
		return false;
	}

	// 每次入口单独结算；不能因同帧、相同 NPC 或前一段表现未结束而合并逻辑次数。
	if (TriggerCount == MAX_int64)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑 %s 的效果触发计数已溢出。"), *Building->GetPathName());
		return false;
	}
	const FCastleBuildingTriggerContext PreviousContext = LastTriggerContext;
	FCastleBuildingTriggerContext ResolvedContext = Context;
	ResolvedContext.TriggerNumberBonus = 0;
	UCastleBadgeRuntimeSubsystem* Runtime = nullptr;
	uint64 FirstTowerEntry = 0;
	if (Context.Source == ECastleBuildingTriggerSource::NPCEntry && !bIndirectTrigger && !Context.bChainReactionChild
		&& Building->GetBuildingType() != ECastleBuildingType::NormalRoom)
	{
		// 波次效果在原生结算前预留，炮塔弹药库联动与连锁子触发都不会占用真实进塔机会。
		Runtime = GetWorld()->GetSubsystem<UCastleBadgeRuntimeSubsystem>();
		if (!Runtime)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("功能塔 %s 缺少本局徽章子系统，无法结算 NPC 进入。"), *Building->GetPathName());
			return false;
		}
		FirstTowerEntry = Runtime->ReserveFirstTowerEntry(ResolvedContext.TriggerNumberBonus);
	}
	++TriggerCount;
	LastTriggerContext = ResolvedContext;
	if (!ExecuteEffect(ResolvedContext))
	{
		// 保留“记录→原生执行→表现通知”的既有顺序，但配置/结算失败必须传回调用方，不能报告成功。
		--TriggerCount;
		LastTriggerContext = PreviousContext;
		if (Runtime) { Runtime->CancelFirstTowerEntry(FirstTowerEntry); }
		return false;
	}
	if (ResolvedContext.TriggerNumberBonus > 0)
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("门后备用弦：%s 本波首次进塔，触发数字额外 +%lld。"),
			*Building->GetName(), ResolvedContext.TriggerNumberBonus);
	}
	OnEffectTriggered.Broadcast(ResolvedContext);
	UE_LOG(LogMoving_Castle, Verbose, TEXT("建筑效果：%s，第 %lld 次触发，来源=%s。"),
		*Building->GetName(), TriggerCount, Context.Source == ECastleBuildingTriggerSource::NPCEntry ? TEXT("NPC进入") : TEXT("主动调用"));
	return true;
}

int64 UCastleBuildingEffectComponent::GetTriggeredBuildingNumber(const FCastleBuildingTriggerContext& Context) const
{
	return CastChecked<ACastleBuildingBase>(GetOwner())->GetEffectiveBuildingNumber() + Context.TriggerNumberBonus;
}

bool UCastleBuildingEffectComponent::ExecuteEffect(const FCastleBuildingTriggerContext& Context)
{
	// 基础效果刻意只记录事件；具体建筑以后在原生派生组件中实现各自的攻击或经济规则。
	return true;
}
