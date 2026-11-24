#include "CastleDialogueSubsystem.h"

#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Dialogue/CastleDialogueScriptDataAsset.h"
#include "Castle/Dialogue/CastleDialogueWidget.h"
#include "Engine/World.h"
#include "Moving_Castle.h"
#include "UObject/SoftObjectPath.h"

namespace
{
	constexpr TCHAR DialogueWidgetClassPath[] = TEXT("/Game/MovingCastle/UI/Dialogue/WBP_CastleDialogue.WBP_CastleDialogue_C");
}

bool UCastleDialogueSubsystem::PlayScript(UCastleDialogueScriptDataAsset* Script,
	ACastlePlayerController* Controller, FText& OutReason)
{
	auto* Widget = PrepareScript(Script, Controller, OutReason);
	if (!Widget) { return false; }
	StartPreparedScript(*Script, *Controller, *Widget);
	return true;
}

UCastleDialogueWidget* UCastleDialogueSubsystem::PrepareScript(UCastleDialogueScriptDataAsset* Script,
	ACastlePlayerController* Controller, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (IsValid(CurrentScript))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("对话子系统 %s 正在播放剧本 %s，不能并发启动 %s。"),
			*GetPathName(), *CurrentScript->GetPathName(), *GetPathNameSafe(Script)));
	}
	else if (!IsValid(Script))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("对话子系统 %s 收到无效剧本资产。"), *GetPathName()));
	}
	else if (!IsValid(Controller) || Controller->GetWorld() != GetWorld())
	{
		OutReason = FText::FromString(FString::Printf(TEXT("对话剧本 %s 的玩家控制器无效或不属于当前世界。"), *Script->GetPathName()));
	}
	else if (!Script->ValidateScript(OutReason))
	{
		// DataAsset 已经写出精确资产名和字段，保持原失败原因继续向调用者传递。
	}
	if (!OutReason.IsEmpty())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return nullptr;
	}

	UClass* WidgetClass = LoadClass<UCastleDialogueWidget>(nullptr, DialogueWidgetClassPath);
	if (!WidgetClass)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("剧本 %s 无法加载必需界面 %s。"),
			*Script->GetPathName(), DialogueWidgetClassPath));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return nullptr;
	}
	UCastleDialogueWidget* Widget = CreateWidget<UCastleDialogueWidget>(Controller, WidgetClass);
	if (Widget) { Widget->SetVisibility(ESlateVisibility::Collapsed); }
	if (!IsValid(Widget) || !Widget->ConfigureDialogue(this, Controller, OutReason) || !Widget->AddToPlayerScreen(70))
	{
		if (OutReason.IsEmpty())
		{
			OutReason = FText::FromString(FString::Printf(TEXT("剧本 %s 的界面 %s 创建或挂载失败。"),
				*Script->GetPathName(), DialogueWidgetClassPath));
		}
		if (IsValid(Widget)) { Widget->RemoveFromParent(); }
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return nullptr;
	}
	return Widget;
}

void UCastleDialogueSubsystem::StartPreparedScript(UCastleDialogueScriptDataAsset& Script,
	ACastlePlayerController& Controller, UCastleDialogueWidget& Widget)
{
	check(!CurrentScript);
	CurrentScript = &Script;
	CurrentWidget = &Widget;
	CurrentController = &Controller;
	CurrentLineIndex = 0;
	ShowCurrentLine();
	Widget.SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Controller.SetPresentationModalState(true, &Widget);
	Widget.SetKeyboardFocus();
	OnDialogueActivityChanged.Broadcast();
	UE_LOG(LogMoving_Castle, Display, TEXT("开始播放对话剧本 %s，共 %d 行。"), *Script.GetPathName(), Script.Lines.Num());
}

void UCastleDialogueSubsystem::AdvanceCurrentLine()
{
	if (!IsValid(CurrentScript) || !IsValid(CurrentWidget) || !CurrentScript->Lines.IsValidIndex(CurrentLineIndex))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("对话推进失败：子系统 %s 的剧本、界面或行索引无效。"), *GetPathName());
		return;
	}
	if (!CurrentWidget->IsCurrentLineFullyRevealed())
	{
		CurrentWidget->CompleteCurrentLine();
		return;
	}
	const FCastleDialogueLine& Line = CurrentScript->Lines[CurrentLineIndex];
	if (!Line.Choices.IsEmpty()) { return; }
	if (Line.bEndsScript)
	{
		FinishDialogue();
		return;
	}
	if (!CurrentScript->Lines.IsValidIndex(CurrentLineIndex + 1))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("剧本 %s 行 %s 没有下一行且未标记结束。"),
			*CurrentScript->GetPathName(), *Line.LineId.ToString());
		return;
	}
	++CurrentLineIndex;
	ShowCurrentLine();
}

void UCastleDialogueSubsystem::SelectChoice(const FName TargetLineId)
{
	if (!IsValid(CurrentScript) || !IsValid(CurrentWidget) || !CurrentScript->Lines.IsValidIndex(CurrentLineIndex))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("对话选择失败：子系统 %s 没有有效的当前剧本行。"), *GetPathName());
		return;
	}
	const FCastleDialogueLine& Line = CurrentScript->Lines[CurrentLineIndex];
	if (!CurrentWidget->IsCurrentLineFullyRevealed()) { return; }
	const FCastleDialogueChoice* Choice = Line.Choices.FindByPredicate(
		[TargetLineId](const FCastleDialogueChoice& Candidate) { return Candidate.TargetLineId == TargetLineId; });
	if (!Choice)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("剧本 %s 行 %s 不包含目标行 %s 的分支选项。"),
			*CurrentScript->GetPathName(), *Line.LineId.ToString(), *TargetLineId.ToString());
		return;
	}
	CurrentLineIndex = FindLineIndex(TargetLineId);
	if (CurrentLineIndex == INDEX_NONE)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("剧本 %s 的分支目标行 %s 在播放期间失效。"),
			*CurrentScript->GetPathName(), *TargetLineId.ToString());
		return;
	}
	ShowCurrentLine();
}

FText UCastleDialogueSubsystem::GetResolvedCurrentLineText() const
{
	const auto* Controller = CurrentController.Get();
	if (!Controller || !IsValid(CurrentScript) || !CurrentScript->Lines.IsValidIndex(CurrentLineIndex)) { return FText::GetEmpty(); }
	return ResolveInputText(CurrentScript->Lines[CurrentLineIndex].TextTemplate, *Controller);
}

FText UCastleDialogueSubsystem::ResolveInputText(const FText& Template, const ACastlePlayerController& Controller)
{
	FString Resolved = Template.ToString();
	Resolved.ReplaceInline(TEXT("{PlaceKey}"), *Controller.GetPrimaryPlaceInteractKeyDisplayText().ToString());
	Resolved.ReplaceInline(TEXT("{DemolishKey}"), *Controller.GetDemolishKeyDisplayText().ToString());
	return FText::FromString(Resolved);
}

void UCastleDialogueSubsystem::StartPreparedSkyIslandDialogue(const ECastleSkyIslandType Type,
	UCastleDialogueScriptDataAsset& Script, ACastlePlayerController& Controller, UCastleDialogueWidget& Widget)
{
	StartPreparedScript(Script, Controller, Widget);
	PlayedSkyIslandDialogues.Add(Type);
}

void UCastleDialogueSubsystem::ResetSkyIslandDialogueHistory()
{
	PlayedSkyIslandDialogues.Reset();
	UE_LOG(LogMoving_Castle, Display, TEXT("已清除当前游戏实例的三类奖励空岛首次登岛对话记录。"));
}

bool UCastleDialogueSubsystem::HasPlayedFirstVisitSkyIslandDialogue(const ECastleSkyIslandType Type) const
{
	return PlayedSkyIslandDialogues.Contains(Type);
}

#if WITH_DEV_AUTOMATION_TESTS
void UCastleDialogueSubsystem::AdvanceDialogueForAutomation()
{
	if (!IsValid(CurrentWidget)) { return; }
	CurrentWidget->CompleteCurrentLine();
	AdvanceCurrentLine();
}
#endif

void UCastleDialogueSubsystem::ShowCurrentLine()
{
	if (!IsValid(CurrentScript) || !IsValid(CurrentWidget) || !CurrentScript->Lines.IsValidIndex(CurrentLineIndex))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("对话子系统 %s 无法展示无效的当前行 %d。"), *GetPathName(), CurrentLineIndex);
		return;
	}
	CurrentWidget->DisplayLine(CurrentScript->Lines[CurrentLineIndex], GetResolvedCurrentLineText(), CurrentScript->CharactersPerSecond);
}

int32 UCastleDialogueSubsystem::FindLineIndex(const FName LineId) const
{
	return IsValid(CurrentScript) ? CurrentScript->FindLineIndex(LineId) : INDEX_NONE;
}

void UCastleDialogueSubsystem::CancelForController(const ACastlePlayerController& Controller)
{
	if (CurrentController.Get() == &Controller) { FinishDialogue(false); }
}

void UCastleDialogueSubsystem::FinishDialogue(const bool bCompleted)
{
	if (ACastlePlayerController* Controller = CurrentController.Get())
	{
		Controller->SetPresentationModalState(false, CurrentWidget);
	}
	if (IsValid(CurrentWidget)) { CurrentWidget->RemoveFromParent(); }
	CurrentWidget = nullptr;
	CurrentScript = nullptr;
	CurrentController.Reset();
	CurrentLineIndex = INDEX_NONE;
	OnDialogueActivityChanged.Broadcast();
	if (bCompleted) { OnDialogueFinished.Broadcast(); }
}
