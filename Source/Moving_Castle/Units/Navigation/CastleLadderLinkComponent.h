#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "NavLinkCustomInterface.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "CastleLadderLinkComponent.generated.h"

/**
 * 建筑功能分支中的双向梯子导航连接。
 * 组件必须挂在 BuildingGameplayRoot 分支中，且仅在注册到所属世界的 NavigationSystem 后才有效。
 */
UCLASS(ClassGroup=(Navigation), meta=(BlueprintSpawnableComponent))
class MOVING_CASTLE_API UCastleLadderLinkComponent : public USceneComponent, public INavLinkCustomInterface, public INavRelevantInterface
{
	GENERATED_BODY()

public:
	UCastleLadderLinkComponent();

	/** 楼层接驳点相对组件的偏移；默认匹配 560 cm 层高的程序化双层夹具。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="梯子导航")
	FVector BottomStandOffset = FVector(0.0, -240.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="梯子导航")
	FVector LadderBottomOffset = FVector(-58.0, -150.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="梯子导航")
	FVector LadderTopOffset = FVector(-58.0, -150.0, 560.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="梯子导航")
	FVector TopStandOffset = FVector(0.0, -60.0, 560.0);

	/** 调用方可在组件注册前关闭 Link；注册后的动态移除应注销组件并触发导航刷新。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="梯子导航")
	bool bLadderLinkEnabled = true;

	/** 返回本次通过梯子的四个精确世界空间折线点，支持上行与下行。 */
	bool GetTraversalPolyline(bool bClimbingUp, TArray<FVector>& OutWorldPoints) const;

	/** 返回组件当前在本世界 NavigationSystem 登记的唯一标识。 */
	virtual FNavLinkId GetId() const override { return CustomLinkId; }
	virtual void UpdateLinkId(FNavLinkId NewUniqueId) override { CustomLinkId = NewUniqueId; }
	virtual void GetLinkData(FVector& LeftPt, FVector& RightPt, ENavLinkDirection::Type& Direction) const override;
	virtual void GetSupportedAgents(FNavAgentSelector& OutSupportedAgents) const override;
	virtual TSubclassOf<UNavArea> GetLinkAreaClass() const override;
	virtual UObject* GetLinkOwner() const override;

	/** 独立导航八叉树节点，仅导出本组件的双向连接，不导出美术碰撞。 */
	virtual bool IsNavigationRelevant() const override;
	virtual FBox GetNavigationBounds() const override;
	virtual void GetNavigationData(FNavigationRelevantData& Data) const override;

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	/** 组件先生成唯一 ID，再交给 NavigationSystem 登记；快照不保存这个可能随重注册改变的身份。 */
	UPROPERTY(Transient)
	FNavLinkId CustomLinkId;

	/** 记住实际登记的系统，确保世界切换或组件卸载时从原系统注销。 */
	TWeakObjectPtr<class UNavigationSystemV1> RegisteredNavigation;

	/** 验证所有几何有限且楼层高度递增；失败时不注册连接。 */
	bool ValidateGeometry(FText& OutFailure) const;
};
