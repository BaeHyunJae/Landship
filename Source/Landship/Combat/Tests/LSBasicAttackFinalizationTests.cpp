#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSBasicAttackFinalizedReconfigurationTest,
                                 "Landship.Combat.BasicAttack.FinalizedReconfiguration",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSBasicAttackFinalizedReconfigurationTest::RunTest(const FString& Parameters)
{
	auto* Config = GetMutableDefault<ULSCombatDeveloperSettings>();
	TGuardValue<float> RestoreMultiplier(Config->BasicAttackSpeedMultiplier, 2.0f);
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game))
	{
		return false;
	}
	UWorld* World = Play.GetTestWorld();
	auto* Source = World->SpawnActor<ALSBasicAttackTestActor>();
	auto* Target = World->SpawnActor<ALSBasicAttackTestActor>(FVector(100, 0, 0), FRotator::ZeroRotator);
	Target->SetInitialFaction(ELSFaction::Enemy);
	Source->Attack->SetAutoAttackOnBeginPlay(false);
	Source->Attack->OnAttackExecutedEvent.AddDynamic(Source, &ALSBasicAttackTestActor::ObserveExecution);
	FLSBasicAttackSettings Settings;
	Settings.Damage = 10.0f;
	Settings.Interval = 0.4f;
	Settings.Range = 200.0f;
	TestTrue(TEXT("Prepare initial attack"), Source->Attack->ConfigureAttack(Settings));
	if (!Play.BeginPlayInTestWorld())
	{
		return false;
	}
	TestTrue(TEXT("Initial attack starts"), Source->Attack->StartAttack(Target));
	TestEqual(TEXT("Initial attack deals damage"), Target->GetCurrentHealth(), 90.0f);
	TestEqual(TEXT("Initial attack is recorded"), Source->Executions.Num(), 1);
	Source->Attack->EndBattleActions();
	const float HealthAfterEnd = Target->GetCurrentHealth();
	const int32 ExecutionsAfterEnd = Source->Executions.Num();
	Settings.Interval = 0.02f;
	TestTrue(TEXT("Valid settings remain accepted after finalization"), Source->Attack->ConfigureAttack(Settings));
	TestEqual(TEXT("New base interval is retained"), Source->Attack->GetAttackSettings().Interval, 0.02f);
	TestTrue(TEXT("New effective interval uses world multiplier"),
	         FMath::IsNearlyEqual(Source->Attack->GetEffectiveAttackInterval(), 0.01f));
	TestFalse(TEXT("Explicit restart is rejected"), Source->Attack->StartAttack(Target));
	TestFalse(TEXT("Automatic restart is rejected"), Source->Attack->StartAutoAttack());
	for (int32 Frame = 0; Frame < 100; ++Frame)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.01f);
	}
	TestEqual(TEXT("Reconfiguration causes no fresh damage"), Target->GetCurrentHealth(), HealthAfterEnd);
	TestEqual(TEXT("Reconfiguration causes no fresh execution"), Source->Executions.Num(), ExecutionsAfterEnd);
	TestNull(TEXT("Finalized attack keeps no target"), Source->Attack->GetAttackTarget());
	Play.ForwardErrorMessages(this);
	return true;
}

#endif
