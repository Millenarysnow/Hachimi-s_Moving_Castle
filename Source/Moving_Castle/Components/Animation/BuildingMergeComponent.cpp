#include "BuildingMergeComponent.h"

#include "AssemblyFlightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Matching/BuildingPartCatalog.h"
#include "Matching/BuildingPartMatcher.h"

DEFINE_LOG_CATEGORY_STATIC(LogBuildingMerge, Log, All);

namespace BuildingMergeMotion
{
	/** 零速起止用于短距离结构调整，不产生整栋弹跳或围绕公共中心旋转。 */
	double Smooth(const double Value)
	{
		const double U = FMath::Clamp(Value, 0.0, 1.0);
		return U * U * (3.0 - 2.0 * U);
	}
}

UBuildingMergeComponent::UBuildingMergeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UBuildingMergeComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoPlay)
	{
		TArray<AActor*> Sources;
		for (AActor* Source : SourceActors) { Sources.Add(Source); }
		StartChange(Sources);
	}
}

bool UBuildingMergeComponent::CaptureActor(AActor* Actor, TArray<FPartSnapshot>& OutParts, FString& Error) const
{
	if (const UAssemblyFlightComponent* Flight = Actor->FindComponentByClass<UAssemblyFlightComponent>(); Flight && Flight->bIsPlaying)
	{
		Error = FString::Printf(TEXT("%s 正在播放 Assembly Flight，请先完成或停止该动画。"), *Actor->GetName());
		return false;
	}
	TArray<UStaticMeshComponent*> Meshes;
	Actor->GetComponents<UStaticMeshComponent>(Meshes, false);
	const UBuildingMergeComponent* PreviousTransition = Actor->FindComponentByClass<UBuildingMergeComponent>();
	if (PreviousTransition == this || !PreviousTransition || !PreviousTransition->bIsPlaying)
	{
		PreviousTransition = nullptr;
	}
	Meshes.Sort([](const UStaticMeshComponent& A, const UStaticMeshComponent& B) { return A.GetName() < B.GetName(); });
	const int32 BeginIndex = OutParts.Num();
	double BaseHeight = TNumericLimits<double>::Max();
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		const FMergePart* PendingPart = PreviousTransition
			? PreviousTransition->Parts.FindByPredicate([Mesh](const FMergePart& Part)
			{
				return Part.Visual.Get() == Mesh && !Part.bFinished && Part.Motion == EPartMotion::Arrive;
			}) : nullptr;
		const bool bPendingArrival = PendingPart && PreviousTransition->ElapsedTime < PendingPart->Delay;
		if ((!Mesh->IsVisible() && !bPendingArrival) || Mesh->bHiddenInGame
			|| (!ExcludedComponentTag.IsNone() && Mesh->ComponentHasTag(ExcludedComponentTag)))
		{
			continue;
		}
		if (!Mesh->IsRegistered() || !Mesh->GetStaticMesh() || Mesh->IsA<UInstancedStaticMeshComponent>()
			|| Mesh->IsSimulatingPhysics() || !Mesh->GetComponentTransform().IsValid()
			|| Mesh->GetComponentScale().GetAbsMin() <= UE_SMALL_NUMBER || !Mesh->Bounds.GetBox().IsValid
			|| Mesh->GetStaticMesh()->GetBoundingBox().GetSize().GetMax() <= UE_SMALL_NUMBER)
		{
			Error = FString::Printf(TEXT("%s.%s 不是有效的独立静态网格；不支持 ISM/HISM、物理模拟或零缩放。"),
				*Actor->GetName(), *Mesh->GetName());
			return false;
		}
		FPartSnapshot& Part = OutParts.AddDefaulted_GetRef();
		Part.Mesh = Mesh;
		Part.WorldTransform = bPendingArrival
			? PendingPart->InitialRelative * Actor->GetActorTransform() : Mesh->GetComponentTransform();
		const FBoxSphereBounds CapturedBounds = bPendingArrival
			? Mesh->GetStaticMesh()->GetBounds().TransformBy(Part.WorldTransform) : Mesh->Bounds;
		Part.Center = CapturedBounds.Origin;
		Part.Radius = CapturedBounds.SphereRadius;
		Part.LocalCenter = Part.WorldTransform.InverseTransformPosition(Part.Center);
		Part.LocalSize = Mesh->GetStaticMesh()->GetBoundingBox().GetSize();
		Part.bIncoming = Actor == IncomingSourceActor;
		Part.bPendingArrival = bPendingArrival;
		Part.RemainingDelay = bPendingArrival ? PendingPart->Delay - PreviousTransition->ElapsedTime : 0.0;
		BaseHeight = FMath::Min(BaseHeight, CapturedBounds.GetBox().Min.Z);
		const FBuildingPartGeometry* Geometry = PartCatalog ? PartCatalog->FindGeometry(Mesh->GetStaticMesh()) : nullptr;
		if (Geometry && Geometry->GeometryId.IsEmpty())
		{
			Error = FString::Printf(TEXT("%s 的目录几何身份为空，请重建目录。"), *Mesh->GetName());
			return false;
		}
		Part.ShapeIdentity = Geometry ? TEXT("Geometry:") + Geometry->GeometryId
			: TEXT("Asset:") + Mesh->GetStaticMesh()->GetPathName();

		// 明确身份优先，适合几何接近但有不同用途的门框/窗框；重复身份仍按最小移动分配。
		FString ExplicitIdentity;
		for (const FName Tag : Mesh->ComponentTags)
		{
			const FString Text = Tag.ToString();
			if (Text.StartsWith(TEXT("MergeIdentity=")))
			{
				if (!ExplicitIdentity.IsEmpty() || Text.Len() == 14)
				{
					Error = FString::Printf(TEXT("%s 的 MergeIdentity 标签为空或重复。"), *Mesh->GetName());
					return false;
				}
				ExplicitIdentity = Text.Mid(14);
			}
		}
		if (!ExplicitIdentity.IsEmpty())
		{
			Part.Identity = TEXT("Role:") + ExplicitIdentity;
		}
		else
		{
			// 没有跨资产几何证据时只承认同一资产，不通过组件序号或包围盒猜测设计身份。
			Part.Identity = Part.ShapeIdentity;
		}
	}
	if (BeginIndex == OutParts.Num())
	{
		Error = FString::Printf(TEXT("%s 没有可见的独立静态网格零件。"), *Actor->GetName());
		return false;
	}
	for (int32 Index = BeginIndex; Index < OutParts.Num(); ++Index) { OutParts[Index].BaseHeight = BaseHeight; }
	return true;
}

bool UBuildingMergeComponent::CreateVisual(FMergePart& Part, UStaticMeshComponent* Template)
{
	UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient);
	Visuals.Add(Visual);
	Part.Visual = Visual;
	Visual->SetMobility(EComponentMobility::Movable);
	Visual->SetStaticMesh(Template->GetStaticMesh());
	// 复制身份标签，让下一次变化能从当前飞行姿态接续，而不是重新出现完整的中间建筑。
	Visual->ComponentTags = Template->ComponentTags;
	for (int32 Index = 0; Index < Template->GetNumMaterials(); ++Index)
	{
		Visual->SetMaterial(Index, Template->GetMaterial(Index));
	}
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetGenerateOverlapEvents(false);
	Visual->SetCanEverAffectNavigation(false);
	Visual->SetCastShadow(Template->CastShadow);
	Visual->SetReceivesDecals(Template->bReceivesDecals);
	Visual->SetVisibility(false, false);
	Visual->RegisterComponent();
	return Visual->IsRegistered();
}

bool UBuildingMergeComponent::BuildPlan(const TArray<FPartSnapshot>& Sources,
	const TArray<FPartSnapshot>& Targets, const FVector& Origin, FString& Error)
{
	TArray<FBuildingPartMatchPoint> SourcePoints;
	TArray<FBuildingPartMatchPoint> TargetPoints;
	for (const FPartSnapshot& Part : Sources)
	{
		SourcePoints.Add({Part.Identity, Part.Center, Part.WorldTransform.GetRotation(), Part.Radius});
	}
	for (const FPartSnapshot& Part : Targets)
	{
		TargetPoints.Add({Part.Identity, Part.Center, Part.WorldTransform.GetRotation(), Part.Radius});
	}
	const TArray<int32> Correspondence = BuildingPartMatcher::Match(SourcePoints, TargetPoints);
	TSet<int32> UsedSources;
	const FTransform OwnerTransform = GetOwner()->GetActorTransform();
	const FVector Direction = ArrivalDirection.GetSafeNormal();
	UpOwnerSpace = OwnerTransform.GetRotation().UnrotateVector(FVector::UpVector);

	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		const FPartSnapshot& Target = Targets[Index];
		const int32 SourceIndex = Correspondence[Index];
		const FPartSnapshot* Source = SourceIndex == INDEX_NONE ? nullptr : &Sources[SourceIndex];
		FMergePart& Part = Parts.AddDefaulted_GetRef();
		Part.Target = Target.Mesh;
		Part.MeshCenter = Target.LocalCenter;
		Part.TargetMeshCenter = Target.LocalCenter;
		Part.Delay = FVector::Distance(Origin, Target.Center) / PropagationSpeed;
		FVector StartCenter = Target.Center;
		FQuat StartRotation = Target.WorldTransform.GetRotation();
		FVector StartScale = Target.WorldTransform.GetScale3D();
		UStaticMeshComponent* VisualTemplate = Target.Mesh;
		if (Source)
		{
			UsedSources.Add(SourceIndex);
			++ReusedPartCount;
			StartCenter = Source->Center;
			StartRotation = Source->WorldTransform.GetRotation();
			// 真正沿用旧网格飞到新位置；只在归位时换成目标原件。导入单位通过尺寸比例换算。
			Part.MeshCenter = Source->LocalCenter;
			Part.TargetUnitRatio = Target.LocalSize.GetMax() / Source->LocalSize.GetMax();
			VisualTemplate = Source->Mesh;
			StartScale = Source->WorldTransform.GetScale3D();
			bool bSameMaterials = Source->Mesh->GetNumMaterials() == Target.Mesh->GetNumMaterials();
			for (int32 Material = 0; bSameMaterials && Material < Target.Mesh->GetNumMaterials(); ++Material)
			{
				bSameMaterials = Source->Mesh->GetMaterial(Material) == Target.Mesh->GetMaterial(Material);
			}
			const bool bSamePose = FVector::Distance(StartCenter, Target.Center) <= KeepPositionTolerance
				&& StartRotation.AngularDistance(Target.WorldTransform.GetRotation()) <= FMath::DegreesToRadians(0.2)
				&& (Source->LocalSize * StartScale).Equals(Target.LocalSize * Target.WorldTransform.GetScale3D(), KeepPositionTolerance)
				&& Source->ShapeIdentity == Target.ShapeIdentity && bSameMaterials;
			if (!Source->bIncoming && !Source->bPendingArrival && bSamePose)
			{
				Part.Motion = EPartMotion::Keep;
				Part.bFinished = true;
				++UnchangedPartCount;
				++CompletedPartCount;
				continue;
			}
			Part.Motion = Source->bIncoming || Source->bPendingArrival ? EPartMotion::Arrive : EPartMotion::Move;
		}
		else
		{
			Part.Motion = EPartMotion::Arrive;
			++AddedPartCount;
		}
		if (Part.Motion == EPartMotion::Arrive)
		{
			++IncomingPartCount;
			if (Source && Source->bPendingArrival)
			{
				// 接续上轮尚未出现的零件，沿用原来的起飞姿态及剩余等待，不再次向外偏移。
				Part.Delay = FMath::Max(Part.Delay, Source->RemainingDelay);
			}
			else
			{
				StartCenter += Direction * FlightDistance;
				const double BaseHeight = Source ? Source->BaseHeight : Target.BaseHeight;
				StartCenter.Z = BaseHeight + Target.Mesh->Bounds.BoxExtent.Z + 30.0;
				const double Angle = Index * 2.399963229728653;
				StartCenter += FVector(-Direction.Y, Direction.X, 0.0) * FMath::Sin(Angle) * 35.0;
				StartRotation = (FRotator(12.0 * FMath::Cos(Angle), 9.0 * FMath::Sin(Angle), 8.0 * FMath::Sin(Angle)).Quaternion() * StartRotation).GetNormalized();
			}
			Part.PlayDuration = Duration;
			Part.ArcHeight = LiftHeight;
			// 屋顶稍晚于底部发射；真实距离传播仍共用操作原点，不按各栋建筑分别起一轮。
			if (!Source || !Source->bPendingArrival)
			{
				Part.Delay += FMath::Clamp((Target.Center.Z - Target.BaseHeight) / 1000.0, 0.0, 1.0) * Duration * 0.18;
			}
		}
		else
		{
			++MovedPartCount;
			Part.PlayDuration = Duration * 0.55;
			Part.ArcHeight = FMath::Min(LiftHeight, FVector::Distance(StartCenter, Target.Center) * 0.18);
		}
		const FVector StartPivot = StartCenter - StartRotation.RotateVector(StartScale * Part.MeshCenter);
		Part.InitialRelative = FTransform(StartRotation, StartPivot, StartScale).GetRelativeTransform(OwnerTransform);
		TotalDuration = FMath::Max(TotalDuration, Part.Delay + Part.PlayDuration);
		if (!CreateVisual(Part, VisualTemplate))
		{
			Error = TEXT("无法创建局部变化的网格副本。");
			return false;
		}
	}
	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		if (UsedSources.Contains(Index)) { continue; }
		const FPartSnapshot& Source = Sources[Index];
		FMergePart& Part = Parts.AddDefaulted_GetRef();
		Part.Motion = EPartMotion::Depart;
		Part.MeshCenter = Source.LocalCenter;
		Part.InitialRelative = Source.WorldTransform.GetRelativeTransform(OwnerTransform);
		Part.EndCenter = OwnerTransform.InverseTransformPosition(Source.Center + Direction * FlightDistance * 0.25 + FVector::UpVector * LiftHeight * 0.4);
		Part.Delay = FVector::Distance(Origin, Source.Center) / PropagationSpeed;
		Part.PlayDuration = Duration * 0.5;
		Part.ArcHeight = LiftHeight * 0.25;
		++RemovedPartCount;
		TotalDuration = FMath::Max(TotalDuration, Part.Delay + Part.PlayDuration);
		if (!CreateVisual(Part, Source.Mesh))
		{
			Error = TEXT("无法创建移除零件的网格副本。");
			return false;
		}
	}
	return true;
}

bool UBuildingMergeComponent::StartMerge(const TArray<AActor*>& Sources)
{
	if (Sources.Num() < 2) { return FailMerge(TEXT("合并至少需要两个源 Actor。")); }
	return StartChange(Sources);
}

bool UBuildingMergeComponent::StartChange(const TArray<AActor*>& Sources)
{
	ResetMerge();
	LastError.Reset();
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !GetWorld() || !GetWorld()->IsGameWorld() || Owner->IsHidden()
		|| !Owner->GetActorTransform().IsValid() || Owner->GetActorScale3D().GetAbsMin() <= UE_SMALL_NUMBER)
	{
		return FailMerge(TEXT("合并结果必须是游戏世界中可见且变换有效的 Actor。"));
	}
	if (!FMath::IsFinite(Duration) || Duration < 0.1 || !FMath::IsFinite(LiftHeight) || LiftHeight < 0.0
		|| !FMath::IsFinite(FlightDistance) || FlightDistance < 0.0 || ArrivalDirection.ContainsNaN() || ArrivalDirection.IsNearlyZero()
		|| !FMath::IsFinite(KeepPositionTolerance) || KeepPositionTolerance < 0.0
		|| !FMath::IsFinite(PropagationSpeed) || PropagationSpeed <= 0.0
		|| (bUseSharedChangeOrigin && ChangeOrigin.ContainsNaN()))
	{
		return FailMerge(TEXT("动画时长、传播速度须为有限正数；尺寸/容差须非负，来向须为有限非零向量。"));
	}
	if (Sources.IsEmpty()) { return FailMerge(TEXT("局部变化至少需要一个源 Actor。")); }
	TSet<AActor*> UniqueSources;
	TArray<FPartSnapshot> SourceParts;
	TArray<FPartSnapshot> TargetParts;
	FString Error;
	for (AActor* Source : Sources)
	{
		if (!IsValid(Source) || Source == Owner || Source->GetWorld() != GetWorld() || Source->IsHidden() || UniqueSources.Contains(Source))
		{
			return FailMerge(TEXT("来源必须有效、可见、在同一世界且不重复，不能包含结果自身。"));
		}
		UniqueSources.Add(Source);
		if (!CaptureActor(Source, SourceParts, Error)) { return FailMerge(Error); }
	}
	if (IncomingSourceActor && !UniqueSources.Contains(IncomingSourceActor))
	{
		return FailMerge(TEXT("Incoming Source Actor 必须属于本轮有效来源。"));
	}
	if (!CaptureActor(Owner, TargetParts, Error)) { return FailMerge(Error); }
	SourcePartCount = SourceParts.Num();
	TargetPartCount = TargetParts.Num();
	const FVector Origin = bUseSharedChangeOrigin ? ChangeOrigin
		: (IncomingSourceActor ? IncomingSourceActor->GetActorLocation() : Owner->GetActorLocation());
	if (!BuildPlan(SourceParts, TargetParts, Origin, Error)) { return FailMerge(Error); }

	// 所有计划和副本就绪后才提交显隐。结果碰撞始终有效，不阻断相邻放置、回收或再次升级。
	TArray<UPrimitiveComponent*> Primitives;
	Owner->GetComponents<UPrimitiveComponent>(Primitives, false);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (!Visuals.Contains(Cast<UStaticMeshComponent>(Primitive)))
		{
			TargetStates.Add({Primitive, Primitive->IsVisible()});
			Primitive->SetVisibility(false, false);
		}
	}
	for (AActor* Source : Sources)
	{
		SourceStates.Add({Source, Source->IsHidden(), Source->GetActorEnableCollision()});
		Source->SetActorHiddenInGame(true);
		Source->SetActorEnableCollision(false);
	}
	bCaptured = true;
	for (FMergePart& Part : Parts)
	{
		if (Part.Motion == EPartMotion::Keep) { Part.Target->SetVisibility(true, false); }
		else { UpdatePart(Part); }
	}
	bIsPlaying = true;
	SetComponentTickEnabled(true);
	const FString Message = FString::Printf(TEXT("局部变化：复用 %d（原位 %d、移动 %d），飞入 %d，新增 %d，移除 %d。"),
		ReusedPartCount, UnchangedPartCount, MovedPartCount, IncomingPartCount, AddedPartCount, RemovedPartCount);
	UE_LOG(LogBuildingMerge, Display, TEXT("%s [%s]"), *Message, *Owner->GetName());
	ShowStatus(Message, FColor::Cyan);
	if (TotalDuration <= 0.0) { CompleteMerge(); }
	return true;
}

void UBuildingMergeComponent::UpdatePart(FMergePart& Part)
{
	if (Part.bFinished) { return; }
	const FTransform OwnerTransform = GetOwner()->GetActorTransform();
	const FTransform Start = Part.InitialRelative * OwnerTransform;
	const double Age = ElapsedTime - Part.Delay;
	const double U = FMath::Clamp(Age / Part.PlayDuration, 0.0, 1.0);
	if (Age < 0.0 && Part.Motion == EPartMotion::Arrive)
	{
		Part.Visual->SetVisibility(false, false);
		return;
	}
	if (U >= 1.0)
	{
		if (Part.Target.IsValid())
		{
			Part.Target->SetVisibility(true, false);
			++CompletedPartCount;
		}
		Part.Visual->DestroyComponent();
		Part.bFinished = true;
		return;
	}
	const double Eased = BuildingMergeMotion::Smooth(U);
	const FVector StartCenter = Start.TransformPosition(Part.MeshCenter);
	FVector Goal = OwnerTransform.TransformPosition(Part.EndCenter);
	FQuat Rotation = Start.GetRotation();
	FVector Scale = Start.GetScale3D();
	if (Part.Target.IsValid())
	{
		const FTransform Target = Part.Target->GetComponentTransform();
		Goal = Target.TransformPosition(Part.TargetMeshCenter);
		Rotation = FQuat::Slerp(Start.GetRotation(), Target.GetRotation(), Eased).GetNormalized();
		Scale = FMath::Lerp(Start.GetScale3D(), Target.GetScale3D() * Part.TargetUnitRatio, Eased);
	}
	else
	{
		// 只有真正多余的旧件离场；保持附近的保留件可见，不抽走整栋建筑。
		Scale *= 1.0 - BuildingMergeMotion::Smooth((U - 0.4) / 0.6) * 0.98;
	}
	const FVector Up = OwnerTransform.GetRotation().RotateVector(UpOwnerSpace);
	const FVector Center = FMath::Lerp(StartCenter, Goal, Eased) + Up * Part.ArcHeight * FMath::Sin(UE_PI * U);
	const FVector Pivot = Center - Rotation.RotateVector(Scale * Part.MeshCenter);
	Part.Visual->SetWorldTransform(FTransform(Rotation, Pivot, Scale), false, nullptr, ETeleportType::TeleportPhysics);
	Part.Visual->SetVisibility(true, false);
}

void UBuildingMergeComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bIsPlaying) { return; }
	if (!FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f || !GetOwner()->GetActorTransform().IsValid()
		|| GetOwner()->GetActorScale3D().GetAbsMin() <= UE_SMALL_NUMBER)
	{
		FailMerge(TEXT("运行中的时间步长或结果 Actor 变换无效。"));
		return;
	}
	ElapsedTime += DeltaTime;
	for (FMergePart& Part : Parts)
	{
		if (Part.Motion != EPartMotion::Depart && !Part.Target.IsValid())
		{
			FailMerge(TEXT("目标零件被外部销毁，已取消变化并恢复仍存在的原件。"));
			return;
		}
		UpdatePart(Part);
	}
	if (ElapsedTime >= TotalDuration) { CompleteMerge(); }
}

void UBuildingMergeComponent::RestoreTarget()
{
	for (const FPrimitiveState& State : TargetStates)
	{
		if (UPrimitiveComponent* Primitive = State.Primitive.Get()) { Primitive->SetVisibility(State.bVisible, false); }
	}
}

void UBuildingMergeComponent::DestroyVisuals()
{
	for (UStaticMeshComponent* Visual : Visuals)
	{
		if (IsValid(Visual)) { Visual->DestroyComponent(); }
	}
	Visuals.Reset();
}

void UBuildingMergeComponent::CompleteMerge()
{
	RestoreTarget();
	DestroyVisuals();
	bIsPlaying = false;
	SetComponentTickEnabled(false);
	CompletedPartCount = TargetPartCount;
	UE_LOG(LogBuildingMerge, Display, TEXT("局部变化完成：%d 件归位，%d 件始终原位保留。[%s]"),
		TargetPartCount, UnchangedPartCount, *GetOwner()->GetName());
	ShowStatus(TEXT("局部变化完成：新建筑零件全部归位。"), FColor::Green);
	OnMergeCompleted.Broadcast();
}

void UBuildingMergeComponent::ResetMerge()
{
	bIsPlaying = false;
	SetComponentTickEnabled(false);
	if (bCaptured)
	{
		RestoreTarget();
		for (const FSourceState& State : SourceStates)
		{
			if (AActor* Actor = State.Actor.Get())
			{
				Actor->SetActorHiddenInGame(State.bHidden);
				Actor->SetActorEnableCollision(State.bCollision);
			}
		}
	}
	DestroyVisuals();
	Parts.Reset();
	TargetStates.Reset();
	SourceStates.Reset();
	bCaptured = false;
	SourcePartCount = TargetPartCount = ReusedPartCount = UnchangedPartCount = MovedPartCount = IncomingPartCount = 0;
	AddedPartCount = RemovedPartCount = CompletedPartCount = 0;
	ElapsedTime = TotalDuration = 0.0;
}

bool UBuildingMergeComponent::FailMerge(const FString& Error)
{
	ResetMerge();
	LastError = Error;
	UE_LOG(LogBuildingMerge, Error, TEXT("零件合并失败：%s"), *Error);
	ShowStatus(FString::Printf(TEXT("零件合并失败：%s"), *Error), FColor::Red);
	return false;
}

void UBuildingMergeComponent::ShowStatus(const FString& Message, const FColor& Color) const
{
	if (bShowStatusOnScreen && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(reinterpret_cast<uint64>(this), 4.0f, Color, Message);
	}
}

void UBuildingMergeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetMerge();
	Super::EndPlay(EndPlayReason);
}
