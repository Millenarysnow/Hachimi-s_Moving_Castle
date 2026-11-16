// 棋盘控制台入口只查找当前世界的唯一棋盘，不借用玩家控制器职责。
#include "Castle/Actors/CastleBoard.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Moving_Castle.h"
namespace CastleBoardDebug
{
 /** 屏幕反馈与日志分开，失败日志由具体操作保留规则要求的严重级别。 */
 void Screen(const FString& Text, bool bSuccess)
 {
  if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 6.f, bSuccess ? FColor::Green : FColor::Red, Text); }
 }
 /** 零个或多个都明确报错，绝不任取一个棋盘。 */
 ACastleBoard* FindUnique(UWorld* World)
 {
  ACastleBoard* Board = nullptr;
  int32 Count = 0;
  if (World) { for (TActorIterator<ACastleBoard> It(World); It; ++It) { Board = *It; ++Count; } }
  if (Count != 1)
  {
   const FString Reason = FString::Printf(TEXT("棋盘命令失败：当前世界有 %d 个棋盘，必须恰好为 1。"), Count);
   UE_LOG(LogMoving_Castle, Error, TEXT("%s"), *Reason);
   Screen(Reason, false);
   return nullptr;
  }
  return Board;
 }
 FAutoConsoleCommandWithWorldAndArgs Expand(TEXT("CastleExpandBoard"), TEXT("向四周扩张一圈。"),
 FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
 {
  if (!Args.IsEmpty())
  {
   UE_LOG(LogMoving_Castle, Error, TEXT("用法：CastleExpandBoard（不接受参数）。"));
   Screen(TEXT("用法：CastleExpandBoard"), false);
   return;
  }
  if (ACastleBoard* Board = FindUnique(World))
  {
   FText Reason;
   const bool bSuccess = Board->ExpandBoard(Reason);
   FIntPoint Min, Max;
   Board->GetBoardExtent(Min, Max);
   Screen(bSuccess ? FString::Printf(TEXT("扩张成功：%d×%d，范围 (%d,%d)～(%d,%d)。"),
    Max.X-Min.X+1, Max.Y-Min.Y+1, Min.X, Min.Y, Max.X, Max.Y) : Reason.ToString(), bSuccess);
  }
 }));
 FAutoConsoleCommandWithWorldAndArgs Hidden(TEXT("CastleSetGroundHidden"), TEXT("CastleSetGroundHidden 1/0：隐藏/显示地块。"),
 FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
 {
  if (Args.Num()!=1 || (Args[0]!=TEXT("0") && Args[0]!=TEXT("1")))
  {
   UE_LOG(LogMoving_Castle, Error, TEXT("用法：CastleSetGroundHidden 1 或 CastleSetGroundHidden 0。"));
   Screen(TEXT("用法：CastleSetGroundHidden 1/0"), false);
   return;
  }
  if (ACastleBoard* Board = FindUnique(World))
  {
   const bool bHidden = Args[0]==TEXT("1");
   Board->SetGroundPresentationHidden(bHidden);
   const bool bSuccess = Board->IsGroundPresentationHidden()==bHidden;
   const FString Message = bSuccess ? (bHidden ? TEXT("地块已隐藏。") : TEXT("地块已显示。")) : TEXT("地块显隐切换失败，请检查日志。");
   if (bSuccess) { UE_LOG(LogMoving_Castle, Display, TEXT("%s"), *Message); }
   Screen(Message, bSuccess);
  }
 }));

}
