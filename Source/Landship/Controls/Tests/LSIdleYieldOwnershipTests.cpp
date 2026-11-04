#if WITH_DEV_AUTOMATION_TESTS

#include "Controls/LSOrderComponent.h"
#include "Troops/LSTroopUnit.h"
#include "AIController.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "Tests/AutomationCommon.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSIdleYieldOwnershipTest,
	"Landship.Controls.IdleYieldRequiresOwnedCastlePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSIdleYieldOwnershipTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	ALSTroopUnit* Troop = World->SpawnActor<ALSTroopUnit>();
	if (!TestNotNull(TEXT("Native troop"), Troop) || !Fixture.BeginPlayInTestWorld()) return false;
	AAIController* AI = Cast<AAIController>(Troop->GetController());
	if (!TestNotNull(TEXT("Existing movement owner"), AI) ||
	    !TestTrue(TEXT("Live troop can move"), Troop->Orders->CanMove())) return false;
	Troop->GetCharacterMovement()->SetComponentTickEnabled(false);
	const FVector Start = Troop->GetNavAgentLocation();
	const TArray<FVector> Points = {Start, Start + FVector(1000, 0, 0), Start + FVector(1000, 1000, 0)};
	const FNavPathSharedPtr ExternalPath = MakeShared<FNavigationPath, ESPMode::ThreadSafe>(Points);
	ExternalPath->MarkReady();
	FAIMoveRequest Request(Points.Last());
	Request.SetAcceptanceRadius(5);
	TestTrue(TEXT("Unrelated AI path exists without a player order"), AI->RequestMove(Request, ExternalPath) != FAIRequestID::InvalidRequest);
	TArray<FVector> Corridor = {FVector(99)};
	TestFalse(TEXT("Idle Orders do not advertise another movement owner's path"), Troop->Orders->GetPlannedMovementPath(Corridor));
	TestTrue(TEXT("Rejected query clears old corridor"), Corridor.IsEmpty());
	for (int32 Index = 0; Index < 20; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.01f);
	}
	TestFalse(TEXT("Idle troop never yields without a configured battlefield castle"), Troop->Orders->IsYieldingToCastle());
	TestEqual(TEXT("No hidden movement order was inserted"), Troop->Orders->GetOrderKind(), ELSOrderKind::Idle);
	TestEqual(TEXT("No hidden order was queued"), Troop->Orders->GetQueuedCount(), 0);
	FLSOrder Hold;
	Hold.Kind = ELSOrderKind::Hold;
	TestTrue(TEXT("Player Hold accepted"), Troop->Orders->SubmitOrder(Hold));
	TestEqual(TEXT("Hold stops the existing mover"), AI->GetMoveStatus(), EPathFollowingStatus::Idle);
	TestFalse(TEXT("Hold exposes no travel corridor"), Troop->Orders->GetPlannedMovementPath(Corridor));
	FLSOrder InvalidMove;
	InvalidMove.Kind = ELSOrderKind::Move;
	InvalidMove.Destination = FVector(1.e8);
	TestFalse(TEXT("Invalid replacement rejected"), Troop->Orders->SubmitOrder(InvalidMove));
	TestEqual(TEXT("Rejected movement preserves Hold"), Troop->Orders->GetOrderKind(), ELSOrderKind::Hold);
	Troop->Orders->EndBattleActions();
	TestFalse(TEXT("Battle end cannot advertise or resume yielding"), Troop->Orders->IsYieldingToCastle());
	TestFalse(TEXT("Battle end rejects new Hold"), Troop->Orders->SubmitOrder(Hold));
	Fixture.ForwardErrorMessages(this);
	return true;
}

#endif
