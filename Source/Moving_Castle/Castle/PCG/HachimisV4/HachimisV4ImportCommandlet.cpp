#include "HachimisV4ImportCommandlet.h"

#if WITH_EDITOR
#include "Castle/PCG/Structure/CastlePCGRoofQuarterRule.h"
#include "Castle/PCG/Structure/CastlePCGSupportRules.h"
#include "CastleHachimisV4Layout.h"
#include "Algo/Count.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "HachimisV4MeshValidation.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "PhysicsEngine/BodySetup.h"
#include "ShaderCompiler.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "UObject/Linker.h"
#include "UObject/SavePackage.h"

THIRD_PARTY_INCLUDES_START
#include <fbxsdk.h>
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogHachimisV4, Log, All);

namespace HachimisV4Import
{
	constexpr TCHAR AssetRoot[] = TEXT("/Game/MovingCastle/Model/Buildings/HachimisV4");
	constexpr double CmPerMetre = 100.0;
	constexpr double WalkCm = 40.8;
	constexpr double SeamTolerance = 0.05;

	/** 单个三角的角属性。哈希同时包含 UV、法线和材质，保证只合并可等价渲染的局部几何。 */
	struct FCorner
	{
		FVector3f Position;
		FVector3f Normal;
		FVector2f UV;
	};

	struct FTriangle
	{
		FCorner Corners[3];
		int32 Material = 0;
	};

	struct FPartSource
	{
		FCastleV4Part Part;
		FString Parent;
		FString WallKey;
		FString RoofCell;
		TArray<FTriangle> Triangles;
		TArray<UMaterial*> Materials;
	};

	struct FSceneSource
	{
		TArray<FPartSource> Parts;
		TMap<FString, FTransform> Mounts;
		TMap<FString, FVector> ClimbMarkers;
		// 独立屋面用自己的根导出，必须用作者给出的厘米锚点恢复主体安装位置。
		FVector RoofAnchor = FVector::ZeroVector;
	};

	/** 保存直接从单份 FBX 读到的盾塔调色板。四款屋面必须提供同一份贴图和同样的材质参数。 */
	struct FShieldRoofPaletteSource
	{
		TArray64<uint8> Png;
		double Roughness = 0.0;
		double Metallic = 0.0;
		double Specular = 0.0;
		FLinearColor Emissive = FLinearColor::Black;

		bool operator==(const FShieldRoofPaletteSource&) const = default;
	};

	/** 单个导入事务记住新建资源和原始语义，跨 FBX 重复几何只生成一次。 */
	class FImporter
	{
	public:
		explicit FImporter(const FString& InDirectory, const FString& InRebuildMesh, bool bInShieldDelivery)
			: Directory(InDirectory), RebuildMesh(InRebuildMesh), bShieldDelivery(bInShieldDelivery) {}
		bool Run(const FJsonObject& Manifest);
		/** 独立交付只写斜撑、坡顶、宽边地板与正式规则集。 */
		bool RunSupportDelivery();
		/** 独立交付四格屋面，已有斜撑、坡顶和梯井资源保持原引用。 */
		bool RunQuadRoofDelivery();
		/** 盾塔四件网格与专用调色板逐件预检后在同一事务中写入。 */
		bool RunShieldQuadRoofDelivery();
		bool RunRoofQuarterDelivery(const FString& DirectionalDirectory);

	private:
		FString Directory;
		FString RebuildMesh;
		bool bShieldDelivery;
		TMap<FString, UStaticMesh*> Meshes;
		TMap<FName, UMaterial*> Palette;
		TMap<FName, UCastleV4WallKit*> WallKits;
		TMap<FName, UCastleV4LadderKit*> LadderKits;
		TArray<UObject*> AssemblyAssets;

		bool ReadScene(const FJsonObject& Row, FSceneSource& Out, bool bRoofModule = false,
			FName SupportGroup = NAME_None, bool bPreflight = false);
		bool VerifyRoofModules(const FJsonObject& Row, const FSceneSource& Building);
		bool ReadNode(FbxNode& Node, FbxNode& Root, FSceneSource& Out, const FString& File, FName SupportGroup, bool bPreflight);
		bool ImportWall(const FJsonObject& Row);
		bool ImportLadder(const FJsonObject& Row);
		bool ImportBuilding(const FJsonObject& Row);
		bool StorePart(FPartSource& Source, const FString& Folder, const FString& AssetName = {});
		bool StoreSlices(const FPartSource& Source, UCastleHachimisV4Layout& Layout, const FString& Folder);
		UMaterial* ReadMaterial(FbxSurfaceMaterial& Source, const FString& File, bool bDeferSave = false);
		UMaterial* ReadSupportMaterial(FbxSurfaceMaterial& Source, const FString& File);
		bool ReadSupportScene(FName Family, FName Group, FSceneSource& Out, bool bPreflight);
		bool ImportSupportPart(FName Family, FName Group, const FString& Folder, FCastleV4Part& Out);
		bool ReadQuadRoofPart(FName Family, FName Group, FPartSource& Out, FShieldRoofPaletteSource* ShieldPalette = nullptr);
		bool StoreQuadRoofKit(FName Family, ECastlePCGQuadRoofKind Kind, FPartSource& Part, UMaterial& Material,
			const FString& Folder, UCastlePCGQuadRoofOverlayRule& Rule, TArray<UObject*>& Assets);
		bool ReadRoofQuarter(const FString& File, const FString& Name, FName Family, FPartSource& Out);
	};

	bool Fail(const FString& Object, const FString& Reason)
	{
		UE_LOG(LogHachimisV4, Error, TEXT("%s：%s"), *Object, *Reason);
		return false;
	}

	FString PackageFile(const UObject& Asset)
	{
		return FPackageName::LongPackageNameToFilename(Asset.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
	}

	/** 暂存和正式写入使用同一原生包序列化。暂存只改文件位置，资产引用仍使用正式长包名。 */
	bool SaveTo(UObject& Asset, const FString& File)
	{
		UPackage* Package = Asset.GetOutermost();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, &Asset, *File, Args))
		{
			return Fail(Asset.GetPathName(), TEXT("原生资源保存失败：") + File);
		}
		return true;
	}

	bool Save(UObject& Asset)
	{
		return SaveTo(Asset, PackageFile(Asset));
	}

	/** 结构件、材质和套件互相引用。整批序列化及旧包备份齐备后，才依次发布正式包。 */
	bool SaveSupportDelivery(TConstArrayView<UObject*> Assets)
	{
		IFileManager& Files = IFileManager::Get();
		const FString Staging = FPaths::ProjectSavedDir() / TEXT("HC515Import") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
		if (!Files.MakeDirectory(*Staging, true)) { return Fail(Staging, TEXT("无法创建结构件暂存目录")); }
		bool bKeepRecoveryFiles = false;
		ON_SCOPE_EXIT
		{
			if (!bKeepRecoveryFiles && !Files.DeleteDirectory(*Staging, true, true))
			{
				Fail(Staging, TEXT("无法清理结构件暂存目录"));
			}
		};
		struct FStagedPackage
		{
			FString Target;
			FString Pending;
			FString Backup;
		};
		TArray<FStagedPackage> Packages;
		for (UObject* Asset : Assets)
		{
			FStagedPackage& Package = Packages.AddDefaulted_GetRef();
			Package.Target = PackageFile(*Asset);
			const FString Subdir = Staging / LexToString(Packages.Num());
			if (!Files.MakeDirectory(*Subdir, false)) { return Fail(Subdir, TEXT("无法创建包暂存目录")); }
			Package.Pending = Subdir / FPaths::GetCleanFilename(Package.Target);
			if (Files.FileExists(*Package.Target))
			{
				Package.Backup = Package.Pending + TEXT(".previous");
				if (Files.IsReadOnly(*Package.Target) || Files.Copy(*Package.Backup, *Package.Target) != COPY_OK)
				{
					return Fail(Package.Target, TEXT("正式包只读或无法备份"));
				}
			}
			if (!SaveTo(*Asset, Package.Pending)) { return false; }
			if (!Package.Backup.IsEmpty())
			{
				// 原生序列化写暂存路径时不会释放正式文件的加载句柄，发布前需显式解除。
				ResetLoaders(Asset->GetOutermost());
			}
		}
		for (int32 Index = 0; Index < Packages.Num(); ++Index)
		{
			const FStagedPackage& Package = Packages[Index];
			if (Files.MakeDirectory(*FPaths::GetPath(Package.Target), true)
				&& Files.Move(*Package.Target, *Package.Pending, true, false, false, true))
			{
				continue;
			}
			// 文件系统可能已触碰当前目标。回退本轮已处理的全部包，新包删除、旧包由备份恢复。
			for (int32 Restore = Index; Restore >= 0; --Restore)
			{
				const FStagedPackage& Changed = Packages[Restore];
				const bool bRestored = Changed.Backup.IsEmpty()
					? Files.Delete(*Changed.Target, false)
					: Files.Copy(*Changed.Target, *Changed.Backup) == COPY_OK;
				if (!bRestored)
				{
					bKeepRecoveryFiles = true;
					Fail(Changed.Target, TEXT("无法回退正式包，恢复文件保留在 ") + Staging);
				}
			}
			return Fail(Package.Target, TEXT("无法发布结构件正式包"));
		}
		return true;
	}

	template<typename T>
	T* CreateOrLoad(const FString& PackageName)
	{
		const FString Name = FPackageName::GetShortName(PackageName);
		if (FPackageName::DoesPackageExist(PackageName))
		{
			return LoadObject<T>(nullptr, *(PackageName + TEXT(".") + Name));
		}
		T* Asset = NewObject<T>(CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Asset);
		return Asset;
	}

	FName PropertyName(FbxNode& Node, const char* Property)
	{
		const FbxProperty Value = Node.FindProperty(Property);
		return Value.IsValid() ? FName(UTF8_TO_TCHAR(Value.Get<FbxString>().Buffer())) : NAME_None;
	}

	FVector Position(const FbxVector4& P)
	{
		return FVector(P[0], -P[1], P[2]) * CmPerMetre;
	}

	/** FBX 场景根含导出器旋转；用作者根的逆矩阵消除。镜像 Y 将 -Y 前脸转换成 UE +Y。 */
	FTransform TransformOf(const FbxAMatrix& M)
	{
		const FbxQuaternion Q = M.GetQ();
		const FbxVector4 S = M.GetS();
		return FTransform(FQuat(-Q[0], Q[1], -Q[2], Q[3]), Position(M.GetT()), FVector(S[0], S[1], S[2]));
	}

	FBox Bounds(const FPartSource& Source)
	{
		FBox Box(ForceInit);
		for (const FTriangle& Triangle : Source.Triangles)
		{
			for (const FCorner& Corner : Triangle.Corners)
			{
				Box += Source.Part.Local.TransformPosition(FVector(Corner.Position));
			}
		}
		return Box;
	}

	void AssignCoverage(const FBox& Box, const UCastleHachimisV4Layout& Layout, int32 Layer, TArray<FIntVector>& Cells)
	{
		const double Half = UCastleHachimisV4Layout::CellPitch * 0.5;
		for (int32 X = 0; X < Layout.Grid.X; ++X)
		{
			for (int32 Y = 0; Y < Layout.Grid.Y; ++Y)
			{
				const FIntVector Cell(X, Y, Layer);
				const FVector Center = Layout.CellBottom(Cell);
				if (Box.Min.X < Center.X + Half - SeamTolerance && Box.Max.X > Center.X - Half + SeamTolerance
					&& Box.Min.Y < Center.Y + Half - SeamTolerance && Box.Max.Y > Center.Y - Half + SeamTolerance)
				{
					Cells.Add(Cell);
				}
			}
		}
	}

	UMaterial* FImporter::ReadMaterial(FbxSurfaceMaterial& Source, const FString& File, bool bDeferSave)
	{
		const FString Name = UTF8_TO_TCHAR(Source.GetName());
		const FbxProperty Diffuse = Source.FindProperty(FbxSurfaceMaterial::sDiffuse);
		if (!Diffuse.IsValid() || !Name.StartsWith(TEXT("M_")))
		{
			Fail(File, FString::Printf(TEXT("材质 %s 缺少作者基础色或命名非法"), *Name));
			return nullptr;
		}
		const FbxDouble3 Color = Diffuse.Get<FbxDouble3>();
		if (!(Color[0] >= 0 && Color[0] <= 1 && Color[1] >= 0 && Color[1] <= 1 && Color[2] >= 0 && Color[2] <= 1))
		{
			Fail(File, FString::Printf(TEXT("材质 %s 的 FBX DiffuseColor 越界"), *Name));
			return nullptr;
		}
		const FColor SRGB = FLinearColor(Color[0], Color[1], Color[2]).ToFColorSRGB();
		float Roughness = 0.0f, Metallic = 0.0f, Specular = 0.0f;
		FColor Emissive = FColor::Black;
		if (bShieldDelivery)
		{
			// 盾塔是 Blender Phong FBX 交付：反解导出时的光泽度和高光换算，保留酒红釉、铜面及粉色自发光。
			const FbxProperty Shine = Source.FindProperty(FbxSurfacePhong::sShininess);
			const FbxProperty Reflect = Source.FindProperty(FbxSurfacePhong::sReflectionFactor);
			const FbxProperty Highlight = Source.FindProperty(FbxSurfacePhong::sSpecularFactor);
			const FbxProperty Glow = Source.FindProperty(FbxSurfaceMaterial::sEmissive);
			const FbxProperty Intensity = Source.FindProperty(FbxSurfaceMaterial::sEmissiveFactor);
			if (!Shine.IsValid() || !Reflect.IsValid() || !Highlight.IsValid() || !Glow.IsValid() || !Intensity.IsValid())
			{
				Fail(File, Name + TEXT(" 缺少作者表面材质参数"));
				return nullptr;
			}
			const double Shininess = Shine.Get<FbxDouble>();
			const double Metalness = Reflect.Get<FbxDouble>();
			const double Strength = Intensity.Get<FbxDouble>();
			const double SpecularAmount = Highlight.Get<FbxDouble>() * 2.0;
			const FbxDouble3 Tint = Glow.Get<FbxDouble3>();
			const FLinearColor Light(Tint[0] * Strength, Tint[1] * Strength, Tint[2] * Strength);
			if (!(Shininess >= 0 && Shininess <= 100 && Metalness >= 0 && Metalness <= 1
				&& SpecularAmount >= 0 && SpecularAmount <= 1 && Strength >= 0
				&& !FVector(Light.R, Light.G, Light.B).ContainsNaN() && Light.GetMin() >= 0 && Light.GetMax() <= 1))
			{
				Fail(File, Name + TEXT(" 表面材质参数超出盾塔交付的有效范围"));
				return nullptr;
			}
			Roughness = 1.0 - FMath::Sqrt(Shininess) / 10.0;
			Metallic = Metalness;
			Specular = SpecularAmount;
			Emissive = Light.ToFColorSRGB();
		}
		if (UMaterial* const* Found = Palette.Find(*Name))
		{
			const UMaterialEditorOnlyData* Data = (*Found)->GetEditorOnlyData();
			if (Data->BaseColor.Constant == SRGB && (!bShieldDelivery || (Data->Roughness.Constant == Roughness
				&& Data->Metallic.Constant == Metallic && Data->Specular.Constant == Specular
				&& Data->EmissiveColor.Constant == Emissive))) { return *Found; }
			Fail(File, FString::Printf(TEXT("同名材质 %s 含不同的作者材质参数"), *Name));
			return nullptr;
		}
		// 铜色等同名材质仅供盾塔使用，不能改写上一批建筑的共享材质。
		const FString Folder = bShieldDelivery ? FString(AssetRoot) / TEXT("Shield/Materials") : FString(AssetRoot) / TEXT("Materials");
		UMaterial* Material = CreateOrLoad<UMaterial>(Folder / Name);
		if (!Material) { Fail(File, Name + TEXT(" 材质包不可读")); return nullptr; }
		// UE 以 sRGB 字节存储常量，着色时转换回线性空间。FBX 未携带 Blender 的程序纹理。
		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		Data->BaseColor.UseConstant = true;
		Data->BaseColor.Constant = SRGB;
		if (bShieldDelivery)
		{
			Data->Roughness.UseConstant = Data->Metallic.UseConstant = Data->Specular.UseConstant = Data->EmissiveColor.UseConstant = true;
			Data->Roughness.Constant = Roughness;
			Data->Metallic.Constant = Metallic;
			Data->Specular.Constant = Specular;
			Data->EmissiveColor.Constant = Emissive;
		}
		Material->PostEditChange();
		if (!bDeferSave && !Save(*Material)) { return nullptr; }
		Palette.Add(*Name, Material);
		return Material;
	}

	UMaterial* FImporter::ReadSupportMaterial(FbxSurfaceMaterial& Source, const FString& File)
	{
		const FString Name = UTF8_TO_TCHAR(Source.GetName());
		if (!Name.StartsWith(TEXT("M_HC515_")))
		{
			Fail(File, Name + TEXT(" 不是结构补件的作者材质名"));
			return nullptr;
		}
		// 该批作者材质使用独立命名空间；复用 V4 的 FBX 颜色换算及同名一致性检查。
		return ReadMaterial(Source, File, true);
	}

	bool FImporter::ReadNode(FbxNode& Node, FbxNode& Root, FSceneSource& Out, const FString& File, FName SupportGroup, bool bPreflight)
	{
		const FString Name = UTF8_TO_TCHAR(Node.GetName());
		const FTransform Local = TransformOf(Root.EvaluateGlobalTransform().Inverse() * Node.EvaluateGlobalTransform());
		if (Local.ContainsNaN() || Local.GetScale3D().GetMin() <= UE_SMALL_NUMBER)
		{
			return Fail(File, Name + TEXT(" 作者节点变换无效"));
		}
		if (FbxMesh* Mesh = Node.GetMesh())
		{
			FPartSource& Source = Out.Parts.AddDefaulted_GetRef();
			FbxNode& Parent = *Node.GetParent();
			Source.Part.Node = *Name;
			Source.Part.Local = Local;
			// 结构补件按整份 FBX 声明用途，原造型则逐节点读取作者语义。
			Source.Part.Group = SupportGroup.IsNone() ? PropertyName(Node, "part_group") : SupportGroup;
			Source.Parent = UTF8_TO_TCHAR(Parent.GetName());
			Source.WallKey = PropertyName(Parent, "wall_key").ToString();
			Source.RoofCell = PropertyName(Parent, "roof_cell").ToString();
			// 墙位之间的柱和柱钉标在独立组，作者未重复写 part_group；按明确节点前缀记录语义。
			if (Source.Part.Group.IsNone() && Name.StartsWith(TEXT("Wall_Junction"))) { Source.Part.Group = TEXT("WallJunction"); }
			if (Source.Part.Group.IsNone()) { return Fail(File, Name + TEXT(" 缺少 part_group")); }
			const int32 MaterialCount = Node.GetMaterialCount();
			// 补件预检使用 FBX 的材质槽数量校验三角索引，此时不加载或创建任何 UE 材质对象。
			for (int32 Index = 0; !bPreflight && Index < MaterialCount; ++Index)
			{
				UMaterial* Material = SupportGroup.IsNone() ? ReadMaterial(*Node.GetMaterial(Index), File)
					: ReadSupportMaterial(*Node.GetMaterial(Index), File);
				if (!Material) { return false; }
				Source.Materials.Add(Material);
			}
			FbxStringList UVSets;
			Mesh->GetUVSetNames(UVSets);
			if (UVSets.GetCount() != 1 || MaterialCount == 0 || !Mesh->IsTriangleMesh())
			{
				return Fail(File, Name + TEXT(" 需要作者导出的三角网格、UV0 和材质"));
			}
			const FbxGeometryElementMaterial* MaterialIndices = Mesh->GetElementMaterial();
			for (int32 Polygon = 0; Polygon < Mesh->GetPolygonCount(); ++Polygon)
			{
				FTriangle& Triangle = Source.Triangles.AddDefaulted_GetRef();
				if (MaterialIndices)
				{
					const bool bPerFace = MaterialIndices->GetMappingMode() == FbxLayerElement::eByPolygon;
					if (!bPerFace && MaterialIndices->GetMappingMode() != FbxLayerElement::eAllSame)
					{
						return Fail(File, Name + TEXT(" 的材质映射模式未知"));
					}
					Triangle.Material = MaterialIndices->GetIndexArray().GetAt(bPerFace ? Polygon : 0);
				}
				if (Triangle.Material < 0 || Triangle.Material >= MaterialCount) { return Fail(File, Name + TEXT(" 的三角缺少材质槽")); }
				for (int32 Vertex = 0; Vertex < 3; ++Vertex)
				{
					FbxVector4 Normal;
					FbxVector2 UV;
					bool bUnmapped = false;
					const int32 Point = Mesh->GetPolygonVertex(Polygon, Vertex);
					if (Point < 0 || !Mesh->GetPolygonVertexNormal(Polygon, Vertex, Normal)
						|| !Mesh->GetPolygonVertexUV(Polygon, Vertex, UVSets.GetStringAt(0), UV, bUnmapped) || bUnmapped)
					{
						return Fail(File, Name + TEXT(" 的三角缺少顶点、法线或 UV"));
					}
					FCorner& Corner = Triangle.Corners[Vertex];
					Corner.Position = FVector3f(Position(Mesh->GetControlPointAt(Point)));
					Corner.Normal = FVector3f(Normal[0], -Normal[1], Normal[2]);
					Corner.UV = FVector2f(UV[0], 1.0 - UV[1]);
					if (Corner.Position.ContainsNaN() || Corner.Normal.ContainsNaN() || Corner.UV.ContainsNaN())
					{
						return Fail(File, Name + TEXT(" 的作者三角包含非有限顶点属性"));
					}
				}
			}
		}
		else if (&Node != &Root)
		{
			const FName Marker = PropertyName(Node, "marker_role");
			if (!Marker.IsNone())
			{
				if (Out.ClimbMarkers.Contains(Marker.ToString())) { return Fail(File, Name + TEXT(" 重复的登梯标识")); }
				Out.ClimbMarkers.Add(Marker.ToString(), Local.GetTranslation());
			}
			else
			{
				Out.Mounts.Add(Name, Local);
			}
		}
		for (int32 Child = 0; Child < Node.GetChildCount(); ++Child)
		{
			if (!ReadNode(*Node.GetChild(Child), Root, Out, File, SupportGroup, bPreflight)) { return false; }
		}
		return true;
	}

	bool FImporter::ReadScene(const FJsonObject& Row, FSceneSource& Out, bool bRoofModule, FName SupportGroup, bool bPreflight)
	{
		const FString File = Directory / Row.GetStringField(TEXT("file"));
		FbxManager* Manager = FbxManager::Create();
		ON_SCOPE_EXIT { Manager->Destroy(); };
		Manager->SetIOSettings(FbxIOSettings::Create(Manager, IOSROOT));
		FbxImporter* Loader = FbxImporter::Create(Manager, "HachimisV4");
		FbxScene* Scene = FbxScene::Create(Manager, "HachimisV4");
		if (!Loader->Initialize(TCHAR_TO_UTF8(*File), -1, Manager->GetIOSettings()) || !Loader->Import(Scene))
		{
			return Fail(File, UTF8_TO_TCHAR(Loader->GetStatus().GetErrorString()));
		}
		int32 UpSign = 0;
		if (!FMath::IsNearlyEqual(Scene->GetGlobalSettings().GetSystemUnit().GetScaleFactor(), CmPerMetre)
			|| Scene->GetGlobalSettings().GetAxisSystem().GetUpVector(UpSign) != FbxAxisSystem::eZAxis || UpSign != 1)
		{
			return Fail(File, TEXT("该交付应以米为单位、+Z 向上"));
		}
		FbxNode* SceneRoot = Scene->GetRootNode();
		FbxNode* Root = SceneRoot->GetChildCount() == 1 ? SceneRoot->GetChild(0) : nullptr;
		if (!Root || Row.GetStringField(TEXT("root")) != UTF8_TO_TCHAR(Root->GetName()))
		{
			return Fail(File, TEXT("FBX 未找到清单指定的唯一作者根"));
		}
		if (bRoofModule)
		{
			const FbxProperty Anchor = Root->FindProperty("assembly_anchor_cm");
			if (!Anchor.IsValid() || Anchor.GetPropertyDataType().GetType() != eFbxDouble3)
			{
				return Fail(File, TEXT("独立屋面根缺少三维作者安装锚点 assembly_anchor_cm"));
			}
			const FbxDouble3 Value = Anchor.Get<FbxDouble3>();
			Out.RoofAnchor = FVector(Value[0], -Value[1], Value[2]);
			if (Out.RoofAnchor.ContainsNaN()) { return Fail(File, TEXT("独立屋面锚点包含非法坐标")); }
		}
		if (!ReadNode(*Root, *Root, Out, File, SupportGroup, bPreflight)) { return false; }
		// 本批次每个作者根作为一件完整网格导入，原建筑批次保持逐节点清单约束。
		if (!SupportGroup.IsNone())
		{
			return Out.Parts.ContainsByPredicate([](const FPartSource& Part) { return !Part.Triangles.IsEmpty(); })
				|| Fail(File, TEXT("作者根缺少可装配的非空三角网格"));
		}
		if (Out.Parts.Num() != Row.GetIntegerField(TEXT("mesh_parts"))
			|| Out.Mounts.Num() + Out.ClimbMarkers.Num() + 1 != Row.GetIntegerField(TEXT("empty_nodes")))
		{
			return Fail(File, FString::Printf(TEXT("实际网格 %d、空节点 %d 和清单不符"), Out.Parts.Num(), Out.Mounts.Num() + Out.ClimbMarkers.Num() + 1));
		}
		return true;
	}

	/** 几何身份不含节点名或安装变换，包含三角顺序、法线、UV 与材质槽。 */
	FString GeometryDigest(const FPartSource& Source)
	{
		FSHA1 Hash;
		for (const FTriangle& Triangle : Source.Triangles)
		{
			for (const FCorner& Corner : Triangle.Corners)
			{
				Hash.Update(reinterpret_cast<const uint8*>(&Corner.Position), sizeof(Corner.Position));
				Hash.Update(reinterpret_cast<const uint8*>(&Corner.Normal), sizeof(Corner.Normal));
				Hash.Update(reinterpret_cast<const uint8*>(&Corner.UV), sizeof(Corner.UV));
			}
			Hash.Update(reinterpret_cast<const uint8*>(&Triangle.Material), sizeof(Triangle.Material));
		}
		for (const UMaterial* Material : Source.Materials)
		{
			const FTCHARToUTF8 Name(*Material->GetName());
			Hash.Update(reinterpret_cast<const uint8*>(Name.Get()), Name.Length() + 1);
		}
		Hash.Final();
		uint8 HashBytes[FSHA1::DigestSize];
		Hash.GetHash(HashBytes);
		return BytesToHex(HashBytes, FSHA1::DigestSize);
	}

	bool FImporter::StorePart(FPartSource& Source, const FString& Folder, const FString& AssetName)
	{
		const FString Digest = GeometryDigest(Source);
		const FString Key = Folder / (AssetName.IsEmpty() ? Digest : AssetName);
		if (UStaticMesh* const* Existing = Meshes.Find(Key)) { Source.Part.Mesh = *Existing; return true; }
		const FString PackageName = Folder / (AssetName.IsEmpty() ? TEXT("SM_") + Digest : AssetName);
		const bool bAlreadySaved = FPackageName::DoesPackageExist(PackageName);
		UStaticMesh* Mesh = CreateOrLoad<UStaticMesh>(PackageName);
		if (!Mesh || Source.Triangles.IsEmpty()) { return Fail(PackageName, TEXT("静态网格缺少资源或三角")); }
		if (!bAlreadySaved || Mesh->bDoFastBuild || Digest == RebuildMesh || !AssetName.IsEmpty())
		{
			// 持久化网格须走编辑器标准构建，FastBuild 的重载路径不会重新计算渲染包围盒。
			FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
			Mesh->bDoFastBuild = false;
			Mesh->GetStaticMaterials().Reset();
			Mesh->SetNumSourceModels(1);
			FMeshBuildSettings& Settings = Mesh->GetSourceModel(0).BuildSettings;
			Settings.bRecomputeNormals = false;
			Settings.bRecomputeTangents = true;
			Settings.bGenerateLightmapUVs = false;
			// 保留 V4 FBX 的浮点 UV，不复用旧半精度构建的 LOD/SDF 派生键。
			// 网格源、部件变换和碰撞不变，生成后的真实 LOD 与距离场都要通过核验。
			Settings.bUseFullPrecisionUVs = true;
			FMeshDescription Description;
			FStaticMeshAttributes Attributes(Description);
			Attributes.Register();
			Attributes.GetVertexInstanceUVs().SetNumChannels(1);
			TArray<FPolygonGroupID> Groups;
			for (UMaterial* Material : Source.Materials)
			{
				const FPolygonGroupID Group = Description.CreatePolygonGroup();
				Groups.Add(Group);
				Attributes.GetPolygonGroupMaterialSlotNames()[Group] = Material->GetFName();
				Mesh->GetStaticMaterials().Emplace(Material, Material->GetFName(), Material->GetFName());
			}
			TMap<FVector3f, FVertexID> Points;
			for (const FTriangle& Triangle : Source.Triangles)
			{
				FVertexInstanceID Instances[3];
				for (int32 Index = 0; Index < 3; ++Index)
				{
					const FCorner& Corner = Triangle.Corners[Index];
					FVertexID* Point = Points.Find(Corner.Position);
					if (!Point)
					{
						const FVertexID NewPoint = Description.CreateVertex();
						Attributes.GetVertexPositions()[NewPoint] = Corner.Position;
						Point = &Points.Add(Corner.Position, NewPoint);
					}
					const FVertexInstanceID Instance = Description.CreateVertexInstance(*Point);
					Instances[Index] = Instance;
					Attributes.GetVertexInstanceNormals()[Instance] = Corner.Normal;
					Attributes.GetVertexInstanceTangents()[Instance] = FVector3f::CrossProduct(
						FMath::Abs(Corner.Normal.Z) < 0.8f ? FVector3f::UpVector : FVector3f::RightVector, Corner.Normal).GetSafeNormal();
					Attributes.GetVertexInstanceBinormalSigns()[Instance] = 1.0f;
					Attributes.GetVertexInstanceUVs().Set(Instance, 0, Corner.UV);
				}
				Description.CreatePolygon(Groups[Triangle.Material], MakeArrayView(Instances));
			}
			Mesh->bHasNavigationData = false;
			Mesh->SetLightMapCoordinateIndex(0);
			Mesh->CreateBodySetup();
			Mesh->GetBodySetup()->CollisionTraceFlag = CTF_UseComplexAsSimple;
			Mesh->CreateMeshDescription(0, MoveTemp(Description));
			UStaticMesh::FCommitMeshDescriptionParams Commit;
			// 已存在网格显式重建时刷新源 GUID，避免命中该次替换前的 DDC 构建结果。
			Commit.bUseHashAsGuid = !bAlreadySaved;
			Mesh->CommitMeshDescription(0, Commit);
			Mesh->Build(false);
			FString Reason;
			if (!HachimisV4MeshValidation::Verify(*Mesh, Reason)) { return Fail(PackageName, Reason); }
			// 结构件使用正式固定名称，由整批发布统一写盘；常规 V4 仍即时保存内容寻址网格。
			if (AssetName.IsEmpty() && !Save(*Mesh)) { return false; }
		}
		Source.Part.Mesh = Mesh;
		Meshes.Add(Key, Mesh);
		return true;
	}

	bool FImporter::ReadSupportScene(FName Family, FName Group, FSceneSource& Out, bool bPreflight)
	{
		const FString AuthorName = Family.ToString() + TEXT("_") + Group.ToString();
		FJsonObject File;
		File.SetStringField(TEXT("file"), TEXT("SM_HC515_") + AuthorName + TEXT(".fbx"));
		File.SetStringField(TEXT("root"), TEXT("ROOT_") + AuthorName);
		return ReadScene(File, Out, false, Group, bPreflight);
	}

	bool FImporter::ImportSupportPart(FName Family, FName Group, const FString& Folder, FCastleV4Part& Out)
	{
		const FString MeshName = TEXT("SM_HC515_") + Family.ToString() + TEXT("_") + Group.ToString();
		FSceneSource Scene;
		if (!ReadSupportScene(Family, Group, Scene, false)) { return false; }
		FPartSource Combined;
		Combined.Part.Node = *MeshName;
		Combined.Part.Group = Group;
		Combined.Part.Local = FTransform::Identity;
		for (const FPartSource& Node : Scene.Parts)
		{
			// 各作者节点的顶点与法线烘焙到共同根；法线按逆转置换算以保留非均匀缩放效果。
			TArray<int32> Slots;
			for (UMaterial* Material : Node.Materials) { Slots.Add(Combined.Materials.AddUnique(Material)); }
			const FTransform& Local = Node.Part.Local;
			for (const FTriangle& Original : Node.Triangles)
			{
				FTriangle& Triangle = Combined.Triangles.Add_GetRef(Original);
				Triangle.Material = Slots[Original.Material];
				for (FCorner& Corner : Triangle.Corners)
				{
					Corner.Position = FVector3f(Local.TransformPosition(FVector(Corner.Position)));
					Corner.Normal = FVector3f(Local.TransformVectorNoScale(FVector(Corner.Normal) / Local.GetScale3D()).GetSafeNormal());
				}
			}
		}
		if (!StorePart(Combined, Folder, MeshName)) { return false; }
		Out = MoveTemp(Combined.Part);
		return true;
	}

	bool FImporter::RunSupportDelivery()
	{
		// 先读取完整交付，验证每份 FBX 的场景和三角网格。哪怕最后一个墙裙缺失，也不触碰任何正式 UObject。
		const FName StepGroups[] = { TEXT("Slope"), TEXT("WallApron"), TEXT("Deck") };
		for (const FName Family : CastlePCGSupport::Families())
		{
			for (const ECastlePCGBraceKind Kind : CastlePCGSupport::BraceKinds())
			{
				FSceneSource Scene;
				if (!ReadSupportScene(Family, CastlePCGSupport::BraceName(Kind), Scene, true)) { return false; }
			}
			for (const FName Group : StepGroups)
			{
				FSceneSource Scene;
				if (!ReadSupportScene(Family, Group, Scene, true)) { return false; }
			}
		}
		const FString RuleSetPath = TEXT("/Game/MovingCastle/Data/DA_CastlePCGRuleSet.DA_CastlePCGRuleSet");
		auto* RuleSet = LoadObject<UCastlePCGRuleSetDataAsset>(nullptr, *RuleSetPath);
		if (!RuleSet) { return Fail(RuleSetPath, TEXT("正式 PCG 规则集不可读")); }
		auto* Braces = NewObject<UCastlePCGOverhangBraceRule>(RuleSet);
		auto* Step = NewObject<UCastlePCGHorizontalLevel2StepRule>(RuleSet);
		const FString BraceFolder = FString(AssetRoot) / TEXT("Overhang");
		const FString StepFolder = FString(AssetRoot) / TEXT("Level2Step");
		for (const FName Family : CastlePCGSupport::Families())
		{
			for (const ECastlePCGBraceKind Kind : CastlePCGSupport::BraceKinds())
			{
				const FName Group = CastlePCGSupport::BraceName(Kind);
				const FString KitPath = BraceFolder / FString::Printf(TEXT("DA_%s_%sKit"), *Family.ToString(), *Group.ToString());
				auto* Kit = CreateOrLoad<UCastlePCGAttachmentKit>(KitPath);
				if (!Kit) { return Fail(KitPath, TEXT("斜撑套件不可读")); }
				Kit->Parts.Reset();
				if (!ImportSupportPart(Family, Group, BraceFolder, Kit->Parts.AddDefaulted_GetRef())) { return false; }
				Braces->FamilyKits.FindOrAdd(Family).ByKind.Add(Kind, Kit);
				AssemblyAssets.Add(Kit);
			}
			const FString SlopePath = StepFolder / (TEXT("DA_") + Family.ToString() + TEXT("_StepSlopeKit"));
			const FString DeckPath = StepFolder / (TEXT("DA_") + Family.ToString() + TEXT("_StepDeckKit"));
			FCastlePCGStepKits& Kits = Step->FamilyKits.Add(Family);
			Kits.Slope = CreateOrLoad<UCastlePCGAttachmentKit>(SlopePath);
			Kits.Deck = CreateOrLoad<UCastlePCGAttachmentKit>(DeckPath);
			if (!Kits.Slope || !Kits.Deck) { return Fail(SlopePath + TEXT(" / ") + DeckPath, TEXT("坡顶或底盘套件不可读")); }
			Kits.Slope->Parts.Reset();
			Kits.Deck->Parts.Reset();
			if (!ImportSupportPart(Family, TEXT("Slope"), StepFolder, Kits.Slope->Parts.AddDefaulted_GetRef())
				|| !ImportSupportPart(Family, TEXT("WallApron"), StepFolder, Kits.Slope->Parts.AddDefaulted_GetRef())
				|| !ImportSupportPart(Family, TEXT("Deck"), StepFolder, Kits.Deck->Parts.AddDefaulted_GetRef()))
			{
				return false;
			}
			AssemblyAssets.Add(Kits.Slope);
			AssemblyAssets.Add(Kits.Deck);
		}
		// 重跑导入只替换这两种规则实例，其他规则和资产参数保持原样。
		RuleSet->Rules.RemoveAll([](const TObjectPtr<UCastleBuildingPCGRule>& Rule)
		{
			return Rule && (Rule->IsA<UCastlePCGOverhangBraceRule>() || Rule->IsA<UCastlePCGHorizontalLevel2StepRule>());
		});
		RuleSet->Rules.Add(Braces);
		RuleSet->Rules.Add(Step);
		FString Error;
		if (!RuleSet->ValidateConfiguration(Error)) { return Fail(RuleSetPath, Error); }
		// 缺少任何 FBX、材质不一致或网格校验失败，均在此发布边界之前返回。
		TArray<UObject*> Assets;
		for (const auto& Entry : Palette) { Assets.Add(Entry.Value); }
		for (const auto& Entry : Meshes) { Assets.Add(Entry.Value); }
		Assets.Append(AssemblyAssets);
		Assets.Add(RuleSet);
		if (!SaveSupportDelivery(Assets)) { return false; }
		UE_LOG(LogHachimisV4, Display, TEXT("正式规则集已绑定 %d 个结构网格、%d 个套件。"), Meshes.Num(), AssemblyAssets.Num());
		return true;
	}

	/** 拦截 FBX SDK 的媒体提取，原始目录严格只读，意外的内嵌文件直接令此交付失败。 */
	FbxCallback::State ReadShieldRoofImage(void* Owner, FbxClassId Type, const char* Name, const void* Bytes, size_t Size)
	{
		auto& Palette = *static_cast<FShieldRoofPaletteSource*>(Owner);
		if (Type != FbxVideo::ClassId || !Name || FPaths::GetCleanFilename(UTF8_TO_TCHAR(Name)) != TEXT("T_HC515_ShieldRoof_BaseColor.png")
			|| !Bytes || Size == 0 || !Palette.Png.IsEmpty())
		{
			return FbxCallback::eFailure;
		}
		Palette.Png.Append(static_cast<const uint8*>(Bytes), Size);
		return FbxCallback::eHandled;
	}

	bool FImporter::ReadQuadRoofPart(FName Family, FName Group, FPartSource& Out, FShieldRoofPaletteSource* ShieldPalette)
	{
		const FString MeshName = TEXT("SM_HC515_L4H_") + Family.ToString() + TEXT("_") + Group.ToString();
		const FString SourceDir = ShieldPalette ? Directory : Directory / TEXT("FBX") / Family.ToString();
		const FString File = SourceDir / (MeshName + TEXT(".fbx"));
		FbxManager* Manager = FbxManager::Create();
		ON_SCOPE_EXIT { Manager->Destroy(); };
		Manager->SetIOSettings(FbxIOSettings::Create(Manager, IOSROOT));
		FbxImporter* Loader = FbxImporter::Create(Manager, "QuadRoofOverlay");
		FbxScene* Scene = FbxScene::Create(Manager, "QuadRoofOverlay");
		if (!Loader->Initialize(TCHAR_TO_UTF8(*File), -1, Manager->GetIOSettings()))
		{
			return Fail(File, UTF8_TO_TCHAR(Loader->GetStatus().GetErrorString()));
		}
		Manager->GetIOSettings()->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, ShieldPalette != nullptr);
		if (ShieldPalette)
		{
			auto* Callback = FbxEmbeddedFileCallback::Create(Manager, "ShieldRoofImage");
			if (Callback->RegisterReadFunction(&ReadShieldRoofImage, ShieldPalette) < 0)
			{
				return Fail(File, TEXT("无法注册盾塔基础色内嵌贴图读取回调"));
			}
			Loader->SetEmbeddedFileReadCallback(Callback);
		}
		if (!Loader->Import(Scene))
		{
			return Fail(File, FString(TEXT("FBX 或内嵌媒体导入失败：")) + UTF8_TO_TCHAR(Loader->GetStatus().GetErrorString()));
		}
		int32 UpSign = 0;
		if (!FMath::IsNearlyEqual(Scene->GetGlobalSettings().GetSystemUnit().GetScaleFactor(), CmPerMetre)
			|| Scene->GetGlobalSettings().GetAxisSystem().GetUpVector(UpSign) != FbxAxisSystem::eZAxis || UpSign != 1)
		{
			return Fail(File, TEXT("四格叠层屋面应以米为单位、+Z 向上"));
		}

		// 此批次以 FBX 场景为根，视觉网格和 UCX 平级。UCX 专供外部建模，绝不能进入 UE 网格描述。
		FbxNode& Root = *Scene->GetRootNode();
		FbxNode* Visual = nullptr;
		for (int32 Index = 0; Index < Root.GetChildCount(); ++Index)
		{
			FbxNode* Node = Root.GetChild(Index);
			const FString Name = UTF8_TO_TCHAR(Node->GetName());
			if (!Node->GetMesh() || Node->GetChildCount() != 0)
			{
				return Fail(File, Name + TEXT(" 应为场景根下的独立网格"));
			}
			if (Name.StartsWith(TEXT("UCX_"), ESearchCase::CaseSensitive)) { continue; }
			if (Visual || Name != MeshName)
			{
				return Fail(File, Name + TEXT(" 不是唯一同名的作者视觉网格 ") + MeshName);
			}
			Visual = Node;
		}
		const TCHAR* ExpectedPalette = ShieldPalette ? TEXT("M_HC515_ShieldRoofPalette") : TEXT("M_HC515_RoofPalette");
		if (!Visual || Visual->GetMaterialCount() != 1 || !Visual->GetMaterial(0)
			|| FString(UTF8_TO_TCHAR(Visual->GetMaterial(0)->GetName())) != ExpectedPalette)
		{
			return Fail(File, MeshName + TEXT(" 缺少唯一视觉网格或调色板材质槽 ") + ExpectedPalette);
		}
		if (ShieldPalette)
		{
			FbxSurfacePhong* Phong = FbxCast<FbxSurfacePhong>(Visual->GetMaterial(0));
			if (!Phong || Phong->Diffuse.GetSrcObjectCount<FbxFileTexture>() != 1
				|| Scene->GetTextureCount() != 1 || ShieldPalette->Png.IsEmpty())
			{
				return Fail(File, TEXT("盾塔调色板须为 Phong 且仅含一张内嵌基础色 PNG"));
			}
			// 反解 Blender FBX 导出器，与 V4 盾塔原材质的换算一致。
			const double Shine = Phong->Shininess.Get();
			ShieldPalette->Metallic = Phong->ReflectionFactor.Get();
			ShieldPalette->Specular = Phong->SpecularFactor.Get() * 2.0;
			const auto Glow = Phong->Emissive.Get();
			const double Intensity = Phong->EmissiveFactor.Get();
			ShieldPalette->Emissive = FLinearColor(Glow[0] * Intensity, Glow[1] * Intensity, Glow[2] * Intensity);
			if (!(Shine >= 0.0 && Shine <= 100.0 && ShieldPalette->Metallic >= 0.0 && ShieldPalette->Metallic <= 1.0
				&& ShieldPalette->Specular >= 0.0 && ShieldPalette->Specular <= 1.0 && Intensity == 0.0))
			{
				return Fail(File, TEXT("盾塔调色板的 Phong 光泽、高光、金属度或自发光与交付约定不符"));
			}
			ShieldPalette->Roughness = 1.0 - FMath::Sqrt(Shine) / 10.0;
		}
		// 视觉节点的控制点已经以四格中心为作者原点。以节点自身为根，去掉导出器写入的 Z 轴半圈，
		// 再复用 V4 的 Y 镜像、绕序、法线与 UV 读取。共享调色板随后显式注入。
		FSceneSource Source;
		if (!ReadNode(*Visual, *Visual, Source, File, Group, true)) { return false; }
		if (Source.Parts.Num() != 1 || Source.Parts[0].Triangles.IsEmpty())
		{
			return Fail(File, MeshName + TEXT(" 缺少非空视觉三角"));
		}
		Out = MoveTemp(Source.Parts[0]);
		return true;
	}

	bool FImporter::StoreQuadRoofKit(FName Family, ECastlePCGQuadRoofKind Kind, FPartSource& Part, UMaterial& Material,
		const FString& Folder, UCastlePCGQuadRoofOverlayRule& Rule, TArray<UObject*>& Assets)
	{
		// 套件只引用这个作者视觉节点，保留其局部原点用于旋转占格掩码。
		Part.Materials.Add(&Material);
		if (!StorePart(Part, Folder, Part.Part.Node.ToString())) { return false; }
		const FString KitPath = Folder / FString::Printf(TEXT("DA_%s_QuadRoof_%sKit"), *Family.ToString(), *Part.Part.Group.ToString());
		auto* Kit = CreateOrLoad<UCastlePCGAttachmentKit>(KitPath);
		if (!Kit) { return Fail(KitPath, TEXT("四格屋面套件不可读")); }
		Assets.Add(Part.Part.Mesh.Get());
		Kit->Parts = {MoveTemp(Part.Part)};
		Rule.FamilyKits.FindOrAdd(Family).ByKind.Add(Kind, Kit);
		Assets.Add(Kit);
		return true;
	}

	bool FImporter::RunShieldQuadRoofDelivery()
	{
		const FName Family(TEXT("Shield"));
		const FString Folder = FString(AssetRoot) / TEXT("QuadRoofOverlay");
		const FString RuleSetPath = TEXT("/Game/MovingCastle/Data/DA_CastlePCGRuleSet.DA_CastlePCGRuleSet");
		FShieldRoofPaletteSource PaletteSource;
		TMap<ECastlePCGQuadRoofKind, FPartSource> Sources;
		for (const ECastlePCGQuadRoofKind Kind : CastlePCGSupport::QuadRoofKinds())
		{
			const FName Name = CastlePCGSupport::QuadRoofName(Kind);
			FShieldRoofPaletteSource Current;
			if (!ReadQuadRoofPart(Family, Name, Sources.Add(Kind), &Current)) { return false; }
			if (PaletteSource.Png.IsEmpty()) { PaletteSource = MoveTemp(Current); }
			else if (Current != PaletteSource)
			{
				return Fail(Directory / (TEXT("SM_HC515_L4H_Shield_") + Name.ToString() + TEXT(".fbx")),
					TEXT("盾塔四款 FBX 的内嵌 PNG 或材质参数不一致"));
			}
		}
		FImage BaseImage;
		if (!FImageUtils::DecompressImage(PaletteSource.Png.GetData(), PaletteSource.Png.Num(), BaseImage) || BaseImage.SizeX != 1024
			|| BaseImage.SizeY != 512 || BaseImage.NumSlices != 1 || BaseImage.RawData.IsEmpty())
		{
			return Fail(Directory / TEXT("T_HC515_ShieldRoof_BaseColor.png"), TEXT("无法解码四个盾塔 FBX 内嵌的 1024×512 PNG"));
		}
		auto* RuleSet = LoadObject<UCastlePCGRuleSetDataAsset>(nullptr, *RuleSetPath);
		if (!RuleSet) { return Fail(RuleSetPath, TEXT("正式 PCG 规则集不可读")); }
		const int32 RuleIndex = RuleSet->Rules.IndexOfByPredicate([](const auto& Rule)
		{
			return Rule && Rule->template IsA<UCastlePCGQuadRoofOverlayRule>();
		});
		if (RuleIndex == INDEX_NONE) { return Fail(RuleSetPath, TEXT("缺少需安装盾塔套件的四格叠层规则")); }
		auto* PreviousRule = CastChecked<UCastlePCGQuadRoofOverlayRule>(RuleSet->Rules[RuleIndex].Get());
		auto* Rule = NewObject<UCastlePCGQuadRoofOverlayRule>(RuleSet);
		Rule->FamilyKits = PreviousRule->FamilyKits;

		const FString TexturePath = Folder / TEXT("T_HC515_ShieldRoof_BaseColor");
		const FString MaterialPath = Folder / TEXT("M_HC515_ShieldRoofPalette");
		auto* Texture = CreateOrLoad<UTexture2D>(TexturePath);
		auto* Material = CreateOrLoad<UMaterial>(MaterialPath);
		if (!Texture || !Material) { return Fail(MaterialPath, TEXT("盾塔调色板材质或贴图资源不可读")); }
		Texture->Source.Init(FImageView(BaseImage, BaseImage.RawData.GetData()));
		Texture->SRGB = true;
		Texture->CompressionSettings = TC_Default;
		Texture->PostEditChange();

		// 交付只有颜色图集，光泽、高光、金属度及自发光使用已验证的作者参数。
		FMaterialExpressionCollection& Expressions = Material->GetExpressionCollection();
		Expressions.Empty();
		auto* BaseColor = NewObject<UMaterialExpressionTextureSample>(Material);
		BaseColor->Texture = Texture;
		BaseColor->SamplerType = SAMPLERTYPE_Color;
		Expressions.AddExpression(BaseColor);
		UMaterialEditorOnlyData* Inputs = Material->GetEditorOnlyData();
		Inputs->BaseColor.UseConstant = false;
		Inputs->BaseColor.Connect(0, BaseColor);
		Inputs->Roughness.UseConstant = Inputs->Metallic.UseConstant = Inputs->Specular.UseConstant = Inputs->EmissiveColor.UseConstant = true;
		Inputs->Roughness.Constant = PaletteSource.Roughness;
		Inputs->Metallic.Constant = PaletteSource.Metallic;
		Inputs->Specular.Constant = PaletteSource.Specular;
		Inputs->EmissiveColor.Constant = PaletteSource.Emissive.ToFColorSRGB();
		Material->PostEditChange();
		if (GShaderCompilingManager) { GShaderCompilingManager->FinishAllCompilation(); }
		if (const FMaterialResource* Resource = Material->GetMaterialResource(SP_PCD3D_SM6);
			Resource && !Resource->GetCompileErrors().IsEmpty())
		{
			return Fail(MaterialPath, FString::Join(Resource->GetCompileErrors(), TEXT("\n")));
		}

		TArray<UObject*> Assets = {Texture, Material};
		Rule->FamilyKits.Remove(Family);
		for (const ECastlePCGQuadRoofKind Kind : CastlePCGSupport::QuadRoofKinds())
		{
			if (!StoreQuadRoofKit(Family, Kind, Sources.FindChecked(Kind), *Material, Folder, *Rule, Assets)) { return false; }
		}
		// 原子发布保留规则插槽和其他家族的引用。失败时恢复编辑器中的原规则引用。
		RuleSet->Rules[RuleIndex] = Rule;
		bool bPublished = false;
		ON_SCOPE_EXIT { if (!bPublished) { RuleSet->Rules[RuleIndex] = PreviousRule; } };
		FString Error;
		if (!RuleSet->ValidateConfiguration(Error)) { return Fail(RuleSetPath, Error); }
		Assets.Add(RuleSet);
		if (!SaveSupportDelivery(Assets)) { return false; }
		bPublished = true;
		UE_LOG(LogHachimisV4, Display, TEXT("已发布 4 款盾塔四格叠层屋面、基础色图集、专用调色板及规则集。"));
		return true;
	}

	bool FImporter::RunQuadRoofDelivery()
	{
		const FName Shield(TEXT("Shield"));
		const FString Folder = FString(AssetRoot) / TEXT("QuadRoofOverlay");
		const FString RuleSetPath = TEXT("/Game/MovingCastle/Data/DA_CastlePCGRuleSet.DA_CastlePCGRuleSet");
		const FString Names[] = {TEXT("T_HC515_Roof_BaseColor"), TEXT("T_HC515_Roof_ORM")};
		FImage Images[2];
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Images); ++Index)
		{
			const FString File = Directory / TEXT("Textures") / (Names[Index] + TEXT(".png"));
			if (!FImageUtils::LoadImage(*File, Images[Index]) || Images[Index].SizeX <= 0 || Images[Index].SizeY <= 0
				|| Images[Index].NumSlices != 1 || Images[Index].RawData.IsEmpty())
			{
				return Fail(File, TEXT("调色板 PNG 缺失、无法解码或尺寸非法"));
			}
		}

		// 共享调色板交付只迭代这五族，盾塔套件从现有正式规则保留。
		struct FRoofSource
		{
			FName Family;
			ECastlePCGQuadRoofKind Kind;
			FPartSource Part;
		};
		TArray<FRoofSource> Sources;
		for (const FName Family : CastlePCGSupport::Families())
		{
			if (Family == Shield) { continue; }
			for (const ECastlePCGQuadRoofKind Kind : CastlePCGSupport::QuadRoofKinds())
			{
				FRoofSource& Source = Sources.AddDefaulted_GetRef();
				Source.Family = Family;
				Source.Kind = Kind;
				if (!ReadQuadRoofPart(Family, CastlePCGSupport::QuadRoofName(Kind), Source.Part)) { return false; }
			}
		}
		auto* RuleSet = LoadObject<UCastlePCGRuleSetDataAsset>(nullptr, *RuleSetPath);
		if (!RuleSet) { return Fail(RuleSetPath, TEXT("正式 PCG 规则集不可读")); }
		const int32 RuleIndex = RuleSet->Rules.IndexOfByPredicate([](const auto& Rule)
		{
			return Rule && Rule->template IsA<UCastlePCGQuadRoofOverlayRule>();
		});
		if (RuleIndex == INDEX_NONE) { return Fail(RuleSetPath, TEXT("缺少正式四格叠层屋面规则")); }
		auto* PreviousRule = CastChecked<UCastlePCGQuadRoofOverlayRule>(RuleSet->Rules[RuleIndex].Get());
		const auto* ShieldKits = PreviousRule->FamilyKits.Find(Shield);
		if (!ShieldKits) { return Fail(RuleSetPath, TEXT("缺少应保留的盾塔专用四格屋面套件")); }
		auto* Rule = NewObject<UCastlePCGQuadRoofOverlayRule>(RuleSet);
		Rule->FamilyKits.Add(Shield, *ShieldKits);

		TArray<UObject*> Assets;
		UTexture2D* Textures[UE_ARRAY_COUNT(Images)] = {};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Images); ++Index)
		{
			const FString Path = Folder / Names[Index];
			UTexture2D* Texture = CreateOrLoad<UTexture2D>(Path);
			if (!Texture) { return Fail(Path, TEXT("调色板贴图包不可读")); }
			const FImageView Pixels(Images[Index], Images[Index].RawData.GetData());
			Texture->Source.Init(Pixels);
			// ORM 的每个字节都表示线性系数。只把基础色解释为 sRGB。
			Texture->SRGB = Index == 0;
			Texture->CompressionSettings = Index == 0 ? TC_Default : TC_Masks;
			Texture->PostEditChange();
			Textures[Index] = Texture;
			Assets.Add(Texture);
		}
		const FString MaterialPath = Folder / TEXT("M_HC515_RoofPalette");
		UMaterial* Material = CreateOrLoad<UMaterial>(MaterialPath);
		if (!Material) { return Fail(MaterialPath, TEXT("屋面调色板材质包不可读")); }
		FMaterialExpressionCollection& Expressions = Material->GetExpressionCollection();
		Expressions.Empty();
		auto* BaseColor = NewObject<UMaterialExpressionTextureSample>(Material);
		BaseColor->Texture = Textures[0];
		BaseColor->SamplerType = SAMPLERTYPE_Color;
		Expressions.AddExpression(BaseColor);
		auto* ORM = NewObject<UMaterialExpressionTextureSample>(Material);
		ORM->Texture = Textures[1];
		ORM->SamplerType = SAMPLERTYPE_Masks;
		ORM->MaterialExpressionEditorY = 240;
		Expressions.AddExpression(ORM);
		UMaterialEditorOnlyData* Inputs = Material->GetEditorOnlyData();
		Inputs->BaseColor.UseConstant = Inputs->AmbientOcclusion.UseConstant = Inputs->Roughness.UseConstant = Inputs->Metallic.UseConstant = false;
		Inputs->BaseColor.Connect(0, BaseColor);
		Inputs->AmbientOcclusion.Connect(1, ORM);
		Inputs->Roughness.Connect(2, ORM);
		Inputs->Metallic.Connect(3, ORM);
		Material->PostEditChange();
		if (GShaderCompilingManager) { GShaderCompilingManager->FinishAllCompilation(); }
		// 按项目 Windows 默认目标 DX12 SM6 核对材质编译结果。
		if (const FMaterialResource* Resource = Material->GetMaterialResource(SP_PCD3D_SM6);
			Resource && !Resource->GetCompileErrors().IsEmpty())
		{
			return Fail(MaterialPath, FString::Join(Resource->GetCompileErrors(), TEXT("\n")));
		}
		Assets.Add(Material);

		for (FRoofSource& Source : Sources)
		{
			if (!StoreQuadRoofKit(Source.Family, Source.Kind, Source.Part, *Material, Folder, *Rule, Assets)) { return false; }
		}
		// 更新规则集中的原插槽，保留其他结构规则的执行顺序。
		RuleSet->Rules[RuleIndex] = Rule;
		bool bPublished = false;
		ON_SCOPE_EXIT { if (!bPublished) { RuleSet->Rules[RuleIndex] = PreviousRule; } };
		FString Error;
		if (!RuleSet->ValidateConfiguration(Error)) { return Fail(RuleSetPath, Error); }
		Assets.Add(RuleSet);
		if (!SaveSupportDelivery(Assets)) { return false; }
		bPublished = true;
		UE_LOG(LogHachimisV4, Display, TEXT("正式规则集已绑定 %d 套四格屋面和共享调色板，正式包共 %d 个。"), Sources.Num(), Assets.Num());
		return true;
	}

	/** 剪切底盘时插值 UV 和法线。只出现在确实跨过格界的作者零件上。 */
	void Clip(TArray<FCorner>& Polygon, const FTransform& Local, int32 Axis, double Boundary, double Sign)
	{
		TArray<FCorner> Input = MoveTemp(Polygon);
		if (Input.IsEmpty()) { return; }
		FCorner Previous = Input.Last();
		auto Distance = [&](const FCorner& C) { return (Local.TransformPosition(FVector(C.Position))[Axis] - Boundary) * Sign; };
		double PrevDistance = Distance(Previous);
		for (const FCorner& Current : Input)
		{
			const double NextDistance = Distance(Current);
			if ((NextDistance >= 0) != (PrevDistance >= 0))
			{
				const float Alpha = PrevDistance / (PrevDistance - NextDistance);
				Polygon.Add({FMath::Lerp(Previous.Position, Current.Position, Alpha),
					FMath::Lerp(Previous.Normal, Current.Normal, Alpha).GetSafeNormal(), FMath::Lerp(Previous.UV, Current.UV, Alpha)});
			}
			if (NextDistance >= 0) { Polygon.Add(Current); }
			Previous = Current;
			PrevDistance = NextDistance;
		}
	}

	bool FImporter::ReadRoofQuarter(const FString& File, const FString& Name, FName Family, FPartSource& Out)
	{
		// 四分 FBX 的局部枢轴来自放置表。单网格节点自身只承载 FBX 轴转换。
		FbxManager* Manager = FbxManager::Create();
		ON_SCOPE_EXIT { Manager->Destroy(); };
		Manager->SetIOSettings(FbxIOSettings::Create(Manager, IOSROOT));
		auto* Loader = FbxImporter::Create(Manager, "RoofQuarter");
		auto* Scene = FbxScene::Create(Manager, "RoofQuarter");
		if (!Loader->Initialize(TCHAR_TO_UTF8(*File), -1, Manager->GetIOSettings()))
		{
			return Fail(File, UTF8_TO_TCHAR(Loader->GetStatus().GetErrorString()));
		}
		Manager->GetIOSettings()->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, false);
		if (!Loader->Import(Scene)) { return Fail(File, UTF8_TO_TCHAR(Loader->GetStatus().GetErrorString())); }
		auto* Root = Scene->GetRootNode();
		auto* Node = Root && Root->GetChildCount() == 1 ? Root->GetChild(0) : nullptr;
		int32 UpSign = 0;
		if (!Node || Name != UTF8_TO_TCHAR(Node->GetName()) || !Node->GetMesh() || Node->GetChildCount() != 0
			|| !FMath::IsNearlyEqual(Scene->GetGlobalSettings().GetSystemUnit().GetScaleFactor(), CmPerMetre)
			|| Scene->GetGlobalSettings().GetAxisSystem().GetUpVector(UpSign) != FbxAxisSystem::eZAxis || UpSign != 1)
		{
			return Fail(File, TEXT("屋角 FBX 须含唯一同名静态网格、米单位和 +Z 向上：") + Name);
		}
		FSceneSource Source;
		if (!ReadNode(*Node, *Node, Source, File, TEXT("RoofQuarter"), true)) { return false; }
		if (Source.Parts.Num() != 1 || Source.Parts[0].Triangles.IsEmpty()) { return Fail(File, TEXT("屋角无有效三角")); }
		Out = MoveTemp(Source.Parts[0]);
		const FString Materials = FString(AssetRoot) / (Family == TEXT("Shield") ? TEXT("Shield/Materials") : TEXT("Materials"));
		for (int32 Index = 0; Index < Node->GetMaterialCount(); ++Index)
		{
			const auto* Slot = Node->GetMaterial(Index);
			if (!Slot) { return Fail(File, FString::Printf(TEXT("缺少材质槽 %d"), Index)); }
			const FString MaterialName = UTF8_TO_TCHAR(Slot->GetName());
			const FString Path = Materials / MaterialName;
			auto* Material = LoadObject<UMaterial>(nullptr, *(Path + TEXT(".") + MaterialName));
			if (!Material) { return Fail(File, TEXT("缺少现有 V4 材质 ") + Path); }
			Out.Materials.Add(Material);
		}
		return true;
	}

	/** 对称网格直接烘焙到顶点，奇数次反射同时修正绕序，运行时组件保持单位缩放。 */
	FPartSource MirrorRoofQuarter(const FPartSource& Source, int32 X, int32 Y)
	{
		FPartSource Mesh = Source;
		for (FTriangle& Triangle : Mesh.Triangles)
		{
			for (FCorner& Corner : Triangle.Corners)
			{
				Corner.Position.X *= X;
				Corner.Position.Y *= Y;
				Corner.Normal.X *= X;
				Corner.Normal.Y *= Y;
			}
			if (X * Y < 0) { Swap(Triangle.Corners[1], Triangle.Corners[2]); }
		}
		return Mesh;
	}

	/** 在作者枢轴坐标中切到棋盘 Y=0，保留既有三角材质、法线与 UV。 */
	FPartSource SliceFarmRoof(const FPartSource& Source, double PivotY, int32 Side)
	{
		FPartSource Result;
		Result.Part = Source.Part;
		Result.Materials = Source.Materials;
		for (const FTriangle& Triangle : Source.Triangles)
		{
			TArray<FCorner> Polygon(Triangle.Corners, 3);
			Clip(Polygon, FTransform::Identity, 1, -PivotY, Side);
			for (int32 I = 1; I + 1 < Polygon.Num(); ++I)
			{
				const FVector3f Area = FVector3f::CrossProduct(Polygon[I].Position - Polygon[0].Position,
					Polygon[I + 1].Position - Polygon[0].Position);
				if (!Area.IsNearlyZero()) { Result.Triangles.Add({{Polygon[0], Polygon[I], Polygon[I + 1]}, Triangle.Material}); }
			}
		}
		return Result;
	}

	bool FImporter::RunRoofQuarterDelivery(const FString& DirectionalDirectory)
	{
		using namespace CastlePCGRoofQuarters;
		const FString ManifestFile = Directory / TEXT("PCG_屋顶放置表.json");
		FString Json;
		TSharedPtr<FJsonObject> Manifest;
		const TSharedPtr<FJsonObject>* Rows = nullptr;
		double Schema = 0.0;
		double Pitch = 0.0;
		const auto Keys = FormKeys();
		if (!FFileHelper::LoadFileToString(Json, *ManifestFile)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Manifest) || !Manifest
			|| !Manifest->TryGetNumberField(TEXT("schema"), Schema) || Schema != 1
			|| !Manifest->TryGetNumberField(TEXT("pitch_cm"), Pitch) || Pitch != UCastleHachimisV4Layout::CellPitch
			|| !Manifest->TryGetObjectField(TEXT("forms"), Rows) || (*Rows)->Values.Num() != Keys.Num())
		{
			return Fail(ManifestFile, TEXT("四分屋顶放置表版本、节距或造型集合无效"));
		}
		const FString RuleSetPath = TEXT("/Game/MovingCastle/Data/DA_CastlePCGRuleSet.DA_CastlePCGRuleSet");
		auto* RuleSet = LoadObject<UCastlePCGRuleSetDataAsset>(nullptr, *RuleSetPath);
		if (!RuleSet) { return Fail(RuleSetPath, TEXT("正式规则集不可读取")); }
		auto* Rule = NewObject<UCastlePCGRoofQuarterRule>(RuleSet);
		TArray<UObject*> Assets;
		double MaxError = 0.0;
		int32 DirectionalMeshes = 0;
		int32 SymmetricMeshes = 0;
		for (const FName Key : Keys)
		{
			const FString Name = Key.ToString();
			FString Family, FormName;
			Name.Split(TEXT("_"), &Family, &FormName);
			const TSharedPtr<FJsonObject>* Row = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Coordinates = nullptr;
			const TSharedPtr<FJsonObject>* Variants = nullptr;
			FString DeclaredFamily, DeclaredForm, Selected;
			FVector Pivot;
			if (!(*Rows)->TryGetObjectField(Name, Row) || !(*Row)->TryGetStringField(TEXT("family"), DeclaredFamily)
				|| !(*Row)->TryGetStringField(TEXT("form"), DeclaredForm) || DeclaredFamily != Family || DeclaredForm != FormName
				|| !(*Row)->TryGetArrayField(TEXT("placement_anchor_blender_xyz_cm"), Coordinates) || Coordinates->Num() != 3
				|| !(*Coordinates)[0]->TryGetNumber(Pivot.X) || !(*Coordinates)[1]->TryGetNumber(Pivot.Y)
				|| !(*Coordinates)[2]->TryGetNumber(Pivot.Z) || Pivot.ContainsNaN()
				|| !(*Row)->TryGetStringField(TEXT("selected_quadrant"), Selected)
				|| !(*Row)->TryGetObjectField(TEXT("variants"), Variants))
			{
				return Fail(ManifestFile, Name + TEXT(" 的家族、造型、商品锚点或变体字段无效"));
			}
			Pivot.Y = -Pivot.Y;
			const FString LayoutName = TEXT("DA_") + Name;
			const FString LayoutPath = FString(AssetRoot) / Family / LayoutName;
			auto* Layout = LoadObject<UCastleHachimisV4Layout>(nullptr, *(LayoutPath + TEXT(".") + LayoutName));
			if (!Layout || FormKey(*Layout) != Key || Layout->bChassisOnly
				|| Layout->Grid.X < 1 || Layout->Grid.X > 2 || Layout->Grid.Y < 1 || Layout->Grid.Y > 2
				|| Layout->Grid.Z < 1 || Layout->Grid.Z > 2)
			{
				return Fail(LayoutPath, TEXT("缺少对应作者屋顶布局或尺寸无效"));
			}
			auto& Form = Rule->Forms.Add(Key);
			Form.Layout = Layout;
			const bool bDirectional = Family == TEXT("Farm") || Name == TEXT("Room_L4") || Name == TEXT("Shield_L5")
				|| Name == TEXT("Balloon_L3H") || Name == TEXT("Balloon_L3V");
			if (!bDirectional && Selected != TEXT("XP_YP")) { return Fail(ManifestFile, Name + TEXT(" 的对称代表片不在 XP_YP")); }
			const FString Folder = FString(AssetRoot) / TEXT("RoofQuarters") / Family;
			FBox Combined(ForceInit);
			for (const int32 X : {1, -1})
			for (const int32 Y : {-1, 1})
			{
				// 作者命名按 Blender Y 编号，归属和收檐方向都按换算后的 UE Y 决定。
				const FString Quadrant = FString::Printf(TEXT("X%s_Y%s"), X > 0 ? TEXT("P") : TEXT("N"), Y < 0 ? TEXT("P") : TEXT("N"));
				const bool bSplit = RequiresFarmSplit(Key) && Y > 0;
				const int32 FirstPiece = Form.Pieces.Num();
				for (int32 Section = 0; Section < (bSplit ? 2 : 1); ++Section)
				{
					const int32 OwnerY = bSplit && Section == 0 ? -1 : Y;
					auto& Piece = Form.Pieces.AddDefaulted_GetRef();
					Piece.Slot = FName(Quadrant + (bSplit ? (Section == 0 ? TEXT("_Back") : TEXT("_Front")) : TEXT("")));
					Piece.Owner = FIntVector(X > 0 ? Layout->Grid.X - 1 : 0, OwnerY > 0 ? Layout->Grid.Y - 1 : 0, Layout->Grid.Z - 1);
					Piece.Outward = FIntPoint(X, OwnerY);
					Piece.PivotInOwner = Pivot - Layout->CellBottom(Piece.Owner);
				}
				for (uint8 Bits = 0; Bits < 4; ++Bits)
				{
					const auto State = static_cast<ECastlePCGRoofQuarterState>(Bits);
					const TCHAR* StateText = StateName(State);
					FString FbxName, File;
					if (bDirectional)
					{
						FbxName = FString::Printf(TEXT("SM_RQ_%s_%s_%s"), *Name, *Quadrant, StateText);
						File = DirectionalDirectory / (FbxName + TEXT(".fbx"));
					}
					else
					{
						const TSharedPtr<FJsonObject>* Variant = nullptr;
						if (!(*Variants)->TryGetObjectField(StateText, Variant)
							|| !(*Variant)->TryGetStringField(TEXT("mesh"), FbxName) || FbxName != FString::Printf(TEXT("SM_RQ_%s_%s"), *Name, StateText)
							|| !(*Variant)->TryGetStringField(TEXT("file"), File)
							|| File != FString::Printf(TEXT("FBX/%s/%s/%s.fbx"), *Family, *FormName, *FbxName))
						{
							return Fail(ManifestFile, Name + TEXT(" 的代表片路径非法：") + StateText);
						}
						File = Directory / File;
					}
					FPartSource Source;
					if (!ReadRoofQuarter(File, FbxName, FName(Family), Source)) { return false; }
					if (!bDirectional) { Source = MirrorRoofQuarter(Source, X, -Y); }
					for (int32 Section = 0; Section < (bSplit ? 2 : 1); ++Section)
					{
						auto& Piece = Form.Pieces[FirstPiece + Section];
						FPartSource Part = bSplit ? SliceFarmRoof(Source, Pivot.Y, Section == 0 ? -1 : 1) : MoveTemp(Source);
						const FString Stem = FString::Printf(TEXT("%s_%s_%s"), *Name, *Piece.Slot.ToString(), StateText);
						Part.Part.Node = FName(TEXT("SM_RQ_") + Stem);
						const FBox World = Bounds(Part).ShiftBy(Pivot);
						const auto Center = Layout->CellBottom(Piece.Owner);
						const int32 YSide = Piece.Outward.Y;
						if (Part.Triangles.IsEmpty() || ((Bits & 1) && (X > 0 ? World.Max.X - Center.X : Center.X - World.Min.X) > Pitch / 2.0 + 0.05)
							|| ((Bits & 2) && (YSide > 0 ? World.Max.Y - Center.Y : Center.Y - World.Min.Y) > Pitch / 2.0 + 0.05))
						{
							return Fail(File, Stem + TEXT(" 没有有效几何或收檐边超过所属格边界"));
						}
						if (Bits == 0) { Combined += World; }
						if (!StorePart(Part, Folder, Part.Part.Node.ToString())) { return false; }
						const FString KitPath = Folder / (TEXT("DA_RQ_") + Stem + TEXT("Kit"));
						auto* Kit = CreateOrLoad<UCastlePCGAttachmentKit>(KitPath);
						if (!Kit) { return Fail(KitPath, TEXT("四分屋顶套件不可读取")); }
						Assets.Add(Part.Part.Mesh.Get());
						Kit->Parts = {MoveTemp(Part.Part)};
						Assets.Add(Kit);
						Piece.Variants.Add(State, Kit);
						if (bDirectional) { ++DirectionalMeshes; }
						else { ++SymmetricMeshes; }
					}
				}
			}

			// 原装屋面按源顶点变换后并集，避免旋转零件的 AABB 八角扩大装配包围盒。
			FBox Original(ForceInit);
			for (const auto& Part : Layout->Parts)
			{
				if (Part.Group != TEXT("Roof") && Part.Group != TEXT("RoofKeys")) { continue; }
				const FMeshDescription* Mesh = Part.Mesh ? Part.Mesh->GetMeshDescription(0) : nullptr;
				if (!Mesh) { return Fail(LayoutPath, Part.Node.ToString() + TEXT(" 的原屋面缺少网格描述")); }
				const auto Positions = FStaticMeshConstAttributes(*Mesh).GetVertexPositions();
				for (const FVertexID Vertex : Mesh->Vertices().GetElementIDs())
				{
					Original += Part.Local.TransformPosition(FVector(Positions[Vertex]));
				}
			}
			const double Error = FMath::Max((Original.Min - Combined.Min).GetAbsMax(), (Original.Max - Combined.Max).GetAbsMax());
			if (!Original.IsValid || !Combined.IsValid || Error > 0.5)
			{
				return Fail(LayoutPath, FString::Printf(TEXT("原位屋角六面误差 %.6f cm，原屋面 %s，新屋面 %s"),
					Error, *Original.ToString(), *Combined.ToString()));
			}
			MaxError = FMath::Max(MaxError, Error);
		}
		const auto Previous = RuleSet->Rules;
		RuleSet->Rules.RemoveAll([](const auto& Entry) { return Entry && Entry->template IsA<UCastlePCGRoofQuarterRule>(); });
		RuleSet->Rules.Add(Rule);
		bool bPublished = false;
		ON_SCOPE_EXIT { if (!bPublished) { RuleSet->Rules = Previous; } };
		FString Error;
		if (!RuleSet->ValidateConfiguration(Error)) { return Fail(RuleSetPath, Error); }
		Assets.Add(RuleSet);
		if (!SaveSupportDelivery(Assets)) { return false; }
		bPublished = true;
		UE_LOG(LogHachimisV4, Display, TEXT("四分屋顶已发布：方向件 %d，对称件 %d，套件 %d，六面最大包围盒误差 %.6f cm。"),
			DirectionalMeshes, SymmetricMeshes, DirectionalMeshes + SymmetricMeshes, MaxError);
		return true;
	}

	bool FImporter::StoreSlices(const FPartSource& Source, UCastleHachimisV4Layout& Layout, const FString& Folder)
	{
		const double Half = UCastleHachimisV4Layout::CellPitch * 0.5;
		for (const FIntVector& Cell : Source.Part.Cells)
		{
			FPartSource Slice;
			Slice.Part = Source.Part;
			Slice.Part.Cells = {Cell};
			Slice.Materials = Source.Materials;
			const FVector Center = Layout.CellBottom(Cell);
			for (const FTriangle& Triangle : Source.Triangles)
			{
				TArray<FCorner> Polygon(Triangle.Corners, 3);
				for (int32 Axis = 0; Axis < 2; ++Axis)
				{
					Clip(Polygon, Source.Part.Local, Axis, Center[Axis] - Half, 1.0);
					Clip(Polygon, Source.Part.Local, Axis, Center[Axis] + Half, -1.0);
				}
				for (int32 Index = 1; Index + 1 < Polygon.Num(); ++Index)
				{
					const FVector3f Area = FVector3f::CrossProduct(Polygon[Index].Position - Polygon[0].Position,
						Polygon[Index + 1].Position - Polygon[0].Position);
					if (!Area.IsNearlyZero())
					{
						Slice.Triangles.Add({{Polygon[0], Polygon[Index], Polygon[Index + 1]}, Triangle.Material});
					}
				}
			}
			if (!StorePart(Slice, Folder)) { return false; }
			Layout.FoundationSlices.Add(MoveTemp(Slice.Part));
		}
		return true;
	}

	bool ReadHatch(const FSceneSource& Scene, FCastleV4Hatch& Out, const FString& File)
	{
		const FVector* Bottom = Scene.ClimbMarkers.Find(TEXT("ClimbStart"));
		const FVector* Top = Scene.ClimbMarkers.Find(TEXT("ClimbExit"));
		const FPartSource* Step = Scene.Parts.FindByPredicate([](const FPartSource& Part)
		{
			return Part.Part.Node.ToString().StartsWith(TEXT("Hatch_ClimbOutStep"));
		});
		TArray<FVector> Shoes;
		for (const FPartSource& Part : Scene.Parts)
		{
			if (Part.Part.Node.ToString().StartsWith(TEXT("Ladder_Shoe"))) { Shoes.Add(Part.Part.Local.GetLocation()); }
		}
		if (!Bottom || !Top || !Step || Shoes.Num() != 2 || !FMath::IsNearlyEqual(Bottom->Z, WalkCm, SeamTolerance)
			|| !FMath::IsNearlyEqual(Top->Z - Bottom->Z, UCastleHachimisV4Layout::StoryRise, SeamTolerance))
		{
			return Fail(File, TEXT("作者梯级、梯脚或 ClimbStart/Exit 标记与两层站立面不相符"));
		}
		Out.BottomStand = *Bottom;
		Out.TopStand = *Top;
		// 梯面到胶囊中心的前向偏移使攀爬轨迹穿过洞口内部。开口范围来自作者 Plan.hole。
		Out.LadderBottom = FVector((Shoes[0].X + Shoes[1].X) * 0.5, Shoes[0].Y + 48.0, Bottom->Z);
		const FVector StepPosition = Step->Part.Local.GetLocation();
		Out.LadderTop = FVector(StepPosition.X, StepPosition.Y + 48.0, Top->Z);
		Out.Opening = FBox(FVector(49.0, 52.0, Top->Z - WalkCm), FVector(195.0, 194.0, Top->Z));
		return true;
	}

	bool FImporter::ImportWall(const FJsonObject& Row)
	{
		const FString Key = Row.GetStringField(TEXT("key"));
		const FString Folder = FString(AssetRoot) / Row.GetStringField(TEXT("family")) / TEXT("Walls");
		UCastleV4WallKit* Kit = CreateOrLoad<UCastleV4WallKit>(Folder / (TEXT("DA_Wall_") + Key));
		if (!Kit || WallKits.Contains(*Key)) { return Fail(Key, TEXT("墙件资产无效或重复")); }
		Kit->WallKey = *Key;
		Kit->Span = Row.GetNumberField(TEXT("span"));
		Kit->Opening = Row.GetNumberField(TEXT("opening"));
		for (const TCHAR* State : {TEXT("open"), TEXT("closed")})
		{
			FSceneSource Scene;
			if (!ReadScene(*Row.GetObjectField(State), Scene)) { return false; }
			TArray<FCastleV4Part>& Parts = FCString::Strcmp(State, TEXT("open")) == 0 ? Kit->Open : Kit->Closed;
			Parts.Reset();
			for (FPartSource& Source : Scene.Parts)
			{
				if (!StorePart(Source, Folder / TEXT("Meshes"))) { return false; }
				Parts.Add(MoveTemp(Source.Part));
			}
		}
		WallKits.Add(*Key, Kit);
		AssemblyAssets.Add(Kit);
		return true;
	}

	bool FImporter::ImportLadder(const FJsonObject& Row)
	{
		const FString Family = Row.GetStringField(TEXT("kind"));
		const FString Folder = FString(AssetRoot) / Family / TEXT("FloorLadder");
		FSceneSource Scene;
		if (!ReadScene(Row, Scene)) { return false; }
		UCastleV4LadderKit* Kit = CreateOrLoad<UCastleV4LadderKit>(Folder / (TEXT("DA_FloorLadder_") + Family));
		if (!Kit || LadderKits.Contains(*Family)) { return Fail(Family, TEXT("楼板梯资产无效或重复")); }
		if (!ReadHatch(Scene, Kit->Hatch, Row.GetStringField(TEXT("file")))) { return false; }
		Kit->Family = *Family;
		Kit->Parts.Reset();
		for (FPartSource& Source : Scene.Parts)
		{
			if (Source.Part.Group != TEXT("UpperFloor") && Source.Part.Group != TEXT("Ladder"))
			{
				return Fail(Kit->GetPathName(), Source.Part.Node.ToString() + TEXT(" 不属于梯或通梯楼板"));
			}
			if (!StorePart(Source, Folder / TEXT("Meshes"))) { return false; }
			Kit->Parts.Add(MoveTemp(Source.Part));
		}
		LadderKits.Add(*Family, Kit);
		AssemblyAssets.Add(Kit);
		return true;
	}

	/** 两次 FBX 导出使用同一局部网格；UV 的浮点尾数不同，比较时容许误差而不改写原数据。 */
	bool SameRoofGeometry(const FPartSource& A, const FPartSource& B)
	{
		if (A.Triangles.Num() != B.Triangles.Num() || A.Materials != B.Materials) { return false; }
		for (int32 Index = 0; Index < A.Triangles.Num(); ++Index)
		{
			const FTriangle& Left = A.Triangles[Index];
			const FTriangle& Right = B.Triangles[Index];
			if (Left.Material != Right.Material) { return false; }
			for (int32 Vertex = 0; Vertex < 3; ++Vertex)
			{
				const FCorner& P = Left.Corners[Vertex];
				const FCorner& Q = Right.Corners[Vertex];
				if (!P.Position.Equals(Q.Position, 0.0001f) || !P.Normal.Equals(Q.Normal, 0.0001f) || !P.UV.Equals(Q.UV, 0.00001f))
				{
					return false;
				}
			}
		}
		return true;
	}

	/** 确认每份独立屋面是主体中相同的一组可切换部件。等价后共用原件网格和覆盖格，不重复生成一份屋顶。 */
	bool FImporter::VerifyRoofModules(const FJsonObject& Row, const FSceneSource& Building)
	{
		const FString File = Row.GetStringField(TEXT("file"));
		const TArray<TSharedPtr<FJsonValue>>* Modules = nullptr;
		if (!Row.TryGetArrayField(TEXT("roof_modules"), Modules) || Modules->IsEmpty())
		{
			return Fail(File, TEXT("盾塔交付缺少独立屋面模块清单"));
		}
		TMap<FName, const FPartSource*> RoofParts;
		for (const FPartSource& Part : Building.Parts)
		{
			if (Part.Part.Group == TEXT("Roof"))
			{
				if (RoofParts.Contains(Part.Part.Node)) { return Fail(File, Part.Part.Node.ToString() + TEXT(" 屋面节点重复")); }
				RoofParts.Add(Part.Part.Node, &Part);
			}
		}
		for (const auto& Item : *Modules)
		{
			const FJsonObject& ModuleRow = *Item->AsObject();
			const FString ModuleFile = ModuleRow.GetStringField(TEXT("file"));
			const FString Root = ModuleRow.GetStringField(TEXT("root"));
			FSceneSource Module;
			if (!ReadScene(ModuleRow, Module, true)) { return false; }
			const FTransform* Mount = Building.Mounts.Find(Root);
			if (!Mount || !Mount->Equals(FTransform(Module.RoofAnchor), SeamTolerance))
			{
				return Fail(ModuleFile, TEXT("独立模块锚点与主体内同名屋面安装变换不同"));
			}
			for (const FPartSource& Part : Module.Parts)
			{
				const FPartSource* Embedded = RoofParts.FindRef(Part.Part.Node);
				if (!Embedded || Part.Part.Group != TEXT("Roof") || Embedded->Parent != Root)
				{
					return Fail(ModuleFile, Part.Part.Node.ToString() + TEXT(" 在主体对应屋面组中不存在或已匹配"));
				}
				if (!(Part.Part.Local * *Mount).Equals(Embedded->Part.Local, SeamTolerance))
				{
					return Fail(ModuleFile, Part.Part.Node.ToString() + TEXT(" 拼回主体后的安装变换不同"));
				}
				if (!SameRoofGeometry(Part, *Embedded))
				{
					return Fail(ModuleFile, Part.Part.Node.ToString() + TEXT(" 与主体的三角或材质槽不同"));
				}
				RoofParts.Remove(Part.Part.Node);
			}
		}
		if (!RoofParts.IsEmpty()) { return Fail(File, TEXT("独立屋面清单未覆盖主体的所有屋面零件")); }
		UE_LOG(LogHachimisV4, Display, TEXT("%s：%d 份独立屋面和接缝已逐件对齐，装配共用主体网格。"), *File, Modules->Num());
		return true;
	}

	bool FImporter::ImportBuilding(const FJsonObject& Row)
	{
		const FString File = Row.GetStringField(TEXT("file"));
		const FString Family = Row.GetStringField(TEXT("kind"));
		const FString Form = Row.GetStringField(TEXT("form"));
		const FString Folder = FString(AssetRoot) / Family;
		FSceneSource Scene;
		if (!ReadScene(Row, Scene) || (bShieldDelivery && !VerifyRoofModules(Row, Scene))) { return false; }
		UCastleHachimisV4Layout* Layout = CreateOrLoad<UCastleHachimisV4Layout>(Folder / (TEXT("DA_") + Family + TEXT("_") + Form));
		if (!Layout) { return Fail(File, TEXT("装配资源无法读取")); }
		const auto& Grid = Row.GetArrayField(TEXT("grid"));
		if (Grid.Num() != 3 || !LadderKits.Contains(*Family)) { return Fail(File, TEXT("装配缺少占格或本族楼板梯")); }
		Layout->Family = *Family;
		Layout->Form = *Form;
		Layout->SourceFile = File;
		Layout->BlueprintPath = Row.GetStringField(TEXT("blueprint"));
		Layout->Grid = FIntVector(Grid[0]->AsNumber(), Grid[1]->AsNumber(), Grid[2]->AsNumber());
		Layout->StandingHeight = WalkCm;
		Layout->bChassisOnly = Row.GetBoolField(TEXT("chassis"));
		Layout->Ladder = LadderKits.FindChecked(*Family);
		Layout->Parts.Reset();
		Layout->FoundationSlices.Reset();
		Layout->Walls.Reset();
		if (Layout->Grid.Z == 2 && !ReadHatch(Scene, Layout->InternalHatch, File)) { return false; }
		if (Layout->Grid.Z == 1 && !Scene.ClimbMarkers.IsEmpty()) { return Fail(File, TEXT("单层主体包含楼层梯标记")); }
		TSet<FString> VerifiedMounts;
		for (const auto& Item : Row.GetArrayField(TEXT("slots")))
		{
			const FJsonObject& Slot = *Item->AsObject();
			const FString Side = Slot.GetStringField(TEXT("side"));
			const int32 Bay = Slot.GetIntegerField(TEXT("bay"));
			const int32 Layer = Slot.GetIntegerField(TEXT("level"));
			const FString Key = Slot.GetStringField(TEXT("wall_key"));
			const FString Node = FString::Printf(TEXT("WALL_%s_%s_%s%d_F%d_Closed"), *Family, *Form, *Side, Bay, Layer + 1);
			UCastleV4WallKit* const* Kit = WallKits.Find(*Key);
			const FTransform* Mount = Scene.Mounts.Find(Node);
			const auto& Pivot = Slot.GetArrayField(TEXT("pivot"));
			if (!Kit || !Key.StartsWith(Family + TEXT("_")) || !Mount || Pivot.Num() != 2 || VerifiedMounts.Contains(Node)
				|| !FMath::IsNearlyEqual((*Kit)->Span, Slot.GetNumberField(TEXT("span")) * CmPerMetre, SeamTolerance))
			{
				return Fail(File, Node + TEXT(" 缺少唯一作者墙位、套件、枢轴或跨度"));
			}
			const FVector Expected(Pivot[0]->AsNumber() * CmPerMetre, -Pivot[1]->AsNumber() * CmPerMetre, Layer * UCastleHachimisV4Layout::StoryRise);
			if (!Mount->GetLocation().Equals(Expected, SeamTolerance)) { return Fail(File, Node + TEXT(" 和清单的枢轴不一致")); }
			const FVector Normal = Mount->TransformVectorNoScale(FVector::RightVector);
			const FIntVector Face(FMath::RoundToInt(Normal.X), FMath::RoundToInt(Normal.Y), FMath::RoundToInt(Normal.Z));
			if (!Normal.Equals(FVector(Face), SeamTolerance) || Face.Z != 0) { return Fail(File, Node + TEXT(" 不是棋盘水平四面")); }
			const FIntVector Cell(
				Face.X == 0 ? Bay - 1 : (Face.X > 0 ? Layout->Grid.X - 1 : 0),
				Face.Y == 0 ? Layout->Grid.Y - Bay : (Face.Y > 0 ? Layout->Grid.Y - 1 : 0), Layer);
			const int32 ClosedCount = Algo::CountIf(Scene.Parts, [&](const FPartSource& Part)
			{
				return Part.Parent == Node && Part.WallKey == Key;
			});
			if (ClosedCount != (*Kit)->Closed.Num()) { return Fail(File, Node + TEXT(" 的闭墙节点数与正式墙套件不同")); }
			VerifiedMounts.Add(Node);
			FCastleV4WallSlot& Wall = Layout->Walls.AddDefaulted_GetRef();
			Wall.Node = *Node;
			Wall.Cell = Cell;
			Wall.Facing = Face;
			Wall.Mount = *Mount;
			Wall.Kit = *Kit;
		}
		int32 KeyParts = 0;
		TSet<FIntVector> RoofBays;
		for (FPartSource& Source : Scene.Parts)
		{
			const FBox Box = Bounds(Source);
			if (Source.Part.Group == TEXT("Roof"))
			{
				if (Source.Parent.StartsWith(TEXT("ROOT_RoofKeys_")))
				{
					Source.Part.Group = TEXT("RoofKeys");
					++KeyParts;
					AssignCoverage(Box, *Layout, Layout->Grid.Z - 1, Source.Part.Cells);
				}
				else if (Layout->Grid.X * Layout->Grid.Y == 1)
				{
					Source.Part.Cells = {FIntVector(0, 0, Layout->Grid.Z - 1)};
					RoofBays.Add(Source.Part.Cells[0]);
				}
				else
				{
					TArray<TSharedPtr<FJsonValue>> RoofCell;
					if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source.RoofCell), RoofCell) || RoofCell.Num() != 2)
					{
						return Fail(File, Source.Part.Node.ToString() + TEXT(" 缺少作者屋面分区"));
					}
					Source.Part.Cells = {FIntVector(RoofCell[0]->AsNumber() - 1, Layout->Grid.Y - RoofCell[1]->AsNumber(), Layout->Grid.Z - 1)};
					RoofBays.Add(Source.Part.Cells[0]);
				}
			}
			else if (Source.Part.Group == TEXT("Foundation") || Source.Part.Group == TEXT("UpperFloor"))
			{
				AssignCoverage(Box, *Layout, Source.Part.Group == TEXT("Foundation") ? 0 : 1, Source.Part.Cells);
			}
			else if (Source.Part.Group == TEXT("Wall"))
			{
				if (!VerifiedMounts.Contains(Source.Parent)) { return Fail(File, Source.Parent + TEXT(" 没有对应的清单墙位")); }
				// 保存作者闭墙独立节点及准确的分面标签，供 SCS 和 PCG 分别消费。
				Source.Part.Group = *Source.Parent;
			}
			if (!StorePart(Source, Folder / TEXT("Parts"))) { return false; }
			if (Source.Part.Group == TEXT("Foundation") && Source.Part.Cells.Num() > 1
				&& !StoreSlices(Source, *Layout, Folder / TEXT("FoundationSlices"))) { return false; }
			if (Source.Part.Node.ToString().StartsWith(TEXT("Deck_ExactCore"))
				&& (!FMath::IsNearlyEqual(Box.GetSize().X, Layout->Grid.X * Layout->CellPitch, SeamTolerance)
					|| !FMath::IsNearlyEqual(Box.GetSize().Y, Layout->Grid.Y * Layout->CellPitch, SeamTolerance)
					|| !FMath::IsNearlyZero(Box.Min.Z, SeamTolerance)))
			{
				return Fail(File, Source.Part.Node.ToString() + TEXT(" 引擎坐标中的底部、占格或厘米换算不正确"));
			}
			Layout->Parts.Add(MoveTemp(Source.Part));
		}
		if (KeyParts != Row.GetIntegerField(TEXT("roof_keys")) || RoofBays.Num() != Row.GetIntegerField(TEXT("roof_bays")))
		{
			return Fail(File, FString::Printf(TEXT("屋面分区 %d 和跨区构件 %d 与作者清单不同"), RoofBays.Num(), KeyParts));
		}
		AssemblyAssets.Add(Layout);
		UE_LOG(LogHachimisV4, Display, TEXT("%s -> %s，%d 节点，%d 墙位，%d 屋面分区，%d 脊部拆件，站立高 %.1f cm"),
			*File, *Layout->BlueprintPath, Layout->Parts.Num(), Layout->Walls.Num(), RoofBays.Num(), KeyParts, Layout->StandingHeight);
		return true;
	}

	bool FImporter::Run(const FJsonObject& Manifest)
	{
		// 显式隔离两个交付批次，杜绝误用开关后写到其他建筑或共享材质目录。
		for (const TCHAR* Category : {TEXT("walls"), TEXT("ladders"), TEXT("buildings")})
		{
			const auto& Rows = Manifest.GetArrayField(Category);
			const bool bWalls = FCString::Strcmp(Category, TEXT("walls")) == 0;
			const bool bLadders = FCString::Strcmp(Category, TEXT("ladders")) == 0;
			if (bShieldDelivery && Rows.Num() != (bLadders ? 1 : 7))
			{
				return Fail(Directory, FString(Category) + TEXT(" 清单数量不符合盾塔独立交付"));
			}
			for (const auto& Item : Rows)
			{
				const FJsonObject& Row = *Item->AsObject();
				if (bShieldDelivery != (Row.GetStringField(bWalls ? TEXT("family") : TEXT("kind")) == TEXT("Shield")))
				{
					return Fail(Directory, FString(Category) + TEXT(" 包含不属于当前导入范围的造型；盾塔必须使用 -Shield"));
				}
			}
		}
		for (const auto& Item : Manifest.GetArrayField(TEXT("unselected")))
		{
			const FJsonObject& Entry = *Item->AsObject();
			UE_LOG(LogHachimisV4, Display, TEXT("正式交付之外的同名副本 %s，SHA256 %s，原因 %s"),
				*Entry.GetStringField(TEXT("file")), *Entry.GetStringField(TEXT("sha256")), *Entry.GetStringField(TEXT("reason")));
		}
		for (const auto& Row : Manifest.GetArrayField(TEXT("walls"))) { if (!ImportWall(*Row->AsObject())) { return false; } }
		for (const auto& Row : Manifest.GetArrayField(TEXT("ladders"))) { if (!ImportLadder(*Row->AsObject())) { return false; } }
		for (const auto& Row : Manifest.GetArrayField(TEXT("buildings"))) { if (!ImportBuilding(*Row->AsObject())) { return false; } }
		// 全部来源和布局验证通过后写出装配资产。导入阶段不修改任何现有建筑蓝图。
		for (UObject* Asset : AssemblyAssets) { if (!Save(*Asset)) { return false; } }
		UE_LOG(LogHachimisV4, Display, TEXT("已保存 %d 个独立网格资源、%d 项作者材质、%d 个装配资产"), Meshes.Num(), Palette.Num(), AssemblyAssets.Num());
		return true;
	}
}
#endif

int32 UHachimisV4ImportCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	FString RoofQuarterSource, RoofQuarterDirectionalSource;
	if (FParse::Value(*Params, TEXT("RoofQuarterSource="), RoofQuarterSource))
	{
		if (!FParse::Value(*Params, TEXT("RoofQuarterDirectionalSource="), RoofQuarterDirectionalSource))
		{
			HachimisV4Import::Fail(RoofQuarterSource, TEXT("需要 -RoofQuarterDirectionalSource= 真实方向件的临时 FBX 目录"));
			return 1;
		}
		HachimisV4Import::FImporter Importer(RoofQuarterSource, FString(), false);
		return Importer.RunRoofQuarterDelivery(RoofQuarterDirectionalSource) ? 0 : 1;
	}
	FString ShieldQuadRoofSource;
	if (FParse::Value(*Params, TEXT("ShieldQuadRoofSource="), ShieldQuadRoofSource))
	{
		HachimisV4Import::FImporter Importer(ShieldQuadRoofSource, FString(), false);
		return Importer.RunShieldQuadRoofDelivery() ? 0 : 1;
	}
	FString QuadRoofSource;
	if (FParse::Value(*Params, TEXT("QuadRoofSource="), QuadRoofSource))
	{
		HachimisV4Import::FImporter Importer(QuadRoofSource, FString(), false);
		return Importer.RunQuadRoofDelivery() ? 0 : 1;
	}
	FString SupportSource;
	if (FParse::Value(*Params, TEXT("SupportSource="), SupportSource))
	{
		// SDK 从暂存的 ASCII 文件路径读取；内容仍来自原始交付，不改写 FBX。
		HachimisV4Import::FImporter Importer(SupportSource, FString(), false);
		return Importer.RunSupportDelivery() ? 0 : 1;
	}
	// 清点脚本已核对物理目录和作者清单，导入器再次核对根节点、材质、尺度和每个语义槽。
	FString ManifestPath;
	FString Json;
	TSharedPtr<FJsonObject> Manifest;
	if (!FParse::Value(*Params, TEXT("Manifest="), ManifestPath) || !FFileHelper::LoadFileToString(Json, *ManifestPath)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Manifest) || !Manifest.IsValid())
	{
		HachimisV4Import::Fail(ManifestPath, TEXT("需要可读的 -Manifest= 清点脚本结果"));
		return 1;
	}
	FString RebuildMesh;
	FParse::Value(*Params, TEXT("RebuildMesh="), RebuildMesh);
	HachimisV4Import::FImporter Importer(Manifest->GetStringField(TEXT("source")), RebuildMesh, FParse::Param(*Params, TEXT("Shield")));
	return Importer.Run(*Manifest) ? 0 : 1;
#else
	// FBX SDK 和 UE 原生资产生成接口仅用于编辑器目标。
	return 1;
#endif
}
