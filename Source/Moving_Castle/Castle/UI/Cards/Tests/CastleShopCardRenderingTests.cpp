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
#include "Framework/Application/SlateUser.h"
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
	/** 使用正式商店的 Slate 输入路由和真实 RHI，检查建筑 Alpha 以及整张牌的实际屏幕投影。 */
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
				// 只从当前视口商店实际挂着的 WidgetTree 取商品，不把刷新后等待 GC 的旧卡计进测试。
				TArray<UUserWidget*> Shops;
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Shops, UCastleShopWidget::StaticClass(), true);
				if (!Test.TestEqual(TEXT("视口有一个正式商店"), Shops.Num(), 1)) { return true; }
				TArray<UCastleShopCardWidget*> Widgets;
				Shops[0]->WidgetTree->ForEachWidget([&Widgets](UWidget* Widget)
				{
					if (UCastleShopCardWidget* OfferCard = Cast<UCastleShopCardWidget>(Widget)) { Widgets.Add(OfferCard); }
				});
				if (!Test.TestEqual(TEXT("实际 UMG 商品 Widget 数"), Widgets.Num(), 6)) { return true; }
				Card = Widgets[0];
				OtherCard = Widgets[1];
				FoilToggle = Cast<UButton>(Shops[0]->WidgetTree->FindWidget(TEXT("FoilPreview")));
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
				const URetainerBox* Retainer = Cast<URetainerBox>(Card->WidgetTree->FindWidget(TEXT("CardProjection")));
				Projection = Retainer != nullptr ? Retainer->GetEffectMaterial() : nullptr;
				if (!Test.TestNotNull(TEXT("整张牌实际经过 Retainer 的透视材质"), Projection.Get())) { return true; }
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
				if (!Test.TestNotNull(TEXT("有实际可点击的箔光对照按钮"), FoilToggle.Get())) { return true; }
				FoilToggle->OnClicked.Broadcast();
				Start = CardScreenPoint(*Card.Get(), FVector2D(0.26, 0.45));
				Outside = CardScreenPoint(*Card.Get(), FVector2D(1.10, 0.45));
				const FWidgetPath Hit = HitAt(Start);
				if (!Test.TestTrue(TEXT("真实鼠标命中稳定卡位"), Hit.ContainsWidget(Card->GetCachedWidget().Get())))
				{
					return true;
				}
				FSlateApplication::Get().RoutePointerMoveEvent(Hit, MakePointer(Start, Outside), false);
				++Phase;
				return false;
			}
			if (Phase == 3)
			{
				Test.TestTrue(TEXT("仅悬停不会倾斜整牌，也不移动箔光"), IsCardNeutral());
				Test.TestFalse(TEXT("仅悬停不会调度动态摄影"), Studio->IsTickable());
				if (!ReadCardSpans(FlatFarSpan, FlatNearSpan)) { return true; }
				Test.TestTrue(FString::Printf(TEXT("平放时卡面两侧等高（左 %d、右 %d）"), FlatFarSpan, FlatNearSpan),
					FlatFarSpan > 0 && FlatNearSpan > 0
					&& FMath::Abs(FlatFarSpan - FlatNearSpan) <= 3);
				FSlateApplication& Slate = FSlateApplication::Get();
				if (!Test.TestTrue(TEXT("按下牌面获得鼠标捕获"), PressCard(Start))) { Shop.CloseShop(); return true; }
				Test.TestTrue(TEXT("只按下不拖动，仍使用正面"), IsCardNeutral());
				Slate.RoutePointerUpEvent(HitAt(Start), MakePointer(Start, Start, false, EKeys::LeftMouseButton));
				Test.TestTrue(TEXT("普通点击没有改变牌姿态或留下鼠标捕获"), IsCardNeutral() && !Card->HasMouseCapture());
				if (!Test.TestTrue(TEXT("再次按下开始拖动"), PressCard(Start))) { Shop.CloseShop(); return true; }
				Slate.RoutePointerMoveEvent(HitAt(Outside), MakePointer(Outside, Start, true), false);
				Test.TestTrue(TEXT("越过卡位边界仍由抓住的卡持有鼠标"), Card->HasMouseCapture());
				++Phase;
				return false;
			}
			if (Phase == 4)
			{
				UTextureRenderTarget2D* Dynamic = Studio->GetDynamicTexture(Card.Get(), &FirstCaptureNumber);
				UTexture* BoundTexture = nullptr;
				if (Dynamic == nullptr || !Face->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Portrait")), BoundTexture)
					|| Dynamic != BoundTexture) { return false; }
				TArray<FFloat16Color> DynamicPixels;
				if (!CheckTransparentPixels(Dynamic, DynamicPixels)) { return true; }
				Test.TestTrue(TEXT("拖动不会覆盖普通卡的静止纹理"), Dynamic != Still.Get());
				Test.TestNull(TEXT("其他商品没有获得动态建筑图"), Studio->GetDynamicTexture(OtherCard.Get()));
				// 同一台相机确实改变了建筑轮廓；仅仅写入不同材质参数或换目标纹理不能通过此断言。
				int32 ChangedCoverage = 0;
				for (int32 Index = 0; Index < DynamicPixels.Num(); ++Index)
				{
					if (FMath::Abs(DynamicPixels[Index].A.GetFloat() - StillPixels[Index].A.GetFloat()) > 0.45f) { ++ChangedCoverage; }
				}
				Test.TestTrue(TEXT("拖动倾角确实转动了建筑相机"), ChangedCoverage > DynamicPixels.Num() / 250);
				FLinearColor State, AxisX;
				Test.TestTrue(TEXT("整牌与箔光都响应同一方向的拖动"), Face->GetVectorParameterValue(
					FMaterialParameterInfo(TEXT("CardState")), State) && State.B == 1.0f && State.R > 0.8f
					&& Projection->GetVectorParameterValue(FMaterialParameterInfo(TEXT("ProjectionX")), AxisX) && AxisX.B < -0.2f);
				int32 FarSpan = 0, NearSpan = 0;
				if (!ReadCardSpans(FarSpan, NearSpan)) { return true; }
				Test.TestTrue(FString::Printf(TEXT("整牌的屏幕像素近边高、远边低（远 %d、近 %d）"), FarSpan, NearSpan),
					FarSpan > 0 && NearSpan - FarSpan > FlatNearSpan / 25);
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
					FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("CardFace/ShopFoilTilt.png"), true, false);
				}
				++Phase;
				return false;
			}
			if (Phase == 5)
			{
				if (FScreenshotRequest::IsScreenshotRequested()) { return false; }
				// 在同一个动态纹理里拍下一角度，覆盖“纹理指针不变但内容已变”的更新路径。
				SecondPoint = CardScreenPoint(*Card.Get(), FVector2D(0.61, 0.78));
				FSlateApplication::Get().RoutePointerMoveEvent(HitAt(SecondPoint), MakePointer(SecondPoint, Outside, true), false);
				++Phase;
				return false;
			}
			if (Phase == 6)
			{
				uint64 CaptureNumber = 0;
				Studio->GetDynamicTexture(Card.Get(), &CaptureNumber);
				if (CaptureNumber <= FirstCaptureNumber) { return false; }
				FSlateApplication::Get().RoutePointerUpEvent(HitAt(SecondPoint), MakePointer(SecondPoint, SecondPoint, false, EKeys::LeftMouseButton));
				Test.TestTrue(TEXT("松手立即归还鼠标，保留当前角度开始平滑回正"), !Card->HasMouseCapture() && !IsCardNeutral());
				++Phase;
				return false;
			}
			if (!IsCardNeutral()) { return false; }
			Test.TestNull(TEXT("回正后解除动态画面"), Studio->GetDynamicTexture(Card.Get()));
			Test.TestFalse(TEXT("静止时不再驱动摄影"), Studio->IsTickable());

			// 模拟窗口失去捕获，再在按住时关闭商店，两条路径都不得等一次鼠标移出。
			FSlateApplication& Slate = FSlateApplication::Get();
			Test.TestTrue(TEXT("可再次抓牌"), PressCard(Start));
			Slate.RoutePointerMoveEvent(HitAt(Outside), MakePointer(Outside, Start, true), false);
			Slate.GetUser(0)->ReleaseCapture(CardTestPointerIndex);
			Test.TestTrue(TEXT("非正常丢失捕获会立即回正"), IsCardNeutral() && !Card->HasMouseCapture());
			Test.TestTrue(TEXT("关闭前可正常开始下一次拖动"), PressCard(Start));
			Slate.RoutePointerMoveEvent(HitAt(Outside), MakePointer(Outside, Start, true), false);
			Shop.CloseShop();
			Test.TestTrue(TEXT("关闭商店立即取消抓牌并回正"), IsCardNeutral() && !Card->HasMouseCapture());
			Test.TestNull(TEXT("关闭商店解绑当前牌的动态画面"), Studio->GetDynamicTexture(Card.Get()));
			Test.TestFalse(TEXT("商店关闭不再驱动摄影"), Studio->IsTickable());
			return true;
		}

	private:
		/** 独立于系统鼠标的测试指针，避免测试与使用者的鼠标位置争夺同一条路由。 */
		static constexpr uint32 CardTestPointerIndex = 100;

		/** 直接路由真实的 Slate 事件，不移动用户的系统鼠标，也不调用任何买卖逻辑。 */
		static FPointerEvent MakePointer(const FVector2D& Position, const FVector2D& Previous, bool bPressed = false,
			const FKey& Button = EKeys::Invalid)
		{
			const TSet<FKey> Pressed = bPressed ? TSet<FKey>{EKeys::LeftMouseButton} : TSet<FKey>();
			return FPointerEvent(0u, CardTestPointerIndex, Position, Previous, Pressed, Button, 0.0f, FModifierKeysState());
		}

		static FVector2D CardScreenPoint(const UCastleShopCardWidget& Widget, const FVector2D& UV)
		{
			const FGeometry Geometry = Widget.GetCachedGeometry();
			return Geometry.LocalToAbsolute(FVector2D(Geometry.GetLocalSize()) * UV);
		}

		FWidgetPath HitAt(const FVector2D& Position) const
		{
			const TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow();
			if (!Test.TestTrue(TEXT("游戏视口存在真实 Slate 命中窗口"), Window.IsValid())) { return FWidgetPath(); }
			return FSlateApplication::Get().LocateWindowUnderMouse(Position, {Window.ToSharedRef()}, false, 0);
		}

		bool PressCard(const FVector2D& Position) const
		{
			// 离屏 RHI 没有活跃的操作系统窗口，只在这次测试路由允许 Slate 接受后台的捕获请求。
			FSlateApplication& Slate = FSlateApplication::Get();
			const bool bPrevious = Slate.GetHandleDeviceInputWhenApplicationNotActive();
			Slate.SetHandleDeviceInputWhenApplicationNotActive(true);
			const FReply Reply = Slate.RoutePointerDownEvent(HitAt(Position), MakePointer(
				Position, Position, true, EKeys::LeftMouseButton));
			Slate.SetHandleDeviceInputWhenApplicationNotActive(bPrevious);
			return Reply.IsEventHandled() && Card->HasMouseCaptureByUser(0, CardTestPointerIndex);
		}

		bool IsCardNeutral() const
		{
			FLinearColor State, X, Y;
			return Face->GetVectorParameterValue(FMaterialParameterInfo(TEXT("CardState")), State) && State.R == 0.0f && State.G == 0.0f
				&& Projection->GetVectorParameterValue(FMaterialParameterInfo(TEXT("ProjectionX")), X) && X.B == 0.0f
				&& Projection->GetVectorParameterValue(FMaterialParameterInfo(TEXT("ProjectionY")), Y) && Y.B == 0.0f && X.A == Y.A;
		}

		bool ReadCardSpans(int32& OutLeft, int32& OutRight) const
		{
			// 读取最终经过 Retainer、材质和 UMG 绘制的牌位，而不是把材质参数本身当成视觉结果。
			TArray<FColor> Pixels;
			FIntVector Size;
			if (!Test.TestTrue(TEXT("能读回最终的 UMG 牌面像素"), FSlateApplication::Get().TakeScreenshot(
				Card->GetCachedWidget().ToSharedRef(), Pixels, Size) && Size.X > 32 && Size.Y > 32)) { return false; }
			const FColor Background = Pixels[0]; // 圆角之外是等色商店底板。
			const auto Span = [&](int32 X)
			{
				int32 Top = Size.Y, Bottom = -1;
				for (int32 Y = 0; Y < Size.Y; ++Y)
				{
					const FColor Pixel = Pixels[Y * Size.X + X];
					if (FMath::Max3(FMath::Abs(Pixel.R - Background.R), FMath::Abs(Pixel.G - Background.G),
						FMath::Abs(Pixel.B - Background.B)) > 36)
					{
						Top = FMath::Min(Top, Y);
						Bottom = Y;
					}
				}
				return Bottom >= Top ? Bottom - Top + 1 : 0;
			};
			OutLeft = Span(Size.X / 5);
			OutRight = Span(Size.X - 1 - Size.X / 5);
			return true;
		}

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
		TWeakObjectPtr<UCastleShopCardWidget> OtherCard;
		TWeakObjectPtr<UButton> FoilToggle;
		TWeakObjectPtr<UMaterialInstanceDynamic> Face;
		TWeakObjectPtr<UMaterialInstanceDynamic> Projection;
		TWeakObjectPtr<UTextureRenderTarget2D> Still;
		TArray<FFloat16Color> StillPixels;
		FVector2D Start;
		FVector2D Outside;
		FVector2D SecondPoint;
		uint64 FirstCaptureNumber = 0;
		int32 FlatFarSpan = 0;
		int32 FlatNearSpan = 0;
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
