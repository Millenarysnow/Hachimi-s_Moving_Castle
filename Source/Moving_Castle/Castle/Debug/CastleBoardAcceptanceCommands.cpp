// MainMap 的开发验收入口：使用正式地图与资产，不安装测试替代模型，也不保存运行时布局。
#if WITH_DEV_AUTOMATION_TESTS
#include "Castle/Actors/CastleBoard.h"
#include "Castle/Actors/CastleBuildingBase.h"
#include "Castle/Actors/CastleGroundTileActor.h"
#include "Castle/Data/CastleBuildingDefinition.h"
#include "Castle/Components/CastleBackpackComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AssemblyFlightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Moving_Castle.h"
#include "NavigationSystem.h"
#include "NavigationData.h"

namespace CastleBoardAcceptance
{
 /** 所有验收失败都明确记录；不在缺少正式配置时创建替代资源。 */
 bool Check(bool bCondition, const FString& Message)
 {
  if (!bCondition) { UE_LOG(LogMoving_Castle, Error, TEXT("W2_MAINMAP_FAIL: %s"), *Message); }
  return bCondition;
 }

 /** 失败原因在操作结束后读取，避免函数参数求值顺序导致捕获到操作前的空 FText。 */
 bool CheckResult(bool bCondition, const FText& Reason)
 {
  return Check(bCondition,Reason.ToString());
 }

 /** 只快进本次验收创建建筑的原有飞入动画，仍执行正式的落位和 PCG 提交。 */
 bool Finish(ACastleBuildingBase* Building)
 {
  if (!Check(IsValid(Building), TEXT("正式放置未返回建筑"))) { return false; }
  if (!Check(IsValid(Building->AssemblyFlight), TEXT("正式建筑缺少飞入组件"))) { return false; }
  if (Building->AssemblyFlight->bIsPlaying)
  { Building->AssemblyFlight->TickComponent(100.f, LEVELTICK_All, nullptr); }
  return Check(IsValid(Building) && Building->bHasCompletedPlacement, TEXT("正式建筑飞入未完成"));
 }

 /** D18 明确要求补建地图导航数据；Python 的放置接口拒绝 NotPlaceable 类，改调导航系统原生创建入口。 */
 FAutoConsoleCommandWithWorldAndArgs CreateNavigation(TEXT("CastleBoardTestCreateNavigation"),
  TEXT("D18 编辑器集成专用：为已配置导航盒、明确缺少导航数据的编辑器世界创建导航数据，不保存资产。"),
  FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
 {
  if (!Check(World && World->WorldType==EWorldType::Editor && Args.IsEmpty(),TEXT("导航补建只接受编辑器世界"))) { return; }
  UNavigationSystemV1* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
  if (!Check(Nav!=nullptr,TEXT("地图缺少 NavigationSystem"))) { return; }
  if (!Check(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)==nullptr,TEXT("地图已有导航数据，不执行重复创建"))) { return; }
  ANavigationData* Data=Nav->GetDefaultNavDataInstance(FNavigationSystem::Create);
  if (!Check(Data!=nullptr,TEXT("导航系统原生创建失败"))) { return; }
  UE_LOG(LogMoving_Castle,Display,TEXT("W2_MAINMAP_NAV_CREATED: %s"),*Data->GetPathName());
 }));

 FAutoConsoleCommandWithWorldAndArgs Expansion(TEXT("CastleBoardTestMainMapExpansion"),
  TEXT("开发验收：记录 MainMap 原地块，隐藏扩张并逐圈到 15×15，检查全部原对象和内容不变。"),
  FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
 {
  if (!Check(World && World->IsGameWorld() && Args.IsEmpty(),TEXT("必须在 PIE 中无参数执行"))) { return; }
  ACastleBoard* Board=nullptr;
  int32 Count=0;
  for (TActorIterator<ACastleBoard> It(World); It; ++It) { Board=*It; ++Count; }
  if (!Check(Count==1,TEXT("需要唯一棋盘"))) { return; }
  FIntPoint Min,Max;
  Board->GetBoardExtent(Min,Max);
  if (!Check(Min==FIntPoint(0,0) && Max==FIntPoint(4,4),TEXT("需要尚未扩张的 5×5 棋盘"))) { return; }
  // 同时记录数据与实际对象，不能仅比较新生成结果而遗漏地块重建或世界位置变化。
  TMap<FIntVector,ACastleGroundTileActor*> Tiles;
  TMap<FIntVector,FCastleGroundCell> Cells;
  TMap<FIntVector,FVector> Locations;
  for (TActorIterator<ACastleGroundTileActor> It(World); It; ++It)
  {
   const FCastleGroundCell Cell=It->GetGroundCell();
   Tiles.Add(Cell.Coordinate,*It); Cells.Add(Cell.Coordinate,Cell);
   Locations.Add(Cell.Coordinate,It->GetActorLocation());
  }
  if (!Check(Tiles.Num()==25 && IsValid(Board->MainTowerActor),TEXT("初始 25 格或主塔缺失"))) { return; }
  const FVector TowerLocation=Board->MainTowerActor->GetActorLocation();
  const FVector RayCenter=Board->GetGridWorldLocation(FIntVector(0,0,0));
  FCastleGridCursorTarget Target;
  if (!Check(Board->TryResolveGridCursorTarget(RayCenter+FVector(0,0,100),RayCenter-FVector(0,0,600),Target),
   TEXT("初始地块射线不命中"))) { return; }
  Board->SetGroundPresentationHidden(true);
  if (!Check(!Board->TryResolveGridCursorTarget(RayCenter+FVector(0,0,100),RayCenter-FVector(0,0,600),Target),
   TEXT("隐藏后仍然命中地块"))) { return; }
  FText Reason;
  if (!CheckResult(Board->ExpandBoard(Reason),Reason)) { return; }
  int32 HiddenCount=0;
  for (TActorIterator<ACastleGroundTileActor> It(World); It; ++It)
  {
   ++HiddenCount;
   if (!Check(!It->GetTileMeshComponent()->IsVisible(),TEXT("隐藏期间扩张出现可见地块"))) { return; }
  }
  if (!Check(HiddenCount==49 && !Board->MainTowerActor->IsHidden(),TEXT("隐藏扩张格数或主塔可见性错误"))) { return; }
  Board->SetGroundPresentationHidden(false);
  for (int32 Step=0; Step<4; ++Step)
  { if (!CheckResult(Board->ExpandBoard(Reason),Reason)) { return; } }
  Board->GetBoardExtent(Min,Max);
  if (!Check(Max.X-Min.X+1==15 && Max.Y-Min.Y+1==15,TEXT("未扩张到 15×15"))) { return; }
  if (!Check(!Board->ExpandBoard(Reason),TEXT("超过上限未拒绝"))) { return; }
  FIntPoint AfterMin,AfterMax;
  Board->GetBoardExtent(AfterMin,AfterMax);
  if (!Check(Min==AfterMin && Max==AfterMax,TEXT("超限失败改变了范围"))) { return; }
  int32 VisibleCount=0;
  for (TActorIterator<ACastleGroundTileActor> It(World); It; ++It)
  {
   ++VisibleCount;
   const FCastleGroundCell Cell=It->GetGroundCell();
   if (!Check(It->GetTileMeshComponent()->IsVisible(),TEXT("显示后仍有隐藏地块"))) { return; }
   if (ACastleGroundTileActor** Old=Tiles.Find(Cell.Coordinate))
   {
    const FCastleGroundCell& Before=Cells.FindChecked(Cell.Coordinate);
    if (!Check(*Old==*It && Before.Color==Cell.Color && Before.Number==Cell.Number
     && Locations.FindChecked(Cell.Coordinate).Equals(It->GetActorLocation()),TEXT("原地块对象、内容或位置变化"))) { return; }
   }
  }
  if (!Check(VisibleCount==225 && TowerLocation.Equals(Board->MainTowerActor->GetActorLocation()),
   TEXT("最终格数或主塔位置错误"))) { return; }
  UE_LOG(LogMoving_Castle,Display,TEXT("W2_MAINMAP_EXPANSION_PASS: 隐藏扩张 5→7；显示后 9→11→13→15；超限拒绝；原 25 格对象、颜色数字、世界位置及主塔位置保持。"));
 }));

 FAutoConsoleCommandWithWorldAndArgs Setup(TEXT("CastleBoardTestMainMapOverhang"),
  TEXT("开发验收：在未扩张的 MainMap 放置正式越界建筑，检查两格及八层上限。仅用于新 PIE。"),
  FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
 {
  if (!Check(World && World->IsGameWorld() && Args.IsEmpty(), TEXT("必须在新 PIE 中无参数执行"))) { return; }
  ACastleBoard* Board = nullptr;
  int32 Count = 0;
  for (TActorIterator<ACastleBoard> It(World); It; ++It) { Board=*It; ++Count; }
  if (!Check(Count==1, TEXT("需要唯一棋盘"))) { return; }
  FIntPoint Min,Max;
  Board->GetBoardExtent(Min,Max);
  if (!Check(Min==FIntPoint(0,0) && Max==FIntPoint(4,4) && Board->GridSize.Z==8,
   TEXT("需要 MainMap 初始 5×5×8 范围"))) { return; }
  if (!Check(Board->GetActorLocation().Equals(FVector(0,0,1000)) && IsValid(Board->MainTowerActor),
   TEXT("D18b 棋盘位置或开局主塔不正确"))) { return; }
  UCastleBuildingDefinition* Definition=Board->FindBuildingDefinition(ECastleBuildingType::NormalRoom);
  if (!Check(IsValid(Definition) && IsValid(Board->PrototypeBackpack), TEXT("缺少正式普通房间定义或背包"))) { return; }
  // 不清空玩家卡牌、不自动扩大容量：验收前必须由测试夹具显式留出一个空位。
  if (!Check(Board->PrototypeBackpack->GetItemCount()<Board->PrototypeBackpack->Capacity,
   TEXT("验收需要至少一个背包空位；请在本次 PIE 测试夹具中显式配置，正式默认容量不变"))) { return; }
  FText Reason;
  FCastleGroundCell Ground;
  if (!Check(Board->TryGetGroundCell(FIntVector(4,2,0),Ground), TEXT("边缘地块缺失"))) { return; }
  FCastleBuildingDescriptor Card;
  if (!CheckResult(Definition->CreateDescriptor(1,Ground.Color,Ground.Number,Card,Reason),Reason)) { return; }
  ACastleBuildingBase* Building=Board->PlaceBuilding(FIntVector(4,2,0),Card,false,Reason);
  if (!Finish(Building)) { return; }
  // 用真实建筑射线解析侧面，正式背包入口负责校验、扣牌、生成和登记。
  for (int32 X=5; X<=6; ++X)
  {
   FCastleGridCursorTarget Target;
   // 正式普通房间的侧面中部为空，瞄准底面以上 518 cm 的上沿横梁，模拟玩家指向可见结构。
   const FVector Center=Building->GetActorLocation()+FVector(0,0,Board->LayerHeight-42.f);
   if (!Check(Board->TryResolveGridCursorTarget(Center+FVector(Board->CellSize,0,0),Center,Target),
    TEXT("无法从真实建筑侧面解析越界来源"))) { return; }
   if (!Check(Target.AdjacentCell==FIntVector(X,2,0), TEXT("侧面射线解析到了错误占用格"))) { return; }
   int32 Slot;
   if (!CheckResult(Board->PrototypeBackpack->TryAddItem(Card,Slot,Reason),Reason)) { return; }
   Building=Board->PlaceBackpackBuildingAtCursor(Target,Slot,Reason);
   if (!Finish(Building)) { return; }
  }
  FCastleGridCursorTarget Third;
  const FVector Center=Building->GetActorLocation()+FVector(0,0,Board->LayerHeight-42.f);
  if (!Check(Board->TryResolveGridCursorTarget(Center+FVector(Board->CellSize,0,0),Center,Third),
   TEXT("第三格候选不可解析"))) { return; }
  if (!Check(!Board->CanPlaceBuildingAtCursor(Third,Card,Reason)
   && Reason.ToString().Contains(TEXT("最多伸出棋盘边界 2 格")),TEXT("第三格未按 D10 拒绝"))) { return; }
  if (!Check(!Board->CanPlaceBuilding(FIntVector(-1,2,0),Card,Reason),TEXT("外围坐标入口错误放行"))) { return; }
  if (!Check(!Board->CanPlaceBuilding(FIntVector(3,3,1),Card,Reason),TEXT("无下方匹配来源的坐标入口错误放行"))) { return; }
  // 在第二个外围格逐层建到第八层，保留场景供视口检查。
  for (int32 Z=1; Z<8; ++Z)
  {
   FCastleGridCursorTarget Top;
   Top.bValid=true; Top.Cell=FIntVector(6,2,Z-1);
   Top.Face=ECastleGridFace::Up; Top.AdjacentCell=FIntVector(6,2,Z);
   int32 Slot;
   if (!CheckResult(Board->PrototypeBackpack->TryAddItem(Card,Slot,Reason),Reason)) { return; }
   Building=Board->PlaceBackpackBuildingAtCursor(Top,Slot,Reason);
   if (!Finish(Building)) { return; }
  }
  FCastleGridCursorTarget Ninth;
  Ninth.bValid=true; Ninth.Cell=FIntVector(6,2,7);
  Ninth.Face=ECastleGridFace::Up; Ninth.AdjacentCell=FIntVector(6,2,8);
  if (!Check(!Board->CanPlaceBuildingAtCursor(Ninth,Card,Reason),TEXT("第九层错误放行"))) { return; }
  UE_LOG(LogMoving_Castle, Display, TEXT("W2_MAINMAP_OVERHANG_PASS: 正式背包放置外围 1/2 格，第三格拒绝；外围叠到第八层，第九层拒绝；无下方匹配来源的内部坐标入口和外围首层坐标入口拒绝。"));
 }));
}
#endif

