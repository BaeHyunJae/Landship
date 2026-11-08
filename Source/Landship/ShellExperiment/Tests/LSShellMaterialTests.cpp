#if WITH_DEV_AUTOMATION_TESTS

#include "ShellExperiment/LSShellField.h"
#include "ShellExperiment/LSShellGeneratedMaterial.h"
#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellExperimentGameMode.h"
#include "ShellExperiment/LSShellPhysicalProp.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "ShellExperiment/LSShellProjectile.h"
#include "ShellExperiment/LSShellTransport.h"
#include "ShellExperiment/LSShellEffectInstance.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSOutpost.h"
#include "Engine/World.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

namespace
{
struct FLSShellMaterialTestPolicy
{
	ULSCombatDeveloperSettings* Settings = GetMutableDefault<ULSCombatDeveloperSettings>();
	bool bFriendly = Settings->bAllowFriendlyFire, bSelf = Settings->bAllowSelfDamage;
	FLSShellMaterialTestPolicy() { Settings->bAllowFriendlyFire = Settings->bAllowSelfDamage = true; }
	~FLSShellMaterialTestPolicy() { Settings->bAllowFriendlyFire = bFriendly; Settings->bAllowSelfDamage = bSelf; }
};

template <typename T>
T* SpawnMaterialTestActor(UWorld* World, FVector Point)
{
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Point, FRotator::ZeroRotator, Spawn);
}

void TickMaterialTestWorld(UWorld* World) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }

ALSShellExperimentCoordinator* SetUpMaterialTestWorld(UWorld* World, FAutomationTestBase& Test)
{
	World->GetWorldSettings()->DefaultGameMode = ALSShellExperimentGameMode::StaticClass();
	auto* Castle = LSCastleTestFixture::Spawn(World, FVector(-5000, 0, 200));
	if (!Test.TestNotNull(TEXT("Production castle"), Castle)) return nullptr;
	Castle->GetMovementComponent()->Deactivate();
	auto* CoreClass = LoadClass<ALSOutpost>(nullptr, TEXT("/Game/Landship/ShellExperiment/Blueprints/BP_ShellCore.BP_ShellCore_C"));
	if (!Test.TestNotNull(TEXT("Production core class"), CoreClass)) return nullptr;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Core = World->SpawnActor<ALSOutpost>(CoreClass, FVector(0, 0, 200), FRotator::ZeroRotator, Spawn);
	auto* Setup = SpawnMaterialTestActor<ALSBattlefieldSetup>(World, FVector::ZeroVector);
	Setup->PlayerCastle = Castle; Setup->CoreOutpost = Core;
	auto* Coordinator = SpawnMaterialTestActor<ALSShellExperimentCoordinator>(World, FVector::ZeroVector);
	Coordinator->PlayerCastle = Castle; Coordinator->CoreOutpost = Core;
	Coordinator->RuleProfile = ELSShellRuleProfile::SupplyStageOne;
	Coordinator->SupplyStage = ELSShellSupplyStage::Materials;
	Coordinator->AvailableModules = {ELSShellModule::Blast, ELSShellModule::Thrust, ELSShellModule::Electric, ELSShellModule::Gravity, ELSShellModule::Iron};
	Coordinator->InitialFirst = ELSShellModule::Iron; Coordinator->InitialSecond = ELSShellModule::Electric;
	return Coordinator;
}

bool BeginMaterialTestWorld(FTestWorldWrapper& TestWorld, ALSShellExperimentCoordinator* Coordinator, FAutomationTestBase& Test)
{
	return Test.TestTrue(TEXT("Actual GameMode begins material fixture"), TestWorld.BeginPlayInTestWorld()) &&
		Test.TestTrue(TEXT("Material coordinator active"), Coordinator->IsBattleActive());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyGravityBudgetTest,
	"Landship.ShellExperiment.Materials.GravityIntegratedCapDiscardsWithoutRedistribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyGravityBudgetTest::RunTest(const FString& Parameters)
{
	FLSShellMaterialTestPolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpMaterialTestWorld(World, *this);
	if (!Coordinator) return false;
	const FVector Center(-2000, 1200, 800);
	auto* Light = SpawnMaterialTestActor<ALSShellMetalBall>(World, Center + FVector(300, 0, 0));
	auto* Heavy = SpawnMaterialTestActor<ALSShellMetalBall>(World, Center - FVector(300, 0, 0));
	Light->IntrinsicMassKg = 60; Heavy->IntrinsicMassKg = 900;
	Light->LinearDamping = Heavy->LinearDamping = 0;
	Light->Body->SetEnableGravity(false); Heavy->Body->SetEnableGravity(false);
	auto* Shielded = SpawnMaterialTestActor<ALSShellMetalBall>(World, Center + FVector(0, 300, 0));
	Shielded->Body->SetEnableGravity(false);
	auto* Cover = SpawnMaterialTestActor<AActor>(World, FVector::ZeroVector);
	auto* CoverBody = NewObject<UBoxComponent>(Cover);
	Cover->SetRootComponent(CoverBody); Cover->AddInstanceComponent(CoverBody);
	CoverBody->SetCanEverAffectNavigation(false);
	CoverBody->SetBoxExtent(FVector(100, 20, 100));
	CoverBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CoverBody->SetCollisionResponseToAllChannels(ECR_Block);
	CoverBody->SetWorldLocation(Center + FVector(0, 150, 0));
	CoverBody->RegisterComponent();
	if (!BeginMaterialTestWorld(TestWorld, Coordinator, *this)) return false;
	FHitResult CoverHit;
	FCollisionQueryParams CoverQuery(SCENE_QUERY_STAT(ShellGravityCoverFixture), false, Shielded);
	if (!TestTrue(TEXT("Gravity exposure instrument first proves actual blocking segment"), World->LineTraceSingleByChannel(
		CoverHit, Center, Shielded->GetActorLocation() - FVector(0, 70, 0), LSCollisionChannels::ExplosionOcclusion, CoverQuery) &&
		CoverHit.GetComponent() == CoverBody)) return false;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	TWeakObjectPtr<ALSShellGravityField> Field = SpawnMaterialTestActor<ALSShellGravityField>(World, Center);
	Field->InitializeSupply(Coordinator, Shot);
	for (int32 Step = 0; Step < 210; ++Step)
	{
		// 위치만 반복 고정해 동일 거리의 공유 충격과 누적 속도를 분리한다. 실제 궤도 검사가 아니다.
		Light->SetActorLocation(Center + FVector(300, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		Heavy->SetActorLocation(Center - FVector(300, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		TickMaterialTestWorld(World);
	}
	const FVector LightVelocity = Light->Body->GetPhysicsLinearVelocity(), HeavyVelocity = Heavy->Body->GetPhysicsLinearVelocity();
	AddInfo(FString::Printf(TEXT("Integrated gravity lightVelocity=%s heavyVelocity=%s sharedImpulse=%.3f"),
		*LightVelocity.ToCompactString(), *HeavyVelocity.ToCompactString(), LightVelocity.Size() * 60 + HeavyVelocity.Size() * 900));
	TestTrue(TEXT("Light body receives at most1500integrated delta speed across whole field"), LightVelocity.Equals(FVector(-1500, 0, 0), 1));
	TestTrue(TEXT("Heavy body retains its original half budget rather than receiving discarded excess"),
		HeavyVelocity.Equals(FVector(260000.f / 900.f, 0, 0), 1));
	TestTrue(TEXT("Discarded capped impulse cannot grow total delivered budget"), LightVelocity.Size() * 60 + HeavyVelocity.Size() * 900 < 360000);
	TestTrue(TEXT("Gravity records external force on both actual bodies"), Light->Reactive->GetLastExternalForceFrame() > 0 && Heavy->Reactive->GetLastExternalForceFrame() > 0);
	TestTrue(TEXT("Real cover prevents Gravity pull and budget dilution"), Shielded->Body->GetPhysicsLinearVelocity().IsNearlyZero(.01));
	TestFalse(TEXT("Finite fixed-position field ends after two seconds"), Field.IsValid());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellSupplyGravityWreckTest,
	"Landship.ShellExperiment.Materials.GravityPullsPersistentWreck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellSupplyGravityWreckTest::RunTest(const FString& Parameters)
{
	FLSShellMaterialTestPolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpMaterialTestWorld(World, *this);
	if (!Coordinator) return false;
	auto* Floor = SpawnMaterialTestActor<AActor>(World, FVector::ZeroVector);
	auto* FloorBody = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(FloorBody); Floor->AddInstanceComponent(FloorBody);
	FloorBody->SetCanEverAffectNavigation(false);
	FloorBody->SetBoxExtent(FVector(1000, 1000, 25));
	FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FloorBody->SetCollisionObjectType(LSCollisionChannels::Ground);
	FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
	FloorBody->SetWorldLocation(FVector(-2000, 1000, 705));
	FloorBody->RegisterComponent();
	auto* Wreck = SpawnMaterialTestActor<ALSShellTransport>(World, FVector(-2000, 1000, 800));
	// 화물·구동·중력을 분리한 기술용 빈 차체다. 공통 사망으로 남은 같은 몸체를 검사한다.
	Wreck->CargoSlots.Reset(); Wreck->bAutoStartRoute = false;
	Wreck->Body->SetEnableGravity(false);
	if (!BeginMaterialTestWorld(TestWorld, Coordinator, *this)) return false;
	Wreck->GetHealthComponent()->ApplyDamage(ULSHealthComponent::MakeDamageRequest(1000, Coordinator->PlayerCastle, Coordinator));
	FLSHealthSnapshot Health;
	Wreck->GetHealthComponent()->TryGetHealth(Health);
	if (!TestTrue(TEXT("Common death leaves actual zero-health physical wreck"), Health.CurrentHealth == 0 && Wreck->Reactive->IsPhysicalBodyPresent())) return false;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	const FVector Center = Wreck->GetActorLocation() + FVector(300, 0, 0);
	auto* Field = SpawnMaterialTestActor<ALSShellGravityField>(World, Center);
	Field->InitializeSupply(Coordinator, Shot);
	for (int32 Step = 0; Step < 10; ++Step) TickMaterialTestWorld(World);
	TestTrue(TEXT("Gravity reaches wreck through physical presence rather than living-health filter"), Wreck->Body->GetPhysicsLinearVelocity().X > 10);
	TestTrue(TEXT("Wreck pull notifies external force"), Wreck->Reactive->GetLastExternalForceFrame() > 0);
	TestTrue(TEXT("Gravity center stays fixed"), Field->GetActorLocation().Equals(Center, .01));
	TestFalse(TEXT("Gravity cannot restart wreck functionality"), Wreck->IsDriving());
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellGeneratedCapacityTest,
	"Landship.ShellExperiment.Materials.GeneratedIronChecksExecutionCapacityAndRecall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellGeneratedCapacityTest::RunTest(const FString& Parameters)
{
	FLSShellMaterialTestPolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpMaterialTestWorld(World, *this);
	if (!Coordinator) return false;
	TArray<ALSShellMetalBall*> Targets;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		auto* Target = SpawnMaterialTestActor<ALSShellMetalBall>(World, FVector(-1800, Index * 350, 800));
		Target->Body->SetEnableGravity(false); Targets.Add(Target);
	}
	if (!BeginMaterialTestWorld(TestWorld, Coordinator, *this)) return false;
	FLSShellGeneratedMaterialRegistry Registry;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.Direction = FVector::ForwardVector;
	TArray<ALSShellMetalBall*> Generated;
	FString Error;
	Shot.Origin = Targets[0]->GetActorLocation() - FVector(700, 0, 0);
	FHitResult OriginalHit;
	if (!TestTrue(TEXT("Placement instrument first reaches unoccluded target"), ALSShellProjectile::SweepFlight(World,
		Shot.Origin, Targets[0]->GetActorLocation(), Shot.Source.Get(), OriginalHit) && OriginalHit.GetActor() == Targets[0])) return false;
	// 이미 캡처한 착탄점 앞에 실제 장애물을 추가해 배치 실패가 용량을 예약하지 않는지 검사한다.
	auto* Blocker = SpawnMaterialTestActor<ALSShellMetalBall>(World,
		OriginalHit.ImpactPoint + OriginalHit.ImpactNormal * 130);
	Blocker->Body->SetEnableGravity(false);
	TestNull(TEXT("Real sphere sweep prevents generation behind later cover"), Registry.GenerateIron(World, OriginalHit, Shot, Error));
	TestEqual(TEXT("Failed geometry cannot reserve material mass"), Registry.GetLiveMassKg(), 0.f);
	TestEqual(TEXT("Failed geometry cannot reserve body count"), Registry.GetLiveBodyCount(), 0);
	Blocker->Destroy();
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Shot.Origin = Targets[Index]->GetActorLocation() - FVector(700, 0, 0);
		FHitResult Hit;
		if (!TestTrue(TEXT("Actual first blocker defines Iron placement"), ALSShellProjectile::SweepFlight(
			World, Shot.Origin, Targets[Index]->GetActorLocation(), Shot.Source.Get(), Hit) && Hit.GetActor() == Targets[Index])) return false;
		auto* Ball = Registry.GenerateIron(World, Hit, Shot, Error);
		if (Index == 4)
		{
			TestNull(TEXT("Fifth750kgbody is denied at execution"), Ball);
			TestTrue(TEXT("Capacity denial is explicit"), !Error.IsEmpty());
			break;
		}
		if (!TestNotNull(TEXT("Generated Iron uses actual native sphere"), Ball)) return false;
		Generated.Add(Ball);
		TestTrue(TEXT("Generated template has750kgand120cmnear-side surface gap"),
			FMath::IsNearlyEqual(Ball->Body->GetMass(), 750.f, .1f) && FMath::IsNearlyEqual(
			FVector::Distance(Ball->GetActorLocation(), Hit.ImpactPoint) - Ball->Radius, 120., .1));
	}
	TestEqual(TEXT("Only four actual generated bodies use3000kg"), Registry.GetLiveBodyCount(), 4);
	TestEqual(TEXT("Live material mass is finite"), Registry.GetLiveMassKg(), 3000.f);
	TickMaterialTestWorld(World);
	TestTrue(TEXT("Budgeted Iron launch reaches actual solver along original shot direction"),
		FMath::IsNearlyEqual(Generated[0]->Body->GetPhysicsLinearVelocity().X, 800., 1.));
	TestFalse(TEXT("Map body is never recallable"), Registry.Recall(Targets[0]));
	TestEqual(TEXT("Generated body accepts charge before recall"), Coordinator->ApplyCharge(Generated[0], Shot, 180), 180.f);
	TWeakObjectPtr<ALSShellMetalBall> Recalled = Generated[0];
	TestTrue(TEXT("Aimed generated-body recall is accepted"), Registry.Recall(Generated[0]));
	TestFalse(TEXT("Recall removes actual generated body"), Recalled.IsValid());
	TestEqual(TEXT("Actual removal immediately frees generated mass"), Registry.GetLiveMassKg(), 2250.f);
	TestEqual(TEXT("Recall causes no environmental detonation"), Coordinator->DetonationCount, 0);
	Shot.Origin = Targets[4]->GetActorLocation() - FVector(700, 0, 0);
	FHitResult Hit;
	if (!TestTrue(TEXT("Retry still uses actual target sweep"), ALSShellProjectile::SweepFlight(World, Shot.Origin,
		Targets[4]->GetActorLocation(), Shot.Source.Get(), Hit) && Hit.GetActor() == Targets[4])) return false;
	TestNotNull(TEXT("Unreserved failed generation can succeed after actual recall"), Registry.GenerateIron(World, Hit, Shot, Error));
	TestEqual(TEXT("Recall and retry restore exactly3000kgwithout inflation"), Registry.GetLiveMassKg(), 3000.f);
	for (int32 Index = 1; Index < Generated.Num(); ++Index) Registry.Recall(Generated[Index]);
	// 개수 한도 분기만 고립시키는 기술용 낮은 한도다. 기본 한도12를 바꾸지 않는다.
	Shot.SupplyTuning.GeneratedBodyCap = 1;
	Shot.Origin = Targets[1]->GetActorLocation() - FVector(700, 0, 0);
	if (!TestTrue(TEXT("Count-limit probe reaches cleared target lane"), ALSShellProjectile::SweepFlight(World,
		Shot.Origin, Targets[1]->GetActorLocation(), Shot.Source.Get(), Hit) && Hit.GetActor() == Targets[1])) return false;
	TestNull(TEXT("Execution body-count limit rejects even while mass has spare capacity"), Registry.GenerateIron(World, Hit, Shot, Error));
	TestEqual(TEXT("Body-count rejection keeps sole surviving750kgbody"), Registry.GetLiveMassKg(), 750.f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellGeneratedGroundScaleTest,
	"Landship.ShellExperiment.Materials.GeneratedIronFitsGroundHitAndScalesChildMass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellGeneratedGroundScaleTest::RunTest(const FString& Parameters)
{
	FLSShellMaterialTestPolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpMaterialTestWorld(World, *this);
	if (!Coordinator) return false;
	auto* Floor = SpawnMaterialTestActor<AActor>(World, FVector::ZeroVector);
	auto* FloorBody = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(FloorBody); Floor->AddInstanceComponent(FloorBody);
	FloorBody->SetCanEverAffectNavigation(false);
	FloorBody->SetBoxExtent(FVector(3000, 1000, 25));
	FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FloorBody->SetCollisionObjectType(LSCollisionChannels::Ground);
	FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
	FloorBody->SetWorldLocation(FVector(-1800, 0, -25));
	FloorBody->RegisterComponent();
	auto* Barrel = SpawnMaterialTestActor<ALSShellBarrel>(World, FVector(-850, 0, 60));
	const FVector Origin(-2500, 0, 300);
	const FVector Direction = (Barrel->GetActorLocation() - Origin).GetSafeNormal();
	// 작은 포탄은 통과하되 반지름70의 전 구간 sweep은 막히는 이전 경로 주변의 실제 몸체다.
	auto* NearFlight = SpawnMaterialTestActor<ALSShellMetalBall>(World, Origin + Direction * 300 + FVector(0, 100, 0));
	NearFlight->Body->SetEnableGravity(false);
	if (!BeginMaterialTestWorld(TestWorld, Coordinator, *this)) return false;
	FHitResult Hit;
	if (!TestTrue(TEXT("Actual low shell hit reaches grounded barrel past nearby body"), ALSShellProjectile::SweepFlight(
		World, Origin, Barrel->GetActorLocation(), Coordinator->PlayerCastle, Hit) && Hit.GetActor() == Barrel)) return false;
	if (!TestTrue(TEXT("Grounded barrel hit really lies below full Iron radius"), Hit.ImpactPoint.Z < 70)) return false;
	FLSShellGeneratedMaterialRegistry Registry;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.Origin = Origin; Shot.Direction = Direction;
	FString Error;
	for (const float Scale : {1.f, 1.f / 3.f})
	{
		Shot.EffectScale = Scale;
		auto* Ball = Registry.GenerateIron(World, Hit, Shot, Error);
		if (!TestNotNull(TEXT("Near-side Iron fits actual grounded barrel hit"), Ball)) return false;
		const float ExpectedRadius = 70 * FMath::Pow(Scale, 1.f / 3);
		const double Lift = Ball->GetActorLocation().Z - Hit.ImpactPoint.Z;
		TestTrue(TEXT("Allocated Iron mass and volume scale once"),
			FMath::IsNearlyEqual(Ball->Body->GetMass(), 750.f * Scale, .1f) && FMath::IsNearlyEqual(Ball->Radius, ExpectedRadius, .01f));
		TestTrue(TEXT("Ground resolution only lifts by at most one actual radius"), Lift >= 0 && Lift <= Ball->Radius);
		TestTrue(TEXT("Final near-side sphere stays above actual floor"), Ball->GetActorLocation().Z >= Ball->Radius - UE_KINDA_SMALL_NUMBER);
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ShellGeneratedGroundFixture), false, Ball);
		TestFalse(TEXT("Final actual sphere overlaps neither floor nor cover"), World->OverlapBlockingTestByChannel(
			Ball->GetActorLocation(), FQuat::Identity, LSCollisionChannels::ObstacleProjectile,
			FCollisionShape::MakeSphere(Ball->Radius), Query));
		TestTrue(TEXT("Launch momentum scales with mass and retains original direction"),
			Ball->Reactive->GetMomentumAdjustedLinearVelocity().Equals(Direction * 800, .1));
		AddInfo(FString::Printf(TEXT("Ground Iron scale=%.6f hit=%s body=%s radius=%.3f mass=%.3f lift=%.3f"),
			Scale, *Hit.ImpactPoint.ToCompactString(), *Ball->GetActorLocation().ToCompactString(),
			Ball->Radius, Ball->Body->GetMass(), Lift));
		Registry.Recall(Ball);
	}
	TestEqual(TEXT("Both recalled scaled cases release their exact mass"), Registry.GetLiveMassKg(), 0.f);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellGeneratedSplitTransactionTest,
	"Landship.ShellExperiment.Materials.GeneratedSplitIsAtomicAndConservesBudgets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellGeneratedSplitTransactionTest::RunTest(const FString& Parameters)
{
	FLSShellMaterialTestPolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpMaterialTestWorld(World, *this);
	if (!Coordinator) return false;
	auto* Target = SpawnMaterialTestActor<ALSShellMetalBall>(World, FVector(-1200, 0, 800));
	Target->Body->SetEnableGravity(false);
	if (!BeginMaterialTestWorld(TestWorld, Coordinator, *this)) return false;
	FLSShellGeneratedMaterialRegistry Registry;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.Origin = FVector(-2500, 0, 800); Shot.Direction = FVector::ForwardVector;
	FHitResult Hit;
	if (!TestTrue(TEXT("Split parent starts from genuine Iron hit geometry"), ALSShellProjectile::SweepFlight(
		World, Shot.Origin, Target->GetActorLocation(), Shot.Source.Get(), Hit) && Hit.GetActor() == Target)) return false;
	FString Error;
	auto* Parent = Registry.GenerateIron(World, Hit, Shot, Error);
	if (!TestNotNull(TEXT("Actual generated parent"), Parent)) return false;
	Parent->Body->SetEnableGravity(false);
	TickMaterialTestWorld(World);
	// 임의 자세·속도·회전은 분할 보존량을 고립시키는 기술용 초기 상태다.
	Parent->Body->SetPhysicsLinearVelocity(FVector(120, 40, 10));
	Parent->Body->SetPhysicsAngularVelocityInRadians(FVector(.2, .3, .4));
	Parent->GetHealthComponent()->ApplyDamage(ULSHealthComponent::MakeDamageRequest(120, Coordinator->PlayerCastle, Coordinator));
	TestEqual(TEXT("Parent stores one finite charge budget"), Coordinator->ApplyCharge(Parent, Shot, 180), 180.f);
	const FVector Center = Parent->Body->GetCenterOfMass();
	const FVector Momentum = Parent->Body->GetPhysicsLinearVelocity() * Parent->Body->GetMass();
	const FVector AngularMomentum = Parent->Body->ScaleByMomentOfInertia(Parent->Body->GetPhysicsAngularVelocityInRadians());
	const FLSShellChargePacket Packet = Parent->Reactive->GetChargeStore().GetPackets()[0];
	TArray<ALSShellMetalBall*> Children;
	bool bCommitted = false;
	FLSShellGeneratedMaterialRegistry::FCommitReplacement Commit = [&](AActor*, const TArray<ALSShellMetalBall*>& Prepared)
	{
		bCommitted = true;
		// 레지스트리의 상태 이전 훅에 넣는 시험용 분배다. 원본 계보와 합180을 보존한다.
		for (int32 Index = 0; Index < Prepared.Num(); ++Index)
		{
			FLSShellChargePacket ChildPacket = Packet;
			ChildPacket.ParentPacketId = Packet.PacketId;
			ChildPacket.PacketId = Packet.PacketId + 1000 + Index;
			ChildPacket.Amount = Packet.Amount / 3;
			Prepared[Index]->Reactive->AddCharge(ChildPacket, 360);
		}
	};
	TArray<FTransform> Invalid {FTransform(Center), FTransform(Center), FTransform(Center)};
	TestFalse(TEXT("Overlapping three-body result fails atomically"), Registry.TryReplaceThree(Parent, Invalid, {}, Commit, Children, Error));
	TestTrue(TEXT("Rejected split keeps original physical parent and ledger"), IsValid(Parent) && Parent->Reactive->GetStoredCharge() == 180 && !bCommitted);
	TestEqual(TEXT("Failed transaction does not add body count"), Registry.GetLiveBodyCount(), 1);
	const float R = 65;
	TArray<FTransform> Poses {FTransform(Center + FVector(R, 0, 0)),
		FTransform(Center + FVector(-R / 2, R * FMath::Sqrt(3.f) / 2, 0)),
		FTransform(Center + FVector(-R / 2, -R * FMath::Sqrt(3.f) / 2, 0))};
	TWeakObjectPtr<ALSShellMetalBall> OldParent = Parent;
	if (!TestTrue(TEXT("Clear symmetric replacement commits actual three-body result"),
		Registry.TryReplaceThree(Parent, Poses, {}, Commit, Children, Error))) return false;
	TestTrue(TEXT("Parent retires only after successful state commit"), bCommitted && !OldParent.IsValid());
	TestEqual(TEXT("Three actual children replace one parent"), Children.Num(), 3);
	FVector ChildMomentum = FVector::ZeroVector, ChildAngularMomentum = FVector::ZeroVector;
	float Mass = 0, Charge = 0, ChildHealth = 0;
	for (auto* Child : Children)
	{
		Mass += Child->Body->GetMass(); Charge += Child->Reactive->GetStoredCharge();
		FLSHealthSnapshot Health;
		Child->GetHealthComponent()->TryGetHealth(Health); ChildHealth += Health.CurrentHealth;
		const FVector P = Child->Body->GetPhysicsLinearVelocity() * Child->Body->GetMass();
		ChildMomentum += P;
		ChildAngularMomentum += Child->Body->ScaleByMomentOfInertia(Child->Body->GetPhysicsAngularVelocityInRadians()) +
			FVector::CrossProduct(Child->Body->GetCenterOfMass() - Center, P);
		const FLSShellShot* Origin = Registry.GetOrigin(Child);
		TestTrue(TEXT("Child preserves immutable generated resource origin"), Origin && Origin->SourceId == Shot.SourceId && Origin->ShotId == Shot.ShotId);
	}
	TestTrue(TEXT("Replacement conserves actual total mass"), FMath::IsNearlyEqual(Mass, 750.f, .1f));
	TestTrue(TEXT("Replacement conserves total linear momentum"), ChildMomentum.Equals(Momentum, 5));
	TestTrue(TEXT("Replacement conserves orbital and spin angular momentum"), ChildAngularMomentum.Equals(AngularMomentum, 100));
	TestEqual(TEXT("State hook preserves rather than triples parent charge"), Charge, 180.f);
	TestEqual(TEXT("Replacement divides remaining480HP rather than refilling each child600HP"), ChildHealth, 480.f);
	TestEqual(TEXT("Registry counts children once"), Registry.GetLiveBodyCount(), 3);
	TestWorld.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSShellFailedIronFollowupTest,
	"Landship.ShellExperiment.Materials.IronFailureCancelsFollowupAndRotatedSurfaceFits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSShellFailedIronFollowupTest::RunTest(const FString& Parameters)
{
	FLSShellMaterialTestPolicy Policy;
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = TestWorld.GetTestWorld();
	auto* Coordinator = SetUpMaterialTestWorld(World, *this);
	if (!Coordinator) return false;
	auto* Target = SpawnMaterialTestActor<ALSShellMetalBall>(World, FVector(-1800, 1000, 800));
	Target->Body->SetEnableGravity(false);
	if (!BeginMaterialTestWorld(TestWorld, Coordinator, *this)) return false;
	FLSShellShot Shot;
	Coordinator->CaptureShotContext(Shot);
	Shot.First = ELSShellModule::Iron;
	Shot.Origin = FVector(-3000, 1000, 800); Shot.Direction = FVector::ForwardVector;
	FHitResult Hit;
	if (!TestTrue(TEXT("Followup fixture first reaches its actual target"), ALSShellProjectile::SweepFlight(
		World, Shot.Origin, Target->GetActorLocation(), Shot.Source.Get(), Hit) && Hit.GetActor() == Target)) return false;
	Shot.SupplyTuning.GeneratedMassCapKg = 1;
	for (ELSShellModule Second : {ELSShellModule::Thrust, ELSShellModule::Electric, ELSShellModule::Blast})
	{
		Shot.Second = Second;
		auto* Effect = SpawnMaterialTestActor<ALSShellEffectInstance>(World, Hit.ImpactPoint);
		Effect->Initialize(Coordinator, Shot, Hit, true);
		TestTrue(TEXT("Failed generation retires the effect without substitute body or point followup"), Effect->IsActorBeingDestroyed() && !Effect->IsPending());
	}
	TickMaterialTestWorld(World);
	TestEqual(TEXT("Failed Iron does not charge original target"), Target->Reactive->GetStoredCharge(), 0.f);
	TestTrue(TEXT("Failed Iron does not propel original target"), Target->Body->GetPhysicsLinearVelocity().IsNearlyZero());
	TestEqual(TEXT("Only three failed first modules execute"), Coordinator->ModuleCount, 3);
	// 실제 몸체의 뒤쪽으로 회전한 저장 접점이다. 생성 공간과 충돌은 여전히 실제 형상으로 검사한다.
	Target->SetActorRotation(FRotator(0, 180, 0));
	Hit.ImpactPoint = Hit.Location = Target->GetActorLocation() + FVector(70, 0, 0);
	Hit.ImpactNormal = FVector::ForwardVector;
	Shot.SupplyTuning.GeneratedMassCapKg = 3000;
	FLSShellGeneratedMaterialRegistry Registry;
	FString Error;
	auto* Generated = Registry.GenerateIron(World, Hit, Shot, Error);
	if (TestNotNull(TEXT("Rotated actual surface with free outward space permits Iron"), Generated))
	{
		TestTrue(TEXT("Generated body stays outside the actual rotated contact surface"), Generated->GetActorLocation().X > Hit.ImpactPoint.X + Generated->Radius);
		TickMaterialTestWorld(World);
		TestTrue(TEXT("Rotation preserves original shot direction"), Generated->Body->GetPhysicsLinearVelocity().X > 0);
	}
	TestWorld.ForwardErrorMessages(this);
	return true;
}

#endif
