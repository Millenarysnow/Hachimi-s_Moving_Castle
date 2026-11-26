#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastlePCGTypes.h"
#include "CastleBuildingPCGComponent.h"
#include "CastleBuildingPCGRule.h"
#include "CastleBuildingPCGSubsystem.generated.h"

class ACastleBoard;
DECLARE_MULTICAST_DELEGATE_OneParam(FCastlePCGLayoutUpdated, const ACastleBoard*);
/** 每次求解成功都公布最终落成集合，包括拆空及只移除独立建筑、余件签名未变的情况。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCastlePCGLayoutResolved, const ACastleBoard&, const TArray<ACastleBuildingBase*>&);

/** 事件驱动的棋盘三维语义场与事务协调者；稳定后不 Tick、不轮询建筑。 */
UCLASS()
class MOVING_CASTLE_API UCastleBuildingPCGSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	/** 棋盘公开布局提交协议，子系统只绑定事件，不把规则写入棋盘。 */
	void ObserveBoard(ACastleBoard* Board);
	/** 可直接用于测试或加载存档后的幂等重算；失败通过 Error 和日志公开。 */
	bool Recompute(ACastleBoard* Board, FVector Origin, FString& Error);
	/** 返回指定棋盘所有已激活的 LV2 梯子，不包含其他棋盘或未完成建筑。 */
	void GetActiveLadderConnections(const ACastleBoard* Board, TArray<FCastleActiveLadderConnection>& OutLadders) const;
	FCastlePCGLayoutUpdated OnPCGLayoutUpdated;
	FCastlePCGLayoutResolved OnLayoutResolved;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
private:
	/** 防止同一棋盘重复绑定；弱引用不阻止关卡释放。 */
	TSet<TWeakObjectPtr<ACastleBoard>> ObservedBoards;
	bool RecomputeInternal(ACastleBoard* Board, FVector Origin, FString& Error);
	/** 自动事件和公开入口共用整盘求解，签名不变时保留原组件。 */
	void HandleLayoutCommitted(ACastleBoard* Board, FVector Origin);
};
