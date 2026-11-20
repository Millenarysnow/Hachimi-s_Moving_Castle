// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleTowerSideGridComponent.h"

#include "Castle/PCG/CastleDeterministicPCGNoise.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 四个面共享塔身坐标但各自有朝外轴；不从塔冠、美术法线或包围盒推断格子。 */
	struct FTowerFace
	{
		ECastleGridFace Face;
		FIntVector Outward;
	};
	const FTowerFace TowerFaces[] = {
		{ECastleGridFace::NegativeX, FIntVector(-1, 0, 0)}, {ECastleGridFace::NegativeY, FIntVector(0, -1, 0)},
		{ECastleGridFace::PositiveX, FIntVector(1, 0, 0)}, {ECastleGridFace::PositiveY, FIntVector(0, 1, 0)}
	};

	/** 合法色与原地块一致；采样直接索引配对表，不需要灰色或无效枚举兜底。 */
	struct FTowerTileColor
	{
		ECastleTileColor Color;
		FLinearColor Tint;
	};
	constexpr FTowerTileColor TowerTileColors[] = {
		{ECastleTileColor::Red, FLinearColor(0.75f, 0.08f, 0.08f)},
		{ECastleTileColor::Blue, FLinearColor(0.06f, 0.25f, 0.80f)},
		{ECastleTileColor::Green, FLinearColor(0.05f, 0.58f, 0.18f)},
		{ECastleTileColor::Yellow, FLinearColor(0.90f, 0.60f, 0.04f)},
		{ECastleTileColor::Purple, FLinearColor(0.48f, 0.12f, 0.72f)}
	};

	/** 面板局部 X 朝外、局部 Z 朝上；保留墙面装饰和相邻格间的缝隙。 */
	constexpr float PanelThickness = 8.0f;
	constexpr float MeshSize = 100.0f;
}

UCastleTowerSideGridComponent::UCastleTowerSideGridComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	PanelMesh = Mesh.Object;
	PanelMaterial = Material.Object;
}

bool UCastleTowerSideGridComponent::Initialize(const FIntVector& BaseCell, const int32 LayerCount, const float CellWidth,
	const float LayerHeight, const float SurfaceOffset, const int32 Seed, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	AActor* Tower = GetOwner();
	USceneComponent* Root = IsValid(Tower) ? Tower->GetRootComponent() : nullptr;
	if (bInitialized || !IsValid(Root) || !IsValid(PanelMesh) || !IsValid(PanelMaterial)
		|| LayerCount < 1 || LayerCount > 64 || !FMath::IsFinite(CellWidth) || CellWidth < 20.0f
		|| !FMath::IsFinite(LayerHeight) || LayerHeight < 20.0f || !FMath::IsFinite(SurfaceOffset)
		|| SurfaceOffset <= CellWidth * 0.5f || SurfaceOffset >= CellWidth)
	{
		OutReason = FText::FromString(FString::Printf(
			TEXT("主塔 %s 侧格初始化失败：塔根、板材资源（Cube / BasicShapeMaterial）、层数或侧面距离不合法；侧面距离须处于半格至一格之间。"),
			*GetPathNameSafe(Tower)));
		return false;
	}

	// 每个侧面与层号独立采样；同一局的显隐、编辑或房间变化都不重新抽取。
	const float PanelWidth = CellWidth * 0.84f;
	const float PanelHeight = LayerHeight * 0.88f;
	Cells.Reserve(LayerCount * UE_ARRAY_COUNT(TowerFaces));
	for (int32 Layer = 0; Layer < LayerCount; ++Layer)
	{
		for (const FTowerFace& Face : TowerFaces)
		{
			const FIntVector SampleCell(Face.Outward.X, Face.Outward.Y, Layer);
			const int32 ColorIndex = FMath::FloorToInt(UCastleDeterministicPCGNoise::SampleNoiseValue(
				Seed, SampleCell, TEXT("TowerSideColor")) * UE_ARRAY_COUNT(TowerTileColors));
			FCastleTowerSideCell& Cell = Cells.AddDefaulted_GetRef();
			Cell.TowerCell = BaseCell + FIntVector(0, 0, Layer);
			Cell.Face = Face.Face;
			Cell.Color = TowerTileColors[ColorIndex].Color;
			Cell.Number = 1 + FMath::FloorToInt(UCastleDeterministicPCGNoise::SampleNoiseValue(
				Seed, SampleCell, TEXT("TowerSideNumber")) * 9);

			const FVector Outward(Face.Outward);
			const FRotator Rotation = FRotationMatrix::MakeFromXZ(Outward, FVector::UpVector).Rotator();
			const FVector Center = Outward * SurfaceOffset + FVector(0, 0, (Layer + 0.5) * LayerHeight);
			UStaticMeshComponent* Panel = NewObject<UStaticMeshComponent>(Tower);
			UTextRenderComponent* Label = NewObject<UTextRenderComponent>(Tower);
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(PanelMaterial, Panel);
			if (!Panel || !Label || !Material)
			{
				OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 塔身格 %s 面 %d 无法创建表现组件或动态材质。"),
					*GetPathNameSafe(Tower), *Cell.TowerCell.ToString(), static_cast<int32>(Cell.Face)));
				return false;
			}
			Material->SetVectorParameterValue(TEXT("Color"), TowerTileColors[ColorIndex].Tint);

			// 先配置碰撞和导航资格再注册，避免短暂暴露建造面为实体障碍或行走几何。
			Panel->SetupAttachment(Root);
			Panel->SetRelativeLocationAndRotation(Center, Rotation);
			Panel->SetRelativeScale3D(FVector(PanelThickness, PanelWidth, PanelHeight) / MeshSize);
			Panel->SetCanEverAffectNavigation(false);
			Panel->SetGenerateOverlapEvents(false);
			Panel->SetCollisionResponseToAllChannels(ECR_Ignore);
			Panel->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Panel->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Panel->SetCastShadow(false);
			Panel->SetStaticMesh(PanelMesh);
			Panel->SetMaterial(0, Material);

			// 文字与面板分别挂到塔根，保持数字直立，不继承薄片的压扁缩放。
			Label->SetupAttachment(Root);
			Label->SetRelativeLocationAndRotation(Center + Outward * (PanelThickness * 0.5f + 3.0f), Rotation);
			Label->SetCanEverAffectNavigation(false);
			Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Label->SetHorizontalAlignment(EHTA_Center);
			Label->SetVerticalAlignment(EVRTA_TextCenter);
			Label->SetTextRenderColor(FColor::White);
			Label->SetWorldSize(PanelWidth * 0.50f);
			Label->SetText(FText::AsNumber(Cell.Number));

			Cell.Panel = Panel;
			Cell.Label = Label;
			Tower->AddInstanceComponent(Panel);
			Tower->AddInstanceComponent(Label);
			Panel->RegisterComponent();
			Label->RegisterComponent();
		}
	}
	bInitialized = true;
	return ValidatePresentation(OutReason);
}

bool UCastleTowerSideGridComponent::ValidatePresentation(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!bInitialized || !IsValid(GetOwner()))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 未初始化侧格。"), *GetPathNameSafe(GetOwner())));
		return false;
	}
	for (const FCastleTowerSideCell& Cell : Cells)
	{
		if (!IsValid(Cell.Panel) || !Cell.Panel->IsRegistered() || !IsValid(Cell.Label) || !Cell.Label->IsRegistered())
		{
			OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 侧格 %s 面 %d 的面板或数字未注册。"),
				*GetPathNameSafe(GetOwner()), *Cell.TowerCell.ToString(), static_cast<int32>(Cell.Face)));
			return false;
		}
	}
	return true;
}

bool UCastleTowerSideGridComponent::SetPresentationHidden(const bool bHidden, FText& OutReason)
{
	// 先核对整组面板再更改，避免只隐藏其中一面后才发现组件失效。
	if (!ValidatePresentation(OutReason)) { return false; }
	if (bPresentationHidden != bHidden)
	{
		for (const FCastleTowerSideCell& Cell : Cells)
		{
			Cell.Panel->SetVisibility(!bHidden);
			Cell.Panel->SetCollisionResponseToChannel(ECC_Visibility, bHidden ? ECR_Ignore : ECR_Block);
			Cell.Label->SetVisibility(!bHidden);
		}
		bPresentationHidden = bHidden;
	}
	return true;
}

const FCastleTowerSideCell* UCastleTowerSideGridComponent::FindCell(const FIntVector& TowerCell, const ECastleGridFace Face) const
{
	return Cells.FindByPredicate([&TowerCell, Face](const FCastleTowerSideCell& Cell)
	{
		return Cell.TowerCell == TowerCell && Cell.Face == Face;
	});
}

bool UCastleTowerSideGridComponent::TryResolveHit(const FHitResult& Hit, FCastleGridCursorTarget& OutTarget) const
{
	if (!bInitialized || bPresentationHidden || Hit.GetActor() != GetOwner()) { return false; }
	const FCastleTowerSideCell* Cell = Cells.FindByPredicate([&Hit](const FCastleTowerSideCell& Entry)
	{
		return IsValid(Entry.Panel) && Entry.Panel.Get() == Hit.GetComponent();
	});
	if (!Cell) { return false; }

	const FTransform& PanelTransform = Cell->Panel->GetComponentTransform();
	const FVector Normal = PanelTransform.GetUnitAxis(EAxis::X);
	if (FVector::DotProduct(Normal, Hit.ImpactNormal) < 0.99) { return false; }
	for (const FTowerFace& Face : TowerFaces)
	{
		if (Face.Face != Cell->Face) { continue; }
		OutTarget.Cell = Cell->TowerCell;
		OutTarget.Face = Cell->Face;
		OutTarget.AdjacentCell = Cell->TowerCell + Face.Outward;
		OutTarget.Surface = ECastleGridCursorSurface::MainTowerSide;
		OutTarget.WorldNormal = Normal;
		// 使用边缘色条反馈可放/不可放，不用原来的整格不透明高亮盖掉塔侧的颜色和数字。
		OutTarget.IndicatorTransform = FTransform(PanelTransform.GetRotation(),
			PanelTransform.TransformPosition(FVector(85.0, 0.0, -43.0)), PanelTransform.GetScale3D() * FVector(0.32, 0.92, 0.07));
		OutTarget.bValid = true;
		return true;
	}
	return false;
}
