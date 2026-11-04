#include "Match/Tests/LSBattleOutcomeNavigationProbe.h"

#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Castle/LSCastle.h"
#include "Combat/LSHealthComponent.h"
#include "Combat/Tests/LSBasicAttackTestActor.h"
#include "Controls/LSOrderComponent.h"
#include "Enemies/LSBattlefieldSetup.h"
#include "Enemies/LSEnemyTroop.h"
#include "Enemies/LSEnemyOutpost.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Enemies/Tests/LSEnemySupportTestAccess.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Match/LSGameState.h"

namespace
{
template<class T> T* SpawnNavigationFixture(UWorld* World, FVector Position)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<T>(Position, FRotator::ZeroRotator, Params);
}
UPathFollowingComponent* NavigationPath(ALSTroopUnit* Unit)
{
	AAIController* Controller = Unit ? Cast<AAIController>(Unit->GetController()) : nullptr;
	return Controller ? Controller->GetPathFollowingComponent() : nullptr;
}
bool ActiveNavigation(ALSTroopUnit* Unit)
{
	UPathFollowingComponent* Path = NavigationPath(Unit);
	return Path && Path->GetStatus() == EPathFollowingStatus::Moving && Path->GetPath().IsValid()
		&& Path->GetCurrentRequestId() != FAIRequestID::InvalidRequest;
}
}

ALSBattleOutcomeNavigationProbe::ALSBattleOutcomeNavigationProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
}

ALSBattleOutcomeNavigationProbe* ULSBattleOutcomeNavigationTestLibrary::StartNavigationProbe(AActor* WorldActor, bool bVictory)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!WorldActor || !WorldActor->GetWorld() || !WorldActor->GetWorld()->IsGameWorld()) return nullptr;
	ALSBattleOutcomeNavigationProbe* Probe = SpawnNavigationFixture<ALSBattleOutcomeNavigationProbe>(WorldActor->GetWorld(), FVector::ZeroVector);
	if (Probe) Probe->bVictory = bVictory;
	return Probe;
#else
	return nullptr;
#endif
}

void ALSBattleOutcomeNavigationProbe::Check(bool Passed, const TCHAR* Message)
{
	Failures += Passed ? 0 : 1;
	const FString Line = FString::Printf(TEXT("OUTCOME_NAV %s: %s"), Passed ? TEXT("PASS") : TEXT("FAIL"), Message);
	Report += Line + TEXT("\n");
	UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
}

void ALSBattleOutcomeNavigationProbe::Finish()
{
	Report += FString::Printf(TEXT("OUTCOME_NAV_FINISHED victory=%d failures=%d samples=%d"), bVictory, Failures, Samples);
	UE_LOG(LogTemp, Display, TEXT("%s"), *Report);
	SetActorTickEnabled(false);
}

void ALSBattleOutcomeNavigationProbe::SendSupport()
{
	Opponent->SetActorLocation(FVector(400, -2500, 88));
	const auto Damage = Requester->FindComponentByClass<ULSHealthComponent>()->ApplyDamage(
		ULSHealthComponent::MakeDamageRequest(1, Opponent.Get(), Opponent.Get()));
	Check(Damage.AppliedDamage > 0, TEXT("real damage with ally source applied to requester"));
	Opponent->SetActorLocation(FVector(3800, 3200, 88));
}

void ALSBattleOutcomeNavigationProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if WITH_DEV_AUTOMATION_TESTS
	Elapsed += DeltaSeconds;
	StageTime += DeltaSeconds;
	if (Elapsed > 15) { Check(false, TEXT("bounded actual navigation timeout")); Finish(); return; }
	if (Stage == 0)
	{
		if (StageTime < 0.5f || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())) return;
		for (TActorIterator<ALSBattlefieldSetup> It(GetWorld()); It; ++It) Setup = *It;
		ALSGameState* State = GetWorld()->GetGameState<ALSGameState>();
		Check(Setup.IsValid() && State && State->IsBattleOutcomeEnabled() && !State->HasBattleEnded(), TEXT("authored setup and active outcome authority"));
		if (Failures) { Finish(); return; }
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (auto* Behavior = It->FindComponentByClass<ULSEnemyBehaviorComponent>()) Behavior->SetComponentTickEnabled(false);
			if (auto* Attack = It->FindComponentByClass<ULSBasicAttackComponent>()) { Attack->SetComponentTickEnabled(false); Attack->StopAttack(); }
			if (auto* Orders = It->FindComponentByClass<ULSOrderComponent>()) Orders->SetComponentTickEnabled(false);
			if (auto* Pawn = Cast<APawn>(*It); Pawn && Pawn->GetController()) Pawn->GetController()->StopMovement();
		}
		// UI 검사용 성 배치가 진격 관찰 전에 교전으로 전환시키지 않도록 PIE 복사본만 옮긴다.
		Check(Setup->PlayerCastle->SetActorLocation(FVector(-1800, -1200, 142)), TEXT("PIE castle placed at distant navigation fixture goal"));
		Friendly = SpawnNavigationFixture<ALSTroopUnit>(GetWorld(), FVector(-2500, 1500, 88));
		Marcher = SpawnNavigationFixture<ALSEnemyTroop>(GetWorld(), FVector(1800, 800, 88));
		Supporter = SpawnNavigationFixture<ALSEnemyTroop>(GetWorld(), FVector(-500, -2500, 88));
		Retry = SpawnNavigationFixture<ALSEnemyTroop>(GetWorld(), FVector(2500, 2300, 88));
		Requester = SpawnNavigationFixture<ALSEnemyOutpost>(GetWorld(), FVector(-500, -2200, 100));
		Opponent = SpawnNavigationFixture<ALSBasicAttackTestActor>(GetWorld(), FVector(3800, 3200, 88));
		Check(Friendly.IsValid() && Marcher.IsValid() && Supporter.IsValid() && Retry.IsValid() && Requester.IsValid() && Opponent.IsValid(), TEXT("native navigation fixtures spawned"));
		if (Failures) { Finish(); return; }
		Requester->Behavior->SetComponentTickEnabled(false);
		Requester->BasicAttack->StopAttack();
		for (ALSEnemyTroop* Unit : {Marcher.Get(), Supporter.Get(), Retry.Get()}) Unit->Behavior->Role = ELSEnemyRole::Assault;
		Retry->Behavior->FailedPathRetryInterval = 1.5f;
		for (ALSTroopUnit* Unit : {Friendly.Get(), static_cast<ALSTroopUnit*>(Marcher.Get()), static_cast<ALSTroopUnit*>(Supporter.Get()), static_cast<ALSTroopUnit*>(Retry.Get())}) Starts.Add(Unit->GetActorLocation());
		FLSOrder Move;
		Move.Kind = ELSOrderKind::Move;
		Move.Destination = FVector(-800, 1500, 88);
		Check(Friendly->Orders->SubmitOrder(Move), TEXT("public friendly Move accepted"));
		Move.Destination = FVector(-800, 2400, 88);
		Check(Friendly->Orders->SubmitOrder(Move, true) && Friendly->Orders->GetQueuedCount() == 1, TEXT("real follow-up movement queued"));
		SendSupport();
		Check(FLSEnemySupportTestAccess::Pending(Supporter->Behavior) > 0, TEXT("actual damage callback populated native support inbox"));
		Stage = 1; StageTime = 0;
	}
	else if (Stage == 1)
	{
		if (StageTime >= NextDiagnosticTime)
		{
			NextDiagnosticTime = StageTime + 0.5f;
			TArray<ALSTroopUnit*> DiagnosticUnits = {Friendly.Get(), Marcher.Get(), Supporter.Get(), Retry.Get()};
			for (int32 Index = 0; Index < DiagnosticUnits.Num(); ++Index)
			{
				ALSTroopUnit* Unit = DiagnosticUnits[Index];
				ULSEnemyBehaviorComponent* Behavior = Unit->FindComponentByClass<ULSEnemyBehaviorComponent>();
				UPathFollowingComponent* Path = NavigationPath(Unit);
				const FString Evidence = FString::Printf(TEXT("OUTCOME_NAV_WAIT time=%.2f unit=%d position=%s displacement=%.2f active=%d pathStatus=%d state=%d support=%d inbox=%d reason=%s"), StageTime, Index,
					*Unit->GetActorLocation().ToString(), FVector::Dist2D(Starts[Index], Unit->GetActorLocation()), ActiveNavigation(Unit),
					Path ? static_cast<int32>(Path->GetStatus()) : -1, Behavior ? static_cast<int32>(Behavior->GetState()) : -1,
					Behavior && FLSEnemySupportTestAccess::HasActive(Behavior), Behavior ? FLSEnemySupportTestAccess::Pending(Behavior) : 0,
					Behavior ? *Behavior->GetLastStopReason() : TEXT("friendly"));
				Report += Evidence + TEXT("\n");
				UE_LOG(LogTemp, Display, TEXT("%s"), *Evidence);
			}
		}
		const bool Ready = ActiveNavigation(Friendly.Get()) && ActiveNavigation(Marcher.Get()) && ActiveNavigation(Supporter.Get()) && ActiveNavigation(Retry.Get())
			&& Marcher->Behavior->GetState() == ELSEnemyState::Marching && Supporter->Behavior->GetState() == ELSEnemyState::Supporting;
		if (!Ready) return;
		TArray<ALSTroopUnit*> Units = {Friendly.Get(), Marcher.Get(), Supporter.Get(), Retry.Get()};
		for (int32 Index = 0; Index < Units.Num(); ++Index) if (FVector::Dist2D(Starts[Index], Units[Index]->GetActorLocation()) < 25) return;
		Check(true, TEXT("friendly Move and enemy March/support/retry all displaced >25cm with active real NavMesh requests"));
		Check(FLSEnemySupportTestAccess::HasActive(Supporter->Behavior) && FLSEnemySupportTestAccess::ActiveRequester(Supporter->Behavior) == Requester.Get(), TEXT("support movement belongs to actual damage requester"));
		for (int32 Index = 0; Index < Units.Num(); ++Index)
		{
			RequestIds.Add(NavigationPath(Units[Index])->GetCurrentRequestId().GetID());
			const FString Evidence = FString::Printf(TEXT("OUTCOME_NAV_BEFORE unit=%d start=%s position=%s displacement=%.2f pathStatus=%d request=%u"), Index,
				*Starts[Index].ToString(), *Units[Index]->GetActorLocation().ToString(), FVector::Dist2D(Starts[Index], Units[Index]->GetActorLocation()),
				static_cast<int32>(NavigationPath(Units[Index])->GetStatus()), RequestIds[Index]);
			Report += Evidence + TEXT("\n");
			UE_LOG(LogTemp, Display, TEXT("%s"), *Evidence);
		}
		Retry->GetController()->StopMovement();
		Check(Retry->Behavior->GetLastStopReason() == TEXT("Path following failed") && !ActiveNavigation(Retry.Get()), TEXT("actual path abort entered native failed-path retry delay"));
		AActor* Terminal = bVictory ? static_cast<AActor*>(Setup->CoreOutpost.Get()) : Setup->PlayerCastle.Get();
		FLSHealthSnapshot Health;
		auto* Component = Terminal->FindComponentByClass<ULSHealthComponent>();
		Check(Component && Component->TryGetHealth(Health) && Component->ApplyDamage(ULSHealthComponent::MakeDamageRequest(Health.CurrentHealth, nullptr, nullptr)).AppliedDamage > 0, TEXT("real lethal terminal damage"));
		Stage = 2; StageTime = 0;
	}
	else if (Stage == 2)
	{
		ALSGameState* State = GetWorld()->GetGameState<ALSGameState>();
		if (!State->HasBattleEnded()) return;
		Check(State->GetBattleOutcome() == (bVictory ? ELSBattleOutcome::Victory : ELSBattleOutcome::Defeat), TEXT("requested result finalized"));
		TArray<ALSTroopUnit*> Units = {Friendly.Get(), Marcher.Get(), Supporter.Get(), Retry.Get()};
		for (int32 Index = 0; Index < Units.Num(); ++Index)
		{
			Stopped.Add(Units[Index]->GetActorLocation());
			// 종료 전에 실제로 실행한 요청의 늦은 완료 통지를 전달한다.
			NavigationPath(Units[Index])->OnRequestFinished.Broadcast(FAIRequestID(RequestIds[Index]), FPathFollowingResult(EPathFollowingResult::Success));
		}
		FLSOrder Move;
		Move.Kind = ELSOrderKind::Move; Move.Destination = FVector(0, 1500, 88);
		Check(!Friendly->Orders->SubmitOrder(Move) && !Friendly->Orders->SubmitOrder(Move, true), TEXT("fresh and queued orders rejected after finalization"));
		SendSupport();
		Stage = 3; StageTime = 0;
	}
	else if (Stage == 3)
	{
		++Samples;
		TArray<ALSTroopUnit*> Units = {Friendly.Get(), Marcher.Get(), Supporter.Get(), Retry.Get()};
		for (int32 Index = 0; Index < Units.Num(); ++Index)
		{
			bStable &= Units[Index]->GetActorLocation().Equals(Stopped[Index], 1.0f) && NavigationPath(Units[Index])->GetStatus() == EPathFollowingStatus::Idle;
			bStable &= Units[Index]->Orders->GetOrderKind() == ELSOrderKind::Idle && Units[Index]->Orders->GetQueuedCount() == 0;
			bStable &= Units[Index]->BasicAttack->GetAttackTarget() == nullptr;
		}
		for (ALSEnemyTroop* Unit : {Marcher.Get(), Supporter.Get(), Retry.Get()})
			bStable &= Unit->Behavior->GetState() == ELSEnemyState::Ended && !FLSEnemySupportTestAccess::HasActive(Unit->Behavior) && FLSEnemySupportTestAccess::Pending(Unit->Behavior) == 0;
		bStable &= GetWorld()->GetGameState<ALSGameState>()->GetBattleOutcome() == (bVictory ? ELSBattleOutcome::Victory : ELSBattleOutcome::Defeat);
		// 배경 에디터의 저프레임에서도 시간과 표본 수를 모두 채운 뒤 판정한다.
		if (StageTime < 2.5f || Samples < 10) return;
		for (int32 Index = 0; Index < Units.Num(); ++Index)
		{
			const FString Evidence = FString::Printf(TEXT("OUTCOME_NAV_AFTER unit=%d stopped=%s position=%s elapsed=%.2f pathStatus=%d queue=%d"), Index,
				*Stopped[Index].ToString(), *Units[Index]->GetActorLocation().ToString(), StageTime,
				static_cast<int32>(NavigationPath(Units[Index])->GetStatus()), Units[Index]->Orders->GetQueuedCount());
			Report += Evidence + TEXT("\n");
			UE_LOG(LogTemp, Display, TEXT("%s"), *Evidence);
		}
		Check(bStable && Samples >= 10, TEXT("positions, paths, orders, support and result stayed stopped across 2.5 seconds beyond retry deadline and stale completion delivery"));
		Finish();
	}
#else
	Check(false, TEXT("development automation build required")); Finish();
#endif
}
