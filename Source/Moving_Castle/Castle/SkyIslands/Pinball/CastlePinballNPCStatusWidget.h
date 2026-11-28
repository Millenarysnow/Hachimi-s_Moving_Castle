#pragma once

#include "Castle/Input/CastleControlMode.h"
#include "Blueprint/UserWidget.h"
#include "CastlePinballNPCStatusWidget.generated.h"

class UTextBlock;

/** 状态数据由 C++ 提供，WBP 只摆放并设置已绑定控件的样式。 */
UCLASS(Abstract)
class MOVING_CASTLE_API UCastlePinballNPCStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 按当前操作模式同步投币键与瞄准态退出键提示。 */
	void RefreshStatus(int32 Gold, int32 Cost, int32 Recruited, int32 Total, double Position, double Angle, ECastleControlMode ControlMode);
	void SetFeedback(const FText& Message);

protected:
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> EconomyText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> RecruitText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> AimText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> ControlsText;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UTextBlock> FeedbackText;
};
