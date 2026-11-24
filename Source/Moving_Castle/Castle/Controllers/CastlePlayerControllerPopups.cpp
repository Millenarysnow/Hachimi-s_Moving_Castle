// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastlePlayerController.h"

#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "Castle/UI/Common/CastleBuildingDemolishMenuWidget.h"
#include "Castle/UI/Common/CastleControlSettingsWidget.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Kismet/GameplayStatics.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr TCHAR ControlSettingsPath[] = TEXT("/Game/MovingCastle/UI/Common/WBP_ControlSettings");
	constexpr TCHAR DemolishMenuPath[] = TEXT("/Game/MovingCastle/UI/Common/WBP_BuildingDemolishMenu");
}

void ACastlePlayerController::InitializePopupClasses()
{
	// 采用现有 HUD 的硬引用加载方式，保证打包包含正式 WBP 及字体等依赖。
	static const ConstructorHelpers::FClassFinder<UCastleControlSettingsWidget> Settings(ControlSettingsPath);
	static const ConstructorHelpers::FClassFinder<UCastleBuildingDemolishMenuWidget> Demolish(DemolishMenuPath);
	ControlSettingsClass = Settings.Class;
	BuildingDemolishMenuClass = Demolish.Class;
}

void ACastlePlayerController::ToggleControlSettings()
{
	if (ControlSettings)
	{
		RemoveControlSettingsWidget();
		RefreshModalInput(true);
		return;
	}
	// 奖励、商店等模态继续拥有焦点。拆除菜单则在设置成功打开后被替换。
	if (bModalInputActive) { return; }
	auto* Settings = ControlSettingsClass ? CreateWidget<UCastleControlSettingsWidget>(this, ControlSettingsClass) : nullptr;
	if (!Settings || !Settings->BindController(*this) || !Settings->AddToPlayerScreen(45))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("控制器 %s 无法加载、绑定或挂载设置资产 %s。"), *GetPathName(), ControlSettingsPath);
		return;
	}
	ControlSettings = Settings;
	RefreshModalInput(true);
}

void ACastlePlayerController::RemoveControlSettingsWidget()
{
	if (ControlSettings)
	{
		ControlSettings->RemoveFromParent();
		ControlSettings = nullptr;
	}
}

bool ACastlePlayerController::CanUseBuildingDemolishMenu(FText& OutReason) const
{
	if (bModalInputActive)
	{
		OutReason = INVTEXT("关闭当前弹窗后才能拆除建筑。");
		return false;
	}
	if (const auto* Route = Cast<ACastlePatrolRoute>(UGameplayStatics::GetActorOfClass(GetWorld(), ACastlePatrolRoute::StaticClass()));
		Route && Route->IsEditing())
	{
		OutReason = INVTEXT("路径编辑期间无法拆除建筑。");
		return false;
	}
	return IsCastleEditingAllowed(OutReason);
}

bool ACastlePlayerController::IsDemolishableBuilding(const ACastleBuildingBase* Building) const
{
	// 飞入组装和卡牌摄影预览都不是已落定棋盘建筑。最终归属使用棋盘自己的查询。
	return IsValid(ActiveBoard) && IsValid(Building) && Building->CanReceiveBuildingTrigger()
		&& ACastleBoard::FindBoardForBuilding(Building) == ActiveBoard;
}

bool ACastlePlayerController::TryOpenBuildingDemolishAt(FVector2D ScreenPosition)
{
	FText Reason;
	if (!CanUseBuildingDemolishMenu(Reason)) { return false; }
	FVector Origin, Direction;
	if (!DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, Origin, Direction)) { return false; }
	// 单次可见性射线只考虑实际首个遮挡面。飞行 Pawn 和卡牌视图不会抢走场上建筑命中。
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CastleBuildingDemolishClick), false, GetPawn());
	GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * GridCursorTraceDistance, ECC_Visibility, Params);
	auto* Building = Cast<ACastleBuildingBase>(Hit.GetActor());
	if (!IsDemolishableBuilding(Building)) { return false; }

	auto* Menu = BuildingDemolishMenuClass ? CreateWidget<UCastleBuildingDemolishMenuWidget>(this, BuildingDemolishMenuClass) : nullptr;
	if (!Menu || !Menu->ConfigureTarget(*Building, *ActiveBoard, ScreenPosition) || !Menu->AddToPlayerScreen(35))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("控制器 %s 无法加载、绑定或挂载拆除资产 %s。"), *GetPathName(), DemolishMenuPath);
		// 已命中建筑却无法展示菜单时消费此次输入，避免转去撤回微缩盘建筑。
		return true;
	}
	const bool bFirstOpen = !BuildingDemolishMenu;
	RemoveBuildingDemolishWidget();
	BuildingDemolishMenu = Menu;
	RefreshModalInput(true);
	if (bFirstOpen && ControlMode == ECastleControlMode::Classic)
	{
		SetMouseLocation(FMath::RoundToInt(ScreenPosition.X), FMath::RoundToInt(ScreenPosition.Y));
	}
	return true;
}

void ACastlePlayerController::HandleDemolishOutsideClick(FKey Key, FVector2D ScreenPosition)
{
	if (!BuildingDemolishMenu) { return; }
	// 菜单期间两种模式都显示真实指针。经典准星仍保持为正常世界交互的瞄准源。
	if (Key == GetDemolishKey() && TryOpenBuildingDemolishAt(ScreenPosition)) { return; }
	CloseBuildingDemolishMenu();
}

void ACastlePlayerController::ConfirmBuildingDemolish(const UCastleBuildingDemolishMenuWidget& Menu)
{
	// 与当前面板核对，防止已替换面板的回调对另一个建筑提交事务。
	if (BuildingDemolishMenu.Get() != &Menu) { return; }
	auto* Building = Menu.GetTargetBuilding();
	FText Reason;
	if (CanUseBuildingDemolishMenu(Reason))
	{
		if (IsDemolishableBuilding(Building))
		{
			const FCastleBuildingDescriptor Descriptor = Building->GetBuildingDescriptor();
			// 原子接口负责背包容量、对应等级的单张卡与棋盘移除。一旦失败，面板保持原目标。
			if (ActiveBoard->RemoveBuildingToBackpack(Building, ActiveBoard->PrototypeBackpack, Reason))
			{
				CloseBuildingDemolishMenu();
				OnBuildingDemolishedToBackpack.Broadcast(Descriptor);
				return;
			}
		}
		else { Reason = INVTEXT("此建筑已失效或未完成放置，无法拆回建筑栏。"); }
	}
	UE_LOG(LogMoving_Castle, Warning, TEXT("建筑 %s 拆除回收被拒绝：%s"), *GetPathNameSafe(Building), *Reason.ToString());
	ShowDirectorMessage(Reason);
}

void ACastlePlayerController::RemoveBuildingDemolishWidget()
{
	if (BuildingDemolishMenu)
	{
		BuildingDemolishMenu->RemoveFromParent();
		BuildingDemolishMenu = nullptr;
	}
}

void ACastlePlayerController::CloseBuildingDemolishMenu()
{
	if (!BuildingDemolishMenu) { return; }
	RemoveBuildingDemolishWidget();
	RefreshModalInput(true);
}

void ACastlePlayerController::HandleClosePopup()
{
	if (BuildingDemolishMenu) { CloseBuildingDemolishMenu(); }
	else if (ControlSettings) { ToggleControlSettings(); }
}
