// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleTowerSideGridComponent.h"

#include "Castle/PCG/CastleDeterministicPCGNoise.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "Moving_Castle.h"
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

	/** UE 基础 Plane 的本地 X/Y 边长均为 100 厘米，面法线沿本地 +Z。 */
	constexpr float MeshSize = 100.0f;
	// Texture 分支的 MI_Tower 使用独立底纹和字色，保持作者字色并按棋盘格设置底纹。
	const FMaterialParameterInfo FrameParameter(TEXT("Frame (S)"));
	const FMaterialParameterInfo BackgroundParameter(TEXT("BackgroundColor"));
	const FMaterialParameterInfo NumberColorParameter(TEXT("NumberColor"));
}

UCastleTowerSideGridComponent::UCastleTowerSideGridComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/ART/Material/MI_Tower.MI_Tower"));
	PanelMesh = Mesh.Object;
	PanelMaterial = Material.Object;
}

bool UCastleTowerSideGridComponent::Initialize(const FIntVector& BaseCell, const int32 LayerCount, const float CellWidth,
	const float GridCellSize, const float LayerHeight, const float SurfaceOffset, const int32 Seed, const int32 MaxNumber, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	AActor* Tower = GetOwner();
	USceneComponent* Root = IsValid(Tower) ? Tower->GetRootComponent() : nullptr;
	if (bInitialized || !IsValid(Root) || !IsValid(PanelMesh) || !IsValid(PanelMaterial)
		|| LayerCount < 1 || LayerCount > 64 || !FMath::IsFinite(CellWidth) || CellWidth < 20.0f
		|| !FMath::IsFinite(GridCellSize) || GridCellSize < CellWidth
		|| !FMath::IsFinite(LayerHeight) || LayerHeight < 20.0f || !FMath::IsFinite(SurfaceOffset)
		|| SurfaceOffset <= CellWidth * 0.5f || SurfaceOffset >= CellWidth)
	{
		OutReason = FText::FromString(FString::Printf(
			TEXT("主塔 %s 侧格初始化失败：塔根、Plane 网格、材质 /Game/ART/Material/MI_Tower、层数、格距或侧面距离无效；棋盘格距不得小于面板宽度，侧面距离须处于半格至一格之间。"),
			*GetPathNameSafe(Tower)));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	float Frame;
	FLinearColor Tint;
	if (!PanelMaterial->GetScalarParameterValue(FrameParameter, Frame)
		|| !PanelMaterial->GetVectorParameterValue(BackgroundParameter, Tint)
		|| !PanelMaterial->GetVectorParameterValue(NumberColorParameter, Tint))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 的侧格材质 %s 缺少 Frame (S)、BackgroundColor 或 NumberColor。"),
			*GetPathNameSafe(Tower), *PanelMaterial->GetPathName()));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}

	TowerBaseCell = BaseCell;
	TowerCellWidth = CellWidth;
	TowerGridCellSize = GridCellSize;
	TowerLayerHeight = LayerHeight;
	TowerSurfaceOffset = SurfaceOffset;
	RunSeed = Seed;
	bInitialized = AppendLayers(LayerCount, MaxNumber, OutReason);
	return bInitialized;
}

bool UCastleTowerSideGridComponent::ExpandToLayerCount(const int32 TargetLayerCount, const int32 MaxNumber, FText& OutReason)
{
	if (!ValidatePresentation(OutReason))
	{
		// ValidatePresentation 已按失效组件和属性写出完整 Error，调用层直接返回以免重复记录同一故障。
		return false;
	}
	if (TargetLayerCount <= GeneratedLayerCount || TargetLayerCount > 64)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 侧格扩层失败：目标层数 %d 必须大于当前层数 %d 且不超过 64。"),
			*GetPathNameSafe(GetOwner()), TargetLayerCount, GeneratedLayerCount));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	return AppendLayers(TargetLayerCount, MaxNumber, OutReason);
}

bool UCastleTowerSideGridComponent::AppendLayers(const int32 TargetLayerCount, const int32 MaxNumber, FText& OutReason)
{
	// 格面图集仅有九帧，生成之前拒绝非法上限。
	if (MaxNumber < 1 || MaxNumber > 9)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 的侧格数字上限 %d 无效（须为 1 至 9）。"),
			*GetPathNameSafe(GetOwner()), MaxNumber));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	AActor* Tower = GetOwner();
	USceneComponent* Root = Tower->GetRootComponent();
	TArray<FCastleTowerSideCell> Pending;
	Pending.Reserve((TargetLayerCount - GeneratedLayerCount) * UE_ARRAY_COUNT(TowerFaces));
	const auto Reject = [&](const FCastleTowerSideCell& Cell, const FString& Cause)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 塔身格 %s 面 %d 无法初始化，原因是%s"),
			*Tower->GetPathName(), *Cell.TowerCell.ToString(), static_cast<int32>(Cell.Face), *Cause));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		for (const FCastleTowerSideCell& Added : Pending)
		{
			if (Added.Panel) { Added.Panel->DestroyComponent(); }
		}
		return false;
	};
	// 每个侧面与层号独立采样，已发布面板从创建到扩层始终保留。
	const TConstArrayView<ECastleTileColor> Colors = CastleTileColors::GetPlayable();
	const float PanelWidth = TowerCellWidth * 0.84f;
	const float PanelHeight = TowerLayerHeight * 0.88f;
	for (int32 Layer = GeneratedLayerCount; Layer < TargetLayerCount; ++Layer)
	{
		for (const FTowerFace& Face : TowerFaces)
		{
			const FIntVector SampleCell(Face.Outward.X, Face.Outward.Y, Layer);
			const int32 ColorIndex = FMath::FloorToInt(UCastleDeterministicPCGNoise::SampleNoiseValue(
				RunSeed, SampleCell, TEXT("TowerSideColor")) * Colors.Num());
			FCastleTowerSideCell& Cell = Pending.AddDefaulted_GetRef();
			Cell.TowerCell = TowerBaseCell + FIntVector(0, 0, Layer);
			Cell.Face = Face.Face;
			Cell.Color = Colors[ColorIndex];
			Cell.Number = 1 + FMath::FloorToInt(UCastleDeterministicPCGNoise::SampleNoiseValue(
				RunSeed, SampleCell, TEXT("TowerSideNumber")) * MaxNumber);

			// 采样色值缺少材质映射时撤销本批次，避免发布无法正确显示的格面。
			FLinearColor PanelTint;
			if (!CastleTileColors::TryGetGridTint(Cell.Color, PanelTint))
			{
				return Reject(Cell, FString::Printf(TEXT("色值 %d 未配置格面材质色。"), static_cast<int32>(Cell.Color)));
			}

			const FVector Outward(Face.Outward);
			// 基础 Plane 的 V=0 在本地 -Y 边，让它朝塔顶，同时让正面法线 +Z 朝塔外。
			const FRotator Rotation = FRotationMatrix::MakeFromYZ(FVector::DownVector, Outward).Rotator();
			const FVector Center = Outward * TowerSurfaceOffset + FVector(0, 0, (Layer + 0.5) * TowerLayerHeight);
			UStaticMeshComponent* Panel = NewObject<UStaticMeshComponent>(Tower);
			Cell.Panel = Panel;
			if (!Panel)
			{
				return Reject(Cell, TEXT("无法创建格面组件。"));
			}
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(PanelMaterial, Panel);
			if (!Material)
			{
				return Reject(Cell, TEXT("无法基于 MI_Tower 创建格面动态材质。"));
			}
			// 3×3 图集帧从零编号，格面数字 1～9 映射到帧 0～8。
			Material->SetVectorParameterValue(BackgroundParameter.Name, PanelTint);
			Material->SetScalarParameterValue(FrameParameter.Name, static_cast<float>(Cell.Number - 1));

			// 先配置碰撞和导航资格再注册，避免短暂暴露建造面为实体障碍或行走几何。
			Panel->SetupAttachment(Root);
			Panel->SetRelativeLocationAndRotation(Center, Rotation);
			// Plane 的本地 X/Y 分别控制宽度和高度，本地 Z 保持单位缩放以留作其零厚度碰撞面。
			Panel->SetRelativeScale3D(FVector(PanelWidth, PanelHeight, MeshSize) / MeshSize);
			Panel->SetCanEverAffectNavigation(false);
			Panel->SetGenerateOverlapEvents(false);
			Panel->SetCollisionResponseToAllChannels(ECR_Ignore);
			Panel->SetCollisionResponseToChannel(ECC_Visibility, bPresentationHidden ? ECR_Ignore : ECR_Block);
			Panel->SetVisibility(!bPresentationHidden);
			Panel->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Panel->SetCastShadow(false);
			Panel->SetStaticMesh(PanelMesh);
			Panel->SetMaterial(0, Material);

			Tower->AddInstanceComponent(Panel);
			Panel->RegisterComponent();
			if (!Panel->IsRegistered())
			{
				return Reject(Cell, TEXT("无法注册纹理格面组件。"));
			}
		}
	}
	Cells.Append(MoveTemp(Pending));
	GeneratedLayerCount = TargetLayerCount;
	return true;
}

bool UCastleTowerSideGridComponent::ValidatePresentation(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!bInitialized || !IsValid(GetOwner()) || !IsValid(PanelMaterial))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 未初始化侧格或 MI_Tower 材质无效。"), *GetPathNameSafe(GetOwner())));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	FLinearColor ExpectedNumberColor;
	if (!PanelMaterial->GetVectorParameterValue(NumberColorParameter, ExpectedNumberColor))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 的侧格材质 %s 缺少 NumberColor。"),
			*GetPathNameSafe(GetOwner()), *PanelMaterial->GetPathName()));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	for (const FCastleTowerSideCell& Cell : Cells)
	{
		const bool bPanelValid = IsValid(Cell.Panel);
		UMaterialInstanceDynamic* Material = bPanelValid
			? Cast<UMaterialInstanceDynamic>(Cell.Panel->GetMaterial(0)) : nullptr;
		FLinearColor ExpectedTint = FLinearColor::Transparent;
		const bool bHasValidAttributes = CastleTileColors::TryGetGridTint(Cell.Color, ExpectedTint)
			&& Cell.Number >= 1 && Cell.Number <= 9;
		float ActualFrame = 0.0f;
		FLinearColor ActualTint = FLinearColor::Transparent;
		FLinearColor ActualNumberColor;
		const bool bFrameMatches = Material && Material->GetScalarParameterValue(FrameParameter, ActualFrame)
			&& FMath::IsNearlyEqual(ActualFrame, static_cast<float>(Cell.Number - 1));
		const bool bTintMatches = Material && bHasValidAttributes && Material->GetVectorParameterValue(BackgroundParameter, ActualTint)
			&& Material->GetVectorParameterValue(NumberColorParameter, ActualNumberColor)
			&& ActualTint.Equals(ExpectedTint, 0.001f) && ActualNumberColor.Equals(ExpectedNumberColor, 0.001f);
		if (!bPanelValid || !Cell.Panel->IsRegistered() || !Material || !bHasValidAttributes
			|| Material->Parent != PanelMaterial || !bFrameMatches || !bTintMatches)
		{
			OutReason = FText::FromString(FString::Printf(
				TEXT("主塔 %s 侧格 %s 面 %d 的面板或数字未注册，或纹理材质属性无效（MI_Tower）：组件有效=%s，已注册=%s，动态材质=%s，格子属性有效=%s，数字=%d，图集帧期望=%.3f 实际=%.3f，期望颜色=%s 实际颜色=%s。"),
				*GetPathNameSafe(GetOwner()), *Cell.TowerCell.ToString(), static_cast<int32>(Cell.Face),
				bPanelValid ? TEXT("是") : TEXT("否"), bPanelValid && Cell.Panel->IsRegistered() ? TEXT("是") : TEXT("否"),
				Material ? TEXT("是") : TEXT("否"), bHasValidAttributes ? TEXT("是") : TEXT("否"), Cell.Number,
				static_cast<float>(Cell.Number - 1), ActualFrame, *ExpectedTint.ToString(), *ActualTint.ToString()));
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
			return false;
		}
	}
	return true;
}

bool UCastleTowerSideGridComponent::SetPresentationHidden(const bool bHidden, FText& OutReason)
{
	// 先核对整组纹理面板再更改，避免只隐藏其中一面后才发现组件或属性贴图失效。
	if (!ValidatePresentation(OutReason)) { return false; }
	if (bPresentationHidden != bHidden)
	{
		for (const FCastleTowerSideCell& Cell : Cells)
		{
			Cell.Panel->SetVisibility(!bHidden);
			Cell.Panel->SetCollisionResponseToChannel(ECC_Visibility, bHidden ? ECR_Ignore : ECR_Block);
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
	const FVector Normal = PanelTransform.GetUnitAxis(EAxis::Z);
	if (FVector::DotProduct(Normal, Hit.ImpactNormal) < 0.99) { return false; }
	for (const FTowerFace& Face : TowerFaces)
	{
		if (Face.Face != Cell->Face) { continue; }
		OutTarget.Cell = Cell->TowerCell;
		OutTarget.Face = Cell->Face;
		OutTarget.AdjacentCell = Cell->TowerCell + Face.Outward;
		OutTarget.Surface = ECastleGridCursorSurface::MainTowerSide;
		OutTarget.WorldNormal = Normal;
		// 塔根位于底层格心下方半层；外侧目标格中心沿完整棋盘格距和半层高推算。
		const USceneComponent* TowerRoot = GetOwner()->GetRootComponent();
		if (!IsValid(TowerRoot))
		{
			UE_LOG(LogMoving_Castle, Error,
				TEXT("主塔 %s 的侧格 %s 缺少塔根组件，无法定位三维全息盒。"),
				*GetPathNameSafe(GetOwner()), *Cell->TowerCell.ToString());
			return false;
		}
		const FTransform RootTransform = TowerRoot->GetComponentTransform();
		const FVector Outward(Face.Outward);
		const FVector LocalBoxCenter(
			Outward.X * TowerGridCellSize,
			Outward.Y * TowerGridCellSize,
			(Cell->TowerCell.Z - TowerBaseCell.Z + 0.5f) * TowerLayerHeight);
		const FVector LocalBoxSize(
			TowerGridCellSize * 0.98f,
			TowerGridCellSize * 0.98f,
			TowerLayerHeight * 0.98f);
		OutTarget.IndicatorTransform = FTransform(RootTransform.GetRotation(),
			RootTransform.TransformPosition(LocalBoxCenter), LocalBoxSize * RootTransform.GetScale3D() / 100.0f);
		OutTarget.bValid = true;
		return true;
	}
	return false;
}
