#if WITH_DEV_AUTOMATION_TESTS

#include "CastleBoard.h"
#include "CastleBuildingDefinition.h"
#include "CastleBuildingTestUtilities.h"
#include "CastleMassFragments.h"
#include "CastleMassUnitSubsystem.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Placement/CastleTowerSideGridComponent.h"
#include "Castle/Support/CastleSupportTowerData.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "MassEntitySubsystem.h"
#include "Moving_Castle.h"
#include "String/LexFromString.h"

namespace CastleSupportDemo
{
	void Feedback(const FString& Message, const bool bSucceeded)
	{
		if (bSucceeded) { UE_LOG(LogMoving_Castle, Display, TEXT("支援塔验收：%s"), *Message); }
		else { UE_LOG(LogMoving_Castle, Error, TEXT("支援塔验收：%s"), *Message); }
		if (GEngine) { GEngine->AddOnScreenDebugMessage(INDEX_NONE, 10.0f, bSucceeded ? FColor::Cyan : FColor::Red, Message); }
	}

	/** 只在全新的 MainMap PIE 使用固定牌和实际资产；不会把调试布局存为关卡内容。 */
	FAutoConsoleCommandWithWorldAndArgs Demo(TEXT("Castle.SupportDemo"),
		TEXT("Castle.SupportDemo <Hive|Balloon> <1|2|3|4> <Red|Yellow|Blue|Green>：MainMap 空棋盘支援塔验收。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const TMap<FString, ECastleTileColor> Colors{{TEXT("Red"), ECastleTileColor::Red}, {TEXT("Yellow"), ECastleTileColor::Yellow},
			{TEXT("Blue"), ECastleTileColor::Blue}, {TEXT("Green"), ECastleTileColor::Green}};
		int32 Level = 0;
		if (!World || World->WorldType != EWorldType::PIE || Args.Num() != 3
			|| (Args[0] != TEXT("Hive") && Args[0] != TEXT("Balloon")) || Args[1].Len() != 1
			|| !LexTryParseString(Level, *Args[1]) || Level < 1 || Level > 4 || !Colors.Contains(Args[2]))
		{
			Feedback(TEXT("请在 MainMap 的新 PIE 输入 Castle.SupportDemo <Hive|Balloon> <1|2|3|4> <Red|Yellow|Blue|Green>。"), false);
			return;
		}
		const auto* Director = World->GetSubsystem<UCastleDirectorSubsystem>();
		FText Reason;
		if (!Director || !Director->IsRunning() || !Director->IsEditingAllowed(Reason))
		{
			Feedback(TEXT("请在 MainMap 构筑阶段运行，不修改进行中的战斗。"), false);
			return;
		}
		TActorIterator<ACastleBoard> Iter(World);
		ACastleBoard* Board = Iter ? *Iter : nullptr;
		if (!Board || Board->GetPlacedBuildingCount() != 0)
		{
			Feedback(TEXT("支援塔验收需要空棋盘，请停止并重新运行 PIE。"), false);
			return;
		}
		const bool bHive = Args[0] == TEXT("Hive");
		const auto Type = bHive ? ECastleBuildingType::BeeHive : ECastleBuildingType::BalloonTower;
		UCastleBuildingDefinition* Definition = Board->FindBuildingDefinition(Type);
		const auto Color = Colors.FindChecked(Args[2]);
		const int32 Digit = bHive ? 8 : 4;
		FCastleBuildingDescriptor Card;
		if (!Definition || !Definition->CreateDescriptor(Level, Color, Digit, Card, Reason))
		{
			Feedback(FString::Printf(TEXT("地图商店池缺少有效的 %s 定义。%s"), *Args[0], *Reason.ToString()), false);
			return;
		}
		FIntPoint Min, Max;
		Board->GetBoardExtent(Min, Max);
		if (Max - Min != FIntPoint(4, 4)) { Feedback(TEXT("验收需要尚未扩张的 5×5 主关棋盘。"), false); return; }
		const bool bTowerSide = Board->PlacementSurfaceMode == ECastlePlacementSurfaceMode::MainTowerSides;
		FIntVector Origin(Min.X, Min.Y, Board->GridOrigin.Z);
		ACastleBuildingBase* Tower = nullptr;
		if (bTowerSide)
		{
			// 用一间同色过渡房取得指定数字，保证固定测试牌可放，同时不改写随机塔格。
			const FIntVector Base = Board->GetBoardCenterCoordinate();
			const FIntVector Offset(0, 1, 0);
			const FCastleTowerSideCell* Cell = Board->FindTowerSideCell(Base, ECastleGridFace::PositiveY);
			UCastleBuildingDefinition* Room = Board->FindBuildingDefinition(ECastleBuildingType::NormalRoom);
			FCastleBuildingDescriptor AnchorCard;
			if (!Cell || !Room || !Room->CreateDescriptor(1, Cell->Color, Digit, AnchorCard, Reason))
			{
				Feedback(TEXT("验收需要主塔 +Y 首层格和有效普通房定义：") + Reason.ToString(), false);
				return;
			}
			FCastleGridCursorTarget Source;
			Source.bValid = true;
			Source.Surface = ECastleGridCursorSurface::MainTowerSide;
			Source.Face = ECastleGridFace::PositiveY;
			Source.Cell = Base;
			Source.AdjacentCell = Base + Offset;
			if (!CastleBuildingTests::CompletePlacement(Board->PlaceBuildingAtCursor(Source, AnchorCard, false, Reason)))
			{
				Feedback(TEXT("无法放置验收过渡房：") + Reason.ToString(), false);
				return;
			}
			// 原演示需要在塔的 +X 侧有四格延伸，所以沿 +Y 向外布置待测支援塔。
			Source.Surface = ECastleGridCursorSurface::Building;
			Source.Cell = Source.AdjacentCell;
			Source.AdjacentCell += Offset;
			Origin = Source.AdjacentCell;
			Tower = Board->PlaceBuildingAtCursor(Source, Card, false, Reason);
		}
		else
		{
			// 地面兼容模式保留原演示测试地块；主塔模式绝不创建临时地面。
			FCastleGroundGenerationResult TestGround;
			for (int32 X = Min.X; X <= Max.X; ++X) for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				TestGround.Cells.Add({FIntVector(X, Y, Origin.Z), Color, Digit});
			}
			if (!Board->ApplyGroundGenerationResult(TestGround, Reason)) { Feedback(Reason.ToString(), false); return; }
			Tower = Board->PlaceBuilding(Origin, Card, false, Reason);
		}
		if (!CastleBuildingTests::CompletePlacement(Tower)) { Feedback(Reason.ToString(), false); return; }

		if (Level >= 3)
		{
			// 蜂巢使用固定 8→7→6→5；气球用紧贴的 4 号箭塔提供 2 点伤害联动。
			UCastleBuildingDefinition* Neighbor = Board->FindBuildingDefinition(bHive ? ECastleBuildingType::NormalRoom : ECastleBuildingType::ArcherTower);
			const FIntVector Offsets[] = { FIntVector(2, 0, 0), FIntVector(3, 0, 0), FIntVector(4, 0, 0), FIntVector(4, 1, 0) };
			for (int32 Index = 0; Index < (bHive ? 4 : 1); ++Index)
			{
				FCastleBuildingDescriptor NeighborCard;
				if (!Neighbor || !Neighbor->CreateDescriptor(1, Color, Digit - Index, NeighborCard, Reason))
				{
					Feedback(TEXT("无法创建验收邻居卡：") + Reason.ToString(), false);
					return;
				}
				const FIntVector Destination = Origin + Offsets[Index];
				ACastleBuildingBase* Placed = nullptr;
				if (bTowerSide)
				{
					// 每间验收房只从紧靠的已登记建筑面延伸，不能靠坐标接口假定有地面。
					FCastleGridCursorTarget Source;
					Source.bValid = true;
					Source.Surface = ECastleGridCursorSurface::Building;
					Source.Face = Index == 3 ? ECastleGridFace::PositiveY : ECastleGridFace::PositiveX;
					Source.Cell = Destination - (Index == 3 ? FIntVector(0, 1, 0) : FIntVector(1, 0, 0));
					Source.AdjacentCell = Destination;
					Placed = Board->PlaceBuildingAtCursor(Source, NeighborCard, false, Reason);
				}
				else { Placed = Board->PlaceBuilding(Destination, NeighborCard, false, Reason); }
				if (!CastleBuildingTests::CompletePlacement(Placed))
				{
					Feedback(TEXT("无法放置验收邻居：") + Reason.ToString(), false);
					return;
				}
			}
		}
		// 使用正式敌人模板，在塔旁生成；保持实际移动、攻击、仇恨和分裂行为。
		auto* EnemyConfig = LoadObject<UMassEntityConfigAsset>(nullptr,
			TEXT("/Game/MovingCastle/Mass/DA_MassEnemy_LavaGoliath.DA_MassEnemy_LavaGoliath"));
		auto* Units = World->GetSubsystem<UCastleMassUnitSubsystem>();
		TArray<FCastleMassUnitHandle> Enemies;
		const FVector AboveCell = Board->GetGridWorldLocation(Origin + FIntVector(0, 0, Level == 4 ? 1 : 0));
		if (!Units->SpawnUnits(EnemyConfig, {FTransform(AboveCell + FVector(-900, 0, 310))}, Enemies, Reason))
		{
			Feedback(TEXT("测试敌人生成失败：") + Reason.ToString(), false);
			return;
		}
		if (bHive && Level < 4)
		{
			// 让主巢打满一次后真实返航，便于分辨各备用巢，不受敌人提前阵亡干扰。
			World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager().GetFragmentDataChecked<FCastleMassUnitFragment>(Enemies[0].Entity).Health = 3000.0f;
		}
		if (Level == 4 && !bHive)
		{
			// 放一名正在上层房间内移动的正式 NPC，方便直接看到落地集结及返回原目标。
			TArray<FCastleMassUnitHandle> NPCs;
			const FVector Floor = AboveCell - FVector(0, 0, Board->LayerHeight * 0.5f);
			if (!Units->SpawnUnits(Definition->SupportData->NPCConfiguration, {FTransform(Floor + FVector(170, 0, 20))}, NPCs, Reason)
				|| !Units->MoveUnitsTo(NPCs, Floor + FVector(670, 400, 20)))
			{
				Feedback(TEXT("验收人员生成或移动失败：") + Reason.ToString(), false);
				return;
			}
		}
		FCastleBuildingTriggerContext Trigger;
		Trigger.Source = ECastleBuildingTriggerSource::Manual;
		if (!Tower->TriggerBuildingEffect(Trigger)) { Feedback(TEXT("验收塔主动触发失败，请看 Output Log。"), false); return; }
		Feedback(FString::Printf(TEXT("%s Lv%d 已放置在建造格 (%d,%d)。"), *Definition->DisplayName.ToString(), Level, Origin.X, Origin.Y), true);
	}));
}

#endif
