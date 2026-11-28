// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CastleGridCursor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_EDITOR
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Castle/Automation/CastlePIEAutomation.h"
#include "CastleBoard.h"
#include "CastlePlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interaction/CastlePatrolRoute.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/OutputDeviceNull.h"
#include "UnrealClient.h"
#endif

/** 验证运行时网格、专属全息材质与参数、三维变换和所有视觉排除标记。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePlacementHoloBoxAutomationTest,
	"MovingCastle.Placement.HoloBox.CursorMaterialAndRenderFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePlacementHoloBoxAutomationTest::RunTest(const FString& Parameters)
{
	// 指示器复用离线生成器核验过的标准立方体，并硬引用专属半透明材质。
	UStaticMesh* ExpectedMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterial* ExpectedMaterial = LoadObject<UMaterial>(nullptr,
		TEXT("/Game/MovingCastle/FX/Placement/M_CastlePlacementHoloBox.M_CastlePlacementHoloBox"));
	if (!TestNotNull(TEXT("引擎占位盒网格可加载"), ExpectedMesh)
		|| !TestNotNull(TEXT("专属全息占位盒材质已经生成"), ExpectedMaterial)) { return false; }

	const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("创建隔离的指示器测试世界"), World)) { return false; }
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		// 测试显式进入了 BeginPlay，先按正式生命周期结束再销毁世界。
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	// 进入游戏生命周期，让正式 Actor 初始化组件并独立创建运行时材质。
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();

	ACastleGridCursor* Cursor = World->SpawnActor<ACastleGridCursor>();
	if (!TestNotNull(TEXT("生成正式网格指示器"), Cursor)) { return false; }
	if (!TestNotNull(TEXT("创建动态全息材质实例"), Cursor->CursorMaterial.Get())) { return false; }

	TestTrue(TEXT("指示器使用六面 0–1 UV 的 100 cm 网格"), Cursor->CursorMesh->GetStaticMesh() == ExpectedMesh);
	TestTrue(TEXT("动态材质使用生成的全息材质"), Cursor->CursorMaterial->GetBaseMaterial() == ExpectedMaterial);
	TestEqual(TEXT("全息材质使用半透明混合"), ExpectedMaterial->GetBlendMode(), BLEND_Translucent);
	TestTrue(TEXT("从盒外可看见背面棱线"), ExpectedMaterial->IsTwoSided());
	TestTrue(TEXT("材质存在 HoloColor 参数索引"), Cursor->HoloColorParameterIndex != INDEX_NONE);
	TestTrue(TEXT("材质存在 SupportNormal 参数索引"), Cursor->SupportNormalParameterIndex != INDEX_NONE);
	TestTrue(TEXT("材质存在 InvalidMode 参数索引"), Cursor->InvalidModeParameterIndex != INDEX_NONE);
	TestEqual(TEXT("指示器碰撞已关闭"), Cursor->CursorMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("指示器不参与导航"), Cursor->CursorMesh->CanEverAffectNavigation());
	TestFalse(TEXT("指示器不投射阴影"), Cursor->CursorMesh->CastShadow);
	TestFalse(TEXT("指示器不影响距离场光照"), Cursor->CursorMesh->bAffectDistanceFieldLighting);
	TestFalse(TEXT("指示器不参与光追可见性"), Cursor->CursorMesh->bVisibleInRayTracing);
	TestFalse(TEXT("指示器不接收贴花"), Cursor->CursorMesh->bReceivesDecals);
	TestFalse(TEXT("指示器不写入 CustomDepth"), Cursor->CursorMesh->bRenderCustomDepth);

	FCastleGridCursorTarget Target;
	Target.bValid = true;
	Target.WorldNormal = FVector::YAxisVector;
	Target.IndicatorTransform = FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(400.0f, 250.0f, 800.0f),
		FVector(4.9f, 4.9f, 5.488f));
	Cursor->ApplyTarget(Target, true);
	TestFalse(TEXT("可放目标显示全息盒"), Cursor->IsHidden());
	TestTrue(TEXT("可放目标变换完整应用"), Cursor->GetActorTransform().Equals(Target.IndicatorTransform, 0.01f));
	TestTrue(TEXT("可放目标显示青蓝主色"), Cursor->CursorMaterial->K2_GetVectorParameterValue(TEXT("HoloColor"))
		.Equals(FLinearColor(0.16f, 0.88f, 1.0f, 1.0f), 0.001f));
	TestEqual(TEXT("可放目标关闭警示斜纹"), Cursor->CursorMaterial->K2_GetScalarParameterValue(TEXT("InvalidMode")), 0.0f);
	TestTrue(TEXT("来源贴面法线反向并转入盒体局部空间"), Cursor->CursorMaterial
		->K2_GetVectorParameterValue(TEXT("SupportNormal")).Equals(FLinearColor(-1.0f, 0.0f, 0.0f, 0.0f), 0.001f));

	Cursor->ApplyTarget(Target, false);
	TestTrue(TEXT("不可放目标仍显示落点盒"), !Cursor->IsHidden());
	TestTrue(TEXT("不可放目标显示警示暖红色"), Cursor->CursorMaterial->K2_GetVectorParameterValue(TEXT("HoloColor"))
		.Equals(FLinearColor(1.0f, 0.20f, 0.14f, 1.0f), 0.001f));
	TestEqual(TEXT("不可放目标开启警示斜纹"), Cursor->CursorMaterial->K2_GetScalarParameterValue(TEXT("InvalidMode")), 1.0f);
	TestTrue(TEXT("切换双态不改变盒体变换"), Cursor->GetActorTransform().Equals(Target.IndicatorTransform, 0.01f));

	Target.bValid = false;
	Cursor->ApplyTarget(Target, false);
	TestTrue(TEXT("无效目标立即隐藏全息盒"), Cursor->IsHidden());
	return true;
}

#if WITH_EDITOR
/** 使用正式地图的视口与主塔侧格，覆盖路径编辑前后控制器的完整光标更新入口。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePathEditGridCursorTest,
	"MovingCastle.Placement.HoloBox.HiddenInPathEditMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePathEditGridCursorTest::RunTest(const FString&)
{
	if (!CastlePIEAutomation::StartPIE(*this, TEXT("/Game/MovingCastle/Maps/MainMap"), FIntPoint(1280, 720))) { return false; }
	const bool bShowScreenMessages = GAreScreenMessagesEnabled;
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Deadline = FPlatformTime::Seconds() + 60.0,
		Phase = 0, ViewportFrames = 0]() mutable
	{
		UWorld* World = GEditor->PlayWorld;
		auto* Player = World ? Cast<ACastlePlayerController>(World->GetFirstPlayerController()) : nullptr;
		FText EditReason;
		const bool bCanEdit = Player && Player->GetPawn() && Player->IsCastleEditingAllowed(EditReason);
		if (!bCanEdit && FPlatformTime::Seconds() < Deadline) { return false; }
		if (!TestTrue(TEXT("MainMap 玩家进入可建造阶段"), bCanEdit)) { return true; }
		auto* Board = Cast<ACastleBoard>(UGameplayStatics::GetActorOfClass(World, ACastleBoard::StaticClass()));
		auto* Route = Cast<ACastlePatrolRoute>(UGameplayStatics::GetActorOfClass(World, ACastlePatrolRoute::StaticClass()));
		if (!TestNotNull(TEXT("真实棋盘"), Board) || !TestNotNull(TEXT("真实巡逻路径"), Route)) { return true; }

		// 截图使用正式 PIE 游戏画面。无渲染的测试仍逐帧验证同样的状态切换。
		const FString ScreenshotDir = FPaths::ProjectSavedDir() / TEXT("Coordinator");
		auto Capture = [&ScreenshotDir](const TCHAR* Filename)
		{
			if (FApp::CanEverRender())
			{
				IFileManager::Get().MakeDirectory(*ScreenshotDir, true);
				FScreenshotRequest::RequestScreenshot(ScreenshotDir / Filename, true, false, false, FIntRect(), true);
			}
		};
		if (Phase == 0)
		{
			// 经典模式使用视口中心。相机放在首层负 X 面外，瞄向正式主塔侧格。
			const FVector Center = Board->GetGridWorldLocation(Board->GetBoardCenterCoordinate());
			const FVector Eye = Center + Board->GetActorTransform().TransformVector(FVector(-2.0 * Board->CellSize, 0, 0));
			auto* Camera = World->SpawnActor<ACameraActor>(Eye, (Center - Eye).Rotation());
			if (!TestNotNull(TEXT("创建测试相机"), Camera)) { return true; }
			Player->SetControlMode(ECastleControlMode::Classic, false);
			Player->SetViewTarget(Camera);
			Player->PlayerCameraManager->UpdateCamera(0.0f);
			// 使用已有验收命令取得正式徽章，截图同时覆盖占用槽和空槽。
			FOutputDeviceNull Output;
			if (!TestTrue(TEXT("为验收画面发放正式徽章"), IConsoleManager::Get().ProcessUserConsoleInput(
				TEXT("Castle.Items.Badge 1"), Output, World))) { return true; }
			// 留出几帧给独立 PIE 窗口的首次绘制，避免截图被编辑器视口先消费。
			GAreScreenMessagesEnabled = false;
			++Phase;
			return false;
		}
		if (FScreenshotRequest::IsScreenshotRequested())
		{
			if (FPlatformTime::Seconds() < Deadline) { return false; }
			AddError(TEXT("MainMap 验收截图超时。"));
			return true;
		}
		Player->Tick(0.0f);
		const auto* Cursor = Player->GridCursor.Get();
		if (!TestNotNull(TEXT("控制器生成建造光标"), Cursor)) { return true; }
		if (Phase == 1)
		{
			if (++ViewportFrames < 4) { return false; }
			if (!TestTrue(TEXT("建造态瞄准主塔侧格且显示全息块"), Player->CurrentGridCursorTarget.bValid
				&& Player->CurrentGridCursorTarget.Surface == ECastleGridCursorSurface::MainTowerSide && !Cursor->IsHidden())) { return true; }
			Capture(TEXT("Temp-MainMap-徽章槽与建造光标.png"));
			++Phase;
			return false;
		}
		if (Phase == 2)
		{
			// 经由 PlayerInput 的正式 P 键绑定进入路径编辑，使用正帧长派发普通输入。
			Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::P, IE_Pressed, 1.0f));
			Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::P, IE_Released, 0.0f));
			Player->PlayerTick(1.0f / 60.0f);
			TestTrue(TEXT("进入正式路径编辑态"), Route->IsEditing());
			Player->Tick(0.0f);
			TestTrue(TEXT("编辑态隐藏建造光标"), Cursor->IsHidden());
			TestFalse(TEXT("编辑态清空旧建造落点"), Player->CurrentGridCursorTarget.bValid);
			TestTrue(TEXT("编辑态保留经典模式中心准星"), Player->ShouldDrawCenterCrosshair());
			Capture(TEXT("Temp-MainMap-路径编辑无建造光标.png"));
			++Phase;
			return false;
		}

		// 取消路径草稿后沿用同一瞄准射线，证明恢复由路径状态切换触发。
		Route->CancelEditingDraft();
		TestFalse(TEXT("退出路径编辑态"), Route->IsEditing());
		Player->Tick(0.0f);
		TestTrue(TEXT("恢复建造落点和全息块"), Player->CurrentGridCursorTarget.bValid && !Cursor->IsHidden());
		return true;
	}));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(CastlePIEAutomation::StopPIE()));
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([bShowScreenMessages]()
	{
		// 包括断言失败时，退回编辑器后都恢复原有屏幕消息开关。
		GAreScreenMessagesEnabled = bShowScreenMessages;
		return true;
	}));
	return true;
}
#endif

#endif
