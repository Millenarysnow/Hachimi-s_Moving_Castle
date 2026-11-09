#include "BuildingPartCatalog.h"

#include "Engine/StaticMesh.h"

#if WITH_EDITOR
#include "MeshDescription.h"
#include "Misc/SecureHash.h"
#include "StaticMeshAttributes.h"

namespace BuildingPartGeometry
{
	/** 保留三角形连接关系，允许相同表面在重新导出后有微小坐标舍入误差。 */
	struct FTriangle
	{
		FVector Points[3];
	};
	struct FShape
	{
		FString Identity;
		FVector Aspect;
		TArray<FTriangle> Triangles;
		double SurfaceArea = 0.0;
	};

	/** 同一平面重新三角化后连接可能不同；用双向表面采样和面积校验判断真实表面是否一致。 */
	bool SameSurface(const FShape& A, const FShape& B)
	{
		constexpr double Tolerance = 1.0 / 4096.0;
		if (!FMath::IsNearlyEqual(A.SurfaceArea, B.SurfaceArea, FMath::Max(A.SurfaceArea, B.SurfaceArea) * 0.0001)) { return false; }
		auto IsOnSurface = [](const FShape& From, const FShape& To)
		{
			for (const FTriangle& Triangle : From.Triangles)
			{
				// 同时检查顶点、边中点和面心，不能只因外轮廓包围盒相同就判为相同建筑零件。
				const FVector Samples[] = { Triangle.Points[0], Triangle.Points[1], Triangle.Points[2],
					(Triangle.Points[0] + Triangle.Points[1]) * 0.5, (Triangle.Points[1] + Triangle.Points[2]) * 0.5,
					(Triangle.Points[2] + Triangle.Points[0]) * 0.5,
					(Triangle.Points[0] + Triangle.Points[1] + Triangle.Points[2]) / 3.0 };
				for (const FVector& Sample : Samples)
				{
					bool bFound = false;
					for (const FTriangle& Surface : To.Triangles)
					{
						const FVector Closest = FMath::ClosestPointOnTriangleToPoint(Sample, Surface.Points[0], Surface.Points[1], Surface.Points[2]);
						if (FVector::DistSquared(Sample, Closest) <= FMath::Square(Tolerance))
						{
							bFound = true;
							break;
						}
					}
					if (!bFound) { return false; }
				}
			}
			return true;
		};
		return IsOnSurface(A, B) && IsOnSurface(B, A);
	}

	bool SameGeometry(const FShape& A, const FShape& B)
	{
		constexpr double Tolerance = 1.0 / 4096.0;
		if (!A.Aspect.Equals(B.Aspect, Tolerance)) { return false; }
		if (A.Triangles.Num() != B.Triangles.Num()) { return SameSurface(A, B); }
		TArray<bool> Used;
		Used.Init(false, B.Triangles.Num());
		for (const FTriangle& Triangle : A.Triangles)
		{
			bool bFound = false;
			for (int32 Index = 0; Index < B.Triangles.Num(); ++Index)
			{
				if (Used[Index]) { continue; }
				bool CornersUsed[3] = { false, false, false };
				int32 Matches = 0;
				for (const FVector& Point : Triangle.Points)
				{
					for (int32 Corner = 0; Corner < 3; ++Corner)
					{
						if (!CornersUsed[Corner] && Point.Equals(B.Triangles[Index].Points[Corner], Tolerance))
						{
							CornersUsed[Corner] = true;
							++Matches;
							break;
						}
					}
				}
				if (Matches == 3)
				{
					Used[Index] = true;
					bFound = true;
					break;
				}
			}
			if (!bFound) { return SameSurface(A, B); }
		}
		return true;
	}
}
#endif

const FBuildingPartGeometry* UBuildingPartCatalog::FindGeometry(const UStaticMesh* Mesh) const
{
	return Entries.Find(TSoftObjectPtr<UStaticMesh>(const_cast<UStaticMesh*>(Mesh)));
}

#if WITH_EDITOR
bool UBuildingPartCatalog::RebuildFromMeshes(const TArray<UStaticMesh*>& Meshes, FString& OutError)
{
	OutError.Reset();
	TMap<TSoftObjectPtr<UStaticMesh>, FBuildingPartGeometry> NewEntries;
	TArray<BuildingPartGeometry::FShape> KnownShapes;
	if (Meshes.IsEmpty())
	{
		OutError = TEXT("零件目录的输入网格列表不能为空。");
		return false;
	}
	for (const UStaticMesh* Mesh : Meshes)
	{
		if (!IsValid(Mesh))
		{
			OutError = TEXT("零件目录包含无效网格。");
			return false;
		}
	}
	// 稳定排序让等价形状的身份种子可复现，与 Python Map 或蓝图组件枚举顺序无关。
	TArray<UStaticMesh*> SortedMeshes = Meshes;
	SortedMeshes.Sort([](const UStaticMesh& A, const UStaticMesh& B) { return A.GetPathName() < B.GetPathName(); });
	for (UStaticMesh* Mesh : SortedMeshes)
	{
		if (!IsValid(Mesh))
		{
			OutError = TEXT("零件目录包含无效网格。");
			return false;
		}
		const FMeshDescription* Description = Mesh->GetMeshDescription(0);
		if (!Description || Description->Vertices().Num() == 0 || Description->Triangles().Num() == 0)
		{
			OutError = FString::Printf(TEXT("%s 没有可读的 LOD0 源三角形。"), *Mesh->GetPathName());
			return false;
		}
		const FStaticMeshConstAttributes Attributes(*Description);
		const TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		FBox Bounds(ForceInit);
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			Bounds += FVector(Positions[Vertex]);
		}
		const FVector Size = Bounds.GetSize();
		const double Unit = Size.GetMax();
		if (!Bounds.IsValid || !FMath::IsFinite(Unit) || Unit <= UE_SMALL_NUMBER)
		{
			OutError = FString::Printf(TEXT("%s 的源几何范围无效。"), *Mesh->GetPathName());
			return false;
		}

		// 去掉整体平移和统一单位差异；四千分之一的量化吸收导出浮点误差。
		// 保留各轴比例、三角形连接与朝向，因此不同形状不能仅凭相同包围盒被配为同一零件。
		const FVector Center = Bounds.GetCenter();
		BuildingPartGeometry::FShape Shape;
		Shape.Aspect = Size / Unit;
		TArray<FString> Triangles;
		Triangles.Reserve(Description->Triangles().Num());
		for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
		{
			BuildingPartGeometry::FTriangle GeometryTriangle;
			int32 CornerIndex = 0;
			TArray<FString, TInlineAllocator<3>> Corners;
			for (const FVertexID Vertex : Description->GetTriangleVertices(Triangle))
			{
				const FVector P = (FVector(Positions[Vertex]) - Center) * (4096.0 / Unit);
				GeometryTriangle.Points[CornerIndex++] = P / 4096.0;
				Corners.Add(FString::Printf(TEXT("%d,%d,%d"), FMath::RoundToInt(P.X), FMath::RoundToInt(P.Y), FMath::RoundToInt(P.Z)));
			}
			Corners.Sort();
			Triangles.Add(Corners[0] + TEXT("/") + Corners[1] + TEXT("/") + Corners[2]);
			Shape.Triangles.Add(GeometryTriangle);
			Shape.SurfaceArea += FVector::CrossProduct(GeometryTriangle.Points[1] - GeometryTriangle.Points[0],
				GeometryTriangle.Points[2] - GeometryTriangle.Points[0]).Size() * 0.5;
		}
		Triangles.Sort();
		const FString Canonical = FString::Join(Triangles, TEXT(";"));
		FTCHARToUTF8 Bytes(*Canonical);
		uint8 Digest[FSHA1::DigestSize];
		FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Digest);
		FBuildingPartGeometry Geometry;
		Geometry.GeometryId = BytesToHex(Digest, FSHA1::DigestSize);
		// 量化边界两侧的同一点可能有不同哈希；再逐三角形校验等价，不能靠降低精度硬凑对应。
		const BuildingPartGeometry::FShape* Equivalent = KnownShapes.FindByPredicate([&Shape](const BuildingPartGeometry::FShape& Existing)
		{
			return BuildingPartGeometry::SameGeometry(Shape, Existing);
		});
		if (Equivalent) { Geometry.GeometryId = Equivalent->Identity; }
		else
		{
			Shape.Identity = Geometry.GeometryId;
			KnownShapes.Add(MoveTemp(Shape));
		}
		Geometry.LocalSize = Size;
		NewEntries.Add(Mesh, MoveTemp(Geometry));
	}
	Modify();
	Entries = MoveTemp(NewEntries);
	MarkPackageDirty();
	return true;
}
#endif // WITH_EDITOR
