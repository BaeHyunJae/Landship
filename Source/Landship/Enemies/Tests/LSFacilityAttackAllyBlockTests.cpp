#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AIController.h"
#include "AssetCompilingManager.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Maps/LSBattlefieldNavigationSystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Obstacles/LSBarricade.h"
#include "Obstacles/LSObstacleGeometry.h"
#include "Tests/AutomationCommon.h"
#include "Troops/LSTroopUnit.h"

struct FLSEnemyFacilityAttackTestAccess
{
	static bool IsWaitingBody(const ULSOrderComponent* Orders, const ALSUnit* Ally)
	{
		return Orders->IsWaitingFriendlyBody(Ally);
	}
	static bool FacilityGoal(const ULSOrderComponent* Orders, AActor* Target, FVector& OutGoal)
	{
		return Orders->ResolveAttackDestination(Target, OutGoal, false);
	}
	static bool Destination(const ULSOrderComponent* Orders, const FVector& Desired, FVector& OutGoal, FNavPathSharedPtr& OutPath)
	{
		return Orders->ResolveDestination(Desired, OutGoal, &OutPath);
	}
	static float ArrivalTolerance(const ULSOrderComponent* Orders) { return Orders->ArrivalTolerance; }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLSFacilityAttackAllyBlockTest,
	"Landship.Enemies.FacilityAttack.StationaryAllyOnAcceptedApproach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLSFacilityAttackAllyBlockTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Fixture;
	if (!TestTrue(TEXT("Isolated manually ticked game world"), Fixture.CreateTestWorld(EWorldType::Game))) return false;
	UWorld* World = Fixture.GetTestWorld();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Floor = World->SpawnActor<AActor>();
	UBoxComponent* FloorBody = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(FloorBody);
	Floor->AddInstanceComponent(FloorBody);
	FloorBody->SetBoxExtent(FVector(1800, 1800, 50));
	FloorBody->SetMobility(EComponentMobility::Static);
	FloorBody->SetCollisionObjectType(LSCollisionChannels::Ground);
	FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
	FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Floor->SetActorLocation(FVector(0, 0, -50));
	FloorBody->RegisterComponent();
	ANavMeshBoundsVolume* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
	UBoxComponent* BoundsBox = NewObject<UBoxComponent>(Bounds);
	Bounds->AddInstanceComponent(BoundsBox);
	BoundsBox->SetupAttachment(Bounds->GetRootComponent());
	BoundsBox->SetBoxExtent(FVector(1900, 1900, 600));
	BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsBox->SetCanEverAffectNavigation(false);
	BoundsBox->RegisterComponent();
	ALSBarricade* Facility = World->SpawnActor<ALSBarricade>(FVector(0, 0, 90), FRotator::ZeroRotator, Spawn);
	ALSTroopUnit* Attacker = World->SpawnActor<ALSTroopUnit>(FVector(-1100, 0, 90), FRotator::ZeroRotator, Spawn);
	ALSTroopUnit* Ally = World->SpawnActor<ALSTroopUnit>(FVector(-1100, -1000, 90), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Real barricade"), Facility) || !TestNotNull(TEXT("Attacking troop"), Attacker) ||
		!TestNotNull(TEXT("Normal idle allied troop"), Ally)) return false;
	ULSBattlefieldNavigationSystem* Nav = NewObject<ULSBattlefieldNavigationSystem>(World);
	World->SetNavigationSystem(Nav);
	Nav->InitializeForWorld(*World, FNavigationSystemRunMode::GameMode);
	FAssetCompilingManager::Get().FinishAllCompilation();
	Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	if (!TestFalse(TEXT("Fixture navigation unlocked"), Nav->IsNavigationBuildingLocked())) return false;
	Nav->Build();
	if (!TestTrue(TEXT("Gameplay BeginPlay"), Fixture.BeginPlayInTestWorld())) return false;
	AAIController* AI = Cast<AAIController>(Attacker->GetController());
	UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	ULSHealthComponent* Health = Facility->FindComponentByClass<ULSHealthComponent>();
	if (!TestNotNull(TEXT("Actual AI path follower"), Following) || !TestNotNull(TEXT("Facility health"), Health)) return false;
	FLSBasicAttackSettings Settings = Attacker->BasicAttack->GetAttackSettings();
	Settings.Damage = 1;
	if (!TestTrue(TEXT("Low-damage observation without changing reach"), Attacker->BasicAttack->ConfigureAttack(Settings))) return false;
	bool bRecordMultiEndpoints = false;
	FAIRequestID LastRecordedRequest = FAIRequestID::InvalidRequest;
	TArray<FString> EndpointHistory;
	auto Advance = [&](float Duration)
	{
		while (Duration > KINDA_SMALL_NUMBER)
		{
			const float Step = FMath::Min(Duration, 0.02f);
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Step);
			if (bRecordMultiEndpoints && Following->GetCurrentRequestId() != LastRecordedRequest)
			{
				LastRecordedRequest = Following->GetCurrentRequestId();
				const FNavPathSharedPtr Path = Following->GetPath();
				if (Path.IsValid() && !Path->GetPathPoints().IsEmpty())
					EndpointHistory.Add(FString::Printf(TEXT("t=%.2f request=%u goal=%s"), World->GetTimeSeconds(),
						LastRecordedRequest.GetID(), *Path->GetPathPoints().Last().Location.ToString()));
			}
			Duration -= Step;
		}
	};
	Advance(0.1f);
	FLSOrder Attack;
	Attack.Kind = ELSOrderKind::AttackTarget;
	Attack.Target = Facility;
	if (!TestTrue(TEXT("Public explicit facility attack accepted before obstruction"), Attacker->Orders->SubmitOrder(Attack))) return false;
	const FNavPathSharedPtr AcceptedPath = Following->GetPath();
	if (!TestTrue(TEXT("Accepted approach is a complete actual NavMesh path"), AcceptedPath.IsValid() &&
		AcceptedPath->IsValid() && !AcceptedPath->IsPartial() && AcceptedPath->GetPathPoints().Num() >= 2)) return false;
	const FVector Start = Attacker->GetActorLocation();
	const FVector Endpoint = AcceptedPath->GetPathPoints().Last().Location;
	const FVector AllyPosition((Start.X + Endpoint.X) * 0.5, (Start.Y + Endpoint.Y) * 0.5, Ally->GetActorLocation().Z);
	Ally->SetActorLocation(AllyPosition, false, nullptr, ETeleportType::TeleportPhysics);
	const FVector GroundEndpoint = Endpoint + FVector(0, 0, Attacker->GetActorLocation().Z - Attacker->GetNavAgentLocation().Z);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSFacilityAllyBlockFixture), false, Attacker);
	Query.AddIgnoredActor(Facility);
	FHitResult Hit;
	const UCapsuleComponent* Capsule = Attacker->GetCapsuleComponent();
	if (!TestTrue(TEXT("Inserted idle ally physically blocks the accepted approach"), World->SweepSingleByChannel(
		Hit, Start, GroundEndpoint, Attacker->GetActorQuat(), Capsule->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query,
		FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())) && Hit.GetActor() == Ally)) return false;
	TestTrue(TEXT("Ally blocks the route before facility attack range"), ULSCombatTargetLibrary::GetAttackDistanceAtLocation(
		Attacker, Facility, Hit.Location) > Settings.Range);
	auto InspectExistingCandidates = [&](const TCHAR* Phase)
	{
		const ANavigationData* Data = Nav->GetNavDataForProps(Attacker->GetNavAgentPropertiesRef(), Attacker->GetNavAgentLocation());
		FNavLocation Origin;
		if (!Data || !Nav->ProjectPointToNavigation(Attacker->GetNavAgentLocation(), Origin, FVector(150, 150, 300), Data)) return 0;
		const FVector Offset(0, 0, Attacker->GetActorLocation().Z - Origin.Location.Z);
		const TArray<FVector> Candidates = LSObstacleGeometry::GetAttackApproachCandidates(Attacker, Facility, Settings.Range);
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
		Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
		int32 CompleteFreeEndpoints = 0, ClearPaths = 0;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			FVector Desired = Candidates[Index];
			Desired.Z = Attacker->GetNavAgentLocation().Z;
			FNavLocation Projected;
			if (!Nav->ProjectPointToNavigation(Desired, Projected, FVector(150, 150, 300), Data) ||
				ULSCombatTargetLibrary::GetAttackDistanceAtLocation(Attacker, Facility, Projected.Location + Offset) > Settings.Range) continue;
			FPathFindingQuery Request(Attacker, *Data, Origin.Location, Projected.Location);
			Request.SetAllowPartialPaths(false);
			const FNavPathSharedPtr Path = Nav->FindPathSync(Attacker->GetNavAgentPropertiesRef(), Request).Path;
			if (!Path.IsValid() || !Path->IsValid() || Path->IsPartial()) continue;
			if (World->OverlapAnyTestByObjectType(Projected.Location + Offset, FQuat::Identity, Objects,
				FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius() + 4, Capsule->GetScaledCapsuleHalfHeight()), Query)) continue;
			++CompleteFreeEndpoints;
			FVector From = Attacker->GetActorLocation();
			bool bClear = true;
			for (const FNavPathPoint& Point : Path->GetPathPoints())
			{
				const FVector To = Point.Location + Offset;
				if (World->SweepTestByChannel(From, To, Attacker->GetActorQuat(), Capsule->GetCollisionObjectType(),
					FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query,
					FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()))) { bClear = false; break; }
				From = To;
			}
			if (!bClear) continue;
			++ClearPaths;
			AddInfo(FString::Printf(TEXT("Facility approach alternative phase=%s index=%d goal=%s pathPoints=%d"),
				Phase, Index, *Projected.Location.ToString(), Path->GetPathPoints().Num()));
		}
		AddInfo(FString::Printf(TEXT("Facility approach candidates phase=%s total=%d completeFreeEndpoints=%d physicallyClearPaths=%d"),
			Phase, Candidates.Num(), CompleteFreeEndpoints, ClearPaths));
		return ClearPaths;
	};
	TestTrue(TEXT("Existing candidate set includes a physically clear complete approach after ally insertion"), InspectExistingCandidates(TEXT("inserted")) > 0);
	FLSHealthSnapshot Before;
	if (!Health->TryGetHealth(Before)) return false;
	int32 ReplacedRequests = 0, BlockedResults = 0;
	const FDelegateHandle Observer = Following->OnRequestFinished.AddLambda([&](FAIRequestID, const FPathFollowingResult& Result)
	{
		ReplacedRequests += (Result.Flags & FPathFollowingResultFlags::NewRequest) != 0;
		BlockedResults += Result.Code == EPathFollowingResult::Blocked;
	});
	ON_SCOPE_EXIT { Following->OnRequestFinished.Remove(Observer); };
	Advance(7);
	const FVector Later = Attacker->GetActorLocation();
	Advance(3);
	FLSHealthSnapshot During;
	if (!Health->TryGetHealth(During)) return false;
	const bool bDamaged = During.CurrentHealth < Before.CurrentHealth;
	const double LastThreeSecondsProgress = FVector::Dist2D(Later, Attacker->GetActorLocation());
	InspectExistingCandidates(TEXT("after10s"));
	AddInfo(FString::Printf(TEXT("Facility ally obstruction: damage=%g range=%g distance=%g order=%d failure=%d replacedRequests=%d blockedResults=%d last3sProgress=%g allyDisplacement=%g allyOrder=%d"),
		Before.CurrentHealth - During.CurrentHealth, Settings.Range, ULSCombatTargetLibrary::GetAttackDistance(Attacker, Facility),
		static_cast<int32>(Attacker->Orders->GetOrderKind()), static_cast<int32>(Attacker->Orders->GetLastFailure()),
		ReplacedRequests, BlockedResults, LastThreeSecondsProgress, FVector::Dist2D(AllyPosition, Ally->GetActorLocation()),
		static_cast<int32>(Ally->Orders->GetOrderKind())));
	TestTrue(TEXT("Accepted explicit barricade attack deals actual damage within 10s while the idle ally remains in its original position"), bDamaged);
	TestEqual(TEXT("Blocking ally retained its idle order"), Ally->Orders->GetOrderKind(), ELSOrderKind::Idle);
	TestTrue(TEXT("Attack approach routes around the ally without moving it"), FVector::Dist2D(AllyPosition, Ally->GetActorLocation()) < 1);
	TestFalse(TEXT("Facility attack does not trigger castle yielding"), Ally->Orders->IsYieldingToCastle());
	Ally->SetActorLocation(FVector(-1100, -1000, 90), false, nullptr, ETeleportType::TeleportPhysics);
	if (Attacker->Orders->GetOrderKind() != ELSOrderKind::AttackTarget)
		TestTrue(TEXT("Explicit retry after reported failure"), Attacker->Orders->SubmitOrder(Attack));
	Advance(6);
	FLSHealthSnapshot After;
	if (!Health->TryGetHealth(After)) return false;
	TestTrue(TEXT("Moving only the idle ally away allows actual facility damage with the same reach and collision"), After.CurrentHealth < During.CurrentHealth);
	TestTrue(TEXT("Idle grounded ally is a waiting body"), FLSEnemyFacilityAttackTestAccess::IsWaitingBody(Attacker->Orders, Ally));
	FLSOrder Move;
	Move.Kind = ELSOrderKind::Move;
	Move.Destination = FVector(-500, -1000, 0);
	if (!TestTrue(TEXT("Moving ally has an accepted independent player path"), Ally->Orders->SubmitOrder(Move))) return false;
	TestTrue(TEXT("Moving ally is still at zero velocity before its first movement tick"), Ally->GetVelocity().IsNearlyZero());
	AAIController* AllyAI = Cast<AAIController>(Ally->GetController());
	TestTrue(TEXT("Zero-velocity ally already owns an active path"), AllyAI && AllyAI->GetMoveStatus() != EPathFollowingStatus::Idle);
	TestFalse(TEXT("An active-path ally at zero velocity is not a stationary obstruction"), FLSEnemyFacilityAttackTestAccess::IsWaitingBody(Attacker->Orders, Ally));
	TestTrue(TEXT("Player stop returns the ally to idle"), Ally->Orders->SubmitOrder(FLSOrder()));
	TestTrue(TEXT("Stopped ally again qualifies as a waiting body"), FLSEnemyFacilityAttackTestAccess::IsWaitingBody(Attacker->Orders, Ally));
	// 앞선 충돌 질의의 접촉 위치에서는 기존 후보가 모두 같은 아군 몸체를 먼저 통과한다.
	TestTrue(TEXT("Stop attacker before testing a fully blocked candidate set"), Attacker->Orders->SubmitOrder(FLSOrder()));
	Attacker->SetActorLocation(Hit.Location, false, nullptr, ETeleportType::TeleportPhysics);
	Ally->SetActorLocation(AllyPosition, false, nullptr, ETeleportType::TeleportPhysics);
	TestEqual(TEXT("All existing approach paths are physically blocked at initial contact"), InspectExistingCandidates(TEXT("contactFallback")), 0);
	FVector Fallback;
	TestTrue(TEXT("All blocked routes retain a valid facility approach fallback"), FLSEnemyFacilityAttackTestAccess::FacilityGoal(Attacker->Orders, Facility, Fallback));
	TestTrue(TEXT("All blocked routes keep the original first vacant endpoint fallback"), FVector::Dist2D(Fallback, Endpoint) < 1);
	TestTrue(TEXT("All blocked routes do not reject an otherwise valid explicit attack"), Attacker->Orders->SubmitOrder(Attack));
	TestEqual(TEXT("Fallback preserves the explicit facility attack order"), Attacker->Orders->GetOrderKind(), ELSOrderKind::AttackTarget);
	TestTrue(TEXT("Reset attacker for several stationary allies"), Attacker->Orders->SubmitOrder(FLSOrder()));
	Attacker->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
	Ally->SetActorLocation(AllyPosition, false, nullptr, ETeleportType::TeleportPhysics);
	const FVector Forward = (Endpoint - Start).GetSafeNormal2D();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);
	const FVector PositionB = AllyPosition + Forward * 140 + Side * 200;
	const FVector PositionC = AllyPosition + Forward * 240 + Side * 350;
	ALSTroopUnit* AllyB = World->SpawnActor<ALSTroopUnit>(PositionB, FRotator::ZeroRotator, Spawn);
	ALSTroopUnit* AllyC = World->SpawnActor<ALSTroopUnit>(PositionC, FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Second natural idle ally"), AllyB) || !TestNotNull(TEXT("Third natural idle ally"), AllyC)) return false;
	Advance(0.1f);
	TestTrue(TEXT("Several idle allied bodies leave an actual complete clear approach"), InspectExistingCandidates(TEXT("threeIdleAllies")) > 0);
	for (ALSTroopUnit* Body : {Ally, AllyB, AllyC})
		TestTrue(TEXT("Each multi-body obstacle is a grounded waiting ally"), FLSEnemyFacilityAttackTestAccess::IsWaitingBody(Attacker->Orders, Body));
	FLSHealthSnapshot MultiBefore;
	if (!Health->TryGetHealth(MultiBefore)) return false;
	if (!TestTrue(TEXT("Public explicit facility attack accepted with several waiting allies"), Attacker->Orders->SubmitOrder(Attack))) return false;
	bRecordMultiEndpoints = true;
	Advance(10);
	bRecordMultiEndpoints = false;
	FLSHealthSnapshot MultiAfter;
	if (!Health->TryGetHealth(MultiAfter)) return false;
	AddInfo(FString::Printf(TEXT("Several waiting allies: actualDamage=%g attackerProgress=%g endpointHistory=%s"),
		MultiBefore.CurrentHealth - MultiAfter.CurrentHealth, FVector::Dist2D(Start, Attacker->GetActorLocation()),
		*FString::Join(EndpointHistory, TEXT(" -> "))));
	TestTrue(TEXT("Several waiting allies do not cause an approach deadlock: actual facility damage within 10s"), MultiAfter.CurrentHealth < MultiBefore.CurrentHealth);
	TestTrue(TEXT("Actual movement request endpoints were observed across the multi-body approach"), !EndpointHistory.IsEmpty());
	const TArray<ALSTroopUnit*> Bodies{Ally, AllyB, AllyC};
	const TArray<FVector> Positions{AllyPosition, PositionB, PositionC};
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		TestEqual(TEXT("Every waiting ally retains its idle order"), Bodies[Index]->Orders->GetOrderKind(), ELSOrderKind::Idle);
		TestTrue(TEXT("Approach selection does not move any waiting ally"), FVector::Dist2D(Positions[Index], Bodies[Index]->GetActorLocation()) < 1);
		TestFalse(TEXT("Multi-body facility approach does not invoke castle yielding"), Bodies[Index]->Orders->IsYieldingToCastle());
	}
	AllyB->SetActorLocation(FVector(-1250, -1100, 90), false, nullptr, ETeleportType::TeleportPhysics);
	AllyC->SetActorLocation(FVector(-1400, -1200, 90), false, nullptr, ETeleportType::TeleportPhysics);
	TestTrue(TEXT("Stop attacker before near-arrival preference check"), Attacker->Orders->SubmitOrder(FLSOrder()));
	Ally->SetActorLocation(FVector(-1100, -1000, 90), false, nullptr, ETeleportType::TeleportPhysics);
	const ANavigationData* Data = Nav->GetNavDataForProps(Attacker->GetNavAgentPropertiesRef(), Attacker->GetNavAgentLocation());
	FNavLocation CurrentGround;
	if (!TestTrue(TEXT("Actual standing origin projects for near-arrival case"), Data && Nav->ProjectPointToNavigation(
		Attacker->GetNavAgentLocation(), CurrentGround, FVector(150, 150, 300), Data))) return false;
	const FVector CenterOffset(0, 0, Attacker->GetActorLocation().Z - CurrentGround.Location.Z);
	FVector Face;
	double FaceDistance;
	if (!TestTrue(TEXT("Real facility face defines the near-arrival case"), LSObstacleGeometry::GetClosestBodyPoint(
		Facility->GetObstacleBody(), Start, Face, FaceDistance))) return false;
	const FVector Outward = (Face - Facility->GetActorLocation()).GetSafeNormal2D();
	Face.Z = CurrentGround.Location.Z;
	FNavLocation NearFace;
	if (!TestTrue(TEXT("Nearest reachable face position comes from actual troop NavData"), Nav->ProjectPointToNavigation(
		Face, NearFace, FVector(300, 100, 300), Data))) return false;
	const double MinimumReach = ULSCombatTargetLibrary::GetAttackDistanceAtLocation(Attacker, Facility, NearFace.Location + CenterOffset);
	FLSBasicAttackSettings NearSettings = Settings;
	NearSettings.Range = static_cast<float>(MinimumReach + 4);
	if (!TestTrue(TEXT("Near-arrival range includes the actually reachable body distance"), Attacker->BasicAttack->ConfigureAttack(NearSettings))) return false;
	ON_SCOPE_EXIT { Attacker->BasicAttack->ConfigureAttack(Settings); };
	Attacker->SetActorLocation(NearFace.Location + CenterOffset + Outward * 14, false, nullptr, ETeleportType::TeleportPhysics);
	const TArray<FVector> NearCandidates = LSObstacleGeometry::GetAttackApproachCandidates(Attacker, Facility, NearSettings.Range);
	if (!TestTrue(TEXT("Near-arrival facility still has existing approach candidates"), !NearCandidates.IsEmpty())) return false;
	FVector FirstDesired = NearCandidates[0];
	FirstDesired.Z = Attacker->GetNavAgentLocation().Z;
	FNavLocation FirstProjected;
	if (!TestTrue(TEXT("First existing near-arrival candidate projects"), Nav->ProjectPointToNavigation(
		FirstDesired, FirstProjected, FVector(150, 150, 300), Data))) return false;
	const double NearbyDistance = FVector::Distance(Attacker->GetNavAgentLocation(), FirstProjected.Location);
	AddInfo(FString::Printf(TEXT("Facility near-arrival preference: minimumReach=%g range=%g currentReach=%g firstCandidateDistance=%g current=%s first=%s"),
		MinimumReach, NearSettings.Range, ULSCombatTargetLibrary::GetAttackDistance(Attacker, Facility), NearbyDistance,
		*Attacker->GetActorLocation().ToString(), *FirstProjected.Location.ToString()));
	TestTrue(TEXT("Near-arrival attacker begins outside actual attack range"), ULSCombatTargetLibrary::GetAttackDistance(Attacker, Facility) > NearSettings.Range);
	TestTrue(TEXT("First candidate lies within actual order arrival tolerance"), NearbyDistance < FLSEnemyFacilityAttackTestAccess::ArrivalTolerance(Attacker->Orders));
	TestTrue(TEXT("First candidate standing position is inside actual attack range"), ULSCombatTargetLibrary::GetAttackDistanceAtLocation(
		Attacker, Facility, FirstProjected.Location + CenterOffset) <= NearSettings.Range);
	FVector FastDestination;
	FNavPathSharedPtr FastPath;
	TestTrue(TEXT("Ordinary destination accepts the first nearby candidate"), FLSEnemyFacilityAttackTestAccess::Destination(
		Attacker->Orders, FirstDesired, FastDestination, FastPath));
	TestFalse(TEXT("Near-arrival fast success requires no generated path"), FastPath.IsValid());
	TestTrue(TEXT("Unrelated idle ally keeps facility waiting-body preference active"), FLSEnemyFacilityAttackTestAccess::IsWaitingBody(Attacker->Orders, Ally));
	int32 FartherCandidate = INDEX_NONE;
	FCollisionObjectQueryParams NearbyObjects;
	NearbyObjects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	NearbyObjects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	for (int32 Index = 1; Index < NearCandidates.Num(); ++Index)
	{
		FVector Desired = NearCandidates[Index];
		Desired.Z = Attacker->GetNavAgentLocation().Z;
		FVector Projected;
		FNavPathSharedPtr Path;
		if (!FLSEnemyFacilityAttackTestAccess::Destination(Attacker->Orders, Desired, Projected, Path) ||
			!Path.IsValid() || !Path->IsValid() || Path->IsPartial() ||
			ULSCombatTargetLibrary::GetAttackDistanceAtLocation(Attacker, Facility, Projected + CenterOffset) > NearSettings.Range) continue;
		if (World->OverlapAnyTestByObjectType(Projected + CenterOffset, FQuat::Identity, NearbyObjects,
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius() + 4, Capsule->GetScaledCapsuleHalfHeight()), Query)) continue;
		FartherCandidate = Index;
		AddInfo(FString::Printf(TEXT("Facility farther alternative remains valid: index=%d goal=%s distance=%g"),
			Index, *Projected.ToString(), FVector::Distance(Attacker->GetNavAgentLocation(), Projected)));
		break;
	}
	TestTrue(TEXT("A farther valid candidate would hide accidental skipping of the near-arrival candidate"), FartherCandidate != INDEX_NONE);
	FVector PreferredNearDestination;
	TestTrue(TEXT("Facility preference accepts the nearby first candidate with a waiting ally"), FLSEnemyFacilityAttackTestAccess::FacilityGoal(
		Attacker->Orders, Facility, PreferredNearDestination));
	TestTrue(TEXT("Near-arrival first candidate stays preferred over farther alternatives"), PreferredNearDestination.Equals(FastDestination, 0.1));
	Fixture.ForwardErrorMessages(this);
	return true;
}

#endif
