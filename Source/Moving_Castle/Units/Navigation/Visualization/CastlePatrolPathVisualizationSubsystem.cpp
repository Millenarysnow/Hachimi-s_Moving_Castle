#include "Visualization/CastlePatrolPathVisualizationSubsystem.h"

#include "CastleNavigationSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/OutputDevice.h"
#include "Moving_Castle.h"

namespace
{
	// Overview / PathTracer_08 使用原包圆柱和 M_PT_DottedLine_Anim；项目材质实例保存原黄橙配色、透明间隔及动画速率。
	constexpr const TCHAR* LineMeshAsset = TEXT("/Game/PathTracer/Meshes/SM_Cylinder_Line.SM_Cylinder_Line");
	constexpr const TCHAR* LineMaterialAsset = TEXT("/Game/MovingCastle/FX/Navigation/MI_CastlePatrolLine.MI_CastlePatrolLine");
	// 复用演示直段的截面缩放 4，以及 75 cm 直段的 ScaleUV=1.5（一个 UV 周期对应 50 cm）。
	const FVector2D DashCrossSection(4.0, 4.0);
	constexpr double DashPeriodCm = 50.0;
	const FName ScaleUVParameter(TEXT("ScaleUV"));
	const FName OffsetParameter(TEXT("Offset"));
	const FVector LineLift(0.0, 0.0, 12.0); // 仅渲染抬高，避免斜坡与楼板交界吞掉线条，不写回路线坐标。

	// 命令按当前世界处理，不用全局 CVar；多开 PIE 或切关卡不会继承上个世界的选择。
	FAutoConsoleCommandWithWorldArgsAndOutputDevice ShowPatrolPathCommand(TEXT("Castle.ShowPatrolPath"),
		TEXT("Castle.ShowPatrolPath <0|1>：关闭或开启当前游戏世界的 NPC 共享巡逻线。"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
	{
		if (Args.Num() != 1 || (Args[0] != TEXT("0") && Args[0] != TEXT("1")))
		{
			const TCHAR* Message = TEXT("Castle.ShowPatrolPath：只接受 0 或 1。用法：Castle.ShowPatrolPath <0|1>");
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), Message);
			Output.Log(Message);
			return;
		}
		auto* Drawing = World ? World->GetSubsystem<UCastlePatrolPathVisualizationSubsystem>() : nullptr;
		if (!Drawing)
		{
			const TCHAR* Message = TEXT("Castle.ShowPatrolPath：请在运行中的 Game/PIE 世界执行。");
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), Message);
			Output.Log(Message);
			return;
		}
		const bool bEnable = Args[0] == TEXT("1");
		if (!Drawing->SetPathDrawingEnabled(bEnable))
		{
			Output.Log(TEXT("Castle.ShowPatrolPath：设置失败，原因见 LogMoving_Castle。"));
			return;
		}
		const TCHAR* Message = bEnable ? TEXT("NPC 巡逻线已开启；有可走路段的有效巡逻闭环才会显示。")
			: TEXT("NPC 巡逻线已关闭；地面圆圈、悬浮编号与巡逻保持不变。");
		UE_LOG(LogMoving_Castle, Display, TEXT("Castle.ShowPatrolPath：%s"), Message);
		Output.Log(Message);
	}));
}

bool UCastlePatrolPathVisualizationSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCastlePatrolPathVisualizationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 与导航共用世界生命周期。依赖顺序保证绑定和解绑时导航子系统在线。
	Navigation = Collection.InitializeDependency<UCastleNavigationSubsystem>();
	if (!IsValid(Navigation))
	{
		RejectDrawing(TEXT("缺少 UCastleNavigationSubsystem。"));
		return;
	}
	Navigation->OnSharedPathChanged.AddDynamic(this, &UCastlePatrolPathVisualizationSubsystem::HandleSharedPathChanged);
}

bool UCastlePatrolPathVisualizationSubsystem::SetPathDrawingEnabled(bool bEnabled)
{
	const UWorld* World = GetWorld();
	if (!IsInGameThread() || !HasCalledPostInitialize() || !World || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer)
	{
		return RejectDrawing(TEXT("开关只允许在有视图的运行世界中由游戏线程调用。"));
	}
	if (bEnabled == bDrawingEnabled) { return true; }
	if (!bEnabled)
	{
		StopDrawing();
		return true;
	}

	// 开启时先核对正式样式，即使当前还没有路线，也不能对缺失或错误配置报开启成功。
	LineMesh = LoadObject<UStaticMesh>(nullptr, LineMeshAsset);
	LineMaterial = LoadObject<UMaterialInterface>(nullptr, LineMaterialAsset);
	if (!IsValid(LineMesh) || !LineMesh->HasValidRenderData() || LineMesh->GetStaticMaterials().Num() != 1
		|| !IsValid(LineMaterial) || !LineMaterial->GetUsageByFlag(MATUSAGE_SplineMesh))
	{
		StopDrawing();
		return RejectDrawing(TEXT("网格必须可渲染且只有一个材质槽，材质必须已启用样条网格用法。"));
	}
	float ParameterValue = 0.0f;
	if (!LineMaterial->GetScalarParameterValue(ScaleUVParameter, ParameterValue)
		|| !LineMaterial->GetScalarParameterValue(OffsetParameter, ParameterValue))
	{
		StopDrawing();
		return RejectDrawing(TEXT("黄虚线材质缺少 ScaleUV 或 Offset 参数。"));
	}
	const FBoxSphereBounds Bounds = LineMesh->GetBounds();
	if (Bounds.Origin.ContainsNaN() || Bounds.BoxExtent.ContainsNaN() || Bounds.BoxExtent.GetMin() <= UE_DOUBLE_SMALL_NUMBER)
	{
		StopDrawing();
		return RejectDrawing(TEXT("圆柱网格的局部包围盒无效。"));
	}

	bDrawingEnabled = true;
	if (!RefreshDrawing())
	{
		StopDrawing();
		return false;
	}
	return true;
}

void UCastlePatrolPathVisualizationSubsystem::HandleSharedPathChanged()
{
	// 导航先发布失效快照再重算，因此不会在建筑移动期间保留旧线。
	if (bDrawingEnabled && !RefreshDrawing()) { StopDrawing(); }
}

bool UCastlePatrolPathVisualizationSubsystem::RefreshDrawing()
{
	if (!IsValid(Navigation) || !IsValid(LineMesh) || !IsValid(LineMaterial))
	{
		return RejectDrawing(TEXT("导航子系统或已加载的绘制资产失效。"));
	}
	const auto Path = Navigation->AcquireSharedPath();
	if (!Path || !Path->bValid || !Path->bClosedLoop)
	{
		// 尚未发布、已失效和单段导航演示都不代表有可绘制的 NPC 巡逻闭环。
		DestroyPathSegments();
		return true;
	}
	const TArray<FVector>& Points = Path->PatrolPoints;
	if (Points.IsEmpty() || Points[0].ContainsNaN())
	{
		return RejectDrawing(FString::Printf(TEXT("巡逻快照 %d 没有有效的起点。"), Path->PathRevision));
	}
	if (Points.Num() == 1)
	{
		// 单点是合法的待命请求，编号标记由输入层显示，此处没有路段可画。
		DestroyPathSegments();
		return true;
	}

	return BuildPathSegments(*Path);
}

bool UCastlePatrolPathVisualizationSubsystem::BuildPathSegments(const FCastleSharedNavigationPath& Path)
{
	struct FDashSegment
	{
		FVector Origin;
		FQuat Rotation;
		double Length;
		float ScaleUV;
		float Offset;
	};

	// 全部使用 NPC 实际采样的相邻点；不复刻示例的大圆角，否则楼梯和建筑转折上会抄近路。
	const TArray<FVector>& Points = Path.PatrolPoints;
	const TArray<double>& Distances = Path.PatrolDistances;
	if (Distances.Num() != Points.Num() || Distances[0] != 0.0)
	{
		return RejectDrawing(FString::Printf(TEXT("巡逻快照 %d 的累计距离与采样列表不匹配。"), Path.PathRevision));
	}
	TArray<FDashSegment> Segments;
	Segments.Reserve(Points.Num() - 1);
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		const FVector Delta = Points[Index] - Points[Index - 1];
		const double Length = Delta.Size();
		if (!FMath::IsFinite(Length) || Length <= UE_DOUBLE_SMALL_NUMBER || !FMath::IsFinite(Distances[Index])
			|| !FMath::IsNearlyEqual(Distances[Index] - Distances[Index - 1], Length, 0.01))
		{
			return RejectDrawing(FString::Printf(TEXT("巡逻快照 %d 的第 %d 条采样线段或累计距离无效。"), Path.PathRevision, Index));
		}
		// 在组件局部 X 轴上拉直网格，再整体旋转到实际三维方向。梯子段也不会遇到样条上向量与切线平行。
		// UV 跨度随长度缩放，起始相位来自整圈累计距离；不能每 20 cm 采样点都从同一相位重播。
		Segments.Add({Points[Index - 1] + LineLift, FQuat::FindBetweenNormals(FVector::ForwardVector, Delta / Length),
			Length, static_cast<float>(Length / DashPeriodCm), static_cast<float>(FMath::Fmod(Distances[Index - 1] / DashPeriodCm, 1.0))});
	}

	DestroyPathSegments();
	UWorld* World = GetWorld();
	FActorSpawnParameters Spawn;
	Spawn.OverrideLevel = World->PersistentLevel;
	Spawn.ObjectFlags |= RF_Transient;
	PathVisualActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
	if (!IsValid(PathVisualActor)) { return RejectDrawing(TEXT("无法创建临时巡逻线 Actor。")); }

	USceneComponent* Root = NewObject<USceneComponent>(PathVisualActor, TEXT("CastlePatrolPathSegments"), RF_Transient);
	PathVisualActor->AddInstanceComponent(Root);
	if (!PathVisualActor->SetRootComponent(Root)) { return RejectDrawing(TEXT("无法安装巡逻线根组件。")); }

	// 同一个临时 Actor 管理整条路线。线段自身只在发布时设置参数，虚线运动完全由原材质的时间输入驱动。
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		const FDashSegment& Segment = Segments[Index];
		const FName Name(*FString::Printf(TEXT("CastlePatrolDash_%d"), Index));
		auto* Line = NewObject<USplineMeshComponent>(PathVisualActor, Name, RF_Transient);
		PathVisualActor->AddInstanceComponent(Line);
		Line->SetupAttachment(Root);
		Line->SetMobility(EComponentMobility::Movable);
		// 只画画面，不拦建造或放点，也不参与导航、物理和阴影。
		Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Line->SetGenerateOverlapEvents(false);
		Line->SetCanEverAffectNavigation(false);
		Line->SetCastShadow(false);
		Line->SetAffectDistanceFieldLighting(false);
		Line->SetVisibleInRayTracing(false);
		Line->SetReceivesDecals(false);
		if (!Line->SetStaticMesh(LineMesh)) { return RejectDrawing(FString::Printf(TEXT("第 %d 段无法安装圆柱网格。"), Index + 1)); }
		auto* Material = UMaterialInstanceDynamic::Create(LineMaterial, Line);
		if (!IsValid(Material)) { return RejectDrawing(FString::Printf(TEXT("第 %d 段无法创建黄虚线材质实例。"), Index + 1)); }
		Material->SetScalarParameterValue(ScaleUVParameter, Segment.ScaleUV);
		Material->SetScalarParameterValue(OffsetParameter, Segment.Offset);
		Line->SetMaterial(0, Material);
		Line->SetRelativeLocationAndRotation(Segment.Origin, Segment.Rotation);
		Line->SetForwardAxis(ESplineMeshAxis::X, false);
		Line->SetStartScale(DashCrossSection, false);
		Line->SetEndScale(DashCrossSection, false);
		const FVector Tangent(Segment.Length, 0, 0);
		Line->SetStartAndEnd(FVector::ZeroVector, Tangent, Tangent, Tangent, false);
	}

	// 只在整条路径构建完毕之后注册，报错由调用者销毁完整的临时 Actor，不留半套渲染组件。
	PathVisualActor->RegisterAllComponents();
	for (const UActorComponent* Component : PathVisualActor->GetComponents())
	{
		if (!Component->IsRegistered())
		{
			return RejectDrawing(FString::Printf(TEXT("巡逻线组件 %s 未注册。"), *Component->GetName()));
		}
	}
	return true;
}

bool UCastlePatrolPathVisualizationSubsystem::RejectDrawing(const FString& Reason) const
{
	UE_LOG(LogMoving_Castle, Error, TEXT("NPC 巡逻线 %s（网格 %s，材质 %s）：%s"),
		*GetPathName(), LineMeshAsset, LineMaterialAsset, *Reason);
	return false;
}

void UCastlePatrolPathVisualizationSubsystem::DestroyPathSegments()
{
	// 正常关闭、路线失效和世界结束都走同一清理，避免留下旧地图实例或屏幕残线。
	if (IsValid(PathVisualActor)) { PathVisualActor->Destroy(); }
	PathVisualActor = nullptr;
}

void UCastlePatrolPathVisualizationSubsystem::StopDrawing()
{
	bDrawingEnabled = false;
	DestroyPathSegments();
	LineMesh = nullptr;
	LineMaterial = nullptr;
}

void UCastlePatrolPathVisualizationSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	StopDrawing();
	Super::OnWorldEndPlay(InWorld);
}

void UCastlePatrolPathVisualizationSubsystem::Deinitialize()
{
	if (IsValid(Navigation))
	{
		Navigation->OnSharedPathChanged.RemoveDynamic(this, &UCastlePatrolPathVisualizationSubsystem::HandleSharedPathChanged);
	}
	StopDrawing();
	Navigation = nullptr;
	Super::Deinitialize();
}
