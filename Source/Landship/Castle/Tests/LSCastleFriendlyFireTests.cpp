#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSCastleProjectile.h"
#include "Castle/LSCastle.h"
#include "Troops/LSUnit.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSCombatStatTestFixture.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Obstacles/LSBarricade.h"
#include "Tests/AutomationCommon.h"
#include "Troops/LSTroopUnit.h"

namespace
{
struct FLSCastleFriendlyFireScope
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	bool bFriendlyFire = Settings->bAllowFriendlyFire;
	bool bSelfDamage = Settings->bAllowSelfDamage;
	~FLSCastleFriendlyFireScope()
	{
		Settings->bAllowFriendlyFire = bFriendlyFire;
		Settings->bAllowSelfDamage = bSelfDamage;
	}
};

ELSFaction OpposingCastleTestFaction(ELSFaction Faction)
{
	return Faction == ELSFaction::Ally ? ELSFaction::Enemy : ELSFaction::Ally;
}

ALSBasicAttackTestActor* MakeCastlePolicyTarget(UWorld* World, const FVector& Location, ELSFaction Faction)
{
	auto* Actor = World->SpawnActor<ALSBasicAttackTestActor>(Location, FRotator::ZeroRotator);
	Actor->SetInitialFaction(Faction);
	Actor->Attack->SetAutoAttackOnBeginPlay(false);
	Actor->HealthComponent->bDestroyOnDeath = false;
	return Actor;
}

AActor* LaunchCastlePolicyShot(FAutomationTestBase& Test, UWorld* World, ELSCastleWeapon Weapon,
                              const FLSDamageRequest& Request, const FVector& Start, const FVector& Aim,
                              const FLSCastleWeaponSettings& Settings, bool bFirstBlocking = false)
{
	const FTransform Transform(FRotator::ZeroRotator, Start);
	if (Weapon == ELSCastleWeapon::Ballista)
	{
		auto* Shot = World->SpawnActorDeferred<ALSBasicProjectile>(ALSBasicProjectile::StaticClass(), Transform,
		    nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		const bool bLaunched = bFirstBlocking
		    ? Shot->LaunchFirstBlocking(Request, Aim - Start, Settings.ProjectileSpeed, Settings.PelletRadius,
		                               Settings.Range / Settings.ProjectileSpeed)
		    : Shot->LaunchInDirection(Request, Aim - Start, Settings.ProjectileSpeed, Settings.PelletRadius,
		                            Settings.Range / Settings.ProjectileSpeed);
		if (!Test.TestTrue(TEXT("발리스타 발사 준비"), bLaunched))
		{
			Shot->Destroy();
			return nullptr;
		}
		Shot->FinishSpawning(Transform);
		return Shot;
	}
	auto* Shot = World->SpawnActorDeferred<ALSCastleProjectile>(ALSCastleProjectile::StaticClass(), Transform,
	    nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Test.TestTrue(TEXT("성 무장 발사 준비"), Shot->Launch(Weapon, Settings, Request, Aim, bFirstBlocking)))
	{
		Shot->Destroy();
		return nullptr;
	}
	Shot->FinishSpawning(Transform);
	return Shot;
}

FLSCastleWeaponSettings CastlePolicyShotSettings()
{
	FLSCastleWeaponSettings Settings;
	Settings.Damage = 20;
	Settings.PelletCount = 1;
	Settings.Range = 700;
	Settings.ProjectileSpeed = 1000;
	Settings.PelletRadius = 2;
	return Settings;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCannonFriendlyFireMatrixTest,
    "Landship.Castle.FriendlyFire.CannonSelfAndFactionMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCannonFriendlyFireMatrixTest::RunTest(const FString& Parameters)
{
	FLSCastleFriendlyFireScope Restore;
	for (ELSFaction Faction : {ELSFaction::Ally, ELSFaction::Enemy})
	for (bool bFriendlyFire : {false, true})
	for (bool bSelfDamage : {false, true})
	{
		Restore.Settings->bAllowFriendlyFire = bFriendlyFire;
		Restore.Settings->bAllowSelfDamage = bSelfDamage;
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = MakeCastlePolicyTarget(World, FVector(400, -40, 100), Faction);
		auto* Ally = MakeCastlePolicyTarget(World, FVector(400, 40, 100), Faction);
		auto* Enemy = MakeCastlePolicyTarget(World, FVector(400, 0, 100), OpposingCastleTestFaction(Faction));
		auto* Outside = MakeCastlePolicyTarget(World, FVector(600, 0, 100), Faction);
		if (!Fixture.BeginPlayInTestWorld()) return false;
		FLSCastleWeaponSettings Settings = CastlePolicyShotSettings();
		Settings.Radius = 100;
		AActor* Shot = LaunchCastlePolicyShot(*this, World, ELSCastleWeapon::Cannon,
		    ULSHealthComponent::MakeDamageRequest(20, Source, Source), FVector(0, 0, 100), FVector(400, 0, 100), Settings);
		if (!Shot) return false;
		Shot->Tick(1);
		const FString Case = FString::Printf(TEXT("Faction=%d FF=%d Self=%d "), static_cast<int32>(Faction),
		    bFriendlyFire, bSelfDamage);
		TestEqual(*(Case + TEXT("발사자 자기 피해는 별도 설정을 따른다")), Source->GetCurrentHealth(), bSelfDamage ? 80.f : 100.f);
		TestEqual(*(Case + TEXT("같은 진영의 범위 피해")), Ally->GetCurrentHealth(), bFriendlyFire ? 80.f : 100.f);
		TestEqual(*(Case + TEXT("상대 진영의 범위 피해")), Enemy->GetCurrentHealth(), 80.f);
		TestEqual(*(Case + TEXT("반경 밖 대상 제외")), Outside->GetCurrentHealth(), 100.f);
		TestTrue(*(Case + TEXT("착탄 후 제거")), Shot->IsActorBeingDestroyed());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleStraightFriendlyFireMatrixTest,
    "Landship.Castle.FriendlyFire.StraightFirstLivingBodyMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCastleStraightFriendlyFireMatrixTest::RunTest(const FString& Parameters)
{
	FLSCastleFriendlyFireScope Restore;
	Restore.Settings->bAllowSelfDamage = true;
	for (ELSCastleWeapon Weapon : {ELSCastleWeapon::Shotgun, ELSCastleWeapon::Ballista})
	for (ELSFaction Faction : {ELSFaction::Ally, ELSFaction::Enemy})
	for (bool bFriendlyFire : {false, true})
	for (bool bBuilding : {false, true})
	{
		Restore.Settings->bAllowFriendlyFire = bFriendlyFire;
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = MakeCastlePolicyTarget(World, FVector(0, 0, 100), Faction);
		auto* Dead = MakeCastlePolicyTarget(World, FVector(75, 0, 100), OpposingCastleTestFaction(Faction));
		auto* Ally = MakeCastlePolicyTarget(World, FVector(150, 0, 100), Faction);
		auto* Enemy = MakeCastlePolicyTarget(World, FVector(300, 0, 100), OpposingCastleTestFaction(Faction));
		auto* Behind = MakeCastlePolicyTarget(World, FVector(450, 0, 100), OpposingCastleTestFaction(Faction));
		CastChecked<UPrimitiveComponent>(Ally->GetRootComponent())->SetCollisionObjectType(
		    bBuilding ? LSCollisionChannels::Building : LSCollisionChannels::Unit);
		CastChecked<UPrimitiveComponent>(Enemy->GetRootComponent())->SetCollisionObjectType(
		    bBuilding ? LSCollisionChannels::Unit : LSCollisionChannels::Building);
		if (!Fixture.BeginPlayInTestWorld()) return false;
		TestEqual(TEXT("시체 준비"), Dead->ApplyTestDamage(100, Source).AppliedDamage, 100.f);
		const int32 DeadHits = Dead->HitsReceived;
		AActor* Shot = LaunchCastlePolicyShot(*this, World, Weapon,
		    ULSHealthComponent::MakeDamageRequest(20, Source, Source), Source->GetActorLocation(), FVector(600, 0, 100),
		    CastlePolicyShotSettings());
		if (!Shot) return false;
		Shot->Tick(.6f);
		const FString Case = FString::Printf(TEXT("Weapon=%d Faction=%d FF=%d Building=%d "),
		    static_cast<int32>(Weapon), static_cast<int32>(Faction), bFriendlyFire, bBuilding);
		TestEqual(*(Case + TEXT("원래 발사자는 통과")), Source->GetCurrentHealth(), 100.f);
		TestEqual(*(Case + TEXT("시체는 통과")), Dead->HitsReceived, DeadHits);
		TestEqual(*(Case + TEXT("앞의 아군은 오폭 허용 시 먼저 맞는다")), Ally->GetCurrentHealth(), bFriendlyFire ? 80.f : 100.f);
		TestEqual(*(Case + TEXT("오폭 금지 시 앞의 적이 가로막는다")), Enemy->GetCurrentHealth(), bFriendlyFire ? 100.f : 80.f);
		TestEqual(*(Case + TEXT("첫 명중 뒤 관통 없음")), Behind->GetCurrentHealth(), 100.f);
		TestTrue(*(Case + TEXT("첫 명중 뒤 제거")), Shot->IsActorBeingDestroyed());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleFriendlyFireLiveSettingsTest,
    "Landship.Castle.FriendlyFire.LiveSettingsAfterSourceRemoval",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCastleFriendlyFireLiveSettingsTest::RunTest(const FString& Parameters)
{
	FLSCastleFriendlyFireScope Restore;
	Restore.Settings->bAllowSelfDamage = true;
	for (ELSCastleWeapon Weapon : {ELSCastleWeapon::Cannon, ELSCastleWeapon::Shotgun, ELSCastleWeapon::Ballista})
	for (ELSFaction Faction : {ELSFaction::Ally, ELSFaction::Enemy})
	for (bool bImpactFriendlyFire : {false, true})
	{
		Restore.Settings->bAllowFriendlyFire = !bImpactFriendlyFire;
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = MakeCastlePolicyTarget(World, FVector(0, 0, 100), Faction);
		auto* Ally = MakeCastlePolicyTarget(World, FVector(150, 0, 100), Faction);
		auto* Enemy = MakeCastlePolicyTarget(World, FVector(300, 0, 100), OpposingCastleTestFaction(Faction));
		if (!Fixture.BeginPlayInTestWorld()) return false;
		FLSCastleWeaponSettings Settings = CastlePolicyShotSettings();
		Settings.Radius = 150;
		const FVector Aim = Weapon == ELSCastleWeapon::Cannon ? FVector(225, 0, 100) : FVector(600, 0, 100);
		AActor* Shot = LaunchCastlePolicyShot(*this, World, Weapon,
		    ULSHealthComponent::MakeDamageRequest(20, Source, Source), Source->GetActorLocation(), Aim, Settings);
		if (!Shot) return false;
		Shot->Tick(.05f);
		TestEqual(TEXT("설정 변경 전에는 아직 명중하지 않는다"), Ally->GetCurrentHealth(), 100.f);
		Restore.Settings->bAllowFriendlyFire = bImpactFriendlyFire;
		Source->Destroy();
		Shot->Tick(.6f);
		const FString Case = FString::Printf(TEXT("Weapon=%d Faction=%d ImpactFF=%d "), static_cast<int32>(Weapon),
		    static_cast<int32>(Faction), bImpactFriendlyFire);
		TestEqual(*(Case + TEXT("제거된 발사자의 진영과 현재 오폭 설정 사용")), Ally->GetCurrentHealth(), bImpactFriendlyFire ? 80.f : 100.f);
		TestEqual(*(Case + TEXT("상대 진영 피해와 직선 탄의 첫 명중 유지")), Enemy->GetCurrentHealth(),
		    Weapon == ELSCastleWeapon::Cannon || !bImpactFriendlyFire ? 80.f : 100.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleObstacleFriendlyFireMatrixTest,
    "Landship.Castle.FriendlyFire.ObstacleFirstBlockingBodyMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCastleObstacleFriendlyFireMatrixTest::RunTest(const FString& Parameters)
{
	FLSCastleFriendlyFireScope Restore;
	Restore.Settings->bAllowSelfDamage = true;
	for (ELSCastleWeapon Weapon : {ELSCastleWeapon::Shotgun, ELSCastleWeapon::Ballista})
	for (ELSFaction Faction : {ELSFaction::Ally, ELSFaction::Enemy})
	for (bool bImpactFriendlyFire : {false, true})
	{
		Restore.Settings->bAllowFriendlyFire = !bImpactFriendlyFire;
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = MakeCastlePolicyTarget(World, FVector(0, 0, 100), Faction);
		auto* Ally = MakeCastlePolicyTarget(World, FVector(150, 0, 100), Faction);
		auto* Enemy = MakeCastlePolicyTarget(World, FVector(300, 0, 100), OpposingCastleTestFaction(Faction));
		auto* Obstacle = World->SpawnActor<ALSBarricade>(FVector(500, 0, 100), FRotator::ZeroRotator);
		for (auto* Actor : {Source, Ally, Enemy})
			CastChecked<UPrimitiveComponent>(Actor->GetRootComponent())->SetCollisionResponseToChannel(
			    LSCollisionChannels::ObstacleProjectile, ECR_Block);
		if (!Fixture.BeginPlayInTestWorld()) return false;
		FLSHealthSnapshot Before;
		if (!TestTrue(TEXT("장애물 체력 준비"), Obstacle->GetHealthComponent()->TryGetHealth(Before))) return false;
		AActor* Shot = LaunchCastlePolicyShot(*this, World, Weapon,
		    ULSHealthComponent::MakeDamageRequest(20, Source, Source), Source->GetActorLocation(), Obstacle->GetActorLocation(),
		    CastlePolicyShotSettings(), true);
		if (!Shot) return false;
		Shot->Tick(.05f);
		Restore.Settings->bAllowFriendlyFire = bImpactFriendlyFire;
		Shot->Tick(.6f);
		const FString Case = FString::Printf(TEXT("Weapon=%d Faction=%d ImpactFF=%d "), static_cast<int32>(Weapon),
		    static_cast<int32>(Faction), bImpactFriendlyFire);
		TestEqual(*(Case + TEXT("발사자 몸체는 통과")), Source->GetCurrentHealth(), 100.f);
		TestEqual(*(Case + TEXT("현재 오폭 설정에 따라 첫 아군 몸체에 피해")), Ally->GetCurrentHealth(), bImpactFriendlyFire ? 80.f : 100.f);
		TestEqual(*(Case + TEXT("오폭 금지여도 아군 몸체 뒤의 적은 보호")), Enemy->GetCurrentHealth(), 100.f);
		FLSHealthSnapshot After;
		if (!TestTrue(TEXT("발사 뒤 장애물 체력 조회"), Obstacle->GetHealthComponent()->TryGetHealth(After))) return false;
		TestEqual(*(Case + TEXT("지정 장애물까지 관통하지 않는다")), After.CurrentHealth, Before.CurrentHealth);
		TestTrue(*(Case + TEXT("첫 실제 몸체에서 종료")), Shot->IsActorBeingDestroyed());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleAlliedShotgunKnockbackTest,
    "Landship.Castle.FriendlyFire.AlliedShotgunKnockbackOncePerVolley",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCastleAlliedShotgunKnockbackTest::RunTest(const FString& Parameters)
{
	FLSCastleFriendlyFireScope Restore;
	Restore.Settings->bAllowSelfDamage = true;
	for (ELSFaction Faction : {ELSFaction::Ally, ELSFaction::Enemy})
	for (bool bFriendlyFire : {false, true})
	for (bool bFirstBlocking : {false, true})
	{
		Restore.Settings->bAllowFriendlyFire = bFriendlyFire;
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = MakeCastlePolicyTarget(World, FVector(0, 0, 100), Faction);
		auto* Ally = World->SpawnActor<ALSTroopUnit>(FVector(400, 30, 100), FRotator::ZeroRotator);
		Ally->SetInitialFaction(Faction);
		if (!TestTrue(TEXT("아군 산탄 검사 고정 능력치 지정"),
			LSCombatStatTestFixture::ConfigureUnit(Ally, LSCombatStatTestFixture::UnitStats(120.0f)))) return false;
		if (!Fixture.BeginPlayInTestWorld()) return false;
		Ally->Orders->SetComponentTickEnabled(false);
		Ally->BasicAttack->StopAttack();
		Ally->GetCharacterMovement()->SetComponentTickEnabled(false);
		Ally->GetCapsuleComponent()->SetCapsuleSize(42, 88);
		FLSCastleWeaponSettings Settings = CastlePolicyShotSettings();
		Settings.PelletCount = 3;
		Settings.ConeAngle = 20;
		Settings.Damage = 30;
		AActor* Shot = LaunchCastlePolicyShot(*this, World, ELSCastleWeapon::Shotgun,
		    ULSHealthComponent::MakeDamageRequest(30, Source, Source), Source->GetActorLocation(), FVector(600, 0, 100),
		    Settings, bFirstBlocking);
		if (!Shot) return false;
		const FString Case = FString::Printf(TEXT("Faction=%d FF=%d Blocking=%d "), static_cast<int32>(Faction),
		    bFriendlyFire, bFirstBlocking);
		// 중앙 탄은 0.368초, 위쪽 탄은 0.381초 부근에 닿는다. 두 명중 사이에서 보류 속도를 비운다.
		Shot->Tick(.374f);
		FLSHealthSnapshot First;
		if (!TestTrue(TEXT("첫 명중 뒤 병력 체력 조회"), Ally->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(First))) return false;
		TestEqual(*(Case + TEXT("첫 탄의 실제 피해")), First.CurrentHealth, bFriendlyFire ? 110.f : 120.f);
		TestEqual(*(Case + TEXT("실제 명령 컴포넌트가 넉백을 받는다")), Ally->Orders->IsKnockedBack(), bFriendlyFire);
		TestEqual(*(Case + TEXT("피해가 허용될 때 보류된 넉백 속도 발생")),
		    !Ally->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero(), bFriendlyFire);
		Ally->GetCharacterMovement()->PendingLaunchVelocity = FVector::ZeroVector;
		Shot->Tick(.25f);
		FLSHealthSnapshot Second;
		if (!TestTrue(TEXT("두 번째 명중 뒤 병력 체력 조회"), Ally->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Second))) return false;
		TestEqual(*(Case + TEXT("같은 병력에 두 번째 탄 피해 적용")), Second.CurrentHealth, bFriendlyFire ? 100.f : 120.f);
		TestTrue(*(Case + TEXT("같은 발사의 후속 명중은 다시 밀지 않는다")), Ally->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleAlliedShotgunKnockbackEligibilityTest,
    "Landship.Castle.FriendlyFire.AlliedShotgunKnockbackRequiresSurvivingDamagedTroop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCastleAlliedShotgunKnockbackEligibilityTest::RunTest(const FString& Parameters)
{
	FLSCastleFriendlyFireScope Restore;
	Restore.Settings->bAllowFriendlyFire = true;
	Restore.Settings->bAllowSelfDamage = true;
	for (ELSFaction Faction : {ELSFaction::Ally, ELSFaction::Enemy})
	for (int32 Case = 0; Case < 3; ++Case)
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = MakeCastlePolicyTarget(World, FVector(0, 0, 100), Faction);
		AActor* Ally;
		if (Case == 2)
		{
			auto* Castle = LSCastleTestFixture::SpawnWithStats(World, LSCastleTestFixture::FixedStats(), FVector(400, 0, 144));
			Castle->SetInitialFaction(Faction);
			Ally = Castle;
		}
		else
		{
			auto* Troop = World->SpawnActor<ALSTroopUnit>(FVector(400, 0, 100), FRotator::ZeroRotator);
			Troop->SetInitialFaction(Faction);
			if (!TestTrue(TEXT("산탄 생존 조건 검사 고정 능력치 지정"),
				LSCombatStatTestFixture::ConfigureUnit(Troop, LSCombatStatTestFixture::UnitStats(120.0f)))) return false;
			Ally = Troop;
		}
		auto* Health = Ally->FindComponentByClass<ULSHealthComponent>();
		Health->bDestroyOnDeath = false;
		if (!Fixture.BeginPlayInTestWorld()) return false;
		auto* Orders = Ally->FindComponentByClass<ULSOrderComponent>();
		Orders->SetComponentTickEnabled(false);
		if (auto* Troop = Cast<ALSUnit>(Ally))
		{
			Troop->BasicAttack->StopAttack();
			Troop->GetCharacterMovement()->SetComponentTickEnabled(false);
		}
		FLSCastleWeaponSettings Settings = CastlePolicyShotSettings();
		Settings.Damage = Case == 0 ? 0.f : Case == 1 ? 1000.f : 20.f;
		AActor* Shot = LaunchCastlePolicyShot(*this, World, ELSCastleWeapon::Shotgun,
		    ULSHealthComponent::MakeDamageRequest(Settings.Damage, Source, Source), Source->GetActorLocation(),
		    FVector(600, 0, 100), Settings);
		if (!Shot) return false;
		Shot->Tick(.6f);
		FLSHealthSnapshot After;
		if (!TestTrue(TEXT("명중 뒤 체력 조회"), Health->TryGetHealth(After))) return false;
		const FString Label = FString::Printf(TEXT("Faction=%d Case=%d "), static_cast<int32>(Faction), Case);
		TestEqual(*(Label + TEXT("0 피해·치명상·성 피해 구분")), After.CurrentHealth, Case == 0 ? 120.f : Case == 1 ? 0.f : 980.f);
		TestFalse(*(Label + TEXT("살아남은 피해 병력만 넉백 대상")), Orders->IsKnockedBack());
		if (auto* Troop = Cast<ALSUnit>(Ally))
			TestTrue(*(Label + TEXT("넉백 속도 없음")), Troop->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero());
		else
			TestTrue(*(Label + TEXT("성 속도 없음")), Ally->GetVelocity().IsNearlyZero());
	}
	return true;
}

#endif
