#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Subsystems/WorldSubsystem.h"
#include "CastleNavigationSubsystem.generated.h"

class ANavigationData;
class UAssemblyFlightComponent;
class UBuildingMergeComponent;
class UNavigationSystemV1;

/** 全世界的一份路线快照。多个 NPC 可以持有同一个只读快照，各自记录路段进度。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleSharedNavigationPath
{
	GENERATED_BODY()

	/** 同一轮 NavMesh 完成构建后递增；不保存可能因 Tile 重建失效的多边形引用。 */
	UPROPERTY(BlueprintReadOnly, Category="共享路线")
	int32 NavigationRevision = 0;

	/** 每次发布均递增，包含几何开始变化时发布的失效快照。 */
	UPROPERTY(BlueprintReadOnly, Category="共享路线")
	int32 PathRevision = 0;

	/** UE 求解的世界坐标转折点，非逐台阶贴地采样；不是玩家设置的路径点。 */
	UPROPERTY(BlueprintReadOnly, Category="共享路线")
	TArray<FVector> Points;

	/** 保留引擎路径点标记，后续跟随器可识别 NavLink 等特殊路段。 */
	UPROPERTY(BlueprintReadOnly, Category="共享路线")
	TArray<int32> PointFlags;

	UPROPERTY(BlueprintReadOnly, Category="共享路线")
	double Length = 0.0;

	UPROPERTY(BlueprintReadOnly, Category="共享路线")
	bool bValid = false;

	/** 玩家修改路径点时递增；仅重建 NavMesh 不改变它，用于保留单位当前目标路段。 */
	UPROPERTY(BlueprintReadOnly, Category="共享路线")
	int32 RouteRevision = 0;

	/** 循环巡逻与旧的单段调试请求共用发布机制，只有循环路线可由 Mass 巡逻器消费。 */
	UPROPERTY(BlueprintReadOnly, Category="共享路线")
	bool bClosedLoop = false;

	/** 投影后的玩家路径点，顺序就是 1→2→…→N→1，不能用求解器的转折点代替。 */
	UPROPERTY(BlueprintReadOnly, Category="共享路线")
	TArray<FVector> Waypoints;

	/** 整条闭环一次性生成的贴 NavMesh 采样；所有实体共享，运行时不再逐实体投影或寻路。 */
	UPROPERTY(BlueprintReadOnly, Category="共享路线")
	TArray<FVector> PatrolPoints;

	/** 与 PatrolPoints 一一对应的累计距离，可二分定位，支持一帧跨多个转折或整圈。 */
	TArray<double> PatrolDistances;

	/** 每个玩家路径点在闭环中的累计距离，最后额外存整圈长度，明确保留 N→1 路段。 */
	TArray<double> WaypointDistances;

	/** 只读数值运算，可由 Mass 工作线程调用；Distance 已由调用方限制到 [0, Length]。 */
	void SamplePatrol(double Distance, FVector& OutLocation, FVector& OutDirection) const;

	/** 只在路线发布后定位一次，不进行 NavMesh 查询。范围可限制到原目标路段以免跳过路径点。 */
	bool FindClosestPatrolDistance(FVector Location, int32 Leg, double& OutDistance, double& OutSeparation) const;

	/** 返回距离所在的玩家路段；不将采样点或转折点当作玩家路径点。 */
	int32 FindPatrolLeg(double Distance) const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCastleSharedPathChanged);

/**
 * 只负责建造完成后的 NavMesh 更新，以及一份共享路线的集中求解。
 * 不保存六面连通关系，不创建 AIController，也不让每个 NPC 单独查询导航。
 */
UCLASS()
class MOVING_CASTLE_API UCastleNavigationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	/** 同一来源支持嵌套；第一项变化锁定生成，最后一项结束后的下一帧才允许更新。 */
	bool BeginGeometryChange(UObject* Source);
	void EndGeometryChange(UObject* Source);

	/** 动画原生开始位置调用；直接消费既有结束信号，包含取消和销毁的清理。 */
	void ObserveAssembly(UAssemblyFlightComponent* Flight);
	void ObserveMerge(UBuildingMergeComponent* Merge);

	/** PCG 或不经过棋盘的几何编辑完成后，可显式请求一轮全局刷新。建造中会合并请求。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|NPC导航")
	bool RequestNavigationRefresh(FText& OutFailureReason);

	/** 设置唯一共享路线的起终点；相同请求复用缓存，NavMesh 更新后自动集中重算。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|NPC导航")
	bool RequestSharedPath(FVector Start, FVector Goal, FText& OutFailureReason);

	/** 一次接收整组路径点，集中算完所有相邻最短路和 N→1 后才原子发布；单点表示原地待命。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|NPC导航")
	bool RequestSharedPatrolRoute(const TArray<FVector>& Waypoints, FText& OutFailureReason);

	/** 清除全局巡逻请求并立即失效；NPC 停止，不沿旧快照继续走。 */
	UFUNCTION(BlueprintCallable, Category="移动城堡|NPC导航")
	void ClearSharedRoute();

	/** 获取玩家原始设置的世界坐标，供编辑入口继续修改；不触发任何查询。 */
	const TArray<FVector>& GetRequestedWaypoints() const { return RequestedWaypoints; }

	/** 蓝图返回副本；C++ 大量单位应使用 AcquireSharedPath 共享同一个快照。 */
	UFUNCTION(BlueprintPure, Category="移动城堡|NPC导航")
	FCastleSharedNavigationPath GetSharedPath() const;

	/** 可以跨线程持有只读数据；新旧版本不原地修改，Mass Processor 每轮取得一次并共享给整批单位。 */
	TSharedPtr<const FCastleSharedNavigationPath, ESPMode::ThreadSafe> AcquireSharedPath() const;

	UFUNCTION(BlueprintPure, Category="移动城堡|NPC导航")
	bool IsNavigationReady() const;

	/** 全世界未结束的变化数量，包含嵌套操作；稳定场景不轮询建筑或动画组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="导航状态")
	int32 ActiveChangeCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="导航状态")
	int32 NavigationRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="导航状态")
	int32 RefreshCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="导航状态")
	int32 PathQueryCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="导航状态")
	bool bBuildingNavigation = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="导航状态")
	FText LastFailure;

	/** 起终点只在此范围内投影，防止误吸附到很远的道路或其他楼层。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="导航查询")
	FVector ProjectionExtent = FVector(80.0, 80.0, 100.0);

	/** 仅在整条路线重算时采样。步长越小越能贴合台阶，内存与集中预处理开销也越大。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="导航查询", meta=(ClampMin="1.0", Units="cm"))
	double PatrolSampleSpacing = 20.0;

	/** 变更使路线失效或新路线发布时通知界面；不得在每个 NPC 上绑定此事件再单独寻路。 */
	UPROPERTY(BlueprintAssignable, Category="移动城堡|NPC导航")
	FCastleSharedPathChanged OnSharedPathChanged;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

private:
	/** 弱来源只用于配对开始/结束；不拥有建筑或动画组件。 */
	TMap<TWeakObjectPtr<UObject>, int32> Changes;
	TWeakObjectPtr<UNavigationSystemV1> LockedNavigation;
	bool bRefreshPending = false;
	bool bHasPathRequest = false;
	bool bPathPending = false;
	bool bShuttingDown = false;
	uint64 CommitFrame = 0;
	uint64 LastChangeFrame = 0;
	double BuildStartTime = 0.0;
	TArray<FVector> RequestedWaypoints;
	bool bRequestedClosedLoop = false;
	int32 RouteRevision = 0;
	int32 PathRevision = 0;
	mutable FRWLock PathLock;
	TSharedPtr<const FCastleSharedNavigationPath, ESPMode::ThreadSafe> PublishedPath;

	UNavigationSystemV1* GetNavigation() const;
	bool ValidateNavigation(UNavigationSystemV1*& OutNavigation, ANavigationData*& OutData, FText& OutFailureReason) const;
	void InvalidatePath();
	void PublishPath(FCastleSharedNavigationPath&& Path);
	void CalculateSharedPath();
	bool RequestRoute(const TArray<FVector>& Waypoints, bool bClosedLoop, FText& OutFailureReason);
	void FailNavigation(const FText& Reason);
	void HandleAssemblyFinished(UAssemblyFlightComponent* Flight, bool bCompleted);
	void HandleMergeFinished(UBuildingMergeComponent* Merge, bool bCompleted);
};

/** 同步放置/替换/删除的栈作用域。与动画观察共享计数，函数返回不会提前解锁后续合并。 */
class MOVING_CASTLE_API FScopedCastleNavigationChange
{
public:
	FScopedCastleNavigationChange(UWorld* World, UObject* Source);
	~FScopedCastleNavigationChange();
	FScopedCastleNavigationChange(const FScopedCastleNavigationChange&) = delete;
	FScopedCastleNavigationChange& operator=(const FScopedCastleNavigationChange&) = delete;
private:
	UCastleNavigationSubsystem* Navigation = nullptr;
	UObject* Source = nullptr;
};
