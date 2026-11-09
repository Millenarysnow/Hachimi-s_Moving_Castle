// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingBase.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

ACastleBuildingBase::ACastleBuildingBase()
{
	// 使用 UE 自带的 BasicShapes/Cube 作为临时占位网格，避免当前阶段依赖正式美术资源。
	PrimaryActorTick.bCanEverTick = false;

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	SetRootComponent(VisualRoot);

	PrototypeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrototypeMesh"));
	PrototypeMesh->SetupAttachment(VisualRoot);
	// 建筑占位网格需要参与屏幕中心射线查询，才能显示建筑侧面和顶部的吸附面；
	// 它仍然不参与物理阻挡，飞行玩家不会被原型建筑卡住。
	PrototypeMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PrototypeMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	PrototypeMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PrototypeMesh->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		PrototypeMesh->SetStaticMesh(CubeMeshFinder.Object);
	}
	else
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑占位可视化初始化失败：找不到 UE BasicShapes/Cube。"));
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicMaterialFinder.Succeeded())
	{
		PrototypeMesh->SetMaterial(0, BasicMaterialFinder.Object);
	}
	else
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑占位可视化初始化失败：找不到 UE BasicShapeMaterial。"));
	}

	PrototypeLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PrototypeLabel"));
	PrototypeLabel->SetupAttachment(VisualRoot);
	PrototypeLabel->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	PrototypeLabel->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	PrototypeLabel->SetTextRenderColor(FColor::White);
	PrototypeLabel->SetWorldSize(52.0f);
	PrototypeLabel->SetXScale(1.1f);
	PrototypeLabel->SetYScale(1.1f);
	PrototypeLabel->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	PrototypeLabel->SetText(FText::FromString(TEXT("Building")));

	// 标签底牌与文字都挂在独立根节点上，合成时只改变建筑网格尺寸，不会把标签压扁。
	PrototypeLabelPlate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrototypeLabelPlate"));
	PrototypeLabelPlate->SetupAttachment(VisualRoot);
	PrototypeLabelPlate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PrototypeLabelPlate->SetCanEverAffectNavigation(false);
	if (CubeMeshFinder.Succeeded())
	{
		PrototypeLabelPlate->SetStaticMesh(CubeMeshFinder.Object);
	}
	if (BasicMaterialFinder.Succeeded())
	{
		PrototypeLabelPlate->SetMaterial(0, BasicMaterialFinder.Object);
	}
}
bool ACastleBuildingBase::InitializeBuilding(const FCastleBuildingDescriptor& Descriptor, const TArray<FIntVector>& InOccupiedCoordinates)
{
	if (!Descriptor.HasBuildingClass())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：没有提供 BuildingClass。"));
		return false;
	}

	if (Descriptor.BuildingClass != GetClass())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：描述类与实际 Actor 类不一致。"));
		return false;
	}

	if (Descriptor.Level < 1 || Descriptor.Level != BuildingLevel)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：描述等级 %d 与 Actor 等级 %d 不一致。"), Descriptor.Level, BuildingLevel);
		return false;
	}

	if (Descriptor.BuildingType != BuildingType)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：描述类别与 Actor 类别不一致。"));
		return false;
	}

	if (InOccupiedCoordinates.Num() == 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：建筑至少需要占用一个网格坐标。"));
		return false;
	}

	if (Descriptor.Number < 0 || Descriptor.Number > 9)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：数字 %d 超出 0 到 9 的范围。"), Descriptor.Number);
		return false;
	}

	if (BuildingType == ECastleBuildingType::Road)
	{
		if (Descriptor.Color != ECastleTileColor::Gray || Descriptor.Number != 0)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：道路必须是灰色 0 号。"));
			return false;
		}
	}
	else if (Descriptor.Color == ECastleTileColor::Gray || Descriptor.Number == 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：功能建筑不能使用灰色或 0 号属性。"));
		return false;
	}

	TSet<FIntVector> UniqueCoordinates;
	for (const FIntVector& Coordinate : InOccupiedCoordinates)
	{
		if (UniqueCoordinates.Contains(Coordinate))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：占用坐标出现重复值 (%d, %d, %d)。"), Coordinate.X, Coordinate.Y, Coordinate.Z);
			return false;
		}

		UniqueCoordinates.Add(Coordinate);
	}

	const int32 ExpectedCellCount = GetExpectedFootprintCellCount(BuildingLevel);
	if (ExpectedCellCount > 0 && InOccupiedCoordinates.Num() != ExpectedCellCount)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：等级 %d 需要 %d 个占用格子，实际收到 %d 个。"), BuildingLevel, ExpectedCellCount, InOccupiedCoordinates.Num());
		return false;
	}

	BuildingType = Descriptor.BuildingType;
	Color = Descriptor.Color;
	Number = Descriptor.Number;
	OccupiedCoordinates = InOccupiedCoordinates;

	// 标签在 Actor 生成后立即更新；网格尺寸由棋盘在知道格子大小后补充配置。
	// 标签使用短类别缩写，把有限的牌面空间优先留给玩家需要读取的数字和等级。
	const TCHAR* TypeText = TEXT("R");
	switch (BuildingType)
	{
	case ECastleBuildingType::ArcherTower:
		TypeText = TEXT("A");
		break;
	case ECastleBuildingType::CannonTower:
		TypeText = TEXT("C");
		break;
	case ECastleBuildingType::Farm:
		TypeText = TEXT("F");
		break;
	default:
		break;
	}
	PrototypeLabel->SetText(FText::FromString(FString::Printf(TEXT("%s %d\nLv%d"), TypeText, Number, BuildingLevel)));

	return true;
}

FCastleBuildingDescriptor ACastleBuildingBase::GetBuildingDescriptor() const
{
	FCastleBuildingDescriptor Descriptor;
	Descriptor.BuildingClass = GetClass();
	Descriptor.BuildingType = BuildingType;
	Descriptor.Color = Color;
	Descriptor.Number = Number;
	Descriptor.Level = BuildingLevel;
	return Descriptor;
}

TSubclassOf<ACastleBuildingBase> ACastleBuildingBase::GetClassForLevel(int32 InLevel) const
{
	// 基类没有具体等级类，具体建筑族必须覆盖此函数才能参与合成替换。
	return nullptr;
}

bool ACastleBuildingBase::CanMergeToNextLevel() const
{
	// 4 级是策划案规定的自动合成上限，5 级可以由后续奖励系统直接生成。
	if (BuildingType == ECastleBuildingType::Road || BuildingLevel < 1 || BuildingLevel >= 4)
	{
		return false;
	}

	return GetClassForLevel(BuildingLevel + 1) != nullptr;
}

bool ACastleBuildingBase::CanMergeWith(const ACastleBuildingBase* Other) const
{
	if (Other == nullptr || Other == this || !CanMergeToNextLevel())
	{
		return false;
	}

	if (BuildingType != Other->BuildingType || Color != Other->Color || Number != Other->Number || BuildingLevel != Other->BuildingLevel)
	{
		return false;
	}

	// 通过同一等级映射类判断是否属于同一个建筑族，防止不同功能建筑误合成。
	return GetClassForLevel(BuildingLevel) == Other->GetClassForLevel(Other->BuildingLevel);
}

int32 ACastleBuildingBase::GetExpectedFootprintCellCount(int32 InLevel)
{
	if (InLevel < 1 || InLevel > 5)
	{
		return 0;
	}

	// 等级 1、2、3、4 分别对应 1、2、4、8 个合成方块；5 级由奖励直接升级，
	// 仍然代表一座建筑，因此恢复为单格占用而不是继续扩大合成形状。
	return InLevel == 5 ? 1 : (1 << (InLevel - 1));
}

void ACastleBuildingBase::ConfigurePrototypeVisual(float InCellSize, float InLayerHeight)
{
	if (PrototypeMesh == nullptr || PrototypeLabel == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑占位可视化配置失败：组件为空。"));
		return;
	}

	if (InCellSize <= 0.0f || InLayerHeight <= 0.0f)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑占位可视化配置失败：CellSize 和 LayerHeight 必须大于 0。"));
		return;
	}

	if (OccupiedCoordinates.Num() == 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑占位可视化配置失败：没有占用坐标。"));
		return;
	}

	FIntVector Minimum = OccupiedCoordinates[0];
	FIntVector Maximum = OccupiedCoordinates[0];
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		Minimum.X = FMath::Min(Minimum.X, Coordinate.X);
		Minimum.Y = FMath::Min(Minimum.Y, Coordinate.Y);
		Minimum.Z = FMath::Min(Minimum.Z, Coordinate.Z);
		Maximum.X = FMath::Max(Maximum.X, Coordinate.X);
		Maximum.Y = FMath::Max(Maximum.Y, Coordinate.Y);
		Maximum.Z = FMath::Max(Maximum.Z, Coordinate.Z);
	}

	const FIntVector Dimensions = Maximum - Minimum + FIntVector(1, 1, 1);
	// BasicShapes/Cube 的原始边长是 100 Unreal Units，因此按网格尺寸换算缩放。
	PrototypeMesh->SetRelativeScale3D(FVector(
		static_cast<float>(Dimensions.X) * InCellSize / 100.0f * 0.86f,
		static_cast<float>(Dimensions.Y) * InCellSize / 100.0f * 0.86f,
		static_cast<float>(Dimensions.Z) * InLayerHeight / 100.0f * 0.72f));

	if (PrototypeLabelPlate != nullptr)
	{
		// 标签底牌贴在建筑占位立方体最高面上方，只按占用范围计算宽高，
		// 不继承 PrototypeMesh 的缩放，合成后的 Lv2/Lv3 标签仍保持可读。
		const float BuildingHeight = static_cast<float>(Dimensions.Z) * InLayerHeight * 0.72f;
		const float LabelPlateWidth = FMath::Min(static_cast<float>(Dimensions.X) * InCellSize * 0.72f, 260.0f);
		const float LabelPlateDepth = FMath::Min(static_cast<float>(Dimensions.Y) * InCellSize * 0.72f, 180.0f);
		PrototypeLabelPlate->SetRelativeLocation(FVector(0.0f, 0.0f, BuildingHeight * 0.5f + 8.0f));
		PrototypeLabelPlate->SetRelativeScale3D(FVector(LabelPlateWidth / 100.0f, LabelPlateDepth / 100.0f, 0.035f));

		if (UMaterialInterface* PlateBaseMaterial = PrototypeLabelPlate->GetMaterial(0))
		{
			UMaterialInstanceDynamic* PlateMaterial = UMaterialInstanceDynamic::Create(PlateBaseMaterial, this);
			if (PlateMaterial != nullptr)
			{
				PlateMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.015f, 0.015f, 0.02f, 1.0f));
				PrototypeLabelPlate->SetMaterial(0, PlateMaterial);
			}
		}
	}

	if (PrototypeLabel != nullptr)
	{
		const float BuildingHeight = static_cast<float>(Dimensions.Z) * InLayerHeight * 0.72f;
		PrototypeLabel->SetRelativeLocation(FVector(0.0f, 0.0f, BuildingHeight * 0.5f + 11.0f));
		PrototypeLabel->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	}

	FLinearColor DisplayColor = FLinearColor::Gray;
	switch (Color)
	{
	case ECastleTileColor::Red:
		DisplayColor = FLinearColor(0.85f, 0.12f, 0.12f, 1.0f);
		break;
	case ECastleTileColor::Blue:
		DisplayColor = FLinearColor(0.10f, 0.35f, 0.90f, 1.0f);
		break;
	case ECastleTileColor::Green:
		DisplayColor = FLinearColor(0.10f, 0.75f, 0.25f, 1.0f);
		break;
	case ECastleTileColor::Yellow:
		DisplayColor = FLinearColor(0.95f, 0.70f, 0.08f, 1.0f);
		break;
	case ECastleTileColor::Purple:
		DisplayColor = FLinearColor(0.60f, 0.20f, 0.85f, 1.0f);
		break;
	default:
		DisplayColor = FLinearColor(0.35f, 0.35f, 0.38f, 1.0f);
		break;
	}

	if (UMaterialInterface* BaseMaterial = PrototypeMesh->GetMaterial(0))
	{
		UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		if (DynamicMaterial != nullptr)
		{
			DynamicMaterial->SetVectorParameterValue(TEXT("Color"), DisplayColor);
			PrototypeMesh->SetMaterial(0, DynamicMaterial);
		}
	}
}

