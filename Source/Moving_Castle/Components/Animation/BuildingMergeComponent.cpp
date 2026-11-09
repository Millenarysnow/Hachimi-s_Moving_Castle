#include "BuildingMergeComponent.h"

#include "AssemblyFlightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogBuildingMerge, Log, All);

namespace BuildingMergeMotion
{
	/** 零速起止让大量零件的交错运动保持连贯，卡顿跨帧也直接求出正确位置。 */
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
		for (AActor* Source : SourceActors)
		{
			Sources.Add(Source);
		}
		StartMerge(Sources);
	}
}

bool UBuildingMergeComponent::CaptureActor(AActor* Actor, const bool bTarget,
	const FTransform& OwnerTransform, FBox& Bounds, FString& Error)
{
	// 两套动画不能同时隐藏相同原件；明确指出冲突，避免漏掉已被飞入组件隐藏的零件。
	if (const UAssemblyFlightComponent* Flight = Actor->FindComponentByClass<UAssemblyFlightComponent>(); Flight && Flight->bIsPlaying)
	{
		Error = FString::Printf(TEXT("%s 正在播放 Assembly Flight，请先完成或停止装配动画。"), *Actor->GetName());
		return false;
	}
	TArray<UStaticMeshComponent*> Meshes;
	Actor->GetComponents<UStaticMeshComponent>(Meshes, false);
	const int32 PreviousCount = Parts.Num();
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (Visuals.Contains(Mesh) || !Mesh->IsVisible() || Mesh->bHiddenInGame
			|| (!ExcludedComponentTag.IsNone() && Mesh->ComponentHasTag(ExcludedComponentTag)))
		{
			continue;
		}
		if (!Mesh->IsRegistered() || !Mesh->GetStaticMesh() || Mesh->IsA<UInstancedStaticMeshComponent>()
			|| Mesh->IsSimulatingPhysics() || !Mesh->GetComponentTransform().IsValid()
			|| Mesh->GetComponentScale().GetAbsMin() <= UE_SMALL_NUMBER || !Mesh->Bounds.GetBox().IsValid)
		{
			Error = FString::Printf(TEXT("%s.%s 不是有效的独立静态网格零件；不支持 ISM/HISM、物理模拟或零缩放。"),
				*Actor->GetName(), *Mesh->GetName());
			return false;
		}

		FMergePart& Part = Parts.AddDefaulted_GetRef();
		Part.bTarget = bTarget;
		Part.Target = bTarget ? Mesh : nullptr;
		Part.InitialRelative = Mesh->GetComponentTransform().GetRelativeTransform(OwnerTransform);
		Part.MeshCenter = Mesh->GetComponentTransform().InverseTransformPosition(Mesh->Bounds.Origin);
		Part.StartCenter = OwnerTransform.InverseTransformPosition(Mesh->Bounds.Origin);
		// 顶层旧件先松开、底层新件先咬合；每件只错开少量时间，仍能看到整群零件同时运动。
		Part.Order = Mesh->Bounds.Origin.Z;
		Bounds += Mesh->Bounds.GetBox();

		UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient);
		Visuals.Add(Visual);
		Part.Visual = Visual;
		Visual->SetMobility(EComponentMobility::Movable);
		Visual->SetStaticMesh(Mesh->GetStaticMesh());
		for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
		{
			Visual->SetMaterial(Index, Mesh->GetMaterial(Index));
		}
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visual->SetGenerateOverlapEvents(false);
		Visual->SetCanEverAffectNavigation(false);
		Visual->SetCastShadow(Mesh->CastShadow);
		Visual->SetReceivesDecals(Mesh->bReceivesDecals);
		Visual->SetVisibility(false, false);
		Visual->RegisterComponent();
		if (!Visual->IsRegistered())
		{
			Error = TEXT("无法注册合并动画的临时零件。");
			return false;
		}
	}
	if (Parts.Num() == PreviousCount)
	{
		Error = FString::Printf(TEXT("%s 没有可参与合并的可见独立静态网格，请检查模型及排除标签。"), *Actor->GetName());
		return false;
	}
	return true;
}

bool UBuildingMergeComponent::StartMerge(const TArray<AActor*>& Sources)
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
		|| !FMath::IsFinite(SeparationRatio) || SeparationRatio < 0.0 || SeparationRatio > 0.5
		|| !FMath::IsFinite(SwirlAngle) || SwirlAngle < 0.0 || SwirlAngle > 360.0)
	{
		return FailMerge(TEXT("时长须为至少 0.1 秒的有限数；抬升须非负，拆散幅度须为 0..0.5，旋转角须为 0..360。"));
	}
	if (Sources.Num() < 2)
	{
		return FailMerge(TEXT("合并至少需要两个源 Actor。"));
	}
	TSet<AActor*> UniqueSources;
	for (AActor* Source : Sources)
	{
		if (!IsValid(Source) || Source == Owner || Source->GetWorld() != GetWorld() || Source->IsHidden()
			|| UniqueSources.Contains(Source))
		{
			return FailMerge(TEXT("源 Actor 必须有效、可见、与结果处于同一世界，且不能重复或包含结果自身。"));
		}
		UniqueSources.Add(Source);
	}

	const FTransform OwnerTransform = Owner->GetActorTransform();
	FBox Bounds(ForceInit);
	FString Error;
	for (AActor* Source : Sources)
	{
		if (!CaptureActor(Source, false, OwnerTransform, Bounds, Error))
		{
			return FailMerge(Error);
		}
	}
	SourcePartCount = Parts.Num();
	if (!CaptureActor(Owner, true, OwnerTransform, Bounds, Error))
	{
		return FailMerge(Error);
	}
	TargetPartCount = Parts.Num() - SourcePartCount;
	ActiveDuration = Duration;
	ActiveSwirlAngle = FMath::DegreesToRadians(SwirlAngle);
	const FVector WorldCore = Bounds.GetCenter();
	CoreCenter = OwnerTransform.InverseTransformPosition(WorldCore);
	LiftOffset = OwnerTransform.InverseTransformVector(FVector(0.0, 0.0, LiftHeight));
	const double Radius = Bounds.GetExtent().Size2D();

	// 所有几何位置都转换到结果 Actor 空间；城堡整体移动、转向或缩放时路径随之运动。
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		FMergePart& Part = Parts[Index];
		const double Height = FMath::Clamp((Part.Order - Bounds.Min.Z) / FMath::Max(Bounds.GetSize().Z, 1.0), 0.0, 1.0);
		const double Angle = Index * 2.399963229728653;
		const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
		Part.Tilt = FRotator(25.0 * FMath::Cos(Angle), 42.0 * FMath::Sin(Angle), 18.0 * FMath::Sin(Angle * 2.0)).Quaternion();
		if (Part.bTarget)
		{
			Part.Delay = ActiveDuration * (0.42 + Height * 0.18);
			Part.ScatterCenter = CoreCenter + OwnerTransform.InverseTransformVector(
				Radial * Radius * 0.075 + FVector(0.0, 0.0, (Height - 0.5) * LiftHeight * 0.5));
		}
		else
		{
			Part.Delay = ActiveDuration * (0.05 + (1.0 - Height) * 0.10);
			const FVector WorldCenter = OwnerTransform.TransformPosition(Part.StartCenter);
			FVector Outward = WorldCenter - WorldCore;
			Outward.Z = 0.0;
			Outward = Outward.GetSafeNormal();
			Part.ScatterCenter = Part.StartCenter + OwnerTransform.InverseTransformVector(
				Outward * Radius * SeparationRatio + Radial * Radius * SeparationRatio * 0.35)
				+ LiftOffset * (0.65 + 0.35 * Height);
		}
	}

	// 全部副本创建成功后才修改原件。结果碰撞保持有效，合并中仍能被选中、回收或继续升级。
	TArray<UPrimitiveComponent*> Primitives;
	Owner->GetComponents<UPrimitiveComponent>(Primitives, false);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (!Visuals.Contains(Cast<UStaticMeshComponent>(Primitive)))
		{
			TargetStates.Add({ Primitive, Primitive->IsVisible() });
			Primitive->SetVisibility(false, false);
		}
	}
	for (AActor* Source : Sources)
	{
		SourceStates.Add({ Source, Source->IsHidden(), Source->GetActorEnableCollision() });
		Source->SetActorHiddenInGame(true);
		Source->SetActorEnableCollision(false);
	}
	bCaptured = true;
	bIsPlaying = true;
	SetComponentTickEnabled(true);
	for (FMergePart& Part : Parts)
	{
		UpdatePart(Part);
	}
	const FString Message = FString::Printf(TEXT("零件合并开始：%d 件旧零件 → %d 件新零件，%.1f 秒。"), SourcePartCount, TargetPartCount, ActiveDuration);
	UE_LOG(LogBuildingMerge, Display, TEXT("%s [%s]"), *Message, *Owner->GetName());
	ShowStatus(Message, FColor::Cyan);
	return true;
}

void UBuildingMergeComponent::UpdatePart(FMergePart& Part)
{
	if (Part.bFinished)
	{
		return;
	}
	const FTransform OwnerTransform = GetOwner()->GetActorTransform();
	FTransform Anchor = Part.InitialRelative * OwnerTransform;
	if (Part.bTarget)
	{
		Anchor = Part.Target->GetComponentTransform();
	}
	const double Age = ElapsedTime - Part.Delay;
	FVector Center;
	FQuat Rotation;
	double Scale = 1.0;
	if (Part.bTarget)
	{
		if (Age < 0.0)
		{
			Part.Visual->SetVisibility(false, false);
			return;
		}
		const double U = FMath::Clamp(Age / (ActiveDuration * 0.40), 0.0, 1.0);
		if (U >= 1.0)
		{
			// 精确收尾使用原组件，任何弹性运动误差都不会写回正式建筑。
			const FPrimitiveState* State = TargetStates.FindByPredicate([&Part](const FPrimitiveState& Item)
			{
				return Item.Primitive == Part.Target;
			});
			Part.Target->SetVisibility(State && State->bVisible, false);
			Part.Visual->DestroyComponent();
			Part.bFinished = true;
			++CompletedPartCount;
			return;
		}
		const double Eased = BuildingMergeMotion::Smooth(U);
		const FVector Goal = Anchor.TransformPosition(Part.MeshCenter);
		const FVector Start = OwnerTransform.TransformPosition(Part.ScatterCenter);
		const FVector Offset = Goal - Start;
		const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Offset) * 0.18;
		Center = FMath::Lerp(Start, Goal, Eased) + (Tangent + OwnerTransform.TransformVector(LiftOffset) * 0.35) * FMath::Sin(UE_PI * U);
		const FQuat Tilt = OwnerTransform.GetRotation() * Part.Tilt * OwnerTransform.GetRotation().Inverse();
		Rotation = FQuat::Slerp(Tilt * Anchor.GetRotation(), Anchor.GetRotation(), Eased).GetNormalized();
		// 每件独立轻微过冲并回弹，像榫卯咬合；不是对整栋建筑做统一缩放。
		Scale = FMath::Lerp(0.12, 1.0, Eased) + 0.045 * FMath::Sin(UE_PI * U) * FMath::Sin(UE_PI * U * 2.0);
	}
	else
	{
		const double Detach = BuildingMergeMotion::Smooth(Age / (ActiveDuration * 0.18));
		const double U = FMath::Clamp((Age - ActiveDuration * 0.18) / (ActiveDuration * 0.23), 0.0, 1.0);
		if (U >= 1.0)
		{
			Part.Visual->DestroyComponent();
			Part.bFinished = true;
			return;
		}
		const FVector Detached = FMath::Lerp(Part.StartCenter, Part.ScatterCenter, Detach);
		const double Eased = BuildingMergeMotion::Smooth(U);
		const FQuat Swirl(FVector::UpVector, ActiveSwirlAngle * Eased);
		const FVector WorldCore = OwnerTransform.TransformPosition(CoreCenter);
		const FVector Radial = OwnerTransform.TransformVector(Detached - CoreCenter);
		Center = WorldCore + Swirl.RotateVector(Radial) * (1.0 - Eased)
			+ OwnerTransform.TransformVector(LiftOffset) * FMath::Sin(UE_PI * U) * 0.35;
		const FQuat Tilt = OwnerTransform.GetRotation() * Part.Tilt * OwnerTransform.GetRotation().Inverse();
		Rotation = (Swirl * FQuat::Slerp(FQuat::Identity, Tilt, Detach) * Anchor.GetRotation()).GetNormalized();
		Scale = 1.0 - BuildingMergeMotion::Smooth((U - 0.45) / 0.55) * 0.98;
	}

	// 平移减去旋转缩放后的几何中心偏移，确保共享原点的屋檐、墙板也绕自身中心运动。
	const FVector VisualScale = Anchor.GetScale3D() * Scale;
	const FVector PivotOffset = Rotation.RotateVector(VisualScale * Part.MeshCenter);
	Part.Visual->SetWorldTransform(FTransform(Rotation, Center - PivotOffset, VisualScale),
		false, nullptr, ETeleportType::TeleportPhysics);
	Part.Visual->SetVisibility(true, false);
}

void UBuildingMergeComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bIsPlaying)
	{
		return;
	}
	if (!FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f || !GetOwner()->GetActorTransform().IsValid()
		|| GetOwner()->GetActorScale3D().GetAbsMin() <= UE_SMALL_NUMBER)
	{
		FailMerge(TEXT("运行中的时间步长或结果 Actor 变换无效。"));
		return;
	}
	ElapsedTime += DeltaTime;
	for (FMergePart& Part : Parts)
	{
		if (Part.bTarget && !Part.Target.IsValid())
		{
			FailMerge(TEXT("合并期间目标网格被外部销毁，已取消动画并恢复仍存在的原件。"));
			return;
		}
		UpdatePart(Part);
	}
	if (ElapsedTime >= ActiveDuration)
	{
		CompleteMerge();
	}
}

void UBuildingMergeComponent::RestoreTarget()
{
	for (const FPrimitiveState& State : TargetStates)
	{
		if (UPrimitiveComponent* Primitive = State.Primitive.Get())
		{
			Primitive->SetVisibility(State.bVisible, false);
		}
	}
}

void UBuildingMergeComponent::DestroyVisuals()
{
	for (UStaticMeshComponent* Visual : Visuals)
	{
		if (IsValid(Visual))
		{
			Visual->DestroyComponent();
		}
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
	UE_LOG(LogBuildingMerge, Display, TEXT("零件合并完成：%d 件新零件全部归位。[%s]"), TargetPartCount, *GetOwner()->GetName());
	ShowStatus(TEXT("合并完成：新建筑零件全部归位。"), FColor::Green);
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
	SourcePartCount = TargetPartCount = CompletedPartCount = 0;
	ElapsedTime = 0.0;
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
