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
	IslandArt = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("IslandArt"));
	IslandArt->SetupAttachment(Root);
	IslandArt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	IslandArt->SetCanEverAffectNavigation(false);
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

UStaticMesh* ACastleSkyIslandBase::LoadIslandArt(const TCHAR* AssetName)
{
	// 目录内的名字与 UE 包名相同。构造时保留硬引用，使烹饪和编辑器共享同一网格。
	const FString Path = FString::Printf(TEXT("/Game/MovingCastle/Model/SkyIslands/%s.%s"), AssetName, AssetName);
	ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(*Path);
	if (!Mesh.Succeeded())
	{
		MissingIslandArtPath = Path;
		UE_LOG(LogMoving_Castle, Error, TEXT("空岛 %s 缺少美术网格 %s。"), *GetPathName(), *Path);
	}
	return Mesh.Object;
}

void ACastleSkyIslandBase::SetIslandArtMesh(const TCHAR* AssetName)
{
	// 正式岛体和原生碰撞共享厘米坐标；出口由单独的美术网格组件负责显示。
	IslandArt->SetStaticMesh(LoadIslandArt(AssetName));
	Platform->SetVisibility(false);
	Departure->SetVisibility(false);
}

void ACastleSkyIslandBase::SetDepartureArtMesh(const TCHAR* AssetName)
{
	const FString Path = FString::Printf(TEXT("/Game/MovingCastle/Model/SkyIslands/%s.%s"), AssetName, AssetName);
	ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(*Path);
	if (!Mesh.Succeeded())
	{
		MissingDepartureArtPath = Path;
		Departure->SetStaticMesh(nullptr);
		Departure->SetVisibility(false);
		UE_LOG(LogMoving_Castle, Error, TEXT("空岛 %s 缺少独立离开门网格 %s。"), *GetPathName(), *Path);
		return;
	}

	// 该资产已将源网格局部坐标平移到门组件原点，因此门体和射线碰撞共享原有世界位置。
	Departure->SetStaticMesh(Mesh.Object);
	Departure->SetRelativeScale3D(FVector::OneVector);
	Departure->SetCanEverAffectNavigation(false);
	Departure->SetVisibility(true);
}

bool ACastleSkyIslandBase::InitializeIsland(const FCastleSkyIslandContext& Context, FText& OutFailureReason)
{
	if (!MissingIslandArtPath.IsEmpty())
	{
		OutFailureReason = FText::Format(INVTEXT("空岛美术网格资产 {0} 未加载。"),
			FText::FromString(MissingIslandArtPath));
		UE_LOG(LogMoving_Castle, Error, TEXT("空岛 %s 的美术网格资产 %s：%s"),
			*GetPathName(), *MissingIslandArtPath, *OutFailureReason.ToString());
		return false;
	}
	if (!MissingDepartureArtPath.IsEmpty())
	{
		OutFailureReason = FText::Format(INVTEXT("空岛离开门网格资产 {0} 未加载。"),
			FText::FromString(MissingDepartureArtPath));
		UE_LOG(LogMoving_Castle, Error, TEXT("空岛 %s 的离开门网格资产 %s：%s"),
			*GetPathName(), *MissingDepartureArtPath, *OutFailureReason.ToString());
		return false;
	}
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
	if (bInitialized) { Context.Controller->RegisterActiveSkyIsland(this); }
	return bInitialized;
}

void ACastleSkyIslandBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 只注销当前实例，较早销毁的空岛不能清掉新空岛的输入目标。
	if (auto* Controller = IslandContext.Controller.Get()) { Controller->UnregisterActiveSkyIsland(this); }
	Super::EndPlay(EndPlayReason);
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

bool ACastleSkyIslandBase::GrantBadgeWithReplaceUI(const UCastleBadgeDataAsset& Badge, FText& OutFailureReason)
{
	return IslandContext.Director.IsValid() && IslandContext.Director->RequestIslandBadgeReward(this, Badge, OutFailureReason);
}
