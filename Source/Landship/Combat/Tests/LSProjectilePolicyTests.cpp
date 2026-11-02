#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LSBasicProjectile.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"

namespace
{
enum class ELSProjectileFlightScenario : uint8
{
	DestroyedTarget,
	DestroyedSource,
	LiveSourceWithoutSnapshot,
	EnableFriendlyFire,
	DisableFriendlyFire
};

ALSBasicAttackTestActor* SpawnPolicyCombatant(UWorld* World, ELSFaction Faction, const FVector& Location)
{
	auto* Actor = World->SpawnActor<ALSBasicAttackTestActor>();
	Actor->SetInitialFaction(Faction);
	Actor->SetActorLocation(Location);
	Actor->Attack->SetAutoAttackOnBeginPlay(false);
	return Actor;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProjectileFlightPolicyTest, "Landship.Combat.Projectile.LiveFlightPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSProjectileFlightPolicyTest::RunTest(const FString& Parameters)
{
	auto* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bOriginalFriendlyFire = Settings->bAllowFriendlyFire;
	const bool bOriginalSelfDamage = Settings->bAllowSelfDamage;
	ON_SCOPE_EXIT
	{
		Settings->bAllowFriendlyFire = bOriginalFriendlyFire;
		Settings->bAllowSelfDamage = bOriginalSelfDamage;
	};
	Settings->bAllowSelfDamage = true;
	for (ELSFaction SourceFaction : {ELSFaction::Ally, ELSFaction::Enemy})
	{
		for (bool bFriendlyFire : {false, true})
		{
			for (ELSProjectileFlightScenario Scenario : {
				ELSProjectileFlightScenario::DestroyedTarget, ELSProjectileFlightScenario::DestroyedSource,
				ELSProjectileFlightScenario::LiveSourceWithoutSnapshot, ELSProjectileFlightScenario::EnableFriendlyFire,
				ELSProjectileFlightScenario::DisableFriendlyFire})
			{
				Settings->bAllowFriendlyFire = bFriendlyFire;
				if (Scenario == ELSProjectileFlightScenario::EnableFriendlyFire)
					Settings->bAllowFriendlyFire = false;
				if (Scenario == ELSProjectileFlightScenario::DisableFriendlyFire)
					Settings->bAllowFriendlyFire = true;
				FTestWorldWrapper Fixture;
				if (!Fixture.CreateTestWorld(EWorldType::Game))
					return false;
				UWorld* World = Fixture.GetTestWorld();
				const ELSFaction EnemyFaction = SourceFaction == ELSFaction::Ally ? ELSFaction::Enemy : ELSFaction::Ally;
				auto* Source = SpawnPolicyCombatant(World, SourceFaction, FVector(0, 0, 100));
				auto* Intended = SpawnPolicyCombatant(World, EnemyFaction, FVector(700, 0, 100));
				auto* OtherEnemy = SpawnPolicyCombatant(World, EnemyFaction, FVector(500, 0, 100));
				auto* Ally = SpawnPolicyCombatant(World, SourceFaction, FVector(300, 0, 100));
				auto* Observer = SpawnPolicyCombatant(World, SourceFaction, FVector(0, 1000, 100));
				if (Scenario == ELSProjectileFlightScenario::DestroyedTarget)
					Ally->SetActorLocation(FVector(300, 1000, 100));
				if (!Fixture.BeginPlayInTestWorld())
					return false;
				FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(10, Source, nullptr);
				if (Scenario == ELSProjectileFlightScenario::LiveSourceWithoutSnapshot)
				{
					Request.bHasSourceFaction = false;
					Request.SourceFaction = EnemyFaction;
				}
				const FTransform Transform(FRotator::ZeroRotator, Source->GetActorLocation());
				auto* Shot = World->SpawnActorDeferred<ALSBasicProjectile>(ALSBasicProjectile::StaticClass(), Transform,
					nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!TestTrue(TEXT("Basic Launch accepts valid request including absent faction snapshot"),
					Shot->Launch(Request, Intended, FVector::ForwardVector, 1000, 2, 2)))
					return false;
				Shot->OnDamageApplied.AddDynamic(Observer, &ALSBasicAttackTestActor::ObserveAttack);
				Shot->FinishSpawning(Transform);
				Shot->Tick(0.05f);
				TestEqual(TEXT("No damage before live policy change"), Observer->Notifications, 0);
				if (Scenario == ELSProjectileFlightScenario::DestroyedTarget)
					Intended->Destroy();
				if (Scenario == ELSProjectileFlightScenario::DestroyedSource)
					Source->Destroy();
				if (Scenario == ELSProjectileFlightScenario::EnableFriendlyFire)
					Settings->bAllowFriendlyFire = true;
				if (Scenario == ELSProjectileFlightScenario::DisableFriendlyFire)
					Settings->bAllowFriendlyFire = false;
				const bool bHitAlly = Settings->bAllowFriendlyFire && Scenario != ELSProjectileFlightScenario::DestroyedTarget;
				Shot->Tick(0.8f);
				const FString Context = FString::Printf(TEXT("Faction=%d FF=%d Scenario=%d"),
					static_cast<int32>(SourceFaction), bFriendlyFire, static_cast<int32>(Scenario));
				TestEqual(Context + TEXT(" ally health"), Ally->GetCurrentHealth(), bHitAlly ? 90.f : 100.f);
				TestEqual(Context + TEXT(" other enemy health"), OtherEnemy->GetCurrentHealth(), bHitAlly ? 100.f : 90.f);
				TestEqual(Context + TEXT(" actual hit notification"), Observer->LastNotifiedTarget.Get(),
					static_cast<AActor*>(bHitAlly ? Ally : OtherEnemy));
				TestEqual(Context + TEXT(" one notification"), Observer->Notifications, 1);
				TestTrue(Context + TEXT(" shot consumed"), Shot->IsActorBeingDestroyed());
				Fixture.ForwardErrorMessages(this);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProjectileAutoTargetPolicyTest, "Landship.Combat.Projectile.AutoTargetsEnemiesOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSProjectileAutoTargetPolicyTest::RunTest(const FString& Parameters)
{
	auto* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bOriginalFriendlyFire = Settings->bAllowFriendlyFire;
	const bool bOriginalSelfDamage = Settings->bAllowSelfDamage;
	ON_SCOPE_EXIT
	{
		Settings->bAllowFriendlyFire = bOriginalFriendlyFire;
		Settings->bAllowSelfDamage = bOriginalSelfDamage;
	};
	Settings->bAllowSelfDamage = true;
	for (ELSFaction Faction : {ELSFaction::Ally, ELSFaction::Enemy})
	{
		for (bool bFriendlyFire : {false, true})
		{
			Settings->bAllowFriendlyFire = bFriendlyFire;
			FTestWorldWrapper Fixture;
			if (!Fixture.CreateTestWorld(EWorldType::Game))
				return false;
			UWorld* World = Fixture.GetTestWorld();
			auto* Source = SpawnPolicyCombatant(World, Faction, FVector(0, 0, 100));
			auto* Ally = SpawnPolicyCombatant(World, Faction, FVector(0, 20, 100));
			auto* Enemy = SpawnPolicyCombatant(World, Faction == ELSFaction::Ally ? ELSFaction::Enemy : ELSFaction::Ally,
				FVector(500, 0, 100));
			if (!Fixture.BeginPlayInTestWorld())
				return false;
			FLSBasicAttackSettings Attack;
			Attack.Damage = 10;
			Attack.Range = 1000;
			Attack.Interval = 1;
			Source->Attack->ConfigureAttack(Attack);
			FLSProjectileAttackSettings Projectile;
			Projectile.ProjectileClass = ALSBasicProjectile::StaticClass();
			Source->Attack->ConfigureProjectile(Projectile);
			TestFalse(TEXT("Explicit friendly target remains invalid with either flag"), Source->Attack->StartAttack(Ally));
			TestTrue(TEXT("Automatic enemy acquisition starts"), Source->Attack->StartAutoAttack());
			TestEqual(TEXT("Closer ally is never acquired"), Source->Attack->GetAttackTarget(), static_cast<AActor*>(Enemy));
			Source->Attack->StopAttack();
			Fixture.ForwardErrorMessages(this);
		}
	}
	return true;
}

#endif
