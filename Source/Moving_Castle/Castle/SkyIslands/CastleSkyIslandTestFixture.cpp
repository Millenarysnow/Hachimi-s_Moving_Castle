#include "CastleSkyIslandTestFixture.h"

#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

FText UCastleSkyIslandFixtureInteractComponent::GetInteractPrompt() const
{
	return INVTEXT("空岛测试物件");
}

bool UCastleSkyIslandFixtureInteractComponent::CanInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const
{
	if (Context.Island != GetOwner() || Context.Hit.GetComponent() != this)
	{
		OutReason = INVTEXT("请瞄准本岛的测试物件。");
		return false;
	}
	return true;
}

bool UCastleSkyIslandFixtureInteractComponent::CanHighlightInteract(
	const FCastleSkyIslandInteractContext& Context) const
{
	// 测试物件复用相同所有权和命中约束，查询期间不修改交互计数或屏幕消息。
	return Context.Island == GetOwner() && Context.Hit.GetComponent() == this;
}

bool UCastleSkyIslandFixtureInteractComponent::ExecuteInteract(const FCastleSkyIslandInteractContext& Context, FText& OutFailureReason)
{
	if (!CanInteract(Context, OutFailureReason)) { return false; }
	++InteractionCount;
	const FString Message = FString::Printf(TEXT("空岛测试物件已交互 %d 次。"), InteractionCount);
	UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *Message);
	if (GEngine) { GEngine->AddOnScreenDebugMessage(2026112301, 4.0f, FColor::Green, Message); }
	return true;
}

ACastleSkyIslandTestFixture::ACastleSkyIslandTestFixture()
{
	// 矮方块放在入口左侧，正前方高柱仍是基座的离开门。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	TestObject = CreateDefaultSubobject<UCastleSkyIslandFixtureInteractComponent>(TEXT("FixtureInteractable"));
	TestObject->SetupAttachment(GetRootComponent());
	TestObject->SetStaticMesh(Cube.Object);
	TestObject->SetRelativeLocation(FVector(100, -450, 100));
	TestObject->SetRelativeScale3D(FVector(1.5, 1.5, 2.0));
	TestObject->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TestObject->SetCollisionResponseToAllChannels(ECR_Ignore);
	TestObject->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}
