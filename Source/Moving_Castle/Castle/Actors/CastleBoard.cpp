// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBoard.h"

#include "CastleBackpackComponent.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleGroundTileActor.h"
#include "BuildingMergeComponent.h"
#include "AssemblyFlightComponent.h"
#include "CastleGroundGenerator.h"
#include "CastleCombatData.h"
#include "CastleMainTowerComponent.h"
#include "CastleMassEnemySpawner.h"
#include "CastleNavigationSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
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

	if (bGenerateGroundOnBeginPlay)
	{
		FText FailureReason;
		if (!GenerateGround(FailureReason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 BeginPlay 生成地面失败：%s"), *FailureReason.ToString());
		}
	}

	if (GeneratedGroundCells.Num() > 0)
	{
		InitializePrototypeDeck();
	}

	if (bSpawnMainTowerOnBeginPlay)
	{
		FText FailureReason;
		if (!SpawnMainTower(FailureReason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 BeginPlay 生成主塔失败：%s"), *FailureReason.ToString());
			return;
		}
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

	const FTransform TowerTransform(GetActorQuat(), GetGridWorldLocation(GetBoardCenterCoordinate()), GetActorScale3D());
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
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

	MainTowerActor = TowerActor;
	UE_LOG(LogMoving_Castle, Display, TEXT("主塔已在棋盘中心格 (%d, %d, %d) 生成，生命 %.1f。"),
		GetBoardCenterCoordinate().X, GetBoardCenterCoordinate().Y, GetBoardCenterCoordinate().Z,
		CombatData->MainTowerMaxHealth);
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
	Request.GridSize = GridSize;
	Request.Origin = GridOrigin;
	Request.Seed = GroundSeed;
	Request.bReserveRoadStartTile = bReserveRoadStartTile;

	const FCastleGroundGenerationResult Result = Generator->GenerateGround(Request);
	return ApplyGroundGenerationResult(Result, OutFailureReason);
}

bool ACastleBoard::ApplyGroundGenerationResult(const FCastleGroundGenerationResult& Result, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

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

	const int64 ExpectedCellCount = static_cast<int64>(GridSize.X) * static_cast<int64>(GridSize.Y);
	if (Result.Cells.Num() != ExpectedCellCount)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("地面结果数量不完整：需要 %lld 个，实际收到 %d 个。"), ExpectedCellCount, Result.Cells.Num()));
		return false;
	}

	TSet<FIntVector> UniqueCoordinates;
	UniqueCoordinates.Reserve(Result.Cells.Num());

	for (const FCastleGroundCell& Cell : Result.Cells)
	{
		if (!IsCoordinateWithinBoard(Cell.Coordinate) || Cell.Coordinate.Z != GridOrigin.Z)
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

		if (Cell.Number < 0 || Cell.Number > 9)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("地面坐标 (%d, %d, %d) 的数字 %d 无效。"), Cell.Coordinate.X, Cell.Coordinate.Y, Cell.Coordinate.Z, Cell.Number));
			return false;
		}

		if ((Cell.Color == ECastleTileColor::Gray && Cell.Number != 0) || (Cell.Color != ECastleTileColor::Gray && Cell.Number == 0))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("地面坐标 (%d, %d, %d) 的颜色和数字组合无效。"), Cell.Coordinate.X, Cell.Coordinate.Y, Cell.Coordinate.Z));
			return false;
		}
	}

	// 数据和 Actor 都准备完成后才提交新地面，失败时保留原棋盘状态。
	const FScopedCastleNavigationChange NavigationChange(GetWorld(), this);
	if (!RebuildGroundVisualization(Result.Cells, OutFailureReason))
	{
		return false;
	}
	GeneratedGroundCells = Result.Cells;
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

bool ACastleBoard::IsCoordinateWithinBoard(const FIntVector& Coordinate) const
{
	const FIntVector RelativeCoordinate = Coordinate - GridOrigin;
	return RelativeCoordinate.X >= 0 && RelativeCoordinate.X < GridSize.X
		&& RelativeCoordinate.Y >= 0 && RelativeCoordinate.Y < GridSize.Y
		&& RelativeCoordinate.Z >= 0 && RelativeCoordinate.Z < GridSize.Z;
}

bool ACastleBoard::CanPlaceBuilding(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();

	if (!ValidateBuildingDescriptor(Descriptor, OutFailureReason))
	{
		return false;
	}

	const TArray<FIntVector> Footprint = BuildPlacementFootprint(Coordinate, Descriptor.Level);
	bool bHasMatch = false;
	for (const FIntVector& Cell : Footprint)
	{
		if (!IsCoordinateWithinBoard(Cell) || GetBuildingAt(Cell) != nullptr)
		{
			OutFailureReason = FText::FromString(TEXT("建筑占用范围超出棋盘，或其中有格子已被占用。"));
			return false;
		}
		FCastleGroundCell GroundCell;
		if (Cell.Z == GridOrigin.Z && !TryGetGroundCellAtPlane(Cell, GroundCell))
		{
			OutFailureReason = FText::FromString(TEXT("建筑占用范围中的地面尚未生成。"));
			return false;
		}
		if (Cell.Z > GridOrigin.Z)
		{
			const FIntVector Support = Cell - FIntVector(0, 0, 1);
			// 四级卡自带的下层格也能支撑自身上层；其他底面仍必须有实际建筑支撑。
			if (!Footprint.Contains(Support) && GetBuildingAt(Support) == nullptr)
			{
				OutFailureReason = FText::FromString(TEXT("上层建筑的整个底面必须有紧贴下方的建筑支撑。"));
				return false;
			}
		}
		bHasMatch |= HasPlacementMatch(Cell, Descriptor);
	}
	if (!bHasMatch)
	{
		OutFailureReason = FText::FromString(TEXT("建筑占用范围或六面相邻格没有满足颜色/数字匹配规则的对象。"));
		return false;
	}

	return true;
}

ACastleBuildingBase* ACastleBoard::PlaceBuilding(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor, bool bTryMerge, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();

	if (!CanPlaceBuilding(Coordinate, Descriptor, OutFailureReason))
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
	ForgetPlacement(Building);
	// 也覆盖关卡代码直接 Destroy 的中途取消；合并路径已先注销，不能删除新结果的坐标。
	for (const FIntVector& Coordinate : Building->GetOccupiedCoordinates())
	{
		if (GetBuildingAt(Coordinate) == Building) { BuildingByCoordinate.Remove(Coordinate); }
	}
}

void ACastleBoard::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const auto& Pair : BuildingByCoordinate)
	{
		if (ACastleBuildingBase* Building = Pair.Value.Get(); IsValid(Building))
		{
			Building->AssemblyFlight->OnAssemblyFinishedNative.RemoveAll(this);
			Building->OnDestroyed.RemoveDynamic(this, &ACastleBoard::HandleBuildingDestroyed);
		}
	}
	PendingPlacements.Reset();
	ReadyMergeRequests.Reset();
	DestroyGroundTileActors();
	Super::EndPlay(EndPlayReason);
}

ACastleBuildingBase* ACastleBoard::PlaceBuildingFromBackpack(const FIntVector& Coordinate, UCastleBackpackComponent* Backpack, int32 BackpackIndex, bool bTryMerge, FText& OutFailureReason)
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
	if (!CanPlaceBuilding(Coordinate, Descriptor, OutFailureReason))
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

	ACastleBuildingBase* PlacedBuilding = PlaceBuilding(Coordinate, RemovedDescriptor, bTryMerge, OutFailureReason);
	if (PlacedBuilding != nullptr)
	{
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

	if (!Building->Destroy())
	{
		OutFailureReason = FText::FromString(TEXT("建筑已从棋盘索引移除，但 Actor 销毁失败。"));
		return false;
	}

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

	// 只接受地面格和建筑网格的真实碰撞命中；空中或棋盘边界不代表可选中的建造面。
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CastleGridCursor), true);
	FHitResult HitResult;
	if (!GetWorld()->LineTraceSingleByChannel(HitResult, RayStart, RayEnd, ECC_Visibility, QueryParams))
	{
		return false;
	}

	FIntVector HitCell = FIntVector::ZeroValue;
	bool bFoundCell = false;
	bool bHitGroundTile = false;

	if (const ACastleGroundTileActor* HitGroundTile = Cast<ACastleGroundTileActor>(HitResult.GetActor()))
	{
		const TObjectPtr<ACastleGroundTileActor>* RegisteredTile = GroundTiles.Find(HitGroundTile->GetGroundCell().Coordinate);
		if (RegisteredTile != nullptr && RegisteredTile->Get() == HitGroundTile
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
	return OutTarget.bValid;
}

void ACastleBoard::BuildGridCursorTarget(const FIntVector& Cell, const FVector& LocalImpactPoint, const FVector& LocalNormal, FCastleGridCursorTarget& OutTarget) const
{
	if (!IsCoordinateWithinBoard(Cell))
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
		CellSize * 0.94f / 100.0f,
		CellSize * 0.94f / 100.0f,
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
		LocalPlaneScale = FVector(2.0f / 100.0f, CellSize * 0.94f / 100.0f, LayerHeight * 0.94f / 100.0f);
		break;
	case ECastleGridFace::PositiveY:
	case ECastleGridFace::NegativeY:
		LocalPlaneCenter.Y = LocalImpactPoint.Y;
		LocalPlaneScale = FVector(CellSize * 0.94f / 100.0f, 2.0f / 100.0f, LayerHeight * 0.94f / 100.0f);
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

TArray<FIntVector> ACastleBoard::BuildPlacementFootprint(const FIntVector& Coordinate, int32 Level) const
{
	// 与既有合成形状一致：二级横向两格，三级平面四格，四级两层八格。
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
		if (!IsCoordinateWithinBoard(Coordinate))
		{
			OutFailureReason = FText::FromString(TEXT("建筑占用坐标超出棋盘边界。"));
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

	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		BuildingByCoordinate.Add(Coordinate, Building);
	}
	Building->OnDestroyed.AddUniqueDynamic(this, &ACastleBoard::HandleBuildingDestroyed);
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
	if (Descriptor.Level == 2 && OccupiedCoordinates[0].X == OccupiedCoordinates[1].X)
	{
		// 标准二级蓝图沿 X 轴制作，沿 Y 合成时旋转整个建筑，不再依赖模型载体节点。
		SpawnRotation *= FQuat(FVector::UpVector, HALF_PI);
	}
	ACastleBuildingBase* NewBuilding = GetWorld()->SpawnActor<ACastleBuildingBase>(Descriptor.GetBuildingClass(),
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
		// 一级合成二级只能在水平 X/Y 方向形成 2x1，不允许用上下堆叠完成合成。
		bValidShape = Dimensions.Z == 1 && ((Dimensions.X == 2 && Dimensions.Y == 1) || (Dimensions.X == 1 && Dimensions.Y == 2));
		break;
	case 2:
		// 二级合成三级固定为同一平面 2x2 方形。
		bValidShape = Dimensions == FIntVector(2, 2, 1);
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
	const TSubclassOf<ACastleBuildingBase> NextBuildingClass = First->GetClassForLevel(NextLevel);
	if (NextBuildingClass == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("当前建筑族没有提供下一级具体 Actor 类。"));
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

bool ACastleBoard::HasPlacementMatch(const FIntVector& Coordinate, const FCastleBuildingDescriptor& Descriptor) const
{
	const bool bIsRoad = Descriptor.GetBuildingType() == ECastleBuildingType::Road;

	// 地面只存在于 Z=0 平面，只有在该平面放置时才把目标地面本身作为匹配来源。
	if (Coordinate.Z == GridOrigin.Z)
	{
		FCastleGroundCell TargetGroundCell;
		if (TryGetGroundCellAtPlane(Coordinate, TargetGroundCell))
		{
			if (bIsRoad ? TargetGroundCell.IsRoadTile() : (TargetGroundCell.Color == Descriptor.Color || TargetGroundCell.Number == Descriptor.Number))
			{
				return true;
			}
		}
	}

	TArray<FIntVector> Neighbors;
	GetFaceNeighborCoordinates(Coordinate, Neighbors);
	for (const FIntVector& NeighborCoordinate : Neighbors)
	{
		if (!IsCoordinateWithinBoard(NeighborCoordinate))
		{
			continue;
		}

		if (ACastleBuildingBase* NeighborBuilding = GetBuildingAt(NeighborCoordinate))
		{
			if (bIsRoad)
			{
				if (NeighborBuilding->IsRoad())
				{
					return true;
				}
			}
			else if (!NeighborBuilding->IsRoad() && (NeighborBuilding->GetBuildingColor() == Descriptor.Color || NeighborBuilding->GetBuildingNumber() == Descriptor.Number))
			{
				return true;
			}

			continue;
		}

		if (NeighborCoordinate.Z == GridOrigin.Z)
		{
			FCastleGroundCell NeighborGroundCell;
			if (TryGetGroundCellAtPlane(NeighborCoordinate, NeighborGroundCell))
			{
				if (bIsRoad ? NeighborGroundCell.IsRoadTile() : (NeighborGroundCell.Color == Descriptor.Color || NeighborGroundCell.Number == Descriptor.Number))
				{
					return true;
				}
			}
		}
	}

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

	TArray<FCastleGroundCell> FunctionalCells;
	for (const FCastleGroundCell& Cell : GeneratedGroundCells)
	{
		if (!Cell.IsRoadTile())
		{
			FunctionalCells.Add(Cell);
		}
	}

	if (FunctionalCells.Num() == 0)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("棋盘原型卡组初始化失败：生成结果中没有功能地面格。"));
		return;
	}

	// 样例功能卡牌的颜色和数字直接取本局实际地面，保证第一次点击时不用手动猜匹配属性。
	const FCastleGroundCell& ArcherCell = FunctionalCells[0];
	const FCastleGroundCell& CannonCell = FunctionalCells[FMath::Min(1, FunctionalCells.Num() - 1)];
	const FCastleGroundCell& FarmCell = FunctionalCells[FMath::Min(2, FunctionalCells.Num() - 1)];

	FText DefinitionReason;
	if (!ValidateBuildingDefinitions(DefinitionReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("开局卡组配置错误：%s"), *DefinitionReason.ToString());
		return;
	}
	const ECastleBuildingType Types[] = { ECastleBuildingType::Road, ECastleBuildingType::ArcherTower,
		ECastleBuildingType::CannonTower, ECastleBuildingType::Farm };
	const FCastleGroundCell Cells[] = { FCastleGroundCell(), ArcherCell, CannonCell, FarmCell };
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

	// 每种样例卡放两张，箭塔可以直接在相邻地块完成一次 1 级到 2 级的合成。
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
	ShowPrototypeMessage(TEXT("测试卡牌已准备：按 1-9、0 选择背包槽位，左键放置，B 打开房间商店。"), FColor::Cyan);
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
		OutFailureReason = FText::FromString(TEXT("放置失败：请先选择有效背包卡牌，并将中心指针对准棋盘格面。"));
		ShowPrototypeMessage(OutFailureReason.ToString(), FColor::Yellow);
		return nullptr;
	}

	// 地面顶面是初始建筑的支撑格，其他格面则把放置位置设为该面的外侧邻格。
	FIntVector TargetCoordinate = CursorTarget.AdjacentCell;
	if (CursorTarget.Face == ECastleGridFace::Up && CursorTarget.Cell.Z == GridOrigin.Z)
	{
		FCastleGroundCell GroundCell;
		if (TryGetGroundCellAtPlane(CursorTarget.Cell, GroundCell) && GetBuildingAt(CursorTarget.Cell) == nullptr)
		{
			TargetCoordinate = CursorTarget.Cell;
		}
	}

	ACastleBuildingBase* PlacedBuilding = PlaceBuildingFromBackpack(TargetCoordinate, PrototypeBackpack, BackpackIndex, true, OutFailureReason);
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
		if (!TileActor->InitializeGroundCell(Cell, CellSize, LayerHeight))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("棋盘格 Actor 初始化失败，坐标 (%d, %d, %d)。"),
				Cell.Coordinate.X, Cell.Coordinate.Y, Cell.Coordinate.Z));
			for (ACastleGroundTileActor* SpawnedTile : SpawnedTiles)
			{
				if (IsValid(SpawnedTile)) { SpawnedTile->Destroy(); }
			}
			return false;
		}
		NewGroundTiles.Add(Cell.Coordinate, TileActor);
	}

	DestroyGroundTileActors();
	GroundTiles = MoveTemp(NewGroundTiles);
	UE_LOG(LogMoving_Castle, Log, TEXT("棋盘管理器已创建 %d 个独立地面格 Actor，单格间距 %.1f 厘米，层高 %.1f 厘米。"),
		GroundTiles.Num(), CellSize, LayerHeight);
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

