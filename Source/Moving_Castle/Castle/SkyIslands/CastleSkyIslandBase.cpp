#include "CastleSkyIslandBase.h"

#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CastlePlayerController.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

FText UCastleSkyIslandDepartureComponent::GetInteractPrompt() const
{
	return INVTEXT("离开 / 启程");
}

bool UCastleSkyIslandDepartureComponent::CanInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!IsValid(Context.Controller) || Context.Island != GetOwner() || Context.Hit.GetComponent() != this)
	{
		OutReason = INVTEXT("离开传送门必须属于当前空岛并被准星命中。");
		return false;
	}
	return true;
}

bool UCastleSkyIslandDepartureComponent::ExecuteInteract(const FCastleSkyIslandInteractContext& Context, FText& OutFailureReason)
{
	if (!CanInteract(Context, OutFailureReason)) { return false; }
	Context.Island->OnDepartureRequested.Broadcast();
	return true;
}

ACastleSkyIslandBase::ACastleSkyIslandBase()
{
	PrimaryActorTick.bCanEverTick = false;
	auto* Root = CreateDefaultSubobject<USceneComponent>(TEXT("IslandRoot"));
	SetRootComponent(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Platform = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderPlatform"));
	Platform->SetupAttachment(Root);
	Platform->SetStaticMesh(Cube.Object);
	Platform->SetRelativeScale3D(FVector(24.0, 24.0, 1.0));
	Platform->SetRelativeLocation(FVector(0, 0, -50));
	ArrivalAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("PlayerArrival"));
	ArrivalAnchor->SetupAttachment(Root);
	ArrivalAnchor->SetRelativeLocation(FVector(-700, 0, 240));
	Departure = CreateDefaultSubobject<UCastleSkyIslandDepartureComponent>(TEXT("DeparturePortal"));
	Departure->SetupAttachment(Root);
	Departure->SetStaticMesh(Cube.Object);
	Departure->SetRelativeLocation(FVector(700, 0, 200));
	Departure->SetRelativeScale3D(FVector(1.2, 2.4, 4.0));
	Departure->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Departure->SetCollisionResponseToAllChannels(ECR_Ignore);
	Departure->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Title = CreateDefaultSubobject<UTextRenderComponent>(TEXT("IslandTitle"));
	Title->SetupAttachment(Root);
	Title->SetWorldSize(90.0f);
	Title->SetHorizontalAlignment(EHTA_Center);
	Title->SetRelativeLocation(FVector(700, 0, 520));
	Title->SetRelativeRotation(FRotator(0, 180, 0));
}

bool ACastleSkyIslandBase::InitializeIsland(const FCastleSkyIslandContext& Context, FText& OutFailureReason)
{
	if (bInitialized || Context.Step < 1 || !Context.Director.IsValid() || !Context.Controller.IsValid()
		|| Context.Controller->GetWorld() != GetWorld() || !Platform->GetStaticMesh() || !Departure->GetStaticMesh())
	{
		OutFailureReason = INVTEXT("空岛初始化需要有效本局上下文、平台网格和离开传送门。");
		UE_LOG(LogMoving_Castle, Error, TEXT("空岛 %s：%s"), *GetPathName(), *OutFailureReason.ToString());
		return false;
	}
	// TextRender 占位牌使用离线字体已具备的字符；准星交互使用本地化中文提示。
	const FString Label = StaticEnum<ECastleSkyIslandType>()->GetNameStringByValue(static_cast<int64>(Context.Type));
	Title->SetText(FText::FromString(FString::Printf(TEXT("%s  |  DEPART"), *Label)));
	IslandContext = Context;
	bInitialized = OnInitializeIsland(Context, OutFailureReason);
	return bInitialized;
}

FTransform ACastleSkyIslandBase::GetPlayerArrivalTransform() const
{
	return ArrivalAnchor->GetComponentTransform();
}

bool ACastleSkyIslandBase::RequestOpenTemporaryRoomShop(FText& OutFailureReason)
{
	return IslandContext.Director.IsValid() && IslandContext.Director->RequestIslandRoomShop(this, OutFailureReason);
}

bool ACastleSkyIslandBase::GrantPendingRoomRewardUI(UCastlePendingRoomReward* Reward, FText& OutFailureReason)
{
	return IslandContext.Director.IsValid() && IslandContext.Director->RequestIslandRoomReward(this, Reward, OutFailureReason);
}

bool ACastleSkyIslandBase::GrantBadgeWithReplaceUI(const FCastleBadgeDefinition& Badge, FText& OutFailureReason)
{
	return IslandContext.Director.IsValid() && IslandContext.Director->RequestIslandBadgeReward(this, Badge, OutFailureReason);
}
