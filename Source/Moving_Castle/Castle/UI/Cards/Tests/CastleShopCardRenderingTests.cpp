// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Castle/UI/Cards/CastleCardPortraitSubsystem.h"
#include "Castle/UI/Cards/CastleShopCardWidget.h"
#include "CastleBoard.h"
#include "CastleEconomyComponent.h"
#include "CastlePlayerController.h"
#include "CastleShopComponent.h"
#include "CastleShopWidget.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/WidgetPath.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/Float16Color.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tests/AutomationCommon.h"
#include "TextureResource.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace
{
	/** 正式商店和真实 RHI 的验收：确认不是成功返回了一张空的或不透明的纹理。 */
	class FCastleCheckShopCardPresentation final : public IAutomationLatentCommand
	{
	public:
		explicit FCastleCheckShopCardPresentation(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			UWorld* World = IsValid(GEngine->GameViewport) ? GEngine->GameViewport->GetWorld() : nullptr;
			ACastlePlayerController* Controller = World ? Cast<ACastlePlayerController>(World->GetFirstPlayerController()) : nullptr;
			if (!Test.TestNotNull(TEXT("商店卡片使用 MainMap 的城堡控制器"), Controller))
			{
				return true;
			}
			UCastleShopComponent& Shop = *Controller->GetShop();
			UCastleCardPortraitSubsystem* Studio = World->GetSubsystem<UCastleCardPortraitSubsystem>();
			if (!Test.TestNotNull(TEXT("游戏世界提供摄影服务"), Studio))
			{
				return true;
			}
			if (Phase == 0)
			{
				// 使用正式奖励开店入口，不跳过校验或往界面塞手工替代商品。
				ACastleBoard* Board = Cast<ACastleBoard>(UGameplayStatics::GetActorOfClass(World, ACastleBoard::StaticClass()));
				FText Reason;
				InitialGold = Controller->GetEconomy()->GetGold();
				if (!Test.TestTrue(TEXT("奖励开店事务正常"), Shop.TryOpenForReward(Controller->GetEconomy(), Board, Reason)))
				{
					Test.AddError(Reason.ToString());
					return true;
				}
				Offers = Shop.GetOffers();
				Test.TestEqual(TEXT("新卡面沿用真实商品数"), Offers.Num(), 6);
				Deadline = FPlatformTime::Seconds() + 45.0;
				++Phase;
				return false;
			}
			if (FPlatformTime::Seconds() > Deadline)
			{
				Test.AddError(FString::Printf(TEXT("商店卡片验收未及时完成，阶段 %d。"), Phase));
				Shop.CloseShop();
				return true;
			}
			if (Phase == 1)
			{
				TSet<UCastleCardPortrait*> ReadyPortraits;
				for (const FCastleShopOffer& Offer : Offers)
				{
					FText Reason;
					UCastleCardPortrait* Portrait = Studio->RequestPortrait(Offer.Descriptor, Shop.GetBoundBoard(), Reason);
					if (Portrait == nullptr || Portrait->GetState() == ECastleCardPortraitState::Failed)
					{
						Test.AddError(Portrait ? Portrait->GetFailureReason().ToString() : Reason.ToString());
						Shop.CloseShop();
						return true;
					}
					if (Portrait->GetState() != ECastleCardPortraitState::Ready) { return false; }
					ReadyPortraits.Add(Portrait);
				}
				for (UCastleCardPortrait* Portrait : ReadyPortraits)
				{
					TArray<FFloat16Color> Pixels;
					CheckTransparentPixels(Portrait->GetTexture(), Pixels);
				}
				// 用正式卡的 Slate 路径送入局部鼠标坐标，确认悬停事件确实驱动渲染而不是仅有接口。
				TArray<UUserWidget*> Widgets;
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, UCastleShopCardWidget::StaticClass(), false);
				if (!Test.TestEqual(TEXT("实际 UMG 商品 Widget 数"), Widgets.Num(), 6)) { return true; }
				Card = CastChecked<UCastleShopCardWidget>(Widgets[0]);
				const FGeometry Geometry = Card->GetCachedGeometry();
				if (!Test.TestTrue(TEXT("卡牌已在商店布局"), Geometry.GetLocalSize().X > 0.0)) { return true; }
				UImage* CardFace = Cast<UImage>(Card->WidgetTree->FindWidget(TEXT("CardFace")));
				UMaterialInstanceDynamic* Material = CardFace ? Cast<UMaterialInstanceDynamic>(CardFace->GetBrush().GetResourceObject()) : nullptr;
				if (!Test.TestNotNull(TEXT("可见卡面绑定真实 UI 合成材质"), Material)) { return true; }
				UTexture* BoundTexture = nullptr;
				FLinearColor State;
				if (!Test.TestTrue(TEXT("普通卡可见且已接入静止建筑层"), Material->GetVectorParameterValue(
					FMaterialParameterInfo(TEXT("CardState")), State) && State.B == 0.0f && State.A == 1.0f)
					|| !Test.TestTrue(TEXT("读出普通卡静止纹理"), Material->GetTextureParameterValue(
						FMaterialParameterInfo(TEXT("Portrait")), BoundTexture))) { return true; }
				Face = Material;
				Still = Cast<UTextureRenderTarget2D>(BoundTexture);
				if (!CheckTransparentPixels(Still.Get(), StillPixels)) { return true; }
				if (FParse::Param(FCommandLine::Get(), TEXT("CastleCardScreenshots")))
				{
					FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("CardFace/ShopNormal.png"), true, false);
				}
				++Phase;
				return false;
			}
			if (Phase == 2)
			{
				if (FScreenshotRequest::IsScreenshotRequested()) { return false; }
				TArray<UUserWidget*> Shops;
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Shops, UCastleShopWidget::StaticClass());
				if (!Test.TestEqual(TEXT("正式商店 Widget 唯一"), Shops.Num(), 1)) { return true; }
				UButton* Toggle = Cast<UButton>(Shops[0]->WidgetTree->FindWidget(TEXT("FoilPreview")));
				if (!Test.TestNotNull(TEXT("有实际可点击的箔光对照按钮"), Toggle)) { return true; }
				Toggle->OnClicked.Broadcast();
				const FGeometry Geometry = Card->GetCachedGeometry();
				const FVector2D Cursor = Geometry.LocalToAbsolute(FVector2D(Geometry.GetLocalSize()) * FVector2D(0.94, 0.38));
				const FPointerEvent Pointer(0u, 0u, Cursor, Cursor, TSet<FKey>(), EKeys::Invalid, 0.0f, FModifierKeysState());
				const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow();
				if (!Test.TestTrue(TEXT("游戏视口有可进行命中测试的 Slate 窗口"), Window.IsValid())) { return true; }
				FSlateApplication& Slate = FSlateApplication::Get();
				const FWidgetPath Hit = Slate.LocateWindowUnderMouse(Cursor, {Window.ToSharedRef()}, false, 0);
				if (!Test.TestTrue(TEXT("真实鼠标命中商品，而不是被卡框或面板吞掉"), Hit.ContainsWidget(Card->GetCachedWidget().Get())))
				{
					return true;
				}
				Slate.RoutePointerMoveEvent(Hit, Pointer, false);
				++Phase;
				return false;
			}
			if (Phase == 3)
			{
				UTextureRenderTarget2D* Hover = Studio->GetHoverTexture(Card.Get());
				if (Hover == nullptr) { return false; }
				TArray<FFloat16Color> HoverPixels;
				if (!CheckTransparentPixels(Hover, HoverPixels)) { return true; }
				Test.TestTrue(TEXT("悬停不会覆盖普通卡的静止纹理"), Hover != Still.Get());
				// 同一台相机确实改变了建筑轮廓；仅仅写入不同材质参数或换目标纹理不能通过此断言。
				int32 ChangedCoverage = 0;
				for (int32 Index = 0; Index < HoverPixels.Num(); ++Index)
				{
					if (FMath::Abs(HoverPixels[Index].A.GetFloat() - StillPixels[Index].A.GetFloat()) > 0.45f) { ++ChangedCoverage; }
				}
				Test.TestTrue(TEXT("鼠标倾角确实转动了建筑相机"), ChangedCoverage > HoverPixels.Num() / 250);
				FLinearColor State;
				Test.TestTrue(TEXT("同一张牌的材质开启箔光并收到倾角"), Face.IsValid() && Face->GetVectorParameterValue(
					FMaterialParameterInfo(TEXT("CardState")), State) && State.B == 1.0f && State.R > 0.8f);
				const TArray<FCastleShopOffer> After = Shop.GetOffers();
				Test.TestEqual(TEXT("箔光不扣金币"), Controller->GetEconomy()->GetGold(), InitialGold);
				Test.TestEqual(TEXT("箔光不改变商品槽数"), After.Num(), Offers.Num());
				for (int32 Index = 0; Index < After.Num() && Index < Offers.Num(); ++Index)
				{
					const FCastleShopOffer& Item = After[Index];
					const FCastleShopOffer& Original = Offers[Index];
					Test.TestTrue(TEXT("箔光保留色号、建筑、价格与售罄状态"), Item.Descriptor.Definition == Original.Descriptor.Definition
						&& Item.Descriptor.Color == Original.Descriptor.Color && Item.Descriptor.Number == Original.Descriptor.Number
						&& Item.Price == Original.Price && Item.bSold == Original.bSold);
				}
				if (FParse::Param(FCommandLine::Get(), TEXT("CastleCardScreenshots")))
				{
					FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("CardFace/ShopFoil.png"), true, false);
				}
				++Phase;
				return false;
			}
			if (FScreenshotRequest::IsScreenshotRequested()) { return false; }
			Shop.CloseShop();
			Test.TestNull(TEXT("关闭商店解除悬停画面"), Studio->GetHoverTexture(Card.Get()));
			Test.TestFalse(TEXT("商店关闭不再驱动摄影"), Studio->IsTickable());
			return true;
		}

	private:
		bool CheckTransparentPixels(UTextureRenderTarget2D* Target, TArray<FFloat16Color>& Pixels)
		{
			if (!Test.TestNotNull(TEXT("有建筑成像纹理"), Target)
				|| !Test.TestTrue(TEXT("从真实 RHI 读回线性 RGBA 像素"), Target->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(Pixels))
				|| !Test.TestEqual(TEXT("读回图像大小正确"), Pixels.Num(), Target->SizeX * Target->SizeY))
			{
				return false;
			}
			int32 LitOpaque = 0;
			int32 Clear = 0;
			for (const FFloat16Color& Pixel : Pixels)
			{
				if (Pixel.A.GetFloat() < 0.05f && FMath::Max3(Pixel.R.GetFloat(), Pixel.G.GetFloat(), Pixel.B.GetFloat()) > 0.025f) { ++LitOpaque; }
				if (Pixel.A.GetFloat() > 0.95f) { ++Clear; }
			}
			Test.TestTrue(TEXT("存在有实际色彩的不透明建筑像素"), LitOpaque > Pixels.Num() / 100);
			Test.TestTrue(TEXT("大部分画面含纯透明背景，四角不残留天空"), Clear > Pixels.Num() / 2
				&& Pixels[0].A.GetFloat() > 0.95f && Pixels[Target->SizeX - 1].A.GetFloat() > 0.95f
				&& Pixels[Pixels.Num() - Target->SizeX].A.GetFloat() > 0.95f && Pixels.Last().A.GetFloat() > 0.95f);
			return true;
		}

		FAutomationTestBase& Test;
		TArray<FCastleShopOffer> Offers;
		TWeakObjectPtr<UCastleShopCardWidget> Card;
		TWeakObjectPtr<UMaterialInstanceDynamic> Face;
		TWeakObjectPtr<UTextureRenderTarget2D> Still;
		TArray<FFloat16Color> StillPixels;
		int32 InitialGold = 0;
		int32 Phase = 0;
		double Deadline = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleShopCardPresentationTest, "MovingCastle.Shop.CardPresentation",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FCastleShopCardPresentationTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("此测试必须由真实 RHI 执行，不能使用 NullRHI"), FApp::CanEverRender())
		|| !AutomationOpenMap(TEXT("/Game/MovingCastle/Maps/MainMap"), true))
	{
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FCastleCheckShopCardPresentation(*this));
	return true;
}

#endif
