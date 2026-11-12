#include "Components/Building/Effects/CastleBuildingEffectComponent.h"

#include "CastleBuildingBase.h"
#include "CastleMassUnitSubsystem.h"
#include "Engine/World.h"
#include "Moving_Castle.h"

UCastleBuildingEffectComponent::UCastleBuildingEffectComponent()
{
	// 效果由离散事件推进，不为所有建筑添加逐帧扫描和无意义 Tick。
	PrimaryComponentTick.bCanEverTick = false;
}

bool UCastleBuildingEffectComponent::Trigger(const FCastleBuildingTriggerContext& Context)
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
	++TriggerCount;
	LastTriggerContext = Context;
	ExecuteEffect(Context);
	OnEffectTriggered.Broadcast(Context);
	UE_LOG(LogMoving_Castle, Verbose, TEXT("建筑效果：%s，第 %lld 次触发，来源=%s。"),
		*Building->GetName(), TriggerCount, Context.Source == ECastleBuildingTriggerSource::NPCEntry ? TEXT("NPC进入") : TEXT("主动调用"));
	return true;
}

void UCastleBuildingEffectComponent::ExecuteEffect(const FCastleBuildingTriggerContext& Context)
{
	// 基础效果刻意只记录事件；具体建筑以后在原生派生组件中实现各自的攻击或经济规则。
}
