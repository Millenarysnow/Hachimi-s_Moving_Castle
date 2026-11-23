#include "CastleSkyIslandAcceptance.h"

#if !UE_BUILD_SHIPPING

#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "CastleRestSkyIsland.h"
#include "CastleBoard.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Moving_Castle.h"

namespace
{
	void ShowIslandAcceptanceMessage(const FText& Message)
	{
		UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *Message.ToString());
		if (GEngine) { GEngine->AddOnScreenDebugMessage(INDEX_NONE, 5.0f, FColor::Yellow, Message.ToString()); }
	}

	ACastleDirectorNodeActor* FindAcceptanceNode(const UCastleDirectorSubsystem& Director, const ECastleSkyIslandType Type)
	{
		UWorld* World = Director.GetWorld();
		const auto* Config = Director.GetConfig();
		if (Type == ECastleSkyIslandType::Rest)
		{
			for (TActorIterator<ACastleDirectorNodeActor> Node(World); Node; ++Node)
			{
				if (Node->GetKind() == ECastleDirectorNodeKind::RestIsland) { return *Node; }
			}
			return nullptr;
		}

		const auto* Towers = World->GetSubsystem<UCastleMainTowerSubsystem>();
		const auto* Tower = Towers ? Towers->GetRegisteredMainTower() : nullptr;
		const TActorIterator<ACastleBoard> Board(World);
		if (!Tower || !Board) { return nullptr; }

		TArray<ACastleDirectorNodeActor*> Nodes;
		for (TActorIterator<ACastleDirectorNodeActor> Node(World); Node; ++Node) { Nodes.Add(*Node); }
		const FVector Center = Tower->GetTargetLocation();
		const double ForwardYaw = Board->GetActorForwardVector().Rotation().Yaw;
		// 导演从棋盘前方圆弧的负角度向正角度生成节点。恢复节点序号以核对奖励岛的实际类型。
		Nodes.Sort([Center, ForwardYaw](const ACastleDirectorNodeActor& A, const ACastleDirectorNodeActor& B)
		{
			return FMath::FindDeltaAngleDegrees(ForwardYaw, (A.GetActorLocation() - Center).Rotation().Yaw)
				< FMath::FindDeltaAngleDegrees(ForwardYaw, (B.GetActorLocation() - Center).Rotation().Yaw);
		});
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (Nodes[Index]->GetKind() != ECastleDirectorNodeKind::RewardIsland) { continue; }
			const uint32 Seed = static_cast<uint32>(Config->RunSeed) ^ (static_cast<uint32>(Director.GetNextStep()) * 0x9e3779b9u)
				^ (static_cast<uint32>(Index) * 0x85ebca6bu);
			FRandomStream Random(static_cast<int32>(Seed));
			if (static_cast<ECastleSkyIslandType>(Random.RandRange(0, 2)) == Type) { return Nodes[Index]; }
		}
		return nullptr;
	}
}

void CastleQueueSkyIslandAcceptance(UWorld* World, const ECastleSkyIslandType Type,
	const TSubclassOf<ACastleSkyIslandBase> IslandClass)
{
	auto* Director = World && World->IsGameWorld() ? World->GetSubsystem<UCastleDirectorSubsystem>() : nullptr;
	auto* Config = Director ? Director->GetConfig() : nullptr;
	if (!Director || !Director->IsRunning() || Director->GetPhase() != ECastleDirectorPhase::Editing || !Config)
	{
		ShowIslandAcceptanceMessage(INVTEXT("请在 MainMap 的 PIE 编辑阶段运行空岛验收命令。"));
		return;
	}
	auto* Node = FindAcceptanceNode(*Director, Type);
	if (!Node)
	{
		ShowIslandAcceptanceMessage(INVTEXT("当前步没有对应类型的空岛节点。请推进关卡后重试。"));
		return;
	}

	// 仅在节点等待导演 Tick 的短窗口登记验收类，随后恢复内存映射。全程不标记或保存资产。
	static bool bSelectionQueued = false;
	if (bSelectionQueued)
	{
		ShowIslandAcceptanceMessage(INVTEXT("已有待处理的空岛节点选择。"));
		return;
	}
	bSelectionQueued = true;
	const TSubclassOf<ACastleSkyIslandBase> Previous = Config->SkyIslandClasses.FindRef(Type);
	const bool bHadEntry = Config->SkyIslandClasses.Contains(Type);
	Config->SkyIslandClasses.Add(Type, IslandClass);
	Director->HandleNodeTouched(Node);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakConfig = TWeakObjectPtr<UCastleDirectorConfigDataAsset>(Config), Type, Previous, bHadEntry, Frame = GFrameCounter](float)
		{
			// 留出一个完整世界帧，让导演先处理排队节点；即使传送失败也会恢复。
			if (GFrameCounter <= Frame + 1) { return true; }
			if (auto* LiveConfig = WeakConfig.Get())
			{
				if (bHadEntry) { LiveConfig->SkyIslandClasses.Add(Type, Previous); }
				else { LiveConfig->SkyIslandClasses.Remove(Type); }
			}
			bSelectionQueued = false;
			return false;
		}));
}

static FAutoConsoleCommandWithWorld CastleTestRestSkyIslandCommand(
	TEXT("CastleTestRestSkyIsland"), TEXT("Enter the current rest sky island node for acceptance."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		CastleQueueSkyIslandAcceptance(World, ECastleSkyIslandType::Rest, ACastleRestSkyIsland::StaticClass());
	}));

#endif
