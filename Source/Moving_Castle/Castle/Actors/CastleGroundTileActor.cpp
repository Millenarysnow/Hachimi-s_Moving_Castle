// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleGroundTileActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 返回格子颜色对应的占位材质颜色。 */
	FLinearColor GetGroundTileColor(const ECastleTileColor Color)
	{
		switch (Color)
		{
		case ECastleTileColor::Red:
			return FLinearColor(0.75f, 0.08f, 0.08f, 1.0f);
		case ECastleTileColor::Blue:
			return FLinearColor(0.06f, 0.25f, 0.80f, 1.0f);
		case ECastleTileColor::Green:
			return FLinearColor(0.05f, 0.58f, 0.18f, 1.0f);
		case ECastleTileColor::Yellow:
			return FLinearColor(0.90f, 0.60f, 0.04f, 1.0f);
		case ECastleTileColor::Purple:
			return FLinearColor(0.48f, 0.12f, 0.72f, 1.0f);
		case ECastleTileColor::Gray:
		default:
			return FLinearColor(0.42f, 0.46f, 0.52f, 1.0f);
		}
	}
}

ACastleGroundTileActor::ACastleGroundTileActor()
{
	// 每个棋盘格是独立 Actor；根节点保留棋盘整体变换，子组件只描述格内布局。
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	TileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TileMesh"));
	TileMesh->SetupAttachment(SceneRoot);
	TileMesh->SetMobility(EComponentMobility::Movable);
	TileMesh->SetCanEverAffectNavigation(false);
	TileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TileMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	TileMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	NumberPlate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NumberPlate"));
	NumberPlate->SetupAttachment(SceneRoot);
	NumberPlate->SetMobility(EComponentMobility::Movable);
	NumberPlate->SetCanEverAffectNavigation(false);
	NumberPlate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NumberPlate->SetVisibility(false);

	NumberLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NumberLabel"));
	NumberLabel->SetupAttachment(SceneRoot);
	NumberLabel->SetCanEverAffectNavigation(false);
	NumberLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NumberLabel->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	NumberLabel->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	NumberLabel->SetTextRenderColor(FColor::White);
	NumberLabel->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		PrototypeCubeMesh = CubeMeshFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (MaterialFinder.Succeeded())
	{
		PrototypeMaterial = MaterialFinder.Object;
	}
}

bool ACastleGroundTileActor::InitializeGroundCell(const FCastleGroundCell& InGroundCell, const float CellSize, const float LayerHeight)
{
	if (bInitialized || CellSize <= 0.0f || LayerHeight <= 0.0f
		|| PrototypeCubeMesh == nullptr || PrototypeMaterial == nullptr
		|| TileMesh == nullptr || NumberPlate == nullptr || NumberLabel == nullptr)
	{
		return false;
	}

	UMaterialInstanceDynamic* TileMaterial = UMaterialInstanceDynamic::Create(PrototypeMaterial, TileMesh);
	UMaterialInstanceDynamic* PlateMaterial = UMaterialInstanceDynamic::Create(PrototypeMaterial, NumberPlate);
	if (TileMaterial == nullptr || PlateMaterial == nullptr)
	{
		return false;
	}
	TileMaterial->SetVectorParameterValue(TEXT("Color"), GetGroundTileColor(InGroundCell.Color));
	PlateMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.015f, 0.015f, 0.02f, 1.0f));

	GroundCell = InGroundCell;
	TileMesh->SetRelativeScale3D(FVector(CellSize / 100.0f * 0.92f, CellSize / 100.0f * 0.92f, 0.12f));
	TileMesh->SetStaticMesh(PrototypeCubeMesh);
	TileMesh->SetMaterial(0, TileMaterial);
	TileMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TileMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	// 此时 Actor 位置、网格、碰撞和包围盒均已完成，导航只会收到最终地块几何。
	TileMesh->SetCanEverAffectNavigation(true);
	TileMesh->SetVisibility(true, true);

	// 底牌位于薄地块上方；尺寸和间距都沿用棋盘现有的局部坐标单位。
	const float TileHalfHeight = 100.0f * 0.12f * 0.5f;
	const float NumberPlateHalfHeight = 100.0f * 0.035f * 0.5f;
	const float NumberPlateCenterHeight = TileHalfHeight + NumberPlateHalfHeight + 1.0f;
	NumberPlate->SetRelativeLocation(FVector(0.0f, 0.0f, NumberPlateCenterHeight));
	NumberPlate->SetRelativeScale3D(FVector(CellSize / 100.0f * 0.62f, CellSize / 100.0f * 0.62f, 0.035f));
	NumberPlate->SetStaticMesh(PrototypeCubeMesh);
	NumberPlate->SetMaterial(0, PlateMaterial);
	NumberPlate->SetVisibility(true, true);

	NumberLabel->SetWorldSize(CellSize * 0.32f);
	NumberLabel->SetXScale(1.15f);
	NumberLabel->SetYScale(1.15f);
	NumberLabel->SetText(FText::AsNumber(InGroundCell.Number));
	NumberLabel->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	NumberLabel->SetRelativeLocation(FVector(0.0f, 0.0f, NumberPlateCenterHeight + NumberPlateHalfHeight + 0.5f));
	NumberLabel->SetVisibility(true, true);

	bInitialized = true;
	return true;
}
