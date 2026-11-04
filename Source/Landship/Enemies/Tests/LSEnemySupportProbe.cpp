#include "Enemies/Tests/LSEnemySupportProbe.h"

#include "Enemies/Tests/LSEnemySupportTestAccess.h"
#include "Enemies/LSEnemyTroop.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Castle/LSCastle.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "Logging/StructuredLog.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NavigationSystem.h"

ALSEnemySupportProbe::ALSEnemySupportProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
}

void ALSEnemySupportProbe::BeginPlay()
{
	Super::BeginPlay();
	if (!FParse::Param(FCommandLine::Get(), TEXT("LSEnemySupportTestExit")))
	{
		SetActorTickEnabled(false);
		Destroy();
	}
}

void ALSEnemySupportProbe::Check(bool bPassed, const TCHAR* Message)
{
	Failures += bPassed ? 0 : 1;
	UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_SUPPORT {Result}: {Message}",
		("Function", __FUNCTION__), ("Result", bPassed ? TEXT("PASS") : TEXT("FAIL")), ("Message", Message));
}

void ALSEnemySupportProbe::Finish()
{
	UE_LOGFMT(LogTemp, Display, "[{Function}] ENEMY_SUPPORT_FINISHED failures={Failures}",
		("Function", __FUNCTION__), ("Failures", Failures));
	SetActorTickEnabled(false);
	FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
}

void ALSEnemySupportProbe::Report(AActor* Requester, FVector Position)
{
	Opponent->SetActorLocation(Position);
	Requester->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(1, Opponent.Get(), Opponent.Get()));
	// 지원은 마지막 실제 보고 위치를 조사해야 한다. 원격 상대의 이동을 읽으면 안 된다.
	Opponent->SetActorLocation(Origin + FVector(5000, 5000, 0));
}

void ALSEnemySupportProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if WITH_DEV_AUTOMATION_TESTS
	StageTime += DeltaSeconds;
	TotalTime += DeltaSeconds;
	if (TotalTime > 15) { Check(false, TEXT("bounded support scenario timeout")); Finish(); return; }
	if (Stage == 0)
	{
		if (StageTime < 1 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())) { return; }
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (ULSEnemyBehaviorComponent* Behavior = It->FindComponentByClass<ULSEnemyBehaviorComponent>())
			{
				Behavior->SetComponentTickEnabled(false);
			}
			if (ULSBasicAttackComponent* Attack = It->FindComponentByClass<ULSBasicAttackComponent>()) { Attack->StopAttack(); }
			if (APawn* Pawn = Cast<APawn>(*It); Pawn && Pawn->GetController()) { Pawn->GetController()->StopMovement(); }
		}
		Origin = GetActorLocation();
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		OwnOutpost = GetWorld()->SpawnActor<ALSEnemyOutpost>(Origin + FVector(-400, 0, 0), FRotator::ZeroRotator, Spawn);
		NearRequester = GetWorld()->SpawnActor<ALSEnemyTroop>(Origin + FVector(0, 200, 0), FRotator::ZeroRotator, Spawn);
		FarRequester = GetWorld()->SpawnActor<ALSEnemyTroop>(Origin + FVector(0, 400, 0), FRotator::ZeroRotator, Spawn);
		Recipient = GetWorld()->SpawnActor<ALSEnemyTroop>(Origin, FRotator::ZeroRotator, Spawn);
		Opponent = GetWorld()->SpawnActor<ALSBasicAttackTestActor>(Origin + FVector(5000, 5000, 0), FRotator::ZeroRotator, Spawn);
		Check(OwnOutpost.IsValid() && NearRequester.IsValid() && FarRequester.IsValid() && Recipient.IsValid() && Opponent.IsValid(), TEXT("support fixtures spawned"));
		if (!OwnOutpost.IsValid() || !NearRequester.IsValid() || !FarRequester.IsValid() || !Recipient.IsValid() || !Opponent.IsValid()) { Finish(); return; }
		for (AActor* Actor : {static_cast<AActor*>(OwnOutpost.Get()), static_cast<AActor*>(NearRequester.Get()), static_cast<AActor*>(FarRequester.Get()), static_cast<AActor*>(Recipient.Get())})
		{
			Actor->FindComponentByClass<ULSEnemyBehaviorComponent>()->SetComponentTickEnabled(false);
			Actor->FindComponentByClass<ULSBasicAttackComponent>()->StopAttack();
		}
		NearRequester->Behavior->GuardedOutpost = OwnOutpost.Get();
		FarRequester->Behavior->GuardedOutpost = OwnOutpost.Get();
		Recipient->Behavior->Role = ELSEnemyRole::Assault;
		Recipient->Behavior->PendingSupportLifetime = 0.1f;
		Report(FarRequester.Get(), Origin + FVector(600, 0, 0));
		Report(NearRequester.Get(), Origin + FVector(600, 0, 0));
		Recipient->Behavior->SetComponentTickEnabled(true);
		Stage = 1; StageTime = 0;
	}
	else if (Stage == 1 && StageTime > 0.06f)
	{
		Check(FLSEnemySupportTestAccess::HasActive(Recipient->Behavior), TEXT("real NavMesh accepts support"));
		Check(FLSEnemySupportTestAccess::ActiveRequester(Recipient->Behavior) == NearRequester.Get(), TEXT("new selection chooses nearest requester"));
		Anchor = Recipient->Behavior->GetEngagementCenter();
		NearRequester->SetActorLocation(Origin + FVector(0, 350, 0));
		FarRequester->SetActorLocation(Origin + FVector(0, 100, 0));
		Report(FarRequester.Get(), Origin + FVector(600, 50, 0));
		Stage = 2; StageTime = 0;
	}
	else if (Stage == 2 && StageTime > 0.25f)
	{
		Check(FLSEnemySupportTestAccess::HasActive(Recipient->Behavior) && FLSEnemySupportTestAccess::ActiveRequester(Recipient->Behavior) == NearRequester.Get(), TEXT("current support retained despite nearer report and elapsed pending TTL"));
		Check(FVector::Dist2D(Recipient->GetActorLocation(), Origin) > 20, TEXT("active support survives elapsed TTL during actual movement"));
		UpdatedReport = Origin + FVector(900, 30, 0);
		Report(NearRequester.Get(), UpdatedReport);
		UpdateMoveStart = Recipient->GetActorLocation();
		Stage = 4; StageTime = 0;
	}
	else if (Stage == 4 && StageTime > 0.3f)
	{
		Check(FLSEnemySupportTestAccess::HasActive(Recipient->Behavior) && FLSEnemySupportTestAccess::ActiveRequester(Recipient->Behavior) == NearRequester.Get(), TEXT("updated support remains active after subsequent decisions"));
		Check(FLSEnemySupportTestAccess::ActiveLocation(Recipient->Behavior) == UpdatedReport, TEXT("actual fresh report updates active investigation position"));
		Check(Recipient->Behavior->GetEngagementCenter() == Anchor && FVector::Dist2D(Recipient->GetActorLocation(), UpdateMoveStart) > 20,
			TEXT("active report update preserves chase anchor through actual movement and decisions"));
		// 별도 Guard 인스턴스에서 담당 거점 우선순위를 검사한다.
		Recipient->Destroy();
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Recipient = GetWorld()->SpawnActor<ALSEnemyTroop>(Origin, FRotator::ZeroRotator, Spawn);
		Recipient->Behavior->SetComponentTickEnabled(false);
		Recipient->Behavior->GuardedOutpost = OwnOutpost.Get();
		Recipient->GetCharacterMovement()->DisableMovement();
		Recipient->BasicAttack->StopAttack();
		Report(NearRequester.Get(), Origin + FVector(600, 0, 0));
		Report(OwnOutpost.Get(), Origin + FVector(600, 0, 0));
		Recipient->Behavior->SetComponentTickEnabled(true);
		Stage = 3; StageTime = 0;
	}
	else if (Stage == 3 && StageTime > 0.06f)
	{
		Check(FLSEnemySupportTestAccess::HasActive(Recipient->Behavior) && FLSEnemySupportTestAccess::ActiveRequester(Recipient->Behavior) == OwnOutpost.Get(), TEXT("assigned outpost wins simultaneous nearer ordinary report"));
		Finish();
	}
#else
	Check(false, TEXT("development automation support accessor required"));
	Finish();
#endif
}
