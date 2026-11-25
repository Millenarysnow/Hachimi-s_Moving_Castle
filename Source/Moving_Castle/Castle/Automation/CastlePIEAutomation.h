#pragma once

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationCommon.h"

namespace CastlePIEAutomation
{
	/** 前一用例可能保留了 PIE 地图，直接加载正式关卡后再发出独立游戏请求。 */
	inline bool StartPIE(FAutomationTestBase& Test, const TCHAR* Map, FIntPoint Size = FIntPoint::ZeroValue)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(Map, FPackageName::GetMapPackageExtension());
		if (!Test.TestTrue(TEXT("载入磁盘上的正式地图"), FEditorFileUtils::LoadMap(Filename, false, false)))
		{
			return false;
		}
		auto* Settings = DuplicateObject(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
		Settings->SetPlayNetMode(PIE_Standalone);
		Settings->SetPlayNumberOfClients(1);
		Settings->SetRunUnderOneProcess(true);
		if (Size != FIntPoint::ZeroValue)
		{
			// 独立窗口保留验收像素尺寸，避免复用编辑器布局中的任意小视口。
			Settings->NewWindowWidth = Size.X;
			Settings->NewWindowHeight = Size.Y;
		}
		FRequestPlaySessionParams Params;
		Params.EditorPlaySettings = Settings;
		Params.bAllowOnlineSubsystem = false;
		GEditor->RequestPlaySession(Params);
		return true;
	}

	/** 等待 PIE 完全退出，下一项测试才能加载关卡并重新持有视口。 */
	inline FFunctionLatentCommand StopPIE()
	{
		return FFunctionLatentCommand([bRequested = false]() mutable
		{
			if (!GEditor->PlayWorld)
			{
				if (!bRequested) { GEditor->CancelRequestPlaySession(); }
				return true;
			}
			if (!bRequested)
			{
				GEditor->RequestEndPlayMap();
				bRequested = true;
			}
			return false;
		});
	}
}
#endif
