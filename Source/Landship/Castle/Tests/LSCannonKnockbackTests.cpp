#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSCastleProjectile.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Combat/Tests/LSCombatStatTestFixture.h"
#include "Combat/Tests/LSHealthTestObserver.h"
#include "Components/CapsuleComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Troops/LSTroopUnit.h"

#include <limits>

namespace
{
struct FLSCannonPolicyScope
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	bool bFriendlyFire = Settings->bAllowFriendlyFire;
	bool bSelfDamage = Settings->bAllowSelfDamage;
	FLSCannonPolicyScope()
	{
		Settings->bAllowFriendlyFire = true;
		Settings->bAllowSelfDamage = true;
	}
	~FLSCannonPolicyScope()
	{
		Settings->bAllowFriendlyFire = bFriendlyFire;
		Settings->bAllowSelfDamage = bSelfDamage;
	}
};

FLSCastleWeaponSettings CannonKnockbackSettings()
{
	FLSCastleWeaponSettings Settings;
	Settings.Damage = 20;
	Settings.Radius = 200;
	Settings.ProjectileSpeed = 1000;
	Settings.KnockbackSpeed = 500;
	Settings.KnockbackLift = 120;
	return Settings;
}

ALSBasicAttackTestActor* MakeCannonSource(UWorld* World, const FVector& Location)
{
	auto* Source = World->SpawnActor<ALSBasicAttackTestActor>(Location, FRotator::ZeroRotator);
	Source->SetInitialFaction(ELSFaction::Enemy);
	Source->Attack->SetAutoAttackOnBeginPlay(false);
	return Source;
}

void FreezeCannonRecipient(ALSTroopUnit* Unit)
{
	Unit->Orders->SetComponentTickEnabled(false);
	Unit->BasicAttack->StopAttack();
	Unit->GetCharacterMovement()->SetComponentTickEnabled(false);
	Unit->GetCapsuleComponent()->SetCapsuleSize(42, 88);
}

ULSHealthTestObserver* ObserveCannonDamage(ALSTroopUnit* Unit)
{
	auto* Health = Unit->FindComponentByClass<ULSHealthComponent>();
	auto* Observer = NewObject<ULSHealthTestObserver>(Health);
	Health->OnDamagedEvent.AddDynamic(Observer, &ULSHealthTestObserver::OnDamage);
	return Observer;
}

ALSCastleProjectile* LaunchKnockbackCannon(FAutomationTestBase& Test, UWorld* World,
                                        ALSBasicAttackTestActor* Source, const FVector& Start,
                                        const FVector& Aim, const FLSCastleWeaponSettings& Settings)
{
	const FTransform Transform(FRotator::ZeroRotator, Start);
	auto* Shot = World->SpawnActorDeferred<ALSCastleProjectile>(ALSCastleProjectile::StaticClass(), Transform,
	    nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Test.TestTrue(TEXT("대포 발사 준비"), Shot->Launch(ELSCastleWeapon::Cannon, Settings,
	    ULSHealthComponent::MakeDamageRequest(Settings.Damage, Source, Source), Aim)))
	{
		Shot->Destroy();
		return nullptr;
	}
	Shot->FinishSpawning(Transform);
	return Shot;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCannonKnockbackVelocityTest,
    "Landship.Castle.Knockback.CannonVelocityAndFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCannonKnockbackVelocityTest::RunTest(const FString& Parameters)
{
	FLSCannonPolicyScope Restore;
	struct FLSVelocityCase
	{
		FVector Offset;
		bool bVerticalShot;
		float Speed;
		float Lift;
		FVector Expected;
	};
	const FLSVelocityCase Cases[] = {
	    {FVector(60, 80, 50), false, 500, 120, FVector(300, 400, 120)},
	    {FVector(-100, 0, 0), false, 500, 120, FVector(-500, 0, 120)},
	    {FVector::ZeroVector, false, 500, 120, FVector(500, 0, 120)},
	    {FVector::ZeroVector, true, 500, 120, FVector(0, 0, 120)},
	    {FVector::ZeroVector, true, 500, 0, FVector::ZeroVector},
	    {FVector(0, 100, 0), false, 500, 0, FVector(0, 500, 0)},
	    {FVector(0, 100, 0), false, 0, 120, FVector(0, 0, 120)},
	    {FVector(0, 100, 0), false, 0, 0, FVector::ZeroVector},
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Cases); ++Index)
	{
		const auto& Case = Cases[Index];
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		const FVector Impact(400, 0, 100);
		const FVector Start = Case.bVerticalShot ? Impact + FVector(0, 0, 500) : FVector(0, 0, 100);
		auto* Source = MakeCannonSource(World, FVector(-1000, 0, 100));
		auto* Recipient = World->SpawnActor<ALSTroopUnit>(Impact + Case.Offset, FRotator::ZeroRotator);
		if (!TestTrue(TEXT("병력 스탯 설정"), LSCombatStatTestFixture::ConfigureUnit(Recipient, LSCombatStatTestFixture::UnitStats(180.f)))) return false;
		if (!Fixture.BeginPlayInTestWorld()) return false;
		FreezeCannonRecipient(Recipient);
		auto* Observer = ObserveCannonDamage(Recipient);
		FLSCastleWeaponSettings Settings = CannonKnockbackSettings();
		Settings.KnockbackSpeed = Case.Speed;
		Settings.KnockbackLift = Case.Lift;
		auto* Shot = LaunchKnockbackCannon(*this, World, Source, Start, Impact, Settings);
		if (!Shot) return false;
		Source->Destroy();
		Shot->Tick(1);
		const FString Label = FString::Printf(TEXT("Case=%d "), Index);
		TestEqual(*(Label + TEXT("피해는 한 번 적용한다")), Observer->DamageResults.Num(), 1);
		if (Observer->DamageResults.Num() == 1)
			TestEqual(*(Label + TEXT("실제 피해량 유지")), Observer->DamageResults[0].AppliedDamage, 20.f);
		TestTrue(*(Label + TEXT("XY 방향과 설정한 넉백 속도를 적용한다")),
		    Recipient->GetCharacterMovement()->PendingLaunchVelocity.Equals(Case.Expected, .01));
		TestEqual(*(Label + TEXT("최종 속도가 0일 때만 넉백을 생략한다")),
		    Recipient->Orders->IsKnockedBack(), !Case.Expected.IsNearlyZero());
		Recipient->GetCharacterMovement()->PendingLaunchVelocity = FVector::ZeroVector;
		Shot->Tick(1);
		TestTrue(*(Label + TEXT("착탄 뒤 같은 대상을 다시 밀지 않는다")),
		    Recipient->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCannonKnockbackSnapshotTest,
    "Landship.Castle.Knockback.CannonSnapshotsAllDirectionsBeforeDamage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCannonKnockbackSnapshotTest::RunTest(const FString& Parameters)
{
	FLSCannonPolicyScope Restore;
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	const FVector Impact(400, 0, 100);
	auto* Source = MakeCannonSource(World, FVector(-1000, 0, 100));
	auto* East = World->SpawnActor<ALSTroopUnit>(Impact + FVector(100, 0, 0), FRotator::ZeroRotator);
	auto* North = World->SpawnActor<ALSTroopUnit>(Impact + FVector(0, 100, 0), FRotator::ZeroRotator);
	if (!Fixture.BeginPlayInTestWorld()) return false;
	FreezeCannonRecipient(East);
	FreezeCannonRecipient(North);
	auto* EastObserver = ObserveCannonDamage(East);
	auto* NorthObserver = ObserveCannonDamage(North);
	EastObserver->DamageCallback = [North, Impact](const FLSDamageResult&)
	{
		North->SetActorLocation(Impact + FVector(0, -1000, 0), false, nullptr, ETeleportType::TeleportPhysics);
	};
	NorthObserver->DamageCallback = [East, Impact](const FLSDamageResult&)
	{
		East->SetActorLocation(Impact + FVector(-1000, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
	};
	auto* Shot = LaunchKnockbackCannon(*this, World, Source, FVector(0, 0, 100), Impact, CannonKnockbackSettings());
	if (!Shot) return false;
	Shot->Tick(1);
	TestEqual(TEXT("동쪽 대상의 피해 알림"), EastObserver->DamageResults.Num(), 1);
	TestEqual(TEXT("북쪽 대상의 피해 알림"), NorthObserver->DamageResults.Num(), 1);
	TestTrue(TEXT("동쪽 대상은 다른 대상의 알림에서 이동해도 최초 위치 방향으로 밀린다"),
	    East->GetCharacterMovement()->PendingLaunchVelocity.Equals(FVector(500, 0, 120), .01));
	TestTrue(TEXT("북쪽 대상은 다른 대상의 알림에서 이동해도 최초 위치 방향으로 밀린다"),
	    North->GetCharacterMovement()->PendingLaunchVelocity.Equals(FVector(0, 500, 120), .01));
	TestTrue(TEXT("두 피해 콜백을 모두 실행했다"),
	    East->GetActorLocation().Equals(Impact + FVector(-1000, 0, 0), .01) &&
	    North->GetActorLocation().Equals(Impact + FVector(0, -1000, 0), .01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCannonZeroKnockbackActionTest,
    "Landship.Castle.Knockback.CannonZeroVelocityPreservesActions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCannonZeroKnockbackActionTest::RunTest(const FString& Parameters)
{
	FLSCannonPolicyScope Restore;
	for (bool bNoIncomingDirection : {false, true})
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		const FVector Impact(400, 0, 100);
		auto* Source = MakeCannonSource(World, Impact + FVector(100, 0, 0));
		auto* Recipient = World->SpawnActor<ALSTroopUnit>(Impact, FRotator::ZeroRotator);
		if (!Fixture.BeginPlayInTestWorld()) return false;
		FreezeCannonRecipient(Recipient);
		FLSOrder Attack;
		Attack.Kind = ELSOrderKind::AttackTarget;
		Attack.Target = Source;
		if (!TestTrue(TEXT("실제 지정 공격 명령 시작"), Recipient->Orders->SubmitOrder(Attack))) return false;
		if (!TestTrue(TEXT("기본 공격 실행 중"), Recipient->BasicAttack->GetAttackTarget() == Source)) return false;
		const FVector Velocity(35, 15, 0);
		Recipient->GetCharacterMovement()->Velocity = Velocity;
		auto* Observer = ObserveCannonDamage(Recipient);
		FLSCastleWeaponSettings Settings = CannonKnockbackSettings();
		Settings.KnockbackSpeed = bNoIncomingDirection ? 500 : 0;
		Settings.KnockbackLift = 0;
		const FVector Start = bNoIncomingDirection ? Impact + FVector(0, 0, 500) : FVector(0, 0, 100);
		auto* Shot = LaunchKnockbackCannon(*this, World, Source, Start, Impact, Settings);
		if (!Shot) return false;
		Shot->Tick(1);
		TestEqual(TEXT("넉백을 생략해도 실제 피해 알림 발생"), Observer->DamageResults.Num(), 1);
		TestFalse(TEXT("0 속도는 넉백 상태를 시작하지 않는다"), Recipient->Orders->IsKnockedBack());
		TestTrue(TEXT("0 속도는 이동 속도를 지우지 않는다"), Recipient->GetCharacterMovement()->Velocity.Equals(Velocity));
		TestTrue(TEXT("0 속도는 보류된 Launch를 만들지 않는다"),
		    Recipient->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero());
		TestTrue(TEXT("0 속도는 현재 공격 실행을 중단하지 않는다"), Recipient->BasicAttack->GetAttackTarget() == Source);
		TestTrue(TEXT("0 속도는 교전 대상을 해제하지 않는다"), Recipient->Orders->GetEngagedTarget() == Source);
		TestEqual(TEXT("지정 공격 명령 유지"), Recipient->Orders->GetOrderKind(), ELSOrderKind::AttackTarget);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCannonKnockbackValidationTest,
    "Landship.Castle.Knockback.CannonSettingsValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSCannonKnockbackValidationTest::RunTest(const FString& Parameters)
{
	for (float Invalid : {-1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	for (bool bSpeed : {false, true})
	{
		FLSCastleWeaponSettings Settings = CannonKnockbackSettings();
		(bSpeed ? Settings.KnockbackSpeed : Settings.KnockbackLift) = Invalid;
		const FString Label = FString::Printf(TEXT("Speed=%d Invalid=%g "), bSpeed, Invalid);
		TestFalse(*(Label + TEXT("대포는 유한한 0 이상의 넉백 수치만 허용한다")),
		    ULSCastleWeaponData::AreSettingsValid(ELSCastleWeapon::Cannon, Settings));
		TestFalse(*(Label + TEXT("산탄의 기존 넉백 검증 유지")),
		    ULSCastleWeaponData::AreSettingsValid(ELSCastleWeapon::Shotgun, Settings));
		TestTrue(*(Label + TEXT("발리스타는 사용하지 않는 넉백 수치를 거부하지 않는다")),
		    ULSCastleWeaponData::AreSettingsValid(ELSCastleWeapon::Ballista, Settings));
	}
	FLSCastleWeaponSettings Settings = CannonKnockbackSettings();
	Settings.KnockbackSpeed = 0;
	Settings.KnockbackLift = 0;
	for (ELSCastleWeapon Weapon : {ELSCastleWeapon::Cannon, ELSCastleWeapon::Shotgun, ELSCastleWeapon::Ballista})
		TestTrue(TEXT("넉백 수치 0은 유효하다"), ULSCastleWeaponData::AreSettingsValid(Weapon, Settings));
	return true;
}

#endif
