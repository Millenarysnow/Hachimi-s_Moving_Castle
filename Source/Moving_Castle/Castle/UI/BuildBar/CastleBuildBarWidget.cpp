// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildBarWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildBarSlotWidget.h"
#include "CastleBuildingDefinition.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "Moving_Castle.h"
#include "Units/Navigation/Interaction/CastlePatrolRoute.h"

namespace
{
	/** 角标数字不属于卡牌属性；只有定义、等级、颜色和卡牌数字会使相应槽位换牌。 */
	bool IsSameCard(const FCastleBuildingDescriptor& Left, const FCastleBuildingDescriptor& Right)
	{
		return Left.Definition == Right.Definition && Left.Level == Right.Level
			&& Left.Color == Right.Color && Left.Number == Right.Number;
	}
}

void UCastleBuildBarWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bReady = false;

	// 直接读取与原背包 UI 相同的棋盘，不维护第二份库存或选择状态。
	Controller = Cast<ACastlePlayerController>(GetOwningPlayer());
	const ACastleBoard* Board = Cast<ACastleBoard>(UGameplayStatics::GetActorOfClass(this, ACastleBoard::StaticClass()));
	if (!Controller.IsValid() || !IsValid(Board) || !IsValid(Board->PrototypeBackpack))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建造栏 WBP %s 初始化失败：缺少城堡控制器或棋盘背包（棋盘 %s）。"),
			*GetClass()->GetPathName(), *GetNameSafe(Board));
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	Backpack = Board->PrototypeBackpack;
	DisplayedCapacity = INDEX_NONE;
	bReady = ValidatePresentation() && SyncBackpack();
	EnsureModeBindings();
	RefreshScreenVisibility();
}

void UCastleBuildBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bReady)
	{
		return;
	}
	EnsureModeBindings();
	RefreshScreenVisibility();
	if (!SyncBackpack())
	{
		bReady = false;
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UCastleBuildBarWidget::NativeDestruct()
{
	if (UCastleDirectorSubsystem* Director = BoundDirector.Get())
	{
		Director->OnDirectorPhaseChanged.Remove(DirectorPhaseHandle);
		BoundDirector.Reset();
	}
	if (ACastlePatrolRoute* Route = BoundPatrolRoute.Get())
	{
		Route->OnEditingStateChanged.Remove(PatrolEditingHandle);
		BoundPatrolRoute.Reset();
	}
	if (UCastleShopComponent* Shop = BoundShop.Get())
	{
		Shop->OnShopChanged.RemoveDynamic(this, &UCastleBuildBarWidget::HandleShopChanged);
		BoundShop.Reset();
	}
	Super::NativeDestruct();
}

void UCastleBuildBarWidget::EnsureModeBindings()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!BoundDirector.IsValid())
	{
		if (UCastleDirectorSubsystem* Director = World->GetSubsystem<UCastleDirectorSubsystem>())
		{
			BoundDirector = Director;
			DirectorPhaseHandle = Director->OnDirectorPhaseChanged.AddUObject(this, &UCastleBuildBarWidget::HandleDirectorPhaseChanged);
		}
	}
	if (!BoundPatrolRoute.IsValid())
	{
		if (ACastlePatrolRoute* Route = Cast<ACastlePatrolRoute>(UGameplayStatics::GetActorOfClass(World, ACastlePatrolRoute::StaticClass())))
		{
			BoundPatrolRoute = Route;
			PatrolEditingHandle = Route->OnEditingStateChanged.AddUObject(this, &UCastleBuildBarWidget::HandlePatrolEditingChanged);
		}
	}
	if (!BoundShop.IsValid() && Controller.IsValid() && IsValid(Controller->GetShop()))
	{
		BoundShop = Controller->GetShop();
		BoundShop->OnShopChanged.AddUniqueDynamic(this, &UCastleBuildBarWidget::HandleShopChanged);
	}
}

bool UCastleBuildBarWidget::ShouldDisplayInCurrentScreen() const
{
	// 透景房间商店打开时自带 10 格备战席，此时隐藏建造栏避免底部重影。
	if (BoundShop.IsValid() && BoundShop->IsShopOpen())
	{
		return false;
	}
	// 路径编辑界面底部居中留空；建造栏只在主塔建造页和空岛游览期间展示。
	if (BoundPatrolRoute.IsValid() && BoundPatrolRoute->IsEditing())
	{
		return false;
	}
	const UCastleDirectorSubsystem* Director = BoundDirector.IsValid()
		? BoundDirector.Get()
		: (GetWorld() ? GetWorld()->GetSubsystem<UCastleDirectorSubsystem>() : nullptr);
	if (IsValid(Director) && Director->IsRunning())
	{
		const ECastleDirectorPhase Phase = Director->GetPhase();
		return Phase == ECastleDirectorPhase::Editing || Phase == ECastleDirectorPhase::SkyIsland;
	}
	const UCastleCombatSubsystem* Combat = GetWorld() ? GetWorld()->GetSubsystem<UCastleCombatSubsystem>() : nullptr;
	return !(IsValid(Combat) && Combat->IsBattleActive());
}

void UCastleBuildBarWidget::RefreshScreenVisibility()
{
	SetVisibility((bReady && ShouldDisplayInCurrentScreen()) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UCastleBuildBarWidget::HandleDirectorPhaseChanged(const ECastleDirectorPhase NewPhase)
{
	RefreshScreenVisibility();
}

void UCastleBuildBarWidget::HandlePatrolEditingChanged(const bool bIsEditing)
{
	RefreshScreenVisibility();
}

void UCastleBuildBarWidget::HandleShopChanged()
{
	RefreshScreenVisibility();
}

TStaticArray<UCastleBuildBarSlotWidget*, 10> UCastleBuildBarWidget::GetSlots() const
{
	return {Slot01, Slot02, Slot03, Slot04, Slot05, Slot06, Slot07, Slot08, Slot09, Slot10};
}

bool UCastleBuildBarWidget::ValidatePresentation() const
{
	if (!IsValid(WidgetTree) || !IsValid(BuildBarStatus))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建造栏 WBP %s 缺少 WidgetTree 或 BuildBarStatus。"), *GetClass()->GetPathName());
		return false;
	}
	const auto Slots = GetSlots();
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (!IsValid(Slots[Index]) || !Slots[Index]->ValidatePresentation())
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建造栏 WBP %s 的 Slot%02d 缺失或配置无效。"), *GetClass()->GetPathName(), Index + 1);
			return false;
		}

		// 徽章是快捷键提示，不是卡牌数字；与子槽位一样在 WBP 中按固定键位命名。
		const FName BadgeName(*FString::Printf(TEXT("HotkeyBadge%02d"), Index + 1));
		const UImage* Badge = WidgetTree->FindWidget<UImage>(BadgeName);
		if (!IsValid(Badge) || !IsValid(Badge->GetBrush().GetResourceObject()))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建造栏 WBP %s 缺少键位徽章 %s 或其切图。"),
				*GetClass()->GetPathName(), *BadgeName.ToString());
			return false;
		}
	}
	return true;
}

bool UCastleBuildBarWidget::SyncBackpack()
{
	if (!Backpack.IsValid() || !Controller.IsValid())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建造栏 WBP %s 同步失败：棋盘背包或玩家控制器已失效。"), *GetClass()->GetPathName());
		return false;
	}

	const auto Slots = GetSlots();
	const int32 Capacity = Backpack->Capacity;
	const TArray<FCastleBuildingDescriptor> Cards = Backpack->GetItems();
	const int32 Selection = Controller->GetSelectedBackpackIndex();
	if (Capacity < 1 || Capacity > Slots.Num() || Cards.Num() > Capacity)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建造栏 WBP %s 无法显示背包 %s：卡牌 %d、容量 %d、WBP 键位 %d。"),
			*GetClass()->GetPathName(), *Backpack->GetPathName(), Cards.Num(), Capacity, Slots.Num());
		return false;
	}

	const bool bFirstSync = DisplayedCapacity == INDEX_NONE;
	bool bPresentationChanged = bFirstSync || Capacity != DisplayedCapacity || Selection != DisplayedSelection;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FCastleBuildingDescriptor* Card = Cards.IsValidIndex(Index) ? &Cards[Index] : nullptr;
		const bool bHadCard = DisplayedCards.IsValidIndex(Index);
		const bool bCardChanged = bFirstSync || (Card != nullptr) != bHadCard
			|| (Card != nullptr && bHadCard && !IsSameCard(*Card, DisplayedCards[Index]));
		if (bCardChanged && !Slots[Index]->SetCard(Card))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建造栏 WBP %s 同步 Slot%02d 失败。"), *GetClass()->GetPathName(), Index + 1);
			return false;
		}
		if (bCardChanged || Selection != DisplayedSelection)
		{
			Slots[Index]->SetSelected(Selection == Index);
		}
		bPresentationChanged |= bCardChanged;
	}

	if (bPresentationChanged)
	{
		// 消耗、购买或销售后更新容量；没有选中的卡牌时只显示容量，不伪造建筑名称。
		BuildBarStatus->SetText(Cards.IsValidIndex(Selection)
			? FText::Format(NSLOCTEXT("CastleBuildBar", "SelectedStatus", "建造  {0}/{1}   ·   {2}"),
				Cards.Num(), Capacity, Cards[Selection].Definition->DisplayName)
			: FText::Format(NSLOCTEXT("CastleBuildBar", "EmptyStatus", "建造  {0}/{1}"), Cards.Num(), Capacity));
		DisplayedCards = Cards;
		DisplayedCapacity = Capacity;
		DisplayedSelection = Selection;
	}
	return true;
}
