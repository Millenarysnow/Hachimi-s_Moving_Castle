#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "CastleMenuController.generated.h"

class SCastleStartMenu;
enum class ECastleMenuAction : uint8 { Tutorial, Play, Quit };

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
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void Report(const FString& Error);
	void TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Detail);
	TSharedPtr<SCastleStartMenu> Menu;
	FDelegateHandle FailureHandle;
	bool bLeaving = false;
};
