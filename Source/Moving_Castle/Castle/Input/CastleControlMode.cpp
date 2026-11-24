// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleControlMode.h"

#include "Misc/ConfigCacheIni.h"
#include "Moving_Castle.h"

namespace
{
	constexpr TCHAR ConfigSection[] = TEXT("MovingCastle.ControlSettings");
	constexpr TCHAR ConfigKey[] = TEXT("ControlMode");
	constexpr TCHAR ClassicValue[] = TEXT("Classic");
	constexpr TCHAR DesignValue[] = TEXT("Design");
}

const FCastleControlBindings& CastleControlInput::GetBindings(ECastleControlMode Mode)
{
	static const FCastleControlBindings Classic{{EKeys::RightMouseButton, EKeys::LeftMouseButton, EKeys::Invalid}};
	static const FCastleControlBindings Design{{EKeys::LeftMouseButton, EKeys::RightMouseButton, EKeys::MiddleMouseButton}};
	check(Mode == ECastleControlMode::Classic || Mode == ECastleControlMode::Design);
	return Mode == ECastleControlMode::Design ? Design : Classic;
}

TArray<FKey> CastleControlInput::GetPointerKeys()
{
	// 一次绑定所有预设使用的指针键。切模式只改变分流，不在输入迭代期间修改绑定表。
	TArray<FKey> Keys;
	for (const ECastleControlMode Mode : {ECastleControlMode::Classic, ECastleControlMode::Design})
	{
		const FCastleControlBindings& Bindings = GetBindings(Mode);
		Keys.AddUnique(Bindings.Get(ECastleWorldInputAction::PrimaryInteract));
		Keys.AddUnique(Bindings.Get(ECastleWorldInputAction::Demolish));
	}
	return Keys;
}

FText CastleControlInput::GetKeyDisplayText(const FKey& Key)
{
	if (Key == EKeys::LeftMouseButton) { return INVTEXT("鼠标左键"); }
	if (Key == EKeys::RightMouseButton) { return INVTEXT("鼠标右键"); }
	if (Key == EKeys::MiddleMouseButton) { return INVTEXT("鼠标中键"); }
	return Key.GetDisplayName();
}

bool CastleControlInput::LoadMode(ECastleControlMode& OutMode)
{
	FString Value;
	// 尚未保存设置的玩家使用经典预设。非法配置也必须在日志中可定位。
	if (GConfig->GetString(ConfigSection, ConfigKey, Value, GGameUserSettingsIni))
	{
		if (Value == DesignValue)
		{
			OutMode = ECastleControlMode::Design;
			return true;
		}
		if (Value != ClassicValue)
		{
			UE_LOG(LogMoving_Castle, Error, TEXT("操作模式配置 %s [%s] %s=%s 无效。"),
				*GGameUserSettingsIni, ConfigSection, ConfigKey, *Value);
			return false;
		}
	}
	OutMode = ECastleControlMode::Classic;
	return true;
}

void CastleControlInput::SaveMode(ECastleControlMode Mode)
{
	const TCHAR* Value = Mode == ECastleControlMode::Design ? DesignValue : ClassicValue;
	GConfig->SetString(ConfigSection, ConfigKey, Value, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}
