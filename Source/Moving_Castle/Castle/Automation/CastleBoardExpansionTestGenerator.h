// 专用于扩张拒绝测试的显式错误策略，不参与正式默认配置。
#pragma once
#include "CastleGroundGenerator.h"
#include "CastleBoardExpansionTestGenerator.generated.h"
/** 每次请求的数字故意依赖宽度，用来验证棋盘拒绝覆盖旧数据。 */
UCLASS()
class UCastleSizeDependentTestGroundGenerator : public UCastleGroundGenerator
{
 GENERATED_BODY()
public:
 virtual FCastleGroundGenerationResult GenerateGround_Implementation(const FCastleGroundGenerationRequest& Request) const override
 {
  FCastleGroundGenerationResult Result;
  for (int32 Y=0; Y<Request.GridSize.Y; ++Y)
  {
   for (int32 X=0; X<Request.GridSize.X; ++X)
   {
    FCastleGroundCell& Cell = Result.Cells.AddDefaulted_GetRef();
    Cell.Coordinate = Request.Origin + FIntVector(X,Y,0);
    Cell.Color = ECastleTileColor::Red;
    Cell.Number = Request.GridSize.X;
   }
  }
  return Result;
 }
};
