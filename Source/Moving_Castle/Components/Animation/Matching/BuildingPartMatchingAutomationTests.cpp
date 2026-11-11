#if WITH_DEV_AUTOMATION_TESTS

#include "BuildingPartMatcher.h"
#include "BuildingPartCatalog.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"

/** 距离贪心会抢占别人的最佳来源；用可手算的反例验证全局一对一分配。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingPartOptimalMatchTest,
	"MovingCastle.Components.Animation.BuildingMerge.OptimalPartMatching",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingPartOptimalMatchTest::RunTest(const FString& Parameters)
{
	const TArray<FBuildingPartMatchPoint> Sources = {
		{TEXT("Brick"), FVector(0.0, 0.0, 0.0)}, {TEXT("Brick"), FVector(3.0, 0.0, 0.0)},
		{TEXT("Roof"), FVector(5.0, 0.0, 0.0)} };
	const TArray<FBuildingPartMatchPoint> Targets = {
		{TEXT("Brick"), FVector(2.0, 0.0, 0.0)}, {TEXT("Brick"), FVector(5.0, 0.0, 0.0)},
		{TEXT("Window"), FVector(5.0, 0.0, 0.0)} };
	const TArray<int32> Result = BuildingPartMatcher::Match(Sources, Targets);
	TestEqual(TEXT("第一块砖不抢占更适合第二块砖的来源"), Result[0], 0);
	TestEqual(TEXT("总距离最小的第二块砖"), Result[1], 1);
	TestEqual(TEXT("同位置的屋顶不能冒充窗户"), Result[2], INDEX_NONE);

	// 数量不一致时也应一对一，不得把一个旧件重复用在多个新位置上。
	const TArray<int32> Rectangular = BuildingPartMatcher::Match({ Sources[0] }, { Targets[0], Targets[1] });
	TestEqual(TEXT("唯一旧件分配给最近目标"), Rectangular[0], 0);
	TestEqual(TEXT("其余目标明确为新增"), Rectangular[1], INDEX_NONE);
	return true;
}

/** 新版模型已统一厘米单位；不同等级的同形立柱须共用几何身份，其他结构仍不能混淆。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBuildingPartCatalogUnitsTest,
	"MovingCastle.Components.Animation.BuildingMerge.CatalogEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBuildingPartCatalogUnitsTest::RunTest(const FString& Parameters)
{
	UBuildingPartCatalog* Catalog = LoadObject<UBuildingPartCatalog>(nullptr,
		TEXT("/Game/MovingCastle/Data/Animation/DA_BuildingPartCatalog.DA_BuildingPartCatalog"));
	UStaticMesh* First = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/MovingCastle/Model/Buildings/PrototypeUpgrade/ArcherTowerLV1/Mesh_ncl_1.Mesh_ncl_1"));
	UStaticMesh* Second = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/MovingCastle/Model/Buildings/PrototypeUpgrade/ArcherTowerLV2/Mesh_ncl_1.Mesh_ncl_1"));
	UStaticMesh* Other = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/MovingCastle/Model/Buildings/PrototypeUpgrade/ArcherTowerLV1/Mesh.Mesh"));
	if (!TestNotNull(TEXT("离线目录存在"), Catalog) || !First || !Second || !Other) { return false; }
	const FBuildingPartGeometry* A = Catalog->FindGeometry(First);
	const FBuildingPartGeometry* B = Catalog->FindGeometry(Second);
	const FBuildingPartGeometry* C = Catalog->FindGeometry(Other);
	if (!TestNotNull(TEXT("一级立柱已登记"), A) || !TestNotNull(TEXT("二级立柱已登记"), B)
		|| !TestNotNull(TEXT("其他形状已登记"), C)) { return false; }
	TestTrue(TEXT("不同等级文件中的同形零件具有相同几何身份"), A->GeometryId == B->GeometryId);
	TestTrue(TEXT("新版不同等级的同形零件已统一为厘米尺寸"), A->LocalSize.Equals(B->LocalSize, 0.05));
	TestTrue(TEXT("不同结构形状不能互换"), A->GeometryId != C->GeometryId);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
