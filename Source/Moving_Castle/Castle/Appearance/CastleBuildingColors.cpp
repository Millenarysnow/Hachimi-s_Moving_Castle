#include "CastleBuildingColors.h"

#include "Components/StaticMeshComponent.h"

namespace CastleBuildingColors
{
	/** 屋角包含瓦与饰件。只打开屋面通道，黄铜、石料、蜂蜡等槽仍取各自原色。 */
	static const TSet<FName> SurfaceGroups = {
		TEXT("Roof"), TEXT("RoofKeys"), TEXT("RoofQuarter"), TEXT("Slope"), TEXT("WallApron"),
		TEXT("Corner"), TEXT("Row"), TEXT("Diagonal"), TEXT("LastBay"), TEXT("Cantilever"),
		TEXT("CornerFan"), TEXT("ThroughArch"), TEXT("DeepCorbel"), TEXT("OpposedSpan"), TEXT("Deck")
	};

	/** 精确匹配作者命名的主体，不扩大到同一材质制作的家具、铆钉、柱脚或屋顶饰边。 */
	static const TSet<FName> StructuralNodes = {
		TEXT("FullHeightTimber"), TEXT("TwinTenonPost"), TEXT("FreestonePier"), TEXT("StoneCourse"), TEXT("BasaltPier"),
		TEXT("PatinaPost"), TEXT("FrontTrellisPost"), TEXT("TrellisPostCap"), TEXT("LaunchTrussLeg"),
		TEXT("LaunchPortalLeg"), TEXT("AdobePier"), TEXT("GlazedPierBelt"), TEXT("GlazedColumnGirdle"),
		TEXT("FlaredClayCapital"), TEXT("RoyalCornerFillet"), TEXT("Wall_JunctionPier"),
		TEXT("ShieldPier_ChamferPier"), TEXT("ShieldPier_ClusterPier"), TEXT("ShieldPier_CollaredPier"),
		TEXT("ShieldPier_CrownedPier"), TEXT("ShieldPier_ForkedRibPier"), TEXT("ShieldPier_BundleRib"),
		TEXT("ShieldPier_Collar"), TEXT("HonedBlueInlay"), TEXT("FoundryArmorInsert"), TEXT("ShieldWall_GlazedInset")
	};

	static FName NodeStem(FName Node)
	{
		// 原始 FBX 名为“构件_三位序号”。闭墙实例还会追加如 _Front1F1 的位置后缀；
		// 独立墙套件使用原名。两者都以序号作为分界，保留 ShieldPier_ChamferPier 等完整构件名。
		const FString Name = Node.ToString();
		for (int32 I = 0; I + 3 < Name.Len(); ++I)
		{
			if (Name[I] == TEXT('_') && FChar::IsDigit(Name[I+1]) && FChar::IsDigit(Name[I+2])
				&& FChar::IsDigit(Name[I+3]) && (I+4 == Name.Len() || Name[I+4] == TEXT('_')))
			{
				return FName(*Name.Left(I));
			}
		}
		return Node;
	}

	void Apply(UStaticMeshComponent& Component, ECastleTileColor Color, ECastleBuildingType Type, FName Node, FName Group)
	{
		const FName Stem = NodeStem(Node);
		// 普通房的一级底盘没有屋顶。只漆外围包边，板面、角鞋和螺钉继续使用木材及五金原色。
		const bool bRoomTrim = Type == ECastleBuildingType::NormalRoom &&
			(Stem == TEXT("Deck_PerimeterCoping") || Stem == TEXT("Deck_EndApron") || Stem == TEXT("Deck_SideApron"));
		const bool bStructure = StructuralNodes.Contains(Stem) || bRoomTrim;
		if (SurfaceGroups.Contains(Group) || bStructure)
		{
			Component.SetCustomPrimitiveDataFloat(SurfaceIndex, static_cast<uint8>(Color));
		}
		if (bStructure)
		{
			Component.SetCustomPrimitiveDataFloat(StructureIndex, static_cast<uint8>(Color));
		}
	}
}
