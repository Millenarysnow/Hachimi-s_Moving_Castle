// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBoard.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Castle/Placement/CastleTowerSideGridComponent.h"
#include "Castle/PCG/CastleBuildingPCGSubsystem.h"
#include "Units/BuildingInteraction/CastleBuildingTriggerSubsystem.h"

#include "CastleBackpackComponent.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleGroundTileActor.h"
#include "BuildingMergeComponent.h"
#include "AssemblyFlightComponent.h"
#include "CastleGroundGenerator.h"
#include "CastleCombatData.h"
#include "CastleMainTowerBalloonComponent.h"
#include "CastleMainTowerComponent.h"
#include "CastleMassEnemySpawner.h"
#include "CastleNavigationSubsystem.h"
#include "CastleTowerDispatchConfigDataAsset.h"
#include "CastleTowerDispatchSubsystem.h"
#include "Interaction/CastleTowerDispatchDoors.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 将局部空间法线转换成六面枚举，优先使用绝对值最大的轴。 */
	ECastleGridFace GetGridFaceFromNormal(const FVector& LocalNormal)
	{
		const FVector AbsNormal = LocalNormal.GetAbs();
		if (AbsNormal.Z >= AbsNormal.X && AbsNormal.Z >= AbsNormal.Y)
		{
			return LocalNormal.Z >= 0.0f ? ECastleGridFace::Up : ECastleGridFace::Down;
		}

		if (AbsNormal.X >= AbsNormal.Y)
		{
			return LocalNormal.X >= 0.0f ? ECastleGridFace::PositiveX : ECastleGridFace::NegativeX;
		}

		return LocalNormal.Y >= 0.0f ? ECastleGridFace::PositiveY : ECastleGridFace::NegativeY;
	}

	/** 返回一个面外侧紧邻的坐标增量。 */
	FIntVector GetGridFaceOffset(ECastleGridFace Face)
	{
		switch (Face)
		{
		case ECastleGridFace::Up:
			return FIntVector(0, 0, 1);
		case ECastleGridFace::Down:
			return FIntVector(0, 0, -1);
		case ECastleGridFace::PositiveX:
			return FIntVector(1, 0, 0);
		case ECastleGridFace::NegativeX:
			return FIntVector(-1, 0, 0);
		case ECastleGridFace::PositiveY:
			return FIntVector(0, 1, 0);
		case ECastleGridFace::NegativeY:
			return FIntVector(0, -1, 0);
		default:
			return FIntVector::ZeroValue;
		}
	}

	/** 返回一个面在棋盘局部坐标中的单位法线。 */
	FVector GetGridFaceNormal(ECastleGridFace Face)
	{
		switch (Face)
		{
		case ECastleGridFace::Up:
			return FVector::UpVector;
		case ECastleGridFace::Down:
			return FVector::DownVector;
		case ECastleGridFace::PositiveX:
			return FVector::ForwardVector;
		case ECastleGridFace::NegativeX:
			return -FVector::ForwardVector;
		case ECastleGridFace::PositiveY:
			return FVector::RightVector;
		case ECastleGridFace::NegativeY:
			return -FVector::RightVector;
		default:
			return FVector::UpVector;
		}
	}

}

ACastleBoard::ACastleBoard()
{
	// 棋盘管理器不需要 Tick；地面格由独立 Actor 承载，不依赖 PCG 或正式模型。
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	PrototypeBackpack = CreateDefaultSubobject<UCastleBackpackComponent>(TEXT("PrototypeBackpack"));

	GroundGeneratorClass = UCastleRandomGroundGenerator::StaticClass();
	// 项目初始规则是明确的资产配置；资产丢失时子系统拒绝更新，不生成替代规则。
	static ConstructorHelpers::FObjectFinder<UCastlePCGRuleSetDataAsset> PCGRules(TEXT("/Game/MovingCastle/Data/DA_CastlePCGRuleSet.DA_CastlePCGRuleSet"));
	PCGRuleSet = PCGRules.Object;

	static ConstructorHelpers::FClassFinder<AActor> MainTowerBlueprint(
		TEXT("/Game/MovingCastle/BP/BP_MainTower"));
	MainTowerClass = MainTowerBlueprint.Class;
}

void ACastleBoard::BeginPlay()
{
	Super::BeginPlay();

	// 运行时再解析默认战斗 Data Asset，避免 CDO 构造早于资产注册时产生无意义的加载警告；
	// 关卡实例仍可直接覆盖 CombatData，缺失资产会在 SpawnMainTower 中明确报错。
	if (CombatData == nullptr)
	{
		CombatData = LoadObject<UCastleCombatDataAsset>(nullptr,
			TEXT("/Game/MovingCastle/Data/DA_CastleCombat.DA_CastleCombat"));
	}

	// 建造面由地图显式指定；塔侧模式从不创建地面作为备用来源。
	if (PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides)
	{
		if (!bSpawnMainTowerOnBeginPlay)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 启用了塔侧建造面但禁用了开局生成主塔。"), *GetPathName());
			return;
		}
		if (UsesProgressiveTowerLayers())
		{
			if (InitialUnlockedLayers < 1 || MaxTowerLayers < InitialUnlockedLayers || MaxTowerLayers > 9 || GridSize.Z < MaxTowerLayers)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 的主塔层数配置无效：初始 %d，最大 %d，棋盘容量 %d；最大层数不得超过 9 和棋盘容量。"),
					*GetPathName(), InitialUnlockedLayers, MaxTowerLayers, GridSize.Z);
				return;
			}
			CurrentUnlockedLayers = InitialUnlockedLayers;
		}
	}
	else if (PlacementSurfaceMode != ECastlePlacementSurfaceMode::GroundBoard)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 的建造面模式无效。"), *GetPathName());
		return;
	}
	else if (bGenerateGroundOnBeginPlay)
	{
		FText FailureReason;
		if (!GenerateGround(FailureReason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 BeginPlay 生成地面失败：%s"), *FailureReason.ToString());
		}
	}

	if (PlacementSurfaceMode == ECastlePlacementSurfaceMode::GroundBoard && !GeneratedGroundCells.IsEmpty())
	{
		InitializePrototypeDeck();
	}

	if (bSpawnMainTowerOnBeginPlay)
	{
		FText FailureReason;
		if (!SpawnMainTower(FailureReason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 生成主塔失败（%s）：%s"), *GetPathName(),
				*GetPathNameSafe(MainTowerClass.Get()), *FailureReason.ToString());
			return;
		}
	}

	if (PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides)
	{
		InitializePrototypeDeck();
	}

	if (bSpawnInitialEnemiesOnBeginPlay)
	{
		FText FailureReason;
		if (!SpawnInitialEnemies(FailureReason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 BeginPlay 生成初始敌人失败：%s"), *FailureReason.ToString());
		}
	}
}

FIntVector ACastleBoard::GetBoardCenterCoordinate() const
{
	return GridOrigin + FIntVector(
		FMath::Max(0, (GridSize.X - 1) / 2),
		FMath::Max(0, (GridSize.Y - 1) / 2),
		0);
}

bool ACastleBoard::SpawnMainTower(FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (IsValid(MainTowerActor))
	{
		OutFailureReason = FText::FromString(TEXT("棋盘已经生成过主塔，不能重复生成。"));
		return false;
	}
	if (!IsValid(CombatData))
	{
		OutFailureReason = FText::FromString(TEXT("棋盘没有配置战斗 Data Asset。"));
		return false;
	}
	if (!CombatData->ValidateConfiguration(OutFailureReason))
	{
		return false;
	}
	if (MainTowerClass == nullptr || GetWorld() == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("棋盘没有配置有效的主塔 Actor 类或 World。"));
		return false;
	}
	if (!IsCoordinateWithinBoard(GetBoardCenterCoordinate()))
	{
		OutFailureReason = FText::FromString(TEXT("棋盘中心坐标超出棋盘范围。"));
		return false;
	}

	if (PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides && !ValidateBoardDimensions(OutFailureReason))
	{
		return false;
	}
	if (UsesProgressiveTowerLayers())
	{
		if (TowerBalloonLimitsByTier.Num() != 4 || !FMath::IsFinite(TowerHealthPerBalloon) || TowerHealthPerBalloon <= 0.0f)
		{
			OutFailureReason = FText::FromString(TEXT("主塔须显式配置四个等级的气球上限以及有限正数的每球生命。"));
			return false;
		}
		for (const int32 Count : TowerBalloonLimitsByTier)
		{
			if (Count < 1 || Count > 100 || !FMath::IsFinite(Count * TowerHealthPerBalloon))
			{
				OutFailureReason = FText::FromString(TEXT("主塔各级气球上限须在 1～100，每级换算出的生命上限须为有限数。"));
				return false;
			}
		}
	}

	// 主塔和普通建筑共用最低格的底面锚点；塔侧放置占用塔身外侧而非塔根格。
	// 使用棋盘变换转换偏移，保证旋转和缩放棋盘时主塔仍与其他建筑保持同一落格规则。
	const FVector TowerBottomCenter = GetGridWorldLocation(GetBoardCenterCoordinate())
		+ GetActorTransform().TransformVector(FVector(0.0f, 0.0f, -LayerHeight * 0.5f));
	const FTransform TowerTransform(GetActorQuat(), TowerBottomCenter, GetActorScale3D());
	AActor* TowerActor = GetWorld()->SpawnActorDeferred<AActor>(MainTowerClass, TowerTransform, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (TowerActor == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("World 延迟生成主塔 Actor 失败。"));
		return false;
	}

	UCastleMainTowerComponent* TowerComponent = NewObject<UCastleMainTowerComponent>(TowerActor, TEXT("MainTowerHealth"));
	if (TowerComponent == nullptr)
	{
		TowerActor->Destroy();
		OutFailureReason = FText::FromString(TEXT("创建主塔生命组件失败。"));
		return false;
	}
	TowerActor->AddInstanceComponent(TowerComponent);
	TowerComponent->MaxHealth = CombatData->MainTowerMaxHealth;
	if (UsesProgressiveTowerLayers())
	{
		TowerComponent->MaxBalloons = TowerBalloonLimitsByTier[0];
		TowerComponent->HealthPerBalloon = TowerHealthPerBalloon;
		TowerComponent->MaxHealth = TowerHealthPerBalloon * TowerComponent->MaxBalloons;
	}
	TowerComponent->TargetHeightOffset = CombatData->MainTowerTargetHeightOffset;
	if (!TowerComponent->InitializeHealth())
	{
		TowerActor->Destroy();
		OutFailureReason = FText::FromString(TEXT("主塔生命组件初始化失败。"));
		return false;
	}
	TowerComponent->RegisterComponent();
	TowerActor->FinishSpawning(TowerTransform);

	UCastleMainTowerSubsystem* TowerSubsystem = GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>();
	if (TowerSubsystem == nullptr || TowerSubsystem->GetRegisteredMainTower() != TowerComponent)
	{
		TowerActor->Destroy();
		OutFailureReason = FText::FromString(TEXT("主塔生成后没有成功注册到当前 World。"));
		return false;
	}

	if (PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides)
	{
		// 真实塔根在 FinishSpawning 后才确定。完整初始化前不发布主塔或侧格索引。
		if (UsesProgressiveTowerLayers() && !InitializeProgressiveTowerPresentation(*TowerActor, *TowerComponent, OutFailureReason))
		{
			TowerActor->Destroy();
			return false;
		}
		UCastleTowerSideGridComponent* Grid = NewObject<UCastleTowerSideGridComponent>(TowerActor, TEXT("TowerSideGrid"));
		TowerActor->AddInstanceComponent(Grid);
		Grid->RegisterComponent();
		const int32 RunSeed = static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
		if (!Grid->Initialize(GetBoardCenterCoordinate(), GetUnlockedLayerCount(), GroundTileSize, LayerHeight,
			TowerSideSurfaceOffset, RunSeed, OutFailureReason))
		{
			TowerActor->Destroy();
			return false;
		}
		if (TowerDispatchConfig)
		{
			UCastleTowerDispatchDoors* Doors = NewObject<UCastleTowerDispatchDoors>(TowerActor, TEXT("TowerDispatchDoors"));
			TowerActor->AddInstanceComponent(Doors);
			Doors->RegisterComponent();
			UCastleTowerDispatchSubsystem* Dispatch = GetWorld()->GetSubsystem<UCastleTowerDispatchSubsystem>();
			if (!Doors->Initialize(*this, OutFailureReason) || !Dispatch->Configure(TowerDispatchConfig, OutFailureReason))
			{
				TowerActor->Destroy();
				return false;
			}
		}
		TowerSideGrid = Grid;
		UE_LOG(LogMoving_Castle, Display, TEXT("主塔 %s 已生成 %d 个侧格（种子 %d）；本局不生成地面棋盘。"),
			*GetPathNameSafe(TowerActor), Grid->GetCells().Num(), RunSeed);
	}
	MainTowerActor = TowerActor;
	UE_LOG(LogMoving_Castle, Display, TEXT("主塔已在棋盘中心格 (%d, %d, %d) 生成，生命 %.1f。"),
		GetBoardCenterCoordinate().X, GetBoardCenterCoordinate().Y, GetBoardCenterCoordinate().Z,
		TowerComponent->MaxHealth);
	return true;
}

bool ACastleBoard::InitializeProgressiveTowerPresentation(AActor& Tower, UCastleMainTowerComponent& Health, FText& OutReason)
{
	const auto Reject = [&](const FString& Detail)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("棋盘 %s 的主塔 %s 无法布置塔尖和气球：%s。"),
			*GetPathName(), *Tower.GetPathName(), *Detail));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	};
	// 正式美术源由十段 500 cm 塔身和独立塔冠组成。用塔根局部包围盒下沿分段，
	// 跨过段尾的接缝件仍归原段。严格核对资产结构，避免换资产后错误隐藏碰撞或塔冠。
	constexpr int32 SourceSectionCount = 10;
	constexpr int32 PartsPerSection = 42;
	constexpr int32 CrownPartCount = 54;
	constexpr double SourceSectionHeight = 500.0;
	constexpr double SourceCrownBase = SourceSectionCount * SourceSectionHeight;
	USceneComponent* Root = Tower.GetRootComponent();
	if (!IsValid(Root)) { return Reject(TEXT("塔根缺失")); }
	TInlineComponentArray<UStaticMeshComponent*> Meshes(&Tower);
	TArray<UStaticMeshComponent*> Crown;
	TowerBodySections.SetNum(SourceSectionCount);
	double CrownTop = SourceCrownBase;
	const FTransform& TowerTransform = Tower.GetActorTransform();
	for (UStaticMeshComponent* Part : Meshes)
	{
		if (!IsValid(Part->GetStaticMesh()) || !Part->GetAttachParent()
			|| Part->GetAttachParent()->GetFName() != TEXT("BuildingMeshGroup"))
		{
			return Reject(FString::Printf(TEXT("分件 %s 的网格或 BuildingMeshGroup 挂接无效"), *Part->GetName()));
		}
		const FTransform Relative = Part->GetComponentTransform().GetRelativeTransform(TowerTransform);
		const FBox Bounds = Part->GetStaticMesh()->GetBoundingBox().TransformBy(Relative);
		if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN() || Bounds.Min.Z < -0.01)
		{
			return Reject(FString::Printf(TEXT("分件 %s 的塔根空间包围盒无效"), *Part->GetName()));
		}
		// 0.01 cm 只用于抵消世界变换往返误差，确保位于段界的原始分件稳定归组。
		const double Base = Bounds.Min.Z + 0.01;
		if (Base >= SourceCrownBase)
		{
			Crown.Add(Part);
			CrownTop = FMath::Max(CrownTop, Bounds.Max.Z);
		}
		else
		{
			const int32 Section = FMath::FloorToInt(Base / SourceSectionHeight);
			TowerBodySections[Section].Add({Part, Part->GetCollisionEnabled(), Part->CanEverAffectNavigation()});
		}
	}
	if (Crown.Num() != CrownPartCount || TowerBodySections.ContainsByPredicate([](const auto& Section)
		{ return Section.Num() != PartsPerSection; }))
	{
		return Reject(TEXT("预期十段塔身各 42 件、塔冠 54 件"));
	}

	USceneComponent* BodyAnchor = NewObject<USceneComponent>(&Tower, TEXT("TowerBodyAnchor"));
	USceneComponent* Anchor = NewObject<USceneComponent>(&Tower, TEXT("TowerCrownAnchor"));
	for (USceneComponent* Group : {BodyAnchor, Anchor})
	{
		Group->SetupAttachment(Root);
		Tower.AddInstanceComponent(Group);
		Group->RegisterComponent();
		if (!Group->IsRegistered()) { return Reject(TEXT("运行时塔身或塔冠锚点未注册")); }
	}
	// 按原始变换挂接全部美术，再拉伸塔身到棋盘层高。塔冠只平移，保留原有比例。
	for (int32 Layer = 0; Layer < TowerBodySections.Num(); ++Layer)
	{
		for (const FTowerBodyPart& Entry : TowerBodySections[Layer])
		{
			UStaticMeshComponent* Part = Entry.Mesh.Get();
			Part->SetMobility(EComponentMobility::Movable);
			if (!Part->AttachToComponent(BodyAnchor, FAttachmentTransformRules::KeepWorldTransform))
			{
				return Reject(FString::Printf(TEXT("无法挂载塔身分件 %s"), *Part->GetName()));
			}
			if (Layer >= CurrentUnlockedLayers)
			{
				// 隐藏的美术也退出射线、物理和寻路，避免未开放层继续阻挡搭建与通行。
				Part->SetVisibility(false);
				Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Part->SetCanEverAffectNavigation(false);
			}
		}
	}
	BodyAnchor->SetRelativeScale3D(FVector(1.0, 1.0, LayerHeight / SourceSectionHeight));
	for (UStaticMeshComponent* Part : Crown)
	{
		Part->SetMobility(EComponentMobility::Movable);
		if (!Part->AttachToComponent(Anchor, FAttachmentTransformRules::KeepWorldTransform))
		{
			return Reject(FString::Printf(TEXT("无法挂载塔尖分件 %s"), *Part->GetName()));
		}
	}
	Anchor->SetRelativeLocation(FVector(0, 0, CurrentUnlockedLayers * LayerHeight - SourceCrownBase));

	USceneComponent* BalloonAnchor = NewObject<USceneComponent>(&Tower, TEXT("TowerBalloonAnchor"));
	BalloonAnchor->SetupAttachment(Anchor);
	BalloonAnchor->SetRelativeLocation(FVector(0, 0, CrownTop + 10.0));
	Tower.AddInstanceComponent(BalloonAnchor);
	BalloonAnchor->RegisterComponent();
	UCastleMainTowerBalloonComponent* Balloons = NewObject<UCastleMainTowerBalloonComponent>(&Tower, TEXT("TowerBalloons"));
	Tower.AddInstanceComponent(Balloons);
	Balloons->RegisterComponent();
	if (!Balloons->Initialize(BalloonAnchor, OutReason) || !Health.AttachBalloonPresentation(Balloons, OutReason)) { return false; }
	TowerCrownAnchor = Anchor;
	return true;
}

bool ACastleBoard::ExpandMainTowerLayers(const int32 TargetLayerCount, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	const auto Reject = [&](const TCHAR* Detail)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("棋盘 %s 无法将主塔 %s 扩层到 %d 层：%s。"),
			*GetPathName(), *GetPathNameSafe(MainTowerActor), TargetLayerCount, Detail));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	};
	if (!UsesProgressiveTowerLayers() || !IsValid(MainTowerActor) || !IsValid(TowerSideGrid) || !IsValid(TowerCrownAnchor)
		|| TowerBodySections.Num() <= MaxTowerLayers)
	{
		return Reject(TEXT("当前地图未启用扩层，或塔身、塔尖、侧格组件未就绪"));
	}
	if (TargetLayerCount <= CurrentUnlockedLayers || TargetLayerCount > MaxTowerLayers)
	{
		return Reject(TEXT("目标层数必须严格大于当前开放层数且不得超过最大主塔层数"));
	}
	for (int32 Layer = CurrentUnlockedLayers; Layer < TargetLayerCount; ++Layer)
	{
		for (const FTowerBodyPart& Part : TowerBodySections[Layer])
		{
			if (!Part.Mesh.IsValid() || !Part.Mesh->IsRegistered()) { return Reject(TEXT("新增层的塔身网格已失效")); }
		}
	}
	if (!TowerSideGrid->ExpandToLayerCount(TargetLayerCount, OutReason)) { return false; }

	// 确认侧格整批生成后再显出既有分件，同时保持旧层的网格变换与房间索引不动。
	FScopedCastleNavigationChange GeometryChange(GetWorld(), this);
	for (int32 Layer = CurrentUnlockedLayers; Layer < TargetLayerCount; ++Layer)
	{
		for (const FTowerBodyPart& Part : TowerBodySections[Layer])
		{
			Part.Mesh->SetVisibility(true);
			Part.Mesh->SetCollisionEnabled(Part.Collision);
			Part.Mesh->SetCanEverAffectNavigation(Part.bAffectsNavigation);
		}
	}

	// 塔尖美术和气球锚点共用父组件，恰好上移所增加的格高。
	TowerCrownAnchor->AddRelativeLocation(FVector(0, 0, (TargetLayerCount - CurrentUnlockedLayers) * LayerHeight));
	CurrentUnlockedLayers = TargetLayerCount;
	UE_LOG(LogMoving_Castle, Display, TEXT("棋盘 %s 的主塔已扩展到 %d 层。"), *GetPathName(), CurrentUnlockedLayers);
	return true;
}

bool ACastleBoard::SpawnInitialEnemies(FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (!IsValid(CombatData) || !CombatData->ValidateConfiguration(OutFailureReason))
	{
		return false;
	}
	if (!IsValid(MainTowerActor))
	{
		OutFailureReason = FText::FromString(TEXT("生成初始敌人前必须先生成主塔。"));
		return false;
	}
	if (IsValid(InitialEnemySpawner))
	{
		OutFailureReason = FText::FromString(TEXT("棋盘已经生成过初始敌人批次，不能重复生成。"));
		return false;
	}

	const FTransform SpawnerTransform(GetActorQuat(), GetActorLocation(), GetActorScale3D());
	ACastleMassEnemySpawner* Spawner = GetWorld()->SpawnActorDeferred<ACastleMassEnemySpawner>(
		ACastleMassEnemySpawner::StaticClass(), SpawnerTransform, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Spawner == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("World 延迟生成 Mass 敌人生成器失败。"));
		return false;
	}
	Spawner->Configuration = CombatData->EnemyConfiguration;
	Spawner->Count = CombatData->InitialEnemyCount;
	Spawner->InnerRadius = CombatData->EnemySpawnInnerRadius;
	Spawner->OuterRadius = CombatData->EnemySpawnOuterRadius;
	Spawner->HeightOffset = CombatData->EnemySpawnHeightOffset;
	Spawner->RandomSeed = CombatData->EnemySpawnSeed;
	Spawner->bSpawnOnBeginPlay = true;
	Spawner->FinishSpawning(SpawnerTransform);

	if (Spawner->SpawnedUnits.Num() != CombatData->InitialEnemyCount)
	{
		Spawner->Destroy();
		OutFailureReason = FText::FromString(TEXT("Mass 敌人生成器 BeginPlay 没有生成完整的初始批次。"));
		return false;
	}

	InitialEnemySpawner = Spawner;
	UE_LOG(LogMoving_Castle, Display, TEXT("已生成 %d 个主塔初始敌人，圆环范围 %.1f~%.1f cm。"),
		Spawner->SpawnedUnits.Num(), Spawner->InnerRadius, Spawner->OuterRadius);
	return true;
}

bool ACastleBoard::GenerateGround(FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (PlacementSurfaceMode != ECastlePlacementSurfaceMode::GroundBoard)
	{
		OutFailureReason = FText::FromString(TEXT("当前不是地面棋盘模式，不能生成地块。"));
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s：%s"), *GetPathName(), *OutFailureReason.ToString());
		return false;
	}
	if (!ValidateBoardDimensions(OutFailureReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 拒绝生成地面：%s"), *GetName(), *OutFailureReason.ToString());
		return false;
	}

	if (GroundGeneratorClass == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("棋盘没有配置地面生成策略类。"));
		return false;
	}

	if (GridSize.X <= 0 || GridSize.Y <= 0 || GridSize.Z <= 0)
	{
		OutFailureReason = FText::FromString(TEXT("棋盘尺寸必须在 X、Y、Z 三个方向都大于 0。"));
		return false;
	}

	if (BuildingByCoordinate.Num() > 0)
	{
		OutFailureReason = FText::FromString(TEXT("场上已经存在建筑，不能直接重新生成地面。"));
		return false;
	}

	UCastleGroundGenerator* Generator = NewObject<UCastleGroundGenerator>(this, GroundGeneratorClass);
	if (Generator == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("无法创建地面生成策略对象。"));
		return false;
	}

	FCastleGroundGenerationRequest Request;
	FIntPoint Min, Max;
	GetBoardExtent(Min, Max);
	Request.GridSize = FIntVector(Max.X-Min.X+1, Max.Y-Min.Y+1, GridSize.Z);
	Request.Origin = FIntVector(Min.X, Min.Y, GridOrigin.Z);
	Request.Seed = GroundSeed;

	const FCastleGroundGenerationResult Result = Generator->GenerateGround(Request);
	return ApplyGroundGenerationResult(Result, OutFailureReason);
}

bool ACastleBoard::ApplyGroundGenerationResult(const FCastleGroundGenerationResult& Result, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (PlacementSurfaceMode != ECastlePlacementSurfaceMode::GroundBoard)
	{
		OutFailureReason = FText::FromString(TEXT("当前不是地面棋盘模式，不能接收地块。"));
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s：%s"), *GetPathName(), *OutFailureReason.ToString());
		return false;
	}
	if (!ValidateBoardDimensions(OutFailureReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 拒绝接收地面结果：%s"), *GetName(), *OutFailureReason.ToString());
		return false;
	}

	if (BuildingByCoordinate.Num() > 0)
	{
		OutFailureReason = FText::FromString(TEXT("场上已经存在建筑，不能替换现有地面结果。"));
		return false;
	}

	if (GridSize.X <= 0 || GridSize.Y <= 0 || GridSize.Z <= 0)
	{
		OutFailureReason = FText::FromString(TEXT("棋盘尺寸无效，无法接收地面结果。"));
		return false;
	}

	FIntPoint Min, Max;
	GetBoardExtent(Min, Max);
	if (!ValidateGroundResult(Result, Min, Max, OutFailureReason)) { return false; }

	// 数据和 Actor 都准备完成后才提交新地面，失败时保留原棋盘状态。
	const FScopedCastleNavigationChange NavigationChange(GetWorld(), this);
	if (!RebuildGroundVisualization(Result.Cells, OutFailureReason))
	{
		return false;
	}
	GeneratedGroundCells = Result.Cells;
	return true;
}

bool ACastleBoard::ValidateGroundResult(const FCastleGroundGenerationResult& Result, const FIntPoint Min,
	const FIntPoint Max, FText& OutFailureReason) const
{
	const int64 ExpectedCellCount = static_cast<int64>(Max.X - Min.X + 1) * (Max.Y - Min.Y + 1);
	if (Result.Cells.Num() != ExpectedCellCount)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("地面结果数量不完整：需要 %lld 个，实际收到 %d 个。"), ExpectedCellCount, Result.Cells.Num()));
		return false;
	}

	TSet<FIntVector> UniqueCoordinates;
	UniqueCoordinates.Reserve(Result.Cells.Num());

	for (const FCastleGroundCell& Cell : Result.Cells)
	{
		if (Cell.Coordinate.X < Min.X || Cell.Coordinate.X > Max.X
			|| Cell.Coordinate.Y < Min.Y || Cell.Coordinate.Y > Max.Y || Cell.Coordinate.Z != GridOrigin.Z)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("地面结果包含越界或非地面层坐标 (%d, %d, %d)。"), Cell.Coordinate.X, Cell.Coordinate.Y, Cell.Coordinate.Z));
			return false;
		}

		if (UniqueCoordinates.Contains(Cell.Coordinate))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("地面结果包含重复坐标 (%d, %d, %d)。"), Cell.Coordinate.X, Cell.Coordinate.Y, Cell.Coordinate.Z));
			return false;
		}
		UniqueCoordinates.Add(Cell.Coordinate);

		if (Cell.Number < 1 || Cell.Number > 9 || Cell.Color < ECastleTileColor::Red || Cell.Color > ECastleTileColor::Purple)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("地面坐标 (%d, %d, %d) 必须使用五色之一与 1 至 9 数字，实际颜色=%d，数字=%d。"),
				Cell.Coordinate.X, Cell.Coordinate.Y, Cell.Coordinate.Z, static_cast<int32>(Cell.Color), Cell.Number));
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 拒绝非法地面：%s"), *GetName(), *OutFailureReason.ToString());
			return false;
		}
	}

	return true;
}

void ACastleBoard::SetGroundPresentationHidden(const bool bInHidden)
{
	if (bGroundPresentationHidden == bInHidden) { return; }
	// 先核对整份索引，损坏状态明确报错，不能静默略过丢失的地块。
	for (const auto& Pair : GroundTiles)
	{
		if (!IsValid(Pair.Value))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 无法切换地块显隐：坐标 %s 的 Actor 已失效。"),
				*GetName(), *Pair.Key.ToString());
			return;
		}
	}
	for (const auto& Pair : GroundTiles) { Pair.Value->SetGroundPresentationHidden(bInHidden); }
	bGroundPresentationHidden = bInHidden;
}

void ACastleBoard::GetBoardExtent(FIntPoint& OutMin, FIntPoint& OutMax) const
{
	OutMin = FIntPoint(GridOrigin.X - ExpansionRings, GridOrigin.Y - ExpansionRings);
	OutMax = FIntPoint(GridOrigin.X + GridSize.X - 1 + ExpansionRings, GridOrigin.Y + GridSize.Y - 1 + ExpansionRings);
}

bool ACastleBoard::ExpandBoard(FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	// 所有拒绝都发生在提交之前；既不临时改尺寸，也不重建已有 Actor。
	const auto Reject = [this, &OutFailureReason](const FString& Reason)
	{
		OutFailureReason = FText::FromString(Reason);
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 拒绝扩张：%s"), *GetName(), *Reason);
		return false;
	};
	if (PlacementSurfaceMode != ECastlePlacementSurfaceMode::GroundBoard)
	{
		return Reject(TEXT("地面扩张仅用于地面棋盘模式。"));
	}
	if (!ValidateBoardDimensions(OutFailureReason)) { return Reject(OutFailureReason.ToString()); }
	FIntPoint Min, Max;
	GetBoardExtent(Min, Max);
	if (Max.X - Min.X + 3 > MaxBoardSize || Max.Y - Min.Y + 3 > MaxBoardSize)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("当前尺寸 %d×%d，已达上限 %d×%d，不能再扩张一圈。"),
			Max.X-Min.X+1, Max.Y-Min.Y+1, MaxBoardSize, MaxBoardSize));
		UE_LOG(LogMoving_Castle, Warning, TEXT("棋盘 %s：%s"), *GetName(), *OutFailureReason.ToString());
		return false;
	}
	FCastleGroundGenerationResult Current;
	Current.Cells = GeneratedGroundCells;
	if (!ValidateGroundResult(Current, Min, Max, OutFailureReason)) { return Reject(OutFailureReason.ToString()); }
	if (GetWorld())
	{
		if (GroundTiles.Num() != GeneratedGroundCells.Num())
		{ return Reject(TEXT("已有地块 Actor 索引数量与地面数据不一致。")); }
		for (const FCastleGroundCell& Cell : GeneratedGroundCells)
		{
			const auto* Tile = GroundTiles.Find(Cell.Coordinate);
			if (!Tile || !IsValid(Tile->Get()))
			{ return Reject(FString::Printf(TEXT("已有地块 Actor 缺失或失效，坐标 %s。"), *Cell.Coordinate.ToString())); }
		}
	}
	if (!GroundGeneratorClass) { return Reject(TEXT("GroundGeneratorClass 未配置。")); }
	UCastleGroundGenerator* Generator = NewObject<UCastleGroundGenerator>(this, GroundGeneratorClass);
	if (!Generator) { return Reject(TEXT("GroundGeneratorClass 无法创建生成器。")); }
	FCastleGroundGenerationRequest Request;
	Request.Origin = FIntVector(Min.X-1, Min.Y-1, GridOrigin.Z);
	Request.GridSize = FIntVector(Max.X-Min.X+3, Max.Y-Min.Y+3, GridSize.Z);
	Request.Seed = GroundSeed;
	const auto Candidate = Generator->GenerateGround(Request);
	if (!ValidateGroundResult(Candidate, Min-FIntPoint(1,1), Max+FIntPoint(1,1), OutFailureReason))
	{ return Reject(OutFailureReason.ToString()); }
	for (const auto& Old : GeneratedGroundCells)
	{
		FCastleGroundCell New;
		if (!Candidate.FindCell(Old.Coordinate, New) || New.Color != Old.Color || New.Number != Old.Number)
		{
			return Reject(FString::Printf(TEXT("生成器 %s 改变已有地块 (%d, %d, %d) 的颜色或数字。"),
				*GroundGeneratorClass->GetName(), Old.Coordinate.X, Old.Coordinate.Y, Old.Coordinate.Z));
		}
	}
	TArray<FCastleGroundCell> Added;
	for (const auto& Cell : Candidate.Cells)
	{
		// 明确的增量集合：旧范围已逐格校验，仅对外围格创建 Actor。
		if (Cell.Coordinate.X < Min.X || Cell.Coordinate.X > Max.X || Cell.Coordinate.Y < Min.Y || Cell.Coordinate.Y > Max.Y)
		{ Added.Add(Cell); }
	}
	TMap<FIntVector, TObjectPtr<ACastleGroundTileActor>> NewTiles;
	if (!CreateGroundTileActors(Added, NewTiles, OutFailureReason)) { return Reject(OutFailureReason.ToString()); }
	// 地块从不参与导航，因此扩张不能发起导航版本变化或让共享巡逻路线失效。
	GroundTiles.Append(NewTiles);
	GeneratedGroundCells.Append(Added);
	++ExpansionRings;
	GetBoardExtent(Min, Max);
	UE_LOG(LogMoving_Castle, Display, TEXT("棋盘 %s 扩张成功：范围 (%d,%d)～(%d,%d)，尺寸 %d×%d。"),
		*GetName(), Min.X, Min.Y, Max.X, Max.Y, Max.X-Min.X+1, Max.Y-Min.Y+1);
	OnBoardExpanded.Broadcast(this);
	return true;
}

bool ACastleBoard::TryGetGroundCell(const FIntVector& Coordinate, FCastleGroundCell& OutCell) const
{
	for (const FCastleGroundCell& Cell : GeneratedGroundCells)
	{
		if (Cell.Coordinate == Coordinate)
		{
			OutCell = Cell;
			return true;
		}
	}

	return false;
}

bool ACastleBoard::UsesProgressiveTowerLayers() const
{
	return PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides && bEnableProgressiveTowerLayers;
}

int32 ACastleBoard::GetUnlockedLayerCount() const
{
	return UsesProgressiveTowerLayers() ? CurrentUnlockedLayers : GridSize.Z;
}

bool ACastleBoard::IsCoordinateWithinBoard(const FIntVector& Coordinate) const
{
	const FIntVector RelativeCoordinate = Coordinate - GridOrigin;
	return RelativeCoordinate.X >= -ExpansionRings && RelativeCoordinate.X < GridSize.X + ExpansionRings
		&& RelativeCoordinate.Y >= -ExpansionRings && RelativeCoordinate.Y < GridSize.Y + ExpansionRings
		&& RelativeCoordinate.Z >= 0 && RelativeCoordinate.Z < GetUnlockedLayerCount();
}

bool ACastleBoard::CanPlaceBuilding(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason) const
{
	return CanPlaceBuildingInternal(Coordinate, Descriptor, nullptr, OutFailureReason);
}

bool ACastleBoard::CanPlaceBuildingAtCursor(const FCastleGridCursorTarget& CursorTarget, const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason) const
{
	FIntVector Coordinate;
	return TryResolvePlacementCoordinate(CursorTarget, Coordinate, OutFailureReason)
		&& CanPlaceBuildingInternal(Coordinate, Descriptor, &CursorTarget, OutFailureReason);
}

bool ACastleBoard::CanPlaceBuildingInternal(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, const FCastleGridCursorTarget* CursorTarget, FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();

	if (!ValidateBuildingDescriptor(Descriptor, OutFailureReason))
	{
		return false;
	}

	FIntPoint Min, Max;
	GetBoardExtent(Min, Max);
	const int32 UnlockedLayers = GetUnlockedLayerCount();
	const TArray<FIntVector> Footprint = BuildPlacementFootprint(Coordinate, Descriptor.Level);
	for (const FIntVector& Cell : Footprint)
	{
		if (Cell.Z < GridOrigin.Z || Cell.Z >= GridOrigin.Z + UnlockedLayers)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("建筑高度超出当前开放的 %d 层。"), UnlockedLayers));
			return false;
		}
		if (Cell.X < Min.X-MaxOverhangCells || Cell.X > Max.X+MaxOverhangCells
			|| Cell.Y < Min.Y-MaxOverhangCells || Cell.Y > Max.Y+MaxOverhangCells)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("最多伸出棋盘边界 %d 格。"), MaxOverhangCells));
			return false;
		}
		// 原本的房间占用不能描述主塔，否则主塔会被计数、合成或当成 PCG 邻居。
		if (IsMainTowerCellReserved(Cell))
		{
			OutFailureReason = FText::FromString(TEXT("建筑不能占用主塔塔身。"));
			return false;
		}
		if (GetBuildingAt(Cell) != nullptr)
		{
			OutFailureReason = FText::FromString(TEXT("建筑占用范围中有格子已被占用。"));
			return false;
		}
	}
	// 整座建筑只逐格检查高度、水平边界与占用，不要求下方存在地面或建筑。
	// 悬空和多格建筑部分悬空都允许；属性仍必须匹配当前选中的唯一来源面。
	return HasPlacementMatch(Coordinate, Descriptor, CursorTarget, OutFailureReason);
}

ACastleBuildingBase* ACastleBoard::PlaceBuilding(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, bool bTryMerge, FText& OutFailureReason)
{
	return PlaceBuildingInternal(Coordinate, Descriptor, bTryMerge, nullptr, OutFailureReason);
}

ACastleBuildingBase* ACastleBoard::PlaceBuildingAtCursor(const FCastleGridCursorTarget& CursorTarget,
	const FCastleBuildingDescriptor& Descriptor, const bool bTryMerge, FText& OutFailureReason)
{
	FIntVector Coordinate;
	return TryResolvePlacementCoordinate(CursorTarget, Coordinate, OutFailureReason)
		? PlaceBuildingInternal(Coordinate, Descriptor, bTryMerge, &CursorTarget, OutFailureReason) : nullptr;
}

ACastleBuildingBase* ACastleBoard::PlaceBuildingInternal(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, bool bTryMerge, const FCastleGridCursorTarget* CursorTarget, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (!CanPlaceBuildingInternal(Coordinate, Descriptor, CursorTarget, OutFailureReason))
	{
		return nullptr;
	}

	const TArray<FIntVector> Footprint = BuildPlacementFootprint(Coordinate, Descriptor.Level);
	// 在生成带碰撞的 Actor 之前暂停导航；动画观察继续持有暂停直到原有结束信号到达。
	const FScopedCastleNavigationChange NavigationChange(GetWorld(), this);
	ACastleBuildingBase* NewBuilding = SpawnBuildingActor(Descriptor, Footprint, OutFailureReason);
	if (NewBuilding == nullptr)
	{
		return nullptr;
	}

	if (!RegisterBuilding(NewBuilding, OutFailureReason))
	{
		NewBuilding->Destroy();
		return nullptr;
	}
	if (auto* PCG = GetWorld()->GetSubsystem<UCastleBuildingPCGSubsystem>()) { PCG->ObserveBoard(this); }

	if (GetWorld()->IsGameWorld())
	{
		// 格子立即占用，背包可立即扣除；视觉和实际等级替换严格等待原有飞入组件完成。
		// 每次放置都播放，即便调用者本次不希望自动合并，也不能跳过首次入场。
		NewBuilding->SetPlacementComplete(false);
		FPendingPlacement Request;
		Request.bTryMerge = bTryMerge;
		Request.Origin = bUseSharedVisualChangeOrigin ? VisualChangeOrigin : NewBuilding->GetActorLocation();
		PendingPlacements.Add(NewBuilding, Request);
		NewBuilding->AssemblyFlight->OnAssemblyFinishedNative.AddUObject(this, &ACastleBoard::HandlePlacementFinished);
		if (!NewBuilding->AssemblyFlight->StartAssembly())
		{
			// 起播前校验失败视为本次放置失败，移除占用并让背包入口恢复卡牌，不改为瞬间出现。
			OutFailureReason = FText::FromString(NewBuilding->AssemblyFlight->LastError);
			ForgetPlacement(NewBuilding);
			FText UnregisterReason;
			if (!UnregisterBuilding(NewBuilding, UnregisterReason))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("飞入起播失败后的占用清理失败：%s"), *UnregisterReason.ToString());
			}
			NewBuilding->Destroy();
			return nullptr;
		}
		return NewBuilding;
	}

	// 编辑器世界只支持摆放/规则编辑，不执行游戏动画；实际游戏入口始终走上述完成事件。
	if (!bTryMerge)
	{
		return NewBuilding;
	}

	// 合成失败不会撤销已经合法放置的建筑；只有在替换流程确实出错时记录原因。
	return TryMergePlacedBuilding(NewBuilding, bUseSharedVisualChangeOrigin ? VisualChangeOrigin : NewBuilding->GetActorLocation(), OutFailureReason);
}

void ACastleBoard::HandlePlacementFinished(UAssemblyFlightComponent* Flight, const bool bCompleted)
{
	ACastleBuildingBase* Building = Cast<ACastleBuildingBase>(Flight->GetOwner());
	const FPendingPlacement* Found = PendingPlacements.Find(Building);
	if (!Found) { return; }
	const FPendingPlacement Request = *Found;
	ForgetPlacement(Building);
	if (!bCompleted)
	{
		// Reset/失败只清理请求，不将不完整建筑标成可合并。重新放置即可重新进入完整流程。
		const FString Detail = Flight->LastError.IsEmpty() ? TEXT("放置飞入已取消。") : Flight->LastError;
		UE_LOG(LogMoving_Castle, Warning, TEXT("放置中止，不触发合并 [%s]：%s"), *GetNameSafe(Building), *Detail);
		ShowPrototypeMessage(Detail, FColor::Yellow);
		return;
	}
	if (!IsValid(Building) || Building->GetOccupiedCoordinates().IsEmpty()
		|| GetBuildingAt(Building->GetOccupiedCoordinates()[0]) != Building)
	{
		return;
	}
	Building->SetPlacementComplete(true);
	UE_LOG(LogMoving_Castle, Display, TEXT("放置飞入完成，开始检查合并 [%s]。"), *Building->GetName());
	if (Request.bTryMerge) { ReadyMergeRequests.Add(Building, Request.Origin); }
	ResolveReadyMerges();
	OnLayoutCommitted.Broadcast(this, Request.Origin);
}

void ACastleBoard::ResolveReadyMerges()
{
	// 完成顺序可以与放置顺序不同。所有仍有效的自动合并请求重新检查，双方都就绪才允许升级。
	TArray<TWeakObjectPtr<ACastleBuildingBase>> Requests;
	ReadyMergeRequests.GenerateKeyArray(Requests);
	Requests.Sort([](const TWeakObjectPtr<ACastleBuildingBase>& A, const TWeakObjectPtr<ACastleBuildingBase>& B)
	{
		return A.GetEvenIfUnreachable()->GetUniqueID() < B.GetEvenIfUnreachable()->GetUniqueID();
	});
	for (const TWeakObjectPtr<ACastleBuildingBase>& Request : Requests)
	{
		ACastleBuildingBase* Building = Request.Get();
		const FVector* Origin = ReadyMergeRequests.Find(Request);
		if (!IsValid(Building) || !Origin)
		{
			ReadyMergeRequests.Remove(Request);
			continue;
		}
		FText Reason;
		const FVector CapturedOrigin = *Origin;
		ACastleBuildingBase* Result = TryMergePlacedBuilding(Building, CapturedOrigin, Reason);
		if (!Reason.IsEmpty())
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("放置完成后的合并失败：%s"), *Reason.ToString());
			ShowPrototypeMessage(Reason.ToString(), FColor::Red);
		}
		else if (Result != Building)
		{
			ShowPrototypeMessage(FString::Printf(TEXT("合成完成：Lv%d，占用 %d 格。"), Result->GetBuildingLevel(), Result->GetOccupiedCoordinates().Num()), FColor::Green);
			// 若更高等级邻居仍在放置，保留这次操作上下文，等待它完成后可继续升级。
			if (Result->CanMergeToNextLevel()) { ReadyMergeRequests.Add(Result, CapturedOrigin); }
		}
		else if (!Building->CanMergeToNextLevel())
		{
			ReadyMergeRequests.Remove(Request);
		}
	}
}

void ACastleBoard::ForgetPlacement(ACastleBuildingBase* Building)
{
	if (!Building) { return; }
	Building->AssemblyFlight->OnAssemblyFinishedNative.RemoveAll(this);
	PendingPlacements.Remove(Building);
	ReadyMergeRequests.Remove(Building);
}

void ACastleBoard::HandleBuildingDestroyed(AActor* Actor)
{
	const FScopedCastleNavigationChange NavigationChange(GetWorld(), this);
	ACastleBuildingBase* Building = CastChecked<ACastleBuildingBase>(Actor);
	Building->BuildingEffect->OnBoardUnregistered(*this);
	if (auto* Triggers = GetWorld()->GetSubsystem<UCastleBuildingTriggerSubsystem>()) { Triggers->UnregisterBuilding(Building); }
	ForgetPlacement(Building);
	// 也覆盖关卡代码直接 Destroy 的中途取消；合并路径已先注销，不能删除新结果的坐标。
	bool bRemovedOccupancy = false;
	for (const FIntVector& Coordinate : Building->GetOccupiedCoordinates())
	{
		if (GetBuildingAt(Coordinate) == Building) { BuildingByCoordinate.Remove(Coordinate); bRemovedOccupancy = true; }
	}
	if (bRemovedOccupancy && !IsActorBeingDestroyed() && !GetWorld()->bIsTearingDown) { OnLayoutCommitted.Broadcast(this, Building->GetActorLocation()); }
}

void ACastleBoard::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const auto& Pair : BuildingByCoordinate)
	{
		if (ACastleBuildingBase* Building = Pair.Value.Get(); IsValid(Building))
		{
			Building->BuildingEffect->OnBoardUnregistered(*this);
			// 流送移除棋盘时不留下无人管理的触发区域；多格重复清理由子系统保持幂等。
			if (auto* Triggers = GetWorld()->GetSubsystem<UCastleBuildingTriggerSubsystem>()) { Triggers->UnregisterBuilding(Building); }
			Building->AssemblyFlight->OnAssemblyFinishedNative.RemoveAll(this);
			Building->OnDestroyed.RemoveDynamic(this, &ACastleBoard::HandleBuildingDestroyed);
		}
	}
	PendingPlacements.Reset();
	ReadyMergeRequests.Reset();
	DestroyGroundTileActors();
	// 塔侧格随主塔回收；单独移除棋盘时也不能留下可被射线命中的幽灵侧格。
	if (IsValid(TowerSideGrid) && IsValid(MainTowerActor) && EndPlayReason == EEndPlayReason::Destroyed)
	{
		MainTowerActor->Destroy();
	}
	TowerSideGrid = nullptr;
	Super::EndPlay(EndPlayReason);
}

ACastleBuildingBase* ACastleBoard::PlaceBuildingFromBackpack(const FIntVector& Coordinate, UCastleBackpackComponent* Backpack, int32 BackpackIndex, bool bTryMerge, FText& OutFailureReason)
{
	return PlaceBuildingFromBackpackInternal(Coordinate, Backpack, BackpackIndex, bTryMerge, nullptr, OutFailureReason);
}

ACastleBuildingBase* ACastleBoard::PlaceBuildingFromBackpackInternal(const FIntVector& Coordinate, UCastleBackpackComponent* Backpack, int32 BackpackIndex, bool bTryMerge, const FCastleGridCursorTarget* CursorTarget, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (Backpack == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("放置建筑时没有提供背包组件。"));
		return nullptr;
	}

	FCastleBuildingDescriptor Descriptor;
	if (!Backpack->TryGetItemAt(BackpackIndex, Descriptor))
	{
		OutFailureReason = FText::FromString(TEXT("指定的背包槽位不存在。"));
		return nullptr;
	}

	// 先做完整校验，再取出卡牌；这样失败时背包不会发生任何变化。
	if (!CanPlaceBuildingInternal(Coordinate, Descriptor, CursorTarget, OutFailureReason))
	{
		return nullptr;
	}

	FCastleBuildingDescriptor RemovedDescriptor;
	FText RemoveReason;
	if (!Backpack->TryRemoveItemAt(BackpackIndex, RemovedDescriptor, RemoveReason))
	{
		OutFailureReason = RemoveReason;
		return nullptr;
	}

	ACastleBuildingBase* PlacedBuilding = PlaceBuildingInternal(Coordinate, RemovedDescriptor, bTryMerge, CursorTarget, OutFailureReason);
	if (PlacedBuilding != nullptr)
	{
		// 建筑放置成功后才提交消费，失败时仍可将卡牌回插原槽。
		Backpack->CommitAutoRefillFromTempBar();
		return PlacedBuilding;
	}

	// 生成失败时恢复卡牌；恢复失败属于严重状态错误，必须显式记录而不是静默丢牌。
	FText RestoreReason;
	if (!Backpack->TryInsertItemAt(BackpackIndex, RemovedDescriptor, RestoreReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("从背包放置失败后的卡牌恢复失败：%s"), *RestoreReason.ToString());
	}

	return nullptr;
}

bool ACastleBoard::RemoveBuilding(ACastleBuildingBase* Building, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (Building == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("要移除的建筑为空。"));
		return false;
	}

	const FScopedCastleNavigationChange NavigationChange(GetWorld(), this);
	if (!UnregisterBuilding(Building, OutFailureReason))
	{
		return false;
	}

	const FVector RemovalOrigin = bUseSharedVisualChangeOrigin ? VisualChangeOrigin : Building->GetActorLocation();
	if (!Building->Destroy())
	{
		OutFailureReason = FText::FromString(TEXT("建筑已从棋盘索引移除，但 Actor 销毁失败。"));
		return false;
	}
	OnLayoutCommitted.Broadcast(this, RemovalOrigin);
	return true;
}

bool ACastleBoard::RemoveBuildingToBackpack(ACastleBuildingBase* Building, UCastleBackpackComponent* Backpack, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (Building == nullptr || Backpack == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("移除建筑并放回背包时缺少建筑或背包。"));
		return false;
	}

	const FCastleBuildingDescriptor Descriptor = Building->GetBuildingDescriptor();
	int32 AddedIndex = INDEX_NONE;
	FText AddReason;
	if (!Backpack->TryAddItem(Descriptor, AddedIndex, AddReason))
	{
		OutFailureReason = AddReason;
		return false;
	}

	FText RemoveReason;
	if (RemoveBuilding(Building, RemoveReason))
	{
		return true;
	}

	// 棋盘移除失败时回滚刚加入的卡牌，避免出现 Actor 和背包状态不一致。
	FCastleBuildingDescriptor RolledBackDescriptor;
	FText RollbackReason;
	if (!Backpack->TryRemoveItemAt(AddedIndex, RolledBackDescriptor, RollbackReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("移除建筑回滚背包失败：%s"), *RollbackReason.ToString());
	}

	OutFailureReason = RemoveReason;
	return false;
}

int32 ACastleBoard::GetPlacedBuildingCount() const
{
	TSet<ACastleBuildingBase*> UniqueBuildings;
	for (const TPair<FIntVector, TObjectPtr<ACastleBuildingBase>>& Pair : BuildingByCoordinate)
	{
		if (Pair.Value != nullptr)
		{
			UniqueBuildings.Add(Pair.Value.Get());
		}
	}

	return UniqueBuildings.Num();
}

TArray<ACastleBuildingBase*> ACastleBoard::GetPlacedBuildings() const
{
	TSet<ACastleBuildingBase*> UniqueBuildings;
	for (const TPair<FIntVector, TObjectPtr<ACastleBuildingBase>>& Pair : BuildingByCoordinate)
	{
		if (Pair.Value != nullptr)
		{
			UniqueBuildings.Add(Pair.Value.Get());
		}
	}

	TArray<ACastleBuildingBase*> Result;
	Result.Reserve(UniqueBuildings.Num());
	for (ACastleBuildingBase* Building : UniqueBuildings)
	{
		Result.Add(Building);
	}

	return Result;
}

ACastleBoard* ACastleBoard::FindBoardForBuilding(const ACastleBuildingBase* Building)
{
	if (!IsValid(Building) || !Building->GetWorld()) { return nullptr; }
	const TArray<FIntVector> Cells = Building->GetOccupiedCoordinates();
	if (Cells.IsEmpty()) { return nullptr; }
	for (TActorIterator<ACastleBoard> It(Building->GetWorld()); It; ++It)
	{
		if (It->GetBuildingAt(Cells[0]) == Building) { return *It; }
	}
	return nullptr;
}

bool ACastleBoard::ValidateEffectQuerySource(const ACastleBuildingBase* Source) const
{
	if (!IsValid(Source) || Source->GetWorld() != GetWorld() || !Source->CanReceiveBuildingTrigger()
		|| Source->GetOccupiedCoordinates().IsEmpty())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 空间查询失败：源建筑 %s 无效、未初始化或未完成建造。"),
			*GetPathName(), *GetPathNameSafe(Source));
		return false;
	}
	for (const FIntVector& Cell : Source->GetOccupiedCoordinates())
	{
		if (GetBuildingAt(Cell) != Source)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 空间查询失败：源建筑 %s 的占用索引不完整。"), *GetPathName(), *Source->GetPathName());
			return false;
		}
	}
	return true;
}

bool ACastleBoard::GetBuildingsBelow(const ACastleBuildingBase* Source, int32 LayerCount,
	TArray<ACastleBuildingBase*>& OutBuildings) const
{
	OutBuildings.Reset();
	if (!ValidateEffectQuerySource(Source)) { return false; }
	if (LayerCount < -1)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 下层查询失败：建筑 %s 的层数 %d 非法。"), *GetPathName(), *Source->GetPathName(), LayerCount);
		return false;
	}
	FIntVector Min = Source->GetOccupiedCoordinates()[0], Max = Min;
	for (const FIntVector& Cell : Source->GetOccupiedCoordinates()) { Min = Min.ComponentMin(Cell); Max = Max.ComponentMax(Cell); }
	TSet<ACastleBuildingBase*> Unique;
	for (const auto& Pair : BuildingByCoordinate)
	{
		const FIntVector& Cell = Pair.Key;
		// D6 定义的集合只有正下方若干层；地面不在建筑占用索引中，多格建筑只贡献一次数字。
		if (Cell.X < Min.X || Cell.X > Max.X || Cell.Y < Min.Y || Cell.Y > Max.Y || Cell.Z >= Min.Z
			|| (LayerCount >= 0 && static_cast<int64>(Min.Z) - Cell.Z > LayerCount)) { continue; }
		if (!IsValid(Pair.Value))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 下层查询遇到失效建筑索引，源=%s，格=%s。"), *GetPathName(), *Source->GetPathName(), *Cell.ToString());
			return false;
		}
		if (Pair.Value != Source && Pair.Value->CanReceiveBuildingTrigger()) { Unique.Add(Pair.Value); }
	}
	OutBuildings = Unique.Array();
	return true;
}

bool ACastleBoard::GetBuildingsInExpandedBounds(const ACastleBuildingBase* Source, const FIntVector& Expansion,
	TArray<ACastleBuildingBase*>& OutBuildings) const
{
	OutBuildings.Reset();
	if (!ValidateEffectQuerySource(Source)) { return false; }
	if (Expansion.X < 0 || Expansion.Y < 0 || Expansion.Z < 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 外扩查询失败：建筑 %s 的外扩量 %s 不能为负。"), *GetPathName(), *Source->GetPathName(), *Expansion.ToString());
		return false;
	}
	FIntVector Min = Source->GetOccupiedCoordinates()[0], Max = Min;
	for (const FIntVector& Cell : Source->GetOccupiedCoordinates()) { Min = Min.ComponentMin(Cell); Max = Max.ComponentMax(Cell); }
	TSet<ACastleBuildingBase*> Unique;
	for (const auto& Pair : BuildingByCoordinate)
	{
		const FIntVector& Cell = Pair.Key;
		// 使用 int64 比较外扩边界，避免位于 int32 边缘的棋盘坐标在加减时回绕。
		if (static_cast<int64>(Cell.X) < static_cast<int64>(Min.X) - Expansion.X || static_cast<int64>(Cell.X) > static_cast<int64>(Max.X) + Expansion.X
			|| static_cast<int64>(Cell.Y) < static_cast<int64>(Min.Y) - Expansion.Y || static_cast<int64>(Cell.Y) > static_cast<int64>(Max.Y) + Expansion.Y
			|| static_cast<int64>(Cell.Z) < static_cast<int64>(Min.Z) - Expansion.Z || static_cast<int64>(Cell.Z) > static_cast<int64>(Max.Z) + Expansion.Z) { continue; }
		if (!IsValid(Pair.Value))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 外扩查询遇到失效建筑索引，源=%s，格=%s。"), *GetPathName(), *Source->GetPathName(), *Cell.ToString());
			return false;
		}
		if (Pair.Value->CanReceiveBuildingTrigger()) { Unique.Add(Pair.Value); }
	}
	OutBuildings = Unique.Array();
	return true;
}

bool ACastleBoard::GetConnectedBuildings(const ACastleBuildingBase* Source, const TArray<ACastleBuildingBase*>& Candidates,
	TArray<ACastleBuildingBase*>& OutBuildings) const
{
	OutBuildings.Reset();
	if (!ValidateEffectQuerySource(Source)) { return false; }
	TSet<ACastleBuildingBase*> Allowed;
	for (ACastleBuildingBase* Candidate : Candidates)
	{
		if (!IsValid(Candidate))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 连通查询失败：源 %s 的候选集合包含失效建筑。"), *GetPathName(), *Source->GetPathName());
			return false;
		}
		// D6 只允许已完成建造的实例参与连通，不能借建造中的建筑跨过空隙。
		if (!Candidate->CanReceiveBuildingTrigger()) { continue; }
		if (!ValidateEffectQuerySource(Candidate)) { return false; }
		Allowed.Add(Candidate);
	}
	if (!Allowed.Contains(const_cast<ACastleBuildingBase*>(Source)))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 连通查询失败：候选集合没有源建筑 %s。"), *GetPathName(), *Source->GetPathName());
		return false;
	}
	TSet<ACastleBuildingBase*> Visited;
	OutBuildings.Add(const_cast<ACastleBuildingBase*>(Source));
	Visited.Add(const_cast<ACastleBuildingBase*>(Source));
	for (int32 Head = 0; Head < OutBuildings.Num(); ++Head)
	{
		for (const FIntVector& Cell : OutBuildings[Head]->GetOccupiedCoordinates())
		{
			TArray<FIntVector> Neighbors;
			GetFaceNeighborCoordinates(Cell, Neighbors);
			for (const FIntVector& Neighbor : Neighbors)
			{
				ACastleBuildingBase* Building = GetBuildingAt(Neighbor);
				if (Allowed.Contains(Building) && !Visited.Contains(Building))
				{
					Visited.Add(Building);
					OutBuildings.Add(Building);
				}
			}
		}
	}
	return true;
}

bool ACastleBoard::GetFaceAdjacentBuildings(const ACastleBuildingBase* Source, const bool bRequirePassage,
	TArray<ACastleBuildingBase*>& OutBuildings) const
{
	OutBuildings.Reset();
	if (!ValidateEffectQuerySource(Source)) { return false; }
	TMap<ACastleBuildingBase*, FIntVector> NeighborCells;
	auto CellLess = [](const FIntVector& A, const FIntVector& B)
	{
		return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z);
	};
	for (const FIntVector& Cell : Source->GetOccupiedCoordinates())
	{
		TArray<FIntVector> Neighbors;
		GetFaceNeighborCoordinates(Cell, Neighbors);
		for (const FIntVector& Neighbor : Neighbors)
		{
			const auto* Entry = BuildingByCoordinate.Find(Neighbor);
			if (!Entry || Entry->Get() == Source) { continue; }
			ACastleBuildingBase* Building = Entry->Get();
			if (!IsValid(Building))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s：建筑 %s 邻格 %s 的占用索引失效。"),
					*GetPathName(), *Source->GetPathName(), *Neighbor.ToString());
				return false;
			}
			// 未落成不会参与联动。通行查询只接纳同色贯通或同号门/梯；炮击邻接不要求通行。
			if (!Building->CanReceiveBuildingTrigger() || (bRequirePassage && Building->GetBuildingColor() != Source->GetBuildingColor()
				&& Building->GetBuildingNumber() != Source->GetBuildingNumber())) { continue; }
			const FIntVector* Previous = NeighborCells.Find(Building);
			if (!Previous || CellLess(Neighbor, *Previous)) { NeighborCells.Add(Building, Neighbor); }
		}
	}
	NeighborCells.GenerateKeyArray(OutBuildings);
	OutBuildings.Sort([&](const ACastleBuildingBase& A, const ACastleBuildingBase& B)
	{
		return CellLess(NeighborCells.FindChecked(const_cast<ACastleBuildingBase*>(&A)), NeighborCells.FindChecked(const_cast<ACastleBuildingBase*>(&B)));
	});
	return true;
}

FVector ACastleBoard::GetGridWorldLocation(const FIntVector& Coordinate) const
{
	const FIntVector RelativeCoordinate = Coordinate - GridOrigin;
	const FVector LocalGridLocation(
		static_cast<float>(RelativeCoordinate.X) * CellSize,
		static_cast<float>(RelativeCoordinate.Y) * CellSize,
		WorldBaseHeight + static_cast<float>(RelativeCoordinate.Z) * LayerHeight);

	// 使用棋盘 Actor 的完整变换，保证棋盘被旋转或缩放时逻辑坐标与占位表现仍一致。
	return GetActorTransform().TransformPosition(LocalGridLocation);
}

bool ACastleBoard::TryResolveGridCursorTarget(const FVector& RayStart, const FVector& RayEnd, FCastleGridCursorTarget& OutTarget) const
{
	OutTarget = FCastleGridCursorTarget();

	if (GetWorld() == nullptr || RayStart.Equals(RayEnd))
	{
		return false;
	}

	// 使用首个真实遮挡命中；不能穿过建筑去选它后面的塔侧格。
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CastleGridCursor), true);
	FHitResult HitResult;
	if (!GetWorld()->LineTraceSingleByChannel(HitResult, RayStart, RayEnd, ECC_Visibility, QueryParams))
	{
		return false;
	}

	if (PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides && HitResult.GetActor() == MainTowerActor)
	{
		// 主塔装饰和其它模型碰撞不是侧格；侧格组件自行验证唯一身份和正面法线。
		return IsValid(TowerSideGrid) && TowerSideGrid->TryResolveHit(HitResult, OutTarget);
	}

	FIntVector HitCell = FIntVector::ZeroValue;
	bool bFoundCell = false;
	bool bHitGroundTile = false;

	if (const ACastleGroundTileActor* HitGroundTile = Cast<ACastleGroundTileActor>(HitResult.GetActor()))
	{
		const TObjectPtr<ACastleGroundTileActor>* RegisteredTile = GroundTiles.Find(HitGroundTile->GetGroundCell().Coordinate);
		if (PlacementSurfaceMode == ECastlePlacementSurfaceMode::GroundBoard
			&& RegisteredTile != nullptr && RegisteredTile->Get() == HitGroundTile
			&& HitResult.GetComponent() == HitGroundTile->GetTileMeshComponent())
		{
			HitCell = HitGroundTile->GetGroundCell().Coordinate;
			bFoundCell = true;
			bHitGroundTile = true;
		}
	}

	// 建筑网格可以覆盖多个坐标，取距离命中点最近的占用格作为当前面所属格子。
	if (!bFoundCell)
	{
		// 模型组件现在由建筑自身直接拥有，射线命中的 Actor 就是棋盘里的建筑。
		if (const ACastleBuildingBase* HitBuilding = Cast<ACastleBuildingBase>(HitResult.GetActor()))
		{
			float BestDistanceSquared = TNumericLimits<float>::Max();
			for (const FIntVector& OccupiedCoordinate : HitBuilding->GetOccupiedCoordinates())
			{
				if (GetBuildingAt(OccupiedCoordinate) != HitBuilding) { continue; }
				const float DistanceSquared = FVector::DistSquared(HitResult.ImpactPoint, GetGridWorldLocation(OccupiedCoordinate));
				if (DistanceSquared < BestDistanceSquared)
				{
					BestDistanceSquared = DistanceSquared;
					HitCell = OccupiedCoordinate;
					bFoundCell = true;
				}
			}
		}
	}

	if (!bFoundCell)
	{
		return false;
	}

	const FTransform BoardTransform = GetActorTransform();
	const FVector LocalImpactPoint = BoardTransform.InverseTransformPosition(HitResult.ImpactPoint);
	const FVector LocalNormal = BoardTransform.InverseTransformVectorNoScale(HitResult.ImpactNormal).GetSafeNormal();
	if (LocalNormal.IsNearlyZero())
	{
		return false;
	}
	// 地面只提供建筑的支撑顶面；薄地块侧面和底面不能作为悬空建造目标。
	if (bHitGroundTile && GetGridFaceFromNormal(LocalNormal) != ECastleGridFace::Up)
	{
		return false;
	}

	BuildGridCursorTarget(HitCell, LocalImpactPoint, LocalNormal, OutTarget);
	OutTarget.Surface = bHitGroundTile ? ECastleGridCursorSurface::Ground : ECastleGridCursorSurface::Building;
	return OutTarget.bValid;
}

void ACastleBoard::BuildGridCursorTarget(const FIntVector& Cell, const FVector& LocalImpactPoint, const FVector& LocalNormal, FCastleGridCursorTarget& OutTarget) const
{
	if (!IsCoordinateWithinBoard(Cell) && !IsValid(GetBuildingAt(Cell)))
	{
		return;
	}

	const FIntVector RelativeCoordinate = Cell - GridOrigin;
	const FVector LocalCellCenter(
		static_cast<float>(RelativeCoordinate.X) * CellSize,
		static_cast<float>(RelativeCoordinate.Y) * CellSize,
		WorldBaseHeight + static_cast<float>(RelativeCoordinate.Z) * LayerHeight);

	const ECastleGridFace Face = GetGridFaceFromNormal(LocalNormal);
	FVector LocalPlaneCenter = LocalCellCenter;
	FVector LocalPlaneScale(
		GroundTileSize / 100.0f,
		GroundTileSize / 100.0f,
		2.0f / 100.0f);

	// 指示器使用薄立方体模拟平面；根据命中面切换薄轴，并把其他轴吸附到当前格中心。
	switch (Face)
	{
	case ECastleGridFace::Up:
	case ECastleGridFace::Down:
		LocalPlaneCenter.Z = LocalImpactPoint.Z;
		break;
	case ECastleGridFace::PositiveX:
	case ECastleGridFace::NegativeX:
		LocalPlaneCenter.X = LocalImpactPoint.X;
		LocalPlaneScale = FVector(2.0f / 100.0f, GroundTileSize / 100.0f, LayerHeight * 0.94f / 100.0f);
		break;
	case ECastleGridFace::PositiveY:
	case ECastleGridFace::NegativeY:
		LocalPlaneCenter.Y = LocalImpactPoint.Y;
		LocalPlaneScale = FVector(GroundTileSize / 100.0f, 2.0f / 100.0f, LayerHeight * 0.94f / 100.0f);
		break;
	default:
		return;
	}

	// 轻微离开实际表面，避免薄片与地面或建筑产生深度冲突。
	LocalPlaneCenter += LocalNormal * 1.5f;

	const FTransform BoardTransform = GetActorTransform();
	OutTarget.Cell = Cell;
	OutTarget.Face = Face;
	OutTarget.AdjacentCell = Cell + GetGridFaceOffset(Face);
	OutTarget.WorldNormal = BoardTransform.TransformVectorNoScale(LocalNormal).GetSafeNormal();
	OutTarget.IndicatorTransform = FTransform(
		BoardTransform.GetRotation(),
		BoardTransform.TransformPosition(LocalPlaneCenter),
		LocalPlaneScale * BoardTransform.GetScale3D());
	OutTarget.bValid = true;
}

TArray<FIntVector> ACastleBoard::BuildPlacementFootprint(const FIntVector& Coordinate, int32 Level)
{
	// 卡牌直接放置仍采用标准朝向：二级沿 X 两格，三级 XY 平面四格，四级两层八格。
	// 自动合成使用两座建筑的实际联合占用，不能在升级时调用这里而丢掉垂直形状。
	const int32 Width = Level >= 2 && Level <= 4 ? 2 : 1;
	const int32 Depth = Level >= 3 && Level <= 4 ? 2 : 1;
	const int32 Height = Level == 4 ? 2 : 1;
	TArray<FIntVector> Result;
	for (int32 X = 0; X < Width; ++X)
	{
		for (int32 Y = 0; Y < Depth; ++Y)
		{
			for (int32 Z = 0; Z < Height; ++Z)
			{
				Result.Add(Coordinate + FIntVector(X, Y, Z));
			}
		}
	}
	return Result;
}

void ACastleBoard::GetFaceNeighborCoordinates(const FIntVector& Coordinate, TArray<FIntVector>& OutNeighbors) const
{
	OutNeighbors.Reset(6);
	OutNeighbors.Add(Coordinate + FIntVector(1, 0, 0));
	OutNeighbors.Add(Coordinate + FIntVector(-1, 0, 0));
	OutNeighbors.Add(Coordinate + FIntVector(0, 1, 0));
	OutNeighbors.Add(Coordinate + FIntVector(0, -1, 0));
	OutNeighbors.Add(Coordinate + FIntVector(0, 0, 1));
	OutNeighbors.Add(Coordinate + FIntVector(0, 0, -1));
}

ACastleBuildingBase* ACastleBoard::GetBuildingAt(const FIntVector& Coordinate) const
{
	if (const TObjectPtr<ACastleBuildingBase>* FoundBuilding = BuildingByCoordinate.Find(Coordinate))
	{
		return FoundBuilding->Get();
	}

	return nullptr;
}

bool ACastleBoard::RegisterBuilding(ACastleBuildingBase* Building, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (Building == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("不能注册空建筑。"));
		return false;
	}

	const TArray<FIntVector> OccupiedCoordinates = Building->GetOccupiedCoordinates();
	if (OccupiedCoordinates.Num() == 0)
	{
		OutFailureReason = FText::FromString(TEXT("建筑没有占用坐标，不能写入棋盘索引。"));
		return false;
	}

	TSet<FIntVector> UniqueCoordinates;
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		// 登记与放置使用同一允许范围；合成与回收再放置也不能退回旧棋盘边界。
		FIntPoint Min, Max;
		GetBoardExtent(Min, Max);
		if (Coordinate.X < Min.X-MaxOverhangCells || Coordinate.X > Max.X+MaxOverhangCells
			|| Coordinate.Y < Min.Y-MaxOverhangCells || Coordinate.Y > Max.Y+MaxOverhangCells
			|| Coordinate.Z < GridOrigin.Z || Coordinate.Z >= GridOrigin.Z+GetUnlockedLayerCount())
		{
			OutFailureReason = FText::FromString(TEXT("建筑占用坐标超出允许的棋盘越界范围或层高。"));
			return false;
		}

		// 合成和内部注册也走同样禁区，避免绕过手牌预判写进塔身。
		if (IsMainTowerCellReserved(Coordinate))
		{
			OutFailureReason = FText::FromString(TEXT("建筑占用坐标与主塔塔身重叠。"));
			return false;
		}
		if (UniqueCoordinates.Contains(Coordinate))
		{
			OutFailureReason = FText::FromString(TEXT("建筑占用坐标存在重复值。"));
			return false;
		}
		UniqueCoordinates.Add(Coordinate);

		if (GetBuildingAt(Coordinate) != nullptr)
		{
			OutFailureReason = FText::FromString(TEXT("建筑占用坐标与现有建筑重叠。"));
			return false;
		}
	}

	if (auto* Triggers = GetWorld()->GetSubsystem<UCastleBuildingTriggerSubsystem>())
	{
		// 占用规则是触发区域的唯一来源，使用棋盘局部坐标，避免美术屋檐、模型缩放改变范围。
		TArray<FBox> Boxes;
		const FVector Extent(CellSize * 0.5, CellSize * 0.5, LayerHeight * 0.5);
		for (const FIntVector& Coordinate : OccupiedCoordinates)
		{
			const FIntVector Relative = Coordinate - GridOrigin;
			const FVector Center(Relative.X * CellSize, Relative.Y * CellSize, WorldBaseHeight + Relative.Z * LayerHeight);
			Boxes.Emplace(Center - Extent, Center + Extent);
		}
		if (!Triggers->RegisterBuilding(Building, GetActorTransform(), MoveTemp(Boxes), OutFailureReason)) { return false; }
	}
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		BuildingByCoordinate.Add(Coordinate, Building);
	}
	Building->OnDestroyed.AddUniqueDynamic(this, &ACastleBoard::HandleBuildingDestroyed);
	Building->BuildingEffect->OnBoardRegistered(*this);
	return true;
}

bool ACastleBoard::UnregisterBuilding(ACastleBuildingBase* Building, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (Building == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("不能注销空建筑。"));
		return false;
	}

	const TArray<FIntVector> OccupiedCoordinates = Building->GetOccupiedCoordinates();
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		if (GetBuildingAt(Coordinate) != Building)
		{
			OutFailureReason = FText::FromString(TEXT("建筑坐标索引不完整，拒绝注销以避免破坏棋盘状态。"));
			return false;
		}
	}

	Building->BuildingEffect->OnBoardUnregistered(*this);
	if (auto* Triggers = GetWorld()->GetSubsystem<UCastleBuildingTriggerSubsystem>()) { Triggers->UnregisterBuilding(Building); }
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		BuildingByCoordinate.Remove(Coordinate);
	}
	return true;
}

ACastleBuildingBase* ACastleBoard::SpawnBuildingActor(const FCastleBuildingDescriptor& Descriptor, const TArray<FIntVector>& OccupiedCoordinates, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (!Descriptor.Validate(OutFailureReason))
	{
		return nullptr;
	}

	if (OccupiedCoordinates.Num() != ACastleBuildingBase::GetExpectedFootprintCellCount(Descriptor.Level))
	{
		OutFailureReason = FText::FromString(TEXT("生成建筑 Actor 时提供的占用格数量与等级不一致。"));
		return nullptr;
	}

	// 建筑以整个占用范围的底面中心为原点；多层建筑仍以最低格为基准，不能取各层高度的平均值。
	FIntVector Minimum = OccupiedCoordinates[0];
	FIntVector Maximum = OccupiedCoordinates[0];
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		Minimum = Minimum.ComponentMin(Coordinate);
		Maximum = Maximum.ComponentMax(Coordinate);
	}
	const FIntVector Dimensions = Maximum - Minimum + FIntVector(1, 1, 1);
	const TSubclassOf<ACastleBuildingBase> BuildingClass = Descriptor.Definition->ResolveBuildingClassForDimensions(Descriptor.Level, Dimensions);
	if (BuildingClass == nullptr)
	{
		OutFailureReason = FText::FromString(FString::Printf(
			TEXT("建筑定义 %s 没有提供等级 %d 对应占格尺寸 (%d, %d, %d) 的生成类。"),
			*Descriptor.Definition->DisplayName.ToString(), Descriptor.Level, Dimensions.X, Dimensions.Y, Dimensions.Z));
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘生成建筑失败：%s"), *OutFailureReason.ToString());
		return nullptr;
	}
	// X/Y 位于占格范围中点，Z 从最低格中心下降半个层高；偏移跟随棋盘的旋转与缩放。
	const FVector LocalBottomCenterOffset(
		(Maximum.X - Minimum.X) * CellSize * 0.5f,
		(Maximum.Y - Minimum.Y) * CellSize * 0.5f,
		-LayerHeight * 0.5f);
	const FVector SpawnLocation = GetGridWorldLocation(Minimum)
		+ GetActorTransform().TransformVector(LocalBottomCenterOffset);

	if (GetWorld() == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("棋盘不在有效 World 中，无法生成建筑 Actor。"));
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 生成等级蓝图自身，并显式保留它的根缩放；棋盘的缩放在此基础上叠乘。
	SpawnParameters.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;
	FQuat SpawnRotation = GetActorQuat();
	if ((Descriptor.Level == 2 && Dimensions == FIntVector(1, 2, 1))
		|| (Descriptor.Level == 3 && Dimensions == FIntVector(1, 2, 2)))
	{
		// 标准二级蓝图沿 X 轴制作，沿 Y 合成时旋转整个建筑，不再依赖模型载体节点。
		// 垂直两格的 X/Y 都相同，不能仅凭 X 相同误判为沿 Y；垂直合成保留模型直立朝向。
		SpawnRotation *= FQuat(FVector::UpVector, HALF_PI);
	}
	ACastleBuildingBase* NewBuilding = GetWorld()->SpawnActor<ACastleBuildingBase>(BuildingClass,
		FTransform(SpawnRotation, SpawnLocation, GetActorScale3D()), SpawnParameters);
	if (NewBuilding == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("World 生成建筑 Actor 失败。"));
		return nullptr;
	}

	if (!NewBuilding->InitializeBuilding(Descriptor, OccupiedCoordinates))
	{
		NewBuilding->Destroy();
		OutFailureReason = FText::FromString(TEXT("建筑 Actor 初始化数据失败。"));
		return nullptr;
	}

	// 模型、占位和标签都由同一资产生成；失败时不提交棋盘占用或消耗卡牌。
	if (!NewBuilding->ConfigureBuildingVisual(CellSize, LayerHeight, OutFailureReason))
	{
		NewBuilding->Destroy();
		return nullptr;
	}

	return NewBuilding;
}

ACastleBuildingBase* ACastleBoard::TryMergePlacedBuilding(ACastleBuildingBase* Building, const FVector& ChangeOrigin, FText& OutFailureReason)
{
	if (Building == nullptr || !Building->bHasCompletedPlacement)
	{
		return nullptr;
	}

	// 连续升级的中间 Actor 不触发生成，最后一次重组的结束信号统一释放导航。
	const FScopedCastleNavigationChange NavigationChange(GetWorld(), this);
	ACastleBuildingBase* CurrentBuilding = Building;
	// 一次放置可能连续升级。先完成全部逻辑替换，再用真正参与的原建筑播放一次重组。
	// 原建筑暂时保留到抓取完成；中间等级只更新索引，不会闪现或反复播多轮动画。
	TArray<AActor*> MergeSources = { Building };
	bool bHasMerged = false;
	while (CurrentBuilding != nullptr && CurrentBuilding->CanMergeToNextLevel())
	{
		ACastleBuildingBase* MergePartner = FindMergePartner(CurrentBuilding);
		if (MergePartner == nullptr)
		{
			break;
		}

		TArray<FIntVector> MergedFootprint;
		if (!BuildMergedFootprint(CurrentBuilding, MergePartner, MergedFootprint))
		{
			// FindMergePartner 已经做过同样检查，走到这里说明索引状态被外部破坏。
			OutFailureReason = FText::FromString(TEXT("合成占用形状校验失败，棋盘索引可能已被外部修改。"));
			break;
		}

		FText MergeReason;
		ACastleBuildingBase* MergedBuilding = MergeBuildings(CurrentBuilding, MergePartner, MergedFootprint, MergeReason);
		if (MergedBuilding == nullptr)
		{
			OutFailureReason = MergeReason;
			// 合成替换失败时保留合法的原有建筑，避免静默丢失场上对象。
			break;
		}

		MergeSources.Add(MergePartner);
		if (bHasMerged)
		{
			// 上一轮生成的中间等级并非真实输入零件，不加入表现快照。
			CurrentBuilding->Destroy();
		}
		CurrentBuilding = MergedBuilding;
		bHasMerged = true;
	}

	if (bHasMerged)
	{
		UBuildingMergeComponent* Transition = CurrentBuilding->BuildingMerge;
		Transition->PartCatalog = CurrentBuilding->GetBuildingDefinition()->PartCatalog;
		// 双方已经完整飞入，合并只表现从现有位置到新位置的局部调整，不再让新建筑飞第二遍。
		Transition->IncomingSourceActor = nullptr;
		Transition->bUseSharedChangeOrigin = true;
		Transition->ChangeOrigin = ChangeOrigin;
		// 放置飞入已完成；合并阶段沿新旧格子方向做局部变化，不再重复播放场外飞入。
		FVector PreviousCenter = FVector::ZeroVector;
		for (int32 Index = 1; Index < MergeSources.Num(); ++Index)
		{
			PreviousCenter += MergeSources[Index]->GetActorLocation();
		}
		PreviousCenter /= MergeSources.Num() - 1;
		Transition->ArrivalDirection = Building->GetActorLocation() - PreviousCenter;
		if (GetWorld()->IsGameWorld() && !CurrentBuilding->BuildingMerge->StartMerge(MergeSources))
		{
			// 规则替换已经完成，表现错误明确传回并由组件输出 Error，不能静默改播其他效果。
			OutFailureReason = FText::FromString(CurrentBuilding->BuildingMerge->LastError);
		}
		for (AActor* Source : MergeSources)
		{
			Source->Destroy();
		}
	}
	return CurrentBuilding;
}

ACastleBuildingBase* ACastleBoard::FindMergePartner(ACastleBuildingBase* Building) const
{
	if (Building == nullptr || !Building->bHasCompletedPlacement || !Building->CanMergeToNextLevel())
	{
		return nullptr;
	}

	TSet<ACastleBuildingBase*> Candidates;
	const TArray<FIntVector> OccupiedCoordinates = Building->GetOccupiedCoordinates();
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		TArray<FIntVector> Neighbors;
		GetFaceNeighborCoordinates(Coordinate, Neighbors);
		for (const FIntVector& NeighborCoordinate : Neighbors)
		{
			ACastleBuildingBase* Candidate = GetBuildingAt(NeighborCoordinate);
			if (Candidate != nullptr && Candidate != Building)
			{
				Candidates.Add(Candidate);
			}
		}
	}

	for (ACastleBuildingBase* Candidate : Candidates)
	{
		TArray<FIntVector> MergedFootprint;
		if (Candidate->bHasCompletedPlacement && Building->CanMergeWith(Candidate) && BuildMergedFootprint(Building, Candidate, MergedFootprint))
		{
			return Candidate;
		}
	}

	return nullptr;
}

bool ACastleBoard::BuildMergedFootprint(const ACastleBuildingBase* First, const ACastleBuildingBase* Second, TArray<FIntVector>& OutFootprint) const
{
	OutFootprint.Reset();

	if (First == nullptr || Second == nullptr || !First->CanMergeWith(Second))
	{
		return false;
	}

	const int32 CurrentLevel = First->GetBuildingLevel();
	if (CurrentLevel < 1 || CurrentLevel > 3)
	{
		return false;
	}

	const int32 ExpectedSingleCount = ACastleBuildingBase::GetExpectedFootprintCellCount(CurrentLevel);
	const TArray<FIntVector> FirstCoordinates = First->GetOccupiedCoordinates();
	const TArray<FIntVector> SecondCoordinates = Second->GetOccupiedCoordinates();
	if (FirstCoordinates.Num() != ExpectedSingleCount || SecondCoordinates.Num() != ExpectedSingleCount)
	{
		return false;
	}

	TSet<FIntVector> UniqueCoordinates;
	for (const FIntVector& Coordinate : FirstCoordinates)
	{
		UniqueCoordinates.Add(Coordinate);
	}
	for (const FIntVector& Coordinate : SecondCoordinates)
	{
		if (UniqueCoordinates.Contains(Coordinate))
		{
			return false;
		}
		UniqueCoordinates.Add(Coordinate);
	}

	const int32 ExpectedMergedCount = ExpectedSingleCount * 2;
	if (UniqueCoordinates.Num() != ExpectedMergedCount)
	{
		return false;
	}

	FIntVector Minimum = *UniqueCoordinates.CreateConstIterator();
	FIntVector Maximum = Minimum;
	for (const FIntVector& Coordinate : UniqueCoordinates)
	{
		Minimum.X = FMath::Min(Minimum.X, Coordinate.X);
		Minimum.Y = FMath::Min(Minimum.Y, Coordinate.Y);
		Minimum.Z = FMath::Min(Minimum.Z, Coordinate.Z);
		Maximum.X = FMath::Max(Maximum.X, Coordinate.X);
		Maximum.Y = FMath::Max(Maximum.Y, Coordinate.Y);
		Maximum.Z = FMath::Max(Maximum.Z, Coordinate.Z);
	}

	const FIntVector Dimensions = Maximum - Minimum + FIntVector(1, 1, 1);
	bool bValidShape = false;
	switch (CurrentLevel)
	{
	case 1:
		// 二级允许沿 X、Y 或 Z 紧贴的两格；三个轴只允许其中一个跨度为 2，斜接不能升级。
		bValidShape = Dimensions == FIntVector(2, 1, 1)
			|| Dimensions == FIntVector(1, 2, 1)
			|| Dimensions == FIntVector(1, 1, 2);
		break;
	case 2:
		// 三级必须填满一个 2x2 方形，既支持水平 XY，也支持垂直 XZ/YZ；四格直线仍非法。
		// 只校验联合形状，不限制两座二级自身的方向，水平对和垂直对都能拼成同一个方形。
		bValidShape = Dimensions == FIntVector(2, 2, 1)
			|| Dimensions == FIntVector(2, 1, 2)
			|| Dimensions == FIntVector(1, 2, 2);
		break;
	case 3:
		// 三级合成四级固定为 2x2x2 立方体。
		bValidShape = Dimensions == FIntVector(2, 2, 2);
		break;
	default:
		break;
	}

	if (!bValidShape)
	{
		return false;
	}

	for (int32 X = Minimum.X; X <= Maximum.X; ++X)
	{
		for (int32 Y = Minimum.Y; Y <= Maximum.Y; ++Y)
		{
			for (int32 Z = Minimum.Z; Z <= Maximum.Z; ++Z)
			{
				if (!UniqueCoordinates.Contains(FIntVector(X, Y, Z)))
				{
					return false;
				}
			}
		}
	}

	OutFootprint = UniqueCoordinates.Array();
	OutFootprint.Sort([](const FIntVector& Left, const FIntVector& Right)
	{
		if (Left.X != Right.X)
		{
			return Left.X < Right.X;
		}
		if (Left.Y != Right.Y)
		{
			return Left.Y < Right.Y;
		}
		return Left.Z < Right.Z;
	});
	return true;
}

ACastleBuildingBase* ACastleBoard::MergeBuildings(ACastleBuildingBase* First, ACastleBuildingBase* Second, const TArray<FIntVector>& MergedFootprint, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	TArray<FIntVector> VerifiedFootprint;
	if (First == nullptr || Second == nullptr || !BuildMergedFootprint(First, Second, VerifiedFootprint) || VerifiedFootprint != MergedFootprint)
	{
		OutFailureReason = FText::FromString(TEXT("合成替换收到无效的建筑或占用形状。"));
		return nullptr;
	}

	const int32 NextLevel = First->GetBuildingLevel() + 1;
	FIntVector MergedMinimum = MergedFootprint[0];
	FIntVector MergedMaximum = MergedFootprint[0];
	for (const FIntVector& Coordinate : MergedFootprint)
	{
		MergedMinimum = MergedMinimum.ComponentMin(Coordinate);
		MergedMaximum = MergedMaximum.ComponentMax(Coordinate);
	}
	const FIntVector MergedDimensions = MergedMaximum - MergedMinimum + FIntVector(1, 1, 1);
	const TSubclassOf<ACastleBuildingBase> NextBuildingClass = First->GetBuildingDefinition()
		? First->GetBuildingDefinition()->ResolveBuildingClassForDimensions(NextLevel, MergedDimensions)
		: nullptr;
	if (NextBuildingClass == nullptr)
	{
		OutFailureReason = FText::FromString(FString::Printf(
			TEXT("当前建筑族没有提供等级 %d 对应占格尺寸 (%d, %d, %d) 的具体 Actor 类。"),
			NextLevel, MergedDimensions.X, MergedDimensions.Y, MergedDimensions.Z));
		return nullptr;
	}

	for (const FIntVector& Coordinate : First->GetOccupiedCoordinates())
	{
		if (GetBuildingAt(Coordinate) != First)
		{
			OutFailureReason = FText::FromString(TEXT("合成前第一个建筑的坐标索引不完整。"));
			return nullptr;
		}
	}
	for (const FIntVector& Coordinate : Second->GetOccupiedCoordinates())
	{
		if (GetBuildingAt(Coordinate) != Second)
		{
			OutFailureReason = FText::FromString(TEXT("合成前第二个建筑的坐标索引不完整。"));
			return nullptr;
		}
	}

	FCastleBuildingDescriptor MergedDescriptor;
	MergedDescriptor.Definition = First->GetBuildingDefinition();
	MergedDescriptor.Color = First->GetBuildingColor();
	MergedDescriptor.Number = First->GetBuildingNumber();
	MergedDescriptor.Level = NextLevel;

	// 先准备本次功能塔升级的整批赠牌。候选有任何错误时不替换原塔，也不写背包。
	TArray<FCastleBuildingDescriptor> RewardCards;
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	const int32 RewardCount = Badges ? Badges->GetUpgradeBonusRoomCards() : 0;
	if (RewardCount > 0 && First->GetBuildingType() != ECastleBuildingType::NormalRoom
		&& First->GetBuildingType() != ECastleBuildingType::SpeedRoom)
	{
		const UCastleDirectorSubsystem* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>();
		if (!Director || !Director->IsRunning() || !IsValid(PrototypeBackpack) || !ValidateBuildingDefinitions(OutFailureReason))
		{
			if (OutFailureReason.IsEmpty()) { OutFailureReason = INVTEXT("徽章赠牌需要运行中的导演和有效背包。"); }
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 合成资产 %s 的徽章赠牌准备失败：%s"),
				*GetPathName(), *GetPathNameSafe(MergedDescriptor.Definition), *OutFailureReason.ToString());
			return nullptr;
		}
		for (int32 Index = 0; Index < RewardCount; ++Index)
		{
			const UCastleBuildingDefinition* Definition = BuildingDefinitions[FMath::RandRange(0, BuildingDefinitions.Num() - 1)];
			const auto Color = static_cast<ECastleTileColor>(FMath::RandRange(
				static_cast<int32>(ECastleTileColor::Red), static_cast<int32>(ECastleTileColor::Purple)));
			if (!Definition->CreateDescriptor(1, Color, FMath::RandRange(1, Director->GetCurrentMaxShopNumber()),
				RewardCards.AddDefaulted_GetRef(), OutFailureReason))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("合成赠牌资产 %s 无法创建一级卡牌：%s"), *Definition->GetPathName(), *OutFailureReason.ToString());
				return nullptr;
			}
		}
	}

	// 先生成新 Actor，成功后才注销和销毁旧 Actor，避免生成失败时丢失原建筑。
	ACastleBuildingBase* MergedBuilding = SpawnBuildingActor(MergedDescriptor, MergedFootprint, OutFailureReason);
	if (MergedBuilding == nullptr)
	{
		return nullptr;
	}

	FText UnregisterReason;
	if (!UnregisterBuilding(First, UnregisterReason))
	{
		MergedBuilding->Destroy();
		OutFailureReason = UnregisterReason;
		return nullptr;
	}
	if (!UnregisterBuilding(Second, UnregisterReason))
	{
		// 第一个建筑已经注销时恢复它的索引；旧 Actor 仍然存活，因此可以安全恢复。
		FText RestoreReason;
		RegisterBuilding(First, RestoreReason);
		MergedBuilding->Destroy();
		OutFailureReason = UnregisterReason;
		return nullptr;
	}

	FText RegisterReason;
	if (!RegisterBuilding(MergedBuilding, RegisterReason))
	{
		FText RestoreFirstReason;
		FText RestoreSecondReason;
		RegisterBuilding(First, RestoreFirstReason);
		RegisterBuilding(Second, RestoreSecondReason);
		MergedBuilding->Destroy();
		OutFailureReason = RegisterReason;
		return nullptr;
	}
	if (!RewardCards.IsEmpty() && !PrototypeBackpack->TryAddItemsAllowOverflow(RewardCards, OutFailureReason))
	{
		// 发牌入口一次校验完整批次，失败时库存未变，因此可以恢复双方原有占用。
		FText RestoreReason;
		const bool bRemoved = UnregisterBuilding(MergedBuilding, RestoreReason);
		const bool bFirstRestored = RegisterBuilding(First, RestoreReason);
		const bool bSecondRestored = RegisterBuilding(Second, RestoreReason);
		if (!bRemoved || !bFirstRestored || !bSecondRestored)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 合成赠牌失败后无法恢复原占用：%s"), *GetPathName(), *RestoreReason.ToString());
		}
		MergedBuilding->Destroy();
		return nullptr;
	}
	if (!RewardCards.IsEmpty())
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("功能塔 %s 升级徽章赠送 %d 张一级房间卡。"), *MergedBuilding->GetPathName(), RewardCards.Num());
	}

	// 调用方完成连续合成并抓取所有真实输入零件后统一销毁旧 Actor。
	return MergedBuilding;
}

bool ACastleBoard::ValidateBuildingDescriptor(const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason) const
{
	return Descriptor.Validate(OutFailureReason);
}

bool ACastleBoard::ValidateBuildingDefinitions(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	if (BuildingDefinitions.IsEmpty())
	{
		OutFailureReason = FText::FromString(TEXT("棋盘没有配置可用建筑 Data Asset，无法生成卡牌或商店商品。"));
		return false;
	}
	TSet<const UCastleBuildingDefinition*> SeenDefinitions;
	for (const UCastleBuildingDefinition* Definition : BuildingDefinitions)
	{
		if (!IsValid(Definition) || SeenDefinitions.Contains(Definition))
		{
			OutFailureReason = FText::FromString(TEXT("棋盘建筑定义池包含空引用或重复资产。"));
			return false;
		}
		if (!Definition->ValidateConfiguration(OutFailureReason))
		{
			return false;
		}
		SeenDefinitions.Add(Definition);
	}
	return true;
}

UCastleBuildingDefinition* ACastleBoard::FindBuildingDefinition(ECastleBuildingType BuildingType) const
{
	for (UCastleBuildingDefinition* Definition : BuildingDefinitions)
	{
		if (IsValid(Definition) && Definition->BuildingType == BuildingType)
		{
			return Definition;
		}
	}
	return nullptr;
}

bool ACastleBoard::TryGetGroundCellAtPlane(const FIntVector& Coordinate, FCastleGroundCell& OutCell) const
{
	if (Coordinate.Z != GridOrigin.Z)
	{
		return false;
	}

	return TryGetGroundCell(Coordinate, OutCell);
}

bool ACastleBoard::ValidateBoardDimensions(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	if (GridSize.X < 1 || GridSize.Y < 1 || GridSize.Z < 1 || MaxBoardSize < GridSize.X
		|| MaxBoardSize < GridSize.Y || MaxBoardSize > 64 || MaxOverhangCells < 0 || MaxOverhangCells > 8)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("非法棋盘配置：GridSize=(%d,%d,%d)，MaxBoardSize=%d（须不小于起始 XY 且不大于 64），MaxOverhangCells=%d（须为 0～8）。"),
			GridSize.X, GridSize.Y, GridSize.Z, MaxBoardSize, MaxOverhangCells));
		return false;
	}
	const bool bValidGroundTileSize = FMath::IsFinite(GroundTileSize) && GroundTileSize > 1.0f && GroundTileSize <= 10000.0f;
	const bool bValidGroundTileGap = FMath::IsFinite(GroundTileGap) && GroundTileGap >= 0.0f && GroundTileGap <= 1000.0f;
	const bool bValidCellSize = FMath::IsFinite(CellSize) && CellSize > 0.0f;
	const bool bValidLayerHeight = FMath::IsFinite(LayerHeight) && LayerHeight > 1.0f && LayerHeight <= 10000.0f;
	const bool bValidWorldBaseHeight = FMath::IsFinite(WorldBaseHeight) && WorldBaseHeight >= 0.0f && WorldBaseHeight <= 10000.0f;
	if (!bValidGroundTileSize || !bValidGroundTileGap || !bValidCellSize
		|| !bValidLayerHeight || !bValidWorldBaseHeight)
	{
		OutFailureReason = FText::FromString(TEXT("棋盘格实体尺寸、间隙、中心距、层高或基准高度超出合法范围。"));
		return false;
	}
	if (!FMath::IsNearlyEqual(CellSize, GroundTileSize + GroundTileGap, 0.01f))
	{
		OutFailureReason = FText::FromString(FString::Printf(
			TEXT("棋盘中心距必须等于实体格边长加间隙：当前 %.3f != %.3f + %.3f。"),
			CellSize, GroundTileSize, GroundTileGap));
		return false;
	}
	return true;
}

bool ACastleBoard::TryResolvePlacementCoordinate(const FCastleGridCursorTarget& CursorTarget, FIntVector& OutCoordinate, FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	const FIntVector FaceOffset = GetGridFaceOffset(CursorTarget.Face);
	if (!CursorTarget.bValid || FaceOffset == FIntVector::ZeroValue
		|| CursorTarget.AdjacentCell != CursorTarget.Cell + FaceOffset)
	{
		OutFailureReason = FText::FromString(TEXT("放置失败：当前指向的格面无效，请重新对准建造面。"));
		return false;
	}

	if (CursorTarget.Surface == ECastleGridCursorSurface::MainTowerSide)
	{
		if (!FindTowerSideCell(CursorTarget.Cell, CursorTarget.Face) || TowerSideGrid->IsPresentationHidden())
		{
			OutFailureReason = FText::FromString(TEXT("放置失败：指向的主塔侧格不可用，请重新选择格面。"));
			return false;
		}
		OutCoordinate = CursorTarget.AdjacentCell;
		return true;
	}
	if (CursorTarget.Surface == ECastleGridCursorSurface::Ground)
	{
		// 地块的顶面支撑首层建筑，落点仍是地块所在格；地块侧面不允许用于建造。
		FCastleGroundCell GroundCell;
		if (PlacementSurfaceMode != ECastlePlacementSurfaceMode::GroundBoard
			|| !IsCoordinateWithinBoard(CursorTarget.Cell) || CursorTarget.Face != ECastleGridFace::Up || !TryGetGroundCellAtPlane(CursorTarget.Cell, GroundCell)
			|| GetBuildingAt(CursorTarget.Cell) != nullptr)
		{
			OutFailureReason = FText::FromString(TEXT("放置失败：指向的地面顶面已不可用，请重新选择。"));
			return false;
		}
		OutCoordinate = CursorTarget.Cell;
		return true;
	}

	// 建筑的任一面都指向其外侧邻格；建筑消失后不能改用同坐标的地面作为匹配来源。
	if (CursorTarget.Surface != ECastleGridCursorSurface::Building || !IsValid(GetBuildingAt(CursorTarget.Cell)))
	{
		OutFailureReason = FText::FromString(TEXT("放置失败：指向的建筑已不存在，请重新选择格面。"));
		return false;
	}
	OutCoordinate = CursorTarget.AdjacentCell;
	return true;
}

bool ACastleBoard::HasPlacementMatch(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, const FCastleGridCursorTarget* CursorTarget, FText& OutFailureReason) const
{
	// 无视线来源的坐标接口仍只用正下方顶面提供属性匹配，不扫描其他邻居。
	// 这是坐标入口的来源约定，不代表整座建筑需要下方支撑；侧向悬空建造使用光标入口。
	FCastleGridCursorTarget SupportTarget;
	if (CursorTarget == nullptr)
	{
		SupportTarget.bValid = true;
		const bool bUsesGround = PlacementSurfaceMode == ECastlePlacementSurfaceMode::GroundBoard && Coordinate.Z == GridOrigin.Z;
		SupportTarget.Surface = bUsesGround ? ECastleGridCursorSurface::Ground : ECastleGridCursorSurface::Building;
		SupportTarget.Cell = bUsesGround ? Coordinate : Coordinate - FIntVector(0, 0, 1);
		SupportTarget.Face = ECastleGridFace::Up;
		SupportTarget.AdjacentCell = SupportTarget.Cell + FIntVector(0, 0, 1);
		CursorTarget = &SupportTarget;
	}

	// 地面模式保留原有越界约束：不能靠地面匹配让多格卡跨越地块边界。
	if (CursorTarget->Surface == ECastleGridCursorSurface::Ground)
	{
		for (const FIntVector& Cell : BuildPlacementFootprint(Coordinate, Descriptor.Level))
		{
			if (!IsCoordinateWithinBoard(Cell))
			{
				OutFailureReason = FText::FromString(TEXT("棋盘外的占用格必须依附建筑面放置，不能使用地面作为匹配来源。"));
				return false;
			}
		}
	}
	FIntVector ResolvedCoordinate;
	if (!TryResolvePlacementCoordinate(*CursorTarget, ResolvedCoordinate, OutFailureReason)) { return false; }
	if (ResolvedCoordinate != Coordinate)
	{
		OutFailureReason = FText::FromString(TEXT("放置失败：落点与当前高亮面不一致。"));
		return false;
	}

	// 所有占用格共享建筑属性；只读取确切命中面，不能拼接不同面的颜色与数字。
	bool bMatches = false;
	if (CursorTarget->Surface == ECastleGridCursorSurface::MainTowerSide)
	{
		const FCastleTowerSideCell* Cell = FindTowerSideCell(CursorTarget->Cell, CursorTarget->Face);
		// TryResolvePlacementCoordinate 已在本次同步调用中确认该侧格身份和可用状态。
		bMatches = Cell->Color == Descriptor.Color || Cell->Number == Descriptor.Number;
	}
	else if (CursorTarget->Surface == ECastleGridCursorSurface::Ground)
	{
		FCastleGroundCell GroundCell;
		if (!TryGetGroundCellAtPlane(CursorTarget->Cell, GroundCell)) { return false; }
		bMatches = GroundCell.Color == Descriptor.Color || GroundCell.Number == Descriptor.Number;
	}
	else
	{
		const ACastleBuildingBase* Source = GetBuildingAt(CursorTarget->Cell);
		bMatches = Source->GetBuildingColor() == Descriptor.Color || Source->GetBuildingNumber() == Descriptor.Number;
	}
	if (bMatches) { return true; }
	OutFailureReason = FText::FromString(TEXT("放置失败：卡牌必须与当前高亮面的颜色或数字相同。"));
	return false;
}

void ACastleBoard::InitializePrototypeDeck()
{
	PrototypePalette.Reset();

	if (PrototypeBackpack == nullptr)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘原型卡组初始化失败：PrototypeBackpack 为空。"));
		return;
	}

	if (PrototypeBackpack->GetItemCount() > 0)
	{
		// 允许关卡蓝图预先配置背包；检测到已有卡牌时不再偷偷追加样例卡。
		UE_LOG(LogMoving_Castle, Log, TEXT("棋盘检测到预先配置的原型背包，跳过自动样例卡组。"));
		return;
	}

	// 样例牌只从当前模式实际生成的格子取色号；塔侧模式从第一层不同面取三组。
	struct FStarterMatch { ECastleTileColor Color; int32 Number; };
	FStarterMatch Matches[3];
	if (PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides)
	{
		FText FailureReason;
		if (!ValidatePlacementSurface(FailureReason) || TowerSideGrid->GetCells().Num() < UE_ARRAY_COUNT(Matches))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 无法按主塔 %s 的侧格准备样例牌：侧格不全。%s"),
				*GetPathName(), *GetPathNameSafe(MainTowerActor.Get()), *FailureReason.ToString());
			return;
		}
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Matches); ++Index)
		{
			const FCastleTowerSideCell& Cell = TowerSideGrid->GetCells()[Index];
			Matches[Index] = {Cell.Color, Cell.Number};
		}
	}
	else
	{
		const TArray<FCastleGroundCell>& FunctionalCells = GeneratedGroundCells;
		if (FunctionalCells.IsEmpty())
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 原型卡组初始化失败：生成结果中没有功能地面格。"), *GetPathName());
			return;
		}
		// 旧地面模式的样例顺序和小棋盘行为保持原样。
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Matches); ++Index)
		{
			const FCastleGroundCell& Cell = FunctionalCells[FMath::Min(Index, FunctionalCells.Num() - 1)];
			Matches[Index] = {Cell.Color, Cell.Number};
		}
	}

	FText DefinitionReason;
	if (!ValidateBuildingDefinitions(DefinitionReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("开局卡组配置错误：%s"), *DefinitionReason.ToString());
		return;
	}
	const ECastleBuildingType Types[] = { ECastleBuildingType::NormalRoom, ECastleBuildingType::ArcherTower,
		ECastleBuildingType::CannonTower, ECastleBuildingType::Farm, ECastleBuildingType::ShieldTower };
	const FStarterMatch Cells[] = { Matches[0], Matches[0], Matches[1], Matches[2], Matches[0] };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Types); ++Index)
	{
		UCastleBuildingDefinition* Definition = FindBuildingDefinition(Types[Index]);
		FCastleBuildingDescriptor Descriptor;
		if (Definition == nullptr || !Definition->CreateDescriptor(1, Cells[Index].Color, Cells[Index].Number, Descriptor, DefinitionReason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("开局卡组缺少有效的 %d 类建筑定义：%s"), static_cast<int32>(Types[Index]), *DefinitionReason.ToString());
			PrototypePalette.Reset();
			return;
		}
		PrototypePalette.Add(Descriptor);
	}

	// 五种样例卡各放两张，盾塔位于第 9、10 槽；同属性盾塔可直接验证一级升二级。
	for (int32 PaletteIndex = 0; PaletteIndex < PrototypePalette.Num(); ++PaletteIndex)
	{
		const int32 CardCount = 2;
		for (int32 CardIndex = 0; CardIndex < CardCount; ++CardIndex)
		{
			int32 AddedIndex = INDEX_NONE;
			FText AddReason;
			if (!PrototypeBackpack->TryAddItem(PrototypePalette[PaletteIndex], AddedIndex, AddReason))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("棋盘原型卡组加入失败：%s"), *AddReason.ToString());
			}
		}
	}

	SelectedPrototypeSlot = 0;
	// 开局卡牌操作说明仅写入日志，避免覆盖建造栏。
	UE_LOG(LogMoving_Castle, Log, TEXT("原型操作：测试卡牌已准备：滚轮或数字键 1-9、0 选择建造卡牌，左键放置，B 打开房间商店。"));
}

ACastleBuildingBase* ACastleBoard::PlacePrototypeBuildingAtCursor(const FCastleGridCursorTarget& CursorTarget, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (!CursorTarget.bValid)
	{
		OutFailureReason = FText::FromString(TEXT("屏幕中心没有对准棋盘格面。"));
		ShowPrototypeMessage(OutFailureReason.ToString(), FColor::Yellow);
		return nullptr;
	}

	if (!PrototypePalette.IsValidIndex(SelectedPrototypeSlot) || PrototypeBackpack == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("当前没有可用的样例卡牌。"));
		ShowPrototypeMessage(OutFailureReason.ToString(), FColor::Red);
		return nullptr;
	}

	const FCastleBuildingDescriptor& SelectedDescriptor = PrototypePalette[SelectedPrototypeSlot];
	const int32 BackpackIndex = FindPrototypeCardIndex(SelectedDescriptor);
	if (BackpackIndex == INDEX_NONE)
	{
		OutFailureReason = FText::FromString(TEXT("当前样例卡已经用完，请切换数字键选择其他卡牌。"));
		ShowPrototypeMessage(OutFailureReason.ToString(), FColor::Yellow);
		return nullptr;
	}
	return PlaceBackpackBuildingAtCursor(CursorTarget, BackpackIndex, OutFailureReason);
}

ACastleBuildingBase* ACastleBoard::PlaceBackpackBuildingAtCursor(const FCastleGridCursorTarget& CursorTarget, int32 BackpackIndex, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	FCastleBuildingDescriptor SelectedDescriptor;
	if (!CursorTarget.bValid || PrototypeBackpack == nullptr || !PrototypeBackpack->TryGetItemAt(BackpackIndex, SelectedDescriptor))
	{
		OutFailureReason = FText::FromString(TEXT("放置失败：请先选择有效背包卡牌，并将中心指针对准建造面。"));
		ShowPrototypeMessage(OutFailureReason.ToString(), FColor::Yellow);
		return nullptr;
	}

	FIntVector TargetCoordinate;
	if (!TryResolvePlacementCoordinate(CursorTarget, TargetCoordinate, OutFailureReason))
	{
		ShowPrototypeMessage(OutFailureReason.ToString(), FColor::Red);
		return nullptr;
	}

	// 预览、取牌前校验和真正生成始终携带同一个面，不能退回只知道落点的坐标接口。
	ACastleBuildingBase* PlacedBuilding = PlaceBuildingFromBackpackInternal(TargetCoordinate, PrototypeBackpack, BackpackIndex, true, &CursorTarget, OutFailureReason);
	if (PlacedBuilding == nullptr)
	{
		ShowPrototypeMessage(OutFailureReason.ToString(), FColor::Red);
		return nullptr;
	}

	if (PlacedBuilding->GetBuildingLevel() > SelectedDescriptor.Level)
	{
		ShowPrototypeMessage(FString::Printf(TEXT("合成完成：新 Actor 为 Lv%d，占用 %d 格。"), PlacedBuilding->GetBuildingLevel(), PlacedBuilding->GetOccupiedCoordinates().Num()), FColor::Green);
	}
	else
	{
		ShowPrototypeMessage(FString::Printf(TEXT("放置成功：%s，坐标 (%d, %d, %d)。"), *PlacedBuilding->GetBuildingDefinition()->DisplayName.ToString(), TargetCoordinate.X, TargetCoordinate.Y, TargetCoordinate.Z), FColor::Green);
	}
	return PlacedBuilding;
}

void ACastleBoard::SelectPrototypeSlot(int32 SlotIndex)
{
	if (!PrototypePalette.IsValidIndex(SlotIndex))
	{
		ShowPrototypeMessage(TEXT("无效的样例卡槽位。"), FColor::Red);
		return;
	}

	SelectedPrototypeSlot = SlotIndex;
	const FCastleBuildingDescriptor& Descriptor = PrototypePalette[SlotIndex];
	const int32 RemainingCount = FindPrototypeCardIndex(Descriptor) == INDEX_NONE ? 0 : 1;
	ShowPrototypeMessage(FString::Printf(TEXT("已选择样例槽 %d：类型 %d，颜色 %d，数字 %d，可用卡牌至少 %d 张。"), SlotIndex + 1, static_cast<int32>(Descriptor.GetBuildingType()), static_cast<int32>(Descriptor.Color), Descriptor.Number, RemainingCount), FColor::Cyan);
}

int32 ACastleBoard::FindPrototypeCardIndex(const FCastleBuildingDescriptor& Descriptor) const
{
	if (PrototypeBackpack == nullptr)
	{
		return INDEX_NONE;
	}

	const TArray<FCastleBuildingDescriptor> Items = PrototypeBackpack->GetItems();
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		const FCastleBuildingDescriptor& Item = Items[Index];
		if (Item.Definition == Descriptor.Definition
			&& Item.Color == Descriptor.Color
			&& Item.Number == Descriptor.Number
			&& Item.Level == Descriptor.Level)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

bool ACastleBoard::RebuildGroundVisualization(const TArray<FCastleGroundCell>& Cells, FText& OutFailureReason)
{
	TMap<FIntVector, TObjectPtr<ACastleGroundTileActor>> NewTiles;
	if (!CreateGroundTileActors(Cells, NewTiles, OutFailureReason)) { return false; }
	DestroyGroundTileActors();
	GroundTiles = MoveTemp(NewTiles);
	return true;
}

bool ACastleBoard::CreateGroundTileActors(const TArray<FCastleGroundCell>& Cells,
	TMap<FIntVector, TObjectPtr<ACastleGroundTileActor>>& OutTiles, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (GetWorld() == nullptr)
	{
		// 纯数据自动化测试允许没有 World；生成数据仍可由棋盘单独校验和接收。
		return true;
	}

	TMap<FIntVector, TObjectPtr<ACastleGroundTileActor>> NewGroundTiles;
	TArray<TObjectPtr<ACastleGroundTileActor>> SpawnedTiles;
	const FTransform BoardTransform = GetActorTransform();
	for (const FCastleGroundCell& Cell : Cells)
	{
		const FIntVector RelativeCoordinate = Cell.Coordinate - GridOrigin;
		const FVector LocalTileCenter(
			static_cast<float>(RelativeCoordinate.X) * CellSize,
			static_cast<float>(RelativeCoordinate.Y) * CellSize,
			WorldBaseHeight + static_cast<float>(RelativeCoordinate.Z) * LayerHeight - LayerHeight * 0.45f);
		const FTransform TileTransform(BoardTransform.GetRotation(), BoardTransform.TransformPosition(LocalTileCenter), BoardTransform.GetScale3D());
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACastleGroundTileActor* TileActor = GetWorld()->SpawnActor<ACastleGroundTileActor>(
			ACastleGroundTileActor::StaticClass(), TileTransform, SpawnParameters);
		if (TileActor == nullptr)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("棋盘格 Actor 生成失败，坐标 (%d, %d, %d)。"),
				Cell.Coordinate.X, Cell.Coordinate.Y, Cell.Coordinate.Z));
			for (ACastleGroundTileActor* SpawnedTile : SpawnedTiles)
			{
				if (IsValid(SpawnedTile)) { SpawnedTile->Destroy(); }
			}
			return false;
		}

		SpawnedTiles.Add(TileActor);
		if (!TileActor->InitializeGroundCell(Cell, GroundTileSize, LayerHeight))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("棋盘格 Actor 初始化失败，坐标 (%d, %d, %d)。"),
				Cell.Coordinate.X, Cell.Coordinate.Y, Cell.Coordinate.Z));
			for (ACastleGroundTileActor* SpawnedTile : SpawnedTiles)
			{
				if (IsValid(SpawnedTile)) { SpawnedTile->Destroy(); }
			}
			return false;
		}
		TileActor->SetGroundPresentationHidden(bGroundPresentationHidden);
		NewGroundTiles.Add(Cell.Coordinate, TileActor);
	}

	OutTiles = MoveTemp(NewGroundTiles);
	return true;
}

void ACastleBoard::DestroyGroundTileActors()
{
	for (const TPair<FIntVector, TObjectPtr<ACastleGroundTileActor>>& Pair : GroundTiles)
	{
		if (ACastleGroundTileActor* Tile = Pair.Value.Get(); IsValid(Tile))
		{
			Tile->Destroy();
		}
	}
	GroundTiles.Reset();
}

void ACastleBoard::ShowPrototypeMessage(const FString& Message, const FColor& Color) const
{
	UE_LOG(LogMoving_Castle, Log, TEXT("原型操作：%s"), *Message);
	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(20261107, 5.0f, Color, Message);
	}
}

