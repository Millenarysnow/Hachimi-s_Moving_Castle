// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CastleGridCursor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

/** 验证运行时专属网格、必需材质参数、三维变换和所有视觉排除标记。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastlePlacementHoloBoxAutomationTest,
	"MovingCastle.Placement.HoloBox.CursorMaterialAndRenderFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastlePlacementHoloBoxAutomationTest::RunTest(const FString& Parameters)
{
	// 材质和专属网格由 Source/Tools/PlacementCursor 中的离线生成器提供。
	UStaticMesh* ExpectedMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/MovingCastle/FX/Placement/SM_CastlePlacementHoloBox.SM_CastlePlacementHoloBox"));
	if (!TestNotNull(TEXT("专属全息占位盒网格已经生成"), ExpectedMesh)) { return false; }

	const UWorld::InitializationValues Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("创建隔离的指示器测试世界"), World)) { return false; }
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

	ACastleGridCursor* Cursor = World->SpawnActor<ACastleGridCursor>();
	if (!TestNotNull(TEXT("生成正式网格指示器"), Cursor)) { return false; }
	if (!TestNotNull(TEXT("创建动态全息材质实例"), Cursor->CursorMaterial.Get())) { return false; }

	TestTrue(TEXT("指示器使用专属 100 cm 网格"), Cursor->CursorMesh->GetStaticMesh() == ExpectedMesh);
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

	Target.bValid = false;
	Cursor->ApplyTarget(Target, false);
	TestTrue(TEXT("无效目标立即隐藏全息盒"), Cursor->IsHidden());
	return true;
}

#endif
