#include "CastleGambleSkyIsland.h"

#include "CastleBackpackComponent.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "CastleEconomyComponent.h"
#include "CastleGambleRouletteWidget.h"
#include "CastleGroundGenerator.h"
#include "CastleGroundTileActor.h"
#include "CastleMainTowerComponent.h"
#include "CastlePlayerController.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
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

	/** 用烹饪后仍存在的节点名解析跨蓝图或原生父组件，打包游戏也能读取造型。 */
	const USceneComponent* FindMiniatureTemplateParent(USCS_Node& Node, const USimpleConstructionScript& Script,
		UBlueprintGeneratedClass& BuildingClass)
	{
		if (const USCS_Node* Parent = Script.FindParentNode(&Node))
		{
			return Cast<USceneComponent>(Parent->GetActualComponentTemplate(&BuildingClass));
		}
		const FName ParentName = Node.ParentComponentOrVariableName;
		if (ParentName.IsNone()) { return nullptr; }
		if (Node.bIsParentComponentNative)
		{
			return Cast<USceneComponent>(BuildingClass.GetDefaultObject<AActor>()->GetDefaultSubobjectByName(ParentName));
		}
		for (auto* Class = &BuildingClass; Class; Class = Cast<UBlueprintGeneratedClass>(Class->GetSuperClass()))
		{
			if (Class->GetFName() != Node.ParentComponentOwnerClassName || !Class->SimpleConstructionScript) { continue; }
			if (const auto* Parent = Class->SimpleConstructionScript->FindSCSNode(ParentName))
			{
				return Cast<USceneComponent>(Parent->GetActualComponentTemplate(&BuildingClass));
			}
		}
		return nullptr;
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
	RoulettePanelClass = TSoftClassPtr<UCastleGambleRouletteWidget>(FSoftObjectPath(
		TEXT("/Game/MovingCastle/UI/SkyIslands/Gamble/WBP_GambleRoulettePanel.WBP_GambleRoulettePanel_C")));
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
	if (!BuildMiniGround(*Board, Context.Seed, OutReason)) { return false; }

	RouletteRandom.Initialize(Context.Seed);
	SlotRandom.Initialize(static_cast<int32>(static_cast<uint32>(Context.Seed) ^ 0x6f8135e9u));
	SetTint(*Roulette, {0.12f, 0.07f, 0.11f});
	SetTint(*Slot, {0.31f, 0.085f, 0.53f});
	SetTint(*Recycler, {0.05f, 0.38f, 0.49f});
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
	if (RoulettePanel) { return Reject(INVTEXT("轮盘面板已经打开。"), OutReason); }
	UClass* Class = RoulettePanelClass.LoadSynchronous();
	ACastlePlayerController* Controller = GetIslandContext().Controller.Get();
	auto* Viewport = UGameViewportSubsystem::Get();
	if (!Class || !Controller || !Viewport)
	{
		return Reject(FText::Format(INVTEXT("缺少轮盘面板 {0}、有效玩家控制器或视口子系统。"), FText::FromString(RoulettePanelClass.ToString())), OutReason);
	}
	auto* Panel = CreateWidget<UCastleGambleRouletteWidget>(Controller, Class);
	if (!Panel) { return Reject(INVTEXT("无法实例化轮盘 WBP。"), OutReason); }
	if (!Panel->BindIsland(this, OutReason)) { return Reject(OutReason, OutReason); }
	// 在加入视口之前完整设置槽位，尺寸由 Border 内部布局决定。
	FGameViewportWidgetSlot ViewportSlot;
	ViewportSlot.Anchors = FAnchors(1.0f, 0.5f);
	ViewportSlot.Alignment = FVector2D(1.0, 0.5);
	ViewportSlot.Offsets = FMargin(-32.0f, 0.0f, 0.0f, 0.0f);
	ViewportSlot.ZOrder = 53;
	Viewport->SetWidgetSlot(Panel, ViewportSlot);
	if (!Viewport->AddWidget(Panel, ViewportSlot))
	{
		Panel->RemoveFromParent();
		return Reject(INVTEXT("无法将轮盘面板加入玩家视口。"), OutReason);
	}
	RoulettePanel = Panel;
	// E 键、视角和鼠标使用控制器同一份输入锁，老虎机只能在面板关闭后通过实际交互开启奖励弹窗。
	Controller->SetDirectorModalState(true, false, Panel);
	return true;
}

bool ACastleGambleSkyIsland::TryCloseRoulettePanel(FText& OutReason)
{
	if (IsRouletteSpinning())
	{
		OutReason = INVTEXT("请等待轮盘停稳后关闭面板。");
		return false;
	}
	// 关闭模态只还回输入，赌注可继续在场景中摆放或撤回。
	RemoveRoulettePanel();
	return true;
}

void ACastleGambleSkyIsland::RemoveRoulettePanel()
{
	if (!RoulettePanel) { return; }
	RoulettePanel->RemoveFromParent();
	RoulettePanel = nullptr;
	if (ACastlePlayerController* Controller = GetIslandContext().Controller.Get())
	{
		Controller->SetDirectorModalState(false, false, nullptr);
	}
}

bool ACastleGambleSkyIsland::TryPlaceCard(const int32 BackpackIndex, const FIntVector& Cell, FText& OutReason)
{
	if (!IsValid(Backpack))
	{
		return Reject(INVTEXT("背包失效，无法放牌。"), OutReason);
	}
	if (bTransacting || IsRouletteSpinning())
	{
		OutReason = INVTEXT("请等待轮盘停稳后放牌。");
		UE_LOG(LogMoving_Castle, Warning, TEXT("赌博空岛 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	TGuardValue<bool> Transaction(bTransacting, true);
	FCastleBuildingDescriptor Card;
	if (!Backpack->TryGetItemAt(BackpackIndex, Card))
	{
		OutReason = INVTEXT("选中的背包槽位没有房间卡。");
		UE_LOG(LogMoving_Castle, Warning, TEXT("赌博空岛 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	if (!MiniBoard.CanPlace(Card, Cell, OutReason))
	{
		UE_LOG(LogMoving_Castle, Warning, TEXT("赌博空岛 %s 拒绝微缩放牌：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	// 完成模型资产检查和表现创建后才消费卡；库存拒绝提交时销毁本次未提交的表现。
	UBoxComponent* Room = CreateMiniCard(Card, Cell, OutReason);
	if (!Room) { return false; }
	MiniRooms.Add(Cell, Room);
	if (!MiniBoard.TryPlace(*Backpack, BackpackIndex, Cell, OutReason))
	{
		RemoveMiniCard(Cell);
		UE_LOG(LogMoving_Castle, Warning, TEXT("赌博空岛 %s 拒绝微缩放牌：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	UE_LOG(LogMoving_Castle, Display, TEXT("赌博空岛 %s 下注区放入房间：%s。"), *GetPathName(), *Cell.ToString());
	return true;
}

bool ACastleGambleSkyIsland::TryReturnLastCard(FText& OutReason)
{
	if (!IsValid(Backpack))
	{
		return Reject(INVTEXT("背包失效，无法撤牌。"), OutReason);
	}
	if (bTransacting || IsRouletteSpinning())
	{
		OutReason = INVTEXT("请等待轮盘停稳后撤牌。");
		UE_LOG(LogMoving_Castle, Warning, TEXT("赌博空岛 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	TGuardValue<bool> Transaction(bTransacting, true);
	const auto Placements = MiniBoard.GetPlacements();
	if (Placements.IsEmpty())
	{
		OutReason = INVTEXT("微缩区没有可撤回的房间卡。");
		return false;
	}
	const FIntVector Cell = Placements.Last().Cell;
	if (!MiniBoard.TryReturnLast(*Backpack, OutReason)) { return false; }
	RemoveMiniCard(Cell);
	UE_LOG(LogMoving_Castle, Display, TEXT("赌博空岛 %s 已撤回最后一张房间卡。"), *GetPathName());
	return true;
}

bool ACastleGambleSkyIsland::TryReturnAllCards(FText& OutReason)
{
	if (bTransacting || IsRouletteSpinning())
	{
		OutReason = INVTEXT("请等待轮盘停稳后退牌。");
		return false;
	}
	if (MiniBoard.GetPlacements().IsEmpty()) { return true; }
	if (!IsValid(Backpack))
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
		UE_LOG(LogMoving_Castle, Warning, TEXT("赌博空岛 %s：%s"), *GetPathName(), *OutReason.ToString());
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
		if (RoulettePanel) { RoulettePanel->FinishSpin(RouletteResult); }
		ShowResult(RouletteResult);
	}
}

void ACastleGambleSkyIsland::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FText Reason;
	// 已开转的牌在开始动画时已结算；停止动画后再返还仍在微缩区的原卡。
	SpinTimeRemaining = 0.0f;
	ensureAlwaysMsgf(TryReturnAllCards(Reason), TEXT("赌博空岛 %s 离岛退牌失败：%s"), *GetPathName(), *Reason.ToString());
	ClearMiniCards();
	ClearMiniGround();
	RemoveRoulettePanel();
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
		if (bGranted && Pending) { bGranted = PresentRoomPrize(Pending, OutReason); }
	}
	else if (Badge) { bGranted = PresentBadgePrize(Badge->Badge, OutReason); }
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

bool ACastleGambleSkyIsland::PresentRoomPrize(UCastlePendingRoomReward* Reward, FText& OutReason)
{
	return GrantPendingRoomRewardUI(Reward, OutReason);
}

bool ACastleGambleSkyIsland::PresentBadgePrize(const FCastleBadgeDefinition& Badge, FText& OutReason)
{
	return GrantBadgeWithReplaceUI(Badge, OutReason);
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

bool ACastleGambleSkyIsland::BuildMiniGround(const ACastleBoard& Board, int32 Seed, FText& OutReason)
{
	const double Scale = Config->MiniBoardCellScale;
	MiniCellSize = Board.CellSize * Scale;
	MiniTileSize = Board.GroundTileSize * Scale;
	MiniLayerHeight = Board.LayerHeight * Scale;
	if (!FMath::IsFinite(MiniCellSize) || !FMath::IsFinite(MiniTileSize) || !FMath::IsFinite(MiniLayerHeight)
		|| MiniCellSize <= 0.0 || MiniTileSize <= 0.0 || MiniTileSize > MiniCellSize || MiniLayerHeight <= 0.0)
	{
		return Reject(INVTEXT("主棋盘的格距、地面边长或层高无法用于微缩棋盘。"), OutReason);
	}
	const FIntPoint Size = Config->MiniBoardGridSize;
	// 以轮盘后方、中央通道左侧的两个近边界定位；增大格数只向远离机台的方向扩展。
	MiniOrigin = FVector(100.0 + MiniTileSize * 0.5, -275.0 - MiniTileSize * 0.5 - (Size.Y - 1) * MiniCellSize,
		12.0 * Scale + 0.1);
	FCastleGroundGenerationRequest Request;
	Request.GridSize = FIntVector(Size.X, Size.Y, 1);
	Request.Seed = Seed;
	const auto Generated = GetDefault<UCastleRandomGroundGenerator>()->GenerateGround(Request);
	if (!MiniBoard.InitializeGround(Generated.Cells, Size, OutReason)) { return Reject(OutReason, OutReason); }

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FCastleGroundCell& Cell : Generated.Cells)
	{
		// 原地块厚 12 cm。出生点下移半厚，使实际顶面正好是零层建筑基底。
		const FTransform Local(FQuat::Identity, GetMiniCellBase(Cell.Coordinate) - FVector::UpVector * (6.0 * Scale), FVector(Scale));
		auto* Tile = GetWorld()->SpawnActor<ACastleGroundTileActor>(
			ACastleGroundTileActor::StaticClass(), Local * GetActorTransform(), Params);
		if (!Tile || !Tile->InitializeGroundCell(Cell, Board.GroundTileSize, Board.LayerHeight))
		{
			if (Tile) { Tile->Destroy(); }
			ClearMiniGround();
			return Reject(FText::Format(INVTEXT("微缩地面格 {0} 创建失败。"), FText::FromString(Cell.Coordinate.ToString())), OutReason);
		}
		Tile->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
		MiniTiles.Add(Cell.Coordinate, Tile);
	}
	return true;
}

void ACastleGambleSkyIsland::ClearMiniGround()
{
	for (const auto& Entry : MiniTiles)
	{
		if (IsValid(Entry.Value)) { Entry.Value->Destroy(); }
	}
	MiniTiles.Reset();
}

FVector ACastleGambleSkyIsland::GetMiniCellBase(const FIntVector& Cell) const
{
	return MiniOrigin + FVector(Cell.X * MiniCellSize, Cell.Y * MiniCellSize, Cell.Z * MiniLayerHeight);
}

bool ACastleGambleSkyIsland::TryResolveIslandGridCursorTarget(const FVector& Start, const FVector& End,
	FCastleGridCursorTarget& OutTarget) const
{
	OutTarget = FCastleGridCursorTarget();
	FHitResult Hit;
	// 保留首个真实遮挡命中，避免穿过机台、岛平台或其他房间选中后方格子。
	const auto* Controller = GetIslandContext().Controller.Get();
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleGambleGridCursor), false, Controller ? Controller->GetPawn() : nullptr);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query)) { return false; }
	const FTransform Transform = GetActorTransform();
	const FVector Normal = Transform.InverseTransformVectorNoScale(Hit.ImpactNormal);
	const FVector Abs = Normal.GetAbs();
	const int32 Axis = Abs.Z >= Abs.X && Abs.Z >= Abs.Y ? 2 : (Abs.X >= Abs.Y ? 0 : 1);
	const bool bPositive = Normal[Axis] > 0.0;
	FIntVector Cell;
	bool bGround = false;
	if (const auto* Tile = Cast<ACastleGroundTileActor>(Hit.GetActor()))
	{
		Cell = Tile->GetGroundCell().Coordinate;
		if (MiniTiles.FindRef(Cell) != Tile || Hit.GetComponent() != Tile->GetTileMeshComponent() || Axis != 2 || !bPositive)
		{
			return false;
		}
		bGround = true;
	}
	else
	{
		if (Hit.GetActor() != this) { return false; }
		const auto* RoomCell = MiniRooms.FindKey(Cast<UBoxComponent>(Hit.GetComponent()));
		if (!RoomCell) { return false; }
		Cell = *RoomCell;
	}

	// 地面顶面直接映射至 Z=0，房间的每一面都映射至其外侧一格。
	const ECastleGridFace Faces[3][2] = {
		{ECastleGridFace::NegativeX, ECastleGridFace::PositiveX},
		{ECastleGridFace::NegativeY, ECastleGridFace::PositiveY},
		{ECastleGridFace::Down, ECastleGridFace::Up}
	};
	FIntVector Adjacent = Cell;
	if (!bGround) { Adjacent[Axis] += bPositive ? 1 : -1; }
	const double Skin = Config->MiniBoardCellScale;
	FVector Center = GetMiniCellBase(Cell) + FVector::UpVector * (bGround ? 0.0 : MiniLayerHeight * 0.5);
	Center[Axis] = Transform.InverseTransformPosition(Hit.ImpactPoint)[Axis] + Normal[Axis] * 1.5 * Skin;
	FVector Size(MiniTileSize, MiniTileSize, MiniLayerHeight);
	Size[Axis] = 2.0 * Skin;
	OutTarget.Cell = Cell;
	OutTarget.Face = Faces[Axis][bPositive];
	OutTarget.AdjacentCell = Adjacent;
	OutTarget.Surface = bGround ? ECastleGridCursorSurface::Ground : ECastleGridCursorSurface::Building;
	OutTarget.WorldNormal = Hit.ImpactNormal;
	OutTarget.IndicatorTransform = FTransform(Transform.GetRotation(), Transform.TransformPosition(Center),
		Size * Transform.GetScale3D() / 100.0);
	OutTarget.bValid = true;
	return true;
}

bool ACastleGambleSkyIsland::CanPlaceCardAtIslandCursor(const FCastleGridCursorTarget& Target, int32 BackpackIndex) const
{
	FCastleBuildingDescriptor Card;
	FText Reason;
	return Target.bValid && !IsRouletteSpinning() && !bTransacting && IsValid(Backpack)
		&& Backpack->TryGetItemAt(BackpackIndex, Card) && MiniBoard.CanPlace(Card, Target.AdjacentCell, Reason);
}

bool ACastleGambleSkyIsland::TryPlaceBackpackAtIslandCursor(const FCastleGridCursorTarget& Target, int32 BackpackIndex,
	FText& OutReason)
{
	return Target.bValid && TryPlaceCard(BackpackIndex, Target.AdjacentCell, OutReason);
}

bool ACastleGambleSkyIsland::TryRetractLastIslandPlacement(FText& OutReason)
{
	return TryReturnLastCard(OutReason);
}

UBoxComponent* ACastleGambleSkyIsland::CreateMiniCard(const FCastleBuildingDescriptor& Card, const FIntVector& Cell,
	FText& OutReason)
{
	const auto* Level = IsValid(Card.Definition) ? Card.Definition->FindLevel(Card.Level) : nullptr;
	if (!Level || !Level->BuildingClass)
	{
		Reject(FText::Format(INVTEXT("房间定义 {0} 缺少 {1} 级建筑类。"),
			FText::FromString(GetPathNameSafe(Card.Definition)), Card.Level), OutReason);
		return nullptr;
	}

	// 读取原生和蓝图组件模板的层级。这里只创建普通网格组件，从不生成或初始化建筑 Actor。
	UClass* BuildingClass = Level->BuildingClass.Get();
	auto* GeneratedClass = Cast<UBlueprintGeneratedClass>(BuildingClass);
	TMap<const USceneComponent*, const USceneComponent*> ScriptParents;
	for (auto* Class = GeneratedClass; Class; Class = Cast<UBlueprintGeneratedClass>(Class->GetSuperClass()))
	{
		auto* Script = Class->SimpleConstructionScript.Get();
		if (!Script) { continue; }
		for (USCS_Node* Node : Script->GetAllNodes())
		{
			auto* Component = Cast<USceneComponent>(Node->GetActualComponentTemplate(GeneratedClass));
			if (!Component) { continue; }
			const USceneComponent* Parent = FindMiniatureTemplateParent(*Node, *Script, *GeneratedClass);
			if (!Parent && !Node->ParentComponentOrVariableName.IsNone())
			{
				Reject(FText::Format(INVTEXT("建筑类 {0} 的模型组件 {1} 缺少已配置的父组件 {2}。"),
					FText::FromString(BuildingClass->GetPathName()), FText::FromName(Node->GetVariableName()),
					FText::FromName(Node->ParentComponentOrVariableName)), OutReason);
				return nullptr;
			}
			ScriptParents.Add(Component, Parent);
		}
	}
	TArray<const UStaticMeshComponent*> Templates;
	AActor::GetActorClassDefaultComponents<UStaticMeshComponent>(BuildingClass, Templates);
	TArray<TPair<const UStaticMeshComponent*, FTransform>> Parts;
	FBox Bounds(ForceInit);
	for (const UStaticMeshComponent* Template : Templates)
	{
		if (!Template->IsVisible() || !Template->GetStaticMesh()) { continue; }
		FTransform Local = FTransform::Identity;
		bool bGameplayPart = false;
		for (const USceneComponent* Part = Template; Part;)
		{
			if (Part->IsA<UBuildingGameplayRootComponent>())
			{
				bGameplayPart = true;
				break;
			}
			Local = Local * Part->GetRelativeTransform();
			const auto* ScriptParent = ScriptParents.Find(Part);
			Part = ScriptParent ? *ScriptParent : Part->GetAttachParent();
		}
		if (bGameplayPart) { continue; }
		Parts.Emplace(Template, Local);
		Bounds += Template->GetStaticMesh()->GetBoundingBox().TransformBy(Local);
	}
	const FVector ModelSize = Bounds.GetSize();
	if (Parts.IsEmpty() || ModelSize.ContainsNaN() || ModelSize.GetMin() <= UE_SMALL_NUMBER)
	{
		Reject(FText::Format(INVTEXT("建筑类 {0} 缺少有效的可见静态网格。"),
			FText::FromString(BuildingClass->GetPathName())), OutReason);
		return nullptr;
	}

	// 押注卡固定占一微缩格；保留作者造型比例并把多格等级模型收进同一格。
	const double Scale = FMath::Min(Config->MiniBoardCellScale, FMath::Min(
		MiniTileSize * 0.94 / FMath::Max(ModelSize.X, ModelSize.Y), MiniLayerHeight * 0.94 / ModelSize.Z));
	const FVector Extent(MiniTileSize * 0.5, MiniTileSize * 0.5, MiniLayerHeight * 0.5);
	auto* Room = NewObject<UBoxComponent>(this);
	Room->SetupAttachment(GetRootComponent());
	Room->SetRelativeLocation(GetMiniCellBase(Cell) + FVector::UpVector * Extent.Z);
	Room->SetBoxExtent(Extent);
	Room->SetCanEverAffectNavigation(false);
	Room->SetGenerateOverlapEvents(false);
	Room->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Room->SetCollisionResponseToAllChannels(ECR_Ignore);
	Room->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Room->RegisterComponent();
	const FVector ModelBase(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z);
	for (const auto& Part : Parts)
	{
		auto* Mesh = NewObject<UStaticMeshComponent>(this);
		Mesh->SetupAttachment(Room);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetRelativeTransform(FTransform(Part.Value.GetRotation(),
			(Part.Value.GetLocation() - ModelBase) * Scale - FVector::UpVector * Extent.Z,
			Part.Value.GetScale3D() * Scale));
		Mesh->SetStaticMesh(Part.Key->GetStaticMesh());
		for (int32 Index = 0; Index < Part.Key->GetNumMaterials(); ++Index)
		{
			Mesh->SetMaterial(Index, Part.Key->GetMaterial(Index));
		}
		Mesh->RegisterComponent();
	}
	// 色号牌沿近端立面显示。数字仍来自原卡，徽章和建筑运算均不介入。
	const FLinearColor Colors[] = {{0.84f, 0.06f, 0.07f}, {0.06f, 0.25f, 0.82f}, {0.05f, 0.61f, 0.18f},
		{0.94f, 0.64f, 0.04f}, {0.54f, 0.12f, 0.77f}};
	const int32 Color = static_cast<int32>(Card.Color) - static_cast<int32>(ECastleTileColor::Red);
	check(Color >= 0 && Color < UE_ARRAY_COUNT(Colors));
	auto* Plate = NewObject<UStaticMeshComponent>(this);
	Plate->SetupAttachment(Room);
	Plate->SetCanEverAffectNavigation(false);
	Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Plate->SetStaticMesh(CubeMesh);
	Plate->SetRelativeLocation(FVector(-Extent.X, 0, 0));
	Plate->SetRelativeScale3D(FVector(0.025, MiniTileSize * 0.35 / 100.0, MiniTileSize * 0.32 / 100.0));
	SetTint(*Plate, Colors[Color]);
	Plate->RegisterComponent();
	auto* Label = NewObject<UTextRenderComponent>(this);
	Label->SetupAttachment(Room);
	Label->SetRelativeLocation(FVector(-Extent.X - 2.0, 0, 0));
	Label->SetRelativeRotation(FRotator(0, 180, 0));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(MiniTileSize * 0.3);
	Label->SetText(FText::AsNumber(Card.Number));
	Label->SetCanEverAffectNavigation(false);
	Label->RegisterComponent();
	return Room;
}

void ACastleGambleSkyIsland::RemoveMiniCard(const FIntVector& Cell)
{
	auto* Room = MiniRooms.FindRef(Cell).Get();
	check(Room);
	TArray<USceneComponent*> VisualParts;
	Room->GetChildrenComponents(false, VisualParts);
	for (USceneComponent* Part : VisualParts) { Part->DestroyComponent(); }
	Room->DestroyComponent();
	MiniRooms.Remove(Cell);
}

void ACastleGambleSkyIsland::ClearMiniCards()
{
	while (!MiniRooms.IsEmpty())
	{
		const FIntVector Cell = MiniRooms.CreateConstIterator().Key();
		RemoveMiniCard(Cell);
	}
}
