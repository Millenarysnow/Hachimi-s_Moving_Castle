#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CastlePCGTypes.h"
#include "CastlePCGSemanticField.h"
#include "CastleBuildingPCGRule.generated.h"

class UStaticMesh;
class ICastlePCGNoiseProvider;

/** 阶段按枚举顺序执行，同一阶段保留规则集里的显式顺序。 */
enum class ECastlePCGRuleStage : uint8 { Topology, Structure, Detail };

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
	/** 所有新规则整盘求值。失败时调用方拒绝发布语义场。 */
	virtual ECastlePCGRuleStage GetStage() const PURE_VIRTUAL(UCastleBuildingPCGRule::GetStage, return ECastlePCGRuleStage::Topology;);
	virtual bool Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const
		PURE_VIRTUAL(UCastleBuildingPCGRule::Evaluate, return false;);
};

/** 垂直规则：移除屋顶，每一对可通行建筑只选择一格梯子。 */
UCLASS(BlueprintType, EditInlineNew)
class MOVING_CASTLE_API UCastlePCGVerticalRule : public UCastleBuildingPCGRule
{
	GENERATED_BODY()
public:
	virtual ECastlePCGRuleStage GetStage() const override { return ECastlePCGRuleStage::Topology; }
	virtual bool Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const override;
	/** 分期保留旧规则测试入口，运行时使用整盘接口。 */
	bool Evaluate(const FCastlePCGRuleContext& Context, FCastlePCGTarget& Out, FString& Error) const;
};

/** 平面规则：输出全部跨建筑相邻格面，连通时开护栏并铺桥，否则保留阻隔。 */
UCLASS(BlueprintType, EditInlineNew)
class MOVING_CASTLE_API UCastlePCGPlanarRule : public UCastleBuildingPCGRule
{
	GENERATED_BODY()
public:
	virtual ECastlePCGRuleStage GetStage() const override { return ECastlePCGRuleStage::Topology; }
	virtual bool Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const override;
};

/** 普通房作者样例归属和通梯方向由结构阶段决定。 */
UCLASS(BlueprintType, EditInlineNew)
class MOVING_CASTLE_API UCastlePCGRoomFormRule : public UCastleBuildingPCGRule
{
	GENERATED_BODY()
public:
	virtual ECastlePCGRuleStage GetStage() const override { return ECastlePCGRuleStage::Structure; }
	virtual bool Evaluate(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Field, FString& Error) const override;
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

	/** 露天底盘仅在与异色房或功能塔实际接壤时使用一级墙套件。 */
	UPROPERTY(EditAnywhere, Category="建筑PCG|V4")
	TObjectPtr<class UCastleV4WallKit> RoomL1Bay;

	bool ValidateConfiguration(FString& Error) const;
	/** 验证、求值并发布完整语义场；禁止半轮结果进入装配。 */
	bool Solve(const FCastlePCGBoardQuery& Query, FCastlePCGSemanticField& Out, FString& Error) const;
};
