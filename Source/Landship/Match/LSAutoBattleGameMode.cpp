#include "Match/LSAutoBattleGameMode.h"
#include "Match/LSAutoBattlePreset.h"
#include "Troops/LSTroopCombatComponent.h"
#include "Controls/LSAutoBattleController.h"
#include "Controls/LSFreeCameraPawn.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Castle/LSMobileCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Castle/LSCastleProjectile.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Misc/ScopeExit.h"

ALSAutoBattleGameMode::ALSAutoBattleGameMode()
{
	PlayerControllerClass = ALSAutoBattleController::StaticClass();
	DefaultPawnClass = ALSFreeCameraPawn::StaticClass();
}

void ALSAutoBattleGameMode::StartPlay()
{
	Super::StartPlay();
	if (!FMath::IsFinite(DecisionInterval) || DecisionInterval < 0.05f || !FMath::IsFinite(RepathInterval) ||
	    RepathInterval < 0.05f || !FMath::IsFinite(RepathDistance) || RepathDistance <= 0 ||
	    !FMath::IsFinite(ApproachPadding) || ApproachPadding < 0 || !MeleeApproach.IsValid())
	{
		Fail(TEXT("자동 판단 주기 또는 접근 설정이 잘못되었습니다."));
		return;
	}
	for (TActorIterator<ALSAutoBattleScenario> It(GetWorld()); It; ++It)
	{
		if (Scenario)
		{
			Fail(TEXT("시나리오는 전용 맵에 하나만 있어야 합니다."));
			return;
		}
		Scenario = *It;
	}
	if (!Scenario)
	{
		Fail(TEXT("자동 전투 시나리오가 없습니다."));
		return;
	}
	const FString Error = Scenario->GetConfigurationError();
	if (!Error.IsEmpty())
	{
		Fail(Error);
		return;
	}
	CastleGround = Scenario->AllyCastleGround;
	NavigationDeadline = GetWorld()->GetTimeSeconds() + 15;
	SpawnHandle = GetWorld()->AddOnActorSpawnedHandler(
	    FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::OnActorSpawned));
	OutcomeHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ThisClass::CollectOutcome);
	GetWorldTimerManager().SetTimer(InitializationTimer, this, &ThisClass::InitializeScenario, 0.2f, true);
}

void ALSAutoBattleGameMode::InitializeScenario()
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Location;
	if (!Nav || Nav->IsNavigationBuildInProgress() ||
	    (!bHasPreparedFormation && !Nav->ProjectPointToNavigation(CastleGround, Location, FVector(100, 100, 300))))
	{
		if (GetWorld()->GetTimeSeconds() > NavigationDeadline)
		{
			GetWorldTimerManager().ClearTimer(InitializationTimer);
			Fail(TEXT("내비게이션 준비에 실패했습니다. NavMesh 범위를 확인하세요."));
		}
		return;
	}
	GetWorldTimerManager().ClearTimer(InitializationTimer);
	const TArray<FLSAutoBattlePlacement> Snapshot = Formation;
	Rebuild(Snapshot, bHasPreparedFormation);
}

void ALSAutoBattleGameMode::Fail(const FString& Reason)
{
	Failure = Reason;
	Phase = ELSAutoBattlePhase::Error;
	GetWorldTimerManager().ClearTimer(DecisionTimer);
	OnChanged.Broadcast();
	UE_LOG(LogTemp, Error, TEXT("AUTO_BATTLE: %s"), *Reason);
}

bool ALSAutoBattleGameMode::ResolveGround(const FVector& Ground, float Radius, float Height, const AActor* Ignore,
                                          bool bDeployment, FVector& OutGround, bool bRestoreValidatedGround) const
{
	if (!Scenario || Ground.ContainsNaN())
	{
		return false;
	}
	const FVector2D Min = bDeployment ? Scenario->DeploymentMin : Scenario->BoundsMin;
	const FVector2D Max = bDeployment ? Scenario->DeploymentMax : Scenario->BoundsMax;
	if (!FBox2D(Min + FVector2D(Radius), Max - FVector2D(Radius)).IsInsideOrOn(FVector2D(Ground)))
	{
		return false;
	}
	FNavLocation Projected;
	if (bRestoreValidatedGround)
	{
		// 복원은 검증·저장된 좌표를 유지한다. 재투영은 내비게이션 검색 결과에 따라 편성을 바꾸거나 거부할 수 있다.
		// 지면이 바뀌거나 점유된 경우는 그대로 거부하며, 새 배치와 프리셋 입력은 내비게이션 검증을 거친다.
		FHitResult Support;
		FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(LSAutoBattleRestoreGround), false);
		if (!GetWorld()->LineTraceSingleByObjectType(Support, Ground + FVector(0, 0, 200), Ground - FVector(0, 0, 200),
		                                             FCollisionObjectQueryParams(LSCollisionChannels::Ground),
		                                             GroundQuery) ||
		    FMath::Abs(Support.ImpactPoint.Z - Ground.Z) > 25 ||
		    Support.ImpactNormal.Z < GetDefault<UCharacterMovementComponent>()->GetWalkableFloorZ())
		{
			return false;
		}
		Projected.Location = Ground;
	}
	else
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (!Nav || !Nav->ProjectPointToNavigation(Ground, Projected, FVector(30, 30, 200)) ||
		    FVector::Dist2D(Ground, Projected.Location) > 25)
		{
			UE_LOG(LogTemp, Verbose, TEXT("AUTO_PLACE_NAV requested=%s projected=%s"), *Ground.ToString(),
			       *Projected.Location.ToString());
			return false;
		}
	}
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Unit);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::Building);
	Objects.AddObjectTypesToQuery(LSCollisionChannels::PathObstacle);
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSAutoBattlePlacement), false, Ignore);
	for (const auto& Ignored : PlacementIgnoredActors)
	{
		if (Ignored.IsValid())
		{
			Query.AddIgnoredActor(Ignored.Get());
		}
	}
	if (GetWorld()->OverlapAnyTestByObjectType(Projected.Location + FVector(0, 0, Height + 4), FQuat::Identity, Objects,
	                                           FCollisionShape::MakeCapsule(Radius + 2, Height), Query))
	{
		if (UE_LOG_ACTIVE(LogTemp, Verbose))
		{
			TArray<FOverlapResult> Hits;
			GetWorld()->OverlapMultiByObjectType(Hits, Projected.Location + FVector(0, 0, Height + 4), FQuat::Identity,
			                                     Objects, FCollisionShape::MakeCapsule(Radius + 2, Height), Query);
			for (const FOverlapResult& Hit : Hits)
			{
				UE_LOG(LogTemp, Verbose, TEXT("AUTO_PLACE_BLOCKED ground=%s actor=%s"), *Projected.Location.ToString(),
				       *GetNameSafe(Hit.GetActor()));
			}
		}
		return false;
	}
	OutGround = Projected.Location;
	return true;
}

bool ALSAutoBattleGameMode::CanPlace(ELSTroopType Type, const FVector& Ground, int32 IgnoreId, FVector& OutGround,
                                     ELSFaction Faction) const
{
	if (static_cast<uint8>(Type) > 2 || (Faction != ELSFaction::Ally && Faction != ELSFaction::Enemy))
	{
		return false;
	}
	return ResolveGround(Ground, Type == ELSTroopType::Cavalry ? 65 : 42, 88, GetPlacementActor(IgnoreId),
	                     Faction == ELSFaction::Ally, OutGround);
}

bool ALSAutoBattleGameMode::CanPlaceCastle(const FVector& Ground, FVector& OutGround, ELSFaction Faction) const
{
	if (Faction != ELSFaction::Ally && Faction != ELSFaction::Enemy)
	{
		return false;
	}
	return ResolveGround(Ground, 142, 142, Faction == ELSFaction::Ally ? AllyCastle : EnemyCastle,
	                     Faction == ELSFaction::Ally, OutGround);
}

void ALSAutoBattleGameMode::Freeze(ALSUnit* Unit)
{
	Unit->Orders->SetComponentTickEnabled(false);
	Unit->Selection->SetPlayerControlled(false);
	Unit->BasicAttack->Deactivate();
	if (ALSMobileCastle* Castle = Cast<ALSMobileCastle>(Unit))
	{
		Castle->Weapons->Deactivate();
	}
	if (AAIController* AI = Cast<AAIController>(Unit->GetController()))
	{
		Controllers.AddUnique(AI);
		AI->StopMovement();
	}
	Unit->GetCharacterMovement()->StopMovementImmediately();
	Unit->GetCharacterMovement()->DisableMovement();
}

ALSUnit* ALSAutoBattleGameMode::SpawnUnit(FLSAutoBattlePlacement& Placement, ELSFaction Faction,
                                          bool bRestoreValidatedGround)
{
	const float Radius = Placement.Type == ELSTroopType::Cavalry ? 65 : 42;
	FVector Ground;
	if (!ResolveGround(Placement.Position, Radius, 88, nullptr, Faction == ELSFaction::Ally, Ground,
	                   bRestoreValidatedGround))
	{
		UE_LOG(LogTemp, Warning, TEXT("AUTO_RESTORE_PLACEMENT id=%d type=%d faction=%d ground=%s"), Placement.Id,
		       static_cast<int32>(Placement.Type), static_cast<int32>(Faction), *Placement.Position.ToString());
		return nullptr;
	}
	const FTransform Transform(FRotator(0, Faction == ELSFaction::Ally ? 0 : 180, 0), Ground + FVector(0, 0, 90));
	ALSTroopUnit* Unit = GetWorld()->SpawnActorDeferred<ALSTroopUnit>(
	    ALSTroopUnit::StaticClass(), Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Unit)
	{
		return nullptr;
	}
	Unit->SetInitialTroopType(Placement.Type);
	Unit->SetInitialFaction(Faction);
	Unit->Orders->SetComponentTickEnabled(false);
	Unit->FinishSpawning(Transform);
	if (!ULSCombatTargetLibrary::IsCombatAlive(Unit))
	{
		Unit->Destroy();
		return nullptr;
	}
	Placement.Position = Ground;
	Freeze(Unit);
	Units.Add(Unit);
	return Unit;
}

ALSMobileCastle* ALSAutoBattleGameMode::SpawnCastle(const FVector& Ground, ELSFaction Faction,
                                                    bool bRestoreValidatedGround)
{
	FVector Projected;
	if (!ResolveGround(Ground, 142, 142, nullptr, Faction == ELSFaction::Ally, Projected, bRestoreValidatedGround))
	{
		return nullptr;
	}
	const FTransform Transform(FRotator::ZeroRotator, Projected + FVector(0, 0, 144));
	ALSMobileCastle* Castle = GetWorld()->SpawnActorDeferred<ALSMobileCastle>(
	    ALSMobileCastle::StaticClass(), Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Castle)
	{
		return nullptr;
	}
	Castle->SetInitialFaction(Faction);
	Castle->Orders->SetComponentTickEnabled(false);
	Castle->FinishSpawning(Transform);
	Castle->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	if (!ULSCombatTargetLibrary::IsCombatAlive(Castle))
	{
		Castle->Destroy();
		return nullptr;
	}
	Freeze(Castle);
	Units.Add(Castle);
	return Castle;
}

bool ALSAutoBattleGameMode::Rebuild(const TArray<FLSAutoBattlePlacement>& NewFormation, bool bRestoreValidatedGround)
{
	TGuardValue<bool> Guard(bChanging, true);
	DestroyBattle();
	++BattleId;
	Phase = ELSAutoBattlePhase::Loading;
	RemainingBudget = Scenario->Budget;
	Formation = NewFormation;
	AllyCastle = SpawnCastle(CastleGround, ELSFaction::Ally, bRestoreValidatedGround);
	EnemyCastle = SpawnCastle(Scenario->EnemyCastleGround, ELSFaction::Enemy, bRestoreValidatedGround);
	if (!AllyCastle || !EnemyCastle)
	{
		DestroyBattle();
		Fail(TEXT("성 생성 또는 배치 검증에 실패했습니다."));
		return false;
	}
	CastleGround = AllyCastle->GetActorLocation() - FVector(0, 0, 144);
	Scenario->EnemyCastleGround = EnemyCastle->GetActorLocation() - FVector(0, 0, 144);
	for (FLSAutoBattlePlacement& Placement : Scenario->EnemyFormation)
	{
		if (Placement.Id >= 0)
		{
			Placement.Id = NextEnemyId--;
		}
		ALSUnit* Enemy = SpawnUnit(Placement, ELSFaction::Enemy, bRestoreValidatedGround);
		if (Enemy)
		{
			PlacementActors.Add(Placement.Id, Enemy);
		}
		if (!Enemy)
		{
			DestroyBattle();
			Fail(TEXT("적 편성 생성에 실패했습니다. 지면·겹침·수치를 확인하세요."));
			return false;
		}
	}
	for (FLSAutoBattlePlacement& Placement : Formation)
	{
		ALSUnit* Unit = SpawnUnit(Placement, ELSFaction::Ally, bRestoreValidatedGround);
		if (!Unit)
		{
			DestroyBattle();
			Fail(TEXT("아군 편성 복원에 실패했습니다."));
			return false;
		}
		PlacementActors.Add(Placement.Id, Unit);
		RemainingBudget -= Scenario->UnitCosts.FindChecked(Placement.Type);
	}
	bHasPreparedFormation = true;
	Failure.Reset();
	Phase = ELSAutoBattlePhase::Preparation;
	OnChanged.Broadcast();
	return true;
}

ALSUnit* ALSAutoBattleGameMode::GetPlacementActor(int32 Id) const
{
	const TObjectPtr<ALSUnit>* Actor = PlacementActors.Find(Id);
	return Actor && IsValid(*Actor) ? Actor->Get() : nullptr;
}

int32 ALSAutoBattleGameMode::FindPlacementId(const AActor* Actor) const
{
	for (const auto& Pair : PlacementActors)
	{
		if (Pair.Value == Actor)
		{
			return Pair.Key;
		}
	}
	return 0;
}

ELSAutoBattleRequest ALSAutoBattleGameMode::PlaceUnit(ELSTroopType Type, const FVector& Ground, int32& OutId,
                                                      ELSFaction Faction)
{
	if (bChanging)
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	if (Phase != ELSAutoBattlePhase::Preparation)
	{
		return ELSAutoBattleRequest::WrongPhase;
	}
	const int32* Cost = Scenario->UnitCosts.Find(Type);
	if (!Cost || (Faction != ELSFaction::Ally && Faction != ELSFaction::Enemy) || NextPlacementId == MAX_int32 ||
	    NextEnemyId == MIN_int32)
	{
		return ELSAutoBattleRequest::InvalidData;
	}
	if (Faction == ELSFaction::Ally && RemainingBudget < *Cost)
	{
		return ELSAutoBattleRequest::InsufficientBudget;
	}
	if ((Faction == ELSFaction::Ally && Formation.Num() >= Scenario->PopulationLimit) ||
	    (Faction == ELSFaction::Enemy && Scenario->EnemyFormation.Num() >= 100))
	{
		return ELSAutoBattleRequest::PopulationFull;
	}
	FVector Position;
	if (!CanPlace(Type, Ground, 0, Position, Faction))
	{
		return ELSAutoBattleRequest::InvalidPlacement;
	}
	TGuardValue<bool> Guard(bChanging, true);
	FLSAutoBattlePlacement Placement;
	Placement.Id = Faction == ELSFaction::Ally ? NextPlacementId : NextEnemyId;
	Placement.Type = Type;
	Placement.Position = Position;
	ALSUnit* Unit = SpawnUnit(Placement, Faction);
	if (!Unit)
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	if (Faction == ELSFaction::Ally)
	{
		++NextPlacementId;
	}
	else
	{
		--NextEnemyId;
	}
	(Faction == ELSFaction::Ally ? Formation : Scenario->EnemyFormation).Add(Placement);
	PlacementActors.Add(Placement.Id, Unit);
	if (Faction == ELSFaction::Ally)
	{
		RemainingBudget -= *Cost;
	}
	OutId = Placement.Id;
	OnChanged.Broadcast();
	return ELSAutoBattleRequest::Success;
}

ELSAutoBattleRequest ALSAutoBattleGameMode::MoveUnit(int32 Id, const FVector& Ground)
{
	if (bChanging)
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	if (Phase != ELSAutoBattlePhase::Preparation)
	{
		return ELSAutoBattleRequest::WrongPhase;
	}
	FLSAutoBattlePlacement* Placement =
	    (Id < 0 ? Scenario->EnemyFormation : Formation)
	        .FindByPredicate([Id](const FLSAutoBattlePlacement& P) { return P.Id == Id; });
	ALSUnit* Unit = GetPlacementActor(Id);
	if (!Placement || !Unit)
	{
		return ELSAutoBattleRequest::InvalidTarget;
	}
	FVector Position;
	if (!CanPlace(Placement->Type, Ground, Id, Position, Id < 0 ? ELSFaction::Enemy : ELSFaction::Ally))
	{
		return ELSAutoBattleRequest::InvalidPlacement;
	}
	TGuardValue<bool> Guard(bChanging, true);
	if (!Unit->SetActorLocation(Position + FVector(0, 0, 90), false, nullptr, ETeleportType::TeleportPhysics))
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	Placement->Position = Position;
	OnChanged.Broadcast();
	return ELSAutoBattleRequest::Success;
}

ELSAutoBattleRequest ALSAutoBattleGameMode::RemoveUnit(int32 Id)
{
	if (bChanging)
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	if (Phase != ELSAutoBattlePhase::Preparation)
	{
		return ELSAutoBattleRequest::WrongPhase;
	}
	auto& EditingFormation = Id < 0 ? Scenario->EnemyFormation : Formation;
	const int32 Index =
	    EditingFormation.IndexOfByPredicate([Id](const FLSAutoBattlePlacement& P) { return P.Id == Id; });
	ALSUnit* Unit = GetPlacementActor(Id);
	if (Index == INDEX_NONE || !Unit)
	{
		return ELSAutoBattleRequest::InvalidTarget;
	}
	TGuardValue<bool> Guard(bChanging, true);
	if (Id > 0)
	{
		RemainingBudget += Scenario->UnitCosts.FindChecked(EditingFormation[Index].Type);
	}
	EditingFormation.RemoveAt(Index);
	PlacementActors.Remove(Id);
	Units.Remove(Unit);
	AAIController* AI = Cast<AAIController>(Unit->GetController());
	Unit->Destroy();
	if (IsValid(AI))
	{
		AI->Destroy();
	}
	Controllers.RemoveAll([](const TWeakObjectPtr<AAIController>& Controller) { return !Controller.IsValid(); });
	OnChanged.Broadcast();
	return ELSAutoBattleRequest::Success;
}

ELSAutoBattleRequest ALSAutoBattleGameMode::ResetFormation(ELSFaction Faction)
{
	if (bChanging)
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	if (Phase != ELSAutoBattlePhase::Preparation)
	{
		return ELSAutoBattleRequest::WrongPhase;
	}
	if (Faction != ELSFaction::Ally && Faction != ELSFaction::Enemy)
	{
		return ELSAutoBattleRequest::InvalidData;
	}
	TArray<int32> Ids;
	for (const auto& P : Faction == ELSFaction::Ally ? Formation : Scenario->EnemyFormation)
	{
		Ids.Add(P.Id);
	}
	for (int32 Id : Ids)
	{
		if (RemoveUnit(Id) != ELSAutoBattleRequest::Success)
		{
			return ELSAutoBattleRequest::Unavailable;
		}
	}
	return ELSAutoBattleRequest::Success;
}

ELSAutoBattleRequest ALSAutoBattleGameMode::MoveCastle(const FVector& Ground, ELSFaction Faction)
{
	if (bChanging)
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	if (Phase != ELSAutoBattlePhase::Preparation)
	{
		return ELSAutoBattleRequest::WrongPhase;
	}
	ALSMobileCastle* EditingCastle = Faction == ELSFaction::Ally ? AllyCastle : EnemyCastle;
	if (!IsValid(EditingCastle) || (Faction != ELSFaction::Ally && Faction != ELSFaction::Enemy))
	{
		return ELSAutoBattleRequest::InvalidTarget;
	}
	FVector Position;
	if (!ResolveGround(Ground, 142, 142, EditingCastle, Faction == ELSFaction::Ally, Position))
	{
		return ELSAutoBattleRequest::InvalidPlacement;
	}
	TGuardValue<bool> Guard(bChanging, true);
	if (!EditingCastle->SetActorLocation(Position + FVector(0, 0, 144), false, nullptr, ETeleportType::TeleportPhysics))
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	if (Faction == ELSFaction::Ally)
	{
		CastleGround = Position;
	}
	else
	{
		Scenario->EnemyCastleGround = Position;
	}
	OnChanged.Broadcast();
	return ELSAutoBattleRequest::Success;
}

ELSAutoBattleRequest ALSAutoBattleGameMode::SelectWeapon(ELSCastleWeapon Weapon, ELSFaction Faction)
{
	if (bChanging)
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	if (Phase != ELSAutoBattlePhase::Preparation)
	{
		return ELSAutoBattleRequest::WrongPhase;
	}
	ALSMobileCastle* EditingCastle = Faction == ELSFaction::Ally ? AllyCastle : EnemyCastle;
	if ((Faction != ELSFaction::Ally && Faction != ELSFaction::Enemy) || !IsValid(EditingCastle))
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	FLSCastleWeaponSettings Settings;
	if (static_cast<uint8>(Weapon) > 2 || !EditingCastle->Weapons->GetResolvedSettings(Weapon, Settings))
	{
		return ELSAutoBattleRequest::InvalidData;
	}
	TGuardValue<bool> Guard(bChanging, true);
	if (Faction == ELSFaction::Ally)
	{
		SelectedWeapon = Weapon;
	}
	else
	{
		Scenario->EnemyWeapon = Weapon;
	}
	OnChanged.Broadcast();
	return ELSAutoBattleRequest::Success;
}

ELSAutoBattleRequest ALSAutoBattleGameMode::StartBattle()
{
	if (bChanging)
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	if (Phase != ELSAutoBattlePhase::Preparation)
	{
		return ELSAutoBattleRequest::WrongPhase;
	}
	if (Formation.IsEmpty())
	{
		return ELSAutoBattleRequest::InvalidData;
	}
	for (ALSUnit* Unit : Units)
	{
		if (!ULSCombatTargetLibrary::IsCombatAlive(Unit) ||
		    (Unit->SupportsBasicAttack() && !Unit->BasicAttack->IsConfigured()))
		{
			return ELSAutoBattleRequest::Unavailable;
		}
	}
	for (ALSUnit* Unit : Units)
	{
		if (ALSTroopUnit* Troop = Cast<ALSTroopUnit>(Unit))
		{
			if (!Troop->CombatBehavior || !Troop->CombatBehavior->IsConfigurationValid())
			{
				return ELSAutoBattleRequest::InvalidData;
			}
		}
	}
	FLSCastleWeaponSettings Settings;
	if (!AllyCastle->Weapons->GetResolvedSettings(SelectedWeapon, Settings) ||
	    !EnemyCastle->Weapons->GetResolvedSettings(Scenario->EnemyWeapon, Settings))
	{
		return ELSAutoBattleRequest::InvalidData;
	}
	TGuardValue<bool> Guard(bChanging, true);
	Agents.Reset();
	MeleePlanner.Reset();
	for (ALSUnit* Unit : Units)
	{
		if (ALSTroopUnit* Troop = Cast<ALSTroopUnit>(Unit))
		{
			Troop->CombatBehavior->SetBattleEnabled(true);
		}
		if (Unit->SupportsBasicAttack())
		{
			Unit->BasicAttack->Activate();
		}
		if (ALSMobileCastle* Castle = Cast<ALSMobileCastle>(Unit))
		{
			Castle->Weapons->Activate();
		}
		Unit->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Unit->GetCharacterMovement()->SetAvoidanceEnabled(false);
		FLSAutoBattleAgent Agent;
		Agent.Unit = Unit;
		Agents.Add(Agent);
	}
	Phase = ELSAutoBattlePhase::Fighting;
	GetWorldTimerManager().SetTimer(DecisionTimer, this, &ThisClass::AutoTick, DecisionInterval, true, 0.001f);
	OnChanged.Broadcast();
	return ELSAutoBattleRequest::Success;
}

ELSAutoBattleRequest ALSAutoBattleGameMode::RetryBattle(bool bKeepFormation)
{
	if (bChanging)
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	if (Phase != ELSAutoBattlePhase::Victory && Phase != ELSAutoBattlePhase::Defeat)
	{
		return ELSAutoBattleRequest::WrongPhase;
	}
	TGuardValue<bool> Guard(bChanging, true);
	const TArray<FLSAutoBattlePlacement> Snapshot = bKeepFormation ? Formation : TArray<FLSAutoBattlePlacement>();
	Phase = ELSAutoBattlePhase::Loading;
	DestroyBattle();
	Formation = Snapshot;
	RemainingBudget = Scenario->Budget;
	for (const FLSAutoBattlePlacement& Placement : Formation)
	{
		RemainingBudget -= Scenario->UnitCosts.FindChecked(Placement.Type);
	}
	// 파괴한 충돌 등록이 정리된 다음 타이머에서 편성을 복원한다.

	NavigationDeadline = GetWorld()->GetTimeSeconds() + 15;
	GetWorldTimerManager().SetTimer(InitializationTimer, this, &ThisClass::InitializeScenario, 0.2f, true);
	OnChanged.Broadcast();
	return ELSAutoBattleRequest::Success;
}

void ALSAutoBattleGameMode::UpdateAgent(FLSAutoBattleAgent& Agent, const TArray<ALSUnit*>& Opponents)
{
	ALSUnit* Unit = Agent.Unit.Get();
	if (!ULSCombatTargetLibrary::IsCombatAlive(Unit))
	{
		return;
	}
	if (!Unit->Orders->TryCompleteKnockback() || Unit->GetCharacterMovement()->IsFalling())
	{
		MeleePlanner.Release(Unit);
		return;
	}
	ALSUnit* Target = Agent.Target.Get();
	if (!ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Target))
	{
		Target = nullptr;
		double Best = TNumericLimits<double>::Max();
		for (ALSUnit* Candidate : Opponents)
		{
			const double Distance = ULSCombatTargetLibrary::GetAttackDistance(Unit, Candidate);
			if (Distance < Best)
			{
				Target = Candidate;
				Best = Distance;
			}
		}
		Agent.Target = Target;
		Agent.NextRepath = 0;
	}
	if (!Target)
	{
		Unit->BasicAttack->StopAttack();
		return;
	}
	AAIController* AI = Cast<AAIController>(Unit->GetController());
	if (!AI)
	{
		return;
	}
	FLSCombatDirective Directive;
	Directive.Target = Target;
	Directive.ApproachRange = Unit->BasicAttack->GetAttackSettings().Range;
	if (ALSTroopUnit* Troop = Cast<ALSTroopUnit>(Unit))
	{
		if (!Troop->CombatBehavior)
		{
			return;
		}
		Directive = Troop->CombatBehavior->EvaluateEngagement(Target, Opponents);
	}
	Target = Cast<ALSUnit>(Directive.Target);
	if (!ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Target))
	{
		Unit->BasicAttack->StopAttack();
		return;
	}
	const bool bMelee = !Unit->IsA<ALSMobileCastle>() && !Unit->BasicAttack->GetProjectileSettings().ProjectileClass;
	FLSMeleeApproachResult MeleePlan;
	if (bMelee && !Directive.bHoldPosition && !Directive.bSuppressBasicAttack)
	{
		MeleePlan = MeleePlanner.Plan(Unit, Target, Opponents, GetWorld()->GetTimeSeconds(), Directive.ApproachRange,
		                              MeleeApproach);
		if (MeleePlan.Target != Target)
		{
			Target = MeleePlan.Target;
			Agent.Target = Target;
			Agent.NextRepath = 0;
		}
		if (!MeleePlan.bCanApproach)
		{
			Unit->BasicAttack->StopAttack();
			if (AI->GetMoveStatus() == EPathFollowingStatus::Moving)
			{
				AI->StopMovement();
			}
			return;
		}
	}
	else
	{
		MeleePlanner.Release(Unit);
	}
	if (Directive.bHoldPosition)
	{
		AI->StopMovement();
		Unit->GetCharacterMovement()->StopMovementImmediately();
		if (!Directive.bSuppressBasicAttack &&
		    ULSCombatTargetLibrary::GetAttackDistance(Unit, Target) <= Directive.ApproachRange)
		{
			Unit->BasicAttack->StartAttack(Target);
		}
		else
		{
			Unit->BasicAttack->StopAttack();
		}
		return;
	}
	const ALSMobileCastle* Castle = Cast<ALSMobileCastle>(Unit);
	FLSCastleWeaponSettings WeaponSettings;
	if (Castle && !Castle->Weapons->GetResolvedSettings(Castle == AllyCastle ? SelectedWeapon : Scenario->EnemyWeapon,
	                                                    WeaponSettings))
	{
		return;
	}
	const double Range = Castle ? WeaponSettings.Range : Directive.ApproachRange;
	const double Distance = Castle ? FVector::Dist2D(Unit->GetActorLocation(), Target->GetActorLocation())
	                               : ULSCombatTargetLibrary::GetAttackDistance(Unit, Target);
	const bool bMoving = AI->GetMoveStatus() == EPathFollowingStatus::Moving;
	const double Padding = bMoving ? FMath::Min(static_cast<double>(ApproachPadding), Range * 0.1) : 0;
	if (Distance <= Range - Padding)
	{
		if (bMoving)
		{
			AI->StopMovement();
			Unit->GetCharacterMovement()->StopMovementImmediately();
		}
		if (!Castle && !Directive.bSuppressBasicAttack && Unit->BasicAttack->GetAttackTarget() != Target)
		{
			Unit->BasicAttack->StartAttack(Target);
		}
		return;
	}
	Unit->BasicAttack->StopAttack();
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < Agent.NextRepath)
	{
		return;
	}
	Agent.NextRepath = Now + RepathInterval;
	if (bMelee && MeleePlan.bCanApproach)
	{
		if (!bMoving || FVector::DistSquared(Agent.Goal, MeleePlan.Destination) >= FMath::Square(RepathDistance))
		{
			const auto Result = AI->MoveToLocation(MeleePlan.Destination, 5, false, true, false, false, nullptr, false);
			if (Result == EPathFollowingRequestResult::Failed)
			{
				MeleePlanner.RejectDestination(Unit, Now, MeleeApproach);
			}
			else
			{
				Agent.Goal = MeleePlan.Destination;
			}
		}
		return;
	}
	float Radius, Height, OtherRadius, OtherHeight;
	Unit->GetSimpleCollisionCylinder(Radius, Height);
	Target->GetSimpleCollisionCylinder(OtherRadius, OtherHeight);
	FVector Direction = (Unit->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = FVector::ForwardVector;
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav)
	{
		return;
	}
	for (int32 Index = 0; Index < 9; ++Index)
	{
		const float Angle = Index == 0 ? 0 : (Index % 2 ? 1 : -1) * ((Index + 1) / 2) * 30;
		const double GoalDistance =
		    Castle ? FMath::Max(Radius + OtherRadius + 5.0, Range * 0.75) : Radius + OtherRadius + Range * 0.65;
		const FVector Desired =
		    Target->GetActorLocation() + Direction.RotateAngleAxis(Angle, FVector::UpVector) * GoalDistance;
		FNavLocation Goal;
		if (!Nav->ProjectPointToNavigation(Desired, Goal, FVector(100, 100, 250)))
		{
			continue;
		}
		if (bMoving && FVector::DistSquared(Agent.Goal, Goal.Location) < FMath::Square(RepathDistance))
		{
			return;
		}
		const EPathFollowingRequestResult::Type Result =
		    AI->MoveToLocation(Goal.Location, 5, false, true, false, false, nullptr, false);
		if (Result != EPathFollowingRequestResult::Failed)
		{
			Agent.Goal = Goal.Location;
			return;
		}
	}
}

void ALSAutoBattleGameMode::FireCastle(ALSMobileCastle* Castle, ELSCastleWeapon Weapon,
                                       const TArray<ALSUnit*>& Opponents)
{
	if (!ULSCombatTargetLibrary::IsCombatAlive(Castle) ||
	    Castle->Weapons->GetStatus(Weapon) != ELSWeaponCastResult::Ready)
	{
		return;
	}
	FLSCastleWeaponSettings Settings;
	if (!Castle->Weapons->GetResolvedSettings(Weapon, Settings))
	{
		return;
	}
	ALSUnit* Best = nullptr;
	double BestScore = -TNumericLimits<double>::Max();
	for (ALSUnit* Candidate : Opponents)
	{
		const double Distance = FVector::Dist2D(Castle->GetActorLocation(), Candidate->GetActorLocation());
		if (Distance > Settings.Range)
		{
			continue;
		}
		double Score = -Distance;
		if (Weapon == ELSCastleWeapon::Ballista)
		{
			FLSHealthSnapshot Health;
			if (!Candidate->FindComponentByClass<ULSHealthComponent>()->TryGetHealth(Health))
			{
				continue;
			}
			Score = Health.CurrentHealth;
		}
		else if (Weapon == ELSCastleWeapon::Cannon)
		{
			Score = 0;
			for (ALSUnit* Nearby : Opponents)
			{
				Score += FVector::DistSquared2D(Candidate->GetActorLocation(), Nearby->GetActorLocation()) <=
				                 FMath::Square(Settings.Radius)
				             ? 1
				             : 0;
			}
		}
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Candidate;
		}
	}
	if (Best)
	{
		Castle->Weapons->CastWeapon(Weapon, Best->GetActorLocation());
	}
}

void ALSAutoBattleGameMode::AutoTick()
{
	if (Phase != ELSAutoBattlePhase::Fighting)
	{
		return;
	}
	MeleePlanner.Prune();
	TArray<ALSUnit*> Allies, Enemies;
	for (ALSUnit* Unit : Units)
	{
		if (ULSCombatTargetLibrary::IsCombatAlive(Unit))
		{
			(Unit->GetFaction() == ELSFaction::Ally ? Allies : Enemies).Add(Unit);
		}
	}
	for (FLSAutoBattleAgent& Agent : Agents)
	{
		if (Agent.Unit.IsValid())
		{
			UpdateAgent(Agent, Agent.Unit->GetFaction() == ELSFaction::Ally ? Enemies : Allies);
		}
	}
	FireCastle(AllyCastle, SelectedWeapon, Enemies);
	FireCastle(EnemyCastle, Scenario->EnemyWeapon, Allies);
}

void ALSAutoBattleGameMode::CollectOutcome(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld() || World->bIsTearingDown || Phase != ELSAutoBattlePhase::Fighting)
	{
		return;
	}
	bool bEnemiesAlive = false;
	for (ALSUnit* Unit : Units)
	{
		bEnemiesAlive |= ULSCombatTargetLibrary::IsCombatAlive(Unit) && Unit->GetFaction() == ELSFaction::Enemy;
	}
	if (ULSCombatTargetLibrary::IsCombatAlive(AllyCastle) && bEnemiesAlive)
	{
		return;
	}
	TGuardValue<bool> Guard(bChanging, true);
	Phase =
	    ULSCombatTargetLibrary::IsCombatAlive(AllyCastle) ? ELSAutoBattlePhase::Victory : ELSAutoBattlePhase::Defeat;
	GetWorldTimerManager().ClearTimer(DecisionTimer);
	for (ALSUnit* Unit : Units)
	{
		if (IsValid(Unit))
		{
			if (ALSTroopUnit* Troop = Cast<ALSTroopUnit>(Unit))
			{
				if (Troop->CombatBehavior)
				{
					Troop->CombatBehavior->SetBattleEnabled(false);
				}
			}
			Unit->EndBattleActions();
			if (ALSMobileCastle* Castle = Cast<ALSMobileCastle>(Unit))
			{
				Castle->Weapons->EndBattleActions();
			}
		}
	}
	OnChanged.Broadcast();
}

void ALSAutoBattleGameMode::OnActorSpawned(AActor* Actor)
{
	if (Phase == ELSAutoBattlePhase::Fighting &&
	    (Actor->IsA<ALSBasicProjectile>() || Actor->IsA<ALSCastleProjectile>()))
	{
		Projectiles.Add(Actor);
	}
}

void ALSAutoBattleGameMode::DestroyBattle()
{
	GetWorldTimerManager().ClearTimer(DecisionTimer);
	for (const TWeakObjectPtr<AActor>& Projectile : Projectiles)
	{
		if (Projectile.IsValid())
		{
			Projectile->Destroy();
		}
	}
	Projectiles.Reset();
	for (ALSUnit* Unit : Units)
	{
		if (IsValid(Unit))
		{
			Unit->Destroy();
		}
	}
	for (const TWeakObjectPtr<AAIController>& AI : Controllers)
	{
		if (AI.IsValid())
		{
			AI->Destroy();
		}
	}
	Controllers.Reset();
	Units.Reset();
	Agents.Reset();
	MeleePlanner.Reset();
	PlacementActors.Reset();
	AllyCastle = nullptr;
	EnemyCastle = nullptr;
}

void ALSAutoBattleGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(InitializationTimer);
	GetWorldTimerManager().ClearTimer(DecisionTimer);
	FWorldDelegates::OnWorldTickEnd.Remove(OutcomeHandle);
	GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
	Super::EndPlay(Reason);
}

ELSAutoBattleRequest ALSAutoBattleGameMode::SetPreparationLimits(int32 Budget, int32 Limit)
{
	if (bChanging)
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	if (Phase != ELSAutoBattlePhase::Preparation)
	{
		return ELSAutoBattleRequest::WrongPhase;
	}
	const int64 Spent = static_cast<int64>(Scenario->Budget) - RemainingBudget;
	if (Budget < Spent || Limit < 1 || Limit > 100 || Limit < Formation.Num())
	{
		return ELSAutoBattleRequest::InvalidData;
	}
	TGuardValue<bool> Guard(bChanging, true);
	Scenario->Budget = Budget;
	Scenario->PopulationLimit = Limit;
	RemainingBudget = Budget - static_cast<int32>(Spent);
	OnChanged.Broadcast();
	return ELSAutoBattleRequest::Success;
}

FLSAutoBattlePreset ALSAutoBattleGameMode::GetPreset() const
{
	FLSAutoBattlePreset P;
	if (!Scenario)
	{
		P.Version = 0;
		return P;
	}
	P.Budget = Scenario->Budget;
	P.PopulationLimit = Scenario->PopulationLimit;
	P.UnitCosts.Reset();
	for (int32 I = 0; I < 3; ++I)
	{
		P.UnitCosts.Add(Scenario->UnitCosts.FindRef(static_cast<ELSTroopType>(I)));
	}
	P.AllyCastle = CastleGround;
	P.EnemyCastle = Scenario->EnemyCastleGround;
	P.AllyWeapon = SelectedWeapon;
	P.EnemyWeapon = Scenario->EnemyWeapon;
	P.Allies = Formation;
	P.Enemies = Scenario->EnemyFormation;
	return P;
}

ELSAutoBattleRequest ALSAutoBattleGameMode::ApplyPreset(const FLSAutoBattlePreset& Input)
{
	if (bChanging)
	{
		return ELSAutoBattleRequest::Unavailable;
	}
	if (Phase != ELSAutoBattlePhase::Preparation)
	{
		return ELSAutoBattleRequest::WrongPhase;
	}
	if (!Input.IsValid())
	{
		return ELSAutoBattleRequest::InvalidData;
	}
	TGuardValue<bool> Guard(bChanging, true);
	FLSAutoBattlePreset P = Input;
	for (ALSUnit* Unit : Units)
	{
		PlacementIgnoredActors.Add(Unit);
	}
	ON_SCOPE_EXIT
	{
		PlacementIgnoredActors.Reset();
	};
	struct FLSPlacementBody
	{
		FVector Ground;
		float Radius;
		float Height;
	};
	TArray<FLSPlacementBody> Bodies;
	auto CheckBody = [this, &Bodies](FVector& Ground, float Radius, float Height, bool bAlly)
	{
		FVector Resolved;
		if (!ResolveGround(Ground, Radius, Height, nullptr, bAlly, Resolved))
		{
			return false;
		}
		for (const auto& Body : Bodies)
		{
			if (FMath::Abs(Resolved.Z + Height - Body.Ground.Z - Body.Height) < Height + Body.Height &&
			    FVector::DistSquared2D(Resolved, Body.Ground) < FMath::Square(Radius + Body.Radius + 4))
			{
				return false;
			}
		}
		Ground = Resolved;
		Bodies.Add({Ground, Radius, Height});
		return true;
	};
	if (!CheckBody(P.AllyCastle, 142, 142, true) || !CheckBody(P.EnemyCastle, 142, 142, false))
	{
		return ELSAutoBattleRequest::InvalidPlacement;
	}
	for (auto& Unit : P.Allies)
	{
		if (!CheckBody(Unit.Position, Unit.Type == ELSTroopType::Cavalry ? 65 : 42, 88, true))
		{
			return ELSAutoBattleRequest::InvalidPlacement;
		}
	}
	for (auto& Unit : P.Enemies)
	{
		if (!CheckBody(Unit.Position, Unit.Type == ELSTroopType::Cavalry ? 65 : 42, 88, false))
		{
			return ELSAutoBattleRequest::InvalidPlacement;
		}
	}
	const auto OldUnits = Units;
	const auto OldControllers = Controllers;
	const int32 OldCount = Units.Num();
	ALSMobileCastle* NewAlly = SpawnCastle(P.AllyCastle, ELSFaction::Ally);
	ALSMobileCastle* NewEnemy = NewAlly ? SpawnCastle(P.EnemyCastle, ELSFaction::Enemy) : nullptr;
	bool bSuccess = NewAlly && NewEnemy;
	TMap<int32, TObjectPtr<ALSUnit>> NewActors;
	int32 AllyId = 1, EnemyId = -1;
	if (bSuccess)
	{
		for (auto& U : P.Allies)
		{
			U.Id = AllyId++;
			ALSUnit* Unit = SpawnUnit(U, ELSFaction::Ally);
			if (!Unit)
			{
				bSuccess = false;
				break;
			}
			NewActors.Add(U.Id, Unit);
		}
	}
	if (bSuccess)
	{
		for (auto& U : P.Enemies)
		{
			U.Id = EnemyId--;
			ALSUnit* Unit = SpawnUnit(U, ELSFaction::Enemy);
			if (!Unit)
			{
				bSuccess = false;
				break;
			}
			NewActors.Add(U.Id, Unit);
		}
	}
	// 새 액터가 모두 준비되기 전까지 기존 편성과 포인터를 확정하지 않는다.
	if (!bSuccess)
	{
		for (int32 I = Units.Num() - 1; I >= OldCount; --I)
		{
			if (IsValid(Units[I]))
			{
				Units[I]->Destroy();
			}
		}
		Units.SetNum(OldCount);
		for (int32 I = OldControllers.Num(); I < Controllers.Num(); ++I)
		{
			if (Controllers[I].IsValid())
			{
				Controllers[I]->Destroy();
			}
		}
		Controllers = OldControllers;
		return ELSAutoBattleRequest::Unavailable;
	}
	for (ALSUnit* Old : OldUnits)
	{
		if (IsValid(Old))
		{
			Old->Destroy();
		}
	}
	for (const auto& Old : OldControllers)
	{
		if (Old.IsValid())
		{
			Old->Destroy();
		}
	}
	Units.RemoveAt(0, OldCount);
	Controllers.RemoveAt(0, OldControllers.Num());
	AllyCastle = NewAlly;
	EnemyCastle = NewEnemy;
	PlacementActors = MoveTemp(NewActors);
	Formation = P.Allies;
	Scenario->EnemyFormation = P.Enemies;
	CastleGround = P.AllyCastle;
	Scenario->EnemyCastleGround = P.EnemyCastle;
	SelectedWeapon = P.AllyWeapon;
	Scenario->EnemyWeapon = P.EnemyWeapon;
	Scenario->Budget = P.Budget;
	Scenario->PopulationLimit = P.PopulationLimit;
	for (int32 I = 0; I < 3; ++I)
	{
		Scenario->UnitCosts.Add(static_cast<ELSTroopType>(I), P.UnitCosts[I]);
	}
	RemainingBudget = P.Budget;
	for (const auto& Unit : Formation)
	{
		RemainingBudget -= P.UnitCosts[static_cast<uint8>(Unit.Type)];
	}
	NextPlacementId = AllyId;
	NextEnemyId = EnemyId;
	++BattleId;
	OnChanged.Broadcast();
	return ELSAutoBattleRequest::Success;
}

bool ALSAutoBattleGameMode::SavePreset(const FString& Name, bool bOverwrite, FString& Error) const
{
	if (bChanging || Phase != ELSAutoBattlePhase::Preparation)
	{
		Error = TEXT("준비 단계에서만 저장할 수 있습니다.");
		return false;
	}
	return FLSAutoBattlePresetStore::Save(Name, GetPreset(), bOverwrite, Error);
}
bool ALSAutoBattleGameMode::LoadPreset(const FString& Name, FString& Error)
{
	if (bChanging || Phase != ELSAutoBattlePhase::Preparation)
	{
		Error = TEXT("준비 단계에서만 불러올 수 있습니다.");
		return false;
	}
	FLSAutoBattlePreset P;
	if (!FLSAutoBattlePresetStore::Load(Name, P, Error))
	{
		return false;
	}
	const auto Result = ApplyPreset(P);
	if (Result != ELSAutoBattleRequest::Success)
	{
		Error = TEXT("현재 전장에 배치할 수 없는 편성입니다. 기존 편성을 유지합니다.");
		return false;
	}
	Error.Reset();
	return true;
}
