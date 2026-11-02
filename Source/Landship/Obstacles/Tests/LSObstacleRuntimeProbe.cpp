#include "Obstacles/Tests/LSObstacleRuntimeProbe.h"
#include "Obstacles/LSMine.h"
#include "Obstacles/LSBarricade.h"
#include "Obstacles/LSTurret.h"
#include "Navigation/LSMineAvoidance.h"
#include "Navigation/LSMineAwareMovementComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Castle/LSMobileCastle.h"
#include "Castle/LSCastleProjectile.h"
#include "Controls/LSOrderComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCombatDeveloperSettings.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSCollisionChannels.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "Vision/LSVisionSubsystem.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

namespace
{
FLSHealthSnapshot HealthOf(AActor* Actor)
{
	FLSHealthSnapshot Result;
	if (Actor)
	{
		if (ULSHealthComponent* Health = Actor->FindComponentByClass<ULSHealthComponent>())
		{
			Health->TryGetHealth(Result);
		}
	}
	return Result;
}
} // namespace
ALSObstacleRuntimeProbe::ALSObstacleRuntimeProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = .02f;
}
void ALSObstacleRuntimeProbe::BeginPlay()
{
	Super::BeginPlay();
	if (!FParse::Param(FCommandLine::Get(), TEXT("ObstacleRuntimeTestExit")))
	{
		SetActorTickEnabled(false);
		Destroy();
	}
}
void ALSObstacleRuntimeProbe::OnMineDestroyed(AActor* Actor)
{
	if (ALSMine* Removed = Cast<ALSMine>(Actor))
	{
		LastMineState = static_cast<int32>(Removed->GetMineState());
	}
}
void ALSObstacleRuntimeProbe::Check(bool bPassed, const TCHAR* Name)
{
	Failures += bPassed ? 0 : 1;
	UE_LOG(LogTemp, Display, TEXT("OBSTACLE_RUNTIME %s: %s phase=%d time=%.2f minDistance=%.3f"),
		   bPassed ? TEXT("PASS") : TEXT("FAIL"), Name, Phase, PhaseTime, MinimumDistance);
}
void ALSObstacleRuntimeProbe::Cleanup()
{
	for (const auto& Actor : Owned)
	{
		if (Actor.IsValid())
		{
			Actor->Destroy();
		}
	}
	Owned.Reset();
	Troop.Reset();
	Castle.Reset();
	Mine.Reset();
	Barrier.Reset();
	Turret.Reset();
}
void ALSObstacleRuntimeProbe::Finish()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	Cleanup();
	SetActorTickEnabled(false);
	UE_LOG(LogTemp, Display, TEXT("OBSTACLE_RUNTIME_FINISHED failures=%d elapsed=%.2f"), Failures, TotalTime);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}
template <class T> T* ALSObstacleRuntimeProbe::Spawn(const FVector& Position)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	T* Actor = GetWorld()->SpawnActor<T>(Position, FRotator::ZeroRotator, Params);
	if (Actor)
	{
		Owned.Add(Actor);
	}
	return Actor;
}
ALSTroopUnit* ALSObstacleRuntimeProbe::SpawnTroop(const FVector& Position, bool bCavalry)
{
	const FTransform Transform(FRotator::ZeroRotator, Position);
	ALSTroopUnit* Actor = GetWorld()->SpawnActorDeferred<ALSTroopUnit>(
		ALSTroopUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Actor)
	{
		return nullptr;
	}
	Actor->SetInitialTroopType(bCavalry ? ELSTroopType::Cavalry : ELSTroopType::Spearman);
	Actor->FinishSpawning(Transform);
	Owned.Add(Actor);
	Actor->Vision->SetSightRadius(2000);
	Actor->BasicAttack->StopAttack();
	return Actor;
}
ALSMobileCastle* ALSObstacleRuntimeProbe::SpawnCastle(const FVector& Position)
{
	ALSMobileCastle* Actor = Spawn<ALSMobileCastle>(Position);
	if (Actor)
	{
		Actor->Vision->SetSightRadius(2000);
	}
	return Actor;
}
bool ALSObstacleRuntimeProbe::Move(ALSUnit* Unit, const FVector& Destination)
{
	FLSOrder Order;
	Order.Kind = ELSOrderKind::Move;
	Order.Destination = Destination;
	return Unit && Unit->Orders->SubmitOrder(Order);
}
void ALSObstacleRuntimeProbe::DiagnoseCorridor()
{
	if (!Castle.IsValid())
	{
		UE_LOG(LogTemp, Display, TEXT("OBSTACLE_RUNTIME_CORRIDOR phase=%d time=%.2f castle=invalid"), Phase, PhaseTime);
		return;
	}
	AAIController* AI = Cast<AAIController>(Castle->GetController());
	const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	const FVector Position = Castle->GetActorLocation();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation OriginProjection, GoalProjection;
	const bool bOriginProjects = Nav && Nav->ProjectPointToNavigation(Castle->GetNavAgentLocation(), OriginProjection);
	const bool bGoalProjects = Nav && Nav->ProjectPointToNavigation(Goal, GoalProjection);
	const UNavigationPath* FreshPath =
		Nav ? Nav->FindPathToLocationSynchronously(GetWorld(), Castle->GetNavAgentLocation(), Goal, Castle.Get())
			: nullptr;
	FString FreshPoints;
	if (FreshPath)
	{
		for (const FVector& Point : FreshPath->PathPoints)
		{
			FreshPoints += Point.ToCompactString() + TEXT(";");
		}
	}
	const FNavPathSharedPtr ActivePath = Following ? Following->GetPath() : FNavPathSharedPtr();
	FString ActivePoints;
	if (ActivePath.IsValid())
	{
		for (const FNavPathPoint& Point : ActivePath->GetPathPoints())
		{
			ActivePoints += Point.Location.ToCompactString() + TEXT(";");
		}
	}
	const UCapsuleComponent* Capsule = Castle->GetCapsuleComponent();
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(LSObstacleCorridorDiagnostic), false, Castle.Get());
	const bool bForwardBlocked = GetWorld()->SweepSingleByChannel(
		Hit, Position, Position + FVector(30, 0, 0), FQuat::Identity, Capsule->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Query);
	UE_LOG(LogTemp, Display,
		   TEXT("OBSTACLE_RUNTIME_CORRIDOR phase=%d time=%.2f pos=%s velocity=%s speed=%.2f maxSpeed=%.2f goal=%s "
				"order=%d lastSuccess=%d failure=%d pathStatus=%d navBuilding=%d originProjected=%d:%s "
				"goalProjected=%d:%s freshValid=%d freshPartial=%d freshPoints=%s activeValid=%d activePartial=%d "
				"activePoints=%s forwardBlocked=%d blocker=%s normal=%s barrierLive=%d"),
		   Phase, PhaseTime, *Position.ToCompactString(), *Castle->GetVelocity().ToCompactString(),
		   Castle->GetVelocity().Size2D(), Castle->GetCharacterMovement()->MaxWalkSpeed, *Goal.ToCompactString(),
		   static_cast<int32>(Castle->Orders->GetOrderKind()), Castle->Orders->DidLastMovementSucceed(),
		   static_cast<int32>(Castle->Orders->GetLastFailure()),
		   Following ? static_cast<int32>(Following->GetStatus()) : -1,
		   UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()), bOriginProjects,
		   *OriginProjection.Location.ToCompactString(), bGoalProjects, *GoalProjection.Location.ToCompactString(),
		   FreshPath && FreshPath->IsValid(), FreshPath && FreshPath->IsPartial(), *FreshPoints,
		   ActivePath.IsValid() && ActivePath->IsValid(), ActivePath.IsValid() && ActivePath->IsPartial(),
		   *ActivePoints, bForwardBlocked, *GetNameSafe(Hit.GetActor()), *Hit.ImpactNormal.ToCompactString(),
		   Barrier.IsValid() && Barrier->IsObstacleActive());
}
void ALSObstacleRuntimeProbe::BeginPhase(int32 Next)
{
	Cleanup();
	Phase = Next;
	PhaseTime = 0;
	NextDiagnosticTime = 0;
	MinimumDistance = TNumericLimits<float>::Max();
	bInitialized = false;
	WorkStarted = -1;
	bStoppedInWorkRing = false;
	bCannonDamageChecked = false;
	LastMineState = -1;
}
void ALSObstacleRuntimeProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFinished)
	{
		return;
	}
	PhaseTime += DeltaSeconds;
	TotalTime += DeltaSeconds;
	if (TotalTime > 120)
	{
		Check(false, TEXT("total bounded timeout"));
		Finish();
		return;
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (Phase == 0)
	{
		if (PhaseTime < 2 || (UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()) && PhaseTime < 12))
		{
			return;
		}
		FNavLocation Projected;
		Check(Nav && Nav->ProjectPointToNavigation(FVector(-1000, -1800, 0), Projected),
			  TEXT("fixture lane projects to real navigation"));
		if (!Nav)
		{
			Finish();
			return;
		}
		if (const ANavigationData* Data = Nav->GetDefaultNavDataInstance())
		{
			UE_LOG(LogTemp, Display,
				   TEXT("OBSTACLE_RUNTIME effectiveNavConfig radius=%.2f height=%.2f navRootHeight=%.2f"),
				   Data->GetConfig().AgentRadius, Data->GetConfig().AgentHeight, Projected.Location.Z);
		}
		BeginPhase(1);
		return;
	}
	if (!bInitialized)
	{
		bInitialized = true;
		Goal = FVector(1000, -1800, 0);
		if (Phase <= 5)
		{
			Mine = Spawn<ALSMine>(FVector(0, -1800, 0));
			if (Mine.IsValid())
			{
				Mine->OnDestroyed.AddDynamic(this, &ThisClass::OnMineDestroyed);
			}
			if (Phase == 5)
			{
				Castle = SpawnCastle(FVector(-1000, -1800, 142));
			}
			else
			{
				Troop = SpawnTroop(FVector(Phase == 4 ? -550 : -1000, -1800, 88), Phase == 2);
			}
			if (!Mine.IsValid() || (Phase == 5 ? !Castle.IsValid() : !Troop.IsValid()))
			{
				Check(false, TEXT("mine phase actors spawned"));
				Finish();
				return;
			}
			if (Phase <= 2)
			{
				Troop->Orders->SetComponentTickEnabled(false);
				ULSMineAwareMovementComponent* Movement =
					Cast<ULSMineAwareMovementComponent>(Troop->GetCharacterMovement());
				AAIController* AI = Cast<AAIController>(Troop->GetController());
				if (Phase == 1 && FParse::Param(FCommandLine::Get(), TEXT("ObstacleDistantMineTest")))
				{
					for (int32 Index = 0; Index < 9; ++Index)
					{
						ALSMine* DistantMine = Spawn<ALSMine>(FVector(3000, 1000 + 100 * Index, 0));
						Check(DistantMine && DistantMine->IsArmed(),
							  TEXT("optional distant mine reproduction actor armed"));
					}
				}
				const TArray<FLSMineSphere> Snapshot = LSMineAvoidance::Snapshot(GetWorld());
				const auto Path = LSMineAvoidance::BuildSafePath(Troop.Get(), Goal, Snapshot);
				if (Phase == 1 && FParse::Param(FCommandLine::Get(), TEXT("ObstacleDistantMineTest")))
				{
					UE_LOG(
						LogTemp, Display,
						TEXT("OBSTACLE_RUNTIME_DISTANT_MINES snapshotCount=%d result=%d pathValid=%d start=%s goal=%s"),
						Snapshot.Num(), static_cast<int32>(Path.Result), Path.Path.IsValid(),
						*Troop->GetActorLocation().ToCompactString(), *Goal.ToCompactString());
				}
				Check(Movement && AI && Path.Result == ELSMinePathResult::SafePath && Path.Path.IsValid(),
					  Phase == 1 ? TEXT("helper path radius42 constructed") : TEXT("helper path radius65 constructed"));
				if (!Movement || !AI || !Path.Path.IsValid())
				{
					Finish();
					return;
				}
				Check(FMath::IsNearlyEqual(Troop->GetCapsuleComponent()->GetScaledCapsuleRadius(),
										   Phase == 1 ? 42.f : 65.f),
					  TEXT("source capsule radius matches fixture"));
				Movement->SetMineAvoidanceEnabled(true);
				FAIMoveRequest Request(Goal);
				Request.SetAcceptanceRadius(15);
				Request.SetReachTestIncludesAgentRadius(false);
				AI->RequestMove(Request, Path.Path);
			}
			else if (Phase == 3)
			{
				Check(Move(Troop.Get(), Goal), TEXT("direct public troop Move accepted"));
			}
			else if (Phase == 4)
			{
				GetWorld()->GetSubsystem<ULSVisionSubsystem>()->RequestRefresh();
			}
			else
			{
				const auto Health = HealthOf(Castle.Get());
				CastleHealth = Health.CurrentHealth;
				CastleMaxHealth = Health.MaxHealth;
				Check(Move(Castle.Get(), Goal), TEXT("direct public castle Move accepted"));
			}
		}
		else if (Phase == 6)
		{
			Castle = SpawnCastle(FVector(-1000, -1800, 142));
			Barrier = Spawn<ALSBarricade>(FVector(0, -1800, 90));
			for (float Offset : {-600.f, 600.f})
			{
				AStaticMeshActor* Wall = Spawn<AStaticMeshActor>(FVector(0, -1800 + Offset, 150));
				if (!Wall)
				{
					Check(false, TEXT("corridor wall spawn"));
					Finish();
					return;
				}
				auto* Mesh = Wall->GetStaticMeshComponent();
				Mesh->SetMobility(EComponentMobility::Movable);
				Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				Mesh->SetCollisionProfileName(TEXT("BlockAll"));
				Mesh->SetCollisionObjectType(LSCollisionChannels::Building);
				Mesh->SetCanEverAffectNavigation(true);
				Wall->SetActorScale3D(FVector(60, 5, 3));
			}
			if (!Castle.IsValid() || !Barrier.IsValid())
			{
				Check(false, TEXT("corridor actors spawn"));
				Finish();
				return;
			}
		}
		else if (Phase == 8)
		{
			Turret = Spawn<ALSTurret>(FVector(0, -1800, 100));
			Castle = SpawnCastle(FVector(-650, -1800, 142));
			Troop = SpawnTroop(FVector(-450, -2200, 88));
			if (!Turret.IsValid() || !Castle.IsValid() || !Troop.IsValid())
			{
				Check(false, TEXT("turret actors spawn"));
				Finish();
				return;
			}
			Castle->Orders->SetComponentTickEnabled(false);
			Troop->Orders->SetComponentTickEnabled(false);
			CastleHealth = HealthOf(Castle.Get()).CurrentHealth;
			TroopHealth = HealthOf(Troop.Get()).CurrentHealth;
			GetWorld()->GetSubsystem<ULSVisionSubsystem>()->RequestRefresh();
		}
	}
	if (Phase <= 2)
	{
		if (!Troop.IsValid() || !Mine.IsValid())
		{
			Check(false, TEXT("helper path actors remain live"));
			Finish();
			return;
		}
		MinimumDistance = FMath::Min(MinimumDistance,
									 static_cast<float>(LSMineAvoidance::GetDisarmDistance(Troop.Get(), Mine.Get())));
		if (FVector::Dist2D(Troop->GetActorLocation(), Goal) < 55 || PhaseTime > 15)
		{
			Check(FVector::Dist2D(Troop->GetActorLocation(), Goal) < 55 && MinimumDistance > 100 && Mine->IsArmed() &&
					  !HealthOf(Troop.Get()).bIsDead,
				  Phase == 1 ? TEXT("helper RequestMove radius42 actually arrived outside trigger")
							 : TEXT("helper RequestMove radius65 actually arrived outside trigger"));
			BeginPhase(Phase + 1);
		}
	}
	else if (Phase == 3)
	{
		if (LastMineState == static_cast<int32>(ELSMineState::Spent) ||
			(Mine.IsValid() && Mine->GetMineState() == ELSMineState::Spent) || PhaseTime > 12)
		{
			Check((LastMineState == static_cast<int32>(ELSMineState::Spent) ||
				   (Mine.IsValid() && Mine->GetMineState() == ELSMineState::Spent)) &&
					  (!Troop.IsValid() || HealthOf(Troop.Get()).bIsDead),
				  TEXT("direct public Move physically contacts mine and troop dies"));
			BeginPhase(4);
		}
	}
	else if (Phase == 4)
	{
		if (!Troop.IsValid() || (!Mine.IsValid() && LastMineState != static_cast<int32>(ELSMineState::Disarmed)))
		{
			Check(false, TEXT("disarm actors remain live"));
			Finish();
			return;
		}
		if (PhaseTime > .4f && WorkStarted < 0)
		{
			FLSOrder Order;
			Order.Kind = ELSOrderKind::Disarm;
			Order.Target = Mine.Get();
			Check(Troop->Orders->SubmitOrder(Order), TEXT("visible mine public Disarm accepted"));
			WorkStarted = 0;
		}
		const double Distance =
			Mine.IsValid() ? LSMineAvoidance::GetDisarmDistance(Troop.Get(), Mine.Get()) : MinimumDistance;
		MinimumDistance = FMath::Min(MinimumDistance, static_cast<float>(Distance));
		if (Distance > 100 && Distance <= 150.5 && Troop->GetVelocity().Size2D() < 5 && Mine.IsValid() &&
			Mine->IsArmed())
		{
			bStoppedInWorkRing = true;
			if (WorkStarted == 0)
			{
				WorkStarted = PhaseTime;
			}
		}
		UStaticMeshComponent* Fill = Mine.IsValid()
			? Cast<UStaticMeshComponent>(Mine->GetDefaultSubobjectByName(TEXT("ProgressFill"))) : nullptr;
		if (!bCannonDamageChecked && Fill && Fill->IsVisible() && Fill->GetRelativeScale3D().X > .05 &&
			Troop->Orders->GetOrderKind() == ELSOrderKind::Disarm && Mine->HasDisarmWorker())
		{
			Check(WorkStarted > 0, TEXT("zero-knockback cannon starts after actual Disarm work progressed"));
			ALSMobileCastle* Source = SpawnCastle(FVector(-1000, -2400, 142));
			if (!Source)
			{
				Check(false, TEXT("zero-knockback cannon friendly source spawned"));
				Finish();
				return;
			}
			const float HealthBefore = HealthOf(Troop.Get()).CurrentHealth;
			const float MineHealthBefore = HealthOf(Mine.Get()).CurrentHealth;
			const FVector Impact = Troop->GetActorLocation();
			const FTransform Transform(FRotator::ZeroRotator, Impact - FVector(200, 0, 0));
			ALSCastleProjectile* Shot = GetWorld()->SpawnActorDeferred<ALSCastleProjectile>(
				ALSCastleProjectile::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			FLSCastleWeaponSettings Settings;
			Settings.Damage = 1;
			Settings.Radius = 30;
			Settings.KnockbackSpeed = 0;
			Settings.KnockbackLift = 0;
			ULSCombatDeveloperSettings* Policy = GetMutableDefault<ULSCombatDeveloperSettings>();
			{
				const bool bFriendlyFireBefore = Policy->bAllowFriendlyFire;
				const bool bSelfDamageBefore = Policy->bAllowSelfDamage;
				ON_SCOPE_EXIT
				{
					Policy->bAllowFriendlyFire = bFriendlyFireBefore;
					Policy->bAllowSelfDamage = bSelfDamageBefore;
				};
				Policy->bAllowFriendlyFire = true;
				Policy->bAllowSelfDamage = true;
				const bool bLaunched = Shot && Shot->Launch(ELSCastleWeapon::Cannon, Settings,
					ULSHealthComponent::MakeDamageRequest(1, Source, Source), Impact);
				Check(bLaunched, TEXT("zero-knockback cannon launched at active Disarm worker"));
				if (!bLaunched)
				{
					if (Shot) Shot->Destroy();
					Finish();
					return;
				}
				Shot->FinishSpawning(Transform);
				Shot->Tick(1);
			}
			bCannonDamageChecked = true;
			Source->Destroy();
			Check(FMath::IsNearlyEqual(HealthBefore - HealthOf(Troop.Get()).CurrentHealth, 1.f),
				TEXT("zero-knockback cannon applied one actual damage to worker"));
			Check(Troop->Orders->GetOrderKind() == ELSOrderKind::Hold && !Mine->HasDisarmWorker() &&
				!Fill->IsVisible(), TEXT("positive cannon damage cancels Disarm and releases progress and claim"));
			Check(!Troop->Orders->IsKnockedBack() &&
				Troop->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero(),
				TEXT("zero-knockback cannon damage creates no knockback state or pending launch"));
			Check(Mine->IsArmed() && FMath::IsNearlyEqual(HealthOf(Mine.Get()).CurrentHealth, MineHealthBefore),
				TEXT("small cannon blast preserves the mine for a fresh Disarm approach"));
			// 피해 반응과 해제 완료를 따로 검증하도록 원래 접근 출발점에서 새 명령을 시작한다.
			Troop->GetCharacterMovement()->StopMovementImmediately();
			Check(Troop->SetActorLocation(FVector(-550, -1800, 88), false, nullptr, ETeleportType::TeleportPhysics),
				TEXT("fresh Disarm approach restores original phase-four start after damage"));
			bStoppedInWorkRing = false;
			FLSOrder Order;
			Order.Kind = ELSOrderKind::Disarm;
			Order.Target = Mine.Get();
			const bool bAccepted = Troop->Orders->SubmitOrder(Order);
			Check(bAccepted && Mine->HasDisarmWorker(),
				TEXT("fresh Disarm approach accepted once after positive zero-knockback cannon damage"));
			WorkStarted = 0;
		}
		if (LastMineState == static_cast<int32>(ELSMineState::Disarmed) ||
			(Mine.IsValid() && Mine->GetMineState() == ELSMineState::Disarmed) || PhaseTime > 18)
		{
			Check(bCannonDamageChecked, TEXT("zero-knockback cannon damage was observed during actual Disarm work"));
			Check((LastMineState == static_cast<int32>(ELSMineState::Disarmed) ||
				   (Mine.IsValid() && Mine->GetMineState() == ELSMineState::Disarmed)) &&
					  bStoppedInWorkRing && WorkStarted > 0 && PhaseTime - WorkStarted >= 5.8f &&
					  MinimumDistance > 100 && Troop->Orders->GetOrderKind() == ELSOrderKind::Hold,
				  TEXT("real Disarm approached safe ring worked six seconds and entered Hold"));
			BeginPhase(5);
		}
	}
	else if (Phase == 5)
	{
		if (LastMineState == static_cast<int32>(ELSMineState::Spent) ||
			(Mine.IsValid() && Mine->GetMineState() == ELSMineState::Spent) || PhaseTime > 12)
		{
			Check(Castle.IsValid() &&
					  (LastMineState == static_cast<int32>(ELSMineState::Spent) ||
					   (Mine.IsValid() && Mine->GetMineState() == ELSMineState::Spent)) &&
					  FMath::IsNearlyEqual(CastleHealth - HealthOf(Castle.Get()).CurrentHealth, CastleMaxHealth * .4f,
										   .1f),
				  TEXT("real castle direct contact loses forty percent max health"));
			BeginPhase(6);
		}
	}
	else if (Phase == 6)
	{
		if (WorkStarted < 0 && PhaseTime > 2 && !UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()))
		{
			Check(Move(Castle.Get(), Goal), TEXT("corridor public Move accepted before destruction"));
			WorkStarted = PhaseTime;
		}
		if (WorkStarted >= 0 && PhaseTime - WorkStarted > 5)
		{
			Check(Castle.IsValid() && Castle->GetActorLocation().X < -150 && Barrier.IsValid() &&
					  Barrier->IsObstacleActive(),
				  TEXT("spanning barricade physically prevents castle crossing"));
			Barrier->GetHealthComponent()->ApplyDamage(
				ULSHealthComponent::MakeDamageRequest(2000, Castle.Get(), Castle.Get()));
			DiagnoseCorridor();
			Phase = 7;
			PhaseTime = 0;
			WorkStarted = -1;
			NextDiagnosticTime = 0;
		}
		else if (PhaseTime > 15)
		{
			Check(false, TEXT("corridor navigation readiness timeout"));
			BeginPhase(8);
		}
	}
	else if (Phase == 7)
	{
		if (PhaseTime >= NextDiagnosticTime)
		{
			DiagnoseCorridor();
			NextDiagnosticTime = PhaseTime + 1;
		}
		if (WorkStarted < 0 && PhaseTime > 2 && !UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()))
		{
			const bool bAccepted = Move(Castle.Get(), Goal);
			Check(bAccepted, TEXT("same public Move accepted after destruction and nav rebuild"));
			AAIController* AI = Castle.IsValid() ? Cast<AAIController>(Castle->GetController()) : nullptr;
			const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
			const FNavPathSharedPtr Path = Following ? Following->GetPath() : FNavPathSharedPtr();
			const float Speed = Castle.IsValid() ? Castle->GetCharacterMovement()->MaxWalkSpeed : 0;
			if (!bAccepted || !Path.IsValid() || !Path->IsValid() || Path->IsPartial() || Speed <= 0)
			{
				Check(false, TEXT("corridor accepted move has a complete selected path and positive speed"));
				BeginPhase(8);
				return;
			}
			double Length = 0;
			const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
			for (int32 Index = 1; Index < Points.Num(); ++Index)
			{
				Length += FVector::Dist2D(Points[Index - 1].Location, Points[Index].Location);
			}
			// 파괴 전 우회 경로에서 이미 이동한 거리도 새 요청의 실제 출발점에 반영한다.
			CorridorTravelBudget = static_cast<float>(Length / Speed) + 4.f;
			WorkStarted = PhaseTime;
			UE_LOG(LogTemp, Display,
				   TEXT("OBSTACLE_RUNTIME_CORRIDOR_BUDGET selectedPathLength=%.2f maxSpeed=%.2f accelerationMargin=4 "
						"budget=%.2f requestTime=%.2f"),
				   Length, Speed, CorridorTravelBudget, WorkStarted);
		}
		if ((Castle.IsValid() && FVector::Dist2D(Castle->GetActorLocation(), Goal) < 190) ||
			(WorkStarted >= 0 && PhaseTime - WorkStarted > CorridorTravelBudget))
		{
			Check(Castle.IsValid() && Castle->GetActorLocation().X > 800,
				  TEXT("castle actually crosses destroyed barricade corridor"));
			BeginPhase(8);
		}
		else if (WorkStarted < 0 && PhaseTime > 15)
		{
			Check(false, TEXT("destroyed corridor navigation readiness timeout"));
			BeginPhase(8);
		}
	}
	else if (Phase == 8)
	{
		if (Castle.IsValid() && HealthOf(Castle.Get()).CurrentHealth < CastleHealth)
		{
			Check(Troop.IsValid() && HealthOf(Troop.Get()).CurrentHealth == TroopHealth,
				  TEXT("turret real projectile damages priority castle while troop remains unharmed"));
			Turret->StopObstacleActions();
			FLSBasicAttackSettings Settings = Troop->BasicAttack->GetAttackSettings();
			Settings.Damage = 600;
			Troop->BasicAttack->ConfigureAttack(Settings);
			Troop->Orders->SetComponentTickEnabled(true);
			FLSOrder Order;
			Order.Kind = ELSOrderKind::AttackTarget;
			Order.Target = Turret.Get();
			Check(Troop->Orders->SubmitOrder(Order), TEXT("public troop AttackTarget turret accepted"));
			Phase = 9;
			PhaseTime = 0;
		}
		else if (PhaseTime > 10)
		{
			Check(false, TEXT("turret projectile castle priority timeout"));
			Finish();
		}
	}
	else if (Phase == 9)
	{
		if (!Turret.IsValid() || !Turret->IsObstacleActive() || PhaseTime > 12)
		{
			Check(!Turret.IsValid() || !Turret->IsObstacleActive(), TEXT("real troop public attack destroys turret"));
			Finish();
		}
	}
}
