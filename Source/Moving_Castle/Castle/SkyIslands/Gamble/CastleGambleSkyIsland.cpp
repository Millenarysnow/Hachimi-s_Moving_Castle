#include "CastleGambleSkyIsland.h"

#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleEconomyComponent.h"
#include "CastleMainTowerComponent.h"
#include "CastlePlayerController.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Items/CastleBadgeDataAsset.h"
#include "Items/CastleBadgeSubsystem.h"
#include "Items/CastleItemGrantLibrary.h"
#include "Items/CastleRoomGroupDataAsset.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float SpinDuration = 1.5f;
	const FVector MiniOrigin(-100.0, -315.0, 107.0);
	const FLinearColor WheelColors[] = {{0.015f, 0.014f, 0.022f}, {0.86f, 0.045f, 0.055f}, {0.06f, 0.66f, 0.19f}};

	FText GetColorName(const ECastleRouletteColor Color)
	{
		switch (Color)
		{
		case ECastleRouletteColor::Black: return INVTEXT("黑色");
		case ECastleRouletteColor::Red: return INVTEXT("红色");
		case ECastleRouletteColor::Green: return INVTEXT("绿色");
		}
		checkNoEntry();
		return FText::GetEmpty();
	}

	/** 黑红成对交错，先放绿色格，再放配对后剩余的黑格和红格。 */
	int32 WheelIndex(const UCastleGambleSkyIslandDataAsset& Config, const ECastleRouletteColor Color, const int32 Slot)
	{
		if (Color == ECastleRouletteColor::Green) { return Slot; }
		const int32 Paired = FMath::Min(Config.RouletteBlackSlots, Config.RouletteRedSlots);
		if (Slot < Paired) { return Config.RouletteGreenSlots + Slot * 2 + (Color == ECastleRouletteColor::Red); }
		return Config.RouletteGreenSlots + Paired + Slot;
	}

	double SampleUnit(FRandomStream& Random)
	{
		return static_cast<double>(Random.GetUnsignedInt()) / 4294967296.0;
	}
}

FText UCastleGambleMachineComponent::GetInteractPrompt() const
{
	switch (Machine)
	{
	case ECastleGambleMachine::Roulette: return INVTEXT("打开轮盘赌桌");
	case ECastleGambleMachine::Slot: return INVTEXT("投入金币并拉动老虎机");
	case ECastleGambleMachine::Recycler: return INVTEXT("回收一个主塔气球");
	}
	checkNoEntry();
	return FText::GetEmpty();
}

bool UCastleGambleMachineComponent::CanInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const
{
	const auto* Island = Cast<ACastleGambleSkyIsland>(GetOwner());
	if (!IsValid(Island) || Context.Hit.GetComponent() != this)
	{
		OutReason = INVTEXT("请瞄准当前赌博空岛的机台。");
		UE_LOG(LogMoving_Castle, Error, TEXT("赌博机台 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	return Island->CanUseMachine(Context, OutReason);
}

bool UCastleGambleMachineComponent::ExecuteInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason)
{
	if (!CanInteract(Context, OutReason)) { return false; }
	auto* Island = CastChecked<ACastleGambleSkyIsland>(GetOwner());
	switch (Machine)
	{
	case ECastleGambleMachine::Roulette: return Island->OpenRoulettePanel(OutReason);
	case ECastleGambleMachine::Slot: return Island->TryPlaySlot(OutReason);
	case ECastleGambleMachine::Recycler: return Island->TryRecycleBalloon(OutReason);
	}
	return Island->Reject(INVTEXT("机台类型无效。"), OutReason);
}

ACastleGambleSkyIsland::ACastleGambleSkyIsland()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	ConfigAsset = TSoftObjectPtr<UCastleGambleSkyIslandDataAsset>(FSoftObjectPath(
		TEXT("/Game/MovingCastle/Data/SkyIslands/DA_GambleSkyIsland.DA_GambleSkyIsland")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Object;
	TintMaterial = Material.Object;
	const auto MakeMachine = [this](const FName Name, ECastleGambleMachine Kind, UStaticMesh* Mesh, FVector Position, FVector Scale)
	{
		auto* Machine = CreateDefaultSubobject<UCastleGambleMachineComponent>(Name);
		Machine->Machine = Kind;
		Machine->SetupAttachment(GetRootComponent());
		Machine->SetStaticMesh(Mesh);
		Machine->SetRelativeLocation(Position);
		Machine->SetRelativeScale3D(Scale);
		Machine->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Machine->SetCollisionResponseToAllChannels(ECR_Ignore);
		Machine->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		return Machine;
	};
	const auto MakeDisplay = [this](const FName Name, USceneComponent* Parent, FVector Position, FVector Scale)
	{
		auto* Display = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Display->SetupAttachment(Parent);
		Display->SetStaticMesh(CubeMesh);
		Display->SetRelativeLocation(Position);
		Display->SetRelativeScale3D(Scale);
		Display->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Display;
	};
	const auto MakeLabel = [this](const FName Name, const TCHAR* Text, FVector Position, float Size)
	{
		auto* Label = CreateDefaultSubobject<UTextRenderComponent>(Name);
		Label->SetupAttachment(GetRootComponent());
		Label->SetRelativeLocation(Position);
		Label->SetRelativeRotation(FRotator(0, 180, 0));
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetWorldSize(Size);
		Label->SetText(FText::FromString(Text));
		return Label;
	};

	// 三台机台从到达点前方展开，平台中央留出通往离开传送门的航线。
	Roulette = MakeMachine(TEXT("Roulette"), ECastleGambleMachine::Roulette, Cylinder.Object, {-80, -550, 66}, {2.8, 2.8, 1.3});
	Slot = MakeMachine(TEXT("SlotMachine"), ECastleGambleMachine::Slot, CubeMesh, {-10, 10, 114}, {1.85, 1.55, 2.25});
	Recycler = MakeMachine(TEXT("BalloonRecycler"), ECastleGambleMachine::Recycler, CubeMesh, {-40, 530, 116}, {1.65, 1.7, 2.3});
	MakeLabel(TEXT("RouletteSign"), TEXT("ROULETTE"), {-120, -550, 250}, 36);
	MakeLabel(TEXT("SlotSign"), TEXT("SLOT MACHINE"), {-50, 10, 295}, 33);
	MakeLabel(TEXT("RecyclerSign"), TEXT("BALLOON RECYCLER"), {-70, 530, 295}, 32);
	SlotResultLabel = MakeLabel(TEXT("SlotResult"), TEXT("READY"), {-106, 10, 155}, 24);
	RecyclerResultLabel = MakeLabel(TEXT("RecyclerResult"), TEXT("READY"), {-126, 530, 155}, 24);
	MiniBoardSurface = MakeDisplay(TEXT("MiniBoardSurface"), GetRootComponent(), MiniOrigin - FVector(0, 0, 6), {1.65, 1.65, 0.12});
	MakeLabel(TEXT("MiniBoardSign"), TEXT("ROOM BETS"), {-120, -315, 198}, 23);
	WheelCenter = CreateDefaultSubobject<USceneComponent>(TEXT("WheelCenter"));
	WheelCenter->SetupAttachment(GetRootComponent());
	WheelCenter->SetRelativeLocation(FVector(-80, -550, 141));
	RoulettePointer = MakeDisplay(TEXT("RoulettePointer"), WheelCenter, {57, 0, 13}, {1.02, 0.09, 0.12});
	for (const FName Name : {FName(TEXT("BlackStripes")), FName(TEXT("RedStripes")), FName(TEXT("GreenStripes"))})
	{
		auto* Stripes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Stripes->SetupAttachment(GetRootComponent());
		Stripes->SetRelativeLocation(FVector(-80, -550, 141));
		Stripes->SetStaticMesh(CubeMesh);
		Stripes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WheelStripes.Add(Stripes);
	}
}

bool ACastleGambleSkyIsland::OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutReason)
{
	Config = ConfigAsset.LoadSynchronous();
	if (!IsValid(Config))
	{
		return Reject(FText::Format(INVTEXT("缺少赌博空岛配置 {0}。"), FText::FromString(ConfigAsset.ToString())), OutReason);
	}
	if (!Config->Validate(OutReason)) { return false; }
	if (Context.Type != ECastleSkyIslandType::Gamble || !CubeMesh || !TintMaterial || !Roulette->GetStaticMesh())
	{
		return Reject(INVTEXT("赌博岛类型、占位网格或着色材质无效。"), OutReason);
	}
	// 从本局棋盘接入同一背包，房间组的候选定义由赌博岛配置显式提供。
	ACastleBoard* Board = nullptr;
	for (TActorIterator<ACastleBoard> It(GetWorld()); It; ++It)
	{
		if (Board) { return Reject(INVTEXT("本世界存在多个棋盘，无法确定赌博空岛的背包来源。"), OutReason); }
		Board = *It;
	}
	auto* Towers = GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>();
	Tower = Towers ? Towers->GetRegisteredMainTower() : nullptr;
	Economy = Context.Controller->GetEconomy();
	Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	if (!IsValid(Board) || !IsValid(Board->PrototypeBackpack) || !IsValid(Tower) || !IsValid(Economy) || !IsValid(Badges))
	{
		return Reject(INVTEXT("赌博空岛需要已初始化的棋盘、背包、主塔、金币账户和徽章子系统。"), OutReason);
	}
	Backpack = Board->PrototypeBackpack;
	MiniCellSize = Board->CellSize * Config->MiniBoardCellScale;
	if (!FMath::IsFinite(MiniCellSize) || MiniCellSize <= 0) { return Reject(INVTEXT("微缩格的世界尺寸无效。"), OutReason); }

	RouletteRandom.Initialize(Context.Seed);
	SlotRandom.Initialize(static_cast<int32>(static_cast<uint32>(Context.Seed) ^ 0x6f8135e9u));
	SetTint(*Roulette, {0.12f, 0.07f, 0.11f});
	SetTint(*Slot, {0.31f, 0.085f, 0.53f});
	SetTint(*Recycler, {0.05f, 0.38f, 0.49f});
	SetTint(*MiniBoardSurface, {0.065f, 0.11f, 0.17f});
	SetTint(*RoulettePointer, {1.0f, 0.78f, 0.19f});
	BuildWheel();
	return true;
}

bool ACastleGambleSkyIsland::CanUseMachine(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (Context.Island != this || !IsValid(Context.Controller) || Context.Controller != GetIslandContext().Controller.Get()
		|| !IsValid(Config))
	{
		return Reject(INVTEXT("交互必须来自当前赌博空岛绑定的玩家。"), OutReason);
	}
	if (bTransacting || Context.Controller->IsMoveInputIgnored() || Context.Controller->IsLookInputIgnored())
	{
		OutReason = INVTEXT("请先关闭当前窗口或等待本次结算结束。");
		UE_LOG(LogMoving_Castle, Warning, TEXT("赌博空岛 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	return true;
}

bool ACastleGambleSkyIsland::Reject(const FText& Reason, FText& OutReason) const
{
	OutReason = Reason;
	UE_LOG(LogMoving_Castle, Error, TEXT("赌博空岛 %s：%s"), *GetPathName(), *Reason.ToString());
	return false;
}

void ACastleGambleSkyIsland::ShowResult(const FText& Result) const
{
	UE_LOG(LogMoving_Castle, Display, TEXT("赌博空岛 %s：%s"), *GetPathName(), *Result.ToString());
	if (ACastlePlayerController* Controller = GetIslandContext().Controller.Get()) { Controller->ShowDirectorMessage(Result); }
}

bool ACastleGambleSkyIsland::OpenRoulettePanel(FText& OutReason)
{
	// 第二期提供界面，核心结算可独立调用，机台明确报告面板尚未安装。
	return Reject(INVTEXT("轮盘面板尚未安装。"), OutReason);
}
bool ACastleGambleSkyIsland::TryPlaceCard(const int32 BackpackIndex, const FIntVector& Cell, FText& OutReason)
{
	if (bTransacting || IsRouletteSpinning() || !IsValid(Backpack))
	{
		return Reject(INVTEXT("轮盘正在结算或背包失效，无法放牌。"), OutReason);
	}
	TGuardValue<bool> Transaction(bTransacting, true);
	if (!MiniBoard.TryPlace(*Backpack, BackpackIndex, Cell, OutReason))
	{
		UE_LOG(LogMoving_Castle, Warning, TEXT("赌博空岛 %s 拒绝微缩放牌：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	DrawMiniCard(MiniBoard.GetPlacements().Last());
	UE_LOG(LogMoving_Castle, Display, TEXT("赌博空岛 %s 下注区放入房间：%s。"), *GetPathName(), *Cell.ToString());
	return true;
}

bool ACastleGambleSkyIsland::TryReturnLastCard(FText& OutReason)
{
	if (bTransacting || IsRouletteSpinning() || !IsValid(Backpack))
	{
		return Reject(INVTEXT("轮盘正在结算或背包失效，无法撤牌。"), OutReason);
	}
	TGuardValue<bool> Transaction(bTransacting, true);
	if (!MiniBoard.TryReturnLast(*Backpack, OutReason)) { return false; }
	RemoveLastMiniCard();
	UE_LOG(LogMoving_Castle, Display, TEXT("赌博空岛 %s 已撤回最后一张房间卡。"), *GetPathName());
	return true;
}

bool ACastleGambleSkyIsland::TryReturnAllCards(FText& OutReason)
{
	if (MiniBoard.GetPlacements().IsEmpty()) { return true; }
	if (bTransacting || !IsValid(Backpack))
	{
		return Reject(INVTEXT("未下注房间无法退回，背包失效或结算仍在进行。"), OutReason);
	}
	TGuardValue<bool> Transaction(bTransacting, true);
	const int32 Count = MiniBoard.GetPlacements().Num();
	if (!MiniBoard.TrySettle(*Backpack, 1, OutReason)) { return Reject(OutReason, OutReason); }
	ClearMiniCards();
	UE_LOG(LogMoving_Castle, Display, TEXT("赌博空岛 %s 已原样退回 %d 张未下注房间卡。"), *GetPathName(), Count);
	return true;
}

bool ACastleGambleSkyIsland::TrySpinRoulette(const ECastleRouletteColor Bet, FText& OutReason)
{
	if (bTransacting || IsRouletteSpinning() || !IsValid(Config) || !IsValid(Backpack) || !IsValid(Badges))
	{
		return Reject(INVTEXT("轮盘正在运行，或本局轮盘配置、背包、徽章源失效。"), OutReason);
	}
	const int32 Count = MiniBoard.GetPlacements().Num();
	if (Count == 0)
	{
		OutReason = INVTEXT("请先放入至少一张房间卡再开转。");
		return false;
	}
	TGuardValue<bool> Transaction(bTransacting, true);
	const int32 Multiplier = Bet == ECastleRouletteColor::Green ? Config->RouletteGreenMultiplier : Config->RouletteRedBlackMultiplier;
	if (!MiniBoard.CanSettle(Multiplier, OutReason)) { return Reject(OutReason, OutReason); }
	FRandomStream Candidate = RouletteRandom;
	ECastleRouletteColor Color;
	int32 ColorSlot = 0;
	const double Luck = FMath::Max(0.0, Badges->GetGambleLuckBonus());
	if (!Config->TryRollRoulette(Bet, Luck, SampleUnit(Candidate), Color, ColorSlot))
	{
		return Reject(INVTEXT("轮盘下注颜色、幸运值或权重无效。"), OutReason);
	}

	// 玩法在动画前原子提交，关闭面板或离岛只能结束展示，不能改变已开奖的结果。
	const bool bWon = Color == Bet;
	if (!MiniBoard.TrySettle(*Backpack, bWon ? Multiplier : 0, OutReason)) { return Reject(OutReason, OutReason); }
	RouletteRandom = Candidate;
	ClearMiniCards();
	RouletteResult = bWon
		? FText::Format(INVTEXT("开出{0}，押中！{1} 张房间卡已回到背包或临时栏。"), GetColorName(Color), FText::AsNumber(Count * Multiplier))
		: FText::Format(INVTEXT("开出{0}，未押中。已销毁 {1} 张下注房间卡。"), GetColorName(Color), FText::AsNumber(Count));
	const double Step = 360.0 / (Config->RouletteBlackSlots + Config->RouletteRedSlots + Config->RouletteGreenSlots);
	SpinStartYaw = WheelCenter->GetRelativeRotation().Yaw;
	SpinTargetYaw = 1440.0 + Step * WheelIndex(*Config, Color, ColorSlot);
	SpinTimeRemaining = SpinDuration;
	SetActorTickEnabled(true);
	return true;
}

void ACastleGambleSkyIsland::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SpinTimeRemaining = FMath::Max(0.0f, SpinTimeRemaining - DeltaSeconds);
	const double Progress = 1.0 - SpinTimeRemaining / SpinDuration;
	const double Ease = 1.0 - FMath::Pow(1.0 - Progress, 3.0);
	WheelCenter->SetRelativeRotation(FRotator(0, FMath::Lerp(SpinStartYaw, SpinTargetYaw, Ease), 0));
	if (!IsRouletteSpinning())
	{
		SetActorTickEnabled(false);
		ShowResult(RouletteResult);
	}
}

void ACastleGambleSkyIsland::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FText Reason;
	ensureAlwaysMsgf(TryReturnAllCards(Reason), TEXT("赌博空岛 %s 离岛退牌失败：%s"), *GetPathName(), *Reason.ToString());
	Super::EndPlay(EndPlayReason);
}

bool ACastleGambleSkyIsland::TryPlaySlot(FText& OutReason)
{
	FCastleBadgeDefinition PendingBadge;
	if (!IsValid(Config) || !IsValid(Economy) || !IsValid(Badges) || !IsValid(Backpack))
	{
		return Reject(INVTEXT("老虎机配置、账户、徽章源或背包失效。"), OutReason);
	}
	if (bTransacting || Badges->TryGetPendingBadge(PendingBadge))
	{
		OutReason = INVTEXT("请先关闭当前奖励窗口。");
		UE_LOG(LogMoving_Castle, Warning, TEXT("赌博空岛 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	if (!Economy->CanSpendGold(Config->SlotMachineCost, OutReason))
	{
		UE_LOG(LogMoving_Castle, Warning, TEXT("赌博空岛 %s 拒绝投币：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	TGuardValue<bool> Transaction(bTransacting, true);
	FRandomStream Candidate = SlotRandom;
	ECastleSlotPrize Prize;
	if (!Config->TryRollSlot(FMath::Max(0.0, Badges->GetGambleLuckBonus()), SampleUnit(Candidate), Prize))
	{
		return Reject(INVTEXT("老虎机幸运值或概率权重无效。"), OutReason);
	}
	FText Summary;
	if (!TrySettleSlot(Prize, Candidate, Summary, OutReason)) { return false; }
	SlotRandom = Candidate;
	const TCHAR* Results[] = {TEXT("- | - | -"), TEXT("2 | 2 | 2"), TEXT("7 | 7 | 7"), TEXT("R | R | R"), TEXT("B | B | B")};
	SlotResultLabel->SetText(FText::FromString(Results[static_cast<uint8>(Prize)]));
	ShowResult(Summary);
	return true;
}

bool ACastleGambleSkyIsland::TrySettleSlot(const ECastleSlotPrize Prize, FRandomStream& Candidate,
	FText& OutSummary, FText& OutReason)
{
	const int32 Cost = Config->SlotMachineCost;
	const UCastleRoomGroupDataAsset* Group = nullptr;
	const UCastleBadgeDataAsset* Badge = nullptr;
	int32 Payout = 0;
	switch (Prize)
	{
	case ECastleSlotPrize::None:
		OutSummary = FText::Format(INVTEXT("老虎机：投入 {0} 金，未中奖。"), FText::AsNumber(Cost));
		break;
	case ECastleSlotPrize::DoubleGold:
	case ECastleSlotPrize::Jackpot:
	{
		const int32 Multiplier = Prize == ECastleSlotPrize::DoubleGold ? 2 : 5;
		const int64 ExpectedPayout = static_cast<int64>(Cost) * Multiplier;
		if (ExpectedPayout > MAX_int32 || static_cast<int64>(Economy->GetGold()) - Cost + ExpectedPayout > MAX_int32)
		{
			return Reject(INVTEXT("老虎机金币奖励超出账户上限，本次未扣款。"), OutReason);
		}
		Payout = static_cast<int32>(ExpectedPayout);
		OutSummary = FText::Format(INVTEXT("老虎机：投入 {0} 金，获得 {1} 金。"), FText::AsNumber(Cost), FText::AsNumber(Payout));
		break;
	}
	case ECastleSlotPrize::RoomGroup:
		if (Config->RoomGroups.IsEmpty()) { return Reject(INVTEXT("老虎机房间组奖励池为空。"), OutReason); }
		Group = Config->RoomGroups[Candidate.RandRange(0, Config->RoomGroups.Num() - 1)];
		if (!IsValid(Group)) { return Reject(INVTEXT("老虎机抽中的房间组资产失效。"), OutReason); }
		OutSummary = FText::Format(INVTEXT("老虎机获得房间组：{0}。"), Group->DisplayName);
		break;
	case ECastleSlotPrize::Badge:
		if (Config->Badges.IsEmpty()) { return Reject(INVTEXT("老虎机徽章奖励池为空。"), OutReason); }
		Badge = Config->Badges[Candidate.RandRange(0, Config->Badges.Num() - 1)];
		if (!IsValid(Badge)) { return Reject(INVTEXT("老虎机抽中的徽章资产失效。"), OutReason); }
		OutSummary = FText::Format(INVTEXT("老虎机获得徽章：{0}。"), Badge->Badge.GetDisplayName());
		break;
	}
	if (!Economy->TrySpendGold(Cost, OutReason)) { return Reject(OutReason, OutReason); }

	// 公共奖励入口失败时撤销本次扣款并返回失败，随机流也由调用方保留在投币前。
	bool bGranted = true;
	if (Group)
	{
		UCastlePendingRoomReward* Pending = nullptr;
		bGranted = UCastleItemGrantLibrary::TryGrantRoomGroup(Group, Backpack, Config->RoomDefinitions, Candidate, Pending, OutReason);
		if (bGranted && Pending) { bGranted = GrantPendingRoomRewardUI(Pending, OutReason); }
	}
	else if (Badge) { bGranted = GrantBadgeWithReplaceUI(Badge->Badge, OutReason); }
	else if (Payout > 0) { bGranted = Economy->TryAddGold(Payout, OutReason); }
	if (!bGranted)
	{
		FText RefundReason;
		ensureAlwaysMsgf(Economy->TryAddGold(Cost, RefundReason), TEXT("赌博空岛 %s 投币回滚失败：%s"),
			*GetPathName(), *RefundReason.ToString());
		return Reject(FText::Format(INVTEXT("老虎机奖励发放失败，奖品 {0}。{1}"),
			FText::FromString(Group ? Group->GetPathName() : GetPathNameSafe(Badge)), OutReason), OutReason);
	}
	return true;
}

bool ACastleGambleSkyIsland::TryRecycleBalloon(FText& OutReason)
{
	if (!IsValid(Config) || !IsValid(Tower) || !IsValid(Economy) || bTransacting)
	{
		return Reject(INVTEXT("气球回收机配置、主塔或金币账户失效，或交易正在进行。"), OutReason);
	}
	// 扣血前验算账户容量，避免可预知的金币溢出使气球扣除后无法支付。
	if (!Economy->CanAddGold(Config->BalloonRecycleGold, OutReason)) { return Reject(OutReason, OutReason); }
	TGuardValue<bool> Transaction(bTransacting, true);
	if (!Tower->TryConsumeBalloons(1, OutReason))
	{
		int32 BalloonCount = 0;
		int32 MaxBalloons = 0;
		Tower->GetBalloonCounts(BalloonCount, MaxBalloons);
		if (BalloonCount != 1 || !Tower->IsAlive()) { return Reject(OutReason, OutReason); }
		OutReason = INVTEXT("至少保留 1 个气球，无法继续回收。");
		RecyclerResultLabel->SetText(INVTEXT("KEEP 1 BALLOON"));
		UE_LOG(LogMoving_Castle, Warning, TEXT("赌博空岛 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	if (!Economy->TryAddGold(Config->BalloonRecycleGold, OutReason)) { return Reject(OutReason, OutReason); }
	RecyclerResultLabel->SetText(FText::FromString(FString::Printf(TEXT("+%d GOLD"), Config->BalloonRecycleGold)));
	ShowResult(FText::Format(INVTEXT("回收 1 个气球，获得 {0} 金。"), FText::AsNumber(Config->BalloonRecycleGold)));
	return true;
}

void ACastleGambleSkyIsland::SetTint(UStaticMeshComponent& Surface, const FLinearColor& Color)
{
	auto* Material = Surface.CreateDynamicMaterialInstance(0, TintMaterial);
	check(Material);
	Material->SetVectorParameterValue(TEXT("Color"), Color);
}

void ACastleGambleSkyIsland::BuildWheel()
{
	const int32 Counts[] = {Config->RouletteBlackSlots, Config->RouletteRedSlots, Config->RouletteGreenSlots};
	const double StepDegrees = 360.0 / (Counts[0] + Counts[1] + Counts[2]);
	const double Width = 2.0 * 91.0 * FMath::Sin(FMath::DegreesToRadians(StepDegrees * 0.5)) / 100.0;
	for (int32 Color = 0; Color < UE_ARRAY_COUNT(Counts); ++Color)
	{
		SetTint(*WheelStripes[Color], WheelColors[Color]);
		for (int32 SlotIndex = 0; SlotIndex < Counts[Color]; ++SlotIndex)
		{
			const FRotator Rotation(0, StepDegrees * WheelIndex(*Config, static_cast<ECastleRouletteColor>(Color), SlotIndex), 0);
			WheelStripes[Color]->AddInstance(FTransform(Rotation, Rotation.Vector() * 91.0, FVector(0.76, Width * 0.9, 0.06)));
		}
	}
}

void ACastleGambleSkyIsland::DrawMiniCard(const FCastleMiniPlacement& Placement)
{
	const FLinearColor Colors[] = {{0.84f, 0.06f, 0.07f}, {0.06f, 0.25f, 0.82f}, {0.05f, 0.61f, 0.18f},
		{0.94f, 0.64f, 0.04f}, {0.54f, 0.12f, 0.77f}};
	const int32 Color = static_cast<int32>(Placement.Card.Color) - static_cast<int32>(ECastleTileColor::Red);
	check(Color >= 0 && Color < UE_ARRAY_COUNT(Colors));
	const FVector Position = MiniOrigin + (FVector(Placement.Cell) + FVector(0, 0, 0.5)) * MiniCellSize;
	auto* Block = NewObject<UStaticMeshComponent>(this);
	Block->SetupAttachment(GetRootComponent());
	Block->SetStaticMesh(CubeMesh);
	Block->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Block->SetRelativeLocation(Position);
	Block->SetRelativeScale3D(FVector(MiniCellSize * 0.92 / 100.0));
	SetTint(*Block, Colors[Color]);
	MiniBlocks.Add(Block);
	Block->RegisterComponent();

	// 在朝向到达点的侧面写实际卡号，并保留桌面俯视时也能看到的彩色方块。
	auto* Label = NewObject<UTextRenderComponent>(this);
	Label->SetupAttachment(GetRootComponent());
	Label->SetRelativeLocation(Position - FVector(MiniCellSize * 0.47, 0, 0));
	Label->SetRelativeRotation(FRotator(0, 180, 0));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(MiniCellSize * 0.53);
	Label->SetText(FText::AsNumber(Placement.Card.Number));
	MiniLabels.Add(Label);
	Label->RegisterComponent();
}

void ACastleGambleSkyIsland::RemoveLastMiniCard()
{
	MiniLabels.Pop(EAllowShrinking::No)->DestroyComponent();
	MiniBlocks.Pop(EAllowShrinking::No)->DestroyComponent();
}

void ACastleGambleSkyIsland::ClearMiniCards()
{
	while (!MiniBlocks.IsEmpty()) { RemoveLastMiniCard(); }
}
