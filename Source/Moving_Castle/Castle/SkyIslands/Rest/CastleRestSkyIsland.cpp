#include "CastleRestSkyIsland.h"

#include "Castle/Components/CastleEconomyComponent.h"
#include "Castle/Components/CastleMainTowerComponent.h"
#include "CastlePlayerController.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Moving_Castle.h"
#include "UObject/ConstructorHelpers.h"

bool UCastleRestSkyIslandDataAsset::Validate(FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	if (!FMath::IsFinite(HealFraction) || HealFraction <= 0.0f || HealFraction > 1.0f || GoldReward < 0)
	{
		OutReason = INVTEXT("休息空岛的回血比例必须在 (0, 1]，金币奖励必须非负。");
		UE_LOG(LogMoving_Castle, Error, TEXT("休息空岛配置 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	return true;
}

FText UCastleRestSkyIslandStation::GetInteractPrompt() const
{
	return bClaimed ? FText::Format(INVTEXT("{0}（已领取）"), Prompt) : Prompt;
}

bool UCastleRestSkyIslandStation::CanInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	const auto* Island = Cast<ACastleRestSkyIsland>(GetOwner());
	if (!Island || Context.Island != Island || !Island->IsIslandController(Context.Controller)
		|| Context.Hit.GetComponent() != this || !Tint || !Sign)
	{
		OutReason = INVTEXT("请用当前玩家的准星对准本岛已初始化的补给台。");
		return false;
	}
	if (bClaimed || bClaiming)
	{
		OutReason = bClaimed ? GetInteractPrompt() : INVTEXT("正在领取补给。");
		UE_LOG(LogMoving_Castle, Display, TEXT("%s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	return true;
}

bool UCastleRestSkyIslandStation::ExecuteInteract(const FCastleSkyIslandInteractContext& Context, FText& OutReason)
{
	if (!CanInteract(Context, OutReason)) { return false; }
	// 血量或金币广播会同步通知 UI。结算期间锁住本台，避免重入再次发放。
	TGuardValue<bool> ClaimGuard(bClaiming, true);
	FText Message;
	auto* Island = CastChecked<ACastleRestSkyIsland>(GetOwner());
	if (!Island->ClaimStation(*this, Message, OutReason))
	{
		UE_LOG(LogMoving_Castle, Error, TEXT("休息空岛 %s 的 %s 领取失败：%s"),
			*Island->GetPathName(), *GetName(), *OutReason.ToString());
		return false;
	}
	bClaimed = true;
	for (int32 Slot = 0; Slot < GetNumMaterials(); ++Slot) { SetMaterial(Slot, Tint); }
	Sign->SetText(FText::FromString(Label + TEXT(" | CLAIMED")));
	UE_LOG(LogMoving_Castle, Display, TEXT("休息空岛 %s：%s"), *Island->GetPathName(), *Message.ToString());
	if (GEngine) { GEngine->AddOnScreenDebugMessage(INDEX_NONE, 4.0f, FColor::Green, Message.ToString()); }
	return true;
}

ACastleRestSkyIsland::ACastleRestSkyIsland()
{
	// 绑定该玩法的岛体，位置沿用作者对齐的岛面中心。
	SetIslandArtMesh(TEXT("SM_SkyIsland_Rest"));
	ConfigAsset = TSoftObjectPtr<UCastleRestSkyIslandDataAsset>(FSoftObjectPath(
		TEXT("/Game/MovingCastle/Data/SkyIslands/DA_RestSkyIsland.DA_RestSkyIsland")));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	StationMaterial = Material.Object;
	// 三台横向展开，牌面朝向入口；入口至离开门的中心通道保持可飞行。
	const auto MakeStation = [this](FName Name, UStaticMesh* Mesh, const FVector& Location)
	{
		auto* Station = CreateDefaultSubobject<UCastleRestSkyIslandStation>(Name);
		Station->SetupAttachment(GetRootComponent());
		Station->SetStaticMesh(Mesh);
		Station->SetRelativeLocation(Location);
		// 网格轴心与命中中心重合（距地面 100 厘米），按作者尺寸直接显示。
		Station->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Station->SetCollisionResponseToAllChannels(ECR_Ignore);
		Station->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		return Station;
	};
	const auto MakeSign = [this](FName Name, const UCastleRestSkyIslandStation& Station)
	{
		auto* Sign = CreateDefaultSubobject<UTextRenderComponent>(Name);
		Sign->SetupAttachment(GetRootComponent());
		Sign->SetRelativeLocation(Station.GetRelativeLocation() + FVector(-94, 0, 160));
		Sign->SetRelativeRotation(FRotator(0, 180, 0));
		Sign->SetHorizontalAlignment(EHTA_Center);
		Sign->SetWorldSize(34.0f);
		return Sign;
	};
	HealStation = MakeStation(TEXT("HealStation"), LoadIslandArt(TEXT("SM_Rest_Heal")), FVector(-220, -530, 100));
	GoldStation = MakeStation(TEXT("GoldStation"), LoadIslandArt(TEXT("SM_Rest_Gold")), FVector(-220, 530, 100));
	ShopStation = MakeStation(TEXT("ShopStation"), LoadIslandArt(TEXT("SM_Rest_RoomShop")), FVector(250, -530, 100));
	HealSign = MakeSign(TEXT("HealSign"), *HealStation);
	GoldSign = MakeSign(TEXT("GoldSign"), *GoldStation);
	ShopSign = MakeSign(TEXT("ShopSign"), *ShopStation);
}

bool ACastleRestSkyIsland::OnInitializeIsland(const FCastleSkyIslandContext& Context, FText& OutReason)
{
	Config = ConfigAsset.LoadSynchronous();
	FLinearColor MaterialTint;
	if (Context.Type != ECastleSkyIslandType::Rest || !Config || !StationMaterial
		|| !StationMaterial->GetVectorParameterValue(FName(TEXT("Color")), MaterialTint)
		|| !HealStation->GetStaticMesh() || !GoldStation->GetStaticMesh() || !ShopStation->GetStaticMesh())
	{
		OutReason = INVTEXT("休息空岛需要正确类型、配置资产及带颜色参数的台座网格与材质。");
		UE_LOG(LogMoving_Castle, Error, TEXT("休息空岛 %s，配置 %s：%s"),
			*GetPathName(), *ConfigAsset.ToString(), *OutReason.ToString());
		return false;
	}
	if (!Config->Validate(OutReason)) { return false; }

	// 离线 TextRender 使用英文简标。交互和领取结果通过游戏 HUD 使用中文。
	const auto ConfigureStation = [this](UCastleRestSkyIslandStation& Station, UTextRenderComponent& Sign,
		const TCHAR* Label, const FText& Prompt)
	{
		Station.Sign = &Sign;
		Station.Label = Label;
		Station.Prompt = Prompt;
		Sign.SetText(FText::FromString(Label));
		Station.Tint = UMaterialInstanceDynamic::Create(StationMaterial, &Station);
		if (!Station.Tint) { return false; }
		// 领取之前展示原作多材质；领取后同一灰色材质覆盖本台全部槽位。
		Station.Tint->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.23f, 0.23f, 0.23f));
		return true;
	};
	if (!ConfigureStation(*HealStation, *HealSign, TEXT("HEAL"), INVTEXT("免费气球回血"))
		|| !ConfigureStation(*GoldStation, *GoldSign, TEXT("GOLD"), INVTEXT("免费金币"))
		|| !ConfigureStation(*ShopStation, *ShopSign, TEXT("ROOM SHOP"), INVTEXT("免费换批房间商店")))
	{
		OutReason = INVTEXT("休息空岛台座无法创建领取后的灰色材质。");
		UE_LOG(LogMoving_Castle, Error, TEXT("休息空岛 %s：%s"), *GetPathName(), *OutReason.ToString());
		return false;
	}
	return true;
}

bool ACastleRestSkyIsland::IsIslandController(const ACastlePlayerController* Controller) const
{
	return IsValid(Controller) && GetIslandContext().Controller.Get() == Controller;
}

bool ACastleRestSkyIsland::ClaimStation(const UCastleRestSkyIslandStation& Station, FText& OutMessage, FText& OutReason)
{
	if (&Station == HealStation)
	{
		const auto* Towers = GetWorld()->GetSubsystem<UCastleMainTowerSubsystem>();
		auto* Tower = Towers ? Towers->GetRegisteredMainTower() : nullptr;
		if (!IsValid(Tower))
		{
			OutReason = INVTEXT("当前世界没有已注册的主塔生命组件。");
			return false;
		}
		const float PreviousHealth = Tower->CurrentHealth;
		if (!Tower->ApplyHealing(Tower->MaxHealth * Config->HealFraction))
		{
			OutReason = INVTEXT("主塔拒绝本次回血。");
			return false;
		}
		OutMessage = PreviousHealth == Tower->MaxHealth ? INVTEXT("主塔已满血，免费回血台已领取。")
			: FText::Format(INVTEXT("主塔恢复 {0} 点生命，气球已同步。"), FText::AsNumber(Tower->CurrentHealth - PreviousHealth));
		return true;
	}
	if (&Station == GoldStation)
	{
		auto* Controller = GetIslandContext().Controller.Get();
		auto* Economy = Controller ? Controller->GetEconomy() : nullptr;
		if (!IsValid(Economy))
		{
			OutReason = INVTEXT("当前玩家没有有效的金币账户。");
			return false;
		}
		// 免费金币台发放玩法收入，提示使用零钱袋叠加后真正到账的金额。
		int32 FinalGold = 0;
		if (!Economy->TryAddGameplayGold(Config->GoldReward, OutReason, &FinalGold)) { return false; }
		OutMessage = FText::Format(INVTEXT("已领取 {0} 金币。"), FText::AsNumber(FinalGold));
		return true;
	}
	if (&Station != ShopStation || !GetIslandContext().Director.IsValid())
	{
		OutReason = INVTEXT("房间商店台或导演上下文无效。");
		return false;
	}
	if (!RequestOpenTemporaryRoomShop(OutReason)) { return false; }
	OutMessage = INVTEXT("房间商店已免费换批并打开。");
	return true;
}
