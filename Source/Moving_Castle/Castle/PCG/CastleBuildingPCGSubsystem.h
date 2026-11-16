#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastlePCGTypes.h"
#include "CastleBuildingPCGComponent.h"
#include "CastleBuildingPCGSubsystem.generated.h"

class ACastleBoard;
DECLARE_MULTICAST_DELEGATE_OneParam(FCastlePCGLayoutUpdated, const ACastleBoard*);

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
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
private:
	/** 防止同一棋盘重复绑定；弱引用不阻止关卡释放。 */
	TSet<TWeakObjectPtr<ACastleBoard>> ObservedBoards;
	/** 上一轮成功提交的正式占用；删除格也能据此找到当前仍存活的六邻建筑。 */
	TMap<TWeakObjectPtr<ACastleBoard>, TMap<FIntVector, TWeakObjectPtr<ACastleBuildingBase>>> PreviousOccupancy;
	bool RecomputeInternal(ACastleBoard* Board, FVector Origin, FString& Error, bool bAffectedOnly);
	/** 自动事件仅更新受影响建筑，公开入口可在规则热编辑/读档后强制完整重算。 */
	void HandleLayoutCommitted(ACastleBoard* Board, FVector Origin);
};
