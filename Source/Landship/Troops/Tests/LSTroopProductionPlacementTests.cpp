#include "Troops/LSTroopProductionComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSTroopProductionPlacementTest, "Landship.Troops.ProductionPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSTroopProductionPlacementTest::RunTest(const FString& Parameters)
{
	for (const FVector Forward : {FVector::ForwardVector, -FVector::ForwardVector,
		FVector::RightVector, -FVector::RightVector})
	{
		const FVector Rear = ULSTroopProductionComponent::MakeSpawnDirection(Forward, 0, 16);
		TestTrue(TEXT("First stationary/moving candidate prefers rear in every heading"),
			FVector::DotProduct(Rear, Forward) < -0.99f);
		TArray<FVector> Path = {FVector::ZeroVector, Forward * 2000};
		TestFalse(TEXT("Active path excludes front even with no velocity input"),
			ULSTroopProductionComponent::IsClearOfTravelPath(Forward * 700, Path, 562));
		TestTrue(TEXT("Rear remains clear of enlarged producer travel corridor"),
			ULSTroopProductionComponent::IsClearOfTravelPath(Rear * 602, Path, 562));
		TestFalse(TEXT("Castle500 plus troop42 and margin rejects a side near path"),
			ULSTroopProductionComponent::IsClearOfTravelPath(Forward * 700 + Forward.RotateAngleAxis(90, FVector::UpVector) * 550, Path, 562));
	}
	const TArray<FVector> TurningPath = {FVector::ZeroVector, FVector(1000, 0, 0), FVector(1000, 1500, 0)};
	TestFalse(TEXT("Later turn is excluded even outside first segment corridor"),
		ULSTroopProductionComponent::IsClearOfTravelPath(FVector(1000, 1000, 0), TurningPath, 562));
	TestFalse(TEXT("Post-projection candidate entering a later segment is rejected"),
		ULSTroopProductionComponent::IsClearOfTravelPath(FVector(1510, 1000, 0), TurningPath, 562));
	TestTrue(TEXT("Safe side candidate remains available"),
		ULSTroopProductionComponent::IsClearOfTravelPath(FVector(1700, 1000, 0), TurningPath, 562));
	TestTrue(TEXT("Stationary producer has no fabricated travel corridor"),
		ULSTroopProductionComponent::IsClearOfTravelPath(FVector(-602, 0, 0), {}, 562));
	TArray<FVector> Directions;
	for (int32 Index = 0; Index < 16; ++Index)
	{
		const FVector Direction = ULSTroopProductionComponent::MakeSpawnDirection(FVector::ForwardVector, Index, 16);
		TestTrue(TEXT("Bounded angular search covers distinct directions"), !Directions.ContainsByPredicate(
			[&Direction](const FVector& Previous) { return Direction.Equals(Previous, 0.001); }));
		Directions.Add(Direction);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSTroopProductionBodyTest, "Landship.Troops.ProductionBodyDimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSTroopProductionBodyTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Game)) return false;
	for (const ELSTroopType Type : {ELSTroopType::Spearman, ELSTroopType::Archer, ELSTroopType::Cavalry})
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALSTroopUnit* Unit = Fixture.GetTestWorld()->SpawnActor<ALSTroopUnit>(Spawn);
		if (!TestNotNull(TEXT("Troop body fixture"), Unit)) return false;
		TestTrue(TEXT("Apply produced troop type"), Unit->SetInitialTroopType(Type));
		float Radius, HalfHeight;
		ALSTroopUnit::GetCapsuleDimensions(Type, Radius, HalfHeight);
		TestEqual(TEXT("Production radius matches configured troop"),
			Unit->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), Radius);
		TestEqual(TEXT("Production half-height matches configured troop"),
			Unit->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), HalfHeight);
		Unit->Destroy();
	}
	return true;
}
#endif
