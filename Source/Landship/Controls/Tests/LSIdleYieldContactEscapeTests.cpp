#if WITH_DEV_AUTOMATION_TESTS

#include "Controls/LSOrderComponent.h"
#include "Castle/LSCastle.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Combat/LSCollisionChannels.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/AutomationCommon.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSIdleYieldContactEscapeTest,
	"Landship.Controls.IdleYieldContactEscape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSIdleYieldContactEscapeTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	UWorld* World = Fixture.GetTestWorld();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALSTroopUnit* Troop = World->SpawnActor<ALSTroopUnit>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 500), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Troop"), Troop) || !TestNotNull(TEXT("Castle"), Castle)) return false;
	Troop->GetCapsuleComponent()->SetCapsuleSize(42, 88);
	Castle->GetCapsuleComponent()->SetCapsuleSize(500, 500);
	// 구형인 성 캡슐의 낮은 표면에 선 병력 캡슐이 정확히 닿는 수평 거리다.
	const double ContactX = FMath::Sqrt(FMath::Square(542.0) - FMath::Square(366.0));
	const FVector Start(ContactX, 0, 88), End = Start + FVector(0, 700, 0);
	Troop->SetActorLocation(Start);
	if (!Fixture.BeginPlayInTestWorld()) return false;
	Troop->GetCharacterMovement()->SetComponentTickEnabled(false);
	Castle->GetMovementComponent()->SetComponentTickEnabled(false);
	TestEqual(TEXT("Actual castle contact fixture keeps its sphere radius"), Castle->GetCapsuleComponent()->GetScaledCapsuleRadius(), 500.0f);
	TestEqual(TEXT("Actual castle contact fixture keeps its sphere half-height"), Castle->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), 500.0f);
	TestTrue(TEXT("Actual castle contact uses its root collision body"), Castle->GetRootComponent() == Castle->GetCapsuleComponent());
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSIdleYieldContactTest), false, Troop);
	TestTrue(TEXT("Previous inflated sweep rejects exact castle contact"), World->SweepTestByObjectType(
		Start, End, FQuat::Identity, Objects, FCollisionShape::MakeCapsule(46, 88), Query));
	TestTrue(TEXT("Exact castle contact permits a separating side path"), Troop->Orders->IsIdleYieldPathClear({Start, End}));
	TestFalse(TEXT("Contact cannot first move deeper into the castle"), Troop->Orders->IsIdleYieldPathClear(
		{Start, Start - FVector(10, 0, 0), End}));
	TestFalse(TEXT("An escape cannot later cross the initial castle"), Troop->Orders->IsIdleYieldPathClear(
		{Start, End, FVector(-ContactX, 0, 88), End + FVector(0, 200, 0)}));
	ALSTroopUnit* Behind = World->SpawnActor<ALSTroopUnit>(Start - FVector(0, 84, 0), FRotator::ZeroRotator, Spawn);
	ALSTroopUnit* Beside = World->SpawnActor<ALSTroopUnit>(Start + FVector(84, 0, 0), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Touching ally behind"), Behind) || !TestNotNull(TEXT("Touching ally beside"), Beside)) return false;
	Behind->GetCapsuleComponent()->SetCapsuleSize(42, 88);
	Beside->GetCapsuleComponent()->SetCapsuleSize(42, 88);
	TestTrue(TEXT("Near cluster permits escape that separates from every initial contact"), Troop->Orders->IsIdleYieldPathClear({Start, End}));
	TestFalse(TEXT("Near cluster rejects escape into an initially touching ally"), Troop->Orders->IsIdleYieldPathClear(
		{Start, Start - FVector(0, 700, 0)}));
	ALSTroopUnit* Later = World->SpawnActor<ALSTroopUnit>(Start + FVector(0, 350, 0), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Ally along escape"), Later)) return false;
	Later->GetCapsuleComponent()->SetCapsuleSize(42, 88);
	TestFalse(TEXT("Initial contact exception does not ignore another ally along the path"), Troop->Orders->IsIdleYieldPathClear({Start, End}));
	Later->SetActorLocation(FVector(5000, 5000, 88));
	AActor* Wall = World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Wall->AddInstanceComponent(Box);
	Box->SetBoxExtent(FVector(150, 20, 150));
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Wall->SetActorLocation(Start + FVector(0, 350, 0));
	TestFalse(TEXT("A real wall across a clear-endpoint escape still blocks"), Troop->Orders->IsIdleYieldPathClear({Start, End}));
	Wall->SetActorLocation(Start + FVector(0, 60, 0));
	TestFalse(TEXT("An initially overlapping wall is never ignored"), Troop->Orders->IsIdleYieldPathClear({Start, End}));
	Wall->SetActorLocation(FVector(5000, -5000, 88));
	TestTrue(TEXT("Moving blockers away restores the same safe escape"), Troop->Orders->IsIdleYieldPathClear({Start, End}));
	const FVector CrowdCenter(6138, 0, 90);
	TArray<ALSTroopUnit*> Crowd;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		ALSTroopUnit* Ally = World->SpawnActor<ALSTroopUnit>(CrowdCenter + FVector(0, (Index - 3.5f) * 200, 0),
			FRotator::ZeroRotator, Spawn);
		if (!TestNotNull(TEXT("Dense eight-worker yield fixture"), Ally)) return false;
		Ally->GetCharacterMovement()->SetComponentTickEnabled(false);
		Crowd.Add(Ally);
	}
	ALSTroopUnit* CenterWorker = Crowd[4];
	Crowd[5]->Orders->bYieldingToCastle = true;
	Crowd[5]->Orders->YieldDestination = CrowdCenter + FVector(-144, 662, -80);
	bool bNearbyYielding = false;
	const float ReservedOffset = CenterWorker->Orders->GetIdleYieldAlongOffset(CrowdCenter, FVector::ForwardVector, FVector::RightVector, 602, bNearbyYielding);
	TestTrue(TEXT("Moving outer workers mark changing local yield space"), bNearbyYielding);
	TestTrue(TEXT("Inner goal clears the outer worker's reserved endpoint"), ReservedOffset > 144);
	Crowd[2]->Orders->bYieldingToCastle = true;
	Crowd[2]->Orders->YieldDestination = CrowdCenter + FVector(-500, -662, -80);
	const float IndependentOffset = CenterWorker->Orders->GetIdleYieldAlongOffset(CrowdCenter, FVector::ForwardVector,
		FVector::RightVector, 602, bNearbyYielding);
	TestEqual(TEXT("Opposite-side reserved slot does not displace this side's goals"), IndependentOffset, ReservedOffset);
	Crowd[5]->Orders->bYieldingToCastle = false;
	Crowd[5]->SetActorLocation(CrowdCenter + FVector(-143, 659, 0));
	Crowd[6]->SetActorLocation(CrowdCenter + FVector(113, 659, 0));
	const float SettledOffset = CenterWorker->Orders->GetIdleYieldAlongOffset(CrowdCenter, FVector::ForwardVector, FVector::RightVector, 602, bNearbyYielding);
	TestFalse(TEXT("Settled workers stop deferring the stable-geometry budget"), bNearbyYielding);
	TestTrue(TEXT("Settled outer workers leave an inner goal beyond occupied fixed slots"), SettledOffset > 144);
	Fixture.ForwardErrorMessages(this);
	return true;
}

#endif
