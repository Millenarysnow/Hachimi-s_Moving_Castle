#include "Castle/Tutorial/CastleTutorialDirector.h"

#include "Audio/CastleAudioSubsystem.h"
#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Castle/Dialogue/CastleDialogueScriptDataAsset.h"
#include "Castle/Dialogue/CastleDialogueSubsystem.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Guide/CastleGuideSubsystem.h"
#include "Castle/Tutorial/CastleTutorialConfigDataAsset.h"
#include "Castle/Tutorial/CastleTutorialWidgets.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleEconomyComponent.h"
#include "CastleMainTowerComponent.h"
#include "CastleMassUnitSubsystem.h"
#include "CastleNavigationSubsystem.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "CastleTowerDispatchConfigDataAsset.h"
#include "CastleTowerDispatchSubsystem.h"
#include "Components/WidgetComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Interaction/CastleTowerDispatchDoors.h"
#include "Moving_Castle.h"

namespace
{
	const FIntVector FrontOffset(1, 0, 0);
	const FIntVector RearOffset(-1, 0, 0);
	constexpr int32 TutorialStepCount = 14;

	bool IsSameTutorialCard(const FCastleBuildingDescriptor& A, const FCastleBuildingDescriptor& B)
	{
		return A.Definition == B.Definition && A.Color == B.Color && A.Number == B.Number && A.Level == B.Level;
	}

	// 词条出现的阶段与原剧本一致，编辑器漏配时拒绝进入教学。
	const TMap<int32, TArray<FName>> RequiredTutorialEntries = {
		{1, {TEXT("Building")}}, {6, {TEXT("Demolish")}}, {8, {TEXT("Patrol")}}, {9, {TEXT("Ghost"), TEXT("Archer")}}
	};
}

bool UCastleTutorialConfigDataAsset::ValidateTutorial(FText& OutReason) const
{
	const auto Reject = [&](const TCHAR* Field)
	{
		OutReason = FText::FromString(FString::Printf(TEXT("教程配置 %s 的 %s 字段无效。"), *GetPathName(), Field));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutReason.ToString());
		return false;
	};
	if (Steps.Num() != TutorialStepCount) { return Reject(TEXT("Steps（必须为 14 步）")); }
	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		const auto& Row = Steps[Index];
		if (Row.Number != Index + 1 || Row.Objective.IsEmpty()) { return Reject(TEXT("Steps.Number / Objective")); }
		// S2 是唯一只等待实操的步骤，其余步骤均须有正式剧本。
		if (Row.Number != 2 && !Row.Dialogue) { return Reject(TEXT("Steps.Dialogue")); }
		if (Row.Dialogue && !Row.Dialogue->ValidateScript(OutReason)) { return false; }
		const auto* ExpectedEntries = RequiredTutorialEntries.Find(Row.Number);
		if (ExpectedEntries && Row.GuideEntries != *ExpectedEntries) { return Reject(TEXT("Steps.GuideEntries")); }
	}
	if (StarterRooms.Num() < 5 || StartingGold <= 0) { return Reject(TEXT("StarterRooms / StartingGold")); }
	for (const auto& Room : StarterRooms)
	{
		if (!Room.Validate(OutReason) || Room.GetBuildingType() != ECastleBuildingType::NormalRoom || Room.Level != 1)
		{
			return Reject(TEXT("StarterRooms"));
		}
	}
	if (!Archer.Validate(OutReason) || Archer.GetBuildingType() != ECastleBuildingType::ArcherTower || Archer.Level != 1)
	{
		return Reject(TEXT("Archer"));
	}
	if (!Farm.Validate(OutReason) || Farm.GetBuildingType() != ECastleBuildingType::Farm || Farm.Level != 1)
	{
		return Reject(TEXT("Farm"));
	}
	if (!HUDClass || !ObjectiveClass || !BubbleClass || !RewardClass || !GhostBattle)
	{
		return Reject(TEXT("HUDClass / ObjectiveClass / BubbleClass / RewardClass / GhostBattle"));
	}
	for (const auto* Script : {ClaimDialogue.Get(), OutOfStockDialogue.Get(), FarmOfferDialogue.Get(), ClosingDialogue.Get()})
	{
		if (!Script) { return Reject(TEXT("ClaimDialogue / OutOfStockDialogue / FarmOfferDialogue / ClosingDialogue")); }
		if (!Script->ValidateScript(OutReason)) { return false; }
	}
	return GhostBattle->ValidateConfiguration(OutReason);
}

ACastleTutorialDirector::ACastleTutorialDirector()
{
	// 首个世界 Tick 发生在关卡全部 Actor 的 BeginPlay 后，主塔、控制器及路线均已完成注册。
	PrimaryActorTick.bCanEverTick = true;
}

bool ACastleTutorialDirector::InitializeTutorial(FText& OutReason)
{
	UWorld* World = GetWorld();
	auto* FormalDirector = World->GetSubsystem<UCastleDirectorSubsystem>();
	auto* Instance = World->GetGameInstance();
	auto* Tower = World->GetSubsystem<UCastleMainTowerSubsystem>()->GetRegisteredMainTower();
	Controller = Cast<ACastlePlayerController>(World->GetFirstPlayerController());
	for (TActorIterator<ACastleBoard> It(World); It; ++It)
	{
		if (Board) { OutReason = INVTEXT("教学关卡只能有一个棋盘。"); return false; }
		Board = *It;
	}
	for (TActorIterator<ACastlePatrolRoute> It(World); It; ++It)
	{
		if (Route) { OutReason = INVTEXT("教学关卡只能有一个巡逻路线入口。"); return false; }
		Route = *It;
	}
	if (!Configuration || !Board || !Route || !Controller || !Instance || !Tower || !FormalDirector || FormalDirector->IsRunning())
	{
		OutReason = INVTEXT("缺少教程配置、棋盘、巡逻路线、控制器或主塔，或正式导演已占用教程关卡。");
		return false;
	}
	if (!Configuration->ValidateTutorial(OutReason)) { return false; }
	// 一心对应正式 HUD 的 20 格血条之一，教程受击和主动生命消耗都留在该下限。
	if (!Tower->EnableTutorialHealthLock(OutReason)) { return false; }
	Dialogue = Instance->GetSubsystem<UCastleDialogueSubsystem>();
	Guide = Instance->GetSubsystem<UCastleGuideSubsystem>();
	Combat = World->GetSubsystem<UCastleCombatSubsystem>();
	Navigation = World->GetSubsystem<UCastleNavigationSubsystem>();
	Dispatch = World->GetSubsystem<UCastleTowerDispatchSubsystem>();
	Shop = Controller->GetShop();
	auto* Backpack = Board->PrototypeBackpack.Get();
	if (!Dialogue || !Guide || !Combat || !Navigation || !Dispatch || !Shop || !Backpack
		|| Backpack->GetItemCount() != 0 || !Backpack->HasFreeSlots(Configuration->StarterRooms.Num()))
	{
		OutReason = INVTEXT("教程缺少运行子系统、商店或空白且容量足够的开局建筑栏。");
		return false;
	}
	TArray<FName> GuideIds;
	for (const auto& Row : Configuration->Steps) { GuideIds.Append(Row.GuideEntries); }
	const auto* BubbleProbe = CreateWidget<UCastleTutorialTextWidget>(Controller, Configuration->BubbleClass);
	const auto* RewardProbe = CreateWidget<UCastleTutorialRewardWidget>(Controller, Configuration->RewardClass);
	if (!BubbleProbe || !RewardProbe || !BubbleProbe->HasRequiredBindings() || !RewardProbe->HasRequiredBindings())
	{
		OutReason = FText::Format(INVTEXT("教学配置 {0} 的气泡或奖励界面无法绑定。"),
			FText::FromString(Configuration->GetPathName()));
		return false;
	}
	if (!Guide->BeginTutorial(*Controller, GuideIds, OutReason)) { return false; }
	Route->ConfigureTutorialRoute();
	if (!Route->SetMaxPlayerWaypoints(2) || !Shop->InitializeShop(Controller->GetEconomy(), Board, OutReason)
		|| !Combat->InitializeCombat(Configuration->GhostBattle, OutReason))
	{
		return false;
	}
	HUD = CreateWidget<UCastleDirectorHUDWidget>(Controller, Configuration->HUDClass);
	Objective = CreateWidget<UCastleTutorialTextWidget>(Controller, Configuration->ObjectiveClass);
	if (!HUD || !Objective || !HUD->HasRequiredBindings() || !HUD->BindMainTowerHealth(Tower)
		|| !Objective->SetTutorialText(FText::GetEmpty()) || !HUD->AddToPlayerScreen(10)
		|| !Objective->AddToPlayerScreen(65) || !Controller->MountDirectorItemHUD(Backpack, false, OutReason))
	{
		if (OutReason.IsEmpty()) { OutReason = INVTEXT("正式 HUD、教程提示或道具栏绑定和挂载失败。"); }
		return false;
	}
	HUD->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Objective->SetVisibility(ESlateVisibility::Collapsed);
	Controller->OnBuildingDemolishedToBackpack.AddUObject(this, &ACastleTutorialDirector::HandleDemolished);
	Dispatch->OnNPCDispatched.AddUObject(this, &ACastleTutorialDirector::HandleDispatched);
	Combat->OnBattleCleared.AddUObject(this, &ACastleTutorialDirector::HandleBattleCleared);
	Combat->OnBattleFailed.AddUObject(this, &ACastleTutorialDirector::HandleBattleFailed);
	return EnterStep(1, OutReason);
}

bool ACastleTutorialDirector::ShowDialogue(UCastleDialogueScriptDataAsset* Script, FText& OutReason)
{
	Objective->SetVisibility(ESlateVisibility::Collapsed);
	return Dialogue->PlayScript(Script, Controller, OutReason);
}

bool ACastleTutorialDirector::EnterStep(const int32 Number, FText& OutReason)
{
	Step = Number;
	NextGuide = 0;
	ActiveGuide = NAME_None;
	Objective->SetVisibility(ESlateVisibility::Collapsed);
	const bool bShopStep = Step == 4 || Step == 5 || Step == 13;
	Controller->SetTutorialShopAccess(bShopStep);
	HUD->SetTutorialShopButtonEnabled(bShopStep);

	if (Step == 4)
	{
		if (!Shop->SetScriptedOffers(MakeArrayView(&Configuration->Archer, 1), {}, OutReason)
			|| !Controller->GetEconomy()->TryAddGold(Configuration->StartingGold, OutReason)) { return false; }
	}
	else if (Step == 8 && !SpawnPathMarkers(OutReason)) { return false; }
	else if (Step == 9) { RemovePathMarkers(); }
	else if (Step == 10)
	{
		const auto* DispatchConfig = Dispatch->GetConfiguration();
		if (!DispatchConfig)
		{
			OutReason = INVTEXT("教程主塔没有有效的出兵配置。");
			return false;
		}
		// 单人待命池让城主沿真实路线出门，仍使用正式手动点击派兵入口。
		if (!Dispatch->StartBattleDispatchSession(DispatchConfig->NPCConfiguration, 1, OutReason)
			|| !Combat->StartBattle(ECastleBattleKind::Normal, 1, OutReason)) { return false; }
	}
	else if (Step == 11)
	{
		if (!Dispatch->EndBattleDispatchSession(OutReason)) { return false; }
		Reward = CreateWidget<UCastleTutorialRewardWidget>(Controller, Configuration->RewardClass);
		if (!Reward || !Reward->Configure(*this) || !Reward->AddToPlayerScreen(69))
		{
			OutReason = INVTEXT("教程奖励界面创建、绑定或挂载失败。");
			return false;
		}
		Controller->SetPresentationModalState(true, Reward);
	}
	else if (Step == 13 && !Shop->SetScriptedOffers({}, MakeArrayView(&Configuration->Farm, 1), OutReason))
	{
		return false;
	}

	UE_LOG(LogMoving_Castle, Display, TEXT("教程 %s 进入 S%d。"), *GetPathName(), Step);
	const auto& Row = Configuration->Steps[Step - 1];
	// 教程复用正式局音乐，只有成功进入 S10 战斗步骤时切换战斗曲。
	UCastleAudioSubsystem::SetBGM(GetWorld(), Step == 10 ? ECastleBGMTrack::Battle : ECastleBGMTrack::Build);
	return !Row.Dialogue || ShowDialogue(Row.Dialogue, OutReason);
}

bool ACastleTutorialDirector::UpdatePresentation(FText& OutReason)
{
	if (Dialogue->IsDialoguePlaying()) { return false; }
	const auto& Row = Configuration->Steps[Step - 1];
	if (Guide->HasOpenModal()) { return false; }
	if (!ActiveGuide.IsNone())
	{
		if (!Guide->IsEntryUnlocked(ActiveGuide))
		{
			OutReason = FText::Format(INVTEXT("教学百科 {0} 尚未确认阅读。"), FText::FromName(ActiveGuide));
			return false;
		}
		ActiveGuide = NAME_None;
		++NextGuide;
	}
	if (Row.GuideEntries.IsValidIndex(NextGuide))
	{
		ActiveGuide = Row.GuideEntries[NextGuide];
		if (!Guide->ShowEntryPopup(ActiveGuide, Controller, OutReason)) { return false; }
		return false;
	}
	Objective->SetTutorialText(UCastleDialogueSubsystem::ResolveInputText(Row.Objective, *Controller));
	Objective->SetVisibility(ESlateVisibility::HitTestInvisible);
	return true;
}

void ACastleTutorialDirector::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FText Reason;
	if (Step == 0)
	{
		if (!InitializeTutorial(Reason)) { Fail(Reason); }
		return;
	}
	if (Bubble)
	{
		auto* Units = GetWorld()->GetSubsystem<UCastleMassUnitSubsystem>();
		FCastleMassUnitSnapshot Snapshot;
		if (!Units->IsUnitValid(DispatchedUnit))
		{
			Bubble->DestroyComponent();
			Bubble = nullptr;
		}
		else if (Units->GetUnitSnapshot(DispatchedUnit, Snapshot))
		{
			Bubble->SetWorldLocation(Snapshot.Transform.GetLocation() + FVector(0, 0, 220));
		}
	}
	if (!UpdatePresentation(Reason))
	{
		if (!Reason.IsEmpty()) { Fail(Reason); }
		return;
	}

	bool bAdvance = false;
	const FIntVector Center = Board->GetBoardCenterCoordinate();
	switch (Step)
	{
	case 1:
		// 开场对话和建造说明均已关闭，再交付固定手牌并开启首房目标。
		if (!Board->PrototypeBackpack->TryAddItemsAllowOverflow(Configuration->StarterRooms, Reason)) { Fail(Reason); return; }
		bAdvance = true;
		break;
	case 2:
		if (const auto* Front = Board->GetBuildingAt(Center + FrontOffset))
		{
			bAdvance = Front->IsNormalRoom() && Front->CanReceiveBuildingTrigger();
		}
		break;
	case 3: bAdvance = HasDoorCircuit(); break;
	case 4: bAdvance = Shop->IsShopOpen(); break;
	case 5:
		bAdvance = HasPurchased(Configuration->Archer);
		if (bAdvance) { Shop->CloseShop(); }
		break;
	case 6:
		bAdvance = bRecoveredRoom && HasDoorCircuit() && Board->GetPlacedBuildings().ContainsByPredicate([](const auto* Room)
		{
			return Room->CanReceiveBuildingTrigger() && Room->GetBuildingDescriptor().GetBuildingType() == ECastleBuildingType::ArcherTower
				&& Room->GetBuildingLevel() == 1;
		});
		break;
	case 7: bAdvance = Route->IsEditing(); break;
	case 8: bAdvance = HasRouteWithArcher(); break;
	case 9: bAdvance = true; break;
	case 10: bAdvance = bDispatched && bBattleCleared; break;
	case 11: bAdvance = bCopyClaimed; break;
	case 12:
		bAdvance = Board->GetPlacedBuildings().ContainsByPredicate([](const auto* Room)
		{
			return Room->CanReceiveBuildingTrigger() && Room->GetBuildingLevel() == 2
				&& Room->GetBuildingDescriptor().GetBuildingType() == ECastleBuildingType::ArcherTower;
		});
		break;
	case 13:
		if (ShopLesson == EShopLesson::AwaitingOpen && Shop->IsShopOpen())
		{
			if (!ShowDialogue(Configuration->OutOfStockDialogue, Reason)) { Fail(Reason); return; }
			ShopLesson = EShopLesson::AwaitingRefresh;
		}
		else if (ShopLesson == EShopLesson::AwaitingRefresh && !Shop->GetOffers().IsEmpty())
		{
			if (!ShowDialogue(Configuration->FarmOfferDialogue, Reason)) { Fail(Reason); return; }
			ShopLesson = EShopLesson::AwaitingFarm;
		}
		else if (ShopLesson == EShopLesson::AwaitingFarm && HasPurchased(Configuration->Farm))
		{
			Shop->CloseShop();
			bAdvance = true;
		}
		break;
	case 14:
		if (bClosing)
		{
			bCompleted = true;
			Objective->SetTutorialText(INVTEXT("新手教程完成"));
			SetActorTickEnabled(false);
			UE_LOG(LogMoving_Castle, Display, TEXT("教程 %s 已完成全部 14 步。"), *GetPathName());
		}
		else if (!Route->IsEditing() && IsBuildingOnPatrolPath(ECastleBuildingType::Farm))
		{
			if (!ShowDialogue(Configuration->ClosingDialogue, Reason)) { Fail(Reason); return; }
			bClosing = true;
		}
		break;
	}
	if (bAdvance && !EnterStep(Step + 1, Reason)) { Fail(Reason); }
}

bool ACastleTutorialDirector::HasDoorCircuit() const
{
	const FIntVector Center = Board->GetBoardCenterCoordinate();
	auto* Front = Board->GetBuildingAt(Center + FrontOffset);
	auto* Rear = Board->GetBuildingAt(Center + RearOffset);
	auto* Doors = Board->MainTowerActor->FindComponentByClass<UCastleTowerDispatchDoors>();
	FVector Exit, Entrance;
	FText Reason;
	// 房间 Actor 会先登记再播放飞入。等两端可通行后再查询双门，避免每帧重复报告施工中的门口。
	if (!Doors || !Front || !Rear || !Front->CanReceiveBuildingTrigger() || !Rear->CanReceiveBuildingTrigger()
		|| !Doors->TryGetDoorWaypoints(Exit, Entrance, Reason)
		|| !Navigation->IsNavigationReady()) { return false; }

	// 逐间按实际门洞可通行的连接搜索，排除通过主塔内部或不可通行邻格直接抄近路。
	TArray<ACastleBuildingBase*> Pending{Front};
	TSet<ACastleBuildingBase*> Seen{Front};
	for (int32 Index = 0; Index < Pending.Num(); ++Index)
	{
		if (Pending[Index] == Rear) { return true; }
		TArray<ACastleBuildingBase*> Neighbors;
		if (!Board->GetFaceAdjacentBuildings(Pending[Index], true, Neighbors)) { return false; }
		for (auto* Room : Neighbors)
		{
			if (Seen.Contains(Room)) { continue; }
			Seen.Add(Room);
			Pending.Add(Room);
		}
	}
	return false;
}

bool ACastleTutorialDirector::IsBuildingOnPatrolPath(const ECastleBuildingType Type) const
{
	const auto Path = Navigation->AcquireSharedPath();
	if (!Path || !Path->bValid || !Path->bTowerDispatch) { return false; }
	for (const auto* Room : Board->GetPlacedBuildings())
	{
		if (!Room->CanReceiveBuildingTrigger() || Room->GetBuildingDescriptor().GetBuildingType() != Type) { continue; }
		// 使用已发布路线的贴地采样与真实占格比较，排除同一水平坐标的其他楼层。
		const FTransform BoardTransform = Board->GetActorTransform();
		for (const FIntVector& Cell : Room->GetOccupiedCoordinates())
		{
			const FVector CellCenter = BoardTransform.InverseTransformPosition(Board->GetGridWorldLocation(Cell));
			for (const FVector& Point : Path->PatrolPoints)
			{
				const FVector Delta = BoardTransform.InverseTransformPosition(Point) - CellCenter;
				if (FMath::Abs(Delta.X) < Board->CellSize * 0.5 && FMath::Abs(Delta.Y) < Board->CellSize * 0.5
					&& FMath::Abs(Delta.Z) <= Board->LayerHeight * 0.5 + 20.0) { return true; }
			}
		}
	}
	return false;
}

bool ACastleTutorialDirector::HasRouteWithArcher() const
{
	return !Route->IsEditing() && Navigation->GetRequestedWaypoints().Num() == 2
		&& IsBuildingOnPatrolPath(ECastleBuildingType::ArcherTower);
}

bool ACastleTutorialDirector::HasPurchased(const FCastleBuildingDescriptor& Descriptor) const
{
	// 售罄状态与建筑栏两处均来自正式交易提交，已有同类建筑不会被误认成购买成功。
	return Shop->GetOffers().ContainsByPredicate([&](const auto& Offer) { return Offer.bSold && IsSameTutorialCard(Descriptor, Offer.Descriptor); })
		&& Board->PrototypeBackpack->GetItems().ContainsByPredicate([&](const auto& Card) { return IsSameTutorialCard(Descriptor, Card); });
}

bool ACastleTutorialDirector::SpawnPathMarkers(FText& OutReason)
{
	const auto Rooms = Board->GetPlacedBuildings();
	auto* const* Archer = Rooms.FindByPredicate([](const auto* Room)
	{
		return Room->CanReceiveBuildingTrigger() && Room->GetBuildingDescriptor().GetBuildingType() == ECastleBuildingType::ArcherTower;
	});
	auto* Rear = Board->GetBuildingAt(Board->GetBoardCenterCoordinate() + RearOffset);
	// 两个目标必须落在不同的房间，箭塔若恰好在后门，第一点提示改为正门入口。
	const bool bArcherAtRear = Archer && *Archer == Rear;
	const ACastleBuildingBase* Targets[] = {
		bArcherAtRear ? Board->GetBuildingAt(Board->GetBoardCenterCoordinate() + FrontOffset) : (Archer ? *Archer : nullptr),
		Rear
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Targets); ++Index)
	{
		if (!Targets[Index])
		{
			OutReason = INVTEXT("教程无法确定两座用于路线标记的建筑。");
			return false;
		}
		auto* Marker = NewObject<UWidgetComponent>(this);
		Marker->SetWidgetSpace(EWidgetSpace::Screen);
		Marker->SetWidgetClass(Configuration->BubbleClass);
		Marker->SetDrawSize(FVector2D(230, 48));
		Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Marker->RegisterComponent();
		Marker->InitWidget();
		PathMarkers.Add(Marker);
		auto* Text = Cast<UCastleTutorialTextWidget>(Marker->GetUserWidgetObject());
		if (!Text || !Text->SetTutorialText(FText::Format(INVTEXT("路径点 {0}"), FText::AsNumber(Index + 1))))
		{
			OutReason = INVTEXT("教程路径目标的文字控件无法绑定。");
			return false;
		}
		Marker->SetWorldLocation(Targets[Index]->GetActorLocation() + FVector(0, 0, Board->LayerHeight + 120.0));
	}
	return true;
}

void ACastleTutorialDirector::RemovePathMarkers()
{
	for (UWidgetComponent* Marker : PathMarkers) { Marker->DestroyComponent(); }
	PathMarkers.Reset();
}

void ACastleTutorialDirector::HandleDemolished(const FCastleBuildingDescriptor& Descriptor)
{
	if (Step == 6 && Descriptor.GetBuildingType() == ECastleBuildingType::NormalRoom) { bRecoveredRoom = true; }
}

void ACastleTutorialDirector::HandleDispatched(const FCastleMassUnitHandle& Unit)
{
	if (Step != 10) { return; }
	// 实体本身由 Mass 管理，气泡作为无碰撞屏幕组件跟随已提交实体的快照。
	if (Bubble) { Bubble->DestroyComponent(); }
	Bubble = NewObject<UWidgetComponent>(this);
	Bubble->SetWidgetSpace(EWidgetSpace::Screen);
	Bubble->SetWidgetClass(Configuration->BubbleClass);
	Bubble->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bubble->SetDrawSize(FVector2D(180, 44));
	Bubble->RegisterComponent();
	Bubble->InitWidget();
	auto* Text = Cast<UCastleTutorialTextWidget>(Bubble->GetUserWidgetObject());
	if (!Text || !Text->SetTutorialText(INVTEXT("呜呜呜...")))
	{
		Fail(INVTEXT("城主气泡的文字控件无法绑定。"));
		return;
	}
	// 只有气泡的文字绑定成功，才从共享语音通道播放城主发言。
	if (auto* Audio = UCastleAudioSubsystem::GetForWorld(GetWorld())) { Audio->PlayTutorialCryBubbleVoice(); }
	DispatchedUnit = Unit;
	// 首次派兵由玩家触发。之后使用正式待命池循环巡逻，清场时结束出兵会话。
	if (!bDispatched && !Dispatch->SetAutoDispatchEnabled(true))
	{
		Fail(INVTEXT("教程无法开启城主循环巡逻。"));
		return;
	}
	bDispatched = true;
}

void ACastleTutorialDirector::HandleBattleCleared(ECastleBattleKind Kind, int32 Ordinal)
{
	if (Step == 10 && Kind == ECastleBattleKind::Normal && Ordinal == 1)
	{
		bBattleCleared = true;
		// S10 仅在战斗清场事件已提交后恢复建造曲与胜利提示。
		UCastleAudioSubsystem::SetBGM(GetWorld(), ECastleBGMTrack::Build);
		UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Victory);
	}
}

void ACastleTutorialDirector::HandleBattleFailed(ECastleBattleKind, int32, const FText& Reason)
{
	Fail(Reason);
}

void ACastleTutorialDirector::ChooseCopyReward()
{
	if (Step != 11 || bCopyChosen || Dialogue->IsDialoguePlaying()) { return; }
	FText Reason;
	if (!Reward->ShowCard(Configuration->Archer, *Board) || !ShowDialogue(Configuration->ClaimDialogue, Reason))
	{
		Fail(Reason.IsEmpty() ? INVTEXT("无法显示复制的箭塔卡。") : Reason);
		return;
	}
	bCopyChosen = true;
}

void ACastleTutorialDirector::ClaimCopyReward()
{
	if (Step != 11 || !bCopyChosen || bCopyClaimed || Dialogue->IsDialoguePlaying()) { return; }
	int32 Index = INDEX_NONE;
	FText Reason;
	if (!Board->PrototypeBackpack->TryAddItem(Configuration->Archer, Index, Reason))
	{
		Controller->ShowDirectorMessage(Reason);
		return;
	}
	bCopyClaimed = true;
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::Reward);
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::ClosePanel);
	Controller->SetPresentationModalState(false, Reward);
	Reward->RemoveFromParent();
	Reward = nullptr;
}

void ACastleTutorialDirector::Fail(const FText& Reason)
{
	EndTutorialDispatch();
	const FText Failure = Reason.IsEmpty() ? INVTEXT("必需运行状态初始化失败。") : Reason;
	UE_LOG(LogMoving_Castle, Error, TEXT("教程 %s（配置 %s，S%d）停止：%s"),
		*GetPathName(), *GetPathNameSafe(Configuration), Step, *Failure.ToString());
	if (Controller)
	{
		Controller->SetTutorialShopAccess(false);
		Controller->ShowDirectorMessage(Failure);
	}
	SetActorTickEnabled(false);
}

void ACastleTutorialDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	// 即使停留在没有对话窗口的派出步骤，切图也会停止城主气泡。
	if (auto* Audio = UCastleAudioSubsystem::GetForWorld(GetWorld())) { Audio->StopDialogueVoice(); }
	// 订阅与界面跟随本关卡销毁，GameInstance 不保留旧教程 Actor。
	if (Controller) { Controller->OnBuildingDemolishedToBackpack.RemoveAll(this); }
	if (Dispatch) { Dispatch->OnNPCDispatched.RemoveAll(this); }
	EndTutorialDispatch();
	if (Combat)
	{
		Combat->OnBattleCleared.RemoveAll(this);
		Combat->OnBattleFailed.RemoveAll(this);
	}
	if (Reward && Controller) { Controller->SetPresentationModalState(false, Reward); }
	for (UUserWidget* Widget : {static_cast<UUserWidget*>(HUD.Get()), static_cast<UUserWidget*>(Objective.Get()),
		static_cast<UUserWidget*>(Reward.Get())})
	{
		if (Widget) { Widget->RemoveFromParent(); }
	}
	RemovePathMarkers();
	Super::EndPlay(Reason);
}

void ACastleTutorialDirector::EndTutorialDispatch()
{
	// 部分开战失败或退出关卡时也销毁已派出的 Mass 实体并释放编辑权限。
	if (Dispatch && Dispatch->IsSessionActive())
	{
		FText Reason;
		if (!Dispatch->EndBattleDispatchSession(Reason))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("教程 %s 无法结束出兵：%s"), *GetPathName(), *Reason.ToString());
		}
	}
}
