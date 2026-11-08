#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellTransport.h"
#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellExperimentGameMode.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "ShellExperiment/LSShellMaterials.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSOutpost.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellTransportChargedCargoRetentionTest,
	"Landship.ShellExperiment.Transport.ChargedCargoRetainsIdentityOnCommonDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellTransportChargedCargoRetentionTest::RunTest(const FString& Parameters)
{
	auto* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bFriendlyFire = Settings->bAllowFriendlyFire, bSelfDamage = Settings->bAllowSelfDamage;
	Settings->bAllowFriendlyFire = Settings->bAllowSelfDamage = true;
	ON_SCOPE_EXIT { Settings->bAllowFriendlyFire = bFriendlyFire; Settings->bAllowSelfDamage = bSelfDamage; };
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = ALSShellExperimentGameMode::StaticClass();
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(-5000, 0, 200));
	if (!TestNotNull(TEXT("Production castle fixture"), Castle)) return false;
	Castle->GetMovementComponent()->Deactivate();
	UClass* CoreClass = LoadClass<ALSOutpost>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Blueprints/BP_ShellCore.BP_ShellCore_C"));
	if (!TestNotNull(TEXT("Production core class"), CoreClass)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALSOutpost* Core = World->SpawnActor<ALSOutpost>(CoreClass, FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Core"), Core)) return false;
	auto* Setup = World->SpawnActor<ALSBattlefieldSetup>();
	Setup->PlayerCastle = Castle; Setup->CoreOutpost = Core;
	auto* Coordinator = World->SpawnActor<ALSShellExperimentCoordinator>();
	Coordinator->PlayerCastle = Castle; Coordinator->CoreOutpost = Core;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	auto* Floor = World->SpawnActor<AStaticMeshActor>(FVector(-1200, 0, -50), FRotator::ZeroRotator, Spawn);
	auto* FloorBody = Floor->GetStaticMeshComponent();
	FloorBody->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->SetActorScale3D(FVector(40, 40, 1));
	FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FloorBody->SetCollisionObjectType(LSCollisionChannels::Ground);
	FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
	auto* FloorMaterial = NewObject<UPhysicalMaterial>(Floor);
	FloorMaterial->Friction = FloorMaterial->StaticFriction = .7f;
	FloorMaterial->Restitution = .3f;
	FloorMaterial->FrictionCombineMode = FloorMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
	FloorMaterial->bOverrideFrictionCombineMode = FloorMaterial->bOverrideRestitutionCombineMode = true;
	FloorBody->SetPhysMaterialOverride(FloorMaterial);
	auto* Transport = World->SpawnActor<ALSShellTransport>(FVector(-1200, 0, 70), FRotator::ZeroRotator, Spawn);
	Transport->RouteStart = FVector(-1200, 0, 70); Transport->RouteEnd = FVector(-600, 0, 70);
	if (!TestTrue(TEXT("Actual GameMode BeginPlay"), TestWorld.BeginPlayInTestWorld())) return false;
	if (!TestTrue(TEXT("Coordinator active"), Coordinator->IsBattleActive())) return false;
	if (!TestTrue(TEXT("Support and cargo activation succeeded"), Transport->GetConfigurationError().IsEmpty() && Transport->Body->IsSimulatingPhysics())) return false;
	const TArray<ALSShellPhysicalProp*> Cargo = Transport->GetCargoActors();
	if (!TestEqual(TEXT("Two actual cargo actors"), Cargo.Num(), 2)) return false;
	auto* Ball = Cast<ALSShellMetalBall>(Cargo[0]);
	if (!TestNotNull(TEXT("First cargo is actual metal ball"), Ball)) return false;
	for (int32 Step = 0; Step < 100; ++Step) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
	if (!TestTrue(TEXT("Loaded transport actually moves under its driver"), Transport->IsDriving() && Transport->Body->GetPhysicsLinearVelocity().Size() > 10)) return false;
	TestTrue(TEXT("Assembly counts chassis and each physical cargo mass once"), FMath::IsNearlyEqual(Transport->AssemblyMassKg, 1410.f, .1f));
	FLSShellChargePacket Packet;
	Packet.PacketId = 9001; Packet.Sequence = Packet.PacketId; Packet.SourceId = 9002; Packet.ShotId = 9003; Packet.Amount = 180;
	Packet.ActivationSequence = Packet.Sequence;
	if (!TestEqual(TEXT("Charge stored on cargo rather than chassis"), Ball->Reactive->AddCharge(Packet, 360), 180.f)) return false;
	TestEqual(TEXT("Chassis charge remains empty"), Transport->Reactive->GetStoredCharge(), 0.f);
	const uint64 BallBodyId = Ball->Reactive->BodyId;
	const uint64 Epoch = Ball->Reactive->GetContinuingContactEpoch(Transport->Body);
	if (!TestTrue(TEXT("Actual structural pair has a continuing epoch"), Epoch != 0)) return false;
	UPrimitiveComponent* OriginalRoot = Transport->GetRootComponent() == Transport->Body ? Transport->Body.Get() : nullptr;
	UPrimitiveComponent* OriginalCargoBody = Ball->GetPhysicalBody();
	const FVector ChassisVelocity = Transport->Body->GetPhysicsLinearVelocity();
	const FVector CargoVelocity = OriginalCargoBody->GetPhysicsLinearVelocity();
	const FVector CargoAngularVelocity = OriginalCargoBody->GetPhysicsAngularVelocityInRadians();
	const FVector CargoLocation = Ball->GetActorLocation();
	FLSHealthSnapshot CargoHealthBefore;
	Ball->GetHealthComponent()->TryGetHealth(CargoHealthBefore);
	const FLSShellChargePacket Before = Ball->Reactive->GetChargeStore().GetPackets()[0];
	Transport->GetHealthComponent()->ApplyDamage(ULSHealthComponent::MakeDamageRequest(1000, Castle, Castle));
	FLSHealthSnapshot ChassisHealth;
	Transport->GetHealthComponent()->TryGetHealth(ChassisHealth);
	TestEqual(TEXT("Common Health reaches zero"), ChassisHealth.CurrentHealth, 0.f);
	if (!TestTrue(TEXT("Exact actor and root remain physical"), IsValid(Transport) && Transport->GetRootComponent() == OriginalRoot && OriginalRoot && OriginalRoot->IsSimulatingPhysics())) return false;
	TestTrue(TEXT("Dead function never drives"), Transport->DriveState == ELSShellTransportDriveState::Wreck && !Transport->IsDriving());
	const TArray<ALSShellPhysicalProp*> ReleasedCargo = Transport->GetCargoActors();
	if (!TestTrue(TEXT("Original cargo actor and body remain"), ReleasedCargo.Num() == 2 && ReleasedCargo[0] == Ball && Ball->GetPhysicalBody() == OriginalCargoBody)) return false;
	TestTrue(TEXT("Release preserves chassis velocity immediately"), Transport->Body->GetPhysicsLinearVelocity().Equals(ChassisVelocity, UE_KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Release preserves cargo linear and angular velocity immediately"), OriginalCargoBody->GetPhysicsLinearVelocity().Equals(CargoVelocity, UE_KINDA_SMALL_NUMBER) &&
		OriginalCargoBody->GetPhysicsAngularVelocityInRadians().Equals(CargoAngularVelocity, UE_KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Release preserves cargo pose immediately"), Ball->GetActorLocation().Equals(CargoLocation, UE_KINDA_SMALL_NUMBER));
	TestEqual(TEXT("Cargo BodyId survives release"), Ball->Reactive->BodyId, BallBodyId);
	TestEqual(TEXT("Zero-gap release preserves continuing epoch"), Ball->Reactive->GetContinuingContactEpoch(Transport->Body), Epoch);
	TestTrue(TEXT("Structural edge is removed"), !Ball->Reactive->GetStructuralContactBodies().Contains(Transport->Body));
	TestFalse(TEXT("Cargo release cannot duplicate resources"), Transport->ReleaseCargo());
	FLSHealthSnapshot CargoHealthAfter;
	Ball->GetHealthComponent()->TryGetHealth(CargoHealthAfter);
	TestEqual(TEXT("Cargo Health survives chassis death"), CargoHealthAfter.CurrentHealth, CargoHealthBefore.CurrentHealth);
	const TArray<FLSShellChargePacket>& AfterPackets = Ball->Reactive->GetChargeStore().GetPackets();
	if (!TestEqual(TEXT("One original cargo packet remains"), AfterPackets.Num(), 1)) return false;
	const FLSShellChargePacket After = AfterPackets[0];
	TestTrue(TEXT("Packet identity, lineage, carrier, activation and amount survive release"), After.PacketId == Before.PacketId &&
		After.SourceId == Before.SourceId && After.ShotId == Before.ShotId && After.CarrierBodyId == Before.CarrierBodyId &&
		After.ActivationSequence == Before.ActivationSequence && After.Amount == Before.Amount);
	TestEqual(TEXT("No charge copied into wreck"), Transport->Reactive->GetStoredCharge(), 0.f);
	++GFrameCounter;
	World->Tick(LEVELTICK_All, .01f);
	if (!TestTrue(TEXT("Original cargo survives first released step"), IsValid(Ball) && !Ball->IsActorBeingDestroyed())) return false;
	FVector DeckPoint, CargoPoint;
	Transport->Body->GetClosestPointOnCollision(Ball->GetPhysicalBody()->GetComponentLocation(), DeckPoint);
	const float ReleaseGap = Ball->GetPhysicalBody()->GetClosestPointOnCollision(DeckPoint, CargoPoint);
	if (!TestTrue(TEXT("First released solver step actually exercises the continuing 3cm skin"), ReleaseGap >= 0 && ReleaseGap <= 3)) return false;
	TestEqual(TEXT("Solver hit after zero-gap release keeps original epoch"), Ball->Reactive->GetContinuingContactEpoch(Transport->Body), Epoch);
	TestEqual(TEXT("Zero-gap release does not spend cargo charge"), Ball->Reactive->GetStoredCharge(), Before.Amount);
	AddInfo(FString::Printf(TEXT("Charged cargo tracer actor=%s body=%llu packet=%llu carrier=%llu amount=%.3f epoch=%llu chassisVelocity=%s cargoVelocity=%s"),
		*Ball->GetName(), BallBodyId, After.PacketId, After.CarrierBodyId, After.Amount, Epoch,
		*ChassisVelocity.ToCompactString(), *CargoVelocity.ToCompactString()));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellBarrelNativeGroundImpactTest,
	"Landship.ShellExperiment.Transport.BarrelNativeGroundImpactDetonatesOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellBarrelNativeGroundImpactTest::RunTest(const FString& Parameters)
{
	auto* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bFriendlyFire = Settings->bAllowFriendlyFire, bSelfDamage = Settings->bAllowSelfDamage;
	Settings->bAllowFriendlyFire = Settings->bAllowSelfDamage = true;
	ON_SCOPE_EXIT { Settings->bAllowFriendlyFire = bFriendlyFire; Settings->bAllowSelfDamage = bSelfDamage; };
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = ALSShellExperimentGameMode::StaticClass();
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(-5000, 0, 200));
	if (!TestNotNull(TEXT("Production castle fixture"), Castle)) return false;
	Castle->GetMovementComponent()->Deactivate();
	UClass* CoreClass = LoadClass<ALSOutpost>(nullptr,
		TEXT("/Game/Landship/ShellExperiment/Blueprints/BP_ShellCore.BP_ShellCore_C"));
	if (!TestNotNull(TEXT("Production core class"), CoreClass)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALSOutpost* Core = World->SpawnActor<ALSOutpost>(CoreClass, FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Core outside barrel explosion radius"), Core)) return false;
	auto* Setup = World->SpawnActor<ALSBattlefieldSetup>();
	Setup->PlayerCastle = Castle; Setup->CoreOutpost = Core;
	auto* Coordinator = World->SpawnActor<ALSShellExperimentCoordinator>();
	Coordinator->PlayerCastle = Castle; Coordinator->CoreOutpost = Core;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	auto* Floor = World->SpawnActor<AStaticMeshActor>(FVector(-2200, 0, -50), FRotator::ZeroRotator, Spawn);
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
	FloorMaterial->FrictionCombineMode = FloorMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
	FloorMaterial->bOverrideFrictionCombineMode = FloorMaterial->bOverrideRestitutionCombineMode = true;
	FloorBody->SetPhysMaterialOverride(FloorMaterial);
	auto* Barrel = World->SpawnActor<ALSShellBarrel>(FVector(-2200, 0, 1000), FRotator::ZeroRotator, Spawn);
	TWeakObjectPtr<ALSShellBarrel> BarrelLifetime = Barrel;
	if (!TestTrue(TEXT("Actual experiment GameMode BeginPlay"), TestWorld.BeginPlayInTestWorld())) return false;
	if (!TestTrue(TEXT("Common coordinator active"), Coordinator->IsBattleActive())) return false;
	if (!TestTrue(TEXT("Native barrel starts from rest in real gravity"), Barrel->Body->IsSimulatingPhysics() &&
		Barrel->Body->IsGravityEnabled() && Barrel->Body->GetPhysicsLinearVelocity().IsNearlyZero())) return false;
	TestNull(TEXT("Ordinary actual floor has no Health pool"), Floor->FindComponentByClass<ULSHealthComponent>());
	bool bSawActualFloorContact = false;
	float GroundNormalSpeed = 0;
	float HealthBeforeQueuedExplosion = -1;
	double GroundContactTime = -1;
	// native multicast는 역순 호출하므로 BeginPlay 뒤 등록해 공통 FIFO 처리 전의 실제 Hit 접촉을 읽는다.
	const FDelegateHandle GroundObserver = FWorldDelegates::OnWorldPostActorTick.AddLambda(
		[&](UWorld* TickWorld, ELevelTick, float)
		{
			if (TickWorld != World || bSawActualFloorContact || !BarrelLifetime.IsValid() ||
				BarrelLifetime->Reactive->GetContinuingContactEpoch(FloorBody) == 0) return;
			bSawActualFloorContact = true;
			GroundContactTime = World->GetTimeSeconds();
			const FVector Center = BarrelLifetime->Body->GetComponentLocation();
			GroundNormalSpeed = FMath::Abs(BarrelLifetime->Reactive->GetSampledSurfaceVelocity(FVector(Center.X, Center.Y, 0)).Z);
			FLSHealthSnapshot Health;
			if (BarrelLifetime->GetHealthComponent()->TryGetHealth(Health)) HealthBeforeQueuedExplosion = Health.CurrentHealth;
		});
	ON_SCOPE_EXIT { FWorldDelegates::OnWorldPostActorTick.Remove(GroundObserver); };
	for (int32 Step = 0; Step < 500; ++Step)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, .01f);
	}
	TestTrue(TEXT("Observer reached native barrel contact with the actual floor primitive"), bSawActualFloorContact);
	TestTrue(TEXT("Actual sampled ground normal speed reaches unchanged1100 threshold"), GroundNormalSpeed >= 1100);
	TestEqual(TEXT("Floor landing adds no separate Health damage before queued explosion"), HealthBeforeQueuedExplosion, 100.f);
	TestEqual(TEXT("Native ground impact commits exactly one FIFO detonation including death notification"), Coordinator->DetonationCount, 1);
	TestFalse(TEXT("Common lethal detonation removes the original barrel"), BarrelLifetime.IsValid());
	TestEqual(TEXT("Floor contact is not promoted to combat body damage"), Coordinator->ContactCount, 0);
	TestEqual(TEXT("No shell or module was injected"), Coordinator->ShellHitCount + Coordinator->ModuleCount, 0);
	TestTrue(TEXT("Ordinary floor remains query-enabled"), FloorBody->IsQueryCollisionEnabled());
	AddInfo(FString::Printf(TEXT("Barrel native ground tracer contact=%d time=%.6f sampledNormalSpeed=%.3f preExplosionHP=%.1f detonations=%d removed=%d"),
		bSawActualFloorContact, GroundContactTime, GroundNormalSpeed, HealthBeforeQueuedExplosion,
		Coordinator->DetonationCount, !BarrelLifetime.IsValid()));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellTransportRetainedRackTest,
	"Landship.ShellExperiment.Transport.DeliveredPlateRetainsRackUntilPlateOrChassisDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellTransportRetainedRackTest::RunTest(const FString& Parameters)
{
	auto* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bFriendlyFire = Settings->bAllowFriendlyFire, bSelfDamage = Settings->bAllowSelfDamage;
	Settings->bAllowFriendlyFire = Settings->bAllowSelfDamage = true;
	ON_SCOPE_EXIT { Settings->bAllowFriendlyFire = bFriendlyFire; Settings->bAllowSelfDamage = bSelfDamage; };
	for (const bool bKillPlate : { true, false })
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		World->GetWorldSettings()->DefaultGameMode = ALSShellExperimentGameMode::StaticClass();
		ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(-5000, 0, 200));
		if (!TestNotNull(TEXT("Production castle"), Castle)) return false;
		Castle->GetMovementComponent()->Deactivate();
		UClass* CoreClass = LoadClass<ALSOutpost>(nullptr, TEXT("/Game/Landship/ShellExperiment/Blueprints/BP_ShellCore.BP_ShellCore_C"));
		if (!TestNotNull(TEXT("Production core class"), CoreClass)) return false;
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Core = World->SpawnActor<ALSOutpost>(CoreClass, FVector(1000, 0, 200), FRotator::ZeroRotator, Spawn);
		if (!TestNotNull(TEXT("Core outside the complete transport route"), Core)) return false;
		auto* Setup = World->SpawnActor<ALSBattlefieldSetup>();
		Setup->PlayerCastle = Castle; Setup->CoreOutpost = Core;
		auto* Coordinator = World->SpawnActor<ALSShellExperimentCoordinator>();
		Coordinator->PlayerCastle = Castle; Coordinator->CoreOutpost = Core;
		Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
		auto* Floor = World->SpawnActor<AStaticMeshActor>(FVector(-1200, 0, -50), FRotator::ZeroRotator, Spawn);
		auto* FloorBody = Floor->GetStaticMeshComponent();
		FloorBody->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Floor->SetActorScale3D(FVector(40, 40, 1));
		FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		FloorBody->SetCollisionObjectType(LSCollisionChannels::Ground);
		FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
		auto* FloorMaterial = NewObject<UPhysicalMaterial>(Floor);
		FloorMaterial->Friction = FloorMaterial->StaticFriction = .7f;
		FloorMaterial->Restitution = .3f;
		FloorMaterial->FrictionCombineMode = FloorMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
		FloorMaterial->bOverrideFrictionCombineMode = FloorMaterial->bOverrideRestitutionCombineMode = true;
		FloorBody->SetPhysMaterialOverride(FloorMaterial);
		const FTransform Transform(FVector(-1200, 0, 70));
		auto* Vehicle = World->SpawnActorDeferred<ALSShellTransport>(ALSShellTransport::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Vehicle->RouteStart = Transform.GetLocation(); Vehicle->RouteEnd = FVector(-600, 0, 70);
		FLSShellCargoSlot PlateSlot;
		PlateSlot.CargoClass = ALSShellPlate::StaticClass();
		PlateSlot.LocalTransform.SetLocation(FVector(-100, 0, 190));
		PlateSlot.bKeepRackedOnDelivery = true;
		FLSShellCargoSlot BarrelSlot;
		BarrelSlot.CargoClass = ALSShellBarrel::StaticClass();
		BarrelSlot.LocalTransform.SetLocation(FVector(50, 0, 130));
		BarrelSlot.bInsulated = true;
		Vehicle->CargoSlots = { PlateSlot, BarrelSlot };
		Vehicle->FinishSpawning(Transform);
		if (!TestTrue(TEXT("BeginPlay with actual common GameMode"), TestWorld.BeginPlayInTestWorld())) return false;
		const TArray<ALSShellPhysicalProp*> Cargo = Vehicle->GetCargoActors();
		if (!TestEqual(TEXT("Exactly two original real cargo actors"), Cargo.Num(), 2)) return false;
		auto* Plate = Cast<ALSShellPlate>(Cargo[0]);
		if (!TestNotNull(TEXT("Actual physical plate cargo"), Plate)) return false;
		UPrimitiveComponent* OriginalPlateBody = Plate->GetPhysicalBody();
		const uint64 OriginalBodyId = Plate->Reactive->BodyId;
		FLSShellChargePacket Packet;
		Packet.PacketId = 5001; Packet.Sequence = 5001; Packet.ActivationSequence = 5001; Packet.SourceId = 5002; Packet.Amount = 90;
		if (!TestEqual(TEXT("Original retained plate stores one finite packet"), Plate->Reactive->AddCharge(Packet, 360), 90.f)) return false;
		for (int32 Step = 0; Step < 1500 && !Vehicle->HasDeliveredCargo(); ++Step) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
		if (!TestTrue(TEXT("Real loaded driver reaches delivery"), Vehicle->HasDeliveredCargo())) return false;
		TestTrue(TEXT("Retained rack parks the live vehicle"), Vehicle->DriveState == ELSShellTransportDriveState::Parked && !Vehicle->IsDriving() && Vehicle->HasRackedCargo());
		TestTrue(TEXT("Plate keeps actual structural rack"), Vehicle->Reactive->GetStructuralContactBodies().Contains(OriginalPlateBody));
		TestFalse(TEXT("Consumable barrel is physically unlatched on delivery"), Vehicle->Reactive->GetStructuralContactBodies().Contains(Cargo[1]->GetPhysicalBody()));
		TestFalse(TEXT("Cargo plate has no invisible world support"), Plate->IsSupported());
		TestFalse(TEXT("Still-racked vehicle cannot use empty exit"), Vehicle->StartExitRoute({ FVector(-400, 0, 70) }));
		const FVector Velocity = OriginalPlateBody->GetPhysicsLinearVelocity();
		const FVector Position = Plate->GetActorLocation();
		ULSHealthComponent* Receiver = bKillPlate ? Plate->GetHealthComponent() : Vehicle->GetHealthComponent();
		Receiver->ApplyDamage(ULSHealthComponent::MakeDamageRequest(1000, Castle, Castle));
		if (!TestTrue(TEXT("Same plate actor/body remain after common death"), IsValid(Plate) && !Plate->IsActorBeingDestroyed() && Plate->GetPhysicalBody() == OriginalPlateBody)) return false;
		TestFalse(TEXT("Plate or chassis death releases all remaining rack exactly once"), Vehicle->HasRackedCargo());
		TestFalse(TEXT("Released rack cannot release again"), Vehicle->ReleaseCargo());
		TestEqual(TEXT("Retained plate BodyId is unchanged"), Plate->Reactive->BodyId, OriginalBodyId);
		TestEqual(TEXT("Retained plate charge amount is unchanged"), Plate->Reactive->GetStoredCharge(), 90.f);
		if (!TestEqual(TEXT("Exactly one original plate packet remains"), Plate->Reactive->GetChargeStore().GetPackets().Num(), 1)) return false;
		TestEqual(TEXT("Same original packet remains"), Plate->Reactive->GetChargeStore().GetPackets()[0].PacketId, Packet.PacketId);
		TestTrue(TEXT("Release preserves immediate physical pose and velocity"), Plate->GetActorLocation().Equals(Position, UE_KINDA_SMALL_NUMBER) &&
			OriginalPlateBody->GetPhysicsLinearVelocity().Equals(Velocity, UE_KINDA_SMALL_NUMBER));
		TestTrue(TEXT("Surviving vehicle keeps delivered state"), Vehicle->HasDeliveredCargo());
		if (bKillPlate) TestTrue(TEXT("Last rack release permits live empty exit"), Vehicle->StartExitRoute({ FVector(-400, 0, 70) }));
		else TestTrue(TEXT("Dead chassis remains wreck and never drives"), Vehicle->DriveState == ELSShellTransportDriveState::Wreck && !Vehicle->IsDriving());
		TestWorld.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellTransportOvershootRecoveryTest,
	"Landship.ShellExperiment.Transport.ExternalOvershootReturnsToSameEndpointByForce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellTransportOvershootRecoveryTest::RunTest(const FString& Parameters)
{
	auto* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bFriendlyFire = Settings->bAllowFriendlyFire, bSelfDamage = Settings->bAllowSelfDamage;
	Settings->bAllowFriendlyFire = Settings->bAllowSelfDamage = true;
	ON_SCOPE_EXIT { Settings->bAllowFriendlyFire = bFriendlyFire; Settings->bAllowSelfDamage = bSelfDamage; };
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = ALSShellExperimentGameMode::StaticClass();
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(-5000, 0, 200));
	if (!TestNotNull(TEXT("Production castle"), Castle)) return false;
	Castle->GetMovementComponent()->Deactivate();
	UClass* CoreClass = LoadClass<ALSOutpost>(nullptr, TEXT("/Game/Landship/ShellExperiment/Blueprints/BP_ShellCore.BP_ShellCore_C"));
	if (!TestNotNull(TEXT("Production core class"), CoreClass)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Core = World->SpawnActor<ALSOutpost>(CoreClass, FVector(2000, 0, 200), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Core outside the recovery corridor"), Core)) return false;
	auto* Setup = World->SpawnActor<ALSBattlefieldSetup>();
	Setup->PlayerCastle = Castle; Setup->CoreOutpost = Core;
	auto* Coordinator = World->SpawnActor<ALSShellExperimentCoordinator>();
	Coordinator->PlayerCastle = Castle; Coordinator->CoreOutpost = Core;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	auto* Floor = World->SpawnActor<AStaticMeshActor>(FVector(-1200, 0, -50), FRotator::ZeroRotator, Spawn);
	auto* FloorBody = Floor->GetStaticMeshComponent();
	FloorBody->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Floor->SetActorScale3D(FVector(80, 40, 1));
	FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FloorBody->SetCollisionObjectType(LSCollisionChannels::Ground);
	FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
	auto* FloorMaterial = NewObject<UPhysicalMaterial>(Floor);
	FloorMaterial->Friction = FloorMaterial->StaticFriction = .7f;
	FloorMaterial->Restitution = .3f;
	FloorMaterial->FrictionCombineMode = FloorMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
	FloorMaterial->bOverrideFrictionCombineMode = FloorMaterial->bOverrideRestitutionCombineMode = true;
	FloorBody->SetPhysMaterialOverride(FloorMaterial);
	const FTransform Transform(FVector(-1200, 0, 70));
	auto* Vehicle = World->SpawnActorDeferred<ALSShellTransport>(ALSShellTransport::StaticClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Vehicle->RouteStart = Transform.GetLocation();
	Vehicle->RouteEnd = FVector(-600, 0, 70);
	Vehicle->CargoSlots.Empty();
	Vehicle->FinishSpawning(Transform);
	if (!TestTrue(TEXT("Actual GameMode BeginPlay"), TestWorld.BeginPlayInTestWorld())) return false;
	for (int32 Step = 0; Step < 1000 && Vehicle->GetActorLocation().X < Vehicle->RouteEnd.X - 200; ++Step)
	{
		++GFrameCounter; World->Tick(LEVELTICK_All, .01f);
	}
	if (!TestTrue(TEXT("Actual empty vehicle is driving before its original endpoint"), Vehicle->IsDriving() &&
		!Vehicle->HasDeliveredCargo() && Vehicle->GetActorLocation().X < Vehicle->RouteEnd.X)) return false;
	TestEqual(TEXT("Empty vehicle uses only actual chassis mass"), Vehicle->Body->GetMass(), 600.f);
	const FVector Endpoint = Vehicle->RouteEnd;
	UPrimitiveComponent* OriginalBody = Vehicle->GetPhysicalBody();
	// 위치·속도를 지정하지 않고 분류된 실제 충격 하나로 종점을 넘기는 기술용 배치다.
	if (!TestTrue(TEXT("Actual external impulse is accepted"), Vehicle->Reactive->ApplyMomentum(FVector(150, 0, 0) * Vehicle->Body->GetMass()))) return false;
	Vehicle->Reactive->NotifyExternalForce();
	float MaximumOvershoot = 0;
	float MaximumHeadingError = 0;
	bool bSawSuspended = false, bSawReverseDriving = false;
	for (int32 Step = 0; Step < 3000 && Vehicle->DriveState != ELSShellTransportDriveState::Arrived; ++Step)
	{
		++GFrameCounter; World->Tick(LEVELTICK_All, .01f);
		MaximumOvershoot = FMath::Max(MaximumOvershoot, static_cast<float>(Vehicle->GetActorLocation().X - Endpoint.X));
		MaximumHeadingError = FMath::Max(MaximumHeadingError, FMath::Abs(FMath::FindDeltaAngleDegrees(0.f, static_cast<float>(Vehicle->GetActorRotation().Yaw))));
		bSawSuspended |= Vehicle->DriveState == ELSShellTransportDriveState::Suspended;
		bSawReverseDriving |= Vehicle->IsDriving() && Vehicle->GetActorLocation().X > Endpoint.X + Vehicle->ArrivalDistance && Vehicle->Body->GetPhysicsLinearVelocity().X < -1;
	}
	if (!TestTrue(TEXT("Physical setup actually overshoots beyond25cm and the unchanged arrival tolerance"), MaximumOvershoot > FMath::Max(25.f, Vehicle->ArrivalDistance))) return false;
	TestTrue(TEXT("Classified external force suspends then resumes actual control"), bSawSuspended && bSawReverseDriving);
	TestTrue(TEXT("Force controller returns to the same endpoint and completes arrival"), Vehicle->DriveState == ELSShellTransportDriveState::Arrived &&
		FVector::DistXY(Vehicle->GetActorLocation(), Endpoint) <= Vehicle->ArrivalDistance && Vehicle->Body->GetPhysicsLinearVelocity().Size() < Vehicle->ResumeMaximumSpeed);
	TestTrue(TEXT("Endpoint and actual body identity are unchanged"), Vehicle->RouteEnd.Equals(Endpoint) && Vehicle->GetPhysicalBody() == OriginalBody);
	TestTrue(TEXT("Near-end heading remains in original forward hemisphere while reversing"), MaximumHeadingError < 90);
	AddInfo(FString::Printf(TEXT("Transport physical overshoot recovery maximumOvershoot=%.3f maximumYawError=%.3f suspended=%d reverseDrive=%d final=%s velocity=%s state=%s"),
		MaximumOvershoot, MaximumHeadingError, bSawSuspended, bSawReverseDriving, *Vehicle->GetActorLocation().ToCompactString(),
		*Vehicle->Body->GetPhysicsLinearVelocity().ToCompactString(), *Vehicle->GetDriveStateText()));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellTransportAlignedUnloadTest,
	"Landship.ShellExperiment.Transport.SouthRouteAlignsBeforeUnloadAndRetainsLandedBall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellTransportAlignedUnloadTest::RunTest(const FString& Parameters)
{
	auto* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	const bool bFriendlyFire = Settings->bAllowFriendlyFire, bSelfDamage = Settings->bAllowSelfDamage;
	Settings->bAllowFriendlyFire = Settings->bAllowSelfDamage = true;
	ON_SCOPE_EXIT { Settings->bAllowFriendlyFire = bFriendlyFire; Settings->bAllowSelfDamage = bSelfDamage; };
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = ALSShellExperimentGameMode::StaticClass();
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(-1800, 0, 500));
	if (!TestNotNull(TEXT("Production castle"), Castle)) return false;
	Castle->GetMovementComponent()->Deactivate();
	UClass* CoreClass = LoadClass<ALSOutpost>(nullptr, TEXT("/Game/Landship/ShellExperiment/Blueprints/BP_ShellCore.BP_ShellCore_C"));
	if (!TestNotNull(TEXT("Production core class"), CoreClass)) return false;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Core = World->SpawnActor<ALSOutpost>(CoreClass, FVector(2400, 0, 400), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Actual core geometry outside the south route"), Core)) return false;
	auto* Setup = World->SpawnActor<ALSBattlefieldSetup>();
	Setup->PlayerCastle = Castle; Setup->CoreOutpost = Core;
	auto* Coordinator = World->SpawnActor<ALSShellExperimentCoordinator>();
	Coordinator->PlayerCastle = Castle; Coordinator->CoreOutpost = Core;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	auto* FloorMaterial = NewObject<UPhysicalMaterial>(World);
	FloorMaterial->Friction = FloorMaterial->StaticFriction = .7f;
	FloorMaterial->Restitution = .3f;
	FloorMaterial->FrictionCombineMode = FloorMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
	FloorMaterial->bOverrideFrictionCombineMode = FloorMaterial->bOverrideRestitutionCombineMode = true;
	auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0, 0, -50), FRotator::ZeroRotator, Spawn);
	Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Floor->SetActorScale3D(FVector(64, 40, 1));
	Floor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Floor->GetStaticMeshComponent()->SetCollisionObjectType(LSCollisionChannels::Ground);
	Floor->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
	Floor->GetStaticMeshComponent()->SetPhysMaterialOverride(FloorMaterial);
	auto* Apron = World->SpawnActor<AStaticMeshActor>(FVector(3600, 0, -51), FRotator::ZeroRotator, Spawn);
	Apron->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Apron->SetActorScale3D(FVector(8, 40, 1));
	Apron->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Apron->GetStaticMeshComponent()->SetCollisionObjectType(LSCollisionChannels::Ground);
	Apron->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
	Apron->GetStaticMeshComponent()->SetPhysMaterialOverride(FloorMaterial);
	auto* Battlefield = World->SpawnActor<ALSBattlefieldSettings>();
	Battlefield->BoundsMin = FVector2D(-3200, -2000); Battlefield->BoundsMax = FVector2D(3200, 2000);
	Battlefield->TerrainActors = { Floor };
	const FVector Start(-800, -1000, 70), End(700, -1550, 70);
	const TArray<FVector> ExitWaypoints { FVector(1580, -1550, 70), FVector(1580, -1800, 70), FVector(3800, -1800, 70) };
	const FTransform Transform((End - Start).Rotation(), Start);
	auto* Vehicle = World->SpawnActorDeferred<ALSShellTransport>(ALSShellTransport::StaticClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Vehicle->RouteStart = Start; Vehicle->RouteEnd = End;
	if (!TestTrue(TEXT("Actual first empty-exit direction configures delivery alignment"), Vehicle->SetDeliveryExitDirection(ExitWaypoints[0] - End))) return false;
	Vehicle->FinishSpawning(Transform);
	if (!TestTrue(TEXT("Actual GameMode BeginPlay"), TestWorld.BeginPlayInTestWorld())) return false;
	if (!TestTrue(TEXT("Coordinator active"), Coordinator->IsBattleActive())) return false;
	const TArray<ALSShellPhysicalProp*> Cargo = Vehicle->GetCargoActors();
	if (!TestEqual(TEXT("Unmodified normal load contains two actual cargo actors"), Cargo.Num(), 2)) return false;
	auto* Ball = Cast<ALSShellMetalBall>(Cargo[0]);
	auto* Barrel = Cast<ALSShellBarrel>(Cargo[1]);
	if (!TestNotNull(TEXT("Original cargo metal ball"), Ball) || !TestNotNull(TEXT("Original cargo barrel"), Barrel)) return false;
	TWeakObjectPtr<ALSShellTransport> VehicleLife = Vehicle;
	TWeakObjectPtr<ALSShellMetalBall> BallLife = Ball;
	TWeakObjectPtr<ALSShellBarrel> BarrelLife = Barrel;
	const uint64 BallId = Ball->Reactive->BodyId, BarrelId = Barrel->Reactive->BodyId;
	TestTrue(TEXT("Actual physical masses remain600/750/60"), FMath::IsNearlyEqual(Vehicle->Body->GetMass(), 600.f, .01f) &&
		FMath::IsNearlyEqual(Ball->Body->GetMass(), 750.f, .01f) && FMath::IsNearlyEqual(Barrel->Body->GetMass(), 60.f, .01f));
	bool bSawAlignment = false, bExitStarted = false, bPassedFirstTurn = false, bBallStayedInBounds = true;
	double ReleaseTime = -1, LandingTime = -1;
	float ReleaseYaw = 0, ReleaseAngularSpeed = 0, ExitDistance = 0;
	FVector VehicleReleaseVelocity = FVector::ZeroVector, BallReleaseVelocity = FVector::ZeroVector, BarrelReleaseVelocity = FVector::ZeroVector;
	FVector LastVehiclePosition = Start, LastBallPosition = Ball->GetActorLocation(), LastBallVelocity = FVector::ZeroVector;
	FVector LastBarrelPosition = Barrel->GetActorLocation(), LastBarrelVelocity = FVector::ZeroVector;
	for (int32 Step = 0; Step < 6000; ++Step)
	{
		++GFrameCounter; World->Tick(LEVELTICK_All, .01f);
		const double Now = World->GetTimeSeconds();
		if (VehicleLife.IsValid())
		{
			bSawAlignment |= VehicleLife->DriveState == ELSShellTransportDriveState::Aligning;
			if (ReleaseTime < 0 && VehicleLife->HasDeliveredCargo())
			{
				ReleaseTime = Now;
				ReleaseYaw = VehicleLife->GetActorRotation().Yaw;
				ReleaseAngularSpeed = FMath::RadiansToDegrees(VehicleLife->Body->GetPhysicsAngularVelocityInRadians().Size());
				VehicleReleaseVelocity = VehicleLife->Body->GetPhysicsLinearVelocity();
				if (!TestTrue(TEXT("Original cargo survive alignment and release"), BallLife.IsValid() && BarrelLife.IsValid())) return false;
				BallReleaseVelocity = BallLife->Body->GetPhysicsLinearVelocity();
				BarrelReleaseVelocity = BarrelLife->Body->GetPhysicsLinearVelocity();
				if (!TestTrue(TEXT("Normal cargo is unlatched after physical alignment"), !VehicleLife->HasRackedCargo() && VehicleLife->GetCargoActors().Num() == 2)) return false;
				bExitStarted = VehicleLife->StartExitRoute(ExitWaypoints);
				if (!TestTrue(TEXT("Actual empty exit begins without pose or velocity assignment"), bExitStarted)) return false;
				LastVehiclePosition = VehicleLife->GetActorLocation();
			}
			if (bExitStarted)
			{
				ExitDistance += FVector::DistXY(LastVehiclePosition, VehicleLife->GetActorLocation());
				LastVehiclePosition = VehicleLife->GetActorLocation();
				bPassedFirstTurn |= VehicleLife->RouteEnd.Equals(ExitWaypoints[2], 1.f);
			}
		}
		if (!BallLife.IsValid()) { bBallStayedInBounds = false; break; }
		LastBallPosition = BallLife->GetActorLocation(); LastBallVelocity = BallLife->Body->GetPhysicsLinearVelocity();
		if (LandingTime < 0 && ReleaseTime >= 0 && BallLife->Reactive->GetContinuingContactEpoch(Floor->GetStaticMeshComponent()) != 0) LandingTime = Now;
		if (LandingTime >= 0)
		{
			const FBox Bounds = BallLife->Body->Bounds.GetBox();
			bBallStayedInBounds &= Bounds.Min.X >= -3200 && Bounds.Max.X <= 3200 && Bounds.Min.Y >= -2000 && Bounds.Max.Y <= 2000;
			FLSHealthSnapshot Health;
			bBallStayedInBounds &= BallLife->GetHealthComponent()->TryGetHealth(Health) && Health.CurrentHealth > 0;
		}
		if (BarrelLife.IsValid()) { LastBarrelPosition = BarrelLife->GetActorLocation(); LastBarrelVelocity = BarrelLife->Body->GetPhysicsLinearVelocity(); }
		if (!bBallStayedInBounds || (LandingTime >= 0 && Now - LandingTime >= 10 && ExitDistance >= 1000 && bPassedFirstTurn)) break;
	}
	AddInfo(FString::Printf(TEXT("Aligned south-route unload ballId=%llu barrelId=%llu align=%d releaseTime=%.3f yaw=%.3f angularDeg=%.3f chassisReleaseV=%s ballReleaseV=%s barrelReleaseV=%s landingTime=%.3f observedUntil=%.3f exitCm=%.3f firstTurn=%d ballPos=%s ballV=%s barrelPresent=%d barrelPos=%s barrelV=%s"),
		BallId, BarrelId, bSawAlignment, ReleaseTime, ReleaseYaw, ReleaseAngularSpeed, *VehicleReleaseVelocity.ToCompactString(), *BallReleaseVelocity.ToCompactString(), *BarrelReleaseVelocity.ToCompactString(),
		LandingTime, World->GetTimeSeconds(), ExitDistance, bPassedFirstTurn, *LastBallPosition.ToCompactString(), *LastBallVelocity.ToCompactString(),
		BarrelLife.IsValid(), *LastBarrelPosition.ToCompactString(), *LastBarrelVelocity.ToCompactString()));
	TestTrue(TEXT("Incoming south heading physically aligns before release"), bSawAlignment && ReleaseTime >= 0 &&
		FMath::Abs(ReleaseYaw) <= Vehicle->DeliveryHeadingToleranceDegrees && ReleaseAngularSpeed <= Vehicle->DeliveryAngularSpeedToleranceDegrees);
	TestTrue(TEXT("Actual empty truck passes its first turn and travels at least1000cm"), bExitStarted && bPassedFirstTurn && ExitDistance >= 1000);
	TestTrue(TEXT("Original cargo ball actually lands and remains alive inside battlefield for at least10game seconds"),
		LandingTime >= 0 && World->GetTimeSeconds() - LandingTime >= 10 && bBallStayedInBounds && BallLife.IsValid() && BallLife->Reactive->BodyId == BallId);
	TestEqual(TEXT("No blast or gameplay effect was injected"), Coordinator->ShellHitCount + Coordinator->ModuleCount + Coordinator->DetonationCount, 0);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
