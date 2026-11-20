#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CastlePCGTypes.h"
#include "CastleBuildingPCGRule.generated.h"

class UStaticMesh;
class ICastlePCGNoiseProvider;

/** 本轮纯求值上下文；只包含正式已完成建筑的邻接，不保存求值顺序状态。 */
struct MOVING_CASTLE_API FCastlePCGRuleContext
{
	ACastleBuildingBase* Building = nullptr;
	TArray<FCastlePCGCellAdjacency> Adjacencies;
	int32 Seed = 0;
	UObject* NoiseProvider = nullptr;
	bool bUseLadderNoise = false;
	FName LadderChannel;
};

/** 单个格面目标；同一格面每轮只能有一个连通状态。 */
struct MOVING_CASTLE_API FCastlePCGFaceTarget
{
	FIntVector Cell = FIntVector::ZeroValue;
	ECastlePCGFace Face = ECastlePCGFace::None;
	bool bConnected = false;
};

/** 声明式规则输出，不创建组件也不控制动画。1=封闭顶板，2=唯一梯子格。 */
struct MOVING_CASTLE_API FCastlePCGTarget
{
	TMap<FIntVector, uint8> Ceilings;
	TArray<FCastlePCGFaceTarget> Faces;
	FString StableKey() const;
};

/** 可在规则集内组合的原生语义规则基类，具体规则不反向进入棋盘代码。 */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced)
class MOVING_CASTLE_API UCastleBuildingPCGRule : public UObject
{
	GENERATED_BODY()
public:
	/** 完整求值后返回结果；无效噪声/规则配置必须返回失败。 */
	virtual bool Evaluate(const FCastlePCGRuleContext& Context, FCastlePCGTarget& Out, FString& Error) const PURE_VIRTUAL(UCastleBuildingPCGRule::Evaluate, return false;);
};

/** 垂直规则：移除屋顶，每一对可通行建筑只选择一格梯子。 */
UCLASS(BlueprintType, EditInlineNew)
class MOVING_CASTLE_API UCastlePCGVerticalRule : public UCastleBuildingPCGRule
{
	GENERATED_BODY()
public:
	virtual bool Evaluate(const FCastlePCGRuleContext& Context, FCastlePCGTarget& Out, FString& Error) const override;
};

/** 平面规则：输出全部跨建筑相邻格面，连通时开护栏并铺桥，否则保留阻隔。 */
UCLASS(BlueprintType, EditInlineNew)
class MOVING_CASTLE_API UCastlePCGPlanarRule : public UCastleBuildingPCGRule
{
	GENERATED_BODY()
public:
	virtual bool Evaluate(const FCastlePCGRuleContext& Context, FCastlePCGTarget& Out, FString& Error) const override;
};

/** 项目规则与美术配置。缺少任何必需资源都拒绝本轮变化，不生成占位资源。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastlePCGRuleSetDataAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	/** 双侧护栏各自使用的单格开口变体。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	TSubclassOf<AActor> VariantLV1ActorClass;
	/** 楼板和梯子的组合变体，必须同时含两块有效网格。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	TSubclassOf<AActor> VariantLV2ActorClass;
	/** 已导入普通房间的封闭顶平板。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	TObjectPtr<UStaticMesh> FlatFloorSlabMesh;
	/** 通行桥宽度，厘米；其长度由棋盘缝隙加两侧搭接确定。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	double BridgeWidth = 180.0;
	/** 功能桥与封闭功能面的厚度，顶面保持在真实楼层高度。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	double FloorThickness = 10.0;
	/** 不可通行邻接的隐藏阻挡墙高度，不能只依靠 15cm 间隙阻止 Recast 连通。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	double BarrierHeight = 200.0;
	/** 开启后以空间噪声择一，否则取 X/Y/Z 字典序首格。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	bool bUseNoiseForLadderCellSelection = false;
	/** 噪声语义通道须非空，避免不同扩展规则无意共用随机性。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG")
	FName LadderSelectionNoiseChannel = TEXT("LadderPlacement");
	/** 在资产中显式配置的规则实例，不在运行时暗中补规则。 */
	UPROPERTY(EditAnywhere, Instanced, Category="建筑PCG")
	TArray<TObjectPtr<UCastleBuildingPCGRule>> Rules;

	/** 普通房仍是独立一级 Actor。这六个源样例只参与局部部件求值。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG|V4")
	TMap<FName, TObjectPtr<class UCastleHachimisV4Layout>> RoomExamples;

	bool ValidateConfiguration(FString& Error) const;
};
