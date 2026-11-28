#include "CastleSkyIslandAcceptance.h"

#if !UE_BUILD_SHIPPING

#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Director/CastleDirectorNodeActor.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CastleRestSkyIsland.h"
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

	ACastleDirectorNodeActor* FindAcceptanceNode(const UCastleDirectorSubsystem& Director, const ECastleSkyIslandType Type, FText& OutReason)
	{
		// 节点序号、种子和权重直接使用导演的正式解析入口。
		for (TActorIterator<ACastleDirectorNodeActor> Node(Director.GetWorld()); Node; ++Node)
		{
			const auto Kind = Node->GetKind();
			if (Kind != ECastleDirectorNodeKind::RewardIsland && Kind != ECastleDirectorNodeKind::RestIsland) { continue; }
			ECastleSkyIslandType Selected;
			if (!Director.TryPreviewSkyIsland(**Node, Selected, OutReason)) { return nullptr; }
			if (Selected == Type) { return *Node; }
		}
		OutReason = INVTEXT("当前步没有对应类型的空岛节点。请推进关卡后重试。");
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
	FText Reason;
	auto* Node = FindAcceptanceNode(*Director, Type, Reason);
	if (!Node)
	{
		ShowIslandAcceptanceMessage(Reason);
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
