#include "Interaction/CastlePatrolWaypointMarker.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 左右以各面自己的观看方向命名。背面翻转由资产 Socket 提供，二者使用同一套非镜像数字。
	const FName FrontTensSocket(TEXT("FrontTens"));
	const FName FrontOnesSocket(TEXT("FrontOnes"));
	const FName FrontCenterSocket(TEXT("FrontCenter"));
	const FName BackTensSocket(TEXT("BackTens"));
	const FName BackOnesSocket(TEXT("BackOnes"));
	const FName BackCenterSocket(TEXT("BackCenter"));

	constexpr const TCHAR* CircleMeshAsset = TEXT("/Game/PathTracer/Meshes/PathMarks/SM_PT_Plane.SM_PT_Plane");
	constexpr const TCHAR* CircleMaterialAsset = TEXT("/Game/PathTracer/Showcase/Materials/PreviewMaterials/Procedural/MI_PT_Circle_Preview_04.MI_PT_Circle_Preview_04");
}

ACastlePatrolWaypointMarker::ACastlePatrolWaypointMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);
	// 标记是纯视觉对象：它既不拦 Visibility 射线，也不向动态 NavMesh 提交几何或障碍。
	const auto CreateVisualMesh = [this](FName Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		if (Parent) { Mesh->SetupAttachment(Parent); }
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		return Mesh;
	};
	Body = CreateVisualMesh(TEXT("MarkerBody"), nullptr);
	SetRootComponent(Body);
	GroundCircle = CreateVisualMesh(TEXT("WaypointGroundCircle"), Body);
	// 工具包平面为 100 cm 见方。只抬高显示平面避开 Z-fighting，落点、悬浮数字与寻路输入都不移动。
	GroundCircle->SetRelativeLocation(FVector(0.0, 0.0, 2.0));
	GroundCircle->SetCastShadow(false);
	GroundCircle->SetAffectDistanceFieldLighting(false);
	GroundCircle->SetVisibleInRayTracing(false);
	GroundCircle->SetReceivesDecals(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CirclePlane(CircleMeshAsset);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CircleStyle(CircleMaterialAsset);
	GroundCircleMesh = CirclePlane.Object;
	GroundCircleMaterial = CircleStyle.Object;
	FrontOnes = CreateVisualMesh(TEXT("FrontOnes"), Body);
	FrontTens = CreateVisualMesh(TEXT("FrontTens"), Body);
	BackOnes = CreateVisualMesh(TEXT("BackOnes"), Body);
	BackTens = CreateVisualMesh(TEXT("BackTens"), Body);
}

void ACastlePatrolWaypointMarker::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// 编辑器预览与运行时共用一套摆位规则；构造时配置无效就不显示可能残留的旧画面。
	if (!SetDisplayedNumber(DisplayedNumber)) { Body->SetVisibility(false, true); }
}

bool ACastlePatrolWaypointMarker::SetDisplayedNumber(int32 Number)
{
	const auto Reject = [this, Number](const FString& Reason)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("巡逻标记 %s（%s）显示 %d 失败：%s"), *GetPathName(),
			*GetPathNameSafe(MarkerBodyMesh.Get()), Number, *Reason);
		return false;
	};

	// 先验证整套配置再修改组件，避免越界时出现前后不一致、只更新了一位等状态。
	if (Number < 0 || Number > MaxDisplayedNumber) { return Reject(TEXT("数字必须介于 0 和 99 之间。")); }
	if (!IsValid(GroundCircle) || !IsValid(GroundCircleMesh) || !IsValid(GroundCircleMaterial)
		|| !GroundCircleMesh->HasValidRenderData() || GroundCircleMesh->GetStaticMaterials().Num() != 1)
	{
		return Reject(FString::Printf(TEXT("地面圆圈的组件、单槽网格 %s 或材质 %s 无效。"), CircleMeshAsset, CircleMaterialAsset));
	}
	if (!IsValid(MarkerBodyMesh)) { return Reject(TEXT("MarkerBodyMesh 未配置。")); }
	if (DigitMeshes.Num() != 10) { return Reject(TEXT("DigitMeshes 必须按 0～9 顺序配置十个网格。")); }
	for (int32 Index = 0; Index < DigitMeshes.Num(); ++Index)
	{
		if (!IsValid(DigitMeshes[Index])) { return Reject(FString::Printf(TEXT("缺少数字 %d 的网格。"), Index)); }
	}
	for (const FName Socket : {FrontTensSocket, FrontOnesSocket, FrontCenterSocket, BackTensSocket, BackOnesSocket, BackCenterSocket})
	{
		if (!MarkerBodyMesh->FindSocket(Socket)) { return Reject(FString::Printf(TEXT("主体网格缺少 Socket %s。"), *Socket.ToString())); }
	}

	Body->SetStaticMesh(MarkerBodyMesh);
	Body->SetVisibility(true);
	// 同一个路点实例同时拥有圆圈和编号，所以草稿撤回、提交、清空时不会出现两者错位或残留。
	GroundCircle->SetStaticMesh(GroundCircleMesh);
	GroundCircle->SetMaterial(0, GroundCircleMaterial);
	GroundCircle->SetVisibility(true);
	const auto ShowDigit = [this](UStaticMeshComponent& Mesh, FName Socket, int32 Digit)
	{
		// 组件始终挂在主体下，只用 Socket 的局部变换摆位；从一位切回两位时，个位从面板中间移回右槽。
		Mesh.SetRelativeTransform(Body->GetSocketTransform(Socket, RTS_Component));
		Mesh.SetStaticMesh(DigitMeshes[Digit]);
		Mesh.SetVisibility(true);
	};
	const bool bTwoDigits = Number >= 10;
	ShowDigit(*FrontOnes, bTwoDigits ? FrontOnesSocket : FrontCenterSocket, Number % 10);
	ShowDigit(*BackOnes, bTwoDigits ? BackOnesSocket : BackCenterSocket, Number % 10);
	if (bTwoDigits)
	{
		ShowDigit(*FrontTens, FrontTensSocket, Number / 10);
		ShowDigit(*BackTens, BackTensSocket, Number / 10);
	}
	else
	{
		// 一位数不补前导零，十位组件只隐藏以便下一次显示复用。
		FrontTens->SetVisibility(false);
		BackTens->SetVisibility(false);
	}
	DisplayedNumber = Number;
	return true;
}
