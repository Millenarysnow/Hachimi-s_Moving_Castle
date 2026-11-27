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

	struct FPatrolRenderSpan
	{
		FVector Direction;
		double Length;
		FBox Bounds;
		FVector LaneOffset = FVector::ZeroVector;
	};

	// 原材质的虚线沿 UV 前进。反向且共用同一段空间的圆柱会叠出两套相反相位，视觉上变成原地开合。
	// 检测三维中心线间距是否小于一条线宽，并要求有共同的纵向区间；相交路口、仅端点相接和上下楼板不是折返。
	bool OverlapsInReverse(const FPatrolRenderSpan& A, const FVector& StartA, const FVector& EndA,
		const FPatrolRenderSpan& B, const FVector& StartB, const FVector& EndB, double LineDiameter)
	{
		if (FVector::DotProduct(A.Direction, B.Direction) > -0.98) { return false; }
		const double From = FVector::DotProduct(StartB - StartA, A.Direction);
		const double To = FVector::DotProduct(EndB - StartA, A.Direction);
		// 此处 B 指向 A 的反向，To 是投影区间的低端；采样分段不完全对齐也能判定。
		if (FMath::Min(A.Length, From) - FMath::Max(0.0, To) <= UE_KINDA_SMALL_NUMBER) { return false; }
		FVector ClosestA, ClosestB;
		FMath::SegmentDistToSegmentSafe(StartA, EndA, StartB, EndB, ClosestA, ClosestB);
		return FVector::DistSquared(ClosestA, ClosestB) < FMath::Square(LineDiameter);
	}

	void SeparateReverseLanes(const TArray<FVector>& Points, const FBox& PathBounds, double LineDiameter,
		TArray<FPatrolRenderSpan>& Spans)
	{
		// 按最长空间轴扫掠包围盒，只精查附近路段；在路线发布时计算，不随帧或 NPC 数重算。
		const FVector Size = PathBounds.GetSize();
		const int32 Axis = Size.X >= Size.Y && Size.X >= Size.Z ? 0 : (Size.Y >= Size.Z ? 1 : 2);
		TArray<int32> Sweep;
		Sweep.Reserve(Spans.Num());
		for (int32 Index = 0; Index < Spans.Num(); ++Index) { Sweep.Add(Index); }
		Sweep.Sort([&](int32 A, int32 B) { return Spans[A].Bounds.Min[Axis] < Spans[B].Bounds.Min[Axis]; });

		const auto LaneOffset = [LineDiameter](const FVector& Direction)
		{
			// 两个方向各挪一条半线宽，即使原路已有不到一条线宽的错位，也能留出可见间隔。
			// 水平/斜坡取行进右侧；竖直段按升降分到世界 Y 两侧，所有侧移都保持原高度。
			const FVector Right(-Direction.Y, Direction.X, 0.0);
			const FVector Side = Right.SizeSquared() > UE_DOUBLE_SMALL_NUMBER
				? Right.GetUnsafeNormal() : FVector(0.0, FMath::Sign(Direction.Z), 0.0);
			return Side * (1.5 * LineDiameter);
		};
		for (int32 Order = 0; Order < Sweep.Num(); ++Order)
		{
			const int32 A = Sweep[Order];
			const FBox SearchBounds = Spans[A].Bounds.ExpandBy(LineDiameter);
			for (int32 Next = Order + 1; Next < Sweep.Num(); ++Next)
			{
				const int32 B = Sweep[Next];
				if (Spans[B].Bounds.Min[Axis] > SearchBounds.Max[Axis]) { break; }
				if (SearchBounds.Intersect(Spans[B].Bounds) && OverlapsInReverse(Spans[A], Points[A], Points[A + 1],
					Spans[B], Points[B], Points[B + 1], LineDiameter))
				{
					Spans[A].LaneOffset = LaneOffset(Spans[A].Direction);
					Spans[B].LaneOffset = LaneOffset(Spans[B].Direction);
				}
			}
		}
	}

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
		const TCHAR* Message = bEnable ? TEXT("NPC 巡逻线已开启；有可走路段的有效巡逻路线才会显示。")
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

void UCastlePatrolPathVisualizationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// 世界开始播放时子系统均已完成 PostInitialize，因此可以复用带完整门控和资产校验的正式开关入口。
	// 专用服务器没有渲染视图，沿用 SetPathDrawingEnabled 的既有限制，不启动巡逻线绘制。
	if (InWorld.GetNetMode() != NM_DedicatedServer)
	{
		// 开启失败时 SetPathDrawingEnabled 已通过 RejectDrawing 输出资产及失败原因，不用默认资源继续运行。
		SetPathDrawingEnabled(true);
	}
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
	if (!Path || !Path->bValid || (!Path->bClosedLoop && !Path->bTowerDispatch))
	{
		// 尚未发布、已失效和单段导航演示都没有可绘制的 NPC 巡逻轨迹。
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

	// 仍按 NPC 的贴地采样构建，不用示例样条重新走捷径；偏移只写入临时渲染顶点。
	const TArray<FVector>& Points = Path.PatrolPoints;
	const TArray<double>& Distances = Path.PatrolDistances;
	// 两类路线都必须保持采样点与累计距离一一对应，并从零距离起步；只有闭环才要求末点回到起点。
	if (Distances.Num() != Points.Num() || Distances[0] != 0.0)
	{
		return RejectDrawing(FString::Printf(TEXT("巡逻快照 %d 的累计距离数量或起点距离无效。"), Path.PathRevision));
	}
	if (Path.bClosedLoop && !Points.Last().Equals(Points[0], 0.01))
	{
		return RejectDrawing(FString::Printf(TEXT("巡逻闭环快照 %d 的采样首尾没有闭合。"), Path.PathRevision));
	}
	const int32 NumSpans = Points.Num() - 1;
	TArray<FPatrolRenderSpan> Spans;
	Spans.Reserve(NumSpans);
	FBox PathBounds(ForceInit);
	for (int32 Index = 0; Index < NumSpans; ++Index)
	{
		const FVector Delta = Points[Index + 1] - Points[Index];
		const double Length = Delta.Size();
		if (!FMath::IsFinite(Length) || Length <= UE_DOUBLE_SMALL_NUMBER || !FMath::IsFinite(Distances[Index + 1])
			|| !FMath::IsNearlyEqual(Distances[Index + 1] - Distances[Index], Length, 0.01))
		{
			return RejectDrawing(FString::Printf(TEXT("巡逻快照 %d 的第 %d 条采样线段或累计距离无效。"), Path.PathRevision, Index + 1));
		}
		const FBox Bounds = FBox(Points[Index], Points[Index]) + Points[Index + 1];
		Spans.Add({Delta / Length, Length, Bounds});
		PathBounds += Bounds;
	}

	const FVector Extent = LineMesh->GetBounds().BoxExtent;
	const double LineDiameter = 2.0 * FMath::Max(Extent.Y * DashCrossSection.X, Extent.Z * DashCrossSection.Y);
	SeparateReverseLanes(Points, PathBounds, LineDiameter, Spans);
	TArray<FVector> KnotOffsets;
	KnotOffsets.Reserve(Points.Num());
	if (Path.bClosedLoop)
	{
		// 闭环首尾共用同一偏移；相邻线段的偏移均值让侧向分道平滑通过采样接缝。
		for (int32 Index = 0; Index < NumSpans; ++Index)
		{
			KnotOffsets.Add(0.5 * (Spans[(Index + NumSpans - 1) % NumSpans].LaneOffset + Spans[Index].LaneOffset));
		}
		KnotOffsets.Add(FVector(KnotOffsets[0]));
	}
	else
	{
		// 主塔开环从门 A 到门 B，不把两端误认为相邻；仅对内部接缝平均前后段的显示偏移。
		KnotOffsets.Add(Spans[0].LaneOffset);
		for (int32 Index = 1; Index < NumSpans; ++Index)
		{
			KnotOffsets.Add(0.5 * (Spans[Index - 1].LaneOffset + Spans[Index].LaneOffset));
		}
		KnotOffsets.Add(Spans[NumSpans - 1].LaneOffset);
	}

	TArray<FDashSegment> Segments;
	Segments.Reserve(NumSpans);
	const auto AppendDash = [&](const FVector& Start, const FVector& End, double DistanceA, double DistanceB)
	{
		const FVector Delta = End - Start;
		const double Length = Delta.Size();
		if (!FMath::IsFinite(Length) || Length <= UE_DOUBLE_SMALL_NUMBER)
		{
			return RejectDrawing(FString::Printf(TEXT("巡逻快照 %d 在累计距离 %.2f cm 处生成了无效的显示线段。"), Path.PathRevision, DistanceA));
		}
		// 局部 X 轴负责网格形变，世界旋转可适配斜坡和梯子。相位仍用 NPC 行程距离，侧移处也不会重新起播。
		Segments.Add({Start + LineLift, FQuat::FindBetweenNormals(FVector::ForwardVector, Delta / Length), Length,
			static_cast<float>((DistanceB - DistanceA) / DashPeriodCm), static_cast<float>(FMath::Fmod(DistanceA / DashPeriodCm, 1.0))});
		return true;
	};
	for (int32 Index = 0; Index < NumSpans; ++Index)
	{
		const FVector Start = Points[Index] + KnotOffsets[Index];
		const FVector End = Points[Index + 1] + KnotOffsets[Index + 1];
		double EndOfFirstDash = Distances[Index + 1];
		FVector EndPointOfFirstDash = End;
		// 很短的折返可能整条去程只有一个采样段，两个折返点都必须接回中心；用显示中点撑开两条线。
		const bool bSplitAtLaneMidpoint = !Spans[Index].LaneOffset.IsZero()
			&& KnotOffsets[Index].IsNearlyZero() && KnotOffsets[Index + 1].IsNearlyZero();
		if (bSplitAtLaneMidpoint)
		{
			EndOfFirstDash = (Distances[Index] + Distances[Index + 1]) * 0.5;
			EndPointOfFirstDash = (Start + End) * 0.5 + Spans[Index].LaneOffset;
		}
		if (!AppendDash(Start, EndPointOfFirstDash, Distances[Index], EndOfFirstDash)
			|| (bSplitAtLaneMidpoint && !AppendDash(EndPointOfFirstDash, End, EndOfFirstDash, Distances[Index + 1])))
		{
			return false;
		}
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
