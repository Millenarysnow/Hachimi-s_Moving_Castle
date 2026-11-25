#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CastleHachimisV4PCG.h"
#include "CastleBoard.h"
#include "CastleBuildingBase.h"
#include "CastleBuildingDefinition.h"
#include "Castle/PCG/CastleBuildingPCGSubsystem.h"
#include "CastleLadderLinkComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "CastleNavigationSubsystem.h"
#include "CastleBuildingPreviewSubsystem.h"
#include "AssemblyFlightComponent.h"
#include "BuildingMergeComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"

namespace CastleV4NavigationTests
{
	const FIntVector Up(0, 0, 1);

	/** 真实地图覆盖 V4 多格叠放、盾塔位于两侧、不同属性的合法悬挑和拆除后的重新铺板。 */
	class FStackCases final : public IAutomationLatentCommand
	{
	public:
		explicit FStackCases(FAutomationTestBase& InTest) : Test(InTest) {}

		bool Update() override
		{
			if (FPlatformTime::Seconds() >= Deadline)
			{
				Test.AddError(FString::Printf(TEXT("TestMap1 叠层测试超时，场景 %d 阶段 %d"), Scene, Stage));
				Cleanup();
				return true;
			}
			if (!Board && !Initialize()) { return true; }
			if (!Navigation->IsNavigationReady()) { return false; }
			if (Stage == 0)
			{
				if (PlaceScene()) { Stage = 1; return false; }
				Cleanup();
				return true;
			}
			if (Stage == 1)
			{
				VerifyStack();
				if (!Remove(Upper)) { Cleanup(); return true; }
				FinishAnimations();
				Stage = 2;
				return false;
			}
			TArray<FCastleActiveLadderConnection> Links;
			PCG->GetActiveLadderConnections(Board, Links);
			Test.TestFalse(TEXT("移除上楼后卸载对应接口梯"), Links.ContainsByPredicate([&](const auto& Link) { return Link.Building == Lower; }));
			if (Scene >= 5) { VerifyStepRoof(false); }
			if (!Remove(Lower) || !Remove(Support)) { Cleanup(); return true; }
			for (ACastleBuildingBase*& Building : Extra) { if (!Remove(Building)) { Cleanup(); return true; } }
			Extra.Reset();
			Stage = 0;
			if (++Scene < 9) { return false; }
			if (FParse::Param(FCommandLine::Get(), TEXT("HachimisV4Capture"))) { CaptureRoomsAndPreviews(); }
			Cleanup();
			return true;
		}

	private:
		bool Initialize()
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType != EWorldType::Game && Context.WorldType != EWorldType::PIE) { continue; }
				for (TActorIterator<ACastleBoard> It(Context.World()); It; ++It) { Board = *It; break; }
				if (Board) { break; }
			}
			if (!Test.TestNotNull(TEXT("叠层回归的 TestMap1 棋盘"), Board)
				|| !Test.TestEqual(TEXT("测试从空棋盘开始"), Board->GetPlacedBuildingCount(), 0)) { return false; }
			World = Board->GetWorld();
			Navigation = World->GetSubsystem<UCastleNavigationSubsystem>();
			PCG = World->GetSubsystem<UCastleBuildingPCGSubsystem>();
			if (!Test.TestNotNull(TEXT("棋盘导航"), Navigation) || !Test.TestNotNull(TEXT("棋盘 PCG"), PCG)) { return false; }
			for (int32 X = 0; X < Board->GridSize.X; ++X)
			for (int32 Y = 0; Y < Board->GridSize.Y; ++Y)
			{
				FCastleGroundCell Data;
				if (!Board->TryGetGroundCell(Board->GridOrigin + FIntVector(X, Y, 0), Data))
				{
					Test.AddError(TEXT("叠层回归无法读取完整测试地面"));
					return false;
				}
				OriginalGround.Cells.Add(Data);
			}
			Center = Board->GridOrigin + FIntVector(1, 1, 0);
			return true;
		}

		bool MakeCard(const TCHAR* Family, int32 Level, ECastleTileColor Color, int32 Number, FCastleBuildingDescriptor& Out)
		{
			const FString Path = FString::Printf(TEXT("/Game/MovingCastle/Data/Buildings/DA_%s.DA_%s"), Family, Family);
			const UCastleBuildingDefinition* Definition = LoadObject<UCastleBuildingDefinition>(nullptr, *Path);
			if (!Test.TestNotNull(Path, Definition)) { return false; }
			FText Error;
			if (Definition->CreateDescriptor(Level, Color, Number, Out, Error)) { return true; }
			Test.AddError(Error.ToString());
			return false;
		}

		bool PlaceScene()
		{
			const bool bRotatedRooms = Scene == 4;
			const bool bShieldBelow = Scene == 2 || Scene == 5;
			const TCHAR* LowerName = bRotatedRooms ? TEXT("NormalRoom") : bShieldBelow ? TEXT("ShieldTower") : TEXT("ArcherTower");
			const TCHAR* UpperName = Scene == 8 ? TEXT("SpeedRoom") : bRotatedRooms || Scene == 7 ? TEXT("NormalRoom")
				: Scene == 1 ? TEXT("ShieldTower") : TEXT("CannonTower");
			const int32 Level = Scene == 0 ? 2 : 1;
			const bool bClosed = Scene == 3 || Scene == 6;
			FCastleBuildingDescriptor LowerCard, UpperCard;
			// 五级保持单格，正式玩法仍不自动合成五级；夹具直接放置，验证其不会再被旧 PCG 排除。
			const int32 LowerLevel = Scene == 2 ? 5 : Scene >= 5 ? 2 : Level;
			if (!MakeCard(LowerName, LowerLevel, ECastleTileColor::Red, 3, LowerCard)
				|| !MakeCard(UpperName, Level, bRotatedRooms ? ECastleTileColor::Red : ECastleTileColor::Blue,
					bClosed ? 4 : 3, UpperCard)) { return false; }
			FCastleGroundGenerationResult Ground = OriginalGround;
			const FIntVector Side = Center + FIntVector(0, 1, 0);
			for (FCastleGroundCell& Cell : Ground.Cells)
			{
				if (Cell.Coordinate == Center || Cell.Coordinate == Center + FIntVector(1, 0, 0))
				{
					Cell.Color = LowerCard.Color;
					Cell.Number = LowerCard.Number;
				}
				if ((bClosed || bRotatedRooms) && Cell.Coordinate == Side)
				{
					Cell.Color = UpperCard.Color;
					Cell.Number = UpperCard.Number;
				}
			}
			FText Error;
			if (!Board->ApplyGroundGenerationResult(Ground, Error)) { Test.AddError(Error.ToString()); return false; }
			Lower = Board->PlaceBuilding(Center, LowerCard, false, Error);
			if (!Lower) { Test.AddError(Error.ToString()); return false; }
			FinishAnimations();
			if (Scene == 2)
			{
				Capture(TEXT("Shield-L5-closed"), Board->GetGridWorldLocation(Center) + FVector(0, 0, 0), FVector(980, 1250, 870));
			}
			if (bRotatedRooms)
			{
				// 沿棋盘 +Y 放独立房，真正触发 L3V 的 90° 格局。Extra 保持从上到下的释放顺序。
				ACastleBuildingBase* SideLower = Board->PlaceBuilding(Side, LowerCard, false, Error);
				if (!SideLower) { Test.AddError(Error.ToString()); return false; }
				Extra.Add(SideLower);
				FinishAnimations();
				ACastleBuildingBase* SideUpper = Board->PlaceBuilding(Side + Up, UpperCard, false, Error);
				if (!SideUpper) { Test.AddError(Error.ToString()); return false; }
				Extra.Insert(SideUpper, 0);
				FinishAnimations();
			}
			if (bClosed)
			{
				// 上楼从同号侧楼横向落位，下方接触层的颜色与数字均不同。
				ACastleBuildingBase* Base = Board->PlaceBuilding(Side, UpperCard, false, Error);
				if (!Base) { Test.AddError(Error.ToString()); return false; }
				Extra.Add(Base);
				FinishAnimations();
				Support = Board->PlaceBuilding(Side + Up, UpperCard, false, Error);
				if (!Support) { Test.AddError(Error.ToString()); return false; }
				FinishAnimations();
				FCastleGridCursorTarget Cursor;
				Cursor.bValid = true;
				Cursor.Surface = ECastleGridCursorSurface::Building;
				Cursor.Cell = Side + Up;
				Cursor.AdjacentCell = Center + Up;
				Cursor.Face = ECastleGridFace::NegativeY;
				Upper = Board->PlaceBuildingAtCursor(Cursor, UpperCard, false, Error);
			}
			else { Upper = Board->PlaceBuilding(Center + Up, UpperCard, false, Error); }
			if (!Upper) { Test.AddError(Error.ToString()); return false; }
			FinishAnimations();
			return true;
		}

		void FinishAnimations()
		{
			// 跑正式完成回调，减少夹具等待。导航构建仍由实际游戏世界推进。
			for (ACastleBuildingBase* Building : Board->GetPlacedBuildings())
			{
				if (Building->AssemblyFlight->bIsPlaying) { Building->AssemblyFlight->TickComponent(60.0f, LEVELTICK_All, nullptr); }
			}
			for (ACastleBuildingBase* Building : Board->GetPlacedBuildings())
			{
				if (Building->BuildingMerge->bIsPlaying) { Building->BuildingMerge->TickComponent(60.0f, LEVELTICK_All, nullptr); }
			}
		}

		void VerifyStack()
		{
			const bool bClosed = Scene == 3 || Scene == 6;
			const FString Label = FString::Printf(TEXT("场景 %d %s / %s"), Scene, *Lower->GetClass()->GetName(), *Upper->GetClass()->GetName());
			TArray<FCastleActiveLadderConnection> Links;
			PCG->GetActiveLadderConnections(Board, Links);
			if (bClosed)
			{
				Test.TestFalse(Label + TEXT(" 上下不匹配时无重合格梯"), Links.ContainsByPredicate([&](const auto& Link) { return Link.LowerCell == Center; }));
				TraceFloor(Center + Up, FVector(120, 120, 0), true);
				if (Scene == 6)
				{
					VerifyStepRoof(true);
					VerifyStepDeck(false);
					VerifySecondUpper();
				}
				return;
			}
			if (!Test.TestEqual(Label + TEXT(" 唯一跨栋梯"), Links.Num(), 1)) { return; }
			Test.TestTrue(Label + TEXT(" 梯由实际下层 Actor 所有"), Links[0].Building == Lower);
			const auto* Link = Lower->FindComponentByClass<UCastleLadderLinkComponent>();
			if (!Test.TestNotNull(Label + TEXT(" 有效梯组件"), Link)) { return; }
			FVector Bottom, Top;
			ENavLinkDirection::Type Direction;
			Link->GetLinkData(Bottom, Top, Direction);
			Bottom = Lower->GetActorTransform().TransformPosition(Bottom);
			Top = Lower->GetActorTransform().TransformPosition(Top);
			Test.TestEqual(Label + TEXT(" 双向 Link"), Direction, ENavLinkDirection::BothWays);
			FText Error;
			Test.TestTrue(Label + TEXT(" Recast 接收两个真实梯口"), Navigation->RequestSharedPatrolRoute({Bottom, Top}, Error));
			const auto Path = Navigation->AcquireSharedPath();
			Test.TestTrue(Label + TEXT(" 上下行路线闭环"), Path && Path->bValid && Path->bClosedLoop);
			Navigation->ClearSharedRoute();
			// 真正提交的梯面顶端位于各族梯井净空内；以实际 Link 核对经过旋转、跨系装配后的开洞。
			FVector Offset = CastleV4PCG::CellFrame(*Board, Links[0].UpperCell).InverseTransformPosition(Links[0].TopWorldLocation);
			Offset.Z = 0.0;
			if (Scene == 2)
			{
				const UCastleHachimisV4Layout* Layout = Lower->HachimisLayout;
				if (!Test.TestNotNull(TEXT("五级盾塔使用 V4 造型"), Layout)) { return; }
				const UCastleV4LadderKit* Kit = Layout->Ladder;
				if (!Test.TestNotNull(TEXT("五级盾塔的作者楼板梯"), Kit)) { return; }
				Test.TestEqual(TEXT("盾塔在下时使用盾塔梯套件，而不是旧通用梯"), Kit->Family, FName(TEXT("Shield")));
				const FCastleV4Part* Shoe = Kit->Parts.FindByPredicate([](const FCastleV4Part& Part)
				{
					return Part.Node.ToString().StartsWith(TEXT("Ladder_Shoe"));
				});
				if (!Test.TestTrue(TEXT("楼板梯中有作者梯脚网格"), Shoe && Shoe->Mesh)) { return; }
				TInlineComponentArray<UStaticMeshComponent*> Meshes(Lower);
				Test.TestTrue(TEXT("跨栋梯实际生成了盾塔梯脚网格"), Meshes.ContainsByPredicate([Shoe](const UStaticMeshComponent* Mesh)
				{
					return Mesh->IsVisible() && Mesh->GetStaticMesh() == Shoe->Mesh;
				}));
				Capture(TEXT("Shield-L5-stacked"), Board->GetGridWorldLocation(Center) + FVector(0, 0, 265), FVector(1250, -1600, 1080));
			}
			if (Scene == 4)
			{
				TraceFloor(Center + FIntVector(0,1,1), Offset, true);
				Capture(TEXT("V4-L3V-rotated-hatch"), Board->GetGridWorldLocation(Center) + FVector(0,257,340), FVector(-1510,-1860,1510));
			}
			TraceFloor(Links[0].UpperCell, Offset, false);
			TraceFloor(Links[0].UpperCell, FVector::ZeroVector, true);
			if (Scene == 5 || Scene == 7 || Scene == 8)
			{
				VerifyStepRoof(true);
				VerifyStepDeck(true);
			}
			if (Scene == 0)
			{
				const FIntVector Other = Links[0].UpperCell == Center + Up ? Center + Up + FIntVector(1,0,0) : Center + Up;
				TraceFloor(Other, Offset, true);
				Capture(TEXT("V4-tower-stack"), Board->GetGridWorldLocation(Center) + FVector(230, 0, 250), FVector(-1550,-2250,1400));
			}
			Test.AddInfo(Label + TEXT(" 梯口与闭环断言完成"));
		}

		/** 原二级屋面由两格的语义共同决定，坡顶留在外露格并朝远离上层的一侧。 */
		void VerifyStepRoof(bool bCovered)
		{
			const UCastleHachimisV4Layout* Layout = Lower->HachimisLayout;
			if (!Test.TestNotNull(TEXT("水平二级功能塔的逐格屋面装配"), Layout)) { return; }
			TArray<FCastlePCGPartAnnotation> Parts;
			TMap<FIntVector, FIntVector> Cells;
			FString Error;
			if (!CastleV4PCG::Extract(*Lower, *Board, Parts, Error) || !CastleV4PCG::MapCells(*Lower, *Board, Cells, Error))
			{
				Test.AddError(Error);
				return;
			}
			int32 RoofParts = 0;
			for (int32 Index = 0; Index < Layout->Parts.Num(); ++Index)
			{
				const FCastleV4Part& Part = Layout->Parts[Index];
				if (Part.Group != TEXT("Roof") && Part.Group != TEXT("RoofKeys")) { continue; }
				++RoofParts;
				Test.TestEqual(Part.Node.ToString() + TEXT(" 原二级屋面在半坡启用时隐藏，拆楼后恢复"),
					Parts[Index].Component->IsVisible(), !bCovered);
			}
			Test.TestTrue(TEXT("核对了非空的原装屋面"), RoofParts > 0);
			const auto* Exposed = Lower->GetOccupiedCoordinates().FindByPredicate([&](const FIntVector& Cell)
			{
				return Cell != Center;
			});
			if (!Test.TestNotNull(TEXT("找到水平二级外露格"), Exposed)) { return; }
			const FTransform Frame = CastleV4PCG::CellFrame(*Board, *Exposed + Up);
			int32 StepParts = 0;
			for (const auto* Part : TInlineComponentArray<UStaticMeshComponent*>(Lower))
			{
				const UStaticMesh* Mesh = Part->GetStaticMesh();
				if (!Part->IsVisible() || !Mesh || !(Mesh->GetName().EndsWith(TEXT("_Slope"))
					|| Mesh->GetName().EndsWith(TEXT("_WallApron")))) { continue; }
				++StepParts;
				const FVector Out = Frame.InverseTransformVectorNoScale(Part->GetComponentTransform().TransformVectorNoScale(FVector::RightVector));
				Test.TestTrue(TEXT("坡顶及墙裙锚于外露格顶；高边朝上层，低檐朝外"),
					Part->GetComponentLocation().Equals(Frame.GetLocation(), 0.1) && Out.Equals(FVector(*Exposed - Center), 0.01));
				Test.TestTrue(TEXT("坡顶可被选中且不参与导航"), Part->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block
					&& !Part->CanEverAffectNavigation());
			}
			Test.TestEqual(TEXT("只在单侧叠放时装配坡顶与墙裙"), StepParts, bCovered ? 2 : 0);
		}

		void VerifyStepDeck(bool bHatch)
		{
			const FString MeshName = FString::Printf(TEXT("SM_HC515_%s_Deck"), *Lower->HachimisLayout->Family.ToString());
			const auto& LadderParts = Lower->HachimisLayout->Ladder->Parts;
			int32 Decks = 0, Floors = 0, ExpectedFloors = 0;
			for (const FCastleV4Part& Part : LadderParts) { ExpectedFloors += Part.Group == TEXT("UpperFloor"); }
			for (const auto* Part : TInlineComponentArray<UStaticMeshComponent*>(Upper))
			{
				const UStaticMesh* Mesh = Part->GetStaticMesh();
				if (!Part->IsVisible() || !Mesh) { continue; }
				if (Mesh->GetName() == MeshName)
				{
					++Decks;
				}
				const bool bFloor = Part->ComponentHasTag(TEXT("CastlePCGGroup=UpperFloor"));
				if (bFloor)
				{
					++Floors;
					Test.TestTrue(TEXT("带孔上层楼板来自下层本族正式梯具"), LadderParts.ContainsByPredicate([&](const FCastleV4Part& Floor)
					{
						return Floor.Group == TEXT("UpperFloor") && Floor.Mesh == Mesh
							&& Part->ComponentHasTag(FName(TEXT("CastlePCGNode=") + Floor.Node.ToString()));
					}));
				}
				if (bFloor || Mesh->GetName() == MeshName)
				{
					// 两种视觉楼板只负责选中，行走面由玩法碰撞提供。
					Test.TestTrue(TEXT("上层宽边板只阻挡选中查询"), Part->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block
						&& Part->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore && !Part->CanEverAffectNavigation());
				}
			}
			Test.TestEqual(TEXT("无梯井时上层才持有一块本族实心宽边板"), Decks, bHatch ? 0 : 1);
			Test.TestEqual(TEXT("有梯井时上层持有全部带孔楼板作者件"), Floors, bHatch ? ExpectedFloors : 0);
			if (Scene == 8)
			{
				// 正式旧版加速房同号叠放。正式带孔楼板已在上方计数，旧原装底板应隐藏。
				Test.TestNull(TEXT("加速房来自旧版装配器"), Upper->HachimisLayout.Get());
				const auto Original = TInlineComponentArray<UStaticMeshComponent*>(Upper);
				const auto* OriginalDeck = Original.FindByPredicate([](const UStaticMeshComponent* Part)
				{
					return Part->GetFName() == TEXT("StaticMesh");
				});
				if (Test.TestTrue(TEXT("找到加速房原装底板"), OriginalDeck != nullptr))
				{
					Test.TestFalse(TEXT("加速房原装底板视觉已隐藏"), (*OriginalDeck)->IsVisible());
				}
				return;
			}
			TArray<FCastlePCGPartAnnotation> Original;
			FString Error;
			if (!CastleV4PCG::Extract(*Upper, *Board, Original, Error)) { Test.AddError(Error); return; }
			const auto& Parts = Upper->HachimisLayout->Parts;
			for (int32 Index = 0; Index < Parts.Num(); ++Index)
			{
				if (Parts[Index].Group == TEXT("Foundation"))
				{
					Test.TestFalse(TEXT("上层原装底盘视觉已隐藏"), Original[Index].Component->IsVisible());
				}
			}
		}

		void VerifySecondUpper()
		{
			// 用第一个上层塔的同号侧面延伸到第二格，两个下层接触面均保持封闭。
			const auto* Exposed = Lower->GetOccupiedCoordinates().FindByPredicate([&](const FIntVector& Cell) { return Cell != Center; });
			if (!Exposed) { Test.AddError(TEXT("第二座上层建筑缺少目标格")); return; }
			FCastleBuildingDescriptor Card;
			if (!MakeCard(TEXT("CannonTower"), 1, ECastleTileColor::Blue, 4, Card)) { return; }
			const FIntVector Offset = *Exposed - Center;
			FCastleGridCursorTarget Cursor;
			Cursor.bValid = true;
			Cursor.Surface = ECastleGridCursorSurface::Building;
			Cursor.Cell = Center + Up;
			Cursor.AdjacentCell = *Exposed + Up;
			if (Offset.X > 0) { Cursor.Face = ECastleGridFace::PositiveX; }
			else if (Offset.X < 0) { Cursor.Face = ECastleGridFace::NegativeX; }
			else if (Offset.Y > 0) { Cursor.Face = ECastleGridFace::PositiveY; }
			else { Cursor.Face = ECastleGridFace::NegativeY; }
			FText Error;
			ACastleBuildingBase* Second = Board->PlaceBuildingAtCursor(Cursor, Card, false, Error);
			if (!Second) { Test.AddError(Error.ToString()); return; }
			FinishAnimations();
			const auto HasStepMesh = [](const ACastleBuildingBase* Building, const TCHAR* Suffix)
			{
				for (const auto* Part : TInlineComponentArray<UStaticMeshComponent*>(Building))
				{
					if (Part->IsVisible() && Part->GetStaticMesh() && Part->GetStaticMesh()->GetName().EndsWith(Suffix)) { return true; }
				}
				return false;
			};
			Test.TestFalse(TEXT("第二格被占后外倾坡顶卸下"), HasStepMesh(Lower, TEXT("_Slope")));
			Test.TestFalse(TEXT("第二格被占后第一个上层宽边板卸下"), HasStepMesh(Upper, TEXT("_Deck")));
			if (!Remove(Second)) { Extra.Insert(Second, 0); return; }
			FinishAnimations();
			VerifyStepRoof(true);
			VerifyStepDeck(false);
		}

		void TraceFloor(const FIntVector& Cell, const FVector& Offset, bool bSolid)
		{
			FVector CenterOnFloor = CastleV4PCG::CellFrame(*Board, Cell).TransformPosition(Offset);
			ACastleBuildingBase* Floor = Board->GetBuildingAt(Cell);
			if (!Test.TestNotNull(TEXT("板面所在格的建筑"), Floor)) { return; }
			if (Floor->HachimisLayout) { CenterOnFloor.Z += Floor->HachimisLayout->StandingHeight; }
			FHitResult Hit;
			const bool bHit = World->LineTraceSingleByChannel(Hit, CenterOnFloor + FVector(0,0,12), CenterOnFloor - FVector(0,0,12), ECC_Pawn);
			Test.TestEqual(FString::Printf(TEXT("格 %s 局部 %s 的上层 Pawn 板面"), *Cell.ToString(), *Offset.ToCompactString()), bHit, bSolid);
			if (bSolid && bHit) { Test.TestEqual(TEXT("功能楼板归属命中格的 Actor"), Hit.GetActor(), static_cast<AActor*>(Floor)); }
		}

		void Capture(const TCHAR* Name, const FVector& Focus, const FVector& Offset)
		{
			if (!FParse::Param(FCommandLine::Get(), TEXT("HachimisV4Capture"))) { return; }
			const FVector Eye = Focus + Offset * 1.2;
			const FRotator ViewRotation = (Focus - Eye).Rotation();
			auto* Actor = World->SpawnActor<ASceneCapture2D>(Eye, ViewRotation);
			if (!Test.TestNotNull(TEXT("创建 UE 场景捕获"), Actor)) { return; }
			// 截图补光只存在于本次场景捕获，便于在屋檐遮挡下核对铺板和木构细节。
			auto* Fill = World->SpawnActor<ADirectionalLight>(Eye, ViewRotation);
			if (!Test.TestNotNull(TEXT("创建测试截图补光"), Fill)) { Actor->Destroy(); return; }
			ULightComponent* Light = Fill->GetLightComponent();
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetIntensity(3.0f);
			Light->SetCastShadows(false);
			USceneCaptureComponent2D* Camera = Actor->GetCaptureComponent2D();
			Camera->bCaptureEveryFrame = false;
			Camera->bCaptureOnMovement = false;
			Camera->CaptureSource = SCS_FinalColorLDR;
			Camera->FOVAngle = 45.0f;
			Camera->TextureTarget = NewObject<UTextureRenderTarget2D>(Actor);
			Camera->TextureTarget->RenderTargetFormat = RTF_RGBA8;
			Camera->TextureTarget->InitAutoFormat(1280, 900);
			Camera->CaptureScene();
			const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("HachimisV4"));
			const FString File = FString(Name) + TEXT(".png");
			UKismetRenderingLibrary::ExportRenderTarget(World, Camera->TextureTarget, Folder, File);
			Test.TestTrue(TEXT("UE 场景捕获文件已写入"), FPaths::FileExists(Folder / File));
			Test.AddInfo(Folder / File);
			Actor->Destroy();
			Fill->Destroy();
		}

		void CaptureRoomsAndPreviews()
		{
			// 从正式卡片入口核对每族和四个单格五级的预览。房间截图来自七座已完成 PCG 的原 Actor。
			const TCHAR* Families[] = {TEXT("NormalRoom"), TEXT("ArcherTower"), TEXT("CannonTower"), TEXT("Farm"), TEXT("ShieldTower"), TEXT("BalloonTower"), TEXT("BeeHive")};
			auto* Preview = World->GetSubsystem<UCastleBuildingPreviewSubsystem>();
			if (!Test.TestNotNull(TEXT("真实建筑卡片预览系统"), Preview)) { return; }
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Families); ++Index)
			{
				const TArray<int32> Levels = Index >= 1 && Index <= 4 ? TArray<int32>{1, 5} : TArray<int32>{1};
				for (int32 Level : Levels)
				{
					FCastleBuildingDescriptor Card;
					if (!MakeCard(Families[Index], Level, ECastleTileColor::Red, 3, Card)) { return; }
					FText Error;
					if (!Test.TestNotNull(FString(Families[Index]) + FString::Printf(TEXT(" Lv%d 卡片预览纹理"), Level), Preview->GetPreview(Card, Error)))
					{
						Test.AddError(Error.ToString());
						return;
					}
				}
			}
			FCastleBuildingDescriptor Room;
			if (!MakeCard(TEXT("NormalRoom"), 1, ECastleTileColor::Red, 3, Room)) { return; }
			FCastleGroundGenerationResult Ground = OriginalGround;
			for (FCastleGroundCell& Cell : Ground.Cells)
			{
				const FIntVector Delta = Cell.Coordinate - Center;
				if (Delta.X >= 0 && Delta.X <= 1 && Delta.Y >= 0 && Delta.Y <= 1) { Cell.Color = Room.Color; Cell.Number = Room.Number; }
			}
			FText Error;
			if (!Board->ApplyGroundGenerationResult(Ground, Error)) { Test.AddError(Error.ToString()); return; }
			for (int32 Z = 0; Z < 2; ++Z)
			for (int32 X = 0; X < 2; ++X)
			for (int32 Y = 0; Y < 2; ++Y)
			{
				if (X == 0 && Y == 0 && Z == 1) { continue; }
				ACastleBuildingBase* Building = Board->PlaceBuilding(Center + FIntVector(X,Y,Z), Room, false, Error);
				if (!Building) { Test.AddError(Error.ToString()); return; }
				Extra.Add(Building);
				FinishAnimations();
			}
			Capture(TEXT("V4-seven-independent-rooms"), Board->GetGridWorldLocation(Center) + FVector(257,257,330), FVector(-1400,-1850,1550));
		}

		bool Remove(ACastleBuildingBase*& Building)
		{
			if (!IsValid(Building)) { return true; }
			FText Error;
			if (!Board->RemoveBuilding(Building, Error)) { Test.AddError(Error.ToString()); return false; }
			Building = nullptr;
			return true;
		}

		void Cleanup()
		{
			if (!Board || !World) { return; }
			Navigation->ClearSharedRoute();
			Remove(Upper);
			Remove(Support);
			Remove(Lower);
			for (int32 Index = Extra.Num() - 1; Index >= 0; --Index) { Remove(Extra[Index]); }
			FinishAnimations();
			FText Error;
			if (!Board->ApplyGroundGenerationResult(OriginalGround, Error)) { Test.AddError(Error.ToString()); }
		}

		FAutomationTestBase& Test;
		double Deadline = FPlatformTime::Seconds() + 150.0;
		int32 Scene = 0;
		int32 Stage = 0;
		UWorld* World = nullptr;
		ACastleBoard* Board = nullptr;
		UCastleNavigationSubsystem* Navigation = nullptr;
		UCastleBuildingPCGSubsystem* PCG = nullptr;
		FCastleGroundGenerationResult OriginalGround;
		FIntVector Center = FIntVector::ZeroValue;
		ACastleBuildingBase* Lower = nullptr;
		ACastleBuildingBase* Upper = nullptr;
		ACastleBuildingBase* Support = nullptr;
		TArray<ACastleBuildingBase*> Extra;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleHachimisV4StackInterfaces, "MovingCastle.Castle.PCG.HachimisV4.StackInterfaces",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCastleHachimisV4StackInterfaces::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(CastleV4NavigationTests::FStackCases(*this));
	return true;
}
#endif
