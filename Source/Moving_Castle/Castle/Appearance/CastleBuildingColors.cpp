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
		TEXT("FullHeightTimber"), TEXT("FreestonePier"), TEXT("StoneCourse"), TEXT("BasaltPier"),
		TEXT("PatinaPost"), TEXT("FrontTrellisPost"), TEXT("TrellisPostCap"), TEXT("LaunchTrussLeg"),
		TEXT("LaunchPortalLeg"), TEXT("AdobePier"), TEXT("GlazedPierBelt"), TEXT("GlazedColumnGirdle"),
		TEXT("FlaredClayCapital"), TEXT("ShieldPier_ChamferPier"), TEXT("ShieldFrame_BeltInlay"),
		TEXT("ShieldFrame_ContinuousBelt")
	};

	static FName NodeStem(FName Node)
	{
		// FBX 保留构件名末尾的三位摆放序号。只拆最后一段数字，避免把柱脚等近似名称认作柱身。
		FString Name = Node.ToString();
		int32 Separator;
		if (Name.FindLastChar(TEXT('_'), Separator) && Name.Mid(Separator + 1).IsNumeric())
		{
			Name.LeftInline(Separator);
		}
		return FName(Name);
	}

	void Apply(UStaticMeshComponent& Component, ECastleTileColor Color, ECastleBuildingType Type, FName Node, FName Group)
	{
		const FName Stem = NodeStem(Node);
		// 普通房的一级底盘没有屋顶。只漆外围包边，板面、角鞋和螺钉继续使用木材及五金原色。
		const bool bRoomTrim = Type == ECastleBuildingType::NormalRoom &&
			(Stem == TEXT("Deck_PerimeterCoping") || Stem == TEXT("Deck_EndApron") || Stem == TEXT("Deck_SideApron"));
		const bool bStructure = StructuralNodes.Contains(Stem) || bRoomTrim;
		if (SurfaceGroups.Contains(Group) || bStructure || Group == TEXT("SpeedRoomShell"))
		{
			Component.SetCustomPrimitiveDataFloat(SurfaceIndex, static_cast<uint8>(Color));
		}
		if (bStructure)
		{
			Component.SetCustomPrimitiveDataFloat(StructureIndex, static_cast<uint8>(Color));
		}
	}
}
