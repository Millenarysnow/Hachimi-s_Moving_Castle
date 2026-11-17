#include "CastleSupportFlightActors.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 形状为明确的美术配置，只允许气球点击球响应射线；所有飞行特效均不影响 NavMesh。 */
	void ConfigureMesh(UStaticMeshComponent& Mesh, UStaticMesh* Shape, USceneComponent* Parent)
	{
		if (Parent) { Mesh.SetupAttachment(Parent); }
		Mesh.SetStaticMesh(Shape);
		Mesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh.SetCanEverAffectNavigation(false);
		Mesh.SetCastShadow(false);
	}

	bool SetTint(UStaticMeshComponent& Mesh, UMaterialInterface* Material, const FLinearColor Tint)
	{
		if (Mesh.GetStaticMesh() && IsValid(Material))
		{
			FLinearColor Unused;
			if (Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Tint")), Unused))
			{
				if (UMaterialInstanceDynamic* Instance = Mesh.CreateDynamicMaterialInstance(0, Material))
				{
					Instance->SetVectorParameterValue(TEXT("Tint"), Tint);
					return true;
				}
			}
		}
		UE_LOG(LogMoving_Castle, Error, TEXT("支援飞行表现 %s 缺少形状或材质 %s 的 Tint 参数。"),
			*Mesh.GetPathName(), *GetPathNameSafe(Material));
		return false;
	}
}

ACastleBeeSwarmVisual::ACastleBeeSwarmVisual()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Bees = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BeeBodies"));
	Wings = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BeeWings"));
	SetRootComponent(Bees);
	ConfigureMesh(*Bees, Sphere.Object, nullptr);
	ConfigureMesh(*Wings, Sphere.Object, Bees);
}

bool ACastleBeeSwarmVisual::Configure(const int32 Count, UMaterialInterface* Material)
{
	if (!SetTint(*Bees, Material, FLinearColor(1.0f, 0.53f, 0.03f))
		|| !SetTint(*Wings, Material, FLinearColor(0.61f, 0.89f, 1.0f))) { return false; }
	// 黄金角分布使相邻蜂体错开；每批共享刚体绕转，蜂体与双翼不需要逐帧改实例变换。
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double Angle = Index * 2.399963229728653;
		const double Radius = 15.0 * FMath::Sqrt(static_cast<double>(Index));
		const FVector Point(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, (Index % 5) * 12.0);
		Bees->AddInstance(FTransform(FQuat::Identity, Point, FVector(0.19, 0.12, 0.12)));
		for (const double Side : {-1.0, 1.0})
		{
			Wings->AddInstance(FTransform(FRotator(0.0, Side * -22.0, 0.0),
				Point + FVector(0.0, Side * 8.0, 4.0), FVector(0.065, 0.15, 0.02)));
		}
	}
	return true;
}

ACastleSupplyBalloon::ACastleSupplyBalloon()
{
	ClickSphere = CreateDefaultSubobject<USphereComponent>(TEXT("BalloonClickTarget"));
	SetRootComponent(ClickSphere);
	ClickSphere->InitSphereRadius(92.0f);
	ClickSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ClickSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	ClickSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ClickSphere->SetGenerateOverlapEvents(false);
	ClickSphere->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Box(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Balloon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BalloonCanopy"));
	Tether = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CargoTether"));
	Cargo = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SupplyCrate"));
	ConfigureMesh(*Balloon, Sphere.Object, ClickSphere);
	ConfigureMesh(*Tether, Box.Object, ClickSphere);
	ConfigureMesh(*Cargo, Box.Object, ClickSphere);
	Balloon->SetRelativeScale3D(FVector(1.56, 1.56, 1.86));
	Tether->SetRelativeScale3D(FVector(0.04, 0.04, 0.54));
	Tether->SetRelativeLocation(FVector(0, 0, -111));
	Cargo->SetRelativeScale3D(FVector(0.62, 0.55, 0.40));
	Cargo->SetRelativeLocation(FVector(0, 0, -145));
}

bool ACastleSupplyBalloon::Configure(const uint64 InId, UMaterialInterface* Material, const FLinearColor Tint)
{
	TargetId = InId;
	return SetTint(*Balloon, Material, Tint) && SetTint(*Tether, Material, FLinearColor(0.14f, 0.08f, 0.04f))
		&& SetTint(*Cargo, Material, Tint * 0.58f);
}

void ACastleSupplyBalloon::ReleaseCargo()
{
	// 以货箱原世界位置续接下落，不能在气球中心重新生成或把箱子跳到地面。
	const FVector CargoLocation = Cargo->GetComponentLocation();
	Balloon->SetVisibility(false);
	Tether->SetVisibility(false);
	ClickSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cargo->SetRelativeLocation(FVector::ZeroVector);
	SetActorLocation(CargoLocation);
}
