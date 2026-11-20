#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CastleBuildingPCGRule.h"
#include "CastleBuildingPCGComponent.generated.h"

class ACastleBoard;
class UStaticMeshComponent;
class UCastleLadderLinkComponent;
namespace CastleV4PCG { struct FStairInterface; }

/** 从显式配置的变体 Actor 模板抓取的静态美术件；不把模板 Actor 留在关卡。 */
struct FCastlePCGMeshTemplate
{
	UStaticMesh* Mesh = nullptr;
	TArray<UMaterialInterface*> Materials;
	FTransform Relative;
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

/** 完整验证后的单栋变化计划；提交前不隐藏原件或修改功能几何。 */
struct FCastlePCGApplyPlan
{
	FString Key;
	/** 美术与功能分别签名。单纯改变功能面时，旧美术和正在播放的升级动画继续沿用。 */
	FString VisualKey;
	TSet<UPrimitiveComponent*> Hidden;
	TArray<FCastlePCGMeshTemplate> Visuals;
	/** 功能盒使用建筑局部变换与未缩放厘米半径。 */
	TArray<TPair<FTransform, FVector>> FunctionalBoxes;
	TArray<FCastlePCGLadderPlan> LadderFrames;
	/** 通梯交界切换上层原功能楼板，关闭的碰撞随邻接撤销而恢复。 */
	TSet<UPrimitiveComponent*> DisabledFloors;
};

/** 建筑语义快照及动态组件的唯一所有者，原始蓝图组件永远不被销毁或改网格。 */
UCLASS(ClassGroup=(Castle), meta=(BlueprintSpawnableComponent))
class MOVING_CASTLE_API UCastleBuildingPCGComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	/** 首次缓存所有原件及作者显隐；后续只读该快照，避免把动画显隐当成作者配置。 */
	bool CacheOriginalParts(const ACastleBoard* Board, FString& Error);
	/** 将纯规则输出变成完整美术/碰撞计划，缺少单格参考护栏等情况在这里失败。 */
	bool Prepare(const ACastleBoard* Board, const UCastlePCGRuleSetDataAsset* Rules, const FCastlePCGTarget& Target,
		const TArray<FCastlePCGMeshTemplate>& LV1, const TArray<FCastlePCGMeshTemplate>& LV2,
		const TMap<FIntVector, CastleV4PCG::FStairInterface>& Stairs,
		FCastlePCGApplyPlan& Plan, FString& Error) const;
	bool NeedsVisualChange(const FCastlePCGApplyPlan& Plan) const { return AppliedVisualKey != Plan.VisualKey; }
	/** 试装差量，保留原组件直到整块棋盘完成应用。调用者在同一次求值中统一提交或回滚。 */
	bool Apply(const ACastleBoard* Board, const FCastlePCGApplyPlan& Plan, AActor* Snapshot, FVector Origin, FString& Error);
	/** 完成棋盘事务。失败时恢复此前的显隐、碰撞、已登记的梯和稳定键。 */
	void FinishApply(bool bCommit);
	/** 只返回当前已激活的梯子，不返回等待动画的临时美术件。 */
	void GetLadders(TArray<FCastleActiveLadderConnection>& Out) const;
	const TArray<FCastlePCGPartAnnotation>& GetOriginalParts() const { return OriginalParts; }
	const FString& GetAppliedKey() const { return AppliedKey; }
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
	FString AppliedKey;
	FString AppliedVisualKey;
	/** 同一游戏线程内的试装状态。旧 Link 留在导航事务里，失败时可保留其原 ID。 */
	struct FBeforeApply
	{
		TArray<TObjectPtr<USceneComponent>> Components;
		TArray<TObjectPtr<USceneComponent>> Visuals;
		bool bVisualChange = false;
		TArray<FCastleActiveLadderConnection> Ladders;
		TMap<TWeakObjectPtr<UPrimitiveComponent>, ECollisionEnabled::Type> Suppressed;
		TMap<UPrimitiveComponent*, TPair<bool, ECollisionEnabled::Type>> ChangedPrimitives;
		FString Key;
		FString VisualKey;
	};
	TOptional<FBeforeApply> BeforeApply;
	bool bCached = false;
	void DestroyDynamicComponents();
};