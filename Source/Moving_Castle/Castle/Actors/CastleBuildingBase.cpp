// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingBase.h"

#include "CastleBuildingDefinition.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

ACastleBuildingBase::ACastleBuildingBase()
{
	// 模型组件由父类创建；逻辑建筑只补充独立标签，不改变原美术材质。
	PrototypeLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PrototypeLabel"));
	PrototypeLabel->SetupAttachment(VisualRoot);
	PrototypeLabel->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	PrototypeLabel->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	PrototypeLabel->SetTextRenderColor(FColor::White);
	PrototypeLabel->SetWorldSize(52.0f);
	PrototypeLabel->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));

	PrototypeLabelPlate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrototypeLabelPlate"));
	PrototypeLabelPlate->SetupAttachment(VisualRoot);
	PrototypeLabelPlate->SetStaticMesh(PlaceholderCube);
	PrototypeLabelPlate->SetMaterial(0, PlaceholderMaterial);
	PrototypeLabelPlate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PrototypeLabelPlate->SetCanEverAffectNavigation(false);
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
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：Data Asset 的逻辑类、类别或等级与实际 Actor 不一致。"));
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
	if (OccupiedCoordinates.IsEmpty())
	{
		OutFailureReason = FText::FromString(TEXT("建筑尚未初始化占用坐标，无法创建模型。"));
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
	// 排除尚未定位的标签，模型边界只用来决定标签应放到多高。
	PrototypeLabel->SetVisibility(false);
	PrototypeLabelPlate->SetVisibility(false);
	if (!ConfigureModel(BuildingDefinition, BuildingLevel, Color, Dimensions, InCellSize, InLayerHeight, OutFailureReason))
	{
		return false;
	}
	const FBox Bounds = GetVisualBounds();
	if (!Bounds.IsValid)
	{
		OutFailureReason = FText::FromString(TEXT("建筑模型没有可见几何体，无法显示或选中。"));
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
