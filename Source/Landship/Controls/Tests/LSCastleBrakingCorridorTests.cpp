#if WITH_DEV_AUTOMATION_TESTS

#include "Controls/LSOrderComponent.h"
#include "Castle/LSCastle.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Castle/LSCastleAIController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Navigation/LSCastleMovementComponent.h"
#include "Navigation/LSCastlePathFollowingComponent.h"
#include "NavigationData.h"
#include "Tests/AutomationCommon.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleBrakingCorridorTest,
	"Landship.Controls.CastleBrakingCorridor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleBrakingCorridorTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 500), FRotator::ZeroRotator);
	ALSCastleAIController* AI = World->SpawnActor<ALSCastleAIController>();
	if (!TestNotNull(TEXT("Castle"), Castle) || !TestNotNull(TEXT("Castle path owner"), AI)) return false;
	Castle->GetCapsuleComponent()->SetCapsuleSize(500, 500);
	AI->Possess(Castle);
	if (!Fixture.BeginPlayInTestWorld()) return false;
	ULSOrderComponent* Orders = Castle->Orders;
	auto* Movement = Cast<ULSCastleMovementComponent>(Castle->GetMovementComponent());
	if (!TestNotNull(TEXT("Actual castle motor"), Movement) || !TestTrue(TEXT("Live movement owner"), Orders->CanMove())) return false;
	Movement->bTurnBeforeForward = true;
	Movement->ForwardBrakingDeceleration = 400;
	Movement->Velocity = FVector(400, 0, 0);
	const FVector Start = Castle->GetNavAgentLocation();
	const FVector ForwardGoal = Start + FVector(2000, 0, 0);
	const FNavPathSharedPtr Forward = MakeShared<FNavigationPath, ESPMode::ThreadSafe>(TArray<FVector>{Start, ForwardGoal});
	Forward->MarkReady();
	// NavMesh 수락 검사는 맵 검사에서 맡는다. 이 검사는 실제 추종기에 완전 경로와 접수된 명령 상태를 연결한다.
	Orders->ActiveOrder.Kind = ELSOrderKind::Move;
	Orders->ActiveOrder.Destination = ForwardGoal;
	Orders->MovementRequestId = AI->RequestMove(FAIMoveRequest(ForwardGoal), Forward);
	TestTrue(TEXT("Active forward path installed"), Orders->MovementRequestId != FAIRequestID::InvalidRequest);
	const auto* Following = Cast<ULSCastlePathFollowingComponent>(AI->GetPathFollowingComponent());
	if (!TestNotNull(TEXT("Actual stopping boundary provider"), Following)) return false;
	float DistanceToStop;
	TestTrue(TEXT("Distant aligned path has more than 200cm to its actual stop boundary"),
		Following->GetDistanceToNextStop(DistanceToStop) && DistanceToStop > 200);
	TArray<FVector> Corridor;
	TestTrue(TEXT("Aligned movement reports actual path"), Orders->GetPlannedMovementPath(Corridor));
	TestEqual(TEXT("Aligned path has no predicted braking detour"), Corridor.Num(), 2);
	const TArray<FVector> TightPoints = {Start, Start + FVector(20, 0, 0), Start + FVector(20, 1000, 0)};
	const FNavPathSharedPtr Tight = MakeShared<FNavigationPath, ESPMode::ThreadSafe>(TightPoints);
	Tight->MarkReady();
	Orders->ActiveOrder.Destination = TightPoints.Last();
	Orders->MovementRequestId = AI->RequestMove(FAIMoveRequest(TightPoints.Last()), Tight);
	TestTrue(TEXT("Aligned tight-corner repath installed"), Orders->MovementRequestId != FAIRequestID::InvalidRequest);
	TestTrue(TEXT("Tight first segment remains aligned with the 400cm/s velocity"),
		FVector::DotProduct(Following->GetCurrentDirection().GetSafeNormal2D(), Movement->Velocity.GetSafeNormal2D()) > 0.99);
	TestTrue(TEXT("Actual tight-corner stopping boundary is inside 200cm braking distance"),
		Following->GetDistanceToNextStop(DistanceToStop) && DistanceToStop < 200);
	TestTrue(TEXT("Immediate aligned tight-corner query includes stopping overrun"), Orders->GetPlannedMovementPath(Corridor));
	if (TestEqual(TEXT("Tight corner reports current, stop, corner, and later path"), Corridor.Num(), 4))
	{
		TestTrue(TEXT("Aligned repath reserves old-forward 200cm before any tick"), Corridor[1].Equals(Start + FVector(200, 0, 0), 0.01));
		TestTrue(TEXT("Original tight corner remains after predicted stop"), Corridor[2].Equals(TightPoints[1], 0.01));
		const FVector SpawnPoint = Start + FVector(602, 0, 0);
		TestTrue(TEXT("Tight path alone leaves old-forward 602cm outside the body corridor"),
			FMath::PointDistToSegmentSquared(SpawnPoint, TightPoints[0], TightPoints[1]) > FMath::Square(562.0) &&
			FMath::PointDistToSegmentSquared(SpawnPoint, TightPoints[1], TightPoints[2]) > FMath::Square(562.0));
		TestTrue(TEXT("Predicted aligned overrun covers old-forward 602cm within body clearance"),
			FMath::PointDistToSegmentSquared(SpawnPoint, Corridor[0], Corridor[1]) < FMath::Square(562.0));
	}
	TestTrue(TEXT("Aligned query does not rewrite the actual tight-corner path"), Following->GetPath() == Tight);
	const FVector ReverseGoal = Start - FVector(1000, 0, 0);
	const TArray<FVector> ReversePoints = {Start, ReverseGoal, ReverseGoal + FVector(0, 1000, 0)};
	const FNavPathSharedPtr Reverse = MakeShared<FNavigationPath, ESPMode::ThreadSafe>(ReversePoints);
	Reverse->MarkReady();
	Orders->ActiveOrder.Destination = ReversePoints.Last();
	Orders->MovementRequestId = AI->RequestMove(FAIMoveRequest(ReversePoints.Last()), Reverse);
	TestTrue(TEXT("Replacement reverse path installed"), Orders->MovementRequestId != FAIRequestID::InvalidRequest);
	Movement->RequestDirectMove(FVector(400, 0, 0), true);
	FLSOrder Hold;
	Hold.Kind = ELSOrderKind::Hold;
	TestTrue(TEXT("Queued player command accepted"), Orders->SubmitOrder(Hold, true));
	// 이동 틱을 한 번도 돌리지 않아 RequestedVelocity에는 이전 전진 요청이 남아 있다.
	TestTrue(TEXT("Previous forward velocity remains before the first movement tick"), Movement->Velocity.Equals(FVector(400, 0, 0)));
	TestTrue(TEXT("Immediate reverse query includes the stopping corridor"), Orders->GetPlannedMovementPath(Corridor));
	if (TestEqual(TEXT("Current, stop, and all remaining actual points"), Corridor.Num(), 4))
	{
		TestTrue(TEXT("400cm/s at 400cm/s2 reserves old-forward 200cm"), Corridor[1].Equals(Start + FVector(200, 0, 0), 0.01));
		TestTrue(TEXT("New reverse leg retained"), Corridor[2].Equals(ReverseGoal, 0.01));
		TestTrue(TEXT("Later actual corner retained"), Corridor.Last().Equals(ReversePoints.Last(), 0.01));
	}
	const FVector UnsafeOldForwardSpawn = Start + FVector(602, 0, 0);
	TestTrue(TEXT("Old-forward spawn is outside the new reverse leg's body clearance"),
		FMath::PointDistToSegmentSquared(UnsafeOldForwardSpawn, Start, ReverseGoal) > FMath::Square(562.0));
	if (Corridor.Num() >= 2)
		TestTrue(TEXT("Braking segment covers that spawn within castle and troop body clearance"),
			FMath::PointDistToSegmentSquared(UnsafeOldForwardSpawn, Corridor[0], Corridor[1]) < FMath::Square(562.0));
	TestTrue(TEXT("Actual AI path was not rewritten"), AI->GetPathFollowingComponent()->GetPath() == Reverse);
	TestTrue(TEXT("Player destination preserved"), Orders->GetOrderDestination().Equals(ReversePoints.Last()));
	TestEqual(TEXT("Player queue preserved"), Orders->GetQueuedCount(), 1);
	TestTrue(TEXT("Read query preserves velocity"), Movement->Velocity.Equals(FVector(400, 0, 0)));
	TestTrue(TEXT("Short horizon query"), Orders->GetPlannedMovementPath(Corridor, 150));
	TestTrue(TEXT("Horizon clips inside braking segment"), Corridor.Num() == 2 && Corridor.Last().Equals(Start + FVector(150, 0, 0), 0.01));
	TestTrue(TEXT("Horizon includes braking and part of new path"), Orders->GetPlannedMovementPath(Corridor, 350));
	TestTrue(TEXT("Horizon counts both segments"), Corridor.Num() == 3 && Corridor.Last().Equals(Start + FVector(50, 0, 0), 0.01));
	Movement->bTurnBeforeForward = false;
	TestTrue(TEXT("Legacy motor keeps actual path only"), Orders->GetPlannedMovementPath(Corridor));
	TestEqual(TEXT("Legacy query has no braking insertion"), Corridor.Num(), 3);
	Movement->bTurnBeforeForward = true;
	Movement->Velocity = FVector::ZeroVector;
	TestTrue(TEXT("Stationary turn keeps actual path"), Orders->GetPlannedMovementPath(Corridor));
	TestEqual(TEXT("Stationary query has no braking insertion"), Corridor.Num(), 3);
	Fixture.ForwardErrorMessages(this);
	return true;
}

#endif
