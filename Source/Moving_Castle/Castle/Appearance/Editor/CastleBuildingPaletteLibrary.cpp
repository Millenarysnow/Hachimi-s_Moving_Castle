#include "CastleBuildingPaletteLibrary.h"

#if WITH_EDITOR
#include "Castle/Appearance/CastleBuildingColors.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "ImageCore.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Misc/PackageName.h"
#include "Moving_Castle.h"
#include "ShaderCompiler.h"
#include "MaterialShared.h"
#include "UObject/SavePackage.h"

namespace CastlePalette
{
	const FString Root = TEXT("/Game/MovingCastle/Model/Buildings/HachimisV4/");
	const FString Materials = Root + TEXT("Materials/");
	const FString Shield = Root + TEXT("Shield/Materials/");
	// 表项与持久化枚举号一一对应。第 0 层供静态网格编辑器显示作者原色。
	const FColor Bases[] = {FColor::Black, FColor(213,69,72), FColor(40,122,219), FColor(54,168,111), FColor(228,171,49)};
	struct FSwatch
	{
		float Tone;       // 负数向作者的冷暗灰偏移，正数向暖米白偏移，混合发生在 sRGB。
		int32 Channel;    // 结构和屋面分别启用，确保屋角内的石材不会随柱子变色。
	};
	constexpr int32 Surface = CastleBuildingColors::SurfaceIndex;
	constexpr int32 Structure = CastleBuildingColors::StructureIndex;
	constexpr float Tiles[] = {-.187f, -.1003f, -.034f, .0425f, .1275f};
	constexpr float Cloth[] = {-.20f, -.045f, .095f, .22f};
	constexpr float Ceramic[] = {-.27f, -.045f, -.15f, .105f, -.115f};

	const TMap<FString, FSwatch>& Solids()
	{
		static const TMap<FString, FSwatch> Table = []
		{
			TMap<FString, FSwatch> T;
			for (const FString Family : {TEXT("Room"), TEXT("Archer"), TEXT("Cannon"), TEXT("Farm"), TEXT("Hive")})
			{
				for (int32 N = 0; N < 5; ++N) { T.Add(Materials + TEXT("M_") + Family + FString::Printf(TEXT("_Tile%d"), N), {Tiles[N], Surface}); }
			}
			for (const FString Family : {TEXT("Room"), TEXT("Archer"), TEXT("Cannon"), TEXT("Farm"), TEXT("Hive"), TEXT("Balloon")})
			{
				T.Add(Materials + TEXT("M_") + Family + TEXT("_Glaze"), {.13f, Surface});
				T.Add(Materials + TEXT("M_") + Family + TEXT("_RoofShadow"), {-.28f, Surface});
			}
			for (const FString Family : {TEXT("Archer"), TEXT("Cannon"), TEXT("Balloon"), TEXT("Hive")})
			{
				T.Add(Materials + TEXT("M_") + Family + TEXT("_Accent"), {.19f, Surface});
			}
			for (const FString Family : {TEXT("Archer"), TEXT("Cannon"), TEXT("Farm"), TEXT("Balloon"), TEXT("Hive"), TEXT("Shield")})
			{
				for (int32 N = 0; N < 5; ++N) { T.Add(Materials + FString::Printf(TEXT("M_HC515_%s_%d"), *Family, N), {Tiles[N], Surface}); }
			}
			for (int32 N = 0; N < 4; ++N) { T.Add(Materials + FString::Printf(TEXT("M_TealSail%d"), N), {Cloth[N], Surface}); }
			for (int32 N = 0; N < 5; ++N) { T.Add(Shield + FString::Printf(TEXT("M_ShieldCeramic%d"), N), {Ceramic[N], Surface}); }
			T.Add(Materials + TEXT("M_LaunchShadow"), {-.28f, Surface});
			T.Add(Shield + TEXT("M_ShieldWine"), {-.40f, Surface});
			T.Add(Shield + TEXT("M_ShieldLacquer"), {-.11f, Surface});
			// 这些原料只有在明确标记的柱身、釉带及底盘包边上才读取 Structure 通道。
			for (const TPair<FString, float>& Pair : TMap<FString,float>{
				{TEXT("M_BlueStone"), -.24f}, {TEXT("M_Basalt"), -.44f}, {TEXT("M_Patina"), -.22f},
				{TEXT("M_SteelTeal"), -.33f}, {TEXT("M_Adobe"), .06f}, {TEXT("M_ClaySlip"), -.22f},
				{TEXT("M_ClayGlaze"), .16f}, {TEXT("M_PostOak"), -.30f}, {TEXT("M_BeamOak"), -.10f}, {TEXT("M_StoneTrim"), -.25f}})
			{
				T.Add(Materials + Pair.Key, {Pair.Value, Structure});
			}
			T.Add(Shield + TEXT("M_ShieldCharcoalStone"), {-.42f, Structure});
			T.Add(Shield + TEXT("M_ShieldStoneFace"), {-.18f, Structure});
			return T;
		}();
		return Table;
	}

	bool PaletteError(const UObject& Asset, const FString& Reason)
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("建筑配色 %s：%s"), *Asset.GetPathName(), *Reason);
		return false;
	}
	FColor Shade(int32 Index, float Tone)
	{
		const FColor Toward = Tone < 0 ? FColor(52,44,60) : FColor(252,241,209);
		auto Mix = [Tone](uint8 A, uint8 B) { return FMath::RoundToInt(FMath::Lerp(float(A), float(B), FMath::Abs(Tone))); };
		return FColor(Mix(Bases[Index].R, Toward.R), Mix(Bases[Index].G, Toward.G), Mix(Bases[Index].B, Toward.B));
	}
	template<class T> T* Add(UMaterial& M, int32 X, int32 Y)
	{
		T* Node = NewObject<T>(&M);
		Node->MaterialExpressionEditorX = X;
		Node->MaterialExpressionEditorY = Y;
		M.GetExpressionCollection().AddExpression(Node);
		return Node;
	}
	UMaterialExpressionScalarParameter* Selector(UMaterial& M, int32 Channel)
	{
		auto* Index = Add<UMaterialExpressionScalarParameter>(M, -650, 170);
		Index->ParameterName = Channel == Surface ? TEXT("BuildingSurfaceIndex") : TEXT("BuildingStructureIndex");
		Index->bUseCustomPrimitiveData = true;
		Index->PrimitiveDataIndex = Channel;
		Index->DefaultValue = 0;
		return Index;
	}

	/** 作者基础色图为 16×8 个 64px 色块。坐标从源图左上算，两个独立交付各有自己的排布。 */
	TMap<int32,float> AtlasSwatches(bool bShield)
	{
		TMap<int32,float> T;
		if (bShield)
		{
			for (int32 Y = 0; Y < 8; ++Y)
			{
				T.Add(Y * 16 + 9, -.11f); // 酒红漆，不含相邻的香槟金包边。
				for (int32 X = 3; X <= 6; ++X) { T.Add(Y * 16 + X, -.40f); }
			}
			// 作者按列放置陶片，各种陶片在酒红底上占用不同高度；粉玻璃、金属和珠光独立。
			const int32 Starts[] = {7,7,6,5,2};
			for (int32 N = 0; N < 5; ++N)
				for (int32 Y = Starts[N]; Y < 8; ++Y) { T.Add(Y * 16 + 2 + N, Ceramic[N]); }
		}
		else
		{
			// 共用图集按材质名称从底行往上排列，瓦和五金交错。按明确的格号登记，绝不按像素色相选区。
			auto Put = [&T](int32 Slot, float Tone) { T.Add((7 - Slot / 16) * 16 + Slot % 16, Tone); };
			for (int32 First : {9,29,49,76,104})
			{
				Put(First-2, .13f); Put(First-1, -.28f);
				for (int32 N=0; N<5; ++N) { Put(First+N, Tiles[N]); }
			}
			for (int32 Slot : {6,15,26,73}) { Put(Slot, .19f); }
			Put(16, .13f); Put(17, -.28f); Put(86, -.28f);
			for (int32 N=0; N<4; ++N) { Put(115+N, Cloth[N]); }
		}
		return T;
	}
}

bool UCastleBuildingPaletteLibrary::BindSolid(UMaterial& Material)
{
	using namespace CastlePalette;
	const FSwatch* Definition = Solids().Find(Material.GetOutermost()->GetName());
	if (!Definition) { return true; } // 配色只对上表中明确授权的材质生效，装饰维持原图。
	auto& Base = Material.GetEditorOnlyData()->BaseColor;
	auto* Colors = Cast<UMaterialExpressionCustom>(Base.Expression);
	if (Base.Expression && (!Colors || Colors->Description != TEXT("CastleBuildingPalette")))
	{
		return PaletteError(Material, TEXT("已有其他固有色接线，不能覆盖"));
	}
	if (!Colors)
	{
		Colors = Add<UMaterialExpressionCustom>(Material, -330, 0);
		Colors->Description = TEXT("CastleBuildingPalette");
		Colors->Inputs.SetNum(1);
		Colors->Inputs[0].InputName = TEXT("ColorIndex");
		Colors->Inputs[0].Input.Connect(0, Selector(Material, Definition->Channel));
		Colors->OutputType = CMOT_Float3;
	}
	// 只写固有色入口，保留源常量供重导入校验。颜色常数在制作时转线性，GPU 只选择一档。
	TArray<FString> Values;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Bases); ++Index)
	{
		const FLinearColor C(Index == 0 ? Base.Constant : Shade(Index, Definition->Tone));
		Values.Add(FString::Printf(TEXT("float3(%.9f,%.9f,%.9f)"), C.R,C.G,C.B));
	}
	Colors->Code = TEXT("const float3 palette[5] = {") + FString::Join(Values,TEXT(",")) + TEXT("};\nreturn palette[(uint)ColorIndex];");
	Base.Connect(0, Colors);
	Base.UseConstant = false;
	return true;
}

bool UCastleBuildingPaletteLibrary::BindAtlas(UMaterial& Material, UTexture2D& Source, bool bShield, TArray<UObject*>& Assets)
{
	using namespace CastlePalette;
	FImage Image;
	if (!Source.SRGB || !Source.Source.GetMipImage(Image, 0) || Image.SizeX != 1024 || Image.SizeY != 512 || Image.NumSlices != 1)
	{
		return PaletteError(Source, TEXT("需要 1024×512 的单层 sRGB 作者图集"));
	}
	Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	const auto Original = Image.AsBGRA8();
	TArray<FColor> Layers;
	Layers.Append(Original.GetData(), Original.Num());
	for (int32 Index = 1; Index < UE_ARRAY_COUNT(Bases); ++Index) { Layers.Append(Original.GetData(), Original.Num()); }
	for (const auto& Swatch : AtlasSwatches(bShield))
	{
		const int32 X0 = Swatch.Key % 16 * 64, Y0 = Swatch.Key / 16 * 64;
		// 各色块带有作者烘焙的小幅明暗纹理。以色块平均值为中心逐像素还原偏差，保护区逐字节沿用源图。
		FIntVector Sum(0);
		for (int32 Y=Y0; Y<Y0+64; ++Y)
			for (int32 X=X0; X<X0+64; ++X) { const FColor C = Original[Y*1024+X]; Sum += FIntVector(C.R,C.G,C.B); }
		const FIntVector Mean = (Sum + FIntVector(2048)) / 4096;
		for (int32 Index=1; Index<UE_ARRAY_COUNT(Bases); ++Index)
		{
			const FColor Target = Shade(Index,Swatch.Value);
			for (int32 Y=Y0; Y<Y0+64; ++Y)
				for (int32 X=X0; X<X0+64; ++X)
				{
					const int32 Offset = Y*1024+X;
					const FColor C = Original[Offset];
					const FIntVector V = FIntVector(Target.R+C.R,Target.G+C.G,Target.B+C.B) - Mean;
					if (V.GetMin()<0 || V.GetMax()>255) { return PaletteError(Source,TEXT("配色超出 sRGB 存储范围")); }
					Layers[Index*Original.Num()+Offset] = FColor(V.X,V.Y,V.Z,C.A);
				}
		}
	}
	const FString Name = Source.GetName() + TEXT("_Gameplay");
	const FString Path = FPackageName::GetLongPackagePath(Source.GetPackage()->GetName()) / Name;
	UTexture2DArray* Array;
	if (FPackageName::DoesPackageExist(Path)) { Array = LoadObject<UTexture2DArray>(nullptr,*Path); }
	else
	{
		Array = NewObject<UTexture2DArray>(CreatePackage(*Path),*Name,RF_Public|RF_Standalone);
		FAssetRegistryModule::AssetCreated(Array);
	}
	if (!Array) { return PaletteError(Source,TEXT("无法创建或读取颜色数组 ") + Path); }
	Array->PreEditChange(nullptr);
	Array->Source.Init(1024,512,UE_ARRAY_COUNT(Bases),1,TSF_BGRA8,reinterpret_cast<const uint8*>(Layers.GetData()));
	Array->SRGB = true;
	Array->CompressionSettings = Source.CompressionSettings;
	Array->PostEditChange();
	Assets.Add(Array);
	auto& Base = Material.GetEditorOnlyData()->BaseColor;
	auto* Sample = Cast<UMaterialExpressionTextureSample>(Base.Expression);
	if (!Sample || (Sample->Texture != &Source && Sample->Texture != Array)) { return PaletteError(Material,TEXT("无法识别作者基础色采样")); }
	if (Sample->Texture == &Source)
	{
		auto* Coordinates = Add<UMaterialExpressionAppendVector>(Material,-390,0);
		Coordinates->A.Connect(0,Add<UMaterialExpressionTextureCoordinate>(Material,-650,-50));
		Coordinates->B.Connect(0,Selector(Material,Surface));
		Sample->Coordinates.Connect(0,Coordinates);
	}
	Sample->Texture = Array;
	return true;
}

bool UCastleBuildingPaletteLibrary::RebuildBuildingPalettes()
{
	using namespace CastlePalette;
	TArray<UObject*> Assets;
	for (const auto& Pair : Solids())
	{
		UMaterial* M = LoadObject<UMaterial>(nullptr,*Pair.Key);
		if (!M) { UE_LOG(LogMoving_Castle,Error,TEXT("找不到建筑配色材质 %s"),*Pair.Key); return false; }
		if (!BindSolid(*M)) { return false; }
		M->PostEditChange();
		Assets.Add(M);
	}
	for (const bool bShield : {false,true})
	{
		const FString Prefix = Root + TEXT("QuadRoofOverlay/");
		const FString Family = bShield ? TEXT("ShieldRoof") : TEXT("Roof");
		auto* M = LoadObject<UMaterial>(nullptr,*(Prefix + TEXT("M_HC515_") + Family + TEXT("Palette")));
		auto* Texture = LoadObject<UTexture2D>(nullptr,*(Prefix + TEXT("T_HC515_") + Family + TEXT("_BaseColor")));
		if (!M || !Texture) { UE_LOG(LogMoving_Castle,Error,TEXT("找不到 %s 的叠层屋面材质或源图"),*Family); return false; }
		if (!BindAtlas(*M,*Texture,bShield,Assets)) { return false; }
		M->PostEditChange();
		Assets.Add(M);
	}
	// 加速房只有单一美术外壳，直接绑定已有的房间中间色档，使该 BP 的硬引用也进入烘焙。
	auto* Speed = LoadObject<UBlueprint>(nullptr,TEXT("/Game/MovingCastle/BP/BP_SpeedRoom_lv1"));
	auto* Mid = LoadObject<UMaterial>(nullptr,*(Materials + TEXT("M_Room_Tile2")));
	if (!Speed || !Speed->SimpleConstructionScript || !Mid) { UE_LOG(LogMoving_Castle,Error,TEXT("加速房蓝图或 Room 中间色档缺失")); return false; }
	auto* Node = Speed->SimpleConstructionScript->FindSCSNode(TEXT("StaticMesh"));
	auto* Shell = Node ? Cast<UStaticMeshComponent>(Node->ComponentTemplate) : nullptr;
	if (!Shell || Shell->GetNumMaterials()!=1) { return PaletteError(*Speed,TEXT("StaticMesh 外壳必须有一个材质槽")); }
	Shell->SetMaterial(0,Mid);
	FKismetEditorUtilities::CompileBlueprint(Speed);
	if (Speed->Status == BS_Error) { return PaletteError(*Speed,TEXT("加速房蓝图编译失败")); }
	Assets.Add(Speed);
	if (GShaderCompilingManager) { GShaderCompilingManager->FinishAllCompilation(); }
	// 编译错误在保存前检查；待发布资产全部验证完毕才开始写盘。
	for (UObject* Asset : Assets)
	{
		if (auto* M = Cast<UMaterial>(Asset))
		{
			auto* Resource = M->GetMaterialResource(SP_PCD3D_SM6);
			if (!Resource || !Resource->GetCompileErrors().IsEmpty()) { return PaletteError(*M,TEXT("SM6 材质未通过编译")); }
		}
	}
	for (UObject* Asset : Assets)
	{
		auto* Package = Asset->GetPackage();
		const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package,Asset,*File,Args)) { return PaletteError(*Asset,TEXT("保存配色包失败")); }
	}
	UE_LOG(LogMoving_Castle,Display,TEXT("已发布 %d 个建筑配色资产。"), Assets.Num());
	return true;
}
#endif
