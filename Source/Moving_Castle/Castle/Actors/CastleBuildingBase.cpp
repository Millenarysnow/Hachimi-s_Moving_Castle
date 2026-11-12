// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildingBase.h"

#include "CastleBuildingDefinition.h"
#include "BuildingMergeComponent.h"
#include "AssemblyFlightComponent.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/Building/Effects/CastleBuildingEffectComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"
#include "Units/BuildingInteraction/CastleBuildingTriggerSubsystem.h"

ACastleBuildingBase::ACastleBuildingBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	BuildingEffect = CreateDefaultSubobject<UCastleBuildingEffectComponent>(TEXT("BuildingEffect"));
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
	LabelPlateMesh = Cube.Object;
	LabelPlateMaterial = Material.Object;

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
	return BuildingEffect->Trigger(Context);
}

void ACastleBuildingBase::SetPlacementComplete(const bool bComplete)
{
	if (bHasCompletedPlacement != bComplete)
	{
		if (auto* Triggers = GetWorld()->GetSubsystem<UCastleBuildingTriggerSubsystem>()) { Triggers->NotifyBuildingStateChanged(this); }
	}
	bHasCompletedPlacement = bComplete;
	PrototypeLabel->SetVisibility(bComplete, false);
	PrototypeLabelPlate->SetVisibility(bComplete, false);
}

void ACastleBuildingBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (auto* Triggers = GetWorld()->GetSubsystem<UCastleBuildingTriggerSubsystem>()) { Triggers->UnregisterBuilding(this); }
	Super::EndPlay(EndPlayReason);
}

bool ACastleBuildingBase::InitializeBuilding(const FCastleBuildingDescriptor& Descriptor, const TArray<FIntVector>& InOccupiedCoordinates)
{
	FText Reason;
	if (!Descriptor.Validate(Reason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑初始化失败：%s"), *Reason.ToString());
		return false;
	}
	if (Descriptor.GetBuildingClass() != GetClass() || Descriptor.Level != BuildingLevel || Descriptor.GetBuildingType() != BuildingType)
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

	// 保存资产身份，后续升级、回收和再次放置都不重新推断建筑种类。
	BuildingDefinition = Descriptor.Definition;
	Color = Descriptor.Color;
	Number = Descriptor.Number;
	OccupiedCoordinates = InOccupiedCoordinates;
	PrototypeLabel->SetText(FText::FromString(FString::Printf(TEXT("%d\nLv%d"), Number, BuildingLevel)));
	return true;
}

FCastleBuildingDescriptor ACastleBuildingBase::GetBuildingDescriptor() const
{
	FCastleBuildingDescriptor Descriptor;
	Descriptor.Definition = BuildingDefinition;
	Descriptor.Level = BuildingLevel;
	Descriptor.Color = Color;
	Descriptor.Number = Number;
	return Descriptor;
}

TSubclassOf<ACastleBuildingBase> ACastleBuildingBase::GetClassForLevel(int32 InLevel) const
{
	const FCastleBuildingLevelDefinition* Entry = IsValid(BuildingDefinition) ? BuildingDefinition->FindLevel(InLevel) : nullptr;
	return Entry != nullptr ? Entry->BuildingClass : nullptr;
}

bool ACastleBuildingBase::CanMergeToNextLevel() const
{
	// 保留策划案的四级自动合成上限；五级仍供后续奖励系统直接升级。
	return BuildingType != ECastleBuildingType::Road && BuildingLevel >= 1 && BuildingLevel < 4
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

	// 已启用的美术碰撞只提供 Visibility 查询；作者明确关闭碰撞的纯表现组件保持无碰撞。
	// 功能分支完整保留作者配置，不能按可见性关闭隐藏地板碰撞。
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
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
	// 卡片只显示建筑形状，等级与数字由卡片自己的文字控件显示。
	PrototypeLabel->SetVisibility(false);
	PrototypeLabelPlate->SetVisibility(false);
	TArray<UPrimitiveComponent*> Primitives;
	GetComponents<UPrimitiveComponent>(Primitives);
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
		Primitive->SetCastShadow(false);
	}
}
