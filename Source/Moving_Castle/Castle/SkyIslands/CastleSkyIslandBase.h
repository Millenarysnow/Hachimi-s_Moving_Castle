#pragma once

#include "CastleSkyIslandTypes.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "CastleSkyIslandBase.generated.h"

class UCastleBadgeDataAsset;
class UCastlePendingRoomReward;
class UTextRenderComponent;
struct FCastleGridCursorTarget;

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

/** 空岛基座与共享交互契约，具体空岛玩法由配置显式登记的子类实现。 */
UCLASS(Abstract)
class MOVING_CASTLE_API ACastleSkyIslandBase : public AActor
{
	GENERATED_BODY()

public:
	ACastleSkyIslandBase();
	bool InitializeIsland(const FCastleSkyIslandContext& Context, FText& OutFailureReason);
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	FTransform GetPlayerArrivalTransform() const;
	FSimpleMulticastDelegate OnDepartureRequested;
	bool RequestOpenTemporaryRoomShop(FText& OutFailureReason);
	bool GrantPendingRoomRewardUI(UCastlePendingRoomReward* Reward, FText& OutFailureReason);
	/** 导演验证岛上交互，并将完整徽章资产交给发放入口与替换界面。 */
	bool GrantBadgeWithReplaceUI(const UCastleBadgeDataAsset& Badge, FText& OutFailureReason);

	/** 只有提供建造区的空岛解析射线和摆放；其余空岛不处理这些输入。 */
	virtual bool TryResolveIslandGridCursorTarget(const FVector& Start, const FVector& End,
		FCastleGridCursorTarget& OutTarget) const { return false; }
	virtual bool CanPlaceCardAtIslandCursor(const FCastleGridCursorTarget& Target, int32 BackpackIndex) const { return false; }
	/** 下列接口返回是否提交成功，失败原因供控制器显示；不支持的空岛保持原因为空。 */
	virtual bool TryPlaceBackpackAtIslandCursor(const FCastleGridCursorTarget& Target, int32 BackpackIndex,
		FText& OutReason) { return false; }
	virtual bool TryRetractLastIslandPlacement(FText& OutReason) { return false; }

protected:
	/** 具体玩法在传送玩家前准备所需资产和交互物件，失败时由导演撤销生成。 */
	virtual bool OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutFailureReason) { return true; }
	const FCastleSkyIslandContext& GetIslandContext() const { return IslandContext; }
	/** 绑定由导演生成的正式岛体。岛面中心为局部原点，各部件均使用厘米。 */
	void SetIslandArtMesh(const TCHAR* AssetName);
	/** 构造期间解析同一目录的美术资产，记录加载失败的完整路径供入岛事务报告。 */
	UStaticMesh* LoadIslandArt(const TCHAR* AssetName);

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Platform;
	/** 整座岛屿的合并视觉网格不参与碰撞，交互射线仍由玩法组件独立接收。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> IslandArt;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Title;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> ArrivalAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCastleSkyIslandDepartureComponent> Departure;
	/** 保存预期资产路径，使初始化失败日志能指出缺失的具体美术网格。 */
	FString MissingIslandArtPath;
	FCastleSkyIslandContext IslandContext;
	bool bInitialized = false;
};
