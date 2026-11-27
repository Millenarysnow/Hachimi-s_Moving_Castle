#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CastleDialogueScriptDataAsset.generated.h"

class UTexture2D;
class USoundBase;

/** 台词由哪一侧角色发言；旁白不偏向任一立绘。 */
UENUM(BlueprintType)
enum class ECastleDialogueFocusSide : uint8
{
	Left,
	Right,
	Narrator
};

/** 单条分支选项只保存显示文案和目标行 ID，不在 Widget 中硬编码剧情。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleDialogueChoice
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话")
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话")
	FName TargetLineId;
};

/** 可独立跳转的剧本行，左右立绘和发言侧均由数据资产显式指定。 */
USTRUCT(BlueprintType)
struct MOVING_CASTLE_API FCastleDialogueLine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话")
	FName LineId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话")
	FText SpeakerName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话")
	TObjectPtr<UTexture2D> LeftPortrait = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话")
	TObjectPtr<UTexture2D> RightPortrait = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话")
	ECastleDialogueFocusSide FocusSide = ECastleDialogueFocusSide::Narrator;

	/** 支持 {PlaceKey} 与 {DemolishKey}，在播放及切换操作模式时解析。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话", meta = (MultiLine = "true"))
	FText TextTemplate;

	/** 独有台词使用固定语音，旺财则在音频总表的三条短音中抽取。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话|语音")
	TObjectPtr<USoundBase> VoiceLine = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话|语音")
	bool bUseWangcaiVoice = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话")
	TArray<FCastleDialogueChoice> Choices;

	/** 结束标志只能出现在数组末行，分支行的流向由 Choices 控制。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话")
	bool bEndsScript = false;
};

/** 通用类 Galgame 台词资源；无效行号、目标行或打字机速率会拒绝播放。 */
UCLASS(BlueprintType)
class MOVING_CASTLE_API UCastleDialogueScriptDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话")
	TArray<FCastleDialogueLine> Lines;

	/** 打字机每秒揭示的字符数。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话")
	float CharactersPerSecond = 35.0f;

	/** 正式剧本逐行要求配音，临时剧本保留可选空语音以支持自动化构造。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "对话|语音")
	bool bRequireVoiceLines = false;

	/** 检查行 ID 唯一、分支目标存在且只有末行能结束剧本。 */
	bool ValidateScript(FText& OutFailureReason) const;
	int32 FindLineIndex(FName LineId) const;

private:
	/** 校验会被多次调用，每个临时剧本对象只需报告一次缺少配音。 */
	mutable bool bReportedOptionalVoice = false;
};
