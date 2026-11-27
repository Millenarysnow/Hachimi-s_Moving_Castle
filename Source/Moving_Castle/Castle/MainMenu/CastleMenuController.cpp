#include "CastleMenuController.h"

#include "CastleMenuModel.h"
#include "CastleMenuStage.h"
#include "Castle/Input/CastleControlMode.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Moving_Castle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

namespace
{
	// 顺序与舞台上的硬引用数组一一对应。尺寸和位置使用 layout.json 的设计像素。
	enum EMenuSprite { TitleTop, TitleBottom, AccentRed, AccentBlue, AccentGold, AccentGreen,
		TutorialLabel, TutorialMarker, StartLabel, StartMarker, ExitLabel, ExitMarker, FocusFill, FocusArrow };
	const FVector2D MenuCanvasSize(1594, 1034);
	const FVector2D SpriteSizes[] = {{463, 120}, {533, 138}, {14, 81}, {27, 14}, {30, 29}, {67, 13},
		{175, 42}, {12, 40}, {175, 42}, {12, 40}, {175, 42}, {12, 40}, {339, 66}, {25, 20}};
	const FVector2D TitlePositions[] = {{179, 224}, {181, 349}, {147, 231}, {145, 359}, {612, 207}, {651, 497}};
	constexpr const TCHAR* SpriteNames[] = {TEXT("TitleTop"), TEXT("TitleBottom"), TEXT("AccentRed"), TEXT("AccentBlue"),
		TEXT("AccentGold"), TEXT("AccentGreen"), TEXT("TutorialLabel"), TEXT("TutorialMarker"), TEXT("StartLabel"),
		TEXT("StartMarker"), TEXT("ExitLabel"), TEXT("ExitMarker"), TEXT("FocusFill"), TEXT("FocusArrow")};
	const FName MainMap(TEXT("/Game/MovingCastle/Maps/MainMap"));
	const FName TutorialMap(TEXT("/Game/MovingCastle/Maps/TutorialMap"));

	FMargin DesignRect(FVector2D Position, FVector2D Size)
	{
		return FMargin(Position.X, Position.Y, Size.X, Size.Y);
	}

	FString ValidateMenuAssets(const ACastleMenuStage& Stage)
	{
		if (!IsValid(Stage.MenuFont) || !IsValid(Stage.PaperMaterial))
		{
			return FString::Printf(TEXT("舞台 %s 的字体 %s 或纸纹 %s 无效。"), *Stage.GetPathName(),
				*GetPathNameSafe(Stage.MenuFont), *GetPathNameSafe(Stage.PaperMaterial));
		}
		if (Stage.MenuTextures.Num() != UE_ARRAY_COUNT(SpriteSizes))
		{
			return FString::Printf(TEXT("舞台 %s 需要 14 张 Hachimis 贴图，已绑定 %d 张。"), *Stage.GetPathName(), Stage.MenuTextures.Num());
		}
		for (int32 Index = 0; Index < Stage.MenuTextures.Num(); ++Index)
		{
			const UTexture2D* Texture = Stage.MenuTextures[Index];
			if (!IsValid(Texture) || Texture->GetSizeX() != SpriteSizes[Index].X * 2 || Texture->GetSizeY() != SpriteSizes[Index].Y * 2)
			{
				return FString::Printf(TEXT("舞台 %s 的 %s 贴图 %s 缺失或尺寸与 @2x 规格不符。"),
					*Stage.GetPathName(), SpriteNames[Index], *GetPathNameSafe(Texture));
			}
		}
		return {};
	}

	/** 绘制层不参与命中，Slate 按钮整行接收鼠标、键盘和手柄输入。 */
	class SCastleMenuSpriteButton final : public SButton
	{
	public:
		void Construct(const FArguments& Args)
		{
			SButton::Construct(Args);
		}

		EVisibility FocusVisibility() const
		{
			return IsHovered() || HasKeyboardFocus() || IsPressed() ? EVisibility::HitTestInvisible : EVisibility::Hidden;
		}

		FSlateColor FillTint() const
		{
			return IsPressed() ? FLinearColor(0.79f, 0.67f, 0.65f) : FLinearColor::White;
		}

		virtual TOptional<bool> OnQueryShowFocus(EFocusCause) const override { return false; }
	};
}

class SCastleStartMenu final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCastleStartMenu) {}
		SLATE_ARGUMENT(ACastleMenuStage*, Stage)
		SLATE_ARGUMENT(ACastleMenuController*, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		const ACastleMenuStage& Stage = *Args._Stage;
		Error = ValidateMenuAssets(Stage);
		const FSlateFontInfo ErrorFont = FCoreStyle::GetDefaultFontStyle("Regular", 16);
		const auto Root = SNew(SOverlay);
		ChildSlot[Root];
		Root->AddSlot(1).VAlign(VAlign_Bottom).Padding(24)
		[
			SAssignNew(ErrorText, STextBlock).Font(ErrorFont).Justification(ETextJustify::Center)
			.AutoWrapText(true).ColorAndOpacity(FLinearColor(0.35f, 0.035f, 0.02f))
		];
		if (!Error.IsEmpty()) { return; }

		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Brushes); ++Index)
		{
			Brushes[Index].SetResourceObject(Stage.MenuTextures[Index]);
			Brushes[Index].ImageSize = SpriteSizes[Index];
		}
		const auto Canvas = SNew(SConstraintCanvas).Visibility(EVisibility::SelfHitTestInvisible);
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(TitlePositions); ++Index)
		{
			Canvas->AddSlot().Offset(DesignRect(TitlePositions[Index], SpriteSizes[Index]))
			[ SNew(SImage).Image(&Brushes[Index]).Visibility(EVisibility::HitTestInvisible) ];
		}
		TutorialChoice = AddChoice(*Canvas, *Args._Controller, INVTEXT("新手教程"), ECastleMenuAction::Tutorial, 569, 34, TutorialLabel, TutorialMarker);
		FirstChoice = AddChoice(*Canvas, *Args._Controller, INVTEXT("开始游戏"), ECastleMenuAction::Play, 645, 33, StartLabel, StartMarker);
		AddChoice(*Canvas, *Args._Controller, INVTEXT("退出游戏"), ECastleMenuAction::Quit, 721, 33, ExitLabel, ExitMarker);
		Root->AddSlot(0)
		[
			SAssignNew(MenuLayer, SScaleBox).Stretch(EStretch::ScaleToFit).Visibility(EVisibility::SelfHitTestInvisible)
			[ SNew(SBox).WidthOverride(MenuCanvasSize.X).HeightOverride(MenuCanvasSize.Y).Visibility(EVisibility::SelfHitTestInvisible)[Canvas] ]
		];

		// 模态覆盖整张视口。经典/设计各自展示实际的按键映射，确认或取消后由控制器恢复焦点。
		const FSlateFontInfo Heading(Stage.MenuFont.Get(), 28);
		const FSlateFontInfo Body(Stage.MenuFont.Get(), 18);
		const FLinearColor Ink(0.12f, 0.08f, 0.07f);
		const auto Classic = MakeModeChoice(*Args._Controller, ECastleControlMode::Classic, INVTEXT("经典模式"), Heading, Body);
		const auto Design = MakeModeChoice(*Args._Controller, ECastleControlMode::Design, INVTEXT("设计模式"), Heading, Body);
		ModeChoice = Classic;
		Root->AddSlot(2)
		[
			SAssignNew(ModeLayer, SBorder).Visibility(EVisibility::Collapsed)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.02f, 0.01f, 0.01f, 0.58f))
			.HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBorder).Padding(26).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.95f, 0.90f, 0.82f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 18)
					[ SNew(STextBlock).Text(INVTEXT("选择操作模式")).Font(Heading).ColorAndOpacity(Ink) ]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)[Classic]
						+ SHorizontalBox::Slot().AutoWidth()[Design]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 18, 0, 0)
					[
						SNew(SButton).OnClicked(FOnClicked::CreateUObject(Args._Controller, &ACastleMenuController::CancelControlMode))
						[ SNew(STextBlock).Text(INVTEXT("取消")).Font(Body) ]
					]
				]
			]
		];
	}

	void SetModeSelectionVisible(bool bVisible)
	{
		ModeLayer->SetVisibility(bVisible ? EVisibility::Visible : EVisibility::Collapsed);
		MenuLayer->SetEnabled(!bVisible);
	}

	FString Error;
	TSharedPtr<SWidget> TutorialChoice;
	TSharedPtr<SWidget> FirstChoice;
	TSharedPtr<SWidget> ModeChoice;
	TSharedPtr<STextBlock> ErrorText;

private:
	TSharedRef<SWidget> AddChoice(SConstraintCanvas& Canvas, ACastleMenuController& Controller, const FText& Label,
		ECastleMenuAction Action, float Y, float LabelX, EMenuSprite TextSprite, EMenuSprite MarkerSprite)
	{
		const auto Button = SNew(SCastleMenuSpriteButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(FMargin(0))
			.HAlign(HAlign_Fill).VAlign(VAlign_Fill).Cursor(EMouseCursor::Hand)
			.OnClicked(FOnClicked::CreateUObject(&Controller, &ACastleMenuController::Choose, Action));
#if WITH_ACCESSIBILITY
		Button->SetAccessibleBehavior(EAccessibleBehavior::Custom, Label);
#endif
		const auto Art = SNew(SConstraintCanvas).Visibility(EVisibility::HitTestInvisible);
		const auto AddArt = [&](EMenuSprite Sprite, FVector2D Offset, bool bFocus, bool bTint)
		{
			const auto Image = SNew(SImage).Image(&Brushes[Sprite]).Visibility(EVisibility::HitTestInvisible);
			if (bFocus)
			{
				Image->SetVisibility(TAttribute<EVisibility>::CreateSP(&Button.Get(), &SCastleMenuSpriteButton::FocusVisibility));
			}
			if (bTint) { Image->SetColorAndOpacity(TAttribute<FSlateColor>::CreateSP(&Button.Get(), &SCastleMenuSpriteButton::FillTint)); }
			Art->AddSlot().Offset(DesignRect(Offset, SpriteSizes[Sprite]))[Image];
		};
		AddArt(FocusFill, {-3, -3}, true, true);
		AddArt(TextSprite, {LabelX, 9}, false, false);
		AddArt(MarkerSprite, {9, 10}, false, false);
		AddArt(FocusArrow, {289, 20}, true, false);
		Button->SetContent(Art);
		Canvas.AddSlot().Offset(DesignRect({183, Y}, {333, 60}))[Button];
		return Button;
	}

	TSharedRef<SButton> MakeModeChoice(ACastleMenuController& Controller, ECastleControlMode Mode, const FText& Label,
		const FSlateFontInfo& Heading, const FSlateFontInfo& Body)
	{
		const auto& Bindings = CastleControlInput::GetBindings(Mode);
		const auto Key = [&](ECastleWorldInputAction Action) { return CastleControlInput::GetKeyDisplayText(Bindings.Get(Action)); };
		const FText Look = Mode == ECastleControlMode::Classic ? INVTEXT("移动鼠标转动视角")
			: FText::Format(INVTEXT("按住{0}转动视角"), Key(ECastleWorldInputAction::HoldCameraLook));
		const FText Description = FText::Format(INVTEXT("{0}交互\n{1}拆除\n{2}"),
			Key(ECastleWorldInputAction::PrimaryInteract), Key(ECastleWorldInputAction::Demolish), Look);
		return SNew(SButton).ContentPadding(FMargin(22, 15)).Cursor(EMouseCursor::Hand)
			.OnClicked(FOnClicked::CreateUObject(&Controller, &ACastleMenuController::ConfirmControlMode, Mode))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[ SNew(STextBlock).Text(Label).Font(Heading) ]
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Text(Description).Font(Body) ]
			];
	}

	FSlateBrush Brushes[UE_ARRAY_COUNT(SpriteSizes)];
	TSharedPtr<SScaleBox> MenuLayer;
	TSharedPtr<SBorder> ModeLayer;
};

ACastleMenuGameMode::ACastleMenuGameMode()
{
	PlayerControllerClass = ACastleMenuController::StaticClass();
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
	// 展示摄影由固定舞台负责，GameMode 无须给玩家寻找出生点。
	bStartPlayersAsSpectators = true;
	SpectatorClass = nullptr;
}

ACastleMenuController::ACastleMenuController()
{
	bAutoManageActiveCameraTarget = false;
	bShowMouseCursor = true;
}

bool ACastleMenuController::IsMenuReady() const
{
	return Menu.IsValid() && Menu->Error.IsEmpty();
}

void ACastleMenuController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) { return; }
	ACastleMenuStage* Stage = nullptr;
	int32 Count = 0;
	for (TActorIterator<ACastleMenuStage> It(GetWorld()); It; ++It) { Stage = *It; ++Count; }
	if (Count != 1 || !GetWorld()->GetGameViewport())
	{
		Report(FString::Printf(TEXT("开始地图 %s 需要本地游戏视口和唯一菜单舞台，当前舞台数 %d。"), *GetWorld()->GetPathName(), Count));
		return;
	}
	SetViewTarget(Stage);
	Menu = SNew(SCastleStartMenu).Stage(Stage).Controller(this);
	GetWorld()->GetGameViewport()->AddViewportWidgetContent(Menu.ToSharedRef(), 10);
	if (!Menu->Error.IsEmpty()) { Report(Menu->Error); return; }
	FocusMenuWidget(Menu->FirstChoice);
	FailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ACastleMenuController::TravelFailed);
	FString Error;
	const UCastleMenuModel* Model = UCastleMenuModel::LoadForMenu(this, Error);
	if (!Model || !Stage->ShowModel(*Model, Error)) { Report(Error); return; }
	UE_LOG(LogMoving_Castle, Display, TEXT("开始界面已就绪：主塔 %d 组、房间 %d 组，城堡每秒旋转 2 度。"), Model->Tower.Num(), Model->Rooms.Num());
}

FReply ACastleMenuController::Choose(const ECastleMenuAction Action)
{
	// 同一帧的触摸和确认事件只派发一次旅行；旅行失败后恢复显式选择。
	if (!Menu || !Menu->Error.IsEmpty() || bLeaving || bModeSelectionOpen) { return FReply::Handled(); }
	if (Action == ECastleMenuAction::Quit)
	{
		bLeaving = true;
#if WITH_EDITOR
		// PIE 与编辑器共享进程，显式结束本次试玩。独立运行的游戏仍由平台退出接口关闭。
		if (GetWorld()->WorldType == EWorldType::PIE)
		{
			GEditor->RequestEndPlayMap();
			return FReply::Handled();
		}
#endif
		UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
		return FReply::Handled();
	}
	if (Action == ECastleMenuAction::Tutorial) { return TravelTo(TutorialMap); }
	const auto* Session = GetGameInstance()->GetSubsystem<UCastleMenuSessionSubsystem>();
	if (!Session)
	{
		Report(TEXT("无法开始游戏，当前 GameInstance 缺少菜单会话。"));
		return FReply::Handled();
	}
	if (Session->bHasChosenControlModeThisSession) { return TravelTo(MainMap); }
	bModeSelectionOpen = true;
	Menu->SetModeSelectionVisible(true);
	FocusMenuWidget(Menu->ModeChoice);
	return FReply::Handled();
}

FReply ACastleMenuController::ConfirmControlMode(ECastleControlMode Mode)
{
	if (!bModeSelectionOpen || bLeaving) { return FReply::Handled(); }
	return TravelTo(MainMap, Mode);
}

FReply ACastleMenuController::CancelControlMode()
{
	if (bModeSelectionOpen && !bLeaving)
	{
		bModeSelectionOpen = false;
		Menu->SetModeSelectionVisible(false);
		FocusMenuWidget(Menu->FirstChoice);
	}
	return FReply::Handled();
}

void ACastleMenuController::FocusMenuWidget(const TSharedPtr<SWidget>& Widget)
{
	FInputModeUIOnly Input;
	Input.SetWidgetToFocus(Widget);
	Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Input);
}

FReply ACastleMenuController::TravelTo(FName Map, TOptional<ECastleControlMode> NewMode)
{
	if (!FPackageName::DoesPackageExist(Map.ToString()))
	{
		Report(FString::Printf(TEXT("无法进入地图 %s，地图包缺失。"), *Map.ToString()));
		return FReply::Handled();
	}
	if (NewMode.IsSet())
	{
		auto* Session = GetGameInstance()->GetSubsystem<UCastleMenuSessionSubsystem>();
		if (!Session)
		{
			Report(TEXT("无法确认操作模式，当前 GameInstance 缺少菜单会话。"));
			return FReply::Handled();
		}
		// 仅操作预设使用既有的配置保存入口。首次确认标记随 GameInstance 跨图存在。
		CastleControlInput::SaveMode(NewMode.GetValue());
		Session->bHasChosenControlModeThisSession = true;
		UE_LOG(LogMoving_Castle, Display, TEXT("开始菜单已确认操作模式：%s。"),
			NewMode.GetValue() == ECastleControlMode::Classic ? TEXT("经典") : TEXT("设计"));
		CancelControlMode();
	}
	bLeaving = true;
	UE_LOG(LogMoving_Castle, Display, TEXT("开始菜单请求进入地图 %s。"), *Map.ToString());
	UGameplayStatics::OpenLevel(this, Map);
	return FReply::Handled();
}

void ACastleMenuController::TravelFailed(UWorld* World, ETravelFailure::Type Type, const FString& Detail)
{
	if (World == GetWorld() && bLeaving)
	{
		bLeaving = false;
		Report(FString::Printf(TEXT("离开菜单失败（%s）：%s"), ETravelFailure::ToString(Type), *Detail));
	}
}

void ACastleMenuController::Report(const FString& Error)
{
	UE_LOG(LogMoving_Castle, Error, TEXT("开始界面：%s"), *Error);
	if (Menu) { Menu->ErrorText->SetText(FText::FromString(Error)); }
}

void ACastleMenuController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GEngine) { GEngine->OnTravelFailure().Remove(FailureHandle); }
	if (Menu && GetWorld()->GetGameViewport()) { GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Menu.ToSharedRef()); }
	Menu.Reset();
	Super::EndPlay(Reason);
}
