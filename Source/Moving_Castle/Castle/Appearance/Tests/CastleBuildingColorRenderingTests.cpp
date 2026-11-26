#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Castle/Actors/CastleBoard.h"
#include "Castle/Data/CastleBuildingDefinition.h"
#include "Castle/UI/Cards/CastleCardPortraitSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Math/Float16Color.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace CastleBuildingColorTests
{
	/** 按正常卡牌摄影流程逐个取景，从 GPU 读回图像。相机固定时比较覆盖区，透明背景不计入色差。 */
	class FCheckPortraits final : public IAutomationLatentCommand
	{
	public:
		explicit FCheckPortraits(FAutomationTestBase& InTest) : Test(InTest) {}
		~FCheckPortraits()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		bool Update() override
		{
			static const TCHAR* Families[] = {TEXT("ArcherTower"),TEXT("CannonTower"),TEXT("Farm"),TEXT("BalloonTower"),
				TEXT("BeeHive"),TEXT("ShieldTower"),TEXT("NormalRoom"),TEXT("SpeedRoom")};
			if (!World)
			{
				const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
					.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
				World = UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
				if (!Test.TestNotNull(TEXT("创建配色验证世界"),World)) { return true; }
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
				Board = World->SpawnActor<ACastleBoard>();
				Studio = World->GetSubsystem<UCastleCardPortraitSubsystem>();
				if (!Test.TestNotNull(TEXT("创建棋盘"),Board) || !Test.TestNotNull(TEXT("创建正式卡图摄影棚"),Studio)) { return true; }
				Studio->SetShopVisible(Board,true);
				if (!IFileManager::Get().MakeDirectory(*Output,true)) { Test.AddError(Output); return true; }
			}
			const auto Playable = CastleTileColors::GetPlayable();
			const ECastleTileColor Color = Playable[ColorIndex];
			const FString Family = Families[FamilyIndex];
			if (!Current)
			{
				const FString Path = TEXT("/Game/MovingCastle/Data/Buildings/DA_") + Family;
				auto* Definition = LoadObject<UCastleBuildingDefinition>(nullptr,*Path);
				FText Reason;
				FCastleBuildingDescriptor Card;
				if (!Test.TestNotNull(Path,Definition)) { return true; }
				// 枚举正式定义提供的每个等级。普通房和加速房仅一级；功能塔各自配置可用的最高等级。
				LevelCount = Definition->Levels.Num();
				if (!Definition->Levels.IsValidIndex(LevelIndex) || !Definition->CreateDescriptor(Definition->Levels[LevelIndex].Level,Color,3,Card,Reason))
				{
					Test.AddError(Family + Reason.ToString());
					return true;
				}
				Current = Studio->RequestPortrait(Card,Board,Reason);
				if (!Test.TestNotNull(Family + Reason.ToString(),Current)) { return true; }
				Card.Number = 7;
				Test.TestTrue(Family + TEXT(" 同色异号仍共享卡图"), Studio->RequestPortrait(Card,Board,Reason) == Current);
				if (!Test.TestFalse(Family + TEXT(" 同模型异色不能命中旧卡图"),Portraits.Contains(Current))) { return true; }
				Portraits.Add(Current);
				Deadline = FPlatformTime::Seconds()+45.;
			}
			Studio->Tick(0.f);
			if (Current->GetState() == ECastleCardPortraitState::Pending && FPlatformTime::Seconds()<Deadline) { return false; }
			if (!Test.TestEqual(Family + Current->GetFailureReason().ToString(),Current->GetState(),ECastleCardPortraitState::Ready)) { return true; }
			auto* Target = Current->GetTexture();
			TArray<FFloat16Color> HDR;
			if (!Target || !Target->GameThread_GetRenderTargetResource()->ReadFloat16Pixels(HDR) || HDR.Num()!=Target->SizeX*Target->SizeY)
			{
				Test.AddError(Family + TEXT(" 无法从 GPU 读回完整卡图"));
				return true;
			}
			TArray<FColor> Pixels;
			Pixels.Reserve(HDR.Num());
			// 与牌面相同，Alpha 为反向覆盖率。PNG 用直通 Alpha；测试只比较不透明像素。
			for (const FFloat16Color& Pixel : HDR)
			{
				const float Coverage = 1.f-Pixel.A.GetFloat();
				const FLinearColor Straight = Coverage>0.f ? FLinearColor(Pixel.R.GetFloat(),Pixel.G.GetFloat(),Pixel.B.GetFloat())/Coverage : FLinearColor::Black;
				FColor Encoded = Straight.ToFColorSRGB();
				Encoded.A = FColor::QuantizeUNormFloatTo8(Coverage);
				Pixels.Add(Encoded);
			}
			const FString LevelSuffix = LevelIndex == 0 ? FString() : FString::Printf(TEXT("-Lv%d"),LevelIndex+1);
			const FString Name = Family + LevelSuffix + TEXT("-") + StaticEnum<ECastleTileColor>()->GetNameStringByValue(static_cast<int64>(Color));
			const FString Filename = Output / (Name + TEXT(".png"));
			if (!Test.TestTrue(Filename, FImageUtils::SaveImageByExtension(*Filename,FImageView(Pixels.GetData(),Target->SizeX,Target->SizeY)))) { return true; }
			for (const auto& Previous : Samples)
			{
				int32 Changed = 0, Opaque = 0;
				for (int32 I=0; I<Pixels.Num(); ++I)
				{
					const FColor A = Pixels[I], B = Previous[I];
					if (A.A>=250 && B.A>=250)
					{
						++Opaque;
						if (FMath::Max3(FMath::Abs(A.R-B.R),FMath::Abs(A.G-B.G),FMath::Abs(A.B-B.B))>=12) { ++Changed; }
					}
				}
				Test.TestTrue(FString::Printf(TEXT("%s 四色须有可见差异（%d / %d）"),*Name,Changed,Opaque), Opaque>500 && Changed>Opaque/100);
			}
			Samples.Add(MoveTemp(Pixels));
			Current = nullptr;
			if (++ColorIndex == Playable.Num())
			{
				ColorIndex=0;
				Samples.Reset();
				if (++LevelIndex == LevelCount) { LevelIndex=0; ++FamilyIndex; }
			}
			return FamilyIndex == UE_ARRAY_COUNT(Families);
		}
	private:
		FAutomationTestBase& Test;
		const FString Output = FPaths::ProjectSavedDir()/TEXT("Automation/BuildingColors");
		UWorld* World = nullptr;
		ACastleBoard* Board = nullptr;
		UCastleCardPortraitSubsystem* Studio = nullptr;
		UCastleCardPortrait* Current = nullptr;
		TSet<UCastleCardPortrait*> Portraits;
		TArray<TArray<FColor>> Samples;
		int32 FamilyIndex = 0, LevelIndex = 0, LevelCount = 0, ColorIndex = 0;
		double Deadline = 0.;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleBuildingColorRendering, "MovingCastle.Castle.Appearance.FourColorPortraits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCastleBuildingColorRendering::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("四色成像需要渲染设备"), FApp::CanEverRender())) { return false; }
	ADD_LATENT_AUTOMATION_COMMAND(CastleBuildingColorTests::FCheckPortraits(*this));
	return true;
}
#endif
