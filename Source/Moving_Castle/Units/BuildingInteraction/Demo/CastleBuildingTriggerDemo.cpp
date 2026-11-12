#include "Units/BuildingInteraction/Demo/CastleBuildingTriggerDemo.h"

#include "Camera/CameraComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingDefinition.h"
#include "CastleBuildings.h"
#include "CastleMassSpawner.h"
#include "CastleTowerAttackDataAsset.h"
#include "CastleNavigationSubsystem.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleMassTypes.h"
#include "CastleMassTraits.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
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
	CounterA = CreateDefaultSubobject<UTextRenderComponent>(TEXT("CounterA"));
	CounterB = CreateDefaultSubobject<UTextRenderComponent>(TEXT("CounterB"));
	EnemyStatus = CreateDefaultSubobject<UTextRenderComponent>(TEXT("EnemyStatus"));
	for (UTextRenderComponent* Counter : {CounterA.Get(), CounterB.Get(), EnemyStatus.Get()})
	{
		Counter->SetupAttachment(GetRootComponent());
		Counter->SetHorizontalAlignment(EHTA_Center);
		Counter->SetWorldSize(64);
		Counter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Counter->SetCanEverAffectNavigation(false);
	}
	CounterA->SetRelativeLocation(FVector(350, -450, 100));
	CounterB->SetRelativeLocation(FVector(1400, -450, 100));
	EnemyStatus->SetRelativeLocation(FVector(1250, -450, 500));
	// 文字朝向本演示的固定相机，不向业务建筑添加逐帧面向相机的额外功能。
	CounterA->SetRelativeRotation((DemoCamera->GetRelativeLocation() - CounterA->GetRelativeLocation()).Rotation());
	CounterB->SetRelativeRotation((DemoCamera->GetRelativeLocation() - CounterB->GetRelativeLocation()).Rotation());
	EnemyStatus->SetRelativeRotation((DemoCamera->GetRelativeLocation() - EnemyStatus->GetRelativeLocation()).Rotation());
	CounterA->SetTextRenderColor(FColor::Cyan);
	CounterB->SetTextRenderColor(FColor::Orange);
	CounterA->SetText(FText::FromString(TEXT("A (2 cells): 0")));
	CounterB->SetText(FText::FromString(TEXT("B (1 cell): 0")));
	EnemyStatus->SetTextRenderColor(FColor::Red);
	EnemyStatus->SetText(FText::FromString(TEXT("Enemy HP: waiting")));
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

	// 演示显式引用带模型的等级蓝图；缺少模型时由正式放置接口报错，不生成演示占位件。
	UCastleBuildingDefinition* A = NewObject<UCastleBuildingDefinition>(Board);
	A->DisplayName = FText::FromString(TEXT("演示双格箭塔"));
	A->BuildingType = ECastleBuildingType::ArcherTower;
	A->AttackData = LoadObject<UCastleTowerAttackDataAsset>(nullptr,
		TEXT("/Game/MovingCastle/Data/Buildings/DA_ArcherTowerAttack"));
	if (!A->AttackData) { Fail(FText::FromString(TEXT("建筑触发演示缺少正式箭塔攻击数据 DA_ArcherTowerAttack。"))); return; }
	FCastleBuildingLevelDefinition Level1;
	Level1.Level = 1;
	Level1.BuildingClass = LoadClass<ACastleBuildingBase>(nullptr,
		TEXT("/Game/MovingCastle/BP/BP_ArcherTower_lv1.BP_ArcherTower_lv1_C"));
	FCastleBuildingLevelDefinition Level2;
	Level2.Level = 2;
	Level2.BuildingClass = LoadClass<ACastleBuildingBase>(nullptr,
		TEXT("/Game/MovingCastle/BP/BP_ArcherTower_lv2.BP_ArcherTower_lv2_C"));
	A->Levels = {Level1, Level2};
	UCastleBuildingDefinition* B = NewObject<UCastleBuildingDefinition>(Board);
	B->DisplayName = FText::FromString(TEXT("演示单格农田"));
	B->BuildingType = ECastleBuildingType::Farm;
	Level1.BuildingClass = LoadClass<ACastleBuildingBase>(nullptr,
		TEXT("/Game/MovingCastle/BP/BP_Farm_lv1.BP_Farm_lv1_C"));
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

	// 同一可直接运行的场景内提供一名敌人，使巡逻进入箭塔后能观察追踪弹与生命变化。
	FTransform EnemySpawnerTransform = GetActorTransform();
	EnemySpawnerTransform.SetLocation(GetActorTransform().TransformPosition(FVector(1250, 0, 250)));
	EnemySpawner = GetWorld()->SpawnActorDeferred<ACastleMassSpawner>(
		ACastleMassSpawner::StaticClass(), EnemySpawnerTransform, this);
	EnemySpawner->Configuration = LoadObject<UMassEntityConfigAsset>(nullptr,
		TEXT("/Game/MovingCastle/Mass/DA_MassEnemyBase"));
	EnemySpawner->Count = 1;
	EnemySpawner->Columns = 1;
	EnemySpawner->Spacing = 1.0f;
	EnemySpawner->bSpawnOnBeginPlay = false;
	// 敌人保持在箭塔射程内，避免巡逻 NPC 到达前目标已移动离开攻击半径。
	EnemySpawner->bStartMoving = false;
	EnemySpawner->FinishSpawning(EnemySpawnerTransform);
	if (!EnemySpawner->Configuration
		|| EnemySpawner->Configuration->FindTrait(UCastleMassEnemyTrait::StaticClass()) == nullptr)
	{
		Fail(FText::FromString(TEXT("建筑触发演示需要包含 Castle Mass 敌人 Trait 的 DA_MassEnemyBase。")));
		EnemySpawner->Destroy();
		return;
	}
	if (!EnemySpawner->SpawnUnits() || EnemySpawner->SpawnedUnits.Num() != 1)
	{
		Fail(FText::FromString(TEXT("建筑触发演示无法生成一名可受击的 Mass 敌人。")));
		EnemySpawner->Destroy();
		return;
	}
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
			// 只隐藏蓝图美术分支，不隐藏数字标签，也不改变功能地板碰撞和触发范围。
			TArray<UStaticMeshComponent*> Meshes;
			Building->GetComponents<UStaticMeshComponent>(Meshes);
			for (UStaticMeshComponent* Mesh : Meshes)
			{
				if (!UBuildingGameplayRootComponent::IsInGameplayHierarchy(Mesh)
					&& !Mesh->ComponentHasTag(TEXT("MergeExclude")))
				{
					Mesh->SetVisibility(false);
				}
			}
		}
		Phase = 1;
	}
	DrawDebugBox(GetWorld(), GetActorTransform().TransformPosition(FVector(350, 0, 15)), FVector(700, 350, 15), GetActorQuat(), FColor::Cyan, false, -1, 0, 4);
	DrawDebugBox(GetWorld(), GetActorTransform().TransformPosition(FVector(1400, 0, 15)), FVector(350, 350, 15), GetActorQuat(), FColor::Orange, false, -1, 0, 4);
	// 仅次数变化时重建文字网格，演示 Tick 不以帧率重复提交同一段渲染数据。
	if (DisplayedCountA != Buildings[0]->BuildingEffect->TriggerCount)
	{
		DisplayedCountA = Buildings[0]->BuildingEffect->TriggerCount;
		CounterA->SetText(FText::FromString(FString::Printf(TEXT("A (2 cells): %lld"), DisplayedCountA)));
	}
	if (DisplayedCountB != Buildings[1]->BuildingEffect->TriggerCount)
	{
		DisplayedCountB = Buildings[1]->BuildingEffect->TriggerCount;
		CounterB->SetText(FText::FromString(FString::Printf(TEXT("B (1 cell): %lld"), DisplayedCountB)));
	}
	if (IsValid(EnemySpawner) && EnemySpawner->SpawnedUnits.Num() == 1)
	{
		UCastleMassUnitSubsystem* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
		FCastleMassUnitSnapshot Snapshot;
		if (Units && Units->GetUnitSnapshot(EnemySpawner->SpawnedUnits[0], Snapshot))
		{
			if (!FMath::IsNearlyEqual(DisplayedEnemyHealth, Snapshot.Health))
			{
				DisplayedEnemyHealth = Snapshot.Health;
				EnemyStatus->SetText(FText::FromString(FString::Printf(TEXT("Enemy HP: %.0f"), DisplayedEnemyHealth)));
			}
		}
		else if (Units && !Units->IsUnitValid(EnemySpawner->SpawnedUnits[0]) && DisplayedEnemyHealth != 0.0f)
		{
			DisplayedEnemyHealth = 0.0f;
			EnemyStatus->SetText(FText::FromString(TEXT("Enemy defeated")));
		}
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.0f, FColor::Yellow, FString::Printf(
			TEXT("建筑进入演示 | NPC=%d | A 双格箭塔=%lld 次 | B 单格农田=%lld 次 | Enemy HP=%.0f\n停留、A 内跨格不增加；离开后再进入，箭塔逐发射击。"),
			NPCCount, Buildings[0]->BuildingEffect->TriggerCount, Buildings[1]->BuildingEffect->TriggerCount,
			DisplayedEnemyHealth));
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
	if (IsValid(EnemySpawner)) { EnemySpawner->Destroy(); }
	if (IsValid(Route)) { Route->Destroy(); }
	for (ACastleBuildingBase* Building : Buildings) { if (IsValid(Building)) { Building->Destroy(); } }
	if (IsValid(Board)) { Board->Destroy(); }
	Super::EndPlay(EndPlayReason);
}
