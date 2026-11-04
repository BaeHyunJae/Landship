#if WITH_DEV_AUTOMATION_TESTS

#include "Castle/LSCastle.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Castle/LSCastleAIController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Navigation/LSCastleMovementComponent.h"
#include "Navigation/LSCastlePathFollowingComponent.h"
#include "NavigationData.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastleTurnBeforeForwardTest,
	"Landship.Castle.Movement.TurnBeforeForwardBothPathModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastleTurnBeforeForwardTest::RunTest(const FString& Parameters)
{
	for (bool bAccelerationPath : {false, true})
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		AActor* Floor = World->SpawnActor<AActor>();
		UBoxComponent* Body = NewObject<UBoxComponent>(Floor);
		Floor->SetRootComponent(Body);
		Body->SetBoxExtent(FVector(10000, 10000, 50));
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Body->SetCollisionResponseToAllChannels(ECR_Block);
		Body->RegisterComponent();
		Floor->SetActorLocation(FVector(0, 0, -50));
		auto* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 144), FRotator::ZeroRotator);
		auto* Movement = CastChecked<ULSCastleMovementComponent>(Castle->GetMovementComponent());
		TestFalse(TEXT("실제 성의 선회 이동은 맵에서 명시적으로 켬"), Movement->bTurnBeforeForward);
		Movement->bTurnBeforeForward = true;
		if (!Fixture.BeginPlayInTestWorld()) return false;
		Castle->Orders->SetComponentTickEnabled(false);
		Movement->MaxSpeed = 400;
		const auto Step = [Movement, bAccelerationPath](const FVector& Direction)
		{
			if (bAccelerationPath) Movement->RequestPathMove(Direction);
			else Movement->RequestDirectMove(Direction * 400, false);
			++GFrameCounter;
			Movement->TickComponent(.01f, LEVELTICK_All, nullptr);
		};
		const FVector Start = Castle->GetActorLocation();
		for (int32 Index = 0; Index < 50; ++Index) Step(FVector::YAxisVector);
		TestTrue(TEXT("반 초 동안 45도 선회"), FMath::IsNearlyEqual(Castle->GetActorRotation().Yaw, 45.0f, .1f));
		TestTrue(TEXT("선회 중 수평 이동 없음"), FVector::Dist2D(Start, Castle->GetActorLocation()) < .1);
		for (int32 Index = 0; Index < 100; ++Index) Step(FVector::YAxisVector);
		TestTrue(TEXT("정렬 후 전진"), Castle->GetActorLocation().Y > Start.Y + 10);
		TestTrue(TEXT("전진 가속을 적용"), Movement->Velocity.Size2D() > 100 && Movement->Velocity.Size2D() < 200);
		const double BeforeBraking = Movement->Velocity.Size2D();
		for (int32 Index = 0; Index < 10; ++Index) Step(FVector::YAxisVector * .25);
		TestTrue(TEXT("감속 요청은 제동률 안에서 점진적으로 적용"),
			Movement->Velocity.Size2D() >= 100 && Movement->Velocity.Size2D() <= BeforeBraking - 30);
		const FVector BeforeReverse = Castle->GetActorLocation();
		const double BeforeReverseSpeed = Movement->Velocity.Size2D();
		for (int32 Index = 0; Index < 10; ++Index) Step(-FVector::YAxisVector);
		TestTrue(TEXT("반대 명령은 먼저 기존 방향으로 점진 제동"),
			Movement->Velocity.Y > 0 && Movement->Velocity.Size2D() <= BeforeReverseSpeed - 30);
		TestTrue(TEXT("제동 중에는 선회 호를 만들지 않음"), FMath::IsNearlyEqual(Castle->GetActorRotation().Yaw, 90.0f, .1f));
		for (int32 Index = 0; Index < 100 && Movement->Velocity.Size2D() > 1; ++Index) Step(-FVector::YAxisVector);
		TestTrue(TEXT("후진 없이 기존 직선 안에서 제동"), Castle->GetActorLocation().Y > BeforeReverse.Y &&
			FMath::Abs(Castle->GetActorLocation().X - BeforeReverse.X) < .1);
		const FVector AfterBraking = Castle->GetActorLocation();
		for (int32 Index = 0; Index < 50; ++Index) Step(-FVector::YAxisVector);
		TestTrue(TEXT("제동을 마친 뒤 제자리 선회"), FVector::Dist2D(AfterBraking, Castle->GetActorLocation()) < .1);
		TestTrue(TEXT("반대 명령에 45도만 추가 선회"), FMath::IsNearlyEqual(
			FMath::Abs(FMath::FindDeltaAngleDegrees(90.0f, Castle->GetActorRotation().Yaw)), 45.0f, .1f));
		Movement->StopMovementImmediately();
		const float StoppedYaw = Castle->GetActorRotation().Yaw;
		for (int32 Index = 0; Index < 50; ++Index)
		{
			++GFrameCounter;
			Movement->TickComponent(.01f, LEVELTICK_All, nullptr);
		}
		TestTrue(TEXT("정지는 이전 경로의 선회를 재개하지 않음"), FMath::IsNearlyEqual(Castle->GetActorRotation().Yaw, StoppedYaw, .1f));
		TestTrue(TEXT("정지 뒤 잔여 속도 없음"), Movement->Velocity.IsNearlyZero());
		Fixture.ForwardErrorMessages(this);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSCastlePathBrakingTest,
	"Landship.Castle.Movement.PathCornerAndArrivalBrakingBothModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSCastlePathBrakingTest::RunTest(const FString& Parameters)
{
	for (bool bAccelerationPath : {false, true})
	{
		FTestWorldWrapper Fixture;
		if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Fixture.GetTestWorld();
		AActor* Floor = World->SpawnActor<AActor>();
		UBoxComponent* Body = NewObject<UBoxComponent>(Floor);
		Floor->SetRootComponent(Body);
		Body->SetBoxExtent(FVector(10000, 10000, 50));
		Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Body->SetCollisionResponseToAllChannels(ECR_Block);
		Body->RegisterComponent();
		Floor->SetActorLocation(FVector(0, 0, -50));
		auto* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 502), FRotator::ZeroRotator);
		Castle->GetCapsuleComponent()->SetCapsuleSize(500, 500);
		auto* Movement = CastChecked<ULSCastleMovementComponent>(Castle->GetMovementComponent());
		Movement->bTurnBeforeForward = true;
		Movement->GetNavMovementProperties()->bUseAccelerationForPaths = bAccelerationPath;
		if (!Fixture.BeginPlayInTestWorld()) return false;
		Castle->Orders->SetComponentTickEnabled(false);
		Movement->MaxSpeed = 400;
		auto* AI = World->SpawnActor<ALSCastleAIController>();
		AI->Possess(Castle);
		auto* Following = CastChecked<ULSCastlePathFollowingComponent>(AI->GetPathFollowingComponent());
		const FVector Start = Castle->GetNavAgentLocation();
		const TArray<FVector> Points = {Start, Start + FVector(1400, 0, 0), Start + FVector(1400, 2200, 0)};
		const FNavPathSharedPtr Path = MakeShared<FNavigationPath, ESPMode::ThreadSafe>(Points);
		Path->MarkReady();
		FAIMoveRequest Request(Points.Last());
		Request.SetAcceptanceRadius(30);
		Request.SetReachTestIncludesAgentRadius(true);
		if (!TestTrue(TEXT("실제 세 점 경로 요청 수락"), AI->RequestMove(Request, Path) != FAIRequestID::InvalidRequest)) return false;
		bool bCornerBraking = false;
		bool bArrivalBraking = false;
		bool bTurned = false;
		bool bTranslatedWhileTurning = false;
		bool bExcessDeceleration = false;
		bool bCompleted = false;
		for (int32 Index = 0; Index < 2500; ++Index)
		{
			const FVector Before = Castle->GetActorLocation();
			const float BeforeYaw = Castle->GetActorRotation().Yaw;
			const double BeforeSpeed = Movement->Velocity.Size2D();
			const uint32 BeforeSegment = Following->GetNextPathIndex();
			++GFrameCounter;
			World->Tick(LEVELTICK_All, .01f);
			const double Speed = Movement->Velocity.Size2D();
			if (BeforeSpeed > 100 && Speed < BeforeSpeed - 1)
			{
				if (BeforeSegment == 1) bCornerBraking = true;
				else bArrivalBraking = true;
			}
			if (BeforeSpeed - Speed > Movement->ForwardBrakingDeceleration * .01 + 1.1) bExcessDeceleration = true;
			if (FMath::Abs(FMath::FindDeltaAngleDegrees(BeforeYaw, Castle->GetActorRotation().Yaw)) > .1)
			{
				bTurned = true;
				if (FVector::Dist2D(Before, Castle->GetActorLocation()) > .1) bTranslatedWhileTurning = true;
			}
			if (Following->GetStatus() == EPathFollowingStatus::Idle)
			{
				bCompleted = Following->DidMoveReachGoal();
				break;
			}
		}
		TestTrue(TEXT("모서리 전에 점진 제동을 관찰"), bCornerBraking);
		TestTrue(TEXT("최종 도착 허용 경계 전에 점진 제동을 관찰"), bArrivalBraking);
		TestTrue(TEXT("90도 경로 모서리에서 선회"), bTurned);
		TestFalse(TEXT("제동률을 넘는 순간 감속 없음"), bExcessDeceleration);
		TestFalse(TEXT("선회 도중 이동하여 모서리를 자르지 않음"), bTranslatedWhileTurning);
		TestTrue(TEXT("기존 경로 소유자가 정상 도착 완료"), bCompleted);
		TestTrue(TEXT("캡슐을 포함하는 도착 경계 유지"), FVector::Dist2D(Castle->GetNavAgentLocation(), Points.Last()) <= 581);
		Fixture.ForwardErrorMessages(this);
	}
	return true;
}

#endif
