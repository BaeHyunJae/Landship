#if WITH_DEV_AUTOMATION_TESTS

#include "Navigation/LSMineAvoidance.h"
#include "Obstacles/LSObstacleGeometry.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Obstacles/LSBarricade.h"
#include "Obstacles/LSMine.h"
#include "Combat/LSCollisionChannels.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "Tests/AutomationCommon.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSMineSweptCapsuleTest, "Landship.Navigation.MineSweptCapsule",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSMineSweptCapsuleTest::RunTest(const FString& Parameters)
{
	const FLSMineSphere Sphere{FVector::ZeroVector, 100};
	TestTrue(TEXT("Crossing between safe endpoints is contact"),
			 LSMineAvoidance::CapsuleSegmentTouchesSphere(FVector(-400, 0, 88), FVector(400, 0, 88), 42, 88, Sphere));
	TestFalse(
		TEXT("Infantry path outside actual 3D contact is safe"),
		LSMineAvoidance::CapsuleSegmentTouchesSphere(FVector(-400, 140, 88), FVector(400, 140, 88), 42, 88, Sphere));
	TestTrue(
		TEXT("Same path contacts larger cavalry body"),
		LSMineAvoidance::CapsuleSegmentTouchesSphere(FVector(-400, 140, 88), FVector(400, 140, 88), 65, 88, Sphere));
	TestFalse(
		TEXT("Explosion radius does not constrain contact avoidance"),
		LSMineAvoidance::CapsuleSegmentTouchesSphere(FVector(-400, 200, 88), FVector(400, 200, 88), 65, 88, Sphere));
	TestTrue(TEXT("Vertical translation includes mid-segment contact"),
			 LSMineAvoidance::CapsuleSegmentTouchesSphere(FVector(0, 0, 400), FVector(0, 0, -400), 42, 88, Sphere));
	const FLSMineSphere Raised{FVector(0, 0, 88), 100};
	TestTrue(
		TEXT("Sphere height changes contact boundary"),
		LSMineAvoidance::CapsuleSegmentTouchesSphere(FVector(-400, 140, 88), FVector(400, 140, 88), 42, 88, Raised));
	const FVector ShellStart(FMath::Sqrt(FMath::Square(142.5) - FMath::Square(42.0)), 0, 88);
	TestTrue(TEXT("Noncontact margin-shell start may move outward"),
			 LSMineAvoidance::IsCapsuleSegmentSafe(ShellStart, ShellStart + FVector(20, 0, 0), 42, 88, Sphere));
	TestFalse(TEXT("Noncontact margin-shell start cannot move inward"),
			  LSMineAvoidance::IsCapsuleSegmentSafe(ShellStart, ShellStart - FVector(.1, 0, 0), 42, 88, Sphere));
	TestFalse(TEXT("Actual overlapping start cannot escape through a contact exemption"),
			  LSMineAvoidance::IsCapsuleSegmentSafe(FVector(100, 0, 88), FVector(200, 0, 88), 42, 88, Sphere));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSMineRelevantNeighborhoodTest, "Landship.Navigation.MineRelevantNeighborhood",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSMineRelevantNeighborhoodTest::RunTest(const FString& Parameters)
{
	const TArray<FVector> Baseline { FVector(-800, 0, 88), FVector(800, 0, 88) };
	TArray<FLSMineSphere> Mines {
		{ FVector(0, 0, 0), 100 }, { FVector(0, 280, 0), 100 }, { FVector(0, 560, 0), 100 }
	};
	for (int32 Index = 0; Index < 12; ++Index)
		Mines.Add({ FVector(3000 + Index * 400, 3000, 0), 100 });
	TArray<int32> Relevant;
	TestTrue(TEXT("Distant world mines do not exhaust local vertex budget"),
		LSMineAvoidance::GatherRelevantMineIndices(Baseline, 42, 88, Mines, 8, Relevant));
	TestEqual(TEXT("One baseline blocker and its two connected neighbors are selected"), Relevant.Num(), 3);
	TestTrue(TEXT("Connected-neighborhood expansion is transitive"), Relevant.Contains(2));
	TestFalse(TEXT("Distant hazard does not allocate graph candidates"), Relevant.Contains(3));
	TestTrue(TEXT("Unselected sphere remains a physical hazard when another edge crosses it"),
		LSMineAvoidance::CapsuleSegmentTouchesSphere(FVector(2800, 3000, 88), FVector(3200, 3000, 88), 42, 88, Mines[3]));
	TArray<FLSMineSphere> Dense;
	for (int32 Index = 0; Index < 9; ++Index)
		Dense.Add({ FVector(-600 + Index * 150, 0, 0), 100 });
	TestFalse(TEXT("Dense local neighborhood still reports the unchanged search bound"),
		LSMineAvoidance::GatherRelevantMineIndices(Baseline, 42, 88, Dense, 8, Relevant));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSObstacleClosestBoxTest, "Landship.Obstacles.RotatedBoxSurface",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSObstacleClosestBoxTest::RunTest(const FString& Parameters)
{
	UBoxComponent* Box = NewObject<UBoxComponent>();
	Box->SetBoxExtent(FVector(600, 20, 40));
	Box->SetWorldRotation(FRotator(0, 37, 0));
	const FVector LocalPoint(0, 120, 0);
	const FVector Point = Box->GetComponentQuat().RotateVector(LocalPoint);
	FVector Closest;
	double Distance = -1;
	TestTrue(TEXT("Unregistered simple box still supplies analytic surface"),
			 LSObstacleGeometry::GetClosestBodyPoint(Box, Point, Closest, Distance));
	TestTrue(TEXT("Long boundary radius cannot hide 100cm separation"), FMath::IsNearlyEqual(Distance, 100.0, .001));
	TestTrue(TEXT("Closest point rotates with actual box"),
			 Closest.Equals(Box->GetComponentQuat().RotateVector(FVector(0, 20, 0)), .001));
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("Geometry test world created"), TestWorld.CreateTestWorld(EWorldType::Game)))
		return false;
	ACharacter* Source = TestWorld.GetTestWorld()->SpawnActor<ACharacter>();
	ALSBarricade* Target = TestWorld.GetTestWorld()->SpawnActor<ALSBarricade>();
	if (!TestNotNull(TEXT("Source spawned"), Source) || !TestNotNull(TEXT("Barricade spawned"), Target))
		return false;
	Source->GetCapsuleComponent()->SetCapsuleSize(42, 88);
	UBoxComponent* TargetBox = Cast<UBoxComponent>(Target->GetObstacleBody());
	if (!TestNotNull(TEXT("Barricade box body"), TargetBox))
		return false;
	TargetBox->SetBoxExtent(FVector(600, 20, 40));
	Target->SetActorRotation(FRotator(0, 37, 0));
	const FVector SourceLocation = Target->GetActorQuat().RotateVector(FVector(0, 200, 0));
	Source->SetActorLocation(SourceLocation);
	TestTrue(TEXT("Melee uses actual capsule-box surfaces instead of boundary cylinders"),
			 FMath::IsNearlyEqual(LSObstacleGeometry::GetAttackDistance(Source, Target, SourceLocation), 138.0, .01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSObstaclePlanarSurfaceTest, "Landship.Obstacles.PlanarBlastSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSObstaclePlanarSurfaceTest::RunTest(const FString& Parameters)
{
	UBoxComponent* Box = NewObject<UBoxComponent>();
	Box->SetBoxExtent(FVector(600, 20, 40));
	Box->SetWorldRotation(FRotator(0, 37, 0));
	const FVector Side = Box->GetComponentQuat().RotateVector(FVector(0, 120, 0));
	const FVector End = Box->GetComponentQuat().RotateVector(FVector(650, 0, 0));
	TestTrue(TEXT("Rotated long box side uses the projected body surface"),
		FMath::IsNearlyEqual(LSObstacleGeometry::GetPlanarBodyDistance(Box, Side), 100.0, .001));
	TestTrue(TEXT("Rotated long box end remains in planar blast range"),
		FMath::IsNearlyEqual(LSObstacleGeometry::GetPlanarBodyDistance(Box, End), 50.0, .001));
	Box->SetWorldLocation(FVector(0, 0, 900));
	TestTrue(TEXT("Box height does not alter legacy planar blast distance"),
		FMath::IsNearlyEqual(LSObstacleGeometry::GetPlanarBodyDistance(Box, End), 50.0, .001));
	USphereComponent* Sphere = NewObject<USphereComponent>();
	Sphere->SetSphereRadius(100);
	Sphere->SetWorldLocation(FVector(0, 0, 1000));
	TestTrue(TEXT("Elevated sphere uses its disk footprint"),
		FMath::IsNearlyEqual(LSObstacleGeometry::GetPlanarBodyDistance(Sphere, FVector(160, 0, -500)), 60.0, .001));
	TestTrue(TEXT("Point inside projected sphere has zero planar distance"),
		FMath::IsNearlyZero(LSObstacleGeometry::GetPlanarBodyDistance(Sphere, FVector(30, 0, 0))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSMineDisarmSurfaceLineTest, "Landship.Navigation.MineDisarmSurfaceLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSMineDisarmSurfaceLineTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestTrue(TEXT("Work-line test world created"), TestWorld.CreateTestWorld(EWorldType::Game)))
		return false;
	UWorld* World = TestWorld.GetTestWorld();
	ACharacter* Worker = World->SpawnActor<ACharacter>();
	ALSMine* Mine = World->SpawnActor<ALSMine>();
	AActor* Wall = World->SpawnActor<AActor>();
	if (!Worker || !Mine || !Wall)
		return false;
	Worker->GetCapsuleComponent()->SetCapsuleSize(42, 88);
	Worker->SetActorLocation(FVector(180, 0, 88));
	Mine->SetActorLocation(FVector::ZeroVector);
	UBoxComponent* WallBody = NewObject<UBoxComponent>(Wall);
	Wall->AddInstanceComponent(WallBody);
	Wall->SetRootComponent(WallBody);
	WallBody->SetBoxExtent(FVector(3, 60, 6));
	WallBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WallBody->SetCollisionResponseToAllChannels(ECR_Ignore);
	WallBody->SetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion, ECR_Block);
	WallBody->SetWorldLocation(FVector(80, 0, 19));
	WallBody->RegisterComponent();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSMineDisarmLineTest), false, Worker);
	Query.AddIgnoredActor(Mine);
	TestFalse(TEXT("Actor-origin ray incorrectly clears a low work shield"),
		World->LineTraceTestByChannel(Mine->GetTriggerCenter(), Worker->GetActorLocation(), LSCollisionChannels::ExplosionOcclusion, Query));
	TestFalse(TEXT("Actual nearest capsule surface line is shielded"), LSMineAvoidance::HasDisarmLine(Worker, Mine));
	Worker->SetActorLocation(FVector(-180, 0, 88));
	TestTrue(TEXT("Opposite accessible work position has a clear surface line"), LSMineAvoidance::HasDisarmLine(Worker, Mine));
	Worker->SetActorLocation(FVector(180, 0, 88));
	WallBody->SetWorldLocation(FVector(220, 0, 19));
	TestTrue(TEXT("Wall behind worker cannot cancel the finite work line"), LSMineAvoidance::HasDisarmLine(Worker, Mine));
	return true;
}
#endif
