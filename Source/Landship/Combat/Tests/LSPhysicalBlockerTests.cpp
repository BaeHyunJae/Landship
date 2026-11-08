#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSPhysicalBlocker.h"
#include "Combat/Tests/LSPhysicalBlockerTestActor.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"

namespace
{
void ConfigureBlockerTestActor(ALSBasicAttackTestActor* Actor, FVector Location, ELSFaction Faction)
{
	Actor->SetActorLocation(Location);
	Actor->SetInitialFaction(Faction);
	Actor->Attack->SetAutoAttackOnBeginPlay(false);
	CastChecked<UPrimitiveComponent>(Actor->GetRootComponent())->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSPhysicalCoverOptInTest, "Landship.Combat.Projectile.PhysicalBlockerOptInPreservesOrdinaryCorpsePassThrough",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSPhysicalCoverOptInTest::RunTest(const FString& Parameters)
{
	auto* Policy = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bPreviousFriendlyFire = Policy->bAllowFriendlyFire;
	ON_SCOPE_EXIT { Policy->bAllowFriendlyFire = bPreviousFriendlyFire; };
	Policy->bAllowFriendlyFire = false;
	for (int32 Case = 0; Case < 4; ++Case)
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = World->SpawnActor<ALSBasicAttackTestActor>();
		auto* Target = World->SpawnActor<ALSBasicAttackTestActor>();
		auto* Cover = World->SpawnActor<ALSPhysicalBlockerTestActor>();
		ConfigureBlockerTestActor(Source, FVector(0, 0, 100), ELSFaction::Ally);
		ConfigureBlockerTestActor(Cover, FVector(200, 0, 100), Case == 3 ? ELSFaction::Ally : ELSFaction::Enemy);
		ConfigureBlockerTestActor(Target, FVector(600, 0, 100), ELSFaction::Enemy);
		Cover->bPhysicalBlockingEnabled = Case != 0;
		if (!Fixture.BeginPlayInTestWorld()) return false;
		if (Case != 3 && !TestEqual(TEXT("Cover dies through common Health"), Cover->ApplyTestDamage(100, Source).AppliedDamage, 100.f)) return false;
		UPrimitiveComponent* CoverBody = CastChecked<UPrimitiveComponent>(Cover->GetRootComponent());
		if (Case == 2) CoverBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		const bool bExpectedBlocking = Case == 1 || Case == 3;
		TestEqual(TEXT("Only opted query-enabled body is a physical blocker"), LSPhysicalBlocker::IsBlockingBody(CoverBody), bExpectedBlocking);
		FLSBasicAttackSettings Attack;
		Attack.Damage = 10; Attack.Interval = 1; Attack.Range = 1000;
		if (!TestTrue(TEXT("Common attack configured"), Source->Attack->ConfigureAttack(Attack))) return false;
		FLSProjectileAttackSettings Projectile;
		Projectile.ProjectileClass = ALSBasicProjectile::StaticClass();
		Projectile.Speed = 1000; Projectile.Radius = 2; Projectile.Lifetime = 2;
		if (!TestTrue(TEXT("Projectile clearance configured"), Source->Attack->ConfigureProjectile(Projectile))) return false;
		TestEqual(TEXT("Attack clearance matches physical blocker opt-in"),
			Source->Attack->HasClearAttackPathFromLocation(Source->GetActorLocation(), Target), !bExpectedBlocking);
		const FTransform Transform(FRotator::ZeroRotator, Source->GetActorLocation());
		auto* Shot = World->SpawnActorDeferred<ALSBasicProjectile>(ALSBasicProjectile::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!TestTrue(TEXT("Normal shared projectile launches"), Shot->Launch(
			ULSHealthComponent::MakeDamageRequest(10, Source, nullptr), Target, FVector::ForwardVector, 1000, 2, 2))) return false;
		Shot->FinishSpawning(Transform);
		Shot->Tick(.8f);
		TestEqual(TEXT("Only opted persistent cover stops the real projectile"), Target->GetCurrentHealth(), bExpectedBlocking ? 100.f : 90.f);
		TestEqual(TEXT("Physical stopping does not bypass Health or friendly damage policy"), Cover->GetCurrentHealth(), Case == 3 ? 100.f : 0.f);
		TestTrue(TEXT("First hit consumes the shot"), Shot->IsActorBeingDestroyed());
		Fixture.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSPhysicalBlockerAttackTest, "Landship.Combat.Attack.ExplicitPhysicalBlockerUsesDamagePolicyAndKeepsCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSPhysicalBlockerAttackTest::RunTest(const FString& Parameters)
{
	auto* Policy = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bPreviousFriendlyFire = Policy->bAllowFriendlyFire;
	ON_SCOPE_EXIT { Policy->bAllowFriendlyFire = bPreviousFriendlyFire; };
	for (bool bFriendlyFire : {false, true})
	{
		Policy->bAllowFriendlyFire = bFriendlyFire;
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = World->SpawnActor<ALSBasicAttackTestActor>();
		auto* Blocker = World->SpawnActor<ALSPhysicalBlockerTestActor>();
		ConfigureBlockerTestActor(Source, FVector(0, 0, 100), ELSFaction::Enemy);
		ConfigureBlockerTestActor(Blocker, FVector(100, 0, 100), ELSFaction::Enemy);
		if (!Fixture.BeginPlayInTestWorld()) return false;
		FLSBasicAttackSettings Settings;
		Settings.Damage = 10; Settings.Interval = 1; Settings.Range = 200;
		if (!TestTrue(TEXT("Common melee configured"), Source->Attack->ConfigureAttack(Settings))) return false;
		TestFalse(TEXT("Ordinary attack never acquires same-faction blocker"), Source->Attack->StartAttack(Blocker));
		int32 ResolverCalls = 0;
		Source->Attack->SetAttackTargetResolver(FLSResolveAttackTarget::CreateLambda([&ResolverCalls]() -> AActor*
		{
			++ResolverCalls;
			return nullptr;
		}));
		TestEqual(TEXT("Explicit blocker request follows friendly-fire policy"), Source->Attack->StartPhysicalBlockerAttack(Blocker), bFriendlyFire);
		TestEqual(TEXT("Common Health receives allowed physical-blocker attack"), Blocker->GetCurrentHealth(), bFriendlyFire ? 90.f : 100.f);
		TestEqual(TEXT("Explicit physical attack never retargets through resolver"), ResolverCalls, 0);
		if (bFriendlyFire)
		{
			const float Cooldown = Source->Attack->GetCooldownRemaining();
			TestTrue(TEXT("Repeat explicit request accepted without resetting cooldown"), Source->Attack->StartPhysicalBlockerAttack(Blocker));
			TestEqual(TEXT("Repeat request cannot produce another hit"), Blocker->GetCurrentHealth(), 90.f);
			Source->Attack->StopAttack();
			TestEqual(TEXT("Stop keeps cooldown"), Source->Attack->GetCooldownRemaining(), Cooldown);
			TestTrue(TEXT("Explicit mode can resume within cooldown"), Source->Attack->StartPhysicalBlockerAttack(Blocker));
			TestFalse(TEXT("Normal request clears explicit mode and still rejects same faction"), Source->Attack->StartAttack(Blocker));
			TestNull(TEXT("Rejected normal switch does not leave explicit target armed"), Source->Attack->GetAttackTarget());
		}
		Blocker->bPhysicalBlockingEnabled = false;
		TestFalse(TEXT("Opt-out body cannot use explicit blocker attack"), Source->Attack->StartPhysicalBlockerAttack(Blocker));
		Source->Attack->SetAttackTargetResolver(FLSResolveAttackTarget());
		Fixture.ForwardErrorMessages(this);
	}
	return true;
}

#endif
