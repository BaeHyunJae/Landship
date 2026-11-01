#include "Enemies/Tests/LSEnemyBoundaryProbe.h"

#include "Castle/LSMobileCastle.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Controls/LSOrderComponent.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Enemies/LSEnemyTroop.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "HAL/PlatformMisc.h"
#include "Logging/StructuredLog.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NavigationSystem.h"

ALSEnemyBoundaryProbe::ALSEnemyBoundaryProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
}

void ALSEnemyBoundaryProbe::BeginPlay()
{
	Super::BeginPlay();
	bDense = FParse::Param(FCommandLine::Get(), TEXT("LSEnemyDenseMarch"));
	bMovingCastle = FParse::Param(FCommandLine::Get(), TEXT("LSEnemyMovingCastle"));
	if (!bDense && !bMovingCastle && !FParse::Param(FCommandLine::Get(), TEXT("LSEnemyBoundaryTestExit")))
	{
		SetActorTickEnabled(false);
		Destroy();
	}
}

void ALSEnemyBoundaryProbe::Freeze(ALSEnemyTroop* Unit)
{
	Unit->Behavior->SetComponentTickEnabled(false);
	Unit->BasicAttack->StopAttack();
	if (AController* Controller = Unit->GetController()) { Controller->StopMovement(); }
}

void ALSEnemyBoundaryProbe::Check(bool bPassed, const TCHAR* Message)
{
	Failures += bPassed ? 0 : 1;
	UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_PROBE {Result}: {Message}",
		("Function", __FUNCTION__), ("Result", bPassed ? TEXT("PASS") : TEXT("FAIL")), ("Message", Message));
}

void ALSEnemyBoundaryProbe::Finish()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("LSEnemyBoundaryLowFPS")))
	{
		Check(MaximumDelta >= 0.18f, TEXT("boundary test observed actual low-frame-rate delta"));
	}
	UE_LOGFMT(LogTemp, Display, "[{Function}] {Marker} failures={Failures} maxActualRadius={Radius} maxDelta={Delta}",
		("Function", __FUNCTION__), ("Marker", bMovingCastle ? TEXT("ENEMY_MOVING_FINISHED") :
			(bDense ? TEXT("ENEMY_DENSE_FINISHED") : TEXT("ENEMY_BOUNDARY_FINISHED"))),
		("Failures", Failures), ("Radius", MaximumRadius), ("Delta", MaximumDelta));
	SetActorTickEnabled(false);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void ALSEnemyBoundaryProbe::OnCastleDamaged(const FLSDamageResult& Damage)
{
	if (Damage.AppliedDamage > 0 && IsValid(Damage.SourceActor) &&
		(!bMovingCastle || (Castle.IsValid() && Castle->GetVelocity().Size2D() > 100)))
	{
		DamageSources.Add(Damage.SourceActor);
	}
}

void ALSEnemyBoundaryProbe::OnExecuted(const FLSAttackExecution& Execution)
{
	if (Execution.Target.Get() == Castle.Get() &&
		(!bMovingCastle || (Castle.IsValid() && Castle->GetVelocity().Size2D() > 100)))
	{
		ExecutionSources.Add(Execution.Source);
	}
}

void ALSEnemyBoundaryProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	MaximumDelta = FMath::Max(MaximumDelta, DeltaSeconds);
	StageTime += DeltaSeconds;
	TotalTime += DeltaSeconds;
	if (TotalTime > (bMovingCastle ? 20 : 35)) { Check(false, TEXT("bounded probe timeout")); Finish(); return; }
	if (Stage == 0)
	{
		if (StageTime < 1) { return; }
		for (TActorIterator<ALSMobileCastle> It(GetWorld()); It; ++It) { Castle = *It; }
		for (TActorIterator<ALSEnemyOutpost> It(GetWorld()); It; ++It) { Core = *It; }
		for (TActorIterator<ALSEnemyTroop> It(GetWorld()); It; ++It)
		{
			Freeze(*It);
			if (It->Behavior->Role == ELSEnemyRole::Assault) { Marchers.Add(*It); }
			else if (It->GetTroopType() == ELSTroopType::Spearman) { Guard = *It; }
		}
		if (!Castle.IsValid() || !Core.IsValid() || !Guard.IsValid() || Marchers.Num() != 2)
		{
			Check(false, TEXT("expected authored scene actors")); Finish(); return;
		}
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		FNavLocation Point;
		if (!Nav || !Nav->ProjectPointToNavigation(Guard->GetActorLocation(), Point))
		{
			if (StageTime < 5) { Marchers.Reset(); return; }
			Check(false, TEXT("navigation ready before stimulus")); Finish(); return;
		}
		Castle->Orders->SetComponentTickEnabled(false);
		Castle->BasicAttack->StopAttack();
		Core->Behavior->SetComponentTickEnabled(false);
		Core->BasicAttack->StopAttack();
		if (bDense || bMovingCastle)
		{
			Castle->SetActorLocation(FVector(bMovingCastle ? -500 : -1500, -1700, 142));
			Castle->FindComponentByClass<ULSHealthComponent>()->OnDamagedEvent.AddDynamic(this, &ThisClass::OnCastleDamaged);
			for (int32 Index = 0; Index < 2; ++Index)
			{
				const FTransform Transform(FVector(0, -1000 + Index * 100, 88));
				ALSEnemyTroop* Unit = GetWorld()->SpawnActorDeferred<ALSEnemyTroop>(ALSEnemyTroop::StaticClass(), Transform,
					nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Unit) { Check(false, TEXT("dense deferred troop spawn")); Finish(); return; }
				Unit->Behavior->Role = ELSEnemyRole::Assault;
				Unit->SetInitialTroopType(ELSTroopType::Spearman);
				Unit->FinishSpawning(Transform);
				Marchers.Add(Unit);
			}
			for (int32 Index = 0; Index < Marchers.Num(); ++Index)
			{
				Marchers[Index]->SetActorLocation(FVector(0, -1200 + Index * 100, 88));
				Starts.Add(Marchers[Index], Marchers[Index]->GetActorLocation());
				Marchers[Index]->BasicAttack->OnAttackExecutedEvent.AddDynamic(this, &ThisClass::OnExecuted);
				Marchers[Index]->Behavior->SetComponentTickEnabled(true);
			}
			if (bMovingCastle)
			{
				Castle->Orders->SetComponentTickEnabled(true);
				FLSOrder Move;
				Move.Kind = ELSOrderKind::Move;
				Move.Destination = FVector(-3800, -1700, 0);
				if (!Castle->Orders->SubmitOrder(Move))
				{
					Check(false, TEXT("moving castle public move order accepted")); Finish(); return;
				}
			}
			Stage = 10; StageTime = 0; return;
		}
		Castle->SetActorLocation(FVector(-2500, -2000, 142));
		Guard->SetActorLocation(Core->GetActorLocation() + FVector(Core->GetDefenseRadius() + 10, 0, -12));
		Guard->Behavior->SetComponentTickEnabled(true);
		Stage = 1; StageTime = 0; return;
	}
	if (!Guard.IsValid() || !Castle.IsValid() || !Core.IsValid()) { Check(false, TEXT("scene lifetime")); Finish(); return; }
	if (Stage == 10)
	{
		bool bAllObserved = true;
		for (const auto& Unit : Marchers)
		{
			if (bMovingCastle && Unit.IsValid() && Unit->Behavior->GetState() == ELSEnemyState::Supporting)
			{
				bStrategicSupportDiversion = true;
			}
			bAllObserved &= Unit.IsValid() && DamageSources.Contains(Unit.Get()) && ExecutionSources.Contains(Unit.Get()) &&
				FVector::Dist2D(Unit->GetActorLocation(), Starts[Unit]) > 300;
		}
		if (bMovingCastle ? StageTime < 12 : (!bAllObserved && StageTime < 20)) { return; }
		if (bMovingCastle)
		{
			Check(Castle->GetVelocity().Size2D() > 100, TEXT("castle still moving under public move order at observation"));
			Check(!bStrategicSupportDiversion, TEXT("castle support reports never divert strategic march to stale investigation"));
		}
		bool bMeleeDamage = false;
		bool bRangedDamage = false;
		for (const auto& Unit : Marchers)
		{
			if (!Unit.IsValid()) { Check(false, TEXT("dense marcher alive")); continue; }
			UE_LOGFMT(LogTemp, Display, "[{Function}] MARCH moving={Moving} unit={Unit} pose={Pose} state={State} targetDistance={Distance} reason={Reason}",
				("Function", __FUNCTION__), ("Unit", Unit->GetName()), ("Pose", Unit->GetActorLocation().ToString()),
				("Moving", bMovingCastle), ("Distance", FVector::Dist(Unit->GetActorLocation(), Castle->GetActorLocation())),
				("State", static_cast<int32>(Unit->Behavior->GetState())), ("Reason", Unit->Behavior->GetLastStopReason()));
			const bool bAttacked = DamageSources.Contains(Unit.Get()) && ExecutionSources.Contains(Unit.Get());
			if (bMovingCastle)
			{
				// 좁은 추격열에서 전원이 동시에 사거리를 확보하는 대형 제어는 요구하지 않는다.
				Check(FVector::Dist2D(Unit->GetActorLocation(), Starts[Unit]) > 300, TEXT("each moving-castle marcher made actual progress"));
				bMeleeDamage |= bAttacked && Unit->GetTroopType() == ELSTroopType::Spearman;
				bRangedDamage |= bAttacked && Unit->GetTroopType() == ELSTroopType::Archer;
			}
			else
			{
				Check(FVector::Dist2D(Unit->GetActorLocation(), Starts[Unit]) > 300 && bAttacked, TEXT("each dense marcher moved, executed and actually damaged castle"));
			}
			Check(!Unit->Behavior->GetLastStopReason().Contains(TEXT("stalled")), TEXT("dense marcher has no movement-stall reason"));
		}
		if (bMovingCastle)
		{
			Check(bMeleeDamage && bRangedDamage, TEXT("both melee and ranged actual execution and damage while castle moves"));
		}
		Finish(); return;
	}
	const float Radius = FVector::Dist2D(Guard->GetActorLocation(), Core->GetActorLocation());
	MaximumRadius = FMath::Max(MaximumRadius, Radius);
	const ELSEnemyState State = Guard->Behavior->GetState();
	if (StageTime >= 1 && Stage <= 3)
	{
		UE_LOGFMT(LogTemp, Display, "[{Function}] BOUNDARY stage={Stage} pose={Pose} radius={Radius} state={State} reason={Reason}",
			("Function", __FUNCTION__), ("Stage", Stage), ("Pose", Guard->GetActorLocation().ToString()),
			("Radius", Radius), ("State", static_cast<int32>(State)), ("Reason", Guard->Behavior->GetLastStopReason()));
	}
	if (Stage == 1 && StageTime >= 1)
	{
		Check(Radius <= Core->GetDefenseRadius() && Radius > Core->GetDefenseRadius() - 150,
			TEXT("soft-band actual correction stays near boundary"));
		Check(State != ELSEnemyState::Returning && State != ELSEnemyState::ReturnBlocked && State != ELSEnemyState::MarchRecovery,
			TEXT("soft-band correction does not become full return or recovery"));
		Guard->SetActorLocation(Core->GetActorLocation() + FVector(Core->GetDefenseRadius() + Guard->Behavior->BoundaryAllowance + 20, 0, -12));
		Stage = 2; StageTime = 0;
	}
	else if (Stage == 2 && StageTime >= 1)
	{
		Check(State == ELSEnemyState::Returning || State == ELSEnemyState::ReturnBlocked ||
			FVector::Dist2D(Guard->GetActorLocation(), Guard->Behavior->GetInitialPosition()) <= Guard->Behavior->ReturnAreaRadius,
			TEXT("hard boundary starts actual home return"));
		Freeze(Guard.Get());
		Guard->SetActorLocation(Guard->Behavior->GetInitialPosition());
		Guard->Behavior->SetComponentTickEnabled(true);
		Stage = 3; StageTime = 0;
	}
	else if (Stage == 3 && StageTime >= 1)
	{
		Check(State == ELSEnemyState::GuardIdle, TEXT("guard restored to actual home idle"));
		ApproachStart = Core->GetActorLocation() + FVector(Core->GetDefenseRadius() - 300, 0, -12);
		Guard->SetActorLocation(ApproachStart);
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Opponent = GetWorld()->SpawnActor<ALSBasicAttackTestActor>(Core->GetActorLocation() + FVector(Core->GetDefenseRadius() + 240, 0, 0), FRotator::ZeroRotator, Spawn);
		if (!Opponent.IsValid()) { Check(false, TEXT("passive outside opponent spawned")); Finish(); return; }
		OpponentHealth = Opponent->GetCurrentHealth();
		MaximumRadius = Radius;
		Stage = 4; StageTime = 0;
	}
	else if (Stage == 4)
	{
		const bool bReentering = State == ELSEnemyState::ReenteringDomain;
		if (bReentering && !bWasReentering) { ++SoftBoundaryEntries; }
		bWasReentering = bReentering;
		bUnexpectedHomeReturn |= State == ELSEnemyState::Returning || State == ELSEnemyState::ReturnBlocked;
		const bool bDamaged = Opponent.IsValid() && Opponent->GetCurrentHealth() < OpponentHealth;
		if (bDamaged && FirstBoundaryDamageTime < 0) { FirstBoundaryDamageTime = StageTime; }
		if (!bDamaged && StageTime < 8) { return; }
		if (bDamaged && StageTime - FirstBoundaryDamageTime < 4) { return; }
		UE_LOGFMT(LogTemp, Display, "[{Function}] BOUNDARY_SUSTAIN entries={Entries} secondsAfterDamage={Duration}",
			("Function", __FUNCTION__), ("Entries", SoftBoundaryEntries), ("Duration", StageTime - FirstBoundaryDamageTime));
		Check(SoftBoundaryEntries <= 1, TEXT("sustained boundary combat does not repeat soft-band round trips"));
		Check(bDamaged && FVector::Dist2D(Guard->GetActorLocation(), ApproachStart) > 100, TEXT("guard approaches and damages legal outside opponent"));
		Check(MaximumRadius <= Core->GetDefenseRadius() + Guard->Behavior->BoundaryAllowance && !bUnexpectedHomeReturn,
			TEXT("outside attack remains inside allowed domain without home-return cycle"));
		Finish();
	}
}
