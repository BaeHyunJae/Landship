#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellEffectInstance.h"
#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellExperimentEnemy.h"
#include "ShellExperiment/LSShellExperimentGameMode.h"
#include "ShellExperiment/LSShellField.h"
#include "ShellExperiment/LSShellWeakPoint.h"
#include "ShellExperiment/LSShellSiegeController.h"
#include "ShellExperiment/LSShellMaterialStateComponent.h"
#include "ShellExperiment/LSShellMaterials.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "ShellExperiment/LSShellTransport.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "AssetCompilingManager.h"
#include "Engine/DataTable.h"
#include "EngineUtils.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSOutpost.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Maps/LSBattlefieldNavigationSystem.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Tests/AutomationCommon.h"

namespace
{
struct FBodyTransitionDamagePolicy
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	bool bFriendlyFire = Settings->bAllowFriendlyFire;
	bool bSelfDamage = Settings->bAllowSelfDamage;
	FBodyTransitionDamagePolicy()
	{
		Settings->bAllowFriendlyFire = true;
		Settings->bAllowSelfDamage = true;
	}
	~FBodyTransitionDamagePolicy()
	{
		Settings->bAllowFriendlyFire = bFriendlyFire;
		Settings->bAllowSelfDamage = bSelfDamage;
	}
};

template <typename T>
T* SpawnTransitionActor(UWorld* World, FVector Location)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Spawn);
}

void TickTransitionWorld(UWorld* World, float Delta = .01f)
{
	++GFrameCounter;
	World->Tick(LEVELTICK_All, Delta);
}

template <typename T>
int32 CountTransitionActors(UWorld* World)
{
	int32 Count = 0;
	for (TActorIterator<T> It(World); It; ++It) ++Count;
	return Count;
}

ALSShellExperimentCoordinator* SetUpBodyTransitionWorld(UWorld* World, FAutomationTestBase& Test)
{
	World->GetWorldSettings()->DefaultGameMode = ALSShellExperimentGameMode::StaticClass();
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(-5000, 0, 200));
	if (!Test.TestNotNull(TEXT("Native castle"), Castle)) return nullptr;
	Castle->GetMovementComponent()->Deactivate();
	UClass* CoreClass = LoadClass<ALSOutpost>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Blueprints/BP_ShellCore.BP_ShellCore_C"));
	if (!Test.TestNotNull(TEXT("Experiment core Blueprint"), CoreClass)) return nullptr;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALSOutpost* Core = World->SpawnActor<ALSOutpost>(CoreClass, FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
	if (!Test.TestNotNull(TEXT("Core"), Core)) return nullptr;
	auto* Setup = SpawnTransitionActor<ALSBattlefieldSetup>(World, FVector::ZeroVector);
	Setup->PlayerCastle = Castle;
	Setup->CoreOutpost = Core;
	auto* Coordinator = SpawnTransitionActor<ALSShellExperimentCoordinator>(World, FVector::ZeroVector);
	Coordinator->PlayerCastle = Castle;
	Coordinator->CoreOutpost = Core;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	Coordinator->SupplyStage = ELSShellSupplyStage::Complete;
	Coordinator->AvailableModules = {ELSShellModule::Blast, ELSShellModule::Thrust, ELSShellModule::Electric,
		ELSShellModule::Gravity, ELSShellModule::Iron, ELSShellModule::Split, ELSShellModule::Adhesion, ELSShellModule::Cooling};
	Coordinator->InitialFirst = ELSShellModule::Cooling;
	Coordinator->InitialSecond = ELSShellModule::Blast;
	Coordinator->Tuning.ModuleDelay = 100;
	return Coordinator;
}

ALSShellExperimentEnemy* SpawnTransitionAssault(UWorld* World, FVector Location)
{
	const FTransform Transform(Location);
	auto* Enemy = World->SpawnActorDeferred<ALSShellExperimentEnemy>(ALSShellExperimentEnemy::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Enemy) return nullptr;
	Enemy->EnemyKind = ELSShellEnemyKind::Assault;
	Enemy->StatsTable = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Data/DT_ShellEnemyStats.DT_ShellEnemyStats"));
	Enemy->FinishSpawning(Transform);
	return Enemy;
}

bool BeginBodyTransitionWorld(FTestWorldWrapper& TestWorld, FAutomationTestBase& Test,
	ALSShellExperimentCoordinator* Coordinator)
{
	if (!TestWorld.BeginPlayInTestWorld()) return false;
	const auto* Mode = TestWorld.GetTestWorld()->GetAuthGameMode<ALSShellExperimentGameMode>();
	return Test.TestNotNull(TEXT("Experiment GameMode"), Mode) &&
		Test.TestTrue(TEXT("Valid material-stage configuration"), Mode->IsConfigurationValid()) &&
		Test.TestTrue(TEXT("Coordinator active"), Coordinator->IsBattleActive());
}

UBoxComponent* AddTransitionFloor(UWorld* World, FVector Center, FVector Extent = FVector(1200, 1200, 25))
{
	AActor* Floor = SpawnTransitionActor<AActor>(World, Center);
	UBoxComponent* Body = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Body);
	Floor->AddInstanceComponent(Body);
	Body->SetWorldLocation(Center);
	Body->SetBoxExtent(Extent);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(LSCollisionChannels::Ground);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetMobility(EComponentMobility::Static);
	Body->SetCanEverAffectNavigation(true);
	Body->RegisterComponent();
	return Body;
}

void AddTransitionNavigation(UWorld* World, FVector Center)
{
	auto* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
	UBoxComponent* BoundsBox = NewObject<UBoxComponent>(Bounds);
	Bounds->AddInstanceComponent(BoundsBox);
	BoundsBox->SetupAttachment(Bounds->GetRootComponent());
	BoundsBox->SetBoxExtent(FVector(3600, 1600, 800));
	BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsBox->SetCanEverAffectNavigation(false);
	Bounds->SetActorLocation(Center);
	BoundsBox->RegisterComponent();
	auto* Navigation = NewObject<ULSBattlefieldNavigationSystem>(World);
	World->SetNavigationSystem(Navigation);
	Navigation->InitializeForWorld(*World, FNavigationSystemRunMode::GameMode);
	FAssetCompilingManager::Get().FinishAllCompilation();
	Navigation->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock,
		UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	Navigation->Build();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellArenaCoolingHitTest,
	"Landship.ShellExperiment.Arena.CoolingHitHasNoDirectDamageOrImpulseAndCoolsNearbyBodies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellArenaCoolingHitTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->SupplyTuning.Arena.bEnabled = true;
	auto* Target = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(4000, 0, 800));
	auto* Nearby = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(4000, 300, 800));
	auto* Far = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(4000, 800, 800));
	auto* BehindCastle = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(4400, 0, 800));
	for (auto* Ball : {Target, Nearby, Far, BehindCastle}) Ball->Body->SetEnableGravity(false);
	if (!BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	// 실제 Pawn 성의 Building 채널이 냉기파를 막는지 범위 안에서 확인한다.
	Coordinator->PlayerCastle->GetCapsuleComponent()->SetCapsuleSize(100, 100);
	Coordinator->PlayerCastle->SetActorLocation(FVector(4200, 0, 800));
	FLSHealthSnapshot Before, After;
	if (!TestTrue(TEXT("Target health is available"), Target->GetHealthComponent()->TryGetHealth(Before))) return false;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.First = ELSShellModule::Cooling;
	Shot.Second = ELSShellModule::Blast;
	Shot.Direction = FVector::ForwardVector;
	FHitResult Hit(Target, Target->Body, Target->GetActorLocation() - FVector(70, 0, 0), -FVector::ForwardVector);
	Hit.ImpactPoint = Hit.Location;
	Coordinator->QueueShellHit(Shot, Hit);
	TickTransitionWorld(World);
	Target->GetHealthComponent()->TryGetHealth(After);
	TestEqual(TEXT("Cooling first hit causes no direct health damage"), After.CurrentHealth, Before.CurrentHealth);
	TestTrue(TEXT("Cooling first hit gives no direct momentum"), Target->Reactive->GetMomentumAdjustedLinearVelocity().IsNearlyZero(1));
	const auto* TargetCold = Target->FindComponentByClass<ULSShellMaterialStateComponent>();
	const auto* NearbyCold = Nearby->FindComponentByClass<ULSShellMaterialStateComponent>();
	const auto* FarCold = Far->FindComponentByClass<ULSShellMaterialStateComponent>();
	const auto* ShieldedCold = BehindCastle->FindComponentByClass<ULSShellMaterialStateComponent>();
	TestTrue(TEXT("Direct and nearby bodies are cooled"), TargetCold && TargetCold->IsCooled() && NearbyCold && NearbyCold->IsCooled());
	TestTrue(TEXT("Body outside Cooling radius is not cooled"), !FarCold || !FarCold->IsCooled());
	TestTrue(TEXT("A real Pawn castle shields a body inside Cooling radius"), !ShieldedCold || !ShieldedCold->IsCooled());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellArenaCoolingRemovedAnchorTest,
	"Landship.ShellExperiment.Arena.DelayedCoolingSurvivesAnchorRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellArenaCoolingRemovedAnchorTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->SupplyTuning.Arena.bEnabled = true;
	auto* Anchor = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(4000, 0, 800));
	auto* Witness = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(4000, 1000, 800));
	Anchor->Body->SetEnableGravity(false);
	Witness->Body->SetEnableGravity(false);
	if (!BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.First = ELSShellModule::Electric;
	Shot.Second = ELSShellModule::Cooling;
	FHitResult Hit(Anchor, Anchor->Body, Anchor->GetActorLocation() - FVector(70, 0, 0), -FVector::ForwardVector);
	Hit.ImpactPoint = Hit.Location;
	auto* Pending = SpawnTransitionActor<ALSShellEffectInstance>(World, Hit.ImpactPoint);
	Pending->Initialize(Coordinator, Shot, Hit, true);
	if (!TestTrue(TEXT("Cooling follow-up is pending on the original body"), Pending->IsPending() && Pending->GetAnchorActor() == Anchor)) return false;
	Anchor->SetActorLocation(FVector(4000, 700, 800), false, nullptr, ETeleportType::TeleportPhysics);
	TickTransitionWorld(World);
	Anchor->Destroy();
	TickTransitionWorld(World);
	if (!TestTrue(TEXT("Removal does not cancel delayed Cooling"), IsValid(Pending) && Coordinator->CanManualTrigger())) return false;
	Coordinator->TriggerPending();
	TickTransitionWorld(World);
	const auto* Cold = Witness->FindComponentByClass<ULSShellMaterialStateComponent>();
	TestTrue(TEXT("Cooling reaches a nearby body at the last valid contact"), Cold && Cold->IsCooled());
	TestFalse(TEXT("Manual trigger consumes the pending effect"), Coordinator->CanManualTrigger());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellArenaGravityFixedHitFollowupTest,
	"Landship.ShellExperiment.Arena.GravityFixedHitSnapshotsDelayAndFiresOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellArenaGravityFixedHitFollowupTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->SupplyTuning.Arena.bEnabled = true;
	Coordinator->Tuning.ModuleDelay = .6f;
	auto* Floor = AddTransitionFloor(World, FVector(4000, 0, -25));
	if (!BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.First = ELSShellModule::Gravity;
	Shot.Second = ELSShellModule::Blast;
	FHitResult Hit(Floor->GetOwner(), Floor, FVector(4000, 0, 0), FVector::UpVector);
	Hit.ImpactPoint = Hit.Location;
	auto* Pending = SpawnTransitionActor<ALSShellEffectInstance>(World, Hit.ImpactPoint);
	Pending->Initialize(Coordinator, Shot, Hit, false);
	Coordinator->SupplyTuning.Arena.GravityFollowupDelay = .1f;
	for (int32 Step = 0; Step < 70; ++Step) TickTransitionWorld(World);
	if (!TestTrue(TEXT("Fixed-terrain Gravity remains pending beyond the ordinary delay"), IsValid(Pending) && Coordinator->CanManualTrigger())) return false;
	TestTrue(TEXT("Pending timer uses the original 1.2 second shot delay"), Pending->GetPendingSeconds() > .3f);
	Coordinator->TriggerPending();
	Coordinator->TriggerPending();
	for (int32 Step = 0; Step < 60; ++Step) TickTransitionWorld(World);
	TestEqual(TEXT("Manual Gravity follow-up executes exactly once"), Coordinator->ModuleCount, 2);
	TestFalse(TEXT("No pending effect remains"), Coordinator->CanManualTrigger());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellArenaGravityMassConvergenceTest,
	"Landship.ShellExperiment.Arena.GravityMassConvergence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellArenaGravityMassConvergenceTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->SupplyTuning.Arena.bEnabled = true;
	const FVector Center(4000, 0, 0);
	const FTransform LightPose(FVector(4000, -450, 800));
	const FTransform HeavyPose(FVector(4000, 450, 800));
	auto* Light = World->SpawnActorDeferred<ALSShellMetalBall>(ALSShellMetalBall::StaticClass(), LightPose,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	auto* Heavy = World->SpawnActorDeferred<ALSShellMetalBall>(ALSShellMetalBall::StaticClass(), HeavyPose,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Light metal ball"), Light) || !TestNotNull(TEXT("Heavy metal ball"), Heavy)) return false;
	Light->IntrinsicMassKg = 60;
	Heavy->IntrinsicMassKg = 750;
	Light->FinishSpawning(LightPose);
	Heavy->FinishSpawning(HeavyPose);
	if (!BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	if (!TestTrue(TEXT("Both balls are actual simulated rigid bodies with authored masses"),
		Light->Body->IsSimulatingPhysics() && Heavy->Body->IsSimulatingPhysics() &&
		FMath::IsNearlyEqual(Light->Body->GetMass(), 60.f, .1f) && FMath::IsNearlyEqual(Heavy->Body->GetMass(), 750.f, .1f))) return false;
	for (auto* Ball : {Light, Heavy})
	{
		Ball->Body->SetEnableGravity(false);
		Ball->Body->SetLinearDamping(0);
	}
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	auto* Field = SpawnTransitionActor<ALSShellGravityField>(World, Center);
	if (!TestNotNull(TEXT("Actual gravity field"), Field)) return false;
	Field->InitializeSupply(Coordinator, Shot);
	const FVector LightStart = Light->Body->GetComponentLocation();
	const FVector HeavyStart = Heavy->Body->GetComponentLocation();
	for (int32 Step = 0; Step < 20; ++Step) TickTransitionWorld(World);
	const double LightTravel = FVector::Dist2D(LightStart, Light->Body->GetComponentLocation());
	const double HeavyTravel = FVector::Dist2D(HeavyStart, Heavy->Body->GetComponentLocation());
	AddInfo(FString::Printf(TEXT("Arena gather at .2s: light=%.2fcm heavy=%.2fcm"), LightTravel, HeavyTravel));
	TestTrue(TEXT("The lighter body travels farther during initial gather"), LightTravel > HeavyTravel);
	for (int32 Step = 20; Step < 120; ++Step) TickTransitionWorld(World);
	const double LightSurface = FMath::Max(0.0, FVector::Dist2D(Center, Light->Body->GetComponentLocation()) - Light->Body->GetScaledSphereRadius());
	const double HeavySurface = FMath::Max(0.0, FVector::Dist2D(Center, Heavy->Body->GetComponentLocation()) - Heavy->Body->GetScaledSphereRadius());
	const bool bTouching = Light->Reactive->GetContactBodies().Contains(Heavy->Body.Get());
	AddInfo(FString::Printf(TEXT("Arena gather at 1.2s: light surface=%.2fcm heavy surface=%.2fcm touching=%d"),
		LightSurface, HeavySurface, bTouching));
	TestTrue(TEXT("Both bodies gather near the center or form a touching group"),
		(LightSurface <= 180 && HeavySurface <= 180) || bTouching);
	Field->StopGathering();
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellArenaGatherAssemblyThrustTest,
	"Landship.ShellExperiment.Arena.GatherAssemblyThrustSharesOneImpulse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellArenaGatherAssemblyThrustTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->SupplyTuning.Arena.bEnabled = true;
	AddTransitionFloor(World, FVector(4000, 0, 705));
	const FTransform Pose(FVector(4000, 0, 800));
	auto* Vehicle = World->SpawnActorDeferred<ALSShellTransport>(ALSShellTransport::StaticClass(), Pose,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Actual transport"), Vehicle)) return false;
	Vehicle->bAutoStartRoute = false;
	Vehicle->RouteStart = Pose.GetLocation();
	Vehicle->RouteEnd = Pose.GetLocation() + FVector(150, 0, 0);
	FLSShellCargoSlot PlateSlot;
	PlateSlot.CargoClass = ALSShellPlate::StaticClass();
	PlateSlot.LocalTransform.SetLocation(FVector(-100, 0, 190));
	PlateSlot.MassOverrideKg = 250;
	PlateSlot.bKeepRackedOnDelivery = true;
	Vehicle->CargoSlots = {PlateSlot};
	Vehicle->FinishSpawning(Pose);
	if (!BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	const auto Cargo = Vehicle->GetCargoActors();
	if (!TestEqual(TEXT("Actual rack creates one cargo"), Cargo.Num(), 1)) return false;
	auto* Plate = Cast<ALSShellPlate>(Cargo[0]);
	if (!TestTrue(TEXT("Actual 600kg chassis and 250kg plate have a live structural rack"),
		Plate && Vehicle->Body->IsSimulatingPhysics() && Plate->Body->IsSimulatingPhysics() && !Plate->IsSupported() &&
		Vehicle->Reactive->GetStructuralContactBodies().Contains(Plate->Body.Get()) &&
		FMath::IsNearlyEqual(Vehicle->Body->GetMass(), 600.f, .1f) && FMath::IsNearlyEqual(Plate->Body->GetMass(), 250.f, .1f))) return false;
	Vehicle->Body->SetEnableGravity(false);
	Plate->Body->SetEnableGravity(false);
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.Direction = FVector::ForwardVector;
	Shot.SupplyTuning.ThrustMomentum = 660000;
	auto* Field = SpawnTransitionActor<ALSShellGravityField>(World, FVector(4000, 0, 0));
	if (!TestNotNull(TEXT("Gather field"), Field)) return false;
	Field->InitializeSupply(Coordinator, Shot);
	const FLSShellGatherOutput Output = Field->CaptureGatherOutput();
	if (!TestEqual(TEXT("Snapshot includes the two actual assembly members once"), Output.Members.Num(), 2)) return false;
	const FVector BeforeVehicle = Vehicle->Body->GetPhysicsLinearVelocity();
	const FVector BeforePlate = Plate->Body->GetPhysicsLinearVelocity();
	Field->StopGathering();
	Coordinator->QueueGatherFollowup(ELSShellModule::Thrust, Shot, Output);
	for (int32 Step = 0; Step < 2; ++Step) TickTransitionWorld(World);
	const FVector VehicleDelta = Vehicle->Body->GetPhysicsLinearVelocity() - BeforeVehicle;
	const FVector PlateDelta = Plate->Body->GetPhysicsLinearVelocity() - BeforePlate;
	const FVector Momentum = VehicleDelta * 600 + PlateDelta * 250;
	AddInfo(FString::Printf(TEXT("Gather thrust chassis=%s plate=%s total momentum=%s"),
		*VehicleDelta.ToCompactString(), *PlateDelta.ToCompactString(), *Momentum.ToCompactString()));
	TestTrue(TEXT("One 660000 kg cm/s impulse reaches the complete rack"), Momentum.Equals(FVector(660000, 0, 0), 20000));
	TestTrue(TEXT("Both racked bodies receive the same velocity change"), VehicleDelta.Equals(PlateDelta, 25));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellArenaGravityCancelOneFieldTest,
	"Landship.ShellExperiment.Arena.GravityCancelsOnlyItsQueuedCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellArenaGravityCancelOneFieldTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->SupplyTuning.Arena.bEnabled = true;
	auto* Control = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(4000, -1000, 800));
	auto* Canceled = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(4000, 1000, 800));
	if (!TestNotNull(TEXT("Control rigid body"), Control) || !TestNotNull(TEXT("Cancellation rigid body"), Canceled) ||
		!BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	if (!TestTrue(TEXT("Both witnesses use actual simulated bodies"), Control->Body->IsSimulatingPhysics() && Canceled->Body->IsSimulatingPhysics())) return false;
	Control->Body->SetEnableGravity(false);
	Canceled->Body->SetEnableGravity(false);
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	ALSShellGravityField* Fields[4];
	const FVector Centers[] = {FVector(3650, -1000, 0), FVector(4450, -1000, 0),
		FVector(3650, 1000, 0), FVector(4450, 1000, 0)};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Fields[Index] = SpawnTransitionActor<ALSShellGravityField>(World, Centers[Index]);
		if (!TestNotNull(TEXT("Actual field"), Fields[Index])) return false;
		Fields[Index]->InitializeSupply(Coordinator, Shot);
		Fields[Index]->Tick(.01f);
		Fields[Index]->SetActorTickEnabled(false);
	}
	Fields[2]->StopGathering();
	TickTransitionWorld(World);
	const float ControlX = Control->Body->GetPhysicsLinearVelocity().X;
	const float CanceledX = Canceled->Body->GetPhysicsLinearVelocity().X;
	AddInfo(FString::Printf(TEXT("Overlapping gravity control X=%.3f canceled X=%.3f"), ControlX, CanceledX));
	TestTrue(TEXT("Both real commands pull the control toward the nearer left field"), ControlX < 0);
	TestTrue(TEXT("Canceling the queued left command leaves the right field pulling"), CanceledX > 0);
	for (int32 Index : {0, 1, 3}) Fields[Index]->StopGathering();
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellArenaBlastPerBodyTest,
	"Landship.ShellExperiment.Arena.BlastMomentumDoesNotDiluteWithMoreBodies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellArenaBlastPerBodyTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->SupplyTuning.Arena.bEnabled = true;
	auto* Alone = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(4200, -1000, 800));
	auto* PairA = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(4200, 1000, 800));
	auto* PairB = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(3800, 1000, 800));
	for (auto* Ball : {Alone, PairA, PairB}) Ball->Body->SetEnableGravity(false);
	if (!BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	FLSShellExplosion Blast;
	Blast.Shot = Shot;
	Blast.Cause = ELSShellDamageCause::Module;
	Blast.Damage = 0;
	Blast.Radius = 500;
	Blast.Location = FVector(4000, -1000, 800);
	Coordinator->QueueExplosion(Blast);
	Blast.Location = FVector(4000, 1000, 800);
	Coordinator->QueueExplosion(Blast);
	TickTransitionWorld(World);
	const FVector SingleVelocity = Alone->Reactive->GetMomentumAdjustedLinearVelocity();
	const FVector PairVelocityA = PairA->Reactive->GetMomentumAdjustedLinearVelocity();
	const FVector PairVelocityB = PairB->Reactive->GetMomentumAdjustedLinearVelocity();
	TestTrue(TEXT("The single-body blast actually imparts upward momentum"), SingleVelocity.Z > 1);
	TestTrue(TEXT("Both equally distant bodies receive the same impulse as the single body"),
		PairVelocityA.Equals(SingleVelocity, 1) && PairVelocityB.Equals(SingleVelocity, 1));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellArenaGravityElectricFollowupTest,
	"Landship.ShellExperiment.Arena.GravityElectricStopsFieldAndChargesOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellArenaGravityElectricFollowupTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->SupplyTuning.Arena.bEnabled = true;
	auto* Target = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(4000, 0, 800));
	Target->Body->SetEnableGravity(false);
	if (!BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.First = ELSShellModule::Gravity;
	Shot.Second = ELSShellModule::Electric;
	FHitResult Hit(Target, Target->Body, Target->GetActorLocation() - FVector(70, 0, 0), -FVector::ForwardVector);
	Hit.ImpactPoint = Hit.Location;
	auto* Pending = SpawnTransitionActor<ALSShellEffectInstance>(World, Hit.ImpactPoint);
	Pending->Initialize(Coordinator, Shot, Hit, true);
	TWeakObjectPtr<ALSShellGravityField> Field = Pending->GetSourceGravityField();
	if (!TestTrue(TEXT("Gravity creates a live field and pending Electric follow-up"), Field.IsValid() && Coordinator->CanManualTrigger())) return false;
	TestEqual(TEXT("The target starts uncharged"), Target->Reactive->GetStoredCharge(), 0.f);
	Coordinator->TriggerPending();
	TickTransitionWorld(World);
	TestFalse(TEXT("Gravity field is removed when Electric fires"), Field.IsValid());
	TestEqual(TEXT("Electric stores one charge on the original body"), Target->Reactive->GetStoredCharge(), 180.f);
	for (int32 Step = 0; Step < 130; ++Step) TickTransitionWorld(World);
	TestEqual(TEXT("The former follow-up timer does not apply Electric again"), Target->Reactive->GetStoredCharge(), 180.f);
	TestEqual(TEXT("Exactly Gravity and Electric executed"), Coordinator->ModuleCount, 2);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellArenaThrustLandingTest,
	"Landship.ShellExperiment.Arena.ThrustLandingDamagesOnceButOrdinaryFallDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellArenaThrustLandingTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->SupplyTuning.Arena.bEnabled = true;
	AddTransitionFloor(World, FVector(4000, 0, -25), FVector(1800, 1800, 25));
	auto* Propelled = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(3500, -600, 400));
	auto* Control = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(3500, 600, 400));
	if (!BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	FLSHealthSnapshot PropelledBefore, ControlBefore, PropelledLanded, ControlLanded, PropelledLater;
	if (!TestTrue(TEXT("Both balls have health before the fall"),
		Propelled->GetHealthComponent()->TryGetHealth(PropelledBefore) && Control->GetHealthComponent()->TryGetHealth(ControlBefore))) return false;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.First = ELSShellModule::Thrust;
	Shot.Direction = FVector::ForwardVector;
	FHitResult Hit(Propelled, Propelled->Body, Propelled->GetActorLocation() - FVector(70, 0, 0), -FVector::ForwardVector);
	Hit.ImpactPoint = Hit.Location;
	Coordinator->ExecuteModule(ELSShellModule::Thrust, Hit.ImpactPoint, Shot, Hit);
	if (!TestTrue(TEXT("Real Thrust marks the propelled body"), Propelled->Reactive->IsThrustPropelled())) return false;
	TestFalse(TEXT("The falling control has no Thrust mark"), Control->Reactive->IsThrustPropelled());
	for (int32 Step = 0; Step < 200; ++Step) TickTransitionWorld(World);
	if (!TestTrue(TEXT("Both actual bodies reached the floor"),
		Propelled->GetActorLocation().Z < 100 && Control->GetActorLocation().Z < 100)) return false;
	if (!TestTrue(TEXT("Both landed bodies retain health"),
		Propelled->GetHealthComponent()->TryGetHealth(PropelledLanded) && Control->GetHealthComponent()->TryGetHealth(ControlLanded))) return false;
	TestTrue(TEXT("Thrust-propelled landing causes damage"), PropelledLanded.CurrentHealth < PropelledBefore.CurrentHealth);
	TestEqual(TEXT("Ordinary unstamped fall causes no landing damage"), ControlLanded.CurrentHealth, ControlBefore.CurrentHealth);
	for (int32 Step = 0; Step < 100; ++Step) TickTransitionWorld(World);
	Propelled->GetHealthComponent()->TryGetHealth(PropelledLater);
	TestEqual(TEXT("Continuing floor contact causes no additional damage"), PropelledLater.CurrentHealth, PropelledLanded.CurrentHealth);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellCooledAssaultBodyTransitionTest,
	"Landship.ShellExperiment.Material.CooledLethalBlastTransfersLiveBodyAndPendingBlast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellCooledAssaultBodyTransitionTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	AddTransitionFloor(World, FVector(4000, 0, -25));
	auto* Enemy = SpawnTransitionAssault(World, FVector(4000, 0, 88));
	if (!TestNotNull(TEXT("Real production Assault"), Enemy) || !BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	TestEqual(TEXT("Assault uses its production health row"), Enemy->FindComponentByClass<ULSHealthComponent>() != nullptr, true);
	if (!TestNotNull(TEXT("Assault reactive body"), Enemy->Reactive.Get()) ||
		!TestTrue(TEXT("Assault physical capsule"), Enemy->Reactive->IsPhysicalBodyPresent())) return false;

	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.First = ELSShellModule::Cooling;
	Shot.Second = ELSShellModule::Blast;
	Shot.ColdSeconds = 100;
	Shot.Source = Coordinator->PlayerCastle;
	Shot.Origin = Enemy->GetActorLocation() - FVector(400, 0, 0);
	FHitResult Hit(Enemy, Enemy->GetCapsuleComponent(), Enemy->GetActorLocation(), FVector::ForwardVector);
	Hit.ImpactPoint = Enemy->GetActorLocation() - FVector(42, 0, 0);
	auto* Pending = SpawnTransitionActor<ALSShellEffectInstance>(World, Hit.ImpactPoint);
	Pending->Initialize(Coordinator, Shot, Hit, true);
	if (!TestTrue(TEXT("Real Cooling module makes the Assault capsule physical"),
		Enemy->MaterialState->IsCooled() && Enemy->GetCapsuleComponent()->IsSimulatingPhysics())) return false;
	if (!TestTrue(TEXT("Delayed Blast is pending on the Assault"),
		Pending->IsPending() && Coordinator->CanManualTrigger() && Pending->GetAnchorActor() == Enemy)) return false;

	const uint64 OriginalBodyId = Enemy->Reactive->BodyId;
	const uint32 OriginalGeneration = Enemy->Reactive->GetBodyGeneration();
	TestTrue(TEXT("Charge is stored on the live physical Assault"),
		FMath::IsNearlyEqual(Coordinator->ApplyCharge(Enemy, Shot, 180), 180.f));
	const auto PacketsBeforeDeath = Enemy->Reactive->GetChargeStore().GetPackets();
	if (!TestEqual(TEXT("One actual charge packet is stored"), PacketsBeforeDeath.Num(), 1)) return false;
	const uint64 PacketId = PacketsBeforeDeath[0].PacketId;
	const FVector PreDeathVelocity = Enemy->Reactive->GetMomentumAdjustedLinearVelocity();
	const float PreDeathMassKg = Enemy->Reactive->GetPhysicsMassKg();

	FLSShellExplosion LethalBlast;
	LethalBlast.Location = Enemy->GetActorLocation();
	LethalBlast.Damage = 200;
	LethalBlast.Radius = 500;
	LethalBlast.Shot = Shot;
	LethalBlast.Cause = ELSShellDamageCause::Module;
	Coordinator->QueueExplosion(LethalBlast);
	TickTransitionWorld(World);

	TArray<ALSShellInertIce*> Ices;
	for (TActorIterator<ALSShellInertIce> It(World); It; ++It) Ices.Add(*It);
	if (!TestEqual(TEXT("One inert ice body replaces the cooled Assault"), Ices.Num(), 1)) return false;
	ALSShellInertIce* Ice = Ices[0];
	if (!TestFalse(TEXT("Source Assault is removed after transfer"), IsValid(Enemy))) return false;
	TestEqual(TEXT("Transition does not create a siege carriage"), CountTransitionActors<ALSShellSiegeCarriage>(World), 0);
	TestTrue(TEXT("Inert ice has no health component"), Ice->FindComponentByClass<ULSHealthComponent>() == nullptr);
	TestEqual(TEXT("Transferred body keeps its identity"), Ice->Reactive->BodyId, OriginalBodyId);
	TestEqual(TEXT("Transferred body advances one generation"), Ice->Reactive->GetBodyGeneration(), OriginalGeneration + 1);
	TestEqual(TEXT("Stored charge remains on transferred body"), Ice->Reactive->GetChargeStore().GetPackets().Num(), 1);
	TestEqual(TEXT("Transferred packet keeps its identity"), Ice->Reactive->GetChargeStore().GetPackets()[0].PacketId, PacketId);
	TestTrue(TEXT("Pending effect anchor follows the replacement"), Pending->IsPending() && Pending->GetAnchorActor() == Ice);
	TestTrue(TEXT("First module damage is not applied as health to inert ice"), Ice->FindComponentByClass<ULSHealthComponent>() == nullptr);
	const float ExpectedDeltaSpeed = FMath::Min(Shot.SupplyTuning.BlastMomentum * Shot.EffectScale,
		Shot.SupplyTuning.ModuleMaximumDeltaSpeed * PreDeathMassKg) / PreDeathMassKg;
	const FVector PostDeathVelocity = Ice->Reactive->GetMomentumAdjustedLinearVelocity();
	TestTrue(TEXT("Lethal module Blast delivers its capped impulse once to inert ice"),
		(PostDeathVelocity - PreDeathVelocity - FVector::UpVector * ExpectedDeltaSpeed).Size() < 30);
	for (int32 Step = 0; Step < 3; ++Step) TickTransitionWorld(World);
	TestEqual(TEXT("Pending Blast has not fired before its timer"), Coordinator->ModuleCount, 1);
	TestTrue(TEXT("Pending Blast remains available on the transferred body"),
		Pending->IsPending() && Pending->GetAnchorActor() == Ice);
	TestTrue(TEXT("No duplicate transition impulse appears on later world steps"),
		Ice->Reactive->GetMomentumAdjustedLinearVelocity().Z < PostDeathVelocity.Z + 50);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellRecalledGeneratedPendingThrustTest,
	"Landship.ShellExperiment.Material.RecallGeneratedBodyCancelsPendingThrust",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellRecalledGeneratedPendingThrustTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator || !BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	Coordinator->Tuning.ModuleDelay = .5f;
	auto* Parent = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(-2000, 0, 500));
	Parent->Body->SetEnableGravity(false);
	auto* Witness = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(-1800, 0, 500));
	Witness->Body->SetEnableGravity(false);
	Witness->Body->SetLinearDamping(0);

	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.First = ELSShellModule::Iron;
	Shot.Second = ELSShellModule::Thrust;
	Shot.Source = Coordinator->PlayerCastle;
	Shot.Direction = FVector::ForwardVector;
	Shot.Origin = Parent->GetActorLocation() - FVector(500, 0, 0);
	FHitResult Hit(Parent, Parent->Body, Parent->GetActorLocation(), -FVector::ForwardVector);
	Hit.ImpactPoint = Parent->GetActorLocation() - FVector(Parent->Radius, 0, 0);
	auto* Pending = SpawnTransitionActor<ALSShellEffectInstance>(World, Hit.ImpactPoint);
	if (!TestNotNull(TEXT("Pending effect actor"), Pending)) return false;
	Pending->Initialize(Coordinator, Shot, Hit, false, Parent);
	AActor* Generated = Pending->GetAnchorActor();
	if (!TestTrue(TEXT("Iron creates and anchors a generated physical body"),
		IsValid(Generated) && Generated != Parent && Pending->IsPending())) return false;
	TestEqual(TEXT("Only the initial Iron module has executed"), Coordinator->ModuleCount, 1);
	TWeakObjectPtr<ALSShellEffectInstance> WeakPending = Pending;
	if (!TestTrue(TEXT("Coordinator recalls its generated body"), Coordinator->RecallGeneratedMaterial(Generated))) return false;
	TestFalse(TEXT("Recall destroys the pending effect anchored to that body"), WeakPending.IsValid());
	for (int32 Step = 0; Step < 80; ++Step) TickTransitionWorld(World);
	TestEqual(TEXT("Canceled pending Thrust never executes"), Coordinator->ModuleCount, 1);
	TestTrue(TEXT("No area impulse reaches a nearby physical witness"),
		Witness->Body->GetPhysicsLinearVelocity().IsNearlyZero(1));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellLiveAssaultCoolingRecoveryTest,
	"Landship.ShellExperiment.Material.LiveAssaultCoolingThawsIntoNormalAIWalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellLiveAssaultCoolingRecoveryTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->PlayerCastle->SetActorLocation(FVector(1500, 0, 200));
	auto* Floor = AddTransitionFloor(World, FVector(2000, 0, -25), FVector(2500, 1500, 25));
	if (!TestTrue(TEXT("Fixture floor root is at its requested world location"),
		Floor->GetComponentLocation().Equals(FVector(2000, 0, -25), .01f)) ||
		!TestTrue(TEXT("Actual floor bounds support the Assault and its strategic goal"),
			Floor->Bounds.GetBox().IsInside(FVector(4000, 0, -1)) &&
			Floor->Bounds.GetBox().IsInside(FVector(1500, 0, -1)))) return false;
	AddTransitionNavigation(World, FVector(2000, 0, 350));
	auto* Enemy = SpawnTransitionAssault(World, FVector(4000, 0, 88));
	if (!TestNotNull(TEXT("Real production Assault"), Enemy) || !BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	if (!TestTrue(TEXT("Assault has live behavior before Cooling"), Enemy->Behavior->IsComponentTickEnabled())) return false;
	if (!TestTrue(TEXT("Assault is possessed for the real AI path"), Enemy->GetController() != nullptr)) return false;
	AddInfo(FString::Printf(TEXT("SHELL_THAW_FIXTURE Floor=%s Bounds=%s FloorChannel=%d CapsuleResponse=%d Castle=%s"),
		*Floor->GetComponentLocation().ToCompactString(), *Floor->Bounds.GetBox().ToString(),
		int32(Floor->GetCollisionObjectType()), int32(Enemy->GetCapsuleComponent()->GetCollisionResponseToChannel(Floor->GetCollisionObjectType())),
		*Coordinator->PlayerCastle->GetActorLocation().ToCompactString()));
	if (!TestTrue(TEXT("Production capsule blocks the fixture's physical ground channel"),
		Enemy->GetCapsuleComponent()->GetCollisionResponseToChannel(Floor->GetCollisionObjectType()) == ECR_Block)) return false;
	auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!TestNotNull(TEXT("Actual fixture navigation system"), Navigation)) return false;
	const auto* NavData = Navigation->GetNavDataForProps(Enemy->GetNavAgentPropertiesRef(), Enemy->GetNavAgentLocation());
	if (!TestNotNull(TEXT("Actual Assault navigation agent data"), NavData)) return false;
	FNavLocation Start, Goal;
	if (!TestTrue(TEXT("Assault projects onto the actual fixture navmesh"),
		Navigation->ProjectPointToNavigation(Enemy->GetNavAgentLocation(), Start, FVector(150, 150, 300), NavData)) ||
		!TestTrue(TEXT("Strategic castle goal projects onto the same navmesh"),
			Navigation->ProjectPointToNavigation(Coordinator->PlayerCastle->GetActorLocation(), Goal, FVector(150, 150, 300), NavData))) return false;
	auto* Path = Navigation->FindPathToLocationSynchronously(World, Start.Location, Goal.Location, Enemy);
	if (!TestTrue(TEXT("Before Cooling the fixture provides a complete path to its strategic goal"),
		Path && Path->IsValid() && !Path->IsPartial())) return false;

	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.First = ELSShellModule::Cooling;
	Shot.Second = ELSShellModule::Blast;
	Shot.ColdSeconds = 4;
	Shot.Source = Coordinator->PlayerCastle;
	FHitResult Hit(Enemy, Enemy->GetCapsuleComponent(), Enemy->GetActorLocation(), FVector::ForwardVector);
	Coordinator->ExecuteModule(ELSShellModule::Cooling, Enemy->GetActorLocation(), Shot, Hit);
	if (!TestTrue(TEXT("Cooling transfers the live character into physical simulation"),
		Enemy->MaterialState->IsCooled() && Enemy->GetCapsuleComponent()->IsSimulatingPhysics())) return false;

	for (int32 Step = 0; Step < 1200 &&
		(Enemy->MaterialState->IsCooled() || Enemy->GetCapsuleComponent()->IsSimulatingPhysics()); ++Step)
	{
		TickTransitionWorld(World);
		if (Step == 399 || Step == 599 || Step == 799 || Step == 999 || Step == 1199)
			AddInfo(Enemy->MaterialState->GetRecoveryDiagnostics());
	}
	if (!TestFalse(TEXT("Cooling expires"), Enemy->MaterialState->IsCooled()) ||
		!TestFalse(TEXT("Capsule returns from rigid simulation"), Enemy->GetCapsuleComponent()->IsSimulatingPhysics())) return false;
	if (!TestTrue(TEXT("Material recovery restores ordinary walking mode"),
		Enemy->GetCharacterMovement()->MovementMode == MOVE_Walking)) return false;
	if (!TestTrue(TEXT("AI behavior resumes after the capsule settles"), Enemy->Behavior->IsComponentTickEnabled())) return false;

	const FVector BeforeAiMovement = Enemy->GetActorLocation();
	FVector Previous = BeforeAiMovement;
	double TotalMovement = 0;
	double MaximumStepMovement = 0;
	for (int32 Step = 0; Step < 500; ++Step)
	{
		TickTransitionWorld(World);
		const FVector Current = Enemy->GetActorLocation();
		const double StepMovement = FVector::Dist2D(Current, Previous);
		TotalMovement += StepMovement;
		MaximumStepMovement = FMath::Max(MaximumStepMovement, StepMovement);
		Previous = Current;
	}
	TestTrue(TEXT("Recovered Assault resumes actual navigation movement"), TotalMovement > 10);
	TestTrue(TEXT("Recovery and AI movement use small world steps rather than a teleport"), MaximumStepMovement < 50);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellColdRigidContactMomentumTest,
	"Landship.ShellExperiment.Material.NativeColdContactKeepsSolverMomentumAcrossLethalRemap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellColdRigidContactMomentumTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	for (const bool bLethal : {false, true})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
		if (!Coordinator) return false;
		auto* Enemy = SpawnTransitionAssault(World, FVector(4000, 0, 500));
		auto* Ball = SpawnTransitionActor<ALSShellMetalBall>(World, FVector(3868, 0, 500));
		if (!TestNotNull(TEXT("Real Assault contact receiver"), Enemy) || !TestNotNull(TEXT("Real sphere collider"), Ball) ||
			!BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
		ULSShellReactiveComponent* PreviousBody = Enemy->Reactive;
		const uint64 BodyId = PreviousBody->BodyId;
		if (bLethal)
		{
			auto* Health = Enemy->FindComponentByClass<ULSHealthComponent>();
			FLSHealthSnapshot Snapshot;
			if (!TestTrue(TEXT("Receiver common Health is ready"), Health && Health->TryGetHealth(Snapshot))) return false;
			Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(Snapshot.CurrentHealth - 1,
				Coordinator->PlayerCastle, Coordinator->PlayerCastle));
		}
		FLSShellShot Cold;
		Coordinator->CaptureShotContext(Cold);
		Cold.ColdSeconds = 4;
		FHitResult Hit(Enemy, Enemy->GetCapsuleComponent(), Enemy->GetActorLocation(), FVector::ForwardVector);
		Coordinator->ExecuteModule(ELSShellModule::Cooling, Enemy->GetActorLocation(), Cold, Hit);
		if (!TestTrue(TEXT("Contact receiver is an actual cold solver body"),
			Enemy->MaterialState->IsCooled() && Enemy->GetCapsuleComponent()->IsSimulatingPhysics() && Ball->Body->IsSimulatingPhysics())) return false;
		Enemy->GetCapsuleComponent()->SetEnableGravity(false);
		Ball->Body->SetEnableGravity(false);
		Enemy->GetCapsuleComponent()->SetLinearDamping(0);
		Ball->Body->SetLinearDamping(0);
		Enemy->GetCapsuleComponent()->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Ball->Body->SetPhysicsLinearVelocity(FVector(600, 0, 0));
		const float BallMass = Ball->Body->GetMass();
		const float EnemyMass = PreviousBody->GetPhysicsMassKg();
		const FVector InitialMomentum = FVector(600, 0, 0) * BallMass;
		FVector SolverBallVelocity = FVector::ZeroVector, SolverEnemyVelocity = FVector::ZeroVector;
		uint64 NativeEpoch = 0;
		int32 ContactCountAtSample = -1;
		// native delegate는 역순 호출한다. 기존 조정자 뒤에 등록해 솔버 결과를 gameplay drain 전에 관측한다.
		const FDelegateHandle SampleHandle = FWorldDelegates::OnWorldPostActorTick.AddLambda(
			[&](UWorld* TickedWorld, ELevelTick, float)
			{
				if (TickedWorld != World || !IsValid(Enemy) || !IsValid(Ball)) return;
				const uint64 Epoch = PreviousBody->GetContinuingContactEpoch(Ball->Body);
				if (!Epoch) return;
				NativeEpoch = Epoch;
				ContactCountAtSample = Coordinator->ContactCount;
				SolverBallVelocity = Ball->Body->GetPhysicsLinearVelocity();
				SolverEnemyVelocity = Enemy->GetCapsuleComponent()->GetPhysicsLinearVelocity();
			});
		ON_SCOPE_EXIT { FWorldDelegates::OnWorldPostActorTick.Remove(SampleHandle); };
		for (int32 Step = 0; Step < 30 && Coordinator->ContactCount == 0; ++Step) TickTransitionWorld(World);
		if (!TestEqual(TEXT("Actual native contact resolves once"), Coordinator->ContactCount, 1) ||
			!TestTrue(TEXT("Observer reaches the native pair before gameplay resolution"), NativeEpoch != 0 && ContactCountAtSample == 0)) return false;
		auto* Current = Coordinator->ResolveCurrentBody(PreviousBody);
		if (!TestTrue(TEXT("Current identity resolves to a physical solver body"), Current && Current->IsPhysicalBodyPresent() &&
			Current->GetPhysicalBody()->IsSimulatingPhysics() && Current->BodyId == BodyId)) return false;
		if (bLethal)
		{
			TestFalse(TEXT("Lethal cold contact removes the source Assault"), IsValid(Enemy));
			TestTrue(TEXT("Lethal contact remaps the same identity to inert ice"), Current->GetOwner()->IsA<ALSShellInertIce>());
		}
		else TestTrue(TEXT("Nonlethal cold contact keeps the living solver Character"), Current == PreviousBody && IsValid(Enemy));
		const FVector FinalEnemyVelocity = Current->GetMomentumAdjustedLinearVelocity();
		const FVector FinalBallVelocity = Ball->Reactive->GetMomentumAdjustedLinearVelocity();
		AddInfo(FString::Printf(TEXT("SHELL_COLD_NATIVE_MOMENTUM Lethal=%d Epoch=%llu ContactSpeed=%.3f SolverTarget=%s FinalTarget=%s SolverBall=%s FinalBall=%s"),
			bLethal, NativeEpoch, Coordinator->LastContactSpeed, *SolverEnemyVelocity.ToCompactString(),
			*FinalEnemyVelocity.ToCompactString(), *SolverBallVelocity.ToCompactString(), *FinalBallVelocity.ToCompactString()));
		TestTrue(TEXT("Gameplay adds no second impulse to the current cold body"), FinalEnemyVelocity.Equals(SolverEnemyVelocity, .1f));
		TestTrue(TEXT("Sphere keeps its native solved velocity"), FinalBallVelocity.Equals(SolverBallVelocity, .1f));
		TestTrue(TEXT("The two native solver bodies retain their total horizontal momentum"),
			FVector::Dist2D(FinalEnemyVelocity * EnemyMass + FinalBallVelocity * BallMass, InitialMomentum) < 50);
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellArenaHeavyThrustDeathTransferTest,
	"Landship.ShellExperiment.Arena.HeavyThrustStampSurvivesBlastDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellArenaHeavyThrustDeathTransferTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->SupplyTuning.Arena.bEnabled = true;
	const FTransform Transform(FVector(4000, 0, 800));
	auto* Heavy = World->SpawnActorDeferred<ALSShellExperimentEnemy>(ALSShellExperimentEnemy::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Heavy actor"), Heavy)) return false;
	Heavy->EnemyKind = ELSShellEnemyKind::Heavy;
	Heavy->StatsTable = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Data/DT_ShellEnemyStats.DT_ShellEnemyStats"));
	Heavy->FinishSpawning(Transform);
	if (!BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	if (!TestTrue(TEXT("Heavy begins as an uncooled physical body"),
		Heavy->Reactive->IsPhysicalBodyPresent() && !Heavy->MaterialState->IsCooled())) return false;
	const uint64 BodyId = Heavy->Reactive->BodyId;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.First = ELSShellModule::Thrust;
	Shot.Direction = FVector::ForwardVector;
	FHitResult Hit(Heavy, Heavy->GetCapsuleComponent(), Heavy->GetActorLocation() - FVector(140, 0, 0), -FVector::ForwardVector);
	Hit.ImpactPoint = Hit.Location;
	Coordinator->ExecuteModule(ELSShellModule::Thrust, Hit.ImpactPoint, Shot, Hit);
	if (!TestTrue(TEXT("Real Thrust marks the Heavy"), Heavy->Reactive->IsThrustPropelled())) return false;
	FLSShellExplosion Blast;
	Blast.Location = Heavy->GetActorLocation();
	Blast.Shot = Shot;
	Blast.Cause = ELSShellDamageCause::Module;
	Blast.Damage = 10000;
	Blast.Radius = 50;
	Coordinator->QueueExplosion(Blast);
	TickTransitionWorld(World);
	TArray<ALSShellSiegeCarriage*> Carriages;
	for (TActorIterator<ALSShellSiegeCarriage> It(World); It; ++It) Carriages.Add(*It);
	if (!TestEqual(TEXT("Lethal blast replaces the Heavy with one carriage"), Carriages.Num(), 1)) return false;
	TestFalse(TEXT("Original Heavy is removed"), IsValid(Heavy));
	TestEqual(TEXT("Carriage retains the physical body identity"), Carriages[0]->Reactive->BodyId, BodyId);
	TestTrue(TEXT("Carriage retains the Thrust-propelled mark"), Carriages[0]->Reactive->IsThrustPropelled());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellArenaWeakPointCoverTest,
	"Landship.ShellExperiment.Arena.WeakPointDoesNotBlockCoolingOrGather",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellArenaWeakPointCoverTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->SupplyTuning.Arena.bEnabled = true;
	const FTransform Transform(FVector(4000, 0, 800));
	auto* Heavy = World->SpawnActorDeferred<ALSShellExperimentEnemy>(ALSShellExperimentEnemy::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Heavy actor"), Heavy)) return false;
	Heavy->EnemyKind = ELSShellEnemyKind::Heavy;
	Heavy->StatsTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Landship/ShellExperiment/Data/DT_ShellEnemyStats.DT_ShellEnemyStats"));
	Heavy->FinishSpawning(Transform);
	if (!BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	Heavy->EndBattleActions();
	Heavy->Siege->EndSiege();
	Heavy->GetCharacterMovement()->DisableMovement();
	auto* WeakPoint = Heavy->GetWeakPoint();
	if (!TestNotNull(TEXT("Heavy has its real attached weak point"), WeakPoint)) return false;
	const FVector WeakLocation = WeakPoint->Body->GetComponentLocation();
	const FVector Source = WeakLocation - FVector(200, 0, 0);
	auto* Ball = SpawnTransitionActor<ALSShellMetalBall>(World, WeakLocation + FVector(200, 0, 0));
	Ball->Body->SetEnableGravity(false);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(ArenaWeakPointFixture), false);
	Query.AddIgnoredActor(Heavy);
	Query.AddIgnoredActor(Ball);
	FHitResult Cover;
	if (!TestTrue(TEXT("Real weak point intersects the fixture's cover ray"),
		World->LineTraceSingleByObjectType(Cover, Source, Ball->GetActorLocation(), Objects, Query) && Cover.GetActor() == WeakPoint)) return false;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	auto* Field = SpawnTransitionActor<ALSShellGravityField>(World, Source);
	Field->InitializeSupply(Coordinator, Shot);
	for (int32 Step = 0; Step < 5; ++Step) TickTransitionWorld(World);
	TestTrue(TEXT("Gather pulls the actual ball through weak-point line of sight"),
		Ball->Reactive->GetMomentumAdjustedLinearVelocity().X < -1);
	Field->StopGathering();
	FHitResult PointHit;
	PointHit.ImpactPoint = PointHit.Location = Source;
	Coordinator->ExecuteModule(ELSShellModule::Cooling, Source, Shot, PointHit);
	TickTransitionWorld(World);
	const auto* Cold = Ball->FindComponentByClass<ULSShellMaterialStateComponent>();
	TestTrue(TEXT("Cooling reaches the ball through the same weak point"), Cold && Cold->IsCooled());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellArenaHeavyDischargeDeathTransferTest,
	"Landship.ShellExperiment.Arena.HeavyThrustStampSurvivesDischargeDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellArenaHeavyDischargeDeathTransferTest::RunTest(const FString& Parameters)
{
	FBodyTransitionDamagePolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpBodyTransitionWorld(World, *this);
	if (!Coordinator) return false;
	Coordinator->SupplyTuning.Arena.bEnabled = true;
	const FVector Location(4000, 0, 800);
	const FTransform Transform(Location);
	auto* Heavy = World->SpawnActorDeferred<ALSShellExperimentEnemy>(ALSShellExperimentEnemy::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Heavy actor"), Heavy)) return false;
	Heavy->EnemyKind = ELSShellEnemyKind::Heavy;
	Heavy->StatsTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Landship/ShellExperiment/Data/DT_ShellEnemyStats.DT_ShellEnemyStats"));
	Heavy->FinishSpawning(Transform);
	auto* Ball = SpawnTransitionActor<ALSShellMetalBall>(World, Location - FVector(500, 0, 0));
	Ball->Body->SetEnableGravity(false);
	if (!BeginBodyTransitionWorld(TestWorld, *this, Coordinator)) return false;
	Heavy->EndBattleActions();
	Heavy->Siege->EndSiege();
	Heavy->GetCharacterMovement()->DisableMovement();
	auto* Health = Heavy->FindComponentByClass<ULSHealthComponent>();
	FLSHealthSnapshot Before;
	if (!TestTrue(TEXT("Heavy health is available"), Health && Health->TryGetHealth(Before))) return false;
	Health->ApplyDamage(ULSHealthComponent::MakeDamageRequest(Before.CurrentHealth - 80, Coordinator->PlayerCastle, Coordinator));
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	if (!TestEqual(TEXT("Ball stores 180 charge"), Coordinator->ApplyCharge(Ball, Shot, 180), 180.f)) return false;
	const float Distance = Heavy->GetCapsuleComponent()->GetScaledCapsuleRadius() + Ball->Body->GetScaledSphereRadius();
	Ball->SetActorLocation(Location - FVector(Distance + 1, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
	TickTransitionWorld(World);
	Ball->Reactive->SetStructuralContact(Heavy->Reactive, false);
	const uint64 Epoch = Ball->Reactive->GetContinuingContactEpoch(Heavy->GetCapsuleComponent());
	if (!TestTrue(TEXT("Declared structural contact has an epoch"), Epoch != 0)) return false;
	const uint64 BodyId = Heavy->Reactive->BodyId;
	Shot.First = ELSShellModule::Thrust;
	Shot.Direction = FVector::ForwardVector;
	FHitResult Hit(Heavy, Heavy->GetCapsuleComponent(), Location - FVector(140, 0, 0), -FVector::ForwardVector);
	Hit.ImpactPoint = Hit.Location;
	bool bMarkedBeforeDrain = false;
	int32 ContactCountBeforeDrain = -1;
	const FDelegateHandle Handle = FWorldDelegates::OnWorldPostActorTick.AddLambda(
		[&](UWorld* TickedWorld, ELevelTick, float)
		{
			if (TickedWorld != World || bMarkedBeforeDrain) return;
			Coordinator->ExecuteModule(ELSShellModule::Thrust, Hit.ImpactPoint, Shot, Hit);
			bMarkedBeforeDrain = Heavy->Reactive->IsThrustPropelled() && !Heavy->MaterialState->IsCooled();
			ContactCountBeforeDrain = Coordinator->ContactCount;
			Coordinator->QueueContact(Ball, Heavy, Location - FVector(140, 0, 0), -FVector::ForwardVector,
				FVector(100, 0, 0), FVector::ZeroVector, Epoch);
		});
	ON_SCOPE_EXIT { FWorldDelegates::OnWorldPostActorTick.Remove(Handle); };
	TickTransitionWorld(World);
	if (!TestTrue(TEXT("Thrust marks the uncooled Heavy after actor ticks and before contact drain"),
		bMarkedBeforeDrain && ContactCountBeforeDrain == 0)) return false;
	TArray<ALSShellSiegeCarriage*> Carriages;
	for (TActorIterator<ALSShellSiegeCarriage> It(World); It; ++It) Carriages.Add(*It);
	if (!TestEqual(TEXT("Lethal declared-contact discharge creates one carriage"), Carriages.Num(), 1)) return false;
	TestEqual(TEXT("One discharge resolved"), Coordinator->DischargeCount, 1);
	TestEqual(TEXT("Subthreshold declared contact causes no mechanical damage"), Coordinator->ContactCount, 0);
	TestEqual(TEXT("Carriage keeps the Heavy body identity"), Carriages[0]->Reactive->BodyId, BodyId);
	TestTrue(TEXT("Discharge death preserves the current Thrust mark"), Carriages[0]->Reactive->IsThrustPropelled());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
