#include "Troops/LSTroopProductionComponent.h"

#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Controls/LSPlayerController.h"
#include "Controls/LSSelectableComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Match/LSGameState.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
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
	if (!bInitialized || !State || ALSTroopUnit::GetStatsRowName(Type).IsNone() || !FMath::IsFinite(CooldownSeconds) ||
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
	return State->GetCurrentPopulation() >= State->GetPopulationLimit() ? ELSProductionResult::PopulationFull
	                                                                    : ELSProductionResult::Ready;
}

bool ULSTroopProductionComponent::FindSpawnLocation(AActor* Castle, ELSTroopType Type, FVector& OutLocation) const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav)
	{
		return false;
	}
	float CastleRadius, CastleHeight;
	Castle->GetSimpleCollisionCylinder(CastleRadius, CastleHeight);
	const float Radius = Type == ELSTroopType::Cavalry ? 65.0f : 42.0f;
	const float Step = Radius * 2 + 40;
	const float FirstDistance = CastleRadius + Radius + 60;
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
			const float Angle = 2 * PI * Index / Samples;
			const FVector Candidate =
			    Castle->GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Distance;
			FNavLocation Point;
			if (!Nav->ProjectPointToNavigation(Candidate, Point, FVector(50, 50, 500)) ||
			    FVector::Dist2D(Point.Location, Castle->GetActorLocation()) < FirstDistance - 1 ||
			    FVector::Dist2D(Point.Location, Castle->GetActorLocation()) > MaxSearchDistance)
			{
				continue;
			}
			const FVector Location = Point.Location + FVector(0, 0, 92);
			if (GetWorld()->OverlapAnyTestByObjectType(Location, FQuat::Identity, Objects,
			                                           FCollisionShape::MakeCapsule(Radius + 2, 90)))
			{
				continue;
			}
			const APawn* Pawn = Cast<APawn>(Castle);
			UNavigationPath* Path = Nav->FindPathToLocationSynchronously(
			    GetWorld(), Pawn ? Pawn->GetNavAgentLocation() : Castle->GetActorLocation(), Point.Location, Castle);
			if (Path && Path->IsValid() && !Path->IsPartial())
			{
				OutLocation = Location;
				return true;
			}
		}
	}
	return false;
}

ELSProductionResult ULSTroopProductionComponent::Produce(ELSTroopType Type)
{
	const ELSProductionResult Status = GetStatus(Type);
	if (Status != ELSProductionResult::Ready)
	{
		return Status;
	}
	TGuardValue<bool> Guard(bProducing, true);
	AActor* Castle = GetSelectedProducer();
	FVector Location;
	if (!FindSpawnLocation(Castle, Type, Location))
	{
		return ELSProductionResult::NoSpace;
	}
	ALSGameState* State = GetWorld()->GetGameState<ALSGameState>();
	if (!State->ChangePopulation(1))
	{
		return ELSProductionResult::PopulationFull;
	}
	const FTransform Transform(FRotator::ZeroRotator, Location);
	ALSTroopUnit* Unit =
	    GetWorld()->SpawnActorDeferred<ALSTroopUnit>(ALSTroopUnit::StaticClass(), Transform, nullptr, nullptr,
	                                                 ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
	if (Unit)
	{
		Unit->SetInitialFaction(ELSFaction::Ally);
		Unit->SetInitialTroopType(Type);
		Unit->FinishSpawning(Transform);
	}
	if (!IsValid(Unit) || Unit->IsActorBeingDestroyed() || !ULSCombatTargetLibrary::IsCombatAlive(Unit))
	{
		if (IsValid(Unit))
		{
			Unit->Destroy();
		}
		State->ChangePopulation(-1);
		return ELSProductionResult::Unavailable;
	}
	TrackUnit(Unit);
	LastProducedUnit = Unit;
	ReadyTimes.Add(Type, GetWorld()->GetTimeSeconds() + CooldownSeconds);
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
