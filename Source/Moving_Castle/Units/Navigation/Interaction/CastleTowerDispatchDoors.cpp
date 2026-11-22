// Copyright Epic Games, Inc. All Rights Reserved.

#include "Interaction/CastleTowerDispatchDoors.h"

#include "Castle/Actors/CastleBoard.h"
#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/PCG/HachimisV4/CastleHachimisV4Layout.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Math/RotationMatrix.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

UCastleTowerDispatchDoors::UCastleTowerDispatchDoors()
{
	PrimaryComponentTick.bCanEverTick = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	DoorMarkerMesh = Cube.Object;
}

bool UCastleTowerDispatchDoors::Initialize(const ACastleBoard& InBoard, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	AActor* Tower = GetOwner();
	USceneComponent* Root = IsValid(Tower) ? Tower->GetRootComponent() : nullptr;
	if (Board.IsValid() || !IsValid(Root) || !IsValid(DoorMarkerMesh) || Tower->GetOwner() != &InBoard)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 无法初始化双门：棋盘、塔根或占位 Cube 无效，或已初始化。"), *GetPathNameSafe(Tower)));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	TArray<USceneComponent*, TInlineAllocator<4>> Parts;
	for (const int32 Sign : {1, -1})
	{
		const FVector Outward(Sign, 0, 0);
		const FRotator Rotation = FRotationMatrix::MakeFromXZ(Outward, FVector::UpVector).Rotator();
		// 门标识位于侧格下沿，保留格号可读性。正对塔外的标记使用 A/B，默认字体也能完整显示。
		const FVector Center = Outward * (InBoard.TowerSideSurfaceOffset + 7.0) + FVector(0, 0, InBoard.LayerHeight * 0.12);
		UStaticMeshComponent* Marker = NewObject<UStaticMeshComponent>(Tower);
		Marker->SetupAttachment(Root);
		Marker->SetRelativeLocationAndRotation(Center, Rotation);
		Marker->SetRelativeScale3D(FVector(0.08, 0.44, 0.54));
		Marker->SetStaticMesh(DoorMarkerMesh);
		Marker->SetCanEverAffectNavigation(false);
		Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Parts.Add(Marker);
		UTextRenderComponent* Label = NewObject<UTextRenderComponent>(Tower);
		Label->SetupAttachment(Root);
		Label->SetRelativeLocationAndRotation(Center + Outward * 6.0, Rotation);
		Label->SetCanEverAffectNavigation(false);
		Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetVerticalAlignment(EVRTA_TextCenter);
		Label->SetWorldSize(36.0f);
		Label->SetTextRenderColor(Sign > 0 ? FColor::Green : FColor::Cyan);
		Label->SetText(FText::FromString(Sign > 0 ? TEXT("A") : TEXT("B")));
		Parts.Add(Label);
	}
	for (USceneComponent* Part : Parts)
	{
		Tower->AddInstanceComponent(Part);
		Part->RegisterComponent();
		if (!Part->IsRegistered())
		{
			for (USceneComponent* Added : Parts) { Added->DestroyComponent(); }
			OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 的双门标识注册失败。"), *Tower->GetPathName()));
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
			return false;
		}
	}
	Board = &InBoard;
	return true;
}

bool UCastleTowerDispatchDoors::TryGetDoorWaypoints(FVector& OutExit, FVector& OutEntrance, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	const ACastleBoard* OwnerBoard = Board.Get();
	if (!IsValid(OwnerBoard))
	{
		OutReason = FText::FromString(FString::Printf(TEXT("主塔 %s 没有已登记的双门棋盘。"), *GetPathNameSafe(GetOwner())));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	}
	const FIntVector Base = OwnerBoard->GetBoardCenterCoordinate();
	FVector Points[2];
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Points); ++Index)
	{
		const FIntVector Cell = Base + FIntVector(Index == 0 ? 1 : -1, 0, 0);
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
		Points[Index] = OwnerBoard->GetGridWorldLocation(Cell)
			+ OwnerBoard->GetActorTransform().TransformVector(FVector(0, 0, StandingHeight - OwnerBoard->LayerHeight * 0.5));
	}
	OutExit = Points[0];
	OutEntrance = Points[1];
	return true;
}
