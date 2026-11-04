#if WITH_DEV_AUTOMATION_TESTS

#include "Maps/LSBattlefieldEncounterCoordinator.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Enemies/Tests/LSEnemySupportTestAccess.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Obstacles/LSMine.h"
#include "Troops/LSTroopUnit.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

namespace
{
template <typename T> T* SpawnRegional(UWorld* World, FVector Position)
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Position, FRotator::ZeroRotator, Parameters);
}

void AdvanceRegional(UWorld* World)
{
	++GFrameCounter;
	World->Tick(LEVELTICK_All, .01f);
}

ULSEnemyBehaviorComponent* AddRegionalBehavior(AActor* Actor, bool bDormant)
{
	auto* Behavior = NewObject<ULSEnemyBehaviorComponent>(Actor);
	Behavior->Role = ELSEnemyRole::Assault;
	Behavior->bStartRegionallyDormant = bDormant;
	Behavior->PrimaryComponentTick.bStartWithTickEnabled = false;
	Actor->AddInstanceComponent(Behavior);
	Behavior->RegisterComponent();
	return Behavior;
}

struct FLSRegionalFixture
{
	ALSBattlefieldEncounterCoordinator* Coordinator;
	ALSBasicAttackTestActor* Source;
	ALSBasicAttackTestActor* EnemySource;
	ALSBasicAttackTestActor* Reporter;
	ALSTroopUnit* Worker;
	ALSBasicAttackTestActor* Guard;
	FLSRegionalFixture(UWorld* World)
	{
		Coordinator = SpawnRegional<ALSBattlefieldEncounterCoordinator>(World, FVector::ZeroVector);
		const TArray<FName> Ids = {TEXT("L1"), TEXT("L2"), TEXT("C1"), TEXT("C2"), TEXT("R1"), TEXT("R2"), TEXT("Core"), TEXT("CentralGuardOnly")};
		const TArray<int32> MemberCounts = {6, 6, 18, 18, 12, 12, 12, 0};
		for (int32 Index = 0; Index < Ids.Num(); ++Index)
		{
			FLSEncounterRegion Region;
			Region.RegionId = Ids[Index];
			const float X = Index * 10000;
			Region.EntryBounds.Add(FBox(FVector(X - 1000, -3000, -100), FVector(X + 1000, -1000, 100)));
			Region.PlacementBounds.Add(FBox(FVector(X - 4500, -4500, -1000), FVector(X + 4500, 4500, 1000)));
			Region.AssaultPlacementBounds.Add(FBox(FVector(X - 1000, 1000, -1000), FVector(X + 2000, 3000, 1000)));
			for (int32 Member = 0; Member < MemberCounts[Index]; ++Member)
			{
				auto* Actor = SpawnRegional<ALSBasicAttackTestActor>(World, FVector(X + Member * 100, 2000, 100));
				Actor->SetInitialFaction(ELSFaction::Enemy);
				Actor->Attack->SetAutoAttackOnBeginPlay(false);
				AddRegionalBehavior(Actor, true);
				Region.AssaultMembers.Add(Actor);
			}
			auto* Alarm = SpawnRegional<ALSBasicAttackTestActor>(World, FVector(X, 0, 100));
			Alarm->SetInitialFaction(ELSFaction::Enemy);
			Alarm->Attack->SetAutoAttackOnBeginPlay(false);
			Region.AlarmTargets.Add(Alarm);
			Region.Mines.Add(SpawnRegional<ALSMine>(World, FVector(X, 3500, 100)));
			Coordinator->Regions.Add(Region);
		}
		Source = SpawnRegional<ALSBasicAttackTestActor>(World, FVector(-20000, 0, 100));
		Source->Attack->SetAutoAttackOnBeginPlay(false);
		EnemySource = SpawnRegional<ALSBasicAttackTestActor>(World, FVector(-21000, 0, 100));
		EnemySource->SetInitialFaction(ELSFaction::Enemy);
		EnemySource->Attack->SetAutoAttackOnBeginPlay(false);
		Reporter = SpawnRegional<ALSBasicAttackTestActor>(World, FVector(10000, 2200, 100));
		Reporter->SetInitialFaction(ELSFaction::Enemy);
		Reporter->Attack->SetAutoAttackOnBeginPlay(false);
		AddRegionalBehavior(Reporter, false);
		Worker = SpawnRegional<ALSTroopUnit>(World, FVector(-25000, 0, 100));
		Worker->BasicAttack->SetAutoAttackOnBeginPlay(false);
		Worker->Orders->SetComponentTickEnabled(false);
		Worker->SetActorEnableCollision(false);
		auto* Castle = LSCastleTestFixture::Spawn(World, FVector(-100000, 0, 100));
		auto* Setup = SpawnRegional<ALSBattlefieldSetup>(World, FVector::ZeroVector);
		Setup->PlayerCastle = Castle;
		Setup->CoreOutpost = SpawnRegional<ALSEnemyOutpost>(World, FVector(100000, 100000, 100));
		Setup->CoreOutpost->BasicAttack->SetAutoAttackOnBeginPlay(false);
		Guard = Cast<ALSBasicAttackTestActor>(Coordinator->Regions[7].AlarmTargets[0]);
		auto* GuardBehavior = AddRegionalBehavior(Guard, false);
		GuardBehavior->Role = ELSEnemyRole::Guard;
		GuardBehavior->GuardAnchor = SpawnRegional<ATargetPoint>(World, FVector(70000, 0, 100));
		GuardBehavior->GuardAnchorRadius = 3000;
	}
	ULSEnemyBehaviorComponent* Behavior(int32 Region, int32 Member = 0) const
	{
		return Coordinator->Regions[Region].AssaultMembers[Member]->FindComponentByClass<ULSEnemyBehaviorComponent>();
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSRegionalEncounterValidationTest,
	"Landship.Maps.RegionalEncounter.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSRegionalEncounterValidationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	FLSRegionalFixture Fixture(World);
	auto* Coordinator = Fixture.Coordinator;
	TestTrue(TEXT("eight regions accept asymmetric non-empty Assault memberships"), Coordinator->GetConfigurationError().IsEmpty());
	TestFalse(TEXT("ordinary enemy behavior remains active by default"), NewObject<ULSEnemyBehaviorComponent>()->bStartRegionallyDormant);
	const auto SavedMembers = Coordinator->Regions[0].AssaultMembers;
	Coordinator->Regions[0].AssaultMembers.Reset();
	TestTrue(TEXT("noncentral empty Assault membership rejected"), Coordinator->GetConfigurationError().Contains(TEXT("non-empty")));
	Coordinator->Regions[0].AssaultMembers = SavedMembers;
	auto Member = Coordinator->Regions[0].AssaultMembers.Last();
	Coordinator->Regions[7].AssaultMembers.Add(Member);
	TestTrue(TEXT("central guard-only region rejects Assault membership"), Coordinator->GetConfigurationError().Contains(TEXT("CentralGuardOnly")));
	Coordinator->Regions[7].AssaultMembers.Reset();
	auto Other = Coordinator->Regions[1].AssaultMembers[0];
	Coordinator->Regions[1].AssaultMembers[0] = Member;
	TestFalse(TEXT("cross-region duplicate rejected"), Coordinator->GetConfigurationError().IsEmpty());
	Coordinator->Regions[1].AssaultMembers[0] = Other;
	TMap<FName, FTransform> Preset;
	FString Error;
	Preset.Add(Member->GetFName(), FTransform(FVector(4000, 2000, 100)));
	TestFalse(TEXT("Assault clearance envelope enforced"), Coordinator->ValidatePresetTransforms(Preset, Error));
	Preset.Reset();
	Preset.Add(Fixture.Guard->GetFName(), FTransform(FVector(74000, 0, 100)));
	TestFalse(TEXT("anchor guard radius enforced on proposed pose"), Coordinator->ValidatePresetTransforms(Preset, Error));
	Preset.Reset();
	const FTransform Original = Member->GetActorTransform();
	Member->SetActorLocation(FVector(90000, 90000, 100));
	Preset.Add(Member->GetFName(), Original);
	TestTrue(TEXT("valid proposed pose can repair an invalid current mixed layout"), Coordinator->ValidatePresetTransforms(Preset, Error));
	Member->SetActorTransform(Original);
	auto* Orphan = SpawnRegional<ALSBasicAttackTestActor>(World, FVector(-30000, 0, 100));
	AddRegionalBehavior(Orphan, true);
	TestFalse(TEXT("orphan dormant actor rejected"), Coordinator->GetConfigurationError().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSRegionalEncounterActivationTest,
	"Landship.Maps.RegionalEncounter.Activation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSRegionalEncounterActivationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	FLSRegionalFixture Fixture(World);
	auto* Coordinator = Fixture.Coordinator;
	if (!TestWorld.BeginPlayInTestWorld()) return false;
	for (const auto& Region : Coordinator->Regions)
		for (AActor* Actor : Region.AssaultMembers)
		{
			TestTrue(TEXT("dormant actor keeps collision"), Actor->GetActorEnableCollision());
			TestFalse(TEXT("dormant actor keeps visibility"), Actor->IsHidden());
			TestTrue(TEXT("all 84 Assaults start dormant"), Actor->FindComponentByClass<ULSEnemyBehaviorComponent>()->IsRegionallyDormant());
		}
	auto* L2Alarm = Coordinator->Regions[1].AlarmTargets[0]->FindComponentByClass<ULSHealthComponent>();
	L2Alarm->ApplyDamage(ULSHealthComponent::MakeDamageRequest(0, Fixture.Source, Fixture.Source));
	FLSDamageRequest Friendly = ULSHealthComponent::MakeDamageRequest(1, Fixture.EnemySource, Fixture.EnemySource);
	Friendly.Policy = ELSDamagePolicy::EnvironmentalHazard;
	TestEqual(TEXT("Enemy-origin damage really commits"), L2Alarm->ApplyDamage(Friendly).AppliedDamage, 1.f);
	Fixture.Reporter->ApplyTestDamage(1, Fixture.Source);
	AdvanceRegional(World);
	TestFalse(TEXT("zero, Enemy damage and generic support do not activate"), Coordinator->IsRegionActivated(TEXT("L2")));
	TestEqual(TEXT("dormant recipient does not queue generic support"), FLSEnemySupportTestAccess::Pending(Fixture.Behavior(1)), 0);
	TestNull(TEXT("dormant actor cannot resolve an attack target"), FLSEnemySupportTestAccess::StrikeTarget(Fixture.Behavior(1)));

	auto* L1Alarm = Cast<ALSBasicAttackTestActor>(Coordinator->Regions[0].AlarmTargets[0]);
	L1Alarm->HealthComponent->bDestroyOnDeath = true;
	L1Alarm->HealthComponent->DestroyDelay = 0;
	TestEqual(TEXT("lethal Ally damage really commits"), L1Alarm->ApplyTestDamage(100, Fixture.Source).AppliedDamage, 100.f);
	Fixture.Source->Destroy();
	TestFalse(TEXT("damage activation deferred out of health mutation"), Coordinator->IsRegionActivated(TEXT("L1")));
	AdvanceRegional(World);
	TestTrue(TEXT("receiver/source destruction preserves committed Ally alarm"), Coordinator->IsRegionActivated(TEXT("L1")));
	TestFalse(TEXT("neighbor remains dormant"), Coordinator->IsRegionActivated(TEXT("L2")));
	TestFalse(TEXT("own members activated"), Fixture.Behavior(0)->IsRegionallyDormant());
	TestTrue(TEXT("activation does not decide immediately"), Fixture.Behavior(0)->GetState() == ELSEnemyState::WaitingForSetup);

	Fixture.EnemySource->SetActorLocation(FVector(10000, -2000, 100));
	AdvanceRegional(World);
	TestFalse(TEXT("Enemy root entry ignored"), Coordinator->IsRegionActivated(TEXT("L2")));
	Fixture.Worker->SetActorLocation(FVector(10000, -2000, 10000));
	AdvanceRegional(World);
	TestTrue(TEXT("alive Ally root XY entry ignores height"), Coordinator->IsRegionActivated(TEXT("L2")));

	ALSMine* Exploding = Coordinator->Regions[2].Mines[0];
	int32 Explosions = 0;
	Exploding->OnMineExploded.AddLambda([&Explosions](const FLSMineExplosion&) { ++Explosions; });
	TestTrue(TEXT("victimless mine actually explodes"), Exploding->RequestTrigger(ELSMineTriggerCause::DirectDamage));
	TestFalse(TEXT("second trigger rejected"), Exploding->RequestTrigger(ELSMineTriggerCause::DirectDamage));
	Exploding->Destroy();
	AdvanceRegional(World);
	TestTrue(TEXT("destroyed victimless mine activates own region"), Coordinator->IsRegionActivated(TEXT("C1")));
	TestEqual(TEXT("one actual explosion record"), Explosions, 1);
	TestFalse(TEXT("mine event does not spread to next region"), Coordinator->IsRegionActivated(TEXT("C2")));
	ALSMine* Disarmed = Coordinator->Regions[3].Mines[0];
	TestTrue(TEXT("worker claims mine"), Disarmed->TryClaimDisarm(Fixture.Worker));
	TestTrue(TEXT("worker completes disarm"), Disarmed->TryCompleteDisarm(Fixture.Worker));
	AdvanceRegional(World);
	TestFalse(TEXT("disarm does not activate region"), Coordinator->IsRegionActivated(TEXT("C2")));

	AActor* DeadMember = Coordinator->Regions[4].AssaultMembers[0].Get();
	FLSDamageRequest Kill = ULSHealthComponent::MakeDamageRequest(100, Fixture.Worker, Fixture.Worker);
	DeadMember->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(Kill);
	Fixture.Behavior(4, 1)->EndBattleActions();
	AdvanceRegional(World);
	TestTrue(TEXT("damaged member activates assigned region"), Coordinator->IsRegionActivated(TEXT("R1")));
	TestTrue(TEXT("dead member never wakes"), Fixture.Behavior(4)->IsRegionallyDormant());
	TestTrue(TEXT("ended member never wakes"), Fixture.Behavior(4, 1)->IsRegionallyDormant());
	TestFalse(TEXT("other living members activate"), Fixture.Behavior(4, 2)->IsRegionallyDormant());
	return true;
}
#endif
