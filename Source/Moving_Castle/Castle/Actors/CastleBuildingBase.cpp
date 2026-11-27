// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingBase.h"
#include "Castle/Appearance/CastleBuildingColors.h"
#include "Castle/Items/CastleBadgeSubsystem.h"
#include "Castle/Items/CastleItemTypes.h"
#include "Castle/Items/CastleRoomTraitComponent.h"

#include "CastleBuildingDefinition.h"
#include "CastleBoard.h"
#include "Castle/PCG/CastleBuildingPCGComponent.h"
#include "BuildingMergeComponent.h"
#include "AssemblyFlightComponent.h"
#include "Castle/PCG/HachimisV4/CastleHachimisV4Layout.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/DecalComponent.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "SceneInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Units/BuildingInteraction/CastleBuildingTriggerSubsystem.h"

int64 ACastleBuildingBase::GetEffectiveBuildingNumber() const
{
	// 实时读取已持有的徽章，替换徽章时现有建筑立即按新数字结算。
	const UCastleBadgeSubsystem* Badges = UCastleBadgeSubsystem::GetForWorld(GetWorld());
	return Color == ECastleTileColor::Blue && Badges ? Number + Badges->GetBlueRoomNumberBonus() : Number;
}

ACastleBuildingBase::ACastleBuildingBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BuildingEffect = CreateDefaultSubobject<UCastleBuildingEffectComponent>(TEXT("BuildingEffect"));
	BuildingPCG = CreateDefaultSubobject<UCastleBuildingPCGComponent>(TEXT("BuildingPCG"));
	// 建筑逻辑和模型组件现在位于同一个 Actor；VisualRoot 只提供统一的组件根节点。
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	SetRootComponent(VisualRoot);
	// 功能分支随整座建筑移动，但不承担美术零件；子组件可继续按楼层等需求嵌套分组。
	GameplayRoot = CreateDefaultSubobject<UBuildingGameplayRootComponent>(TEXT("GameplayRoot"));
	GameplayRoot->SetupAttachment(VisualRoot);
	BuildingMerge = CreateDefaultSubobject<UBuildingMergeComponent>(TEXT("BuildingMerge"));
	AssemblyFlight = CreateDefaultSubobject<UAssemblyFlightComponent>(TEXT("AssemblyFlight"));
	// 所有建筑共享原有连续飞入实现；只有棋盘放置流程有权起播，避免预览/升级模型误播。
	AssemblyFlight->bAutoPlay = false;
	AssemblyFlight->ExcludedComponentTag = TEXT("MergeExclude");

	// 引擎基础资源只用于数字底牌；建筑美术必须由等级蓝图显式配置。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FloorMaterial(TEXT("/Game/ART/Material/MI_Floor.MI_Floor"));
	LabelPlateMesh = Cube.Object;
	LabelPlateMaterial = Material.Object;
	FloorDecalMaterial = FloorMaterial.Object;

	// 标签只在 ConfigureBuildingVisual 成功后显示，避免生成中间状态被玩家看见。
	PrototypeLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("PrototypeLabel"));
	PrototypeLabel->SetupAttachment(VisualRoot);
	PrototypeLabel->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	PrototypeLabel->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	PrototypeLabel->SetTextRenderColor(FColor::White);
	PrototypeLabel->SetWorldSize(52.0f);
	PrototypeLabel->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	PrototypeLabel->SetVisibility(false);

	PrototypeLabelPlate = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrototypeLabelPlate"));
	// 数字牌是界面辅助物，合并时随旧 Actor 消失，不把它误当成屋顶零件卷入建筑。
	PrototypeLabelPlate->ComponentTags.Add(TEXT("MergeExclude"));
	PrototypeLabelPlate->SetupAttachment(VisualRoot);
	// 标签底牌不参与碰撞或导航；先设置资格，再赋网格，避免创建期导航更新。
	PrototypeLabelPlate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PrototypeLabelPlate->SetCanEverAffectNavigation(false);
	PrototypeLabelPlate->SetStaticMesh(LabelPlateMesh);
	PrototypeLabelPlate->SetMaterial(0, LabelPlateMaterial);
	PrototypeLabelPlate->SetVisibility(false);
}

bool ACastleBuildingBase::TriggerBuildingEffect(const FCastleBuildingTriggerContext& Context)
{
	if (!BuildingEffect)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑 %s 缺少原生效果组件。"), *GetName());
		return false;
	}
	if (!BuildingEffect->Trigger(Context)) { return false; }
	// 仅真实注册在棋盘里的自身触发参与弹药库联动；独立测试/预览不属于棋盘事件集合。
	// 联动炮塔直接调用自己的组件入口，因此不会回到这里再次传播，互为弹药库也只有一跳。
	if (ACastleBoard* Board = ACastleBoard::FindBoardForBuilding(this)) { Board->OnBuildingTriggered.Broadcast(this, Context); }
	return true;
}

void ACastleBuildingBase::SetPlacementComplete(const bool bComplete)
{
	// 先核对已经发布的整组贴花，再改变建筑状态，避免同一栋楼只有部分占格切换可见性。
	for (int32 Index = 0; Index < FloorDecals.Num(); ++Index)
	{
		if (!IsValid(FloorDecals[Index]) || !FloorDecals[Index]->IsRegistered())
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建筑 %s 的地板贴花 [%d] 已失效或未注册，无法切换放置状态。"),
				*GetPathName(), Index);
			ensureMsgf(false, TEXT("建筑 %s 的地板贴花 [%d] 无效。"), *GetPathName(), Index);
			return;
		}
	}
	if (bHasCompletedPlacement != bComplete)
	{
		if (auto* Triggers = GetWorld()->GetSubsystem<UCastleBuildingTriggerSubsystem>()) { Triggers->NotifyBuildingStateChanged(this); }
	}
	bHasCompletedPlacement = bComplete;
	PrototypeLabel->SetVisibility(bComplete, false);
	PrototypeLabelPlate->SetVisibility(bComplete, false);
	if (RoomTraitComponent)
	{
		RoomTraitComponent->SetPortalAvailable(CanReceiveBuildingTrigger());
	}
	for (UDecalComponent* Decal : FloorDecals)
	{
		Decal->SetVisibility(bComplete, false);
	}
}

void ACastleBuildingBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (auto* Triggers = GetWorld()->GetSubsystem<UCastleBuildingTriggerSubsystem>()) { Triggers->UnregisterBuilding(this); }
	Super::EndPlay(EndPlayReason);
}

bool ACastleBuildingBase::InitializeBuilding(const FCastleBuildingDescriptor& Descriptor, const TArray<FIntVector>& InOccupiedCoordinates)
{
	FText Reason;
	if (!Descriptor.Validate(Reason) || !CastleRoomCards::ValidateTrait(Descriptor, Reason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑 %s 初始化定义 %s 失败：%s"),
			*GetPathName(), *GetPathNameSafe(Descriptor.Definition.Get()), *Reason.ToString());
		return false;
	}
	if (Descriptor.Level != BuildingLevel || Descriptor.GetBuildingType() != BuildingType)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：Data Asset 的生成类、类别或等级与实际 Actor 不一致。"));
		return false;
	}
	if (InOccupiedCoordinates.Num() != GetExpectedFootprintCellCount(BuildingLevel))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：%d 级需要 %d 格，实际收到 %d 格。"),
			BuildingLevel, GetExpectedFootprintCellCount(BuildingLevel), InOccupiedCoordinates.Num());
		return false;
	}
	TSet<FIntVector> UniqueCoordinates;
	for (const FIntVector& Coordinate : InOccupiedCoordinates)
	{
		if (UniqueCoordinates.Contains(Coordinate))
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：占用坐标重复。"));
			return false;
		}
		UniqueCoordinates.Add(Coordinate);
	}
	FIntVector Minimum = InOccupiedCoordinates[0];
	FIntVector Maximum = InOccupiedCoordinates[0];
	for (const FIntVector& Coordinate : InOccupiedCoordinates)
	{
		Minimum = Minimum.ComponentMin(Coordinate);
		Maximum = Maximum.ComponentMax(Coordinate);
	}
	const FIntVector Dimensions = Maximum - Minimum + FIntVector(1, 1, 1);
	const TSubclassOf<ACastleBuildingBase> ExpectedClass = Descriptor.Definition->ResolveBuildingClassForDimensions(BuildingLevel, Dimensions);
	if (ExpectedClass == nullptr || ExpectedClass != GetClass())
	{
		UE_LOG(LogMoving_Castle, Error,
			TEXT("建筑初始化失败：定义 %s 的等级 %d 与占格尺寸 (%d, %d, %d) 不匹配实际类 %s。"),
			*Descriptor.Definition->DisplayName.ToString(), BuildingLevel, Dimensions.X, Dimensions.Y, Dimensions.Z,
			*GetClass()->GetPathName());
		return false;
	}

	// 保存资产身份，后续升级、回收和再次放置都不重新推断建筑种类。
	BuildingDefinition = Descriptor.Definition;
	Color = Descriptor.Color;
	Number = Descriptor.Number;
	OccupiedCoordinates = InOccupiedCoordinates;
	RoomTrait = Descriptor.RoomTrait;
	TraitRemainingRounds = Descriptor.TraitRemainingRounds;
	UpdateTraitLabel(false);
	return true;
}

FCastleBuildingDescriptor ACastleBuildingBase::GetBuildingDescriptor() const
{
	FCastleBuildingDescriptor Descriptor;
	Descriptor.Definition = BuildingDefinition;
	Descriptor.Level = BuildingLevel;
	Descriptor.Color = Color;
	Descriptor.Number = Number;
	Descriptor.RoomTrait = RoomTrait;
	Descriptor.TraitRemainingRounds = TraitRemainingRounds;
	return Descriptor;
}

void ACastleBuildingBase::UpdateTraitLabel(const bool bHighwayActive)
{
	// 数字标签使用引擎默认字库，以 ASCII 显示当前可用的道路或房间传送门。
	const TCHAR* Suffix = RoomTrait == ECastleRoomTrait::SkyIslandPortal ? TEXT("\nPORTAL")
		: (RoomTrait == ECastleRoomTrait::Highway && bHighwayActive ? TEXT("\nHIGHWAY") : TEXT(""));
	PrototypeLabel->SetText(FText::FromString(FString::Printf(TEXT("%d\nLv%d%s"), Number, BuildingLevel, Suffix)));
}

void ACastleBuildingBase::ConsumeSkyIslandPortal()
{
	check(RoomTrait == ECastleRoomTrait::SkyIslandPortal && IsValid(RoomTraitComponent));
	// 先清除卡牌身份。离岛回收、拆回背包和其他观察者现在都只会读到普通房间。
	RoomTrait = ECastleRoomTrait::None;
	RoomTraitComponent->DestroyComponent();
	RoomTraitComponent = nullptr;
	UpdateTraitLabel(false);
}

TSubclassOf<ACastleBuildingBase> ACastleBuildingBase::GetClassForLevel(int32 InLevel) const
{
	const FCastleBuildingLevelDefinition* Entry = IsValid(BuildingDefinition) ? BuildingDefinition->FindLevel(InLevel) : nullptr;
	return Entry != nullptr ? Entry->BuildingClass : nullptr;
}

bool ACastleBuildingBase::CanMergeToNextLevel() const
{
	// 保留策划案的四级自动合成上限；五级仍供后续奖励系统直接升级。
	return BuildingType != ECastleBuildingType::NormalRoom && BuildingLevel >= 1 && BuildingLevel < 4
		&& GetClassForLevel(BuildingLevel + 1) != nullptr;
}

bool ACastleBuildingBase::CanMergeWith(const ACastleBuildingBase* Other) const
{
	// 相同类别但引用不同 Data Asset 的建筑仍是不同建筑，不能误合成为另一种模型。
	return Other != nullptr && Other != this && CanMergeToNextLevel()
		&& BuildingDefinition == Other->BuildingDefinition && BuildingLevel == Other->BuildingLevel
		&& Color == Other->Color && Number == Other->Number;
}

int32 ACastleBuildingBase::GetExpectedFootprintCellCount(int32 InLevel)
{
	// 等级 1..4 对应 1/2/4/8 格；五级由奖励生成，沿用既有的单格规则。
	return InLevel < 1 || InLevel > 5 ? 0 : (InLevel == 5 ? 1 : (1 << (InLevel - 1)));
}

bool ACastleBuildingBase::ConfigureBuildingVisual(float InCellSize, float InLayerHeight, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	if (OccupiedCoordinates.IsEmpty() || InCellSize <= 0.0f || InLayerHeight <= 0.0f)
	{
		OutFailureReason = FText::FromString(TEXT("建筑尚未初始化占用坐标，或棋盘尺寸无效，无法创建模型。"));
		return false;
	}

	FIntVector Minimum = OccupiedCoordinates[0];
	FIntVector Maximum = OccupiedCoordinates[0];
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		Minimum = Minimum.ComponentMin(Coordinate);
		Maximum = Maximum.ComponentMax(Coordinate);
	}
	const FIntVector Dimensions = Maximum - Minimum + FIntVector(1, 1, 1);

	// 只有美术分支中的可见网格才算建筑模型；功能楼板和标签不能掩盖模型漏配。
	// 失败时标签保持隐藏，棋盘会销毁未提交的 Actor，并保留原有卡牌与占用。
	PrototypeLabel->SetVisibility(false);
	PrototypeLabelPlate->SetVisibility(false);

	TArray<UStaticMeshComponent*> StaticMeshes;
	GetComponents<UStaticMeshComponent>(StaticMeshes);
	bool bHasConfiguredModel = false;
	for (UStaticMeshComponent* Component : StaticMeshes)
	{
		if (Component == PrototypeLabelPlate
			|| UBuildingGameplayRootComponent::IsInGameplayHierarchy(Component))
		{
			continue;
		}
		if (Component->GetStaticMesh() != nullptr && Component->IsVisible())
		{
			bHasConfiguredModel = true;
			break;
		}
	}

	if (!bHasConfiguredModel)
	{
		OutFailureReason = FText::FromString(FString::Printf(
			TEXT("建筑类 %s 缺少可见美术模型：请在 VisualRoot 下配置 StaticMeshComponent；GameplayRoot 下的功能网格不算美术模型。"),
			*GetClass()->GetPathName()));
		return false;
	}

	if (HachimisLayout)
	{
		// 蓝图原件的索引由导入器写入。场外摄影也从正式布局读节点与分组，因此不依赖棋盘 PCG。
		TBitArray<> Seen(false, HachimisLayout->Parts.Num());
		for (UStaticMeshComponent* Component : StaticMeshes)
		{
			if (Component->CreationMethod == EComponentCreationMethod::SimpleConstructionScript)
			{
				for (FName Tag : Component->ComponentTags)
				{
					FString IndexText = Tag.ToString();
					if (IndexText.RemoveFromStart(TEXT("CastleV4Part=")))
					{
						int32 Index;
						if (!LexTryParseString(Index, *IndexText) || !HachimisLayout->Parts.IsValidIndex(Index) || Seen[Index]
							|| !Component->GetStaticMesh() || Component->GetStaticMesh() != HachimisLayout->Parts[Index].Mesh)
						{
							OutFailureReason = FText::FromString(Component->GetPathName() + TEXT(" 配色索引或源网格与布局不符。"));
							UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
							return false;
						}
						Seen[Index] = true;
						const auto& Part = HachimisLayout->Parts[Index];
						CastleBuildingColors::Apply(*Component, Color, BuildingType, Part.Node, Part.Group);
					}
				}
			}
		}
		if (Seen.Contains(false))
		{
			OutFailureReason = FText::FromString(HachimisLayout->GetPathName() + TEXT(" 缺少作者组件 ") + HachimisLayout->Parts[Seen.Find(false)].Node.ToString());
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
			return false;
		}
		// V4 的瓦、板、榫等独立节点密度更高，仍在各等级原飞入窗口内依次发射。
		// 窗口按原造型节点数和既有每件间隔计算，飞行时长及完成回调仍由原组件负责。
		static constexpr int32 LegacyVisualCounts[] = {9, 18, 29, 71, 71};
		if (BuildingLevel < 1 || BuildingLevel > UE_ARRAY_COUNT(LegacyVisualCounts) || HachimisLayout->Parts.Num() <= 1)
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("V4 造型 %s 不能计算原等级的飞入窗口。"), *HachimisLayout->GetPathName()));
			UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
			return false;
		}
		const ACastleBuildingBase* Template = GetClass()->GetDefaultObject<ACastleBuildingBase>();
		AssemblyFlight->LaunchInterval = Template->AssemblyFlight->LaunchInterval * (LegacyVisualCounts[BuildingLevel - 1] - 1)
			/ (HachimisLayout->Parts.Num() - 1);
	}

	// 已启用的美术碰撞只提供 Visibility 查询；作者明确关闭碰撞的纯表现组件保持无碰撞。
	// 功能分支完整保留作者配置，不能按可见性关闭隐藏地板碰撞。
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		// 摄影只使用直接照明，预览组件无需参与关卡阴影、间接光或光追加速结构。
		Primitive->SetCastShadow(false);
		Primitive->SetAffectDynamicIndirectLighting(false);
		Primitive->SetVisibleInRayTracing(false);
		if (UBuildingGameplayRootComponent::IsInGameplayHierarchy(Primitive))
		{
			continue;
		}
		Primitive->SetSimulatePhysics(false);
		const bool bNeedsSelectionQuery = Primitive->IsVisible()
			&& Primitive->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
		Primitive->SetCollisionEnabled(bNeedsSelectionQuery ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		Primitive->SetCollisionResponseToAllChannels(ECR_Ignore);
		Primitive->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Primitive->SetCanEverAffectNavigation(false);
	}

	const FBox Bounds = GetVisualBounds();
	if (!Bounds.IsValid)
	{
		OutFailureReason = FText::FromString(TEXT("建筑没有可见几何体，无法显示或选中。"));
		return false;
	}
	if (RoomTrait == ECastleRoomTrait::Highway || RoomTrait == ECastleRoomTrait::SkyIslandPortal)
	{
		if (!IsValid(GameplayRoot))
		{
			OutFailureReason = INVTEXT("房间特质需要有效 GameplayRoot，无法配置功能组件。");
			UE_LOG(LogMoving_Castle, Error, TEXT("房间 %s：%s"), *GetPathName(), *OutFailureReason.ToString());
			return false;
		}
		if (!RoomTraitComponent)
		{
			auto* Component = NewObject<UCastleRoomTraitComponent>(this);
			if (!Component->TryInitializeTrait(GetBuildingDescriptor(), OutFailureReason)) { return false; }
			Component->SetupAttachment(GameplayRoot);
			AddInstanceComponent(Component);
			Component->RegisterComponent();
			RoomTraitComponent = Component;
		}
		if (RoomTrait == ECastleRoomTrait::SkyIslandPortal)
		{
			RoomTraitComponent->ConfigurePortalTransform(InCellSize, InLayerHeight);
		}
	}
	const FBox LocalBounds = Bounds.TransformBy(GetActorTransform().ToInverseMatrixWithScale());
	const float PlateHeight = LocalBounds.Max.Z + 8.0f;
	const float Width = FMath::Min(Dimensions.X * InCellSize * 0.72f, 300.0f);
	const float Depth = FMath::Min(Dimensions.Y * InCellSize * 0.72f, 180.0f);
	PrototypeLabelPlate->SetRelativeLocation(FVector(0.0f, 0.0f, PlateHeight));
	PrototypeLabelPlate->SetRelativeScale3D(FVector(Width / 100.0f, Depth / 100.0f, 0.035f));
	UMaterialInstanceDynamic* PlateMaterial = UMaterialInstanceDynamic::Create(LabelPlateMaterial, this);
	if (PlateMaterial == nullptr)
	{
		OutFailureReason = FText::FromString(TEXT("建筑标签材质创建失败。"));
		return false;
	}
	PlateMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.015f, 0.015f, 0.02f));
	PrototypeLabelPlate->SetMaterial(0, PlateMaterial);
	PrototypeLabel->SetRelativeLocation(FVector(0.0f, 0.0f, PlateHeight + 2.5f));
	const FColor LabelColors[] = { FColor::Silver, FColor::Red, FColor::Cyan, FColor::Green, FColor::Yellow, FColor::Magenta };
	PrototypeLabel->SetTextRenderColor(LabelColors[static_cast<int32>(Color)]);
	PrototypeLabel->SetVisibility(true);
	PrototypeLabelPlate->SetVisibility(true);
	return true;
}

bool ACastleBuildingBase::ConfigureFloorDecals(const ACastleBoard& Board, FText& OutFailureReason)
{
	OutFailureReason = FText::GetEmpty();
	TArray<UDecalComponent*> PendingDecals;
	const auto Reject = [this, &OutFailureReason, &PendingDecals](const FString& Cause)
	{
		OutFailureReason = FText::FromString(FString::Printf(TEXT("建筑 %s 地板贴花初始化失败：%s"), *GetPathName(), *Cause));
		UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *OutFailureReason.ToString());
		for (UDecalComponent* Decal : PendingDecals)
		{
			// 数组只在组件创建成功后写入；注册失败时也要一并撤销未注册的组件。
			Decal->DestroyComponent();
		}
		return false;
	};

	if (!IsValid(FloorDecalMaterial) || !IsValid(GameplayRoot) || OccupiedCoordinates.IsEmpty()
		|| !FloorDecals.IsEmpty() || !FMath::IsFinite(Board.GroundTileSize) || Board.GroundTileSize <= 0.0f
		|| !FMath::IsFinite(Board.GroundTileGap) || Board.GroundTileGap < 0.0f
		|| !FMath::IsFinite(Board.CellSize) || Board.CellSize <= 0.0f
		|| !FMath::IsFinite(Board.LayerHeight) || Board.LayerHeight <= 0.0f
		|| Board.GetActorTransform().ContainsNaN())
	{
		return Reject(FString::Printf(TEXT("楼板材质 /Game/ART/Material/MI_Floor、功能根节点、占格或棋盘尺寸无效。材质=%s。"),
			*GetPathNameSafe(FloorDecalMaterial)));
	}

	FLinearColor DecalTint;
	if (!CastleTileColors::TryGetGridTint(Color, DecalTint) || Number < 1 || Number > 9)
	{
		return Reject(FString::Printf(TEXT("卡牌属性无效：颜色 %d、数字 %d，无法设置材质颜色和图集帧。"),
			static_cast<int32>(Color), Number));
	}

	const bool bHasV4Layout = IsValid(HachimisLayout);
	// V4 楼板使用布局记录的作者站立高度；无 V4 布局的旧功能楼板契约位于格底，保持原有标高。
	const double StandingHeight = bHasV4Layout ? HachimisLayout->StandingHeight : 0.0;
	if (bHasV4Layout && (!FMath::IsFinite(StandingHeight) || StandingHeight <= 0.0 || StandingHeight >= Board.LayerHeight))
	{
		return Reject(FString::Printf(TEXT("V4 布局 %s 的作者站立高度 %.3f 超出楼层范围。"),
			*HachimisLayout->GetPathName(), StandingHeight));
	}

	const FTransform& BoardTransform = Board.GetActorTransform();
	// Decal 局部 +X 轴朝向楼板，投影深度保持短距离，避免纹理落到楼板附近的墙面。
	const FQuat DecalRotation = BoardTransform.GetRotation() * FRotator(-90.0f, 0.0f, 0.0f).Quaternion();
	// 俯投后贴花 X 是棋盘高度轴、Z 是棋盘宽度轴，非等比缩放也必须沿对应轴传递。
	const FVector BoardScale = BoardTransform.GetScale3D();
	const FVector DecalWorldScale(BoardScale.Z, BoardScale.Y, BoardScale.X);
	PendingDecals.Reserve(OccupiedCoordinates.Num());
	for (const FIntVector& Coordinate : OccupiedCoordinates)
	{
		// 旧功能楼板按单格边长并仅向同建筑邻格延伸半缝；V4 楼板按作者完整格距铺设。
		const float NegativeXExtension = !bHasV4Layout && OccupiedCoordinates.Contains(Coordinate + FIntVector(-1, 0, 0))
			? Board.GroundTileGap * 0.5f : 0.0f;
		const float PositiveXExtension = !bHasV4Layout && OccupiedCoordinates.Contains(Coordinate + FIntVector(1, 0, 0))
			? Board.GroundTileGap * 0.5f : 0.0f;
		const float NegativeYExtension = !bHasV4Layout && OccupiedCoordinates.Contains(Coordinate + FIntVector(0, -1, 0))
			? Board.GroundTileGap * 0.5f : 0.0f;
		const float PositiveYExtension = !bHasV4Layout && OccupiedCoordinates.Contains(Coordinate + FIntVector(0, 1, 0))
			? Board.GroundTileGap * 0.5f : 0.0f;
		const float FloorWidth = bHasV4Layout ? Board.CellSize
			: Board.GroundTileSize + NegativeXExtension + PositiveXExtension;
		const float FloorDepth = bHasV4Layout ? Board.CellSize
			: Board.GroundTileSize + NegativeYExtension + PositiveYExtension;
		const FVector LocalSurfaceOffset(
			(PositiveXExtension - NegativeXExtension) * 0.5f,
			(PositiveYExtension - NegativeYExtension) * 0.5f,
			-Board.LayerHeight * 0.5f + static_cast<float>(StandingHeight));
		// 格心先落到对应占格的楼板顶面，再应用棋盘完整变换，支持旋转、缩放和竖向占格。
		const FVector WorldLocation = Board.GetGridWorldLocation(Coordinate)
			+ BoardTransform.TransformVector(LocalSurfaceOffset);
		const FTransform DecalWorldTransform(DecalRotation, WorldLocation, DecalWorldScale);
		if (DecalWorldTransform.ContainsNaN() || !FMath::IsFinite(FloorWidth) || FloorWidth <= 0.0f
			|| !FMath::IsFinite(FloorDepth) || FloorDepth <= 0.0f)
		{
			return Reject(FString::Printf(TEXT("占格 %s 的楼板贴花变换或投影尺寸无效。"), *Coordinate.ToString()));
		}

		UDecalComponent* Decal = NewObject<UDecalComponent>(this);
		if (!Decal)
		{
			return Reject(FString::Printf(TEXT("占格 %s 无法创建贴花组件。"), *Coordinate.ToString()));
		}
		PendingDecals.Add(Decal);
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(FloorDecalMaterial, Decal);
		if (!Material)
		{
			return Reject(FString::Printf(TEXT("占格 %s 无法从材质 %s 创建动态实例。"),
				*Coordinate.ToString(), *FloorDecalMaterial->GetPathName()));
		}
		// 3×3 图集帧从零编号，建筑数字 1～9 映射到帧 0～8。
		Material->SetVectorParameterValue(TEXT("Color"), DecalTint);
		Material->SetScalarParameterValue(TEXT("Frame (S)"), static_cast<float>(Number - 1));

		Decal->SetupAttachment(GameplayRoot);
		Decal->SetWorldTransform(DecalWorldTransform);
		Decal->DecalSize = FVector(12.0f, FloorDepth * 0.5f, FloorWidth * 0.5f);
		Decal->SetDecalMaterial(Material);
		Decal->SetCanEverAffectNavigation(false);
		Decal->SetVisibility(bHasCompletedPlacement, false);
		Decal->ComponentTags.Add(TEXT("CastleBuildingFloorDecal"));
	}

	for (UDecalComponent* Decal : PendingDecals)
	{
		AddInstanceComponent(Decal);
		Decal->RegisterComponent();
		if (!Decal->IsRegistered())
		{
			return Reject(FString::Printf(TEXT("占格贴花组件 %s 无法注册。"), *GetNameSafe(Decal)));
		}
	}
	for (UDecalComponent* Decal : PendingDecals) { FloorDecals.Add(Decal); }
	return true;
}

FBox ACastleBuildingBase::GetVisualBounds() const
{
	FBox Result(ForceInit);
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
	for (const UPrimitiveComponent* Primitive : Primitives)
	{
		// 隐藏碰撞即使为了编辑调试临时设为可见，也不能扩大建筑标签高度和卡片取景范围。
		if (UBuildingGameplayRootComponent::IsInGameplayHierarchy(Primitive))
		{
			continue;
		}
		// 标签不属于模型几何体，取景和标签高度都不应把文字自身再次计入范围。
		if (Primitive == PrototypeLabelPlate || Primitive == PrototypeLabel)
		{
			continue;
		}
		if (Primitive->IsRegistered() && Primitive->IsVisible())
		{
			Result += Primitive->Bounds.GetBox();
		}
	}
	return Result;
}

void ACastleBuildingBase::PrepareForPreview()
{
	// 预览虽然使用相同建筑类，但不属于真实放置实例，不能接收任何玩法效果。
	bBuildingPreview = true;
	if (RoomTraitComponent) { RoomTraitComponent->SetPortalAvailable(false); }
	// 卡片只显示建筑形状，等级与数字由卡片自己的文字控件显示。
	PrototypeLabel->SetVisibility(false);
	PrototypeLabelPlate->SetVisibility(false);
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
#if WITH_EDITOR
	TSet<UMaterialInterface*> QueuedMaterials;
	const FSceneInterface* Scene = GetWorld()->Scene;
#endif
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (UBuildingGameplayRootComponent::IsInGameplayHierarchy(Primitive))
		{
			// 预览不展示功能几何，但完整保留功能分支作者配置的碰撞与导航属性。
			Primitive->SetVisibility(false, false);
			continue;
		}
		Primitive->SetVisibleInSceneCaptureOnly(true);
		Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
#if WITH_EDITOR
		if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Primitive); Scene && Mesh && Mesh->IsVisible())
		{
			for (UMaterialInterface* Material : Mesh->GetMaterials())
			{
				if (!IsValid(Material) || QueuedMaterials.Contains(Material)) { continue; }
				QueuedMaterials.Add(Material);
				// 按需着色器必须先提交实际取景所需的编译任务，摄影 Tick 只观察完成状态。
				if (FMaterialResource* Resource = Material->GetMaterialResource(Scene->GetShaderPlatform());
					Resource && !Resource->IsGameThreadShaderMapComplete())
				{
					Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
				}
			}
		}
#endif
	}
}

bool ACastleBuildingBase::ArePreviewRenderResourcesReady(FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();
	const FSceneInterface* Scene = GetWorld()->Scene;
	if (!Scene)
	{
		OutFailureReason = INVTEXT("预览模型没有可供摄影的渲染场景。");
		return false;
	}
	TArray<UStaticMeshComponent*> Meshes;
	GetComponents<UStaticMeshComponent>(Meshes);
	TSet<UMaterialInterface*> CheckedMaterials;
	bool bReady = true;
	for (const UStaticMeshComponent* Component : Meshes)
	{
		// 隐藏的功能几何与号牌从不入镜，摄影只对实际绘制的资产作资源检查。
		if (!Component->IsVisible()) { continue; }
		const UStaticMesh* Mesh = Component->GetStaticMesh();
		if (!IsValid(Mesh))
		{
			OutFailureReason = FText::FromString(FString::Printf(TEXT("美术组件 %s 缺少预览网格。"), *Component->GetPathName()));
			return false;
		}
		bReady &= !Mesh->IsCompiling() && !Mesh->HasPendingRenderResourceInitialization() && Component->IsRenderStateCreated();
		for (UMaterialInterface* Material : Component->GetMaterials())
		{
			if (!IsValid(Material))
			{
				OutFailureReason = FText::FromString(FString::Printf(TEXT("美术组件 %s 缺少预览材质。"), *Component->GetPathName()));
				return false;
			}
			if (CheckedMaterials.Contains(Material)) { continue; }
			CheckedMaterials.Add(Material);
			const FMaterialResource* Resource = Material->GetMaterialResource(Scene->GetShaderPlatform());
			bReady &= Material->IsComplete() && Resource && Resource->IsGameThreadShaderMapComplete();
		}
	}
	return bReady;
}
