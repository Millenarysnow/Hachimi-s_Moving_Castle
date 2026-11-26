#if !UE_BUILD_SHIPPING

#include "Blueprint/GameViewportSubsystem.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Castle/Controllers/CastlePlayerController.h"
#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Items/CastleBadgeDataAsset.h"
#include "Items/CastleBadgeSubsystem.h"
#include "Items/CastleItemGrantLibrary.h"
#include "Items/CastlePendingRoomReward.h"
#include "Items/CastleRoomGroupDataAsset.h"
#include "Kismet/GameplayStatics.h"
#include "Moving_Castle.h"
#include "TimerManager.h"
#include "UI/Items/CastleBadgeBarWidget.h"
#include "UI/Items/CastleBadgeReplaceModalWidget.h"
#include "UI/Items/CastleTempBuildBarWidget.h"
#include "UI/Items/CastleWildcardRoomPickerWidget.h"

namespace CastleItemsDebug
{
	// 固定按实现指南 7.2、7.3 节的编号映射，枚举增删或资产注册顺序都不改变命令语义。
	constexpr const TCHAR* RoomGroupNames[] =
	{
		TEXT("DA_RoomGroup_01_RandomSupply"),
		TEXT("DA_RoomGroup_02_WangcaiPatchBag"),
		TEXT("DA_RoomGroup_03_ShieldBehindField"),
		TEXT("DA_RoomGroup_04_CustomNormalRoom"),
		TEXT("DA_RoomGroup_05_TwinRooms"),
		TEXT("DA_RoomGroup_06_TopSupply"),
		TEXT("DA_RoomGroup_07_TripleThreeArchers"),
		TEXT("DA_RoomGroup_08_LuckyCannons"),
		TEXT("DA_RoomGroup_09_SequentialFarms"),
		TEXT("DA_RoomGroup_10_Highway"),
		TEXT("DA_RoomGroup_11_RedStorm"),
		TEXT("DA_RoomGroup_12_DoubleStairPuzzle"),
		TEXT("DA_RoomGroup_13_TricolorAmmoSquad"),
		TEXT("DA_RoomGroup_14_ThreeWayToolbox"),
		TEXT("DA_RoomGroup_15_BigNumberRoom"),
		TEXT("DA_RoomGroup_16_RandomFuncCustomRoom"),
		TEXT("DA_RoomGroup_17_ThreeBlueEights"),
		TEXT("DA_RoomGroup_18_HiveChainBox"),
		TEXT("DA_RoomGroup_19_FullyCustomRoom"),
		TEXT("DA_RoomGroup_20_Normalize"),
		TEXT("DA_RoomGroup_21_PortalRoom"),
		TEXT("DA_RoomGroup_22_GreenLushRedWither"),
		TEXT("DA_RoomGroup_23_SelfOrbitCannon"),
		TEXT("DA_RoomGroup_24_TowerBuyout"),
		TEXT("DA_RoomGroup_25_DirectDeploy"),
		TEXT("DA_RoomGroup_26_ColorShift"),
	};

	constexpr const TCHAR* BadgeNames[] =
	{
		TEXT("DA_Badge_01_FirepowerCard"),
		TEXT("DA_Badge_02_CoinPurse"),
		TEXT("DA_Badge_03_StartingFunds"),
		TEXT("DA_Badge_04_DoubleDoorPass"),
		TEXT("DA_Badge_05_SpareBowstring"),
		TEXT("DA_Badge_06_BalloonRack"),
		TEXT("DA_Badge_07_ShopDiscount"),
		TEXT("DA_Badge_08_ShopRebate"),
		TEXT("DA_Badge_09_ShiftWhistle"),
		TEXT("DA_Badge_10_StringGlove"),
		TEXT("DA_Badge_11_QuickstringPass"),
		TEXT("DA_Badge_12_FreeRefresh"),
		TEXT("DA_Badge_13_QuickLadder"),
		TEXT("DA_Badge_14_LoopPurchase"),
		TEXT("DA_Badge_15_LateAdvantage"),
		TEXT("DA_Badge_16_SiphonArrow"),
		TEXT("DA_Badge_17_BlueAbacus"),
		TEXT("DA_Badge_18_WorkshopRefund"),
		TEXT("DA_Badge_19_EmergencyBallast"),
		TEXT("DA_Badge_20_RapidMarch"),
		TEXT("DA_Badge_21_BerserkBee"),
		TEXT("DA_Badge_22_ChainReaction"),
		TEXT("DA_Badge_23_BurstAlarm"),
		TEXT("DA_Badge_24_LapReassignment"),
		TEXT("DA_Badge_25_LastBalloonClasp"),
	};

	/** 验收只操作当前游戏世界里唯一的棋盘，无明确目标时报告错误。 */
	ACastleBoard* FindBoard(UWorld* World)
	{
		if (World && World->IsGameWorld())
		{
			TActorIterator<ACastleBoard> Actor(World);
			ACastleBoard* Board = Actor ? *Actor : nullptr;
			if (Actor)
			{
				++Actor;
			}
			if (Board && Board->PrototypeBackpack && !Actor)
			{
				return Board;
			}
		}
		UE_LOG(LogMoving_Castle, Error, TEXT("道具验收需要游戏世界内有唯一的 CastleBoard 及其背包：%s。"), *GetPathNameSafe(World));
		return nullptr;
	}

	APlayerController* FindPlayer(UWorld* World)
	{
		APlayerController* Player = IsValid(World) && World->IsGameWorld() ? World->GetFirstPlayerController() : nullptr;
		if (!IsValid(Player) || !Player->IsLocalController())
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("道具验收需要本地玩家控制器：%s。"), *GetPathNameSafe(World));
			return nullptr;
		}
		return Player;
	}

	template<typename T>
	TSubclassOf<T> LoadWidget(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/MovingCastle/UI/Items/%s.%s_C"), Name, Name);
		const TSubclassOf<T> WidgetClass = LoadClass<T>(nullptr, *Path);
		if (!WidgetClass)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("道具界面类 %s 无法加载。"), *Path);
		}
		return WidgetClass;
	}

	template<typename T>
	int32 CountWidgets(UWorld* World)
	{
		TArray<UUserWidget*> Instances;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Instances, T::StaticClass(), true);
		return Instances.Num();
	}

	template<typename T>
	T* LoadData(const TCHAR* Directory, const TCHAR* Name)
	{
		// 目录和对象名由编号表指定，不扫描资产或按枚举值反推文件名。
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), Directory, Name, Name);
		T* Asset = LoadObject<T>(nullptr, *Path);
		if (!Asset)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("道具数据资产 %s 无法加载。"), *Path);
		}
		return Asset;
	}

	UCastleBadgeSubsystem* FindBadges(UWorld* World)
	{
		UGameInstance* Instance = IsValid(World) ? World->GetGameInstance() : nullptr;
		UCastleBadgeSubsystem* Badges = Instance ? Instance->GetSubsystem<UCastleBadgeSubsystem>() : nullptr;
		if (!Badges)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("徽章验收世界 %s 缺少 GameInstance 徽章子系统。"), *GetPathNameSafe(World));
		}
		return Badges;
	}

	/** 预览挂点由验收命令指定，面板内部布局始终由各自的 WBP 提供。 */
	void Present(UUserWidget& Widget, const FVector2D Position, const int32 ZOrder)
	{
		Widget.SetDesiredSizeInViewport(FVector2D(450.0, 260.0));
		Widget.SetPositionInViewport(Position, false);
		Widget.AddToViewport(ZOrder);
	}

	void MountHUD(UWorld* World)
	{
		APlayerController* Player = FindPlayer(World);
		ACastleBoard* Board = FindBoard(World);
		UCastleBadgeSubsystem* Badges = FindBadges(World);
		if (!Player || !Board || !Badges || !GEngine || !GEngine->GameViewport)
		{
			return;
		}
		if (CountWidgets<UCastleTempBuildBarWidget>(World) || CountWidgets<UCastleBadgeBarWidget>(World)
			|| CountWidgets<UCastleBadgeReplaceModalWidget>(World))
		{
			UE_LOG(LogMoving_Castle, Display, TEXT("道具验收控件已挂载。"));
			return;
		}
		const auto TempClass = LoadWidget<UCastleTempBuildBarWidget>(TEXT("WBP_TempBuildBar"));
		const auto BadgeClass = LoadWidget<UCastleBadgeBarWidget>(TEXT("WBP_BadgeBar"));
		const auto ReplaceClass = LoadWidget<UCastleBadgeReplaceModalWidget>(TEXT("WBP_BadgeReplaceModal"));
		if (!TempClass || !BadgeClass || !ReplaceClass)
		{
			return;
		}
		auto* Temp = CreateWidget<UCastleTempBuildBarWidget>(Player, TempClass);
		auto* Bar = CreateWidget<UCastleBadgeBarWidget>(Player, BadgeClass);
		auto* Modal = CreateWidget<UCastleBadgeReplaceModalWidget>(Player, ReplaceClass);
		if (!Temp || !Bar || !Modal || !Temp->BindBackpack(Board->PrototypeBackpack)
			|| !Bar->BindBadges(Badges) || !Modal->BindBadges(Badges))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("未能绑定完整的道具验收 HUD。"));
			return;
		}
		FVector2D ViewportSize;
		GEngine->GameViewport->GetViewportSize(ViewportSize);
		Present(*Temp, FVector2D(24.0, ViewportSize.Y * 0.60), 45);
		Present(*Bar, FVector2D(24.0, 50.0), 45);
		// 替换弹窗和正式入口使用同一视口范围，居中与等比缩放由 WBP 自己决定。
		Modal->AddToViewport(60);
		Player->SetInputMode(FInputModeGameAndUI());
		Player->SetShowMouseCursor(true);
		UE_LOG(LogMoving_Castle, Display, TEXT("道具验收 HUD 已挂载，可用 Castle.Items.Group 与 Castle.Items.Badge 发放。"));
	}

	bool ReadOrdinal(const TArray<FString>& Args, const int32 Maximum, int32& OutIndex, const TCHAR* Hint)
	{
		int32 OneBased = 0;
		if (Args.Num() != 1 || !LexTryParseString(OneBased, *Args[0]) || OneBased < 1 || OneBased > Maximum)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("参数无效。用法：%s。"), Hint);
			return false;
		}
		OutIndex = OneBased - 1;
		return true;
	}

	/** 自定义房间组复用正式选牌控件，并按控件实例管理输入占用。 */
	bool PresentRoomGroupPicker(ACastlePlayerController& Player, UCastlePendingRoomReward& Reward, FText& OutReason)
	{
		auto* Viewport = UGameViewportSubsystem::Get();
		if (Player.IsModalInputActive() || !Viewport || !Player.GetWorld()->GetGameViewport())
		{
			OutReason = INVTEXT("当前已有模态界面或缺少游戏视口，无法打开房间选择。");
			return false;
		}
		const auto PickerClass = LoadWidget<UCastleWildcardRoomPickerWidget>(TEXT("WBP_WildcardRoomPicker"));
		auto* Picker = PickerClass ? CreateWidget<UCastleWildcardRoomPickerWidget>(&Player, PickerClass) : nullptr;
		if (!IsValid(Picker))
		{
			OutReason = INVTEXT("无法创建 WBP_WildcardRoomPicker。");
			return false;
		}

		const TWeakObjectPtr<ACastlePlayerController> WeakPlayer(&Player);
		const TWeakObjectPtr<UCastleWildcardRoomPickerWidget> WeakPicker(Picker);
		// 调试发放不收金币；等待选牌回调退出后才卸载控件，以免 NativeDestruct 在事务内再次取消。
		if (!Reward.SetClaimTransaction([](FText&) { return true; },
			[WeakPlayer, WeakPicker](const UCastlePendingRoomReward::EClaimResult Result)
			{
				// 写入失败时，原选牌控件继续显示失败原因并保留当前选择。
				if (Result == UCastlePendingRoomReward::EClaimResult::WriteFailed) { return; }
				if (ACastlePlayerController* Owner = WeakPlayer.Get())
				{
					Owner->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(Owner, [Owner, WeakPicker]
					{
						if (UCastleWildcardRoomPickerWidget* FinishedPicker = WeakPicker.Get())
						{
							Owner->SetPresentationModalState(false, FinishedPicker);
							FinishedPicker->RemoveFromParent();
						}
					}));
				}
			}, OutReason)
			|| !Picker->BindReward(&Reward, OutReason) || !Picker->BeginBoundReward(OutReason))
		{
			return false;
		}
		FGameViewportWidgetSlot PickerSlot;
		PickerSlot.ZOrder = 57;
		if (!Viewport->AddWidget(Picker, PickerSlot))
		{
			OutReason = INVTEXT("WBP_WildcardRoomPicker 无法挂载到游戏视口。");
			return false;
		}
		Player.SetPresentationModalState(true, Picker);
		return true;
	}

	void GrantGroup(const TArray<FString>& Args, UWorld* World)
	{
		int32 Index;
		if (!ReadOrdinal(Args, UE_ARRAY_COUNT(RoomGroupNames), Index, TEXT("Castle.Items.Group <1-26>")))
		{
			return;
		}
		const auto* Asset = LoadData<UCastleRoomGroupDataAsset>(TEXT("/Game/MovingCastle/Data/Items/RoomGroups"), RoomGroupNames[Index]);
		if (!Asset) { return; }
		ACastleBoard* Board = FindBoard(World);
		if (!Board)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("房间组 %s 发放失败：需要有效的唯一棋盘及背包。"), *Asset->GetPathName());
			return;
		}
		auto* Player = Cast<ACastlePlayerController>(FindPlayer(World));
		if (!Player)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("房间组 %s 发放需要本地 CastlePlayerController。"), *Asset->GetPathName());
			return;
		}

		// 从本次执行的时间取调试种子，避免消耗正式交易的随机流。
		FRandomStream Random(static_cast<int32>(FPlatformTime::Cycles()));
		UCastleBackpackComponent* Backpack = Board->PrototypeBackpack;
		UCastlePendingRoomReward* Pending = nullptr;
		FText Reason;
		if (!UCastleItemGrantLibrary::TryGrantRoomGroup(Asset, Backpack, Board, Board->BuildingDefinitions, Random, Pending, Reason)
			|| (Pending && !PresentRoomGroupPicker(*Player, *Pending, Reason)))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("房间组 %s 发放失败：%s"), *Asset->GetPathName(), *Reason.ToString());
			Player->ShowDirectorMessage(Reason);
			return;
		}
		if (Pending)
		{
			UE_LOG(LogMoving_Castle, Display, TEXT("房间组 %s 已打开属性选择。"), *Asset->GetPathName());
			return;
		}

		const FText Message = FText::Format(INVTEXT("已发放房间组 {0}。建筑栏 {1} 张，临时栏 {2} 张。"),
			Asset->DisplayName, FText::AsNumber(Backpack->GetItemCount()), FText::AsNumber(Backpack->GetTempItemCount()));
		UE_LOG(LogMoving_Castle, Display, TEXT("%s：%s"), *Asset->GetPathName(), *Message.ToString());
		Player->ShowDirectorMessage(Message);
	}

	void GrantBadge(const TArray<FString>& Args, UWorld* World)
	{
		int32 Index;
		if (!ReadOrdinal(Args, UE_ARRAY_COUNT(BadgeNames), Index, TEXT("Castle.Items.Badge <1-25>")))
		{
			return;
		}
		UCastleBadgeSubsystem* Badges = FindBadges(World);
		const auto* Asset = LoadData<UCastleBadgeDataAsset>(TEXT("/Game/MovingCastle/Data/Items/Badges"), BadgeNames[Index]);
		if (!Badges || !Asset)
		{
			return;
		}
	// 库入口在写入前统一校验正式徽章资产的效果、交易属性、PNG 和 GLB。
		FText Reason;
		bool bReplace = false;
		if (UCastleItemGrantLibrary::TryGrantBadge(Asset, Badges, bReplace, Reason))
		{
			UE_LOG(LogMoving_Castle, Display, TEXT("徽章 %s：%s"), *Asset->GetPathName(),
				bReplace ? TEXT("已进入满槽替换流程。") : TEXT("已发放至徽章栏。"));
		}
	}

	void RemoveCard(const TArray<FString>& Args, UWorld* World)
	{
		int32 Index;
		if (!ReadOrdinal(Args, 10, Index, TEXT("Castle.Items.Remove <1-10>")))
		{
			return;
		}
		if (ACastleBoard* Board = FindBoard(World))
		{
			FCastleBuildingDescriptor Removed;
			FText Reason;
			if (!Board->PrototypeBackpack->TryConsumeItemAt(Index, Removed, Reason))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("移除第 %d 张验收卡失败：%s"), Index + 1, *Reason.ToString());
			}
		}
	}

	void FillBackpack(UWorld* World)
	{
		ACastleBoard* Board = FindBoard(World);
		auto* Room = LoadObject<UCastleBuildingDefinition>(nullptr,
			TEXT("/Game/MovingCastle/Data/Buildings/DA_NormalRoom.DA_NormalRoom"));
		if (!Board || !Room)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("填充验收背包需要棋盘和 DA_NormalRoom。"));
			return;
		}
		auto& Backpack = *Board->PrototypeBackpack;
		while (Backpack.HasFreeSlots())
		{
			FText Reason;
			int32 Index;
			if (!Backpack.TryAddBuilding(Room, 1, ECastleTileColor::Red, 4, Index, Reason))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("填充验收背包失败：%s"), *Reason.ToString());
				return;
			}
		}
		UE_LOG(LogMoving_Castle, Display, TEXT("验收背包建筑栏 %d 张，临时栏 %d 张。"),
			Backpack.GetItemCount(), Backpack.GetTempItemCount());
	}

	void ResetBadges(UWorld* World)
	{
		if (UCastleBadgeSubsystem* Badges = FindBadges(World))
		{
			Badges->ClearAllBadgesForTesting();
		}
	}

	void Reload(UWorld* World)
	{
		if (IsValid(World) && World->IsGameWorld())
		{
			UGameplayStatics::OpenLevel(World, FName(*UGameplayStatics::GetCurrentLevelName(World, true)));
		}
		else
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("重载验收需要有效游戏世界。"));
		}
	}

	FAutoConsoleCommandWithWorld MountCommand(TEXT("Castle.Items.UI"), TEXT("挂载道具验收的临时栏与徽章界面。"),
		FConsoleCommandWithWorldDelegate::CreateStatic(&MountHUD));
	FAutoConsoleCommandWithWorld FillCommand(TEXT("Castle.Items.Fill"), TEXT("用红色数字 4 普通房填满建筑栏。"),
		FConsoleCommandWithWorldDelegate::CreateStatic(&FillBackpack));
	FAutoConsoleCommandWithWorldAndArgs GroupCommand(TEXT("Castle.Items.Group"), TEXT("从房间组目录发放编号 1 至 26 的房间组。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GrantGroup));
	FAutoConsoleCommandWithWorldAndArgs BadgeCommand(TEXT("Castle.Items.Badge"), TEXT("从徽章目录发放编号 1 至 25 的徽章。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GrantBadge));
	FAutoConsoleCommandWithWorldAndArgs RemovalCommand(TEXT("Castle.Items.Remove"), TEXT("移除第 1 至 10 个建筑栏槽位。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RemoveCard));
	FAutoConsoleCommandWithWorld ResetBadgesCommand(TEXT("Castle.Items.ResetBadges"), TEXT("清掉当前 GameInstance 的徽章。"),
		FConsoleCommandWithWorldDelegate::CreateStatic(&ResetBadges));
	FAutoConsoleCommandWithWorld ReloadCommand(TEXT("Castle.Items.Reload"), TEXT("重载当前地图来验收徽章跨局继承。"),
		FConsoleCommandWithWorldDelegate::CreateStatic(&Reload));
}

#endif
