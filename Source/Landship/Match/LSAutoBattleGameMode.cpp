#include "Match/LSAutoBattleGameMode.h"
#include "Match/LSAutoBattlePreset.h"
#include "Troops/LSTroopCombatComponent.h"
#include "Troops/LSTroopPoolSubsystem.h"
#include "Controls/LSAutoBattleController.h"
#include "Controls/LSFreeCameraPawn.h"
#include "Controls/LSOrderComponent.h"
#include "Controls/LSSelectableComponent.h"
#include "Match/LSAutoBattleCastle.h"
#include "Castle/LSCastleWeaponComponent.h"
#include "Castle/LSCastleProjectile.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSProjectilePoolSubsystem.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Navigation/LSSlopeMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/ScopeExit.h"

namespace
{
constexpr float PlacementClearance = 2.0f;

FString GetBattlefieldKey(const ALSAutoBattleGameMode::FLSBattlefieldOption& Option)
{
	return Option.TerrainPackage ? Option.TerrainPackage : TEXT("flat");
}

int32 FindBattlefieldIndex(const FString& Key)
{
	const auto Options = ALSAutoBattleGameMode::GetBattlefieldOptions();
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		if (GetBattlefieldKey(Options[Index]) == Key)
			return Index;
	}
	return INDEX_NONE;
}
}

TConstArrayView<ALSAutoBattleGameMode::FLSBattlefieldOption> ALSAutoBattleGameMode::GetBattlefieldOptions()
{
	static const FLSBattlefieldOption Battlefields[] = {
	    {TEXT("평지"), nullptr},
	    {TEXT("다리 1개"), TEXT("/Game/Developers/HMH/TABS/L_TABS_Bridge")},
	    {TEXT("다리 3개"), TEXT("/Game/Developers/HMH/TABS/L_TABS_ThreeBridges")},
	    {TEXT("높낮이 전장"), TEXT("/Game/Developers/HMH/TABS/L_TABS_Heights")},
	};
	return MakeArrayView(Battlefields);
}

int32 ALSAutoBattleGameMode::GetCurrentBattlefieldIndex() const
{
	return FlatTerrain.IsEmpty() ? INDEX_NONE : BattlefieldIndex;
}

bool ALSAutoBattleGameMode::SelectBattlefield(int32 Index)
{
	const auto Battlefields = GetBattlefieldOptions();
	if (bChanging || Phase != ELSAutoBattlePhase::Preparation || FlatTerrain.IsEmpty() ||
	    !Battlefields.IsValidIndex(Index) || Index == BattlefieldIndex)
	{
		return false;
	}
	TGuardValue<bool> Guard(bChanging, true);
	const TCHAR* Package = Battlefields[Index].TerrainPackage;
	ULevelStreamingDynamic* NewTerrain = nullptr;
	if (Package)
	{
		bool bRequested = false;
		NewTerrain = ULevelStreamingDynamic::LoadLevelInstance(
		    this, Package, FVector::ZeroVector, FRotator::ZeroRotator, bRequested);
		if (!bRequested || !NewTerrain)
			return false;
	}

	Phase = ELSAutoBattlePhase::Loading;
	DestroyBattle();
	UnloadingTerrain = VariantTerrain;
	if (UnloadingTerrain)
	{
		UnloadingTerrain->SetShouldBeVisible(false);
		UnloadingTerrain->SetShouldBeLoaded(false);
		UnloadingTerrain->SetIsRequestingUnloadAndRemoval(true);
	}
	VariantTerrain = NewTerrain;

	const bool bFlat = Package == nullptr;
	for (const FLSFlatTerrainState& State : FlatTerrain)
	{
		if (AStaticMeshActor* Actor = State.Actor.Get())
		{
			Actor->SetActorHiddenInGame(!bFlat || State.bHidden);
			Actor->SetActorEnableCollision(bFlat && State.bCollision);
			Actor->GetStaticMeshComponent()->SetCanEverAffectNavigation(bFlat && State.bNavigation);
		}
	}
	BattlefieldIndex = Index;
	Formation.Reset();
	Scenario->EnemyFormation.Reset();
	CastleGround = bFlat ? Scenario->AllyCastleGround : FVector(-2100, 0, 0);
	Scenario->EnemyCastleGround = bFlat ? DefaultEnemyCastleGround : FVector(2100, 0, 0);
	SelectedWeapon = ELSCastleWeapon::Cannon;
	Scenario->EnemyWeapon = DefaultEnemyWeapon;
	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		if (auto* Camera = Cast<ALSFreeCameraPawn>(Controller->GetPawn()))
		{
			Camera->SetHomeView(bFlat ? Scenario->CameraLocation : FVector(-3900, -1800, 2400),
			                    bFlat ? Scenario->CameraRotation : FRotator(-32, 18, 0));
		}
	}
	bHasPreparedFormation = false;
	bTerrainNavigationRebuildPending = true;
	NavigationDeadline = GetWorld()->GetTimeSeconds() + 15;
	GetWorldTimerManager().SetTimer(InitializationTimer, this, &ThisClass::InitializeScenario, 0.2f, true);
	OnChanged.Broadcast();
	return true;
}

ALSAutoBattleGameMode::ALSAutoBattleGameMode()
{
	PlayerControllerClass = ALSAutoBattleController::StaticClass();
	DefaultPawnClass = ALSFreeCameraPawn::StaticClass();
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DecalMaterial(
	    TEXT("/Game/Landship/Castle/Materials/M_CastleFieldDecal.M_CastleFieldDecal"));
	FieldDecalMaterial = DecalMaterial.Object;
}

void ALSAutoBattleGameMode::StartPlay()
{
	Super::StartPlay();
	if (!FMath::IsFinite(DecisionInterval) || DecisionInterval < 0.05f || !FMath::IsFinite(RepathInterval) ||
	    RepathInterval < 0.05f || !FMath::IsFinite(RepathDistance) || RepathDistance <= 0 ||
	    !FMath::IsFinite(ApproachPadding) || ApproachPadding < 0 || !MeleeApproach.IsValid() ||
	    !FMath::IsFinite(EnemyAimTime) || EnemyAimTime <= 0 ||
	    !FMath::IsFinite(EnemyWeaponRecovery) || EnemyWeaponRecovery <= 0)
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
	DefaultEnemyCastleGround = Scenario->EnemyCastleGround;
	DefaultEnemyWeapon = Scenario->EnemyWeapon;
	if (UGameplayStatics::GetCurrentLevelName(this) == TEXT("L_TABS"))
	{
		for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		{
			if (It->GetLevel() != GetWorld()->PersistentLevel)
				continue;
			FlatTerrain.Add({*It, It->IsHidden(), It->GetActorEnableCollision(),
			                 It->GetStaticMeshComponent()->CanEverAffectNavigation()});
		}
	}
	NavigationDeadline = GetWorld()->GetTimeSeconds() + 15;
	SpawnHandle = GetWorld()->AddOnActorSpawnedHandler(
	    FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::OnActorSpawned));
	OutcomeHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &ThisClass::CollectOutcome);
	GetWorldTimerManager().SetTimer(InitializationTimer, this, &ThisClass::InitializeScenario, 0.2f, true);
}

void ALSAutoBattleGameMode::InitializeScenario()
{
	if ((UnloadingTerrain && UnloadingTerrain->GetLoadedLevel()) ||
	    (VariantTerrain && !VariantTerrain->IsLevelVisible()))
	{
		if (GetWorld()->GetTimeSeconds() > NavigationDeadline)
		{
			GetWorldTimerManager().ClearTimer(InitializationTimer);
			Fail(TEXT("전장 지형을 불러오지 못했습니다."));
		}
		return;
	}
	UnloadingTerrain = nullptr;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (bTerrainNavigationRebuildPending && Nav)
	{
		if (ANavigationData* Data = Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
		{
			// 지형을 등록한 프레임이 끝난 뒤 전체 타일을 다시 만들어 이전 다리의 경로를 제거한다.
			Data->RebuildAll();
			bTerrainNavigationRebuildPending = false;
			return;
		}
	}
	FNavLocation Location;
	if (!Nav || bTerrainNavigationRebuildPending || Nav->IsNavigationBuildInProgress() ||
	    (!bHasPreparedFormation && !PendingPreset.IsSet() &&
	     (!Nav->ProjectPointToNavigation(CastleGround, Location, FVector(100, 100, 300)) ||
	      !Nav->ProjectPointToNavigation(Scenario->EnemyCastleGround, Location, FVector(100, 100, 300)))))
	{
		if (GetWorld()->GetTimeSeconds() > NavigationDeadline)
		{
			GetWorldTimerManager().ClearTimer(InitializationTimer);
			Fail(TEXT("내비게이션 준비에 실패했습니다. NavMesh 범위를 확인하세요."));
		}
		return;
	}
	GetWorldTimerManager().ClearTimer(InitializationTimer);
	if (PendingPreset.IsSet())
	{
		FLSAutoBattlePreset Preset = MoveTemp(PendingPreset.GetValue());
		PendingPreset.Reset();
		Phase = ELSAutoBattlePhase::Preparation;
		const bool bRestoringPrevious = !PreviousPreset.IsSet();
		if (ApplyPreset(Preset) == ELSAutoBattleRequest::Success)
		{
			PreviousPreset.Reset();
			PresetLoadMessage = bRestoringPrevious
			    ? TEXT("저장된 전장에 편성을 배치할 수 없어 이전 전장과 편성을 복구했습니다.")
			    : TEXT("전장과 편성을 불러왔습니다.");
			OnChanged.Broadcast();
			return;
		}
		if (PreviousPreset.IsSet())
		{
			const int32 PreviousIndex = FindBattlefieldIndex(PreviousPreset->Battlefield);
			PendingPreset = MoveTemp(PreviousPreset.GetValue());
			PreviousPreset.Reset();
			if (SelectBattlefield(PreviousIndex))
				return;
		}
		Fail(TEXT("편성 불러오기에 실패했습니다. 전장과 배치 위치를 확인하세요."));
		return;
	}
	const TArray<FLSAutoBattlePlacement> Snapshot = Formation;
	Rebuild(Snapshot, bHasPreparedFormation);
}

void ALSAutoBattleGameMode::Fail(const FString& Reason)
{
	if (PendingPreset.IsSet() || PreviousPreset.IsSet())
		PresetLoadMessage = Reason;
	PendingPreset.Reset();
	PreviousPreset.Reset();
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
	                                           FCollisionShape::MakeCapsule(Radius + PlacementClearance, Height), Query))
	{
		if (UE_LOG_ACTIVE(LogTemp, Verbose))
		{
			TArray<FOverlapResult> Hits;
			GetWorld()->OverlapMultiByObjectType(Hits, Projected.Location + FVector(0, 0, Height + 4), FQuat::Identity,
			                                     Objects, FCollisionShape::MakeCapsule(Radius + PlacementClearance, Height), Query);
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
	if (ALSTroopUnit::GetStatsRowName(Type).IsNone() || (Faction != ELSFaction::Ally && Faction != ELSFaction::Enemy))
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
	if (ALSAutoBattleCastle* Castle = Cast<ALSAutoBattleCastle>(Unit))
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
	ULSTroopPoolSubsystem* Pool = GetWorld()->GetSubsystem<ULSTroopPoolSubsystem>();
	const FLSTroopLease Lease = Pool ? Pool->AcquireUnit(ALSTroopUnit::StaticClass(), Placement.Type,
	    Faction, Transform, this) : FLSTroopLease();
	ALSTroopUnit* Unit = Lease.Unit.Get();
	if (!Unit)
	{
		return nullptr;
	}
	Unit->Orders->SetComponentTickEnabled(false);
	if (!ULSCombatTargetLibrary::IsCombatAlive(Unit))
	{
		ReleaseUnitActor(Unit);
		return nullptr;
	}
	Placement.Position = Ground;
	Freeze(Unit);
	Units.Add(Unit);
	return Unit;
}

ALSAutoBattleCastle* ALSAutoBattleGameMode::SpawnCastle(const FVector& Ground, ELSFaction Faction,
                                                    bool bRestoreValidatedGround)
{
	FVector Projected;
	if (!ResolveGround(Ground, 142, 142, nullptr, Faction == ELSFaction::Ally, Projected, bRestoreValidatedGround))
	{
		return nullptr;
	}
	const FTransform Transform(FRotator::ZeroRotator, Projected + FVector(0, 0, 144));
	ALSAutoBattleCastle* Castle = GetWorld()->SpawnActorDeferred<ALSAutoBattleCastle>(
	    ALSAutoBattleCastle::StaticClass(), Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
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
	if (Faction == ELSFaction::Enemy && Scenario->EnemyFormation.Num() >= 100)
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
	EditingFormation.RemoveAt(Index);
	PlacementActors.Remove(Id);
	Units.Remove(Unit);
	AAIController* AI = Cast<AAIController>(Unit->GetController());
	ReleaseUnitActor(Unit);
	ReleaseUnitController(AI);
	Controllers.Remove(AI);
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
	ALSAutoBattleCastle* EditingCastle = Faction == ELSFaction::Ally ? AllyCastle : EnemyCastle;
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
	ALSAutoBattleCastle* EditingCastle = Faction == ELSFaction::Ally ? AllyCastle : EnemyCastle;
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
	if (ULSProjectilePoolSubsystem* Pool = GetWorld()->GetSubsystem<ULSProjectilePoolSubsystem>())
	{
		TMap<TSubclassOf<ALSBasicProjectile>, int32> FirstVolley;
		for (ALSUnit* Unit : Units)
		{
			const FLSProjectileAttackSettings& Shot = Unit->BasicAttack->GetProjectileSettings();
			if (Shot.bUsePool && Shot.ProjectileClass &&
			    Shot.ProjectileClass->GetDefaultObject<ALSBasicProjectile>()->SupportsPooling())
				++FirstVolley.FindOrAdd(Shot.ProjectileClass);
		}
		for (const auto& Pair : FirstVolley)
			Pool->PrewarmPool(Pair.Key, Pair.Value);
	}
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
		if (ALSAutoBattleCastle* Castle = Cast<ALSAutoBattleCastle>(Unit))
		{
			Castle->Weapons->Activate();
		}
		Unit->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Unit->GetCharacterMovement()->SetAvoidanceEnabled(false);
		if (auto* SlopeMovement = Cast<ULSSlopeMovementComponent>(Unit->GetCharacterMovement()))
		{
			const FRuntimeFloatCurve& SpeedCurve =
			    Unit->IsA<ALSAutoBattleCastle>() ? Scenario->CastleSlopeSpeed : Scenario->TroopSlopeSpeed;
			SlopeMovement->SetSlopeSpeedCurve(*SpeedCurve.GetRichCurveConst());
		}
		if (Unit == AllyCastle.Get())
		{
			AllyCastle->EnablePlayerDrive();
			continue;
		}
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
	if (Phase != ELSAutoBattlePhase::Fighting && Phase != ELSAutoBattlePhase::Victory &&
	    Phase != ELSAutoBattlePhase::Defeat)
	{
		return ELSAutoBattleRequest::WrongPhase;
	}
	TGuardValue<bool> Guard(bChanging, true);
	const TArray<FLSAutoBattlePlacement> Snapshot = bKeepFormation ? Formation : TArray<FLSAutoBattlePlacement>();
	Phase = ELSAutoBattlePhase::Loading;
	DestroyBattle();
	Formation = Snapshot;
	// 파괴한 충돌 등록이 정리된 다음 타이머에서 편성을 복원한다.

	NavigationDeadline = GetWorld()->GetTimeSeconds() + 15;
	GetWorldTimerManager().SetTimer(InitializationTimer, this, &ThisClass::InitializeScenario, 0.2f, true);
	OnChanged.Broadcast();
	return ELSAutoBattleRequest::Success;
}

void ALSAutoBattleGameMode::UpdateAgent(FLSAutoBattleAgent& Agent, const TArray<ALSUnit*>& Opponents,
    const TArray<ALSUnit*>& Allies)
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
	ALSTroopUnit* TroopUnit = Cast<ALSTroopUnit>(Unit);
	const bool bSupport = TroopUnit && TroopUnit->CombatBehavior && TroopUnit->CombatBehavior->IsSupportBehavior();
	const bool bLocalDetection = TroopUnit && !bSupport;
	FLSUnitStatRow TroopStats;
	const float AttackRange = TroopUnit && TroopUnit->TryGetStats(TroopStats)
	    ? TroopStats.AttackRange : Unit->BasicAttack->GetAttackSettings().Range;
	const double DetectionRange = bLocalDetection
	    ? FMath::Max(Scenario->EnemyDetectionRanges.FindRef(TroopUnit->GetTroopType()), AttackRange)
	    : TNumericLimits<double>::Max();
	TArray<ALSUnit*> DetectedOpponents;
	ALSUnit* NearestDetected = nullptr;
	ALSUnit* AdvanceTarget = Unit->GetFaction() == ELSFaction::Ally ? EnemyCastle.Get() : AllyCastle.Get();
	if (bLocalDetection)
	{
		const bool bHasEnemyCastle = ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, AdvanceTarget);
		if (!bHasEnemyCastle)
		{
			AdvanceTarget = nullptr;
		}
		double NearestDetectedDistance = TNumericLimits<double>::Max();
		double NearestRemainingDistance = TNumericLimits<double>::Max();
		for (ALSUnit* Candidate : Opponents)
		{
			if (!ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Candidate))
			{
				continue;
			}
			const double Distance = ULSCombatTargetLibrary::GetAttackDistance(Unit, Candidate);
			if (Distance <= DetectionRange)
			{
				DetectedOpponents.Add(Candidate);
				if (Distance < NearestDetectedDistance)
				{
					NearestDetected = Candidate;
					NearestDetectedDistance = Distance;
				}
			}
			if (!bHasEnemyCastle && Distance < NearestRemainingDistance)
			{
				AdvanceTarget = Candidate;
				NearestRemainingDistance = Distance;
			}
		}
	}
	if (!bSupport && (!ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Target) ||
	    (bLocalDetection && ULSCombatTargetLibrary::GetAttackDistance(Unit, Target) >
	                            DetectionRange + Scenario->TargetRetentionPadding)))
	{
		Target = NearestDetected;
		if (!bLocalDetection)
		{
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
		}
		if (Agent.Target.Get() != Target || Agent.Target.IsStale())
		{
			Unit->BasicAttack->StopAttack();
			MeleePlanner.Release(Unit);
			Agent.Target = Target;
			Agent.NextRepath = 0;
		}
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
		Directive = Troop->CombatBehavior->EvaluateEngagement(Target, bSupport ? Allies : Opponents);
		if (!ULSCombatTargetLibrary::IsCombatAlive(Unit))
		{
			MeleePlanner.Release(Unit);
			return;
		}
	}
	Target = Cast<ALSUnit>(Directive.Target);
	const bool bAdvancing = (bLocalDetection || bSupport) && !Target && !Directive.bHoldPosition;
	if (bAdvancing)
	{
		Target = AdvanceTarget;
		if (bSupport && !ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Target))
		{
			Target = nullptr;
			double NearestDistance = TNumericLimits<double>::Max();
			for (ALSUnit* Opponent : Opponents)
			{
				if (!ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Opponent))
					continue;
				const double Distance = ULSCombatTargetLibrary::GetAttackDistance(Unit, Opponent);
				if (Distance < NearestDistance)
				{
					Target = Opponent;
					NearestDistance = Distance;
				}
			}
		}
		Directive.bSuppressBasicAttack = true;
	}
	const bool bValidTarget = bSupport && !bAdvancing ? ULSCombatTargetLibrary::IsCombatAlive(Target) &&
	    Target != Unit && Target->GetFaction() == Unit->GetFaction() :
	    ULSCombatTargetLibrary::CanAutoAttackTarget(Unit, Target);
	if (!bValidTarget)
	{
		Unit->BasicAttack->StopAttack();
		MeleePlanner.Release(Unit);
		AI->StopMovement();
		return;
	}
	if (bSupport)
	{
		Unit->BasicAttack->StopAttack();
		if (Agent.Target.Get() != Target)
			Agent.NextRepath = 0;
		Agent.Target = Target;
	}
	const bool bMelee = !bSupport && !Unit->IsA<ALSAutoBattleCastle>() &&
	    !Unit->BasicAttack->GetProjectileSettings().ProjectileClass;
	FLSMeleeApproachResult MeleePlan;
	if (bMelee && !bAdvancing && !Directive.bHoldPosition &&
	    (!Directive.bSuppressBasicAttack || Directive.bUseMeleeApproach))
	{
		MeleePlan = MeleePlanner.Plan(Unit, Target, DetectedOpponents, GetWorld()->GetTimeSeconds(), Directive.ApproachRange,
		                              MeleeApproach);
		if (MeleePlan.Target != Target)
		{
			Unit->BasicAttack->StopAttack();
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
	const ALSAutoBattleCastle* Castle = Cast<ALSAutoBattleCastle>(Unit);
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
	if ((!bAdvancing || bSupport) && Distance <= Range - Padding)
	{
		if (bMoving)
		{
			AI->StopMovement();
			Unit->GetCharacterMovement()->StopMovementImmediately();
		}
		if (Directive.bSuppressBasicAttack)
		{
			Unit->BasicAttack->StopAttack();
		}
		else if (!Castle && Unit->BasicAttack->GetAttackTarget() != Target)
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

TOptional<ELSCastleWeapon> ALSAutoBattleGameMode::GetEnemyAimingWeapon() const
{
	if (Phase == ELSAutoBattlePhase::Fighting && EnemyWeaponAim.IsSet())
		return EnemyWeaponAim.GetValue().Weapon;
	return {};
}

bool ALSAutoBattleGameMode::UpdateEnemyWeapons(const TArray<ALSUnit*>& Opponents)
{
	ALSAutoBattleCastle* Castle = EnemyCastle.Get();
	if (!ULSCombatTargetLibrary::IsCombatAlive(Castle))
	{
		EnemyWeaponAim.Reset();
		return false;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	const FVector CastleLocation = Castle->GetActorLocation();
	if (EnemyWeaponAim.IsSet())
	{
		const FLSEnemyWeaponAim Aim = EnemyWeaponAim.GetValue();
		// 밀려나거나 떨어지면 예고한 위치에서 발사할 수 없으므로 조준을 취소한다.
		if (Castle->Orders->IsKnockedBack() || !Castle->GetCharacterMovement()->IsMovingOnGround() ||
		    !CastleLocation.Equals(Aim.CastleLocation, 1.0))
		{
			EnemyWeaponAim.Reset();
			EnemyNextWeaponTime = Now + EnemyWeaponRecovery;
			return false;
		}
		if (Now < Aim.FireTime)
		{
			DrawEnemyWeaponWarning();
			return true;
		}

		EnemyWeaponAim.Reset();
		EnemyNextWeaponTime = Now + EnemyWeaponRecovery;
		Castle->Weapons->CastWeapon(Aim.Weapon, Aim.Aim);
		return true;
	}
	if (Now < EnemyNextWeaponTime || Castle->Orders->IsKnockedBack() ||
	    !Castle->GetCharacterMovement()->IsMovingOnGround())
		return false;

	TOptional<FLSEnemyWeaponAim> Best;
	double BestScore = 0;
	for (const ELSCastleWeapon Weapon : {ELSCastleWeapon::Cannon, ELSCastleWeapon::Shotgun, ELSCastleWeapon::Ballista})
	{
		const FLSCastleWeaponSettings* Settings = Castle->Weapons->FindSettings(Weapon);
		if (!Settings || Castle->Weapons->GetStatus(Weapon) != ELSWeaponCastResult::Ready)
			continue;

		TArray<ALSUnit*, TInlineAllocator<32>> InRange;
		for (ALSUnit* Opponent : Opponents)
		{
			if (ULSCombatTargetLibrary::CanAutoAttackTarget(Castle, Opponent) &&
			    FVector::DistSquared2D(CastleLocation, Opponent->GetActorLocation()) <= FMath::Square(Settings->Range))
			{
				InRange.Add(Opponent);
			}
		}

		// 대규모 편성에서도 밀집도·사선 검사를 수행할 후보 수를 제한한다.
		constexpr int32 MaxAimSamples = 24;
		const int32 Stride = FMath::Max(1, FMath::DivideAndRoundUp(InRange.Num(), MaxAimSamples));
		for (int32 Index = 0; Index < InRange.Num(); Index += Stride)
		{
			const ALSUnit* Candidate = InRange[Index];
			FVector Aim = Candidate->GetActorLocation();
			double Score = 0;
			if (Weapon == ELSCastleWeapon::Cannon)
			{
				Aim.Z -= Candidate->GetSimpleCollisionHalfHeight();
				int32 Count = 0;
				for (const ALSUnit* Nearby : InRange)
				{
					if (FVector::DistSquared2D(Aim, Nearby->GetActorLocation()) <= FMath::Square(Settings->Radius) &&
					    ++Count >= 8)
						break;
				}
				Score = Count >= 2 ? 60 + Count : 15;
			}
			else
			{
				const FVector Origin = Castle->Weapons->GetWeaponFireOrigin(Weapon, Aim);
				const FVector Direction = (Aim - CastleLocation).GetSafeNormal2D();
				if (Direction.IsNearlyZero() ||
				    FMath::Abs(Aim.Z - Origin.Z) > Candidate->GetSimpleCollisionHalfHeight() + Settings->Radius)
				{
					continue;
				}

				const FVector End(Aim.X, Aim.Y, Origin.Z);
				FCollisionObjectQueryParams Cover;
				Cover.AddObjectTypesToQuery(ECC_WorldStatic);
				Cover.AddObjectTypesToQuery(LSCollisionChannels::Ground);
				Cover.AddObjectTypesToQuery(LSCollisionChannels::PathObstacle);
				FCollisionQueryParams Query(SCENE_QUERY_STAT(LSEnemyCastleFireLane), false, Castle);
				Query.AddIgnoredActor(Candidate);
				FHitResult CoverHit;
				if (GetWorld()->LineTraceSingleByObjectType(CoverHit, Origin, End, Cover, Query))
					continue;

				if (Weapon == ELSCastleWeapon::Ballista)
				{
					FLSHealthSnapshot Health;
					const auto* Component = Candidate->FindComponentByClass<ULSHealthComponent>();
					if (!Component || !Component->TryGetHealth(Health))
						continue;
					Score = Candidate == AllyCastle.Get() ? 45 : 10 + FMath::Min(Health.CurrentHealth / 100.0, 4.0);
				}
				else
				{
					const double ConeDot = FMath::Cos(FMath::DegreesToRadians(Settings->ConeAngle * 0.5));
					int32 Count = 0;
					for (const ALSUnit* Nearby : InRange)
					{
						const FVector Offset = Nearby->GetActorLocation() - CastleLocation;
						if (FVector::DotProduct(Direction, Offset.GetSafeNormal2D()) >= ConeDot && ++Count >= 8)
							break;
					}
					const double Distance = ULSCombatTargetLibrary::GetAttackDistance(Castle, Candidate);
					Score = (Distance <= 300.0 ? 100 : 25) + Count;
				}
			}

			// 준비에서 고른 적 무장은 같은 상황 점수에서 우선한다.
			Score += Weapon == Scenario->EnemyWeapon ? 0.5 : 0;
			if (Score > BestScore && Castle->Weapons->ValidateAim(Weapon, Aim) == ELSWeaponCastResult::Ready)
			{
				BestScore = Score;
				Best.Emplace(FLSEnemyWeaponAim{Weapon, Aim, CastleLocation, Now + EnemyAimTime});
			}
		}
	}

	if (!Best.IsSet())
	{
		EnemyNextWeaponTime = Now + 0.25;
		return false;
	}
	if (auto* AI = Cast<AAIController>(Castle->GetController()))
		AI->StopMovement();
	Castle->GetCharacterMovement()->StopMovementImmediately();
	EnemyWeaponAim = Best;
	DrawEnemyWeaponWarning();
	return true;
}

void ALSAutoBattleGameMode::DrawEnemyWeaponWarning() const
{
	const FLSEnemyWeaponAim& Aim = EnemyWeaponAim.GetValue();
	const FLSCastleWeaponSettings* Settings = EnemyCastle->Weapons->FindSettings(Aim.Weapon);
	if (!Settings)
		return;
	const float Duration = DecisionInterval + 0.025f;
	const FColor Color = FColor::Red;
	const FVector Origin = EnemyCastle->Weapons->GetWeaponFireOrigin(Aim.Weapon, Aim.Aim);
	const FVector Direction = (Aim.Aim - Aim.CastleLocation).GetSafeNormal2D();
	if (Aim.Weapon == ELSCastleWeapon::Cannon)
	{
		DrawDebugCircle(GetWorld(), Aim.Aim + FVector(0, 0, 8), Settings->Radius, 32, Color, false, Duration, 0, 5,
		                FVector::ForwardVector, FVector::RightVector, false);
	}
	else if (Aim.Weapon == ELSCastleWeapon::Shotgun)
	{
		const double HalfAngle = Settings->ConeAngle * 0.5;
		FVector Previous = Origin;
		for (int32 Segment = 0; Segment <= 12; ++Segment)
		{
			const FVector Edge = Origin + Direction.RotateAngleAxis(-HalfAngle + Settings->ConeAngle * Segment / 12,
			                                                        FVector::UpVector) * Settings->Range;
			DrawDebugLine(GetWorld(), Previous, Edge, Color, false, Duration, 0, 4);
			Previous = Edge;
		}
		DrawDebugLine(GetWorld(), Previous, Origin, Color, false, Duration, 0, 4);
	}
	else
	{
		const FVector ShotDirection = (Aim.Aim - Origin).GetSafeNormal2D();
		DrawDebugLine(GetWorld(), Origin, Origin + ShotDirection * Settings->Range, Color, false, Duration, 0, 7);
	}
}

void ALSAutoBattleGameMode::AutoTick()
{
	if (Phase != ELSAutoBattlePhase::Fighting || bChanging)
	{
		return;
	}
	// 공격·피해 알림에서 복원을 재진입하면 순회 중인 편성 배열이 해제될 수 있다.
	TGuardValue<bool> Guard(bChanging, true);
	UpdateCastleFields();
	MeleePlanner.Prune();
	TArray<ALSUnit*> Allies, Enemies;
	for (ALSUnit* Unit : Units)
	{
		if (ULSCombatTargetLibrary::IsCombatAlive(Unit))
		{
			(Unit->GetFaction() == ELSFaction::Ally ? Allies : Enemies).Add(Unit);
		}
	}
	const bool bEnemyAiming = UpdateEnemyWeapons(Allies);
	for (FLSAutoBattleAgent& Agent : Agents)
	{
		if (Agent.Unit.IsValid() && (!bEnemyAiming || Agent.Unit.Get() != EnemyCastle.Get()))
		{
			const bool bAlly = Agent.Unit->GetFaction() == ELSFaction::Ally;
			UpdateAgent(Agent, bAlly ? Enemies : Allies, bAlly ? Allies : Enemies);
		}
	}
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
	ClearCastleFields();
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
			if (ALSAutoBattleCastle* Castle = Cast<ALSAutoBattleCastle>(Unit))
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
	ClearCastleFields();
	EnemyWeaponAim.Reset();
	EnemyNextWeaponTime = 0;
	for (const TWeakObjectPtr<AActor>& Projectile : Projectiles)
	{
		if (Projectile.IsValid())
		{
			const ALSBasicProjectile* Arrow = Cast<ALSBasicProjectile>(Projectile.Get());
			if (!Arrow || !Arrow->IsPoolManaged())
				Projectile->Destroy();
		}
	}
	Projectiles.Reset();
	if (ULSProjectilePoolSubsystem* Pool = GetWorld()->GetSubsystem<ULSProjectilePoolSubsystem>())
		Pool->ReleaseAll();
	for (ALSUnit* Unit : Units)
	{
		ReleaseUnitActor(Unit);
	}
	for (const TWeakObjectPtr<AAIController>& AI : Controllers)
	{
		ReleaseUnitController(AI.Get());
	}
	Controllers.Reset();
	Units.Reset();
	Agents.Reset();
	MeleePlanner.Reset();
	PlacementActors.Reset();
	AllyCastle = nullptr;
	EnemyCastle = nullptr;
}

void ALSAutoBattleGameMode::ReleaseUnitActor(ALSUnit* Unit)
{
	if (!IsValid(Unit) || Unit->IsActorBeingDestroyed())
		return;
	if (ALSTroopUnit* Troop = Cast<ALSTroopUnit>(Unit); Troop && Troop->IsPoolManaged())
	{
		if (Troop->GetActivationSerial() == 0)
			return;
		if (ULSTroopPoolSubsystem* Pool = GetWorld()->GetSubsystem<ULSTroopPoolSubsystem>())
			if (Pool->ReleaseUnit({Troop, Troop->GetActivationSerial()}))
				return;
	}
	Unit->Destroy();
}

void ALSAutoBattleGameMode::ReleaseUnitController(AAIController* Controller)
{
	if (!IsValid(Controller) || Controller->IsActorBeingDestroyed())
		return;
	const ALSTroopUnit* Troop = Cast<ALSTroopUnit>(Controller->GetPawn());
	if (!Troop || !Troop->IsPoolManaged())
		Controller->Destroy();
}

void ALSAutoBattleGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(InitializationTimer);
	GetWorldTimerManager().ClearTimer(DecisionTimer);
	FWorldDelegates::OnWorldTickEnd.Remove(OutcomeHandle);
	GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
	Super::EndPlay(Reason);
}

FLSAutoBattlePreset ALSAutoBattleGameMode::GetPreset() const
{
	FLSAutoBattlePreset P;
	if (!Scenario)
	{
		P.Version = 0;
		return P;
	}
	P.UnitCosts.Reset();
	const int32 CurrentIndex = GetCurrentBattlefieldIndex();
	const auto Battlefields = GetBattlefieldOptions();
	if (Battlefields.IsValidIndex(CurrentIndex))
		P.Battlefield = GetBattlefieldKey(Battlefields[CurrentIndex]);
	for (int32 I = 0; I < ALSTroopUnit::GetTroopTypeCount(); ++I)
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
			    FVector::DistSquared2D(Resolved, Body.Ground) < FMath::Square(Radius + Body.Radius + PlacementClearance))
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
	ALSAutoBattleCastle* NewAlly = SpawnCastle(P.AllyCastle, ELSFaction::Ally);
	ALSAutoBattleCastle* NewEnemy = NewAlly ? SpawnCastle(P.EnemyCastle, ELSFaction::Enemy) : nullptr;
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
				ReleaseUnitActor(Units[I]);
			}
		}
		Units.SetNum(OldCount);
		for (int32 I = OldControllers.Num(); I < Controllers.Num(); ++I)
		{
			if (Controllers[I].IsValid())
			{
				ReleaseUnitController(Controllers[I].Get());
			}
		}
		Controllers = OldControllers;
		return ELSAutoBattleRequest::Unavailable;
	}
	for (ALSUnit* Old : OldUnits)
	{
		if (IsValid(Old))
		{
			ReleaseUnitActor(Old);
		}
	}
	for (const auto& Old : OldControllers)
	{
		if (Old.IsValid())
		{
			ReleaseUnitController(Old.Get());
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
	for (int32 I = 0; I < ALSTroopUnit::GetTroopTypeCount(); ++I)
	{
		Scenario->UnitCosts.Add(static_cast<ELSTroopType>(I), P.UnitCosts[I]);
	}
	NextPlacementId = AllyId;
	NextEnemyId = EnemyId;
	bHasPreparedFormation = true;
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
	if (!P.Battlefield.IsEmpty())
	{
		const int32 Index = FindBattlefieldIndex(P.Battlefield);
		if (Index == INDEX_NONE || GetCurrentBattlefieldIndex() == INDEX_NONE)
		{
			Error = TEXT("저장된 전장을 이 레벨에서 열 수 없습니다.");
			return false;
		}
		if (Index != GetCurrentBattlefieldIndex())
		{
			PreviousPreset = GetPreset();
			PendingPreset = MoveTemp(P);
			if (SelectBattlefield(Index))
			{
				Error.Reset();
				return true;
			}
			PendingPreset.Reset();
			PreviousPreset.Reset();
			Error = TEXT("저장된 전장을 불러오지 못했습니다.");
			return false;
		}
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

FString ALSAutoBattleGameMode::TakePresetLoadMessage()
{
	FString Message = MoveTemp(PresetLoadMessage);
	PresetLoadMessage.Reset();
	return Message;
}
