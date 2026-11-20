#include "HachimisV4BindCommandlet.h"

#if WITH_EDITOR
#include "CastleHachimisV4Layout.h"
#include "CastleBuildingBase.h"
#include "Castle/PCG/CastleBuildingPCGRule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/Animation/Matching/BuildingPartCatalog.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogHachimisV4Bind, Log, All);

namespace HachimisV4Bind
{
	constexpr TCHAR ContentRoot[] = TEXT("/Game/MovingCastle/Model/Buildings/HachimisV4");

	/** 绑定前保留原蓝图、目标装配和功能楼板的精确节点。 */
	struct FBinding
	{
		UBlueprint* Blueprint = nullptr;
		UCastleHachimisV4Layout* Layout = nullptr;
		USCS_Node* VisualGroup = nullptr;
		TArray<USCS_Node*> OldParts;
		TMap<FIntVector, USCS_Node*> Floors;
		TArray<USCS_Node*> OldFloorRemainders;
	};

	bool Error(const UObject& Object, const FString& Reason)
	{
		UE_LOG(LogHachimisV4Bind, Error, TEXT("%s：%s"), *Object.GetPathName(), *Reason);
		return false;
	}

	bool Save(UObject& Object)
	{
		UPackage* Package = Object.GetOutermost();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		return UPackage::SavePackage(Package, &Object, *File, Args) || Error(Object, TEXT("无法保存资产"));
	}

	bool Inspect(UCastleHachimisV4Layout& Layout, FBinding& Out)
	{
		UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Layout.BlueprintPath);
		const ACastleBuildingBase* CDO = BP && BP->GeneratedClass ? BP->GeneratedClass->GetDefaultObject<ACastleBuildingBase>() : nullptr;
		if (!CDO || !BP->SimpleConstructionScript || !CDO->GameplayRoot)
		{
			return Error(Layout, Layout.BlueprintPath + TEXT(" 不是含 GameplayRoot 的原建筑蓝图"));
		}
		const FIntVector Grid = Layout.Grid;
		const int32 Count = Grid.X * Grid.Y * Grid.Z;
		if (ACastleBuildingBase::GetExpectedFootprintCellCount(CDO->GetBuildingLevel()) != Count)
		{
			return Error(*BP, TEXT("作者占格与原等级类不一致"));
		}
		Out.Blueprint = BP;
		Out.Layout = &Layout;
		for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (Node->GetVariableName() == TEXT("BuildingMeshGroup")) { Out.VisualGroup = Node; }
			if (Node->ParentComponentOrVariableName != CDO->GameplayRoot->GetFName()) { continue; }
			UStaticMeshComponent* Floor = Cast<UStaticMeshComponent>(Node->ComponentTemplate);
			if (!Floor || !Floor->GetStaticMesh() || !Floor->CanEverAffectNavigation()) { continue; }
			if (Floor->ComponentHasTag(TEXT("CastleV4FloorRemainder")))
			{
				Out.OldFloorRemainders.Add(Node);
				continue;
			}
			const FVector P = Floor->GetRelativeLocation();
			const FIntVector Cell(FMath::RoundToInt(P.X / Layout.CellPitch + (Grid.X - 1) * 0.5),
				FMath::RoundToInt(P.Y / Layout.CellPitch + (Grid.Y - 1) * 0.5), FMath::RoundToInt(P.Z / Layout.StoryRise));
			const FVector Expected = Layout.CellBottom(Cell);
			if (Cell.GetMin() < 0 || Cell.X >= Grid.X || Cell.Y >= Grid.Y || Cell.Z >= Grid.Z || Out.Floors.Contains(Cell)
				|| !P.Equals(Expected, 300.0))
			{
				return Error(*BP, Node->GetVariableName().ToString() + TEXT(" 不是唯一占格的原功能板"));
			}
			Out.Floors.Add(Cell, Node);
		}
		if (!Out.VisualGroup || Out.Floors.Num() != Count)
		{
			return Error(*BP, FString::Printf(TEXT("缺少 BuildingMeshGroup 或功能地板：期望 %d，实际 %d"), Count, Out.Floors.Num()));
		}
		for (USCS_Node* Node : Out.VisualGroup->GetChildNodes())
		{
			if (!Cast<UStaticMeshComponent>(Node->ComponentTemplate) || !Node->GetChildNodes().IsEmpty())
			{
				return Error(*BP, Node->GetVariableName().ToString() + TEXT(" 不是独立美术网格节点，需保护其功能挂接"));
			}
			Out.OldParts.Add(Node);
		}
		return true;
	}

	void SetFloor(UStaticMeshComponent& Floor, const FVector& Low, const FVector& High, const FIntVector& Cell, bool bHatch)
	{
		// 原楼板使用 100 cm 立方网格。调整到作者铺面的 40.8 cm，并保留原功能碰撞通道。
		Floor.SetRelativeLocation((Low + High) * 0.5);
		Floor.SetRelativeScale3D((High - Low) / 100.0);
		Floor.ComponentTags.AddUnique(*FString::Printf(TEXT("CastleV4Floor=%d,%d,%d"), Cell.X, Cell.Y, Cell.Z));
		if (bHatch) { Floor.ComponentTags.AddUnique(TEXT("CastleV4FloorRemainder")); }
	}

	bool Bind(const FBinding& Binding)
	{
		UBlueprint& BP = *Binding.Blueprint;
		USimpleConstructionScript& Script = *BP.SimpleConstructionScript;
		UCastleHachimisV4Layout& Layout = *Binding.Layout;
		const ACastleBuildingBase* CDO = BP.GeneratedClass->GetDefaultObject<ACastleBuildingBase>();
		BP.Modify();
		Script.Modify();
		for (USCS_Node* Node : Binding.OldParts) { Script.RemoveNode(Node, false); }
		for (USCS_Node* Node : Binding.OldFloorRemainders) { Script.RemoveNode(Node, false); }
		for (int32 Index = 0; Index < Layout.Parts.Num(); ++Index)
		{
			const FCastleV4Part& Part = Layout.Parts[Index];
			const FName Name(*FString::Printf(TEXT("V4Part_%04d"), Index));
			USCS_Node* Node = Script.CreateNode(UStaticMeshComponent::StaticClass(), Name);
			UStaticMeshComponent* Mesh = CastChecked<UStaticMeshComponent>(Node->ComponentTemplate);
			Mesh->SetStaticMesh(Part.Mesh);
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetRelativeTransform(Part.Local);
			Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
			Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Mesh->SetCanEverAffectNavigation(false);
			Mesh->SetGenerateOverlapEvents(false);
			Mesh->ComponentTags.Add(*FString::Printf(TEXT("CastleV4Part=%d"), Index));
			Mesh->ComponentTags.Add(*FString::Printf(TEXT("SourceNode=%s"), *Part.Node.ToString()));
			Binding.VisualGroup->AddChildNode(Node);
		}
		for (const auto& Pair : Binding.Floors)
		{
			const FIntVector Cell = Pair.Key;
			UStaticMeshComponent* Floor = CastChecked<UStaticMeshComponent>(Pair.Value->ComponentTemplate);
			const FVector Center = Layout.CellBottom(Cell) + FVector(0.0, 0.0, Layout.StandingHeight);
			const FVector Low = Center - FVector(Layout.CellPitch * 0.5, Layout.CellPitch * 0.5, 5.0);
			const FVector High = Center + FVector(Layout.CellPitch * 0.5, Layout.CellPitch * 0.5, 0.0);
			const FBox& Hatch = Layout.InternalHatch.Opening;
			if (Cell.Z == 0 || !Hatch.IsValid || !FBox(Low, High).IntersectXY(Hatch))
			{
				SetFloor(*Floor, Low, High, Cell, false);
				continue;
			}
			if (Hatch.Min.X <= Low.X || Hatch.Max.X >= High.X || Hatch.Min.Y <= Low.Y || Hatch.Max.Y >= High.Y)
			{
				return Error(BP, TEXT("内置梯井没有完整落在一个上层格面内"));
			}
			// 保留原 SCS 地板节点作为左侧条。其余三条沿用相同的功能网格与碰撞配置。
			SetFloor(*Floor, Low, FVector(Hatch.Min.X, High.Y, High.Z), Cell, false);
			const FBox Rectangles[] = {
				FBox(FVector(Hatch.Max.X, Low.Y, Low.Z), High),
				FBox(FVector(Hatch.Min.X, Low.Y, Low.Z), FVector(Hatch.Max.X, Hatch.Min.Y, High.Z)),
				FBox(FVector(Hatch.Min.X, Hatch.Max.Y, Low.Z), FVector(Hatch.Max.X, High.Y, High.Z))};
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Rectangles); ++Index)
			{
				const FName Name(*FString::Printf(TEXT("V4InternalFloor_%d_%d_%d"), Cell.X, Cell.Y, Index));
				USCS_Node* Node = Script.CreateNode(UStaticMeshComponent::StaticClass(), Name);
				UStaticMeshComponent* Segment = CastChecked<UStaticMeshComponent>(Node->ComponentTemplate);
				Segment->SetStaticMesh(Floor->GetStaticMesh());
				Segment->SetMobility(Floor->Mobility);
				Segment->SetVisibility(Floor->IsVisible());
				Segment->SetHiddenInGame(Floor->bHiddenInGame);
				Segment->SetCollisionProfileName(Floor->GetCollisionProfileName());
				Segment->SetCollisionResponseToChannels(Floor->GetCollisionResponseToChannels());
				Segment->SetCollisionEnabled(Floor->GetCollisionEnabled());
				Segment->SetCanEverAffectNavigation(true);
				Segment->SetGenerateOverlapEvents(false);
				SetFloor(*Segment, Rectangles[Index].Min, Rectangles[Index].Max, Cell, true);
				Script.AddNode(Node);
				Node->SetParent(CDO->GameplayRoot);
			}
		}
		FKismetEditorUtilities::CompileBlueprint(&BP, EBlueprintCompileOptions::SkipGarbageCollection);
		ACastleBuildingBase* NewCDO = BP.GeneratedClass->GetDefaultObject<ACastleBuildingBase>();
		if (!NewCDO || BP.Status == BS_Error || Binding.VisualGroup->GetChildNodes().Num() != Layout.Parts.Num())
		{
			return Error(BP, TEXT("重新编译后缺少原类或作者节点"));
		}
		NewCDO->Modify();
		NewCDO->HachimisLayout = &Layout;
		BP.MarkPackageDirty();
		UE_LOG(LogHachimisV4Bind, Display, TEXT("%s：%d 个 V4 独立组件，%d 个占格功能面"),
			*BP.GetPathName(), Layout.Parts.Num(), Binding.Floors.Num());
		return true;
	}

	bool Run(bool bInspect, bool bRulesOnly)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Registry.ScanPathsSynchronous({ContentRoot}, true);
		TArray<FAssetData> Found;
		Registry.GetAssetsByPath(ContentRoot, Found, true);
		TArray<FBinding> Bindings;
		TSet<FString> Paths;
		TMap<FName, TObjectPtr<UCastleHachimisV4Layout>> Examples;
		TArray<UStaticMesh*> Meshes;
		int32 LayoutCount = 0;
		for (const FAssetData& Data : Found)
		{
			if (Data.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName())
			{
				Meshes.Add(Cast<UStaticMesh>(Data.GetAsset()));
			}
			if (Data.AssetClassPath != UCastleHachimisV4Layout::StaticClass()->GetClassPathName()) { continue; }
			UCastleHachimisV4Layout* Layout = Cast<UCastleHachimisV4Layout>(Data.GetAsset());
			if (!Layout || Layout->Parts.IsEmpty()) { return false; }
			++LayoutCount;
			if (Layout->Family == TEXT("Room"))
			{
				if (Examples.Contains(Layout->Form)) { return Error(*Layout, TEXT("普通房样例键重复")); }
				Examples.Add(Layout->Form, Layout);
			}
			if (Layout->Family == TEXT("Room") && Layout->Form != TEXT("L1")) { continue; }
			if (Paths.Contains(Layout->BlueprintPath)) { return Error(*Layout, TEXT("多个装配同时绑定同一蓝图")); }
			Paths.Add(Layout->BlueprintPath);
			if (!Inspect(*Layout, Bindings.AddDefaulted_GetRef())) { return false; }
		}
		if (LayoutCount != 39 || Bindings.Num() != 34 || Examples.Num() != 6)
		{
			UE_LOG(LogHachimisV4Bind, Error, TEXT("%s 装配、蓝图和 Room 资产数为 %d/%d/%d，应为 39/34/6。"),
				ContentRoot, LayoutCount, Bindings.Num(), Examples.Num());
			return false;
		}

		UBuildingPartCatalog* Catalog = LoadObject<UBuildingPartCatalog>(nullptr,
			TEXT("/Game/MovingCastle/Data/Animation/DA_BuildingPartCatalog.DA_BuildingPartCatalog"));
		UCastlePCGRuleSetDataAsset* Rules = LoadObject<UCastlePCGRuleSetDataAsset>(nullptr,
			TEXT("/Game/MovingCastle/Data/DA_CastlePCGRuleSet.DA_CastlePCGRuleSet"));
		FString Failure;
		UCastleV4WallKit* RoomL1Bay = LoadObject<UCastleV4WallKit>(nullptr,
			TEXT("/Game/MovingCastle/Model/Buildings/HachimisV4/Room/Walls/DA_Wall_Room_L1_Bay.DA_Wall_Room_L1_Bay"));
		if (!Rules || !Catalog || !RoomL1Bay || RoomL1Bay->WallKey != TEXT("Room_L1_Bay"))
		{
			UE_LOG(LogHachimisV4Bind, Error, TEXT("DA_CastlePCGRuleSet、DA_BuildingPartCatalog 或 Room_L1_Bay 缺失或无效"));
			return false;
		}
		if (bInspect)
		{
			for (const FBinding& Binding : Bindings)
			{
				const ACastleBuildingBase* CDO = Binding.Blueprint->GeneratedClass->GetDefaultObject<ACastleBuildingBase>();
				if (CDO->HachimisLayout != Binding.Layout || Binding.OldParts.Num() != Binding.Layout->Parts.Num()
					|| Rules->RoomL1Bay != RoomL1Bay || Rules->RoomExamples.Num() != 6)
				{
					return Error(*Binding.Blueprint, TEXT("重新加载后的装配引用或蓝图节点数不同"));
				}
			}
			for (UStaticMesh* Mesh : Meshes)
			{
				const FStaticMeshRenderData* Render = Mesh ? Mesh->GetRenderData() : nullptr;
				if (!Mesh || !Render || Render->LODResources.IsEmpty() || Render->LODResources[0].GetNumTriangles() == 0
					|| Render->Bounds.ContainsNaN() || Mesh->GetBounds().ContainsNaN() || Mesh->GetBounds().BoxExtent.IsNearlyZero()
					|| !Catalog->FindGeometry(Mesh))
				{
					UE_LOG(LogHachimisV4Bind, Error, TEXT("%s 重载后缺少几何目录或有效包围盒"), *GetPathNameSafe(Mesh));
					return false;
				}
			}
			UE_LOG(LogHachimisV4Bind, Display, TEXT("重新加载确认 39 个装配、34 个蓝图和 %d 个目录网格"), Meshes.Num());
			return true;
		}
		if (!bRulesOnly)
		{
			if (!Catalog->AppendExactGeometry(Meshes, Failure)) { return Error(*Catalog, Failure); }
			for (const FBinding& Binding : Bindings) { if (!Bind(Binding)) { return false; } }
		}
		Rules->Modify();
		Rules->RoomL1Bay = RoomL1Bay;
		Rules->RoomExamples = MoveTemp(Examples);
		if (!Save(*Rules)) { return false; }
		if (bRulesOnly) { return true; }
		if (!Save(*Catalog)) { return false; }
		for (const FBinding& Binding : Bindings) { if (!Save(*Binding.Blueprint)) { return false; } }
		return true;
	}
}
#endif

int32 UHachimisV4BindCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	return HachimisV4Bind::Run(FParse::Param(*Params, TEXT("Verify")), FParse::Param(*Params, TEXT("RulesOnly"))) ? 0 : 1;
#else
	return 1;
#endif
}
