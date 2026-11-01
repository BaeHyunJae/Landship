#if WITH_DEV_AUTOMATION_TESTS
#include "Obstacles/LSMine.h"
#include "Obstacles/LSMineHazardSubsystem.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/EnsureScope.h"
#include "Combat/Tests/LSHealthTestObserver.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSMineFailedInitializationTest, "Landship.Obstacles.Mine.FailedInitializationStaysInert",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSMineFailedInitializationTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Play.GetTestWorld();
	ALSMine* Invalid = World->SpawnActor<ALSMine>();
	ALSMine* Valid = World->SpawnActor<ALSMine>();
	Valid->SetActorLocation(FVector(400, 0, 0));
	TestFalse(TEXT("Uninitialized mine is not armed"), Invalid->IsArmed());
	TestFalse(TEXT("Uninitialized mine is not a hazard"), Invalid->IsHazardActive());
	TestFalse(TEXT("Pre-BeginPlay contact trigger rejected"), Invalid->RequestTrigger(ELSMineTriggerCause::Contact));
	TestFalse(TEXT("Pre-BeginPlay chain trigger rejected"), Invalid->RequestTrigger(ELSMineTriggerCause::Chain));
	Invalid->GetContactSphere()->SetSphereRadius(Invalid->GetDisarmDistance());
	ULSHealthTestObserver* Observer = NewObject<ULSHealthTestObserver>(World);
	Invalid->GetHealthComponent()->OnDiedEvent.AddDynamic(Observer, &ULSHealthTestObserver::OnDeath);
	const FString Prefix = TEXT("[LSHealthComponent::InitializeHealth] Owner=");
	AddExpectedErrorPlain(Prefix, EAutomationExpectedErrorFlags::Contains, 1);
	{
		FEnsureScope ExpectedEnsure([this, &Prefix](const FEnsureHandlerArgs& Args)
		{
			const FString Message(Args.Message);
			if (!Message.StartsWith(Prefix)) return false;
			AddError(Message);
			return true;
		});
		if (!Play.BeginPlayInTestWorld()) return false;
		TestEqual(TEXT("Invalid configuration diagnosed once"), ExpectedEnsure.GetCount(), 1);
	}
	TestEqual(TEXT("Failed health persists"), Invalid->GetHealthComponent()->GetInitializationState(), ELSHealthInitializationState::Failed);
	TestFalse(TEXT("Failed body disabled"), Invalid->GetObstacleBody()->IsQueryCollisionEnabled());
	TestFalse(TEXT("Failed contact disabled"), Invalid->GetContactSphere()->IsQueryCollisionEnabled());
	TestFalse(TEXT("Failed mine not registered"), World->GetSubsystem<ULSMineHazardSubsystem>()->GetMines().Contains(Invalid));
	TestFalse(TEXT("Failed mine is not a hazard"), Invalid->IsHazardActive());
	TestFalse(TEXT("Failed contact trigger rejected"), Invalid->RequestTrigger(ELSMineTriggerCause::Contact));
	TestFalse(TEXT("Failed chain trigger rejected"), Invalid->RequestTrigger(ELSMineTriggerCause::Chain));
	TestTrue(TEXT("Valid nearby mine detonates"), Valid->RequestTrigger(ELSMineTriggerCause::DirectDamage));
	for (int32 Index = 0; Index < 30; ++Index) { ++GFrameCounter; World->Tick(LEVELTICK_All, .01f); }
	TestEqual(TEXT("Failure is not a synthetic spent/death transition"), Invalid->GetMineState(), ELSMineState::Armed);
	TestFalse(TEXT("Failure does not remove actor"), Invalid->IsActorBeingDestroyed());
	TestEqual(TEXT("Failure does not emit death"), Observer->DeathResults.Num(), 0);
	Play.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSMinePrototypeBodyTest, "Landship.Obstacles.Mine.PrototypeBodyAndConfiguration",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSMinePrototypeBodyTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game))
		return false;
	ALSMine* Mine = Play.GetTestWorld()->SpawnActor<ALSMine>();
	const USphereComponent* Body = Cast<USphereComponent>(Mine->GetObstacleBody());
	const UStaticMeshComponent* Visual = Cast<UStaticMeshComponent>(Mine->GetDefaultSubobjectByName(TEXT("Visual")));
	if (!TestNotNull(TEXT("Sphere body"), Body) || !TestNotNull(TEXT("Visible body"), Visual) ||
		!TestNotNull(TEXT("Visible sphere mesh"), Visual->GetStaticMesh().Get()))
		return false;
	const FVector Extent = Visual->GetStaticMesh()->GetBounds().BoxExtent * Visual->GetRelativeScale3D();
	const double Radius = Body->GetUnscaledSphereRadius();
	TestTrue(TEXT("Visible sphere matches collision on all axes"), Extent.Equals(FVector(Radius), .1));
	TestTrue(TEXT("Default mine configuration is valid"), Mine->GetConfigurationError().IsEmpty());
	TestEqual(TEXT("Only authoritative body uses attack object type"), Body->GetCollisionObjectType(), LSCollisionChannels::Building);
	TestEqual(TEXT("Contact sensor is outside attack object queries"), Mine->GetContactSphere()->GetCollisionObjectType(), ECC_WorldDynamic);
	TestEqual(TEXT("Contact sensor ignores obstacle projectiles"), Mine->GetContactSphere()->GetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile), ECR_Ignore);
	TestEqual(TEXT("Contact sensor ignores blast shielding"), Mine->GetContactSphere()->GetCollisionResponseToChannel(LSCollisionChannels::ExplosionOcclusion), ECR_Ignore);
	Mine->GetContactSphere()->SetRelativeScale3D(FVector(1, 1, 2));
	TestFalse(TEXT("Contact scaling is rejected"), Mine->GetConfigurationError().IsEmpty());
	Mine->GetContactSphere()->SetRelativeScale3D(FVector::OneVector);
	Mine->GetContactSphere()->SetSphereRadius(Mine->GetDisarmDistance());
	TestFalse(TEXT("No safe distance band is rejected"), Mine->GetConfigurationError().IsEmpty());
	Play.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSMineFirstTriggerTest, "Landship.Obstacles.Mine.FirstTriggerSurvivesLethalDamage",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSMineFirstTriggerTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game))
		return false;
	UWorld* World = Play.GetTestWorld();
	ALSMine* Mine = World->SpawnActor<ALSMine>();
	if (!Play.BeginPlayInTestWorld())
		return false;
	TestTrue(TEXT("Chain accepted"), Mine->RequestTrigger(ELSMineTriggerCause::Chain));
	TestFalse(TEXT("Contact cannot replace chain"), Mine->RequestTrigger(ELSMineTriggerCause::Contact));
	FLSDamageRequest Request;
	Request.DamageAmount = 1000;
	Mine->GetHealthComponent()->ApplyDamage(Request);
	TestTrue(TEXT("Dead health does not cancel delay"), Mine->IsTriggered());
	TestTrue(TEXT("Delayed mine remains hazard"),
			 World->GetSubsystem<ULSMineHazardSubsystem>()->GetMines().Contains(Mine));
	for (int32 Index = 0; Index < 15; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, .01f);
	}
	TestTrue(TEXT("Original delay still pending"), Mine->IsTriggered());
	TWeakObjectPtr<ALSMine> Weak = Mine;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, .01f);
	}
	TestTrue(TEXT("Spent or removed after original delay"),
			 !Weak.IsValid() || Weak->GetMineState() == ELSMineState::Spent);
	Play.ForwardErrorMessages(this);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSMinePositiveHitTest, "Landship.Obstacles.Mine.NonlethalPositiveHitTriggers",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLSMinePositiveHitTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Play;
	if (!Play.CreateTestWorld(EWorldType::Game))
		return false;
	ALSMine* Mine = Play.GetTestWorld()->SpawnActor<ALSMine>();
	if (!Play.BeginPlayInTestWorld())
		return false;
	FLSDamageRequest Request;
	Request.DamageAmount = .001f;
	Mine->GetHealthComponent()->ApplyDamage(Request);
	TestEqual(TEXT("Nonlethal hit explodes synchronously"), Mine->GetMineState(), ELSMineState::Spent);
	TestFalse(TEXT("Spent contact disabled"), Mine->GetContactSphere()->IsQueryCollisionEnabled());
	TestEqual(TEXT("Registry removes spent mine immediately"),
			  Play.GetTestWorld()->GetSubsystem<ULSMineHazardSubsystem>()->GetMines().Num(), 0);
	Play.ForwardErrorMessages(this);
	return true;
}
#endif
