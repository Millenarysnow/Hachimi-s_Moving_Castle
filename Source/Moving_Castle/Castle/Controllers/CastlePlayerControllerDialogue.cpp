#include "CastlePlayerController.h"

#include "Castle/Dialogue/CastleDialogueSubsystem.h"
#include "Castle/Guide/CastleGuideSubsystem.h"
#include "Engine/GameInstance.h"
#include "Moving_Castle.h"

void ACastlePlayerController::CloseDialogueAndGuideForWorldEnd()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		GameInstance->GetSubsystem<UCastleDialogueSubsystem>()->CancelForController(*this);
		GameInstance->GetSubsystem<UCastleGuideSubsystem>()->CancelForController(*this);
	}
	PresentationModals.Reset();
}

void ACastlePlayerController::CastleResetSkyIslandDialogues()
{
	UGameInstance* GameInstance = GetGameInstance();
	UCastleDialogueSubsystem* Dialogue = GameInstance ? GameInstance->GetSubsystem<UCastleDialogueSubsystem>() : nullptr;
	if (!Dialogue)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("CastleResetSkyIslandDialogues 失败：控制器 %s 无法取得对话子系统。"), *GetPathName());
		return;
	}
	Dialogue->ResetSkyIslandDialogueHistory();
}
