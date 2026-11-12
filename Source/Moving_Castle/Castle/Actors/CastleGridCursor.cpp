// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleGridCursor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMeshFinder.Succeeded())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("网格指示器初始化失败：找不到 UE BasicShapes/Cube。"));
		return;
	}
	CursorMesh->SetStaticMesh(CubeMeshFinder.Object);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!BasicMaterialFinder.Succeeded())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("网格指示器初始化失败：找不到 UE BasicShapeMaterial。"));
		return;
	}

	CursorMaterial = UMaterialInstanceDynamic::Create(BasicMaterialFinder.Object, this);
	if (CursorMaterial == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("网格指示器初始化失败：无法创建动态材质。"));
		return;
	}

	// 使用高亮浅蓝色，保证在当前阶段的红、蓝、绿、黄地面上都能区分。
	CursorMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.12f, 0.78f, 1.0f, 1.0f));
	CursorMesh->SetMaterial(0, CursorMaterial);
}

void ACastleGridCursor::ApplyTarget(const FCastleGridCursorTarget& Target, bool bCanPlace)
{
	if (!Target.bValid)
	{
		HideCursor();
		return;
	}

	// 非法卡牌仍高亮玩家实际指向的面，明确反馈失败原因来自这个面而非其他邻格。
	if (CursorMaterial != nullptr)
	{
		CursorMaterial->SetVectorParameterValue(TEXT("Color"), bCanPlace
			? FLinearColor(0.12f, 0.78f, 1.0f, 1.0f) : FLinearColor(1.0f, 0.08f, 0.04f, 1.0f));
	}
	SetActorTransform(Target.IndicatorTransform);
	SetActorHiddenInGame(false);
}

void ACastleGridCursor::HideCursor()
{
	SetActorHiddenInGame(true);
}
