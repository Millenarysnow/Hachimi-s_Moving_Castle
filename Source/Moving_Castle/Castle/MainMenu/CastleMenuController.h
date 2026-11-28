#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CastleMenuController.generated.h"

class SCastleStartMenu;
enum class ECastleControlMode : uint8;
enum class ECastleMenuAction : uint8 { Tutorial, Play, Quit };

/** 跨地图保留本次运行的首次选择状态，随 GameInstance 销毁。 */
UCLASS()
class UCastleMenuSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	bool bHasChosenControlModeThisSession = false;
};

/** 菜单地图单独指定此模式，使正式图和教程继续使用各自的 Pawn、HUD 与输入。 */
UCLASS()
class ACastleMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ACastleMenuGameMode();
};

/** 只管理本地视口与三个菜单动作；跨图时移除本世界持有的 Slate 层和旅行回调。 */
UCLASS()
class ACastleMenuController : public APlayerController
{
	GENERATED_BODY()
public:
	ACastleMenuController();
	FReply Choose(ECastleMenuAction Action);
	FReply ConfirmControlMode(ECastleControlMode Mode);
	FReply CancelControlMode();
	bool IsMenuReady() const;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	friend class FCastleStartMenuSessionTest;
#if WITH_DEV_AUTOMATION_TESTS
	/** 供 PIE 核对实际绘制的开始按钮尺寸和焦点。 */
	TSharedPtr<SWidget> GetPlayButtonForAutomation() const;
#endif
	/** 两个确认按钮与已确认玩家共用地图包校验和单次旅行门控。 */
	FReply TravelTo(FName Map, TOptional<ECastleControlMode> NewMode = {});
	void FocusMenuWidget(const TSharedPtr<SWidget>& Widget);
	void Report(const FString& Error);
	void TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Detail);
	TSharedPtr<SCastleStartMenu> Menu;
	FDelegateHandle FailureHandle;
	bool bLeaving = false;
	bool bModeSelectionOpen = false;
};
