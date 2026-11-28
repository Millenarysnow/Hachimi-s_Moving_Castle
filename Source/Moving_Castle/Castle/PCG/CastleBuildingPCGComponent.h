#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CastlePCGTypes.h"
#include "CastleBuildingPCGComponent.generated.h"

class ACastleBoard;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class FCastleMenuModelBuilder;

/** 从显式配置的变体 Actor 模板抓取的静态美术件；不把模板 Actor 留在关卡。 */
struct FCastlePCGMeshTemplate
{
	UStaticMesh* Mesh = nullptr;
	TArray<UMaterialInterface*> Materials;
	FTransform Relative;
	/** V4 套件作者节点与语义组供选择、诊断和归属验证使用。 */
	FName Node;
	FName Group;
	/** V4 动态件继续提供选中射线，功能碰撞仍由 GameplayRoot 管理。 */
	bool bSelectable = false;
};

/** 一次梯子登记的四个动作点。两端站立面与梯身偏移分别保留。 */
struct FCastlePCGLadderPlan
{
	FIntVector LowerCell;
	FTransform Relative;
	FVector BottomStand;
	FVector TopStand;
	FVector LadderBottom;
	FVector LadderTop;
};

/** 功能盒使用建筑局部变换与未缩放厘米半径；墙盒和走板的用途由生成方明确声明。 */
struct FCastlePCGFunctionalBox
{
	FTransform Relative;
	FVector Extent;
	/** 隐藏墙盒提供通行边界，其表面不能作为玩家瞄准的楼板。 */
	bool bIsNavigationBarrier;
};

/** 功能根节点的独立计划。原板的碰撞缓存在组件所有者上。 */
struct FCastlePCGGameplayPlan
{
	FString Key;
	TArray<FCastlePCGFunctionalBox> FunctionalBoxes;
	TArray<FCastlePCGLadderPlan> LadderFrames;
	TSet<UPrimitiveComponent*> DisabledFloors;
};

/** 仅管理表现与 Visibility 选中资格，独立于玩法碰撞及导航登记。 */
struct FCastlePCGVisualPlan
{
	FString Key;
	TSet<UPrimitiveComponent*> Hidden;
	TArray<FCastlePCGMeshTemplate> Visuals;
};

/** 单栋两份完整计划均验证完成后才允许开始棋盘提交。 */
struct FCastlePCGApplyPlan
{
	FCastlePCGGameplayPlan Gameplay;
	FCastlePCGVisualPlan Visual;
	void FinalizeKeys();
};

/** 建筑语义快照及动态组件的唯一所有者，原始蓝图组件永远不被销毁或改网格。 */
UCLASS(ClassGroup=(Castle), meta=(BlueprintSpawnableComponent))
class MOVING_CASTLE_API UCastleBuildingPCGComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	/** 识别由 PCG 生成的通行墙盒，避免用组件名或碰撞形状推测功能。 */
	static bool IsNavigationBarrier(const UPrimitiveComponent* Component);
	/** 首次缓存所有原件及作者显隐；后续只读该快照，避免把动画显隐当成作者配置。 */
	bool CacheOriginalParts(const ACastleBoard* Board, FString& Error);
	/** 仅在玩法事务期间调用，失败后调用者对本栋及已试装的其他栋逆序回滚。 */
	bool ApplyGameplay(const FCastlePCGGameplayPlan& Plan, FString& Error);
	void FinishGameplay(bool bCommit);
	/** 视觉过渡失败时记录错误，最终组件与视觉键保持为本轮计划。 */
	/** 同一栋建筑的玩法也发生变化时，保留合并动画原有的导航观察。 */
	void ApplyVisual(const ACastleBoard& Board, const FCastlePCGVisualPlan& Plan, FVector Origin, bool bGameplayChanged);
	/** 成功完成整轮后维护兼容的聚合键；单通道 Diff 只比较各自的键。 */
	void UpdateAppliedKey();
	/** 只返回当前已激活的梯子，不返回等待动画的临时美术件。 */
	void GetLadders(TArray<FCastleActiveLadderConnection>& Out) const;
	/** 只采集语义系统已提交的战斗美术目标件，合并和飞入副本均不在这两份集合中。 */
	bool CollectMenuParts(FCastleMenuModelBuilder& Builder, const FTransform& TowerFrame, FString& Error) const;
	const TArray<FCastlePCGPartAnnotation>& GetOriginalParts() const { return OriginalParts; }
	const FString& GetAppliedKey() const { return AppliedKey; }
	const FString& GetAppliedGameplayKey() const { return AppliedGameplayKey; }
	const FString& GetAppliedVisualKey() const { return AppliedVisualKey; }
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	/** 原始组件强引用保证反复变化后仍能精确恢复。 */
	UPROPERTY(Transient)
	TArray<FCastlePCGPartAnnotation> OriginalParts;
	/** 动态美术和功能盒独立交接。只改碰撞时保持现有美术组件身份。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> DynamicVisuals;
	/** 功能盒及 NavLink 由本组件统一注销。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> DynamicComponents;
	/** 查询契约的稳定副本，不依赖后续动画临时组件。 */
	UPROPERTY(Transient)
	TArray<FCastleActiveLadderConnection> ActiveLadders;
	/** 缓存任何被接口暂时关闭的原碰撞，不改变蓝图作者默认配置。 */
	TMap<TWeakObjectPtr<UPrimitiveComponent>, ECollisionEnabled::Type> SuppressedCollision;
	/** V4 原件在表现通道独立恢复 Visibility 拾取资格。 */
	TMap<TWeakObjectPtr<UPrimitiveComponent>, ECollisionEnabled::Type> OriginalVisualCollision;
	FString AppliedKey;
	FString AppliedGameplayKey;
	FString AppliedVisualKey;
	/** 同一游戏线程内的试装状态。旧 Link 留在导航事务里，失败时可保留其原 ID。 */
	struct FBeforeGameplay
	{
		TArray<TObjectPtr<USceneComponent>> Components;
		TArray<FCastleActiveLadderConnection> Ladders;
		TMap<TWeakObjectPtr<UPrimitiveComponent>, ECollisionEnabled::Type> Suppressed;
		TMap<UPrimitiveComponent*, ECollisionEnabled::Type> ChangedPrimitives;
		FString PendingKey;
	};
	TOptional<FBeforeGameplay> BeforeGameplay;
	bool bCached = false;
	void DestroyDynamicComponents();
};
