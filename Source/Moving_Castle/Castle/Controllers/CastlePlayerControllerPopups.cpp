// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastlePlayerController.h"

#include "Audio/CastleAudioSubsystem.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "Castle/Guide/CastleGuideSubsystem.h"
#include "Castle/UI/Common/CastleBuildingDemolishMenuWidget.h"
#include "Castle/UI/Common/CastleControlSettingsWidget.h"
#include "Engine/GameInstance.h"
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
		// 设置共用齿轮、快捷键与关闭按钮入口，实际关闭时只播一次。
		UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::ClosePanel);
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
	if (!IsValid(ActiveBoard) || !IsValid(Building) || !Building->CanReceiveBuildingTrigger()
		|| ACastleBoard::FindBoardForBuilding(Building) != ActiveBoard)
	{
		return false;
	}
	// 即使超阶建筑只占一格，也不为它打开一级回收菜单。
	if (Building->GetBuildingLevel() > 4)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("控制器 %s 拒绝拆除建筑 %s（等级 %d）：拆除只支持一级至四级建筑。"),
			*GetPathName(), *Building->GetPathName(), Building->GetBuildingLevel());
		return false;
	}
	return true;
}

ACastleBuildingBase* ACastlePlayerController::FindDemolishTargetAt(FVector2D ScreenPosition, FIntVector& OutCell) const
{
	FText Reason;
	if (!CanUseBuildingDemolishMenu(Reason)) { return nullptr; }
	FVector Origin, Direction;
	if (!DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y, Origin, Direction)) { return nullptr; }
	// 单次可见性射线只考虑实际首个遮挡面。飞行 Pawn 和卡牌视图不会抢走场上建筑命中。
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CastleBuildingDemolishClick), true, GetPawn());
	GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * GridCursorTraceDistance, ECC_Visibility, Params);
	auto* Building = Cast<ACastleBuildingBase>(Hit.GetActor());
	return IsDemolishableBuilding(Building) && ActiveBoard->TryResolveBuildingCell(*Building, Hit.ImpactPoint, OutCell)
		? Building : nullptr;
}

bool ACastlePlayerController::OpenBuildingDemolishMenu(ACastleBuildingBase& Building, const FIntVector& TargetCell,
	FVector2D ScreenPosition)
{
	if (!IsDemolishableBuilding(&Building) || ActiveBoard->GetBuildingAt(TargetCell) != &Building)
	{
		UE_LOG(LogMoving_Castle, Warning, TEXT("控制器 %s 不能打开建筑 %s 的格 %s 的拆除菜单：目标已失效。"),
			*GetPathName(), *Building.GetPathName(), *TargetCell.ToString());
		return false;
	}
	auto* Menu = BuildingDemolishMenuClass ? CreateWidget<UCastleBuildingDemolishMenuWidget>(this, BuildingDemolishMenuClass) : nullptr;
	const FVector2D LogicalOffset = ControlMode == ECastleControlMode::Classic ? FVector2D(12.0, 12.0) : FVector2D::ZeroVector;
	if (!Menu || !Menu->ConfigureTarget(Building, TargetCell, *ActiveBoard, ScreenPosition, LogicalOffset) || !Menu->AddToPlayerScreen(35))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("控制器 %s 无法加载、绑定或挂载拆除资产 %s。"), *GetPathName(), DemolishMenuPath);
		return false;
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
	if (Key == GetDemolishKey())
	{
		FIntVector TargetCell;
		if (auto* Building = FindDemolishTargetAt(ScreenPosition, TargetCell))
		{
			OpenBuildingDemolishMenu(*Building, TargetCell, ScreenPosition);
			return;
		}
	}
	CloseBuildingDemolishMenu();
}

void ACastlePlayerController::ConfirmBuildingDemolish(const UCastleBuildingDemolishMenuWidget& Menu)
{
	// 与当前面板核对，防止已替换面板的回调对另一个建筑提交事务。
	if (BuildingDemolishMenu.Get() != &Menu) { return; }
	auto* Building = Menu.GetTargetBuilding();
	const FIntVector TargetCell = Menu.GetTargetCell();
	FText Reason;
	if (!IsDemolishableBuilding(Building) || ActiveBoard->GetBuildingAt(TargetCell) != Building)
	{
		// 建筑可能已被合并替换。旧菜单不再允许提交它先前绑定的格子。
		Reason = INVTEXT("此建筑已失效或不再占据待拆除的格子。");
		CloseBuildingDemolishMenu();
	}
	else if (CanUseBuildingDemolishMenu(Reason))
	{
		FCastleBuildingDescriptor Descriptor = Building->GetBuildingDescriptor();
		Descriptor.Level = 1;
		// 棋盘原子提交目标格与实际返还的一张卡，满栏等失败时保留可重试的菜单。
		if (ActiveBoard->DemolishBuildingCellToBackpack(Building, TargetCell, ActiveBoard->PrototypeBackpack, Reason))
		{
			CloseBuildingDemolishMenu();
			OnBuildingDemolishedToBackpack.Broadcast(Descriptor);
			return;
		}
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
	UCastleAudioSubsystem::PlaySFX(GetWorld(), ECastleSFXEvent::ClosePanel);
	RemoveBuildingDemolishWidget();
	RefreshModalInput(true);
}

void ACastlePlayerController::HandleClosePopup()
{
	if (BuildingDemolishMenu) { CloseBuildingDemolishMenu(); }
	else if (ControlSettings) { ToggleControlSettings(); }
	else if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UCastleGuideSubsystem* Guide = GameInstance->GetSubsystem<UCastleGuideSubsystem>())
		{
			Guide->CloseCurrentModal();
		}
	}
}

void ACastlePlayerController::BeginTemporaryHUDPointer()
{
	// 临时指针只改变经典常态。已有设置、拆除或导演模态继续决定输入焦点。
	if (ControlMode != ECastleControlMode::Classic || bModalInputActive || BuildingDemolishMenu || bHoldingTemporaryHUDPointer)
	{
		return;
	}
	bHoldingTemporaryHUDPointer = true;
	if (bHoldingTowerDispatch) { ReleaseTowerDispatchInput(); }
	RefreshModalInput(true);
}

void ACastlePlayerController::EndTemporaryHUDPointer()
{
	if (!bHoldingTemporaryHUDPointer) { return; }
	bHoldingTemporaryHUDPointer = false;
	// 已从 HUD 打开的模态仍持有输入，松 Alt 时不抢它的焦点。
	if (!bModalInputActive && !BuildingDemolishMenu) { RefreshModalInput(true); }
}
