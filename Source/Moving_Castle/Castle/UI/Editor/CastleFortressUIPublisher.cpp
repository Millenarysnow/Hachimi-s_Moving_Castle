#include "CastleFortressUIPublisher.h"
#include "Moving_Castle.h"

#if WITH_EDITOR
#include "Algo/Find.h"
#include "Blueprint/WidgetTree.h"
#include "Castle/Dialogue/CastleDialogueWidget.h"
#include "Castle/Guide/CastleGuideWidgets.h"
#include "Castle/Tutorial/CastleTutorialWidgets.h"
#include "Castle/UI/Common/CastleBuildingDemolishMenuWidget.h"
#include "Castle/UI/Common/CastleControlSettingsWidget.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Castle/UI/Director/CastleTowerUpgradeRewardWidget.h"
#include "Castle/UI/Items/CastleBadgeReplaceModalWidget.h"
#include "Castle/UI/Items/CastleWildcardRoomPickerWidget.h"
#include "CastleShopWBPBuilderLibrary.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WrapBox.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"

namespace CastleFortressUI
{
	const FLinearColor Ink(0.96f, 0.91f, 0.80f);
	const FLinearColor Gold(0.96f, 0.74f, 0.37f);
	const FLinearColor DarkInk(0.12f, 0.058f, 0.026f);
	const FMargin OuterSlice(0.08f, 0.12f, 0.08f, 0.12f);
	const FMargin RowSlice(0.14f, 0.30f, 0.14f, 0.30f);
	constexpr TCHAR RootPath[] = TEXT("/Game/MovingCastle/UI/");

	// 先检查全套原图资产，任何缺图均在改动或保存蓝图前报错。
	const TCHAR* const TextureNames[] = {
		TEXT("Workshop/T_OuterFrameFill"),
		TEXT("Workshop/T_OuterFrameRim"),
		TEXT("Workshop/T_CardCrest"),
		TEXT("Workshop/T_ParchmentPanel"),
		TEXT("Workshop/T_PreviewPanel"),
		TEXT("Workshop/T_PreviewCorners"),
		TEXT("Workshop/T_PreviewDiagram"),
		TEXT("Workshop/T_LeafTopRight"),
		TEXT("Workshop/T_LeafBottomLeft"),
		TEXT("Workshop/T_TitleRule"),
		TEXT("Workshop/T_FooterDivider"),
		TEXT("Workshop/T_Cancel_Normal"),
		TEXT("Workshop/T_Cancel_Hover"),
		TEXT("Workshop/T_Cancel_Pressed"),
		TEXT("Workshop/T_Confirm_Normal"),
		TEXT("Workshop/T_Confirm_Hover"),
		TEXT("Workshop/T_Confirm_Pressed"),
		TEXT("Workshop/T_Close_Normal"),
		TEXT("Workshop/T_Close_Hover"),
		TEXT("Workshop/T_Close_Pressed"),
		TEXT("Workshop/T_Step_Normal"),
		TEXT("Workshop/T_Step_Hover"),
		TEXT("Workshop/T_Step_Pressed"),
		TEXT("Workshop/T_ColorChip_Normal"),
		TEXT("Workshop/T_ColorChip_Hover"),
		TEXT("Workshop/T_ColorChip_Selected"),
		TEXT("Workshop/T_ArrowLeft"),
		TEXT("Workshop/T_ArrowRight"),
		TEXT("Workshop/T_Close"),
		TEXT("Settings/T_MainFrameBase"),
		TEXT("Settings/T_SectionPanel"),
		TEXT("Settings/T_GearCrest"),
		TEXT("Settings/T_TitleRuleLeft"),
		TEXT("Settings/T_TitleRuleRight"),
		TEXT("Settings/T_SectionHeaderRule"),
		TEXT("Settings/T_FooterRule"),
		TEXT("Settings/T_ButtonPrimary"),
		TEXT("Settings/T_ButtonSecondary"),
		TEXT("Resources/T_MainFrame"),
		TEXT("Resources/T_RowNormal"),
		TEXT("Resources/T_RowHighlighted"),
		TEXT("Resources/T_TooltipBody"),
		TEXT("Resources/T_CastleWatermark"),
		TEXT("Resources/T_CastleCrest"),
		TEXT("Resources/T_GoldCoins"),
		TEXT("Resources/T_TowerBalloons"),
		TEXT("Resources/T_RoomHouse"),
	};

	struct FPalette
	{
		TMap<FString, UTexture2D*> Textures;
		UFont* Font = nullptr;

		bool Load()
		{
			constexpr TCHAR FontPath[] = TEXT("/Game/MovingCastle/Fonts/MicrosoftYaHei_Font.MicrosoftYaHei_Font");
			Font = LoadObject<UFont>(nullptr, FontPath);
			if (!Font)
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("堡垒弹窗缺少字体 %s。"), FontPath);
				return false;
			}
			for (const TCHAR* Key : TextureNames)
			{
				const FString Path = FString(RootPath) + TEXT("FortressKit/") + Key;
				const FString ObjectPath = Path + TEXT(".") + FPackageName::GetShortName(Path);
				auto* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
				// 离线生成读取 PNG 源尺寸，避免无头运行时目标平台的纹理派生数据尚未就绪。
				if (!Texture || Texture->Source.GetSizeX() <= 0 || Texture->Source.GetSizeY() <= 0 || !Texture->SRGB
					|| !Texture->NeverStream || Texture->LODGroup != TEXTUREGROUP_UI || Texture->MipGenSettings != TMGS_NoMipmaps)
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("堡垒贴图 %s 缺失、尺寸或纹理设置无效。"), *ObjectPath);
					return false;
				}
				Textures.Add(Key, Texture);
			}
			return true;
		}

		FVector2D SourceSize(const TCHAR* Key) const
		{
			const auto* Texture = Textures.FindChecked(Key);
			return FVector2D(Texture->Source.GetSizeX(), Texture->Source.GetSizeY());
		}

		FSlateBrush Brush(const TCHAR* Key, FVector2D Size, FMargin Slice = FMargin(0)) const
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(Textures.FindChecked(Key));
			Brush.SetImageSize(Size);
			Brush.DrawAs = Slice == FMargin(0) ? ESlateBrushDrawType::Image : ESlateBrushDrawType::Box;
			Brush.Margin = Slice;
			Brush.TintColor = FSlateColor(FLinearColor::White);
			return Brush;
		}
	};

	/** 布局使用逻辑像素，主画布整体等比缩放。小装饰单独使用透明留边的原图尺寸。 */
	struct FLayout
	{
		UWidgetTree& Tree;
		const FPalette& Palette;

		template<class T>
		T* Add(FName Name = NAME_None)
		{
			return Tree.ConstructWidget<T>(T::StaticClass(), Name);
		}

		void Place(UCanvasPanel& Canvas, UWidget& Child, FVector2D Position, FVector2D Size)
		{
			auto* Slot = Canvas.AddChildToCanvas(&Child);
			Slot->SetPosition(Position);
			Slot->SetSize(Size);
		}

		void Anchor(UCanvasPanel& Canvas, UWidget& Child, FAnchors Anchors)
		{
			auto* Slot = Canvas.AddChildToCanvas(&Child);
			Slot->SetAnchors(Anchors);
			Slot->SetOffsets(FMargin(0));
		}

		UCanvasPanel& Root()
		{
			auto* Root = Add<UCanvasPanel>();
			Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			Tree.RootWidget = Root;
			return *Root;
		}

		UCanvasPanel& Modal(FVector2D DesignSize, FName ShellName, const FAnchors& Bounds)
		{
			auto& Screen = Root();
			auto* Scale = Add<UScaleBox>();
			Scale->SetStretch(EStretch::ScaleToFit);
			Scale->SetStretchDirection(EStretchDirection::DownOnly);
			Scale->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			Anchor(Screen, *Scale, Bounds);
			auto* Shell = Add<UBorder>(ShellName);
			Shell->SetPadding(FMargin(0));
			FSlateBrush Empty;
			Empty.DrawAs = ESlateBrushDrawType::NoDrawType;
			Shell->SetBrush(Empty);
			auto* Size = Add<USizeBox>();
			Size->SetWidthOverride(DesignSize.X);
			Size->SetHeightOverride(DesignSize.Y);
			auto* Canvas = Add<UCanvasPanel>();
			Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			Size->SetContent(Canvas);
			Shell->SetContent(Size);
			auto* Slot = CastChecked<UScaleBoxSlot>(Scale->AddChild(Shell));
			Slot->SetHorizontalAlignment(HAlign_Center);
			Slot->SetVerticalAlignment(VAlign_Center);
			return *Canvas;
		}

		UTextBlock* Text(FName Name, int32 Size, const TCHAR* Value = TEXT(""), FLinearColor Color = Ink)
		{
			auto* Label = Add<UTextBlock>(Name);
			Label->SetText(FText::FromString(Value));
			Label->SetFont(FSlateFontInfo(Palette.Font, Size));
			Label->SetColorAndOpacity(FSlateColor(Color));
			Label->SetAutoWrapText(true);
			Label->SetVisibility(ESlateVisibility::HitTestInvisible);
			return Label;
		}

		UTextBlock* Centered(FName Name, int32 Size, const TCHAR* Value = TEXT(""), FLinearColor Color = Ink)
		{
			auto* Label = Text(Name, Size, Value, Color);
			Label->SetJustification(ETextJustify::Center);
			return Label;
		}

		UImage* Image(const TCHAR* Key, FVector2D Size, FName Name = NAME_None, FMargin Slice = FMargin(0))
		{
			auto* Widget = Add<UImage>(Name);
			Widget->SetBrush(Palette.Brush(Key, Size, Slice));
			Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
			return Widget;
		}

		void Art(UCanvasPanel& Canvas, const TCHAR* Key, FVector2D Position, FVector2D Size, FMargin Slice = FMargin(0))
		{
			Place(Canvas, *Image(Key, Size, NAME_None, Slice), Position, Size);
		}

		void WorkshopArt(UCanvasPanel& Canvas, const TCHAR* Key, FVector2D ContentPosition)
		{
			// 原始工坊切图每边有六逻辑像素的透明影边。
			Art(Canvas, Key, ContentPosition - FVector2D(6, 6), Palette.SourceSize(Key) / 2.0);
		}

		void Style(UButton& Button, const TCHAR* Normal, const TCHAR* Hover, const TCHAR* Pressed,
			const TCHAR* Disabled, FVector2D NaturalSize, FMargin Slice, FMargin Padding = FMargin(0))
		{
			FButtonStyle Style = Button.GetStyle();
			Style.SetNormal(Palette.Brush(Normal, NaturalSize, Slice));
			Style.SetHovered(Palette.Brush(Hover, NaturalSize, Slice));
			Style.SetPressed(Palette.Brush(Pressed, NaturalSize, Slice));
			Style.SetDisabled(Palette.Brush(Disabled, NaturalSize, Slice));
			Style.SetNormalPadding(Padding);
			Style.SetPressedPadding(Padding);
			Button.SetBackgroundColor(FLinearColor::White);
			Button.SetStyle(Style);
		}

		void WorkshopStyle(UButton& Button, const FString& Set, FVector2D Size, FMargin Slice = FMargin(0))
		{
			const FString Path = FString(TEXT("Workshop/T_")) + Set;
			const FString Normal = Path + TEXT("_Normal");
			const FString Hover = Path + TEXT("_Hover");
			const FString Pressed = Path + TEXT("_Pressed");
			Style(Button, *Normal, *Hover, *Pressed, *Normal, Size, Slice);
		}

		UButton* Footer(FName Name, const TCHAR* Label, bool bPrimary)
		{
			auto* Button = Add<UButton>(Name);
			WorkshopStyle(*Button, bPrimary ? TEXT("Confirm") : TEXT("Cancel"), {104, 48}, FMargin(.27f));
			Button->SetContent(Centered(NAME_None, 22, Label, bPrimary ? DarkInk : Ink));
			return Button;
		}

		UButton* IconButton(FName Name, const TCHAR* Set, const TCHAR* Key, FVector2D ButtonSize)
		{
			auto* Button = Add<UButton>(Name);
			WorkshopStyle(*Button, Set, ButtonSize);
			const FVector2D Source = Palette.SourceSize(Key);
			Button->SetContent(Image(Key, Source * (20.0 / Source.GetMax())));
			return Button;
		}

		void RowStyle(UButton& Button, FVector2D Size = {260, 60})
		{
			Style(Button, TEXT("Resources/T_RowNormal"), TEXT("Resources/T_RowHighlighted"), TEXT("Resources/T_RowHighlighted"),
				TEXT("Resources/T_RowNormal"), Size, RowSlice, FMargin(20, 4));
		}

		UWidgetSwitcher* IconStates(FName Name, const TCHAR* Key)
		{
			auto* States = Add<UWidgetSwitcher>(Name);
			const FVector2D Size = Palette.SourceSize(Key);
			for (const FLinearColor Tint : {FLinearColor::White, FLinearColor(0.74f, 0.74f, 0.74f)})
			{
				auto* Icon = Image(Key, Size * (58 / Size.GetMax()));
				Icon->SetColorAndOpacity(Tint);
				States->AddChild(Icon);
			}
			States->SetVisibility(ESlateVisibility::HitTestInvisible);
			return States;
		}

		UButton* Reward(FName Name, FName Label, const TCHAR* TextValue, UWidget& Icon)
		{
			auto* Row = Add<UButton>(Name);
			RowStyle(*Row, {250, 89});
			auto* Content = Add<UHorizontalBox>();
			auto* IconBox = Add<USizeBox>();
			IconBox->SetWidthOverride(76);
			auto* Scale = Add<UScaleBox>();
			Scale->SetStretch(EStretch::ScaleToFit);
			Scale->SetStretchDirection(EStretchDirection::DownOnly);
			Scale->SetContent(&Icon);
			IconBox->SetContent(Scale);
			Content->AddChildToHorizontalBox(IconBox)->SetVerticalAlignment(VAlign_Center);
			auto* Slot = Content->AddChildToHorizontalBox(Text(Label, 24, TextValue));
			Slot->SetPadding(FMargin(12, 0, 8, 0));
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Slot->SetVerticalAlignment(VAlign_Center);
			auto* ButtonSlot = CastChecked<UButtonSlot>(Row->AddChild(Content));
			ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
			return Row;
		}

		void WorkshopFrame(UCanvasPanel& Canvas, FVector2D Position, FVector2D Size, FMargin Slice = FMargin(0))
		{
			Art(Canvas, TEXT("Workshop/T_OuterFrameFill"), Position, Size, Slice);
			Art(Canvas, TEXT("Workshop/T_OuterFrameRim"), Position, Size, Slice);
		}

		UCanvasPanel& RewardFrame(const TCHAR* Title)
		{
			// 使用原始 605×793 纵框，顶部留够盾徽伸出框体的空间。
			auto& Canvas = Modal({605, 856}, TEXT("FortressPanel"), FAnchors(.05f, .035f, .95f, .965f));
			Art(Canvas, TEXT("Resources/T_MainFrame"), {0, 63}, {605, 793});
			Art(Canvas, TEXT("Resources/T_CastleCrest"), {254, 4}, {96, 110});
			Art(Canvas, TEXT("Resources/T_CastleWatermark"), {184, 710}, {237, 111});
			Place(Canvas, *Centered(NAME_None, 42, Title, Gold), {44, 129}, {517, 63});
			return Canvas;
		}
	};

	void BuildWorkshop(FLayout& L)
	{
		auto& Canvas = L.Modal({1060, 700}, TEXT("PickerPanel"), FAnchors(.02f, .02f, .98f, .98f));
		L.WorkshopFrame(Canvas, {0, 40}, {1060, 654.77});
		L.WorkshopArt(Canvas, TEXT("Workshop/T_CardCrest"), {496.5, 7});
		L.WorkshopArt(Canvas, TEXT("Workshop/T_TitleRule"), {250, 107});
		L.WorkshopArt(Canvas, TEXT("Workshop/T_TitleRule"), {724, 107});
		L.Place(Canvas, *L.Centered(TEXT("RewardTitle"), 28, TEXT("卡牌工坊"), Gold), {346, 89}, {368, 48});
		auto* Close = L.IconButton(TEXT("CloseButton"), TEXT("Close"), TEXT("Workshop/T_Close"), {46, 46});
		L.Place(Canvas, *Close, {972, 63}, {46, 46});
		L.WorkshopArt(Canvas, TEXT("Workshop/T_PreviewPanel"), {48, 157.77});
		L.WorkshopArt(Canvas, TEXT("Workshop/T_PreviewCorners"), {53, 162.77});
		L.WorkshopArt(Canvas, TEXT("Workshop/T_ParchmentPanel"), {622, 157.77});
		L.WorkshopArt(Canvas, TEXT("Workshop/T_LeafTopRight"), {967, 158.77});
		L.WorkshopArt(Canvas, TEXT("Workshop/T_LeafBottomLeft"), {621, 541.77});
		L.WorkshopArt(Canvas, TEXT("Workshop/T_FooterDivider"), {48, 598.77});

		auto* Placeholder = L.Add<UCanvasPanel>(TEXT("PreviewPlaceholder"));
		Placeholder->SetVisibility(ESlateVisibility::HitTestInvisible);
		L.Place(Canvas, *Placeholder, {0, 0}, {1060, 700});
		L.WorkshopArt(*Placeholder, TEXT("Workshop/T_PreviewDiagram"), {100, 168.27});
		L.Place(*Placeholder, *L.Centered(NAME_None, 20, TEXT("选齐建筑、颜色和数字后预览卡牌")), {77, 530}, {486, 35});
		L.Place(Canvas, *L.Centered(NAME_None, 20, TEXT("卡牌预览"), Gold), {77, 181}, {486, 35});

		auto* CardScale = L.Add<UScaleBox>();
		CardScale->SetStretch(EStretch::ScaleToFit);
		CardScale->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		auto* CardSlot = L.Add<USizeBox>(TEXT("PreviewCardSlot"));
		CardSlot->SetWidthOverride(162);
		CardSlot->SetHeightOverride(226);
		CardScale->SetContent(CardSlot);
		L.Place(Canvas, *CardScale, {188, 222}, {268, 312});

		L.Place(Canvas, *L.Text(NAME_None, 27, TEXT("配置房间"), DarkInk), {647, 177}, {280, 39});
		const auto Choices = [&](FName Name, const TCHAR* Caption, double Top, double Height)
		{
			L.Place(Canvas, *L.Text(NAME_None, 18, Caption, DarkInk), {647, Top}, {340, 27});
			auto* Wrap = L.Add<UWrapBox>(Name);
			Wrap->SetInnerSlotPadding({4, 4});
			auto* Scroll = L.Add<UScrollBox>();
			Scroll->AddChild(Wrap);
			L.Place(Canvas, *Scroll, {647, Top + 28}, {340, Height});
		};
		Choices(TEXT("DefinitionChoices"), TEXT("建筑类型"), 226, 108);
		Choices(TEXT("ColorChoices"), TEXT("颜色"), 369, 54);
		Choices(TEXT("NumberChoices"), TEXT("数字"), 460, 76);
		L.Place(Canvas, *L.Text(TEXT("StatusText"), 18), {56, 613}, {682, 52});
		L.Place(Canvas, *L.Footer(TEXT("CancelButton"), TEXT("取消"), false), {744, 606}, {119, 54});
		L.Place(Canvas, *L.Footer(TEXT("ConfirmButton"), TEXT("确认"), true), {863, 606}, {157, 54});

		auto* Option = L.Add<UButton>(TEXT("OptionStyleTemplate"));
		L.Style(*Option, TEXT("Settings/T_ButtonSecondary"), TEXT("Settings/T_ButtonPrimary"), TEXT("Settings/T_ButtonPrimary"),
			TEXT("Settings/T_ButtonPrimary"), {32, 32}, FMargin(.25f), FMargin(9, 3));
		Option->SetVisibility(ESlateVisibility::Collapsed);
		Option->SetContent(L.Text(TEXT("OptionTextTemplate"), 16, TEXT(""), Ink));
		L.Place(Canvas, *Option, {0, 0}, {0, 0});
		auto* Color = L.Add<UButton>(TEXT("ColorStyleTemplate"));
		L.Style(*Color, TEXT("Workshop/T_ColorChip_Normal"), TEXT("Workshop/T_ColorChip_Hover"),
			TEXT("Workshop/T_ColorChip_Selected"), TEXT("Workshop/T_ColorChip_Selected"), {63, 42}, FMargin(.22f), FMargin(7, 4));
		Color->SetVisibility(ESlateVisibility::Collapsed);
		L.Place(Canvas, *Color, {0, 0}, {0, 0});

		// 常驻待获取按钮和工坊分别命中，隐藏其中一个不会改变另一方的定位。
		auto* Root = CastChecked<UCanvasPanel>(L.Tree.RootWidget);
		auto* Claim = L.Add<UButton>(TEXT("ClaimButton"));
		L.WorkshopStyle(*Claim, TEXT("Confirm"), {104, 48}, FMargin(.27f));
		Claim->SetContent(L.Centered(TEXT("ClaimLabel"), 19, TEXT("获取"), DarkInk));
		auto* ClaimSlot = Root->AddChildToCanvas(Claim);
		ClaimSlot->SetAnchors(FAnchors(.5f, .80f));
		ClaimSlot->SetAlignment({.5f, .5f});
		ClaimSlot->SetSize({300, 56});
	}

	void BuildSettings(FLayout& L)
	{
		auto& Canvas = L.Modal({1199, 813}, TEXT("FortressPanel"), FAnchors(.035f, .04f, .965f, .96f));
		L.Art(Canvas, TEXT("Settings/T_MainFrameBase"), {0, 0}, {1199, 813});
		L.Art(Canvas, TEXT("Settings/T_GearCrest"), {525, 0}, {147, 160});
		L.Art(Canvas, TEXT("Settings/T_TitleRuleLeft"), {364, 194}, {114, 14});
		L.Art(Canvas, TEXT("Settings/T_TitleRuleRight"), {720, 194}, {114, 14});
		L.Place(Canvas, *L.Centered(NAME_None, 43, TEXT("游戏设置"), Gold), {478, 164}, {242, 76});
		const auto Section = [&](double Left, FName Name, const TCHAR* Title, FName KeyName)
		{
			L.Art(Canvas, TEXT("Settings/T_SectionPanel"), {Left, 265}, {516, 387});
			L.Art(Canvas, TEXT("Settings/T_SectionHeaderRule"), {Left + 26, 374}, {465, 4});
			auto* Mode = L.Add<UButton>(Name);
			L.Style(*Mode, TEXT("Settings/T_ButtonSecondary"), TEXT("Settings/T_ButtonPrimary"), TEXT("Settings/T_ButtonPrimary"),
				TEXT("Settings/T_ButtonPrimary"), {365, 70}, FMargin(.25f));
			Mode->SetContent(L.Centered(NAME_None, 27, Title, Ink));
			L.Place(Canvas, *Mode, {Left + 54, 291}, {408, 66});
			L.Place(Canvas, *L.Centered(KeyName, 24), {Left + 39, 415}, {438, 171});
		};
		Section(66, TEXT("ClassicButton"), TEXT("经典模式（MC）"), TEXT("ClassicKeys"));
		Section(616, TEXT("DesignButton"), TEXT("设计模式（动物园之星）"), TEXT("DesignKeys"));
		L.Art(Canvas, TEXT("Settings/T_FooterRule"), {66, 677}, {1066, 6});
		L.Place(Canvas, *L.Footer(TEXT("CloseButton"), TEXT("返回游戏"), true), {472, 704}, {254, 66});
	}

	void BuildRewardChoice(FLayout& L)
	{
		auto& Canvas = L.RewardFrame(TEXT("战斗奖励"));
		const auto Icon = [&](const TCHAR* Key, FName Name = NAME_None)
		{
			const FVector2D Source = L.Palette.SourceSize(Key);
			return L.Image(Key, Source * (63.0 / Source.GetMax()), Name);
		};
		// 预览展示正式配置的基础金币，运行时标签读取导演配置。
		L.Place(Canvas, *L.Reward(TEXT("GoldButton"), TEXT("GoldLabel"), TEXT("金币 +60"),
			*Icon(TEXT("Resources/T_GoldCoins"))), {28, 205}, {549, 112});
		L.Place(Canvas, *L.Reward(TEXT("HealingButton"), TEXT("HealingLabel"), TEXT("主塔回血"),
			*Icon(TEXT("Resources/T_TowerBalloons"), TEXT("HealingIcon"))), {28, 331}, {549, 112});
		L.Place(Canvas, *L.Reward(TEXT("ShopButton"), TEXT("ShopLabel"), TEXT("房间商店"),
			*L.IconStates(TEXT("ShopIconStates"), TEXT("Resources/T_RoomHouse"))), {28, 457}, {549, 112});
		L.Place(Canvas, *L.Centered(TEXT("FailureText"), 22), {45, 600}, {515, 96});
	}

	void BuildTutorialReward(FLayout& L)
	{
		auto& Canvas = L.RewardFrame(TEXT("战斗奖励"));
		auto* Choices = L.Add<UVerticalBox>(TEXT("Choices"));
		const auto Reward = [&](FName Name, const TCHAR* Label, const TCHAR* Key)
		{
			const FVector2D Source = L.Palette.SourceSize(Key);
			auto* Icon = L.Image(Key, Source * (62.0 / Source.GetMax()));
			auto* Slot = Choices->AddChildToVerticalBox(L.Reward(Name, NAME_None, Label, *Icon));
			Slot->SetPadding(FMargin(0, 0, 0, 12));
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		};
		// 教程第一项只显示基础金币，实际可领的复制奖励仍是第三项。
		Reward(TEXT("GoldButton"), TEXT("金币 +60"), TEXT("Resources/T_GoldCoins"));
		Reward(TEXT("HealingButton"), TEXT("主塔回血"), TEXT("Resources/T_TowerBalloons"));
		Reward(TEXT("CopyButton"), TEXT("获得复制的随机功能房间"), TEXT("Resources/T_RoomHouse"));
		L.Place(Canvas, *Choices, {28, 205}, {549, 375});
		auto* Card = L.Add<UButton>(TEXT("CardButton"));
		UCastleShopWBPBuilderLibrary::ConfigureTransparentButton(Card, false);
		Card->SetVisibility(ESlateVisibility::Collapsed);
		L.Place(Canvas, *Card, {176, 215}, {253, 370});
	}

	void BuildTowerReward(FLayout& L)
	{
		auto& Canvas = L.RewardFrame(TEXT("主塔升级奖励"));
		auto* Rows = L.Add<UScrollBox>(TEXT("RewardRows"));
		L.Place(Canvas, *Rows, {32, 212}, {541, 450});
		L.Place(Canvas, *L.Footer(TEXT("ContinueButton"), TEXT("继续"), true), {200, 680}, {205, 62});
		auto* Template = L.Add<UButton>(TEXT("RewardButtonTemplate"));
		L.RowStyle(*Template);
		Template->SetVisibility(ESlateVisibility::Collapsed);
		Template->SetContent(L.Centered(TEXT("RewardTextTemplate"), 23));
		L.Place(Canvas, *Template, {0, 0}, {0, 0});
		// 图标都由 WBP 提供，运行时行按奖励类型复用相应的透明插画。
		L.Place(Canvas, *L.Image(TEXT("Resources/T_GoldCoins"), {52, 41}, TEXT("PathRewardIcon")), {0, 0}, {0, 0});
		L.Place(Canvas, *L.Image(TEXT("Resources/T_CastleCrest"), {48, 55}, TEXT("NPCRewardIcon")), {0, 0}, {0, 0});
		L.Place(Canvas, *L.Image(TEXT("Resources/T_RoomHouse"), {53, 53}, TEXT("RoomRewardIcon")), {0, 0}, {0, 0});
	}

	void BuildGuide(FLayout& L, bool bBook)
	{
		auto& Canvas = L.Modal({1060, 655}, TEXT("FortressPanel"), FAnchors(.045f, .05f, .955f, .95f));
		L.WorkshopFrame(Canvas, {0, 0}, {1060, 655});
		L.Place(Canvas, *L.Centered(TEXT("EntryTitle"), 35, TEXT(""), Gold), {89, 46}, {882, 64});
		auto* Picture = L.Add<UScaleBox>();
		Picture->SetStretch(EStretch::ScaleToFit);
		Picture->SetContent(L.Add<UImage>(TEXT("Illustration")));
		L.Place(Canvas, *Picture, {250, 124}, {560, 247});
		auto* Description = L.Add<UScrollBox>();
		Description->AddChild(L.Text(TEXT("EntryDescription"), 24));
		L.Place(Canvas, *Description, {87, 390}, {886, 143});
		if (bBook)
		{
			L.Place(Canvas, *L.Centered(TEXT("EmptyMessage"), 27, TEXT("尚未收录词条")), {230, 266}, {600, 55});
			L.Place(Canvas, *L.IconButton(TEXT("PreviousButton"), TEXT("Step"), TEXT("Workshop/T_ArrowLeft"), {55, 54}),
				{377, 556}, {55, 54});
			L.Place(Canvas, *L.Centered(TEXT("PageStatus"), 20), {441, 568}, {178, 40});
			L.Place(Canvas, *L.IconButton(TEXT("NextButton"), TEXT("Step"), TEXT("Workshop/T_ArrowRight"), {55, 54}),
				{628, 556}, {55, 54});
		}
		L.Place(Canvas, *L.IconButton(TEXT("CloseButton"), TEXT("Close"), TEXT("Workshop/T_Close"), {46, 46}),
			{972, 34}, {46, 46});
	}

	void BuildBadge(FLayout& L)
	{
		auto& Canvas = L.Modal({795, 538}, TEXT("FortressPanel"), FAnchors(.045f, .05f, .955f, .95f));
		L.WorkshopFrame(Canvas, {0, 0}, {795, 538}, OuterSlice);
		L.Place(Canvas, *L.Centered(NAME_None, 32, TEXT("徽章替换"), Gold), {58, 32}, {679, 49});
		L.Place(Canvas, *L.Centered(TEXT("PendingBadgeLabel"), 22), {63, 89}, {669, 77});
		auto* Rows = L.Add<UVerticalBox>(TEXT("ReplaceSlots"));
		for (int32 Index = 0; Index < 6; ++Index)
		{
			auto* Button = L.Add<UButton>();
			L.RowStyle(*Button, {250, 38});
			// 徽章表示层直接读取 UButton 的文本子节点。
			Button->SetContent(L.Centered(NAME_None, 20));
			auto* Slot = Rows->AddChildToVerticalBox(Button);
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Slot->SetPadding(FMargin(0, 0, 0, 3));
		}
		L.Place(Canvas, *Rows, {55, 171}, {685, 259});
		L.Place(Canvas, *L.Footer(TEXT("DiscardButton"), TEXT("舍弃新徽章"), false), {275, 444}, {245, 55});
	}

	void BuildDemolish(FLayout& L)
	{
		auto& Root = L.Root();
		auto* Panel = L.Add<UBorder>(TEXT("PopupPanel"));
		Panel->SetBrush(L.Palette.Brush(TEXT("Resources/T_TooltipBody"), {208, 290}, FMargin(.18f, .30f)));
		Panel->SetPadding(FMargin(20, 16));
		Panel->SetHorizontalAlignment(HAlign_Center);
		auto* Column = L.Add<UVerticalBox>();
		auto* Card = L.Add<USizeBox>(TEXT("BuildingCardSlot"));
		Card->SetWidthOverride(162);
		Card->SetHeightOverride(226);
		Column->AddChildToVerticalBox(Card)->SetHorizontalAlignment(HAlign_Center);
		Column->AddChildToVerticalBox(L.Footer(TEXT("DemolishButton"), TEXT("拆除"), false))->SetPadding(FMargin(0, 8, 0, 0));
		Panel->SetContent(Column);
		Root.AddChildToCanvas(Panel)->SetAutoSize(true);
	}

	// 保持原对话树及其锚点，只替换底板和增加分支按钮样式模板。
	bool StyleDialogue(UWidgetBlueprint& Asset, const FPalette& Palette)
	{
		FLayout L{*Asset.WidgetTree, Palette};
		auto* Panel = Cast<UOverlay>(Asset.WidgetTree->FindWidget(TEXT("DialoguePanel")));
		auto* Border = Panel ? Cast<UBorder>(Panel->GetChildAt(0)) : nullptr;
		auto* Continue = Panel ? Cast<UButton>(Panel->GetChildAt(1)) : nullptr;
		if (!Border || !Continue || Continue->GetFName() != TEXT("ContinueButton") || Continue->GetContent())
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("%s 缺少独立点击层或底栏的原始布局。"), *Asset.GetPathName());
			return false;
		}
		if (auto* Existing = Cast<UOverlay>(Border->GetContent()); Existing && Existing->GetFName() == TEXT("FortressDialogueSkin"))
		{
			// 再次生成时直接更新两层笔刷，至关重要的正文和点击层均留在原槽位。
			auto* Fill = Cast<UImage>(Existing->GetChildAt(0));
			auto* Rim = Cast<UImage>(Existing->GetChildAt(1));
			auto* Template = Cast<UButton>(Asset.WidgetTree->FindWidget(TEXT("ChoiceButtonTemplate")));
			if (!Fill || !Rim || !Template || !Existing->GetChildAt(2))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("%s 的堡垒对话底栏结构不完整。"), *Asset.GetPathName());
				return false;
			}
			Fill->SetBrush(Palette.Brush(TEXT("Workshop/T_OuterFrameFill"), {650, 90}, OuterSlice));
			Rim->SetBrush(Palette.Brush(TEXT("Workshop/T_OuterFrameRim"), {650, 90}, OuterSlice));
			L.RowStyle(*Template);
			return true;
		}
		auto* Skin = L.Add<UOverlay>(TEXT("FortressDialogueSkin"));
		for (const TCHAR* Key : {TEXT("Workshop/T_OuterFrameFill"), TEXT("Workshop/T_OuterFrameRim")})
		{
			auto* Slot = Skin->AddChildToOverlay(L.Image(Key, {650, 90}, NAME_None, OuterSlice));
			Slot->SetHorizontalAlignment(HAlign_Fill);
			Slot->SetVerticalAlignment(VAlign_Fill);
		}
		// 在旧底板内部承载切图和原正文，继续按钮仍是底板的兄弟节点。
		UWidget* Body = Border->GetContent();
		if (!Body)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("%s 的对话正文不可用。"), *Asset.GetPathName());
			return false;
		}
		Border->ClearChildren();
		auto* BodySlot = Skin->AddChildToOverlay(Body);
		BodySlot->SetHorizontalAlignment(HAlign_Fill);
		BodySlot->SetVerticalAlignment(VAlign_Fill);
		BodySlot->SetPadding(FMargin(28, 10));
		FSlateBrush Empty;
		Empty.DrawAs = ESlateBrushDrawType::NoDrawType;
		Border->SetBrush(Empty);
		Border->SetPadding(FMargin(0));
		Border->SetContent(Skin);
		UCastleShopWBPBuilderLibrary::ConfigureTransparentButton(Continue, false);
		auto* Template = L.Add<UButton>(TEXT("ChoiceButtonTemplate"));
		Template->SetVisibility(ESlateVisibility::Collapsed);
		L.RowStyle(*Template);
		Panel->AddChildToOverlay(Template);
		return true;
	}

	bool StyleGameOver(UWidgetBlueprint& Asset, const FPalette& Palette)
	{
		// 结算保留现有骷髅和重试两张图标，替换其背景与按钮状态。
		const auto* SkullSource = Cast<UImage>(Asset.WidgetTree->FindWidget(TEXT("SkullIcon")));
		const auto* RestartSource = Cast<UWidgetSwitcher>(Asset.WidgetTree->FindWidget(TEXT("RestartIconStates")));
		const auto* Resting = RestartSource ? Cast<UImage>(RestartSource->GetWidgetAtIndex(0)) : nullptr;
		const auto* Pressed = RestartSource ? Cast<UImage>(RestartSource->GetWidgetAtIndex(1)) : nullptr;
		if (!SkullSource || !Resting || !Pressed || !SkullSource->GetBrush().GetResourceObject()
			|| !Resting->GetBrush().GetResourceObject() || !Pressed->GetBrush().GetResourceObject())
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("%s 缺少结算骷髅或重试双态图标。"), *Asset.GetPathName());
			return false;
		}
		const FSlateBrush SkullBrush = SkullSource->GetBrush();
		const FSlateBrush RestartBrushes[] = {Resting->GetBrush(), Pressed->GetBrush()};
		if (!Asset.WidgetTree->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("%s 无法释放旧结算组件树。"), *Asset.GetPathName());
			return false;
		}
		Asset.WidgetTree = NewObject<UWidgetTree>(&Asset, TEXT("WidgetTree"), RF_Transactional);
		FLayout L{*Asset.WidgetTree, Palette};
		auto& Canvas = L.Modal({795, 538}, TEXT("FortressPanel"), FAnchors(.045f, .05f, .955f, .95f));
		L.WorkshopFrame(Canvas, {0, 0}, {795, 538}, OuterSlice);
		auto* Skull = L.Add<UImage>(TEXT("SkullIcon"));
		Skull->SetBrush(SkullBrush);
		Skull->SetVisibility(ESlateVisibility::HitTestInvisible);
		auto* Scale = L.Add<UScaleBox>();
		Scale->SetStretch(EStretch::ScaleToFit);
		Scale->SetContent(Skull);
		L.Place(Canvas, *Scale, {335, 55}, {125, 96});
		L.Place(Canvas, *L.Centered(TEXT("SummaryText"), 35, TEXT(""), Gold), {65, 165}, {665, 69});
		L.Place(Canvas, *L.Centered(TEXT("FailureText"), 24), {65, 250}, {665, 109});
		auto* States = L.Add<UWidgetSwitcher>(TEXT("RestartIconStates"));
		for (FSlateBrush Brush : RestartBrushes)
		{
			const FVector2D OriginalSize(Brush.GetImageSize());
			Brush.SetImageSize(OriginalSize * (29.0 / OriginalSize.GetMax()));
			auto* Image = L.Add<UImage>();
			Image->SetBrush(Brush);
			States->AddChild(Image);
		}
		States->SetVisibility(ESlateVisibility::HitTestInvisible);
		auto* Restart = L.Add<UButton>(TEXT("RestartButton"));
		L.WorkshopStyle(*Restart, TEXT("Confirm"), {154, 54}, FMargin(.27f));
		auto* Action = L.Add<UHorizontalBox>();
		Action->AddChildToHorizontalBox(States)->SetVerticalAlignment(VAlign_Center);
		auto* Label = Action->AddChildToHorizontalBox(L.Text(NAME_None, 23, TEXT("重试"), DarkInk));
		Label->SetPadding(FMargin(10, 0, 0, 0));
		Label->SetVerticalAlignment(VAlign_Center);
		Restart->SetContent(Action);
		L.Place(Canvas, *Restart, {289, 391}, {217, 72});
		return true;
	}

	struct FTarget
	{
		const TCHAR* RelativePath;
		void (*Build)(FLayout&);
	};

	const FTarget RewardChoiceTarget{TEXT("Director/WBP_RewardChoice"), BuildRewardChoice};
	const FTarget TutorialRewardTarget{TEXT("Dialogue/WBP_CastleTutorialReward"), BuildTutorialReward};
	const FTarget Targets[] = {
		{TEXT("Items/WBP_WildcardRoomPicker"), BuildWorkshop},
		{TEXT("Common/WBP_ControlSettings"), BuildSettings},
		RewardChoiceTarget,
		TutorialRewardTarget,
		{TEXT("Director/WBP_TowerUpgradeRewards"), BuildTowerReward},
		{TEXT("Dialogue/WBP_CastleDialogue"), nullptr},
		{TEXT("Dialogue/WBP_CastleGuidePopup"), [](FLayout& L) { BuildGuide(L, false); }},
		{TEXT("Dialogue/WBP_CastleGuideBook"), [](FLayout& L) { BuildGuide(L, true); }},
		{TEXT("Common/WBP_BuildingDemolishMenu"), BuildDemolish},
		{TEXT("Items/WBP_BadgeReplaceModal"), BuildBadge},
		{TEXT("Director/WBP_GameOver"), nullptr},
	};

	bool Prepare(UWidgetBlueprint& Asset, const FTarget& Target, const FPalette& Palette)
	{
		TSet<FName> Previous;
		Asset.ForEachSourceWidget([&](UWidget* Widget) { Previous.Add(Widget->GetFName()); });
		if (Target.Build)
		{
			// 保留蓝图对象和原有同名变量的 GUID，重建 UI 树，不写入任何 Graph 逻辑。
			if (!Asset.WidgetTree->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("%s 无法释放旧组件树。"), *Asset.GetPathName());
				return false;
			}
			Asset.WidgetTree = NewObject<UWidgetTree>(&Asset, TEXT("WidgetTree"), RF_Transactional);
			FLayout Layout{*Asset.WidgetTree, Palette};
			Target.Build(Layout);
		}
		else if (!(Asset.GetFName() == TEXT("WBP_CastleDialogue") ? StyleDialogue(Asset, Palette) : StyleGameOver(Asset, Palette)))
		{
			return false;
		}
		Asset.ForEachSourceWidget([&](UWidget* Widget)
		{
			const FName Name = Widget->GetFName();
			Previous.Remove(Name);
			if (!Asset.WidgetVariableNameToGuidMap.Contains(Name)) { Asset.OnVariableAdded(Name); }
		});
		for (const FName Name : Previous) { Asset.OnVariableRemoved(Name); }
		FKismetEditorUtilities::CompileBlueprint(&Asset);
		if (Asset.Status == BS_Error || !Asset.GeneratedClass)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("堡垒弹窗 %s 编译失败。"), *Asset.GetPathName());
			return false;
		}
		return true;
	}

	UWidgetBlueprint* LoadTarget(const FTarget& Target)
	{
		const FString Path = FString(RootPath) + Target.RelativePath;
		auto* Asset = LoadObject<UWidgetBlueprint>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)));
		if (!Asset || !Asset->WidgetTree)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("堡垒弹窗 %s 或其组件树缺失。"), *Path);
			return nullptr;
		}
		return Asset;
	}

	bool Save(UWidgetBlueprint& Asset)
	{
		UPackage* Package = Asset.GetOutermost();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Asset.MarkPackageDirty();
		if (!UPackage::SavePackage(Package, &Asset, *Filename, Args))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("堡垒弹窗 %s 无法保存到 %s。"), *Asset.GetPathName(), *Filename);
			return false;
		}
		UE_LOG(LogMoving_Castle, Display, TEXT("堡垒弹窗已生成：%s"), *Asset.GetPathName());
		return true;
	}

	/** 整批准备和编译通过后才保存，编辑期专项发布与 Commandlet 共用这条路径。 */
	bool Publish(TConstArrayView<FTarget> Batch)
	{
		FPalette Palette;
		if (!Palette.Load()) { return false; }
		TArray<UWidgetBlueprint*> Assets;
		for (const FTarget& Target : Batch)
		{
			UWidgetBlueprint* Asset = LoadTarget(Target);
			if (!Asset) { return false; }
			Assets.Add(Asset);
		}
		for (int32 Index = 0; Index < Assets.Num(); ++Index)
		{
			if (!Prepare(*Assets[Index], Batch[Index], Palette)) { return false; }
		}
		for (UWidgetBlueprint* Asset : Assets)
		{
			if (!Save(*Asset)) { return false; }
		}
		return true;
	}
}

UClass* PublishCastleFortressTutorialPopup(FName Name)
{
	using namespace CastleFortressUI;
	// 只接受教程配置原本持有的面板，严禁误重生成其他 UI。
	const auto* Target = Algo::FindByPredicate(Targets, [Name](const FTarget& Candidate)
	{
		const FStringView Path(Candidate.RelativePath);
		return Path.StartsWith(TEXT("Dialogue/")) && FName(FPackageName::GetShortName(Candidate.RelativePath)) == Name;
	});
	if (!Target)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("无法生成未知的教程弹窗 %s。"), *Name.ToString());
		return nullptr;
	}
	FPalette Palette;
	UWidgetBlueprint* Asset = LoadTarget(*Target);
	if (!Asset || !Palette.Load() || !Prepare(*Asset, *Target, Palette) || !Save(*Asset)) { return nullptr; }
	return Asset->GeneratedClass;
}
#endif

bool UCastleFortressUIPublisherLibrary::PublishGoldRewardWidgets()
{
#if WITH_EDITOR
	using namespace CastleFortressUI;
	const FTarget Batch[] = {RewardChoiceTarget, TutorialRewardTarget};
	return Publish(Batch);
#else
	UE_LOG(LogMoving_Castle, Error, TEXT("金币奖励 WBP 发布须在编辑器中执行。"));
	return false;
#endif
}

int32 UCastleFortressUIPublisherCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	using namespace CastleFortressUI;
	return Publish(Targets) ? 0 : 1;
#else
	return 1;
#endif
}
