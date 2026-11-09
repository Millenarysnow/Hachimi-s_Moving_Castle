#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "AssemblyFlightComponent.generated.h"

class UStaticMeshComponent;

/** 组装完成事件；正式游戏可按需绑定，组件自身不引用任何城堡、商店或房屋蓝图。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAssemblyFlightCompleted);

/**
 * 通用连续飞入组件。给包含独立 StaticMeshComponent 的 Actor 添加本组件即可播放，
 * 适合建筑装配、道具入场等正式运行时效果。
 *
 * 所有排序、发射调度、弧线运动、中心旋转和收尾都由 C++ 完成。
 * 原零件只临时隐藏并禁用自身碰撞；飞行使用瞬态显示组件，因而可以安全处理网格根组件和嵌套层级。
 * 不处理实例化网格、骨骼网格或物理模拟零件；配置不合法时明确报错，不改用其他显现方式。
 */
UCLASS(ClassGroup=(Castle), meta=(BlueprintSpawnableComponent, DisplayName="Assembly Flight"))
class MOVING_CASTLE_API UAssemblyFlightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAssemblyFlightComponent();

	/** 游戏开始时自动播放；编辑器中不运行动画，保留完整模型以便摆放。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assembly Flight|Playback")
	bool bAutoPlay = true;

	/** 每件的飞行时长。与发射间隔独立，二者相除近似为稳定阶段的同时飞行数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assembly Flight|Playback", meta=(ClampMin="0.001", Units="s"))
	double FlightDuration = 1.2;

	/** 按高度排序后，相邻零件的发射时间差；默认 0.08 秒对应约 15 件同时飞行。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assembly Flight|Playback", meta=(ClampMin="0.001", Units="s"))
	double LaunchInterval = 0.08;

	/** 起播等待时间；等待期间所选零件隐藏。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assembly Flight|Playback", meta=(ClampMin="0.0", Units="s"))
	double StartDelay = 1.0;

	/** 起点位于整座模型水平包围圆之外，此值为额外距离，单位厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assembly Flight|Path", meta=(ClampMin="0.001", Units="cm"))
	double FlightDistance = 750.0;

	/** 弧线的额外抬升高度；侧向弯曲为该值的 35%。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assembly Flight|Path", meta=(ClampMin="0.0", Units="cm"))
	double FlightArcHeight = 220.0;

	/** 留空收集所属 Actor 自身的所有静态网格；填写后只处理带此 Component Tag 的零件。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assembly Flight|Selection")
	FName PartComponentTag = NAME_None;

	/** 可关闭屏幕文字；开始、完成和失败始终记录到 LogAssemblyFlight，便于验收定位。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Assembly Flight|Feedback")
	bool bShowStatusOnScreen = true;

	/** 当前播放状态；组件只在播放期间 Tick，不占用所属 Actor 的 Tick 或蓝图事件。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Assembly Flight|Runtime")
	bool bIsPlaying = false;

	/** 本次选择的零件总数，包含尚未发射、飞行中和已落定的零件。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Assembly Flight|Runtime")
	int32 TotalPartCount = 0;

	/** 已精确落定的零件数。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Assembly Flight|Runtime")
	int32 CompletedPartCount = 0;

	/** 当前真实在途的显示组件数，不含等待发射的隐藏零件。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Assembly Flight|Runtime")
	int32 FlyingPartCount = 0;

	/** 本轮已播放的秒数，受所属 Actor 的时间缩放影响。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Assembly Flight|Runtime")
	double ElapsedTime = 0.0;

	/** 最近一次失败原因；成功开始后清空，失败同时写 Error 日志。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Assembly Flight|Runtime")
	FString LastError;

	/** 全部零件落定并恢复原状态后广播。 */
	UPROPERTY(BlueprintAssignable, Category="Assembly Flight|Playback")
	FAssemblyFlightCompleted OnAssemblyCompleted;

	/** 开始或重播。必须在游戏世界调用；先完整校验，校验失败不隐藏任何零件。 */
	UFUNCTION(BlueprintCallable, Category="Assembly Flight|Playback")
	bool StartAssembly();

	/** 停止动画，销毁临时显示组件并恢复零件原有显隐与碰撞；随后可以再次播放。 */
	UFUNCTION(BlueprintCallable, Category="Assembly Flight|Playback")
	void ResetAssembly();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 单件的运行时快照。弱引用防止组件被外部销毁时留下悬空指针。 */
	struct FFlightPart
	{
		TWeakObjectPtr<UStaticMeshComponent> Source;
		TWeakObjectPtr<UStaticMeshComponent> Visual;
		FVector LocalCenter = FVector::ZeroVector;
		FVector StartCenterOwnerSpace = FVector::ZeroVector;
		FVector ArcOffsetOwnerSpace = FVector::ZeroVector;
		FQuat StartTilt = FQuat::Identity;
		double BottomHeight = 0.0;
		double CenterHeight = 0.0;
		double LaunchTime = 0.0;
		FString StableName;
		ECollisionEnabled::Type OriginalCollision = ECollisionEnabled::NoCollision;
		bool bOriginalVisible = true;
		bool bOriginalHiddenInGame = false;
	};

	/** 仅缓存当前轮数据；按最终世界包围盒底部高度排序。 */
	TArray<FFlightPart> Parts;
	int32 NextLaunchIndex = 0;
	double ActiveFlightDuration = 0.0;
	bool bHasCapturedState = false;

	/** 分开校验和修改：任何零件不受支持时整轮拒绝，不产生半隐藏模型。 */
	bool CaptureParts(FString& OutError);
	bool CreateFlightVisual(FFlightPart& Part);
	void UpdateFlightVisual(FFlightPart& Part, double Age);
	void RestorePart(FFlightPart& Part);
	void RestoreCapturedState();
	bool FailAssembly(const FString& Error);
	void ShowStatus(const FString& Message, const FColor& Color) const;
};
