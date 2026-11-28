// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleGroundTileActor.h"
#include "Castle/Rendering/CastleRenderingConstants.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

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

	// 地块实体受光，数字底板的明度由界面语义决定。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(CastleRenderingConstants::ToonBasicColorMaterialPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlateFinder(CastleRenderingConstants::InfoUnlitColorMaterialPath);
	PrototypeMaterial = MaterialFinder.Object;
	NumberPlateMaterial = PlateFinder.Object;
}

bool ACastleGroundTileActor::InitializeGroundCell(const FCastleGroundCell& InGroundCell, const float GroundTileSize, const float LayerHeight)
{
	if (!PrototypeMaterial || !NumberPlateMaterial)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("地块 %s 缺少显式材质 %s 或 %s。"), *GetPathName(),
			CastleRenderingConstants::ToonBasicColorMaterialPath, CastleRenderingConstants::InfoUnlitColorMaterialPath);
		return false;
	}
	if (bInitialized || GroundTileSize <= 0.0f || LayerHeight <= 0.0f
		|| PrototypeCubeMesh == nullptr
		|| TileMesh == nullptr || NumberPlate == nullptr || NumberLabel == nullptr)
	{
		return false;
	}

	// 格面的显示色与摆放属性必须相符，先校验再创建材质和启用格面碰撞。
	FLinearColor TileTint;
	if (!CastleTileColors::TryGetGridTint(InGroundCell.Color, TileTint))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("地块 %s 在坐标 %s 使用了无效色值 %d。"),
			*GetPathName(), *InGroundCell.Coordinate.ToString(), static_cast<int32>(InGroundCell.Color));
		return false;
	}

	UMaterialInstanceDynamic* TileMaterial = UMaterialInstanceDynamic::Create(PrototypeMaterial, TileMesh);
	UMaterialInstanceDynamic* PlateMaterial = UMaterialInstanceDynamic::Create(NumberPlateMaterial, NumberPlate);
	if (TileMaterial == nullptr || PlateMaterial == nullptr)
	{
		return false;
	}
	TileMaterial->SetVectorParameterValue(TEXT("Color"), TileTint);
	PlateMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.015f, 0.015f, 0.02f, 1.0f));

	GroundCell = InGroundCell;
	// 立方体基础网格边长为 100 cm；棋盘把实体格面精确缩放为 500 cm，
	// 相邻中心距由棋盘的 20 cm 间隙额外提供，不再用经验比例缩小地块。
	TileMesh->SetRelativeScale3D(FVector(GroundTileSize / 100.0f, GroundTileSize / 100.0f, 0.12f));
	TileMesh->SetStaticMesh(PrototypeCubeMesh);
	TileMesh->SetMaterial(0, TileMaterial);
	TileMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	// 地块只保留中心射线格面检测，其余对象通道都忽略；棋盘地面不属于单位导航表面。
	TileMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	TileMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	TileMesh->SetCanEverAffectNavigation(false);
	TileMesh->SetVisibility(true, true);

	// 底牌位于薄地块上方；尺寸和间距都沿用棋盘现有的局部坐标单位。
	const float TileHalfHeight = 100.0f * 0.12f * 0.5f;
	const float NumberPlateHalfHeight = 100.0f * 0.035f * 0.5f;
	const float NumberPlateCenterHeight = TileHalfHeight + NumberPlateHalfHeight + 1.0f;
	NumberPlate->SetRelativeLocation(FVector(0.0f, 0.0f, NumberPlateCenterHeight));
	NumberPlate->SetRelativeScale3D(FVector(GroundTileSize / 100.0f * 0.62f, GroundTileSize / 100.0f * 0.62f, 0.035f));
	NumberPlate->SetStaticMesh(PrototypeCubeMesh);
	NumberPlate->SetMaterial(0, PlateMaterial);
	NumberPlate->SetVisibility(true, true);

	NumberLabel->SetWorldSize(GroundTileSize * 0.32f);
	NumberLabel->SetXScale(1.15f);
	NumberLabel->SetYScale(1.15f);
	NumberLabel->SetText(FText::AsNumber(InGroundCell.Number));
	NumberLabel->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	NumberLabel->SetRelativeLocation(FVector(0.0f, 0.0f, NumberPlateCenterHeight + NumberPlateHalfHeight + 0.5f));
	NumberLabel->SetVisibility(true, true);

	bInitialized = true;
	return true;
}

void ACastleGroundTileActor::SetGroundPresentationHidden(const bool bInHidden)
{
 // 只改表现与专用射线通道，不重建网格，不触碰任何导航设置。
 TileMesh->SetVisibility(!bInHidden);
 NumberPlate->SetVisibility(!bInHidden);
 NumberLabel->SetVisibility(!bInHidden);
 TileMesh->SetCollisionResponseToChannel(ECC_Visibility, bInHidden ? ECR_Ignore : ECR_Block);
}
