#if !UE_BUILD_SHIPPING

#include "Blueprint/WidgetBlueprintLibrary.h"
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
#include "Kismet/GameplayStatics.h"
#include "Moving_Castle.h"
#include "UI/Items/CastleBadgeBarWidget.h"
#include "UI/Items/CastleBadgeReplaceModalWidget.h"
#include "UI/Items/CastleTempBuildBarWidget.h"

namespace CastleItemsDebug
{
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
	T* LoadData(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("/Game/MovingCastle/Data/Items/%s.%s"), Name, Name);
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

	void GrantGroup(const TArray<FString>&, UWorld* World)
	{
		// 保留命令名供 Phase 1D 接入新资产目录，骨架阶段明确拒绝旧模板。
		UE_LOG(LogMoving_Castle, Error, TEXT("世界 %s：Castle.Items.Group 的房间组规则引擎尚未实现（Phase 0）。"),
			*GetPathNameSafe(World));
	}

	void GrantBadge(const TArray<FString>& Args, UWorld* World)
	{
		static const TCHAR* Names[] = {
			TEXT("DA_Badge_BlueRoomNumber"), TEXT("DA_Badge_UpgradeRoomCards"),
			TEXT("DA_Badge_MainTowerHealth"), TEXT("DA_Badge_ArcherFireRate"),
			TEXT("DA_Badge_FirstMainTowerDamage"), TEXT("DA_Badge_LowMainTowerHealth")
		};
		int32 Index;
		if (!ReadOrdinal(Args, UE_ARRAY_COUNT(Names), Index, TEXT("Castle.Items.Badge <1-6>")))
		{
			return;
		}
		UCastleBadgeSubsystem* Badges = FindBadges(World);
		const auto* Asset = LoadData<UCastleBadgeDataAsset>(Names[Index]);
		if (!Badges || !Asset)
		{
			return;
		}
		FText Reason;
		bool bReplace;
		UCastleItemGrantLibrary::TryGrantBadge(Asset, Badges, bReplace, Reason);
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
	FAutoConsoleCommandWithWorldAndArgs GroupCommand(TEXT("Castle.Items.Group"), TEXT("房间组命令的骨架入口，待规则接入。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GrantGroup));
	FAutoConsoleCommandWithWorldAndArgs BadgeCommand(TEXT("Castle.Items.Badge"), TEXT("发放编号 1 至 6 的既有徽章。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GrantBadge));
	FAutoConsoleCommandWithWorldAndArgs RemovalCommand(TEXT("Castle.Items.Remove"), TEXT("移除第 1 至 10 个建筑栏槽位。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RemoveCard));
	FAutoConsoleCommandWithWorld ResetBadgesCommand(TEXT("Castle.Items.ResetBadges"), TEXT("清掉当前 GameInstance 的徽章。"),
		FConsoleCommandWithWorldDelegate::CreateStatic(&ResetBadges));
	FAutoConsoleCommandWithWorld ReloadCommand(TEXT("Castle.Items.Reload"), TEXT("重载当前地图来验收徽章跨局继承。"),
		FConsoleCommandWithWorldDelegate::CreateStatic(&Reload));
}

#endif
