#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetTree.h"
#include "Castle/Dialogue/CastleDialogueWidget.h"
#include "Castle/Guide/CastleGuideWidgets.h"
#include "Castle/Tutorial/CastleTutorialWidgets.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "WidgetBlueprint.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleFortressPopupBindingsTest, "MovingCastle.UI.FortressKit.BindingsAndTextures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleFortressPopupBindingsTest::RunTest(const FString&)
{
	// 对磁盘上的真实蓝图实例检查 BindWidget 和图像画刷，避免只验证生成器的模型。
	const TCHAR* const Targets[] = {
		TEXT("Items/WBP_WildcardRoomPicker"), TEXT("Common/WBP_ControlSettings"),
		TEXT("Director/WBP_RewardChoice"), TEXT("Dialogue/WBP_CastleTutorialReward"), TEXT("Director/WBP_TowerUpgradeRewards"),
		TEXT("Dialogue/WBP_CastleDialogue"), TEXT("Dialogue/WBP_CastleGuidePopup"), TEXT("Dialogue/WBP_CastleGuideBook"),
		TEXT("Common/WBP_BuildingDemolishMenu"), TEXT("Items/WBP_BadgeReplaceModal"), TEXT("Director/WBP_GameOver")
	};
	UGameInstance* Game = NewObject<UGameInstance>(GEngine);
	Game->InitializeStandalone();
	UWorld* World = Game->GetWorld();
	ON_SCOPE_EXIT
	{
		Game->Shutdown();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	TSet<const UTexture2D*> KitTextures;
	for (const TCHAR* Target : Targets)
	{
		const FString Package = FString(TEXT("/Game/MovingCastle/UI/")) + Target;
		const FString Object = Package + TEXT(".") + FPackageName::GetShortName(Package);
		auto* Asset = LoadObject<UWidgetBlueprint>(nullptr, *Object);
		if (!TestNotNull(Target, Asset)) { return false; }
		if (!TestTrue(Object + TEXT(" 蓝图已编译"), Asset->Status != BS_Error && IsValid(Asset->GeneratedClass))) { return false; }
		auto* Widget = CreateWidget<UUserWidget>(Game, Asset->GeneratedClass.Get());
		if (!TestNotNull(Object + TEXT(" 可实例化"), Widget)) { return false; }

		for (TFieldIterator<FObjectPropertyBase> It(Asset->ParentClass); It; ++It)
		{
			const FObjectPropertyBase* Property = *It;
			if (!Property->HasMetaData(TEXT("BindWidget")) && !Property->HasMetaData(TEXT("BindWidgetOptional"))) { continue; }
			const UWidget* Control = Widget->GetWidgetFromName(Property->GetFName());
			TestTrue(FString::Printf(TEXT("%s 原生绑定 %s"), Target, *Property->GetName()),
				Control && Control->IsA(Property->PropertyClass) && Property->GetObjectPropertyValue_InContainer(Widget) == Control);
		}

		int32 BrushCount = 0;
		const UWidget* GoldButton = Widget->GetWidgetFromName(TEXT("GoldButton"));
		bool bGoldRewardTexture = false;
		bool bGoldRewardLabel = false;
		const auto VerifyBrush = [&](const UWidget& Owner, const FSlateBrush& Brush)
		{
			const auto* Texture = Cast<UTexture2D>(Brush.GetResourceObject());
			if (!Texture || !Texture->GetPathName().StartsWith(TEXT("/Game/MovingCastle/UI/FortressKit/"))) { return; }
			++BrushCount;
			KitTextures.Add(Texture);
			TestTrue(FString::Printf(TEXT("%s/%s 的切图 %s 有效"), Target, *Owner.GetName(), *Texture->GetName()),
				Texture->Source.GetSizeX() > 0 && Texture->Source.GetSizeY() > 0 && Brush.GetImageSize().X > 0
				&& Brush.GetImageSize().Y > 0 && Texture->LODGroup == TEXTUREGROUP_UI
				&& Texture->SRGB && Texture->NeverStream && Texture->MipGenSettings == TMGS_NoMipmaps);
		};
		Asset->WidgetTree->ForEachWidget([&](UWidget* Source)
		{
			if (const auto* Image = Cast<UImage>(Source)) { VerifyBrush(*Image, Image->GetBrush()); }
			if (const auto* Border = Cast<UBorder>(Source)) { VerifyBrush(*Border, Border->Background); }
			if (const auto* Button = Cast<UButton>(Source))
			{
				const FButtonStyle& Style = Button->GetStyle();
				VerifyBrush(*Button, Style.Normal);
				VerifyBrush(*Button, Style.Hovered);
				VerifyBrush(*Button, Style.Pressed);
				VerifyBrush(*Button, Style.Disabled);
			}
			// 两个奖励面板的金币资源要位于首项按钮内，不能由其他面板的贴图引用凑数。
			if (GoldButton)
			{
				for (const UWidget* Parent = Source->GetParent(); Parent; Parent = Parent->GetParent())
				{
					if (Parent->GetFName() != GoldButton->GetFName()) { continue; }
					if (const UImage* Icon = Cast<UImage>(Source))
					{
						bGoldRewardTexture |= GetPathNameSafe(Icon->GetBrush().GetResourceObject()).EndsWith(TEXT("/Resources/T_GoldCoins.T_GoldCoins"));
					}
					if (const UTextBlock* Label = Cast<UTextBlock>(Source))
					{
						bGoldRewardLabel |= Label->GetText().ToString() == TEXT("金币 +60");
					}
					break;
				}
			}
		});
		TestTrue(Object + TEXT(" 实际引用堡垒贴图"), BrushCount > 0);

		if (const auto* Reward = Cast<UCastleRewardChoiceWidget>(Widget))
		{
			TestTrue(TEXT("战后奖励具有回血图标和商店双态贴图"), Reward->HasRequiredBindings());
			TestTrue(TEXT("战后首项显示金币图标和基础金额"), bGoldRewardTexture && bGoldRewardLabel);
		}
		if (const auto* GameOver = Cast<UCastleGameOverWidget>(Widget))
		{
			TestTrue(TEXT("游戏结算具有骷髅图标和重试双态贴图"), GameOver->HasRequiredBindings());
		}
		if (const auto* Dialogue = Cast<UCastleDialogueWidget>(Widget))
		{
			TestTrue(TEXT("对话具有分支样式与双立绘槽"), Dialogue->HasRequiredBindings());
		}
		if (const auto* Guide = Cast<UCastleGuideBookWidget>(Widget))
		{
			TestTrue(TEXT("书册具有内容、翻页和关闭绑定"), Guide->HasRequiredBindings());
		}
		if (const auto* Popup = Cast<UCastleGuidePopupWidget>(Widget))
		{
			TestTrue(TEXT("图文弹窗具有内容和关闭绑定"), Popup->HasRequiredBindings());
		}
		if (const auto* Tutorial = Cast<UCastleTutorialRewardWidget>(Widget))
		{
			TestTrue(TEXT("教程选项和卡牌绑定存在"), Tutorial->HasRequiredBindings());
			TestTrue(TEXT("教程首项显示金币图标和基础金额"), bGoldRewardTexture && bGoldRewardLabel);
		}
		if (const auto* Replacements = Cast<UPanelWidget>(Widget->GetWidgetFromName(TEXT("ReplaceSlots"))))
		{
			TestEqual(TEXT("替换布局包含六个直接子按钮"), Replacements->GetChildrenCount(), 6);
			for (UWidget* Child : Replacements->GetAllChildren())
			{
				const auto* Button = Cast<UButton>(Child);
				TestTrue(TEXT("徽章替换按钮直接持有文本"), Button && Cast<UTextBlock>(Button->GetContent()));
			}
		}
	}
	TestEqual(TEXT("47 张导入纹理均被目标 WBP 引用"), KitTextures.Num(), 47);
	return true;
}
#endif
