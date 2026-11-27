// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleGridCursor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 可放态使用清透青蓝，不可放态使用高对比警示暖红。 */
	const FLinearColor HoloPlaceColor(0.16f, 0.88f, 1.0f, 1.0f);
	const FLinearColor HoloInvalidColor(1.0f, 0.20f, 0.14f, 1.0f);

	/** 引擎标准盒的六个面均有独立的 0–1 UV，生成器会核对该约定。 */
	constexpr TCHAR PlacementMeshPath[] = TEXT("/Engine/BasicShapes/Cube.Cube");
	/** 全息材质及其必需参数为运行时硬依赖。 */
	constexpr TCHAR PlacementMaterialPath[] = TEXT("/Game/MovingCastle/FX/Placement/M_CastlePlacementHoloBox.M_CastlePlacementHoloBox");
}

ACastleGridCursor::ACastleGridCursor()
{
	// 指示器只在控制器更新目标时改变变换，不需要独立 Tick。
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);

	CursorMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CursorMesh"));
	SetRootComponent(CursorMesh);
	CursorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CursorMesh->SetCanEverAffectNavigation(false);
	CursorMesh->SetVisibility(true, true);
	// 无论资产加载结果如何，光标组件都不能参与环境渲染和射线查询。
	CursorMesh->SetCastShadow(false);
	CursorMesh->SetAffectDistanceFieldLighting(false);
	CursorMesh->SetVisibleInRayTracing(false);
	CursorMesh->SetReceivesDecals(false);
	CursorMesh->SetRenderCustomDepth(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlacementMeshFinder(PlacementMeshPath);
	if (!PlacementMeshFinder.Succeeded())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("网格全息指示器初始化失败：找不到盒体网格资产 %s。指示器保持隐藏。"), PlacementMeshPath);
		return;
	}
	CursorMesh->SetStaticMesh(PlacementMeshFinder.Object);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlacementMaterialFinder(PlacementMaterialPath);
	if (!PlacementMaterialFinder.Succeeded())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("网格全息指示器初始化失败：找不到材质资产 %s。指示器保持隐藏。"), PlacementMaterialPath);
		return;
	}
	CursorMaterialTemplate = PlacementMaterialFinder.Object;
}

void ACastleGridCursor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// 构造阶段已报告缺失的资产。此时仍保持隐藏，也不创建材质实例。
	if (CursorMaterialTemplate == nullptr)
	{
		return;
	}

	// MID 可为不存在的名称创建覆盖值，所以必须直接检查父材质定义的参数。
	static const FName HoloColorName(TEXT("HoloColor"));
	static const FName SupportNormalName(TEXT("SupportNormal"));
	static const FName InvalidModeName(TEXT("InvalidMode"));
	FLinearColor VectorValue;
	float ScalarValue = 0.0f;
	const bool bHasHoloColor = CursorMaterialTemplate->GetVectorParameterValue(FMaterialParameterInfo(HoloColorName), VectorValue);
	const bool bHasSupportNormal = CursorMaterialTemplate->GetVectorParameterValue(FMaterialParameterInfo(SupportNormalName), VectorValue);
	const bool bHasInvalidMode = CursorMaterialTemplate->GetScalarParameterValue(FMaterialParameterInfo(InvalidModeName), ScalarValue);
	if (!bHasHoloColor || !bHasSupportNormal || !bHasInvalidMode)
	{
		UE_LOG(LogMoving_Castle, Error,
			TEXT("网格全息指示器初始化失败：材质 %s 缺少必需参数 HoloColor=%d、SupportNormal=%d、InvalidMode=%d。指示器保持隐藏。"),
			PlacementMaterialPath, bHasHoloColor, bHasSupportNormal, bHasInvalidMode);
		return;
	}

	CursorMaterial = UMaterialInstanceDynamic::Create(CursorMaterialTemplate, this);
	if (CursorMaterial == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("网格全息指示器初始化失败：无法从 %s 创建动态材质。指示器保持隐藏。"), PlacementMaterialPath);
		return;
	}

	// 正式参数存在后，缓存当前实例的索引供控制器每帧更新。
	const bool bHasColorIndex = CursorMaterial->InitializeVectorParameterAndGetIndex(HoloColorName, HoloPlaceColor, HoloColorParameterIndex);
	const bool bHasNormalIndex = CursorMaterial->InitializeVectorParameterAndGetIndex(
		SupportNormalName, FLinearColor(0.0f, 0.0f, 1.0f, 0.0f), SupportNormalParameterIndex);
	const bool bHasModeIndex = CursorMaterial->InitializeScalarParameterAndGetIndex(InvalidModeName, 0.0f, InvalidModeParameterIndex);
	if (!bHasColorIndex || !bHasNormalIndex || !bHasModeIndex)
	{
		UE_LOG(LogMoving_Castle, Error,
			TEXT("网格全息指示器初始化失败：无法缓存材质 %s 的参数索引 HoloColor=%d、SupportNormal=%d、InvalidMode=%d。指示器保持隐藏。"),
			PlacementMaterialPath, bHasColorIndex, bHasNormalIndex, bHasModeIndex);
		CursorMaterial = nullptr;
		return;
	}

	CursorMesh->SetMaterial(0, CursorMaterial);
}

void ACastleGridCursor::ApplyTarget(const FCastleGridCursorTarget& Target, bool bCanPlace)
{
	if (!Target.bValid)
	{
		HideCursor();
		return;
	}

	// 初始化失败已经写入 Error。保持隐藏，避免控制器每帧重复报告同一故障。
	if (CursorMaterial == nullptr)
	{
		HideCursor();
		return;
	}

	// 先将目标世界变换应用到根组件，再把来源面的反向法线变换回材质的盒体局部空间。
	SetActorTransform(Target.IndicatorTransform);
	const FVector SupportNormalLocal = GetActorTransform().InverseTransformVectorNoScale(-Target.WorldNormal).GetSafeNormal();
	const bool bColorSet = CursorMaterial->SetVectorParameterByIndex(
		HoloColorParameterIndex, bCanPlace ? HoloPlaceColor : HoloInvalidColor);
	const bool bModeSet = CursorMaterial->SetScalarParameterByIndex(InvalidModeParameterIndex, bCanPlace ? 0.0f : 1.0f);
	const bool bSupportNormalSet = CursorMaterial->SetVectorParameterByIndex(
		SupportNormalParameterIndex, FLinearColor(SupportNormalLocal.X, SupportNormalLocal.Y, SupportNormalLocal.Z, 0.0f));
	if (!bColorSet || !bModeSet || !bSupportNormalSet)
	{
		UE_LOG(LogMoving_Castle, Error,
			TEXT("网格全息指示器应用目标失败：材质 %s 的参数索引写入失败（HoloColor=%d、InvalidMode=%d、SupportNormal=%d）。指示器保持隐藏。"),
			PlacementMaterialPath, bColorSet, bModeSet, bSupportNormalSet);
		HideCursor();
		return;
	}
	SetActorHiddenInGame(false);
}

void ACastleGridCursor::HideCursor()
{
	SetActorHiddenInGame(true);
}
