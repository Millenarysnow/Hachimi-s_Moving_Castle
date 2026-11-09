#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BuildingMergeComponent.generated.h"

class UBuildingPartCatalog;
class UPrimitiveComponent;
class UStaticMeshComponent;

/** 变化完成、原件恢复且全部临时显示网格清理后广播。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBuildingMergeCompleted);

/**
 * 局部零件变化组件：保留件不动，变化件从原位置移到新位置，新增件沿弧线飞入。
 * 以几何目录/显式身份进行一对一对应，不依赖导出序号，也不把建筑打散到公共中心。
 * 多个结果可共用 ChangeOrigin，从同一次操作的位置向周围传播变化。
 */
UCLASS(ClassGroup=(Castle), meta=(BlueprintSpawnableComponent, DisplayName="Building Merge"))
class MOVING_CASTLE_API UBuildingMergeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBuildingMergeComponent();

	/** 普通 Actor 可指定 Source Actors 并勾选自动播放，无需蓝图逻辑。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Playback")
	bool bAutoPlay = false;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Building Merge|Playback")
	TArray<TObjectPtr<AActor>> SourceActors;

	/** 新放下的来源建筑，其可复用零件也按放置飞入处理；已有来源的零件只做局部移动。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Building Merge|Playback")
	TObjectPtr<AActor> IncomingSourceActor;

	/** 不同网格资产间的离线几何身份。相同网格可直接对应；MergeIdentity=... 标签优先。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Matching")
	TObjectPtr<UBuildingPartCatalog> PartCatalog;

	/** 位置和尺寸差异小于此厘米数且朝向一致时，保持原位，不重复播放动画。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Matching", meta=(ClampMin="0.0", Units="cm"))
	double KeepPositionTolerance = 0.5;

	/** 单个新增零件的飞入时间；局部移动使用其 55%，总时长另加按距离传播的等待。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Motion", meta=(ClampMin="0.1", Units="s"))
	double Duration = 1.2;

	/** 新件弧线高度；短移动按实际距离限制弧高，避免整栋建筑剧烈跳动。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Motion", meta=(ClampMin="0.0", Units="cm"))
	double LiftHeight = 120.0;

	/** 新件从放置方向外侧飞入的距离，沿用独立零件飞入的表现语言。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Motion", meta=(ClampMin="0.0", Units="cm"))
	double FlightDistance = 550.0;

	/** 世界空间的来向，必须是非零有限向量；各相邻变化可以使用同一来向。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Motion")
	FVector ArrivalDirection = FVector::ForwardVector;

	/** 多处变化共享同一次操作的世界位置；关闭时使用新放置来源或结果位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Propagation")
	bool bUseSharedChangeOrigin = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Propagation", meta=(EditCondition="bUseSharedChangeOrigin"))
	FVector ChangeOrigin = FVector::ZeroVector;

	/** 传播速度为厘米/秒，使用真实距离，不按各栋建筑分别归一化。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Propagation", meta=(ClampMin="1.0"))
	double PropagationSpeed = 3600.0;

	/** 排除等级标签底牌等非建筑结构网格。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Selection")
	FName ExcludedComponentTag = TEXT("MergeExclude");

	/** 开始/完成/失败始终输出日志，屏幕提示可独立关闭。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Feedback")
	bool bShowStatusOnScreen = true;

	/** 运行统计区分真实复用、新增与移除，便于检查自动对应是否符合美术设计。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	bool bIsPlaying = false;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	int32 SourcePartCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	int32 TargetPartCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	int32 ReusedPartCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	int32 UnchangedPartCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	int32 MovedPartCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	int32 IncomingPartCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	int32 AddedPartCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	int32 RemovedPartCount = 0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	int32 CompletedPartCount = 0;

	/** 实际整轮时长含传播等待；调用方应使用此值判断整轮结束。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	double TotalDuration = 0.0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	double ElapsedTime = 0.0;
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	FString LastError;

	UPROPERTY(BlueprintAssignable, Category="Building Merge|Playback")
	FBuildingMergeCompleted OnMergeCompleted;

	/** 在旧 Actor 销毁前启动；至少两个有效来源。完整建立计划后才隐藏来源。 */
	UFUNCTION(BlueprintCallable, Category="Building Merge|Playback")
	bool StartMerge(const TArray<AActor*>& Sources);

	/** 邻接/PCG 只改变一座建筑时也可使用同一表现；至少一个旧 Actor，结果是组件所属 Actor。 */
	UFUNCTION(BlueprintCallable, Category="Building Merge|Playback")
	bool StartChange(const TArray<AActor*>& Sources);

	/** 恢复仍存在的原件与来源，不重建已经由玩法销毁的旧建筑。 */
	UFUNCTION(BlueprintCallable, Category="Building Merge|Playback")
	void ResetMerge();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 捕获阶段只有数据；来源销毁前将所需网格和材质复制到瞬态显示组件。 */
	struct FPartSnapshot
	{
		UStaticMeshComponent* Mesh = nullptr;
		FTransform WorldTransform;
		FVector Center = FVector::ZeroVector;
		FVector LocalCenter = FVector::ZeroVector;
		FVector LocalSize = FVector::ZeroVector;
		FString Identity;
		FString ShapeIdentity;
		double Radius = 0.0;
		double BaseHeight = 0.0;
		bool bIncoming = false;
		/** 上一轮已安排、但还在等待飞入的零件也属于输入，不能只抓当前可见组件。 */
		bool bPendingArrival = false;
		double RemainingDelay = 0.0;
	};

	enum class EPartMotion : uint8 { Keep, Move, Arrive, Depart };

	/** 每件保存自己的起终点，围绕几何中心计算路径，支持共享导入原点。 */
	struct FMergePart
	{
		TWeakObjectPtr<UStaticMeshComponent> Target;
		TWeakObjectPtr<UStaticMeshComponent> Visual;
		FTransform InitialRelative;
		FVector MeshCenter = FVector::ZeroVector;
		FVector TargetMeshCenter = FVector::ZeroVector;
		FVector EndCenter = FVector::ZeroVector;
		double TargetUnitRatio = 1.0;
		double Delay = 0.0;
		double PlayDuration = 0.0;
		double ArcHeight = 0.0;
		EPartMotion Motion = EPartMotion::Keep;
		bool bFinished = false;
	};

	struct FPrimitiveState
	{
		TWeakObjectPtr<UPrimitiveComponent> Primitive;
		bool bVisible = false;
	};
	struct FSourceState
	{
		TWeakObjectPtr<AActor> Actor;
		bool bHidden = false;
		bool bCollision = false;
	};

	TArray<FMergePart> Parts;
	TArray<FPrimitiveState> TargetStates;
	TArray<FSourceState> SourceStates;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Visuals;
	FVector UpOwnerSpace = FVector::UpVector;
	bool bCaptured = false;

	bool CaptureActor(AActor* Actor, TArray<FPartSnapshot>& OutParts, FString& Error) const;
	bool BuildPlan(const TArray<FPartSnapshot>& Sources, const TArray<FPartSnapshot>& Targets, const FVector& Origin, FString& Error);
	bool CreateVisual(FMergePart& Part, UStaticMeshComponent* Template);
	void UpdatePart(FMergePart& Part);
	void RestoreTarget();
	void DestroyVisuals();
	void CompleteMerge();
	bool FailMerge(const FString& Error);
	void ShowStatus(const FString& Message, const FColor& Color) const;
};
