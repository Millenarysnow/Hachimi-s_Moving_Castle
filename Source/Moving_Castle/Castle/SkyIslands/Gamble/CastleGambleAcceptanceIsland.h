#pragma once

#include "CastleGambleSkyIsland.h"
#include "CastleGambleAcceptanceIsland.generated.h"

class UCastleWildcardRoomPickerWidget;
struct FKey;

/** 开发验收命令的独立载体，复用真实赌博玩法且不会写入导演配置或推进关数。 */
UCLASS(NotBlueprintable, Transient)
class ACastleGambleAcceptanceIsland : public ACastleGambleSkyIsland
{
	GENERATED_BODY()

public:
	bool Enter(const FCastleSkyIslandContext& Context, const FKey& InteractKey, FText& OutReason);
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

protected:
	virtual bool PresentRoomPrize(UCastlePendingRoomReward* Reward, FText& OutReason) override;
	virtual bool PresentBadgePrize(const FCastleBadgeDefinition& Badge, FText& OutReason) override;

private:
	friend class FCastleGambleAcceptanceScenario;
	void Interact();
	void Depart();
	void ClosePicker();
	UFUNCTION() void OnPickerClosed(bool bClaimed);

	UPROPERTY(Transient) TObjectPtr<UInputComponent> FixtureInput;
	UPROPERTY(Transient) TObjectPtr<UCastleWildcardRoomPickerWidget> Picker;
	FTransform ReturnTransform;
	FRotator ReturnView;
	bool bDepartureQueued = false;
};
