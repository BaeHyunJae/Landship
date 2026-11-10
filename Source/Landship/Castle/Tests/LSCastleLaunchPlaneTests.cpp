#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSCastle.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Castle/LSCastleProjectile.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSCombatStatTestFixture.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/CapsuleComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Obstacles/LSMine.h"
#include "Tests/AutomationCommon.h"
#include "Troops/LSTroopUnit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleLaunchPlaneTest,
	"Landship.Castle.Weapons.EnlargedCastleLaunchPlane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleLaunchPlaneTest::RunTest(const FString& Parameters)
{
	for (ELSCastleWeapon Weapon : {ELSCastleWeapon::Shotgun, ELSCastleWeapon::Ballista})
	{
		for (bool bGroundHeight : {false, true})
		{
			FTestWorldWrapper Fixture;
			if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
			UWorld* World = Fixture.GetTestWorld();
			auto* Castle = LSCastleTestFixture::SpawnWithStats(World, LSCastleTestFixture::FixedStats(), FVector(0, 0, 500));
			Castle->GetCapsuleComponent()->SetCapsuleSize(500, 500);
			auto* Target = World->SpawnActor<ALSTroopUnit>(FVector(900, 0, 88), FRotator::ZeroRotator);
			Target->SetInitialFaction(ELSFaction::Enemy);
			if (!TestTrue(TEXT("발사 평면 검사 고정 능력치 지정"),
				LSCombatStatTestFixture::ConfigureUnit(Target, LSCombatStatTestFixture::UnitStats()))) return false;
			Target->BasicAttack->SetAutoAttackOnBeginPlay(false);
			auto* Data = NewObject<ULSCastleWeaponData>(World);
			Data->Weapon = Weapon;
			FLSCastleWeaponSettings Settings;
			Settings.Damage = 60;
			Settings.Range = 1600;
			Settings.Radius = 5;
			Settings.PelletCount = 1;
			Settings.PelletRadius = 5;
			Data->Levels.Add(Settings);
			if (!TestTrue(TEXT("검사 전용 무장 수치 연결"), Castle->Weapons->SetWeaponDefinitions({Data}))) return false;
			TestTrue(TEXT("대포의 기존 높이 기본값 유지"),
				Castle->Weapons->GetWeaponLaunchLocation(ELSCastleWeapon::Cannon).Equals(FVector(0, 0, 650)));
			TestTrue(TEXT("직선 무장에 설정된 기존 기본 위치 유지"),
				Castle->Weapons->GetWeaponLaunchLocation(Weapon).Equals(FVector(0, 0, 500)));
			if (bGroundHeight)
			{
				Castle->Weapons->ShotgunLaunchOffset = FVector(0, 0, -412);
				Castle->Weapons->BallistaLaunchOffset = FVector(0, 0, -412);
			}
			if (!Fixture.BeginPlayInTestWorld()) return false;
			Castle->Orders->SetComponentTickEnabled(false);
			Castle->GetMovementComponent()->SetComponentTickEnabled(false);
			Target->Orders->SetComponentTickEnabled(false);
			Target->GetCharacterMovement()->SetComponentTickEnabled(false);
			FLSHealthSnapshot Before, After;
			auto* Health = Target->FindComponentByClass<ULSHealthComponent>();
			if (!TestTrue(TEXT("실제 병력 체력 준비"), Health->TryGetHealth(Before))) return false;
			if (!TestTrue(TEXT("일반 지면 조준 발사"), Castle->Weapons->CastWeapon(Weapon, FVector(1600, 0, 0)) == ELSWeaponCastResult::Ready)) return false;
			for (TActorIterator<ALSCastleProjectile> It(World); It; ++It) It->Tick(1);
			for (TActorIterator<ALSBasicProjectile> It(World); It; ++It) It->Tick(1);
			Health->TryGetHealth(After);
			TestEqual(TEXT("중심 높이는 병력을 지나치고 낮춘 발사 평면은 명중"), After.CurrentHealth,
				bGroundHeight ? Before.CurrentHealth - Settings.Damage : Before.CurrentHealth);
			Fixture.ForwardErrorMessages(this);
		}
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Castle = LSCastleTestFixture::SpawnWithStats(World, LSCastleTestFixture::FixedStats(), FVector(0, 0, 500));
		Castle->GetCapsuleComponent()->SetCapsuleSize(500, 500);
		Castle->Weapons->ShotgunLaunchOffset = FVector(0, 0, -412);
		Castle->Weapons->BallistaLaunchOffset = FVector(0, 0, -412);
		auto* Data = NewObject<ULSCastleWeaponData>(World);
		Data->Weapon = Weapon;
		FLSCastleWeaponSettings Settings;
		Settings.Range = 1600;
		Settings.Radius = 5;
		Settings.PelletCount = 1;
		Data->Levels.Add(Settings);
		if (!Castle->Weapons->SetWeaponDefinitions({Data})) return false;
		auto* Mine = World->SpawnActor<ALSMine>(FVector(700, 0, 20), FRotator::ZeroRotator);
		if (!Fixture.BeginPlayInTestWorld()) return false;
		if (!TestTrue(TEXT("낮춘 발사 위치에서 장애물 지정 발사"),
			Castle->Weapons->CastWeaponAtObstacle(Weapon, Mine) == ELSWeaponCastResult::Ready)) return false;
		for (TActorIterator<ALSCastleProjectile> It(World); It; ++It) It->Tick(1);
		for (TActorIterator<ALSBasicProjectile> It(World); It; ++It) It->Tick(1);
		TestFalse(TEXT("장애물 조준은 여전히 낮은 지뢰 몸체에 명중"), Mine->IsArmed());
		Fixture.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShotgunHullOriginTest,
	"Landship.Castle.Weapons.ShotgunHullOriginAndNearAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShotgunHullOriginTest::RunTest(const FString& Parameters)
{
	for (const FVector Direction : {FVector::ForwardVector, FVector::RightVector})
	for (const double AimDistance : {100.0, FMath::Sqrt(80256.0), 1000.0})
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 500), FRotator(0, 45, 0));
		Castle->GetCapsuleComponent()->SetCapsuleSize(500, 500);
		Castle->GetMovementComponent()->NavAgentProps.AgentRadius = 580;
		Castle->Weapons->ShotgunLaunchOffset = FVector(0, 0, -412);
		auto* Data = NewObject<ULSCastleWeaponData>(World);
		Data->Weapon = ELSCastleWeapon::Shotgun;
		FLSCastleWeaponSettings Settings;
		Settings.Range = 600;
		Settings.ProjectileSpeed = 1000;
		Settings.PelletCount = 1;
		Data->Levels.Add(Settings);
		if (!TestTrue(TEXT("외곽 발사 검사 수치 연결"), Castle->Weapons->SetWeaponDefinitions({Data}))) return false;
		if (!Fixture.BeginPlayInTestWorld()) return false;
		Castle->Orders->SetComponentTickEnabled(false);
		Castle->GetMovementComponent()->SetComponentTickEnabled(false);
		TestEqual(TEXT("검사 성의 내비게이션 반경은 실제 몸체와 다름"),
			Castle->GetMovementComponent()->NavAgentProps.AgentRadius, 580.f);
		const FVector Aim = Direction * AimDistance;
		const FVector ExpectedOrigin = Direction * FMath::Sqrt(80256.0) + FVector(0, 0, 88);
		TestTrue(TEXT("내비게이션 반경 대신 조준 방향의 실제 성 외곽 사용"),
			Castle->Weapons->GetWeaponFireOrigin(ELSCastleWeapon::Shotgun, Aim).Equals(ExpectedOrigin));
		for (const ELSCastleWeapon Weapon : {ELSCastleWeapon::Cannon, ELSCastleWeapon::Ballista})
			TestTrue(TEXT("대포와 발리스타는 설정한 발사 위치 유지"),
				Castle->Weapons->GetWeaponFireOrigin(Weapon, Aim).Equals(Castle->Weapons->GetWeaponLaunchLocation(Weapon)));
		if (!TestTrue(TEXT("성 내부와 외곽의 지면 조준도 발사 성공"),
			Castle->Weapons->CastWeapon(ELSCastleWeapon::Shotgun, Aim) == ELSWeaponCastResult::Ready)) return false;
		ALSCastleProjectile* Shot = nullptr;
		for (TActorIterator<ALSCastleProjectile> It(World); It; ++It) Shot = *It;
		if (!TestNotNull(TEXT("실제 산탄 투사체 생성"), Shot)) return false;
		TestTrue(TEXT("투사체 액터가 성 외곽에서 시작"), Shot->GetActorLocation().Equals(ExpectedOrigin));
		TArray<FVector> Positions;
		Shot->GetFlightPositions(Positions);
		if (!TestEqual(TEXT("검사 산탄 한 발"), Positions.Num(), 1)) return false;
		TestTrue(TEXT("실제 탄도 성 외곽에서 시작"), Positions[0].Equals(ExpectedOrigin));
		Shot->Tick(.1f);
		Shot->GetFlightPositions(Positions);
		if (!TestEqual(TEXT("근거리 조준 뒤에도 탄 비행 유지"), Positions.Num(), 1)) return false;
		TestTrue(TEXT("조준점이 발사 위치보다 가까워도 전방으로 수평 비행"),
			Positions[0].Equals(ExpectedOrigin + Direction * 100, .01));
		Fixture.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShotgunHullRangeTest,
	"Landship.Castle.Weapons.ShotgunRangeStartsAtHull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShotgunHullRangeTest::RunTest(const FString& Parameters)
{
	for (const bool bInsideRange : {true, false})
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 500), FRotator::ZeroRotator);
		Castle->GetCapsuleComponent()->SetCapsuleSize(500, 500);
		Castle->Weapons->ShotgunLaunchOffset = FVector(0, 0, -412);
		auto* Target = World->SpawnActor<ALSBasicAttackTestActor>(
			FVector(bInsideRange ? 850 : 950, 0, 88), FRotator::ZeroRotator);
		Target->SetInitialFaction(ELSFaction::Enemy);
		Target->Attack->SetAutoAttackOnBeginPlay(false);
		auto* Data = NewObject<ULSCastleWeaponData>(World);
		Data->Weapon = ELSCastleWeapon::Shotgun;
		FLSCastleWeaponSettings Settings;
		Settings.Range = 600;
		Settings.ProjectileSpeed = 1000;
		Settings.Damage = 20;
		Settings.PelletCount = 1;
		Settings.PelletRadius = 2;
		Settings.KnockbackSpeed = 0;
		Settings.KnockbackLift = 0;
		Data->Levels.Add(Settings);
		if (!TestTrue(TEXT("외곽 사거리 검사 수치 연결"), Castle->Weapons->SetWeaponDefinitions({Data}))) return false;
		if (!Fixture.BeginPlayInTestWorld()) return false;
		Castle->Orders->SetComponentTickEnabled(false);
		Castle->GetMovementComponent()->SetComponentTickEnabled(false);
		if (!TestTrue(TEXT("외곽 기준 사거리 발사"), Castle->Weapons->CastWeapon(
			ELSCastleWeapon::Shotgun, FVector(1000, 0, 0)) == ELSWeaponCastResult::Ready)) return false;
		ALSCastleProjectile* Shot = nullptr;
		for (TActorIterator<ALSCastleProjectile> It(World); It; ++It) Shot = *It;
		if (!TestNotNull(TEXT("사거리 검사 실제 투사체"), Shot)) return false;
		Shot->Tick(1);
		TestEqual(TEXT("사거리는 성 중심이 아니라 외곽 발사 위치부터 적용"),
			Target->GetCurrentHealth(), bInsideRange ? 80.f : 100.f);
		TestTrue(TEXT("명중 또는 외곽부터 최대 비행 거리 뒤 제거"), Shot->IsActorBeingDestroyed());
		Fixture.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShotgunContactDefenseTest,
	"Landship.Castle.Weapons.ShotgunContactDefense",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShotgunContactDefenseTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* Castle = LSCastleTestFixture::SpawnWithStats(World, LSCastleTestFixture::FixedStats(), FVector(0, 0, 500));
	Castle->GetCapsuleComponent()->SetCapsuleSize(500, 500);
	Castle->Weapons->ShotgunLaunchOffset = FVector(0, 0, -412);
	auto* Spearman = World->SpawnActor<ALSTroopUnit>(FVector(400, 0, 88), FRotator::ZeroRotator);
	Spearman->SetInitialFaction(ELSFaction::Enemy);
	if (!TestTrue(TEXT("접촉 검사 고정 능력치 지정"),
		LSCombatStatTestFixture::ConfigureUnit(Spearman, LSCombatStatTestFixture::UnitStats()))) return false;
	Spearman->BasicAttack->SetAutoAttackOnBeginPlay(false);
	auto* Data = NewObject<ULSCastleWeaponData>(World);
	Data->Weapon = ELSCastleWeapon::Shotgun;
	FLSCastleWeaponSettings Settings;
	Settings.Damage = 20;
	Settings.Range = 600;
	Settings.PelletCount = 1;
	Settings.PelletRadius = 2;
	Settings.KnockbackSpeed = 0;
	Settings.KnockbackLift = 0;
	Data->Levels.Add(Settings);
	if (!Castle->Weapons->SetWeaponDefinitions({Data}) || !Fixture.BeginPlayInTestWorld()) return false;
	Castle->Orders->SetComponentTickEnabled(false);
	Castle->GetMovementComponent()->SetComponentTickEnabled(false);
	Spearman->Orders->SetComponentTickEnabled(false);
	Spearman->GetCharacterMovement()->SetComponentTickEnabled(false);
	FLSHealthSnapshot Before, After;
	auto* Health = Spearman->FindComponentByClass<ULSHealthComponent>();
	if (!Health->TryGetHealth(Before)) return false;
	TestEqual(TEXT("접촉 검사 고정 초기 체력"), Before.CurrentHealth, 180.0f);
	if (!TestTrue(TEXT("접촉 거리의 창병을 향해 산탄 발사"), Castle->Weapons->CastWeapon(
		ELSCastleWeapon::Shotgun, FVector(1000, 0, 0)) == ELSWeaponCastResult::Ready)) return false;
	for (TActorIterator<ALSCastleProjectile> It(World); It; ++It) It->Tick(1);
	Health->TryGetHealth(After);
	TestEqual(TEXT("성에 붙은 창병도 발사점 앞에서 명중"), After.CurrentHealth, Before.CurrentHealth - 20);
	Fixture.ForwardErrorMessages(this);
	return true;
}

#endif
