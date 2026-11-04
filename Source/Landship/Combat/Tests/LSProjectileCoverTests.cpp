#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Combat/Tests/LSAttackPathTestPawn.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "Obstacles/LSBarricade.h"

namespace
{
UBoxComponent* AddCoverBox(AActor* Owner, const FVector& Position, ECollisionChannel ObjectType,
	ECollisionResponse ProjectileResponse)
{
	UBoxComponent* Box = NewObject<UBoxComponent>(Owner);
	Owner->AddInstanceComponent(Box);
	if (Owner->GetRootComponent())
		Box->SetupAttachment(Owner->GetRootComponent());
	else
		Owner->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(10, 100, 100));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionObjectType(ObjectType);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ProjectileResponse);
	Box->RegisterComponent();
	Box->SetWorldLocation(Position);
	return Box;
}

ALSBasicAttackTestActor* SpawnCoverCombatant(UWorld* World, ELSFaction Faction, float X)
{
	ALSBasicAttackTestActor* Actor = World->SpawnActor<ALSBasicAttackTestActor>();
	Actor->SetInitialFaction(Faction);
	Actor->SetActorLocation(FVector(X, 0, 100));
	Actor->Attack->SetAutoAttackOnBeginPlay(false);
	CastChecked<UPrimitiveComponent>(Actor->GetRootComponent())
		->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	return Actor;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProjectileCoverOrderTest, "Landship.Combat.Projectile.CoverAndBodyOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSProjectileCoverOrderTest::RunTest(const FString& Parameters)
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bOriginalFriendlyFire = Settings->bAllowFriendlyFire;
	ON_SCOPE_EXIT { Settings->bAllowFriendlyFire = bOriginalFriendlyFire; };
	for (ELSProjectileVisualStyle Style : {ELSProjectileVisualStyle::Arrow, ELSProjectileVisualStyle::TurretBolt, ELSProjectileVisualStyle::BallistaBolt})
	{
		for (int32 Scenario = 0; Scenario < 5; ++Scenario)
		{
			Settings->bAllowFriendlyFire = Scenario == 3;
			FTestWorldWrapper Fixture;
			if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
			UWorld* World = Fixture.GetTestWorld();
			ALSBasicAttackTestActor* Source = SpawnCoverCombatant(World, ELSFaction::Ally, 0);
			ALSBasicAttackTestActor* Enemy = SpawnCoverCombatant(World, ELSFaction::Enemy, Scenario == 1 ? 200 : 600);
			ALSBasicAttackTestActor* Friend = Scenario == 2 || Scenario == 3
				? SpawnCoverCombatant(World, ELSFaction::Ally, 200) : nullptr;
			if (Scenario == 4)
			{
				// 같은 목표의 보조 표시를 몸체보다 먼저 만나도 실제 몸체까지 계속 검사한다.
				AddCoverBox(Enemy, FVector(200, 0, 100), LSCollisionChannels::Building, ECR_Ignore);
			}
			else
				AddCoverBox(World->SpawnActor<AActor>(), FVector(400, 0, 100), ECC_WorldStatic, ECR_Block);
			if (!Fixture.BeginPlayInTestWorld()) return false;
			const FTransform Transform(FRotator::ZeroRotator, FVector(0, 0, 100));
			ALSBasicProjectile* Shot = World->SpawnActorDeferred<ALSBasicProjectile>(ALSBasicProjectile::StaticClass(), Transform);
			Shot->SetVisualStyle(Style);
			const FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(10, Source, nullptr);
			const bool bLaunched = Style == ELSProjectileVisualStyle::BallistaBolt
				? Shot->LaunchInDirection(Request, FVector::ForwardVector, 1000, 2, 2)
				: Shot->Launch(Request, Enemy, FVector::ForwardVector, 1000, 2, 2);
			if (!TestTrue(TEXT("Targeted or directional projectile launches"), bLaunched)) return false;
			Shot->OnDamageApplied.AddDynamic(Source, &ALSBasicAttackTestActor::ObserveAttack);
			Shot->FinishSpawning(Transform);
			Shot->Tick(0.9f);
			const FString Context = FString::Printf(TEXT("Style=%d Scenario=%d"), static_cast<int32>(Style), Scenario);
			const bool bEnemyHit = Scenario == 1 || Scenario == 4;
			TestEqual(Context + TEXT(" enemy damage follows earliest body or cover"), Enemy->GetCurrentHealth(), bEnemyHit ? 90.f : 100.f);
			if (Friend)
				TestEqual(Context + TEXT(" friendly body retains live damage policy"), Friend->GetCurrentHealth(), Scenario == 3 ? 90.f : 100.f);
			TestTrue(Context + TEXT(" projectile ends at cover or accepted body"), Shot->IsActorBeingDestroyed());
			TestEqual(Context + TEXT(" geometry emits no damage callback"), Source->Notifications, bEnemyHit || Scenario == 3 ? 1 : 0);
			if (Scenario == 0 || Scenario == 2)
				TestTrue(Context + TEXT(" actual wall stops shot"), Shot->GetActorLocation().X > 380 && Shot->GetActorLocation().X < 400);
			if (Scenario == 3)
				TestTrue(Context + TEXT(" friendly hit precedes wall"), Shot->GetActorLocation().X < 200);
			if (Scenario == 4)
				TestTrue(Context + TEXT(" ignored helper does not become target impact"), Shot->GetActorLocation().X > 580);
			Fixture.ForwardErrorMessages(this);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProjectileDirectionalPolicyCoverTest,
	"Landship.Combat.Projectile.DirectionalPolicyPassThroughStopsAtWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSProjectileDirectionalPolicyCoverTest::RunTest(const FString& Parameters)
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bOriginalFriendlyFire = Settings->bAllowFriendlyFire;
	const bool bOriginalSelfDamage = Settings->bAllowSelfDamage;
	ON_SCOPE_EXIT { Settings->bAllowFriendlyFire = bOriginalFriendlyFire; Settings->bAllowSelfDamage = bOriginalSelfDamage; };
	for (const bool bFriendlyFire : {false, true})
	{
		Settings->bAllowFriendlyFire = bFriendlyFire;
		Settings->bAllowSelfDamage = true;
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = SpawnCoverCombatant(World, ELSFaction::Enemy, 0);
		auto* Target = SpawnCoverCombatant(World, ELSFaction::Ally, 600);
		auto* FriendlyFacility = World->SpawnActor<ALSBarricade>();
		FriendlyFacility->SetActorLocation(FVector(200, 0, 100));
		AddCoverBox(World->SpawnActor<AActor>(), FVector(400, 0, 100), ECC_WorldStatic, ECR_Block);
		if (!Fixture.BeginPlayInTestWorld()) return false;
		FLSHealthSnapshot Before;
		if (!TestTrue(TEXT("Friendly facility initialized"), FriendlyFacility->GetHealthComponent()->TryGetHealth(Before))) return false;
		const FTransform Transform(FRotator::ZeroRotator, FVector(0, 0, 100));
		auto* Shot = World->SpawnActorDeferred<ALSBasicProjectile>(ALSBasicProjectile::StaticClass(), Transform);
		Shot->SetVisualStyle(ELSProjectileVisualStyle::BallistaBolt);
		if (!TestTrue(TEXT("Directional physical-cover shot launches"), Shot->LaunchInDirection(
			ULSHealthComponent::MakeDamageRequest(10, Source, nullptr), FVector::ForwardVector, 1000, 2, 2))) return false;
		Shot->OnDamageApplied.AddDynamic(Source, &ALSBasicAttackTestActor::ObserveAttack);
		Shot->FinishSpawning(Transform);
		Shot->Tick(.9f);
		FLSHealthSnapshot After;
		if (!FriendlyFacility->GetHealthComponent()->TryGetHealth(After)) return false;
		TestEqual(TEXT("Directional shot still excludes its own source when self damage is enabled"), Source->GetCurrentHealth(), 100.f);
		TestEqual(TEXT("Friendly facility obeys current damage policy"), After.CurrentHealth, Before.CurrentHealth - (bFriendlyFire ? 10.f : 0.f));
		TestEqual(TEXT("Enemy beyond physical wall never receives damage"), Target->GetCurrentHealth(), 100.f);
		TestEqual(TEXT("Only actual accepted facility damage emits an event"), Source->Notifications, bFriendlyFire ? 1 : 0);
		TestTrue(TEXT("Directional shot ends at permitted facility or later wall"), Shot->IsActorBeingDestroyed());
		TestTrue(TEXT("Rejected friendly facility passes through to physical wall"), bFriendlyFire ? Shot->GetActorLocation().X < 200 : Shot->GetActorLocation().X > 380);
		Fixture.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSProjectileDirectionalRadiusLifetimeTest,
	"Landship.Combat.Projectile.DirectionalCoverRadiusAndLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSProjectileDirectionalRadiusLifetimeTest::RunTest(const FString& Parameters)
{
	for (const int32 Scenario : {0, 1, 2})
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		auto* Source = SpawnCoverCombatant(World, ELSFaction::Ally, 0);
		auto* Target = SpawnCoverCombatant(World, ELSFaction::Enemy, 600);
		Target->SetActorLocation(FVector(600, 107, 100));
		AddCoverBox(World->SpawnActor<AActor>(), FVector(400, 0, 100), ECC_WorldStatic, ECR_Block);
		if (!Fixture.BeginPlayInTestWorld()) return false;
		const FTransform Transform(FRotator::ZeroRotator, FVector(0, 107, 100));
		auto* Shot = World->SpawnActorDeferred<ALSBasicProjectile>(ALSBasicProjectile::StaticClass(), Transform);
		const float Radius = Scenario == 1 ? 8.f : 2.f;
		const float Lifetime = Scenario == 2 ? .2f : 2.f;
		if (!TestTrue(TEXT("Directional radius/lifetime shot launches"), Shot->LaunchInDirection(
			ULSHealthComponent::MakeDamageRequest(10, Source, nullptr), FVector::ForwardVector, 1000, Radius, Lifetime))) return false;
		Shot->FinishSpawning(Transform);
		Shot->Tick(.9f);
		TestEqual(TEXT("Actual radius and lifetime decide whether target is reached"), Target->GetCurrentHealth(), Scenario == 0 ? 90.f : 100.f);
		TestTrue(TEXT("Directional shot resolves or expires once"), Shot->IsActorBeingDestroyed());
		if (Scenario == 1) TestTrue(TEXT("Configured radius catches grazing physical cover"), Shot->GetActorLocation().X < 400);
		if (Scenario == 2) TestEqual(TEXT("Lifetime stops flight before the wall without extra travel"), Shot->GetActorLocation().X, 200.0, .01);
		Fixture.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSAttackPositionCoverTest, "Landship.Combat.Projectile.AttackPositionCover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSAttackPositionCoverTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	ALSBasicAttackTestActor* Source = SpawnCoverCombatant(World, ELSFaction::Ally, 0);
	ALSBasicAttackTestActor* Target = SpawnCoverCombatant(World, ELSFaction::Enemy, 600);
	APawn* TransientPawn = World->SpawnActor<APawn>();
	AddCoverBox(TransientPawn, FVector(200, 0, 100), LSCollisionChannels::Unit, ECR_Block);
	if (!Fixture.BeginPlayInTestWorld()) return false;
	FLSBasicAttackSettings Attack;
	Attack.Damage = 10;
	Attack.Range = 100;
	TestTrue(TEXT("Configure attack"), Source->Attack->ConfigureAttack(Attack));
	FLSProjectileAttackSettings Projectile;
	Projectile.ProjectileClass = ALSBasicProjectile::StaticClass();
	TestTrue(TEXT("Configure projectile"), Source->Attack->ConfigureProjectile(Projectile));
	TestTrue(TEXT("Planning ignores transient Pawn and does not claim range"),
		Source->Attack->HasClearAttackPathFromLocation(Source->GetActorLocation(), Target));
	UBoxComponent* Wall = AddCoverBox(World->SpawnActor<AActor>(), FVector(400, 0, 100), ECC_WorldStatic, ECR_Block);
	TestFalse(TEXT("Physical wall blocks candidate firing position"),
		Source->Attack->HasClearAttackPathFromLocation(Source->GetActorLocation(), Target));
	TestTrue(TEXT("Lateral candidate can fire around wall"),
		Source->Attack->HasClearAttackPathFromLocation(FVector(0, 500, 100), Target));
	Wall->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Ignore);
	TestTrue(TEXT("Ignored helper geometry does not block candidate"),
		Source->Attack->HasClearAttackPathFromLocation(Source->GetActorLocation(), Target));
	Wall->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	TestTrue(TEXT("Configure instant attack"), Source->Attack->ConfigureProjectile(FLSProjectileAttackSettings()));
	TestTrue(TEXT("Instant attacks keep existing geometric behavior"),
		Source->Attack->HasClearAttackPathFromLocation(Source->GetActorLocation(), Target));
	TestFalse(TEXT("Invalid target rejected"), Source->Attack->HasClearAttackPathFromLocation(Source->GetActorLocation(), nullptr));
	Fixture.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSAttackPositionFriendlyBodiesTest, "Landship.Combat.Projectile.AttackPositionFriendlyBodies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSAttackPositionFriendlyBodiesTest::RunTest(const FString& Parameters)
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bOriginalFriendlyFire = Settings->bAllowFriendlyFire;
	ON_SCOPE_EXIT { Settings->bAllowFriendlyFire = bOriginalFriendlyFire; };
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	ALSBasicAttackTestActor* Source = SpawnCoverCombatant(World, ELSFaction::Enemy, 0);
	ALSBasicAttackTestActor* Target = SpawnCoverCombatant(World, ELSFaction::Ally, 600);
	ALSBarricade* Barricade = World->SpawnActor<ALSBarricade>();
	Barricade->SetActorLocation(FVector(300, 0, 100));
	if (!Fixture.BeginPlayInTestWorld()) return false;
	FLSBasicAttackSettings Attack;
	Attack.Damage = 10;
	Attack.Range = 1000;
	TestTrue(TEXT("Configure attack"), Source->Attack->ConfigureAttack(Attack));
	FLSProjectileAttackSettings Projectile;
	Projectile.ProjectileClass = ALSBasicProjectile::StaticClass();
	TestTrue(TEXT("Configure projectile"), Source->Attack->ConfigureProjectile(Projectile));
	Settings->bAllowFriendlyFire = false;
	TestTrue(TEXT("Own barricade passes planning when friendly damage is disabled"),
		Source->Attack->HasClearAttackPathFromLocation(Source->GetActorLocation(), Target));
	Settings->bAllowFriendlyFire = true;
	TestFalse(TEXT("Own barricade blocks planning when friendly damage is enabled"),
		Source->Attack->HasClearAttackPathFromLocation(Source->GetActorLocation(), Target));
	Barricade->SetActorLocation(FVector(300, 1000, 100));
	ALSAttackPathTestPawn* Pawn = World->SpawnActor<ALSAttackPathTestPawn>();
	Pawn->Faction = ELSFaction::Enemy;
	AddCoverBox(Pawn, FVector(300, 0, 100), LSCollisionChannels::Unit, ECR_Block);
	ULSHealthComponent* Health = NewObject<ULSHealthComponent>(Pawn);
	Pawn->AddInstanceComponent(Health);
	Health->RegisterComponent();
	TestTrue(TEXT("Unready combat Pawn passes planning"),
		Source->Attack->HasClearAttackPathFromLocation(Source->GetActorLocation(), Target));
	TestTrue(TEXT("Initialize Pawn health"), Health->InitializeHealth(ELSCombatStatsInitResult::Success, 100,
		FLSPrepareCombatDeath::CreateLambda([] {}), FString()));
	Settings->bAllowFriendlyFire = false;
	TestTrue(TEXT("Own Pawn passes planning when friendly damage is disabled"),
		Source->Attack->HasClearAttackPathFromLocation(Source->GetActorLocation(), Target));
	Settings->bAllowFriendlyFire = true;
	TestFalse(TEXT("Own Pawn blocks planning when friendly damage is enabled"),
		Source->Attack->HasClearAttackPathFromLocation(Source->GetActorLocation(), Target));
	Pawn->Faction = ELSFaction::Ally;
	TestTrue(TEXT("Hostile Pawn remains a transient planning occluder"),
		Source->Attack->HasClearAttackPathFromLocation(Source->GetActorLocation(), Target));
	Pawn->Faction = ELSFaction::Enemy;
	Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(100, nullptr, nullptr));
	TestTrue(TEXT("Dead Pawn does not become planning cover"),
		Source->Attack->HasClearAttackPathFromLocation(Source->GetActorLocation(), Target));
	Fixture.ForwardErrorMessages(this);
	return true;
}

#endif
