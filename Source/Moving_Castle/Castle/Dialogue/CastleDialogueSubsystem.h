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

	/** 首次短剧情仅在成功挂载后标记；Rest 类型不触发也不写标记。 */
	bool TryPlayFirstVisitSkyIslandDialogue(ECastleSkyIslandType Type, UCastleDialogueScriptDataAsset* Script,
		ACastlePlayerController* Controller, bool& bOutStarted, FText& OutReason);
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
