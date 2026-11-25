#include "CastleTutorialWidgetAuthoring.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Castle/Dialogue/CastleDialogueWidget.h"
#include "Castle/Guide/CastleGuideWidgets.h"
#include "Castle/Tutorial/CastleTutorialConfigDataAsset.h"
#include "Castle/Tutorial/CastleTutorialWidgets.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Moving_Castle.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"

namespace
{
	const FLinearColor PanelColor(0.047f, 0.075f, 0.089f, 0.98f);
	const FLinearColor Ink(0.95f, 0.95f, 0.89f);
	const FLinearColor Accent(0.96f, 0.75f, 0.40f);

	/** 用项目已有中文字体构建所有文字，绑定名和运行时成员一致。 */
	struct FLayout
	{
		UWidgetTree& Tree;
		UFont& Font;

		template<typename T>
		T* Add(const FName Name = NAME_None)
		{
			return Tree.ConstructWidget<T>(T::StaticClass(), Name);
		}

		UTextBlock* Text(const FName Name, int32 Size, const TCHAR* Label = TEXT(""), FLinearColor Color = Ink)
		{
			auto* Widget = Add<UTextBlock>(Name);
			Widget->SetFont(FSlateFontInfo(&Font, Size));
			Widget->SetText(FText::FromString(Label));
			Widget->SetColorAndOpacity(FSlateColor(Color));
			Widget->SetAutoWrapText(true);
			Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
			return Widget;
		}

		UBorder* Panel(UWidget& Child, FMargin Padding)
		{
			auto* Border = Add<UBorder>();
			Border->SetBrushColor(PanelColor);
			Border->SetPadding(Padding);
			Border->SetContent(&Child);
			return Border;
		}

		UButton* Button(FName Name, const TCHAR* Label)
		{
			auto* Widget = Add<UButton>(Name);
			Widget->SetBackgroundColor(FLinearColor(0.13f, 0.26f, 0.28f));
			Widget->SetContent(Text(NAME_None, 19, Label));
			return Widget;
		}

		/** 视口锚点负责响应窗口变化；内容由各面板决定所需高度。 */
		void Anchor(UCanvasPanel& Canvas, UWidget& Child, FAnchors Anchors, FMargin Offsets, FVector2D Alignment = FVector2D::ZeroVector, bool bAutoSize = false)
		{
			auto* Slot = Canvas.AddChildToCanvas(&Child);
			Slot->SetAnchors(Anchors);
			Slot->SetOffsets(Offsets);
			Slot->SetAlignment(Alignment);
			Slot->SetAutoSize(bAutoSize);
		}

		void Portrait(UCanvasPanel& Canvas, FName Name, FName SlotName, const FAnchors& Anchors, EHorizontalAlignment Side)
		{
			// 在锚定范围内缩放正方形，等比贴合面板上沿和靠外一侧。
			auto* Scale = Add<UScaleBox>(SlotName);
			Scale->SetStretch(EStretch::ScaleToFit);
			Scale->SetVisibility(ESlateVisibility::HitTestInvisible);
			auto* Square = Add<USizeBox>();
			Square->SetWidthOverride(380);
			Square->SetHeightOverride(380);
			auto* Image = Add<UImage>(Name);
			Image->SetVisibility(ESlateVisibility::HitTestInvisible);
			Square->SetContent(Image);
			auto* PortraitSlot = CastChecked<UScaleBoxSlot>(Scale->AddChild(Square));
			PortraitSlot->SetHorizontalAlignment(Side);
			PortraitSlot->SetVerticalAlignment(VAlign_Bottom);
			Anchor(Canvas, *Scale, Anchors, FMargin(0));
		}
	};

	/** 通过原生 Blueprint 编译器检查 BindWidget，失败的布局不写磁盘。 */
	UClass* PublishWidget(const TCHAR* Name, UClass& Parent, UFont& Font, TFunctionRef<void(FLayout&)> Build)
	{
		const FString Path = FString(TEXT("/Game/MovingCastle/UI/Dialogue/")) + Name;
		auto* Package = CreatePackage(*Path);
		auto* Asset = LoadObject<UWidgetBlueprint>(nullptr, *(Path + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		TSet<FName> PreviousWidgets;
		if (!Asset)
		{
			Asset = CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(&Parent, Package, FName(Name),
				BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
			FAssetRegistryModule::AssetCreated(Asset);
		}
		else
		{
			// 重新生成时让编译器使用新的 WidgetTree，不在旧树中积累同名绑定控件。
			Asset->ForEachSourceWidget([&](UWidget* Widget) { PreviousWidgets.Add(Widget->GetFName()); });
			Asset->WidgetTree->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Asset->WidgetTree = NewObject<UWidgetTree>(Asset, TEXT("WidgetTree"), RF_Transactional);
		}
		FLayout Layout{*Asset->WidgetTree, Font};
		Build(Layout);
		// 与 UMG 设计器的增删操作一致：保留原有绑定名 GUID，只登记新增名字并删除已移除的名字。
		Asset->ForEachSourceWidget([&](UWidget* Widget)
		{
			const FName WidgetName = Widget->GetFName();
			PreviousWidgets.Remove(WidgetName);
			if (!Asset->WidgetVariableNameToGuidMap.Contains(WidgetName)) { Asset->OnVariableAdded(WidgetName); }
		});
		for (const FName OldName : PreviousWidgets) { Asset->OnVariableRemoved(OldName); }
		FKismetEditorUtilities::CompileBlueprint(Asset);
		if (Asset->Status == BS_Error)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("教程布局 %s 编译失败。"), *Path);
			return nullptr;
		}
		Asset->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, Asset, *Filename, Args))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("教程布局 %s 保存失败，目标 %s。"), *Path, *Filename);
			return nullptr;
		}
		return Asset->GeneratedClass;
	}

	UFont* LoadTutorialLayoutFont()
	{
		auto* Font = LoadObject<UFont>(nullptr, TEXT("/Game/MovingCastle/Fonts/MicrosoftYaHei_Font.MicrosoftYaHei_Font"));
		if (!Font) { UE_LOG(LogMoving_Castle, Error, TEXT("教程布局缺少 MicrosoftYaHei_Font。")); }
		return Font;
	}

	UClass* PublishDialogueWidget(UFont& Font)
	{
		return PublishWidget(TEXT("WBP_CastleDialogue"), *UCastleDialogueWidget::StaticClass(), Font, [](FLayout& L)
		{
			auto* Root = L.Add<UCanvasPanel>();
			L.Tree.RootWidget = Root;
			L.Portrait(*Root, TEXT("LeftPortrait"), TEXT("LeftPortraitSlot"), FAnchors(.08f, .35f, .30f, .72f), HAlign_Left);
			L.Portrait(*Root, TEXT("RightPortrait"), TEXT("RightPortraitSlot"), FAnchors(.70f, .35f, .92f, .72f), HAlign_Right);

			auto* Overlay = L.Add<UOverlay>(TEXT("DialoguePanel"));
			auto* Lines = L.Add<UVerticalBox>();
			auto* Speaker = L.Text(TEXT("SpeakerName"), 24, TEXT(""), Accent);
			Speaker->SetAutoWrapText(false);
			Lines->AddChildToVerticalBox(Speaker)->SetPadding(FMargin(0, 0, 0, 4));
			Lines->AddChildToVerticalBox(L.Text(TEXT("DialogueBody"), 22))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			auto* Hint = L.Text(TEXT("ContinueHint"), 16, TEXT("点击继续"), Accent);
			Hint->SetAutoWrapText(false);
			Lines->AddChildToVerticalBox(Hint)->SetHorizontalAlignment(HAlign_Right);
			auto* PanelSlot = Overlay->AddChildToOverlay(L.Panel(*Lines, FMargin(28, 10)));
			PanelSlot->SetHorizontalAlignment(HAlign_Fill);
			PanelSlot->SetVerticalAlignment(VAlign_Fill);

			// 点击区是底板的兄弟节点，UButtonSlot 的默认居中不再限制正文宽度。
			auto* Continue = L.Add<UButton>(TEXT("ContinueButton"));
			const auto* Focusable = FindFProperty<FBoolProperty>(UButton::StaticClass(), TEXT("IsFocusable"));
			check(Focusable);
			Focusable->SetPropertyValue_InContainer(Continue, false);
			Continue->SetBackgroundColor(FLinearColor::Transparent);
			auto* ClickSlot = Overlay->AddChildToOverlay(Continue);
			ClickSlot->SetHorizontalAlignment(HAlign_Fill);
			ClickSlot->SetVerticalAlignment(VAlign_Fill);
			L.Anchor(*Root, *Overlay, FAnchors(.08f, .72f, .92f, .94f), FMargin(0));
			auto* Choices = L.Add<UVerticalBox>(TEXT("ChoiceContainer"));
			L.Anchor(*Root, *Choices, FAnchors(.31f, .44f, .69f, .70f), FMargin(0));
		});
	}
}

bool BuildCastleDialogueWidget()
{
	UFont* Font = LoadTutorialLayoutFont();
	return Font && PublishDialogueWidget(*Font);
}

bool BuildCastleTutorialWidgets(UCastleTutorialConfigDataAsset& Config)
{
	UFont* Font = LoadTutorialLayoutFont();
	if (!Font) { return false; }
	UClass* Dialogue = PublishDialogueWidget(*Font);

	// 百科和首次说明共用版面，只有百科具备独立翻页和空状态绑定。
	const auto Guide = [&](bool bBook) -> UClass*
	{
		UClass* Parent = bBook ? UCastleGuideBookWidget::StaticClass() : UCastleGuidePopupWidget::StaticClass();
		return PublishWidget(bBook ? TEXT("WBP_CastleGuideBook") : TEXT("WBP_CastleGuidePopup"), *Parent, *Font,
			[bBook](FLayout& L)
		{
			auto* Root = L.Add<UCanvasPanel>();
			L.Tree.RootWidget = Root;
			auto* Lines = L.Add<UVerticalBox>();
			Lines->AddChildToVerticalBox(L.Text(TEXT("EntryTitle"), 30, TEXT(""), Accent));
			auto* Scale = L.Add<UScaleBox>();
			Scale->SetStretch(EStretch::ScaleToFit);
			Scale->SetContent(L.Add<UImage>(TEXT("Illustration")));
			Lines->AddChildToVerticalBox(Scale);
			Lines->AddChildToVerticalBox(L.Text(TEXT("EntryDescription"), 22))->SetPadding(FMargin(0, 20, 0, 20));
			if (bBook)
			{
				Lines->AddChildToVerticalBox(L.Text(TEXT("EmptyMessage"), 21, TEXT("尚未收录词条")));
			}
			auto* Buttons = L.Add<UHorizontalBox>();
			Lines->AddChildToVerticalBox(Buttons);
			if (bBook)
			{
				Buttons->AddChildToHorizontalBox(L.Button(TEXT("PreviousButton"), TEXT("上一条")));
				Buttons->AddChildToHorizontalBox(L.Text(TEXT("PageStatus"), 19))->SetPadding(FMargin(15, 0));
				Buttons->AddChildToHorizontalBox(L.Button(TEXT("NextButton"), TEXT("下一条")));
			}
			Buttons->AddChildToHorizontalBox(L.Button(TEXT("CloseButton"), TEXT("关闭")))->SetPadding(FMargin(14, 0, 0, 0));
			L.Anchor(*Root, *L.Panel(*Lines, FMargin(35)), FAnchors(.22f, .27f, .78f, .77f), FMargin(0));
		});
	};
	auto* Popup = Guide(false);
	auto* Book = Guide(true);
	Config.ObjectiveClass = PublishWidget(TEXT("WBP_CastleTutorialObjective"), *UCastleTutorialTextWidget::StaticClass(), *Font, [](FLayout& L)
	{
		auto* Root = L.Add<UCanvasPanel>();
		L.Tree.RootWidget = Root;
		auto* Text = L.Text(TEXT("TutorialText"), 20);
		Text->SetJustification(ETextJustify::Center);
		L.Anchor(*Root, *L.Panel(*Text, FMargin(24, 12)), FAnchors(.24f, .05f, .76f, .12f), FMargin(0));
	});
	Config.BubbleClass = PublishWidget(TEXT("WBP_CastleTutorialBubble"), *UCastleTutorialTextWidget::StaticClass(), *Font, [](FLayout& L)
	{
		auto* Text = L.Text(TEXT("TutorialText"), 19);
		Text->SetJustification(ETextJustify::Center);
		L.Tree.RootWidget = L.Panel(*Text, FMargin(8, 5));
	});
	Config.RewardClass = PublishWidget(TEXT("WBP_CastleTutorialReward"), *UCastleTutorialRewardWidget::StaticClass(), *Font, [](FLayout& L)
	{
		auto* Root = L.Add<UCanvasPanel>();
		L.Tree.RootWidget = Root;
		auto* Content = L.Add<UVerticalBox>();
		Content->AddChildToVerticalBox(L.Text(NAME_None, 28, TEXT("战斗奖励"), Accent))->SetPadding(FMargin(0, 0, 0, 15));
		auto* Choices = L.Add<UVerticalBox>(TEXT("Choices"));
		Content->AddChildToVerticalBox(Choices);
		Choices->AddChildToVerticalBox(L.Button(TEXT("ExperienceButton"), TEXT("主塔经验")));
		Choices->AddChildToVerticalBox(L.Button(TEXT("HealingButton"), TEXT("主塔回血")))->SetPadding(FMargin(0, 8));
		Choices->AddChildToVerticalBox(L.Button(TEXT("CopyButton"), TEXT("获得复制的随机功能房间")));
		Content->AddChildToVerticalBox(L.Add<UButton>(TEXT("CardButton")));
		L.Anchor(*Root, *L.Panel(*Content, FMargin(24)), FAnchors(.33f, .18f, .67f, .69f), FMargin(0));
	});
	return Dialogue && Popup && Book && Config.ObjectiveClass && Config.BubbleClass && Config.RewardClass;
}
#endif
