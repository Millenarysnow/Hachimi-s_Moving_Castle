#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Level.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/ToonProfile.h"
#include "Engine/World.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialFunction.h"
#include "Misc/AutomationTest.h"
#include "ShaderCompiler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleStylizedAssets, "MovingCastle.Rendering.StylizedAssetIntegrity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleStylizedAssets::RunTest(const FString& Parameters)
{
	// 直接读正式地图及资产，避免测试夹具补齐缺失配置。
	const UWorld* Map = LoadObject<UWorld>(nullptr, TEXT("/Game/MovingCastle/Maps/MainMap.MainMap"));
	if (!TestNotNull(TEXT("加载 MainMap"), Map) || !TestNotNull(TEXT("主关持久关卡"), Map->PersistentLevel.Get()))
	{
		return false;
	}
	int32 Suns = 0;
	int32 Clouds = 0;
	int32 SkyDomes = 0;
	int32 ExposureVolumes = 0;
	for (const AActor* Actor : Map->PersistentLevel->Actors)
	{
		if (!Actor) { continue; }
		TestFalse(TEXT("主关没有动态天气蓝图"), Actor->GetClass()->GetName().Contains(TEXT("BP_StylizedWeather_fix")));
		Suns += Actor->IsA<ADirectionalLight>() ? 1 : 0;
		if (const auto* Mesh = Actor->FindComponentByClass<UStaticMeshComponent>();
			Mesh && GetNameSafe(Mesh->GetMaterial(0)) == TEXT("MI_StylizedSkyDome"))
		{
			++SkyDomes;
			// 碰撞预设须写入正式地图，以免重载本体时重新参与玩法 Visibility 查询。
			TestEqual(TEXT("天空球使用持久的无碰撞配置"), Mesh->GetCollisionProfileName(), FName(TEXT("NoCollision")));
			TestEqual(TEXT("天空球读取后实际无碰撞"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		}
		if (const auto* Cloud = Actor->FindComponentByClass<UVolumetricCloudComponent>())
		{
			++Clouds;
			TestEqual(TEXT("体积云绑定可调材质实例"), GetNameSafe(Cloud->GetMaterial()), FString(TEXT("MI_AerialCloudscape")));
			TestTrue(TEXT("原生云在 40 km 淡出后仍能继续追踪"), Cloud->TracingMaxDistance > 40.0f);
		}
		const auto* Volume = Cast<APostProcessVolume>(Actor);
		if (Volume && Volume->bUnbound && Volume->Settings.bOverride_AutoExposureMethod)
		{
			++ExposureVolumes;
			TestEqual(TEXT("曝光手动档"), Volume->Settings.AutoExposureMethod.GetValue(), AEM_Manual);
			TestTrue(TEXT("实际挂载暖棕描边实例"), Volume->Settings.WeightedBlendables.Array.ContainsByPredicate(
				[](const FWeightedBlendable& Entry)
				{
					return Entry.Object && Entry.Object->GetFName() == TEXT("MI_PP_StylizedOutline") && Entry.Weight == 1.0f;
				}));
		}
	}
	TestEqual(TEXT("唯一原生太阳"), Suns, 1);
	TestEqual(TEXT("唯一原生体积云"), Clouds, 1);
	TestEqual(TEXT("唯一天空球"), SkyDomes, 1);
	TestEqual(TEXT("唯一固定曝光体积"), ExposureVolumes, 1);

	const FString Root(TEXT("/Game/MovingCastle/Rendering/"));
	for (const TCHAR* Kind : {TEXT("Surface"), TEXT("Unit"), TEXT("Metal")})
	{
		const UToonProfile* Profile = LoadObject<UToonProfile>(nullptr, *(Root + FString::Printf(TEXT("Profiles/TP_%s.TP_%s"), Kind, Kind)));
		if (TestNotNull(FString::Printf(TEXT("%s Profile"), Kind), Profile))
		{
			TestTrue(TEXT("漫反射色阶包含 VSM 阴影"), Profile->Settings.bDiffuseRampIncludeShadow);
			// 零消光在发行版的 VSM 透射路径中会使 Toon 接收面的遮挡失效。
			TestEqual(TEXT("Toon 近景 VSM 投影消光系数"), Profile->Settings.ShadowExtinctionCoefficient, 0.65f);
			// 过强的间接光会盖过色阶。检查序列化的 Profile 和制作器使用同一补光比例。
			TestEqual(TEXT("限制间接漫反射以保留受光和背光反差"), Profile->Settings.DiffuseIndirectScale, 0.30f);
			TestEqual(TEXT("漫反射色阶有四个控制点"), Profile->Settings.DiffuseRamp.ColorCurves[0].GetNumKeys(), 4);
		}
		const UMaterialFunction* Function = LoadObject<UMaterialFunction>(nullptr,
			*(Root + FString::Printf(TEXT("Functions/MF_ToonSurface_%s.MF_ToonSurface_%s"), Kind, Kind)));
		if (TestNotNull(FString::Printf(TEXT("%s 表面函数"), Kind), Function))
		{
			int32 ToonNodes = 0;
			for (const UMaterialExpression* Expression : Function->GetExpressions())
			{
				const auto* Toon = Cast<UMaterialExpressionSubstrateToonBSDF>(Expression);
				if (Toon)
				{
					++ToonNodes;
					TestTrue(TEXT("节点绑定同族 Profile"), Toon->ToonProfile == Profile);
				}
			}
			TestEqual(TEXT("共享表面内恰有一个原生 Toon 节点"), ToonNodes, 1);
		}
	}

	// 检查发行版 Win64 的 SM6 资源，等待异步编译后读取着色器和错误。
	const TCHAR* Paths[] = {
		TEXT("/Game/MovingCastle/Rendering/Sky/M_StylizedSkyDome.M_StylizedSkyDome"),
		TEXT("/Game/MovingCastle/Rendering/PostProcess/M_PP_StylizedOutline.M_PP_StylizedOutline"),
		TEXT("/Game/MovingCastle/Rendering/Materials/M_Toon_BasicColor.M_Toon_BasicColor"),
		TEXT("/Game/MovingCastle/Rendering/Materials/M_Info_UnlitColor.M_Info_UnlitColor"),
		TEXT("/Game/MovingCastle/Rendering/Materials/M_Toon_FBXPhong.M_Toon_FBXPhong"),
		TEXT("/Game/MovingCastle/Rendering/Materials/M_Toon_NPCPolyart.M_Toon_NPCPolyart"),
		TEXT("/Game/MovingCastle/Rendering/Materials/M_Toon_EnemyPolyart.M_Toon_EnemyPolyart"),
		TEXT("/Game/MovingCastle/Rendering/Materials/M_Toon_EnemyPolyartMask.M_Toon_EnemyPolyartMask"),
		TEXT("/Game/MovingCastle/Rendering/Materials/M_Toon_MassHumanoid.M_Toon_MassHumanoid"),
		TEXT("/Game/MovingCastle/Rendering/Materials/M_Toon_TutorialLord.M_Toon_TutorialLord"),
		TEXT("/Game/MovingCastle/Mass/M_MassEnemyTint.M_MassEnemyTint"),
		TEXT("/AerialCloudscape/Materials/M_AerialCloudscape.M_AerialCloudscape"),
		TEXT("/AerialTerrain/Materials/M_AerialTerrainSurface.M_AerialTerrainSurface"),
	};
	TArray<UMaterial*> Materials;
	for (const TCHAR* Path : Paths)
	{
		UMaterial* Material = LoadObject<UMaterial>(nullptr, Path);
		if (TestNotNull(Path, Material)) { Materials.Add(Material); }
	}
	if (GShaderCompilingManager) { GShaderCompilingManager->FinishAllCompilation(); }
	for (const UMaterial* Material : Materials)
	{
		const FMaterialResource* Resource = Material->GetMaterialResource(SP_PCD3D_SM6);
		TestTrue(Material->GetPathName(), Resource && Resource->GetCompileErrors().IsEmpty() && Resource->GetGameThreadShaderMap());
		for (const UMaterialExpression* Expression : Material->GetExpressions())
		{
			TestFalse(TEXT("母材质不直接放置 Toon 节点"), Expression && Expression->IsA<UMaterialExpressionSubstrateToonBSDF>());
		}
		const FString Name = Material->GetName();
		if (Name.StartsWith(TEXT("M_Toon_")) || Name == TEXT("M_MassEnemyTint"))
		{
			TestTrue(Name + TEXT(" 声明骨骼、实例化静态与 Mass 骨骼用途"), Material->GetUsageByFlag(MATUSAGE_SkeletalMesh)
				&& Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes) && Material->GetUsageByFlag(MATUSAGE_InstancedSkinnedMesh));
		}
		if (Name == TEXT("M_PP_StylizedOutline"))
		{
			TestEqual(TEXT("混合位置在 TSR 之前"), Material->BlendableLocation.GetValue(), BL_SceneColorBeforeDOF);
			for (const TCHAR* Parameter : {TEXT("OutlineThicknessPx"), TEXT("OutlineStrength"), TEXT("NormalEdgeStrength"),
				TEXT("FadeStartMeters"), TEXT("FadeEndMeters")})
			{
				float Value = 0.0f;
				TestTrue(Parameter, Material->GetScalarParameterValue(FMaterialParameterInfo(Parameter), Value));
			}
		}
		if (Name == TEXT("M_AerialCloudscape"))
		{
			for (const TCHAR* Parameter : {TEXT("CloudPeriodKm"), TEXT("CloudExtinction"), TEXT("CloudFadeStartKm"),
				TEXT("CloudFadeEndKm"), TEXT("CloudBandLower"), TEXT("CloudBandUpper")})
			{
				float Value = 0.0f;
				TestTrue(Parameter, Material->GetScalarParameterValue(FMaterialParameterInfo(Parameter), Value));
			}
			for (const TCHAR* Parameter : {TEXT("CloudTravelPhase"), TEXT("CloudSunlitTint"), TEXT("CloudInteriorTint")})
			{
				FLinearColor Value;
				TestTrue(Parameter, Material->GetVectorParameterValue(FMaterialParameterInfo(Parameter), Value));
			}
			for (const TCHAR* Parameter : {TEXT("CloudCoverage"), TEXT("CloudShape")})
			{
				UTexture* Value = nullptr;
				TestTrue(Parameter, Material->GetTextureParameterValue(FMaterialParameterInfo(Parameter), Value) && Value);
			}
		}
	}
	return !HasAnyErrors();
}

#endif
