// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingModelActor.h"

#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

ACastleBuildingModelActor::ACastleBuildingModelActor()
{
	PrimaryActorTick.bCanEverTick = false;
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	SetRootComponent(VisualRoot);
	ModelPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ModelPivot"));
	ModelPivot->SetupAttachment(VisualRoot);
	PrototypeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrototypeMesh"));
	PrototypeMesh->SetupAttachment(ModelPivot);
	PrototypeMesh->SetCanEverAffectNavigation(false);
	ModelBlueprint = CreateDefaultSubobject<UChildActorComponent>(TEXT("ModelBlueprint"));
	ModelBlueprint->SetupAttachment(ModelPivot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	PlaceholderCube = Cube.Object;
	PlaceholderMaterial = Material.Object;
}

bool ACastleBuildingModelActor::ConfigureModel(const UCastleBuildingDefinition* Definition, int32 Level, ECastleTileColor Color,
	const FIntVector& FootprintDimensions, float CellSize, float LayerHeight, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (!IsValid(Definition) || !Definition->ValidateConfiguration(OutFailureReason))
	{
		if (OutFailureReason.IsEmpty())
		{
			OutFailureReason = FText::FromString(TEXT("建筑模型缺少有效的建筑 Data Asset。"));
		}
		return false;
	}
	const FCastleBuildingLevelDefinition* Entry = Definition->FindLevel(Level);
	if (Entry == nullptr || FootprintDimensions.GetMin() < 1 || CellSize <= 0.0f || LayerHeight <= 0.0f)
	{
		OutFailureReason = FText::FromString(TEXT("建筑模型的等级、占用范围或网格尺寸无效。"));
		return false;
	}

	// 已填写但无法加载的引用属于配置错误，不得悄悄改成占位立方体。
	UStaticMesh* ConfiguredMesh = nullptr;
	UClass* ConfiguredActorClass = nullptr;
	if (!Entry->StaticMesh.IsNull())
	{
		ConfiguredMesh = Entry->StaticMesh.LoadSynchronous();
		if (ConfiguredMesh == nullptr)
		{
			OutFailureReason = FText::FromString(TEXT("已配置的建筑静态网格无法加载。"));
			return false;
		}
	}
	if (!Entry->ModelActorClass.IsNull())
	{
		ConfiguredActorClass = Entry->ModelActorClass.LoadSynchronous();
		if (ConfiguredActorClass == nullptr || ConfiguredActorClass->HasAnyClassFlags(CLASS_Abstract)
			|| ConfiguredActorClass->IsChildOf(ACastleBuildingModelActor::StaticClass()))
		{
			OutFailureReason = FText::FromString(TEXT("建筑模型 Actor 无法加载、是抽象类，或错误地引用了建筑逻辑/模型载体类。"));
			return false;
		}
	}

	ModelBlueprint->SetChildActorClass(nullptr);
	PrototypeMesh->SetStaticMesh(nullptr);
	PrototypeMesh->SetMaterial(0, nullptr);
	ModelPivot->SetRelativeRotation(FRotator::ZeroRotator);
	bUsingPlaceholder = ConfiguredMesh == nullptr && ConfiguredActorClass == nullptr;
	if (bUsingPlaceholder)
	{
		if (PlaceholderCube == nullptr || PlaceholderMaterial == nullptr)
		{
			OutFailureReason = FText::FromString(TEXT("未配置模型，且引擎占位立方体或基础材质不可用。"));
			return false;
		}
		PrototypeMesh->SetStaticMesh(PlaceholderCube);
		PrototypeMesh->SetRelativeTransform(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(
			FootprintDimensions.X * CellSize * 0.86f / 100.0f,
			FootprintDimensions.Y * CellSize * 0.86f / 100.0f,
			FootprintDimensions.Z * LayerHeight * 0.72f / 100.0f)));
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(PlaceholderMaterial, this);
		if (Material == nullptr)
		{
			OutFailureReason = FText::FromString(TEXT("建筑占位颜色材质创建失败。"));
			return false;
		}
		const FLinearColor Colors[] = {
			FLinearColor(0.42f, 0.46f, 0.52f), FLinearColor(0.85f, 0.12f, 0.12f),
			FLinearColor(0.10f, 0.35f, 0.90f), FLinearColor(0.10f, 0.75f, 0.25f),
			FLinearColor(0.95f, 0.70f, 0.08f), FLinearColor(0.65f, 0.20f, 0.85f)
		};
		const int32 ColorIndex = static_cast<int32>(Color);
		if (ColorIndex < 0 || ColorIndex >= UE_ARRAY_COUNT(Colors))
		{
			OutFailureReason = FText::FromString(TEXT("建筑占位颜色无效。"));
			return false;
		}
		Material->SetVectorParameterValue(TEXT("Color"), Colors[ColorIndex]);
		PrototypeMesh->SetMaterial(0, Material);
	}
	else
	{
		// 二级标准模型沿 X 轴制作；实际合成沿 Y 轴时整体旋转 90 度，保留原模型比例。
		if (Level == 2 && FootprintDimensions.X == 1 && FootprintDimensions.Y == 2)
		{
			ModelPivot->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
		}
		if (ConfiguredMesh != nullptr)
		{
			PrototypeMesh->SetStaticMesh(ConfiguredMesh);
			PrototypeMesh->SetRelativeTransform(Entry->ModelTransform);
		}
		else
		{
			ModelBlueprint->SetRelativeTransform(Entry->ModelTransform);
			ModelBlueprint->SetChildActorClass(ConfiguredActorClass);
			if (ModelBlueprint->GetChildActor() == nullptr)
			{
				OutFailureReason = FText::FromString(TEXT("已配置的建筑模型 Actor 创建失败。"));
				return false;
			}
		}
	}

	// 正式模型同样提供 Visibility 射线命中，但不阻挡玩家或参与物理模拟。
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives, true);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		Primitive->SetSimulatePhysics(false);
		Primitive->SetCollisionEnabled(Primitive->IsVisible() ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		Primitive->SetCollisionResponseToAllChannels(ECR_Ignore);
		Primitive->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Primitive->SetCanEverAffectNavigation(false);
	}
	return true;
}

FBox ACastleBuildingModelActor::GetVisualBounds() const
{
	FBox Result(ForceInit);
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives, true);
	for (const UPrimitiveComponent* Primitive : Primitives)
	{
		// 蓝图模型模式下此组件没有网格，不能把空组件的原点当成可见几何体。
		if (Primitive == PrototypeMesh && PrototypeMesh->GetStaticMesh() == nullptr)
		{
			continue;
		}
		if (Primitive->IsRegistered() && Primitive->IsVisible())
		{
			Result += Primitive->Bounds.GetBox();
		}
	}
	return Result;
}

void ACastleBuildingModelActor::PrepareForPreview()
{
	SetActorEnableCollision(false);
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives, true);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		Primitive->SetVisibleInSceneCaptureOnly(true);
		Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Primitive->SetCastShadow(false);
	}
}
