#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellEffectInstance.h"
#include "ShellExperiment/LSShellDetonationCue.h"
#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellExperimentEnemy.h"
#include "ShellExperiment/LSShellExperimentGameMode.h"
#include "ShellExperiment/LSShellExplosive.h"
#include "ShellExperiment/LSShellProjectile.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "ShellExperiment/LSShellWeakPoint.h"
#include "ShellExperiment/LSShellField.h"
#include "ShellExperiment/LSShellPhysicalProp.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Components/PrimitiveComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSOutpost.h"
#include "Engine/World.h"
#include "Engine/EngineBaseTypes.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Match/LSGameState.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Tests/AutomationCommon.h"

namespace
{
struct FLSShellTestDamagePolicy
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	bool bFriendlyFire = Settings->bAllowFriendlyFire;
	bool bSelfDamage = Settings->bAllowSelfDamage;
	FLSShellTestDamagePolicy()
	{
		Settings->bAllowFriendlyFire = true;
		Settings->bAllowSelfDamage = true;
	}
	~FLSShellTestDamagePolicy()
	{
		Settings->bAllowFriendlyFire = bFriendlyFire;
		Settings->bAllowSelfDamage = bSelfDamage;
	}
};

template <typename T>
T* SpawnShellActor(UWorld* World, FVector Location)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Spawn);
}

void TickShellWorld(UWorld* World, float Delta = 0.01f)
{
	++GFrameCounter;
	World->Tick(LEVELTICK_All, Delta);
}

float ShellHealth(AActor* Actor)
{
	FLSHealthSnapshot Snapshot;
	const ULSHealthComponent* Health = Actor ? Actor->FindComponentByClass<ULSHealthComponent>() : nullptr;
	return Health && Health->TryGetHealth(Snapshot) ? Snapshot.CurrentHealth : -1;
}

ALSShellExperimentCoordinator* SetUpShellWorld(UWorld* World, FAutomationTestBase& Test)
{
	World->GetWorldSettings()->DefaultGameMode = ALSShellExperimentGameMode::StaticClass();
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(-5000, 0, 200));
	if (!Test.TestNotNull(TEXT("Native castle with production stat row"), Castle)) return nullptr;
	Castle->GetMovementComponent()->Deactivate();
	UClass* CoreClass = LoadClass<ALSOutpost>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Blueprints/BP_ShellCore.BP_ShellCore_C"));
	if (!Test.TestNotNull(TEXT("Experiment core Blueprint"), CoreClass)) return nullptr;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALSOutpost* Core = World->SpawnActor<ALSOutpost>(CoreClass, FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
	if (!Test.TestNotNull(TEXT("Core"), Core)) return nullptr;
	ALSBattlefieldSetup* Setup = SpawnShellActor<ALSBattlefieldSetup>(World, FVector::ZeroVector);
	Setup->PlayerCastle = Castle;
	Setup->CoreOutpost = Core;
	ALSShellExperimentCoordinator* Coordinator = SpawnShellActor<ALSShellExperimentCoordinator>(World, FVector::ZeroVector);
	Coordinator->PlayerCastle = Castle;
	Coordinator->CoreOutpost = Core;
	return Coordinator;
}

bool BeginShellWorld(FTestWorldWrapper& TestWorld, FAutomationTestBase& Test, ALSShellExperimentCoordinator* Coordinator)
{
	if (!Test.TestTrue(TEXT("BeginPlay through actual GameMode"), TestWorld.BeginPlayInTestWorld())) return false;
	const ALSShellExperimentGameMode* Mode = TestWorld.GetTestWorld()->GetAuthGameMode<ALSShellExperimentGameMode>();
	if (!Test.TestNotNull(TEXT("Experiment GameMode"), Mode)) return false;
	if (!Test.TestTrue(TEXT("Configuration valid before actors begin"), Mode->IsConfigurationValid())) return false;
	if (!Test.TestTrue(TEXT("Coordinator active"), Coordinator->IsBattleActive())) return false;
	if (!Test.TestTrue(TEXT("Native castle health initialized"), ShellHealth(Coordinator->PlayerCastle) > 0)) return false;
	return Test.TestTrue(TEXT("Native core health initialized"), ShellHealth(Coordinator->CoreOutpost) > 0);
}

ALSShellExperimentEnemy* SpawnConfiguredShellEnemy(UWorld* World, FVector Location, ELSShellEnemyKind Kind)
{
	const FTransform Transform(Location);
	auto* Enemy = World->SpawnActorDeferred<ALSShellExperimentEnemy>(ALSShellExperimentEnemy::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Enemy) return nullptr;
	Enemy->EnemyKind = Kind;
	Enemy->StatsTable = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Data/DT_ShellEnemyStats.DT_ShellEnemyStats"));
	Enemy->FinishSpawning(Transform);
	return Enemy;
}

template <typename T>
int32 CountShellActors(UWorld* World)
{
	int32 Count = 0;
	for (TActorIterator<T> It(World); It; ++It) ++Count;
	return Count;
}

void TickShellAtTime(UWorld* World, double Time)
{
	World->TimeSeconds = Time - 0.01;
	TickShellWorld(World);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellCoreArmorTest, "Landship.ShellExperiment.CoreArmorAndNativeDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellCoreArmorTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->Tuning.ModuleDelay = 100;
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	const float Initial = ShellHealth(Coordinator->CoreOutpost);
	if (!TestTrue(TEXT("FireAt accepts loaded shell"), Coordinator->FireAt(Coordinator->CoreOutpost->GetActorLocation()))) return false;
	for (int32 Step = 0; Step < 20; ++Step) TickShellWorld(World);
	const float ReloadBeforeEditing = Coordinator->GetReloadFraction();
	Coordinator->SetComposing(true);
	Coordinator->SwapModules();
	Coordinator->SetComposing(false);
	TestEqual(TEXT("Composition changes next shot during flight"), Coordinator->GetModule(0), ELSShellModule::Blast);
	TestEqual(TEXT("Composition does not reset existing reload progress"), Coordinator->GetReloadFraction(), ReloadBeforeEditing);
	for (int32 Step = 0; Step < 300 && Coordinator->ShellHitCount == 0; ++Step) TickShellWorld(World);
	if (!TestEqual(TEXT("Actual shell flight reaches core"), Coordinator->ShellHitCount, 1)) return false;
	TestEqual(TEXT("Direct shell damage reduced by half"), ShellHealth(Coordinator->CoreOutpost), Initial - 25);
	for (TActorIterator<ALSShellEffectInstance> It(World); It; ++It)
		TestEqual(TEXT("In-flight shot retains original second module"), It->GetPendingModule(), ELSShellModule::Blast);
	FLSShellShot Shot;
	Shot.Source = Coordinator->PlayerCastle;
	Shot.Origin = Coordinator->PlayerCastle->GetActorLocation();
	Coordinator->ExecuteModule(ELSShellModule::Blast, Coordinator->CoreOutpost->GetActorLocation(), Shot, FHitResult());
	TickShellWorld(World);
	TestEqual(TEXT("Module blast reduced by half"), ShellHealth(Coordinator->CoreOutpost), Initial - 25 - 75);
	Coordinator->ApplyExperimentDamage(Coordinator->CoreOutpost, 40, Shot, Coordinator->CoreOutpost->GetActorLocation(),
		FVector::ForwardVector, ELSShellDamageCause::Impact);
	TestEqual(TEXT("Secondary impact bypasses shell armor"), ShellHealth(Coordinator->CoreOutpost), Initial - 25 - 75 - 40);
	TestEqual(TEXT("Committed damage counts native health reduction"), Coordinator->CoreDamageCommitted, 140.0f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellDetonationSnapshotTest, "Landship.ShellExperiment.DetonationSurvivesSourceRemovalOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellDetonationSnapshotTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	ALSShellExplosive* Explosive = SpawnShellActor<ALSShellExplosive>(World, Coordinator->CoreOutpost->GetActorLocation());
	Explosive->Reactive->DetonationDamage = 80;
	Explosive->Reactive->DetonationRadius = 500;
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	const float Initial = ShellHealth(Coordinator->CoreOutpost);
	if (!TestTrue(TEXT("First detonation accepted"), Explosive->RequestDetonation())) return false;
	TestFalse(TEXT("Repeated request rejected before queue drains"), Explosive->RequestDetonation());
	TestEqual(TEXT("Request does not apply immediate damage"), ShellHealth(Coordinator->CoreOutpost), Initial);
	Explosive->Reactive->DetonationDamage = 1;
	Explosive->SetActorLocation(FVector(10000, 0, 200));
	TWeakObjectPtr<ALSShellExplosive> RemovedSource = Explosive;
	Explosive->Destroy();
	TestFalse(TEXT("Source removed before queue drains"), RemovedSource.IsValid());
	TickShellWorld(World);
	TestEqual(TEXT("Copied detonation uses original position and full secondary damage"), ShellHealth(Coordinator->CoreOutpost), Initial - 80);
	TestEqual(TEXT("One detonation resolved"), Coordinator->DetonationCount, 1);
	TickShellWorld(World);
	TestEqual(TEXT("No repeated detonation on next frame"), Coordinator->DetonationCount, 1);
	TestEqual(TEXT("No repeated damage"), ShellHealth(Coordinator->CoreOutpost), Initial - 80);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellDelayedAnchorTest, "Landship.ShellExperiment.DelayedModuleSurvivesProjectileAndTargetRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellDelayedAnchorTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->Tuning.ModuleDelay = 0.5f;
	Coordinator->Tuning.DirectVelocity = 0;
	Coordinator->Tuning.DirectLift = 0;
	Coordinator->Tuning.GravityAcceleration = 0;
	Coordinator->Tuning.BlastDamage = 40;
	ALSShellExperimentEnemy* Enemy = SpawnShellActor<ALSShellExperimentEnemy>(World, FVector(4000, 0, 200));
	Enemy->StatsTable = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Data/DT_ShellEnemyStats.DT_ShellEnemyStats"));
	Enemy->StatsRowName = TEXT("Assault");
	if (!TestNotNull(TEXT("Production enemy stat table"), Enemy->StatsTable.Get())) return false;
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	Enemy->Behavior->SetComponentTickEnabled(false);
	Enemy->GetCharacterMovement()->DisableMovement();
	FLSShellShot Shot;
	Shot.Source = Coordinator->PlayerCastle;
	Shot.Origin = Enemy->GetActorLocation() - FVector(500, 0, 0);
	TWeakObjectPtr<ALSShellProjectile> Projectile = SpawnShellActor<ALSShellProjectile>(World, Shot.Origin);
	Projectile->Launch(Coordinator, Shot, Shot.Origin, FVector::ForwardVector);
	for (int32 Step = 0; Step < 50 && Coordinator->ShellHitCount == 0; ++Step) TickShellWorld(World);
	if (!TestEqual(TEXT("Actual swept projectile hits living enemy"), Coordinator->ShellHitCount, 1)) return false;
	TestFalse(TEXT("Original projectile removed at impact"), Projectile.IsValid());
	TestEqual(TEXT("Native direct damage applied before delayed effect"), ShellHealth(Enemy), 50.0f);
	TWeakObjectPtr<ALSShellEffectInstance> Effect;
	for (TActorIterator<ALSShellEffectInstance> It(World); It; ++It) Effect = *It;
	if (!TestTrue(TEXT("Independent delayed effect exists"), Effect.IsValid())) return false;
	const FVector InitialImpact = Coordinator->LastShellImpact;
	Enemy->SetActorLocation(Enemy->GetActorLocation() + FVector(0, 200, 0));
	TickShellWorld(World);
	const FVector FrozenImpact = InitialImpact + FVector(0, 200, 0);
	TestTrue(TEXT("Living target carries impact offset"), Effect->GetActorLocation().Equals(FrozenImpact, 0.1));
	ALSBasicAttackTestActor* Witness = SpawnShellActor<ALSBasicAttackTestActor>(World, FrozenImpact);
	TWeakObjectPtr<ALSShellExperimentEnemy> RemovedEnemy = Enemy;
	Enemy->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(50, Coordinator->PlayerCastle, Coordinator));
	TestFalse(TEXT("Native death removes target"), RemovedEnemy.IsValid());
	if (!TestTrue(TEXT("Effect survives target death"), Effect.IsValid())) return false;
	TestTrue(TEXT("Death preserves last world impact position"), Effect->GetActorLocation().Equals(FrozenImpact, 0.1));
	for (int32 Step = 0; Step < 70 && Coordinator->ModuleCount < 2; ++Step) TickShellWorld(World);
	TestEqual(TEXT("Delayed second module executes once"), Coordinator->ModuleCount, 2);
	TestEqual(TEXT("Delayed blast damages witness at preserved point"), Witness->GetCurrentHealth(), 60.0f);
	TestFalse(TEXT("Effect consumed after second module"), Effect.IsValid());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellFinalizationTest, "Landship.ShellExperiment.FinalizedOutcomeStopsNewReactions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellFinalizationTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator || !BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	Coordinator->SetComposing(true);
	TestTrue(TEXT("Composition opens"), Coordinator->IsComposing());
	TestEqual(TEXT("Composition applies world dilation"), UGameplayStatics::GetGlobalTimeDilation(World), 0.2f);
	Coordinator->PlayerCastle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(ShellHealth(Coordinator->PlayerCastle), nullptr, nullptr));
	TickShellWorld(World);
	ALSGameState* State = World->GetGameState<ALSGameState>();
	if (!TestNotNull(TEXT("Actual battle GameState"), State)) return false;
	if (!TestEqual(TEXT("Castle native death finalizes defeat"), State->GetBattleOutcome(), ELSBattleOutcome::Defeat)) return false;
	TestFalse(TEXT("Coordinator stops through finalization callback"), Coordinator->IsBattleActive());
	TestFalse(TEXT("Finalization closes composition"), Coordinator->IsComposing());
	TestEqual(TEXT("Finalization restores dilation"), UGameplayStatics::GetGlobalTimeDilation(World), 1.0f);
	const float Initial = ShellHealth(Coordinator->CoreOutpost);
	FLSShellExplosion Explosion;
	Explosion.Location = Coordinator->CoreOutpost->GetActorLocation();
	Coordinator->QueueExplosion(Explosion);
	FHitResult Hit(Coordinator->CoreOutpost, Cast<UPrimitiveComponent>(Coordinator->CoreOutpost->GetRootComponent()),
		Explosion.Location, FVector::UpVector);
	Coordinator->QueueShellHit(Explosion.Shot, Hit);
	Coordinator->ExecuteModule(ELSShellModule::Blast, Explosion.Location, Explosion.Shot, Hit);
	Coordinator->ApplyExperimentDamage(Coordinator->CoreOutpost, 100, Explosion.Shot, Explosion.Location,
		FVector::ForwardVector, ELSShellDamageCause::Detonation);
	TestFalse(TEXT("New shot rejected after finalization"), Coordinator->FireAt(Explosion.Location));
	Coordinator->SetComposing(true);
	TestFalse(TEXT("Composition cannot reopen after finalization"), Coordinator->IsComposing());
	TickShellWorld(World, 0.1f);
	TestEqual(TEXT("No new queued or direct damage"), ShellHealth(Coordinator->CoreOutpost), Initial);
	TestEqual(TEXT("No new detonation"), Coordinator->DetonationCount, 0);
	TestEqual(TEXT("No new shell hit"), Coordinator->ShellHitCount, 0);
	TestEqual(TEXT("No new module"), Coordinator->ModuleCount, 0);
	TestEqual(TEXT("Final result remains defeat"), State->GetBattleOutcome(), ELSBattleOutcome::Defeat);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellLandingRecoveryTest, "Landship.ShellExperiment.ForcedMotionRecoversAfterActualLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellLandingRecoveryTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	AActor* Floor = SpawnShellActor<AActor>(World, FVector::ZeroVector);
	UBoxComponent* FloorBody = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(FloorBody);
	Floor->AddInstanceComponent(FloorBody);
	FloorBody->SetBoxExtent(FVector(2000, 2000, 25));
	FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FloorBody->SetCollisionObjectType(LSCollisionChannels::Ground);
	FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
	FloorBody->SetMobility(EComponentMobility::Static);
	FloorBody->SetWorldLocation(FVector(4000, 0, -25));
	FloorBody->RegisterComponent();
	ALSShellExperimentEnemy* Enemy = SpawnShellActor<ALSShellExperimentEnemy>(World, FVector(4000, 0, 90));
	Enemy->StatsTable = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Data/DT_ShellEnemyStats.DT_ShellEnemyStats"));
	Enemy->StatsRowName = TEXT("Assault");
	if (!TestNotNull(TEXT("Production enemy stat table"), Enemy->StatsTable.Get())) return false;
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	if (!TestTrue(TEXT("Behavior initially ticking"), Enemy->Behavior->IsComponentTickEnabled())) return false;
	const bool bInitialAvoidance = Enemy->GetCharacterMovement()->bUseRVOAvoidance;
	const FVector Start = Enemy->GetActorLocation();
	Enemy->Reactive->ApplyImpulseVelocity(FVector(600, 0, 350));
	TestTrue(TEXT("Impulse enters forced motion"), Enemy->Reactive->IsForcedMotionActive());
	TestFalse(TEXT("Behavior suspended during impulse"), Enemy->Behavior->IsComponentTickEnabled());
	TestFalse(TEXT("Avoidance suspended during impulse"), Enemy->GetCharacterMovement()->bUseRVOAvoidance);
	bool bSawAirborne = false;
	float HighestZ = Start.Z;
	for (int32 Step = 0; Step < 300 && Enemy->Reactive->IsForcedMotionActive(); ++Step)
	{
		TickShellWorld(World);
		bSawAirborne |= Enemy->GetCharacterMovement()->IsFalling();
		HighestZ = FMath::Max(HighestZ, static_cast<float>(Enemy->GetActorLocation().Z));
	}
	TestTrue(TEXT("CharacterMovement actually became airborne"), bSawAirborne);
	TestTrue(TEXT("Impulse produced actual upward travel"), HighestZ > Start.Z + 20);
	TestTrue(TEXT("Impulse produced actual horizontal travel"), Enemy->GetActorLocation().X > Start.X + 50);
	TestTrue(TEXT("CharacterMovement actually landed"), Enemy->GetCharacterMovement()->IsMovingOnGround());
	TestTrue(TEXT("Landed on fixture floor"), FMath::Abs(Enemy->GetActorLocation().Z - Enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) < 5);
	TestFalse(TEXT("Forced motion released after landing"), Enemy->Reactive->IsForcedMotionActive());
	TestTrue(TEXT("Previously ticking behavior restored"), Enemy->Behavior->IsComponentTickEnabled());
	TestEqual(TEXT("Previous avoidance setting restored"), !!Enemy->GetCharacterMovement()->bUseRVOAvoidance, bInitialAvoidance);
	TestEqual(TEXT("Landing does not apply fall damage"), ShellHealth(Enemy), 100.0f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellMutualVictoryTest, "Landship.ShellExperiment.SimultaneousNativeDeathsShowVictory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellMutualVictoryTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator || !BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	Coordinator->CoreOutpost->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(ShellHealth(Coordinator->CoreOutpost), nullptr, nullptr));
	Coordinator->PlayerCastle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(ShellHealth(Coordinator->PlayerCastle), nullptr, nullptr));
	TickShellWorld(World);
	ALSGameState* State = World->GetGameState<ALSGameState>();
	if (!TestNotNull(TEXT("Actual battle GameState"), State)) return false;
	TestEqual(TEXT("Common outcome retains simultaneous destruction"), State->GetBattleOutcome(), ELSBattleOutcome::MutualDestruction);
	TestTrue(TEXT("Experiment result displays victory"), Coordinator->GetStatusText().StartsWith(TEXT("승리")));
	TestTrue(TEXT("Captured core actor is retained"), IsValid(Coordinator->CoreOutpost));
	TestFalse(TEXT("Simultaneous result stops experiment"), Coordinator->IsBattleActive());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellCoverTest, "Landship.ShellExperiment.ActualShellAndBlastRespectCover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellCoverTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	AActor* Cover = SpawnShellActor<AActor>(World, FVector::ZeroVector);
	UBoxComponent* CoverBody = NewObject<UBoxComponent>(Cover);
	Cover->SetRootComponent(CoverBody);
	Cover->AddInstanceComponent(CoverBody);
	CoverBody->SetBoxExtent(FVector(20, 500, 500));
	CoverBody->SetMobility(EComponentMobility::Static);
	CoverBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CoverBody->SetCollisionObjectType(ECC_WorldStatic);
	CoverBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	CoverBody->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	CoverBody->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Block);
	CoverBody->SetWorldLocation(FVector(4400, 0, 200));
	CoverBody->RegisterComponent();
	ALSBasicAttackTestActor* Behind = SpawnShellActor<ALSBasicAttackTestActor>(World, FVector(4600, 0, 200));
	CastChecked<UPrimitiveComponent>(Behind->GetRootComponent())->SetCollisionResponseToChannel(
		LSCollisionChannels::ObstacleProjectile, ECR_Block);
	ALSBasicAttackTestActor* Exposed = SpawnShellActor<ALSBasicAttackTestActor>(World, FVector(4320, 200, 200));
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	FLSShellShot Shot;
	Shot.Source = Coordinator->PlayerCastle;
	Shot.Origin = FVector(4000, 0, 200);
	Shot.First = ELSShellModule::Blast;
	Shot.bChild = true;
	Shot.bInheritedModule = true;
	TWeakObjectPtr<ALSShellProjectile> Projectile = SpawnShellActor<ALSShellProjectile>(World, Shot.Origin);
	Projectile->Launch(Coordinator, Shot, Shot.Origin, FVector::ForwardVector);
	for (int32 Step = 0; Step < 50 && Coordinator->ShellHitCount == 0; ++Step) TickShellWorld(World);
	if (!TestEqual(TEXT("Actual shell flight reaches first cover"), Coordinator->ShellHitCount, 1)) return false;
	TestFalse(TEXT("Shell is consumed at cover"), Projectile.IsValid());
	TestTrue(TEXT("Impact lies on cover front face"), FMath::IsNearlyEqual(Coordinator->LastShellImpact.X, 4380.0, 0.1));
	TestEqual(TEXT("Impact starts the inherited blast once"), Coordinator->ModuleCount, 1);
	if (!TestTrue(TEXT("Same actual blast damages exposed witness"), Exposed->GetCurrentHealth() < 100)) return false;
	TestEqual(TEXT("Cover blocks both shell and blast damage to rear witness"), Behind->GetCurrentHealth(), 100.0f);
	CoverBody->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Ignore);
	Coordinator->ExecuteModule(ELSShellModule::Blast, Coordinator->LastShellImpact, Shot, FHitResult());
	TickShellWorld(World);
	TestTrue(TEXT("Removing only blast occlusion exposes the same rear witness"), Behind->GetCurrentHealth() < 100);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellContactLatchTest, "Landship.ShellExperiment.ActualContactLatchResetsOnlyAfterSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellContactLatchTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->Tuning.ImpactDamagePerSpeed = 0.01f;
	ALSShellExplosive* Obstacle = SpawnShellActor<ALSShellExplosive>(World, FVector(4600, 0, 200));
	Obstacle->Reactive->bExplosive = false;
	Obstacle->Body->SetBoxExtent(FVector(60, 400, 400));
	ALSShellExperimentEnemy* Enemy = SpawnShellActor<ALSShellExperimentEnemy>(World, FVector(4000, 0, 200));
	Enemy->StatsTable = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Data/DT_ShellEnemyStats.DT_ShellEnemyStats"));
	Enemy->StatsRowName = TEXT("Assault");
	if (!TestNotNull(TEXT("Production enemy stat table"), Enemy->StatsTable.Get())) return false;
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	Enemy->Behavior->SetComponentTickEnabled(false);
	UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
	Movement->GravityScale = 0;
	Movement->FallingLateralFriction = 0;
	Movement->BrakingDecelerationFalling = 0;
	Movement->SetMovementMode(MOVE_Falling);
	Enemy->Reactive->ApplyImpulseVelocity(FVector(600, 0, 0));
	for (int32 Step = 0; Step < 150 && Coordinator->ContactCount == 0; ++Step) TickShellWorld(World);
	if (!TestEqual(TEXT("Actual forced CharacterMovement impact reaches coordinator once"), Coordinator->ContactCount, 1)) return false;
	if (!TestTrue(TEXT("Real contact commits native obstacle damage"), ShellHealth(Obstacle) < 100)) return false;
	const float HealthAfterFirstContact = ShellHealth(Obstacle);
	const FVector ContactPosition = Enemy->GetActorLocation();
	for (int32 Attempt = 0; Attempt < 5; ++Attempt)
	{
		Enemy->Reactive->ApplyImpulseVelocity(FVector(600, 0, 0));
		TickShellWorld(World);
		FHitResult BlockingHit;
		Movement->SafeMoveUpdatedComponent(FVector(10, 0, 0), Enemy->GetActorQuat(), true, BlockingHit);
		if (!TestTrue(TEXT("Repeated actual movement sweep still hits intended obstacle"), BlockingHit.bBlockingHit && BlockingHit.GetActor() == Obstacle)) return false;
		if (!TestTrue(TEXT("Continuous pushes do not separate the bodies"), Enemy->GetActorLocation().Equals(ContactPosition, 1))) return false;
		TickShellWorld(World);
		TestEqual(TEXT("Continuous contact resolves no duplicate reaction"), Coordinator->ContactCount, 1);
		TestEqual(TEXT("Continuous contact commits no duplicate damage"), ShellHealth(Obstacle), HealthAfterFirstContact);
	}
	Enemy->Reactive->ApplyImpulseVelocity(FVector(-600, 0, 0));
	TickShellWorld(World, 0.1f);
	TickShellWorld(World);
	FVector ClosestPoint;
	const float DistanceToObstacle = Obstacle->Body->GetClosestPointOnCollision(Enemy->GetActorLocation(), ClosestPoint);
	if (!TestTrue(TEXT("Actual reverse impulse separates body surfaces by more than 3cm"), DistanceToObstacle > Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() + 3)) return false;
	TestEqual(TEXT("Separation itself applies no new contact"), Coordinator->ContactCount, 1);
	Enemy->Reactive->ApplyImpulseVelocity(FVector(1200, 0, 0));
	for (int32 Step = 0; Step < 100 && Coordinator->ContactCount < 2; ++Step) TickShellWorld(World);
	TestEqual(TEXT("Actual recontact after separation resolves a second reaction"), Coordinator->ContactCount, 2);
	TestTrue(TEXT("Second separated contact commits new native damage"), ShellHealth(Obstacle) < HealthAfterFirstContact);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellQueuedMutualTest, "Landship.ShellExperiment.QueuedDetonationFinalizesMutualDestructionInOneFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellQueuedMutualTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	ALSShellExplosive* Explosive = SpawnShellActor<ALSShellExplosive>(World, FVector(-2500, 0, 200));
	Explosive->Reactive->DetonationRadius = 10000;
	Explosive->Reactive->DetonationDamage = 2000;
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	ALSGameState* State = World->GetGameState<ALSGameState>();
	if (!TestNotNull(TEXT("Actual battle GameState"), State)) return false;
	TestEqual(TEXT("Core initially alive"), ShellHealth(Coordinator->CoreOutpost), 1000.0f);
	TestEqual(TEXT("Castle initially alive"), ShellHealth(Coordinator->PlayerCastle), 1000.0f);
	if (!TestTrue(TEXT("Native explosive queues one detonation"), Explosive->RequestDetonation())) return false;
	TestEqual(TEXT("Queueing has not damaged core"), ShellHealth(Coordinator->CoreOutpost), 1000.0f);
	TestEqual(TEXT("Queueing has not damaged castle"), ShellHealth(Coordinator->PlayerCastle), 1000.0f);
	TestFalse(TEXT("Queueing has not finalized battle"), State->HasBattleEnded());
	TickShellWorld(World);
	TestEqual(TEXT("One queue drain resolves one detonation"), Coordinator->DetonationCount, 1);
	TestEqual(TEXT("Same drain kills native core"), ShellHealth(Coordinator->CoreOutpost), 0.0f);
	TestEqual(TEXT("Same drain kills native castle"), ShellHealth(Coordinator->PlayerCastle), 0.0f);
	FLSDamageResult CoreDamage, CastleDamage;
	if (!TestTrue(TEXT("Native core damage record exists"), Coordinator->CoreOutpost->FindComponentByClass<ULSHealthComponent>()->TryGetLastDamage(CoreDamage))) return false;
	if (!TestTrue(TEXT("Native castle damage record exists"), Coordinator->PlayerCastle->FindComponentByClass<ULSHealthComponent>()->TryGetLastDamage(CastleDamage))) return false;
	TestEqual(TEXT("Both native damage commits share world timestamp"), CoreDamage.TimestampSeconds, CastleDamage.TimestampSeconds);
	TestEqual(TEXT("Frame end finalizes common simultaneous result"), State->GetBattleOutcome(), ELSBattleOutcome::MutualDestruction);
	TestFalse(TEXT("One-frame mutual outcome stops experiment"), Coordinator->IsBattleActive());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellHeavyWeakPointTest, "Landship.ShellExperiment.P1.HeavyWeakPointDetonationAndBodyOnlyDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellHeavyWeakPointTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	for (int32 Scenario = 0; Scenario < 3; ++Scenario)
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
		if (!Coordinator) return false;
		ALSShellExperimentEnemy* Heavy = SpawnConfiguredShellEnemy(World, FVector(4000, 0, 200), ELSShellEnemyKind::Heavy);
		ALSCastle* Inside = LSCastleTestFixture::Spawn(World, FVector(4700, 0, 420));
		ALSCastle* Outside = LSCastleTestFixture::Spawn(World, FVector(5500, 0, 420));
		if (!TestNotNull(TEXT("Configured native heavy"), Heavy) || !TestNotNull(TEXT("Inside witness"), Inside) || !TestNotNull(TEXT("Outside witness"), Outside)) return false;
		Inside->GetMovementComponent()->Deactivate();
		Outside->GetMovementComponent()->Deactivate();
		if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
		Heavy->Behavior->SetComponentTickEnabled(false);
		Heavy->GetCharacterMovement()->DisableMovement();
		ALSShellWeakPoint* WeakPoint = Heavy->GetWeakPoint();
		if (!TestNotNull(TEXT("Production heavy creates separate weak point"), WeakPoint)) return false;
		TestEqual(TEXT("Heavy native health"), ShellHealth(Heavy), 600.0f);
		TestEqual(TEXT("Weak point native health"), ShellHealth(WeakPoint), 200.0f);
		TestFalse(TEXT("Split initially locked"), Coordinator->IsSplitUnlocked());
		TWeakObjectPtr<ALSShellExperimentEnemy> RemovedHeavy = Heavy;
		TWeakObjectPtr<ALSShellWeakPoint> RemovedWeakPoint = WeakPoint;
		if (Scenario == 0)
		{
			WeakPoint->GetHealthComponent()->ApplyDamage(ULSHealthComponent::MakeDamageRequest(200, Coordinator->PlayerCastle, Coordinator));
			TestTrue(TEXT("Weak point death commits one pending detonation"), Heavy->HasCommittedWeakPointDetonation());
		}
		else if (Scenario == 1)
		{
			FLSShellExplosion Explosion;
			Explosion.Location = WeakPoint->GetActorLocation();
			Explosion.Damage = 1000;
			Explosion.Radius = 200;
			Explosion.Velocity = 0;
			Explosion.Cause = ELSShellDamageCause::Module;
			Explosion.Shot.Source = Coordinator->PlayerCastle;
			Coordinator->QueueExplosion(Explosion);
		}
		else Heavy->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
			ULSHealthComponent::MakeDamageRequest(600, Coordinator->PlayerCastle, Coordinator));
		TickShellWorld(World);
		const FString Case = FString::Printf(TEXT("Scenario%d "), Scenario);
		TestFalse(Case + TEXT("heavy native death removes body"), RemovedHeavy.IsValid());
		TestFalse(Case + TEXT("weak point removed"), RemovedWeakPoint.IsValid());
		TestTrue(Case + TEXT("heavy defeat unlocks Split"), Coordinator->IsSplitUnlocked());
		TestEqual(Case + TEXT("only weak point death produces a detonation"), Coordinator->DetonationCount, Scenario == 2 ? 0 : 1);
		TestEqual(Case + TEXT("500 damage at700cm has half strength within1400cm"), ShellHealth(Inside), Scenario == 2 ? 1000.0f : 750.0f);
		TestEqual(Case + TEXT("1500cm witness outside weak point radius"), ShellHealth(Outside), 1000.0f);
		TickShellWorld(World);
		TestEqual(Case + TEXT("no repeated heavy detonation"), Coordinator->DetonationCount, Scenario == 2 ? 0 : 1);
		TestTrue(Case + TEXT("unlock remains available"), Coordinator->IsSplitUnlocked());
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSplitOrderTest, "Landship.ShellExperiment.P1.SplitOrderUsesThreeActualChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSplitOrderTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	for (const bool bSplitFirst : {true, false})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
		if (!Coordinator) return false;
		Coordinator->Tuning.ModuleDelay = 0.1f;
		Coordinator->Tuning.GravityAcceleration = 0;
		const FVector Point(4000, 0, 200);
		if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
		FLSShellShot Shot;
		Shot.Source = Coordinator->PlayerCastle;
		Shot.Origin = Point;
		Shot.First = bSplitFirst ? ELSShellModule::Split : ELSShellModule::Gravity;
		Shot.Second = bSplitFirst ? ELSShellModule::Blast : ELSShellModule::Split;
		FHitResult Impact;
		Impact.ImpactPoint = Impact.Location = Point;
		Impact.ImpactNormal = FVector::UpVector;
		auto* ParentEffect = SpawnShellActor<ALSShellEffectInstance>(World, Point);
		ParentEffect->Initialize(Coordinator, Shot, Impact, true);
		if (bSplitFirst) TestFalse(TEXT("Split-first parent has no manual second-slot authority"), Coordinator->CanManualTrigger());
		for (int32 Step = 0; Step < 30 && CountShellActors<ALSShellProjectile>(World) == 0; ++Step) TickShellWorld(World);
		if (!TestEqual(TEXT("Exactly three real child projectiles spawned"), CountShellActors<ALSShellProjectile>(World), 3)) return false;
		TestFalse(TEXT("Children have no manual trigger authority"), Coordinator->CanManualTrigger());
		TArray<ALSBasicAttackTestActor*> Targets;
		TArray<FVector> ExpectedImpactPoints;
		FVector CentralStart, CentralDirection;
		double MostForward = -1;
		for (TActorIterator<ALSShellProjectile> It(World); It; ++It)
		{
			const FVector Direction = It->GetActorForwardVector();
			auto* Target = SpawnShellActor<ALSBasicAttackTestActor>(World, It->GetActorLocation() + Direction * 600);
			auto* Body = CastChecked<USphereComponent>(Target->GetRootComponent());
			Body->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
			Targets.Add(Target);
			ExpectedImpactPoints.Add(Target->GetActorLocation() - Direction * Body->GetScaledSphereRadius());
			const double Forward = FVector::DotProduct(Direction, Shot.Direction);
			if (Forward > MostForward) { MostForward = Forward; CentralStart = It->GetActorLocation(); CentralDirection = Direction; }
		}
		auto* Near = SpawnShellActor<ALSBasicAttackTestActor>(World, CentralStart + CentralDirection * 850);
		auto* Outside = SpawnShellActor<ALSBasicAttackTestActor>(World, CentralStart + CentralDirection * 950);
		for (int32 Step = 0; Step < 100 && Coordinator->ShellHitCount < 3; ++Step) TickShellWorld(World);
		if (!TestEqual(TEXT("Each real child reaches one actual body"), Coordinator->ShellHitCount, 3)) return false;
		for (int32 TargetIndex = 0; TargetIndex < Targets.Num(); ++TargetIndex)
		{
			ALSBasicAttackTestActor* Target = Targets[TargetIndex];
			TestEqual(TEXT("Each child commits direct20 once and optional blast once"), Target->HitsReceived, bSplitFirst ? 2 : 1);
			if (bSplitFirst)
			{
				FLSDamageResult Damage;
				if (!TestTrue(TEXT("Inherited blast has native damage record"), Target->HealthComponent->TryGetLastDamage(Damage))) return false;
				TestTrue(TEXT("Inherited blast carries actual origin"), Damage.bHasAttackOrigin);
				TestTrue(TEXT("Inherited blast origin is child impact center rather than parent or castle"), Damage.AttackOrigin.Equals(ExpectedImpactPoints[TargetIndex], 0.1));
				const float ExpectedBlast = 50 * (1 - FVector::Dist2D(Target->GetActorLocation(), Damage.AttackOrigin) / 300);
				TestTrue(TEXT("Inherited blast uses50 damage and300cm falloff"), FMath::IsNearlyEqual(Damage.RequestedDamage, ExpectedBlast, 0.01f));
				TestTrue(TEXT("Direct and inherited blast both reduce native health"), FMath::IsNearlyEqual(Target->GetCurrentHealth(), 80 - ExpectedBlast, 0.01f));
			}
			else TestEqual(TEXT("Split-second child applies only direct20"), Target->GetCurrentHealth(), 80.0f);
		}
		TestEqual(TEXT("Three inherited effects execute only for Split-first"), Coordinator->ModuleCount, bSplitFirst ? 4 : 2);
		TestEqual(TEXT("No child projectiles remain after their first hits"), CountShellActors<ALSShellProjectile>(World), 0);
		TestTrue(TEXT("Inner radius witness distinguishes inherited blast from direct-only children"), bSplitFirst ? Near->GetCurrentHealth() < 100 : Near->GetCurrentHealth() == 100);
		TestEqual(TEXT("Witness beyond300cm receives no inherited blast"), Outside->GetCurrentHealth(), 100.0f);
		for (int32 Step = 0; Step < 50; ++Step) TickShellWorld(World);
		TestEqual(TEXT("No parent automatic second-slot execution or repeated children"), Coordinator->ModuleCount, bSplitFirst ? 4 : 2);
		TestEqual(TEXT("No repeated child hits"), Coordinator->ShellHitCount, 3);
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellManualFieldTest, "Landship.ShellExperiment.P1.ManualSecondModulePreservesFirstField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellManualFieldTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->Tuning.ModuleDelay = 0.5f;
	Coordinator->Tuning.GravityAcceleration = 0;
	Coordinator->Tuning.DirectVelocity = Coordinator->Tuning.DirectLift = 0;
	Coordinator->Tuning.BlastDamage = 40;
	ALSShellExperimentEnemy* Enemy = SpawnConfiguredShellEnemy(World, FVector(4000, 0, 200), ELSShellEnemyKind::Assault);
	if (!TestNotNull(TEXT("Configured enemy"), Enemy) || !BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	Enemy->Behavior->SetComponentTickEnabled(false);
	Enemy->GetCharacterMovement()->DisableMovement();
	FLSShellShot Shot;
	Shot.Source = Coordinator->PlayerCastle;
	Shot.Origin = Enemy->GetActorLocation() - FVector(500, 0, 0);
	auto* Projectile = SpawnShellActor<ALSShellProjectile>(World, Shot.Origin);
	Projectile->Launch(Coordinator, Shot, Shot.Origin, FVector::ForwardVector);
	for (int32 Step = 0; Step < 50 && Coordinator->ShellHitCount == 0; ++Step) TickShellWorld(World);
	if (!TestEqual(TEXT("Actual projectile starts Gravity then pending Blast"), Coordinator->ShellHitCount, 1)) return false;
	TWeakObjectPtr<ALSShellGravityField> Field;
	TWeakObjectPtr<ALSShellEffectInstance> Effect;
	for (TActorIterator<ALSShellGravityField> It(World); It; ++It) Field = *It;
	for (TActorIterator<ALSShellEffectInstance> It(World); It; ++It) Effect = *It;
	if (!TestTrue(TEXT("Independent first field and pending effect exist"), Field.IsValid() && Effect.IsValid())) return false;
	const FVector FieldOrigin = Field->GetActorLocation();
	Enemy->SetActorLocation(Enemy->GetActorLocation() + FVector(0, 200, 0));
	TickShellWorld(World);
	const FVector CurrentImpact = FieldOrigin + FVector(0, 200, 0);
	if (!TestTrue(TEXT("Pending effect follows living impact anchor"), Effect->GetActorLocation().Equals(CurrentImpact, 0.1))) return false;
	auto* Witness = SpawnShellActor<ALSBasicAttackTestActor>(World, CurrentImpact);
	if (!TestTrue(TEXT("Manual trigger available before automatic deadline"), Coordinator->CanManualTrigger())) return false;
	Coordinator->TriggerPending();
	Coordinator->TriggerPending();
	TestFalse(TEXT("Manual trigger consumes authority once"), Coordinator->CanManualTrigger());
	TickShellWorld(World);
	TestEqual(TEXT("Manual Blast executes exactly once after Gravity"), Coordinator->ModuleCount, 2);
	TestEqual(TEXT("Manual Blast uses current anchored point"), Witness->GetCurrentHealth(), 60.0f);
	for (int32 Step = 0; Step < 70; ++Step) TickShellWorld(World);
	TestEqual(TEXT("Automatic deadline does not repeat manual Blast"), Coordinator->ModuleCount, 2);
	TestEqual(TEXT("No repeated native witness damage"), Witness->GetCurrentHealth(), 60.0f);
	TestTrue(TEXT("First persistent field remains after manual second module"), Field.IsValid());
	if (Field.IsValid()) TestTrue(TEXT("First field keeps original world point"), Field->GetActorLocation().Equals(FieldOrigin, 0.1));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellOffensiveEventTest, "Landship.ShellExperiment.P1.OffensiveEventWarnsThreeSecondsAndExecutesOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellOffensiveEventTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->bEnableEvents = true;
	ALSShellExperimentEnemy* Heavy = SpawnConfiguredShellEnemy(World, FVector(4000, 0, 200), ELSShellEnemyKind::Heavy);
	if (!TestNotNull(TEXT("Only offensive event candidate"), Heavy) || !BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	Heavy->Behavior->SetComponentTickEnabled(false);
	Heavy->GetCharacterMovement()->DisableMovement();
	const float InitialSpeed = Heavy->GetCharacterMovement()->MaxWalkSpeed;
	const float InitialInterval = Heavy->BasicAttack->GetAttackSettings().Interval;
	TestTrue(TEXT("Candidate selected between45 and90 game seconds"), Coordinator->EventCandidateTime >= 45 && Coordinator->EventCandidateTime <= 90);
	TickShellAtTime(World, Coordinator->EventCandidateTime - 0.1);
	TestFalse(TEXT("Event not attempted before candidate time"), Coordinator->bEventAttempted);
	TickShellAtTime(World, Coordinator->EventCandidateTime + 0.01);
	const double WarningStarted = World->GetTimeSeconds();
	TestTrue(TEXT("Candidate attempted at scheduled game time"), Coordinator->bEventAttempted);
	TestFalse(TEXT("Warning does not execute event immediately"), Coordinator->bEventExecuted);
	TestFalse(TEXT("Warning text present"), Coordinator->GetEventText().IsEmpty());
	TestEqual(TEXT("Warning keeps original movement speed"), Heavy->GetCharacterMovement()->MaxWalkSpeed, InitialSpeed);
	TestEqual(TEXT("Warning keeps original attack interval"), Heavy->BasicAttack->GetAttackSettings().Interval, InitialInterval);
	TickShellAtTime(World, WarningStarted + 2.9);
	TestFalse(TEXT("Event cannot execute during first2.9 warning seconds"), Coordinator->bEventExecuted);
	TickShellAtTime(World, WarningStarted + 3.05);
	TestTrue(TEXT("Event executes after three-second warning"), Coordinator->bEventExecuted);
	TestEqual(TEXT("Offensive event accelerates existing heavy"), Heavy->GetCharacterMovement()->MaxWalkSpeed, InitialSpeed * 1.5f);
	TestEqual(TEXT("Offensive event halves existing heavy attack interval"), Heavy->BasicAttack->GetAttackSettings().Interval, InitialInterval * 0.5f);
	TestEqual(TEXT("Offensive event does not spawn another heavy"), CountShellActors<ALSShellExperimentEnemy>(World), 1);
	TickShellAtTime(World, WarningStarted + 30);
	TestEqual(TEXT("Later ticks do not repeat movement buff"), Heavy->GetCharacterMovement()->MaxWalkSpeed, InitialSpeed * 1.5f);
	TestEqual(TEXT("Later ticks do not repeat attack buff"), Heavy->BasicAttack->GetAttackSettings().Interval, InitialInterval * 0.5f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSkippedEventTest, "Landship.ShellExperiment.P1.NoEventCandidatesSkipWithoutRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSkippedEventTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->bEnableEvents = true;
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	TickShellAtTime(World, Coordinator->EventCandidateTime + 0.01);
	TestTrue(TEXT("Empty candidate set consumes the one attempt"), Coordinator->bEventAttempted);
	TestFalse(TEXT("Empty candidate set executes nothing"), Coordinator->bEventExecuted);
	TestTrue(TEXT("Skipped event starts no warning"), Coordinator->GetEventText().IsEmpty());
	ALSShellExperimentEnemy* Heavy = SpawnConfiguredShellEnemy(World, FVector(4000, 0, 200), ELSShellEnemyKind::Heavy);
	if (!TestNotNull(TEXT("Valid candidate added only after skip"), Heavy)) return false;
	Heavy->Behavior->SetComponentTickEnabled(false);
	Heavy->GetCharacterMovement()->DisableMovement();
	const float InitialSpeed = Heavy->GetCharacterMovement()->MaxWalkSpeed;
	const float InitialInterval = Heavy->BasicAttack->GetAttackSettings().Interval;
	TickShellAtTime(World, Coordinator->EventCandidateTime + 30);
	TestFalse(TEXT("Newly valid candidate cannot restart skipped event"), Coordinator->bEventExecuted);
	TestTrue(TEXT("No late warning"), Coordinator->GetEventText().IsEmpty());
	TestEqual(TEXT("Late candidate has no movement buff"), Heavy->GetCharacterMovement()->MaxWalkSpeed, InitialSpeed);
	TestEqual(TEXT("Late candidate has no attack buff"), Heavy->BasicAttack->GetAttackSettings().Interval, InitialInterval);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellCancelledEventTest, "Landship.ShellExperiment.P1.EventRevalidatesOriginalHeavyAndStopsAfterOutcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellCancelledEventTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	for (const bool bFinalizeBattle : {false, true})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
		if (!Coordinator) return false;
		Coordinator->bEnableEvents = true;
		ALSShellExperimentEnemy* Heavy = SpawnConfiguredShellEnemy(World, FVector(4000, 0, 200), ELSShellEnemyKind::Heavy);
		if (!TestNotNull(TEXT("Original offensive candidate"), Heavy) || !BeginShellWorld(TestWorld, *this, Coordinator)) return false;
		Heavy->Behavior->SetComponentTickEnabled(false);
		Heavy->GetCharacterMovement()->DisableMovement();
		TickShellAtTime(World, Coordinator->EventCandidateTime + 0.01);
		const double WarningStarted = World->GetTimeSeconds();
		if (!TestTrue(TEXT("Warning has started"), Coordinator->bEventAttempted && !Coordinator->GetEventText().IsEmpty())) return false;
		ALSShellExperimentEnemy* UnbuffedHeavy = Heavy;
		if (bFinalizeBattle)
		{
			Coordinator->PlayerCastle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
				ULSHealthComponent::MakeDamageRequest(ShellHealth(Coordinator->PlayerCastle), nullptr, nullptr));
			TickShellWorld(World);
			if (!TestFalse(TEXT("Castle native death finalizes during warning"), Coordinator->IsBattleActive())) return false;
			TestTrue(TEXT("Finalization clears event warning"), Coordinator->GetEventText().IsEmpty());
		}
		else
		{
			Heavy->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
				ULSHealthComponent::MakeDamageRequest(600, Coordinator->PlayerCastle, Coordinator));
			UnbuffedHeavy = SpawnConfiguredShellEnemy(World, FVector(5000, 0, 200), ELSShellEnemyKind::Heavy);
			if (!TestNotNull(TEXT("Replacement candidate is not the warned heavy"), UnbuffedHeavy)) return false;
			UnbuffedHeavy->Behavior->SetComponentTickEnabled(false);
			UnbuffedHeavy->GetCharacterMovement()->DisableMovement();
		}
		const float InitialSpeed = UnbuffedHeavy->GetCharacterMovement()->MaxWalkSpeed;
		const float InitialInterval = UnbuffedHeavy->BasicAttack->GetAttackSettings().Interval;
		TickShellAtTime(World, WarningStarted + 3.05);
		TestFalse(TEXT("Invalidated or finalized pending event does not execute"), Coordinator->bEventExecuted);
		TestEqual(TEXT("No movement buff after cancellation or finalization"), UnbuffedHeavy->GetCharacterMovement()->MaxWalkSpeed, InitialSpeed);
		TestEqual(TEXT("No attack buff after cancellation or finalization"), UnbuffedHeavy->BasicAttack->GetAttackSettings().Interval, InitialInterval);
		TickShellAtTime(World, WarningStarted + 30);
		TestFalse(TEXT("No event retry after cancellation or finalization"), Coordinator->bEventExecuted);
		if (bFinalizeBattle) TestTrue(TEXT("Finalized battle starts no new warning"), Coordinator->GetEventText().IsEmpty());
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChildFrameDrainTest, "Landship.ShellExperiment.P1.ConsumedChildHitsDrainBeforeWorldFrameEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChildFrameDrainTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	ALSBasicAttackTestActor* Target = SpawnShellActor<ALSBasicAttackTestActor>(World, FVector(4600, 0, 200));
	Target->SetInitialFaction(ELSFaction::Enemy);
	CastChecked<UPrimitiveComponent>(Target->GetRootComponent())->SetCollisionResponseToChannel(
		LSCollisionChannels::ObstacleProjectile, ECR_Block);
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	if (!TestEqual(TEXT("Native target alive before parent flight"), Target->GetCurrentHealth(), 100.0f)) return false;
	FLSShellShot Shot;
	Shot.Source = Coordinator->PlayerCastle;
	Shot.Origin = FVector(4000, 0, 200);
	Shot.First = ELSShellModule::Split;
	Shot.Second = ELSShellModule::Blast;
	ALSShellProjectile* Parent = SpawnShellActor<ALSShellProjectile>(World, Shot.Origin);
	TWeakObjectPtr<ALSShellProjectile> ParentLifetime = Parent;
	Parent->Launch(Coordinator, Shot, Shot.Origin, FVector::ForwardVector);
	for (int32 Step = 0; Step < 100 && Coordinator->ShellHitCount == 0; ++Step) TickShellWorld(World, 0.016f);
	if (!TestTrue(TEXT("Actual parent projectile reaches native target"), Coordinator->ShellHitCount >= 1)) return false;
	TestFalse(TEXT("Actual parent consumed at its first body hit"), ParentLifetime.IsValid());
	int32 LiveChildren = 0;
	for (TActorIterator<ALSShellProjectile> It(World); It; ++It)
		if (*It != Parent) ++LiveChildren;
	AddInfo(FString::Printf(TEXT("World-frame snapshot after parent impact: resolvedHits=%d liveChildren=%d modules=%d targetHealth=%.2f"),
		Coordinator->ShellHitCount, LiveChildren, Coordinator->ModuleCount, Target->GetCurrentHealth()));
	if (!TestTrue(TEXT("Split leaves at most three live children"), LiveChildren <= 3)) return false;
	if (!TestTrue(TEXT("Every consumed child hit is resolved before this world frame ends"), Coordinator->ShellHitCount - 1 >= 3 - LiveChildren)) return false;
	TickShellWorld(World, 0.016f);
	TestEqual(TEXT("Parent and three actual child hits resolved by next frame"), Coordinator->ShellHitCount, 4);
	TestEqual(TEXT("Split plus three inherited Blasts execute once"), Coordinator->ModuleCount, 4);
	TestEqual(TEXT("No children remain after their actual first body hits"), CountShellActors<ALSShellProjectile>(World), 0);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellRotatedSplitAnchorTest, "Landship.ShellExperiment.P1.SecondSplitRehitsRotatedLivingAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellRotatedSplitAnchorTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->Tuning.ModuleDelay = 100;
	Coordinator->Tuning.GravityAcceleration = 0;
	ALSBasicAttackTestActor* Target = SpawnShellActor<ALSBasicAttackTestActor>(World, FVector(4600, 0, 200));
	Target->SetInitialFaction(ELSFaction::Enemy);
	CastChecked<UPrimitiveComponent>(Target->GetRootComponent())->SetCollisionResponseToChannel(
		LSCollisionChannels::ObstacleProjectile, ECR_Block);
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	FLSShellShot Shot;
	Shot.Source = Coordinator->PlayerCastle;
	Shot.Origin = FVector(4000, 0, 200);
	Shot.First = ELSShellModule::Gravity;
	Shot.Second = ELSShellModule::Split;
	FHitResult Impact;
	if (!TestTrue(TEXT("Production shell sweep hits living native body"), ALSShellProjectile::SweepFlight(
		World, Shot.Origin, Target->GetActorLocation(), Coordinator->PlayerCastle, Impact) && Impact.GetActor() == Target)) return false;
	const FVector OriginalImpact = Impact.ImpactPoint;
	TWeakObjectPtr<ALSShellEffectInstance> Effect = SpawnShellActor<ALSShellEffectInstance>(World, OriginalImpact);
	Effect->Initialize(Coordinator, Shot, Impact, true);
	TWeakObjectPtr<ALSShellGravityField> Field;
	for (TActorIterator<ALSShellGravityField> It(World); It; ++It) Field = *It;
	if (!TestTrue(TEXT("First Gravity field exists independently"), Field.IsValid())) return false;
	Target->SetActorRotation(FRotator(0, 180, 0));
	TickShellWorld(World, 0.016f);
	if (!TestTrue(TEXT("Pending impact follows rotation to far side of living body"), Effect.IsValid() && Effect->GetActorLocation().X > Target->GetActorLocation().X)) return false;
	TestTrue(TEXT("First Gravity field retains original impact point"), Field.IsValid() && Field->GetActorLocation().Equals(OriginalImpact, 0.1));
	TestEqual(TEXT("Sweep and Gravity do not damage anchor"), Target->GetCurrentHealth(), 100.0f);
	if (!TestTrue(TEXT("Second Split can be triggered manually"), Coordinator->CanManualTrigger())) return false;
	Coordinator->TriggerPending();
	TestFalse(TEXT("Manual Split consumes second-slot authority"), Coordinator->CanManualTrigger());
	for (int32 Step = 0; Step < 50 && Coordinator->ShellHitCount < 3; ++Step) TickShellWorld(World, 0.016f);
	TestEqual(TEXT("Three actual children rehit rotated living body"), Coordinator->ShellHitCount, 3);
	TestEqual(TEXT("Children apply three native direct20 hits"), Target->GetCurrentHealth(), 40.0f);
	TestEqual(TEXT("No inherited module damage"), Target->HitsReceived, 3);
	TestEqual(TEXT("Only first Gravity and second Split execute"), Coordinator->ModuleCount, 2);
	TestEqual(TEXT("All children consumed on their first body hits"), CountShellActors<ALSShellProjectile>(World), 0);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellBlastEjectionTest, "Landship.ShellExperiment.P1.BlastEjectsSurvivorAgainstInwardWalkingVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellBlastEjectionTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	AActor* Floor = SpawnShellActor<AActor>(World, FVector::ZeroVector);
	UBoxComponent* FloorBody = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(FloorBody);
	Floor->AddInstanceComponent(FloorBody);
	FloorBody->SetBoxExtent(FVector(2000, 2000, 25));
	FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FloorBody->SetCollisionObjectType(LSCollisionChannels::Ground);
	FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
	FloorBody->SetMobility(EComponentMobility::Static);
	FloorBody->SetWorldLocation(FVector(4000, 0, -25));
	FloorBody->RegisterComponent();
	ALSShellExperimentEnemy* Enemy = SpawnConfiguredShellEnemy(World, FVector(4000, 0, 90), ELSShellEnemyKind::Assault);
	if (!TestNotNull(TEXT("Production Assault on open floor"), Enemy) || !BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	Enemy->Behavior->SetComponentTickEnabled(false);
	UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
	const FVector WalkingStart = Enemy->GetActorLocation();
	for (int32 Step = 0; Step < 20; ++Step)
	{
		Movement->RequestDirectMove(FVector(-100, 0, 0), true);
		TickShellWorld(World);
	}
	if (!TestTrue(TEXT("Requested walking produces actual inward travel"), Enemy->GetActorLocation().X < WalkingStart.X - 5)) return false;
	const FVector Outward = FVector::ForwardVector;
	FLSShellExplosion Explosion;
	Explosion.Location = Enemy->GetActorLocation() - Outward * 460;
	Explosion.Radius = 500;
	Explosion.Damage = 150;
	Explosion.Velocity = 1200;
	Explosion.Cause = ELSShellDamageCause::Module;
	Explosion.Shot.Source = Coordinator->PlayerCastle;
	bool bObservedBeforeDrain = false;
	bool bWalkingBeforeBlast = false;
	FVector PreBlastVelocity = FVector::ZeroVector;
	FVector PreBlastPosition = FVector::ZeroVector;
	const FDelegateHandle Observer = FWorldDelegates::OnWorldPostActorTick.AddLambda(
		[&](UWorld* TickedWorld, ELevelTick, float)
		{
			if (TickedWorld != World || bObservedBeforeDrain) return;
			bObservedBeforeDrain = ShellHealth(Enemy) == 100;
			bWalkingBeforeBlast = Movement->IsMovingOnGround();
			PreBlastVelocity = Enemy->GetVelocity();
			PreBlastPosition = Enemy->GetActorLocation();
		});
	ON_SCOPE_EXIT { FWorldDelegates::OnWorldPostActorTick.Remove(Observer); };
	Coordinator->QueueExplosion(Explosion);
	Movement->RequestDirectMove(FVector(-100, 0, 0), true);
	TickShellWorld(World);
	if (!TestTrue(TEXT("Observer samples native movement before explosion damage drains"), bObservedBeforeDrain)) return false;
	if (!TestTrue(TEXT("Blast survivor was actually walking toward blast"), bWalkingBeforeBlast && FVector::DotProduct(PreBlastVelocity, Outward) < -90)) return false;
	if (!TestTrue(TEXT("Real module blast commits nonlethal native damage"), ShellHealth(Enemy) > 0 && ShellHealth(Enemy) < 100)) return false;
	bool bSawOutwardVelocity = false;
	for (int32 Step = 0; Step < 50; ++Step)
	{
		TickShellWorld(World);
		bSawOutwardVelocity |= FVector::DotProduct(Enemy->GetVelocity(), Outward) > 0;
	}
	const double OutwardTravel = FVector::DotProduct(Enemy->GetActorLocation() - PreBlastPosition, Outward);
	const float BodyDiameter = Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() * 2;
	AddInfo(FString::Printf(TEXT("Blast ejection: inwardBefore=%s outwardTravel=%.2f bodyDiameter=%.2f health=%.2f"),
		*PreBlastVelocity.ToString(), OutwardTravel, BodyDiameter, ShellHealth(Enemy)));
	TestTrue(TEXT("Survivor develops actual outward velocity"), bSawOutwardVelocity);
	TestTrue(TEXT("Survivor visibly travels at least one body diameter outward within0.5 game seconds"), OutwardTravel >= BodyDiameter);
	TestTrue(TEXT("Ejected native Assault remains alive"), ShellHealth(Enemy) > 0);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellBlastMomentumPolicyTest, "Landship.ShellExperiment.P1.BlastFloorIsModuleOnlyAndPreservesForcedMomentum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellBlastMomentumPolicyTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	for (const bool bExistingForcedMotion : {false, true})
	{
		for (const ELSShellDamageCause Cause : {ELSShellDamageCause::Module, ELSShellDamageCause::Detonation})
		{
			FTestWorldWrapper TestWorld;
			if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
			UWorld* World = TestWorld.GetTestWorld();
			ALSShellExperimentCoordinator* Coordinator = SetUpShellWorld(World, *this);
			if (!Coordinator) return false;
			ALSShellExperimentEnemy* Enemy = SpawnConfiguredShellEnemy(World, FVector(4000, 0, 1000), ELSShellEnemyKind::Assault);
			if (!TestNotNull(TEXT("Native Assault momentum receiver"), Enemy) || !BeginShellWorld(TestWorld, *this, Coordinator)) return false;
			Enemy->Behavior->SetComponentTickEnabled(false);
			UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
			Movement->GravityScale = 0;
			Movement->FallingLateralFriction = 0;
			Movement->BrakingDecelerationFalling = 0;
			Movement->SetMovementMode(MOVE_Falling);
			if (bExistingForcedMotion)
			{
				Enemy->Reactive->ApplyImpulseVelocity(FVector(-300, 200, -400));
				Enemy->Reactive->ApplyGravity(Enemy->GetActorLocation() - FVector(1000, 0, 0), 1000, 0.1f);
				if (!TestTrue(TEXT("Public impulse and gravity create native pending forced momentum"),
					Enemy->Reactive->IsForcedMotionActive() && Movement->PendingLaunchVelocity.Equals(FVector(-400, 200, -400), 0.01))) return false;
			}
			FLSShellExplosion Explosion;
			Explosion.Location = Enemy->GetActorLocation() - FVector(460, 0, 0);
			Explosion.Radius = 500;
			Explosion.Damage = 150;
			Explosion.Velocity = 1200;
			Explosion.Cause = Cause;
			Explosion.Shot.Source = Coordinator->PlayerCastle;
			bool bObservedBeforeDrain = false;
			FVector BeforeVelocity = FVector::ZeroVector;
			FVector BeforePosition = FVector::ZeroVector;
			const FDelegateHandle Observer = FWorldDelegates::OnWorldPostActorTick.AddLambda(
				[&](UWorld* TickedWorld, ELevelTick, float)
				{
					if (TickedWorld != World || bObservedBeforeDrain) return;
					bObservedBeforeDrain = ShellHealth(Enemy) == 100;
					BeforeVelocity = Enemy->GetVelocity();
					BeforePosition = Enemy->GetActorLocation();
				});
			ON_SCOPE_EXIT { FWorldDelegates::OnWorldPostActorTick.Remove(Observer); };
			Coordinator->QueueExplosion(Explosion);
			TickShellWorld(World);
			if (!TestTrue(TEXT("Native motion sampled before queued explosion drains"), bObservedBeforeDrain)) return false;
			if (!TestTrue(TEXT("Real queued blast commits nonlethal native damage"), ShellHealth(Enemy) > 0 && ShellHealth(Enemy) < 100)) return false;
			const FVector Outward = (BeforePosition - Explosion.Location).GetSafeNormal2D();
			const FVector Tangent(-Outward.Y, Outward.X, 0);
			if (bExistingForcedMotion)
			{
				if (!TestTrue(TEXT("Actual pre-blast motion has inward, tangential and falling components"),
					FVector::DotProduct(BeforeVelocity, Outward) < -300 && FMath::Abs(FVector::DotProduct(BeforeVelocity, Tangent)) > 100 && BeforeVelocity.Z < -300)) return false;
			}
			else if (!TestTrue(TEXT("Unforced comparison starts stationary"), BeforeVelocity.IsNearlyZero(0.01))) return false;
			const float Scale = 1 - FVector::Dist2D(BeforePosition, Explosion.Location) / Explosion.Radius;
			const float LinearRadial = Explosion.Velocity * Scale;
			const float LinearLift = LinearRadial * 0.22f;
			if (!TestTrue(TEXT("Edge blast exercises values below configured module minimums"),
				LinearRadial < Coordinator->Tuning.BlastMinimumVelocity && LinearLift < Coordinator->Tuning.BlastMinimumLift)) return false;
			const float Radial = Cause == ELSShellDamageCause::Module ? FMath::Max(LinearRadial, Coordinator->Tuning.BlastMinimumVelocity) : LinearRadial;
			const float Lift = Cause == ELSShellDamageCause::Module ? FMath::Max(LinearLift, Coordinator->Tuning.BlastMinimumLift) : LinearLift;
			const FVector Expected = BeforeVelocity + (Outward * Radial + FVector::UpVector * Lift) / Enemy->Reactive->GetMovementResistance();
			const FVector After = Movement->PendingLaunchVelocity;
			const FString Case = FString::Printf(TEXT("Cause=%s ExistingForced=%d "), Cause == ELSShellDamageCause::Module ? TEXT("Module") : TEXT("Detonation"), bExistingForcedMotion);
			AddInfo(FString::Printf(TEXT("%sbefore=%s nativePending=%s expected=%s health=%.2f"),
				*Case, *BeforeVelocity.ToString(), *After.ToString(), *Expected.ToString(), ShellHealth(Enemy)));
			TestTrue(Case + TEXT("radial impulse preserves existing inward momentum and applies only cause-appropriate minimum"),
				FMath::IsNearlyEqual(FVector::DotProduct(After, Outward), FVector::DotProduct(Expected, Outward), 0.05));
			TestTrue(Case + TEXT("blast preserves tangential momentum"),
				FMath::IsNearlyEqual(FVector::DotProduct(After, Tangent), FVector::DotProduct(BeforeVelocity, Tangent), 0.05));
			TestTrue(Case + TEXT("lift adds to existing falling momentum and applies only cause-appropriate minimum"),
				FMath::IsNearlyEqual(After.Z, Expected.Z, 0.05));
			TestWorld.ForwardErrorMessages(this);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyMomentumTest, "Landship.ShellExperiment.Supply.RigidMomentumUsesActualMass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyMomentumTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	auto* Ball = SpawnShellActor<ALSShellMetalBall>(World, FVector(-1800, -1000, 500));
	auto* Barrel = SpawnShellActor<ALSShellBarrel>(World, FVector(-1800, 1000, 500));
	Ball->Body->SetEnableGravity(false); Barrel->Body->SetEnableGravity(false);
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	TestTrue(TEXT("Ball is a real solver body"), Ball->Body->IsSimulatingPhysics());
	TestTrue(TEXT("Barrel is a real solver body"), Barrel->Body->IsSimulatingPhysics());
	TestTrue(TEXT("Ball solver actor actually has CCD enabled"), FChaosEngineInterface::IsCcdEnabled(Ball->Body->BodyInstance.GetPhysicsActorHandle()));
	TestTrue(TEXT("Barrel solver actor actually has CCD enabled"), FChaosEngineInterface::IsCcdEnabled(Barrel->Body->BodyInstance.GetPhysicsActorHandle()));
	TestTrue(TEXT("Ball solver mass is300kg"), FMath::IsNearlyEqual(Ball->Body->GetMass(), 300.f, .01f));
	TestTrue(TEXT("Barrel solver mass is60kg"), FMath::IsNearlyEqual(Barrel->Body->GetMass(), 60.f, .01f));
	const uint64 Id = Ball->Reactive->BodyId;
	const uint32 Generation = Ball->Reactive->GetBodyGeneration();
	Ball->Reactive->SetPhysicalBody(Ball->Body);
	TestTrue(TEXT("Same-body binding preserves identity"), Id != 0 && Id == Ball->Reactive->BodyId && Generation == Ball->Reactive->GetBodyGeneration());
	TestTrue(TEXT("Ball accepts momentum"), Ball->Reactive->ApplyMomentum(FVector(15000, 0, 0)));
	TestTrue(TEXT("Barrel accepts same momentum"), Barrel->Reactive->ApplyMomentum(FVector(15000, 0, 0)));
	TickShellWorld(World);
	TickShellWorld(World);
	const float BallSpeed = Ball->Body->GetPhysicsLinearVelocity().X;
	const float BarrelSpeed = Barrel->Body->GetPhysicsLinearVelocity().X;
	AddInfo(FString::Printf(TEXT("Real solver velocities ball=%.3f barrel=%.3f"), BallSpeed, BarrelSpeed));
	TestTrue(TEXT("Impulse divided by300kg gives about50cm/s"), FMath::IsNearlyEqual(BallSpeed, 50.f, 1.f));
	TestTrue(TEXT("Same impulse divided by60kg gives about250cm/s"), FMath::IsNearlyEqual(BarrelSpeed, 250.f, 2.f));
	TestEqual(TEXT("Momentum causes no direct health damage"), ShellHealth(Ball), 600.f);
	TestEqual(TEXT("Momentum does not detonate distant barrel"), ShellHealth(Barrel), 100.f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyBlastEdgeTest, "Landship.ShellExperiment.Supply.BlastBudgetStillFallsToZeroAtEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyBlastEdgeTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	auto* Ball = SpawnShellActor<ALSShellMetalBall>(World, FVector(-1431, 0, 500));
	Ball->Body->SetEnableGravity(false);
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	FLSShellExplosion Explosion;
	Coordinator->CaptureShotContext(Explosion.Shot);
	Explosion.Cause = ELSShellDamageCause::Module;
	Explosion.Damage = 0; Explosion.Radius = 500; Explosion.Location = FVector(-2000, 0, 500);
	Coordinator->QueueExplosion(Explosion);
	TickShellWorld(World); TickShellWorld(World);
	const float Speed = Ball->Body->GetPhysicsLinearVelocity().Size();
	AddInfo(FString::Printf(TEXT("Single edge target solver speed=%.3f"), Speed));
	TestTrue(TEXT("One edge body does not receive entire blast budget"), Speed > 0 && Speed < 5);
	TestEqual(TEXT("Zero-damage blast remains damage-free"), ShellHealth(Ball), 600.f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyEmitterTest, "Landship.ShellExperiment.Supply.LiveEmitterDoesNotShieldItsOwnDetonation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyEmitterTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	auto* Barrel = SpawnShellActor<ALSShellBarrel>(World, FVector(-850, 0, 200));
	Barrel->Body->SetEnableGravity(false);
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	const float Before = ShellHealth(Coordinator->CoreOutpost);
	TestTrue(TEXT("Still-living barrel queues detonation"), Barrel->Reactive->RequestDetonation());
	TestEqual(TEXT("Queued emitter remains alive until drain"), ShellHealth(Barrel), 100.f);
	TestFalse(TEXT("Duplicate detonation refused"), Barrel->Reactive->RequestDetonation());
	TickShellWorld(World);
	TestEqual(TEXT("Exactly one detonation"), Coordinator->DetonationCount, 1);
	TestTrue(TEXT("Actual core receives environmental blast through emitter surface"),
		FMath::IsNearlyEqual(Before - ShellHealth(Coordinator->CoreOutpost), 250.f * (1 - 250.f / 900), .1f));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyCollisionTest, "Landship.ShellExperiment.Supply.ActualRollingBallHitsBarrelWithoutFreeDetonation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyCollisionTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	auto* Floor = SpawnShellActor<AStaticMeshActor>(World, FVector(-1700, 0, -50));
	Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
	Floor->SetActorScale3D(FVector(20, 20, 1));
	auto* Ball = SpawnShellActor<ALSShellMetalBall>(World, FVector(-2100, 0, 70));
	auto* Barrel = SpawnShellActor<ALSShellBarrel>(World, FVector(-1750, 0, 60));
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	for (int32 Step = 0; Step < 30; ++Step) TickShellWorld(World);
	TestTrue(TEXT("Thrust applies only physical momentum"), Ball->Reactive->ApplyMomentum(FVector(240000, 0, 0)));
	for (int32 Step = 0; Step < 100 && Coordinator->ContactCount == 0; ++Step) TickShellWorld(World);
	AddInfo(FString::Printf(TEXT("Native rolling contact count=%d speed=%.3f barrel=%.3f"), Coordinator->ContactCount, Coordinator->LastContactSpeed, ShellHealth(Barrel)));
	TestTrue(TEXT("Native contact enters response queue"), Coordinator->ContactCount > 0);
	TestTrue(TEXT("Uncharged first contact damages but cannot kill100HP barrel"), ShellHealth(Barrel) > 0 && ShellHealth(Barrel) < 100);
	TestEqual(TEXT("Uncharged first impact does not auto-detonate"), Coordinator->DetonationCount, 0);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyEmitterBudgetTest, "Landship.ShellExperiment.Supply.LiveAndKilledEmittersGiveSameSurvivorMomentum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyEmitterBudgetTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FVector Velocities[2];
	float Health[2];
	for (int32 Killed = 0; Killed < 2; ++Killed)
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		auto* Coordinator = SetUpShellWorld(World, *this);
		if (!Coordinator) return false;
		Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
		auto* Barrel = SpawnShellActor<ALSShellBarrel>(World, FVector(-2000, 0, 500));
		auto* Ball = SpawnShellActor<ALSShellMetalBall>(World, FVector(-1500, 0, 500));
		Barrel->Body->SetEnableGravity(false);
		Ball->Body->SetEnableGravity(false);
		if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
		TWeakObjectPtr<ALSShellBarrel> Emitter = Barrel;
		if (Killed)
		{
			Barrel->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
				ULSHealthComponent::MakeDamageRequest(100, Coordinator->PlayerCastle, Coordinator));
			if (!TestFalse(TEXT("Common lethal damage removes emitter before queue drains"), Emitter.IsValid())) return false;
		}
		else
		{
			if (!TestTrue(TEXT("Living emitter accepts detonation"), Barrel->Reactive->RequestDetonation())) return false;
			TestEqual(TEXT("Request leaves common health intact until drain"), ShellHealth(Barrel), 100.f);
		}
		TickShellWorld(World);
		TickShellWorld(World);
		Velocities[Killed] = Ball->Body->GetPhysicsLinearVelocity();
		Health[Killed] = ShellHealth(Ball);
		AddInfo(FString::Printf(TEXT("Emitter killedBeforeDrain=%d survivorVelocity=%s survivorHealth=%.3f detonations=%d"),
			Killed, *Velocities[Killed].ToCompactString(), Health[Killed], Coordinator->DetonationCount));
		TestFalse(TEXT("Both detonation paths remove emitting body"), Emitter.IsValid());
		TestEqual(TEXT("Each emitter resolves exactly once"), Coordinator->DetonationCount, 1);
		TestEqual(TEXT("No module was injected"), Coordinator->ModuleCount, 0);
		TestEqual(TEXT("No contact contributed momentum"), Coordinator->ContactCount, 0);
		TestTrue(TEXT("Surviving ball receives substantial environmental momentum"), Velocities[Killed].Size() > 200);
		TestTrue(TEXT("Environmental detonation reduces common health without killing ball"),
			FMath::IsNearlyEqual(Health[Killed], 600.f - 250.f * (1.f - 430.f / 900.f), .1f));
		TestWorld.ForwardErrorMessages(this);
	}
	TestTrue(TEXT("Emitter lifetime cannot consume part of survivor impulse budget"), Velocities[0].Equals(Velocities[1], .5));
	TestTrue(TEXT("Identical geometry gives identical common health damage"), FMath::IsNearlyEqual(Health[0], Health[1], .01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyRigidRecontactTest, "Landship.ShellExperiment.Supply.RigidPairNeedsMoreThanThreeCentimetersBeforeRecontact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyRigidRecontactTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	const FVector Start(-2000, 0, 500);
	auto* A = SpawnShellActor<ALSShellMetalBall>(World, Start);
	auto* B = SpawnShellActor<ALSShellMetalBall>(World, Start + FVector(141, 0, 0));
	A->Body->SetEnableGravity(false);
	B->Body->SetEnableGravity(false);
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	// 접촉 허용 간격만 분리해 검증하는 기술용 배치다. 일반 플레이의 이동 경로를 재현하지 않는다.
	auto PlacePair = [&](float Gap, float Speed)
	{
		A->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
		B->SetActorLocation(Start + FVector(140 + Gap, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		A->Body->SetPhysicsLinearVelocity(FVector(Speed, 0, 0));
		B->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		A->Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		B->Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		A->Body->WakeAllRigidBodies(); B->Body->WakeAllRigidBodies();
	};
	PlacePair(1, 800);
	TickShellWorld(World);
	if (!TestEqual(TEXT("Two native Hit notifications resolve one real rigid contact"), Coordinator->ContactCount, 1)) return false;
	TestTrue(TEXT("Contact metric observes incoming speed"), FMath::IsNearlyEqual(Coordinator->LastContactSpeed, 800.f, 2.f));
	const float AfterFirstA = ShellHealth(A), AfterFirstB = ShellHealth(B);
	TestTrue(TEXT("First contact damages both common health components once"),
		FMath::IsNearlyEqual(AfterFirstA, 570.f, .2f) && FMath::IsNearlyEqual(AfterFirstB, 570.f, .2f));
	for (int32 Repeat = 0; Repeat < 3; ++Repeat)
	{
		PlacePair(2, 800);
		TestTrue(TEXT("Fixture retains surface gap within continuing-contact skin"),
			FMath::IsNearlyEqual(FVector::Distance(A->GetActorLocation(), B->GetActorLocation()) - 140, 2.f, .01f));
		TickShellWorld(World);
		TestTrue(TEXT("Solver really transfers velocity during continuing contact"), B->Body->GetPhysicsLinearVelocity().X > 100);
		TestEqual(TEXT("Continuing solver contact is not a new gameplay event"), Coordinator->ContactCount, 1);
		TestEqual(TEXT("Continuing contact cannot repeat A damage"), ShellHealth(A), AfterFirstA);
		TestEqual(TEXT("Continuing contact cannot repeat B damage"), ShellHealth(B), AfterFirstB);
	}
	PlacePair(4, 0);
	TickShellWorld(World);
	TestTrue(TEXT("Actual separated geometry exceeds three-centimeter skin"),
		FVector::Distance(A->GetActorLocation(), B->GetActorLocation()) - 140 > 3);
	TestEqual(TEXT("Separation alone produces no contact event"), Coordinator->ContactCount, 1);
	PlacePair(1, 800);
	TickShellWorld(World);
	TestEqual(TEXT("Real solver recontact after separation resolves second event"), Coordinator->ContactCount, 2);
	TestTrue(TEXT("Recontact applies exactly one more symmetric damage event"),
		FMath::IsNearlyEqual(ShellHealth(A), 540.f, .4f) && FMath::IsNearlyEqual(ShellHealth(B), 540.f, .4f));
	TestEqual(TEXT("Metal pair does not detonate"), Coordinator->DetonationCount, 0);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyPendingMomentumContactTest, "Landship.ShellExperiment.Supply.PendingMomentumCountsInFirstPhysicsStepContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyPendingMomentumContactTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	auto* A = SpawnShellActor<ALSShellMetalBall>(World, FVector(-2000, 0, 500));
	auto* B = SpawnShellActor<ALSShellMetalBall>(World, FVector(-1859, 0, 500));
	A->Body->SetEnableGravity(false); B->Body->SetEnableGravity(false);
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	TickShellWorld(World);
	TestEqual(TEXT("Resting one-centimeter gap has not collided"), Coordinator->ContactCount, 0);
	TestTrue(TEXT("Instrument begins with a zero prephysics velocity sample"), A->Reactive->GetPreResponseVelocity().IsNearlyZero());
	TestTrue(TEXT("Production momentum queues800cm/s delta"), A->Reactive->ApplyMomentum(FVector(240000, 0, 0)));
	TestTrue(TEXT("Solver velocity has not yet consumed pending impulse"), A->Body->GetPhysicsLinearVelocity().IsNearlyZero());
	TickShellWorld(World);
	AddInfo(FString::Printf(TEXT("First impulse step contactCount=%d sampledSpeed=%.3f solverA=%s solverB=%s healthA=%.3f healthB=%.3f"),
		Coordinator->ContactCount, Coordinator->LastContactSpeed, *A->Body->GetPhysicsLinearVelocity().ToCompactString(),
		*B->Body->GetPhysicsLinearVelocity().ToCompactString(), ShellHealth(A), ShellHealth(B)));
	TestTrue(TEXT("Solver collision reached the other real body in first step"), B->Body->GetPhysicsLinearVelocity().X > 100);
	TestEqual(TEXT("First-step contact is counted before a second velocity sample"), Coordinator->ContactCount, 1);
	TestTrue(TEXT("First-step contact includes pending impulse once"), FMath::IsNearlyEqual(Coordinator->LastContactSpeed, 800.f, 2.f));
	TestTrue(TEXT("First-step symmetric common damage matches800cm/s contact"),
		FMath::IsNearlyEqual(ShellHealth(A), 570.f, .2f) && FMath::IsNearlyEqual(ShellHealth(B), 570.f, .2f));
	const FVector ConsumedVelocity = A->Body->GetPhysicsLinearVelocity();
	TickShellWorld(World);
	TestTrue(TEXT("Next sample does not add solver-consumed impulse again"),
		A->Reactive->GetPreResponseVelocity().Equals(ConsumedVelocity, 1));
	TestEqual(TEXT("Following sample does not create another event"), Coordinator->ContactCount, 1);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyAfterSampleMomentumTest, "Landship.ShellExperiment.Supply.MomentumAfterPrePhysicsSampleCountsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyAfterSampleMomentumTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	auto* A = SpawnShellActor<ALSShellMetalBall>(World, FVector(-2000, 0, 500));
	auto* B = SpawnShellActor<ALSShellMetalBall>(World, FVector(-1859, 0, 500));
	A->Body->SetEnableGravity(false); B->Body->SetEnableGravity(false);
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	TickShellWorld(World);
	// 두 Reactive의 표본 뒤이면서 물리 시뮬레이션 전인 호출 순서를 실제 틱 의존성으로 만든다.
	struct FLSShellAfterSampleTick final : FTickFunction
	{
		TFunction<void()> Callback;
		virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread,
			const FGraphEventRef& CompletionEvent) override { Callback(); }
		virtual FString DiagnosticMessage() override { return TEXT("ShellSupplyAfterSampleMomentumFixture"); }
	} AfterSample;
	int32 Applications = 0;
	AfterSample.Callback = [&]
	{
		if (Applications != 0) return;
		++Applications;
		TestTrue(TEXT("Fixture executes inside actual world PrePhysics phase"), World->bInTick && World->TickGroup == TG_PrePhysics);
		TestTrue(TEXT("Completed Reactive sample precedes momentum call"), A->Reactive->GetPreResponseVelocity().IsNearlyZero());
		TestTrue(TEXT("Solver has no velocity before this phase-specific impulse"), A->Body->GetPhysicsLinearVelocity().IsNearlyZero());
		TestTrue(TEXT("Production momentum is accepted after sampling"), A->Reactive->ApplyMomentum(FVector(240000, 0, 0)));
	};
	AfterSample.bCanEverTick = true;
	AfterSample.bStartWithTickEnabled = true;
	AfterSample.bRunOnAnyThread = false;
	AfterSample.bAllowTickBatching = false;
	AfterSample.TickGroup = TG_PrePhysics;
	AfterSample.EndTickGroup = TG_PrePhysics;
	AfterSample.AddPrerequisite(A->Reactive, A->Reactive->PrimaryComponentTick);
	AfterSample.AddPrerequisite(B->Reactive, B->Reactive->PrimaryComponentTick);
	AfterSample.SetTickFunctionEnable(true);
	AfterSample.RegisterTickFunction(World->PersistentLevel);
	ON_SCOPE_EXIT { AfterSample.UnRegisterTickFunction(); };
	TickShellWorld(World);
	AddInfo(FString::Printf(TEXT("After-sample impulse applications=%d contacts=%d speed=%.3f solverA=%s solverB=%s"),
		Applications, Coordinator->ContactCount, Coordinator->LastContactSpeed,
		*A->Body->GetPhysicsLinearVelocity().ToCompactString(), *B->Body->GetPhysicsLinearVelocity().ToCompactString()));
	TestEqual(TEXT("Phase fixture applied momentum exactly once"), Applications, 1);
	TestTrue(TEXT("Same physics step really transfers velocity to other rigid body"), B->Body->GetPhysicsLinearVelocity().X > 100);
	TestEqual(TEXT("After-sample momentum produces one first-step gameplay contact"), Coordinator->ContactCount, 1);
	TestTrue(TEXT("First contact includes the newly queued800cm/s delta"), FMath::IsNearlyEqual(Coordinator->LastContactSpeed, 800.f, 2.f));
	TestTrue(TEXT("After-sample contact applies native symmetric damage once"),
		FMath::IsNearlyEqual(ShellHealth(A), 570.f, .2f) && FMath::IsNearlyEqual(ShellHealth(B), 570.f, .2f));
	const FVector ConsumedVelocity = A->Body->GetPhysicsLinearVelocity();
	TickShellWorld(World);
	TestTrue(TEXT("Following PrePhysics sample excludes impulse consumed after prior sample"),
		A->Reactive->GetPreResponseVelocity().Equals(ConsumedVelocity, 1));
	TestEqual(TEXT("Phase fixture does not apply a second impulse"), Applications, 1);
	TestEqual(TEXT("Next sample cannot repeat the contact"), Coordinator->ContactCount, 1);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyRotatedAnchorTest, "Landship.ShellExperiment.Supply.FollowupBlastUsesRotatedLocalPointAndNormal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyRotatedAnchorTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	Coordinator->Tuning.ModuleDelay = 100;
	auto* Ball = SpawnShellActor<ALSShellMetalBall>(World, FVector(-2000, 0, 500));
	Ball->Body->SetEnableGravity(false);
	if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.First = ELSShellModule::Thrust; Shot.Second = ELSShellModule::Blast;
	Shot.Source = Coordinator->PlayerCastle;
	Shot.Origin = Ball->GetActorLocation() - FVector(500, 0, 0);
	FHitResult Hit;
	if (!TestTrue(TEXT("Production sweep establishes actual rigid surface anchor"), ALSShellProjectile::SweepFlight(
		World, Shot.Origin, Ball->GetActorLocation(), Shot.Source.Get(), Hit) && Hit.GetActor() == Ball)) return false;
	const FVector LocalPoint = Ball->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
	const FVector LocalNormal = Ball->GetActorTransform().InverseTransformVectorNoScale(Hit.ImpactNormal);
	TWeakObjectPtr<ALSShellEffectInstance> Effect = SpawnShellActor<ALSShellEffectInstance>(World, Hit.ImpactPoint);
	Effect->Initialize(Coordinator, Shot, Hit, true);
	TickShellWorld(World); TickShellWorld(World);
	// 후행 효과의 좌표 변환을 고립시키는 기술용 자세 변경이다. 회전을 유발한 플레이 입력은 아니다.
	Ball->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Ball->Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	Ball->SetActorRotation(FRotator(0, 90, 0), ETeleportType::TeleportPhysics);
	TickShellWorld(World);
	const FVector Point = Ball->GetActorTransform().TransformPosition(LocalPoint);
	const FVector Normal = Ball->GetActorTransform().TransformVectorNoScale(LocalNormal).GetSafeNormal();
	if (!TestTrue(TEXT("Pending effect follows rotated local hit point"), Effect.IsValid() && Effect->GetActorLocation().Equals(Point, .1))) return false;
	TestTrue(TEXT("Rotation changes the surface normal being tested"), FVector::DotProduct(Normal, Hit.ImpactNormal) < .1);
	const FVector BlastPoint = Point + Normal * Shot.SupplyTuning.SurfaceOffset;
	auto* Witness = SpawnShellActor<ALSShellMetalBall>(World, BlastPoint + Normal * 569);
	Witness->Body->SetEnableGravity(false);
	if (!TestTrue(TEXT("Manual trigger owns this actual pending effect"), Coordinator->CanManualTrigger())) return false;
	Coordinator->TriggerPending();
	TickShellWorld(World);
	AddInfo(FString::Printf(TEXT("Rotated anchor point=%s normal=%s edgeWitnessHealth=%.4f"),
		*Point.ToCompactString(), *Normal.ToCompactString(), ShellHealth(Witness)));
	TestTrue(TEXT("Rotated normal offsets Blast2cm outward and reaches499cm edge witness"),
		FMath::IsNearlyEqual(ShellHealth(Witness), 599.7f, .04f));
	TestEqual(TEXT("Exactly first Thrust and followup Blast execute"), Coordinator->ModuleCount, 2);
	TestFalse(TEXT("Followup consumes effect"), Effect.IsValid());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyRemovedAnchorTest, "Landship.ShellExperiment.Supply.RemovedBodyThrustUsesInitialPointButBlastKeepsLastPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyRemovedAnchorTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	for (const ELSShellModule Second : {ELSShellModule::Thrust, ELSShellModule::Blast})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		auto* Coordinator = SetUpShellWorld(World, *this);
		if (!Coordinator) return false;
		Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
		Coordinator->SupplyTuning.ThrustRadius = 500;
		Coordinator->SupplyTuning.ThrustMomentum = 240000;
		Coordinator->Tuning.ModuleDelay = 100;
		auto* Ball = SpawnShellActor<ALSShellMetalBall>(World, FVector(-2000, 0, 500));
		Ball->Body->SetEnableGravity(false);
		if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
		FLSShellShot Shot;
		Coordinator->CaptureShotContext(Shot);
		Shot.First = Second == ELSShellModule::Thrust ? ELSShellModule::Blast : ELSShellModule::Thrust;
		Shot.Second = Second; Shot.Source = Coordinator->PlayerCastle;
		Shot.Origin = Ball->GetActorLocation() - FVector(500, 0, 0);
		FHitResult Hit;
		if (!TestTrue(TEXT("Production sweep reaches removable rigid body"), ALSShellProjectile::SweepFlight(
			World, Shot.Origin, Ball->GetActorLocation(), Shot.Source.Get(), Hit) && Hit.GetActor() == Ball)) return false;
		TWeakObjectPtr<ALSShellEffectInstance> Effect = SpawnShellActor<ALSShellEffectInstance>(World, Hit.ImpactPoint);
		Effect->Initialize(Coordinator, Shot, Hit, true);
		const FVector InitialImpact = Hit.ImpactPoint;
		TickShellWorld(World); TickShellWorld(World);
		if (!TestTrue(TEXT("Followup survives first real module"), Effect.IsValid())) return false;
		// 마지막 유효 위치가 초기 피격점과 다름을 보장하는 기술용 배치다.
		Ball->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Ball->SetActorLocation(Ball->GetActorLocation() + FVector(0, 200, 0), false, nullptr, ETeleportType::TeleportPhysics);
		TickShellWorld(World);
		const FVector FrozenPoint = Effect->GetActorLocation();
		TestTrue(TEXT("Effect observed moved body before removal"), FrozenPoint.Y > 190);
		TWeakObjectPtr<ALSShellMetalBall> Removed = Ball;
		Ball->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
			ULSHealthComponent::MakeDamageRequest(ShellHealth(Ball), Coordinator->PlayerCastle, Coordinator));
		if (!TestFalse(TEXT("Common lethal damage removes physical anchor"), Removed.IsValid())) return false;
		TickShellWorld(World);
		if (Second == ELSShellModule::Thrust)
		{
			// 대상 제거 뒤에는 최초 착탄점에서 주변을 추진한다. 이동한 마지막 접점을 쓰는 폭발과 구분한다.
			if (!TestTrue(TEXT("Removed body retains pending Thrust at original impact point"),
				Effect.IsValid() && Effect->GetActorLocation().Equals(InitialImpact, .1))) return false;
			TestTrue(TEXT("Removed body retains manual second-slot authority"), Coordinator->CanManualTrigger());
			auto* A = SpawnShellActor<ALSShellMetalBall>(World, InitialImpact + FVector(0, -200, 0));
			auto* B = SpawnShellActor<ALSShellMetalBall>(World, InitialImpact + FVector(0, 200, 0));
			for (auto* Witness : {A, B})
			{
				Witness->Body->SetEnableGravity(false);
				Witness->Body->SetLinearDamping(0);
			}
			Coordinator->TriggerPending();
			TickShellWorld(World);
			TickShellWorld(World);
			const FVector ExpectedVelocity = Shot.Direction.GetSafeNormal2D() * (Shot.SupplyTuning.ThrustMomentum / 600.f);
			TestTrue(TEXT("Both sides receive the same original incidence direction and half shared budget"),
				A->Body->GetPhysicsLinearVelocity().Equals(ExpectedVelocity, .1) &&
				B->Body->GetPhysicsLinearVelocity().Equals(ExpectedVelocity, .1));
			TestEqual(TEXT("Fallback Thrust does not damage first witness"), ShellHealth(A), 600.f);
			TestEqual(TEXT("Fallback Thrust does not damage second witness"), ShellHealth(B), 600.f);
			TestTrue(TEXT("Successful point Thrust has no missing-body failure"), Coordinator->GetTargetFailureText().IsEmpty());
			TestEqual(TEXT("Manual fallback executes second module once"), Coordinator->ModuleCount, 2);
			TestFalse(TEXT("Manual fallback consumes effect"), Effect.IsValid());
			Coordinator->TriggerPending();
			TickShellWorld(World);
			TestEqual(TEXT("Repeated Space request cannot replay fallback"), Coordinator->ModuleCount, 2);
		}
		else
		{
			if (!TestTrue(TEXT("Position-based Blast remains at last valid anchor"), Effect.IsValid() && Effect->GetActorLocation().Equals(FrozenPoint, .1))) return false;
			auto* Witness = SpawnShellActor<ALSShellMetalBall>(World, FrozenPoint);
			Witness->Body->SetEnableGravity(false);
			Coordinator->TriggerPending();
			TickShellWorld(World);
			TestEqual(TEXT("Blast damages common health at preserved position"), ShellHealth(Witness), 450.f);
			TestEqual(TEXT("Last-position Blast executes once"), Coordinator->ModuleCount, 2);
			TestFalse(TEXT("Last-position Blast consumes effect"), Effect.IsValid());
		}
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyFeedbackCauseTest, "Landship.ShellExperiment.Supply.DetonationFeedbackKeepsActualDamageCause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyFeedbackCauseTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	for (int32 Case = 0; Case < 5; ++Case)
	{
		const bool bExternalFinalDamage = Case == 1;
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		auto* Coordinator = SetUpShellWorld(World, *this);
		if (!Coordinator) return false;
		Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
		auto* Barrel = SpawnShellActor<ALSShellBarrel>(World, FVector(-850, 0, 200));
		Barrel->Body->SetEnableGravity(false);
		if (Case == 4)
		{
			auto* Second = SpawnShellActor<ALSShellBarrel>(World, FVector(-850, 300, 200));
			Second->Body->SetEnableGravity(false);
		}
		if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
		FLSShellShot Shot;
		Coordinator->CaptureShotContext(Shot);
		Shot.Source = Coordinator->PlayerCastle;
		Shot.Origin = Barrel->GetActorLocation() - FVector(100, 0, 0);
		const ELSShellDamageCause Cause = Case == 2 ? ELSShellDamageCause::Shell :
			Case == 3 ? ELSShellDamageCause::Impact : ELSShellDamageCause::Module;
		if (Case == 4) Barrel->Reactive->RequestDetonation();
		else Coordinator->ApplyExperimentDamage(Barrel, bExternalFinalDamage ? 1 : 100, Shot,
			Barrel->GetActorLocation(), FVector::ForwardVector, Cause);
		if (bExternalFinalDamage)
		{
			TestEqual(TEXT("Nonfatal module leaves common health99"), ShellHealth(Barrel), 99.f);
			Barrel->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
				ULSHealthComponent::MakeDamageRequest(99, Coordinator->PlayerCastle, Coordinator));
		}
		TickShellWorld(World);
		const int32 ExpectedDetonations = Case == 4 ? 2 : 1;
		TestEqual(TEXT("Presentation metadata does not duplicate detonation"), Coordinator->DetonationCount, ExpectedDetonations);
		TestTrue(TEXT("Presentation does not defer common core health processing"),
			FMath::IsNearlyEqual(ShellHealth(Coordinator->CoreOutpost), 1000.f - ExpectedDetonations * 250.f * (1 - 250.f / 900), .1f));
		const TCHAR* Labels[] = {TEXT("포탄 폭발 → 물체 기폭"), TEXT("피해 → 물체 기폭"), TEXT("직격 → 물체 기폭"),
			TEXT("충돌 → 물체 기폭"), TEXT("주변 기폭 → 연쇄 기폭")};
		TestEqual(TEXT("Displayed cause uses fatal request rather than an earlier module"), Coordinator->GetReactionText(),
			FString(Labels[Case]));
		if (Case == 0)
		{
			const float CoreAfterDetonation = ShellHealth(Coordinator->CoreOutpost);
			// 공개 큐를 사용한 UI 우선순위 검사다. 실제 물리 충돌이나 일반 플레이의 증거가 아니다.
			Coordinator->QueueContact(Coordinator->PlayerCastle, Coordinator->CoreOutpost,
				Coordinator->CoreOutpost->GetActorLocation(), -FVector::ForwardVector,
				FVector(100, 0, 0), FVector::ZeroVector);
			TickShellWorld(World);
			TestEqual(TEXT("Low-speed contact cannot overwrite unexpired detonation cause"),
				Coordinator->GetReactionText(), FString(Labels[Case]));
			TestEqual(TEXT("Presentation-only contact does not damage core"), ShellHealth(Coordinator->CoreOutpost), CoreAfterDetonation);
			TestEqual(TEXT("Presentation threshold does not count sub300 gameplay impact"), Coordinator->ContactCount, 0);
			// 만료 뒤의 분기만 확인하도록 게임 시계를 전진시킨다. 실제 대기 시간이나 플레이 속도를 재지 않는다.
			TickShellAtTime(World, World->GetTimeSeconds() + 3.1);
			Coordinator->QueueContact(Coordinator->PlayerCastle, Coordinator->CoreOutpost,
				Coordinator->CoreOutpost->GetActorLocation(), -FVector::ForwardVector,
				FVector(100, 0, 0), FVector::ZeroVector);
			TickShellWorld(World);
			TestEqual(TEXT("Low-speed contact replaces expired detonation cause"), Coordinator->GetReactionText(), FString(TEXT("충돌")));
			TestEqual(TEXT("Expired-priority fixture still leaves core health unchanged"), ShellHealth(Coordinator->CoreOutpost), CoreAfterDetonation);
		}
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyHeavierCandidateTest, "Landship.ShellExperiment.Supply.HeavierCandidateRetainsPhysicalChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyHeavierCandidateTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	for (int32 Candidate = 0; Candidate < 2; ++Candidate)
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		auto* Coordinator = SetUpShellWorld(World, *this);
		if (!Coordinator) return false;
		Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
		Coordinator->SupplyTuning.ThrustMomentum = Candidate ? 540000.f : 240000.f;
		Coordinator->SupplyTuning.BlastMomentum = Candidate ? 540000.f : 360000.f;
		const FBox CoreBox = CastChecked<UPrimitiveComponent>(Coordinator->CoreOutpost->GetRootComponent())->Bounds.GetBox();
		if (!TestTrue(TEXT("Loaded production core really has the expected lane-facing600cm edge"),
			FMath::IsNearlyEqual(CoreBox.Min.X, -600., .1))) return false;
		auto* Floor = SpawnShellActor<AStaticMeshActor>(World, FVector(-1200, 0, -50));
		auto* FloorBody = Floor->GetStaticMeshComponent();
		FloorBody->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Floor->SetActorScale3D(FVector(40, 40, 1));
		FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		FloorBody->SetCollisionObjectType(LSCollisionChannels::Ground);
		FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
		FloorBody->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Ignore);
		auto* FloorMaterial = NewObject<UPhysicalMaterial>(Floor);
		FloorMaterial->Friction = FloorMaterial->StaticFriction = .7f;
		FloorMaterial->Restitution = .3f;
		FloorMaterial->FrictionCombineMode = EFrictionCombineMode::Min;
		FloorMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
		FloorMaterial->bOverrideFrictionCombineMode = true;
		FloorMaterial->bOverrideRestitutionCombineMode = true;
		FloorBody->SetPhysMaterialOverride(FloorMaterial);
		auto* Ball = SpawnShellActor<ALSShellMetalBall>(World, FVector(-1200, 0, 70));
		auto* Barrel = SpawnShellActor<ALSShellBarrel>(World, FVector(-850, 0, 60));
		Ball->IntrinsicMassKg = Candidate ? 900.f : 300.f;
		Ball->Restitution = Candidate ? .05f : .2f;
		TWeakObjectPtr<ALSShellMetalBall> BallLifetime = Ball;
		TWeakObjectPtr<ALSShellBarrel> BarrelLifetime = Barrel;
		if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
		const float ActualMass = Ball->Body->GetMass();
		TestTrue(TEXT("Production ConfigurePhysicalBody installs requested solver mass before simulation"),
			FMath::IsNearlyEqual(ActualMass, Ball->IntrinsicMassKg, .01f));
		for (int32 Step = 0; Step < 30; ++Step) TickShellWorld(World);
		FLSShellShot Shot;
		Coordinator->CaptureShotContext(Shot);
		Shot.First = ELSShellModule::Thrust; Shot.Second = ELSShellModule::Blast;
		Shot.Source = Coordinator->PlayerCastle;
		Shot.Origin = Ball->GetActorLocation() - FVector(500, 0, 0);
		Shot.Direction = FVector::ForwardVector;
		Shot.Damage = 50;
		FHitResult Hit;
		if (!TestTrue(TEXT("Production sweep reaches real ball before barrel or floor"), ALSShellProjectile::SweepFlight(
			World, Shot.Origin, Ball->GetActorLocation(), Shot.Source.Get(), Hit) && Hit.GetActor() == Ball)) return false;
		// 수평 입사의 기술용 명중 배치다. 마우스 조준이나 성에서 출발한 포탄 비행을 재현하지 않는다.
		Coordinator->QueueShellHit(Shot, Hit);
		const float CoreBefore = ShellHealth(Coordinator->CoreOutpost);
		const FString BallName = Ball->GetName(), BarrelName = Barrel->GetName();
		float FirstContactSpeed = -1, FirstContactBarrelHealth = -1, MaximumVerticalVelocity = 0;
		bool bFirstContactBeforeFollowup = false, bSawFollowup = false, bSampledPostFollowup = false;
		FVector PreviousVelocity = Ball->Body->GetPhysicsLinearVelocity();
		FVector PreFollowupVelocity = FVector::ZeroVector, PostFollowupVelocity = FVector::ZeroVector;
		for (int32 Step = 0; Step < 500; ++Step)
		{
			const bool bAwaitingPostFollowup = bSawFollowup && !bSampledPostFollowup;
			TickShellWorld(World);
			if (FirstContactSpeed < 0 && Coordinator->ContactCount > 0)
			{
				FirstContactSpeed = Coordinator->LastContactSpeed;
				FirstContactBarrelHealth = ShellHealth(BarrelLifetime.Get());
				bFirstContactBeforeFollowup = Coordinator->ModuleCount == 1 && BarrelLifetime.IsValid() &&
					Coordinator->LastContactActors.Contains(BallName) && Coordinator->LastContactActors.Contains(BarrelName);
			}
			if (BallLifetime.IsValid())
			{
				const FVector Velocity = Ball->Body->GetPhysicsLinearVelocity();
				MaximumVerticalVelocity = FMath::Max(MaximumVerticalVelocity, static_cast<float>(Velocity.Z));
				if (!bSawFollowup && Coordinator->ModuleCount >= 2)
				{
					PreFollowupVelocity = PreviousVelocity;
					bSawFollowup = true;
				}
				if (bAwaitingPostFollowup)
				{
					PostFollowupVelocity = Velocity;
					bSampledPostFollowup = true;
				}
				PreviousVelocity = Velocity;
			}
		}
		const float CoreAfter = ShellHealth(Coordinator->CoreOutpost);
		const float FinalBallHealth = ShellHealth(BallLifetime.Get());
		AddInfo(FString::Printf(TEXT("Physical chain technical fixture candidate=%d massKg=%.3f restitution=%.3f thrust=%.0f blast=%.0f firstContactSpeed=%.3f barrelHealthAfterFirst=%.3f firstBeforeFollowup=%d preFollowupVelocity=%s postFollowupVelocity=%s sampledPostFollowup=%d maxVerticalVelocity=%.3f detonations=%d coreBefore=%.3f coreAfter=%.3f finalBallHealth=%.3f ballRemoved=%d"),
			Candidate, ActualMass, Candidate ? .05f : .2f, Shot.SupplyTuning.ThrustMomentum, Shot.SupplyTuning.BlastMomentum,
			FirstContactSpeed, FirstContactBarrelHealth, bFirstContactBeforeFollowup, *PreFollowupVelocity.ToCompactString(),
			*PostFollowupVelocity.ToCompactString(), bSampledPostFollowup, MaximumVerticalVelocity,
			Coordinator->DetonationCount, CoreBefore, CoreAfter, FinalBallHealth, !BallLifetime.IsValid()));
		TestTrue(TEXT("Real ball-barrel contact occurs before delayed Blast"), bFirstContactBeforeFollowup && FirstContactSpeed > 0);
		TestTrue(TEXT("Uncharged first actual contact leaves barrel alive"), FirstContactBarrelHealth > 0);
		TestTrue(TEXT("Instrument records actual velocities across followup physics step"), bSawFollowup && bSampledPostFollowup);
		TestEqual(TEXT("Physical chain produces exactly one barrel detonation"), Coordinator->DetonationCount, 1);
		TestTrue(TEXT("Physical chain reduces real common core health"), CoreAfter < CoreBefore);
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyHorizontalThrustTest, "Landship.ShellExperiment.Supply.ThrustProjectsIncidenceOntoHorizontalPlane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyHorizontalThrustTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	for (const bool bVertical : {false, true})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		auto* Coordinator = SetUpShellWorld(World, *this);
		if (!Coordinator) return false;
		Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
		// 직격 충격·중력·감쇠·후행 효과를 분리한 기술용 배치다.
		Coordinator->SupplyTuning.DirectMomentum = 0;
		Coordinator->Tuning.ModuleDelay = 100;
		auto* Ball = SpawnShellActor<ALSShellMetalBall>(World, FVector(-2000, 0, 800));
		Ball->IntrinsicMassKg = 300;
		Ball->LinearDamping = 0;
		Ball->Body->SetEnableGravity(false);
		if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
		FLSShellShot Shot;
		Coordinator->CaptureShotContext(Shot);
		Shot.First = ELSShellModule::Thrust; Shot.Second = ELSShellModule::Blast;
		Shot.Source = Coordinator->PlayerCastle;
		Shot.Direction = bVertical ? -FVector::UpVector : FVector(3, 4, -12).GetSafeNormal();
		Shot.Origin = Ball->GetActorLocation() - Shot.Direction * 500;
		Shot.Damage = 50;
		FHitResult Hit;
		if (!TestTrue(TEXT("Actual oblique or vertical sweep reaches intended rigid body"), ALSShellProjectile::SweepFlight(
			World, Shot.Origin, Ball->GetActorLocation(), Shot.Source.Get(), Hit) && Hit.GetActor() == Ball)) return false;
		Coordinator->QueueShellHit(Shot, Hit);
		TestTrue(TEXT("Queued hit has not applied momentum before frame-end processing"), Ball->Body->GetPhysicsLinearVelocity().IsNearlyZero());
		TickShellWorld(World);
		TickShellWorld(World);
		const FVector Velocity = Ball->Body->GetPhysicsLinearVelocity();
		AddInfo(FString::Printf(TEXT("Horizontal thrust verticalIncidence=%d incidence=%s actualVelocity=%s health=%.3f failure=%s"),
			bVertical, *Shot.Direction.ToCompactString(), *Velocity.ToCompactString(), ShellHealth(Ball), *Coordinator->GetTargetFailureText()));
		TestEqual(TEXT("Thrust-first queued shell impact preserves common health"), ShellHealth(Ball), 600.f);
		TestEqual(TEXT("Exactly one shell hit reaches frame-end queue"), Coordinator->ShellHitCount, 1);
		TestEqual(TEXT("Long-delay fixture executes only first Thrust"), Coordinator->ModuleCount, 1);
		if (bVertical)
		{
			TestTrue(TEXT("Pure vertical incidence cannot invent a horizontal direction"), Velocity.IsNearlyZero(.01));
			TestEqual(TEXT("Pure vertical Thrust distinguishes missing direction from missing body"),
				Coordinator->GetTargetFailureText(), FString(TEXT("추진 방향 없음")));
		}
		else
		{
			const FVector Expected = FVector(.6, .8, 0) * (Shot.SupplyTuning.ThrustMomentum / 300.f);
			TestTrue(TEXT("Oblique Thrust uses full horizontal impulse in3to4direction without vertical momentum"), Velocity.Equals(Expected, .1));
			TestTrue(TEXT("Valid horizontal direction does not report target failure"), Coordinator->GetTargetFailureText().IsEmpty());
		}
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyBlastAxisTest, "Landship.ShellExperiment.Supply.ModuleBlastLiftsButDetonationKeepsRadialBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyBlastAxisTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	for (const bool bDetonation : {false, true})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		auto* Coordinator = SetUpShellWorld(World, *this);
		if (!Coordinator) return false;
		Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
		// 솔버 속도로 모듈 축과 총예산을 읽도록 직격 충격·중력·감쇠·후행 효과만 분리한다.
		Coordinator->SupplyTuning.DirectMomentum = 0;
		Coordinator->Tuning.ModuleDelay = 100;
		auto* A = SpawnShellActor<ALSShellMetalBall>(World, FVector(bDetonation ? -2300 : -2000, 0, 800));
		auto* B = SpawnShellActor<ALSShellMetalBall>(World, FVector(bDetonation ? -1700 : -2300, 0, 800));
		for (auto* Ball : {A, B})
		{
			Ball->IntrinsicMassKg = 300;
			Ball->LinearDamping = 0;
			Ball->Body->SetEnableGravity(false);
		}
		auto* Emitter = bDetonation ? SpawnShellActor<ALSShellBarrel>(World, FVector(-2000, 0, 800)) : nullptr;
		if (Emitter) Emitter->Body->SetEnableGravity(false);
		if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
		FLSShellShot Shot;
		Coordinator->CaptureShotContext(Shot);
		Shot.First = ELSShellModule::Blast; Shot.Second = ELSShellModule::Thrust;
		Shot.Source = Coordinator->PlayerCastle;
		Shot.Direction = FVector::ForwardVector;
		Shot.Damage = 50;
		FVector Origin;
		float Radius, Damage, Budget;
		if (bDetonation)
		{
			Origin = Emitter->GetActorLocation();
			Radius = Emitter->Reactive->DetonationRadius;
			Damage = Emitter->Reactive->DetonationDamage;
			Budget = Emitter->Reactive->DetonationVelocity * Shot.SupplyTuning.ReferenceMassKg;
			if (!TestTrue(TEXT("Native living barrel queues its environmental detonation"), Emitter->Reactive->RequestDetonation())) return false;
		}
		else
		{
			// 두 구체 사이의 빈 공간에서 A의 표면에 명중시켜 B까지 실제 차폐 없는 노출을 만든다.
			Shot.Origin = FVector(-2150, 0, 800);
			FHitResult Hit;
			if (!TestTrue(TEXT("Production shell sweep reaches actual Blast anchor"), ALSShellProjectile::SweepFlight(
				World, Shot.Origin, A->GetActorLocation(), Shot.Source.Get(), Hit) && Hit.GetActor() == A)) return false;
			Origin = Hit.ImpactPoint + Hit.ImpactNormal.GetSafeNormal() * Shot.SupplyTuning.SurfaceOffset;
			Radius = Shot.BaseTuning.BlastRadius;
			Damage = Shot.BaseTuning.BlastDamage;
			Budget = Shot.SupplyTuning.BlastMomentum;
			Coordinator->QueueShellHit(Shot, Hit);
		}
		auto* Cover = SpawnShellActor<AStaticMeshActor>(World, Origin + FVector(0, 200, 0));
		auto* CoverBody = Cover->GetStaticMeshComponent();
		// BeginPlay 뒤 Static 컴포넌트의 메시 교체는 엔진이 거부한다. 이 고정 배치는 시뮬레이션하지 않는다.
		CoverBody->SetMobility(EComponentMobility::Movable);
		if (!TestTrue(TEXT("Runtime cover fixture actually accepts its collision mesh"), CoverBody->SetStaticMesh(
			LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"))))) return false;
		Cover->SetActorScale3D(FVector(2, .4, 2));
		CoverBody->SetCollisionProfileName(TEXT("BlockAll"));
		CoverBody->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Block);
		auto* Shielded = SpawnShellActor<ALSShellMetalBall>(World, Origin + FVector(0, 400, 0));
		Shielded->Body->SetEnableGravity(false);
		FCollisionQueryParams CoverQuery(SCENE_QUERY_STAT(LSShellBlastAxisFixture), false, Shielded);
		if (Emitter) CoverQuery.AddIgnoredComponent(Emitter->Body.Get());
		FHitResult CoverHit;
		const bool bTraceBlocked = World->LineTraceSingleByChannel(CoverHit,
			Shielded->GetActorLocation() - FVector(0, Shielded->Radius, 0), Origin,
			LSCollisionChannels::ExplosionOcclusion, CoverQuery);
		AddInfo(FString::Printf(TEXT("Blast cover fixture traceBlocked=%d firstActor=%s objectType=%d channelResponse=%d queryEnabled=%d point=%s"),
			bTraceBlocked, *GetNameSafe(CoverHit.GetActor()), static_cast<int32>(CoverBody->GetCollisionObjectType()),
			static_cast<int32>(CoverBody->GetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion)),
			CoverBody->IsQueryCollisionEnabled(), *CoverHit.ImpactPoint.ToCompactString()));
		if (!TestTrue(TEXT("Instrument proves real explosion-channel trace hits cover before queued explosion"),
			bTraceBlocked && CoverHit.GetComponent() == CoverBody && CoverBody->GetCollisionObjectType() == ECC_WorldStatic &&
			CoverBody->GetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion) == ECR_Block &&
			CoverBody->IsQueryCollisionEnabled())) return false;
		const float ScaleA = 1.f - (FVector::Distance(Origin, A->GetActorLocation()) - A->Radius) / Radius;
		const float ScaleB = 1.f - (FVector::Distance(Origin, B->GetActorLocation()) - B->Radius) / Radius;
		const FVector IncomingA = ((A->GetActorLocation() - Origin).GetSafeNormal2D() +
			FVector::UpVector * Shot.SupplyTuning.BlastLiftRatio).GetSafeNormal();
		const FVector IncomingB = ((B->GetActorLocation() - Origin).GetSafeNormal2D() +
			FVector::UpVector * Shot.SupplyTuning.BlastLiftRatio).GetSafeNormal();
		if (!TestTrue(TEXT("Two exposed survivors share a saturated impulse budget"), ScaleA > 0 && ScaleB > 0 && ScaleA + ScaleB > 1)) return false;
		TickShellWorld(World);
		TickShellWorld(World);
		const FVector VelocityA = A->Body->GetPhysicsLinearVelocity(), VelocityB = B->Body->GetPhysicsLinearVelocity();
		const float ImpulseA = VelocityA.Size() * A->Body->GetMass(), ImpulseB = VelocityB.Size() * B->Body->GetMass();
		AddInfo(FString::Printf(TEXT("Blast axis detonation=%d origin=%s velocityA=%s velocityB=%s impulseA=%.3f impulseB=%.3f budget=%.3f healthA=%.3f healthB=%.3f"),
			bDetonation, *Origin.ToCompactString(), *VelocityA.ToCompactString(), *VelocityB.ToCompactString(),
			ImpulseA, ImpulseB, Budget, ShellHealth(A), ShellHealth(B)));
		TestTrue(TEXT("Changing axis preserves total scalar momentum budget"), FMath::IsNearlyEqual(ImpulseA + ImpulseB, Budget, 100.f));
		TestTrue(TEXT("Distance weights still divide shared budget"), FMath::IsNearlyEqual(ImpulseA / ImpulseB, ScaleA / ScaleB, .002f));
		TestTrue(TEXT("A retains common shell and distance-attenuated blast damage"),
			FMath::IsNearlyEqual(ShellHealth(A), 600.f - (bDetonation ? 0.f : 50.f) - Damage * ScaleA, .1f));
		TestTrue(TEXT("B retains distance-attenuated blast damage"), FMath::IsNearlyEqual(ShellHealth(B), 600.f - Damage * ScaleB, .1f));
		FLSDamageResult DamageA, DamageB;
		if (!TestTrue(TEXT("Common health retains actual blast damage records for both receivers"),
			A->FindComponentByClass<ULSHealthComponent>()->TryGetLastDamage(DamageA) &&
			B->FindComponentByClass<ULSHealthComponent>()->TryGetLastDamage(DamageB))) return false;
		AddInfo(FString::Printf(TEXT("Blast common damage detonation=%d incomingA=%s incomingB=%s appliedA=%.3f appliedB=%.3f"),
			bDetonation, *DamageA.IncomingDirection.ToCompactString(), *DamageB.IncomingDirection.ToCompactString(),
			DamageA.AppliedDamage, DamageB.AppliedDamage));
		TestTrue(TEXT("Latest records are attenuated explosion damage rather than initial direct shell damage"),
			FMath::IsNearlyEqual(DamageA.AppliedDamage, Damage * ScaleA, .1f) &&
			FMath::IsNearlyEqual(DamageB.AppliedDamage, Damage * ScaleB, .1f));
		TestTrue(TEXT("Common damage preserves radial incoming direction and lift ratio independently of impulse axis"),
			DamageA.bHasIncomingDirection && DamageB.bHasIncomingDirection &&
			DamageA.IncomingDirection.Equals(IncomingA, .001) && DamageB.IncomingDirection.Equals(IncomingB, .001));
		TestEqual(TEXT("Actual cover still shields in-radius body from health damage"), ShellHealth(Shielded), 600.f);
		TestTrue(TEXT("Shielded body receives no momentum and consumes no budget"), Shielded->Body->GetPhysicsLinearVelocity().IsNearlyZero(.01));
		TestTrue(TEXT("Occlusion instrument records real blocking geometry"), Coordinator->GetShieldPoints().Num() > 0);
		if (bDetonation)
		{
			const FVector ExpectedA = (FVector(-1, 0, Shot.SupplyTuning.BlastLiftRatio)).GetSafeNormal();
			const FVector ExpectedB = (FVector(1, 0, Shot.SupplyTuning.BlastLiftRatio)).GetSafeNormal();
			TestTrue(TEXT("Environmental detonation keeps opposite outward radial directions and lift ratio"),
				VelocityA.GetSafeNormal().Equals(ExpectedA, .001) && VelocityB.GetSafeNormal().Equals(ExpectedB, .001));
			TestEqual(TEXT("Native barrel detonates once"), Coordinator->DetonationCount, 1);
		}
		else
		{
			TestTrue(TEXT("Module Blast gives both exposed bodies only upward momentum"),
				VelocityA.GetSafeNormal().Equals(FVector::UpVector, .001) && VelocityB.GetSafeNormal().Equals(FVector::UpVector, .001));
			TestEqual(TEXT("Module Blast does not become environmental detonation"), Coordinator->DetonationCount, 0);
			TestEqual(TEXT("Module axis test actually used queued shell impact"), Coordinator->ShellHitCount, 1);
		}
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyModuleDeltaSpeedLimitTest, "Landship.ShellExperiment.Supply.ModuleDeltaSpeedLimitKeepsMotionSnapshotAndUnsharedExcess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyModuleDeltaSpeedLimitTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	for (int32 Case = 0; Case < 4; ++Case)
	{
		const bool bBlast = Case == 1, bEnvironmental = Case == 2, bDirect = Case == 3;
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		auto* Coordinator = SetUpShellWorld(World, *this);
		if (!Coordinator) return false;
		Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
		Coordinator->SupplyTuning.ModuleMaximumDeltaSpeed = 1500;
		Coordinator->SupplyTuning.ThrustMomentum = 240000;
		Coordinator->SupplyTuning.BlastMomentum = 360000;
		// 모듈 검사에서는 직격 충격을 끈다. 직격 전용 검사는 120000으로 상한보다 큰 충격을 분리한다.
		Coordinator->SupplyTuning.DirectMomentum = bDirect ? 120000.f : 0.f;
		Coordinator->Tuning.ModuleDelay = 100;
		auto* Light = SpawnShellActor<ALSShellMetalBall>(World, FVector(bEnvironmental ? -1700 : -2000, 0, 800));
		Light->IntrinsicMassKg = 60;
		Light->LinearDamping = 0;
		Light->Body->SetEnableGravity(false);
		auto* Heavy = bBlast ? SpawnShellActor<ALSShellMetalBall>(World, FVector(-2300, 0, 800)) : nullptr;
		if (Heavy)
		{
			Heavy->IntrinsicMassKg = 900;
			Heavy->LinearDamping = 0;
			Heavy->Body->SetEnableGravity(false);
		}
		auto* Emitter = bEnvironmental ? SpawnShellActor<ALSShellBarrel>(World, FVector(-2000, 0, 800)) : nullptr;
		if (Emitter) Emitter->Body->SetEnableGravity(false);
		if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
		// 기존 운동에 더하는 변화량을 읽는 기술용 초기 속도다. 중력·감쇠·후행 모듈은 분리한다.
		const FVector InitialVelocity(100, 0, 0);
		Light->Body->SetPhysicsLinearVelocity(InitialVelocity);
		if (Heavy) Heavy->Body->SetPhysicsLinearVelocity(InitialVelocity);
		FLSShellShot Shot;
		Coordinator->CaptureShotContext(Shot);
		Shot.Source = Coordinator->PlayerCastle;
		Shot.First = bBlast ? ELSShellModule::Blast : ELSShellModule::Thrust;
		Shot.Second = bBlast ? ELSShellModule::Thrust : ELSShellModule::Blast;
		Shot.Direction = bDirect ? FVector::UpVector : FVector::ForwardVector;
		Shot.Origin = bBlast ? FVector(-2150, 0, 800) : Light->GetActorLocation() - Shot.Direction * 500;
		Shot.Damage = 50;
		// 캡처 뒤의 실시간 편집이 이미 발사한 모듈의 1500 상한을 바꾸지 않아야 한다.
		Coordinator->SupplyTuning.ModuleMaximumDeltaSpeed = 10;
		FVector ExplosionOrigin = FVector::ZeroVector;
		if (bEnvironmental)
		{
			ExplosionOrigin = Emitter->GetActorLocation();
			if (!TestTrue(TEXT("Native barrel queues uncapped environmental detonation"), Emitter->Reactive->RequestDetonation())) return false;
		}
		else
		{
			FHitResult Hit;
			if (!TestTrue(TEXT("Limit fixture uses actual shell sweep to the light physical body"), ALSShellProjectile::SweepFlight(
				World, Shot.Origin, Light->GetActorLocation(), Shot.Source.Get(), Hit) && Hit.GetActor() == Light)) return false;
			ExplosionOrigin = Hit.ImpactPoint + Hit.ImpactNormal.GetSafeNormal() * Shot.SupplyTuning.SurfaceOffset;
			Coordinator->QueueShellHit(Shot, Hit);
		}
		TickShellWorld(World);
		// 폭발을 소진한 프레임의 실제 몸체 위치로 거리 가중치를 읽는다. 충격은 다음 물리 스텝에서 소비된다.
		const float LightScale = 1.f - (FVector::Distance(ExplosionOrigin, Light->GetActorLocation()) - Light->Radius) /
			(bEnvironmental ? 900.f : Shot.BaseTuning.BlastRadius);
		const float HeavyScale = Heavy ? 1.f - (FVector::Distance(ExplosionOrigin, Heavy->GetActorLocation()) - Heavy->Radius) /
			Shot.BaseTuning.BlastRadius : 0.f;
		TickShellWorld(World);
		const FVector Velocity = Light->Body->GetPhysicsLinearVelocity();
		const FVector DeltaVelocity = Velocity - InitialVelocity;
		AddInfo(FString::Printf(TEXT("Light-body module limit case=%d actualMass=%.3f shotCap=%.3f liveCap=%.3f initialVelocity=%s actualVelocity=%s deltaVelocity=%s"),
			Case, Light->Body->GetMass(), Shot.SupplyTuning.ModuleMaximumDeltaSpeed,
			Coordinator->SupplyTuning.ModuleMaximumDeltaSpeed, *InitialVelocity.ToCompactString(),
			*Velocity.ToCompactString(), *DeltaVelocity.ToCompactString()));
		if (bEnvironmental)
		{
			const float ExpectedDeltaSpeed = 140000.f * LightScale / 60.f;
			const FVector ExpectedDirection = FVector(1, 0, Shot.SupplyTuning.BlastLiftRatio).GetSafeNormal();
			TestTrue(TEXT("Environmental detonation retains additional speed above module limit"),
				ExpectedDeltaSpeed > 1500 && DeltaVelocity.Equals(ExpectedDirection * ExpectedDeltaSpeed, .2));
			TestEqual(TEXT("Native environmental path resolves one detonation"), Coordinator->DetonationCount, 1);
		}
		else if (bDirect)
		{
			TestTrue(TEXT("Direct momentum is not clamped by module limit and preserves existing movement"),
				Velocity.Equals(InitialVelocity + FVector::UpVector * 2000, .1));
			TestFalse(TEXT("Vertical Thrust fails without adding a module impulse"), Coordinator->GetTargetFailureText().IsEmpty());
		}
		else
		{
			const FVector ExpectedAxis = bBlast ? FVector::UpVector : FVector::ForwardVector;
			TestTrue(TEXT("Module adds captured1500delta without replacing existing velocity"),
				Velocity.Equals(InitialVelocity + ExpectedAxis * 1500, .1));
			TestTrue(TEXT("Additional module speed does not exceed captured cap"), DeltaVelocity.Size() <= 1500.1);
			if (Heavy)
			{
				const float ExpectedHeavyImpulse = Shot.SupplyTuning.BlastMomentum * HeavyScale / (LightScale + HeavyScale);
				const FVector HeavyDelta = Heavy->Body->GetPhysicsLinearVelocity() - InitialVelocity;
				AddInfo(FString::Printf(TEXT("Limited Blast heavyDelta=%s expectedUnredistributedImpulse=%.3f actualTotalImpulse=%.3f budget=%.3f"),
					*HeavyDelta.ToCompactString(), ExpectedHeavyImpulse, DeltaVelocity.Size() * 60 + HeavyDelta.Size() * 900,
					Shot.SupplyTuning.BlastMomentum));
				TestTrue(TEXT("Excess light-body impulse is not redistributed to heavy exposed target"),
					HeavyDelta.Equals(FVector::UpVector * (ExpectedHeavyImpulse / 900.f), .1));
				TestTrue(TEXT("Discarded excess leaves total applied impulse below original blast budget"),
					DeltaVelocity.Size() * 60 + HeavyDelta.Size() * 900 < Shot.SupplyTuning.BlastMomentum - 10000);
			}
		}
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyThrustFirstDamageTest, "Landship.ShellExperiment.Supply.ThrustFirstShotPreservesWeakTargetUntilFollowupBlast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyThrustFirstDamageTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	for (int32 Case = 0; Case < 3; ++Case)
	{
		const bool bLegacy = Case == 2, bBlastFirst = Case == 1;
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		auto* Coordinator = SetUpShellWorld(World, *this);
		if (!Coordinator) return false;
		Coordinator->RuleProfile = bLegacy ? ELSShellRuleProfile::Legacy : ELSShellRuleProfile::SupplyStageOne;
		// 첫 직격 정책을 고립시키는 기술용 배치다. 후행은 수동 발동하며 AI 보행과 중력은 분리한다.
		Coordinator->Tuning.ModuleDelay = 100;
		Coordinator->Tuning.GravityAcceleration = 0;
		// Blast 선행 사례만 모듈 피해를 꺼 직격50의 공통 체력 기록을 따로 관찰한다.
		if (bBlastFirst) Coordinator->Tuning.BlastDamage = 0;
		auto* Enemy = SpawnConfiguredShellEnemy(World, FVector(-2000, 0, 200), ELSShellEnemyKind::Assault);
		if (!TestNotNull(TEXT("Production100HP Assault fixture"), Enemy) || !BeginShellWorld(TestWorld, *this, Coordinator)) return false;
		Enemy->Behavior->SetComponentTickEnabled(false);
		Enemy->GetCharacterMovement()->StopMovementImmediately();
		Enemy->GetCharacterMovement()->GravityScale = 0;
		Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		if (bBlastFirst)
		{
			Coordinator->SetComposing(true);
			Coordinator->SwapModules();
			Coordinator->SetComposing(false);
		}
		const ELSShellModule FiredFirst = bLegacy ? ELSShellModule::Gravity :
			bBlastFirst ? ELSShellModule::Blast : ELSShellModule::Thrust;
		if (!TestEqual(TEXT("Real firing pair starts with intended module"), Coordinator->GetModule(0), FiredFirst)) return false;
		TestEqual(TEXT("Weak target starts with production common health"), ShellHealth(Enemy), 100.f);
		if (!TestTrue(TEXT("Living weak target has its actual query-enabled capsule"),
			Enemy->GetCapsuleComponent()->IsQueryCollisionEnabled())) return false;
		const FVector InitialLocation = Enemy->GetActorLocation();
		if (!TestTrue(TEXT("Public FireAt launches real shell toward weak target"), Coordinator->FireAt(InitialLocation))) return false;
		Coordinator->SetComposing(true);
		Coordinator->SwapModules();
		Coordinator->SetComposing(false);
		TestTrue(TEXT("Composition changes next shot while original shell is in flight"), Coordinator->GetModule(0) != FiredFirst);
		for (int32 Step = 0; Step < 250 && Coordinator->ShellHitCount == 0; ++Step) TickShellWorld(World);
		if (!TestEqual(TEXT("Real shell flight resolves exactly one actual impact"), Coordinator->ShellHitCount, 1)) return false;
		TestEqual(TEXT("100second delay keeps first-impact observation before second module"), Coordinator->ModuleCount, 1);
		if (!TestTrue(TEXT("Actual first module leaves weak target present"), IsValid(Enemy))) return false;
		const float ExpectedHealth = Case == 0 ? 100.f : 50.f;
		TestEqual(TEXT("Immutable firing profile and first slot decide direct health policy"), ShellHealth(Enemy), ExpectedHealth);
		AddInfo(FString::Printf(TEXT("Thrust-first damage case=%d firedFirst=%d currentFirst=%d healthAfterImpact=%.3f moduleCount=%d"),
			Case, static_cast<int32>(FiredFirst), static_cast<int32>(Coordinator->GetModule(0)), ShellHealth(Enemy), Coordinator->ModuleCount));
		if (Case == 0)
		{
			TestFalse(TEXT("Neither Thrust-first direct impact nor Thrust module commits health damage"),
				Enemy->FindComponentByClass<ULSHealthComponent>()->HasTakenDamage());
			TickShellWorld(World);
			TestTrue(TEXT("Health-free Thrust still produces real target displacement and velocity"),
				FVector::Dist2D(Enemy->GetActorLocation(), InitialLocation) > 1 && Enemy->GetCharacterMovement()->Velocity.Size2D() > 100);
			TestEqual(TEXT("Short isolated observation contains no secondary gameplay collision"), Coordinator->ContactCount, 0);
			TestEqual(TEXT("Isolated target motion has not reached remote core"), ShellHealth(Coordinator->CoreOutpost), 1000.f);
			if (!TestTrue(TEXT("Original followup remains manually available"), Coordinator->CanManualTrigger())) return false;
			TWeakObjectPtr<ALSShellExperimentEnemy> TargetLifetime = Enemy;
			Coordinator->TriggerPending();
			TickShellWorld(World);
			TestEqual(TEXT("Original second Blast executes despite later composition change"), Coordinator->ModuleCount, 2);
			TestFalse(TEXT("Followup Blast retains lethal common damage against weak target"), TargetLifetime.IsValid());
		}
		else
		{
			FLSDamageResult DirectDamage;
			if (!TestTrue(TEXT("Blast-first and Legacy preserve common direct damage record"),
				Enemy->FindComponentByClass<ULSHealthComponent>()->TryGetLastDamage(DirectDamage))) return false;
			TestEqual(TEXT("Blast-first and Legacy direct damage remain50"), DirectDamage.AppliedDamage, 50.f);
		}
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyDeadAssaultPointThrustTest, "Landship.ShellExperiment.Supply.DeadAssaultAutomaticallyThrustsFromOriginalImpact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyDeadAssaultPointThrustTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpShellWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	Coordinator->SupplyTuning.ThrustRadius = 500;
	Coordinator->SupplyTuning.ThrustMomentum = 240000;
	Coordinator->Tuning.ModuleDelay = .1f;
	auto* Enemy = SpawnConfiguredShellEnemy(World, FVector(-2000, 0, 800), ELSShellEnemyKind::Assault);
	if (!TestNotNull(TEXT("Actual100HP Assault"), Enemy) || !BeginShellWorld(TestWorld, *this, Coordinator)) return false;
	Enemy->Behavior->SetComponentTickEnabled(false);
	Enemy->GetCharacterMovement()->GravityScale = 0;
	Enemy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	TestEqual(TEXT("Original target has production100HP before first Blast"), ShellHealth(Enemy), 100.f);
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.First = ELSShellModule::Blast; Shot.Second = ELSShellModule::Thrust;
	Shot.Source = Coordinator->PlayerCastle;
	Shot.Direction = FVector(3, 4, -12).GetSafeNormal();
	Shot.Origin = Enemy->GetActorLocation() - Shot.Direction * 500;
	FHitResult Hit;
	if (!TestTrue(TEXT("Production sweep establishes actual first impact on weak target"), ALSShellProjectile::SweepFlight(
		World, Shot.Origin, Enemy->GetActorLocation(), Shot.Source.Get(), Hit) && Hit.GetActor() == Enemy)) return false;
	const FVector InitialPoint = Hit.ImpactPoint;
	const FVector ExpectedDirection = Shot.Direction.GetSafeNormal2D();
	TWeakObjectPtr<ALSShellEffectInstance> Effect = SpawnShellActor<ALSShellEffectInstance>(World, InitialPoint);
	TWeakObjectPtr<ALSShellExperimentEnemy> OriginalTarget = Enemy;
	Effect->Initialize(Coordinator, Shot, Hit, true);
	TickShellWorld(World);
	if (!TestFalse(TEXT("First real Blast removes100HP Assault through common death"), OriginalTarget.IsValid())) return false;
	if (!TestTrue(TEXT("Dead target retains pending automatic and Space authority"), Effect.IsValid() && Coordinator->CanManualTrigger())) return false;
	// 첫 폭발 뒤 관측용 몸체를 놓아 후행 추진의 무피해와 배분만 측정한다.
	auto* A = SpawnShellActor<ALSShellMetalBall>(World, InitialPoint + FVector(0, -200, 0));
	auto* B = SpawnShellActor<ALSShellMetalBall>(World, InitialPoint + FVector(0, 200, 0));
	auto* Shielded = SpawnShellActor<ALSShellMetalBall>(World, InitialPoint + FVector(0, 0, 400));
	auto* Outside = SpawnShellActor<ALSShellMetalBall>(World, InitialPoint + FVector(0, 400, 700));
	for (auto* Ball : {A, B, Shielded, Outside})
	{
		Ball->Body->SetEnableGravity(false);
		Ball->Body->SetLinearDamping(0);
	}
	auto* Cover = SpawnShellActor<AStaticMeshActor>(World, InitialPoint + FVector(0, 0, 200));
	auto* CoverBody = Cover->GetStaticMeshComponent();
	CoverBody->SetMobility(EComponentMobility::Movable);
	if (!TestTrue(TEXT("Runtime fallback cover accepts actual collision mesh"), CoverBody->SetStaticMesh(
		LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"))))) return false;
	Cover->SetActorScale3D(FVector(2, 2, .4));
	CoverBody->SetCollisionProfileName(TEXT("BlockAll"));
	CoverBody->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Block);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSShellPointThrustFixture), false, Shielded);
	FHitResult CoverHit;
	if (!TestTrue(TEXT("Fallback instrument proves explosion-channel cover before timer fires"), World->LineTraceSingleByChannel(
		CoverHit, Shielded->GetActorLocation() - FVector(0, 0, Shielded->Radius), InitialPoint,
		LSCollisionChannels::ExplosionOcclusion, Query) && CoverHit.GetComponent() == CoverBody)) return false;
	// 캡처 뒤 수치·방향을 바꿔도 원래 반경·예산·입사 방향과 타이머를 사용해야 한다.
	Coordinator->SupplyTuning.ThrustRadius = 10;
	Coordinator->SupplyTuning.ThrustMomentum = 1;
	Coordinator->SupplyTuning.ModuleMaximumDeltaSpeed = 1;
	Coordinator->Tuning.ModuleDelay = 100;
	Shot.Direction = -FVector::ForwardVector;
	for (int32 Step = 0; Step < 20 && Coordinator->ModuleCount < 2; ++Step) TickShellWorld(World);
	TickShellWorld(World);
	const FVector VelocityA = A->Body->GetPhysicsLinearVelocity(), VelocityB = B->Body->GetPhysicsLinearVelocity();
	AddInfo(FString::Printf(TEXT("Automatic point thrust initial=%s expectedDirection=%s velocityA=%s velocityB=%s impulseSum=%.3f modules=%d"),
		*InitialPoint.ToCompactString(), *ExpectedDirection.ToCompactString(), *VelocityA.ToCompactString(),
		*VelocityB.ToCompactString(), VelocityA.Size() * A->Body->GetMass() + VelocityB.Size() * B->Body->GetMass(), Coordinator->ModuleCount));
	TestEqual(TEXT("Automatic fallback executes second module once"), Coordinator->ModuleCount, 2);
	TestTrue(TEXT("Bodies on both sides move in original common incidence direction rather than radially"),
		VelocityA.Equals(ExpectedDirection * 400, .1) && VelocityB.Equals(ExpectedDirection * 400, .1));
	TestTrue(TEXT("Exposed targets share captured total thrust budget"), FMath::IsNearlyEqual(
		VelocityA.Size() * A->Body->GetMass() + VelocityB.Size() * B->Body->GetMass(), 240000.f, 100.f));
	for (auto* Ball : {A, B, Shielded, Outside}) TestEqual(TEXT("Point Thrust commits no witness health damage"), ShellHealth(Ball), 600.f);
	TestTrue(TEXT("Actual cover excludes shielded target from thrust"), Shielded->Body->GetPhysicsLinearVelocity().IsNearlyZero(.01));
	TestTrue(TEXT("Full3D distance excludes vertically distant target despite nearby planar position"), Outside->Body->GetPhysicsLinearVelocity().IsNearlyZero(.01));
	TestTrue(TEXT("Fallback records actual shielding"), Coordinator->GetShieldPoints().Num() > 0);
	TestFalse(TEXT("Automatic fallback consumes effect and manual authority"), Effect.IsValid() || Coordinator->CanManualTrigger());
	const int32 Modules = Coordinator->ModuleCount;
	Coordinator->TriggerPending();
	TickShellWorld(World);
	TestEqual(TEXT("Space after automatic activation cannot repeat it"), Coordinator->ModuleCount, Modules);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyEmptyPointThrustTest, "Landship.ShellExperiment.Supply.EmptyPointThrustShowsActivationAndStopsAfterOutcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyEmptyPointThrustTest::RunTest(const FString& Parameters)
{
	FLSShellTestDamagePolicy Policy;
	for (const bool bFinalize : {false, true})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		auto* Coordinator = SetUpShellWorld(World, *this);
		if (!Coordinator) return false;
		Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
		Coordinator->Tuning.ModuleDelay = .1f;
		auto* Ground = SpawnShellActor<AStaticMeshActor>(World, FVector(-2000, 0, -50));
		auto* GroundBody = Ground->GetStaticMeshComponent();
		GroundBody->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Ground->SetActorScale3D(FVector(10, 10, 1));
		GroundBody->SetCollisionProfileName(TEXT("BlockAll"));
		GroundBody->SetCollisionObjectType(LSCollisionChannels::Ground);
		GroundBody->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Ignore);
		if (!BeginShellWorld(TestWorld, *this, Coordinator)) return false;
		FLSShellShot Shot;
		Coordinator->CaptureShotContext(Shot);
		Shot.First = ELSShellModule::Blast; Shot.Second = ELSShellModule::Thrust;
		Shot.Source = Coordinator->PlayerCastle;
		Shot.Origin = FVector(-2200, 0, 500);
		const FVector Aim(-2000, 0, -10);
		Shot.Direction = (Aim - Shot.Origin).GetSafeNormal();
		FHitResult Hit;
		if (!TestTrue(TEXT("Production sweep hits ground without a combat-body anchor"), ALSShellProjectile::SweepFlight(
			World, Shot.Origin, Aim, Shot.Source.Get(), Hit) && Hit.GetActor() == Ground)) return false;
		TWeakObjectPtr<ALSShellEffectInstance> Effect = SpawnShellActor<ALSShellEffectInstance>(World, Hit.ImpactPoint);
		Effect->Initialize(Coordinator, Shot, Hit, false);
		TickShellWorld(World);
		if (!TestTrue(TEXT("Initial ground hit retains pending point Thrust without target body"),
			Effect.IsValid() && Coordinator->CanManualTrigger())) return false;
		ALSShellMetalBall* Witness = nullptr;
		if (bFinalize)
		{
			// 종료 직전 몸체를 놓아 기존 후행 예약과 새 큐가 모두 운동을 주지 않는지 측정한다.
			Witness = SpawnShellActor<ALSShellMetalBall>(World, Hit.ImpactPoint + FVector(0, 200, 100));
			Witness->Body->SetEnableGravity(false);
			Coordinator->PlayerCastle->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
				ULSHealthComponent::MakeDamageRequest(ShellHealth(Coordinator->PlayerCastle), nullptr, nullptr));
			TickShellWorld(World);
			Coordinator->QueuePointThrust(Hit.ImpactPoint, Shot);
			Coordinator->TriggerPending();
		}
		for (int32 Step = 0; Step < 20; ++Step) TickShellWorld(World);
		TestFalse(TEXT("Timer or finalized outcome releases effect and manual authority"), Effect.IsValid() || Coordinator->CanManualTrigger());
		if (bFinalize)
		{
			TestFalse(TEXT("Common castle death finalizes battle"), Coordinator->IsBattleActive());
			TestEqual(TEXT("Outcome stops reserved and newly queued point Thrust"), Coordinator->ModuleCount, 1);
			TestTrue(TEXT("Post-outcome target receives no pending momentum"), Witness->Body->GetPhysicsLinearVelocity().IsNearlyZero(.01));
			TestEqual(TEXT("Post-outcome target receives no damage"), ShellHealth(Witness), 600.f);
		}
		else
		{
			TestEqual(TEXT("Empty point still executes second module"), Coordinator->ModuleCount, 2);
			TestTrue(TEXT("Empty point activation produces reaction text"), Coordinator->GetReactionText().Contains(TEXT("추진")));
			bool bCueAtInitialPoint = false;
			for (TActorIterator<ALSShellDetonationCue> It(World); It; ++It)
				bCueAtInitialPoint |= It->GetActorLocation().Equals(Hit.ImpactPoint, .1);
			TestTrue(TEXT("Empty activation creates actual cue actor at initial impact"), bCueAtInitialPoint);
			TestTrue(TEXT("Empty activation does not report missing target"), Coordinator->GetTargetFailureText().IsEmpty());
		}
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

#endif
