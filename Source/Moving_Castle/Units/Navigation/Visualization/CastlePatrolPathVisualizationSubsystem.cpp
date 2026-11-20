#include "Visualization/CastlePatrolPathVisualizationSubsystem.h"

#include "CastleNavigationSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/OutputDevice.h"
#include "Moving_Castle.h"

namespace
{
	// Path Tracer Toolkit 的实心样式：原包圆柱网格 + M_PT_Color 的项目材质实例，保留真实楼板遮挡。
	constexpr const TCHAR* LineMeshAsset = TEXT("/Game/PathTracer/Meshes/SM_Cylinder_Line.SM_Cylinder_Line");
	constexpr const TCHAR* LineMaterialAsset = TEXT("/Game/MovingCastle/FX/Navigation/MI_CastlePatrolLine.MI_CastlePatrolLine");
	constexpr double LineRadius = 7.0; // 厘米：从俯视仍能辨认线路，又不会盖住棋盘格心和路点。
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
			: TEXT("NPC 巡逻线已关闭；编号路点与巡逻保持不变。");
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
		|| !IsValid(LineMaterial) || !LineMaterial->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes))
	{
		StopDrawing();
		return RejectDrawing(TEXT("网格必须可渲染且只有一个材质槽，材质必须已启用实例网格用法。"));
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

	const FBoxSphereBounds Bounds = LineMesh->GetBounds();
	TArray<FTransform> Instances;
	Instances.Reserve(Points.Num() - 1);
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		// 包括已采样的格心转折、上下楼和末点回到首点；不额外求解，不在角点间平滑抄近路。
		const FVector Delta = Points[Index] - Points[Index - 1];
		const double Length = Delta.Size();
		if (!FMath::IsFinite(Length) || Length <= UE_DOUBLE_SMALL_NUMBER)
		{
			return RejectDrawing(FString::Printf(TEXT("巡逻快照 %d 的第 %d 条采样线段无效，拒绝绘制整条路线。"),
				Path->PathRevision, Index));
		}
		const FQuat Rotation = FQuat::FindBetweenNormals(FVector::ForwardVector, Delta / Length);
		const FVector Scale(Length / (2.0 * Bounds.BoxExtent.X), LineRadius / Bounds.BoxExtent.Y, LineRadius / Bounds.BoxExtent.Z);
		// 网格的长轴为局部 X，但枢轴不在中点。把真实包围盒中心对齐线段中点，避免每段偏移半截。
		const FVector Center = Points[Index - 1] + Delta * 0.5 + LineLift;
		Instances.Emplace(Rotation, Center - Rotation.RotateVector(Bounds.Origin * Scale), Scale);
	}

	if (!PathSegments && !CreatePathSegments()) { return false; }
	if (!IsValid(PathSegments) || !PathSegments->IsRegistered())
	{
		return RejectDrawing(TEXT("巡逻线实例组件失效或未注册。"));
	}
	// 整批验证通过才提交渲染数据；绝不能留下部分路径，也不通知 NavMesh。
	PathSegments->ClearInstances();
	PathSegments->AddInstances(Instances, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true, /*bUpdateNavigation=*/false);
	if (PathSegments->GetInstanceCount() != Instances.Num())
	{
		return RejectDrawing(TEXT("未能向实例组件提交整条巡逻线。"));
	}
	return true;
}

bool UCastlePatrolPathVisualizationSubsystem::CreatePathSegments()
{
	UWorld* World = GetWorld();
	FActorSpawnParameters Spawn;
	Spawn.OverrideLevel = World->PersistentLevel;
	Spawn.ObjectFlags |= RF_Transient;
	AActor* Owner = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
	if (!IsValid(Owner)) { return RejectDrawing(TEXT("无法创建临时巡逻线 Actor。")); }

	PathSegments = NewObject<UInstancedStaticMeshComponent>(Owner, TEXT("CastlePatrolPathSegments"), RF_Transient);
	Owner->AddInstanceComponent(PathSegments);
	if (!Owner->SetRootComponent(PathSegments) || !PathSegments->SetStaticMesh(LineMesh))
	{
		return RejectDrawing(TEXT("无法安装巡逻线实例组件或圆柱网格。"));
	}
	// 只画画面，不拦建造/放点的射线，不参与导航、物理和阴影；随世界销毁，不保存到关卡。
	PathSegments->SetMobility(EComponentMobility::Movable);
	PathSegments->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PathSegments->SetGenerateOverlapEvents(false);
	PathSegments->SetCanEverAffectNavigation(false);
	PathSegments->SetCastShadow(false);
	PathSegments->SetAffectDistanceFieldLighting(false);
	PathSegments->SetVisibleInRayTracing(false);
	PathSegments->SetReceivesDecals(false);
	PathSegments->SetMaterial(0, LineMaterial);
	PathSegments->RegisterComponent();
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
	if (IsValid(PathSegments))
	{
		if (AActor* Owner = PathSegments->GetOwner(); IsValid(Owner)) { Owner->Destroy(); }
	}
	PathSegments = nullptr;
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
