#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Troops/LSTroopProductionComponent.h"
#include "Troops/Tests/LSTroopPairProductionObserver.h"
#include "Castle/LSCastle.h"
#include "Castle/Tests/LSCastleTestFixture.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSPlayerController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Match/LSGameMode.h"
#include "Match/LSGameState.h"
#include "Maps/LSBattlefieldSettings.h"
#include "Maps/LSBattlefieldNavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "AssetCompilingManager.h"
#include "AIController.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/EnsureScope.h"
#include "UObject/UnrealType.h"
#include "Navigation/LSCastleMovementComponent.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Vision/LSVisionSubsystem.h"

struct FLSTroopPairTestAccess
{
	static void Initialize(ULSTroopProductionComponent* Production) { Production->InitializePopulation(); }
	static void SetSearchDistance(ULSTroopProductionComponent* Production, float Distance) { Production->MaxSearchDistance = Distance; }
	static bool FindPair(ULSTroopProductionComponent* Production, AActor* Castle, TArray<FVector>& Locations)
	{
		return Production->FindSpawnPair(Castle, ELSTroopType::Spearman, Locations);
	}
	static bool FindOne(ULSTroopProductionComponent* Production, AActor* Castle, FVector& Location)
	{
		return Production->FindSpawnLocation(Castle, ELSTroopType::Spearman, Location);
	}
};

namespace
{
AActor* MakePairBox(UWorld* World, const FVector& Center, const FVector& Extent, bool bNavigation)
{
	AActor* Actor = World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
	Actor->SetRootComponent(Box);
	Actor->AddInstanceComponent(Box);
	Box->SetBoxExtent(Extent);
	Box->SetMobility(EComponentMobility::Static);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCanEverAffectNavigation(bNavigation);
	Actor->SetActorLocation(Center);
	Box->RegisterComponent();
	return Actor;
}

bool BuildPairNavigation(UWorld* World)
{
	MakePairBox(World, FVector(0, 0, -50), FVector(2200, 2200, 50), true);
	ANavMeshBoundsVolume* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Bounds);
	Bounds->AddInstanceComponent(Box);
	Box->SetupAttachment(Bounds->GetRootComponent());
	Box->SetBoxExtent(FVector(2300, 2300, 600));
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetCanEverAffectNavigation(false);
	Box->RegisterComponent();
	ULSBattlefieldNavigationSystem* Nav = NewObject<ULSBattlefieldNavigationSystem>(World);
	World->SetNavigationSystem(Nav);
	Nav->InitializeForWorld(*World, FNavigationSystemRunMode::EditorMode);
	FAssetCompilingManager::Get().FinishAllCompilation();
	Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	if (Nav->IsNavigationBuildingLocked()) return false;
	Nav->Build();
	return true;
}

int32 CountPairControllers(UWorld* World)
{
	int32 Count = 0;
	for (TActorIterator<AAIController> It(World); It; ++It)
		if (!It->IsActorBeingDestroyed()) ++Count;
	return Count;
}

int32 CountPairTroops(UWorld* World)
{
	int32 Count = 0;
	for (TActorIterator<ALSTroopUnit> It(World); It; ++It)
		if (!It->IsActorBeingDestroyed()) ++Count;
	return Count;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSTroopPairProductionTest, "Landship.Troops.PairProduction.AtomicLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSTroopPairProductionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!TestTrue(TEXT("Pair world"), Fixture.CreateTestWorld(EWorldType::Game))) return false;
	UWorld* World = Fixture.GetTestWorld();
	World->GetWorldSettings()->DefaultGameMode = ALSGameMode::StaticClass();
	ALSBattlefieldSettings* Settings = World->SpawnActor<ALSBattlefieldSettings>();
	ALSPlayerController* Controller = World->SpawnActor<ALSPlayerController>();
	if (!TestNotNull(TEXT("Controller"), Controller)) return false;
	ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
	Player->PlayerController = Controller;
	Controller->Player = Player;
	if (!TestTrue(TEXT("Production owner is a local player controller"), Controller->IsLocalController())) return false;
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 104));
	if (!TestNotNull(TEXT("Actual game castle"), Castle) || !TestNotNull(TEXT("Controller"), Controller) ||
		!TestNotNull(TEXT("Vision settings"), Settings) || !TestTrue(TEXT("Pair navigation"), BuildPairNavigation(World)) ||
		!TestTrue(TEXT("Pair BeginPlay"), Fixture.BeginPlayInTestWorld())) return false;
	Castle->Orders->SetComponentTickEnabled(false);
	Castle->Vision->SetSightRadius(0);
	ALSGameState* State = World->GetGameState<ALSGameState>();
	if (!TestNotNull(TEXT("Population state"), State)) return false;
	ULSTroopProductionComponent* Production = Controller->Production;
	FLSTroopPairTestAccess::Initialize(Production);
	Controller->ApplySelection({Castle});
	TestEqual(TEXT("Actual castle is selected producer"), Production->GetSelectedProducer(), static_cast<AActor*>(Castle));
	State->SetPopulation(99, 100);
	TestEqual(TEXT("One population slot cannot fund two troops"), Production->GetStatus(ELSTroopType::Spearman), ELSProductionResult::PopulationFull);
	State->SetPopulation(0, 100);
	if (!TestEqual(TEXT("Initialized local production is ready"), Production->GetStatus(ELSTroopType::Spearman), ELSProductionResult::Ready)) return false;
	ULSTroopPairProductionObserver* Observer = NewObject<ULSTroopPairProductionObserver>(World);
	State->OnPopulationChanged.AddDynamic(Observer, &ULSTroopPairProductionObserver::OnPopulationChanged);

	TArray<FVector> Slots;
	if (!TestTrue(TEXT("Two safe slots exist"), FLSTroopPairTestAccess::FindPair(Production, Castle, Slots))) return false;
	TestEqual(TEXT("Exactly two planned slots"), Slots.Num(), 2);
	TestTrue(TEXT("Pair capsules reserve separate positions"), FVector::Dist2D(Slots[0], Slots[1]) > 88);
	// 실패 경로에서 생산된 시야가 적 최초 발견을 남기는지도 검사한다.
	const FTransform EnemyTransform(FRotator::ZeroRotator, Slots[0] + FVector(-500, 0, 0));
	ALSBasicAttackTestActor* Enemy = World->SpawnActorDeferred<ALSBasicAttackTestActor>(
		ALSBasicAttackTestActor::StaticClass(), EnemyTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Enemy->SetInitialFaction(ELSFaction::Enemy);
	Enemy->Attack->SetAutoAttackOnBeginPlay(false);
	ULSVisionParticipantComponent* EnemyVision = NewObject<ULSVisionParticipantComponent>(Enemy);
	Enemy->AddInstanceComponent(EnemyVision);
	EnemyVision->bTriggersEnemyDiscovery = true;
	EnemyVision->RegisterComponent();
	Enemy->FinishSpawning(EnemyTransform);
	ULSVisionSubsystem* Vision = World->GetSubsystem<ULSVisionSubsystem>();
	Vision->RequestRefresh();
	if (!TestFalse(TEXT("Enemy initially undiscovered"), Vision->HasDiscoveredEnemy())) return false;

	int32 StagedSpawns = 0;
	bool bEverySpawnIsIsolated = true;
	const FDelegateHandle FailureHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
	{
		if (ALSTroopUnit* Unit = Cast<ALSTroopUnit>(Actor))
		{
			++StagedSpawns;
			bEverySpawnIsIsolated &= Unit->IsGameplayStaged() && Unit->IsHidden() && !Unit->GetActorEnableCollision() &&
				!Unit->GetController() && !Unit->Vision->IsParticipationEnabled() && !Unit->GetCapsuleComponent()->GetGenerateOverlapEvents() &&
				!Unit->GetCharacterMovement()->bUseRVOAvoidance;
			if (StagedSpawns == 2) Unit->Destroy();
		}
	}));
	TestEqual(TEXT("Second spawn failure rejects whole pair"), Production->Produce(ELSTroopType::Spearman), ELSProductionResult::Unavailable);
	World->RemoveOnActorSpawnedHandler(FailureHandle);
	TestEqual(TEXT("Both spawns reached isolation"), StagedSpawns, 2);
	TestTrue(TEXT("Registration never exposed a staged body"), bEverySpawnIsIsolated);
	TestEqual(TEXT("Rollback leaves no troops"), CountPairTroops(World), 0);
	TestEqual(TEXT("Rollback emits no temporary population event"), Observer->Populations.Num(), 0);
	TestEqual(TEXT("Rollback preserves population"), State->GetCurrentPopulation(), 0);
	TestEqual(TEXT("Rollback preserves cooldown"), Production->GetRemainingCooldown(ELSTroopType::Spearman), 0.0f);
	TestEqual(TEXT("Rollback preserves empty production history"), Production->GetLastProducedBatch().Num(), 0);
	TestFalse(TEXT("Rollback never discovers enemy"), Vision->HasDiscoveredEnemy());

	const int32 InitialControllers = CountPairControllers(World);
	int32 ControllerSpawns = 0;
	const FDelegateHandle ControllerFailure = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
	{
		if (AAIController* AI = Cast<AAIController>(Actor))
			if (++ControllerSpawns == 2) AI->Destroy();
	}));
	TestEqual(TEXT("Second controller failure rejects whole pair"), Production->Produce(ELSTroopType::Spearman), ELSProductionResult::Unavailable);
	World->RemoveOnActorSpawnedHandler(ControllerFailure);
	TestEqual(TEXT("Both controllers were staged"), ControllerSpawns, 2);
	TestEqual(TEXT("Controller rollback leaves no native controllers"), CountPairControllers(World), InitialControllers);
	TestEqual(TEXT("Controller rollback leaves no troops"), CountPairTroops(World), 0);
	TestEqual(TEXT("Controller rollback has no population events"), Observer->Populations.Num(), 0);
	TestFalse(TEXT("Controller rollback has no discovery"), Vision->HasDiscoveredEnemy());

	int32 InitSpawns = 0;
	const FDelegateHandle InitFailure = World->AddOnActorPreSpawnInitialization(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
	{
		if (ALSTroopUnit* Unit = Cast<ALSTroopUnit>(Actor))
		{
			if (++InitSpawns == 2)
			{
				FSoftObjectProperty* Property = FindFProperty<FSoftObjectProperty>(ALSTroopUnit::StaticClass(), TEXT("TroopStatsTable"));
				*Property->ContainerPtrToValuePtr<TSoftObjectPtr<UDataTable>>(Unit) = nullptr;
			}
		}
	}));
	{
		const FString Prefix = TEXT("[LSCombatStats::TryReadRow] Combat stats initialization failed:");
		AddExpectedErrorPlain(Prefix, EAutomationExpectedErrorFlags::Contains, 1);
		FEnsureScope ExpectedEnsure([this, &Prefix](const FEnsureHandlerArgs& Args)
		{
			const FString Message(Args.Message);
			if (!Message.StartsWith(Prefix)) return false;
			AddError(Message);
			return true;
		});
		TestEqual(TEXT("Second initialization failure rejects whole pair"), Production->Produce(ELSTroopType::Spearman), ELSProductionResult::Unavailable);
		TestEqual(TEXT("Exactly one initialization failure diagnosed"), ExpectedEnsure.GetCount(), 1);
	}
	World->RemoveOnActorPreSpawnInitialization(InitFailure);
	TestEqual(TEXT("Both initialization sources were reached before native initialization"), InitSpawns, 2);
	TestEqual(TEXT("Initialization rollback leaves no troops"), CountPairTroops(World), 0);
	TestEqual(TEXT("Initialization rollback has no population events"), Observer->Populations.Num(), 0);
	TestFalse(TEXT("Initialization rollback has no discovery"), Vision->HasDiscoveredEnemy());
	TestEqual(TEXT("All failures preserve cooldown"), Production->GetRemainingCooldown(ELSTroopType::Spearman), 0.0f);
	TestEqual(TEXT("All failures preserve history"), Production->GetLastProducedBatch().Num(), 0);

	Castle->SetActorRotation(FRotator(0, 90, 0));
	FLSOrder Move;
	Move.Kind = ELSOrderKind::Move;
	Move.Destination = FVector(1500, 0, 0);
	if (!TestTrue(TEXT("Moving actual castle accepts order"), Castle->Orders->SubmitOrder(Move))) return false;
	ULSCastleMovementComponent* CastleMovement = Cast<ULSCastleMovementComponent>(Castle->GetMovementComponent());
	if (!TestNotNull(TEXT("Actual pawn movement"), CastleMovement)) return false;
	CastleMovement->bTurnBeforeForward = true;
	CastleMovement->Velocity = FVector(200, 0, 0);
	TArray<FVector> MovingSlots;
	if (!TestTrue(TEXT("Moving castle still provides pair slots"), FLSTroopPairTestAccess::FindPair(Production, Castle, MovingSlots))) return false;
	TestEqual(TEXT("Moving pair has two rear or side positions"), MovingSlots.Num(), 2);
	for (const FVector& Slot : MovingSlots)
		TestTrue(TEXT("Moving production follows path rear rather than facing rear"), Slot.X <= Castle->GetActorLocation().X + 1);
	FLSOrder Hold;
	Hold.Kind = ELSOrderKind::Hold;
	Castle->Orders->SubmitOrder(Hold);
	CastleMovement->Velocity = FVector::ZeroVector;
	Castle->SetActorRotation(FRotator::ZeroRotator);

	if (!TestEqual(TEXT("Pair produces successfully"), Production->Produce(ELSTroopType::Spearman), ELSProductionResult::Ready)) return false;
	TestEqual(TEXT("Exactly two troops committed"), CountPairTroops(World), 2);
	TestEqual(TEXT("One population event for a committed pair"), Observer->Populations.Num(), 1);
	TestEqual(TEXT("Committed pair population"), State->GetCurrentPopulation(), 2);
	if (!TestEqual(TEXT("Batch history contains both"), Production->GetLastProducedBatch().Num(), 2)) return false;
	for (const TWeakObjectPtr<ALSTroopUnit>& Unit : Production->GetLastProducedBatch())
	{
		TestTrue(TEXT("Committed troop active with controller and vision"), Unit.IsValid() && !Unit->IsGameplayStaged() &&
			!Unit->IsHidden() && Unit->GetActorEnableCollision() && Unit->GetController() && Unit->Vision->IsParticipationEnabled());
		if (Unit.IsValid()) Unit->Orders->SetComponentTickEnabled(false);
	}
	TestEqual(TEXT("Legacy getter names final troop"), Production->GetLastProducedUnit(), Production->GetLastProducedBatch().Last().Get());
	TestTrue(TEXT("One type cooldown starts once"), Production->GetRemainingCooldown(ELSTroopType::Spearman) > 9.9f);
	TestEqual(TEXT("Same type cannot immediately produce again"), Production->Produce(ELSTroopType::Spearman), ELSProductionResult::Cooldown);
	TestEqual(TEXT("Other type remains ready"), Production->GetStatus(ELSTroopType::Archer), ELSProductionResult::Ready);
	State->OnPopulationChanged.RemoveDynamic(Observer, &ULSTroopPairProductionObserver::OnPopulationChanged);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSTroopPairOnlyOnePositionTest, "Landship.Troops.PairProduction.RequiresTwoPositions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSTroopPairOnlyOnePositionTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!Fixture.CreateTestWorld(EWorldType::Editor)) return false;
	UWorld* World = Fixture.GetTestWorld();
	ALSCastle* Castle = LSCastleTestFixture::Spawn(World, FVector(0, 0, 104));
	if (!Castle || !BuildPairNavigation(World)) return false;
	ULSTroopProductionComponent* Production = NewObject<ULSTroopProductionComponent>(Castle);
	float CastleRadius, CastleHalfHeight;
	Castle->GetSimpleCollisionCylinder(CastleRadius, CastleHalfHeight);
	const float FirstDistance = CastleRadius + 42 + 60;
	FLSTroopPairTestAccess::SetSearchDistance(Production, FirstDistance + 20);
	MakePairBox(World, FVector(1000 - FirstDistance + 60, 0, 150), FVector(1000, 1500, 150), false);
	FVector Single;
	TestTrue(TEXT("Exactly one rear capsule fits"), FLSTroopPairTestAccess::FindOne(Production, Castle, Single));
	TArray<FVector> Locations = {FVector(9999)};
	TestFalse(TEXT("One position cannot satisfy pair"), FLSTroopPairTestAccess::FindPair(Production, Castle, Locations));
	TestEqual(TEXT("Failed pair search preserves output"), Locations.Num(), 1);
	TestEqual(TEXT("Failed pair search does not publish first slot"), Locations[0], FVector(9999));
	return true;
}
#endif
