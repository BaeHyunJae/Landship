#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LSHealthComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Combat/Tests/LSHealthTestObserver.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSDamageRecordDeferredTest, "Landship.Combat.DamageRecords.DeferredCommitAndDestroyedReceiver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSDamageRecordDeferredTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	AActor* Receiver = World->SpawnActor<AActor>();
	ALSHealthTestActor* Source = World->SpawnActor<ALSHealthTestActor>();
	Source->Faction = ELSFaction::Enemy;
	ULSHealthComponent* Health = NewObject<ULSHealthComponent>(Receiver);
	Receiver->AddInstanceComponent(Health);
	Health->RegisterComponent();
	bool bPrepared = false;
	int32 CommittedCount = 0;
	TestTrue(TEXT("Initialize health"), Health->InitializeHealth(ELSCombatStatsInitResult::Success, 100,
		FLSPrepareCombatDeath::CreateLambda([&]
		{
			TestEqual(TEXT("No new callback before mandatory death hook"), CommittedCount, 1);
			bPrepared = true;
			Receiver->Destroy();
		}), FString()));
	FLSDamageResult Last;
	Last.AppliedDamage = 987;
	TestFalse(TEXT("No record before first damage"), Health->TryGetLastDamage(Last));
	TestEqual(TEXT("Failed record lookup preserves output"), Last.AppliedDamage, 987.0f);
	TestFalse(TEXT("No damage observed before commit"), Health->HasTakenDamage());
	const int64 ReceiverId = Health->GetDamageReceiverId();
	TArray<FLSDamageResult> Records;
	ULSHealthTestObserver* SurvivingSubscriber = NewObject<ULSHealthTestObserver>(World);
	Health->OnDamageCommitted.AddWeakLambda(SurvivingSubscriber, [&](const FLSDamageResult& Damage)
	{
		++CommittedCount;
		Records.Add(Damage);
	});
	FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(0, Source, Source);
	Health->ApplyDamage(Request);
	TestFalse(TEXT("Zero damage does not reveal health"), Health->HasTakenDamage());
	Request.DamageAmount = 25;
	Request.bHasAttackOrigin = true;
	Request.AttackOrigin = FVector(321, 54, 6);
	Request.bHasImpactLocation = true;
	Request.ImpactLocation = FVector(900, 54, 6);
	Request.bHasIncomingDirection = true;
	Request.IncomingDirection = FVector::ForwardVector;
	Health->ApplyDamage(Request);
	TestEqual(TEXT("Committed callback is deferred"), CommittedCount, 0);
	TestTrue(TEXT("Positive commit reveals health immediately"), Health->HasTakenDamage());
	TestTrue(TEXT("Positive commit stores record"), Health->TryGetLastDamage(Last));
	TestEqual(TEXT("Previous health frozen"), Last.PreviousHealth, 100.0f);
	TestEqual(TEXT("Max health frozen"), Last.MaxHealth, 100.0f);
	TestEqual(TEXT("Post damage health frozen"), Last.CurrentHealth, 75.0f);
	TestTrue(TEXT("Stored record has no strong source"), Last.SourceActor == nullptr && Last.DamageCauser == nullptr);
	++GFrameCounter;
	World->GetTimerManager().Tick(0.01f);
	TestEqual(TEXT("Nonlethal callback dispatches once"), CommittedCount, 1);
	Request.DamageAmount = 500;
	Health->ApplyDamage(Request);
	TestTrue(TEXT("Mandatory death hook ran"), bPrepared);
	TestEqual(TEXT("Lethal callback is still deferred after owner destruction"), CommittedCount, 1);
	Source->Destroy();
	Request.SourceActor = nullptr;
	Request.DamageCauser = nullptr;
	++GFrameCounter;
	World->GetTimerManager().Tick(0.01f);
	TestEqual(TEXT("Surviving subscriber receives destroyed receiver commit"), Records.Num(), 2);
	if (Records.Num() == 2)
	{
		const FLSDamageResult& Damage = Records[1];
		TestEqual(TEXT("Stable key survives receiver destruction"), Damage.ReceiverId, ReceiverId);
		TestFalse(TEXT("Receiver is weak"), Damage.Receiver.IsValid());
		TestFalse(TEXT("Destroyed source is weak"), Damage.Source.IsValid());
		TestTrue(TEXT("Source faction remains frozen"), Damage.bHasSourceFaction && Damage.SourceFaction == ELSFaction::Enemy);
		TestTrue(TEXT("Origin survives source destruction"), Damage.bHasAttackOrigin && Damage.AttackOrigin == FVector(321, 54, 6));
		TestEqual(TEXT("Lethal snapshot clamps damage"), Damage.AppliedDamage, 75.0f);
		TestEqual(TEXT("Lethal snapshot records previous health"), Damage.PreviousHealth, 75.0f);
	}
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSDamageRecordUnknownOriginTest, "Landship.Combat.DamageRecords.UnknownOriginAndProjectileSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSDamageRecordUnknownOriginTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSBasicAttackTestActor* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Target = World->SpawnActor<ALSBasicAttackTestActor>();
	Target->SetInitialFaction(ELSFaction::Enemy);
	Source->SetActorLocation(FVector(0, 0, 100));
	Target->SetActorLocation(FVector(300, 0, 100));
	if (!TestWorld.BeginPlayInTestWorld()) return false;
	Target->HealthComponent->ApplyDamage(ULSHealthComponent::MakeDamageRequest(1, nullptr, nullptr));
	FLSDamageResult Record;
	TestTrue(TEXT("Direct damage is recorded"), Target->HealthComponent->TryGetLastDamage(Record));
	TestFalse(TEXT("Unknown direct origin is not invented"), Record.bHasAttackOrigin);
	const FVector Origin(0, 0, 100);
	const FTransform Transform(FRotator::ZeroRotator, Origin);
	ALSBasicProjectile* Shot = World->SpawnActorDeferred<ALSBasicProjectile>(ALSBasicProjectile::StaticClass(), Transform);
	Shot->SetVisualStyle(ELSProjectileVisualStyle::TurretBolt);
	FLSDamageRequest Request = ULSHealthComponent::MakeDamageRequest(10, Source, nullptr);
	TestTrue(TEXT("Launch projectile"), Shot->Launch(Request, Target, FVector::ForwardVector, 1000, 6, 2));
	Shot->FinishSpawning(Transform);
	TestEqual(TEXT("Typed visual style retained"), Shot->GetVisualStyle(), ELSProjectileVisualStyle::TurretBolt);
	TInlineComponentArray<UStaticMeshComponent*> Meshes(Shot);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		TestFalse(TEXT("Projectile visuals never affect navigation"), Mesh->CanEverAffectNavigation());
		TestEqual(TEXT("Projectile visuals have no collision"), Mesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	}
	Source->Destroy();
	Request.SourceActor = nullptr;
	for (int32 Index = 0; Index < 50; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.01f);
	}
	TestTrue(TEXT("Projectile hit recorded after shooter destruction"), Target->HealthComponent->TryGetLastDamage(Record));
	TestEqual(TEXT("Projectile damage remains unchanged"), Record.AppliedDamage, 10.0f);
	TestTrue(TEXT("Actual launch origin preserved"), Record.bHasAttackOrigin && Record.AttackOrigin == Origin);
	TestTrue(TEXT("Actual incoming flight preserved"), Record.bHasIncomingDirection && Record.IncomingDirection == FVector::ForwardVector);
	TestTrue(TEXT("Impact point is explicit"), Record.bHasImpactLocation);
	TestTrue(TEXT("Shooter faction preserved"), Record.bHasSourceFaction && Record.SourceFaction == ELSFaction::Ally);
	TestFalse(TEXT("Destroyed shooter is not retained"), Record.Source.IsValid());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSDamageRecordHiddenSnapshotTest, "Landship.Combat.DamageRecords.HiddenReceiverSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSDamageRecordHiddenSnapshotTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Default damage record is visible"), FLSDamageResult().bReceiverHiddenAtCommit);
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	AActor* Receiver = World->SpawnActor<AActor>();
	ULSHealthComponent* Health = NewObject<ULSHealthComponent>(Receiver);
	Receiver->AddInstanceComponent(Health);
	Health->RegisterComponent();
	ULSHealthComponent* OtherHealth = NewObject<ULSHealthComponent>(World->SpawnActor<AActor>());
	TestTrue(TEXT("Receiver key is assigned before initialization"), Health->GetDamageReceiverId() > 0);
	TestNotEqual(TEXT("Instances constructed from the CDO have distinct keys"),
		Health->GetDamageReceiverId(), OtherHealth->GetDamageReceiverId());
	TestNotEqual(TEXT("Instance key is not the CDO key"),
		Health->GetDamageReceiverId(), GetDefault<ULSHealthComponent>()->GetDamageReceiverId());
	TestTrue(TEXT("Initialize health"), Health->InitializeHealth(ELSCombatStatsInitResult::Success, 100,
		FLSPrepareCombatDeath::CreateLambda([] {}), FString()));
	ULSHealthTestObserver* Subscriber = NewObject<ULSHealthTestObserver>(World);
	TArray<FLSDamageResult> Records;
	Health->OnDamageCommitted.AddWeakLambda(Subscriber, [&](const FLSDamageResult& Damage) { Records.Add(Damage); });
	Receiver->SetActorHiddenInGame(false);
	Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(1, nullptr, nullptr));
	Receiver->SetActorHiddenInGame(true);
	Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(2, nullptr, nullptr));
	FLSDamageResult Last;
	TestTrue(TEXT("Read hidden commit"), Health->TryGetLastDamage(Last));
	TestTrue(TEXT("Hidden state captured at positive commit"), Last.bReceiverHiddenAtCommit);
	Receiver->SetActorHiddenInGame(false);
	TestTrue(TEXT("Read record after actor becomes visible"), Health->TryGetLastDamage(Last));
	TestTrue(TEXT("Stored hidden state does not follow actor visibility"), Last.bReceiverHiddenAtCommit);
	Health->OnDamageCommitted.RemoveAll(Subscriber);
	++GFrameCounter;
	World->GetTimerManager().Tick(0.01f);
	TestEqual(TEXT("Copied listeners still receive queued records after RemoveAll"), Records.Num(), 2);
	const FLSDamageResult* VisibleCommit = Records.FindByPredicate(
		[](const FLSDamageResult& Damage) { return Damage.AppliedDamage == 1; });
	const FLSDamageResult* HiddenCommit = Records.FindByPredicate(
		[](const FLSDamageResult& Damage) { return Damage.AppliedDamage == 2; });
	TestTrue(TEXT("Visible commit remains visible in deferred snapshot"), VisibleCommit && !VisibleCommit->bReceiverHiddenAtCommit);
	TestTrue(TEXT("Hidden commit remains hidden in deferred snapshot"), HiddenCommit && HiddenCommit->bReceiverHiddenAtCommit);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
