#include "CastlePinballNPCStatusWidget.h"

#include "Components/TextBlock.h"

void UCastlePinballNPCStatusWidget::RefreshStatus(
	int32 Gold, int32 Cost, int32 Recruited, int32 Total, double Position, double Angle)
{
	// HUD 不参与扣费或命中判定。文字取实际余额和模拟提交后的招募数。
	EconomyText->SetText(FText::Format(INVTEXT("金币 {0}    每次投币 {1}"), Gold, Cost));
	RecruitText->SetText(FText::Format(INVTEXT("已招募 {0}/{1}"), Recruited, Total));
	AimText->SetText(FText::Format(INVTEXT("投币口 {0} cm    偏角 {1}°"),
		FMath::RoundToInt(Position), FMath::RoundToInt(Angle)));
	ControlsText->SetText(INVTEXT("A / D 移动    ← / → 调角    左键 / 空格 投币    E / Tab / Esc 退出"));
}

void UCastlePinballNPCStatusWidget::SetFeedback(const FText& Message)
{
	FeedbackText->SetText(Message);
}
