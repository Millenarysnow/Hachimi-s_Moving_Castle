#include "CastleStylizedMaterialTools.h"

#include "Engine/ToonProfile.h"
#include "Materials/Material.h"

DEFINE_LOG_CATEGORY_STATIC(LogCastleStylizedMaterialTools, Log, All);

namespace
{
	/** 在 Python 异常之外保留 UE 原生日志，便于从无窗口运行的日志找到具体资产。 */
	bool ReportMaterialError(const UObject* Asset, const TCHAR* Reason)
	{
		UE_LOG(LogCastleStylizedMaterialTools, Error, TEXT("%s：%s"), *GetPathNameSafe(Asset), Reason);
		return false;
	}

#if WITH_EDITOR
	/** 用线性插值连接两个平台，控制过渡宽度，并避免曲线切线造成过冲。 */
	void SetRamp(FRuntimeCurveLinearColor& Ramp, FLinearColor Low, FLinearColor High, float Start, float End)
	{
		Ramp.ExternalCurve = nullptr;
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			FRichCurve& Curve = Ramp.ColorCurves[Channel];
			Curve.Reset();
			Curve.AddKey(0.0f, Low.Component(Channel));
			Curve.AddKey(Start, Low.Component(Channel));
			Curve.AddKey(End, High.Component(Channel));
			Curve.AddKey(1.0f, High.Component(Channel));
		}
	}
#endif
}

bool UCastleStylizedMaterialTools::ConfigureToonProfile(UToonProfile* Profile, FLinearColor ShadowColor, FLinearColor HighlightColor)
{
#if WITH_EDITOR
	if (!Profile || FVector3f(ShadowColor.R, ShadowColor.G, ShadowColor.B).ContainsNaN()
		|| FVector3f(HighlightColor.R, HighlightColor.G, HighlightColor.B).ContainsNaN()
		|| ShadowColor.GetMin() < 0.0f || ShadowColor.GetMax() > 1.0f
		|| HighlightColor.GetMin() < 0.0f || HighlightColor.GetMax() > 1.0f)
	{
		return ReportMaterialError(Profile, TEXT("需要有效的 Toon Profile 和 [0, 1] 范围内的线性色阶"));
	}
	Profile->Modify();
	FToonProfileStruct& Settings = Profile->Settings;
	Settings.bDiffuseRampIncludeShadow = true;
	Settings.DiffuseIndirectScale = 1.0f;
	Settings.SpecularIndirectScale = 1.0f;
	// 半 Lambert 与 VSM 透射共同决定横轴；0.54 到 0.56 是两档之间的窄过渡。
	SetRamp(Settings.DiffuseRamp, ShadowColor, FLinearColor::White, 0.54f, 0.56f);
	SetRamp(Settings.SpecularRamp, FLinearColor::Black, HighlightColor, 0.72f, 0.74f);
	// 由资产自己的编辑器回调刷新渲染线程中的 Profile 图集。
	Profile->PostEditChange();
	return true;
#else
	return ReportMaterialError(Profile, TEXT("Toon Profile 写入只能在编辑器中运行"));
#endif
}

bool UCastleStylizedMaterialTools::ReadV4Colors(UMaterial* Material, FLinearColor& BaseColor, FLinearColor& EmissiveColor)
{
#if WITH_EDITOR
	const UMaterialEditorOnlyData* Data = Material ? Material->GetEditorOnlyData() : nullptr;
	if (!Data || !Data->BaseColor.UseConstant || Data->BaseColor.IsConnected() || Data->EmissiveColor.IsConnected())
	{
		return ReportMaterialError(Material, TEXT("V4 材质必须含原始 BaseColor 常量，并且颜色输入不能已有节点"));
	}
	// 5.8 的 FColorMaterialInput 存储线性 FLinearColor。导入器写入的 FColor 已按 sRGB 转过一次。
	BaseColor = Data->BaseColor.Constant;
	// 未为自发光启用常量的旧 V4 材质，原图的输入语义为零。
	EmissiveColor = Data->EmissiveColor.UseConstant ? Data->EmissiveColor.Constant : FLinearColor::Black;
	return true;
#else
	return ReportMaterialError(Material, TEXT("V4 材质输入只能在编辑器中读取"));
#endif
}
