#if WITH_DEV_AUTOMATION_TESTS
#include "Controls/LSPlayerController.h"
#include "Controls/LSSelectableComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Engine/World.h"
#include "Tests/AutomationCommon.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSMineOrderRejectionPreservesExecutionTest,
								 "Landship.Controls.Mines.RejectedDisarmPreservesExecution",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSMineOrderRejectionPreservesExecutionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = Fixture.GetTestWorld();
	ALSPlayerController* Controller = World->SpawnActor<ALSPlayerController>();
	ALSBasicAttackTestActor* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSBasicAttackTestActor* Enemy = World->SpawnActor<ALSBasicAttackTestActor>();
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	Enemy->SetActorLocation(FVector(100, 0, 0));
	ULSSelectableComponent* Selection = NewObject<ULSSelectableComponent>(Source);
	Source->AddInstanceComponent(Selection);
	Selection->SetPlayerControlled(true);
	Selection->RegisterComponent();
	ULSOrderComponent* Orders = NewObject<ULSOrderComponent>(Source);
	Source->AddInstanceComponent(Orders);
	Orders->RegisterComponent();
	if (!Fixture.BeginPlayInTestWorld())
		return false;
	FLSBasicAttackSettings Settings;
	Settings.Range = 200;
	Settings.Damage = 10;
	Settings.Interval = 1;
	Source->Attack->ConfigureAttack(Settings);
	FLSOrder Attack;
	Attack.Kind = ELSOrderKind::AttackTarget;
	Attack.Target = Enemy;
	TestTrue(TEXT("Active attack accepted"), Orders->SubmitOrder(Attack));
	TestTrue(TEXT("Queued attack accepted"), Orders->SubmitOrder(Attack, true));
	FLSOrder Disarm;
	Disarm.Kind = ELSOrderKind::Disarm;
	TestFalse(TEXT("Direct queued disarm rejected"), Orders->SubmitOrder(Disarm, true));
	TestEqual(TEXT("Active attack preserved"), Orders->GetOrderKind(), ELSOrderKind::AttackTarget);
	TestEqual(TEXT("Queue preserved"), Orders->GetQueuedCount(), 1);
	Controller->ApplySelection({Source});
	TestFalse(TEXT("Invalid group disarm rejected"),
			  Controller->IssueSelectionOrder(ELSOrderKind::Disarm, FVector::ZeroVector, nullptr, false));
	TestEqual(TEXT("Group rejection preserves active order"), Orders->GetOrderKind(), ELSOrderKind::AttackTarget);
	TestEqual(TEXT("Group rejection preserves queue"), Orders->GetQueuedCount(), 1);
	Fixture.ForwardErrorMessages(this);
	return true;
}
#endif
