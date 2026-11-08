#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellMaterials.h"
#include "ShellExperiment/LSShellMaterialStateComponent.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Tests/AutomationCommon.h"

namespace
{
bool BeginMaterialStateWorld(FTestWorldWrapper& TestWorld)
{
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	TestWorld.GetTestWorld()->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
	return TestWorld.BeginPlayInTestWorld();
}

template <typename T>
T* SpawnMaterialStateActor(UWorld* World, FVector Point, FVector Velocity = FVector::ZeroVector)
{
	FLSShellRigidBodyState State;
	State.bValid = true;
	State.Transform = FTransform(FRotator::ZeroRotator, Point);
	State.ShapeExtent = FVector(17, 17, 53);
	State.MassKg = 123;
	State.LinearVelocity = Velocity;
	State.AngularVelocity = FVector(.2f, .3f, -.4f);
	T* Actor = World->SpawnActorDeferred<T>(T::StaticClass(), State.Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Actor) return nullptr;
	if (!Actor->InitializeBody(State)) { Actor->Destroy(); return nullptr; }
	Actor->FinishSpawning(State.Transform);
	return Actor;
}

void TickMaterialStateWorld(UWorld* World, int32 Steps)
{
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, .1f);
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellInertMaterialStateTest,
	"Landship.ShellExperiment.Materials.InertIceUsesCapturedShapeMassMotionWithoutHealth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellInertMaterialStateTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("Game world starts without combat actors"), BeginMaterialStateWorld(TestWorld))) return false;
	UWorld* World = TestWorld.GetTestWorld();
	const FVector Velocity(285, -47, 91);
	auto* Ice = SpawnMaterialStateActor<ALSShellInertIce>(World, FVector(0, 0, 500), Velocity);
	if (!TestNotNull(TEXT("Inert ice created through deferred state initialization"), Ice)) return false;
	auto* Body = Cast<UCapsuleComponent>(Ice->GetPhysicalBody());
	if (!TestNotNull(TEXT("Exact capsule collision shape"), Body)) return false;
	TestNull(TEXT("No second Health component"), Ice->FindComponentByClass<ULSHealthComponent>());
	TestTrue(TEXT("Original shape dimensions"), Body->GetUnscaledCapsuleRadius() == 17 && Body->GetUnscaledCapsuleHalfHeight() == 53);
	TestTrue(TEXT("Captured mass"), FMath::IsNearlyEqual(Body->GetMass(), 123.f, .01f));
	TestTrue(TEXT("Captured linear velocity"), Body->GetPhysicsLinearVelocity().Equals(Velocity, .01f));
	TestTrue(TEXT("Captured angular velocity"), Body->GetPhysicsAngularVelocityInRadians().Equals(FVector(.2f, .3f, -.4f), .01f));
	TestTrue(TEXT("Permanent cold state"), Ice->MaterialState->IsCooled() && Ice->MaterialState->GetColdSecondsRemaining() == -1);
	TickMaterialStateWorld(World, 41);
	TestTrue(TEXT("Dead ice cannot thaw after the living duration"), IsValid(Ice) && Ice->MaterialState->IsCooled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellMaterialColdRefreshTest,
	"Landship.ShellExperiment.Materials.CoolingUsesMaximumRefreshAndRestoresFriction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellMaterialColdRefreshTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!BeginMaterialStateWorld(TestWorld)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Carriage = SpawnMaterialStateActor<ALSShellSiegeCarriage>(World, FVector(0, 0, 500));
	if (!TestNotNull(TEXT("Inert physical carriage"), Carriage)) return false;
	auto* Material = Carriage->MaterialState.Get();
	UPrimitiveComponent* Body = Carriage->GetPhysicalBody();
	UPhysicalMaterial* Original = Body->BodyInstance.GetSimplePhysicalMaterial();
	TestTrue(TEXT("Finite cold accepted"), Material->ApplyCooling(4));
	TestTrue(TEXT("Actual cold friction"), FMath::IsNearlyEqual(Body->BodyInstance.GetSimplePhysicalMaterial()->Friction, .05f));
	TickMaterialStateWorld(World, 5);
	const float Remaining = Material->GetColdSecondsRemaining();
	Material->ApplyCooling(1);
	TestTrue(TEXT("Short refresh neither adds nor shortens duration"), FMath::IsNearlyEqual(Material->GetColdSecondsRemaining(), Remaining, .001f));
	Material->ApplyCooling(4);
	TestTrue(TEXT("Full refresh sets four seconds"), FMath::IsNearlyEqual(Material->GetColdSecondsRemaining(), 4.f, .001f));
	TestEqual(TEXT("Cold kinetic multiplier"), Material->GetKineticDamageMultiplier(), 2.f);
	TestFalse(TEXT("Below shatter threshold"), Material->ShouldShatter(899));
	TestTrue(TEXT("At shatter threshold"), Material->ShouldShatter(900));
	TickMaterialStateWorld(World, 41);
	TestFalse(TEXT("Finite material thaws"), Material->IsCooled());
	TestTrue(TEXT("Original actual physical material restored"), Body->BodyInstance.GetSimplePhysicalMaterial() == Original);
	TestNull(TEXT("Carriage has no replacement Health pool"), Carriage->FindComponentByClass<ULSHealthComponent>());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellMaterialBondBudgetTest,
	"Landship.ShellExperiment.Materials.AdhesionConsumesSlotsAndKeepsEpochOnRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellMaterialBondBudgetTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!BeginMaterialStateWorld(TestWorld)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* A = SpawnMaterialStateActor<ALSShellSiegeCarriage>(World, FVector(0, 0, 500));
	auto* B = SpawnMaterialStateActor<ALSShellSiegeCarriage>(World, FVector(34, 0, 500));
	if (!TestNotNull(TEXT("First body"), A) || !TestNotNull(TEXT("Second body"), B)) return false;
	// 네이티브 Hit 시험과 별개로 공개 접촉 어댑터 이후의 결속·슬롯 계약을 격리한다.
	A->Reactive->SetStructuralContact(B->Reactive, false);
	A->Reactive->ReleaseStructuralContact(B->Reactive);
	const uint64 Epoch = A->Reactive->GetContinuingContactEpoch(B->GetPhysicalBody());
	TestTrue(TEXT("Technical contact fixture has an epoch"), Epoch != 0);
	TestEqual(TEXT("One allocated slot accepted"), A->MaterialState->ApplyAdhesion(1), 1);
	TestEqual(TEXT("Actual constraint consumes slot"), A->MaterialState->GetAvailableBondSlots(), 0);
	TestEqual(TEXT("One owned bond"), A->MaterialState->GetActiveBondCount(), 1);
	TestEqual(TEXT("Peer sees same bond"), B->MaterialState->GetActiveBondCount(), 1);
	A->MaterialState->ReleaseAllBonds();
	TestEqual(TEXT("Released slot remains spent"), A->MaterialState->GetAvailableBondSlots(), 0);
	TestEqual(TEXT("Both records release"), B->MaterialState->GetActiveBondCount(), 0);
	TestEqual(TEXT("Release preserves continuing epoch"), A->Reactive->GetContinuingContactEpoch(B->GetPhysicalBody()), Epoch);
	TestEqual(TEXT("Another paid effect admits at most three slots"), A->MaterialState->ApplyAdhesion(8, .5f), 3);
	TestEqual(TEXT("Active and reserved slots leave no free capacity"), A->MaterialState->ApplyAdhesion(3), 0);
	auto* Rack = SpawnMaterialStateActor<ALSShellSiegeCarriage>(World, FVector(1000, 0, 500));
	auto* Cargo = SpawnMaterialStateActor<ALSShellSiegeCarriage>(World, FVector(1034, 0, 500));
	if (!TestNotNull(TEXT("Rack body"), Rack) || !TestNotNull(TEXT("Cargo body"), Cargo)) return false;
	Rack->Reactive->SetStructuralContact(Cargo->Reactive, true);
	Rack->MaterialState->ApplyAdhesion(3);
	TestEqual(TEXT("Existing rack pair consumes no glue slot"), Rack->MaterialState->GetAvailableBondSlots(), 3);
	TestEqual(TEXT("Existing rack pair creates no second constraint"), Rack->MaterialState->GetActiveBondCount(), 0);
	Rack->MaterialState->ReleaseAllBonds();
	TestTrue(TEXT("Glue cannot overwrite insulation"), Rack->Reactive->IsContactInsulated(Cargo->GetPhysicalBody()));
	TestTrue(TEXT("Glue release cannot release the rack owner"), Rack->Reactive->GetStructuralContactBodies().Contains(Cargo->GetPhysicalBody()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellCarriageTopologyTest,
	"Landship.ShellExperiment.Materials.CarriagePreservesBondedAndCrowdedCapturedShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellCarriageTopologyTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!BeginMaterialStateWorld(TestWorld)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto SpawnCarriageFromCapture = [World](const FLSShellRigidBodyState& State, ALSShellInertMaterial* Original)
	{
		auto* Replacement = World->SpawnActorDeferred<ALSShellSiegeCarriage>(ALSShellSiegeCarriage::StaticClass(), State.Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Replacement) return static_cast<ALSShellSiegeCarriage*>(nullptr);
		if (!Replacement->InitializeBody(State)) { Replacement->Destroy(); return static_cast<ALSShellSiegeCarriage*>(nullptr); }
		// 사망 교체처럼 원본 충돌을 중단한 뒤 새 몸체를 활성화한다. 자세 보정이나 물리 스텝은 넣지 않는다.
		Original->GetPhysicalBody()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Replacement->FinishSpawning(State.Transform);
		return Replacement;
	};
	auto* BondedSource = SpawnMaterialStateActor<ALSShellInertIce>(World, FVector(0, 0, 500));
	auto* Peer = SpawnMaterialStateActor<ALSShellInertIce>(World, FVector(0, 0, 606));
	if (!TestNotNull(TEXT("Captured source body"), BondedSource) || !TestNotNull(TEXT("Actual bond peer"), Peer)) return false;
	BondedSource->Reactive->SetStructuralContact(Peer->Reactive, false);
	BondedSource->Reactive->ReleaseStructuralContact(Peer->Reactive);
	if (!TestEqual(TEXT("One real adhesive slot is accepted"), BondedSource->MaterialState->ApplyAdhesion(1), 1)) return false;
	if (!TestEqual(TEXT("Actual native bond exists before capture"), BondedSource->MaterialState->GetActiveBondCount(), 1)) return false;
	const FLSShellRigidBodyState BondedState = BondedSource->MaterialState->CaptureBodyState();
	TestTrue(TEXT("Capture records surviving bonds and actual collision owner"),
		BondedState.bHasSurvivingBonds && BondedState.OriginalCollisionOwner.Get() == BondedSource);
	auto* BondedCarriage = SpawnCarriageFromCapture(BondedState, BondedSource);
	if (!TestNotNull(TEXT("Bonded replacement body"), BondedCarriage)) return false;
	auto* BondedCapsule = Cast<UCapsuleComponent>(BondedCarriage->GetPhysicalBody());
	if (!TestNotNull(TEXT("Bonded replacement keeps the captured capsule"), BondedCapsule)) return false;
	TestTrue(TEXT("Captured dimensions preserve the original contact region"),
		BondedCapsule->GetUnscaledCapsuleRadius() == BondedState.ShapeExtent.X &&
		BondedCapsule->GetUnscaledCapsuleHalfHeight() == BondedState.ShapeExtent.Z);
	BondedSource->MaterialState->CommitMaterialTransfer(BondedCarriage->MaterialState);
	TestEqual(TEXT("Existing bond transfers once to the same-shape replacement"), BondedCarriage->MaterialState->GetActiveBondCount(), 1);
	TestEqual(TEXT("Peer retains one surviving bond"), Peer->MaterialState->GetActiveBondCount(), 1);
	TestEqual(TEXT("Retired source retains no duplicate bond"), BondedSource->MaterialState->GetActiveBondCount(), 0);
	auto* SurvivingJoint = BondedCarriage->FindComponentByClass<UPhysicsConstraintComponent>();
	TestTrue(TEXT("Transferred bond is an actual valid unbroken native constraint"), SurvivingJoint &&
		SurvivingJoint->ConstraintInstance.IsValidConstraintInstance() && !SurvivingJoint->ConstraintInstance.IsBroken());
	TestTrue(TEXT("Replacement retains captured mass and pose"),
		FMath::IsNearlyEqual(BondedCapsule->GetMass(), BondedState.MassKg, .01f) &&
		BondedCapsule->GetComponentTransform().Equals(BondedState.Transform, .01f));

	auto* CrowdedSource = SpawnMaterialStateActor<ALSShellInertIce>(World, FVector(1000, 0, 500));
	if (!TestNotNull(TEXT("Unbonded source for corner clearance"), CrowdedSource)) return false;
	const FLSShellRigidBodyState CrowdedState = CrowdedSource->MaterialState->CaptureBodyState();
	TestFalse(TEXT("Unbonded capture has no surviving bond flag"), CrowdedState.bHasSurvivingBonds);
	FLSShellRigidBodyState CornerState;
	CornerState.bValid = true;
	CornerState.Shape = ELSShellBodyShape::Sphere;
	CornerState.ShapeExtent = FVector(1);
	CornerState.Transform = FTransform(CrowdedState.Transform.GetLocation() + FVector(14, 14, 0));
	auto* Corner = World->SpawnActorDeferred<ALSShellInertIce>(ALSShellInertIce::StaticClass(), CornerState.Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Physical corner blocker"), Corner)) return false;
	if (!TestTrue(TEXT("Corner blocker has a real sphere shape"), Corner->InitializeBody(CornerState))) return false;
	Corner->FinishSpawning(CornerState.Transform);
	UPrimitiveComponent* CornerBody = Corner->GetPhysicalBody();
	const FVector ProspectiveBoxExtent = CrowdedState.ShapeExtent * GetDefault<ALSShellSiegeCarriage>()->CapturedExtentScale;
	if (!TestFalse(TEXT("Original captured capsule does not overlap the nearby physical body"),
		CornerBody->OverlapComponent(CrowdedState.Transform.GetLocation(), CrowdedState.Transform.GetRotation(),
			CrowdedSource->GetPhysicalBody()->GetCollisionShape()))) return false;
	if (!TestTrue(TEXT("Prospective box corner would newly overlap the physical body"),
		CornerBody->OverlapComponent(CrowdedState.Transform.GetLocation(), CrowdedState.Transform.GetRotation(),
			FCollisionShape::MakeBox(ProspectiveBoxExtent)))) return false;
	auto* CrowdedCarriage = SpawnCarriageFromCapture(CrowdedState, CrowdedSource);
	if (!TestNotNull(TEXT("Crowded replacement succeeds without new overlap"), CrowdedCarriage)) return false;
	TestTrue(TEXT("New box corner overlap keeps the captured capsule instead"), CrowdedCarriage->GetPhysicalBody()->IsA<UCapsuleComponent>());
	TestFalse(TEXT("Replacement creates no overlap with the nearby actual body"),
		CornerBody->OverlapComponent(CrowdedState.Transform.GetLocation(), CrowdedState.Transform.GetRotation(),
			CrowdedCarriage->GetPhysicalBody()->GetCollisionShape()));

	auto* ClearSource = SpawnMaterialStateActor<ALSShellInertIce>(World, FVector(2000, 0, 500));
	if (!TestNotNull(TEXT("Unbonded source with clear surrounding space"), ClearSource)) return false;
	const FLSShellRigidBodyState ClearState = ClearSource->MaterialState->CaptureBodyState();
	auto* ClearCarriage = SpawnCarriageFromCapture(ClearState, ClearSource);
	if (!TestNotNull(TEXT("Clear replacement body"), ClearCarriage)) return false;
	auto* ClearBox = Cast<UBoxComponent>(ClearCarriage->GetPhysicalBody());
	if (!TestNotNull(TEXT("Clear unbonded carriage still uses its settling box shape"), ClearBox)) return false;
	TestTrue(TEXT("Clear box retains the prescribed captured extent scale"),
		ClearBox->GetUnscaledBoxExtent().Equals(ClearState.ShapeExtent * ClearCarriage->CapturedExtentScale, .001f));
	FLSShellRigidBodyState RoundState = ClearState;
	RoundState.Transform.SetLocation(FVector(4000, 0, 500));
	RoundState.Shape = ELSShellBodyShape::Capsule;
	RoundState.ShapeExtent = FVector(140);
	RoundState.bHasSurvivingBonds = true;
	auto* RoundCarriage = World->SpawnActorDeferred<ALSShellSiegeCarriage>(ALSShellSiegeCarriage::StaticClass(), RoundState.Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Round retained carriage"), RoundCarriage) ||
		!TestTrue(TEXT("Round retained shape initializes"), RoundCarriage->InitializeBody(RoundState))) return false;
	RoundCarriage->FinishSpawning(RoundState.Transform);
	const auto* RoundVisual = RoundCarriage->FindComponentByClass<UStaticMeshComponent>();
	TestTrue(TEXT("Retained round Heavy shape uses the matching sphere visual instead of projecting cube corners"),
		RoundVisual && RoundVisual->GetStaticMesh() && RoundVisual->GetStaticMesh()->GetPathName() == TEXT("/Engine/BasicShapes/Sphere.Sphere") &&
		RoundVisual->GetRelativeScale3D().Equals(FVector(2.8f), .001f));
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
