#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BuildingMergeComponent.generated.h"

class UPrimitiveComponent;
class UStaticMeshComponent;

/** 所有新建筑零件归位、原模型恢复且临时零件清理完毕后广播。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBuildingMergeCompleted);

/**
 * 以真实建筑零件表现“拆散、旋流汇聚、展开咬合”的通用合并组件。
 * 挂在合并结果 Actor 上，StartMerge 在旧 Actor 销毁前抓取网格、材质和几何中心。
 * 动画只移动瞬态网格副本，原模型的变换、组件层级、Mobility 和材质保持原样。
 * 不负责等级、格子、背包或销毁建筑；这些规则由调用方管理，动画不阻塞逻辑合并。
 */
UCLASS(ClassGroup=(Castle), meta=(BlueprintSpawnableComponent, DisplayName="Building Merge"))
class MOVING_CASTLE_API UBuildingMergeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBuildingMergeComponent();

	/** 普通 Actor 可勾选自动播放并指定 Sources；棋盘生成的建筑由 C++ 合并流程触发。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Playback")
	bool bAutoPlay = false;

	/** 自动播放的旧建筑列表。只用于实例配置，不要求蓝图事件或关卡蓝图连线。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Building Merge|Playback")
	TArray<TObjectPtr<AActor>> SourceActors;

	/** 完整合并时长，内部按比例调度拆件和重组，因此零件很多时也不会无限延长。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Playback", meta=(ClampMin="0.1", Units="s"))
	double Duration = 3.2;

	/** 拆件时的抬升高度（厘米），旋流轨迹也使用该高度形成上下交错。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Motion", meta=(ClampMin="0.0", Units="cm"))
	double LiftHeight = 160.0;

	/** 零件从旧建筑向外松开的幅度，相对于参与建筑的整体水平半径。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Motion", meta=(ClampMin="0.0", ClampMax="0.5"))
	double SeparationRatio = 0.16;

	/** 汇聚过程中绕中心旋转的角度；保留真实网格与材质，不用粒子替代建筑。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Motion", meta=(ClampMin="0.0", ClampMax="360.0", Units="deg"))
	double SwirlAngle = 115.0;

	/** 带此标签的辅助网格不作为建筑零件飞行，例如等级标签的底牌。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Selection")
	FName ExcludedComponentTag = TEXT("MergeExclude");

	/** 开始、完成、失败始终写入 LogBuildingMerge；屏幕文字可按需关闭。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building Merge|Feedback")
	bool bShowStatusOnScreen = true;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	bool bIsPlaying = false;

	/** 抓取的真实旧建筑网格数，源 Actor 随后销毁也不影响动画。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	int32 SourcePartCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	int32 TargetPartCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	int32 CompletedPartCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	double ElapsedTime = 0.0;

	/** 配置错误或外部移除了目标零件时明确报错，不切换成整栋缩放或其他替代动画。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Building Merge|Runtime")
	FString LastError;

	UPROPERTY(BlueprintAssignable, Category="Building Merge|Playback")
	FBuildingMergeCompleted OnMergeCompleted;

	/** 先完整校验并创建副本，成功后才隐藏原件。至少需要两个有效、互不重复的源 Actor。 */
	UFUNCTION(BlueprintCallable, Category="Building Merge|Playback")
	bool StartMerge(const TArray<AActor*>& Sources);

	/** 取消或重播前恢复保存过的显隐/碰撞并清理副本；不会重新生成被调用方销毁的源 Actor。 */
	UFUNCTION(BlueprintCallable, Category="Building Merge|Playback")
	void ResetMerge();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 每件独立保存几何中心，避免 SketchUp 共享原点造成零件绕建筑原点旋转。 */
	struct FMergePart
	{
		TWeakObjectPtr<UStaticMeshComponent> Target;
		TWeakObjectPtr<UStaticMeshComponent> Visual;
		FTransform InitialRelative;
		FVector MeshCenter = FVector::ZeroVector;
		FVector StartCenter = FVector::ZeroVector;
		FVector ScatterCenter = FVector::ZeroVector;
		FQuat Tilt = FQuat::Identity;
		double Order = 0.0;
		double Delay = 0.0;
		bool bTarget = false;
		bool bFinished = false;
	};

	/** 原件只修改自身显隐，不递归传播，以免破坏原来隐藏的嵌套子组件。 */
	struct FPrimitiveState
	{
		TWeakObjectPtr<UPrimitiveComponent> Primitive;
		bool bVisible = false;
	};

	/** 普通 Actor 复用时可取消恢复；正式棋盘会在抓取成功后销毁这些旧 Actor。 */
	struct FSourceState
	{
		TWeakObjectPtr<AActor> Actor;
		bool bHidden = false;
		bool bCollision = false;
	};

	TArray<FMergePart> Parts;
	TArray<FPrimitiveState> TargetStates;
	TArray<FSourceState> SourceStates;

	/** 强引用仅持有本轮的瞬态副本，完成/取消时逐个销毁，避免残留渲染组件。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Visuals;

	FVector CoreCenter = FVector::ZeroVector;
	FVector LiftOffset = FVector::ZeroVector;
	double ActiveDuration = 0.0;
	double ActiveSwirlAngle = 0.0;
	bool bCaptured = false;

	bool CaptureActor(AActor* Actor, bool bTarget, const FTransform& OwnerTransform, FBox& Bounds, FString& Error);
	void UpdatePart(FMergePart& Part);
	void RestoreTarget();
	void DestroyVisuals();
	void CompleteMerge();
	bool FailMerge(const FString& Error);
	void ShowStatus(const FString& Message, const FColor& Color) const;
};
