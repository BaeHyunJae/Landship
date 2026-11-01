#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Misc/AutomationTest.h"
#include "Obstacles/LSMine.h"
#include "Tests/AutomationCommon.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Vision/LSVisionSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSMineVisionMemoryTest, "Landship.Vision.Mine.DiscoveryMemoryAndLifetime",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSMineVisionMemoryTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Ordinary marker has no explosion ring"), FLSMinimapMarker().ExplosionRadius, 0.0f);
	FTestWorldWrapper Fixture;
	if (!TestTrue(TEXT("Create world"), Fixture.CreateTestWorld(EWorldType::Game)))
		return false;
	UWorld* World = Fixture.GetTestWorld();
	World->SpawnActor<ALSBattlefieldSettings>();
	ALSBasicAttackTestActor* Observer = World->SpawnActor<ALSBasicAttackTestActor>();
	ALSMine* Mine = World->SpawnActor<ALSMine>(FVector(1500, 0, 0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Observer"), Observer) || !TestNotNull(TEXT("Mine"), Mine))
		return false;
	Observer->SetActorEnableCollision(false);
	Observer->Attack->SetAutoAttackOnBeginPlay(false);
	ULSVisionParticipantComponent* Sight = NewObject<ULSVisionParticipantComponent>(Observer);
	Observer->AddInstanceComponent(Sight);
	Sight->bProvidesSight = true;
	Sight->SetSightRadius(600);
	Sight->RegisterComponent();
	Mine->GetContactSphere()->SetRelativeLocation(FVector(500, 0, 0));
	Mine->GetVisionComponent()->bTriggersEnemyDiscovery = true;
	if (!TestTrue(TEXT("Begin play"), Fixture.BeginPlayInTestWorld()))
		return false;
	ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
	if (!TestNotNull(TEXT("Vision"), Vision))
		return false;
	auto MineMarkers = [&]()
	{
		TArray<FLSMinimapMarker> Result;
		for (const FLSMinimapMarker& Marker : Vision->GetMinimapMarkers())
		{
			if (Marker.ExplosionRadius > 0)
				Result.Add(Marker);
		}
		return Result;
	};
	TestTrue(TEXT("World mine remains visible outside sight"), Vision->IsActorVisibleToFriendly(Mine));
	TArray<AActor*> Concealed;
	Vision->GetConcealedActors(Concealed);
	TestFalse(TEXT("World cursor can still hit fogged mine"), Concealed.Contains(Mine));
	TestFalse(TEXT("Direct orders require actual sight"), ULSVisionSubsystem::PassesFriendlySight(Observer, Mine));
	TestEqual(TEXT("Undiscovered mine has no marker"), MineMarkers().Num(), 0);
	Observer->SetActorLocation(FVector(1000, 0, 1000));
	Vision->RequestRefresh();
	TestTrue(TEXT("Actor origin is in sight"), Vision->IsPointVisible(Mine->GetActorLocation()));
	TestFalse(TEXT("Trigger offset remains outside sight"), ULSVisionSubsystem::PassesFriendlySight(Observer, Mine));
	TestEqual(TEXT("Origin sight cannot discover mine"), MineMarkers().Num(), 0);
	Observer->SetActorLocation(FVector(1500, 0, 1000));
	Vision->RequestRefresh();
	TestTrue(TEXT("Trigger point permits direct orders"), ULSVisionSubsystem::PassesFriendlySight(Observer, Mine));
	TestEqual(TEXT("Actual sight discovers marker"), MineMarkers().Num(), 1);
	if (MineMarkers().Num() == 1)
	{
		TestEqual(TEXT("Marker uses trigger center"), MineMarkers()[0].Location, Mine->GetTriggerCenter());
		TestEqual(TEXT("Marker carries explosion radius"), MineMarkers()[0].ExplosionRadius,
				  Mine->GetExplosionRadius());
	}
	TestFalse(TEXT("Mine never discovers an enemy caption"), Vision->HasDiscoveredEnemy());
	Observer->SetActorLocation(FVector(-2000, 0, 1000));
	Vision->RequestRefresh();
	TestEqual(TEXT("Discovered marker persists outside sight"), MineMarkers().Num(), 1);
	TestFalse(TEXT("Persistent marker grants no attack permission"),
			  ULSVisionSubsystem::PassesFriendlySight(Observer, Mine));
	TestTrue(TEXT("Start delayed chain"), Mine->RequestTrigger(ELSMineTriggerCause::Chain));
	Mine->GetHealthComponent()->ApplyDamage(ULSHealthComponent::MakeDamageRequest(1000, nullptr, nullptr));
	TestTrue(TEXT("Lethal damage preserves triggered lifetime"), Mine->IsTriggered());
	TestEqual(TEXT("Triggered dead marker persists"), MineMarkers().Num(), 1);
	const TWeakObjectPtr<ALSMine> PendingMine = Mine;
	// 첫 타이머 틱은 대기 타이머를 활성화하므로 실제 프레임을 나눠 지연 시간을 진행한다.
	for (int32 Frame = 0; Frame < 25; ++Frame)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, 0.01f);
	}
	TestTrue(TEXT("Chain reached spent state or actor removal"),
			 !PendingMine.IsValid() || PendingMine->GetMineState() == ELSMineState::Spent);
	TestEqual(TEXT("Spent mine marker removed outside sight"), MineMarkers().Num(), 0);
	ALSMine* Removed = World->SpawnActor<ALSMine>(FVector(-1500, 0, 0), FRotator::ZeroRotator);
	Vision->RequestRefresh();
	TestEqual(TEXT("New nearby mine discovered"), MineMarkers().Num(), 1);
	Observer->SetActorLocation(FVector(1500, 0, 1000));
	Vision->RequestRefresh();
	Removed->Destroy();
	TestEqual(TEXT("EndPlay removes remembered mine immediately"), MineMarkers().Num(), 0);
	Fixture.ForwardErrorMessages(this);
	return true;
}

#endif
