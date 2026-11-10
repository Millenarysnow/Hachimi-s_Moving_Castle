#include "AssemblyFlightComponent.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssemblyFlight, Log, All);

UAssemblyFlightComponent::UAssemblyFlightComponent()
{
	// 独立组件自己管理 Tick，不抢占所属 Actor 的 Tick，也不要求任何蓝图连线。
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UAssemblyFlightComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoPlay)
	{
		StartAssembly();
	}
}

bool UAssemblyFlightComponent::CaptureParts(FString& OutError)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !GetWorld() || !GetWorld()->IsGameWorld())
	{
		OutError = TEXT("只能在游戏世界中播放，并且组件必须有有效的所属 Actor。");
		return false;
	}

	// 元数据只约束编辑器输入；代码调用仍须显式验证 NaN、无穷和零值。
	if (!FMath::IsFinite(FlightDuration) || FlightDuration <= 0.0
		|| !FMath::IsFinite(LaunchInterval) || LaunchInterval <= 0.0
		|| !FMath::IsFinite(FlightDistance) || FlightDistance <= 0.0
		|| !FMath::IsFinite(FlightArcHeight) || FlightArcHeight < 0.0
		|| !FMath::IsFinite(StartDelay) || StartDelay < 0.0)
	{
		OutError = TEXT("单件时长、发射间隔和飞入距离必须为有限正数；弧高和开始等待必须为有限非负数。");
		return false;
	}

	const FTransform OwnerTransform = Owner->GetActorTransform();
	if (!OwnerTransform.IsValid() || OwnerTransform.GetScale3D().GetAbsMin() <= UE_SMALL_NUMBER)
	{
		OutError = TEXT("所属 Actor 的变换无效或缩放为零，无法计算飞入坐标。");
		return false;
	}

	TArray<UStaticMeshComponent*> Meshes;
	Owner->GetComponents<UStaticMeshComponent>(Meshes, false);
	FBox WorldBounds(ForceInit);
	Parts.Reset();

	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (!ExcludedComponentTag.IsNone() && Mesh->ComponentHasTag(ExcludedComponentTag))
		{
			continue;
		}
		if (!PartComponentTag.IsNone() && !Mesh->ComponentHasTag(PartComponentTag))
		{
			continue;
		}

		// 原本不可见的辅助网格不属于本次可见建筑；不临时显示它们，也不改变其状态。
		if (!Mesh->IsVisible() || Mesh->bHiddenInGame)
		{
			continue;
		}

		if (!Mesh->IsRegistered() || !Mesh->GetStaticMesh() || Mesh->IsA<UInstancedStaticMeshComponent>() || Mesh->IsSimulatingPhysics())
		{
			OutError = FString::Printf(TEXT("零件 %s 必须是已注册、有网格、关闭物理模拟的独立 StaticMeshComponent；不支持 ISM/HISM。"), *Mesh->GetName());
			return false;
		}

		const FTransform Target = Mesh->GetComponentTransform();
		if (!Target.IsValid() || Target.GetScale3D().GetAbsMin() <= UE_SMALL_NUMBER || !Mesh->Bounds.GetBox().IsValid)
		{
			OutError = FString::Printf(TEXT("零件 %s 的变换或包围盒无效。"), *Mesh->GetName());
			return false;
		}

		FFlightPart& Part = Parts.AddDefaulted_GetRef();
		Part.Source = Mesh;
		Part.LocalCenter = Target.InverseTransformPosition(Mesh->Bounds.Origin);
		Part.BottomHeight = Mesh->Bounds.Origin.Z - Mesh->Bounds.BoxExtent.Z;
		Part.CenterHeight = Mesh->Bounds.Origin.Z;
		Part.StableName = Mesh->GetName();
		Part.OriginalCollision = Mesh->GetCollisionEnabled();
		Part.bOriginalVisible = Mesh->IsVisible();
		Part.bOriginalHiddenInGame = Mesh->bHiddenInGame;
		WorldBounds += Mesh->Bounds.GetBox();
	}

	if (Parts.IsEmpty())
	{
		OutError = TEXT("没有找到匹配的可见静态网格零件；请检查所属 Actor 和 Part Component Tag。");
		return false;
	}

	// 底部优先；同一底部再比较中心高度和组件名，固定模型的重播顺序因此可以复现。
	Parts.Sort([](const FFlightPart& A, const FFlightPart& B)
	{
		if (A.BottomHeight != B.BottomHeight)
		{
			return A.BottomHeight < B.BottomHeight;
		}
		if (A.CenterHeight != B.CenterHeight)
		{
			return A.CenterHeight < B.CenterHeight;
		}
		return A.StableName < B.StableName;
	});

	const FVector Center = WorldBounds.GetCenter();
	const FVector Extent = WorldBounds.GetExtent();
	const double Radius = FMath::Sqrt(Extent.X * Extent.X + Extent.Y * Extent.Y) + FlightDistance;
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		FFlightPart& Part = Parts[Index];
		const double Angle = FMath::DegreesToRadians(30.0 + Index * 137.508);
		const double CosAngle = FMath::Cos(Angle);
		const double SinAngle = FMath::Sin(Angle);
		const FVector StartCenter(Center.X + CosAngle * Radius, Center.Y + SinAngle * Radius,
			WorldBounds.Min.Z + Part.CenterHeight - Part.BottomHeight + 80.0);
		Part.StartCenterOwnerSpace = OwnerTransform.InverseTransformPosition(StartCenter);
		Part.ArcOffsetOwnerSpace = OwnerTransform.InverseTransformVector(
			FVector(-SinAngle * FlightArcHeight * 0.35, CosAngle * FlightArcHeight * 0.35, FlightArcHeight));
		Part.StartTilt = FRotator(CosAngle * 26.0, SinAngle * 55.0, SinAngle * 18.0).Quaternion();
		Part.LaunchTime = StartDelay + Index * LaunchInterval;
		if (!FMath::IsFinite(Part.LaunchTime) || !FMath::IsFinite(Part.LaunchTime + FlightDuration))
		{
			OutError = TEXT("发射时间超出可表示范围，请减小时间参数。");
			return false;
		}
	}
	return true;
}

bool UAssemblyFlightComponent::StartAssembly()
{
	// 重播先恢复上一轮；只恢复本组件保存过的状态，不重建原模型组件、不影响其他 Actor。
	ResetAssembly();
	LastError.Reset();
	FString Error;
	if (!CaptureParts(Error))
	{
		Parts.Reset();
		return FailAssembly(Error);
	}

	ActiveFlightDuration = FlightDuration;
	TotalPartCount = Parts.Num();
	bHasCapturedState = true;
	for (FFlightPart& Part : Parts)
	{
		UStaticMeshComponent* Source = Part.Source.Get();
		Source->SetVisibility(false, false);
		Source->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	bIsPlaying = true;
	SetComponentTickEnabled(true);
	const FString Message = FString::Printf(TEXT("连续飞入开始：%d 个零件，每 %.3f 秒发射一件，单件飞行 %.2f 秒。"), TotalPartCount, LaunchInterval, ActiveFlightDuration);
	UE_LOG(LogAssemblyFlight, Display, TEXT("%s [%s]"), *Message, *GetOwner()->GetName());
	ShowStatus(Message, FColor::Cyan);
	return true;
}

bool UAssemblyFlightComponent::CreateFlightVisual(FFlightPart& Part)
{
	UStaticMeshComponent* Source = Part.Source.Get();
	// 只在发射时创建临时网格，稳定阶段只保留约 FlightDuration / LaunchInterval 个显示组件。
	UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient);
	Part.Visual = Visual;
	Visual->SetMobility(EComponentMobility::Movable);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetGenerateOverlapEvents(false);
	Visual->SetCanEverAffectNavigation(false);
	Visual->SetVisibility(false, false);
	Visual->SetStaticMesh(Source->GetStaticMesh());
	for (int32 MaterialIndex = 0; MaterialIndex < Source->GetNumMaterials(); ++MaterialIndex)
	{
		Visual->SetMaterial(MaterialIndex, Source->GetMaterial(MaterialIndex));
	}
	Visual->SetCastShadow(Source->CastShadow);
	Visual->SetRenderCustomDepth(Source->bRenderCustomDepth);
	Visual->SetCustomDepthStencilValue(Source->CustomDepthStencilValue);
	Visual->SetReceivesDecals(Source->bReceivesDecals);
	Visual->RegisterComponent();
	return Visual->IsRegistered();
}

void UAssemblyFlightComponent::UpdateFlightVisual(FFlightPart& Part, const double Age)
{
	const FTransform OwnerTransform = GetOwner()->GetActorTransform();
	const FTransform Target = Part.Source->GetComponentTransform();
	const double U = FMath::Clamp(Age / ActiveFlightDuration, 0.0, 1.0);
	const double Eased = U * U * (3.0 - 2.0 * U);
	const double Arch = 4.0 * U * (1.0 - U);
	const FVector TargetCenter = Target.TransformPosition(Part.LocalCenter);
	const FVector StartCenter = OwnerTransform.TransformPosition(Part.StartCenterOwnerSpace);
	const FVector FlightCenter = FMath::Lerp(StartCenter, TargetCenter, Eased)
		+ OwnerTransform.TransformVector(Part.ArcOffsetOwnerSpace) * Arch;

	// 将倾斜视为所属 Actor 局部空间中的偏转，所属 Actor 在飞行中移动或转向时路径一起跟随。
	const FQuat OwnerRotation = OwnerTransform.GetRotation();
	const FQuat WorldTilt = OwnerRotation * Part.StartTilt * OwnerRotation.Inverse();
	const FQuat StartRotation = WorldTilt * Target.GetRotation();
	const FQuat FlightRotation = FQuat::Slerp(StartRotation, Target.GetRotation(), Eased).GetNormalized();
	// 减去旋转、缩放后的局部几何中心偏移，让共享原点的 SketchUp 零件围绕自身转正。
	const FVector CenterOffset = FlightRotation.RotateVector(Target.GetScale3D() * Part.LocalCenter);
	Part.Visual->SetWorldTransform(FTransform(FlightRotation, FlightCenter - CenterOffset, Target.GetScale3D()),
		false, nullptr, ETeleportType::TeleportPhysics);
	Part.Visual->SetVisibility(true, false);
}

void UAssemblyFlightComponent::RestorePart(FFlightPart& Part)
{
	if (UStaticMeshComponent* Visual = Part.Visual.Get())
	{
		Visual->DestroyComponent();
	}
	Part.Visual.Reset();
	if (UStaticMeshComponent* Source = Part.Source.Get())
	{
		// 原零件从未被移动，无需近似插值回原位，落定就是恢复其精确原几何与层级。
		Source->SetVisibility(Part.bOriginalVisible, false);
		Source->SetHiddenInGame(Part.bOriginalHiddenInGame, false);
		Source->SetCollisionEnabled(Part.OriginalCollision);
	}
}

void UAssemblyFlightComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bIsPlaying)
	{
		return;
	}
	ElapsedTime += DeltaTime;
	FlyingPartCount = 0;

	// 通过绝对发射时间调度，帧率低或单帧跨过多次发射也不会丢件或强制排队等待。
	while (NextLaunchIndex < Parts.Num() && Parts[NextLaunchIndex].LaunchTime <= ElapsedTime)
	{
		++NextLaunchIndex;
	}
	for (int32 Index = CompletedPartCount; Index < NextLaunchIndex; ++Index)
	{
		FFlightPart& Part = Parts[Index];
		if (!Part.Source.IsValid() || !Part.Source->IsRegistered())
		{
			FailAssembly(TEXT("播放期间有原零件被删除或取消注册，已中止并恢复其余零件。"));
			return;
		}
		const double Age = ElapsedTime - Part.LaunchTime;
		if (Age >= ActiveFlightDuration)
		{
			RestorePart(Part);
			++CompletedPartCount;
			continue;
		}
		if (!Part.Visual.IsValid() && !CreateFlightVisual(Part))
		{
			FailAssembly(TEXT("无法注册飞行显示组件，已中止并恢复原模型。"));
			return;
		}
		UpdateFlightVisual(Part, Age);
		++FlyingPartCount;
	}

	if (CompletedPartCount == TotalPartCount)
	{
		bIsPlaying = false;
		bHasCapturedState = false;
		SetComponentTickEnabled(false);
		const FString Message = FString::Printf(TEXT("连续飞入完成：%d 个零件全部归位。"), TotalPartCount);
		UE_LOG(LogAssemblyFlight, Display, TEXT("%s [%s]"), *Message, *GetOwner()->GetName());
		ShowStatus(Message, FColor::Green);
		OnAssemblyCompleted.Broadcast();
		// 这是函数最后一次访问自身状态的操作；棋盘的完成回调可以同步合并并销毁所属 Actor。
		OnAssemblyFinishedNative.Broadcast(this, true);
	}
}

void UAssemblyFlightComponent::RestoreCapturedState()
{
	if (bHasCapturedState)
	{
		for (FFlightPart& Part : Parts)
		{
			RestorePart(Part);
		}
	}
	bHasCapturedState = false;
}

void UAssemblyFlightComponent::ResetAssembly()
{
	const bool bWasPlaying = bIsPlaying;
	SetComponentTickEnabled(false);
	bIsPlaying = false;
	RestoreCapturedState();
	Parts.Reset();
	NextLaunchIndex = 0;
	TotalPartCount = 0;
	CompletedPartCount = 0;
	FlyingPartCount = 0;
	ElapsedTime = 0.0;
	if (bWasPlaying)
	{
		// 取消不冒充完成：棋盘收到 false 后丢弃本次延迟合并，不能提前升级。
		OnAssemblyFinishedNative.Broadcast(this, false);
	}
}

bool UAssemblyFlightComponent::FailAssembly(const FString& Error)
{
	// 这是明确中止后的状态恢复，不尝试另一种视觉逻辑；日志和 Details 都保留失败原因。
	LastError = Error;
	UE_LOG(LogAssemblyFlight, Error, TEXT("连续飞入失败 [%s]：%s"), *GetNameSafe(GetOwner()), *Error);
	ShowStatus(FString::Printf(TEXT("连续飞入失败：%s"), *Error), FColor::Red);
	ResetAssembly();
	return false;
}

void UAssemblyFlightComponent::ShowStatus(const FString& Message, const FColor& Color) const
{
	if (bShowStatusOnScreen && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 5.0f, Color, Message);
	}
}

void UAssemblyFlightComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 即使被提前移除或关卡结束，也不留下隐藏的原件或仍占资源的临时显示组件。
	ResetAssembly();
	Super::EndPlay(EndPlayReason);
}
