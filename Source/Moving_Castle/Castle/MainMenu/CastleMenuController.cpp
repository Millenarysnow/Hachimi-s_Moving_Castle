#include "CastleMenuController.h"

#include "CastleMenuModel.h"
#include "CastleMenuStage.h"
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
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

/** 使用真正的 Slate 按钮保留键盘、手柄和触摸语义，显示部分只有一行字。 */
class SCastleMenuTextButton final : public SButton
{
public:
	SLATE_BEGIN_ARGS(SCastleMenuTextButton) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ARGUMENT(FSlateFontInfo, Font)
		SLATE_EVENT(FOnClicked, OnChosen)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		SButton::Construct(SButton::FArguments()
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(FMargin(16, 5)).HAlign(HAlign_Center).VAlign(VAlign_Center)
			.Cursor(EMouseCursor::Hand).OnClicked(Args._OnChosen)
			[ SNew(STextBlock).Text(Args._Label).Font(Args._Font).ColorAndOpacity(this, &SCastleMenuTextButton::Ink) ]);
	}
	/** 用字色表达焦点，不让全局 Slate 焦点框破坏纸面无边框排版。 */
	virtual TOptional<bool> OnQueryShowFocus(EFocusCause) const override { return false; }
private:
	FSlateColor Ink() const
	{
		return IsHovered() || HasKeyboardFocus() ? FLinearColor(0.027f, 0.026f, 0.026f)
			: FLinearColor(0.15f, 0.14f, 0.135f);
	}
};

class SCastleStartMenu final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCastleStartMenu) {}
		SLATE_ARGUMENT(ACastleMenuStage*, Stage)
		SLATE_ARGUMENT(ACastleMenuController*, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		ACastleMenuStage& Stage = *Args._Stage;
		Logo.SetResourceObject(Stage.TitleTexture);
		Logo.ImageSize = FVector2D(Stage.TitleTexture->GetSizeX(), Stage.TitleTexture->GetSizeY());
		const FSlateFontInfo Font(Stage.MenuFont.Get(), 22);
		auto MakeChoice = [&](const FText& Label, ECastleMenuAction Action)
		{
			return SNew(SCastleMenuTextButton).Label(Label).Font(Font)
				.OnChosen(FOnClicked::CreateUObject(Args._Controller, &ACastleMenuController::Choose, Action));
		};
		FirstChoice = MakeChoice(INVTEXT("新手教程"), ECastleMenuAction::Tutorial);
		// 左列保持一个完整的排版整体。按可用宽高等比缩放，纸纹透过字标透明通道。
		ChildSlot
		[
			SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot().Anchors(FAnchors(0.075f, 0.13f, 0.47f, 0.87f)).Offset(FMargin(0))
			[
				SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
				[
					SNew(SBox).WidthOverride(400).HeightOverride(434)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[ SNew(SBox).WidthOverride(400).HeightOverride(237)
							[ SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[ SNew(SImage).Image(&Logo) ] ] ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 33, 0, 0).HAlign(HAlign_Center)
						[ SNew(SBox).HeightOverride(51)[FirstChoice.ToSharedRef()] ]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[ SNew(SBox).HeightOverride(51)[MakeChoice(INVTEXT("开始游戏"), ECastleMenuAction::Play)] ]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[ SNew(SBox).HeightOverride(51)[MakeChoice(INVTEXT("退出游戏"), ECastleMenuAction::Quit)] ]
					]
				]
			]
			+ SConstraintCanvas::Slot().Anchors(FAnchors(0.10f, 0.88f, 0.90f, 0.99f)).Offset(FMargin(0))
			[ SAssignNew(ErrorText, STextBlock).Font(FSlateFontInfo(Stage.MenuFont.Get(), 13))
				.Justification(ETextJustify::Center).AutoWrapText(true).ColorAndOpacity(FLinearColor(0.35f, 0.035f, 0.02f)) ]
		];
	}
	TSharedPtr<SWidget> FirstChoice;
	TSharedPtr<STextBlock> ErrorText;
private:
	FSlateBrush Logo;
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
	if (!IsValid(Stage->TitleTexture) || !IsValid(Stage->MenuFont) || !IsValid(Stage->PaperMaterial))
	{
		Report(FString::Printf(TEXT("舞台 %s 菜单资产无效：标题 %s，字体 %s，纸纹 %s。"), *Stage->GetPathName(),
			*GetPathNameSafe(Stage->TitleTexture), *GetPathNameSafe(Stage->MenuFont), *GetPathNameSafe(Stage->PaperMaterial)));
		return;
	}
	SetViewTarget(Stage);
	Menu = SNew(SCastleStartMenu).Stage(Stage).Controller(this);
	GetWorld()->GetGameViewport()->AddViewportWidgetContent(Menu.ToSharedRef(), 10);
	FInputModeUIOnly Input;
	Input.SetWidgetToFocus(Menu->FirstChoice);
	Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Input);
	FailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ACastleMenuController::TravelFailed);
	FString Error;
	const UCastleMenuModel* Model = UCastleMenuModel::LoadForMenu(this, Error);
	if (!Model || !Stage->ShowModel(*Model, Error)) { Report(Error); return; }
	UE_LOG(LogMoving_Castle, Display, TEXT("开始界面已就绪：主塔 %d 组、房间 %d 组，城堡每秒旋转 2 度。"), Model->Tower.Num(), Model->Rooms.Num());
}

FReply ACastleMenuController::Choose(const ECastleMenuAction Action)
{
	// 同一帧的触摸和确认事件只派发一次旅行；旅行失败后恢复显式选择。
	if (bLeaving) { return FReply::Handled(); }
	if (Action == ECastleMenuAction::Quit)
	{
		bLeaving = true;
		UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
		return FReply::Handled();
	}
	const TCHAR* Map = Action == ECastleMenuAction::Tutorial ? TEXT("/Game/MovingCastle/Maps/TutorialMap") : TEXT("/Game/MovingCastle/Maps/MainMap");
	if (!FPackageName::DoesPackageExist(Map))
	{
		Report(FString::Printf(TEXT("无法进入地图 %s，地图包缺失。"), Map));
		return FReply::Handled();
	}
	bLeaving = true;
	UGameplayStatics::OpenLevel(this, FName(Map));
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
