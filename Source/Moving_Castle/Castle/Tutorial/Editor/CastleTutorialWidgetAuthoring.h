#pragma once

#if WITH_EDITOR
class UCastleTutorialConfigDataAsset;

/** 编辑器专用布局构建器，蓝图只持有原生控件和样式。 */
bool BuildCastleTutorialWidgets(UCastleTutorialConfigDataAsset& Config);
#endif
