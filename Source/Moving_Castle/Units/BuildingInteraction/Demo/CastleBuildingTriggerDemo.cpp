#include "Units/BuildingInteraction/Demo/CastleBuildingTriggerDemo.h"

#include "Camera/CameraComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingDefinition.h"
#include "CastleBuildings.h"
#include "CastleMassSpawner.h"
#include "CastleNavigationSubsystem.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CastlePatrolRoute.h"
#include "MassEntityConfigAsset.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

ACastleBuildingTriggerDemo::ACastleBuildingTriggerDemo()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	// 楼板是唯一的实际导航几何。建筑白模在飞入后隐藏，让玩家能观察穿过逻辑区域的 NPC。
	UStaticMeshComponent* Floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor"));
	Floor->SetupAttachment(GetRootComponent());
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Floor->SetStaticMesh(Cube.Object);
	Floor->SetRelativeLocation(FVector(700, 200, -25));
	Floor->SetRelativeScale3D(FVector(34, 16, 0.5));
	Floor->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->SetCanEverAffectNavigation(true);
	DemoCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	DemoCamera->SetupAttachment(GetRootComponent());
	DemoCamera->SetRelativeLocation(FVector(700, -2900, 2700));
	DemoCamera->SetRelativeRotation((FVector(700, 180, 20) - DemoCamera->GetRelativeLocation()).Rotation());
	DemoCamera->SetFieldOfView(60);
	DemoCamera->PostProcessSettings.bOverride_AutoExposureBias = true;
	DemoCamera->PostProcessSettings.AutoExposureBias = -1.0f;
}

void ACastleBuildingTriggerDemo::BeginPlay()
{
	Super::BeginPlay();
	const FScopedCastleNavigationChange Change(GetWorld(), this);
	Board = GetWorld()->SpawnActorDeferred<ACastleBoard>(ACastleBoard::StaticClass(), GetActorTransform(), this);
	Board->bGenerateGroundOnBeginPlay = false;
	Board->bSpawnMainTowerOnBeginPlay = Board->bSpawnInitialEnemiesOnBeginPlay = false;
	Board->GridSize = FIntVector(3, 1, 1);
	Board->WorldBaseHeight = 250;
	Board->FinishSpawning(GetActorTransform());

	// 这是显式的演示白模定义，不替代正式建筑资产，也不会修改正式商店配置。
	UCastleBuildingDefinition* A = NewObject<UCastleBuildingDefinition>(Board);
	A->DisplayName = FText::FromString(TEXT("演示双格箭塔"));
	A->BuildingType = ECastleBuildingType::ArcherTower;
	FCastleBuildingLevelDefinition Level1;
	Level1.Level = 1;
	Level1.BuildingClass = ACastleArcherTowerLevel1::StaticClass();
	FCastleBuildingLevelDefinition Level2;
	Level2.Level = 2;
	Level2.BuildingClass = ACastleArcherTowerLevel2::StaticClass();
	A->Levels = {Level1, Level2};
	UCastleBuildingDefinition* B = NewObject<UCastleBuildingDefinition>(Board);
	B->DisplayName = FText::FromString(TEXT("演示单格农田"));
	B->BuildingType = ECastleBuildingType::Farm;
	Level1.BuildingClass = ACastleFarmLevel1::StaticClass();
	B->Levels = {Level1};
	Board->BuildingDefinitions = {A, B};
	FCastleGroundGenerationResult Ground;
	for (int32 X = 0; X < 3; ++X)
	{
		FCastleGroundCell& Cell = Ground.Cells.AddDefaulted_GetRef();
		Cell.Coordinate = FIntVector(X, 0, 0);
		Cell.Color = ECastleTileColor::Red;
		Cell.Number = 3;
	}
	FText Failure;
	if (!Board->ApplyGroundGenerationResult(Ground, Failure)) { Fail(Failure); return; }
	FCastleBuildingDescriptor Card;
	if (!A->CreateDescriptor(2, ECastleTileColor::Red, 3, Card, Failure)) { Fail(Failure); return; }
	Buildings.Add(Board->PlaceBuilding(FIntVector::ZeroValue, Card, false, Failure));
	if (!Buildings.Last()) { Fail(Failure); return; }
	if (!B->CreateDescriptor(1, ECastleTileColor::Red, 3, Card, Failure)) { Fail(Failure); return; }
	Buildings.Add(Board->PlaceBuilding(FIntVector(2, 0, 0), Card, false, Failure));
	if (!Buildings.Last()) { Fail(Failure); return; }

	Route = GetWorld()->SpawnActorDeferred<ACastlePatrolRoute>(ACastlePatrolRoute::StaticClass(), GetActorTransform(), this);
	Route->bAllowPlayerEditing = false;
	// 去程穿过两楼，回程在楼外；每圈每个 NPC 对 A、B 分别进入一次。
	Route->LocalWaypoints = {FVector(-600, 0, 5), FVector(2000, 0, 5), FVector(2000, 600, 5), FVector(-600, 600, 5)};
	Route->FinishSpawning(GetActorTransform());
	Spawner = GetWorld()->SpawnActorDeferred<ACastleMassSpawner>(ACastleMassSpawner::StaticClass(), GetActorTransform(), this);
	Spawner->Configuration = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/MovingCastle/Mass/DA_MassNPCBase"));
	if (!Spawner->Configuration) { Fail(FText::FromString(TEXT("演示缺少正式 NPC 配置 DA_MassNPCBase。"))); return; }
	Spawner->Count = NPCCount;
	Spawner->bFollowSharedPatrolRoute = true;
	Spawner->FinishSpawning(GetActorTransform());
	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController()) { Controller->SetViewTarget(this); }
}

void ACastleBuildingTriggerDemo::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Phase < 0) { return; }
	UCastleNavigationSubsystem* Navigation = GetWorld()->GetSubsystem<UCastleNavigationSubsystem>();
	const auto Path = Navigation->AcquireSharedPath();
	if (Phase == 0)
	{
		if (!Navigation->LastFailure.IsEmpty()) { Fail(Navigation->LastFailure); return; }
		if (!Path || !Path->bValid || Spawner->SpawnedUnits.Num() != NPCCount) { return; }
		for (ACastleBuildingBase* Building : Buildings)
		{
			// 只隐藏演示白模，不隐藏数字标签，也不改变效果、触发范围和实际楼板。
			if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Building->GetDefaultSubobjectByName(TEXT("PrototypeMesh"))))
			{
				Mesh->SetVisibility(false);
			}
		}
		Phase = 1;
	}
	DrawDebugBox(GetWorld(), GetActorTransform().TransformPosition(FVector(350, 0, 15)), FVector(700, 350, 15), GetActorQuat(), FColor::Cyan, false, -1, 0, 4);
	DrawDebugBox(GetWorld(), GetActorTransform().TransformPosition(FVector(1400, 0, 15)), FVector(350, 350, 15), GetActorQuat(), FColor::Orange, false, -1, 0, 4);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.0f, FColor::Yellow, FString::Printf(
			TEXT("建筑进入演示 | NPC=%d | A 双格箭塔=%lld 次 | B 单格农田=%lld 次\n停留、A 内跨格不增加；离开后再进入，次数继续增加。"),
			NPCCount, Buildings[0]->BuildingEffect->TriggerCount, Buildings[1]->BuildingEffect->TriggerCount));
	}
}

void ACastleBuildingTriggerDemo::Fail(const FText& Reason)
{
	Phase = -1;
	UE_LOG(LogMoving_Castle, Error, TEXT("建筑进入演示失败：%s"), *Reason.ToString());
	if (GEngine) { GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 60.0f, FColor::Red, Reason.ToString()); }
}

void ACastleBuildingTriggerDemo::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 主动删除演示器时同样清理其批次与建筑，不能在正式世界留下区域或无人管理的 NPC。
	if (IsValid(Spawner)) { Spawner->Destroy(); }
	if (IsValid(Route)) { Route->Destroy(); }
	for (ACastleBuildingBase* Building : Buildings) { if (IsValid(Building)) { Building->Destroy(); } }
	if (IsValid(Board)) { Board->Destroy(); }
	Super::EndPlay(EndPlayReason);
}
