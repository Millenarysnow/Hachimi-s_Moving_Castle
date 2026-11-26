#include "CastleMenuStage.h"

#include "CastleMenuModel.h"
#include "Camera/CameraTypes.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"

ACastleMenuStage::ACastleMenuStage()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("StageRoot")));
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("CastlePivot"));
	Pivot->SetupAttachment(GetRootComponent());
	// 柔暖主光揭示屋顶和塔柱，冷色补光保留转到背光面时的颜色层次。
	auto* Key = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("KeyLight"));
	Key->SetupAttachment(GetRootComponent());
	Key->SetRelativeRotation(FRotator(-48, 32, 0));
	Key->SetIntensity(4.0f);
	Key->ForwardShadingPriority = 1;
	Key->SetLightColor(FLinearColor(1.0f, 0.94f, 0.84f));
	auto* Fill = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("FillLight"));
	Fill->SetupAttachment(GetRootComponent());
	Fill->SetRelativeRotation(FRotator(-24, 205, 0));
	Fill->SetIntensity(1.7f);
	Fill->SetLightColor(FLinearColor(0.82f, 0.90f, 1.0f));
	Fill->SetCastShadows(false);
}

bool ACastleMenuStage::ShowModel(const UCastleMenuModel& Model, FString& Error)
{
	// 全部资源和变换验证完成后才注册本批组件，避免损坏存档显示一半城堡。
	struct FResolvedBatch
	{
		UStaticMesh* Mesh;
		TArray<UMaterialInterface*> Materials;
		const FCastleMenuMeshBatch* Source;
	};
	TArray<FResolvedBatch> Groups;
	FBox Bounds(ForceInit);
	auto Prepare = [&](const FCastleMenuMeshBatch& Batch)
	{
		UStaticMesh* Mesh = Batch.Mesh.LoadSynchronous();
		if (!IsValid(Mesh) || Batch.Materials.IsEmpty() || Batch.Placements.IsEmpty()
			|| Batch.PrimitiveData.Num() > FCustomPrimitiveData::NumCustomPrimitiveDataFloats
			|| Batch.PrimitiveData.ContainsByPredicate([](float Value) { return !FMath::IsFinite(Value); }))
		{
			Error = FString::Printf(TEXT("展示网格 %s 缺失，或其实例、材质、属性色数据无效。"), *Batch.Mesh.ToString());
			return false;
		}
		FResolvedBatch& Group = Groups.Add_GetRef({Mesh, {}, &Batch});
		for (int32 Slot = 0; Slot < Batch.Materials.Num(); ++Slot)
		{
			UMaterialInterface* Material = Batch.Materials[Slot].LoadSynchronous();
			if (!IsValid(Material))
			{
				Error = FString::Printf(TEXT("展示网格 %s 的材质 %s 无法加载。"), *Mesh->GetPathName(), *Batch.Materials[Slot].ToString());
				return false;
			}
			Group.Materials.Add(Material);
		}
		for (const FTransform& Placement : Batch.Placements)
		{
			if (!Placement.IsValid()) { Error = Mesh->GetPathName() + TEXT(" 含无效展示变换。"); return false; }
			Bounds += Mesh->GetBoundingBox().TransformBy(Placement.ToMatrixWithScale());
		}
		return true;
	};
	for (const auto& Batch : Model.Tower) { if (!Prepare(Batch)) { return false; } }
	for (const auto& Batch : Model.Rooms) { if (!Prepare(Batch)) { return false; } }
	if (!Bounds.IsValid || Bounds.GetExtent().GetMin() <= 0.0) { Error = TEXT("展示城堡没有有效的三维包围盒。"); return false; }
	for (const FResolvedBatch& Group : Groups)
	{
		for (FTransform Placement : Group.Source->Placements)
		{
			// 游局材质按普通静态网格编译。逐件复现相同顶点工厂，保留材质与原始属性色。
			auto* Part = NewObject<UStaticMeshComponent>(this);
			Part->SetupAttachment(Pivot);
			Part->SetMobility(EComponentMobility::Movable);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetCanEverAffectNavigation(false);
			Part->SetGenerateOverlapEvents(false);
			Part->SetStaticMesh(Group.Mesh);
			for (int32 I = 0; I < Group.Materials.Num(); ++I) { Part->SetMaterial(I, Group.Materials[I]); }
			Part->SetDefaultCustomPrimitiveDataFloatArray(0, Group.Source->PrimitiveData);
			Placement.AddToTranslation(-Bounds.GetCenter());
			Part->SetRelativeTransform(Placement);
			AddInstanceComponent(Part);
			Part->RegisterComponent();
		}
	}
	const FVector Extent = Bounds.GetExtent();
	SweepRadius = FVector2D(Extent.X, Extent.Y).Size();
	HalfHeight = Extent.Z;
	return true;
}

void ACastleMenuStage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 与前作一致的角速度，独立于帧率；转轴在整座已摆城堡的包围中心。
	SpinAngle = FMath::Fmod(SpinAngle + 2.0 * DeltaSeconds, 360.0);
	Pivot->SetRelativeRotation(FRotator(0, SpinAngle, 0));
}

void ACastleMenuStage::CalcCamera(float DeltaTime, FMinimalViewInfo& View)
{
	int32 Width = 0, Height = 0;
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->GetViewportSize(Width, Height); }
	// 视口刚创建时没有可投影的像素。下次有效相机更新会直接使用真实宽高。
	if (Width <= 0 || Height <= 0) { return; }
	View.FOV = 32.0f;
	View.AspectRatio = static_cast<float>(Width) / Height;
	View.AspectRatioAxisConstraint = AspectRatio_MaintainXFOV;
	View.bConstrainAspectRatio = false;
	View.Rotation = FRotator(-12, 35, 0);
	const FRotationMatrix Basis(View.Rotation);
	const FVector Forward = Basis.GetUnitAxis(EAxis::X);
	const FVector Right = Basis.GetUnitAxis(EAxis::Y);
	const FVector Up = Basis.GetUnitAxis(EAxis::Z);
	const double TanX = FMath::Tan(FMath::DegreesToRadians(View.FOV * 0.5));
	const double TanY = TanX / View.AspectRatio;
	// 转轴位于画面宽度的 72% 处。整周扫过的城堡最多占视口宽度的 39%、高度的 70%，
	// 让单塔的气球上方、带侧翼的城堡两侧都能看到纸面。深度项计入偏心透视产生的位移。
	constexpr double CenterX = 0.44, HalfX = 0.39, HalfY = 0.70;
	const double DepthRadius = SweepRadius * FMath::Abs(Forward.Size2D()) + HalfHeight * FMath::Abs(Forward.Z);
	const double VerticalRadius = SweepRadius * Up.Size2D() + HalfHeight * FMath::Abs(Up.Z);
	const double Distance = DepthRadius + FMath::Max((SweepRadius / TanX + CenterX * DepthRadius) / HalfX,
		VerticalRadius / (TanY * HalfY)) + 10.0;
	View.Location = GetActorLocation() - Forward * Distance - Right * (CenterX * Distance * TanX);
	View.PostProcessBlendWeight = 1.0f;
	auto& Post = View.PostProcessSettings;
	Post.bOverride_AutoExposureMethod = true;
	Post.AutoExposureMethod = AEM_Manual;
	Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Post.AutoExposureApplyPhysicalCameraExposure = false;
	Post.bOverride_AutoExposureBias = true;
	Post.AutoExposureBias = 0.0f;
	Post.bOverride_VignetteIntensity = true;
	Post.VignetteIntensity = 0.0f;
	Post.bOverride_MotionBlurAmount = true;
	Post.MotionBlurAmount = 0.0f;
	// 纸张只替换空背景像素，建筑使用当前项目的正常灯光和战斗材质。
	if (PaperMaterial) { Post.AddBlendable(PaperMaterial, 1.0f); }
}
