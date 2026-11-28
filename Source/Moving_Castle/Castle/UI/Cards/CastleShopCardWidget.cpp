// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleShopCardWidget.h"

#include "Blueprint/WidgetTree.h"
#include "CastleBuildingDefinition.h"
#include "CastleCardPortraitSubsystem.h"
#include "CastleShopWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float CardWidth = 162.0f;
	constexpr float CardHeight = 226.0f;
	// 牌的宽度是投影单位。相机离开牌面数个牌宽，让近边/远边的差别明显却不过度张开。
	constexpr double CardProjectionDistance = 2.9;
	constexpr double CardYawLimitDegrees = 22.0;
	constexpr double CardPitchLimitDegrees = 18.0;
	constexpr float CardInspectionReturnSeconds = 0.20f;
	/** 方案 A 判定阈值：局部位移小于 6px 视为短点购买，达到或超过则进入 3D 赏牌拖拽。 */
	constexpr double CardClickDragThresholdPx = 6.0;
	constexpr TCHAR FaceAssetPath[] = TEXT("/Game/MovingCastle/UI/Cards/M_CastleCardFace.M_CastleCardFace");
	constexpr TCHAR CardPerspectiveAssetPath[] = TEXT("/Game/MovingCastle/UI/Cards/M_CastleCardPerspective.M_CastleCardPerspective");
	constexpr TCHAR ShopCardFontAssetPath[] = TEXT("/Game/MovingCastle/Fonts/MicrosoftYaHei_Font.MicrosoftYaHei_Font");
	const FName ShopCardTypefaceName(TEXT("Default"));
	const FName PortraitParameter(TEXT("Portrait"));
	const FName TintParameter(TEXT("CardTint"));
	const FName StateParameter(TEXT("CardState"));
	const FName CardComposedParameter(TEXT("ComposedCard"));
	const FName CardProjectionXParameter(TEXT("ProjectionX"));
	const FName CardProjectionYParameter(TEXT("ProjectionY"));
	const FName CardAspectParameter(TEXT("CardAspect"));
	const FName CardOpacityParameter(TEXT("CardOpacity"));

	/** 卡面使用四种玩法色的既有色值，未知颜色交给调用处报告配置错误。 */
	bool GetGameplayTint(ECastleTileColor Color, FLinearColor& OutColor)
	{
		const TCHAR* Hex = nullptr;
		switch (Color)
		{
		case ECastleTileColor::Red: Hex = TEXT("D54548"); break;
		case ECastleTileColor::Blue: Hex = TEXT("287ADB"); break;
		case ECastleTileColor::Green: Hex = TEXT("36A86F"); break;
		case ECastleTileColor::Yellow: Hex = TEXT("E4AB31"); break;
		default: return false;
		}
		OutColor = FLinearColor(FColor::FromHex(Hex));
		return true;
	}

	/** 文字作为独立的 Slate 层压在箔光上，始终按界面 DPI 升缩，而不是成为捕获的一部分。 */
	UTextBlock* AddLabel(UWidgetTree& Tree, UCanvasPanel& Card, const FName Name, const FVector2D& Position,
		int32 Size, const UFont& FontAsset, const FLinearColor& Color)
	{
		UTextBlock* Label = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Label->GetFont();
		Font.FontObject = &FontAsset;
		Font.Size = Size;
		// 使用微软雅黑资源实际登记的字重，字号和描边等样式继续沿用各文字控件的设置。
		Font.TypefaceFontName = ShopCardTypefaceName;
		Label->SetFont(Font);
		Label->SetColorAndOpacity(FSlateColor(Color));
		Label->SetShadowColorAndOpacity(FLinearColor(0.007f, 0.012f, 0.026f, 0.80f));
		Label->SetShadowOffset(FVector2D(0.0, 1.5));
		Label->SetVisibility(ESlateVisibility::HitTestInvisible);
		UCanvasPanelSlot* Slot = Card.AddChildToCanvas(Label);
		Slot->SetAutoSize(true);
		Slot->SetPosition(Position);
		return Label;
	}
}

UCastleCardFaceRetainer::UCastleCardFaceRetainer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 静止状态只用已有合成结果；着色参数和捕获帧变更由卡片主动请求重画。
	InitRenderOnInvalidation(false);
	InitRenderOnPhase(false);
}

UCastleShopCardWidget::UCastleShopCardWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 硬引用确保打包收录卡面资产；资源路径错了明确拒绝，不替换成线框或引擎默认白图。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(FaceAssetPath);
	FaceMaterial = Material.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ProjectionMaterial(CardPerspectiveAssetPath);
	PerspectiveMaterial = ProjectionMaterial.Object;
}

void UCastleShopCardWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!ensureMsgf(WidgetTree != nullptr, TEXT("商店卡牌 %s 的 WidgetTree 未创建。"), *GetPathName()))
	{
		return;
	}
	// 字体是卡面构造的前置资源，加载失败时不创建会使用引擎默认字体的控件。
	const UFont* FontAsset = LoadObject<UFont>(nullptr, ShopCardFontAssetPath);
	if (!IsValid(FontAsset))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店卡牌 %s 无法初始化：字体资产 %s 加载失败或类型不是 UFont。"),
			*GetPathName(), ShopCardFontAssetPath);
		return;
	}
	// 只接受该资产自己的运行时复合字体。离线字体或指向引擎字体的来源都无法满足卡面配置。
	const FCompositeFont* CompositeFont = FontAsset->GetCompositeFont();
	if (CompositeFont != &FontAsset->GetInternalCompositeFont())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店卡牌 %s 无法初始化：资产 %s 未使用自身的运行时复合字体。"),
			*GetPathName(), ShopCardFontAssetPath);
		return;
	}
	// 确认 Slate 能找到指定的字重和字体数据，避免它在字重缺失时改用首条字体。
	const FTypefaceEntry* Typeface = CompositeFont->DefaultTypeface.Fonts.FindByPredicate(
		[](const FTypefaceEntry& Entry) { return Entry.Name == ShopCardTypefaceName; });
	if (!Typeface || !Typeface->Font.HasFont())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("商店卡牌 %s 无法初始化：字体资产 %s 中的字重 %s 缺失或未配置字体数据。"),
			*GetPathName(), ShopCardFontAssetPath, *ShopCardTypefaceName.ToString());
		return;
	}
	SetVisibility(ESlateVisibility::Visible);
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(CardWidth);
	Size->SetHeightOverride(CardHeight);
	WidgetTree->RootWidget = Size;
	// 只有外层 Widget 命中鼠标。字、框和卡窗一起进入合成纹理，购买按钮不在这棵树里。
	FaceRetainer = WidgetTree->ConstructWidget<UCastleCardFaceRetainer>(UCastleCardFaceRetainer::StaticClass(),
		TEXT("CardProjection"));
	FaceRetainer->SetVisibility(ESlateVisibility::HitTestInvisible);
	FaceRetainer->SetTextureParameter(CardComposedParameter);
	Size->AddChild(FaceRetainer);
	UCanvasPanel* Card = WidgetTree->ConstructWidget<UCanvasPanel>();
	FaceRetainer->AddChild(Card);
	FaceImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CardFace"));
	FaceImage->SetVisibility(ESlateVisibility::Collapsed);
	UCanvasPanelSlot* FaceSlot = Card->AddChildToCanvas(FaceImage);
	FaceSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	FaceSlot->SetOffsets(FMargin(0.0f));

	const FLinearColor Ivory(0.95f, 0.97f, 0.98f);
	LeadingNumber = AddLabel(*WidgetTree, *Card, TEXT("LeadingNumber"), FVector2D(14.0, 5.0), 27, *FontAsset, Ivory);
	TrailingNumber = AddLabel(*WidgetTree, *Card, TEXT("TrailingNumber"), FVector2D(126.0, 180.0), 25, *FontAsset, Ivory);
	Title = AddLabel(*WidgetTree, *Card, TEXT("BuildingName"), FVector2D(15.0, 176.0), 16, *FontAsset, Ivory);
	LevelLabel = AddLabel(*WidgetTree, *Card, TEXT("BuildingLevel"), FVector2D(15.0, 197.0), 12, *FontAsset,
		FLinearColor(0.72f, 0.82f, 0.88f));
	AddLabel(*WidgetTree, *Card, TEXT("CardFamily"), FVector2D(89.0, 15.0), 10, *FontAsset, Ivory)
		->SetText(NSLOCTEXT("CastleCard", "Family", "色号堡垒"));
	PortraitStatus = AddLabel(*WidgetTree, *Card, TEXT("PortraitStatus"), FVector2D(CardWidth * 0.5, CardHeight * 0.45),
		13, *FontAsset, Ivory);
	CastChecked<UCanvasPanelSlot>(PortraitStatus->Slot)->SetAlignment(FVector2D(0.5, 0.5));
	PortraitStatus->SetVisibility(ESlateVisibility::Collapsed);
}

bool UCastleShopCardWidget::Configure(UCastleShopWidget& InShop, const FCastleBuildingDescriptor& Descriptor,
	const ACastleBoard* Board, bool bSold)
{
	return ConfigurePresentation(&InShop, Descriptor, Board, bSold);
}

bool UCastleShopCardWidget::ConfigureStandalone(const FCastleBuildingDescriptor& Descriptor, const ACastleBoard* Board)
{
	// 与商品共用同一取景和拖拽手感，独立牌面只浏览建筑，点击不产生购买意图。
	return ConfigurePresentation(nullptr, Descriptor, Board, false);
}

bool UCastleShopCardWidget::ConfigurePresentation(UCastleShopWidget* InShop, const FCastleBuildingDescriptor& Descriptor,
	const ACastleBoard* Board, bool bSold)
{
	FText Reason;
	if (!Descriptor.Validate(Reason))
	{
		return ReportSetupError(Descriptor, Reason);
	}
	// NativeOnInitialized 未能构造文字时，拒绝把空卡面当作配置成功。
	if (!IsValid(Title))
	{
		return ReportSetupError(Descriptor, FText::FromString(FString::Printf(TEXT("卡面文字未初始化，请检查 WidgetTree 和字体 %s。"),
			ShopCardFontAssetPath)));
	}
	if (FaceImage == nullptr || !IsValid(FaceMaterial) || FaceMaterial->GetMaterial()->MaterialDomain != MD_UI)
	{
		return ReportSetupError(Descriptor, FText::FromString(FString::Printf(TEXT("卡面没有绑定有效的 UI 材质 %s。"), FaceAssetPath)));
	}
	if (FaceRetainer == nullptr || !IsValid(PerspectiveMaterial) || PerspectiveMaterial->GetMaterial()->MaterialDomain != MD_UI
		|| PerspectiveMaterial->GetBlendMode() != BLEND_AlphaComposite)
	{
		return ReportSetupError(Descriptor, FText::FromString(FString::Printf(TEXT("整牌合成没有绑定有效的预乘 UI 材质 %s。"),
			CardPerspectiveAssetPath)));
	}
	FLinearColor Tint;
	UTexture* TextureType = nullptr;
	FLinearColor Value;
	if (!GetGameplayTint(Descriptor.Color, Tint)
		|| !FaceMaterial->GetVectorParameterValue(FMaterialParameterInfo(TintParameter), Value)
		|| !FaceMaterial->GetVectorParameterValue(FMaterialParameterInfo(StateParameter), Value)
		|| !FaceMaterial->GetTextureParameterValue(FMaterialParameterInfo(PortraitParameter), TextureType) || !IsValid(TextureType))
	{
		return ReportSetupError(Descriptor, FText::FromString(FString::Printf(TEXT("建筑色号或卡面材质 %s 的参数接口无效。"), FaceAssetPath)));
	}
	float Scalar = 0.0f;
	if (!PerspectiveMaterial->GetVectorParameterValue(FMaterialParameterInfo(CardProjectionXParameter), Value)
		|| !PerspectiveMaterial->GetVectorParameterValue(FMaterialParameterInfo(CardProjectionYParameter), Value)
		|| !PerspectiveMaterial->GetScalarParameterValue(FMaterialParameterInfo(CardAspectParameter), Scalar)
		|| !PerspectiveMaterial->GetScalarParameterValue(FMaterialParameterInfo(CardOpacityParameter), Scalar)
		|| !PerspectiveMaterial->GetTextureParameterValue(FMaterialParameterInfo(CardComposedParameter), TextureType) || !IsValid(TextureType))
	{
		return ReportSetupError(Descriptor, FText::FromString(FString::Printf(TEXT("整牌透视材质 %s 的参数接口无效。"),
			CardPerspectiveAssetPath)));
	}
	Portraits = GetWorld() ? GetWorld()->GetSubsystem<UCastleCardPortraitSubsystem>() : nullptr;
	if (Portraits == nullptr)
	{
		return ReportSetupError(Descriptor, NSLOCTEXT("CastleCard", "MissingStudio", "当前世界没有建筑卡牌摄影服务。"));
	}
	// 编辑器的按需编译可能等到第一次 Slate 绘制才触发；准备此材质后才把它接到可见卡面。
	FaceMaterial->EnsureIsComplete();
	if (!FaceMaterial->IsComplete())
	{
		return ReportSetupError(Descriptor, FText::FromString(FString::Printf(TEXT("卡面材质 %s 的着色器未能就绪。"), FaceAssetPath)));
	}
	Face = UMaterialInstanceDynamic::Create(FaceMaterial, this);
	if (!IsValid(Face))
	{
		return ReportSetupError(Descriptor, FText::FromString(FString::Printf(TEXT("无法实例化卡面材质 %s。"), FaceAssetPath)));
	}
	PerspectiveMaterial->EnsureIsComplete();
	if (!PerspectiveMaterial->IsComplete())
	{
		return ReportSetupError(Descriptor, FText::FromString(FString::Printf(TEXT("整牌透视材质 %s 的着色器未能就绪。"),
			CardPerspectiveAssetPath)));
	}
	Perspective = UMaterialInstanceDynamic::Create(PerspectiveMaterial, this);
	if (!IsValid(Perspective))
	{
		return ReportSetupError(Descriptor, FText::FromString(FString::Printf(TEXT("无法实例化整牌透视材质 %s。"),
			CardPerspectiveAssetPath)));
	}

	Portrait = Portraits->RequestPortrait(Descriptor, Board, Reason);
	if (Portrait == nullptr)
	{
		return ReportSetupError(Descriptor, Reason);
	}

	// 缓存记录的是已经写进当前卡面材质的数据。刷新商店会换材质，即使摄影纹理没变也必须重新绑定。
	DisplayedTexture = nullptr;
	DisplayedCaptureNumber = 0;
	Face->SetVectorParameterValue(TintParameter, Tint);
	FaceImage->SetBrushFromMaterial(Face);
	FaceImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	Perspective->SetScalarParameterValue(CardAspectParameter, CardHeight / CardWidth);
	// 售罄样式只在合成的出口施加透明度，不能让卡面先淡一次，再让外层 Slate 淡一次。
	Perspective->SetScalarParameterValue(CardOpacityParameter, bSold ? 0.40f : 1.0f);
	ApplyProjection();
	FaceRetainer->SetEffectMaterial(Perspective);
	const FText CardNumber = FText::AsNumber(Descriptor.Number);
	LeadingNumber->SetText(CardNumber);
	TrailingNumber->SetText(CardNumber);
	Title->SetText(Descriptor.Definition->DisplayName);
	LevelLabel->SetText(FText::Format(NSLOCTEXT("CastleCard", "Level", "等级 {0}"), FText::AsNumber(Descriptor.Level)));
	ShopOwner = InShop;
	DisplayedDescriptor = Descriptor;
	bIsSold = bSold;
	bCanInspect = !bSold;
	RefreshPortrait();
	ApplyFaceState();
	return true;
}

bool UCastleShopCardWidget::ReportSetupError(const FCastleBuildingDescriptor& Descriptor, const FText& Reason)
{
	UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡牌 %s Lv%d 初始化失败：%s"),
		*GetPathNameSafe(Descriptor.Definition.Get()), Descriptor.Level, *Reason.ToString());
	SetPortraitStatus(NSLOCTEXT("CastleCard", "SetupFailed", "卡面配置错误"), Reason);
	return false;
}

void UCastleShopCardWidget::SetPortraitStatus(const FText& Message, const FText& Details)
{
	if (PortraitStatus != nullptr && !PortraitStatus->GetText().EqualTo(Message))
	{
		PortraitStatus->SetText(Message);
		PortraitStatus->SetVisibility(Message.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		RequestFaceRender();
	}
	if (!StatusDetails.EqualTo(Details))
	{
		StatusDetails = Details;
		SetToolTipText(Details);
	}
}

void UCastleShopCardWidget::SetFoilPreview(bool bEnabled)
{
	bFoilPreview = bEnabled;
	ApplyFaceState();
}

void UCastleShopCardWidget::ApplyFaceState()
{
	if (IsValid(Face))
	{
		// 动态材质实例属于单张卡。只有已经成像的纹理能打开建筑层，箔光不参与拍摄。
		Face->SetVectorParameterValue(StateParameter, FLinearColor(Tilt.X, Tilt.Y, bFoilPreview ? 1.0f : 0.0f,
			DisplayedTexture != nullptr ? 1.0f : 0.0f));
		RequestFaceRender();
	}
}

void UCastleShopCardWidget::RequestFaceRender()
{
	if (FaceRetainer != nullptr)
	{
		FaceRetainer->RequestRender();
	}
}

void UCastleShopCardWidget::ApplyProjection()
{
	// 坐标为屏幕向右、向下、远离相机。正倾角对应右/下边靠近，先绕牌的横轴再绕屏幕纵轴。
	double SY, CY, SP, CP;
	FMath::SinCos(&SY, &CY, FMath::DegreesToRadians(Tilt.X * CardYawLimitDegrees));
	FMath::SinCos(&SP, &CP, FMath::DegreesToRadians(Tilt.Y * CardPitchLimitDegrees));
	const FVector AxisX(CY, 0.0, -SY);
	const FVector AxisY(-SY * SP, CP, -CY * SP);
	const double HalfWidth = 0.5;
	const double HalfHeight = 0.5 * static_cast<double>(CardHeight) / CardWidth;

	// 先投影四角，按离槽位边缘最近的角统一调整焦距；不单独挤压某一轴，也不改变商店布局。
	double FocalLength = CardProjectionDistance;
	for (const double X : {-HalfWidth, HalfWidth})
	{
		for (const double Y : {-HalfHeight, HalfHeight})
		{
			const FVector Corner = X * AxisX + Y * AxisY;
			const double Extent = FMath::Max(FMath::Abs(Corner.X) / HalfWidth, FMath::Abs(Corner.Y) / HalfHeight);
			FocalLength = FMath::Min(FocalLength, (CardProjectionDistance + Corner.Z) / Extent);
		}
	}
	// 材质从屏幕像素反投影到同一平面，确保圆角、建筑和文字不会沿三角形对角线扭曲。
	Perspective->SetVectorParameterValue(CardProjectionXParameter, FLinearColor(FVector4(AxisX, CardProjectionDistance)));
	Perspective->SetVectorParameterValue(CardProjectionYParameter, FLinearColor(FVector4(AxisY, FocalLength)));
}

void UCastleShopCardWidget::RefreshPortrait()
{
	if (Portrait == nullptr || Face == nullptr)
	{
		return;
	}
	const FText* Error = Portrait->GetState() == ECastleCardPortraitState::Failed ? &Portrait->GetFailureReason()
		: Portraits->GetDynamicFailure(this);
	UTextureRenderTarget2D* NextTexture = nullptr;
	uint64 CaptureNumber = 0;
	if (Error != nullptr)
	{
		SetPortraitStatus(NSLOCTEXT("CastleCard", "CaptureFailed", "建筑成像失败"), *Error);
	}
	else if (Portrait->GetState() == ECastleCardPortraitState::Ready)
	{
		// 当前角度尚未提交首帧时仍显示本牌静止缓存；实际失败走上方报错，不冒充成功。
		UTextureRenderTarget2D* Dynamic = Portraits->GetDynamicTexture(this, &CaptureNumber);
		NextTexture = Dynamic != nullptr ? Dynamic : Portrait->GetTexture();
		SetPortraitStatus(FText::GetEmpty(), FText::GetEmpty());
	}
	else
	{
		SetPortraitStatus(NSLOCTEXT("CastleCard", "Preparing", "建筑取景中"), FText::GetEmpty());
	}
	if (DisplayedTexture != NextTexture)
	{
		DisplayedTexture = NextTexture;
		if (NextTexture != nullptr)
		{
			Face->SetTextureParameterValue(PortraitParameter, NextTexture);
		}
		ApplyFaceState();
	}
	if (DisplayedCaptureNumber != CaptureNumber)
	{
		// 摄影服务复用同一个动态目标。即使指针没变，目标里的新角度也要重新进入 Retainer。
		DisplayedCaptureNumber = CaptureNumber;
		RequestFaceRender();
	}
}

void UCastleShopCardWidget::SetInspectionOffset(const FVector2D& NewOffset)
{
	if (NewOffset == InspectionOffset)
	{
		return;
	}
	InspectionOffset = NewOffset;
	// 拖向的一侧应退远，对侧抬近。在这里统一反转共享倾角，不改变重新抓牌与回正使用的位移。
	// 小幅拖动时灵敏，拖出卡位后角度仍平滑趋于上限。
	Tilt = -FVector2D(NewOffset.X / FMath::Sqrt(1.0 + FMath::Square(NewOffset.X)),
		NewOffset.Y / FMath::Sqrt(1.0 + FMath::Square(NewOffset.Y)));
	ApplyProjection();
	ApplyFaceState();
	if (InspectionOffset == FVector2D::ZeroVector)
	{
		Portraits->ClearDynamicView(this);
		RefreshPortrait();
	}
	else
	{
		Portraits->SetDynamicView(this, Portrait, Tilt);
	}
}

void UCastleShopCardWidget::FinishInspection()
{
	bReturning = false;
	SetInspectionOffset(FVector2D::ZeroVector);
	if (UCastleShopWidget* Shop = ShopOwner.Get())
	{
		Shop->ReleaseCardInspection(*this);
	}
}

void UCastleShopCardWidget::CancelInspection()
{
	const TOptional<FCardGrab> CancelledGrab = Grab;
	Grab.Reset();
	FinishInspection();
	// ReleaseCapture 会同步发出 CaptureLost。先清状态，且只归还本牌持有的那一个指针。
	if (CancelledGrab.IsSet() && FSlateApplication::IsInitialized()
		&& HasMouseCaptureByUser(CancelledGrab->UserIndex, CancelledGrab->PointerIndex))
	{
		if (const TSharedPtr<FSlateUser> User = FSlateApplication::Get().GetUser(CancelledGrab->UserIndex))
		{
			User->ReleaseCapture(CancelledGrab->PointerIndex);
		}
	}
}

void UCastleShopCardWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	OnCardHovered.Broadcast(*this, true);
}

void UCastleShopCardWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	OnCardHovered.Broadcast(*this, false);
}

FReply UCastleShopCardWidget::NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent)
{
	if (!bCanInspect || Grab.IsSet() || InMouseEvent.IsTouchEvent() || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(MyGeometry, InMouseEvent);
	}
	UCastleShopWidget* Shop = ShopOwner.Get();
	const TSharedPtr<SWidget> Captor = GetCachedWidget();
	if ((!Shop && ShopOwner.IsStale()) || !Captor.IsValid())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑卡牌 %s 无法抓取：商店或 Slate 接收控件已经失效。"), *GetPathName());
		return FReply::Unhandled();
	}
	if (Shop) { Shop->ActivateCardInspection(*this); }
	// 重新抓住同一张正在回正的牌时，从此刻的姿态接着拖，且直接视为拖拽态不误触短点购买。
	bReturning = false;
	const bool bResumingTilt = !InspectionOffset.IsZero();
	Grab.Emplace(FCardGrab{static_cast<int32>(InMouseEvent.GetUserIndex()), InMouseEvent.GetPointerIndex(),
		MyGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()), InspectionOffset, bResumingTilt});
	return FReply::Handled().CaptureMouse(Captor.ToSharedRef());
}

FReply UCastleShopCardWidget::NativeOnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent)
{
	// Slate 将快速第二次按下作为双击发送；抓牌仍是同一个手势，不另加双击动作。
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return NativeOnMouseButtonDown(MyGeometry, InMouseEvent);
	}
	return Super::NativeOnMouseButtonDoubleClick(MyGeometry, InMouseEvent);
}

FReply UCastleShopCardWidget::NativeOnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent)
{
	if (!Grab.IsSet() || !Grab->Matches(InMouseEvent.GetUserIndex(), InMouseEvent.GetPointerIndex()))
	{
		return Super::NativeOnMouseMove(MyGeometry, InMouseEvent);
	}
	// 使用未投影的局部坐标，商店 ScaleBox 或系统 DPI 不会改变手感。普通悬停没有抓牌起点。
	const FVector2D Delta = MyGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()) - Grab->Origin;
	if (!Grab->bExceededDragThreshold)
	{
		if (Delta.Size() < CardClickDragThresholdPx)
		{
			return FReply::Handled();
		}
		Grab->bExceededDragThreshold = true;
	}
	SetInspectionOffset(Grab->InitialOffset + Delta / FVector2D(CardWidth * 0.44, CardHeight * 0.35));
	return FReply::Handled();
}

FReply UCastleShopCardWidget::NativeOnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent)
{
	if (!Grab.IsSet() || !Grab->Matches(InMouseEvent.GetUserIndex(), InMouseEvent.GetPointerIndex())
		|| InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonUp(MyGeometry, InMouseEvent);
	}
	// 方案 A：位移未超过拖拽阈值且在卡面范围内松手视为短点购买；否则进入平滑回正。
	const bool bWasShortClick = !Grab->bExceededDragThreshold
		&& MyGeometry.IsUnderLocation(InMouseEvent.GetScreenSpacePosition());
	// 正常松手会触发 CaptureLost；先解绑抓牌状态，区别于窗口切走导致的强制取消。
	Grab.Reset();
	if (bWasShortClick)
	{
		FinishInspection();
		const FReply Reply = FReply::Handled().ReleaseMouseCapture();
		if (ShopOwner.IsValid()) { OnCardClicked.Broadcast(*this); }
		return Reply;
	}
	if (InspectionOffset == FVector2D::ZeroVector)
	{
		FinishInspection();
	}
	else
	{
		ReturnFromOffset = InspectionOffset;
		ReturnElapsed = 0.0f;
		bReturning = true;
	}
	return FReply::Handled().ReleaseMouseCapture();
}

void UCastleShopCardWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	if (Grab.IsSet() && Grab->Matches(CaptureLostEvent.UserIndex, CaptureLostEvent.PointerIndex))
	{
		Grab.Reset();
		FinishInspection();
	}
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
}

void UCastleShopCardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bReturning)
	{
		ReturnElapsed += InDeltaTime;
		if (ReturnElapsed >= CardInspectionReturnSeconds)
		{
			FinishInspection();
		}
		else
		{
			// 先快后慢归位。投影、卡窗与反射共用同一回正进度，结束后不再驱动摄影。
			const double Remaining = 1.0 - ReturnElapsed / CardInspectionReturnSeconds;
			SetInspectionOffset(ReturnFromOffset * FMath::Square(Remaining));
		}
	}
	RefreshPortrait();
}

void UCastleShopCardWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 菜单先完成卡牌配置再挂载，因此只有实际进入视口的独立牌驱动摄影。
	if (Portraits && Portrait && !ShopOwner.IsValid())
	{
		Portraits->SetStandalonePortraitVisible(this, Portrait, true);
	}
}

void UCastleShopCardWidget::NativeDestruct()
{
	CancelInspection();
	if (Portraits) { Portraits->SetStandalonePortraitVisible(this, Portrait, false); }
	Super::NativeDestruct();
}
