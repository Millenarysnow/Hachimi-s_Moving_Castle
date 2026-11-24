#pragma once

#include "CoreMinimal.h"
#include "CastleGameplayTypes.h"
#include "Engine/DataAsset.h"
#include "CastleTutorialConfigDataAsset.generated.h"

class UCastleCombatConfigDataAsset;
class UCastleDialogueScriptDataAsset;
class UCastleDirectorHUDWidget;
class UCastleTutorialTextWidget;
class UCastleTutorialRewardWidget;

/** 每行与一项真实玩法验成条件对应，文本与机制介绍由资产配置。 */
USTRUCT(BlueprintType)
struct FCastleTutorialStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "教程") int32 Number = 0;
	UPROPERTY(EditAnywhere, Category = "教程") TObjectPtr<UCastleDialogueScriptDataAsset> Dialogue;
	UPROPERTY(EditAnywhere, Category = "教程") TArray<FName> GuideEntries;
	UPROPERTY(EditAnywhere, Category = "教程") FText Objective;
};

/** TutorialMap 独享的固定配置，主塔和棋盘仍使用正式玩法代码。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleTutorialConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category = "教程") TArray<FCastleTutorialStep> Steps;
	UPROPERTY(EditAnywhere, Category = "教程") TArray<FCastleBuildingDescriptor> StarterRooms;
	UPROPERTY(EditAnywhere, Category = "教程") FCastleBuildingDescriptor Archer;
	UPROPERTY(EditAnywhere, Category = "教程") FCastleBuildingDescriptor Farm;
	UPROPERTY(EditAnywhere, Category = "教程") int32 StartingGold = 0;
	UPROPERTY(EditAnywhere, Category = "教程") TObjectPtr<UCastleCombatConfigDataAsset> GhostBattle;
	UPROPERTY(EditAnywhere, Category = "教程") TObjectPtr<UCastleDialogueScriptDataAsset> ClaimDialogue;
	UPROPERTY(EditAnywhere, Category = "教程") TObjectPtr<UCastleDialogueScriptDataAsset> OutOfStockDialogue;
	UPROPERTY(EditAnywhere, Category = "教程") TObjectPtr<UCastleDialogueScriptDataAsset> FarmOfferDialogue;
	UPROPERTY(EditAnywhere, Category = "教程") TObjectPtr<UCastleDialogueScriptDataAsset> ClosingDialogue;
	UPROPERTY(EditAnywhere, Category = "教程") TSubclassOf<UCastleDirectorHUDWidget> HUDClass;
	UPROPERTY(EditAnywhere, Category = "教程") TSubclassOf<UCastleTutorialTextWidget> ObjectiveClass;
	UPROPERTY(EditAnywhere, Category = "教程") TSubclassOf<UCastleTutorialTextWidget> BubbleClass;
	UPROPERTY(EditAnywhere, Category = "教程") TSubclassOf<UCastleTutorialRewardWidget> RewardClass;

	/** 错误携带字段名，拒绝启动半套教程。 */
	bool ValidateTutorial(FText& OutReason) const;
};
