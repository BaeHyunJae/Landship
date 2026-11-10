#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSCastle.h"
#include "Troops/LSUnit.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSCombatStatTestFixture.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/BoxComponent.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSSelectableComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "Troops/LSTroopUnit.h"
#include <limits>

namespace
{
void AdvanceWeapons(UWorld* World, float Seconds)
{
	while (Seconds > KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Seconds, 0.01f);
		++GFrameCounter;
		World->Tick(LEVELTICK_All, Step);
		Seconds -= Step;
	}
}

ALSBasicAttackTestActor* Target(UWorld* World, const FVector& Location, ELSFaction Faction = ELSFaction::Enemy)
{
	ALSBasicAttackTestActor* Actor = World->SpawnActor<ALSBasicAttackTestActor>(Location, FRotator::ZeroRotator);
	Actor->SetInitialFaction(Faction);
	return Actor;
}

float Health(AActor* Actor)
{
	FLSHealthSnapshot Snapshot;
	const ULSHealthComponent* Component = Actor->FindComponentByClass<ULSHealthComponent>();
	return Component && Component->TryGetHealth(Snapshot) ? Snapshot.CurrentHealth : -1;
}

void StopUnitOrders(ALSUnit* Unit)
{
	Unit->Orders->SetComponentTickEnabled(false);
	Unit->BasicAttack->StopAttack();
}

ALSBasicProjectile* Projectile(UWorld* World)
{
	for (TActorIterator<ALSBasicProjectile> It(World); It; ++It)
		return *It;
	return nullptr;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCannonAimTest, "Landship.Castle.Weapons.CannonAndAim",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCannonAimTest::RunTest(const FString& Parameters)
{
	auto* CombatSettings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bFriendlyFire = CombatSettings->bAllowFriendlyFire;
	const bool bSelfDamage = CombatSettings->bAllowSelfDamage;
	ON_SCOPE_EXIT
	{
		CombatSettings->bAllowFriendlyFire = bFriendlyFire;
		CombatSettings->bAllowSelfDamage = bSelfDamage;
	};
	CombatSettings->bAllowFriendlyFire = false;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSPlayerController* Controller = World->SpawnActor<ALSPlayerController>();
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 144), FRotator::ZeroRotator);
	ALSCastle* EnemyCastle = LSCastleTestFixture::Spawn(World, FVector(800, 200, 144), FRotator::ZeroRotator);
	EnemyCastle->SetInitialFaction(ELSFaction::Enemy);
	ALSBasicAttackTestActor* Enemy = Target(World, FVector(800, 0, 144));
	ALSBasicAttackTestActor* Ally = Target(World, FVector(850, -100, 144), ELSFaction::Ally);
	ALSBasicAttackTestActor* Outside = Target(World, FVector(1100, 0, 144));
	if (!TestWorld.BeginPlayInTestWorld())
		return false;
	TestFalse(TEXT("미선택 시 조준 불가"), Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon));
	Controller->ApplySelection({EnemyCastle});
	TestFalse(TEXT("적 성 조준 불가"), Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon));
	Controller->ApplySelection({Castle});
	TestTrue(TEXT("Q 조준 준비"), Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon));
	Controller->CancelInput();
	TestEqual(TEXT("취소 시 쿨타임 미소모"), Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon), 0.0f);
	Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon);
	TestTrue(TEXT("사거리 밖 거부"),
	         Controller->ConfirmCastleWeapon(FVector(2000, 0, 0)) == ELSWeaponCastResult::OutOfRange);
	TestTrue(TEXT("잘못된 클릭 뒤 조준 유지"), Controller->GetPendingInput() == ELSCommandInput::CastleWeapon);
	TestEqual(TEXT("사거리 밖 실패는 쿨타임 미소모"), Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon),
	          0.0f);
	TestTrue(TEXT("유효 지면 시전"), Controller->ConfirmCastleWeapon(FVector(800, 0, 0)) == ELSWeaponCastResult::Ready);
	TestTrue(TEXT("시전 뒤 조준 해제"), Controller->GetPendingInput() == ELSCommandInput::None);
	TestEqual(TEXT("발사 직후 피해 없음"), Health(Enemy), 100.0f);
	AdvanceWeapons(World, 0.5f);
	TestEqual(TEXT("원 안의 적 피해"), Health(Enemy), 40.0f);
	TestEqual(TEXT("원 안의 적 성 피해"), Health(EnemyCastle), 940.0f);
	TestEqual(TEXT("원 밖 적 제외"), Health(Outside), 100.0f);
	TestEqual(TEXT("아군 피해 없음"), Health(Ally), 100.0f);
	TestTrue(TEXT("대포 쿨타임 적용"),
	         FMath::IsNearlyEqual(Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Cannon), 5.1f, 0.02f));
	TestFalse(TEXT("쿨타임 중 재조준 차단"), Controller->ArmCastleWeapon(ELSCastleWeapon::Cannon));
	TestTrue(TEXT("다른 무장 조준은 독립"), Controller->ArmCastleWeapon(ELSCastleWeapon::Shotgun));
	Controller->ApplySelection({EnemyCastle});
	TestTrue(TEXT("성 선택 상실 후 시전 불가"),
	         Controller->ConfirmCastleWeapon(FVector(800, 0, 0)) == ELSWeaponCastResult::Unavailable);
	TestEqual(TEXT("선택 상실은 산탄 쿨타임 미소모"), Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Shotgun),
	          0.0f);
	TestTrue(TEXT("NaN 조준 거부"),
	         Castle->Weapons->ValidateAim(ELSCastleWeapon::Ballista, FVector(std::numeric_limits<double>::quiet_NaN(),
	                                                                         0, 0)) == ELSWeaponCastResult::InvalidAim);
	Castle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
	    ULSHealthComponent::MakeDamageRequest(10000, EnemyCastle, EnemyCastle));
	TestTrue(TEXT("파괴된 성의 무장 차단"),
	         Castle->Weapons->GetStatus(ELSCastleWeapon::Shotgun) == ELSWeaponCastResult::Unavailable);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShotgunTest, "Landship.Castle.Weapons.ConeAndKnockback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShotgunTest::RunTest(const FString& Parameters)
{
	auto* CombatSettings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bFriendlyFire = CombatSettings->bAllowFriendlyFire;
	const bool bSelfDamage = CombatSettings->bAllowSelfDamage;
	ON_SCOPE_EXIT
	{
		CombatSettings->bAllowFriendlyFire = bFriendlyFire;
		CombatSettings->bAllowSelfDamage = bSelfDamage;
	};
	CombatSettings->bAllowFriendlyFire = false;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSCastle* Castle = LSCastleTestFixture::SpawnWithStats(World, LSCastleTestFixture::FixedStats(), FVector(0, 0, 144));
	ALSCastle* Structure = LSCastleTestFixture::SpawnWithStats(World, LSCastleTestFixture::FixedStats(), FVector(500, 200, 144));
	Structure->SetInitialFaction(ELSFaction::Enemy);
	TArray<ALSTroopUnit*> Units;
	const FVector Locations[] = {FVector(400, 0, 96), FVector(400, 400, 96), FVector(1250, 0, 96),
	                             FVector(400, -150, 96)};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		ALSTroopUnit* Unit = World->SpawnActor<ALSTroopUnit>(Locations[Index], FRotator::ZeroRotator);
		Unit->SetInitialFaction(Index == 3 ? ELSFaction::Ally : ELSFaction::Enemy);
		if (!TestTrue(TEXT("부채꼴 검사 고정 능력치 지정"),
			LSCombatStatTestFixture::ConfigureUnit(Unit, LSCombatStatTestFixture::UnitStats(120.0f)))) return false;
		Units.Add(Unit);
	}
	auto* Shotgun = NewObject<ULSCastleWeaponData>(World);
	Shotgun->Weapon = ELSCastleWeapon::Shotgun;
	FLSCastleWeaponSettings Settings;
	Settings.Damage = 35;
	Settings.Cooldown = 4.2f;
	Settings.Range = 1200;
	Settings.Radius = 250;
	Settings.ConeAngle = 70;
	Settings.KnockbackSpeed = 650;
	Settings.KnockbackLift = 180;
	Settings.ProjectileSpeed = 1800;
	Settings.ArcHeight = 350;
	Settings.ShellRadius = 18;
	Settings.PelletCount = 13;
	Settings.PelletRadius = 6;
	Shotgun->Levels = {Settings};
	if (!TestTrue(TEXT("부채꼴 검사 고정 산탄 수치 지정"), Castle->Weapons->SetWeaponDefinitions({Shotgun}))) return false;
	if (!TestWorld.BeginPlayInTestWorld())
		return false;
	Castle->Orders->SetComponentTickEnabled(false);
	Structure->Orders->SetComponentTickEnabled(false);
	for (ALSTroopUnit* Unit : Units)
		StopUnitOrders(Unit);
	TestTrue(TEXT("영벡터 방향 거부"),
	         Castle->Weapons->ValidateAim(ELSCastleWeapon::Shotgun, Castle->GetActorLocation()) ==
	             ELSWeaponCastResult::InvalidAim);
	TestTrue(TEXT("전방 부채꼴 시전"),
	         Castle->Weapons->CastWeapon(ELSCastleWeapon::Shotgun, FVector(1000, 0, 0)) == ELSWeaponCastResult::Ready);
	TestEqual(TEXT("산탄 발사 직후 피해 없음"), Health(Units[0]), 120.0f);
	AdvanceWeapons(World, 0.3f);
	TestTrue(TEXT("탄환이 맞은 병력 피해"), Health(Units[0]) < 120.0f && Health(Units[0]) >= 85.0f);
	TestEqual(TEXT("각도 밖 병력 제외"), Health(Units[1]), 120.0f);
	TestEqual(TEXT("거리 밖 병력 제외"), Health(Units[2]), 120.0f);
	TestEqual(TEXT("아군 피해 제외"), Health(Units[3]), 120.0f);
	TestTrue(TEXT("구조물은 명중한 탄환 피해만 적용"), Health(Structure) < 1000.0f && Health(Structure) >= 965.0f);
	TestTrue(TEXT("산탄 쿨타임 적용"),
	         FMath::IsNearlyEqual(Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Shotgun), 3.9f, 0.02f));
	AdvanceWeapons(World, 0.25f);
	TestTrue(TEXT("적 일반 병력이 실제로 밀려남"), Units[0]->GetActorLocation().X > 460);
	TestTrue(TEXT("적 성은 밀려나지 않음"), FVector::Dist2D(Structure->GetActorLocation(), FVector(500, 200, 144)) < 1);
	TestTrue(TEXT("아군은 밀려나지 않음"), FVector::Dist2D(Units[3]->GetActorLocation(), Locations[3]) < 1);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBallistaTest, "Landship.Castle.Weapons.BallistaFirstEnemy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBallistaTest::RunTest(const FString& Parameters)
{
	auto* CombatSettings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bFriendlyFire = CombatSettings->bAllowFriendlyFire;
	const bool bSelfDamage = CombatSettings->bAllowSelfDamage;
	ON_SCOPE_EXIT
	{
		CombatSettings->bAllowFriendlyFire = bFriendlyFire;
		CombatSettings->bAllowSelfDamage = bSelfDamage;
	};
	CombatSettings->bAllowFriendlyFire = false;
	for (const bool bWallPresent : {true, false})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game))
			return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 144), FRotator::ZeroRotator);
		ALSBasicAttackTestActor* Ally = Target(World, FVector(200, 0, 144), ELSFaction::Ally);
		ALSBasicAttackTestActor* Enemy = Target(World, FVector(500, 0, 144));
		ALSBasicAttackTestActor* Behind = Target(World, FVector(800, 0, 144));
		Behind->FindComponentByClass<UPrimitiveComponent>()->SetCollisionObjectType(LSCollisionChannels::Building);
		if (bWallPresent)
		{
			AActor* Wall = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
			Wall->SetRootComponent(Box);
			Wall->AddInstanceComponent(Box);
			Box->SetBoxExtent(FVector(20, 100, 200));
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->RegisterComponent();
			Wall->SetActorLocation(FVector(300, 0, 144));
		}
		if (!TestWorld.BeginPlayInTestWorld())
			return false;
		TestTrue(TEXT("발리스타 직선 발사"),
		         Castle->Weapons->CastWeapon(ELSCastleWeapon::Ballista, FVector(1800, 0, 0)) == ELSWeaponCastResult::Ready);
		TestEqual(TEXT("발사 즉시 피해 없음"), Health(Enemy), 100.0f);
		TestTrue(TEXT("발리스타 쿨타임 적용"),
		         FMath::IsNearlyEqual(Castle->Weapons->GetRemainingCooldown(ELSCastleWeapon::Ballista), 7.5f));
		Castle->Destroy();
		AdvanceWeapons(World, 0.1f);
		TestEqual(TEXT("비행 중 피해 없음"), Health(Enemy), 100.0f);
		AdvanceWeapons(World, 0.3f);
		TestEqual(bWallPresent ? TEXT("벽이 먼저 막으면 적 피해 없음") : TEXT("벽이 없으면 아군을 통과해 첫 적에게 큰 피해"),
		          Health(Enemy), bWallPresent ? 100.0f : 0.0f);
		TestEqual(TEXT("뒤의 적 성·거점은 관통하지 않음"), Health(Behind), 100.0f);
		TestEqual(TEXT("아군 피해 없음"), Health(Ally), 100.0f);
		TestNull(bWallPresent ? TEXT("벽에서 투사체 제거") : TEXT("첫 적 명중 뒤 투사체 제거"), Projectile(World));
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBallistaRangeTest, "Landship.Castle.Weapons.BallistaRange",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBallistaRangeTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 144), FRotator::ZeroRotator);
	ALSBasicAttackTestActor* Far = Target(World, FVector(2600, 0, 144));
	if (!TestWorld.BeginPlayInTestWorld())
		return false;
	Castle->Weapons->CastWeapon(ELSCastleWeapon::Ballista, FVector(3000, 0, 0));
	Castle->GetMovementComponent()->SetComponentTickEnabled(false);
	AdvanceWeapons(World, 1.5f);
	TestEqual(TEXT("사거리 밖 적은 피해 없음"), Health(Far), 100.0f);
	TestNull(TEXT("최대 비행 거리 후 제거"), Projectile(World));
	TestTrue(TEXT("다른 무장은 쿨타임 독립"),
	         Castle->Weapons->GetStatus(ELSCastleWeapon::Cannon) == ELSWeaponCastResult::Ready);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
