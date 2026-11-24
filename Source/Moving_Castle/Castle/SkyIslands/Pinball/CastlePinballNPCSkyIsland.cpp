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
#include "Materials/MaterialInstanceDynamic.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FLinearColor BoardColor(0.035f, 0.12f, 0.18f);
	const FLinearColor RailColor(0.15f, 0.57f, 0.70f);
	const FLinearColor PegColor(0.70f, 0.19f, 0.40f);
	const FLinearColor MultiplyColor(0.22f, 0.68f, 0.35f);
	const FLinearColor AddColor(0.21f, 0.40f, 0.89f);
	const FLinearColor NPCColor(0.80f, 0.39f, 0.16f);
	const FLinearColor GoldColor(0.95f, 0.68f, 0.08f);

	// 模板网格的三个尺寸都是 100 厘米，且枢轴位于包围盒中心。
	void SetShape(UStaticMeshComponent& Component, UStaticMesh* Mesh, UMaterialInterface* Material,
		const FVector& Position, const FVector& Size, const FLinearColor& Color)
	{
		Component.SetStaticMesh(Mesh);
		Component.SetRelativeLocation(Position);
		Component.SetRelativeScale3D(Size / 100.0);
		Component.SetGenerateOverlapEvents(false);
		UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Material, &Component);
		Tint->SetVectorParameterValue(TEXT("Color"), Color);
		Component.SetMaterial(0, Tint);
	}
}

ACastlePinballNPCSkyIsland::ACastlePinballNPCSkyIsland()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Playfield = CreateDefaultSubobject<USceneComponent>(TEXT("PinballPlayfield"));
	Playfield->SetupAttachment(GetRootComponent());
	// 最大允许倾角下，场地低端也要留在共享平台之上。
	Playfield->SetRelativeLocation(FVector(0.0, 0.0, 500.0));
	Simulation = CreateDefaultSubobject<UCastlePinballNPCSimulationComponent>(TEXT("PinballSimulation"));
	Console = CreateDefaultSubobject<UCastlePinballNPCConsoleComponent>(TEXT("PinballConsole"));
	Console->SetupAttachment(Playfield);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
	SphereMesh = Sphere.Object;
	ShapeMaterial = Material.Object;
	IslandConfig = TSoftObjectPtr<UCastlePinballNPCSkyIslandDataAsset>(
		FSoftObjectPath(TEXT("/Game/MovingCastle/Data/SkyIslands/DA_PinballNPCSkyIsland.DA_PinballNPCSkyIsland")));
	StatusWidgetClass = TSoftClassPtr<UCastlePinballNPCStatusWidget>(
		FSoftObjectPath(TEXT("/Game/MovingCastle/UI/SkyIslands/Pinball/WBP_PinballNPCStatus.WBP_PinballNPCStatus_C")));
}

bool ACastlePinballNPCSkyIsland::OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutFailureReason)
{
	using namespace CastlePinball;
	Config = IslandConfig.LoadSynchronous();
	if (!Config || !CubeMesh || !CylinderMesh || !SphereMesh || !ShapeMaterial)
	{
		OutFailureReason = INVTEXT("弹珠机缺少配置资产、基础网格或颜色材质。");
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
	AddShape(CubeMesh, {0.0, 0.0, -10.0}, {2.0 * HalfLength, 2.0 * HalfWidth, 20.0}, BoardColor);
	for (double Side : {-1.0, 1.0})
	{
		AddShape(CubeMesh, {0.0, Side * (HalfWidth + 6.0), 25.0}, {2.0 * HalfLength, 12.0, 50.0}, RailColor);
	}
	AddShape(CubeMesh, {-HalfLength - 6.0, 0.0, 25.0}, {12.0, 2.0 * HalfWidth, 50.0}, RailColor);
	SetShape(*Console, CubeMesh, ShapeMaterial, {HalfLength + 65.0, 0.0, 39.0}, {90.0, 140.0, 78.0}, GoldColor);
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
	AddShape(CubeMesh, {LaunchX, 0.0, 52.0}, {12.0, 2.0 * HalfWidth, 10.0}, RailColor);
	LauncherCarriage = AddShape(CubeMesh, {LaunchX, 0.0, 52.0}, {35.0, 35.0, 20.0}, GoldColor);
	LauncherBarrel = AddShape(CylinderMesh, {LaunchX, 0.0, 30.0}, {16.0, 16.0, 53.0}, GoldColor);
	LauncherAngle = (Config->LauncherMinAngle + Config->LauncherMaxAngle) * 0.5;
	UpdateLauncher(0.0f);
	AddShape(CubeMesh, {HalfLength - 12.0, 0.0, 1.0}, {24.0, 2.0 * HalfWidth, 2.0}, RailColor);
	AddLabel({HalfLength, 0.0, 28.0}, INVTEXT("COLLECTOR"), 23.0f);
	for (const FCastlePinballPeg& Peg : Config->Pegs)
	{
		AddShape(CylinderMesh, {Peg.Center.X, Peg.Center.Y, 27.0}, {2.0 * Peg.Radius, 2.0 * Peg.Radius, 54.0}, PegColor);
	}
	for (const FCastlePinballGate& Gate : Config->MultiplierGates)
	{
		const bool bMultiply = Gate.Mode == ECastlePinballGateMode::Multiply;
		const FLinearColor& Color = bMultiply ? MultiplyColor : AddColor;
		AddShape(CubeMesh, {Gate.Center.X, Gate.Center.Y, 1.5}, {7.0, Gate.Width, 3.0}, Color);
		AddShape(CubeMesh, {Gate.Center.X, Gate.Center.Y, 68.0}, {10.0, Gate.Width, 8.0}, Color);
		for (double Side : {-1.0, 1.0})
		{
			AddShape(CubeMesh, {Gate.Center.X, Gate.Center.Y + Side * Gate.Width * 0.5, 33.0},
				{10.0, 10.0, 66.0}, Color);
		}
		AddLabel({Gate.Center.X, Gate.Center.Y, 78.0},
			FText::FromString(FString::Printf(TEXT("%s%d"), bMultiply ? TEXT("x") : TEXT("+"), Gate.Value)), 24.0f);
	}
	for (int32 Index = 0; Index < Simulation->GetNPCs().Num(); ++Index)
	{
		NPCBodies.Add(AddShape(CylinderMesh, FVector::ZeroVector, {2.0 * NPCRadius, 2.0 * NPCRadius, 68.0}, NPCColor));
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
	LauncherBarrel->SetRelativeLocationAndRotation(FVector(LaunchX, LauncherPosition, 33.0), FRotator(90.0, LauncherAngle, 0.0));
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

		Body->SetRelativeLocation(FVector(NPC.Position.X, NPC.Position.Y, 34.0));
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
			Body = AddShape(SphereMesh, FVector::ZeroVector,
				{2.0 * CoinRadius, 2.0 * CoinRadius, 2.0 * CoinRadius}, GoldColor);
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

UStaticMeshComponent* ACastlePinballNPCSkyIsland::AddShape(
	UStaticMesh* Mesh, const FVector& Position, const FVector& Size, const FLinearColor& Color)
{
	UStaticMeshComponent* Shape = NewObject<UStaticMeshComponent>(this);
	Shape->SetupAttachment(Playfield);
	Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shape->SetCanEverAffectNavigation(false);
	SetShape(*Shape, Mesh, ShapeMaterial, Position, Size, Color);
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
