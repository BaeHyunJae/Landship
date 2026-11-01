#include "Controls/Tests/LSMineOrdersRuntimeProbe.h"
#include "Controls/LSOrderComponent.h"
#include "Troops/LSTroopUnit.h"
#include "Obstacles/LSMine.h"
#include "Obstacles/LSMineHazardSubsystem.h"
#include "Navigation/LSMineAvoidance.h"
#include "Navigation/LSMineAwareMovementComponent.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Vision/LSVisionParticipantComponent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"

namespace
{
FLSHealthSnapshot MineOrdersHealth(const AActor* Actor)
{
	FLSHealthSnapshot Snapshot;
	if (IsValid(Actor))
		if (const ULSHealthComponent* Health = Actor->FindComponentByClass<ULSHealthComponent>())
			Health->TryGetHealth(Snapshot);
	return Snapshot;
}
} // namespace

ALSMineOrdersRuntimeProbe::ALSMineOrdersRuntimeProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = .02f;
}

void ALSMineOrdersRuntimeProbe::BeginPlay()
{
	Super::BeginPlay();
	if (!FParse::Param(FCommandLine::Get(), TEXT("MineOrdersRuntimeTestExit")))
	{
		SetActorTickEnabled(false);
		Destroy();
	}
}

ALSTroopUnit* ALSMineOrdersRuntimeProbe::SpawnTroop(const FVector& Position, bool bEnemy)
{
	const FTransform Transform(FRotator::ZeroRotator, Position);
	ALSTroopUnit* Troop = GetWorld()->SpawnActorDeferred<ALSTroopUnit>(
		ALSTroopUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Troop)
		return nullptr;
	Troop->SetInitialFaction(bEnemy ? ELSFaction::Enemy : ELSFaction::Ally);
	Troop->SetInitialTroopType(ELSTroopType::Spearman);
	Troop->FinishSpawning(Transform);
	Owned.Add(Troop);
	if (Troop->GetController())
		Owned.Add(Troop->GetController());
	Troop->Vision->SetSightRadius(2000);
	Troop->Orders->SetComponentTickEnabled(false);
	Troop->BasicAttack->StopAttack();
	return Troop;
}

ALSMine* ALSMineOrdersRuntimeProbe::SpawnMine(const FVector& Position)
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALSMine* Mine = GetWorld()->SpawnActor<ALSMine>(Position, FRotator::ZeroRotator, Parameters);
	if (Mine)
	{
		Owned.Add(Mine);
		Mines.Add(Mine);
		Mine->OnDestroyed.AddDynamic(this, &ThisClass::OnMineDestroyed);
	}
	return Mine;
}

void ALSMineOrdersRuntimeProbe::OnMineDestroyed(AActor* Actor)
{
	if (const ALSMine* Mine = Cast<ALSMine>(Actor))
		LastMineState = static_cast<int32>(Mine->GetMineState());
}

void ALSMineOrdersRuntimeProbe::Cleanup()
{
	for (const TWeakObjectPtr<AActor>& Actor : Owned)
		if (Actor.IsValid())
			Actor->Destroy();
	Owned.Reset();
	Mines.Reset();
	Source.Reset();
	Enemy.Reset();
	ForwardMine.Reset();
}

void ALSMineOrdersRuntimeProbe::BeginPhase(int32 Next)
{
	Cleanup();
	Phase = Next;
	PhaseTime = 0;
	NextDiagnostic = 0;
	StoppedSince = ReleasedAt = -1;
	MinimumDistance = TNumericLimits<float>::Max();
	MaximumBlockedSpeed = 0;
	bInitialized = bIssued = bSawAvoidance = bRetentionFailed = false;
	LastMineState = -1;
}

void ALSMineOrdersRuntimeProbe::Check(bool bPass, const TCHAR* Name)
{
	Failures += bPass ? 0 : 1;
	UE_LOG(LogTemp, Display, TEXT("MINE_ORDERS_RUNTIME %s: %s phase=%d time=%.3f minSurfaceDistance=%.3f"),
		   bPass ? TEXT("PASS") : TEXT("FAIL"), Name, Phase, PhaseTime, MinimumDistance);
}

void ALSMineOrdersRuntimeProbe::Finish()
{
	if (bFinished)
		return;
	bFinished = true;
	Cleanup();
	SetActorTickEnabled(false);
	UE_LOG(LogTemp, Display, TEXT("MINE_ORDERS_RUNTIME_FINISHED failures=%d elapsed=%.3f"), Failures, TotalTime);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void ALSMineOrdersRuntimeProbe::Sample()
{
	if (!Source.IsValid())
		return;
	for (const TWeakObjectPtr<ALSMine>& Weak : Mines)
		if (const ALSMine* Mine = Weak.Get(); Mine && Mine->IsHazardActive())
			MinimumDistance =
				FMath::Min(MinimumDistance, static_cast<float>(LSMineAvoidance::GetDisarmDistance(Source.Get(), Mine)));
	if (const ULSMineAwareMovementComponent* Movement =
			Cast<ULSMineAwareMovementComponent>(Source->GetCharacterMovement()))
		bSawAvoidance |= Movement->IsMineAvoidanceEnabled() && Source->GetVelocity().Size2D() > 5;
}

void ALSMineOrdersRuntimeProbe::Diagnose()
{
	if (PhaseTime < NextDiagnostic)
		return;
	NextDiagnostic = PhaseTime + .5f;
	const ALSTroopUnit* Troop = Source.Get();
	const ULSOrderComponent* Orders = Troop ? Troop->Orders : nullptr;
	const ULSMineAwareMovementComponent* Movement =
		Troop ? Cast<ULSMineAwareMovementComponent>(Troop->GetCharacterMovement()) : nullptr;
	const ULSMineHazardSubsystem* Hazards = GetWorld()->GetSubsystem<ULSMineHazardSubsystem>();
	UE_LOG(LogTemp, Display,
		   TEXT("MINE_ORDERS_RUNTIME_SAMPLE phase=%d t=%.3f pos=%s speed=%.2f order=%d queue=%d engaged=%s "
				"enemyHP=%.2f sourceHP=%.2f avoidance=%d constrained=%d constrainedTime=%.3f minSurface=%.3f "
				"revision=%llu released=%.3f"),
		   Phase, PhaseTime, Troop ? *Troop->GetActorLocation().ToCompactString() : TEXT("invalid"),
		   Troop ? Troop->GetVelocity().Size2D() : 0, Orders ? static_cast<int32>(Orders->GetOrderKind()) : -1,
		   Orders ? Orders->GetQueuedCount() : -1, Orders ? *GetNameSafe(Orders->GetEngagedTarget()) : TEXT("none"),
		   MineOrdersHealth(Enemy.Get()).CurrentHealth, MineOrdersHealth(Troop).CurrentHealth,
		   Movement && Movement->IsMineAvoidanceEnabled(), Movement && Movement->IsMineConstrained(),
		   Movement ? Movement->GetMineConstraintDuration() : 0, MinimumDistance,
		   static_cast<unsigned long long>(Hazards ? Hazards->GetRevision() : 0), ReleasedAt);
}

void ALSMineOrdersRuntimeProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFinished)
		return;
	PhaseTime += DeltaSeconds;
	TotalTime += DeltaSeconds;
	if (TotalTime > 120)
	{
		Check(false, TEXT("120 game-second total timeout"));
		Finish();
		return;
	}
	if (Phase == 0)
	{
		if (PhaseTime < 2 || (UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()) && PhaseTime < 12))
			return;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		FNavLocation Point;
		Check(Nav && Nav->ProjectPointToNavigation(FVector(-600, -1800, 0), Point),
			  TEXT("real Orders fixture lane has navigation"));
		if (!Nav)
		{
			Finish();
			return;
		}
		BeginPhase(1);
		return;
	}
	if (!bInitialized)
	{
		bInitialized = true;
		Destination = FVector(1200, -1800, 0);
		Source = SpawnTroop(FVector(-600, -1800, 88), false);
		if (Phase != 3)
			Enemy = SpawnTroop(FVector(0, -1800, 88), true);
		if (Phase == 5)
		{
			for (int32 Index = 0; Index < 6; ++Index)
			{
				const double Angle = 2 * PI * Index / 6;
				ALSMine* Mine = SpawnMine(FVector(-600 + 220 * FMath::Cos(Angle), -1800 + 220 * FMath::Sin(Angle), 0));
				if (Index == 0)
					ForwardMine = Mine;
			}
		}
		else
			ForwardMine = SpawnMine(FVector(-300, -1800, 0));
		Check(Source.IsValid() && ForwardMine.IsValid() && (Phase == 3 || Enemy.IsValid()) &&
				  Mines.Num() == (Phase == 5 ? 6 : 1),
			  TEXT("phase actors spawned with real troop definitions"));
		if (!Source.IsValid() || !ForwardMine.IsValid() || (Phase != 3 && !Enemy.IsValid()))
		{
			Finish();
			return;
		}
		FLSUnitStatRow Stats;
		const bool bStats = Source->TryGetStats(Stats);
		Check(bStats && Stats.DetectionRange > 516 && Stats.AttackRange < 300 && Source->Orders->CanMove() &&
				  Source->Orders->CanAttack(),
			  TEXT("troop stats permit automatic acquisition and require approach"));
		UE_LOG(LogTemp, Display, TEXT("MINE_ORDERS_RUNTIME_STATS detection=%.2f range=%.2f speed=%.2f HP=%.2f"),
			   Stats.DetectionRange, Stats.AttackRange, Stats.MovementSpeed, Stats.Health);
		EnemyInitialHealth = MineOrdersHealth(Enemy.Get()).CurrentHealth;
		return;
	}
	Sample();
	Diagnose();
	if (!bIssued)
	{
		if (PhaseTime < .4f)
			return;
		bIssued = true;
		Source->Orders->SetComponentTickEnabled(true);
		if (Phase != 1)
		{
			FLSOrder Order;
			Order.Kind = Phase == 2 ? ELSOrderKind::AttackTarget : ELSOrderKind::AttackMove;
			Order.Destination = Destination;
			Order.Target = Phase == 2 ? Enemy.Get() : nullptr;
			Check(Source->Orders->SubmitOrder(Order), TEXT("real public order accepted"));
			if (Order.Kind == ELSOrderKind::AttackMove)
				Destination = Source->Orders->GetOrderDestination();
			if (Phase == 5)
			{
				FLSOrder Queued;
				Queued.Kind = ELSOrderKind::Hold;
				Check(Source->Orders->SubmitOrder(Queued, true) && Source->Orders->GetQueuedCount() == 1,
					  TEXT("queued Hold accepted behind AttackMove"));
			}
		}
		return;
	}
	const bool bSourceAlive = Source.IsValid() && !MineOrdersHealth(Source.Get()).bIsDead;
	if (Phase == 2 || Phase == 3)
	{
		if (!bSourceAlive)
		{
			Check(LastMineState == static_cast<int32>(ELSMineState::Spent) ||
					  (ForwardMine.IsValid() && ForwardMine->GetMineState() == ELSMineState::Spent),
				  TEXT("direct order contacts and spends mine"));
			Check(!bSawAvoidance, TEXT("direct movement never enables mine avoidance"));
			BeginPhase(Phase + 1);
			return;
		}
	}
	else
	{
		if (!bSourceAlive)
		{
			Check(false, TEXT("automatic movement source survived"));
			Finish();
			return;
		}
		if (Phase == 5 && ReleasedAt < 0)
		{
			const bool bRetained = Source->Orders->GetOrderKind() == ELSOrderKind::AttackMove &&
								   Source->Orders->GetQueuedCount() == 1 &&
								   Source->Orders->GetOrderDestination().Equals(Destination, 1);
			bRetentionFailed |= !bRetained;
			if (PhaseTime > 1 && Source->GetVelocity().Size2D() < 1 &&
				Source->Orders->GetEngagedTarget() == Enemy.Get())
			{
				if (StoppedSince < 0)
					StoppedSince = PhaseTime;
				MaximumBlockedSpeed =
					FMath::Max(MaximumBlockedSpeed, static_cast<float>(Source->GetVelocity().Size2D()));
			}
			else
				StoppedSince = -1;
			if (StoppedSince >= 0 && PhaseTime - StoppedSince >= 2)
			{
				Check(
					!bRetentionFailed && bRetained,
					TEXT("closed mine ring preserves AttackMove destination and queued Hold for two stopped seconds"));
				Check(Source->Orders->GetLastFailure() != ELSOrderFailure::Unreachable,
					  TEXT("hazard stop is not reported as general Unreachable"));
				Check(MinimumDistance > 100, TEXT("closed ring stops before actual body contact"));
				bool bRingArmed = true;
				for (const TWeakObjectPtr<ALSMine>& Mine : Mines)
					bRingArmed &= Mine.IsValid() && Mine->IsArmed();
				Check(bRingArmed, TEXT("all ring mines remain armed while blocked"));
				Check(MineOrdersHealth(Enemy.Get()).CurrentHealth == EnemyInitialHealth,
					  TEXT("closed ring prevents attack before removal"));
				ULSMineHazardSubsystem* Hazards = GetWorld()->GetSubsystem<ULSMineHazardSubsystem>();
				RevisionBefore = Hazards ? Hazards->GetRevision() : 0;
				ForwardMine->Destroy();
				ReleasedAt = PhaseTime;
				Check(Hazards && Hazards->GetRevision() > RevisionBefore,
					  TEXT("fixture removal updates hazard revision"));
			}
		}
		else if (MineOrdersHealth(Enemy.Get()).CurrentHealth < EnemyInitialHealth)
		{
			Check(Enemy.IsValid() && !MineOrdersHealth(Enemy.Get()).bIsDead,
				  TEXT("stationary enemy survives observed source damage"));
			Check(Source->Orders->GetEngagedTarget() == Enemy.Get() &&
					  Source->BasicAttack->GetAttackTarget() == Enemy.Get(),
				  TEXT("real Orders automatically acquired and attacked the standing enemy"));
			Check(bSawAvoidance, TEXT("real Orders enabled mine avoidance during movement"));
			Check(MinimumDistance > 100, TEXT("actual source body stayed outside trigger throughout approach"));
			bool bAllArmed = true;
			for (const TWeakObjectPtr<ALSMine>& Mine : Mines)
				if (!(Phase == 5 && Mine == ForwardMine))
					bAllArmed &= Mine.IsValid() && Mine->IsArmed();
			Check(bAllArmed, TEXT("all remaining mines are armed after real chase"));
			if (Phase == 4 || Phase == 5)
				Check(Source->Orders->GetOrderKind() == ELSOrderKind::AttackMove &&
						  Source->Orders->GetOrderDestination().Equals(Destination, 1) &&
						  (Phase != 5 || Source->Orders->GetQueuedCount() == 1),
					  TEXT("automatic engagement retains original AttackMove and pending queue"));
			if (Phase == 5)
			{
				Check(ReleasedAt > 0, TEXT("ring removal preceded resumed attack"));
				Finish();
			}
			else
				BeginPhase(Phase + 1);
			return;
		}
	}
	if (PhaseTime > 22)
	{
		Check(false, TEXT("phase timeout waiting for required observed behavior"));
		Finish();
	}
}
