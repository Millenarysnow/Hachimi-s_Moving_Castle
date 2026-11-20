// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleEconomyHUDWidget.h"

#include "CastleEconomyComponent.h"
#include "CastlePlayerController.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Moving_Castle.h"

bool UCastleEconomyHUDWidget::BindToOwningEconomy()
{
	if (!IsValid(GoldText) || !IsValid(CoinIcon) || !IsValid(CoinIcon->GetBrush().GetResourceObject()))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("金币 HUD %s 缺少 GoldText、CoinIcon 或金币贴图。"), *GetClass()->GetPathName());
		return false;
	}
	// 尺寸必须由 WBP 笔刷明确保存；不在运行时猜尺寸，否则有贴图也可能不显示。
	const FSlateBrush& CoinBrush = CoinIcon->GetBrush();
	const auto IconSize = CoinBrush.GetImageSize();
	if (!(IconSize.X > 0.0f && IconSize.Y > 0.0f) || CoinBrush.GetDrawType() == ESlateBrushDrawType::NoDrawType)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("金币 HUD %s 的 CoinIcon 笔刷尺寸或绘制方式无效；贴图：%s。"),
			*GetClass()->GetPathName(), *GetPathNameSafe(CoinBrush.GetResourceObject()));
		return false;
	}

	// 以创建控件的玩家为唯一账户来源；测试关卡和正式关卡使用同一个 WBP。
	const ACastlePlayerController* Controller = Cast<ACastlePlayerController>(GetOwningPlayer());
	if (!IsValid(Controller) || !IsValid(Controller->GetEconomy()))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("金币 HUD %s 的拥有者 %s 没有有效的城堡金币账户。"),
			*GetClass()->GetPathName(), *GetPathNameSafe(GetOwningPlayer()));
		return false;
	}
	UCastleEconomyComponent* Account = Controller->GetEconomy();
	Economy = Account;
	Account->OnGoldChanged.AddUniqueDynamic(this, &UCastleEconomyHUDWidget::HandleGoldChanged);
	HandleGoldChanged(Account->GetGold(), Account->GetGold());
	return true;
}

void UCastleEconomyHUDWidget::HandleGoldChanged(int32 PreviousGold, int32 CurrentGold)
{
	// 只使用提交后的余额，避免初始发放和后续收支由两个不同的公式驱动 UI。
	GoldText->SetText(FText::Format(NSLOCTEXT("CastleEconomyHUD", "Balance", "金币 {0}"), FText::AsNumber(CurrentGold)));
}

void UCastleEconomyHUDWidget::NativeDestruct()
{
	if (UCastleEconomyComponent* Account = Economy.Get())
	{
		Account->OnGoldChanged.RemoveDynamic(this, &UCastleEconomyHUDWidget::HandleGoldChanged);
		Economy.Reset();
	}
	Super::NativeDestruct();
}
