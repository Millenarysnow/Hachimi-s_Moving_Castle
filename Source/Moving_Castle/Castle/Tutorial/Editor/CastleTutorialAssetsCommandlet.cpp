#include "CastleTutorialAssetsCommandlet.h"
#include "Moving_Castle.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Castle/Combat/CastleCombatConfigDataAsset.h"
#include "Castle/Combat/CastleDifficultyCurveDataAsset.h"
#include "Castle/Dialogue/CastleDialogueScriptDataAsset.h"
#include "Castle/Director/CastleDirectorConfigDataAsset.h"
#include "Castle/Guide/CastleGuideDataAsset.h"
#include "Castle/Tutorial/CastleTutorialConfigDataAsset.h"
#include "Castle/Tutorial/CastleTutorialDirector.h"
#include "Castle/UI/Director/CastleDirectorWidgets.h"
#include "CastleBoard.h"
#include "CastleBuildingDefinition.h"
#include "CastleTutorialWidgetAuthoring.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Factories/TextureFactory.h"
#include "FileHelpers.h"
#include "Interaction/CastlePatrolRoute.h"
#include "MassEntityConfigAsset.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/SavePackage.h"

namespace
{
	const FString DataRoot(TEXT("/Game/MovingCastle/Data/Tutorial/"));
	const FString PortraitRoot(TEXT("/Game/MovingCastle/UI/Dialogue/Portraits/"));
	constexpr TCHAR TutorialMap[] = TEXT("/Game/MovingCastle/Maps/TutorialMap");
	/** 四色池下复现教学首层四面黄色及原数字，固定牌能沿预设回路完成农田替换。 */
	constexpr int32 TutorialSeed = 369972;

	template<typename T>
	T* FindAsset(const FString& Path, uint32 LoadFlags = LOAD_None)
	{
		return LoadObject<T>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)), nullptr, LoadFlags);
	}

	/** 重跑时原位刷新数据，保留引用该资源的蓝图和地图对象路径。 */
	template<typename T>
	T* AuthorAsset(const FString& Path)
	{
		if (auto* Existing = FindAsset<T>(Path, LOAD_NoWarn | LOAD_Quiet)) { return Existing; }
		auto* Asset = NewObject<T>(CreatePackage(*Path), FName(*FPackageName::GetShortName(Path)), RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Asset);
		return Asset;
	}

	bool Save(UObject& Asset)
	{
		Asset.MarkPackageDirty();
		auto* Package = Asset.GetOutermost();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, &Asset, *Filename, Args);
	}

	/** 贴图名与交付文件逐个对应。原生 TextureFactory 保存 PNG 的透明度和原始尺寸。 */
	bool ImportPortraits(const FString& Root, TMap<FName, UTexture2D*>& OutPortraits)
	{
		struct FPortraitSource { const TCHAR* Id; const TCHAR* File; };
		const FPortraitSource Sources[] = {
			{TEXT("Hachimis_Uh"), TEXT("哈基米德/呃.png")},
			{TEXT("Hachimis_Cry"), TEXT("哈基米德/呜哇.png")},
			{TEXT("Hachimis_Hmph"), TEXT("哈基米德/哼哼.png")},
			{TEXT("Hachimis_Normal"), TEXT("哈基米德/嗯.png")},
			{TEXT("Hachimis_Shock"), TEXT("哈基米德/噫.png")},
			{TEXT("Hachimis_Proud"), TEXT("哈基米德/得意.png")},
			{TEXT("Hachimis_Smug"), TEXT("哈基米德/拽拽的.png")},
			{TEXT("Hachimis_Dizzy"), TEXT("哈基米德/晕乎乎.png")},
			{TEXT("Hachimis_Ehe"), TEXT("哈基米德/诶嘿.png")},
			{TEXT("Wangcai_Disdain"), TEXT("机械狗助手-墨镜管家-动作贴纸/01-嫌弃.png")},
			{TEXT("Wangcai_Serious"), TEXT("机械狗助手-墨镜管家-动作贴纸/02-认真.png")},
			{TEXT("Wangcai_Cool"), TEXT("机械狗助手-墨镜管家-动作贴纸/03-冷漠.png")},
		};
		for (const auto& Source : Sources)
		{
			const FString File = Root / Source.File;
			if (!FPaths::FileExists(File))
			{
				UE_LOG(LogMoving_Castle, Error, TEXT("正式立绘源文件不存在：%s"), *File);
				return false;
			}
			const FString Name = FString(TEXT("T_Portrait_")) + Source.Id;
			const FString Path = PortraitRoot + Name;
			auto* Texture = FindAsset<UTexture2D>(Path, LOAD_NoWarn | LOAD_Quiet);
			if (!Texture)
			{
				auto* Factory = NewObject<UTextureFactory>();
				Factory->SuppressImportOverwriteDialog();
				bool bCancelled = false;
				Texture = Cast<UTexture2D>(UFactory::StaticImportObject(UTexture2D::StaticClass(), CreatePackage(*Path),
					FName(*Name), RF_Public | RF_Standalone, bCancelled, *File, nullptr, Factory));
				if (!Texture)
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("导入正式立绘失败：%s"), *File);
					return false;
				}
				FAssetRegistryModule::AssetCreated(Texture);
			}
			Texture->LODGroup = TEXTUREGROUP_UI;
			Texture->MipGenSettings = TMGS_NoMipmaps;
			Texture->CompressionSettings = TC_EditorIcon;
			Texture->SRGB = true;
			Texture->PostEditChange();
			if (!Save(*Texture)) { return false; }
			OutPortraits.Add(FName(Source.Id), Texture);
		}
		return true;
	}

	/** 语气表情和说话人随台词配置，角色尚未出场时只留该侧为空。 */
	struct FLine
	{
		const TCHAR* Text;
		const TCHAR* Left;
		const TCHAR* Right;
		bool bWangcai = false;
		FName Id = NAME_None;
		TArray<FCastleDialogueChoice> Choices;
	};

	UCastleDialogueScriptDataAsset* Script(const TCHAR* Name, const TMap<FName, UTexture2D*>& Portraits,
		std::initializer_list<FLine> Rows)
	{
		auto* Asset = AuthorAsset<UCastleDialogueScriptDataAsset>(DataRoot + Name);
		Asset->Lines.Reset();
		for (const FLine& Source : Rows)
		{
			auto& Row = Asset->Lines.AddDefaulted_GetRef();
			Row.LineId = Source.Id.IsNone() ? FName(TEXT("Line"), Asset->Lines.Num()) : Source.Id;
			Row.SpeakerName = Source.bWangcai ? INVTEXT("旺财") : INVTEXT("哈基米德");
			Row.FocusSide = Source.bWangcai ? ECastleDialogueFocusSide::Right : ECastleDialogueFocusSide::Left;
			Row.TextTemplate = FText::AsCultureInvariant(FString(Source.Text));
			Row.Choices = Source.Choices;
			if (Source.Left)
			{
				Row.LeftPortrait = Portraits.FindRef(FName(*(FString(TEXT("Hachimis_")) + Source.Left)));
				if (!Row.LeftPortrait)
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("%s 使用了未导入的哈基米德表情 %s。"), Name, Source.Left);
					return nullptr;
				}
			}
			if (Source.Right)
			{
				Row.RightPortrait = Portraits.FindRef(FName(*(FString(TEXT("Wangcai_")) + Source.Right)));
				if (!Row.RightPortrait)
				{
					UE_LOG(LogMoving_Castle, Error, TEXT("%s 使用了未导入的旺财表情 %s。"), Name, Source.Right);
					return nullptr;
				}
			}
		}
		Asset->Lines.Last().bEndsScript = true;
		FText Reason;
		return Asset->ValidateScript(Reason) && Save(*Asset) ? Asset : nullptr;
	}

	bool AuthorTutorialScripts(UCastleTutorialConfigDataAsset& Config, const TMap<FName, UTexture2D*>& P)
	{
		// 编排器仅决定何时播放，原文台词、语气与目标显示均保存在关卡数据中。
		Config.Steps = {
			{1, Script(TEXT("DA_Tutorial_S01"), P, {
				{TEXT("哼哼，大管家你来了，你就是那个能让我的城堡重返荣光的人吗？"), TEXT("Hmph"), nullptr},
				{TEXT("额，它现在看上去可能只有一座塔，但曾经可是万人瞩目的空中堡垒：哈基米德移动城堡，而本小姐正是它的建造者，哈基米德。那段时间无数人都想涉足本小姐的城堡，却遭到我的拒绝。所以你能在这里已经可以感到荣幸了！"), TEXT("Uh"), nullptr},
				{TEXT("小姐的意思是终于把您等来了，可急死她了。另外其实城堡都是我建的。"), TEXT("Uh"), TEXT("Disdain"), true},
				{TEXT("咳咳，不要多嘴。总之我现在正是聘用你为我的管家了。旺财现在是你的助手，之后你要建造什么建筑都可以找他。才不是我懒得干活哦不要误会。"), TEXT("Smug"), TEXT("Cool")},
				{TEXT("什么时候能给我改个名就最好了。"), TEXT("Smug"), TEXT("Disdain"), true},
				{TEXT("好啦我可靠的大管家，我给你买了几个房间，已经放在建筑栏里了。"), TEXT("Ehe"), TEXT("Cool")},
				{TEXT("每个房间都有两种基础属性，颜色和数字。房间之间想要连接的话就需要数字或者颜色一致，就可以连接了。相互连接的房间才能和我的主塔一起飞在空中哦？"), TEXT("Normal"), TEXT("Serious")},
				{TEXT("虽然不知道这个房间是什么原理，但小姐想必是玩UNO上头了才这么设定的。"), TEXT("Uh"), TEXT("Disdain"), true},
				{TEXT("现在试试在主塔的正门前造第一个房间吧。"), TEXT("Normal"), TEXT("Serious")},
				{TEXT("{PlaceKey}点击主塔墙面我就会帮你建好的，要注意选择数字或颜色一样的房间哦。"), TEXT("Normal"), TEXT("Serious"), true}
			}), {TEXT("Building")}, INVTEXT("查看建造说明")},
			{2, nullptr, {}, INVTEXT("从建筑栏选普通房，用{PlaceKey}在主塔正门（+X）前建造第一个房间")},
			{3, Script(TEXT("DA_Tutorial_S03"), P, {
				{TEXT("不错，我城堡的伟大复兴已经迈出了结实的第一步！现在可以用我给的这些房间连接到主塔的后门吗，这样我就可以不走回头路的参观这些房间了。"), TEXT("Proud"), TEXT("Serious")},
				{TEXT("小姐的意思是要造一个回路从前门连接到后门。"), TEXT("Normal"), TEXT("Serious"), true}
			}), {}, INVTEXT("用普通房沿主塔任一侧绕一圈，连接正门（+X）和后门（−X）")},
			{4, Script(TEXT("DA_Tutorial_S04"), P, {
				{TEXT("太好了。但随着我们城堡的前进，会遇上一些可恶的小怪，要不是本小姐之前一时疏忽，也不会让我的城堡摔成如今的样子...呜。哼这次我吸取了教训，看到我给你准备的启动资金了吗，可以去右下角的商店里购买一个箭塔。"), TEXT("Cry"), TEXT("Serious")}
			}), {}, INVTEXT("按 B 或点击右下角商店")},
			{5, Script(TEXT("DA_Tutorial_S05"), P, {
				{TEXT("商店就是你购买房间的来源啦，选择购买这个箭塔吧。"), TEXT("Normal"), TEXT("Serious")}
			}), {}, INVTEXT("购买货架上的 1 级箭塔")},
			{6, Script(TEXT("DA_Tutorial_S06"), P, {
				{TEXT("箭塔已经在建筑栏里了，箭塔也拥有普通房间的基础属性，但拥有攻击的功能。你可以拆掉一块原本的普通房间放上箭塔。"), TEXT("Normal"), TEXT("Serious")},
				{TEXT("{DemolishKey}点击要拆的房间选择拆除就可以了，注意拆掉的空间箭塔也能拼接进去哦。"), TEXT("Normal"), TEXT("Serious"), true}
			}), {TEXT("Demolish")}, INVTEXT("用{DemolishKey}打开拆除菜单，回收一间普通房，把箭塔放入原位恢复回路")},
			{7, Script(TEXT("DA_Tutorial_S07"), P, {
				{TEXT("这下就不怕这些小怪了，不过箭塔还需要我们的士兵来激活才能投入使用。大管家现在我任命你兼职大将军，来管理我们士兵的行进路线吧。按P键可以进入路径编辑界面。"), TEXT("Proud"), TEXT("Serious")}
			}), {}, INVTEXT("按 P 进入路径编辑")},
			{8, Script(TEXT("DA_Tutorial_S08"), P, {
				{TEXT("现在我们拥有两个路径点，{PlaceKey}点击房间就可以按顺序设置路径点了。士兵就会从正门出来，按照顺序经过这些路径点然后从后门回到主塔，就这样持续到战斗结束。"), TEXT("Normal"), TEXT("Serious")}
			}), {TEXT("Patrol")}, INVTEXT("用{PlaceKey}按顺序点击标记的两个房间，再按 P 提交经过箭塔的路线")},
			{9, Script(TEXT("DA_Tutorial_S09"), P, {
				{TEXT("小姐容我提醒，我们真的有士兵吗？"), TEXT("Uh"), TEXT("Cool"), true},
				{TEXT("唔...这也不能怪本小姐啊，大管家你觉得该怎么办？"), TEXT("Dizzy"), TEXT("Cool"), false, NAME_None,
					{{INVTEXT("A：城主大人亲自上阵吧"), TEXT("CastleLord")}, {INVTEXT("B：旺财我们走"), TEXT("Wangcai")}}},
				{TEXT("让小姐去吧，小姐需要一些锻炼才能更加健康。"), TEXT("Shock"), TEXT("Disdain"), true, TEXT("Wangcai")},
				{TEXT("什么，竟然让本小姐做这种事情，呜也只好如此了，只是走几圈的话..."), TEXT("Cry"), TEXT("Cool"), false, TEXT("CastleLord")},
				{TEXT("管家大人准备好的话就可以驱动城堡向前行进了。"), TEXT("Cry"), TEXT("Serious"), true},
				{TEXT("怎么没有人在意本小姐的感受！"), TEXT("Shock"), TEXT("Cool")}
			}), {TEXT("Ghost"), TEXT("Archer")}, INVTEXT("迎接幽灵")},
			{10, Script(TEXT("DA_Tutorial_S10"), P, {
				{TEXT("这是幽灵，会接近主塔攻击气球，可能会成群结队的出现，请管家大人务必小心。不过好在我们已经提前准备了防御建筑，箭塔可以在每次激活后发射一定数量的箭矢，您可以在合适的时机{PlaceKey}点击主塔派出小姐去激活箭塔。箭塔以及幽灵的详细介绍我也放在“？”里了，您可以随时查阅。"), TEXT("Cry"), TEXT("Serious"), true}
			}), {}, INVTEXT("用{PlaceKey}点击主塔派出城主，让她途经箭塔清灭幽灵")},
			{11, Script(TEXT("DA_Tutorial_S11"), P, {
				{TEXT("每次战斗结束都可以从这三个选项选择一个来强化小姐的城堡。第一个选项是加经验值，可以升级主塔。第二个选项是给主塔回血，第三个选项是获得复制的随机功能房间。嗯，这次我们选复制房间选项吧，为了应对今后可能数量更庞大的小怪群，我们需要更充足的火力。"), TEXT("Normal"), TEXT("Serious"), true}
			}), {}, INVTEXT("选择复制功能房间，再点击箭塔卡收入建筑栏")},
			{12, Script(TEXT("DA_Tutorial_S12"), P, {
				{TEXT("小姐还是一如既往的聪明呢。"), TEXT("Proud"), TEXT("Cool"), true},
				{TEXT("好啦接下来本小姐来接手吧，功能房间是可以进行升级的，需要完全一样的房间按一定方式摆放便可以升级。管家把新获得的箭塔摆在原来的箭塔旁边试试。"), TEXT("Smug"), TEXT("Serious")}
			}), {}, INVTEXT("将复制的箭塔紧贴原箭塔放置，合成 2 级箭塔")},
			{13, Script(TEXT("DA_Tutorial_S13"), P, {
				{TEXT("很好，这样箭塔就升为了2级。箭塔变得更强壮了。下一步，我们的启动资金是有限的，所以还要安置一些经济房间，比如农田。我的管家应该明白要去哪里继续购买房间吧。"), TEXT("Proud"), TEXT("Serious")}
			}), {}, INVTEXT("打开商店查看货架，刷新后买下农田")},
			{14, Script(TEXT("DA_Tutorial_S14"), P, {
				{TEXT("记得放在士兵的移动路径上，可以通过改变路径点的位置来改变士兵移动路径哦。"), TEXT("Normal"), TEXT("Serious")}
			}), {}, INVTEXT("放置农田。按 P 调整路径点，确保巡逻路线经过农田")}
		};
		Config.ClaimDialogue = Script(TEXT("DA_Tutorial_Claim"), P, {
			{TEXT("呼呼，这个本小姐懂，管家把这个箭塔点击一下就能收进建筑栏里了。"), TEXT("Proud"), TEXT("Cool")}
		});
		Config.OutOfStockDialogue = Script(TEXT("DA_Tutorial_OutOfStock"), P, {
			{TEXT("唔这奸商，不好好备货就过来卖了，算了本小姐才不斤斤计较，刷新一下商店看看。"), TEXT("Hmph"), TEXT("Cool")}
		});
		Config.FarmOfferDialogue = Script(TEXT("DA_Tutorial_FarmOffer"), P, {
			{TEXT("好啦，看到这个农田房间了吗，可以在激活后提供一定数量的金币，正适合伟大复兴的哈基米德移动城堡，来让我们买下它。"), TEXT("Ehe"), TEXT("Serious")}
		});
		Config.ClosingDialogue = Script(TEXT("DA_Tutorial_Closing"), P, {
			{TEXT("不愧是小姐，这么快就要开始捞金币了吗？"), TEXT("Proud"), TEXT("Disdain"), true},
			{TEXT("金币是本小姐城堡的基本！好了，接下来就交给我的好管家了，有什么不记得的可以看旺财整理的材料。加油哦。不要让本小姐这么快就来帮忙。"), TEXT("Proud"), TEXT("Cool")},
			{TEXT("小姐的意思是她很看好管家大人哦。"), TEXT("Ehe"), TEXT("Serious"), true}
		});
		for (const auto& Step : Config.Steps)
		{
			if (Step.Number != 2 && !Step.Dialogue) { return false; }
		}
		return Config.ClaimDialogue && Config.OutOfStockDialogue && Config.FarmOfferDialogue && Config.ClosingDialogue;
	}

	bool AuthorGuide()
	{
		auto* Book = AuthorAsset<UCastleGuideDataAsset>(DataRoot + TEXT("DA_CastleGuideEntries"));
		Book->Entries = {
			{TEXT("Building"), INVTEXT("建造与连接"), INVTEXT("从建筑栏选一张房间卡。用{PlaceKey}点击主塔侧格或房间边缘放置。颜色或数字有一项相同即可连接。连接主塔正门与后门可以形成巡逻回路。"), nullptr, true},
			{TEXT("Demolish"), INVTEXT("拆除与回收"), INVTEXT("用{DemolishKey}点击建筑，选择拆除。普通房会回到建筑栏。拆除回路中的房间会切断巡逻，补上匹配房间即可恢复。")},
			{TEXT("Patrol"), INVTEXT("士兵巡逻路线"), INVTEXT("按 P 编辑路线，用{PlaceKey}依次为房间设路径点。士兵从正门出发，逐一经过路径点，最后从后门返回。")},
			{TEXT("Ghost"), INVTEXT("幽灵"), INVTEXT("幽灵会成群靠近主塔并攻击气球。派出巡逻人员及时激活防御建筑。")},
			{TEXT("Archer"), INVTEXT("箭塔"), INVTEXT("巡逻人员经过箭塔时会激活它，使其发射一批箭矢。相同色号、相同等级的箭塔紧贴放置后可以升级。")}
		};
		FText Reason;
		return Book->ValidateEntries(Reason) && Save(*Book);
	}

	bool AuthorIslandScripts(const TMap<FName, UTexture2D*>& P)
	{
		auto* Director = FindAsset<UCastleDirectorConfigDataAsset>(TEXT("/Game/MovingCastle/Data/Director/DA_DirectorConfig"));
		if (!Director) { return false; }
		Director->SkyIslandArrivalDialogues = {
			{ECastleSkyIslandType::Shop, Script(TEXT("DA_Arrival_Shop"), P, {
				{TEXT("这个商店里有好多特殊道具啊，想必一定能为本小姐的城堡添加一份助力"), TEXT("Ehe"), nullptr}
			})},
			{ECastleSkyIslandType::NPC, Script(TEXT("DA_Arrival_NPC"), P, {
				{TEXT("管家你来啦，快去招兵买马好把本小姐解放出来，呜...求你了"), TEXT("Cry"), nullptr}
			})},
			{ECastleSkyIslandType::Gamble, Script(TEXT("DA_Arrival_Gamble"), P, {
				{TEXT("好管家再借我一点钱，我一定能赚回来的..."), TEXT("Cry"), nullptr},
				{TEXT("管家大人，小姐这个月的工资可以被扣掉了嗷，之后打工积极的话再抵"), TEXT("Cry"), TEXT("Disdain"), true},
				{TEXT("怎么会这样...不对，本城主还要靠打工拿工资吗！？"), TEXT("Shock"), TEXT("Cool")}
			})}
		};
		for (const auto& Pair : Director->SkyIslandArrivalDialogues) { if (!Pair.Value) { return false; } }
		return Save(*Director);
	}

	bool AuthorBattleAndCards(UCastleTutorialConfigDataAsset& Config)
	{
		// 与 TutorialSeed 配套，一楼四面均为黄色，正门和后门同为 3。
		const auto Card = [](const TCHAR* Name)
		{
			FCastleBuildingDescriptor Result;
			Result.Definition = FindAsset<UCastleBuildingDefinition>(FString(TEXT("/Game/MovingCastle/Data/Buildings/")) + Name);
			Result.Color = ECastleTileColor::Yellow;
			Result.Number = 3;
			Result.Level = 1;
			return Result;
		};
		Config.StarterRooms.Init(Card(TEXT("DA_NormalRoom")), 5);
		Config.Archer = Card(TEXT("DA_ArcherTower"));
		Config.Farm = Card(TEXT("DA_Farm"));
		Config.StartingGold = 500;
		Config.HUDClass = LoadClass<UCastleDirectorHUDWidget>(nullptr,
			TEXT("/Game/MovingCastle/UI/Director/WBP_DirectorHUD.WBP_DirectorHUD_C"));
		// 正式幽灵模板包含飞行追击、近战主塔伤害和幽灵动画，沿用教程的一波三只出场规则。
		auto* Ghost = FindAsset<UMassEntityConfigAsset>(TEXT("/Game/MovingCastle/Mass/DA_MassEnemy_ShadowSwarm"));
		if (!Ghost) { return false; }
		auto* Curve = AuthorAsset<UCastleDifficultyCurveDataAsset>(DataRoot + TEXT("DA_TutorialDifficulty"));
		FCastleCombatWaveDefinition Wave;
		Wave.BugCurve.bEnabled = false;
		FCastleRaidSpawnEntry& Raid = Wave.RaidEntries.AddDefaulted_GetRef();
		Raid.EnemyId = TEXT("Ghost");
		Raid.Count = 3;
		Raid.SpawnInterval = 0.0f;
		// 教学一波让一级箭塔每支命中箭矢消灭一只幽灵，保留真实伤害和清场事件。
		Raid.AttributeOverrides.HealthOverride = 10.0f;
		Curve->NormalBattles = {{{Wave}}};
		Curve->EliteBattles = Curve->NormalBattles;
		Curve->CountGrowthPerOrdinal = 0.0f;
		auto* Battle = AuthorAsset<UCastleCombatConfigDataAsset>(DataRoot + TEXT("DA_TutorialGhostBattle"));
		Battle->Roster = {{TEXT("Ghost"), Ghost, 1, 1, 0.0f}};
		Battle->SpawnInnerRadius = 1550.0f;
		Battle->SpawnOuterRadius = 1650.0f;
		Battle->DifficultyCurve = Curve;
		Config.GhostBattle = Battle;
		FText Reason;
		return Battle->ValidateConfiguration(Reason) && Save(*Curve) && Save(*Battle);
	}

	bool AuthorMap(UCastleTutorialConfigDataAsset& Config)
	{
		// 使用正式地图为模板，继承飞行、棋盘、光照、导航场景及游戏模式。
		const bool bExisting = FPackageName::DoesPackageExist(TutorialMap);
		const TCHAR* Source = bExisting ? TutorialMap : TEXT("/Game/MovingCastle/Maps/MainMap");
		if (!FEditorFileUtils::LoadMap(Source, !bExisting, false)) { return false; }
		auto* World = GEditor->GetEditorWorldContext().World();
		if (!World) { return false; }
		TActorIterator<ACastleBoard> Board(World);
		if (!Board) { return false; }
		Board->Modify();
		Board->bDealPrototypeDeckOnBeginPlay = false;
		Board->bSpawnInitialEnemiesOnBeginPlay = false;
		Board->bUseFixedTowerSideSeed = true;
		Board->FixedTowerSideSeed = TutorialSeed;
		TActorIterator<ACastlePatrolRoute> ExistingRoute(World);
		auto* Route = ExistingRoute ? *ExistingRoute : World->SpawnActor<ACastlePatrolRoute>();
		if (!Route) { return false; }
		Route->Modify();
		Route->bSubmitDraftOnToggle = true;
		TActorIterator<ACastleTutorialDirector> ExistingDirector(World);
		auto* Director = ExistingDirector ? *ExistingDirector : World->SpawnActor<ACastleTutorialDirector>();
		if (!Director) { return false; }
		Director->Modify();
		Director->Configuration = &Config;
		return UEditorLoadingAndSavingUtils::SaveMap(World, TutorialMap);
	}
}
#endif

int32 UCastleTutorialAssetsCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	// 布局改版只写对话组件，避免重新导入立绘或另外改写教程配置、地图和剧本。
	if (FParse::Param(*Params, TEXT("OnlyDialogueWidget")))
	{
		if (!BuildCastleDialogueWidget()) { return 1; }
		UE_LOG(LogMoving_Castle, Display, TEXT("已编译保存 WBP_CastleDialogue。"));
		return 0;
	}
	FString PortraitSource;
	if (!FParse::Value(*Params, TEXT("PortraitSource="), PortraitSource) || PortraitSource.IsEmpty())
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("请通过 -PortraitSource 指定正式立绘交付目录。"));
		return 1;
	}
	auto* Config = AuthorAsset<UCastleTutorialConfigDataAsset>(DataRoot + TEXT("DA_TutorialConfig"));
	TMap<FName, UTexture2D*> Portraits;
	FText Reason;
	if (!ImportPortraits(PortraitSource, Portraits) || !BuildCastleTutorialWidgets(*Config)
		|| !AuthorTutorialScripts(*Config, Portraits) || !AuthorGuide() || !AuthorBattleAndCards(*Config)
		|| !Config->ValidateTutorial(Reason) || !Save(*Config) || !AuthorIslandScripts(Portraits) || !AuthorMap(*Config))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("教程、立绘或空岛剧本资产生成失败。"));
		return 1;
	}
	UE_LOG(LogMoving_Castle, Display, TEXT("完成教程地图、UI、剧本和 12 张正式立绘。"));
	return 0;
#else
	return 1;
#endif
}
