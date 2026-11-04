#include "Enemies/Tests/LSEnemyRegressionProbe.h"

#include "AIController.h"
#include "Castle/LSCastle.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/LSBasicAttackComponent.h"
#include "Combat/LSBasicProjectile.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/StaticMeshComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Enemies/LSEnemyTroop.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "Logging/StructuredLog.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

struct FLSEnemyRegressionTestAccess
{
	static bool FindShotPosition(const ULSEnemyBehaviorComponent* Behavior, const AActor* Target, FVector& OutGoal)
	{
#if WITH_DEV_AUTOMATION_TESTS
		const FVector Here = Behavior->GetOwner()->GetActorLocation();
		return Behavior->FindAttackPosition(Target->GetActorLocation(), OutGoal,
			Behavior->Role == ELSEnemyRole::Assault ? &Here : nullptr, Target);
#else
		return false;
#endif
	}
	static double RetryDeadline(const ULSEnemyBehaviorComponent* Behavior)
	{
#if WITH_DEV_AUTOMATION_TESTS
		return Behavior->NextMoveAttempt;
#else
		return 0;
#endif
	}
	static bool HasSupport(const ULSEnemyBehaviorComponent* Behavior)
	{
#if WITH_DEV_AUTOMATION_TESTS
		return Behavior->bHasActiveSupport;
#else
		return false;
#endif
	}
	static void LogState(const ULSEnemyBehaviorComponent* Behavior, const FVector& Start, const FVector& ExpectedCenter, double OriginalDeadline)
	{
#if WITH_DEV_AUTOMATION_TESTS
		UE_LOGFMT(LogTemp, Display,
			"[{Function}] ENEMY_RETRY_DIAGNOSTIC state={State} pose={Pose} start={Start} center={Center} expectedCenter={ExpectedCenter} active={Active} reportedOpponent={Opponent} goal={Goal} retry={Retry} originalRetry={OriginalRetry} now={Now} failed={Failed} recovering={Recovering} latched={Latched} reason={Reason}",
			("Function", __FUNCTION__), ("State", static_cast<int32>(Behavior->State)),
			("Pose", Behavior->GetOwner()->GetActorLocation().ToString()), ("Start", Start.ToString()),
			("Center", Behavior->EngagementCenter.ToString()), ("ExpectedCenter", ExpectedCenter.ToString()),
			("Active", Behavior->bHasActiveSupport), ("Opponent", Behavior->ActiveSupport.OpponentLocation.ToString()),
			("Goal", Behavior->MovementGoal.ToString()), ("Retry", Behavior->NextMoveAttempt),
			("OriginalRetry", OriginalDeadline), ("Now", Behavior->GetWorld()->GetTimeSeconds()),
			("Failed", Behavior->bMovementFailed), ("Recovering", Behavior->bRecovering),
			("Latched", Behavior->bEngagementLatched), ("Reason", Behavior->LastStopReason));
#endif
	}
};

ALSEnemyRegressionProbe::ALSEnemyRegressionProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
}

void ALSEnemyRegressionProbe::BeginPlay()
{
	Super::BeginPlay();
	if (!FParse::Param(FCommandLine::Get(), TEXT("LSEnemyRegressionTestExit")))
	{
		SetActorTickEnabled(false);
		Destroy();
		return;
	}
	FParse::Value(FCommandLine::Get(), TEXT("LSEnemyRegressionCase="), Case);
	for (TActorIterator<ALSEnemyTroop> It(GetWorld()); It; ++It) { Freeze(*It); }
}

void ALSEnemyRegressionProbe::Freeze(ALSEnemyTroop* Troop)
{
	Troop->Behavior->SetComponentTickEnabled(false);
	Troop->BasicAttack->StopAttack();
	if (AController* Controller = Troop->GetController()) { Controller->StopMovement(); }
}

void ALSEnemyRegressionProbe::Check(bool bPassed, const TCHAR* Message)
{
	Failures += bPassed ? 0 : 1;
	UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_REGRESSION {Case} {Result}: {Message}",
		("Function", __FUNCTION__), ("Case", Case), ("Result", bPassed ? TEXT("PASS") : TEXT("FAIL")), ("Message", Message));
}

void ALSEnemyRegressionProbe::Finish()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("LSEnemyRegressionLowFPS")))
	{
		Check(MaximumDelta >= 0.18f, TEXT("actual game delta reached low frame rate stimulus"));
	}
	UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_REGRESSION_FINISHED case={Case} failures={Failures} samples={Samples} maxDelta={Delta} maxRadius={Radius}",
		("Function", __FUNCTION__), ("Case", Case), ("Failures", Failures), ("Samples", Samples), ("Delta", MaximumDelta), ("Radius", MaximumRadius));
	SetActorTickEnabled(false);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void ALSEnemyRegressionProbe::OnOpponentDamaged(const FLSDamageResult& Damage)
{
	if (Damage.AppliedDamage <= 0 || Damage.SourceActor.Get() != Unit.Get()) { return; }
	const ELSEnemyState State = Unit->Behavior->GetState();
	bDamagedDuringReturn |= State == ELSEnemyState::Returning || State == ELSEnemyState::ReturnBlocked;
}

void ALSEnemyRegressionProbe::OnShotExecuted(const FLSAttackExecution& Execution)
{
	++ObservedShotCallbacks;
	LastShotExecutionId = 0;
	if (!Case.StartsWith(TEXT("ClearShot")) || !Execution.bProjectile || Execution.Source.Get() != Unit.Get() || Execution.Target.Get() != Opponent.Get() ||
		!Unit.IsValid() || !Opponent.IsValid()) return;
	LastShotExecutionId = Execution.ExecutionId;
	ShotOrigins.Add(Execution.ExecutionId, Execution.SourceLocation);
	LastShotSource = Execution.SourceLocation;
	LastShotTarget = Execution.TargetLocation;
	// 이 표적은 움직이지 않는다. 발사 기록과 같은 위치일 때만 공통 경로 질의의 대상 위치로 쓴다.
	const bool bClear = Opponent->GetActorLocation().Equals(Execution.TargetLocation, .01) &&
		Unit->BasicAttack->HasClearAttackPathFromLocation(Execution.SourceLocation, Opponent.Get());
	if (bClear) ++ClearShotLaunches;
	else ++BlockedShotLaunches;
}

void ALSEnemyRegressionProbe::OnShotProjectileLaunched(ALSBasicProjectile* Projectile)
{
	if (IsValid(Projectile) && LastShotExecutionId > 0)
		ProjectileExecutions.Add(TWeakObjectPtr<AActor>(Projectile), LastShotExecutionId);
}

void ALSEnemyRegressionProbe::OnClearShotDamageCommitted(const FLSDamageResult& Damage)
{
	const int64* ExecutionId = ProjectileExecutions.Find(Damage.Causer);
	const FVector* Origin = ExecutionId ? ShotOrigins.Find(*ExecutionId) : nullptr;
	if (Damage.AppliedDamage > 0 && Damage.Source.Get() == Unit.Get() &&
		Damage.bHasSourceFaction && Damage.SourceFaction == ELSFaction::Enemy &&
		ExecutionId && Origin && Damage.bHasAttackOrigin && Origin->Equals(Damage.AttackOrigin, .01))
	{
		OwnCommittedDamage += Damage.AppliedDamage;
		DamagingExecutionId = *ExecutionId;
	}
	else ++OtherDamageRecords;
}

void ALSEnemyRegressionProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	StageTime += DeltaSeconds;
	MaximumDelta = FMath::Max(MaximumDelta, DeltaSeconds);
	if (Elapsed > 35) { Check(false, TEXT("bounded regression timeout")); Finish(); return; }
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (Stage == 0)
	{
		if (StageTime < 1 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())) { return; }
		for (TActorIterator<ALSCastle> It(GetWorld()); It; ++It) { Castle = *It; }
		for (TActorIterator<ALSEnemyOutpost> It(GetWorld()); It; ++It) { Core = *It; }
		for (TActorIterator<ALSEnemyTroop> It(GetWorld()); It; ++It)
		{
			const bool bGuard = Case == TEXT("Return") || Case == TEXT("ClearShotGuard") || Case == TEXT("ClearShotAnchor");
			const ELSTroopType Type = (Case == TEXT("Retry") || Case.StartsWith(TEXT("ClearShot"))) ? ELSTroopType::Archer : ELSTroopType::Spearman;
			if (It->Behavior->Role == (bGuard ? ELSEnemyRole::Guard : ELSEnemyRole::Assault) && It->GetTroopType() == Type) { Unit = *It; }
		}
		Check(Castle.IsValid() && Core.IsValid() && Unit.IsValid() && Nav &&
			(Case == TEXT("Detour") || Case == TEXT("Return") || Case == TEXT("Retry") || Case == TEXT("AssaultBand") || Case == TEXT("ClearShotAssault") || Case == TEXT("ClearShotGuard") || Case == TEXT("ClearShotAnchor")), TEXT("known regression case and authored actors available"));
		if (Failures) { Finish(); return; }
		Castle->Orders->SetComponentTickEnabled(false);

		Castle->SetActorLocation(FVector(-3000, -2600, 142));
		Core->Behavior->SetComponentTickEnabled(false);
		Core->BasicAttack->StopAttack();
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (Case.StartsWith(TEXT("ClearShot")))
		{
			for (TActorIterator<ALSEnemyTroop> It(GetWorld()); It; ++It)
			{
				Freeze(*It);
				It->BasicAttack->Deactivate();
				It->GetCharacterMovement()->StopMovementImmediately();
				It->GetCharacterMovement()->SetComponentTickEnabled(false);
			}
			Core->BasicAttack->Deactivate();
			Start = Unit->Behavior->Role == ELSEnemyRole::Guard ? Unit->GetActorLocation() : FVector(-1700, -1000, 88);
			Unit->SetActorLocation(Start);
			Unit->Behavior->ChaseRadius = 1000;
			if (Case == TEXT("ClearShotAnchor"))
			{
				ATargetPoint* Anchor = GetWorld()->SpawnActor<ATargetPoint>(Start, FRotator::ZeroRotator, Spawn);
				Check(Anchor != nullptr, TEXT("noncombat guard anchor spawned"));
				if (!Anchor) { Finish(); return; }
				Unit->Behavior->GuardedOutpost = nullptr;
				Unit->Behavior->GuardAnchor = Anchor;
				Unit->Behavior->GuardAnchorRadius = 700;
			}
			AStaticMeshActor* Wall = GetWorld()->SpawnActor<AStaticMeshActor>(Start + FVector(400, 0, 112), FRotator::ZeroRotator, Spawn);
			Check(Wall != nullptr, TEXT("actual clear-shot terrain wall spawned"));
			if (!Wall) { Finish(); return; }
			Blocker = Wall;
			auto* Mesh = Wall->GetStaticMeshComponent();
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Wall->SetActorScale3D(FVector(.8f, 4.4f, 4));
			Mesh->SetCollisionProfileName(TEXT("BlockAll"));
			Mesh->SetCollisionResponseToChannel(LSCollisionChannels::ObstacleProjectile, ECR_Block);
			Mesh->SetCanEverAffectNavigation(true);
			Opponent = GetWorld()->SpawnActor<ALSBasicAttackTestActor>(Start + FVector(800, 0, 0), FRotator::ZeroRotator, Spawn);
			Check(Opponent.IsValid(), TEXT("passive actual clear-shot target spawned"));
			if (!Opponent.IsValid()) { Finish(); return; }
			FLSHealthSnapshot InitialHealth;
			Check(Opponent->HealthComponent->TryGetHealth(InitialHealth) && !InitialHealth.bIsDead && InitialHealth.CurrentHealth > 0,
				TEXT("scenario target has observed initialized living health"));
			if (Failures) { Finish(); return; }
			OpponentInitialHealth = InitialHealth.CurrentHealth;
			Opponent->HealthComponent->OnDamageCommitted.AddUObject(this, &ThisClass::OnClearShotDamageCommitted);
			Unit->BasicAttack->OnAttackExecutedEvent.AddUniqueDynamic(this, &ThisClass::OnShotExecuted);
			Unit->BasicAttack->OnProjectileLaunched.AddUniqueDynamic(this, &ThisClass::OnShotProjectileLaunched);
			Check(Unit->BasicAttack->OnAttackExecutedEvent.IsAlreadyBound(this, &ThisClass::OnShotExecuted) &&
				Unit->BasicAttack->OnProjectileLaunched.IsAlreadyBound(this, &ThisClass::OnShotProjectileLaunched),
				TEXT("execution and projectile observers bound before scenario starts"));
			FLSBasicAttackSettings AttackSettings = Unit->BasicAttack->GetAttackSettings();
			AttackSettings.Damage = 1;
			AttackSettings.Interval = .2f;
			Check(Unit->BasicAttack->ConfigureAttack(AttackSettings), TEXT("bounded repeat archer damage configured"));
			FLSProjectileAttackSettings Projectile = Unit->BasicAttack->GetProjectileSettings();
			Projectile.ProjectileClass = ALSBasicProjectile::StaticClass();
			Check(Unit->BasicAttack->ConfigureProjectile(Projectile), TEXT("actual archer projectile configured"));
			Stage = 40;
		}
		else if (Case == TEXT("Detour"))
		{
			Start = FVector(-1700, -1000, 88);
			Unit->SetActorLocation(Start);
			Unit->Behavior->ChaseRadius = 1000;
			AStaticMeshActor* Wall = GetWorld()->SpawnActor<AStaticMeshActor>(FVector(-1300, -1000, 200), FRotator::ZeroRotator, Spawn);
			Check(Wall != nullptr, TEXT("runtime navigation obstacle spawned"));
			if (!Wall) { Finish(); return; }
			Blocker = Wall;
			Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Wall->SetActorScale3D(FVector(1, 27, 4));
			Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Wall->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
			Opponent = GetWorld()->SpawnActor<ALSBasicAttackTestActor>(FVector(-900, -1000, 88), FRotator::ZeroRotator, Spawn);
			Stage = 1;
		}
		else if (Case == TEXT("Return"))
		{
			Start = Unit->Behavior->GetInitialPosition();
			const FTransform Transform(Start);
			ALSTroopUnit* HomeBlocker = GetWorld()->SpawnActorDeferred<ALSTroopUnit>(ALSTroopUnit::StaticClass(), Transform,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			Check(HomeBlocker != nullptr, TEXT("occupied return area actor spawned"));
			if (!HomeBlocker) { Finish(); return; }
			HomeBlocker->SetInitialFaction(ELSFaction::Enemy);
			HomeBlocker->SetInitialTroopType(ELSTroopType::Cavalry);
			HomeBlocker->FinishSpawning(Transform);
			HomeBlocker->Orders->SetComponentTickEnabled(false);
			HomeBlocker->BasicAttack->Deactivate();
			HomeBlocker->GetCharacterMovement()->DisableMovement();
			Blocker = HomeBlocker;
			Unit->SetActorLocation(Core->GetActorLocation() + FVector(Core->GetDefenseRadius() + 100, 0, -12));
			Unit->Behavior->SetComponentTickEnabled(true);
			Stage = 10;
		}
		else if (Case == TEXT("AssaultBand"))
		{
			Unit->SetActorLocation(FVector(-1700, -1000, 88));
			Opponent = GetWorld()->SpawnActor<ALSBasicAttackTestActor>(Unit->GetActorLocation() + FVector(500, 0, 0), FRotator::ZeroRotator, Spawn);
			Check(Opponent.IsValid(), TEXT("passive local opponent available for actual assault chase"));
			if (!Opponent.IsValid()) { Finish(); return; }
			Unit->Behavior->SetComponentTickEnabled(true);
			Stage = 30;
		}
		else
		{
			Unit->SetActorLocation(Core->GetActorLocation() + FVector(0, 400, -12));
			Unit->Behavior->FailedPathRetryInterval = 3;
			Unit->Behavior->SetComponentTickEnabled(true);
			Stage = 20;
		}
		StageTime = 0;
		return;
	}
	if (!Unit.IsValid() || !Core.IsValid()) { Check(false, TEXT("regression actors remain alive")); Finish(); return; }
	ULSEnemyBehaviorComponent* Behavior = Unit->Behavior;
	const ELSEnemyState State = Behavior->GetState();
	if (Stage == 40)
	{
		if (StageTime < 2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())) return;
		Check(ULSCombatTargetLibrary::GetAttackDistance(Unit.Get(), Opponent.Get()) <= Unit->BasicAttack->GetAttackSettings().Range,
			TEXT("archer starts inside physical attack range"));
		Check(!Unit->BasicAttack->HasClearAttackPathFromLocation(Start, Opponent.Get()),
			TEXT("terrain wall really blocks the initial projectile corridor"));
		FVector Goal;
		const bool bGoal = FLSEnemyRegressionTestAccess::FindShotPosition(Behavior, Opponent.Get(), Goal);
		const float ActorHeight = Start.Z - Unit->GetNavAgentLocation().Z;
		Check(bGoal && FVector::Dist2D(Goal, Start) > Behavior->ArrivalTolerance &&
			Unit->BasicAttack->HasClearAttackPathFromLocation(Goal + FVector(0, 0, ActorHeight), Opponent.Get()),
			TEXT("bounded actual NavMesh candidate changes position and provides a clear shot"));
		Check(!Behavior->IsComponentTickEnabled() && !Unit->BasicAttack->IsActive() &&
			!Unit->GetCharacterMovement()->IsComponentTickEnabled() &&
			FVector::Dist2D(Start, Unit->GetActorLocation()) <= 1 &&
			Opponent->GetCurrentHealth() == OpponentInitialHealth && !Opponent->HealthComponent->HasTakenDamage(),
			TEXT("setup wait preserves frozen archer and untouched observed target health"));
		if (Failures) { Finish(); return; }
		Unit->BasicAttack->Activate(true);
		Unit->GetCharacterMovement()->SetComponentTickEnabled(true);
		Behavior->SetComponentTickEnabled(true);
		Stage = 41; StageTime = 0;
	}
	else if (Stage == 41)
	{
		++Samples;
		bObservedAttackReposition |= FVector::Dist2D(Start, Unit->GetActorLocation()) > Behavior->ArrivalTolerance;
		const FVector DomainCenter = Behavior->Role == ELSEnemyRole::Guard
			? (Behavior->GuardAnchor ? Behavior->GuardAnchor->GetActorLocation() : Core->GetActorLocation())
			: Start;
		const float DomainRadius = Behavior->Role == ELSEnemyRole::Guard
			? (Behavior->GuardAnchor ? Behavior->GuardAnchorRadius : Core->GetDefenseRadius())
			: Behavior->ChaseRadius;
		MaximumRadius = FMath::Max(MaximumRadius, static_cast<float>(FVector::Dist2D(Unit->GetActorLocation(), DomainCenter)));
		bInvalidState |= MaximumRadius > DomainRadius + Behavior->BoundaryAllowance;
		if (OwnCommittedDamage <= 0 && StageTime < 12) return;
		FLSDamageResult LastDamage;
		const bool bOwnLastDamage = Opponent->HealthComponent->TryGetLastDamage(LastDamage) &&
			LastDamage.Source.Get() == Unit.Get() && LastDamage.bHasSourceFaction && LastDamage.SourceFaction == ELSFaction::Enemy;
		const FString DamageResult = FString::Printf(
			TEXT("actual archer reposition and correlated projectile damage: initialHP=%.2f currentHP=%.2f ownDamage=%.2f execution=%lld otherDamage=%d"),
			OpponentInitialHealth, Opponent->GetCurrentHealth(), OwnCommittedDamage, DamagingExecutionId, OtherDamageRecords);
		Check(bObservedAttackReposition && OwnCommittedDamage > 0 && DamagingExecutionId > 0 && bOwnLastDamage, *DamageResult);
		Check(!bInvalidState, TEXT("all actual reposition samples preserve the assigned guard or assault domain"));
		const FString LaunchResult = FString::Printf(
			TEXT("actual launch snapshots have clear corridors: callbacks=%d clear=%d blocked=%d source=%s target=%s currentSource=%s"),
			ObservedShotCallbacks, ClearShotLaunches, BlockedShotLaunches, *LastShotSource.ToString(), *LastShotTarget.ToString(), *Unit->GetActorLocation().ToString());
		Check(ClearShotLaunches > 0 && BlockedShotLaunches == 0, *LaunchResult);
		Finish();
	}
	else if (Stage == 1)
	{
		if (StageTime < 2 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())) { return; }
		UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(GetWorld(), Unit->GetNavAgentLocation(), Opponent->GetActorLocation(), Unit.Get()) : nullptr;
		float PathRadius = 0;
		if (Path) { for (const FVector& Point : Path->PathPoints) { PathRadius = FMath::Max(PathRadius, static_cast<float>(FVector::Dist2D(Point, Start))); } }
		Check(Path && Path->IsValid() && !Path->IsPartial() && PathRadius > Behavior->ChaseRadius &&
			FVector::Dist2D(Start, Opponent->GetActorLocation()) < Behavior->ChaseRadius,
			TEXT("actual complete NavMesh route detours outside provisional disk despite inside endpoint"));
		if (Failures) { Finish(); return; }
		Unit->Behavior->SetComponentTickEnabled(true);
		Stage = 2; StageTime = 0;
	}
	else if (Stage == 2)
	{
		++Samples;
		bInvalidState |= State == ELSEnemyState::MarchRecovery || Behavior->GetTarget() == Opponent.Get() || State == ELSEnemyState::Engaging;
		if (StageTime < 1) { return; }
		Check(!bInvalidState && Samples >= 2, TEXT("detour candidate refused without engagement latch or MarchRecovery"));
		Check(FVector::Dist2D(Start, Unit->GetActorLocation()) > 20, TEXT("rejected candidate leaves actual strategic march progressing"));
		Finish();
	}
	else if (Stage == 10)
	{
		if (State != ELSEnemyState::Returning && State != ELSEnemyState::ReturnBlocked) { return; }
		Check(true, TEXT("hard boundary caused actual return state"));
		Unit->SetActorLocation(Start + FVector(-400, 0, 0));
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Opponent = GetWorld()->SpawnActor<ALSBasicAttackTestActor>(Unit->GetActorLocation() + FVector(100, 0, 0), FRotator::ZeroRotator, Spawn);
		Check(Opponent.IsValid(), TEXT("passive in-range return opponent spawned"));
		if (!Opponent.IsValid()) { Finish(); return; }
		Opponent->HealthComponent->OnDamagedEvent.AddDynamic(this, &ThisClass::OnOpponentDamaged);
		Stage = 11; StageTime = 0;
	}
	else if (Stage == 11)
	{
		++Samples;
		bInvalidState |= State != ELSEnemyState::Returning && State != ELSEnemyState::ReturnBlocked;
		if (!bDamagedDuringReturn && StageTime < 4) { return; }
		Check(bDamagedDuringReturn && !bInvalidState, TEXT("actual damage executes while return state stays active"));
		Opponent->SetActorLocation(Start + FVector(1000, 0, 0));
		Center = Unit->GetActorLocation();
		Stage = 12; StageTime = 0;
	}
	else if (Stage == 12)
	{
		++Samples;
		bInvalidState |= State != ELSEnemyState::Returning && State != ELSEnemyState::ReturnBlocked;
		MaximumRadius = FMath::Max(MaximumRadius, static_cast<float>(FVector::Dist2D(Unit->GetActorLocation(), Core->GetActorLocation())));
		if (StageTime < 2) { return; }
		Check(!bInvalidState && Behavior->GetTarget() != Opponent.Get() && !Unit->BasicAttack->GetAttackTarget(), TEXT("return does not start fresh chase when previous opponent leaves range"));
		Check(FVector::Dist2D(Unit->GetActorLocation(), Start) <= FVector::Dist2D(Center, Start) + 20 &&
			MaximumRadius <= Core->GetDefenseRadius() + Behavior->BoundaryAllowance,
			TEXT("actual return movement stays toward home and inside allowed defense domain"));
		Blocker->Destroy();
		Stage = 13; StageTime = 0;
	}
	else if (Stage == 13)
	{
		if (StageTime < 3) { return; }
		UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_RETURN_DIAGNOSTIC pose={Pose} home={Home} distance={Distance} state={State} reason={Reason}",
			("Function", __FUNCTION__), ("Pose", Unit->GetActorLocation().ToString()), ("Home", Start.ToString()),
			("Distance", FVector::Dist2D(Unit->GetActorLocation(), Start)), ("State", static_cast<int32>(State)),
			("Reason", Behavior->GetLastStopReason()));
		Check(FVector::Dist2D(Unit->GetActorLocation(), Start) <= Behavior->ReturnAreaRadius, TEXT("dynamic removal of home obstruction permits actual return arrival"));
		Finish();
	}
	else if (Stage == 30)
	{
		if (State != ELSEnemyState::Engaging || Behavior->GetTarget() != Opponent.Get()) { return; }
		Center = Behavior->GetEngagementCenter();
		Check(FVector::Dist2D(Center, Opponent->GetActorLocation()) < Behavior->ChaseRadius,
			TEXT("actual local chase established original engagement center"));
		Unit->SetActorLocation(Center + FVector(Behavior->ChaseRadius + 10, 0, 0));
		Start = Unit->GetActorLocation();
		const float ForcedRadius = FVector::Dist2D(Start, Center);
		Check(ForcedRadius > Behavior->ChaseRadius && ForcedRadius <= Behavior->ChaseRadius + Behavior->BoundaryAllowance,
			TEXT("actual forced assault center lies strictly inside soft boundary band"));
		// 기존 추적 이동이 경계 판단보다 먼저 안쪽으로 들어가지 않도록 한 판단 동안만 이동 틱을 멈춘다.
		Unit->GetCharacterMovement()->SetComponentTickEnabled(false);
		UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_ASSAULT_BAND_STIMULUS pose={Pose} center={Center} radius={Radius}",
			("Function", __FUNCTION__), ("Pose", Start.ToString()), ("Center", Center.ToString()), ("Radius", ForcedRadius));
		Stage = 31; StageTime = 0;
	}
	else if (Stage == 31)
	{
		++Samples;
		const float Radius = FVector::Dist2D(Unit->GetActorLocation(), Center);
		MaximumRadius = FMath::Max(MaximumRadius, Radius);
		if (!bObservedBandReentry)
		{
			if (State == ELSEnemyState::ReenteringDomain)
			{
				bObservedBandReentry = true;
				Unit->GetCharacterMovement()->SetComponentTickEnabled(true);
				StageTime = 0;
			}
			else if (StageTime > 1)
			{
				Unit->GetCharacterMovement()->SetComponentTickEnabled(true);
				Check(false, TEXT("assault boundary decision observed forced soft band before movement resumes"));
				Finish();
				return;
			}
		}
		bInvalidState |= State == ELSEnemyState::MarchRecovery || State == ELSEnemyState::Returning ||
			State == ELSEnemyState::ReturnBlocked || !Behavior->GetEngagementCenter().Equals(Center, 1);
		if (StageTime < 4) { return; }
		UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_ASSAULT_BAND center={Center} actualCenter={ActualCenter} pose={Pose} radius={Radius} maxRadius={Maximum} state={State} sawReentry={SawReentry} invalidState={InvalidState} samples={Samples} reason={Reason}",
			("Function", __FUNCTION__), ("Center", Center.ToString()), ("ActualCenter", Behavior->GetEngagementCenter().ToString()),
			("Pose", Unit->GetActorLocation().ToString()), ("Radius", Radius), ("Maximum", MaximumRadius),
			("State", static_cast<int32>(State)), ("SawReentry", bObservedBandReentry), ("InvalidState", bInvalidState),
			("Samples", Samples), ("Reason", Behavior->GetLastStopReason()));
		Check(bObservedBandReentry && !bInvalidState && Samples >= 2,
			TEXT("assault soft-band correction keeps original chase center without recovery or full return for four seconds"));
		Check(Radius <= Behavior->ChaseRadius && FVector::Dist2D(Start, Unit->GetActorLocation()) > 10 &&
			MaximumRadius <= Behavior->ChaseRadius + Behavior->BoundaryAllowance,
			TEXT("assault actually moved back inside original chase disk after soft overshoot"));
		Unit->SetActorLocation(Center + FVector(Behavior->ChaseRadius + Behavior->BoundaryAllowance + 20, 0, 0));
		Stage = 32; StageTime = 0;
	}
	else if (Stage == 32)
	{
		if (State != ELSEnemyState::MarchRecovery && StageTime < 2) { return; }
		Check(State == ELSEnemyState::MarchRecovery && Behavior->GetEngagementCenter().Equals(Center, 1),
			TEXT("hard assault overshoot starts actual MarchRecovery and preserves original center"));
		Finish();
	}
	else if (Stage == 20)
	{
		if (Unit->GetVelocity().Size2D() < 20 || !Unit->GetController()) { return; }
		// 실제 진행 중 요청을 외부 컨트롤러 API로 중단해 경로 완료 실패 통지를 받는다.
		Unit->GetController()->StopMovement();
		RetryDeadline = FLSEnemyRegressionTestAccess::RetryDeadline(Behavior);
		Check(RetryDeadline > GetWorld()->GetTimeSeconds(), TEXT("actual active path abort scheduled future retry"));
		if (Failures) { Finish(); return; }
		Stage = 21; StageTime = 0;
	}
	else if (Stage == 21)
	{
		if (StageTime < 0.4f) { return; }
		FLSUnitStatRow Stats;
		Check(Unit->TryGetStats(Stats), TEXT("support receiver stats available"));
		const float Distance = FMath::Min(Stats.DetectionRange - 100, Behavior->ChaseRadius - 100);
		Check(Distance > Unit->BasicAttack->GetAttackSettings().Range && Distance < Behavior->ChaseRadius,
			TEXT("reported support lies outside firing range and inside latched chase disk"));
		if (Failures) { Finish(); return; }
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Opponent = GetWorld()->SpawnActor<ALSBasicAttackTestActor>(Unit->GetActorLocation() + FVector(Distance, 0, 0), FRotator::ZeroRotator, Spawn);
		Check(Opponent.IsValid(), TEXT("remote support attacker spawned"));
		if (!Opponent.IsValid()) { Finish(); return; }
		const auto Damage = Core->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(ULSHealthComponent::MakeDamageRequest(1, Opponent.Get(), Opponent.Get()));
		Check(Damage.AppliedDamage > 0, TEXT("real nonfatal core damage emitted support during prior path retry wait"));
		Opponent->SetActorLocation(FVector(3900, 3200, 88));
		Check(FVector::Dist(Unit->GetActorLocation(), Opponent->GetActorLocation()) > Stats.DetectionRange,
			TEXT("actual opponent moved beyond detection before receiver selects saved support position"));
		Start = Unit->GetActorLocation();
		Stage = 22; StageTime = 0;
	}
	else if (Stage == 22)
	{
		++Samples;
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now < RetryDeadline)
		{
			const bool bHasSupport = FLSEnemyRegressionTestAccess::HasSupport(Behavior);
			if (bHasSupport)
			{
				if (!bSupportObserved) { Center = Behavior->GetEngagementCenter(); }
				if (!bSupportObserved) { FLSEnemyRegressionTestAccess::LogState(Behavior, Start, Center, RetryDeadline); }
				bSupportObserved = true;
			}
			if (bSupportObserved)
			{
				bInvalidState |= !bHasSupport || State != ELSEnemyState::Supporting || !Behavior->GetEngagementCenter().Equals(Center, 1);
			}
			return;
		}
		bResumedSupportMovement |= FLSEnemyRegressionTestAccess::HasSupport(Behavior) && State == ELSEnemyState::Supporting &&
			Unit->GetActorLocation().X > Start.X + 20 && Behavior->GetEngagementCenter().Equals(Center, 1);
		if (Now < RetryDeadline + 1) { return; }
		FLSEnemyRegressionTestAccess::LogState(Behavior, Start, Center, RetryDeadline);
		Check(bSupportObserved && !bInvalidState, TEXT("valid support stayed active with fixed chase center during prior retry wait"));
		Check(bResumedSupportMovement,
			TEXT("same active support resumes actual movement after retry deadline"));
		Finish();
	}
}
