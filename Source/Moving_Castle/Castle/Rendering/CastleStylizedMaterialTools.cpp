#include "CastleStylizedMaterialTools.h"

#include "Engine/ToonProfile.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialInstanceConstant.h"
#include "StaticParameterSet.h"

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
	// 纯 VSM 遮挡统一进入背光档，不随遮挡物与接收面的距离变化。
	Settings.ShadowExtinctionCoefficient = 0.0f;
	Settings.DiffuseIndirectScale = 1.0f;
	Settings.SpecularIndirectScale = 1.0f;
	// 半 Lambert 与 VSM 透射共同决定横轴，窄过渡保留斜面抗锯齿。
	SetRamp(Settings.DiffuseRamp, ShadowColor, FLinearColor::White, 0.53f, 0.56f);
	SetRamp(Settings.SpecularRamp, FLinearColor::Black, HighlightColor, 0.72f, 0.74f);
	// 由资产自己的编辑器回调刷新渲染线程中的 Profile 图集。
	Profile->PostEditChange();
	return true;
#else
	return ReportMaterialError(Profile, TEXT("Toon Profile 写入只能在编辑器中运行"));
#endif
}

bool UCastleStylizedMaterialTools::ConnectLegacySurface(UMaterial* Material, UMaterialExpressionMaterialFunctionCall* Call)
{
#if WITH_EDITOR
	UMaterialEditorOnlyData* Data = Material ? Material->GetEditorOnlyData() : nullptr;
	if (!Data || !Call || Call->GetOuter() != Material || !Data->BaseColor.IsConnected())
	{
		return ReportMaterialError(Material, TEXT("迁移需保留已接线的固有色，且 Toon 调用须属于此材质"));
	}
	// 按真实引脚名定位，复制完整 FExpressionInput，保留诸如 FBX 颜色函数的多输出索引。
	const TPair<FName, const FExpressionInput*> Bindings[] = {
		{TEXT("BaseColor"), &Data->BaseColor}, {TEXT("Emissive"), &Data->EmissiveColor}, {TEXT("Normal"), &Data->Normal}
	};
	for (const auto& Binding : Bindings)
	{
		FExpressionInput* Destination = nullptr;
		for (FFunctionExpressionInput& Input : Call->FunctionInputs)
		{
			if (Input.Input.InputName == Binding.Key)
			{
				Destination = &Input.Input;
				break;
			}
		}
		if (!Destination)
		{
			return ReportMaterialError(Material, *FString::Printf(TEXT("共享 Toon 函数缺少引脚 %s"), *Binding.Key.ToString()));
		}
		if (Binding.Value == &Data->EmissiveColor && Data->EmissiveColor.UseConstant
			&& !Data->EmissiveColor.Constant.Equals(FLinearColor::Black, 0.0f))
		{
			// 色板材质使用连接的固有色以及根节点内联的自发光。编译器先读取内联常量。
			auto* Constant = Cast<UMaterialExpressionConstant3Vector>(Destination->Expression);
			if (!Constant || Constant == Data->EmissiveColor.Expression || Constant->Constant != Data->EmissiveColor.Constant)
			{
				Material->Modify();
				Constant = NewObject<UMaterialExpressionConstant3Vector>(Material, NAME_None, RF_Transactional);
				Constant->Constant = Data->EmissiveColor.Constant;
				Constant->MaterialExpressionEditorX = Call->MaterialExpressionEditorX - 220;
				Constant->MaterialExpressionEditorY = Call->MaterialExpressionEditorY + 150;
				Material->GetExpressionCollection().AddExpression(Constant);
				Destination->Connect(0, Constant);
			}
		}
		else
		{
			*Destination = *Binding.Value;
			Destination->InputName = Binding.Key;
		}
	}
	return true;
#else
	return ReportMaterialError(Material, TEXT("材质迁移只能在编辑器中运行"));
#endif
}

TArray<FString> UCastleStylizedMaterialTools::GetUnsupportedOverrides(const UMaterialInstanceConstant* Instance,
	const UMaterialInterface* Parent)
{
	TArray<FString> Missing;
#if WITH_EDITOR
	if (!Instance || !Parent)
	{
		ReportMaterialError(Instance, TEXT("核对覆盖需要有效实例和目标母材质"));
		Missing.Add(TEXT("无效的实例或母材质"));
		return Missing;
	}
	// 按类型和关联位置比较，避免同名的标量和颜色被当作同一个参数。
	const auto Inspect = [&Missing, Parent](EMaterialParameterType Type, const auto& Values)
	{
		if (Values.IsEmpty()) { return; }
		TArray<FMaterialParameterInfo> Parameters;
		TArray<FGuid> Ids;
		Parent->GetAllParameterInfoOfType(Type, Parameters, Ids);
		for (const auto& Value : Values)
		{
			// UE 将 RefractionDepthBias 存在实例的标量数组，但通过 GetRefractionSettings 读取，普通表达式列表不登记它。
			float RefractionBias = 0.0f;
			const bool bBuiltinRefraction = Type == EMaterialParameterType::Scalar
				&& Value.ParameterInfo == FMaterialParameterInfo(TEXT("RefractionDepthBias"))
				&& Parent->GetMaterial()->GetRefractionSettings(RefractionBias);
			if (!Parameters.Contains(Value.ParameterInfo) && !bBuiltinRefraction)
			{
				Missing.Add(FString::Printf(TEXT("%u:%s"), static_cast<uint32>(Type), *Value.ParameterInfo.Name.ToString()));
			}
		}
	};
	Inspect(EMaterialParameterType::Scalar, Instance->ScalarParameterValues);
	Inspect(EMaterialParameterType::Vector, Instance->VectorParameterValues);
	Inspect(EMaterialParameterType::DoubleVector, Instance->DoubleVectorParameterValues);
	Inspect(EMaterialParameterType::Texture, Instance->TextureParameterValues);
	Inspect(EMaterialParameterType::TextureCollection, Instance->TextureCollectionParameterValues);
	Inspect(EMaterialParameterType::ParameterCollection, Instance->ParameterCollectionParameterValues);
	Inspect(EMaterialParameterType::RuntimeVirtualTexture, Instance->RuntimeVirtualTextureParameterValues);
	Inspect(EMaterialParameterType::SparseVolumeTexture, Instance->SparseVolumeTextureParameterValues);
	Inspect(EMaterialParameterType::Font, Instance->FontParameterValues);
	const FStaticParameterSet StaticParameters = Instance->GetStaticParameters();
	Inspect(EMaterialParameterType::StaticSwitch, StaticParameters.StaticSwitchParameters);
	Inspect(EMaterialParameterType::StaticComponentMask, StaticParameters.EditorOnly.StaticComponentMaskParameters);
#else
	ReportMaterialError(Instance, TEXT("材质迁移核对只能在编辑器中运行"));
	Missing.Add(TEXT("编辑器专用"));
#endif
	return Missing;
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
