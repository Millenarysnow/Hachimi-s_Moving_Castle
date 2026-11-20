// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "CastleEconomyHUDWidget.generated.h"

class UCastleEconomyComponent;
class UImage;
class UTextBlock;

/** 金币 HUD 的 C++ 数据绑定。图标、排版和快捷键提示由 WBP 编辑，控件不参与交易或输入。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastleEconomyHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 挂载前校验 WBP 和账户，订阅真实余额变化并立即同步一次；失败时不挂载。 */
	bool BindToOwningEconomy();

protected:
	/** HUD 离开视口时解除本局账户订阅，避免重载关卡后旧界面继续接收事件。 */
	virtual void NativeDestruct() override;

private:
	/** 购买、出售、奖励等只读同一账户通知，不通过 Tick 轮询或在界面重复计算。 */
	UFUNCTION()
	void HandleGoldChanged(int32 PreviousGold, int32 CurrentGold);

	/** WBP 提供徽标与数值标签；绑定与资源缺失均视为资产错误。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> CoinIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> GoldText;

	/** 账户归本地玩家控制器，HUD 不延长其生命周期。 */
	TWeakObjectPtr<UCastleEconomyComponent> Economy;
};
