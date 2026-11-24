#pragma once

#if WITH_EDITOR
class UCastleTutorialConfigDataAsset;

/** 编辑器专用布局构建器，蓝图只持有原生控件和样式。 */
bool BuildCastleTutorialWidgets(UCastleTutorialConfigDataAsset& Config);

/** 单独编译保存正式对话 WBP，供布局改版使用。 */
bool BuildCastleDialogueWidget();
#endif
