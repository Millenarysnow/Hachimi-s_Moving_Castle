// Copyright Epic Games, Inc. All Rights Reserved.
#include "CastleDirectorWidgets.h"

#include "Castle/Combat/CastleCombatSubsystem.h"
#include "Castle/Controllers/CastlePlayerController.h"
#include "Castle/Director/CastleDirectorSubsystem.h"
#include "Castle/UI/Common/CastleHUDControls.h"
#include "CastleMainTowerComponent.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Moving_Castle.h"
#include "Units/Navigation/Interaction/CastlePatrolRoute.h"

namespace
{
	/** 固定索引与奖励/结算 WBP 中的两个 Image 对应，检查层级以免按下时切到空控件。 */
	bool HasDirectorActionIconBrushes(const UWidgetSwitcher* Icons)
	{
		return IsValid(Icons) && Icons->GetNumWidgets() == 2
			&& CastleHUDControls::HasValidTextureBrush(Cast<UImage>(Icons->GetWidgetAtIndex(0)))
			&& CastleHUDControls::HasValidTextureBrush(Cast<UImage>(Icons->GetWidgetAtIndex(1)));
	}
}

bool UCastleDirectorNodeLabelWidget::HasRequiredBindings() const
{
	return IsValid(LabelText);
}

bool UCastleDirectorNodeLabelWidget::SetNodeLabel(const FText& Text, const FLinearColor& Color)
{
	if (!IsValid(LabelText))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("节点标签WBP %s 缺少必需控件 LabelText。"), *GetClass()->GetPathName());
		return false;
	}
	LabelText->SetText(Text);
	LabelText->SetColorAndOpacity(FSlateColor(Color));
	return true;
}

bool UCastleDirectorNodeLabelWidget::TryGetNodeLabel(FText& OutText, FLinearColor& OutColor) const
{
	if (!IsValid(LabelText))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("读取节点标签失败：WBP %s 缺少必需控件 LabelText。"), *GetClass()->GetPathName());
		return false;
	}
	OutText = LabelText->GetText();
	OutColor = LabelText->GetColorAndOpacity().GetSpecifiedColor();
	return true;
}

bool UCastleDirectorHUDWidget::ComputeFilledHearts(const float CurrentHealth, const float MaxHealth, int32& OutFilledCount)
{
	OutFilledCount = 0;
	if (!FMath::IsFinite(CurrentHealth) || !FMath::IsFinite(MaxHealth)
		|| MaxHealth <= 0.0f || CurrentHealth < 0.0f || CurrentHealth > MaxHealth)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主塔血条计算失败：生命参数无效（Current=%.3f, Max=%.3f）。"), CurrentHealth, MaxHealth);
		return false;
	}
	if (CurrentHealth <= 0.0f)
	{
		OutFilledCount = 0;
		return true;
	}
	if (CurrentHealth >= MaxHealth)
	{
		OutFilledCount = UCastleIconBarWidget::FixedIconCount;
		return true;
	}
	// 任意受击即可体现格数下降，同时存活时至少保留 1 颗亮心避免误判为已死亡。
	const int32 Floored = FMath::FloorToInt((CurrentHealth / MaxHealth) * static_cast<float>(UCastleIconBarWidget::FixedIconCount));
	OutFilledCount = FMath::Max(1, Floored);
	return true;
}

bool UCastleDirectorHUDWidget::ComputeFilledSkulls(const int32 DefeatedCount, const int32 TotalCount, int32& OutFilledCount)
{
	OutFilledCount = 0;
	if (TotalCount <= 0 || DefeatedCount < 0 || DefeatedCount > TotalCount)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("战斗骷髅进度计算失败：进度参数无效（Defeated=%d, Total=%d）。"), DefeatedCount, TotalCount);
		return false;
	}
	if (DefeatedCount == 0)
	{
		OutFilledCount = 0;
		return true;
	}
	if (DefeatedCount == TotalCount)
	{
		OutFilledCount = UCastleIconBarWidget::FixedIconCount;
		return true;
	}
	// 开战为 0 颗亮骷髅，击杀至少 1 名敌人后至少点亮 1 颗，清场点亮全部 20 颗。
	const int32 Floored = FMath::FloorToInt((static_cast<float>(DefeatedCount) / static_cast<float>(TotalCount))
		* static_cast<float>(UCastleIconBarWidget::FixedIconCount));
	OutFilledCount = FMath::Max(1, Floored);
	return true;
}

int32 UCastleDirectorHUDWidget::GetFilledHeartCount() const
{
	return IsValid(MainTowerHealthBar) ? MainTowerHealthBar->GetFilledCount() : INDEX_NONE;
}

int32 UCastleDirectorHUDWidget::GetFilledSkullCount() const
{
	return IsValid(BattleProgressBar) ? BattleProgressBar->GetFilledCount() : INDEX_NONE;
}

void UCastleDirectorHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!HasRequiredBindings())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主界面 HUD WBP %s 缺少必需控件或切图，请检查 WBP 绑定与贴图配置。"),
			*GetClass()->GetPathName());
		return;
	}

	// 按每个按钮对应的展示模式初始化切图状态，缺失状态会在校验中直接报错。
	GuideButton->ConfigurePresentationMode(ECastleHUDButtonPresentationMode::Standard);
	SettingsButton->ConfigurePresentationMode(ECastleHUDButtonPresentationMode::Standard);
	MainMenuButton->ConfigurePresentationMode(ECastleHUDButtonPresentationMode::Standard);

	BuildUndoButton->ConfigurePresentationMode(ECastleHUDButtonPresentationMode::Standard);
	BuildRedoButton->ConfigurePresentationMode(ECastleHUDButtonPresentationMode::WithDisabledState);
	BuildShopButton->ConfigurePresentationMode(ECastleHUDButtonPresentationMode::Standard);

	PathUndoButton->ConfigurePresentationMode(ECastleHUDButtonPresentationMode::Standard);
	PathRedoButton->ConfigurePresentationMode(ECastleHUDButtonPresentationMode::WithDisabledState);
	PathDeleteButton->ConfigurePresentationMode(ECastleHUDButtonPresentationMode::Standard);

	BattlePauseButton->ConfigurePresentationMode(ECastleHUDButtonPresentationMode::TwoStateToggle);
	BattleSpeedButton->ConfigurePresentationMode(ECastleHUDButtonPresentationMode::TwoStateToggle);
	BattleRetryButton->ConfigurePresentationMode(ECastleHUDButtonPresentationMode::Standard);

	// 建造重做与路径重做当前无后端重做栈，默认显示“没有可复原步骤”的灰态切图。
	BuildRedoButton->SetActionEnabled(false);
	PathRedoButton->SetActionEnabled(false);

	if (!bButtonsBound)
	{
		GuideButton->OnButtonClicked.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandleGuideClicked);
		SettingsButton->OnButtonClicked.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandleSettingsClicked);
		MainMenuButton->OnButtonClicked.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandleMainMenuClicked);
		BuildUndoButton->OnButtonClicked.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandleBuildUndoClicked);
		BuildRedoButton->OnButtonClicked.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandleBuildRedoClicked);
		BuildShopButton->OnButtonClicked.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandleBuildShopClicked);
		PathUndoButton->OnButtonClicked.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandlePathUndoClicked);
		PathRedoButton->OnButtonClicked.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandlePathRedoClicked);
		PathDeleteButton->OnButtonClicked.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandlePathDeleteClicked);
		BattlePauseButton->OnButtonClicked.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandleBattlePauseClicked);
		BattleSpeedButton->OnButtonClicked.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandleBattleSpeedClicked);
		BattleRetryButton->OnButtonClicked.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandleBattleRetryClicked);
		bButtonsBound = true;
	}

	// 初始化时战斗进度条先置空并隐藏，等待进入战斗后显示。
	BattleProgressBar->SetFilledCount(0);
	BattleProgressBar->SetVisibility(ESlateVisibility::Collapsed);
	RightActionSwitcher->SetActiveWidgetIndex(static_cast<int32>(ECastleHUDScreenMode::Build));
}

bool UCastleDirectorHUDWidget::BindMainTowerHealth(UCastleMainTowerComponent* MainTower)
{
	if (!IsValid(MainTower) || !HasRequiredBindings())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主界面 HUD %s 无法绑定主塔：主塔组件 %s 或必需控件/切图无效。"),
			*GetClass()->GetPathName(), *GetPathNameSafe(MainTower));
		return false;
	}
	HealthSource = MainTower;
	MainTower->OnHealthChanged.AddUniqueDynamic(this, &UCastleDirectorHUDWidget::HandleTowerHealthChanged);
	HandleTowerHealthChanged(MainTower->CurrentHealth, MainTower->CurrentHealth, 0.0f);

	if (UWorld* World = GetWorld())
	{
		if (UCastleDirectorSubsystem* Director = World->GetSubsystem<UCastleDirectorSubsystem>())
		{
			if (BoundDirector.IsValid())
			{
				BoundDirector->OnDirectorPhaseChanged.Remove(DirectorPhaseHandle);
				BoundDirector->OnBattleControlStateChanged.Remove(BattleControlHandle);
			}
			BoundDirector = Director;
			DirectorPhaseHandle = Director->OnDirectorPhaseChanged.AddUObject(this, &UCastleDirectorHUDWidget::HandleDirectorPhaseChanged);
			BattleControlHandle = Director->OnBattleControlStateChanged.AddUObject(this, &UCastleDirectorHUDWidget::HandleBattleControlStateChanged);
		}
	}

	EnsurePatrolRouteBinding();
	SyncScreenMode();
	SyncBattleControlButtons();
	return true;
}

void UCastleDirectorHUDWidget::EnsurePatrolRouteBinding()
{
	if (BoundPatrolRoute.IsValid() || !GetWorld())
	{
		return;
	}
	if (ACastlePatrolRoute* Route = Cast<ACastlePatrolRoute>(UGameplayStatics::GetActorOfClass(GetWorld(), ACastlePatrolRoute::StaticClass())))
	{
		BoundPatrolRoute = Route;
		PatrolEditingHandle = Route->OnEditingStateChanged.AddUObject(this, &UCastleDirectorHUDWidget::HandlePatrolEditingChanged);
	}
}

void UCastleDirectorHUDWidget::SyncScreenMode()
{
	if (!IsValid(RightActionSwitcher) || !IsValid(BattleProgressBar))
	{
		return;
	}
	ECastleHUDScreenMode TargetMode = ECastleHUDScreenMode::Build;
	const UCastleDirectorSubsystem* Director = BoundDirector.IsValid()
		? BoundDirector.Get()
		: (GetWorld() ? GetWorld()->GetSubsystem<UCastleDirectorSubsystem>() : nullptr);
	const UCastleCombatSubsystem* Combat = GetWorld() ? GetWorld()->GetSubsystem<UCastleCombatSubsystem>() : nullptr;
	if ((IsValid(Director) && Director->IsRunning() && Director->GetPhase() == ECastleDirectorPhase::Battle)
		|| ((!IsValid(Director) || !Director->IsRunning()) && IsValid(Combat) && Combat->IsBattleActive()))
	{
		TargetMode = ECastleHUDScreenMode::Battle;
	}
	else if (BoundPatrolRoute.IsValid() && BoundPatrolRoute->IsEditing())
	{
		TargetMode = ECastleHUDScreenMode::PathEdit;
	}

	CurrentScreenMode = TargetMode;
	RightActionSwitcher->SetActiveWidgetIndex(static_cast<int32>(CurrentScreenMode));

	const bool bShowBattleBar = (CurrentScreenMode == ECastleHUDScreenMode::Battle);
	BattleProgressBar->SetVisibility(bShowBattleBar ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (bShowBattleBar)
	{
		SyncBattleProgress();
	}
	else
	{
		BattleProgressBar->SetFilledCount(0);
	}
}

void UCastleDirectorHUDWidget::SyncBattleControlButtons()
{
	const UCastleDirectorSubsystem* Director = BoundDirector.IsValid()
		? BoundDirector.Get()
		: (GetWorld() ? GetWorld()->GetSubsystem<UCastleDirectorSubsystem>() : nullptr);
	const bool bPaused = IsValid(Director) && Director->IsBattlePaused();
	const bool bDoubleSpeed = IsValid(Director) && Director->IsBattleDoubleSpeed();
	if (IsValid(BattlePauseButton))
	{
		BattlePauseButton->SetAlternateState(bPaused);
	}
	if (IsValid(BattleSpeedButton))
	{
		BattleSpeedButton->SetAlternateState(bDoubleSpeed);
	}
}

void UCastleDirectorHUDWidget::SyncBattleProgress()
{
	if (!IsValid(BattleProgressBar) || CurrentScreenMode != ECastleHUDScreenMode::Battle)
	{
		return;
	}
	const UCastleCombatSubsystem* Combat = GetWorld() ? GetWorld()->GetSubsystem<UCastleCombatSubsystem>() : nullptr;
	if (!IsValid(Combat) || !Combat->IsBattleActive())
	{
		return;
	}
	int32 DefeatedCount = 0;
	int32 TotalCount = 0;
	if (!Combat->GetBattleDefeatedProgress(DefeatedCount, TotalCount))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主界面 HUD %s 读取战斗击杀进度失败。"), *GetClass()->GetPathName());
		return;
	}
	int32 FilledSkulls = 0;
	if (!ComputeFilledSkulls(DefeatedCount, TotalCount, FilledSkulls))
	{
		return;
	}
	BattleProgressBar->SetFilledCount(FilledSkulls);
}

void UCastleDirectorHUDWidget::NativeTick(const FGeometry& Geometry, const float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	EnsurePatrolRouteBinding();
	SyncScreenMode();
	if (CurrentScreenMode == ECastleHUDScreenMode::Battle)
	{
		SyncBattleControlButtons();
		SyncBattleProgress();
	}
}

void UCastleDirectorHUDWidget::HandleTowerHealthChanged(float PreviousHealth, float CurrentHealth, float Damage)
{
	const UCastleMainTowerComponent* MainTower = HealthSource.Get();
	if (!IsValid(MainTower) || !IsValid(MainTowerHealthBar))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主界面 HUD %s 无法更新主塔血条：主塔组件 %s 或 MainTowerHealthBar 已失效。"),
			*GetClass()->GetPathName(), *GetPathNameSafe(MainTower));
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	int32 FilledHearts = 0;
	if (!ComputeFilledHearts(CurrentHealth, MainTower->MaxHealth, FilledHearts))
	{
		return;
	}
	MainTowerHealthBar->SetFilledCount(FilledHearts);
}

void UCastleDirectorHUDWidget::HandleDirectorPhaseChanged(const ECastleDirectorPhase NewPhase)
{
	SyncScreenMode();
	SyncBattleControlButtons();
}

void UCastleDirectorHUDWidget::HandleBattleControlStateChanged()
{
	SyncBattleControlButtons();
}

void UCastleDirectorHUDWidget::HandlePatrolEditingChanged(const bool bIsEditing)
{
	SyncScreenMode();
}

void UCastleDirectorHUDWidget::HandleGuideClicked()
{
	UE_LOG(LogMoving_Castle, Display, TEXT("主界面 HUD：点击查看攻略按钮（后端界面暂未实现）。"));
}

void UCastleDirectorHUDWidget::HandleSettingsClicked()
{
	UE_LOG(LogMoving_Castle, Display, TEXT("主界面 HUD：点击设置按钮（后端界面暂未实现）。"));
}

void UCastleDirectorHUDWidget::HandleMainMenuClicked()
{
	UE_LOG(LogMoving_Castle, Display, TEXT("主界面 HUD：点击主菜单按钮（后端界面暂未实现）。"));
}

void UCastleDirectorHUDWidget::HandleBuildUndoClicked()
{
	UE_LOG(LogMoving_Castle, Display, TEXT("主界面 HUD：点击建造撤回按钮（后端逻辑暂未实现）。"));
}

void UCastleDirectorHUDWidget::HandleBuildRedoClicked()
{
	UE_LOG(LogMoving_Castle, Display, TEXT("主界面 HUD：点击建造重做按钮（后端逻辑暂未实现）。"));
}

void UCastleDirectorHUDWidget::HandleBuildShopClicked()
{
	if (ACastlePlayerController* Controller = Cast<ACastlePlayerController>(GetOwningPlayer()))
	{
		Controller->ToggleShop();
	}
}

void UCastleDirectorHUDWidget::HandlePathUndoClicked()
{
	EnsurePatrolRouteBinding();
	if (BoundPatrolRoute.IsValid())
	{
		BoundPatrolRoute->UndoDraftPoint();
	}
}

void UCastleDirectorHUDWidget::HandlePathRedoClicked()
{
	UE_LOG(LogMoving_Castle, Display, TEXT("主界面 HUD：点击路径重做按钮（后端逻辑暂未实现）。"));
}

void UCastleDirectorHUDWidget::HandlePathDeleteClicked()
{
	EnsurePatrolRouteBinding();
	if (BoundPatrolRoute.IsValid())
	{
		BoundPatrolRoute->ClearPatrolRoute();
	}
}

void UCastleDirectorHUDWidget::HandleBattlePauseClicked()
{
	if (UCastleDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UCastleDirectorSubsystem>() : nullptr)
	{
		Director->ToggleBattlePause();
	}
}

void UCastleDirectorHUDWidget::HandleBattleSpeedClicked()
{
	if (UCastleDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UCastleDirectorSubsystem>() : nullptr)
	{
		Director->ToggleBattleSpeed();
	}
}

void UCastleDirectorHUDWidget::HandleBattleRetryClicked()
{
	if (UCastleDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UCastleDirectorSubsystem>() : nullptr)
	{
		Director->RestartCurrentMap();
	}
}

void UCastleDirectorHUDWidget::NativeDestruct()
{
	if (UCastleMainTowerComponent* MainTower = HealthSource.Get())
	{
		MainTower->OnHealthChanged.RemoveDynamic(this, &UCastleDirectorHUDWidget::HandleTowerHealthChanged);
		HealthSource.Reset();
	}
	if (UCastleDirectorSubsystem* Director = BoundDirector.Get())
	{
		Director->OnDirectorPhaseChanged.Remove(DirectorPhaseHandle);
		Director->OnBattleControlStateChanged.Remove(BattleControlHandle);
		BoundDirector.Reset();
	}
	if (ACastlePatrolRoute* Route = BoundPatrolRoute.Get())
	{
		Route->OnEditingStateChanged.Remove(PatrolEditingHandle);
		BoundPatrolRoute.Reset();
	}
	Super::NativeDestruct();
}

bool UCastleDirectorHUDWidget::HasRequiredBindings() const
{
	if (!IsValid(MainTowerHealthBar) || !MainTowerHealthBar->ValidatePresentation())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主界面 HUD %s 缺少有效的顶部 20 格主塔血条 MainTowerHealthBar。"), *GetClass()->GetPathName());
		return false;
	}
	if (!IsValid(BattleProgressBar) || !BattleProgressBar->ValidatePresentation())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主界面 HUD %s 缺少有效的底部 20 格战斗骷髅进度条 BattleProgressBar。"), *GetClass()->GetPathName());
		return false;
	}
	if (!IsValid(RightActionSwitcher) || RightActionSwitcher->GetNumWidgets() != 3)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("主界面 HUD %s 的 RightActionSwitcher 缺失或子页数不为 3（实际 %d）。"),
			*GetClass()->GetPathName(), IsValid(RightActionSwitcher) ? RightActionSwitcher->GetNumWidgets() : 0);
		return false;
	}

	const struct
	{
		const TCHAR* Name;
		const UCastleHUDIconButtonWidget* Button;
		ECastleHUDButtonPresentationMode Mode;
	} RequiredButtons[] = {
		{TEXT("GuideButton"), GuideButton, ECastleHUDButtonPresentationMode::Standard},
		{TEXT("SettingsButton"), SettingsButton, ECastleHUDButtonPresentationMode::Standard},
		{TEXT("MainMenuButton"), MainMenuButton, ECastleHUDButtonPresentationMode::Standard},
		{TEXT("BuildUndoButton"), BuildUndoButton, ECastleHUDButtonPresentationMode::Standard},
		{TEXT("BuildRedoButton"), BuildRedoButton, ECastleHUDButtonPresentationMode::WithDisabledState},
		{TEXT("BuildShopButton"), BuildShopButton, ECastleHUDButtonPresentationMode::Standard},
		{TEXT("PathUndoButton"), PathUndoButton, ECastleHUDButtonPresentationMode::Standard},
		{TEXT("PathRedoButton"), PathRedoButton, ECastleHUDButtonPresentationMode::WithDisabledState},
		{TEXT("PathDeleteButton"), PathDeleteButton, ECastleHUDButtonPresentationMode::Standard},
		{TEXT("BattlePauseButton"), BattlePauseButton, ECastleHUDButtonPresentationMode::TwoStateToggle},
		{TEXT("BattleSpeedButton"), BattleSpeedButton, ECastleHUDButtonPresentationMode::TwoStateToggle},
		{TEXT("BattleRetryButton"), BattleRetryButton, ECastleHUDButtonPresentationMode::Standard}
	};
	for (const auto& Entry : RequiredButtons)
	{
		if (!IsValid(Entry.Button) || !Entry.Button->ValidatePresentation(Entry.Mode))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("主界面 HUD %s 的按钮 %s 缺失或切图配置无效。"),
				*GetClass()->GetPathName(), Entry.Name);
			return false;
		}
	}
	return true;
}

void UCastleRewardChoiceWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	// BindWidget 缺失属于资产错误，不能创建临时按钮冒充配置成功。
	if (!HasRequiredBindings())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("奖励 WBP %s 的必需控件、回血图标或商店普通/按下笔刷无效，请检查资源与显示尺寸。"), *GetClass()->GetPathName());
		return;
	}
	ExperienceButton->OnClicked.AddDynamic(this, &UCastleRewardChoiceWidget::ChooseExperience);
	HealingButton->OnClicked.AddDynamic(this, &UCastleRewardChoiceWidget::ChooseHealing);
	ShopButton->OnClicked.AddDynamic(this, &UCastleRewardChoiceWidget::ChooseShop);
	// 图标只是按钮内容，按下/释放只切换图像；点击仍走原有奖励事务。
	ShopButton->OnPressed.AddDynamic(this, &UCastleRewardChoiceWidget::ShowShopPressedIcon);
	ShopButton->OnReleased.AddDynamic(this, &UCastleRewardChoiceWidget::ShowShopNormalIcon);
}

void UCastleRewardChoiceWidget::ShowShopPressedIcon()
{
	ShopIconStates->SetActiveWidgetIndex(1);
}

void UCastleRewardChoiceWidget::ShowShopNormalIcon()
{
	ShopIconStates->SetActiveWidgetIndex(0);
}

void UCastleRewardChoiceWidget::NativeTick(const FGeometry& Geometry, const float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	UCastleDirectorSubsystem* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>();
	if (Director && ExperienceLabel && HealingLabel && ShopLabel && FailureText)
	{
		ExperienceLabel->SetText(Director->GetRewardLabel(ECastleDirectorReward::Experience));
		HealingLabel->SetText(Director->GetRewardLabel(ECastleDirectorReward::Healing));
		ShopLabel->SetText(Director->GetRewardLabel(ECastleDirectorReward::Shop));
		FailureText->SetText(Director->GetStatusMessage());
	}
}

void UCastleRewardChoiceWidget::ChooseExperience()
{
	FText Reason;
	GetWorld()->GetSubsystem<UCastleDirectorSubsystem>()->TryChooseReward(ECastleDirectorReward::Experience, Reason);
}
void UCastleRewardChoiceWidget::ChooseHealing()
{
	FText Reason;
	GetWorld()->GetSubsystem<UCastleDirectorSubsystem>()->TryChooseReward(ECastleDirectorReward::Healing, Reason);
}
void UCastleRewardChoiceWidget::ChooseShop()
{
	FText Reason;
	GetWorld()->GetSubsystem<UCastleDirectorSubsystem>()->TryChooseReward(ECastleDirectorReward::Shop, Reason);
}

void UCastleGameOverWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	if (!HasRequiredBindings())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("游戏结束 WBP %s 的必需控件、骷髅图标或重试普通/按下笔刷无效，请检查资源与显示尺寸。"), *GetClass()->GetPathName());
		return;
	}
	RestartButton->OnClicked.AddDynamic(this, &UCastleGameOverWidget::Restart);
	// 世界已暂停时，Slate 仍能即时更新按压反馈；不改变重试实际触发时机。
	RestartButton->OnPressed.AddDynamic(this, &UCastleGameOverWidget::ShowRestartPressedIcon);
	RestartButton->OnReleased.AddDynamic(this, &UCastleGameOverWidget::ShowRestartNormalIcon);
}

void UCastleGameOverWidget::ShowRestartPressedIcon()
{
	RestartIconStates->SetActiveWidgetIndex(1);
}

void UCastleGameOverWidget::ShowRestartNormalIcon()
{
	RestartIconStates->SetActiveWidgetIndex(0);
}

void UCastleGameOverWidget::NativeTick(const FGeometry& Geometry, const float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	UCastleDirectorSubsystem* Director = GetWorld()->GetSubsystem<UCastleDirectorSubsystem>();
	if (Director && SummaryText && FailureText)
	{
		SummaryText->SetText(FText::FromString(FString::Printf(TEXT("游戏结束 · 通过 %d 关"), Director->GetClearedSteps())));
		FailureText->SetText(Director->GetGameOverFailure());
	}
}

void UCastleGameOverWidget::Restart()
{
	GetWorld()->GetSubsystem<UCastleDirectorSubsystem>()->RestartCurrentMap();
}

bool UCastleRewardChoiceWidget::HasRequiredBindings() const
{
	return ExperienceButton && HealingButton && ShopButton && ExperienceLabel && HealingLabel && ShopLabel && FailureText
		&& CastleHUDControls::HasValidTextureBrush(HealingIcon) && HasDirectorActionIconBrushes(ShopIconStates);
}

bool UCastleGameOverWidget::HasRequiredBindings() const
{
	return SummaryText && FailureText && RestartButton
		&& CastleHUDControls::HasValidTextureBrush(SkullIcon) && HasDirectorActionIconBrushes(RestartIconStates);
}
