#include "CastlePinballNPCSkyIsland.h"

#include "Castle/Director/CastleDirectorSubsystem.h"
#include "CastlePinballNPCSimulationComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
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
	Simulation->OnCommitRecruit.BindUObject(this, &ACastlePinballNPCSkyIsland::CommitRecruit);

	// 负 Pitch 让局部 +X 向下。地板表面是局部 Z=0，NPC 与球体离地高度都沿局部 +Z 测量。
	Playfield->SetRelativeRotation(FRotator(-Config->TiltAngleDegrees, 0.0, 0.0));
	AddShape(CubeMesh, {0.0, 0.0, -10.0}, {2.0 * HalfLength, 2.0 * HalfWidth, 20.0}, BoardColor);
	for (double Side : {-1.0, 1.0})
	{
		AddShape(CubeMesh, {0.0, Side * (HalfWidth + 6.0), 25.0}, {2.0 * HalfLength, 12.0, 50.0}, RailColor);
	}
	AddShape(CubeMesh, {-HalfLength - 6.0, 0.0, 25.0}, {12.0, 2.0 * HalfWidth, 50.0}, RailColor);
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
	FText Reason;
	if (!Simulation->Advance(DeltaSeconds, Reason))
	{
		SetActorTickEnabled(false);
		if (GEngine) { GEngine->AddOnScreenDebugMessage(INDEX_NONE, 6.0f, FColor::Red, Reason.ToString()); }
	}
	SyncScene();
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
	Label->SetRelativeRotation(FRotator(0.0, 180.0, 0.0));
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
	SetActorTickEnabled(false);
	Simulation->OnCommitRecruit.Unbind();
	Simulation->Clear();
	CoinBodies.Reset();
	NPCBodies.Reset();
	NPCLabels.Reset();
	UE_LOG(LogMoving_Castle, Display, TEXT("弹珠机空岛 %s 已清理。"), *GetPathName());
	Super::EndPlay(EndPlayReason);
}
