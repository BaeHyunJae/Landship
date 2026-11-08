#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellNavigationBodyComponent.h"
#include "ShellExperiment/LSShellObstacleResponseComponent.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "ShellExperiment/LSShellMaterials.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSPhysicalBlocker.h"
#include "Combat/LSCollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

struct FLSShellObstacleResponseTestAccess
{
	static UPrimitiveComponent* Probe(ULSShellObstacleResponseComponent* Response)
	{
		FVector Direction;
		return Response->FindFirstBlocker(Direction);
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellNavigationBodyLifecycleTest, "Landship.ShellExperiment.Navigation.ActualPrimitiveSettlesMovesAndDisables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellNavigationBodyLifecycleTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	AActor* Owner = World->SpawnActor<AActor>();
	auto* Body = NewObject<UBoxComponent>(Owner);
	Owner->SetRootComponent(Body);
	Owner->AddInstanceComponent(Body);
	Body->SetBoxExtent(FVector(40, 60, 30));
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	auto* Tracking = NewObject<ULSShellNavigationBodyComponent>(Owner);
	Owner->AddInstanceComponent(Tracking);
	Tracking->SetNavigationBody(Body);
	TestFalse(TEXT("Actual body excluded before primitive registration"), Body->CanEverAffectNavigation());
	Body->RegisterComponent();
	Tracking->RegisterComponent();
	if (!Fixture.BeginPlayInTestWorld()) return false;
	Tracking->SetTrackingEnabled(true);
	TestEqual(TEXT("Component provider opts the exact primitive into physical cover"), LSPhysicalBlocker::FindBlockingBody(Owner), static_cast<UPrimitiveComponent*>(Body));
	for (int32 Step = 0; Step < 49; ++Step) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
	TestFalse(TEXT("Less than half a second is not settled"), Body->CanEverAffectNavigation());
	for (int32 Step = 0; Step < 3; ++Step) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
	TestTrue(TEXT("Settled actual primitive is registered through native navigation flag"), Body->CanEverAffectNavigation());
	const uint32 SettledRevision = Tracking->NavigationRevision;
	Owner->SetActorLocation(FVector(11, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
	++GFrameCounter; World->Tick(LEVELTICK_All, .01f);
	TestFalse(TEXT("More than ten centimeters invalidates registration"), Body->CanEverAffectNavigation());
	TestTrue(TEXT("Native registration transition is observable"), Tracking->NavigationRevision > SettledRevision);
	const uint32 BeforeCreepRevision = Tracking->NavigationRevision;
	// 실제 변위 조건만 고립시키는 기술용 연속 이동이다. 속도 상한보다 느린 5cm/s를 재현한다.
	for (int32 Step = 0; Step < 200; ++Step)
	{
		Owner->SetActorLocation(FVector(11 + (Step + 1) * .05, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		++GFrameCounter; World->Tick(LEVELTICK_All, .01f);
		if (!TestFalse(TEXT("Five-centimeter-per-second drift cannot pass half-second settling window"), Body->CanEverAffectNavigation())) return false;
	}
	TestEqual(TEXT("Slow continuous drift does not repeatedly toggle native nav registration"), Tracking->NavigationRevision, BeforeCreepRevision);
	for (int32 Step = 0; Step < 52; ++Step) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
	TestTrue(TEXT("Moved primitive can settle again"), Body->CanEverAffectNavigation());
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	++GFrameCounter; World->Tick(LEVELTICK_All, .01f);
	TestFalse(TEXT("Collision removal unregisters the body"), Body->CanEverAffectNavigation());
	TestNull(TEXT("Removed query body no longer blocks shared combat"), LSPhysicalBlocker::FindBlockingBody(Owner));
	Tracking->SetTrackingEnabled(false);
	for (int32 ShapeCase = 0; ShapeCase < 4; ++ShapeCase)
	{
		AActor* RotatingOwner = World->SpawnActor<AActor>();
		UShapeComponent* RotatingBody = nullptr;
		if (ShapeCase == 0)
		{
			auto* Sphere = NewObject<USphereComponent>(RotatingOwner);
			Sphere->InitSphereRadius(70); RotatingBody = Sphere;
		}
		else if (ShapeCase < 3)
		{
			auto* Capsule = NewObject<UCapsuleComponent>(RotatingOwner);
			Capsule->InitCapsuleSize(70, ShapeCase == 1 ? 70 : 100); RotatingBody = Capsule;
		}
		else
		{
			auto* Box = NewObject<UBoxComponent>(RotatingOwner);
			Box->SetBoxExtent(FVector(70, 40, 30)); RotatingBody = Box;
		}
		RotatingOwner->SetRootComponent(RotatingBody); RotatingOwner->AddInstanceComponent(RotatingBody);
		RotatingBody->SetMobility(EComponentMobility::Movable);
		RotatingBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		RotatingBody->SetCollisionResponseToAllChannels(ECR_Block);
		RotatingBody->SetWorldLocation(FVector(-1000, 1000 + ShapeCase * 500, 500));
		auto* RotationTracking = NewObject<ULSShellNavigationBodyComponent>(RotatingOwner);
		RotatingOwner->AddInstanceComponent(RotationTracking);
		RotationTracking->SetNavigationBody(RotatingBody);
		RotatingBody->RegisterComponent(); RotationTracking->RegisterComponent();
		RotatingBody->SetEnableGravity(false);
		RotatingBody->SetLinearDamping(0); RotatingBody->SetAngularDamping(0);
		RotatingBody->SetSimulatePhysics(true);
		// 중력·접촉·중심 이동을 분리하고 native 각속도만 주는 기술용 배치다.
		RotatingBody->SetPhysicsAngularVelocityInDegrees(FVector(0, 30, 0));
		RotationTracking->SetTrackingEnabled(true);
		const FVector FixedCenter = RotatingBody->GetComponentLocation();
		const FQuat FirstRotation = RotatingBody->GetComponentQuat();
		for (int32 Step = 0; Step < 75; ++Step) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
		if (!TestTrue(TEXT("Rotation instrument reaches actual awake solver with stationary center"),
			RotatingBody->IsSimulatingPhysics() && RotatingBody->GetPhysicsAngularVelocityInDegrees().Size() > 5 &&
			RotatingBody->GetComponentLocation().Equals(FixedCenter, .1) &&
			RotatingBody->GetComponentQuat().AngularDistance(FirstRotation) > FMath::DegreesToRadians(5.f))) return false;
		if (ShapeCase < 2)
		{
			if (!TestTrue(TEXT("Stationary spinning sphere or spherical capsule can settle"), RotationTracking->IsNavigationRegistered())) return false;
			const uint32 RotationRevision = RotationTracking->NavigationRevision;
			RotatingBody->SetPhysicsAngularVelocityInDegrees(FVector(0, 60, 0));
			for (int32 Step = 0; Step < 60; ++Step) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
			TestTrue(TEXT("Rotation alone retains unchanged spherical nav footprint"), RotationTracking->IsNavigationRegistered());
			TestEqual(TEXT("Spherical native rotation cannot churn registration revision"), RotationTracking->NavigationRevision, RotationRevision);
		}
		else if (ShapeCase == 2)
		{
			TestFalse(TEXT("Ordinary capsule still rejects actual tilt motion"), RotationTracking->IsNavigationRegistered());
			RotationTracking->SetTrackingEnabled(false); RotationTracking->SetTrackingEnabled(true);
			const FVector CapsuleAxis = RotatingBody->GetUpVector();
			const FQuat BeforeAxialSpin = RotatingBody->GetComponentQuat();
			RotatingBody->SetPhysicsAngularVelocityInDegrees(CapsuleAxis * 30);
			for (int32 Step = 0; Step < 75; ++Step) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
			if (!TestTrue(TEXT("Longitudinal-axis capsule spin permits settling"), RotationTracking->IsNavigationRegistered())) return false;
			const uint32 AxialRevision = RotationTracking->NavigationRevision;
			RotatingBody->SetPhysicsAngularVelocityInDegrees(RotatingBody->GetUpVector() * 60);
			for (int32 Step = 0; Step < 60; ++Step) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
			TestTrue(TEXT("Capsule axial instrument rotates quaternion while retaining actual capsule axis"),
				RotatingBody->GetComponentQuat().AngularDistance(BeforeAxialSpin) > FMath::DegreesToRadians(5.f) &&
				FMath::Abs(FVector::DotProduct(RotatingBody->GetUpVector(), CapsuleAxis)) > .999);
			TestTrue(TEXT("Capsule axial spin retains registered footprint"), RotationTracking->IsNavigationRegistered());
			TestEqual(TEXT("Capsule axial spin cannot churn registration revision"), RotationTracking->NavigationRevision, AxialRevision);
		}
		else TestFalse(TEXT("Box keeps full angular settling threshold"), RotationTracking->IsNavigationRegistered());
		AddInfo(FString::Printf(TEXT("Native nav rotation case=%d shape=%s angularSpeed=%.3f centerDisplacement=%.5f registered=%d revision=%u"),
			ShapeCase, *RotatingBody->GetClass()->GetName(), RotatingBody->GetPhysicsAngularVelocityInDegrees().Size(),
			FVector::Distance(RotatingBody->GetComponentLocation(), FixedCenter), RotationTracking->IsNavigationRegistered(),
			RotationTracking->NavigationRevision));
		if (ShapeCase == 0)
		{
			RotationTracking->SetTrackingEnabled(false); RotationTracking->SetTrackingEnabled(true);
			RotatingBody->SetPhysicsLinearVelocity(FVector(5, 0, 0));
			RotatingBody->SetPhysicsAngularVelocityInDegrees(FVector(0, FMath::RadiansToDegrees(5.f / 70), 0));
			const FVector MoveStart = RotatingBody->GetComponentLocation();
			for (int32 Step = 0; Step < 120; ++Step)
			{
				++GFrameCounter; World->Tick(LEVELTICK_All, .01f);
				if (!TestFalse(TEXT("Actual5cm/smoving sphere still fails1cmsettle window"), RotationTracking->IsNavigationRegistered())) return false;
			}
			TestTrue(TEXT("Moving-sphere instrument observes real translation"), RotatingBody->GetComponentLocation().X - MoveStart.X > 5);
		}
		RotationTracking->SetTrackingEnabled(false);
	}
	// Heavy의 실제140/140 캡슐 관성을 관측하고 같은 pose의 무장 없는 box 포가로 이전한다.
	AActor* Original = World->SpawnActor<AActor>();
	auto* OriginalBody = NewObject<UCapsuleComponent>(Original);
	Original->SetRootComponent(OriginalBody); Original->AddInstanceComponent(OriginalBody);
	OriginalBody->SetCanEverAffectNavigation(false);
	OriginalBody->SetMobility(EComponentMobility::Movable);
	OriginalBody->InitCapsuleSize(140, 140);
	OriginalBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	OriginalBody->SetCollisionResponseToAllChannels(ECR_Block);
	OriginalBody->SetWorldTransform(FTransform(FRotator(0, 35, 0), FVector(1000, 1000, 500)));
	OriginalBody->RegisterComponent();
	OriginalBody->SetMassOverrideInKg(NAME_None, 123, true);
	OriginalBody->SetEnableGravity(false);
	OriginalBody->SetSimulatePhysics(true);
	OriginalBody->SetPhysicsLinearVelocity(FVector(20, -10, 0));
	OriginalBody->SetPhysicsAngularVelocityInRadians(FVector(.3, .2, .4));
	FLSShellRigidBodyState State;
	State.bValid = true; State.Shape = ELSShellBodyShape::Capsule; State.ShapeExtent = FVector(140);
	State.Transform = OriginalBody->GetComponentTransform(); State.MassKg = OriginalBody->GetMass();
	State.LinearVelocity = OriginalBody->GetPhysicsLinearVelocity();
	State.AngularVelocity = OriginalBody->GetPhysicsAngularVelocityInRadians();
	State.AngularMomentum = OriginalBody->ScaleByMomentOfInertia(State.AngularVelocity);
	State.bHasAngularMomentum = true;
	Original->Destroy();
	auto* Carriage = World->SpawnActorDeferred<ALSShellSiegeCarriage>(ALSShellSiegeCarriage::StaticClass(), State.Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Deferred inert carriage"), Carriage) ||
		!TestTrue(TEXT("Carriage accepts captured Heavy state"), Carriage->InitializeBody(State))) return false;
	Carriage->FinishSpawning(State.Transform);
	auto* CarriageBox = Cast<UBoxComponent>(Carriage->GetPhysicalBody());
	if (!TestNotNull(TEXT("Heavy carriage has actual non-rolling box collision"), CarriageBox)) return false;
	TestTrue(TEXT("Box half extents match Heavy240x240x220body visual"), CarriageBox->GetUnscaledBoxExtent().Equals(FVector(120, 120, 110), .01));
	TestTrue(TEXT("Replacement retains original pose and mass"), CarriageBox->GetComponentTransform().Equals(State.Transform, .01) &&
		FMath::IsNearlyEqual(CarriageBox->GetMass(), State.MassKg, .01f));
	TestTrue(TEXT("Box replacement preserves actual linear momentum"),
		(CarriageBox->GetPhysicsLinearVelocity() * CarriageBox->GetMass()).Equals(State.LinearVelocity * State.MassKg, .1));
	TestTrue(TEXT("Box replacement recalculates angular velocity to preserve actual angular momentum"),
		CarriageBox->ScaleByMomentOfInertia(CarriageBox->GetPhysicsAngularVelocityInRadians()).Equals(State.AngularMomentum, 10));
	TestTrue(TEXT("Carriage stays physically colliding"), CarriageBox->IsSimulatingPhysics() && CarriageBox->IsQueryCollisionEnabled());
	TestNull(TEXT("Carriage has no second Health"), Carriage->FindComponentByClass<ULSHealthComponent>());
	TestNull(TEXT("Carriage has no armament"), Carriage->FindComponentByClass<ULSBasicAttackComponent>());
	Fixture.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellClearingForceTest, "Landship.ShellExperiment.Navigation.ClearingForceUsesMassAndFrictionBound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellClearingForceTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Six hundred kg wreck uses friction plus one hundred net acceleration"),
		ULSShellObstacleResponseComponent::ComputeClearingForce(600, .35f, 980, 100), 265800.f);
	TestTrue(TEXT("Wreck clearing exceeds static friction instead of fixed120000"),
		ULSShellObstacleResponseComponent::ComputeClearingForce(600, .35f, 980, 100) > 600 * .35f * 980);
	TestEqual(TEXT("Actual lighter mass scales the same net acceleration"),
		ULSShellObstacleResponseComponent::ComputeClearingForce(60, .4f, 980, 100), 29520.f);
	TestEqual(TEXT("Missing physical mass cannot invent a force"), ULSShellObstacleResponseComponent::ComputeClearingForce(0, .35f, 980, 100), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellStructuralClearingMassTest, "Landship.ShellExperiment.Navigation.ClearingMassCountsStructuralCycleOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellStructuralClearingMassTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	TArray<UPrimitiveComponent*> Bodies;
	TArray<ULSShellReactiveComponent*> Reactives;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		AActor* Owner = World->SpawnActor<AActor>();
		auto* Body = NewObject<UBoxComponent>(Owner);
		Owner->SetRootComponent(Body);
		Owner->AddInstanceComponent(Body);
		Body->SetBoxExtent(FVector(20));
		Body->SetMobility(EComponentMobility::Movable);
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Body->SetCollisionObjectType(LSCollisionChannels::Building);
		Body->RegisterComponent();
		Owner->SetActorLocation(FVector(Index * 100, 0, 200));
		Body->SetMassOverrideInKg(NAME_None, (Index + 1) * 100, true);
		Body->SetSimulatePhysics(true);
		auto* Reactive = NewObject<ULSShellReactiveComponent>(Owner);
		Owner->AddInstanceComponent(Reactive);
		Reactive->SetPhysicalBody(Body);
		Reactive->RegisterComponent();
		Bodies.Add(Body);
		Reactives.Add(Reactive);
	}
	if (!Fixture.BeginPlayInTestWorld()) return false;
	for (UPrimitiveComponent* Body : Bodies)
		if (!TestTrue(TEXT("Fixture has a real simulated body"), Body->IsSimulatingPhysics())) return false;
	Reactives[0]->SetStructuralContact(Reactives[1], false);
	Reactives[1]->SetStructuralContact(Reactives[2], true);
	Reactives[2]->SetStructuralContact(Reactives[0], false);
	const TArray<UPrimitiveComponent*> Assembly = ULSShellObstacleResponseComponent::CollectStructuralBodies(Bodies[0]);
	TestEqual(TEXT("Cycle and symmetric edges do not duplicate a body"), Assembly.Num(), 3);
	float Mass = 0;
	for (UPrimitiveComponent* Body : Assembly) Mass += Body->GetMass();
	TestTrue(TEXT("Insulation does not remove actual carried mass"), FMath::IsNearlyEqual(Mass, 600.f, .1f));
	Reactives[0]->ReleaseStructuralContact(Reactives[2]);
	Reactives[1]->ReleaseStructuralContact(Reactives[2]);
	TestEqual(TEXT("Released body leaves the structural assembly without a proximity substitution"),
		ULSShellObstacleResponseComponent::CollectStructuralBodies(Bodies[0]).Num(), 2);
	Fixture.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellNativeCorridorProbeTest, "Landship.ShellExperiment.Navigation.BlockerProbeFollowsNativePathCorners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellNativeCorridorProbeTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	auto* Character = World->SpawnActor<ACharacter>(FVector(0, 0, 88), FRotator::ZeroRotator);
	auto* Controller = World->SpawnActor<AAIController>();
	Controller->Possess(Character);
	auto* Response = NewObject<ULSShellObstacleResponseComponent>(Character);
	Character->AddInstanceComponent(Response);
	Response->RegisterComponent();
	AActor* Cover = World->SpawnActor<AActor>();
	auto* Body = NewObject<UBoxComponent>(Cover);
	Cover->SetRootComponent(Body);
	Cover->AddInstanceComponent(Body);
	Body->SetBoxExtent(FVector(15));
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Body->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
	auto* Tracking = NewObject<ULSShellNavigationBodyComponent>(Cover);
	Cover->AddInstanceComponent(Tracking);
	Tracking->SetNavigationBody(Body);
	Body->RegisterComponent();
	Tracking->RegisterComponent();
	Cover->SetActorLocation(FVector(180, 0, 88));
	if (!Fixture.BeginPlayInTestWorld()) return false;
	Tracking->SetTrackingEnabled(true);
	const TArray<FVector> Points { FVector(0, 0, 0), FVector(200, 0, 0), FVector(200, 200, 0) };
	FNavPathSharedPtr Path = MakeShared<FNavigationPath, ESPMode::ThreadSafe>(Points);
	FAIMoveRequest Request(Points.Last());
	Request.SetAllowPartialPath(false);
	Controller->RequestMove(Request, Path);
	if (!TestTrue(TEXT("Instrument installs a ready native path in actual path following"),
		Controller->GetPathFollowingComponent()->HasValidPath())) return false;
	TestEqual(TEXT("Probe reaches a blocker on the first leg rather than cutting directly to the goal"),
		FLSShellObstacleResponseTestAccess::Probe(Response), static_cast<UPrimitiveComponent*>(Body));
	TestEqual(TEXT("Observer identifies native path point source"), Response->LastProbeSource, FString(TEXT("native path points")));
	Cover->SetActorLocation(FVector(100, 100, 88), false, nullptr, ETeleportType::TeleportPhysics);
	TestNull(TEXT("A body only on the strategic diagonal is not on the native cornered corridor"),
		FLSShellObstacleResponseTestAccess::Probe(Response));
	Fixture.ForwardErrorMessages(this);
	return true;
}

#endif
