#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellExperimentGameMode.h"
#include "ShellExperiment/LSShellExperimentEnemy.h"
#include "ShellExperiment/LSShellPhysicalProp.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "ShellExperiment/LSShellSiegeController.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSOutpost.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ShellExperiment/LSShellProjectile.h"
#include "Tests/AutomationCommon.h"

namespace
{
struct FLSShellChargeIntegrationPolicy
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	bool bFriendlyFire = Settings->bAllowFriendlyFire;
	bool bSelfDamage = Settings->bAllowSelfDamage;
	FLSShellChargeIntegrationPolicy() { Settings->bAllowFriendlyFire = Settings->bAllowSelfDamage = true; }
	~FLSShellChargeIntegrationPolicy()
	{
		Settings->bAllowFriendlyFire = bFriendlyFire;
		Settings->bAllowSelfDamage = bSelfDamage;
	}
};

template <typename T>
T* SpawnChargeIntegrationActor(UWorld* World, FVector Location)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Spawn);
}

void TickChargeIntegrationWorld(UWorld* World)
{
	++GFrameCounter;
	World->Tick(LEVELTICK_All, .01f);
}

float ChargeIntegrationHealth(AActor* Actor)
{
	FLSHealthSnapshot Snapshot;
	auto* Health = Actor ? Actor->FindComponentByClass<ULSHealthComponent>() : nullptr;
	return Health && Health->TryGetHealth(Snapshot) ? Snapshot.CurrentHealth : -1;
}

ALSShellExperimentCoordinator* SetUpChargeIntegrationWorld(UWorld* World, FAutomationTestBase& Test)
{
	World->GetWorldSettings()->DefaultGameMode = ALSShellExperimentGameMode::StaticClass();
	auto* Castle = LSCastleTestFixture::Spawn(World, FVector(-5000, 0, 200));
	if (!Test.TestNotNull(TEXT("Production castle"), Castle)) return nullptr;
	Castle->GetMovementComponent()->Deactivate();
	UClass* CoreClass = LoadClass<ALSOutpost>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Blueprints/BP_ShellCore.BP_ShellCore_C"));
	if (!Test.TestNotNull(TEXT("Production experiment core class"), CoreClass)) return nullptr;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Core = World->SpawnActor<ALSOutpost>(CoreClass, FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
	if (!Test.TestNotNull(TEXT("Production core"), Core)) return nullptr;
	auto* Setup = SpawnChargeIntegrationActor<ALSBattlefieldSetup>(World, FVector::ZeroVector);
	Setup->PlayerCastle = Castle; Setup->CoreOutpost = Core;
	auto* Coordinator = SpawnChargeIntegrationActor<ALSShellExperimentCoordinator>(World, FVector::ZeroVector);
	Coordinator->PlayerCastle = Castle; Coordinator->CoreOutpost = Core;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	Coordinator->SupplyStage = ELSShellSupplyStage::Charge;
	Coordinator->AvailableModules = {ELSShellModule::Blast, ELSShellModule::Thrust, ELSShellModule::Electric};
	Coordinator->InitialFirst = ELSShellModule::Electric;
	Coordinator->InitialSecond = ELSShellModule::Thrust;
	return Coordinator;
}

bool BeginChargeIntegrationWorld(FTestWorldWrapper& TestWorld, FAutomationTestBase& Test, ALSShellExperimentCoordinator* Coordinator)
{
	if (!Test.TestTrue(TEXT("BeginPlay through actual experiment GameMode"), TestWorld.BeginPlayInTestWorld())) return false;
	if (!Test.TestTrue(TEXT("Charge-stage coordinator active"), Coordinator->IsBattleActive())) return false;
	return Test.TestEqual(TEXT("Production core starts with1000HP"), ChargeIntegrationHealth(Coordinator->CoreOutpost), 1000.f);
}

void AddChargeIntegrationFloor(UWorld* World)
{
	auto* Floor = SpawnChargeIntegrationActor<AStaticMeshActor>(World, FVector(-1200, 0, -50));
	auto* Body = Floor->GetStaticMeshComponent();
	Body->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->SetActorScale3D(FVector(40, 40, 1));
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(LSCollisionChannels::Ground);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Ignore);
	auto* Material = NewObject<UPhysicalMaterial>(Floor);
	Material->Friction = Material->StaticFriction = .7f;
	Material->Restitution = .3f;
	Material->FrictionCombineMode = Material->RestitutionCombineMode = EFrictionCombineMode::Min;
	Material->bOverrideFrictionCombineMode = Material->bOverrideRestitutionCombineMode = true;
	Body->SetPhysMaterialOverride(Material);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargedContactTracerTest,
	"Landship.ShellExperiment.ChargeIntegration.ChargedThrustNativeContactDetonatesBarrel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargedContactTracerTest::RunTest(const FString& Parameters)
{
	FLSShellChargeIntegrationPolicy Policy;
	for (const bool bCharged : {false, true})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		auto* Coordinator = SetUpChargeIntegrationWorld(World, *this);
		if (!Coordinator) return false;
		// 후행 폭발과 직격 충격을 분리하고 실제 지면 위의 첫 접촉을 비교하는 기술용 배치다.
		Coordinator->Tuning.ModuleDelay = 100;
		Coordinator->SupplyTuning.DirectMomentum = 0;
		Coordinator->SupplyTuning.ThrustMomentum = 240000;
		AddChargeIntegrationFloor(World);
		auto* Ball = SpawnChargeIntegrationActor<ALSShellMetalBall>(World, FVector(-1200, 0, 70));
		auto* Barrel = SpawnChargeIntegrationActor<ALSShellBarrel>(World, FVector(-850, 0, 60));
		Ball->IntrinsicMassKg = 300;
		TWeakObjectPtr<ALSShellBarrel> BarrelLifetime = Barrel;
		if (!BeginChargeIntegrationWorld(TestWorld, *this, Coordinator)) return false;
		for (int32 Step = 0; Step < 30; ++Step) TickChargeIntegrationWorld(World);
		TestTrue(TEXT("Tracer starts with real300kg sphere and60kg barrel"),
			FMath::IsNearlyEqual(Ball->Body->GetMass(), 300.f, .01f) && FMath::IsNearlyEqual(Barrel->Body->GetMass(), 60.f, .01f));
		TestEqual(TEXT("Unstruck production barrel starts with100HP"), ChargeIntegrationHealth(Barrel), 100.f);
		FLSShellShot ChargeShot;
		Coordinator->CaptureShotContext(ChargeShot);
		FHitResult Hit;
		if (!TestTrue(TEXT("Production shell sweep reaches intended ball"), ALSShellProjectile::SweepFlight(World,
			Ball->GetActorLocation() - FVector(500, 0, 0), Ball->GetActorLocation(), Coordinator->PlayerCastle, Hit) && Hit.GetActor() == Ball)) return false;
		const FVector BeforeChargeVelocity = Ball->Body->GetPhysicsLinearVelocity();
		const uint64 BeforeChargeForce = Ball->Reactive->GetLastExternalForceFrame();
		if (bCharged)
		{
			Coordinator->ExecuteModule(ELSShellModule::Electric, Hit.ImpactPoint, ChargeShot, Hit, true);
			TestEqual(TEXT("Electric module stores actual180charge"), Ball->Reactive->GetStoredCharge(), 180.f);
			TestTrue(TEXT("Charging does not add a solver impulse"), Ball->Body->GetPhysicsLinearVelocity().Equals(BeforeChargeVelocity, .01));
			TestEqual(TEXT("Charging does not enter external-force state"), Ball->Reactive->GetLastExternalForceFrame(), BeforeChargeForce);
		}
		FLSShellShot ThrustShot;
		Coordinator->CaptureShotContext(ThrustShot);
		ThrustShot.First = ELSShellModule::Thrust; ThrustShot.Second = ELSShellModule::Blast;
		ThrustShot.Direction = FVector::ForwardVector;
		Coordinator->QueueShellHit(ThrustShot, Hit);
		for (int32 Step = 0; Step < 100; ++Step)
		{
			TickChargeIntegrationWorld(World);
			if (bCharged ? Coordinator->DetonationCount > 0 : Coordinator->ContactCount > 0) break;
		}
		AddInfo(FString::Printf(TEXT("Charge native tracer charged=%d stored=%.3f contacts=%d discharges=%d detonations=%d speed=%.3f barrelHP=%.3f coreHP=%.3f thrustTime=%.6f contactTime=%.6f"),
			bCharged, Ball->Reactive->GetStoredCharge(), Coordinator->ContactCount, Coordinator->DischargeCount,
			Coordinator->DetonationCount, Coordinator->LastContactSpeed, ChargeIntegrationHealth(BarrelLifetime.Get()),
			ChargeIntegrationHealth(Coordinator->CoreOutpost), Coordinator->LastThrustGameTime, Coordinator->LastContactGameTime));
		TestEqual(TEXT("Tracer executes exactly one actual shell impact"), Coordinator->ShellHitCount, 1);
		TestEqual(TEXT("Native first contact resolves once"), Coordinator->ContactCount, 1);
		TestTrue(TEXT("Actual contact follows Thrust before delayed Blast"),
			Coordinator->LastThrustGameTime >= 0 && Coordinator->LastContactGameTime >= Coordinator->LastThrustGameTime &&
			Coordinator->ModuleCount == (bCharged ? 2 : 1));
		if (bCharged)
		{
			TestEqual(TEXT("One actual contact consumes90rather than all180"), Ball->Reactive->GetStoredCharge(), 90.f);
			TestEqual(TEXT("Symmetric native Hit callbacks create one discharge"), Coordinator->DischargeCount, 1);
			TestEqual(TEXT("Electrical damage then mechanical damage cause one barrel detonation"), Coordinator->DetonationCount, 1);
			TestFalse(TEXT("Charged first contact removes production100HP barrel"), BarrelLifetime.IsValid());
			TestTrue(TEXT("Actual environmental detonation damages common core health"), ChargeIntegrationHealth(Coordinator->CoreOutpost) < 1000);
		}
		else
		{
			TestTrue(TEXT("Uncharged actual first contact damages barrel without killing it"),
				BarrelLifetime.IsValid() && ChargeIntegrationHealth(Barrel) > 0 && ChargeIntegrationHealth(Barrel) < 100);
			TestEqual(TEXT("Uncharged control has no discharge"), Coordinator->DischargeCount, 0);
			TestEqual(TEXT("Uncharged control cannot detonate barrel on first contact"), Coordinator->DetonationCount, 0);
			TestEqual(TEXT("Uncharged first-contact control leaves core unchanged"), ChargeIntegrationHealth(Coordinator->CoreOutpost), 1000.f);
		}
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeContactSequenceTest,
	"Landship.ShellExperiment.ChargeIntegration.LateChargeWaitsForActualSeparatedRecontact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeContactSequenceTest::RunTest(const FString& Parameters)
{
	FLSShellChargeIntegrationPolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpChargeIntegrationWorld(World, *this);
	if (!Coordinator) return false;
	const FVector Start(-2000, 0, 500);
	auto* A = SpawnChargeIntegrationActor<ALSShellMetalBall>(World, Start);
	auto* B = SpawnChargeIntegrationActor<ALSShellBarrel>(World, Start + FVector(300, 0, 0));
	// 금속구는 중계체이므로, 사망·기폭 없이 접촉별 피해를 비교할 말단으로 폭약통을 쓴다.
	B->SetGeneratedHealthBeforeBeginPlay(600);
	B->Reactive->DetonationImpactThreshold = 10000;
	const float ContactDistance = A->Body->GetScaledSphereRadius() + B->Body->GetScaledBoxExtent().X;
	B->SetActorLocation(Start + FVector(ContactDistance + 1, 0, 0));
	A->Body->SetEnableGravity(false); B->Body->SetEnableGravity(false);
	if (!BeginChargeIntegrationWorld(TestWorld, *this, Coordinator)) return false;
	if (!TestTrue(TEXT("Production barrel is a nonrelay nonstorage damage endpoint"),
		!B->Reactive->bConductiveRelay && !B->Reactive->bCanStoreCharge)) return false;
	// 구조 접촉과 공개 접촉 큐로 같은 프레임의 처리 순서만 고립시킨 기술용 배치다.
	A->Reactive->SetStructuralContact(B->Reactive, false);
	const uint64 OriginalEpoch = A->Reactive->GetContinuingContactEpoch(B->Body);
	if (!TestTrue(TEXT("Declared structural pair has symmetric continuing epoch"), OriginalEpoch != 0 &&
		B->Reactive->GetContinuingContactEpoch(A->Body) == OriginalEpoch)) return false;
	Coordinator->QueueContact(A, B, Start + FVector(70, 0, 0), -FVector::ForwardVector,
		FVector(800, 0, 0), FVector::ZeroVector, OriginalEpoch);
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	if (!TestEqual(TEXT("Charge added after contact queue is actually accepted"), Coordinator->ApplyCharge(A, Shot, 180), 180.f)) return false;
	TickChargeIntegrationWorld(World);
	TestEqual(TEXT("Earlier same-frame contact cannot retroactively spend later charge"), A->Reactive->GetStoredCharge(), 180.f);
	TestEqual(TEXT("Earlier contact has no discharge"), Coordinator->DischargeCount, 0);
	TestTrue(TEXT("Earlier queued contact still commits mechanical damage"), FMath::IsNearlyEqual(ChargeIntegrationHealth(B), 570.f, .2f));
	Coordinator->QueueContact(B, A, Start + FVector(70, 0, 0), FVector::ForwardVector,
		FVector::ZeroVector, FVector(800, 0, 0), OriginalEpoch);
	A->Reactive->ReleaseStructuralContact(B->Reactive);
	TickChargeIntegrationWorld(World);
	TestEqual(TEXT("Duplicate symmetric epoch and structural release cannot create discharge"), Coordinator->DischargeCount, 0);
	TestEqual(TEXT("Charge survives continuing structural-pair release"), A->Reactive->GetStoredCharge(), 180.f);
	TestEqual(TEXT("Same epoch cannot repeat mechanical damage"), Coordinator->ContactCount, 1);
	// 이후에는 실제 4cm 분리와 솔버 재접촉을 만든다. 일반 플레이 경로를 주장하는 자세 변경은 아니다.
	B->SetActorLocation(Start + FVector(ContactDistance + 4, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
	TickChargeIntegrationWorld(World);
	TestEqual(TEXT("Actual separation releases old pair epoch"), A->Reactive->GetContinuingContactEpoch(B->Body), uint64(0));
	TestEqual(TEXT("Separation itself spends no charge"), A->Reactive->GetStoredCharge(), 180.f);
	A->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
	B->SetActorLocation(Start + FVector(ContactDistance + 1, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
	A->Body->SetPhysicsLinearVelocity(FVector(800, 0, 0));
	B->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	A->Body->WakeAllRigidBodies(); B->Body->WakeAllRigidBodies();
	TickChargeIntegrationWorld(World);
	const uint64 NewEpoch = A->Reactive->GetContinuingContactEpoch(B->Body);
	AddInfo(FString::Printf(TEXT("Late charge recontact oldEpoch=%llu newEpoch=%llu stored=%.3f contacts=%d discharges=%d receiverHP=%.3f"),
		OriginalEpoch, NewEpoch, A->Reactive->GetStoredCharge(), Coordinator->ContactCount, Coordinator->DischargeCount, ChargeIntegrationHealth(B)));
	TestTrue(TEXT("Actual native recontact creates new epoch"), NewEpoch != 0 && NewEpoch != OriginalEpoch);
	TestTrue(TEXT("Actual solver recontact transfers momentum"), B->Body->GetPhysicsLinearVelocity().X > 100);
	TestEqual(TEXT("Only separated native recontact consumes90charge"), A->Reactive->GetStoredCharge(), 90.f);
	TestEqual(TEXT("New native contact produces exactly one discharge"), Coordinator->DischargeCount, 1);
	TestEqual(TEXT("Second real contact creates second mechanical event"), Coordinator->ContactCount, 2);
	TestTrue(TEXT("New contact applies90electrical and30mechanical common damage"), FMath::IsNearlyEqual(ChargeIntegrationHealth(B), 450.f, .3f));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeSourceArmorTest,
	"Landship.ShellExperiment.ChargeIntegration.RemovedSourceAndMixedPacketsKeepCoreArmor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeSourceArmorTest::RunTest(const FString& Parameters)
{
	FLSShellChargeIntegrationPolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpChargeIntegrationWorld(World, *this);
	if (!Coordinator) return false;
	const auto* CoreBody = CastChecked<UPrimitiveComponent>(Coordinator->CoreOutpost->GetRootComponent());
	const FBox CoreBox = CoreBody->Bounds.GetBox();
	auto* Ball = SpawnChargeIntegrationActor<ALSShellMetalBall>(World,
		FVector(CoreBox.Min.X - 70.5, CoreBody->Bounds.Origin.Y, CoreBody->Bounds.Origin.Z));
	Ball->Body->SetEnableGravity(false);
	auto* EnvironmentSource = SpawnChargeIntegrationActor<ALSBasicAttackTestActor>(World, FVector(-3000, 1000, 500));
	EnvironmentSource->SetInitialFaction(ELSFaction::Enemy);
	if (!BeginChargeIntegrationWorld(TestWorld, *this, Coordinator)) return false;
	FLSShellShot PlayerShot, EnvironmentShot;
	Coordinator->CaptureShotContext(PlayerShot);
	Coordinator->CaptureShotContext(EnvironmentShot, EnvironmentSource);
	if (!TestTrue(TEXT("Production snapshots distinguish player and environmental resources"),
		PlayerShot.bPlayerResource && !EnvironmentShot.bPlayerResource && EnvironmentShot.Faction == ELSFaction::Enemy)) return false;
	TestEqual(TEXT("Player resource packet accepted"), Coordinator->ApplyCharge(Ball, PlayerShot, 45), 45.f);
	TestEqual(TEXT("Environmental resource packet accepted separately"), Coordinator->ApplyCharge(Ball, EnvironmentShot, 45), 45.f);
	const TArray<FLSShellChargePacket>& Packets = Ball->Reactive->GetChargeStore().GetPackets();
	if (!TestEqual(TEXT("Two actual carrier packets exist before contact"), Packets.Num(), 2)) return false;
	TWeakObjectPtr<ALSBasicAttackTestActor> RemovedSource = EnvironmentSource;
	EnvironmentSource->Destroy();
	TestFalse(TEXT("Original environment source disappears before native contact"), RemovedSource.IsValid());
	const TArray<FLSShellChargePacket>& RetainedPackets = Ball->Reactive->GetChargeStore().GetPackets();
	if (!TestEqual(TEXT("Carrier retains both actual packets after source removal"), RetainedPackets.Num(), 2)) return false;
	const FLSShellChargePacket EnvironmentPacket = RetainedPackets[1];
	TestTrue(TEXT("Stored packet retains source identity, shot identity, faction and provenance after source removal"),
		EnvironmentPacket.SourceId == EnvironmentShot.SourceId && EnvironmentPacket.ShotId == EnvironmentShot.ShotId &&
		EnvironmentPacket.SourceId != 0 && EnvironmentPacket.ShotId != 0 && EnvironmentPacket.Faction == ELSFaction::Enemy &&
		EnvironmentPacket.Provenance == ELSShellChargeProvenance::Environment && !EnvironmentPacket.Source.IsValid());
	// 공통 피해의 방어·출처를 고립시킨 기술용 저속 접촉이다. 중력과 기계적 피해를 분리한다.
	Ball->Body->SetPhysicsLinearVelocity(FVector(100, 0, 0));
	for (int32 Step = 0; Step < 20 && Coordinator->DischargeCount == 0; ++Step) TickChargeIntegrationWorld(World);
	TestEqual(TEXT("Real ball-core contact releases both packets in one discharge event"), Coordinator->DischargeCount, 1);
	TestEqual(TEXT("Mixed release consumes exactly90charge"), Ball->Reactive->GetStoredCharge(), 0.f);
	TestEqual(TEXT("Sub300native contact does not add mechanical damage"), Coordinator->ContactCount, 0);
	TestTrue(TEXT("Core applies player45at50percent and environmental45atfull damage"),
		FMath::IsNearlyEqual(ChargeIntegrationHealth(Coordinator->CoreOutpost), 1000.f - 22.5f - 45.f, .1f));
	FLSDamageResult LastDamage;
	if (!TestTrue(TEXT("Common core health retains actual discharge record"),
		Coordinator->CoreOutpost->FindComponentByClass<ULSHealthComponent>()->TryGetLastDamage(LastDamage))) return false;
	AddInfo(FString::Printf(TEXT("Mixed discharge coreHP=%.3f lastApplied=%.3f sourceFaction=%d sourceAlive=%d attackOrigin=%s impact=%s"),
		ChargeIntegrationHealth(Coordinator->CoreOutpost), LastDamage.AppliedDamage, static_cast<int32>(LastDamage.SourceFaction),
		LastDamage.Source.IsValid(), *LastDamage.AttackOrigin.ToCompactString(), *LastDamage.ImpactLocation.ToCompactString()));
	TestTrue(TEXT("Removed-source environmental share preserves faction and full common damage"),
		LastDamage.bHasSourceFaction && LastDamage.SourceFaction == ELSFaction::Enemy &&
		!LastDamage.Source.IsValid() && FMath::IsNearlyEqual(LastDamage.AppliedDamage, 45.f, .1f));
	TestTrue(TEXT("Discharge origin remains actual native contact surface rather than removed source position"),
		LastDamage.bHasAttackOrigin && FMath::IsNearlyEqual(LastDamage.AttackOrigin.X, CoreBox.Min.X, 1.) &&
		FMath::Abs(LastDamage.AttackOrigin.Y - CoreBody->Bounds.Origin.Y) < 1 &&
		FMath::Abs(LastDamage.AttackOrigin.Z - CoreBody->Bounds.Origin.Z) < 2);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellChargeHeavyEndpointTest,
	"Landship.ShellExperiment.ChargeIntegration.ConductiveHeavyReceivesOneContactBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellChargeHeavyEndpointTest::RunTest(const FString& Parameters)
{
	FLSShellChargeIntegrationPolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpChargeIntegrationWorld(World, *this);
	if (!Coordinator) return false;
	const FVector HeavyLocation(-2000, 0, 500);
	const FTransform HeavyTransform(HeavyLocation);
	auto* Heavy = World->SpawnActorDeferred<ALSShellExperimentEnemy>(ALSShellExperimentEnemy::StaticClass(), HeavyTransform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Production Heavy frame can be spawned"), Heavy)) return false;
	Heavy->EnemyKind = ELSShellEnemyKind::Heavy;
	Heavy->StatsTable = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Data/DT_ShellEnemyStats.DT_ShellEnemyStats"));
	if (!TestNotNull(TEXT("Production Heavy stat table exists"), Heavy->StatsTable.Get())) return false;
	Heavy->FinishSpawning(HeavyTransform);
	auto* Ball = SpawnChargeIntegrationActor<ALSShellMetalBall>(World, HeavyLocation - FVector(500, 0, 0));
	Ball->Body->SetEnableGravity(false);
	if (!BeginChargeIntegrationWorld(TestWorld, *this, Coordinator)) return false;
	Heavy->EndBattleActions();
	Heavy->Siege->EndSiege();
	Heavy->GetCharacterMovement()->DisableMovement();
	if (!TestEqual(TEXT("Heavy uses its production common600HP frame"), ChargeIntegrationHealth(Heavy), 600.f)) return false;
	if (!TestTrue(TEXT("Heavy retains charge storage and conductive relay roles"),
		Heavy->Reactive->bCanStoreCharge && Heavy->Reactive->bConductiveRelay)) return false;
	const float ContactDistance = Heavy->GetCapsuleComponent()->GetScaledCapsuleRadius() + Ball->Body->GetScaledSphereRadius();
	Ball->SetActorLocation(HeavyLocation - FVector(ContactDistance + 1, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	if (!TestEqual(TEXT("Map ball accepts180before the new contact"), Coordinator->ApplyCharge(Ball, Shot, 180), 180.f)) return false;
	// 실제 몸체의 선언 접촉과 공개 큐를 쓰는 기술용 배치다. 충전 뒤 접촉을 예약하고 기계적 피해는 배제한다.
	Ball->Reactive->SetStructuralContact(Heavy->Reactive, false);
	const uint64 Epoch = Ball->Reactive->GetContinuingContactEpoch(Heavy->GetCapsuleComponent());
	if (!TestTrue(TEXT("New declared contact has one symmetric epoch"), Epoch != 0 &&
		Heavy->Reactive->GetContinuingContactEpoch(Ball->Body) == Epoch)) return false;
	const FVector Point = HeavyLocation - FVector(Heavy->GetCapsuleComponent()->GetScaledCapsuleRadius(), 0, 0);
	Coordinator->QueueContact(Ball, Heavy, Point, -FVector::ForwardVector, FVector(100, 0, 0), FVector::ZeroVector, Epoch);
	Coordinator->QueueContact(Heavy, Ball, Point, FVector::ForwardVector, FVector::ZeroVector, FVector(100, 0, 0), Epoch);
	TestEqual(TEXT("Queueing contact does not apply damage before the shared drain"), ChargeIntegrationHealth(Heavy), 600.f);
	TestEqual(TEXT("Queueing contact does not consume charge before the shared drain"), Ball->Reactive->GetStoredCharge(), 180.f);
	TickChargeIntegrationWorld(World);
	TestEqual(TEXT("Conductive Heavy receives exactly90common electrical damage"), ChargeIntegrationHealth(Heavy), 510.f);
	TestEqual(TEXT("One contact leaves exactly90charge on the map ball"), Ball->Reactive->GetStoredCharge(), 90.f);
	TestEqual(TEXT("Relay endpoint does not copy spent charge into its own store"), Heavy->Reactive->GetStoredCharge(), 0.f);
	TestEqual(TEXT("Symmetric contact and relay traversal produce one discharge"), Coordinator->DischargeCount, 1);
	TestEqual(TEXT("Sub300contact adds no mechanical damage event"), Coordinator->ContactCount, 0);
	FLSDamageResult Damage;
	if (!TestTrue(TEXT("Heavy damage is committed through common health"),
		Heavy->FindComponentByClass<ULSHealthComponent>()->TryGetLastDamage(Damage))) return false;
	TestEqual(TEXT("Common damage record carries one90point share"), Damage.AppliedDamage, 90.f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
