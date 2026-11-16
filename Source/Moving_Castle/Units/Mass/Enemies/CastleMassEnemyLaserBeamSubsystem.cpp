#include "CastleMassEnemyLaserBeamSubsystem.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Moving_Castle.h"

bool UCastleMassEnemyLaserBeamSubsystem::UpdateBeams(const TArray<FCastleMassEnemyLaserBeam>& Beams, FString& OutFailureReason)
{
	OutFailureReason.Reset();
	// 在创建或清空组件前校验整批输入，错误不能伪装成成功，也不替换网格/材质。
	for (const FCastleMassEnemyLaserBeam& Beam : Beams)
	{
		if (!IsValid(Beam.Mesh) || !IsValid(Beam.Material) || Beam.Start.ContainsNaN() || Beam.End.ContainsNaN()
			|| !FMath::IsFinite(Beam.Thickness) || Beam.Thickness <= 0.0f || Beam.Start.Equals(Beam.End, UE_SMALL_NUMBER))
		{
			OutFailureReason = FString::Printf(TEXT("实体 %d 的光束网格 %s、材质 %s、端点或粗细无效"), Beam.Entity.Index, *GetPathNameSafe(Beam.Mesh), *GetPathNameSafe(Beam.Material));
			return false;
		}
		const FVector Extent = Beam.Mesh->GetBounds().BoxExtent;
		if (Extent.X <= UE_SMALL_NUMBER || Extent.Y <= UE_SMALL_NUMBER || Extent.Z <= UE_SMALL_NUMBER)
		{
			OutFailureReason = FString::Printf(TEXT("光束网格 %s 三轴包围盒必须非零"), *Beam.Mesh->GetPathName());
			return false;
		}
	}
	for (UInstancedStaticMeshComponent* Component : BeamComponents) { Component->ClearInstances(); }
	for (const FCastleMassEnemyLaserBeam& Beam : Beams)
	{
		// 不同网格/材质各占一个批次，同一配置的所有幼骸共用组件。
		UInstancedStaticMeshComponent* Component = nullptr;
		for (UInstancedStaticMeshComponent* Candidate : BeamComponents)
		{
			if (Candidate->GetStaticMesh() == Beam.Mesh && Candidate->GetMaterial(0) == Beam.Material) { Component = Candidate; break; }
		}
		if (!Component)
		{
			AWorldSettings* Settings = GetWorld()->GetWorldSettings();
			if (!Settings) { OutFailureReason = TEXT("当前世界没有有效 WorldSettings，无法注册光束批次"); return false; }
			Component = NewObject<UInstancedStaticMeshComponent>(Settings, NAME_None, RF_Transient);
			Settings->AddInstanceComponent(Component);
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCastShadow(false);
			Component->SetStaticMesh(Beam.Mesh);
			Component->SetMaterial(0, Beam.Material);
			Component->RegisterComponent();
			BeamComponents.Add(Component);
		}
		const FVector Delta = Beam.End - Beam.Start;
		const FBoxSphereBounds Bounds = Beam.Mesh->GetBounds();
		const FVector Scale(Delta.Size() / (2.0 * Bounds.BoxExtent.X), Beam.Thickness / Bounds.BoxExtent.Y, Beam.Thickness / Bounds.BoxExtent.Z);
		const FQuat Rotation = Delta.Rotation().Quaternion();
		// 补偿网格自身包围盒中心，保证端点精确落在发射点与目标点上。
		const FVector Center = (Beam.Start + Beam.End) * 0.5 - Rotation.RotateVector(Bounds.Origin * Scale);
		Component->AddInstance(FTransform(Rotation, Center, Scale), true);
	}
	ActiveBeams = Beams;
	return true;
}

void UCastleMassEnemyLaserBeamSubsystem::RemoveBeam(FMassEntityHandle Entity)
{
	TArray<FCastleMassEnemyLaserBeam> Remaining = ActiveBeams;
	const int32 Removed = Remaining.RemoveAll([Entity](const FCastleMassEnemyLaserBeam& Beam) { return Beam.Entity == Entity; });
	if (Removed > 0)
	{
		FString Reason;
		if (!UpdateBeams(Remaining, Reason)) { UE_LOG(LogMoving_Castle, Error, TEXT("实体 %d 的激光回收失败：%s"), Entity.Index, *Reason); }
	}
}

int32 UCastleMassEnemyLaserBeamSubsystem::GetActiveBeamCount() const
{
	int32 Count = 0;
	for (const UInstancedStaticMeshComponent* Component : BeamComponents) { Count += Component->GetInstanceCount(); }
	return Count;
}

void UCastleMassEnemyLaserBeamSubsystem::Deinitialize()
{
	for (UInstancedStaticMeshComponent* Component : BeamComponents) { Component->DestroyComponent(); }
	ActiveBeams.Reset();
	BeamComponents.Reset();
	Super::Deinitialize();
}
