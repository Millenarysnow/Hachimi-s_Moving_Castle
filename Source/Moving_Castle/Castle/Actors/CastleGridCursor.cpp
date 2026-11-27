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

	/** 全息材质、专属网格与必需参数均是运行时硬依赖，不使用引擎默认材质兜底。 */
	constexpr TCHAR PlacementMeshPath[] = TEXT("/Game/MovingCastle/FX/Placement/SM_CastlePlacementHoloBox.SM_CastlePlacementHoloBox");
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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlacementMeshFinder(PlacementMeshPath);
	if (!PlacementMeshFinder.Succeeded())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("网格全息指示器初始化失败：找不到专属网格资产 %s。指示器保持隐藏。"), PlacementMeshPath);
		return;
	}
	CursorMesh->SetStaticMesh(PlacementMeshFinder.Object);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlacementMaterialFinder(PlacementMaterialPath);
	if (!PlacementMaterialFinder.Succeeded())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("网格全息指示器初始化失败：找不到材质资产 %s。指示器保持隐藏。"), PlacementMaterialPath);
		return;
	}

	CursorMaterial = UMaterialInstanceDynamic::Create(PlacementMaterialFinder.Object, this);
	if (CursorMaterial == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("网格全息指示器初始化失败：无法从 %s 创建动态材质。指示器保持隐藏。"), PlacementMaterialPath);
		return;
	}

	// Initialize...AndGetIndex 会在参数真实存在时才成功，避免材质参数名拼写错误时静默显示默认外观。
	const bool bHasHoloColor = CursorMaterial->InitializeVectorParameterAndGetIndex(
		TEXT("HoloColor"), HoloPlaceColor, HoloColorParameterIndex);
	const bool bHasSupportNormal = CursorMaterial->InitializeVectorParameterAndGetIndex(
		TEXT("SupportNormal"), FLinearColor(0.0f, 0.0f, 1.0f, 0.0f), SupportNormalParameterIndex);
	const bool bHasInvalidMode = CursorMaterial->InitializeScalarParameterAndGetIndex(
		TEXT("InvalidMode"), 0.0f, InvalidModeParameterIndex);
	if (!bHasHoloColor || !bHasSupportNormal || !bHasInvalidMode)
	{
		UE_LOG(LogMoving_Castle, Error,
			TEXT("网格全息指示器初始化失败：材质 %s 缺少必需参数 HoloColor=%d、SupportNormal=%d、InvalidMode=%d。指示器保持隐藏。"),
			PlacementMaterialPath, bHasHoloColor, bHasSupportNormal, bHasInvalidMode);
		CursorMaterial = nullptr;
		return;
	}

	CursorMesh->SetMaterial(0, CursorMaterial);

	// 光标只负责视觉反馈，所有环境参与标记都显式关闭，射线仍可命中其后的真实建造面。
	CursorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CursorMesh->SetCanEverAffectNavigation(false);
	CursorMesh->SetCastShadow(false);
	CursorMesh->SetAffectDistanceFieldLighting(false);
	CursorMesh->SetVisibleInRayTracing(false);
	CursorMesh->SetReceivesDecals(false);
	CursorMesh->SetRenderCustomDepth(false);
}

void ACastleGridCursor::ApplyTarget(const FCastleGridCursorTarget& Target, bool bCanPlace)
{
	if (!Target.bValid)
	{
		HideCursor();
		return;
	}

	// 资源或参数未成功初始化时保持隐藏，并记录失效目标，绝不退回不透明默认材质。
	if (CursorMesh == nullptr || CursorMaterial == nullptr || HoloColorParameterIndex == INDEX_NONE
		|| SupportNormalParameterIndex == INDEX_NONE || InvalidModeParameterIndex == INDEX_NONE)
	{
		UE_LOG(LogMoving_Castle, Error,
			TEXT("网格全息指示器无法应用目标：必需网格、材质或参数索引失效，材质路径 %s。指示器保持隐藏。"),
			PlacementMaterialPath);
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
