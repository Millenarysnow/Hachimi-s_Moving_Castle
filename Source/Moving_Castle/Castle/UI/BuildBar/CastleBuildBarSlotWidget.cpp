// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildBarSlotWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Castle/Appearance/CastleBuildingIconIndex.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Input/Events.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 文件名按贴花图集帧号排列，编号语义由 CastleBuildingIcons 声明。
	const TCHAR* const BuildBarIconNames[] = {
		TEXT("T_BuildBarIcon_0_NormalRoom"), TEXT("T_BuildBarIcon_1_Farm"), TEXT("T_BuildBarIcon_2_CannonTower"),
		TEXT("T_BuildBarIcon_3_ArcherTower"), TEXT("T_BuildBarIcon_4_ShieldTower"),
		TEXT("T_BuildBarIcon_5_BalloonTower"), TEXT("T_BuildBarIcon_6_BeeHive")
	};
}

UCastleBuildBarSlotWidget::UCastleBuildBarSlotWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 同一组资源在各槽共用，每个实例的图片控件和卡牌染色仍各自独立。
	static const TArray<UTexture2D*> Textures = []
	{
		TArray<UTexture2D*> Result;
		for (const TCHAR* Name : BuildBarIconNames)
		{
			const FString Path = FString::Printf(TEXT("/Game/MovingCastle/UI/BuildBar/Icons/%s.%s"), Name, Name);
			const ConstructorHelpers::FObjectFinder<UTexture2D> Texture(*Path);
			Result.Add(Texture.Object);
		}
		return Result;
	}();
	IconTextures = Textures;
}

bool UCastleBuildBarSlotWidget::ValidatePresentation() const
{
	const struct { const TCHAR* Name; const UImage* Image; } RequiredImages[] = {
		{TEXT("NormalCircle"), NormalCircle}, {TEXT("EmptyCircle"), EmptyCircle},
		{TEXT("SelectedCircle"), SelectedCircle}, {TEXT("SelectionDot"), SelectionDot}
	};
	for (const auto& RequiredImage : RequiredImages)
	{
		if (!IsValid(RequiredImage.Image) || !IsValid(RequiredImage.Image->GetBrush().GetResourceObject()))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建造槽位 WBP %s 缺少控件 %s 或其切图。"),
				*GetClass()->GetPathName(), RequiredImage.Name);
			return false;
		}
	}
	if (!IsValid(PreviewBackdrop) || !IsValid(PreviewArea) || !IsValid(CardStats))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建造槽位 WBP %s 缺少 PreviewBackdrop、PreviewArea 或 CardStats。"), *GetClass()->GetPathName());
		return false;
	}
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(BuildBarIconNames); ++Index)
	{
		if (!IconTextures.IsValidIndex(Index) || !IsValid(IconTextures[Index])
			|| IconTextures[Index]->GetFName() != FName(BuildBarIconNames[Index])
			|| IconTextures[Index]->LODGroup != TEXTUREGROUP_UI || IconTextures[Index]->CompressionSettings != TC_EditorIcon
#if WITH_EDITORONLY_DATA
			|| IconTextures[Index]->MipGenSettings != TMGS_NoMipmaps
#endif
		)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建造槽位 WBP %s 缺少有效 UI 贴图 %s 或贴图设置不符。"),
				*GetClass()->GetPathName(), BuildBarIconNames[Index]);
			return false;
		}
	}
	return true;
}

bool UCastleBuildBarSlotWidget::SetCard(const FCastleBuildingDescriptor* Card)
{
	if (!ValidatePresentation()) { return false; }
	if (Card == nullptr)
	{
		// 清空库存槽位时释放其图片刷子上的旧资源，父级照常更新空槽底图。
		if (BuildingIcon)
		{
			BuildingIcon->SetBrushResourceObject(nullptr);
		}
		PreviewBackdrop->SetVisibility(ESlateVisibility::Collapsed);
		PreviewArea->SetVisibility(ESlateVisibility::Collapsed);
		CardStats->SetVisibility(ESlateVisibility::Collapsed);
		bHasCard = false;
		return true;
	}

	FText Reason;
	if (!Card->Validate(Reason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建造槽位 WBP %s 无法显示卡牌：%s"), *GetClass()->GetPathName(), *Reason.ToString());
		return false;
	}
	const ECastleBuildingType Type = Card->GetBuildingType();
	const int32 Index = CastleBuildingIcons::FindIndex(Type);
	FLinearColor Tint;
	if (!IconTextures.IsValidIndex(Index) || !CastleTileColors::TryGetGridTint(Card->Color, Tint))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建造槽位 WBP %s 无法匹配建筑类型 %d、颜色 %d 的功能贴图。"),
			*GetClass()->GetPathName(), static_cast<int32>(Type), static_cast<int32>(Card->Color));
		return false;
	}
	if (!BuildingIcon)
	{
		// 沿用 WBP 的方形图标区域，只向空容器挂载一张静态图片。
		if (!IsValid(WidgetTree) || PreviewArea->GetChildrenCount() != 0)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建造槽位 WBP %s 缺少控件树或 PreviewArea 已被占用。"), *GetClass()->GetPathName());
			return false;
		}
		UImage* Image = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BuildingIcon"));
		if (!IsValid(Image) || !PreviewArea->AddChild(Image))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建造槽位 WBP %s 无法在 PreviewArea 挂载功能图标 %s。"),
				*GetClass()->GetPathName(), BuildBarIconNames[Index]);
			return false;
		}
		Image->SetVisibility(ESlateVisibility::HitTestInvisible);
		BuildingIcon = Image;
	}

	BuildingIcon->SetBrushFromTexture(IconTextures[Index], false);
	BuildingIcon->SetColorAndOpacity(Tint);
	CardStats->SetText(FText::Format(NSLOCTEXT("CastleBuildBar", "CardStats", "{0} · Lv{1}"), Card->Number, Card->Level));
	CardStats->SetVisibility(ESlateVisibility::HitTestInvisible);
	PreviewBackdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
	PreviewArea->SetVisibility(ESlateVisibility::HitTestInvisible);
	bHasCard = true;
	return true;
}

void UCastleBuildBarSlotWidget::SetSelected(bool bSelected)
{
	// 黄点与选中底盘共用控制器状态；即便库存改变早于输入更新，也不会给空槽画选中环。
	const bool bShowSelection = bHasCard && bSelected;
	EmptyCircle->SetVisibility(bHasCard ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	NormalCircle->SetVisibility(bHasCard && !bShowSelection ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	SelectedCircle->SetVisibility(bShowSelection ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	SelectionDot->SetVisibility(bShowSelection ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

FReply UCastleBuildBarSlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}

	// 点击空槽也消费输入，避免玩家原本想操作 HUD 时误放置或拆除世界建筑。
	if (bHasCard)
	{
		if (SlotIndex == INDEX_NONE)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建造槽位 WBP %s 收到卡牌点击，但父建造栏尚未分配槽位索引。"), *GetClass()->GetPathName());
			return FReply::Handled();
		}
		OnSlotClicked.Broadcast(SlotIndex);
	}
	return FReply::Handled();
}
