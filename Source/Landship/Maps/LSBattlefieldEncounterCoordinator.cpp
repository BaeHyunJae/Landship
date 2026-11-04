#include "Maps/LSBattlefieldEncounterCoordinator.h"
#include "Maps/LSBattlefieldNavigationSystem.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSOutpost.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Enemies/LSBattlefieldStateSubsystem.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSFactionInterface.h"
#include "Obstacles/LSMine.h"
#include "Obstacles/LSTurret.h"
#include "Troops/LSUnit.h"
#include "Match/LSBattleOutcomePolicy.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

ALSBattlefieldEncounterCoordinator::ALSBattlefieldEncounterCoordinator()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

bool ALSBattlefieldEncounterCoordinator::AreActionsStopped() const
{
	if (LSBattleOutcome::AreActionsStopped(GetWorld())) return true;
	const auto* Battlefield = GetWorld()->GetSubsystem<ULSBattlefieldStateSubsystem>();
	return !LSBattleOutcome::IsManaged(GetWorld()) && Battlefield && Battlefield->IsBattleEndedForEnemies();
}

FString ALSBattlefieldEncounterCoordinator::GetDormantMembershipError(const AActor* Member)
{
	int32 Count = 0;
	const ALSBattlefieldEncounterCoordinator* Coordinator = nullptr;
	for (TActorIterator<ALSBattlefieldEncounterCoordinator> It(Member->GetWorld()); It; ++It)
	{
		for (const FLSEncounterRegion& Region : It->Regions)
			for (const AActor* Candidate : Region.AssaultMembers)
				if (Candidate == Member) { ++Count; Coordinator = *It; }
	}
	if (Count != 1) return FString::Printf(TEXT("Dormant Assault %s requires exactly one region membership; found %d"), *Member->GetPathName(), Count);
	return Coordinator->Validate(nullptr, false);
}

FString ALSBattlefieldEncounterCoordinator::Validate(const TMap<FName, FTransform>* Transforms, bool bCheckWorldMembership) const
{
	const TArray<FName> Required = {TEXT("L1"), TEXT("L2"), TEXT("C1"), TEXT("C2"), TEXT("R1"), TEXT("R2"), TEXT("Core"), TEXT("CentralGuardOnly")};
	if (Regions.Num() != Required.Num()) return TEXT("Encounter requires exactly eight regions");
	TSet<FName> Ids;
	TMap<const AActor*, FName> Owners;
	auto PositionOf = [&](const AActor* Actor)
	{
		const FTransform* Placement = Transforms ? Transforms->Find(Actor->GetFName()) : nullptr;
		return Placement ? Placement->GetLocation() : Actor->GetActorLocation();
	};
	auto ContainsXY = [](const TArray<FBox>& Bounds, const FVector& Position)
	{
		for (const FBox& Box : Bounds)
			if (Position.X >= Box.Min.X && Position.X <= Box.Max.X && Position.Y >= Box.Min.Y && Position.Y <= Box.Max.Y) return true;
		return false;
	};
	auto Register = [&](const AActor* Actor, FName RegionId) -> FString
	{
		if (!IsValid(Actor) || Actor->GetWorld() != GetWorld() || Actor == this || !Actor->GetRootComponent())
			return TEXT("Encounter references require live same-world actors with roots");
		if (const FName* Existing = Owners.Find(Actor); Existing && *Existing != RegionId)
			return FString::Printf(TEXT("%s belongs to multiple regions"), *Actor->GetPathName());
		Owners.Add(Actor, RegionId);
		if (Transforms)
		{
			if (const FTransform* Placement = Transforms->Find(Actor->GetFName()); Placement &&
				(Placement->ContainsNaN() || Placement->GetScale3D().GetMin() <= 0))
				return TEXT("Encounter preset contains an invalid actor transform");
		}
		return FString();
	};
	for (const FLSEncounterRegion& Region : Regions)
	{
		if (!Required.Contains(Region.RegionId) || Ids.Contains(Region.RegionId)) return TEXT("Encounter RegionId is unknown or duplicated");
		Ids.Add(Region.RegionId);
		if (Region.EntryBounds.IsEmpty()) return TEXT("Every encounter region requires explicit entry bounds");
		if (Region.PlacementBounds.IsEmpty()) return TEXT("Every encounter region requires explicit placement bounds");
		for (const TArray<FBox>* Bounds : {&Region.EntryBounds, &Region.PlacementBounds, &Region.AssaultPlacementBounds})
			for (const FBox& Box : *Bounds)
				if (!Box.IsValid || Box.Min.ContainsNaN() || Box.Max.ContainsNaN() || Box.Max.X <= Box.Min.X || Box.Max.Y <= Box.Min.Y)
					return TEXT("Encounter bounds require finite positive XY extents");
		if (Region.RegionId == TEXT("CentralGuardOnly") && !Region.AssaultMembers.IsEmpty())
			return TEXT("CentralGuardOnly must not have Assault members");
		if (Region.RegionId != TEXT("CentralGuardOnly") && Region.AssaultMembers.IsEmpty())
			return FString::Printf(TEXT("%s requires a non-empty dormant Assault membership"), *Region.RegionId.ToString());
		TSet<const AActor*> LocalMembers;
		for (const AActor* Member : Region.AssaultMembers)
		{
			const FString Error = Register(Member, Region.RegionId);
			if (!Error.IsEmpty()) return Error;
			const auto* Behavior = Member->FindComponentByClass<ULSEnemyBehaviorComponent>();
			if (LocalMembers.Contains(Member) || !Behavior || Behavior->Role != ELSEnemyRole::Assault || !Behavior->bStartRegionallyDormant)
				return TEXT("Region Assault members must be unique and configured for regional dormancy");
			if (!ContainsXY(Region.PlacementBounds, PositionOf(Member)) ||
				(!Region.AssaultPlacementBounds.IsEmpty() && !ContainsXY(Region.AssaultPlacementBounds, PositionOf(Member))))
				return FString::Printf(TEXT("%s lies outside its regional Assault placement bounds"), *Member->GetPathName());
			LocalMembers.Add(Member);
		}
		TSet<const AActor*> LocalTargets;
		const bool bFrontPreset = Transforms && (Region.RegionId == TEXT("L1") || Region.RegionId == TEXT("C1") || Region.RegionId == TEXT("R1"));
		for (const AActor* Target : Region.AlarmTargets)
		{
			const FString Error = Register(Target, Region.RegionId);
			if (!Error.IsEmpty()) return Error;
			if (LocalTargets.Contains(Target) || !Target->FindComponentByClass<ULSHealthComponent>()) return TEXT("Alarm targets require unique health-bearing actors");
			if (!ContainsXY(Region.PlacementBounds, PositionOf(Target))) return FString::Printf(TEXT("%s lies outside its regional placement bounds"), *Target->GetPathName());
			const FTransform* Placement = Transforms ? Transforms->Find(Target->GetFName()) : nullptr;
			const FTransform Proposed = Placement ? *Placement : Target->GetActorTransform();
			if (bFrontPreset)
				if (const auto* Turret = Cast<ALSTurret>(Target))
				{
					const UPrimitiveComponent* Body = Turret->GetObstacleBody();
					// 권위 있는 루트 몸체에 제안된 회전·크기를 적용해 입구 서쪽 외곽을 검사한다.
					if (!Body || Body != Target->GetRootComponent() || Body->CalcBounds(Proposed).GetBox().Min.X < 4200.0)
						return FString::Printf(TEXT("%s front turret body must remain east of the mouth"), *Target->GetPathName());
				}
			if (const auto* Behavior = Target->FindComponentByClass<ULSEnemyBehaviorComponent>();
				Behavior && Behavior->Role == ELSEnemyRole::Guard && !Target->IsA<ALSOutpost>())
			{
				if (bFrontPreset)
					if (const auto* Unit = Cast<ALSUnit>(Target))
					{
						const auto& Profiles = GetDefault<ULSBattlefieldNavigationSystem>()->GetSupportedAgents();
						const FNavDataConfig* Troops = Profiles.FindByPredicate([](const FNavDataConfig& Profile) { return Profile.Name == TEXT("Troops"); });
						const FVector Scale = Proposed.GetScale3D();
						const double BodyRadius = Unit->GetCapsuleComponent()->GetUnscaledCapsuleRadius() * FMath::Min(Scale.X, Scale.Y);
						if (!Troops || Proposed.GetLocation().X < 4200.0 + Troops->AgentRadius + BodyRadius)
							return FString::Printf(TEXT("%s front Guard must retain troop-navigation and body clearance east of the mouth"), *Target->GetPathName());
					}
				const AActor* Anchor = Behavior->GuardAnchor ? Behavior->GuardAnchor.Get() : static_cast<AActor*>(Behavior->GuardedOutpost.Get());
				const float Radius = Behavior->GuardAnchor ? Behavior->GuardAnchorRadius : Behavior->GuardedOutpost ? Behavior->GuardedOutpost->GetDefenseRadius() : 0;
				if (!IsValid(Anchor) || Anchor->GetWorld() != GetWorld() || FVector::Dist2D(PositionOf(Target), PositionOf(Anchor)) > Radius)
					return FString::Printf(TEXT("%s guard placement lies outside its assigned defense radius"), *Target->GetPathName());
			}
			LocalTargets.Add(Target);
		}
		TSet<const AActor*> LocalMines;
		for (const ALSMine* Mine : Region.Mines)
		{
			const FString Error = Register(Mine, Region.RegionId);
			if (!Error.IsEmpty()) return Error;
			if (LocalMines.Contains(Mine)) return TEXT("Region mines must be unique");
			if (!ContainsXY(Region.PlacementBounds, PositionOf(Mine))) return FString::Printf(TEXT("%s lies outside its regional placement bounds"), *Mine->GetPathName());
			LocalMines.Add(Mine);
		}
	}
	if (bCheckWorldMembership) for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const auto* Behavior = It->FindComponentByClass<ULSEnemyBehaviorComponent>();
		if (!Behavior || !Behavior->bStartRegionallyDormant) continue;
		int32 MembershipCount = 0;
		for (TActorIterator<ALSBattlefieldEncounterCoordinator> Coordinator(GetWorld()); Coordinator; ++Coordinator)
			for (const FLSEncounterRegion& Region : Coordinator->Regions)
				for (const AActor* Member : Region.AssaultMembers)
					if (Member == *It) ++MembershipCount;
		if (MembershipCount != 1) return FString::Printf(TEXT("Dormant Assault %s requires exactly one region membership; found %d"), *It->GetPathName(), MembershipCount);
	}
	return FString();
}

FString ALSBattlefieldEncounterCoordinator::GetConfigurationError() const { return Validate(nullptr); }

bool ALSBattlefieldEncounterCoordinator::ValidatePresetTransforms(const TMap<FName, FTransform>& Transforms, FString& OutError) const
{
	OutError = Validate(&Transforms);
	return OutError.IsEmpty();
}

void ALSBattlefieldEncounterCoordinator::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// 배치된 액터는 어느 BeginPlay에서 지뢰가 폭발하든 수신할 수 있어야 한다.
	RegisterListeners();
}

void ALSBattlefieldEncounterCoordinator::BeginPlay()
{
	Super::BeginPlay();
	const FString Error = GetConfigurationError();
	bConfigurationValid = Error.IsEmpty();
	if (!bConfigurationValid) UE_LOG(LogTemp, Error, TEXT("Encounter %s: %s"), *GetPathName(), *Error);
	// 일반 SpawnActor 이후 첫 플레이 전에 지정한 참조도 연결한다.
	RegisterListeners();
}

void ALSBattlefieldEncounterCoordinator::RegisterListeners()
{
	for (const FLSEncounterRegion& Region : Regions)
	{
		TArray<TObjectPtr<AActor>> Targets = Region.AlarmTargets;
		Targets.Append(Region.AssaultMembers);
		for (ALSMine* Mine : Region.Mines) Targets.AddUnique(Mine);
		for (AActor* Target : Targets)
		{
			if (!IsValid(Target) || Target->GetWorld() != GetWorld()) continue;
			if (auto* Health = Target->FindComponentByClass<ULSHealthComponent>())
			{
				DamageRegions.Add(Health->GetDamageReceiverId(), Region.RegionId);
				if (!BoundHealth.Contains(Health))
				{
					Health->OnDamageCommitted.AddUObject(this, &ThisClass::OnDamageCommitted);
					BoundHealth.Add(Health);
				}
			}
		}
		for (ALSMine* Mine : Region.Mines)
		{
			if (!IsValid(Mine) || Mine->GetWorld() != GetWorld()) continue;
			MineRegions.Add(Mine->GetUniqueID(), Region.RegionId);
			if (!BoundMines.Contains(Mine))
			{
				Mine->OnMineExploded.AddUObject(this, &ThisClass::OnMineExploded);
				BoundMines.Add(Mine);
			}
		}
	}
}

void ALSBattlefieldEncounterCoordinator::UnregisterListeners()
{
	for (auto& Health : BoundHealth) if (Health.IsValid()) Health->OnDamageCommitted.RemoveAll(this);
	for (auto& Mine : BoundMines) if (Mine.IsValid()) Mine->OnMineExploded.RemoveAll(this);
	BoundHealth.Reset();
	BoundMines.Reset();
}

void ALSBattlefieldEncounterCoordinator::EndPlay(const EEndPlayReason::Type Reason)
{
	bConfigurationValid = false;
	UnregisterListeners();
	Super::EndPlay(Reason);
}

void ALSBattlefieldEncounterCoordinator::OnDamageCommitted(const FLSDamageResult& Damage)
{
	if (Damage.AppliedDamage <= 0 || !Damage.bHasSourceFaction || Damage.SourceFaction != ELSFaction::Ally) return;
	if (const FName* Region = DamageRegions.Find(Damage.ReceiverId)) ActivateRegion(*Region);
}

void ALSBattlefieldEncounterCoordinator::OnMineExploded(const FLSMineExplosion& Explosion)
{
	if (const FName* Region = MineRegions.Find(Explosion.MineId)) ActivateRegion(*Region);
}

void ALSBattlefieldEncounterCoordinator::ActivateRegion(FName RegionId)
{
	if (!bConfigurationValid || AreActionsStopped() || ActivatedRegions.Contains(RegionId)) return;
	ActivatedRegions.Add(RegionId);
	for (const FLSEncounterRegion& Region : Regions)
		if (Region.RegionId == RegionId)
			for (AActor* Member : Region.AssaultMembers)
				if (IsValid(Member))
					if (auto* Behavior = Member->FindComponentByClass<ULSEnemyBehaviorComponent>()) Behavior->ActivateRegion();
}

void ALSBattlefieldEncounterCoordinator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bConfigurationValid || AreActionsStopped()) return;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		const auto* Faction = Cast<ILSFactionInterface>(Actor);
		const auto* Unit = Cast<ALSUnit>(Actor);
		if (!Faction || Faction->GetFaction() != ELSFaction::Ally || !Actor->GetRootComponent() ||
			(Unit && Unit->IsGameplayStaged()) || !ULSCombatTargetLibrary::IsCombatAlive(Actor)) continue;
		const FVector Position = Actor->GetRootComponent()->GetComponentLocation();
		for (const FLSEncounterRegion& Region : Regions)
		{
			if (ActivatedRegions.Contains(Region.RegionId)) continue;
			for (const FBox& Box : Region.EntryBounds)
				if (Position.X >= Box.Min.X && Position.X <= Box.Max.X && Position.Y >= Box.Min.Y && Position.Y <= Box.Max.Y)
				{
					ActivateRegion(Region.RegionId);
					break;
				}
		}
	}
}

#if WITH_EDITOR
EDataValidationResult ALSBattlefieldEncounterCoordinator::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Parent = Super::IsDataValid(Context);
	if (IsTemplate()) return Parent;
	const FString Error = GetConfigurationError();
	if (!Error.IsEmpty()) { Context.AddError(FText::FromString(Error)); return EDataValidationResult::Invalid; }
	return Parent == EDataValidationResult::Invalid ? Parent : EDataValidationResult::Valid;
}
#endif
