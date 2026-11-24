#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Castle/Combat/CastleCombatTypes.h"
#include "CastleGameplayTypes.h"
#include "CastleTutorialDirector.generated.h"

class ACastleBoard;
class ACastlePatrolRoute;
class ACastlePlayerController;
class UCastleCombatSubsystem;
class UCastleDialogueScriptDataAsset;
class UCastleDialogueSubsystem;
class UCastleDirectorHUDWidget;
class UCastleGuideSubsystem;
class UCastleNavigationSubsystem;
class UCastleShopComponent;
class UCastleTowerDispatchSubsystem;
class UCastleTutorialConfigDataAsset;
class UCastleTutorialRewardWidget;
class UCastleTutorialTextWidget;
class UWidgetComponent;
struct FCastleBuildingDescriptor;

/** 教学独立编排器，只读取真实棋盘、商店、路线、派兵和清场状态。 */
UCLASS()
class MOVING_CASTLE_API ACastleTutorialDirector : public AActor
{
	GENERATED_BODY()
public:
	ACastleTutorialDirector();
	virtual void Tick(float DeltaSeconds) override;
	void ChooseCopyReward();
	void ClaimCopyReward();
	int32 GetCurrentStep() const { return Step; }
	bool HasCompletedTutorial() const { return bCompleted; }
	bool HasShownDispatchBubble() const { return bDispatched; }

	UPROPERTY(EditInstanceOnly, Category = "教程") TObjectPtr<UCastleTutorialConfigDataAsset> Configuration;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	/** S13 中缺货与刷新各有一段剧本，状态在成功播放后才推进。 */
	enum class EShopLesson : uint8 { AwaitingOpen, AwaitingRefresh, AwaitingFarm };
	bool InitializeTutorial(FText& OutReason);
	bool EnterStep(int32 Number, FText& OutReason);
	bool ShowDialogue(UCastleDialogueScriptDataAsset* Script, FText& OutReason);
	bool UpdatePresentation(FText& OutReason);
	bool HasDoorCircuit() const;
	bool HasRouteWithArcher() const;
	bool IsBuildingOnPatrolPath(ECastleBuildingType Type) const;
	bool HasPurchased(const FCastleBuildingDescriptor& Descriptor) const;
	bool SpawnPathMarkers(FText& OutReason);
	void RemovePathMarkers();
	void Fail(const FText& Reason);
	void EndTutorialDispatch();
	void HandleDemolished(const FCastleBuildingDescriptor& Descriptor);
	void HandleDispatched(const FCastleMassUnitHandle& Unit);
	void HandleBattleCleared(ECastleBattleKind Kind, int32 Ordinal);
	void HandleBattleFailed(ECastleBattleKind Kind, int32 Ordinal, const FText& Reason);

	UPROPERTY(Transient) TObjectPtr<ACastleBoard> Board;
	UPROPERTY(Transient) TObjectPtr<ACastlePatrolRoute> Route;
	UPROPERTY(Transient) TObjectPtr<ACastlePlayerController> Controller;
	UPROPERTY(Transient) TObjectPtr<UCastleDialogueSubsystem> Dialogue;
	UPROPERTY(Transient) TObjectPtr<UCastleGuideSubsystem> Guide;
	UPROPERTY(Transient) TObjectPtr<UCastleCombatSubsystem> Combat;
	UPROPERTY(Transient) TObjectPtr<UCastleNavigationSubsystem> Navigation;
	UPROPERTY(Transient) TObjectPtr<UCastleTowerDispatchSubsystem> Dispatch;
	UPROPERTY(Transient) TObjectPtr<UCastleShopComponent> Shop;
	UPROPERTY(Transient) TObjectPtr<UCastleDirectorHUDWidget> HUD;
	UPROPERTY(Transient) TObjectPtr<UCastleTutorialTextWidget> Objective;
	UPROPERTY(Transient) TObjectPtr<UCastleTutorialRewardWidget> Reward;
	UPROPERTY(Transient) TObjectPtr<UWidgetComponent> Bubble;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidgetComponent>> PathMarkers;
	FCastleMassUnitHandle DispatchedUnit;
	int32 Step = 0;
	int32 NextGuide = 0;
	FName ActiveGuide;
	EShopLesson ShopLesson = EShopLesson::AwaitingOpen;
	bool bRecoveredRoom = false;
	bool bDispatched = false;
	bool bBattleCleared = false;
	bool bCopyChosen = false;
	bool bCopyClaimed = false;
	bool bClosing = false;
	bool bCompleted = false;
};
