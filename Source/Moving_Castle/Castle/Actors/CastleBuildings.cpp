// Copyright Epic Games, Inc. All Rights Reserved.

#include "CastleBuildings.h"
#include "Components/Building/Effects/Attack/CastleTowerAttackEffectComponent.h"
#include "Components/Building/Effects/Farm/CastleFarmEffectComponent.h"
#include "Components/Building/Effects/Attack/CastleCannonTowerAttackEffectComponent.h"
#include "Components/Building/Effects/Shield/CastleShieldTowerEffectComponent.h"
#include "Components/Building/BuildingGameplayRootComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ACastleNormalRoomBuilding::ACastleNormalRoomBuilding()
{
	// 普通房间不参与合成，颜色和数字来自正式卡牌描述。
	BuildingType = ECastleBuildingType::NormalRoom;
	BuildingLevel = 1;
}
ACastleArcherTowerBuilding::ACastleArcherTowerBuilding(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCastleTowerAttackEffectComponent>(TEXT("BuildingEffect")))
{
	// 箭塔等级子类只设置等级；共同建筑效果子对象在构造时已替换为通用塔攻击组件。
	BuildingType = ECastleBuildingType::ArcherTower;
}


ACastleArcherTowerLevel1::ACastleArcherTowerLevel1()
{
	BuildingLevel = 1;
}

ACastleArcherTowerLevel2::ACastleArcherTowerLevel2()
{
	BuildingLevel = 2;
}

ACastleArcherTowerLevel3::ACastleArcherTowerLevel3()
{
	BuildingLevel = 3;
}

ACastleArcherTowerLevel4::ACastleArcherTowerLevel4()
{
	BuildingLevel = 4;
}

ACastleArcherTowerLevel5::ACastleArcherTowerLevel5()
{
	BuildingLevel = 5;
}

ACastleCannonTowerBuilding::ACastleCannonTowerBuilding(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCastleCannonTowerAttackEffectComponent>(TEXT("BuildingEffect")))
{
	// 炮塔族的具体等级由等级子类构造函数设置，此处只设置类别。
	BuildingType = ECastleBuildingType::CannonTower;
}


ACastleCannonTowerLevel1::ACastleCannonTowerLevel1()
{
	BuildingLevel = 1;
}

ACastleCannonTowerLevel2::ACastleCannonTowerLevel2()
{
	BuildingLevel = 2;
}

ACastleCannonTowerLevel3::ACastleCannonTowerLevel3()
{
	BuildingLevel = 3;
}

ACastleCannonTowerLevel4::ACastleCannonTowerLevel4()
{
	BuildingLevel = 4;
}

ACastleCannonTowerLevel5::ACastleCannonTowerLevel5()
{
	BuildingLevel = 5;
}

ACastleFarmBuilding::ACastleFarmBuilding(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCastleFarmEffectComponent>(TEXT("BuildingEffect")))
{
	// 农田族的具体等级由等级子类构造函数设置，此处只设置类别。
	BuildingType = ECastleBuildingType::Farm;
}


ACastleFarmLevel1::ACastleFarmLevel1()
{
	BuildingLevel = 1;
}

ACastleFarmLevel2::ACastleFarmLevel2()
{
	BuildingLevel = 2;
}

ACastleFarmLevel3::ACastleFarmLevel3()
{
	BuildingLevel = 3;
}

ACastleFarmLevel4::ACastleFarmLevel4()
{
	BuildingLevel = 4;
}

ACastleFarmLevel5::ACastleFarmLevel5()
{
	BuildingLevel = 5;
}

ACastleShieldTowerBuilding::ACastleShieldTowerBuilding(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCastleShieldTowerEffectComponent>(TEXT("BuildingEffect")))
{
	// 子对象由类显式定义，效果执行时不临时补建缺失组件，不把球体当成建筑美术模型。
	BuildingType = ECastleBuildingType::ShieldTower;
	ShieldBubble = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShieldBubble"));
	ShieldBubble->SetupAttachment(GameplayRoot);
	ShieldBubble->ComponentTags.Add(TEXT("MergeExclude"));
	ShieldBubble->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShieldBubble->SetCanEverAffectNavigation(false);
	ShieldBubble->SetCastShadow(false);
	ShieldBubble->SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShieldMaterial(TEXT("/Game/MovingCastle/Data/Buildings/M_CastleShield.M_CastleShield"));
	ShieldBubble->SetStaticMesh(Sphere.Object);
	ShieldBubble->SetMaterial(0, ShieldMaterial.Object);
	ShieldRemainingText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ShieldRemainingText"));
	ShieldRemainingText->SetupAttachment(GameplayRoot);
	ShieldRemainingText->ComponentTags.Add(TEXT("MergeExclude"));
	ShieldRemainingText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShieldRemainingText->SetCanEverAffectNavigation(false);
	ShieldRemainingText->SetHorizontalAlignment(EHTA_Center);
	ShieldRemainingText->SetVerticalAlignment(EVRTA_TextCenter);
	ShieldRemainingText->SetTextRenderColor(FColor::White);
	ShieldRemainingText->SetWorldSize(180.0f);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CounterMaterial(TEXT("/Game/MovingCastle/Data/Buildings/M_CastleShieldCounter.M_CastleShieldCounter"));
	ShieldRemainingText->SetTextMaterial(CounterMaterial.Object);
	// 次数属于状态反馈，使用显式的双面透明文字材质，不让相邻大体积美术遮住数字。
	ShieldRemainingText->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f));
	ShieldRemainingText->SetVisibility(false);
}

ACastleShieldTowerLevel1::ACastleShieldTowerLevel1()
{
	// 一级卡可直接购买和放置，单格占用沿用建筑基类规则。
	BuildingLevel = 1;
}

ACastleShieldTowerLevel2::ACastleShieldTowerLevel2()
{
	// 二级只改变等级身份，两个实际占用格由棋盘在合成时传入。
	BuildingLevel = 2;
}

ACastleShieldTowerLevel3::ACastleShieldTowerLevel3()
{
	// 三级继续使用统一四格合成和回收描述，不复制占用算法。
	BuildingLevel = 3;
}

ACastleShieldTowerLevel4::ACastleShieldTowerLevel4()
{
	// 四级是现有八格自动合成结果，不在盾塔族内扩大升级上限。
	BuildingLevel = 4;
}

ACastleShieldTowerLevel5::ACastleShieldTowerLevel5()
{
	// 五级只固定身份；相连攻击塔翻倍由唯一原生护盾组件结算。
	BuildingLevel = 5;
}

