// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Camera/CameraActor.h"
#include "Castle/Actors/CastleBoard.h"
#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/Automation/CastlePIEAutomation.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Castle/Components/CastleBackpackComponent.h"
#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Data/CastleBuildingDefinition.h"
#include "Castle/UI/Cards/CastleCardPortraitSubsystem.h"
#include "Castle/UI/Cards/CastleShopCardWidget.h"
#include "Castle/UI/Common/CastleBuildingDemolishMenuWidget.h"
#include "Castle/UI/Common/CastleControlSettingsWidget.h"
#include "Castle/UI/Common/CastleHUDControls.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Castle/Items/CastleRoomTraitComponent.h"
#include "Castle/Rendering/CastleHoverHighlightSubsystem.h"
#include "Components/Animation/BuildingMergeComponent.h"
#include "Components/Button.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "InputKeyEventArgs.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "TextureResource.h"


namespace
{
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

	/** 正式 HUD 的齿轮和两张设置按钮驱动模式，再检查 Pawn 对鼠标轴输入的实际响应。 */
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
				Rotation = Player->GetControlRotation();
				SendInput(*Player, EKeys::MouseX, IE_Axis, 36.0f);
				return false;
			case 1:
			{
				Test.TestFalse(TEXT("经典自由鼠标轴改变视角"), Player->GetControlRotation().Equals(Rotation));
				auto* HUD = FindWidget<UCastleDirectorHUDWidget>(*Player);
				auto* Settings = HUD ? Cast<UCastleHUDIconButtonWidget>(HUD->GetWidgetFromName(TEXT("SettingsButton"))) : nullptr;
				if (!Test.TestNotNull(TEXT("正式 HUD 齿轮"), Settings)) { return true; }
				Settings->OnButtonClicked.Broadcast();
				auto* Panel = FindWidget<UCastleControlSettingsWidget>(*Player);
				if (!Test.TestNotNull(TEXT("齿轮挂载正式设置 WBP"), Panel)) { return true; }
				auto* Design = Cast<UButton>(Panel->GetWidgetFromName(TEXT("DesignButton")));
				if (!Test.TestNotNull(TEXT("设计模式按钮已绑定"), Design)) { return true; }
				Test.TestTrue(TEXT("设置是模态界面"), Player->IsModalInputActive() && Player->IsLookInputIgnored());
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
				Rotation = Player->GetControlRotation();
				SendInput(*Player, EKeys::MouseX, IE_Axis, 36.0f);
				return false;
			}
			case 3:
				Test.TestTrue(TEXT("设计模式松开中键时鼠标轴不改变视角"), Player->GetControlRotation().Equals(Rotation));
				SendInput(*Player, EKeys::MiddleMouseButton, IE_Pressed, 1.0f);
				SendInput(*Player, EKeys::MouseX, IE_Axis, 36.0f);
				return false;
			case 4:
				Test.TestFalse(TEXT("设计模式按住中键后鼠标轴改变视角"), Player->GetControlRotation().Equals(Rotation));
				SendInput(*Player, EKeys::MiddleMouseButton, IE_Released, 0.0f);
				// 飞行角色会将按键转换成移动组件的最终输入向量，即使地图碰撞阻挡位移也能验证方向。
				SendInput(*Player, EKeys::W, IE_Pressed, 1.0f);
				SendInput(*Player, EKeys::Q, IE_Pressed, 1.0f);
				return false;
			case 5:
				return false;
			case 6:
				if (APawn* Pawn = Player->GetPawn(); Test.TestNotNull(TEXT("正式飞行 Pawn"), Pawn))
				{
					const FVector Input = Pawn->GetLastMovementInputVector();
					Test.TestTrue(TEXT("设计模式 W 与 Q 传入水平和上升飞行输入"), Input.SizeSquared2D() > 0 && Input.Z > 0);
				}
				SendInput(*Player, EKeys::W, IE_Released, 0.0f);
				SendInput(*Player, EKeys::Q, IE_Released, 0.0f);
				SendInput(*Player, EKeys::E, IE_Pressed, 1.0f);
				return false;
			case 7:
				return false;
			case 8:
				Test.TestTrue(TEXT("设计模式 E 传入下降飞行输入"),
					Player->GetPawn() && Player->GetPawn()->GetLastMovementInputVector().Z < 0);
				SendInput(*Player, EKeys::E, IE_Released, 0.0f);
				SendInput(*Player, EKeys::SpaceBar, IE_Pressed, 1.0f);
				return false;
			case 9:
				Test.TestFalse(TEXT("设计模式空格不再触发上升"),
					Player->GetPawn() && Player->GetPawn()->GetLastMovementInputVector().Z > 0);
				SendInput(*Player, EKeys::SpaceBar, IE_Released, 0.0f);
				Player->SetControlMode(ECastleControlMode::Classic, false);
				SendInput(*Player, EKeys::SpaceBar, IE_Pressed, 1.0f);
				return false;
			case 10:
				Test.TestTrue(TEXT("经典模式空格传入上升飞行输入"),
					Player->GetPawn() && Player->GetPawn()->GetLastMovementInputVector().Z > 0);
				SendInput(*Player, EKeys::SpaceBar, IE_Released, 0.0f);
				SendInput(*Player, EKeys::LeftControl, IE_Pressed, 1.0f);
				return false;
			case 11:
				Test.TestTrue(TEXT("经典模式左 Ctrl 传入下降飞行输入"),
					Player->GetPawn() && Player->GetPawn()->GetLastMovementInputVector().Z < 0);
				SendInput(*Player, EKeys::LeftControl, IE_Released, 0.0f);
				SendInput(*Player, EKeys::LeftAlt, IE_Pressed, 1.0f);
				return false;
			case 12:
				Test.TestTrue(TEXT("经典按住左 Alt 释放 HUD 指针并停用视角"), Player->bShowMouseCursor
					&& !Player->CanRotateCameraFromMouse() && !Player->ShouldDrawCenterCrosshair());
				SendInput(*Player, EKeys::LeftAlt, IE_Released, 0.0f);
				return false;
			case 13:
				Test.TestTrue(TEXT("松开左 Alt 恢复经典准星与自由视角"), !Player->bShowMouseCursor
					&& Player->CanRotateCameraFromMouse() && Player->ShouldDrawCenterCrosshair());
				Press(*Player, EKeys::O);
				return false;
			case 14:
				Test.TestNotNull(TEXT("O 键打开设置 WBP"), FindWidget<UCastleControlSettingsWidget>(*Player));
				Press(*Player, EKeys::Escape);
				return false;
			case 15:
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
		FRotator Rotation = FRotator::ZeroRotator;
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
