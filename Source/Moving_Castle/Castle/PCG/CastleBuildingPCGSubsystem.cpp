#include "CastleBuildingPCGSubsystem.h"

#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleBuildingPCGRule.h"
#include "BuildingMergeComponent.h"
#include "CastleDeterministicPCGNoise.h"
#include "CastleLegacyPCGAdapter.h"
#include "CastlePCGSemantics.h"
#include "CastleNavigationSubsystem.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Moving_Castle.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"

namespace
{
	/** 任何装配资源或功能几何不合法时在整盘预检失败，保持上一稳定场景。 */
	bool ValidatePlan(const ACastleBuildingBase& Building, const FCastlePCGApplyPlan& Plan, FString& Error)
	{
		for (const FCastlePCGMeshTemplate& Piece : Plan.Visual.Visuals)
		{
			if (!IsValid(Piece.Mesh) || !Piece.Relative.IsValid())
			{
				Error = FString::Printf(TEXT("%s 的视觉件 %s 使用无效网格或变换：%s。"),
					*Building.GetPathName(), *Piece.Node.ToString(), *GetPathNameSafe(Piece.Mesh));
				return false;
			}
		}
		for (const FCastlePCGFunctionalBox& Box : Plan.Gameplay.FunctionalBoxes)
		{
			if (!Box.Relative.IsValid() || Box.Extent.ContainsNaN() || Box.Extent.GetMin() <= 0.0)
			{
				Error = FString::Printf(TEXT("%s 的功能盒半尺寸或变换无效：%s。"),
					*Building.GetPathName(), *Box.Extent.ToString());
				return false;
			}
		}
		for (const FCastlePCGLadderPlan& Link : Plan.Gameplay.LadderFrames)
		{
			if (!Link.Relative.IsValid() || Link.BottomStand.ContainsNaN() || Link.TopStand.ContainsNaN()
				|| Link.LadderBottom.ContainsNaN() || Link.LadderTop.ContainsNaN())
			{
				Error = FString::Printf(TEXT("%s 的格 %s 梯链接坐标或变换无效。"),
					*Building.GetPathName(), *Link.LowerCell.ToString());
				return false;
			}
			// 与 Link 登记使用同一父节点变换。高度非法必须在导航锁和旧组件移交之前拒绝。
			const FTransform WorldFrame = Link.Relative * Building.GameplayRoot->GetComponentTransform();
			const double Rise = WorldFrame.TransformPosition(Link.LadderTop).Z - WorldFrame.TransformPosition(Link.LadderBottom).Z;
			if (!FMath::IsFinite(Rise) || Rise <= UE_DOUBLE_SMALL_NUMBER)
			{
				Error = FString::Printf(TEXT("%s 的格 %s 梯链接世界高度 %g 无效。"),
					*Building.GetPathName(), *Link.LowerCell.ToString(), Rise);
				return false;
			}
		}
		return true;
	}
}

bool UCastleBuildingPCGSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCastleBuildingPCGSubsystem::ObserveBoard(ACastleBoard* Board)
{
	if (ObservedBoards.Contains(Board)) { return; }
	ObservedBoards.Add(Board);
	Board->OnLayoutCommitted.AddUObject(this, &UCastleBuildingPCGSubsystem::HandleLayoutCommitted);
}

void UCastleBuildingPCGSubsystem::HandleLayoutCommitted(ACastleBoard* Board, const FVector Origin)
{
	FString Error;
	Recompute(Board, Origin, Error);
}

bool UCastleBuildingPCGSubsystem::Recompute(ACastleBoard* Board, const FVector Origin, FString& Error)
{
	Error.Reset();
	if (RecomputeInternal(Board, Origin, Error)) { return true; }
	UE_LOG(LogMoving_Castle, Error, TEXT("棋盘 %s 建筑PCG拒绝本轮更新：%s"), *GetPathNameSafe(Board), *Error);
	return false;
}

bool UCastleBuildingPCGSubsystem::RecomputeInternal(ACastleBoard* Board, const FVector Origin, FString& Error)
{
	if (!IsValid(Board) || !IsValid(Board->PCGRuleSet))
	{
		Error = FString::Printf(TEXT("棋盘 %s 未配置有效 PCGRuleSet。"), *GetPathNameSafe(Board));
		return false;
	}
	const UCastlePCGRuleSetDataAsset& Rules = *Board->PCGRuleSet;
	CastleV4PCG::FOccupancy Occupancy;
	TArray<ACastleBuildingBase*> Buildings;
	bool bHasLegacy = false;
	for (ACastleBuildingBase* Building : Board->GetPlacedBuildings())
	{
		// 飞入完成后才由棋盘发布正式占用，期间不读取尚未落定的组件数据。
		if (!Building->bHasCompletedPlacement) { continue; }
		const auto& Cells = Building->GetOccupiedCoordinates();
		if (Cells.IsEmpty() || !IsValid(Building->BuildingPCG) || !IsValid(Building->BuildingMerge) || !IsValid(Building->GameplayRoot))
		{
			Error = Building->GetPathName() + TEXT(" 缺少正式占格、PCG、建筑合并组件或 GameplayRoot。");
			return false;
		}
		for (const FIntVector& Cell : Cells)
		{
			if (Occupancy.Contains(Cell))
			{
				Error = FString::Printf(TEXT("%s 的格 %s 重复占用。"), *Building->GetPathName(), *Cell.ToString());
				return false;
			}
			Occupancy.Add(Cell, Building);
		}
		Buildings.Add(Building);
		bHasLegacy |= !Building->HachimisLayout;
	}

	// 非局部规则可影响任意格。每次先完成纯求解，再仅凭两个签名决定组件是否更新。
	const FCastlePCGBoardQuery Query(*Board, Occupancy, NewObject<UCastleDeterministicPCGNoise>(this));
	FCastlePCGSemanticField Field;
	if (!Rules.Solve(Query, Field, Error)) { return false; }
	CastleLegacyPCG::FVariants LegacyVariants;
	if (bHasLegacy && !CastleLegacyPCG::ReadVariants(*GetWorld(), Rules, LegacyVariants, Error)) { return false; }
	CastleV4PCG::FStairs Stairs;
	if (!CastleV4PCG::BuildStairs(*Board, Occupancy, Field, Stairs, Error)) { return false; }

	TArray<TPair<UCastleBuildingPCGComponent*, FCastlePCGApplyPlan>> Plans;
	TArray<int32> GameplayChanges;
	TArray<int32> VisualChanges;
	for (ACastleBuildingBase* Building : Buildings)
	{
		UCastleBuildingPCGComponent* Component = Building->BuildingPCG;
		if (!Component->CacheOriginalParts(Board, Error)) { return false; }
		FCastlePCGApplyPlan& Plan = Plans.Emplace_GetRef(Component, FCastlePCGApplyPlan{}).Value;
		if (Building->HachimisLayout)
		{
			if (!CastleV4PCG::Prepare(*Board, *Building, Rules, Field, Stairs, Component->GetOriginalParts(), Plan, Error)) { return false; }
			if (bHasLegacy) { CastleLegacyPCG::PrepareMixed(*Board, *Building, Rules, LegacyVariants, Field, Stairs, Plan); }
		}
		else if (!CastleLegacyPCG::Prepare(*Board, *Building, Rules, LegacyVariants, Field, Stairs,
			Component->GetOriginalParts(), Plan, Error)) { return false; }
		if (!CastleV4PCG::AddAttachments(*Board, *Building, Field, Plan, Error) || !ValidatePlan(*Building, Plan, Error)) { return false; }
		Plan.FinalizeKeys();
		const int32 Index = Plans.Num() - 1;
		if (Component->GetAppliedGameplayKey() != Plan.Gameplay.Key) { GameplayChanges.Add(Index); }
		if (Component->GetAppliedVisualKey() != Plan.Visual.Key)
		{
			if (!IsValid(Building->GetBuildingDefinition()))
			{
				Error = Building->GetPathName() + TEXT(" 缺少视觉过渡所需的建筑定义。");
				return false;
			}
			VisualChanges.Add(Index);
		}
	}
	if (!GameplayChanges.IsEmpty())
	{
		// 梯子必须登记到动态 Recast。无导航数据的纯逻辑世界允许提交普通碰撞，
		// 而有导航数据的世界必须能更新墙、桥、切板及挂件占用的导航区域。
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		const ANavigationData* Data = Navigation ? Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
		const bool bHasLadderChanges = GameplayChanges.ContainsByPredicate([&Plans](int32 Index)
		{
			return !Plans[Index].Value.Gameplay.LadderFrames.IsEmpty();
		});
		if ((bHasLadderChanges || Data)
			&& (!Cast<ARecastNavMesh>(Data) || Data->GetRuntimeGenerationMode() != ERuntimeGenerationType::Dynamic))
		{
			Error = Board->GetPathName() + TEXT(" 的玩法几何差量要求有效的动态 Recast 导航数据。");
			return false;
		}
		// 整轮玩法变更共享导航锁。试装失败时在锁内撤回次序相反的全部已触碰建筑。
		const FScopedCastleNavigationChange NavigationChange(GetWorld(), Board);
		for (int32 Change = 0; Change < GameplayChanges.Num(); ++Change)
		{
			auto& Entry = Plans[GameplayChanges[Change]];
			if (!Entry.Key->ApplyGameplay(Entry.Value.Gameplay, Error))
			{
				for (int32 Rollback = Change; Rollback >= 0; --Rollback) { Plans[GameplayChanges[Rollback]].Key->FinishGameplay(false); }
				return false;
			}
		}
		for (const int32 Index : GameplayChanges) { Plans[Index].Key->FinishGameplay(true); }
	}

	// 玩法提交点已过。逐栋应用视觉，每个过渡各自报告失败，其他建筑继续更新。
	for (const int32 Index : VisualChanges)
	{
		const auto& Entry = Plans[Index];
		// 仅表现签名变化时，不把动画登记到导航子系统。
		Entry.Key->ApplyVisual(*Board, Entry.Value.Visual, Origin, GameplayChanges.Contains(Index));
	}
	for (const auto& Entry : Plans) { Entry.Key->UpdateAppliedKey(); }
	if (!GameplayChanges.IsEmpty() || !VisualChanges.IsEmpty()) { OnPCGLayoutUpdated.Broadcast(Board); }
	return true;
}

void UCastleBuildingPCGSubsystem::GetActiveLadderConnections(const ACastleBoard* Board, TArray<FCastleActiveLadderConnection>& OutLadders) const
{
	OutLadders.Reset();
	if (!Board) { return; }
	for (auto* Building : Board->GetPlacedBuildings())
	{
		if (Building->bHasCompletedPlacement) { Building->BuildingPCG->GetLadders(OutLadders); }
	}
}

void UCastleBuildingPCGSubsystem::Deinitialize()
{
	for (const auto& Board : ObservedBoards) { if (Board.IsValid()) { Board->OnLayoutCommitted.RemoveAll(this); } }
	ObservedBoards.Reset();
	Super::Deinitialize();
}
