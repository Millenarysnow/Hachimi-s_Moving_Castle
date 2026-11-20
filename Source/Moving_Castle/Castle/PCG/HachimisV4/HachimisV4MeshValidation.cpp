#if WITH_EDITOR

#include "HachimisV4MeshValidation.h"

#include "DistanceFieldAtlas.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "MeshDescription.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"

namespace HachimisV4MeshValidation
{
	bool Verify(UStaticMesh& Mesh, FString& Reason)
	{
		// StaticMesh 的 LOD 编译和距离场是不同队列，等待前者不能证明 GPU 会收到有效的 SDF。
		FStaticMeshCompilingManager::Get().FinishCompilation({&Mesh});
		const bool bNeedsDistanceField = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GenerateMeshDistanceFields"))->GetInt() != 0
			|| Mesh.bGenerateMeshDistanceField;
		if (bNeedsDistanceField && GDistanceFieldAsyncQueue)
		{
			GDistanceFieldAsyncQueue->BlockUntilBuildComplete(&Mesh, false);
		}
		const FMeshDescription* Source = Mesh.GetMeshDescription(0);
		const FStaticMeshRenderData* Render = Mesh.GetRenderData();
		if (!Source || Source->Triangles().Num() == 0 || !Render || Render->LODResources.IsEmpty()
			|| Render->LODResources[0].GetNumTriangles() == 0)
		{
			Reason = TEXT("缺少作者或实际渲染 LOD0 三角");
			return false;
		}

		const FBoxSphereBounds SourceBounds = Source->GetBounds();
		// 不只检查 NaN。损坏的派生缓存还可能给出有限但数量级错误、反向的范围。
		// 构建可去掉重合点或退化边，0.1 cm 与资产装配测量沿用同一容差。
		constexpr double ToleranceCm = 0.1;
		auto MatchesSource = [&](const FBoxSphereBounds& Bounds)
		{
			return !Bounds.ContainsNaN() && Bounds.BoxExtent.GetMin() >= 0.0 && Bounds.SphereRadius > 0.0
				&& Bounds.Origin.Equals(SourceBounds.Origin, ToleranceCm) && Bounds.BoxExtent.Equals(SourceBounds.BoxExtent, ToleranceCm);
		};
		if (!MatchesSource(Render->Bounds) || !MatchesSource(Mesh.GetBounds()))
		{
			Reason = FString::Printf(TEXT("作者、渲染与组件边界不一致：作者 %s；LOD %s；组件 %s"),
				*SourceBounds.GetBox().ToString(), *Render->Bounds.GetBox().ToString(), *Mesh.GetBounds().GetBox().ToString());
			return false;
		}
		if (bNeedsDistanceField)
		{
			const FDistanceFieldVolumeData* DistanceField = Render->LODResources[0].DistanceFieldData;
			if (!DistanceField || DistanceField->bAsyncBuilding || DistanceField->AlwaysLoadedMip.IsEmpty())
			{
				Reason = TEXT("启用距离场但没有完成 LOD0 距离场构建");
				return false;
			}
			const FBox3f& Bounds = DistanceField->LocalSpaceMeshBounds;
			const FVector FieldExtent(Bounds.GetExtent());
			// 距离场可扩边来覆盖薄件和体素边界，但不能漏掉作者网格，或引入偏心、非有限的渲染原点。
			if (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN()
				|| FieldExtent.GetMin() <= 0.0 || !FieldExtent.ComponentMax(SourceBounds.BoxExtent).Equals(FieldExtent, ToleranceCm)
				|| !FVector(Bounds.GetCenter()).Equals(SourceBounds.Origin, ToleranceCm))
			{
				Reason = FString::Printf(TEXT("距离场边界无效或偏离作者原点：%s；作者 %s"),
					*Bounds.ToString(), *SourceBounds.GetBox().ToString());
				return false;
			}
		}
		return true;
	}
}

#endif
