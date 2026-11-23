#pragma once

#include "CastleSkyIslandTypes.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "CastleSkyIslandBase.generated.h"

class UCastlePendingRoomReward;
class UTextRenderComponent;
struct FCastleBadgeDefinition;

/** 基座的离开按钮只负责报告玩家意图，离岛、传送与关数提交由导演拥有。 */
UCLASS()
class MOVING_CASTLE_API UCastleSkyIslandDepartureComponent : public UStaticMeshComponent, public ICastleSkyIslandInteractable
{
	GENERATED_BODY()

public:
	virtual FText GetInteractPrompt() const override;
	virtual bool CanInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const override;
	virtual bool ExecuteInteract(const FCastleSkyIslandInteractContext& Context, FText& OutFailureReason) override;
};

/** 占位平台与共享交互契约，具体空岛玩法由配置显式登记的子类实现。 */
UCLASS(Abstract)
class MOVING_CASTLE_API ACastleSkyIslandBase : public AActor
{
	GENERATED_BODY()

public:
	ACastleSkyIslandBase();
	bool InitializeIsland(const FCastleSkyIslandContext& Context, FText& OutFailureReason);
	FTransform GetPlayerArrivalTransform() const;
	FSimpleMulticastDelegate OnDepartureRequested;
	bool RequestOpenTemporaryRoomShop(FText& OutFailureReason);
	bool GrantPendingRoomRewardUI(UCastlePendingRoomReward* Reward, FText& OutFailureReason);
	bool GrantBadgeWithReplaceUI(const FCastleBadgeDefinition& Badge, FText& OutFailureReason);

protected:
	/** 具体玩法在传送玩家前准备所需资产和交互物件，失败时由导演撤销生成。 */
	virtual bool OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutFailureReason) { return true; }
	const FCastleSkyIslandContext& GetIslandContext() const { return IslandContext; }

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Platform;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Title;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> ArrivalAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCastleSkyIslandDepartureComponent> Departure;
	FCastleSkyIslandContext IslandContext;
	bool bInitialized = false;
};
