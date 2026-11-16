#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CastleBuildingPCGRule.h"
#include "CastleBuildingPCGComponent.generated.h"

class ACastleBoard;
class UStaticMeshComponent;
class UCastleLadderLinkComponent;

/** 从显式配置的变体 Actor 模板抓取的静态美术件；不把模板 Actor 留在关卡。 */
struct FCastlePCGMeshTemplate
{
	UStaticMesh* Mesh = nullptr;
	TArray<UMaterialInterface*> Materials;
	FTransform Relative;
};

/** 完整验证后的单栋变化计划；提交前不隐藏原件或修改功能几何。 */
struct FCastlePCGApplyPlan
{
	FString Key;
	TSet<UPrimitiveComponent*> Hidden;
	TArray<FCastlePCGMeshTemplate> Visuals;
	/** 功能盒使用建筑局部变换与未缩放厘米半径。 */
	TArray<TPair<FTransform, FVector>> FunctionalBoxes;
	TArray<TPair<FIntVector, FTransform>> LadderFrames;
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
		FCastlePCGApplyPlan& Plan, FString& Error) const;
	/** 差量提交，旧件来源已由调用者预先抓取。StartChange 后无论成功失败都销毁快照载体。 */
	bool Apply(const ACastleBoard* Board, const FCastlePCGApplyPlan& Plan, AActor* Snapshot, FVector Origin, FString& Error);
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
	/** 动态美术、功能盒及 NavLink 一并归属本组件，删除建筑时按生命周期注销。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> DynamicComponents;
	/** 查询契约的稳定副本，不依赖后续动画临时组件。 */
	UPROPERTY(Transient)
	TArray<FCastleActiveLadderConnection> ActiveLadders;
	FString AppliedKey;
	bool bCached = false;
	void DestroyDynamicComponents();
};