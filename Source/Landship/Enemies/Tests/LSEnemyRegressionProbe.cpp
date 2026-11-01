#include "Enemies/Tests/LSEnemyRegressionProbe.h"

#include "AIController.h"
#include "Castle/LSMobileCastle.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Components/StaticMeshComponent.h"
#include "Controls/LSOrderComponent.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Enemies/LSEnemyTroop.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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
		for (TActorIterator<ALSMobileCastle> It(GetWorld()); It; ++It) { Castle = *It; }
		for (TActorIterator<ALSEnemyOutpost> It(GetWorld()); It; ++It) { Core = *It; }
		for (TActorIterator<ALSEnemyTroop> It(GetWorld()); It; ++It)
		{
			const bool bGuard = Case == TEXT("Return");
			const ELSTroopType Type = Case == TEXT("Retry") ? ELSTroopType::Archer : ELSTroopType::Spearman;
			if (It->Behavior->Role == (bGuard ? ELSEnemyRole::Guard : ELSEnemyRole::Assault) && It->GetTroopType() == Type) { Unit = *It; }
		}
		Check(Castle.IsValid() && Core.IsValid() && Unit.IsValid() && Nav &&
			(Case == TEXT("Detour") || Case == TEXT("Return") || Case == TEXT("Retry") || Case == TEXT("AssaultBand")), TEXT("known regression case and authored actors available"));
		if (Failures) { Finish(); return; }
		Castle->Orders->SetComponentTickEnabled(false);
		Castle->BasicAttack->StopAttack();
		Castle->SetActorLocation(FVector(-3000, -2600, 142));
		Core->Behavior->SetComponentTickEnabled(false);
		Core->BasicAttack->StopAttack();
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (Case == TEXT("Detour"))
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
	if (Stage == 1)
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
