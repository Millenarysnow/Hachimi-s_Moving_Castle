#include "CastlePinballNPCConsoleComponent.h"

#include "CastlePinballNPCSkyIsland.h"
#include "CastlePlayerController.h"
#include "Moving_Castle.h"

FText UCastlePinballNPCConsoleComponent::GetInteractPrompt() const
{
	return INVTEXT("操控弹珠机");
}

bool UCastlePinballNPCConsoleComponent::CanInteract(
	const FCastleSkyIslandInteractContext& Context, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (Context.Island != GetOwner() || !IsValid(Context.Controller) || Context.Hit.GetComponent() != this)
	{
		OutReason = INVTEXT("投币控制台必须属于当前空岛并被准星命中。");
		UE_LOG(LogMoving_Castle, Error, TEXT("投币控制台 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	return true;
}

bool UCastlePinballNPCConsoleComponent::CanHighlightInteract(
	const FCastleSkyIslandInteractContext& Context) const
{
	// 纯查询不进入投币瞄准模式，也不复用错误日志型交互入口。
	return IsValid(GetOwner()) && Context.Island == GetOwner() && IsValid(Context.Controller)
		&& Context.Hit.GetComponent() == this;
}

bool UCastlePinballNPCConsoleComponent::ExecuteInteract(
	const FCastleSkyIslandInteractContext& Context, FText& OutFailureReason)
{
	return CanInteract(Context, OutFailureReason)
		&& CastChecked<ACastlePinballNPCSkyIsland>(GetOwner())->EnterAimMode(*Context.Controller, OutFailureReason);
}
