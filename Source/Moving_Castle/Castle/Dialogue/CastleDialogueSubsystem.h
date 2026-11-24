#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Castle/SkyIslands/CastleSkyIslandTypes.h"
#include "CastleDialogueSubsystem.generated.h"

class ACastlePlayerController;
class UCastleDialogueScriptDataAsset;
class UCastleDialogueWidget;

DECLARE_MULTICAST_DELEGATE(FCastleDialogueFinished);

/** 管理当前唯一对话、剧本行跳转和同一游戏实例内的空岛首次到达记录。 */
UCLASS()
class MOVING_CASTLE_API UCastleDialogueSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	bool PlayScript(UCastleDialogueScriptDataAsset* Script, ACastlePlayerController* Controller, FText& OutReason);
	void AdvanceCurrentLine();
	void SelectChoice(FName TargetLineId);
	bool IsDialoguePlaying() const { return CurrentScript != nullptr; }
	const UCastleDialogueScriptDataAsset* GetCurrentScript() const { return CurrentScript; }
	FText GetResolvedCurrentLineText() const;
	/** 台词和非模态教程目标共用控制器当前的语义键显示名。 */
	static FText ResolveInputText(const FText& Template, const ACastlePlayerController& Controller);
	FCastleDialogueFinished OnDialogueFinished;

	/** 提前完成全部可失败的剧本及 WBP 准备，提交前界面保持折叠且不占用输入。 */
	UCastleDialogueWidget* PrepareScript(UCastleDialogueScriptDataAsset* Script, ACastlePlayerController* Controller, FText& OutReason);
	void StartPreparedScript(UCastleDialogueScriptDataAsset& Script, ACastlePlayerController& Controller, UCastleDialogueWidget& Widget);
	/** 只有成功传送及提交已准备的界面后才写首访记录。 */
	void StartPreparedSkyIslandDialogue(ECastleSkyIslandType Type, UCastleDialogueScriptDataAsset& Script,
		ACastlePlayerController& Controller, UCastleDialogueWidget& Widget);
	/** 地图退出时撤下界面，但不触发推进教学的完成事件。 */
	void CancelForController(const ACastlePlayerController& Controller);
	void ResetSkyIslandDialogueHistory();
	bool HasPlayedFirstVisitSkyIslandDialogue(ECastleSkyIslandType Type) const;

#if WITH_DEV_AUTOMATION_TESTS
	/** 自动化通过真实行推进入口关闭无选项短剧，不绕过剧本解析和 Widget 状态机。 */
	void AdvanceDialogueForAutomation();
#endif

private:
	void ShowCurrentLine();
	int32 FindLineIndex(FName LineId) const;
	void FinishDialogue(bool bCompleted = true);

	UPROPERTY(Transient) TObjectPtr<UCastleDialogueScriptDataAsset> CurrentScript;
	UPROPERTY(Transient) TObjectPtr<UCastleDialogueWidget> CurrentWidget;
	TWeakObjectPtr<ACastlePlayerController> CurrentController;
	int32 CurrentLineIndex = INDEX_NONE;
	TSet<ECastleSkyIslandType> PlayedSkyIslandDialogues;
};
