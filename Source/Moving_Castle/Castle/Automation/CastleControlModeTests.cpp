// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Camera/CameraActor.h"
#include "Castle/Actors/CastleFlightPawn.h"
#include "Castle/Actors/CastleBoard.h"
#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/Automation/CastlePIEAutomation.h"
#include "Castle/Automation/CastleBuildingTestUtilities.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Castle/Components/CastleBackpackComponent.h"
#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Data/CastleBuildingDefinition.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/Items/CastleRoomTraitComponent.h"
#include "Castle/Placement/CastleTowerSideGridComponent.h"
#include "Castle/SkyIslands/CastleSkyIslandBase.h"
#include "Castle/SkyIslands/Gamble/CastleGambleAcceptanceIsland.h"
#include "Castle/UI/Cards/CastleCardPortraitSubsystem.h"
#include "Castle/UI/Cards/CastleShopCardWidget.h"
#include "Castle/UI/Common/CastleBuildingDemolishMenuWidget.h"
#include "Castle/UI/Common/CastleControlSettingsWidget.h"
#include "Castle/UI/Common/CastleHUDControls.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Castle/Rendering/CastleHoverHighlightSubsystem.h"
#include "Components/Animation/BuildingMergeComponent.h"
#include "Components/Button.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Tests/AutomationCommon.h"
#include "TextureResource.h"


namespace
{
	// PlayerTick 将零时长视为暂停。取一帧的正时长让普通按键和鼠标键走非暂停输入栈。
	constexpr float InputTickSeconds = 1.0f / 60.0f;

	/** 通过 PlayerInput 注入按键和轴，等待真实输入栈在下一帧派发。 */
	void SendInput(ACastlePlayerController& Player, const FKey& Key, EInputEvent Event, float Amount)
	{
		Player.InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Amount));
	}

	void Press(ACastlePlayerController& Player, const FKey& Key)
	{
		SendInput(Player, Key, IE_Pressed, 1.0f);
		SendInput(Player, Key, IE_Released, 0.0f);
	}

	/** 执行正常的玩家输入栈与飞行 Pawn Tick，再读取移动组件即将消费的垂直向量。 */
	double ReadVerticalFlightInput(ACastlePlayerController& Player, ACastleFlightPawn& Pawn)
	{
		// 在本测试步骤内数值采样，避免视口失焦或组件 Tick 顺序清掉本次事件及向量。
		Player.PlayerTick(InputTickSeconds);
		Pawn.ConsumeMovementInputVector();
		static_cast<AActor&>(Pawn).Tick(0.0f);
		Pawn.ConsumeMovementInputVector();
		return Pawn.GetLastMovementInputVector().Z;
	}

	/** 按下和松开均通过真实绑定派发，验证当前模式的方向以及松开复位。 */
	void CheckFlightKey(FAutomationTestBase& Test, ACastlePlayerController& Player, ACastleFlightPawn& Pawn,
		const FKey& Key, double ExpectedZ)
	{
		const FString KeyLabel = FString::Printf(TEXT("%s模式 %s"),
			Player.GetControlMode() == ECastleControlMode::Design ? TEXT("设计") : TEXT("经典"), *Key.ToString());
		SendInput(Player, Key, IE_Pressed, 1.0f);
		Test.TestEqual(KeyLabel + TEXT(" 按下的垂直飞行输入"), ReadVerticalFlightInput(Player, Pawn), ExpectedZ);
		SendInput(Player, Key, IE_Released, 0.0f);
		Test.TestEqual(KeyLabel + TEXT(" 松开后的垂直飞行输入"), ReadVerticalFlightInput(Player, Pawn), 0.0);
	}

	/** 通过真实输入栈调档，验证速度与两项加减速同时生效。 */
	void CheckSpeedKey(FAutomationTestBase& Test, ACastlePlayerController& Player, const FKey& Key, const float Speed)
	{
		const auto* Pawn = Cast<ACastleFlightPawn>(Player.GetPawn());
		if (!Test.TestNotNull(TEXT("用于调速的正式飞行 Pawn"), Pawn)) { return; }
		Press(Player, Key);
		Player.PlayerTick(InputTickSeconds);
		const auto* Movement = Pawn->GetCharacterMovement();
		const FString Label = FString::Printf(TEXT("%s模式 %s"), Player.GetControlMode() == ECastleControlMode::Design
			? TEXT("设计") : TEXT("经典"), *Key.ToString());
		Test.TestEqual(Label + TEXT(" 飞行上限"), Movement->MaxFlySpeed, Speed);
		Test.TestEqual(Label + TEXT(" 飞行加速"), Movement->MaxAcceleration, Speed * 5.5f);
		Test.TestEqual(Label + TEXT(" 飞行制动"), Movement->BrakingDecelerationFlying, Speed * 5.5f);
	}

	/** 使用正式视口投影移动设计模式指针，下一步可由世界主点击重算射线。 */
	bool AimDesignPointerAt(ACastlePlayerController& Player, ACameraActor& Camera, const FVector& Eye, const FVector& Target)
	{
		Camera.SetActorLocationAndRotation(Eye, (Target - Eye).Rotation());
		Player.SetViewTarget(&Camera);
		Player.PlayerCameraManager->UpdateCamera(0.0f);
		FVector2D ScreenPosition;
		if (!Player.ProjectWorldLocationToScreen(Target, ScreenPosition)) { return false; }
		Player.SetMouseLocation(FMath::RoundToInt(ScreenPosition.X), FMath::RoundToInt(ScreenPosition.Y));
		return true;
	}

	ACastlePlayerController* GetViewportPlayer()
	{
		// 仅取本用例启动的 PIE 控制器，避免向其他测试的临时世界发送输入。
		UWorld* World = GEditor->PlayWorld;
		return World ? Cast<ACastlePlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	template<class TWidget>
	TWidget* FindWidget(ACastlePlayerController& Player)
	{
		TArray<UUserWidget*> Widgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(&Player, Widgets, TWidget::StaticClass(), true);
		return Widgets.Num() == 1 ? Cast<TWidget>(Widgets[0]) : nullptr;
	}

	/** 按控制器同一视觉过滤条件验证建筑整体高亮集合与路径编辑门控。 */
	bool CheckBuildingHover(FAutomationTestBase& Test, ACastlePlayerController& Player,
		ACastleBuildingBase& Building, const TCHAR* Message, const bool bExpected)
	{
		auto* Highlight = Player.GetWorld()->GetSubsystem<UCastleHoverHighlightSubsystem>();
		if (!Test.TestNotNull(TEXT("建筑悬停子系统"), Highlight)) { return false; }
		const TArray<UPrimitiveComponent*> Current = Highlight->GetHighlightedPrimitives();
		if (!bExpected)
		{
			return Test.TestTrue(Message, Current.IsEmpty());
		}

		TArray<UPrimitiveComponent*> Components;
		Building.GetComponents<UPrimitiveComponent>(Components);
		Components.RemoveAll([&Building](const UPrimitiveComponent* Component)
		{
			return !IsValid(Component) || !Component->IsRegistered() || !Component->IsVisible()
				|| UBuildingGameplayRootComponent::IsInGameplayHierarchy(Component)
				|| Component == Building.GetRoomTraitComponent()
				|| Component->GetFName() == TEXT("PrototypeLabel")
				|| Component->GetFName() == TEXT("PrototypeLabelPlate");
		});
		bool bSameComponentSet = !Components.IsEmpty() && Current.Num() == Components.Num();
		for (const UPrimitiveComponent* Component : Components)
		{
			bSameComponentSet &= Current.Contains(Component);
		}
		return Test.TestTrue(Message, bSameComponentSet);
	}

	/** 正式 HUD 的齿轮和设置按钮切换模式，再检查鼠标旋转门控与实际垂直输入。 */
	class FCheckControlModes final : public IAutomationLatentCommand
	{
	public:
		explicit FCheckControlModes(FAutomationTestBase& InTest) : Test(InTest) {}

		bool Update() override
		{
			auto* Player = GetViewportPlayer();
			// 请求在编辑器 Tick 启动，首个潜伏帧不保证正式玩家已经就绪。
			if (!Player && FPlatformTime::Seconds() < StartupDeadline) { return false; }
			if (!Test.TestNotNull(TEXT("真实视口控制器"), Player)) { return true; }
			switch (Stage++)
			{
			case -2:
			case -1:
				// PIE 首帧先完成视口捕获及输入栈初始化，避免随后到达的焦点事件清空模拟轴值。
				return false;
			case 0:
				Test.TestTrue(TEXT("开局经典模式隐藏鼠标并显示准星"), Player->GetControlMode() == ECastleControlMode::Classic
					&& !Player->bShowMouseCursor && Player->ShouldDrawCenterCrosshair());
				Test.TestTrue(TEXT("经典主交互为右键，拆除为左键"), Player->GetPrimaryPlaceInteractKey() == EKeys::RightMouseButton
					&& Player->GetDemolishKey() == EKeys::LeftMouseButton);
				return false;
			case 1:
			{
				Test.TestTrue(TEXT("经典模式允许鼠标旋转相机"), Player->CanRotateCameraFromMouse());
				auto* HUD = FindWidget<UCastleDirectorHUDWidget>(*Player);
				auto* Settings = HUD ? Cast<UCastleHUDIconButtonWidget>(HUD->GetWidgetFromName(TEXT("SettingsButton"))) : nullptr;
				if (!Test.TestNotNull(TEXT("正式 HUD 齿轮"), Settings)) { return true; }
				Settings->OnButtonClicked.Broadcast();
				auto* Panel = FindWidget<UCastleControlSettingsWidget>(*Player);
				if (!Test.TestNotNull(TEXT("齿轮挂载正式设置 WBP"), Panel)) { return true; }
				auto* ClassicKeys = Cast<UTextBlock>(Panel->GetWidgetFromName(TEXT("ClassicKeys")));
				auto* DesignKeys = Cast<UTextBlock>(Panel->GetWidgetFromName(TEXT("DesignKeys")));
				Test.TestTrue(TEXT("设置文案说明经典模式 E 交互与 Space/Ctrl 升降"), ClassicKeys
					&& ClassicKeys->GetText().ToString().Contains(TEXT("E 空岛交互"))
					&& ClassicKeys->GetText().ToString().Contains(TEXT("Space/Ctrl 升降")));
				Test.TestTrue(TEXT("设置文案说明设计模式 Q 下降与 E 上升"), DesignKeys
					&& DesignKeys->GetText().ToString().Contains(TEXT("Q 下降 / E 上升")));
				Test.TestTrue(TEXT("设置在两个操作模式中均说明中括号调速"), ClassicKeys && DesignKeys
					&& ClassicKeys->GetText().ToString().Contains(TEXT("[ / ] 调节飞行速度"))
					&& DesignKeys->GetText().ToString().Contains(TEXT("[ / ] 调节飞行速度")));
				auto* Design = Cast<UButton>(Panel->GetWidgetFromName(TEXT("DesignButton")));
				if (!Test.TestNotNull(TEXT("设计模式按钮已绑定"), Design)) { return true; }
				Test.TestTrue(TEXT("设置是模态界面"), Player->IsModalInputActive() && Player->IsLookInputIgnored());
				CheckSpeedKey(Test, *Player, EKeys::RightBracket, 900.0f);
				Design->OnClicked.Broadcast();
				Test.TestTrue(TEXT("设计模式即时生效"), Player->GetControlMode() == ECastleControlMode::Design
					&& Player->GetPrimaryPlaceInteractKey() == EKeys::LeftMouseButton
					&& Player->GetDemolishKey() == EKeys::RightMouseButton);
				auto* Close = Cast<UButton>(Panel->GetWidgetFromName(TEXT("CloseButton")));
				if (!Test.TestNotNull(TEXT("返回游戏按钮已绑定"), Close)) { return true; }
				Test.TestFalse(TEXT("当前设计模式按钮使用禁用选中画刷"), Design->GetIsEnabled());
				Close->OnClicked.Broadcast();
				return false;
			}
			case 2:
			{
				Test.TestTrue(TEXT("关闭设置后显示鼠标且隐藏准星"), Player->bShowMouseCursor
					&& !Player->ShouldDrawCenterCrosshair() && !Player->IsModalInputActive());
				int32 Width, Height;
				Player->GetViewportSize(Width, Height);
				Player->SetMouseLocation(Width / 4, Height / 3);
				float X = 0.0f, Y = 0.0f;
				Test.TestTrue(TEXT("设计模式瞄准读取偏离视口中心的鼠标位置"), Player->TryGetAimScreenPosition(X, Y)
					&& FMath::IsNearlyEqual(X, static_cast<float>(Width / 4), 2.0f)
					&& FMath::IsNearlyEqual(Y, static_cast<float>(Height / 3), 2.0f));
				return false;
			}
			case 3:
			{
				auto* FlightPawn = Cast<ACastleFlightPawn>(Player->GetPawn());
				if (!Test.TestNotNull(TEXT("MainMap 正式飞行 Pawn"), FlightPawn)) { return true; }
				Test.TestFalse(TEXT("设计模式松开中键时不允许鼠标旋转相机"), Player->CanRotateCameraFromMouse());
				Test.TestEqual(TEXT("设计模式相机旋转门控绑定中键"),
					CastleControlInput::GetBindings(ECastleControlMode::Design).Get(ECastleWorldInputAction::HoldCameraLook),
					EKeys::MiddleMouseButton);
				SendInput(*Player, EKeys::MiddleMouseButton, IE_Pressed, 1.0f);
				Player->PlayerTick(InputTickSeconds);
				Test.TestTrue(TEXT("设计模式按住中键时允许鼠标旋转相机"), Player->CanRotateCameraFromMouse());
				SendInput(*Player, EKeys::MiddleMouseButton, IE_Released, 0.0f);
				// MainMap 导演的 E 绑定仍在输入栈中，测到正向向量即证明该绑定向飞行 Pawn 透传。
				CheckFlightKey(Test, *Player, *FlightPawn, EKeys::Q, -1.0);
				CheckFlightKey(Test, *Player, *FlightPawn, EKeys::E, 1.0);
				CheckFlightKey(Test, *Player, *FlightPawn, EKeys::SpaceBar, 0.0);
				CheckFlightKey(Test, *Player, *FlightPawn, EKeys::LeftControl, 0.0);
				CheckSpeedKey(Test, *Player, EKeys::RightBracket, 1350.0f);
				CheckSpeedKey(Test, *Player, EKeys::LeftBracket, 900.0f);
				Player->SetControlMode(ECastleControlMode::Classic, false);
				return false;
			}
			case 4:
			{
				auto* FlightPawn = Cast<ACastleFlightPawn>(Player->GetPawn());
				if (!Test.TestNotNull(TEXT("经典模式正式飞行 Pawn"), FlightPawn)) { return true; }
				CheckFlightKey(Test, *Player, *FlightPawn, EKeys::SpaceBar, 1.0);
				CheckFlightKey(Test, *Player, *FlightPawn, EKeys::LeftControl, -1.0);
				CheckFlightKey(Test, *Player, *FlightPawn, EKeys::Q, 0.0);
				CheckFlightKey(Test, *Player, *FlightPawn, EKeys::E, 0.0);
				for (const float Speed : {550.0f, 350.0f, 200.0f, 200.0f})
				{
					CheckSpeedKey(Test, *Player, EKeys::LeftBracket, Speed);
				}
				for (const float Speed : {350.0f, 550.0f, 900.0f, 1350.0f, 1900.0f, 2600.0f, 3600.0f, 3600.0f})
				{
					CheckSpeedKey(Test, *Player, EKeys::RightBracket, Speed);
				}
				const UCapsuleComponent* Capsule = FlightPawn->GetCapsuleComponent();
				Test.TestTrue(TEXT("飞行碰撞继续查询 WorldDynamic 对 Pawn 的重叠"), Capsule->GetGenerateOverlapEvents()
					&& Capsule->GetCollisionEnabled() == ECollisionEnabled::QueryOnly && Capsule->GetCollisionObjectType() == ECC_Pawn);
				for (int32 Channel = 0; Channel < ECC_MAX; ++Channel)
				{
					Test.TestEqual(FString::Printf(TEXT("观察者胶囊通道 %d 的响应"), Channel),
						Capsule->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Channel)),
						Channel == ECC_WorldDynamic ? ECR_Overlap : ECR_Ignore);
				}
				// 连续多档输入在同一个测试帧中执行。将临时 HUD 指针放到下一次潜伏更新，
				// 让真实 PlayerInput 队列按常规帧序处理独占按键。
				return false;
			}
			case 5:
				SendInput(*Player, EKeys::LeftAlt, IE_Pressed, 1.0f);
				return false;
			case 6:
				Test.TestTrue(TEXT("经典按住左 Alt 释放 HUD 指针并停用视角"), Player->bShowMouseCursor
					&& !Player->CanRotateCameraFromMouse() && !Player->ShouldDrawCenterCrosshair());
				SendInput(*Player, EKeys::LeftAlt, IE_Released, 0.0f);
				return false;
			case 7:
				Test.TestTrue(TEXT("松开左 Alt 恢复经典准星与自由视角"), !Player->bShowMouseCursor
					&& Player->CanRotateCameraFromMouse() && Player->ShouldDrawCenterCrosshair());
				Press(*Player, EKeys::O);
				return false;
			case 8:
				Test.TestNotNull(TEXT("O 键打开设置 WBP"), FindWidget<UCastleControlSettingsWidget>(*Player));
				Press(*Player, EKeys::Escape);
				return false;
			case 9:
				Test.TestNull(TEXT("Esc 关闭设置 WBP"), FindWidget<UCastleControlSettingsWidget>(*Player));
				return true;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase& Test;
		const double StartupDeadline = FPlatformTime::Seconds() + 20.0;
		int32 Stage = -2;
	};

	/** MainMap 驱动真实验收空岛输入栈，验证 E 上升不离岛且设计主键点击离岛门。 */
	class FCheckDesignSkyIslandRouting final : public IAutomationLatentCommand
	{
	public:
		explicit FCheckDesignSkyIslandRouting(FAutomationTestBase& InTest) : Test(InTest) {}

		bool Update() override
		{
			UWorld* World = GEditor->PlayWorld;
			auto* Player = World ? Cast<ACastlePlayerController>(World->GetFirstPlayerController()) : nullptr;
			if (!Player || !World->HasBegunPlay())
			{
				if (FPlatformTime::Seconds() < Deadline) { return false; }
				Test.AddError(TEXT("设计模式空岛交互测试未能取得 PIE 玩家控制器。"));
				return true;
			}
			auto* Director = World->GetSubsystem<UCastleDirectorSubsystem>();
			if (!Director || !Director->IsRunning())
			{
				if (FPlatformTime::Seconds() < Deadline) { return false; }
				Test.AddError(TEXT("设计模式空岛交互测试未能取得正式导演。"));
				return true;
			}
			switch (Stage++)
			{
			case 0:
			{
				Player->SetControlMode(ECastleControlMode::Design, false);
				ReturnLocation = Player->GetPawn()->GetActorLocation();
				FOutputDeviceNull Output;
				Test.TestTrue(TEXT("调用 MainMap 赌博空岛验收命令"), IConsoleManager::Get().ProcessUserConsoleInput(
					TEXT("CastleTestGambleSkyIsland"), Output, World));
				return false;
			}
			case 1:
			{
				const TActorIterator<ACastleGambleAcceptanceIsland> IslandIterator(World);
				if (!Test.TestTrue(TEXT("验收命令已生成赌博空岛"), static_cast<bool>(IslandIterator))) { return true; }
				Island = *IslandIterator;
				Test.TestTrue(TEXT("编辑阶段验收岛已登记给玩家控制器"),
					Director->GetPhase() == ECastleDirectorPhase::Editing && Player->GetInteractiveSkyIsland() == Island.Get());
				Departure = Island->FindComponentByClass<UCastleSkyIslandDepartureComponent>();
				if (!Test.TestNotNull(TEXT("验收岛离开传送门组件"), Departure.Get())) { return true; }
				Camera = World->SpawnActor<ACameraActor>();
				if (!Test.TestNotNull(TEXT("设计模式空岛交互测试相机"), Camera.Get())) { return true; }
				// 先对准微缩地块范围外的平台空地，验证左键不会落入主棋盘建造流程。
				const FVector GroundTarget = Island->GetActorLocation() + FVector(0.0, 900.0, 0.0);
				return !Test.TestTrue(TEXT("空岛平台空地点投影到设计模式鼠标坐标"),
					AimDesignPointerAt(*Player, *Camera, GroundTarget + FVector::UpVector * 700.0, GroundTarget));
			}
			case 2:
			{
				FVector RayStart, RayDirection;
				FHitResult Hit;
				const FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleDesignSkyIslandAcceptanceTest), false, Player->GetPawn());
				if (!Test.TestTrue(TEXT("鼠标射线实际命中微缩棋盘外的平台空地"), Player->TryGetAimWorldRay(RayStart, RayDirection)
					&& World->LineTraceSingleByChannel(Hit, RayStart, RayStart + RayDirection * 100000.0, ECC_Visibility, Query)
					&& Hit.GetActor() == Island.Get() && Hit.GetComponent() != Departure.Get())) { return true; }
				FCastleGridCursorTarget GridTarget;
				Test.TestFalse(TEXT("平台空地点不属于赌博空岛微缩棋盘"),
					Island->TryResolveIslandGridCursorTarget(RayStart, RayStart + RayDirection * 100000.0, GridTarget));
				// 先清掉进岛消息，再验证本次空地点击没有生成新的控制器操作提示。
				GEngine->RemoveOnScreenDebugMessage(20261108);
				Press(*Player, Player->GetPrimaryPlaceInteractKey());
				Player->PlayerTick(InputTickSeconds);
				Test.TestFalse(TEXT("空地点按左键不显示操作提示"), GEngine->OnScreenDebugMessageExists(20261108));
				return false;
			}
			case 3:
			{
				Test.TestTrue(TEXT("空岛平台空地点按左键后仍留在验收空岛"),
					Island.IsValid() && Player->GetInteractiveSkyIsland() == Island.Get()
					&& Director->GetPhase() == ECastleDirectorPhase::Editing);
				if (!Test.TestNotNull(TEXT("空地点按后保留离开门"), Departure.Get())) { return true; }
				const FVector Target = Departure->GetComponentLocation();
				return !Test.TestTrue(TEXT("离开门投影到设计模式鼠标坐标"), AimDesignPointerAt(
					*Player, *Camera, Target - Departure->GetForwardVector() * 420.0, Target));
			}
			case 4:
			{
				FVector RayStart, RayDirection;
				FHitResult Hit;
				const FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleDesignSkyIslandAcceptanceDoorTest), false, Player->GetPawn());
				Test.TestTrue(TEXT("鼠标射线实际命中离开门"), Player->TryGetAimWorldRay(RayStart, RayDirection)
					&& World->LineTraceSingleByChannel(Hit, RayStart, RayStart + RayDirection * 100000.0, ECC_Visibility, Query)
					&& Hit.GetComponent() == Departure.Get());
				// 离开门处于鼠标射线中时，E 仍要越过导演和验收输入层到达飞行 Pawn。
				auto* FlightPawn = Cast<ACastleFlightPawn>(Player->GetPawn());
				if (!Test.TestNotNull(TEXT("空岛上的正式飞行 Pawn"), FlightPawn)) { return true; }
				CheckFlightKey(Test, *Player, *FlightPawn, EKeys::E, 1.0);
				Test.TestTrue(TEXT("设计模式 E 不销毁验收空岛"), Island.IsValid());
				Test.TestEqual(TEXT("设计模式 E 不改变导演编辑阶段"), Director->GetPhase(), ECastleDirectorPhase::Editing);
				Test.TestTrue(TEXT("验收空岛仍由控制器登记"), Player->GetInteractiveSkyIsland() == Island.Get());
				return false;
			}
			case 5:
				if (!Test.TestTrue(TEXT("派发完 E 后玩家仍在验收空岛"), Island.IsValid())) { return true; }
				Press(*Player, Player->GetPrimaryPlaceInteractKey());
				return false;
			case 6:
				if (Island.IsValid() && FPlatformTime::Seconds() < Deadline) { --Stage; return false; }
				Test.TestFalse(TEXT("设计模式鼠标左键触发离开门并销毁验收岛"), Island.IsValid());
				const FVector ReturnedLocation = Player->GetPawn()->GetActorLocation();
				Test.TestTrue(FString::Printf(TEXT("左键离岛返回原飞行位置，角色胶囊因落地碰撞允许 100 cm 容差，原位置 %s，返回位置 %s"),
					*ReturnLocation.ToCompactString(), *ReturnedLocation.ToCompactString()), ReturnedLocation.Equals(ReturnLocation, 100.0));
				// 恢复 PIE 飞行视角并销毁测试相机，随后由 StopPIE 退出地图。
				Player->SetViewTarget(Player->GetPawn());
				Camera->Destroy();
				return true;
			default:
				return true;
			}
		}

	private:
		FAutomationTestBase& Test;
		double Deadline = FPlatformTime::Seconds() + 55.0;
		int32 Stage = 0;
		FVector ReturnLocation = FVector::ZeroVector;
		TWeakObjectPtr<ACastleGambleAcceptanceIsland> Island;
		TWeakObjectPtr<UCastleSkyIslandDepartureComponent> Departure;
		TWeakObjectPtr<ACameraActor> Camera;
	};

	/** 在 MainMap 放置正式传送门房间，比较经典验收岛与设计模式对同一门体的实际输入路由。 */
	class FCheckDesignRoomPortalRouting final : public IAutomationLatentCommand
	{
	public:
		explicit FCheckDesignRoomPortalRouting(FAutomationTestBase& InTest) : Test(InTest) {}

		bool Update() override
		{
			auto* Player = GetViewportPlayer();
			auto* Director = Player ? Player->GetWorld()->GetSubsystem<UCastleDirectorSubsystem>() : nullptr;
			if (!Player || !Director || !Director->IsRunning())
			{
				if (FPlatformTime::Seconds() < Deadline) { return false; }
				Test.AddError(TEXT("房间传送门输入验收未取得 MainMap 正式玩家和导演。"));
				return true;
			}
			UWorld& World = *Player->GetWorld();
			const int32 CurrentStage = Stage++;
			if (CurrentStage == 0)
			{
				Player->SetControlMode(ECastleControlMode::Design, false);
				const TActorIterator<ACastleBoard> BoardIterator(&World);
				if (!Test.TestTrue(TEXT("MainMap 已生成可构筑棋盘"), static_cast<bool>(BoardIterator))) { return true; }
				auto* Board = *BoardIterator;
				// MainMap 从塔侧格起建。使用首层 -X 格的实际射线和色号构造传送门卡。
				const FIntVector Base = Board->GetBoardCenterCoordinate();
				const FVector Center = Board->GetGridWorldLocation(Base);
				const FVector Probe = Board->GetActorTransform().TransformVector(FVector(-2.0 * Board->CellSize, 0.0, 0.0));
				FCastleGridCursorTarget SideTarget;
				if (!Test.TestTrue(TEXT("首层 -X 射线命中正式塔侧格"),
					Board->TryResolveGridCursorTarget(Center + Probe, Center, SideTarget)
					&& SideTarget.Surface == ECastleGridCursorSurface::MainTowerSide
					&& SideTarget.Face == ECastleGridFace::NegativeX)) { return true; }
				const auto* SideCell = Board->FindTowerSideCell(Base, SideTarget.Face);
				if (!Test.TestNotNull(TEXT("取得塔侧格颜色和数字"), SideCell)) { return true; }
				const auto* Definition = Board->FindBuildingDefinition(ECastleBuildingType::NormalRoom);
				if (!Test.TestNotNull(TEXT("棋盘使用的普通房定义"), Definition)) { return true; }
				FCastleBuildingDescriptor Card;
				FText Reason;
				if (!Definition->CreateDescriptor(1, SideCell->Color, SideCell->Number, Card, Reason))
				{
					Test.AddError(Reason.ToString());
					return true;
				}
				// 使用背包正常放置，并等同于飞入完成后再读取已经注册的门体碰撞。
				Card.RoomTrait = ECastleRoomTrait::SkyIslandPortal;
				int32 BackpackIndex;
				if (!Board->PrototypeBackpack->TryAddItem(Card, BackpackIndex, Reason))
				{
					Test.AddError(Reason.ToString());
					return true;
				}
				Room = Board->PlaceBackpackBuildingAtCursor(SideTarget, BackpackIndex, Reason);
				if (!Test.TestTrue(TEXT("正式放置传送门房间并完成飞入"), CastleBuildingTests::CompletePlacement(Room.Get())))
				{
					Test.AddError(Reason.ToString());
					return true;
				}
				auto* Door = Room->GetRoomTraitComponent();
				auto* Camera = World.SpawnActor<ACameraActor>();
				if (!Test.TestNotNull(TEXT("放置后生成可用门体"), Door)
					|| !Test.TestNotNull(TEXT("测试观察相机"), Camera)) { return true; }
				const FVector Target = Door->GetComponentLocation();
				// 相机在门体前的室内位置，防止屋外墙面先截住指针射线。
				return !Test.TestTrue(TEXT("房间门体投影到设计模式鼠标坐标"),
					AimDesignPointerAt(*Player, *Camera, Target - Door->GetForwardVector() * 80.0, Target));
			}
			if (CurrentStage == 1)
			{
				// 保留指向房间门的测试相机，进入真实验收岛以推入其高优先级 E 输入组件。
				Player->SetControlMode(ECastleControlMode::Classic, false);
				FOutputDeviceNull Output;
				return !Test.TestTrue(TEXT("为经典 E 优先级启动赌博验收岛"), IConsoleManager::Get().ProcessUserConsoleInput(
					TEXT("CastleTestGambleSkyIsland"), Output, &World));
			}
			if (CurrentStage == 3)
			{
				// 按验收岛离开流程返回后再恢复设计模式，避免测试的两次 E 共用验收输入层。
				if (Player->GetInteractiveSkyIsland() && FPlatformTime::Seconds() < Deadline) { --Stage; return false; }
				if (!Test.TestNull(TEXT("经典优先级测试的验收岛已注销"), Player->GetInteractiveSkyIsland())) { return true; }
				Player->SetControlMode(ECastleControlMode::Design, false);
				return false;
			}

			FVector RayStart, RayDirection;
			FHitResult Hit;
			const FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleDesignRoomPortalTest), false, Player->GetPawn());
			if (!Test.TestTrue(TEXT("当前模式的真实射线直接命中房间门体"), Room.IsValid()
				&& Player->TryGetAimWorldRay(RayStart, RayDirection)
				&& World.LineTraceSingleByChannel(Hit, RayStart, RayStart + RayDirection * 100000.0, ECC_Visibility, Query)
				&& Hit.GetComponent() == Room->GetRoomTraitComponent())) { return true; }
			if (CurrentStage == 2)
			{
				auto* AcceptanceIsland = Cast<ACastleGambleAcceptanceIsland>(Player->GetInteractiveSkyIsland());
				if (!Test.TestNotNull(TEXT("当前经典交互由验收岛接管"), AcceptanceIsland)) { return true; }
				// 验收岛正在处理经典 E，即使准星直射房间门也不能在同一次输入中再次传送。
				Press(*Player, EKeys::E);
				Player->PlayerTick(InputTickSeconds);
				if (!Test.TestTrue(TEXT("经典验收岛 E 保留岛内路由与房间门体"), Director->GetPhase() == ECastleDirectorPhase::Editing
					&& Player->GetInteractiveSkyIsland() == AcceptanceIsland && Room->GetRoomTraitComponent()
					&& Room->GetRoomTraitComponent()->IsPortalAvailable())) { return true; }
				// 复用正式的延迟离岛回调结束夹具，本用例的离岛键覆盖由空岛测试负责。
				AcceptanceIsland->OnDepartureRequested.Broadcast();
				return false;
			}
			auto* FlightPawn = Cast<ACastleFlightPawn>(Player->GetPawn());
			if (!Test.TestNotNull(TEXT("房间门前的正式飞行 Pawn"), FlightPawn)) { return true; }
			CheckFlightKey(Test, *Player, *FlightPawn, EKeys::E, 1.0);
			Test.TestTrue(TEXT("设计模式 E 保留编辑阶段与可用门体"),
				Director->GetPhase() == ECastleDirectorPhase::Editing && Room->GetRoomTraitComponent()
				&& Room->GetRoomTraitComponent()->IsPortalAvailable());
			Press(*Player, EKeys::LeftMouseButton);
			Player->PlayerTick(InputTickSeconds);
			Test.TestTrue(TEXT("设计模式左键经主交互进入额外空岛并消耗门体"),
				Director->GetPhase() == ECastleDirectorPhase::SkyIsland && Room->GetRoomTrait() == ECastleRoomTrait::None);
			return true;
		}

	private:
		FAutomationTestBase& Test;
		const double Deadline = FPlatformTime::Seconds() + 35.0;
		int32 Stage = 0;
		TWeakObjectPtr<ACastleBuildingBase> Room;
	};

	/** 当前地图每次由自动化重载。使用正式资产生成普通房和合成塔，保留真实可见性碰撞。 */
	class FCheckDemolishMenu final : public IAutomationLatentCommand
	{
	public:
		explicit FCheckDemolishMenu(FAutomationTestBase& InTest) : Test(InTest) {}

		bool Update() override
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test.AddError(FString::Printf(TEXT("建筑拆除测试等待阶段 %d 超时。"), Stage));
				return true;
			}
			auto* Player = GetViewportPlayer();
			if (!Player) { return false; }
			if (!Board)
			{
				for (TActorIterator<ACastleBoard> It(Player->GetWorld()); It; ++It) { Board = *It; break; }
				if (!Board) { return false; }
				return !Prepare(*Player);
			}
			switch (Stage)
			{
			case 0:
				if (!Room->CanReceiveBuildingTrigger() || !FirstTower->CanReceiveBuildingTrigger()) { return false; }
				if (FirstTower->BuildingMerge->bIsPlaying) { return false; }
				if (!PlaceSecondTower()) { return true; }
				++Stage;
				return false;
			case 1:
				Tower = Board->GetBuildingAt(TowerCell);
				if (!Tower || !Tower->CanReceiveBuildingTrigger() || Tower->GetBuildingLevel() != 2
					|| Tower->BuildingMerge->bIsPlaying) { return false; }
				AimAt(RoomCell);
				++Stage;
				return false;
			case 2:
			{
				auto* Route = Cast<ACastlePatrolRoute>(UGameplayStatics::GetActorOfClass(
					Player->GetWorld(), ACastlePatrolRoute::StaticClass()));
				if (!Test.TestNotNull(TEXT("MainMap 路径编辑入口"), Route)) { return true; }
				auto* Building = Board->GetBuildingAt(RoomCell);
				if (!Test.TestNotNull(TEXT("路径门控使用真实普通房"), Building)) { return true; }
				switch (PathHoverSubstage)
				{
				case 0:
					if (!CheckBuildingHover(Test, *Player, *Building, TEXT("经典准星命中可拆除房间后高亮全部可见外观"), true))
					{
						return true;
					}
					Press(*Player, EKeys::P);
					PathHoverSubstage = 1;
					return false;
				case 1:
					if (!Test.TestTrue(TEXT("P 键进入正式路径编辑模式"), Route->IsEditing())
						|| !CheckBuildingHover(Test, *Player, *Building, TEXT("路径编辑期间清空建筑悬停描边"), false))
					{
						return true;
					}
					Press(*Player, EKeys::P);
					PathHoverSubstage = 2;
					return false;
				case 2:
					if (!Test.TestFalse(TEXT("再次按 P 退出路径编辑"), Route->IsEditing())
						|| !CheckBuildingHover(Test, *Player, *Building, TEXT("退出路径编辑后恢复建筑悬停描边"), true))
					{
						return true;
					}
					SendInput(*Player, EKeys::LeftAlt, IE_Pressed, 1.0f);
					PathHoverSubstage = 3;
					return false;
				case 3:
					if (!Test.TestFalse(TEXT("左 Alt 临时 HUD 指针期间不能旋转视角"),
						Player->CanRotateCameraFromMouse())
						|| !CheckBuildingHover(Test, *Player, *Building, TEXT("左 Alt 临时 HUD 指针期间清空描边"), false))
					{
						return true;
					}
					SendInput(*Player, EKeys::LeftAlt, IE_Released, 0.0f);
					PathHoverSubstage = 4;
					return false;
				case 4:
					if (!CheckBuildingHover(Test, *Player, *Building, TEXT("松开左 Alt 后恢复建筑悬停描边"), true))
					{
						return true;
					}
					Press(*Player, Player->GetDemolishKey());
					++Stage;
					return false;
				}
				return true;
			}
			case 3:
				if (!CheckBuildingHover(Test, *Player, *Room, TEXT("拆除菜单打开后清空原建筑悬停描边"), false)) { return true; }
				if (!CheckCard(*Player, *Room, RoomCell)) { return true; }
				Test.TestTrue(TEXT("经典菜单显示鼠标并停用相机鼠标轴"), Player->bShowMouseCursor && !Player->CanRotateCameraFromMouse());
				if (!ClickDemolish(*Player)) { return true; }
				Test.TestTrue(TEXT("满建筑栏后原建筑与菜单仍存在"), Board->GetBuildingAt(RoomCell) == Room
					&& Player->GetBuildingDemolishMenu() && Board->PrototypeBackpack->GetItemCount() == 10);
				if (!RemoveBackpackCard()) { return true; }
				if (!CheckRecovered(*Player, *Room, RoomCell)) { return true; }
				++Stage;
				Player->SetControlMode(ECastleControlMode::Design, false);
				AimAt(TowerCell);
				return false;
			case 4:
			{
				int32 Width, Height;
				Player->GetViewportSize(Width, Height);
				Player->SetMouseLocation(Width / 2, Height / 2);
				if (DesignHoverSubstage == 0)
				{
					DesignHoverSubstage = 1;
					return false;
				}
				if (DesignHoverSubstage == 1)
				{
					if (!CheckBuildingHover(Test, *Player, *Tower,
						TEXT("设计模式系统鼠标命中升级塔后高亮全部可见外观"), true))
					{
						return true;
					}
					Press(*Player, Player->GetDemolishKey());
					++Stage;
					return false;
				}
				return true;
			}
			case 5:
			{
				if (!CheckBuildingHover(Test, *Player, *Tower, TEXT("设计模式拆除菜单打开后清空建筑描边"), false)) { return true; }
				if (!CheckCard(*Player, *Tower, TowerCell)) { return true; }
				// 这段地图从未开店。使用菜单同一摄影请求，等待真实 RHI 生成一级建筑画面。
				auto* Studio = Player->GetWorld()->GetSubsystem<UCastleCardPortraitSubsystem>();
				FText PortraitReason;
				auto* Portrait = Studio ? Studio->RequestPortrait(TowerCard, Board, PortraitReason) : nullptr;
				if (!Test.TestNotNull(TEXT("未开商店时已提交独立一级摄影请求"), Portrait)) { return true; }
				if (Portrait->GetState() == ECastleCardPortraitState::Pending) { return false; }
				if (!Test.TestEqual(TEXT("商店外独立摄影完成"), Portrait->GetState(), ECastleCardPortraitState::Ready))
				{
					Test.AddError(Portrait->GetFailureReason().ToString());
					return true;
				}
				TArray<FFloat16Color> Pixels;
				auto* Texture = Portrait->GetTexture();
				if (!Test.TestTrue(TEXT("独立牌的摄影纹理可真实读回"), Texture
					&& Texture->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(Pixels))) { return true; }
				Test.TestTrue(TEXT("独立牌含不透明、带色的建筑像素"), Pixels.ContainsByPredicate([](const FFloat16Color& Pixel)
				{
					return Pixel.A.GetFloat() < 0.5f && Pixel.R.GetFloat() + Pixel.G.GetFloat() + Pixel.B.GetFloat() > 0.05f;
				}));
				if (!RemoveBackpackCard()) { return true; }
				if (!CheckRecovered(*Player, *Tower, TowerCell) || !RemoveBackpackCard()) { return true; }
				const auto* Survivor = Board->GetBuildingAt(TowerCell + FIntVector(1, 0, 0));
				Test.TestTrue(TEXT("只拆除射线命中的格，另一格原地降为一级"), Survivor && Survivor->GetBuildingLevel() == 1);
				// 用刚回收建筑的空格验证设计主交互。视口中心与鼠标命中故意分离。
				{
					int32 Index;
					FText Reason;
					if (!Board->PrototypeBackpack->TryAddItem(RoomCard, Index, Reason))
					{
						Test.AddError(Reason.ToString());
						return true;
					}
					const FVector Target = Board->GetGridWorldLocation(RoomCell);
					Camera->SetActorLocationAndRotation(Target + FVector(0, -400, 1500), FRotator(-90, 0, 0));
					Press(*Player, EKeys::Zero);
				}
				++Stage;
				return false;
			}
			case 6:
			{
				FVector2D Mouse;
				if (!Test.TestTrue(TEXT("空格投影到设计模式的偏心鼠标坐标"),
					Player->ProjectWorldLocationToScreen(Board->GetGridWorldLocation(RoomCell), Mouse))) { return true; }
				Player->SetMouseLocation(FMath::RoundToInt(Mouse.X), FMath::RoundToInt(Mouse.Y));
				Press(*Player, Player->GetPrimaryPlaceInteractKey());
				++Stage;
				return false;
			}
			case 7:
				Test.TestTrue(TEXT("设计左键在鼠标指向的地块放置并扣除一张卡"),
					Board->GetBuildingAt(RoomCell) && Board->PrototypeBackpack->GetItemCount() == 9);
				Player->SetControlMode(ECastleControlMode::Classic, false);
				return true;
			default:
				return true;
			}
		}

	private:
		bool Prepare(ACastlePlayerController& Player)
		{
			FText Reason;
			auto* RoomDef = Board->FindBuildingDefinition(ECastleBuildingType::NormalRoom);
			auto* TowerDef = Board->FindBuildingDefinition(ECastleBuildingType::ArcherTower);
			if (!Test.TestNotNull(TEXT("普通房定义"), RoomDef) || !Test.TestNotNull(TEXT("箭塔定义"), TowerDef)) { return false; }
			if (!RoomDef->CreateDescriptor(1, ECastleTileColor::Red, 3, RoomCard, Reason)
				|| !TowerDef->CreateDescriptor(1, ECastleTileColor::Red, 4, TowerCard, Reason))
			{
				Test.AddError(Reason.ToString());
				return false;
			}
			RoomCell = Board->GridOrigin + FIntVector(2, 2, 0);
			TowerCell = Board->GridOrigin + FIntVector(2, 4, 0);
			// 与正式放置相同，房卡和塔卡必须匹配地块色号或数字。只调整临时测试世界。
			FCastleGroundGenerationResult Ground;
			for (int32 X = 0; X < Board->GridSize.X; ++X)
			{
				for (int32 Y = 0; Y < Board->GridSize.Y; ++Y)
				{
					FCastleGroundCell Cell;
					if (!Board->TryGetGroundCell(Board->GridOrigin + FIntVector(X, Y, 0), Cell))
					{
						Test.AddError(TEXT("TestMap1 地面夹具缺少预期棋盘格。"));
						return false;
					}
					if (Cell.Coordinate == RoomCell || Cell.Coordinate == TowerCell
						|| Cell.Coordinate == TowerCell + FIntVector(1, 0, 0))
					{
						Cell.Color = ECastleTileColor::Red;
						Cell.Number = Cell.Coordinate == RoomCell ? RoomCard.Number : TowerCard.Number;
					}
					Ground.Cells.Add(Cell);
				}
			}
			if (!Board->ApplyGroundGenerationResult(Ground, Reason))
			{
				Test.AddError(Reason.ToString());
				return false;
			}
			Room = Board->PlaceBuilding(RoomCell, RoomCard, false, Reason);
			if (!Room) { Test.AddError(Reason.ToString()); return false; }
			FirstTower = Board->PlaceBuilding(TowerCell, TowerCard, false, Reason);
			if (!FirstTower) { Test.AddError(Reason.ToString()); return false; }
			while (Board->PrototypeBackpack->GetItemCount() < 10)
			{
				int32 AddedIndex;
				if (!Board->PrototypeBackpack->TryAddItem(RoomCard, AddedIndex, Reason))
				{
					Test.AddError(Reason.ToString());
					return false;
				}
			}
			Camera = Player.GetWorld()->SpawnActor<ACameraActor>();
			if (!Test.TestNotNull(TEXT("测试观察相机"), Camera)) { return false; }
			Player.SetViewTarget(Camera);
			return true;
		}

		bool PlaceSecondTower()
		{
			FText Reason;
			if (Board->PlaceBuilding(TowerCell + FIntVector(1, 0, 0), TowerCard, true, Reason)) { return true; }
			Test.AddError(Reason.ToString());
			return false;
		}

		void AimAt(const FIntVector& TargetCell)
		{
			Camera->SetActorLocationAndRotation(Board->GetGridWorldLocation(TargetCell) + FVector(120, 120, 1200), FRotator(-90, 0, 0));
		}

		bool CheckCard(const ACastlePlayerController& Player, const ACastleBuildingBase& Expected, const FIntVector& Cell) const
		{
			const auto* Menu = Player.GetBuildingDemolishMenu();
			if (!Test.TestTrue(TEXT("拆除键命中正式 WBP 的目标建筑与格"), Menu
				&& Menu->GetTargetBuilding() == &Expected && Menu->GetTargetCell() == Cell)) { return false; }
			const auto& Card = Menu->GetBuildingCard()->GetDescriptor();
			const auto Actual = Expected.GetBuildingDescriptor();
			return Test.TestTrue(TEXT("独立卡牌显示同定义同色号的一级回收卡"),
				Card.Level == 1 && Card.Color == Actual.Color && Card.Number == Actual.Number && Card.Definition == Actual.Definition);
		}

		bool ClickDemolish(const ACastlePlayerController& Player) const
		{
			auto* Button = Cast<UButton>(Player.GetBuildingDemolishMenu()->GetWidgetFromName(TEXT("DemolishButton")));
			if (!Test.TestNotNull(TEXT("正式回收按钮"), Button)) { return false; }
			Button->OnClicked.Broadcast();
			return true;
		}

		bool RemoveBackpackCard() const
		{
			FCastleBuildingDescriptor Removed;
			FText Reason;
			if (Board->PrototypeBackpack->TryRemoveItemAt(9, Removed, Reason)) { return true; }
			Test.AddError(Reason.ToString());
			return false;
		}

		bool CheckRecovered(const ACastlePlayerController& Player, const ACastleBuildingBase& Building, FIntVector Cell) const
		{
			const auto Expected = Building.GetBuildingDescriptor();
			if (!ClickDemolish(Player)) { return false; }
			FCastleBuildingDescriptor Returned;
			return Test.TestTrue(TEXT("回收后目标格空闲、菜单关闭、背包增加一张一级卡"),
				!Board->GetBuildingAt(Cell) && !Player.GetBuildingDemolishMenu()
				&& Board->PrototypeBackpack->GetItemCount() == 10 && Board->PrototypeBackpack->TryGetItemAt(9, Returned)
				&& Returned.Level == 1 && Returned.Number == Expected.Number
				&& Returned.Color == Expected.Color && Returned.Definition == Expected.Definition);
		}

		FAutomationTestBase& Test;
		double Deadline = FPlatformTime::Seconds() + 40.0;
		int32 Stage = 0;
		int32 PathHoverSubstage = 0;
		int32 DesignHoverSubstage = 0;
		ACastleBoard* Board = nullptr;
		ACameraActor* Camera = nullptr;
		ACastleBuildingBase* Room = nullptr;
		ACastleBuildingBase* FirstTower = nullptr;
		ACastleBuildingBase* Tower = nullptr;
		FIntVector RoomCell = FIntVector::ZeroValue;
		FIntVector TowerCell = FIntVector::ZeroValue;
		FCastleBuildingDescriptor TowerCard;
		FCastleBuildingDescriptor RoomCard;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleControlModesTest, "MovingCastle.ControlModes.SettingsAndInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleControlModesTest::RunTest(const FString& Parameters)
{
	if (!CastlePIEAutomation::StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FCheckControlModes(*this));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(CastlePIEAutomation::StopPIE()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDesignSkyIslandInteractionTest, "MovingCastle.ControlModes.DesignSkyIslandInteraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleDesignSkyIslandInteractionTest::RunTest(const FString& Parameters)
{
	// MainMap 提供正式导演、正式 Pawn 输入栈与命令入口，验收岛由现有命令按正式初始化流程生成。
	if (!CastlePIEAutomation::StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FCheckDesignSkyIslandRouting(*this));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(CastlePIEAutomation::StopPIE()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDesignRoomPortalInteractionTest, "MovingCastle.ControlModes.DesignRoomPortalInteraction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleDesignRoomPortalInteractionTest::RunTest(const FString& Parameters)
{
	// 使用正式 MainMap 的门体碰撞与输入栈，覆盖辅助射线单测之外的按键入口。
	if (!CastlePIEAutomation::StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FCheckDesignRoomPortalRouting(*this));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(CastlePIEAutomation::StopPIE()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleDemolishMenuTest, "MovingCastle.ControlModes.DemolishMenu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleDemolishMenuTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("右下角触发时面板完全留在逻辑视口内"),
		UCastleBuildingDemolishMenuWidget::ClampPopupPosition(FVector2D(1278, 718), FVector2D(220, 300), FVector2D(1280, 720)),
		FVector2D(1060, 420));
	if (!CastlePIEAutomation::StartPIE(*this, TEXT("/Game/MovingCastle/Maps/Test/TestMap1"))) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FCheckDemolishMenu(*this));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(CastlePIEAutomation::StopPIE()));
	return true;
}

#endif
