#include "Troops/LSTroopProductionComponent.h"

#include "Combat/LSCollisionChannels.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Match/LSGameState.h"
#include "Match/LSBattleOutcomePolicy.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/LSMineAvoidance.h"
#include "Troops/LSTroopCombatComponent.h"
#include "TimerManager.h"

ULSTroopProductionComponent::ULSTroopProductionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void ULSTroopProductionComponent::BeginPlay()
{
	Super::BeginPlay();
	// 맵에 미리 배치한 병력의 체력 초기화가 끝난 다음 집계한다.
	GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ULSTroopProductionComponent::InitializePopulation);
}

void ULSTroopProductionComponent::InitializePopulation()
{
	ALSPlayerController* Controller = Cast<ALSPlayerController>(GetOwner());
	ALSGameState* State = GetWorld()->GetGameState<ALSGameState>();
	if (!Controller || !Controller->IsLocalController() || !State)
	{
		return;
	}
	TArray<ALSUnit*> InitialUnits;
	for (TActorIterator<ALSUnit> It(GetWorld()); It; ++It)
	{
		if (It->Selection->Kind == ELSSelectionKind::Unit && It->GetFaction() == ELSFaction::Ally && ULSCombatTargetLibrary::IsCombatAlive(*It))
		{
			InitialUnits.Add(*It);
		}
	}
	if (!State->SetCurrentPopulation(InitialUnits.Num()))
	{
		UE_LOG(LogTemp, Error, TEXT("Initial troops exceed the configured population limit"));
		return;
	}
	for (ALSUnit* Unit : InitialUnits)
	{
		TrackUnit(Unit);
	}
	bInitialized = true;
}

AActor* ULSTroopProductionComponent::GetSelectedProducer() const
{
	const ALSPlayerController* Controller = Cast<ALSPlayerController>(GetOwner());
	if (!Controller)
	{
		return nullptr;
	}
	for (AActor* Actor : Controller->GetSelectedActors())
	{
		const ULSSelectableComponent* Selection = Actor->FindComponentByClass<ULSSelectableComponent>();
		const ILSFactionInterface* Faction = Cast<ILSFactionInterface>(Actor);
		if (Selection && Selection->Kind == ELSSelectionKind::Castle && Selection->CanPlayerControl() && Faction &&
		    Faction->GetFaction() == ELSFaction::Ally && ULSCombatTargetLibrary::IsCombatAlive(Actor))
		{
			return Actor;
		}
	}
	return nullptr;
}

float ULSTroopProductionComponent::GetRemainingCooldown(ELSTroopType Type) const
{
	const double* ReadyTime = ReadyTimes.Find(Type);
	return ReadyTime && GetWorld() ? FMath::Max(0.0, *ReadyTime - GetWorld()->GetTimeSeconds()) : 0;
}

ELSProductionResult ULSTroopProductionComponent::GetStatus(ELSTroopType Type) const
{
	const ALSGameState* State = GetWorld() ? GetWorld()->GetGameState<ALSGameState>() : nullptr;
	if (LSBattleOutcome::AreActionsStopped(GetWorld()) || !bInitialized || !State || ALSTroopUnit::GetStatsRowName(Type).IsNone() || !FMath::IsFinite(CooldownSeconds) ||
	    CooldownSeconds <= 0 || !FMath::IsFinite(MaxSearchDistance) || MaxSearchDistance <= 0)
	{
		return ELSProductionResult::Unavailable;
	}
	if (bProducing)
	{
		return ELSProductionResult::Busy;
	}
	if (!GetSelectedProducer())
	{
		return ELSProductionResult::SelectCastle;
	}
	if (GetRemainingCooldown(Type) > 0)
	{
		return ELSProductionResult::Cooldown;
	}
	return static_cast<int64>(State->GetCurrentPopulation()) + 2 > State->GetPopulationLimit() ? ELSProductionResult::PopulationFull
	                                                                    : ELSProductionResult::Ready;
}

FVector ULSTroopProductionComponent::MakeSpawnDirection(const FVector& Forward, int32 Index, int32 Samples)
{
	const int32 Side = Index % 2 ? 1 : -1;
	const float Angle = Side * ((Index + 1) / 2) * 360.0f / Samples;
	return (-Forward).RotateAngleAxis(Angle, FVector::UpVector);
}

bool ULSTroopProductionComponent::IsClearOfTravelPath(const FVector& Point, const TArray<FVector>& Path, float Clearance)
{
	const FVector FlatPoint(Point.X, Point.Y, 0);
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		const FVector Start(Path[Index - 1].X, Path[Index - 1].Y, 0);
		const FVector End(Path[Index].X, Path[Index].Y, 0);
		if (FMath::PointDistToSegmentSquared(FlatPoint, Start, End) <= FMath::Square(Clearance))
		{
			return false;
		}
	}
	return true;
}

bool ULSTroopProductionComponent::FindSpawnLocation(AActor* Castle, ELSTroopType Type, FVector& OutLocation,
	const TArray<FVector>& ReservedLocations) const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav)
	{
		return false;
	}
	float CastleRadius, CastleHeight;
	Castle->GetSimpleCollisionCylinder(CastleRadius, CastleHeight);
	const ALSTroopUnit* TroopDefaults = GetDefault<ALSTroopUnit>();
	// CDO는 아직 RefreshDefinition을 거치지 않아 부모 캡슐 크기다. 나머지 에이전트 속성만 CDO를 쓴다.
	float Radius, HalfHeight;
	ALSTroopUnit::GetCapsuleDimensions(Type, Radius, HalfHeight);
	FNavAgentProperties TroopProperties = TroopDefaults->GetNavAgentPropertiesRef();
	TroopProperties.AgentRadius = Radius;
	TroopProperties.AgentHeight = HalfHeight * 2;
	const FVector GroundOrigin = Castle->GetActorLocation() - FVector(0, 0, CastleHeight);
	const ANavigationData* TroopData = Nav->GetNavDataForProps(TroopProperties, GroundOrigin);
	if (!TroopData)
	{
		return false;
	}
	const float Step = Radius * 2 + 40;
	const float FirstDistance = CastleRadius + Radius + 60;
	const float CorridorClearance = CastleRadius + Radius + 20;
	TArray<FVector> TravelPath;
	if (const ULSOrderComponent* Orders = Castle->FindComponentByClass<ULSOrderComponent>())
	{
		Orders->GetPlannedMovementPath(TravelPath, MaxSearchDistance + CorridorClearance);
	}
	FVector Forward = Castle->GetActorForwardVector().GetSafeNormal2D();
	for (int32 Index = 1; Index < TravelPath.Num(); ++Index)
	{
		const FVector Direction = (TravelPath[Index] - TravelPath[0]).GetSafeNormal2D();
		if (!Direction.IsNearlyZero())
		{
			Forward = Direction;
			break;
		}
	}
	const TArray<FLSMineSphere> Mines = LSMineAvoidance::Snapshot(GetWorld());
	const FVector ProjectionExtent(50, 50, FMath::Max(300.0f, HalfHeight * 2));
	FNavLocation ProducerOrigin;
	if (!Nav->ProjectPointToNavigation(GroundOrigin, ProducerOrigin, ProjectionExtent, TroopData))
	{
		return false;
	}
	FCollisionObjectQueryParams Objects;
	for (ECollisionChannel Channel : {LSCollisionChannels::Unit, LSCollisionChannels::Building,
	                                  LSCollisionChannels::PathObstacle, ECC_WorldStatic, ECC_WorldDynamic})
	{
		Objects.AddObjectTypesToQuery(Channel);
	}
	for (float Distance = FirstDistance; Distance <= MaxSearchDistance; Distance += Step)
	{
		const int32 Samples = FMath::Clamp(FMath::CeilToInt(2 * PI * Distance / Step), 8, 256);
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const FVector Direction = MakeSpawnDirection(Forward, Index, Samples);
			if (FVector::DotProduct(Direction, Forward) > KINDA_SMALL_NUMBER) continue;
			const FVector Candidate = GroundOrigin + Direction * Distance;
			if (!IsClearOfTravelPath(Candidate, TravelPath, CorridorClearance))
			{
				continue;
			}
			FNavLocation Point;
			if (!Nav->ProjectPointToNavigation(Candidate, Point, ProjectionExtent, TroopData) ||
			    FVector::Dist2D(Point.Location, Castle->GetActorLocation()) < FirstDistance - 1 ||
			    FVector::Dist2D(Point.Location, Castle->GetActorLocation()) > MaxSearchDistance ||
			    !IsClearOfTravelPath(Point.Location, TravelPath, CorridorClearance) ||
			    FVector::DotProduct((Point.Location - GroundOrigin).GetSafeNormal2D(), Forward) > KINDA_SMALL_NUMBER)
			{
				continue;
			}
			FVector ApproachHit;
			if (TroopData->Raycast(ProducerOrigin.Location, Point.Location, ApproachHit,
				TroopData->GetDefaultQueryFilter(), this))
			{
				continue;
			}
			const FVector Location = Point.Location + FVector(0, 0, HalfHeight + 4);
			if (ReservedLocations.ContainsByPredicate([&](const FVector& Reserved)
				{ return FVector::DistSquared2D(Location, Reserved) <= FMath::Square(2 * Radius + 4) &&
					FMath::Abs(Location.Z - Reserved.Z) < 2 * HalfHeight + 4; }))
			{
				continue;
			}
			if (Mines.ContainsByPredicate([&](const FLSMineSphere& Mine)
				{ return !LSMineAvoidance::IsCapsuleSegmentSafe(Location, Location, Radius, HalfHeight, Mine); }))
			{
				continue;
			}
			if (GetWorld()->OverlapAnyTestByObjectType(Location, FQuat::Identity, Objects,
			                                           FCollisionShape::MakeCapsule(Radius + 2, HalfHeight + 2)))
			{
				continue;
			}
			// 성은 NavMesh를 파내지 않는다. 성 아래의 병력 표면에서 검사해야 벽 너머 후보의 자기 경로로 통과하지 않는다.
			FPathFindingQuery Query(this, *TroopData, ProducerOrigin.Location, Point.Location);
			Query.SetAllowPartialPaths(false);
			const FPathFindingResult Result = Nav->FindPathSync(TroopProperties, Query);
			if (Result.IsSuccessful() && Result.Path.IsValid() && Result.Path->IsValid() && !Result.Path->IsPartial())
			{
				OutLocation = Location;
				return true;
			}
		}
	}
	return false;
}

bool ULSTroopProductionComponent::FindSpawnPair(AActor* Castle, ELSTroopType Type, TArray<FVector>& OutLocations) const
{
	TArray<FVector> Locations;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FVector Location;
		if (!FindSpawnLocation(Castle, Type, Location, Locations)) return false;
		Locations.Add(Location);
	}
	OutLocations = MoveTemp(Locations);
	return true;
}

ELSProductionResult ULSTroopProductionComponent::Produce(ELSTroopType Type)
{
	const ELSProductionResult Status = GetStatus(Type);
	if (Status != ELSProductionResult::Ready) return Status;
	TGuardValue<bool> Guard(bProducing, true);
	TArray<FVector> Locations;
	if (!FindSpawnPair(GetSelectedProducer(), Type, Locations)) return ELSProductionResult::NoSpace;

	TArray<ALSTroopUnit*> Units;
	TArray<AAIController*> Controllers;
	const auto Rollback = [&]
	{
		for (AAIController* Controller : Controllers)
			if (IsValid(Controller)) Controller->Destroy();
		for (ALSTroopUnit* Unit : Units)
			if (IsValid(Unit)) Unit->Destroy();
	};
	for (const FVector& Location : Locations)
	{
		const FTransform Transform(FRotator::ZeroRotator, Location);
		FActorSpawnParameters Spawn;
		Spawn.bDeferConstruction = true;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// 등록 시 충돌·RVO·자동 빙의가 생기기 전에 격리해야 두 번째 실패를 외부에서 관측하지 않는다.
		Spawn.CustomPreSpawnInitialization = [](AActor* Actor)
		{
			CastChecked<ALSTroopUnit>(Actor)->StageForProduction();
		};
		ALSTroopUnit* Unit = GetWorld()->SpawnActor<ALSTroopUnit>(ALSTroopUnit::StaticClass(), Transform, Spawn);
		if (!IsValid(Unit)) { Rollback(); return ELSProductionResult::Unavailable; }
		Units.Add(Unit);
		Unit->SetInitialFaction(ELSFaction::Ally);
		if (!Unit->SetInitialTroopType(Type)) { Rollback(); return ELSProductionResult::Unavailable; }
		Unit->FinishSpawning(Transform);
		FLSHealthSnapshot Health;
		if (!IsValid(Unit) || Unit->IsActorBeingDestroyed() || !Unit->IsStatsInitialized() ||
			!Unit->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health) || Health.bIsDead ||
			(Unit->SupportsBasicAttack() && !Unit->BasicAttack->IsConfigured()) ||
			!IsValid(Unit->CombatBehavior) || !Unit->CombatBehavior->IsConfigurationValid())
		{
			Rollback();
			return ELSProductionResult::Unavailable;
		}
	}
	for (ALSTroopUnit* Unit : Units)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AAIController* Controller = GetWorld()->SpawnActor<AAIController>(Spawn);
		if (!IsValid(Controller)) { Rollback(); return ELSProductionResult::Unavailable; }
		Controllers.Add(Controller);
	}
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		if (!IsValid(Units[Index]) || Units[Index]->IsActorBeingDestroyed() ||
			!IsValid(Controllers[Index]) || Controllers[Index]->IsActorBeingDestroyed())
		{
			Rollback();
			return ELSProductionResult::Unavailable;
		}
		Controllers[Index]->Possess(Units[Index]);
		if (!IsValid(Units[Index]) || Units[Index]->IsActorBeingDestroyed() ||
			!IsValid(Controllers[Index]) || Controllers[Index]->IsActorBeingDestroyed() ||
			Units[Index]->GetController() != Controllers[Index])
		{
			Rollback();
			return ELSProductionResult::Unavailable;
		}
	}
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		FLSHealthSnapshot Health;
		if (!IsValid(Units[Index]) || Units[Index]->IsActorBeingDestroyed() ||
			!IsValid(Controllers[Index]) || Controllers[Index]->IsActorBeingDestroyed() ||
			Units[Index]->GetController() != Controllers[Index] ||
			!Units[Index]->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health) || Health.bIsDead ||
			(Units[Index]->SupportsBasicAttack() && !Units[Index]->BasicAttack->IsConfigured()) ||
			!IsValid(Units[Index]->CombatBehavior) || !Units[Index]->CombatBehavior->IsConfigurationValid() ||
			LSBattleOutcome::AreActionsStopped(GetWorld()))
		{
			Rollback();
			return ELSProductionResult::Unavailable;
		}
	}
	ALSGameState* State = GetWorld()->GetGameState<ALSGameState>();
	// 격리 중에는 인구 알림·시야 등록·생산 기록이 없다. 검증을 모두 마친 묶음만 한 번 확정한다.
	if (!State || static_cast<int64>(State->GetCurrentPopulation()) + 2 > State->GetPopulationLimit())
	{
		Rollback();
		return ELSProductionResult::PopulationFull;
	}
	LastProducedBatch.Reset();
	for (ALSTroopUnit* Unit : Units)
	{
		TrackUnit(Unit);
		LastProducedBatch.Add(Unit);
		Unit->ReleaseProductionStaging();
	}
	LastProducedUnit = Units.Last();
	ReadyTimes.Add(Type, GetWorld()->GetTimeSeconds() + CooldownSeconds);
	State->ChangePopulation(2);
	for (ALSTroopUnit* Unit : Units)
		if (ULSCombatTargetLibrary::IsCombatAlive(Unit)) Unit->ActivateProducedUnit();
	return ELSProductionResult::Ready;
}

void ULSTroopProductionComponent::TrackUnit(ALSUnit* Unit)
{
	PopulationUnits.Add(Unit);
	Unit->FindComponentByClass<ULSHealthComponent>()->OnDiedEvent.AddDynamic(this,
	                                                                         &ULSTroopProductionComponent::OnUnitDied);
	Unit->OnDestroyed.AddDynamic(this, &ULSTroopProductionComponent::OnUnitDestroyed);
}

void ULSTroopProductionComponent::OnUnitDied(const FLSDamageResult& Damage)
{
	TArray<AActor*> Dead;
	for (const TWeakObjectPtr<ALSUnit>& Unit : PopulationUnits)
	{
		if (Unit.IsValid() && !ULSCombatTargetLibrary::IsCombatAlive(Unit.Get()))
		{
			Dead.Add(Unit.Get());
		}
	}
	for (AActor* Unit : Dead)
	{
		OnUnitDestroyed(Unit);
	}
}

void ULSTroopProductionComponent::OnUnitDestroyed(AActor* Actor)
{
	if (PopulationUnits.Remove(Cast<ALSUnit>(Actor)))
	{
		if (!GetWorld() || GetWorld()->bIsTearingDown)
		{
			return;
		}
		if (ALSGameState* State = GetWorld()->GetGameState<ALSGameState>())
		{
			State->ChangePopulation(-1);
		}
	}
}

void ULSTroopProductionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorld()->GetTimerManager().ClearAllTimersForObject(this);
	for (const TWeakObjectPtr<ALSUnit>& Unit : PopulationUnits)
	{
		if (Unit.IsValid())
		{
			Unit->OnDestroyed.RemoveDynamic(this, &ULSTroopProductionComponent::OnUnitDestroyed);
			Unit->FindComponentByClass<ULSHealthComponent>()->OnDiedEvent.RemoveDynamic(
			    this, &ULSTroopProductionComponent::OnUnitDied);
		}
	}
	PopulationUnits.Reset();
	Super::EndPlay(Reason);
}
