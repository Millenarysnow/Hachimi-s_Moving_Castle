// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interaction/CastleTowerDispatchDoors.h"

#include "Castle/Actors/CastleBoard.h"
#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/PCG/HachimisV4/CastleHachimisV4Layout.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 与 V4 建筑功能地板保持同一默认站立面标高和 5 cm 厚度；门外房间配置特殊标高时同步对齐。
	constexpr double DefaultStandingHeight = 40.8;
	constexpr double FloorThickness = 5.0;
	// 主塔一楼内部在 X=0 留出 60 cm 隔断空隙，防止 A* 从门 A 直接穿塔到门 B 抄近路。
	constexpr double InnerGapHalfWidth = 30.0;
	// 主塔 ±Y 两侧无门，地板向内收进 77.5 cm，防止与 ±Y 紧邻的开放底盘房间意外连通。
	constexpr double SideInsetY = 77.5;
	constexpr TCHAR DoorSignPath[] = TEXT("/Game/MovingCastle/Model/MainTower/DoorSigns/SM_MainTower_DoorSigns_560.SM_MainTower_DoorSigns_560");
}

UCastleTowerDispatchDoors::UCastleTowerDispatchDoors()
{
	PrimaryComponentTick.bCanEverTick = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	DoorFloorMesh = Cube.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Signs(DoorSignPath);
	DoorSignMesh = Signs.Object;
}

bool UCastleTowerDispatchDoors::Initialize(const ACastleBoard& InBoard, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	AActor* Tower = GetOwner();
	USceneComponent* Root = IsValid(Tower) ? Tower->GetRootComponent() : nullptr;
	const auto Reject = [&](const FString& Detail)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 无法初始化双门：%s。"),
			*GetPathNameSafe(Tower), *Detail));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	};
	const double SlabLengthX = InBoard.CellSize * 0.5 - InnerGapHalfWidth;
	const double SlabWidthY = InBoard.CellSize - 2.0 * SideInsetY;
	if (Board.IsValid() || !IsValid(Root) || !IsValid(DoorFloorMesh) || Tower->GetOwner() != &InBoard
		|| !FMath::IsFinite(InBoard.CellSize) || SlabLengthX <= 100.0 || SlabWidthY <= 100.0)
	{
		return Reject(TEXT("棋盘、塔根、格距或功能地板网格 /Engine/BasicShapes/Cube 无效，或已初始化"));
	}
	if (!IsValid(DoorSignMesh) || DoorSignMesh->GetStaticMaterials().IsEmpty())
	{
		return Reject(FString::Printf(TEXT("正式双门网格 %s 缺失或没有材质槽"), DoorSignPath));
	}
	for (const FStaticMaterial& Material : DoorSignMesh->GetStaticMaterials())
	{
		if (!IsValid(Material.MaterialInterface) || Material.MaterialInterface->GetFName() != Material.MaterialSlotName)
		{
			return Reject(FString::Printf(TEXT("正式双门网格 %s 的材质槽 %s 没有同名作者材质"),
				DoorSignPath, *Material.MaterialSlotName.ToString()));
		}
	}
	// 主塔美术网格不参与导航也不阻挡 Pawn，防止闭合外墙模型截断门 A/B 内外连通；仅由下方不可见功能地板提供行走面与 Pawn 碰撞。
	TInlineComponentArray<UStaticMeshComponent*> ExistingMeshes(Tower);
	for (UStaticMeshComponent* Mesh : ExistingMeshes)
	{
		if (!IsValid(Mesh)) { continue; }
		if (Mesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		}
		Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Mesh->SetCanEverAffectNavigation(false);
	}
	TArray<UStaticMeshComponent*, TInlineAllocator<3>> Parts;
	// FBX 已使用塔根原点并同时包含 +X 正门和 -X 后门，保持作者变换即可对齐塔身。
	UStaticMeshComponent* SignsMesh = NewObject<UStaticMeshComponent>(Tower, TEXT("MainTowerDoorSigns"));
	SignsMesh->SetupAttachment(Root);
	SignsMesh->SetMobility(EComponentMobility::Movable);
	SignsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SignsMesh->SetCanEverAffectNavigation(false);
	SignsMesh->SetGenerateOverlapEvents(false);
	SignsMesh->SetStaticMesh(DoorSignMesh);
	Parts.Add(SignsMesh);
	const int32 Signs[] = {1, -1};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Signs); ++Index)
	{
		const int32 Sign = Signs[Index];
		// 主塔一楼内部门 A (+X) 与门 B (−X) 各置一块不可见功能导航地板，外沿齐平主塔 ±X 格界以衔接门外房间。
		const double DoorAnchorX = Sign * (InBoard.CellSize * 0.5 + InnerGapHalfWidth) * 0.5;
		UStaticMeshComponent* Floor = NewObject<UStaticMeshComponent>(Tower, Index == 0 ? TEXT("MainTowerDoorAFloor") : TEXT("MainTowerDoorBFloor"));
		Floor->SetupAttachment(Root);
		Floor->SetMobility(EComponentMobility::Movable);
		Floor->SetRelativeLocation(FVector(DoorAnchorX, 0.0, DefaultStandingHeight - FloorThickness * 0.5));
		Floor->SetRelativeScale3D(FVector(SlabLengthX / 100.0, SlabWidthY / 100.0, FloorThickness / 100.0));
		Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Floor->SetCollisionResponseToAllChannels(ECR_Ignore);
		Floor->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Floor->SetCanEverAffectNavigation(false);
		Floor->SetGenerateOverlapEvents(false);
		Floor->SetVisibility(false);
		Floor->SetHiddenInGame(true);
		Floor->SetStaticMesh(DoorFloorMesh);
		Floor->SetCanEverAffectNavigation(true);
		DoorFloors[Index] = Floor;
		Parts.Add(Floor);
	}
	for (UStaticMeshComponent* Part : Parts)
	{
		Tower->AddInstanceComponent(Part);
		Part->RegisterComponent();
		if (!Part->IsRegistered())
		{
			const FString Detail = FString::Printf(TEXT("组件 %s 注册失败，网格 %s"),
				*Part->GetName(), *GetPathNameSafe(Part->GetStaticMesh()));
			for (UStaticMeshComponent* Added : Parts) { Added->DestroyComponent(); }
			DoorFloors[0] = DoorFloors[1] = nullptr;
			return Reject(Detail);
		}
	}
	DoorSigns = SignsMesh;
	Board = &InBoard;
	return true;
}

void UCastleTowerDispatchDoors::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	// 标识只有美术职责；两块功能楼板继续沿用原先的 Actor 所有权和导航语义。
	if (IsValid(DoorSigns)) { DoorSigns->DestroyComponent(); }
	DoorSigns = nullptr;
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UCastleTowerDispatchDoors::TryGetDoorWaypoints(FVector& OutExit, FVector& OutEntrance, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	const ACastleBoard* OwnerBoard = Board.Get();
	if (!IsValid(OwnerBoard) || !DoorFloors[0].IsValid() || !DoorFloors[1].IsValid())
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 没有已登记的双门棋盘或内部导航地板。"), *GetPathNameSafe(GetOwner())));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	const FIntVector Base = OwnerBoard->GetBoardCenterCoordinate();
	FVector Points[2];
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Points); ++Index)
	{
		const int32 Sign = Index == 0 ? 1 : -1;
		const FIntVector Cell = Base + FIntVector(Sign, 0, 0);
		const ACastleBuildingBase* Room = OwnerBoard->GetBuildingAt(Cell);
		if (!IsValid(Room) || !Room->bHasCompletedPlacement)
		{
			OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 的门 %s 外格 %s 需要已完成放置的房间，无法建立巡逻路线。"),
				*GetPathNameSafe(GetOwner()), Index == 0 ? TEXT("A") : TEXT("B"), *Cell.ToString()));
			UE_LOG(LogMoving_Castle, Warning, TEXT("%s"), *OutReason.ToString());
			return false;
		}
		// 延续玩家放点使用的作者标高。盾塔沿用底面标高，其余正式房间须有布局资产。
		double StandingHeight = 0.0;
		if (IsValid(Room->HachimisLayout))
		{
			StandingHeight = Room->HachimisLayout->StandingHeight;
		}
		else if (Room->GetBuildingType() != ECastleBuildingType::ShieldTower)
		{
			OutReason = FText::FromString(FString::Printf(TEXT("主塔门外房间 %s 缺少站立面布局。"), *Room->GetPathName()));
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
			return false;
		}
		if (!FMath::IsFinite(StandingHeight))
		{
			OutReason = FText::FromString(FString::Printf(TEXT("主塔门外房间 %s 的站立面高度必须是有限数。"), *Room->GetPathName()));
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
			return false;
		}
		const double DoorAnchorX = Sign * (OwnerBoard->CellSize * 0.5 + InnerGapHalfWidth) * 0.5;
		const FVector DesiredFloorLocation(DoorAnchorX, 0.0, StandingHeight - FloorThickness * 0.5);
		if (!DoorFloors[Index]->GetRelativeLocation().Equals(DesiredFloorLocation, 0.01))
		{
			DoorFloors[Index]->SetRelativeLocation(DesiredFloorLocation);
		}
		Points[Index] = OwnerBoard->GetGridWorldLocation(Base)
			+ OwnerBoard->GetActorTransform().TransformVector(FVector(DoorAnchorX, 0.0, StandingHeight - OwnerBoard->LayerHeight * 0.5));
	}
	OutExit = Points[0];
	OutEntrance = Points[1];
	return true;
}
