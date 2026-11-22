// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Castle/UI/Cards/CastleCardPortraitSubsystem.h"
#include "Castle/UI/Cards/CastleShopCardWidget.h"
#include "Castle/UI/Shop/CastleShopBenchSlotWidget.h"
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
				// 只从当前视口商店实际挂着的商品槽取卡，不把旧实例计进测试。
				TArray<UUserWidget*> Shops;
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Shops, UCastleShopWidget::StaticClass(), true);
				if (!Test.TestEqual(TEXT("视口有一个正式商店"), Shops.Num(), 1)) { return true; }
				UCastleShopWidget* ShopUI = Cast<UCastleShopWidget>(Shops[0]);
				if (!Test.TestNotNull(TEXT("正式商店类型有效"), ShopUI)
					|| !Test.TestTrue(TEXT("货币战争风格商店 WBP 校验通过"), ShopUI->ValidatePresentation()))
				{
					return true;
				}
				TArray<UCastleShopCardWidget*> Widgets = ShopUI->GetOfferCards();
				if (!Test.TestEqual(TEXT("实际 UMG 商品 Widget 数"), Widgets.Num(), 6)) { return true; }
				RoomShop = ShopUI;
				Card = Widgets[0];
				OtherCard = Widgets[1];
				FoilToggle = Cast<UButton>(ShopUI->WidgetTree->FindWidget(TEXT("FoilPreview")));
				const FGeometry Geometry = Card->GetCachedGeometry();
				if (!Test.TestTrue(TEXT("卡牌已在商店布局"), Geometry.GetLocalSize().X > 0.0)) { return true; }
				UMaterialInstanceDynamic* Material = GetFaceMaterial(*Card.Get());
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
				Projection = GetProjectionMaterial(*Card.Get());
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
				Start = WidgetScreenPoint(*Card.Get(), FVector2D(0.50, 0.45));
				Outside = WidgetScreenPoint(*Card.Get(), FVector2D(1.34, 0.45));
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
				int32 FlatLeft = 0, FlatRight = 0;
				if (!ReadCardSpans(true, FlatLeft, FlatRight)) { return true; }
				Test.TestTrue(FString::Printf(TEXT("平放时卡面两侧等高（左 %d、右 %d）"), FlatLeft, FlatRight),
					FlatLeft > 0 && FlatRight > 0 && FMath::Abs(FlatLeft - FlatRight) <= 3);
				FSlateApplication& Slate = FSlateApplication::Get();
				if (!Test.TestTrue(TEXT("按下牌面获得鼠标捕获"), PressCard(Start))) { Shop.CloseShop(); return true; }
				Test.TestTrue(TEXT("只按下不拖动，仍使用正面"), IsCardNeutral());
				// 方案 A：位移超过拖拽阈值进入 3D 赏牌，不触发购买。
				Slate.RoutePointerMoveEvent(HitAt(Outside), MakePointer(Outside, Start, true), false);
				Test.TestTrue(TEXT("越过卡位边界仍由抓住的卡持有鼠标"), Card->HasMouseCapture());
				++Phase;
				return false;
			}
			if (Phase == 4)
			{
				UTextureRenderTarget2D* Dynamic = Studio->GetDynamicTexture(Card.Get());
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
				if (!CheckDragDirection(TEXT("右拖"), true, true)) { Shop.CloseShop(); return true; }
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
				// 不松手，从同一个抓牌起点检查剩余三向；每换一次方向等到下一帧再读渲染像素。
				struct FDragProbe
				{
					const TCHAR* Name;
					FVector2D UV;
					bool bHorizontal;
					bool bDragPositive;
				};
				static const FDragProbe Probes[] = {
					{TEXT("左拖"), FVector2D(0.06, 0.45), true, false},
					{TEXT("上拖"), FVector2D(0.50, 0.05), false, false},
					{TEXT("下拖"), FVector2D(0.50, 0.85), false, true},
				};
				FVector2D Previous = Outside;
				if (DragProbeCount > 0)
				{
					const FDragProbe& Last = Probes[DragProbeCount - 1];
					if (!CheckDragDirection(Last.Name, Last.bHorizontal, Last.bDragPositive)) { Shop.CloseShop(); return true; }
					Previous = WidgetScreenPoint(*Card.Get(), Last.UV);
				}
				FSlateApplication& Slate = FSlateApplication::Get();
				if (DragProbeCount < UE_ARRAY_COUNT(Probes))
				{
					const FVector2D Next = WidgetScreenPoint(*Card.Get(), Probes[DragProbeCount].UV);
					Slate.RoutePointerMoveEvent(HitAt(Next), MakePointer(Next, Previous, true), false);
					++DragProbeCount;
					return false;
				}
				// 在同一个动态纹理里拍下一角度，覆盖“纹理指针不变但内容已变”的更新路径。
				Studio->GetDynamicTexture(Card.Get(), &CaptureBeforeDiagonal);
				DiagonalPoint = WidgetScreenPoint(*Card.Get(), FVector2D(0.61, 0.78));
				Slate.RoutePointerMoveEvent(HitAt(DiagonalPoint), MakePointer(DiagonalPoint, Previous, true), false);
				++Phase;
				return false;
			}
			if (Phase == 6)
			{
				uint64 CaptureNumber = 0;
				Studio->GetDynamicTexture(Card.Get(), &CaptureNumber);
				if (CaptureNumber <= CaptureBeforeDiagonal) { return false; }
				FSlateApplication::Get().RoutePointerUpEvent(HitAt(DiagonalPoint), MakePointer(DiagonalPoint, DiagonalPoint, false, EKeys::LeftMouseButton));
				Test.TestTrue(TEXT("松手立即归还鼠标，保留当前角度开始平滑回正"), !Card->HasMouseCapture() && !IsCardNeutral());
				++Phase;
				return false;
			}
			if (Phase == 8)
			{
				CheckOfferPortraits(Shop, *Studio, TEXT("重开后"));
				if (!BeginBenchSale(Shop, Controller->GetEconomy()->GetGold()))
				{
					Shop.CloseShop();
					return true;
				}
				++Phase;
				return false;
			}
			if (Phase == 9)
			{
				// 等回收区完成本帧布局，再以实际 Slate 路由松手并检查交易结果。
				const FVector2D Drop = WidgetScreenPoint(*SellTarget.Get(), FVector2D(0.5, 0.5));
				FSlateApplication& Slate = FSlateApplication::Get();
				Slate.RoutePointerMoveEvent(HitAt(Drop), MakePointer(Drop, DragPoint, true), false);
				Slate.RoutePointerUpEvent(HitAt(Drop), MakePointer(Drop, Drop, false, EKeys::LeftMouseButton));
				Test.TestEqual(TEXT("拖入右侧回收区只售出一张背包牌"), Shop.GetBackpackItems().Num(), BackpackCountBeforeSale - 1);
				Test.TestEqual(TEXT("拖拽出售增加准确报价"), Controller->GetEconomy()->GetGold(), GoldBeforeSale + QuotedSellPrice);
				Test.TestFalse(TEXT("售出卡牌后关闭出售遮罩"), RoomShop->IsSellDropOverlayVisible());
				Shop.CloseShop();
				return true;
			}
			if (!IsCardNeutral()) { return false; }
			Test.TestNull(TEXT("回正后解除动态画面"), Studio->GetDynamicTexture(Card.Get()));
			Test.TestFalse(TEXT("静止时不再驱动摄影"), Studio->IsTickable());

			// 方案 A 验收：若开局背包已满先售出 1 张腾出备战席，再短点商品卡直接购入并扣款入包。
			if (Shop.GetBackpackItems().Num() >= 10)
			{
				FText SellReason;
				Test.TestTrue(TEXT("售出一张备战席卡牌腾出购买空间"), Shop.TrySellBackpackItem(0, SellReason));
			}
			const int32 BackpackCountBeforeBuy = Shop.GetBackpackItems().Num();
			const FVector2D BuyPoint = WidgetScreenPoint(*OtherCard.Get(), FVector2D(0.50, 0.50));
			const int32 GoldBeforeShortClick = Controller->GetEconomy()->GetGold();
			const int32 OtherOfferPrice = Offers[1].Price;
			{
				FSlateApplication& Slate = FSlateApplication::Get();
				const bool bPrevActive = Slate.GetHandleDeviceInputWhenApplicationNotActive();
				Slate.SetHandleDeviceInputWhenApplicationNotActive(true);
				Slate.RoutePointerDownEvent(HitAt(BuyPoint), MakePointer(BuyPoint, BuyPoint, true, EKeys::LeftMouseButton));
				Slate.RoutePointerUpEvent(HitAt(BuyPoint), MakePointer(BuyPoint, BuyPoint, false, EKeys::LeftMouseButton));
				Slate.SetHandleDeviceInputWhenApplicationNotActive(bPrevActive);
			}
			Test.TestTrue(TEXT("方案 A 短点商品卡直接购入并标记售罄"), Shop.GetOffers()[1].bSold);
			Test.TestEqual(TEXT("方案 A 短点购买扣除准确报价"), Controller->GetEconomy()->GetGold(), GoldBeforeShortClick - OtherOfferPrice);
			Test.TestEqual(TEXT("方案 A 短点购买将卡牌送入备战席"), Shop.GetBackpackItems().Num(), BackpackCountBeforeBuy + 1);
			CheckOfferPortraits(Shop, *Studio, TEXT("购买后"));

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
			FText Reason;
			if (!Test.TestTrue(TEXT("正常重新打开保留原商品的商店"), Shop.TryOpenShop(Reason)))
			{
				Test.AddError(Reason.ToString());
				return true;
			}
			++Phase;
			return false;
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

		static FVector2D WidgetScreenPoint(const UWidget& Widget, const FVector2D& UV)
		{
			const FGeometry Geometry = Widget.GetCachedGeometry();
			return Geometry.LocalToAbsolute(FVector2D(Geometry.GetLocalSize()) * UV);
		}

		/** 从实际绘制控件取当前材质，覆盖刷新时重新创建的实例。 */
		static UMaterialInstanceDynamic* GetFaceMaterial(const UCastleShopCardWidget& Widget)
		{
			const UImage* CardFace = Cast<UImage>(Widget.WidgetTree->FindWidget(TEXT("CardFace")));
			return CardFace ? Cast<UMaterialInstanceDynamic>(CardFace->GetBrush().GetResourceObject()) : nullptr;
		}

		static UMaterialInstanceDynamic* GetProjectionMaterial(const UCastleShopCardWidget& Widget)
		{
			const URetainerBox* Retainer = Cast<URetainerBox>(Widget.WidgetTree->FindWidget(TEXT("CardProjection")));
			return Retainer ? Retainer->GetEffectMaterial() : nullptr;
		}

		/** 同一缩略图可以由多个槽位共用。逐槽验证刷新后的材质实际绑定了对应纹理，包括售罄槽。 */
		void CheckOfferPortraits(const UCastleShopComponent& Shop, UCastleCardPortraitSubsystem& Studio, const TCHAR* Context)
		{
			const TArray<UCastleShopCardWidget*> Widgets = RoomShop->GetOfferCards();
			const TArray<FCastleShopOffer> CurrentOffers = Shop.GetOffers();
			if (!Test.TestEqual(TEXT("刷新后的商品与控件数量匹配"), Widgets.Num(), CurrentOffers.Num())) { return; }
			for (int32 Index = 0; Index < Widgets.Num(); ++Index)
			{
				FText Reason;
				UCastleCardPortrait* Portrait = Studio.RequestPortrait(CurrentOffers[Index].Descriptor, Shop.GetBoundBoard(), Reason);
				const UMaterialInstanceDynamic* Material = GetFaceMaterial(*Widgets[Index]);
				UTexture* BoundTexture = nullptr;
				FLinearColor State;
				Test.TestTrue(FString::Printf(TEXT("%s第 %d 张商品牌仍显示对应建筑缩略图"), Context, Index + 1),
					Portrait && Portrait->GetTexture() && Material
					&& Material->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Portrait")), BoundTexture)
					&& BoundTexture == Portrait->GetTexture()
					&& Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("CardState")), State) && State.A == 1.0f);
			}
		}

		/** 用真实命中路径从备战席开始拖拽，避免直接调用交易组件掩盖槽位无法命中的问题。 */
		bool BeginBenchSale(const UCastleShopComponent& Shop, const int32 CurrentGold)
		{
			BackpackCountBeforeSale = Shop.GetBackpackItems().Num();
			GoldBeforeSale = CurrentGold;
			FText Reason;
			if (!Test.TestTrue(TEXT("备战席首张卡可取得卖价"), Shop.TryGetSellPrice(0, QuotedSellPrice, Reason)))
			{
				Test.AddError(Reason.ToString());
				return false;
			}
			const UCastleShopBenchSlotWidget* BenchSlot = RoomShop->GetBenchSlots()[0];
			SellTarget = RoomShop->WidgetTree->FindWidget(TEXT("RightSellTarget"));
			if (!Test.TestNotNull(TEXT("运行中的商店有右侧回收区"), SellTarget.Get())) { return false; }
			const FVector2D Origin = WidgetScreenPoint(*BenchSlot, FVector2D(0.5, 0.5));
			const FWidgetPath Hit = HitAt(Origin);
			if (!Test.TestTrue(TEXT("备战席整张卡片可被鼠标命中"), Hit.ContainsWidget(BenchSlot->GetCachedWidget().Get())))
			{
				return false;
			}
			DragPoint = WidgetScreenPoint(*SellTarget.Get(), FVector2D(0.5, 0.5));
			FSlateApplication& Slate = FSlateApplication::Get();
			const bool bPreviouslyAllowedBackgroundInput = Slate.GetHandleDeviceInputWhenApplicationNotActive();
			Slate.SetHandleDeviceInputWhenApplicationNotActive(true);
			Slate.RoutePointerDownEvent(Hit, MakePointer(Origin, Origin, true, EKeys::LeftMouseButton));
			Slate.RoutePointerMoveEvent(HitAt(DragPoint), MakePointer(DragPoint, Origin, true), false);
			Slate.SetHandleDeviceInputWhenApplicationNotActive(bPreviouslyAllowedBackgroundInput);
			return Test.TestTrue(TEXT("左键拖动备战席卡牌后打开出售遮罩"), RoomShop->IsSellDropOverlayVisible());
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
			const UMaterialInstanceDynamic* CurrentFace = GetFaceMaterial(*Card.Get());
			const UMaterialInstanceDynamic* CurrentProjection = GetProjectionMaterial(*Card.Get());
			FLinearColor State, X, Y;
			return CurrentFace && CurrentProjection
				&& CurrentFace->GetVectorParameterValue(FMaterialParameterInfo(TEXT("CardState")), State) && State.R == 0.0f && State.G == 0.0f
				&& CurrentProjection->GetVectorParameterValue(FMaterialParameterInfo(TEXT("ProjectionX")), X) && X.B == 0.0f
				&& CurrentProjection->GetVectorParameterValue(FMaterialParameterInfo(TEXT("ProjectionY")), Y) && Y.B == 0.0f && X.A == Y.A;
		}

		/** 拖向一侧时，卡窗使用反向倾角，且最终像素中对侧比被拖一侧更长。 */
		bool CheckDragDirection(const TCHAR* Direction, bool bHorizontal, bool bDragPositive) const
		{
			// 屏幕右/下为正，投影轴的 B 为深度分量；正向拖动应增大右/下边的深度。
			const float DragSign = bDragPositive ? 1.0f : -1.0f;
			FLinearColor State, Axis;
			const FMaterialParameterInfo AxisParam(bHorizontal ? TEXT("ProjectionX") : TEXT("ProjectionY"));
			const bool bTiltMatches = Test.TestTrue(FString::Printf(TEXT("%s时卡窗、箔光与整牌共享正确方向的倾角"), Direction),
				Face->GetVectorParameterValue(FMaterialParameterInfo(TEXT("CardState")), State) && State.B == 1.0f
				&& (bHorizontal ? State.R : State.G) * DragSign < -0.6f
				&& FMath::IsNearlyZero(bHorizontal ? State.G : State.R, 0.001f)
				&& Projection->GetVectorParameterValue(AxisParam, Axis) && Axis.B * DragSign > 0.15f);
			int32 NegativeSide = 0, PositiveSide = 0;
			if (!ReadCardSpans(bHorizontal, NegativeSide, PositiveSide)) { return false; }
			const int32 FarSpan = bDragPositive ? PositiveSide : NegativeSide;
			const int32 NearSpan = bDragPositive ? NegativeSide : PositiveSide;
			const bool bSpansMatch = Test.TestTrue(FString::Printf(TEXT("%s时被拖一侧的屏幕轮廓更短（远 %d、近 %d 像素）"), Direction, FarSpan, NearSpan),
				FarSpan > 0 && NearSpan - FarSpan > NearSpan / 25);
			return bTiltMatches && bSpansMatch;
		}

		bool ReadCardSpans(bool bHorizontal, int32& OutNegativeSide, int32& OutPositiveSide) const
		{
			// 读取最终经过 Retainer、材质和 UMG 绘制的牌位，而不是把材质参数本身当成视觉结果。
			TArray<FColor> Pixels;
			FIntVector Size;
			if (!Test.TestTrue(TEXT("能读回最终的 UMG 牌面像素"), FSlateApplication::Get().TakeScreenshot(
				Card->GetCachedWidget().ToSharedRef(), Pixels, Size) && Size.X > 32 && Size.Y > 32)) { return false; }
			const FColor Background = Pixels[0]; // 圆角之外是等色商店底板。
			// 横拖比较左右两列的高度，纵拖比较上下两行的宽度；避开圆角最外沿。
			const int32 SliceCount = bHorizontal ? Size.X : Size.Y;
			const int32 SpanLength = bHorizontal ? Size.Y : Size.X;
			const auto Span = [&](int32 Slice)
			{
				int32 First = SpanLength, Last = -1;
				for (int32 Index = 0; Index < SpanLength; ++Index)
				{
					const FColor Pixel = Pixels[bHorizontal ? Index * Size.X + Slice : Slice * Size.X + Index];
					if (FMath::Max3(FMath::Abs(Pixel.R - Background.R), FMath::Abs(Pixel.G - Background.G),
						FMath::Abs(Pixel.B - Background.B)) > 36)
					{
						First = FMath::Min(First, Index);
						Last = FMath::Max(Last, Index);
					}
				}
				return Last >= First ? Last - First + 1 : 0;
			};
			OutNegativeSide = Span(SliceCount / 5);
			OutPositiveSide = Span(SliceCount - 1 - SliceCount / 5);
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
		TWeakObjectPtr<UCastleShopWidget> RoomShop;
		TWeakObjectPtr<UCastleShopCardWidget> Card;
		TWeakObjectPtr<UCastleShopCardWidget> OtherCard;
		TWeakObjectPtr<UButton> FoilToggle;
		TWeakObjectPtr<UMaterialInstanceDynamic> Face;
		TWeakObjectPtr<UMaterialInstanceDynamic> Projection;
		TWeakObjectPtr<UTextureRenderTarget2D> Still;
		TWeakObjectPtr<UWidget> SellTarget;
		TArray<FFloat16Color> StillPixels;
		FVector2D Start;
		FVector2D Outside;
		FVector2D DiagonalPoint;
		FVector2D DragPoint;
		uint64 CaptureBeforeDiagonal = 0;
		int32 DragProbeCount = 0;
		int32 InitialGold = 0;
		int32 BackpackCountBeforeSale = 0;
		int32 GoldBeforeSale = 0;
		int32 QuotedSellPrice = 0;
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
