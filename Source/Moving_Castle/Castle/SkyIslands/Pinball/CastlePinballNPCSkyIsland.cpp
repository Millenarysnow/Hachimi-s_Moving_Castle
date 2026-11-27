#include "CastlePinballNPCSkyIsland.h"

#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Components/CastleEconomyComponent.h"
#include "CastlePinballNPCConsoleComponent.h"
#include "CastlePinballNPCSimulationComponent.h"
#include "CastlePinballNPCStatusWidget.h"
#include "CastlePlayerController.h"
#include "Components/InputComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

ACastlePinballNPCSkyIsland::ACastlePinballNPCSkyIsland()
{
	// 绑定该玩法的岛体，位置沿用作者对齐的岛面中心。
	SetIslandArtMesh(TEXT("SM_SkyIsland_PinballNPC"));
	SetDepartureArtMesh(TEXT("SM_SkyIsland_Departure_PinballNPC"));
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Playfield = CreateDefaultSubobject<USceneComponent>(TEXT("PinballPlayfield"));
	Playfield->SetupAttachment(GetRootComponent());
	// 最大允许倾角下，场地低端也要留在共享平台之上。
	Playfield->SetRelativeLocation(FVector(0.0, 0.0, 500.0));
	Simulation = CreateDefaultSubobject<UCastlePinballNPCSimulationComponent>(TEXT("PinballSimulation"));
	Console = CreateDefaultSubobject<UCastlePinballNPCConsoleComponent>(TEXT("PinballConsole"));
	Console->SetupAttachment(Playfield);

	// 投币台独立网格的坐标相对原盘面组件原点保存，和盘面共用倾斜坐标系。
	Console->SetStaticMesh(LoadIslandArt(TEXT("SM_Pinball_Console")));
	Console->SetRelativeLocation(FVector(CastlePinball::HalfLength + 65.0, 0.0, 39.0));
	Console->SetRelativeScale3D(FVector::OneVector);
	Console->SetCanEverAffectNavigation(false);
	Console->SetVisibility(true);
	BoardMesh = LoadIslandArt(TEXT("SM_Pinball_Board"));
	PegMesh = LoadIslandArt(TEXT("SM_Pinball_Peg"));
	GateMesh = LoadIslandArt(TEXT("SM_Pinball_Gate"));
	CarriageMesh = LoadIslandArt(TEXT("SM_Pinball_Carriage"));
	GuideMesh = LoadIslandArt(TEXT("SM_Pinball_Guide"));
	CoinMesh = LoadIslandArt(TEXT("SM_Pinball_Coin"));
	for (int32 Style = 1; Style <= 6; ++Style)
	{
		RecruitMeshes.Add(LoadIslandArt(*FString::Printf(TEXT("SM_Pinball_Recruit%d"), Style)));
	}
	IslandConfig = TSoftObjectPtr<UCastlePinballNPCSkyIslandDataAsset>(
		FSoftObjectPath(TEXT("/Game/MovingCastle/Data/SkyIslands/DA_PinballNPCSkyIsland.DA_PinballNPCSkyIsland")));
	StatusWidgetClass = TSoftClassPtr<UCastlePinballNPCStatusWidget>(
		FSoftObjectPath(TEXT("/Game/MovingCastle/UI/SkyIslands/Pinball/WBP_PinballNPCStatus.WBP_PinballNPCStatus_C")));
}

bool ACastlePinballNPCSkyIsland::OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutFailureReason)
{
	using namespace CastlePinball;
	Config = IslandConfig.LoadSynchronous();
	if (!Config || !Console->GetStaticMesh())
	{
		OutFailureReason = INVTEXT("弹珠机缺少配置资产或独立投币台网格。");
		UE_LOG(LogMoving_Castle, Error, TEXT("空岛 %s，配置 %s：%s"), *GetPathName(),
			*IslandConfig.ToSoftObjectPath().ToString(), *OutFailureReason.ToString());
		return false;
	}
	if (!Simulation->Initialize(*Config, Context.Seed, OutFailureReason)) { return false; }
	LoadedStatusWidgetClass = StatusWidgetClass.LoadSynchronous();
	if (!LoadedStatusWidgetClass || LoadedStatusWidgetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		OutFailureReason = INVTEXT("弹珠机缺少可实例化的状态面板。");
		UE_LOG(LogMoving_Castle, Error, TEXT("空岛 %s，面板 %s：%s"), *GetPathName(),
			*StatusWidgetClass.ToSoftObjectPath().ToString(), *OutFailureReason.ToString());
		return false;
	}
	Simulation->OnCommitRecruit.BindUObject(this, &ACastlePinballNPCSkyIsland::CommitRecruit);

	// 局部 +X 仍向下；翻转偏航把低端和盘面法线转向岛内 -X 的玩家到达点。
	Playfield->SetRelativeRotation(FRotator(-Config->TiltAngleDegrees, 180.0, 0.0));
	// 木板、护栏与投币台整体随配置倾斜，不能烘入水平的岛体。
	AddPlayfieldMesh(BoardMesh, FVector::ZeroVector);
	Console->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Console->SetCollisionResponseToAllChannels(ECR_Ignore);
	Console->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// 基座到达锚点仍由导演使用。本岛只调整该实例上的位置和朝向。
	USceneComponent* Arrival = CastChecked<USceneComponent>(GetDefaultSubobjectByName(TEXT("PlayerArrival")));
	const FVector ArrivalLocation(-820.0, 0.0, 990.0);
	const FVector ConsoleLocation = Playfield->GetRelativeTransform().TransformPosition(Console->GetRelativeLocation());
	const FVector ToConsole = ConsoleLocation - ArrivalLocation;
	FRotator ArrivalView = ToConsole.Rotation();
	// 飞行 Pawn 的相机臂把眼睛沿视角 Up 抬高 64 cm，预扣这段偏移让入岛准星对准控制台。
	ArrivalView.Pitch -= FMath::RadiansToDegrees(FMath::Asin(64.0 / ToConsole.Size()));
	Arrival->SetRelativeLocationAndRotation(ArrivalLocation, ArrivalView);
	AddLabel({HalfLength + 65.0, 0.0, 88.0}, INVTEXT("PINBALL  |  E"), 28.0f);
	// 滑车横移，导向套以同一枢轴偏航，预装的展示金币随套筒移动。
	LauncherCarriage = AddPlayfieldMesh(CarriageMesh, {LaunchX, 0.0, 52.0});
	LauncherBarrel = AddPlayfieldMesh(GuideMesh, {LaunchX, 0.0, 52.0});
	LauncherAngle = (Config->LauncherMinAngle + Config->LauncherMaxAngle) * 0.5;
	UpdateLauncher(0.0f);
	AddLabel({HalfLength, 0.0, 28.0}, INVTEXT("COLLECTOR"), 23.0f);
	for (const FCastlePinballPeg& Peg : Config->Pegs)
	{
		// 原作挡钉半径为 19 厘米，水平缩放后与本次模拟的圆形反弹区对齐。
		AddPlayfieldMesh(PegMesh, {Peg.Center.X, Peg.Center.Y, 0}, {Peg.Radius / 19.0, Peg.Radius / 19.0, 1.0});
	}
	for (const FCastlePinballGate& Gate : Config->MultiplierGates)
	{
		const bool bMultiply = Gate.Mode == ECastlePinballGateMode::Multiply;
		// 以原作 150 厘米门宽为基准，顶部数字由当前配置生成。
		AddPlayfieldMesh(GateMesh, {Gate.Center.X, Gate.Center.Y, 0}, {1.0, Gate.Width / 150.0, 1.0});
		AddLabel({Gate.Center.X + 9.0, Gate.Center.Y, 74.0},
			FText::FromString(FString::Printf(TEXT("%s%d"), bMultiply ? TEXT("x") : TEXT("+"), Gate.Value)), 24.0f);
	}
	for (int32 Index = 0; Index < Simulation->GetNPCs().Num(); ++Index)
	{
		NPCBodies.Add(AddPlayfieldMesh(RecruitMeshes[Index % RecruitMeshes.Num()], FVector::ZeroVector));
		NPCLabels.Add(AddLabel(FVector::ZeroVector, FText::GetEmpty(), 26.0f));
	}
	SyncScene();
	SetActorTickEnabled(true);
	return true;
}

void ACastlePinballNPCSkyIsland::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateLauncher(DeltaSeconds);
	FText Reason;
	if (!Simulation->Advance(DeltaSeconds, Reason))
	{
		ExitAimMode();
		SetActorTickEnabled(false);
		if (GEngine) { GEngine->AddOnScreenDebugMessage(INDEX_NONE, 6.0f, FColor::Red, Reason.ToString()); }
	}
	SyncScene();
	RefreshStatus();
}

bool ACastlePinballNPCSkyIsland::EnterAimMode(ACastlePlayerController& Controller, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (AimController.Get() == &Controller)
	{
		ExitAimMode();
		return true;
	}
	if (AimInput || GetIslandContext().Controller.Get() != &Controller || !IsActorTickEnabled()
		|| !Controller.IsLocalController() || !Controller.GetEconomy() || !Controller.GetPawn()
		|| !Controller.GetPawn()->GetMovementComponent() || !LoadedStatusWidgetClass)
	{
		OutReason = INVTEXT("投币操控需要当前空岛、本地控制器、移动组件、金币账户和状态面板。");
		UE_LOG(LogMoving_Castle, Error, TEXT("空岛 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}

	StatusWidget = CreateWidget<UCastlePinballNPCStatusWidget>(&Controller, LoadedStatusWidgetClass);
	if (!StatusWidget || !StatusWidget->AddToPlayerScreen(20))
	{
		StatusWidget = nullptr;
		OutReason = INVTEXT("弹珠机状态面板无法加入本地玩家视口。");
		UE_LOG(LogMoving_Castle, Error, TEXT("面板 %s：%s"), *StatusWidgetClass.ToSoftObjectPath().ToString(), *OutReason.ToString());
		return false;
	}

	// 高优先级输入只在瞄准期间存在。按住方向键由 Tick 读取，阻止下层飞行输入响应。
	AimInput = NewObject<UInputComponent>(this);
	AimInput->bBlockInput = true;
	AimInput->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ACastlePinballNPCSkyIsland::LaunchFromInput);
	// 此输入组件高于控制器；通过同一映射分流，不让发射同时触发棋盘放置。
	for (const FKey& Key : CastleControlInput::GetPointerKeys())
	{
		FInputKeyBinding Binding(FInputChord(Key), IE_Pressed);
		Binding.KeyDelegate.GetDelegateWithKeyForManualSet().BindWeakLambda(this, [this](FKey Pressed)
		{
			if (const auto* Player = AimController.Get(); Player && Pressed == Player->GetPrimaryPlaceInteractKey())
			{
				LaunchFromInput();
			}
		});
		AimInput->KeyBindings.Add(MoveTemp(Binding));
	}
	for (const FKey& Key : {EKeys::E, EKeys::Tab, EKeys::Escape})
	{
		AimInput->BindKey(Key, IE_Pressed, this, &ACastlePinballNPCSkyIsland::ExitAimMode);
	}
	AimController = &Controller;
	Controller.GetPawn()->GetMovementComponent()->StopMovementImmediately();
	Controller.SetIgnoreMoveInput(true);
	Controller.SetIgnoreLookInput(true);
	FVector ViewLocation;
	FRotator ViewRotation;
	Controller.GetPlayerViewPoint(ViewLocation, ViewRotation);
	Controller.SetControlRotation((Playfield->GetComponentLocation() - ViewLocation).Rotation());
	Controller.PushInputComponent(AimInput);
	StatusWidget->SetFeedback(FText::GetEmpty());
	RefreshStatus();
	UE_LOG(LogMoving_Castle, Display, TEXT("弹珠机 %s 已接管投币输入。"), *GetPathName());
	return true;
}

void ACastlePinballNPCSkyIsland::ExitAimMode()
{
	if (!AimInput) { return; }
	if (ACastlePlayerController* Controller = AimController.Get())
	{
		// 只归还本岛加上的一份忽略计数，保留控制器其他模态输入的计数。
		Controller->PopInputComponent(AimInput);
		Controller->SetIgnoreMoveInput(false);
		Controller->SetIgnoreLookInput(false);
	}
	AimController.Reset();
	AimInput = nullptr;
	if (StatusWidget)
	{
		StatusWidget->RemoveFromParent();
		StatusWidget = nullptr;
	}
	UE_LOG(LogMoving_Castle, Display, TEXT("弹珠机 %s 已归还投币输入。"), *GetPathName());
}

void ACastlePinballNPCSkyIsland::UpdateLauncher(float DeltaSeconds)
{
	using namespace CastlePinball;
	if (const ACastlePlayerController* Controller = AimController.Get())
	{
		// 旋转后的局部 -Y 是屏幕右侧，向右移轨和向右发射都应减小局部 Y 分量。
		const int32 Move = static_cast<int32>(Controller->IsInputKeyDown(EKeys::A)) - Controller->IsInputKeyDown(EKeys::D);
		const int32 Turn = static_cast<int32>(Controller->IsInputKeyDown(EKeys::Left)) - Controller->IsInputKeyDown(EKeys::Right);
		// 滑轨和转轴的止挡属于玩法操作范围，配置本身在入岛前严格校验。
		LauncherPosition = FMath::Clamp(LauncherPosition + Move * DeltaSeconds * 310.0, -HalfWidth + CoinRadius, HalfWidth - CoinRadius);
		LauncherAngle = FMath::Clamp(LauncherAngle + Turn * DeltaSeconds * 42.0,
			static_cast<double>(Config->LauncherMinAngle), static_cast<double>(Config->LauncherMaxAngle));
	}
	LauncherCarriage->SetRelativeLocation(FVector(LaunchX, LauncherPosition, 52.0));
	LauncherBarrel->SetRelativeLocationAndRotation(FVector(LaunchX, LauncherPosition, 52.0), FRotator(0.0, LauncherAngle, 0.0));
}

bool ACastlePinballNPCSkyIsland::TryLaunchCoin(FText& OutReason)
{
	ACastlePlayerController* Controller = AimController.Get();
	if (!Controller || !Controller->GetEconomy() || bLaunching || !IsActorTickEnabled())
	{
		OutReason = INVTEXT("当前未启用投币操控，或上一笔投币仍在提交。");
		UE_LOG(LogMoving_Castle, Error, TEXT("空岛 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	TGuardValue<bool> LaunchGuard(bLaunching, true);
	if (!Simulation->CanLaunch(LauncherPosition, LauncherAngle, OutReason)) { return false; }
	if (!Controller->GetEconomy()->TrySpendGold(Config->CoinLaunchCost, OutReason))
	{
		UE_LOG(LogMoving_Castle, Warning, TEXT("弹珠机 %s 拒绝投币：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	Simulation->CommitLaunch(LauncherPosition, LauncherAngle);
	UE_LOG(LogMoving_Castle, Display, TEXT("弹珠机 %s 已投入一枚金币，扣除 %d 金。"), *GetPathName(), Config->CoinLaunchCost);
	return true;
}

void ACastlePinballNPCSkyIsland::LaunchFromInput()
{
	FText Result;
	if (TryLaunchCoin(Result)) { Result = INVTEXT("已投币。"); }
	if (StatusWidget) { StatusWidget->SetFeedback(Result); }
	RefreshStatus();
}

void ACastlePinballNPCSkyIsland::RefreshStatus()
{
	if (StatusWidget && AimController.IsValid())
	{
		StatusWidget->RefreshStatus(AimController->GetEconomy()->GetGold(), Config->CoinLaunchCost,
			Simulation->GetRecruitedCount(), Config->InitialNPCCount, LauncherPosition, LauncherAngle);
	}
}

bool ACastlePinballNPCSkyIsland::CommitRecruit(int32 NPCIndex, FText& OutReason)
{
	UCastleDirectorSubsystem* Director = GetIslandContext().Director.Get();
	if (!Director)
	{
		OutReason = INVTEXT("弹珠机失去了本局导演，无法结算招募。");
		return false;
	}
	if (!Director->AddRunNPCCount(1, OutReason)) { return false; }

	const FText Message = FText::Format(INVTEXT("NPC {0} 已招募，本局待命人数 {1}。"), NPCIndex + 1, Director->GetRunNPCCount());
	if (StatusWidget) { StatusWidget->SetFeedback(Message); }
	UE_LOG(LogMoving_Castle, Display, TEXT("弹珠机 %s：%s"), *GetPathName(), *Message.ToString());
	if (GEngine) { GEngine->AddOnScreenDebugMessage(INDEX_NONE, 5.0f, FColor::Green, Message.ToString()); }
	return true;
}

void ACastlePinballNPCSkyIsland::SyncScene()
{
	using namespace CastlePinball;
	for (int32 Index = 0; Index < NPCBodies.Num(); ++Index)
	{
		const FCastlePinballNPC& NPC = Simulation->GetNPCs()[Index];
		UStaticMeshComponent* Body = NPCBodies[Index];
		UTextRenderComponent* Label = NPCLabels[Index];
		Body->SetVisibility(!NPC.bRecruited);
		Label->SetVisibility(!NPC.bRecruited);
		if (NPC.bRecruited) { continue; }

		Body->SetRelativeLocation(FVector(NPC.Position.X, NPC.Position.Y, 0.0));
		Label->SetRelativeLocation(FVector(NPC.Position.X, NPC.Position.Y, 76.0));
		Label->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), NPC.Hits, Config->CoinsRequiredPerNPC)));
	}
	TSet<uint64> ActiveIds;
	for (const FCastlePinballCoin& Coin : Simulation->GetCoins())
	{
		ActiveIds.Add(Coin.Id);
		TObjectPtr<UStaticMeshComponent>& Body = CoinBodies.FindOrAdd(Coin.Id);
		if (!Body)
		{
			// 金币网格以币心为轴，半径与模拟相同，可以直接使用模拟姿态。
			Body = AddPlayfieldMesh(CoinMesh, FVector::ZeroVector);
		}
		Body->SetRelativeLocationAndRotation(FVector(Coin.Position.X, Coin.Position.Y, CoinRadius), Coin.Rotation);
	}
	for (auto It = CoinBodies.CreateIterator(); It; ++It)
	{
		if (!ActiveIds.Contains(It.Key()))
		{
			It.Value()->DestroyComponent();
			It.RemoveCurrent();
		}
	}
}

UStaticMeshComponent* ACastlePinballNPCSkyIsland::AddPlayfieldMesh(
	UStaticMesh* Mesh, const FVector& Position, const FVector& Scale)
{
	UStaticMeshComponent* Shape = NewObject<UStaticMeshComponent>(this);
	Shape->SetupAttachment(Playfield);
	Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shape->SetCanEverAffectNavigation(false);
	Shape->SetStaticMesh(Mesh);
	Shape->SetRelativeLocation(Position);
	Shape->SetRelativeScale3D(Scale);
	AddInstanceComponent(Shape);
	Shape->RegisterComponent();
	return Shape;
}

UTextRenderComponent* ACastlePinballNPCSkyIsland::AddLabel(const FVector& Position, const FText& Text, float Size)
{
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
	Label->SetupAttachment(Playfield);
	Label->SetRelativeLocation(Position);
	// 文本正面沿局部 +X，随盘面一起朝向近端的玩家。
	Label->SetRelativeRotation(FRotator::ZeroRotator);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(Size);
	Label->SetText(Text);
	AddInstanceComponent(Label);
	Label->RegisterComponent();
	return Label;
}

void ACastlePinballNPCSkyIsland::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 正式离岛由导演成功传送后销毁整岛，保留已同步增加的本局待命人数。
	ExitAimMode();
#if !UE_BUILD_SHIPPING
	if (AcceptanceInput)
	{
		// 独立验收的交互层与瞄准层分别入栈，销毁时按逆序归还。
		if (auto* Controller = GetIslandContext().Controller.Get()) { Controller->PopInputComponent(AcceptanceInput); }
		AcceptanceInput = nullptr;
		OnDepartureRequested.RemoveAll(this);
	}
#endif
	SetActorTickEnabled(false);
	Simulation->OnCommitRecruit.Unbind();
	Simulation->Clear();
	CoinBodies.Reset();
	NPCBodies.Reset();
	NPCLabels.Reset();
	UE_LOG(LogMoving_Castle, Display, TEXT("弹珠机空岛 %s 已清理。"), *GetPathName());
	Super::EndPlay(EndPlayReason);
}
