// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingBase.h"

#include "CastleBuildingDefinition.h"
#include "BuildingMergeComponent.h"
#include "AssemblyFlightComponent.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

ACastleBuildingBase::ACastleBuildingBase()
{
	// 建筑逻辑和模型组件现在位于同一个 Actor；VisualRoot 只提供统一的组件根节点。
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	SetRootComponent(VisualRoot);
	// 功能分支随整座建筑移动，但不承担美术零件；子组件可继续按楼层等需求嵌套分组。
	GameplayRoot = CreateDefaultSubobject<UBuildingGameplayRootComponent>(TEXT("GameplayRoot"));
	GameplayRoot->SetupAttachment(VisualRoot);
	BuildingMerge = CreateDefaultSubobject<UBuildingMergeComponent>(TEXT("BuildingMerge"));
	AssemblyFlight = CreateDefaultSubobject<UAssemblyFlightComponent>(TEXT("AssemblyFlight"));
	// 所有建筑共享原有连续飞入实现；只有棋盘放置流程有权起播，避免预览/升级模型误播。
	AssemblyFlight->bAutoPlay = false;
	AssemblyFlight->ExcludedComponentTag = TEXT("MergeExclude");

	// 只有没有美术模型的等级才显示这个组件，正式模型直接来自等级蓝图自身。
	PrototypeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrototypeMesh"));
	PrototypeMesh->SetupAttachment(VisualRoot);
	PrototypeMesh->SetCanEverAffectNavigation(false);
	PrototypeMesh->SetVisibility(false);
	PrototypeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	PlaceholderCube = Cube.Object;
	PlaceholderMaterial = Material.Object;

	// 标签只在 ConfigureBuildingVisual 成功后显示，避免生成中间状态被玩家看见。
	PrototypeLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PrototypeLabel"));
	PrototypeLabel->SetupAttachment(VisualRoot);
	PrototypeLabel->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	PrototypeLabel->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	PrototypeLabel->SetTextRenderColor(FColor::White);
	PrototypeLabel->SetWorldSize(52.0f);
	PrototypeLabel->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	PrototypeLabel->SetVisibility(false);

	PrototypeLabelPlate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrototypeLabelPlate"));
	// 数字牌是界面辅助物，合并时随旧 Actor 消失，不把它误当成屋顶零件卷入建筑。
	PrototypeLabelPlate->ComponentTags.Add(TEXT("MergeExclude"));
	PrototypeLabelPlate->SetupAttachment(VisualRoot);
	PrototypeLabelPlate->SetStaticMesh(PlaceholderCube);
	PrototypeLabelPlate->SetMaterial(0, PlaceholderMaterial);
	PrototypeLabelPlate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PrototypeLabelPlate->SetCanEverAffectNavigation(false);
	PrototypeLabelPlate->SetVisibility(false);
}

void ACastleBuildingBase::SetPlacementComplete(const bool bComplete)
{
	bHasCompletedPlacement = bComplete;
	PrototypeLabel->SetVisibility(bComplete, false);
	PrototypeLabelPlate->SetVisibility(bComplete, false);
}

bool ACastleBuildingBase::InitializeBuilding(const FCastleBuildingDescriptor& Descriptor, const TArray<FIntVector>& InOccupiedCoordinates)
{
	FText Reason;
	if (!Descriptor.Validate(Reason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：%s"), *Reason.ToString());
		return false;
	}
	if (Descriptor.GetBuildingClass() != GetClass() || Descriptor.Level != BuildingLevel || Descriptor.GetBuildingType() != BuildingType)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：Data Asset 的生成类、类别或等级与实际 Actor 不一致。"));
		return false;
	}
	if (InOccupiedCoordinates.Num() != GetExpectedFootprintCellCount(BuildingLevel))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：%d 级需要 %d 格，实际收到 %d 格。"),
			BuildingLevel, GetExpectedFootprintCellCount(BuildingLevel), InOccupiedCoordinates.Num());
		return false;
	}
	TSet<FIntVector> UniqueCoordinates;
	for (const FIntVector& Coordinate : InOccupiedCoordinates)
	{
		if (UniqueCoordinates.Contains(Coordinate))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：占用坐标重复。"));
			return false;
		}
		UniqueCoordinates.Add(Coordinate);
	}

	// 保存资产身份，后续升级、回收和再次放置都不重新推断建筑种类。
	BuildingDefinition = Descriptor.Definition;
	Color = Descriptor.Color;
	Number = Descriptor.Number;
	OccupiedCoordinates = InOccupiedCoordinates;
	PrototypeLabel->SetText(FText::FromString(FString::Printf(TEXT("%d\nLv%d"), Number, BuildingLevel)));
	return true;
}

FCastleBuildingDescriptor ACastleBuildingBase::GetBuildingDescriptor() const
{
	FCastleBuildingDescriptor Descriptor;
	Descriptor.Definition = BuildingDefinition;
	Descriptor.Level = BuildingLevel;
	Descriptor.Color = Color;
	Descriptor.Number = Number;
	return Descriptor;
}

TSubclassOf<ACastleBuildingBase> ACastleBuildingBase::GetClassForLevel(int32 InLevel) const
{
	const FCastleBuildingLevelDefinition* Entry = IsValid(BuildingDefinition) ? BuildingDefinition->FindLevel(InLevel) : nullptr;
	return Entry != nullptr ? Entry->BuildingClass : nullptr;
}

bool ACastleBuildingBase::CanMergeToNextLevel() const
{
	// 保留策划案的四级自动合成上限；五级仍供后续奖励系统直接升级。
	return BuildingType != ECastleBuildingType::Road && BuildingLevel >= 1 && BuildingLevel < 4
		&& GetClassForLevel(BuildingLevel + 1) != nullptr;
}

bool ACastleBuildingBase::CanMergeWith(const ACastleBuildingBase* Other) const
{
	// 相同类别但引用不同 Data Asset 的建筑仍是不同建筑，不能误合成为另一种模型。
	return Other != nullptr && Other != this && CanMergeToNextLevel()
		&& BuildingDefinition == Other->BuildingDefinition && BuildingLevel == Other->BuildingLevel
		&& Color == Other->Color && Number == Other->Number;
}

int32 ACastleBuildingBase::GetExpectedFootprintCellCount(int32 InLevel)
{
	// 等级 1..4 对应 1/2/4/8 格；五级由奖励生成，沿用既有的单格规则。
	return InLevel < 1 || InLevel > 5 ? 0 : (InLevel == 5 ? 1 : (1 << (InLevel - 1)));
}

bool ACastleBuildingBase::ConfigureBuildingVisual(float InCellSize, float InLayerHeight, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (OccupiedCoordinates.IsEmpty() || InCellSize <= 0.0f || InLayerHeight <= 0.0f)
	{
		OutFailureReason = FText::FromString(TEXT("建筑尚未初始化占用坐标，或棋盘尺寸无效，无法创建模型。"));
		return false;
	}

	FIntVector Minimum = OccupiedCoordinates[0];
	FIntVector Maximum = OccupiedCoordinates[0];
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		Minimum = Minimum.ComponentMin(Coordinate);
		Maximum = Maximum.ComponentMax(Coordinate);
	}
	const FIntVector Dimensions = Maximum - Minimum + FIntVector(1, 1, 1);

	// 先隐藏标签和占位网格，再检查蓝图是否直接提供了可见 StaticMeshComponent。
	// 这样同一个函数同时支持带模型的等级蓝图和没有模型的纯 C++ 等级。
	PrototypeLabel->SetVisibility(false);
	PrototypeLabelPlate->SetVisibility(false);
	PrototypeMesh->SetVisibility(false);
	PrototypeMesh->SetStaticMesh(nullptr);

	TArray<UStaticMeshComponent*> StaticMeshes;
	GetComponents<UStaticMeshComponent>(StaticMeshes);
	bool bHasConfiguredModel = false;
	for (UStaticMeshComponent* Component : StaticMeshes)
	{
		if (Component == PrototypeMesh || Component == PrototypeLabelPlate
			|| UBuildingGameplayRootComponent::IsInGameplayHierarchy(Component))
		{
			continue;
		}
		if (Component->GetStaticMesh() != nullptr && Component->IsVisible())
		{
			bHasConfiguredModel = true;
			break;
		}
	}

	bUsingPlaceholder = !bHasConfiguredModel;
	if (bUsingPlaceholder)
	{
		if (PlaceholderCube == nullptr || PlaceholderMaterial == nullptr)
		{
			OutFailureReason = FText::FromString(TEXT("未配置模型，且引擎占位立方体或基础材质不可用。"));
			return false;
		}
		PrototypeMesh->SetStaticMesh(PlaceholderCube);
		PrototypeMesh->SetRelativeTransform(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(
			Dimensions.X * InCellSize * 0.86f / 100.0f,
			Dimensions.Y * InCellSize * 0.86f / 100.0f,
			Dimensions.Z * InLayerHeight * 0.72f / 100.0f)));
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
		PrototypeMesh->SetVisibility(true);
	}

	// 美术模型提供 Visibility 射线命中；功能分支保留作者配置，不能按可见性关闭隐藏地板碰撞。
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (UBuildingGameplayRootComponent::IsInGameplayHierarchy(Primitive))
		{
			continue;
		}
		Primitive->SetSimulatePhysics(false);
		Primitive->SetCollisionEnabled(Primitive->IsVisible() ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		Primitive->SetCollisionResponseToAllChannels(ECR_Ignore);
		Primitive->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Primitive->SetCanEverAffectNavigation(false);
	}

	const FBox Bounds = GetVisualBounds();
	if (!Bounds.IsValid)
	{
		OutFailureReason = FText::FromString(TEXT("建筑没有可见几何体，无法显示或选中。"));
		return false;
	}
	const FBox LocalBounds = Bounds.TransformBy(GetActorTransform().ToInverseMatrixWithScale());
	const float PlateHeight = LocalBounds.Max.Z + 8.0f;
	const float Width = FMath::Min(Dimensions.X * InCellSize * 0.72f, 300.0f);
	const float Depth = FMath::Min(Dimensions.Y * InCellSize * 0.72f, 180.0f);
	PrototypeLabelPlate->SetRelativeLocation(FVector(0.0f, 0.0f, PlateHeight));
	PrototypeLabelPlate->SetRelativeScale3D(FVector(Width / 100.0f, Depth / 100.0f, 0.035f));
	UMaterialInstanceDynamic* PlateMaterial = UMaterialInstanceDynamic::Create(PlaceholderMaterial, this);
	if (PlateMaterial == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("建筑标签材质创建失败。"));
		return false;
	}
	PlateMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.015f, 0.015f, 0.02f));
	PrototypeLabelPlate->SetMaterial(0, PlateMaterial);
	PrototypeLabel->SetRelativeLocation(FVector(0.0f, 0.0f, PlateHeight + 2.5f));
	const FColor LabelColors[] = { FColor::Silver, FColor::Red, FColor::Cyan, FColor::Green, FColor::Yellow, FColor::Magenta };
	PrototypeLabel->SetTextRenderColor(LabelColors[static_cast<int32>(Color)]);
	PrototypeLabel->SetVisibility(true);
	PrototypeLabelPlate->SetVisibility(true);
	return true;
}

FBox ACastleBuildingBase::GetVisualBounds() const
{
	FBox Result(ForceInit);
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
	for (const UPrimitiveComponent* Primitive : Primitives)
	{
		// 隐藏碰撞即使为了编辑调试临时设为可见，也不能扩大建筑标签高度和卡片取景范围。
		if (UBuildingGameplayRootComponent::IsInGameplayHierarchy(Primitive))
		{
			continue;
		}
		// 标签不属于模型几何体，取景和标签高度都不应把文字自身再次计入范围。
		if (Primitive == PrototypeLabelPlate || Primitive == PrototypeLabel)
		{
			continue;
		}
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

void ACastleBuildingBase::PrepareForPreview()
{
	// 卡片只显示建筑形状，等级与数字由卡片自己的文字控件显示。
	PrototypeLabel->SetVisibility(false);
	PrototypeLabelPlate->SetVisibility(false);
	SetActorEnableCollision(false);
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (UBuildingGameplayRootComponent::IsInGameplayHierarchy(Primitive))
		{
			// 预览使用独立 Actor：功能几何不进入卡片，也不向真实关卡提供碰撞或导航。
			Primitive->SetVisibility(false, false);
			Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Primitive->SetCanEverAffectNavigation(false);
			continue;
		}
		Primitive->SetVisibleInSceneCaptureOnly(true);
		Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Primitive->SetCastShadow(false);
	}
}
