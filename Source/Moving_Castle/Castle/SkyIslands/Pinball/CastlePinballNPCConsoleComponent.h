#pragma once

#include "Castle/SkyIslands/CastleSkyIslandBase.h"
#include "CastlePinballNPCConsoleComponent.generated.h"

class ACastlePinballNPCSkyIsland;

/** 由导演的准星射线直接命中；交互契约与基座离开传送门一致。 */
UCLASS()
class MOVING_CASTLE_API UCastlePinballNPCConsoleComponent : public UStaticMeshComponent, public ICastleSkyIslandInteractable
{
	GENERATED_BODY()

public:
	virtual FText GetInteractPrompt() const override;
	virtual bool CanInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const override;
	virtual bool ExecuteInteract(const FCastleSkyIslandInteractContext& Context, FText& OutFailureReason) override;
};
