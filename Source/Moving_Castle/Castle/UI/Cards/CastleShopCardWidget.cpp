// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleShopCardWidget.h"

#include "Blueprint/WidgetTree.h"
#include "CastleBuildingDefinition.h"
#include "CastleCardPortraitSubsystem.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Input/Reply.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float CardWidth = 162.0f;
	constexpr float CardHeight = 226.0f;
	constexpr TCHAR FaceAssetPath[] = TEXT("/Game/MovingCastle/UI/Cards/M_CastleCardFace.M_CastleCardFace");
	const FName PortraitParameter(TEXT("Portrait"));
	const FName TintParameter(TEXT("CardTint"));
	const FName StateParameter(TEXT("CardState"));

	/** 使用设计好的五种玩法色；灰不是一种商品颜色，必须向调用处报告配置错误。 */
	bool GetGameplayTint(ECastleTileColor Color, FLinearColor& OutColor)
	{
		const TCHAR* Hex = nullptr;
		switch (Color)
		{
		case ECastleTileColor::Red: Hex = TEXT("D54548"); break;
		case ECastleTileColor::Blue: Hex = TEXT("287ADB"); break;
		case ECastleTileColor::Green: Hex = TEXT("36A86F"); break;
		case ECastleTileColor::Yellow: Hex = TEXT("E4AB31"); break;
		case ECastleTileColor::Purple: Hex = TEXT("9658DC"); break;
		default: return false;
		}
		OutColor = FLinearColor(FColor::FromHex(Hex));
		return true;
	}

	/** 文字作为独立的 Slate 层压在箔光上，始终按界面 DPI 升缩，而不是成为捕获的一部分。 */
	UTextBlock* AddLabel(UWidgetTree& Tree, UCanvasPanel& Card, const FName Name, const FVector2D& Position,
		int32 Size, bool bBold, const FLinearColor& Color)
	{
		UTextBlock* Label = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = Size;
		Font.TypefaceFontName = bBold ? FName(TEXT("Bold")) : FName(TEXT("Regular"));
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

UCastleShopCardWidget::UCastleShopCardWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 硬引用确保打包收录卡面资产；资源路径错了明确拒绝，不替换成线框或引擎默认白图。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(FaceAssetPath);
	FaceMaterial = Material.Object;
}

void UCastleShopCardWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!ensureMsgf(WidgetTree != nullptr, TEXT("商店卡牌 %s 的 WidgetTree 未创建。"), *GetPathName()))
	{
		return;
	}
	SetVisibility(ESlateVisibility::Visible);
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(CardWidth);
	Size->SetHeightOverride(CardHeight);
	WidgetTree->RootWidget = Size;
	UCanvasPanel* Card = WidgetTree->ConstructWidget<UCanvasPanel>();
	Size->AddChild(Card);
	// 仅卡片本体参与鼠标命中；装饰、文字和状态均不吞输入，外部购买按钮不动。
	Card->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	FaceImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CardFace"));
	FaceImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanelSlot* FaceSlot = Card->AddChildToCanvas(FaceImage);
	FaceSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	FaceSlot->SetOffsets(FMargin(0.0f));

	const FLinearColor Ivory(0.95f, 0.97f, 0.98f);
	LeadingNumber = AddLabel(*WidgetTree, *Card, TEXT("LeadingNumber"), FVector2D(14.0, 5.0), 27, true, Ivory);
	TrailingNumber = AddLabel(*WidgetTree, *Card, TEXT("TrailingNumber"), FVector2D(126.0, 180.0), 25, true, Ivory);
	Title = AddLabel(*WidgetTree, *Card, TEXT("BuildingName"), FVector2D(15.0, 176.0), 16, true, Ivory);
	LevelLabel = AddLabel(*WidgetTree, *Card, TEXT("BuildingLevel"), FVector2D(15.0, 197.0), 12, false,
		FLinearColor(0.72f, 0.82f, 0.88f));
	AddLabel(*WidgetTree, *Card, TEXT("CardFamily"), FVector2D(89.0, 15.0), 10, false, Ivory)
		->SetText(NSLOCTEXT("CastleCard", "Family", "色号堡垒"));
	PortraitStatus = AddLabel(*WidgetTree, *Card, TEXT("PortraitStatus"), FVector2D(CardWidth * 0.5, CardHeight * 0.45),
		13, true, Ivory);
	CastChecked<UCanvasPanelSlot>(PortraitStatus->Slot)->SetAlignment(FVector2D(0.5, 0.5));
	PortraitStatus->SetVisibility(ESlateVisibility::Collapsed);
}

bool UCastleShopCardWidget::Configure(const FCastleBuildingDescriptor& Descriptor, const ACastleBoard* Board, bool bSold)
{
	FText Reason;
	if (!Descriptor.Validate(Reason))
	{
		return ReportSetupError(Descriptor, Reason);
	}
	if (FaceImage == nullptr || !IsValid(FaceMaterial) || FaceMaterial->GetMaterial()->MaterialDomain != MD_UI)
	{
		return ReportSetupError(Descriptor, FText::FromString(FString::Printf(TEXT("卡面没有绑定有效的 UI 材质 %s。"), FaceAssetPath)));
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

	Portrait = Portraits->RequestPortrait(Descriptor, Board, Reason);
	if (Portrait == nullptr)
	{
		return ReportSetupError(Descriptor, Reason);
	}

	Face->SetVectorParameterValue(TintParameter, Tint);
	FaceImage->SetBrushFromMaterial(Face);
	const FText CardNumber = FText::AsNumber(Descriptor.Number);
	LeadingNumber->SetText(CardNumber);
	TrailingNumber->SetText(CardNumber);
	Title->SetText(Descriptor.Definition->DisplayName);
	LevelLabel->SetText(FText::Format(NSLOCTEXT("CastleCard", "Level", "等级 {0}"), FText::AsNumber(Descriptor.Level)));
	SetRenderOpacity(bSold ? 0.40f : 1.0f);
	bHoverable = !bSold;
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
	}
}

void UCastleShopCardWidget::RefreshPortrait()
{
	if (Portrait == nullptr || Face == nullptr)
	{
		return;
	}
	const FText* Error = Portrait->GetState() == ECastleCardPortraitState::Failed ? &Portrait->GetFailureReason()
		: Portraits->GetHoverFailure(this);
	UTextureRenderTarget2D* NextTexture = nullptr;
	if (Error != nullptr)
	{
		SetPortraitStatus(NSLOCTEXT("CastleCard", "CaptureFailed", "建筑成像失败"), *Error);
	}
	else if (Portrait->GetState() == ECastleCardPortraitState::Ready)
	{
		// 悬停还在等待自己第一帧的时候留在规定的静止姿态；实际失败走上方报错，不冒充成功。
		UTextureRenderTarget2D* Hovered = Portraits->GetHoverTexture(this);
		NextTexture = Hovered != nullptr ? Hovered : Portrait->GetTexture();
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
}

void UCastleShopCardWidget::UpdateTilt(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!bHoverable || !IsValid(Portrait) || !Geometry.IsUnderLocation(Event.GetScreenSpacePosition()))
	{
		return;
	}
	const FVector2D Size = Geometry.GetLocalSize();
	if (Size.X <= 0.0 || Size.Y <= 0.0)
	{
		return; // 尚未排版的控件不可交互，不构造无效的 UV 样本。
	}
	// 使用牌自己的局部坐标，在商店 ScaleBox 与系统 DPI 下保持相同倾角响应。
	const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	Tilt = Local * 2.0 / Size - FVector2D(1.0, 1.0);
	Portraits->SetHover(this, Portrait, Tilt);
	ApplyFaceState();
}

void UCastleShopCardWidget::EndHover()
{
	if (IsValid(Portraits))
	{
		Portraits->ClearHover(this);
	}
	Tilt = FVector2D::ZeroVector;
	RefreshPortrait();
	ApplyFaceState();
}

void UCastleShopCardWidget::NativeOnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(MyGeometry, InMouseEvent);
	UpdateTilt(MyGeometry, InMouseEvent);
}

void UCastleShopCardWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	EndHover();
	Super::NativeOnMouseLeave(InMouseEvent);
}

FReply UCastleShopCardWidget::NativeOnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& InMouseEvent)
{
	UpdateTilt(MyGeometry, InMouseEvent);
	return Super::NativeOnMouseMove(MyGeometry, InMouseEvent);
}

void UCastleShopCardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshPortrait();
}

void UCastleShopCardWidget::NativeDestruct()
{
	EndHover();
	Super::NativeDestruct();
}
