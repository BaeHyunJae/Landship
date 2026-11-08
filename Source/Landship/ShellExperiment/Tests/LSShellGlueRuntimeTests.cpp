#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellExperimentGameMode.h"
#include "ShellExperiment/LSShellMaterialStateComponent.h"
#include "ShellExperiment/LSShellPhysicalProp.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSOutpost.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Tests/AutomationCommon.h"

namespace
{
struct FGlueTestDamagePolicy
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	bool bFriendlyFire = Settings->bAllowFriendlyFire;
	bool bSelfDamage = Settings->bAllowSelfDamage;
	FGlueTestDamagePolicy()
	{
		Settings->bAllowFriendlyFire = true;
		Settings->bAllowSelfDamage = true;
	}
	~FGlueTestDamagePolicy()
	{
		Settings->bAllowFriendlyFire = bFriendlyFire;
		Settings->bAllowSelfDamage = bSelfDamage;
	}
};

template <typename T>
T* SpawnGlueActor(UWorld* World, FVector Location)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Location, FRotator::ZeroRotator, Spawn);
}

ALSShellMetalBall* SpawnGlueBall(UWorld* World, FVector Location)
{
	const FTransform Transform(Location);
	auto* Ball = World->SpawnActorDeferred<ALSShellMetalBall>(ALSShellMetalBall::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Ball) return nullptr;
	Ball->IntrinsicMassKg = 750;
	Ball->Radius = 70;
	Ball->FinishSpawning(Transform);
	return Ball;
}

ALSShellExperimentCoordinator* SetUpGlueWorld(UWorld* World, FAutomationTestBase& Test)
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
	auto* Setup = SpawnGlueActor<ALSBattlefieldSetup>(World, FVector::ZeroVector);
	Setup->PlayerCastle = Castle;
	Setup->CoreOutpost = Core;
	auto* Coordinator = SpawnGlueActor<ALSShellExperimentCoordinator>(World, FVector::ZeroVector);
	Coordinator->PlayerCastle = Castle;
	Coordinator->CoreOutpost = Core;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	Coordinator->SupplyStage = ELSShellSupplyStage::Complete;
	Coordinator->AvailableModules = {ELSShellModule::Blast, ELSShellModule::Thrust, ELSShellModule::Electric,
		ELSShellModule::Gravity, ELSShellModule::Iron, ELSShellModule::Split, ELSShellModule::Adhesion, ELSShellModule::Cooling};
	Coordinator->InitialFirst = ELSShellModule::Thrust;
	Coordinator->InitialSecond = ELSShellModule::Blast;
	Coordinator->SupplyTuning.ThrustMomentum = 660000;
	return Coordinator;
}

void TickGlueWorld(UWorld* World, float DeltaSeconds)
{
	++GFrameCounter;
	World->Tick(LEVELTICK_All, DeltaSeconds);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellNativeAdhesiveGlueRuntimeTest,
	"Landship.ShellExperiment.Material.AdhesiveImpulseBreakMatchesNormalAndFTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellNativeAdhesiveGlueRuntimeTest::RunTest(const FString& Parameters)
{
	FGlueTestDamagePolicy Policy;
	float NormalShockPeaks[2] = {};
	for (const bool bFSlowed : {false, true})
	{
		FTestWorldWrapper TestWorld;
		if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = TestWorld.GetTestWorld();
		auto* Coordinator = SetUpGlueWorld(World, *this);
		if (!Coordinator) return false;
		auto* A = SpawnGlueBall(World, FVector(2000, 0, 500));
		auto* B = SpawnGlueBall(World, FVector(2140, 0, 500));
		if (!TestNotNull(TEXT("First native metal body"), A) || !TestNotNull(TEXT("Second native metal body"), B)) return false;
		for (ALSShellMetalBall* Ball : {A, B})
		{
			Ball->Body->SetEnableGravity(false); // 기술용 fixture 설정: 접촉과 joint 하중만 관찰한다.
			Ball->Body->SetLinearDamping(0);
			Ball->Body->SetAngularDamping(0);
		}
		if (!TestWorld.BeginPlayInTestWorld()) return false;
		const auto* Mode = World->GetAuthGameMode<ALSShellExperimentGameMode>();
		if (!TestNotNull(TEXT("Actual experiment GameMode"), Mode) ||
			!TestTrue(TEXT("Native glue test starts in active Supply mode"), Coordinator->IsBattleActive())) return false;
		if (!TestTrue(TEXT("Both native spheres start simulating"), A->Body->IsSimulatingPhysics() && B->Body->IsSimulatingPhysics())) return false;

		const FVector PairNormal = (B->Body->GetComponentLocation() - A->Body->GetComponentLocation()).GetSafeNormal();
		A->Body->AddImpulse(PairNormal * 15000); // 실제 첫 Hit를 만드는 기술용 닫힘 충격이다.
		uint64 ContactEpoch = 0;
		for (int32 Step = 0; Step < 24 && ContactEpoch == 0; ++Step)
		{
			TickGlueWorld(World, 1.f / 120.f);
			ContactEpoch = A->Reactive->GetContinuingContactEpoch(B->Body);
		}
		if (!TestTrue(TEXT("Touching native spheres generated a continuing hit epoch"), ContactEpoch != 0)) return false;
		if (!TestEqual(TEXT("One adhesion slot is accepted"), A->MaterialState->ApplyAdhesion(1), 1)) return false;
		if (!TestEqual(TEXT("One real joint is retained"), A->MaterialState->GetActiveBondCount(), 1) ||
			!TestEqual(TEXT("Peer observes the same reciprocal joint"), B->MaterialState->GetActiveBondCount(), 1) ||
			!TestEqual(TEXT("A consumed slot is not available"), A->MaterialState->GetAvailableBondSlots(), 0)) return false;

		TArray<UPhysicsConstraintComponent*> Constraints;
		A->GetComponents(Constraints);
		if (!TestEqual(TEXT("Adhesion created one native constraint component"), Constraints.Num(), 1)) return false;
		TWeakObjectPtr<UPhysicsConstraintComponent> Bond = Constraints[0];
		if (!TestTrue(TEXT("Native constraint instance is valid and intact"),
			Bond.IsValid() && Bond->ConstraintInstance.IsValidConstraintInstance() && !Bond->ConstraintInstance.IsBroken())) return false;
		UPrimitiveComponent *ConstraintBodyA = nullptr, *ConstraintBodyB = nullptr;
		FName BoneA, BoneB;
		Bond->GetConstrainedComponents(ConstraintBodyA, BoneA, ConstraintBodyB, BoneB);
		if (!TestTrue(TEXT("Constraint binds the two actual metal bodies"),
			((ConstraintBodyA == A->Body && ConstraintBodyB == B->Body) ||
			 (ConstraintBodyA == B->Body && ConstraintBodyB == A->Body)))) return false;
		const FVector AStart = A->Body->GetComponentLocation();
		const FVector BStart = B->Body->GetComponentLocation();

		// 같은 외부120Hz에서 실제 F 조합 경로의 게임 시간 배율만 비교한다.
		if (bFSlowed) Coordinator->SetComposing(true);
		FLSShellShot Shot;
		Coordinator->CaptureShotContext(Shot);
		Shot.First = ELSShellModule::Thrust;
		Shot.Second = ELSShellModule::Blast;
		Shot.Direction = PairNormal;
		Shot.SupplyTuning.ThrustMomentum = 660000;
		Shot.Source = Coordinator->PlayerCastle;
		FHitResult Hit(A, A->Body, AStart, -PairNormal);
		Coordinator->ExecuteModule(ELSShellModule::Thrust, AStart, Shot, Hit);
		for (int32 Step = 0; Step < 120; ++Step) TickGlueWorld(World, 1.f / 120.f);
		const float ExpectedGameStep = (bFSlowed ? .2f : 1.f) / 120.f;
		TestTrue(TEXT("Instrument observes normal or actual F-scaled game step"), FMath::IsNearlyEqual(World->GetDeltaSeconds(), ExpectedGameStep, .00001f));
		NormalShockPeaks[bFSlowed ? 1 : 0] = A->MaterialState->GetPeakBondImpulse();
		TestTrue(TEXT("Actual solver records nonzero below-budget Thrust impulse"),
			NormalShockPeaks[bFSlowed ? 1 : 0] > 100000 && NormalShockPeaks[bFSlowed ? 1 : 0] < 450000);
		const FVector AThrustDelta = A->Body->GetComponentLocation() - AStart;
		const FVector BThrustDelta = B->Body->GetComponentLocation() - BStart;
		TestTrue(TEXT("Thrust moves the constrained bodies as one physical pair"),
			FVector::DotProduct(AThrustDelta, PairNormal) > 10 && FVector::DotProduct(BThrustDelta, PairNormal) > 10);
		if (!TestTrue(TEXT("Native joint remains valid after normal capped Thrust"),
			Bond.IsValid() && Bond->ConstraintInstance.IsValidConstraintInstance() && !Bond->ConstraintInstance.IsBroken() &&
			A->MaterialState->GetActiveBondCount() == 1 && B->MaterialState->GetActiveBondCount() == 1)) return false;

		// 모듈 상한을 우회하는 기술용 과부하다. 일반 플레이에서 이 충격이 생긴다는 주장이 아니다.
		A->Body->AddImpulse(-PairNormal * (3.f * Shot.SupplyTuning.ThrustMomentum));
		int32 ActiveToInactiveTransitions = 0;
		bool bWasActive = true;
		for (int32 Step = 0; Step < 120 && bWasActive; ++Step)
		{
			TickGlueWorld(World, 1.f / 120.f);
			const bool bActive = A->MaterialState->GetActiveBondCount() > 0 || B->MaterialState->GetActiveBondCount() > 0;
			if (bWasActive && !bActive)
			{
				++ActiveToInactiveTransitions;
				bWasActive = false;
			}
		}
		if (!TestTrue(TEXT("PostPhysics impulse overload latches exactly one physical joint break"), ActiveToInactiveTransitions == 1)) return false;
		TestTrue(TEXT("Actual solver overload exceeds fixed450000impulse budget"), A->MaterialState->GetPeakBondImpulse() > 450000);
		AddInfo(FString::Printf(TEXT("Adhesion impulse comparison FSlowed=%d gameStep=%.8f normalPeak=%.3f overloadPeak=%.3f breakTransitions=%d"),
			bFSlowed, World->GetDeltaSeconds(), NormalShockPeaks[bFSlowed ? 1 : 0], A->MaterialState->GetPeakBondImpulse(), ActiveToInactiveTransitions));
		TestEqual(TEXT("Broken bond is removed from both material states"), A->MaterialState->GetActiveBondCount() + B->MaterialState->GetActiveBondCount(), 0);
		TestEqual(TEXT("Broken adhesion slot is not refilled"), A->MaterialState->GetAvailableBondSlots(), 0);
		TestEqual(TEXT("Peer has no refilled adhesion slot"), B->MaterialState->GetAvailableBondSlots(), 0);
		// PostPhysics 파단은 이미 공유된 충격을 되돌리지 않는다. 작은 추가 외력으로 실제 구속 해제를 확인한다.
		A->Body->AddImpulse(-PairNormal * 15000);
		for (int32 Step = 0; Step < 30; ++Step) TickGlueWorld(World, 1.f / 120.f);
		TestTrue(TEXT("Broken physical joint permits independent actual separation"),
			FVector::Dist(A->Body->GetComponentLocation(), B->Body->GetComponentLocation()) > 140);
		TestWorld.ForwardErrorMessages(this);
		Coordinator->SetComposing(false);
	}
	AddInfo(FString::Printf(TEXT("Same660000shock normalPeak=%.3f FPeak=%.3f comparedCases=2"),
		NormalShockPeaks[0], NormalShockPeaks[1]));
	return true;
}

#endif
