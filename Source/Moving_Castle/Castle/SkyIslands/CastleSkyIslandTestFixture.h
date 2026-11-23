#pragma once

#include "CastleSkyIslandBase.h"
#include "CastleSkyIslandTestFixture.generated.h"

/** 独立于离开门的夹具交互点，用于证明普通物件也走相同的准星入口。 */
UCLASS()
class UCastleSkyIslandFixtureInteractComponent : public UStaticMeshComponent, public ICastleSkyIslandInteractable
{
	GENERATED_BODY()

public:
	virtual FText GetInteractPrompt() const override;
	virtual bool CanInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const override;
	virtual bool ExecuteInteract(const FCastleSkyIslandInteractContext& Context, FText& OutFailureReason) override;
	int32 GetInteractionCount() const { return InteractionCount; }

private:
	int32 InteractionCount = 0;
};

/** 只供显式登记的自动化和非发行版验收命令使用，正式配置不引用。 */
UCLASS(NotBlueprintable)
class MOVING_CASTLE_API ACastleSkyIslandTestFixture : public ACastleSkyIslandBase
{
	GENERATED_BODY()

public:
	ACastleSkyIslandTestFixture();

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCastleSkyIslandFixtureInteractComponent> TestObject;
};
